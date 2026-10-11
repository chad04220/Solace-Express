// The road network's bridges (bridges.h, bridge_mesh.h): every span the network flags has one, its deck the road's own
// centreline; every stretch over water is one; its piers stand on the ground or the sea floor and carry the girders,
// never more than 60 m apart; it is solid where it is drawn and open under its deck between the piers; its mesh is
// whole. A long crossing of the water has its main span - a truss on a highway, a tied arch on a road - its steel
// solid and the road under it clear; a mountain lane's viaduct is masonry, open under each arch and solid above it.
// And the roads don't hang off the hillsides any more: no lane or track stands far above the ground off a
// bridge, and no bridge over dry ground runs along a slope unless it stands high over it (v3.45 had 2 km of lane
// viaduct on stilts down Kaleo's side)
#include "../src/bridge_mesh.h"
#include "../src/entities.h"
#include "test_world.h"
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { fails++; printf("FAIL %s\n", what); } }

int main() {
  buildTestWorld();
  const World& w = g_world;
  // every flagged run of the network is a bridge, its deck the road's own points
  int runs = 0;
  for (size_t pi = 0; pi < w.roads.paths.size(); pi++) {
    const RoadPath& p = w.roads.paths[pi];
    for (size_t k = 0; k + 1 < p.pts.size();) {
      if (!p.bridge[k]) { k++; continue; }
      size_t e = k; while (e + 1 < p.pts.size() && p.bridge[e]) e++;
      const Bridge* b = nullptr;
      for (const Bridge& x : w.bridges) if (x.path == (int)pi && x.first == (int)k && x.last == (int)e) b = &x;
      check(b != nullptr, "a bridge for every flagged span");
      if (b) {
        bool same = b->deck.size() == e - k + 1;
        for (size_t q = 0; same && q < b->deck.size(); q++) same = b->deck[q].x == p.pts[k + q].x && b->deck[q].z == p.pts[k + q].z && b->deck[q].h == p.pts[k + q].h;
        check(same, "the deck is the road's own centreline and heights");
      }
      runs++; k = e;
    }
    // over the water, always a bridge
    for (size_t k = 0; k + 1 < p.pts.size(); k++)
      if (w.naturalHeight(p.pts[k].x, p.pts[k].z) < 0.5f && w.naturalHeight(p.pts[k + 1].x, p.pts[k + 1].z) < 0.5f) check(p.bridge[k] != 0, "a road over the water is on a bridge");
  }
  check(runs == (int)w.bridges.size() && runs > 0, "one bridge per span, and there are some");
  int water = 0, piers = 0, mains[3] = {};
  for (const Bridge& b : w.bridges) {
    water += b.water;
    // the piers: on the ground (or into the sea floor), up to the girders, clear of the ground below the deck
    std::vector<float> at = {b.along.front()};
    for (const BridgePier& p : b.piers) {
      piers++;
      const float g = w.groundHeight(p.x, p.z);
      check(p.bottom <= g + 0.01f && p.bottom > g - 4.f, "a pier stands on the ground or the sea floor");
      check(p.top - std::max(g, 0.f) >= 1.5f, "a pier is clear of the ground or the sea below the girders");
      check(p.top < p.top + b.depth && p.halfWidth > 0.5f && p.halfThick >= 0.7f, "a pier has its size");
      float best = 1e9f, s = 0;   // its distance along the deck: the nearest point of the centreline
      for (size_t i = 0; i + 1 < b.deck.size(); i++) {
        const vec2 A(b.deck[i].x, b.deck[i].z), B(b.deck[i + 1].x, b.deck[i + 1].z), P(p.x, p.z), d = B - A;
        const float t = std::clamp(((P.x - A.x) * d.x + (P.y - A.y) * d.y) / std::max(d.x * d.x + d.y * d.y, 1e-6f), 0.f, 1.f);
        const float dd = length(A + d * t - P);
        if (dd < best) { best = dd; s = b.along[i] + (b.along[i + 1] - b.along[i]) * t; }
      }
      check(best < 0.05f, "a pier stands under the centreline");
      at.push_back(s);
    }
    at.push_back(b.along.back());
    std::sort(at.begin(), at.end());
    float gap = 0; for (size_t i = 0; i + 1 < at.size(); i++) gap = std::max(gap, at[i + 1] - at[i]);
    // (a gap is longer only in a main span, or where the deck rests near the ground and needs no pier)
    bool lowGap = true;
    for (size_t i = 0; i + 1 < at.size(); i++) if (at[i + 1] - at[i] > 60.f) {
      if (b.main != BM_NONE && at[i] >= b.main0 - 0.5f && at[i + 1] <= b.main1 + 0.5f) continue;
      const float mid = (at[i] + at[i + 1]) * 0.5f; size_t q = 0; while (q + 1 < b.along.size() && b.along[q + 1] < mid) q++;
      lowGap = lowGap && b.deck[q].h - b.depth - std::max(w.groundHeight(b.deck[q].x, b.deck[q].z), 0.f) < 3.f;
    }
    check(gap <= 60.f || lowGap, "the piers are never more than 60 m apart where the deck stands clear");
    // solid where it is drawn: on the deck, against a parapet, at a pier; nothing above it (off a main span: the deck
    // point nearest the middle outside it)
    size_t m = b.deck.size() / 2;
    for (size_t q = 0; q < b.deck.size(); q++) {
      const bool under = b.main != BM_NONE && b.along[q] > b.main0 - 2.f && b.along[q] < b.main1 + 2.f;
      if (!under && (b.main == BM_NONE || std::abs((int)q - (int)b.deck.size() / 2) < std::abs((int)m - (int)b.deck.size() / 2) ||
                     (b.along[m] > b.main0 - 2.f && b.along[m] < b.main1 + 2.f))) m = q;
      if (b.main == BM_NONE) { m = b.deck.size() / 2; break; }
    }
    const RoadPoint& c = b.deck[m];
    check(bridgeCollide(w.bridges, vec3(c.x, c.h + 0.4f, c.z), 0.5f), "the deck is solid");
    check(!bridgeCollide(w.bridges, vec3(c.x, c.h + 40.f, c.z), 0.5f), "nothing over the deck");
    const float tDown = bridgeRaycast(w.bridges, vec3(c.x, c.h + 50.f, c.z), vec3(0, -1, 0), 100.f);
    check(fabsf(tDown - 50.f) < 0.05f, "a ray down meets the road surface");
    if (fabsf(tDown - 50.f) >= 0.05f) printf("  bridge on path %d (%d..%d, %zu points, %.0f m) at %.0f %.0f: the ray met it at %.2f\n", b.path, b.first, b.last, b.deck.size(), b.length, c.x, c.z, tDown);
    check(g_scenery.collide(vec3(c.x, c.h + 0.4f, c.z), 0.5f) == kBridgeKind + 1, "the scenery's collision reports the bridge");
    int kind = -1; const float ts = g_scenery.raycast(vec3(c.x, c.h + 50.f, c.z), vec3(0, -1, 0), 100.f, &kind);
    check(ts >= 0.f && fabsf(ts - 50.f) < 0.05f && kind == 0, "the scenery's raycast stops at the bridge (no entity)");
    if (!b.piers.empty()) {
      const BridgePier& p = b.piers[b.piers.size() / 2];
      check(bridgeCollide(w.bridges, vec3(p.x, (p.top + std::max(p.bottom, 0.f)) * 0.5f, p.z), 0.5f), "a pier is solid");
    }
    // open beneath: between two piers, under the girders (a boat, a low pass)
    if (b.piers.size() >= 2) {
      const BridgePier &p0 = b.piers[0], &p1 = b.piers[1];
      const float x = (p0.x + p1.x) * 0.5f, z = (p0.z + p1.z) * 0.5f, y = std::min(p0.top, p1.top) - 2.5f, g = std::max(w.groundHeight(x, z), 0.f);
      if (y - g > 1.5f) check(!bridgeCollide(w.bridges, vec3(x, y, z), 0.5f), "open under the deck between the piers");
    }
    // a main span: its two main piers (a truss's third between), none other under it; its steel solid, the road
    // under it clear to 5 m; the truss as high as an eighth of its span or so, the arch's rise a fifth or so of its
    if (b.main != BM_NONE) {
      mains[b.main]++;
      int mainPiers = 0, under = 0;
      for (const BridgePier& p : b.piers) { mainPiers += p.main; under += !p.main && p.s > b.main0 + 1.f && p.s < b.main1 - 1.f; }
      check(mainPiers == (b.main == BM_TRUSS ? 3 : 2) && under == 0, "a main span stands on its main piers, nothing under it");
      check(b.main1 - b.main0 >= 90.f && b.main0 > b.along.front() + 25.f && b.main1 < b.along.back() - 25.f, "a main span is long, and clear of the abutments");
      check(b.main == BM_TRUSS ? b.mainRise >= 8.f && b.mainRise <= 12.f : b.mainRise >= (b.main1 - b.main0) / 6.5f && b.mainRise <= (b.main1 - b.main0) / 4.5f, "a main span's truss or arch in proportion");
      int members = 0; bool finite = true;
      forEachBridgeMember(b, [&](const BridgeMember& mb) { members++; finite = finite && std::isfinite(mb.a.x + mb.a.y + mb.a.z + mb.b.x + mb.b.y + mb.b.z); });
      check(members > 60 && finite, "a main span has its steel");
      for (float u : {0.3f, 0.5f, 0.7f}) {
        vec3 c, d, x; bridgeFrame(b, b.main0 + (b.main1 - b.main0) * u, c, d, x);
        check(!bridgeCollide(w.bridges, c + vec3(0, 5.f, 0), 1.f), "the road under a main span's steel is clear");
        const float top = b.main == BM_TRUSS ? b.mainRise : 0.4f + b.mainRise * 4.f * u * (1.f - u);
        check(bridgeCollide(w.bridges, c + x * (b.halfDeck + 0.4f) + vec3(0, top, 0), 0.3f), "a main span's truss or arch is solid");
      }
    }
    // a masonry viaduct: open under each arch at its crown, solid in the spandrel over its springing; the scenery kept
    // under the arch, not just the slab
    for (const BridgeArch& a : b.arches) {
      check(b.stone && a.rise >= 2.5f && a.rise <= 0.42f * (a.s1 - a.s0) + 1e-3f, "a viaduct's arch in proportion");
      vec3 c, d, x; bridgeFrame(b, (a.s0 + a.s1) * 0.5f, c, d, x);
      const float crown = bridgeIntrados(b, (a.s0 + a.s1) * 0.5f), g = std::max(w.groundHeight(c.x, c.z), 0.f);
      check(fabsf(crown - (c.y - b.depth - kBridgeCrown)) < 0.01f, "an arch's crown just under the slab");
      if (crown - 1.f - g > 0.6f) check(!bridgeCollide(w.bridges, vec3(c.x, crown - 1.f, c.z), 0.5f), "open under a viaduct's arch");
      const float s1 = a.s0 + (a.s1 - a.s0) * 0.15f;
      vec3 c1, d1, x1; bridgeFrame(b, s1, c1, d1, x1);
      const float i1 = bridgeIntrados(b, s1);
      check(bridgeCollide(w.bridges, vec3(c1.x, (i1 + c1.y - b.depth) * 0.5f + 0.3f, c1.z), 0.2f), "a viaduct's spandrel is solid");
      float under = 0.f; check(bridgeOver(w.bridges, c1.x, c1.z, 0.f, &under) && under <= i1 + 0.01f, "the scenery is kept under the arch");
    }
    // over dry ground it crosses something: most of its points not on a slope running across it - or it stands high
    // above the slope, a viaduct along it where a fill would have been more than 15 m high
    if (!b.water) {
      int slope = 0;
      for (size_t i = 0; i < b.deck.size(); i++) {
        if (b.deck[i].h - w.naturalHeight(b.deck[i].x, b.deck[i].z) > 15.f) continue;
        const size_t a = i > 0 ? i - 1 : i, e = std::min(i + 1, b.deck.size() - 1);
        const float dx = b.deck[e].x - b.deck[a].x, dz = b.deck[e].z - b.deck[a].z, l = std::max(hypotf(dx, dz), 1e-3f);
        const float off = roadSpec(b.cls).halfPlatform + 20.f, px = -dz / l * off, pz = dx / l * off;
        slope += fabsf(w.naturalHeight(b.deck[i].x + px, b.deck[i].z + pz) - w.naturalHeight(b.deck[i].x - px, b.deck[i].z - pz)) > 0.66f * off;
      }
      check(slope * 2 < (int)b.deck.size(), "a bridge over dry ground crosses something, not along a hillside");
    }
  }
  int stoneArches = 0; for (const Bridge& b : w.bridges) stoneArches += (int)b.arches.size();
  printf("main spans: %d truss, %d tied arch; %d masonry arches\n", mains[BM_TRUSS], mains[BM_ARCH], stoneArches);
  check(mains[BM_TRUSS] >= 1 && mains[BM_ARCH] >= 1 && stoneArches >= 5, "the long crossings have their main spans, the mountain viaducts their arches");
  // the lanes and tracks hug the ground off their bridges
  float worst = 0;
  for (const RoadPath& p : w.roads.paths) {
    if (p.cls < RC_LANE) continue;
    for (size_t k = 0; k < p.pts.size(); k++) {
      const bool br = (k > 0 && p.bridge[k - 1]) || (k + 1 < p.pts.size() && p.bridge[k]);
      const float g = w.naturalHeight(p.pts[k].x, p.pts[k].z);
      if (!br && g >= 0.5f && p.pts[k].h - g > worst) { worst = p.pts[k].h - g; if (worst > 18.f) printf("  %s %d->%d point %zu at %.0f %.0f: %.1f m over the ground\n", p.cls == RC_LANE ? "lane" : "track", p.from, p.to, k, p.pts[k].x, p.pts[k].z, worst); }
    }
  }
  // (more than 15 m clear along a hillside for 45 m or more is a viaduct: a fill's crest between two short of it, no higher)
  check(worst < 18.f, "no lane or track stands more than 18 m over the ground off a bridge");
  // the meshes: one per bridge, whole, the road surface at the road's height
  std::vector<EVert> verts; std::vector<BridgeMeshRange> ranges;
  buildBridgeMeshes(w.bridges, verts, ranges);
  check(ranges.size() == w.bridges.size(), "a mesh per bridge");
  bool whole = true, unit = true, onRoad = true; int deckVerts = 0;
  for (size_t bi = 0; bi < ranges.size(); bi++) {
    const Bridge& b = w.bridges[bi];
    whole = whole && ranges[bi].count > 0 && ranges[bi].count % 3 == 0;
    for (int i = ranges[bi].first; i < ranges[bi].first + ranges[bi].count; i++) {
      const EVert& v = verts[i];
      whole = whole && std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz) && std::isfinite(v.u) && std::isfinite(v.v);
      unit = unit && fabsf(sqrtf(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz) - 1.f) < 1e-3f;
      if (v.part >= P_DECK - 0.5f) {   // (a deck vertex: v is its distance along the road; its height that point's)
        deckVerts++;
        size_t q = 0; while (q + 1 < b.along.size() && fabsf(b.along[q] - v.v) > 1e-3f) q++;
        onRoad = onRoad && fabsf(b.along[q] - v.v) <= 1e-3f && fabsf(v.py - b.deck[q].h) < 1e-3f && (int)(v.part + 0.5f) == P_DECK + b.cls;
      }
    }
  }
  check(whole, "the meshes are whole (finite, in triangles)");
  check(unit, "their normals are unit length");
  check(onRoad && deckVerts > 0, "the deck's road surface is at the road's height, painted for its class");
  printf("bridges: %zu (%d over water), %d piers, %zu mesh vertices; %d checks, %d failures\n", w.bridges.size(), water, piers, verts.size(), checks, fails);
  return fails ? 1 : 0;
}
