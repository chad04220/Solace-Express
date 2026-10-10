# Solace Express v3.44.0: bounded world/release delta audit

## Bottom line

The newly published **v3.44.0** fixes the original terminal parent-frustum defect (WR-4). It reduces but does not eliminate WR-3's destroyed-but-visible scenery problem: the repeated ownership sweep drops from 130 mismatched entities to 13. WR-1, WR-2, WR-5 and WR-6 remain. Every carryover below has new v3.44 test or archive evidence; these are not merely source-only assumptions.

This is an addendum to `../../reports/world-release.md`, not a replacement audit or a claim that the entire new release has been exhaustively retested.

- Source: `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e`, isolated `solace-v344-audit` checkout.
- Published asset: [SolaceExpress-windows-x64.zip, v3.44.0](https://github.com/chad04220/Solace-Express/releases/download/v3.44.0/SolaceExpress-windows-x64.zip).
- Download: **107,043,513 bytes**, SHA-256 **a6a559a817bc95b60617fb9bfbeca2ef79f41e8bf296e772a262033691c0643d**.
- All probes linked the parent's completed stock v3.44 `game_objs` objects. No production source edits, new Ninja build, push, installed player file mutation or Windows execution.

## Carryover findings

### WR-1: plasma crater/contact mismatch remains — P2, high confidence

**New reproduction:** `repros/world-release/run_probes.sh crater`, log `logs/world-release/runs/run-20261010T165114Z-y2STpZ/crater-probe.log`.

At MDB `(-8000,45,14000)`, a real XR-40 plasma detonation creates radius 24 m, stored depth -8 m. The render formula gives center floor **37.000000 m** while world contact and `Game::wreckGround` give **45.000000 m**. After 600 actual `Game::update(1/60)` calls, with `buildFrame` every frame, the aircraft is grounded and uncrashed, bottom **44.983555 m**, floating **7.983555 m** above the displayed crater floor. This excludes an unobserved delayed frame overlay. The harness places the aircraft at the site after detonation; it is not a hand-flown landing replay.

**Source:** `src/game_wraith.cpp:252–258,416–424`; `src/shaders/common.glsl:23–34,86`; `src/aircraft.cpp:494–499`; `src/game.cpp:1345–1348,2134–2140`. The weapons/aircraft/common-shader files are unchanged from v3.43; the game-file change only appends volcano rendering.

**Expected / actual:** consistent visible and physical terrain / plasma deformation stays render-only. The new permanent summit crater does affect actual world terrain, but that separate feature does not repair dynamic bomb craters. **Recommendation:** share a consistent deformation sampler between render, player contact, weapon ground hits and wreck contact, or intentionally replace the physical crater presentation with a non-deforming effect.

### WR-2: eighth plasma bomb restores earlier scenery collision — P2, high confidence

**New reproduction:** same crater log. The old garden fixture no longer exists, so the probe selects the first real oak in a fixed ordered chunk search. Its new coordinates are **(-9196.251953,224.486023,13617.464844)**. At its crown, actual `Scenery::collide` returns **4 before blast → 0 after blast → 4 after eight total blasts**. Later blasts are spaced 1,000 m away. The retained crater count is seven, and the first oak's `destroyed` state becomes false again.

**Source:** `src/game_wraith.cpp:257–258` retains seven crater effects; `src/game.cpp:2728–2733` rebuilds scenery masks from those retained effects; `src/entities.cpp:139–149` decides disappearance from current masks. Durable laser damage is still tracked separately at `src/entities.cpp:166–174`.

**Expected / actual:** expired visuals need not preserve a crater mesh forever, but destroyed obstacle collision should not silently return while the session is active. This finding concerns coupled physical restoration, not the reasonable bounded visual budget. **Recommendation:** persist destruction independently of the seven-entry rendered crater list and explicitly reset it at the intended session boundary.

### WR-3: partial fix; float-boundary ownership still bypasses renderer damage filtering — P2, high confidence

**New reproduction:** `run_probes.sh world`, log `logs/world-release/runs/run-20261010T165033Z-gYAhn7/world-probe.log`. The same five centers and 21×21 chunks per center inspect **2,205 chunks / 1,214,634 entities**, with **13 ownership mismatches**. v3.43 produced 130 mismatches in 1,225,372 entities. Counts are per sampled world, not a global prevalence estimate.

Example: Pine **(-7744.000977,149.351486,12111.620117)**, seed **0.807475686**, is generated in chunk **(125,203)** but `chunkOf(x)` resolves **126**. After real `damage` reaches its hit points: `destroyed=1`, stored/rendered owner `chunkAffected=0`, coordinate-key chunk `chunkAffected=1`. Similar examples occur just below z boundaries. The renderer continues gating destruction filtering on the stored chunk's affected flag.

**What was fixed:** new street-grid gardens and farm outbuildings explicitly use their own center for placement (`src/entities.cpp:385,437–440`), avoiding the earlier deliberate multi-meter ownership errors.

**What remains:** `src/entities.h:88` still evaluates `floorf((v + WORLD_HALF) / CH)` in float. Adding 40,000 rounds coordinates just below a 256 m boundary onto it. `Ctx::inside` at `src/entities.cpp:181–185` uses direct comparisons, so it disagrees with the damage index. `src/entities.cpp:142,150,173` indexes affected/destroyed state by `chunkOf`; `src/entity_render.cpp:252,284,298` still relies on stored-chunk gating.

**Expected / actual:** generation, damage, collision and render ownership agree / damage is associated with a different chunk and can leave removed scenery visible or shadow-casting. The new test proves the wrong affected flag on real generated entities; the render consequence follows the exact production predicate, without a new GPU screenshot. **Recommendation:** use one consistent, sufficiently precise ownership calculation (e.g. double intermediate before floor) in both placement and damage indexing, with exact-boundary regression fixtures and another ownership sweep.

### WR-4: fixed in v3.44.0 — four original adversarial airport cases retained

The solution previously labeled *unreleased* in the v3.43 report is now shipped. `src/entity_render.cpp:253–259` uses authored all-LOD chunk bounds; `src/entity_bounds.h:78–102` caches by monotonic generation revision. Generation, trim and asynchronous publication update revision in `src/entities.cpp:98,600,620`. These world/bounds source files are unchanged from the separately inspected `84b8d1a` development implementation.

**New reproduction:** `run_probes.sh frustum`, log `logs/world-release/runs/run-20261010T165119Z-sjj79y/frustum-probe.log`. The probe directly includes released `entity_bounds.h`, builds released meshes and tests actual `Renderer::viewProj`. For **PVI, CAP, CDR and KLO**, old nominal padding rejects a terminal with fully in-frame front-facing triangles, but the released authored bound reports **visible=1 in all four cases**. The same camera/headings as the original report still expose the old predicate; newly authored geometry changes vertex counts, not this result.

This is verified CPU geometry/culling evidence, not a screenshot or exhaustive proof of every animated bound. Do not request a duplicate terminal-padding fix.

### WR-5: structurally valid corrupt world cache still accepts NaN — P3 hardening, high confidence

**New reproduction:** `run_probes.sh cache`, log `logs/world-release/runs/run-20261010T165123Z-UUITaH/cache-probe.log`. A copied audit-generated v3.44 cache retains its magic, stamp and vector lengths; one MDB bilinear height float becomes NaN. Result: **accepted=1, sample_runway_height=nan, finite=0**.

The new format magic is correctly bumped to **WLD3**, preventing ordinary reuse of a WLD1 cache. This does not validate payload values: `src/world.cpp:300–320` still checks structural size/read/EOF only. The cause, impact and recommendation from the original finding remain: reject nonfinite/inconsistent payloads or check a payload checksum, then regenerate. No naturally occurring corruption was observed, and no remote input vector is established; only our private copied cache was changed.

### WR-6: project LICENSE still absent from published ZIP — P3 documentation, high confidence

The new archive's top-level files still omit `LICENSE`, although packaged `README.md:235` says to see it. `.github/workflows/build.yml:78` omits it from the copy list. **New archive inspection confirms this**, rather than relying only on workflow text. Include the existing project license in future distributions. This is a documentation/completeness finding, not a legal determination. Updated third-party notices and all three voice license files are present.

## Changed-world checks and passed coverage

1. **Permanent volcano geometry is bounded as designed.** Independently compared both versions' own generated heightmap caches: **16,777,216 float bit patterns**, all new values finite. Exactly **558 texels** change, in height and detail-amplitude channels only; **zero differing floats outside the 520 m summit disk**. The farthest changed center is 519.193382 m. Source is `src/world.cpp:261–280`; this is real terrain, distinct from the remaining plasma crater defect. Command: `python3 repros/world-release/compare_terrain.py`; result `logs/world-release/terrain-array-delta.json`. This does not compare derived envelope arrays or claim every rendered sample outside that disk is unchanged, since bilinear footprints cross texel boundaries.
2. **All 16 runway centers and two edges, sampled every 20 m**, still have exactly zero elevation error in the independent world probe. Parent-owned runway-preservation and airport tests supply broader coverage separately; their results are not assumed here.
3. **New generation/stream publication stayed deterministic in the repeated probe.** 36 chunks, level upgrade then trim/regenerate: zero mismatches. Six-worker async generation: 36 chunks, zero mismatches. Twenty clear-with-live-work rounds: final 36 chunks, zero mismatches. Log `logs/world-release/runs/run-20261010T165103Z-YbDhrK/stream-probe.log`. This is not a ThreadSanitizer run.
4. **Source-only review of new safety/resource guards:** airport additions are stationary and tested against runway/approach, apron/access, grading and overlap bounds (`src/airport_scenery.cpp:32–87,248–307`). New roads cap the segment list at 64 and reject unsuitable routes before generation (`src/scenery.cpp:155–217`). Volcano effects cap smoke at 64 and embers at 40, distance-cull, and respect the 12-light capacity (`src/volcano_effects.h:9,23–59`). These observations are not a fresh exhaustive collision/visual proof.
5. **Traffic and aircraft contact/breakup core files did not change.** The earlier five-airport 900-second traffic exercise and v3.43 breakup results are historical coverage only; those long simulations were not rerun in this bounded delta pass. New scenery can affect traffic interactions, so unchanged simulator source alone is not claimed as full traffic clearance.

## Published package integrity

Command: `run_probes.sh zip`; machine-readable result `logs/world-release/runs/run-20261010T165101Z-uP5zyi/zip-integrity.json`.

- All **1,230 ZIP entries** pass CRC; expanded size **118,500,584 bytes**.
- No absolute/traversal/colon paths or case-insensitive name collisions found.
- `VERSION.txt` identifies **v3.44.0**. Executable signature is PE and machine **0x8664 (AMD64)**; the Windows binary was not run.
- All **1,224 expected packaged source assets** are present and match after line-ending normalization. This includes nested high/environment JPEGs and the newly copied materials manifest. No expected runtime material/voice/loading asset is missing.
- README, diagnostics, radio stations and updated third-party notices match source after line-ending normalization. All three voice-license files are included.
- `.github/workflows/build.yml:83` now recursively copies materials, correctly carrying the nested material sets that the old top-level-only glob would omit.

## Reproduction and limits

`repros/world-release/README.md` documents script-relative defaults and `SOURCE_DIR`, `BUILD_DIR`, `AUDIT_OUTPUT_DIR`, `RELEASE_ZIP`, plus the previous-cache override for terrain comparison. The runner guards the exact release and source/build pairing and never starts Ninja. Bash/Python syntax and all invoked probes succeeded; known-defect diagnostics deliberately report observations rather than requiring nonzero exit status.

The old v3.43 report, original diagnostic logs and explicitly labeled development excerpt are preserved. Do not package generated cache files, binaries, extracted executable or the 107 MB release ZIP as small audit evidence. No manual whole-game playthrough, new GPU terminal captures, Windows execution or new full memory/race sanitizer pass is implied. Parent-owned stock test results should be reported independently once complete.
