# Matched living-islands visual review

`environment_review_harness` captures the game's actual environment renderer on a surfaceless OpenGL 3.3 context. It writes lossless native-size PNGs, the absolute camera manifest, per-frame submission counts, and a complete entity-mesh inventory.

## Scope and honesty

The generated test-only adapter leaves production source files unchanged. It retains the exact production entity, terrain, water, lighting, entity/terrain shadow, cloud, sprite, TAA, bloom and post shaders. After upstream lazy per-aircraft compilation (`994dbaf`), every production renderer-startup program is compiled: the raster renderer is copied unchanged, and only scene-scope assertions and read-only draw counters are added. For the frozen older baseline, twenty unused aircraft/cockpit/UFO/mesh-builder startup programs are omitted. Each run records its exact mode in `renderer_mode.txt` and its log. Runtime assertions prohibit aircraft, traffic, UFOs, debris, weapon effects, camera feeds or hangar scenes in the environment harness. This validates renderer startup and the captured environment scenes, not the entire interactive game.

The production volcano particle helper is included when present, with the normal sprite rendering pass and bounded smoke/ember counts. It is absent in the frozen baseline.

Every capture is native 1920 × 1080 with renderScale 1, Medium quality, fixed weather/time, deterministic scenery generation and no hidden resolution reduction. The example review uses Mesa llvmpipe software rendering. Its wall-clock frame times must never be reported as RTX 3070 performance or as proof of 60 FPS. Submitted triangles, instance counts and draw calls are real geometry-cost evidence; they do not predict a specific GPU's FPS.

## Reproduce

1. Archive the exact baseline commit into a separate directory. Do not rely on a working tree that other work can change.
2. Generate adapters for each source tree:

   `python3 tools/validation/prepare_environment_review_adapter.py SOURCE/src OUTPUT_ADAPTER_DIR`

3. Build a test-only executable from `tests/environment_review_harness.cpp` and the game's sources. Substitute the generated renderer.cpp, raster_renderer.cpp and entity_render.cpp for their production counterparts. Include the source's `src` directory, its generated shader header directory, dl and pthread. The baseline must embed baseline GLSL.
4. From the baseline source directory, run:

   `SHADERCACHE=/absolute/baseline-cache LP_NUM_THREADS=2 LIBGL_ALWAYS_SOFTWARE=1 environment_review_harness /absolute/baseline-captures 1920 1080 8`

5. Run the proposed executable from the proposed source directory with the baseline's absolute camera manifest:

   `REQUIRE_ENV_MATERIALS=1 SHADERCACHE=/absolute/proposed-cache LP_NUM_THREADS=2 LIBGL_ALWAYS_SOFTWARE=1 environment_review_harness /absolute/proposed-captures 1920 1080 8 /absolute/baseline-captures/views.csv`

6. Create comparison boards and CSV deltas:

   `python3 tools/validation/compare_environment_review.py BASELINE_CAPTURES PROPOSED_CAPTURES OUTPUT --require-complete`

The optional final harness argument filters scenes by name. When rerunning one view, write to a separate output directory to preserve the full batch's frame CSV. Comparison boards downscale the real screenshots and add labels; they do not retouch, recolor or regenerate the images. Native originals remain available.

## Scene coverage

- Port Verde city, Kaleo town and Meadowbrook rural settlement
- Capital airport and close parked vehicle/building details
- Cedar Ridge forest plus an oak closeup
- Kaleo rocky terrain plus a boulder closeup
- Orchard roads and a close rural house
- Mount Kaleo volcano and a confirmed shallow coastal water patch

All proposed views read the baseline's frozen absolute camera positions, rather than re-selecting convenient locations after changes.

## Counters

`mesh_inventory.csv` lists actual vertices, triangles and packed GPU bytes for every entity kind and LOD. `frames.csv` lists submitted G-buffer instance/triangle/draw counts, shadow triangles actually submitted that frame, and instance-upload bytes. Cross-fade duplication is included because both LODs are genuinely submitted. Frames 0–2 are excluded from the summary median to avoid initial streaming/JIT/shadow setup. A short software render batch is only a diagnostic; full hardware performance requires the game's native-resolution benchmark on the target GPU.

