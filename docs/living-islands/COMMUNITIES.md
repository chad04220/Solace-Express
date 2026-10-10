# Island communities and immutable terrain

## What changed

- Each community now has its own street frame, aligned to the nearest original road. City blocks are 84 × 56 m, towns 140 × 112 m, and villages 168 × 112 m. The CPU and ground shader use the same uploaded origin, rotation and block dimensions.
- Buildings occupy street-front lots and face the nearest street. Block interiors remain gardens. A central green reserves space for a civic landmark and a small number of planted trees.
- Cities retain dense blocks and substantial high-rise skylines, as explicitly requested. Shops, townhouses and mid-rises transition into clusters of towers and skyscrapers towards the core. The two cities use tighter blocks than towns and villages, and tall buildings retain the baseline height multipliers. The final generated world has 9,502 street-front buildings, including 476 tall city buildings: Port Verde has 3,911 buildings and Solace Capital 3,324.
- Low-rise homes have deterministic garden planting and occasional driveway cars. Larger town/village block interiors have sparse, district-varied courtyard trees; house-garden planting was reduced to offset that addition. These are static scenery, not moving road traffic or a population simulation.
- Farm compounds face a nearby road, with barns and silos organized around the farmhouse instead of arbitrary headings. Farm buildings and garden items now belong to the chunk containing their own centres.
- New road links are accepted only on dry terrain, with bounded grade and runway protection. Airport links must leave through the landside gate at a sufficiently outward angle. The full rendered road table contains 45 segments, below its fixed 64-segment budget. A site without a safe connection is not linked across water or steep cliffs.

## Settlement relocation

Seven original community centres produced no inhabitable lots: two were underwater and the others lay in airport approach exclusions. Only these settlement centres moved. Existing populated communities retained their locations; no airport moved.

| Community | Original centre (x, z), m | New centre (x, z), m | Generated frontage buildings |
| --- | --- | --- | ---: |
| Harlan | 200, 22500 | 600, 22000 | 26 |
| Lighthouse Key | -33600, 26200 | -33000, 27900 | 7 |
| Westvale | -30000, -2000 | -27000, -3400 | 111 |
| Riverton | -12000, -1000 | -16400, 3000 | 147 |
| Greenhollow | -4000, 9000 | -4800, 8000 | 105 |
| Saltmarsh | -20000, 22000 | -16500, 17700 | 73 |
| Ice Harbor | 27000, -27500 | 31000, -24900 | 46 |

All 18 named communities now have accepted building sites. The larger relocations are approximately 4.8–5.9 km because closer land was unsuitable or protected. Buildings are still rejected on unsafe slopes, water or airport exclusion areas.

## Preservation boundary

`sceneryBaseMod` uses the original, immutable settlement array and original road polylines. Those are part of the terrain recipe and must not be replaced by the new scenery coordinates. Even the original first polyline's accidentally zero-filled eighth point is retained for terrain calculation; it is omitted only from the rendered road network. Runway dimensions, headings, elevations, surfaces and thresholds are untouched by this work. The separately authorized Mount Kaleo crater is the only terrain-shape exception in the wider update, confined to the 600 m audit disk documented in `BASELINE.md`; community placement itself never modifies terrain.

The world-cache format is invalidated separately so new masks and road IDs are not combined with older scenery. Cache loads rebuild the derived road and community-lot tables from the loaded, unchanged terrain.

## Runtime budget and checks

Accepted and rejected lot records are computed at world setup and read in O(1) during chunk streaming. The derived cache occupies 1,867,944 bytes (35,922 records × 52 bytes, about 1.8 MiB); a standalone rebuild measured 10.46 ms on the review CPU. This is a startup-memory tradeoff, not a hardware performance guarantee. Cached conservative footprint heights also avoid repeated ground sampling for streamed buildings. World setup invalidates the cache before generation, and queries for another World use the uncached calculation. The 28 m lot lattice provides a fixed spatial density bound. No new asset kind, render pass, texture sampler or animation system is introduced by community placement.

`community_layout_test` verifies all 18 communities are inhabited, deterministic lot selection, orthonormal street frames, street-facing entrances, reserved central greens, footprint street/runway/water clearance, a maximum 2.6 m footprint relief, a substantial but bounded city skyline, safe new road links, per-chunk ownership, and identical direct-detail versus level-upgrade generation. It is a CPU regression test, not an RTX 3070 frame-rate measurement.

Suggested production review centres: Port Verde (-29500, 10500), Solace Capital (-3200, -1200), Meadowbrook (-6600, 15700), the relocated Westvale (-27000, -3400), and the coastal Ice Harbor (31000, -24900). View from both near-street and approach altitude; inspect yards, greens and buildings at street intersections.
