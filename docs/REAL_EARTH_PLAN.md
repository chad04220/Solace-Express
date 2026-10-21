# The real Earth: continental plan (option C)

The owner chose the full real Earth on 2026-10-20, after the feasibility study:

> "lets go for a full C implementation"

That means real terrain, coastlines, airports, roads, towns and land cover everywhere. A flight can go anywhere, over
land or sea, without a break. The Solace Islands stay authored, at their place in the real Atlantic (32.5° N, 58.0° W).

This document is the design, the data, the stages and the contracts. Claude's task list tracks the stages (#274 on).
The history before it (Codex's Phase 0 tools, the procedural regions) is summarised in section 9.

---

## 1. The idea: real data, sliding charts

The engine's world unit stays what it is today: a **chart**.
- A chart is a `World`: an 80 km square, 2048² terrain texels of 39 m, its own roads, settlements, airports, scenery
  mask and envelope.
- It is laid out on an azimuthal equidistant projection about the chart's origin (`regionTravel::Chart`): x east,
  z south, y altitude.
- Physics, the renderer, the scenery streaming, collision, the GPS and ATC all keep working inside one chart, as they
  do now.

The change is where a chart's content comes from. **Everything in a real-Earth chart is a pure function of
geography**, never of the chart:
- heights from the elevation data;
- coasts and lakes from the data;
- the land cover;
- airports, towns and roads from their tables, by real position;
- the procedural detail seeded by geographic position, not by chart coordinates.

So two charts that overlap agree wherever they both reach. That is what lets a flight **slide** from one chart to the
next anywhere, over land too. Today's handoffs need open ocean, because each generated region is its own invented world.

```
offline (tools/earth, Python + NumPy, in CI)                 at run time (src/earth_*.cpp)
  ETOPO 2022 30" ─► elev bundles (1° tiles, int16) ─┐        pack reader (bundles, inflate, LRU tile cache)
  CGLS-LC100 ─────► cover bundles (1° tiles, uint8) ├─ pack ─►  height(lat, lon) / cover(lat, lon) / airports /
  OurAirports ────► airports.bin                    │          places / roads by area
  GeoNames ───────► places.bin                      │        GLO-90 fetcher (AWS open data, cached COGs)
  GRIP4 ──────────► roads bundles (edges, nodes)   ─┘        World::buildEarth(chart) ── the usual World pipeline
                                                              far terrain from the pack ── the horizon to orbit
                                                              sliding charts ── background build, prewarm, swap
```

---

## 2. The data

| Layer | Source | Licence | Resolution in game | Where it lives |
|---|---|---|---|---|
| Base elevation, land and sea floor | NOAA ETOPO 2022 30″ surface | CC0 | 30″ (≈ 0.9 km), 1° tiles | the base pack |
| Fine elevation, land | Copernicus DEM GLO-90 (3″) | free, any use, with attribution | 3″ (≈ 90 m) | fetched on demand from the AWS open data bucket (`copernicus-dem-90m`), cached |
| Land cover | Copernicus Global Land Service LC100 2019 (discrete map) | CC BY 4.0 | 1/336° (≈ 330 m) by majority, 1° tiles | the base pack |
| Airports, runways | OurAirports | public domain | points, runway ends | the base pack |
| Towns and cities | GeoNames `cities500` | CC BY 4.0 | points, population | the base pack |
| Roads | GRIP4 (GLOBIO) | CC BY 4.0 working assumption (see `EARTH_DATA_SOURCES.md`) | polylines, simplified to 15-30 m | the base pack |

Notes:
- **Measured sizes (feasibility study).**
  - ETOPO packed: about 1.4 GB at 60″; about 0.44 GB with the sea floor clamped to −50 m; about 1.6 GB at 30″.
  - The game clamps the sea floor to −200 m. The sea is opaque, and that keeps the shelves for the water's colour.
  - GLO-90 source tiles total 66 GiB. A ready-made global GLO-90 pack would be about 15 GB, which is why it streams
    instead: a 1° tile is 2.6 MB on average.
- **Copernicus DEM tiles** are Cloud Optimized GeoTIFFs: Float32, Deflate, floating-point predictor (3), one internal
  tile, with overviews. The game reads them with its own small TIFF reader and stb_image's inflate. Ocean has no tile.
  The base pack carries the bitmap of which 1° cells exist (26,475 of them).
- **CGLS-LC100** is one 362,880 × 141,120 uint8 BigTIFF (80° N to 60° S, 1/1008°), Deflate, in 256² tiles with the index
  at the end. Ranged requests pull a region's tiles for development. CI downloads the whole 1.7 GB file to build the
  global layer. Classes:
  - 20 shrubs, 30 herbaceous, 40 cropland, 50 built-up, 60 bare;
  - 70 snow and ice, 80 permanent water, 90 wetland, 100 moss and lichen;
  - 111-116 closed forest, 121-126 open forest (evergreen needle, evergreen broad, deciduous needle, deciduous broad,
    mixed, unknown);
  - 200 open sea.
- **No raw dataset is committed.** Every tool records its sources: URL, version, size and SHA-256.
- **Attribution** goes in `THIRD_PARTY_NOTICES.md`, the credits screen and `licenses/` inside the pack:
  - "© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union
    and ESA; all rights reserved" for the DEM;
  - "Copernicus Global Land Service, LC100 v3.0.1 2019, CC BY 4.0";
  - GeoNames (CC BY 4.0);
  - GRIP4 (the citation in `EARTH_DATA_SOURCES.md`);
  - NOAA NCEI ETOPO 2022 (CC0);
  - OurAirports.

### 2.1 The pack format (version 2)

The pack is a directory, `earth/`, with `manifest.json` and one file per layer per 10° × 10° block. For example,
`elev/N40W080.svb` covers 40° N to 50° N and 80° W to 70° W.

A bundle file has a fixed layout:
- a 64-byte header:
  - the magic `SVB2`;
  - the layer;
  - the tile's samples per degree and its apron;
  - the block's south-west corner;
- an index of 100 entries, one per 1° tile: offset, compressed size, raw size, CRC-32, and flags (absent, constant,
  stored);
- the tiles' zlib streams.

A tile is a square grid of cell-centred samples over its 1°, plus a one-sample apron that copies its neighbours, so
sampling never crosses a tile:
- **Elevation:** int16 metres, with row deltas before deflate. 120 + 2 samples a side.
- **Cover:** uint8 classes. 336 + 2 samples a side.
- **Constant tiles** (all ocean, all one class) store one value and no stream.

Airports, places and roads are each a sorted binary table with a 1° cell index:
- `airports.bin` holds, per airport, its ident, name, type, position, elevation, and its runways (both ends, length,
  width, surface, lighting).
- `places.bin` holds name, position, population and feature code.
- `roads/*.svb` hold, per 1° tile, **edges between global nodes**:
  - each edge is a whole polyline from junction to junction, its points quantised to 1/2²⁰ of a degree;
  - an edge is filed in every tile its bounding box touches, so a chart reads each edge whole;
  - node ids are global, so a junction is one node in every chart.

The pack is built by `python3 -m tools.earth2 build --box SOUTH WEST NORTH EAST ...`: a box for development, or the
globe in CI. The test fixture comes from `python3 -m tools.earth2.fixture`, and `python3 -m tools.earth2 verify PACK`
checks a pack. The same inputs and tool version give byte-identical bundles. The game verifies the CRC of every tile it
reads, and the manifest's SHA-256 of every bundle when it opens the pack.

---

## 3. Charts on the real Earth

### 3.1 Placement

- A chart's origin snaps to a lattice of 1/32° in latitude and 1/32° in longitude (about 3.5 km). The same start or
  route gives the same charts, so the disk cache can hold them.
- Its id is `earth:<lat index>:<lon index>`.
- Free Flight starts a chart on the lattice point nearest the chosen airport.
- In flight, the next chart is centred about 20 km ahead of the aircraft along its track (section 5).

### 3.2 `World::buildEarth(origin)`

The usual `buildConfigured` pipeline, with Earth inputs:

1. **Terrain** (`earth::sampleChart`).
   - Every texel's geographic position comes from the chart (in double).
   - Its height is bicubic from GLO-90 where that is loaded, otherwise from ETOPO.
   - **Geographic detail:** octaves of 3D noise on the sphere, in double, evaluated at the texel's position and added
     to the base. Their amplitude comes from the source's resolution and the local relief: a few metres on GLO-90,
     tens of metres on ETOPO-only mountains. The shader's chart-local `terrainFbm` keeps only a metre or two of
     sub-texel roughness, so a chart swap moves the ground by centimetres, not metres.
   - **No grid smoothing.** The 1-2-1 filter depends on the grid; bicubic sampling removes the creases it was there for.
   - **Canopy and roofs.** GLO-90 is a surface model: it includes tree canopy and buildings. The base takes off an
     estimate where the cover says forest (closed about 12 m, open about 5 m) or built-up (about 4 m), blurred across
     the class edges, so the scenery's own trees and houses stand on the ground rather than on top of a canopy.
2. **Water.**
   - The sea is the cover's class 200, plus the elevation's ≤ 0 m that touches it.
   - The sea floor is ETOPO's, clamped to −200 m and shaped up to the coast.
   - Land below sea level that the sea doesn't reach (the Netherlands' polders, the Dead Sea, Death Valley) stays
     land.
   - Lakes are class 80. Each lake's level is the median DEM height over its connected cells (the Copernicus DEM's
     water surfaces are flattened).
   - `LakeMask` and `World::lakeGround` generalise from Fjordhaven's one reservoir to a chart-wide mask with a level
     per texel. The terrain paints water there as it does for the reservoir.
