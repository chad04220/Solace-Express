// Solace Express - the settlements' extent and streets (settlements.h)
#include "settlements.h"
#include "world.h"
#include "scenery.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace {

inline float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// a road's own cost to build along, over the effort field (cheap: a settlement grows out along its roads)
float alongRoad(int cls) { return cls == RC_HIGHWAY ? 0.7f : cls == RC_ROAD ? 0.45f : cls == RC_LANE ? 0.55f : 1.f; }

SettlementField makeField(const World& w, const RoadIndex& roads, int town) {
  generationCheckpoint();
  const Town& T = w.towns[town];
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
  for (int j = 0; j < n; j++) {
    generationCheckpoint();
    for (int i = 0; i < n; i++) h[(size_t)j * n + i] = w.naturalHeight(cx(i), cz(j), 6);
  }
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      if ((i & 63) == 0) generationCheckpoint();
      const size_t c = (size_t)j * n + i;
      const float x = cx(i), z = cz(j);
      if (h[c] < 2.5f || airportInfluence(w, x, z) > 0.02f) { cost[c] = INFINITY; continue; }   // (the sea, the beach, an airfield)
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
    generationCheckpoint();
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
  generationCheckpoint();
  // (the network's own roads: those before its first street - the streets, the racetrack and the farms come after
  // them, laid over the fields, and a world read from its cache has them all)
  RoadIndex roads;
  for (size_t pi = 0; pi < world.roads.paths.size() && world.roads.paths[pi].cls != RC_STREET; pi++) roads.addPath(world.roads.paths[pi], (int)pi);
  std::vector<SettlementField> fields(world.towns.size());
  parallelFor((int)world.towns.size(), [&](int t) { fields[t] = makeField(world, roads, t); });
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

// Lay a path of a class along a line (a street, a farm track, a farmyard): level with every road it crosses, its ends at
// the height of any road it starts or stops on; never on a fill or in a cut more than 3.5 m, nor anywhere steeper than
// its class may be (it should lie on its ground: else not built). With out, the path laid is handed back, not added.
static bool layPath(const World& world, RoadIndex& idx, RoadNetwork& net, RoadClass cls, const std::vector<vec2>& line, RoadPath* out = nullptr) {
  auto ground = [&](vec2 p) { return world.groundHeight(p.x, p.y, 8); };
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
  RoadPath p = layRoad(world, cls, line, h0, h1, pins);
  if (p.pts.size() < 2) return false;
  for (const RoadPoint& q : p.pts) if (fabsf(q.h - world.naturalHeight(q.x, q.z)) > 3.5f) return false;
  for (size_t k = 0; k + 1 < p.pts.size(); k++)   // (nor where its crossings leave it steeper than a street may be)
    if (fabsf(p.pts[k + 1].h - p.pts[k].h) > roadSpec(cls).maxGrade * hypotf(p.pts[k + 1].x - p.pts[k].x, p.pts[k + 1].z - p.pts[k].z) + 0.001f) return false;
  if (out) { *out = std::move(p); return true; }
  const int pi = (int)net.paths.size();
  idx.addPath(p, pi); net.paths.push_back(std::move(p));
  return true;
}

void addSettlementStreets(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net) {
  generationCheckpoint();
  RoadIndex idx;
  for (size_t pi = 0; pi < net.paths.size(); pi++) idx.addPath(net.paths[pi], (int)pi);
  auto ground = [&](vec2 p) { return world.groundHeight(p.x, p.y, 8); };
  // whether a place is in this settlement, within a share of its extent, on dry land clear of the airfields
  auto inside = [&](vec2 p, const SettlementField& F, float limit) {
    int t; const float share = settlementShare(fields, p.x, p.y, &t);
    return t == F.town && share < limit && ground(p) >= 2.5f && airportInfluence(world, p.x, p.y) < 0.02f;
  };
  auto lay = [&](const std::vector<vec2>& line) { return layPath(world, idx, net, RC_STREET, line); };
  // ---- the cores: a grid of streets turned to the main road, blocks of 110 x 80 m in a city, 100 x 75 in a town;
  // each line of it built where it runs through the core over buildable ground, but not along a road there already
  std::vector<int> core;   // (the core's streets: the suburbs carry them on out)
  for (const SettlementField& F : fields) {
    generationCheckpoint();
    if (F.kind == 0) continue;
    const vec2 c(F.gx, F.gz), u(F.axisC, F.axisS), v(-F.axisS, F.axisC);
    const float bu = F.kind == 2 ? 110.f : 100.f, bv = F.kind == 2 ? 80.f : 75.f, half = F.core * 1.2f, coreShare = F.core / F.budget;
    for (int family = 0; family < 2; family++) {
      const vec2 along = family ? v : u, across = family ? u : v;
      const float spacing = family ? bu : bv;
      for (int k = -(int)(half / spacing); k <= (int)(half / spacing); k++) {
        generationCheckpoint();
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
  // found nowhere to go (or short of minLen: not built); ending at another street where it meets one, in a turning
  // where it doesn't. Three rounds, each branching off the last's
  auto branch = [&](const SettlementField& F, vec2 from, vec2 dir, int parent, float maxLen, uint32_t seed, float minLen = 60.f) {
    std::vector<vec2> line{from};
    vec2 pos = from; float heading = atan2f(dir.y, dir.x), run = 0.f;
    const float step = 12.f;
    bool joined = false;
    while (run < maxLen && !joined) {
      generationCheckpoint();
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
    if (run < minLen && !joined) return false;
    return lay(line);
  };
  // the core's streets carried on out from its edge, wandering as the ground has them
  for (int pi : core) {
    generationCheckpoint();
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
      generationCheckpoint();
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
  // ---- and the closes: short streets (48 m and more) into the back-land the rounds leave - off each street every
  // 70 m, to either side, wherever the ground 60 m out is still open (no street within 34 m of it), so a block's middle
  // is reached from its streets and built on rather than left a field behind the houses
  const size_t parents = net.paths.size();
  for (size_t pi = 0; pi < parents; pi++) {
    generationCheckpoint();
    const RoadPath P = net.paths[pi];
    if (P.cls != RC_STREET) continue;
    const float half = roadSpec(P.cls).halfPlatform;
    float acc = 35.f * hash2i((int)pi, 41);
    for (size_t k = 1; k < P.pts.size(); k++) {
      const vec2 a(P.pts[k - 1].x, P.pts[k - 1].z), b(P.pts[k].x, P.pts[k].z);
      acc += length(b - a);
      if (acc < 70.f || k + 1 >= P.pts.size()) continue;
      int town = -1;
      const float share = settlementShare(fields, b.x, b.y, &town);
      if (town < 0 || share >= 0.85f) continue;
      const SettlementField& F = fields[town];
      if (F.kind > 0 && share < F.core / F.budget * 0.9f) continue;   // (the core has its grid)
      acc = 0.f;
      const vec2 d = (b - a) * (1.f / std::max(length(b - a), 1e-3f));
      for (int side = 0; side < 2; side++) {
        const vec2 out = side ? vec2(-d.y, d.x) : vec2(d.y, -d.x);
        if (idx.nearest(b + out * (half + 30.f), 32.f, (int)pi) < 1e8f || idx.nearest(b + out * (half + 60.f), 34.f, -1) < 1e8f) continue;
        if (!inside(b + out * (half + 60.f), F, 0.95f)) continue;
        const uint32_t seed = (uint32_t)(pi * 6151 + k * 92821 + side * 17);
        branch(F, b, out, (int)pi, 72.f + 48.f * hash2i((int)(seed % 100003), 3), seed, 48.f);
      }
    }
  }
}

// ---- the farmsteads (Phase B2): on a jittered 300 m grid over the farmland (as the terrain's mask has it: World::bakeMask,
// no settlement here), each where the ground is level and dry, clear of the settlements and the airfields, 80 m to
// 1.3 km from a road, a lane or a neighbouring farm's track: a yard of gravel and concrete 22 x 42 m (RC_YARD: its ground
// levelled and banked as a road's is) at the end of a gravel track (RC_TRACK) that leaves the road square to it and
// bends a little on its way. Neither crosses another road nor bridges anything, the track keeps clear of the other
// farms, and the yards keep 280 m apart.
void addFarmsteads(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net) {
  generationCheckpoint();
  RoadIndex idx;
  for (size_t pi = 0; pi < net.paths.size(); pi++) idx.addPath(net.paths[pi], (int)pi);
  auto commit = [&](RoadPath p) { const int pi = (int)net.paths.size(); idx.addPath(p, pi); net.paths.push_back(std::move(p)); };
  std::vector<vec2> yards;
  for (const RoadPath& p : net.paths)
    if (p.cls == RC_YARD && !p.pts.empty()) yards.push_back(vec2(p.pts.front().x, p.pts.front().z));
  const Racetrack race = racetrackOf(net);
  auto atRace = [&](vec2 p) {   // (within the racetrack's grounds: its infield, the land round it)
    if (!race.found) return false;
    const float s = dot2(p - race.o, race.u), v = dot2(p - race.o, vec2(-race.u.y, race.u.x));
    return s > -450.f && s < 1250.f && v > -300.f && v < 900.f;
  };
  const float FC = 300.f;
  const int n = (int)ceilf(2.f * WORLD_HALF / FC);
  for (int fj = 0; fj < n; fj++)
    for (int fi = 0; fi < n; fi++) {
      if ((fi & 15) == 0) generationCheckpoint();
      if (hash2i(fi * 11 + 3, fj * 5 + 7) > 0.55f) continue;
      const vec2 site(-WORLD_HALF + (fi + 0.5f + (hash2i(fi * 7 + 1, fj * 3 - 2) - 0.5f) * 0.6f) * FC,
                      -WORLD_HALF + (fj + 0.5f + (hash2i(fi - 4, fj * 9 + 5) - 0.5f) * 0.6f) * FC);
      float b[4]; world.sampleBase(site.x, site.y, b);
      if (b[0] < 5.f) continue;
      const float farm = smoothstepf(4.f, 15.f, b[0]) * smoothstepf(420.f, 260.f, b[0]) * smoothstepf(0.4f, 0.6f, b[2]) * smoothstepf(45.f, 25.f, b[1]) *
                         smoothstepf(0.32f, 0.5f, valueNoise(site.x / 3000.f + 7.7f, site.y / 3000.f - 3.3f));
      if (farm < 0.35f || airportInfluence(world, site.x, site.y) > 0.001f || atRace(site)) continue;
      int town = -1; const float share = settlementShare(fields, site.x, site.y, &town);
      if (town >= 0 && share < 1.4f) continue;
      // the yard's ground: dry, and level enough (5 m at most across it and round it); no road within 80 m, no other
      // yard within 280 m (its land)
      float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 25; k++) {
        const vec2 q = site + vec2((k % 5 - 2) * 12.f, (k / 5 - 2) * 12.f);
        const float g = world.groundHeight(q.x, q.y, 8);
        lo = std::min(lo, g); hi = std::max(hi, g);
      }
      if (lo < 3.f || hi - lo > 5.f || idx.nearest(site, 80.f, -1) < 1e8f) continue;
      bool crowded = false;
      for (const vec2& y : yards) crowded |= length(y - site) < 280.f;
      if (crowded) continue;
      // the way in: a track from the nearest road, lane or neighbour's track within 1.3 km (a farm gate on no street) -
      // or, should that way cross a road or pass through another farm, from the next nearest
      auto clear = [&](vec2 q, float r) { for (const vec2& y : yards) if (length(q - y) < r) return false; return true; };
      std::vector<int> tried;
      RoadPath yard, track;
      bool laid = false;
      for (int attempt = 0; attempt < 3 && !laid; attempt++) {
        float d = 1e9f; int sg = -1; float t = 0.f;
        const float reach = 1300.f;
        for (int j = RoadIndex::cell(site.y - reach); j <= RoadIndex::cell(site.y + reach); j++)
          for (int i = RoadIndex::cell(site.x - reach); i <= RoadIndex::cell(site.x + reach); i++) {
            auto it = idx.cells.find(RoadIndex::key(i, j)); if (it == idx.cells.end()) continue;
            for (int id : it->second) {
              const RoadIndex::Seg& sgm = idx.segs[id];
              if (sgm.cls == RC_STREET || sgm.cls == RC_YARD || sgm.cls == RC_CIRCUIT || sgm.cls == RC_PITLANE || std::find(tried.begin(), tried.end(), sgm.path) != tried.end()) continue;
              const vec2 e = sgm.b - sgm.a; const float u = std::clamp(dot2(site - sgm.a, e) / std::max(dot2(e, e), 1e-6f), 0.f, 1.f);
              const float q = length(sgm.a + e * u - site);
              if (q < d) { d = q; sg = id; t = u; }
            }
          }
        if (d > reach) break;
        const RoadIndex::Seg road = idx.segs[sg];
        tried.push_back(road.path);
        vec2 onRoad = road.a + (road.b - road.a) * t;
        if (road.cls == RC_TRACK && !clear(onRoad, 70.f)) {   // (off a neighbour's track: joining it clear of its farm)
          float best = 1e9f;
          for (const RoadPoint& q : net.paths[road.path].pts)
            if (clear(vec2(q.x, q.z), 70.f) && length(vec2(q.x, q.z) - site) < best) { best = length(vec2(q.x, q.z) - site); onRoad = vec2(q.x, q.z); }
          if (best > reach) continue;
        }
        const vec2 dir = (site - onRoad) * (1.f / std::max(length(site - onRoad), 1e-3f)), side(-dir.y, dir.x);
        const vec2 yardFront = site - dir * 10.f, yardBack = site + dir * 10.f;   // (its platform 13 m round that: 46 m long)
        // the track: square off the road, a gentle bend half way, onto the yard's front; crossing nothing, and 62 m
        // clear of every other farm's yard (its buildings)
        const float run = length(yardFront - onRoad), bend = (hash2i(fi + 31, fj - 17) - 0.5f) * std::min(40.f, run * 0.25f);
        const std::vector<vec2> line = {onRoad, onRoad + dir * std::min(20.f, run * 0.2f), onRoad + dir * (run * 0.55f) + side * bend, yardFront};
        std::vector<RoadPin> crossing;
        for (size_t k = 1; k + 1 < line.size(); k++) idx.crossings(line[k], line[k + 1], -1, crossing);
        bool through = !crossing.empty();
        for (size_t k = 0; k + 1 < line.size() && !through; k++) {
          const vec2 e = line[k + 1] - line[k];
          for (const vec2& y : yards) {
            const float u = std::clamp(dot2(y - line[k], e) / std::max(dot2(e, e), 1e-6f), 0.f, 1.f);
            through |= length(line[k] + e * u - y) < 62.f;
          }
        }
        if (through) continue;
        if (!layPath(world, idx, net, RC_YARD, {yardFront, yardBack}, &yard) || !layPath(world, idx, net, RC_TRACK, line, &track)) continue;
        bool bridged = false;
        for (uint8_t f : yard.bridge) bridged |= f != 0;
        for (uint8_t f : track.bridge) bridged |= f != 0;
        laid = !bridged;
      }
      if (!laid) continue;
      yards.push_back(site); commit(std::move(yard)); commit(std::move(track));
    }
}

std::vector<Farmstead> farmsteadsOf(const RoadNetwork& net) {
  std::vector<Farmstead> farms;
  for (const RoadPath& p : net.paths) {
    if (p.cls != RC_YARD || p.pts.size() < 2) continue;
    const RoadPoint &a = p.pts.front(), &b = p.pts.back();
    const float dx = b.x - a.x, dz = b.z - a.z, l = std::max(hypotf(dx, dz), 1e-3f);
    const float cx = (a.x + b.x) * 0.5f, cz = (a.z + b.z) * 0.5f;
    farms.push_back({cx, cz, dx / l, dz / l, hash2i((int)floorf(cx), (int)floorf(cz))});
  }
  return farms;
}

// ---------------------------------------------------------------- the racetrack (Phase C)
std::vector<vec2> circuitLine() {
  // each leg a straight (R 0) or a bend of radius R through deg (+ to the right), as it's raced from the pit lane's
  // mouth: the main straight; turn 1, a right-hander; a kink left and right; turn 4, right onto the back straight; the
  // hairpin; a long left; the run up to the last corners, right and right again onto the main straight
  struct Leg { float len, R, deg; };
  static const Leg legs[] = {{900.f, 0.f, 0.f}, {0.f, 45.f, 90.f}, {250.f, 0.f, 0.f}, {0.f, 150.f, -40.f}, {0.f, 150.f, 40.f},
                             {150.f, 0.f, 0.f}, {0.f, 60.f, 90.f}, {900.f, 0.f, 0.f}, {0.f, 30.f, 160.f}, {150.f, 0.f, 0.f},
                             {0.f, 80.f, -130.f}, {450.f, 0.f, 0.f}, {0.f, 100.f, 60.f}};
  std::vector<vec2> pts{vec2(0.f, 0.f)};
  vec2 p(0.f, 0.f); float th = 0.f;
  auto straight = [&](float len) {
    const int n = std::max(1, (int)ceilf(len / 4.f)); const vec2 from = p, d(cosf(th), sinf(th));
    for (int i = 1; i <= n; i++) pts.push_back(from + d * (len * i / n));
    p = pts.back();
  };
  auto bend = [&](float R, float deg) {
    const float a = deg * DEG, sg = a > 0.f ? 1.f : -1.f;
    const int n = std::max(2, (int)ceilf(fabsf(a) * R / 4.f));
    const vec2 c = p + vec2(-sinf(th), cosf(th)) * (R * sg);
    for (int i = 1; i <= n; i++) { const float t = th + a * i / n; pts.push_back(c - vec2(-sinf(t), cosf(t)) * (R * sg)); }
    th += a; p = pts.back();
  };
  for (const Leg& L : legs) { if (L.R > 0.f) bend(L.R, L.deg); else straight(L.len); }
  // (heading back up the far side: on to where a 70 m bend to the right comes out on the main straight's line, then
  // along it to the start)
  straight(p.y - 70.f); bend(70.f, 90.f); straight(-p.x);
  pts.back() = vec2(0.f, 0.f);
  return pts;
}

Racetrack racetrackOf(const RoadNetwork& net) {
  Racetrack R;
  for (size_t pi = 0; pi < net.paths.size(); pi++) {
    const RoadPath& p = net.paths[pi];
    if (p.cls == RC_PITLANE) R.pitLane = (int)pi;
    if (p.cls != RC_CIRCUIT || p.pts.size() < 3 || R.found) continue;
    R.o = vec2(p.pts[0].x, p.pts[0].z);
    for (const RoadPoint& q : p.pts)
      if (length(vec2(q.x, q.z) - R.o) >= 100.f) { R.u = (vec2(q.x, q.z) - R.o) * (1.f / length(vec2(q.x, q.z) - R.o)); break; }
    R.found = true; R.circuit = (int)pi;
  }
  return R;
}

// Where the circuit goes: the level ground it needs, out of the settlements and 3 km from the runways, its whole
// length and everything beside it 60 m and more from any road. Searched on the base map every 300 m and 15 degrees -
// the main straight within 4 m of level (its pits and grandstands stand beside it), the circuit nowhere steeper than 5%
// once smoothed over 180 m, the least ground to move under it, the nearer a town or a city the better - then the best
// such place laid out on the ground itself: the circuit within 5 m of the ground everywhere, its pit lane, and a road
// from its paddock to the nearest road that crosses nothing on the way
void addRacetrack(const World& world, const std::vector<SettlementField>& fields, RoadNetwork& net) {
  const std::vector<vec2> line = circuitLine();
  std::vector<vec2> track;   // (every 20 m)
  for (size_t k = 0; k < line.size(); k += 5) track.push_back(line[k]);
  const int nTrack = (int)track.size();
  int nStraight = 0; while (nStraight < nTrack && track[nStraight].x <= 900.f && fabsf(track[nStraight].y) < 1.f) nStraight++;
  std::vector<vec2> grounds;   // (the pits, the paddock and its road, the grandstands, the car park)
  for (float s = 130.f; s <= 660.f; s += 30.f)
    for (float v : {-90.f, -70.f, -50.f, -30.f, 25.f, 45.f, 70.f, 110.f, 150.f}) grounds.push_back(vec2(s, v));
  auto base = [&](vec2 p) { float b[4]; world.sampleBase(p.x, p.y, b); return b[0]; };
  struct Site { float score; vec2 o, u; };
  const float step = 300.f;
  const int n = (int)(2.f * WORLD_HALF / step);
  std::vector<std::vector<Site>> rows(n);
  parallelFor(n, [&](int j) {
    std::vector<float> hs(nTrack), sm(nTrack);
    for (int i = 0; i < n; i++) {
      if ((i & 63) == 0) generationCheckpoint();
      const vec2 o(-WORLD_HALF + (i + 0.5f) * step, -WORLD_HALF + (j + 0.5f) * step);
      if (base(o) < 6.f || base(o) > 350.f) continue;
      for (int d = 0; d < 24; d++) {
        const vec2 u(cosf(d * 15.f * DEG), sinf(d * 15.f * DEG)), r(-u.y, u.x);
        auto at = [&](vec2 q) { return o + u * q.x + r * q.y; };
        float lo = 1e9f, hi = -1e9f; bool ok = true;
        for (int k = 0; k < nStraight && ok; k++) { const float h = base(at(track[k])); lo = std::min(lo, h); hi = std::max(hi, h); ok = hi - lo <= 4.f && h >= 6.f; }
        if (!ok) continue;
        const float straightH = 0.5f * (lo + hi), straightRange = hi - lo;
        for (int k = 0; k < nTrack && ok; k++) { hs[k] = base(at(track[k])); ok = hs[k] >= 5.f && hs[k] <= 400.f; lo = std::min(lo, hs[k]); hi = std::max(hi, hs[k]); }
        if (!ok || hi - lo > 35.f) continue;
        for (const vec2& g : grounds) {
          const float h = base(at(g));
          ok = ok && h >= 5.f && fabsf(h - straightH) <= (g.y > 60.f ? 8.f : 5.f);
        }
        if (!ok) continue;
        // (the circuit's profile smoothed over 180 m, round the loop: its grade, and the ground off it)
        float earth = 0.f, steep = 0.f;
        for (int k = 0; k < nTrack; k++) {
          float sum = 0.f; for (int q = -4; q <= 4; q++) sum += hs[(k + q + nTrack) % nTrack];
          sm[k] = sum / 9.f;
        }
        for (int k = 0; k < nTrack; k++) { earth += fabsf(hs[k] - sm[k]); steep = std::max(steep, fabsf(sm[(k + 1) % nTrack] - sm[k]) / 20.f); }
        if (steep > 0.05f) continue;
        float town = 1e9f;
        for (const Town& t : world.towns) if (t.kind >= 1) town = std::min(town, length(vec2(t.x, t.z) - at(vec2(400.f, 300.f))));
        rows[j].push_back({earth / nTrack + 0.05f * (hi - lo) + 0.5f * straightRange + town / 5000.f, o, u});
      }
    }
  });
  std::vector<Site> sites;
  for (auto& r : rows) sites.insert(sites.end(), r.begin(), r.end());
  std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) {
    return a.score != b.score ? a.score < b.score : a.o.x != b.o.x ? a.o.x < b.o.x : a.o.y != b.o.y ? a.o.y < b.o.y : atan2f(a.u.y, a.u.x) < atan2f(b.u.y, b.u.x); });
  RoadIndex idx;
  for (size_t pi = 0; pi < net.paths.size(); pi++) idx.addPath(net.paths[pi], (int)pi);
  int tried = 0;
  for (const Site& S : sites) {
    if (++tried > 300) break;
    const vec2 r(-S.u.y, S.u.x);
    auto at = [&](vec2 q) { return S.o + S.u * q.x + r * q.y; };
    bool ok = true;
    for (int k = 0; k < nTrack && ok; k++) {
      const vec2 p = at(track[k]);
      ok = settlementShare(fields, p.x, p.y) >= 1.3f && airportInfluence(world, p.x, p.y) <= 0.f && idx.nearest(p, 80.f, -1) > 1e8f &&
           world.groundHeight(p.x, p.y, 8) >= 4.f;
      for (const Airport& a : world.airports) ok = ok && hypotf(p.x - a.x, p.y - a.z) >= 3000.f;
    }
    for (size_t k = 0; k < grounds.size() && ok; k++) {
      const vec2 p = at(grounds[k]);
      ok = settlementShare(fields, p.x, p.y) >= 1.3f && airportInfluence(world, p.x, p.y) <= 0.f && idx.nearest(p, 40.f, -1) > 1e8f && world.groundHeight(p.x, p.y, 8) >= 4.f;
    }
    if (!ok) continue;
    // the circuit, on the ground: within 5 m of it all the way round (its run-off's banks no higher than a road's)
    std::vector<vec2> wl; for (const vec2& q : line) wl.push_back(at(q));
    const float h0 = world.groundHeight(S.o.x, S.o.y, 8);
    RoadPath circuit = layRoad(world, RC_CIRCUIT, wl, h0, h0, {});
    if (circuit.pts.size() < 100) continue;
    for (const RoadPoint& q : circuit.pts) ok = ok && fabsf(q.h - world.naturalHeight(q.x, q.z)) <= 5.f;
    if (!ok) continue;
    // its pit lane, out of the circuit 10 m past the start of the main straight and back in 120 m before turn 1, at
    // the circuit's height beside it
    RoadPath pit = layRoad(world, RC_PITLANE, {at(vec2(10.f, 0.f)), at(vec2(130.f, RACE_PITLANE_V)), at(vec2(640.f, RACE_PITLANE_V)), at(vec2(780.f, 0.f))}, h0, h0, {});
    auto straightH = [&](float s) {   // (the circuit's height along its main straight)
      for (size_t k = 0; k + 1 < circuit.pts.size(); k++) {
        const float s0 = dot2(vec2(circuit.pts[k].x, circuit.pts[k].z) - S.o, S.u), s1 = dot2(vec2(circuit.pts[k + 1].x, circuit.pts[k + 1].z) - S.o, S.u);
        if (s >= s0 && s <= s1) return circuit.pts[k].h + (circuit.pts[k + 1].h - circuit.pts[k].h) * (s - s0) / std::max(s1 - s0, 1e-3f);
      }
      return circuit.pts[0].h;
    };
    for (RoadPoint& q : pit.pts) q.h = straightH(dot2(vec2(q.x, q.z) - S.o, S.u));
    // the paddock's road behind the garages, and on from either end to the nearest highway, road or lane (not across
    // the circuit, nor along a bridge), the nearest first
    RoadIndex with = idx;
    with.addPath(circuit, (int)net.paths.size()); with.addPath(pit, (int)net.paths.size() + 1);
    RoadPath access; bool laid = false;
    for (int end = 0; end < 2 && !laid; end++) {
      const vec2 from = at(vec2(end ? 640.f : 170.f, RACE_PADDOCK_V)), other = at(vec2(end ? 170.f : 640.f, RACE_PADDOCK_V));
      std::vector<int> tried;
      for (int attempt = 0; attempt < 3 && !laid; attempt++) {
        float d = 1e9f; int sg = -1; float t = 0.f;
        const float reach = 2500.f;
        for (int jj = RoadIndex::cell(from.y - reach); jj <= RoadIndex::cell(from.y + reach); jj++)
          for (int ii = RoadIndex::cell(from.x - reach); ii <= RoadIndex::cell(from.x + reach); ii++) {
            auto it = idx.cells.find(RoadIndex::key(ii, jj)); if (it == idx.cells.end()) continue;
            for (int id : it->second) {
              const RoadIndex::Seg& sgm = idx.segs[id];
              if (sgm.cls > RC_LANE || std::find(tried.begin(), tried.end(), sgm.path) != tried.end()) continue;
              const vec2 e = sgm.b - sgm.a; const float uu = std::clamp(dot2(from - sgm.a, e) / std::max(dot2(e, e), 1e-6f), 0.f, 1.f);
              const float q = length(sgm.a + e * uu - from);
              if (q < d) { d = q; sg = id; t = uu; }
            }
          }
        if (d > reach) break;
        const RoadIndex::Seg road = idx.segs[sg];
        tried.push_back(road.path);
        const vec2 J = road.a + (road.b - road.a) * t;
        const RoadPath& P = net.paths[road.path];
        bool bridged = false;   // (not off a bridge)
        for (size_t k = 0; k + 1 < P.pts.size(); k++)
          if (P.bridge[k] && length(vec2(P.pts[k].x, P.pts[k].z) - J) + length(vec2(P.pts[k + 1].x, P.pts[k + 1].z) - J) < length(vec2(P.pts[k + 1].x - P.pts[k].x, P.pts[k + 1].z - P.pts[k].z)) + 60.f) bridged = true;
        if (bridged) continue;
        const vec2 dir = (J - from) * (1.f / std::max(length(J - from), 1e-3f)), side(-dir.y, dir.x);
        const float run = length(J - from);
        const std::vector<vec2> way = {other, from, from + dir * (run * 0.5f) + side * (run * 0.08f), J};
        std::vector<RoadPin> crossing;
        for (size_t k = 1; k + 1 < way.size(); k++) with.crossings(way[k], way[k + 1], -1, crossing);
        crossing.erase(std::remove_if(crossing.begin(), crossing.end(), [&](const RoadPin& c) { return hypotf(c.x - J.x, c.z - J.y) < 3.f; }), crossing.end());
        if (!crossing.empty()) continue;
        bool near = false;   // (40 m and more off the circuit once away from the paddock)
        for (int k = 0; k < nTrack; k++) {
          const vec2 p = at(track[k]);
          for (size_t w = 1; w + 1 < way.size(); w++) {
            const vec2 e = way[w + 1] - way[w]; const float uu = std::clamp(dot2(p - way[w], e) / std::max(dot2(e, e), 1e-6f), 0.f, 1.f);
            near = near || length(way[w] + e * uu - p) < 56.f;
          }
        }
        if (near) continue;
        laid = layPath(world, with, net, RC_ROAD, way, &access);
      }
    }
    if (!laid) continue;
    net.paths.push_back(std::move(circuit)); net.paths.push_back(std::move(pit)); net.paths.push_back(std::move(access));
    return;
  }
}
