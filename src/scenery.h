// Air Xpress - scenery: towns, roads, farmland, forests, rocks and procedural buildings.
// Everything here is evaluated identically on the CPU (collisions) and in the GPU ray tracer.
#pragma once
#include "world.h"

static const int MASK_N = 2048;
static const float MASK_TEXEL = 2.0f * WORLD_HALF / MASK_N;
static const float LOT = 28.0f;          // building lot grid (m)
static const float TREE_CELL = 10.0f;    // one tree per cell at most
static const float ROCK_CELL = 16.0f;
static const float STACK_CELL = 70.0f;
static const float ROAD_RANGE = 80.0f;
static const float SCENERY_DENSITY = 0.3333f;  // trees, rocks and sea stacks per cell (matches the shader)   // mask R channel encodes road distance 0..80 m

struct Town { const char* name; float x, z, r; int kind; };  // kind 0 village, 1 town, 2 city
extern const Town kTowns[];
extern const int kNumTowns;
struct RoadSeg { float ax, az, bx, bz; };
extern std::vector<RoadSeg> g_roads;

float roadDistance(float x, float z, int* segOut = nullptr);
float valueNoise(float x, float z);               // same as GLSL vnoise()
float coverFbm(float x, float z, int oct);        // same as GLSL fbm2()
void sceneryBaseMod(float x, float z, float& h, float& amp);  // flatten roads / towns in the base map

// Cover kinds returned by World::cover
enum CoverKind { COV_NONE = 0, COV_CONIFER, COV_BROADLEAF, COV_PALM, COV_ROCK, COV_STACK };

struct Lot { bool present; float cx, cz, hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; };