3. **Land cover into the ground.**
   - Lushness comes from the class: forest and crops high, shrubs mid, bare low.
   - Coldness comes from latitude, height and the snow class.
   - The mask's farmland channel is cropland. Its urbanness is built-up, together with the towns below.
   - The forest density follows closed and open forest.
   - Tree species follow the forest type: needleleaf to firs, spruce and pines; broadleaf to oaks and birches;
     tropical evergreen broadleaf to palms.
4. **Airports.**
   - OurAirports' large, medium and small airports in the square, open, with a runway at least 300 m long.
   - The main runway is the longest one with a hard surface, else the longest. It is placed from its two ends:
     heading, length, width, surface and threshold elevation.
   - Size: large → international (2), medium → regional (1), small → strip (0).
   - The grounds are flattened to the runway's own height and blended into the terrain, as Solace's are
     (`airportGroundsCap`, `airportFunnelCeiling`).
   - The layout comes from `airport_layout.cpp`.
   - Codes are the real idents (KBOS, EGLL). Names are the real names.
   - Secondary runways come in stage E8.
5. **Roads.**
   - Every GRIP edge the square reaches, whole, with these classes:
     - 1 highway → `RC_HIGHWAY`;
     - 2 primary → `RC_ROAD`;
     - 3 secondary → `RC_ROAD`;
     - 4 tertiary → `RC_LANE`;
     - 5 local → `RC_TRACK`, or none inside a town, where the settlement's own streets take over.
   - Each edge is laid with `layRoad`. Its ends are pinned at the node heights, which are the ground's height at the
     node, the same in every chart.
   - Edges that leave the square are cut 2 km inside it. The swap distance (section 5) keeps every cut far from the
     aircraft.
   - Real alignments are never re-routed. Where a real road is steeper than its class allows, its profile follows the
     ground; that is an exception, not a failure.
