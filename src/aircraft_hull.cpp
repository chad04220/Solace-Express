// Solace Express - aircraft hull meshes. The light aircraft are exact distance-field shapes; finding them took every
// pixel near the aircraft up to a hundred and more samples of the whole airframe (the cockpit view ~28 on average).
// Each airframe now gets a triangle hull, built once from that same shape: a closed surface of voxel faces that
// holds the airframe in every position of its gear, flaps, steering and controls. Rasterized with the aircraft, the
// distance to it on each pixel is where the exact march starts; a pixel the hull misses has no aircraft on it at all.
// The surface drawn is still the exact shape, with all its detail, and the hull can't open a seam in it.
//
// Built at three resolutions: 1 m cells find where the airframe is, 0.25 m voxels hold the outside, and in the
// cockpit view 6.25 cm voxels hold the cabin (so the hull stays clear of the pilot's eye). A voxel is solid when the
// airframe, in any of the sampled states, may reach into it: its centre distance is under its half diagonal (with
// room for the distance field's slack) plus the furthest a part moves between two sampled states.
#include "renderer.h"
#include <unordered_map>

namespace {
const char* kHullVS = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uVP; uniform vec2 uJit; uniform mat3 uRot; uniform vec3 uPos;
out vec3 vW;
void main(){
  vW = uRot*aPos + uPos;
  gl_Position = uVP*vec4(vW, 1.0);
  gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the ray tracer's sub-pixel jitter
}
)";
const char* kHullFS = R"(
in vec3 vW; uniform vec3 uCam; uniform float uFree; uniform int uPass;   // 0: inside faces only, 1: outside faces only
out vec2 oT;   // written to the second channel only: distance along the pixel's ray, 0 on the hull's inside
// Faces nearer than uFree are dropped: no part of the airframe is that close to the camera, so a ray may cross them
// freely. Any face it then meets from inside solid space is a back face, and that pixel marches from the camera.
void main(){
  float t = length(vW - uCam);
  if (t < uFree || gl_FrontFacing != (uPass == 1)) discard;
  oT = vec2(0.0, gl_FrontFacing ? t : 0.0);
}
)";
struct HullState { float ps[4], ctl[4]; };
const float kS0 = 1.f, kS1 = 0.25f, kS2 = 0.0625f;   // cell sizes of the three levels
float halfDiag(float s) { return s * 0.8660254f; }
}

bool Renderer::compileHull(const std::string& bakeVS, const std::string& bakeFS) {
  std::string hdr = "#version 330 core\n", e;
  progHull = linkProgramCached(hdr + kHullVS, hdr + kHullFS, e);
  progHullBake = linkProgramCached(bakeVS, bakeFS, e);
  return progHull && progHullBake;
}

// distances of a list of aircraft-space points (the bake program and its uniforms are already bound)
void Renderer::hullEval(const std::vector<vec3>& pts, std::vector<float>& out) {
  const int TW = 512;
  int n = (int)pts.size(), rows = (n + TW - 1) / TW;
  out.assign(n, 1e9f);
  if (!n) return;
  std::vector<float> buf((size_t)TW * rows * 4, 1e4f);
  for (int i = 0; i < n; i++) { buf[(size_t)i * 4] = pts[i].x; buf[(size_t)i * 4 + 1] = pts[i].y; buf[(size_t)i * 4 + 2] = pts[i].z; }
  if (!texHPts) {
    glGenTextures(1, &texHPts); glGenTextures(1, &texHOut); glGenFramebuffers(1, &fboHOut);
  }
  glActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, texHPts);   // (19: a unit the bake doesn't otherwise use)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, TW, rows, 0, GL_RGBA, GL_FLOAT, buf.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glUniform1i(glGetUniformLocation(progHullBake, "uHPts"), 19);
  glActiveTexture(GL_TEXTURE0 + 18);   // (the result texture is only created here; it is not sampled)
  glBindTexture(GL_TEXTURE_2D, texHOut);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, TW, rows, 0, GL_RED, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, fboHOut);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texHOut, 0);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
  glBindVertexArray(vaoEmpty);
  // in bands of rows, each finished before the next: one long draw could trip the driver's watchdog
  for (int r0 = 0; r0 < rows; r0 += 32) {
    int r1 = std::min(rows, r0 + 32);
    glEnable(GL_SCISSOR_TEST); glScissor(0, r0, TW, r1 - r0);
    glViewport(0, 0, TW, rows);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SCISSOR_TEST);
    glFinish();
  }
  std::vector<float> res((size_t)TW * rows);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, TW, rows, GL_RED, GL_FLOAT, res.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // leave units 18 and 19 as the other passes expect them (18: the baked terrain shadow, read without rebinding by
  // the passes that run before the ray tracer sets its textures)
  glActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, tshFront >= 0 ? texTSh[tshFront] : 0);
  glActiveTexture(GL_TEXTURE0);
  for (int i = 0; i < n; i++) out[i] = res[i];
}

