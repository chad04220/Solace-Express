# Procedural Earth review handoff

Review branch: `codex/procedural-earth-regions-20261012`, based on `d1b2c99`.
Initial publication `94546c98aee144bfa13687903f005fbdc9c516d1` preserved the exact
tested local tree. This remains review work, not a release. Tests and measurements
below must be interpreted with their explicit scope. The first full CI run found
additional cross-platform failures; see the corrective follow-up at the end.

## What is implemented

- A deterministic versioned catalog with 6,143 generated geographic regions
  plus authored Solace, retaining its existing airport/save identity.
- Five genuinely different landscape recipes: archipelago, volcanic chain,
  fjords, atolls, and continental fragments. Existing airport grading,
  settlements, roads, lots, vegetation, bridges and collisions are reused.
- Actual Free Flight destination selection and inter-region navigation, with
  double-precision geographic addressing and local-chart state transport.
- One active full World plus one preparing/retiring full World. Background work
  owns its scenery and does not replace globals until ready publication.
- Budgeted GPU texture/geometry staging; no midflight loader or frozen simulation
  while generation runs. Publication waits for a nonblocking scenery-reader drain.
- Full-derived active/candidate/previous silhouettes plus independently generated
  lightweight horizon meshes. A hard 64-slot GPU cache covers the measured
  conventional and fictional research-altitude neighborhoods with headroom.
- Cooperative cancellation of stale candidate generation, including safe parallel
  loop unwinding and cache publication protection. Actual latency tests are part
  of the acceptance evidence, rather than an assumed instantaneous cancellation.
- An immutable paired coarse-render/collision fallback for late detailed terrain.
  It is Free Flight-only, pins the accepted surface near aircraft contact, and
  prevents first reveal or detail publication from changing the ground under a
  low aircraft. Strict and fast-math parity and actual Plane-contact tests pass.
- Separately requested realistic finite jet fuel calibration and mission policy,
  documented in `JET_FUEL.md`. Research launches retain explicit full tanks;
  accepted lower-fuel mission plans are not silently overridden.

## Preservation and coordinate contract

The authored Solace regeneration matched all 146,389,954 baseline cache bytes,
SHA-256 `9dae7400e1226f376771b084c18b81d3327bcaece5b9052dc94f7fea282e6f96`.
Existing career airport indices and save schema are unchanged. Procedural airport
identity is region-qualified at runtime, and Free Flight cannot serialize those
indices into the campaign. Returning to career/research restores Solace first.

Surface-chart y is altitude. The new geodesy foundation does not turn the flight
model into rigid ECEF/orbital dynamics. Chart changes preserve geographic position
and transport state with horizontal frame convergence; the real-Plane tests
quantify the small local-chart approximation rather than claiming mathematical
identity over arbitrary trajectories. Renderer curvature uses the existing
6,371 km default visual radius; debug flat-world/panorama modes are separate.

Generated land fades inside a 38 km half-width square, whose conservative disk
radius is 53.74 km. Neighbor centers are 226.52–312.73 km apart, giving a 119.04 km
minimum generated coast-envelope gap. The Solace exception has a conservative
86.63 km gap to its nearest generated region. Earlier 150 km gap wording was
incorrect and must not be carried into integration notes.

## Visibility and resource evidence

The independent full-360 visibility enumeration covers 108,549 viewpoints and
cross-checks spatial counts against brute force. At 13.716–16 km, conservative
3 km summit envelopes produce a sampled maximum 24 groups. Fictional 25/28/30 km
cases produce 32/34/36. These are potential envelopes before fog, FOV, pixel-size
and occlusion tests; sampled maxima are not global mathematical proofs.

Earlier CPU-only tests built 24 coarse meshes in 275 ms and 36 in 365 ms,
without full World arrays. Those measurements predate retained collision grids
and the naturalistic recipe revision; they are historical baselines, not final
combined memory or generation-time figures. The theoretical maximum per coarse
mesh is 3.75 MiB of GPU triangles; actual dry-land payload is lower. The 64-slot
paired-grid limit is 16.25 MiB, with two pending grids adding about 0.51 MiB.

Those figures are not total game memory. Software OpenGL and aircraft shader
compilation are separate, much larger costs. The traffic-enabled real-scene
fixture reaches about 947 MiB after terrain/shadow passes, then selects the
preexisting generic mixed-fleet objects shader for Atlas plus Starling traffic.
That shader compile is killed near the environment limit, as previously noted
in `tests/UI_REVIEW.md`. Keep this failed full-traffic evidence. An explicitly
labeled air-traffic-off diagnostic may isolate terrain handoff behavior; it does
not prove the mixed-fleet GPU path on this host.

