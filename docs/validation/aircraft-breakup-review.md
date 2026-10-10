# Aircraft integration and breakup review

**Checkpoint:** 2026-10-10 20:54 UTC. The breakup/prop/pose/traffic batch, under-wing Atlas bay integration and final leading-fairing refinement are applied and frozen. The round-two native bay and partial-breakup captures have been inspected; the final leading-ramp render and aggregate validation are separate gates.

## Scope and resolved findings

- Fixed and exposed landing gear use three explicit rigid debris owners: left main, right main, and nose/tail assembly. The shared fixed-main shape is extracted once, so the fuselage does not retain duplicate legs or wheel covers. Colour, depth and shadow paths use the same owner mapping. Bay doors stay with their bays.
- Retractable gear at or below the existing 0.2 door-opening stage retains its containing-airframe attachment. Above that stage, complete assemblies detach, including partial-extension poses. This follows the requested distinction between stowed and exposed gear.
- Larkspur uses the existing component system with 12 pieces: propeller, three gear assemblies, two wings, two tailplanes, fin, nose, aft fuselage and centre section. Atlas has 13 with exposed gear and 10 stowed: two nacelles replace the single propeller. The Atlas fan and aft core cone belong to their nacelle bodies; the nacelle bound includes the complete cone tip.
- Detached prop blades now follow their actual spinner body through the existing bounded, depth-tested prop renderer. They retain the captured separation angle with blur disabled, then tumble with their debris body. The intact aircraft keeps its existing prop rendering.
- Detached control surfaces, wheel steering, rolling phase, fan phase and research actuators retain their separation state after keyboard/pad or actuator changes. The followed centre section's current world position and rotation remain live, preserving nearby-wreck shadow selection. Normal flight resumes live visual state when no wreck exists.
- Complete rigid gear bounds now set their effective debris size rather than inheriting the 0.6 m point-mass minimum. This is limited to rigid-only gear and feeds the existing impulse and angular-speed limits.
- Atlas player and traffic nose-wheel phase now share its authored 0.4218 m radius (0.74 × the 0.57 m main). Traffic also respects its 3.65 m gear height and actual 3.45 m half-track/contact stations. Previously it packed 3.415 m height and sampled a 5.174 m half-track. Every legacy rolling radius and un-authored traffic default is unchanged; Larkspur packed geometry already matched the player.

The two appended aircraft remain ordinary AircraftSpec/ModelDef entries with shared cockpit definitions, flight physics, animation, mesh baking, rendering and breakup. Existing IDs 0–12 remain intact; career membership is explicitly 0–8, 13, 14, with research identities 9–12. No separate new-aircraft prototype path is shipped.

## Focused evidence

| Check | Result |
|---|---|
| Whole-fleet breakup ownership, component inventory, tears, mass, energy and descent | Pass, all 15 types; gear states 0, 0.2, 0.21, 0.3, 0.65, 1 |
| Prop geometry, actual debris owners and conservative draw bounds | 100,677 checks pass; 9 prop types and 12 hubs |
| Atlas revised stow, door sweep, fixed material, skin and extraction geometry | 21,923,754 checks pass |
| Separation state, keyboard/pad, actuator mutation, moving shadow centre and live-flight recovery | 45 cases pass |
| Actual player/traffic wheel phase and all 96 packed geometry values | 75 cases pass across all 15 types and signed travel |
| Production render-source contracts | 37 checks pass |
| Closed fixed-wheel covers, exposed tread and nose steering | 8,074 checks pass for Kestrel and Larkspur |
| Changed prop renderer translation unit | Compiles |
| Whitespace/error check | `git diff --check` passes |

Atlas's revised source geometry retains the 4.17334 m main station, 3.45 m half-track and 3.65 m gear height. Sampled clearance is 88.59 mm from doors to tyres and 98.12 mm from doors to the trailing-link leg, with 196.19 mm fully deployed door-to-wheel-plane clearance. The closed wheel envelope has at least 51 mm clearance; the complete stowed assembly has 44.87 mm aft-wall clearance. These are CPU/source measurements, not a substitute for native render inspection or settled-aircraft physics.

## Revised under-wing Atlas bays

The swept rectangular cavity and final blended fairing remain inside the real wing planform and fixed flap panel. A two-axis trunnion at `(track, -1.30, 2.72)` moves the same deployed truck forward and inward to approximately `(1.85, -1.30, 1.07057)`. Asymmetric side doors keep clear throughout the existing deployment sequence. The actual visual and aerodynamic flap roots both move from 3.85 to 4.05 m; this is the only round-two flight-model input change. Player/traffic wheel radii and contact dimensions do not change.

