"""Copernicus DEM GLO-90: which 1-degree tiles exist (a 360 x 180 bit map in the pack, so the game never asks for
an ocean tile), their names in the AWS open data bucket, and a cropped copy for the test fixture.

The bitmap: bit (lat + 90) * 360 + (lon + 180), least significant bit first, for the tile whose south-west corner
is (lat, lon). A tile is Copernicus_DSM_COG_30_<N|S>dd_00_<E|W>ddd_00_DEM.tif in a folder of the same name; its
samples are points (PixelIsPoint) at lat + 1 - r / rows, lon + c / cols, r and c from 0: the tile holds its north and
west edges, its neighbours the south and east ones.
"""
import os
import re
import struct
import urllib.parse
import urllib.request
import zlib

BUCKET = "copernicus-dem-90m"
URL = "https://%s.s3.amazonaws.com/" % BUCKET
_NAME = re.compile(r"Copernicus_DSM_COG_30_([NS])(\d\d)_00_([EW])(\d\d\d)_00_DEM")


def tile_name(lat, lon):
    return "Copernicus_DSM_COG_30_%s%02d_00_%s%03d_00_DEM" % ("N" if lat >= 0 else "S", abs(lat), "E" if lon >= 0 else "W", abs(lon))


def tile_url(lat, lon):
    n = tile_name(lat, lon)
    return URL + n + "/" + n + ".tif"


def parse_name(key):
    m = _NAME.search(key)
    if not m:
        return None
    lat = int(m.group(2)) * (1 if m.group(1) == "N" else -1)
    lon = int(m.group(4)) * (1 if m.group(3) == "E" else -1)
    return lat, lon


def list_bucket():
    """{(lat, lon): bytes} for every DEM tile in the bucket (S3 list-objects-v2, about 27 pages)."""
    tiles, token = {}, None
    while True:
        q = {"list-type": "2", "max-keys": "1000"}
        if token:
            q["continuation-token"] = token
        xml = urllib.request.urlopen(URL + "?" + urllib.parse.urlencode(q), timeout=120).read().decode()
        for key, size in re.findall(r"<Key>([^<]+)</Key>.*?<Size>(\d+)</Size>", xml, re.S):
            if key.endswith("_DEM.tif"):
                at = parse_name(key)
                if at:
                    tiles[at] = int(size)
        m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", xml)
        if not m:
            return tiles
        token = m.group(1)


def write_listing(path, tiles):
    with open(path, "w") as f:
        for (lat, lon), size in sorted(tiles.items()):
            f.write("%d %d %d\n" % (lat, lon, size))


def read_listing(path):
    tiles = {}
    with open(path) as f:
        for line in f:
            lat, lon, size = line.split()
            tiles[(int(lat), int(lon))] = int(size)
    return tiles


def bitmap(tiles):
    bits = bytearray(360 * 180 // 8)
    for lat, lon in tiles:
        i = (lat + 90) * 360 + (lon + 180)
        bits[i >> 3] |= 1 << (i & 7)
    return bytes(bits)


def write_float_tiff(path, values, west, north, step_x, step_y):
    """A GLO-90-shaped GeoTIFF of these samples (rows north to south): Float32, Deflate, floating-point predictor,
    one internal tile padded to multiples of 16, PixelIsPoint at (west + c step_x, north - r step_y). The game's
    reader and tiff.py read it exactly as they read the real tiles (the test fixture's)."""
    import numpy as np
    h, w = values.shape
    tw, th = (w + 15) // 16 * 16, (h + 15) // 16 * 16
    pad = np.zeros((th, tw), dtype=">f4")
    pad[:h, :w] = values
    b = pad.view(np.uint8).reshape(th, tw, 4).transpose(0, 2, 1).reshape(th, 4 * tw)   # (bytes by significance)
    d = b.copy()
    d[:, 1:] = (b[:, 1:].astype(np.int16) - b[:, :-1].astype(np.int16)) & 0xFF
    data = zlib.compress(d.astype(np.uint8).tobytes(), 9)
    geokeys = [1, 1, 0, 4, 1024, 0, 1, 2, 1025, 0, 1, 2, 2048, 0, 1, 4326, 2054, 0, 1, 9102]
    entries = []   # (tag, type, values)
    entries.append((256, 3, [w]))
    entries.append((257, 3, [h]))
    entries.append((258, 3, [32]))
    entries.append((259, 3, [8]))
    entries.append((262, 3, [1]))
    entries.append((277, 3, [1]))
    entries.append((284, 3, [1]))
    entries.append((317, 3, [3]))
    entries.append((322, 3, [tw]))
    entries.append((323, 3, [th]))
    entries.append((324, 4, [0]))   # (patched below)
    entries.append((325, 4, [len(data)]))
    entries.append((339, 3, [3]))
    entries.append((33550, 12, [step_x, step_y, 0.0]))
    entries.append((33922, 12, [0.0, 0.0, 0.0, west, north, 0.0]))
    entries.append((34735, 3, geokeys))
    fmt = {3: "H", 4: "I", 12: "d"}
    size = {3: 2, 4: 4, 12: 8}
    ifd_at = 8
    extra_at = ifd_at + 2 + 12 * len(entries) + 4
    extra = bytearray()
    blob = []
    for tag, typ, vals in entries:
        raw = struct.pack("<" + fmt[typ] * len(vals), *vals)
        if len(raw) <= 4:
            blob.append(struct.pack("<HHI", tag, typ, len(vals)) + raw.ljust(4, b"\0"))
        else:
            blob.append(struct.pack("<HHII", tag, typ, len(vals), extra_at + len(extra)))
            extra += raw
            if len(extra) % 2:
                extra += b"\0"
    data_at = extra_at + len(extra)
    blob[10] = struct.pack("<HHII", 324, 4, 1, data_at)
    with open(path, "wb") as f:
        f.write(b"II" + struct.pack("<HI", 42, ifd_at))
        f.write(struct.pack("<H", len(entries)) + b"".join(blob) + struct.pack("<I", 0))
        f.write(bytes(extra))
        f.write(data)


def crop(src_path, dst_path, south, north):
    """The rows of a GLO-90 tile between two latitudes (inclusive of the north row), written with write_float_tiff."""
    from .tiff import open_tiff
    t = open_tiff(src_path)
    west, step_x, top, step_y = t.geo
    r0 = int(round((top - north) / step_y))
    r1 = int(round((top - south) / step_y))
    values = t.window(0, r0, t.width, r1 - r0)
    write_float_tiff(dst_path, values, west, top - r0 * step_y, step_x, step_y)
    return values.shape