The first high-altitude launch fills its far neighborhood asynchronously after
flight begins. Steady flight has 3 km altitude lookahead. Initial settle frames,
ready/wanted counts and truncation status must be reported separately.

The integrated paired-collision checkpoint passed 26/26 scoped Linux tests and
linked the Windows executable. The all-archetype cancellation run covered all five
archetypes and 65 heavy-stage interruptions: 300 checks passed, with unchanged
active state and byte-identical fresh cache/scenery output. The worst observed
worker drain was 10.463 ms. This is observational, not a deadline: filesystem
calls and allocation/deallocation cannot be interrupted. A cancellation racing
an already-entered atomic replacement may leave a fully valid completed cache;
it does not publish a partially written file.

Real full-frame measurement found and fixed two distinct streaming costs:
a bulk terrain-shadow bake and a software-driver shader variant change when the
world-data texture changed between power-of-two and non-power-of-two heights.
Shadow updates now use eight rows per frame; data rows consistently use padded
non-power-of-two dimensions without changing texel addresses.

At the paired checkpoint, generated-region handoff was 277.665 ms and Solace
handoff 390.668 ms, within their roughly 300–420 ms software-GL frame envelope.
The previous generated handoff was 5,079 ms. Detailed publication itself took
3.075/2.289 ms. A 2 MiB combined transfer cap passed; an indivisible empty texture
allocation still took 16.5 ms, so the 1.5 ms time target is soft. Initial shader
warm-up reached 9.96 seconds and is excluded from steady-flight comparisons.
These runs use per-pass glFinish attribution and air traffic off, and do not
establish hardware-GPU frame rate or the mixed-fleet GPU path.

The 30 km camera stress selected and readied 28/28 groups after 18 updates over
6.601 seconds; another 30 verification frames followed. Resident meshes peaked
at 40, below 64; coarse GPU payload was 51,520,200 bytes, retained collision grids
10,383,984 bytes. Whole-process peak RSS was 1,886,588 KiB. These are one actual
viewpoint's requested resources, not the separately sampled global maximum of
36 potential envelopes. The aircraft remained in ordinary low-altitude flight.
See the detailed render evidence for per-phase percentiles and caveats.

The naturalistic shape revision was validated separately after this paired
checkpoint. Its final captures and production/cache tests are identified below
and in the final section of `renderer-acceptance.md`.

Capture provenance matters: the optional EGL harness is `EXCLUDE_FROM_ALL`.
One later repeat was found to have reused the earlier paired executable and is
preserved as `stale-harness-repeat`, explicitly invalid for naturalistic-recipe
claims. The verified runner now builds that target explicitly, records executable
and source hashes plus UTC capture times, checks camera telemetry, and rejects
source/executable changes during a capture batch.

## Integration boundaries

The separate UI/rebrand branch should preserve functional changes in Game setup,
loading, Earth-route selection, fuel/weight validation and research launch plans.
No branding or terminal-layout redesign was made. Changes to
`game_research_ui.cpp` are narrow technical-data strings only.

World ownership changes span World, scenery, roads, settlement/landmark helpers,
Game and renderer. Do not merge only the selector or copy whole files over the
other branch. The `GenerationCancelState` scope belongs only to background
candidate jobs; active flight queries and ordinary generation have no token.

Renderer diagnostics are opt-in (`REGION_RENDER_DIAG`). They expose pass/RSS and
aircraft coverage identity for reproduction and must not be mistaken for a
production quality setting. The seam fixture uses explicit test-only placement
near a boundary; it is not an unassisted long-distance pilot flight.

Prebuilt regional packs were explicitly canceled. Their completed local files
and experimental source are preserved outside this checkout; no pack builder or
pack deployment remains connected to the active runtime. Do not revive that
experiment or the canceled global raster measurement as part of this review.

## Final review evidence and remaining limits

The naturalistic revision passed 25 production validation steps with 94,319
assertions across all five landform families, including actual airports, lots,
roads, production collision sweeps, independent fresh-build byte equality,
cache replay and valid-but-stale recipe-cache rejection. The active authored
world remained unchanged throughout candidate generation. The worst observed
two-thread generation was 19.768 seconds; the two-World test process peaked at
398,576 KiB. Authored Solace again matched every baseline cache byte.

