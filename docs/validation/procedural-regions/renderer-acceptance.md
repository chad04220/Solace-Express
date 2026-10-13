# Procedural region renderer acceptance

Measured 2026-10-12 against the paired render/collision-surface build. This is a functional streaming and resource-bound result, not a hardware frame-rate certification or final landform-art review.

## Scope and method

Actual EGL rendering used llvmpipe (LLVM 19.1.7), 640×360, full scene, road traffic and scenery enabled. The explicit diagnostic setting `HARNESS_AIR_TRAFFIC=0` disabled airborne traffic. Mixed-fleet rendering is therefore **not covered** by this acceptance: earlier default-traffic attempts exceeded the process memory limit while compiling the existing generic aircraft shader.

Each measured step runs actual `Game::update` and rendering. Diagnostic pass timings synchronize with `glFinish`; they are not asynchronous GPU timer estimates. Crossing fixtures use a 5 km camera/aircraft altitude, clear 150 km visibility, and no clouds. The harness asserts continuous flight screen, advancing simulation clock, no pause/crash, position/fuel continuity, ready-only publication, and the shared upload-byte limit. Its `FUNCTIONAL_PASS` label does **not** enforce a latency threshold.

The complete three-scene process exited 0 in 182.499 s, with recorded process peak RSS 1,886,588 KiB. Production airborne traffic settings were unchanged.

## Crossing measurements

Full-frame times in milliseconds. Percentiles use the harness's nearest-rank convention: sorted sample at `ceil(p × n) - 1`. Each handoff has only one sample.

| Crossing / phase | Frames | p95 | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Generated → generated, baseline | 30 | 307.108 | 314.196 | 314.196 |
| Generated → generated, prefetch | 115 | 319.087 | 328.228 | 330.394 |
| Generated → generated, handoff | 1 | 277.665 | 277.665 | 277.665 |
| Generated → generated, after | 30 | 357.926 | 367.395 | 367.395 |
| Solace → generated, baseline | 30 | 333.885 | 343.689 | 343.689 |
| Solace → generated, prefetch | 78 | 313.009 | 330.625 | 330.625 |
| Solace → generated, handoff | 1 | 390.668 | 390.668 | 390.668 |
| Solace → generated, after | 30 | 405.899 | 422.173 | 422.173 |

- Generated crossing: `pr1-4-17-7` → `pr1-4-16-7`; commit 3.075 ms, whole update 3.634 ms, eight shadow rows at publication. Data-texture storage is **17 → 17 rows**. The previously measured 5,079 ms handoff with **16 POT → 17 NPOT** storage did not recur. Consistent NPOT padding preserves texel addresses and avoids that sampler-class change.
- Solace control: `solace` → `pr1-3-27-28`; **243 → 13 NPOT rows**, commit 2.289 ms, whole update 3.403 ms, eight shadow rows. The handoff is somewhat above its baseline, but within the following phase's range; no multi-second publication spike appeared.
- Recorded geographic motion across publication was 3.913 m and 3.933 m respectively, consistent with the advancing flight rather than a frozen handoff. Forward/backward captures retained the distant coast silhouettes across publication. The 640×360, hazed views establish persistence, not fine shoreline or material parity.

### Explicit timing qualifications

The first generated-fixture warm-up frame took **9,955.932 ms**, including first-use rendering work; it is excluded from baseline/prefetch percentiles above. The later Solace warm-up maximum was 352.035 ms in the same process. These results do not establish instant cold startup.

Combined detailed-plus-horizon uploads never exceeded **2,097,152 bytes per measured update**. Individual transfer calls are additionally capped at 256 KiB by the mocked-GL regression. The **1.5 ms time target is soft**: empty, indivisible driver texture allocation took 16.161 ms in generated prefetch frame 88 and 16.475 ms in the later Solace prefetch sequence, both with zero payload bytes transferred. A driver allocation cannot be interrupted by the row-upload scheduler. Do not describe this implementation as a hard 1.5 ms or stutter-free guarantee on arbitrary hardware.

## High-altitude neighborhood

The 30 km fixture moves only the camera; the Atlas continues normal low-altitude flight. This is a visibility stress, not a claim that the aircraft can fly at 30 km. Visibility is 200 km with no clouds.

- All **28 selected regions became ready after 18 updates / 6.601 s**. The summary's 48 frames includes 30 additional verification updates. This started with a populated cache from earlier fixtures, so it is not an empty-cache startup benchmark.
- Final resident/peak meshes: **39 / 40**, below the hard 64-mesh cap. Reported cumulative CPU mesh builds: 40. Final GPU triangle storage: **51,520,200 bytes**. Retained exact collision grids: **10,383,984 bytes**, pending grids zero; one protected proxy remained pinned.
- All 48 frames: p95 **395.317 ms**, p99/maximum **893.738 ms**. The shared 2 MiB transfer cap held throughout.
- Four saved camera directions (0°, 90°, 180°, 270°) each submitted 25 coarse meshes. Selection includes conservative horizon/altitude lookahead; submitted meshes and selected regions must not be equated with individually visible islands. Bright atmospheric haze and this resolution limit visual interpretation.

