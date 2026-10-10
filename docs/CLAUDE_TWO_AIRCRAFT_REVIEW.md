# Solace Express two aircraft review

Local proposal on `codex/two-new-aircraft-review-20261010`, based on **v3.44.0**, commit `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e`.

Review two native flyable aircraft developed from the lightplane/airliner idea: distinct authored exteriors and cockpits, ordinary flight physics, career access and Free Flight. The Atlas is the largest flyable airframe. The expanded review also covers Atlas's requested compact, fully underwing bays and streamlined fairings, shared covered wheels, all-fleet gear and airframe breakup, an all-15-aircraft visual audit, an exact-preserving XR-30/XR-40 build-interface refactor, and manual XR-40 full-VTOL hold throttle.

**Ready for local review, 2026-10-10 22:31 UTC:** the final Release build passes **73/73 CTests in 289.81 seconds**, the full Windows cross-build passes, and **ASan/UBSan passes 69/69 selected CTests in 1,515.20 seconds with full flight coverage and exit code 0**. The final native sweep is accepted across **all 15 aircraft: 283 images at 1920×1080, 1,132 rendered frames and zero GL errors**, comprising 147 exterior and 136 cockpit views. All 28 confirmed cockpit findings are resolved. The final-guidance clean-ABI comparison passes **3/3 exact native fixtures**; the XR build-interface refactor also retains its eight exact native comparisons. The four shader checks excluded from the sanitizer run passed in Release, as detailed below. This is a local review handoff, not native Windows hardware approval. Local preparation and commits are in scope. **The user approved pushing `codex/two-new-aircraft-review-20261010` at 22:31:49 UTC; merging or publishing a release is not authorized.**

## Upstream integration and diagnostic safety

