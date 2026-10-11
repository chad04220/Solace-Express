# Living islands: the plan

The owner's brief (2026-10-10): the islands should pass for a real, lived-in place - farms, villages, towns and cities
joined by roads and highways, with bridges, and landmarks worth flying past and looking down on - at the level of
detail the aircraft have. Codex's Living Islands proposal (merged for v3.44) is the starting point: its building, tree
and vehicle meshes, photographic materials, shallow water and volcano crater stay.

Each phase ships as a version, measured on the owner's RTX 3070 laptop through `diagnostics.bat` (native 1080p,
60 fps or better everywhere) before the next starts.

## Where it stands (v3.48 and after)

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
- **Phase A, item 5: arch and truss spans done (after v3.48).** Each long crossing of the water (200 m or more, on a
  highway or a road) carries one main span over its deepest water, wherever the deck turns no more than 12 degrees
  across it. The span stands on two heavier piers, with nothing under it:
  - a highway's is a steel Warren truss over two spans, continuous over a pier between them (160 m, 11 m high);
  - a road's is a white tied arch (125 m, rising 23 m), its hangers carrying the deck and its ribs braced together
    over the road.
  - Every mountain lane's viaduct is now masonry: a sandstone arch between each pair of supports, its spandrels solid
    up to the road and its parapets stone (19 arches, rising 7 to 13 m).
  - The steel and the spandrels are solid where they are drawn, the road under the steel is clear, each arch is open
    under its crown, and the scenery stays under the arches (`bridges_test`).
- **Phase B, items 1 and 3 begun (v3.47, `settlements.h`, `community_layout`).** Each settlement grows over an effort
  field from its centre: steep ground is dear, a road cheap to build along, and the sea and airfields can't be built
  on. Its streets are network roads of class RC_STREET: a grid core turned to the main road in towns and cities, and
  round it branches that hold a 12% grade, keep 40 m apart, and join the next street or end in a turning. They meet
  every road level, pinned at the crossing, with a level landing past the other road's platform. Lots go along every
  street and road (not the highways), zoned from the heart out, facing their road, clear of the platforms and of each
  other. Each settlement has a church on its main road, and a park in the towns and cities. Street trees line the
  centres and the gardens have trees. The mask's town ground follows the buildings, so woods and fields come up to
  the last houses. 26 settlements, about 23,000 buildings, 305 km of streets.
- **City skylines (v3.48).** Each city peaks at its heart: a landmark supertall, then skyscrapers, round towers,
  tower blocks and office blocks, thinning to mid-rise and then houses. Mean height is 118 to 127 m at the heart of
  Port Verde and Solace Capital, and 84 m in Kailani. Suburbs fill in with closes, short cul-de-sacs off the
  branch streets.
