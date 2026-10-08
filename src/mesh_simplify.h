// Solace Express - simplifying the baked aircraft meshes (aircraft_mesh.cpp). Surface nets put a vertex in every
// lattice cube the surface crosses, so a flat panel carries as many triangles as a curved nose: the XR-40's airframe and
// parts came to ~9 million. This collapses edges in order of their quadric error (Garland & Heckbert), each vertex
// moving onto a neighbour - never to a new point - so every vertex that stays is one the bake pulled onto the field's
// surface, with the field's own normal, material id and occlusion. A collapse is refused when it would:
//   - move a vertex off its planes by more than maxErr (the quadric of every triangle it and its merged vertices had),
//   - cross a material boundary or an open edge (those vertices never move: the per-pixel material lookup and the
//     moving hull's cut stay exactly where the bake put them),
//   - join vertices whose normals differ by more than ~25 degrees (a crease) or whose occlusion differs by more than
//     maxAoStep (the cabin's baked shading),
//   - turn any triangle by more than ~45 degrees or to nothing, or break the surface's manifold (the link condition),
//   - leave an edge longer than maxEdge (0: no limit). The raster passes' logarithmic depth is set per vertex and
//     interpolated across the triangle, which on a long one close to the camera runs metres off: the far side of the
//     Q400's fuselage came through its belly in long patches from a few metres below.
// Only the triangles before triEnd take part; the rest (the cabin's fine patch) are kept as they are.
#pragma once
#include "common.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace meshsimp {
struct Quadric {
  double a[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // xx xy xz xw yy yz yw zz zw ww
  double n = 0;   // the planes summed: the cost is the mean squared distance to them
  void addPlane(double nx, double ny, double nz, double d, double w) {
    n += w;
    a[0] += w * nx * nx; a[1] += w * nx * ny; a[2] += w * nx * nz; a[3] += w * nx * d;
    a[4] += w * ny * ny; a[5] += w * ny * nz; a[6] += w * ny * d;
    a[7] += w * nz * nz; a[8] += w * nz * d; a[9] += w * d * d;
  }
  void add(const Quadric& o) { for (int i = 0; i < 10; i++) a[i] += o.a[i]; n += o.n; }
  double eval(double x, double y, double z) const {
    return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y
         + a[7] * z * z + 2 * a[8] * z + a[9];
  }
};
}

