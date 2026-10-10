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
extern std::vector<RoadSeg> g_roads;

// A settlement's street axes follow its incoming road. Block dimensions are whole lots,
// uploaded unchanged to the terrain material; these affect scenery, never ground height.
struct CommunityPlan { float cosine, sine; int blockX, blockZ; };
extern std::vector<CommunityPlan> g_communityPlans;
int communityAt(float x, float z);
vec2 communityLocal(int town, float x, float z);
vec2 communityWorld(int town, float x, float z);
float communityStreetDistance(int town, float x, float z, float* yaw = nullptr);
bool communityPark(int town, float localX, float localZ);
bool communityLot(const World& world, int town, int i, int j, struct Lot& out);
// Called after the immutable heightfield is available, including a cache hit.
void sceneryConnectRoads(const World& world);
void sceneryBakeCommunityLots(const World& world); // setup-only: immutable O(1) streaming lookup

float roadDistance(float x, float z, int* segOut = nullptr);
float valueNoise(float x, float z);               // same as GLSL vnoise()
float coverFbm(float x, float z, int oct);        // same as GLSL fbm2()
void sceneryInit();  // build shared road tables before any threaded use
void sceneryBaseMod(float x, float z, float& h, float& amp);  // flatten roads / towns in the base map


struct Lot { bool present; float cx, cz, hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; float yaw; int town; };
