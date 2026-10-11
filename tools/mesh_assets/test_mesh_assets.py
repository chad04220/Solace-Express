#!/usr/bin/env python3
"""Focused release-tool tests; tiny synthetic meshes, no GL/context/bakes needed."""
import copy
import hashlib
import json
import re
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import mesh_assets as assets


def mesh_bytes(model=0, slot=0, source="11" * 32):
    identity = hashlib.sha256(f"fixture-{model}-{slot}".encode()).digest()
    vertex = (0., 0., 0., 0., 0., 1., 3., 1.,
              1., 0., 0., 0., 0., 1., 3., 1.,
              0., 1., 0., 0., 0., 1., 3., 1.)
    payload = struct.pack("<24f3If", *vertex, 0, 1, 2, float(slot))
    # A complete rigid-part block covers the moving-part parser too.
    payload += struct.pack("<3I24f3I", 46, 24, 3, *vertex, 0, 1, 2)
    header = bytearray(192)
    header[:8] = b"SEAMSH01"
    struct.pack_into("<14I", header, 8, 1, 192, 1, 24, model, slot, 1, 0, 24, 3, 1, 3, 30, 0)
    struct.pack_into("<Q", header, 64, len(payload))
    header[72:104] = identity
    header[136:168] = bytes.fromhex(source)
    result = header + payload
    result[104:136] = hashlib.sha256(result).digest()
    return f"aircraft_{model:02d}_{slot}_{identity.hex()}.mesh", result


def rehash(data):
    data[104:136] = bytes(32)
    data[104:136] = hashlib.sha256(data).digest()
    return data


class MeshToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.raw = self.root / "raw"
        self.raw.mkdir()
        self.source = {"schema": 1, "digest": "22" * 32, "geometry_digest": "11" * 32,
                       "files": [{"path": "fixture", "sha256": "33" * 32, "kind": "cpu"}]}
        self.roster = [(f"model_{i}", f"Aircraft {i}") for i in range(15)]
        self.report = {"schema": 1, "complete": True, "full_quality": True,
                       "implementation": assets.IMPLEMENTATION, "source_digest": self.source["digest"],
                       "geometry_digest": self.source["geometry_digest"], "format_version": 1,
                       "algorithm_version": 24, "asset_count": 30,
                       "gl": {"vendor": "Fixture", "renderer": "Fixture GPU", "version": "3.3"}, "files": []}
        for model in range(15):
            for slot in (0, 1):
                filename, data = mesh_bytes(model, slot)
                (self.raw / filename).write_bytes(data)
                self.report["files"].append({"model_index": model, "slot": slot, "file": filename,
                                             "model_id": self.roster[model][0], "model_name": self.roster[model][1]})
        self.write_report()

    def tearDown(self):
        self.temp.cleanup()

    def write_report(self):
        (self.raw / assets.REPORT_NAME).write_bytes(assets.canonical_json(self.report))

    def first_asset(self):
        return self.raw / self.report["files"][0]["file"]

    def mutate(self, offset, fmt, value):
        path = self.first_asset()
        data = bytearray(path.read_bytes())
        struct.pack_into(fmt, data, offset, value)
        path.write_bytes(rehash(data))

    def assert_bad_asset(self):
        with self.assertRaises(assets.AssetError):
            assets.inspect_asset(self.first_asset(), self.source["geometry_digest"])

    def bundle(self, name="assets.zip"):
        output = self.root / name
        assets.bundle(self.raw, output, self.source, self.roster, "fixture-source")
        return output

    def test_all_payload_components_and_identity(self):
        info = assets.inspect_asset(self.first_asset(), self.source["geometry_digest"])
        self.assertEqual((info["vertex_count"], info["triangle_count"], info["fine_start_index"]), (3, 1, 3))
        self.assertEqual((info["hull_float_count"], info["part_count"], info["part_types"], info["part_index_count"]), (1, 1, [46], 3))
        self.assertEqual(info["sha256"], assets.file_sha256(self.first_asset()))

    def test_deterministic_zip_and_exact_lossless_roundtrip(self):
        first = self.bundle("first.zip")
        second = self.bundle("second.zip")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        output = self.root / "release" / "aircraft"
        manifest = assets.extract(first, output, self.source, self.roster)
        self.assertEqual(manifest["asset_count"], 30)
        for entry in self.report["files"]:
            self.assertEqual((self.raw / entry["file"]).read_bytes(), (output / entry["file"]).read_bytes())
        with zipfile.ZipFile(first) as archive:
            self.assertEqual(len(archive.infolist()), 32)
            self.assertTrue(all(item.date_time == (1980, 1, 1, 0, 0, 0) for item in archive.infolist()))

    def test_failed_bundle_preserves_existing_output(self):
        output = self.root / "assets.zip"
        output.write_bytes(b"previous-good-release")
        self.first_asset().unlink()
        with self.assertRaises(assets.AssetError):
            assets.bundle(self.raw, output, self.source, self.roster, "fixture")
        self.assertEqual(output.read_bytes(), b"previous-good-release")
        self.assertFalse((self.raw / assets.MANIFEST_NAME).exists())

    def test_bundle_sync_uses_writable_handle(self):
        real_open = open
        opened = []

        def checked_open(path, *args, **kwargs):
            stream = real_open(path, *args, **kwargs)
            if Path(path).name.startswith(".assets.zip."):
                if not stream.writable():
                    stream.close()
                    self.fail("Windows requires a writable handle for ZIP fsync")
                opened.append(stream)
            return stream

        with patch.object(assets, "open", checked_open, create=True):
            self.bundle()
        self.assertEqual(len(opened), 1)
        self.assertTrue(opened[0].closed)

    def test_bundle_sync_failure_preserves_existing_output(self):
        output = self.root / "assets.zip"
        output.write_bytes(b"previous-good-release")
        with patch.object(assets.os, "fsync", side_effect=OSError("sync failed")):
            with self.assertRaisesRegex(OSError, "sync failed"):
                assets.bundle(self.raw, output, self.source, self.roster, "fixture")
        self.assertEqual(output.read_bytes(), b"previous-good-release")
        self.assertFalse((self.raw / assets.MANIFEST_NAME).exists())
        self.assertFalse(list(self.root.glob(".assets.zip.*")))
        self.assertFalse(list(self.root.glob(".mesh-bundle-check-*")))

    def test_shader_binary_or_unexpected_file_is_rejected(self):
        (self.raw / "driver.bin").write_bytes(b"not distributable")
        with self.assertRaisesRegex(assets.AssetError, "Unexpected file"):
            self.bundle()

    def test_partial_missing_duplicate_and_mismatched_report_refused(self):
        for update in ({"complete": False}, {"full_quality": False}, {"asset_count": 29},
                       {"source_digest": "00" * 32}, {"geometry_digest": "00" * 32},
                       {"implementation": "legacy-cache-import"}):
            original = copy.deepcopy(self.report)
            self.report.update(update)
            self.write_report()
            with self.assertRaises(assets.AssetError):
                self.bundle()
            self.report = original
        self.report["files"][-1] = self.report["files"][0]
        self.write_report()
        with self.assertRaisesRegex(assets.AssetError, "Duplicate"):
            self.bundle()

    def test_truncation_and_extra_bytes_refused(self):
        original = self.first_asset().read_bytes()
        for data in (original[:-1], original + b"\x00", b"MESH" + original[4:]):
            self.first_asset().write_bytes(data)
            self.assert_bad_asset()

    def test_corruption_refused_even_when_structurally_valid(self):
        data = bytearray(self.first_asset().read_bytes())
        data[192] ^= 1
        self.first_asset().write_bytes(data)
        self.assert_bad_asset()

    def test_duplicate_rigid_parts_refused(self):
        data = bytearray(self.first_asset().read_bytes())
        data += data[304:]
        struct.pack_into("<I", data, 56, 60)
        struct.pack_into("<Q", data, 64, len(data) - 192)
        self.first_asset().write_bytes(rehash(data))
        self.assert_bad_asset()

    def test_stale_or_wrong_identity_refused(self):
        with self.assertRaises(assets.AssetError):
            assets.inspect_asset(self.first_asset(), "ff" * 32)
        wrong_name = self.raw / ("aircraft_01" + self.first_asset().name[11:])
        self.first_asset().rename(wrong_name)
        with self.assertRaises(assets.AssetError):
            assets.inspect_asset(wrong_name)

    def test_version_quality_reserved_and_count_bounds(self):
        original = self.first_asset().read_bytes()
        for offset, value in ((8, 2), (12, 196), (16, 0), (20, 23), (24, 15), (28, 2),
                              (32, 2), (36, 1), (40, 0xffffffff), (44, 2), (48, 0), (52, 4), (60, 1), (168, 1)):
            self.first_asset().write_bytes(original)
            self.mutate(offset, "<I", value)
            self.assert_bad_asset()

    def test_nonfinite_positions_normals_indices_and_hull(self):
        original = self.first_asset().read_bytes()
        for offset, fmt, value in ((192, "<f", float("nan")), (204, "<f", float("inf")),
                                   (212, "<f", 0.), (220, "<f", 2.), (288, "<I", 3),
                                   (300, "<f", 0.5), (304, "<I", 47), (308, "<I", 0xffffffff)):
            self.first_asset().write_bytes(original)
            self.mutate(offset, fmt, value)
            self.assert_bad_asset()

    def test_tampered_manifest_refused(self):
        self.bundle()
        path = self.raw / assets.MANIFEST_NAME
        manifest = assets.read_json(path)
        manifest["assets"][0]["triangle_count"] = 99
        path.write_bytes(assets.canonical_json(manifest))
        with self.assertRaisesRegex(assets.AssetError, "manifest does not match"):
            assets.validate(self.raw, self.source, self.roster)

    def test_extract_refuses_traversal_duplicate_symlink_and_unknown_member(self):
        good = self.bundle()
        with zipfile.ZipFile(good) as archive:
            records = [(entry, archive.read(entry)) for entry in archive.infolist()]
        for case in ("traversal", "duplicate", "symlink", "unknown", "windows-drive", "windows-stream"):
            bad = self.root / f"{case}.zip"
            with zipfile.ZipFile(bad, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for index, (original, data) in enumerate(records):
                    info = copy.copy(original)
                    if index == 0:
                        if case == "traversal": info.filename = "aircraft/../evil"
                        if case == "duplicate": info.filename = records[1][0].filename
                        if case == "symlink": info.external_attr = 0o120777 << 16
                        if case == "unknown": info.filename = "aircraft/shader.bin"
                        if case == "windows-drive": info.filename = "aircraft/C:evil"
                        if case == "windows-stream": info.filename = "aircraft/manifest.json:evil"
                    archive.writestr(info, data)
            dest = self.root / f"extract-{case}"
            with self.assertRaises(assets.AssetError):
                assets.extract(bad, dest, self.source, self.roster)
            self.assertFalse(dest.exists())
        self.assertFalse((self.root / "evil").exists())

    def test_extract_never_replaces_existing_directory(self):
        good = self.bundle()
        with self.assertRaises(assets.AssetError):
            assets.extract(good, self.raw, self.source, self.roster)
        self.assertTrue(self.first_asset().exists())

    def test_source_manifest_recomputed_and_changed_sources_rejected(self):
        source_root = self.root / "source"
        source_root.mkdir()
        records = {"cpu": "", "shader": "", "producer": ""}
        files = []
        for kind, name in (("cpu", "cpu.cpp"), ("shader", "shader.glsl"), ("producer", "export.cpp")):
            (source_root / name).write_text(kind)
            digest = assets.file_sha256(source_root / name)
            files.append({"path": name, "sha256": digest, "kind": kind})
            records[kind] = f"{name}:{digest}\n"
        source = {"schema": 1, "files": files,
                  "geometry_digest": hashlib.sha256(("solace-aircraft-cpu-source-v1\n" + records["cpu"] + records["producer"]).encode()).hexdigest(),
                  "digest": hashlib.sha256(("solace-aircraft-build-source-v1\n" + "".join(records.values())).encode()).hexdigest()}
        path = self.root / "source.json"
        path.write_bytes(assets.canonical_json(source))
        self.assertEqual(assets.source_manifest(path, source_root), source)
        (source_root / "shader.glsl").write_text("changed")
        with self.assertRaisesRegex(assets.AssetError, "Source changed"):
            assets.source_manifest(path, source_root)

    def test_canonical_startup_contract(self):
        # Source contract for the intentionally copied producer setup. A startup
        # rule edit must fail this test rather than silently changing fan AO.
        root = Path(__file__).resolve().parents[2]
        game = re.sub(r"\s+", "", (root / "src/game.cpp").read_text())
        export = re.sub(r"\s+", "", (root / "tools/mesh_assets/export_aircraft_meshes.cpp").read_text())
        prewarm = game.split("voidGame::prewarm(", 1)[1].split("voidGame::menuBackgroundCamera(", 1)[0]
        tour = game.split("voidGame::menuTour(", 1)[1].split("std::vector<std::pair<int,bool>>Game::prewarmItems(", 1)[0]
        self.assertIn("update(1.f/60.f);render();", prewarm)
        self.assertIn("realTime=3.f;frame();", prewarm)
        self.assertIn("fillPlaneVisual(fp.plane,demo,realTime*250.f,prewarmInside);", tour)
        self.assertIn("demo.rpm=2400;", tour)
        self.assertIn("demo.engineRunning=true;demo.engineSpool=0.75f;", tour)
        self.assertIn("pv.Pr[0]=propAngle;pv.Pr[1]=blur;pv.Pr[2]=(float)std::max(s.blades,2);", game)
        self.assertIn("pv.engineHealth[i]=i<s.engines?p.fail.engineHealth[i]:0.f;", game)
        self.assertIn("constfloatfirstPrewarmTime=3.f+1.f/60.f;", export)
        self.assertIn("fp.plane.Pr[0]=firstPrewarmTime*250.f;", export)
        self.assertIn("fp.plane.Pr[1]=1.f;", export)
        self.assertIn("fp.plane.Pr[2]=float(std::max(kAircraft[model].blades,2));", export)
        self.assertIn("fp.plane.Pr[3]=0.f;", export)
        self.assertIn("fp.plane.engineHealth[engine]=engine<kAircraft[model].engines?1.f:0.f;", export)

    def test_json_duplicate_keys_rejected(self):
        path = self.root / "duplicate.json"
        path.write_text('{"schema":1,"schema":2}')
        with self.assertRaisesRegex(assets.AssetError, "Duplicate JSON"):
            assets.read_json(path)


if __name__ == "__main__":
    unittest.main()
