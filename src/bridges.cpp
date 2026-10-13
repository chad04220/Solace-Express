// Somnium Volandi - the road network's bridges: their structure (bridges.h)
#include "bridges.h"
#include "world.h"
#include <algorithm>
#include <cmath>

namespace {

// The deck as wide as the road's platform, so its edges run on from the graded bed's: beyond the paving, where the
// ground has its verge, a kerb and walkway, the parapet at the edge
float deckHalf(int cls) { return roadSpec(cls).halfPlatform; }

// An oriented box: centre, unit axes and half sizes along them
struct OBox { vec3 c, ax, ay, az; float hx, hy, hz; };

bool sphereHits(const OBox& b, vec3 p, float r) {
  const vec3 d = p - b.c;
  const float x = std::clamp(dot(d, b.ax), -b.hx, b.hx), y = std::clamp(dot(d, b.ay), -b.hy, b.hy), z = std::clamp(dot(d, b.az), -b.hz, b.hz);
  const vec3 q = b.c + b.ax * x + b.ay * y + b.az * z;
  return length(p - q) <= r;
}

// the segment a + d t (t in [0, L]) against the box: the entry distance, or -1
float segmentHits(const OBox& b, vec3 a, vec3 d, float L) {
  const vec3 o = a - b.c;
  float tn = 0.f, tf = L;
  const vec3 axes[3] = {b.ax, b.ay, b.az}; const float h[3] = {b.hx, b.hy, b.hz};
  for (int k = 0; k < 3; k++) {
    const float oo = dot(o, axes[k]), dd = dot(d, axes[k]);
    if (fabsf(dd) < 1e-8f) { if (fabsf(oo) > h[k]) return -1.f; continue; }
    float t1 = (-h[k] - oo) / dd, t2 = (h[k] - oo) / dd;
    if (t1 > t2) std::swap(t1, t2);
    tn = std::max(tn, t1); tf = std::min(tf, t2);
    if (tn > tf) return -1.f;
  }
  return tn;
}

// The solid parts of a bridge, deck segment by segment (its slab, its two parapets) and pier by pier
template <class F> void forEachBox(const Bridge& b, F&& f) {
  const float par = bridgeParapet(b.cls);
  const size_t n = b.deck.size();
  auto level = [&](size_t i) { const float dx = b.deck[i + 1].x - b.deck[i].x, dz = b.deck[i + 1].z - b.deck[i].z, l = std::max(hypotf(dx, dz), 1e-6f); return vec2(dx / l, dz / l); };
  for (size_t i = 0; i + 1 < n; i++) {
    const RoadPoint &A = b.deck[i], &B = b.deck[i + 1];
    vec3 a(A.x, A.h, A.z), c(B.x, B.h, B.z);
    vec3 along = c - a; const float len = length(along);
    if (len < 1e-3f) continue;
    along = along / len;
    vec3 across = normalize(vec3(-along.z, 0.f, along.x));
    vec3 up = cross(across, along);
    if (up.y < 0) up = up * -1.f;
    const vec3 mid = (a + c) * 0.5f;
    // (each box overlapping its neighbours by the mitre the bend between them leaves open at the outer edge - and a
    // few centimetres more, so the joint on the centreline is inside both rather than on two faces' edge)
    const vec2 d = level(i);
    float bend = 0.f;
    for (size_t j : {i - 1, i + 1}) if (j < n - 1) { const vec2 e = level(j); bend = std::max(bend, fabsf(d.x * e.y - d.y * e.x)); }
    const float hz = len * 0.5f + b.halfDeck * bend + 0.05f;
    f(OBox{mid - up * (b.depth * 0.5f), across, up, along, b.halfDeck, b.depth * 0.5f, hz});   // the slab and girders
    for (float s : {-1.f, 1.f})   // the parapets, standing on its edges
      f(OBox{mid + across * (s * (b.halfDeck - 0.2f)) + up * (par * 0.5f), across, up, along, 0.2f, par * 0.5f, hz});
  }
  for (const BridgePier& p : b.piers) {
    const vec3 ux(p.ux, 0.f, p.uz), vx(-p.uz, 0.f, p.ux);
    f(OBox{vec3(p.x, (p.top + p.bottom) * 0.5f, p.z), vx, vec3(0.f, 1.f, 0.f), ux, p.halfWidth, (p.top - p.bottom) * 0.5f, p.halfThick});
  }
  // a masonry viaduct's spandrels: slices of 2 m or so, each an upright box from the higher end of its arch's
  // underside up to the slab, and under it a box along the chord of the arch's underside (the intrados), as deep as
  // the drop between its ends - so the solid follows the arch to within the chord's sag (centimetres)
  for (const BridgeArch& a : b.arches) {
    const int n = std::max(4, (int)ceilf((a.s1 - a.s0) / 2.f));
    for (int i = 0; i < n; i++) {
      const float s0 = a.s0 + (a.s1 - a.s0) * i / n, s1 = a.s0 + (a.s1 - a.s0) * (i + 1) / n, sm = (s0 + s1) * 0.5f;
      vec3 c, d, x, c0, d0, x0, c1, d1, x1; bridgeFrame(b, sm, c, d, x); bridgeFrame(b, s0, c0, d0, x0); bridgeFrame(b, s1, c1, d1, x1);
      const float i0 = bridgeIntrados(b, s0 + 1e-3f), i1 = bridgeIntrados(b, s1 - 1e-3f), top = c.y - b.depth, hi = std::max(i0, i1);
      if (top - hi > 0.05f)
        f(OBox{vec3(c.x, (top + hi) * 0.5f, c.z), x, vec3(0.f, 1.f, 0.f), d, b.halfDeck, (top - hi) * 0.5f, (s1 - s0) * 0.5f + 0.05f});
      const vec3 p0(c0.x, i0, c0.z), p1(c1.x, i1, c1.z), u = p1 - p0; const float l = length(u);
      if (l < 1e-3f) continue;
      const vec3 az = u / l, ay = normalize(cross(az, x)) * (cross(az, x).y < 0.f ? -1.f : 1.f);
      const float h = fabsf(i1 - i0) * sqrtf(std::max(1.f - az.y * az.y, 0.f)) + 0.1f;   // (perpendicular to the chord)
      f(OBox{(p0 + p1) * 0.5f + ay * (h * 0.5f), x, ay, az, b.halfDeck, h * 0.5f, l * 0.5f});
    }
  }
  // a main span's members
  forEachBridgeMember(b, [&](const BridgeMember& m) {
    const vec3 u = m.b - m.a; const float l = length(u);
    if (l < 1e-3f) return;
    const vec3 z = u / l, x0 = cross(vec3(0.f, 1.f, 0.f), z);
    const vec3 x = length(x0) > 1e-3f ? normalize(x0) : vec3(1.f, 0.f, 0.f), y = cross(z, x);
    f(OBox{(m.a + m.b) * 0.5f, x, y, z, m.hw, m.hh, l * 0.5f});
  });
}

}  // namespace

