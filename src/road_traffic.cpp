// Somnium Volandi - the traffic on the road network (road_traffic.h)
#include "road_traffic.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

RoadTraffic g_roadTraffic;

namespace {
bool drives(int cls) { return cls == RC_HIGHWAY || cls == RC_ROAD || cls == RC_LANE || cls == RC_STREET; }
// cars a kilometre of each class carries, both ways together
float density(int cls) { return cls == RC_HIGHWAY ? 20.f : cls == RC_ROAD ? 10.f : cls == RC_STREET ? 14.f : 2.f; }
const float kCell = 500.f;
const int kCells = (int)(2.f * WORLD_HALF / kCell);
int cellOf(float v) { return std::clamp((int)floorf((v + WORLD_HALF) / kCell), 0, kCells - 1); }
uint32_t next(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
float unit(uint32_t& s) { return (next(s) & 0xFFFFFF) / 16777216.f; }
}  // namespace

// a lane's speed: a highway's fast lane 31 m/s (110 km/h), its slow lane - the lorries' - 24; a road's 22, a street's 10,
// a lane's 13 (every car in a lane at its pace: none catches the one ahead)
float RoadTraffic::speedOf(int cls, int lane, int kind) {
  (void)kind;
  return cls == RC_HIGHWAY ? (lane == 0 ? 31.f : 24.f) : cls == RC_ROAD ? 22.f : cls == RC_STREET ? 10.f : 13.f;
}
// the lanes' centres right of the centreline: a highway's two each way (the reserve 2 m either side, then 3.6 m lanes:
// terrain_material.glsl's markings), a road's and a street's one (3.5 m), a lane's half its 5.8 m
float RoadTraffic::laneOffset(int cls, int lane) {
  return cls == RC_HIGHWAY ? (lane == 0 ? 4.15f : 7.75f) : cls == RC_LANE ? 1.45f : 1.75f;
}

void RoadTraffic::index(const World& world) {
  const auto& P = world.roads.paths;
  netKey = P.data(); netN = P.size();
  along.assign(P.size(), {}); drivable.assign(P.size(), 0);
  for (int e = 0; e < 2; e++) onFrom[e].assign(P.size(), {});
  cellPaths.assign((size_t)kCells * kCells, {});
  cars.clear(); filled = false;
  // the drivable roads' segments in 64 m cells: what meets a road's end
  struct Seg { int path, k; };
  std::unordered_map<int64_t, std::vector<Seg>> segs;
  auto key = [](float x, float z) { return (int64_t)floorf(x / 64.f) * 1000003 + (int64_t)floorf(z / 64.f); };
  for (size_t pi = 0; pi < P.size(); pi++) {
    const RoadPath& p = P[pi];
    std::vector<float>& a = along[pi];
    a.assign(p.pts.size(), 0.f);
    for (size_t k = 1; k < p.pts.size(); k++) a[k] = a[k - 1] + hypotf(p.pts[k].x - p.pts[k - 1].x, p.pts[k].z - p.pts[k - 1].z);
    if (!drives(p.cls) || p.pts.size() < 2 || a.back() < 20.f) continue;
    drivable[pi] = 1;
    int last = -1;
    for (size_t k = 0; k < p.pts.size(); k++) {
      const int c = cellOf(p.pts[k].z) * kCells + cellOf(p.pts[k].x);
      if (c != last) { auto& v = cellPaths[c]; if (v.empty() || v.back() != (int)pi) v.push_back((int)pi); last = c; }
      if (k + 1 < p.pts.size()) {
        const float mx = 0.5f * (p.pts[k].x + p.pts[k + 1].x), mz = 0.5f * (p.pts[k].z + p.pts[k + 1].z);
        segs[key(mx, mz)].push_back({(int)pi, (int)k});
      }
    }
  }
  for (auto& v : cellPaths) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }
  // each end's ways on: a road whose end meets it (on from there), or one it ends on (along it either way); else back
  for (size_t pi = 0; pi < P.size(); pi++) {
    if (!drivable[pi]) continue;
    const RoadPath& p = P[pi];
    for (int e = 0; e < 2; e++) {
      const RoadPoint& E = e ? p.pts.back() : p.pts.front();
      std::vector<End>& ways = onFrom[e][pi];
      for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
          auto it = segs.find(key(E.x + di * 64.f, E.z + dj * 64.f));
          if (it == segs.end()) continue;
          for (const Seg& sg : it->second) {
            if (sg.path == (int)pi) continue;
            const RoadPath& q = P[sg.path];
            const RoadPoint &A = q.pts[sg.k], &B = q.pts[sg.k + 1];
            const float dx = B.x - A.x, dz = B.z - A.z, L2 = std::max(dx * dx + dz * dz, 1e-6f);
            const float t = std::clamp(((E.x - A.x) * dx + (E.z - A.z) * dz) / L2, 0.f, 1.f);
            if (hypotf(A.x + dx * t - E.x, A.z + dz * t - E.z) > 3.f) continue;
            const float s = along[sg.path][sg.k] + t * sqrtf(L2), len = along[sg.path].back();
            bool dup = false;
            for (const End& w : ways) dup = dup || (w.path == sg.path && fabsf(w.s - s) < 5.f);
            if (dup) continue;
            if (s > 3.f) ways.push_back({sg.path, s, -1});          // (back along it)
            if (s < len - 3.f) ways.push_back({sg.path, s, 1});     // (on along it)
          }
        }
      if (ways.empty()) ways.push_back({(int)pi, e ? along[pi].back() : 0.f, e ? -1 : 1});   // (a dead end: back the way it came)
    }
  }
}

