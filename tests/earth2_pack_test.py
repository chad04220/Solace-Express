"""The Earth pack v2 tools (tools/earth2): the fixture verifies; bundles, road tiles and the point tables round-trip;
and, where NumPy is installed, the fixture's expect.txt is what the reference sampler reads from it today (so the
C++ reader's parity test, tests/earth_data_test.cpp, checks against the tools as they are)."""
import array
import json
import os
import random
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from tools.earth2 import build, roads, svb, tables  # noqa: E402

FIXTURE = os.path.join(ROOT, "tests", "fixtures", "earth2")
failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


def test_fixture():
    problems = build.verify(FIXTURE)
    check(not problems, "fixture verifies: %s" % problems)
    # a changed byte is caught
    tmp = tempfile.mkdtemp()
    try:
        dst = os.path.join(tmp, "pack")
        shutil.copytree(FIXTURE, dst)
        p = os.path.join(dst, "elev", "N40W080.svb")
        data = bytearray(open(p, "rb").read())
        data[-20] ^= 0x40
        open(p, "wb").write(bytes(data))
        check(build.verify(dst), "a changed bundle fails verification")
    finally:
        shutil.rmtree(tmp)


def test_bundle_roundtrip():
    rng = random.Random(5)
    tmp = tempfile.mkdtemp()
    try:
        w = 122
        tiles = {}
        values = {}
        for lat in (40, 41):
            for lon in (-72, -71):
                v = array.array("h", [rng.randrange(-200, 3000) for _ in range(w * w)])
                values[(lat, lon)] = v
                tiles[(lat, lon)] = svb.encode_raster(v, svb.TYPE_INT16, w)
        tiles[(42, -72)] = svb.encode_raster(array.array("h", [-200] * (w * w)), svb.TYPE_INT16, w)
        path = os.path.join(tmp, "elev", "N40W080.svb")
        svb.write_bundle(path, svb.LAYER_ELEV, svb.TYPE_INT16, 40, -80, 120, 1, tiles)
        b = svb.Bundle(path)
        for key, v in values.items():
            check(list(b.samples(*key)) == list(v), "elevation tile %s round-trips" % (key,))
        check(b.kind(42, -72) == svb.KIND_CONSTANT and set(b.samples(42, -72)) == {-200}, "a constant tile")
        check(b.kind(43, -72) == svb.KIND_ABSENT and b.samples(43, -72) is None, "an absent tile")
        check(sorted(b.tiles()) == sorted(list(values) + [(42, -72)]), "the bundle lists its tiles")
    finally:
        shutil.rmtree(tmp)


def test_roads_roundtrip():
    rng = random.Random(9)
    edges = []
    for k in range(50):
        lat, lon = 413000000 + rng.randrange(1000000), -706000000 + rng.randrange(1000000)
        pts = [(lat, lon)]
        for _ in range(rng.randrange(1, 30)):
            lat += rng.randrange(-20000, 20000)
            lon += rng.randrange(-20000, 20000)
            pts.append((lat, lon))
        edges.append((rng.randrange(6), rng.randrange(3), pts))
    raw = roads.encode_tile(edges)
    back = roads.decode_tile(raw)
    check(len(back) == len(edges), "road tile edge count")
    for (cls, surf, pts), (eid, c2, s2, p2) in zip(edges, back):
        check(c2 == cls and s2 == surf and p2 == pts and eid == roads.edge_id(cls, pts), "road edge round-trips")
    # junctions: two lines meeting end to end chain, a T junction splits
    a = (1, 1, [(41.0, -70.0), (41.001, -70.0)])
    b = (1, 1, [(41.001, -70.0), (41.002, -70.0)])
    c = (2, 1, [(41.0005, -70.001), (41.0005, -70.0)])
    t = (2, 1, [(41.0, -70.0), (41.0005, -70.0), (41.001, -70.0)])
    check(len(roads.build_edges([a, b])) == 1, "two pieces meeting end to end become one edge")
    check(len(roads.build_edges([t, c])) == 3, "a line split where another ends on it")


def test_tables_roundtrip():
    tmp = tempfile.mkdtemp()
    try:
        places = [{"id": i, "name": "Place %d" % i, "lat": 41 + i * 0.01, "lon": -70.5 + i * 0.02, "pop": 1000 * i,
                   "kind": i % 4, "cc": "US"} for i in range(20)]
        p = os.path.join(tmp, "places.bin")
        tables.write_places(p, places)
        magic, cells, recs, runs, strings = tables.read_table(p)
        check(magic == b"SVP2" and len(recs) == 20, "places round-trip")
        names = sorted(tables.string_at(strings, r[3]) for r in recs)
        check(names == sorted(x["name"] for x in places), "place names round-trip")
        # the sort: by cell, then the most populous first
        check(all(recs[i][2] >= recs[i + 1][2] for i in range(len(recs) - 1) if tables.cell_of(recs[i][0] * 1e-7, recs[i][1] * 1e-7) ==
                  tables.cell_of(recs[i + 1][0] * 1e-7, recs[i + 1][1] * 1e-7)), "places sorted by population within a cell")
    finally:
        shutil.rmtree(tmp)
    check(tables.surface_code("ASPH-G") == 1 and tables.surface_code("Turf") == 3 and tables.surface_code("GRVL") == 4 and
          tables.surface_code("WATER") == 7 and tables.surface_code("") == 0, "runway surfaces")


