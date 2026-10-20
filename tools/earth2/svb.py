"""The bundle file (.svb): one 10 x 10 degree block of one layer, its 100 one-degree tiles.

Layout (little endian):
  header, 64 bytes: magic "SVB2", u16 format version, u8 layer, u8 sample type, i16 south, i16 west (the block's
    south-west corner, degrees, multiples of 10), u16 samples per degree, u16 apron, u32 tile count (100), u32 flags,
    40 bytes reserved (zero)
  index, 100 entries of 24 bytes, entry (lat - south) * 10 + (lon - west) for the tile whose south-west corner is
    (lat, lon): u64 offset, u32 stored size, u32 raw size, u32 CRC-32 of the raw bytes, u16 kind (0 absent, 1 zlib,
    2 constant), i16 the constant value
  the tiles' zlib streams, in index order

A raster tile is (n + 2a)^2 cell-centred samples, rows from north to south: stored row r, column c is centred at
latitude lat + 1 - (r - a + 0.5) / n, longitude lon + (c - a + 0.5) / n, the apron (a samples) copying the
neighbouring tiles. Elevation is int16 metres with each row delta coded (the first sample as it is, then each
sample minus the one before it, wrapping at 16 bits); cover is uint8 classes as they are. A blob tile (the roads) is
its own format (roads.py).
"""
import array
import os
import struct
import sys
import zlib

MAGIC = b"SVB2"
FORMAT_VERSION = 1
LAYER_ELEV, LAYER_COVER, LAYER_ROADS, LAYER_LOCAL_ROADS = 1, 2, 3, 4   # (local roads: GRIP class 5, optional)
LAYER_NAMES = {LAYER_ELEV: "elev", LAYER_COVER: "cover", LAYER_ROADS: "roads", LAYER_LOCAL_ROADS: "lroads"}
TYPE_INT16, TYPE_UINT8, TYPE_BLOB = 1, 2, 3
KIND_ABSENT, KIND_ZLIB, KIND_CONSTANT = 0, 1, 2
HEADER = struct.Struct("<4sHBBhhHHII40x")
ENTRY = struct.Struct("<QIIIHh")
BLOCK = 10
TILES = BLOCK * BLOCK
assert HEADER.size == 64 and ENTRY.size == 24