// Bake the hull of the current airframe into slot (0 outside, 1 cockpit). The bake program is bound with the ray
// tracer's uniforms for this frame.
void Renderer::bakeHull(const FrameParams& fp, int slot, uint64_t key) {
  const PlaneVisual& pv = fp.plane;
  const float* M = pv.M;   // 24 vec4
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  bool inside = slot == 1;
  bool retract = (int)(m(0, 1) + 0.5f) >= 3;
  // the states: each moving part swept through its range (the others at rest), the yoke through pull x turn
  std::vector<HullState> st;
  auto add = [&](float gear, float flaps, float steer, float p, float r, float y, float thr) {
    st.push_back({{gear, flaps, steer, inside ? 1.f : 0.f}, {p, r, y, thr}});
  };
  if (retract) {   // dense where the doors swing (the first fifth of the travel), then every 1/16
    for (int i = 0; i <= 8; i++) add(0.025f * i, 0, 0, 0, 0, 0, 0);
    for (int i = 1; i <= 13; i++) add(0.2f + 0.8f * i / 13.f, 0, 0, 0, 0, 0, 0);
  } else add(1, 0, 0, 0, 0, 0, 0);
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
  int ns = std::min((int)st.size(), 128);
  std::vector<float> sps(128 * 4, 0.f), sct(128 * 4, 0.f);
  for (int i = 0; i < ns; i++) for (int c = 0; c < 4; c++) { sps[i * 4 + c] = st[i].ps[c]; sct[i * 4 + c] = st[i].ctl[c]; }
  glUniform1i(glGetUniformLocation(progHullBake, "uHStN"), ns);
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStPS"), 128, sps.data());
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStCtl"), 128, sct.data());

  const float slack = 1.3f;   // the distance field may overstate distances by up to ~25%
  const float d1 = slack * halfDiag(kS1) + 0.05f + 0.02f;   // 0.25 m voxels: + half the largest step between states
  const float d2 = slack * halfDiag(kS2) + 0.035f + 0.01f;  // cabin voxels: the yoke and levers move least per step
  float L = m(0, 0), span = m(9, 0);
  float br = std::max(L, span * 2.f) * 0.55f + 1.5f;
  int n0 = 2 * (int)ceilf(br / kS0); float org = -n0 * 0.5f * kS0;   // cube of level-0 cells around the origin
  int n1 = n0 * 4;
  // the cabin (cockpit hull only), in level-1 cells
  float E[4] = {m(22, 0), m(22, 1), m(22, 2), m(22, 3)}, pz = m(21, 3);
  float cb0[3] = {-1.1f, E[1] - 1.25f, std::min(pz, E[2]) - 0.7f}, cb1[3] = {1.1f, E[1] + 0.5f, E[2] + 1.6f};
  auto inCabin = [&](int i, int j, int k) {
    if (!inside) return false;
    float lo[3] = {org + i * kS1, org + j * kS1, org + k * kS1};
    for (int a = 0; a < 3; a++) if (lo[a] + kS1 <= cb0[a] || lo[a] >= cb1[a]) return false;
    return true;
  };
  auto centre = [&](float s, int i, int j, int k) { return vec3(org + (i + 0.5f) * s, org + (j + 0.5f) * s, org + (k + 0.5f) * s); };
  // level 0: where is the airframe at all
  std::vector<vec3> pts; std::vector<float> d;
  for (int k = 0; k < n0; k++) for (int j = 0; j < n0; j++) for (int i = 0; i < n0; i++) pts.push_back(centre(kS0, i, j, k));
  hullEval(pts, d);
  float keep0 = slack * halfDiag(kS0) + std::max(d1, slack * halfDiag(kS1) + d2);
  // level 1: 0.25 m voxels in the kept cells
  std::vector<int> c1;   // packed level-1 indices evaluated
  pts.clear();
  for (int k = 0; k < n0; k++) for (int j = 0; j < n0; j++) for (int i = 0; i < n0; i++) {
    if (d[((size_t)k * n0 + j) * n0 + i] >= keep0) continue;
    for (int c = 0; c < 64; c++) {
      int ii = i * 4 + (c & 3), jj = j * 4 + ((c >> 2) & 3), kk = k * 4 + (c >> 4);
      c1.push_back((kk * n1 + jj) * n1 + ii);
      pts.push_back(centre(kS1, ii, jj, kk));
    }
  }
  hullEval(pts, d);
  // per level-1 voxel: 0 empty, 1 solid, 2 refined (its 64 sub-voxels in mask)
  std::vector<uint8_t> state((size_t)n1 * n1 * n1, 0);
  std::unordered_map<int, uint64_t> mask;
  std::vector<int> refine;
  for (size_t q = 0; q < c1.size(); q++) {
    int id = c1[q], ii = id % n1, jj = (id / n1) % n1, kk = id / (n1 * n1);
    if (inCabin(ii, jj, kk)) { if (d[q] < slack * halfDiag(kS1) + d2) refine.push_back(id); }
    else if (d[q] < d1) state[id] = 1;
  }
  // level 2: the cabin at 6.25 cm
  pts.clear();
  for (int id : refine) {
    int ii = id % n1, jj = (id / n1) % n1, kk = id / (n1 * n1);
    for (int c = 0; c < 64; c++) pts.push_back(centre(kS2, ii * 4 + (c & 3), jj * 4 + ((c >> 2) & 3), kk * 4 + (c >> 4)));
  }
  pts.push_back(vec3(E[0], E[1], E[2]));   // the eye itself
  hullEval(pts, d);
  float dEye = d.back();
  for (size_t r = 0; r < refine.size(); r++) {
    uint64_t mk = 0;
    for (int c = 0; c < 64; c++) if (d[r * 64 + c] < d2) mk |= 1ull << c;
    if (mk == ~0ull) state[refine[r]] = 1;
    else if (mk) { state[refine[r]] = 2; mask[refine[r]] = mk; }
  }
  // faces between solid and empty space; where a solid 0.25 m voxel meets a refined one, its face is split into the
  // 16 sub-faces and only those in front of empty sub-voxels are kept, so the two resolutions join without a gap
  std::vector<float> tri;
  // face of a cube with centre c, half size h, outward along +-axis a. Faces of different sizes meet in T-junctions,
  // where rasterization can leave pinholes a ray would slip through; each face reaches 2 mm past its edges to close
  // them (more hull only ever starts a ray earlier).
  auto quad = [&](vec3 c, int a, int sg, float h) {
    int u = (a + 1) % 3, v = (a + 2) % 3;
    vec3 base = c; (&base.x)[a] += sg * h;
    vec3 eu, ev; (&eu.x)[u] = h + 0.002f; (&ev.x)[v] = h + 0.002f;
    vec3 q0 = base - eu - ev, q1 = base + eu - ev, q2 = base + eu + ev, q3 = base - eu + ev;
    vec3 o[6] = {q0, q1, q2, q0, q2, q3};
    if (sg < 0) { std::swap(o[1], o[2]); std::swap(o[4], o[5]); }
    for (auto& w : o) { tri.push_back(w.x); tri.push_back(w.y); tri.push_back(w.z); }
  };
  auto st1 = [&](int i, int j, int k) -> int { if (i < 0 || j < 0 || k < 0 || i >= n1 || j >= n1 || k >= n1) return 0; return state[((size_t)k * n1 + j) * n1 + i]; };
  auto sub = [&](int i, int j, int k, int si, int sj, int sk) -> bool {   // is sub-voxel (si,sj,sk) of level-1 voxel (i,j,k) solid
    int s = st1(i, j, k);
    if (s != 2) return s == 1;
    return (mask[((k * n1 + j) * n1 + i)] >> (si + sj * 4 + sk * 16)) & 1;
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
        if (ns2 == 0) { quad(centre(kS1, i, j, k), a, sg, kS1 * 0.5f); continue; }
        for (int p = 0; p < 4; p++) for (int q = 0; q < 4; q++) {   // the neighbour's sub-voxels against this face
          int sv[3]; sv[a] = sg > 0 ? 0 : 3; sv[(a + 1) % 3] = p; sv[(a + 2) % 3] = q;
          if (sub(ni, nj, nk, sv[0], sv[1], sv[2])) continue;
          int own[3] = {sv[0], sv[1], sv[2]}; own[a] = sg > 0 ? 3 : 0;
          quad(centre(kS2, i * 4 + own[0], j * 4 + own[1], k * 4 + own[2]), a, sg, kS2 * 0.5f);
        }
      } else {
        uint64_t mk = mask[(int)(((size_t)k * n1 + j) * n1 + i)];
        for (int c = 0; c < 64; c++) {
          if (!((mk >> c) & 1)) continue;
          int sv[3] = {c & 3, (c >> 2) & 3, c >> 4};
          int t[3] = {sv[0] + dirs[f][0], sv[1] + dirs[f][1], sv[2] + dirs[f][2]};
          bool solid;
          if (t[a] >= 0 && t[a] <= 3) solid = (mk >> (t[0] + t[1] * 4 + t[2] * 16)) & 1;
          else { t[a] = (t[a] + 4) & 3; solid = sub(ni, nj, nk, t[0], t[1], t[2]); }
          if (!solid) quad(centre(kS2, i * 4 + sv[0], j * 4 + sv[1], k * 4 + sv[2]), a, sg, kS2 * 0.5f);
        }
      }
    }
  }
  HullMesh& H = hull[slot];
  if (!H.vbo) glGenBuffers(1, &H.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(float), tri.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  H.verts = (int)tri.size() / 3; H.key = key;
  // the eye may sit in solid voxels (they reach up to ~17 cm from the airframe): nothing is nearer to it than the
  // exact distance there, so the hull faces within that ball are ignored (see kHullFS)
  H.ok = true; H.free = inside ? std::max(0.f, dEye / slack - 0.005f) : 0.f;
  if (getenv("HULLDBG")) printf("hull %s: %d states, %d+%d+%d points, %zu refined, %d triangles%s\n", inside ? "cockpit" : "outside", ns,
                                n0 * n0 * n0, (int)c1.size(), (int)refine.size() * 64, refine.size(), H.verts / 3, H.free > 0.f ? " (clear ball at the eye)" : "");
}

