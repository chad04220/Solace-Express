"""Deterministic regular-grid sampling and bounded lossless elevation codec."""
import hashlib
import math
import mmap
from pathlib import Path
import re
import struct
import zlib

from .common import finite_number, read_bounded, strict_json
from .cube import (FACES, SAMPLES, WIDTH, Tile, cube_area_weight,
                   direction_to_latlon, face_uv_to_direction, sample_location)

MAGIC = b"SEARTH01"
# magic, version, face, level, x, y, width, height, offset metres,
# codec (1=zstd, 2=zlib), quantization metres (1), uncompressed bytes, SHA256.
HEADER = struct.Struct("<8sHBBIIHHiBBI32s")
RAW_BYTES = WIDTH * WIDTH * 2
MAX_TILE_BYTES = 2 * 1024 * 1024
CODECS = {"zstd": 1, "zlib": 2}


class Grid:
    """Regular geographic point samples, stored south to north.

    periodic grids must include a repeated +360-degree endpoint column.
    Missing/nonfinite samples are rejected; regional sampling never extrapolates.
    """
    def __init__(self, data):
        if not isinstance(data, dict) or (type(data.get("version")) is not int or data.get("version") != 1):
            raise ValueError("unsupported grid version")
        for key in ("lon_min", "lat_min", "lon_step", "lat_step"):
            setattr(self, key, finite_number(data[key], key))
        self.periodic = data.get("periodic", False)
        if type(self.periodic) is not bool or self.lon_step <= 0 or self.lat_step <= 0:
            raise ValueError("invalid grid spacing or periodic flag")
        rows = data["heights"]
        if not isinstance(rows, list) or len(rows) < 2 or not isinstance(rows[0], list) or len(rows[0]) < 2:
            raise ValueError("grid must be at least 2 by 2")
        self.height, self.width = len(rows), len(rows[0])
        if self.width * self.height > 16_000_000:
            raise ValueError("grid sample limit exceeded")
        for row in rows:
            if not isinstance(row, list) or len(row) != self.width:
                raise ValueError("ragged elevation grid")
            for value in row:
                if not -12000 <= finite_number(value, "elevation") <= 10000:
                    raise ValueError("elevation outside supported terrestrial range")
            if self.periodic and row[0] != row[-1]:
                raise ValueError("periodic endpoints disagree")
        self.rows = rows
        self._validate_extent()

    def _validate_extent(self):
        if not -90 <= self.lat_min <= 90 or self.lat_min + self.lat_step * (self.height - 1) > 90 + 1e-8:
            raise ValueError("grid latitude extent invalid")
        if self.periodic and abs(self.lon_step * (self.width - 1) - 360) > 1e-8:
            raise ValueError("periodic grid must span 360 degrees")
        if self.lon_step * (self.width - 1) > 360 + 1e-8:
            raise ValueError("grid longitude extent invalid")

    @classmethod
    def from_json(cls, path):
        return cls(strict_json(read_bounded(path, 512 * 1024 * 1024)))

    def _get(self, x, y):
        return self.rows[y][x]

    def sample(self, lat, lon):
        if not all(math.isfinite(v) for v in (lat, lon)) or not -90 <= lat <= 90:
            raise ValueError("invalid sampling coordinate")
        if self.periodic:
            lon = (lon - self.lon_min) % 360 + self.lon_min
        else:
            # Regional grids may span the antimeridian; choose the equivalent
            # longitude nearest their centre without silently clamping.
            centre = self.lon_min + self.lon_step * (self.width - 1) / 2
            lon += 360 * math.floor((centre - lon + 180) / 360)
        x, y = (lon - self.lon_min) / self.lon_step, (lat - self.lat_min) / self.lat_step
        if getattr(self, "global_latitude", False) and -.5 - 1e-8 <= y <= self.height - .5 + 1e-8:
            y = max(0, min(self.height - 1, y))
        if not -1e-8 <= x <= self.width - 1 + 1e-8 or not -1e-8 <= y <= self.height - 1 + 1e-8:
            raise ValueError("elevation sample outside source coverage")
        x, y = max(0, min(self.width - 1, x)), max(0, min(self.height - 1, y))
        i, j = min(self.width - 2, int(x)), min(self.height - 2, int(y))
        fx, fy = x - i, y - j
        a = self._get(i, j) * (1 - fx) + self._get(i + 1, j) * fx
        b = self._get(i, j + 1) * (1 - fx) + self._get(i + 1, j + 1) * fx
        return a * (1 - fy) + b * fy

    def close(self):
        pass


