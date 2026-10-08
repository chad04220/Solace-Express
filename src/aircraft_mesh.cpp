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
#include "mesh_simplify.h"
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <memory>

namespace {
inline int64_t key3(int x, int y, int z) { return ((int64_t)(x + 4096) << 42) | ((int64_t)(y + 4096) << 21) | (int64_t)(z + 4096); }
const float kH = kS2 / 4.f;   // the lattice: 1.5625 cm
const uint32_t kMeshMagic = 0x4d455348u + 18;   // (bump with the format, or with what the bake makes of the field: the cockpit's sharp edges, +14; its thin patch laid out fat and drawn last, +16; back on the surface, +17; its flat faces flat-shaded, +18)
// the rigid parts a cockpit has (plane_parts.glsl PT_*) and each one's instances: x which seat or side, y which pedal
struct PartInst { int type; float sx, sy; };
const int kMaxPartInst = 128;
bool partIsSurface(int type) { return type >= 11 && type <= 14; }
// a control surface's box at rest (the right side's, body space), from the model's numbers (plane_parts.glsl partField)
bool surfaceBox(int type, const float* M, vec3& lo, vec3& hi) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  const float R = m(0, 3);
  float span, rc, tc, sw, yOff, zOff, dih = 0.f, s0, s1, hf; bool fin = false;
  if (type == 11 || type == 12) {
    span = m(9, 0); rc = m(9, 1); tc = m(9, 2); sw = m(9, 3); yOff = m(10, 0); zOff = m(10, 1); dih = m(10, 2); hf = 0.74f;
    const float flapEnd = span * m(11, 3);
    if (type == 11) { s0 = 0.55f * R; s1 = flapEnd; } else { s0 = flapEnd + 0.03f; s1 = span * 0.94f; }
  } else if (type == 13) {
    span = m(12, 0); rc = m(12, 1); tc = m(12, 2); sw = m(12, 3); yOff = m(13, 0); zOff = m(13, 1); dih = m(13, 2); hf = 0.68f; s0 = 0.12f; s1 = span * 0.98f;
  } else {
    span = m(14, 0); rc = m(14, 1); tc = m(14, 2); sw = m(14, 3); yOff = m(15, 0); zOff = m(15, 1); hf = 0.66f;
    s0 = m(13, 3) > 0.5f ? 0.05f : 0.08f * span; s1 = span * 0.97f; fin = true;
  }
  if (!(span > 0.f) || s1 <= s0) return false;
  float c0 = 1e9f, c1 = -1e9f;
  for (float sv : {s0, s1}) { float k = std::clamp(sv / span, 0.f, 1.f), ch = rc + (tc - rc) * k, le = sw * k; c0 = std::min(c0, le + ch * hf); c1 = std::max(c1, le + ch); }
  const float tt = 0.12f, mg = 0.03f;
  if (!fin) { lo = vec3(s0 - mg, yOff + std::min(dih * s0, dih * s1) - tt, zOff + c0 - mg); hi = vec3(s1 + mg, yOff + std::max(dih * s0, dih * s1) + tt, zOff + c1 + mg); }
  else { lo = vec3(-tt, yOff + s0 - mg, zOff + c0 - mg); hi = vec3(tt, yOff + s1 + mg, zOff + c1 + mg); }
  return true;
}
// an XR-40 part's box in its own frame and the lattice it is meshed on (plane_parts.glsl PT_WR_*, wraith_sdf.glsl
// wrPartField): the nacelles at 8 mm (each a metre across), the fan at 5 mm, the small vanes, petals and turret pieces
// at 3-4 mm, the doors, the bomb and the control surfaces at 6 mm
bool wraithPartBox(int type, vec3& lo, vec3& hi, float& h) {
  switch (type) {
    case 15: case 16: lo = vec3(-0.62f, -0.58f, -1.42f); hi = vec3(0.62f, 0.58f, 1.34f); h = 0.008f; return true;
    case 17: lo = vec3(-0.46f, -0.46f, -1.13f); hi = vec3(0.46f, 0.46f, -0.59f); h = 0.005f; return true;
    case 18: lo = vec3(-0.31f, -0.026f, -0.09f); hi = vec3(0.31f, 0.026f, 0.09f); h = 0.003f; return true;
    case 19: lo = vec3(-0.25f, -0.026f, -0.09f); hi = vec3(0.25f, 0.026f, 0.09f); h = 0.003f; return true;
    case 20: lo = vec3(-0.024f, -0.26f, -0.085f); hi = vec3(0.024f, 0.26f, 0.085f); h = 0.003f; return true;
    case 21: lo = vec3(0.24f, -0.17f, 0.92f); hi = vec3(0.5f, 0.17f, 1.4f); h = 0.004f; return true;
    case 22: lo = vec3(-0.545f, -0.04f, -1.68f); hi = vec3(0.015f, 0.012f, 1.68f); h = 0.006f; return true;
    case 23: lo = vec3(-0.31f, -0.31f, -0.31f); hi = vec3(0.31f, 0.31f, 0.31f); h = 0.006f; return true;
    case 24: lo = vec3(-0.415f, -0.014f, -0.515f); hi = vec3(0.015f, 0.036f, 0.515f); h = 0.004f; return true;
    case 25: lo = vec3(-0.175f, -0.145f, -0.94f); hi = vec3(0.175f, 0.145f, 0.365f); h = 0.004f; return true;
    case 26: lo = vec3(-0.06f, -0.08f, -1.15f); hi = vec3(0.06f, 0.04f, -0.66f); h = 0.003f; return true;
    case 27: lo = vec3(-0.05f, -0.43f, -0.3f); hi = vec3(0.05f, 0.05f, 0.05f); h = 0.003f; return true;
    case 28: lo = vec3(2.87f, -0.25f, 2.77f); hi = vec3(6.03f, -0.06f, 4.46f); h = 0.006f; return true;
    case 45: lo = vec3(-0.08f, -0.08f, -0.08f); hi = vec3(0.08f, 0.08f, 1.08f); h = 0.003f; return true;   // a tilt actuator (unit length: its pose stretches it)
    case 30: lo = vec3(1.17f, -0.44f, 4.5f); hi = vec3(5.33f, -0.15f, 5.6f); h = 0.006f; return true;    // the XR-30's elevon
    case 31: lo = vec3(-0.04f, -0.06f, -0.63f); hi = vec3(1.54f, 0.06f, 0.93f); h = 0.005f; return true;   // its canard
    case 32: {   // its rudder: the (span, chord, thickness) box through the canted fin's frame (jtPartField)
      const float C = cosf(0.42f), S = sinf(0.42f);
      lo = vec3(1e9f, 1e9f, 1e9f); hi = vec3(-1e9f, -1e9f, -1e9f);
      for (int c = 0; c < 8; c++) {
        float sv = (c & 1) ? 2.25f : 0.1f, ch = (c & 2) ? 2.95f : 1.8f, t = (c & 4) ? 0.06f : -0.06f;
        float x = 1.f + C * t + S * sv, y = 0.3f - S * t + C * sv, z = ch + 4.6f;
        lo = vec3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z)); hi = vec3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z));
      }
      h = 0.006f; return true;
    }
    case 29: {   // the right ruddervator: its (span, chord, thickness) box through the canted fin's frame (wrPartField)
      const float C = cosf(0.72f), S = sinf(0.72f);
      lo = vec3(1e9f, 1e9f, 1e9f); hi = vec3(-1e9f, -1e9f, -1e9f);
      for (int c = 0; c < 8; c++) {
        float fs = (c & 1) ? 2.95f : 0.1f, ch = (c & 2) ? 3.25f : 1.75f, t = (c & 4) ? 0.07f : -0.07f;
        float z = ch + 4.4f, qy = fs - 0.75f;
        float x = C * t + S * qy + 1.05f, y = -S * t + C * qy + 0.67f - 0.05f * z;
        lo = vec3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z)); hi = vec3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z));
      }
      h = 0.006f; return true;
    }
  }
  return false;
}
// a gear part's box (its own frame) to survey for its tight one, and its lattice (plane_parts.glsl PT_GEAR_*, and the
// XR-30's nozzles and gear, PT_JT_NOZZLE on): generous,
// from the model's numbers - the legs' mounts and the bays' heights come from the field itself
bool gearPartBox(int type, const float* M, vec3& lo, vec3& hi, float& h) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  const float track = m(18, 0), wr = m(18, 1), mz = m(18, 2), gh = m(19, 0);
  const int gtype = (int)(m(0, 1) + 0.5f);
  switch (type) {
    case 33: lo = vec3(track - 0.5f, -gh - 0.1f, mz - wr - 0.6f); hi = vec3(track + 0.5f, 1.5f, mz + wr + 0.6f); h = 0.005f; return true;
    case 34: lo = vec3(-0.45f, -gh - 0.1f, -1.0f); hi = vec3(0.45f, 1.5f, 1.0f); h = 0.005f; return true;
    case 35: lo = vec3(-0.3f, -gh - 0.1f, -0.8f); hi = vec3(0.3f, 1.2f, 0.6f); h = 0.004f; return true;
    case 36: if (gtype == 4) { lo = vec3(-2.5f, -0.06f, -0.06f); hi = vec3(2.5f, 0.03f, wr + 0.2f); }
             else { lo = vec3(-0.06f, -0.06f, -wr - 0.2f); hi = vec3(0.5f, 0.03f, wr + 0.2f); }
             h = 0.004f; return true;
    case 37: lo = vec3(-0.06f, -0.06f, -wr - 0.2f); hi = vec3(0.4f, 0.03f, wr + 0.2f); h = 0.004f; return true;
    // the XR-30's (plane_sdf.glsl jtPartField): a nozzle in its own frame, the gear extended (body space), a door
    case 38: lo = vec3(-0.56f, -0.44f, -0.12f); hi = vec3(0.56f, 0.44f, 1.2f); h = 0.005f; return true;
    case 39: lo = vec3(track * 0.8f - 0.3f, -gh - 0.1f, mz - 0.45f); hi = vec3(track + 0.25f, 0.f, mz + 0.45f); h = 0.005f; return true;
    case 40: lo = vec3(track - 0.35f, -gh - 0.15f, mz - 0.55f); hi = vec3(track + 0.35f, -gh + 0.95f, mz + 0.55f); h = 0.005f; return true;
    case 41: lo = vec3(-0.3f, -gh - 0.1f, m(18, 3) - 0.45f); hi = vec3(0.3f, 0.f, m(18, 3) + 0.45f); h = 0.005f; return true;
    case 42: lo = vec3(-0.35f, -gh - 0.15f, m(18, 3) - 0.5f); hi = vec3(0.35f, -gh + 0.85f, m(18, 3) + 0.5f); h = 0.004f; return true;
    case 43: case 44: lo = vec3(-0.06f, -0.06f, -0.5f); hi = vec3(0.32f, 0.03f, 0.5f); h = 0.004f; return true;
  }
  return false;
}
int partList(const float* M, bool inside, PartInst* out) {
  const int eng = (int)(M[2] + 0.5f);
  int n = 0;
  if (eng == 6 && !inside) {   // the XR-40: per pod its nacelle, fan, vanes, ten iris petals and tilt actuator; the bay doors, the bomb, the turrets, the elevons and ruddervators, the gear
    for (int i = 0; i < 4; i++) {
      out[n++] = {i < 2 ? 15 : 16, (float)i, 0}; out[n++] = {17, (float)i, 0}; out[n++] = {18, (float)i, 0};
      for (int k = -1; k <= 1; k += 2) { out[n++] = {19, (float)i, (float)k}; out[n++] = {20, (float)i, (float)k}; }
      for (int j = 0; j < 10; j++) out[n++] = {21, (float)i, (float)j};
    }
    out[n++] = {23, 0, 0};
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {22, (float)s, 0}; out[n++] = {24, (float)s, 0}; out[n++] = {25, (float)s, 0}; out[n++] = {26, (float)s, 0}; out[n++] = {27, (float)s, 0};
      out[n++] = {28, (float)s, 0}; out[n++] = {29, (float)s, 0};
      out[n++] = {39, (float)s, 0}; out[n++] = {40, (float)s, 0}; out[n++] = {43, (float)s, -1}; out[n++] = {43, (float)s, 1};   // its gear (the XR-30's parts)
    }
    out[n++] = {41, 0, 0}; out[n++] = {42, 0, 0}; out[n++] = {44, 0, -1}; out[n++] = {44, 0, 1};
    for (int i = 0; i < 4; i++) out[n++] = {45, (float)i, 0};   // the pods' tilt actuators
    return n;
  }
  if (eng < 5) {   // the light aircraft's (and the XR-10's and XR-20's) control surfaces, outside and from the cockpit
    for (int s = -1; s <= 1; s += 2) { out[n++] = {11, (float)s, 0}; out[n++] = {12, (float)s, 0}; out[n++] = {13, (float)s, 0}; }   // flap, aileron, elevator
    out[n++] = {14, 0, 0};   // rudder
    // and their gear: a retracting main leg a side and its bay's two doors; the nose wheel (and its doors) or the tail wheel
    const int gtype = (int)(M[1] + 0.5f); const bool tail = M[19 * 4 + 2] > 0.5f;
    if (gtype >= 3) for (int s = -1; s <= 1; s += 2) { out[n++] = {33, (float)s, 0}; out[n++] = {36, (float)s, -1}; out[n++] = {36, (float)s, 1}; }
    if (tail) out[n++] = {35, 0, 0};
    else { out[n++] = {34, 0, 0}; if (gtype >= 3) { out[n++] = {37, 0, -1}; out[n++] = {37, 0, 1}; } }
  }
  if (eng == 5 && !inside) {   // the XR-30: elevons, canards, rudders, nozzles; its gear's struts, wheels and bay doors
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {30, (float)s, 0}; out[n++] = {31, (float)s, 0}; out[n++] = {32, (float)s, 0}; out[n++] = {38, (float)s, 0};
      out[n++] = {39, (float)s, 0}; out[n++] = {40, (float)s, 0}; out[n++] = {43, (float)s, -1}; out[n++] = {43, (float)s, 1};
    }
    out[n++] = {41, 0, 0}; out[n++] = {42, 0, 0}; out[n++] = {44, 0, -1}; out[n++] = {44, 0, 1};
    return n;
  }
  if (!inside) return n;
  if (eng == 5) { out[n++] = {6, 0, 0}; out[n++] = {7, 0, 0}; return n; }                      // the XR-30: stick, throttle
  if (eng == 6) { out[n++] = {8, 0, 0}; out[n++] = {9, 0, 0}; out[n++] = {10, -1, 0}; out[n++] = {10, 1, 0}; return n; }   // the XR-40: and its pedals
  if (eng > 6) return 0;
  for (int s = -1; s <= 1; s += 2) { out[n++] = {0, (float)s, 0}; out[n++] = {1, (float)s, 0}; }   // the yokes: shaft, wheel
  for (int s = -1; s <= 1; s += 2) for (int q = -1; q <= 1; q += 2) out[n++] = {2, (float)s, (float)q};   // the pedals
  if ((int)(M[21 * 4 + 2] + 0.5f) == 0) out[n++] = {3, 0, 0};   // a push-pull throttle
  else { out[n++] = {4, -1, 0}; out[n++] = {4, 1, 0}; out[n++] = {5, 0, 0}; }   // throttle levers, flap lever
  return n;
}
}

