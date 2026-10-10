# Foliage draw-range extension

Local review work on `codex/menu-hangar-review-20261009`, based on `473d420`, 9 October 2026. Requested scope: keep foliage detail farther away at each level-of-detail (LOD) change. Target remains native 1920×1080, render scale 1.0, RTX 3070, minimum 60 FPS. **This patch has no target-GPU timing result.**

## Distance policy

All distances below are metres from the camera to the instance anchor, using the renderer's existing 3D distance. At an exact boundary, the next detail level is selected. Quality presets remain ordered. Bushes keep the existing 0.6× near and 0.5× middle tree thresholds.

| Preset | Tree LOD0 end, before → after | Tree LOD1 end, before → after | Tree cull, before → after | Bush LOD0 / LOD1 / cull, before → after |
|---|---:|---:|---:|---|
| Low | 190 → 210 | 900 → 1000 | 2600 → 2900 | 114 / 450 / 800 → 126 / 500 / 900 |
| Medium | 260 → 290 | 1300 → 1450 | 4500 → 5000 | 156 / 650 / 1300 → 174 / 725 / 1450 |
| High | 360 → 400 | 1800 → 2000 | 7000 → 7800 | 216 / 900 / 1800 → 240 / 1000 / 2000 |

Every main-view foliage switch/cull moves outward about 10–12.5%. The proposal deliberately avoids doubling the expensive near-geometry footprint.

The shared policy is `src/entity_lod.h`, used by the production renderer and the existing CPU foliage test. Chunk-wide and individual-instance selection use the same boundary helper. Streaming/prefetch/retention continue to derive their detailed-chunk radius from the extended foliage range. The terrain canopy blend still receives that same tree range through `entTreeFar`; no separate old cutoff is left in the main view.

Unchanged: mesh geometry and its per-kind vertex budgets, placement/density/seed order, CPU/GPU distance thinning, material resolution, foliage animation, rocks/buildings/fixtures, resolution scaling, shadow radii/map sizes and cached-shadow policy. The near shadow cascade explicitly retains its old LOD switch, rather than silently increasing shadow geometry alongside view detail. Far-cascade LOD selection is unchanged. Auxiliary camera feeds keep the entire original Low range policy, independently of the main view's quality.

The existing hard LOD switches and deterministic far thinning remain; this is not a cross-fade or a claim to remove all popping. Existing model silhouettes are preserved and switch farther away. Inspect movement across all boundaries before accepting the visual result.

## Cost estimate, not a frame-time result

Raising a radial distance by roughly 11% covers roughly 23% more unthinned ground area. The main-view detailed streaming footprint, including its existing 300 m padding, grows by **21.76% / 21.92% / 23.12%** on Low / Medium / High. The wider building/large-rock streaming scan is unchanged. Extra detailed chunks can increase generation, memory and first-visit loading/streaming work even though distant foliage thins out.

The regression test also computes a reproducible flat, full-circle, uniform-density model for each of the seven foliage kinds. It integrates the existing survival fraction `min(1, (80 × nominal height / distance)^2)` and weights each LOD's annulus by its actual mesh vertex count:

| Preset | Additional surviving instances, per-kind min–max | Additional surviving mesh vertices/triangles, per-kind min–max |
|---|---:|---:|
| Low | 5.32–11.08% | 10.99–17.18% |
| Medium | 4.04–6.87% | 8.73–16.92% |
| High | 3.48–5.48% | 8.01–14.24% |

These are geometric estimates, not measured CPU submission, visible pixels, GPU time, VRAM use or FPS. Actual species mix, terrain, altitude, frustum/occlusion, partial-chunk bulk submissions, cutout overdraw and shader-side rejection change the cost. They are not a substitute for timing. The change does not assert that the user's 60-FPS minimum is met.

## Regression and acceptance

`environment_foliage_test` now checks exact main-view distances, preset monotonicity, both sides of each LOD boundary, retained detail at old switch distances, bush scaling, original camera-feed and shadow policy, unchanged non-foliage distances, conservative analytical cost bounds, and the existing deterministic mesh/ABI/geometry/leaf-coverage contracts. No new CMake target is required.

Focused checks on this local patch passed: native foliage test; a fresh-source ASan+UBSan build/run of that test; native syntax compilation of `entity_render.cpp`; and LLVM-MinGW Windows x64 syntax compilation of `entity_render.cpp` plus the test. The foliage test retains 30,036 budgeted environment vertices and passes 911,400 exact leaf-coverage comparisons. These are CPU/compiler checks, not rendered transition screenshots or Windows execution. `git diff --check` is clean. Aggregate branch checks are separate.

Focused native verification:

```sh
cmake --build BUILD --target environment_foliage_test
ctest --test-dir BUILD -R '^environment_foliage$' --output-on-failure
```

A standalone sanitizer check, without starting an aggregate build:

```sh
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -fno-sanitize-recover=undefined -Isrc tests/environment_foliage_test.cpp \
  src/entity_mesh.cpp src/entities.cpp src/world.cpp src/scenery.cpp \
  src/airport_scenery.cpp -pthread -o /tmp/environment_foliage_sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/environment_foliage_sanitized
```

Leak detection is disabled for the sandbox's existing LeakSanitizer limitation, not because a leak result passed. The full native suite and Windows application build belong to the combined branch validation.

Hardware acceptance follows [the environment benchmark contract](ENVIRONMENT_BENCHMARK.md): A/B the same native-1080p settings and Low/Medium/High presets, include Cedar Ridge moving first-visit/revisit forest runs, low-angle canopy and bush transitions, rapid camera reversals, and cockpit/exterior/feed switches. Check the terrain-canopy handover at each far limit and shadows before/after crossing LOD boundaries. Record frame-time tails, dropped/missing display observations, streaming/loading hitches and memory, not just averages. Do not lower density, effects or render scale to claim this extension meets 60 FPS.
