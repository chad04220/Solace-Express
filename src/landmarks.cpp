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
std::vector<vec3> g_clearings;

float baseAt(const World& w, float x, float z, int c = 0) { float b[4]; w.sampleBase(x, z, b); return b[c]; }
bool clearOfRunways(const World& w, float x, float z, float r) {
  if (airportInfluence(x, z) > 0.f) return false;
  for (const Airport& a : w.airports) if (hypotf(x - a.x, z - a.z) < r) return false;
  return true;
}

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

// The stone circle: on the most prominent bare summit - higher by 12 m and more than the ground 400 m round it every
// way, its top near level, no wood on it - out of the settlements, off the roads, 2 km and more from the runways. Sixteen stones in a
// ring 13 m across, two of them gone (fallen, lying beside their places), a flat stone at the centre; its clearing 28 m
void stoneCircle(const World& world, std::vector<PlacedEnt>& out) {
  float best = 0.f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 125.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 125.f) {
      const float g = baseAt(world, x, z);
      if (g < 120.f || g > 650.f || baseAt(world, x, z, 3) > 0.5f) continue;
      float rim = -1e9f;
      for (int k = 0; k < 8; k++) rim = std::max(rim, baseAt(world, x + 400.f * cosf(k * 0.7854f), z + 400.f * sinf(k * 0.7854f)));
      const float rise = g - rim;
      if (rise < 12.f || rise <= best) continue;
      float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 8; k++) { const float h = world.groundHeight(x + 22.f * cosf(k * 0.7854f), z + 22.f * sinf(k * 0.7854f), 8); lo = std::min(lo, h); hi = std::max(hi, h); }
      if (hi - lo > 6.f || world.forestAt(x, z) > 0.45f) continue;
      if (!clearOfRunways(world, x, z, 2000.f) || settlementShare(world.settlements, x, z) < 1.5f || roadEdgeDistance(world.roadGrid, x, z) < 80.f) continue;
      best = rise; at = vec2(x, z);
    }
  if (best <= 0.f) return;
  for (int i = 0; i < 16; i++) {
    const float a = i * 0.3927f + 0.1f, h = hash2i(i * 7 + 3, 11);
    const vec2 p = at + vec2(cosf(a), sinf(a)) * 13.f, in = (at - p) * (1.f / 13.f);
    const float g = world.groundHeight(p.x, p.y, 8), seed = 0.0002f + i * 1e-5f;   // (seed: a thinning key of ~0, kept to the rocks' range)
    if (i == 5 || i == 12) {   // (two fallen, lying outward beside their places)
      const vec2 q = p - in * 2.2f;
      out.push_back({EK_SLAB, {q.x, world.groundHeight(q.x, q.y, 8) - 0.25f, q.y, atan2f(in.x, in.y), 0.55f, 0.75f, 1.9f, seed}});
      continue;
    }
    // (each standing, broad face to the centre: 1.3 m wide, 3.1 to 3.8 m tall, half a metre thick, its foot sunk)
    out.push_back({EK_BLOCK, {p.x, g - 0.45f, p.y, atan2f(in.x, in.y), 0.6f + 0.1f * h, 2.5f + 0.6f * h, 0.42f + 0.06f * h, seed}});
  }
  out.push_back({EK_SLAB, {at.x, world.groundHeight(at.x, at.y, 8) - 0.3f, at.y, 0.6f, 1.2f, 0.7f, 0.8f, 0.0003f}});
  g_clearings.push_back(vec3(at.x, at.y, 28.f));
}

// The wreck: on a reef in warm, clear shallows - the sea floor 1.5 to 4 m down, open water 150 m round it every way -
// 3 km and more from the runways, off the roads; the shallowest reef in the warmest water. She lies on the floor,
// her keel in it
void shipwreck(const World& world, std::vector<PlacedEnt>& out) {
  float best = -1e9f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 150.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 150.f) {
      const float g = baseAt(world, x, z);
      if (g > -1.5f || g < -4.f || baseAt(world, x, z, 3) > 0.25f) continue;
      bool open = true;
      for (int k = 0; k < 8 && open; k++) open = baseAt(world, x + 150.f * cosf(k * 0.7854f), z + 150.f * sinf(k * 0.7854f)) < -0.5f;
      if (!open || !clearOfRunways(world, x, z, 3000.f) || roadEdgeDistance(world.roadGrid, x, z) < 150.f) continue;
      const float score = baseAt(world, x, z, 2) - 0.1f * fabsf(g + 2.5f);   // (lush: the warm south; then 2.5 m down)
      if (score > best) { best = score; at = vec2(x, z); }
    }
  if (best <= -1e8f) return;
  const float floor = world.groundHeight(at.x, at.y, 8);
  out.push_back({EK_SHIPWRECK, {at.x, floor + 3.6f, at.y, hash2i((int)at.x, (int)at.y) * 6.2832f, 1.f, 1.f, 1.f, 0.5f}});
}

