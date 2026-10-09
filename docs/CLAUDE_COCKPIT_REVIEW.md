# Cockpit review branch handoff

Final review base: `5660ef005a1a396a671a5f522bae59004d0c6055` (v3.37). The cockpit work was first prepared on `ce435bba8063507ff2d90983b7663a477717fa20`, then merged without conflicts with the newer standard-atmosphere, dynamic-weather and airline-UI changes. This is a review/implementation branch. The user explicitly chose to perform final rendering with Claude before merging. **No final latest-base cockpit screenshots exist.** Do not interpret CPU passes as visual approval, population ergonomics certification, or native GPU performance certification.

## Aircraft changes

| Aircraft | Prepared interior | Review limits |
|---|---|---|
| Kestrel T2 | Analog modules, lower supported seats, linked compact floor center sticks, mirrored side push-pull power controls, heel supports | Thin sampled stick-hand/leg 7.81 mm and support/leg 3.67 mm gaps. Inner stick arm can transiently cover shared status. |
| Wren 180 | Distinct analog panel, supported center sticks and side power controls, compact seats and heel supports | Both actual seat views at full/idle power cleared hardware/instrument/forward-view checks on the prior base. Repeat those native views here. Opposing inner-arm operation is outside the declared posture. |
| Bushmaster STOL | Raised analog panels, supported lower seats, dual compact floor sticks, central dogleg power levers with real journals | Local stick design proposal; the user's explicit stick choice concerned Kestrel/Wren. Thin sampled arm/shell 4.16 mm and forearm/thigh 6.61 mm margins. |
| Islander Twin | Modest eye/window adjustment, floor-supported full-size yokes, compact supported twin power bank | Normal both-seat panel scan passes; sampled operating limb gaps around 7 mm. Check actual supports and transient lower-status hand overlap. |
| Pelican Caravan | Raised eye/window front, floor-supported full-size yokes, centered shared status and backing, single-engine power lever | Bounded normal scans and control checks pass; new-base mesh and day/night readability remain unreviewed. |
| Meridian Q400 | Raised/aft supported full-size yokes with short pitch stroke, retained glass display layout | Wrist regrip reduces a rigid-proxy grip contact to near tangency; continuous wrist articulation remains unverified. |
| Starling 500 | Floor-supported yokes, aft pedals/heel boards, slim aft power bank, high shared status with backing/support | Fixed body/hardware and normal displays pass. Deterministic full-roll arm mapping has a 5.79 mm upper-arm/yoke witness; inspect the actual posture rather than assuming whole-body clearance. |
| Swift S6 | Inboard/lower supported seats, compact geared two-hand yokes, supported central push-pull row, preserved world pedal mounts and panel station | Smallest sampled inner-arm/body gap 1.48 mm. Shared power use requires a folded ready inboard arm; transitions are unswept. Extreme yoke operation partly obscures engine display; downward forward-view assessment is 4 degrees. |
| Osprey C6 | Lower eye/window correction, floor-supported yokes, compact twin power bank, joined crew/aft floor transition | Normal scans pass; sampled operating limb gaps around 8 mm. Verify floor/support extraction and transient hand overlap. |
| XR-10 Nightjar | Lower/inboard crew eyes, full-size floor-supported yokes, reachable raised power bank, heel supports and 4 mm pedal lift | Head margin 12.16 mm; pedal/shell 3.66 mm. Fixed-body/hardware checks pass. Deterministic arm mapping has 1.71 mm yoke and 1.27 mm shell witnesses; normal displays clear. |
| XR-30 Specter | Dedicated research panels, opaque readable attitude value band, beveled saddle underside and recessed sidewall lamps | Latest geometry and lights need native rendering. Soft bolster contact is distinct from hard-furniture contact. |
| XR-20 Mantis | Retained accepted single-seat layout, heel-supported pilot/pedals, live single-engine instrumentation | Preserve latest upstream single-engine, gear and research-mode behavior; final native special-mode review remains. |
| XR-40 Wraith | Dedicated faceted interior/panel layout, corrected attitude value band, retained controls and special modes | Final display, cloak/bomb/pod/feed-state rendering remains. |

