# Aircraft interiors

How the cockpits are authored and fitted: the layout records, the crew-station fit and the live controls.

## Authored data

`assets/cockpits/layouts.json` owns the ten conventional aircraft layouts. Each record is exactly nine vec4: pilot, copilot, engine/system module, shared status, structural dimensions, control mounts, pedestal dimensions, trim and cloth. Run `python3 tools/generate_cockpit_layouts.py` after editing it. The generated CPU table and GLSL accessor must agree; `--check` detects drift. Do not introduce an uncoordinated tenth record or reuse packed exterior-model/gear slots.

`research_cockpit_layout.glsl` owns Nocturne's five-panel arc and Somnus's three-panel bridge, including physical centers, normals, bounds and corner shapes. The CPU focus adapter consumes these same functions. Their external-view screens, window/feed transforms, bomb pane and state-driven functions remain separate.

Reverie retains its dedicated centerline cockpit and the latest upstream outer model. Its supported seat and closed local pedal recess are interior changes. Aircraft0/1/2/3/4/5/6/8/9 have coordinated crew-station or control updates; moving an eye alone is not a fit solution.

## Fit and live controls

`modelCabinFit` computes six CPU-authored values for pedal and seat support locations. `uCabinFootFit`/`uCabinSeatFit` carry them without changing the exterior24-vec4 model ABI. Keep CPU and shader pan caps synchronized. Scrubrunner's pedal center remains exactly.252m after its seats move inward; deriving it again from eyeX would silently invalidate the tested foot positions.

The existing flight-input response and rigid-part IDs are retained:

- Dawnlark and Ambler: paired compact center sticks; per-seat linked push-pull throttles. Both throttle instances use the same input. Their authored mixture controls retain the existing static behavior.
- Scrubrunner: a local proposed pair of compact floor sticks and central journaled power levers, reusing existing yoke/knob IDs. It is a different mechanical layout, not a trainer-helper alias.
- Longshore and Archipel: full-size yokes on floor-supported open bearings, with45/50mm rendered pitch stroke respectively. Full normalized input still gives full flight-command response.
- Skerry and Wavecrest: supported short-throw yokes and narrow aft twin-power banks, with real travel slots, journals, axles and a separately positioned flap lever.
- Emissary and Vigil: supported short-throw full-size yokes, closed heel rests and reachable shortened power consoles. Emissary retains both primary displays and moves its shared status face to upper centre. Their articulated-arm diagnostics remain qualified pending visual review.
- Cutwater retains its current family until its bounded compact-yoke fit revision passes; rejected pose experiments are not integrated.

A moving shaft needs a real aperture in its static bearing. Static supports must reach a real floor or structure and be inside extraction bounds. Do not hide unsupported or clipping solids behind a too-small near-field guard. The authored human probes use ordinary fixed limb lengths and connected arms/legs; shrinking the person is not a clearance repair.

## Displays and materials

`cockpit_fittings.glsl` builds distinct housings/supports while protecting live faces. `cockpit_material.glsl` maps those faces into the existing atlas with independent X/Y gradients. Exact per-pixel face classification prevents mesh triangles from spreading display material beyond its intended face. Upstream generic cluster-frame correction remains available outside the authored fleet/Reverie layouts.

Readouts use live heading, flap, gear and fuel state; engine pages receive actual engine count/type and per-engine health. Static decorative controls must not be described as new simulated systems. Material emission is restrained around the flight scan; cockpit styling must not change exterior fixture ownership or engine-failure glow.

## Required verification

Build/run the repository checks, including `cockpit_layout`, `research_cockpit_layout`, `cockpit_layout_generated`, `cockpit_layout_geometry`, `cockpit_focus_zoom` and the real Game camera test. Keep the latest shader reserved-word, declaration and matrix-division guards. The geometry contract's45mm face/outer-skin bound remains active. Scrubrunner's intentionally raised panel uses source-grounded both-eye0–3degree forward rays in place of the generic eye-drop proxy; it does not simply waive visibility testing.

Those tests do not prove whole-cockpit ergonomics. Each changed craft also requires a bounded, connected representative crew check: head/seat/support, pedal reach and travel, full primary-control travel, actual hands and operating arms, power-control reach, hard-solid clipping, normal instrument scan and useful forward view. Document intentional padded/held-control contact and reasonable transient hand occlusion separately. Record actual residuals rather than labeling a sampled test as continuous or population certification.

Native game captures must show both normal seats where applicable, daylight/night legibility, meaningful control extrema, and detail views of new supports. Empty-cabin images establish rendered geometry/legibility, not body fit. A diagnostic occupant overlay must retain real scene depth, exact checked dimensions and a paired plain reference; never use an x-ray or cutaway to conceal cockpit-solid collisions.

## Shared files

Edit shared-file hunks, never whole files: transplanting an older `models.cpp`, `plane_parts.glsl`, `plane_sdf.glsl`,
gear-material or renderer file discards newer gear and driver fixes. In particular, preserve `gearStations` packing,
`gearNosePose` vector division, the gear, well and floor paths, Reverie's external definitions, the shader retry and
rejection markers and the reduced variants.