6. **Towns.**
   - GeoNames places in the square:
     - village under 5,000 people;
     - town under 100,000;
     - city above that.
   - The radius comes from the population, shaped by the built-up cover.
   - Then `buildSettlementFields` and the settlements' own streets, as today, and farmsteads where the cover is
     cropland.
7. **The rest unchanged:**
   - road grid;
   - mask bake;
   - height bounds;
   - envelope;
   - bridges;
   - road furniture.

   No invented landmarks.

The cache key is the chart id, the pack's content hash, the GLO-90 tiles used and the build stamp. Budget: a chart builds
in under 10 s on two background threads (a generated region takes about 6 s today).

---

## 4. The far terrain

Beyond the chart, out to the horizon, everything is drawn from the pack and never from a `World`:
- patches of 129² vertices in rings of growing size round the camera: 40 km patches to 150 km, 160 km patches to 600 km,
  640 km patches beyond that, out to the horizon from orbit;
- each patch is sampled from ETOPO (and GLO-90 where it is cached), coloured from the land cover in the same palette
  as the near ground's material, with the sea clipped as today;
- the existing far pass (`far_region_vs.glsl`, the dithered near/far blend, the coarse collision fallback in
  `regional_surface`) carries them.

This replaces `distant_regions`' silhouettes of the generated regions in a real-Earth flight.

---

## 5. Sliding charts

- **Prepare.**
  - While flying, the next chart is built in the background, centred 20 km ahead along the track and snapped to the
    lattice.
  - Then come its GPU upload, staged at 2 MiB a frame as today, and **its scenery near the swap point**: the chunks the
    current chart has loaded within 10 km of where the swap will be, generated from the new chart.
- **Swap.**
  - The swap happens once the aircraft is nearer the new chart's centre than the old one's by 2 km.
  - Over land too. It needs no ocean and no altitude.
  - When the aircraft is within 30 m of the ground, the swap keeps its height above the ground rather than its
    altitude, so a few centimetres of resampling never become a bump.
  - The whole flight state carries over through `regionTravel::Change`, as today.
- **Seamless.**
  - Scenery is placed by geographic position (section 3), so the prewarmed chunks hold the same trees, houses and
    lamps where the old ones stood.
  - The scenery table swaps whole, with no drain-and-restream.
  - Traffic, ATC and the route carry across by real airport ident, not by index.
- **Late detail.** When the next chart isn't ready, the far terrain's paired collision surface carries the flight,
  as today.

---

## 6. Stages

Each stage builds and passes the full `ctest` and the MinGW build, adds its own tests, and is pushed on its own.

| Stage | What | Shows |
|---|---|---|
| **E1** data | `tools/earth` v2 builders, dev pack (New England, 40-45° N, 75-69° W), test fixture (Martha's Vineyard and Nantucket, under 3 MB) | a pack that verifies; Python tests |
| **E1b** reader | `src/earth_data.*`: bundles, inflate, LRU cache, sampling, tables by area, the GLO-90 COG reader; C++ against Python parity tests | `earth_data_test` |
| **E2** charts | `World::buildEarth`; Free Flight "Real Earth" from any airport in the pack (search by ident or name) | flying out of KBOS over real ground |
| **E3** far terrain | section 4 | real coasts and mountains to the horizon |
| **E4** sliding charts | section 5 | Boston to Maine without a stop |
| **E5** global data | a CI workflow that builds the global pack and attaches it to a release; the game finds and installs it; the GLO-90 download cache with offline fallback | anywhere on Earth |
| **E6** Solace in the Atlantic | the authored chart in the real ocean, its sea floor blended into ETOPO's, Bermuda and the US coast reachable; the generated regions leave real-Earth play | Solace to Bermuda to New York |
| **E7** career | real airports' boards, travel and long hauls between them, a spatial index for ATC, the GPS and the board; the career keeps Solace as its start | jobs anywhere |
| **E8** realism | secondary runways, airport layouts by size, rivers, regional building styles, night lights by population, landmarks | polish |

### 6.1 Where it stands

- **E1, E1b:** done. `tools/earth2` builds the dev pack and the fixture; `src/earth_data.*` reads them, with parity
  tests against the Python sampler (`earth_data`, `earth2_pack`).
- **E2:** the first part is in. These pieces work:
  - `World::buildEarth` (`src/world_earth.cpp`) and the lattice and names (`src/earth_charts.*`).
  - The Free Flight "Real Earth" picker, which searches by ident, name or town.
  - `earth_chart_test`.
  - The fixture's chart (Martha's Vineyard) builds in about 8 s; Boston's in about 34 s.

  Changes from section 3.2 so far:
  - The chart-local `terrainFbm` is scaled down (`World::detailScale`, 0.2 on Earth). Its amplitude comes from the
    slope and the source's resolution. Geographic noise on the sphere waits for E4, where charts must agree.
  - The sea also takes class 80 at sea level (salt marsh and estuaries the cover calls water), and the lakes at the
    sea's level beside it.
  - Lakes come from the DEM, not only the cover. The Copernicus DEM flattens its lakes (Wachusett Reservoir reads
    115.0 m across, Quabbin 158.0 m). A lake is a stretch of the cover's water holding at least a dozen texels within
    10 cm of each other:
    - its bed lies 1.5 m under its level, flat;
    - the land within 6 texels of it is held 1.5 m over it, so a wooded shore with its canopy taken off doesn't sink
      under the water;
    - `World::lakes` holds each texel's level code, beside the forest noise in the road grid's second channel;
    - the ground is never under the level there, water is painted where the natural ground lies under it, and roads
      bridge it.

    Water the DEM doesn't hold level stays land. That covers small ponds, unflattened rivers, and every lake where only
    ETOPO is loaded. Boston's chart finds 75 lakes (38 over 30 ha). The fixture's crop now reaches 41.68° N, so the
    test chart holds the upper Cape's ponds.
  - GRIP edges are cut 1.5 km inside the square. Local roads (GRIP class 5) come from a separate `lroads` layer.
  - Towns: 24 places, merged where they overlap, ranked by population over 1 + (distance from the chart's centre /
    15 km)². Cities count from anywhere in the square, and the villages by the start come before small towns at the
    edge.
  - The road grid's GPU data is an index list plus a segment table: 40 MB for Boston, down from 136 MB. The forest
    noise moved to the road grid's second channel, so a texel's first entry has 26 bits.

  - The ground material's sand keeps to the beaches on the real Earth: land under 3.5 m within two texels of the
    sea. On Solace it covers everything up to 4.5 m, which on a real coast would sand over the marshes, the harbour
    landfill and Logan.

  Seen in renders: Boston from the harbour, with Logan on its peninsula, the harbour islands and the downtown towers.
  Between the towns' blobs lies open grass, though, where the real metro is continuous: the settlements are circles
  from the population, at most 1.8 km across, not yet shaped by the built-up cover. That is an E8 item.

  Still open in E2:
  - Boston's build time against the 10 s budget: about 25 s, down from 34. The road grid and the community lots now
    run in parallel. What remains is mostly the settlements' streets (~5 s) and the farm tracks (~3 s), each laid
    against the ones before it. E4's real fix is to carry the overlapping three quarters of a chart's content into the
    next chart, not to build each chart from nothing;
  - the overlap-agreement test in section 7. It waits for E4's geographic detail: the chart-local noise still
    differs between overlapping charts by up to a few metres.

