// Somnium Volandi - the landmarks (docs/LIVING_ISLANDS_PLAN.md, Phase C): each found where the world has room for it
// once the world is built or loaded (after its road furniture), and handed to the scenery chunks as they are generated
// (entities.cpp). Each search is deterministic and runs on the base height map (World::sampleBase), so it is quick.
#include "scenery.h"
#include "earth_charts.h"
#include "entities.h"
#include "settlements.h"
#include "airport_layout.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_set>

namespace {
float baseAt(const World& w, float x, float z, int c = 0) { float b[4]; w.sampleBase(x, z, b); return b[c]; }
// clear of every seaplane base's water lane - its length and `ends` beyond each end, `sides` to either side of it - and
// 60 m of its dock's jetty and mooring
bool seaLaneClear(const World& w, float x, float z, float ends, float sides) {
  for (const Airport& a : w.airports) {
    if (!a.water()) continue;
    const vec2 q = aptLocal(a, vec3(x, 0, z));
    if (fabsf(q.x) < a.length * 0.5f + ends && fabsf(q.y) < a.width * 0.5f + sides) return false;
    if (a.dockLen > 0.f) {
      const vec2 d(a.dockDirX, a.dockDirZ), o(a.dockX, a.dockZ), p = vec2(x, z) - o;
      const float u = std::clamp(p.x * d.x + p.y * d.y, 0.f, a.dockLen + 10.f);
      if (length(p - d * u) < 60.f) return false;
    }
  }
  return true;
}
bool clearOfRunways(const World& w, float x, float z, float r) {
  if (airportInfluence(w, x, z) > 0.f) return false;
  for (const Airport& a : w.airports) if (!a.water() && hypotf(x - a.x, z - a.z) < r) return false;   // (a seaplane base's lane: seaLaneClear)
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
    if (base(p.x, p.y) < 80.f || base(p.x, p.y) > 700.f || cold(p.x, p.y) > 0.3f || airportInfluence(world, p.x, p.y) > 0.f) return false;
    for (const Airport& a : world.airports) if (!a.water() && hypotf(p.x - a.x, p.y - a.z) < 6000.f) return false;
    if (settlementShare(world.settlements, p.x, p.y) < 2.f) return false;
    return roadEdgeDistance(world.roadGrid, p.x, p.y) > 40.f;
  };
  const int N = 10; const float gap = 300.f;
  struct Row { float score; vec2 c, u; };
  std::vector<Row> rows;
  for (float z = -WORLD_HALF + 2000.f; z < WORLD_HALF - 2000.f; z += 400.f)
    for (float x = -WORLD_HALF + 2000.f; x < WORLD_HALF - 2000.f; x += 400.f) {
      generationCheckpoint();
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
    generationCheckpoint();
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
void stoneCircle(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  float best = 0.f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 125.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 125.f) {
      generationCheckpoint();
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
  state.clearings.push_back(vec3(at.x, at.y, 28.f));
}

// The wreck: on a reef in warm, clear shallows - the sea floor 1.5 to 4 m down, open water 150 m round it every way -
// 3 km and more from the runways, off the roads; the shallowest reef in the warmest water. She lies on the floor,
// her keel in it
void shipwreck(const World& world, std::vector<PlacedEnt>& out) {
  float best = -1e9f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 150.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 150.f) {
      generationCheckpoint();
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
      generationCheckpoint();
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
void observatory(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  if (world.procedural) return;   // Mount Kaleo's authored crater is unique to Solace.
  const vec2 cone(25000.f, -9000.f);   // (world.cpp kCones: Mount Kaleo)
  float best = -1e9f, bestA = 0.f, bestR = 0.f;
  for (int a = 0; a < 144; a++)
    for (float r = 250.f; r <= 1500.f; r += 25.f) {
      generationCheckpoint();
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
      generationCheckpoint();
      const float g = baseAt(world, cone.x + cosf(a) * r, cone.y + sinf(a) * r);
      if (g > hi) { hi = g; rr = r; }
    }
    const vec2 p = cone + vec2(cosf(a), sinf(a)) * rr, outward(cosf(a), sinf(a));
    if (!clearOfRunways(world, p.x, p.y, 2500.f)) continue;
    float lo = 1e9f;
    for (int k = 0; k < 8; k++) lo = std::min(lo, world.groundHeight(p.x + 7.f * size[i] * cosf(k * 0.785f), p.y + 7.f * size[i] * sinf(k * 0.785f), 8));
    out.push_back({EK_OBSERVATORY, {p.x, lo, p.y, atan2f(outward.x, outward.y), size[i], size[i], size[i], 0.5f}});
    state.clearings.push_back(vec3(p.x, p.y, 18.f));
  }
}

// The star fort: on a cape - water two thirds of the way round it 350 m out, low (5 to 35 m up), level enough for the
// fort (6 m at the most across 130 m) - out of the settlements, off the roads, 2.5 km and more from the runways; the
// most surrounded by the sea of such places. Its clearing 75 m
void starFort(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  float best = -1e9f; vec2 at;
  for (float z = -WORLD_HALF + 1000.f; z < WORLD_HALF - 1000.f; z += 100.f)
    for (float x = -WORLD_HALF + 1000.f; x < WORLD_HALF - 1000.f; x += 100.f) {
      generationCheckpoint();
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
  state.clearings.push_back(vec3(at.x, at.y, 75.f));
}

// Lighthouse Key: the keepers' two cottages behind the light, landward, and the landing stage on the nearest shore - its
// deck from the land (the ground there 0.3 to 2.5 m up: the stage raised to meet it) out 45 m over water 1 m deep and more
void lighthouseKey(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  float lx, lz, yaw;
  if (!findLighthouseSite(world, lx, lz, yaw)) return;
  const vec2 L(lx, lz), seaward(sinf(yaw), cosf(yaw)), side(-seaward.y, seaward.x);
  for (float sd : {-1.f, 1.f}) {
    const vec2 c = L - seaward * 34.f + side * (sd * 11.f);
    float lo = 1e9f;
    for (int k = 0; k < 4; k++) lo = std::min(lo, world.groundHeight(c.x + ((k & 1) ? 5.f : -5.f), c.y + ((k & 2) ? 5.f : -5.f), 8));
    if (lo < 2.f) continue;
    out.push_back({EK_HOUSE_HIP, {c.x, lo - 0.05f, c.y, atan2f(seaward.x, seaward.y), 0.85f, 1.f, 0.85f, 0.3f + 0.4f * (sd > 0.f)}});
    state.clearings.push_back(vec3(c.x, c.y, 12.f));
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

// The real Earth's lighthouses (World::earthChart's landmarks: Wikidata's), each where it stands - or, where the ground
// has it in the sea (a light at a breakwater's end, the shore drawn a little out), on the nearest land within 120 m;
// with none there, out on its own rock, its foot at the sea floor (2 m down at most) - as tall as it is (16 m when not
// known: the tower scaled from its 27 m, 0.45 to 1.6 of it, its height above the water its own), its front to the open
// water. The settlements' lots under it are given up, and its ground cleared 25 m round
void realLighthouses(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  if (!world.earthChart) return;
  auto g = [&](vec2 p) { return world.groundHeight(p.x, p.y, 8); };
  std::vector<int> ids;
  for (const earthCharts::ChartLandmark& l : world.earthChart->landmarks) {
    generationCheckpoint();
    if (l.kind != earth::LandmarkRecord::LIGHTHOUSE) continue;
    vec2 at(l.x, l.z);
    if (g(at) < 0.5f) {
      bool found = false;
      for (float r = 10.f; r <= 120.f && !found; r += 10.f) {
        float lowest = 1e9f;   // (on the ring: the lowest land, nearest the water)
        for (int a = 0; a < 24; a++) {
          const vec2 p = at + vec2(cosf(a * 0.2618f), sinf(a * 0.2618f)) * r;
          const float h = g(p);
          if (h >= 0.8f && h < lowest) { lowest = h; at = p; found = true; }
        }
      }
      if (!found) at = vec2(l.x, l.z);
    }
    const float s = std::clamp((l.height > 0.f ? l.height : 16.f) / 27.f, 0.45f, 1.6f), R = 6.f * s;
    vec2 sea(0.f, 1.f); float low = 1e9f;
    for (int a = 0; a < 16; a++) {
      const vec2 u(sinf(a * 0.3927f), cosf(a * 0.3927f));
      const float h = g(at + u * 80.f);
      if (h < low) { low = h; sea = u; }
    }
    float foot = g(at);
    for (int k = 0; k < 8; k++) foot = std::min(foot, g(at + vec2(cosf(k * 0.7854f), sinf(k * 0.7854f)) * (3.4f * s)));
    const float y = foot >= 0.f ? foot - 0.05f : std::max(foot, -2.f) - 0.05f;
    const float sy = s + std::max(-y, 0.f) / 27.f;
    out.push_back({EK_LIGHTHOUSE, {at.x, y, at.y, atan2f(sea.x, sea.y), s, sy, s, hash2i((int)at.x, (int)at.y)}});
    state.clearings.push_back(vec3(at.x, at.y, 25.f));
    lotsIn(world, at.x - 60.f, at.y - 60.f, at.x + 60.f, at.y + 60.f, ids);
    for (int id : ids) {   // (a lot whose footprint comes within the light's reach and its keeper's house)
      Lot& lot = state.lots[id];
      const float c = cosf(lot.yaw), sn = sinf(lot.yaw), dx = at.x - lot.cx, dz = at.y - lot.cz;
      const float qx = std::max(fabsf(c * dx - sn * dz) - lot.hw, 0.f), qz = std::max(fabsf(sn * dx + c * dz) - lot.hd, 0.f);
      if (qx * qx + qz * qz < (R + 2.f) * (R + 2.f)) lot.present = false;
    }
  }
}

// The racetrack (its circuit, its pit lane and its road in are the network's: addRacetrack, settlements.h). Along the
// main straight's pit side, behind the pit lane, four blocks of eight garages - each bay where the pit lane's boxes are
// painted (every 12 m along it: terrain_material.glsl) - and the pit wall between the pit lane and the track; across
// the track three grandstands behind a catch fence, and a fourth on the outside of turn 1; the teams' transporters
// in the paddock behind the garages; a car park behind the grandstands; a guardrail round the outside of every bend.
// Its grounds cleared: the run-off, the pits, the stands and the car park
void racetrack(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  const Racetrack R = racetrackOf(world.roads);
  if (!R.found || R.pitLane < 0) return;
  const RoadPath &C = world.roads.paths[R.circuit], &P = world.roads.paths[R.pitLane];
  const vec2 r(-R.u.y, R.u.x);
  auto sOf = [&](float x, float z) { return dot2(vec2(x, z) - R.o, R.u); };
  auto yawTo = [](vec2 f) { return atan2f(f.x, f.y); };   // (a mesh's front, its +z, that way)
  // along a path's stretch beside the main straight: its height at s, and its distance along from its start
  auto beside = [&](const RoadPath& path, float s, float* along = nullptr) {
    float a = 0.f;
    for (size_t k = 0; k + 1 < path.pts.size(); k++) {
      const RoadPoint &A = path.pts[k], &B = path.pts[k + 1];
      const float s0 = sOf(A.x, A.z), s1 = sOf(B.x, B.z), L = hypotf(B.x - A.x, B.z - A.z);
      if (s >= s0 && s <= s1 && s1 > s0) {
        const float t = (s - s0) / (s1 - s0);
        if (along) *along = a + t * L;
        return A.h + (B.h - A.h) * t;
      }
      a += L;
    }
    if (along) *along = 0.f;
    return path.pts[0].h;
  };
  auto put = [&](int kind, vec2 p, float y, float yaw, float sx, float sy, float sz, float seed) { out.push_back({kind, {p.x, y, p.y, yaw, sx, sy, sz, seed}}); };
  // ---- the garages, their bays on the pit lane's boxes: the first from where 12 m along the lane falls past s 214
  float along0 = 0.f; beside(P, 214.f, &along0);
  const float s0 = 214.f + (ceilf(along0 / 12.f) * 12.f - along0);
  for (int i = 0; i < 4; i++) {
    const float sc = s0 + 48.f + 96.f * i;
    const float y = 0.5f * (beside(P, sc - 48.f) + beside(P, sc + 48.f));
    put(EK_PITS, R.at(sc, RACE_GARAGE_V), y - 0.05f, yawTo(r), 1.f, 1.f, 1.f, 0.13f + 0.21f * i);
    for (int k = 0; k < 8; k++) {   // (each team's transporter in the paddock behind its garage, nose to it)
      const vec2 t = R.at(sc - 42.f + 12.f * k, -63.5f);
      put(EK_TRANSPORTER, t, world.groundHeight(t.x, t.y, 8), yawTo(r), 1.f, 1.f, 1.f, hash2i(i * 8 + k, 41));
    }
  }
  // ---- the pit wall, from where the pit lane runs beside the track to where it leaves it
  for (float s = 174.f; s <= 606.f; s += 48.f) put(EK_PITWALL, R.at(s, RACE_PITWALL_V), beside(C, s), yawTo(r), 1.f, 1.f, 1.f, hash2i((int)s, 7));
  // ---- the grandstands opposite, and the catch fence before them
  for (float s : {310.f, 430.f, 550.f}) put(EK_GRANDSTAND, R.at(s, RACE_STAND_V), beside(C, s) - 0.1f, yawTo(r * -1.f), 1.f, 1.f, 1.f, 0.5f);
  for (float s = 260.f; s <= 600.f; s += 20.f) put(EK_FENCE, R.at(s, 17.5f), beside(C, s), yawTo(r), 1.f, 1.f, 1.f, 0.5f);
  // ---- the fourth grandstand, facing turn 1 from its outside (unless the road in passes there)
  {
    const vec2 out1 = R.u * 0.7071f - r * 0.7071f, c = R.at(931.8f, 13.2f) + out1 * 38.f, along1(-out1.y, out1.x);
    bool clear = true;
    for (float a : {-60.f, 0.f, 60.f}) for (float d : {-16.f, 16.f}) {
      const vec2 q = c + along1 * a + out1 * d;
      clear = clear && roadEdgeDistance(world.roadGrid, q.x, q.y) > 3.f;
    }
    if (clear) {
      float lo = 1e9f;
      for (float a : {-55.f, 0.f, 55.f}) for (float d : {-14.f, 14.f}) { const vec2 q = c + along1 * a + out1 * d; lo = std::min(lo, world.groundHeight(q.x, q.y, 8)); }
      put(EK_GRANDSTAND, c, lo - 0.1f, yawTo(out1 * -1.f), 1.f, 1.f, 1.f, 0.5f);
      state.clearings.push_back(vec3(c.x, c.y, 70.f));
    }
  }
  // ---- the car park behind the grandstands: rows facing each other in pairs, three in four places taken, on the
  // grass; none on a road or within 30 m of the circuit
  for (float v : {72.f, 78.f, 90.f, 96.f, 108.f, 114.f})
    for (float s = 262.f; s <= 600.f; s += 2.9f) {
      const vec2 p = R.at(s, v);
      const float h = hash2i((int)(s * 10.f), (int)v);
      if (h > 0.75f || roadEdgeDistance(world.roadGrid, p.x, p.y) < 30.f - roadSpec(RC_CIRCUIT).halfPlatform) continue;
      const vec2 nose = ((int)v % 18 == 0) ? r * -1.f : r;
      put(EK_CAR, p, world.groundHeight(p.x, p.y, 8), yawTo(nose), 1.f, 1.f, 1.f, hash2i((int)(s * 7.f), (int)v * 3));
    }
  // ---- the guardrails round the outside of every bend (its radius under 250 m, judged over 20 m either way), 15.4 m
  // out from the centreline at the run-off's edge, in 16 m lengths along it, facing the track
  {
    const int n = (int)C.pts.size();
    std::vector<float> along(n, 0.f);
    for (int k = 1; k < n; k++) along[k] = along[k - 1] + hypotf(C.pts[k].x - C.pts[k - 1].x, C.pts[k].z - C.pts[k - 1].z);
    auto pointAt = [&](float a, float* h, vec2* d) {
      int k = 0; while (k + 2 < n && along[k + 1] < a) k++;
      const float t = std::clamp((a - along[k]) / std::max(along[k + 1] - along[k], 1e-3f), 0.f, 1.f);
      const vec2 A(C.pts[k].x, C.pts[k].z), B(C.pts[k + 1].x, C.pts[k + 1].z);
      if (h) *h = C.pts[k].h + (C.pts[k + 1].h - C.pts[k].h) * t;
      if (d) *d = (B - A) * (1.f / std::max(length(B - A), 1e-3f));
      return A + (B - A) * t;
    };
    auto bend = [&](float a) {   // (+ turning right)
      vec2 d0, d1; pointAt(std::max(a - 20.f, 0.f), nullptr, &d0); pointAt(std::min(a + 20.f, along.back()), nullptr, &d1);
      return (d0.x * d1.y - d0.y * d1.x) / 40.f;
    };
    float a = 0.f;
    while (a + 16.f < along.back()) {
      const float k0 = bend(a + 8.f);
      if (fabsf(k0) < 1.f / 250.f) { a += 4.f; continue; }
      const float side = k0 > 0.f ? -1.f : 1.f;   // (the outside: away from the turn)
      // (16 m along the rail's own line, out on the bend's outside: so much less along the centreline)
      const float step = 16.f / (1.f + 15.4f * fabsf(k0));
      float ha, hb; vec2 da, db;
      const vec2 pa = pointAt(a, &ha, &da), pb = pointAt(a + step, &hb, &db);
      vec2 A = pa + vec2(-da.y, da.x) * (15.4f * side), B = pb + vec2(-db.y, db.x) * (15.4f * side);
      vec2 front = vec2(da.y, -da.x) * side;   // (towards the track)
      if (dot2(front, vec2(-(B - A).y, (B - A).x)) < 0.f) std::swap(A, B), std::swap(ha, hb);
      const vec2 u = B - A; const float len = length(u);
      if (len > 4.f) put(EK_GUARDRAIL, (A + B) * 0.5f, 0.5f * (ha + hb), atan2f(-u.y / len, u.x / len), len / 16.f, 1.f, 1.f, 0.5f + std::clamp((hb - ha) / len, -0.3f, 0.3f));
      a += step;
    }
  }
  // ---- its grounds cleared: the run-off all round, the pits, the paddock, the stands and the car park
  float run = 1e9f;
  for (size_t k = 1; k < C.pts.size(); k++) {   // (a 26 m circle every 25 m round it)
    run += hypotf(C.pts[k].x - C.pts[k - 1].x, C.pts[k].z - C.pts[k - 1].z);
    if (run >= 25.f) { state.clearings.push_back(vec3(C.pts[k].x, C.pts[k].z, 26.f)); run = 0.f; }
  }
  for (float s = 120.f; s <= 680.f; s += 40.f)
    for (float v : {-95.f, -65.f, -35.f, 25.f, 55.f, 85.f, 115.f}) { const vec2 p = R.at(s, v); state.clearings.push_back(vec3(p.x, p.y, 30.f)); }
}

// The Fjordhaven dam (its gorge and lake: world.h Reservoir, built into the world's heightmap): the arch across the
// gorge, its crown upstream, its foot 4 m into the valley floor, scaled to its chord and height; its lake against it. The
// gorge's walls round it cleared
void fjordhavenDam(const World& world, WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  const Reservoir& r = world.reservoir;
  if (r.on <= 0.f) return;
  const vec2 v(r.vx, r.vz), o = r.crown() - v * (55.f * r.scale());
  out.push_back({EK_DAM, {o.x, r.floor - 4.f, o.y, atan2f(v.x, v.y), r.scale(), (r.crest + 4.f) / 70.f, r.scale(), 0.5f}});
  state.clearings.push_back(vec3(o.x, o.y, 0.5f * r.chord + 60.f));
}

// ---- the sea's traffic (Phase D)
// A coastal town's harbour: the nearest place to its centre where the sea is 6 m deep, within 4.5 km (clear of the
// container terminal's quay); none for a town inland
bool harbourOf(const World& world, int t, vec2& at) {
  const vec2 c(world.towns[t].x, world.towns[t].z);
  vec2 quay(1e9f, 1e9f);
  for (const PlacedEnt& p : civicLandmarks(world)) if (p.kind == EK_QUAY) quay = vec2(p.e.x, p.e.z);
  for (float r = 100.f; r <= 4500.f; r += 50.f)
    for (int a = 0; a < 90; a++) {
      const vec2 p = c + vec2(cosf(a * 0.0698f), sinf(a * 0.0698f)) * r;
      if (world.naturalHeight(p.x, p.y) < -6.f && length(p - quay) > 500.f) { at = p; return true; }
    }
  return false;
}

// The way by sea from a to b: A* over 200 m cells of open water (6 m deep and more; dearer under 15 m), then drawn
// taut - each leg straight as far as the water under it stays 3 m deep
bool seaRoute(const World& world, vec2 a, vec2 b, std::vector<vec2>& out) {
  const float C = 200.f, margin = 8000.f;
  const float x0 = std::min(a.x, b.x) - margin, z0 = std::min(a.y, b.y) - margin;
  const int nx = (int)((std::max(a.x, b.x) + margin - x0) / C) + 1, nz = (int)((std::max(a.y, b.y) + margin - z0) / C) + 1;
  auto centre = [&](int i, int j) { return vec2(x0 + (i + 0.5f) * C, z0 + (j + 0.5f) * C); };
  std::vector<float> depth((size_t)nx * nz);
  parallelFor(nz, [&](int j) { for (int i = 0; i < nx; i++) { const vec2 p = centre(i, j); depth[(size_t)j * nx + i] = -world.naturalHeight(p.x, p.y); } });
  const int si = std::clamp((int)((a.x - x0) / C), 0, nx - 1), sj = std::clamp((int)((a.y - z0) / C), 0, nz - 1);
  const int ti = std::clamp((int)((b.x - x0) / C), 0, nx - 1), tj = std::clamp((int)((b.y - z0) / C), 0, nz - 1);
  std::vector<float> cost((size_t)nx * nz, 1e30f); std::vector<int> from((size_t)nx * nz, -1);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
  const int s = sj * nx + si, goal = tj * nx + ti;
  cost[s] = 0.f; q.push({0.f, s});
  while (!q.empty()) {
    generationCheckpoint();
    const auto [f, k] = q.top(); q.pop();
    if (k == goal) break;
    const int i = k % nx, j = k / nx;
    const float h = length(centre(i, j) - centre(ti, tj));
    if (f > cost[k] + h + 1e-3f) continue;
    for (int dj = -1; dj <= 1; dj++)
      for (int di = -1; di <= 1; di++) {
        if (!di && !dj) continue;
        const int ni = i + di, nj = j + dj; if (ni < 0 || nj < 0 || ni >= nx || nj >= nz) continue;
        const int nk = nj * nx + ni;
        if (depth[nk] < 6.f && nk != goal) continue;
        const float c = cost[k] + C * (di && dj ? 1.4142f : 1.f) * (depth[nk] < 15.f ? 3.f : 1.f);
        if (c < cost[nk]) { cost[nk] = c; from[nk] = k; q.push({c + length(centre(ni, nj) - centre(ti, tj)), nk}); }
      }
  }
  if (from[goal] < 0) return false;
  std::vector<vec2> cells;
  for (int k = goal; k >= 0; k = from[k]) cells.push_back(centre(k % nx, k / nx));
  std::reverse(cells.begin(), cells.end());
  cells.front() = a; cells.back() = b;
  auto clear = [&](vec2 p, vec2 q2) {
    const int n = std::max(1, (int)(length(q2 - p) / 40.f));
    for (int k = 0; k <= n; k++) { const vec2 m = p + (q2 - p) * ((float)k / n); if (world.naturalHeight(m.x, m.y) > -3.f) return false; }
    return true;
  };
  out.assign(1, a);
  for (size_t i = 0; i + 1 < cells.size();) {
    size_t j = cells.size() - 1;
    while (j > i + 1 && !clear(cells[i], cells[j])) j--;
    out.push_back(cells[j]); i = j;
  }
  return true;
}

// A coastal town's ferry terminal: from its harbour back to the nearest shore, the causeway's line - from 12 m inside the
// shore straight out past the harbour - in as few 40 m lengths as leave the berth beyond its linkspan in water deep
// enough for her (4.5 m under her middle, 2.5 m under her ends and her sides) and a straight way out from it, 250 m
// over 4 m and more to water 6 m deep. Eight lengths at the most; none for a town whose shore allows none
bool ferryPier(const World& world, int t, FerryPier& P, vec2& berth, vec2& out) {
  vec2 h; if (!harbourOf(world, t, h)) return false;
  vec2 S; bool found = false;
  for (float r = 10.f; r <= 600.f && !found; r += 10.f)
    for (int a = 0; a < 72; a++) {
      const vec2 p = h + vec2(cosf(a * 0.0873f), sinf(a * 0.0873f)) * r;
      if (world.naturalHeight(p.x, p.y) > 0.5f) { S = p; found = true; break; }
    }
  if (!found) return false;
  P.dir = (h - S) * (1.f / std::max(length(h - S), 1e-3f));
  P.root = S - P.dir * 12.f;
  const vec2 d = P.dir, side(-d.y, d.x);
  auto deep = [&](vec2 a, vec2 b, float under) {
    const int n = std::max(1, (int)(length(b - a) / 8.f));
    for (int k = 0; k <= n; k++) { const vec2 m = a + (b - a) * ((float)k / n); if (world.naturalHeight(m.x, m.y) > -under) return false; }
    return true;
  };
  for (int n = 1; n <= 8; n++) {
    const vec2 B = P.root + d * (40.f * n + kFerryBerth), O = B + d * 250.f;
    bool ok = deep(B - d * 20.f, B + d * 20.f, 4.5f) && deep(B - d * 48.f, B + d * 48.f, 2.5f);
    for (float s : {-9.f, 9.f}) ok = ok && deep(B + side * s - d * 40.f, B + side * s + d * 40.f, 2.5f);
    ok = ok && deep(B, O, 4.f) && world.naturalHeight(O.x, O.y) < -6.f;
    if (ok) { P.lengths = n; berth = B; out = O; return true; }
  }
  return false;
}

// The routes: between the coastal towns of each two islands, terminal to terminal, the nearest pair (32 km at the most);
// of those as few as join every island, the shortest first (Kruskal's). Each from its berth out to sea, across by sea,
// and in to the other's berth
void ferries(const World& world, WorldSceneryState& state) {
  state.ferries.clear();
  const auto& N = world.roads.nodes;
  const int townCount = (int)world.towns.size();
  struct Cand { float d; int a, b; };
  std::vector<Cand> cand;
  std::vector<int> isl(townCount, -1); std::vector<FerryPier> pier(townCount); std::vector<vec2> berth(townCount), out(townCount);
  std::vector<bool> coast(townCount, false);
  for (int t = 0; t < townCount && t < (int)N.size(); t++) { isl[t] = N[t].island; coast[t] = ferryPier(world, t, pier[t], berth[t], out[t]); }
  for (int a = 0; a < townCount; a++)
    for (int b = a + 1; b < townCount; b++)
      if (coast[a] && coast[b] && (!world.procedural || (isl[a] >= 0 && isl[b] >= 0)) && isl[a] != isl[b] && length(berth[a] - berth[b]) < 32000.f) cand.push_back({length(berth[a] - berth[b]), a, b});
  std::sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) { return x.d != y.d ? x.d < y.d : x.a != y.a ? x.a < y.a : x.b < y.b; });
  // Preserve the original island aliases for Solace; generated archipelagos may have more than 64 islands.
  const int islandCount = world.procedural ? (isl.empty() ? 0 : *std::max_element(isl.begin(), isl.end()) + 1) : 64;
  std::vector<int> root(islandCount); for (int i = 0; i < islandCount; i++) root[i] = i;
  auto find = [&](int i) { while (root[i] != i) i = root[i] = root[root[i]]; return i; };
  for (const Cand& c : cand) {
    generationCheckpoint();
    const int ra = find(world.procedural ? isl[c.a] : isl[c.a] & 63), rb = find(world.procedural ? isl[c.b] : isl[c.b] & 63);
    if (ra == rb) continue;
    FerryRoute r; r.townA = c.a; r.townB = c.b; r.pier[0] = pier[c.a]; r.pier[1] = pier[c.b];
    std::vector<vec2> sea;
    if (!seaRoute(world, out[c.a], out[c.b], sea)) continue;
    r.pts.push_back(berth[c.a]); r.pts.insert(r.pts.end(), sea.begin(), sea.end()); r.pts.push_back(berth[c.b]);
    r.along.assign(r.pts.size(), 0.f);
    for (size_t k = 1; k < r.pts.size(); k++) r.along[k] = r.along[k - 1] + length(r.pts[k] - r.pts[k - 1]);
    root[ra] = rb;
    state.ferries.push_back(std::move(r));
  }
}

// Each route's terminals: the causeway's lengths and the linkspan at its head
void ferryTerminals(WorldSceneryState& state, std::vector<PlacedEnt>& out) {
  for (const FerryRoute& r : state.ferries)
    for (const FerryPier& P : r.pier) {
      const float yaw = atan2f(P.dir.x, P.dir.y);
      for (int k = 0; k < P.lengths; k++) {
        const vec2 c = P.root + P.dir * (20.f + 40.f * k);
        out.push_back({EK_FERRYPIER, {c.x, 0.f, c.y, yaw, 1.f, 1.f, 1.f, 0.5f}});
      }
      const vec2 L = P.root + P.dir * (40.f * P.lengths + 5.f);
      out.push_back({EK_LINKSPAN, {L.x, 0.f, L.y, yaw, 1.f, 1.f, 1.f, 0.5f}});
      state.clearings.push_back(vec3(P.root.x, P.root.y, 30.f));
    }
}

// The yachts moored off each coastal town: in 1.2 to 7 m of water within 70 m of its shore and 2.5 km of its centre,
// the nearest to it first, 16 m apart - eight off a village, fourteen off a town, twenty-four off a city - clear of the
// container terminal's quay and of the ferries' terminals and berths, all lying to the same breeze
void harbourBoats(const World& world, std::vector<PlacedEnt>& out) {
  vec2 quay(1e9f, 1e9f);
  for (const PlacedEnt& p : civicLandmarks(world)) if (p.kind == EK_QUAY) quay = vec2(p.e.x, p.e.z);
  std::vector<vec2> harbours, pierA, pierB;   // (the berths; the terminals' causeways out to them and on past them)
  for (const FerryRoute& r : ferryRoutes(world)) {
    harbours.push_back(r.pts.front()); harbours.push_back(r.pts.back());
    for (const FerryPier& P : r.pier) { pierA.push_back(P.root); pierB.push_back(P.root + P.dir * (40.f * P.lengths + kFerryBerth + 60.f)); }
  }
  for (int t = 0; t < (int)world.towns.size(); t++) {
    const vec2 c(world.towns[t].x, world.towns[t].z);
    // (the 20 m grid round it, its rows side by side - the points as the one pass stepped them, each row's spots in its
    // order, the rows in theirs - the ground looked at only within its 2.5 km)
    std::vector<float> xs, zs;
    for (float z = c.y - 2500.f; z <= c.y + 2500.f; z += 20.f) zs.push_back(z);
    for (float x = c.x - 2500.f; x <= c.x + 2500.f; x += 20.f) xs.push_back(x);
    std::vector<std::vector<std::pair<float, vec2>>> rows(zs.size());
    parallelFor((int)zs.size(), [&](int j) {
      const float z = zs[j];
      for (const float x : xs) {
        generationCheckpoint();
        const vec2 p(x, z); const float d = length(p - c);
        if (d > 2500.f || length(p - quay) < 450.f) continue;
        const float g = world.naturalHeight(x, z);
        if (g > -1.2f || g < -7.f) continue;
        bool shore = false, near = false;
        for (int k = 0; k < 8 && !shore; k++) shore = world.naturalHeight(x + 70.f * cosf(k * 0.785f), z + 70.f * sinf(k * 0.785f)) > 0.5f;
        for (const vec2& h : harbours) near = near || length(p - h) < 250.f;
        for (size_t k = 0; k < pierA.size() && !near; k++) {
          const vec2 ab = pierB[k] - pierA[k]; const float u = std::clamp(((p.x - pierA[k].x) * ab.x + (p.y - pierA[k].y) * ab.y) / std::max(ab.x * ab.x + ab.y * ab.y, 1.f), 0.f, 1.f);
          near = length(pierA[k] + ab * u - p) < 80.f;
        }
        if (shore && !near && clearOfRunways(world, x, z, 1500.f) && seaLaneClear(world, x, z, 600.f, 120.f)) rows[j].push_back({d, p});
      }
    });
    std::vector<std::pair<float, vec2>> spots;
    for (const auto& r : rows) spots.insert(spots.end(), r.begin(), r.end());
    std::sort(spots.begin(), spots.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.x < b.second.x; });
    const int most = world.towns[t].kind == 2 ? 24 : world.towns[t].kind == 1 ? 14 : 8;
    std::vector<vec2> moored;
    for (const auto& sp : spots) {
      generationCheckpoint();
      if ((int)moored.size() >= most) break;
      bool crowded = false; for (const vec2& m : moored) crowded = crowded || length(m - sp.second) < 16.f;
      if (crowded) continue;
      moored.push_back(sp.second);
      const float h = hash2i((int)sp.second.x, (int)sp.second.y);
      out.push_back({EK_BOAT, {sp.second.x, 0.f, sp.second.y, 0.7f + 0.3f * (h - 0.5f), 0.9f + 0.2f * h, 0.9f + 0.2f * h, 0.9f + 0.2f * h, h}});
    }
  }
}
}  // namespace

void sceneryBakeLandmarks(World& world) {
  generationPhase(GenerationPhase::Landmarks);
  WorldSceneryState& state = sceneryStateForBuild(world);
  std::vector<PlacedEnt> out;
  state.clearings.clear();
  windFarms(world, out);
  stoneCircle(world, state, out);
  shipwreck(world, out);
  offshorePlatform(world, out);
  observatory(world, state, out);
  starFort(world, state, out);
  lighthouseKey(world, state, out);
  realLighthouses(world, state, out);
  racetrack(world, state, out);
  fjordhavenDam(world, state, out);
  ferries(world, state);
  ferryTerminals(state, out);
  for (const Airport& a : world.airports)   // (a seaplane base's dock's root: its office and windsock, airport_scenery.cpp)
    if (a.water() && a.dockLen > 0.f) state.clearings.push_back(vec3(a.dockX - a.dockDirX * 20.f, a.dockZ - a.dockDirZ * 20.f, 40.f));
  harbourBoats(world, out);
  for (const PlacedEnt& c : civicLandmarks(world)) {   // (the cities': placed with their lots; their grounds cleared)
    out.push_back(c);
    if (c.kind == EK_STADIUM) state.clearings.push_back(vec3(c.e.x, c.e.z, 105.f));
    if (c.kind == EK_QUAY)
      for (float x = -180.f; x <= 180.f; x += 90.f)
        state.clearings.push_back(vec3(c.e.x + cosf(c.e.yaw) * x - sinf(c.e.yaw) * 62.f, c.e.z - sinf(c.e.yaw) * x - cosf(c.e.yaw) * 62.f, 80.f));
  }
  generationCheckpoint();
  state.landmarks.build(out);
}

const std::vector<vec3>& landmarkClearings(const World& world) { return sceneryState(world).clearings; }
const std::vector<PlacedEnt>& landmarks(const World& world) { return sceneryState(world).landmarks.items; }
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(const World& world, int cx, int cz) { return sceneryState(world).landmarks.in(cx, cz); }
const std::vector<vec3>& landmarkClearings() { return landmarkClearings(sceneryWorld()); }
const std::vector<PlacedEnt>& landmarks() { return landmarks(sceneryWorld()); }
std::pair<const PlacedEnt*, const PlacedEnt*> landmarksIn(int cx, int cz) { return landmarksIn(sceneryWorld(), cx, cz); }

// ---------------------------------------------------------------- the Fjordhaven reservoir
vec2 Reservoir::crown() const { return vec2(x, z) + vec2(vx, vz) * (archR() * (1.f - sqrtf(1.f - 0.6944f))); }   // (sin of its half-angle: 150/180)
vec2 Reservoir::archCentre() const { return crown() - vec2(vx, vz) * archR(); }

// The texels the lake covers: from 60 m above the dam's crown, every texel joined to it whose ground is under the
// water's level, none within 45 m of the dam's upstream face (its mesh carries the water to its face) - nor any, should
// the water reach 3 km from the dam or the sea: it would spill
std::vector<int> reservoirTexels(const World& world, const Reservoir& r) {
  const vec2 P(r.x, r.z), v(r.vx, r.vz), A = r.archCentre(), start = r.crown() + v * 60.f;
  const float keep = r.archR() + 45.f;
  auto texel = [](float c) { return std::clamp((int)floorf((c + WORLD_HALF) / HM_TEXEL), 0, HM_N - 1); };
  auto centre = [](int i) { return -WORLD_HALF + (i + 0.5f) * HM_TEXEL; };
  std::vector<int> wet, queue{texel(start.y) * HM_N + texel(start.x)};
  std::unordered_set<int> seen(queue.begin(), queue.end());
  if (world.naturalHeight(centre(texel(start.x)), centre(texel(start.y))) >= r.level) return wet;
  while (!queue.empty()) {
    generationCheckpoint();
    const int k = queue.back(); queue.pop_back(); wet.push_back(k);
    const int i = k % HM_N, j = k / HM_N;
    for (int d = 0; d < 4; d++) {
      const int ni = i + (d == 0) - (d == 1), nj = j + (d == 2) - (d == 3), nk = nj * HM_N + ni;
      if (ni < 0 || nj < 0 || ni >= HM_N || nj >= HM_N || seen.count(nk)) continue;
      const vec2 p(centre(ni), centre(nj));
      if (length(p - A) < keep) continue;
      const float g = world.naturalHeight(p.x, p.y);
      if (g >= r.level) continue;
      if (g < 1.f || length(p - P) > 3000.f) return {};   // (it would spill)
      seen.insert(nk); queue.push_back(nk);
    }
  }
  std::sort(wet.begin(), wet.end());
  return wet;
}

// The gorge: within 14 km of Fjordhaven, on the mountains' flanks (60 to 1300 m), a valley whose walls rise 60 to 80 m
// over its floor within a chord of 400 m at the most, its floor rising gently upstream (and staying under the water)
// and falling away downstream - searched every 50 m and 15 degrees. Of those whose lake holds (it spills nowhere), drowns
// no road, no settlement and no airfield, nor leaves dry ground between itself and the dam: the largest lake, and the
// dam that holds it the shortest
bool findReservoir(const World& world, Reservoir& out) {
  if (world.procedural) return false;   // The authored Fjordhaven reservoir is Solace-only.
  int fj = -1; for (int t = 0; t < (int)world.towns.size(); t++) if (!strcmp(world.towns[t].name, "Fjordhaven")) fj = t;
  if (fj < 0) return false;
  const vec2 F(world.towns[fj].x, world.towns[fj].z);
  const float step = 50.f; const int n = (int)(28000.f / step);
  std::vector<std::vector<Reservoir>> rows(n);
  parallelFor(n, [&](int j) {
    const float z = F.y - 14000.f + j * step;
    for (int i = 0; i < n; i++) {
      const float x = F.x - 14000.f + i * step;
      if (hypotf(x - F.x, z - F.y) > 14000.f) continue;
      const float g0 = world.naturalHeight(x, z);
      if (g0 < 60.f || g0 > 1300.f) continue;
      for (float D : {80.f, 70.f, 60.f})
        for (int d = 0; d < 12; d++) {
          const vec2 u(cosf(d * 15.f * DEG), sinf(d * 15.f * DEG));
          float tl = -1.f, tr = -1.f;
          for (float t = 10.f; t <= 200.f && (tl < 0.f || tr < 0.f); t += 10.f) {
            if (tl < 0.f && world.naturalHeight(x - u.x * t, z - u.y * t) >= g0 + D) tl = t;
            if (tr < 0.f && world.naturalHeight(x + u.x * t, z + u.y * t) >= g0 + D) tr = t;
          }
          if (tl < 0.f || tr < 0.f) continue;
          for (float sg : {-1.f, 1.f}) {
            const vec2 v = vec2(-u.y, u.x) * sg;
            const float up = world.naturalHeight(x + v.x * 250.f, z + v.y * 250.f), dn = world.naturalHeight(x - v.x * 250.f, z - v.y * 250.f);
            if (up < g0 || up > g0 + D - 10.f || dn > g0 - 3.f) continue;
            // (the chord centred between the walls, the dam's foot 4 m into the floor, its crest D over it)
            const vec2 P = vec2(x, z) + u * (0.5f * (tr - tl));
            Reservoir r; r.on = 1.f; r.x = P.x; r.z = P.y; r.vx = v.x; r.vz = v.y; r.chord = tl + tr; r.floor = g0; r.crest = D;
            r.level = g0 - 4.f + 67.f * (D + 4.f) / 70.f;
            rows[j].push_back(r);
          }
        }
    }
  });
  std::vector<Reservoir> cand;
  for (auto& r : rows) cand.insert(cand.end(), r.begin(), r.end());
  std::vector<float> area(cand.size(), 0.f);
  parallelFor((int)cand.size(), [&](int c) {
    const Reservoir& r = cand[c];
    const std::vector<int> wet = reservoirTexels(world, r);
    if (wet.size() < 60) return;   // (0.09 km2 at least)
    const vec2 A = r.archCentre(), v(r.vx, r.vz);
    for (int k : wet) {
      const float x = -WORLD_HALF + (k % HM_N + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (k / HM_N + 0.5f) * HM_TEXEL;
      if (roadEdgeDistance(world.roadGrid, x, z) < 25.f || settlementShare(world.settlements, x, z) < 1.5f || !clearOfRunways(world, x, z, 3000.f)) return;
      // (beside the dam, past where its mesh carries the water: the lake no nearer it than its arch's ends)
      const vec2 d = vec2(x, z) - A; const float r0 = length(d);
      if (r0 < r.archR() + 90.f && dot2(d, v) < r0 * cosf(75.f * DEG)) return;
    }
    if (roadEdgeDistance(world.roadGrid, r.x, r.z) < 60.f) return;
    area[c] = wet.size() * HM_TEXEL * HM_TEXEL;
  });
  std::vector<int> order;
  for (size_t c = 0; c < cand.size(); c++) if (area[c] > 0.f) order.push_back((int)c);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return fabsf(area[a] - area[b]) > 1.f ? area[a] > area[b] : cand[a].chord != cand[b].chord ? cand[a].chord < cand[b].chord : a < b; });
  for (int c : order) {
    generationCheckpoint();
    Reservoir r = cand[c];
    // the lake's square: round its texels, three texels' margin; then its fine mask, which must not spill out of it
    const std::vector<int> wet = reservoirTexels(world, r);
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int k : wet) {
      const float x = -WORLD_HALF + (k % HM_N + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (k / HM_N + 0.5f) * HM_TEXEL;
      x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z);
    }
    r.cx = 0.5f * (x0 + x1); r.cz = 0.5f * (z0 + z1); r.r = 0.5f * std::max(x1 - x0, z1 - z0) + 3.f * HM_TEXEL;
    LakeMask m;
    if (!reservoirMask(world, r, m)) continue;
    out = r;
    return true;
  }
  return false;
}

