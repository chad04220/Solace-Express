# Osprey C6 — coastal charter cabin

**Optional bespoke interior integration module.** This is an extension to the two aircraft rows, not a claim that the vanilla Tier-A row schema supports unique cabin assets. Neither aircraft eye nor fuselage dimensions changes. Exterior wingroot Z remains -0.80 m.

Ivory composite door panels, copper anodized accents, tobacco leather cushions and cocoa textile pockets/runner give this twin a warm, practical charter identity. The existing analog twin dashboard, duplicated flight instruments, yokes, pedals and twin-engine levers remain the baseline generator. Four new passenger seats complete its six-place visual arrangement. Seat construction follows the baseline bolstered/reclined-seat vocabulary rather than borrowing the research aircraft's glass cockpit.

## Source and integration

`mapOspreyCabinTrim.glsl` is the complete static SDF source. Units are metres; X starboard, Y up, Z aft. Compile after the existing SDF primitives and `sdFuselage` definitions and before `mapPlaneBody`. Inside the cockpit-only branch, union `mapOspreyCabinTrim(p)` with `res` **only for Osprey**. Resolve the aircraft by the actual `AircraftSpec.id == "osprey_c6"` in the caller and pass a dedicated integration flag, or dispatch from an existing renderer model-identity mechanism if available. That flag/dispatch is new integration plumbing, not a claimed existing row field. Do not infer aircraft identity from dimensions, seat count, cockpit type, or a fixed roster index. Scope this to the own-aircraft cockpit; do not accidentally apply it to traffic.

Preserve all baseline code that draws panel ID10 and radio ID65, and preserve their display-atlas sampling and control animation. Add material handling for IDs120–123 in the interior material path, before broad fallback conditions. `ospreyCabinAlbedo` supplies linear albedo; use roughness .8 for ivory, .35 and metal .75 for copper, .9 for leather, 1.0 for textile. IDs124–125 are reserved but unused. No collision with Mantis110–119 or baseline10–69.

For the complete preview palette, apply Osprey-only albedo overrides to existing IDs11 (shell .58,.54,.44),12 (pilot seats .30,.115,.052),63 (trim .72,.66,.53),69 (harness .062,.040,.028), while retaining material geometry, normals, other properties and all display IDs. These are aesthetic overrides, not new schema fields. Leave glareshield14 black to prevent glare.

## Static bake

Bake the module independently from the moving assembly; retain material IDs. `static_parts.csv` is a semantic part table and the GLSL is authoritative for transforms, repetition, cuts and hull clipping. The minimum non-clipped authored solid thickness is 8 mm. Boundary clipping and rounded seams may naturally taper below 8 mm. At 256 samples over the whole 9.8 m aircraft, 8 mm trim cannot survive: use an interior-local high-resolution bake or separate meshes, not a coarse exterior voxel grid. Seat belts here are static decorative lap straps. Do not bake existing yokes, pedals, throttle/flap levers into these objects.

Every custom solid is intersected with `sdFuselage(p) <= -.090`, keeping it inside the outer-body distance field. This is a geometric containment guard, not a guarantee of uniform Euclidean wall thickness for a non-exact SDF. The floor and supports meet intentionally. No structural, ergonomic-certification, collision-hull or mass-model claims are made.

## Previews and verification

Five requested pilot-eye views use actual module geometry plus the production aircraft SDF: cockpit, left, right, aft and down. Eye is (-.34,.74,-1.90). The gauge faces use the **real production `drawInstruments` routine and font atlas**, with fixed illustrative values (112 IAS,3450 altitude,247 heading,650 vertical speed;4 pitch,14 bank,.82 engine,.62 fuel). They are not a live simulation capture. Lighting is inspection lighting; there is no rendered world or production display-atlas caching/postprocessing.

`verification/aircraft_visual_test.cpp` compiles the real GLSL through Mesa/EGL. Its numerical test samples the custom volume against unchanged baseline yoke/pedal distance fields over 27 pitch/roll/yaw combinations. This is sampled QA, not an exhaustive proof. Containment additionally follows directly from the clip operation. Pilot horizontal sight line remains clear: all added forward dashboard trim stays below y=.60, below eye y=.74; passenger geometry is aft of the pilot. The new module does not replace instrument faces.

See verification logs for actual results. Windows/full-game rendering, rasterizer mesh conversion, animation attachments and production material-dispatch wiring remain integration checks. The isolated test copy unconditionally inserts this hook into conventional cockpit geometry because it renders Osprey only; **do not copy that unconditional fixture change into the game**.

### Recorded checks

- 2,211,840 GLSL volume samples across 27 pitch/roll/yaw poses; 145,368 occupied custom-volume samples.
- Zero sampled yoke/pedal intersections; minimum sampled gap 0.149767 m.
- Zero sampled containment failures; outer-body clip additionally enforced in source.
- Source-envelope passenger row gap 0.359833 m; baseline front-seat to first-passenger row gap 0.761573 m.
- Existing endcap seam probe maximum 0.000202 m. The baseline headrest probe's `1.0000 m` result is a **sentinel for suppressed pilot headrests**, not a measured clearance. New passenger headrests are covered by the custom containment probe.