- **E3:** the first part is in (`src/earth_far.*`, `earth_far_test`). As built, compared with section 4:
  - **Patches.** The patches are cells of a degree lattice: 0.5°, 1°, 2°, 4° and 8° a side. They are chosen as a
    quadtree round the viewer: a cell nearer than 0.4 of its own side is drawn as its four.
  - **Counts.** That gives 39 patches on the ground, 47 at 11 km and 56 at 100 km over a whole Earth's data, all
    within the far pass's cache of 61.
  - **Mesh.** Each patch is a 65 × 65 grid on the parallels and meridians, from ETOPO, coloured by the cover. GLO-90
    isn't read for the far terrain yet.
  - **Seams.** Neighbours of one level share their edges exactly; skirts hide the cracks between levels.
  - **Plumbing.** The patches ride the generated regions' far pass: `Renderer::updateRegionHorizon` selects and
    builds them when the World is a real-Earth chart. They aren't drawn inside the chart's own square
    (`uClipHalf`).
  - **Collision.** Each patch has a geographic collision surface (`regionalSurface::makeGeographicSurface`), which
    is the ground beyond the chart's square.
  - **Chart identity.** `World::chartDescriptor()` replaces the far pass's assumption that a World without a recipe
    is Solace.

  Renders from the dev pack show it: from 9 km over Boston, Cape Cod Bay and the Cape's hook lie beyond the chart's
  edge.