A direct shaft initially intersected the aft fairing. A static notch cleared it but left an uncovered 92.5 mm aft strip when stowed, so neither that notch nor its longer-bay alternative was shipped. The final connected vertical oleo and lower trailing link exit through the closed bay's floor without an aft opening. Its knee journal, forward-facing scissors and complete rig remain one existing PT33 part. Debris bounds include the posed knee, and the approximate mass centre includes a knee share. The PT33 extraction bound was also corrected to include the new forward hinge.

An independent 401-pose sweep with 163,200 primitive-surface samples per pose found no fixed-airframe intrusion, conservatively enclosing the rounded knee journal with a sharp cylinder. Wheel skin clearance leaves approximately 78 mm beyond the retained 30 mm upper skin. The source-bound C++ test independently checks wheel/door and full-body intersections, CPU/shader pose agreement, extraction bounds, the original penetrating-shaft witness, and the closed aft lip. Its minimum positive SDF value is an estimator at the well-opening transition, not a measured physical clearance.

The subsequent leading-only fairing refinement adds a 1.75 m parabolic front ramp while leaving the floor, cavity, aft lip, doorseat and complete mechanism unchanged. An independent 401-pose sweep again found no fixed-material intrusion; the closed-floor field is unchanged to floating-point noise. A conservative bound leaves 87.23 mm between all newly added fairing material and the nacelle cylinder. Primitive-based material ownership also avoids repainting distant nacelle/exhaust vertices due to smooth-union roundoff. These changes do not alter flight-model inputs or breakup poses. Final ramp evidence is in [atlas-leading-fairing-focused.txt](atlas-leading-fairing-focused.txt).

Round-two raw results and source hashes are in [atlas-underwing-gear-focused.txt](atlas-underwing-gear-focused.txt) and [atlas-underwing-gear-source-sha256.txt](atlas-underwing-gear-source-sha256.txt).

The wheel-parity regression fails exactly the five Atlas cases on the pre-fix production objects; the other 70 pass. Independent review also caught a candidate-only frozen-world-position regression before it was applied: the new moved-wreck test fails all 45 cases on that candidate and passes after retaining live world placement. No remaining correctness finding was identified in the scoped prop/pose/size/radius/traffic review.

## Reproduction and artifacts

Build the repository normally, then run the CTest cases `breakup`, `traffic_prop_discs`, `atlas_gear_geometry`, `wheel_cover_geometry`, `gear_breakup_render_contract` and `wreck_pose` (see CMakeLists.txt for registered names). The pose route is also directly available as:

```
SOLACE_WRECK_POSE_ONLY=1 gameplay_test
```

It runs both the 45 separation-state and 75 wheel-parity cases. The ordinary gameplay regression route also includes them.

- [Raw focused runs and expected-failure controls](aircraft-breakup-focused.txt)
- [Tested source hashes](aircraft-breakup-source-sha256.txt)
- [Wheel-cover measurements and source hashes](aircraft-wheel-cover-geometry.json)

## Remaining validation limits

The initial production cached-render sweep contains all 15 aircraft and 274 native 1920×1080 views. Earlier real-breakup views already show three exposed gear bodies without fuselage duplicates, and stowed Atlas gear retaining its airframe attachment. The first post-fix full-airframe front/rear/top views and inventories now show Larkspur's 12 pieces and Atlas's 13 exposed/10 stowed pieces. Native front/rear views show a detached three-blade Larkspur propeller without a duplicate at the nose, separated gear, distinct Atlas nacelles and closed component cut faces. These are under `new-aircraft-review/postrepair-gates/13_larkspur_l4` and `14_atlas_a180`; the round-two 11-step bay sheet and native 50% bay/partial-gear breakup were subsequently inspected under `new-aircraft-review/round2-final/14_atlas_a180`. The trailing-link knee/journal is connected, the doors clear through the shown transition, and the centre section retains no duplicate gear. The final leading-ramp recapture remains pending at this checkpoint. The rendering environment is EGL/Linux, not native Windows or an RTX 3070 performance validation.

The latest separate native-autoland report covers 34 specific cases, including 6 m/s wind 25° off the runway (about 2.54 m/s crosswind). It reports one go-around and three additional comfort-bank peaks above 26°. It does not establish a full professional/comfort or adverse-weather envelope; see [the performance report](../NEW_AIRCRAFT_PERFORMANCE.md).
