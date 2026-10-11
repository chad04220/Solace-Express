# The real Earth: plan for Codex

The owner (chad04220) wants this:

> "itd actually be really neat if we had the solace islands be off the east coast of america in the middle of the
> atlantic ocean and had the rest of the real world simulated as realistically as possible"

They have pointed at two datasets: NOAA's **ETOPO 2022** global relief model for the land and sea floor, and
**GLOBIO GRIP** for the world's roads. They chose the **full-size planet** (Earth's radius) over a small one.

This document is the brief for that work: what exists today, the target architecture, the data, and the phases.
Each phase comes with deliverables and tests you can run headless, without a GPU. It follows
`docs/COLLABORATION.md`:
- Work on `codex/...` branches.
- Hand over plain source.
- Send large data as download links to the owner.
- Post each phase in issue #2 with the usual heading lines.

Claude reviews and merges each phase.

Tracking: Claude's task #187. The owner has chosen the real Earth over generated landmasses: everything outside Solace
comes from real data, with the game's procedural detail only on top of it where the data is coarser than the eye.

---

## 1. Where things stand (v3.47, commit bdd896c on `claude/compassionate-davinci-4cfo20`)

**The world is flat and small.**
- `World` (`src/world.h`) holds one hand-designed archipelago.
- It is a 2048² heightmap `hm`: 39 m texels covering [-40 km, 40 km]. Each texel holds base height, detail amplitude, lushness and coldness.
- It is ringed by 10 km of sea, and the whole square repeats every 100 km (`WRAP_HALF`, `wrapCoord`, `nearCopy`).
- The ground's height is `World::height(x, z, octaves)` on the CPU and `terrainH(p, oct)` in `src/shaders/common.glsl` on the GPU. Each is the base plus `terrainFbm` detail plus the graded roads, and the two match exactly.
- The generated arrays are cached on disk under a stamp (`World::build`, `loadCache`). The tests share one generated world through the `test_world` fixture.

**Coordinates are floats in metres.**
- x is east, y is up, z is south; heading 0 looks along −z.
- The aircraft is brought back inside the square when it crosses the seam: `Plane::step` adds `seamShift`, and `Game::followSeam` moves what the flight has placed in the world. This is the only re-basing the game has.

**The round world is drawn, not simulated** (`src/planet.h`, `src/shaders/planet.glsl`, commit bdd896c).
- The renderer lays the flat world over a sphere of 6,371 km about the camera's nadir (an azimuthal equidistant wrap).
- The physics and scenery stay flat. The lighting pass maps each drawn point back to the flat world.
- The terrain draws every copy of the islands out to the horizon, at any height up to orbit.
- `tests/planet_gpu_test.cpp` holds the GLSL and the C++ in step.
- This is a stand-in for a real globe. It is exact near the camera and good to a fraction of a percent at the horizon.

**The roads, settlements and scenery are generated** from the heightmap (`docs/LIVING_ISLANDS_PLAN.md`):
- `road_network.cpp`:
  - A* routing at each class's grade;
  - `layRoad()` for a given polyline;
  - `profile()` vertical curves;
  - `roadGrade` building the bed into the ground (CPU and `roads.glsl` in step; `tests/road_grade_gpu_test.cpp`).
- `bridges.cpp`.
- `settlements.cpp`: settlement extents grown over an effort field, streets as network roads.
- `scenery.cpp`: lots, trees, the mask (2048² RGBA8: road distance, building density, urbanness, farmland).
- `entities.cpp`: the instanced buildings and trees, streamed in 400 m chunks.

**Airports** are the 16 hand-placed entries of `kAirports` (`src/world.cpp`): code, name, x/z, elevation, heading, length, width, surface and size. Each one's ground is flattened in `World::build`, and its layout comes from `airport_layout.cpp`.

**The career** (`career.cpp`) runs between those 16 airports.

---

## 2. What we're building

