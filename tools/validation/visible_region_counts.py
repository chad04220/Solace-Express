#!/usr/bin/env python3
"""Count potential island-region visibility without generating any terrain.

Requires numpy and scipy. Run from any directory, for example:
  python3 tools/validation/visible_region_counts.py --output /tmp/visibility.json

This is a read-only geometry experiment, not a world/asset/pack generator.
The enumeration uses the canonical tangent-cube mapping from tools/earth/cube.py.
Percentiles use area-uniform Fibonacci camera positions; sampled maxima also
include every region centre, grid edge midpoint and vertex, and three arbitrary
offsets per cell. A sampled maximum is not a proven global maximum.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.spatial import cKDTree

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "earth"))
import cube  # noqa: E402

RADIUS_KM = 6371.0
LEVEL = 5
SIDE = 1 << LEVEL
OCEAN_EDGE_KM = 38.0
SOLACE_POSITION = (32.5, -58.0)
SOLACE_RESERVATION_KM = 150.0
SOLACE_ENVELOPE_KM = 71.0
DEFAULT_ALTITUDES_KM = (12.0, 13.716, 14.0, 15.545, 16.0, 18.0, 20.0, 25.0, 26.0, 28.0, 30.0)


def horizon_angle(height_km):
    return math.acos(RADIUS_KM / (RADIUS_KM + height_km))


def cell_samples(dx, dy):
    return np.asarray([
        cube.face_uv_to_direction(face, 2 * (x + dx) / SIDE - 1, 2 * (y + dy) / SIDE - 1)
        for face in cube.FACES for y in range(SIDE) for x in range(SIDE)
    ], dtype=np.float64)


def unique(points):
    # Only deduplicate exact geometric aliases at cube/grid seams. The rounded
    # keys select original full-precision vectors; they do not change vectors.
    _, indices = np.unique(np.round(points, 12), axis=0, return_index=True)
    return points[np.sort(indices)]


def canonical_centres():
    raw = cell_samples(0.5, 0.5)
    solace = np.asarray(cube.latlon_to_direction(*SOLACE_POSITION))
    separation = np.arccos(np.clip(raw @ solace, -1.0, 1.0)) * RADIUS_KM
    reserved = separation < SOLACE_RESERVATION_KM
    centres = np.vstack((raw[~reserved], solace))
    ids = [f"pr1-{f}-{x}-{y}" for f in range(6) for y in range(SIDE) for x in range(SIDE)]
    reserved_ids = [ident for ident, reserve in zip(ids, reserved) if reserve]
    return centres, raw, solace, reserved_ids


def viewpoints(centres, raw, uniform_count):
    edges = unique(np.vstack([cell_samples(*uv) for uv in ((0, .5), (1, .5), (.5, 0), (.5, 1))]))
    vertices = unique(np.asarray([
        cube.face_uv_to_direction(face, 2*x/SIDE-1, 2*y/SIDE-1)
        for face in cube.FACES for y in range(SIDE+1) for x in range(SIDE+1)
    ]))
    offsets = np.vstack([cell_samples(*uv) for uv in ((.17, .29), (.61, .83), (.89, .11))])
    # These are deterministic area-uniform positions, not terrain raster cells.
    k = np.arange(uniform_count)
    z = 1 - 2 * (k + .5) / uniform_count
    longitude = k * math.pi * (3 - math.sqrt(5))
    r = np.sqrt(1-z*z)
    uniform = np.column_stack((r*np.cos(longitude), r*np.sin(longitude), z))
    special_latlon = [SOLACE_POSITION, (90, 0), (-90, 0), (0, 180), (0, -180),
                      (45, 180), (-45, -180), (0, 0), (0, 90), (0, -90),
                      (32.5, -57.0), (33.0, -58.0)]
    special = np.asarray([cube.latlon_to_direction(*p) for p in special_latlon])
    structured = unique(np.vstack((centres, raw, edges, vertices, offsets, special)))
    return uniform, structured, {
        "area_uniform_fibonacci": len(uniform),
        "canonical_centres": len(centres),
        "raw_grid_centres": len(raw),
        "unique_grid_edge_midpoints": len(edges),
        "unique_grid_vertices_including_cube_corners": len(vertices),
        "arbitrary_offsets_per_cell": 3,
        "arbitrary_offset_samples": len(offsets),
        "special_poles_dateline_solace_samples_before_deduplication": len(special),
        "structured_total_after_deduplication": len(structured),
        "all_samples": len(uniform)+len(structured),
    }


def stats(counts):
    values, frequencies = np.unique(counts, return_counts=True)
    return {
        "min": int(counts.min()), "median": float(np.median(counts)),
        "p95": int(np.percentile(counts, 95, method="higher")),
        "sampled_max": int(counts.max()), "mean": float(counts.mean()),
        "histogram": {str(int(v)): int(n) for v, n in zip(values, frequencies)},
    }


def counts_at(tree, points, solace_distance, horizon_km, envelope_km, workers):
    radius = 2*math.sin((horizon_km+envelope_km)/(2*RADIUS_KM))
    counts = tree.query_ball_point(points, radius, return_length=True, workers=workers)
    if envelope_km:
        # The tree includes Solace exactly once. Replace its generated-region
        # envelope with the separately stated authored-region assumption.
        counts += (solace_distance <= horizon_km+SOLACE_ENVELOPE_KM).astype(np.int64)
        counts -= (solace_distance <= horizon_km+envelope_km).astype(np.int64)
    return counts


def minimum_separation_km(centres):
    # Every centre's exact nearest neighbour, without quadratic storage.
    distance, _ = cKDTree(centres).query(centres, k=2)
    return 2*RADIUS_KM*math.asin(float(distance[:, 1].min())/2)


def loose_packing_bound(horizon_km, envelope_km, separation_km):
    # Pairwise-disjoint spherical caps of radius separation/2 fit inside the
    # search cap enlarged by separation/2. Count authored Solace separately.
    # Round the measured minimum DOWN one metre for numerical conservatism.
    separation = math.floor(separation_km*1000)/1000
    cap_radius = separation/2
    numerator = math.sin((horizon_km+envelope_km+cap_radius)/(2*RADIUS_KM))**2
    denominator = math.sin(cap_radius/(2*RADIUS_KM))**2
    return math.floor(numerator/denominator+1e-9)+1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--uniform-points", type=int, default=65536)
    parser.add_argument("--altitudes-km", nargs="+", type=float, default=DEFAULT_ALTITUDES_KM)
    parser.add_argument("--summits-km", nargs="+", type=float, default=(0.0, 1.0, 3.0))
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    if args.uniform_points < 1 or any(h < 0 or not math.isfinite(h) for h in (*args.altitudes_km, *args.summits_km)):
        parser.error("sample count must be positive; altitudes must be finite and nonnegative")
    started = time.monotonic()
    centres, raw, solace, reserved = canonical_centres()
    uniform, structured, sample_info = viewpoints(centres, raw, args.uniform_points)
    points = np.vstack((uniform, structured))
    solace_distance = np.arccos(np.clip(points@solace, -1, 1))*RADIUS_KM
    tree = cKDTree(centres)
    minimum_generated_separation = minimum_separation_km(centres[:-1])
    # Independently cross-check KD-tree query counts with brute-force great-
    # circle distance at 32 spread-out viewpoints in every tested scenario.
    check_indices = np.linspace(0, len(points)-1, 32, dtype=int)
    check_distances = np.arccos(np.clip(points[check_indices]@centres.T, -1, 1))*RADIUS_KM
    cross_checks = 0
    report = {
        "schema_version": 1,
        "purpose": "Potential 360-degree above-horizon region envelopes, without terrain generation.",
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (ROOT/"src/procedural_regions.cpp", ROOT/"src/procedural_regions.h",
                                    ROOT/"src/geo.cpp", ROOT/"src/terrain_mesh.cpp", ROOT/"tools/earth/cube.py")},
        "earth_radius_km": RADIUS_KM,
        "address_level": LEVEL,
        "raw_grid_cells": len(raw),
        "reserved_generated_ids_replaced_by_one_solace": reserved,
        "unique_region_count": len(centres),
        "minimum_generated_centre_separation_km": minimum_generated_separation,
        "minimum_canonical_centre_separation_including_solace_km": minimum_separation_km(centres),
        "samples": sample_info,
        "formula": "R*(acos(R/(R+camera_height)) + acos(R/(R+terrain_height))); intersect envelope if centre surface distance <= horizon arc + envelope radius.",
        "assumptions": [
            "Camera heights and hypothetical summit heights are above sea level; spherical Earth, no atmospheric refraction.",
            "Summit scenarios put the same maximum height in every region. This is a conservative sensitivity study, not measured peak distribution.",
            "38 km is the recipe square half-width, NOT a conservative land bounding-disk radius. Its circumscribed radius is sqrt(2)*38 km.",
            "The circumscribed disk bounds analytic recipe land, without asserting a bound for raster interpolation or separate scene objects.",
            "Solace uses a separately assumed 71 km envelope based on the authored override extent, not a measured coastline.",
            "The 71 km position override does not create additional regions; one generated centre within 150 km is replaced by Solace.",
            "Centre-only counts do not mean the centre has dry land. Disk-intersection counts can include envelopes whose actual islands are below the horizon.",
            "No fog, clouds, local terrain occlusion, visual resolution, view frustum, or screen-space size test is applied.",
            "Area-uniform samples supply typical percentiles. Structured samples additionally probe centres, seams and offsets for maxima.",
            "Sampled maxima are not mathematically proven all-position maxima; a production cache needs headroom and eviction by geometric bounds.",
            "A separate loose all-position cap-packing bound uses all generated centres' minimum separation and adds one for Solace. It is much looser than observed maxima and is not a predicted typical count.",
            "These are fictional procedural island regions positioned on Earth, not real coastline data.",
        ],
        "cheap_source_height_bound": {
            "procedural_recipe_base_plus_detail_upper_bound_m": 2560,
            "derivation": "Volcanic base <=30+2400; other bases are lower; amplitude<=65 and abs(fbm)<2, giving <=2560 m. This is an upper bound, not an attained/measured summit. Authored Solace and downstream world edits are excluded.",
        },
        "memory": {
            "far_grid_side": 129,
            "far_vertex_bytes": 40,
            "far_all_triangles_bytes_per_region": 128*128*6*40,
            "far_all_triangles_mib_per_region": 3.75,
            "measured_example_world_owned_capacity_bytes": 140864265,
            "measured_example_source": "procedural-region-evidence/volcanic-world-rss.txt",
            "scope": "Far mesh number is maximum GPU triangle payload (underwater triangles omitted in practice), not CPU+GPU RSS. Full World number excludes transient allocations, GPU and process resources. Active and candidate Worlds remain additional costs.",
            "budgets": [{"coarse_group_slots": n, "worst_gpu_mib": n*3.75,
                         "naive_full_worlds_gb": n*140864265/1e9} for n in (12, 16, 24, 32, 36, 48)],
        },
        "scenarios": [],
    }
    envelopes = {"centre_only": 0.0, "disk_38km_sensitivity": OCEAN_EDGE_KM,
                 "recipe_square_circumscribed_disk": math.sqrt(2)*OCEAN_EDGE_KM}
    for altitude in args.altitudes_km:
        for summit in args.summits_km:
            horizon_km = RADIUS_KM*(horizon_angle(altitude)+horizon_angle(summit))
            result = {"camera_altitude_km": altitude, "terrain_summit_km": summit,
                      "sea_level_horizon_arc_km": RADIUS_KM*horizon_angle(altitude),
                      "camera_plus_terrain_horizon_arc_km": horizon_km, "envelopes": {}}
            for name, envelope in envelopes.items():
                counts = counts_at(tree, points, solace_distance, horizon_km, envelope, args.workers)
                limits = np.full(len(centres), horizon_km+envelope)
                if envelope:
                    limits[-1] = horizon_km+SOLACE_ENVELOPE_KM
                expected = np.count_nonzero(check_distances <= limits[None, :], axis=1)
                if not np.array_equal(counts[check_indices], expected):
                    raise AssertionError(f"KD-tree/brute-force disagreement: {altitude}, {summit}, {name}")
                cross_checks += len(check_indices)
                at = int(np.argmax(counts))
                result["envelopes"][name] = {
                    "generated_envelope_radius_km": envelope,
                    "solace_envelope_radius_km": SOLACE_ENVELOPE_KM if envelope else 0.0,
                    "area_uniform": stats(counts[:len(uniform)]),
                    "structured": stats(counts[len(uniform):]),
                    "all_samples_max": int(counts.max()),
                    "loose_all_position_packing_upper_bound": loose_packing_bound(horizon_km, envelope, minimum_generated_separation),
                    "one_max_viewpoint_lat_lon_degrees": list(cube.direction_to_latlon(points[at])),
                }
            report["scenarios"].append(result)
            summary = result["envelopes"]["recipe_square_circumscribed_disk"]
            typical = summary["area_uniform"]
            print(f"camera={altitude:g}km summit={summit:g}km horizon={horizon_km:.1f}km "
                  f"square-envelope median={typical['median']:g} p95={typical['p95']} "
                  f"sampled-max={summary['all_samples_max']}", flush=True)
    report["runtime_seconds"] = time.monotonic()-started
    report["validation"] = {"brute_force_great_circle_count_cross_checks_passed": cross_checks,
                            "terrain_height_samples_generated": 0,
                            "worlds_materialized": 0}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print(f"Wrote {args.output}; {len(points):,} viewpoints, {len(centres):,} region centres, "
          f"zero terrain samples; {report['runtime_seconds']:.2f}s.")


if __name__ == "__main__":
    main()