bool Renderer::compilePlaneMesh() {
  std::string e;
  for (int v = 0; v < 2; v++) {   // (every aircraft, then the light aircraft alone: pickAfPrograms)
    progPlaneMeshV[v] = linkProgramCached(planeMeshVSAssembly(""), planeMeshFSAssembly(v ? "#define AF_LIGHT\n" : ""), e);
    if (!progPlaneMeshV[v]) { error = "Aircraft mesh shader: " + e; return false; }
  }
  progPlaneMesh = progPlaneMeshV[0];
  // the depth pre-pass; with uScrSkip the research cockpit's windows are cut (cabin_windows.glsl: the screens are holes)
  // (uCloakZ: a cloaked XR-40's sweeping front, body z - what lies ahead of it is see-through and writes no depth; -1e9 none)
  progPlaneMeshDepth = linkProgramCached(planeMeshVSAssembly(""), planeMeshDepthFSAssembly(), e);
  if (!progPlaneMeshDepth) { error = "Aircraft mesh depth shader: " + e; return false; }
  progPartPose = linkProgramCached(kFullscreenVS, partPoseFSAssembly(), e);
  if (!progPartPose) { error = "Cockpit part pose shader: " + e; return false; }
  return true;
}

// every aircraft but a wreck
bool Renderer::planeMeshWanted(const FrameParams& fp) const {
  const PlaneVisual& pv = fp.plane;
  return !meshOff && progPlaneMesh && progHullBake && pv.on && fp.wreck.pieces == 0;   // (a cloaked XR-40 too: the mesh passes leave its cloaked part out)
}