uint64_t Renderer::hullKey(const FrameParams& fp, int slot) const {
  const PlaneVisual& pv = fp.plane;
  uint64_t h = 1469598103934665603ull ^ (uint64_t)slot;
  auto mix = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } };
  mix(pv.M, sizeof(float) * 96);
  if (slot == 0) { mix(&pv.lensN, sizeof(int)); mix(pv.lensP, sizeof(float) * 4 * pv.lensN); mix(pv.lensD, sizeof(float) * 4 * pv.lensN); }
  return h;
}

// Light aircraft only (the research jets' shapes have more moving parts than these states cover), not wrecks, and
// for now the outside views only: in the cabin a march started from the hull can still step over a few of the finest
// fittings (vent rims, bezels) that the march from the eye happens to land on (HULLCOCKPIT turns it on to test).
bool Renderer::hullWanted(const FrameParams& fp) const {
  const PlaneVisual& pv = fp.plane;
  static const bool off = getenv("HULLOFF") != nullptr, cockpit = getenv("HULLCOCKPIT") != nullptr;
  if (pv.PS[3] > 0.5f && !cockpit) return false;
  return !off && progHull && progHullBake && pv.on && fp.wreck.pieces == 0 && pv.M[2] < 4.5f;
}

void Renderer::drawHull(const FrameParams& fp, int slot) {
  hullOn = false;
  HullMesh& H = hull[slot];
  if (!H.ok || !H.verts || !fboEnv) return;
  if (!fboHull) glGenFramebuffers(1, &fboHull);
  if (!texHullDepth || hullDepthW != rw || hullDepthH != rh) {
    if (texHullDepth) glDeleteTextures(1, &texHullDepth);
    glGenTextures(1, &texHullDepth); glBindTexture(GL_TEXTURE_2D, texHullDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, rw, rh, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    hullDepthW = rw; hullDepthH = rh;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fboHull);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texEnv, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texHullDepth, 0);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
  float far[4] = {0, 1e30f, 0, 0};   // no hull on this pixel: no aircraft along its ray
  glClearBufferfv(GL_COLOR, 0, far);
  glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glUseProgram(progHull);
  mat4 vp = viewProj(fp, 0.01f, 400.f);   // a near plane at 1 cm: in the cockpit the seats and controls are closer than 0.5 m
  glUniformMatrix4fv(glGetUniformLocation(progHull, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(glGetUniformLocation(progHull, "uJit"), jitX, jitY);
  glUniform3f(glGetUniformLocation(progHull, "uCam"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  // the clear ball was measured at the design eye; the camera may sit off it (head movement, zoom)
  float freeR = 0.f;
  if (slot == 1 && H.free > 0.f) {
    const float* R = fp.plane.rot; vec3 dl = fp.camPos - fp.plane.pos;
    vec3 lc(R[0] * dl.x + R[1] * dl.y + R[2] * dl.z, R[3] * dl.x + R[4] * dl.y + R[5] * dl.z, R[6] * dl.x + R[7] * dl.y + R[8] * dl.z);
    freeR = std::max(0.f, H.free - length(lc - vec3(fp.plane.M[88], fp.plane.M[89], fp.plane.M[90])));
  }
  glUniform1f(glGetUniformLocation(progHull, "uFree"), freeR);
  glUniformMatrix3fv(glGetUniformLocation(progHull, "uRot"), 1, GL_FALSE, fp.plane.rot);
  glUniform3f(glGetUniformLocation(progHull, "uPos"), fp.plane.pos.x, fp.plane.pos.y, fp.plane.pos.z);
  if (!vaoHull) glGenVertexArrays(1, &vaoHull);
  glBindVertexArray(vaoHull); glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
  // Where faces meet in the same plane (the 2 mm overlaps, voxels touching along an edge) an inside face and an
  // outside face can lie at the same depth: the inside one must win, or a ray inside solid space would take the outside
  // face for its start. So the inside faces go first, and the outside faces are pushed back a hair.
  GLint lp = glGetUniformLocation(progHull, "uPass");
  glUniform1i(lp, 0); glDrawArrays(GL_TRIANGLES, 0, H.verts);
  glUniform1i(lp, 1); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.f, 4.f);
  glDrawArrays(GL_TRIANGLES, 0, H.verts);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  static const char* vis = getenv("HULLVIS");   // (debug: the hull channel as an image - grey by distance, red inside, black missed)
  if (vis) {
    std::vector<float> px((size_t)rw * rh * 2);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0, 0, rw, rh, GL_RG, GL_FLOAT, px.data());
    if (FILE* f = fopen(vis, "wb")) {
      fprintf(f, "P6\n%d %d\n255\n", rw, rh);
      for (int y = rh - 1; y >= 0; y--) for (int x = 0; x < rw; x++) {
        float v = px[((size_t)y * rw + x) * 2 + 1]; unsigned char c[3] = {0, 0, 0};
        if (v <= 0.f) c[0] = 255; else if (v < 1e29f) c[0] = c[1] = c[2] = (unsigned char)std::clamp(255.f - v * 40.f, 20.f, 255.f);
        fwrite(c, 1, 3, f);
      }
      fclose(f);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  hullOn = true;
}
