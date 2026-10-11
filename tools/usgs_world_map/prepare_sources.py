"""Prepare/audit a single global source for the existing tools.earth interface.

Acquisition is explicit. This module delegates conversion to prepare_raster;
it does not implement another tile writer or change elevation datums.
Run from the repo root: python3 -m tools.usgs_world_map.prepare_sources --help.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
import zipfile

from tools.earth.common import read_bounded, sha256_file, strict_json
from tools.earth.cube import (SAMPLES, WIDTH, Tile, cube_area_weight,
                              direction_to_latlon, face_uv_to_direction,
                              sample_location)
from tools.earth.elevation import EnviGrid, decode_tile
from tools.earth.pack import verify_pack
from tools.earth.prepare import prepare_raster

INTERFACE_COMMIT = "ebcbba8c4c5f839894cd2f326c4818454c29fa2f"
ETOPO_URL = "https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif"
ETOPO_SHA = "9d27d4b8ea8e76977e2988bca667d7c8fa68b927355feffcddd6b4875a7fd08e"
ETOPO_BYTES = 465969062
GMTED_URL = "https://edcintl.cr.usgs.gov/downloads/sciweb1/shared/topo/downloads/GMTED/Grid_ZipFiles/mn30_grd.zip"
GMTED_SHA = "dfd0d6c6486f4da22109be6107c93a70ef9917a5b6f954cd82d87cf8b920149d"
GMTED_BYTES = 277229566
BIN_BYTES = 21600 * 10800 * 4
NOTICE = """NOAA ETOPO 2022 credit and license-link notice (not full legal text)
NOAA National Centers for Environmental Information. 2022: ETOPO 2022
15 Arc-Second Global Relief Model. NOAA National Centers for Environmental
Information. https://doi.org/10.25921/fd45-gt74 . Accessed 2026-10-11.
This input is the v1 60-arc-second ice-surface derivative, including bathymetry.
NOAA's official metadata specifies CC0-1.0 worldwide:
https://data.noaa.gov/metaview/page?header=none&view=getDataView&xml=NOAA/NESDIS/NGDC/MGG/DEM//iso/xml/etopo_2022.xml
Legal text: https://creativecommons.org/publicdomain/zero/1.0/legalcode
NOAA/NCEI provide no warranty of accuracy, reliability or completeness.
The source is not suitable for navigation. This artifact is game-development
elevation data; it does not establish terrain truth or source metre accuracy.
"""


def write_json(path, data, *, exclusive=False):
    raw = json.dumps(data, indent=2, sort_keys=True, allow_nan=False) + "\n"
    # Exclusive creation also refuses dangling links and a destination that
    # appears after the caller's preflight. Never truncate an existing config.
    with Path(path).open("x" if exclusive else "w", encoding="utf-8") as stream:
        stream.write(raw)


def file_record(path):
    path = Path(path)
    return {"path": path.name, "bytes": path.stat().st_size,
            "sha256": sha256_file(path)}


def verify_download(path, size, digest):
    if path.is_symlink() or not path.is_file():
        raise ValueError("source must be a regular local file")
    record = file_record(path)
    if record["bytes"] != size or record["sha256"] != digest:
        raise ValueError("download differs from the pinned source: " + str(path))
    return record


def metadata(path):
    import rasterio

    with rasterio.open(path) as src:
        return {"driver": src.driver, "width": src.width, "height": src.height,
                "bands": src.count, "dtype": src.dtypes[0], "crs": str(src.crs),
                "bounds_west_south_east_north": list(src.bounds),
                "affine_a_b_c_d_e_f": list(src.transform)[:6],
                "nodata": src.nodata, "registration": "pixel-area",
                "row_order": "north-to-south", "column_order": "west-to-east"}


def budget(parent):
    memory = {}
    if Path("/proc/meminfo").exists():
        for line in Path("/proc/meminfo").read_text().splitlines():
            key, value = line.split(":", 1)
            if key in ("MemTotal", "MemAvailable"):
                memory[key + "_bytes"] = int(value.split()[0]) * 1024
    disk = shutil.disk_usage(parent)
    return {"available_disk_bytes": disk.free, "memory": memory,
            "prepared_binary_exact_bytes": BIN_BYTES,
            "recommended_incremental_disk_bytes": 3 * BIN_BYTES + 64 * 1024**2,
            "audit_strip_bytes_per_array": 256 * 21600 * 4,
            "GDAL_CACHEMAX_MiB": 128,
            "note": "Disk recommendation includes prepared raster, packaging and headroom; source already exists. No large global tile pack is built."}


def audit_raster(original, prepared):
    """Compare all 233,280,000 Float32 pixels bit-for-bit, in bounded strips."""
    import numpy as np
    import rasterio
    from rasterio.windows import Window

    before, after = metadata(original), metadata(prepared)
    for key in ("width", "height", "bands", "dtype", "nodata"):
        if before[key] != after[key]:
            raise ValueError("conversion changed raster geometry/type: " + key)
    for key in ("bounds_west_south_east_north", "affine_a_b_c_d_e_f"):
        if not np.allclose(before[key], after[key], rtol=0, atol=1e-9):
            raise ValueError("conversion changed geometry beyond header text precision: " + key)
    if before["width"] != 21600 or before["height"] != 10800 or before["dtype"] != "float32":
        raise ValueError("unexpected pinned ETOPO grid")
    grid = EnviGrid(prepared)
    try:
        offset, item_type = grid.offset, np.dtype(grid.item.format)
        minimum, maximum = math.inf, -math.inf
        min_rc = max_rc = None
        missing = nonfinite = different = pixels = 0
        probes = []
        with rasterio.Env(GDAL_CACHEMAX=128 * 1024**2), rasterio.open(original) as src, prepared.open("rb") as stream:
            stream.seek(offset)
            for row in range(0, src.height, 256):
                count = min(256, src.height - row)
                a = src.read(1, window=Window(0, row, src.width, count))
                b = np.fromfile(stream, dtype=item_type, count=count * src.width).reshape(a.shape)
                # Byte order is normalized by asarray; Float32 values are not rounded.
                b = np.asarray(b, dtype=np.float32)
                different += int(np.count_nonzero(a.view(np.uint32) != b.view(np.uint32)))
                missing += int(np.count_nonzero(a == src.nodata))
                nonfinite += int(np.count_nonzero(~np.isfinite(a)))
                lo, hi = float(a.min()), float(a.max())
                if lo < minimum:
                    minimum = lo
                    r, c = np.unravel_index(int(a.argmin()), a.shape)
                    min_rc = (row + int(r), int(c))
                if hi > maximum:
                    maximum = hi
                    r, c = np.unravel_index(int(a.argmax()), a.shape)
                    max_rc = (row + int(r), int(c))
                pixels += a.size
                if row % 2048 == 0:
                    print(f"Audited {row + count}/{src.height} rows", flush=True)
            if stream.read(1):
                raise ValueError("unexpected trailing ENVI data")
            for name, lat, lon in (("Everest vicinity", 27.9881, 86.925),
                                  ("Death Valley", 36.24, -116.82),
                                  ("Amazon", -3.465, -62.215),
                                  ("Uluru", -25.3, 131),
                                  ("Greenland ice surface", 72, -40),
                                  ("Antarctic ice surface", -80, 0),
                                  ("Atlantic", 32.5, -58),
                                  ("Mariana vicinity", 11.35, 142.2)):
                r, c = src.index(lon, lat)
                value = float(src.read(1, window=Window(c, r, 1, 1))[0, 0])
                probes.append({"name": name, "lat": lat, "lon": lon,
                               "nearest_source_cell_height_m": value,
                               "bilinear_sampler_height_m": grid.sample(lat, lon)})
            def extremum(value, rc):
                x, y = src.xy(*rc)
                return {"metres": value, "row": rc[0], "column": rc[1],
                        "cell_centre_lon": x, "cell_centre_lat": y}
            extrema = {"minimum": extremum(minimum, min_rc), "maximum": extremum(maximum, max_rc)}
        if different or missing or nonfinite or minimum < -12000 or maximum > 10000:
            raise ValueError(f"invalid global baseline: changed={different}, nodata={missing}, nonfinite={nonfinite}")
        wrap_checks = []
        for lat in (-90, -89.999, -80, -45, 0, 32.5, 45, 80, 89.999, 90):
            values = [grid.sample(lat, lon) for lon in (-540, -180, 180, 540)]
            if max(values) - min(values) > 1e-7:
                raise ValueError("longitude wrap mismatch")
            wrap_checks.append({"latitude": lat, "height_at_antimeridian_m": values[0]})
        caps = []
        for lat, centre in ((90, 90 - 1/120), (-90, -90 + 1/120)):
            for lon in (-180, -90, 0, 90, 180):
                pole, edge = grid.sample(lat, lon), grid.sample(centre, lon)
                if abs(pole - edge) > 1e-6:
                    raise ValueError("half-cell polar extension mismatch")
                caps.append({"latitude": lat, "longitude": lon, "height_m": pole})
        return {"status": "passed", "pixels_compared_bit_for_bit": int(pixels),
                "changed_float32_pixels": different, "nodata_pixels": missing,
                "nonfinite_pixels": nonfinite, "extrema": extrema, "probes": probes,
                "longitude_wrap_checks": wrap_checks, "polar_cap_checks": caps,
                "polar_policy": "Existing EnviGrid extends nearest latitude centre over the half-cell cap of a complete global raster; longitude remains periodic.",
                "vertical_reference": "EGM2008 orthometric metres; unchanged",
                "source_accuracy": "Not measured by this conversion audit"}
    finally:
        grid.close()


def preview(prepared, destination):
    """A labeled diagnostic map, never a satellite/albedo/biome atlas."""
    import numpy as np
    import rasterio
    from rasterio.enums import Resampling
    from PIL import Image, ImageDraw, ImageFont
    with rasterio.open(prepared) as src:
        values = src.read(1, out_shape=(1024, 2048), resampling=Resampling.average)
    levels = [-11000, -6000, -2000, -200, 0, 100, 500, 1500, 3000, 5000, 8200]
    colours = [(7,19,39),(14,53,85),(21,99,134),(71,153,175),(109,181,166),
               (72,121,71),(134,150,81),(176,153,113),(141,120,105),(197,194,181),(250,250,245)]
    rgb = np.stack([np.interp(values, levels, [c[i] for c in colours]) for i in range(3)], axis=-1).astype("uint8")
    canvas = Image.new("RGB", (2048, 1120), (239,242,240))
    canvas.paste(Image.fromarray(rgb), (0, 0))
    draw = ImageDraw.Draw(canvas)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    font = ImageFont.truetype(font_path, 24) if Path(font_path).exists() else ImageFont.load_default()
    draw.text((24,1032), "GLOBAL ELEVATION DATA | ETOPO 2022 v1, 60 arc-seconds | EGM2008 metres", fill=(25,40,40), font=font)
    draw.text((24,1070), "Diagnostic equirectangular map: blue = sea floor; green/brown/white = land and ice. Not game imagery.", fill=(45,60,60), font=font)
    canvas.save(destination)
    with Image.open(destination) as checked:
        checked.load()


def prepare(args):
    # Only acquisition/audit needs these optional dependencies and Unix RSS
    # accounting. Offline handoff verification remains standard-library-only.
    import numpy as np
    import rasterio
    import resource

    source, output = Path(args.etopo).absolute(), Path(args.output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("use a new, single-writer output directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    preflight = budget(output.parent)
    print(json.dumps(preflight, indent=2), flush=True)
    if preflight["available_disk_bytes"] < preflight["recommended_incremental_disk_bytes"]:
        raise ValueError("insufficient disk for source preparation and packaging")
    origin = verify_download(source, ETOPO_BYTES, ETOPO_SHA)
    extra = None
    if args.gmted_zip:
        archive = Path(args.gmted_zip).absolute()
        extra = verify_download(archive, GMTED_BYTES, GMTED_SHA)
        with zipfile.ZipFile(archive) as z:
            if z.testzip() is not None:
                raise ValueError("GMTED archive CRC failure")
        extra.update(url=GMTED_URL, version="GMTED2010-mean-30arcsec",
                     license="USGS public domain with requested credit",
                     status="Separate comparison source only; not converted, blended or sampled into the ETOPO pack",
                     vertical_reference="EGM96-dominant; source report documents other local datums",
                     datum_source="https://pubs.usgs.gov/of/2011/1073/pdf/of2011-1073.pdf")
    gdal = subprocess.run(["gdal_translate", "--version"], capture_output=True, text=True, check=True).stdout.strip()
    output.mkdir()
    prepared = output / "etopo2022_surface_60s.bin"
    os.environ["GDAL_CACHEMAX"] = "128"
    start = time.perf_counter()
    conversion = prepare_raster(source, prepared)  # Existing published adapter.
    conversion_seconds = time.perf_counter() - start
    start = time.perf_counter()
    audit = audit_raster(source, prepared)
    audit_seconds = time.perf_counter() - start
    write_json(output / "source-audit.json", audit)
    (output / "NOAA-CC0-notice.txt").write_text(NOTICE, encoding="utf-8")
    if args.preview:
        preview(prepared, output / "global-elevation-diagnostic.png")
    transform = "tools.earth prepare-raster at " + INTERFACE_COMMIT + "; GDAL GTiff->ENVI Float32 BSQ; same cells and bounds; no resampling, quantization or vertical transformation; canonical header description"
    manifest = {"format": "solace-global-elevation-source-preparation", "version": 1,
                "interface_commit": INTERFACE_COMMIT,
                "original": {**origin, "url": ETOPO_URL, "version": "ETOPO2022-v1-surface-60arcsec", "metadata": metadata(source)},
                "prepared": {**file_record(prepared), "metadata": metadata(prepared), "vertical_reference": "EGM2008 orthometric metres"},
                "transformation": transform, "gmted_comparison": extra,
                "terms": {"license_id": "CC0-1.0", "notice": "NOAA-CC0-notice.txt", "credit_doi": "https://doi.org/10.25921/fd45-gt74"},
                "preflight": preflight, "environment": {"python": platform.python_version(), "numpy": np.__version__, "rasterio": rasterio.__version__, "rasterio_gdal": rasterio.__gdal_version__, "gdal_translate": gdal, "platform": platform.platform()},
                "measured": {"conversion_seconds": conversion_seconds, "audit_seconds": audit_seconds,
                             "process_peak_RSS_KiB_linux": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                             "child_peak_RSS_KiB_linux": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss},
                "adapter_result": {k: v for k, v in conversion.items() if k != "path"},
                "files": [file_record(p) for p in sorted(output.iterdir()) if p.is_file()]}
    write_json(output / "source-manifest.json", manifest)
    print(json.dumps({"status": "prepared-and-audited", "output": str(output), "measured": manifest["measured"]}, indent=2))


def _envi_header(prepared):
    """Match EnviGrid's header selection without ignoring a dangling symlink."""
    header = prepared.with_suffix(".hdr")
    if not header.exists() and not header.is_symlink():
        header = Path(str(prepared) + ".hdr")
    return header


