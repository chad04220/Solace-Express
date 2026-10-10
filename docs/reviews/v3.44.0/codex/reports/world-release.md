# Solace Express v3.43.0: world, collision, streaming, traffic and package audit

## Scope and provenance

- Released source: `91212bf4a131c02d2da41efbdb09248a09bc3599`, read-only checkout `solace-v343-audit`.
- Released ZIP: `SolaceExpress-v3.43.0-windows-x64.zip`; 41,583,832 bytes; SHA-256 `19205ebcad8c3a201386e39edfde970060054634aaf479e6583bb44574b6a728`.
- Release asset URL: https://github.com/chad04220/Solace-Express/releases/download/v3.43.0/SolaceExpress-windows-x64.zip
- Custom probes link the stock Linux Release objects from this exact source, using one standalone compiler process at a time. No production files, saves, branch history or published assets were changed.
- `84b8d1a` is inspected only as a **separate, unreleased development comparison**. Its changes must not be presented as shipping in v3.43.0.
- Evidence: `logs/world-release/`; standalone source and reproduction instructions: `repros/world-release/README.md`.

## Findings at a glance

| ID | Priority | Finding | Evidence | Development distinction |
|---|---|---|---|---|
| WR-1 | P2 | Plasma crater graphics and physical ground disagree by 8 m | Reproduced through real Game::update for 10 s | Relevant code still unchanged in inspected 84b8d1a |
| WR-2 | P2 | Eighth bomb resurrects scenery and its collision in the first blast area | Actual generated oak collision: 4 → 0 → 4 | Seven-crater cap still unchanged in 84b8d1a |
| WR-3 | P2 | Cross-chunk scenery destruction removes collision but leaves rendering | 130 ownership mismatches in 1,225,372 real sampled entities | Garden/farm ownership partially addressed unreleased; boundary-index rounding remains |
| WR-4 | P2 | Terminal disappears at a viewport edge while real geometry is still visible | Four actual airports; fully visible, front-facing triangles fail chunk culling | Authored-bounds remedy already present in 84b8d1a; do not implement a duplicate fix |
| WR-5 | P3 | Structurally valid corrupted world cache can inject NaN ground into the simulation | Same-length, valid-stamp cache with one changed float accepted | Still lacks finite/value-integrity validation in inspected development code |
| WR-6 | P3 | ZIP's README points to LICENSE, which the package omits | Verified ZIP contents and packaging workflow | Distribution-documentation completeness, not a gameplay blocker |

P2 means normal-priority, player-visible correctness. No P0/P1 security, destructive-data, or universal-launch defect was established in this subsystem. WR-5 needs local cache corruption; it is not an ordinary gameplay trigger.

## WR-1: rendered plasma crater has an invisible physical floor 8 m above it

**Status:** reproduced; high confidence. **Priority:** P2.

### Cause and exact locations

- `src/game_wraith.cpp:252–258`: detonation adds radius 24 m / depth -8 m plasma crater.
- `src/game_wraith.cpp:416–424`: the crater/scorch arrays are sent to rendering.
- `src/shaders/common.glsl:23–34,86`: rendering uses absolute depth and lowers the visible crater bowl.
- `src/aircraft.cpp:494–499`: wheel/structure contacts exclusively call `g_world.height`; this excludes all dynamic craters.
- `src/game.cpp:1344–1347`: normal Game flight updates call that same Plane::step with no alternate crater collision callback.
- `src/game.cpp:2133–2139`: wreckGround includes only the crash crater and wreck pits; it excludes Wraith craters/scorches.
- `src/game_wraith.cpp:21–32,337–350`: weapon groundHit / bomb impacts also sample the unchanged world height via groundAt.
- `src/game.cpp:2430–2439,2470–2472`: wreck/debris contacts use wreckGround and inherit the missing plasma/scorch ground.

### Reproduction

Run `crater_probe` against release objects. The harness launches a real headless XR-40 research flight, detonates at Meadowbrook runway center `(-8000,45,14000)`, and obtains the actual crater parameters from wraithVisual. It then places the same aircraft over the crater with gear down, and runs 600 actual `Game::update(1/60)` ticks, calling buildFrame each tick.

