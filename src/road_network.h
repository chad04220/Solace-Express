// Solace Express - the islands' road network: highways between the cities and towns, roads to every village and
// airfield, each routed over the terrain (slope, water, airfields) and given a graded profile, with bridges where it
// crosses water or a deep valley. Generated with the world (World::build) and kept in its cache; the terrain takes the
// graded profile under every road (terrainH, common.glsl / World::height), the ground material paints it.
#pragma once
#include "common.h"
#include <vector>
#include <cstdint>

class World;

enum RoadClass : uint8_t { RC_HIGHWAY = 0, RC_ROAD = 1, RC_LANE = 2, RC_TRACK = 3, RC_STREET = 4, RC_COUNT };

// Each class's design (m): the paved half width (edge line to edge line, the central reserve included), the level
// platform's half width (shoulders, verges); the steepest grade anywhere along it (rise over run, never exceeded: its
// routes are found to hold it, its profile clamped to it); the tightest curve it is laid out with (a hairpin up a
// mountainside aside, where the route leaves no room); the points' spacing along a straight (closer round a curve);
// and its vertical curves' K - the metres of road over which the grade may change by one percent, crests and sags
// rounded over that much road rather than kinked
struct RoadSpec { float halfPaved, halfPlatform, maxGrade, minRadius, spacing, curveK; };
inline const RoadSpec& roadSpec(int c) {
  static const RoadSpec k[RC_COUNT] = {
    {12.4f, 16.0f, 0.06f, 450.f, 25.f, 40.f},   // highway: two carriageways of two 3.6 m lanes, 3 m shoulders, 4 m reserve
    {4.6f, 6.5f, 0.09f, 140.f, 20.f, 18.f},     // road: two 3.4 m lanes, 0.9 m shoulders
    {2.9f, 4.0f, 0.12f, 45.f, 15.f, 7.f},       // lane: a single 5.8 m carriageway, no lines
    {1.9f, 2.8f, 0.15f, 25.f, 12.f, 4.f},       // track: gravel
    {3.5f, 6.0f, 0.12f, 20.f, 12.f, 3.f},       // street: two 3.5 m lanes between kerbs, 2.5 m pavements (settlements.h)
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
// A road of class cls along a line laid out already (a settlement's street, settlements.cpp): its corners rounded, its
// points spaced and its profile graded as the network's own are, its ends at h0 and h1 - and wherever a pin is, a
// point exactly there held at the pin's height (where it crosses a road built before it: the two meet level). Never a
// bridge.
struct RoadPin { float x, z, h; };
RoadPath layRoad(const World& world, RoadClass cls, const std::vector<vec2>& line, float h0, float h1, const std::vector<RoadPin>& pins);
// Whether a stretch of road (A to B, of class cls) runs by an airfield's grounds - its platform or banks would reach
// them: there it lies on the ground as it is, painted, never graded (an airfield's terrain is never reshaped), its
// profile held on that ground
bool roadByAirfield(const RoadPoint& A, const RoadPoint& B, int cls);

// ---- the network as the terrain, the ground material and the scenery read it: straight graded segments (each path's
// points with the collinear ones dropped), listed under every 39 m mask texel that their platform or banks reach
// A segment's flags: a bridge span (the ground below left alone, no paint); a stretch by an airfield's grounds (painted on
// the natural ground); an end - a, b - where the road runs onto a bridge: the platform stops square at the joint (not
// in a round end under the deck), carried on a little under the deck's slab, then its fill falls away under the first
// span like a spill-through abutment's
enum : uint8_t { RS_BRIDGE = 1, RS_NOGRADE = 2, RS_ENDA = 4, RS_ENDB = 8 };
static const float ROAD_BANK_MAX = 50.f;            // the widest cut or fill bank beyond a platform's edge (m)
// (a street's narrower: it follows the ground closely, and a town's grid of them would crowd the texels' lists)
inline float roadBankMax(int cls) { return cls == RC_STREET ? 20.f : ROAD_BANK_MAX; }
static const float ROAD_BANK_RUN = 2.f;             // a bank's run for its rise (1 in 2, on average: rounded at its top
                                                    // and toe, at most 1 in 1.33 at its middle)
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
// The ground at (x, z) with the roads built into it, from the natural ground g there: each road's platform level across
// at its graded height, its cut or fill banks blending back to g. Exactly roads.glsl roadGrade.
float roadGrade(const RoadGrid& grid, float x, float z, float g);
// The nearest road within 500 m: how far its platform's edge is (negative: on it), the segment (-1 and 1e9: none)
float roadEdgeDistance(const RoadGrid& grid, float x, float z, int* seg = nullptr);
// The grid's entries as the shaders read them (roads.glsl roadTexel): two RGBA32F texels each, in list order - rows 1..
// of a 4096-wide uData (row 0 is the caller's own); the floats of those rows, padded to whole rows. The second's w:
// class + 8 x flags + 128 x path
static const int ROAD_DATA_W = 4096;
std::vector<float> roadEntryRows(const RoadGrid& grid);
