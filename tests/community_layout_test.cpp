// The settlements (settlements.h, scenery.h): grown over the ground with their streets on the road network, every
// building on a lot along a road or a street and facing it, clear of the roads and of each other, on dry, buildable
// ground off the runways; a skyline at the cities' hearts; deterministic, bounded per chunk, streamed the same however
// it is reached. No GL or display required.
#include "../src/scenery.h"
#include "../src/entities.h"
#include "../src/airport_layout.h"
#include "test_world.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

static int failures = 0;
static void check(bool value, const char* message) { if (!value) { if (failures < 16) std::printf("FAIL: %s\n", message); failures++; } }
int main() {
  buildTestWorld();
  check(g_world.settlements.size() == (size_t)kNumTowns, "every settlement has grown");
  // The road network (road_network.h): every settlement and airfield on it, its roads on dry land or bridges, off the
  // runways' protected rectangles, graded within their class's limit
  const RoadNetwork& net = g_world.roads;
  check(!net.paths.empty() && !g_world.roadGrid.segs.empty(), "the road network is built");
  std::vector<int> joined(net.nodes.size(), 0);
  for (const RoadPath& p : net.paths) {
    if (p.from >= 0) joined[p.from]++;
    if (p.to >= 0) joined[p.to]++;
    check(p.pts.size() >= 2 && p.bridge.size() + 1 == p.pts.size(), "a path has points and a bridge flag per segment");
    float length = 0; for (size_t k = 1; k < p.pts.size(); k++) length += hypotf(p.pts[k].x - p.pts[k - 1].x, p.pts[k].z - p.pts[k - 1].z);
    // (by an airfield, and 100 m either side, the road lies on the airfield's ground as it is: never graded)
    std::vector<uint8_t> byAirfield(p.pts.size(), 0);
    {
      std::vector<float> along(p.pts.size(), 0.f);
      for (size_t k = 1; k < p.pts.size(); k++) along[k] = along[k - 1] + hypotf(p.pts[k].x - p.pts[k - 1].x, p.pts[k].z - p.pts[k - 1].z);
      for (size_t k = 0; k + 1 < p.pts.size(); k++)
        if (roadByAirfield(p.pts[k], p.pts[k + 1], p.cls))
          for (size_t q = 0; q < p.pts.size(); q++) byAirfield[q] = byAirfield[q] || (along[q] > along[k] - 100.f && along[q] < along[k + 1] + 100.f);
    }
    for (size_t k = 0; k < p.pts.size(); k++) {
      const RoadPoint& q = p.pts[k];
      check(std::isfinite(q.x + q.z + q.h), "finite road points");
      const bool onBridge = (k > 0 && p.bridge[k - 1]) || (k + 1 < p.pts.size() && p.bridge[k]);
      if (!onBridge) check(g_world.naturalHeight(q.x, q.z, 6) > -1.f, "roads off bridges stay on land");
      for (const Airport& airport : g_world.airports) {
        vec2 a = aptLocal(airport, vec3(q.x, 0, q.z));
        const bool inside = fabsf(a.x) < airport.length * .5f + 250.f && fabsf(a.y) < airport.width * .5f + 75.f;
        if (inside && failures < 40) std::printf("  in %s's protected rectangle: path %d->%d point %zu/%zu u %.0f v %.0f\n", airport.code, p.from, p.to, k, p.pts.size(), a.x, a.y);
        check(!inside, "roads avoid runway protected rectangles");
      }
      if (k > 0) {
        const RoadPoint& o = p.pts[k - 1];
        const float run = hypotf(q.x - o.x, q.z - o.z);
        check(run > 0.5f, "no zero-length road segments");
        // (ends too far apart in height for the road's length: the shortfall spread evenly along it)
        const float spread = fabsf(p.pts.back().h - p.pts.front().h) / std::max(length, 1.f);
        const bool steep = !byAirfield[k] && !byAirfield[k - 1] && fabsf(q.h - o.h) > (roadSpec(p.cls).maxGrade * 1.05f + spread) * run + 0.05f;
        if (steep) std::printf("  steep: path %d->%d class %d at %.0f %.0f: %.1f m over %.1f m\n", p.from, p.to, (int)p.cls, q.x, q.z, q.h - o.h, run);
        check(!steep, "roads graded within their class's limit");
      }
    }
  }
  int alone = 0;
  for (size_t n = 0; n < net.nodes.size(); n++) {
    if (joined[n]) continue;
    bool settled = false;   // (an airfield alone on its island - Gull Rock's strip - has nowhere to go)
    for (size_t m = 0; m < net.nodes.size(); m++) settled = settled || (m != n && net.nodes[m].kind < 3 && net.nodes[m].island == net.nodes[n].island);
    if (!settled && net.nodes[n].kind == 3) continue;
    alone++; std::printf("  not on the network: node %zu (kind %d) at %.0f %.0f\n", n, net.nodes[n].kind, net.nodes[n].x, net.nodes[n].z);
  }
  check(alone == 0, "every settlement and airfield is on the road network (an airfield alone on its island aside)");
  // ---- the streets: the network's, in every town and city
  int streets = 0; std::vector<int> townStreets(kNumTowns, 0);
  for (const RoadPath& p : net.paths) {
    if (p.cls != RC_STREET) continue;
    streets++;
    int t = -1; settlementShare(g_world.settlements, p.pts[p.pts.size() / 2].x, p.pts[p.pts.size() / 2].z, &t);
    if (t >= 0) townStreets[t]++;
    check(p.from < 0 && p.to < 0 && p.bridge.size() + 1 == p.pts.size(), "a street joins no node and is never a bridge");
  }
  for (int t = 0; t < kNumTowns; t++) if (kTowns[t].kind == 2) check(townStreets[t] >= 30, "a city has its streets");
  check(streets > 300, "the settlements have their streets");
  // ---- the lots
  RoadIndex roads;
  for (size_t pi = 0; pi < net.paths.size(); pi++) roads.addPath(net.paths[pi], (int)pi);
  const std::vector<Lot> lots = settlementLots();
  sceneryBakeCommunityLots(g_world);
  const std::vector<Lot>& again = settlementLots();
  bool same = lots.size() == again.size();
  for (size_t i = 0; same && i < lots.size(); i++) same = lots[i].cx == again[i].cx && lots[i].cz == again[i].cz && lots[i].yaw == again[i].yaw && lots[i].kind == again[i].kind;
  check(same, "the lots are laid out the same every time");
  int total = 0, tall = 0, occupiedTowns = 0, cars = 0;
  std::vector<int> count(kNumTowns, 0);
  for (size_t li = 0; li < lots.size(); li++) {
    const Lot& lot = lots[li];
    total++; tall += lot.wallH > 38.f; count[lot.town]++;
    int t = -1; const float share = settlementShare(g_world.settlements, lot.cx, lot.cz, &t);
    check(t == lot.town && share < 1.15f, "a lot lies in its own settlement");
    const float c = cosf(lot.yaw), s = sinf(lot.yaw);
    auto at = [&](float x, float z) { return vec2(lot.cx + c * x + s * z, lot.cz - s * x + c * z); };
    // its front on its road, a setback from the platform's edge; facing it
    const float front = roads.nearest(at(0.f, lot.hd), 16.f, -1, nullptr, nullptr, true);
    check(front >= 0.5f && front <= 13.f, "a building fronts a road, set back from it");
    if (lot.street >= 0) {   // (nearer its own road a step further out from its front)
      auto toStreet = [&](vec2 p) {
        const RoadPath& r = net.paths[lot.street]; float best = 1e9f;
        for (size_t k = 0; k + 1 < r.pts.size(); k++) {
          const vec2 a(r.pts[k].x, r.pts[k].z), d = vec2(r.pts[k + 1].x, r.pts[k + 1].z) - a;
          const float t = std::clamp(dot2(p - a, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
          best = std::min(best, length(a + d * t - p));
        }
        return best;
      };
      check(toStreet(at(0.f, lot.hd + 2.f)) < toStreet(at(0.f, lot.hd)), "a building faces its road");
    }
    float low = 1e9f, high = -1e9f;
    for (int k = 0; k < 9; k++) {
      const vec2 p = at((k % 3 - 1) * lot.hw, (k / 3 - 1) * lot.hd);
      const float h = g_world.groundHeight(p.x, p.y, 7); low = std::min(low, h); high = std::max(high, h);
      check(h >= 2.5f && g_world.onRunway(p.x, p.y, 60.f) < 0, "a footprint is dry and clear of the runways");
      check(roads.nearest(p, 3.f, -1, nullptr, nullptr, true) >= 0.99f, "no building stands on a road");
    }
    const bool big = lot.kind == EK_TOWER || lot.kind == EK_SKYSCRAPER || lot.kind == EK_OFFICE || lot.kind == EK_WAREHOUSE;
    check(high - low <= (big ? 3.5f : 2.6f) + .001f, "steep sites refused, the ground left as it is");
    // no two buildings in one another
    std::vector<int> near; lotsIn(lot.cx - 60.f, lot.cz - 60.f, lot.cx + 60.f, lot.cz + 60.f, near);
    for (int o : near) {
      if (o <= (int)li) continue;
      const Lot& b = lots[o];
      const vec2 ua(c, -s), va(s, c), ub(cosf(b.yaw), -sinf(b.yaw)), vb(sinf(b.yaw), cosf(b.yaw)), d(b.cx - lot.cx, b.cz - lot.cz);
      bool apart = false;
      for (vec2 ax : {ua, va, ub, vb}) {
        const float ra = lot.hw * fabsf(dot2(ax, ua)) + lot.hd * fabsf(dot2(ax, va)), rb = b.hw * fabsf(dot2(ax, ub)) + b.hd * fabsf(dot2(ax, vb));
        apart = apart || fabsf(dot2(d, ax)) > ra + rb - 0.01f;
      }
      check(apart, "no two buildings stand in each other");
    }
  }
  for (int town = 0; town < kNumTowns; town++) {
    std::printf("%-18s %4d buildings, %3d streets\n", kTowns[town].name, count[town], townStreets[town]);
    occupiedTowns += count[town] > 0;
    if (kTowns[town].kind == 2) check(count[town] > 1000, "a city is built up");
  }
  check(total > 4000 && total < 40000, "bounded, populated settlements");
  check(occupiedTowns == kNumTowns, "every settlement is lived in");
  check(tall > 60 && tall < total / 10, "the cities' skylines, at their hearts");
  // Detail upgrades and direct generation must yield the same entities. Every community item
  // owns its centre's chunk, including trees and parked cars that cross a lot/chunk boundary.
  for (int town : {0, 1, 5, 10}) {
    const Town& t = kTowns[town];
    for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
      int cx = Scenery::chunkOf(t.x) + ox, cz = Scenery::chunkOf(t.z) + oz;
      g_scenery.ensure(cx, cz, 1); Scenery::Chunk* chunk = g_scenery.ensure(cx, cz, 2);
      std::vector<Ent> upgraded = chunk->ents;
      for (int kind = 0; kind < EK_COUNT; kind++) for (uint32_t n = chunk->off[kind]; n < chunk->off[kind + 1]; n++) {
        const Ent& e = chunk->ents[n];
        check(Scenery::chunkOf(e.x) == cx && Scenery::chunkOf(e.z) == cz, "entities are owned by their centre's chunk");
        check(e.y > 0.f && std::isfinite(e.x + e.y + e.z + e.yaw), "settlement entities stay finite and above sea level");
        cars += kind == EK_CAR;
      }
      size_t buildings = chunk->off[EK_RWYLIGHT] - chunk->off[EK_HOUSE];
      check(buildings < 400, "a chunk's buildings bounded");
      check(chunk->ents.size() < 4000, "a chunk's entities bounded");
      g_scenery.clear(); chunk = g_scenery.ensure(cx, cz, 2);
      check(upgraded.size() == chunk->ents.size() && (upgraded.empty() || !std::memcmp(upgraded.data(), chunk->ents.data(), upgraded.size() * sizeof(Ent))), "streaming level upgrade matches direct detailed generation");
      g_scenery.clear();
    }
  }
  check(cars > 0, "communities contain driveway activity");
  std::printf("%d buildings, %d tall, %d inhabited towns, %zu road paths, %d parked cars sampled; %d failures\n", total, tall, occupiedTowns, g_world.roads.paths.size(), cars, failures);
  return failures ? 1 : 0;
}
