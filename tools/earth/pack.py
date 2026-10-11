"""Deterministic pack directory construction and bounded untrusted verification.

The verifier never extracts an archive, follows links, loads executable content,
fetches a URL or trusts a filename from a manifest. SHA256 verifies integrity,
not publisher identity. Verify in an isolated directory, without concurrent writes.
"""
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
import zlib

from . import FORMAT_VERSION, TOOL_VERSION
from .airports import read_airports, validate_airports
from .common import canonical_json, read_bounded, sha256_file, strict_json
from .cube import APRON, FACES, SAMPLES, WIDTH, Tile
from .elevation import EnviGrid, Grid, MAX_TILE_BYTES, build_elevation, decode_tile
from .roads import MAX_ROAD_FILE, read_geojson, tile_roads, validate_road_tile

MAX_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_ENTRIES = 200000
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_PACK_BYTES = 64 * 1024 ** 3
MAX_LICENSE_BYTES = 1024 * 1024
SHA_RE = re.compile(r"[0-9a-f]{64}\Z")
ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\Z")
SCHEME = {"name": "tangent-cube", "faces": list(FACES), "samples": SAMPLES,
          "apron": APRON, "stored_width": WIDTH, "sample_positions": "cell-centres",
          "corner_owner": "first-incident-face-in-face-order"}
QUANTIZATION = {"unit_m": 1, "residual": "int16-le", "offset": "int32-metres",
                "rounding": "half-away-from-zero", "delta": "row-reset-modulo-65536"}


def _string(value, name, limit=4096):
    if not isinstance(value, str) or not value or len(value) > limit or any(ord(c) < 32 for c in value):
        raise ValueError("invalid " + name)
    return value


def _safe_path(value):
    if not isinstance(value, str) or not value or len(value) > 240 or "\\" in value or ":" in value or "\x00" in value:
        raise ValueError("unsafe pack path")
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or any(part in (".", "..", "") or part.endswith((".", " ")) for part in value.split("/")):
        raise ValueError("unsafe pack path")
    # Reject device names too, since the pack may later be copied to Windows.
    for part in path.parts:
        if part.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(1, 10)], *[f"LPT{i}" for i in range(1, 10)]}:
            raise ValueError("unsafe Windows pack path")
    return path


def _bounded_int(value, label, maximum, minimum=0):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("invalid " + label)
    return value


def _check_sha(value):
    if not isinstance(value, str) or not SHA_RE.fullmatch(value):
        raise ValueError("invalid SHA256")


def _codec_version(codec):
    if codec == "zstd":
        try:
            import zstandard
        except ImportError as exc:
            raise ValueError("zstd requires the optional zstandard package") from exc
        return {"python_package": zstandard.__version__, "library": ".".join(map(str, zstandard.ZSTD_VERSION))}
    if codec == "zlib":
        return {"library": zlib.ZLIB_RUNTIME_VERSION}
    raise ValueError("unsupported elevation codec")


def _input_path(base, value):
    path = Path(_string(value, "input path"))
    if not path.is_absolute():
        path = base / path
    if path.is_symlink() or not path.is_file():
        raise ValueError("input must be a regular file: " + str(path))
    return path


