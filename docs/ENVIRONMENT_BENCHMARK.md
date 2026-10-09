# Environment performance validation: native 1080p / RTX 3070

Original proposal base: `a7f8d30ec39c89dacded275e33a8750d6719138c`. This branch integrates it onto v3.39.0 (`e9ec52d5eae709c25ffb5e804af6c968f9ffc2b4`) plus the release-note reset and stationary XR-40 storm diagnostic through `6f0eb23e3ffb5925c60f3e2befa2a1fa3fcd743d`. See CLAUDE_ENVIRONMENT_REVIEW.md for integration checks and the XR-40 support fix. **No RTX 3070 result has been measured here.** Software Mesa images, Linux CPU tests and successful Windows compilation cannot establish target-GPU frame rate.

## What the new benchmark measures

The existing command remains valid and still defaults to 120 measured frames:

```bat
SolaceExpress.exe --bench air,storm,night --size 1920x1080 --fullscreen --out bench.txt
```

For a longer capture with raw data:

```bat
SolaceExpress.exe --bench apv_CAP_1_12 --size 1920x1080 --fullscreen --bench-frames 7200 --bench-csv --out bench_airport.txt
```

The report and `bench_airport.txt.frames.csv` contain:

- QPC wall times for game update, render/driver submission, SwapBuffers and the message pump/metadata collection. The `frame_wall_ms` column measures work inside one loop iteration; it is not CPU-active time.
- `present_interval_ms`, from one SwapBuffers call start to the next. It includes intervening game/UI work and driver blocking. Its first row is `NA`. **It is application presentation cadence, not proof of delivery to the display.** The CSV also exports raw frame-start and present-start QPC timestamps; the report gives the QPC frequency for trace alignment.
- Raw, delayed **renderScene-only** GPU query samples, with monotonically increasing sample IDs and originating renderer-frame serials. A row with no fresh in-range completion has `NA`; the previous result is never repeated as a new sample. These timers exclude the later UI draw and presentation. The final pending queries are not force-read. Sample coverage and query-overwrite counts are reported; missing samples can hide slow frames and preclude a strict GPU-tail claim.
- Min, nearest-rank median/p95/p99/max and mean for each stream. App-present 1% low is 1000 divided by the mean of the slowest ceil(1% × sample count) intervals; it is not the reciprocal p99. The number above 1000/60 ms is explicit.
- Actual client/render dimensions, scale and quality per frame; `native_1080p_valid` requires a 1920×1080 client and renderer at scale 1. The benchmark explicitly fixes scale to 1 and does not run dynamic resolution.
- Final preset weather and measured-end weather, actual quiet-warmup completion, scenery/terrain-shadow pending flags, mesh-bake counts and scene status. A warmup timeout, cancelled run, crash or changed scene marks the capture incomplete. A non-1080p run is still allowed for legacy diagnostics but cannot pass the native-1080p check.

Vsync-off is requested, and whether WGL accepted the request is reported; the benchmark does not invoke the game's software limiter. Driver settings and desktop composition may still pace it. One `glFinish` before and one after the batch preserve the old aggregate throughput measure; **no per-frame or per-pass GPU wait is added**. The aggregate includes the final drain and is different from the per-frame CPU/app-present distributions. The old summary line is retained for compatibility; its GPU total is a last delayed snapshot and its pass values are EMA snapshots, not whole-run percentiles.

`--analyze` is diagnostic only. Its formerly “exact GPU passes” label is corrected to serialized CPU-submit-plus-GPU-wait wall time. Waiting between passes disrupts normal overlap. The sync-debug query-readiness guard also prevents polling timestamp queries that `syncTiming` did not issue.

### Weather scene syntax

Plain comma-separated scene lists retain their meaning. Weather presets use nine comma-separated values, so separate weather-bearing scenes with semicolons:

```bat
SolaceExpress.exe --bench "look_0_-4000_200_15_600_5_16@0.95,600,1,1,20,200,8,0.35,16;apv_CAP_1_22" --size 1920x1080 --fullscreen --bench-csv
```

Weather order: cloud cover, cloud base metres, precipitation (0 none / 1 rain / 2 snow), storm (0/1), wind knots, wind-from degrees, gust knots, turbulence, hour. Incomplete, nonfinite or comma-ambiguous weather lists are rejected instead of becoming accidental extra scenes.

