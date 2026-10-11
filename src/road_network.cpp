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
  // the ground between the cells' centres (bilinear)
  float at(vec2 p) const {
    const float fx = (p.x + WORLD_HALF) / G - 0.5f, fz = (p.y + WORLD_HALF) / G - 0.5f;
    const int i = std::clamp((int)floorf(fx), 0, GN - 2), j = std::clamp((int)floorf(fz), 0, GN - 2);
    const float tx = std::clamp(fx - i, 0.f, 1.f), tz = std::clamp(fz - j, 0.f, 1.f);
    return (h[idx(i, j)] * (1 - tx) + h[idx(i + 1, j)] * tx) * (1 - tz) + (h[idx(i, j + 1)] * (1 - tx) + h[idx(i + 1, j + 1)] * tx) * tz;
  }
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
// 40 directions: the eight neighbours, the knight's moves and the moves three and four cells across for one or two
// along - a route can run at 14, 18, 27 and 34 degrees to the grid without a zigzag, and slant down a mountainside
// steeper than its grade (a slope of 0.37 is 0.12 at 18 degrees off the contour, 0.17 at the knight's 27)
const int kDirs[40][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
                          {2, 1}, {2, -1}, {-2, 1}, {-2, -1}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2},
                          {3, 1}, {3, -1}, {-3, 1}, {-3, -1}, {1, 3}, {1, -3}, {-1, 3}, {-1, -3},
                          {3, 2}, {3, -2}, {-3, 2}, {-3, -2}, {2, 3}, {2, -3}, {-2, 3}, {-2, -3},
                          {4, 1}, {4, -1}, {-4, 1}, {-4, -1}, {1, 4}, {1, -4}, {-1, 4}, {-1, -4}};

struct Router {
  const Grid& g;
  std::vector<float> cost;   // best cost found to the cell
  std::vector<int> from;     // the cell it came from
  std::vector<uint32_t> seen;   // the search that last touched the cell (no clearing between searches)
  std::vector<uint8_t> roadCell;   // cells an existing road passes through (a new route reuses them cheaply)
  uint32_t search = 0;
  bool access = false;       // routing an airfield's access road: out across its grounds (dearly), never a runway's strip
  float steepest = 1.f;      // the steepest ground a step may climb, in the class's grade (more only when nothing else routes)
  bool passable(int n) const { return g.blocked[n] == 0 || (access && g.blocked[n] == 1); }
  explicit Router(const Grid& grid) : g(grid), cost(grid.h.size()), from(grid.h.size()), seen(grid.h.size(), 0), roadCell(grid.h.size(), 0) {}

  // the cost of a stretch len long from ground ha to ground hb, ending in cell b, for a road of class c (infinite: not
  // allowed). The class's grade is what the road will be graded to (profile): ground steeper than that never - the
  // route climbs across the slope instead, in hairpins where it can, round the mountain where it can't (a step only a
  // little steeper, taken again and again down a mountainside, left the road a hundred metres up in the air)
  float stepCost(float ha, float hb, float len, int b, int c) const {
    if (!passable(b)) return INFINITY;
    const RoadSpec& s = roadSpec(c);
    float k = g.blocked[b] ? 3.f : 1.f;
    if (hb < 0.5f) {   // over the water: a bridge (the deep sea, and the small roads: never)
      if (c >= RC_LANE || hb < -22.f) return INFINITY;
      k *= 12.f;
    } else {
      const float grade = fabsf(hb - std::max(ha, 0.5f)) / len, gm = s.maxGrade;
      if (grade > steepest * gm) return INFINITY;
      k *= grade > gm ? 1.f + 30.f * (grade - gm) / gm : 1.f + 0.6f * (grade / gm) * (grade / gm);
      k *= 1.f + 3.f * std::max(0.f, g.slope[b] - 0.08f);   // a steep hillside: cut and fill (the valley floor instead)
    }
    if (roadCell[b]) k *= 0.55f;
    return len * k;
  }
  float step(int a, int b, float len, int c) const { return stepCost(g.h[a], g.h[b], len, b, c); }
  // a straight line's cost, half a cell at a time over the ground between the cells' centres (nothing it crosses
  // blocked, nor too steep)
  float line(vec2 a, vec2 b, int c) const {
    const float L = length(b - a);
    const int n = std::max(1, (int)ceilf(L / (G * 0.5f)));
    float sum = 0, hp = g.at(a);
    for (int k = 1; k <= n; k++) {
      const vec2 p = a + (b - a) * ((float)k / n);
      const float hq = g.at(p);
      const float s = stepCost(hp, hq, L / n, Grid::idx(Grid::cell(p.x), Grid::cell(p.y)), c);
      if (!std::isfinite(s)) return INFINITY;
      sum += s; hp = hq;
    }
    return sum;
  }
  // a move between two cells: to a neighbour, the step; further, the line between their centres
  float move(int a, int b, int c) const {
    const int di = b % GN - a % GN, dj = b / GN - a / GN;
    if (abs(di) <= 1 && abs(dj) <= 1) return step(a, b, G * sqrtf((float)(di * di + dj * dj)), c);
    return line(vec2(Grid::cx(a % GN), Grid::cx(a / GN)), vec2(Grid::cx(b % GN), Grid::cx(b / GN)), c);
  }

