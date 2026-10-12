#!/usr/bin/env python3
"""Conditional readiness margins, not a proof of real collision continuity.

Uses measured CPU time and observed upload-frame count, plus conservative region
spacing and effective geographic speed (ground speed times time acceleration).
No synthetic scheduler displacement is treated as real aircraft flight.
"""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spacing', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu-seconds', type=float, default=23.045)
    parser.add_argument('--upload-frames', type=int, default=70)
    parser.add_argument('--cancel-seconds', type=float, default=None,
                        help='Measured cancellation retirement bound, if available')
    args = parser.parse_args()
    spacing = json.loads(args.spacing.read_text())
    generated_gap = spacing['conservative_island_ocean_gap_min_km']
    solace_gap = spacing['solace_conservative_ocean_gap_km']
    clearances = {
        'generated_departure_coast': generated_gap,
        'solace_departure_coast': solace_gap,
        'generated_owner_edge_lower_bound': spacing['minimum_owner_edge_land_clearance_km'],
        # Solace is not a regular Voronoi cell. This is a representative location,
        # explicitly not a lower bound on its actual ownership-boundary clearance.
        'solace_gap_midpoint_scenario': solace_gap / 2,
    }
    pipelines = {'uncancelled_obsolete_plus_replacement': 2 * args.cpu_seconds}
    if args.cancel_seconds is not None:
        pipelines['measured_cancellation_plus_replacement'] = args.cancel_seconds + args.cpu_seconds
    cases = []
    for pipeline, cpu in pipelines.items():
        for fps in (60, 30, 15, 4):
            ready = cpu + args.upload_frames / fps
            for ground_speed in (235, 260, 300, 420, 600, 1000):
                for time_acceleration in (1, 4):
                    effective_speed = ground_speed * time_acceleration
                    for location, km in clearances.items():
                        travel = effective_speed * ready / 1000
                        cases.append(dict(pipeline=pipeline, location=location, fps=fps,
                                          ground_speed_m_s=ground_speed, time_acceleration=time_acceleration,
                                          effective_geographic_speed_m_s=effective_speed,
                                          readiness_wall_seconds=ready,
                                          conservative_distance_km=km, travel_before_ready_km=travel,
                                          margin_km=km-travel, sufficient_under_assumptions=km>=travel,
                                          maximum_effective_speed_under_assumptions_m_s=1000*km/ready,
                                          maximum_ground_speed_under_assumptions_m_s=1000*km/ready/time_acceleration))
    report = {
        'schema_version': 3,
        'claim': 'Conditional geometry/timing margins only; no universal no-late-collision guarantee.',
        'inputs': {'cpu_seconds_observed': args.cpu_seconds,
                   'upload_frames_observed': args.upload_frames,
                   'cancellation_seconds_supplied': args.cancel_seconds,
                   'spacing_source': str(args.spacing)},
        'limitations': [
            'Observed generation times are not hard upper bounds under CPU contention or slow storage.',
            'Supply a measured integrated upload-frame count; scheduler contention and horizon sharing can change it.',
            'Solace midpoint is a scenario, not the clearance of its irregular ownership boundary.',
            'Conservative envelopes do not sample actual coasts; a negative margin disproves this bound, not an observed crash.',
            'A blocked/failed candidate can remain unready indefinitely. Accepted drawable coarse surfaces now provide paired collision until safe full-detail promotion; this arithmetic does not test that separate fallback.',
            'Normal Free Flight permits 4x cruise: 235-300 m/s ground speed consumes ocean clearance at 940-1200 geographic m/s per wall second.',
            '260 m/s represents XR-10 nominal cruise; 420 m/s is the XR-30 published cruise field; 600 m/s is an illustrative fast-jet bound, not a measured sustained-performance result.',
            '1000 m/s physical speed is synthetic scheduler stress; similar effective geographic speed is reachable through normal cruise time acceleration.',
            'Actual Game tests separately cover 4x continued physics, gated-future coarse contact, low/contact pinning and safe detail promotion; this arithmetic alone establishes none of those behaviors.',
        ],
        'cases': cases,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    for row in cases:
        if row['time_acceleration']==4 and row['ground_speed_m_s'] in (235,300,600) and row['fps']==60 and row['location'] in (
                'generated_owner_edge_lower_bound','solace_gap_midpoint_scenario'):
            print(f"{row['pipeline']} {row['location']} {row['ground_speed_m_s']}m/s x4: "
                  f"ready {row['readiness_wall_seconds']:.3f}s, margin {row['margin_km']:+.3f}km")
    # Arithmetic regression for the historical baseline, not an acceptance gate.
    assert abs((2*23.045+70/60)-47.2566666667)<1e-8
    print('Conditional safety report:', args.output)


if __name__ == '__main__':
    main()