// The platform: in deep water (the floor 60 m and more down for 1 km round it), 6 km and more from any land (as far as
// rays every 15 degrees see it) but within 12 km of it, 8 km and more from the runways; the nearest to land of such places
void offshorePlatform(const World& world, std::vector<PlacedEnt>& out) {
  float best = 1e9f; vec2 at;
  for (float z = -WORLD_HALF + 4000.f; z < WORLD_HALF - 4000.f; z += 1000.f)
    for (float x = -WORLD_HALF + 4000.f; x < WORLD_HALF - 4000.f; x += 1000.f) {
      if (baseAt(world, x, z) > -60.f) continue;
      bool deep = true;
      for (int k = 0; k < 8 && deep; k++) deep = baseAt(world, x + 1000.f * cosf(k * 0.7854f), z + 1000.f * sinf(k * 0.7854f)) < -60.f;
      if (!deep || !clearOfRunways(world, x, z, 8000.f)) continue;
      float land = 1e9f;   // (how far the nearest land: rings out to 12 km)
      for (float r = 1000.f; r <= 12000.f && land > 1e8f; r += 1000.f)
        for (int k = 0; k < 24; k++) if (baseAt(world, x + r * cosf(k * 0.2618f), z + r * sinf(k * 0.2618f)) > 0.f) { land = r; break; }
      if (land < 6000.f || land > 12000.f || land >= best) continue;
      best = land; at = vec2(x, z);
    }
  if (best > 1e8f) return;
  out.push_back({EK_PLATFORM, {at.x, 0.f, at.y, hash2i((int)at.y, (int)at.x) * 6.2832f, 1.f, 1.f, 1.f, 0.5f}});
}

// The Kaleo observatory: three telescope domes along the rim of Mount Kaleo's crater, where the rim is highest, 2.5 km
// and more from the crater field's runway, each on its own ground and facing out over the island; a clearing of 18 m
void observatory(const World& world, std::vector<PlacedEnt>& out) {
  const vec2 cone(25000.f, -9000.f);   // (world.cpp kCones: Mount Kaleo)
  float best = -1e9f, bestA = 0.f, bestR = 0.f;
  for (int a = 0; a < 144; a++)
    for (float r = 250.f; r <= 1500.f; r += 25.f) {
      const vec2 p = cone + vec2(cosf(a * 0.0436f), sinf(a * 0.0436f)) * r;
      const float g = baseAt(world, p.x, p.y);
      if (g > best && clearOfRunways(world, p.x, p.y, 2500.f)) { best = g; bestA = a * 0.0436f; bestR = r; }
    }
  if (best < 500.f) return;
  const float da[3] = {0.f, 0.13f, -0.12f}, size[3] = {1.f, 0.8f, 1.15f};
  for (int i = 0; i < 3; i++) {
    // (along the rim: at each angle the radius where it's highest, near the first's)
    const float a = bestA + da[i];
    float hi = -1e9f, rr = bestR;
    for (float r = std::max(bestR - 150.f, 250.f); r <= bestR + 150.f; r += 10.f) {
      const float g = baseAt(world, cone.x + cosf(a) * r, cone.y + sinf(a) * r);
      if (g > hi) { hi = g; rr = r; }
    }
    const vec2 p = cone + vec2(cosf(a), sinf(a)) * rr, outward(cosf(a), sinf(a));
    if (!clearOfRunways(world, p.x, p.y, 2500.f)) continue;
    float lo = 1e9f;
    for (int k = 0; k < 8; k++) lo = std::min(lo, world.groundHeight(p.x + 7.f * size[i] * cosf(k * 0.785f), p.y + 7.f * size[i] * sinf(k * 0.785f), 8));
    out.push_back({EK_OBSERVATORY, {p.x, lo, p.y, atan2f(outward.x, outward.y), size[i], size[i], size[i], 0.5f}});
    g_clearings.push_back(vec3(p.x, p.y, 18.f));
  }
}