bool reservoirMask(const World& world, const Reservoir& r, LakeMask& out) {
  out = LakeMask();
  const float cell = out.cell; const int n = (int)ceilf(2.f * r.r / cell);
  out.n = n; out.x0 = r.cx - 0.5f * n * cell; out.z0 = r.cz - 0.5f * n * cell; out.m.assign((size_t)n * n, 0.f);
  auto centre = [&](int i, int j) { return vec2(out.x0 + (i + 0.5f) * cell, out.z0 + (j + 0.5f) * cell); };
  const vec2 A = r.archCentre(), start = r.crown() + vec2(r.vx, r.vz) * 60.f;
  const float keep = r.archR() + 2.f;   // (the water up to 2 m off the dam's face: the mask's edge falls inside the dam)
  std::vector<float> g((size_t)n * n);
  parallelFor(n, [&](int j) { for (int i = 0; i < n; i++) { const vec2 p = centre(i, j); g[(size_t)j * n + i] = world.naturalHeight(p.x, p.y); } });
  auto open = [&](int i, int j) { return length(centre(i, j) - A) >= keep; };
  const int si = (int)floorf((start.x - out.x0) / cell), sj = (int)floorf((start.y - out.z0) / cell);
  if (si <= 0 || sj <= 0 || si >= n - 1 || sj >= n - 1 || g[(size_t)sj * n + si] >= r.level) return false;
  std::vector<int> queue{sj * n + si};
  out.m[(size_t)sj * n + si] = 1.f;
  while (!queue.empty()) {
    generationCheckpoint();
    const int k = queue.back(); queue.pop_back();
    const int i = k % n, j = k / n;
    if (i == 0 || j == 0 || i == n - 1 || j == n - 1) return false;   // (it would spill out of its square)
    for (int d = 0; d < 4; d++) {
      const int ni = i + (d == 0) - (d == 1), nj = j + (d == 2) - (d == 3), nk = nj * n + ni;
      if (out.m[nk] > 0.f || !open(ni, nj) || g[nk] >= r.level) continue;
      out.m[nk] = 1.f; queue.push_back(nk);
    }
  }
  for (int pass = 0; pass < 2; pass++) {   // (on into the shore two cells: the mask's edge lies under the ground)
    std::vector<float> m = out.m;
    for (int j = 1; j < n - 1; j++)
      for (int i = 1; i < n - 1; i++) {
        const size_t k = (size_t)j * n + i;
        if (out.m[k] > 0.f || !open(i, j) || g[k] < r.level) continue;
        bool next = false;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) next = next || out.m[k + (size_t)(dj * n + di)] > 0.f;
        if (next) m[k] = 1.f;
      }
    out.m.swap(m);
  }
  return true;
}

