#!/usr/bin/env python3
"""Native 1080p environment diagnostic bundle for an instrumented Windows build.

Dry-run is the default. --run launches the executable. No settings or caches are
removed. Child APPDATA is isolated under the output folder; the game's normal
shader/body cache beside the executable can still be created/updated normally.
These are warm fixed-camera diagnostic cases, NOT a streaming or display-FPS gate.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import re
import subprocess
import sys
import uuid

CASES = {
    "capital_day": "look_-3200_-1200_215_18_700_20_12@0.25,1500,0,0,8,200,0,0.1,12",
    "port_verde_day": "look_-29500_10500_225_16_750_20_12@0.25,1500,0,0,8,200,0,0.1,12",
    "forest_low_view": "wcam_-24000_-5000_80_35_-7_12",
    "kaleo_rocks": "look_23000_10000_220_18_300_8_12@0.25,1500,0,0,8,200,0,0.1,12",
    "fjord_rocks": "wcam_17886.990_-27578.244_293.345_15.000_-14.000_12.000",
    "gull_rock_coast": "look_3000_31000_220_14_450_5_12@0.25,1500,0,0,8,200,0,0.1,12",
    "airport_day": "apv_CAP_1_12",
    "airport_night": "apv_CAP_1_22",
    "airport_wet": "look_0_-4000_200_15_600_5_16@0.95,600,1,0,12,200,4,0.2,16",
    "airport_storm": "look_0_-4000_200_15_600_5_16@0.95,600,1,1,20,200,8,0.35,16",
    "airport_wet_night": "look_0_-4000_200_15_600_5_22@0.95,600,1,1,20,200,8,0.35,22",
}
# Clear inherited diagnostic switches, not arbitrary environment variables.
DEBUG_ENV = ["WX", "TOD", "RSCALE", "DBGOFF", "GLERR", "CLOUDSPLITOFF", "MESHOFF", "SCREENFEEDS", "MATDIR", "HUDDEMO", "GAVOUT", "MTBAY", "JCLOUD", "JMACH", "WRCLOUD", "WRTHR", "WRSPD", "WRBOMB", "WRUFO", "AF_ALL", "HULLDBG", "MESHNOPRE", "PARTNOINST", "ENTDBG", "CKCTL", "GAVCTL", "GAVCTLN", "GAVTHR", "LTDIST", "MENUORBIT", "NOTRF", "WAKEDBG", "WAKEOFF", "WRSPARKS", "NOEARLY", "NOMARCH", "OBJDBG", "OBJFULL", "OLDNEAR", "PROXYDUMP", "PROXYMARCH", "RASTERNOFX", "SHMAPOFF", "CLIPDBG", "NVFAIL", "NVFAIL_OK", "SHADERDUMP", "SHADERTIME", "TSHOFF", "HULLOFF"]


def inspect_csv(path, expected_frames, expected_quality):
    with Path(path).open(newline='', encoding='utf-8-sig') as fh:
        rows = list(csv.DictReader(fh))
    issues = []
    if len(rows) != expected_frames:
        issues.append(f"expected {expected_frames} rows, found {len(rows)}")
    if not rows:
        return {"capture_issues": issues, "performance_verdict": "NOT_EVALUATED"}
    native = all(r.get('native_1080p_valid') == '1' and r.get('display_width') == '1920' and r.get('display_height') == '1080' and r.get('render_width') == '1920' and r.get('render_height') == '1080' and r.get('render_scale') == '1.000000' for r in rows)
    if not native: issues.append("native 1920x1080 invariant failed")
    if any(r.get('quality') != str(expected_quality) for r in rows): issues.append("quality invariant failed")
    statuses = {r.get('scene_status') for r in rows}
    if len(statuses) != 1 or '-1' in statuses or None in statuses: issues.append("scene changed or aircraft crashed")
    ids = [r['gpu_sample_id'] for r in rows if r.get('gpu_sample_id') not in (None, 'NA', '')]
    if len(ids) != len(set(ids)): issues.append("repeated GPU sample ID")
    try:
        issued = [int(r['render_frame_serial']) for r in rows]
        origins = [int(r['gpu_sample_origin_frame']) for r in rows if r.get('gpu_sample_id') not in (None, 'NA', '')]
        gpu_times = [float(r['gpu_render_scene_ms']) for r in rows if r.get('gpu_sample_id') not in (None, 'NA', '')]
        if any(b != a + 1 for a, b in zip(issued, issued[1:])):
            issues.append("renderer frame sequence is not one frame per measurement")
        if origins != issued[:-3]:
            issues.append("GPU origin sequence is incomplete; only the final three pending queries are expected to be absent")
        if any(not math.isfinite(v) or v <= 0 for v in gpu_times):
            issues.append("invalid raw GPU duration")
    except (KeyError, TypeError, ValueError):
        issues.append("GPU origin/renderer serial/duration metadata is missing or malformed")
    if len(ids) < max(0, len(rows) - 3): issues.append("GPU distribution incomplete beyond the normal trailing query delay")
    return {"rows": len(rows), "native_1080p_every_row": native, "fresh_gpu_samples": len(ids), "capture_issues": issues,
            "performance_verdict": "NOT_EVALUATED: app intervals, raw GPU timings and external display capture must be reviewed separately"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True, help='new output directory; never replaces an existing run')
    p.add_argument('--frames', type=int, default=7200, help='simulation frames per case; NOT a guarantee of 120 wall seconds')
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--quality', type=int, choices=[0, 1, 2], default=1)
    p.add_argument('--case', action='append', choices=sorted(CASES))
    p.add_argument('--commit', default='unverified', help='source commit for this binary; recorded as caller-supplied')
    p.add_argument('--run', action='store_true')
    args = p.parse_args()
    if not 2 <= args.frames <= 1000000 or not 1 <= args.repeats <= 100:
        p.error('frames must be 2..1000000 and repeats 1..100')
    cases = args.case or list(CASES)
    exe, out = args.exe.resolve(), args.out.resolve()
    if not args.run:
        print(json.dumps({"dry_run": True, "exe": str(exe), "out": str(out), "frames": args.frames, "quality": args.quality, "cases": {k: CASES[k] for k in cases}, "repeats": args.repeats}, indent=2))
        return
    if os.name != 'nt': p.error('--run requires Windows and a native OpenGL GPU; do not use software Mesa results as RTX evidence')
    if not exe.is_file(): p.error('executable not found')
    if out.exists(): p.error('output directory exists; choose a new directory to preserve previous evidence')
    out.mkdir(parents=True)
    appdata = out / 'isolated_appdata'; settings = appdata / 'SolaceExpress'; settings.mkdir(parents=True)
    (settings/'settings.cfg').write_text(f'quality {args.quality}\nrenderRes 0\nfpsTarget 0\ntraffic 1\nfov 55\nfullscreen 0\n', encoding='ascii')
    env = os.environ.copy()
    for key in DEBUG_ENV: env.pop(key, None)
    env['APPDATA'] = str(appdata)
    manifest = {'schema': 'solace-environment-benchmark-v1', 'utc': datetime.now(timezone.utc).isoformat(),
                'run_id': uuid.uuid4().hex, 'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'source_commit_caller_supplied': args.commit, 'host': platform.platform(),
                'frames_per_case': args.frames, 'quality': args.quality, 'native_output': [1920, 1080],
                'dynamic_resolution': False, 'simulation_dt_seconds': 1/60, 'case_geometry_visual_validation': 'pending',
                'scope': 'warm fixed-camera diagnostics; manual movement/streaming/presentation validation remains required', 'runs': []}
    # Read-only machine context; unsupported commands are recorded rather than hidden.
    commands = {
        'system.txt': ['powershell.exe', '-NoProfile', '-Command', "Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | Format-List; Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate | Format-List; Get-CimInstance Win32_OperatingSystem | Select-Object Caption,BuildNumber,TotalVisibleMemorySize | Format-List"],
        'power.txt': ['powercfg.exe', '/getactivescheme'],
    }
    if shutil.which('nvidia-smi'):
        commands['nvidia.txt'] = ['nvidia-smi', '--query-gpu=name,driver_version,memory.total,pci.bus_id', '--format=csv']
    for name, command in commands.items():
        try:
            info = subprocess.run(command, capture_output=True, text=True, check=False)
            (out/name).write_text(info.stdout + info.stderr + f'\nexit={info.returncode}\n', encoding='utf-8')
        except OSError as exc:
            (out/name).write_text(f'Unavailable: {exc}\n', encoding='utf-8')
    for repeat in range(args.repeats):
        # Alternate order avoids always measuring one case only on a colder GPU.
        order = cases if repeat % 2 == 0 else list(reversed(cases))
        for label in order:
            folder = out / f'{repeat + 1:02d}_{label}'; folder.mkdir()
            report = folder / 'bench.txt'
            command = [str(exe), '--bench', CASES[label], '--size', '1920x1080', '--fullscreen', '--bench-frames', str(args.frames), '--bench-csv', '--out', str(report)]
            print(f'Run {repeat + 1}/{args.repeats}: {label}', flush=True)
            result = subprocess.run(command, cwd=exe.parent, env=env, check=False)
            entry = {'case': label, 'scene': CASES[label], 'repeat': repeat + 1, 'command': command, 'returncode': result.returncode}
            frame_csv = Path(str(report) + '.frames.csv')
            if frame_csv.is_file(): entry.update(inspect_csv(frame_csv, args.frames, args.quality))
            else: entry['capture_issues'] = ['frame CSV missing; confirm this is the instrumented build']
            if report.is_file():
                report_text = report.read_text(encoding='utf-8', errors='replace')
                entry['report_sha256'] = hashlib.sha256(report.read_bytes()).hexdigest()
                overwritten = re.search(r'query_overwrites=(\d+)', report_text)
                if overwritten and int(overwritten.group(1)):
                    entry.setdefault('capture_issues', []).append('GPU queries overwritten before completion was read')
                if 'capture complete=yes' not in report_text:
                    entry.setdefault('capture_issues', []).append('benchmark did not mark capture complete')
                if '@' in CASES[label]:
                    expected = [float(v) for v in CASES[label].split('@', 1)[1].split(',')]
                    weather_line = next((line for line in report_text.splitlines() if 'weather preset:' in line), '')
                    got = dict(re.findall(r'(\w+)=([-+0-9.eE]+)', weather_line))
                    keys = ['cover', 'base', 'precip', 'storm', 'wind_kt', 'from', 'gust_kt', 'turbulence', 'hour']
                    if any(k not in got or abs(float(got[k]) - v) > .01 for k, v in zip(keys, expected)):
                        entry.setdefault('capture_issues', []).append('final preset weather differs from requested weather')
            else:
                entry.setdefault('capture_issues', []).append('text report missing')
            manifest['runs'].append(entry)
            (out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            for name in ('startup.log', 'settings.cfg'):
                src = settings/name
                if src.is_file(): (folder/name).write_bytes(src.read_bytes())
            if result.returncode or entry.get('capture_issues'):
                print(f'Capture needs attention: {entry}', file=sys.stderr)
                return 2
    print(f'Captured evidence in {out}. No 60-FPS verdict has been inferred.')
    return 0

if __name__ == '__main__': sys.exit(main())