void Renderer::bakePlaneMesh(const FrameParams& fp, int slot, uint64_t key) {
  bakeCount++;
  const PlaneVisual& pv = fp.plane;
  const float* M = pv.M;
  const bool inside = slot == 1;
  PlaneMesh& PM = planeMeshes[key];
  PM.key = key; PM.movKey = key ^ 0x4d4f56494e47ull;
  std::vector<float> vb, hullTri; std::vector<uint32_t> ib; uint32_t fineStart = 0;   // (fineStart: where the fine patch's indices begin)
  std::vector<uint32_t> partBlob;   // the rigid parts: per part its type, float count, index count, vertices (8 floats each), indices
  // the cache
  std::string path;
  if (!g_shaderCacheDir.empty()) {
    static const std::string stamp = meshCacheStamp();
    char name[64]; snprintf(name, sizeof name, "/mesh_%016llx_%s.bin", (unsigned long long)key, stamp.c_str());
    path = g_shaderCacheDir + name;
    if (FILE* f = fopen(path.c_str(), "rb")) {
      uint32_t hdr[6] = {0, 0, 0, 0, 0, 0};
      bool ok = fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == kMeshMagic;
      if (ok) {   // (the counts must add up to the file: a damaged header is not an allocation; the hull's floats end in the eye flag)
        long long here = ftell(f); fseek(f, 0, SEEK_END); long long size = ftell(f); fseek(f, (long)here, SEEK_SET);
        ok = here == (long long)sizeof hdr && size == here + 4ll * ((long long)hdr[1] + hdr[2] + hdr[3] + hdr[5]) && hdr[1] % 8 == 0 && hdr[2] % 3 == 0 && hdr[3] % 9 == 1 && hdr[4] % 3 == 0 && hdr[4] <= hdr[2];
      }
      if (ok) { vb.resize(hdr[1]); ib.resize(hdr[2]); hullTri.resize(hdr[3]); fineStart = hdr[4]; partBlob.resize(hdr[5]); }
      ok = ok && (vb.empty() || fread(vb.data(), sizeof(float), vb.size(), f) == vb.size()) && (ib.empty() || fread(ib.data(), sizeof(uint32_t), ib.size(), f) == ib.size())
           && (hullTri.empty() || fread(hullTri.data(), sizeof(float), hullTri.size(), f) == hullTri.size())
           && (partBlob.empty() || fread(partBlob.data(), sizeof(uint32_t), partBlob.size(), f) == partBlob.size());
      fclose(f);
      if (!ok) { vb.clear(); ib.clear(); hullTri.clear(); partBlob.clear(); }
      else if (getenv("HULLDBG")) printf("mesh %s: from the cache (%zu vertices)\n", inside ? "cockpit" : "outside", vb.size() / 8);
    }
  }
  if (ib.empty()) {
    bakeBuilt++;
    if (onBakeStart) onBakeStart();   // (not in the cache, or unreadable: the launch's loading screen says it is building)
    // ---- the field's states, on the bake program (bound by the caller)
    std::vector<HullState> st = hullStateList(M, inside, true);   // (the sweeps that move only rigid parts dropped)
    const int ns = (int)st.size();
    std::vector<float> sps(128 * 4, 0.f), sct(128 * 4, 0.f), swr(128 * 4, 0.f), swr2(128 * 4, 0.f);
    for (int i = 0; i < ns; i++) for (int c = 0; c < 4; c++) { sps[i * 4 + c] = st[i].ps[c]; sct[i * 4 + c] = st[i].ctl[c]; swr[i * 4 + c] = st[i].wr[c]; swr2[i * 4 + c] = st[i].wr2[c]; }
    glUniform1i(U(progHullBake, "uHStN"), ns);
    glUniform4fv(U(progHullBake, "uHStPS"), 128, sps.data());
    glUniform4fv(U(progHullBake, "uHStCtl"), 128, sct.data());
    glUniform4fv(U(progHullBake, "uHStWr"), 128, swr.data());
    glUniform4fv(U(progHullBake, "uHStWr2"), 128, swr2.data());
    auto mode = [&](int m, int s) { glUniform1i(U(progHullBake, "uHMode"), m); glUniform1i(U(progHullBake, "uHState"), s); };
    glUniform1i(U(progHullBake, "uHPart"), -2);   // the airframe without its rigid parts: they are meshes of their own (below)
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
      if ((q & 4095) == 0) bakeTick();
      if (moving[q] || fabsf(dRest[q]) >= band) continue;
      bool ring = !thin[q] && nearThin(cand[q]);
      if (!thin[q]) coarseCells.push_back({cand[q], (uint8_t)(ring ? 2 : 1)});
      if (thin[q] || ring) fineCells.push_back({cand[q], 1});
    }
    // ---- surface nets over a set of cells on a lattice of `sub` steps per cell: one vertex per lattice cube the
    // surface crosses, at the mean of its edge crossings, pulled onto the surface; a quad round every lattice edge the
    // surface crosses, between the four cubes that share it, wound with the field's normal
    size_t nLattice = 0;
    // iso: the level meshed (a positive one lays the surface that far out: the cockpit's thin patch, trimmed per pixel)
    auto nets = [&](const std::vector<std::pair<int, uint8_t>>& cells, int sub, float sink, float inflate, float iso = 0.f) {
      const float h = kS2 / sub;
      std::unordered_map<int64_t, int> corner;   // lattice coords -> sample index
      std::vector<vec3> cpts;
      for (auto& ce : cells) {
        if ((&ce - &cells[0]) % 1024 == 0) bakeTick();
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
      if (iso != 0.f) for (float& x : cv) x -= iso;
      auto cval = [&](int lx, int ly, int lz, float& v) { auto it = corner.find(key3(lx, ly, lz)); if (it == corner.end()) return false; v = cv[it->second]; return true; };
      std::unordered_map<int64_t, int> cubeV;   // cube coords -> vertex (local)
      std::vector<vec3> vpos; std::vector<int> vcube; std::vector<uint8_t> vsink;   // (each vertex's cube, and whether its cell sinks)
      std::vector<vec3> xpt; std::vector<int> xv;   // (in the cockpit: every edge crossing and its vertex, for the sharp edges below)
      const int ce[12][2] = {{0, 1}, {1, 3}, {2, 3}, {0, 2}, {4, 5}, {5, 7}, {6, 7}, {4, 6}, {0, 4}, {1, 5}, {3, 7}, {2, 6}};   // corner bit x + 2y + 4z
      for (auto& cl : cells) {
        if ((&cl - &cells[0]) % 1024 == 0) bakeTick();
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
            if (inside) { xpt.push_back(pa + (pb - pa) * t); xv.push_back((int)vpos.size()); }
          }
          cubeV[key3(cx, cy, cz)] = (int)vpos.size();
          vpos.push_back(sum * (1.f / cnt)); vcube.push_back(cx); vcube.push_back(cy); vcube.push_back(cz); vsink.push_back(cl.second == 2);
        }
      }
      // In the cockpit, seen from a metre: each vertex where the planes through its edge crossings meet, along the
      // field's normals there (dual contouring), instead of at the crossings' mean - on a sharp edge (a window frame's
      // lip, a strut's rim, a bezel) the mean sits off the edge and the edge zigzags with the lattice; where two faces
      // meet the planes put it on the edge itself, a flat face or a gentle curve takes it to the mean as before. A
      // small pull to the mean keeps an under-determined vertex (a flat face's, a crease's along its line) in place
      if (inside && !xpt.empty()) {
        std::vector<float> xn; mode(3, 0); hullEval4(xpt, xn);
        std::vector<double> A(vpos.size() * 6, 0.0), B(vpos.size() * 3, 0.0);
        for (size_t i = 0; i < xpt.size(); i++) {
          const int q = xv[i]; const vec3 c = vpos[q], pp = xpt[i] - c;
          const double nx = xn[i * 4], ny = xn[i * 4 + 1], nz = xn[i * 4 + 2], nd = nx * pp.x + ny * pp.y + nz * pp.z;
          double* a = &A[q * 6]; a[0] += nx * nx; a[1] += nx * ny; a[2] += nx * nz; a[3] += ny * ny; a[4] += ny * nz; a[5] += nz * nz;
          double* b = &B[q * 3]; b[0] += nx * nd; b[1] += ny * nd; b[2] += nz * nd;
        }
        const double lam = 0.05;
        for (size_t q = 0; q < vpos.size(); q++) {
          const double* a = &A[q * 6]; const double* b = &B[q * 3];
          const double m00 = a[0] + lam, m01 = a[1], m02 = a[2], m11 = a[3] + lam, m12 = a[4], m22 = a[5] + lam;
          const double c00 = m11 * m22 - m12 * m12, c01 = m02 * m12 - m01 * m22, c02 = m01 * m12 - m02 * m11;
          const double det = m00 * c00 + m01 * c01 + m02 * c02;
          if (fabs(det) < 1e-12) continue;
          const double c11 = m00 * m22 - m02 * m02, c12 = m01 * m02 - m00 * m12, c22 = m00 * m11 - m01 * m01;
          vec3 x((float)((c00 * b[0] + c01 * b[1] + c02 * b[2]) / det), (float)((c01 * b[0] + c11 * b[1] + c12 * b[2]) / det), (float)((c02 * b[0] + c12 * b[1] + c22 * b[2]) / det));
          vec3 p = vpos[q] + x;
          const float lo[3] = {org + vcube[q * 3] * h, org + vcube[q * 3 + 1] * h, org + vcube[q * 3 + 2] * h};   // (within its own cube)
          p.x = std::max(lo[0], std::min(lo[0] + h, p.x)); p.y = std::max(lo[1], std::min(lo[1] + h, p.y)); p.z = std::max(lo[2], std::min(lo[2] + h, p.z));
          vpos[q] = p;
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
          vec3 p = vpos[q] - vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * std::max(-h, std::min(h, d4[q * 4] - iso));
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
        for (size_t q = 0; q < vpos.size(); q++) { float a = fabsf(d4[q * 4] - iso); mx = std::max(mx, a); if (a > 0.002f) n2mm++; if (a > 0.005f) n5mm++; }
        printf("mesh %s (lattice %.2f cm): %zu vertices off the surface: %d > 2 mm, %d > 5 mm, max %.1f mm\n", inside ? "cockpit" : "outside", h * 100.f, vpos.size(), n2mm, n5mm, mx * 1000.f);
      }
      // the ring round the fine patch sinks into the wall (along the normal, away from the cabin), so the patch is in
      // front of it wherever the two lay the same surface
      for (size_t q = 0; q < vpos.size(); q++) if (vsink[q]) vpos[q] = vpos[q] - vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * sink;
      // (inflate: the surface moved that far outward. The fine patch lies on the surface itself: it was once a shell 4 mm
      // out whose fragments marched the whole aircraft's field to the surface, 130 ms a frame in the cockpit)
      if (inflate > 0.f) for (size_t q = 0; q < vpos.size(); q++) vpos[q] = vpos[q] + vec3(vn[q * 4], vn[q * 4 + 1], vn[q * 4 + 2]) * inflate;
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
      size_t qn = 0;
      for (auto& kv : cubeV) {
        if ((qn++ & 16383) == 0) bakeTick();
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
    nets(coarseCells, 4, 0.003f, 0.f);
    fineStart = (uint32_t)ib.size();
    const size_t fineV = vb.size() / 8;   // (the fine patch's vertices are its own, after the coarse mesh's)
    if (!fineCells.empty()) nets(fineCells, 8, 0.f, 0.f);
    // simplified (mesh_simplify.h): flat panels to a few triangles, curves to within 1 mm (the cabin's 0.4 mm: seen
    // from half a metre); the fine patch apart, to the same 0.4 mm. On worker threads, while the GPU goes on with the hull and the parts (vb, ib and fineStart are not
    // touched again until they are joined, below)
    const size_t rawTris = ib.size() / 3;
    std::vector<float> vbF(vb.begin() + fineV * 8, vb.end()); vb.resize(fineV * 8);
    std::vector<uint32_t> ibF(ib.begin() + fineStart, ib.end()); ib.resize(fineStart);
    for (uint32_t& i : ibF) i -= (uint32_t)fineV;
    std::thread simpStatic([&vb, &ib, inside] { size_t e = ib.size(); simplifyMesh(vb, ib, e, inside ? 0.0004f : 0.001f); });
    std::thread simpFine([&vbF, &ibF] { size_t e = ibF.size(); simplifyMesh(vbF, ibF, e, 0.0004f); });
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
    // ---- the rigid parts (plane_parts.glsl), each alone in its own frame at rest: a 1 cm survey finds its box, then
    // surface nets on a 2 mm lattice over it, every vertex pulled onto the surface as above
    // (each part meshed here is simplified on a thread of its own and added to the blob once all are done, in order)
    struct PartOut { int type; std::vector<float> vb; std::vector<uint32_t> ib; size_t raw; };
    std::vector<std::unique_ptr<PartOut>> partOut; std::vector<std::thread> partSimp;
    {
      PartInst pl[kMaxPartInst]; const int np = partList(M, inside, pl);
      std::vector<int> done;
      for (int pi = 0; pi < np; pi++) {
        const int type = pl[pi].type;
        if (std::find(done.begin(), done.end(), type) != done.end()) continue;
        done.push_back(type);
        glUniform1i(U(progHullBake, "uHPart"), type);
        glUniform2f(U(progHullBake, "uHPartSide"), pl[pi].sx, pl[pi].sy);
        // its box: a control surface's from the model's numbers (6 mm lattice: seen from metres away); a cockpit
        // part's from a 1 cm survey of +-0.35 m about its own origin (2 mm lattice: seen from arm's length)
        vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        float h = 0.002f;
        vec3 glo, ghi;
        if (wraithPartBox(type, lo, hi, h)) { lo = lo - vec3(2.f * h, 2.f * h, 2.f * h); hi = hi + vec3(2.f * h, 2.f * h, 2.f * h); }
        else if (gearPartBox(type, M, glo, ghi, h)) {   // a 2 cm survey of the generous box for the tight one
          const float hc = 0.02f;
          const int sx = (int)ceilf((ghi.x - glo.x) / hc) + 1, sy = (int)ceilf((ghi.y - glo.y) / hc) + 1, sz = (int)ceilf((ghi.z - glo.z) / hc) + 1;
          std::vector<vec3> sp; std::vector<float> sd;
          for (int k = 0; k < sz; k++) for (int j = 0; j < sy; j++) for (int i = 0; i < sx; i++) sp.push_back(vec3(glo.x + i * hc, glo.y + j * hc, glo.z + k * hc));
          mode(1, 0); hullEval(sp, sd);
          bool any = false;
          for (size_t q = 0; q < sp.size(); q++) if (sd[q] < hc) { any = true; lo = vec3(std::min(lo.x, sp[q].x), std::min(lo.y, sp[q].y), std::min(lo.z, sp[q].z)); hi = vec3(std::max(hi.x, sp[q].x), std::max(hi.y, sp[q].y), std::max(hi.z, sp[q].z)); }
          if (!any) continue;
          const float mg = hc + 2.f * h;
          lo = lo - vec3(mg, mg, mg); hi = hi + vec3(mg, mg, mg);
        }
        else if (partIsSurface(type)) {
          if (!surfaceBox(type, M, lo, hi)) continue;
          h = 0.006f; lo = lo - vec3(2.f * h, 2.f * h, 2.f * h); hi = hi + vec3(2.f * h, 2.f * h, 2.f * h);
        } else {
          const float hc = 0.01f; const int nc = 71;
          std::vector<vec3> sp; std::vector<float> sd;
          for (int k = 0; k < nc; k++) for (int j = 0; j < nc; j++) for (int i = 0; i < nc; i++) sp.push_back(vec3((i - 35) * hc, (j - 35) * hc, (k - 35) * hc));
          mode(1, 0); hullEval(sp, sd);
          bool any = false;
          for (size_t q = 0; q < sp.size(); q++) if (sd[q] < hc) { any = true; lo = vec3(std::min(lo.x, sp[q].x), std::min(lo.y, sp[q].y), std::min(lo.z, sp[q].z)); hi = vec3(std::max(hi.x, sp[q].x), std::max(hi.y, sp[q].y), std::max(hi.z, sp[q].z)); }
          if (!any) continue;
          const float mg = hc + 2.f * h;
          lo = lo - vec3(mg, mg, mg); hi = hi + vec3(mg, mg, mg);
        }
        const int nx = (int)ceilf((hi.x - lo.x) / h) + 1, ny = (int)ceilf((hi.y - lo.y) / h) + 1, nz = (int)ceilf((hi.z - lo.z) / h) + 1;
        std::vector<vec3> cp((size_t)nx * ny * nz); std::vector<float> cv;
        for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) cp[((size_t)k * ny + j) * nx + i] = vec3(lo.x + i * h, lo.y + j * h, lo.z + k * h);
        mode(1, 0); hullEval(cp, cv);
        auto C = [&](int i, int j, int k) { return cv[((size_t)k * ny + j) * nx + i]; };
        const int mx = nx - 1, my = ny - 1, mz = nz - 1;
        std::vector<int> cubeV((size_t)mx * my * mz, -1);
        std::vector<vec3> vp; std::vector<int> vc;
        const int ce[12][2] = {{0, 1}, {1, 3}, {2, 3}, {0, 2}, {4, 5}, {5, 7}, {6, 7}, {4, 6}, {0, 4}, {1, 5}, {3, 7}, {2, 6}};
        for (int k = 0; k < mz; k++) for (int j = 0; j < my; j++) for (int i = 0; i < mx; i++) {
          if (i == 0 && j == 0) bakeTick();
          float v[8]; int neg = 0;
          for (int c = 0; c < 8; c++) { v[c] = C(i + (c & 1), j + ((c >> 1) & 1), k + (c >> 2)); if (v[c] < 0.f) neg++; }
          if (neg == 0 || neg == 8) continue;
          vec3 sum; int cnt = 0;
          for (int e = 0; e < 12; e++) {
            int a = ce[e][0], b = ce[e][1];
            if ((v[a] < 0.f) == (v[b] < 0.f)) continue;
            float t = v[a] / (v[a] - v[b]);
            vec3 pa(lo.x + (i + (a & 1)) * h, lo.y + (j + ((a >> 1) & 1)) * h, lo.z + (k + (a >> 2)) * h);
            vec3 pb(lo.x + (i + (b & 1)) * h, lo.y + (j + ((b >> 1) & 1)) * h, lo.z + (k + (b >> 2)) * h);
            sum = sum + pa + (pb - pa) * t; cnt++;
          }
          cubeV[((size_t)k * my + j) * mx + i] = (int)vp.size();
          vp.push_back(sum * (1.f / cnt)); vc.push_back(i); vc.push_back(j); vc.push_back(k);
        }
        std::vector<float> pn, p4;
        for (int it = 0; it < 3; it++) {
          mode(3, 0); hullEval4(vp, pn);
          mode(2, 0); hullEval4(vp, p4);
          for (size_t q = 0; q < vp.size(); q++) {
            vec3 x = vp[q] - vec3(pn[q * 4], pn[q * 4 + 1], pn[q * 4 + 2]) * std::max(-h, std::min(h, p4[q * 4]));
            const float m = 0.25f * h;
            float l0 = lo.x + vc[q * 3] * h - m, l1 = lo.y + vc[q * 3 + 1] * h - m, l2 = lo.z + vc[q * 3 + 2] * h - m;
            x.x = std::max(l0, std::min(l0 + h + 2.f * m, x.x)); x.y = std::max(l1, std::min(l1 + h + 2.f * m, x.y)); x.z = std::max(l2, std::min(l2 + h + 2.f * m, x.z));
            vp[q] = x;
          }
        }
        mode(3, 0); hullEval4(vp, pn);
        mode(2, 0); hullEval4(vp, p4);
        std::vector<float> pvb(vp.size() * 8); std::vector<uint32_t> pib;
        for (size_t q = 0; q < vp.size(); q++) {
          float* o = &pvb[q * 8];
          o[0] = vp[q].x; o[1] = vp[q].y; o[2] = vp[q].z; o[3] = pn[q * 4]; o[4] = pn[q * 4 + 1]; o[5] = pn[q * 4 + 2];
          o[6] = floorf(p4[q * 4 + 1] + 0.5f); o[7] = p4[q * 4 + 2];
        }
        auto cubeAt = [&](int i, int j, int k, int& out) { if (i < 0 || j < 0 || k < 0 || i >= mx || j >= my || k >= mz) return false; out = cubeV[((size_t)k * my + j) * mx + i]; return out >= 0; };
        auto quad = [&](int a, int b, int c, int dq) {
          vec3 nA(pn[a * 4] + pn[b * 4] + pn[c * 4] + pn[dq * 4], pn[a * 4 + 1] + pn[b * 4 + 1] + pn[c * 4 + 1] + pn[dq * 4 + 1], pn[a * 4 + 2] + pn[b * 4 + 2] + pn[c * 4 + 2] + pn[dq * 4 + 2]);
          vec3 fn = cross(vp[b] - vp[a], vp[c] - vp[a]) + cross(vp[c] - vp[a], vp[dq] - vp[a]);
          if (dot(fn, nA) < 0.f) std::swap(b, dq);
          pib.push_back(a); pib.push_back(b); pib.push_back(c); pib.push_back(a); pib.push_back(c); pib.push_back(dq);
        };
        for (int k = 0; k < mz; k++) for (int j = 0; j < my; j++) for (int i = 0; i < mx; i++) {
          int a = cubeV[((size_t)k * my + j) * mx + i], b, c, dq;
          if (a < 0) continue;
          float v0 = C(i, j, k);
          if ((v0 < 0.f) != (C(i + 1, j, k) < 0.f) && cubeAt(i, j - 1, k, b) && cubeAt(i, j - 1, k - 1, c) && cubeAt(i, j, k - 1, dq)) quad(a, b, c, dq);
          if ((v0 < 0.f) != (C(i, j + 1, k) < 0.f) && cubeAt(i, j, k - 1, b) && cubeAt(i - 1, j, k - 1, c) && cubeAt(i - 1, j, k, dq)) quad(a, b, c, dq);
          if ((v0 < 0.f) != (C(i, j, k + 1) < 0.f) && cubeAt(i - 1, j, k, b) && cubeAt(i - 1, j - 1, k, c) && cubeAt(i, j - 1, k, dq)) quad(a, b, c, dq);
        }
        if (pib.empty()) continue;
        if (getenv("HULLDBG")) printf("mesh part %d: %d x %d x %d lattice, %zu vertices, %zu triangles\n", type, nx, ny, nz, vp.size(), pib.size() / 3);
        // simplified as the airframe is (the cockpit's controls to 0.3 mm, the exterior's parts to 0.8 mm)
        partOut.push_back(std::make_unique<PartOut>(PartOut{type, std::move(pvb), std::move(pib), 0}));
        PartOut* po = partOut.back().get(); po->raw = po->ib.size() / 3;
        partSimp.emplace_back([po, inside] { size_t pe = po->ib.size(); simplifyMesh(po->vb, po->ib, pe, inside ? 0.0003f : 0.0008f); });
      }
      glUniform1i(U(progHullBake, "uHPart"), -1);
    }
    simpStatic.join(); simpFine.join();
    fineStart = (uint32_t)ib.size();
    { const uint32_t off = (uint32_t)(vb.size() / 8); vb.insert(vb.end(), vbF.begin(), vbF.end()); for (uint32_t i : ibF) ib.push_back(i + off); }
    // ---- the cabin's flat faces, flat: a vertex at the foot of a rounded edge carries the bend's turned normal, and
    // across the simplified mesh's large flat triangles it smeared a band of the bend over the face - on the XR-40's
    // polished titanium the fixtures' highlights broke into a sawtooth along the edges (an armrest display's mount,
    // the owner's report). A triangle lying in a flat stretch of the field's surface - the field's normal at its
    // centre and half way to each corner within 2 degrees of its plane's (a curve's chord fails it, and stays smooth)
    // - takes its plane's normal at its corners, on copies of them; one across a bend keeps its corners'
    if (inside && !ib.empty()) {
      const size_t nt = ib.size() / 3;
      std::vector<vec3> smp(nt * 4), fnv(nt);
      for (size_t t = 0; t < nt; t++) {
        const float *a = &vb[(size_t)ib[t * 3] * 8], *b = &vb[(size_t)ib[t * 3 + 1] * 8], *c = &vb[(size_t)ib[t * 3 + 2] * 8];
        const vec3 A(a[0], a[1], a[2]), B(b[0], b[1], b[2]), Cc(c[0], c[1], c[2]);
        const vec3 fn = cross(B - A, Cc - A); const float l = length(fn);
        fnv[t] = l > 1e-12f ? fn * (1.f / l) : vec3(0, 0, 0);
        const vec3 m = (A + B + Cc) * (1.f / 3.f);
        smp[t * 4] = m; smp[t * 4 + 1] = (m + A) * 0.5f; smp[t * 4 + 2] = (m + B) * 0.5f; smp[t * 4 + 3] = (m + Cc) * 0.5f;
      }
      glUniform1i(U(progHullBake, "uHPart"), -2);   // (the field the airframe's mesh was laid from)
      std::vector<float> cn; mode(3, 0); hullEval4(smp, cn);
      glUniform1i(U(progHullBake, "uHPart"), -1);
      const float flatCos = cosf(2.f * DEG), keepCos = cosf(1.5f * DEG);
      int nFlat = 0;
      for (size_t t = 0; t < nt; t++) {
        if (dot(fnv[t], fnv[t]) < 0.5f) continue;
        vec3 f = fnv[t];
        if (cn[t * 16] * f.x + cn[t * 16 + 1] * f.y + cn[t * 16 + 2] * f.z < 0.f) f = f * -1.f;
        bool flat = true;
        for (int k = 0; k < 4 && flat; k++) {
          const float* g = &cn[(t * 4 + k) * 4];
          const float gl = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
          flat = g[0] * f.x + g[1] * f.y + g[2] * f.z >= flatCos * gl;
        }
        if (!flat) continue;
        bool off = false;
        for (int k = 0; k < 3; k++) { const float* v = &vb[(size_t)ib[t * 3 + k] * 8]; if (v[3] * f.x + v[4] * f.y + v[5] * f.z < keepCos) off = true; }
        if (!off) continue;
        for (int k = 0; k < 3; k++) {
          float v[8]; memcpy(v, &vb[(size_t)ib[t * 3 + k] * 8], sizeof v);
          v[3] = f.x; v[4] = f.y; v[5] = f.z;
          ib[t * 3 + k] = (uint32_t)(vb.size() / 8);
          vb.insert(vb.end(), v, v + 8);
        }
        nFlat++;
      }
      if (getenv("HULLDBG")) printf("mesh cockpit: %d of %zu triangles flat-shaded\n", nFlat, nt);
    }
    for (auto& th : partSimp) th.join();
    for (auto& po : partOut) {
      partBlob.push_back((uint32_t)po->type); partBlob.push_back((uint32_t)po->vb.size()); partBlob.push_back((uint32_t)po->ib.size());
      size_t at = partBlob.size(); partBlob.resize(at + po->vb.size());
      memcpy(&partBlob[at], po->vb.data(), po->vb.size() * sizeof(float));
      partBlob.insert(partBlob.end(), po->ib.begin(), po->ib.end());
      if (getenv("HULLDBG")) printf("mesh part %d simplified: %zu -> %zu triangles\n", po->type, po->raw, po->ib.size() / 3);
    }
    if (getenv("HULLDBG")) printf("mesh %s simplified: %zu -> %zu triangles\n", inside ? "cockpit" : "outside", rawTris, ib.size() / 3);
    if (getenv("HULLDBG")) {
      int nmov = 0; for (uint8_t m : moving) nmov += m;
      printf("mesh %s: %d states, %zu band cells (%d moving, %d thin), %zu lattice samples, %zu vertices, %zu triangles, moving hull %zu triangles\n",
             inside ? "cockpit" : "outside", ns, cand.size(), nmov, nThin, nLattice, vb.size() / 8, ib.size() / 3, hullTri.size() / 9);
    }
    if (!path.empty()) {   // (the eye flag rides at the end of the hull's floats)
      if (FILE* f = fopen(path.c_str(), "wb")) {
        uint32_t hdr[6] = {kMeshMagic, (uint32_t)vb.size(), (uint32_t)ib.size(), (uint32_t)hullTri.size(), fineStart, (uint32_t)partBlob.size()};
        fwrite(hdr, sizeof hdr, 1, f);
        if (!vb.empty()) fwrite(vb.data(), sizeof(float), vb.size(), f);
        if (!ib.empty()) fwrite(ib.data(), sizeof(uint32_t), ib.size(), f);
        if (!hullTri.empty()) fwrite(hullTri.data(), sizeof(float), hullTri.size(), f);
        if (!partBlob.empty()) fwrite(partBlob.data(), sizeof(uint32_t), partBlob.size(), f);
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
  // the rigid parts, each a mesh of its own (validated: a damaged cache drops the parts, never reads past its end)
  for (auto& P : PM.parts) { if (P.vao) glDeleteVertexArrays(1, &P.vao); if (P.vbo) glDeleteBuffers(1, &P.vbo); if (P.ibo) glDeleteBuffers(1, &P.ibo); }
  PM.parts.clear();
  for (size_t at = 0; at + 3 <= partBlob.size();) {
    uint32_t type = partBlob[at], nf = partBlob[at + 1], ni = partBlob[at + 2];
    if (nf % 8 || ni % 3 || at + 3 + (size_t)nf + ni > partBlob.size()) break;
    bool idxOk = true;
    for (uint32_t q = 0; q < ni && idxOk; q++) idxOk = partBlob[at + 3 + nf + q] < nf / 8;
    if (!idxOk) break;
    PartMesh P; P.type = (int)type; P.idx = (int)ni;
    glGenVertexArrays(1, &P.vao); glGenBuffers(1, &P.vbo); glGenBuffers(1, &P.ibo);
    glBindVertexArray(P.vao);
    glBindBuffer(GL_ARRAY_BUFFER, P.vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)nf * sizeof(float), &partBlob[at + 3], GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 32, (void*)28);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, P.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)ni * sizeof(uint32_t), &partBlob[at + 3 + nf], GL_STATIC_DRAW);
    glBindVertexArray(0);
    PM.parts.push_back(P);
    at += 3 + (size_t)nf + ni;
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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

// The rigid parts' poses for this frame (plane_parts.glsl partPose, the field's own): the player's aircraft's and each
// traffic aircraft's (by its own controls), four texels an instance (R's columns, T), drawn by kPartPoseFS from the
// aircraft's uniforms or its traffic row
void Renderer::computePartPoses(const FrameParams& fp, const PlaneMesh* player, const PlaneMesh* const* traffic) {
  for (auto& o : poseOwner) o = PoseOwner();
  poseType.clear();
  if (!progPartPose) return;
  std::vector<float> info;
  auto add = [&](int owner, const PlaneMesh* pm, const float* M, bool inside) {
    if (!pm || pm->parts.empty()) return;
    PartInst pl[kMaxPartInst]; const int np = partList(M, inside, pl);
    PoseOwner& O = poseOwner[owner]; O.pm = pm; O.base = (int)poseType.size(); O.n = 0;
    for (int i = 0; i < np && (int)poseType.size() < kMaxPoseInst; i++) {
      bool have = false; for (auto& P : pm->parts) have = have || P.type == pl[i].type;
      if (!have) continue;
      poseType.push_back(pl[i].type); O.n++;
      info.insert(info.end(), {(float)pl[i].type, (float)owner, pl[i].sx, pl[i].sy});
    }
  };
  add(0, player, fp.plane.M, fp.plane.PS[3] > 0.5f);
  for (int k = 0; k < std::min(fp.trafficN, kMaxTrafficDrawn); k++) if (traffic && traffic[k]) add(k + 1, traffic[k], fp.traffic[k].t, false);
  const int n = (int)poseType.size();
  if (!n) { for (auto& o : poseOwner) o = PoseOwner(); return; }
  if (!texPartPose) {
    glGenTextures(1, &texPartPose); glBindTexture(GL_TEXTURE_2D, texPartPose);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kMaxPoseInst * 4, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, &texPartInfo); glBindTexture(GL_TEXTURE_2D, texPartInfo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kMaxPoseInst, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fboPartPose); glBindFramebuffer(GL_FRAMEBUFFER, fboPartPose);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texPartPose, 0);
  }
  glBindTexture(GL_TEXTURE_2D, texPartInfo);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RGBA, GL_FLOAT, info.data());
  GLint prevFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fboPartPose);
  { GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0); }
  glViewport(0, 0, n * 4, 1);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  setRT(progPartPose, fp);
  glActiveTexture(GL_TEXTURE0 + 31); glBindTexture(GL_TEXTURE_2D, texPartInfo); glUniform1i(U(progPartPose, "uPPInfo"), 31);
  glBindVertexArray(vaoEmpty); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0 + 31); glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(0, 0, rw, rh);
}

