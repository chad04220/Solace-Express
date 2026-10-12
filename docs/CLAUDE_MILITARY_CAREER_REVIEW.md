# Military career and terminal review

## Baseline and scope

Separate local review branch `codex/military-career-review-20261012`, based on verified upstream default-branch head `ac111a603860436ef77aecba1346bc4764dd2b2c` (released [v3.49.1](https://github.com/chad04220/Solace-Express/releases/tag/v3.49.1)). No release, merge, or main/default-branch update is part of this review. Existing Earth tooling and all unrelated content are preserved.

The military terminal borrows the research terminal's clear three-column organization: mission register, assigned-aircraft native preview, and scrollable briefing. Its restrained olive palette is confined to military service, pause, debrief and loading. Civilian and research palettes stay unchanged.

## Missions

The visible board contains nine service-funded XR sorties. Aircraft, fuel, equipment and stores are included; military awards remain separate from civilian money and records.

| Mission | Aircraft | Actual objective | Kit | Award |
|---|---|---|---|---:|
| Nightjar local survey | XR-10 | Two local structures, clear forward sensor dwell for 3 s inside 1.6 km | Unarmed sensors | 800 SC |
| Mantis island circuit | XR-20 | Three nearby airport structures, 4 s sensor dwell inside 2.4 km | Unarmed sensors | 1,100 SC |
| Specter high-speed intelligence | XR-30 | Two sites, 3 s dwell inside 7 km at 3,500–6,500 m MSL and at least 180 m/s horizontal ground speed | Unarmed long-range sensors | 1,500 SC |
| Wraith precision hover scan | XR-40 | One roof-relative close scan, 8 s dwell inside 250 m, 30–120 m above roof and at most 12 m/s total motion | Pulse / EMP | 1,300 SC |
| Wraith interceptor sweep | XR-40 | Destroy three designated Needle fighters; extract | Kinetic / EMP | 1,400 SC |
| Defend the island | XR-40 | Preserve the control tower through three mixed waves, clear hostile ordnance; extract | Pulse / Plasma | 1,200 SC |
| Wraith bomber defense | XR-40 | Preserve a hangar through four bomber-led waves and live ordnance; extract | Heavy / Plasma | 2,200 SC |
| Wraith relay suppression | XR-40 | Destroy three Cantor relays guarded by two optional Bastion escorts; extract | Heavy / EMP | 2,000 SC |
| Strike the command group | XR-40 | Destroy designated Archon and Cantor; optional Needle escort; extract | Pulse / Penetrator | 1,800 SC |

Reconnaissance uses the actual existing airport structure locations and line-of-sight checks. Sensor prerequisites are enforced by the live mission update and surfaced early in the compact HUD. EMP disrupts shields but does not destroy hulls. Heavy guns retain their authored charge delay. All missions finish only after the existing extraction check.

## Save and mode boundaries

- Existing saves load forward into this review. As with any expanded catalogue, older executables cannot interpret an active new mission ID; retain an independent save backup when switching builds.
- Persisted mission IDs 0–2 and contract IDs 11–13 retain their meanings. New IDs are appended. Version 6 adds an explicit successful-mission bitmask; v1–v5 saves load forward, preserving existing service totals without inferring mission completion from aggregate successes. Older executables cannot read v6 saves.
- The introductory trainer recon remains loadable/replayable for existing active saves, although the new board lists only XR sorties.
- The catalogue is authoritative for aircraft, reward, loadout and mission identity. Enemies remain in the separate Hive roster, never selectable as player aircraft.
- Launch, settlement, duplicate reward prevention, retry and pending-save handling continue through the career transaction system.
- Returning a loaner resets combat actors/projectiles, Wraith effects, damage, craters, transient flight state and military input edges. A failed cancellation save remains pending and blocks another launch.
- Baseline reproduction found two failures: Escape and controller B could cancel loading and then be processed again by the hub in the same frame. `returnToMilitary()` consumes those edges. Military debrief uses focused buttons instead of the generic unconditional Enter shortcut, preserving keyboard/controller retry.

## Ordered progression and classified access

The canonical sequence is the board order above: stable IDs `3,4,5,6,7,1,8,9,2`. Only Nightjar local survey is initially available. Successful objective completion followed by extraction records that mission and unlocks the next; completed missions remain replayable. Failures, aborts, loading cancellations and research practice do not record success. The legacy trainer is compatible but does not advance the XR sequence.

Locked cards remain selectable for inspection, but names, aircraft identities, roles, objectives, kit and rewards are redacted. Their real aircraft geometry uses the existing hangar material-independent silhouette pass, retaining a visible rim with no paint, registrations, cockpit screens or exhaust. Unlocked cards immediately return to the ordinary fully lit aircraft preview. Button focus, career eligibility, committed launch, direct flight start and loading readiness each enforce the relevant access/token boundary.

A valid old accepted attempt can still settle that exact mission in the career layer. It does not grant new locked attempts; finishing it out of order records only its own success, and later cards still require the complete predecessor chain. Production session reload continues the existing behavior of returning interrupted military attempts to the briefing without awarding success.

## Loading and preview contract

The loading picture key comes from the actual assigned `plane.spec`, never the research/hangar selection. Existing packaged native aircraft pictures are reused. If a picture is absent, the actual frozen sortie is shown through an exterior native camera; the military flow does not fall back to an unrelated airport map.

The mission scene renders and streams behind the card. Readiness still requires distinct completed scene frames, no pending scenery, no required shadow work, and no new bake. Time or repeated updates alone cannot complete loading. No fabricated progress/timers are added, and combat/flight clocks stay frozen before departure. The ready prompt remains an explicit Enter/click/controller-A action.

## Validation

Verified in the local review environment on 2026-10-12:

- Linux Release build: all default targets, plus production UI capture harnesses, built successfully.
- 120 distinct configured CTests passed across the 113-test aggregate run, four additional GPU suites, and three GLSL-validation suites. No suite was silently substituted. The aggregate uses the repository's `FLIGHT_QUICK=1` representative flight-model coverage.
- Military career: 723 assertions in normal and fast-math builds, including pre-expansion active v5 saves.
- Military gameplay: 374 assertions, including explicit keyboard/controller paging and briefing-scroll bounds at 960×540, covering live mission transitions, sensor constraints, equipment, loading, keyboard/controller and cleanup.
- Focused ASan + UBSan: military career, military gameplay and world fixture passed, with no sanitizer report.
- Windows x86-64 MinGW cross-build: `SolaceExpress.exe` linked successfully. The executable was not run on native Windows in this workspace; no remote CI/release was started.
- Full unmodified production renderer initialized on Mesa llvmpipe. Native 1280×720 game frames were captured and visually inspected. Additional 960×540 / 140% UI-only captures verified narrow-window readability; these are layout evidence only, not additional aircraft render evidence.
- Keyboard/controller controls were exercised at 1920×1080, 1280×720 and 960×540. The military palette does not replace civilian or research colors.

Logs are in `docs/military-review/`. GPU hardware performance, physical-controller feel and manual flight/aiming still need owner validation. Source assertions were not weakened to make the suite pass.

### Finite-ammunition combat evidence

The dynamic test keeps enemy AI, attack projectiles, relay regeneration, authored wave timing, weapon heat/charge/cooldown and objective damage running continuously. Every fourth finite gun round is deliberately spent as a miss. There is no rearm. Aircraft motion follows a scripted 180 m/s orbit and impact positions are scripted, so this is a combat-logic/ammunition feasibility check rather than a human-aim or manual-piloting demonstration.

| Sortie | Time | Gun rounds used | Deliberate misses | Bombs used | Enemy projectiles | Protected site |
|---|---:|---:|---:|---:|---:|---:|
| Island defense | 93.3 s | 233 / 500 | 58 | 3 / 6 | 28 | 511 / 600 |
| Command strike | 81.0 s | 214 / 500 | 53 | 1 / 8 | 44 | n/a |
| Interceptor sweep | 9.8 s | 51 / 360 | 12 | 0 / 4 | 5 | n/a |
| Bomber defense | 169.1 s | 42 / 48 | 10 | 4 / 6 | 79 | 422 / 600 |
| Relay suppression | 69.6 s | 19 / 48 | 4 | 3 / 4 | 12 | n/a |

All five reached successful extraction. The isolated bomber-defense no-splash fixture also completes with 38 rounds and four bombs at 20% misses, or 37 rounds and four bombs with 100 shield on every enemy. This review reduced its initial excessive enemy count to four Bastions, two Needles and one Cantor across four waves; the original prototype forced undisclosed splash tactics even at perfect accuracy.

### Preview inventory

All committed PNGs below are native production game renders, not mockups:

- `military-review/military-terminal-xr10.png`: final service terminal, Nightjar survey
- `military-review/military-terminal-xr40.png`: final service terminal, Wraith relay suppression
- `military-review/military-loading-live-xr40.png`: ready-to-fly view with packaged picture deliberately absent, proving actual-sortie exterior fallback
- `military-review/military-pause.png`: military pause controls
- `military-review/military-debrief.png`: aborted-sortie debrief and retry/return controls

The separately delivered ready-to-fly XR-40 loading screenshot uses the existing packaged aircraft image. It is explicitly the ready state, not an image of work still pending.

### Review entry points

- `src/military_missions.h`: append-only mission catalogue and sensor envelope
- `src/career.cpp` / `src/career.h`: authoritative loaners, awards and save validation
- `src/game_military_ui.cpp`: scoped military terminal, pause and debrief
- `src/game.cpp`: selected-airframe preview, actual-sortie loading camera, cleanup and encounter construction
- `tests/military_career_test.cpp`: every ID, persistence, settlement and civilian isolation
- `tests/military_gameplay_test.cpp`: production Game-layer loading/lifecycle, objectives, weapon and input tests
- `tests/ui_review_harness.cpp`: production full-scene capture names `military3`, `military5`, `military9`, `military_loading3`, `military_loading9`

### Reproducing the focused checks

```sh
cmake -S . -B build-review -DCMAKE_BUILD_TYPE=Release
cmake --build build-review -j4
FLIGHT_QUICK=1 ctest --test-dir build-review --output-on-failure -j4
cmake --build build-review --target ui_review_harness -j4
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 SHADERCACHE=/tmp/solace-military-cache FRAMES=8 \
  build-review/ui_review_harness military3,military9,military_loading9live,military_debrief9,military_pause9 \
  /tmp/solace-military-captures 1280 720 1 scene
```

Install the standard `glslangValidator` tool before configuring to register the three optional GLSL-validation suites. The local review explicitly installed it and ran all three successfully. `military_loading9live` suppresses only the packaged picture in the test fixture, exercising the unchanged production missing-image path.

Final focused gameplay rerun: all 374 assertions pass in both Release and ASan/UBSan after the pagination additions. The final Windows cross-build SHA-256 is `d581dd428ac9de3c417b86ecbe11e5981a812d4c652946ed6f92c01564d0f8da`; its executable remains outside source control and is not a packaged release.

## Ordered-progression follow-up validation

The owner requested sequential unlocks after reviewing the initial military UI. This follow-up adds the v6 per-mission record and classified UI described above.

- Military career: 1,560 checks pass in both normal and fast-math builds.
- Military gameplay: 474 checks pass, including actual scan/extraction unlock, replay, failure/abort/practice isolation, failed-success-save retry, stale/direct/keyboard/controller/mouse launch attempts and available → locked → available renderer frames.
- Classified camera tests ray-cast actual hangar room triangles across three aircraft, all sampled yaw/pitch/zoom extremes, five aircraft landmarks and three viewport sizes. Review caught the original outdoor-distance camera being behind showroom walls; the military silhouette room now encloses the full orbit.
- Independent audit checked all 1,024 completion masks (9,216 eligibility assertions) and a saturated all-failures ledger success/save/reload. Both pass; no remaining source blocker.
- ASan/UBSan military career/gameplay plus the world fixture pass (three suites, no sanitizer report).
- The 120-test aggregate initially passed 119; its existing survey-migration test built a v3 fixture by removing v5 fields but retained the new v6 progress line. The fixture now removes that anachronistic line too; assertions and production parser strictness are unchanged. The unrelated unknown-version rejection fixture now genuinely uses unsupported v7 and separately checks incomplete v6.
- Windows cross-build linked the progression revision successfully. Its executable SHA-256 is `0b45ad2db404fc91fa15cdb8be25f7f962d3bb6250348e0ddc8eceb6c1d4c299` (not a release).

Native same-aircraft before/after frames are in `military-review/progression/mission-02-classified.png` and `mission-02-unlocked.png`, with `mission-01-available.png` showing a fresh career. Screenshot fixtures explicitly seed the relevant completed mission record; the gameplay tests separately prove live successful extraction causes the unlock. The classified frame uses the unchanged hangar silhouette shader with military-only exposure adjustment so its dark rim remains visible under the terminal overlay.

The corrected survey-migration and career-save suites both pass, together with their world fixture; all 120 distinct suites are now passing across the aggregate and focused rerun.

## Research and hangar preview follow-up

The owner subsequently requested cleaner research XR-40 equipment controls and styled Rotate/Zoom/Reset controls in research and hangar menus. See [the focused preview-controls handoff](CLAUDE_PREVIEW_CONTROLS_REVIEW.md) for behavior, input coverage and native captures. This is a separate logical change after ordered progression.

## Claude's review (2026-10-12)

Merged into `claude/compassionate-davinci-4cfo20` on top of the road-lookup and diagnostics work since v3.49.1
(`62bd74f`..`28a1bb5`); the merge was clean.

- Read: the save format (v6 `military_progress` line, strict parse, the v1-v5 forward path that infers no completions),
  the append-only catalogue and its `static_assert`s on the persisted IDs, the ordered-unlock gate in `beginMilitary`
  and `canFly` (an accepted older attempt may settle only itself), settlement, and `returnToMilitary`'s reset (the
  scenery budget back to 2.5 ms after a cancelled load). No renderer or shader source changes; the military preview
  reuses the hangar's silhouette pass through `FrameParams`.
- Validated on the merged tree: Release build, all 121 CTest suites (the new `hangar_preview_controls` among them),
  and the MinGW cross-build of `SolaceExpress.exe` and the new test.
- Shipped in v3.49.2's notes. Hardware frame times, physical controllers and hand-flown combat remain for the owner.

