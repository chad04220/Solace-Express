#!/usr/bin/env python3
"""Validate and losslessly package production SEAMSH01 aircraft assets (stdlib only).

The exporter is the sole producer: this tool deliberately cannot import/rename a
legacy driver-keyed cache. Every release contains all 15 exterior/cockpit pairs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import mmap
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

FORMAT_VERSION = 1
ALGORITHM_VERSION = 24
PROFILE = 1
HEADER_BYTES = 192
MAX_FILE_BYTES = 512 * 1024 * 1024
MODEL_COUNT = 15
MANIFEST_NAME = "manifest.json"
REPORT_NAME = "bake-report.json"
IMPLEMENTATION = "production-strict-fresh-v1"
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
DEFAULT_SOURCE_ROOT = Path(__file__).resolve().parents[2]


class AssetError(ValueError):
    """An incomplete, incompatible or unverifiable asset bundle."""


def require(condition, message):
    if not condition:
        raise AssetError(message)


def canonical_json(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n").encode("utf-8")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    require(path.is_file() and not path.is_symlink(), f"Missing or linked JSON: {path}")
    require(path.stat().st_size <= 2 * 1024 * 1024, f"Oversized JSON: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise AssetError(f"Invalid JSON {path}: {exc}") from exc


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def source_manifest(path, source_root):
    value = read_json(path)
    require(value.get("schema") == 1, "Unsupported source manifest schema")
    for key in ("digest", "geometry_digest"):
        require(isinstance(value.get(key), str) and SHA256_RE.fullmatch(value[key]), f"Invalid {key}")
    files = value.get("files")
    require(isinstance(files, list) and files, "Source manifest must contain files")
    names = []
    kind_names = {"cpu": [], "shader": [], "producer": []}
    records = {"cpu": "", "shader": "", "producer": ""}
    for item in files:
        require(isinstance(item, dict), "Invalid source manifest entry")
        name, digest = item.get("path"), item.get("sha256")
        require(isinstance(name, str) and isinstance(digest, str) and SHA256_RE.fullmatch(digest), "Invalid source file hash")
        relative = PurePosixPath(name)
        require(not relative.is_absolute() and ".." not in relative.parts and "\\" not in name and str(relative) == name,
                f"Unsafe source path: {name}")
        source = source_root / name
        require(source.is_file() and not source.is_symlink() and source.resolve().is_relative_to(source_root.resolve()),
                f"Missing or linked source file: {name}")
        require(file_sha256(source) == digest, f"Source changed since exporter build: {name}; rebuild exporter")
        kind = item.get("kind")
        require(kind in kind_names, "Source file must have CPU/shader/producer kind")
        names.append(name)
        kind_names[kind].append(name)
        records[kind] += f"{name}:{digest}\n"
    require(len(names) == len(set(names)) and all(group == sorted(group) for group in kind_names.values()),
            "Source paths must be unique and sorted within kind")
    require(names == kind_names["cpu"] + kind_names["shader"] + kind_names["producer"], "Source groups must be CPU, shader, producer")
    cpu_digest = hashlib.sha256(("solace-aircraft-cpu-source-v1\n" + records["cpu"] + records["producer"]).encode()).hexdigest()
    full_digest = hashlib.sha256(("solace-aircraft-build-source-v1\n" + records["cpu"] + records["shader"] + records["producer"]).encode()).hexdigest()
    require((cpu_digest, full_digest) == (value["geometry_digest"], value["digest"]), "Source manifest digest mismatch")
    return value


def aircraft_roster(source_root):
    source = (source_root / "src/aircraft.cpp").read_text(encoding="utf-8")
    try:
        table = source.split("const AircraftSpec kAircraft[] = {", 1)[1].split("// clang-format on", 1)[0]
    except IndexError as exc:
        raise AssetError("Aircraft roster declaration changed; update exporter validator") from exc
    roster = re.findall(r'\{"([^"\n]+)",\s*"([^"\n]+)"', table)
    require(len(roster) == MODEL_COUNT and len({item[0] for item in roster}) == MODEL_COUNT,
            "Expected exactly 15 unique aircraft; update bundle contract with roster changes")
    return roster


def _mesh_vertices(data, offset, float_count, index_count, label):
    require(float_count % 8 == 0 and index_count % 3 == 0, f"{label}: invalid vertex/index stride")
    for vertex in struct.iter_unpack("<8f", memoryview(data)[offset:offset + float_count * 4]):
        require(all(math.isfinite(value) for value in vertex), f"{label}: nonfinite vertex")
        n2 = sum(value * value for value in vertex[3:6])
        require(0.98 <= n2 <= 1.02 and 0 <= vertex[7] <= 1.001, f"{label}: invalid normal/AO")
    offset += float_count * 4
    count = float_count // 8
    for (index,) in struct.iter_unpack("<I", memoryview(data)[offset:offset + index_count * 4]):
        require(index < count, f"{label}: index outside vertex buffer")
    return offset + index_count * 4


def inspect_asset(path, expected_source=None):
    require(path.is_file() and not path.is_symlink(), f"Missing or linked mesh: {path}")
    size = path.stat().st_size
    require(HEADER_BYTES <= size <= MAX_FILE_BYTES, f"Mesh size outside bounds: {path}")
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        require(data[:8] == b"SEAMSH01", f"Not a portable mesh (legacy caches cannot be promoted): {path}")
        version, header, flags, algorithm, model, slot, profile, reserved, nf, ni, nh, fine, np, reserved2 = struct.unpack_from("<14I", data, 8)
        require((version, header, flags, algorithm, profile) == (FORMAT_VERSION, HEADER_BYTES, 1, ALGORITHM_VERSION, PROFILE),
                f"Unsupported format, algorithm or full-quality profile: {path}")
        require(reserved == reserved2 == 0 and data[168:192] == bytes(24), f"Nonzero reserved header: {path}")
        require(model < MODEL_COUNT and slot in (0, 1), f"Invalid model/view: {path}")
        payload_bytes, = struct.unpack_from("<Q", data, 64)
        require(payload_bytes == 4 * (nf + ni + nh + np) and size == HEADER_BYTES + payload_bytes,
                f"Mesh length/count mismatch: {path}")
        require(nf > 0 and ni > 0 and nf % 8 == 0 and ni % 3 == 0 and nh % 9 == 1 and fine % 3 == 0 and fine <= ni,
                f"Incomplete or invalid body counts: {path}")
        identity, checksum, source = data[72:104].hex(), data[104:136].hex(), data[136:168].hex()
        require(identity != "0" * 64 and source != "0" * 64, f"Missing identity/source digest: {path}")
        if expected_source:
            require(source == expected_source, f"Mesh source differs from current build: {path}")
        filename = f"aircraft_{model:02d}_{slot}_{identity}.mesh"
        require(path.name == filename, f"Mesh filename does not match embedded identity: {path}")
        digest = hashlib.sha256(data[:104])
        digest.update(bytes(32))
        digest.update(memoryview(data)[136:])
        require(digest.hexdigest() == checksum, f"Mesh checksum mismatch: {path}")
        offset = _mesh_vertices(data, HEADER_BYTES, nf, ni, path.name)
        hull_end = offset + nh * 4
        require(all(math.isfinite(x[0]) for x in struct.iter_unpack("<f", memoryview(data)[offset:hull_end])),
                f"Nonfinite moving hull: {path}")
        require(struct.unpack_from("<f", data, hull_end - 4)[0] in (0.0, 1.0), f"Invalid hull eye flag: {path}")
        offset = hull_end
        part_count = part_vertices = part_indices = 0
        part_types = []
        while offset < size:
            require(size - offset >= 12, f"Truncated rigid part: {path}")
            part_type, part_nf, part_ni = struct.unpack_from("<3I", data, offset)
            require(part_type <= 46 and part_type not in part_types and offset + 12 + 4 * (part_nf + part_ni) <= size,
                    f"Invalid or duplicate rigid-part bounds/type: {path}")
            offset = _mesh_vertices(data, offset + 12, part_nf, part_ni, f"{path.name} part {part_count}")
            part_count += 1
            part_vertices += part_nf // 8
            part_indices += part_ni
            part_types.append(part_type)
        return {
            "file": filename, "model_index": model, "slot": slot,
            "view": "exterior" if slot == 0 else "cockpit", "format_version": version,
            "algorithm_version": algorithm, "profile": profile, "full_quality": True,
            "identity": identity, "source_digest": source, "checksum_sha256": checksum,
            "sha256": hashlib.sha256(data).hexdigest(), "bytes": size,
            "payload_bytes": payload_bytes, "vertex_count": nf // 8, "index_count": ni,
            "triangle_count": ni // 3, "fine_start_index": fine,
            "hull_float_count": nh, "hull_triangle_count": (nh - 1) // 9,
            "part_word_count": np, "part_count": part_count, "part_types": part_types,
            "part_vertex_count": part_vertices, "part_index_count": part_indices,
        }


def inspect_directory(directory, source, roster):
    require(directory.is_dir() and not directory.is_symlink(), f"Missing or linked asset directory: {directory}")
    report = read_json(directory / REPORT_NAME)
    require(report.get("schema") == 1 and report.get("complete") is True and report.get("full_quality") is True,
            "Bake report is absent, incomplete or not full quality")
    require(report.get("implementation") == IMPLEMENTATION, "Unrecognized exporter: legacy cache imports are not accepted")
    require(report.get("source_digest") == source["digest"] and report.get("geometry_digest") == source["geometry_digest"],
            "Exporter binary/source digest mismatch; rebuild and rebake")
    require(report.get("format_version") == FORMAT_VERSION and report.get("algorithm_version") == ALGORITHM_VERSION,
            "Bake report format/algorithm mismatch")
    require(report.get("asset_count") == MODEL_COUNT * 2, "Bake report does not contain all 30 aircraft bodies")
    gl = report.get("gl", {})
    require(isinstance(gl, dict) and all(isinstance(gl.get(key), str) and gl[key] for key in ("vendor", "renderer", "version")),
            "Missing canonical producer GL provenance")
    records = report.get("files")
    require(isinstance(records, list) and len(records) == MODEL_COUNT * 2, "Incomplete bake file list")
    meshes = []
    seen = set()
    for entry in records:
        require(isinstance(entry, dict), "Invalid bake report entry")
        model, slot, name = entry.get("model_index"), entry.get("slot"), entry.get("file")
        require(type(model) is int and type(slot) is int and 0 <= model < MODEL_COUNT and slot in (0, 1), "Invalid bake model/view")
        require(isinstance(name, str) and name == Path(name).name and "/" not in name and "\\" not in name, "Unsafe bake filename")
        require((model, slot) not in seen, "Duplicate model/view in bake report")
        seen.add((model, slot))
        require((entry.get("model_id"), entry.get("model_name")) == roster[model], "Bake aircraft roster differs from current source")
        mesh = inspect_asset(directory / name, source["geometry_digest"])
        require((mesh["model_index"], mesh["slot"]) == (model, slot), "Bake report and mesh identity disagree")
        mesh.update(model_id=entry["model_id"], model_name=entry["model_name"])
        meshes.append(mesh)
    require(seen == {(model, slot) for model in range(MODEL_COUNT) for slot in (0, 1)}, "Missing aircraft/view")
    allowed = {item["file"] for item in meshes} | {REPORT_NAME, MANIFEST_NAME}
    require(all(path.is_file() and not path.is_symlink() and path.name in allowed for path in directory.iterdir()),
            "Unexpected file/directory in asset bundle (shader binaries and legacy caches are forbidden)")
    meshes.sort(key=lambda item: (item["model_index"], item["slot"]))
    return report, meshes


def build_manifest(source, report, meshes, revision):
    require(isinstance(revision, str) and revision and len(revision) <= 200, "Missing source revision")
    return {
        "schema": 1, "bundle": "SolaceExpress-aircraft", "asset_count": len(meshes),
        "format_version": FORMAT_VERSION, "algorithm_version": ALGORITHM_VERSION,
        "full_quality": True, "compression": "ZIP-DEFLATE-lossless", "quantization": "none",
        "shader_binaries": False, "source_revision": revision, "source": source,
        "producer": {"implementation": report["implementation"], "gl": report["gl"]},
        "bake_report_sha256": hashlib.sha256(canonical_json(report)).hexdigest(),
        "total_mesh_bytes": sum(item["bytes"] for item in meshes), "assets": meshes,
    }


def validate(directory, source, roster):
    report, meshes = inspect_directory(directory, source, roster)
    manifest = read_json(directory / MANIFEST_NAME)
    expected = build_manifest(source, report, meshes, manifest.get("source_revision"))
    require(manifest == expected, "Release manifest does not match validated assets/source")
    return manifest


def current_revision(source_root):
    try:
        return subprocess.check_output(["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        raise AssetError("Use --source-revision outside a Git checkout") from exc


def bundle(directory, output, source, roster, revision):
    require(not output.resolve().is_relative_to(directory.resolve()), "Bundle output must be outside asset input directory")
    report, meshes = inspect_directory(directory, source, roster)
    manifest = build_manifest(source, report, meshes, revision)
    report_bytes, manifest_bytes = canonical_json(report), canonical_json(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{output.name}.", suffix=".tmp", dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
            records = [(REPORT_NAME, report_bytes), (MANIFEST_NAME, manifest_bytes)]
            records.extend((item["file"], None) for item in meshes)
            for name, content in sorted(records):
                info = zipfile.ZipInfo("aircraft/" + name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                info._compresslevel = 9
                with archive.open(info, "w", force_zip64=True) as dest:
                    if content is not None:
                        dest.write(content)
                    else:
                        with (directory / name).open("rb") as src:
                            shutil.copyfileobj(src, dest, 1024 * 1024)
        # Check the archived bytes, not just the input inspected before compression.
        with tempfile.TemporaryDirectory(prefix=".mesh-bundle-check-", dir=output.parent) as check:
            extracted = Path(check) / "aircraft"
            extract(Path(temporary), extracted, source, roster)
        with open(temporary, "rb") as stream:
            os.fsync(stream.fileno())
        atomic_write(directory / MANIFEST_NAME, manifest_bytes)
        # Report serialization is canonical in archives; unchanged producer text need not be rewritten.
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return manifest


def extract(archive_path, output, source, roster):
    require(not output.exists(), f"Destination already exists; use a new staging directory: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        with zipfile.ZipFile(archive_path) as archive:
            entries = archive.infolist()
            require(len(entries) == MODEL_COUNT * 2 + 2, "Bundle must contain exactly 30 meshes, manifest and bake report")
            seen = set()
            total = 0
            for info in entries:
                parts = PurePosixPath(info.filename).parts
                require(len(parts) == 2 and parts[0] == "aircraft" and parts[1] not in (".", "..")
                        and "\\" not in info.filename and info.filename == f"aircraft/{parts[1]}", "Unsafe archive path")
                name = parts[1]
                require(name in (REPORT_NAME, MANIFEST_NAME) or re.fullmatch(r"aircraft_[0-9]{2}_[01]_[0-9a-f]{64}\.mesh", name),
                        "Unrecognized archive member (only canonical mesh/report names are allowed)")
                require(name not in seen, "Duplicate archive member")
                seen.add(name)
                require(info.compress_type == zipfile.ZIP_DEFLATED and not info.flag_bits & 1, "Archive must use unencrypted lossless DEFLATE")
                mode = info.external_attr >> 16
                require((mode & 0o170000) in (0, 0o100000), "Archive links/directories are forbidden")
                limit = 2 * 1024 * 1024 if name in (REPORT_NAME, MANIFEST_NAME) else MAX_FILE_BYTES
                require(0 < info.file_size <= limit, "Archive member exceeds size bounds")
                total += info.file_size
                require(total <= MODEL_COUNT * 2 * MAX_FILE_BYTES + 4 * 1024 * 1024, "Archive exceeds bundle size limit")
                with archive.open(info) as src, (staging / name).open("xb") as dest:
                    shutil.copyfileobj(src, dest, 1024 * 1024)
        result = validate(staging, source, roster)
        os.rename(staging, output)
        return result
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("bundle", "validate", "extract"):
        sub = commands.add_parser(command)
        sub.add_argument("--input", type=Path, required=True)
        sub.add_argument("--source-manifest", type=Path, required=True)
        sub.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
        if command in ("bundle", "extract"):
            sub.add_argument("--output", type=Path, required=True)
        if command == "bundle":
            sub.add_argument("--source-revision")
    args = parser.parse_args(argv)
    try:
        source = source_manifest(args.source_manifest, args.source_root)
        roster = aircraft_roster(args.source_root)
        if args.command == "bundle":
            result = bundle(args.input, args.output, source, roster, args.source_revision or current_revision(args.source_root))
            print(f"Bundled {result['asset_count']} full-quality meshes: {args.output} ({args.output.stat().st_size} compressed bytes)")
            print(f"Bundle SHA256: {file_sha256(args.output)}")
        elif args.command == "extract":
            result = extract(args.input, args.output, source, roster)
            print(f"Staged {result['asset_count']} validated full-quality meshes: {args.output}")
        else:
            result = validate(args.input, source, roster)
            print(f"Validated {result['asset_count']} full-quality meshes ({result['total_mesh_bytes']} bytes)")
        return 0
    except (AssetError, OSError, KeyError, TypeError, struct.error, zipfile.BadZipFile) as exc:
        print(f"Aircraft asset validation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