```
            offline (tools/earth/, Python, run once per data release)
  ETOPO 2022 ──► elevation tiles  ─┐
  GRIP roads ──► road tiles        ├─► world pack (tiles + manifest with SHA-256) ──► download link / CI artifact
  OurAirports ─► airport table     │
  land cover ──► surface tiles    ─┘
            in game
  geodesy (doubles: lat, lon, height) ── floating origin (floats near the camera, re-based like the seam)
  tile streamer (worker threads, budgeted) ── CPU height queries (physics) == GPU height sampling (rendering)
  Solace patch: the islands' own heightmap, roads and scenery, placed at one lat/lon, blended into ETOPO's sea floor
  per-tile road grading (layRoad) ── per-tile mask ── the existing ground material, scenery and entity streaming
```

These are the rules everything else follows:
- The real globe replaces the copies of the islands. There is **one** Solace.
- The islands' own content stays exactly as it is today, inside its patch.
- Everything outside the patch comes from the data, plus the game's procedural detail where the data is coarser than the eye.

---

## 3. The data

| Dataset | Used for | Resolution | Raw size (approx.) | Licence and credit |
|---|---|---|---|---|
| **ETOPO 2022** (NOAA NCEI), <https://www.ncei.noaa.gov/products/etopo-global-relief-model> | land height and sea floor, one seamless grid; the coastline at its 0 m contour | 15″, 30″, 60″ (≈ 460 m, 0.9 km, 1.8 km) | 60″ ≈ 0.5 GB, 30″ ≈ 1.9 GB, 15″ ≈ 7.5 GB (int16/float) | NOAA data, generally public domain. **Read the product page's terms before shipping**, and credit NOAA NCEI with the dataset's DOI in `THIRD_PARTY_NOTICES.md`. |
| **GRIP global roads** (GLOBIO; Meijer et al. 2018), <https://www.globio.info/download-grip-dataset> | every real road, classed highway / primary / secondary / tertiary / local | vector | several GB as published | **Not yet checked.** Read its licence and attribution terms before anything derived from it is shipped. If they don't allow redistribution in a game, fall back to OpenStreetMap (ODbL: attribution plus share-alike on the derived database) or Natural Earth's roads (public domain, major roads only). |
| **OurAirports**, <https://ourairports.com/data/> | airports and runways (`airports.csv`, `runways.csv`) | points, runway ends, headings, lengths, widths, surfaces, elevations | ≈ 15 MB CSV | Released to the public domain by its authors. Confirm on the site and credit it anyway. |
| **Land cover** (pick one): ESA WorldCover 10 m (CC BY 4.0), Copernicus Global Land Cover 100 m, or MODIS MCD12Q1 500 m | forest, cropland, desert, snow and ice, urban: drives the ground material and the scenery | 10 m to 500 m | WorldCover is very large; resample to ~250-500 m | Each needs credit. Check the terms of the one chosen. |
| Optional, later: **Copernicus GLO-30 / GLO-90 DEM** | finer height near airports and mountains | 30 m / 90 m | very large | Free with credit; check the terms. |

These rules apply to all of it:
- **No raw dataset is committed.** The tools take the raw files as input and record where each came from: URL, version, SHA-256.
- The outputs are a **world pack**: a directory of tiles plus `manifest.json`, with each tile's SHA-256, the source versions, and the tool's version.
- The owner downloads or builds the pack. The game verifies it like the voice packs (`tools/voice_import.py`) and the prebuilt aircraft meshes (`docs/PREBUILT_AIRCRAFT_SAFETY.md`).
- Licences go in `THIRD_PARTY_NOTICES.md`, plus a `licenses/` folder inside the pack.
- **Every size above is an estimate. Measure, and report the real numbers in issue #2.**

---

## 4. Phases

Each phase ends with a branch that builds and passes the full `ctest`, its own new tests, and the Windows cross-build. It is posted in issue #2 with numbers.

Keep each phase reviewable: one subsystem per branch, with no reformatting of files you don't need to change.

