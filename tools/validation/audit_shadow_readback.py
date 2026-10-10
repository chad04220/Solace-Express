#!/usr/bin/env python3
"""Reproduce an isolated, unit-scale scenery caster against actual GPU readback.

This is an opt-in diagnostic, not a render acceptance test. Requires NumPy.
Inputs come from ASSET_REVIEW_SHADOW_DUMP=1 and export_shadow_fixture_mesh.cpp.
No production source, graphics state, or input files are modified.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def raster(vertices, matrix, size, origin):
    """CPU triangle raster with production polygon offset factor 1.5, units 2.

    Floating-depth units use the triangle maximum-depth float32 exponent.
    This matches the captured Mesa path within normal raster precision.
    """
    world = vertices[:, :, :3].astype(np.float64) + origin
    q = (world @ matrix[:3, :3].T + matrix[:3, 3]) * .5 + .5
    if q[:, :, 2].min() <= 0 or q[:, :, 2].max() >= 1:
        raise ValueError("clipped light-depth triangles are unsupported")
    q[:, :, :2] *= size
    depth = np.ones((size, size))
    parts = np.full((size, size), -1, dtype=np.int16)
    biases = np.zeros((size, size))
    for index, tri in enumerate(q):
        lo = np.maximum(np.floor(tri[:, :2].min(0)).astype(int), 0)
        hi = np.minimum(np.ceil(tri[:, :2].max(0)).astype(int), size)
        if np.any(lo >= hi):
            continue
        a = np.column_stack((tri[:, :2], np.ones(3)))
        if abs(np.linalg.det(a)) < 1e-9:
            continue
        inverse = np.linalg.inv(a)
        yy, xx = np.mgrid[lo[1]:hi[1], lo[0]:hi[0]]
        pixels = np.stack((xx + .5, yy + .5, np.ones_like(xx)), -1)
        inside = (pixels @ inverse).min(-1) >= -1e-7
        plane = inverse @ tri[:, 2]
        unit = 2. ** (np.floor(np.log2(tri[:, 2].max())) - 23)
        bias = 1.5 * max(abs(plane[0]), abs(plane[1])) + 2 * unit
        candidate = pixels @ plane + bias
        view = np.s_[lo[1]:hi[1], lo[0]:hi[0]]
        hit = inside & (candidate < depth[view])
        depth[view][hit] = candidate[hit]
        parts[view][hit] = int(vertices[index, 0, 6])
        biases[view][hit] = bias * 6000
    return depth, parts, biases


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("prefix", type=Path, help="capture prefix, e.g. kind_13_front_oblique")
    ap.add_argument("--mesh", required=True, type=Path, help="raw EVert float32 mesh export")
    ap.add_argument("--origin", nargs=3, type=float, default=[-577., 20., -4413.])
    ap.add_argument("--output", type=Path, help="optional small JSON report")
    args = ap.parse_args()
    meta_path = Path(str(args.prefix) + "_shadow_metadata.json")
    depth_path = Path(str(args.prefix) + "_shadow_near.f32")
    meta = json.loads(meta_path.read_text())
    size = meta["shadow_resolution"]
    actual = np.fromfile(depth_path, dtype="<f4").reshape(size, size)
    vertices = np.fromfile(args.mesh, dtype="<f4").reshape(-1, 3, 10)
    if not np.isfinite(actual).all() or not np.isfinite(vertices).all():
        raise ValueError("non-finite capture or mesh")
    matrix = np.array(meta["shadow_matrices_column_major_near_far"][:16]).reshape(4, 4, order="F")
    predicted, parts, biases = raster(vertices, matrix, size, np.array(args.origin))
    common = (predicted < 1) & (actual < 1)
    if not common.any():
        raise ValueError("no common occupied texels")
    error = abs(predicted[common] - actual[common]) * 6000
    report = {
        "input_sha256": {str(p): sha(p) for p in (meta_path, depth_path, args.mesh)},
        "origin": args.origin,
        "resolution": size,
        "near_radius_m": meta["shadow_radii"][0],
        "texel_width_m": 2 * meta["shadow_radii"][0] / size,
        "actual_occupied_texels": int((actual < 1).sum()),
        "predicted_occupied_texels": int((predicted < 1).sum()),
        "coverage_mismatches": int(((predicted < 1) != (actual < 1)).sum()),
        "depth_error_m": {"mean": float(error.mean()), "p99": float(np.quantile(error, .99)), "max": float(error.max())},
        "parts": [],
        "limitations": "Unit-scale, yaw-zero isolated fixtures only; no foliage deformation or alpha-cutouts. Exact coplanar tie/raster fill rules may differ. Successful depth reproduction is not visual acceptance.",
    }
    for part in np.unique(parts[common]):
        mask = common & (parts == part)
        report["parts"].append({"part": int(part), "texels": int(mask.sum()),
                                "caster_bias_m": [float(biases[mask].min()), float(biases[mask].max())]})
    encoded = json.dumps(report, indent=2) + "\n"
    print(encoded, end="")
    if args.output:
        args.output.write_text(encoded)


if __name__ == "__main__":
    main()
