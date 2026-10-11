// Solace Express - the islands' road network (road_network.h)
#include "road_network.h"
#include "world.h"
#include "scenery.h"
#include "airport_layout.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <numeric>
#include <functional>

void airportGroundsCap(float x, float z, float& grounds, float& cap);   // world.cpp: an airfield's grounds, its funnels' cap
float airportFunnelCeiling(float x, float z);                           // world.cpp: how high a road may be built there

namespace {

// ---------------------------------------------------------------- the routing grid
// 50 m cells over the whole map: the ground (6 octaves, the shape a road follows, not every bump), which island, and
// whether an airfield's grounds keep roads out. (Its approach funnels don't: a road may pass under an approach, graded
// no higher than the funnel holds the ground - see profile)
const float G = 50.f;
const int GN = (int)(2.f * WORLD_HALF / G);   // 1600

struct Grid {
  std::vector<float> h;          // ground height at the cell centre
  std::vector<float> slope;      // the ground's steepest slope there (rise over run)
  std::vector<uint8_t> blocked;  // 1: an airfield's grounds (only its own access road crosses them), 2: a runway's strip
  std::vector<int> island;       // land: the island's index; sea: -1
  int islands = 0;
  static int idx(int i, int j) { return j * GN + i; }
  static float cx(int i) { return -WORLD_HALF + (i + 0.5f) * G; }
  static int cell(float v) { return std::clamp((int)floorf((v + WORLD_HALF) / G), 0, GN - 1); }
};

Grid makeGrid(const World& world) {
  Grid g;
  g.h.resize((size_t)GN * GN); g.slope.resize(g.h.size()); g.blocked.resize(g.h.size()); g.island.assign(g.h.size(), -1);
  parallelFor(GN, [&](int j) {
    for (int i = 0; i < GN; i++) {
      const float x = Grid::cx(i), z = Grid::cx(j);
      g.h[Grid::idx(i, j)] = world.groundHeight(x, z, 6);
      // (an airfield's grounds - world.cpp airportLimits - end 550 m beyond a rectangle 260 m past its runway's ends and
      // at most 420 m to either side of it: farther from every runway's centre than that rectangle's corner, nothing to ask)
      bool near = false;
      for (const Airport& a : world.airports) near = near || hypotf(x - a.x, z - a.z) < hypotf(a.length * 0.5f + 820.f, 980.f);
      if (!near) continue;
      float grounds, cap; airportGroundsCap(x, z, grounds, cap);
      g.blocked[Grid::idx(i, j)] = grounds > 0.001f;
      for (const Airport& a : world.airports) {
        const vec2 q = aptLocal(a, vec3(x, 0, z));
        if (fabsf(q.x) < a.length * 0.5f + 300.f && fabsf(q.y) < a.width * 0.5f + 100.f) g.blocked[Grid::idx(i, j)] = 2;
      }
    }
  });
  parallelFor(GN, [&](int j) {
    for (int i = 0; i < GN; i++) {
      auto H = [&](int a, int b) { return g.h[Grid::idx(std::clamp(a, 0, GN - 1), std::clamp(b, 0, GN - 1))]; };
      const float dx = (H(i + 1, j) - H(i - 1, j)) / (2 * G), dz = (H(i, j + 1) - H(i, j - 1)) / (2 * G);
      g.slope[Grid::idx(i, j)] = sqrtf(dx * dx + dz * dz);
    }
  });
  // islands: land cells joined edge to edge (the shallows between two islands are not land)
  std::vector<int> stack;
  for (int s = 0; s < GN * GN; s++) {
    if (g.h[s] < 0.5f || g.island[s] >= 0) continue;
    const int id = g.islands++;
    stack.push_back(s); g.island[s] = id;
    while (!stack.empty()) {
      const int c = stack.back(); stack.pop_back();
      const int ci = c % GN, cj = c / GN;
      const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto& d : nb) {
        const int ni = ci + d[0], nj = cj + d[1];
        if (ni < 0 || nj < 0 || ni >= GN || nj >= GN) continue;
        const int n = Grid::idx(ni, nj);
        if (g.h[n] < 0.5f || g.island[n] >= 0) continue;
        g.island[n] = id; stack.push_back(n);
      }
    }
  }
  return g;
}

// ---------------------------------------------------------------- A* over the grid
// 16 directions: the eight neighbours and the knight's moves, so a route can run at 27 and 63 degrees without a zigzag
const int kDirs[16][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
                          {2, 1}, {2, -1}, {-2, 1}, {-2, -1}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};

struct Router {
  const Grid& g;
  std::vector<float> cost;   // best cost found to the cell
  std::vector<int> from;     // the cell it came from
  std::vector<uint32_t> seen;   // the search that last touched the cell (no clearing between searches)
  std::vector<uint8_t> roadCell;   // cells an existing road passes through (a new route reuses them cheaply)
  uint32_t search = 0;
  bool access = false;       // routing an airfield's access road: out across its grounds (dearly), never a runway's strip
  bool passable(int n) const { return g.blocked[n] == 0 || (access && g.blocked[n] == 1); }
  explicit Router(const Grid& grid) : g(grid), cost(grid.h.size()), from(grid.h.size()), seen(grid.h.size(), 0), roadCell(grid.h.size(), 0) {}

