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
#include "hull_mesh.h"
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
uniform int uChan;   // 1: the player's aircraft (second channel), 2: the traffic (third)
out vec4 oT;   // distance along the pixel's ray, 0 on the hull's inside (only the channel being drawn is written)
// Faces nearer than uFree are dropped: the ray tracer marches every ray that far itself (in the cockpit the eye sits
// inside the hull's margins, a few centimetres from the cabin roof). Beyond it, a ray in empty space meets an outside
// face first (nothing lies before it), and a ray inside solid space an inside face (it just keeps marching).
// Pass 2 (the player's moving hull): the farthest inside face along the ray into the fourth channel (MAX blending), the
// end of the hull volume: past it nothing moves on this ray, so the march stops there and the mesh is the airframe.
void main(){
  float t = length(vW - uCam);
  if (uPass == 2) { if (gl_FrontFacing) discard; oT = vec4(0.0, 0.0, 0.0, t); return; }
  if (t < uFree || gl_FrontFacing != (uPass == 1)) discard;
  float v = gl_FrontFacing ? t : 0.0;
  oT = uChan == 2 ? vec4(0.0, 0.0, v, 0.0) : vec4(0.0, v, 0.0, 0.0);
}
)";
}

bool Renderer::compileHull(const std::string& bakeVS, const std::string& bakeFS) {
  std::string hdr = "#version 330 core\n", e;
  progHull = linkProgramCached(hdr + kHullVS, hdr + kHullFS, e);
  progHullBake = linkProgramCached(bakeVS, bakeFS, e);
  return progHull && progHullBake;
}

// distances of a list of aircraft-space points (the bake program and its uniforms are already bound)
void Renderer::hullEval(const std::vector<vec3>& pts, std::vector<float>& out) {
  std::vector<float> o4;
  hullEval4(pts, o4);
  out.resize(pts.size());
  for (size_t i = 0; i < pts.size(); i++) out[i] = o4[i * 4];
}
// the bake program's four outputs per point (kHullBakeMain: by uHMode the distance | distance, material id, cabin AO
// | the normal)
void Renderer::hullEval4(const std::vector<vec3>& pts, std::vector<float>& out) {
  const int TW = 512;
  int n = (int)pts.size(), rows = (n + TW - 1) / TW;
  out.assign((size_t)n * 4, 1e9f);
  if (!n) return;
  // the point list as a 512-wide texture: no taller than the device allows (the Q400's lattice is 22 million points,
  // 43 thousand rows; a texture past GL_MAX_TEXTURE_SIZE is refused and the bake read back stale data: Codex's
  // fleet review), and no taller than 8192 rows in any case, in batches evaluated one after the other
  static GLint maxTex = 0;
  if (!maxTex) { glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex); if (maxTex < 1024) maxTex = 1024; }
  const int maxRows = std::min(maxTex, 8192);
  if (rows > maxRows) {
    std::vector<vec3> part; std::vector<float> po;
    for (int at = 0; at < n; at += TW * maxRows) {
      int cnt = std::min(n - at, TW * maxRows);
      part.assign(pts.begin() + at, pts.begin() + at + cnt);
      hullEval4(part, po);
      std::copy(po.begin(), po.begin() + (size_t)cnt * 4, out.begin() + (size_t)at * 4);
    }
    return;
  }
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, TW, rows, 0, GL_RGBA, GL_FLOAT, nullptr);
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
    if (bakeYield && bakeDue()) {   // a frame from inside the bake, then this pass's state back
      bakeYield();
      glBindFramebuffer(GL_FRAMEBUFFER, fboHOut); glDrawBuffers(1, &c0);
      glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
      glBindVertexArray(vaoEmpty);
      glActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, texHPts);
    }
  }
  std::vector<float> res((size_t)TW * rows * 4);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, TW, rows, GL_RGBA, GL_FLOAT, res.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // leave units 18 and 19 as the other passes expect them (18: the baked terrain shadow, read without rebinding by
  // the passes that run before the ray tracer sets its textures)
  glActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, tshFront >= 0 ? texTSh[tshFront] : 0);
  glActiveTexture(GL_TEXTURE0);
  std::copy(res.begin(), res.begin() + (size_t)n * 4, out.begin());
  bakeTick();
}

