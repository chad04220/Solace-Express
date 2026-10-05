# Integration handoff

Owner-authorized scope: two new aircraft; detailed landing gear and wheels on both and all nine existing aircraft; a lower, smoothly blended XR-30 canopy exterior; rotating wheels on every wheeled vehicle. Native new-aircraft definitions and separately requested shared model edits are independent review items. Base: `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6`.

## New aircraft

Use [authoring.md](authoring.md), which follows the supplied brief's per-aircraft output format and computed self-checks. Paste Swift's `AircraftSpec` and `ModelDef` rows at aligned index 7, followed by Nightjar at aligned index 8, immediately before the original XR-30 rows. Update `kResearchJet` to 9 and `kWraith` to 10. Leave both old research types last; the native array-length-minus-two calculation then yields nine career aircraft. Original career indices 0..6 remain unchanged. Retain stable IDs `swift_s6` and `xr10_nightjar`.

Do not copy the laboratory count override or temporary slots 9/10 into production. `Plane::perf` / `aeroModel` index their caches from actual pointers into `kAircraft[]`; the lab appends real entries, rather than using detached local structs.

Swift is a costlier, faster, longer-range PPL touring choice above the Wren, with retractable gear and paved-runway operation. Nightjar is an optional ATP civil research platform with equipment space, ordinary fuel and aerodynamic limits. Both use `special=0`; neither depends on the XR-30 or Wraith physics/cockpit route. Economy values are design proposals, with performance verified separately. No authored mission or research programme is implied by adding the row.

`AircraftSpec.wingY` and `wingZ` in the supplied brief disagree with the pinned source's coarse-contact convention. The rows match the actual code: height divided by `fusRad`, and wingtip quarter-chord z. `ModelDef` contains the root position in metres. Retracting nose gear in `packModel` uses `max(-.36*L, st[3].z+.35)`, while the coarse physics contact still uses `-.36*L`. These pre-existing source/documentation mismatches are called out in authoring.md; the model review does not change the solver or contact positions.

## Fleet gear and wheel patch

Shared changes are limited to `src/shaders/plane_sdf.glsl`, `wraith_sdf.glsl`, and `plane_material.glsl`. All existing aircraft rows, wheel radii, contact envelopes, steering equations, deployment paths, gear selectors and model packing stay unchanged. The same finish is used by Kestrel, Wren, Bushmaster, Islander, Pelican, Meridian, Starling, XR-30, XR-40 and both candidates, with geometry appropriate to fixed spring legs, tailwheel gear, single retracts and paired nacelle/research wheels. Spats receive a hub access opening.

The patch adds metal wheel rims, projecting axle caps and inboard brake discs to mains; oleo sleeves, torque links and short diagonal drag braces to appropriate legs; and scaled collars to fixed/tailwheel gear. Fine grooves, recess rings and six-bolt hub patterns are evaluated once during surface shading, not as extra solids in every ray step. Existing material IDs 6 (rubber) and 8 (metal) are reused. Helpers use conservative local bounds relative to the current nearest distance.

`apply_gear_detail.py` and the two `gear_*.glsl.inc` files reproduce the three edits atomically against the verified parent shaders. The final Tier A aircraft require no negative-wing bound correction and no centre-display relocation; neither experimental hook remains in this patch.

For the forthcoming mesh renderer, attach added leg details to the existing leg part, wheel rims/discs to the existing wheel part, and tread/bolt patterns to the wheel's rest-frame UV/material coordinates. Reuse the same gear and steering rigs as their parent parts. This patch targets today's SDF files; its manual deployment formulas are not a new Tier B aircraft or a substitute for the upcoming PART/rig contract. Do not bake moving details into the static body.

## XR-30 canopy exterior

Only `mapJet`'s exterior opaque sensor-canopy primitive/union changes. Centre y falls from 0.50 to 0.44 m; vertical radius from 0.42 to 0.37 m; crown falls 0.11 m. Half-width increases from 0.60 to 0.62 m and half-length from 1.90 to 2.00 m. A 0.16 m smooth union blends the shoulder into the existing airframe, below the brief's 0.35 m blend limit. Material 32, the sealed interior, pilot eye, feed-camera panes and feed cameras are unchanged. The canopy is closed static geometry and remains inside the existing plane bound.

## Prepared wheel animation

See [the wheel integration](../../../prototypes/wheel_animation/README.md). It is deliberately optional in this review branch: existing production C++ files contain no new wheel-state fields until Claude applies or ports that patch. It extends common player/AI animation state and the entity wheel rig, independently of the four Tier A aircraft rows. `AircraftSpec`, `ModelDef`, catalogue IDs, original contact forces and save layout are preserved.

Signed travel per contact, divided by the rendered tyre radius, drives main-left, main-right and nose/tail phases. It follows turns and reverse motion, stops on stationary contact, and coasts or brakes after lift-off. Pauses and additional camera/shadow passes never advance state. Existing research nose geometry remains unsteered.

All four wheeled scenery kinds have separate wheel slots/pivots in both detailed LODs: parked GA plane (3), parked airliner (6), car (4), fuel bowser (6). Static scenery draws zero phase. An explicit `GroundVehicleVisual` packet supports a future movement driver, using the same vertex/instance strides. It updates both view and shadow pose and invalidates only cascades affected by changed or removed packets. It does not create routes, vehicle AI, collision registration or moving airport furniture. The driver must own position/velocity/reset and replace any static duplicate in scenery.

For the upcoming aircraft mesh renderer, each wheel's `rigSPIN` uses the same phase and radius, composed under its gear and steer parents. Keep rotating rims/brake rotors on the wheel, and fixed brake calipers/torque links on the leg. Existing SDF tyres/rims are axisymmetric; their visible bolt/rest-coordinate finish rotates without changing their distance envelopes.

## Release checks

Review the four rows and three shared shaders independently, integrate the index constants, then rebuild and run the normal tests plus the candidate cases. Regenerate learned performance with `flight_test --table`; the design speeds and guide's formula stalls are different from learned performance. Exercise existing career saves, newly purchased/rented Swift flights and both added selectors before release. On the owner's Windows / RTX 3070 build, inspect all guide harness angles, night cockpit lighting, moving gear/doors/controls and paired performance shots. Linux software evidence does not establish hardware frame cost, so measure the shared gear patch before merging it into the release.
