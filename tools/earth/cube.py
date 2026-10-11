"""Tangent-adjusted cube grid, including integer-owned seam/corner aprons.

ECEF axes: X at longitude 0, Y at longitude 90 E, Z north. Each tile has
256 cell-centred samples and a one-cell apron; its stored grid is 258 squared.
The apron is an alias of another face's integer sample, not an extrapolation.
"""
from dataclasses import dataclass
import math

SAMPLES, APRON, WIDTH = 256, 1, 258
FACES = ("px", "nx", "py", "ny", "pz", "nz")
BASES = {
    "px": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    "nx": ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
    "py": ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
    "ny": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    "pz": ((0, 0, 1), (0, 1, 0), (-1, 0, 0)),
    "nz": ((0, 0, -1), (0, 1, 0), (1, 0, 0)),
}
EARTH_RADIUS_M = 6371000.0
MAX_LEVEL = 16


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normalise(v):
    if len(v) != 3 or not all(math.isfinite(c) for c in v):
        raise ValueError("direction must contain three finite coordinates")
    length = math.hypot(*v)
    if length == 0:
        raise ValueError("zero direction")
    return tuple(c / length for c in v)


@dataclass(frozen=True, order=True)
class Tile:
    face: str
    level: int
    x: int
    y: int

    def __post_init__(self):
        if self.face not in FACES:
            raise ValueError("unknown cube face")
        if type(self.level) is not int or not 0 <= self.level <= MAX_LEVEL:
            raise ValueError("unsupported tile level")
        if any(type(v) is not int or not 0 <= v < (1 << self.level)
               for v in (self.x, self.y)):
            raise ValueError("tile index outside level")

    def as_dict(self):
        return {"face": self.face, "level": self.level, "x": self.x, "y": self.y}

    @property
    def key(self):
        return f"{self.face}/{self.level}/{self.x}_{self.y}"


def face_uv_to_direction(face, u, v):
    if face not in BASES or not all(math.isfinite(x) for x in (u, v)):
        raise ValueError("invalid face coordinate")
    if not -1 <= u <= 1 or not -1 <= v <= 1:
        raise ValueError("face coordinate outside [-1, 1]")
    n, a, b = BASES[face]
    p, q = math.tan(u * math.pi / 4), math.tan(v * math.pi / 4)
    return normalise(tuple(n[k] + p * a[k] + q * b[k] for k in range(3)))


def direction_to_face_uv(direction):
    d = normalise(direction)
    # Stable face priority defines ownership on exact edges/corners.
    face = max(FACES, key=lambda f: dot(d, BASES[f][0]))
    n, a, b = BASES[face]
    denominator = dot(d, n)
    u = math.atan2(dot(d, a), denominator) * 4 / math.pi
    v = math.atan2(dot(d, b), denominator) * 4 / math.pi
    return face, max(-1.0, min(1.0, u)), max(-1.0, min(1.0, v))


def latlon_to_direction(lat, lon):
    if not all(math.isfinite(v) for v in (lat, lon)) or not -90 <= lat <= 90 or not -180 <= lon <= 180:
        raise ValueError("invalid latitude or longitude")
    if abs(lat) == 90:
        return 0.0, 0.0, 1.0 if lat > 0 else -1.0
    if lon == 180:
        lon = -180
    p, q = math.radians(lat), math.radians(lon)
    direction = (math.cos(p) * math.cos(q), math.cos(p) * math.sin(q), math.sin(p))
    return tuple(0.0 if abs(v) < 1e-15 else v for v in direction)


def direction_to_latlon(direction):
    x, y, z = normalise(direction)
    return math.degrees(math.atan2(z, math.hypot(x, y))), math.degrees(math.atan2(y, x))


def tile_for_latlon(lat, lon, level):
    if type(level) is not int or not 0 <= level <= MAX_LEVEL:
        raise ValueError("unsupported tile level")
    face, u, v = direction_to_face_uv(latlon_to_direction(lat, lon))
    n = 1 << level
    return Tile(face, level, min(n - 1, int((u + 1) * n / 2)),
                min(n - 1, int((v + 1) * n / 2)))


def sample_location(tile, col, row):
    """Return (face, full_face_column, full_face_row), all interior addresses.

    At a double-outside cube corner choose the first incident face in FACES,
    then its corner interior sample. This explicit corner ownership gives all
    three incident faces the same corner ghost, independent of fold order.
    """
    if any(type(v) is not int or not -APRON <= v < SAMPLES + APRON for v in (col, row)):
        raise ValueError("sample outside one-cell apron")
    count = SAMPLES * (1 << tile.level)
    i, j = tile.x * SAMPLES + col, tile.y * SAMPLES + row
    face = tile.face
    outside_i, outside_j = not 0 <= i < count, not 0 <= j < count
    if not outside_i and not outside_j:
        return face, i, j
    n, a, b = BASES[face]
    si, sj = (-1 if i < 0 else 1), (-1 if j < 0 else 1)
    if outside_i and outside_j:
        corner = tuple(n[k] + si * a[k] + sj * b[k] for k in range(3))
        face = next(f for f in FACES if dot(corner, BASES[f][0]) == 1)
        _, ua, va = BASES[face]
        return face, (count - 1 if dot(corner, ua) > 0 else 0), (count - 1 if dot(corner, va) > 0 else 0)
    crossing_axis, sign = (a, si) if outside_i else (b, sj)
    next_normal = tuple(sign * c for c in crossing_axis)
    face = next(f for f in FACES if BASES[f][0] == next_normal)
    _, ua, va = BASES[face]
    variable_axis = b if outside_i else a
    variable_index = j if outside_i else i
    # The old normal fixes one coordinate to the new edge. The other is an
    # exact signed permutation of the old along-edge integer coordinate.
    indices = []
    for axis in (ua, va):
        fixed = dot(n, axis)
        indices.append((count - 1 if fixed > 0 else 0) if fixed else
                       (variable_index if dot(variable_axis, axis) > 0 else count - 1 - variable_index))
    return face, indices[0], indices[1]


def sample_latlon(tile, col, row):
    face, i, j = sample_location(tile, col, row)
    count = SAMPLES * (1 << tile.level)
    return direction_to_latlon(face_uv_to_direction(face, 2 * (i + .5) / count - 1,
                                                    2 * (j + .5) / count - 1))


def cube_area_weight(u, v):
    """Tangent-map spherical Jacobian, omitting the constant pi²/16."""
    a, b = math.tan(u * math.pi / 4), math.tan(v * math.pi / 4)
    return (1 + a * a) * (1 + b * b) / (1 + a * a + b * b) ** 1.5
