// Solace Express - the settlements' extent and streets (settlements.h)
#include "settlements.h"
#include "world.h"
#include "scenery.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

float airportInfluence(float x, float z);   // world.cpp: how far into an airfield's surroundings (0: clear of them)

namespace {

inline float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// a road's own cost to build along, over the effort field (cheap: a settlement grows out along its roads)
float alongRoad(int cls) { return cls == RC_HIGHWAY ? 0.7f : cls == RC_ROAD ? 0.45f : cls == RC_LANE ? 0.55f : 1.f; }

SettlementField makeField(const World& w, const RoadIndex& roads, int town) {
  const Town& T = kTowns[town];
  SettlementField F;
  F.town = town; F.kind = T.kind; F.cx = T.x; F.cz = T.z;
  F.budget = T.r * (T.kind == 2 ? 1.35f : T.kind == 1 ? 1.3f : 1.25f);
  F.core = F.budget * (T.kind == 2 ? 0.5f : T.kind == 1 ? 0.42f : 0.3f);
  const float half = F.budget * 1.7f + 300.f;
  F.n = (int)ceilf(2.f * half / SETTLE_CELL);
  F.x0 = T.x - F.n * SETTLE_CELL * 0.5f; F.z0 = T.z - F.n * SETTLE_CELL * 0.5f;
  const int n = F.n;
  auto cx = [&](int i) { return F.x0 + (i + 0.5f) * SETTLE_CELL; };
  auto cz = [&](int j) { return F.z0 + (j + 0.5f) * SETTLE_CELL; };
  std::vector<float> h((size_t)n * n), cost((size_t)n * n);
  // (the natural ground: the same whether the network's grid is built yet or loaded with its streets)
  for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) h[(size_t)j * n + i] = w.naturalHeight(cx(i), cz(j), 6);
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      const size_t c = (size_t)j * n + i;
      const float x = cx(i), z = cz(j);
      if (h[c] < 2.5f || airportInfluence(x, z) > 0.02f) { cost[c] = INFINITY; continue; }   // (the sea, the beach, an airfield)
      auto H = [&](int a, int b) { return h[(size_t)std::clamp(b, 0, n - 1) * n + std::clamp(a, 0, n - 1)]; };
      const float sx = (H(i + 1, j) - H(i - 1, j)) / (2.f * SETTLE_CELL), sz = (H(i, j + 1) - H(i, j - 1)) / (2.f * SETTLE_CELL);
      const float s = sqrtf(sx * sx + sz * sz);
      float k = 1.f + (s / 0.15f) * (s / 0.15f);
      if (s > 0.35f) k *= 3.f;
      int seg = -1;
      if (roads.nearest(vec2(x, z), 30.f, -1, &seg) < 1e8f) k *= alongRoad(roads.segs[seg].cls);
      cost[c] = k;
    }
  // the cheapest way to each cell from the centre (eight neighbours), as far as a little past the budget
  F.effort.assign((size_t)n * n, 1e9f);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
  // (from its centre - or, where that's steep, the gentlest ground within most of its radius: a hill town's level part)
  int c0 = (n / 2) * n + n / 2;
  if (!std::isfinite(cost[c0]) || cost[c0] > 2.f) {
    const int r = (int)(0.8f * T.r / SETTLE_CELL);
    for (int j = n / 2 - r; j <= n / 2 + r; j++) for (int i = n / 2 - r; i <= n / 2 + r; i++) {
      if (i < 0 || j < 0 || i >= n || j >= n || (i - n / 2) * (i - n / 2) + (j - n / 2) * (j - n / 2) > r * r) continue;
      const int c = j * n + i;
      if (std::isfinite(cost[c]) && (!std::isfinite(cost[c0]) || cost[c] < cost[c0] - 0.05f)) c0 = c;
    }
  }
  if (std::isfinite(cost[c0])) { F.effort[c0] = 0.f; q.push({0.f, c0}); }
  const int nb[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  while (!q.empty()) {
    const auto [e, c] = q.top(); q.pop();
    if (e > F.effort[c] || e > F.budget * 1.4f) continue;
    const int i = c % n, j = c / n;
    for (auto& d : nb) {
      const int a = i + d[0], b = j + d[1];
      if (a < 0 || b < 0 || a >= n || b >= n) continue;
      const int m = b * n + a;
      if (!std::isfinite(cost[m])) continue;
      const float ne = e + SETTLE_CELL * (d[0] && d[1] ? 1.4142f : 1.f) * 0.5f * (cost[c] + cost[m]);
      if (ne < F.effort[m]) { F.effort[m] = ne; q.push({ne, m}); }
    }
  }
  // its grid turned to the road nearest its centre (none within twice its radius: square to the map)
  int seg = -1; float t = 0.f;
  F.gx = T.x; F.gz = T.z;
  if (roads.nearest(vec2(T.x, T.z), 2.f * T.r, -1, &seg, &t) < 1e8f) {
    const RoadIndex::Seg& s = roads.segs[seg];
    const vec2 d = s.b - s.a; const float l = std::max(length(d), 1e-3f);
    F.axisC = d.x / l; F.axisS = d.y / l;
    const vec2 g = s.a + d * t; F.gx = g.x; F.gz = g.y;
  }
  return F;
}

}  // namespace

