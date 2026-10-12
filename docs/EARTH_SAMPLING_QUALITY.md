# Earth sampling quality: bounded scalar-reference evidence

Date: 2026-10-12 UTC. Baseline: `7954f2b71567021b207c9ddb28f3e2e1591d922c`.

## Decision

**Keep the performance optimization mathematically equivalent at the configured N. Do not silently lower N, change the default, or treat consecutive midpoint-rule agreement as an accuracy guarantee.** Retain N=2 as the compatibility default while labeling it as a speed/legacy setting, not a high-quality guarantee.

For a new quality-focused L4 diagnostic build, N=16 is the most defensible available setting within the current production 1..16 API, followed by reference checks on steep terrain, coasts, polar cells and face edges. It is not enough to promise less than 0.5 m integration error everywhere: this study found an L4 N16–N256 difference of 0.949 m. No fixed N in the existing API establishes a global sub-metre integration bound. Coarse levels particularly need either higher-N support with explicit cost controls or a separately designed source-cell-aware integration method.

No production defaults or source files were changed by this study. The N32..N1024 loops are scalar diagnostic references only; they do not bypass the production API for a pack build.

## Verified inputs and oracle

- The same owned ETOPO2022 surface 60 arcsecond ENVI input was restored locally. It is 21600 × 10800 Float32 little-endian, 933,120,000 bytes.
- Binary SHA-256: `522c6329ed7a8c08c14718e164b04ba203d23c51212371146b789baa419c9895`.
- Header SHA-256: `fffb4f6c3051d382557744a7e32775664220338255921f5b867fc7c7f947a86f`.
- Frozen baseline copies of `cube.py`, `common.py`, `elevation.py` and `__init__.py` are under `baseline/tools/earth`. The scalar oracle retains the baseline midpoint positions, coordinate conversion, bilinear interpolation, Jacobian and summation order.
- 140 N2 oracle values (including aprons/corners) were checked against the previously delivered 20-tile L4 pack: all rounded integers matched; maximum observed rounding difference was 0.498916 m. See `oracle-check.json`.

## Method and coverage

807 deterministic canonical target pixels across levels 0, 2, 4, 6 and 8, with N1, N2, N4, N8, N16, N32 and N64 at every pixel. The stress sample includes Everest, K2, Denali, Aconcagua, Alps, New Zealand, multiple islands and shorelines, trenches, plains, both poles, antimeridian crossings, every face's corners and edge midpoints, and 24 seeded uniform-sphere locations at all five levels. Nearby shifted pixels are included around the named sites. Exact duplicate addresses are removed.

77 cases selected for large residuals globally and within each level were deepened to N128 and N256. Five selected cases were checked through N1024; see the reference-check results below. This is a stress-oriented bounded sample, not a statistically representative global survey and not a complete tile/pack build.

Reference values integrate the **same bilinear source interpolant**. They are not measurements of ground truth or proofs of exact integration. Differences compare unrounded metre values unless explicitly labeled integer-height differences.

## All 807 probes: maximum absolute differences versus N64

| Level | Probes | N2 | N8 | N16 | N32 |
|---:|---:|---:|---:|---:|---:|
| 0 | 159 | 373.815867 m | 37.379537 m | 7.431440 m | 1.155884 m |
| 2 | 162 | 219.289584 m | 7.230108 m | 1.587776 m | 0.327737 m |
| 4 | 162 | 41.758116 m | 4.326090 m | 0.906755 m | 0.147442 m |
| 6 | 162 | 14.582970 m | 1.038312 m | 0.210160 m | 0.053846 m |
| 8 | 162 | 10.145259 m | 1.038312 m | 0.141757 m | 0.053846 m |

These broader probes do not contradict the earlier 1,280-probe L4 diagnostic: the locations, levels and comparison reference differ. The previous 38.356337 m N2–N8 and 1.373257 m N8–N16 maxima are measurements on that previous sample, not global bounds.

## 77 selected difficult cases: maximum differences versus N256

| Level | Deep probes | N2 | N8 | N16 | N32 | N64 | N128 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 35 | 373.872432 | 37.487136 | 7.478801 | 1.108523 | 0.174773 | 0.058851 |
| 2 | 15 | 219.369923 | 7.199738 | 1.668116 | 0.408077 | 0.080340 | 0.022471 |
| 4 | 9 | 41.800250 | 4.369926 | 0.948889 | 0.176606 | 0.048431 | 0.011424 |
| 6 | 8 | 14.588363 | 1.118434 | 0.221957 | 0.133968 | 0.080123 | 0.024698 |
| 8 | 10 | 10.225382 | 1.118434 | 0.221880 | 0.133968 | 0.080123 | 0.024698 |

All values in metres. The largest selected N128–N256 difference is 0.058851 m. N256 itself still has numerical integration error; the table does not claim an exact reference.

## Reference stability spot checks

Five selected difficult cases (three L0 mountain cells, L4 K2 and the L8 north-pole-adjacent cell) were extended to N512/N1024. The maximum N256–N1024 difference was 0.016008 m, and maximum N512–N1024 difference was 0.003235 m. The L8 north-pole N256–N1024 difference was 0.002611 m. These checks support using N256 as a useful diagnostic reference for those five cases; they do not establish its accuracy everywhere. See `reference-check.json`.

## Why a spacing-only or convergence-only adaptive default is unsafe

