# Osprey C6: geometry revision and audit

## Actual correction

Moved the matched wing-root z coordinate from −1.35 m to −0.80 m in both delivered aggregate rows. No other production field changed.

The main-gear bay is centred at x=±1.612 m, z=0.392 m, with half-width 0.17 m and half-length 0.37 m. Its aft edge is z=0.762 m. Previously the fixed-panel/flap boundary crossed the bay: the worst full-width clearance was −0.480341 m. It is now +0.069659 m, exceeding the new 0.05 m minimum. The bay no longer crosses the moving flap cut, and the forward/thicker fixed section provides a better attachment at both main struts. This is a geometry layout correction, not a change to shared animation code.

The validator now checks the whole bay envelope and exact matching of the spec/model wing roots. It retains all previous checks. Wing area, span, thickness, cabin/stations, cockpit eye, engine and gear parameters, mass and flight coefficients are unchanged.

## Reviewed without changing

- Matched true orthographic front, rear, top and underside views use centred cameras and lighting without a lateral component. The earlier `front` and `rear` views are perspective-like quarter orientations despite being orthographic projections; they are unsuitable for judging bilateral shape.
- Wing/fuselage joins, mirrored nacelle/wing joins, tail roots, neutral controls and gear attachments were inspected in those matched views and the detailed underside views. No further unintended structural gap or bilateral mismatch was identified at the rendered resolution.
- Production-GLSL mirror sampling covers 1,769,472 point pairs across 6 gear positions (0, .021, .1, .5, .9, 1), 3 reflected control states (−1, 0, +1) and both flap endpoints. Pitch/flaps stay equal; roll, yaw and steering change sign under reflection. Maximum structural mirror error is 0.000000000 m in the sampled float field; 0 unexpected failures. The intentional pitot contributes 36 unequal samples, maximum 0.015721381 m, in its exact production bound. See verification/fix-geometry.log.
- The single left-wing pitot is intentionally asymmetric in the existing game, as are pilot position and actual red/green navigation colors. The initial strict audit detected the pitot at one sampled point per state, 36 differences total. The final test reports that exact production-bounded exception independently, rather than hiding it as a generic tolerance.
- Propellers in the inspection compositor have arbitrary stopped rotation phases. Their blade angles are not structural symmetry evidence. The supplied geometry-only silhouettes exclude that compositor and all materials. At 960×800, top/underside masks are exactly mirrored; front/rear differ by 12 pixels each, at the left pitot and its absent mirrored counterpart. See verification/fix-silhouette-metrics.json.
- Thin flap/aileron/elevator/rudder hinge gaps are authored articulation clearances. They have not been filled or fused, which would interfere with control motion.

## Shared-engine limitation

The unchanged `gearBay` function clamps the cutter with `res.x + 0.03`, preserving an approximately 30 mm skin around the cut. The generic gear wells are therefore enclosed cavities rather than fully open bays. Doors, legs and wheels still animate. This package does not claim to fix that shared behavior or alter other aircraft to do so. Gear removal at the existing near-zero threshold is also unchanged.

## Fresh verification on the delivered rows

- 23/23 analytic checks pass, including full bay clearance.
- Rebuilt Release Linux suite: 10/10 CTest tests pass; see fix-build.log and fix-ctest.log.
- Maximum initial mass 2450 kg sorties: 0 failures; takeoff 347 m, climb 5.7 m/s, comfort-route bank 25.8°, crosswind touchdown 0.7 m/s with 2.9 m lateral stop error and no go-arounds. See fix-max-weight.log.
- Actual-GLSL endcap jump remains 0.000202 m, below the unchanged 0.001 m threshold. Headrest fitting continues to omit unsupported headrests; its printed 1 m sentinel is not physical clearance.
- The row files are exact substrings of the tested isolated C++ source; no repository content was pushed or changed. No test assertion was weakened.

The check remains a sampled Linux llvmpipe/SDF inspection, not a mathematical proof over all continuous states or Windows/full-game/rasterizer validation. Cockpit instrument textures remain outside this inspection harness. A separately authored unique-interior supplement, if included by the integrator, is additional to these Tier A row changes and needs its own checks.