def block_of(lat, lon):
    """The south-west corner of the 10-degree block holding the tile whose south-west corner is (lat, lon)."""
    return (lat // BLOCK) * BLOCK, (lon // BLOCK) * BLOCK


def corner_name(lat, lon, lat_digits=2, lon_digits=3):
    return "%s%0*d%s%0*d" % ("N" if lat >= 0 else "S", lat_digits, abs(lat), "E" if lon >= 0 else "W", lon_digits, abs(lon))


def bundle_path(root, layer, lat, lon):
    """The bundle file holding tile (lat, lon) of a layer, under the pack root."""
    south, west = block_of(lat, lon)
    return os.path.join(root, LAYER_NAMES[layer], corner_name(south, west) + ".svb")


def entry_index(south, west, lat, lon):
    return (lat - south) * BLOCK + (lon - west)


def _delta_rows(values, width):
    """int16 samples (array('h')), each row delta coded."""
    out = array.array("h", values)
    for r in range(len(values) // width):
        base = r * width
        for c in range(width - 1, 0, -1):
            out[base + c] = ((values[base + c] - values[base + c - 1] + 32768) & 0xFFFF) - 32768
    return out


def _undelta_rows(values, width):
    out = array.array("h", values)
    for r in range(len(values) // width):
        base = r * width
        for c in range(1, width):
            out[base + c] = ((out[base + c - 1] + values[base + c] + 32768) & 0xFFFF) - 32768
    return out


def encode_raster(samples, sample_type, width):
    """A raster tile's raw bytes from its samples (a flat sequence or a NumPy array, rows north to south) and whether
    it is constant. Returns (kind, raw bytes or None, constant value)."""
    if hasattr(samples, "dtype"):   # (NumPy: the builders' path, the same bytes)
        flat = samples.reshape(-1)
        if (flat == flat[0]).all():
            return KIND_CONSTANT, None, int(flat[0])
        if sample_type == TYPE_INT16:
            rows = flat.astype("int32").reshape(-1, width)
            delta = rows.copy()
            delta[:, 1:] = rows[:, 1:] - rows[:, :-1]
            return KIND_ZLIB, (delta & 0xFFFF).astype("<u2").tobytes(), 0
        if sample_type == TYPE_UINT8:
            return KIND_ZLIB, flat.astype("uint8").tobytes(), 0
        raise ValueError("not a raster type: %r" % sample_type)
    first = samples[0]
    if all(v == first for v in samples):
        return KIND_CONSTANT, None, int(first)
    if sample_type == TYPE_INT16:
        raw = _delta_rows(array.array("h", samples), width)
        if sys.byteorder != "little":
            raw.byteswap()
        return KIND_ZLIB, raw.tobytes(), 0
    if sample_type == TYPE_UINT8:
        return KIND_ZLIB, bytes(samples), 0
    raise ValueError("not a raster type: %r" % sample_type)


def decode_raster(raw, sample_type, width):
    if sample_type == TYPE_INT16:
        values = array.array("h")
        values.frombytes(raw)
        if sys.byteorder != "little":
            values.byteswap()
        return _undelta_rows(values, width)
    if sample_type == TYPE_UINT8:
        return array.array("B", raw)
    raise ValueError("not a raster type: %r" % sample_type)


def write_bundle(path, layer, sample_type, south, west, samples_per_degree, apron, tiles):
    """tiles: {(lat, lon): (kind, raw bytes or None, constant)} for the tiles present; the rest are absent. Written
    to path + ".tmp" then moved into place. Returns the file's size."""
    if south % BLOCK or west % BLOCK:
        raise ValueError("block corner must be a multiple of 10")
    entries = [None] * TILES
    payloads = []
    offset = HEADER.size + ENTRY.size * TILES
    for i in range(TILES):
        lat, lon = south + i // BLOCK, west + i % BLOCK
        tile = tiles.get((lat, lon))
        if tile is None:
            entries[i] = (0, 0, 0, 0, KIND_ABSENT, 0)
            continue
        kind, raw, constant = tile
        if kind == KIND_CONSTANT:
            entries[i] = (0, 0, 0, 0, KIND_CONSTANT, constant)
            continue
        stored = zlib.compress(raw, 9)
        entries[i] = (offset, len(stored), len(raw), zlib.crc32(raw) & 0xFFFFFFFF, KIND_ZLIB, 0)
        payloads.append(stored)
        offset += len(stored)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(HEADER.pack(MAGIC, FORMAT_VERSION, layer, sample_type, south, west, samples_per_degree, apron, TILES, 0))
        for e in entries:
            f.write(ENTRY.pack(*e))
        for p in payloads:
            f.write(p)
    os.replace(tmp, path)
    return offset


class Bundle:
    """A bundle file, read with every check the game makes."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            data = f.read()
        if len(data) < HEADER.size + ENTRY.size * TILES:
            raise ValueError("%s: short bundle" % path)
        (magic, version, self.layer, self.sample_type, self.south, self.west, self.samples_per_degree, self.apron,
         count, self.flags) = HEADER.unpack_from(data, 0)
        if magic != MAGIC or version != FORMAT_VERSION or count != TILES:
            raise ValueError("%s: not an SVB2 bundle" % path)
        if self.south % BLOCK or self.west % BLOCK or not -90 <= self.south < 90 or not -180 <= self.west < 180:
            raise ValueError("%s: bad block corner" % path)
        self.entries = [ENTRY.unpack_from(data, HEADER.size + ENTRY.size * i) for i in range(TILES)]
        self._data = data
        end = HEADER.size + ENTRY.size * TILES
        for i, (off, size, raw_size, crc, kind, const) in enumerate(self.entries):
            if kind == KIND_ZLIB:
                if off < end or off + size > len(data):
                    raise ValueError("%s: tile %d outside the file" % (path, i))
            elif kind not in (KIND_ABSENT, KIND_CONSTANT):
                raise ValueError("%s: tile %d of unknown kind %d" % (path, i, kind))

    @property
    def width(self):
        return self.samples_per_degree + 2 * self.apron

    def kind(self, lat, lon):
        return self.entries[entry_index(self.south, self.west, lat, lon)][4]

    def raw(self, lat, lon):
        off, size, raw_size, crc, kind, const = self.entries[entry_index(self.south, self.west, lat, lon)]
        if kind != KIND_ZLIB:
            return None
        raw = zlib.decompress(self._data[off:off + size])
        if len(raw) != raw_size or (zlib.crc32(raw) & 0xFFFFFFFF) != crc:
            raise ValueError("%s: tile (%d, %d) fails its check" % (self.path, lat, lon))
        return raw

    def samples(self, lat, lon):
        """A raster tile's samples (flat, rows north to south), or None when absent."""
        off, size, raw_size, crc, kind, const = self.entries[entry_index(self.south, self.west, lat, lon)]
        n = self.width * self.width
        if kind == KIND_ABSENT:
            return None
        if kind == KIND_CONSTANT:
            return array.array("h" if self.sample_type == TYPE_INT16 else "B", [const]) * n
        values = decode_raster(self.raw(lat, lon), self.sample_type, self.width)
        if len(values) != n:
            raise ValueError("%s: tile (%d, %d) is %d samples, not %d" % (self.path, lat, lon, len(values), n))
        return values

    def tiles(self):
        for i, e in enumerate(self.entries):
            if e[4] != KIND_ABSENT:
                yield self.south + i // BLOCK, self.west + i % BLOCK
