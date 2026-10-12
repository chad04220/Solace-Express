#!/usr/bin/env python3
"""Offline Phase 0 contracts, with independent geometric and analytic oracles.

The tiny committed fixture is entirely synthetic. No test downloads source data
or changes runtime game assets. Run directly, or through CTest's earth_pack test.
"""
from __future__ import annotations

import copy
import csv
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from earth import airports, common, cube, elevation, pack, prepare, roads  # noqa: E402

FIXTURE = ROOT / "tests" / "fixtures" / "earth"
SAMPLES = 256
WIDTH = 258
COUNT = WIDTH * WIDTH
REJECT = (ValueError, RuntimeError, OSError)
FACES = ("px", "nx", "py", "ny", "pz", "nz")

# Explicit geometry oracle, not imported implementation tables. Each entry gives
# the neighbor face, its touching edge, and whether the edge index is reversed.
EDGES = {
    "px": {"left": ("ny", "right", False), "right": ("py", "left", False),
           "bottom": ("nz", "top", False), "top": ("pz", "bottom", False)},
    "nx": {"left": ("py", "right", False), "right": ("ny", "left", False),
           "bottom": ("nz", "bottom", True), "top": ("pz", "top", True)},
    "py": {"left": ("px", "right", False), "right": ("nx", "left", False),
           "bottom": ("nz", "right", True), "top": ("pz", "right", False)},
    "ny": {"left": ("nx", "right", False), "right": ("px", "left", False),
           "bottom": ("nz", "left", False), "top": ("pz", "left", True)},
    "pz": {"left": ("ny", "top", True), "right": ("py", "top", False),
           "bottom": ("px", "top", False), "top": ("nx", "top", True)},
    "nz": {"left": ("ny", "bottom", False), "right": ("py", "bottom", True),
           "bottom": ("nx", "bottom", True), "top": ("px", "bottom", False)},
}


def direction(face, u, v):
    """Literal face equations from the documented cube convention."""
    a, b = math.tan(u * math.pi / 4), math.tan(v * math.pi / 4)
    value = {"px": (1, a, b), "nx": (-1, -a, b),
             "py": (-a, 1, b), "ny": (a, -1, b),
             "pz": (-b, a, 1), "nz": (b, a, -1)}[face]
    norm = math.sqrt(sum(c * c for c in value))
    return tuple(c / norm for c in value)


def latlon(value):
    return (math.degrees(math.atan2(value[2], math.hypot(value[0], value[1]))),
            math.degrees(math.atan2(value[1], value[0])))


