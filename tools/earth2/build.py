"""Build and verify an Earth pack (v2).

  python3 -m tools.earth2 build --out PACK --box SOUTH WEST NORTH EAST \\
      --etopo ETOPO_30s(.bin with .hdr, or GeoTIFF) --cover CGLS_LC100(.tif or URL) \\
      --airports airports.csv --runways runways.csv --places cities500.zip --grip GRIP4_RegionN_vector_shp.zip ... \\
      [--glo90-listing listing.txt] [--glo90-dir DIR] [--local-roads]
  python3 -m tools.earth2 verify PACK

The box is in whole degrees (the tiles it covers are built); --box -90 -180 90 180 is the globe. Each source is
recorded in manifest.json with its size and SHA-256 (a URL with the bytes actually read). pack.idx is the game's
plain-text index of the same.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import time

from . import TOOL_VERSION, glo90, roads, svb, tables

ATTRIBUTION = """Somnium Volandi Earth pack: sources and licences

Elevation (base): NOAA National Centers for Environmental Information (2022), ETOPO 2022 15 Arc-Second Global Relief
Model, 30 arc-second surface, https://doi.org/10.25921/fd45-gt74. CC0 1.0. Not for navigation. Changes: tiled to
1-degree tiles, rounded to metres, the sea floor clamped at -200 m.

Elevation (fine, fetched or included per tile): Copernicus DEM GLO-90. (c) DLR e.V. 2010-2014 and (c) Airbus Defence
and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved. Available on a
free basis for the general public under the Copernicus DEM licence. Source: s3://copernicus-dem-90m (AWS open data).

Land cover: Copernicus Global Land Service, Land Cover 100 m, collection 3, epoch 2019 (Buchhorn et al. 2020),
https://doi.org/10.5281/zenodo.3939050, CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Changes: reduced
3 x 3 by majority to 1/336 degree, tiled; outside 80 N - 60 S filled from the elevation.