// The star fort: on a cape - water two thirds of the way round it 350 m out, low (5 to 35 m up), level enough for the
// fort (6 m at the most across 130 m) - out of the settlements, off the roads, 2.5 km and more from the runways; the
// most surrounded by the sea of such places. Its clearing 75 m
void starFort(const World& world, std::vector<PlacedEnt>& out) {
  float best = -1e9f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 100.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 100.f) {
      const float g = baseAt(world, x, z);
      if (g < 5.f || g > 35.f) continue;
      int sea = 0;
      for (int k = 0; k < 16; k++) sea += baseAt(world, x + 350.f * cosf(k * 0.3927f), z + 350.f * sinf(k * 0.3927f)) < 0.f;
      if (sea < 10 || sea <= best) continue;
      float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 12; k++) {
        const float h = world.groundHeight(x + 66.f * cosf(k * 0.5236f), z + 66.f * sinf(k * 0.5236f), 8);
        lo = std::min(lo, h); hi = std::max(hi, h);
      }
      if (lo < 2.f || hi - lo > 6.f) continue;
      if (!clearOfRunways(world, x, z, 2500.f) || settlementShare(world.settlements, x, z) < 1.3f || roadEdgeDistance(world.roadGrid, x, z) < 90.f) continue;
      best = (float)sea; at = vec2(x, z);
    }
  if (best < 0.f) return;
  float lo = 1e9f;
  for (int k = 0; k < 12; k++) lo = std::min(lo, world.groundHeight(at.x + 40.f * cosf(k * 0.5236f), at.y + 40.f * sinf(k * 0.5236f), 8));
  out.push_back({EK_STARFORT, {at.x, lo, at.y, hash2i((int)at.x, (int)at.y) * 6.2832f, 1.f, 1.f, 1.f, 0.5f}});
  g_clearings.push_back(vec3(at.x, at.y, 75.f));
}

// Lighthouse Key: the keepers' two cottages behind the light, landward, and the landing stage on the nearest shore - its
// deck from the land (the ground there 0.3 to 2.5 m up: the stage raised to meet it) out 45 m over water 1 m deep and more
void lighthouseKey(const World& world, std::vector<PlacedEnt>& out) {
  float lx, lz, yaw;
  if (!findLighthouseSite(world, lx, lz, yaw)) return;
  const vec2 L(lx, lz), seaward(sinf(yaw), cosf(yaw)), side(-seaward.y, seaward.x);
  for (float sd : {-1.f, 1.f}) {
    const vec2 c = L - seaward * 34.f + side * (sd * 11.f);
    float lo = 1e9f;
    for (int k = 0; k < 4; k++) lo = std::min(lo, world.groundHeight(c.x + ((k & 1) ? 5.f : -5.f), c.y + ((k & 2) ? 5.f : -5.f), 8));
    if (lo < 2.f) continue;
    out.push_back({EK_HOUSE_HIP, {c.x, lo - 0.05f, c.y, atan2f(seaward.x, seaward.y), 0.85f, 1.f, 0.85f, 0.3f + 0.4f * (sd > 0.f)}});
    g_clearings.push_back(vec3(c.x, c.y, 12.f));
  }
  float best = 1e9f; vec2 shore, dir;
  for (float r = 40.f; r <= 450.f; r += 10.f)
    for (int a = 0; a < 72; a++) {
      const vec2 u(cosf(a * 0.0873f), sinf(a * 0.0873f)), s = L + u * r;
      const float g = world.groundHeight(s.x, s.y, 8);
      if (g < 0.3f || g > 2.5f || r >= best) continue;
      for (int b = 0; b < 24; b++) {   // (out over the water)
        const vec2 d(cosf(b * 0.2618f), sinf(b * 0.2618f));
        bool ok = world.groundHeight(s.x + d.x * 8.f, s.y + d.y * 8.f, 8) < 0.f;
        for (float t = 15.f; t <= 45.f && ok; t += 6.f) ok = world.groundHeight(s.x + d.x * t, s.y + d.y * t, 8) < -1.f;
        if (ok) { best = r; shore = s; dir = d; break; }
      }
    }
  if (best > 1e8f) return;
  const vec2 c = shore + dir * 19.5f;   // (its deck's landward 6 m over the shore)
  const float lift = std::max(0.f, world.groundHeight(shore.x, shore.y, 8) - 1.3f);
  out.push_back({EK_JETTY, {c.x, lift, c.y, atan2f(dir.x, dir.y), 1.f, 1.f, 1.f, 0.5f}});
}
}  // namespace

void sceneryBakeLandmarks(const World& world) {
  std::vector<PlacedEnt> out;
  g_clearings.clear();
  windFarms(world, out);
  stoneCircle(world, out);
  shipwreck(world, out);
  offshorePlatform(world, out);
  observatory(world, out);
  starFort(world, out);
  lighthouseKey(world, out);
  for (const PlacedEnt& c : civicLandmarks()) {   // (the cities': placed with their lots; their grounds cleared)
    out.push_back(c);
    if (c.kind == EK_STADIUM) g_clearings.push_back(vec3(c.e.x, c.e.z, 105.f));
    if (c.kind == EK_QUAY)
      for (float x = -180.f; x <= 180.f; x += 90.f)
        g_clearings.push_back(vec3(c.e.x + cosf(c.e.yaw) * x - sinf(c.e.yaw) * 62.f, c.e.z - sinf(c.e.yaw) * x - cosf(c.e.yaw) * 62.f, 80.f));
  }
  g_landmarks.build(out);
}

const std::vector<vec3>& landmarkClearings() { return g_clearings; }

const std::vector<PlacedEnt>& landmarks() { return g_landmarks.items; }
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(int cx, int cz) { return g_landmarks.in(cx, cz); }
