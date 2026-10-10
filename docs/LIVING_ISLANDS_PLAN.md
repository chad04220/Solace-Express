# Living islands: the plan

The owner's brief (2026-10-10): the islands should pass for a real, lived-in place - farms, villages, towns and cities
joined by roads and highways, with bridges, and landmarks worth flying past and looking down on - at the level of
detail the aircraft have. Codex's Living Islands proposal (merged for v3.44) is the starting point: its building, tree
and vehicle meshes, photographic materials, shallow water and volcano crater stay.

Each phase ships as a version, measured on the owner's RTX 3070 laptop through `diagnostics.bat` (native 1080p,
60 fps or better everywhere) before the next starts.

## Where it stands (v3.44 candidate)

- 18 settlements: 2 cities, 3 towns, 13 villages. Lots on a 28 m grid round each centre, aligned with the road in.
- Roads: 45 straight segments painted on the ground, 8 m wide. They join some towns and airports; one crosses the south
  bay over open water, and much of each island has none. No highways, no bridges, no grading.
- Buildings: 10,003 in the world, 36% fewer than before the merge - the farms, barns, silos and country shops mostly
  went into denser towns and cities. Trees, bushes and rocks: unchanged (15.9 million and 360,000).

## Phase A - the road network

1. A network generated with the world (cached with it): highways between the cities and towns, regional roads to every
   village and airport, lanes to the farms. Each route found over the terrain (slope, water, airports, settlements), not
   drawn straight: curves that follow valleys and contours, switchbacks where a road climbs.
2. Classes with their own cross-section and markings: dual-carriageway highway (two lanes each way, central reserve,
   hard shoulders, crash barrier), two-lane regional road (centre and edge lines), country lane (narrow, no lines),
   farm track (gravel). Junctions without markings running through them.
3. Thousands of segments instead of 64: a grid of per-cell segment lists the terrain shader reads.
4. The ground under a road levelled across its width and smoothed along it (cut and fill banks), so a road reads as built
   from low level too. Runways and the airfields stay exactly as they are.
5. Bridges wherever a road crosses water or a deep valley: beam bridges on piers for short spans, arch and truss bridges
   for longer ones. Real meshes, lit and shadowed like the buildings.
6. Road furniture: barriers and lighting along the highways near the cities, signs at junctions, poles along country
   lanes.

## Phase B - settlements

1. A hierarchy that thins out from the cities: city (dense core, towers, avenues), town (main street, squares, shops,
   terraces), village (houses along the road, a church, a pub, a school), hamlet, farmstead.
2. Farmsteads restored and better: a farmhouse, barns, silos, machine sheds, a yard and its fences, set among the
   fields and reached by a track.
3. Streets that follow the roads into each settlement: a main street through the centre, side streets off it, squares
   and parks, car parks, street trees and lights.
4. Building detail at aircraft quality: window frames and sills, doors, chimneys, gutters, balconies, shop fronts and
   signs, rooftop plant on city blocks; varied, weathered facades; interiors lit at night floor by floor.
5. Waterfronts: harbour walls, quays, marinas with boats, beaches with promenades.

## Phase C - landmarks (hand designed)

Each built as its own set of meshes, placed by hand, and meant to be flown past and looked down on:

- **Strait bridge**: a cable-stayed bridge carrying the coast highway across the south bay by Port Verde.
- **Kaleo observatory**: telescope domes on the volcano's rim, reached by a switchback road.
- **Lighthouse Key**: the lighthouse, keeper's cottages, a landing stage and cliffs.
- **Fjordhaven dam**: a concrete arch dam with its reservoir in the northern mountains.
- **Port Verde container port**: cranes, stacked containers, a ship at the quay.
- **Solace Capital stadium**: a lit bowl in the city's park.
- **Headland fortress**: a star fort on a cape.
- **Wind farm**: turbines along a ridge, turning with the wind.
- **Shipwreck**: a rusting hull on a reef in clear shallows.
- **Stone circle**: on a bare hilltop.
- **Offshore platform**: a rig with a flare and a supply boat.
- **Racetrack**: a circuit with its grandstand.

## Phase D - life

Cars and lorries driving the network (headlights and tail lights at night), ferries between the islands, boats in the
harbours, trains if a line earns its place, lights coming on across the towns at dusk.

## Phase E - the whole picture

Terrain materials, water, vegetation, clouds and lighting looked at again against the aircraft, scene by scene, from
the cockpit at low level and from altitude.

## Contracts kept throughout

- Every runway, its markings, lights and approach surfaces exactly as they are (`runway_preservation_test`).
- Collision follows what is drawn: buildings, bridges and terrain edits are solid.
- The world cache invalidates itself when the generator changes; the first launch after an update builds it once.
- Frame time measured on the owner's GPU every phase; nothing ships that takes a scene below 60 fps at 1080p.
