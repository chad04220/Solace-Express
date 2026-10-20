# Procedural regions

Around the authored Solace Islands the game places 6,143 generated island regions at stable addresses on the real
globe. They are invented landscapes, not a model of real countries, coastlines or airports, and nothing is
downloaded: each region is worked out from its address and a world seed. They reuse the game's own terrain, road
routing, settlement growth, airport facilities, scenery meshes, collision and flight model. Free Flight can start in
any of them, a flight can cross from one to the next, and the career travels to them.

Code: `src/procedural_regions.*` (descriptors and recipes), `src/geo.*` (geodesy on the sphere),
`src/region_travel.*` (charts and handoffs), `src/distant_regions.*` (the far silhouettes), `src/regional_surface.*`
(the coarse collision fallback), and the region loading and Earth flight in `src/game.cpp`.

## Addresses and recipes

- Generator version 1 divides a tangent cube into 6,144 address cells at level 5, reserves the area round Solace and
  derives a repeatable recipe from the canonical address and the world seed. Neighbouring centres are 226.52-312.73 km
  apart; Solace's nearest generated neighbour is 196.94 km away. The menu never generates or stores the whole planet.
- A descriptor (`procedural::Descriptor`) carries the id, the name, the origin, the archetype (archipelago, volcanic
  chain, fjords, atolls, continental fragments; latitude picks the climate and the tropical or temperate set) and the
  size class. The size comes from a stream of its own, so it changes nothing else about the region: 30% are small,
  38% medium, 32% large.
- **Small:** a few islands and two or three fields (two cones joined at their feet, one mountain block cut by three
  fjords, two atolls, one long island with a field at each end), and two villages.
- **Medium:** four to six fields, and four villages, one of them a market town.
- **Large:** islands on Solace's own scale, with a city round an international airport, up to ten airports, and ten
  villages, three of them market towns.
- A recipe (`procedural::Recipe`) builds the region's airports, towns, landmasses (rolling islands, calderas, reef
  rings) and fjord channels. Coastlines use 64-knot periodic profiles with headlands, bays and coves; atolls have
  uneven lagoons, reef passages and dry inhabited arcs; fjords have curved trunks, unequal heads and tributaries.
  Runways, aprons and town pads are graded on their own ground and every approach is kept clear. Land fades to ocean
  before a 38 km square edge. A recipe is under 16 KiB, and sampling it is pure, thread safe and allocation free.
- `kRecipeRevision` (3) invalidates the generated terrain caches when the landscape formulas change. It never moves
  an address, renames a region or enters Solace's cache stamp.

## Sights

`Recipe::features()` works out a region's sights from its layout whenever asked (nothing is stored, so a recipe's
memory stays as it is): volcano craters (from the rim's height), summits over 200 m off the cones (two in a small or
medium region, three in a large one), capes, islets, the sounds between an archipelago's islands, atoll lagoons,
fjord heads and the strait. Each has a key of its own in the region (`crater0`, `summit1`) and a name drawn from
32-word pools shuffled by the region's seed, so neighbouring regions don't repeat each other. A small region has two
to six, a large one five to thirteen, besides its built landmarks. They are on the GPS and the moving map as
diamonds, the job board offers photo flights over them, and the pilot profile's passport keeps a page per region:
its airports stamped (`regionId:CODE`) and its sights found (`regionId/key`). Four achievements count them: land
beyond the islands, land in five regions, land at every airport in a region, find twenty sights.

## In the career

- `Career::region` is where the career is ("solace" until it travels) and `OwnedPlane::region` where each aircraft is
  parked. The campaign, the trials, Defense Command and the airline stay in the islands.
- **Travel** (`Career::destinations`, `Career::travel`): an airline ticket to the regions near this one, $120 plus
  $0.45 a km. A Private licence reaches 450 km, a Commercial 900 km, the ATP every region listed; the islands are
  always listed when away, and the flight home is on account when the bank can't pay. The career arrives at the
  region's main airport (`procedural::mainAirport`).
- Each region has its own job board, paying 15% more than the islands'. An aircraft bought there is parked there;
  one parked elsewhere is rented instead.