bool RoadTraffic::place(const World& world, const RoadCar& c, vec3& at, vec2& heading) const {
  if (c.path < 0 || c.path >= (int)world.roads.paths.size() || c.path >= (int)along.size()) return false;
  const RoadPath& p = world.roads.paths[c.path];
  const std::vector<float>& a = along[c.path];
  const size_t k = std::min((size_t)(std::upper_bound(a.begin(), a.end(), c.s) - a.begin()), a.size() - 1);
  const size_t i = k == 0 ? 0 : k - 1;
  const RoadPoint &A = p.pts[i], &B = p.pts[std::min(i + 1, p.pts.size() - 1)];
  const float seg = std::max(a[std::min(i + 1, a.size() - 1)] - a[i], 1e-3f), t = std::clamp((c.s - a[i]) / seg, 0.f, 1.f);
  const vec2 d = vec2(B.x - A.x, B.z - A.z) * (1.f / std::max(hypotf(B.x - A.x, B.z - A.z), 1e-3f)) * (float)c.dir;
  const vec2 right(-d.y, d.x), off = right * laneOffset(p.cls, c.lane);
  at = vec3(A.x + (B.x - A.x) * t + off.x, A.h + (B.h - A.h) * t, A.z + (B.z - A.z) * t + off.y);
  heading = d;
  return true;
}

