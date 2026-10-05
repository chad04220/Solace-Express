# Revision 2 validation

## Osprey exterior
- Both matched wing-root fields moved from -1.35 m to -0.80 m.
- Full gear-bay-to-flap clearance now 69.66 mm.
- 23 analytical checks, rebuilt 10/10 CTest suite and maximum-load supplement pass.
- 1,769,472 exact-GLSL mirror pairs: zero unexpected differences. The existing single left pitot is explicitly reported as an intentional exception.
- Matched orthographic views and silhouettes supplied.

## Osprey bespoke interior
- Static ivory/copper/tobacco cabin supplement; retains baseline gauges and animated controls.
- 2,211,840 sampled points across 27 control poses; zero sampled control intersections or containment failures.
- Minimum sampled control clearance 149.767 mm.
- Five inspected views use the actual baseline instrument shader at fixed preview values.

## Mantis exterior
- Matched control-surface volumes and rounded hinge pockets replace incomplete inserts.
- Corrected shutter reflection, cover closure and moving missile-carrier support.
- 640,000 mirror pairs include reflected controls and swapped unequal stores.
- 263,718 control-motion probes across nine states per control: no sampled fixed-airframe penetration.
- 78,000 closed bay/gear skin probes: no sampled holes.
- Carrier supports checked at 21 deployment positions per side.
- Flight spec unchanged; original nominal and maximum-weight conventional flight checks remain applicable.

## Mantis bespoke interior
- 115 static components with matching camera-pane metadata.
- Conservative convex-cavity containment checks report at least 10.918 mm clearance.
- Sampled retained-control sweep clearance: pitch 12 mm, throttle 14 mm, pedals 12 mm, excluding documented intended bearing/slider contacts.
- Static camera/reference markings are clearly labeled. Live feeds and avionics are not implemented in this authoring pack.

## Limits

These are local software-GL and source/spec checks. They do not prove every possible continuous pose or final baked mesh. The Osprey custom interior needs its documented hook; Mantis needs the future renderer, research registration, feeds and weapon gameplay integration. No Windows/GPU benchmark or finished game build is claimed. Source baseline and all current tests, detailed results and reproducible fixtures are documented in each aircraft folder. The production game source is unchanged by this proposal. Historical game-test results refer to a298771; see README.md for publication verification.
