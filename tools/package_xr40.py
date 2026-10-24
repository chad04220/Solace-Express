#!/usr/bin/env python3
"""Build the independent Windows product with two fresh, full-quality XR-40 meshes.

The original all-aircraft release validator stays strict about its 30 views.
This product has a separate, explicitly scoped two-view contract.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile
from mesh_assets import mesh_assets as assets

ROOT = Path(__file__).resolve().parents[1]


def validate_aircraft(directory: Path, source: dict) -> tuple[dict, list[dict]]:
    assets.require(directory.is_dir() and not directory.is_symlink(), "Missing aircraft directory")
    report = assets.read_json(directory / "bake-report.json")
    assets.require(report.get("schema") == 1 and report.get("scope") == "XR40Battle"
                   and report.get("complete") is True and report.get("full_quality") is True
                   and report.get("asset_count") == 2, "Expected a complete --xr40 export")
    assets.require(report.get("implementation") == assets.IMPLEMENTATION, "Unrecognized producer")
    assets.require(report.get("source_digest") == source["digest"]
                   and report.get("geometry_digest") == source["geometry_digest"], "Exporter/consumer source mismatch")
    assets.require(report.get("format_version") == assets.FORMAT_VERSION
                   and report.get("algorithm_version") == assets.ALGORITHM_VERSION, "Mesh version mismatch")
    gl = report.get("gl", {})
    assets.require(all(isinstance(gl.get(k), str) and gl[k] for k in ("vendor", "renderer", "version")), "Missing GL provenance")
    roster = assets.aircraft_roster(ROOT)
    model = next(i for i, (ident, _) in enumerate(roster) if ident == "xr40_somnus")
    records = report.get("files")
    assets.require(isinstance(records, list) and len(records) == 2, "Expected two mesh records")
    meshes, slots, names = [], set(), {"bake-report.json"}
    for entry in records:
        assets.require(isinstance(entry, dict), "Invalid mesh record")
        slot, name = entry.get("slot"), entry.get("file")
        assets.require(type(slot) is int and slot in (0, 1) and slot not in slots
                       and entry.get("model_index") == model, "Duplicate or wrong aircraft/view")
        assets.require((entry.get("model_id"), entry.get("model_name")) == roster[model], "Wrong aircraft identity")
        assets.require(isinstance(name, str) and Path(name).name == name and "/" not in name and "\\" not in name, "Unsafe mesh path")
        mesh = assets.inspect_asset(directory / name, source["geometry_digest"])
        assets.require((mesh["model_index"], mesh["slot"]) == (model, slot), "Mesh header differs from report")
        meshes.append(mesh); slots.add(slot); names.add(name)
    assets.require({p.name for p in directory.iterdir()} == names, "Unexpected mesh files or diagnostic worker receipts")
    return report, meshes


def package(exe: Path, aircraft: Path, manifest_path: Path, output: Path) -> dict:
    assets.require(not output.exists(), "Output already exists; choose a new filename")
    assets.require(exe.is_file() and not exe.is_symlink(), "Missing Windows executable")
    with exe.open("rb") as stream:
        header = stream.read(64)
        assets.require(len(header) == 64 and header[:2] == b"MZ", "Not a Windows executable")
        stream.seek(struct.unpack_from("<I", header, 60)[0]); pe = stream.read(6)
        assets.require(pe == b"PE\0\0\x64\x86", "Expected Windows x64 PE executable")
    source = assets.source_manifest(manifest_path, ROOT)
    report, meshes = validate_aircraft(aircraft, source)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xr40-package-", dir=output.parent) as temporary:
        stage = Path(temporary) / "XR40Battle"; stage.mkdir()
        shutil.copy2(exe, stage / "XR40Battle.exe")
        for name in ("materials", "voice", "cockpits", "loading"):
            shutil.copytree(ROOT / "assets" / name, stage / name)
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            shutil.copy2(ROOT / name, stage / name)
        shutil.copy2(ROOT / "docs/XR40_BATTLE.md", stage / "README.md")
        (stage / "media").mkdir()
        for name in ("xr40-battle-hangar.png", "xr40-battle-flight.png", "xr40-battle-cockpit.png"):
            shutil.copy2(ROOT / "docs/media" / name, stage / "media" / name)
        shutil.copytree(aircraft, stage / "aircraft")
        manifest = {
            "schema": 1, "product": "XR40Battle", "platform": "windows-x64",
            "source_revision": revision, "source": source,
            "producer": report, "aircraft": meshes,
            "files": [{"path": p.relative_to(stage).as_posix(), "bytes": p.stat().st_size,
                       "sha256": assets.file_sha256(p)} for p in sorted(stage.rglob("*")) if p.is_file()],
        }
        (stage / "manifest.json").write_bytes(assets.canonical_json(manifest))
        archive = Path(temporary) / "package.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in sorted(stage.rglob("*")):
                if not path.is_file():
                    continue
                assets.require(not path.is_symlink(), "Package cannot include symlinks")
                entry = zipfile.ZipInfo("XR40Battle/" + path.relative_to(stage).as_posix(), (2026, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                with path.open("rb") as src, z.open(entry, "w") as dst:
                    shutil.copyfileobj(src, dst)
        with zipfile.ZipFile(archive) as z:
            assets.require(z.testzip() is None, "ZIP integrity failure")
            for file in manifest["files"]:
                data = z.read("XR40Battle/" + file["path"])
                assets.require(len(data) == file["bytes"] and hashlib.sha256(data).hexdigest() == file["sha256"], "ZIP content mismatch")
        archive.rename(output)
    return {"file": str(output), "bytes": output.stat().st_size, "sha256": assets.file_sha256(output),
            "meshes": len(meshes), "files": len(manifest["files"]) + 1, "source_revision": revision}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--aircraft", required=True, type=Path)
    parser.add_argument("--source-manifest", required=True, type=Path, help="From the executable's own build/gen directory")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.exe, args.aircraft, args.source_manifest, args.output), indent=2))
    except (OSError, ValueError, StopIteration, subprocess.SubprocessError) as exc:
        raise SystemExit(f"XR-40 package refused: {exc}") from exc


if __name__ == "__main__":
    main()
