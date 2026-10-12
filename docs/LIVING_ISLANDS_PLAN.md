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
  - a highway's is a steel Warren truss over two spans, continuous over a pier between them (160 m, 11 m high) -
    and where the crossing is 400 m and more, the strait bridge (Phase C, below);
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
  addressed with 19 bits. They now hold 492k of 524k entries, so tracks, yards and the pit lane reach only 12 m
  beyond their platforms (streets and the racetrack's circuit 20 m, other roads 50 m). `roads_test` fails if any
  segment goes unlisted or the lists come within a texel's worth of the limit. New roads (Phase C's access roads)
  need that headroom, or a wider packing. `road_class_numbers` holds roads.glsl's widths and reaches to roadSpec's.
- **Phases A to D are done:** the road network, the settlements, Phase C's twelve landmarks, and Phase D's traffic on
  the roads, the towns' lights at dusk, ferries between the islands and yachts in the harbours. Phase E, the visual
  pass, has had its first round (below).
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
- **Phase C: the cities' landmarks (after v3.48).** Each is placed with the cities' lots (`sceneryBakeCommunityLots`), on
  ground no road crosses (checked on a fine grid) and claimed before any lot:
  - Solace Capital's stadium stands on the flattest dry ground at the city's edge that it fits, beside the highway. It
    is an oval bowl 184 x 148 m: a striped, lined pitch, two tiers of blue seats (a white band, people here and there),
    the concourse wall with its fins, and the roof ring with its floodlights. After dark the pitch and stands glow as
    if floodlit.
  - Port Verde's container terminal stands 4 km from the city, which is on a hill: its quay is reclaimed from the
    shallows on the nearest straight 430 m of shore with 8 m of water at the berth. It has a concrete apron 5 m up with
    its rails and bays marked, fenders, bollards and ladders down the wall. Four ship-to-shore gantry cranes stand on
    it, blue and red, booms out over the water. Behind them are blocks of containers stacked one to four high, each
    its own line's colour, and a 230 m container ship lies alongside, loaded.
- **Phase C: the racetrack (after v3.48, `addRacetrack`, `landmarks_test`).** A 3.9 km circuit on Kaleo's eastern
  plain, 6 km from Kailani: the level ground it needs, found on the base map (every 300 m and 15 degrees) and then
  laid on the ground, out of the settlements, 3 km from the runways, and clear of every road.
  - The circuit and its pit lane are the network's own roads (classes `RC_CIRCUIT` and `RC_PITLANE`), graded and
    cached with it, so farms and lots keep off them, and a road runs from the paddock to the nearest road.
  - The layout: a 1 km main straight, turn 1 (a right-hander), a kink, the back straight, a hairpin, a long left, and
    two right-handers back onto the straight.
  - The terrain paints a 15 m dark track with white edges. Round every bend it lays red-and-white kerbs and, on the
    outside, a gravel trap; the shader reads the bend's curvature from the circuit's own segments. The run-off is
    mown grass, and the main straight carries a chequered start line and the grid.
  - Along the main straight stand four blocks of eight pit garages (`EK_PITS`), their bays lit in the teams' colours
    on the boxes painted in the pit lane, and the pit wall (`EK_PITWALL`) with its crews' stands and debris fence.
  - Across the track stand three grandstands (`EK_GRANDSTAND`) behind a catch fence, and a fourth faces turn 1.
  - The teams' transporters (`EK_TRANSPORTER`) are in the paddock, a car park of 500 cars is behind the stands, and
    guardrails line the outside of every bend.
  - Its grounds are meadow with lone trees: no wood, no crops, nothing on the run-off.
- **Phase C: the Fjordhaven dam (after v3.48, `findReservoir`, `landmarks_test`).** A concrete arch dam across a gorge
  in the mountains 4 km north-west of Fjordhaven. It is 80 m high on a 370 m chord, and holds a lake of 0.28 km² at
  531 m.
  - The gorge is searched for within 14 km of the town, every 50 m and 15 degrees. Its walls must rise 60 to 80 m
    within a 400 m chord, its floor rise gently upstream and fall away below. The lake is flood-filled to the crest
    less its freeboard and must spill nowhere and drown no road, settlement or runway. The largest lake wins.
  - The lake is built into the ground as the roads are (`World::lakeGround`, roads.glsl `lakeGround`). An 8 m mask
    over its square, made from the natural ground whenever the world is built or loaded, lifts the ground to the
    water's level wherever the water lies.
  - Its shore is therefore the ground's own contour at any distance, and an aircraft meets the water as it meets
    the sea. The terrain paints it deep green and rippling, with a wet margin, judged per pixel on the natural ground.
  - The dam (`EK_DAM`): its arch about a centre downstream, thick at its foot and thin at its crest, carried into the
    rock past its abutments. A road runs along its crest between parapets, with five spillway gates and their gantry
    at its crown, and a powerhouse at its toe with its penstocks down the face. Nothing grows in the lake.