def _source(base, spec, source_id, licenses):
    if not isinstance(spec, dict):
        raise ValueError("invalid source specification")
    path = _input_path(base, spec.get("path"))
    actual_hash = sha256_file(path)
    if "sha256" in spec and spec["sha256"] != actual_hash:
        raise ValueError("source SHA256 mismatch: " + source_id)
    license_id = spec.get("license_id")
    if not isinstance(license_id, str) or not ID_RE.fullmatch(license_id):
        raise ValueError("invalid source license ID")
    license_bytes = read_bounded(_input_path(base, spec.get("license_path")), MAX_LICENSE_BYTES)
    if not license_bytes:
        raise ValueError("empty source license")
    if license_id in licenses and licenses[license_id] != license_bytes:
        raise ValueError("conflicting source license texts for one ID")
    licenses[license_id] = license_bytes
    record = {"id": source_id, "url": _string(spec.get("url"), "source URL"),
              "version": _string(spec.get("version"), "source version"),
              "sha256": actual_hash, "bytes": path.stat().st_size,
              "license_id": license_id, "license_path": "licenses/" + license_id + ".txt"}
    if "resolution_arcsec" in spec:
        value = spec["resolution_arcsec"]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 < value <= 3600:
            raise ValueError("invalid source angular resolution")
        record["resolution_arcsec"] = value
    if "description" in spec:
        record["description"] = _string(spec["description"], "source description")
    if "origin" in spec:
        origin = spec["origin"]
        if not isinstance(origin, dict) or set(origin) != {"url", "sha256", "bytes", "transformation"}:
            raise ValueError("invalid original-source metadata")
        _string(origin["url"], "original source URL")
        _check_sha(origin["sha256"])
        _bounded_int(origin["bytes"], "original source size", 1024 ** 4)
        _string(origin["transformation"], "source transformation")
        record["origin"] = origin
    return path, record


