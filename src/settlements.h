// Solace Express - the settlements' extent and streets (docs/LIVING_ISLANDS_PLAN.md, Phase B). Each settlement grows
// over the ground from its centre and along the roads into it as far as building there is worth the effort - steep
// ground dear, a road cheap to build along, the sea and an airfield's grounds never - rather than filling a circle.
// Its streets: in a town or a city a grid at its core, turned to its main road; round that, and through a village,
// streets that branch off the roads and the grid and follow the lie of the land, ending in a turning or joining the
// next. They are the network's own roads (class RC_STREET: graded, painted, met level where they cross another) and
// cached with it; the extents are worked out again from the ground and the network whenever the world is built or
// loaded. The lots along them, and what stands on each: scenery.cpp.
#pragma once
#include "road_network.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

class World;

static const float SETTLE_CELL = 20.f;   // the effort field's cells (m)

struct SettlementField {
  int town = -1, kind = 0;            // kTowns' index; 0 village, 1 town, 2 city
  float cx = 0.f, cz = 0.f;           // its centre
  float x0 = 0.f, z0 = 0.f;           // the field's corner
  int n = 0;                          // its cells a side
  float budget = 0.f, core = 0.f;     // the effort at its edge, at its core's (m)
  float axisC = 1.f, axisS = 0.f;     // its grid's along-axis (x, z: the main road's direction at the centre)
  float gx = 0.f, gz = 0.f;           // a point its grid runs through (on the main road, nearest the centre)
  std::vector<float> effort;          // per cell: the cheapest way there (m of effort; 1e9 never)
  float at(float x, float z) const;   // the effort at a place (bilinear; 1e9 off the field)
};

// The network's roads as segments in 64 m cells, for asking what is near a place
struct RoadIndex {
  struct Seg { vec2 a, b; float ha, hb; int path; uint8_t cls; };
  static constexpr float C = 64.f;
  std::vector<Seg> segs;
  std::unordered_map<int64_t, std::vector<int>> cells;
  static int64_t key(int i, int j) { return (int64_t)i * 1000003 + j; }
  static int cell(float v) { return (int)floorf(v / C); }
  void add(const Seg& s);
  void addPath(const RoadPath& p, int pi);
  // the nearest segment within r, not of path `skip`: its distance - with `edge`, to its platform's edge (negative: on
  // it) - (1e9: none), which, and where along it
  float nearest(vec2 p, float r, int skip, int* seg = nullptr, float* t = nullptr, bool edge = false) const;
  // the places a to b crosses a road (not of path `skip`): there and that road's height there
  void crossings(vec2 a, vec2 b, int skip, std::vector<RoadPin>& out) const;
};
inline float dot2(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }

// The settlements' extents, from the ground and the network's roads (not its streets): deterministic
std::vector<SettlementField> buildSettlementFields(const World& world);
// How far out in its settlement a place lies - the effort there over its settlement's budget: 0 at the centre, 1 at
// the edge - for the settlement it lies furthest within (a large value: none); which one in *town
float settlementShare(const std::vector<SettlementField>& fields, float x, float z, int* town = nullptr);
// The settlements' streets, laid out and added to the network
void addSettlementStreets(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net);
