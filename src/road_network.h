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
// level platform's half width (shoulders, verges), and the steepest grade it is laid at.
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