## Materials, time and additional cameras

`REQUIRE_ENV_MATERIALS=1` requires all seven real 2048px environment layers and all seven natural-colour 512px fallback layers to load. The high-resolution set is used at Medium; base scanned-material counts alone are not sufficient proof. The camera manifest includes absolute world coordinates, FOV, cloud cover and hour. Additional matched views cover low-angle grass/asphalt/concrete, low and raised shoreline, and the excavated volcano bowl by day and night.

## Actual flyable-aircraft scale review

`aircraft_scale_review_harness OUTPUT [FRAMES]` uses the current production Kestrel model through the ordinary fleet bake/render path. A unit-scale production house and sedan are deliberately staged on the Capital apron: this is a clearly labelled scale fixture, not a claim that a generated town contains this arrangement. No model is rescaled. The house has a 3m buried foundation, so its total mesh AABB is taller than its above-grade height; both values are recorded separately.

A second view places the same unscaled airframe in the actual Mount Kaleo environment as a staged airborne reference. Its camera distance and the crater's camera distance are recorded because perspective is not an equal-depth measurement. The nominal crater wall radius is 340m (680m across), and the localized terrain edit fades out by radius520m. The fixture is not a flight-simulation replay. Current upstream startup compiles every renderer program and specializes aircraft shaders on demand; the old pre-integration diagnostic omitted eight unused startup programs and identifies that mode in its log.

## Isolated multi-angle asset review

`environment_asset_review_harness OUTPUT KIND_IDS [FRAMES=8] [ANGLE_COUNT=4] [FROZEN_VIEWS]` stages one unit-scale production entity at a time on a flat airport apron, clearing nearby scenery only inside that test process. `KIND_IDS` is a comma-separated list of the production `EntKind` enum integers, or `all`; `mesh_inventory.csv` maps IDs to names. Views are front-oblique, side, rear-oblique and top-oblique. The harness uses actual distance-based near LOD and asserts the view remains outside the LOD cross-fade band; it does not force or replace geometry. Each capture records real vertex bounds, visible height above grade, buried geometry bounds, near-LOD thresholds and all-LOD geometry costs. A subsequent run should consume the first run's `views.csv` so geometry changes cannot silently alter the comparison camera. The surrounding scene's aggregate submission cost is reported separately from the specimen's own mesh cost.


### Close-detail provenance and contact checks

Run `record_review_snapshot.py SOURCE PRE_BUILD.json` immediately before a coordinated build, then `record_review_snapshot.py SOURCE POST_BUILD.json --compare PRE_BUILD.json --binary EXECUTABLE` afterward. The post-build report must show an empty `build_source_changes` array. Both manifests include every production source and shader, including the separate entity mesh `.inc` modules, material payloads, and the review harness; the executable receives its own SHA-256. Keep the binary and manifests with the capture batch.

The isolated asset stage samples the actual production terrain height rather than assuming the airport elevation. `stage_contact.csv` records the ground range across a 5-by-5 footprint grid and each wheel part's true lowest vertex against the ground at that vertex. An apparent floating vehicle with zero recorded wheel gap is a rendering/shadow issue, not permission to lower or distort production geometry. Changing the staged specimen is followed by the production temporal reset, which also invalidates both entity shadow cascades.

New default front/rear cameras are closer and lower than the first sedan checkpoint, with an almost level side camera. These are inspection views, not matched-camera comparisons to that earlier checkpoint; use `views.csv` to freeze them for subsequent revisions. `compose_asset_review.py` creates labelled boards no wider than 1280px by default (override with `--max-width`), while native 1920-by-1080 images remain unaltered.

When extending an existing isolated review to additional kinds, `ALLOW_NEW_ASSET_VIEWS=1` permits kinds absent from the supplied frozen manifest to receive a newly recorded default camera. Every existing named view still uses its exact frozen pose and FOV; `camera_source` explicitly distinguishes the two. Without that opt-in, a missing frozen view remains a fatal error.