  // the cost of one step a -> b for a road of class c (infinite: not allowed)
  float step(int a, int b, float len, int c) const {
    if (!passable(b)) return INFINITY;
    const RoadSpec& s = roadSpec(c);
    const float hb = g.h[b], ha = g.h[a];
    float k = g.blocked[b] ? 3.f : 1.f;
    if (hb < 0.5f) {   // over the water: a bridge (the deep sea, and the small roads: never)
      if (c >= RC_LANE || hb < -22.f) return INFINITY;
      k *= 12.f;
    } else {
      const float grade = fabsf(hb - std::max(ha, 0.5f)) / len, gm = s.maxGrade;
      if (grade > 2.5f * gm) return INFINITY;
      k *= grade > gm ? 1.f + 30.f * (grade - gm) / gm : 1.f + 0.6f * (grade / gm) * (grade / gm);
      k *= 1.f + 3.f * std::max(0.f, g.slope[b] - 0.08f);   // a steep hillside: cut and fill (the valley floor instead)
    }
    if (roadCell[b]) k *= 0.55f;
    return len * k;
  }

  // from a cell to the cheapest of the goal cells (a set: any cell with goal[] set), within the window; the path from
  // the start, cell by cell, or empty when there is none
  std::vector<int> route(int start, const std::vector<uint8_t>& goal, vec2 target, int c, int i0, int j0, int i1, int j1) {
    search++;
    struct QE { float f, g; int n; bool operator>(const QE& o) const { return f > o.f; } };
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto H = [&](int n) { return target.x > -1e8f ? 0.55f * hypotf(Grid::cx(n % GN) - target.x, Grid::cx(n / GN) - target.y) : 0.f; };
    cost[start] = 0; from[start] = -1; seen[start] = search;
    open.push({H(start), 0.f, start});
    while (!open.empty()) {
      const QE e = open.top(); open.pop();
      const int c0 = e.n;
      if (e.g > cost[c0]) continue;   // (a stale entry: the cell was reached more cheaply since; its own cost, exactly)
      if (goal[c0]) {
        std::vector<int> path;
        for (int n = c0; n >= 0; n = from[n]) path.push_back(n);
        std::reverse(path.begin(), path.end());
        return path;
      }
      const int ci = c0 % GN, cj = c0 / GN;
      for (auto& d : kDirs) {
        const int ni = ci + d[0], nj = cj + d[1];
        if (ni < i0 || nj < j0 || ni > i1 || nj > j1) continue;
        if (abs(d[0]) + abs(d[1]) == 3) {   // a knight's move must not jump a blocked or watery corner
          const int mi = ci + d[0] / 2 * (abs(d[0]) == 2), mj = cj + d[1] / 2 * (abs(d[1]) == 2);
          if (!passable(Grid::idx(mi, mj))) continue;
        }
        const int n = Grid::idx(ni, nj);
        const float len = G * sqrtf((float)(d[0] * d[0] + d[1] * d[1]));
        const float sc = step(c0, n, len, c);
        if (!std::isfinite(sc)) continue;
        const float nc = cost[c0] + sc;
        if (seen[n] == search && nc >= cost[n]) continue;
        seen[n] = search; cost[n] = nc; from[n] = c0;
        open.push({nc + H(n), nc, n});
      }
    }
    return {};
  }
};

// ---------------------------------------------------------------- shaping a route
float lineCost(const Router& r, vec2 a, vec2 b, int c) {
  const float L = length(b - a);
  const int n = std::max(1, (int)ceilf(L / (G * 0.5f)));
  float sum = 0;
  int prev = Grid::idx(Grid::cell(a.x), Grid::cell(a.y));
  for (int k = 1; k <= n; k++) {
    const vec2 p = a + (b - a) * ((float)k / n);
    const int cur = Grid::idx(Grid::cell(p.x), Grid::cell(p.y));
    const float s = r.step(prev, cur, L / n, c);
    if (!std::isfinite(s)) return INFINITY;
    sum += s; prev = cur;
  }
  return sum;
}

// the cell path as straight legs: from each corner, as far ahead as a straight leg costs no more than the cells it
// replaces (and crosses nothing the route avoided)
std::vector<vec2> straighten(const Router& r, const std::vector<int>& cells, int c) {
  std::vector<vec2> p(cells.size());
  for (size_t k = 0; k < cells.size(); k++) p[k] = vec2(Grid::cx(cells[k] % GN), Grid::cx(cells[k] / GN));
  std::vector<float> acc(p.size(), 0.f);   // the route's own cost up to each cell
  for (size_t k = 1; k < p.size(); k++) acc[k] = acc[k - 1] + r.step(cells[k - 1], cells[k], length(p[k] - p[k - 1]), c);
  std::vector<vec2> out{p[0]};
  size_t i = 0;
  while (i + 1 < p.size()) {
    size_t best = i + 1;
    for (size_t j = std::min(p.size() - 1, i + 400); j > i + 1; j--) {
      const float lc = lineCost(r, p[i], p[j], c);
      if (std::isfinite(lc) && lc <= (acc[j] - acc[i]) * 1.03f + G) { best = j; break; }
    }
    out.push_back(p[best]); i = best;
  }
  return out;
}

