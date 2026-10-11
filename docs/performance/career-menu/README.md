# Career menu CPU frame-drop fix

Base: released v3.46.0, `72d0af0b23defb8067dd697f87d94384b39a412b`. Investigation and local validation: 11 October 2026. The user's report was career-menu FPS below 60, while the research menu seemed fine. This patch is source-only and has not been published or merged.

## Verified cause

`Game::drawHubContracts` previously ran `Career::plan` every frame for the Fly/Overweight gate, even though the earlier details block separately cached its displayed estimate. `Career::plan` constructs a plane and calls `apEngage`; its arrival planner normally evaluates both runway directions and the selected direction again. Those searches scan terrain, iterate holding-pattern candidates, and inspect scenery obstacles. `Scenery::obstacleTop` also synchronously ensures destination chunks exist.

The research menu does not call that career launch-planning path. Career tabs still have live 3D backgrounds, so GPU or scenery rendering can be a separate source of frame time.

## Changes

- A per-Game immutable raw launch-plan cache serves both the details and post-chooser gate. Unchanged frames perform no full route planning.
- The exact existing calculation runs once when its inputs change. No approximate time/fuel formula or visual-quality reduction is introduced.
- The completed background flight quote, continuation fee waivers, current selected tank, uplift costs and net are applied to a fresh copy. Fuel arrows do not invalidate the expensive route estimate. A same-frame aircraft change is picked up by the gate.
- Displayed/default fuel and the overweight gate now use the same quote-refined estimate, as final launch already does. Previously the gate silently used the quick estimate even after the displayed quote changed.
- Explicit scalar serialization captures route, waypoints, aircraft/source, weather, payload, economic inputs and damage. It avoids rounded float keys, raw-struct padding, lossy hashes and cross-Game static state.
- The asynchronous flight quote uses the same exact contract identity. A late result stays attached to the request that started it; a changed same-ID forecast/route cannot overwrite the new estimate.
- Flight simulation, renderer, native resolution, assets, save format and `finalizeLaunchPlan` calculation are unchanged. The prior state tests now inject their quote fixture using the updated key.

The key is deliberately conservative. Some unrelated fleet or airport-state changes can cause an extra one-time recalculation; stable frames do not.

## Measurements

These are CPU-only Linux observations on a shared host, GCC 14.2.0, Release/O3. No window, GL context, GPU rendering or presentation occurred. They do not measure an RTX 3070 and do not establish a 60-FPS minimum.

First, 100 repetitions of the unchanged full planner per fixture, after learning aircraft performance:

| Fixture | Warm median, ms | Warm p95, ms | First plan, ms |
|---|---:|---:|---:|
| L1 | 15.231 | 17.174 | 69.442 |
| C1 | 16.440 | 23.846 | 18.222 |
| P1 | 14.250 | 19.421 | 52.424 |
| A4 | 8.209 | 8.829 | 68.376 |

Then actual production `drawHub` UI generation at a 1920×1080 layout, three warm-up frames and 120 timed frames per fixture, completed quotes pre-seeded to isolate steady UI generation. GNU link wrapping counted every `Career::plan` invocation:

| Fixture | Before calls | After calls | Before mean, ms | After mean, ms | Before p95, ms | After p95, ms |
|---|---:|---:|---:|---:|---:|---:|
| L1 | 120 | 0 | 17.134649 | 0.056193 | 19.958302 | 0.122517 |
| C1 | 120 | 0 | 14.974586 | 0.086348 | 16.132867 | 0.109443 |

Call counts establish elimination of repeated work independently of shared-host timing noise. Baseline production paths used here were checked byte-for-byte against the v3.46.0 base. The baseline build also contained the independent prebuilt-asset loader work, which does not execute in this no-GL test.

## Validation

- `menu_plan_cache_test`: 624 checks, zero failures. Every LaunchPlan field matches a fresh production calculation; 200 unchanged helper calls and 120 real Contracts UI frames rebuild zero plans.
- Coverage includes fuel edits, aircraft/source changes, changed same-ID endpoints, exact waypoint edits, continuation index/departure, weather/forecast, payload, owned tank/location, positioning threshold, scenery/terrain damage, nonzero fee waivers, changed job aircraft/source, quoted fuel crossing the overweight threshold, rental full tanks, owned fuel floor, over-cap clamp, late old quotes and independent Game instances.
- All four existing gameplay regression partitions passed with zero failures, including quotes, fuel, saves, continuation and launch-state coverage.
- The Windows x64 LLVM-MinGW production executable and focused-test executable both cross-build/link. Native Windows execution was not run.
- The registered `menu_plan_cache` CTest test passes with the unchanged world fixture reused. Details are recorded in `validation.json`.
- This is focused validation, not a claim that the entire repository test suite or native Windows hardware/runtime was exercised.

## Hangar follow-up

The Hangar does not call the route planner or start flight-time/fuel simulations while drawing. Its refill-price display is arithmetic; showroom room geometry is cached by aircraft size. The actual preview CPU preparation and production Hangar UI were exercised together with the full 11-aircraft owned fleet:

| Case | Frames | Full plans | Quote jobs started | Mean CPU, ms | p95 CPU, ms |
|---|---:|---:|---:|---:|---:|
| Unchanged Hangar | 120 | 0 | 0 | 0.060305 | 0.095106 |
| Switching through all 15 airframes | 120 | 0 | 0 | 0.048820 | 0.077274 |
| Changing owned fuel | 120 | 0 | 0 | 0.059844 | 0.077403 |

These CPU checks required no Hangar implementation change. They are now regression-covered. The real showroom still renders aircraft shadows, lighting and post-processing each frame; those GPU costs were not measured, and these numbers are not total frame time or an FPS guarantee.

## Reproduction

Build normally in a separate build directory. The focused regression is registered as the `menu_plan_cache` CTest test and is a dependency of the existing gameplay test target so legacy explicit CI target lists build it.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target menu_plan_cache_test gameplay_test
ctest --test-dir build -R '^menu_plan_cache$' --output-on-failure
```

The adjacent probe sources reproduce the CPU measurements. `plan_probe.cpp` links the project's environment/core objects and takes the test-world cache path as its argument. `ui_cpu_probe.cpp` links environment/core/game/entity-mesh objects and `src/radio_stub.cpp`, with include paths for `src` and `tests`. On Linux add `-ldl -pthread -Wl,--wrap=_ZNK6Career4planERK8ContractiNS_6SourceE`. Define `CACHE_PATCH` for the candidate; omit it against the unchanged baseline. Set `SOLACE_TEST_WORLD` to a valid `solace-test-run` cache from that same world version. The UI probe makes no rendering calls.

## Remaining limits

- Opening or changing a genuinely new route still pays one full-plan calculation; measured first calls above were 18–69 ms. This change addresses repeated steady-frame work, not all selection/startup hitches.
- The existing background autopilot simulation still runs once per uncached quote and may temporarily compete for CPU time. Its existing shared-world/scenery concurrency design is unchanged.
- Live outdoor rendering, streaming, presentation pacing and native-GPU timing remain separate. If the career screen still dips after integration, F3 on that screen shows frame time, GPU-pass times and scenery CPU time for the next diagnosis.
- Upstream has newer settlement/world work after v3.46.0. Review and integrate this small patch onto that work; preserve its unrelated changes and rerun tests there.
