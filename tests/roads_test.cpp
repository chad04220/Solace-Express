// The road network laid out as roads are (road_network.h): every stretch within its class's grade, crests and sags
// rounded to its K, its bends no tighter than its radius off the climbs; the ground built into a level platform under
// it with banks no steeper than a cut or a fill stands; no two stretches so close for the height between them that no
// bank could join them; and where a road runs onto a bridge the deck takes up the road's bed at its own height and
// width, the bed stopping square at the joint under the deck's slab.
#include "../src/world.h"
#include "../src/scenery.h"
#include "test_world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

float airportInfluence(float x, float z);   // world.cpp

static int fails = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { fails++; printf("FAIL %s\n", what); } }
static float pct(std::vector<float> v, float q) {
  if (v.empty()) return 0.f;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, (size_t)(q * v.size()))];
}
static const char* kClass[RC_COUNT] = {"highway", "road", "lane", "track", "street", "yard", "circuit", "pitlane"};

int main() {
  buildTestWorld();
  World& w = g_world;
  const RoadNetwork& net = w.roads;
  // ---- along each road: its grade, its vertical curves, its bends, off the stretches by an airfield (on the ground as
  // it is there) and a road whose ends stand too far apart for its length (the shortfall spread along it)
  std::vector<float> bend[RC_COUNT], radius[RC_COUNT];
  for (const RoadPath& p : net.paths) {
    const RoadSpec& s = roadSpec(p.cls);
    const size_t n = p.pts.size();
    std::vector<float> along(n, 0.f);
    for (size_t k = 1; k < n; k++) along[k] = along[k - 1] + hypotf(p.pts[k].x - p.pts[k - 1].x, p.pts[k].z - p.pts[k - 1].z);
    // (by an airfield, and 100 m either side: the road on its ground as it is, and between two such stretches what
    // the ground there leaves it)
    std::vector<uint8_t> held(n, 0);
    for (size_t k = 0; k + 1 < n; k++)
      if (roadByAirfield(p.pts[k], p.pts[k + 1], p.cls))
        for (size_t q = 0; q < n; q++) held[q] = held[q] || (along[q] > along[k] - 100.f && along[q] < along[k + 1] + 100.f);
    const float spread = fabsf(p.pts.back().h - p.pts.front().h) / std::max(along.back(), 1.f);
    const float steepest = std::max(s.maxGrade, spread);
    for (size_t k = 0; k + 1 < n; k++) {
      if (held[k] || held[k + 1]) continue;
      const float run = along[k + 1] - along[k], grade = fabsf(p.pts[k + 1].h - p.pts[k].h) / std::max(run, 1e-3f);
      if (grade > steepest * 1.002f + 1e-3f) printf("  %s %d->%d at %.0f %.0f: grade %.3f\n", kClass[p.cls], p.from, p.to, p.pts[k].x, p.pts[k].z, grade);
      check(grade <= steepest * 1.002f + 1e-3f, "every stretch within its class's grade");
    }
    for (size_t k = 1; k + 1 < n; k++) {
      if (held[k - 1] || held[k] || held[k + 1]) continue;
      const float d0 = along[k] - along[k - 1], d1 = along[k + 1] - along[k];
      const float g0 = (p.pts[k].h - p.pts[k - 1].h) / d0, g1 = (p.pts[k + 1].h - p.pts[k].h) / d1;
      bend[p.cls].push_back(fabsf(g1 - g0) / (0.5f * (d0 + d1)) * s.curveK * 100.f);   // (1: the K's rate)
      // the bend's radius: the circle through the point and its neighbours - off the climbs (a hairpin up a slope keeps
      // its tightness) and away from the road's ends (where it meets another)
      if (along[k] < 2.f * s.minRadius || along.back() - along[k] < 2.f * s.minRadius || fabsf(g0) > 0.6f * s.maxGrade) continue;
      const float ux = p.pts[k].x - p.pts[k - 1].x, uz = p.pts[k].z - p.pts[k - 1].z, vx = p.pts[k + 1].x - p.pts[k].x, vz = p.pts[k + 1].z - p.pts[k].z;
      const float cr = fabsf(ux * vz - uz * vx), chord = hypotf(p.pts[k + 1].x - p.pts[k - 1].x, p.pts[k + 1].z - p.pts[k - 1].z);
      radius[p.cls].push_back(cr > 1e-6f ? d0 * d1 * chord / (2.f * cr) / s.minRadius : 1e3f);   // (1: the class's radius)
    }
  }
  for (int c = 0; c < RC_COUNT; c++) {
    if (bend[c].empty()) continue;
    printf("%-7s change of grade p99 %.2f of its K's, radius p5 %.2f of its own (%zu points)\n", kClass[c], pct(bend[c], 0.99f), pct(radius[c], 0.05f), bend[c].size());
    // (a street's, a farm track's and its yard's: level through its junctions and crossings, then away at its grade - as
    // a town's slow streets are, and a farm's tracks)
    if (c != RC_STREET && c != RC_TRACK && c != RC_YARD) check(pct(bend[c], 0.99f) <= 1.05f, "crests and sags rounded to the class's K");
    check(radius[c].empty() || pct(radius[c], 0.05f) >= (c == RC_HIGHWAY ? 0.75f : 0.85f), "bends no tighter than the class's radius off the climbs");
  }
  // ---- every segment listed under the texel at its middle (unless that texel's list is full: a junction's), the lists
  // all within the bits a texel's first entry has - past them a texel lists nothing, and its roads lie on the ground
  // as it is
  {
    const RoadGrid& g = w.roadGrid;
    int unlisted = 0;
    for (size_t si = 0; si < g.segs.size(); si++) {
      const RoadSegment& s = g.segs[si];
      const uint32_t h = g.head[(size_t)RoadGrid::cell((s.az + s.bz) * 0.5f) * RoadGrid::N + RoadGrid::cell((s.ax + s.bx) * 0.5f)];
      bool found = RoadGrid::count(h) >= 63;
      for (uint32_t k = RoadGrid::first(h), e = k + RoadGrid::count(h); k < e && !found; k++) found = g.list[k] == si;
      unlisted += !found;
    }
    uint32_t fullest = 0; for (uint32_t h : g.head) fullest = std::max(fullest, RoadGrid::count(h));
    printf("the texels' lists: %zu entries of %u, the fullest %u long; %d segments unlisted\n", g.list.size(), RoadGrid::kFirstMask, fullest, unlisted);
    check(unlisted == 0 && g.list.size() + 63 <= RoadGrid::kFirstMask, "every segment listed where it lies, the lists within their bits");
  }
  // ---- the bed: level across the platform, the banks a cut or fill could stand at; where no other road is near
  std::vector<float> tilt[RC_COUNT], bank[RC_COUNT];
  for (const RoadSegment& s : w.roadGrid.segs) {
    if (s.flags & (RS_BRIDGE | RS_NOGRADE)) continue;
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L = hypotf(dx, dz);
    if (L < 4.f) continue;
    const float ux = dx / L, uz = dz / L, nx = -uz, nz = ux, P = roadSpec(s.cls).halfPlatform;
    for (float t : {0.25f, 0.5f, 0.75f}) {
      const float x = s.ax + dx * t, z = s.az + dz * t;
      bool alone = true;
      for (float o : {-P - 6.f, 0.f, P + 6.f}) for (float a : {-8.f, 8.f}) {
        int sg = -1; const float e = roadEdgeDistance(w.roadGrid, x + nx * o + ux * a, z + nz * o + uz * a, &sg);
        if (sg >= 0 && e < 2.f && w.roadGrid.segs[sg].path != s.path) alone = false;
      }
      if (!alone) continue;
      tilt[s.cls].push_back(fabsf(w.height(x + nx * 0.8f * P, z + nz * 0.8f * P) - w.height(x - nx * 0.8f * P, z - nz * 0.8f * P)) / (1.6f * P));
      for (float side : {-1.f, 1.f}) {   // (the steepest metre out to the bank's reach, beyond the natural ground's own)
        float worst = 0, prev = w.height(x + nx * side * P, z + nz * side * P);
        for (float o = P + 1.f; o <= P + roadBankMax(s.cls); o += 1.f) {
          const float px = x + nx * side * o, pz = z + nz * side * o, h = w.height(px, pz);
          worst = std::max(worst, fabsf(h - prev) - fabsf(w.naturalHeight(px, pz) - w.naturalHeight(px - nx * side, pz - nz * side)));
          prev = h;
        }
        bank[s.cls].push_back(worst);
      }
    }
  }
  for (int c = 0; c < RC_COUNT; c++) {
    if (tilt[c].empty()) continue;
    printf("%-7s across its platform: tilt p99 %.4f max %.4f; banks p99 %.2f max %.2f beyond the ground's own slope\n", kClass[c], pct(tilt[c], 0.99f), pct(tilt[c], 1.f), pct(bank[c], 0.99f), pct(bank[c], 1.f));
    check(pct(tilt[c], 0.99f) <= (c <= RC_ROAD ? 0.005f : 0.025f) && pct(tilt[c], 1.f) <= 0.1f, "the platform level across");
    check(pct(bank[c], 0.99f) <= 0.8f && pct(bank[c], 1.f) <= 1.6f, "the banks no steeper than a cut or a fill stands");
  }
  // ---- no two stretches (off the bridges) nearer than the bank between them could join at their heights
  int conflicts = 0;
  for (size_t a = 0; a < net.paths.size(); a++) for (size_t b = a; b < net.paths.size(); b++) {
    const RoadPath &A = net.paths[a], &B = net.paths[b];
    float sa = 0;
    for (size_t i = 0; i < A.pts.size(); i++) {
      if (i) sa += hypotf(A.pts[i].x - A.pts[i - 1].x, A.pts[i].z - A.pts[i - 1].z);
      if ((i > 0 && A.bridge[i - 1]) || (i + 1 < A.pts.size() && A.bridge[i])) continue;
      float sb = 0;
      for (size_t j = 0; j < B.pts.size(); j++) {
        if (j) sb += hypotf(B.pts[j].x - B.pts[j - 1].x, B.pts[j].z - B.pts[j - 1].z);
        if ((a == b && fabsf(sb - sa) < 150.f) || (j > 0 && B.bridge[j - 1]) || (j + 1 < B.pts.size() && B.bridge[j])) continue;
        const float d = hypotf(A.pts[i].x - B.pts[j].x, A.pts[i].z - B.pts[j].z);
        if (d > 80.f) continue;
        const float gap = d - roadSpec(A.cls).halfPlatform - roadSpec(B.cls).halfPlatform;
        conflicts += fabsf(A.pts[i].h - B.pts[j].h) > std::max(gap, 0.f) + 1.5f;
      }
    }
  }
  printf("%d pairs of points too close for the height between them\n", conflicts);
  check(conflicts <= 12, "no stretches too close for the height between them (a few at a junction's mouth)");
  // ---- the bridges' joints: the deck as wide as the road's platform, square to the road, at the bed's height; under it
  // the bed carried on just below the deck's surface, then falling away
  int joints = 0;
  for (const Bridge& b : w.bridges) {
    check(b.halfDeck == roadSpec(b.cls).halfPlatform, "the deck as wide as the road's platform");
    for (int end = 0; end < 2; end++) {
      const RoadPoint& p = end ? b.deck.back() : b.deck.front();
      const float ux = end ? b.outX : -b.inX, uz = end ? b.outZ : -b.inZ;   // (along the road, away from the bridge)
      if (ux == 0.f && uz == 0.f) continue;
      joints++;
      const RoadPath& path = net.paths[b.path];
      const int k = end ? b.last : b.first, o = end ? k + 1 : k - 1;
      const float rx = path.pts[o].x - path.pts[k].x, rz = path.pts[o].z - path.pts[k].z, rl = hypotf(rx, rz);
      check(fabsf(rx / rl - ux) < 1e-3f && fabsf(rz / rl - uz) < 1e-3f, "a deck's end square to the road running onto it");
      const float grade = fabsf(path.pts[o].h - path.pts[k].h) / rl;
      bool level = true;
      for (float across : {-0.8f, 0.f, 0.8f}) {
        const float ax = uz * across * b.halfDeck, az = -ux * across * b.halfDeck;
        level = level && fabsf(w.height(p.x + ux * 0.5f + ax, p.z + uz * 0.5f + az) - p.h) <= 0.5f * grade + 0.05f;
      }
      check(level, "the road's bed meets the deck at its height, across its width");
      if (!level) printf("  joint of path %d (%d..%d) end %d at %.0f %.0f: deck %.2f, the bed half a metre off it %.2f\n", b.path, b.first, b.last, end, p.x, p.z, p.h, w.height(p.x + ux * 0.5f, p.z + uz * 0.5f));
      const float under = w.height(p.x - ux * 1.5f, p.z - uz * 1.5f);
      check(under <= p.h - 0.1f && under >= p.h - 1.f, "past the joint the bed carried on just under the deck's surface");
      check(w.height(p.x - ux * 8.f, p.z - uz * 8.f) <= p.h - 1.f || w.naturalHeight(p.x - ux * 8.f, p.z - uz * 8.f) > p.h - 1.f,
            "then falling away under the first span");
    }
  }
  check(joints > 0, "there are joints to look at");
  // ---- the road furniture (road_furniture.cpp): each kind out along the network; on dry land, off the airfields; the
  // poles and signs off every platform; the street lamps where the terrain shader pools their light; each pole's wires
  // ending at the next pole; laid out the same every time
  {
    const std::vector<RoadFurniture> items = roadFurniture();
    int n[EK_COUNT] = {}, offPool = 0, loose = 0, onRoad = 0;
    std::vector<vec2> poles;
    for (const RoadFurniture& f : items) if (f.kind == EK_POLE) poles.push_back(vec2(f.e.x, f.e.z));
    for (const RoadFurniture& f : items) {
      const Ent& e = f.e;
      n[f.kind]++;
      check(f.kind >= EK_STREETLAMP && f.kind <= EK_POLE && std::isfinite(e.x + e.y + e.z + e.yaw + e.sx + e.sy + e.sz + e.seed), "road furniture: its kinds, finite");
      check(e.y > 0.5f && airportInfluence(e.x, e.z) <= 0.001f, "road furniture on dry land, off the airfields");
      int sg = -1; const float edge = roadEdgeDistance(w.roadGrid, e.x, e.z, &sg);
      if (f.kind == EK_POLE || f.kind == EK_ROADSIGN) onRoad += edge < 0.9f;
      if (f.kind == EK_STREETLAMP && sg >= 0 && w.roadGrid.segs[sg].cls == RC_STREET) {   // (as roads.glsl roadAt and the pools)
        const RoadSegment& r = w.roadGrid.segs[sg];
        const float dx = r.bx - r.ax, dz = r.bz - r.az, L = std::max(hypotf(dx, dz), 1e-3f);
        const float t = std::clamp(((e.x - r.ax) * dx + (e.z - r.az) * dz) / (L * L), 0.f, 1.f), along = r.along + t * L;
        const float qx = e.x - (r.ax + dx * t), qz = e.z - (r.az + dz * t), across = (dx * qz - dz * qx) / L;
        const float k = fmodf(along - (across > 0.f ? 15.f : 0.f) + 3000.f, 30.f);
        offPool += std::min(k, 30.f - k) > 0.6f;
      }
      if (f.kind == EK_GUARDRAIL) check(e.seed >= 0.2f && e.seed <= 0.8f, "a guardrail's grade within reason");
      if (f.kind == EK_POLE && e.seed < 1.5f) {   // (its wires' far end: 50 m on along its front, scaled, up its grade)
        const float d = 50.f * e.sz, ex = e.x + sinf(e.yaw) * d, ez = e.z + cosf(e.yaw) * d;
        bool met = false;
        for (const vec2& q : poles) met = met || hypotf(q.x - ex, q.y - ez) < 0.05f;
        loose += !met;
      }
    }
    printf("road furniture: %d street lamps (%d off their pools), %d guardrails, %d signs, %d poles (%d wires to no pole); %d on a road\n",
           n[EK_STREETLAMP], offPool, n[EK_GUARDRAIL], n[EK_ROADSIGN], n[EK_POLE], loose, onRoad);
    check(n[EK_STREETLAMP] > 5000 && n[EK_GUARDRAIL] > 2000 && n[EK_ROADSIGN] > 60 && n[EK_POLE] > 1500, "road furniture of every kind");
    check(offPool * 100 <= n[EK_STREETLAMP], "the street lamps where their light is pooled");
    check(loose == 0, "each pole's wires end at the next pole");
    check(onRoad == 0, "no pole or sign on a road");
    sceneryBakeRoadFurniture(w);
    const std::vector<RoadFurniture>& again = roadFurniture();
    bool same = again.size() == items.size();
    for (size_t i = 0; same && i < items.size(); i++) same = again[i].kind == items[i].kind && !memcmp(&again[i].e, &items[i].e, sizeof(Ent));
    check(same, "the road furniture laid out the same every time");
  }
  printf("roads: %zu paths, %zu bridges with %d joints; %d checks, %d failures\n", net.paths.size(), w.bridges.size(), joints, checks, fails);
  return fails ? 1 : 0;
}
