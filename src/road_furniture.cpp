// Solace Express - the road furniture (docs/LIVING_ISLANDS_PLAN.md, Phase A item 6): laid along the network once the
// world is built or loaded, then handed to each scenery chunk as it is generated (entities.cpp).
#include "scenery.h"
#include "entities.h"
#include "settlements.h"
#include "world.h"
#include <algorithm>
#include <cmath>

float airportInfluence(float x, float z);   // world.cpp

namespace {
std::vector<RoadFurniture> g_items;
std::vector<int> g_chunkOf;   // (each item's chunk, ascending: the items sorted by it)

// A path's centre line: where it is s metres along, which way it runs there, the road's height, on a bridge or not
struct Station { vec2 p, d; float h; bool bridge; };
struct Line {
  const RoadPath& P;
  std::vector<float> arc;
  explicit Line(const RoadPath& path) : P(path), arc(path.pts.size(), 0.f) {
    for (size_t k = 1; k < P.pts.size(); k++) arc[k] = arc[k - 1] + hypotf(P.pts[k].x - P.pts[k - 1].x, P.pts[k].z - P.pts[k - 1].z);
  }
  float length() const { return arc.back(); }
  Station at(float s) const {
    s = std::clamp(s, 0.f, length());
    size_t k = 1; while (k + 1 < P.pts.size() && arc[k] < s) k++;
    const RoadPoint &a = P.pts[k - 1], &b = P.pts[k];
    const float l = std::max(arc[k] - arc[k - 1], 1e-3f), t = (s - arc[k - 1]) / l;
    return {vec2(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t), vec2(b.x - a.x, b.z - a.z) * (1.f / l), a.h + (b.h - a.h) * t, P.bridge[k - 1] != 0};
  }
  // whether a bridge span lies within r of s along it
  bool nearBridge(float s, float r) const {
    for (size_t k = 0; k + 1 < P.pts.size(); k++) if (P.bridge[k] && arc[k + 1] > s - r && arc[k] < s + r) return true;
    return false;
  }
  // how far along it the point nearest q lies
  float arcOf(vec2 q) const {
    float best = 1e9f, at = 0.f;
    for (size_t k = 0; k + 1 < P.pts.size(); k++) {
      const vec2 a(P.pts[k].x, P.pts[k].z), d = vec2(P.pts[k + 1].x, P.pts[k + 1].z) - a;
      const float t = std::clamp(dot2(q - a, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f), e = length2(a + d * t - q);
      if (e < best) { best = e; at = arc[k] + (arc[k + 1] - arc[k]) * t; }
    }
    return at;
  }
  static float length2(vec2 v) { return v.x * v.x + v.y * v.y; }
};
inline vec2 rightOf(vec2 d) { return vec2(-d.y, d.x); }   // (the right of the traffic going along d: they keep to it)
inline float crossz(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
inline float yawFacing(vec2 front) { return atan2f(front.x, front.y); }   // (an entity's front, its local +z, along front)
}  // namespace

void sceneryBakeRoadFurniture(const World& world) {
  std::vector<RoadFurniture> out;
  const RoadNetwork& net = world.roads;
  RoadIndex roads;
  for (size_t pi = 0; pi < net.paths.size(); pi++) roads.addPath(net.paths[pi], (int)pi);
  auto ground = [&](vec2 p) { return world.groundHeight(p.x, p.y, 7); };
  auto put = [&](int kind, vec2 p, float y, float yaw, float sx, float sy, float sz, float seed) {
    out.push_back({kind, {p.x, y, p.y, yaw, sx, sy, sz, seed}});
  };
  // off every platform but its own path's by `clear` metres (a junction's mouth left open), on dry land, off the airfields
  auto clearOf = [&](vec2 p, int pi, float clear) {
    return ground(p) > 1.f && airportInfluence(p.x, p.y) <= 0.001f && roads.nearest(p, clear + 1.f, pi, nullptr, nullptr, true) >= clear;
  };
  // a guardrail from a to b (heights ha, hb) facing `front`: 16 m lengths stretched to fit, their beam on the grade
  auto rail = [&](vec2 a, float ha, vec2 b, float hb, vec2 front) {
    if (dot2(front, rightOf(b - a)) < 0.f) { std::swap(a, b); std::swap(ha, hb); }   // (local +x from a to b, +z to the front)
    const vec2 u = b - a; const float len = length(u);
    if (len < 4.f) return;
    const vec2 dir = u * (1.f / len);
    put(EK_GUARDRAIL, (a + b) * 0.5f, (ha + hb) * 0.5f, atan2f(-dir.y, dir.x), len / 16.f, 1.f, 1.f, 0.5f + std::clamp((hb - ha) / len, -0.3f, 0.3f));
  };

  for (size_t pi = 0; pi < net.paths.size(); pi++) {
    const RoadPath& P = net.paths[pi];
    if (P.pts.size() < 2) continue;
    const Line L(P);
    const RoadSpec& spec = roadSpec(P.cls);
    const int ipi = (int)pi;
    auto share = [&](vec2 p) { return settlementShare(world.settlements, p.x, p.y); };

    // ---- street lamps. On a street's pavements every 30 m, side to side, where the terrain shader pools their light
    // (terrain_material.glsl: on the right as it runs at 15 m and every 30 m on, on the left at 0 and every 30 m)
    if (P.cls == RC_STREET)
      for (int j = 0; j * 15.f <= L.length(); j++) {
        const Station st = L.at(j * 15.f);
        if (st.bridge) continue;
        const vec2 out = rightOf(st.d) * ((j & 1) ? 1.f : -1.f), p = st.p + out * (spec.halfPaved + 1.f);
        if (!clearOf(p, ipi, 1.f)) continue;
        put(EK_STREETLAMP, p, ground(p), yawFacing(out * -1.f), 1.f, 0.85f, 1.f, hash2i((int)p.x, (int)p.y));
      }
    // on a highway's verges through and near the towns and cities, both sides every 45 m; along a road or a lane
    // through a settlement, one side every 35 m
    if (P.cls == RC_HIGHWAY || P.cls == RC_ROAD || P.cls == RC_LANE) {
      const float step = P.cls == RC_HIGHWAY ? 45.f : 35.f, side = hash2i(ipi, 17) < 0.5f ? 1.f : -1.f;
      for (float s = step * 0.5f; s <= L.length(); s += step) {
        const Station st = L.at(s);
        if (st.bridge || share(st.p) > (P.cls == RC_HIGHWAY ? 1.3f : 0.95f)) continue;
        for (float sd : {-1.f, 1.f}) {
          if (P.cls != RC_HIGHWAY && sd != side) continue;
          const vec2 out = rightOf(st.d) * sd, p = st.p + out * (spec.halfPaved + 1.2f);
          if (!clearOf(p, ipi, 1.f)) continue;
          put(EK_STREETLAMP, p, ground(p), yawFacing(out * -1.f), 1.f, P.cls == RC_HIGHWAY ? 1.2f : 0.9f, 1.f, hash2i((int)p.y, (int)p.x));
        }
      }
    }

    // ---- guardrails. Down a highway's central reserve; along a road's edge where it stands 2.5 m or more over the
    // ground beside it, and for 30 m onto each bridge (whose parapets carry on from them)
    if (P.cls == RC_HIGHWAY || P.cls == RC_ROAD || P.cls == RC_LANE) {
      const float seg = 16.f;
      std::vector<std::pair<float, float>> runs;   // (the stretches between its bridges: the rails stop at a deck's joint)
      float from = 0.f;
      for (size_t k = 0; k + 1 < P.pts.size(); k++)
        if (P.bridge[k]) { if (L.arc[k] > from) runs.push_back({from, L.arc[k]}); from = L.arc[k + 1]; }
      if (L.length() > from) runs.push_back({from, L.length()});
      for (const auto& run : runs)
      for (float s = run.first; s + 4.f <= run.second; s += seg) {
        const float e = std::min(s + seg, run.second);
        const Station a = L.at(s), b = L.at(e), m = L.at((s + e) * 0.5f);
        bool held = false;   // (by an airfield the road lies on its ground as it is: no furniture)
        for (const Station& q : {a, m, b}) held |= airportInfluence(q.p.x, q.p.y) > 0.001f;
        if (held) continue;
        bool junction = false;   // (the reserve open where another road joins)
        for (const Station& q : {a, m, b}) junction |= roads.nearest(q.p, spec.halfPlatform + 3.f, ipi, nullptr, nullptr, true) <= 3.f;
        if (P.cls == RC_HIGHWAY && !junction) rail(a.p, a.h, b.p, b.h, rightOf(m.d));
        const bool approach = L.nearBridge((s + e) * 0.5f, 30.f + seg * 0.5f);
        for (float sd : {-1.f, 1.f}) {
          const vec2 n = rightOf(m.d) * sd;
          const float fill = m.h - world.naturalHeight(m.p.x + n.x * (spec.halfPlatform + 4.f), m.p.y + n.y * (spec.halfPlatform + 4.f));
          if (!approach && fill < 2.5f) continue;
          const float x = spec.halfPaved + 0.6f;
          const vec2 pa = a.p + rightOf(a.d) * (sd * x), pb = b.p + rightOf(b.d) * (sd * x);
          bool mouth = false;   // (a junction's mouth left open)
          for (vec2 q : {pa, (pa + pb) * 0.5f, pb}) mouth |= roads.nearest(q, 3.f, ipi, nullptr, nullptr, true) < 2.f;
          if (mouth) continue;
          rail(pa, a.h, pb, b.h, n * -1.f);
        }
      }
    }

    // ---- poles: along a road or a lane out in the country, one side, 3.5 m off its platform; each pole's three
    // wires run to the next, 15 to 55 m on and never more than 2 m off the line its side of the road takes
    if ((P.cls == RC_ROAD || P.cls == RC_LANE) && L.length() > 120.f) {
      const float sd = hash2i(ipi, 41) < 0.5f ? 1.f : -1.f, off = spec.halfPlatform + 3.5f;
      auto spot = [&](float s) { const Station st = L.at(s); return st.p + rightOf(st.d) * (sd * off); };
      std::vector<vec2> line;   // the poles in order (a gap: a pole refused, so the wires stop there)
      std::vector<bool> stands;
      float s = 20.f;
      while (s < L.length() - 20.f) {
        const vec2 p = spot(s);
        const Station st = L.at(s);
        const bool ok = !L.nearBridge(s, 25.f) && share(p) > 1.2f && clearOf(p, -1, 1.5f) && !st.bridge;
        line.push_back(p); stands.push_back(ok);
        // the next: the furthest of 15 to 55 m on whose wires keep within 2 m of the line between (round a hairpin, near)
        float next = s + 15.f;
        for (float t = std::min(s + 55.f, L.length() - 20.f); t > s + 15.f; t -= 5.f) {
          const vec2 q = spot(t), u = q - p; const float ul = std::max(length(u), 1e-3f);
          bool straight = true;
          for (float v = s + 5.f; v < t && straight; v += 5.f) straight = fabsf(crossz(u * (1.f / ul), spot(v) - p)) <= 2.f;
          if (straight) { next = t; break; }
        }
        s = next;
      }
      for (size_t i = 0; i < line.size(); i++) {
        if (!stands[i]) continue;
        const vec2 p = line[i]; const float y = ground(p);
        if (i + 1 < line.size() && stands[i + 1]) {
          const vec2 q = line[i + 1], u = q - p; const float d = length(u);
          put(EK_POLE, p, y, yawFacing(u * (1.f / d)), 1.f, 1.f, d / 50.f, 0.5f + std::clamp((ground(q) - y) / d, -0.45f, 0.45f));
        } else {
          const Station st = L.at(L.arcOf(p));
          put(EK_POLE, p, y, yawFacing(st.d), 1.f, 1.f, 1.f, 2.f);
        }
      }
    }

    // ---- signs, to the right of the traffic and facing it, out of the settlements (whose buildings stand close by):
    // where a road or a lane ends on another, a direction sign on its approach 30 m short of the junction and on the
    // other road's approaches 150 m either side of it (green by a highway); where one comes into a settlement, its
    // name at the edge
    auto sign = [&](const Line& R, float s, float toward, int path, bool major) {   // (toward: +1 the traffic runs up R's arc)
      if (s < 0.f || s > R.length()) return;
      const Station st = R.at(s);
      if (st.bridge) return;
      const vec2 t = st.d * toward, p = st.p + rightOf(t) * (roadSpec(R.P.cls).halfPlatform + 1.5f);
      if (!clearOf(p, path, 1.f) || share(p) < 1.05f) return;
      const float k = major ? 1.5f : 1.f, h = hash2i((int)p.x, (int)p.y);
      put(EK_ROADSIGN, p, ground(p), yawFacing(t * -1.f), k, major ? 1.35f : 1.f, 1.f, (major ? 0.1f : 0.6f) + 0.3f * h);
    };
    if (P.cls == RC_ROAD || P.cls == RC_LANE || P.cls == RC_HIGHWAY) {
      for (int end = 0; end < 2; end++) {
        const RoadPoint& e = end ? P.pts.back() : P.pts.front();
        int sg = -1;
        if (roads.nearest(vec2(e.x, e.z), 1.5f, ipi, &sg) > 1e8f) continue;
        const RoadPath& Q = net.paths[roads.segs[sg].path];
        if (Q.cls != RC_HIGHWAY && Q.cls != RC_ROAD && Q.cls != RC_LANE) continue;
        const bool major = Q.cls == RC_HIGHWAY || P.cls == RC_HIGHWAY;
        sign(L, end ? L.length() - 30.f : 30.f, end ? 1.f : -1.f, ipi, major);
        const Line M(Q);
        const float sJ = M.arcOf(vec2(e.x, e.z));
        sign(M, sJ - 150.f, 1.f, roads.segs[sg].path, major);
        sign(M, sJ + 150.f, -1.f, roads.segs[sg].path, major);
      }
      float was = share(L.at(0.f).p);
      for (float s = 10.f; s <= L.length(); s += 10.f) {
        const float now = share(L.at(s).p);
        if ((was >= 1.f) != (now >= 1.f)) {   // (its edge between s - 10 and s: the sign where the fields begin, out from it)
          const float out = was >= 1.f ? -1.f : 1.f;
          for (float t = out < 0.f ? s - 10.f : s; t >= 0.f && t <= L.length() && fabsf(t - s) < 250.f; t += out * 10.f)
            if (share(L.at(t).p) >= 1.1f) { sign(L, t, -out, ipi, false); break; }
        }
        was = now;
      }
    }
  }
  // by chunk, so each chunk takes its own as it is generated
  for (RoadFurniture& f : out) f.e.yaw = remainderf(f.e.yaw, 6.2831853f);
  std::vector<int> key(out.size());
  for (size_t i = 0; i < out.size(); i++) key[i] = Scenery::chunkOf(out[i].e.z) * Scenery::NC + Scenery::chunkOf(out[i].e.x);
  std::vector<size_t> order(out.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key[a] < key[b]; });
  g_items.clear(); g_chunkOf.clear();
  for (size_t i : order) { g_items.push_back(out[i]); g_chunkOf.push_back(key[i]); }
}

const std::vector<RoadFurniture>& roadFurniture() { return g_items; }

std::pair<const RoadFurniture*, const RoadFurniture*> roadFurnitureIn(int cx, int cz) {
  const int k = cz * Scenery::NC + cx;
  const auto lo = std::lower_bound(g_chunkOf.begin(), g_chunkOf.end(), k), hi = std::upper_bound(lo, g_chunkOf.end(), k);
  return {g_items.data() + (lo - g_chunkOf.begin()), g_items.data() + (hi - g_chunkOf.begin())};
}
