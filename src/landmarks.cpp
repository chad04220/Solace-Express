// Solace Express - the landmarks (docs/LIVING_ISLANDS_PLAN.md, Phase C): each found where the world has room for it
// once the world is built or loaded (after its road furniture), and handed to the scenery chunks as they are generated
// (entities.cpp). Each search is deterministic and runs on the base height map (World::sampleBase), so it is quick.
#include "scenery.h"
#include "entities.h"
#include "settlements.h"
#include "world.h"
#include <algorithm>
#include <cmath>

float airportInfluence(float x, float z);   // world.cpp

namespace {
PlacedByChunk g_landmarks;

// The wind farm: a row of ten turbines 300 m apart along the crest of a ridge - each standing higher than the ground
// 250 m either side of the row - on the green hills (80 to 700 m, where it's mild: no snow), out of the settlements, off the
// roads, and 6 km or more from every airfield's runway (a 138 m obstacle). The best such row by how much it stands out;
// a second, 12 km or more from the first, if one stands out as well.
void windFarms(const World& world, std::vector<PlacedEnt>& out) {
  auto base = [&](float x, float z) { float b[4]; world.sampleBase(x, z, b); return b[0]; };
  auto cold = [&](float x, float z) { float b[4]; world.sampleBase(x, z, b); return b[3]; };
  auto fit = [&](vec2 p) {   // (a turbine's own place: high, clear of the airfields, the settlements and the roads)
    if (base(p.x, p.y) < 80.f || base(p.x, p.y) > 700.f || cold(p.x, p.y) > 0.3f || airportInfluence(p.x, p.y) > 0.f) return false;
    for (const Airport& a : world.airports) if (hypotf(p.x - a.x, p.y - a.z) < 6000.f) return false;
    if (settlementShare(world.settlements, p.x, p.y) < 2.f) return false;
    return roadEdgeDistance(world.roadGrid, p.x, p.y) > 40.f;
  };
  const int N = 10; const float gap = 300.f;
  struct Row { float score; vec2 c, u; };
  std::vector<Row> rows;
  for (float z = -WORLD_HALF + 2000.f; z < WORLD_HALF - 2000.f; z += 400.f)
    for (float x = -WORLD_HALF + 2000.f; x < WORLD_HALF - 2000.f; x += 400.f) {
      if (base(x, z) < 120.f || base(x, z) > 700.f || cold(x, z) > 0.3f) continue;
      for (int d = 0; d < 12; d++) {
        const float th = d * 0.2618f; const vec2 u(sinf(th), cosf(th)), v(-u.y, u.x);
        float score = 0.f; int off = 0;
        for (int i = 0; i < N && off <= 2; i++) {
          const vec2 p = vec2(x, z) + u * ((i - (N - 1) * 0.5f) * gap);
          const float g = base(p.x, p.y);
          const float crest = g - 0.5f * (base(p.x + v.x * 250.f, p.y + v.y * 250.f) + base(p.x - v.x * 250.f, p.y - v.y * 250.f));
          const float along = fabsf(base(p.x + u.x * 80.f, p.y + u.y * 80.f) - base(p.x - u.x * 80.f, p.y - u.y * 80.f));
          if (crest < 5.f || along > 30.f || g < 80.f || g > 700.f) { off++; continue; }
          score += crest;
        }
        if (off <= 2 && score > 0.f) rows.push_back({score, vec2(x, z), u});
      }
    }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score != b.score ? a.score > b.score : (a.c.x != b.c.x ? a.c.x < b.c.x : a.c.y < b.c.y); });
  std::vector<vec2> farms;
  for (const Row& r : rows) {
    if (farms.size() >= 2 || (!farms.empty() && r.score < rows.front().score * 0.6f)) break;
    bool near = false; for (const vec2& f : farms) near = near || length(f - r.c) < 12000.f;
    if (near) continue;
    int placed = 0; std::vector<vec2> at;
    for (int i = 0; i < N; i++) {   // (each on the crest, and in its own place)
      const vec2 p = r.c + r.u * ((i - (N - 1) * 0.5f) * gap), v(-r.u.y, r.u.x);
      const float crest = base(p.x, p.y) - 0.5f * (base(p.x + v.x * 250.f, p.y + v.y * 250.f) + base(p.x - v.x * 250.f, p.y - v.y * 250.f));
      if (crest >= 5.f && fit(p)) at.push_back(p);
    }
    if ((int)at.size() < N - 2) continue;   // (two may drop out - a road, a hamlet - but no more)
    for (const vec2& p : at) {
      // each facing across the row (the wind's way, turning to it as it changes: ent_vs.glsl), on its own ground
      out.push_back({EK_WINDTURBINE, {p.x, world.groundHeight(p.x, p.y, 8) - 0.3f, p.y, atan2f(-r.u.y, r.u.x), 1.f, 1.f, 1.f, hash2i((int)p.x, (int)p.y)}});
      placed++;
    }
    if (placed) farms.push_back(r.c);
  }
}
}  // namespace

void sceneryBakeLandmarks(const World& world) {
  std::vector<PlacedEnt> out;
  windFarms(world, out);
  g_landmarks.build(out);
}

const std::vector<PlacedEnt>& landmarks() { return g_landmarks.items; }
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(int cx, int cz) { return g_landmarks.in(cx, cz); }