class EnviGrid(Grid):
    """Read a north-up WGS84, Float32, single-band ENVI file using mmap.

    Prepare with gdal_translate -of ENVI -ot Float32 -co INTERLEAVE=BSQ.
    The binary and .hdr are local inputs, never committed. No GDAL dependency
    is needed at build time; arbitrary projections or nodata fail closed.
    """
    def __init__(self, path):
        path = Path(path)
        header = path.with_suffix(".hdr")
        if not header.exists():
            header = Path(str(path) + ".hdr")
        text = read_bounded(header, 64 * 1024).decode("ascii")
        fields = dict((key.strip().lower(), value.strip()) for key, value in
                      re.findall(r"([^\n=]+)=\s*(\{[^}]*\}|[^\n]*)", text))
        def integer(key, default=None):
            return int(fields[key] if key in fields else default)
        self.width, self.height = integer("samples"), integer("lines")
        if not 2 <= self.width <= 200000 or not 2 <= self.height <= 100000:
            raise ValueError("unsupported ENVI dimensions")
        if integer("bands") != 1 or integer("data type") != 4 or fields.get("interleave", "").lower() != "bsq":
            raise ValueError("ENVI must be single-band Float32 BSQ")
        info = [s.strip() for s in fields.get("map info", "").strip("{}").split(",")]
        if len(info) < 8 or info[0].lower() != "geographic lat/lon" or "wgs-84" not in info[7].lower():
            raise ValueError("ENVI must use geographic WGS-84")
        refx, refy, ulx, uly, dx, dy = map(float, info[1:7])
        if refx != 1 or refy != 1 or dx <= 0 or dy <= 0 or "rotation" in fields.get("map info", "").lower():
            raise ValueError("unsupported ENVI transform")
        self.lon_min, self.lon_step = ulx + dx / 2, dx
        self.lat_min, self.lat_step = uly - dy * (self.height - .5), dy
        self.periodic = abs(dx * self.width - 360) < 1e-7
        self.storage_width = self.width
        self.global_latitude = self.periodic and abs(uly - 90) < 1e-7 and abs(uly - dy * self.height + 90) < 1e-7
        if self.periodic:
            self.width += 1  # Virtual repeated endpoint for centre-aligned global rasters.
        self.offset = integer("header offset", 0)
        if self.offset < 0 or self.offset > 1024 * 1024:
            raise ValueError("invalid ENVI header offset")
        order = integer("byte order")
        if order not in (0, 1):
            raise ValueError("unsupported ENVI byte order")
        self.item = struct.Struct("<f" if order == 0 else ">f")
        self.nodata = float(fields.get("data ignore value", "nan"))
        if path.is_symlink() or not path.is_file() or path.stat().st_size != self.offset + self.storage_width * self.height * 4:
            raise ValueError("ENVI binary size mismatch")
        self._validate_extent()
        self.file = path.open("rb")
        self.mapped = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)

    def _get(self, x, y):
        x %= self.storage_width
        value = self.item.unpack_from(self.mapped, self.offset + ((self.height - 1 - y) * self.storage_width + x) * 4)[0]
        if not math.isfinite(value) or value == self.nodata or not -12000 <= value <= 10000:
            raise ValueError("missing or invalid elevation sample")
        return value

    def close(self):
        self.mapped.close()
        self.file.close()


def round_metre(value):
    finite_number(value, "elevation")
    if not -12000 <= value <= 10000:
        raise ValueError("elevation outside supported terrestrial range")
    return math.floor(value + .5) if value >= 0 else math.ceil(value - .5)


def encode_tile(tile, heights, codec="zstd"):
    if codec not in CODECS:
        raise ValueError("unsupported elevation codec")
    values = [round_metre(v) for v in heights]
    if len(values) != WIDTH * WIDTH:
        raise ValueError("elevation tile must contain 258 by 258 samples")
    offset = (min(values) + max(values)) // 2
    residuals = [v - offset for v in values]
    if min(residuals) < -32768 or max(residuals) > 32767:
        raise ValueError("elevation residual does not fit int16")
    delta = []
    for start in range(0, len(values), WIDTH):
        previous = 0
        for value in residuals[start:start + WIDTH]:
            delta.append((value - previous) & 65535)
            previous = value
    raw = struct.pack("<" + str(len(delta)) + "H", *delta)
    if codec == "zstd":
        try:
            import zstandard
        except ImportError as exc:
            raise ValueError("zstd requires the optional zstandard Python package; use --codec zlib for fixtures") from exc
        compressed = zstandard.ZstdCompressor(level=9, threads=0, write_checksum=True,
                                              write_content_size=True).compress(raw)
    else:
        compressed = zlib.compress(raw, level=9)
    header = HEADER.pack(MAGIC, 1, FACES.index(tile.face), tile.level, tile.x, tile.y,
                         WIDTH, WIDTH, offset, CODECS[codec], 1, len(raw), hashlib.sha256(raw).digest())
    return header + compressed