- **Long hauls**: with an aircraft that has the range (the Emissary 500 owned, the Galleon 180 rented), the board
  offers up to two jobs to another region's main airport, flown whole across the open ocean. The destination is
  charted on every region the flight crosses, so the HUD, the GPS and the autopilot's course hold point at it from
  the start; the Earth flight machinery below carries the flight. Delivered, the career and the aircraft go on from
  there; anything else calls the job off and the career stays where it set out. A long haul is never saved mid-flight.
- Saves: a career with nothing beyond the islands writes save version 6 byte for byte. Anything away writes version
  7, with a `region` line and `plane_region` lines. The loader reads the islands' airports from their table and a
  region's from its recipe, never from the loaded world; a region airport its recipe no longer has moves the career
  or the aircraft to the region's main airport and releases the job.
- Each screen has its world (`Game::screenWorld`): the hub and the debrief stand in the career's region, the
  research terminal in the islands, the menu in either.

## Loading and isolation

Selecting a descriptor is cheap; loading it replaces the actual terrain world behind an interactive loading screen.
World replacement joins the old scenery workers and the career's quote readers, clears the region's airport,
lighthouse and collision caches and uploads the new terrain. Scenery lots, vegetation, landmarks, road furniture,
ferry routes and spatial indices belong to each World, so a candidate being prepared never overwrites the active
one's.

The runtime holds one active full chart and one pending or retiring chart. Background generation uses at most two
threads. The disk cache holds the authored world and one replaceable generated world; its stamp covers the generator
version, the recipe revision, the address, the world seed and the build.

## Flying between regions

Charts are nonrepeating: past the islands' square the ocean goes on, and the next region is solid ground when you
reach it. Chart coordinates map to double-precision geographic positions (y stays altitude, as the flight model
simulates it). A handoff keeps the geographic position, altitude, speed, attitude, fuel and the flight's state; it is
a change of chart, not an ECEF rebase, and physics stays local to the chart.

From takeoff, the regions ahead (by heading or route) are prepared off-thread: terrain, navigation, roads and bridges
on the CPU, then the GPU data staged at most 2 MiB a frame in 256 KiB calls. Only a ready candidate is published,
after a nonblocking drain of the scenery readers; stale work is retired off-thread. The handoff has ownership
hysteresis and needs the aircraft clear over open ocean; the far geometry of the active, candidate and previous
regions stays drawn, with a dithered near/far blend.

If full detail is late, the drawn coarse surface has a paired collision grid of the same triangles and shoreline, so
the ground you see is the ground you hit. An aircraft low over it pins the version it is on; a newer surface is held
back if it would put terrain under the aircraft, and full terrain and airport facilities are promoted only once the
aircraft is safely clear of both. The fallback has no buildings or airport services.

The horizon (`distant_regions`) samples the regions' recipes and Solace's base into 129-square silhouette meshes,
with no heightmaps, roads, trees or towns: an immutable centre catalogue under 320 KiB, selection in every direction
with 3 km of altitude lookahead, and a hard cap of 64 groups (reported as incomplete when it truncates). A coarse mesh
is at most 4.125 MiB of GPU triangles, and 64 collision counterparts at most 16.25 MiB. Visual curvature uses the
same 6,371 km Earth radius as the catalogue.

## Tests and tools

- `procedural_regions_test`: deterministic recipes, canonical ids, seam and pole discovery, distinct landforms,
  protected airport and town footprints and dry routes.
- `procedural_landforms_test`: atoll asymmetry, lagoon passages, dry reef arcs, branched fjords, profile continuity,
  exact ocean edges, memory and concurrent sampling.
- `procedural_world_test`: the generated terrain, roads, lots and facilities, flatness, approaches, collisions,
  memory, real Plane crossings, and each region's sights and their landmarks (`--recipe-only --region all` for a
  cheap pass).
- `region_scenery_isolation_test`, `region_scenery_test`, `distant_regions_test`, `world_upload_budget_test`: world
  ownership, the far silhouettes and the staging budgets.
- `procedural_region_runtime_test`: real Free Flight loading, flights, cancellation and restoration with saves; the
  career's travel, a region's board, a v7 save, the world each screen stands in, and a long haul flown across and
  delivered.
- `tools/validation/procedural_world_report.py` and `tools/validation/procedural_render_report.py`: isolated-process
  cache comparisons and fingerprinted renders (`ui_review_harness region-<id>` captures a region scene).
