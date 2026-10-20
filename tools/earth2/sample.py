"""The game's sampling (src/earth_data.cpp Sampler), the same arithmetic in Python, over a pack: the reference the
C++ is tested against (tests/earth_data_test.cpp reads the fixture's expect.txt, written from this)."""
import math
import os

from . import glo90, svb
from .tiff import open_tiff


def cubic_weights(t):
    t2, t3 = t * t, t * t * t
    return (-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2)


class PackSampler:
    def __init__(self, root):
        self.root = root
        self.bundles = {}
        self.tiles = {}
        self.fine = {}
        bits = os.path.join(root, "glo90.bits")
        self.bits = open(bits, "rb").read() if os.path.exists(bits) else None

    def _bundle(self, layer, lat, lon):
        path = svb.bundle_path(self.root, layer, lat, lon)
        if path not in self.bundles:
            self.bundles[path] = svb.Bundle(path) if os.path.exists(path) else None
        return self.bundles[path]

    def tile(self, layer, lat, lon):
        key = (layer, lat, lon)
        if key not in self.tiles:
            b = self._bundle(layer, lat, lon)
            samples = b.samples(lat, lon) if b else None
            self.tiles[key] = (samples, b.width) if samples is not None else None
        return self.tiles[key]

    def listed(self, lat, lon):
        if not self.bits:
            return False
        i = (lat + 90) * 360 + (lon + 180)
        return (self.bits[i >> 3] >> (i & 7)) & 1

    def fine_tile(self, lat, lon):
        key = (lat, lon)
        if key not in self.fine:
            p = os.path.join(self.root, "glo90", glo90.tile_name(lat, lon) + ".tif")
            t = None
            if self.listed(lat, lon) and os.path.exists(p):
                tf = open_tiff(p)
                west, dlon, north, dlat = tf.geo
                a = tf.window(0, 0, tf.width, tf.height)
                t = {"rows": tf.height, "cols": tf.width, "west": west, "north": north, "dlon": dlon, "dlat": dlat,
                     "start": int(round((lat + 1 - north) / dlat)),
                     "h": [[float(v) if math.isfinite(v) and -12000 < v < 9000 else 0.0 for v in row] for row in a.tolist()]}
            self.fine[key] = t
        return self.fine[key]

    def etopo_sample(self, row, col):
        rows, cols = 180 * 120, 360 * 120
        row = min(max(row, 0), rows - 1)
        col = col % cols
        lat, lon = 89 - row // 120, col // 120 - 180
        t = self.tile(svb.LAYER_ELEV, lat, lon)
        if t is None:
            return 0.0
        s, w = t
        return float(s[(row % 120 + 1) * w + (col % 120 + 1)])

    def coarse(self, lat, lon):
        fr, fc = (90.0 - lat) * 120 - 0.5, (lon + 180.0) * 120 - 0.5
        r0, c0 = math.floor(fr), math.floor(fc)
        wr, wc = cubic_weights(fr - r0), cubic_weights(fc - c0)
        return sum(wr[i] * sum(wc[j] * self.etopo_sample(r0 - 1 + i, c0 - 1 + j) for j in range(4)) for i in range(4))

    def glo_point(self, row, lon_deg):
        lat = 89 - math.floor(row / 1200)
        lon_t = math.floor(lon_deg)
        t = self.fine_tile(lat, lon_t)
        if t is None:
            return (0.0, not self.listed(lat, (lon_t + 180) % 360 - 180))
        r = row - (89 - lat) * 1200 - t["start"]
        if r < 0 or r >= t["rows"]:
            return (0.0, False)
        fc = (lon_deg - t["west"]) / t["dlon"]
        c0 = min(max(math.floor(fc), 0), t["cols"] - 1)
        f = fc - c0
        a = t["h"][r][c0]
        if f <= 1e-9:
            return (a, True)
        if c0 + 1 < t["cols"]:
            b = t["h"][r][c0 + 1]
        else:
            b, ok2 = self.glo_point(row, t["west"] + 1.0 + 1e-12)
            if not ok2:
                b = a
        return (a + (b - a) * f, True)

    def elevation(self, lat, lon):
        t = self.fine_tile(math.floor(lat), math.floor(lon))
        if t is None:
            return self.coarse(lat, lon)
        fr = (90.0 - lat) * 1200
        r0 = math.floor(fr)
        wr = cubic_weights(fr - r0)
        fc = (lon - t["west"]) / t["dlon"]
        c0 = math.floor(fc)
        wc = cubic_weights(fc - c0)
        h, ok = 0.0, True
        for i in range(4):
            row = 0.0
            for j in range(4):
                v, good = self.glo_point(r0 - 1 + i, t["west"] + (c0 - 1 + j) * t["dlon"])
                ok = ok and good
                row += wc[j] * v
            h += wr[i] * row
        return h if ok else self.coarse(lat, lon)

    def cover(self, lat, lon):
        la, lo = math.floor(lat), math.floor(lon)
        t = self.tile(svb.LAYER_COVER, la, lo)
        if t is None:
            return 0
        s, w = t
        r = min(max(math.floor((la + 1 - lat) * 336), 0), 335)
        c = min(max(math.floor((lon - lo) * 336), 0), 335)
        return s[(r + 1) * w + (c + 1)]