- **E4:** the first part is in. These pieces work:
  - **Sliding.** `Game::slideEarthChart`: past 5 km from its chart's centre, the next chart is prepared ahead along
    the track. It goes a minute's flying ahead (20 to 60 km), on the lattice, built on two threads and never written
    to the cache. A chart already preparing is kept while it lies within 8 km of that aim.
  - **The move.** The flight moves into the next chart once it is nearer its centre than the old one's by 2 km: over
    land or sea, at any height, never on the ground. Below 30 m it keeps its height over the ground. The move goes
    through `regionTravel::Change` as the region handoffs do.
  - **Shared noise.** The detail noise of all charts is one field (`World::noisePoint`, `uNoiseFrame`). Each chart's
    x and z are turned and moved into the frame of an anchor: the centre of its 8° cell, in double. Neighbouring
    charts' grounds now differ by their base's texels alone: mean 0.17 m, p95 0.58 m, down from 0.55 and 1.6
    (`earth_chart_test`).
  - **Tests.** `earth_slide_test` flies two slides over the fixture. `earth_gpu_test` holds the GPU's ground to the
    C++'s on a real-Earth chart, with its noise frame, lakes and roads (0.4 mm).
  - **A fix.** A real-Earth flight no longer looks for a generated region to hand over to.
  - **Scenery staged before the move** (`Scenery::stage`, `Scenery::adopt`). Once the next chart is built, its
    buildings and trees are made on two workers of their own, from its World, while the flight is still in the old
    chart.
    - Chunk generation reads the World it was given (`sceneryWorld()`, set per thread), with its own cache of the
      lighthouse and the airport furniture. g_scenery's workers read g_world as before.
    - The staged set is streamed as the renderer streams: out to the buildings' range, the trees to theirs. It is
      centred where the track first meets the move, else where the aircraft is.
    - The move waits until everything within 1.5 km of the aircraft is made, unless the flight is already 20 km out.
      The staged chunks and their cache are then taken over whole, so the new chart's land comes across with its
      scenery, rather than emptied and filling in round the aircraft. Nothing near is generated on the frame of the
      move.
    - `earth_slide_test`: about 1,500 chunks staged and taken over at each slide, none missing within 1.5 km. Each one
      compared is the same, entity for entity, as a chunk made afterwards from g_world. Staged from the old chart's
      World instead, none match.
  - **The same trees in the same places** (`World::anchorPoint`). The natural scenery is laid in the frame
    neighbouring charts share: the trees, bushes, boulders, outcrops, spires and sea stacks.
    - The frame is the anchor's azimuthal x and z, exactly, not the noise frame's turn and shift (which is metres out
      tens of km from the centre). `World::buildEarth` holds it on a 65×65 grid; it is read bilinear, in double.
    - Each chunk visits the frame's cells over its own bounds, and maps each cell's point back through an affine fit
      over the chunk (exact to millimetres over 256 m). A point belongs to the chunk that holds it in this chart, so
      nothing is made twice. Off the real Earth the cells are the chart's own, as before.
    - The woods' noise in the forest bake is read in the same frame, so their edges agree too
      (`kGeneratorVersion` 4).
    - `earth_slide_test`: within 400 m of a point in Martha's Vineyard's state forest, 92% of the old chart's trees
      and stones stand in the next chart too, of their kind, within a metre. The rest sit at the woods' edges, where
      each chart's own texels tip a threshold.
  - **Airports the same in both charts** (`Airport::side`). Each chart lists its airports its own way, and an airport's
    apron, terminal and hangars stood on the side of its runway its index gave it. So at the move a field could swap
    sides. A real airport's side is now its own: away from its other runways, else as its ident has it.
    - `aptSide` (airport_layout.h) gives it to the layout, the scenery and the traffic. The terrain shader reads it per
      slot (`uApSides`) rather than from the slot's parity, so `Renderer::airportSlots` now takes the 16 nearest airports.
    - The islands keep the side their index gives them, so their renders are unchanged.
  - **What refers to an airport, carried across** (`Game::carriedAirport`). At the move the old chart's airports are
    noted by ident and found again in the new one.
    - The autopilot flies on to the same field, NAV or approach, its touchdown and heights moved onto the new chart's
      ground. Before, it fell back to a hold.
    - The GPS's pick is kept, and so is the tower's exchange (a landing clearance stands). The towers keep their voice
      by ident.
    - The AI traffic stays where it was on the Earth (`Traffic::carry`), each field's aircraft over its ground in the
      new chart. Only those of a field the new chart doesn't hold go.
    - When the new chart doesn't hold the field, all of this falls back to what it did before.
    - `earth_slide_test` checks it at both slides: every airport both charts hold keeps its side (9 of 9, 5 of 5), and
      the autopilot, the GPS's pick, a landing clearance and a circuit's aircraft are carried to it.

  Still open in E4:
  - **Towns and roads agreement.** Each chart lays out its settlements' streets and roads its own way, so the houses
    and gardens near the aircraft change at the move.
  - **Base agreement.** The bases still differ by up to a metre or so where the ground is rough: bilinear texels on
    two grids.

