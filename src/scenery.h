// Somnium Volandi - scenery masks: towns, roads, farmland and forest patches, evaluated identically on the CPU
// (entity placement) and in the shaders (ground materials). Trees, rocks and buildings: entities.h
#pragma once
#include "world.h"
#include "entities.h"
#include <unordered_map>

static const int MASK_N = 2048;
static const float MASK_TEXEL = 2.0f * WORLD_HALF / MASK_N;
static const float LOT = 28.0f;          // building lot grid (m)
static const float TREE_CELL = 10.0f;    // one tree per cell at most
static const float ROCK_CELL = 16.0f;
static const float STACK_CELL = 70.0f;
static const float ROAD_RANGE = 80.0f;

extern const Town kTowns[];
extern const int kNumTowns;
struct RoadSeg { float ax, az, bx, bz; };

// The settlement at a place (settlements.h: as far as each has grown; before they have, within its radius), -1 none
int communityAt(float x, float z);
void sceneryBakeCommunityLots(World& world); // the lots and the planted trees, once the world is built or loaded

// From the nearest road of g_world's network within 500 m (1e9: none): its platform's edge, plus 6 m. segOut: the
// segment (World::roadGrid.segs)
float roadDistance(float x, float z, int* segOut = nullptr);
float valueNoise(float x, float z);               // same as GLSL vnoise()
float coverFbm(float x, float z, int oct);        // same as GLSL fbm2()
// The farmland's field at a place, as the terrain draws its patchwork (terrain_material.glsl fieldMaterial, in step):
// border, how far into its field (0 on the hedge between two fields, the hedge drawn under 0.035), and its crop
// (0..1: under 0.76 a crop, ploughland or an orchard row; above, a pasture)
void fieldPatch(float x, float z, float& border, float& crop);
// Entities placed once the world is built or loaded, each chunk's handed to it as it is generated: sorted by the chunk
// their centres lie in
struct PlacedEnt { int kind; Ent e; };
struct PlacedByChunk {
  std::vector<PlacedEnt> items;
  std::vector<int> chunk;   // (each item's: cz * Scenery::NC + cx, ascending)
  void build(const std::vector<PlacedEnt>& all);
  std::pair<const PlacedEnt*, const PlacedEnt*> in(int cx, int cz) const;
};
// The road furniture (road_furniture.cpp; docs/LIVING_ISLANDS_PLAN.md, Phase A item 6) - street lamps, guardrails,
// direction signs, poles and their wires - laid along the network (after its bridges)
using RoadFurniture = PlacedEnt;
void sceneryBakeRoadFurniture(World& world);
const std::vector<RoadFurniture>& roadFurniture();
const std::vector<RoadFurniture>& roadFurniture(const World& world);
std::pair<const RoadFurniture*, const RoadFurniture*> roadFurnitureIn(const World& world, int cx, int cz);
std::pair<const RoadFurniture*, const RoadFurniture*> roadFurnitureIn(int cx, int cz);   // (the items centred in that chunk)
// The landmarks (landmarks.cpp; Phase C), each where the world has room for it: a wind farm along a ridge, a stone
// circle on a bare summit, a wreck on a reef, an offshore platform. Their clearings (x, z, radius) are kept clear of
// trees, bushes and boulders
void sceneryBakeLandmarks(World& world);
const std::vector<PlacedEnt>& landmarks();
const std::vector<PlacedEnt>& landmarks(const World& world);
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(const World& world, int cx, int cz);
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(int cx, int cz);
const std::vector<vec3>& landmarkClearings();
const std::vector<vec3>& landmarkClearings(const World& world);
// Whether a sea stack may stand at (x, z), in the sea: off a cliffed shore - land within 220 m of it rising 14 m within
// 40 m of the water's edge (or 26 m within 100 m): a headland, a cliff's foot - never out in an open bay or off a beach
bool seaStackShore(float x, float z);
// The cities' landmarks, placed with their lots (sceneryBakeCommunityLots): Solace Capital's stadium, Port Verde's
// container terminal - its quay, cranes, container stacks and a ship alongside
const std::vector<PlacedEnt>& civicLandmarks();
const std::vector<PlacedEnt>& civicLandmarks(const World& world);
// Lighthouse Key's light (entities.cpp): its place in that world, and which way it looks out to sea
bool findLighthouseSite(const World& world, float& x, float& z, float& yaw);
// The Fjordhaven dam's gorge and its lake's level (world.h Reservoir), on the natural ground once the roads are laid
// (World::build); and the heightmap texels its water reaches (indices into World::hm's texels; empty: it would spill)
bool findReservoir(const World& world, Reservoir& out);
std::vector<int> reservoirTexels(const World& world, const Reservoir& r);
// Its lake's mask (world.h LakeMask) over its square, from the natural ground: false if the water would reach the
// square's edge (spill)
bool reservoirMask(const World& world, const Reservoir& r, LakeMask& out);
// The ferries (Phase D): a route over the sea between two coastal towns on different islands, from harbour to harbour -
// as few routes as join every island, the shortest - found with the landmarks; one ferry on each, on a timetable (its
// place a matter of the time alone: the same on every run), and the yachts moored off each coastal town (landmarks()).
// At each end a terminal: a causeway from the shore out in 40 m lengths (EK_FERRYPIER), the linkspan at its head
// (EK_LINKSPAN), the berth beyond it - the route's first (last) point, where she lies end-on to the linkspan - and her
// way out straight to sea from it (the route's second point)
struct FerryPier { vec2 root, dir; int lengths = 0; };   // (its landward end, the way out along it, its 40 m lengths)
static const float kFerryBerth = 68.f;                   // (the berth's way beyond the causeway's end: the ramp, half her)
struct FerryRoute { int townA = -1, townB = -1; std::vector<vec2> pts; std::vector<float> along; FerryPier pier[2]; };
const std::vector<FerryRoute>& ferryRoutes();
const std::vector<FerryRoute>& ferryRoutes(const World& world);
static const float kFerrySpeed = 9.f, kFerryDwell = 150.f;   // (m/s: 17.5 knots; s at each end)
// where the ferries are at time t (s): each its route's EK_FERRY, at the waterline, her bows along her route (she runs
// both ways); docked: true while she waits at a harbour
void ferryInstances(const World& world, float t, std::vector<std::pair<int, Ent>>& out);
void ferryInstances(float t, std::vector<std::pair<int, Ent>>& out);
bool ferryAt(const FerryRoute& r, float t, vec2& at, vec2& heading, bool* docked = nullptr);
// Stop pending chunk work and invalidate the active region's entity caches before replacing g_world.
// Call with rendering and all new request/ensure/pump operations disabled.
void sceneryResetWorldCaches();
void sceneryInit();  // build shared road tables before any threaded use
void sceneryBaseMod(float x, float z, float& h, float& amp);  // flatten roads / towns in the base map


