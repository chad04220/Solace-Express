"""Explicit optional, fixed-batch ENVI acceleration; no pack-format changes.

Arrays cover at most BATCH_CELLS, never (whole world × N × N). The Float32
source is a read-only zero-copy view of EnviGrid's mmap, including byte order
and header offset. Scalar sampling remains available without this dependency.
"""
import math

import numpy as np

from .cube import (BASES, FACES, SAMPLES, WIDTH, direction_to_latlon,
                   face_uv_to_direction, sample_location)
from .elevation import EnviGrid, sample_cell

BATCH_CELLS = 4096
# Deliberately conservative cushion for double-precision geometry differences.
# This is not a promise that different platforms/libm versions are identical.
COORDINATE_CUSHION_DEGREES = 1e-10


def _locations(tile, start, stop):
    linear = np.arange(start, stop, dtype=np.int64)
    col, row = linear % WIDTH - 1, linear // WIDTH - 1
    i, j = col + tile.x * SAMPLES, row + tile.y * SAMPLES
    face = np.full(len(linear), FACES.index(tile.face), dtype=np.int8)
    count = SAMPLES << tile.level
    outside = (i < 0) | (i >= count) | (j < 0) | (j >= count)
    # Interior addresses need no Python call. Delegate all seams and corners to
    # the established integer ownership contract instead of duplicating it.
    for index in np.flatnonzero(outside):
        owner, x, y = sample_location(tile, int(col[index]), int(row[index]))
        face[index], i[index], j[index] = FACES.index(owner), x, y
    return col, row, face, i, j


def _bilinear(source, raster, lat, lon, coordinate_reference=None):
    if not np.all(np.isfinite(lat) & np.isfinite(lon) & (lat >= -90) & (lat <= 90)):
        raise ValueError("invalid sampling coordinate")
    original_lon = lon
    if source.periodic:
        lon = (lon - source.lon_min) % 360 + source.lon_min
    else:
        centre = source.lon_min + source.lon_step * (source.width - 1) / 2
        lon = lon + 360 * np.floor((centre - lon + 180) / 360)
    x = (lon - source.lon_min) / source.lon_step
    y = (lat - source.lat_min) / source.lat_step
    if coordinate_reference is not None:
        # An ulp can change floor(x/y) at a source-cell boundary. Even a
        # zero-weight corner is validated by the scalar reader, so gathering
        # the other stencil could either reject valid input or miss nodata.
        # Evaluate these subpixel queries through the complete scalar path
        # BEFORE any array gather or coverage rejection.
        eps_x = COORDINATE_CUSHION_DEGREES / source.lon_step + 1e-10
        eps_y = COORDINATE_CUSHION_DEGREES / source.lat_step + 1e-10
        near = ((np.abs(x - np.rint(x)) <= eps_x) |
                (np.abs(y - np.rint(y)) <= eps_y))
        for edge in (-1e-8, source.width - 1 + 1e-8):
            near |= np.abs(x - edge) <= eps_x
        y_edges = [-1e-8, source.height - 1 + 1e-8]
        if source.global_latitude:
            y_edges += [-.5 - 1e-8, source.height - .5 + 1e-8]
        for edge in y_edges:
            near |= np.abs(y - edge) <= eps_y
        if np.any(near):
            result = np.empty(len(lat), dtype=np.float64)
            regular = ~near
            if np.any(regular):
                result[regular] = _bilinear(source, raster, lat[regular], original_lon[regular])
            for index in np.flatnonzero(near):
                result[index] = source.sample(*coordinate_reference(int(index)))
            return result
    if source.global_latitude:
        cap = (y >= -.5 - 1e-8) & (y <= source.height - .5 + 1e-8)
        y = np.where(cap, np.clip(y, 0, source.height - 1), y)
    if not np.all((x >= -1e-8) & (x <= source.width - 1 + 1e-8) &
                  (y >= -1e-8) & (y <= source.height - 1 + 1e-8)):
        raise ValueError("elevation sample outside source coverage")
    x, y = np.clip(x, 0, source.width - 1), np.clip(y, 0, source.height - 1)
    i = np.minimum(source.width - 2, x.astype(np.int64))
    j = np.minimum(source.height - 2, y.astype(np.int64))
    fx, fy = x - i, y - j
    # Force Float64 BEFORE interpolation, exactly as struct.unpack returns
    # Python doubles in the scalar reader. Validate even zero-weight corners.
    a = raster[source.height - 1 - j, i % source.storage_width].astype(np.float64)
    b = raster[source.height - 1 - j, (i + 1) % source.storage_width].astype(np.float64)
    c = raster[source.height - 2 - j, i % source.storage_width].astype(np.float64)
    d = raster[source.height - 2 - j, (i + 1) % source.storage_width].astype(np.float64)
    for values in (a, b, c, d):
        if not np.all(np.isfinite(values) & (values != source.nodata) &
                      (values >= -12000) & (values <= 10000)):
            raise ValueError("missing or invalid elevation sample")
    upper = a * (1 - fx) + b * fx
    lower = c * (1 - fx) + d * fx
    return upper * (1 - fy) + lower * fy


