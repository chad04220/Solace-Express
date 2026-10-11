// The traffic on the road network (road_traffic.h; docs/LIVING_ISLANDS_PLAN.md, Phase D): cars and lorries round the
// camera on the highways, roads, lanes and streets, each in its lane on the right of its road and on its surface, at its
// lane's pace; on through the junctions; coming and going out of sight; and never two in one place.
#include "../src/road_traffic.h"
#include "../src/world.h"
#include "../src/scenery.h"
#include "test_world.h"
#include <cmath>
#include <cstdio>
#include <map>

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { if (failures < 20) std::printf("FAIL: %s\n", what); failures++; } }

int main() {
  buildTestWorld();
  const World& w = g_world;
  // (each lane's car - 1.8 m wide - on its road's paving)
  for (int c : {RC_HIGHWAY, RC_ROAD, RC_LANE, RC_STREET})
    for (int lane = 0; lane < (c == RC_HIGHWAY ? 2 : 1); lane++)
      check(RoadTraffic::laneOffset(c, lane) - 0.9f > (c == RC_HIGHWAY ? 2.f : 0.f) && RoadTraffic::laneOffset(c, lane) + 0.9f < roadSpec(c).halfPaved, "a lane on its road's paving, right of its centre");
  // round Solace Capital and out along the highway, the camera flying over at 60 m/s
  const vec2 start(-3200.f, -1200.f), end(-12000.f, -9000.f);
  RoadTraffic T;
  std::map<uint32_t, RoadCar> last;
  std::map<uint32_t, vec3> lastAt;
  int steps = 0, junctions = 0, cars = 0, offLane = 0, offSurface = 0, wrongSide = 0, jumps = 0, nearSpawn = 0, crowded = 0, lorriesFast = 0;
  float seen = 0.f;
  const float dt = 0.1f, flight = length(end - start) / 60.f;
  for (float t = 0.f; t <= flight; t += dt, steps++) {
    const vec2 c2 = start + (end - start) * (t / flight);
    const vec3 cam(c2.x, w.height(c2.x, c2.y) + 300.f, c2.y);
    T.update(w, cam, dt);
    std::map<uint32_t, vec3> now;
    for (const RoadCar& c : T.all()) {
      vec3 at; vec2 h;
      check(T.place(w, c, at, h), "a car somewhere");
      now[c.id] = at;
      const RoadPath& p = w.roads.paths[c.path];
      if (steps % 50 == 0) {
        cars++;
        // on its road: in its lane, to the right of the centreline as it drives, on the road's surface off the bridges
        int k = 0; float best = 1e9f, side = 0.f;
        for (size_t q = 0; q + 1 < p.pts.size(); q++) {
          const vec2 A(p.pts[q].x, p.pts[q].z), B(p.pts[q + 1].x, p.pts[q + 1].z), d = B - A;
          const float u = std::clamp(dot2(vec2(at.x, at.z) - A, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
          const vec2 off = vec2(at.x, at.z) - (A + d * u);
          if (length(off) < best) { best = length(off); k = (int)q; side = dot2(off, vec2(-h.y, h.x)); }
        }
        offLane += fabsf(best - RoadTraffic::laneOffset(p.cls, c.lane)) > 0.6f;
        wrongSide += side < 0.f;
        if (!p.bridge[k] && !(k > 0 && p.bridge[k - 1]) && !(k + 1 < (int)p.bridge.size() && p.bridge[k + 1])) offSurface += fabsf(at.y - w.height(at.x, at.z)) > 0.35f;
        if (c.kind != EK_CAR && p.cls == RC_HIGHWAY) lorriesFast += c.lane != 1;
        // (no other car within 6 m of it on its own road, its way)
        for (const RoadCar& o : T.all()) crowded += o.id < c.id && o.path == c.path && o.dir == c.dir && o.lane == c.lane && fabsf(o.s - c.s) < 6.f;
      }
      auto it = last.find(c.id);
      if (it == last.end()) {   // (a new one: out of sight once the ring has filled)
        if (steps > 0) nearSpawn += hypotf(at.x - cam.x, at.z - cam.z) < RoadTraffic::kSpawnMin - 1.f;
        continue;
      }
      junctions += it->second.path != c.path;
      const float moved = length(vec2(at.x, at.z) - vec2(lastAt[c.id].x, lastAt[c.id].z));
      jumps += moved > RoadTraffic::speedOf(p.cls, c.lane, c.kind) * dt * 1.2f + 12.f;   // (a corner's lane offset at the most)
      seen = std::max(seen, (float)T.all().size());
    }
    last.clear(); for (const RoadCar& c : T.all()) last[c.id] = c;
    lastAt = now;
  }
  std::printf("road traffic: up to %.0f cars round the camera; %d looked at: %d out of their lane, %d on the wrong side, %d off the road's surface, %d crowded, %d lorries in a highway's fast lane; %d through junctions, %d jumps, %d appearing in sight\n",
              seen, cars, offLane, wrongSide, offSurface, crowded, lorriesFast, junctions, jumps, nearSpawn);
  check(seen >= 200 && seen <= RoadTraffic::kMost, "a city's traffic round the camera, within its budget");
  check(offLane == 0 && wrongSide == 0, "each car in its lane, on the right of its road");
  check(offSurface * 50 <= cars, "each car on its road's surface");
  check(lorriesFast == 0, "the lorries in a highway's slow lane");
  check(crowded * 100 <= cars, "never two cars in one place");
  check(junctions > 20, "the cars on through the junctions");
  check(jumps == 0, "each car on along its road from one step to the next");
  check(nearSpawn == 0, "new cars come out of sight");
  std::printf("road_traffic: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