void RoadIndex::add(const Seg& s) {
  const int id = (int)segs.size(); segs.push_back(s);
  for (int j = cell(std::min(s.a.y, s.b.y)); j <= cell(std::max(s.a.y, s.b.y)); j++)
    for (int i = cell(std::min(s.a.x, s.b.x)); i <= cell(std::max(s.a.x, s.b.x)); i++) cells[key(i, j)].push_back(id);
}
void RoadIndex::addPath(const RoadPath& p, int pi) {
  for (size_t k = 0; k + 1 < p.pts.size(); k++)
    add({vec2(p.pts[k].x, p.pts[k].z), vec2(p.pts[k + 1].x, p.pts[k + 1].z), p.pts[k].h, p.pts[k + 1].h, pi, (uint8_t)p.cls});
}
float RoadIndex::nearest(vec2 p, float r, int skip, int* seg, float* tOut, bool edge) const {
  float best = 1e9f;
  const float reach = r + (edge ? 16.f : 0.f);   // (to a platform's edge: the widest's half within reach too)
  for (int j = cell(p.y - reach); j <= cell(p.y + reach); j++)
    for (int i = cell(p.x - reach); i <= cell(p.x + reach); i++) {
      auto it = cells.find(key(i, j)); if (it == cells.end()) continue;
      for (int id : it->second) {
        const Seg& s = segs[id]; if (s.path == skip) continue;
        const vec2 d = s.b - s.a; const float t = std::clamp(dot2(p - s.a, d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
        const float e = length(s.a + d * t - p) - (edge ? roadSpec(s.cls).halfPlatform : 0.f);
        if (e < best) { best = e; if (seg) *seg = id; if (tOut) *tOut = t; }
      }
    }
  return best <= r ? best : 1e9f;
}
void RoadIndex::crossings(vec2 a, vec2 b, int skip, std::vector<RoadPin>& out) const {
  const vec2 r = b - a;
  for (int j = cell(std::min(a.y, b.y)); j <= cell(std::max(a.y, b.y)); j++)
    for (int i = cell(std::min(a.x, b.x)); i <= cell(std::max(a.x, b.x)); i++) {
      auto it = cells.find(key(i, j)); if (it == cells.end()) continue;
      for (int id : it->second) {
        const Seg& s = segs[id]; if (s.path == skip) continue;
        const vec2 q = s.b - s.a; const float den = cross2(r, q);
        if (fabsf(den) < 1e-6f) continue;
        const vec2 w = s.a - a; const float tab = cross2(w, q) / den, ts = cross2(w, r) / den;
        if (tab <= 0.f || tab >= 1.f || ts < 0.f || ts > 1.f) continue;
        const vec2 at = a + r * tab;
        bool dup = false; for (const RoadPin& p : out) dup = dup || hypotf(p.x - at.x, p.z - at.y) < 8.f;
        if (!dup) out.push_back({at.x, at.y, s.ha + (s.hb - s.ha) * ts});
      }
    }
}

float SettlementField::at(float x, float z) const {
  const float fx = (x - x0) / SETTLE_CELL - 0.5f, fz = (z - z0) / SETTLE_CELL - 0.5f;
  const int i = (int)floorf(fx), j = (int)floorf(fz);
  if (i < 0 || j < 0 || i >= n - 1 || j >= n - 1) return 1e9f;
  const float tx = fx - i, tz = fz - j;
  const float a = effort[(size_t)j * n + i], b = effort[(size_t)j * n + i + 1], c = effort[(size_t)(j + 1) * n + i], d = effort[(size_t)(j + 1) * n + i + 1];
  if (std::max(std::max(a, b), std::max(c, d)) > 1e8f) {   // (an edge of the reachable: the nearest corner's, no blending into "never")
    const float m = tx < 0.5f ? (tz < 0.5f ? a : c) : (tz < 0.5f ? b : d);
    return m;
  }
  return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
}

std::vector<SettlementField> buildSettlementFields(const World& world) {
  RoadIndex roads;
  for (size_t pi = 0; pi < world.roads.paths.size(); pi++)
    if (world.roads.paths[pi].cls != RC_STREET) roads.addPath(world.roads.paths[pi], (int)pi);
  std::vector<SettlementField> fields(kNumTowns);
  parallelFor(kNumTowns, [&](int t) { fields[t] = makeField(world, roads, t); });
  return fields;
}

float settlementShare(const std::vector<SettlementField>& fields, float x, float z, int* town) {
  float best = 1e9f; int which = -1;
  for (const SettlementField& F : fields) {
    if (x < F.x0 || z < F.z0 || x > F.x0 + F.n * SETTLE_CELL || z > F.z0 + F.n * SETTLE_CELL) continue;
    const float e = F.at(x, z);
    if (e > 1e8f) continue;
    const float r = e / F.budget;
    if (r < best) { best = r; which = F.town; }
  }
  if (town) *town = which;
  return best;
}

void addSettlementStreets(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net) {
  RoadIndex idx;
  for (size_t pi = 0; pi < net.paths.size(); pi++) idx.addPath(net.paths[pi], (int)pi);
  auto ground = [&](vec2 p) { return world.groundHeight(p.x, p.y, 8); };
  // whether a place is in this settlement, within a share of its extent, on dry land clear of the airfields
  auto inside = [&](vec2 p, const SettlementField& F, float limit) {
    int t; const float share = settlementShare(fields, p.x, p.y, &t);
    return t == F.town && share < limit && ground(p) >= 2.5f && airportInfluence(p.x, p.y) < 0.02f;
  };
  // lay a street along a line: level with every road it crosses, its ends at the height of any road it starts or
  // stops on; never on a fill or in a cut more than 3.5 m (it should lie on its ground: else not built)
  auto lay = [&](const std::vector<vec2>& line) {
    if (line.size() < 2) return false;
    std::vector<RoadPin> pins;
    for (size_t k = 0; k + 1 < line.size(); k++) idx.crossings(line[k], line[k + 1], -1, pins);
    auto endHeight = [&](vec2 p) {
      int sg = -1; float t = 0.f;
      if (idx.nearest(p, 1.5f, -1, &sg, &t) < 1e8f) return idx.segs[sg].ha + (idx.segs[sg].hb - idx.segs[sg].ha) * t;
      return ground(p);
    };
    pins.erase(std::remove_if(pins.begin(), pins.end(), [&](const RoadPin& p) {   // (not at its ends: those are its ends' heights)
      return hypotf(p.x - line.front().x, p.z - line.front().y) < 3.f || hypotf(p.x - line.back().x, p.z - line.back().y) < 3.f; }), pins.end());
    // level through each junction and crossing, out to 4 m past the other road's platform: a side street meets the
    // road it leaves at its height, and only then climbs (else the road's own bed dipped at every junction)
    std::vector<float> arc(line.size(), 0.f);
    for (size_t k = 1; k < line.size(); k++) arc[k] = arc[k - 1] + length(line[k] - line[k - 1]);
    auto arcOf = [&](vec2 q) {
      float best = 1e9f, at = 0.f;
      for (size_t k = 0; k + 1 < line.size(); k++) {
        const vec2 d = line[k + 1] - line[k]; const float t = std::clamp(dot2(q - line[k], d) / std::max(dot2(d, d), 1e-6f), 0.f, 1.f);
        const float e = length(line[k] + d * t - q);
        if (e < best) { best = e; at = arc[k] + (arc[k + 1] - arc[k]) * t; }
      }
      return at;
    };
    auto pointAt = [&](float s) {
      size_t k = 0; while (k + 2 < line.size() && arc[k + 1] < s) k++;
      const float t = std::clamp((s - arc[k]) / std::max(arc[k + 1] - arc[k], 1e-3f), 0.f, 1.f);
      return line[k] + (line[k + 1] - line[k]) * t;
    };
    auto otherHalf = [&](vec2 q) { int sg = -1; idx.nearest(q, 3.f, -1, &sg); return sg >= 0 ? roadSpec(idx.segs[sg].cls).halfPlatform : 0.f; };
    std::vector<RoadPin> landings;
    auto landing = [&](vec2 q, float h, int dirs) {
      const float s = arcOf(q), L = otherHalf(q) + 4.f;
      for (int sgn : {-1, 1}) {
        if (!(dirs & (sgn < 0 ? 1 : 2))) continue;
        const float t = s + sgn * L;
        if (t <= 2.f || t >= arc.back() - 2.f) continue;
        const vec2 at = pointAt(t); landings.push_back({at.x, at.y, h});
      }
    };
    for (const RoadPin& p : pins) landing(vec2(p.x, p.z), p.h, 3);
    const float h0 = endHeight(line.front()), h1 = endHeight(line.back());
    if (idx.nearest(line.front(), 1.5f, -1) < 1e8f) landing(line.front(), h0, 2);
    if (idx.nearest(line.back(), 1.5f, -1) < 1e8f) landing(line.back(), h1, 1);
    pins.insert(pins.end(), landings.begin(), landings.end());
    RoadPath p = layRoad(world, RC_STREET, line, h0, h1, pins);
    if (p.pts.size() < 2) return false;
    for (const RoadPoint& q : p.pts) if (fabsf(q.h - world.naturalHeight(q.x, q.z)) > 3.5f) return false;
    for (size_t k = 0; k + 1 < p.pts.size(); k++)   // (nor where its crossings leave it steeper than a street may be)
      if (fabsf(p.pts[k + 1].h - p.pts[k].h) > roadSpec(RC_STREET).maxGrade * hypotf(p.pts[k + 1].x - p.pts[k].x, p.pts[k + 1].z - p.pts[k].z) + 0.001f) return false;
    const int pi = (int)net.paths.size();
    idx.addPath(p, pi); net.paths.push_back(std::move(p));
    return true;
  };
  // ---- the cores: a grid of streets turned to the main road, blocks of 110 x 80 m in a city, 100 x 75 in a town;
  // each line of it built where it runs through the core over buildable ground, but not along a road there already
  std::vector<int> core;   // (the core's streets: the suburbs carry them on out)
  for (const SettlementField& F : fields) {
    if (F.kind == 0) continue;
    const vec2 c(F.gx, F.gz), u(F.axisC, F.axisS), v(-F.axisS, F.axisC);
    const float bu = F.kind == 2 ? 110.f : 100.f, bv = F.kind == 2 ? 80.f : 75.f, half = F.core * 1.2f, coreShare = F.core / F.budget;
    for (int family = 0; family < 2; family++) {
      const vec2 along = family ? v : u, across = family ? u : v;
      const float spacing = family ? bu : bv;
      for (int k = -(int)(half / spacing); k <= (int)(half / spacing); k++) {
        std::vector<vec2> run;
        auto flush = [&]() { if (run.size() >= 8 && lay({run.front(), run.back()})) core.push_back((int)net.paths.size() - 1); run.clear(); };
        for (float s = -half; s <= half; s += 10.f) {
          const vec2 p = c + along * s + across * (k * spacing);
          bool ok = inside(p, F, coreShare);
          if (ok && !run.empty()) ok = fabsf(ground(p) - ground(run.back())) <= 10.f * 0.14f;
          if (ok) {   // (not along a road already there: a street a few metres beside it)
            int sg = -1;
            if (idx.nearest(p, 24.f, -1, &sg) < 1e8f) {
              const vec2 d = idx.segs[sg].b - idx.segs[sg].a;
              ok = fabsf(dot2(d, along)) < 0.9f * length(d);
            }
          }
          if (ok) run.push_back(p); else flush();
        }
        flush();
      }
    }
  }
  // ---- round the cores and through the villages: streets branching off the roads and streets every so far, out to
  // either side by turns, each finding its way over the ground - holding its grade, wandering a little, keeping 40 m
  // from the next street (room for the lots between) - until it has gone its length, reached the settlement's edge or
  // found nowhere to go; ending at another street where it meets one, in a turning where it doesn't. Twice: the second
  // round branches off the first's
  auto branch = [&](const SettlementField& F, vec2 from, vec2 dir, int parent, float maxLen, uint32_t seed) {
    std::vector<vec2> line{from};
    vec2 pos = from; float heading = atan2f(dir.y, dir.x), run = 0.f;
    const float step = 12.f;
    bool joined = false;
    while (run < maxLen && !joined) {
      const float wander = 5.f * DEG * sinf(seed * 0.37f + run / 70.f);
      float best = 1e9f; vec2 next; float nh = heading;
      for (float d : {0.f, -8.f, 8.f, -16.f, 16.f}) {
        const float a = heading + d * DEG + wander;
        const vec2 q = pos + vec2(cosf(a), sinf(a)) * step;
        if (!inside(q, F, 1.f)) continue;
        const float grade = fabsf(ground(q) - ground(pos)) / step;
        if (grade > 0.12f) continue;
        const float score = grade * 12.f + fabsf(d) / 16.f;
        if (score < best) { best = score; next = q; nh = a; }
      }
      if (best > 1e8f) break;
      // the next street near: past the first few metres, join it if it's close ahead, else stop short of it
      int sg = -1; float t = 0.f;
      const float near = idx.nearest(next, 40.f, run < 30.f ? parent : -2, &sg, &t);
      if (near < 1e8f && run >= 30.f) {
        if (near < 24.f && idx.segs[sg].cls != RC_HIGHWAY) { const RoadIndex::Seg& s = idx.segs[sg]; line.push_back(s.a + (s.b - s.a) * t); joined = true; }
        break;
      }
      line.push_back(next); pos = next; heading = nh; run += step;
    }
    if (run < 60.f && !joined) return false;
    return lay(line);
  };
  // the core's streets carried on out from its edge, wandering as the ground has them
  for (int pi : core) {
    const RoadPath P = net.paths[pi];
    for (int end = 0; end < 2; end++) {
      const RoadPoint &e = end ? P.pts.back() : P.pts.front(), &f = end ? P.pts[P.pts.size() - 2] : P.pts[1];
      int town = -1; settlementShare(fields, e.x, e.z, &town);
      if (town < 0) continue;
      const vec2 d(e.x - f.x, e.z - f.z);
      const uint32_t seed = (uint32_t)(pi * 31 + end * 7);
      branch(fields[town], vec2(e.x, e.z), d * (1.f / std::max(length(d), 1e-3f)), pi, 250.f + 300.f * hash2i(pi, end), seed);
    }
  }
  for (int round = 0; round < 3; round++) {
    const size_t parents = net.paths.size();
    for (size_t pi = 0; pi < parents; pi++) {
      const RoadPath P = net.paths[pi];   // (a copy: the streets laid off it grow the list under it)
      if (P.cls == RC_HIGHWAY) continue;
      if (round > 0 && P.cls != RC_STREET) continue;
      float acc = 0.f; int side = (int)(pi & 1);
      for (size_t k = 1; k < P.pts.size(); k++) {
        const vec2 a(P.pts[k - 1].x, P.pts[k - 1].z), b(P.pts[k].x, P.pts[k].z);
        acc += length(b - a);
        int town = -1;
        const float share = settlementShare(fields, b.x, b.y, &town);
        if (town < 0 || share >= 0.95f || (k < P.bridge.size() && P.bridge[k])) continue;
        const SettlementField& F = fields[town];
        const float spacing = F.kind == 2 ? 110.f : F.kind == 1 ? 130.f : 150.f;
        if (acc < spacing) continue;
        if (F.kind > 0 && share < F.core / F.budget * 0.9f) continue;   // (the core has its grid)
        acc = 0.f; side ^= 1;
        const vec2 d = (b - a) * (1.f / std::max(length(b - a), 1e-3f)), out = side ? vec2(-d.y, d.x) : vec2(d.y, -d.x);
        // (not where a street leaves already, nor off a stretch with no room beside it)
        if (idx.nearest(b + out * (roadSpec(P.cls).halfPlatform + 30.f), 32.f, (int)pi) < 1e8f) continue;
        const uint32_t seed = (uint32_t)(pi * 7919 + k * 104729 + round * 13);
        const float len = (F.kind == 0 ? 120.f : 150.f) + (F.kind == 0 ? 140.f : 250.f) * hash2i((int)(seed % 100003), (int)round);
        branch(F, b, out, (int)pi, round ? len * 0.75f : len, seed);
      }
    }
  }
}
