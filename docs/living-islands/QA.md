# Runway and world QA report

**Final accepted source:** 54/54 Release tests passed; Windows Release cross-build
passed. The 17 focused sanitizer passes remain tied to unchanged CPU inputs.
Adaptive near-shadow coverage was rejected and reverted; residual fine close-edge
shadow artifacts remain. See the [final ledger](#final-accepted-source-2026-10-10-1117-1122-utc).

Baseline: `52317bb1f74a5ee9e66e063e2f17d5be786955a0`, archived into an independent
source tree before generating immutable fixtures. See `../../tests/fixtures/RUNWAY_BASELINE.md` for
provenance, checksums, tolerance policy and reproduction details.

## Verified preservation

- All 16 airport/runway descriptors, surfaces, headings, dimensions and endpoints
  match the original definitions exactly.
- **193,625** dense runway, shoulder and overrun locations: **zero** height or
  gradient difference. Ground-contact classification, normal and physics/scenery
  height agreement pass at 7/8/11 octaves.
- **1,506** runway-edge, threshold, approach and PAPI instances retain their
  positions, elevations, yaw, scale and navigation/light values.
- Runway geometry, surface classification, paint layout, designator arithmetic and
  navigation-fixture layout retain their original contracts. The four frozen
  runway shader-function bodies allow only pixel-footprint plumbing to off-runway
  apron/taxiway painting. They do not lock the textures those functions sample.
- The user's later request for realistic grass, pavement and concrete intentionally
  changes environmental material appearance, including runway textures. Photographic
  inputs and ambient-light refinements are authorized; identical rendered runway
  appearance is neither required nor claimed.
- Expanded, fully detailed scenery checks cover **1,540 chunk visits** and **513,862 entity footprints**. No runway/graded-strip intrusion was found.
  Production collision and obstacle-height queries on three runway lanes pass.
- **17,523** frozen whole-world locations at three octave counts retain exactly
  the same sampled heights outside the explicitly authorized summit crater.

Review caught direct runway-function changes to weathering and markings. Those
function edits were restored, and the geometry/paint-layout source check now passes.
The subsequent photographic-material request uses the shared environmental sampling
path while preserving the physical runway and paint-layout contracts.

### Test sensitivity

Independent, temporary fault injections all failed as intended:

1. Increasing MDB's runway width by 1 m failed descriptor/classification checks.
2. Raising one MDB heightmap texel by 2 cm failed the dense height/contact/slope
   checks; the maximum observed sampled change was 18.9 mm.
3. Injecting a house at the runway center failed the oriented-footprint intrusion
   check.

No fault injection altered production sources or the immutable fixtures.

## Explicit crater exception

The full-array audit before the crater found identical heightmap RGBA, all
height bounds and all terrain-envelope arrays against the original source.
After the requested summit crater was added:

- Exactly **558** heightmap texels changed, in height/detail-amplitude only
- Farthest changed texel center: **519.193 m** from `(25000, -9000)`
- **Zero** changed texels outside the prescribed 520 m disk
- Unchanged lushness/coldness channels everywhere
- Maximum possible bilinear support extent: **574.436 m**, inside the test's
  explicit 600 m allowance
- No runway, airport-ground or approach-corridor exemption

All other heightmap texels remain bit-identical. The original immutable
whole-world sample file was retained rather than regenerated to accept the crater.

## CPU streaming check

Final dense-city layout plus precomputed accepted/rejected lot grids and cached
building support heights, GCC `-O2`, identical **641 chunk locations** around the
original 18 settlement positions. Timers cover cold level-2 chunk generation,
not the preceding world build/cache preparation or rendering.

| Metric | Original baseline | Final redesigned world |
| --- | ---: | ---: |
| Process CPU time | 353.112 ms | 361.882 ms |
| Mean CPU time / chunk | 0.551 ms | 0.565 ms |
| CPU time / emitted instance | 1.190 µs | 1.252 µs |
| Total emitted instances | 296,637 | 289,076 |
| Building/fixture-class instances | 9,055 | 7,016 |
| Port Verde cold near-chunk set | 20.503 ms | 27.418 ms |
| Capital cold near-chunk set | 19.314 ms | 20.069 ms |
| Kaleo cold near-chunk set | 18.698 ms | 19.318 ms |

Total CPU cost is +2.5%; normalized per-instance cost is +5.2%. Earlier uncached
city generation was substantially slower; precomputing lot acceptance and reusing
validated ground support moved that work out of the streaming path. The standalone
`tests/fixtures/community_chunk_benchmark.cpp` reproduces this fixed workload against either source
tree. These are host-sensitive CPU observations, **not FPS/GPU performance claims**.
Concurrent jobs produced much noisier wall-clock measurements, so process CPU time
is reported. The user-requested denser skylines remain in the final state.

Keep the existing prefetch/loading-screen path for abrupt camera cuts: chunks
within 700 m are generated synchronously rather than limited by the frame budget.
Airport furniture and lighthouse setup also have first-use initialization costs.

## Cache and bounds review

- Windows generated-world/performance caches already use the rebuilt EXE stamp.
  The explicit world format has additionally advanced to WLD3 for the new masks
  and summit crater.
- Shader binary identity includes shader sources and driver identity.
- Entity meshes are rebuilt at launch, so they have no obsolete disk mesh cache.
- Community lot-cache preparation runs after world masks exist and after cache
  loads, including rejected lots; it is not deferred to the first visible chunk.
- Chunk culling, collision reach and shadow bounds depend on `kEntInfo`; geometry
  must stay within that metadata. Natural-scene collision padding assumes bounded
  footprints. Enlarging future community landmarks must update that bound.
- Static airport/lighthouse caches assume a freshly built process/world. A future
  live-world reload needs coordinated invalidation before workers resume.

## Offshore-object review

The two rectangular-looking offshore objects in `shore_water` are existing
**Sea stack** rock entities, present in the baseline capture. Production raycasts
identified them at `(25020.230, 22008.625)` and `(25106.338, 22003.586)`; a farther
visible object is another sea stack at `(23561.904, 20964.824)`.

A 6 km shoreline box contained 140,556 trees and 119 building/fixture instances,
with **zero** tree/building centers below sea level. Five deliberately offshore
sea stacks were present. No boat/buoy or other maritime-object changes were made.

## Limits

The CPU contract does not claim a complete flight-model retest, screenshot-quality
approval or GPU-frame-time improvement. Airport-layout, entity-raycast, world-cache,
shader/material, actual renderer and sanitizer tests remain complementary checks.


## Recorded hashes

Immutable fixture SHA-256 values (LF text):

- Runway terrain/fixtures: `a0b1c619f3ed21945e099390d23500f10dee9ea2336e201c02771eaac06d3656`
- Runway shader functions: `eb802cec38c90b1a648536c5d4e9d8b7cb02ff2e6029dcc107d58af584c6886b`
- Whole-world height samples: `2be01e9c00f8a585dbee8e99f3d9dbb29422a239acd04c7d83701f1d97767990`

Matched GCC array FNV-1a hashes (float byte sequences; current includes the authorized crater):

| Array | Float count | Baseline hash | Current hash |
| --- | ---: | --- | --- |
| hm RGBA | 16,777,216 | dadc1e51deff9a43 | c25a3aabe07dc087 |
| tpV0 | 4,198,401 | 95c862d653a2cbab | 5fca325fdb98efa4 |
| hmax0 | 65,536 | 9b1c044af4fd113d | a7745869880a1f9f |
| hmax1 | 16,384 | 97f58b4865ca76bd | 09280035b52ae91b |
| hmax2 | 4,096 | 85e175b334cf40d7 | b7e403bd657afb9b |
| hmax3 | 1,024 | a0902c5ee59a3b64 | 4e3d34d79b394fa7 |
| hmax4 | 256 | 6aaaaeec10ca03bc | af997ee4dd09bdab |
| tpM0 | 4,194,304 | fc3e371c667cc16e | 2df4b9fde23f5dc0 |
| tpM1 | 1,048,576 | 5b45c7d25736df85 | 1245d31a6a32e918 |
| tpM2 | 262,144 | bf5996334cf780f2 | 1ee79f3c4a5ddc88 |
| tpM3 | 65,536 | b3d14e15156c685c | b1585be62c300156 |
| tpM4 | 16,384 | 54a74ee90cbc1170 | e7d12a679952d6ad |
| tpM5 | 4,096 | 7509aac030d23712 | 525fef64ec9ea296 |
| tpM6 | 1,024 | d40b3b45870c8e05 | 7b54c77a3ecc4e14 |

All matched before the explicit crater. Its height bounds/envelopes appropriately
change with the crater; the exact texel comparison proves locality rather than
incorrectly requiring those derived global arrays to keep their old hashes.

## Reproduction commands

Run from the repository root with GCC, CMake and Ninja available. Keep the immutable
source outside the working tree; do not update expected fixtures from current code.

```sh
BASE=/tmp/solace-qa-52317bb
mkdir -p "$BASE"
git archive 52317bb1f74a5ee9e66e063e2f17d5be786955a0 src | tar -x -C "$BASE"

cmake -S . -B build-qa -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-qa --target runway_preservation_test -j2
ctest --test-dir build-qa -R '^runway_preservation$' --output-on-failure

# Independent full-array capture and the bounded-crater audit.
g++ -std=c++17 -O2 -pthread -I"$BASE/src" \
  tests/fixtures/world_terrain_array_audit.cpp "$BASE/src/world.cpp" \
  "$BASE/src/scenery.cpp" -o "$BASE/array-baseline"
"$BASE/array-baseline" --capture "$BASE/hm.bin"
g++ -std=c++17 -O2 -pthread -Isrc \
  tests/fixtures/world_terrain_array_audit.cpp src/world.cpp src/scenery.cpp \
  -o "$BASE/array-current"
"$BASE/array-current" --compare "$BASE/hm.bin"

# Identical fixed-location CPU workload; one fresh process per cold measurement.
g++ -std=c++17 -O2 -pthread -I"$BASE/src" \
  tests/fixtures/community_chunk_benchmark.cpp "$BASE/src/world.cpp" \
  "$BASE/src/scenery.cpp" "$BASE/src/entities.cpp" "$BASE/src/airport_scenery.cpp" \
  -o "$BASE/chunks-baseline"
g++ -std=c++17 -O2 -pthread -Isrc \
  tests/fixtures/community_chunk_benchmark.cpp src/world.cpp src/scenery.cpp \
  src/entities.cpp src/airport_scenery.cpp -o "$BASE/chunks-current"
"$BASE/chunks-baseline" > "$BASE/chunks-baseline.csv"
"$BASE/chunks-current" > "$BASE/chunks-current.csv"

# Scene-pixel entity identification and coastline center checks.
g++ -std=c++17 -O2 -pthread -Isrc \
  tests/fixtures/shoreline_entity_audit.cpp src/world.cpp src/scenery.cpp \
  src/entities.cpp src/airport_scenery.cpp -o "$BASE/shore-audit"
"$BASE/shore-audit"
```

The array fingerprint is intentionally a matched-toolchain audit, not a portable
CMake pass/fail gate. The registered regression instead uses immutable, tolerant
numeric samples while requiring exact runway descriptors and shape/paint-layout
shader-source contracts. These checks intentionally permit the later authorized
photographic inputs and environmental lighting changes.

## Integration status

### Sanitizer checkpoint, 2026-10-10

The merged-source checkpoint at
`e88393d4b2cc3f98bb2479b682f307c1c976fe8f` (including upstream `994dbaf`)
passed all **11 targeted AddressSanitizer and UndefinedBehaviorSanitizer checks**
in 363.11 seconds: airport layouts, entity raycast, world cache, foliage,
buildings, runway preservation, community layout, airport environment, volcano
effects, environmental asset meshes, and environmental scale audit.

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSANITIZE=ON
cmake --build build-asan -j2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-asan -j1 --output-on-failure \
  -R '^(runway_preservation|community_layout|airport_environment|volcano_effects|environment_asset_mesh|environment_scale_audit|environment_foliage|environment_buildings|world_cache|entity_raycast|airport_layouts)$'
```

The retained checkpoint log is `/tmp/solace-asan-checkpoint-e88393d.log`, SHA-256
`d4d1e231a0536e4189221ec3705b766a2518eb35464057b4d221aeb35a654a30`.
LeakSanitizer could not run in the instrumented execution environment: enabling
leak detection produced its fatal ptrace-compatibility error. The successful run
therefore disabled leak detection; **leak freedom has not been verified**.

This is a checkpoint, not a pass for subsequent changes. A later environmental
mesh-detail revision is in progress; its final assets, materials, captures, and
post-revision aggregate/sanitizer checks remain pending. Earlier checks, including
the 30-test stable suite and this 11-check sanitizer run, must not be represented
as coverage of those newer changes. No GPU-performance conclusion follows from
these CPU tests.

### Four-slot asset revision: regression coverage

The subsequent detailed-asset revision adds hero slot 3 while retaining the
near/middle/far slot identities 0/1/2. The mesh-budget and total arrays now use
`ENT_LODS`; a three-element array indexed by the four-slot loop would itself be
undefined behavior. Every slot requires a reviewed per-kind ceiling, rather than
an unlimited hero exemption. Reviewed authored ceilings now cover all 52 kinds
and all four slots. This section does not declare the new asset set release-tested.

The common structural test now checks range safety before reading any triangles,
all-slot finite attributes, unit normals, nondegenerate triangles, contiguous
ranges, deterministic construction, and the 40-byte vertex/32-byte instance ABI.
All 17 town-building construction envelopes and buried anchors extend to hero.
Vehicle checks include constant wheel radius/pivot metadata, actual tyre radius,
ground tangency, windscreens and silhouette continuity. Runway light and PAPI
hero meshes must exactly reuse their existing near meshes.
Eight vehicle lamp cups additionally require their normals to face into the
concave cavity and toward the opening; unit length alone is insufficient.

The shop/apartment test retains exact original lower-tier costs and extends its
original bounds, single roof-deck area, unit normals and ground-contact tests to
hero. New opening probes require an actual gap in the front wall, inset glazing,
and a physical reveal. Botanical tests have explicit species-specific four-tier
budgets, preserved distant costs, supported stems, planted silhouettes, and
911,400 leaf-mask equivalence comparisons. The flat-circle foliage calculation
compares only range policies using the same four meshes on both sides; it is
neither an old-versus-new whole-asset benchmark nor measured GPU cost.

Targeted authoring checks caught shop and truck envelope overruns, zero normals
in small sedan fascia patches, backward reflector normals, and a six-vertex
PAPI material-tag leak. These were corrected in the geometry/dispatch without
enlarging the old envelopes or weakening the normal/fixture contracts. The
orientation test first rejected all 2,304 wrongly facing reflector vertices and
then passed them after the reference-point correction.

### Detailed-mesh CPU checkpoint, 2026-10-10 09:41 UTC

An independent source snapshot, linked against the actual production entities,
world, scenery and airport-layout translation units, passed:

- `environment_asset_mesh_test`: 6,331,498 checks, zero failures.
- `environment_building_test`: 342,993 checks, zero failures.
- `environment_foliage_test`: all geometry/range checks and 911,400 leaf-mask
  equivalence comparisons; 150,690 natural-asset vertices across four slots.

Authored near/middle/far/hero vertex totals are **45,519 / 17,433 / 7,002 /
439,992**, or 509,946 vertices overall. Their raw 40-byte vertex payload is
20,397,840 bytes, before allocator/renderer overhead. These are static geometry
counts, not visible instances, rendered frame workload, measured GPU memory,
or an FPS claim. The sedan's revised near/middle/far costs are 2,919 / 1,323 / 96;
the higher costs of its new curved body are explicitly budgeted, not hidden under
a claim that all former geometry costs were preserved.

Validated mesh-source SHA-256 hashes:

| Source | SHA-256 |
| --- | --- |
| `entity_mesh.cpp` | `5e8a2ec96986f1f490896c16141cf09748964a37222f67b6fe1605478966a536` |
| `entity_mesh.h` | `cdf41927c1bf3c0f789231648b5aa7806cb88c1282b78791292e58320ed17d1c` |
| `entity_mesh_nature.inc` | `299b2bc9f3a13a5d6e004ffc68300e4f6ad030ac6c5b86276a32fcbb4b3e8fa6` |
| `entity_mesh_buildings.inc` | `fdfd7e79a4d3cddb450c6961c30e903c224e169e45810945857b78415cb2a96c` |
| `entity_mesh_airport.inc` | `5bfd5b7b48bda70d41ae0135696c5384fc1fa1bd94660e0950d9f7f2278e2795` |

The snapshot, complete source/test hash manifest and logs are retained under
`/tmp/solace-deeper-mesh-qa/checkpoint5`. Result-log SHA-256 hashes:

- Common mesh: `95eb3b40963e4edf4afc5db075b0f0c12200b6cdf5a034020ec2cf0ca83be16b`
- Shop/apartment: `606f1f6fa69213c77460fca12d35a27fbc69d23f050f0841646c0022f09f26b5`
- Foliage: `931f3f254d44c13deb9e69fd1ccfc14d94242bfa8483d117d9b81f6a1ca4b7df`

Equivalent isolated build commands, avoiding a shared Ninja directory:

```sh
set -e
QA=$(mktemp -d /tmp/solace-mesh-qa.XXXXXX)
cp -a src "$QA/src"
cp tests/environment_{asset_mesh,building,foliage}_test.cpp "$QA/"
for unit in entity_mesh entities world scenery airport_scenery; do
  g++ -std=c++17 -O1 -pthread -I"$QA/src" -c "$QA/src/$unit.cpp" -o "$QA/$unit.o"
done
for test in asset_mesh building foliage; do
  g++ -std=c++17 -O1 -pthread -I"$QA/src" \
    "$QA/environment_${test}_test.cpp" "$QA"/*.o -o "$QA/$test-test"
  "$QA/$test-test"
done
```

This checkpoint did not cover visual acceptance, subsequent mesh/material edits,
renderer/culling integration, or the final aggregate and sanitizer runs. At that
point the new placement-height policy still awaited runway/community validation.
The earlier 11-check sanitizer checkpoint must not be substituted for those tests.

### City and airport submission costs, 2026-10-10

The independent [all-entity submission audit](performance/ENTITY_SUBMISSIONS.md)
compares archived `994dbaf` production placement/meshes against a recorded current
source snapshot at identical frozen Capital, Port Verde and airport cameras.
Medium city street/close views submit 18.0–23.3% more triangles overall; High is
23.8–32.4%. The airport ramp increases 37.1% and 34.5%, respectively. These totals
include changed communities and lower meshes, not only hero detail. A controlled
same-world comparison with identical parent bounds isolates the close policy.
Exact per-pass counts, authored source hashes, cameras and a read-only reproduction
utility are recorded alongside the report. Both-dirty shadow-refresh counts are
reported separately; the normal cached shadow passes never use hero geometry.
No GPU timing or FPS claim follows from these counts. Later asset edits require
a refreshed source manifest and run before calling the ledger final.

### Merged v6 checkpoint, 2026-10-10 10:41–10:47 UTC

**54/54 Release tests passed** in 231.57 seconds. **17/17 focused ASan/UBSan
tests passed** in 351.49 seconds. These are checkpoint results: the subsequent
night-window correction and the visual shadow-resolution decision are separate
gates, requiring their own affected rebuilds and tests. No target-GPU frame rate
or native Windows playtest is established by this checkpoint.

Evidence, all repository-relative:

- [Release CTest result](performance/qa-v6/release-v6-ctest.log) and
  [complete per-test output](performance/qa-v6/release-v6-full.log).
- [Sanitizer CTest result](performance/qa-v6/asan-v6-ctest.log) and
  [complete per-test output](performance/qa-v6/asan-v6-full.log).
- [Exact pre-build source manifest](performance/qa-v6/v6-source-before.sha256)
  and [preservation/mesh/scale binary hashes](performance/qa-v6/v6-test-binaries.sha256).

The source-manifest file's SHA-256 is
`5ab2a714143535c0f0b0d9517521265b341b1658b97197c429588c17381b9850`.
All production sources remained unchanged during the v6 runs. Only the separate
review harness, its generated-adapter preparation tool, and review guide changed
for a later diagnostic; they are not the production geometry or shaders.
The core source includes merged upstream `9dde57e` at merge `6952bd7`, plus the
local fixes identified by the manifest. The aircraft/aero/models/breakup and
aircraft shader byte-preservation guard against `9dde57e` also passed.

The earlier v5 aggregate was **52/54**, not a clean pass. Its two failures were
corrected before v6: upper fir/spruce shoots exceeded the retained crown-height
envelope, and the scale audit still described a replaced procedural facade grid.
Shoot insertions were lowered without changing counts or weakening the 1 cm
height test. Fir near/hero maxima are now 14.008728/14.008335 m; spruce maxima are
20.006845/20.005325 m. The scale test now checks actual fitted facade grids and
observed physical scale bounds. A brief GA windshield source edit/restore occurred
during the v5 run; matching endpoint hashes are **not** treated as proof of an
uninterrupted hold. The complete v6 rerun includes the accepted GA correction.

Measured v6 results in both Release and sanitizer runs:

- All 16 airports: 193,625 frozen terrain/contact samples, 1,506 navigation
  fixtures, 1,540 chunk visits and 513,862 footprint checks. Maximum height and
  gradient deltas are exactly zero.
- Full-world sample contract: 17,523 frozen locations at octaves 7/8/11; maximum
  height delta outside the authorized summit exception is zero. Two locations
  lie within the explicit 600 m crater exception; no runway exception was added.
- Community placement: 9,502 buildings, 476 tall buildings, all 18 towns inhabited,
  45 roads and 36 sampled parked cars.
- Common mesh: 6,374,862 checks, zero failures. Near/middle/far/hero totals are
  46,365 / 17,961 / 7,026 / 442,110 vertices; total 513,462, or 20,538,480 raw
  vertex bytes. Those are geometry payload counts, not GPU memory or timings.
- Building construction: 342,993 checks; nature structure: 1,880,801 checks;
  close-detail policy: 3,224,602 checks; all passed.
- Authored/animated bounds: 68,475,802 checks and 34,025,712 visible transformed
  samples, zero failures. Chunk revision/cache bounds: 29,320 checks, including
  2,010 reach-zero protrusion cases.
- Apartment floor modules remain fixed at 3.4 m. Office/skyscraper shaders match
  their fitted nominal 3.7 m grid; observed physical pitches are 3.404–3.583 m
  for offices and 3.404–3.996 m for skyscrapers, within the authorized scale range.

The available GLSL validator was explicitly configured. Existing shader checks
cover 154 assembled stages and their 154 pruned variants. The new fail-closed
`shader_glsl_inline_wreck` test extracts and compiles the actual inline vertex and
fragment strings and links their pair, covering the previously omitted 34th
startup program. This is **310 stage variants**, not 310 linked programs; the
inline pair's pruned variants are not separately covered.

Sanitizer options were `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. LeakSanitizer is unsupported
under this environment's ptrace, so leak freedom is not verified. The 17 checks
are airport layout, entity raycast, world cache, foliage, buildings, runway
preservation, community layout, airport environment, volcano effects, common
mesh, scale audit, close policy, nature detail, airport construction, close bounds,
chunk bounds and extracted terrain/material arithmetic.

### Post-18f6178 and night-material checkpoint, 2026-10-10 10:55–10:58 UTC

**54/54 Release tests passed** in 178.86 seconds after the clean upstream merge
`4f2ed6565b7f02b545075f970b52484557c1f3f4` and the approved night-only curtain-wall
emission change. This includes a newly rebuilt, full-mode flight test (104.78 s),
breakup, gameplay, all runway/world contracts and the 310 shader-stage variants
described above. Evidence is in [qa-post18-v7](performance/qa-post18-v7/):
[CTest results](performance/qa-post18-v7/ctest.log),
[complete test output](performance/qa-post18-v7/full.log),
[source manifest](performance/qa-post18-v7/source-before.sha256),
[end-of-run verification](performance/qa-post18-v7/source-after-check.log),
[build log](performance/qa-post18-v7/build.log), and
[test executable hashes](performance/qa-post18-v7/test-binaries.sha256).

The source window remained held for production and registered tests. The only
manifest differences were the separate asset review harness and review guide,
used for ongoing visual diagnostics. Geometry, world/placement sources and the
17 focused sanitizer test sources are byte-identical to the passing v6 checkpoint;
the sanitizer results above are carried forward on that evidence, not reported
as a second sanitizer run. The independent aircraft/aero/models/breakup/aircraft
shader-helper byte guard also passed against latest upstream
`18f617869a207f0085e3efc5b4c7d0e94defbc17`.

Full flight was explicitly run with `env -u FLIGHT_QUICK ctest ...`.
`FLIGHT_QUICK` must be **absent** for full mode: setting it to `0` still selects
the upstream reduced mode because the test checks environment-variable presence.
The upstream Linux sanitizer CI now selects that reduced mode, while its Windows
job and this recorded Release run retain full mode. Do not describe reduced CI
as complete fleet coverage. The integrated upstream changes also build
`breakup_test` explicitly, prioritize long CTest jobs, clear stale Windows
`error.log` after successful startup, and repair the research screenshot boot
clock; none changes environment geometry or runway golden data.

The [hash-bound float32 night-emission audit](performance/qa-post18-v7/night-emission.json)
passed 3,727,360 sampled panes: 27.9441% occupied, sampled lit luminance
0.059234–0.279848, exact zero daytime emission, no additional texture samples.
These arithmetic bounds do not replace captured-pixel review.

Reproduction commands for this checkpoint:

```sh
cmake -S . -B ../solace-world-community-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DGLSLANG_VALIDATOR=/path/to/glslangValidator
cmake --build ../solace-world-community-build --parallel 3
env -u FLIGHT_QUICK ctest --test-dir ../solace-world-community-build \
  --output-on-failure -j2
python3 tools/validation/audit_night_emission.py
```

This checkpoint precedes the approved adaptive near-shadow implementation.
The radius-96 render experiment was review-adapter-only during these tests;
its prospective production controller and final Windows build require later
validation. No target-GPU performance claim is made.

### Final accepted source, 2026-10-10 11:17–11:22 UTC

**54/54 Release CTest tests passed in 173.85 seconds.** Full-mode flight passed
in 98.71 seconds with `FLIGHT_QUICK` absent. Breakup, gameplay, every runway/world
contract, all detailed-asset checks, night/material checks and the configured GLSL
checks passed. This is the accepted fixed-shadow-policy build after clean upstream
merge `4f2ed6565b7f02b545075f970b52484557c1f3f4`, incorporating latest upstream
`18f617869a207f0085e3efc5b4c7d0e94defbc17`.

Evidence: [final CTest log](performance/qa-final/release-ctest.log),
[complete per-test output](performance/qa-final/release-full.log),
[Release build](performance/qa-final/release-build.log),
[source manifest](performance/qa-final/source-before.sha256),
[end-of-run source verification](performance/qa-final/source-after-check.log),
and [executable hashes](performance/qa-final/test-binaries.sha256).
All 287 recorded source/build/test/tool inputs stayed unchanged through the
accepted builds and test run. The source-manifest SHA-256 is
`3188765464ae9bb4e23c348165dde8f65366e25041017691c5664599a691afcd`.
Generated Python bytecode caches are excluded from this source manifest.

The [Windows Release cross-build](performance/qa-final/windows-build.log) passed.
`SolaceExpress.exe` is **7,539,712 bytes**, SHA-256
`78ad1140e077cbf06ab129b115818f383c1606f46b88fed619f2472a8f795f28`.
This verifies compilation and linking; no native Windows execution, RTX 3070
benchmark, stable-60-FPS result or target-GPU playtest was performed.

The v6 **17/17 focused ASan/UBSan passes** remain applicable to byte-identical
accepted CPU geometry/world/placement and focused-test sources, with the same
LeakSanitizer limitation. They are not represented as a fresh final sanitizer
run. The ASan build tree was reconfigured after reverting the experiment and
once again registers exactly 54 normal tests, with no adaptive target.

All fresh preservation results match the earlier detailed ledger: 16 airports,
193,625 runway/contact samples and 17,523 full-world sample locations; maximum
height/gradient deltas remain zero outside the bounded crater exception.
The common mesh still passes 6,374,862 checks across all four LODs. The independent
aircraft/aero/model/breakup/aircraft-shader-helper guard passes against `18f6178`.

**Known visual limitation:** fine close-edge/eave shadow artifacts remain.
The 96/160/256 m adaptive near-coverage experiment did not give a clean overall
quality improvement, so its runtime/header/test/CMake changes were restored
byte-for-byte to the pre-proposal source. Only a clearly disabled prototype and
[separate diagnostic count ledger](performance/REJECTED_ADAPTIVE_SHADOW_SUBMISSIONS.md)
remain. The [fixed-policy submission ledger](performance/ENTITY_SUBMISSIONS.md)
is the accepted geometry-count report; the rejected controller's sanitizer and
count results do not increase accepted-game coverage or establish a GPU saving.
