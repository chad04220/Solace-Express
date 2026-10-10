# Disabled adaptive near-shadow experiment

**NOT ENABLED.** The production renderer retains its original near radii of
300 / 420 / 520 m and original cache/fade behavior. This directory is an archived
CPU prototype and an unapplied proposal patch, not a production build target.
Do not apply the patch without a new coverage and motion review.

## Outcome (2026-10-10)

A 96 m radius at the unchanged medium 2048 map improved close eave, balcony and
car-contact shadows. However, matched natural-LOD views demonstrated that houses
and pumps at 120–180 m lost distinct ground shadows retained by the original
420 m radius. The existing far cascade could not recover those small casters.

A measured 160 m compromise still weakened a house shadow at 180 m. A 256 m
compromise preserved that shadow, but conspicuous close eave lobes remained.
Neither produced a clean overall improvement, so the adaptive runtime changes
and their production CMake test target were removed. No shader helper, caster
bias, far-cascade policy or shadow allocation was changed.

The diagnostic comparisons are discussed in
[SHADOW_READBACK.md](../../../../docs/living-islands/SHADOW_READBACK.md).
Local review boards were `captures/shadow-boundary-v8-comparison/` and
`captures/shadow-compromise-v9-comparison/` within the review output directory.
These are diagnostic capture locations, not shipped game assets.

## Archived proposal

The prototype uses main-camera AGL and exponentially smoothed camera motion,
not aircraft speed. It selects 96 / 160 / 256 / quality-base radii with asymmetric
thresholds and dwell, contracts near fade bounds for 0.2 s, commits the radius
once at a tiny positive fade, and expands for 0.3 s. Feeds and hangars leave its
state and terrain sampler untouched. Resets, quality changes, cuts and invalid
samples restore the conservative quality base.

The reversible `proposal.patch` contains the reviewed runtime integration,
controller and test as a proposal only. Its source-wiring checks require the
proposal to be applied in a separate review checkout. No file in this directory
is included by the production renderer or registered by CMake.

## Verification and limitations

Before removal, optimized and CMake-built ASan/UBSan checks passed 184,023
assertions, including production wiring. Coverage includes every quality base,
30/60/144 Hz altitude and speed ramps, threshold jitter, exact handover bounds,
feed/hangar continuation equivalence, teleports and invalid/subnormal dt,
fast acceleration during expansion, and actual-radius vehicle keys. LeakSanitizer
was disabled because the execution environment uses ptrace; this is also the
repository's sanitizer-CI configuration.

The archived standalone CPU test can be run from the repository root:

```sh
c++ -std=c++17 -O2 -pthread \
  -Itools/validation/experiments/adaptive-near-shadow -Isrc \
  tools/validation/experiments/adaptive-near-shadow/near_shadow_coverage_test.cpp \
  src/entities.cpp src/scenery.cpp src/world.cpp src/airport_scenery.cpp \
  -o /tmp/solace-near-shadow-prototype-test
/tmp/solace-near-shadow-prototype-test
```

The archived standalone invocation passed 183,999 checks after production
restoration. It deliberately skips the unapplied production-wiring contract. The CPU checks do not establish visual acceptance or native RTX 3070
60 FPS performance. Full moving-camera acceptance was paused after the static
coverage regression. At 96 m, the unchanged 0.12-radius movement refresh threshold
would be 11.52 m instead of 50.4 m at 420 m, potentially increasing refresh cadence.
No GPU-performance saving was established.