def build_pack(config, output, codec=None):
    """Build into a new pack directory with atomic final rename.

    Isolated, single-writer output is required: portable rename is not an atomic
    no-replace operation against another publisher creating an empty directory.

    Config paths are relative to the config file. A dict uses the current cwd.
    Configuration is trusted local build input; the pack it emits is verified
    with exactly the same untrusted-input path used by the command-line verifier.
    """
    if isinstance(config, (str, Path)):
        config_path = Path(config)
        base = config_path.absolute().parent
        config = strict_json(read_bounded(config_path, 4 * 1024 * 1024))
    else:
        base = Path.cwd()
    if not isinstance(config, dict) or (type(config.get("version")) is not int or config.get("version") != 1):
        raise ValueError("unsupported build configuration")
    name = _string(config.get("name"), "pack name", 128)
    coverage = _string(config.get("coverage"), "coverage description")
    tiles_spec = config.get("tiles")
    if not isinstance(tiles_spec, list) or not 1 <= len(tiles_spec) <= 65536:
        raise ValueError("invalid build tile count")
    try:
        tiles = sorted(Tile(**value) for value in tiles_spec)
    except (TypeError, KeyError) as exc:
        raise ValueError("invalid build tile") from exc
    if len(tiles) != len(set(tiles)):
        raise ValueError("duplicate build tile")
    selected_codec = codec or config.get("codec", "zstd")
    codec_version = _codec_version(selected_codec)
    area_samples = _bounded_int(config.get("area_samples", 2), "area samples", 16, 1)
    tolerance = config.get("road_tolerance_m", 20)
    if isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or not 0 <= tolerance <= 1000:
        raise ValueError("invalid road tolerance")
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("output already exists; use a new directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    licenses, sources = {}, []
    elevation_spec = config.get("elevation")
    source_path, source = _source(base, elevation_spec, "elevation", licenses)
    sources.append(source)
    source_format = elevation_spec.get("format", "grid-json")
    if source_format == "grid-json":
        elevation = Grid.from_json(source_path)
    elif source_format == "envi":
        # The header controls geometry/byte order. Verify an optional expected
        # record before EnviGrid reads it, then record the actual hashed source.
        header = source_path.with_suffix(".hdr")
        if not header.exists() and not header.is_symlink():
            header = Path(str(source_path) + ".hdr")
        header_spec = dict(elevation_spec, path=str(header))
        header_spec.pop("sha256", None)
        header_spec.pop("origin", None)
        expected_header = elevation_spec.get("header")
        if "header" in elevation_spec:
            if not isinstance(expected_header, dict) or set(expected_header) != {"sha256", "bytes"}:
                raise ValueError("invalid expected ENVI header record")
            _check_sha(expected_header["sha256"])
            _bounded_int(expected_header["bytes"], "expected ENVI header bytes", 64 * 1024, 1)
            header_spec["sha256"] = expected_header["sha256"]
        _, header_source = _source(base, header_spec, "elevation-header", licenses)
        if expected_header is not None and header_source["bytes"] != expected_header["bytes"]:
            raise ValueError("ENVI header byte count mismatch")
        elevation = EnviGrid(source_path)
        sources.append(header_source)
    else:
        raise ValueError("unsupported elevation source format")
    staging = None
    try:
        road_features = []
        if config.get("roads") is not None:
            if config["roads"].get("format", "geojson") != "geojson":
                raise ValueError("unsupported road source format")
            path, source = _source(base, config["roads"], "roads", licenses)
            sources.append(source)
            road_features = read_geojson(path)
        airport_rows = []
        if config.get("airports") is not None:
            paths = []
            for key in ("airports", "runways"):
                spec = config["airports"][key]
                if spec.get("format", "csv") != "csv":
                    raise ValueError("unsupported airport source format")
                path, source = _source(base, spec, key, licenses)
                paths.append(path)
                sources.append(source)
            airport_rows = read_airports(*paths)
        staging = Path(tempfile.mkdtemp(prefix=".earth-pack-", dir=output.parent))
        files = []
        def write(rel, raw, kind):
            _safe_path(rel)
            if len(raw) > (MAX_TILE_BYTES if kind == "elevation" else MAX_FILE_BYTES):
                raise ValueError("generated file exceeds pack limit")
            path = staging / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
            files.append({"path": rel, "kind": kind, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
        for tile in tiles:
            write("elevation/" + tile.key + ".elv", build_elevation(tile, elevation, area_samples, selected_codec), "elevation")
            if config.get("roads") is not None:
                write("roads/" + tile.key + ".json", canonical_json(tile_roads(road_features, tile, tolerance)), "roads")
        if config.get("airports") is not None:
            write("airports.json", canonical_json({"version": 1, "airports": airport_rows}), "airports")
        for license_id, raw in sorted(licenses.items()):
            write("licenses/" + license_id + ".txt", raw, "license")
        manifest = {
            "format": "solace-earth-pack", "format_version": FORMAT_VERSION,
            "tool": {"version": TOOL_VERSION, "codec": selected_codec, "codec_version": codec_version},
            "name": name, "coverage": coverage, "scheme": SCHEME, "quantization": QUANTIZATION,
            "sampling": {"method": "bilinear-jacobian-midpoint-quadrature", "area_samples": area_samples},
            "road_tolerance_m": tolerance, "sources": sorted(sources, key=lambda s: s["id"]),
            "tiles": [t.as_dict() for t in tiles], "files": sorted(files, key=lambda f: f["path"]),
            "measurements": {"source_bytes": sum(s["bytes"] for s in sources),
                             "payload_bytes": sum(f["bytes"] for f in files),
                             "elevation_tiles": len(tiles), "airport_count": len(airport_rows)},
        }
        (staging / "manifest.json").write_bytes(canonical_json(manifest))
        verify_pack(staging)
        if output.exists() or output.is_symlink():
            raise ValueError("output appeared during build")
        staging.rename(output)
        staging = None
        return manifest
    finally:
        elevation.close()
        if staging is not None:
            shutil.rmtree(staging)


def _verify_source(source, listed):
    keys = {"id", "url", "version", "sha256", "bytes", "license_id", "license_path"}
    if not isinstance(source, dict) or not keys.issubset(source) or not set(source).issubset(keys | {"origin", "resolution_arcsec", "description"}):
        raise ValueError("invalid source metadata")
    for key in ("id", "url", "version", "license_id"):
        _string(source[key], key)
    _check_sha(source["sha256"])
    _bounded_int(source["bytes"], "source bytes", 1024 ** 4)
    if not ID_RE.fullmatch(source["license_id"]):
        raise ValueError("invalid source license")
    expected = "licenses/" + source["license_id"] + ".txt"
    if source["license_path"] != expected or expected not in listed or listed[expected]["kind"] != "license":
        raise ValueError("missing source license")
    if "resolution_arcsec" in source:
        value = source["resolution_arcsec"]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 < value <= 3600:
            raise ValueError("invalid source angular resolution")
    if "description" in source:
        _string(source["description"], "source description")
    if "origin" in source:
        origin = source["origin"]
        if not isinstance(origin, dict) or set(origin) != {"url", "sha256", "bytes", "transformation"}:
            raise ValueError("invalid source origin")
        _string(origin["url"], "origin URL")
        _string(origin["transformation"], "origin transformation")
        _check_sha(origin["sha256"])
        _bounded_int(origin["bytes"], "origin bytes", 1024 ** 4)


def verify_pack(root):
    """Validate directory, metadata, checksums and payloads within fixed budgets."""
    root = Path(root).absolute()
    if root.is_symlink() or not root.is_dir():
        raise ValueError("pack must be an ordinary directory")
    # Reject a link anywhere in the user-supplied root path, not just its leaf.
    if any(parent.is_symlink() for parent in root.parents):
        raise ValueError("pack path contains a symlink")
    manifest = strict_json(read_bounded(root / "manifest.json", MAX_MANIFEST_BYTES))
    keys = {"format", "format_version", "tool", "name", "coverage", "scheme", "quantization", "sampling",
            "road_tolerance_m", "sources", "tiles", "files", "measurements"}
    if not isinstance(manifest, dict) or set(manifest) != keys or manifest["format"] != "solace-earth-pack" or type(manifest["format_version"]) is not int or manifest["format_version"] != FORMAT_VERSION:
        raise ValueError("unsupported pack manifest")
    if canonical_json(manifest["scheme"]) != canonical_json(SCHEME) or canonical_json(manifest["quantization"]) != canonical_json(QUANTIZATION):
        raise ValueError("unsupported pack grid or quantization")
    _string(manifest["name"], "pack name", 128)
    _string(manifest["coverage"], "coverage")
    tool = manifest["tool"]
    if not isinstance(tool, dict) or set(tool) != {"version", "codec", "codec_version"} or tool["version"] != TOOL_VERSION or tool["codec"] not in ("zlib", "zstd"):
        raise ValueError("unsupported pack tool or codec")
    if not isinstance(tool["codec_version"], dict) or not tool["codec_version"] or len(tool["codec_version"]) > 4:
        raise ValueError("invalid codec version metadata")
    for key, value in tool["codec_version"].items():
        _string(key, "codec version key", 64)
        _string(value, "codec version", 64)
    sampling = manifest["sampling"]
    if not isinstance(sampling, dict) or set(sampling) != {"method", "area_samples"} or sampling["method"] != "bilinear-jacobian-midpoint-quadrature":
        raise ValueError("invalid sampling metadata")
    _bounded_int(sampling["area_samples"], "area samples", 16, 1)
    tolerance = manifest["road_tolerance_m"]
    if isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or not 0 <= tolerance <= 1000:
        raise ValueError("invalid road tolerance")
    if not isinstance(manifest["tiles"], list) or not 1 <= len(manifest["tiles"]) <= 65536:
        raise ValueError("invalid tile inventory")
    try:
        tiles = [Tile(**t) for t in manifest["tiles"]]
    except (TypeError, KeyError) as exc:
        raise ValueError("invalid tile inventory") from exc
    if len(tiles) != len(set(tiles)):
        raise ValueError("duplicate tile identity")
    entries = manifest["files"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_ENTRIES:
        raise ValueError("invalid file inventory")
    listed, folded, total = {}, set(), 0
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"path", "kind", "bytes", "sha256"}:
            raise ValueError("invalid file entry")
        rel = entry["path"]
        _safe_path(rel)
        if rel == "manifest.json" or rel.casefold() in folded:
            raise ValueError("duplicate or reserved pack path")
        folded.add(rel.casefold())
        kind = entry["kind"]
        if kind not in ("elevation", "roads", "airports", "license"):
            raise ValueError("unsupported file kind")
        maximum = MAX_TILE_BYTES if kind == "elevation" else MAX_LICENSE_BYTES if kind == "license" else MAX_FILE_BYTES
        total += _bounded_int(entry["bytes"], "payload size", maximum, 1)
        if total > MAX_PACK_BYTES:
            raise ValueError("total pack byte limit exceeded")
        _check_sha(entry["sha256"])
        listed[rel] = entry
    sources = manifest["sources"]
    if not isinstance(sources, list) or not 1 <= len(sources) <= 64:
        raise ValueError("invalid source inventory")
    source_ids = set()
    for source in sources:
        _verify_source(source, listed)
        if source["id"] in source_ids:
            raise ValueError("duplicate source ID")
        source_ids.add(source["id"])
    if "elevation" not in source_ids or not source_ids.issubset({"elevation", "elevation-header", "roads", "airports", "runways"}) or (("airports" in source_ids) != ("runways" in source_ids)):
        raise ValueError("incomplete or unknown sources")
    expected = {"elevation/" + t.key + ".elv": "elevation" for t in tiles}
    if "roads" in source_ids:
        expected.update({"roads/" + t.key + ".json": "roads" for t in tiles})
    if "airports" in source_ids:
        expected["airports.json"] = "airports"
    expected.update({source["license_path"]: "license" for source in sources})
    if set(expected) != set(listed) or any(listed[p]["kind"] != kind for p, kind in expected.items()):
        raise ValueError("payload inventory does not match tile/source roster")
    # Enumerate first, reject nonregular files/symlinks and unlisted entries.
    actual, entry_count = set(), 0
    expected_dirs = {str(p) for rel in listed for p in PurePosixPath(rel).parents if str(p) != "."}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(directory) / name
            entry_count += 1
            if entry_count > MAX_ENTRIES * 4:
                raise ValueError("directory entry limit exceeded")
            if path.is_symlink():
                raise ValueError("symlink in pack")
            rel = path.relative_to(root).as_posix()
            if name in dirs:
                if not path.is_dir() or rel not in expected_dirs:
                    raise ValueError("unexpected pack directory")
            else:
                if not path.is_file():
                    raise ValueError("nonregular pack file")
                actual.add(rel)
    if actual != set(listed) | {"manifest.json"}:
        raise ValueError("missing or unlisted pack file")
    airport_count = 0
    for rel, entry in listed.items():
        raw = read_bounded(root / rel, entry["bytes"])
        if len(raw) != entry["bytes"] or hashlib.sha256(raw).hexdigest() != entry["sha256"]:
            raise ValueError("payload size or SHA256 mismatch: " + rel)
        if entry["kind"] == "elevation":
            value = decode_tile(raw)
            if "elevation/" + value["tile"].key + ".elv" != rel or value["codec"] != tool["codec"]:
                raise ValueError("elevation identity/codec mismatch")
        elif entry["kind"] == "roads":
            tile = validate_road_tile(strict_json(raw))
            if "roads/" + tile.key + ".json" != rel:
                raise ValueError("road tile identity mismatch")
        elif entry["kind"] == "airports":
            value = strict_json(raw)
            if not isinstance(value, dict) or set(value) != {"version", "airports"} or (type(value["version"]) is not int or value["version"] != 1):
                raise ValueError("invalid airport table schema")
            validate_airports(value["airports"])
            airport_count = len(value["airports"])
    measurements = manifest["measurements"]
    if not isinstance(measurements, dict) or any(type(v) is not int for v in measurements.values()) or measurements != {"source_bytes": sum(s["bytes"] for s in sources),
            "payload_bytes": total, "elevation_tiles": len(tiles), "airport_count": airport_count}:
        raise ValueError("manifest measurement mismatch")
    return manifest
