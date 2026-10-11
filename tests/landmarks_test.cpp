// The landmarks (landmarks.cpp; docs/LIVING_ISLANDS_PLAN.md, Phase C), each where the world has room for it. The wind
// farm: a row of turbines along a ridge's crest, 300 m and more apart, on the hills under the snow, out of the settlements, off the
// roads and 6 km or more from every runway; each in its own chunk's scenery; found the same every time.
#include "../src/scenery.h"
#include "../src/entities.h"
#include "../src/settlements.h"
#include "test_world.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { if (failures < 20) std::printf("FAIL: %s\n", what); failures++; } }

int main() {
  buildTestWorld();
  const World& w = g_world;
  const std::vector<PlacedEnt> items = landmarks();
  std::vector<Ent> turbines;
  for (const PlacedEnt& p : items) if (p.kind == EK_WINDTURBINE) turbines.push_back(p.e);
  check(turbines.size() >= 8 && turbines.size() <= 20, "a wind farm of eight turbines or more, two farms at the most");
  for (size_t i = 0; i < turbines.size(); i++) {
    const Ent& t = turbines[i];
    float b[4]; w.sampleBase(t.x, t.z, b);
    check(std::isfinite(t.x + t.y + t.z + t.yaw) && b[0] >= 80.f && b[0] <= 700.f && b[3] <= 0.3f && t.y > 50.f, "a turbine on the green hills, where it's mild");
    check(fabsf(t.y - w.groundHeight(t.x, t.z, 8)) < 1.f, "a turbine on its ground");
    for (const Airport& a : w.airports) check(hypotf(t.x - a.x, t.z - a.z) >= 6000.f, "a turbine 6 km or more from every runway");
    check(settlementShare(w.settlements, t.x, t.z) >= 2.f, "a turbine out of the settlements");
    check(roadEdgeDistance(w.roadGrid, t.x, t.z) > 40.f, "a turbine off the roads");
    // on the crest: higher than the ground 250 m either side across its row (its front, local +z, faces across it)
    const float fx = sinf(t.yaw), fz = cosf(t.yaw);
    float s1[4], s2[4]; w.sampleBase(t.x + fx * 250.f, t.z + fz * 250.f, s1); w.sampleBase(t.x - fx * 250.f, t.z - fz * 250.f, s2);
    check(b[0] > 0.5f * (s1[0] + s2[0]) + 4.f, "a turbine on a ridge's crest");
    for (size_t j = i + 1; j < turbines.size(); j++) check(hypotf(turbines[j].x - t.x, turbines[j].z - t.z) >= 290.f, "the turbines 300 m or so apart");
  }
  // in their chunks' scenery, as generated
  for (size_t i = 0; i < turbines.size(); i += 3) {
    const Ent& t = turbines[i];
    const Scenery::Chunk* ch = g_scenery.ensure(Scenery::chunkOf(t.x), Scenery::chunkOf(t.z), 1);
    bool found = false;
    for (uint32_t k = ch->off[EK_WINDTURBINE]; k < ch->off[EK_WINDTURBINE + 1]; k++) found = found || !memcmp(&ch->ents[k], &t, sizeof(Ent));
    check(found, "a turbine in its chunk's scenery");
  }
  g_scenery.clear();
  sceneryBakeLandmarks(w);
  const std::vector<PlacedEnt>& again = landmarks();
  bool same = again.size() == items.size();
  for (size_t i = 0; same && i < items.size(); i++) same = again[i].kind == items[i].kind && !memcmp(&again[i].e, &items[i].e, sizeof(Ent));
  check(same, "the landmarks found the same every time");
  if (!turbines.empty()) std::printf("wind farm: %zu turbines, from %.0f %.0f (%.0f m) to %.0f %.0f, facing %.0f deg\n", turbines.size(), turbines.front().x, turbines.front().z,
                                     turbines.front().y, turbines.back().x, turbines.back().z, turbines.front().yaw * 57.2958f);
  std::printf("landmarks: %zu placed; %d checks, %d failures\n", items.size(), checks, failures);
  return failures ? 1 : 0;
}
