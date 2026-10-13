#!/usr/bin/env python3
"""Run production regional-world checks in isolated, bounded-memory processes.

Examples:
  python3 tools/validation/procedural_world_report.py --test build/procedural_world_test \
    --output build/procedural-world-review --baseline /path/to/unchanged/test_world.bin
  python3 tools/validation/procedural_world_report.py --test build/procedural_world_test \
    --output build/procedural-world-review --regions first --quick --keep-caches

The optional --active-cache keeps one authored world alive while a single
regional world builds on a worker; --threads sets that worker's parallel budget.
Full mode builds each selected descriptor twice from scratch, compares every
serialized byte, and reloads the first cache. --quick omits only the independent
second build, so its report does not claim full-world fresh-build determinism.
Optional --mismatch-region exercises descriptor rejection against an existing
cache. Files/logs are created only beneath --output. Baseline files are read only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--test", required=True, type=Path, help="built procedural_world_test executable")
    parser.add_argument("--output", required=True, type=Path, help="report/log output directory")
    parser.add_argument("--regions", nargs="+", default=["first"], help="canonical region IDs or first")
    parser.add_argument("--threads", type=int, default=0, help="optional region worker parallelism limit (1..256)")
    parser.add_argument("--active-cache", type=Path, help="authored baseline cache kept active during region worker checks")
    parser.add_argument("--baseline", type=Path, help="unchanged original Solace cache, stamped solace-test-run")
    parser.add_argument("--mismatch-region", help="different descriptor to rebuild against the first region's cache")
    parser.add_argument("--legacy-recipe-cache", action="store_true",
                        help="prove the pre-revision recipe stamp is rejected despite a valid complete cache payload")
    parser.add_argument("--quick", action="store_true", help="omit second fresh build and full-cache determinism comparison")
    parser.add_argument("--keep-caches", action="store_true", help="retain generated binary caches for rendering/review")
    parser.add_argument("--timeout", type=int, default=1800, help="maximum seconds per subprocess")
    args = parser.parse_args()
    if args.threads < 0 or args.threads > 256:
        parser.error("--threads must be 1..256, or zero for the default")
    worker_args = ["--threads", str(args.threads)] if args.threads else []
    if args.active_cache:
        if not args.active_cache.resolve().is_file():
            parser.error(f"active cache does not exist: {args.active_cache}")
        worker_args += ["--active-cache", str(args.active_cache.resolve())]
    binary = args.test.resolve()
    if not binary.is_file():
        parser.error(f"test executable does not exist: {binary}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache_dir = Path(tempfile.mkdtemp(prefix="world-caches-", dir=output))
    report = {
        "schema_version": 1,
        "test_binary": str(binary),
        "test_binary_sha256": sha256(binary),
        "regions_requested": args.regions,
        "independent_fresh_build_comparison_requested": not args.quick,
        "one_materialized_region_per_process": not bool(args.active_cache),
        "active_authored_world_during_staging": bool(args.active_cache),
        "worker_parallel_limit": args.threads,
        "legacy_recipe_cache_rejection_requested": args.legacy_recipe_cache,
        "memory_note": "World/scenery vector-capacity estimates exclude transient allocations, hash-map node overhead, bridge internals, GPU and process resources. Peak RSS is measured by GNU time or the native test process getrusage when available.",
        "steps": [],
        "passed": True,
    }
    gnu_time = Path("/usr/bin/time") if sys.platform.startswith("linux") else None
    if gnu_time and not gnu_time.is_file():
        gnu_time = None

    def run(name: str, *arguments: str) -> bool:
        command = [str(binary), *map(str, arguments)]
        rss_file = output / f"{name}.rss-kib.txt"
        invoked = [str(gnu_time), "-f", "%M", "-o", str(rss_file), *command] if gnu_time else command
        start = time.monotonic()
        log_file = output / f"{name}.log"
        result = {"name": name, "command": command, "log": str(log_file)}
        print(f"Running {name}", flush=True)
        try:
            with log_file.open("w", encoding="utf-8") as log:
                process = subprocess.Popen(invoked, cwd=cache_dir, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=os.name == "posix")
                try:
                    returncode = process.wait(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    # Kill the test as well as a possible GNU-time wrapper.
                    if os.name == "posix":
                        os.killpg(process.pid, signal.SIGKILL)
                    else:
                        process.kill()
                    process.wait()
                    raise
            result["returncode"] = returncode
            result["passed"] = returncode == 0
        except subprocess.TimeoutExpired:
            result["returncode"] = None
            result["passed"] = False
            result["error"] = f"exceeded {args.timeout} seconds"
        except OSError as exc:
            result["returncode"] = None
            result["passed"] = False
            result["error"] = str(exc)
        result["seconds"] = round(time.monotonic() - start, 3)
        result["peak_rss_kib"] = None
        if rss_file.is_file():
            # GNU time writes a diagnostic line before the number after failure.
            lines = rss_file.read_text(encoding="utf-8").splitlines()
            if lines and lines[-1].isdigit():
                result["peak_rss_kib"] = int(lines[-1])
        if log_file.is_file():
            result["measurements"] = [line for line in log_file.read_text(encoding="utf-8").splitlines()
                                      if line.startswith(("REGION ", "AIRPORTS ", "SETTLEMENTS ", "MEMORY ",
                                                          "SOMNIUM_PRESERVATION ", "CACHE_COMPARE ", "PROCESS ", "FLIGHT ", "BACKGROUND ", "procedural_world_test:"))]
            if result["peak_rss_kib"] is None:
                for line in result["measurements"]:
                    match = re.fullmatch(r"PROCESS peak_rss_kib=(\d+)", line)
                    if match:
                        result["peak_rss_kib"] = int(match.group(1))
        report["steps"].append(result)
        report["passed"] = report["passed"] and result["passed"]
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {'PASS' if result['passed'] else 'FAIL'} ({result['seconds']:.1f}s)", flush=True)
        return bool(result["passed"])

    try:
        first_cache = None
        for index, region in enumerate(args.regions):
            first = cache_dir / f"region-{index}-a.bin"
            second = cache_dir / f"region-{index}-b.bin"
            if not run(f"region-{index}-fresh", "--region", region, "--cache", first, "--expect-fresh", *worker_args):
                continue
            if first_cache is None:
                first_cache = first
                if args.legacy_recipe_cache:
                    # This is the exact v1 recipe stamp, before terrain-content
                    # revision was separated from stable catalog/address IDs.
                    # Alter only a copy's header; keep a structurally valid cache
                    # so rejection cannot accidentally pass due to truncation.
                    fresh_log = (output / f"region-{index}-fresh.log").read_text(encoding="utf-8")
                    canonical = re.search(r"^REGION id=(\S+)", fresh_log, re.MULTILINE).group(1)
                    legacy_key = f"1:{0x534f4c4143452026}:{canonical}:procedural-world-test-v1"
                    h = 1469598103934665603
                    for byte in legacy_key.encode("ascii"):
                        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
                    legacy_stamp = f"procedural-{h:016x}"
                    legacy = cache_dir / "legacy-recipe.bin"
                    shutil.copyfile(first, legacy)
                    with legacy.open("r+b") as f:
                        f.seek(4)
                        f.write(legacy_stamp.encode("ascii").ljust(64, b"\0"))
                    if run("legacy-recipe-valid-payload", "--verify-cache-stamp", legacy, legacy_stamp):
                        if run("legacy-recipe-rejected", "--region", canonical, "--cache", legacy, "--expect-fresh", *worker_args):
                            run("legacy-recipe-regenerated-parity", "--compare-caches", first, legacy)
            run(f"region-{index}-reload", "--region", region, "--cache", first, "--expect-cache", *worker_args)
            if not args.quick:
                if run(f"region-{index}-repeat", "--region", region, "--cache", second, "--expect-fresh", *worker_args):
                    run(f"region-{index}-determinism", "--compare-caches", first, second)
            if first.is_file():
                report.setdefault("caches", []).append({"region": region, "path": str(first), "bytes": first.stat().st_size,
                                                        "sha256": sha256(first), "retained": args.keep_caches})
        if args.mismatch_region and first_cache:
            # Use a separate copy, preserving the first region for later rendering.
            mismatch = cache_dir / "descriptor-mismatch.bin"
            shutil.copyfile(first_cache, mismatch)
            run("descriptor-mismatch", "--region", args.mismatch_region, "--cache", mismatch, "--expect-fresh", *worker_args)
        if args.baseline:
            baseline = args.baseline.resolve()
            if not baseline.is_file():
                report["passed"] = False
                report["baseline_error"] = f"baseline cache does not exist: {baseline}"
            else:
                before_hash = sha256(baseline)
                fresh = cache_dir / "somnium-fresh.bin"
                report["baseline"] = {"path": str(baseline), "bytes": baseline.stat().st_size, "sha256": before_hash}
                run("original-solace-preservation", "--preserve-solace", baseline, fresh)
                report["baseline"]["remained_unchanged"] = sha256(baseline) == before_hash
                report["passed"] = report["passed"] and report["baseline"]["remained_unchanged"]
    finally:
        report["cache_directory_retained"] = str(cache_dir) if args.keep_caches else None
        if not args.keep_caches:
            shutil.rmtree(cache_dir)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Report: {output / 'report.json'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