def _verify_file_record(path, record, label):
    if not isinstance(record, dict) or type(record.get("bytes")) is not int:
        raise ValueError("missing/invalid " + label + " record")
    if path.is_symlink() or not path.is_file():
        raise ValueError(label + " must be a regular local file")
    actual = file_record(path)
    if any(actual[key] != record.get(key) for key in ("bytes", "sha256")):
        raise ValueError(label + " bytes/SHA256 mismatch")
    return actual


def _prepared_source(root, manifest):
    """Bind both ENVI inputs to the preparation manifest before sampling."""
    if not isinstance(manifest, dict) or not isinstance(manifest.get("prepared"), dict):
        raise ValueError("missing prepared source record")
    name = manifest["prepared"].get("path")
    # Preparation writes basenames. Do not let a supplied manifest escape root
    # or reinterpret a Windows drive/UNC path on another operating system.
    if (not isinstance(name, str) or not name or name in (".", "..")
            or any(c in name for c in ("/", "\\", ":", "\0"))):
        raise ValueError("prepared source path must be a local filename")
    prepared = root / name
    header = _envi_header(prepared)
    files = manifest.get("files")
    if not isinstance(files, list) or any(not isinstance(f, dict) for f in files):
        raise ValueError("missing/invalid prepared source inventory")
    baseline = manifest["prepared"]
    for path, label in ((prepared, "prepared baseline"), (header, "prepared header")):
        records = [f for f in files if f.get("path") == path.name]
        if len(records) != 1:
            raise ValueError("missing/duplicate " + label + " record")
        if path == prepared and (type(baseline.get("bytes")) is not int or
                any(baseline.get(key) != records[0].get(key) for key in ("bytes", "sha256"))):
            raise ValueError("prepared baseline and inventory disagree")
        _verify_file_record(path, records[0], label)
    return prepared, records[0]