const std::vector<FerryRoute>& ferryRoutes(const World& world) { return sceneryState(world).ferries; }
const std::vector<FerryRoute>& ferryRoutes() { return ferryRoutes(g_world); }

// A ferry's place at time t: her route's length each way at kFerrySpeed, kFerryDwell at each end; each route's timetable
// set off by its own amount. Her bows along the route from A to B (out stern first on the way back: she's double-ended);
// the heading a look 120 m along it, so she comes round her turns
bool ferryAt(const FerryRoute& r, float t, vec2& at, vec2& heading, bool* docked) {
  if (r.pts.size() < 2) return false;
  const float L = r.along.back(), run = L / kFerrySpeed, T = 2.f * (run + kFerryDwell);
  float ph = fmodf(t + T * hash2i(r.townA * 7 + 1, r.townB * 13 + 5), T); if (ph < 0.f) ph += T;
  float s; bool dock = false;
  if (ph < kFerryDwell) { s = 0.f; dock = true; }
  else if (ph < kFerryDwell + run) s = (ph - kFerryDwell) * kFerrySpeed;
  else if (ph < 2.f * kFerryDwell + run) { s = L; dock = true; }
  else s = L - (ph - 2.f * kFerryDwell - run) * kFerrySpeed;
  auto pointAt = [&](float x) {
    x = std::clamp(x, 0.f, L);
    size_t k = 0; while (k + 2 < r.along.size() && r.along[k + 1] < x) k++;
    const float u = std::clamp((x - r.along[k]) / std::max(r.along[k + 1] - r.along[k], 1e-3f), 0.f, 1.f);
    return r.pts[k] + (r.pts[k + 1] - r.pts[k]) * u;
  };
  at = pointAt(s);
  const vec2 ahead = pointAt(std::min(s + 120.f, L)) - pointAt(std::max(s - 120.f, 0.f));
  heading = ahead * (1.f / std::max(length(ahead), 1e-3f));
  if (docked) *docked = dock;
  return true;
}

void ferryInstances(const World& world, float t, std::vector<std::pair<int, Ent>>& out) {
  for (const FerryRoute& r : ferryRoutes(world)) {
    vec2 at, h;
    if (!ferryAt(r, t, at, h)) continue;
    out.push_back({EK_FERRY, Ent{at.x, 0.f, at.y, atan2f(h.x, h.y), 1.f, 1.f, 1.f, 0.5f}});
  }
}

void ferryInstances(float t, std::vector<std::pair<int, Ent>>& out) { ferryInstances(g_world, t, out); }
