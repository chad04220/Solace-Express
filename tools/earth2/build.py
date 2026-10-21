"""Build and verify an Earth pack (v2).

  python3 -m tools.earth2 build --out PACK --box SOUTH WEST NORTH EAST \\
      --etopo ETOPO_30s(.bin with .hdr, or GeoTIFF) --cover CGLS_LC100(.tif or URL) \\
      --airports airports.csv --runways runways.csv --places cities500.zip --grip GRIP4_RegionN_vector_shp.zip ... \\
      [--glo90-listing listing.txt] [--glo90-dir DIR] [--local-roads] [--only rasters,tables,roads,glo90] [--part NAME]
  python3 -m tools.earth2 split-grip --out DIR GRIP4_RegionN_vector_shp.zip ...
  python3 -m tools.earth2 index PACK
  python3 -m tools.earth2 package PACK DIST [--max-bytes N]
  python3 -m tools.earth2 verify PACK

The box is in whole degrees (the tiles it covers are built); --box -90 -180 90 180 is the globe. Each source is
recorded in manifest.json with its size and SHA-256 (a URL with the bytes actually read). pack.idx is the game's
plain-text index of the same.

A pack too big for one machine is built in parts (.github/workflows/earth-pack.yml): each part a box of whole
10-degree blocks, or the global tables alone (--only), into one folder; --part NAME records what a part wrote in
parts/NAME.json instead of the index, and `index` then writes the index of them all. `package` puts a pack in zips
of at most --max-bytes each (a release's files are under 2 GB), stored, not compressed again, each unpacking into an
earth folder; the first, the core, holds the index, the tables and stream.idx (where each bundle lies in the others,
for the game to stream them), and SHA256SUMS lists them all.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import time
import zipfile

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


STAGES = ("rasters", "tables", "roads", "glo90")


def build(args):
    box = tuple(args.box)
    out = args.out
    only = set(args.only.split(",")) if getattr(args, "only", None) else set(STAGES)
    if only - set(STAGES):
        raise SystemExit("--only: %s (the stages are %s)" % (",".join(sorted(only - set(STAGES))), ",".join(STAGES)))
    if getattr(args, "part", None) and only & {"rasters", "roads"}:   # (parts share no bundle: each whole blocks)
        if any(edge % 10 for edge in box):
            raise SystemExit("--part: the box's edges must be on the 10-degree blocks (%s)" % (box,))
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    files = {}
    sources = []
    log = (lambda *a: print(*a, flush=True)) if not args.quiet else (lambda *a: None)

    if "rasters" in only:
        if not args.etopo:
            raise SystemExit("the rasters need --etopo")
        _rasters(args, box, out, files, sources, log, t0)
    if "tables" in only:
        _tables(args, box, out, files, sources, log)
    if "roads" in only:
        _roads(args, box, out, files, sources, log, t0)
    if "glo90" in only:
        _glo90(args, box, out, files, sources)

    if getattr(args, "part", None):
        os.makedirs(os.path.join(out, "parts"), exist_ok=True)
        with open(os.path.join(out, "parts", args.part + ".json"), "w") as f:
            json.dump({"box": list(box), "files": {rel.replace(os.sep, "/"): size for rel, size in files.items()},
                       "sources": sources}, f, indent=1, sort_keys=True)
            f.write("\n")
        log("part %s: %d files, %.1f MB, %.0fs" % (args.part, len(files), sum(files.values()) / 1e6, time.time() - t0))
        return
    _attribution(out, files)
    write_index(out, box, files, sources)
    log("pack %s: %d files, %.1f MB, %.0fs" % (out, len(files), sum(files.values()) / 1e6, time.time() - t0))


def _rasters(args, box, out, files, sources, log, t0):
    """Elevation and cover, tile by tile."""
    from . import rasters   # (NumPy)
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


def _tables(args, box, out, files, sources, log):
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


BLOCK_MARGIN = 0.5   # (degrees: the lines a block's roads are noded from, past its edges)


def split_grip(zips, out_dir, log):
    """GRIP's regions filed by 10-degree block (roads.split_grip), with their source records (sources.json) for the
    parts that read them (build --grip-blocks)"""
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    written = roads.split_grip(zips, out_dir, BLOCK_MARGIN, log=log)
    with open(os.path.join(out_dir, "sources.json"), "w") as f:
        json.dump([source_record("GRIP4 " + os.path.basename(z), z, "CC-BY-4.0 (working assumption)") for z in zips], f, indent=1)
    return written


def _roads(args, box, out, files, sources, log, t0):
    if getattr(args, "grip_blocks", None):   # (the parts' way: each block from its own file, split_grip)
        if not getattr(args, "part", None):
            raise SystemExit("--grip-blocks: for a part (--part), its box on the 10-degree blocks")
        sources.extend(json.load(open(os.path.join(args.grip_blocks, "sources.json"))))
        for south in range(box[0], box[2], 10):
            for west in range(box[1], box[3], 10):
                _roads_of(args, (south, west, south + 10, west + 10), box, out, files, log, t0, BLOCK_MARGIN,
                          blocks=roads.read_block_lines(args.grip_blocks, south, west))
        return
    if not args.grip:
        return
    for z in args.grip:
        sources.append(source_record("GRIP4 " + os.path.basename(z), z, "CC-BY-4.0 (working assumption)"))
    if not getattr(args, "part", None):
        _roads_of(args, box, box, out, files, log, t0, 0.05)
        return
    # A part's roads block by block, to bound the memory (a 10-degree block of Europe is millions of lines). Each block
    # is noded from the lines within half a degree of it, so an edge crossing into the next block is the same edge,
    # with the same id, in both: the game reads it once.
    for south in range(box[0], box[2], 10):
        for west in range(box[1], box[3], 10):
            _roads_of(args, (south, west, south + 10, west + 10), box, out, files, log, t0, BLOCK_MARGIN)


def _roads_of(args, area, box, out, files, log, t0, margin, blocks=None):
    main, local = [], []
    lbox = getattr(args, "local_roads_box", None)
    if blocks is not None:   # (a block's own lines, already quantised: split_grip)
        for q in blocks:
            if q[0] != 5 or args.local_roads:
                (local if q[0] == 5 else main).append(q)
    for z in (args.grip or []) if blocks is None else []:
        for line in roads.read_grip(z, area, margin):
            if line[0] == 5:
                if not args.local_roads:
                    continue
                if lbox and not any(lbox[0] <= la < lbox[2] and lbox[1] <= lo < lbox[3] for la, lo in line[2]):
                    continue   # (the test fixture: the local roads of the islands alone)
            for q in roads.quantise_lines((line,)):   # (packed as read: a block of Europe is millions of lines)
                (local if q[0] == 5 else main).append(q)
    if not main and not local:
        return
    log("roads %s: %d lines (%d local) %.0fs" % (area, len(main) + len(local), len(local), time.time() - t0))
    files.update(roads.write_roads(out, roads.build_edges(main, quantised=True), area))
    del main
    if local:
        files.update(roads.write_roads(out, roads.build_edges(local, quantised=True), area, svb.LAYER_LOCAL_ROADS))
    log("roads %s done %.0fs" % (area, time.time() - t0))


def _glo90(args, box, out, files, sources):
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


def _attribution(out, files):
    os.makedirs(os.path.join(out, "licenses"), exist_ok=True)
    with open(os.path.join(out, "licenses", "ATTRIBUTION.txt"), "w") as f:
        f.write(ATTRIBUTION)
    files["licenses/ATTRIBUTION.txt"] = len(ATTRIBUTION)


def index(root):
    """The index of a pack built in parts (build --part): every part's files, which must all be there, the sources
    they read (each once), and the box round them all; the parts' records are then removed. Returns the problems."""
    pdir = os.path.join(root, "parts")
    names = sorted(n for n in os.listdir(pdir) if n.endswith(".json")) if os.path.isdir(pdir) else []
    if not names:
        return ["%s: no parts to index" % root]
    files, sources, seen, problems = {}, [], set(), []
    box = None
    for n in names:
        part = json.load(open(os.path.join(pdir, n)))
        b = part["box"]
        box = b if box is None else [min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3])]
        for rel, size in part["files"].items():
            p = os.path.join(root, rel)
            if rel in files:
                problems.append("%s: written by two parts" % rel)
            elif not os.path.exists(p) or os.path.getsize(p) != size:
                problems.append("%s (part %s): missing or changed" % (rel, n[:-5]))
            files[rel] = size
        for rec in part["sources"]:
            key = json.dumps(rec, sort_keys=True)
            if key not in seen:
                seen.add(key)
                sources.append(rec)
    if problems:
        return problems
    _attribution(root, files)
    write_index(root, tuple(box), files, sources)
    shutil.rmtree(pdir)
    return []


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


