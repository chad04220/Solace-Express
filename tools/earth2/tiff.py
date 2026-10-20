"""A small GeoTIFF reader for the sources the pack is built from: classic or BigTIFF, tiled or in strips, one sample
per pixel (uint8, int16, uint16, int32, float32), uncompressed or Deflate, predictor none, horizontal or floating
point. Reads a local file or a remote one by HTTP ranges (only the tiles a window needs). Needs NumPy.

The GLO-90 tiles (Float32, Deflate, predictor 3, one internal tile), CGLS-LC100 (uint8, Deflate, 256^2 tiles, BigTIFF)
and the ETOPO 2022 GeoTIFFs are all of this kind. src/earth_data.cpp reads the GLO-90 ones the same way.
"""
import os
import struct
import urllib.request
import zlib

import numpy as np

_TYPES = {1: ("B", 1), 2: ("c", 1), 3: ("H", 2), 4: ("I", 4), 5: ("II", 8), 6: ("b", 1), 7: ("B", 1), 8: ("h", 2),
          9: ("i", 4), 10: ("ii", 8), 11: ("f", 4), 12: ("d", 8), 16: ("Q", 8), 17: ("q", 8), 18: ("Q", 8)}
TAG_WIDTH, TAG_HEIGHT, TAG_BITS, TAG_COMPRESSION = 256, 257, 258, 259
TAG_STRIP_OFFSETS, TAG_SPP, TAG_ROWS_PER_STRIP, TAG_STRIP_COUNTS = 273, 277, 278, 279
TAG_PLANAR, TAG_PREDICTOR, TAG_TILE_W, TAG_TILE_H, TAG_TILE_OFFSETS, TAG_TILE_COUNTS = 284, 317, 322, 323, 324, 325
TAG_SAMPLE_FORMAT, TAG_PIXEL_SCALE, TAG_TIEPOINT, TAG_NODATA = 339, 33550, 33922, 42113


class FileSource:
    def __init__(self, path):
        self.name = path
        self.f = open(path, "rb")
        self.size = os.path.getsize(path)

    def read(self, offset, size):
        self.f.seek(offset)
        data = self.f.read(size)
        if len(data) != size:
            raise IOError("%s: short read at %d" % (self.name, offset))
        return data


class HttpSource:
    """Ranged reads of a remote file: small reads in 1 MiB blocks kept for the life of the source (the header and its
    directory), big ones (a tile index, a run of tiles) in one request of their own. With a cache directory, every
    fetched range is kept there too, so a second build reads nothing from the network."""
    BLOCK = 1 << 20

    def __init__(self, url, retries=4, cache_dir=None):
        self.name = url
        self.url = url
        self.retries = retries
        self.cache_dir = cache_dir or os.environ.get("EARTH2_HTTP_CACHE")
        self.blocks = {}
        self.fetched = 0
        self.size = None

    def _cached(self, first, last):
        if not self.cache_dir:
            return None
        import hashlib
        key = hashlib.sha1(self.url.encode()).hexdigest()[:16]
        return os.path.join(self.cache_dir, "%s_%d_%d.bin" % (key, first, last))

    def _get(self, first, last):
        import time
        if self.size is not None:
            last = min(last, self.size - 1)
        path = self._cached(first, last)
        if path and os.path.exists(path):
            with open(path, "rb") as f:
                return f.read()
        for attempt in range(self.retries + 1):
            try:
                req = urllib.request.Request(self.url, headers={"Range": "bytes=%d-%d" % (first, last)})
                with urllib.request.urlopen(req, timeout=300) as r:
                    data = r.read()
                    status = r.status
                    total = (r.headers.get("Content-Range") or "").rpartition("/")[2]
                if total.isdigit():
                    self.size = int(total)
                    last = min(last, self.size - 1)
                if status != 206 or len(data) != last - first + 1:
                    raise IOError("%s: ranged read %d-%d gave %d bytes (HTTP %d)" % (self.url, first, last, len(data), status))
                self.fetched += len(data)
                if path:
                    os.makedirs(self.cache_dir, exist_ok=True)
                    with open(path + ".tmp", "wb") as f:
                        f.write(data)
                    os.replace(path + ".tmp", path)
                return data
            except Exception:
                if attempt == self.retries:
                    raise
                time.sleep(2 ** (attempt + 1))

    def read(self, offset, size):
        if size > 2 * self.BLOCK:
            return self.read_exact(offset, size)
        b0, b1 = offset // self.BLOCK, (offset + size - 1) // self.BLOCK
        missing = [b for b in range(b0, b1 + 1) if b not in self.blocks]
        while missing:   # (each run of missing blocks in one request)
            run = [missing.pop(0)]
            while missing and missing[0] == run[-1] + 1:
                run.append(missing.pop(0))
            data = self._get(run[0] * self.BLOCK, (run[-1] + 1) * self.BLOCK - 1)
            for i, b in enumerate(run):
                self.blocks[b] = data[i * self.BLOCK:(i + 1) * self.BLOCK]
        joined = b"".join(self.blocks[b] for b in range(b0, b1 + 1))
        start = offset - b0 * self.BLOCK
        out = joined[start:start + size]
        if len(out) != size:
            raise IOError("%s: read past the end at %d" % (self.url, offset))
        return out

    def read_exact(self, offset, size):
        """One request for exactly these bytes, not kept in memory (a tile index, a run of tiles)."""
        data = self._get(offset, offset + size - 1)
        if len(data) != size:
            raise IOError("%s: read past the end at %d" % (self.url, offset))
        return data


