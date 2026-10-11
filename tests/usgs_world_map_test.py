#!/usr/bin/env python3
"""Offline global-source handoff regressions; only the Python standard library.

Every raster and split download here is synthetic and tiny. No network, GDAL,
NumPy, Rasterio, zstd, or prepared global data is required. Run directly or via
CTest's usgs_world_map test, including on native Windows.
"""
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.earth.common import sha256_file
from tools.earth.elevation import decode_tile, encode_tile
from tools.earth import pack as pack_module
from tools.earth.pack import build_pack, verify_pack
from tools.usgs_world_map import join_downloads as join
from tools.usgs_world_map import prepare_sources as handoff

REJECT = (ValueError, OSError)


def write_grid(path, values=None, width=8, height=4):
    values = [100.25] * (width * height) if values is None else values
    path.write_bytes(struct.pack("<" + "f" * len(values), *values))
    path.with_suffix(".hdr").write_text(
        "ENVI\n"
        f"samples = {width}\nlines = {height}\nbands = 1\n"
        "header offset = 0\ndata type = 4\ninterleave = bsq\nbyte order = 0\n"
        f"map info = {{Geographic Lat/Lon, 1, 1, -180, 90, {360/width}, {180/height}, WGS-84}}\n"
        "data ignore value = -99999\n", encoding="ascii")


def source_manifest(path):
    return {"prepared": handoff.file_record(path),
            "files": [handoff.file_record(path), handoff.file_record(path.with_suffix(".hdr"))],
            "transformation": "Synthetic offline test fixture; no real ETOPO data"}


def quiet(function, *args):
    with contextlib.redirect_stdout(io.StringIO()):
        return function(*args)


class LocalFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="usgs-world-map-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def link(self, path, target):
        try:
            path.symlink_to(target)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("this host does not allow symlinks: " + str(exc))


