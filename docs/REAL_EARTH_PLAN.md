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
  - The sea also takes class 80 at sea level (salt marsh and estuaries the cover calls water). Inland lakes aren't
    painted as water yet.
  - GRIP edges are cut 1.5 km inside the square. Local roads (GRIP class 5) come from a separate `lroads` layer.
  - Towns: the 24 largest places, merged where they overlap.
  - The road grid's GPU data is an index list plus a segment table: 40 MB for Boston, down from 136 MB. The forest
    noise moved to the road grid's second channel, so a texel's first entry has 26 bits.

  Still open in E2:
  - lakes as water;
  - Boston's build time, against the 10 s budget (the mask bake ~12 s, the settlements' streets ~5 s, the road grid
    ~5 s);
  - towns chosen nearer the start, not only by population;
  - the overlap-agreement test in section 7.

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
