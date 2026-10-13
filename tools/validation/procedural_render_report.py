#!/usr/bin/env python3
"""Build the optional EGL harness explicitly, then capture a fingerprinted review.

The harness is EXCLUDE_FROM_ALL: a successful default build is insufficient.
This diagnostic disables airborne traffic, preserves production materials, and
records that coverage limitation. It does not certify hardware frame rate.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source_manifest():
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'tests/ui_review_harness.cpp', Path(__file__).resolve()]
    paths += sorted(path for path in (ROOT / 'src').rglob('*') if path.is_file())
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--shader-cache', required=True, type=Path)
    parser.add_argument('--expected-binary-sha256')
    parser.add_argument('--jobs', nargs='+', choices=['atoll', 'fjord', 'overview', 'streaming'],
                        default=['atoll', 'fjord', 'overview', 'streaming'])
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    build = args.build_dir.resolve(); binary = build / 'ui_review_harness'
    with (output / 'explicit-harness-build.log').open('w') as log:
        subprocess.run([args.cmake, '--build', str(build), '--target', 'ui_review_harness', '-j2'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary_hash = digest(binary)
    if args.expected_binary_sha256 and binary_hash != args.expected_binary_sha256:
        raise RuntimeError('rebuilt executable differs from the explicitly expected fingerprint')
    sources = source_manifest()
    manifest_hash = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    record = dict(binary=str(binary), binary_sha256=binary_hash, source_manifest_sha256=manifest_hash,
                  source_files=sources, air_traffic_enabled=False,
                  method='actual EGL; diagnostic air traffic off; software timings are not hardware FPS', jobs=[])
    report = output / 'render-report.json'
    def save():
        report.write_text(json.dumps(record, indent=2) + '\n')
    save()
    print('HARNESS_SHA256', binary_hash, 'SOURCE_MANIFEST_SHA256', manifest_hash, flush=True)
    jobs = {
        'atoll': ('region-pr1-1-9-19', 1280, 720, {'HARNESS_REGION_VIEW': 'lagoon'}),
        'fjord': ('region-pr1-4-17-7', 1280, 720, {'HARNESS_REGION_VIEW': 'relief'}),
        'overview': ('horizon6-pr1-1-9-19,horizon6-pr1-4-17-7', 960, 540, {'HARNESS_HORIZON_PITCH_DEG': '20'}),
        'streaming': ('seamless-pr1-4-17-7,seamless-solace,horizon16-pr1-4-17-7,horizon28-pr1-4-17-7,horizon30-pr1-4-17-7', 640, 360, {}),
    }
    for name in args.jobs:
        if digest(binary) != binary_hash or source_manifest() != sources:
            raise RuntimeError('source or executable changed during capture; refusing mixed-provenance results')
        scenes, width, height, overrides = jobs[name]
        folder = output / name; folder.mkdir(exist_ok=True)
        env = os.environ.copy()
        for key in ('HARNESS_REGION_VIEW', 'HARNESS_HORIZON_PITCH_DEG', 'REGION_RENDER_DIAG'):
            env.pop(key, None)
        env.update(HARNESS_AIR_TRAFFIC='0', LP_NUM_THREADS='2',
                   MESA_SHADER_CACHE_DIR='/tmp/procedural-mesa-cache', MATDIR=str(ROOT / 'assets/materials'),
                   SHADERCACHE=str(args.shader_cache.resolve()), **overrides)
        command = [str(binary), scenes, str(folder), str(width), str(height), '1', 'scene']
        job = dict(name=name, command=command, width=width, height=height, overrides=overrides,
                   captured_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   binary_sha256=binary_hash, source_manifest_sha256=manifest_hash)
        print('CAPTURING', name, job['captured_at_utc'], flush=True)
        started = time.monotonic()
        log_path = output / (name + '.log')
        with log_path.open('w') as log:
            result = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            _, status, usage = os.wait4(result.pid, 0)
            result.returncode = os.waitstatus_to_exitcode(status)
        job.update(returncode=result.returncode, wall_seconds=round(time.monotonic() - started, 3),
                   process_peak_rss_kib=usage.ru_maxrss)
        job['sources_unchanged'] = source_manifest() == sources and digest(binary) == binary_hash
        text = log_path.read_text()
        job['camera_pitch_telemetry_verified'] = name not in ('overview', 'streaming') or 'downward_pitch_deg=' in text
        job['image_sha256'] = {path.name: digest(path) for path in folder.glob('*.png')}
        record['jobs'].append(job); save()
        print('FINISHED', name, 'exit', result.returncode, 'seconds', job['wall_seconds'], flush=True)
        if result.returncode or not job['sources_unchanged'] or not job['camera_pitch_telemetry_verified']:
            raise RuntimeError('capture failed or fingerprint/framing telemetry does not match the built source')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