### Phase 0: the data pipeline (`tools/earth/`, Python, offline)

**Tile scheme.** Use a **quadrilateralised cube sphere**: six faces, and a quadtree on each.
- A tile at level L covers 1/4^L of a face. Each tile is a 256 × 256 grid of samples, plus a one-sample apron on each side so neighbours meet.
- Level 6 has texels of ≈ 0.6 km, close to ETOPO's 30″.
- This avoids the poles' crush of a lat/lon grid.
- Each face's (u, v) maps to a direction on the sphere through the standard tangent-adjusted cube map.

**Elevation.** Resample ETOPO into those tiles, bilinear and area-weighted when going down.
- Store int16 decimetres, or int16 metres with a per-tile offset, then delta + zstd.
- Ocean-only tiles at the coarse levels compress to almost nothing.

**Roads.** Clip GRIP's polylines to tiles.
- Keep the class.
- Simplify (Douglas-Peucker, about 15-30 m at the finest level).
- Quantise each tile's points to its own 16-bit grid.
- Keep junctions as shared vertices so the network stays connected across tiles: give each node a global ID.

**Airports.** Filter OurAirports to open fields with at least one runway (types small, medium and large airport; seaplane bases optional).
- Keep the ICAO/IATA ident, name, position, elevation, and each runway's ends, heading, length, width and surface.
- Write one table: CSV or a small binary in the pack.

**Determinism.** The same inputs and the same tool version must give byte-identical tiles.

**Tests:**
- A **small fixture region** checked into `tests/fixtures/earth/`, under about 1 MB: for example a 600 km box round Bermuda at a coarse level. Unit tests run on it: tiles round-trip, aprons agree with their neighbours, road nodes keep their connectivity across tile edges, and the airport rows parse.
- A determinism test: build the fixture twice and compare the hashes.

**Deliverables:**
- `tools/earth/*.py`, with a README: the exact download URLs, versions, checksums and commands.
- The fixture.
- The tests.
- A measured size table for the full packs at 60″ and 30″.

### Phase 1: geodesy and a floating origin (C++, no rendering)

- `src/geo.h/.cpp`: a position is **doubles**, latitude, longitude and height. The world is the **sphere of `PLANET_RADIUS`** (`src/planet.h`): latitude and longitude map straight onto it. That puts distances off WGS84 by under 0.5%, which is fine for a game. Keep the ellipsoid out unless the owner asks for it.
- A **local frame**: an origin (lat, lon) with east-up-south axes matching the game's x-y-z, and float metres within it.
  - Re-base when the aircraft is more than ~20 km from the origin.
  - Generalise today's seam re-base: `seamShift` and `Game::followSeam` already move the aircraft, the flight's craters and pits, the traffic and the route by an offset.
  - Re-basing must change nothing the physics can feel, so test it the way the seam is tested: `tests/flight_test.cpp` part 2 flies an aircraft across the seam and checks it flies on as it was.
- Conversions: tile ↔ direction ↔ local frame. Bearings and great-circle distances: the career's distances, the GPS, ATC.
- **The Solace patch's placement.**
  - A constant lat/lon for the islands' centre, plus the rotation of their square to north. Suggestion: about **32.5° N, 58° W**, roughly 600 km east of Bermuda (32.3° N, 64.8° W) in deep water. **The owner picks.**
  - Inside the patch, local coordinates are exactly today's x and z, so all of Solace's content and tests stay as they are.
- **Tests:**
  - lat/lon ↔ local round trips (to a millimetre near the origin);
  - re-basing invariance (a flight crossing a re-base flies the same path);
  - the patch's corners land where they should.

### Phase 2: the ground from the tiles

- A **tile streamer**:
  - Load tiles round the camera by level, using distance and screen error like the terrain quadtree's split rule in `terrain_mesh.cpp`.
  - Read them on worker threads, with a per-frame budget like the scenery's.
  - Keep a fixed cache.
  - The physics needs the finest level under the aircraft **before** it gets there; prefetch along the velocity.
