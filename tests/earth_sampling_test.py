#!/usr/bin/env python3
"""Optional NumPy equivalence contracts; no downloads or runtime assets."""
import importlib.util
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from earth import cube, elevation, pack

HAS_NUMPY = importlib.util.find_spec("numpy") is not None
if HAS_NUMPY:
    import numpy as np
    from earth import sampling_numpy as fast


def envi(root, name="global", *, big=False, offset=0, rows=None,
         lon=-180, lat=90, dx=1, dy=1, nodata=None):
    if rows is None:
        # Steep deterministic checkerboard + signed relief, with no random seed
        # or floating source-generation ambiguity.
        rows = [[((x * 671 + y * 349) % 21001) - 11500
                 for x in range(360)] for y in range(180)]
    path = root / (name + ".bin")
    flat = [v for row in rows for v in row]
    path.write_bytes(bytes(offset) + struct.pack((">" if big else "<") + str(len(flat)) + "f", *flat))
    header = (f"ENVI\nsamples = {len(rows[0])}\nlines = {len(rows)}\nbands = 1\n"
              f"header offset = {offset}\ndata type = 4\ninterleave = bsq\n"
              f"byte order = {int(big)}\n"
              f"map info = {{Geographic Lat/Lon, 1, 1, {lon}, {lat}, {dx}, {dy}, WGS-84}}\n")
    if nodata is not None:
        header += f"data ignore value = {nodata}\n"
    path.with_suffix(".hdr").write_text(header, encoding="ascii")
    return path