def test_parts_index():
    """A pack built in parts (build --part, then index): the index lists every part's files, the sources once, the box
    round them all, and it verifies; a part's file missing, or written by two parts, is refused."""
    tmp = tempfile.mkdtemp()
    try:
        w = 122
        flat = svb.encode_raster(array.array("h", [12] * (w * w)), svb.TYPE_INT16, w)

        def part(root, name, south, west, sources):
            path = svb.bundle_path(root, svb.LAYER_ELEV, south, west)
            size = svb.write_bundle(path, svb.LAYER_ELEV, svb.TYPE_INT16, south, west, 120, 1, {(south + 1, west + 2): flat})
            os.makedirs(os.path.join(root, "parts"), exist_ok=True)
            with open(os.path.join(root, "parts", name + ".json"), "w") as f:
                json.dump({"box": [south, west, south + 10, west + 30], "sources": sources,
                           "files": {os.path.relpath(path, root).replace(os.sep, "/"): size}}, f)
        etopo = {"name": "ETOPO 2022 30s surface", "licence": "CC0-1.0", "file": "e.tif", "bytes": 1, "sha256": "0"}
        root = os.path.join(tmp, "pack")
        part(root, "a", 40, -80, [etopo])
        part(root, "b", -10, 100, [etopo, {"name": "GRIP4 GRIP4_Region6_vector_shp.zip", "licence": "CC-BY-4.0"}])
        check(build.index(root) == [], "two parts indexed")
        check(not build.verify(root), "the pack of two parts verifies: %s" % build.verify(root))
        manifest = json.load(open(os.path.join(root, "manifest.json")))
        check(manifest["box"] == [-10, -80, 50, 130], "the box round both parts")
        check(len(manifest["sources"]) == 2, "each source once")
        check(not os.path.exists(os.path.join(root, "parts")), "the parts' records removed")
        check("licenses/ATTRIBUTION.txt" in manifest["files"], "the attribution written")
        bad = os.path.join(tmp, "bad")
        part(bad, "a", 40, -80, [etopo])
        part(bad, "b", 40, -80, [etopo])
        dist = os.path.join(tmp, "dist")
        names = build.package(root, dist, max_bytes=1)
        check(len(names) == 3, "packaged in zips under the size given, a file each (%d)" % len(names))
        import zipfile
        unpacked = os.path.join(tmp, "unpacked")
        for n in names:
            zipfile.ZipFile(os.path.join(dist, n)).extractall(unpacked)
        first = zipfile.ZipFile(os.path.join(dist, names[0])).namelist()
        check("earth/pack.idx" in first and "earth/manifest.json" in first, "the index in the first zip")
        check(not build.verify(os.path.join(unpacked, "earth")), "the zips unpack into a pack that verifies")
        sums = open(os.path.join(dist, "SHA256SUMS")).read().split("\n")
        check(all(any(line.endswith("  " + n) for line in sums) for n in names), "SHA256SUMS lists every zip")
        check(any("two parts" in p for p in build.index(bad)), "a file written by two parts is refused")
        missing = os.path.join(tmp, "missing")
        part(missing, "a", 40, -80, [etopo])
        os.remove(svb.bundle_path(missing, svb.LAYER_ELEV, 40, -80))
        check(any("missing" in p for p in build.index(missing)), "a part's file missing is refused")
        check(not os.path.exists(os.path.join(missing, "pack.idx")), "and no index written")
    finally:
        shutil.rmtree(tmp)


def test_expectations():
    try:
        import numpy  # noqa: F401
    except ImportError:
        print("SKIP expectations (no NumPy)")
        return
    from tools.earth2 import fixture
    tmp = tempfile.mkdtemp()
    try:
        dst = os.path.join(tmp, "pack")
        shutil.copytree(FIXTURE, dst)
        fixture.write_expect(dst)
        check(open(os.path.join(dst, "expect.txt")).read() == open(os.path.join(FIXTURE, "expect.txt")).read(),
              "expect.txt is what the reference reads from the fixture")
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    test_fixture()
    test_bundle_roundtrip()
    test_roads_roundtrip()
    test_tables_roundtrip()
    test_parts_index()
    test_expectations()
    print("earth2_pack_test: %s" % ("ok" if not failures else "%d failures" % failures))
    sys.exit(1 if failures else 0)