Airports: OurAirports (https://ourairports.com/data/), public domain. Changes: open small, medium and large airports
with a runway of 300 m or more; feet to metres.

Places: GeoNames (https://www.geonames.org/), cities500, CC BY 4.0. Changes: populated places, binary table.

Roads: GRIP4 (Version 4, 2018), J. R. Meijer, M. A. J. Huijbregts, C. G. J. Schotten and A. M. Schipper, Global
patterns of current and future road infrastructure, Environmental Research Letters 13, 064006 (2018),
https://doi.org/10.1088/1748-9326/aabd42, dataset https://doi.org/10.5281/zenodo.6420961, used under CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). Supplied without warranties and not suitable for navigation; no
endorsement implied. Changes: noded at junctions, chained, simplified (6 m), cut into pieces of at most 5 km,
quantised to 1e-7 degree, filed by 1-degree tile.
"""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def source_record(name, path, licence):
    rec = {"name": name, "licence": licence}
    if path.startswith("http"):
        rec["url"] = path
    else:
        rec["file"] = os.path.basename(path)
        rec["bytes"] = os.path.getsize(path)
        rec["sha256"] = sha256(path)
    return rec


def tiles_in(box):
    s, w, n, e = box
    return [(lat, lon) for lat in range(s, n) for lon in range(w, e)]


def build(args):
    from . import rasters   # (NumPy)
    box = tuple(args.box)
    out = args.out
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    files = {}
    sources = []
    log = (lambda *a: print(*a, flush=True)) if not args.quiet else (lambda *a: None)

    # elevation and cover, tile by tile
    etopo = rasters.open_grid(args.etopo)
    sources.append(source_record("ETOPO 2022 30s surface", args.etopo, "CC0-1.0"))
    cover_grid = rasters.open_grid(args.cover) if args.cover else None
    if args.cover:
        sources.append(source_record("CGLS-LC100 2019 discrete classification", args.cover, "CC-BY-4.0"))
    elev_tiles, cover_tiles = {}, {}
    todo = tiles_in(box)
    for k, (lat, lon) in enumerate(todo):
        e = rasters.elevation_tile(etopo, lat, lon)
        elev_tiles[(lat, lon)] = e
        if cover_grid is not None:
            cover_tiles[(lat, lon)] = rasters.cover_tile(cover_grid, lat, lon, e)
        if k % 50 == 0:
            log("tile %d/%d (%d, %d) %.0fs" % (k + 1, len(todo), lat, lon, time.time() - t0))
    files.update(rasters.write_layer(out, svb.LAYER_ELEV, elev_tiles, rasters.ELEV_SPD))
    if cover_tiles:
        files.update(rasters.write_layer(out, svb.LAYER_COVER, cover_tiles, rasters.COVER_SPD))
    log("rasters done %.0fs" % (time.time() - t0))

    # tables
    if args.airports:
        rows = tables.read_airports(args.airports, args.runways, None if box == (-90, -180, 90, 180) else box)
        size, na, nr = tables.write_airports(os.path.join(out, "airports.bin"), rows)
        files["airports.bin"] = size
        sources.append(source_record("OurAirports airports.csv", args.airports, "public domain"))
        sources.append(source_record("OurAirports runways.csv", args.runways, "public domain"))
        log("airports %d, runways %d" % (na, nr))
    if args.places:
        places = tables.read_places(args.places, None if box == (-90, -180, 90, 180) else box)
        size, n = tables.write_places(os.path.join(out, "places.bin"), places)
        files["places.bin"] = size
        sources.append(source_record("GeoNames cities500", args.places, "CC-BY-4.0"))
        log("places %d" % n)

    # roads
    if args.grip:
        lines = []
        for z in args.grip:
            lines.extend(roads.read_grip(z, box))
            sources.append(source_record("GRIP4 " + os.path.basename(z), z, "CC-BY-4.0 (working assumption)"))
        main = [l for l in lines if l[0] != 5]
        local = [l for l in lines if l[0] == 5]
        lbox = getattr(args, "local_roads_box", None)
        if lbox:   # (the test fixture: the local roads of the islands alone)
            local = [l for l in local if any(lbox[0] <= la < lbox[2] and lbox[1] <= lo < lbox[3] for la, lo in l[2])]
        log("roads: %d lines (%d local) %.0fs" % (len(lines), len(local), time.time() - t0))
        files.update(roads.write_roads(out, roads.build_edges(main), box))
        if args.local_roads and local:
            files.update(roads.write_roads(out, roads.build_edges(local), box, svb.LAYER_LOCAL_ROADS))
        log("roads done %.0fs" % (time.time() - t0))

    # GLO-90
    if args.glo90_listing:
        listing = glo90.read_listing(args.glo90_listing)
        with open(os.path.join(out, "glo90.bits"), "wb") as f:
            f.write(glo90.bitmap(listing))
        files["glo90.bits"] = 360 * 180 // 8
        sources.append(source_record("Copernicus DEM GLO-90 listing", args.glo90_listing, "Copernicus DEM licence"))
    if args.glo90_dir:
        dst = os.path.join(out, "glo90")
        os.makedirs(dst, exist_ok=True)
        for lat, lon in tiles_in(box):
            name = glo90.tile_name(lat, lon) + ".tif"
            src = os.path.join(args.glo90_dir, name)
            if os.path.exists(src):
                shutil.copyfile(src, os.path.join(dst, name))
                files["glo90/" + name] = os.path.getsize(src)

    os.makedirs(os.path.join(out, "licenses"), exist_ok=True)
    with open(os.path.join(out, "licenses", "ATTRIBUTION.txt"), "w") as f:
        f.write(ATTRIBUTION)
    files["licenses/ATTRIBUTION.txt"] = len(ATTRIBUTION)
    write_index(out, box, files, sources)
    log("pack %s: %d files, %.1f MB, %.0fs" % (out, len(files), sum(files.values()) / 1e6, time.time() - t0))


def write_index(out, box, files, sources):
    manifest = {
        "format": "somnium-earth", "version": 2, "tool": TOOL_VERSION,
        "box": list(box),
        "layers": {"elev": {"samplesPerDegree": 120, "apron": 1, "type": "int16", "units": "m", "seaFloorClamp": -200},
                   "cover": {"samplesPerDegree": 336, "apron": 1, "type": "uint8", "classes": "CGLS-LC100"},
                   "roads": {"type": "edges"}, "lroads": {"type": "edges", "optional": True}},
        "files": {}, "sources": sources,
    }
    for rel in sorted(files):
        p = os.path.join(out, rel)
        manifest["files"][rel] = {"bytes": os.path.getsize(p), "sha256": sha256(p)}
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write("\n")
    with open(os.path.join(out, "pack.idx"), "w") as f:
        f.write("somnium-earth 2\n")
        f.write("box %d %d %d %d\n" % box)
        for rel in sorted(files):
            info = manifest["files"][rel]
            f.write("file %s %d %s\n" % (rel.replace(os.sep, "/"), info["bytes"], info["sha256"]))
        f.write("end\n")


def verify(root):
    """Every file the index lists: its size and hash, and every bundle and table read through with all its checks."""
    problems = []
    lines = open(os.path.join(root, "pack.idx")).read().split("\n")
    if lines[0] != "somnium-earth 2" or "end" not in lines:
        return ["pack.idx: not a version 2 index"]
    count = 0
    for line in lines:
        if not line.startswith("file "):
            continue
        _, rel, size, digest = line.split(" ")
        p = os.path.join(root, rel)
        if not os.path.exists(p) or os.path.getsize(p) != int(size) or sha256(p) != digest:
            problems.append("%s: missing or changed" % rel)
            continue
        count += 1
        try:
            if rel.endswith(".svb"):
                b = svb.Bundle(p)
                for lat, lon in b.tiles():
                    if b.sample_type == svb.TYPE_BLOB:
                        raw = b.raw(lat, lon)
                        if raw is not None:
                            roads.decode_tile(raw)
                    else:
                        b.samples(lat, lon)
            elif rel in ("airports.bin", "places.bin"):
                tables.read_table(p)
        except ValueError as e:
            problems.append(str(e))
    if count == 0:
        problems.append("pack.idx lists no files")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.earth2")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--box", type=int, nargs=4, required=True, metavar=("SOUTH", "WEST", "NORTH", "EAST"))
    b.add_argument("--etopo", required=True)
    b.add_argument("--cover")
    b.add_argument("--airports")
    b.add_argument("--runways")
    b.add_argument("--places")
    b.add_argument("--grip", action="append")
    b.add_argument("--local-roads", action="store_true")
    b.add_argument("--glo90-listing")
    b.add_argument("--glo90-dir")
    b.add_argument("--quiet", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("pack")
    args = ap.parse_args(argv)
    if args.cmd == "build":
        build(args)
        return 0
    problems = verify(args.pack)
    for p in problems:
        print("FAIL", p)
    print("%s: %s" % (args.pack, "ok" if not problems else "%d problems" % len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
