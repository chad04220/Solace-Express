# Aircraft and community scale audit

## Finding

The flyable fleet and environment use the same metre coordinate system. Production aircraft placement applies rotation and translation without a world-size multiplier. All 13 authored/packed main-wing spans match their flight-physics spans exactly. This verifies the game's own model/physics consistency; it is not a claim that every fictional aircraft exactly matches a real manufacturer's dimensions.

The measured main-wing spans range from 10.1 m (Kestrel) to 27.4 m (Meridian). Kestrel fuselage stations span 7.30 m; Meridian stations span 26.05 m against its 26.00 m nominal physics/model length. Small fixtures, propellers, rounded surfaces and lights extend beyond these authored structural stations. Research craft use additional custom shape functions; their nominal wing endpoints also match the authored spans. These numbers are not a GPU-measured complete airframe bounding box.

## Useful comparisons

- Kestrel T2 main wing: 10.10 m wide; nominal fuselage 7.30 m long.
- Meridian Q400 main wing: 27.40 m wide; nominal fuselage 26.00 m long.
- Actual unit-scale passenger-car mesh: 1.80 m wide × 4.40 m long × 1.46 m high.
- Actual unit-scale house: 9 × 11 m main walls, 10 m roof width, 12.70 m depth including porch, 8.60 m main ridge and 9.40 m chimney top. Its authored entrance is 1.10 × 2.20 m.
- Local street: 7 m carriageway, plus 1.5 m sidewalk on each side. A Kestrel's wingspan therefore exceeds the carriageway width, as expected for an aircraft beside a car street.
- City blocks: 84 × 56 m. Town blocks: 140 × 112 m. Village blocks: 168 × 112 m.

The generated world currently measures approximately:

| Building | Actual above-base height, including roof hardware |
| --- | ---: |
| Houses | 8.46–10.34 m |
| Bungalows | 5.76–7.04 m |
| Townhouses | 10.44–12.76 m |
| Apartments (corrected fixed module) | 23.20 m |
| Offices | 25.87–38.54 m |
| Towers | 68.00–108.80 m |
| Skyscrapers | 104.00–166.31 m |

These are actual LOD0 mesh bounds multiplied by generated instance scales, not just the nominal `kEntInfo` heights. Foundation geometry below the base is excluded from height.

## Storeys and the corrected apartment detail defect

Facade coordinates are already scaled into metres before window placement. Window/floor pitches consequently stay fixed when a building gets taller: houses 2.9 m, bungalows/farmhouses 3.0 m, townhouses 3.2 m, apartments 3.4 m, towers 3.6 m, and offices/skyscrapers 3.7 m. The skyscraper height multiplier adds window rows; it does not stretch each window. Saying all buildings have 3.2 m storeys would be inaccurate.

The audit found a mesh-detail mismatch in 652 apartment instances: their previous vertical scales of 0.650–1.673 stretched balconies to 2.210–5.687 m spacing and entrance doors to 1.690–4.349 m high, while shader windows remained 3.400 m apart.

The approved minimal environment-only correction is now applied: this apartment archetype remains at vertical scale 1.0. Measured balcony/floor spacing is now exactly 3.400 m and entrance height exactly 2.600 m. Offices, towers, skyscrapers, building count and all aircraft remain unchanged. Apartments now share a 22 m nominal height (23.2 m including roof equipment) until proper modular height variants are introduced. No additional mesh generation, draw calls or shader work was added. The audit asserts these corrected dimensions.

## Mount Kaleo, relative to aircraft

The broad cone's authored footprint is 10.4 km across. Its summit crater has a roughly 290 m floor core, a 680 m rim diameter, and a 1.04 km outer terrain blend, contained within the 1.2 km audit diameter. The outer edge blends into the original irregular mountain; these are design dimensions rather than a perfectly circular silhouette.

Actual full-detail terrain sampling at one-degree intervals measured:

- Crater centre: 1,780.13 m above sea level.
- At the 340 m rim radius: 2,000.77–2,026.50 m elevation, or 220.64–246.37 m above the centre.
- At the 145 m floor-core radius: 1,780.16–1,786.82 m elevation.

The 680 m crater rim spans about 67.3 Kestrel wingspans or 24.8 Meridian wingspans. It is a mountain-scale formation, not a small prop enlarged for the camera. These dimensions are verified from the same `World::height` surface used by rendering/physics, not from the smoke billboard or the screenshot's perspective.

## Reproduce

From the repository root:

```sh
cmake --build build --target environment_scale_audit
ctest --test-dir build -R '^environment_scale_audit$' --output-on-failure
```

The audit checks production model packing, metre-coordinate shader contracts, actual environment mesh bounds, generated building scales, corrected apartment floor/door modules and sampled volcano dimensions. It passes with zero failures. The CMake target and CTest name are `environment_scale_audit`; it is also a dependency of the existing CI `airport_layout_test` target.

The reviewed [actual Kestrel / house / sedan view](previews/kestrel-house-car-scale.png) uses the flyable aircraft through its production rendering path, staged on an apron with unit-scale scenery specimens. The house is 9.4 m above grade; its mesh includes 3 m of buried foundation. Background parked airliners are scenery and are excluded as proof of flyable-fleet scale.

The [volcano reference](previews/kestrel-volcano-scale.png) stages the same unscaled Kestrel airborne over the real crater. Its camera distance is 514 m versus 877 m to the bowl target, so perspective must not be mistaken for an equal-depth measurement. The tiny white aircraft is just above the lava near image center. This is a spatial reference, not a flight replay. These captures precede the final subtle wall-fracture refinement and the upstream shader-specialization integration; their physical dimensions are unchanged.
