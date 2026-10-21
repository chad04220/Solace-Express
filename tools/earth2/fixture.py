"""The test fixture (tests/fixtures/earth2): a real-data pack of three 1-degree tiles over Martha's Vineyard and
Nantucket (41-42 N, 72-69 W), small enough to commit, and expect.txt - what the Python reference reads from it (the
sampling, the tables, the roads), which tests/earth_data_test.cpp checks the game's reader against.

  python3 -m tools.earth2.fixture --out tests/fixtures/earth2 --etopo ... --cover ... --airports ... --runways ...
      --places ... --grip ... --glo90-dir DIR

GLO-90: N41W071 is cropped to 41.20-41.68 N (the islands and the upper Cape's ponds), N41W070 is whole, and N41W072 is listed in the bitmap but
left out, so the reader's fallback beside a tile it hasn't got is tested too.
"""
import argparse
import json
import os
import random
import struct
import zlib

from . import build as builder
from . import glo90, roads, svb, tables
from .sample import PackSampler

BOX = (41, -72, 42, -69)
# (the local roads are Martha's Vineyard's alone, to keep the fixture small)


def inflate_vectors(path):
    """zlib streams of known bytes for the C++ inflate: stored, fixed and dynamic blocks at several levels, empty,
    one byte, long runs, text and noise. Each: u32 raw size, u32 CRC-32, u32 stream size, the stream."""
    rng = random.Random(20261020)
    cases = [b"", b"a", b"abc" * 1000, bytes(70000), bytes(rng.randrange(256) for _ in range(5000)),
             (b"Somnium Volandi " * 4000)[:65000],
             bytes(rng.choice(b"ACGT") for _ in range(30000))]
    out = bytearray()
    for raw in cases:
        for level in (0, 1, 6, 9):
            z = zlib.compress(raw, level)
            out += struct.pack("<III", len(raw), zlib.crc32(raw) & 0xFFFFFFFF, len(z)) + z
        co = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
        z = co.compress(raw) + co.flush()
        out += struct.pack("<III", len(raw), zlib.crc32(raw) & 0xFFFFFFFF, len(z)) + z
    with open(path, "wb") as f:
        f.write(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    for name in ("out", "etopo", "cover", "airports", "runways", "places", "glo90-dir"):
        ap.add_argument("--" + name, required=True)
    ap.add_argument("--grip", action="append", required=True)
    ap.add_argument("--landmarks", help="the box's lighthouses (python3 -m tools.earth2.landmarks OUT --box 41 -72 42 -69)")
    a = ap.parse_args(argv)
    out = a.out
    args = argparse.Namespace(out=out, box=list(BOX), etopo=a.etopo, cover=a.cover, airports=a.airports,
                              runways=a.runways, places=a.places, landmarks=a.landmarks, grip=a.grip, local_roads=True, glo90_listing=None,
                              glo90_dir=None, quiet=False, local_roads_box=(41.2, -70.85, 41.5, -70.45))
    builder.build(args)
    # GLO-90: two tiles, one cropped, and the bitmap listing a third that isn't there
    gdir = os.path.join(out, "glo90")
    os.makedirs(gdir, exist_ok=True)
    glo90.crop(os.path.join(a.glo90_dir, glo90.tile_name(41, -71) + ".tif"), os.path.join(gdir, glo90.tile_name(41, -71) + ".tif"), 41.20, 41.68)
    with open(os.path.join(a.glo90_dir, glo90.tile_name(41, -70) + ".tif"), "rb") as f, \
         open(os.path.join(gdir, glo90.tile_name(41, -70) + ".tif"), "wb") as g:
        g.write(f.read())
    with open(os.path.join(out, "glo90.bits"), "wb") as f:
        f.write(glo90.bitmap([(41, -72), (41, -71), (41, -70)]))
    inflate_vectors(os.path.join(out, "inflate.bin"))
    manifest = json.load(open(os.path.join(out, "manifest.json")))
    files = {rel: info["bytes"] for rel, info in manifest["files"].items()}
    for rel in ("glo90/" + glo90.tile_name(41, -71) + ".tif", "glo90/" + glo90.tile_name(41, -70) + ".tif", "glo90.bits", "inflate.bin"):
        files[rel] = os.path.getsize(os.path.join(out, rel))
    manifest["sources"].append({"name": "Copernicus DEM GLO-90 N41W071 (cropped 41.20-41.68 N) and N41W070", "licence": "Copernicus DEM licence"})
    builder.write_index(out, BOX, files, manifest["sources"])
    write_expect(out)


def write_expect(root):
    s = PackSampler(root)
    rng = random.Random(7)
    lines = []
    points = [(41.393, -70.614), (41.40, -70.55), (41.2531, -70.0602), (41.45, -70.60), (41.548, -70.9), (41.30, -71.5),
              (41.70, -70.30), (41.05, -69.5), (41.2, -70.0), (41.6799, -70.70), (41.681, -70.70), (41.9999, -69.0001),
              (41.0001, -71.9999), (41.25, -70.0001), (41.25, -69.9999)]
    points += [(41 + rng.random(), -72 + 3 * rng.random()) for _ in range(200)]
    for lat, lon in points:
        lines.append("elev %.7f %.7f %.4f" % (lat, lon, s.elevation(lat, lon)))
        lines.append("coarse %.7f %.7f %.4f" % (lat, lon, s.coarse(lat, lon)))
        lines.append("cover %.7f %.7f %d" % (lat, lon, s.cover(lat, lon)))
    # GLO-90 samples as stored
    for lat, lon in ((41, -71), (41, -70)):
        t = s.fine_tile(lat, lon)
        for r, c in ((0, 0), (t["rows"] - 1, t["cols"] - 1), (t["rows"] // 2, t["cols"] // 3), (17, 999)):
            if r < t["rows"] and c < t["cols"]:
                lines.append("glo90 %d %d %d %d %d %d %.6f" % (lat, lon, t["rows"], t["cols"], r, c, t["h"][r][c]))
    # tables
    m, cells, recs, runs, strings = tables.read_table(os.path.join(root, "airports.bin"))
    lines.append("airports %d %d" % (len(recs), len(runs)))
    for r in recs:
        ident = tables.string_at(strings, r[6])
        first, n = r[5], r[4]
        rw = runs[first] if n else None
        lines.append("airport %s %.7f %.7f %d %d %d %d %d" % (ident, r[0] * 1e-7, r[1] * 1e-7, r[2], r[3], n,
                                                         rw[4] if rw else 0, rw[10] if rw else 0xFFFF))
    m, cells, recs, runs, strings = tables.read_table(os.path.join(root, "places.bin"))
    lines.append("places %d" % len(recs))
    for r in recs[:40]:
        lines.append("place %s %d %.7f %.7f" % (tables.string_at(strings, r[3]).replace(" ", "_"), r[2], r[0] * 1e-7, r[1] * 1e-7))
    if os.path.exists(os.path.join(root, "landmarks.bin")):
        m, cells, recs, runs, strings = tables.read_table(os.path.join(root, "landmarks.bin"))
        lines.append("landmarks %d" % len(recs))
        for r in recs:
            lines.append("landmark %s %.7f %.7f %d %d" % ((tables.string_at(strings, r[4]) or "-").replace(" ", "_"), r[0] * 1e-7, r[1] * 1e-7, r[2], r[3]))
    for layer, name in ((svb.LAYER_ROADS, "roads"), (svb.LAYER_LOCAL_ROADS, "lroads")):
        ids = {}
        for lat in range(BOX[0], BOX[2]):
            for lon in range(BOX[1], BOX[3]):
                path = svb.bundle_path(root, layer, lat, lon)
                if not os.path.exists(path):
                    continue
                raw = svb.Bundle(path).raw(lat, lon)
                if raw:
                    for eid, cls, surf, pts in roads.decode_tile(raw):
                        ids[eid] = (cls, surf, len(pts), pts[0], pts[-1])
        lines.append("%s %d %d" % (name, len(ids), sum(v[2] for v in ids.values())))
        for eid in sorted(ids)[:25]:
            cls, surf, n, p0, p1 = ids[eid]
            lines.append("edge %s %d %d %d %d %d %d %d %d" % (name, eid, cls, surf, n, p0[0], p0[1], p1[0], p1[1]))
    with open(os.path.join(root, "expect.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