The complete default-seed catalog passed 2,397,110 placement/approach/dry-route
checks. All 55,287 nine-seed metadata manifests matched the pre-naturalism
catalog. Strict and fast-math shape suites each passed 99,597 assertions; the
same shape suite passed ASan+UBSan with leak detection disabled because the
sandbox's ptrace wrapper prevents LSan. This is not a leak-sanitizer pass.

The Windows executable was relinked from this revised source. Its SHA-256 is
`de5ab6ba63074b94f4d31841e5d6d35e7c5931aa6dbb45c836ee9270af120933`.
This is a cross-compilation/link check, not a Windows runtime or hardware-GPU run.

Final fingerprinted EGL captures passed with revised landforms. Generated and
Solace handoff frames were 266.340/366.014 ms, against software-renderer phase
ranges documented in `renderer-acceptance.md`. The 16/28/30 km selected camera
neighborhoods reached 21/21, 27/27 and 28/28 ready groups. Peak residency was
40 meshes, final coarse payload 50,990,280 bytes, retained grids 10,383,984 bytes,
and the five-scene process peak was 1,760,568 KiB. Later altitude tests reuse
cache residency; they are not independent cold-start measurements. Actual
atoll/fjord low and 6 km oblique captures verify the revised geometry.

The final targeted Linux regression passed 28/28 tests in 206.68 seconds, recorded in
`natural-final-tests.txt` and `natural-final-details.txt`. This is a selected
integration suite from 139 registered tests, not a claim that the entire project
suite was run. Check those recorded results for the final status.

Remaining validation limits:
- Default airborne traffic was disabled only in the EGL diagnostic fixture. Its
  preexisting generic mixed-fleet shader exceeds this host's software-driver
  compilation memory limit. Production traffic remains enabled normally; its
  CPU behavior passes, but hardware-GPU mixed-fleet flight needs testing.
- Real frame-rate and worst-turn stress on the target GPU remain unmeasured.
  Transfer bytes are hard-capped, but driver allocations, OS calls and shader
  warm-up have no hard millisecond guarantee. Initial horizon population takes
  time and is reported separately from steady flight.
- Coarse collision fallback is consistent with its visible terrain, but does
  not provide full buildings or usable airport facilities before detail arrives.
- Flight dynamics remain local surface-chart physics, not native ECEF/orbital
  dynamics. Research/career retain authored Solace topology and save isolation.
- The UI/rebrand and optional foveation/prepass changes need deliberate merge
  resolution; no upstream merge, push or release was performed here.

Relevant evidence lives under `docs/validation/procedural-regions/` and
`docs/validation/jet-fuel/`. No universal FPS or arbitrary hardware-speed guarantee
is implied by a byte budget or a sampled generation time.

## Read-only upstream integration review (2026-10-12)

The upstream foveation/depth-prepass commit was inspected without merging it:
https://github.com/chad04220/Solace-Express/commit/62a525a9f258f4435efd923267d0ff20e20056bf
It overlaps Game settings/update, game_ui, renderer.h/.cpp, raster_renderer.cpp,
entity_render.cpp and terrain_mesh.cpp, and adds GL entry points plus terrain
shader assembly changes. Keep the NVIDIA foveation feature and this branch's
streaming ownership/budget/diagnostic work when resolving those files.

Specific hazards:
- Its new drawTerrainMesh(mode) early-return on no resident terrain chunks must
  not suppress this branch's ocean or distant-region draws. This branch must
  still draw those while the aircraft crosses an empty ocean chart area.
- Its optional terrain depth prepass must honor the same near/far dither coverage
  as the shaded full terrain; otherwise invisible full-terrain pixels may write
  depth and prevent the coarse complement from filling them. Keep that prepass
  disabled until combined coverage is tested.
- Camera-feed/panorama exclusions and paired begin/end foveation state must remain
  correct around the new distant G-buffer draw. Hardware VRS validation is separate
  from the software-GL measurements here.
- The optional depth program is lazily built. Keep accurate shader startup counts
  with this branch's additional mandatory far-region program.

The separate rebrand commit's packaging-path CI assertions and earlier script-
only shard selection failure are upstream integration notes, not fixes included
in this procedural branch. No unrelated CI repair or blind merge was performed.

### Later rebrand integration checkpoint (read-only, 20:52 UTC)