void bridgeFrame(const Bridge& b, float s, vec3& centre, vec3& dir, vec3& across) {
  const size_t n = b.deck.size();
  size_t i = 0; while (i + 2 < n && b.along[i + 1] < s) i++;
  const RoadPoint &A = b.deck[i], &B = b.deck[std::min(i + 1, n - 1)];
  const float l = std::max(b.along[std::min(i + 1, n - 1)] - b.along[i], 1e-3f), t = std::clamp((s - b.along[i]) / l, 0.f, 1.f);
  centre = vec3(A.x + (B.x - A.x) * t, A.h + (B.h - A.h) * t, A.z + (B.z - A.z) * t);
  const float dx = B.x - A.x, dz = B.z - A.z, dl = std::max(hypotf(dx, dz), 1e-6f);
  dir = vec3(dx / dl, 0.f, dz / dl); across = vec3(-dir.z, 0.f, dir.x);
}

float bridgeIntrados(const Bridge& b, float s) {
  for (const BridgeArch& a : b.arches) {
    if (s < a.s0 || s > a.s1) continue;
    vec3 c, d, x; bridgeFrame(b, s, c, d, x);
    const float u = (s - a.s0) / std::max(a.s1 - a.s0, 1e-3f);
    return c.y - b.depth - kBridgeCrown - a.rise * (2.f * u - 1.f) * (2.f * u - 1.f);
  }
  return 1e9f;
}