`ASSET_REVIEW_HOUR=22` uses the production sun/night calculation for matching night captures. The default is 14.3h; each camera row and renderer-mode file records the selected hour. Keep night output separate and use the daytime frozen camera manifest.

For isolated shadow diagnosis only, `ASSET_REVIEW_DBG=4` disables scenery cascade evaluation through the existing production debug bit. `ASSET_REVIEW_QUALITY=2` selects actual High quality. Default captures remain Medium with all production effects enabled. Both override values are recorded in each camera row and renderer-mode file; diagnostic outputs must remain separate and must not be presented as the standard-quality result. Direct fixture chunk mutations call `markChanged()` so cached exact chunk bounds are always invalidated.

`ASSET_REVIEW_LAMPS=1` provides targeted car/truck front-left and front-right lamp, frontal fascia, and high-oblique lamp cameras. This deliberately crops the full asset to expose lens/reflector recesses and panel joins without changing geometry, scale, materials or LOD selection. Each view is labelled `headlamp_closeup` in the camera manifest and has its own stable name for later frozen-camera comparisons.

`ASSET_REVIEW_SHADOW_DUMP=1` optionally reads the existing final-frame GB0 and near scenery-depth buffers into lossless little-endian float32 files, with bottom-row-first pixel order. A per-view JSON sidecar records dimensions, actual lighting-pass camera basis/jitter, sun direction, both column-major shadow matrices and cascade radii. This is diagnostic readback only: it changes no shader, texture/filter state or rendering pass. Enable it only for a small selected-view run; native PNGs stay unchanged.

`ASSET_REVIEW_NEAR_SHADOW_96=1` is a clearly labelled test-adapter-only experiment: the near cascade radius becomes96m while both texture allocations remain unchanged. It is off by default and is recorded in the mode file and camera CSV. This isolates spatial sampling from filtering; it is not an approved production coverage/transition policy. Use only a separate diagnostic directory with frozen cameras.

`ASSET_REVIEW_CAMERA_DISTANCES=20,40,60,80,100,120,140,180` is an optional fixed-unit distance sequence. It preserves the frozen target, bearing and FOV, then moves only the camera. The normal production LOD and crossfade policy remains active; unlike the ordinary close inspection, it deliberately permits lower LODs. Actual LOD, crossfade partner and distance override are recorded per view. Filenames include the requested distance, and outputs belong in a separate diagnostic batch. This is useful for testing cascade handover on small fixtures without changing their geometry or apparent scale artificially.

`ASSET_REVIEW_COVERAGE_LOG=1` records actual per-frame near/far radii, committed adaptive radius, phase/tier/fade scale, filtered camera speed, AGL, camera-anchored cascade centres, fade bounds, dirty flags, radius-change flag, per-cascade draw/instance/triangle submissions and CPU wall time in `coverage_frames.csv`. These are read-only adapter observations, not GPU timings. The radius96 override is inserted after the adaptive controller assignment when present and is never used to accept adaptive handover.

`ASSET_REVIEW_CAMERA_PATH=/absolute/replay.csv` replays ordered main-camera frames without resetting temporal history between frames. The CSV header is `frame,camera_x,camera_y,camera_z,target_x,target_y,target_z,dt,capture`; frame indices start at0, dt must be positive and at most0.25seconds, and capture1 emits a native keyframe. Use one kind, one angle and no distance-sequence override. The full per-frame camera and actual coverage state are logged; the normal view CSV describes the frozen starting inspection camera. No controller methods or geometry are forced.

`ASSET_REVIEW_NEAR_SHADOW_RADIUS=160` (or256) generalizes the earlier fixed96 experiment. The explicit numeric value is logged in each camera row and mode file. Values must be finite32..2000m. It changes only the test adapter's near footprint, after the production controller assignment; it does not replace allocation, far coverage, shader bias or geometry. Static compromise comparisons must also enable coverage logging and verify fadeScale stays1 throughout, so a controller handover cannot silently alter the experiment. Never combine this override with adaptive-motion acceptance.