- **Phase C: the strait bridge (after v3.48, `bridges.h` `BM_STAYED`, `bridges_test`).** The network gives no highway
  a crossing of the bay by Port Verde, so the strait bridge is the highway's longest crossing of the water: 498 m over
  the strait on the highway from Solace Capital to Northpoint, 9 km short of Northpoint.
  - It is cable-stayed: a highway crossing of 400 m and more takes a main span of half its length (200 to 300 m; here
    249 m) over its deepest water.
  - The span hangs between two concrete H-pylons rising 55 m over the road (0.22 of the span). Each pylon stands on a
    widened main pier, its legs past the deck's edges and leaning in a little, with a beam across high over the
    traffic and another at their heads.
  - From each leg a plane of white-sheathed stays fans out either way, a semi-fan of ten, every 12 m along the deck.
    The outermost are anchored highest, and the two fans meet at the span's middle.
  - The pylons and stays are bridge members, drawn and solid like the truss's and the arch's; the road between the
    legs stays clear.
- **Phase D: traffic (after v3.48, `road_traffic.h`, `road_traffic_test`).** Cars and lorries drive the highways,
  roads, lanes and streets within 2 km of the camera: up to 1,200 of them, 20 a kilometre on a highway, 14 on a
  street, 10 on a road, 2 on a lane.
  - Each keeps to its lane on the right of its road and on its surface, bridges included. A highway has two lanes
    each way: the fast at 110 km/h and the slow at 86, where the lorries keep. A road runs at 80, a lane at 47 and a
    street at 36.
  - Every car in a lane holds that lane's pace and keeps 10 m behind the one ahead. At its road's end it carries on
    along a road that meets it or one it ends on, waiting its turn while that way is taken, and turns back at a dead
    end.
  - Cars appear and leave out of sight, beyond the 1.6 km they are drawn to (and the ring fills afresh after a jump).
    They are drawn with the scenery's own cars and the transporter as a lorry, batched into the same instanced draws,
    and kept out of the cached shadow cascades.
  - At night their headlights burn white and their tail lights red, with a glint each at range, as the runway lights
    have.
- **Phase D: the lights at dusk.** The towns light up as the dark falls rather than fading up all at once. Each
  window comes on at its own moment, a few at first dusk and every one by full dark, at close to its full brightness
  once on. Each street lamp comes on with its photocell within the first of the dusk, as does the pool of light under
  it. Around 18:00 the street lamps and the first windows are lit; by 18:15, most of the town.
