# Aircraft interior architecture and review

This branch prepares the cockpit redesign on upstream `ce435bba8063507ff2d90983b7663a477717fa20` (v3.36.3 follow-up). It preserves the newer landing-gear mechanism, aircraft outer model definitions, NVIDIA-safe gear compilation and reduced shader paths. Integration and per-aircraft verification are still being completed; this document is an architecture guide, not a claim that every occupant-space check has passed.

## Authored data

`assets/cockpits/layouts.json` owns the ten conventional aircraft layouts. Each record is exactly nine vec4: pilot, copilot, engine/system module, shared status, structural dimensions, control mounts, pedestal dimensions, trim and cloth. Run `python3 tools/generate_cockpit_layouts.py` after editing it. The generated CPU table and GLSL accessor must agree; `--check` detects drift. Do not introduce an uncoordinated tenth record or reuse packed exterior-model/gear slots.

`research_cockpit_layout.glsl` owns Specter's five-panel arc and Wraith's three-panel bridge, including physical centers, normals, bounds and corner shapes. The CPU focus adapter consumes these same functions. Their external-view screens, window/feed transforms, bomb pane and state-driven functions remain separate.

Mantis retains its dedicated centerline cockpit and the latest upstream outer model. Its supported seat and closed local pedal recess are interior changes. Aircraft0/1/2/3/4/5/6/8/9 have coordinated crew-station or control updates; moving an eye alone is not a fit solution.

## Fit and live controls

`modelCabinFit` computes six CPU-authored values for pedal and seat support locations. `uCabinFootFit`/`uCabinSeatFit` carry them without changing the exterior24-vec4 model ABI. Keep CPU and shader pan caps synchronized. Bushmaster's pedal center remains exactly.252m after its seats move inward; deriving it again from eyeX would silently invalidate the tested foot positions.

The existing flight-input response and rigid-part IDs are retained:

- Kestrel and Wren: paired compact center sticks; per-seat linked push-pull throttles. Both throttle instances use the same input. Their authored mixture controls retain the existing static behavior.
- Bushmaster: a local proposed pair of compact floor sticks and central journaled power levers, reusing existing yoke/knob IDs. It is a different mechanical layout, not a trainer-helper alias.
- Pelican and Meridian: full-size yokes on floor-supported open bearings, with45/50mm rendered pitch stroke respectively. Full normalized input still gives full flight-command response.
- Islander and Osprey: supported short-throw yokes and narrow aft twin-power banks, with real travel slots, journals, axles and a separately positioned flap lever.
- Starling and Nightjar: supported short-throw full-size yokes, closed heel rests and reachable shortened power consoles. Starling retains both primary displays and moves its shared status face to upper centre. Their articulated-arm diagnostics remain qualified pending visual review.
- Swift retains its current family until its bounded compact-yoke fit revision passes; rejected pose experiments are not integrated.

A moving shaft needs a real aperture in its static bearing. Static supports must reach a real floor or structure and be inside extraction bounds. Do not hide unsupported or clipping solids behind a too-small near-field guard. The authored human probes use ordinary fixed limb lengths and connected arms/legs; shrinking the person is not a clearance repair.

## Displays and materials

`cockpit_fittings.glsl` builds distinct housings/supports while protecting live faces. `cockpit_material.glsl` maps those faces into the existing atlas with independent X/Y gradients. Exact per-pixel face classification prevents mesh triangles from spreading display material beyond its intended face. Upstream generic cluster-frame correction remains available outside the authored fleet/Mantis layouts.

Readouts use live heading, flap, gear and fuel state; engine pages receive actual engine count/type and per-engine health. Static decorative controls must not be described as new simulated systems. Material emission is restrained around the flight scan; cockpit styling must not change exterior fixture ownership or engine-failure glow.

## Required verification

Build/run the repository checks, including `cockpit_layout`, `research_cockpit_layout`, `cockpit_layout_generated`, `cockpit_layout_geometry`, `cockpit_focus_zoom` and the real Game camera test. Keep the latest shader reserved-word, declaration and matrix-division guards. The geometry contract's45mm face/outer-skin bound remains active. Bushmaster's intentionally raised panel uses source-grounded both-eye0–3degree forward rays in place of the generic eye-drop proxy; it does not simply waive visibility testing.

Those tests do not prove whole-cockpit ergonomics. Each changed craft also requires a bounded, connected representative crew check: head/seat/support, pedal reach and travel, full primary-control travel, actual hands and operating arms, power-control reach, hard-solid clipping, normal instrument scan and useful forward view. Document intentional padded/held-control contact and reasonable transient hand occlusion separately. Record actual residuals rather than labeling a sampled test as continuous or population certification.

Native game captures must show both normal seats where applicable, daylight/night legibility, meaningful control extrema, and detail views of new supports. Empty-cabin images establish rendered geometry/legibility, not body fit. A diagnostic occupant overlay must retain real scene depth, exact checked dimensions and a paired plain reference; never use an x-ray or cutaway to conceal cockpit-solid collisions.

## Preservation and publication

Review shared-file hunks against the pinned upstream head. Never transplant whole older `models.cpp`, `plane_parts.glsl`, `plane_sdf.glsl`, gear-material or renderer files: that would discard newer gear and driver fixes. In particular, preserve `gearStations` packing, `gearNosePose` vector division, new gear/well/floor paths, Mantis external definitions, shader retry/rejection markers and reduced variants.

The requested destination is a separate review branch, with the default branch unchanged. Publish only after the prepared work and its stated checks meet the agreed quality bar. Per-craft evidence and any remaining limitations belong in the final handoff; a successful build alone is not final cockpit approval.
