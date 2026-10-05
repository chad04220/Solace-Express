// Solace Express - the aircraft hull's faces from its voxels (aircraft_hull.cpp builds the voxels; hull_test checks
// the mesh is closed). Level-1 voxels: 0 empty, 1 solid, 2 split into 4x4x4 sub-voxels (mask, bit x + 4y + 16z).
// With `fine`, every face is laid on the sub-voxel lattice (unit U = the sub-voxel size), else on the voxel lattice.
#pragma once
#include "common.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

// The airframe's moving parts' states the hull bake and the mesh bake sweep: the gear, flaps, steering and the
// controls (and in the cockpit the yoke and the throttle) each through their range, the others at rest. M: the
// packed model (models.cpp packModel), inside: the cockpit field. The first state is the rest state.
struct HullState { float ps[4], ctl[4]; };
const float kS0 = 1.f, kS1 = 0.25f, kS2 = 0.0625f;   // cell sizes of the three voxel levels
inline float halfDiag(float s) { return s * 0.8660254f; }
inline std::vector<HullState> hullStateList(const float* M, bool inside) {
  std::vector<HullState> st;
  auto add = [&](float gear, float flaps, float steer, float p, float r, float y, float thr) {
    st.push_back({{gear, flaps, steer, inside ? 1.f : 0.f}, {p, r, y, thr}});
  };
  bool retract = (int)(M[1] + 0.5f) >= 3;
  add(1, 0, 0, 0, 0, 0, 0);   // rest: gear down, flaps up, controls centred
  if (retract) {   // dense where the doors swing (the first fifth of the travel), then every 1/16
    for (int i = 0; i <= 8; i++) add(0.025f * i, 0, 0, 0, 0, 0, 0);
    for (int i = 1; i <= 12; i++) add(0.2f + 0.8f * i / 13.f, 0, 0, 0, 0, 0, 0);
  }
  for (int i = 0; i <= 8; i++) {
    float u = i / 8.f, s = u * 2.f - 1.f;
    add(1, u, 0, 0, 0, 0, 0);                // flaps
    add(1, 0, 0.45f * s, 0, 0, 0, 0);        // nose / tail wheel steering
    add(1, 0, 0, s, 0, 0, 0);                // elevator (and the yoke's pull)
    add(1, 0, 0, 0, s, 0, 0);                // ailerons (and the yoke's turn)
    add(1, 0, 0, 0, 0, s, 0);                // rudder and pedals
  }
  if (inside) {
    for (int i = 0; i <= 4; i++) for (int k = 0; k <= 4; k++) add(1, 0, 0, i * 0.5f - 1.f, k * 0.5f - 1.f, 0, 0);   // yoke
    for (int i = 0; i <= 4; i++) add(1, 0, 0, 0, 0, 0, i * 0.25f);                                                  // throttle
  }
  if (st.size() > 128) st.resize(128);
  return st;
}
// the cabin's box (body space), where the cockpit hull refines to 6.25 cm voxels
inline void cabinBox(const float* M, float* lo, float* hi) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  float E[3] = {m(22, 0), m(22, 1), m(22, 2)}, pz = m(21, 3);
  lo[0] = -1.1f; lo[1] = E[1] - 1.25f; lo[2] = std::min(pz, E[2]) - 0.7f;
  hi[0] = 1.1f; hi[1] = E[1] + 0.5f; hi[2] = E[2] + 1.6f;
}

