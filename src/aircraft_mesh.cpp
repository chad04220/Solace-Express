// Solace Express - the aircraft mesh (renderer rebuild R2, docs/RENDERER_REBUILD.md): the part of the airframe that
// never moves, baked from its distance field as a mesh, so the raster renderer rasterizes it and marches the field only
// where something moves.
//
// The field is sampled on the GPU with the hull bake's program (aircraft_hull.cpp), in every state of the gear, flaps,
// steering and controls that the hull bake sweeps. A 6.25 cm cell whose distance changes between states (a flap, a
// gear leg and its doors, a control surface, the yoke, the pedals) is "moving"; the rest of the surface band is
// static and gets the mesh: surface nets on a 1.56 cm lattice, one vertex per lattice cube the surface crosses, placed
// on the surface with the field's normal, material id and (in the cabin) ambient occlusion. The moving cells, grown by
// one 0.25 m voxel, make a hull (hull_mesh.h) that the objects pass starts its march on, exactly as the full hull did
// for the whole airframe; everywhere else the mesh is the airframe and no pixel marches. Meshes are cached on disk by
// model, view and shader fingerprint.
#include "renderer.h"
#include "shaders.h"
#include "hull_mesh.h"
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace {
inline int64_t key3(int x, int y, int z) { return ((int64_t)(x + 4096) << 42) | ((int64_t)(y + 4096) << 21) | (int64_t)(z + 4096); }
const float kH = kS2 / 4.f;   // the lattice: 1.5625 cm
const uint32_t kMeshMagic = 0x4d455348u + 6;   // (bump with the format)
}

bool Renderer::compilePlaneMesh() {
  std::string e;
  progPlaneMesh = linkProgramCached(planeMeshVSAssembly(""), planeMeshFSAssembly(""), e);
  if (!progPlaneMesh) { error = "Aircraft mesh shader: " + e; return false; }
  // the depth pre-pass; with uScrSkip the fragments at or behind a screen (texScrDepth) are dropped: the screens are holes
  progPlaneMeshDepth = linkProgramCached(planeMeshVSAssembly(""), "#version 330 core\nflat in float vId; in vec3 vW; in vec3 vN; in float vIdS; in float vAo; uniform int uScrSkip; uniform sampler2D uScrDepth;\nvoid main(){ if (uScrSkip == 1 && gl_FragCoord.z >= texelFetch(uScrDepth, ivec2(gl_FragCoord.xy), 0).r - 2e-7) discard; }\n", e);
  if (!progPlaneMeshDepth) { error = "Aircraft mesh depth shader: " + e; return false; }
  // the screens alone, depth only (the bomb camera's pane excepted while it shows a picture)
  progPlaneMeshScr = linkProgramCached(planeMeshVSAssembly(""), "#version 330 core\nflat in float vId; in vec3 vW; in vec3 vN; in float vIdS; in float vAo; uniform int uBombPane;\nvoid main(){ int mid = int(vId + 0.5); bool scr = (mid >= 41 && mid <= 43) || (mid >= 61 && mid <= 63); if (!scr || (uBombPane == 1 && mid == 61)) discard; }\n", e);
  if (!progPlaneMeshScr) { error = "Aircraft mesh screen shader: " + e; return false; }
  return true;
}

// every aircraft but a cloaked XR-40 and a wreck
bool Renderer::planeMeshWanted(const FrameParams& fp) const {
  const PlaneVisual& pv = fp.plane;
  bool cloaked = (int)(pv.M[2] + 0.5f) == 6 && pv.wr[4][3] > 0.001f;   // (the cloak sees through the skin: that frame marches as before)
  return !meshOff && progPlaneMesh && progHullBake && pv.on && fp.wreck.pieces == 0 && !cloaked;
}