// Each rigid part instance of an aircraft (trafK -1 the player's, else traffic aircraft k) at this frame's pose, with
// the bound program (its other uniforms and the depth state the caller's); nothing when the poses are not this mesh's
void Renderer::drawPlaneParts(const PlaneMesh& pm, GLuint prog, int trafK) {
  if (trafK < -1 || trafK >= kMaxTrafficDrawn) return;
  const PoseOwner& O = poseOwner[trafK + 1];
  if (O.pm != &pm || !O.n) return;
  glActiveTexture(GL_TEXTURE0 + 30); glBindTexture(GL_TEXTURE_2D, texPartPose); glUniform1i(U(prog, "uPartPose"), 30);
  for (int i = O.base; i < O.base + O.n; i++) {
    const PartMesh* P = nullptr; for (auto& q : pm.parts) if (q.type == poseType[i]) P = &q;
    if (!P || !P->idx) continue;
    glUniform1i(U(prog, "uPartInst"), i);
    glBindVertexArray(P->vao);
    glDrawElements(GL_TRIANGLES, P->idx, GL_UNSIGNED_INT, nullptr);
  }
  glUniform1i(U(prog, "uPartInst"), -1);
  glBindVertexArray(pm.vao);
  glActiveTexture(GL_TEXTURE0);
}