class Tiff:
    """The first image of a TIFF (its full-resolution raster); overviews are the further images (ifds)."""

    def __init__(self, source, image=0):
        self.src = source
        head = self.src.read(0, 16)
        if head[:2] == b"II":
            self.bo = "<"
        elif head[:2] == b"MM":
            self.bo = ">"
        else:
            raise ValueError("%s: not a TIFF" % source.name)
        magic = struct.unpack(self.bo + "H", head[2:4])[0]
        self.big = magic == 43
        if magic not in (42, 43):
            raise ValueError("%s: not a TIFF (magic %d)" % (source.name, magic))
        offset = struct.unpack(self.bo + "Q", head[8:16])[0] if self.big else struct.unpack(self.bo + "I", head[4:8])[0]
        self._spans = {}
        self.ifds = []
        while offset and len(self.ifds) <= image:
            ifd, offset = self._ifd(offset)
            self.ifds.append(ifd)
        if len(self.ifds) <= image:
            raise ValueError("%s: no image %d" % (source.name, image))
        t = self.ifds[image]
        self.tags = t
        self.width, self.height = t[TAG_WIDTH][0], t[TAG_HEIGHT][0]
        bits = t.get(TAG_BITS, (1,))[0]
        fmt = t.get(TAG_SAMPLE_FORMAT, (1,))[0]
        if t.get(TAG_SPP, (1,))[0] != 1:
            raise ValueError("%s: more than one sample per pixel" % source.name)
        self.dtype = {(8, 1): "u1", (8, 2): "i1", (16, 1): "u2", (16, 2): "i2", (32, 1): "u4", (32, 2): "i4",
                      (32, 3): "f4", (64, 3): "f8"}[(bits, fmt)]
        self.compression = t.get(TAG_COMPRESSION, (1,))[0]
        if self.compression not in (1, 8, 32946):
            raise ValueError("%s: compression %d is not supported" % (source.name, self.compression))
        self.predictor = t.get(TAG_PREDICTOR, (1,))[0]
        if TAG_TILE_W in t:
            self.tw, self.th = t[TAG_TILE_W][0], t[TAG_TILE_H][0]
            self.offsets, self.counts = t[TAG_TILE_OFFSETS], t[TAG_TILE_COUNTS]
        else:
            self.tw, self.th = self.width, t.get(TAG_ROWS_PER_STRIP, (self.height,))[0]
            self.offsets, self.counts = t[TAG_STRIP_OFFSETS], t[TAG_STRIP_COUNTS]
        self.across = (self.width + self.tw - 1) // self.tw
        self.down = (self.height + self.th - 1) // self.th
        if len(self.offsets) < self.across * self.down:
            raise ValueError("%s: %d tiles listed, %d needed" % (source.name, len(self.offsets), self.across * self.down))
        scale = self.ifds[0].get(TAG_PIXEL_SCALE)
        tie = self.ifds[0].get(TAG_TIEPOINT)
        self.geo = None
        if scale and tie:   # (x0, dx, y0, dy): pixel (i, j)'s corner is at x0 + i dx, y0 - j dy
            k = self.ifds[0][TAG_WIDTH][0] / self.width   # (an overview's pixels are coarser by its reduction)
            self.geo = (tie[3] - tie[0] * scale[0], scale[0] * k, tie[4] + tie[1] * scale[1], scale[1] * k)
        nod = t.get(TAG_NODATA) or self.ifds[0].get(TAG_NODATA)
        self.nodata = float(nod.decode().strip("\0 ")) if isinstance(nod, bytes) and nod.strip(b"\0 ") else None

    def _ifd(self, offset):
        if self.big:
            count = struct.unpack(self.bo + "Q", self.src.read(offset, 8))[0]
            entry, head, nxt = 20, 8, 8
        else:
            count = struct.unpack(self.bo + "H", self.src.read(offset, 2))[0]
            entry, head, nxt = 12, 2, 4
        data = self.src.read(offset + head, count * entry + nxt)
        tags = {}
        for i in range(count):
            e = data[i * entry:(i + 1) * entry]
            if self.big:
                tag, typ, n = struct.unpack(self.bo + "HHQ", e[:12])
                inline = e[12:20]
            else:
                tag, typ, n = struct.unpack(self.bo + "HHI", e[:8])
                inline = e[8:12]
            code, size = _TYPES.get(typ, ("B", 1))
            total = size * n
            if total <= len(inline):
                raw = inline[:total]
            else:
                ptr = struct.unpack(self.bo + ("Q" if self.big else "I"), inline)[0]
                raw = self.src.read(ptr, total)
            if typ == 2 or typ == 7:
                tags[tag] = raw
            else:
                tags[tag] = struct.unpack(self.bo + code * n if len(code) == 1 else self.bo + code * n, raw)
        next_offset = struct.unpack(self.bo + ("Q" if self.big else "I"), data[count * entry:count * entry + nxt])[0]
        return tags, next_offset

    def _decode(self, data, rows):
        if self.compression in (8, 32946):
            data = zlib.decompress(data)
        dt = np.dtype(self.dtype).newbyteorder(self.bo)
        width = self.tw
        if self.predictor == 3:   # floating point: bytes split by significance per row, then byte differences
            b = np.frombuffer(data, dtype=np.uint8)[:rows * width * dt.itemsize].reshape(rows, dt.itemsize * width)
            b = np.cumsum(b, axis=1, dtype=np.uint8)
            b = b.reshape(rows, dt.itemsize, width).transpose(0, 2, 1).copy()
            return b.view(np.dtype(self.dtype).newbyteorder(">")).reshape(rows, width).astype(self.dtype)
        a = np.frombuffer(data, dtype=dt)[:rows * width].reshape(rows, width)
        if self.predictor == 2:
            a = np.cumsum(a, axis=1, dtype=a.dtype)
        return a.astype(self.dtype)

    def tile(self, tx, ty):
        """One tile (or strip) as an array of tw x th (the last strip may be shorter)."""
        i = ty * self.across + tx
        rows = self.th if TAG_TILE_W in self.tags else min(self.th, self.height - ty * self.th)
        size = self.counts[i]
        if size == 0:
            return np.full((rows, self.tw), self.nodata if self.nodata is not None else 0, dtype=self.dtype)
        data = self._spans.pop(self.offsets[i], None)
        if data is None or len(data) != size:
            data = self.src.read(self.offsets[i], size)
        return self._decode(data, rows)

    def prefetch(self, x0, y0, w, h):
        """For a remote source: fetch every tile of a window in as few requests as the file's layout allows (runs of
        tiles less than 256 KiB apart in one request); tile() takes them from there."""
        if not isinstance(self.src, HttpSource):
            return
        tx0, tx1 = x0 // self.tw, (x0 + w - 1) // self.tw
        ty0, ty1 = y0 // self.th, (y0 + h - 1) // self.th
        spans = sorted((self.offsets[ty * self.across + tx], self.counts[ty * self.across + tx])
                       for ty in range(ty0, ty1 + 1) for tx in range(tx0, tx1 + 1) if self.counts[ty * self.across + tx])
        run = []
        def flush():
            if run:
                first, last = run[0][0], run[-1][0] + run[-1][1]
                data = self.src.read_exact(first, last - first)
                self._spans.update({o: data[o - first:o - first + s] for o, s in run})
        for off, size in spans:
            if run and off - (run[-1][0] + run[-1][1]) > (256 << 10):
                flush()
                run = []
            run.append((off, size))
        flush()

    def window(self, x0, y0, w, h, fill=None):
        """Pixels [x0, x0 + w) x [y0, y0 + h); outside the raster: fill (the nodata value, else 0)."""
        if fill is None:
            fill = self.nodata if self.nodata is not None else 0
        out = np.full((h, w), fill, dtype=self.dtype)
        cx0, cy0 = max(x0, 0), max(y0, 0)
        cx1, cy1 = min(x0 + w, self.width), min(y0 + h, self.height)
        if cx0 >= cx1 or cy0 >= cy1:
            return out
        self.prefetch(cx0, cy0, cx1 - cx0, cy1 - cy0)
        for ty in range(cy0 // self.th, (cy1 - 1) // self.th + 1):
            for tx in range(cx0 // self.tw, (cx1 - 1) // self.tw + 1):
                t = self.tile(tx, ty)
                gx0, gy0 = tx * self.tw, ty * self.th
                ax0, ax1 = max(cx0, gx0), min(cx1, gx0 + self.tw)
                ay0, ay1 = max(cy0, gy0), min(cy1, gy0 + t.shape[0])
                if ax0 < ax1 and ay0 < ay1:
                    out[ay0 - y0:ay1 - y0, ax0 - x0:ax1 - x0] = t[ay0 - gy0:ay1 - gy0, ax0 - gx0:ax1 - gx0]
        return out


def open_tiff(path_or_url, image=0):
    if path_or_url.startswith("http://") or path_or_url.startswith("https://"):
        return Tiff(HttpSource(path_or_url), image)
    return Tiff(FileSource(path_or_url), image)