// corners rounded with arcs of at least the class's radius where the legs allow it
std::vector<vec2> fillet(const std::vector<vec2>& p, float R) {
  if (p.size() < 3) return p;
  std::vector<vec2> out{p[0]};
  for (size_t k = 1; k + 1 < p.size(); k++) {
    const vec2 a = p[k - 1], b = p[k], c = p[k + 1];
    vec2 u = b - a, v = c - b;
    const float lu = length(u), lv = length(v);
    if (lu < 1e-3f || lv < 1e-3f) continue;
    u = u * (1.f / lu); v = v * (1.f / lv);
    const float cosT = std::clamp(u.x * v.x + u.y * v.y, -1.f, 1.f), theta = acosf(cosT);   // the turn
    if (theta < 0.02f) { out.push_back(b); continue; }
    // the tangent length the radius needs, held to under half of each leg (a tighter arc where the legs are short)
    float t = R * tanf(theta * 0.5f);
    t = std::min(t, 0.48f * std::min(lu, lv));
    const float r = t / tanf(theta * 0.5f);
    const vec2 s = b - u * t, e = b + v * t;
    const float side = u.x * v.y - u.y * v.x > 0 ? 1.f : -1.f;
    const vec2 nrm(-u.y * side, u.x * side), centre = s + nrm * r;
    const int steps = std::max(2, (int)ceilf(theta * r / 12.f));
    const float a0 = atan2f(s.y - centre.y, s.x - centre.x);
    for (int q = 0; q <= steps; q++) {
      const float a = a0 + side * theta * q / steps;
      out.push_back(centre + vec2(cosf(a), sinf(a)) * r);
    }
  }
  out.push_back(p.back());
  return out;
}

std::vector<vec2> resample(const std::vector<vec2>& p, float spacing) {
  std::vector<float> s(p.size(), 0.f);
  for (size_t k = 1; k < p.size(); k++) s[k] = s[k - 1] + length(p[k] - p[k - 1]);
  const float L = s.back();
  const int n = std::max(1, (int)roundf(L / spacing));
  std::vector<vec2> out;
  size_t seg = 0;
  for (int q = 0; q <= n; q++) {
    const float at = L * q / n;
    while (seg + 1 < s.size() - 1 && s[seg + 1] < at) seg++;
    const float span = std::max(s[seg + 1] - s[seg], 1e-4f), f = std::clamp((at - s[seg]) / span, 0.f, 1.f);
    out.push_back(p[seg] + (p[seg + 1] - p[seg]) * f);
  }
  return out;
}