```text
plasma_crater count=1 radius=24.00 stored_depth=-8.00
shader_floor=37.000000 world_collision_floor=45.000000 wreck_ground=45.000000
Game_update_after_10s bottom_y=44.983555 shader_floor=37.000000
floats_by_m=7.983555 on_ground=1 crashed=0 screen=2 game_time=10.000
```

**Expected:** a landed or hovering craft meets the same ground that is drawn. **Actual:** it rests on an invisible surface almost 8 m above the visible crater. This does not heal after frame assembly or later simulation ticks. The same mismatch applies to new weapons impacting an existing pit; the static inspection also establishes that debris above plasma craters uses the old floor.

**Player route:** use XR-40 research flight, bomb a flat land/runway area, then return and attempt a vertical landing or very low pass through that crater. The programmatic fixture places the plane after detonation to isolate the surface result; it does not replay the full manual circuit/landing input sequence.

**Fix direction:** one authoritative dynamic-ground sampler used by aircraft contact/AGL/normal queries, weapons, wrecks, and rendering. Include plasma/scorch depth sign handling exactly once. Keep water treatment consistent. Test center, lip, overlapping craters, and crater limits using the real Game loop. Alternatively, if craters are intentionally cosmetic, remove depth displacement rather than leaving misleading collision geometry.

**Unreleased comparison:** the relevant aircraft direct-height sampling and Wraith crater code are unchanged in 84b8d1a. This is independent of the newer world meshes/settlements.

## WR-2: the eighth bomb restores objects and collision at the first blast

**Status:** reproduced on an actual generated entity; high confidence. **Priority:** P2, persistence/design-policy decision required.

### Cause

- `src/game_wraith.cpp:257–258` retains only the last seven plasma craters.
- `src/game.cpp:2727–2732` clears and rebuilds scenery.craters each rendered frame solely from the retained crater arrays.
- `src/entities.cpp:135–141` treats an entity as destroyed by a blast only while its center lies in one of those current masks. Unlike direct laser damage (`:162–170`), bomb damage does not record a durable per-entity destruction key.

The visual crater cap itself is a deliberate and reasonable resource bound. The defect is coupling permanent-looking object removal and physical collision to that short render-history list.

### Reproduction

The crater probe locates the actual generated Oak at `(-6808.587891,197.387787,15543.897461)`, shoots no laser at it, detonates a bomb over its ground position, assembles a frame, then detonates seven further bombs 1,000 m apart. It queries the crown using actual Scenery::collide before, after first blast, and after the eighth blast.

```text
real_entity_collider_before_blast=4
real_entity_first_blast kind=Oak ... destroyed=1
real_entity_collider_after_blast=0
after_8_bombs crater_count=7 first_blast_destroyed=0
real_entity_collider_after_8_bombs=4
```

Kind 4 is EK_OAK + 1. This proves collision restoration, rather than just disappearance of an old cosmetic mark.

**Expected:** an object removed by a blast stays destroyed until the flight/reset boundary, independently of how many crater meshes remain visible. **Actual:** it and its collision silently return after seven unrelated remote blasts. Revisiting a cleared low-level route can encounter trees/buildings again.

**Fix direction:** keep the seven-item visual cap if desired, but record blast-destroyed entity identities or persistent affected cells separately. If restoration is an intentional game rule, make it explicit and avoid restoring collision through occupied space. Test eight separated blast sites, revisits and chunk trim/regeneration. The inspected development commit retains the same cap and mask reconstruction.

## WR-3: destroyed cross-chunk entities remain visible and cast shadows

**Status:** exact released data and render predicate reproduced; high confidence. **Priority:** P2.

### Cause