std::vector<Bridge> buildBridges(const World& world) {
  generationCheckpoint();
  std::vector<Bridge> out;
  const auto& paths = world.roads.paths;
  for (size_t pi = 0; pi < paths.size(); pi++) {
    generationCheckpoint();
    const RoadPath& P = paths[pi];
    const int n = (int)P.pts.size();
    if (n < 2 || (int)P.bridge.size() < n - 1) continue;
    std::vector<float> along(n, 0.f);
    for (int k = 1; k < n; k++) along[k] = along[k - 1] + hypotf(P.pts[k].x - P.pts[k - 1].x, P.pts[k].z - P.pts[k - 1].z);
    for (int k = 0; k + 1 < n;) {
      if (!P.bridge[k]) { k++; continue; }
      int e = k; while (e + 1 < n && P.bridge[e]) e++;
      Bridge b; b.path = (int)pi; b.first = k; b.last = e; b.cls = P.cls;
      for (int q = k; q <= e; q++) {
        b.deck.push_back(P.pts[q]); b.along.push_back(along[q]);
        b.water = b.water || world.groundHeight(P.pts[q].x, P.pts[q].z, 8) < 0.5f;
      }
      b.length = along[e] - along[k];
      b.halfDeck = deckHalf(b.cls);
      if (k > 0) { const float dx = P.pts[k].x - P.pts[k - 1].x, dz = P.pts[k].z - P.pts[k - 1].z, l = std::max(hypotf(dx, dz), 1e-3f); b.inX = dx / l; b.inZ = dz / l; }
      if (e + 1 < n) { const float dx = P.pts[e + 1].x - P.pts[e].x, dz = P.pts[e + 1].z - P.pts[e].z, l = std::max(hypotf(dx, dz), 1e-3f); b.outX = dx / l; b.outZ = dz / l; }
      // how high it stands (the deck's underside over the ground or the water): the piers further apart the higher
      // it stands, and the girders that much deeper
      float clear = 0;
      for (const RoadPoint& p : b.deck) clear = std::max(clear, p.h - std::max(world.groundHeight(p.x, p.z, 8), 0.f));
      const float spacing = b.water ? (b.cls == RC_HIGHWAY ? 50.f : 42.f) : std::clamp(26.f + 0.9f * clear, 26.f, 55.f);
      b.depth = 0.35f + spacing / (b.cls == RC_HIGHWAY ? 17.f : b.cls == RC_ROAD ? 19.f : 23.f);
      // a mountain lane's viaduct: masonry (its arches below, once its piers stand), the slab and the crown of each
      // arch over it 1.2 m deep
      b.stone = !b.water && b.cls >= RC_LANE && clear >= 6.f;
      if (b.stone) b.depth = 1.2f;
      // a long crossing of the water: its main span over the deepest stretch of water straight enough for it (12 degrees
      // of bend at the most), 30 m or more from the abutments - a highway's a truss of two spans, a road's a tied arch,
      // and a highway's of 400 m and more cable-stayed, its main span half of it (200 to 300 m), its pylons a fifth or so
      // of that over the road
      auto groundAt = [&](float s) { vec3 c, d, x; bridgeFrame(b, s, c, d, x); return world.groundHeight(c.x, c.z, 8); };
      if (b.water && b.length >= 200.f && b.cls <= RC_ROAD) {
        const bool stayed = b.cls == RC_HIGHWAY && b.length >= 400.f, truss = b.cls == RC_HIGHWAY && !stayed;
        const float W = stayed ? std::clamp(0.5f * b.length, 200.f, 300.f) : truss ? 2.f * std::clamp(0.16f * b.length, 60.f, 85.f) : std::clamp(0.4f * b.length, 90.f, 125.f);
        float best = -1e9f, at = 0.f;
        for (float sc = along[k] + 30.f + W * 0.5f; sc <= along[e] - 30.f - W * 0.5f; sc += 5.f) {
          generationCheckpoint();
          const float a0 = sc - W * 0.5f, a1 = sc + W * 0.5f;
          if (groundAt(a0) > 0.f || groundAt(a1) > 0.f || groundAt(sc) > -0.5f) continue;
          float turn = 0.f;
          for (int q = k + 1; q < e; q++) {
            if (along[q] < a0 || along[q] > a1) continue;
            const float ax = P.pts[q].x - P.pts[q - 1].x, az = P.pts[q].z - P.pts[q - 1].z, bx = P.pts[q + 1].x - P.pts[q].x, bz = P.pts[q + 1].z - P.pts[q].z;
            turn += fabsf(atan2f(ax * bz - az * bx, ax * bx + az * bz));
          }
          if (turn > 12.f * 0.0174533f) continue;
          const float score = -groundAt(sc) - 0.01f * fabsf(sc - (along[k] + along[e]) * 0.5f);   // (deepest; then the middle)
          if (score > best) { best = score; at = sc; }
        }
        if (best > -1e8f) {
          b.main = stayed ? BM_STAYED : truss ? BM_TRUSS : BM_ARCH; b.main0 = at - W * 0.5f; b.main1 = at + W * 0.5f;
          b.mainRise = stayed ? std::clamp(0.22f * W, 45.f, 70.f) : truss ? std::clamp(W * 0.5f / 7.f, 8.f, 12.f) : W / 5.5f;
        }
      }
      // the piers: evenly between the supports - the abutments, and a main span's piers - none where the deck rests on
      // the ground anyway
      auto pierAt = [&](float s, bool mainPier) {
        int q = k; while (q + 1 < e && along[q + 1] < s) q++;
        const RoadPoint &A = P.pts[q], &B = P.pts[q + 1];
        const float seg = std::max(along[q + 1] - along[q], 1e-3f), t = std::clamp((s - along[q]) / seg, 0.f, 1.f);
        BridgePier pr;
        pr.x = A.x + (B.x - A.x) * t; pr.z = A.z + (B.z - A.z) * t; pr.s = s; pr.main = mainPier;
        const float h = A.h + (B.h - A.h) * t;
        pr.ux = (B.x - A.x) / seg; pr.uz = (B.z - A.z) / seg;
        pr.top = h - b.depth;
        const float g = world.groundHeight(pr.x, pr.z, 8);
        if (pr.top - std::max(g, 0.f) < 1.5f && g >= 0.5f) return;   // (the deck nearly on the ground: it needs none)
        pr.bottom = g - (g < 0.5f ? 2.f : 1.f);                       // (the footing: into the sea floor, or the ground)
        const float tall = pr.top - pr.bottom;
        pr.halfThick = std::clamp(0.7f + 0.012f * tall, 0.7f, 2.0f) * (mainPier ? 1.8f : 1.f);
        pr.halfWidth = b.stone ? b.halfDeck : b.cls == RC_HIGHWAY ? b.halfDeck * 0.62f : b.cls == RC_ROAD ? b.halfDeck * 0.5f : 1.1f;
        if (mainPier && b.main == BM_STAYED) pr.halfWidth = b.halfDeck + 4.5f;   // (a pylon's: its cap under both legs)
        if (b.stone) pr.halfThick = std::clamp(0.045f * spacing, 1.2f, 2.2f);
        b.piers.push_back(pr);
      };
      std::vector<std::pair<float, float>> runs;
      if (b.main == BM_NONE) runs.push_back({along[k], along[e]});
      else {
        runs.push_back({along[k], b.main0}); runs.push_back({b.main1, along[e]});
        pierAt(b.main0, true);
        if (b.main == BM_TRUSS) pierAt((b.main0 + b.main1) * 0.5f, true);
        pierAt(b.main1, true);
      }
      for (const auto& r : runs) {
        const int count = std::max(0, (int)ceilf((r.second - r.first) / spacing) - 1);
        for (int i = 1; i <= count; i++) pierAt(r.first + (r.second - r.first) * (float)i / (count + 1), false);
      }
      std::sort(b.piers.begin(), b.piers.end(), [](const BridgePier& p, const BridgePier& q) { return p.s < q.s; });
      // a masonry viaduct's arches: between each pair of supports, rising as high as the lower support's ground allows
      // (0.42 of the span at the most); none lower than 2.5 m (the slab carried on over it)
      if (b.stone) {
        std::vector<float> sup = {along[k]};
        for (const BridgePier& p : b.piers) sup.push_back(p.s);
        sup.push_back(along[e]);
        for (size_t i = 0; i + 1 < sup.size(); i++) {
          const float s0 = sup[i], s1 = sup[i + 1];
          float room = 1e9f;
          for (float s : {s0, s1}) {
            vec3 c, d, x; bridgeFrame(b, s, c, d, x);
            room = std::min(room, c.y - b.depth - kBridgeCrown - std::max(world.groundHeight(c.x, c.z, 8), 0.f) - 0.8f);
          }
          const float rise = std::min(0.42f * (s1 - s0), room);
          if (rise >= 2.5f) b.arches.push_back({s0, s1, rise});
        }
      }
      // the bounds
      b.minX = b.minZ = b.minY = 1e9f; b.maxX = b.maxZ = b.maxY = -1e9f;
      forEachBox(b, [&](const OBox& o) {
        for (int c = 0; c < 8; c++) {
          const vec3 v = o.c + o.ax * ((c & 1) ? o.hx : -o.hx) + o.ay * ((c & 2) ? o.hy : -o.hy) + o.az * ((c & 4) ? o.hz : -o.hz);
          b.minX = std::min(b.minX, v.x); b.maxX = std::max(b.maxX, v.x); b.minY = std::min(b.minY, v.y); b.maxY = std::max(b.maxY, v.y);
          b.minZ = std::min(b.minZ, v.z); b.maxZ = std::max(b.maxZ, v.z);
        }
      });
      out.push_back(std::move(b));
      k = e;
    }
  }
  return out;
}