// Bake the hull of the current airframe into slot (0 outside, 1 cockpit). The bake program is bound with the ray
// tracer's uniforms for this frame.
void Renderer::bakeHull(const FrameParams& fp, int slot, uint64_t key) {
  bakeCount++; bakeBuilt++;   // (hulls are not cached on disk)
  const PlaneVisual& pv = fp.plane;
  const float* M = pv.M;   // 24 vec4
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  bool inside = slot == 1;
  // the states: each moving part swept through its range (the others at rest), the yoke through pull x turn
  std::vector<HullState> st = hullStateList(M, inside);
  int ns = (int)st.size();
  std::vector<float> sps(128 * 4, 0.f), sct(128 * 4, 0.f), swr(128 * 4, 0.f), swr2(128 * 4, 0.f);
  for (int i = 0; i < ns; i++) for (int c = 0; c < 4; c++) { sps[i * 4 + c] = st[i].ps[c]; sct[i * 4 + c] = st[i].ctl[c]; swr[i * 4 + c] = st[i].wr[c]; swr2[i * 4 + c] = st[i].wr2[c]; }
  glUniform1i(glGetUniformLocation(progHullBake, "uHPart"), -1);   // (the whole aircraft, its rigid parts posed in each state)
  glUniform1i(glGetUniformLocation(progHullBake, "uHStN"), ns);
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStPS"), 128, sps.data());
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStCtl"), 128, sct.data());
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStWr"), 128, swr.data());
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStWr2"), 128, swr2.data());
  glUniform1i(glGetUniformLocation(progHullBake, "uHMode"), 0);

  const float slack = 1.3f;   // the distance field may overstate distances by up to ~25%
  const float d1 = slack * halfDiag(kS1) + 0.05f + 0.02f;   // 0.25 m voxels: + half the largest step between states
  const float d2 = slack * halfDiag(kS2) + 0.035f + 0.01f;  // cabin voxels: the yoke and levers move least per step
  float L = m(0, 0), span = m(9, 0);
  float br = std::max(L, span * 2.f) * 0.55f + 1.5f;
  int n0 = 2 * (int)ceilf(br / kS0); float org = -n0 * 0.5f * kS0;   // cube of level-0 cells around the origin
  int n1 = n0 * 4;
  // the cabin (cockpit hull only), in level-1 cells
  float E[4] = {m(22, 0), m(22, 1), m(22, 2), m(22, 3)};
  float cb0[3], cb1[3]; cabinBox(M, cb0, cb1);
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
  std::vector<float> tri;
  hullFaces(n1, org, inside ? kS2 : kS1, inside, state, mask, tri);
  HullMesh& H = hulls[key];
  if (!H.vbo) glGenBuffers(1, &H.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(float), tri.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  H.verts = (int)tri.size() / 3; H.key = key;
  H.ok = true;
  if (getenv("HULLDBG")) printf("hull %s: %d states, %d+%d+%d points, %zu refined, %d triangles%s\n", inside ? "cockpit" : "outside", ns,
                                n0 * n0 * n0, (int)c1.size(), (int)refine.size() * 64, refine.size(), H.verts / 3, inside ? fmt(" (airframe %.3f m from the eye)", dEye).c_str() : "");
}

// How far every ray marches before the hull may let it skip ahead. In the cockpit that is the whole space around the
// pilot (panel, glareshield, seats): those surfaces are found exactly as without a hull, edge for edge, and the hull
// speeds up the long rays (out of the windows across the wings and struts, down the cabin).
float Renderer::hullNear(const FrameParams& fp) const { return fp.plane.PS[3] > 0.5f ? 1.2f : 0.f; }

bool Renderer::hullBaked(const FrameParams& fp) const {
  if (!hullWanted(fp)) return true;
  return hulls.count(hullKey(fp, fp.plane.PS[3] > 0.5f ? 1 : 0)) != 0;
}

// AI traffic flies the same light aircraft: each one's outside hull (baked at launch) drawn with its own transform into
// the third channel, the nearest of them per pixel. Only when every traffic aircraft in view has one; else its march
// starts from the camera as before.
void Renderer::drawTrafficHulls(const FrameParams& fp, const PlaneMesh* const* meshes) {
  trafHullOn = false;
  int n = std::min(fp.trafficN, kMaxTrafficDrawn);
  if (hullOff || n == 0 || !progHull || !fboEnv) return;
  std::vector<const HullMesh*> hm(n);
  for (int k = 0; k < n; k++) {
    uint64_t h = meshes && meshes[k] ? meshes[k]->movKey : trafficModelKey(fp.traffic[k].t);   // its moving hull, or its full one
    auto it = hulls.find(h);
    // a meshed aircraft whose moving hull is empty (every moving piece a rigid part) has nothing to march: no hull of
    // its own, and its pixels read "no traffic on this ray" (aborting here left every aircraft marched in full)
    if (meshes && meshes[k] && it != hulls.end() && it->second.ok && !it->second.verts) { hm[k] = nullptr; continue; }
    if (it == hulls.end() || !it->second.ok || !it->second.verts) {
      static int warned = 0;
      if (getenv("HULLDBG") && warned++ < 3) printf("traffic hulls: aircraft %d of %d has none (type %.0f)\n", k, n, fp.traffic[k].t[2]);
      return;
    }
    hm[k] = &it->second;
  }
  ensureHullTarget();
  glBindFramebuffer(GL_FRAMEBUFFER, fboHull);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texEnv, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texHullDepth, 0);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);
  float far[4] = {0, 0, 1e30f, 0};
  glClearBufferfv(GL_COLOR, 0, far);
  glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
  glUseProgram(progHull);
  mat4 vp = viewProj(fp, 0.01f, 2000.f);
  glUniformMatrix4fv(glGetUniformLocation(progHull, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(glGetUniformLocation(progHull, "uJit"), jitX, jitY);
  glUniform3f(glGetUniformLocation(progHull, "uCam"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  glUniform1f(glGetUniformLocation(progHull, "uFree"), 0.f);
  glUniform1i(glGetUniformLocation(progHull, "uChan"), 2);
  if (!vaoHull) glGenVertexArrays(1, &vaoHull);
  glBindVertexArray(vaoHull);
  GLint lp = glGetUniformLocation(progHull, "uPass");
  for (int pass = 0; pass < 2; pass++) {   // all inside faces first, then all outside faces (see drawHull)
    glUniform1i(lp, pass);
    if (pass) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.f, 4.f); }
    for (int k = 0; k < n; k++) {
      if (!hm[k]) continue;
      const float* t = fp.traffic[k].t;
      float rot[9] = {t[25 * 4], t[25 * 4 + 1], t[25 * 4 + 2], t[26 * 4], t[26 * 4 + 1], t[26 * 4 + 2], t[27 * 4], t[27 * 4 + 1], t[27 * 4 + 2]};
      glUniformMatrix3fv(glGetUniformLocation(progHull, "uRot"), 1, GL_FALSE, rot);
      glUniform3f(glGetUniformLocation(progHull, "uPos"), t[24 * 4], t[24 * 4 + 1], t[24 * 4 + 2]);
      glBindBuffer(GL_ARRAY_BUFFER, hm[k]->vbo);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
      glDrawArrays(GL_TRIANGLES, 0, hm[k]->verts);
    }
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  glUniform1i(glGetUniformLocation(progHull, "uChan"), 1);
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  trafHullOn = true;
}

uint64_t Renderer::hullKey(const FrameParams& fp, int slot) const {
  const PlaneVisual& pv = fp.plane;
  uint64_t h = 1469598103934665603ull ^ (uint64_t)slot;
  auto mix = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } };
  mix(pv.M, sizeof(float) * 96);
  // (not the light fixtures: their housings stand at most ~0.2 m proud of the airframe, inside the 0.25 m voxels'
  // margin, so one hull serves the menu's airframe without them and the flight's with them)
  return h;
}

