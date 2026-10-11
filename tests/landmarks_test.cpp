// The landmarks (landmarks.cpp; docs/LIVING_ISLANDS_PLAN.md, Phase C), each where the world has room for it. A stone
// circle on a bare summit, its stones standing (two fallen) round a clearing no tree grows in; a wreck on a reef in
// warm shallows, open water round her; an offshore platform in deep water out of sight of no land. The wind
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
  // the stone circle: its standing stones round a centre (its flat stone), on a summit, in a clearing
  {
    std::vector<Ent> stones; const Ent* centre = nullptr;
    for (const PlacedEnt& p : items) if (p.kind == EK_BLOCK || p.kind == EK_SLAB) stones.push_back(p.e);
    for (const PlacedEnt& p : items) if (p.kind == EK_SLAB && p.e.sx > 1.f) centre = &p.e;
    check(centre != nullptr && stones.size() >= 15, "a stone circle: its stones and its centre");
    if (centre) {
      int standing = 0;
      for (const Ent& e : stones) {
        const float d = hypotf(e.x - centre->x, e.z - centre->z);
        check(d < 17.f, "the circle's stones round its centre");
        if (d > 11.f && d < 15.f && e.sy > 2.f) standing++;
        check(fabsf(e.y - w.groundHeight(e.x, e.z)) < 1.f, "a stone on its ground, its foot sunk");
      }
      check(standing >= 12, "most of the circle's stones standing, 3 m and more tall");
      float b[4]; w.sampleBase(centre->x, centre->z, b);
      bool summit = true;
      for (int k = 0; k < 8; k++) { float r[4]; w.sampleBase(centre->x + 400.f * cosf(k * 0.785f), centre->z + 400.f * sinf(k * 0.785f), r); summit = summit && b[0] > r[0] + 11.f; }
      check(summit, "the circle on a summit");
      int trees = 0;
      for (int cz = Scenery::chunkOf(centre->z - 30.f); cz <= Scenery::chunkOf(centre->z + 30.f); cz++)
        for (int cx = Scenery::chunkOf(centre->x - 30.f); cx <= Scenery::chunkOf(centre->x + 30.f); cx++) {
          const Scenery::Chunk* ch = g_scenery.ensure(cx, cz, 2);
          for (int k = 0; k <= EK_BUSH; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++) trees += hypotf(ch->ents[n].x - centre->x, ch->ents[n].z - centre->z) < 25.f;
          int found = 0;
          for (int k = EK_BLOCK; k <= EK_SLAB; k++)
            for (uint32_t n = ch->off[k]; n < ch->off[k + 1]; n++) found += hypotf(ch->ents[n].x - centre->x, ch->ents[n].z - centre->z) < 17.f;
          (void)found;
        }
      check(trees == 0, "no tree or bush in the circle's clearing");
      g_scenery.clear();
    }
  }
  // the wreck: on a reef 1.5 to 4 m down, open water round her; the platform: in deep water 5 km and more from land
  int wrecks = 0, platforms = 0;
  for (const PlacedEnt& p : items) {
    if (p.kind == EK_SHIPWRECK) {
      wrecks++;
      const float floor = w.groundHeight(p.e.x, p.e.z);
      check(floor < -1.f && floor > -4.5f && fabsf(p.e.y - floor - 3.6f) < 0.01f, "the wreck aground on a reef in the shallows");
      for (int k = 0; k < 8; k++) check(w.groundHeight(p.e.x + 150.f * cosf(k * 0.785f), p.e.z + 150.f * sinf(k * 0.785f)) < 0.f, "open water round the wreck");
    }
    if (p.kind == EK_PLATFORM) {
      platforms++;
      check(p.e.y == 0.f && w.groundHeight(p.e.x, p.e.z) < -60.f, "the platform in deep water, at the sea");
      for (int k = 0; k < 24; k++) check(w.groundHeight(p.e.x + 5000.f * cosf(k * 0.26f), p.e.z + 5000.f * sinf(k * 0.26f)) < 0.f, "no land within 5 km of the platform");
      for (const Airport& a : w.airports) check(hypotf(p.e.x - a.x, p.e.z - a.z) >= 8000.f, "the platform 8 km and more from every runway");
    }
  }
  check(wrecks == 1 && platforms == 1, "a wreck and a platform");
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
  for (const PlacedEnt& p : items)
    if (p.kind == EK_SHIPWRECK || p.kind == EK_PLATFORM || (p.kind == EK_SLAB && p.e.sx > 1.f))
      std::printf("%s at %.0f %.0f (%.1f m)\n", p.kind == EK_SHIPWRECK ? "wreck" : p.kind == EK_PLATFORM ? "platform" : "stone circle", p.e.x, p.e.z, p.e.y);
  std::printf("landmarks: %zu placed; %d checks, %d failures\n", items.size(), checks, failures);
  return failures ? 1 : 0;
}
