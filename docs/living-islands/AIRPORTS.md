# Airport ground-life pass

## What changed

`src/airport_scenery.cpp` adds restrained, organised landside activity to all 16
existing airports: low service stores, operations offices at paved fields,
stationary fuel/support vehicles, staff cars, smaller parking-area lamps, and low
boundary planting. Small strips retain their flight-club, farming, research or
polar character. Existing terminal, hangar, apron and parked-aircraft layouts are
retained; the shared entity meshes/materials supply their improved close-up detail.

The paved terminal lots now have a continuous 16 m central access aisle and a real
opening in the perimeter fence. Fence endpoints are set 8.2 m from its centre to
allow for post thickness. Airport access-road endpoints are coordinated at
`(termU, side * (lotV1 + 18))`; strip access is at
`(termU + 80, side * (bldV + 40))`. The community road pass only connects suitable
outward-facing routes. An open entrance does not imply that every remote field
has a road all the way to a town.

Cars whose footprints would overlap existing furniture or block an access route
are omitted. Clearing those cars means total object counts can fall even though
service detail was added.

## Current airport asset construction, 2026-10-10

The shared airport module now has four authored mesh tiers, including a distance-gated close tier for all 20 airport/vehicle kinds. Hangars have roof/door construction, terminals and towers have fitted glazing, equipment has mechanical fittings, parked aircraft have curved airframes and physical engine/gear detail, and road vehicles have curved bodies, real wheel profiles and recessed optics.

The [current asset ledger](ASSETS.md#current-measured-mesh-inventory) and [per-kind/slot CSV](performance/airport-asset-inventory.csv) record exact costs and bounds. The arch-hangar door now has a true end-wall opening, the fuel-pump display is no longer hidden by its coarse sign, and the pump casing is painted metal while pad/piers remain concrete. Fixed runway lights and PAPI retain their frozen geometry and placement.

This improves authored construction without adding a new airport simulation. Generic equipment, simple service-side walls and opaque interior cues remain limitations. Final production closeups and target-GPU profiling are separate acceptance gates.

## Safety and determinism

- Runway definitions, `airport_layout.h`, terrain heights and physical ground are
  unchanged by this pass. No runway position, heading, size, elevation, surface,
  threshold, taxiway or apron geometry is edited.
- Added props are behind movement areas, outside runway grading and approach
  funnels, and inside the existing airport terrain envelope. Placement checks
  include the complete rotated footprint, not just the centre.
- A site must be dry and have less than 1.25 m of terrain-height variation over its
  centre and footprint corners. Unsuitable details are skipped; terrain is never
  reshaped to accommodate them.
- New details require 0.65 m clearance from existing furniture. Their independent
  deterministic random stream does not reshuffle earlier aircraft/gate choices.
- Public-entry and apron movement routes remain clear for service/emergency use.
  This is a scenery-clearance guarantee, not a new emergency-vehicle simulation.

## Validation

Against the integrated source at this pass's completion:

- `airport_environment_test`: 97,114 checks, zero failures across all 16 airports.
  Includes bit-identical repeated generation, finite transforms, runway-definition
  nonmutation, the full 16 m paved entrance, dry/slope limits, approach and apron
  exclusions, fixture separation, and service-yard/lighting presence.
- The test identifies 158 new service/landscape fixtures by their dedicated
  kinds/scales. This is not a count of every additional car or truck.
- `airport_layout_test`: 4,933 non-taxi-light objects, zero runway/taxiway/AI-stand
  incursions or solid-object overlap problems. Rerun after final integration.
- These CPU checks do not establish photorealism, final image quality or the
  native-1080p RTX 3070 / 60 FPS target. Production-render close-ups and target-GPU
  measurements remain separate requirements.

## Suggested production-render review cameras

All coordinates are metres in world `(x, y, z)`, with absolute altitude. These are
recommended starting cameras derived from the actual placement coordinates, not
claims that screenshots have already been captured or visually approved.
Use the same camera, daylight/weather, exposure, native resolution and scenery
quality for before/after images. A horizontal FOV near 50–60 degrees is a useful
starting point; adjust only if actual rendered framing needs it.

| View | Camera position | Look-at point | Inspect |
|---|---|---|---|
| Port Verde service yard | `(-26424, 60, 5731)` | `(-26466, 20, 5691)` | Low service store, bowser parking, cargo/hangar context, material scale |
| Port Verde terminal landside | `(-26069.54, 100, 5530)` | `(-26089.54, 19, 5683)` | Continuous central aisle, actual fence gap, occupied lots, low planting, small lamps |
| Meadowbrook service yard | `(-8340.55, 70, 13933.30)` | `(-8270.53, 49, 13948.29)` | Regional-scale support yard, grounded vehicle silhouettes, terminal context |
| Harlan Farm support store | `(-1108.01, 89, 20790.57)` | `(-1146.25, 74, 20850.12)` | Rural service shed/bowser, existing arch hangar, no urban overdevelopment |

## Deliberate limitations

Cars and support vehicles are static parked scenery. There is no pedestrian,
animal, road-traffic, service-vehicle or worker animation in this pass. Smaller
lamps use the existing emissive fixture system; no additional shadowed dynamic
lights are introduced. The models remain bounded procedural geometry rather than
photogrammetry assets. These choices preserve runway safety and runtime budgets
while improving spatial context, but must not be described as fully simulated
crowds or certified photorealism.