bool bridgeOver(const std::vector<Bridge>& bridges, float x, float z, float r, float* underside) {
  bool any = false; float u = 1e9f;
  for (const Bridge& b : bridges) {
    if (x < b.minX - r || x > b.maxX + r || z < b.minZ - r || z > b.maxZ + r) continue;
    for (size_t i = 0; i + 1 < b.deck.size(); i++) {
      const RoadPoint &A = b.deck[i], &B = b.deck[i + 1];
      const float dx = B.x - A.x, dz = B.z - A.z, l2 = std::max(dx * dx + dz * dz, 1e-6f);
      const float t = std::clamp(((x - A.x) * dx + (z - A.z) * dz) / l2, 0.f, 1.f);
      if (hypotf(A.x + dx * t - x, A.z + dz * t - z) <= b.halfDeck + r) {
        any = true;
        // (under a masonry arch, its underside; near a support the lower of the arches either side, within r)
        const float s = b.along[i] + (b.along[i + 1] - b.along[i]) * t;
        float under = A.h + (B.h - A.h) * t - b.depth;
        for (float ds : {-r, 0.f, r}) under = std::min(under, bridgeIntrados(b, s + ds));
        u = std::min(u, under);
      }
    }
  }
  if (underside) *underside = u;
  return any;
}

bool bridgeCollide(const std::vector<Bridge>& bridges, vec3 p, float r) {
  for (const Bridge& b : bridges) {
    if (p.x < b.minX - r || p.x > b.maxX + r || p.y < b.minY - r || p.y > b.maxY + r || p.z < b.minZ - r || p.z > b.maxZ + r) continue;
    bool hit = false;
    forEachBox(b, [&](const OBox& o) { hit = hit || sphereHits(o, p, r); });
    if (hit) return true;
  }
  return false;
}

float bridgeRaycast(const std::vector<Bridge>& bridges, vec3 a, vec3 d, float L) {
  const vec3 e = a + d * L;
  float best = -1.f;
  for (const Bridge& b : bridges) {
    if (std::max(a.x, e.x) < b.minX || std::min(a.x, e.x) > b.maxX || std::max(a.y, e.y) < b.minY || std::min(a.y, e.y) > b.maxY ||
        std::max(a.z, e.z) < b.minZ || std::min(a.z, e.z) > b.maxZ) continue;
    forEachBox(b, [&](const OBox& o) {
      const float t = segmentHits(o, a, d, L);
      if (t >= 0.f && (best < 0.f || t < best)) best = t;
    });
  }
  return best;
}
