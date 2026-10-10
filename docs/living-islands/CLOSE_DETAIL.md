# Close-detail asset redesign (in progress)

The user rejected the scale-preview house and car as too simplified, then requested every environmental asset to meet the aircraft's visual-quality standard. That feedback supersedes acceptance of the earlier environment checkpoint. Its passing tests and distant screenshots are not approval of the new detailed assets.

## Rendering policy

Entity mesh slot 3 is a new close-inspection mesh. Slots 0, 1 and 2 retain the established near/middle/far ranges, including the previously extended foliage ranges. The close tier is not a global multiplication of every visible object's geometry.

- Trees: approximately 55–94 m by quality; bushes approximately 27–46 m
- Cars: approximately 60–102 m; trucks approximately 84–144 m
- Rock formations: approximately 109–186 m; small rocks approximately 39–66 m
- Buildings/equipment: size-dependent, capped at approximately 196–336 m
- Close meshes and tier zero share complementary screen-door masks over the final 15% of the close range
- At most two meshes overlap per instance; the pixels belong to one or the other
- Existing shadow tiers and auxiliary-camera tiers do not use the new close meshes
- Dynamic ground vehicles use the same close-tier selection and wheel-pose interface

The CPU policy test exercises 3,224,602 checks across all entity kinds and quality levels, including mask coverage, no overlapping fade intervals, bulk-chunk eligibility and camera-feed exclusion. It does not measure actual GPU performance. Geometry and shader changes require separate validation and pixel review.

## Authoring ownership

The procedural source is split into three included modules using the existing shared mesh builder:

- `entity_mesh_buildings.inc`: residential, commercial, civic and rural structures
- `entity_mesh_airport.inc`: airport structures, equipment, scenery aircraft and ground vehicles
- `entity_mesh_nature.inc`: tree/shrub/palm species and rock formations

Every kind needs an intentional close form and coherent cheaper forms. A mesh having more triangles is not sufficient acceptance: fitted construction, believable proportions, real recesses and glazing, material response, normals, contact, and silhouette transitions must be checked in actual multi-angle renders.

The immutable runway preservation contract remains in force. This work is not yet ready for publication.

## Human-scale architecture

Physical openings exposed an older placement problem: requested building heights could scale an authored 2.2 m door to 1.43–3.74 m. Habitable building vertical scales now stay within 0.92–1.08, with apartments at 1.0. Silos and water vessels retain their broader silhouette range. Tall city building type choices and lot density remain; arbitrary stretching of floor modules is removed. A future wider continuous skyline-height range should use floor-count variants.

The established three tiers keep their distance ranges, not necessarily their old vertex counts: for example the car now needs a coherent curved lower-detail shell so it does not turn back into a box at the close-tier boundary. Exact per-kind budgets are regression-tested. Shadows still use the lower tiers and therefore must be measured with these revised counts.