- `src/entities.cpp:336–348`: garden trees are generated in their parent lot's chunk, without checking the offset tree center against that chunk.
- `src/entities.cpp:354–371`: farm outbuildings are similarly emitted from the farmhouse anchor's chunk after rotating their offsets.
- `src/entities.cpp:164–169`: damage is stored under `chunkOf(e.z) * NC + chunkOf(e.x)`.
- `src/entities.cpp:145–150`: chunkAffected checks that coordinate-key chunk.
- `src/entity_render.cpp:244,269,283`: destruction filtering runs only when the **stored/rendered** chunk is affected. An unaffected chunk can bulk-copy its destroyed entity straight into the draw buffer.
- `src/entities.h:74` (`chunkOf` in this release): the float addition `(v + WORLD_HALF)` can also round a point just below a chunk boundary onto the next boundary. Example `e.x=-7744.000977` is emitted in chunk 125 but maps to 126.

### Reproduction

`world_probe` builds the release world, samples 21×21 chunks around MDB, ORC, HFS, the capital-area point `(1500,-3500)`, and KLO, and checks each emitted entity's stored chunk against chunkOf. Among 1,225,372 entities from 2,205 chunks, 130 mismatched.

A clear offset example, without the 1 mm float-boundary ambiguity:

```text
kind=Oak pos=(-6808.587891,197.387787,15543.897461)
source_chunk=(129,217) coordinate_chunk=(129,216)
seed=0.240118742
damage_destroyed=1 destroyed=1 owner_affected=0 coordinate_affected=1
```

Damage uses the entity's actual hit-point count. The original chunk still contains the entity, and the exact renderer gate sees `affected=false`, so it remains eligible for both the view and shadow buckets. Scenery collision/raycast checks destroyed independently and stop hitting it.

**Expected:** a successful weapon destruction removes visible geometry, its shadow and its collision together. **Actual:** some trees/farm buildings become visible but intangible and cannot be shot again.

**Fix direction:** enforce one canonical entity owner when placing every offset member, and share that owner/key between generation, damage and rendering; use numerically consistent chunk indexing near boundaries. Alternately invalidate all actual owner chunks rather than guessing ownership from position. Add garden/farm-edge destruction tests, float-boundary cases, and trim/rebuild checks.

**Unreleased comparison:** 84b8d1a adds C.inside checks for garden trees (`entities.cpp:385`) and each farm outbuilding (`:437–440`), plus expanded parent-lot search margins. That addresses the broad offset-placement cause in development. chunkOf retains the same float arithmetic, so the release's separate near-boundary cases need verification rather than declaring the entire class fixed. This audit has not run the whole new world's ownership sweep.

## WR-4: airport terminal chunk culling is smaller than the real terminal

**Status:** actual geometry and production culling/projection reproduced on CPU; high confidence. **Priority:** P2 in v3.43.0; remedy already present unreleased.

- `src/entity_render.cpp:245` uses `boxVisible(x0-12, ymin, z0-12, x1+12, ymax, z1+12)`.
- `src/entities.cpp:517–520` already knows that a chunk's contents can extend far beyond 12 m.
- Actual sampled terminal-containing chunk reaches are PVI 45.07 m, CAP 124.03 m, CDR 35.83 m, and KLO 95.03 m. Nominal bounds also do not necessarily include authored roof details.

`frustum_probe` builds the real released environment meshes, transforms their actual vertices by each actual airport entity, and uses the real Renderer::viewProj at 1920×1080 and 55° vertical FOV. It evaluates the released chunk-box predicate verbatim. For four terminals the chunk is rejected although entire front-facing triangles are strictly inside the viewport.

Most compact CDR reproduction:

```text
entity=(-22182.50000,419.66458,-5949.20020), chunk=(69,133)
camera=(-21962.50000,444.66458,-5949.20020)
heading=321 degrees, pitch=0, fovY=55 degrees, aspect=1920/1080
chunk_reach=35.82666, applied_padding=12
visible_mesh_vertices=78, fully_visible_front_facing_triangles=6
released_chunk_visible=false
```

PVI, CAP and KLO have independently logged passing triangle evidence. The output reports real mesh visibility, not an inferred oversized nominal box.

