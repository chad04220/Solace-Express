// Solace Express - the traffic on the road network (docs/LIVING_ISLANDS_PLAN.md, Phase D): cars and lorries driving the
// highways, roads, lanes and streets around the camera, each on the right of its road at its lane's speed, carrying on
// through the junctions (onto a road that meets its end, or along the one its road ends on) and turning back at a dead
// end. Only drawn, never flown into: they come and go out of sight - past the 1.6 km a car is drawn to - and leave beyond
// the ring they're kept in. Their headlights and tail lights burn at night (ent_fs2.glsl: an instance's seed of 2 and
// more; and at range a glint each, as the runway lights have: Game's sprites).
#pragma once
#include "entities.h"
#include <cstdint>
#include <vector>

class World;

struct RoadCar {
  int path = -1;      // the road it's on (RoadNetwork::paths)
  float s = 0;        // how far along it (m, from its start)
  int dir = 1;        // which way it drives: +1 up the road's points, -1 down them
  int lane = 0;       // 0 its class's inside lane (a highway's fast lane), 1 the outside (a highway's slow lane)
  int kind = EK_CAR;  // EK_CAR, or EK_TRANSPORTER (a lorry)
  float seed = 2.f;   // its look (paint, livery): 2 and more, which lights it at night
  uint32_t id = 0;    // (which car: numbered as they come)
};

class RoadTraffic {
public:
  // Bring the traffic round cam up to date by dt seconds: each car on along its road, those gone too far away dropped,
  // new ones added out of sight until the ring holds its share of each road. Deterministic for a given world, camera
  // path and steps
  void update(const World& world, vec3 cam, float dt);
  void clear() { cars.clear(); }
  // Each car as the scenery draws it (its kind, and an Ent: on the road's surface, facing its way, seed >= 2)
  void instances(const World& world, std::vector<std::pair<int, Ent>>& out) const;
  const std::vector<RoadCar>& all() const { return cars; }
  // Where a car is: its position on the road's surface, its heading (unit, level), and the path's class
  bool place(const World& world, const RoadCar& c, vec3& at, vec2& heading) const;
  static float speedOf(int cls, int lane, int kind);   // its lane's speed (m/s)
  static float laneOffset(int cls, int lane);          // its lane's centre, right of the road's centreline (m)
  static constexpr float kRing = 2000.f;               // the cars kept within this of the camera
  static constexpr float kSpawnMin = 1650.f;           // and added no nearer than this (once the ring has filled): unseen
  static constexpr float kDrawn = 1600.f;              // a car drawn to this far (entity_lod.h entRangeOf), lorries too
  static constexpr int kMost = 1200;                   // the most kept
private:
  struct End { int path; float s; int dir; };           // a way on from a road's end
  void index(const World& world);
  std::vector<RoadCar> cars;
  const void* netKey = nullptr; size_t netN = 0;
  std::vector<std::vector<float>> along;                // each path's distance along at each point
  std::vector<std::vector<End>> onFrom[2];              // each path's ways on from its start (0) and its end (1)
  std::vector<uint8_t> drivable;                        // each path: cars on it
  std::vector<std::vector<int>> cellPaths;              // per 500 m cell, the drivable paths through it
  uint32_t rng = 12345u, ids = 0;
  bool filled = false;          // (the ring filled round the camera: from then on cars come only out of sight)
  vec2 lastCam = vec2(1e9f, 1e9f);   // (a jump of 400 m and more - a new flight, a teleport: filled again from scratch)
};
extern RoadTraffic g_roadTraffic;