**Upstream release checkpoint, 2026-10-10 21:46 UTC:** [v3.45.0 was published at 20:58:17 UTC](https://github.com/chad04220/Solace-Express/releases/tag/v3.45.0), targeting `033e03ca5ca5e2f5c6753abb6b968d41138c06f5`. This proposal still uses the v3.44.0 b22 base; the published upstream release and this local branch are separate source lines. v3.45.1 had release notes only at the 21:46 check; no published v3.45.1 release is claimed here. The earlier [b22-to-adb700a](https://github.com/chad04220/Solace-Express/compare/b22c2fca66fff88b62801a79f9f0c6edf06dcb9e...adb700acca71f497ba32b963356359bb1e5aa77d) and [adb700a-to-5e0981e](https://github.com/chad04220/Solace-Express/compare/adb700acca71f497ba32b963356359bb1e5aa77d...5e0981eef6f9485e47fe1db1eeb642b2de5506a4) comparisons remain historical integration context, not a current-tip inventory.

The upstream work adds **100 km world wrapping** across aircraft stepping, camera/particles, autopilot routes, terrain/scenery and shaders. It also rewrites test builds around a shared object library and adds a daily full-flight sanitizer workflow. Together with the preceding audit fixes, this overlaps the proposal's aircraft/game code, `CMakeLists.txt`, career code and flight/gameplay tests. Preserve these changes during eventual Claude integration. Upstream's archived `docs/reviews/v3.44.0/REVIEW.md` describes a historical audit; it is not this proposal's current open-task status.

**Preserve the post-v3.45 terrain fix:** commit [`9097edde6b55d14484a4a9ab88d6c859f3705bd6`](https://github.com/chad04220/Solace-Express/commit/9097edde6b55d14484a4a9ab88d6c859f3705bd6) corrects a v3.45 terrain-material coordinate regression. The wrapping change supplied `(x, z, height)` where the material path expects `(x, height, z)`, causing wrong height/material and road/runway lookups. The fix restores the expected order and adds a source regression test. Retain both when combining the branches. The local b22-based proposal is **not shown affected by that later regression**. The same upstream correction clarifies that roads currently stop at water and resume on the far side; bridge decks are not modeled. Do not carry the earlier release-note bridge claim forward as rendered geometry.

**CI coverage changed upstream:** commit [`bd0cd201dd1be7dff1bc9e83c61f1934dabb5439`](https://github.com/chad04220/Solace-Express/commit/bd0cd201dd1be7dff1bc9e83c61f1934dabb5439) selects affected tests for branch pushes using the changed files and build dependencies. Documentation-only pushes can select no tests; the configured nightly/release routes still request full coverage. A green branch status therefore does not automatically mean the complete suite ran. Inspect the exact run's test-selection summary and results, and run the full combined-source suite for integration. Neither older upstream CI nor a selected-test run validates this local proposal or its eventual merge.

Upstream commit [`fe0bdeb3c54da252d718cc83d340d59a8de91b0a`](https://github.com/chad04220/Solace-Express/commit/fe0bdeb3c54da252d718cc83d340d59a8de91b0a) isolates Windows diagnostic modes (`--bench`, `--shots`, `--profile`, `--analyze`, `--loadshots`) in memory and prevents career, settings and stations writes. **This v3.44-based branch does not contain that later guard. Do not run its diagnostic commands against a real career profile.** Use disposable copied user data in a verified isolated Windows test profile, or first integrate and verify the upstream guard. A separate executable folder alone does not isolate `%APPDATA%\SolaceExpress`.

Preserve the diagnostic guard, terrain coordinate fix and other intervening audit/build/world changes during eventual Claude integration; do not replace newer upstream files wholesale with this older-base proposal. Resolve overlapping changes deliberately and rerun complete combined-source tests. No production rebase or integration is performed by this handoff. The diagnostic caution identifies an uncontained write risk; it is not evidence that any user's data was harmed.

## Aircraft and native integration

| | Larkspur L4 | Atlas A180 |
|---|---|---|
| Permanent ID and index | `larkspur_l4`, 13 | `atlas_a180`, 14 |
| Role | Four-seat shoulder-wing piston tourer, including pilot | 180-passenger twin-underwing transport |
| Length and wing span | 8.80 m / 11.60 m | 42.60 m / 39.80 m |
| Propulsion | One 190 kW six-cylinder piston; three-blade propeller | Two 105 kN turbofans; separate moving fan parts |
| Structure | +3.8 / −1.52 g | +2.5 / −1.0 g |
| Career | PPL; purchase 46,000; rent 360 | ATP; purchase 1,600,000; rent 18,000 |

Values above are authored specifications, not real-world aircraft certification. The game intentionally compresses range to its island map. See [the flight-model validation report](NEW_AIRCRAFT_PERFORMANCE.md), its [raw performance run](validation/new-aircraft-performance.txt), [aero/trim run](validation/new-aircraft-aero.txt) and [same-condition fleet comparison](validation/new-aircraft-cruise-comparison.csv). **These focused results include Atlas's final 4.05 m flap-root geometry and the reviewed guidance correction. The final Release aggregate, Windows cross-build and scoped ASan/UBSan test run also pass.**

Atlas's final focused physics run measured **540 mph TAS / Mach 0.807 at 10,000 m and 75% power**. At its 74,385 kg maximum mass, takeoff ground roll was **1,308 m** and distance to 15 m was **1,526 m**. Fully flown constant-mass approaches stopped **1,216 m beyond CAP's threshold and 1,235 m beyond PVI's**, on their unchanged runways. The 59,942 kg mid-load stops were 1,043 m and 1,027 m, respectively. Its dispatch floor is 1,900 m. These are calm, dry, specified-load results; the shorter synthetic landing benchmark must not be substituted for complete approaches.

Larkspur's post-wing-height run measured **153 mph TAS at 1,500 m and 75% power**, between Wren and Swift. At its 1,501 kg maximum mass, ground roll was 260 m and takeoff distance to 15 m was 507 m; the fully flown CAP approach stopped 455 m beyond the threshold. Its dispatch floor is 600 m. These results include the raised wing for pilot-eye clearance and the correction that avoids charging its cantilever wing for nonexistent struts. Both aircraft pass the final-source independent performance, aero and wake runs and the final Release aggregate.

Both aircraft have their own `AircraftSpec`, `ModelDef`, generated cockpit layout, livery/detail paths and native mesh extraction. They use the existing materials, lighting, displays, control animation, audio, camera focus, wake, damage and flight interfaces. They are not enlarged scenery instances or replacements for existing aircraft.

- Larkspur: distinct shoulder-wing silhouette, fixed covered wheels, three-blade propeller, two front control sticks and a second seating row.
- Atlas: swept wings, underwing nacelles and rotating fans, transport flight deck with navigation/overhead fittings, twin nose wheels and four-wheel main bogies. Its gear wells, extraction bounds and inboard flap start match the authored geometry. Fan pose updates follow time, spool and individual engine health; stopped fans retain the ordinary cache behavior.
- New flight behavior is expressed through `fullCabinEnvelope`, `gearHeightM`, `takeoffFlap` and `gearTrackM`. The new types learn reference loading and maximum-weight runway performance with passengers and cargo included. Existing aircraft retain their established reference-loading defaults. Atlas main-gear/flap clearance is represented in both drawn geometry and aerodynamic strip placement.
- The bundled voice pack has no Larkspur/Atlas name recordings. Their research-combat SPLASH events retain full aircraft names on screen and speak the existing, accurate “Splash [count] down” fragments. All 13 existing recorded aircraft names stay unchanged. Speaking the two new names requires genuine new recordings and voice-index entries; none were fabricated.
- Existing `FLEET_ON`, `JET_ON`, `WRAITH_ON` and per-model specialization remain intact. New cabin/fan functions are excluded from other aircraft’s specialized sources. Rigid-part validation extends through Atlas fan part 46.

## Native autopilot validation and limits

The final 4.05 m flap-root source passes **34/34 native routes, 8/8 induced missed-approach recoveries, 23 gate/state checks and the entire flight_test suite with zero failures**. These dependency-consistent focused runs use the applied source hashes in [the manifest](validation/new-aircraft-source-sha256.txt), and the final rebuilt Release suite subsequently passed 73/73 tests. See [the native matrix](validation/new-aircraft-autoland.csv), [recovery matrix](validation/new-aircraft-go-around.csv), [flight log](validation/new-aircraft-flight.txt) and [performance report](NEW_AIRCRAFT_PERFORMANCE.md#native-autopilot-integration).

The round-two exact comfort rollout was a real containment failure: touchdown 12.51 m off centre with 6.40 m/s lateral velocity, then 28.23 m origin offset on CAP's 25 m half-width. The reviewed bounded correction adds capability-based lateral rejection, a healthy-engine/flap-functional missed-approach cleanup and explicit terminal-return state, consistent low-speed yaw washout, and anticipatory ground steering. It retains the runway bounds, brake/force model and legacy fleet behavior. All three modeled wheel-contact centres must now remain within pavement throughout rollout, including bounces; this is not a rendered tyre-edge guarantee. The added header boolean required rebuilding every consumer; those rebuilds are complete for final validation. Applied aircraft.cpp hash is `db56dcabc289c318c50b495d91b072b952e0797ab30d640c89ea1cebac9911c2`, header `469135a8858fbc43b889beacfa8d5f6bebb14b94a16e6a01331547d0f44b1b00`.

The exact comfort regression now peaks at **25.43°**, within its unchanged 26° limit, with **0.818–1.242 g** and maximum contact cross-track 3.46 m. Two wider-matrix comfort limits remain: CAP maximum initial load records **27.12° calm and 26.93° windy**; the prior mid-load calm exception is resolved. Maximum-load windy CAP touchdown is 2.25 m/s (443 ft/min), a firm landing. The specified 6 m/s wind is 25° off runway, only about 2.54 m/s steady crosswind. Ordinary/recovery comfort labels are not universal bank/g or strong-crosswind certification.

The eight-case left-engine-out diagnostic retains exactly the same three baseline failed flights: CAP mid/calm terrain, CAP maximum/calm trees, and PVI maximum/calm gear collapse. Five cases land. The new air-return policy is disabled for degraded engines/flaps; ground steering still applies, so successful rollout values can differ. Do not claim universal native engine-out autoland from the separate manual controllability result. [Final diagnosis, rejected variants and matched baseline comparisons](validation/new-aircraft-round2-guidance-review.txt) and [independent review](validation/new-aircraft-independent-guidance-review.txt) are preserved for the controller handoff.

Native cases burn game-compressed fuel, so maximum load means initial mass. The constant-mass approaches above separately validate maximum-weight landing capability. Neither result is all-weather approval.

## Roster and save compatibility

The aligned spec/model tables remain append-only. Original indices **0–12 are unchanged**, including Osprey 8 and the research identities Nightjar 9, Specter 10, Mantis 11 and Wraith 12.

- `kAircraftCount = 15` is the complete-table/render/cache extent.
- `kCareerAircraft = {0,1,2,3,4,5,6,7,8,13,14}` and `kNumAircraft = 11` describe the ordinary roster. Career count is never an aircraft-index bound.
- Career selection, purchase, finance, dispatch and save loading use explicit membership. Free Flight offers all 11 ordinary aircraft without license or ownership gates. The hangar places the two additions before four locked research teasers; research IDs and access stay unchanged even for research craft with `special == 0`.
- Saves continue using permanent string IDs for fleet, loan and job aircraft; numeric forced-aircraft references also keep their identities. No save-format migration is required to read existing v3.44 careers.
- Performance-cache rows map through the career roster. Old nine-row caches are rejected and relearned; they cannot be loaded into the research slots by mistake. Model prewarming, renderer limits and diagnostic loops cover all 15 types.

Compatibility is one-way for new content: **an older executable cannot understand a career containing `larkspur_l4` or `atlas_a180`**. Keep a pre-review save backup for rollback. Interactive Free Flight is the safest first gameplay review because its tested path does not purchase aircraft, settle jobs or change career saves. This does not make the older branch's command-line diagnostic modes read-only; those require the isolation described above.

## Shared wheel cover correction

Kestrel and Larkspur both use the existing covered-wheel gear type. The shared wheel-pants field now covers the central sidewalls and hub, leaves the contact tread exposed underneath, and does not add an exposed metal hub through the cover. The nose cover follows the same steering transform and remains inside its extraction bounds.

This intentionally affects both users of that geometry path. It does not change Kestrel flight tuning or wheel contact stations. Review front, side and steered-nose views on both aircraft. `wheel_cover_geometry_test.py` discovers all actual gear-type-0 models instead of hardcoding only these two.

## Gear and full airframe breakup

The requested gear-breakup behavior is shared across all 15 aircraft. Fixed gear and exposed retractable gear separate into three independent debris bodies: left main, right main and nose/tail gear. Retractable gear at or below the existing 0.2 door-opening stage remains with its containing structure; above that stage the complete assembly detaches, including partial-extension poses. Bay doors remain attached to the bay, rather than following the detached wheels and struts. Production debris color, depth and shadow paths use the same gear ownership.

The applied breakup batch passes focused tests for all 15 aircraft across gear states 0, 0.2, 0.21, 0.3, 0.65 and 1. It covers component ownership/inventory, tears, mass, energy and descent; the round-two underwing revision was followed by another focused breakup pass. Initial gear-only captures and later full-airframe captures are distinct evidence sets.

The expanded breakup review includes wings, tail, fuselage, engines, propellers and gear ownership. **Prop-debris and frozen-separation-pose corrections are now applied.** Detached prop blades follow their real spinner body, retain the captured angle with blur disabled and tumble with their debris body. Controls, wheel steering/rolling, fans and research actuators retain their separation state after later input changes. World placement remains live so moving wreckage keeps correct nearby shadows. Complete rigid-gear bounds now determine effective debris size within the existing impulse/angular-speed limits.

The batch also corrects Atlas player/traffic nose-wheel phase to its authored 0.4218 m radius and traffic packing to the actual 3.65 m gear height and 3.45 m main half-track. Legacy rolling radii and un-authored traffic defaults are unchanged. Focused evidence includes **100,677 prop checks**, **45 separation/input/moving-shadow cases**, **75 player/traffic wheel and packed-geometry cases**, **37 render contracts** and **8,074 covered-wheel checks**, all passing. The earlier 339,405-check Atlas gear result is superseded by the current 21,923,754-check underwing test below. Expected-failure controls demonstrate the Atlas wheel-parity failures before the fix and reject a candidate that incorrectly froze wreck world placement.

See the [focused review](validation/aircraft-breakup-review.md), [original batch runs and expected-failure controls](validation/aircraft-breakup-focused.txt), [original batch source hashes](validation/aircraft-breakup-source-sha256.txt) and [wheel-cover measurements](validation/aircraft-wheel-cover-geometry.json). The revised Atlas evidence and newer hashes are linked below. Focused actual-source CPU/contract results, final native captures and the passing final Release aggregate provide complementary evidence.

The full-airframe capture adapter invokes real `Game::breakUp`, spreads the actual debris bodies and records their inventory. Inspected post-fix front/rear/top views now show **12 Larkspur pieces and 13 Atlas pieces with exposed gear, versus 10 Atlas pieces stowed**. Larkspur's detached three-blade propeller is visible without a duplicate at its nose; Atlas has distinct nacelles and intact component cut faces. The round-two partial-gear breakup also shows connected trailing-link gear separated from the centre section without duplicate gear. The final leading-ramp capture and whole-fleet visual review have passed. Preserve these source-bound inventories and frames alongside the final aggregate results.

## Revised Atlas underwing bays and streamlined fairings

The requested compact bays and their fairings now remain within the real swept-wing planform and fixed flap panel. A two-axis trunnion folds the same deployed truck forward and inward; a connected vertical oleo and lower trailing link clear the closed bay without leaving an aft notch. Asymmetric doors retain the existing opening sequence. Contact stations, gear height, half-track and wheel radii stay unchanged. Visual and aerodynamic flap roots both move **3.85 → 4.05 m**, which is the round-two flight-model input change requiring the new physics validation above.

The current actual-source geometry test passes **21,923,754 checks**. Sampled clearances include **88.59 mm door-to-tyre**, **98.12 mm door-to-leg**, **51 mm closed wheel-envelope clearance** and **44.87 mm complete-assembly aft-wall clearance**. An independent 401-pose sweep with 163,200 primitive-surface samples per pose finds no fixed-airframe intrusion. Rejected direct-shaft and open-notch candidates remain negative controls, not shipped geometry. See [underwing runs](validation/atlas-underwing-gear-focused.txt) and [current source hashes](validation/atlas-underwing-gear-source-sha256.txt).

The applied final leading-only fairing adds a **1.75 m parabolic front ramp**, retaining the tested floor, cavity, aft lip, doorseat and mechanism. Primitive-based material ownership prevents distant nacelle/exhaust vertices from being repainted by smooth-union roundoff: **74,537 ownership checks pass**, and the old predicate misclassifies 16,380 distant samples. The additional ramp does not change flight-model inputs or breakup transforms. [Focused ramp and ownership evidence](validation/atlas-leading-fairing-focused.txt) records the repeated full geometry pass and dense sweep. The native 11-step bay sheet, 50% transition/breakup and **final nine-view ramp gate have passed**; the final ramp comparison was delivered at 21:02 UTC. Its later native measurements are in the sibling `new-aircraft-review/validation/atlas-ramp-final-native-measurements.json`: zero falsely wing-painted pod vertices, all four closed-door envelopes 95.13 mm ahead of the trailing edge, and an 18,986,904-byte mesh cache. The measured cache increase is 3.01% and submitted-triangle increase 3.56% versus the pre-underwing baseline. These are recorded geometry/cache costs, not frame-rate or native hardware approval.

## Fleet visual audit

The audit covers **all 15 aircraft**, including all four research types, across multiple exterior and cockpit angles, control extremes, throttle endpoints and applicable gear/fan details. The immutable initial set contains 274 native views. The **final sweep contains 283 accepted 1920×1080 images: all 147 exterior and 136 cockpit views**, rendered over 1,132 frames with zero GL errors. All 28 confirmed cockpit findings are resolved, and seven comparison sheets were delivered. See the [render completion record](validation/native-render-completion.json), [capture summary](validation/fleet-final-capture-summary.json), [evidence integrity check](validation/native-evidence-fleet-final-complete.json), [final exterior review](validation/fleet-exterior-final-review.json) and [final cockpit review](validation/fleet-cockpit-integrated-review.json). This is acceptance of the recorded production-geometry software-Mesa views, not native Windows hardware approval.

The capture path uses the **actual cached production geometry, materials and shaders** through a native review adapter: EGL/llvmpipe, Medium, render scale 1, 1920×1080, four rendered frames. Each set records binary/source/cache provenance. Native PNG recompression is lossless and checked against decoded-pixel hashes; contact sheets are review derivatives. Simplified inspection shaders, CPU field/ray probes and source-mirror diagnostics help locate defects but cannot replace production captures or establish final visual quality.

Preserve the initial snapshot and each later source-bound comparison set. Applied repairs and targeted recaptures address Atlas gear-door depth artifacts, cockpit control/housing clearance and trim, selected conventional cockpit edge/shading defects, Larkspur rear-window aperture work and Islander control-surface shading. The later request for compact underwing Atlas bays supersedes the earlier choice to retain its original fairing. The [first correction review](validation/fleet-cockpit-postfix-review.json) and [28-finding acceptance summary](validation/fleet-cockpit-round2-review.json) document the repair history. Minor continuous Bushmaster contact faceting, the existing Wren vent highlight and small Mantis display-bevel raster steps were independently reviewed and retained; they are not open confirmed defects. The final Atlas ramp is accepted. The working capture protocol and provenance remain in the sibling review workspace's `RENDER_HANDOFF.md` and `fleet-initial/`.

The subsequent **clean-ABI final-guidance comparison passes 3/3 fixtures**, matching decoded pixels, packed frame state, uploaded bounds and topology exactly: Atlas front, Atlas cockpit and Wraith weapons/belly. The final-guidance review binary is `5ed3edea36ad257b27ea9f4901ffef2cd55bb2845db6b504d9e269d8733f9ca3`; the 283-image sweep used the preserved pre-guidance visual binary `a408baffd161bb610d729b6b6de254e1efc70faf64c8a632ef9bd59830ff0b41`. [Exact comparison](validation/guidance-native-visual-parity.json), [final-guidance source hashes](validation/fleet-cockpit-guidance-source.json) and [complete consumer rebuild](validation/fleet-cockpit-guidance-rebuild.json) bridge those builds. [Frozen sweep sources](validation/fleet-cockpit-final-source.json) and [actual final cache winding](validation/fleet-cockpit-final-winding.json) remain separately recorded. This sampled exact comparison is not a second full-fleet recapture on the later binary.

The branch contains these reports and seven comparison PNGs, with a [manifest](validation/cockpit-comparisons/manifest.json), [pixel verification](validation/cockpit-comparisons/pixel-verification.json) and [comparison guide](validation/cockpit-comparisons/README.txt):

- [Windshield joins](validation/cockpit-comparisons/01_windshield_joins.png)
- [Cockpit contacts](validation/cockpit-comparisons/02_cockpit_contacts.png)
- [Seat webbing](validation/cockpit-comparisons/03_seat_webbing.png)
- [Atlas controls](validation/cockpit-comparisons/04_atlas_controls.png)
- [Atlas vents and bezel](validation/cockpit-comparisons/05_atlas_vents_bezel.png)
- [Larkspur window](validation/cockpit-comparisons/06_larkspur_window.png)
- [Specter details](validation/cockpit-comparisons/07_specter_details.png)

The complete 283 raw captures remain in the separate `new-aircraft-review/fleet-final/` workspace; they are not all included as PNGs in this branch. The comparison sheets are review derivatives, not replacements for those native originals.

## Common build interface with separate research modules

The XR-30/XR-40 refactor is **applied**. It uses one shared CPU build contract and dispatch interface while retaining separate conventional, Specter and Wraith builder modules, part plans and shader families. The actual legacy baseline was refreshed after every intentional visual/bay/toggle repair. The refactor preserves those repairs, geometry, materials, physics, game state, shader defines, stable part IDs, cache algorithm 24 and build lifecycle.

Live validation passes **7,329 exact legacy-versus-production comparisons**, **1,459 durable checks**, byte-identical sampled shader defines/assemblies, CPU/GLSL ABI/module ownership, specialization, all 37 gear/debris source contracts, 8,074 wheel-cover checks, and the current **21,923,754-check Atlas geometry test**. The durable test passes ASan/UBSan with leak detection disabled because ptrace prevents LSan operation. Mesh/hull/shader-check C++17 syntax checks pass. Three source-bound tests were redirected to the relocated fleet module without removing geometry assertions.

The eight refreshed **native 1080p XR-30/XR-40 fixtures match exactly** before and after: decoded pixels, packed frame state, uploaded bounds and topology, with zero RGB error. They cover Specter neutral/vectoring/cockpit and Wraith hover/weapons/bomb-absent/cloak/cockpit states. These use actual cached production meshes; they are not independent cold bakes of the whole fleet. The final Atlas-only shader edit was verified absent from all 22 checked pruned research programs. [Complete parity evidence and reproducible native comparisons](validation/aircraft-build-refactor/README.md) include every before/after PNG, CSV, provenance record and focused log.

The final integrated Release build and all 73 registered CTests pass, including the shader/research/gameplay coverage. Separate final-source flight tests pass the Atlas rollout correction, the whole-fleet visual review is accepted, and the broader ASan/UBSan run passes all 69 selected tests. The four sanitizer exclusions and native hardware limits remain explicit below.

## Manual XR40 full VTOL throttle

The requested manual throttle change is live in `src/game.cpp`/`src/game.h`, with `tests/vtol_throttle_test.cpp` registered in CMake. In the full 90° pod notch, hold throttle-up (default Shift, PgUp or +) for full power, or hold 1–9 for a proportional target. RT requests proportional power, LT reduces it, and throttle-down or 0 requests idle. Releasing power immediately commands idle; engine thrust decays at the existing spool rate. Held controls must be released after pause/input-context changes before power can rearm.

The focused test reports **154 checks, zero failures**, covering digital/preset/analog inputs, rebinding, releases, mode entry/exit, pause/context/disconnect safety, catch-up frames and autopilot ownership. Forward flight, intermediate pod notches and engaged autopilot retain their existing throttle behavior. This input-only change is recorded as a nonvisual delta against the frozen capture build. **Physical controller behavior and Windows interaction are unverified** and require hands-on testing; a simulated trigger-input test is not controller hardware approval.

## Validation status

Focused evidence collected during implementation:

- Final 4.05 m flap-root geometry: independent performance, aero and wake tests pass with the applied guidance source hashes linked above.
- Final-source native matrix 34/34, induced recoveries 8/8 and 23 state checks pass; full `flight_test` returns zero failures. The final rebuilt Release aggregate also passes. Two comfort-bank exceptions, a firm windy touchdown and three unchanged engine-out diagnostic failures remain disclosed.
- Applied breakup/prop/pose/traffic batch: whole-fleet breakup, 45 pose cases, 75 wheel/traffic cases and 37 render contracts passed. Current underwing Atlas geometry passes 21,923,754 checks and final fairing ownership passes 74,537 checks. Post-fix full-airframe, round-two bay, final ramp and final fleet captures are accepted; final Release aggregate tests pass.
- Manual XR-40 full-VTOL throttle: 154 focused checks and its final Release CTest pass; physical-controller behavior remains unverified.
- Common build interface: applied; exact CPU/assembly, durable/focused sanitizer and all eight native pixel/state/topology comparisons pass, followed by the final Release aggregate and scoped 69-test ASan/UBSan run.
- Aircraft voice resolution: all 15 identities passed, including exact decoded fallback fragments for the additions and unchanged named clips for the original 13. Missing required recordings still fail closed. The new regression fails against the pre-fix resolver.
- Traffic/actual-debris prop geometry and projection bounds: **100,677 checks passed**, covering the explicit nine-prop-aircraft roster, 12 hubs and 100,383 visible bound samples; Atlas and research jets stay excluded from the prop roster.
- Catalog identities, explicit ordering, research exclusion and performance-cache round trips: passed.
- Legacy-save loading, new fleet/loan/job identities, purchase/finance/dispatch and research/invalid-selection rejection: passed.
- Free Flight: **352 combinations** (11 aircraft × 16 airports × two start modes) passed, including full tanks, no fees and return flow. Restart, crash, loading cancellation, and active/recovery career-save isolation passed.
- All 15 specialized mesh programs linked; per-aircraft mesh/bake/march/shadow source-isolation checks passed. A separate shader sweep passed 336 assembled/pruned variants before the last integration edits.
- Source-bound cockpit/control/navigation contract: **211,681 checks, zero failures**, including the latest throttle-bank visibility correction. Covered-wheel/steering contract: **8,074 checks, zero failures**. These establish sampled geometry relationships, not complete raster or ergonomic approval.
- Final visual acceptance: **28/28 confirmed cockpit findings resolved**, seven comparison sheets delivered; all 147 exterior and 136 cockpit views accepted across all 15 aircraft. The final-guidance native comparison is exact for all three fixtures.
- Final-source Windows cross-build: **passed**, confirmed at 21:47 UTC; [build log](validation/build-windows-final.log) and [configuration log](validation/configure-windows-final.log). Native Windows launch, GPU/controller behavior and hardware performance remain unverified.

Validation checkpoints must be kept distinct:

- The **first correction batch passed 68/68 CTests in 244.64 seconds**, with a successful Release build and Windows cross-build. Logs in the sibling `new-aircraft-review/` workspace are `ctest-postfix.log`, `build-postfix.log` and `build-windows-postfix.log`. This supersedes the old 61/62 checkpoint, but predates round-two Atlas geometry and the now-applied refactor.
- A later **non-physics run passed 62/64**, recorded in `ctest-round3.log`. Its failures were a stale compiled `atlas_gear_geometry` executable and a brittle Bushmaster source assertion in `cockpit_layout_geometry`. The Atlas source test passed after rebuilding; the corrected Bushmaster assertion passed **3,080 focused checks**. Both are superseded by the final complete 73/73 pass, so neither remains an open test failure.
- The reviewed Atlas rollout correction passes final-source focused native/full-flight tests and the final rebuilt Release aggregate. The isolated physics blocker is closed, subject to the expressly documented comfort and engine-out limits.
- **Final Release:** build passed; **73/73 CTests passed in 289.81 seconds**, after rebuilding aircraft-header consumers. See the repository-local [test log](validation/ctest-final.log), [build log](validation/build-final.log) and [configuration log](validation/configure-final.log). The full Windows cross-build also passed, with its logs linked above.
- Upstream's new shared-object test build and sanitizer workflow are external integration changes, not additional test evidence for this local source.

**Final ASan/UBSan result: 69/69 selected tests passed in 1,515.20 seconds, exit code 0.** The complete build passed at 22:04 UTC; the test run used `-j2` and **full flight coverage without `FLIGHT_QUICK`**. See the repository-local [sanitizer test log](validation/ctest-sanitize-final.log), [build log](validation/build-sanitize-final.log) and [configuration log](validation/configure-sanitize-final.log).

The sanitizer exclusion pattern `shader_glsl|terrain_shadow_bake` matches **four tests**: `shader_glsl`, `shader_glsl_inline_wreck`, `shader_glsl_specialization` and `terrain_shadow_bake`. This follows the CI sanitizer scope; it is not a 73-test sanitizer pass. **All four passed in the complete 73/73 Release run.** The standalone refactor sanitizer result above remains separately recorded.

The final Release suite, full Windows cross-build, scoped ASan/UBSan run, 283-image native sweep, visual acceptance and clean-ABI guidance comparison are complete. **The proposal is ready for local review; no aggregate validation result remains pending.** Native Windows launch, actual GPU performance and physical-controller interaction remain unverified hardware limits, and the three unchanged engine-out diagnostic failures remain disclosed above. Preserve the final source/binary hashes and the specified test exclusions with the result. The user has approved pushing the review branch; merging or publishing a release still requires approval.

Useful review commands from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/new_aircraft_performance_test --report
build/new_aircraft_autoland_test
build/new_aircraft_autoland_test --go-around
build/vtol_throttle_test
python tools/generate_cockpit_layouts.py --check
python tests/new_aircraft_cockpit_contract_test.py
python tests/wheel_cover_geometry_test.py
python tests/gear_breakup_render_contract_test.py
```

On MSVC, configure with `-A x64`, build with `--config Release`, and test with `-C Release`. The executable is `build\Release\SolaceExpress.exe`. The supported MinGW cross-build uses `cmake-mingw.cmake`; see [the build instructions](../README.md#building).

## Installation and visual review

1. Back up `%APPDATA%\SolaceExpress` before testing. Use a separate Windows test profile with disposable copied user data, and verify its actual data path before launching diagnostic modes. Alternatively, integrate and verify the upstream in-memory diagnostic guard first. Extract the complete review package into a separate folder and keep its `materials`, `voice` and `loading` resources alongside the executable. An install-folder copy alone still shares the normal user-data location.
2. Launch on the intended Windows GPU using the verified isolated test profile. Allow the normal shader/airframe cache rebuild to finish. Keep that test profile's `%APPDATA%\SolaceExpress\startup.log` and any error log; do not delete real career data to resolve a cache problem.
3. Main menu → Free Flight: inspect and launch Larkspur and Atlas at Solace Capital, first from the runway and then airborne. Confirm the list contains 11 ordinary choices and excludes research. Check the career hangar separately for both purchasable additions and all four locked research teasers.
4. Inspect exteriors from front, rear, both sides and below; test parked/running engines, flap extremes, full control deflection, steering and gear cycling. On Atlas, watch both fan rotors at steady power, individual engine failure, the complete bogie fold and flap/gear clearance. Compare size beside Meridian/Kestrel without per-aircraft display scaling.
5. Inspect each cockpit forward, down, left, right and overhead, in daylight and at night. Move all controls; check live display visibility, navigation/pedestal clearance, focus zoom, supported seats and pedals, window edges and exterior transitions. Complete the same multi-angle audit across all 15 aircraft, including Kestrel wheel covers and the four research types.
6. Test fixed, partial, extended and stowed gear breakup, then full-airframe breakup. Inspect captured control/gear/fan/prop poses, both Atlas engines and all detached assemblies. A gear-only exploded screenshot does not satisfy this step.
7. On XR-40, test press-and-hold/release at the full VTOL notch using keyboard and an actual controller, including pause, context switching and disconnect/reconnect. Confirm normal spool-down and unchanged intermediate/forward/autopilot controls. The eight native refactor fixtures already match the repaired baseline exactly and integrated gameplay tests pass; retain their evidence during the separate hands-on Windows/controller review.

**Only after user-data isolation is verified, or the upstream diagnostic guard is integrated and verified**, the Windows tool accepts this reproducible screenshot example:

```bat
SolaceExpress.exe --shots gav_13_120_10_0,gav_14_120_10_0,ckv13_0_-8_12,ckv14_0_-8_12 --size 1920x1080
```

Images are written to `shots` beside the executable. The same isolation requirement applies to `diagnostics.bat quick` and every diagnostic mode listed above. In that protected test context, the batch can supply the existing benchmark/system report; add targeted new-aircraft comparisons because its default scene list does not exercise every new view. Do not interpret a successful diagnostic run as proof that the older branch could safely be run against real user data.

## Hardware limits and review boundary

Linux CPU tests, glslang checks and software-Mesa captures do not establish Windows driver behavior, VRAM use, loading time or frame rate. The cached production-geometry captures provide direct visual evidence for the recorded build; simplified diagnostic shaders do not. Final exterior/cockpit quality, control and fan animation, cold/warm GPU timings and controller interaction must still be checked on the intended hardware. No 60 FPS or performance-improvement claim is made.

Keep changes within the expanded review scope: the two native additions and their integration, compact underwing Atlas bays and streamlined fairings, shared covered wheels, all-fleet gear/full-airframe breakup, evidence-backed all-15 visual fixes, exact-preserving common build interfaces with separate XR-30/XR-40 modules, and manual XR-40 full-VTOL hold throttle. Preserve unrelated world/scenery content, research identities, shader-family separation and existing behavior outside the explicitly requested changes. Eventual integration must preserve the newer upstream world-wrap, terrain-coordinate correction, diagnostic isolation, build/test and audit work. The branch remains a local proposal based on v3.44.0. Local preparation and commits are in scope, and the user approved pushing `codex/two-new-aircraft-review-20261010` at 22:31:49 UTC. No merge or release publication is authorized.