**Expected:** a terminal still occupying the edge of the screen continues drawing as the camera turns. **Actual:** the entire chunk can disappear before its visible terminal has left the view. No full GPU screenshot of these exact poses was needed/claimed; it is an exact CPU render-decision repro, with ordinary player-accessible camera poses.

**Development remedy already exists:** `84b8d1a:src/entity_render.cpp:253–259` explicitly replaces the 12 m rule with a cached union of authored/animated all-LOD instance bounds. The probe separately runs the exact development `entBuildLocalBounds` and `entInstanceBounds` functions on each released fixture: all four rejected cases become visible. This verifies that remedy's geometric mechanism, not the entire new development build. Incorporate/release the existing fix and retain the regression cases; do not duplicate its implementation.

## WR-5: a cache with a NaN height passes the loader

**Status:** reproduced local corruption; high confidence. **Priority:** P3 hardening.

`src/world.cpp:280–297` validates magic, stamp, exact vector lengths, complete reads and EOF, but neither data checksum nor finite/range constraints before publishing arrays. `world_cache_test.cpp` thoroughly checks structural corruption but not valid-length payload corruption.

The probe makes a separate copy of its own generated cache, overwrites exactly one base-height float involved in the MDB center bilinear sample with NaN, and leaves all sizes, stamp and other bytes intact:

```text
accepted=1 sample_runway_height=nan finite=0
```

Expected: reject and regenerate cache. Actual: publish a world whose height sampler returns NaN. Code consuming those values has no general finite-ground guard. This requires corrupted local cache bytes, not ordinary controls or a remote input, and no installed player cache was modified.

Recommendation: checksum/version the payload and reject non-finite heights/amplitudes/bounds, with appropriate ranges. If validation is used without a checksum, also ensure derived envelopes actually bound height data. Preserve the good loader's exact-size/atomic-publication behavior. Development has the same missing validation at `84b8d1a:src/world.cpp:300–315`.

## WR-6: missing project LICENSE in Windows package

**Status:** reproduced package contents; high confidence. **Priority:** P3 documentation.

The ZIP includes THIRD_PARTY_NOTICES.md and all three voice-license texts, but no top-level LICENSE. Its packaged README line 235 explicitly says “See LICENSE.” `.github/workflows/build.yml:78` copies README/notices/diagnostics/radio config while omitting LICENSE.

Include the repository's project LICENSE in the packaged file list or revise the distribution documentation to point to an accessible intended license. This is an incomplete shipped reference; no claim of a third-party licensing violation is made.

## Passed coverage

### Package

- ZIP decompression / CRC checked for all 1,187 entries: no failed entry.
- Expected size/hash verified against supplied release metadata.
- Expanded size 52,834,701 bytes; no absolute/path-traversal entries; no case-insensitive duplicate names.
- VERSION.txt is `v3.43.0`.
- All 1,181 expected shipped assets under loading/materials/voice are present. JPEG and audio assets match release source bytes; text files differ only in expected LF/CRLF checkout conversion.
- Packaged README, radio configuration, third-party notices and diagnostics script match the release source after line-ending normalization.
- Three referenced voice-license files are present.
- Executable is PE32+ AMD64/x64, Windows GUI. objdump imports: OPENGL32, GDI32, WINMM, MFPlay, MFPlat, ole32, KERNEL32 and USER32; no missing private sidecar DLL requirement was found.
- Optional menu.mp4 is absent from both the checked release source and the archive. It is conditionally packaged and is not a missing mandatory runtime asset.

### Terrain / streaming / collision

- All 16 runway surfaces sampled every 20 m along their centerline plus both edges: maximum measured height discrepancy from published runway elevation was 0.000000 m.
- 36 real chunks: level-1→level-2 generation, trimming/eviction and regeneration produced identical entity arrays.
- Same 36 chunks generated through the actual six-worker asynchronous request/pump path: no missing or mismatched final chunks.
- 20 clear/invalidation rounds while jobs were active, followed by final-epoch reconstruction: all 36 chunks matched the synchronous baseline.
- These are deterministic functional concurrency tests, not a ThreadSanitizer proof.
- Parent stock exact-release CTest log reports 41/41 passing. Relevant shipped tests include airport layouts, world cache structural rejection, terrain envelopes, entity raycast vs fine sweep, breakup component/mass/dynamics checks, foliage/building mesh contracts and terrain shadow bake. They do not cover the above cross-subsystem crater/persistence/ownership/frustum cases.

