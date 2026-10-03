// Air Xpress - scenery masks: towns, roads, farmland and forest patches, evaluated identically on the CPU
// (entity placement) and in the GPU ray tracer (ground materials). Trees, rocks and buildings: entities.h
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

float roadDistance(float x, float z, int* segOut = nullptr);
float valueNoise(float x, float z);               // same as GLSL vnoise()
float coverFbm(float x, float z, int oct);        // same as GLSL fbm2()
void sceneryInit();  // build shared road tables before any threaded use
void sceneryBaseMod(float x, float z, float& h, float& amp);  // flatten roads / towns in the base map


struct Lot { bool present; float cx, cz, hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; };