// A static airframe into the G-buffer (the target, viewport and depth test are the caller's): the player's aircraft
// (trafK -1) or traffic aircraft k, placed by rot / pos
// The mesh's depth alone (and, for a research craft's sealed cockpit, its screens' depth first): run at the start of
// the frame for the player's aircraft (rasterWorld), so the scenery, the terrain and the sea behind the cabin walls or
// the airframe fail the depth test before they are shaded, and again by drawPlaneMesh for a mesh that was not
// the window cut's uniforms (cabin_windows.glsl): on, the eye in the body frame, the model, the bomb camera's pane
void Renderer::setScreenCut(GLuint p, const FrameParams& fp, bool on) {
  glUniform1i(U(p, "uScrSkip"), on ? 1 : 0);
  if (!on) return;
  glUniform3f(U(p, "uScrEye"), fp.plane.M[22 * 4], fp.plane.M[22 * 4 + 1], fp.plane.M[22 * 4 + 2]);
  glUniform1i(U(p, "uScrModel"), (int)(fp.plane.M[2] + 0.5f));
  glUniform1i(U(p, "uBombPane"), fp.fx.feed[3] > 0.5f ? 1 : 0);
}

void Renderer::drawPlaneMeshDepth(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK) {
  if (!pm.ok || !pm.idx) return;
  // (a near plane at 1 cm: in the cockpit the panel is closer than 0.5 m. From the camera, the aircraft placed relative
  // to it: plane_mesh_vs.glsl)
  mat4 vp = viewProjRel(fp, 0.01f, 2000.f);
  const vec3 rp = pos - fp.camPos;
  const float logC = 2.f / log2f(40000.f + 1.f);
  glBindVertexArray(pm.vao);
  // the research craft's displays as windows: the cabin is sealed, so a screen must be a hole through the whole
  // airframe, not a missing pane with the pod's structure behind it. The pre-pass and the material pass cut every
  // fragment the eye sees through a pane (cabin_windows.glsl), and the world drawn before the airframe stays (the
  // bomb camera's pane keeps its picture while it has one)
  const bool scrSkip = screenWindows && trafK < 0 && fp.plane.PS[3] > 0.5f && (int)(fp.plane.M[2] + 0.5f) >= 5;
  {
    glUseProgram(progPlaneMeshDepth);
    glUniformMatrix4fv(U(progPlaneMeshDepth, "uVP"), 1, GL_FALSE, vp.m);
    glUniform2f(U(progPlaneMeshDepth, "uJit"), jitX, jitY);
    glUniform1f(U(progPlaneMeshDepth, "uLogC"), logC);
    glUniformMatrix3fv(U(progPlaneMeshDepth, "uRot"), 1, GL_FALSE, rot);
    glUniform3f(U(progPlaneMeshDepth, "uPos"), rp.x, rp.y, rp.z);
    setScreenCut(progPlaneMeshDepth, fp, scrSkip);
    {   // a cloaked XR-40 (the player's, outside): its front's body z, as the mesh pass's own test
      const PlaneVisual& pv = fp.plane;
      const bool ck = trafK < 0 && (int)(pv.M[2] + 0.5f) == 6 && pv.wr[4][3] > 0.001f && pv.PS[3] < 0.5f;
      glUniform1f(U(progPlaneMeshDepth, "uCloakZ"), ck ? pv.wr[6][1] : -1e9f);
    }
    glUniform1i(U(progPlaneMeshDepth, "uPartInst"), -1);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
    drawPlaneParts(pm, progPlaneMeshDepth, trafK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::drawPlaneMesh(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK, bool depthDone) {
  if (!pm.ok || !pm.idx) return;
  // depth first, with nothing shaded: the skin's far side, the far wing and the cabin's hidden surfaces never run the
  // material shader (already done when the frame began with it)
  static const bool noPre = getenv("MESHNOPRE") != nullptr;   // (debug: no depth pre-pass)
  if (!noPre && !depthDone) drawPlaneMeshDepth(fp, pm, rot, pos, trafK);
  mat4 vp = viewProjRel(fp, 0.01f, 2000.f);
  const vec3 rp = pos - fp.camPos;
  const float logC = 2.f / log2f(40000.f + 1.f);
  const bool scrSkip = screenWindows && trafK < 0 && fp.plane.PS[3] > 0.5f && (int)(fp.plane.M[2] + 0.5f) >= 5;
  // (this aircraft's own build of the program: the light aircraft's leaves the research jets out - pickAfPrograms)
  const float eng = trafK >= 0 ? fp.traffic[trafK].t[2] : fp.plane.M[2];
  static const bool all = getenv("AF_ALL") != nullptr;
  const GLuint prog = progPlaneMeshV[eng > 4.5f || all || !progPlaneMeshV[1] ? 0 : 1];
  glBindVertexArray(pm.vao);
  // then the materials on exactly the nearest surface
  setRT(prog, fp);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  glUniformMatrix4fv(U(prog, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(U(prog, "uJit"), jitX, jitY);
  glUniform1f(U(prog, "uLogC"), logC);
  glUniformMatrix3fv(U(prog, "uRot"), 1, GL_FALSE, rot);
  glUniform3f(U(prog, "uPos"), rp.x, rp.y, rp.z);
  glUniform1i(U(prog, "uMeshTraffic"), trafK);
  setScreenCut(prog, fp, scrSkip);
  glUniform1i(U(prog, "uPartInst"), -1);
  if (!noPre) { glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); }
  glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);   // (the airframe and the cabin's fine patch, which lies on the surface: one draw)
  drawPlaneParts(pm, prog, trafK);   // (its moving parts, each at its pose)
  glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

// Whether this build's aircraft meshes are in the cache: any file of its stamp (a launch after the first reads them all;
// an update that changes the aircraft builds them all again) - for the loading bar's pacing and wording
void Renderer::checkMeshCache() {
  meshCached = false;
  if (g_shaderCacheDir.empty()) return;
  const std::string tail = "_" + meshCacheStamp() + ".bin";
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(g_shaderCacheDir, ec)) {
    const std::string n = e.path().filename().string();
    if (n.size() > tail.size() && n.compare(0, 5, "mesh_") == 0 && n.compare(n.size() - tail.size(), tail.size(), tail) == 0) { meshCached = true; return; }
  }
}

uint64_t Renderer::trafficModelKey(const float* t) const {
  uint64_t h = 1469598103934665603ull;
  const uint8_t* b = (const uint8_t*)t;
  for (size_t i = 0; i < sizeof(float) * 96; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