Important existing preset limitation: `apv_`, `wcam_`, `apt` and `loadshot_` start another flight after the general WX override and can reset that weather. The bundle uses `look_` for wet/storm cases and checks the *actual* post-preset weather. A wet surface currently follows `wx.precip == 1`; this is not an independent “rain stopped but asphalt stays wet” simulation. Research presets that return early can also bypass WX. Do not assume an arbitrary preset supports an override.

## Repeatable environment case bundle

The Python standard-library runner lives at `tools/environment_benchmark.py`. A dry run is the default:

```bat
python tools\environment_benchmark.py --exe C:\Games\SolaceExpress\SolaceExpress.exe --out C:\Benchmarks\solace-run-01 --commit COMMIT_SHA
```

Add `--run` to execute. `--frames 7200 --repeats 3 --quality 1` are the defaults; quality 1 matches the reviewed baseline (Medium). Native resolution does not imply maximum quality. Repeat with `--quality 2` (High) as a separate optional check if that is the desired preset. Test both the original and candidate build with the **same explicit quality**. Do not call a lower-quality comparison a full-quality optimization. Use `--case airport_storm`, etc., for a targeted rerun. The output directory must be new, so prior evidence is preserved.

The runner isolates the child's APPDATA/settings, sets 100% resolution, traffic on and FOV 55, clears known inherited debug overrides, hashes the executable and captures read-only GPU/CPU/driver/display/power context. It does not install anything, modify the user's normal settings, remove caches, or change drivers/power settings. The game's ordinary shader/body cache beside the executable can still be built/updated. A caller-supplied source SHA is labeled as such; validate it against the actual build artifact.

Each repeat starts a fresh process per case and alternates case order. All-body preparation and quiet warmup remain outside the measured window. Report elapsed duration as well as frame count: 7200 fixed 1/60-second simulation updates do **not** guarantee 120 seconds of real elapsed capture when running uncapped. Preset camera coordinates are repeatable; asynchronous warmup length and time-advancing simulation mean this is not a bit-exact replay. The final eleven fixtures each passed a fresh-process CPU-only 140-second lifecycle check (maximum warmup plus the default measured simulation duration), in addition to a same-process sweep. This does not verify rendering or frame rate. No automatically chosen angle is claimed visually validated until screenshots confirm the intended content. A crash/scene change still rejects a long case rather than accepting a suddenly cheaper menu, including runs longer than the verified default.

| Case | Coverage | Anchor / limitation |
|---|---|---|
| capital_day | Dense buildings, roads, urban shadows | Capital town (-3200, -1200), source-defined city |
| port_verde_day | Second dense city / port context | Port Verde (-29500, 10500), source-defined city |
| forest_low_view | Low-height foliage view | (-24000, -5000), Cedar Ridge; static camera, not a moving fly-through |
| kaleo_rocks | Mixed vegetation and rock formations | (23000, 10000), CPU population probe, view confirmation pending |
| fjord_rocks | Rocky northern mixed scenery | Same camera position/direction as the look-view of (18000, -28000), converted to wcam to keep the aircraft parked; view confirmation pending |
| gull_rock_coast | Small island, coast/water/rock context | Gull Rock (3000, 31000), airport anchor, close formation coverage to inspect |
| airport_day / airport_night | Terminal-height geometry, windows, lighting | CAP `apv` view 1 at noon / 22:00 |
| airport_wet / airport_storm | Wet materials, dense clouds, precipitation/lightning | Same CAP look-camera and weather recorded in the log |
| airport_wet_night | Combined wet lighting/cloud/airport stress | Same view at 22:00 |

The original fjord look_ fixture crashed its offscreen aircraft after 41.63 simulated seconds. It is replaced with `wcam_17886.990_-27578.244_293.345_15.000_-14.000_12.000`, which preserves the camera position and direction and survived the 140-second CPU probe. The wcam preset uses its own clear weather (cloud cover 0.15, visibility 60 km, wind 4 m/s from 200 degrees), rather than the old look_ override (cloud cover 0.25, visibility 40 km, wind 8 knots). Use this exact replacement for both A/B builds; do not mix old and new fixture results. No production physics or debugScene behavior was changed.