- **Phase D: ferries and yachts (after v3.48, `landmarks.cpp`, `landmarks_test`).** Five ferries join the islands into
  one network: Mill Creek to Coral Cove (9 km), Port Verde to Lighthouse Key, Palm Bay to Turtle Bay, Solace Capital
  to Mango Grove and Fjordhaven to Ember Bay (15 to 19 km).
  - Each coastal town's harbour is the nearest water 6 m deep, clear of the container terminal. Its ferry terminal
    runs from 12 m inside the nearest shore straight out past the harbour: a causeway (`EK_FERRYPIER`) in as few 40 m
    lengths as leave the berth beyond its linkspan deep enough for her (4.5 m under her middle, 2.5 m under her ends
    and sides), with a straight way out from it, 250 m over 4 m and more. A town whose shore allows none within eight
    lengths has no ferry.
  - The causeway has a 14 m roadway 3 m over the sea, lanes marked, a concrete coping, lamp posts and bollards, and
    armour stone sloping into the sea. At its head stand a quay wall and the linkspan (`EK_LINKSPAN`): a steel ramp
    down to her car deck under its lifting gantry, in the line's blue, with a fendered mooring dolphin either side of
    the berth. Nothing is built on the causeway, and no yacht moors by it.
  - Between each two islands the nearest pair of terminals, 32 km apart at most, is a candidate route, and as few as
    join every island are kept, the shortest first.
  - Each route runs from her berth straight out to sea, across, and in to the other berth, so she lies end-on to each
    linkspan. The crossing is found by sea: A* over 200 m cells of water 6 m deep (dearer under 15 m), then drawn taut,
    every leg straight as long as the water under it stays 3 m deep. No route passes under a bridge too low for her.
  - Each ferry (`EK_FERRY`) is a double-ended 100 m ro-pax ferry: white over navy, two glazed saloon decks, a wheelhouse
    and car-deck doors at each end, a funnel either side, lifeboats. She runs her route at 9 m/s (17 knots), waits 150 s
    at each end and comes back stern first, never turning round. Her place is a matter of the time alone, from her own
    timetable, so she needs no state.
  - At night her saloons are lit, and her navigation lights show at range: two masthead lights, red and green
    sidelights at the end she is heading for, and a stern light at the other.
  - The yachts (`EK_BOAT`): ten-metre sloops at their moorings off every coastal town, eight off a village, fourteen off
    a town and twenty-four off a city, 150 in all. They lie in 1.2 to 7 m of water within 70 m of the shore and 2.5 km
    of the town, 16 m apart, clear of the container terminal's quay, the ferries' berths and the runways, all lying to the same
    breeze, clear of the ferries' terminals. Each shows an anchor light at her masthead at night.
- **The v3.48.0 diagnostics (RTX 3070 Laptop, 1080p).** Every scene holds 60 fps. Against v3.43 most scenes are 1 to
  2 ms dearer in the world pass, the price of the roads, towns, landmarks and the deeper view. The night scene, on
  Cedar Ridge's runway, went from 9.7 to 15.5 ms, and its terrain materials from 1.9 to 6.0 ms.
  - The cost is the view, not the night: it is the only diagnostic scene on the ground, looking along it.
  - There, every ground pixel reads the finest levels of the photographed 2K materials (v3.44), anisotropically, and
    they were uploaded uncompressed (RGBA8, 300 MiB with their mipmaps). The crash scene, also on the ground, grew
    most after it (9.6 to 12.6 ms).
  - The 2K set is now block-compressed (BC3, `texture_compress.h`, encoded on all cores at launch: about 0.4 s), a
    quarter of the bytes and of the texture traffic. It falls back to RGBA8 where the driver lacks S3TC.
  - The image is unchanged: 41.7 dB over a runway frame, no difference visible up close.
  - `startup.log` names which set loaded, and the analysis gains a `2K ground photographs (512)` row, the cost of the
    set against the 512 one, for the next diagnostics to confirm.
  - **The v3.49.0 diagnostics proved that wrong:** the night scene was unchanged (15.5 ms), and the 512 set saved
    nothing there. The compression stays (a quarter of the memory), but the cause was elsewhere.
  - **The cause: the terrain was drawn far to near.** Its chunks went to the GPU in the quadtree's order, the islands'
    copies from -K to K, so the far copies came first. Since v3.47 the view reaches the horizon, and from the ground
    every copy out to it lies in the same few rows above the horizon line: each was shaded in full - its farmland's
    field patchwork the dearest part - then hidden by the nearer ground.
  - The chunks are now drawn nearest first, so the depth test turns away what a nearer chunk hides before its
    material is worked out. On the software renderer the night scene's world pass falls from 2,149 to 1,317 ms (its
    terrain material from about 1,150 to 400 ms, under v3.43's 650), and the cockpit scene's from 719 to 647 ms. The
    airborne scenes are unchanged, and the picture is the same to the level.
  - The analysis also gains a row for each part of the ground's material (airfield surfaces, roads, the towns'
    ground and fields, the detail layers) and for anisotropic filtering, for the next diagnostics.
