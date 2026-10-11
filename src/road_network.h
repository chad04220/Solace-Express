// Solace Express - the islands' road network: highways between the cities and towns, roads to every village and
// airfield, each routed over the terrain (slope, water, airfields) and given a graded profile, with bridges where it
// crosses water or a deep valley. Generated with the world (World::build) and kept in its cache; the terrain takes the
// graded profile under every road (terrainH, common.glsl / World::height), the ground material paints it.
#pragma once
#include "common.h"
#include <vector>
#include <cstdint>

class World;

enum RoadClass : uint8_t { RC_HIGHWAY = 0, RC_ROAD = 1, RC_LANE = 2, RC_TRACK = 3, RC_COUNT };

// Cross-section of each class (m): the paved half width (edge line to edge line, the central reserve included), the
// level platform's half width (shoulders, verges), and the grade its routes are found at (a lane or a track may pitch
// to 1.6 times it for a stretch, rather than leave the hillside it is on)
struct RoadSpec { float halfPaved, halfPlatform, maxGrade, minRadius, spacing; };
inline const RoadSpec& roadSpec(int c) {
  static const RoadSpec k[RC_COUNT] = {
    {12.4f, 16.0f, 0.06f, 450.f, 25.f},   // highway: two carriageways of two 3.6 m lanes, 3 m shoulders, 4 m reserve
    {4.6f, 6.5f, 0.09f, 140.f, 20.f},     // road: two 3.4 m lanes, 0.9 m shoulders
    {2.9f, 4.0f, 0.12f, 45.f, 15.f},      // lane: a single 5.8 m carriageway, no lines
    {1.9f, 2.8f, 0.15f, 25.f, 12.f},      // track: gravel
  };
  return k[c];
}

struct RoadPoint { float x, z, h; };   // the centreline and its graded height
struct RoadPath {
  RoadClass cls;
  std::vector<RoadPoint> pts;
  std::vector<uint8_t> bridge;   // per segment (pts[i] -> pts[i+1]): 1 on a bridge
  int from = -1, to = -1;        // the nodes it joins (RoadNetwork::nodes), -1 for a point on another road
};
struct RoadNode { float x, z; int kind; int island; };   // kind: 0 village, 1 town, 2 city, 3 airfield

struct RoadNetwork {
  std::vector<RoadNode> nodes;
  std::vector<RoadPath> paths;
};

// Deterministic for a given world (its heightfield and airports); takes a few seconds.
RoadNetwork buildRoadNetwork(const World& world);

// ---- the network as the terrain, the ground material and the scenery read it: straight graded segments (each path's
// points with the collinear ones dropped), listed under every 39 m mask texel that their platform or banks reach
enum : uint8_t { RS_BRIDGE = 1, RS_NOGRADE = 2 };   // a bridge span (the ground below left alone, no paint); a stretch by
                                                    // an airfield's grounds (painted on the natural ground)
static const float ROAD_BANK_MAX = 40.f;            // the widest cut or fill bank beyond a platform's edge (m)
struct RoadSegment { float ax, az, bx, bz, ah, bh, along; uint16_t path; uint8_t cls, flags; };
struct RoadGrid {
  static const int N = 2048;                        // (the mask's texels: scenery.h MASK_N)
  std::vector<RoadSegment> segs;
  // per texel, uploaded as it is (R32UI, common.glsl): the mask's forest noise in the top 7 bits (World::bakeMask), how
  // many entries in the next 6, the first of them in the low 19
  std::vector<uint32_t> head;
  std::vector<uint32_t> list;                       // the entries: segment indices
  // a coarser index for the nearest road further off (scenery: farmsteads up to 480 m from theirs): 500 m cells, each
  // listing the segments within 500 m of it
  static const int NC = 160;
  std::vector<uint32_t> nearHead, nearList;         // (first << 8 | count; count < 256)
  static int cell(float v);
  static uint32_t count(uint32_t h) { return (h >> 19) & 63u; }
  static uint32_t first(uint32_t h) { return h & 0x7FFFFu; }
};
void buildRoadGrid(const RoadNetwork& net, RoadGrid& grid);
// The ground at (x, z) with the roads built into it, from the natural ground g there: each road's platform level at its
// graded height, its banks blending back to g. Exactly common.glsl roadGrade.
float roadGrade(const RoadGrid& grid, float x, float z, float g);
// The nearest road within 500 m: how far its platform's edge is (negative: on it), the segment (-1 and 1e9: none)
float roadEdgeDistance(const RoadGrid& grid, float x, float z, int* seg = nullptr);
// The grid's entries as the shaders read them (roads.glsl roadTexel): two RGBA32F texels each, in list order - rows 1..
// of a 4096-wide uData (row 0 is the caller's own); the floats of those rows, padded to whole rows
static const int ROAD_DATA_W = 4096;
std::vector<float> roadEntryRows(const RoadGrid& grid);
