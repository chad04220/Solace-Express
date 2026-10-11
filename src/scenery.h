// Solace Express - scenery masks: towns, roads, farmland and forest patches, evaluated identically on the CPU
// (entity placement) and in the shaders (ground materials). Trees, rocks and buildings: entities.h
#pragma once
#include "world.h"

static const int MASK_N = 2048;
static const float MASK_TEXEL = 2.0f * WORLD_HALF / MASK_N;
static const float LOT = 28.0f;          // building lot grid (m)
static const float TREE_CELL = 10.0f;    // one tree per cell at most
static const float ROCK_CELL = 16.0f;
static const float STACK_CELL = 70.0f;
static const float ROAD_RANGE = 80.0f;

struct Town { const char* name; float x, z, r; int kind; };  // kind 0 village, 1 town, 2 city
extern const Town kTowns[];
extern const int kNumTowns;
struct RoadSeg { float ax, az, bx, bz; };

// The settlement at a place (settlements.h: as far as each has grown; before they have, within its radius), -1 none
int communityAt(float x, float z);
void sceneryBakeCommunityLots(const World& world); // the lots and the planted trees, once the world is built or loaded

// From the nearest road of g_world's network within 500 m (1e9: none): its platform's edge, plus 6 m. segOut: the
// segment (World::roadGrid.segs)
float roadDistance(float x, float z, int* segOut = nullptr);
float valueNoise(float x, float z);               // same as GLSL vnoise()
float coverFbm(float x, float z, int oct);        // same as GLSL fbm2()
void sceneryInit();  // build shared road tables before any threaded use
void sceneryBaseMod(float x, float z, float& h, float& amp);  // flatten roads / towns in the base map


// A building's place in a settlement: its centre, its half frontage and depth (along its own x and z: its front, +z,
// faces the road), the ground it stands on, its kind (entities.h EK_), the settlement, the road it fronts
// (World::roads.paths) and how far out in the settlement (settlementShare)
struct Lot { bool present; float cx, cz, hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; float yaw; int town; int kind; int street; float share; };
const std::vector<Lot>& settlementLots();
void lotsIn(float x0, float z0, float x1, float z1, std::vector<int>& out);   // the lots whose centres lie in the box
// A tree planted in a settlement: on a street's pavement or in a park
struct TreeSpot { float x, z, scale; int kind; float seed; };
const std::vector<TreeSpot>& settlementTrees();
void treesIn(float x0, float z0, float x1, float z1, std::vector<int>& out);