- **E5:** the game side and the build are in:
  - **GLO-90 streaming** (`src/earth_fetch.*`). Copernicus tiles come from the AWS open data bucket into the cache
    folder (`%LOCALAPPDATA%\SomniumVolandi\earth-glo90`). They download one at a time on a thread of their own,
    nearest first, over WinHTTP on Windows and curl elsewhere.
    - A tile is written beside its place and renamed in when whole.
    - Only tiles Copernicus lists (the pack's bitmap) are asked for, and only if neither the pack nor the cache has
      them.
    - A failed tile isn't tried again in the session. Offline, the charts are ETOPO's as before.
    - A chart's tiles are fetched before it is built (40 s at most). While flying, the tiles of the chart ahead are
      asked for every two seconds.
    - A chart built before its tiles came is built again with them: its cache key counts them.
  - **Finding the pack.** The game looks beside the executable, then in the user's and the cache's folders, then in
    the working folder (`earthCharts::setSearch`).
  - **Installing from the game** (`src/earth_install.*`). With no pack, Free Flight offers "Download the real Earth".
    - The zips listed in `SHA256SUMS` come from the release `earth` of the game's repository, or from the address on
      the first line of `earth-source.txt` beside the game. They download one after another over the fetcher's own
      transfer. The button shows the share done; the line under the title shows the zip and its megabytes so far.
    - Each zip is unpacked into a staging folder as it comes, then deleted, so the disk holds the pack and one zip at
      most. The reader takes stored zips only (zip64 too), checks each file's CRC, and refuses a name that would leave
      its folder.
    - The result must open as a pack before it is moved into the user's folder (`%APPDATA%\SomniumVolandi\earth`).
      A folder already there is set aside and put back if the move fails. The game then opens the pack without a
      restart.
    - A failure says why and offers to try again. The pack already there is never touched.
    - **Tried again, it goes on from where it stopped** (`earthFetch::downloadResuming`: an HTTP range on WinHTTP,
      curl's `-C -` elsewhere, no time limit but a stall). The zips already unpacked are kept, listed in
      `unpacked.txt`, and the zip it broke off in is carried on from its part. A damaged zip is fetched again alone.
      A changed `SHA256SUMS` (the pack built again) begins it all again; so does a result that won't open as a pack.
    - The workflow makes the `earth` release when it is named and isn't there.
    - `earth_install_test` packages the fixture in four zips and installs it from a file address through curl: whole,
      and again over the last. It also checks that a missing source and a damaged zip each fail and leave the pack as
      it was, and that the zip reader refuses compressed zips and escaping names. Then the resuming:
      - after the damaged zip, the next try fetches only it and the zips after;
      - a download broken off halfway through the third zip carries on from its half;
      - a changed list fetches every zip again.
      `earth_fetch_test` checks a part carried on from byte for byte, and a failed one's part kept.
  - **The global build** (`.github/workflows/earth-pack.yml`, run by hand).
    - The sources go into the Actions cache once: ETOPO's GeoTIFF (read as it is, the same samples as the grid it was
      converted to), CGLS-LC100, GRIP4's seven regions, OurAirports, GeoNames and the GLO-90 listing.
    - The world is built in parts of 10° by 30 to 120°, on a matrix (`build --part --only rasters,roads`). The tables
      are built as one more part.
    - A part's roads are built block by block, to bound the memory. The lines are packed as they are read, a point
      to one integer, which halves the peak and gives the same bytes.
    - Each block is noded from the lines within 0.5° of it. An edge crossing into the next block then nearly always
      has the same id in both, and the game reads it once: 132 of the 133 edges crossing 70° W in New England. The
      one left is a long chain cut into 5 km pieces from different starts, so a few km of it are doubled.
    - A GRIP region whose bounds miss the block isn't read.
    - In CI, GRIP is filed by block once, in the sources job (`split-grip`): each line goes in every block within
      0.5° of it, quantised as the edges are, its points as steps, in zlib chunks. A part reads its blocks' files
      alone, and the same bytes come out as from the zips. North America's 909 MB of zips become 390 MB of block
      files, so the sources stay well under the Actions cache's 10 GB. The New England part builds in 92 s, down
      from 146.
    - `index` writes the pack's index from the parts and `verify` checks it. `package` puts the pack in zips under
      2 GB, stored, each unpacking into `earth/`.
    - The zips are kept as the run's artifacts and attached to a release only when one is named.
    - A three-block part of New England (40-50° N, 80-50° W) builds here in about 3 minutes (44 s of rasters,
      2.5 minutes of roads) at a peak of 2.2 GB. It writes 42 MB, 30 MB of it the New England block's local roads.

  Still open in E5:
  - **A first global run** and its size, then the `earth` release it fills.
  - **The far terrain** doesn't read GLO-90 yet.

- **E6:** the first part is in. These pieces work:
  - **Solace on the real Earth.** A Free Flight out of the islands with the pack there goes on into the real
    Earth's charts. Free Flight's "Beyond" button chooses the generated islands instead. Career flights and long hauls
    are as they were until E7.
  - **The lattice.** Within 110 km of the islands' origin the chart to fly into is theirs
    (`earthCharts::slideIdAt`). So no real-Earth chart is centred there, and theirs reach only the open sea round
    the islands.
  - **The moves.** The flight moves between the islands' chart and the real Earth's as between two real-Earth charts:
    nearer the new centre by 2 km. It leaves the islands only over open water, in their border or beyond. The islands'
    chart is read from the launch's cache.
  - **The far terrain.** From the islands it is the real Earth's patches. From a real-Earth chart in sight of the
    islands, their own silhouette is drawn first (`horizon::build`), as the generated regions' far pass draws them.
    `Renderer::earthHorizon` carries the pack when the World isn't a chart of its own.
  - **Routes.** On the real Earth the pause menu's route goes to any real airport. The picker is the Free Flight one,
    with the distance to each airport. The autopilot holds the course, and the charts on the way prepare as the
    flight goes: Solace to Bermuda is 640 km.
  - **Charts without an airport.** A chart with no airport of its own (the open ocean, the wilds) carries the nearest
    real one beyond its square. A World keeps an airport, as the flight, the GPS and ATC expect; nothing of it is
    built there, and the chart that holds it builds it when the flight arrives.
  - **Tests.** `earth_solace_test` checks:
    - the lattice;
    - the islands' silhouette from beside them, and never from their own chart;
    - a flight west out of the islands into an open-ocean chart, and east back into theirs, where it was on the Earth
      each time;
    - that no generated region is ever wanted.

  Still open in E6:
  - **The sea floor.** The islands' sea floor isn't blended into ETOPO's yet. The sea is opaque, so it doesn't show.
  - **The real Earth's charts and the islands.** They reach only the islands' open sea, so no chart shows Solace's land
    at its own detail but the islands' own.

- **E7:** the first part is in. These pieces work:
  - **The career's world.** A new career takes the real Earth beyond the islands when the pack is there
    (`Career::realEarth`). An older career keeps the generated regions; the hub's travel screen changes it, at home,
    with nothing parked or open beyond the islands.
  - **Travel.** The airline flies to real airports: scheduled service, medium or large fields, the twelve nearest
    within 3,000 km, each at the chart about its runway. The licences reach as they did for the regions: 450 km for
    a Private, 900 km for a Commercial, the rest for an ATP. From the islands the nearest are Bermuda (640 km) and
    the American coast.
  - **Airports without a chart.** A chart's airports come without building it (`earthCharts::chartAirports`, the
    same selection `World::buildEarth` uses). The career's boards, travel and long hauls read them through
    `procedural::airportOf`, as they read a region's recipe.
  - **Long hauls** go to real airports. A haul flies out of the islands on the real Earth's charts and is there in
    any chart that holds its airport inside the square. Near the destination, the slide prefers the chart about the
    airport. The career goes on from that field.
  - **Saves.** A real-Earth career writes version 8 ("earth 1"); one with the generated regions writes 6 or 7 as
    before. A career whose chart can't be read (its pack gone) loads at home, its aircraft with it, rather than
    being lost.
  - **Names.** A real-Earth place is named bare ("Nantucket Memorial Airport"), not as a region ("the ...").
  - **Tests.** `earth_career_test`, over the fixture:
    - travel from the islands to real airports;
    - a long haul on the islands' board to Nantucket (1,432 km), flown out of the islands and arriving in the
      chart about KACK;
    - delivered, the career and its aircraft there;
    - saved as version 8 and loaded again;
    - flown home;
    - the pack gone: loaded at home.

  - **The GPS map beyond the chart** (`Game::beyondAirports`). The real airports within 120 km of the chart's centre
    but outside its square are read from the pack's cell-indexed table, once for each chart, and placed on it. Each is
    drawn dimmer than the chart's own, by its main runway's line. Medium and large ones are named; at 15 km of range and
    less, every one is. `earth_slide_test`: from Nantucket's chart, 16 of them, Martha's Vineyard's where the Earth has
    it.

  - **Cross-country work beyond the chart** (`Career::refreshBoard`, `Contract::crossCountry`). On a real-Earth chart
    the board also carries up to two loads for small fields beyond it, 30 to 180 km on: strips, and fields no airline
    serves, which the long hauls never take.
    - They are flown as a long haul is, the charts on the way made ready as you fly, and delivered at the field.
    - A long haul of 200 km or less on the real Earth is titled "Cross-country" too. The board card says
      "CROSS-COUNTRY / BEYOND THE CHART", and the plan's challenge line no longer speaks of an ocean there.
    - `earth_career_test`: at Nantucket, after the long haul from the islands, the board holds three, two to small
      fields beyond the chart. The nearest (Plymouth, 92 km) is flown from there into the chart about its field and
      delivered.
  - **ATC across charts.** Carried at the move by ident since E4's fourth part: the tower's exchange goes on with the
    same field in the new chart.

  Still open in E7: nothing beyond what E4 and E5 leave open.