- **CPU height:** the replacement for `World::height` outside the patch.
  - The tile's bilinear sample, plus the existing procedural detail (`terrainFbm`).
  - Its amplitude comes from the tile's local relief, as `hm`'s detail amplitude does today.
  - Inside the patch it **is** today's `World::height`.
  - Across the patch's border, blend the islands' 10 km sea apron (−80 m, `SEA_DEPTH`) into ETOPO's sea floor over 20-40 km. The real floor there is about −5,000 m: a slope like a continental shelf's.
- **GPU height:** a clipmap or virtual texture of the same tiles, sampled by `terrain_vs.glsl` and `terrain_fs.glsl` in place of `uHM` and `baseAt`.
  - It must give **the same numbers as the CPU**. Write a GPU test like `tests/road_grade_gpu_test.cpp`: thousands of places across tile edges and levels, the shader against the C++, to a centimetre.
- **The renderer's planet** becomes real geometry.
  - Today `planetPos` wraps the flat world round the camera.
  - With tiles, each vertex's direction comes from its tile, and its position is (radius + height) × direction relative to the camera's origin, computed in the local frame so floats stay small.
  - Keep `planet_gpu_test`'s properties: near the camera nothing moves; the horizon and limb match `planetHorizon`.
- **Drop the island copies** (`selectTerrainChunks`' `kx/kz` loop, the entity copies near the seam) once the globe draws.
- **Tests:**
  - CPU/GPU parity;
  - height continuity across tile edges and level changes;
  - the patch blend has no step;
  - Solace's own tests unchanged (`airport_layouts`, `roads`, `bridges`, `community_layout`, the flight model, the gameplay loop).
- Probe the far view on llvmpipe with the render harness (§6).

### Phase 3: the real roads

- Each tile's GRIP polylines become network roads.
  - Map the classes: highway → `RC_HIGHWAY`, primary → `RC_ROAD`, secondary → `RC_ROAD` or `RC_LANE`, tertiary → `RC_LANE`, local → `RC_TRACK` or `RC_STREET` in towns.
  - Lay each out with `layRoad(world, cls, line, h0, h1, pins)`. It rounds the alignment, resamples it, and profiles it to the class's grade and vertical curves.
  - Pin the junctions to shared heights so tiles agree at their edges.
- The real alignment is given, so don't re-route it.
  - Where the real road is steeper than the class allows (mountain passes), let the profile follow the ground. Record it as a real road: an exception list, not a failure.
  - Bridges follow `bridges.cpp`'s rules where the profile leaves the ground over water or a gully.
- Grade per tile: build a per-tile **road grid** with `RoadGrid`'s encoding (`roadEntryRows`, the `cls + 8·flags + 128·path` code word), so `roads.glsl` and `terrain_material.glsl` paint and grade as they do now. `roadGrade` must stay identical on the CPU and the GPU.
- **Tests:**
  - per tile, `tests/roads_test.cpp`'s checks (grades, bed tilt, banks, conflicts), with the exception list;
  - continuity across tile edges;
  - the road-grade GPU test on real tiles.

### Phase 4: the real airports

- OurAirports' rows become `Airport` records, with each runway placed from its ends.
- The ground under each runway and apron is flattened as `World::build` does for Solace's fields: an airport's grounds are a flat pad blended into the terrain.
- Each airport's layout comes from `airport_layout.cpp`, sized by runway length and type. Major airports can get hand-tuned layouts later.
- Roads must not cross runways: `roadByAirfield` and the funnel ceiling (`airportFunnelCeiling`) already keep Solace's roads out of the approaches.
- ATC, the GPS, the map and the career's search need a **spatial index** of airports, because 40,000 won't fit a linear scan per frame.
- **Tests:**
  - runway geometry against the data (heading, length, thresholds);
  - flat pads;
  - no road on a runway;
  - every airport within its tile;
  - the existing airport tests on Solace unchanged.

### Phase 5: land cover, towns and scenery

- Land cover drives the ground material (`terrain_material.glsl` takes lushness, coldness, farmland and urbanness today) and the scenery's mask.
- Towns and cities come from the land cover's urban class, plus population or settlement data if a suitable openly licensed set is chosen. Grow them with `settlements.cpp`'s effort field from their real centres, with streets from the real road data where it has them.
- Forests and fields come from the land cover's classes through the existing tree and field generators.
- **Tests:** like `community_layout_test` per tile; deterministic per tile; scenery chunk streaming stays within its budget.

### Phase 6: the career on a planet

This is design work with the owner first:
- the contracts between real airports;
- the aircraft's ranges;
- fuel prices;
- time zones and the sun's position by longitude (the renderer's local-sun shading is ready for it);
- weather by region;
- the Solace career staying as the start.