The forest/rock population probes identify dense candidates, not GPU cost. Camera orientation/occlusion can make an apparently dense location cheap; inspect one native screenshot before timing, and preserve that view for both builds. Capture screenshots outside timed batches.

## RTX 3070 test contract

1. Use the actual RTX 3070, record desktop/mobile variant and VRAM, CPU/RAM, Windows build, driver, monitor refresh/VRR, HAGS and driver frame-cap/vsync settings. Confirm the game startup log names the intended GPU. Do not silently alter system settings to improve numbers.
2. Use a release build and native 1920×1080 client/render targets, scale 1.0, automatic scaling off. Keep all declared effects/materials/traffic, FOV and quality constant. A 1080p window on a larger monitor is 1080p rendering, not evidence of a native 1080p panel or unchanged display composition.
3. Warm shaders/body caches once without deleting the original caches, then thermally settle with normal gameplay. Record cache status and any compiler/fallback warnings. Keep overlays/recording/background load fixed; do not record video during the timing capture unless both builds use it.
4. Run at least three repeats. Retain every run, all outliers and full raw CSVs. Report each case and the worst repeat, not a pooled average that hides the worst location. If the run is shorter than 120 wall seconds, add a longer capture or a separate sustained session.
5. Do two separate tests: uncapped throughput/bottleneck diagnostics and real gameplay with the user's normal 60-FPS pacing. A 60-FPS cap masks performance headroom; an uncapped test can submit frames that the display never shows.
6. Pair app/GPU data with a display-presentation trace and a separate moving/streaming run below. Verify native-resolution invariants and a complete sample count before interpreting performance. Incomplete data is **inconclusive**, not a pass.

### Real display cadence: external capture

