// Community planning is deterministic, bounded, aligned with the rendered street grid,
// and contains no water/slope/airport-overlapping buildings. No GL or display required.
#include "../src/scenery.h"
#include "../src/entities.h"
#include "../src/airport_layout.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

static int failures = 0;
static void check(bool value, const char* message) { if (!value) { if (failures < 16) std::printf("FAIL: %s\n", message); failures++; } }
int main() {
  g_world.build();
  check(g_communityPlans.size() == (size_t)kNumTowns, "every community has a shared street plan");
  check(!g_roads.empty() && g_roads.size() <= 64, "road ids stay inside renderer's fixed 64-segment budget");
  size_t countBefore = g_roads.size(); sceneryConnectRoads(g_world);
  check(g_roads.size() == countBefore, "access road initialization is idempotent");
  for (const RoadSeg& road : g_roads) {
    check(std::isfinite(road.ax + road.az + road.bx + road.bz), "finite road endpoints");
    check(hypotf(road.bx - road.ax, road.bz - road.az) > 1.f, "no zero-length road segments");
    // Forty surveyed legacy trunk segments precede all new access links.
    if (&road - g_roads.data() >= 40) for (int step = 0; step <= 200; step++) {
      float f = step / 200.f, x = lerpf(road.ax, road.bx, f), z = lerpf(road.az, road.bz, f);
      check(g_world.groundHeight(x, z, 7) > 1.5f, "new access links stay on dry land");
      for (const Airport& airport : g_world.airports) {
        vec2 q = aptLocal(airport, vec3(x, 0, z));
        check(fabsf(q.x) >= airport.length * .5f + 250.f || fabsf(q.y) >= airport.width * .5f + 75.f, "new access links avoid runway protected rectangles");
      }
    }
  }
  Lot invalid;
  check(!communityLot(g_world, -1, 0, 0, invalid) && !communityLot(g_world, kNumTowns, 0, 0, invalid), "invalid public town indices are rejected");
  int total = 0, tall = 0, occupiedTowns = 0, cars = 0;
  for (int town = 0; town < kNumTowns; town++) {
    const Town& t = kTowns[town]; const CommunityPlan& plan = g_communityPlans[town];
    check(fabsf(plan.cosine * plan.cosine + plan.sine * plan.sine - 1.f) < 2e-6f, "street frame is orthonormal");
    int range = (int)ceilf(t.r / LOT) + 1, count = 0;
    for (int j = -range; j <= range; j++) for (int i = -range; i <= range; i++) {
      Lot lot, again;
      if (!communityLot(g_world, town, i, j, lot)) continue;
      count++; total++; tall += lot.wallH > 38.f;
      check(communityLot(g_world, town, i, j, again) && lot.cx == again.cx && lot.cz == again.cz && lot.yaw == again.yaw && lot.wallH == again.wallH, "lot generation is deterministic");
      check(communityAt(lot.cx, lot.cz) == town, "lot stays in its own settlement");
      vec2 local = communityLocal(town, lot.cx, lot.cz), back = communityWorld(town, local.x, local.y);
      check(hypotf(back.x - lot.cx, back.y - lot.cz) < .01f, "local/world frame round trip");
      check(!communityPark(town, local.x, local.y), "town square is reserved from ordinary buildings");
      float d = communityStreetDistance(town, lot.cx, lot.cz);
      check(d <= 21.f && d > 10.f, "every building has street frontage and a setback");
      float forward = communityStreetDistance(town, lot.cx + sinf(lot.yaw) * 2.f, lot.cz + cosf(lot.yaw) * 2.f);
      check(forward < d - 1.9f, "building entrance faces the nearest street");
      check(roadDistance(lot.cx, lot.cz) >= hypotf(lot.hw, lot.hd) + 7.f, "regional road footprint exclusion");
      float low = 1e9f, high = -1e9f;
      for (int k = 0; k < 4; k++) {
        float x = (k & 1) ? lot.hw : -lot.hw, z = (k & 2) ? lot.hd : -lot.hd;
        float wx = lot.cx + cosf(lot.yaw) * x + sinf(lot.yaw) * z, wz = lot.cz - sinf(lot.yaw) * x + cosf(lot.yaw) * z;
        float h = g_world.groundHeight(wx, wz, 7); low = std::min(low, h); high = std::max(high, h);
        check(h > 2.5f && g_world.onRunway(wx, wz, 60.f) < 0, "footprint is dry and clear of runways");
        check(communityStreetDistance(town, wx, wz) >= 3.5f, "no building corner intersects a local street");
      }
      check(high - low <= 2.6f + .001f, "sloped sites rejected without changing terrain");
    }
    std::printf("%-18s %4d street-front buildings\n", t.name, count);
    occupiedTowns += count > 0;
    if (t.kind == 2) check(count > 150, "city retains a populated low-rise fabric");
    // The central square exists in the plan even where airport/water masks suppress placement.
    check(communityPark(town, plan.blockX * LOT * .5f, plan.blockZ * LOT * .5f), "civic green is deterministic");
  }
  check(total > 600 && total < 12000, "bounded, populated community density");
  check(occupiedTowns == kNumTowns, "all named communities inhabit suitable land");
  check(tall > 150 && tall < total / 5, "dense cities retain a substantial, bounded high-rise skyline");
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
      check(buildings < 120, "28m frontage lattice bounds community buildings per chunk");
      check(chunk->ents.size() < 2000, "detail budget includes the existing 6m forest lattice");
      g_scenery.clear(); chunk = g_scenery.ensure(cx, cz, 2);
      check(upgraded.size() == chunk->ents.size() && (upgraded.empty() || !std::memcmp(upgraded.data(), chunk->ents.data(), upgraded.size() * sizeof(Ent))), "streaming level upgrade matches direct detailed generation");
      g_scenery.clear();
    }
  }
  check(cars > 0, "communities contain driveway activity");
  Lot cached, raw;
  bool cachedPresent = communityLot(g_world, 0, -5, -5, cached);
  sceneryInit(); // A new world setup invalidates the derived cache before any generation.
  bool rawPresent = communityLot(g_world, 0, -5, -5, raw);
  check(cachedPresent == rawPresent && cached.cx == raw.cx && cached.cz == raw.cz && cached.yaw == raw.yaw && cached.wallH == raw.wallH, "uncached fallback preserves cached lot data");
  sceneryBakeCommunityLots(g_world);
  std::printf("%d buildings, %d tall, %d inhabited towns, %zu roads, %d parked cars sampled; %d failures\n", total, tall, occupiedTowns, g_roads.size(), cars, failures);
  return failures ? 1 : 0;
}