Initial neighborhood population is asynchronous and observable. The implementation does not promise that every far silhouette is present on the first high-altitude frame.

## Render/contact pairing and bounded storage

The final 26-test suite passed, including strict/fast-math surface sampling and the renderer's real-future/mock-GL upload regression. Renderer checks cover exact GPU/registry identity during low-aircraft pinning, camera relocation/eviction, cancellation of both accepted and unaccepted candidates, clearance-enabled replacement, and deferral of a previously absent unsafe proxy until both rendering and collision can activate together.

Resident collision storage is separate from GPU triangles: at most 64 × 129² × 16 = **17,040,384 bytes** for retained grids, plus a bounded two-mesh CPU construction/upload batch. The final mocked-GL sweep reached 63 resident/peak meshes and stayed within its byte/grid bounds. Windows linked successfully; these actual-driver measurements are Linux software-renderer evidence only.

## Evidence

Raw evidence bundle: `procedural-region-evidence/seamless-paired.log` and `procedural-region-evidence/seamless-paired/`, including before/after screenshots, four directional horizon screenshots, and these CSVs:

| CSV | SHA-256 |
| --- | --- |
| `seamless-pr1-4-17-7-frames.csv` | `258c987c805879e693d540eef3023e789cc60ed865b3aa4f839ae9e4b903c1bc` |
| `seamless-solace-frames.csv` | `ee7a86315ee9e2a479c0c6f3d1525d6009550770bc9f7296087669d3abbc5da3` |
| `horizon30-pr1-4-17-7-frames.csv` | `ba26aeef16b6513dfd4a4deed77246aaccf7d61b3af5e51c24fa3247f747f9a8` |

Test/build records: `paired-final-ctest.txt`, `paired-final-ctest-details.txt`, `paired-final-linux-build.log`, and `paired-final-windows-build.log` in the same evidence bundle. Subsequent terrain-recipe changes require their own visual/generation review; these numbers describe the measured paired build.

## Recipe revision 2: final fingerprinted rerun

These final captures use the rebuilt naturalistic-recipe source. The earlier measurements above remain valid for their paired checkpoint only. The invalidated `stale-harness-repeat` batch is not used here.

Harness SHA-256: `8b51b890b099d3068819ea454bf02adaf3ec845559502eb05d85dcad73b780db`. Source-manifest SHA-256: `76cf6565c554bc07bd6ac7e03227770115b33bba49e27ad7217043ee56efb70e`. All four jobs exited zero and verified unchanged source/executable fingerprints; camera jobs verified the new pitch telemetry. Full source and image hashes plus UTC capture times are in `final-renders/render-report.json`.

| Crossing / phase | Frames | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| Generated / baseline | 30 | 316.686 | 323.323 | 323.323 |
| Generated / prefetch | 115 | 326.702 | 352.656 | 352.724 |
| Generated / handoff | 1 | 266.340 | 266.340 | 266.340 |
| Generated / after | 30 | 317.226 | 332.554 | 332.554 |
| Solace / baseline | 30 | 345.347 | 349.458 | 349.458 |
| Solace / prefetch | 80 | 327.415 | 348.951 | 348.951 |
| Solace / handoff | 1 | 366.014 | 366.014 | 366.014 |
| Solace / after | 30 | 381.507 | 403.469 | 403.469 |

Both crossings retained continuous actual flight. Generated publication took 3.807 ms; Solace publication took 2.579 ms. The transfer limit remained 2 MiB/frame. First-use warm-up still reached 2.063 seconds; texture allocation remains an indivisible soft-time-budget cost. No hardware frame-rate or mixed-fleet GPU claim follows.

At the selected viewpoint, 16/28/30 km camera requests reached 21/21, 27/27 and 28/28 ready groups. Initial settling was 6.968/4.887/0.968 seconds respectively in that sequence; later cases reuse earlier cache residency and are not independent cold starts. Peak residency was 40 meshes. Final coarse GPU payload was 50,990,280 bytes and retained collision grids 10,383,984 bytes.

The five-scene streaming process took 227.966 seconds, with its own measured peak RSS 1,760,568 KiB. Separate actual 6 km overviews and 657/765 m atoll/fjord views verify revised geometry. These camera placements are review fixtures; the aircraft in the high-camera tests remains in ordinary low-altitude Free Flight.

Airborne traffic remains disabled only in this diagnostic runner. Production air traffic is unchanged; its preexisting generic mixed-fleet shader still requires a hardware-GPU validation pass because software-driver compilation exceeded this host’s memory limit.
