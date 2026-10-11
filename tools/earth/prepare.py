"""Explicit offline input adapters; no implicit downloads or license grants."""
from collections import Counter
import math
import json
from pathlib import Path
import re
import subprocess
import tempfile

from .common import canonical_json, read_bounded, sha256_file, strict_json
from .elevation import EnviGrid, Grid
from .roads import CLASSES, MAX_ROAD_FILE, read_geojson


def _new_output(path):
    path = Path(path)
    if path.exists() or path.is_symlink():
        raise ValueError("output already exists: " + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def prepare_opendap(source, output):
    """Parse NOAA ETOPO's bounded DAP2 ASCII subset response into grid-json."""
    output = _new_output(output)
    text = read_bounded(source, 64 * 1024 * 1024).decode("ascii")
    match = re.search(r"^z\.z\[(\d+)\]\[(\d+)\]\s*$", text, re.MULTILINE)
    if match is None:
        raise ValueError("missing NOAA z.z grid")
    height, width = map(int, match.groups())
    if min(width, height) < 2 or width * height > 4_000_000:
        raise ValueError("unsupported OPeNDAP subset size")
    rest = text[match.end():]
    lat_marker = re.search(r"^z\.lat\[(\d+)\]\s*$", rest, re.MULTILINE)
    lon_marker = re.search(r"^z\.lon\[(\d+)\]\s*$", rest, re.MULTILINE)
    if lat_marker is None or lon_marker is None or lon_marker.start() <= lat_marker.end():
        raise ValueError("missing OPeNDAP coordinate axes")
    if int(lat_marker.group(1)) != height or int(lon_marker.group(1)) != width:
        raise ValueError("OPeNDAP coordinate size mismatch")
    rows = []
    for line in rest[:lat_marker.start()].strip().splitlines():
        index, separator, numbers = line.partition(",")
        if not separator or index.strip() != f"[{len(rows)}]":
            raise ValueError("invalid OPeNDAP row index")
        row = [float(v) for v in numbers.split(",")]
        if len(row) != width:
            raise ValueError("OPeNDAP row width mismatch")
        rows.append(row)
    if len(rows) != height:
        raise ValueError("OPeNDAP row count mismatch")
    def axis(raw, length):
        # THREDDS ASCII axes are one unindexed CSV line.
        values = [float(v) for v in raw.strip().split(",")]
        if len(values) != length or not all(math.isfinite(v) for v in values):
            raise ValueError("invalid OPeNDAP axis")
        step = (values[-1] - values[0]) / (length - 1)
        if step <= 0 or any(abs(v - (values[0] + i * step)) > 1e-8 for i, v in enumerate(values)):
            raise ValueError("OPeNDAP axis is not ascending and regular")
        whole_arcseconds = round(step * 3600)
        if whole_arcseconds > 0 and abs(step * 3600 - whole_arcseconds) < 1e-8:
            step = whole_arcseconds / 3600
        return values[0], step
    lat_min, lat_step = axis(rest[lat_marker.end():lon_marker.start()], height)
    lon_min, lon_step = axis(rest[lon_marker.end():], width)
    value = {"version": 1, "lon_min": lon_min, "lat_min": lat_min,
             "lon_step": lon_step, "lat_step": lat_step, "periodic": False, "heights": rows}
    Grid(value)  # Validate every value and extent before writing.
    # Fixed field insertion order is part of this adapter v1 format.
    output.write_bytes((json.dumps(value, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8"))
    return {"path": str(output), "bytes": output.stat().st_size, "sha256": sha256_file(output),
            "origin": {"url": "record-the-original-request-URL", "sha256": sha256_file(source),
                       "bytes": Path(source).stat().st_size,
                       "transformation": "tools.earth prepare-opendap v1; ascending regular axes; no resampling"}}


def prepare_grip(source, output, id_field=None, class_field="gp_rtp", unknown_class=None):
    """Normalize an ogr2ogr-exported GRIP GeoJSON with explicit class semantics.

    gp_rtp 1..5 maps highway..local. Unknown 0 fails unless explicitly assigned;
    gp_rex 3/4 is omitted (closed/construction). Export order plus pinned input
    hash identifies a feature when no trustworthy source ID field is supplied.
    Coordinate-equal vertices share IDs; crossings without source vertices are
    never silently declared junctions.
    """
    output = _new_output(output)
    value = strict_json(read_bounded(source, MAX_ROAD_FILE))
    if not isinstance(value, dict) or value.get("type") != "FeatureCollection" or not isinstance(value.get("features"), list):
        raise ValueError("expected GRIP GeoJSON FeatureCollection")
    if unknown_class is not None and unknown_class not in CLASSES:
        raise ValueError("invalid unspecified-road class override")
    source_hash = sha256_file(source)
    result, excluded, class_counts = [], Counter(), Counter()
    for index, feature in enumerate(value["features"]):
        if not isinstance(feature, dict) or feature.get("type") != "Feature" or not isinstance(feature.get("properties"), dict):
            raise ValueError("invalid GRIP feature")
        props = {key.casefold(): val for key, val in feature["properties"].items()}
        if len(props) != len(feature["properties"]):
            raise ValueError("ambiguous case-insensitive GRIP field names")
        road_type = props.get(class_field.casefold())
        if type(road_type) is not int or not 0 <= road_type <= 5:
            raise ValueError("unknown or missing GRIP road class")
        access = props.get("gp_rex", 0)
        if type(access) is not int or not 0 <= access <= 4:
            raise ValueError("invalid GRIP road access")
        if access in (3, 4):
            excluded["closed_or_construction"] += 1
            continue
        if road_type == 0 and unknown_class is None:
            raise ValueError("GRIP gp_rtp=0 needs an explicit --unknown-class decision")
        cls = CLASSES[road_type - 1] if road_type else unknown_class
        road_id = props.get(id_field.casefold()) if id_field else f"{source_hash[:16]}:{index}"
        if road_id is None:
            raise ValueError("requested GRIP ID field missing")
        geometry = feature.get("geometry", {})
        if geometry.get("type") == "LineString":
            parts = [geometry.get("coordinates")]
        elif geometry.get("type") == "MultiLineString":
            parts = geometry.get("coordinates")
        else:
            raise ValueError("GRIP geometry must be LineString/MultiLineString")
        if not isinstance(parts, list) or not parts:
            raise ValueError("empty GRIP geometry")
        for part_index, points in enumerate(parts):
            feature_id = str(road_id) + (f":part:{part_index}" if len(parts) > 1 else "")
            provenance = {key: props[key] for key in ("gp_rsi", "gp_rsy", "gp_rcy", "gp_gripreg", "gp_rex", "gp_rav") if key in props}
            provenance.update({"source_feature_id": str(road_id), "source_sha256": source_hash})
            result.append({"type": "Feature", "properties": {"id": feature_id, "class": cls, "provenance": provenance},
                           "geometry": {"type": "LineString", "coordinates": points}})
            class_counts[cls] += 1
    raw = canonical_json({"type": "FeatureCollection", "features": result})
    if len(raw) > MAX_ROAD_FILE:
        raise ValueError("normalized road file too large; prepare smaller regions")
    # Validate prior to committing output, including IDs and coordinate ranges.
    from .roads import _normalise_features
    _normalise_features(result)
    output.write_bytes(raw)
    return {"path": str(output), "sha256": sha256_file(output), "bytes": len(raw),
            "class_counts": dict(class_counts), "excluded": dict(excluded), "source_sha256": source_hash}


def prepare_raster(source, output):
    """Convert a local GeoTIFF through installed GDAL in isolated staging.

    Only the GTiff driver is enabled; VRT and other reference-bearing formats
    are not accepted. A single writer must own the output and its sidecars.
    """
    output = _new_output(output)
    header = output.with_suffix(".hdr")
    sidecar = Path(str(output) + ".aux.xml")
    if header == output or any(p.exists() or p.is_symlink() for p in (header, sidecar)):
        raise ValueError("ENVI header/sidecar output already exists or aliases binary")
    source = Path(source).absolute()
    if source.is_symlink() or not source.is_file():
        raise ValueError("raster source must be a local regular GeoTIFF")
    with source.open("rb") as stream:
        if stream.read(4) not in (b"II*\0", b"MM\0*", b"II+\0", b"MM\0+"):
            raise ValueError("raster preparation supports local GeoTIFF only")
    with tempfile.TemporaryDirectory(prefix=".earth-raster-", dir=output.parent) as temporary:
        staged = Path(temporary) / output.name
        try:
            subprocess.run(["gdal_translate", "-if", "GTiff", "-of", "ENVI", "-ot", "Float32",
                            "-co", "INTERLEAVE=BSQ", str(source), str(staged)],
                           check=True, capture_output=True, text=True)
            header_path = staged.with_suffix(".hdr")
            header_text = header_path.read_text(encoding="ascii")
            header_text = re.sub(r"(?im)^description\s*=\s*\{[^}]*\}",
                                 "description = {Solace Earth deterministic ENVI input}", header_text)
            header_path.write_text(header_text, encoding="ascii", newline="\n")
            grid = EnviGrid(staged)
            grid.close()
        except (OSError, subprocess.CalledProcessError, ValueError) as exc:
            raise ValueError("GDAL raster preparation failed: " + str(exc)) from exc
        artifacts = [(staged, output), (staged.with_suffix(".hdr"), header)]
        extra = Path(str(staged) + ".aux.xml")
        if extra.exists():
            artifacts.append((extra, sidecar))
        if any(dst.exists() or dst.is_symlink() for _, dst in artifacts):
            raise ValueError("raster output appeared during preparation")
        for src, dst in artifacts:
            src.rename(dst)
    return {"path": str(output), "bytes": output.stat().st_size, "sha256": sha256_file(output),
            "header_sha256": sha256_file(header), "source_sha256": sha256_file(source)}