// the graded profile: the ground smoothed along the road, never steeper than the class allows; over water at least the
// deck clearance. Bridges: over water, or where the road runs well above the ground for long enough
void profile(const World& world, RoadPath& path, float h0, float h1) {
  const RoadSpec& s = roadSpec(path.cls);
  const int n = (int)path.pts.size();
  std::vector<float> ground(n), ds(n, 0.f);
  for (int k = 0; k < n; k++) ground[k] = world.groundHeight(path.pts[k].x, path.pts[k].z, 8);
  for (int k = 1; k < n; k++) ds[k] = hypotf(path.pts[k].x - path.pts[k - 1].x, path.pts[k].z - path.pts[k - 1].z);
  const float spacing = s.spacing, window = path.cls == RC_HIGHWAY ? 300.f : path.cls == RC_ROAD ? 140.f : 70.f;
  const int w = std::max(1, (int)roundf(window / spacing * 0.5f));
  const float deck = 9.f;   // a bridge's deck over the sea
  // the steepest the profile may climb: the class's grade, a lane's or a track's pitching steeper for a stretch (the
  // routes are found at the class's grade, but a hillside falling faster than that left them hanging in the air)
  const float steepest = path.cls >= RC_LANE ? s.maxGrade * 1.6f : s.maxGrade;
  std::vector<float> target(n);
  for (int k = 0; k < n; k++) {
    float sum = 0, wsum = 0;
    for (int q = std::max(0, k - w); q <= std::min(n - 1, k + w); q++) {
      const float wt = 1.f - fabsf((float)(q - k)) / (w + 1);
      sum += std::max(ground[q], deck) * wt * (ground[q] < 0.5f) + ground[q] * wt * (ground[q] >= 0.5f); wsum += wt;
    }
    target[k] = sum / wsum;
    if (ground[k] < 0.5f) target[k] = std::max(target[k], deck);
  }
  target[0] = h0; target[n - 1] = h1;
  // under an approach funnel never above what it holds the ground to (a metre's margin, the banks blending down to
  // ground that is itself no higher; nor below the ground there, should that stand higher)
  std::vector<float> ceiling(n);
  for (int k = 0; k < n; k++) ceiling[k] = std::max(airportFunnelCeiling(path.pts[k].x, path.pts[k].z) - 1.f, ground[k]);
  // the funnels' ceiling and the bridges' deck clearance, then the grade limit forward and back until it settles (the
  // ends held where the road meets a town or another road); the grade has the last word
  std::vector<float> h = target;
  for (int it = 0; it < 6; it++) {
    for (int k = 0; k < n; k++) h[k] = std::min(ground[k] < 0.5f ? std::max(h[k], deck) : h[k], ceiling[k]);
    h[0] = h0;
    for (int k = 1; k < n; k++) h[k] = std::clamp(h[k], h[k - 1] - steepest * ds[k], h[k - 1] + steepest * ds[k]);
    h[n - 1] = h1;
    for (int k = n - 2; k >= 0; k--) h[k] = std::clamp(h[k], h[k + 1] - steepest * ds[k + 1], h[k + 1] + steepest * ds[k + 1]);
  }
  // ends too far apart in height for the road's length at its class's grade (a mountain strip's access road, say):
  // what the start is still short of spread along the whole road, a steadily steeper climb rather than a cliff
  float total = 0; for (int k = 1; k < n; k++) total += ds[k];
  const float r = h0 - h[0];
  float sk = 0;
  for (int k = 0; k < n; k++) { if (k) sk += ds[k]; h[k] += r * (1.f - sk / std::max(total, 1e-3f)); }
  for (int k = 0; k < n; k++) path.pts[k].h = h[k];
  // across a steep hillside (the ground either side of the road differing by more than a third of the way between)
  // a road standing clear of it is a shelf, graded into the slope; a bridge only where it crosses something - a ravine,
  // a gully - with the ground falling away on both sides
  std::vector<uint8_t> hillside(n, 0);
  for (int k = 0; k < n; k++) {
    const int a = std::max(k - 1, 0), b = std::min(k + 1, n - 1);
    float dx = path.pts[b].x - path.pts[a].x, dz = path.pts[b].z - path.pts[a].z; const float l = std::max(hypotf(dx, dz), 1e-3f);
    const float off = s.halfPlatform + 20.f, px = -dz / l * off, pz = dx / l * off;
    const float gl = world.groundHeight(path.pts[k].x + px, path.pts[k].z + pz, 8), gr = world.groundHeight(path.pts[k].x - px, path.pts[k].z - pz, 8);
    hillside[k] = fabsf(gl - gr) > 0.66f * off;
  }
  path.bridge.assign(n - 1, 0);
  for (int k = 0; k + 1 < n; k++) {
    const bool water = ground[k] < 0.5f || ground[k + 1] < 0.5f;
    const bool high = h[k] - ground[k] > 9.f && h[k + 1] - ground[k + 1] > 9.f && !(hillside[k] && hillside[k + 1]);
    path.bridge[k] = water || high;
  }
  // (a span shorter than three segments over dry ground is an embankment, not a bridge)
  for (int k = 0; k + 1 < n; ) {
    if (!path.bridge[k]) { k++; continue; }
    int e = k; bool wet = false;
    while (e + 1 < n && path.bridge[e]) { wet = wet || ground[e] < 0.5f || ground[e + 1] < 0.5f; e++; }
    if (!wet && e - k < 3) for (int q = k; q < e; q++) path.bridge[q] = 0;
    k = e;
  }
}

}  // namespace