inline void hullFaces(int n1, float org, float U, bool fine, const std::vector<uint8_t>& state,
                      const std::unordered_map<int, uint64_t>& mask, std::vector<float>& tri) {
  // Faces between solid and empty space, as a watertight mesh: every corner is computed from integer lattice
  // coordinates by one formula, so faces that meet share their edges bit for bit and rasterize without a pinhole. In
  // the cockpit hull every face is laid on the 6.25 cm lattice (a 0.25 m face as its 16 parts), so no edge of a small
  // face ever ends on the side of a big one either.
  auto quadL = [&](int cx, int cy, int cz, int a, int sg) {   // face of lattice cell (cx,cy,cz), outward along +-axis a
    int c[3] = {cx, cy, cz}, u = (a + 1) % 3, v = (a + 2) % 3;
    int pa = c[a] + (sg > 0 ? 1 : 0);
    auto P = [&](int du, int dv) { int l[3]; l[a] = pa; l[u] = c[u] + du; l[v] = c[v] + dv; return vec3(org + l[0] * U, org + l[1] * U, org + l[2] * U); };
    vec3 q0 = P(0, 0), q1 = P(1, 0), q2 = P(1, 1), q3 = P(0, 1);
    vec3 o[6] = {q0, q1, q2, q0, q2, q3};
    if (sg < 0) { std::swap(o[1], o[2]); std::swap(o[4], o[5]); }
    for (auto& w : o) { tri.push_back(w.x); tri.push_back(w.y); tri.push_back(w.z); }
  };
  auto coarseFace = [&](int i, int j, int k, int a, int sg) {   // a whole 0.25 m face
    if (!fine) { quadL(i, j, k, a, sg); return; }
    for (int p = 0; p < 4; p++) for (int q = 0; q < 4; q++) {
      int own[3]; own[a] = sg > 0 ? 3 : 0; own[(a + 1) % 3] = p; own[(a + 2) % 3] = q;
      quadL(i * 4 + own[0], j * 4 + own[1], k * 4 + own[2], a, sg);
    }
  };
  auto st1 = [&](int i, int j, int k) -> int { if (i < 0 || j < 0 || k < 0 || i >= n1 || j >= n1 || k >= n1) return 0; return state[((size_t)k * n1 + j) * n1 + i]; };
  auto sub = [&](int i, int j, int k, int si, int sj, int sk) -> bool {   // is sub-voxel (si,sj,sk) of level-1 voxel (i,j,k) solid
    int s = st1(i, j, k);
    if (s != 2) return s == 1;
    return (mask.at((k * n1 + j) * n1 + i) >> (si + sj * 4 + sk * 16)) & 1;
  };
  const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int k = 0; k < n1; k++) for (int j = 0; j < n1; j++) for (int i = 0; i < n1; i++) {
    int s = st1(i, j, k);
    if (s == 0) continue;
    for (int f = 0; f < 6; f++) {
      int a = f / 2, sg = dirs[f][a];
      int ni = i + dirs[f][0], nj = j + dirs[f][1], nk = k + dirs[f][2];
      if (s == 1) {
        int ns2 = st1(ni, nj, nk);
        if (ns2 == 1) continue;
        if (ns2 == 0) { coarseFace(i, j, k, a, sg); continue; }
        for (int p = 0; p < 4; p++) for (int q = 0; q < 4; q++) {   // the neighbour's sub-voxels against this face
          int sv[3]; sv[a] = sg > 0 ? 0 : 3; sv[(a + 1) % 3] = p; sv[(a + 2) % 3] = q;
          if (sub(ni, nj, nk, sv[0], sv[1], sv[2])) continue;
          int own[3] = {sv[0], sv[1], sv[2]}; own[a] = sg > 0 ? 3 : 0;
          quadL(i * 4 + own[0], j * 4 + own[1], k * 4 + own[2], a, sg);
        }
      } else {
        uint64_t mk = mask.at((int)(((size_t)k * n1 + j) * n1 + i));
        for (int c = 0; c < 64; c++) {
          if (!((mk >> c) & 1)) continue;
          int sv[3] = {c & 3, (c >> 2) & 3, c >> 4};
          int t[3] = {sv[0] + dirs[f][0], sv[1] + dirs[f][1], sv[2] + dirs[f][2]};
          bool solid;
          if (t[a] >= 0 && t[a] <= 3) solid = (mk >> (t[0] + t[1] * 4 + t[2] * 16)) & 1;
          else { t[a] = (t[a] + 4) & 3; solid = sub(ni, nj, nk, t[0], t[1], t[2]); }
          if (!solid) quadL(i * 4 + sv[0], j * 4 + sv[1], k * 4 + sv[2], a, sg);
        }
      }
    }
  }
}