class PortableSamplerTests(unittest.TestCase):
    def test_scalar_import_and_sampling_without_site_packages(self):
        code = ("from tools.earth.elevation import sample_elevation; from tools.earth.cube import Tile; "
                "source=type('Constant',(),{'sample':lambda self,lat,lon:17})(); "
                "assert set(sample_elevation(Tile('px',0,0,0),source))=={17}; "
                "import sys; assert 'numpy' not in sys.modules")
        result = subprocess.run([sys.executable, "-S", "-c", code], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_numpy_has_actionable_explicit_error(self):
        code = ("from tools.earth.elevation import sample_elevation; from tools.earth.cube import Tile; "
                "sample_elevation(Tile('px',0,0,0),None,sampler='numpy')")
        result = subprocess.run([sys.executable, "-S", "-c", code], cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("optional NumPy package; use --sampler scalar", result.stderr)

    def test_required_numpy_suite_cannot_silently_skip(self):
        result = subprocess.run([sys.executable, "-S", str(Path(__file__).resolve()), "--require-numpy"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tests require NumPy", result.stderr)

    def test_invalid_backend_or_n_fails_before_sampling(self):
        tile = cube.Tile("px", 0, 0, 0)
        for sampler in (None, "auto", "other"):
            with self.assertRaisesRegex(ValueError, "unsupported elevation sampler"):
                elevation.sample_elevation(tile, None, sampler=sampler)
        for n in (True, 0, 17, 1.5):
            with self.assertRaisesRegex(ValueError, "area_samples"):
                elevation.sample_elevation(tile, None, n, sampler="numpy")


@unittest.skipUnless(HAS_NUMPY, "optional NumPy is not installed; scalar tests remain active")
class NumpySamplerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="earth-numpy-test-")
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_batch_addresses_match_canonical_reference_all_faces(self):
        for face in cube.FACES:
            for level, tx, ty in ((0, 0, 0), (6, 0, 63), (16, 65535, 0)):
                tile = cube.Tile(face, level, tx, ty)
                # Every core/apron address at L0, including all oriented seams
                # and eight corners; additional largest-level corner tiles.
                for start in range(0, cube.WIDTH ** 2, fast.BATCH_CELLS):
                    end = min(cube.WIDTH ** 2, start + fast.BATCH_CELLS)
                    col, row, f, i, j = fast._locations(tile, start, end)
                    for k in range(len(col)):
                        self.assertEqual((cube.FACES[f[k]], int(i[k]), int(j[k])),
                                         cube.sample_location(tile, int(col[k]), int(row[k])))

    def test_bilinear_byte_order_offset_wrap_caps_and_invalid_data(self):
        for big in (False, True):
            source = elevation.EnviGrid(envi(self.root, str(big), big=big, offset=128))
            try:
                raster = np.ndarray((source.height, source.storage_width), dtype=source.item.format,
                                    buffer=source.mapped, offset=source.offset)
                self.assertFalse(raster.flags.writeable)
                points = [(90, -180), (-90, 180), (0, 179.999), (12.3, -44.4),
                          (89.5, -179.5), (-89.5, 180.5), (0, -540)]
                lat, lon = np.array(points).T
                self.assertTrue(np.array_equal(fast._bilinear(source, raster, lat, lon),
                                               [source.sample(*p) for p in points]))
                for bad in ((91, 0), (math.nan, 0), (0, math.inf)):
                    with self.assertRaises(ValueError):
                        fast._bilinear(source, raster, np.array([bad[0]]), np.array([bad[1]]))
            finally:
                source.close()
        for bad in (math.nan, math.inf, -12001, 10001, -9999):
            path = envi(self.root, "bad", rows=[[bad, 0], [0, 0]], lon=-1, lat=1,
                        dx=1, dy=1, nodata=-9999)
            source = elevation.EnviGrid(path)
            try:
                raster = np.ndarray((2, 2), dtype="<f4", buffer=source.mapped)
                with self.assertRaisesRegex(ValueError, "missing or invalid"):
                    fast._bilinear(source, raster, np.array([0.]), np.array([0.]))
            finally:
                source.close()

    def test_regional_antimeridian_and_coverage_fail_closed(self):
        source = elevation.EnviGrid(envi(self.root, rows=[[1, 2, 3], [4, 5, 6]],
                                         lon=178, lat=1, dx=1, dy=1))
        try:
            raster = np.ndarray((2, 3), dtype="<f4", buffer=source.mapped)
            lat, lon = np.array([0., 0.]), np.array([179., -180.])
            self.assertEqual(fast._bilinear(source, raster, lat, lon).tolist(),
                             [source.sample(a, b) for a, b in zip(lat, lon)])
            with self.assertRaisesRegex(ValueError, "outside source coverage"):
                fast._bilinear(source, raster, np.array([1.]), np.array([179.]))
        finally:
            source.close()

    def test_nodata_stencil_at_float_boundary_matches_scalar(self):
        tile = cube.Tile("pz", 7, 23, 52)
        col, row = 101, 41
        f, i, j = cube.sample_location(tile, col, row)
        for samples, valid in (([-9999, 1, 1], True), ([1, 1, -9999], False)):
            source = elevation.EnviGrid(envi(self.root, rows=[samples, samples],
                lon=-76.4489255276565, lat=62.10313546168512, nodata=-9999))
            try:
                raster = np.ndarray((2, 3), dtype="<f4", buffer=source.mapped)
                def vector():
                    return fast._quadrature(source, raster, np.array([cube.FACES.index(f)]),
                                            np.array([i]), np.array([j]), 256 << 7, 1)[0]
                if valid:
                    self.assertEqual(elevation.sample_cell(tile, source, col, row, 1), 1)
                    self.assertEqual(vector(), 1)
                else:
                    with self.assertRaisesRegex(ValueError, "missing or invalid"):
                        elevation.sample_cell(tile, source, col, row, 1)
                    with self.assertRaisesRegex(ValueError, "missing or invalid"):
                        vector()
            finally:
                source.close()

    def test_full_tile_near_nodata_boundary_matches_scalar(self):
        tile = cube.Tile("pz", 12, 1889, 656)
        source = elevation.EnviGrid(envi(self.root, rows=[[-9999, 1, 1, 1, 1]]*180,
            lon=-7.402389675100306, lat=90, nodata=-9999))
        try:
            self.assertEqual(elevation.sample_elevation(tile, source, 1), [1.]*(258**2))
            self.assertEqual(elevation.sample_elevation(tile, source, 1, sampler="numpy"), [1.]*(258**2))
        finally:
            source.close()

    def test_hidden_ridge_keeps_requested_n_instead_of_adapting(self):
        tile = cube.Tile("px", 0, 0, 0)
        west, east = (cube.direction_to_latlon(cube.face_uv_to_direction("px", 2*x/256-1, 0))[1]
                      for x in (127, 128))
        step = (east-west)/16
        row = [10000. if k == 8 else 0. for k in range(17)]
        source = elevation.EnviGrid(envi(self.root, rows=[row]*3, lon=west-step/2,
                                         lat=1.5, dx=step, dy=1))
        try:
            raster = np.ndarray((3, 17), dtype="<f4", buffer=source.mapped)
            got = {}
            for n in (2, 4, 8, 16):
                got[n] = fast._quadrature(source, raster, np.array([0]), np.array([127]),
                                           np.array([127]), 256, n)[0]
                self.assertAlmostEqual(got[n], elevation.sample_cell(tile, source, 127, 127, n), places=7)
            self.assertLess(max(abs(got[n]) for n in (2, 4, 8)), 1e-7)
            self.assertGreater(got[16], 625)
        finally:
            source.close()

    def test_unrounded_n1_n2_n8_n16_matches_scalar_probes(self):
        source = elevation.EnviGrid(envi(self.root))
        try:
            raster = np.ndarray((source.height, source.storage_width), dtype="<f4", buffer=source.mapped)
            cells = [(-1, -1), (0, 0), (127, 128), (255, 255), (256, 256), (256, 33), (53, -1)]
            for face in cube.FACES:
                for level, x, y in ((0, 0, 0), (6, 0, 63), (16, 32768, 32768)):
                    tile = cube.Tile(face, level, x, y)
                    locations = [cube.sample_location(tile, *p) for p in cells]
                    f = np.array([cube.FACES.index(p[0]) for p in locations])
                    i, j = np.array([p[1:] for p in locations]).T
                    for n in (1, 2, 8, 16):
                        got = fast._quadrature(source, raster, f, i, j, 256 << level, n)
                        want = [elevation.sample_cell(tile, source, col, row, n) for col, row in cells]
                        self.assertLess(max(abs(a-b) for a, b in zip(got, want)), 1e-7)
        finally:
            source.close()

    def test_full_tiles_lossless_identity_repeatability_and_all_aprons(self):
        source = elevation.EnviGrid(envi(self.root))
        try:
            tiles = [cube.Tile(f, 0, 0, 0) for f in cube.FACES]
            values = {}
            for tile in tiles:
                a = elevation.build_elevation(tile, source, 2, "zlib")
                b = elevation.build_elevation(tile, source, 2, "zlib", sampler="numpy")
                self.assertEqual(a, b, tile)
                self.assertEqual(b, elevation.build_elevation(tile, source, 2, "zlib", sampler="numpy"))
                values[tile.face] = elevation.decode_tile(b)["heights"]
            for tile in tiles:
                for row in range(-1, 257):
                    for col in range(-1, 257):
                        if 0 <= row < 256 and 0 <= col < 256:
                            continue
                        f, i, j = cube.sample_location(tile, col, row)
                        self.assertEqual(values[tile.face][(row+1)*258+col+1], values[f][(j+1)*258+i+1])
        finally:
            source.close()

    def test_rounding_ties_recompute_using_scalar_and_batches_are_bounded(self):
        source = elevation.EnviGrid(envi(self.root, rows=[[1.5] * 4] * 2, dx=90, dy=90))
        tile = cube.Tile("px", 0, 0, 0)
        try:
            original = fast._quadrature
            sizes = []
            def tracked(*args):
                sizes.append(len(args[3]))
                return original(*args)
            with mock.patch.object(fast, "_quadrature", side_effect=tracked), \
                    mock.patch.object(fast, "sample_cell", wraps=elevation.sample_cell) as reference:
                got = elevation.sample_elevation(tile, source, 2, sampler="numpy")
            self.assertEqual(reference.call_count, 258**2)
            self.assertLessEqual(max(sizes), fast.BATCH_CELLS)
            self.assertEqual(sum(sizes), 258**2)
            self.assertEqual(got, elevation.sample_elevation(tile, source, 2))
        finally:
            source.close()

    def test_json_grid_is_explicitly_rejected(self):
        grid = elevation.Grid.from_json(ROOT / "tests/fixtures/earth/elevation.json")
        with self.assertRaisesRegex(ValueError, "ENVI source; use --sampler scalar"):
            elevation.sample_elevation(cube.Tile("ny", 6, 49, 56), grid, sampler="numpy")

    def test_pack_identity_with_envi_and_failed_build_cleanup(self):
        path = envi(self.root)
        license_path = self.root / "notice.txt"
        license_path.write_text("Synthetic test data only.")
        config = {"version": 1, "name": "synthetic-numpy", "coverage": "synthetic test", "codec": "zlib",
                  "area_samples": 2, "tiles": [cube.Tile("py", 6, 29, 51).as_dict()],
                  "elevation": {"path": str(path), "format": "envi", "url": "https://example.invalid/synthetic",
                                "version": "synthetic-v1", "license_id": "TEST", "license_path": str(license_path)}}
        a, b = self.root / "scalar", self.root / "numpy"
        ma, mb = pack.build_pack(config, a), pack.build_pack(config, b, sampler="numpy")
        self.assertEqual(ma, mb)
        for file in a.rglob("*"):
            if file.is_file():
                self.assertEqual(file.read_bytes(), (b / file.relative_to(a)).read_bytes())
        path.write_bytes(bytes(path.stat().st_size))
        path.with_suffix(".hdr").write_text(path.with_suffix(".hdr").read_text() + "data ignore value = 0\n")
        with self.assertRaisesRegex(ValueError, "missing or invalid"):
            pack.build_pack(config, self.root / "failed", sampler="numpy")
        self.assertFalse((self.root / "failed").exists())
        self.assertEqual(list(self.root.glob(".earth-pack-*")), [])


if __name__ == "__main__":
    if "--require-numpy" in sys.argv:
        sys.argv.remove("--require-numpy")
        if not HAS_NUMPY:
            raise SystemExit("earth sampling tests require NumPy; install tools/earth/requirements-numpy.txt")
    unittest.main(verbosity=2)
