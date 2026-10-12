# Offline Earth sampling acceleration review

Date: 2026-10-12 UTC. Base: `7954f2b71567021b207c9ddb28f3e2e1591d922c`.
Local review branch: `codex/earth-sampling-performance-review-20261012`.
No runtime Earth, globe, airport, save, game or shader changes. No global pack built.

## Result and scope

An explicitly selected NumPy backend accelerates the same ENVI source sampling:

```sh
# In a separately chosen Python 3.12 environment; never installed implicitly.
python -m pip install -r tools/earth/requirements-numpy.txt
python -m tools.earth build --config CONFIG.json --output NEW_PACK --sampler numpy
```

The standard-library scalar implementation remains the default/reference.
The NumPy backend requires ENVI; grid-JSON and custom sources keep `--sampler scalar`.
Neither backend changes configured `area_samples` (1..16), the default N=2,
canonical footprints, source values, interpolation or whole-metre quantization.
This is a performance change, not a sampling-quality upgrade. Read
[the separate quality study](EARTH_SAMPLING_QUALITY.md) before choosing N.

Pack format v1, manifest shape, codecs and tool version remain unchanged.
Execution-backend choice is not a v1 manifest field; retain the command and pinned
NumPy/Python version alongside build evidence. Claude's ENVI missing/malformed-field
fix is preserved byte-for-byte. The original source reader and codec are unchanged.

## Measurements on the same restored ETOPO source

Environment: Linux x86_64, Python 3.12.14, NumPy 2.3.5, zstandard 0.25.0 /
libzstd 1.5.7. Input: 21600 × 10800 little-endian Float32, 933,120,000 bytes.
Binary SHA-256 `522c6329ed7a8c08c14718e164b04ba203d23c51212371146b789baa419c9895`;
header SHA-256 `fffb4f6c3051d382557744a7e32775664220338255921f5b867fc7c7f947a86f`.
The source was restored from the owner's existing verified bundle, not redownloaded
or transformed. Output data and restored rasters remain outside Git.

Twenty selected tiles cover ten location categories at L4 and L6: Himalaya,
California coast, Atlantic, Greenland, Mariana, north and south poles,
antimeridian, a face edge and a cube corner. Each backend was run three times per
N2 tile. These are warm-process timings on a shared host, not cold-cache measurements
or a representative global throughput guarantee. Sampling excludes source hashing,
Python/NumPy startup and filesystem publication; encoding is timed separately.

| N2, median across ten per-tile medians | Scalar sampling | NumPy sampling | Scalar sample+encode | NumPy sample+encode |
|---|---:|---:|---:|---:|
| L4 | 1.735 s | 0.0360 s | 1.761 s | 0.0622 s |
| L6 | 1.754 s | 0.0365 s | 1.779 s | 0.0630 s |

Per-tile sampling speedups were **43.0–51.6×**; including unchanged zstd encoding,
**25.7–30.6×**. Across all 1,331,280 stored heights there were zero quantized
mismatches. Every corresponding encoded payload matched exactly, every repeat
matched, and lossless codec round trips passed. Maximum observed unrounded
NumPy–scalar difference: **7.15e-10 m**. This tiny arithmetic difference is distinct
from quadrature error and source accuracy.

Four additional full N8 tiles (Himalaya and north-pole tiles, L4/L6; one repeat)
matched every encoded byte. Scalar sampling took 26.43–27.25 s versus 0.483–0.553 s;
sample+encode speedup was 46.9–52.2×. Maximum observed unrounded difference was
2.46e-10 m. N8 timing does not establish that N8 meets a terrain-quality tolerance.

The existing 20-tile L4 N2 pack was rebuilt twice through the header-pinned builder
with NumPy in 2.12 / 2.09 seconds, including source hashing and pack verification.
**All 22 delivered files, including manifest and notices, matched byte-for-byte.**
Manifest SHA-256: `cddc55d760f1397d29cedcff34facbb19d132dc97cc3da493dd670ad7102f4b4`.
A separate matched, new-process CLI benchmark on this same host measured scalar
**36.74 / 37.34 s** versus NumPy **2.39 / 2.34 s**, approximately **15.7× end-to-end**.
Each timed command includes Python startup, NumPy import, source/header validation,
compression, filesystem writes and final pack verification. All four runs matched
all 22 delivered files. Order was scalar A, NumPy A, scalar B, NumPy B; filesystem
cache was already warm, no cache flush was attempted, and the shared host also ran
other validation work. Do not present this as cold-cache or dedicated-machine data.

## Implementation and numerical safety

- Fixed batches of at most 4,096 stored cells; no N²-sized sampling tensor and no
  growing cache across tiles. The same sy/sx summation order is retained.
- The validated ENVI mmap is viewed read-only and zero-copy, honoring byte order
  and header offset. Only gathered Float32 corners are converted to Float64.
- Interior integer addresses are generated in arrays. Every external edge/corner
  delegates to the existing canonical `sample_location` ownership contract.