- **E8:** the first realism pass is in: the cities built over as the land cover has them.
  - **Districts.** The cover's built-up class is read on a 400 m grid. Each stretch of it is covered by up to 56
    districts: towns about 2.6 km apart, each named for the nearest place, the densest and the most central first.
    They fill what the places' circles left between them: a city's suburbs, towns grown into each other. Boston's
    chart goes from 24 towns to 80 and from 355,000 lots to 613,000. Its build takes 34 s, up from about 25.
  - **Low built-up land.** On the real Earth, built-up cover is built on from 1 m over the sea, not 2.5 m
    (`buildableHeight`). Taking the roofs off the surface model never leaves the ground under 2 m where the model
    stands over it. Boston's waterfront, Back Bay and South Boston were left bare before.
  - **Approaches.** On the real Earth an airport's approach funnels no longer keep streets and houses away.
    Only its grounds do (`airportExclusion`). Under a funnel, a building stands only if it is lower than the
    approach surface, as real ones are under Logan's approaches.
  - **The cache.** The world cache (WLD9) keeps a real-Earth chart's land cover, so a chart loaded from it builds
    its towns as a fresh one does. The charts' content version is 2.
  - Seen in renders: Boston from 380 m over South Boston, a city of streets and houses to the horizon where open
    grass lay before.

- **E8, second pass: the far terrain's lights after dark.** Each far patch's vertex (`horizon::Vertex::lights`) is lit
  by two things: the share of its cell the cover has built over (a 3 x 3 sampling at the finest level, 5 x 5 on the
  coarser ones), and a glow round every place of 2,000 people or more. The glow reaches 0.7 to 16 km, by the square
  root of the population, and is brighter by its logarithm, so the towns the cover misses still show.
  - The far pass writes it as sodium-warm emission once the sun is down (`uNight`). It is squared so each glow draws
    in to its core, and broken into clumps on a hashed 250 m lattice in the patch's own frame. The lattice fades to its
    mean where a pixel spans several cells, so it never shimmers. The sea and the skirts carry none.
  - The vertex grows to 11 floats; a coarse mesh's bound is now 4.125 MiB.
  - `earth_far_test`: New Bedford's patch is lit at the city and dark at sea. In renders from 6 km over Nantucket at
    22:30, Martha's Vineyard's and the Cape's towns show as faint warm clusters 60 to 100 km off.
  - `REGION_TOD=<hours>` sets the time of day of a region scene, for review shots.

- **A fix: every airport of a chart painted.** A real-Earth chart holds up to 40 airports, but the ground shaders took
  the first 16 (`uAp`, `uApDim`). The smaller fields past them had their flattened grounds, ATC and the GPS, and no
  runway drawn. `Renderer::airportSlots` now gives them the airports nearest the camera, or the map's centre for the
  GPS image, re-chosen each frame.
  - It takes the 8 nearest even-indexed airports into the even slots and the 8 nearest odd ones into the odd. The
    shaders take the side an apron lies on from the slot's parity, as `airport_layout.h` lays it out by the airport's.
    The runway shader's pinned functions (`runway_preservation_test`) stay as they were.
  - Each slot carries its buildings' world bounds and box range as uniforms (`uApBox`), in place of the world table's
    rows 384 to 415, which were by airport index. The uniform is kept out of the aircraft bodies' bake, so its cache
    key doesn't move.
  - With 16 or fewer airports, every one keeps its own slot. The islands' renders are pixel-identical before and
    after, airport and GPS map.
  - `airport_slots_test`: forty airports. The 8 nearest of each parity are chosen, nearest first, each with its
    runway, size and buildings, and the far end's come in as the camera goes there.