RoadNetwork buildRoadNetwork(const World& world) {
  RoadNetwork net;
  const Grid g = makeGrid(world);
  Router router(g);
  // ---- the nodes: every settlement's centre, every airfield's landside gate
  for (int t = 0; t < kNumTowns; t++) {
    const int c = Grid::idx(Grid::cell(kTowns[t].x), Grid::cell(kTowns[t].z));
    net.nodes.push_back({kTowns[t].x, kTowns[t].z, kTowns[t].kind, g.island[c]});
  }
  for (size_t ai = 0; ai < world.airports.size(); ai++) {
    const Airport& a = world.airports[ai]; const AptLayout L = aptLayout(a, (int)ai);
    const float u = L.termU + (L.paved ? 0.f : 80.f), v = L.paved ? L.lotV1 + 18.f : L.bldV + 40.f;
    // the landside gate, past the car park (the access road starts there and finds its way out across the grounds),
    // clear of the runway's protected strip
    float out = v;
    while (fabsf(out) < a.width * 0.5f + 110.f) out += 10.f;
    const vec3 gate = aptWorld(a, u, L.side * out, 0);
    const int c = Grid::idx(Grid::cell(gate.x), Grid::cell(gate.z));
    net.nodes.push_back({gate.x, gate.z, 3, g.island[c]});
  }
  const int N = (int)net.nodes.size();
  auto cellOf = [&](const RoadNode& n) { return Grid::idx(Grid::cell(n.x), Grid::cell(n.z)); };
  // ---- the links. Highways: per island, the towns and cities joined by a minimum spanning tree, and every city to
  // every other. Roads: per island, every settlement joined by a minimum spanning tree (the highways already count),
  // then the pairs whose way round the network is much longer than the way across. Between islands: a bridge where a
  // strait is short and shallow enough
  struct Link { int a, b; float d; RoadClass cls; };
  std::vector<Link> links;
  std::vector<int> comp(N); std::iota(comp.begin(), comp.end(), 0);
  std::function<int(int)> root = [&](int x) { return comp[x] == x ? x : comp[x] = root(comp[x]); };
  std::vector<std::vector<std::pair<int, float>>> adj(N);
  auto join = [&](const Link& l) { comp[root(l.a)] = root(l.b); links.push_back(l); adj[l.a].push_back({l.b, l.d}); adj[l.b].push_back({l.a, l.d}); };
  auto pairsOn = [&](int isl, int minKind) {
    std::vector<Link> cand;
    for (int p = 0; p < kNumTowns; p++) for (int q = p + 1; q < kNumTowns; q++) {
      const RoadNode &A = net.nodes[p], &B = net.nodes[q];
      if (A.island != isl || B.island != isl || A.kind < minKind || B.kind < minKind) continue;
      cand.push_back({p, q, hypotf(A.x - B.x, A.z - B.z), minKind >= 1 ? RC_HIGHWAY : RC_ROAD});
    }
    std::sort(cand.begin(), cand.end(), [](const Link& x, const Link& y) { return x.d < y.d; });
    return cand;
  };
  for (int isl = 0; isl < g.islands; isl++) {
    for (const Link& l : pairsOn(isl, 1))
      if (root(l.a) != root(l.b) || (net.nodes[l.a].kind == 2 && net.nodes[l.b].kind == 2)) join(l);
    const std::vector<Link> cand = pairsOn(isl, 0);
    for (const Link& l : cand) if (root(l.a) != root(l.b)) join(l);
    for (const Link& l : cand) {
      if (l.d > 16000.f) break;
      std::vector<float> dist(N, INFINITY); dist[l.a] = 0;
      using QE = std::pair<float, int>;
      std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q; q.push({0.f, l.a});
      while (!q.empty()) { auto [d, x] = q.top(); q.pop(); if (d > dist[x]) continue; for (auto [y, w] : adj[x]) if (d + w < dist[y]) { dist[y] = d + w; q.push({dist[y], y}); } }
      if (dist[l.b] > 1.7f * l.d) join(l);
    }
  }
  std::vector<Link> straits;   // settlements on different islands, nearest first (tried after the islands' own roads)
  for (int p = 0; p < kNumTowns; p++) for (int q = p + 1; q < kNumTowns; q++) {
    const RoadNode &A = net.nodes[p], &B = net.nodes[q];
    const float d = hypotf(A.x - B.x, A.z - B.z);
    if (A.island >= 0 && B.island >= 0 && A.island != B.island && d < 14000.f) straits.push_back({p, q, d, RC_ROAD});
  }
  std::sort(straits.begin(), straits.end(), [](const Link& x, const Link& y) { return x.d < y.d; });
  // highways first (the network the rest joins), then the roads, each routed over the grid
  std::stable_sort(links.begin(), links.end(), [](const Link& x, const Link& y) { return x.cls < y.cls; });
  std::vector<uint8_t> goal(g.h.size(), 0);
  auto mark = [&](const RoadPath& p) {
    for (size_t k = 0; k + 1 < p.pts.size(); k++) {
      const vec2 a(p.pts[k].x, p.pts[k].z), b(p.pts[k + 1].x, p.pts[k + 1].z);
      const int steps = std::max(1, (int)ceilf(length(b - a) / (G * 0.5f)));
      for (int q = 0; q <= steps; q++) { const vec2 c = a + (b - a) * ((float)q / steps); router.roadCell[Grid::idx(Grid::cell(c.x), Grid::cell(c.y))] = 1; }
    }
  };
  auto window = [&](vec2 a, vec2 b, float margin, int& i0, int& j0, int& i1, int& j1) {
    i0 = Grid::cell(std::min(a.x, b.x) - margin); i1 = Grid::cell(std::max(a.x, b.x) + margin);
    j0 = Grid::cell(std::min(a.y, b.y) - margin); j1 = Grid::cell(std::max(a.y, b.y) + margin);
  };
  // a path along the cells from a to b (b's height given: where it meets another road), shaped and graded; how much of
  // it crosses water (m)
  auto shape = [&](const std::vector<int>& cells, RoadClass cls, int from, int to, vec2 a, vec2 b, float hb, float& water) {
    RoadPath path; path.cls = cls; path.from = from; path.to = to;
    std::vector<vec2> legs = straighten(router, cells, cls);
    legs.front() = a; legs.back() = b;
    const std::vector<vec2> pts = resample(fillet(legs, roadSpec(cls).minRadius), roadSpec(cls).spacing);
    for (const vec2& p : pts) path.pts.push_back({p.x, p.y, 0.f});
    water = 0;
    if (path.pts.size() < 2) return path;
    profile(world, path, world.groundHeight(a.x, a.y, 8), hb);
    for (size_t k = 0; k + 1 < path.pts.size(); k++)
      if (world.groundHeight(path.pts[k].x, path.pts[k].z, 6) < 0.5f) water += hypotf(path.pts[k + 1].x - path.pts[k].x, path.pts[k + 1].z - path.pts[k].z);
    return path;
  };
  auto link = [&](const Link& l, float maxWater) {
    const RoadNode &A = net.nodes[l.a], &B = net.nodes[l.b];
    int i0, j0, i1, j1; window(vec2(A.x, A.z), vec2(B.x, B.z), 0.35f * l.d + 3000.f, i0, j0, i1, j1);
    std::fill(goal.begin(), goal.end(), 0); goal[cellOf(B)] = 1;
    const std::vector<int> cells = router.route(cellOf(A), goal, vec2(B.x, B.z), l.cls, i0, j0, i1, j1);
    if (cells.size() < 2) return false;
    float water;
    RoadPath path = shape(cells, l.cls, l.a, l.b, vec2(A.x, A.z), vec2(B.x, B.z), world.groundHeight(B.x, B.z, 8), water);
    if (path.pts.size() < 2 || water > maxWater) return false;
    mark(path); net.paths.push_back(std::move(path));
    return true;
  };
  for (const Link& l : links) link(l, 2500.f);
  // straits: one bridge per pair of islands, the shortest that routes with under 3.5 km of water
  std::vector<int> isl(g.islands); std::iota(isl.begin(), isl.end(), 0);
  std::function<int(int)> islRoot = [&](int x) { return isl[x] == x ? x : isl[x] = islRoot(isl[x]); };
  for (const Link& l : straits) {
    const int ia = islRoot(net.nodes[l.a].island), ib = islRoot(net.nodes[l.b].island);
    if (ia == ib) continue;
    if (link(l, 3500.f)) isl[ia] = ib;
  }
  // ---- every airfield's gate (and any settlement still alone) to the nearest road or settlement on its island: a
  // search outward until it reaches one
  for (int i = 0; i < N; i++) {
    const RoadNode& A = net.nodes[i];
    if (A.island < 0) continue;
    bool joined = false;
    for (const RoadPath& p : net.paths) joined = joined || p.from == i || p.to == i;
    if (joined) continue;
    goal.assign(g.h.size(), 0);
    bool any = false;
    for (size_t c = 0; c < goal.size(); c++) if (router.roadCell[c] && g.island[c] == A.island) { goal[c] = 1; any = true; }
    std::vector<int> townCell(kNumTowns, -1);
    for (int t = 0; t < kNumTowns; t++) if (t != i && net.nodes[t].island == A.island) { townCell[t] = cellOf(net.nodes[t]); goal[townCell[t]] = 1; any = true; }
    if (!any) continue;
    int i0, j0, i1, j1; window(vec2(A.x, A.z), vec2(A.x, A.z), 9000.f, i0, j0, i1, j1);
    router.access = A.kind == 3;
    // (a strip's access road: a country lane, narrower and allowed steeper - the mountain strips' climb)
    const RoadClass cls = A.kind == 3 && world.airports[i - kNumTowns].size == 0 ? RC_LANE : RC_ROAD;
    const std::vector<int> cells = router.route(cellOf(A), goal, vec2(-1e9f, -1e9f), cls, i0, j0, i1, j1);
    if (cells.size() < 2) { router.access = false; continue; }
    // the far end: a settlement it reached, or the nearest point of the road it reached, at that road's height
    vec2 end(Grid::cx(cells.back() % GN), Grid::cx(cells.back() / GN));
    float hEnd = world.groundHeight(end.x, end.y, 8); int to = -1;
    for (int t = 0; t < kNumTowns; t++) if (townCell[t] == cells.back()) { end = vec2(net.nodes[t].x, net.nodes[t].z); hEnd = world.groundHeight(end.x, end.y, 8); to = t; }
    if (to < 0) {
      const vec2 reached = end;
      float bd = 1e9f;
      for (const RoadPath& p : net.paths) for (const RoadPoint& q : p.pts) {
        const float d = hypotf(q.x - reached.x, q.z - reached.y);
        if (d < bd) { bd = d; end = vec2(q.x, q.z); hEnd = q.h; }
      }
      if (bd > 200.f) { router.access = false; continue; }
    }
    float water;
    RoadPath path = shape(cells, cls, i, to, vec2(A.x, A.z), end, hEnd, water);   // (straightened as it was routed)
    router.access = false;
    if (path.pts.size() < 2 || water > 600.f) continue;
    mark(path); net.paths.push_back(std::move(path));
  }
  return net;
}