class PreparationManifestTests(LocalFixture):
    def setUp(self):
        super().setUp()
        self.source = self.root / "source"
        self.source.mkdir()
        self.prepared = self.source / "etopo2022_surface_60s.bin"
        self.header = self.prepared.with_suffix(".hdr")
        write_grid(self.prepared)
        (self.source / "NOAA-CC0-notice.txt").write_text("Synthetic test source; public domain.\n")
        self.manifest = source_manifest(self.prepared)
        self.manifest_path = self.source / "source-manifest.json"
        handoff.write_json(self.manifest_path, self.manifest)
        self.config = self.source / "representative-L4.config.json"

    def make(self):
        quiet(handoff.make_sample, SimpleNamespace(source_dir=self.source))

    def rejected(self):
        with self.assertRaises(REJECT):
            self.make()
        self.assertFalse(self.config.exists())

    def test_valid_pair_preserves_committed_20_tile_inventory(self):
        self.make()
        actual = json.loads(self.config.read_text())
        committed = json.loads((ROOT / "tools/usgs_world_map/representative-L4.config.json").read_text())
        self.assertEqual(actual["tiles"], committed["tiles"])
        self.assertEqual(len(actual["tiles"]), 20)
        self.assertEqual(actual["elevation"]["sha256"], sha256_file(self.prepared))
        self.assertEqual(actual["elevation"]["header"],
                         {key: self.manifest["files"][1][key] for key in ("sha256", "bytes")})
        self.assertEqual(actual["area_samples"], 2)

    def test_header_changed_after_make_sample_is_rejected_before_build_reads_it(self):
        self.make()
        self.header.write_text(self.header.read_text().replace("byte order = 0", "byte order = 1"))
        output = self.root / "pack"
        # Use the generated config unchanged, overriding only optional zstd so
        # the test remains dependency-free. No tile should ever be sampled.
        with mock.patch.object(pack_module, "EnviGrid", side_effect=AssertionError("must not open altered header")):
            with self.assertRaisesRegex(ValueError, "source SHA256 mismatch: elevation-header"):
                build_pack(self.config, output, codec="zlib")
        self.assertFalse(output.exists())

    def test_header_only_byte_order_change_fails_before_config_write(self):
        original_binary = self.prepared.read_bytes()
        self.header.write_text(self.header.read_text().replace("byte order = 0", "byte order = 1"))
        self.assertEqual(self.prepared.read_bytes(), original_binary)
        self.rejected()

    def test_header_only_geometry_change_fails_before_config_write(self):
        self.header.write_text(self.header.read_text().replace("-180, 90", "-170, 90"))
        self.rejected()

    def test_same_size_binary_corruption_is_rejected(self):
        self.prepared.write_bytes(struct.pack("<32f", *([200.25] * 32)))
        self.rejected()

    def test_missing_and_duplicate_inventory_records_are_rejected(self):
        for index in (0, 1):
            for duplicate in (False, True):
                with self.subTest(index=index, duplicate=duplicate):
                    value = copy.deepcopy(self.manifest)
                    if duplicate:
                        value["files"].append(value["files"][index])
                    else:
                        value["files"].pop(index)
                    handoff.write_json(self.manifest_path, value)
                    self.rejected()

    def test_recorded_sizes_and_hashes_are_all_enforced(self):
        for record in ("prepared", 0, 1):
            for field, bad in (("bytes", 1), ("bytes", True), ("sha256", "0" * 64)):
                with self.subTest(record=record, field=field, bad=bad):
                    value = copy.deepcopy(self.manifest)
                    target = value["prepared"] if record == "prepared" else value["files"][record]
                    target[field] = bad
                    handoff.write_json(self.manifest_path, value)
                    self.rejected()

    def test_missing_files_are_rejected(self):
        for path in (self.prepared, self.header):
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.unlink()
                self.rejected()
                path.write_bytes(original)

    def test_manifest_cannot_select_nonlocal_source(self):
        for name in ("../outside.bin", "/outside.bin", r"..\outside.bin",
                     r"C:\outside.bin", "C:outside.bin", ".", "..", "", None):
            with self.subTest(name=name):
                value = copy.deepcopy(self.manifest)
                value["prepared"]["path"] = name
                handoff.write_json(self.manifest_path, value)
                self.rejected()

    def test_manifest_duplicate_json_keys_are_rejected(self):
        self.manifest_path.write_text('{"prepared":null,"prepared":{}}')
        self.rejected()

    def test_binary_and_header_symlinks_are_rejected(self):
        for path in (self.prepared, self.header):
            with self.subTest(path=path.name):
                outside = self.root / path.name
                path.rename(outside)
                self.link(path, outside)
                self.rejected()
                path.unlink()
                outside.rename(path)

    def test_dangling_primary_header_cannot_hide_behind_fallback(self):
        fallback = Path(str(self.prepared) + ".hdr")
        self.header.rename(fallback)
        self.manifest["files"][1]["path"] = fallback.name
        handoff.write_json(self.manifest_path, self.manifest)
        self.link(self.header, self.root / "missing-header")
        self.rejected()

    def test_fallback_header_is_verified_when_primary_is_absent(self):
        fallback = Path(str(self.prepared) + ".hdr")
        self.header.rename(fallback)
        self.manifest["files"][1]["path"] = fallback.name
        handoff.write_json(self.manifest_path, self.manifest)
        self.make()
        self.assertTrue(self.config.is_file())

    def test_existing_config_is_not_overwritten(self):
        self.config.write_bytes(b"keep existing")
        with self.assertRaises(REJECT):
            self.make()
        self.assertEqual(self.config.read_bytes(), b"keep existing")

    def test_dangling_config_symlink_does_not_create_external_target(self):
        outside = self.root / "outside.json"
        self.link(self.config, outside)
        with self.assertRaises(REJECT):
            self.make()
        self.assertFalse(outside.exists())
        self.assertTrue(self.config.is_symlink())

    def test_existing_config_symlink_does_not_modify_external_target(self):
        outside = self.root / "outside.json"
        outside.write_bytes(b"keep external")
        self.link(self.config, outside)
        with self.assertRaises(REJECT):
            self.make()
        self.assertEqual(outside.read_bytes(), b"keep external")

    def test_config_appearing_after_preflight_is_not_overwritten(self):
        validate = handoff._prepared_source

        def competing_writer(*args):
            result = validate(*args)
            self.config.write_bytes(b"another writer")
            return result

        with mock.patch.object(handoff, "_prepared_source", side_effect=competing_writer):
            with self.assertRaises(FileExistsError):
                self.make()
        self.assertEqual(self.config.read_bytes(), b"another writer")