The conventional layouts remain nine bounded `vec4` records per aircraft, generated from `assets/cockpits/layouts.json`. Compact controls retain normalized full flight inputs and existing bindings. Fixed mixture controls retain their existing static semantics; no new mixture binding is implied. Representative rigs use connected limbs and declared operating postures, not smaller humans substituted to clear a failed test.

## Preservation and related changes

Twenty-two precise checks confirm upstream exterior body/gear/well geometry, gear packing through `gearStations`, aerodynamic moving-surface poses, material gear frames and window early-rejection helpers remain intact. Model definitions differ only in nine intentional cabin-eye tuples and three windshield fronts. No rejected roof-expansion experiment is integrated. Dedicated XR geometry and latest NVIDIA compile/retry/fallback paths are retained.

Continuous cockpit focus zoom uses actual layout target bounds. Live engine health and the formerly swallowed gear-annunciator assignment are corrected. Related state/voice work preserves upstream flight physics and autopilot behavior, fixes accepted fuel-plan persistence and survey-history migration, and uses binding-neutral lesson audio.

**Save compatibility:** this branch writes format 4 and reads formats 1–4. Released older executables cannot read its format-4 saves. Back up careers before trying it. The earlier unpublished review prototype used a different format-4 survey record; those prototype saves are not claimed compatible.

## Validation and renderer boundary

**Final merged result (v3.37):** the full native Release build succeeds and all 21 CPU CTests pass (112.50 seconds), including cockpit camera, flight model, progression, saves, hull/mesh, world cache, full gameplay and both cockpit layout families. The EGL `terrain_shadow_bake` test was deliberately excluded under the user’s no-further-rendering instruction. CMake includes the new upstream weather implementation. The 22 precise gear/shell preservation checks, generated-data parity and `git diff --check` also pass.

The detailed geometry, GPU comparison and historical test counts below were measured before the final v3.37 merge. The newer upstream commit does not change the cabin model definitions or interior shader geometry. Final merged build and CPU-suite results are recorded above.

- Final combined application/render harness builds successfully. Six focused CTests pass: focus zoom, shader keywords, research layout, conventional layout, generated layout parity and conservative layout geometry.
- Swift's 12 bounded CPU criteria pass; compiled records show its final integration changes only aircraft 7. Utility/glass checks retain their explicit operating-arm qualifications above.
- Focus zoom: 14,171 pure checks and 205 actual-Game checks passed. State regressions, save tests, full gameplay tests and voice regression passed; 1,068 indexed recordings decoded.
- Renderer tests: 9,637 exhaust checks, 100,493 prop checks, mesh integrity including sanitizer runs, and 26 model/view enumerations passed.
- The exact two-program baker reuses previously computed normals without changing samples, normal arithmetic or AO. Production shader comparison passed 49,920 components with maximum numerical error zero on the utility/glass field. Its dispatch passed 11,657 assertions, 87 mock draws and 7 adversarial yields with ASan/UBSan. Both final Swift-inclusive bake programs cold-link with GL errors zero.
- Final image capture is blocked in this software-Mesa VM by the separate full aircraft/wreck/UFO G-buffer shader: integrated cold/warm and empty-context attempts were killed around 7.55–7.57 million KiB RSS. This is not a successful image test. No unverified normal-loop experiment is part of the intended handoff.

Useful local checks: configure/build normally, run `ctest --output-on-failure`, run `python tools/generate_cockpit_layouts.py --check`, and run `python tests/conventional_cockpit_layout_test.py`. Final review should render both seats where present, idle/full power, extreme controls, day/night, forward views and supported floor/seat/pedal details. Check research states and latest gear before deciding to merge. No 60 FPS claim is made from this software renderer.
