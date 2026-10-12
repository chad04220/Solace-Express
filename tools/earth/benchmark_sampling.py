"""Bounded, explicit real-source scalar/NumPy equivalence and timing study.

Run with python -m tools.earth.benchmark_sampling --source INPUT.bin --output
NEW.json. No downloads, installs, global pack build or game changes. Output is
an exclusive local evidence JSON; keep source data and packs outside Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import time

from .common import sha256_file
from .cube import tile_for_latlon
from .elevation import EnviGrid, decode_tile, encode_tile, round_metre, sample_elevation

# Geographic case names are diagnostic labels for a tile containing the point,
# not assertions that every cell in that tile belongs to that terrain class.
CASES = (
    ("Himalaya", 27.9881, 86.9250),
    ("California-coast", 37.7749, -122.4194),
    ("Atlantic", 0, -30),
    ("Greenland", 72, -42),
    ("Mariana", 11.35, 142.2),
    ("north-pole", 90, 0),
    ("south-pole", -90, 0),
    ("antimeridian", 0, 180),
    ("cube-edge", 0, 45),
    ("cube-corner", 35.264389682754654, 45),
)


def benchmark(source_path, levels=(4, 6), area_samples=2, repeat=3, codec="zstd"):
    import numpy
    source_path = Path(source_path)
    header = source_path.with_suffix(".hdr")
    if not header.exists():
        header = Path(str(source_path) + ".hdr")
    record = {"schema": "earth-sampling-benchmark-v1", "environment": {
        "python": platform.python_version(), "numpy": numpy.__version__,
        "platform": platform.platform(), "processor": platform.processor()},
        "source": {"bytes": source_path.stat().st_size, "sha256": sha256_file(source_path),
                   "header_bytes": header.stat().st_size, "header_sha256": sha256_file(header)},
        "area_samples": area_samples, "repeat": repeat, "codec": codec,
        "scope": "Selected tiles only; observed warm-process wall timings, not a global build or terrain truth.",
        "cases": []}
    source = EnviGrid(source_path)
    try:
        for level in levels:
            for name, lat, lon in CASES:
                tile = tile_for_latlon(lat, lon, level)
                measurements, values, payloads = {}, {}, {}
                for backend in ("scalar", "numpy"):
                    sample_times, encode_times, digests = [], [], []
                    for _ in range(repeat):
                        start = time.perf_counter()
                        heights = sample_elevation(tile, source, area_samples, sampler=backend)
                        sampled = time.perf_counter()
                        payload = encode_tile(tile, heights, codec)
                        encoded = time.perf_counter()
                        sample_times.append(sampled - start)
                        encode_times.append(encoded - sampled)
                        digests.append(hashlib.sha256(payload).hexdigest())
                    values[backend], payloads[backend] = heights, payload
                    measurements[backend] = {"sampling_seconds": sample_times,
                        "encode_seconds": encode_times, "median_sample_seconds": statistics.median(sample_times),
                        "median_sample_and_encode_seconds": statistics.median(a+b for a,b in zip(sample_times, encode_times)),
                        "repeat_payload_identity": len(set(digests)) == 1, "payload_sha256": digests[-1]}
                errors = [abs(a-b) for a,b in zip(values["scalar"], values["numpy"])]
                mismatches = sum(round_metre(a) != round_metre(b)
                                 for a,b in zip(values["scalar"], values["numpy"]))
                decoded = decode_tile(payloads["numpy"])["heights"]
                item = {"name": name, "tile": tile.as_dict(), "timings": measurements,
                    "speedup_sampling": measurements["scalar"]["median_sample_seconds"] / measurements["numpy"]["median_sample_seconds"],
                    "speedup_sampling_and_encode": measurements["scalar"]["median_sample_and_encode_seconds"] / measurements["numpy"]["median_sample_and_encode_seconds"],
                    "max_unrounded_difference_m": max(errors), "mean_unrounded_difference_m": statistics.mean(errors),
                    "quantized_mismatches": mismatches, "payloads_identical": payloads["scalar"] == payloads["numpy"],
                    "lossless_codec_roundtrip": decoded == [round_metre(v) for v in values["numpy"]],
                    "payload_bytes": len(payloads["numpy"])}
                record["cases"].append(item)
                print(f"{name} {tile.key}: {item['speedup_sampling']:.1f}x sampling, "
                      f"{item['speedup_sampling_and_encode']:.1f}x including encode; "
                      f"{mismatches} mismatches", flush=True)
                if (mismatches or not item["payloads_identical"] or not item["lossless_codec_roundtrip"] or
                        not all(v["repeat_payload_identity"] for v in measurements.values())):
                    raise ValueError("backend equivalence failed: " + tile.key)
    finally:
        source.close()
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--levels", type=int, nargs="+", default=[4, 6], choices=range(17))
    parser.add_argument("--area-samples", type=int, default=2, choices=range(1, 17))
    parser.add_argument("--repeat", type=int, default=3, choices=range(1, 6))
    parser.add_argument("--codec", default="zstd", choices=("zstd", "zlib"))
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists() or output.is_symlink():
        parser.error("output already exists; use a new file")
    record = benchmark(args.source, args.levels, args.area_samples, args.repeat, args.codec)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