void RoadTraffic::update(const World& world, vec3 cam, float dt) {
  const auto& P = world.roads.paths;
  if (P.empty()) { cars.clear(); return; }
  if (netKey != (const void*)P.data() || netN != P.size()) index(world);
  cam.x = wrapCoord(cam.x); cam.z = wrapCoord(cam.z);
  if (length(vec2(cam.x, cam.z) - lastCam) > 400.f) filled = false;
  lastCam = vec2(cam.x, cam.z);
  dt = std::clamp(dt, 0.f, 0.25f);
  // ---- on along their roads, through the junctions: each lane's cars front first, each no nearer than 10 m to the
  // one ahead (a car waiting at a junction holds those behind it); at its road's end a car carries on along a way it
  // picks there - waiting while that way is taken within 12 m of where it would come out
  auto progress = [&](const RoadCar& c) { return c.dir > 0 ? c.s : along[c.path].back() - c.s; };
  std::vector<int> order(cars.size());
  for (size_t i = 0; i < cars.size(); i++) order[i] = (int)i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    const RoadCar &A = cars[a], &B = cars[b];
    if (A.path != B.path) return A.path < B.path;
    if (A.dir != B.dir) return A.dir < B.dir;
    if (A.lane != B.lane) return A.lane < B.lane;
    return progress(A) != progress(B) ? progress(A) > progress(B) : A.id < B.id; });
  auto taken = [&](int path, int dir, int lane, float s, int self) {
    for (size_t i = 0; i < cars.size(); i++)
      if ((int)i != self && cars[i].path == path && cars[i].dir == dir && cars[i].lane == lane && fabsf(cars[i].s - s) < 12.f) return true;
    return false;
  };
  for (size_t n = 0; n < order.size(); n++) {
    const int i = order[n];
    RoadCar& c = cars[i];
    const int path0 = c.path, dir0 = c.dir, lane0 = c.lane;
    const float len = along[c.path].back(), was = progress(c);
    float to = was + speedOf(P[c.path].cls, c.lane, c.kind) * dt;
    // (the car ahead in its lane: the one before it in this order, if still on this road, this way, in this lane)
    if (n > 0) {
      const RoadCar& a = cars[order[n - 1]];
      if (a.path == path0 && a.dir == dir0 && a.lane == lane0) to = std::min(to, std::max(progress(a) - 10.f, was));
    }
    if (to <= len) { c.s = c.dir > 0 ? to : len - to; continue; }
    const std::vector<End>& ways = onFrom[c.dir > 0 ? 1 : 0][c.path];
    const End& w = ways[next(rng) % ways.size()];
    const int lane = P[w.path].cls != RC_HIGHWAY ? 0 : c.kind != EK_CAR ? 1 : c.lane;
    const float past = std::min(to - len, 5.f), s = std::clamp(w.s + w.dir * past, 0.f, along[w.path].back());
    if (taken(w.path, w.dir, lane, s, i)) { c.s = c.dir > 0 ? len : 0.f; continue; }   // (waiting its turn at the junction)
    c.path = w.path; c.dir = w.dir; c.lane = lane; c.s = s;
  }
  // ---- gone out of the ring: dropped
  cars.erase(std::remove_if(cars.begin(), cars.end(), [&](const RoadCar& c) {
    vec3 at; vec2 h; if (!place(world, c, at, h)) return true;
    return hypotf(at.x - cam.x, at.z - cam.z) > kRing + 300.f; }), cars.end());
  // ---- the ring's roads: each one's share of cars (its length in the ring by its class's density), added out of sight
  // (from kSpawnMin out, once the ring has filled), clear of the cars already on it
  std::vector<int> near; std::vector<float> inRing;
  const int r = (int)ceilf(kRing / kCell) + 1;
  for (int j = cellOf(cam.z) - r; j <= cellOf(cam.z) + r; j++)
    for (int i = cellOf(cam.x) - r; i <= cellOf(cam.x) + r; i++) {
      if (i < 0 || j < 0 || i >= kCells || j >= kCells) continue;
      for (int pi : cellPaths[(size_t)j * kCells + i]) near.push_back(pi);
    }
  std::sort(near.begin(), near.end()); near.erase(std::unique(near.begin(), near.end()), near.end());
  float want = 0.f, total = 0.f;
  for (int pi : near) {
    const RoadPath& p = P[pi];
    float L = 0.f;
    for (size_t k = 0; k + 1 < p.pts.size(); k++) {
      const float mx = 0.5f * (p.pts[k].x + p.pts[k + 1].x), mz = 0.5f * (p.pts[k].z + p.pts[k + 1].z);
      if (hypotf(mx - cam.x, mz - cam.z) < kRing) L += along[pi][k + 1] - along[pi][k];
    }
    inRing.push_back(L * density(p.cls) / 1000.f);
    want += inRing.back(); total += inRing.back();
  }
  const int target = std::min(kMost, (int)want);
  for (int attempt = 0; attempt < (filled ? 40 : 4000) && (int)cars.size() < target && total > 0.f; attempt++) {
    float pick = unit(rng) * total; size_t n = 0;
    while (n + 1 < near.size() && pick > inRing[n]) { pick -= inRing[n]; n++; }
    const int pi = near[n];
    const RoadPath& p = P[pi];
    RoadCar c; c.path = pi; c.s = unit(rng) * along[pi].back(); c.dir = unit(rng) < 0.5f ? 1 : -1;
    const float lorry = p.cls == RC_HIGHWAY ? 0.14f : p.cls == RC_ROAD ? 0.1f : p.cls == RC_LANE ? 0.03f : 0.f;
    c.kind = unit(rng) < lorry ? EK_TRANSPORTER : EK_CAR;
    c.lane = p.cls == RC_HIGHWAY ? (c.kind == EK_CAR && unit(rng) < 0.5f ? 0 : 1) : 0;
    c.seed = 2.f + unit(rng);
    vec3 at; vec2 h; place(world, c, at, h);
    const float d = hypotf(at.x - cam.x, at.z - cam.z);
    if (d > kRing || (filled && d < kSpawnMin)) continue;
    bool crowded = false;
    for (const RoadCar& o : cars) crowded = crowded || (o.path == c.path && o.dir == c.dir && fabsf(o.s - c.s) < 30.f);
    if (crowded) continue;
    c.id = ++ids;
    cars.push_back(c);
  }
  filled = true;
}

void RoadTraffic::instances(const World& world, std::vector<std::pair<int, Ent>>& out) const {
  for (const RoadCar& c : cars) {
    vec3 at; vec2 h;
    if (!place(world, c, at, h)) continue;
    out.push_back({c.kind, Ent{at.x, at.y + 0.02f, at.z, atan2f(h.x, h.y), 1.f, 1.f, c.kind == EK_CAR ? 1.f : 0.85f, c.seed}});
  }
}
