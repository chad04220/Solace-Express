"""The raster layers: elevation from ETOPO 2022 (30 arc-seconds, cell-centred on the same grid as the tiles, so
copied rather than resampled) and land cover from CGLS-LC100 (1/1008 degree, reduced 3 x 3 by majority to 1/336).
Needs NumPy."""
import mmap
import os

import numpy as np

from . import svb
from .tiff import open_tiff

ELEV_SPD, COVER_SPD, APRON = 120, 336, 1
SEA_FLOOR_CLAMP = -200   # (the sea is opaque: deeper than this only costs bytes)

# CGLS-LC100 discrete classes (docs/REAL_EARTH_PLAN.md section 2)
COVER_UNKNOWN, COVER_SNOW, COVER_WATER, COVER_SEA, COVER_BARE = 0, 70, 80, 200, 60
COVER_CLASSES = (0, 20, 30, 40, 50, 60, 70, 80, 90, 100, 111, 112, 113, 114, 115, 116, 121, 122, 123, 124, 125, 126, 200)


class EnviGrid:
    """A north-up float32 BSQ ENVI raster on a geographic grid, memory mapped."""

    def __init__(self, path):
        hdr = {}
        with open(path + ".hdr") as f:
            for line in f:
                if "=" in line:
                    k, v = line.split("=", 1)
                    hdr[k.strip()] = v.strip()
        self.width, self.height = int(hdr["samples"]), int(hdr["lines"])
        if int(hdr["data type"]) != 4 or hdr.get("interleave", "bsq") != "bsq":
            raise ValueError("%s: expected float32 BSQ" % path)
        info = [p.strip() for p in hdr["map info"].strip("{}").split(",")]
        self.x0, self.y0, self.dx, self.dy = float(info[3]), float(info[4]), float(info[5]), float(info[6])
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        self.a = np.frombuffer(self.m, dtype="<f4").reshape(self.height, self.width)

    def window(self, x0, y0, w, h):
        """Columns wrap round the globe; rows outside are clamped to the edge."""
        rows = np.clip(np.arange(y0, y0 + h), 0, self.height - 1)
        cols = np.arange(x0, x0 + w) % self.width
        return self.a[np.ix_(rows, cols)]


class TiffGrid:
    """The same over a GeoTIFF (tiff.py), local or remote."""

    def __init__(self, path):
        self.t = open_tiff(path)
        self.width, self.height = self.t.width, self.t.height
        self.x0, self.dx, self.y0, self.dy = self.t.geo

    def window(self, x0, y0, w, h):
        out = None
        # (wrap in longitude: at most two pieces)
        pieces = []
        x = x0
        while x < x0 + w:
            xm = x % self.width
            take = min(self.width - xm, x0 + w - x)
            pieces.append(self.t.window(xm, y0, take, h))
            x += take
        out = np.concatenate(pieces, axis=1) if len(pieces) > 1 else pieces[0]
        return out


def open_grid(path):
    return EnviGrid(path) if os.path.exists(path + ".hdr") else TiffGrid(path)


def _grid_index(grid, lat, lon, spd):
    """The source pixel of the tile's north-west interior sample, given the source has spd pixels a degree
    (checked: the tile grid must be the source's own)."""
    if abs(grid.dx * spd - 1) > 1e-6 or abs(grid.dy * spd - 1) > 1e-6:
        raise ValueError("source grid is not %d a degree" % spd)
    col = (lon - grid.x0) * spd
    row = (grid.y0 - (lat + 1)) * spd
    if abs(col - round(col)) > 1e-6 or abs(row - round(row)) > 1e-6:
        raise ValueError("source grid is not aligned with the tiles")
    return int(round(row)), int(round(col))


def elevation_tile(grid, lat, lon):
    """Tile (lat, lon) of ETOPO 30": int16 metres, the sea floor clamped, (120 + 2)^2 samples."""
    n, a = ELEV_SPD, APRON
    row, col = _grid_index(grid, lat, lon, n)
    w = grid.window(col - a, row - a, n + 2 * a, n + 2 * a).astype(np.float64)
    bad = ~np.isfinite(w) | (w < -12000)
    w[bad] = 0
    w = np.maximum(np.rint(w), SEA_FLOOR_CLAMP)
    return np.clip(w, -32768, 32767).astype(np.int16)


def _majority3(block):
    """block: (3m, 3m) uint8 classes -> (m, m), each cell the class most of its 3 x 3 pixels hold (ties to the
    one met first in reading order). Unknown (0) pixels never win while any pixel is known."""
    m = block.shape[0] // 3
    v = block.reshape(m, 3, m, 3).transpose(0, 2, 1, 3).reshape(m, m, 9)
    best = np.zeros((m, m), dtype=np.uint8)
    best_count = np.full((m, m), -1, dtype=np.int16)
    for k in range(9):
        cand = v[:, :, k]
        count = (v == cand[:, :, None]).sum(axis=2).astype(np.int16)
        count[cand == COVER_UNKNOWN] = 0
        better = count > best_count
        best[better] = cand[better]
        best_count[better] = count[better]
    return best


def cover_tile(grid, lat, lon, elevation=None):
    """Tile (lat, lon) of the land cover: uint8 classes, (336 + 2)^2. Outside the source's 80 N - 60 S (or where it
    has no data) the class follows the elevation tile when given: land is snow and ice there, the rest sea."""
    n, a, k = COVER_SPD, APRON, 3
    src_spd = n * k
    m = n + 2 * a
    if lat >= 80 or lat < -60:
        out = np.full((m, m), COVER_SEA, dtype=np.uint8)
    else:
        row, col = _grid_index(grid, lat, lon, src_spd)
        w = grid.window(col - a * k, row - a * k, m * k, m * k)
        w = np.where(w == 255, COVER_UNKNOWN, w).astype(np.uint8)
        out = _majority3(w)
    if elevation is not None and (lat >= 80 or lat < -60 or (out == COVER_UNKNOWN).any()):
        e = _resample_nearest(elevation, m)
        fill = np.where(e > 0, COVER_SNOW if (lat >= 60 or lat < -60) else COVER_BARE, COVER_SEA).astype(np.uint8)
        out = np.where((out == COVER_UNKNOWN) | (lat >= 80) | (lat < -60), fill, out).astype(np.uint8)
    return out


def _resample_nearest(tile, m):
    """An elevation tile ((120 + 2)^2) at the cover's (336 + 2)^2 sample centres, nearest."""
    n_e = ELEV_SPD
    centres = (np.arange(m) - APRON + 0.5) / COVER_SPD   # (degrees from the tile's north/west edge)
    idx = np.clip(np.floor(centres * n_e).astype(int) + APRON, 0, n_e + 2 * APRON - 1)
    return tile[np.ix_(idx, idx)]


def write_layer(root, layer, tiles, spd):
    """tiles: {(lat, lon): array} -> the bundle files. Returns {relative path: bytes}."""
    sample_type = svb.TYPE_INT16 if layer == svb.LAYER_ELEV else svb.TYPE_UINT8
    width = spd + 2 * APRON
    blocks = {}
    for (lat, lon), arr in tiles.items():
        blocks.setdefault(svb.block_of(lat, lon), {})[(lat, lon)] = svb.encode_raster(arr, sample_type, width)
    written = {}
    for (south, west), entries in sorted(blocks.items()):
        path = svb.bundle_path(root, layer, south, west)
        written[os.path.relpath(path, root)] = svb.write_bundle(path, layer, sample_type, south, west, spd, APRON, entries)
    return written