// Light aircraft only (the research jets' shapes have more moving parts than these states cover), not wrecks, and
// the outside views only. In the cabin (HULLCOCKPIT turns it on) the image matches with the 1.2 m near segment, but
// nearly every cockpit ray ends within that segment anyway: it saved only 2-6% of the aircraft march for a 100-160k
// triangle hull per type, so the cabin stays on the plain march.
bool Renderer::hullWanted(const FrameParams& fp) const {
  const PlaneVisual& pv = fp.plane;
  if (pv.PS[3] > 0.5f) return false;   // (the cockpit is drawn from its mesh)
  return !hullOff && progHull && progHullBake && pv.on && fp.wreck.pieces == 0 && pv.M[2] < 4.5f;
}

void Renderer::ensureHullTarget() {
  if (!fboHull) glGenFramebuffers(1, &fboHull);
  if (!texHullDepth || hullDepthW < rw || hullDepthH < rh) {   // (only grows: the camera feeds draw smaller views)
    int w = std::max(rw, hullDepthW), h = std::max(rh, hullDepthH);
    if (texHullDepth) glDeleteTextures(1, &texHullDepth);
    glGenTextures(1, &texHullDepth); glBindTexture(GL_TEXTURE_2D, texHullDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    hullDepthW = w; hullDepthH = h;
  }
}

void Renderer::drawHull(const FrameParams& fp, int slot, uint64_t key, float nearOverride, bool exitToo) {
  hullOn = false; hullExitOn = false;
  hullNearNow = nearOverride >= 0.f ? nearOverride : hullNear(fp);
  auto it = hulls.find(key);
  if (it == hulls.end()) return;
  HullMesh& H = it->second;
  if (!H.ok || !H.verts || !fboEnv) return;
  // the projection below reaches kHullFar: an aircraft that may extend past it (a fly-by camera 400-700 m off) would be
  // clipped from its own hull and vanish, so then the hull is left off and the ray tracer marches it as usual
  static const float kHullFar = 400.f;
  if (length(fp.plane.pos - fp.camPos) + 45.f > kHullFar * 0.95f) return;
  ensureHullTarget();
  glBindFramebuffer(GL_FRAMEBUFFER, fboHull);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texEnv, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texHullDepth, 0);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
  float far[4] = {0, 1e30f, 0, 0};   // no hull on this pixel: no aircraft along its ray (fourth channel: no hull end, 0)
  glClearBufferfv(GL_COLOR, 0, far);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
  glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glUseProgram(progHull);
  mat4 vp = viewProj(fp, 0.01f, kHullFar);   // a near plane at 1 cm: in the cockpit the seats and controls are closer than 0.5 m
  glUniformMatrix4fv(glGetUniformLocation(progHull, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(glGetUniformLocation(progHull, "uJit"), jitX, jitY);
  glUniform3f(glGetUniformLocation(progHull, "uCam"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  glUniform1f(glGetUniformLocation(progHull, "uFree"), hullNearNow);
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
  if (exitToo) {   // the end of the hull volume on each ray: the farthest inside face, no depth test, MAX-blended into the fourth channel
    glDisable(GL_DEPTH_TEST); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glEnable(GL_BLEND); glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE);
    glUniform1i(lp, 2); glDrawArrays(GL_TRIANGLES, 0, H.verts);
    glDisable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glEnable(GL_DEPTH_TEST);
    hullExitOn = true;
  }
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

// The hull passes' target (aircraft_hull.cpp drawHull): per pixel the aircraft hull's start, the traffic hulls' start
// and the moving hull's exit, with its own depth; full view size (the render scale uses a corner)
void Renderer::createHullTarget() {
  auto mk = [&](GLuint& t, GLenum ifmt, GLenum fmt) {
    if (t) glDeleteTextures(1, &t);
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, W, H, 0, fmt, GL_FLOAT, nullptr);   // (full view size: the render scale uses a corner)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mk(texEnv, GL_RGBA32F, GL_RGBA);   // - | aircraft hull start | traffic hulls' start | the moving hull's exit
  mk(texEnvDepth, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT);
  if (!fboEnv) glGenFramebuffers(1, &fboEnv);
  glBindFramebuffer(GL_FRAMEBUFFER, fboEnv);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texEnv, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texEnvDepth, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