// ---------------------------------------------------------------- the grid the terrain and the material read
int RoadGrid::cell(float v) { return std::clamp((int)floorf((v + WORLD_HALF) / (2.f * WORLD_HALF / N)), 0, N - 1); }

namespace {
// a path's points with those within tol of the straight line between their neighbours dropped (Douglas-Peucker, the
// height included at a tenth of the tolerance): long straight roads become few long segments
void simplify(const RoadPath& p, int a, int b, float tol, std::vector<uint8_t>& keep) {
  if (b - a < 2) return;
  const RoadPoint &A = p.pts[a], &B = p.pts[b];
  const float dx = B.x - A.x, dz = B.z - A.z, L2 = std::max(dx * dx + dz * dz, 1e-6f);
  float worst = 0; int at = -1;
  for (int k = a + 1; k < b; k++) {
    const RoadPoint& P = p.pts[k];
    const float t = std::clamp(((P.x - A.x) * dx + (P.z - A.z) * dz) / L2, 0.f, 1.f);
    const float e = std::max(hypotf(A.x + dx * t - P.x, A.z + dz * t - P.z), fabsf(A.h + (B.h - A.h) * t - P.h) * 10.f);
    if (e > worst) { worst = e; at = k; }
  }
  if (worst <= tol) return;
  keep[at] = 1;
  simplify(p, a, at, tol, keep); simplify(p, at, b, tol, keep);
}
}  // namespace