PR #5 merged at 20:43:36 UTC via `0f47bd7`; its direct follow-up
[`faa662f04d61b6506e85e7ed217899f2306e1fa7`](https://github.com/chad04220/Solace-Express/commit/faa662f04d61b6506e85e7ed217899f2306e1fa7)
was inspected after this branch's tests. No rebase, fetch, merge or source copy
from that delta was performed. This tested branch deliberately remains based on
`d1b2c99`; its old executable/environment names do not describe current upstream.

Verified changes and merge requirements:
- Upstream's Windows target/project/executable/package are `SomniumVolandi`,
  without a `SolaceExpress` target alias. Preserve our regional sources/tests
  while updating build commands, asset/source macros and bundle names.
- Runtime `SOLACE_*` switches become `SOMNIUM_*`. Our new
  `procedural_region_runtime` and `jet_fuel` CMake fixtures must use
  `SOMNIUM_TEST_WORLD` with upstream `tests/test_world.h`; otherwise they
  regenerate a world rather than consuming the shared fixture. `TERRPRE`,
  `SHADERCACHE` and `HARNESS_*` remain separate unchanged switches.
- The exact new save header is `somnium_save 6`. The reader still accepts
  `somnium_save`, `solace_save` and `airxpress_save` versions 1–6. Old executables
  do not understand the new token. Preserve this backward reader alongside our
  fuel estimates and Free Flight save-isolation guards.
- Saves/settings/stations now use `%APPDATA%\SomniumVolandi`, with preferred
  caches under `%LOCALAPPDATA%\SomniumVolandi`. Old branded directories are not
  automatically migrated. Existing executable-directory/current-save-directory
  `shadercache` migration/cleanup still exists, so “no cache migration of any
  kind” would be inaccurate. PR #5's earlier compatibility text is stale.
- Upstream's process-global atomic loading/task observations belong to the
  intended UI/load job. When merging them into our `World::buildConfigured`,
  background candidate generation must not reset the active tracker. Preserve
  the `this == &g_world` stage guard, per-world scenery, and cancellation scopes.
- Retain upstream `foveate` and `menuMotion` settings, persistence/UI, and
  `g_ren.foveation = 0` in the expanded headless initializer. Merge callbacks
  without dropping regional loading, readiness, promotion or research fuel.
- The prepass now calls `drawTerrainMesh(fp, mode)`. Preserve our empty-chart
  ocean draw and distant-region color pass, but do not run far color geometry
  in a depth-only pass. The existing near/far depth-coverage and balanced VRS
  concerns above remain acceptance gates after integration.
- Merge the themed/loading harness fixtures and renamed temporary settings path
  alongside the region/seamless fixtures. Rebuild matching-source aircraft
  bundles: branding changes producer/geometry identity prefixes, so old bundles
  cannot simply be renamed.

Pinned evidence: [CMake](https://github.com/chad04220/Solace-Express/blob/faa662f04d61b6506e85e7ed217899f2306e1fa7/CMakeLists.txt),
[save serialization](https://github.com/chad04220/Solace-Express/blob/faa662f04d61b6506e85e7ed217899f2306e1fa7/src/career.cpp),
[platform paths](https://github.com/chad04220/Solace-Express/blob/faa662f04d61b6506e85e7ed217899f2306e1fa7/src/platform_win32.cpp#L338-L429),
[world progress](https://github.com/chad04220/Solace-Express/blob/faa662f04d61b6506e85e7ed217899f2306e1fa7/src/world_load_progress.h),
[terrain modes](https://github.com/chad04220/Solace-Express/blob/faa662f04d61b6506e85e7ed217899f2306e1fa7/src/terrain_mesh.cpp).

Highest-risk shared files are CMakeLists.txt, Game/UI, World, renderer/terrain/
raster/entity rendering, career serialization, and tests/ui_review_harness.cpp.
Current local evidence must not be claimed to validate that unperformed merge.

## Published CI follow-up (2026-10-12)

The first full CI run on the published review commit failed in the shader bake
identity guard, the older Free Flight regression fixture, the Windows temporary
save path, the MSVC fast-math transport tolerance, and the sanitizer stale-candidate
fixture. Sampling on both platforms, aircraft generation, and sanitizer shard 1
passed. The failures and scoped corrections are documented in
[`validation/procedural-regions/ci-corrections-20261012.md`](validation/procedural-regions/ci-corrections-20261012.md).
Existing assertions, numerical tolerances, and pinned aircraft bake identities
were retained. The corrective source has separate local evidence; the original
render measurements above do not establish Windows/RTX runtime performance.

Read the latest exact-commit CI result in
[the coordination thread](https://github.com/chad04220/Solace-Express/issues/2#issuecomment-6133278045)
before integrating. No rebrand/foveation merge or release is part of this repair.
