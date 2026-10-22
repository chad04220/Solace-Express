// Somnium Volandi - the settlements' extent and streets (docs/LIVING_ISLANDS_PLAN.md, Phase B). Each settlement grows
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
// A farmstead's yard: its centre, the way in from its track (unit), a seed (entities.cpp stands the buildings round it)
struct Farmstead { float x, z, dx, dz, seed; };

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
// settlementShare for the many places of a pass over fields that stay as they are: the fields' extents on a grid, each
// cell listing those reaching it in their order - so the same answer, its ties as theirs, from a few fields not all
struct SettlementLookup {
  explicit SettlementLookup(const std::vector<SettlementField>& fields);
  float share(float x, float z, int* town = nullptr) const;
private:
  static constexpr float C = 500.f;
  const std::vector<SettlementField>& fields;
  float x0 = 0.f, z0 = 0.f, x1 = -1.f, z1 = -1.f;   // (the extents' bounds: none beyond them)
  int nx = 0, nz = 0;
  std::vector<int> first, ids;                      // (cell k's fields: ids[first[k]] .. ids[first[k + 1]])
};
// The settlements' streets, laid out and added to the network
void addSettlementStreets(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net);
// The farmsteads out in the farmland, clear of the settlements and the airfields: each a level yard (a short path of
// class RC_YARD, its ground levelled and banked as a road's is) at the end of a gravel track (RC_TRACK) from the
// nearest road, lane or neighbour's track. After the streets; the farms are read back off the network (farmsteadsOf),
// cached or not
void addFarmsteads(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net);
std::vector<Farmstead> farmsteadsOf(const RoadNetwork& net);

// The racetrack (docs/LIVING_ISLANDS_PLAN.md, Phase C): a circuit of 3.9 km (class RC_CIRCUIT, one closed path) laid
// where the ground is level enough for it, out in the country, with its pit lane (RC_PITLANE) beside the main straight
// and a road from its paddock to the nearest road. After the streets, before the farms (which keep clear of it). Its
// frame is read back off the network (racetrackOf), cached or not: o, the circuit's start - on its main straight at
// the pit lane's mouth - and u, the way it's raced there; a place's s along u from o, and v across it to the right
// (-u.y, u.x): the infield's side
void addRacetrack(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net);
struct Racetrack { bool found = false; vec2 o, u; int circuit = -1, pitLane = -1;
                   vec2 at(float s, float v) const { return o + u * s + vec2(-u.y, u.x) * v; } };
Racetrack racetrackOf(const RoadNetwork& net);
// The circuit's centreline in its own frame (s, v), closed: its first point its last
std::vector<vec2> circuitLine();
// Where it lies in its frame: the pits' side of the main straight (v < 0) - the pit lane's centre 26 m off the
// circuit's, between them the pit wall; the garages behind the pit lane, the paddock and its road behind them; across
// the track (v > 0) the grandstands
static const float RACE_PITLANE_V = -26.f, RACE_PITWALL_V = -17.25f, RACE_GARAGE_V = -43.5f, RACE_PADDOCK_V = -80.f, RACE_STAND_V = 36.f;