class PackHandoffTests(LocalFixture):
    @classmethod
    def setUpClass(cls):
        cls.shared = tempfile.TemporaryDirectory(prefix="usgs-world-map-packs-")
        cls.addClassCleanup(cls.shared.cleanup)
        cls.base = Path(cls.shared.name)
        cls.prepared = cls.base / "source.bin"
        write_grid(cls.prepared)
        (cls.base / "notice.txt").write_text("Synthetic test source; public domain.\n")
        cls.config = {"version": 1, "name": "synthetic-handoff",
                      "coverage": "Three synthetic tiles; not real terrain", "codec": "zlib",
                      "area_samples": 2,
                      "tiles": [handoff.Tile("px", 4, 14, 7).as_dict(),
                                handoff.Tile("px", 4, 15, 7).as_dict(),
                                handoff.Tile("py", 4, 0, 7).as_dict()],
                      "elevation": {"path": str(cls.prepared), "format": "envi",
                                    "url": "https://example.invalid/synthetic", "version": "1",
                                    "license_id": "CC0-1.0", "license_path": str(cls.base / "notice.txt")}}
        cls.pack = cls.base / "pack"
        build_pack(cls.config, cls.pack)

    def setUp(self):
        super().setUp()
        self.input = self.root / "relocated.bin"
        shutil.copyfile(self.prepared, self.input)
        shutil.copyfile(self.prepared.with_suffix(".hdr"), self.input.with_suffix(".hdr"))
        self.output = self.root / "convergence.json"

    def convergence(self, pack=None):
        quiet(handoff.convergence, SimpleNamespace(pack=pack or self.pack,
                                                   prepared=self.input, output=self.output))

    def assert_source_rejected_before_sampling(self, pack=None):
        with mock.patch.object(handoff, "EnviGrid", side_effect=AssertionError("must not sample mismatched input")):
            with self.assertRaises(REJECT):
                self.convergence(pack)
        self.assertFalse(self.output.exists())

    def test_matching_relocated_pair_has_bounded_quantization(self):
        self.convergence()
        result = json.loads(self.output.read_text())
        self.assertTrue(result["source_matches_pack"])
        self.assertEqual(result["source"]["sha256"], sha256_file(self.input))
        self.assertEqual(result["source_header"]["sha256"], sha256_file(self.input.with_suffix(".hdr")))
        self.assertEqual(result["probe_count"], 3 * 64)
        self.assertEqual(result["quadrature"]["pack_N"], 2)
        self.assertLess(result["quadrature"]["max_absolute_difference_m"], 1e-9)
        self.assertAlmostEqual(result["max_quantization_error_m_on_probes"], .25)

    def test_optional_header_pin_preserves_legacy_pack_bytes(self):
        config = copy.deepcopy(self.config)
        record = handoff.file_record(self.prepared.with_suffix(".hdr"))
        config["elevation"]["header"] = {key: record[key] for key in ("sha256", "bytes")}
        pinned = self.root / "pinned-pack"
        build_pack(config, pinned)
        snapshot = lambda root: {p.relative_to(root).as_posix(): p.read_bytes()
                                 for p in root.rglob("*") if p.is_file()}
        self.assertEqual(snapshot(pinned), snapshot(self.pack))

    def test_malformed_and_mismatching_header_pins_reject_before_sampling(self):
        record = handoff.file_record(self.input.with_suffix(".hdr"))
        good = {key: record[key] for key in ("sha256", "bytes")}
        cases = [None, {}, {"sha256": good["sha256"]}, {"bytes": good["bytes"]},
                 {**good, "path": "other.hdr"}, {**good, "bytes": True},
                 {**good, "bytes": 0}, {**good, "bytes": 65537},
                 {**good, "bytes": good["bytes"] + 1}, {**good, "sha256": "invalid"},
                 {**good, "sha256": "0" * 64}]
        for record in cases:
            with self.subTest(record=record):
                config = copy.deepcopy(self.config)
                config["elevation"]["header"] = record
                output = self.root / "rejected-pack"
                with mock.patch.object(pack_module, "EnviGrid", side_effect=AssertionError("must not sample invalid pin")):
                    with self.assertRaises(REJECT):
                        build_pack(config, output)
                self.assertFalse(output.exists())

    def test_optional_header_pin_accepts_verified_fallback_header(self):
        fallback = Path(str(self.input) + ".hdr")
        self.input.with_suffix(".hdr").rename(fallback)
        config = copy.deepcopy(self.config)
        config["tiles"] = config["tiles"][:1]
        config["elevation"]["path"] = str(self.input)
        record = handoff.file_record(fallback)
        config["elevation"]["header"] = {key: record[key] for key in ("sha256", "bytes")}
        output = self.root / "fallback-pack"
        result = build_pack(config, output)
        self.assertEqual(next(s["sha256"] for s in result["sources"] if s["id"] == "elevation-header"), record["sha256"])
        self.convergence(output)

    def test_build_rejects_dangling_primary_header_before_reading_fallback(self):
        fallback = Path(str(self.input) + ".hdr")
        self.input.with_suffix(".hdr").rename(fallback)
        self.link(self.input.with_suffix(".hdr"), self.root / "missing")
        config = copy.deepcopy(self.config)
        config["elevation"]["path"] = str(self.input)
        with mock.patch.object(pack_module, "EnviGrid", side_effect=AssertionError("must not ignore dangling primary")):
            with self.assertRaises(REJECT):
                build_pack(config, self.root / "rejected-pack")

    def test_wrong_same_size_raster_is_rejected_before_sampling(self):
        write_grid(self.input, [200.25] * 32)
        self.assert_source_rejected_before_sampling()

    def test_changed_header_is_rejected_before_sampling(self):
        header = self.input.with_suffix(".hdr")
        header.write_text(header.read_text().replace("byte order = 0", "byte order = 1"))
        self.assert_source_rejected_before_sampling()

    def test_missing_or_symlinked_inputs_are_rejected(self):
        for path in (self.input, self.input.with_suffix(".hdr")):
            with self.subTest(path=path.name):
                saved = path.with_name(path.name + ".saved")
                path.rename(saved)
                self.assert_source_rejected_before_sampling()
                self.link(path, saved)
                self.assert_source_rejected_before_sampling()
                path.unlink()
                saved.rename(path)

    def test_fallback_header_matches_pack_hash(self):
        self.input.with_suffix(".hdr").rename(Path(str(self.input) + ".hdr"))
        self.convergence()
        self.assertTrue(json.loads(self.output.read_text())["source_matches_pack"])

    def test_pack_without_header_provenance_is_rejected(self):
        changed = self.root / "changed-pack"
        shutil.copytree(self.pack, changed)
        manifest = json.loads((changed / "manifest.json").read_text())
        manifest["sources"] = [s for s in manifest["sources"] if s["id"] != "elevation-header"]
        manifest["measurements"]["source_bytes"] = sum(s["bytes"] for s in manifest["sources"])
        handoff.write_json(changed / "manifest.json", manifest)
        verify_pack(changed)  # Valid v1 metadata alone cannot bind a local ENVI header.
        self.assert_source_rejected_before_sampling(changed)

    def test_rehashed_wrong_elevation_payload_fails_quantization(self):
        changed = self.root / "changed-pack"
        shutil.copytree(self.pack, changed)
        manifest = json.loads((changed / "manifest.json").read_text())
        entry = next(f for f in manifest["files"] if f["kind"] == "elevation")
        path = changed / entry["path"]
        decoded = decode_tile(path.read_bytes())
        tile = next(handoff.Tile(**t) for t in manifest["tiles"]
                    if "elevation/" + handoff.Tile(**t).key + ".elv" == entry["path"])
        path.write_bytes(encode_tile(tile, [h + 2 for h in decoded["heights"]], "zlib"))
        entry.update(bytes=path.stat().st_size, sha256=sha256_file(path))
        manifest["measurements"]["payload_bytes"] = sum(f["bytes"] for f in manifest["files"])
        handoff.write_json(changed / "manifest.json", manifest)
        verify_pack(changed)  # All file integrity checks pass; source reconstruction does not.
        with self.assertRaisesRegex(ValueError, "quantization residual"):
            self.convergence(changed)
        self.assertFalse(self.output.exists())

    def test_real_quadrature_difference_over_half_metre_is_accepted(self):
        # Fine alternating cells produce genuine N2/N8 differences. Quantization
        # must compare with the recorded N2, not the refined diagnostic N8.
        values = [500.25 if (r + c) % 2 else -500.25 for r in range(180) for c in range(360)]
        write_grid(self.input, values, width=360, height=180)
        config = copy.deepcopy(self.config)
        config["elevation"]["path"] = str(self.input)
        config["tiles"] = [handoff.Tile("px", 0, 0, 0).as_dict()]
        changed = self.root / "varying-pack"
        build_pack(config, changed)
        self.convergence(changed)
        result = json.loads(self.output.read_text())
        self.assertGreater(result["quadrature"]["max_absolute_difference_m"], .5)
        self.assertLessEqual(result["max_quantization_error_m_on_probes"], .5 + 1e-6)

    def test_sample_seams_and_repeated_inventory(self):
        repeat = self.root / "repeat-pack"
        shutil.copytree(self.pack, repeat)
        quiet(handoff.check_sample, SimpleNamespace(pack=self.pack, repeat=repeat, output=self.output))
        result = json.loads(self.output.read_text())
        self.assertEqual(result["tile_count"], 3)
        self.assertGreater(result["apron_core_exact_match_counts"]["same_face"], 0)
        self.assertGreater(result["apron_core_exact_match_counts"]["cross_face"], 0)
        self.assertTrue(result["same_environment_repeat_all_file_hashes_match"])