An already installed official [PresentMon console release](https://github.com/GameTechDev/PresentMon/releases) can observe the Windows presentation stream. Pin and record the executable version/hash. The documented CLI/column reference is [PresentMon 2.3.1](https://github.com/GameTechDev/PresentMon/blob/v2.3.1/README-ConsoleApplication.md); check the selected version's help rather than assuming all versions match.

Start the game, get its PID, then capture only that process for 120 wall seconds, with GPU/display tracking left enabled:

```bat
PresentMon.exe --process_id GAME_PID --output_file display.csv --qpc_time --delay 5 --timed 120 --terminate_after_timed
```

Capture startup/warmup separately or delimit the measurement using QPC timestamps. Do not include unrelated process/swap-chain streams. Keep undisplayed rows; do not use `--exclude_dropped` to conceal them. Use display dwell (`DisplayedTime`, or the documented equivalent for the pinned version) for delivered cadence, and app-present intervals as a separate series. Missing display data is not zero. On an OpenGL application, runtime instrumentation can be less complete; externally inferred CPU timings must not be treated as direct game-update timings. Save the schema and unavailable-field counts. These are software display-presentation observations, not photon/input-latency measurements.

At a true 60-Hz fixed-refresh display, tiny clock/quantization variation is expected; 59.94 Hz is inherently below exactly 60. Report the actual refresh and raw intervals. Do not hide the difference by silently widening the target threshold.

### Offline analysis

The portable, standard-library tool is `tools/validation/analyze_frames.py`. Select the metric explicitly; it never guesses that a CPU/GPU duration is displayed frame cadence:

```bat
python tools\validation\analyze_frames.py display.csv --metric DisplayedTime --process-id GAME_PID --out displayed-statistics.json
python tools\validation\analyze_frames.py bench_airport.txt.frames.csv --metric present_interval_ms --out app-statistics.json
```

Use `--scene` for a multi-scene internal CSV and `--swap-chain` if a process has multiple presentation streams. Existing output files are not replaced. Missing/zero/undisplayed values and source hashes remain visible. The tool does not issue a native-1080p or 60-FPS verdict.

## Movement, streaming and transitions: still required

The warm fixed-camera bundle deliberately excludes startup and much of streaming, so it **cannot certify the user's minimum-60-FPS requirement on its own**. Run these real gameplay paths at 100% native scale, same aircraft, same heading/route/speed/weather and same view in both builds. Record route/video separately from timing if capture overhead is material.

- Dense city: a low circuit across Capital, followed by Port Verde in a fresh session. Include a near-building turn, looking toward the longest urban sightline, and a 180-degree camera reversal. Keep a safe, recorded AGL clearance and speed.
- Forest: Cedar Ridge (-24000, -5000) at 50–100 m above the tallest canopy where terrain clearance permits. Cross multiple scenery chunks and tree LOD thresholds; approach, fly through, then reverse so both first visits and revisits are measured. Record actual AGL and path, not an assumed constant MSL height over hills.
- Rocks/coast: a low approach along the Kaleo/Fjord candidates and a Gull Rock coast pass, including land-to-water and water-to-land view changes. Include big formations at near distance and long shadow views.
- Airports: CAP approach/low pass, taxi/apron/terminal sweep, departure. Repeat daylight, night, wet and storm/night. Maintain comparable traffic and camera state; capture glass/window/emissive/light effects as well as the runway.
- Streaming/transitions: fresh-process first visit and same-session revisit; city ↔ forest ↔ coast; quick camera reversals; exterior ↔ cockpit; menu/hub ↔ loading ↔ flight; airport arrival and departure. Include a normal-speed session of at least several minutes and capture hitches rather than warming them away. Time loading/compile phases separately from active gameplay, but retain them in the evidence package.

For deterministic engineering replay, a future dedicated route driver should fix input/waypoints, random seeds, aircraft state, world/weather/time/camera, explicitly reset or preserve caches by phase, and log first/last state. The current `wxfly_...` autopilot presets use MSL altitude, pre-simulation and variable warmup, so do not mistake them for a route replay or silently use them at unsafe terrain heights. No new replay engine was introduced in this instrumentation patch.

## Reading the result and deciding what to fix

- A mean of 60 FPS is insufficient. Publish median/p95/p99/p99.9/max intervals, 1% low, frames over 16.6667 ms, and counts at ≥33.333 / ≥50 / ≥100 ms, per case/repeat. State whether each value is CPU wall, app presentation, renderScene GPU or delivered display cadence.
- A literal observed “never below 60 FPS” result requires every valid active-gameplay delivered interval in the declared capture to be within 1000/60 ms, with complete tracking and verified settings. A p99 or 1%-low target is a different, weaker condition and cannot be substituted without agreement. A finite successful capture still cannot guarantee all future play.
- Use p95 CPU update/render-submit and GPU work as headroom diagnostics, ideally below roughly 14 ms for each busy critical path. This is an engineering margin, not a measured result or an alternative acceptance threshold. CPU and GPU overlap; do not add their distributions to manufacture a frame time.
- High update/submission time with lower GPU work suggests a CPU/driver/streaming constraint. High GPU work that responds to resolution suggests pixel work. High swap/display wait can be pacing/compositor behavior. Correlate expensive frames by origin serial/QPC, pending chunks, bakes and scene transitions before deciding.
- Run resolution/feature-off experiments only as separate, labeled diagnostic tests. Native 1080p with all chosen features stays the acceptance configuration. Re-test visual quality, shadows and foliage/rock silhouettes after any optimization.

## Verification of this patch

CPU-only tests:

```sh
c++ -std=c++17 -DNDEBUG tools/validation/benchmark_metrics_test.cpp -o benchmark_metrics_test
./benchmark_metrics_test
python3 tools/validation/benchmark_query_mock_test.py
python3 tools/validation/test_analyze_frames.py
python3 tools/validation/test_environment_runner.py
ASAN_OPTIONS=detect_leaks=0 python3 tools/validation/benchmark_query_mock_test.py --sanitize
```

The mock compiles the actual query-read block against fake GL calls, proving unavailable queries are not read, stale values do not receive new sample IDs, valid equal-duration samples are fresh, origin IDs survive the delay, and sync debug timing does not mark unissued timestamp queries readable. This is a data-flow test, not GPU timing validation. LeakSanitizer is disabled in this sandbox because ptrace prevents it from running; address/undefined-behavior sanitizer checks remain enabled.

Windows compilation and RTX3070 execution status must be appended to the final integration report. No OpenGL context was used for these CPU instrumentation checks.