---

## 5. Contracts to keep (they have tests)

- **CPU and GPU agree** on everything both compute:
  - the terrain height (`World::height` / `terrainH`);
  - the road bed (`roadGrade`, `road_grade_gpu_test`);
  - the planet (`planet_gpu_test`).
  New samplers get the same kind of test.
- **The aircraft bodies' bake is untouched by world work.**
  - `tests/shader_prune_test.cpp` hashes the bake's pruned shader source (`tests/fixtures/aircraft_bake.fnv`).
  - A world-only edit to a shared GLSL snippet must not change it. Gate world code behind a define the bake doesn't set, as `ROUND_WORLD` and `LOCAL_SUN` do (`planet.glsl`, `common.glsl`).
  - Also note that `tools/aircraft_geometry_inputs.cmake` hashes `world.h`, `renderer.cpp/h` and `shaders.h` into the shipped meshes' identity: editing them makes CI bake a fresh set (that's fine, but expect it).
- **Determinism.** The world is generated the same everywhere, cached under a stamp, and the tests read one generated world (`test_world`).
- **Floats stay near the origin.** Nothing simulated may sit more than a few tens of km from the current origin in float coordinates.
- **Solace is unchanged** inside its patch: its heights, roads, bridges, settlements, airports and all of their tests.
- **Saves.** A career save written before this work must still load: Solace's local coordinates plus the patch's origin. Version the save format if positions change meaning (`save_test`).

---

## 6. How to test headless (no GPU)

- `ctest` in a Release build. The GPU tests (`road_grade_gpu`, `planet_gpu`, `terrain_shadow_bake`) run on Mesa's llvmpipe through EGL, and skip where there's no EGL.
- **Pictures.** `render_harness` on Xvfb.
  - Start it once as a render server: `SHADERCACHE=<dir> build/render_harness serve 960 540`.
  - Send it shots with `tools/render_client.sh at AT=x,z,height,heading CAMPITCH=deg WX=cover,base,precip,storm,wind,from,gust,turb,hour out=/path.ppm`.
  - Each shot takes a second or two once loaded. Add `bench=4` for per-pass times.
  - Post before/after pictures in issue #2.
- The MinGW cross-build (`cmake-mingw.cmake`) before anything Windows-facing.

---

## 7. Decisions for the owner (ask in issue #2 before Phase 1)

1. **Where Solace sits.** The suggestion is about 32.5° N, 58° W, east of Bermuda in deep water. The alternative is nearer the coast, on the continental shelf, so the sea floor round it is shallow.
2. **How much data ships with the game.**
   - A 60″ global base: small, maybe a few hundred MB.
   - A 30″ base, ≈ 1 km: around a gigabyte or more.
   - Optional regional high-detail packs.
   Measure in Phase 0 and propose numbers.
3. **Roads.** GRIP if its terms allow; otherwise OpenStreetMap (ODbL obligations) or Natural Earth (major roads only).
4. **Scope of the first release.** The North Atlantic region first (the US east coast, Bermuda, Solace), then grow outwards.
