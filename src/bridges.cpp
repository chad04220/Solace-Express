// Solace Express - the road network's bridges: their structure (bridges.h)
#include "bridges.h"
#include "world.h"
#include <algorithm>
#include <cmath>

namespace {

// The deck's edge beyond the paving: a kerb (and on the highways and roads a narrow walkway), then the parapet
float deckHalf(int cls) { return roadSpec(cls).halfPaved + (cls <= RC_ROAD ? 0.9f : 0.45f) + 0.35f; }

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
  for (size_t i = 0; i + 1 < b.deck.size(); i++) {
    const RoadPoint &A = b.deck[i], &B = b.deck[i + 1];
    vec3 a(A.x, A.h, A.z), c(B.x, B.h, B.z);
    vec3 along = c - a; const float len = length(along);
    if (len < 1e-3f) continue;
    along = along / len;
    vec3 across = normalize(vec3(-along.z, 0.f, along.x));
    vec3 up = cross(across, along);
    if (up.y < 0) up = up * -1.f;
    const vec3 mid = (a + c) * 0.5f;
    f(OBox{mid - up * (b.depth * 0.5f), across, up, along, b.halfDeck, b.depth * 0.5f, len * 0.5f});   // the slab and girders
    for (float s : {-1.f, 1.f})   // the parapets, standing on its edges
      f(OBox{mid + across * (s * (b.halfDeck - 0.2f)) + up * (par * 0.5f), across, up, along, 0.2f, par * 0.5f, len * 0.5f});
  }
  for (const BridgePier& p : b.piers) {
    const vec3 ux(p.ux, 0.f, p.uz), vx(-p.uz, 0.f, p.ux);
    f(OBox{vec3(p.x, (p.top + p.bottom) * 0.5f, p.z), vx, vec3(0.f, 1.f, 0.f), ux, p.halfWidth, (p.top - p.bottom) * 0.5f, p.halfThick});
  }
}

}  // namespace

std::vector<Bridge> buildBridges(const World& world) {
  std::vector<Bridge> out;
  const auto& paths = world.roads.paths;
  for (size_t pi = 0; pi < paths.size(); pi++) {
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
      // how high it stands (the deck's underside over the ground or the water): the piers further apart the higher
      // it stands, and the girders that much deeper
      float clear = 0;
      for (const RoadPoint& p : b.deck) clear = std::max(clear, p.h - std::max(world.groundHeight(p.x, p.z, 8), 0.f));
      const float spacing = b.water ? (b.cls == RC_HIGHWAY ? 50.f : 42.f) : std::clamp(26.f + 0.9f * clear, 26.f, 55.f);
      b.depth = 0.35f + spacing / (b.cls == RC_HIGHWAY ? 17.f : b.cls == RC_ROAD ? 19.f : 23.f);
      // the piers: evenly between the abutments, none where the deck rests on the ground anyway
      const int count = std::max(0, (int)ceilf(b.length / spacing) - 1);
      for (int i = 1; i <= count; i++) {
        const float s = along[k] + b.length * (float)i / (count + 1);
        int q = k; while (q + 1 < e && along[q + 1] < s) q++;
        const RoadPoint &A = P.pts[q], &B = P.pts[q + 1];
        const float seg = std::max(along[q + 1] - along[q], 1e-3f), t = std::clamp((s - along[q]) / seg, 0.f, 1.f);
        BridgePier pr;
        pr.x = A.x + (B.x - A.x) * t; pr.z = A.z + (B.z - A.z) * t;
        const float h = A.h + (B.h - A.h) * t;
        pr.ux = (B.x - A.x) / seg; pr.uz = (B.z - A.z) / seg;
        pr.top = h - b.depth;
        const float g = world.groundHeight(pr.x, pr.z, 8);
        if (pr.top - std::max(g, 0.f) < 1.5f && g >= 0.5f) continue;   // (the deck nearly on the ground: it needs none)
        pr.bottom = g - (g < 0.5f ? 2.f : 1.f);                       // (the footing: into the sea floor, or the ground)
        const float tall = pr.top - pr.bottom;
        pr.halfThick = std::clamp(0.7f + 0.012f * tall, 0.7f, 2.0f);
        pr.halfWidth = b.cls == RC_HIGHWAY ? b.halfDeck * 0.62f : b.cls == RC_ROAD ? b.halfDeck * 0.5f : 1.1f;
        b.piers.push_back(pr);
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
      if (hypotf(A.x + dx * t - x, A.z + dz * t - z) <= b.halfDeck + r) { any = true; u = std::min(u, A.h + (B.h - A.h) * t - b.depth); }
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
