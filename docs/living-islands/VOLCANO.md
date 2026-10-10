# Mount Kaleo: active summit

The existing summit at (25000, -9000) now contains a physical crater, rather than a molten material painted over the original rounded mountain. World generation changes only 558 heightmap texels inside 520 m of the summit. The floor is approximately 1780 m; irregular walls rise roughly 230 m over a 145–340 m radial transition, blending back into the original mountain by 520 m. Detail amplitude is reduced on the floor. Bilinear influence stays inside the independent 600 m audit boundary. Airport grounds and approach influence are explicitly rejected.

All other terrain samples, runway geometry, runway surfaces and navigation fixtures remain protected by the frozen baseline test. The world-cache magic is WLD3 so an older cached summit cannot survive the change. The normal terrain envelope and height bounds are rebuilt from the modified heightmap; visual and collision heights therefore share the same source.

## Effects

- Floor-aware molten fissures and a smaller hot vent, surrounded by cooled basalt and irregular rubble/snow transitions.
- Deterministic wind-driven ash/smoke billboards with finite lifetimes, sorted within the plume. Quality budgets are 24/40/64 puffs; distance fade is 18–24 km.
- Ballistic, cooling ember sprites: maximum 12/24/40 by quality; emitted intermittently and retired at terrain contact. They fade between 3.5 and 5.5 km.
- One warm point emitter, with a 70 m physical emitter radius. Its intensity follows the renderer's inverse-square light convention. Camera-distance fading removes it smoothly between 1.5 and 2.2 km, and it never exceeds the existing 12-light capacity.
- Effects are excluded from indoor hangar previews.

This is a bounded visual volcanic system. It does not implement lava-fluid simulation, ash weather hazards, eruption damage or a new flight model. Smoke uses the existing sprite renderer and is not a volumetric fluid simulation. Native GPU timing and visual day/night review remain required.

`volcano_effects_test` checks the floor/wall relationship, deterministic output, finite particles, terrain contact, sprite and light bounds, hangar suppression, distant culling and an intensity range that survives the lighting shader's rejection threshold.