class JoinDownloadsTests(LocalFixture):
    def setUp(self):
        super().setUp()
        self.parts = self.root / "parts"
        self.parts.mkdir()
        self.payloads = (b"synthetic first part", b"synthetic second part")
        self.records = []
        for index, payload in enumerate(self.payloads):
            name = "part." + str(index)
            (self.parts / name).write_bytes(payload)
            self.records.append((name, len(payload), hashlib.sha256(payload).hexdigest()))
        self.output = self.root / "joined.zip"
        self.staging = self.root / "joined.zip.assembling"

    def join(self, archive_hash=None):
        archive_hash = archive_hash or hashlib.sha256(b"".join(self.payloads)).hexdigest()
        argv = ["join_downloads", "--parts-dir", str(self.parts), "--output", str(self.output)]
        with mock.patch.object(join, "PARTS", self.records), mock.patch.object(join, "ARCHIVE_SHA256", archive_hash), mock.patch.object(sys, "argv", argv):
            quiet(join.main)

    def test_exact_parts_publish_verified_bytes(self):
        self.join()
        self.assertEqual(self.output.read_bytes(), b"".join(self.payloads))
        self.assertFalse(self.staging.exists())

    def test_corrupt_short_and_missing_parts_leave_no_output(self):
        second = self.parts / "part.1"
        for value in (b"X" * len(self.payloads[1]), b"short", None):
            with self.subTest(value=value):
                if value is None:
                    second.unlink()
                else:
                    second.write_bytes(value)
                with self.assertRaises(REJECT):
                    self.join()
                self.assertFalse(self.output.exists())
                self.assertFalse(self.staging.exists())

    def test_complete_archive_hash_is_independently_enforced(self):
        with self.assertRaisesRegex(ValueError, "assembled ZIP SHA256 mismatch"):
            self.join("0" * 64)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.staging.exists())

    def test_existing_output_and_staging_are_preserved(self):
        for path in (self.output, self.staging):
            with self.subTest(path=path.name):
                path.write_bytes(b"keep existing")
                with self.assertRaises(REJECT):
                    self.join()
                self.assertEqual(path.read_bytes(), b"keep existing")
                path.unlink()

    def test_dangling_output_and_staging_links_are_preserved(self):
        for path in (self.output, self.staging):
            with self.subTest(path=path.name):
                outside = self.root / (path.name + ".outside")
                self.link(path, outside)
                with self.assertRaises(REJECT):
                    self.join()
                self.assertFalse(outside.exists())
                self.assertTrue(path.is_symlink())
                path.unlink()

    def test_symlink_part_is_rejected_and_owned_staging_cleaned(self):
        part = self.parts / "part.1"
        outside = self.root / "outside-part"
        part.rename(outside)
        self.link(part, outside)
        with self.assertRaises(REJECT):
            self.join()
        self.assertEqual(outside.read_bytes(), self.payloads[1])
        self.assertFalse(self.staging.exists())
        self.assertFalse(self.output.exists())


class DependencyTests(unittest.TestCase):
    def test_handoff_imports_without_site_packages_or_unix_resource(self):
        code = ("import sys; "
                "sys.modules.update({m: None for m in ('numpy', 'rasterio', 'PIL', 'resource')}); "
                "from tools.usgs_world_map import prepare_sources")
        result = subprocess.run([sys.executable, "-S", "-c", code], cwd=ROOT,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