def make_sample(args):
    root = Path(args.source_dir).absolute()
    path = root / "representative-L4.config.json"
    if path.exists() or path.is_symlink():
        raise ValueError("sample config already exists")
    value = strict_json(read_bounded(root / "source-manifest.json", 4 * 1024 * 1024))
    prepared, header_record = _prepared_source(root, value)
    # L4: near the source's native resolution, plus within/cross-face aprons.
    tiles = [Tile("py",4,7,12), Tile("py",4,8,12),
             Tile("ny",4,2,15), Tile("ny",4,3,15),
             Tile("ny",4,13,14), Tile("ny",4,14,14),
             Tile("pz",4,5,5), Tile("nx",4,1,10),
             Tile("px",4,15,7), Tile("py",4,0,7),
             Tile("nx",4,7,7), Tile("nx",4,8,7)]
    tiles += [Tile(face,4,x,y) for face in ("pz", "nz") for x in (7,8) for y in (7,8)]
    config = {"version": 1, "name": "etopo2022-global-source-representative-L4",
              "coverage": "20 selected L4 tiles: Himalaya, California coast, Atlantic, Greenland, Mariana, both poles, antimeridian and a cube-face edge. Bounded diagnostic sample; not a complete global tile inventory.",
              "codec": "zstd", "area_samples": 2,
              "tiles": [t.as_dict() for t in tiles],
              "elevation": {"path": prepared.name, "format": "envi", "url": ETOPO_URL,
                            "version": "ETOPO2022-v1-surface-60arcsec", "resolution_arcsec": 60,
                            "sha256": value["prepared"]["sha256"], "license_id": "CC0-1.0",
                            "header": {key: header_record[key] for key in ("sha256", "bytes")},
                            "license_path": "NOAA-CC0-notice.txt",
                            "description": "North-up WGS84 geographic Float32; EGM2008 heights unchanged; complete 360x180 global source",
                            "origin": {"url": ETOPO_URL, "sha256": ETOPO_SHA, "bytes": ETOPO_BYTES,
                                       "transformation": value["transformation"]}}}
    write_json(path, config, exclusive=True)
    print(json.dumps(file_record(path), indent=2))