- **Phase B, item 2: farmsteads (after v3.48).** There are about 380 farms out in the farmland, clear of the
  settlements and the airfields and at least 280 m apart (`addFarmsteads`, `community_layout`).
  - Each is a near-level yard of gravel, concrete and mud (class RC_YARD) at the end of a gravel track (RC_TRACK) of
    up to 1.3 km.
  - The track leaves the nearest road, lane or neighbouring farm's track square to it, crosses nothing, and keeps
    62 m clear of every other farm.
  - The yard and track are network paths, graded and banked like the streets and cached with the world, so
    `World::farms` is read back off the network.
  - Round each yard stand a farmhouse by the way in, a barn across the far end with its silos, a machine shed, often
    a second barn, and a lorry or a car. All stand clear of every road, with no tree or bush in the compound.
  - The fields between are hedged: trees and bushes stand along the borders of the terrain's field patchwork
    (`fieldPatch`, the CPU mirror of the shader's), with a few lone trees in the pastures and none in the crops.
- **Phase A, item 6: road furniture (after v3.48, `road_furniture.cpp`, `roads_test`).** It is laid along the
  network when the world is built or loaded, and each scenery chunk takes its own share:
  - About 23,500 street lamps. On every street they stand on the pavement where the terrain shader already pooled
    their light: 30 m apart, side to side, with the street trees kept off them. They also line the highways' verges
    through and near the towns and cities, and the roads through each settlement. Their lanterns light at night.
  - About 8,400 lengths of steel W-beam guardrail. They run down every highway's central reserve, with an opening
    wherever another road joins. They also run along any road standing 2.5 m or more over its fill, and for 30 m onto
    each bridge, up to the deck's joint. Each 16 m length is sheared to the road's grade in the vertex shader.
  - About 2,300 wooden poles along the country roads and lanes, one side, each carrying three wires to the next. The
    next pole is the furthest one, 15 to 55 m on, whose wires stay within 2 m of the roadside. The wires follow the
    grade in the vertex shader, are drawn only within 160 m, and cast no shadow.
  - Direction signs where a road or lane joins another out of town (green near a highway, blue elsewhere), and a
    settlement's name sign where a road comes into it.
  - None stands on another road's platform, on a bridge, or by an airfield. `entity_kind_numbers` holds the shaders'
    kind numbers to the enum.
- **A limit to watch.** The road grid lists each segment under every 39 m texel its banks reach, and those lists are
  addressed with 19 bits. They now hold 490k of 524k entries, so tracks and yards reach only 12 m beyond their
  platforms (streets 20 m, other roads 50 m). `roads_test` fails if any segment goes unlisted or the lists come
  within a texel's worth of the limit. New roads (Phase C's access roads) need that headroom, or a wider packing.
- **Phase A is done.** Phase C's hand-built landmarks are under way, then life on the roads (Phase D).
- **Phase C, the wind farm (after v3.48, `landmarks.cpp`, `landmarks_test`).** Ten 3 MW turbines stand 300 m apart
  along the crest of a ridge (the best one on the mild green hills, 80 to 700 m up). They are out of the settlements,
  off the roads, and 6 km or more from every runway. Each is a 90 m tower with its nacelle and three 45 m blades. The
  rotor turns 14 times a minute once the wind is over 2.5 m/s and yaws to face the wind (`ent_vs.glsl`, both passes),
  and a red aviation light flashes on each nacelle at night. The landmarks are found when the world is built or loaded,
  on the base height map, and handed to the scenery chunks like the road furniture (`PlacedByChunk`).
- **Phase C: the stone circle, the wreck and the platform (after v3.48).** Each is found by its own search
  (`landmarks.cpp`) and checked by `landmarks_test`:
  - The stone circle stands on the most prominent bare summit, out of the settlements and off the roads: fourteen
    standing stones 3 to 4 m tall in a ring 26 m across, two more fallen beside their places, and a flat stone at the
    centre. Its clearing is kept free of trees, bushes and boulders. The stones are rock entities, given to their chunk
    with its other rocks at the finer detail level, so trimming the scenery back doesn't lose them.
  - The wreck is a 72 m coaster aground on a reef in warm, clear shallows (the floor 1.5 to 4 m down, open water round
    her). She lists 12 degrees and sits down by the stern, rust through her black paint, her holds open, weed below the
    tide line.
  - The offshore platform stands in deep water 6 to 12 km from the nearest land. It has a yellow four-legged jacket,
    two decks of modules, the quarters (lit windows at night) under a marked helideck, a drilling derrick and a crane.
    Its flare burns at the end of its boom, flickering, and a red supply vessel lies off its west face.
- **Phase C: the observatory, the star fort and Lighthouse Key (after v3.48).**
  - Three telescope domes stand along Mount Kaleo's crater rim at about 2,010 m, where the rim is highest, 2.5 km and
    more from the crater field's runway. Each is a white drum under a silver dome with its slit open, beside its
    control building.
  - The star fort stands on the cape most surrounded by the sea, level and dry, 124 m across. It has five bastions and
    their curtains, stone ramparts 7 m high with a parapet and a grassed top, a gatehouse, two whitewashed barracks
    under slate, a magazine, and its flag.
  - Lighthouse Key now has its keepers' two cottages behind the light, and a timber landing stage on the nearest shore
    with a fishing boat moored alongside.
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
