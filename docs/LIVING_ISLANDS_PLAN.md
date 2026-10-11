# Living islands: the plan

The owner's brief (2026-10-10): the islands should pass for a real, lived-in place - farms, villages, towns and cities
joined by roads and highways, with bridges, and landmarks worth flying past and looking down on - at the level of
detail the aircraft have. Codex's Living Islands proposal (merged for v3.44) is the starting point: its building, tree
and vehicle meshes, photographic materials, shallow water and volcano crater stay.

Each phase ships as a version, measured on the owner's RTX 3070 laptop through `diagnostics.bat` (native 1080p,
60 fps or better everywhere) before the next starts.

## Where it stands (v3.47)

- 26 settlements, 14,098 street-front buildings. Since v3.44, Kaleo's east has a port city (Kailani), a market town
  (Canefield) and three villages, and Palm Bay's island, Far Isle and Nordholm's coast have a village each.
- **Phase A, items 1-4 shipped.** The road network is generated and cached with the world: 44 paths, 104 km of
  highway, 191 km of roads and 63 km of lanes joining all 26 settlements and every airfield on an island that has
  one. The roads are graded into the ground (on the CPU and in every shader's `terrainH`, held in step by
  `road_grade_gpu`) and painted by class. Runways, airfield grounds and approach funnels are untouched
  (`tests/fixtures/RUNWAY_BASELINE.md`, road-grading exception).
- **Laid out the way roads are (v3.47, `roads_test`).** Routes are found by A* over the terrain in 40 directions at
  the class's grade (6, 9, 12%: never steeper, so the profile can follow the ground), with a turning cost and no
  doubling back in one corner, so a climb is a switchback of long legs and hairpins. A route along a road already
  built joins it at a junction rather than running beside it. The bends are filleted and eased out to the class's
  radius (450, 140, 45 m) where the ground lets them, and sampled every 3 degrees. The profile is clamped to the
  grade and its crests and sags rounded to the class's K (40, 18, 7 m per percent). Under it the ground is a level
  platform with 1-in-2 banks, its end square where it runs onto a bridge. Beside an airfield the road lies on the
  airfield's ground as it is. The terrain mesh is finer along graded roads out to about 2 km.
- **Phase A, item 5: beam bridges done.** A beam bridge on piers stands under every span the network leaves to a
  bridge (`bridges.h`, `bridge_mesh.h`): 12, seven over the water, five where mountain lanes cross gullies or stand
  more than 15 m above a slope. Each is as wide as its road's platform and square to it at its ends, carrying the road's
  surface and markings between concrete parapets (a kerb and a steel rail on the lanes), on a box girder. Its piers go
  down to the ground or the sea floor, with an abutment at each end. They are drawn and shadowed like the buildings,
  they are solid, and nothing grows up into them (`bridges_test`).
- **Phase B, items 1 and 3 begun (v3.47, `settlements.h`, `community_layout`).** Each settlement grows over an effort
  field from its centre: steep ground is dear, a road cheap to build along, and the sea and airfields can't be built
  on. Its streets are network roads of class RC_STREET: a grid core turned to the main road in towns and cities, and
  round it branches that hold a 12% grade, keep 40 m apart, and join the next street or end in a turning. They meet
  every road level, pinned at the crossing, with a level landing past the other road's platform. Lots go along every
  street and road (not the highways), zoned from the heart out, facing their road, clear of the platforms and of each
  other. Each settlement has a church on its main road, and a park in the towns and cities. Street trees line the
  centres and the gardens have trees. The mask's town ground follows the buildings, so woods and fields come up to
  the last houses. 26 settlements, about 23,000 buildings, 305 km of streets.
- **Still to come in Phase A:**
  - Item 5: arch and truss spans for the longest crossings.
  - Item 6: road furniture.
- The islands sit in 10 km of open sea, and the map wraps every 100 km (v3.45).

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