  // from a cell to the cheapest of the goal cells (a set: any cell with goal[] set), within the window; the path from
  // the start, cell by cell, or empty when there is none. A turn costs as much road as (1 - cos of it) / 2 times the
  // class's radius and 150 m - next to nothing for a slight bend, a hairpin dear: up a slope a zig-zag of a dozen
  // short legs and a switchback of three long ones climb it on the same road, and without it the route took the
  // sawtooth. Nor more than 135 degrees at once: the route doubles back in two corners with a leg between, its legs a
  // road's turning circle apart - a V doubling back in one corner brought them together at its tip, two platforms at
  // heights metres apart side by side. (Turned from the way the cheapest route so far came in: a heuristic, the search
  // no longer exact)
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
      const int pc = from[c0];
      const float ix = pc >= 0 ? (float)(ci - pc % GN) : 0.f, iz = pc >= 0 ? (float)(cj - pc / GN) : 0.f, il = sqrtf(ix * ix + iz * iz);
      const float turnCost = 0.5f * (roadSpec(c).minRadius + 150.f);
      for (auto& d : kDirs) {
        const int ni = ci + d[0], nj = cj + d[1];
        if (ni < i0 || nj < j0 || ni > i1 || nj > j1) continue;
        const int n = Grid::idx(ni, nj);
        float sc = move(c0, n, c);
        if (il > 0.f) {
          const float turn = (ix * d[0] + iz * d[1]) / (il * sqrtf((float)(d[0] * d[0] + d[1] * d[1])));   // (its cosine)
          if (turn < -0.7072f) continue;   // (never back on itself in one corner: a hairpin takes two, a leg between)
          sc += turnCost * (1.f - turn);
        }
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

// the cell path as straight legs: from each corner, as far ahead as a straight leg costs no more than the cells it
// replaces (and crosses nothing the route avoided)
std::vector<vec2> straighten(const Router& r, const std::vector<int>& cells, int c) {
  std::vector<vec2> p(cells.size());
  for (size_t k = 0; k < cells.size(); k++) p[k] = vec2(Grid::cx(cells[k] % GN), Grid::cx(cells[k] / GN));
  std::vector<float> acc(p.size(), 0.f);   // the route's own cost up to each cell
  for (size_t k = 1; k < p.size(); k++) acc[k] = acc[k - 1] + r.move(cells[k - 1], cells[k], c);
  std::vector<vec2> out{p[0]};
  size_t i = 0;
  while (i + 1 < p.size()) {
    size_t best = i + 1;
    for (size_t j = std::min(p.size() - 1, i + 400); j > i + 1; j--) {
      const float lc = r.line(p[i], p[j], c);
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

// the line at most ds between points (its corners kept)
std::vector<vec2> densify(const std::vector<vec2>& p, float ds) {
  std::vector<vec2> out{p[0]};
  for (size_t k = 1; k < p.size(); k++) {
    const float L = length(p[k] - p[k - 1]);
    if (L < 1e-3f) continue;
    const int m = std::max(1, (int)ceilf(L / ds - 1e-3f));
    for (int q = 1; q <= m; q++) out.push_back(p[k - 1] + (p[k] - p[k - 1]) * ((float)q / m));
  }
  return out;
}

// The bends eased to the class's radius where the ground allows (a fillet between short legs is tighter): a point on
// a bend tighter than R moved across the road towards its neighbours' chord - to the offset that bends it at R, half
// way at a time - and its neighbours looked at again, until none is tighter or none can move. A point never moves
// into the sea (where it wasn't over it already), onto an airfield's grounds, nor farther than maxShift from where
// the route put it, nor at all where the road climbs near its grade (easing a bend shortens it, and a climb needs
// every metre of it): there the bend stays as tight as it is (a hairpin up a mountainside). The ends never move.
void ease(std::vector<vec2>& p, float R, float maxShift, const std::function<bool(vec2, vec2)>& ok, const std::function<bool(vec2, vec2)>& climbing) {
  const int n = (int)p.size();
  if (n < 3) return;
  const std::vector<vec2> orig = p;
  std::vector<uint8_t> queued(n, 1); queued[0] = queued[n - 1] = 0;
  std::vector<int> work; work.reserve(n);
  for (int k = n - 2; k >= 1; k--) work.push_back(k);
  for (long budget = 400L * n; !work.empty() && budget > 0; budget--) {
    const int k = work.back(); work.pop_back(); queued[k] = 0;
    const vec2 a = p[k - 1], b = p[k], c = p[k + 1], w = c - a;
    const float lu = length(b - a), lv = length(c - b), lw = length(w);
    if (lu < 1e-3f || lv < 1e-3f || lw < 1e-3f || climbing(a, c)) continue;
    const vec2 u = b - a, v = c - b;
    if (2.f * fabsf(u.x * v.y - u.y * v.x) / (lu * lv * lw) * R <= 1.001f) continue;   // (the circle through the three)
    const vec2 nrm(-w.y / lw, w.x / lw), foot = a + w * (lu / (lu + lv));
    const float off = (b.x - foot.x) * nrm.x + (b.y - foot.y) * nrm.y, want = std::min(fabsf(off), lw * lw / (8.f * R)) * (off < 0.f ? -1.f : 1.f);
    const vec2 nb = b + nrm * ((want - off) * 0.5f);
    if (length(nb - b) < 0.002f || length(nb - orig[k]) > maxShift || !ok(orig[k], nb)) continue;
    p[k] = nb;
    for (int q : {k - 1, k, k + 1}) if (q > 0 && q < n - 1 && !queued[q]) { queued[q] = 1; work.push_back(q); }
  }
}

// Points along the eased line for the road: one every `spacing` along a straight, and round a bend wherever the line
// has turned by 3 degrees since the last (so the chords between them lie within a few centimetres of the curve, and
// the curve reads as one wherever it is drawn), never closer than minChord
std::vector<vec2> resample(const std::vector<vec2>& p, float spacing, float minChord) {
  std::vector<vec2> out{p[0]};
  if (p.size() < 2) return out;
  const float maxTurn = 3.f * DEG;
  auto heading = [&](size_t k) { return atan2f(p[k + 1].y - p[k].y, p[k + 1].x - p[k].x); };
  float run = 0, h0 = heading(0);
  for (size_t k = 1; k + 1 < p.size(); k++) {
    run += length(p[k] - p[k - 1]);
    float turn = heading(k) - h0;
    turn = fabsf(turn - 2.f * PI * floorf((turn + PI) / (2.f * PI)));
    const float next = length(p[k + 1] - p[k]);
    if (run + next > spacing * 1.001f || (turn >= maxTurn && run >= minChord)) { out.push_back(p[k]); run = 0; h0 = heading(k); }
  }
  // the last point: the line's end (the one before it dropped should it be too close)
  if (out.size() > 1 && length(p.back() - out.back()) < minChord * 0.5f) out.pop_back();
  out.push_back(p.back());
  return out;
}

}  // namespace

bool roadByAirfield(const RoadPoint& A, const RoadPoint& B, int cls) {
  const float reach = roadSpec(cls).halfPlatform + roadBankMax(cls);
  const float dx = B.x - A.x, dz = B.z - A.z, L = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / L, nz = dx / L;
  for (int q = 0; q <= 4; q++)
    for (int side = -2; side <= 2; side++) {
      float grounds, cap; airportGroundsCap(A.x + dx * q / 4 + nx * reach * side / 2, A.z + dz * q / 4 + nz * reach * side / 2, grounds, cap);
      if (grounds > 0.f) return true;
    }
  return false;
}

namespace {

// the graded profile: the ground smoothed along the road, never steeper than the class allows; over water at least the
// deck clearance. Bridges: over water, or where the road runs well above the ground for long enough
void profile(const World& world, RoadPath& path, float h0, float h1, const std::vector<std::pair<int, float>>& pins = {}) {
  const RoadSpec& s = roadSpec(path.cls);
  const int n = (int)path.pts.size();
  std::vector<float> ground(n), ds(n, 0.f);
  for (int k = 0; k < n; k++) ground[k] = world.groundHeight(path.pts[k].x, path.pts[k].z, 8);
  for (int k = 1; k < n; k++) ds[k] = hypotf(path.pts[k].x - path.pts[k - 1].x, path.pts[k].z - path.pts[k - 1].z);
  std::vector<float> along(n, 0.f);
  for (int k = 1; k < n; k++) along[k] = along[k - 1] + ds[k];
  const float half = (path.cls == RC_HIGHWAY ? 300.f : path.cls == RC_ROAD ? 140.f : path.cls == RC_LANE ? 70.f : 50.f) * 0.5f;
  const float deck = 9.f;   // a bridge's deck over the sea
  const float steepest = s.maxGrade;
  // the ground (over the sea, the deck's height) averaged along the road, the nearer the more (by distance: the points
  // closer round a bend than along a straight)
  std::vector<float> target(n);
  for (int k = 0, q0 = 0; k < n; k++) {
    while (along[k] - along[q0] > half) q0++;
    float sum = 0, wsum = 0;
    for (int q = q0; q < n && along[q] - along[k] <= half; q++) {
      const float wt = (1.f - fabsf(along[q] - along[k]) / (half + 1.f)) * 0.5f * ((q > 0 ? ds[q] : 0.f) + (q + 1 < n ? ds[q + 1] : 0.f) + 1e-3f);
      sum += (ground[q] < 0.5f ? std::max(ground[q], deck) : ground[q]) * wt; wsum += wt;
    }
    target[k] = sum / wsum;
    if (ground[k] < 0.5f) target[k] = std::max(target[k], deck);
  }
  target[0] = h0; target[n - 1] = h1;
  // under an approach funnel never above what it holds the ground to (a metre's margin, the banks blending down to
  // ground that is itself no higher; nor below the ground there, should that stand higher)
  std::vector<float> ceiling(n), floor(n);
  for (int k = 0; k < n; k++) {
    ceiling[k] = std::max(airportFunnelCeiling(path.pts[k].x, path.pts[k].z) - 1.f, ground[k]);
    floor[k] = ground[k] < 0.5f ? std::min(deck, ceiling[k]) : -1e9f;
  }
  // by an airfield's grounds the road lies on the ground as it is (buildRoadGrid paints it there, ungraded): its points
  // held there, as its ends are
  std::vector<uint8_t> airfield(n - 1), held(n, 0);
  std::vector<float> heldH(ground);
  for (int k = 0; k + 1 < n; k++) if ((airfield[k] = roadByAirfield(path.pts[k], path.pts[k + 1], path.cls))) held[k] = held[k + 1] = 1;
  for (const auto& pin : pins) if (pin.first > 0 && pin.first < n - 1) { held[pin.first] = 1; heldH[pin.first] = pin.second; }   // (and where it crosses a road)
  // the funnels' ceiling and the bridges' deck clearance, then the grade limit forward and back until it settles (the
  // ends held where the road meets a town or another road, the points by an airfield on its ground); the grade has the
  // last word elsewhere
  std::vector<float> h = target;
  auto hold = [&](int k) { if (held[k]) h[k] = heldH[k]; };
  for (int k = 0; k < n; k++) hold(k);
  // (the held points never move: each pass clamps the rest against them, until a pass changes nothing)
  for (int it = 0; it < 40; it++) {
    const std::vector<float> was = h;
    for (int k = 0; k < n; k++) if (!held[k]) h[k] = std::clamp(h[k], floor[k], ceiling[k]);
    h[0] = h0;
    for (int k = 1; k < n; k++) if (!held[k]) h[k] = std::clamp(h[k], h[k - 1] - steepest * ds[k], h[k - 1] + steepest * ds[k]);
    h[n - 1] = h1;
    for (int k = n - 2; k >= 0; k--) if (!held[k]) h[k] = std::clamp(h[k], h[k + 1] - steepest * ds[k + 1], h[k + 1] + steepest * ds[k + 1]);
    float moved = 0; for (int k = 0; k < n; k++) moved = std::max(moved, fabsf(h[k] - was[k]));
    if (moved < 1e-3f) break;
  }
  // the vertical curves: wherever the grade changes faster than the class's K allows, the point eased towards the
  // straight line between its neighbours - to where the change is just that fast - and its neighbours looked at again
  // (a crest cut down, a sag filled, over as much road as the change needs). A point eased towards that line only
  // ever brings the grades either side of it nearer each other: the grade limit holds; nor past the funnels' ceiling
  // or the deck's clearance. The ends never move.
  {
    const float bend = 1.f / (s.curveK * 100.f);   // the change of grade per metre allowed
    std::vector<uint8_t> queued(n, 1); queued[0] = queued[n - 1] = 0;
    std::vector<int> work; for (int k = n - 2; k >= 1; k--) work.push_back(k);
    for (long budget = 600L * n; !work.empty() && budget > 0; budget--) {
      const int k = work.back(); work.pop_back(); queued[k] = 0;
      if (held[k]) continue;
      const float d0 = std::max(ds[k], 1e-3f), d1 = std::max(ds[k + 1], 1e-3f);
      const float g0 = (h[k] - h[k - 1]) / d0, g1 = (h[k + 1] - h[k]) / d1, limit = bend * 0.5f * (d0 + d1);
      if (fabsf(g1 - g0) <= limit * 1.001f) continue;
      // (with its neighbours held, the change of grade at k is (line - h) (1/d0 + 1/d1))
      const float line = h[k - 1] + (h[k + 1] - h[k - 1]) * d0 / (d0 + d1), per = 1.f / d0 + 1.f / d1;
      const float want = line - (line - h[k] > 0.f ? 1.f : -1.f) * limit / per;
      const float nh = std::clamp(want, std::min(floor[k], h[k]), std::max(ceiling[k], h[k]));   // (never across a bound)
      if (fabsf(nh - h[k]) < 1e-4f) continue;
      h[k] = nh;
      for (int q : {k - 1, k + 1}) if (q > 0 && q < n - 1 && !queued[q]) { queued[q] = 1; work.push_back(q); }
    }
  }
  // ends too far apart in height for the road's length at its class's grade (a mountain strip's access road, say):
  // what the start is still short of spread along the whole road, a steadily steeper climb rather than a cliff
  float total = 0; for (int k = 1; k < n; k++) total += ds[k];
  const float r = h0 - h[0];
  float sk = 0;
  for (int k = 0; k < n; k++) { if (k) sk += ds[k]; h[k] += r * (1.f - sk / std::max(total, 1e-3f)); hold(k); }
  for (int k = 0; k < n; k++) path.pts[k].h = h[k];
  // across a steep hillside (the ground either side of the road differing by more than a third of the way between)
  // a road standing clear of it is a shelf, graded into the slope; a bridge only where it crosses something - a ravine,
  // a gully - with the ground falling away on both sides, or where it stands more than 15 m clear of the slope (a
  // viaduct along it, rather than a fill that high)
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
    const float clear = std::min(h[k] - ground[k], h[k + 1] - ground[k + 1]);
    const bool high = clear > 9.f && (!(hillside[k] && hillside[k + 1]) || clear > 15.f);
    path.bridge[k] = (water || high) && !airfield[k];
  }
  // (a gap of under 60 m between two spans, the road still well clear of the ground, is bridged too - not a pillar of
  // fill between two viaducts; then a span shorter than 45 m over dry ground is an embankment, not a bridge)
  for (int k = 0; k + 1 < n; ) {
    if (path.bridge[k]) { k++; continue; }
    int e = k; bool low = false;
    while (e + 1 < n && !path.bridge[e]) { low = low || h[e] - ground[e] < 5.f || h[e + 1] - ground[e + 1] < 5.f; e++; }
    if (k > 0 && e + 1 < n && !low && along[e] - along[k] < 60.f) for (int q = k; q < e; q++) path.bridge[q] = !airfield[q];
    k = e;
  }
  for (int k = 0; k + 1 < n; ) {
    if (!path.bridge[k]) { k++; continue; }
    int e = k; bool wet = false;
    while (e + 1 < n && path.bridge[e]) { wet = wet || ground[e] < 0.5f || ground[e + 1] < 0.5f; e++; }
    if (!wet && along[e] - along[k] < 45.f) for (int q = k; q < e; q++) path.bridge[q] = 0;
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
  auto shape = [&](const std::vector<int>& cells, RoadClass cls, int from, int to, vec2 a, float ha, vec2 b, float hb, float& water) {
    RoadPath path; path.cls = cls; path.from = from; path.to = to;
    const RoadSpec& s = roadSpec(cls);
    std::vector<vec2> legs = straighten(router, cells, cls);
    legs.front() = a; legs.back() = b;
    // the legs' corners rounded, the bends between short legs eased out to the class's radius as far as the ground
    // lets them, then points along it closer round the bends
    const float ds = s.spacing / ceilf(s.spacing / std::clamp(s.minRadius / 12.f, 2.f, 10.f));
    std::vector<vec2> line = densify(fillet(legs, s.minRadius), ds);
    ease(line, s.minRadius, std::clamp(0.3f * s.minRadius, 8.f, 60.f), [&](vec2 from, vec2 to) {
      const int c = Grid::idx(Grid::cell(to.x), Grid::cell(to.y));
      return router.passable(c) && (g.at(to) >= 0.5f || g.at(from) < 0.5f);
    }, [&](vec2 a, vec2 b) { return fabsf(g.at(b) - g.at(a)) > 0.6f * s.maxGrade * length(b - a); });
    const std::vector<vec2> pts = resample(line, s.spacing, ds);
    for (const vec2& p : pts) path.pts.push_back({p.x, p.y, 0.f});
    water = 0;
    if (path.pts.size() < 2) return path;
    profile(world, path, ha, hb);
    for (size_t k = 0; k + 1 < path.pts.size(); k++)
      if (world.groundHeight(path.pts[k].x, path.pts[k].z, 6) < 0.5f) water += hypotf(path.pts[k + 1].x - path.pts[k].x, path.pts[k + 1].z - path.pts[k].z);
    return path;
  };
  // a route at the class's grade; only where there is none, one up ground half as steep again, then 2.5 times (the
  // router left that steep until the route is shaped)
  auto route = [&](int start, const std::vector<uint8_t>& goals, vec2 target, int c, int i0, int j0, int i1, int j1) {
    router.steepest = 1.f;
    std::vector<int> cells = router.route(start, goals, target, c, i0, j0, i1, j1);
    for (float relax : {1.5f, 2.5f}) {
      if (cells.size() >= 2) break;
      router.steepest = relax; cells = router.route(start, goals, target, c, i0, j0, i1, j1);
    }
    return cells;
  };
  // the nearest point of a road already built to c (off its bridges), and its height there; its distance (1e9: none)
  auto nearestRoad = [&](vec2 c, vec2& at, float& h) {
    float best = 1e9f;
    for (const RoadPath& p : net.paths)
      for (size_t k = 0; k + 1 < p.pts.size(); k++) {
        if (p.bridge[k]) continue;
        const RoadPoint &A = p.pts[k], &B = p.pts[k + 1];
        const float dx = B.x - A.x, dz = B.z - A.z, t = std::clamp(((c.x - A.x) * dx + (c.y - A.z) * dz) / std::max(dx * dx + dz * dz, 1e-6f), 0.f, 1.f);
        const float d = hypotf(A.x + dx * t - c.x, A.z + dz * t - c.y);
        if (d < best) { best = d; at = vec2(A.x + dx * t, A.z + dz * t); h = A.h + (B.h - A.h) * t; }
      }
    return best;
  };
  // A link along its route - but where the route runs along a road already built (four of its cells or more in a row)
  // that road carries it: the link's own road ends at a junction where it reaches that road and starts again at
  // another where it leaves it, rather than running on beside it (two roads a few metres apart at heights a metre or
  // two different, their platforms one lumpy bank)
  auto link = [&](const Link& l, float maxWater) {
    const RoadNode &A = net.nodes[l.a], &B = net.nodes[l.b];
    int i0, j0, i1, j1; window(vec2(A.x, A.z), vec2(B.x, B.z), 0.35f * l.d + 3000.f, i0, j0, i1, j1);
    std::fill(goal.begin(), goal.end(), 0); goal[cellOf(B)] = 1;
    const std::vector<int> cells = route(cellOf(A), goal, vec2(B.x, B.z), l.cls, i0, j0, i1, j1);
    if (cells.size() < 2) { router.steepest = 1.f; return false; }
    std::vector<std::pair<int, int>> pieces;   // (first, last cell) of the link's own stretches
    {
      int start = 0;
      for (size_t k = 0; k < cells.size();) {
        if (!router.roadCell[cells[k]]) { k++; continue; }
        size_t e = k; while (e + 1 < cells.size() && router.roadCell[cells[e + 1]]) e++;
        if (e - k + 1 >= 4) { pieces.push_back({start, (int)k}); start = (int)e; }
        k = e + 1;
      }
      pieces.push_back({start, (int)cells.size() - 1});
    }
    std::vector<RoadPath> made; float wet = 0;
    for (const auto& pc : pieces) {
      if (pc.second - pc.first < 1) continue;   // (the node on the road already)
      const bool first = pc.first == 0, last = pc.second == (int)cells.size() - 1;
      vec2 a(A.x, A.z), b(B.x, B.z); float ha = world.groundHeight(A.x, A.z, 8), hb = world.groundHeight(B.x, B.z, 8);
      const vec2 ca(Grid::cx(cells[pc.first] % GN), Grid::cx(cells[pc.first] / GN)), cb(Grid::cx(cells[pc.second] % GN), Grid::cx(cells[pc.second] / GN));
      if (!first && nearestRoad(ca, a, ha) > G * 1.5f) a = ca, ha = world.groundHeight(ca.x, ca.y, 8);
      if (!last && nearestRoad(cb, b, hb) > G * 1.5f) b = cb, hb = world.groundHeight(cb.x, cb.y, 8);
      std::vector<int> sub(cells.begin() + pc.first, cells.begin() + pc.second + 1);
      float water;
      RoadPath path = shape(sub, l.cls, first ? l.a : -1, last ? l.b : -1, a, ha, b, hb, water);
      if (path.pts.size() < 2) continue;
      wet += water; made.push_back(std::move(path));
    }
    router.steepest = 1.f;
    if (made.empty() || wet > maxWater) return false;
    for (RoadPath& p : made) { mark(p); net.paths.push_back(std::move(p)); }
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
    // (a strip's access road: a country lane, narrower and allowed steeper - the mountain strips' climb; and any other
    // place's whose road would have to be steeper than a road may be)
    RoadClass cls = A.kind == 3 && world.airports[i - kNumTowns].size == 0 ? RC_LANE : RC_ROAD;
    std::vector<int> cells = route(cellOf(A), goal, vec2(-1e9f, -1e9f), cls, i0, j0, i1, j1);
    if (cls == RC_ROAD && router.steepest > 1.f) {
      std::vector<int> lane = route(cellOf(A), goal, vec2(-1e9f, -1e9f), RC_LANE, i0, j0, i1, j1);
      if (lane.size() >= 2) { cells = lane; cls = RC_LANE; }
    }
    if (cells.size() < 2) { router.access = false; router.steepest = 1.f; continue; }
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
      if (bd > 200.f) { router.access = false; router.steepest = 1.f; continue; }
    }
    float water;
    RoadPath path = shape(cells, cls, i, to, vec2(A.x, A.z), world.groundHeight(A.x, A.z, 8), end, hEnd, water);   // (straightened as it was routed)
    router.access = false; router.steepest = 1.f;
    if (path.pts.size() < 2 || water > 600.f) continue;
    mark(path); net.paths.push_back(std::move(path));
  }
  return net;
}

RoadPath layRoad(const World& world, RoadClass cls, const std::vector<vec2>& line, float h0, float h1, const std::vector<RoadPin>& pins) {
  RoadPath path; path.cls = cls;
  if (line.size() < 2) return path;
  const RoadSpec& s = roadSpec(cls);
  const float ds = s.spacing / ceilf(s.spacing / std::clamp(s.minRadius / 12.f, 2.f, 10.f));
  std::vector<vec2> pts = resample(densify(fillet(line, s.minRadius), ds), s.spacing, ds);
  // each pin a point of its own, exactly where it is (on the segment nearest it)
  std::vector<std::pair<int, float>> held;
  for (const RoadPin& pin : pins) {
    const vec2 c(pin.x, pin.z);
    size_t at = 0; float best = 1e9f, tb = 0.f;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
      const vec2 d = pts[k + 1] - pts[k];
      const float t = std::clamp(((c.x - pts[k].x) * d.x + (c.y - pts[k].y) * d.y) / std::max(d.x * d.x + d.y * d.y, 1e-6f), 0.f, 1.f);
      const float e = length(pts[k] + d * t - c);
      if (e < best) { best = e; at = k; tb = t; }
    }
    if (best > 2.f) continue;
    if (tb > 0.f && tb < 1.f) {
      const vec2 q = pts[at] + (pts[at + 1] - pts[at]) * tb;
      if (length(q - pts[at]) < 0.5f) tb = 0.f;
      else if (length(q - pts[at + 1]) < 0.5f) tb = 1.f;
      else { pts.insert(pts.begin() + at + 1, q); for (auto& h : held) if (h.first > (int)at) h.first++; }
    }
    held.push_back({(int)at + (tb > 0.f ? 1 : 0), pin.h});
  }
  for (const vec2& p : pts) path.pts.push_back({p.x, p.y, 0.f});
  if (path.pts.size() < 2) return path;
  profile(world, path, h0, h1, held);
  path.bridge.assign(path.pts.size() - 1, 0);
  return path;
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
    // each run of bridge, of road by an airfield, of graded road kept whole (its ends kept), the points along it
    // simplified
    std::vector<uint8_t> airfield(n - 1);
    for (int k = 0; k + 1 < n; k++) airfield[k] = roadByAirfield(p.pts[k], p.pts[k + 1], p.cls);
    std::vector<uint8_t> keep(n, 0); keep[0] = keep[n - 1] = 1;
    for (int k = 1; k + 1 < n; k++) if (p.bridge[k - 1] != p.bridge[k] || airfield[k - 1] != airfield[k]) keep[k] = 1;
    for (int a = 0; a < n - 1; ) { int b = a + 1; while (!keep[b]) b++; simplify(p, a, b, 0.15f, keep); a = b; }
    float along = 0;
    int a = 0;
    for (int b = 1; b < n; b++) {
      if (!keep[b]) continue;
      const RoadPoint &A = p.pts[a], &B = p.pts[b];
      RoadSegment seg{A.x, A.z, B.x, B.z, A.h, B.h, along, (uint16_t)pi, (uint8_t)p.cls, (uint8_t)(p.bridge[a] ? RS_BRIDGE : 0)};
      if (!p.bridge[a] && a > 0 && p.bridge[a - 1]) seg.flags |= RS_ENDA;       // (off the end of a bridge)
      if (!p.bridge[a] && b + 1 < n && p.bridge[b]) seg.flags |= RS_ENDB;   // (onto one)
      // by an airfield's grounds: painted on the ground as it is (the airfield's terrain is never reshaped - and the
      // profile held on it there)
      if (airfield[a]) seg.flags |= RS_NOGRADE;
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
    const float R = roadSpec(s.cls).halfPlatform + roadBankMax(s.cls);
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
    if (s.flags & (RS_BRIDGE | RS_NOGRADE)) continue;   // a bridge's ground and an airfield's are left as they are
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L2 = std::max(dx * dx + dz * dz, 1e-3f), L = sqrtf(L2);
    const float tr = ((x - s.ax) * dx + (z - s.az) * dz) / L2, t = std::clamp(tr, 0.f, 1.f);
    const float px = s.ax + dx * t - x, pz = s.az + dz * t - z, d = sqrtf(px * px + pz * pz);
    const float P = roadSpec(s.cls).halfPlatform;
    // the height: past an end, on along the segment's grade (as far as its platform reaches - the next segment's own
    // height there, near enough, where a round end held it level and dragged the road's bed off its grade)
    const float te = std::clamp(tr, -P / L, 1.f + P / L);
    float hr = s.ah + (s.bh - s.ah) * te;
    const float past = std::max(std::max(-tr, tr - 1.f), 0.f) * L;   // (how far beyond its ends)
    // past an end at a bridge: under the deck's slab for 2 m, then falling away at 1 in 1.5 to the ground
    const bool bridgeEnd = (tr < 0.f && (s.flags & RS_ENDA)) || (tr > 1.f && (s.flags & RS_ENDB));
    if (bridgeEnd) hr -= 0.3f;
    const float bank = std::clamp(fabsf(g - hr) * ROAD_BANK_RUN, 4.f, roadBankMax(s.cls));
    float w = 1.f - smoothstepf(P, P + bank, d);
    if (bridgeEnd) w *= 1.f - smoothstepf(2.f, 2.f + std::clamp(fabsf(g - hr) * 1.5f, 1.f, 20.f), past);
    if (w <= 0.f) continue;
    // its say in the height: a platform the point is on - or its bank's rounded top - two hundred times a bank's (a
    // hairpin's other leg, a junction's banks tilted it); beyond its ends a segment gives way to the next
    float w4 = w * w; w4 *= w4;
    const float top = smoothstepf(0.9f, 1.f, w), wt = w4 * (1.f + 200.f * top * top) * (1.f - 0.9f * smoothstepf(0.f, P, past));
    W = std::max(W, w); hs += wt * hr; ws += wt;
  }
  return W > 0.f ? g + (hs / ws - g) * W : g;
}

float roadEdgeDistance(const RoadGrid& grid, float x, float z, int* seg) {
  if (seg) *seg = -1;
  // the place's own texel first: its list holds every segment whose banks could reach it, so a platform within the
  // narrowest banks' reach (a street's) is found exactly - however crowded a city's coarse cell, whose list keeps only
  // the 255 segments nearest its centre
  float fine = 1e9f; int fineSeg = -1;
  if (!grid.head.empty()) {
    const uint32_t h = grid.head[(size_t)RoadGrid::cell(z) * RoadGrid::N + RoadGrid::cell(x)];
    for (uint32_t k = RoadGrid::first(h), e = k + RoadGrid::count(h); k < e; k++) {
      const RoadSegment& s = grid.segs[grid.list[k]];
      const float dx = s.bx - s.ax, dz = s.bz - s.az;
      const float t = std::clamp(((x - s.ax) * dx + (z - s.az) * dz) / std::max(dx * dx + dz * dz, 1e-3f), 0.f, 1.f);
      const float d = hypotf(s.ax + dx * t - x, s.az + dz * t - z) - roadSpec(s.cls).halfPlatform;
      if (d < fine) { fine = d; fineSeg = (int)grid.list[k]; }
    }
  }
  if (fine <= roadBankMax(RC_STREET)) { if (seg) *seg = fineSeg; return fine; }
  if (grid.nearHead.empty()) { if (seg) *seg = fineSeg; return fine; }
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
  if (fine < best) { best = fine; if (seg) *seg = fineSeg; }
  return best > 500.f ? 1e9f : best;
}

std::vector<float> roadEntryRows(const RoadGrid& grid) {
  std::vector<float> d;
  d.reserve(grid.list.size() * 8 + ROAD_DATA_W * 4);
  for (uint32_t si : grid.list) {
    const RoadSegment& r = grid.segs[si];
    const float e[8] = {r.ax, r.az, r.bx, r.bz, r.ah, r.bh, r.along, (float)(r.cls + 8 * r.flags + 128 * r.path)};
    d.insert(d.end(), e, e + 8);
  }
  d.resize((d.size() + ROAD_DATA_W * 4 - 1) / (ROAD_DATA_W * 4) * (ROAD_DATA_W * 4), 0.f);
  return d;
}
