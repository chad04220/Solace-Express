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

namespace {
inline int64_t key3(int x, int y, int z) { return ((int64_t)(x + 4096) << 42) | ((int64_t)(y + 4096) << 21) | (int64_t)(z + 4096); }
const float kH = kS2 / 4.f;   // the lattice: 1.5625 cm
const uint32_t kMeshMagic = 0x4d455348u + 3;   // (bump with the format)
}

bool Renderer::compilePlaneMesh() {
  std::string e;
  progPlaneMesh = linkProgramCached(planeMeshVSAssembly(""), planeMeshFSAssembly(""), e);
  if (!progPlaneMesh) { error = "Aircraft mesh shader: " + e; return false; }
  return true;
}

// the light aircraft (the research jets' fields animate differently and are marched as before)
bool Renderer::planeMeshWanted(const FrameParams& fp) const {
  const PlaneVisual& pv = fp.plane;
  return !meshOff && progPlaneMesh && progHullBake && pv.on && fp.wreck.pieces == 0 && pv.M[2] < 4.5f;
}

void Renderer::bakePlaneMesh(const FrameParams& fp, int slot, uint64_t key) {
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
      if (ok) { vb.resize(hdr[1]); ib.resize(hdr[2]); hullTri.resize(hdr[3]); }
      ok = ok && (vb.empty() || fread(vb.data(), sizeof(float), vb.size(), f) == vb.size()) && (ib.empty() || fread(ib.data(), sizeof(uint32_t), ib.size(), f) == ib.size())
           && (hullTri.empty() || fread(hullTri.data(), sizeof(float), hullTri.size(), f) == hullTri.size());
      fclose(f);
      if (!ok) { vb.clear(); ib.clear(); hullTri.clear(); }
    }
  }
  if (ib.empty()) {
    // ---- the field's states, on the bake program (bound by the caller)
    std::vector<HullState> st = hullStateList(M, inside);
    const int ns = (int)st.size();
    std::vector<float> sps(128 * 4, 0.f), sct(128 * 4, 0.f);
    for (int i = 0; i < ns; i++) for (int c = 0; c < 4; c++) { sps[i * 4 + c] = st[i].ps[c]; sct[i * 4 + c] = st[i].ctl[c]; }
    glUniform1i(U(progHullBake, "uHStN"), ns);
    glUniform4fv(U(progHullBake, "uHStPS"), 128, sps.data());
    glUniform4fv(U(progHullBake, "uHStCtl"), 128, sct.data());
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
    std::unordered_map<int, uint8_t> cellMov;   // level-2 index -> moving
    for (size_t q = 0; q < cand.size(); q++) cellMov[cand[q]] = moving[q];
    // ---- the lattice corners of the static band cells, each once
    std::unordered_map<int64_t, int> corner;   // lattice coords -> sample index
    std::vector<vec3> cpts;
    std::vector<int> staticCells;
    for (size_t q = 0; q < cand.size(); q++) {
      if (moving[q] || fabsf(dRest[q]) >= band) continue;
      staticCells.push_back(cand[q]);
      int id = cand[q], i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
      for (int z = 0; z <= 4; z++) for (int y = 0; y <= 4; y++) for (int x = 0; x <= 4; x++) {
        int lx = i2 * 4 + x, ly = j2 * 4 + y, lz = k2 * 4 + z;
        int64_t kk = key3(lx, ly, lz);
        if (corner.count(kk)) continue;
        corner[kk] = (int)cpts.size();
        cpts.push_back(vec3(org + lx * kH, org + ly * kH, org + lz * kH));
      }
    }
    std::vector<float> cv; mode(1, 0); hullEval(cpts, cv);
    auto cval = [&](int lx, int ly, int lz, float& v) { auto it = corner.find(key3(lx, ly, lz)); if (it == corner.end()) return false; v = cv[it->second]; return true; };
    // ---- surface nets: one vertex per lattice cube the surface crosses, at the mean of its edge crossings
    std::unordered_map<int64_t, int> cubeV;   // cube coords -> vertex
    std::vector<vec3> vpos;
    const int ce[12][2] = {{0, 1}, {1, 3}, {2, 3}, {0, 2}, {4, 5}, {5, 7}, {6, 7}, {4, 6}, {0, 4}, {1, 5}, {3, 7}, {2, 6}};   // corner bit x + 2y + 4z
    for (int id : staticCells) {
      int i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
      for (int z = 0; z < 4; z++) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        int cx = i2 * 4 + x, cy = j2 * 4 + y, cz = k2 * 4 + z;
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
          vec3 pa(org + (cx + (a & 1)) * kH, org + (cy + ((a >> 1) & 1)) * kH, org + (cz + (a >> 2)) * kH);
          vec3 pb(org + (cx + (b & 1)) * kH, org + (cy + ((b >> 1) & 1)) * kH, org + (cz + (b >> 2)) * kH);
          sum = sum + pa + (pb - pa) * t; cnt++;
        }
        cubeV[key3(cx, cy, cz)] = (int)vpos.size();
        vpos.push_back(sum * (1.f / cnt));
      }
    }
    // ---- the vertices' normals, material ids and cabin ambient occlusion from the field; each is pulled onto the surface
    std::vector<float> vn; mode(3, 0); hullEval4(vpos, vn);
    for (size_t q = 0; q < vpos.size(); q++) { vec3 n(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]); vn[q * 4] = n.x; vn[q * 4 + 1] = n.y; vn[q * 4 + 2] = n.z; }
    mode(2, 0); hullEval4(vpos, d4);
    for (size_t q = 0; q < vpos.size(); q++) vpos[q] = vpos[q] - vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * std::max(-kH, std::min(kH, d4[q * 4]));
    vb.resize(vpos.size() * 8);
    for (size_t q = 0; q < vpos.size(); q++) {
      float* o = &vb[q * 8];
      o[0] = vpos[q].x; o[1] = vpos[q].y; o[2] = vpos[q].z; o[3] = vn[q * 4]; o[4] = vn[q * 4 + 1]; o[5] = vn[q * 4 + 2];
      o[6] = floorf(d4[q * 4 + 1] + 0.5f); o[7] = inside ? d4[q * 4 + 2] : 1.f;
    }
    // ---- the faces: a quad round every lattice edge the surface crosses, between the four cubes that share it,
    // wound with the field's normal
    auto cube = [&](int x, int y, int z, int& out) { auto it = cubeV.find(key3(x, y, z)); if (it == cubeV.end()) return false; out = it->second; return true; };
    auto quad = [&](int a, int b, int c, int dq) {
      vec3 nAvg(vb[a * 8 + 3] + vb[b * 8 + 3] + vb[c * 8 + 3] + vb[dq * 8 + 3], vb[a * 8 + 4] + vb[b * 8 + 4] + vb[c * 8 + 4] + vb[dq * 8 + 4], vb[a * 8 + 5] + vb[b * 8 + 5] + vb[c * 8 + 5] + vb[dq * 8 + 5]);
      vec3 fn = cross(vpos[b] - vpos[a], vpos[c] - vpos[a]) + cross(vpos[c] - vpos[a], vpos[dq] - vpos[a]);
      if (dot(fn, nAvg) < 0.f) std::swap(b, dq);
      ib.push_back(a); ib.push_back(b); ib.push_back(c); ib.push_back(a); ib.push_back(c); ib.push_back(dq);
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
    // ---- the hull of what moves: the 0.25 m voxels holding a moving cell, and their neighbours
    std::vector<uint8_t> mv((size_t)n1 * n1 * n1, 0);
    for (size_t q = 0; q < cand.size(); q++) {
      if (!moving[q]) continue;
      int id = cand[q], i2 = id % n2, j2 = (id / n2) % n2, k2 = id / (n2 * n2);
      for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int i1 = i2 / 4 + dx, j1 = j2 / 4 + dy, k1 = k2 / 4 + dz;
        if (i1 < 0 || j1 < 0 || k1 < 0 || i1 >= n1 || j1 >= n1 || k1 >= n1) continue;
        mv[((size_t)k1 * n1 + j1) * n1 + i1] = 1;
      }
    }
    std::unordered_map<int, uint64_t> noMask;
    hullFaces(n1, org, kS1, false, mv, noMask, hullTri);
    {   // the eye against the moving hull: outside it, the cockpit's hull pass keeps its near faces and no pixel marches the first metre
      int ei = (int)floorf((M[22 * 4] - org) / kS1), ej = (int)floorf((M[22 * 4 + 1] - org) / kS1), ek = (int)floorf((M[22 * 4 + 2] - org) / kS1);
      bool in = false;
      for (int dz = -1; dz <= 1 && !in; dz++) for (int dy = -1; dy <= 1 && !in; dy++) for (int dx = -1; dx <= 1 && !in; dx++) {
        int i1 = ei + dx, j1 = ej + dy, k1 = ek + dz;
        if (i1 < 0 || j1 < 0 || k1 < 0 || i1 >= n1 || j1 >= n1 || k1 >= n1) continue;
        in = mv[((size_t)k1 * n1 + j1) * n1 + i1] != 0;
      }
      hullTri.push_back(in ? 1.f : 0.f);   // (carried at the end of the hull's floats: the cache keeps it)
    }
    if (getenv("HULLDBG")) {
      int nmov = 0; for (uint8_t m : moving) nmov += m;
      printf("mesh %s: %d states, %zu band cells (%d moving), %zu lattice samples, %zu vertices, %zu triangles, moving hull %zu triangles\n",
             inside ? "cockpit" : "outside", ns, cand.size(), nmov, cpts.size(), vpos.size(), ib.size() / 3, hullTri.size() / 9);
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
  setRT(progPlaneMesh, fp);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  mat4 vp = viewProj(fp, 0.01f, 2000.f);   // (a near plane at 1 cm: in the cockpit the panel is closer than 0.5 m)
  glUniformMatrix4fv(U(progPlaneMesh, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(U(progPlaneMesh, "uJit"), jitX, jitY);
  glUniform1f(U(progPlaneMesh, "uLogC"), 2.f / log2f(40000.f + 1.f));
  glUniformMatrix3fv(U(progPlaneMesh, "uRot"), 1, GL_FALSE, rot);
  glUniform3f(U(progPlaneMesh, "uPos"), pos.x, pos.y, pos.z);
  glUniform1i(U(progPlaneMesh, "uMeshTraffic"), trafK);
  glBindVertexArray(pm.vao);
  glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

uint64_t Renderer::trafficModelKey(const float* t) const {
  uint64_t h = 1469598103934665603ull;
  const uint8_t* b = (const uint8_t*)t;
  for (size_t i = 0; i < sizeof(float) * 96; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
