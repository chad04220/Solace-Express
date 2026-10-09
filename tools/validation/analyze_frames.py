#!/usr/bin/env python3
"""Offline frame interval statistics. Python stdlib only; no game/GL/network use.

This reports evidence, never certifies native resolution or a performance pass.
Choose the metric explicitly: displayed cadence and app presentation are different.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def percentile(values, q):
    """Nearest-rank percentile; values must be nonempty, positive and finite."""
    ordered = sorted(values)
    return ordered[max(0, math.ceil(q * len(ordered)) - 1)]


def summarize(values, target_fps=60.0):
    if not values or any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("intervals must be nonempty, finite and greater than zero")
    if not math.isfinite(target_fps) or target_fps <= 0:
        raise ValueError("target FPS must be finite and greater than zero")
    n = len(values)
    budget = 1000.0 / target_fps
    slow1 = sorted(values, reverse=True)[:max(1, math.ceil(n * .01))]
    slow01 = sorted(values, reverse=True)[:max(1, math.ceil(n * .001))]
    total = math.fsum(values)
    return {
        "valid_intervals": n,
        "sum_interval_seconds": total / 1000,
        "mean_ms": total / n,
        "aggregate_fps_from_intervals": n * 1000 / total,
        "p50_ms": percentile(values, .50),
        "p95_ms": percentile(values, .95),
        "p99_ms": percentile(values, .99),
        "p99_9_ms": percentile(values, .999),
        "max_ms": max(values),
        "minimum_instantaneous_fps": 1000 / max(values),
        "one_percent_low_fps_slowest_mean": 1000 / statistics.fmean(slow1),
        "point_one_percent_low_fps_slowest_mean": 1000 / statistics.fmean(slow01),
        "p99_reciprocal_fps_NOT_one_percent_low": 1000 / percentile(values, .99),
        "target_fps": target_fps,
        "budget_ms": budget,
        "over_budget_count": sum(v > budget for v in values),
        "over_budget_percent": 100 * sum(v > budget for v in values) / n,
        "at_least_33_333ms_count": sum(v >= 1000 / 30 for v in values),
        "at_least_50ms_count": sum(v >= 50 for v in values),
        "at_least_100ms_count": sum(v >= 100 for v in values),
    }


def analyze(path, metric, process_id=None, swap_chain=None, target_fps=60.0, scene=None):
    data = Path(path).read_bytes()
    # UTF-8/BOM is the PresentMon CSV contract for this tool; fail rather than guess.
    reader = csv.DictReader(data.decode("utf-8-sig").splitlines())
    if not reader.fieldnames:
        raise ValueError("empty CSV or missing header")
    headers = {h.casefold(): h for h in reader.fieldnames}
    if len(headers) != len(reader.fieldnames):
        raise ValueError("duplicate column names (case-insensitive)")
    def column(name):
        return headers.get(name.casefold())
    actual_metric = column(metric)
    if actual_metric is None:
        raise ValueError(f"metric {metric!r} missing; available: {reader.fieldnames}")
    pid_col, swap_col = column("ProcessID"), column("SwapChainAddress")
    if process_id is not None and pid_col is None:
        raise ValueError("--process-id supplied but CSV has no ProcessID")
    if swap_chain is not None and swap_col is None:
        raise ValueError("--swap-chain supplied but CSV has no SwapChainAddress")
    scene_col = column("scene")
    if scene is not None and scene_col is None:
        raise ValueError("--scene supplied but CSV has no scene column")
    chosen = []
    for row in reader:
        if scene is not None and row[scene_col] != scene:
            continue
        if process_id is not None and row[pid_col] != str(process_id):
            continue
        if swap_chain is not None and row[swap_col].casefold() != swap_chain.casefold():
            continue
        chosen.append(row)
    if not chosen:
        raise ValueError("no rows match selected process/swap chain")
    if scene_col and len({r[scene_col] for r in chosen}) > 1:
        raise ValueError("multiple benchmark scenes; select --scene explicitly")
    identities = {(r.get(pid_col, "unspecified"), r.get(swap_col, "unspecified")) for r in chosen}
    if len(identities) != 1:
        raise ValueError("multiple process/swap-chain streams; select --process-id and --swap-chain explicitly")
    values, unavailable, zero, dropped, invalid = [], 0, 0, 0, []
    dropped_col = column("Dropped")
    for i, row in enumerate(chosen, 1):
        if dropped_col and row[dropped_col].strip().casefold() in ("1", "true"):
            dropped += 1
        raw = (row.get(actual_metric) or "").strip()
        if raw.casefold() in ("", "na", "n/a"):
            unavailable += 1
            continue
        try:
            value = float(raw)
        except ValueError:
            invalid.append(i)
            continue
        if not math.isfinite(value) or value < 0:
            invalid.append(i)
        elif value == 0:
            zero += 1
        else:
            values.append(value)
    if invalid:
        raise ValueError(f"invalid/nonfinite/negative values at selected-row indexes {invalid[:10]}; refuse partial statistics")
    stats = summarize(values, target_fps)
    modes = sorted({r.get(column("PresentMode"), "unavailable") for r in chosen})
    kinds = {
        "displayedtime": "display dwell time for a displayed frame",
        "msbetweendisplaychange": "display-to-display interval",
        "msbetweenpresents": "application present-to-present interval; NOT screen delivery",
        "present_interval_ms": "instrumented app SwapBuffers start-to-start interval; NOT screen delivery",
    }
    if metric.casefold() not in kinds:
        # Component durations are not a frame cadence; do not manufacture FPS from them.
        for name in list(stats):
            if "fps" in name and name != "target_fps":
                del stats[name]
    warnings = [
        "Statistics alone cannot establish native 1920x1080, quality, scene coverage, cache state, target GPU, or absence of dynamic resolution.",
        "Percentiles use nearest rank. 1% low is 1000 / mean(slowest ceil(1% * N) intervals), not 1000 / p99.",
        "No outlier trimming, winsorizing, warmup removal, or automatic stream selection was performed.",
    ]
    if stats["sum_interval_seconds"] < 115:
        warnings.append("Less than 115 seconds of valid interval coverage; this is shorter than the planned 120-second capture.")
    if unavailable or zero:
        warnings.append("Unavailable and zero values are excluded and counted; inspect their meaning before drawing conclusions. DisplayedTime NA can be an undisplayed frame, not a zero-ms frame.")
    if len(modes) > 1:
        warnings.append("Presentation mode changed during capture; inspect the timeline rather than hiding the transition.")
    if metric.casefold() not in kinds:
        warnings.append("Unrecognized metric semantics: this distribution may be a component timer and must not be called frame rate.")
    return {
        "schema": "solace-frame-statistics-v1",
        "input": str(Path(path).resolve()),
        "input_sha256": hashlib.sha256(data).hexdigest(),
        "metric": actual_metric,
        "metric_semantics": kinds.get(metric.casefold(), "caller-defined metric, units must be milliseconds"),
        "process_and_swap_chain": list(next(iter(identities))),
        "selected_rows": len(chosen),
        "unavailable_metric_rows": unavailable,
        "zero_metric_rows": zero,
        "dropped_rows_if_explicit_column": dropped if dropped_col else None,
        "present_modes": modes,
        "statistics": stats,
        "native_1080p_60fps_verdict": "NOT_EVALUATED: requires trace validity, hardware, resolution and scene evidence",
        "warnings": warnings,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--metric", required=True, help="e.g. DisplayedTime or MsBetweenPresents; never inferred")
    parser.add_argument("--process-id")
    parser.add_argument("--swap-chain")
    parser.add_argument("--scene", help="exact scene name for a multi-scene internal benchmark CSV")
    parser.add_argument("--target-fps", type=float, default=60.0)
    parser.add_argument("--out", type=Path, help="new JSON output; existing files are never replaced")
    args = parser.parse_args()
    try:
        result = analyze(args.csv, args.metric, args.process_id, args.swap_chain, args.target_fps, args.scene)
        output = json.dumps(result, indent=2, allow_nan=False) + "\n"
        if args.out:
            with args.out.open("x", encoding="utf-8") as fh:
                fh.write(output)
        else:
            print(output, end="")
    except (OSError, ValueError, csv.Error) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