def check_sample(args):
    root = Path(args.pack).absolute()
    manifest = verify_pack(root)  # Existing bounded verifier.
    decoded = {}
    for spec in manifest["tiles"]:
        tile = Tile(**spec)
        decoded[tile] = decode_tile((root / ("elevation/" + tile.key + ".elv")).read_bytes())["heights"]
    counts = {"same_face": 0, "cross_face": 0, "corner": 0}
    for tile, heights in decoded.items():
        boundary = [(c,r) for r in (-1,SAMPLES) for c in range(-1,SAMPLES+1)]
        boundary += [(c,r) for c in (-1,SAMPLES) for r in range(SAMPLES)]
        for col, row in boundary:
            face, i, j = sample_location(tile,col,row)
            other = Tile(face,tile.level,i//SAMPLES,j//SAMPLES)
            if other not in decoded:
                continue
            core = (j % SAMPLES + 1) * WIDTH + i % SAMPLES + 1
            ghost = (row + 1) * WIDTH + col + 1
            if heights[ghost] != decoded[other][core]:
                raise ValueError("quantized apron/core mismatch: " + tile.key)
            kind = "corner" if col in (-1,SAMPLES) and row in (-1,SAMPLES) else ("same_face" if face == tile.face else "cross_face")
            counts[kind] += 1
    if not counts["same_face"] or not counts["cross_face"]:
        raise ValueError("sample does not exercise both seam categories")
    result = {"status": "verified", "manifest": file_record(root / "manifest.json"),
              "pack_bytes": sum(p.stat().st_size for p in root.rglob("*") if p.is_file()),
              "elevation_bytes": sum(f["bytes"] for f in manifest["files"] if f["kind"] == "elevation"),
              "tile_count": len(decoded), "apron_core_exact_match_counts": counts,
              "sampling": manifest["sampling"], "producer": manifest["tool"]}
    if args.repeat:
        repeat = Path(args.repeat).absolute()
        verify_pack(repeat)
        inventory = lambda p: {str(f.relative_to(p)): file_record(f) for f in p.rglob("*") if f.is_file()}
        if inventory(root) != inventory(repeat):
            raise ValueError("repeat sample differs")
        result["same_environment_repeat_all_file_hashes_match"] = True
    write_json(args.output, result)
    print(json.dumps(result, indent=2))


def convergence(args):
    """Probe quadrature choices; this is not a ground-truth accuracy test."""
    root = Path(args.pack).absolute()
    manifest = verify_pack(root)
    prepared = Path(args.prepared).absolute()
    source_records = {}
    for ident, path in (("elevation", prepared), ("elevation-header", _envi_header(prepared))):
        records = [s for s in manifest["sources"] if s["id"] == ident]
        if len(records) != 1:
            raise ValueError("pack must record exactly one " + ident + " source")
        source_records[ident] = _verify_file_record(path, records[0], ident)
    grid = EnviGrid(prepared)
    comparisons, four_to_eight, eight_to_sixteen, quantization_errors = [], [], [], []
    try:
        for spec in manifest["tiles"]:
            tile = Tile(**spec)
            heights = decode_tile((root / ("elevation/" + tile.key + ".elv")).read_bytes())["heights"]
            n = SAMPLES * (1 << tile.level)
            for row in range(16, SAMPLES, 32):
                for col in range(16, SAMPLES, 32):
                    face, i, j = sample_location(tile, col, row)
                    def value(samples):
                        total = weights = 0.0
                        for sy in range(samples):
                            v = 2 * (j + (sy + .5) / samples) / n - 1
                            for sx in range(samples):
                                u = 2 * (i + (sx + .5) / samples) / n - 1
                                lat, lon = direction_to_latlon(face_uv_to_direction(face,u,v))
                                weight = cube_area_weight(u,v)
                                total += grid.sample(lat,lon) * weight
                                weights += weight
                        return total / weights
                    packed_sampling = value(manifest["sampling"]["area_samples"])
                    four_sampling = value(4)
                    refined_sampling = value(8)
                    comparisons.append(abs(packed_sampling - refined_sampling))
                    four_to_eight.append(abs(four_sampling - refined_sampling))
                    eight_to_sixteen.append(abs(refined_sampling - value(16)))
                    quantization_errors.append(abs(heights[(row+1)*WIDTH+col+1] - packed_sampling))
        # Quantization is checked against the pack's recorded quadrature N,
        # never N8/N16: their convergence differences can legitimately exceed
        # half a metre. Allow only a tiny floating-point accumulation tolerance.
        maximum_quantization_error = max(quantization_errors)
        if maximum_quantization_error > .5 + 1e-6:
            raise ValueError("pack quantization residual exceeds half a metre: "
                             + str(maximum_quantization_error))
        result = {"probe_count": len(comparisons), "probe_core_indices": "16,48,...,240 on each axis of each sample tile",
                  "source": source_records["elevation"],
                  "source_header": source_records["elevation-header"],
                  "source_matches_pack": True,
                  "quadrature": {"pack_N": manifest["sampling"]["area_samples"], "comparison_N": 8,
                                 "max_absolute_difference_m": max(comparisons), "mean_absolute_difference_m": sum(comparisons)/len(comparisons)},
                  "N4_to_N8": {"max_absolute_difference_m": max(four_to_eight), "mean_absolute_difference_m": sum(four_to_eight)/len(four_to_eight)},
                  "N8_to_N16": {"max_absolute_difference_m": max(eight_to_sixteen), "mean_absolute_difference_m": sum(eight_to_sixteen)/len(eight_to_sixteen)},
                  "max_quantization_error_m_on_probes": maximum_quantization_error,
                  "limitation": "Sparse convergence diagnostic relative to the same interpolated ETOPO source, not a global error bound or true terrain accuracy."}
        write_json(args.output,result)
        print(json.dumps(result,indent=2))
    finally:
        grid.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare")
    p.add_argument("--etopo", required=True)
    p.add_argument("--gmted-zip")
    p.add_argument("--output", required=True)
    p.add_argument("--preview", action="store_true")
    p.set_defaults(run=prepare)
    p = commands.add_parser("make-sample")
    p.add_argument("--source-dir", required=True)
    p.set_defaults(run=make_sample)
    p = commands.add_parser("check-sample")
    p.add_argument("--pack", required=True)
    p.add_argument("--repeat")
    p.add_argument("--output", required=True)
    p.set_defaults(run=check_sample)
    p = commands.add_parser("convergence")
    p.add_argument("--pack", required=True)
    p.add_argument("--prepared", required=True)
    p.add_argument("--output", required=True)
    p.set_defaults(run=convergence)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