void buildRoadGrid(const RoadNetwork& net, RoadGrid& grid) {
  grid.segs.clear(); grid.list.clear(); grid.head.assign((size_t)RoadGrid::N * RoadGrid::N, 0);
  for (size_t pi = 0; pi < net.paths.size(); pi++) {
    const RoadPath& p = net.paths[pi];
    const int n = (int)p.pts.size();
    if (n < 2) continue;
    const RoadSpec& s = roadSpec(p.cls);
    // each run of bridge or road kept whole (its ends kept), the points along it simplified
    std::vector<uint8_t> keep(n, 0); keep[0] = keep[n - 1] = 1;
    for (int k = 1; k + 1 < n; k++) if (p.bridge[k - 1] != p.bridge[k]) keep[k] = 1;
    for (int a = 0; a < n - 1; ) { int b = a + 1; while (!keep[b]) b++; simplify(p, a, b, 0.15f, keep); a = b; }
    float along = 0;
    int a = 0;
    for (int b = 1; b < n; b++) {
      if (!keep[b]) continue;
      const RoadPoint &A = p.pts[a], &B = p.pts[b];
      RoadSegment seg{A.x, A.z, B.x, B.z, A.h, B.h, along, (uint16_t)pi, (uint8_t)p.cls, (uint8_t)(p.bridge[a] ? RS_BRIDGE : 0)};
      // by an airfield's grounds: painted on the ground as it is (the airfield's terrain is never reshaped)
      const float reach = s.halfPlatform + ROAD_BANK_MAX;
      const float dx = B.x - A.x, dz = B.z - A.z, L = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / L, nz = dx / L;
      for (int q = 0; q <= 4 && !(seg.flags & RS_NOGRADE); q++)
        for (int side = -2; side <= 2; side++) {
          float grounds, cap; airportGroundsCap(A.x + dx * q / 4 + nx * reach * side / 2, A.z + dz * q / 4 + nz * reach * side / 2, grounds, cap);
          if (grounds > 0.f) { seg.flags |= RS_NOGRADE; break; }
        }
      for (int k = a; k < b; k++) along += hypotf(p.pts[k + 1].x - p.pts[k].x, p.pts[k + 1].z - p.pts[k].z);
      grid.segs.push_back(seg);
      a = b;
    }
  }
  // the texels each segment's reach (its platform and widest banks) overlaps: a capsule against each texel's square
  const float T = 2.f * WORLD_HALF / RoadGrid::N;
  std::vector<std::vector<uint32_t>> at((size_t)RoadGrid::N * RoadGrid::N);
  for (size_t si = 0; si < grid.segs.size(); si++) {
    const RoadSegment& s = grid.segs[si];
    const float R = roadSpec(s.cls).halfPlatform + ROAD_BANK_MAX;
    const int i0 = RoadGrid::cell(std::min(s.ax, s.bx) - R), i1 = RoadGrid::cell(std::max(s.ax, s.bx) + R);
    const int j0 = RoadGrid::cell(std::min(s.az, s.bz) - R), j1 = RoadGrid::cell(std::max(s.az, s.bz) + R);
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L2 = std::max(dx * dx + dz * dz, 1e-6f);
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
      // nearest point of the segment to the texel's centre, then the texel's half-diagonal as slack
      const float cx = -WORLD_HALF + (i + 0.5f) * T, cz = -WORLD_HALF + (j + 0.5f) * T;
      const float t = std::clamp(((cx - s.ax) * dx + (cz - s.az) * dz) / L2, 0.f, 1.f);
      if (hypotf(s.ax + dx * t - cx, s.az + dz * t - cz) > R + T * 0.7072f) continue;
      at[(size_t)j * RoadGrid::N + i].push_back((uint32_t)si);
    }
  }
  for (size_t c = 0; c < at.size(); c++) {
    if (at[c].empty()) continue;
    const size_t n = std::min<size_t>(at[c].size(), 63);   // (never more than a few dozen: junctions of three roads)
    if (grid.list.size() + n > 0x7FFFFu) break;            // (the 19 bits a texel's first entry has: 2.6 times today's)
    grid.head[c] = (uint32_t)n << 19 | (uint32_t)grid.list.size();
    grid.list.insert(grid.list.end(), at[c].begin(), at[c].begin() + n);
  }
  // the coarse index
  const float CS = 2.f * WORLD_HALF / RoadGrid::NC, reach = 500.f;
  std::vector<std::vector<uint32_t>> near((size_t)RoadGrid::NC * RoadGrid::NC);
  auto ccell = [&](float v) { return std::clamp((int)floorf((v + WORLD_HALF) / CS), 0, RoadGrid::NC - 1); };
  for (size_t si = 0; si < grid.segs.size(); si++) {
    const RoadSegment& s = grid.segs[si];
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L2 = std::max(dx * dx + dz * dz, 1e-6f);
    for (int j = ccell(std::min(s.az, s.bz) - reach); j <= ccell(std::max(s.az, s.bz) + reach); j++)
      for (int i = ccell(std::min(s.ax, s.bx) - reach); i <= ccell(std::max(s.ax, s.bx) + reach); i++) {
        const float cx = -WORLD_HALF + (i + 0.5f) * CS, cz = -WORLD_HALF + (j + 0.5f) * CS;
        const float t = std::clamp(((cx - s.ax) * dx + (cz - s.az) * dz) / L2, 0.f, 1.f);
        if (hypotf(s.ax + dx * t - cx, s.az + dz * t - cz) > reach + CS * 0.7072f) continue;
        near[(size_t)j * RoadGrid::NC + i].push_back((uint32_t)si);
      }
  }
  grid.nearHead.assign(near.size(), 0); grid.nearList.clear();
  for (size_t c = 0; c < near.size(); c++) {
    if (near[c].empty()) continue;
    // (a cell with more than 255 segments within 500 m keeps the nearest to its centre)
    if (near[c].size() > 255) {
      const float cx = -WORLD_HALF + (c % RoadGrid::NC + 0.5f) * CS, cz = -WORLD_HALF + (c / RoadGrid::NC + 0.5f) * CS;
      auto dist = [&](uint32_t si) { const RoadSegment& s = grid.segs[si]; return hypotf((s.ax + s.bx) * 0.5f - cx, (s.az + s.bz) * 0.5f - cz); };
      std::sort(near[c].begin(), near[c].end(), [&](uint32_t a, uint32_t b) { return dist(a) < dist(b); });
      near[c].resize(255);
    }
    grid.nearHead[c] = (uint32_t)grid.nearList.size() << 8 | (uint32_t)near[c].size();
    grid.nearList.insert(grid.nearList.end(), near[c].begin(), near[c].end());
  }
}