### Traffic

- Ran the real deterministic Traffic subsystem (initial seed 4242), 900 simulated seconds at 60 Hz at each of MDB, CAP, KLO, CDR and FAR, with player observation point airport+(4000,300,4000).
- Zero sampled airport-aircraft body-center overlaps with static scenery, checking once per simulated second using radius 0.25 m during ground states. This does not prove full-wing clearance at every substep.
- Airport craft continued cycling through parked/taxi/hold/climb/circuit/final/rollout states. Maximum observed ground-state dwell was approximately 167 s at CAP; this was not enough to establish deadlock.
- Active craft at each sample finished with finite positions/speeds. The model intentionally excludes player-ground collisions and escort collisions (`traffic.cpp:820–821`); these exclusions are documented here as simulation limitations rather than falsely called memory/physics faults.

### Resource bounds / breakup inspection

- Dynamic visual crater cap 7, laser scorch cap 16, simultaneous plasma blast cap 6, particles approximately 9,001, crash pits cap 20, breakup pieces cap 16, and debris drawing cap 16 are explicit. Their presence prevents several obvious unbounded visual accumulations; crater persistence needs the separation described in WR-2.
- Worker queue capped at twice worker count, at most six workers. Generated chunks are pruned by the renderer's trim cadence.
- Breakup sources were inspected for piece-array limits, mass/body partitioning and integration substeps. The parent stock breakup test passed; this worker did not invent a separate breakup failure after that pass.

## Limits

- Exact released Windows binary was inspected and hash-checked, but not executed on real Windows hardware by this worker. Full native driver behavior, Windows filesystem edge cases and interactive input are covered only where another audit worker supplies evidence.
- Native CPU game probes use unchanged source logic and test-only access via the existing GameTest friendship. They do not replace manual playthroughs.
- No claim that the entire 80 km world, every weather/LOD/camera combination or every possible traffic seed was exhaustively enumerated. The sampled ownership and culling counterexamples are sufficient for their findings.
- Authored development bounds were compared explicitly and separately; newer scenery is not attributed to the release.
- Exclude large generated caches, executable copies and compiler outputs from the user-facing evidence archive; preserve source probes/logs and this report.

## Visual follow-up observation (cause not isolated)

The renderer worker supplied actual exact-release captures `previews/renderer/cloak_mesh_partial_960.ppm` and `previews/renderer/cloak_mesh_partial_settled_960.ppm`, scene `wr_3_210_12_16_3`, quality 1, native 960×540 render, traffic disabled, synchronous entity generation (ENTSTREAM unset). Both were visually inspected. Strong rectilinear field/canopy boundaries and blue-speckled far vegetation remain after increasing from 3 initial update/render + 3 static frames to 3 initial + 20 static frames. This is useful visual evidence for a dedicated terrain/material/foliage review. It is not currently attributed to streaming, shadow invalidation or a particular code defect; no pending-count or shadow-settle instrumentation was available in that stock capture. Do not count it as another confirmed failure merely from appearance.

## Reproduction packaging update

The probes now use `AUDIT_OUTPUT_DIR` for private caches, with executable-relative defaults. `repros/world-release/run_probes.sh` supports SOURCE_DIR / BUILD_DIR / AUDIT_OUTPUT_DIR / RELEASE_ZIP, verifies the exact release commit, checks build-source provenance, links one standalone probe at a time, and writes separate timestamped logs. It does not run a competing build. The development bounds excerpt remains explicitly labeled and isolated. Bash/Python/all six C++ syntax checks passed; the ZIP mode passed; the streaming mode passed when launched from `/tmp` with explicit environment paths. Original diagnostic results were preserved. See `logs/world-release/portability-validation.txt`.