def package(root, dist, max_bytes=1900000000):
    """The zips of a pack (see the module's notes); returns their names, the core's first.

    The first zip is the core: the index, the manifest, the tables, the licences and stream.idx, which says where each
    of the other files lies in the zips after it - its zip, and the offset of its bytes there (stored, so a range of the
    zip is the file). The game installs the core alone and fetches a bundle's tiles from those ranges as it wants them;
    or every zip, for the whole pack. The zips are named for the pack (the start of pack.idx's SHA-256), so a pack built
    again never has its pieces read at another's offsets."""
    listed = []
    for line in open(os.path.join(root, "pack.idx")).read().split("\n"):
        if line.startswith("file "):
            listed.append(line.split(" ")[1])
    core = ["pack.idx", "manifest.json"] + [r for r in listed if "/" not in r or r.startswith("licenses/")]
    rest = sorted(r for r in listed if r not in core)
    bins, size = [], 0
    for rel in rest:
        n = os.path.getsize(os.path.join(root, rel))
        if not bins or (bins[-1] and size + n > max_bytes):
            bins.append([])
            size = 0
        bins[-1].append(rel)
        size += n
    pack_id = sha256(os.path.join(root, "pack.idx"))[:12]
    total = 1 + len(bins)
    names = ["earth-pack-%s-%02d-of-%02d.zip" % (pack_id, k + 1, total) for k in range(total)]
    os.makedirs(dist, exist_ok=True)
    stream = []
    for name, rels in zip(names[1:], bins):
        path = os.path.join(dist, name)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED, allowZip64=True) as z:
            for rel in rels:
                z.write(os.path.join(root, rel), "earth/" + rel)
        with zipfile.ZipFile(path) as z, open(path, "rb") as f:   # (each file's bytes: past its local header)
            for info in z.infolist():
                f.seek(info.header_offset)
                head = f.read(30)
                name_len, extra_len = struct.unpack("<HH", head[26:30])
                stream.append((info.filename[len("earth/"):], name, info.header_offset + 30 + name_len + extra_len, info.file_size))
    index = "somnium-stream 1\n"
    index += "".join("zip %s %d\n" % (n, os.path.getsize(os.path.join(dist, n))) for n in names[1:])
    index += "".join("file %s %s %d %d\n" % s for s in stream) + "end\n"
    with zipfile.ZipFile(os.path.join(dist, names[0]), "w", zipfile.ZIP_STORED, allowZip64=True) as z:
        for rel in core:
            z.write(os.path.join(root, rel), "earth/" + rel)
        z.writestr("earth/stream.idx", index)
    with open(os.path.join(dist, "SHA256SUMS"), "w") as f:
        for name in names:
            f.write("%s  %s\n" % (sha256(os.path.join(dist, name)), name))
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.earth2")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--box", type=int, nargs=4, required=True, metavar=("SOUTH", "WEST", "NORTH", "EAST"))
    b.add_argument("--etopo")
    b.add_argument("--cover")
    b.add_argument("--airports")
    b.add_argument("--runways")
    b.add_argument("--places")
    b.add_argument("--grip", action="append")
    b.add_argument("--grip-blocks", help="GRIP filed by block (split-grip): a part's roads from its blocks' files")
    b.add_argument("--local-roads", action="store_true")
    b.add_argument("--glo90-listing")
    b.add_argument("--glo90-dir")
    b.add_argument("--quiet", action="store_true")
    b.add_argument("--only", help="the stages to run, of " + ",".join(STAGES))
    b.add_argument("--part", help="record what this part wrote (parts/PART.json) for `index`, not the index")
    i = sub.add_parser("index")
    i.add_argument("pack")
    sg = sub.add_parser("split-grip")
    sg.add_argument("--out", required=True)
    sg.add_argument("zips", nargs="+")
    k = sub.add_parser("package")
    k.add_argument("pack")
    k.add_argument("dist")
    k.add_argument("--max-bytes", type=int, default=1900000000)
    v = sub.add_parser("verify")
    v.add_argument("pack")
    args = ap.parse_args(argv)
    if args.cmd == "build":
        build(args)
        return 0
    if args.cmd == "index":
        problems = index(args.pack)
        for p in problems:
            print("FAIL", p)
        if problems:
            return 1
    if args.cmd == "split-grip":
        written = split_grip(args.zips, args.out, lambda *a: print(*a, flush=True))
        print("%d block files, %.1f MB" % (len(written), sum(os.path.getsize(p) for p in written) / 1e6))
        return 0
    if args.cmd == "package":
        for name in package(args.pack, args.dist, args.max_bytes):
            print(name, os.path.getsize(os.path.join(args.dist, name)))
        return 0
    problems = verify(args.pack)
    for p in problems:
        print("FAIL", p)
    print("%s: %s" % (args.pack, "ok" if not problems else "%d problems" % len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