// A building's place in a settlement: its centre, its half frontage and depth (along its own x and z: its front, +z,
// faces the road), the ground it stands on, its kind (entities.h EK_), the settlement, the road it fronts
// (World::roads.paths) and how far out in the settlement (settlementShare)
struct Lot { bool present; float cx, cz, hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; float yaw; int town; int kind; int street; float share; };
const std::vector<Lot>& settlementLots();
const std::vector<Lot>& settlementLots(const World& world);
void lotsIn(const World& world, float x0, float z0, float x1, float z1, std::vector<int>& out);
void lotsIn(float x0, float z0, float x1, float z1, std::vector<int>& out);   // the lots whose centres lie in the box
// A tree planted in a settlement: on a street's pavement or in a park
struct TreeSpot { float x, z, scale; int kind; float seed; };
const std::vector<TreeSpot>& settlementTrees();
const std::vector<TreeSpot>& settlementTrees(const World& world);
void treesIn(const World& world, float x0, float z0, float x1, float z1, std::vector<int>& out);
void treesIn(float x0, float z0, float x1, float z1, std::vector<int>& out);

// Baked scenery belongs to its chart, so background construction never edits the
// active flight's lots, landmarks, routes or chunk indices. Treat published state
// as immutable; mutation is limited to the three sceneryBake functions above.
struct WorldSceneryState {
  std::vector<Lot> lots;
  std::vector<TreeSpot> trees;
  std::vector<PlacedEnt> civic;
  std::unordered_map<int64_t, std::vector<int>> lotCells, treeCells;
  PlacedByChunk furniture, landmarks;
  std::vector<vec3> clearings;
  std::vector<FerryRoute> ferries;
};
const WorldSceneryState& sceneryState(const World& world);
// Detaches state shared by a copied World before a bake. A new World allocates its
// state lazily; callers must finish baking before publishing that World to readers.
WorldSceneryState& sceneryStateForBuild(World& world);
