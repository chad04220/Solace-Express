# Claude handoff: offline Earth sampling performance and quality

Base: `7954f2b71567021b207c9ddb28f3e2e1591d922c`.
Local branch: `codex/earth-sampling-performance-review-20261012`.
Scope: offline producer only. Publication/merge is not part of this local handoff.

## What changed

1. Explicit `--sampler numpy` for validated ENVI inputs; standard-library scalar
   stays the default. Fixed 4,096-cell batches, zero-copy read-only source mmap,
   identical configured N, midpoint positions, canonical seams and codec.
2. Scalar fallback before source-grid boundary gathers preserves nodata/coverage
   semantics; scalar complete-cell fallback around rounding ties preserves metre
   decisions. No adaptive-N policy, downsampling shortcut or parallel writer.
3. Fifteen focused optional-sampler tests, CTest registration, a reproducible
   benchmark CLI and a dedicated Linux/Windows synthetic CI workflow. Pinned
   NumPy is required in that workflow rather than silently skipped; dependency-free
   scalar checks remain separate. Existing game CI is untouched.
4. A bounded independent scalar quality study, with frozen baseline oracle and
   807 probes plus selected N256/N1024 references. No production N/default change.

## Verified results

- New-process, same-host 20-tile L4 N2 CLI builds: scalar 36.74 / 37.34 s;
  NumPy 2.39 / 2.34 s. Approximately 15.7× including startup, source hashes,
  compression, writes and final verification. Warm filesystem, shared host.
- All 22 files from every real sample rebuild match the previously delivered
  scalar pack byte-for-byte, including manifest. No pack/schema/version changes.
- Twenty L4/L6 N2 tiles × three repeats: zero quantized mismatches, identical
  encoded payloads/repeats, maximum unrounded difference 7.15e-10 m.
- Four full N8 terrain/polar tiles also match every encoded byte.
- Main checks: 15/15 required-NumPy, 4/4 portable, 57/57 Earth, 38/38 handoff;
  focused CTest 4/4. Independent audit adds 3,840 varied-N real probes and nine
  full-tile endian/offset/rounding-limit cases. Claude's ENVI diagnostics preserved.

## Review focus

Read [performance details](EARTH_SAMPLING_PERFORMANCE.md), then inspect
`tools/earth/sampling_numpy.py`, `tests/earth_sampling_test.py` and the small
`elevation.py`/`pack.py`/CLI wiring. The initial independent audit found an important
source-stencil boundary discrepancy; the final implementation checks before the
vector gather, not merely after quantization. Review both directions of that
nodata regression and the complete pz/L12 tile reproduction.

[Quality findings](EARTH_SAMPLING_QUALITY.md) are separate from the acceleration.
N2 remains a compatibility default, not a terrain-accuracy guarantee. At sampled
L4 steep terrain N2–N256 reached 41.80 m; N16–N256 reached 0.949 m. A valid narrow
bilinear ridge can have N2=N4=N8=0 while N16 finds about 625 m. Consecutive-rule
agreement or mean physical source spacing cannot alone justify adaptive reduction.

## Remaining boundaries

- No full-global pack or compressed global-size measurement.
- Total memory includes potentially resident source-backed mmap pages; 3.5 MB is
  measured temporary sampling allocation, not a whole-process memory claim.
- Native Windows execution and remote CI for this local revision are not yet
  verified. Run both dedicated optional-sampler jobs and normal repository gates
  after publication. Same-environment identity is measured; cross-platform libm
  byte identity is not proven.
- No runtime game, globe, airports, scenery, saves, source-data conversion or
  coordinate/datum behavior change.

## Claude's review (2026-10-12)

Merged into `claude/compassionate-davinci-4cfo20` after the military career review (`3454f98`); the merge was clean.

- Read: `sampling_numpy.py` (fixed 4,096-cell batches over a zero-copy view of the ENVI mmap; the scalar path taken
  before any gather for sub-cell coordinates near source-grid lines and coverage edges, and after it for values near
  a half-metre rounding tie or the height limits; the scalar `sy`/`sx` summation order kept), the
  `sample_cell`/`sample_elevation` split in `elevation.py` (the scalar reference unchanged, NumPy imported only when
  asked for), the `--sampler` wiring, the CTest registration and the new path-filtered workflow.
- Run here, NumPy 2.4.6 (newer than the pinned 2.3.5): `earth_sampling_test.py --require-numpy` 15/15, the portable
  sampler without site packages 4/4, `earth_pack_test.py` 57/57 (2 skipped: no `zstandard` here), `usgs_world_map_test`
  38/38; the merged tree's 122 CTest suites and the MinGW cross-build.
- Not reproduced here: the timing study (it needs the real global ENVI source). Offline tooling only; the game is
  untouched.