- **E8, third pass: the airports' other runways** (`World::extraRunways`). A real airport's main runway is its
  `Airport`; the others come from OurAirports with both ends known, 250 m or more, not on the water, up to five each.
  Each is laid on the chart by its ends; the main one over again, ends swapped, is left out.
  - **The ground.** Each gets a strip of level ground at its airport's height and its approach funnels, in the
    chart's terrain and in every rule the scenery and roads read (`airportGroundsCap`, `airportFunnelCeiling`,
    `airportInfluence`, `airportExclusion`). The charts' content version is 3.
  - **Flying.** `World::onRunway` answers on them with their airport, which is what landing and arrival read, and
    gives the runway's own surface to the wheels' friction.
  - **The look.** The 8 nearest the camera are painted by an overlay of their own after their airport
    (`extraRunwayMaterial`). The runway shader's pinned functions stay as they were. The overlay draws pavement or
    strip, edge and centre lines, threshold bars, designators, touchdown wear and the pools of the edge lights. It
    stops at the main runway, whose markings run on through a crossing. Their edge lights and threshold bars stand
    on their airport's height, and no light stands on a runway it crosses.
  - **The layout.** None of the airport's buildings stands on them: the layout is made for the main runway, and what
    falls within 20 m of another is left out. The GPS map draws them beside their main runways.
  - `earth_chart_test`: the fixture's chart has 8 of them, among them Martha's Vineyard's 15/33 (1,004 m) across its
    6/24. Each is level at its airport's height, answers with its airport and its own surface, and has nothing of the
    scenery standing on it. Rendered from 1.5 km and 300 m, the airport shows both runways on one levelled ground.

- **E8, fourth pass: regional building styles** (`World::buildingStyle`, `earthCharts::buildingStyleOf`). A chart takes
  the country most of its airports are in, and that country a way of building. There are seven:
  - North America and the suburbs like it (Australia, New Zealand);
  - western and central Europe;
  - the Mediterranean, with Latin America's colonial towns;
  - the Nordic countries;
  - the tropics;
  - East Asia;
  - the dry lands, the American south-west among them.
  A country not named takes the tropics' style within 25° of the equator, else Europe's.
  - The scenery shader (`uStyle`, `styledWall`, `styledRoof`) gives a house its region's shares of brick, weatherboard
    and render, and of tiles, slate, shingles and sheet metal, each in its own colours. Examples: New England's boards
    under grey shingles, the Mediterranean's white and ochre under terracotta, Nordic red and yellow boarding. The
    islands and the generated regions keep their own (style 0, the code as it was).
  - `REGION_STYLE=<n>` builds a region scene's houses another way, for review. Oak Bluffs from 260 m in four styles
    shows the roofs change from grey shingle to terracotta to red and black to tin.

  Still open in E8: airport layouts by size, rivers (GRIP has none; a rivers dataset), and real landmarks.

Decisions taken with the defaults (the owner can change them):
- The generated regions keep working until E6. In real-Earth play the real world replaces them; they stay available
  as a "Generated Earth" choice in Free Flight.
- The data isn't in the game's zip. The game looks for `earth/` beside the executable and in its data folder, and says
  how to get it.

---

## 7. Contracts to keep (they have tests)

- **CPU and GPU agree** on the terrain height (`World::height` / `terrainH`), the road bed (`road_grade_gpu_test`) and
  the planet (`planet_gpu_test`). New samplers get the same kind of test.
- **Charts agree.** Two overlapping real-Earth charts give the same ground within 0.3 m at a point 20 km from both
  centres, and the same airports, road nodes and town centres there within 0.3 m (`earth_chart_test`).
- **The aircraft bodies' bake is untouched by world work.** Gate world-only GLSL behind defines the bake doesn't set
  (`tests/shader_prune_test.cpp`, `tests/fixtures/aircraft_bake.fnv`).
- **Determinism.** The same pack and build give byte-identical charts. The tests read the fixture, never the network.
- **Floats stay near the origin.** Nothing simulated sits more than a few tens of km from the chart origin.
- **Solace is unchanged** inside its own chart, with all of its tests.
- **Saves.**
  - A career save written before this work still loads.
  - A real-Earth position is saved geographically (save version 8, when E7 needs it).
  - The unknown-version case in the save tests moves on accordingly.
- **Shared GLSL and structs.** World-only structs and functions stay behind `GROUND_ROADS` and the like.
- **Profiling bits.** `uDbg` bits never share a bit with `Renderer::kProbe*`.

---

## 8. Testing headless

- `ctest` (Release). The GPU tests run on llvmpipe through EGL.
- **Pictures:** `render_harness serve` on Xvfb, `tools/render_client.sh`. Real-Earth scenes take `EARTH=<pack dir>`
  and `AT=lat,lon,height,heading` for geographic placement.
- The MinGW cross-build before anything Windows-facing.

---

## 9. History

- 2026-10-11/12: Codex's Phase 0 (`tools/earth` v1: cube tiles, zstd, the source and terms review in
  `EARTH_DATA_SOURCES.md` and `EARTH_LANDCOVER_REVIEW.md`, the sampling studies). The cube contract and `geo.h`'s
  tiles stay for the generated regions' addresses. The v2 pack uses 1° geographic tiles because the elevation and
  land-cover sources are already on that grid.
- 2026-10-13: the owner moved to procedural regions (`PROCEDURAL_EARTH.md`): 6,143 generated regions, ocean handoffs,
  the far silhouettes and the coarse collision fallback. All of that is reused here.
- 2026-10-20: the feasibility study measured the packs (section 2) and built a Madeira prototype with GLO-90 in the
  regions' sampler. The owner chose option C.