def decode_tile(data):
    if not isinstance(data, bytes) or not HEADER.size < len(data) <= MAX_TILE_BYTES:
        raise ValueError("invalid elevation file size")
    magic, version, face, level, x, y, width, height, offset, codec, unit, length, digest = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1 or face >= len(FACES) or width != WIDTH or height != WIDTH or unit != 1 or length != RAW_BYTES:
        raise ValueError("invalid elevation header")
    tile = Tile(FACES[face], level, x, y)
    if not -12000 <= offset <= 10000:
        raise ValueError("invalid elevation offset")
    compressed = data[HEADER.size:]
    try:
        if codec == CODECS["zlib"]:
            decoder = zlib.decompressobj()
            raw = decoder.decompress(compressed, RAW_BYTES + 1)
            if len(raw) != RAW_BYTES or decoder.unconsumed_tail or decoder.unused_data or not decoder.eof:
                raise ValueError("invalid or oversized zlib frame")
        elif codec == CODECS["zstd"]:
            try:
                import zstandard
            except ImportError as exc:
                raise ValueError("zstd decoding requires the optional zstandard package") from exc
            parameters = zstandard.get_frame_parameters(compressed)
            if parameters.content_size != RAW_BYTES or parameters.window_size > RAW_BYTES or parameters.dict_id != 0:
                raise ValueError("invalid or oversized zstd frame")
            raw = zstandard.ZstdDecompressor(max_window_size=1024).decompress(
                compressed, max_output_size=RAW_BYTES, allow_extra_data=False)
            if len(raw) != RAW_BYTES:
                raise ValueError("zstd decoded length mismatch")
        else:
            raise ValueError("unsupported elevation codec")
    except Exception as exc:
        if isinstance(exc, (KeyboardInterrupt, SystemExit)):
            raise
        raise ValueError("invalid compressed elevation: " + str(exc)) from exc
    if hashlib.sha256(raw).digest() != digest:
        raise ValueError("elevation raw SHA256 mismatch")
    deltas = struct.unpack("<" + str(WIDTH * WIDTH) + "H", raw)
    values = []
    previous = 0
    for index, delta in enumerate(deltas):
        if index % WIDTH == 0:
            previous = 0
        unsigned = (previous + delta) & 65535
        previous = unsigned if unsigned < 32768 else unsigned - 65536
        value = offset + previous
        if not -12000 <= value <= 10000:
            raise ValueError("decoded elevation outside terrestrial range")
        values.append(value)
    return {"tile": tile, "offset_m": offset, "heights": values, "codec": next(k for k, v in CODECS.items() if v == codec)}


def build_elevation(tile, source, area_samples=1, codec="zstd"):
    """Sample bilinear values; N>1 adds Jacobian-weighted N×N quadrature.

    N=1 samples the centre. N>1 integrates bilinear source samples over the
    target pixel's face-coordinate footprint using spherical area weights.
    Select N from the source/target spacing (documented CLI default: 2).
    This is bounded midpoint quadrature, not an exact raster-cell integral.
    """
    if type(area_samples) is not int or not 1 <= area_samples <= 16:
        raise ValueError("area_samples must be an integer in [1, 16]")
    count = SAMPLES * (1 << tile.level)
    result = []
    for row in range(-1, SAMPLES + 1):
        for col in range(-1, SAMPLES + 1):
            face, i, j = sample_location(tile, col, row)
            total = weight_sum = 0.0
            for sy in range(area_samples):
                v = 2 * (j + (sy + .5) / area_samples) / count - 1
                for sx in range(area_samples):
                    u = 2 * (i + (sx + .5) / area_samples) / count - 1
                    lat, lon = direction_to_latlon(face_uv_to_direction(face, u, v))
                    weight = cube_area_weight(u, v) if area_samples > 1 else 1.0
                    total += source.sample(lat, lon) * weight
                    weight_sum += weight
            result.append(total / weight_sum)
    return encode_tile(tile, result, codec)