1. **Midpoint aliasing can produce identical wrong answers.** A valid bilinear source grid with a one-node 10,000 m triangular ridge and 16 source cells across a target pixel gives N2=N4=N8=0 m, while N16..N256 converge to about 625.001 m. Equivalent 64- and 128-source-cell cases defeat all smaller power-of-two rules. This is a continuous bilinear surface, not a discontinuous custom sampler. See `synthetic-results.json`.
2. **Consecutive-rule tests fail on real data too.** With an acceptance threshold of 0.25 m, N2–N4 accepted 399 probes, but 13 had N4–N64 residual greater than 0.5 m (maximum 2.392 m). N4–N8 accepted 468, with 10 residual failures (maximum 4.326 m). N8–N16 accepted 592, with 5 failures (maximum 0.766 m). See `diagnostics.json`.
3. **Source-spacing ratios are a necessary warning signal, not a sufficient error estimate.** Even target pixels smaller than one source cell can cross a steep source-cell boundary. The L6 K2 case has a footprint span of about 0.364 source cells along the largest geographic bounding axis, yet N2–N256 differs by 14.588 m. Heuristics using only 1×, 2× or 4× geographic source-span multipliers still left substantial errors in this sample. `real-results.json` records these diagnostic choices; choices capped at N64 are biased by comparing with the same N64 reference and are not candidate guarantees.
4. **Poles cannot use a single equatorial spacing ratio.** A target footprint adjacent to a pole spans a large longitude interval. Its east-west source-cell size collapses with latitude, and the global ENVI sampler extends the closest source-latitude centre across the half-cell polar cap. Longitude variation in that row remains in the baseline interpolant. The pz/L8 pixel i=j=32768 (~89.999029°N, 135°E) has N8–N256 residual 1.118434 m, despite a very small physical target footprint. The similar residual at L6 is therefore unsurprising. A corner-based longitude-span heuristic is also deliberately conservative and singular at the exact pole; do not treat that diagnostic as a robust production bound.
5. **Seam aliases must share all decisions.** Aprons are aliases of canonical face/integer footprints. Any future adaptive decision must be derived from the canonical address and identical source metadata, not the tile-local position or a tile-wide maximum. Otherwise identical seam footprints can select different N and create cracks after metre rounding.

## Recommended quality policy

- Separate three controls: the backend (performance only), explicit N (reconstruction/integration choice), and the quality-validation tolerance. Changing backend must preserve the configured sampling contract.
- Do not reduce N because a tile is mostly flat, because N2 and N4 agree, or because global mean source spacing exceeds target spacing. Require difficult spatial probes and a source-support analysis.
- For a future adaptive integrator, enforce a source-aware floor, inspect all source cells that can influence a footprint, handle poles/antimeridian geometrically, and refine steep/source-boundary regions using a conservative error bound or independently verified reference. Offset/jittered deterministic rules can expose some aliasing but cannot establish a strict bound by themselves.
- If a requested tolerance cannot be supported within N≤16, report the unresolved case rather than silently accepting it. A larger N cap or another integrator would be a separately reviewed sampling change, with manifest provenance and reproducibility tests.
- A 0.25 m empirical quadrature budget can be useful for diagnostics alongside 0.5 m maximum metre-rounding error, but adding them is only meaningful when the quadrature error is actually bounded. This study supplies convergence evidence, not that bound. Arbitrarily small numerical differences can still cross a half-metre rounding threshold; preserving integer output requires direct parity checks.

## Regression cases to retain

- Bilinear narrow-ridge fixtures with 16, 64 and 128 source cells per target; check the expected false-convergence behavior and prohibit adaptive acceptance based solely on low-N agreement.
- Real Aconcagua: ny/L4/i2957/j487; Everest: py/L4/i1908/j3323; K2: py/L4/i1434/j3715 and py/L6/i5738/j14861. Use the exact addresses in `probes.csv` rather than infer them from names.
- North-pole adjacent pz/L6/i8192/j8192 and pz/L8/i32768/j32768; corresponding south-polar, antimeridian and full-face edge/corner rows in the CSV.
- Constant positive/negative heights, signed half-metre rounding thresholds, affine/bilinear source grids, source-cell boundaries, valid extrema and source nodata/nonfinite rejection.
- All 12 cube edges in both directions and all 8 corners, interior-tile aprons, canonical owner identity and identical rounded output across scalar/optimized backends at the same N.
- Whole-tile byte/integer parity at N1, N2, N8, N16, including at least one steep, coast, polar and face-edge tile; keep performance measurements separate from quality comparisons.

## Timing and memory

The first scalar study, including input hashing, 807 N1..N64 probes and 37 selected N128/N256 extensions, took 46.395 s with peak RSS 212,408 KiB (~207.4 MiB). Pure per-N scalar timings across the same 807 probes were N1 0.0107 s, N2 0.0238 s, N4 0.0838 s, N8 0.3256 s, N16 1.2832 s, N32 5.0949 s and N64 20.4249 s. The additional 40 N128/N256 probes took 20.671 s, peak RSS 25,116 KiB. The five N512/N1024 reference checks took 40.521 s, peak RSS 18,608 KiB. Total time across these three sequential stages was 107.588 s.

These are local warm/cold-cache mixed diagnostic measurements, not full-build throughput estimates. RSS includes resident memory-mapped source pages; the 933 MB source is not copied into a Python array by the scalar oracle. Results are subject to concurrent executor load. The separate performance benchmark is the appropriate source for backend speedup and full-tile working-memory claims.

## Reproduction and evidence

Run `study.py --synthetic`, then `study.py --source <verified ENVI binary>`, `deepen.py <binary>`, `reference_check.py <binary>`, and `summarize.py`. The `.hdr` must be adjacent. These scripts write evidence only alongside themselves and read the pinned baseline implementation. No network calls, downloads, repository writes, full-global builds or external publication occur.

Files: `probes.csv`, `deep-probes-all.csv`, `real-results.json`, `deep-results.json`, `reference-check.json`, `synthetic-results.json`, `diagnostics.json`, `oracle-check.json`, and the scripts/frozen baseline. Source data are deliberately excluded.