float roadGrade(const RoadGrid& grid, float x, float z, float g) {
  if (grid.head.empty()) return g;
  const uint32_t h = grid.head[(size_t)RoadGrid::cell(z) * RoadGrid::N + RoadGrid::cell(x)];
  float W = 0.f, hs = 0.f, ws = 0.f;
  for (uint32_t k = RoadGrid::first(h), e = k + RoadGrid::count(h); k < e; k++) {
    const RoadSegment& s = grid.segs[grid.list[k]];
    if (s.flags) continue;   // a bridge's ground and an airfield's are left as they are
    const float dx = s.bx - s.ax, dz = s.bz - s.az;
    const float t = std::clamp(((x - s.ax) * dx + (z - s.az) * dz) / std::max(dx * dx + dz * dz, 1e-3f), 0.f, 1.f);
    const float px = s.ax + dx * t - x, pz = s.az + dz * t - z, d = sqrtf(px * px + pz * pz);
    const float hr = s.ah + (s.bh - s.ah) * t, P = roadSpec(s.cls).halfPlatform;
    const float bank = std::clamp(fabsf(g - hr) * 1.6f, 3.f, ROAD_BANK_MAX);
    const float w = 1.f - smoothstepf(P, P + bank, d);
    if (w <= 0.f) continue;
    float w4 = w * w; w4 *= w4;
    W = std::max(W, w); hs += w4 * hr; ws += w4;
  }
  return W > 0.f ? g + (hs / ws - g) * W : g;
}

float roadEdgeDistance(const RoadGrid& grid, float x, float z, int* seg) {
  if (seg) *seg = -1;
  if (grid.nearHead.empty()) return 1e9f;
  const float CS = 2.f * WORLD_HALF / RoadGrid::NC;
  const int i = std::clamp((int)floorf((x + WORLD_HALF) / CS), 0, RoadGrid::NC - 1), j = std::clamp((int)floorf((z + WORLD_HALF) / CS), 0, RoadGrid::NC - 1);
  const uint32_t h = grid.nearHead[(size_t)j * RoadGrid::NC + i];
  float best = 1e9f;
  for (uint32_t k = h >> 8, e = k + (h & 255u); k < e; k++) {
    const RoadSegment& s = grid.segs[grid.nearList[k]];
    const float dx = s.bx - s.ax, dz = s.bz - s.az;
    const float t = std::clamp(((x - s.ax) * dx + (z - s.az) * dz) / std::max(dx * dx + dz * dz, 1e-3f), 0.f, 1.f);
    const float d = hypotf(s.ax + dx * t - x, s.az + dz * t - z) - roadSpec(s.cls).halfPlatform;
    if (d < best) { best = d; if (seg) *seg = (int)grid.nearList[k]; }
  }
  return best > 500.f ? 1e9f : best;
}

std::vector<float> roadEntryRows(const RoadGrid& grid) {
  std::vector<float> d;
  d.reserve(grid.list.size() * 8 + ROAD_DATA_W * 4);
  for (uint32_t si : grid.list) {
    const RoadSegment& r = grid.segs[si];
    const float e[8] = {r.ax, r.az, r.bx, r.bz, r.ah, r.bh, r.along, (float)(r.cls + 4 * r.flags + 16 * r.path)};
    d.insert(d.end(), e, e + 8);
  }
  d.resize((d.size() + ROAD_DATA_W * 4 - 1) / (ROAD_DATA_W * 4) * (ROAD_DATA_W * 4), 0.f);
  return d;
}