- Geometry uses vector tangent/atan2/hypot with mathematically equivalent ratios.
  NumPy/libm can differ by a few ulps. Near source-grid stencil or coverage/cap
  boundaries, exact scalar geometry and source sampling run **before gathers**.
  This preserves validation of all four corners, including zero-weight nodata.
- Near half-metre rounding ties or supported height endpoints, the complete scalar
  cell quadrature is recomputed. The conservative cushion scales with source
  spacing. Pathological inputs concentrated on these boundaries can reduce speed;
  correctness/reference behavior takes priority over throughput.
- These guards plus measured equivalence are not a formal cross-platform libm
  proof. Same-environment repeatability was measured. Native Windows CI is a
  separate gate; no Windows run is claimed in this local report.
- Regional coverage and nodata still fail closed. No value fill, silent downsample,
  lossy intermediate or parallel publication was introduced.

An independent audit found and resolved a nodata-stencil boundary bug in the first
vector implementation. Scalar x=1.0 versus vector x just below 1 selected different
zero-weight source neighbors. Regressions cover both incorrectly rejecting valid
input and incorrectly accepting scalar-invalid input, plus a complete tile case.
The final pre-gather scalar-boundary path fixes the independent reproduction.

## Memory and portability

Memory-only traced runs on the final sampler:

| Tile / N | Peak traced sampling allocations | Peak process RSS |
|---|---:|---:|
| Himalaya L6 / N2 | 3,445,868 bytes | 41,476 KiB |
| Himalaya L6 / N16 | 3,541,964 bytes | 41,536 KiB |
| Himalaya L0 / N16 | 3,541,855 bytes | 489,792 KiB |

Tracing excludes NumPy import and the source mmap. RSS includes interpreter and
resident file-backed source pages. The L0 footprint faults in far more source
pages than L6; do not describe total process memory as 3.5 MB. The virtual mapping
is the complete 933,120,000-byte source and, over a world traversal, all of those
pages can become resident. Reserve source-mapping residency plus interpreter,
codec and fixed-batch headroom. The sampler does not allocate a second full raster.
No multiprocessing or hidden worker-count/memory multiplication is introduced.

The portable scalar path imports no NumPy. A `python -S` subprocess builds and
samples with standard-library support. Explicit NumPy selection without the
package gives an actionable error and does not leave a partial pack. Missing
NumPy causes optional tests to skip normally; dedicated CI invokes
`--require-numpy` so an unavailable requested accelerator **fails** rather than
reporting a green skipped suite.

## Validation and reproduction

```sh
python tests/earth_sampling_test.py --require-numpy
python -S tests/earth_sampling_test.py PortableSamplerTests
python tests/earth_pack_test.py
python tests/usgs_world_map_test.py
python -m tools.earth.benchmark_sampling --source INPUT.bin \
  --output NEW_BENCHMARK.json --levels 4 6 --area-samples 2 --repeat 3
```

The synthetic optional-sampler suite passed **15/15**, and the separate no-site-package
portable checks passed **4/4**. The suite checks exact canonical addresses at L0/L6/L16,
all-face full-tile N2 equality and aprons/corners, N1/N2/N8/N16 unrounded probes,
big-endian and header-offset inputs, regional/antimeridian/polar behavior,
invalid/nodata sources, rounding ties, bounded batches, lossless round trips,
pack identity/cleanup, absent dependencies and mandatory-CI-dependency behavior.
A deliberately hidden bilinear ridge demonstrates why N2=N4=N8 agreement cannot
justify silently lowering a requested N16.

The existing Earth suite passed 57/57 (including Claude's diagnostic regression),
and source-handoff tests passed 38/38. Focused CTest passed earth_pack,
earth_sampling, usgs_world_map and ci_test_selection (4/4). Full native game CTest,
Windows execution and remote CI were not run for this unpublished local revision.
No runtime changes require a new game build, but publication should await the
new Linux/Windows optional-sampler CI and the repository's normal gates.

[Evidence directory](validation/earth-sampling-performance) contains complete
per-tile timing/error records, repeat pack proof, memory observations, test logs,
source-reader preservation proof and independent audit findings.

## Bounded planning, not a global-build claim

A complete L6 level has 24,576 tiles and 3,271,753,728 bytes of raw 258² int16
sample payloads, before headers, manifest or compression. Multiplying the sampled
N2 L6 median sample+encode time by that count yields about **26 minutes** of that
work versus about **12.1 hours** scalar. This excludes startup, source validation,
full-world page faults, storage, final verification and unmeasured terrain mix;
it is a planning projection, not a measured global build or commitment.

Higher N carries its actual N² work; no quality reduction is hidden in these
numbers. Before any full-world build, choose levels/N and accuracy diagnostics,
reserve the source, output, staging and verification disk/RAM budgets, measure a
wider bounded tranche, then approve the final run. Compressed global size remains
unmeasured. The quality study shows why speeding up N2 alone does not make its
steep-terrain integration error acceptable.