- **The v3.49.1 diagnostics:** the night scene's frame fell from 15.5 to 13.9 ms, less than the software renderer
  promised. Its new rows put the roads first among the ground's costs (3 to 5 ms) - wrongly.
  - Those rows' switches shared bits with the analysis's cost probes (renderer.h `kProbe*`, 1 << 12 to 1 << 15, which
    leave whole draws out): "roads on the ground" (4096) also left out the scenery, and matched the "scenery" probe to
    a few tenths of a millisecond in every scene; "towns' ground and fields" (8192) the terrain mesh; "ground detail
    layers" (16384) the airframe march. The switches now sit above the probes (`kDbgRoads`, `kDbgTowns`, `kDbgDetail`,
    `kDbgRoadPaint`, `kDbgAniso`), and `terrain_material_test` fails any shader mask that shares a probe's bit.
  - What the rows measured truly, in the ground and low scenes: the buildings and trees 2.5 to 3.7 ms, the terrain
    materials 2 to 4 ms, anisotropic filtering 1.2 to 1.8 ms, the airfield surfaces about 1 ms.
  - The roads were made cheaper on the strength of the wrong row, and stay so. Every ground pixel in a mask texel
    (39 m) that any road's banks reached looked through all of that texel's entries twice: once for the road's own
    surface normal (`terrain_fs`), once for its paint (`roadAt`) - 7.6 entries on average, 15 reads. Now one pass
    (`roads.glsl` `roadQuery`, compiled only into the terrain's and the GPS map's programs: `GROUND_ROADS`) gives
    both, reading only the entries whose platform comes within 1.5 m of the texel - 3.0 on average, none in 37% of
    texels: `buildRoadGrid` lists those first, and `roadEntryRows` marks the first of the rest (128 in the code, the
    path moved to 256 x) so the loop stops there. `roadGrade`, under the terrain's vertices, still reads them all.
    On the software renderer over a town, the scenery left out (four runs of the old build, three of the new), the
    roads' cost fell from 159 to 93 ms of the world pass looking across the town and from 280 to 185 ms looking down
    on it: the pass about 5% quicker.
  - The junction test (where the markings stop) is exact now: a second road's platform within a metre. The old one
    also stopped them where another road came first in the texel's list - 8.5% of the paving's places, nearly all of
    them streets and farm tracks, which have no markings.
  - `road_grade_gpu` checks the shader's road against every entry of its texel at 590,000 places: on the same road,
    across it to a centimetre, the same junctions.
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

- **Strait bridge**: a cable-stayed bridge carrying the coast highway across the south bay by Port Verde. (Built on
  the highway's longest crossing of the water instead: no highway crosses that bay.)
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

Done so far (after v3.48):

- **The sea stacks** stood anywhere in 1 to 18 m of water: 266 of them, 21 off a steep shore. A dozen dark pillars
  were scattered across the open bay south of Port Verde. Now a stack stands only off a cliffed shore
  (`seaStackShore`): land within 220 m rising 14 m within 40 m of the water's edge, or 26 m within 100 m - a
  headland, a cliff's foot. There are 126 of them, in small clusters at the foot of the steep coasts. `landmarks_test`
  checks the bay is clear and every stack is off a cliff.

- **The airfields' grounds**, beyond the mown margins of their strips, were one flat, even green from low level. Now
  they are cut in broad 16 m bands along the runway, a lighter swath and a darker one, with the longer grass of the
  rough left in olive patches between them. The pattern fades out well before the airfield's edge and keeps off its
  roads and buildings.
- **On a low airfield** (Palm Bay's, 4 m up, beside the beach), the beach sand's weight lay faintly over all of its
  flattened ground and read as a veil of haze. On flattened ground the sand now lies in patches with ragged edges,
  sand or grass.

- **Looked at and left as they are:** the haze from altitude, which is as thick as the weather's visibility makes it
  (40 km: 60% of the way to the horizon's colour at 10 km); the forest along the coasts; the water along the beaches
  and the yachts on it; the clouds from under their base and from above them; the cities from 40 m over their streets;
  the farmland's patchwork from 800 m, pale where the wheat is ripe; Kailani's lights at dusk.
- All of it rendered with the photographed materials (the render harness's `MATDIR` pointed at `assets/materials`:
  without it the harness draws the procedural set).

## Contracts kept throughout

- Every runway, its markings, lights and approach surfaces exactly as they are (`runway_preservation_test`).
- Collision follows what is drawn: buildings, bridges and terrain edits are solid.
- The world cache invalidates itself when the generator changes; the first launch after an update builds it once.
- Frame time measured on the owner's GPU every phase; nothing ships that takes a scene below 60 fps at 1080p.