def _quadrature(source, raster, face, i, j, count, area_samples):
    bases = np.asarray([BASES[f] for f in FACES], dtype=np.float64)[face]
    n, a, b = bases[:, 0], bases[:, 1], bases[:, 2]
    total = np.zeros(len(i), dtype=np.float64)
    weight_sum = np.zeros(len(i), dtype=np.float64)
    # Preserve the scalar sy/sx summation order. Never reduce a large N² axis
    # or silently choose fewer subpixels for faster/high-level sampling.
    for sy in range(area_samples):
        v = 2 * (j + (sy + .5) / area_samples) / count - 1
        q = np.tan(v * math.pi / 4)
        for sx in range(area_samples):
            u = 2 * (i + (sx + .5) / area_samples) / count - 1
            p = np.tan(u * math.pi / 4)
            x = n[:, 0] + p * a[:, 0] + q * b[:, 0]
            y = n[:, 1] + p * a[:, 1] + q * b[:, 1]
            z = n[:, 2] + p * a[:, 2] + q * b[:, 2]
            # atan2 ratios are invariant under the reference's normalization.
            lat = np.arctan2(z, np.hypot(x, y)) * (180 / math.pi)
            lon = np.arctan2(y, x) * (180 / math.pi)
            weight = ((1 + p * p) * (1 + q * q) /
                      (1 + p * p + q * q) ** 1.5) if area_samples > 1 else 1.0
            def coordinate_reference(index):
                ru = 2 * (int(i[index]) + (sx + .5) / area_samples) / count - 1
                rv = 2 * (int(j[index]) + (sy + .5) / area_samples) / count - 1
                return direction_to_latlon(face_uv_to_direction(FACES[face[index]], ru, rv))
            total += _bilinear(source, raster, lat, lon, coordinate_reference) * weight
            weight_sum += weight
    return total / weight_sum


def sample_envi(tile, source, area_samples):
    if type(source) is not EnviGrid:
        raise ValueError("numpy sampling requires an ENVI source; use --sampler scalar")
    raster = np.ndarray((source.height, source.storage_width),
                        dtype=source.item.format, buffer=source.mapped,
                        offset=source.offset)
    # 22 km is the entire supported height span. Scale the cushion by source
    # spacing so very fine regional grids conservatively recompute more cells.
    guard_m = (22000 * COORDINATE_CUSHION_DEGREES *
               (1 / source.lon_step + 1 / source.lat_step) + 1e-7)
    result = []
    for start in range(0, WIDTH * WIDTH, BATCH_CELLS):
        stop = min(start + BATCH_CELLS, WIDTH * WIDTH)
        col, row, face, i, j = _locations(tile, start, stop)
        values = _quadrature(source, raster, face, i, j,
                             SAMPLES << tile.level, area_samples)
        # libm/vector ufuncs can differ by a few ulps. Retain the scalar result
        # around half-metre rounding ties (and terrestrial range endpoints),
        # rather than allowing an insignificant floating difference to change
        # the encoded integer. This is separate from quadrature accuracy.
        near_tie = np.abs(values - (np.floor(values) + .5)) <= guard_m
        near_limit = (np.abs(values + 12000) <= guard_m) | (np.abs(values - 10000) <= guard_m)
        for index in np.flatnonzero(near_tie | near_limit):
            values[index] = sample_cell(tile, source, int(col[index]), int(row[index]), area_samples)
        result.extend(values.tolist())
    return result