def address(face, level, i, j):
    """Convert an in-face global core address to a tile and local sample."""
    return cube.Tile(face, level, i // SAMPLES, j // SAMPLES), i % SAMPLES, j % SAMPLES


def edge_address(edge, k, size, ghost=False):
    lo, hi = (-1, size) if ghost else (0, size - 1)
    return {"left": (lo, k), "right": (hi, k),
            "bottom": (k, lo), "top": (k, hi)}[edge]


def ghost_address(face, level, i, j):
    last = (1 << level) - 1
    tx = min(last, max(0, i // SAMPLES))
    ty = min(last, max(0, j // SAMPLES))
    return cube.Tile(face, level, tx, ty), i - tx * SAMPLES, j - ty * SAMPLES


def file_snapshot(root):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


def write_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")


def feature(ident, coords, node_ids=None, cls="primary"):
    props = {"id": ident, "class": cls}
    if node_ids is not None:
        props["node_ids"] = node_ids
    return {"type": "Feature", "properties": props,
            "geometry": {"type": "LineString", "coordinates": coords}}


class SnapshotTests(unittest.TestCase):
    def test_windows_snapshot_keys_match_manifest_paths(self):
        # Exercise Windows path spelling even when this test runs on Linux.
        relative = PureWindowsPath("elevation/ny/6/49_56.elv")
        payload = mock.Mock()
        payload.relative_to.return_value = relative
        payload.is_file.return_value = True
        payload.read_bytes.return_value = b"payload"
        root = mock.Mock()
        root.rglob.return_value = [payload]
        self.assertEqual(file_snapshot(root), {"elevation/ny/6/49_56.elv": b"payload"})


class JsonTests(unittest.TestCase):
    def test_strict_json_rejects_duplicate_keys_and_exponent_overflow(self):
        for raw in [b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":Infinity}',
                    b'{"x":1e999}', b'{"x":-1e999}', b'{"x":[{"y":1e999}]}', b'\xff']:
            with self.subTest(raw=raw), self.assertRaises(REJECT):
                common.strict_json(raw)
        self.assertEqual(common.strict_json(b'{"x":1e30}'), {"x": 1e30})


class CubeTests(unittest.TestCase):
    def test_documented_axes_and_tangent_mapping(self):
        for face in FACES:
            for u, v in [(0, 0), (-0.75, 0.25), (0.6, -0.4), (-1, 1), (1, -1)]:
                with self.subTest(face=face, u=u, v=v):
                    actual = cube.face_uv_to_direction(face, u, v)
                    for got, expected in zip(actual, direction(face, u, v)):
                        self.assertAlmostEqual(got, expected, places=14)
                    self.assertAlmostEqual(sum(c*c for c in actual), 1, places=14)
            # Interior inverse tests do not depend on ambiguous cube-edge ties.
            for u, v in [(-0.75, 0.25), (0.6, -0.4)]:
                got_face, got_u, got_v = cube.direction_to_face_uv(direction(face, u, v))
                self.assertEqual(got_face, face)
                self.assertAlmostEqual(got_u, u, places=14)
                self.assertAlmostEqual(got_v, v, places=14)

    def test_cell_centres_and_latlon_tile_membership(self):
        for face in FACES:
            for level in (0, 1, 6):
                n = 1 << level
                tile = cube.Tile(face, level, n // 2, n // 2)
                for col, row in [(0, 0), (33, 196), (255, 255)]:
                    u = 2 * ((tile.x * 256 + col + 0.5) / (256 * n)) - 1
                    v = 2 * ((tile.y * 256 + row + 0.5) / (256 * n)) - 1
                    expected = latlon(direction(face, u, v))
                    actual = cube.sample_latlon(tile, col, row)
                    self.assertAlmostEqual(actual[0], expected[0], places=11)
                    self.assertAlmostEqual(actual[1], expected[1], places=11)
                    self.assertEqual(cube.tile_for_latlon(*expected, level), tile)
        # A known regional location is an external check on the inverse too.
        self.assertEqual(cube.tile_for_latlon(32.3, -64.8, 6), cube.Tile("ny", 6, 49, 56))

    def test_every_face_edge_has_exact_adjacent_core_apron(self):
        checks = 0
        for level in (0, 1, 3):
            size = SAMPLES << level
            for face in FACES:
                for edge, (neighbor, neighbor_edge, reverse) in EDGES[face].items():
                    for k in range(size):
                        i, j = edge_address(edge, k, size, ghost=True)
                        args = ghost_address(face, level, i, j)
                        ni, nj = edge_address(neighbor_edge, size - 1 - k if reverse else k, size)
                        self.assertEqual(cube.sample_location(*args), (neighbor, ni, nj),
                                         (face, level, edge, k))
                        self.assertEqual(cube.sample_latlon(*args),
                                         cube.sample_latlon(*address(neighbor, level, ni, nj)))
                        checks += 1
        self.assertEqual(checks, 67584)

    def test_internal_tile_aprons_match_both_neighbors(self):
        for face in FACES:
            for level in (1, 2):
                side = 1 << level
                for x in range(side - 1):
                    for y in range(side):
                        a, b = cube.Tile(face, level, x, y), cube.Tile(face, level, x + 1, y)
                        for k in range(256):
                            self.assertEqual(cube.sample_latlon(a, 256, k), cube.sample_latlon(b, 0, k))
                            self.assertEqual(cube.sample_latlon(a, 255, k), cube.sample_latlon(b, -1, k))
                for y in range(side - 1):
                    for x in range(side):
                        a, b = cube.Tile(face, level, x, y), cube.Tile(face, level, x, y + 1)
                        for k in range(256):
                            self.assertEqual(cube.sample_latlon(a, k, 256), cube.sample_latlon(b, k, 0))
                            self.assertEqual(cube.sample_latlon(a, k, 255), cube.sample_latlon(b, k, -1))

    def test_all_eight_cube_corners_share_one_exact_owner(self):
        for level in (0, 2, 6):
            size = SAMPLES << level
            seen = {}
            for face in FACES:
                for su in (-1, 1):
                    for sv in (-1, 1):
                        xyz = direction(face, su, sv)
                        sx, sy, sz = tuple(1 if c > 0 else -1 for c in xyz)
                        owner = "px" if sx > 0 else "nx"
                        expected = (owner, size - 1 if sy == sx else 0, size - 1 if sz > 0 else 0)
                        args = ghost_address(face, level, -1 if su < 0 else size, -1 if sv < 0 else size)
                        self.assertEqual(cube.sample_location(*args), expected)
                        point = cube.sample_latlon(*args)
                        self.assertEqual(point, cube.sample_latlon(*address(expected[0], level, expected[1], expected[2])))
                        key = (sx, sy, sz)
                        if key in seen:
                            self.assertEqual(point, seen[key])
                        seen[key] = point
            self.assertEqual(len(seen), 8)

    def test_invalid_coordinates_fail_closed(self):
        for args in [("bad", 0, 0, 0), ("px", -1, 0, 0), ("px", 1, 2, 0),
                     ("px", 1, 0, -1), ("px", 1.5, 0, 0), ("px", 9999, 0, 0)]:
            with self.subTest(args=args), self.assertRaises(REJECT):
                cube.Tile(*args)
        for lat, lon in [(91, 0), (-91, 0), (math.nan, 0), (0, math.inf)]:
            with self.subTest(lat=lat, lon=lon), self.assertRaises(REJECT):
                cube.tile_for_latlon(lat, lon, 1)
        for args in [("px", math.nan, 0), ("px", 0, math.inf)]:
            with self.assertRaises(REJECT):
                cube.face_uv_to_direction(*args)
        for coords in [(0, 0, 0), (math.nan, 1, 1), (1, math.inf, 1)]:
            with self.assertRaises(REJECT):
                cube.direction_to_face_uv(coords)


class ElevationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="earth-elevation-test-")
        self.root = Path(self.tmp.name)
        self.grid = elevation.Grid.from_json(FIXTURE / "elevation.json")

    def tearDown(self):
        self.tmp.cleanup()

    def test_affine_grid_interpolation_is_analytic(self):
        for lat, lon in [(25, -75), (40, -55), (32.3, -64.8), (31.777, -68.012), (35, -65)]:
            expected = -4500 + 23 * (lat - 32) + 17 * (lon + 65)
            self.assertAlmostEqual(self.grid.sample(lat, lon), expected, places=10)
        for lat, lon in [(24.9, -65), (40.1, -65), (32, -75.1), (32, -54.9), (math.nan, -65)]:
            with self.subTest(lat=lat, lon=lon), self.assertRaises(REJECT):
                self.grid.sample(lat, lon)

    def test_area_downsampling_uses_spherical_jacobian(self):
        col, row = 39, 181
        cut_lon = 45 * (2 * (col + .5) / 256 - 1)
        class StepSource:
            def sample(self, lat, lon):
                return -11000 if lon < cut_lon else 9000
        values = elevation.decode_tile(elevation.build_elevation(
            cube.Tile("px", 0, 0, 0), StepSource(), area_samples=2, codec="zlib"))["heights"]
        weighted, total = 0., 0.
        for sy in (.25, .75):
            for sx in (.25, .75):
                u, v = 2*(col+sx)/256-1, 2*(row+sy)/256-1
                a, b = math.tan(u*math.pi/4), math.tan(v*math.pi/4)
                jacobian = (1+a*a)*(1+b*b) / ((1+a*a+b*b)**1.5)
                expected_height = -11000 if sx < .5 else 9000
                weighted += expected_height*jacobian; total += jacobian
        expected = weighted/total
        self.assertGreater(abs(expected - (-1000)), 1, "oracle must distinguish unweighted averaging")
        self.assertLessEqual(abs(values[(row+1)*WIDTH+col+1] - expected), .5000001)
        for bad in (0, 17, True, 1.5):
            with self.assertRaises(REJECT):
                elevation.build_elevation(cube.Tile("px", 0, 0, 0), StepSource(), area_samples=bad, codec="zlib")

    def test_periodic_grid_wraps_antimeridian(self):
        data = {"version": 1, "lon_min": -180, "lat_min": -90, "lon_step": 90,
                "lat_step": 90, "periodic": True,
                "heights": [[-110, -90, -70, -90, -110], [-20, 0, 20, 0, -20], [70, 90, 110, 90, 70]]}
        path = self.root / "periodic.json"
        write_json(path, data)
        grid = elevation.Grid.from_json(path)
        self.assertEqual(grid.sample(0, -180), -20)
        self.assertEqual(grid.sample(0, 180), -20)
        self.assertAlmostEqual(grid.sample(0, 179.9), grid.sample(0, -179.9), places=12)

    def test_grid_schema_and_nonfinite_data_rejected(self):
        original = json.loads((FIXTURE / "elevation.json").read_text())
        cases = []
        for key, value in [("version", 99), ("version", True), ("lon_step", 0), ("lat_step", -1),
                           ("lon_min", math.nan), ("lat_min", math.inf), ("heights", [])]:
            data = copy.deepcopy(original); data[key] = value; cases.append(data)
        for value in (math.nan, math.inf, -math.inf, None):
            data = copy.deepcopy(original); data["heights"][0][0] = value; cases.append(data)
        data = copy.deepcopy(original); data["heights"][1].pop(); cases.append(data)
        for i, data in enumerate(cases):
            path = self.root / f"bad-{i}.json"; write_json(path, data)
            with self.subTest(case=i), self.assertRaises(REJECT):
                elevation.Grid.from_json(path)

    def test_elevation_codec_roundtrip_and_repeatability(self):
        tile = cube.Tile("nx", 2, 1, 2)
        values = [(-11000 + (i * 37) % 19850) for i in range(COUNT)]
        encoded = elevation.encode_tile(tile, values, codec="zlib")
        decoded = elevation.decode_tile(encoded)
        self.assertEqual(decoded["tile"], tile)
        self.assertEqual(list(decoded["heights"]), values)
        self.assertEqual(encoded, elevation.encode_tile(tile, values, codec="zlib"))
        self.assertLess(len(encoded), COUNT * 2)

    def test_fixture_encoded_values_match_affine_oracle_and_neighbor(self):
        tiles = [cube.Tile("ny", 6, x, 56) for x in (49, 50)]
        decoded = [elevation.decode_tile(elevation.build_elevation(t, self.grid, area_samples=1, codec="zlib"))["heights"] for t in tiles]
        for tile, heights in zip(tiles, decoded):
            self.assertEqual(len(heights), COUNT)
            for col, row in [(-1, -1), (0, 0), (45, 123), (255, 255), (256, 256)]:
                # Expected projection uses the independent literal face equation.
                u = 2 * ((tile.x*256 + col + .5) / (64*256)) - 1
                v = 2 * ((tile.y*256 + row + .5) / (64*256)) - 1
                lat, lon = latlon(direction("ny", u, v))
                expected = -4500 + 23*(lat-32) + 17*(lon+65)
                self.assertLessEqual(abs(heights[(row+1)*WIDTH + col+1] - expected), .5000001)
        a, b = decoded
        for row in range(-1, 257):
            self.assertEqual(a[(row+1)*WIDTH+257], b[(row+1)*WIDTH+1])
            self.assertEqual(a[(row+1)*WIDTH+256], b[(row+1)*WIDTH])

    def test_elevation_invalid_input_and_malformed_payload(self):
        tile = cube.Tile("px", 0, 0, 0)
        for values in [[], [0] * (COUNT - 1), [0] * (COUNT + 1),
                       [math.nan] + [0] * (COUNT - 1), [math.inf] + [0] * (COUNT - 1)]:
            with self.subTest(length=len(values), first=values[:1]), self.assertRaises(REJECT):
                elevation.encode_tile(tile, values, codec="zlib")
        good = elevation.encode_tile(tile, [0] * COUNT, codec="zlib")
        for data in [b"", b"not an Earth elevation tile", good[:4], good[:-1], good + b"trailing"]:
            with self.subTest(length=len(data)), self.assertRaises(REJECT):
                elevation.decode_tile(data)
        with self.assertRaises(REJECT):
            elevation.encode_tile(tile, [0] * COUNT, codec="unknown")


    def test_literal_codec_frame_decodes_signed_deltas_and_row_resets(self):
        # Build a raw-format specimen without invoking encode_tile. This catches
        # a matching encoder/decoder bug in wrapping signed deltas or row reset.
        header = struct.Struct("<8sHBBIIHHiBBI32s")
        deltas = [0] * COUNT
        deltas[:5] = [53536, 22000, 43537, 21998, 55537]
        deltas[258:261] = [65526, 1, 9]
        raw = struct.pack("<" + str(COUNT) + "H", *deltas)
        data = header.pack(b"SEARTH01", 1, 0, 0, 0, 0, 258, 258, 0, 2, 1,
                           COUNT * 2, hashlib.sha256(raw).digest()) + zlib.compress(raw)
        values = elevation.decode_tile(data)["heights"]
        self.assertEqual(values[:6], [-12000, 10000, -11999, 9999, 0, 0])
        self.assertEqual(values[258:262], [-10, -9, 0, 0])
        self.assertTrue(all(v == 0 for v in values[262:]))

    def test_binary_header_bounds_versions_hashes_and_decompression_bomb(self):
        header = struct.Struct("<8sHBBIIHHiBBI32s")
        good = elevation.encode_tile(cube.Tile("px", 0, 0, 0), [0] * COUNT, codec="zlib")
        fields = list(header.unpack_from(good))
        # Version, face, level, indices, dimensions, offset, codec, unit, size.
        for index, bad in [(1, 999), (2, 6), (3, 255), (4, 1), (5, 1),
                           (6, 65535), (7, 65535), (8, 2147483647),
                           (9, 255), (10, 0), (11, 4294967295), (12, b"x" * 32)]:
            changed = fields.copy(); changed[index] = bad
            with self.subTest(field=index), self.assertRaises(REJECT):
                elevation.decode_tile(header.pack(*changed) + good[header.size:])
        # A tiny compressed input expands to 16 times the one-tile limit, while
        # the header still advertises a legitimate tile-sized payload.
        bomb = header.pack(*fields) + zlib.compress(bytes(COUNT * 2 * 16), level=9)
        with self.assertRaises(REJECT):
            elevation.decode_tile(bomb)
        for payload in [good + good[header.size:], good[:header.size] + b"bad frame"]:
            with self.assertRaises(REJECT):
                elevation.decode_tile(payload)

    def test_zstd_roundtrip_and_bounded_frames_when_available(self):
        try:
            import zstandard
        except ImportError:
            self.skipTest("optional zstandard package not installed; zlib fixture coverage remains active")
        values = [-4500 + i % 777 for i in range(COUNT)]
        encoded = elevation.encode_tile(cube.Tile("pz", 0, 0, 0), values, codec="zstd")
        self.assertEqual(elevation.decode_tile(encoded)["heights"], values)
        self.assertEqual(encoded, elevation.encode_tile(cube.Tile("pz", 0, 0, 0), values, codec="zstd"))
        header = struct.Struct("<8sHBBIIHHiBBI32s")
        prefix = encoded[:header.size]
        bombs = [zstandard.ZstdCompressor().compress(bytes(COUNT * 2 * 16)),
                 zstandard.ZstdCompressor(write_content_size=False).compress(bytes(COUNT * 2)),
                 encoded[header.size:] + encoded[header.size:]]
        for payload in bombs:
            with self.subTest(size=len(payload)), self.assertRaises(REJECT):
                elevation.decode_tile(prefix + payload)

    def test_all_face_encoded_aprons_and_corners_agree_exactly(self):
        data = {"version": 1, "lon_min": -180, "lat_min": -90, "lon_step": 90,
                "lat_step": 90, "periodic": True,
                "heights": [[-1100, -900, -700, -900, -1100], [-200, 0, 200, 0, -200],
                            [700, 900, 1100, 900, 700]]}
        path = self.root / "global.json"; write_json(path, data)
        source = elevation.Grid.from_json(path)
        decoded = {face: elevation.decode_tile(elevation.build_elevation(
            cube.Tile(face, 0, 0, 0), source, area_samples=1, codec="zlib"))["heights"] for face in FACES}
        def height(face, i, j):
            return decoded[face][(j + 1) * WIDTH + i + 1]
        for face in FACES:
            for edge, (neighbor, other_edge, reverse) in EDGES[face].items():
                for k in range(256):
                    i, j = edge_address(edge, k, 256, ghost=True)
                    ni, nj = edge_address(other_edge, 255-k if reverse else k, 256)
                    self.assertEqual(height(face, i, j), height(neighbor, ni, nj))
        for face in FACES:
            for su in (-1, 1):
                for sv in (-1, 1):
                    sx, sy, sz = (1 if v > 0 else -1 for v in direction(face, su, sv))
                    owner = "px" if sx > 0 else "nx"
                    self.assertEqual(height(face, -1 if su < 0 else 256, -1 if sv < 0 else 256),
                                     height(owner, 255 if sy == sx else 0, 255 if sz > 0 else 0))

    def make_envi(self, name, rows, *, big_endian=False, lon=-2, lat=2, dx=1, dy=1, nodata=None):
        path = self.root / name
        values = [v for row in rows for v in row]
        path.write_bytes(struct.pack((">" if big_endian else "<") + str(len(values)) + "f", *values))
        header = (f"ENVI\nsamples = {len(rows[0])}\nlines = {len(rows)}\nbands = 1\n"
                  "header offset = 0\ndata type = 4\ninterleave = bsq\n"
                  f"byte order = {1 if big_endian else 0}\n"
                  f"map info = {{Geographic Lat/Lon, 1, 1, {lon}, {lat}, {dx}, {dy}, WGS-84, units=Degrees}}\n")
        if nodata is not None:
            header += f"data ignore value = {nodata}\n"
        path.with_suffix(".hdr").write_text(header, encoding="ascii")
        return path

    def test_envi_north_south_orientation_endianness_and_size(self):
        for big in (False, True):
            path = self.make_envi(f"grid-{big}.bin", [[11, 12, 13, 14], [21, 22, 23, 24],
                                                   [31, 32, 33, 34], [41, 42, 43, 44]], big_endian=big)
            source = elevation.EnviGrid(path)
            try:
                self.assertEqual(source.sample(1.5, -1.5), 11)
                self.assertEqual(source.sample(-1.5, 1.5), 44)
                self.assertEqual(source.sample(0, 0), 27.5)
                with self.assertRaises(REJECT):
                    source.sample(2.1, 0)
            finally:
                source.close()
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(REJECT):
                elevation.EnviGrid(path)

    def test_envi_nodata_and_global_poles(self):
        path = self.make_envi("nodata.bin", [[-9999, 2], [3, 4]], nodata=-9999)
        source = elevation.EnviGrid(path)
        try:
            with self.assertRaises(REJECT):
                source.sample(1.5, -1.5)
        finally:
            source.close()
        # Global area pixels cover the poles although their centres stop short.
        path = self.make_envi("global.bin", [[100] * 4, [-100] * 4], lon=-180, lat=90, dx=90, dy=90)
        source = elevation.EnviGrid(path)
        try:
            self.assertEqual(source.sample(89.99, 0), 100)
            self.assertEqual(source.sample(-89.99, 0), -100)
            self.assertEqual(source.sample(0, -180), source.sample(0, 180))
        finally:
            source.close()


class RoadTests(unittest.TestCase):
    def test_fixture_shared_junction_survives_simplification(self):
        features = roads.read_geojson(FIXTURE / "roads.geojson")
        tile = cube.Tile("ny", 6, 49, 56)
        result = roads.tile_roads(features, tile, tolerance_m=30)
        by_id = {line["id"]: line for line in result["roads"]}
        a = next(line for ident, line in by_id.items() if ident.startswith("synthetic-east-west"))
        b = next(line for ident, line in by_id.items() if ident.startswith("synthetic-north-south"))
        ja = [v for v in a["vertices"] if v["id"] == "node:junction"]
        jb = [v for v in b["vertices"] if v["id"] == "node:junction"]
        self.assertEqual(len(ja), 1)
        self.assertEqual(ja, jb)
        self.assertEqual(a["class"], "primary")
        self.assertEqual(b["class"], "local")

    def test_adjacent_tiles_share_boundary_node_identity(self):
        features = roads.read_geojson(FIXTURE / "roads.geojson")
        left = roads.tile_roads(features, cube.Tile("ny", 6, 49, 56))["roads"]
        right = roads.tile_roads(features, cube.Tile("ny", 6, 50, 56))["roads"]
        le = {v["id"]: v for line in left for v in line["vertices"] if v["u"] == 65535}
        ri = {v["id"]: v for line in right for v in line["vertices"] if v["u"] == 0}
        common = set(le) & set(ri)
        self.assertTrue(common, "Road crossing must produce one shared global boundary node")
        for ident in common:
            self.assertEqual(le[ident]["v"], ri[ident]["v"])

    def test_cube_face_crossing_and_antimeridian_connectivity(self):
        for coords, faces in [([[44, 0], [46, 0]], ("px", "py")),
                              ([[179, 0], [-179, 0]], ("nx",))]:
            fs = [feature("crossing", coords)]
            chunks = [roads.tile_roads(fs, cube.Tile(face, 0, 0, 0))["roads"] for face in faces]
            self.assertTrue(all(chunks), (coords, "line must not disappear at a seam"))
            if len(chunks) == 2:
                ids = [{v["id"] for r in chunk for v in r["vertices"]} for chunk in chunks]
                self.assertTrue(ids[0] & ids[1], "face seam must have a shared endpoint ID")
            else:
                # The two-degree antimeridian road stays short on the -X face.
                vertices = [v for r in chunks[0] for v in r["vertices"]]
                self.assertLess(max(v["u"] for v in vertices) - min(v["u"] for v in vertices), 2000)

    def test_simplification_preserves_shared_source_nodes(self):
        fs = [feature("main", [[-0.02, 0], [-0.01, 0], [0, 0], [0.01, 0], [0.02, 0]],
                      ["a", "discard1", "joint", "discard2", "b"]),
              feature("branch", [[0, 0], [0, 1]], ["joint", "tip"], cls="local")]
        result = roads.tile_roads(fs, cube.Tile("px", 0, 0, 0), tolerance_m=30)
        main = next(r for r in result["roads"] if r["id"].startswith("main"))
        self.assertEqual([v["id"] for v in main["vertices"]], ["node:a", "node:joint", "node:b"])
        reverse = roads.tile_roads(list(reversed(fs)), cube.Tile("px", 0, 0, 0), tolerance_m=30)
        self.assertEqual(result, reverse)

    def test_equivalent_antimeridian_endpoints_have_one_global_node(self):
        fs = [feature("west", [[179.9, 0], [180, 0]]),
              feature("east", [[-180, 0], [-179.9, 0]])]
        result = roads.tile_roads(fs, cube.Tile("nx", 0, 0, 0))
        west = next(r for r in result["roads"] if r["id"] == "west")
        east = next(r for r in result["roads"] if r["id"] == "east")
        self.assertEqual(west["vertices"][-1], east["vertices"][0])

    def test_equivalent_pole_and_signed_zero_nodes_share_identity(self):
        cases = [(cube.Tile("pz", 0, 0, 0), [[0, 89.9], [0, 90]], [[180, 90], [180, 89.9]]),
                 (cube.Tile("px", 0, 0, 0), [[-.1, 0], [0, -0.0]], [[-0.0, 0], [.1, 0]])]
        for tile, before, after in cases:
            result = roads.tile_roads([feature("before", before), feature("after", after)], tile)
            a = next(r for r in result["roads"] if r["id"] == "before")
            b = next(r for r in result["roads"] if r["id"] == "after")
            self.assertEqual(a["vertices"][-1], b["vertices"][0])

    def test_stored_road_node_identity_cannot_name_two_positions(self):
        value = roads.tile_roads([feature("road", [[0, 0], [1, 0]])], cube.Tile("px", 0, 0, 0))
        value["roads"][0]["vertices"][1]["id"] = value["roads"][0]["vertices"][0]["id"]
        with self.assertRaises(REJECT):
            roads.validate_road_tile(value)
        value = roads.tile_roads([feature("road", [[0, 0], [1, 0]])], cube.Tile("px", 0, 0, 0))
        value["version"] = True
        with self.assertRaises(REJECT):
            roads.validate_road_tile(value)

    def test_road_schema_and_nonfinite_coordinates(self):
        with tempfile.TemporaryDirectory(prefix="earth-road-test-") as tmp:
            path = Path(tmp) / "roads.json"
            cases = [feature("bad", [[0, 0], [math.nan, 1]]),
                     feature("bad", [[0, 0], [1, 91]]),
                     feature("bad", [[0, 0], [1, 1]], cls="unrecognized"),
                     feature("bad", [[0, 0], [1, 1]], node_ids=["only-one"])]
            for value in cases:
                write_json(path, {"type": "FeatureCollection", "features": [value]})
                with self.subTest(value=value), self.assertRaises(REJECT):
                    fs = roads.read_geojson(path)
                    roads.tile_roads(fs, cube.Tile("px", 0, 0, 0))


class AirportTests(unittest.TestCase):
    def test_filtering_and_metric_units(self):
        result = airports.read_airports(FIXTURE / "airports.csv", FIXTURE / "runways.csv")
        self.assertEqual(len(result), 1)
        airport = result[0]
        self.assertEqual(airport["ident"], "XBER")
        self.assertEqual(airport["icao_code"], "XBER")
        self.assertEqual(airport["iata_code"], "ZZZ")
        self.assertEqual(airport["latitude_deg"], 32.3)
        self.assertAlmostEqual(airport["elevation_m"], 15.24, places=9)
        self.assertEqual(len(airport["runways"]), 1)
        runway = airport["runways"][0]
        self.assertAlmostEqual(runway["length_m"], 914.4, places=9)
        self.assertAlmostEqual(runway["width_m"], 24.384, places=9)
        self.assertEqual(runway["le_heading_degT"], 90)
        self.assertEqual(runway["he_heading_degT"], 270)
        self.assertEqual(runway["surface"], "ASPH")

    def mutated_csv(self, source, key, value, path):
        with source.open(newline="", encoding="utf-8") as src:
            reader = csv.DictReader(src); fields = reader.fieldnames; rows = list(reader)
        rows[0][key] = value
        with path.open("w", newline="", encoding="utf-8") as dst:
            writer = csv.DictWriter(dst, fields, lineterminator="\n"); writer.writeheader(); writer.writerows(rows)

    def test_stored_airport_numbers_have_numeric_types(self):
        original = airports.read_airports(FIXTURE / "airports.csv", FIXTURE / "runways.csv")
        for key, value in [("latitude_deg", True), ("longitude_deg", "32.5"), ("elevation_m", False)]:
            changed = copy.deepcopy(original); changed[0][key] = value
            with self.subTest(key=key), self.assertRaises(REJECT):
                airports.validate_airports(changed)
        changed = copy.deepcopy(original); changed[0]["runways"][0]["length_m"] = "914.4"
        with self.assertRaises(REJECT):
            airports.validate_airports(changed)

    def test_blank_optional_airport_fields_remain_unknown(self):
        with tempfile.TemporaryDirectory(prefix="earth-airport-null-test-") as tmp:
            path = Path(tmp) / "runways.csv"
            self.mutated_csv(FIXTURE / "runways.csv", "width_ft", "", path)
            result = airports.read_airports(FIXTURE / "airports.csv", path)
            self.assertIsNone(result[0]["runways"][0]["width_m"])
            self.mutated_csv(FIXTURE / "runways.csv", "le_latitude_deg", "", path)
            with self.assertRaises(REJECT):
                airports.read_airports(FIXTURE / "airports.csv", path)

    def test_airport_validation_rejects_invalid_known_numbers(self):
        with tempfile.TemporaryDirectory(prefix="earth-airport-test-") as tmp:
            path = Path(tmp) / "airports.csv"
            for key, value in [("latitude_deg", "NaN"), ("longitude_deg", "Infinity"),
                               ("latitude_deg", "91"), ("elevation_ft", "-Infinity")]:
                self.mutated_csv(FIXTURE / "airports.csv", key, value, path)
                with self.subTest(key=key, value=value), self.assertRaises(REJECT):
                    airports.read_airports(path, FIXTURE / "runways.csv")
            path = Path(tmp) / "runways.csv"
            for key, value in [("length_ft", "-1"), ("width_ft", "nan"),
                               ("le_heading_degT", "361"), ("le_latitude_deg", "-91")]:
                self.mutated_csv(FIXTURE / "runways.csv", key, value, path)
                with self.subTest(key=key, value=value), self.assertRaises(REJECT):
                    airports.read_airports(FIXTURE / "airports.csv", path)


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="earth-adapter-test-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def dap_fixture():
        return ("Dataset { synthetic only; } fixture;\n"
                "z.z[2][3]\n[0],1,2,3\n[1],11,12,13\n"
                "z.lat[2]\n10,11\nz.lon[3]\n20,21,22\n")

    def test_opendap_axes_shape_provenance_and_determinism(self):
        source = self.root / "synthetic.asc"
        source.write_text(self.dap_fixture(), encoding="ascii")
        a, b = self.root / "a.json", self.root / "b.json"
        report = prepare.prepare_opendap(source, a)
        prepare.prepare_opendap(source, b)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        value = json.loads(a.read_bytes())
        self.assertEqual(value, {"version": 1, "lon_min": 20, "lat_min": 10, "lon_step": 1,
                                 "lat_step": 1, "periodic": False, "heights": [[1, 2, 3], [11, 12, 13]]})
        self.assertEqual(elevation.Grid.from_json(a).sample(10.5, 21.5), 7.5)
        self.assertEqual(report["sha256"], hashlib.sha256(a.read_bytes()).hexdigest())
        self.assertEqual(report["origin"]["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual(report["bytes"], a.stat().st_size)
        self.assertEqual(report["origin"]["bytes"], source.stat().st_size)

    def test_opendap_malformed_axes_dimensions_and_values_leave_no_output(self):
        good = self.dap_fixture()
        cases = [good.replace("10,11", "11,10"), good.replace("20,21,22", "20,21,23"),
                 good.replace("z.lat[2]", "z.lat[3]"), good.replace("z.z[2][3]", "z.z[999999][999999]"),
                 good.replace("[1],11,12,13", "[2],11,12,13"), good.replace("[0],1,2,3", "[0],1,2"),
                 good.replace("[0],1,2,3", "[0],NaN,2,3"), good.replace("10,11", "10,Infinity")]
        source, output = self.root / "bad.asc", self.root / "out.json"
        for i, text in enumerate(cases):
            source.write_text(text, encoding="ascii")
            with self.subTest(case=i), self.assertRaises(REJECT):
                prepare.prepare_opendap(source, output)
            self.assertFalse(output.exists())
        output.write_bytes(b"keep existing output")
        with self.assertRaises(REJECT):
            prepare.prepare_opendap(source, output)
        self.assertEqual(output.read_bytes(), b"keep existing output")

    @staticmethod
    def grip_feature(ident, road_class, access=0, multiline=False):
        points = [[[0, 0], [.01, .01]], [[.02, .02], [.03, .03]]] if multiline else [[0, 0], [.01, .01]]
        return {"type": "Feature", "properties": {"ID": ident, "GP_RTP": road_class, "GP_REX": access,
                "GP_RSI": 42, "GP_RSY": 2018, "GP_RCY": 2019},
                "geometry": {"type": "MultiLineString" if multiline else "LineString", "coordinates": points}}

    def test_grip_classes_exclusions_multiline_and_provenance(self):
        source = self.root / "grip.json"
        features = [self.grip_feature(f"road-{i}", i) for i in range(1, 6)]
        features += [self.grip_feature("split", 3, multiline=True),
                     self.grip_feature("closed", 0, access=3), self.grip_feature("construction", 0, access=4)]
        write_json(source, {"type": "FeatureCollection", "features": features})
        a, b = self.root / "a.json", self.root / "b.json"
        report = prepare.prepare_grip(source, a, id_field="id")
        prepare.prepare_grip(source, b, id_field="id")
        self.assertEqual(a.read_bytes(), b.read_bytes())
        normalized = {f["properties"]["id"]: f for f in roads.read_geojson(a)}
        self.assertEqual(len(normalized), 7)
        for i, cls in enumerate(("highway", "primary", "secondary", "tertiary", "local"), 1):
            self.assertEqual(normalized[f"road-{i}"]["properties"]["class"], cls)
        for part in (0, 1):
            value = normalized[f"split:part:{part}"]
            self.assertEqual(value["geometry"]["type"], "LineString")
            self.assertEqual(value["geometry"]["coordinates"], features[5]["geometry"]["coordinates"][part])
            props = value["properties"]["provenance"]
            self.assertEqual(props["gp_rsi"], 42)
            self.assertEqual(props["gp_rsy"], 2018)
            self.assertEqual(props["gp_rcy"], 2019)
            self.assertEqual(props["source_feature_id"], "split")
            self.assertEqual(props["source_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        tiled = roads.tile_roads(list(normalized.values()), cube.Tile("px", 0, 0, 0))
        for line in tiled["roads"]:
            self.assertEqual(line["provenance"], normalized[line["id"]]["properties"]["provenance"])
        self.assertEqual(report["excluded"], {"closed_or_construction": 2})
        self.assertEqual(report["class_counts"]["secondary"], 3)
        self.assertEqual(report["sha256"], hashlib.sha256(a.read_bytes()).hexdigest())

    def test_grip_unknown_class_requires_explicit_mapping_and_bad_values_fail(self):
        source, output = self.root / "grip.json", self.root / "roads.json"
        value = {"type": "FeatureCollection", "features": [self.grip_feature("unknown", 0)]}
        write_json(source, value)
        with self.assertRaises(REJECT):
            prepare.prepare_grip(source, output, id_field="id")
        self.assertFalse(output.exists())
        prepare.prepare_grip(source, output, id_field="id", unknown_class="tertiary")
        self.assertEqual(roads.read_geojson(output)[0]["properties"]["class"], "tertiary")
        for cls in (-1, 6, True, "1"):
            value["features"][0]["properties"]["GP_RTP"] = cls
            write_json(source, value)
            target = self.root / f"bad-{str(cls)}.json"
            with self.subTest(value=cls), self.assertRaises(REJECT):
                prepare.prepare_grip(source, target, id_field="id")
            self.assertFalse(target.exists())

    def test_raster_preserves_preexisting_auxiliary_xml(self):
        source = self.root / "invalid.tif"; source.write_bytes(b"not a raster")
        output = self.root / "output.bin"
        sentinel = self.root / "output.bin.aux.xml"
        sentinel.write_bytes(b"pre-existing metadata must survive")
        before = file_snapshot(self.root)
        with mock.patch.object(prepare.subprocess, "run") as run:
            with self.assertRaises(REJECT):
                prepare.prepare_raster(source, output)
            run.assert_not_called()
        self.assertEqual(file_snapshot(self.root), before)

    def test_raster_vrt_is_rejected_before_invoking_gdal(self):
        source = self.root / "untrusted.vrt"
        source.write_text('<VRTDataset rasterXSize="2" rasterYSize="2"><VRTRasterBand dataType="Float32" band="1">'
                          '<SimpleSource><SourceFilename>/not-an-authorized-source</SourceFilename></SimpleSource>'
                          '</VRTRasterBand></VRTDataset>', encoding="ascii")
        with mock.patch.object(prepare.subprocess, "run") as run:
            with self.assertRaises(REJECT):
                prepare.prepare_raster(source, self.root / "output.bin")
            run.assert_not_called()
        self.assertEqual(set(file_snapshot(self.root)), {"untrusted.vrt"})

    def test_raster_gdal_failure_cleans_only_its_staging(self):
        source = self.root / "invalid.tif"; source.write_bytes(b"II*\0invalid synthetic TIFF")
        (self.root / "unrelated.txt").write_bytes(b"keep me")
        before = file_snapshot(self.root)
        with mock.patch.object(prepare.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "gdal_translate")):
            with self.assertRaises(REJECT):
                prepare.prepare_raster(source, self.root / "output.bin")
        self.assertEqual(file_snapshot(self.root), before)
        self.assertFalse(any(p.is_dir() for p in self.root.iterdir()))

    def test_real_geotiff_to_envi_and_header_bytes_are_repeatable(self):
        if shutil.which("gdal_translate") is None:
            self.skipTest("optional GDAL executable not installed")
        asc = self.root / "synthetic.asc"
        asc.write_text("ncols 2\nnrows 2\nxllcorner -2\nyllcorner 0\ncellsize 1\nNODATA_value -9999\n"
                       "11 12\n21 22\n", encoding="ascii")
        source = self.root / "synthetic.tif"
        subprocess.run(["gdal_translate", "-of", "GTiff", "-a_srs", "EPSG:4326", str(asc), str(source)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        a, b = self.root / "a" / "output.bin", self.root / "b" / "output.bin"
        first = prepare.prepare_raster(source, a)
        second = prepare.prepare_raster(source, b)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        self.assertEqual(a.with_suffix(".hdr").read_bytes(), b.with_suffix(".hdr").read_bytes())
        self.assertEqual(first["header_sha256"], second["header_sha256"])
        self.assertEqual(first["header_sha256"], hashlib.sha256(a.with_suffix(".hdr").read_bytes()).hexdigest())
        self.assertEqual(first["source_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        grid = elevation.EnviGrid(a)
        try:
            self.assertEqual(grid.sample(1.5, -1.5), 11)
            self.assertEqual(grid.sample(.5, -.5), 22)
            self.assertEqual(grid.sample(1, -1), 16.5)
        finally:
            grid.close()


class PackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="earth-pack-test-")
        cls.root = Path(cls.temp.name)
        cls.pristine = cls.root / "pristine"
        cls.manifest = pack.build_pack(FIXTURE / "config.json", cls.pristine)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix="case-", dir=self.root))
        self.target = self.case / "pack"
        shutil.copytree(self.pristine, self.target)

    def test_fixture_is_synthetic_small_and_sources_are_pinned(self):
        self.assertLess(sum(p.stat().st_size for p in FIXTURE.rglob("*") if p.is_file()), 1_000_000)
        self.assertIn("fabricated", (FIXTURE / "README.md").read_text().lower())
        conf = json.loads((FIXTURE / "config.json").read_text())
        for source in [conf["elevation"], conf["roads"], *conf["airports"].values()]:
            self.assertEqual(source["sha256"], hashlib.sha256((FIXTURE / source["path"]).read_bytes()).hexdigest())

    def test_fixture_hashes_survive_windows_style_git_checkout(self):
        if shutil.which("git") is None:
            self.skipTest("Git unavailable for isolated checkout contract test")
        checkout = self.case / "checkout"
        shutil.copytree(FIXTURE, checkout)
        expected = file_snapshot(checkout)
        def git(*args):
            return subprocess.run(["git", "-c", "core.autocrlf=true", "-C", str(checkout), *args],
                                  check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        git("init", "--quiet")
        git("add", ".")
        for relative in expected:
            (checkout / relative).unlink()
        git("checkout-index", "--all", "--force")
        for relative, raw in expected.items():
            self.assertEqual((checkout / relative).read_bytes(), raw, relative)

    def test_two_full_builds_are_byte_identical(self):
        second = self.case / "second"
        actual = pack.build_pack(FIXTURE / "config.json", second)
        self.assertEqual(actual, self.manifest)
        self.assertEqual(file_snapshot(second), file_snapshot(self.pristine))
        self.assertEqual(pack.verify_pack(second), actual)

    def test_verification_does_not_mutate_pack(self):
        before = file_snapshot(self.target)
        self.assertEqual(pack.verify_pack(self.target), self.manifest)
        self.assertEqual(file_snapshot(self.target), before)

    def test_hash_corruption_missing_and_truncated_files(self):
        payload = next(self.target.rglob("*.elv"))
        original = payload.read_bytes()
        for broken in [original[:-1], original[:-1] + bytes([original[-1] ^ 1]), b""]:
            payload.write_bytes(broken)
            before = file_snapshot(self.target)
            with self.assertRaises(REJECT):
                pack.verify_pack(self.target)
            self.assertEqual(file_snapshot(self.target), before)
        payload.unlink()
        with self.assertRaises(REJECT):
            pack.verify_pack(self.target)

    def test_unknown_manifest_version_and_nonfinite_json_rejected(self):
        manifest_path = self.target / "manifest.json"
        for version in (999, True, math.nan, math.inf):
            data = copy.deepcopy(self.manifest); data["format_version"] = version
            write_json(manifest_path, data)
            with self.subTest(version=version), self.assertRaises(REJECT):
                pack.verify_pack(self.target)

    def test_manifest_paths_never_escape_pack(self):
        outside = self.case / "outside.txt"
        outside.write_text("must remain unchanged", encoding="utf-8")
        for path in ["../outside.txt", str(outside), "C:\\outside.txt", "roads/../../outside.txt",
                     "roads\\..\\outside.txt", "./manifest.json", "", ".", "elevation//x.elv"]:
            changed = copy.deepcopy(self.manifest); changed["files"][0]["path"] = path
            write_json(self.target / "manifest.json", changed)
            with self.subTest(path=path), self.assertRaises(REJECT):
                pack.verify_pack(self.target)
            self.assertEqual(outside.read_text(), "must remain unchanged")

    def test_manifest_inventory_sizes_and_hash_schema(self):
        cases = []
        for key, value in [("bytes", -1), ("bytes", True), ("bytes", 1.5), ("bytes", 2**63),
                           ("sha256", "x" * 64), ("sha256", "0" * 63), ("kind", "unknown")]:
            changed = copy.deepcopy(self.manifest); changed["files"][0][key] = value; cases.append(changed)
        changed = copy.deepcopy(self.manifest); changed["files"].append(changed["files"][0]); cases.append(changed)
        changed = copy.deepcopy(self.manifest); changed["files"].pop(); cases.append(changed)
        for index, changed in enumerate(cases):
            write_json(self.target / "manifest.json", changed)
            before = file_snapshot(self.target)
            with self.subTest(case=index), self.assertRaises(REJECT):
                pack.verify_pack(self.target)
            self.assertEqual(file_snapshot(self.target), before)

    def test_duplicate_json_keys_and_wrong_root_types(self):
        path = self.target / "manifest.json"
        canonical = json.dumps(self.manifest)
        malformed = ['{"format_version":1,' + canonical[1:], "[]", "null", '"string"',
                     canonical[:-1], '{"format_version":1,"files":NaN}']
        for text in malformed:
            path.write_text(text, encoding="utf-8")
            with self.subTest(text=text[:80]), self.assertRaises(REJECT):
                pack.verify_pack(self.target)

    def test_symlink_payload_and_directory_rejected(self):
        entry = next(e for e in self.manifest["files"] if e["kind"] == "elevation")
        path = self.target / entry["path"]
        outside = self.case / "outside.elv"; outside.write_bytes(path.read_bytes())
        path.unlink()
        try:
            path.symlink_to(outside)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("this filesystem does not permit symbolic links: " + str(exc))
        with self.assertRaises(REJECT):
            pack.verify_pack(self.target)
        path.unlink(); path.write_bytes(outside.read_bytes())
        directory = self.target / "elevation"
        external = self.case / "external-elevation"
        directory.rename(external); directory.symlink_to(external, target_is_directory=True)
        with self.assertRaises(REJECT):
            pack.verify_pack(self.target)

    def test_rehashed_malicious_payload_still_requires_valid_schema(self):
        # Updating the outer hash must not turn an invalid payload into a valid
        # pack. This also establishes that verify decodes, rather than only hashes.
        originals = file_snapshot(self.target)
        for kind in ("elevation", "roads", "airports"):
            manifest = copy.deepcopy(self.manifest)
            entry = next(e for e in manifest["files"] if e["kind"] == kind)
            path = self.target / entry["path"]
            raw = originals[entry["path"]]
            if kind == "elevation":
                bad = raw[:8] + struct.pack("<H", 65535) + raw[10:]
            elif kind == "roads":
                value = json.loads(raw); value["version"] = 999
                bad = (json.dumps(value) + "\n").encode()
            else:
                value = json.loads(raw)
                # Airport output is either a plain table or a versioned envelope.
                if isinstance(value, list):
                    value[0]["latitude_deg"] = 999
                else:
                    value["airports"][0]["latitude_deg"] = 999
                bad = (json.dumps(value) + "\n").encode()
            path.write_bytes(bad)
            entry["bytes"], entry["sha256"] = len(bad), hashlib.sha256(bad).hexdigest()
            write_json(self.target / "manifest.json", manifest)
            with self.subTest(kind=kind), self.assertRaises(REJECT):
                pack.verify_pack(self.target)
            path.write_bytes(raw)

    def test_provenance_licenses_and_measured_counts_are_complete(self):
        sources = {source["id"]: source for source in self.manifest["sources"]}
        config = json.loads((FIXTURE / "config.json").read_text())
        expected = {"elevation": config["elevation"], "roads": config["roads"], **config["airports"]}
        self.assertEqual(set(sources), set(expected))
        for ident, spec in expected.items():
            self.assertEqual(sources[ident]["sha256"], spec["sha256"])
            self.assertEqual(sources[ident]["version"], spec["version"])
            self.assertEqual(sources[ident]["url"], spec["url"])
            self.assertEqual(sources[ident]["bytes"], (FIXTURE / spec["path"]).stat().st_size)
            self.assertEqual((self.target / sources[ident]["license_path"]).read_bytes(),
                             (FIXTURE / "LICENSE.txt").read_bytes())
        measurements = self.manifest["measurements"]
        self.assertEqual(measurements["source_bytes"], sum((FIXTURE / spec["path"]).stat().st_size for spec in expected.values()))
        self.assertEqual(measurements["payload_bytes"], sum(e["bytes"] for e in self.manifest["files"]))
        self.assertEqual(measurements["elevation_tiles"], 2)
        self.assertEqual(measurements["airport_count"], 1)

    def test_metadata_types_do_not_confuse_booleans_with_integer_contracts(self):
        for section, key in [("scheme", "apron"), ("quantization", "unit_m"), ("measurements", "airport_count")]:
            value = copy.deepcopy(self.manifest); value[section][key] = True
            write_json(self.target / "manifest.json", value)
            with self.subTest(section=section), self.assertRaises(REJECT):
                pack.verify_pack(self.target)

    def test_late_build_failure_cleans_staging_and_never_publishes_partial_pack(self):
        source = self.case / "fixture"; shutil.copytree(FIXTURE, source)
        config = json.loads((source / "config.json").read_text())
        # The first tile is valid, but the second has no regional elevation.
        config["tiles"] = [{"face": "ny", "level": 6, "x": 49, "y": 56},
                           {"face": "px", "level": 0, "x": 0, "y": 0}]
        write_json(source / "config.json", config)
        before = file_snapshot(self.case)
        entries = {p.relative_to(self.case) for p in self.case.rglob("*")}
        with self.assertRaises(REJECT):
            pack.build_pack(source / "config.json", self.case / "partial")
        self.assertEqual(file_snapshot(self.case), before)
        self.assertEqual({p.relative_to(self.case) for p in self.case.rglob("*")}, entries)

    def test_build_input_failures_leave_existing_directory_untouched(self):
        src = self.case / "fixture"; shutil.copytree(FIXTURE, src)
        path = src / "config.json"
        config = json.loads(path.read_text()); config["elevation"]["sha256"] = "0" * 64
        write_json(path, config)
        before = file_snapshot(self.target)
        with self.assertRaises(REJECT):
            pack.build_pack(path, self.target)
        self.assertEqual(file_snapshot(self.target), before)
        fresh = self.case / "must-not-appear"
        with self.assertRaises(REJECT):
            pack.build_pack(path, fresh)
        self.assertFalse(fresh.exists(), "failed builds must not publish partial output")


if __name__ == "__main__":
    unittest.main(verbosity=2)