void Renderer::bakePlaneMesh(const FrameParams& fp, int slot, uint64_t key) {
  bakeCount++;
  const PlaneVisual& pv = fp.plane;
  const float* M = pv.M;
  const bool inside = slot == 1;
  PlaneMesh& PM = planeMeshes[key];
  PM.key = key; PM.movKey = key ^ 0x4d4f56494e47ull;
  std::vector<float> vb, hullTri; std::vector<uint32_t> ib;
  // the cache
  std::string path;
  if (!g_shaderCacheDir.empty()) {
    static const std::string stamp = shaderCacheStamp();
    char name[64]; snprintf(name, sizeof name, "/mesh_%016llx_%s.bin", (unsigned long long)key, stamp.c_str());
    path = g_shaderCacheDir + name;
    if (FILE* f = fopen(path.c_str(), "rb")) {
      uint32_t hdr[4] = {0, 0, 0, 0};
      bool ok = fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == kMeshMagic;
      if (ok) {   // (the counts must add up to the file: a damaged header is not an allocation)
        long long here = ftell(f); fseek(f, 0, SEEK_END); long long size = ftell(f); fseek(f, (long)here, SEEK_SET);
        ok = here == (long long)sizeof hdr && size == here + 4ll * ((long long)hdr[1] + hdr[2] + hdr[3]) && hdr[1] % 8 == 0 && hdr[2] % 3 == 0 && hdr[3] % 9 == 0;
      }
      if (ok) { vb.resize(hdr[1]); ib.resize(hdr[2]); hullTri.resize(hdr[3]); }
      ok = ok && (vb.empty() || fread(vb.data(), sizeof(float), vb.size(), f) == vb.size()) && (ib.empty() || fread(ib.data(), sizeof(uint32_t), ib.size(), f) == ib.size())
           && (hullTri.empty() || fread(hullTri.data(), sizeof(float), hullTri.size(), f) == hullTri.size());
      fclose(f);
      if (!ok) { vb.clear(); ib.clear(); hullTri.clear(); }
      else if (getenv("HULLDBG")) printf("mesh %s: from the cache (%zu vertices)\n", inside ? "cockpit" : "outside", vb.size() / 8);
    }
  }
  if (ib.empty()) {
    // ---- the field's states, on the bake program (bound by the caller)
    std::vector<HullState> st = hullStateList(M, inside);
    const int ns = (int)st.size();
    std::vector<float> sps(128 * 4, 0.f), sct(128 * 4, 0.f), swr(128 * 4, 0.f), swr2(128 * 4, 0.f);
    for (int i = 0; i < ns; i++) for (int c = 0; c < 4; c++) { sps[i * 4 + c] = st[i].ps[c]; sct[i * 4 + c] = st[i].ctl[c]; swr[i * 4 + c] = st[i].wr[c]; swr2[i * 4 + c] = st[i].wr2[c]; }
    glUniform1i(U(progHullBake, "uHStN"), ns);
    glUniform4fv(U(progHullBake, "uHStPS"), 128, sps.data());
    glUniform4fv(U(progHullBake, "uHStCtl"), 128, sct.data());
    glUniform4fv(U(progHullBake, "uHStWr"), 128, swr.data());
    glUniform4fv(U(progHullBake, "uHStWr2"), 128, swr2.data());
    auto mode = [&](int m, int s) { glUniform1i(U(progHullBake, "uHMode"), m); glUniform1i(U(progHullBake, "uHState"), s); };
    const float slack = 1.3f;   // the field may overstate distances by up to ~25%
    float L = M[0], span = M[9 * 4];
    float br = std::max(L, span * 2.f) * 0.55f + 1.5f;
    int n0 = 2 * (int)ceilf(br / kS0); float org = -n0 * 0.5f * kS0;
    int n1 = n0 * 4, n2 = n1 * 4;
    auto centre = [&](float s, int i, int j, int k) { return vec3(org + (i + 0.5f) * s, org + (j + 0.5f) * s, org + (k + 0.5f) * s); };
    std::vector<vec3> pts; std::vector<float> d, d4;
    // ---- level 0 and 1: where the airframe is at all, in any state (the least distance over the states)
    mode(0, 0);
    for (int k = 0; k < n0; k++) for (int j = 0; j < n0; j++) for (int i = 0; i < n0; i++) pts.push_back(centre(kS0, i, j, k));
    hullEval(pts, d);
    float keep0 = slack * (halfDiag(kS0) + halfDiag(kS1) + halfDiag(kS2)) + 0.1f;
    std::vector<int> c1; pts.clear();
    for (int k = 0; k < n0; k++) for (int j = 0; j < n0; j++) for (int i = 0; i < n0; i++) {
      if (d[((size_t)k * n0 + j) * n0 + i] >= keep0) continue;
      for (int c = 0; c < 64; c++) { int ii = i * 4 + (c & 3), jj = j * 4 + ((c >> 2) & 3), kk = k * 4 + (c >> 4); c1.push_back((kk * n1 + jj) * n1 + ii); pts.push_back(centre(kS1, ii, jj, kk)); }
    }
    hullEval(pts, d);
    float keep1 = slack * (halfDiag(kS1) + halfDiag(kS2)) + 0.1f;
    // ---- level 2: the 6.25 cm cells the surface can pass through in some state (least distance under the band,
    // greatest above it: not deep inside in every state)
    std::vector<int> c2; pts.clear();
    for (size_t q = 0; q < c1.size(); q++) {
      if (d[q] >= keep1) continue;
      int id = c1[q], ii = id % n1, jj = (id / n1) % n1, kk = id / (n1 * n1);
      for (int c = 0; c < 64; c++) { int i2 = ii * 4 + (c & 3), j2 = jj * 4 + ((c >> 2) & 3), k2 = kk * 4 + (c >> 4); c2.push_back((k2 * n2 + j2) * n2 + i2); pts.push_back(centre(kS2, i2, j2, k2)); }
    }
    const float band = slack * halfDiag(kS2) + 2.5f * kH;
    std::vector<float> dMin, dMax;
    hullEval(pts, dMin);
    mode(4, 0); hullEval(pts, dMax);
    std::vector<int> cand; std::vector<vec3> candP;
    for (size_t q = 0; q < c2.size(); q++) if (dMin[q] < band && dMax[q] > -band) { cand.push_back(c2[q]); candP.push_back(pts[q]); }
    // ---- which of them move: the distance in some state differs from the rest state's
    std::vector<float> dRest; mode(1, 0); hullEval(candP, dRest);
    std::vector<uint8_t> moving(cand.size(), 0);
    for (int s = 1; s < ns; s++) {
      mode(1, s); hullEval(candP, d);
      for (size_t q = 0; q < cand.size(); q++) if (fabsf(d[q] - dRest[q]) > 0.008f) moving[q] = 1;
    }
    // ---- and in the cabin, what is too thin for the lattice: a plate or a rod under ~2.5 cells through (the sun
    // visors, a grab handle, a lens rim) comes off the 1.56 cm lattice as a wavy, notched shape. Those cells, and a
    // ring of one cell round them, are meshed again on a lattice twice as fine, as a patch laid over the first mesh:
    // the first leaves the thin cells out and sinks its ring 3 mm into the wall, so the patch wins the depth test
    // there without a flicker and no crack opens between the two (the exterior keeps its trailing edges and
    // antennas as they are: a cabin is seen from a metre, an airframe from twenty)
    std::vector<uint8_t> thin(cand.size(), 0); int nThin = 0;
    if (inside) {
      std::vector<float> cn; mode(3, 0); hullEval4(candP, cn);
      std::vector<vec3> behind(cand.size());
      for (size_t q = 0; q < cand.size(); q++) { vec3 n(cn[q * 4], cn[q * 4 + 1], cn[q * 4 + 2]); behind[q] = candP[q] - n * (dRest[q] + 2.f * kH); }
      std::vector<float> dIn; mode(1, 0); hullEval(behind, dIn);
      for (size_t q = 0; q < cand.size(); q++) if (!moving[q] && fabsf(dRest[q]) < band && dIn[q] > -0.5f * kH) { thin[q] = 1; nThin++; }
    }
    std::unordered_map<int, uint8_t> cellMov;   // level-2 index -> moving
    for (size_t q = 0; q < cand.size(); q++) cellMov[cand[q]] = moving[q];
    // ---- the static band cells: the coarse mesh's (all but the thin ones; 2: in the ring round a thin cell), and the
    // fine patch's (the thin ones and their ring)
    std::unordered_set<int> thinSet;
    for (size_t q = 0; q < cand.size(); q++) if (thin[q]) thinSet.insert(cand[q]);
    auto nearThin = [&](int id) {
      int i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
      for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int i = i2 + dx, j = j2 + dy, k = k2 + dz;
        if (i < 0 || j < 0 || k < 0 || i >= n2 || j >= n2 || k >= n2) continue;
        if (thinSet.count((k * n2 + j) * n2 + i)) return true;
      }
      return false;
    };
    std::vector<std::pair<int, uint8_t>> coarseCells, fineCells;
    for (size_t q = 0; q < cand.size(); q++) {
      if (moving[q] || fabsf(dRest[q]) >= band) continue;
      bool ring = !thin[q] && nearThin(cand[q]);
      if (!thin[q]) coarseCells.push_back({cand[q], (uint8_t)(ring ? 2 : 1)});
      if (thin[q] || ring) fineCells.push_back({cand[q], 1});
    }
    // ---- surface nets over a set of cells on a lattice of `sub` steps per cell: one vertex per lattice cube the
    // surface crosses, at the mean of its edge crossings, pulled onto the surface; a quad round every lattice edge the
    // surface crosses, between the four cubes that share it, wound with the field's normal
    size_t nLattice = 0;
    auto nets = [&](const std::vector<std::pair<int, uint8_t>>& cells, int sub, float sink) {
      const float h = kS2 / sub;
      std::unordered_map<int64_t, int> corner;   // lattice coords -> sample index
      std::vector<vec3> cpts;
      for (auto& ce : cells) {
        int id = ce.first, i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
        for (int z = 0; z <= sub; z++) for (int y = 0; y <= sub; y++) for (int x = 0; x <= sub; x++) {
          int lx = i2 * sub + x, ly = j2 * sub + y, lz = k2 * sub + z;
          int64_t kk = key3(lx, ly, lz);
          if (corner.count(kk)) continue;
          corner[kk] = (int)cpts.size();
          cpts.push_back(vec3(org + lx * h, org + ly * h, org + lz * h));
        }
      }
      nLattice += cpts.size();
      std::vector<float> cv; mode(1, 0); hullEval(cpts, cv);
      auto cval = [&](int lx, int ly, int lz, float& v) { auto it = corner.find(key3(lx, ly, lz)); if (it == corner.end()) return false; v = cv[it->second]; return true; };
      std::unordered_map<int64_t, int> cubeV;   // cube coords -> vertex (local)
      std::vector<vec3> vpos; std::vector<int> vcube; std::vector<uint8_t> vsink;   // (each vertex's cube, and whether its cell sinks)
      const int ce[12][2] = {{0, 1}, {1, 3}, {2, 3}, {0, 2}, {4, 5}, {5, 7}, {6, 7}, {4, 6}, {0, 4}, {1, 5}, {3, 7}, {2, 6}};   // corner bit x + 2y + 4z
      for (auto& cl : cells) {
        int id = cl.first, i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
        for (int z = 0; z < sub; z++) for (int y = 0; y < sub; y++) for (int x = 0; x < sub; x++) {
          int cx = i2 * sub + x, cy = j2 * sub + y, cz = k2 * sub + z;
          float v[8]; bool ok = true;
          for (int c = 0; c < 8 && ok; c++) ok = cval(cx + (c & 1), cy + ((c >> 1) & 1), cz + (c >> 2), v[c]);
          if (!ok) continue;
          int neg = 0; for (int c = 0; c < 8; c++) if (v[c] < 0.f) neg++;
          if (neg == 0 || neg == 8) continue;
          vec3 sum; int cnt = 0;
          for (int e = 0; e < 12; e++) {
            int a = ce[e][0], b = ce[e][1];
            if ((v[a] < 0.f) == (v[b] < 0.f)) continue;
            float t = v[a] / (v[a] - v[b]);
            vec3 pa(org + (cx + (a & 1)) * h, org + (cy + ((a >> 1) & 1)) * h, org + (cz + (a >> 2)) * h);
            vec3 pb(org + (cx + (b & 1)) * h, org + (cy + ((b >> 1) & 1)) * h, org + (cz + (b >> 2)) * h);
            sum = sum + pa + (pb - pa) * t; cnt++;
          }
          cubeV[key3(cx, cy, cz)] = (int)vpos.size();
          vpos.push_back(sum * (1.f / cnt)); vcube.push_back(cx); vcube.push_back(cy); vcube.push_back(cz); vsink.push_back(cl.second == 2);
        }
      }
      // the vertices' normals, material ids and cabin ambient occlusion from the field; each is pulled onto the
      // surface in three steps (the field is a bound where parts are cut from one another, so one pull by its value
      // falls short by a varying fraction of a cell), and stays within its own cube (at a sharp edge the normal is one
      // face's or the other's: a vertex pulled a whole cell onto the wrong face zigzagged the edge)
      std::vector<float> vn, d4;
      for (int it = 0; it < 3; it++) {
        mode(3, 0); hullEval4(vpos, vn);
        mode(2, 0); hullEval4(vpos, d4);
        for (size_t q = 0; q < vpos.size(); q++) {
          vec3 p = vpos[q] - vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * std::max(-h, std::min(h, d4[q * 4]));
          const float m = 0.25f * h;
          float lo[3] = {org + vcube[q * 3] * h - m, org + vcube[q * 3 + 1] * h - m, org + vcube[q * 3 + 2] * h - m};
          p.x = std::max(lo[0], std::min(lo[0] + h + 2.f * m, p.x)); p.y = std::max(lo[1], std::min(lo[1] + h + 2.f * m, p.y)); p.z = std::max(lo[2], std::min(lo[2] + h + 2.f * m, p.z));
          vpos[q] = p;
        }
      }
      mode(3, 0); hullEval4(vpos, vn);
      mode(2, 0); hullEval4(vpos, d4);
      if (getenv("HULLDBG")) {   // how far off the surface the vertices still sit
        int n2mm = 0, n5mm = 0; float mx = 0.f;
        for (size_t q = 0; q < vpos.size(); q++) { float a = fabsf(d4[q * 4]); mx = std::max(mx, a); if (a > 0.002f) n2mm++; if (a > 0.005f) n5mm++; }
        printf("mesh %s (lattice %.2f cm): %zu vertices off the surface: %d > 2 mm, %d > 5 mm, max %.1f mm\n", inside ? "cockpit" : "outside", h * 100.f, vpos.size(), n2mm, n5mm, mx * 1000.f);
      }
      // the ring round the fine patch sinks into the wall (along the normal, away from the cabin), so the patch is in
      // front of it wherever the two lay the same surface
      for (size_t q = 0; q < vpos.size(); q++) if (vsink[q]) vpos[q] = vpos[q] - vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * sink;
      const int base = (int)(vb.size() / 8);
      vb.resize(vb.size() + vpos.size() * 8);
      for (size_t q = 0; q < vpos.size(); q++) {
        float* o = &vb[(base + q) * 8];
        o[0] = vpos[q].x; o[1] = vpos[q].y; o[2] = vpos[q].z; o[3] = vn[q * 4]; o[4] = vn[q * 4 + 1]; o[5] = vn[q * 4 + 2];
        o[6] = floorf(d4[q * 4 + 1] + 0.5f); o[7] = inside ? d4[q * 4 + 2] : 1.f;
      }
      auto cube = [&](int x, int y, int z, int& out) { auto it = cubeV.find(key3(x, y, z)); if (it == cubeV.end()) return false; out = it->second; return true; };
      auto quad = [&](int a, int b, int c, int dq) {
        vec3 nAvg(vn[a * 4] + vn[b * 4] + vn[c * 4] + vn[dq * 4], vn[a * 4 + 1] + vn[b * 4 + 1] + vn[c * 4 + 1] + vn[dq * 4 + 1], vn[a * 4 + 2] + vn[b * 4 + 2] + vn[c * 4 + 2] + vn[dq * 4 + 2]);
        vec3 fn = cross(vpos[b] - vpos[a], vpos[c] - vpos[a]) + cross(vpos[c] - vpos[a], vpos[dq] - vpos[a]);
        if (dot(fn, nAvg) < 0.f) std::swap(b, dq);
        ib.push_back(base + a); ib.push_back(base + b); ib.push_back(base + c); ib.push_back(base + a); ib.push_back(base + c); ib.push_back(base + dq);
      };
      for (auto& kv : cubeV) {
        int64_t k = kv.first;
        int cx = (int)((k >> 42) & 0x1fffff) - 4096, cy = (int)((k >> 21) & 0x1fffff) - 4096, cz = (int)(k & 0x1fffff) - 4096;
        float v0, vx, vy, vz;
        if (!cval(cx, cy, cz, v0)) continue;
        int a = kv.second, b, c, dq;
        if (cval(cx + 1, cy, cz, vx) && (v0 < 0.f) != (vx < 0.f) && cube(cx, cy - 1, cz, b) && cube(cx, cy - 1, cz - 1, c) && cube(cx, cy, cz - 1, dq)) quad(a, b, c, dq);
        if (cval(cx, cy + 1, cz, vy) && (v0 < 0.f) != (vy < 0.f) && cube(cx, cy, cz - 1, b) && cube(cx - 1, cy, cz - 1, c) && cube(cx - 1, cy, cz, dq)) quad(a, b, c, dq);
        if (cval(cx, cy, cz + 1, vz) && (v0 < 0.f) != (vz < 0.f) && cube(cx - 1, cy, cz, b) && cube(cx - 1, cy - 1, cz, c) && cube(cx, cy - 1, cz, dq)) quad(a, b, c, dq);
      }
    };
    nets(coarseCells, 4, 0.003f);
    if (!fineCells.empty()) nets(fineCells, 8, 0.f);
    // ---- the hull of what moves: the moving 6.25 cm cells, each grown by one cell, as faces on the fine lattice (a
    // 0.25 m margin round the yoke's sweep reached the pilot's eye and every cockpit ray started inside the hull: all
    // of them marched; on the fine lattice the hull is the yoke's, the levers' and the pedals' own)
    std::vector<uint8_t> mv((size_t)n1 * n1 * n1, 0); std::unordered_map<int, uint64_t> mvMask;
    auto setFine = [&](int i2, int j2, int k2) {
      if (i2 < 0 || j2 < 0 || k2 < 0 || i2 >= n2 || j2 >= n2 || k2 >= n2) return;
      mvMask[((k2 / 4) * n1 + j2 / 4) * n1 + i2 / 4] |= 1ull << ((i2 & 3) + 4 * (j2 & 3) + 16 * (k2 & 3));
    };
    for (size_t q = 0; q < cand.size(); q++) {
      if (!moving[q]) continue;
      int id = cand[q], i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
      for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) setFine(i2 + dx, j2 + dy, k2 + dz);
    }
    for (auto& kv : mvMask) mv[kv.first] = kv.second == ~0ull ? 1 : 2;
    hullFaces(n1, org, kS2, true, mv, mvMask, hullTri);
    {   // the eye against the moving hull (its cell and the neighbours), for the record
      int ei = (int)floorf((M[22 * 4] - org) / kS2), ej = (int)floorf((M[22 * 4 + 1] - org) / kS2), ek = (int)floorf((M[22 * 4 + 2] - org) / kS2);
      bool in = false;
      for (int dz = -1; dz <= 1 && !in; dz++) for (int dy = -1; dy <= 1 && !in; dy++) for (int dx = -1; dx <= 1 && !in; dx++) {
        int i2 = ei + dx, j2 = ej + dy, k2 = ek + dz;
        if (i2 < 0 || j2 < 0 || k2 < 0 || i2 >= n2 || j2 >= n2 || k2 >= n2) continue;
        auto it = mvMask.find(((k2 / 4) * n1 + j2 / 4) * n1 + i2 / 4);
        in = it != mvMask.end() && ((it->second >> ((i2 & 3) + 4 * (j2 & 3) + 16 * (k2 & 3))) & 1);
      }
      hullTri.push_back(in ? 1.f : 0.f);   // (carried at the end of the hull's floats: the cache keeps it)
    }
    if (getenv("HULLDBG")) {
      int nmov = 0; for (uint8_t m : moving) nmov += m;
      printf("mesh %s: %d states, %zu band cells (%d moving, %d thin), %zu lattice samples, %zu vertices, %zu triangles, moving hull %zu triangles\n",
             inside ? "cockpit" : "outside", ns, cand.size(), nmov, nThin, nLattice, vb.size() / 8, ib.size() / 3, hullTri.size() / 9);
    }
    if (!path.empty()) {   // (the eye flag rides at the end of the hull's floats)
      if (FILE* f = fopen(path.c_str(), "wb")) {
        uint32_t hdr[4] = {kMeshMagic, (uint32_t)vb.size(), (uint32_t)ib.size(), (uint32_t)hullTri.size()};
        fwrite(hdr, sizeof hdr, 1, f);
        if (!vb.empty()) fwrite(vb.data(), sizeof(float), vb.size(), f);
        if (!ib.empty()) fwrite(ib.data(), sizeof(uint32_t), ib.size(), f);
        if (!hullTri.empty()) fwrite(hullTri.data(), sizeof(float), hullTri.size(), f);
        fclose(f);
      }
    }
  }
  // ---- to the GPU: the mesh, and the moving hull under its own key in the hull table
  if (!PM.vao) glGenVertexArrays(1, &PM.vao);
  if (!PM.vbo) glGenBuffers(1, &PM.vbo);
  if (!PM.ibo) glGenBuffers(1, &PM.ibo);
  glBindVertexArray(PM.vao);
  glBindBuffer(GL_ARRAY_BUFFER, PM.vbo);
  glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(float), vb.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 32, (void*)24);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 32, (void*)28);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, PM.ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib.size() * sizeof(uint32_t), ib.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  PM.idx = (int)ib.size();
  PM.eyeInMov = !hullTri.empty() && hullTri.back() > 0.5f;
  if (!hullTri.empty()) hullTri.pop_back();
  HullMesh& H = hulls[PM.movKey];
  if (!H.vbo) glGenBuffers(1, &H.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glBufferData(GL_ARRAY_BUFFER, hullTri.size() * sizeof(float), hullTri.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  H.verts = (int)hullTri.size() / 3; H.key = PM.movKey; H.ok = true;
  PM.ok = true;
}

// A static airframe into the G-buffer (the target, viewport and depth test are the caller's): the player's aircraft
// (trafK -1) or traffic aircraft k, placed by rot / pos
void Renderer::drawPlaneMesh(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK) {
  if (!pm.ok || !pm.idx) return;
  mat4 vp = viewProj(fp, 0.01f, 2000.f);   // (a near plane at 1 cm: in the cockpit the panel is closer than 0.5 m)
  const float logC = 2.f / log2f(40000.f + 1.f);
  glBindVertexArray(pm.vao);
  // depth first, with nothing shaded: the skin's far side, the far wing and the cabin's hidden surfaces never run the
  // material shader
  static const bool noPre = getenv("MESHNOPRE") != nullptr;   // (debug: no depth pre-pass)
  // the research craft's displays as windows: the cabin is sealed, so a screen must be a hole through the whole
  // airframe, not a missing pane with the pod's structure behind it. The screens' depth goes into texScrDepth first;
  // the pre-pass and the material pass then drop every fragment at or behind a screen, and the world drawn before
  // the airframe stays (the bomb camera's pane keeps its picture while it has one)
  const bool scrSkip = screenWindows && trafK < 0 && fp.plane.PS[3] > 0.5f && (int)(fp.plane.M[2] + 0.5f) >= 5 && progPlaneMeshScr;
  if (scrSkip) {
    if (!texScrDepth || scrDepthW < rw || scrDepthH < rh) {
      int w = std::max(rw, scrDepthW), h = std::max(rh, scrDepthH);
      if (texScrDepth) glDeleteTextures(1, &texScrDepth);
      glGenTextures(1, &texScrDepth); glBindTexture(GL_TEXTURE_2D, texScrDepth);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindTexture(GL_TEXTURE_2D, 0);
      if (!fboScrDepth) glGenFramebuffers(1, &fboScrDepth);
      glBindFramebuffer(GL_FRAMEBUFFER, fboScrDepth);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texScrDepth, 0);
      { GLenum none = GL_NONE; glDrawBuffers(1, &none); } glReadBuffer(GL_NONE);
      scrDepthW = w; scrDepthH = h;
    }
    GLint prevFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fboScrDepth);
    glViewport(0, 0, rw, rh);
    glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(progPlaneMeshScr);
    glUniformMatrix4fv(U(progPlaneMeshScr, "uVP"), 1, GL_FALSE, vp.m);
    glUniform2f(U(progPlaneMeshScr, "uJit"), jitX, jitY);
    glUniform1f(U(progPlaneMeshScr, "uLogC"), logC);
    glUniformMatrix3fv(U(progPlaneMeshScr, "uRot"), 1, GL_FALSE, rot);
    glUniform3f(U(progPlaneMeshScr, "uPos"), pos.x, pos.y, pos.z);
    glUniform1i(U(progPlaneMeshScr, "uBombPane"), fp.fx.feed[3] > 0.5f ? 1 : 0);
    glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, gb);
    glViewport(0, 0, rw, rh);
  }
  if (!noPre) {
    glUseProgram(progPlaneMeshDepth);
    glUniformMatrix4fv(U(progPlaneMeshDepth, "uVP"), 1, GL_FALSE, vp.m);
    glUniform2f(U(progPlaneMeshDepth, "uJit"), jitX, jitY);
    glUniform1f(U(progPlaneMeshDepth, "uLogC"), logC);
    glUniformMatrix3fv(U(progPlaneMeshDepth, "uRot"), 1, GL_FALSE, rot);
    glUniform3f(U(progPlaneMeshDepth, "uPos"), pos.x, pos.y, pos.z);
    glUniform1i(U(progPlaneMeshDepth, "uScrSkip"), scrSkip ? 1 : 0);
    glActiveTexture(GL_TEXTURE0 + 29); glBindTexture(GL_TEXTURE_2D, scrSkip ? texScrDepth : 0); glUniform1i(U(progPlaneMeshDepth, "uScrDepth"), 29);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  // then the materials on exactly the nearest surface
  setRT(progPlaneMesh, fp);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  glUniformMatrix4fv(U(progPlaneMesh, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(U(progPlaneMesh, "uJit"), jitX, jitY);
  glUniform1f(U(progPlaneMesh, "uLogC"), logC);
  glUniformMatrix3fv(U(progPlaneMesh, "uRot"), 1, GL_FALSE, rot);
  glUniform3f(U(progPlaneMesh, "uPos"), pos.x, pos.y, pos.z);
  glUniform1i(U(progPlaneMesh, "uMeshTraffic"), trafK);
  glUniform1i(U(progPlaneMesh, "uScrSkip"), scrSkip ? 1 : 0);
  glActiveTexture(GL_TEXTURE0 + 29); glBindTexture(GL_TEXTURE_2D, scrSkip ? texScrDepth : 0); glUniform1i(U(progPlaneMesh, "uScrDepth"), 29);
  if (!noPre) { glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); }
  glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
  glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

uint64_t Renderer::trafficModelKey(const float* t) const {
  uint64_t h = 1469598103934665603ull;
  const uint8_t* b = (const uint8_t*)t;
  for (size_t i = 0; i < sizeof(float) * 96; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