// vb: 8 floats a vertex (position, normal, material id, occlusion); ib: triangles; triEnd: the index count of the part
// that is simplified (updated). Returns the triangle count removed.
inline size_t simplifyMesh(std::vector<float>& vb, std::vector<uint32_t>& ib, size_t& triEnd, float maxErr, float maxAoStep = 0.04f, float maxEdge = 0.f) {
  using namespace meshsimp;
  const size_t nv = vb.size() / 8, nt = ib.size() / 3, ntA = std::min(triEnd, ib.size()) / 3;
  if (nv < 4 || ntA < 4) return 0;
  auto P = [&](uint32_t v) { return vec3(vb[v * 8], vb[v * 8 + 1], vb[v * 8 + 2]); };
  auto N = [&](uint32_t v) { return vec3(vb[v * 8 + 3], vb[v * 8 + 4], vb[v * 8 + 5]); };
  auto ID = [&](uint32_t v) { return vb[v * 8 + 6]; };
  auto AO = [&](uint32_t v) { return vb[v * 8 + 7]; };
  // vertex -> its triangles (dead ones dropped as they are met)
  std::vector<uint32_t> deg(nv + 1, 0);
  for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) deg[ib[t * 3 + k] + 1]++;
  std::vector<std::vector<uint32_t>> vt(nv);
  for (size_t v = 0; v < nv; v++) vt[v].reserve(deg[v + 1]);
  for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) vt[ib[t * 3 + k]].push_back((uint32_t)t);
  std::vector<uint8_t> lock(nv, 0), vDead(nv, 0), tDead(nt, 0);
  std::vector<uint32_t> ver(nv, 0);
  for (size_t t = ntA; t < nt; t++) for (int k = 0; k < 3; k++) lock[ib[t * 3 + k]] = 1;
  // the quadrics: each triangle's plane on its three vertices
  std::vector<Quadric> Q(nv);
  for (size_t t = 0; t < ntA; t++) {
    uint32_t a = ib[t * 3], b = ib[t * 3 + 1], c = ib[t * 3 + 2];
    vec3 pa = P(a), n = cross(P(b) - pa, P(c) - pa);
    double l = length(n); if (l < 1e-12) continue;
    double nx = n.x / l, ny = n.y / l, nz = n.z / l, d = -(nx * pa.x + ny * pa.y + nz * pa.z);
    for (uint32_t v : {a, b, c}) Q[v].addPlane(nx, ny, nz, d, 1.0);
  }
  // the ring of a vertex: its neighbours, each with the number of live triangles it shares with it (deduplicated by a
  // per-vertex stamp: a ring is linear in its size)
  std::vector<uint32_t> ringV, ringC, mark(nv, 0), slot(nv, 0), mark2(nv, 0);
  uint32_t gen = 0, gen2 = 0;
  auto ring = [&](uint32_t v) {
    ringV.clear(); ringC.clear(); gen++;
    auto& L = vt[v];
    size_t w = 0;
    for (size_t i = 0; i < L.size(); i++) {
      uint32_t t = L[i];
      if (tDead[t]) continue;
      L[w++] = t;
      for (int k = 0; k < 3; k++) {
        uint32_t x = ib[t * 3 + k];
        if (x == v) continue;
        if (mark[x] != gen) { mark[x] = gen; slot[x] = (uint32_t)ringV.size(); ringV.push_back(x); ringC.push_back(1); }
        else ringC[slot[x]]++;
      }
    }
    L.resize(w);
  };
  // lock the open edges' and the material boundaries' vertices (and any non-manifold edge's)
  for (uint32_t v = 0; v < nv; v++) {
    if (lock[v]) continue;
    ring(v);
    if (ringV.empty()) { lock[v] = 1; continue; }
    for (size_t j = 0; j < ringV.size(); j++) if (ringC[j] != 2 || ID(ringV[j]) != ID(v)) { lock[v] = 1; break; }
  }
  const double maxE = 0.5 * (double)maxErr * maxErr;   // (the mean over the planes: held to ~0.7 maxErr, the removed vertex itself to maxErr, so the drift of earlier removals stays inside the bound)
  // is u -> v allowed, and its cost (needs ring(u) computed: ringV)
  const size_t kMaxValence = 20;   // (no fans: a hub vertex makes slivers, and every collapse beside it slower)
  // the cheap part: same material and occlusion, no crease between, and the mean squared distance from v to every
  // plane u and v carry (their own and those of the vertices merged into them) within maxErr
  auto cheapCost = [&](uint32_t u, uint32_t v, double& cost) {
    if (vDead[v] || ID(u) != ID(v) || fabsf(AO(u) - AO(v)) > maxAoStep || dot(N(u), N(v)) < 0.9f) return false;
    vec3 pv = P(v);
    cost = std::max(0.0, Q[u].eval(pv.x, pv.y, pv.z) + Q[v].eval(pv.x, pv.y, pv.z)) / std::max(Q[u].n + Q[v].n, 1.0);
    return cost <= maxE;
  };
  auto tryCollapse = [&](uint32_t u, uint32_t v, double& cost) {
    if (!cheapCost(u, v, cost)) return false;
    vec3 pv = P(v), pu = P(u);
    // the link condition: u and v share exactly the vertices opposite their edge (two, the edge being interior); and
    // v's ring afterwards (both rings less u and v) no larger than kMaxValence
    gen2++; size_t nB = 0;
    for (uint32_t t : vt[v]) {
      if (tDead[t]) continue;
      for (int k = 0; k < 3; k++) { uint32_t x = ib[t * 3 + k]; if (x != v && mark2[x] != gen2) { mark2[x] = gen2; nB++; } }
    }
    int common = 0;
    for (uint32_t x : ringV) if (mark2[x] == gen2) common++;
    if (common != 2) return false;
    if (nB + ringV.size() - (size_t)common - 2 > kMaxValence) return false;
    if (maxEdge > 0.f) for (uint32_t x : ringV) if (x != v && length(P(x) - pv) > maxEdge) return false;   // (u's neighbours, joined to v)
    // no triangle of u's that stays turns over, or folds by more than ~45 degrees, or collapses
    for (uint32_t t : vt[u]) {
      if (tDead[t]) continue;
      uint32_t a = ib[t * 3], b = ib[t * 3 + 1], c = ib[t * 3 + 2];
      if (a == v || b == v || c == v) continue;
      vec3 pa = P(a), pb = P(b), pc = P(c);
      vec3 n0 = cross(pb - pa, pc - pa);
      if (a == u) pa = pv; else if (b == u) pb = pv; else pc = pv;
      vec3 n1 = cross(pb - pa, pc - pa);
      float l0 = length(n0), l1 = length(n1);
      if (l1 < 1e-10f || l1 < l0 * 0.02f) return false;
      if (dot(n0, n1) < 0.7f * l0 * l1) return false;
      if (fabsf(dot(pu - pv, n1)) > maxErr * l1) return false;   // (u, removed, stays within maxErr of the new surface)
    }
    return true;
  };
  // In rounds: every free vertex finds its cheapest allowed target; the collapses are then made cheapest first, each
  // only if neither of its two vertices has had its ring changed this round - so the test it passed still holds (a
  // collapse changes the triangles of its vertex's ring and nothing else, and marks that ring) - until a round finds
  // little left to do. (Score once a round, not once a collapse: the same vertices were scored again and again beside
  // every collapse.)
  struct Pick { float cost; uint32_t u, v; bool operator<(const Pick& o) const { return cost < o.cost; } };
  std::vector<Pick> picks;
  std::vector<uint32_t> touchedAt(nv, 0);   // (the round that last changed the vertex's ring)
  std::vector<uint8_t> stale(nv, 1);         // (scored again only when its ring or its target's changed: else its answer stands)
  size_t removed = 0;
  for (uint32_t round = 1; round < 64; round++) {
    picks.clear();
    for (uint32_t u = 0; u < nv; u++) {
      if (lock[u] || vDead[u] || !stale[u]) continue;
      stale[u] = 0;
      ring(u);
      double bc = 1e300; uint32_t bv = UINT32_MAX;
      for (uint32_t v : ringV) { double c; if (cheapCost(u, v, c) && c < bc) { bc = c; bv = v; } }
      if (bv == UINT32_MAX) continue;
      double c;
      if (tryCollapse(u, bv, c)) picks.push_back({(float)c, u, bv});
    }
    if (picks.empty()) break;
    std::sort(picks.begin(), picks.end());
    size_t done = 0;
    for (const Pick& k : picks) {
      if (vDead[k.u] || vDead[k.v] || touchedAt[k.u] == round || touchedAt[k.v] == round) { stale[k.u] = 1; continue; }
      ring(k.u);
      // u onto v: its triangles with v go, the rest take v; u's ring (v among it) is changed for this round
      for (uint32_t x : ringV) { touchedAt[x] = round; stale[x] = 1; }
      touchedAt[k.u] = round;
      for (uint32_t t : vt[k.u]) {
        if (tDead[t]) continue;
        uint32_t* tri = &ib[t * 3];
        if (tri[0] == k.v || tri[1] == k.v || tri[2] == k.v) { tDead[t] = 1; removed++; continue; }
        for (int q = 0; q < 3; q++) if (tri[q] == k.u) tri[q] = k.v;
        vt[k.v].push_back(t);
      }
      vt[k.u].clear();
      Q[k.v].add(Q[k.u]);
      vDead[k.u] = 1;
      done++;
    }
    if (done * 200 < picks.size() + 1) break;   // (a round that did almost nothing: what is left is near the bound)
  }
  // compact: the live vertices the live triangles use, the simplified triangles first, the kept ones after
  std::vector<uint32_t> remap(nv, UINT32_MAX);
  std::vector<float> vb2; std::vector<uint32_t> ib2; vb2.reserve(vb.size() / 2); ib2.reserve(ib.size() / 2);
  auto emit = [&](size_t t) {
    for (int k = 0; k < 3; k++) {
      uint32_t v = ib[t * 3 + k];
      if (remap[v] == UINT32_MAX) { remap[v] = (uint32_t)(vb2.size() / 8); vb2.insert(vb2.end(), vb.begin() + v * 8, vb.begin() + v * 8 + 8); }
      ib2.push_back(remap[v]);
    }
  };
  for (size_t t = 0; t < ntA; t++) if (!tDead[t]) emit(t);
  triEnd = ib2.size();
  for (size_t t = ntA; t < nt; t++) emit(t);
  vb.swap(vb2); ib.swap(ib2);
  return removed;
}
