// Solace Express - terrain envelope mesh. A triangle mesh that lies on or just above the rendered terrain everywhere
// (World::buildEnvelope) is rasterized before the ray tracer: the distance to it on each pixel is where that pixel's
// exact terrain march can start, so the march no longer crosses the empty air in front of the ground step by step.
// The visible ground is still the exact heightfield with all its detail; the mesh only says where to look for it.
//
// Quadtree of 32 x 32-cell chunks: level 0 cells are the heightmap's (39 m), each level up doubles them. Chunks of
// different levels meet with T-junctions; vertical skirts hanging from every chunk edge close the slivers between
// them, so a ray can't slip under the mesh (each edge lies above the ground along it, so the wall between two edges
// does too). A pixel whose nearest hit is the underside of the mesh, or a frame where the camera itself is below it,
// falls back to the full march.
#include "renderer.h"
#include <functional>

namespace {
const char* kEnvVS = R"(
layout(location = 0) in vec4 aInst;   // chunk origin (level-0 vertex index x, z), level, -
uniform sampler2D uV0;                 // level-0 vertex heights ((HM_N+1)^2)
uniform sampler2D uM;                  // cell bounds, a mip level per quadtree level
uniform mat4 uVP; uniform vec2 uJit;
uniform vec3 uCraterB; uniform float uRim;
out vec3 vW; flat out int vSkirt;
const float WH = 40000.0, T = 39.0625; const int N = 2048, C = 32;
float vertH(ivec2 v, int L){
  if (L == 0) return texelFetch(uV0, clamp(v, ivec2(0), ivec2(N)), 0).r;
  int n = N >> L; float m = 0.0;
  for (int k = 0; k < 4; k++) {
    ivec2 c = clamp(v - ivec2(k & 1, k >> 1), ivec2(0), ivec2(n - 1));
    m = max(m, texelFetch(uM, c, L).r);
  }
  return m;
}
void main(){
  int L = int(aInst.z + 0.5), id = gl_VertexID;
  ivec2 g; bool bot = false; vSkirt = 0;
  if (id < C*C*6) {   // two triangles per cell, wound counter-clockwise seen from above
    int cell = id/6, k = id - cell*6;
    ivec2 o = k == 0 ? ivec2(0, 0) : (k == 1 || k == 4) ? ivec2(0, 1) : (k == 2 || k == 3) ? ivec2(1, 0) : ivec2(1, 1);
    g = ivec2(cell % C, cell / C) + o;
  } else {            // skirts: a quad below every edge segment
    int s = id - C*C*6, e = s/(C*6), r = s - e*C*6, seg = r/6, k = r - seg*6;
    int a = seg + ((k == 2 || k == 3 || k == 5) ? 1 : 0);
    bot = k == 1 || k == 4 || k == 5;
    g = e == 0 ? ivec2(a, 0) : e == 1 ? ivec2(C, a) : e == 2 ? ivec2(a, C) : ivec2(0, a);
    ivec2 o0 = ivec2(aInst.xy);
    bool border = (e == 0 && o0.y == 0) || (e == 2 && o0.y + (C << L) >= N) || (e == 3 && o0.x == 0) || (e == 1 && o0.x + (C << L) >= N);
    if (border) bot = false;   // nothing to close at the edge of the world
    vSkirt = 1;
  }
  ivec2 v = ivec2(aInst.xy) / (1 << L) + g;
  float cs = T*float(1 << L);
  vec2 xz = vec2(-WH + 0.5*T) + vec2(v)*cs;
  float y = vertH(v, L);
  if (uRim > 0.0 && length(xz - uCraterB.xy) < uCraterB.z + 2.0*cs) y += uRim;   // crater rims
  if (bot) y = -800.0;
  vW = vec3(xz.x, y, xz.y);
  gl_Position = uVP*vec4(vW, 1.0);
  gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the ray tracer's sub-pixel jitter
}
)";
const char* kEnvFS = R"(
in vec3 vW; flat in int vSkirt;
uniform vec3 uCam;
out float oT;
// distance along the pixel's ray; 0 where the nearest hit is the mesh's underside (that ray starts below it)
void main(){ oT = (gl_FrontFacing || vSkirt == 1) ? length(vW - uCam) : 0.0; }
)";
}

bool Renderer::compileEnvelope() {
  std::string hdr = "#version 330 core\n", e;
  progEnv = linkProgramCached(hdr + kEnvVS, hdr + kEnvFS, e);
  return progEnv != 0;
}

void Renderer::initEnvelope() {
  const int N = HM_N, NV = HM_N + 1;
  glGenTextures(1, &texEnvV0); glBindTexture(GL_TEXTURE_2D, texEnvV0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NV, NV, 0, GL_RED, GL_FLOAT, g_world.tpV0.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &texEnvM); glBindTexture(GL_TEXTURE_2D, texEnvM);
  for (int L = 0; L < TP_LEVELS; L++) glTexImage2D(GL_TEXTURE_2D, L, GL_R32F, N >> L, N >> L, 0, GL_RED, GL_FLOAT, g_world.tpM[L].data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, TP_LEVELS - 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenVertexArrays(1, &vaoEnv); glBindVertexArray(vaoEnv);
  glGenBuffers(1, &vboEnvInst); glBindBuffer(GL_ARRAY_BUFFER, vboEnvInst);
  glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float) * 4, nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0); glVertexAttribDivisor(0, 1);
  glBindVertexArray(0);
}

void Renderer::createEnvelopeTarget() {
  auto mk = [&](GLuint& t, GLenum ifmt, GLenum fmt) {
    if (t) glDeleteTextures(1, &t);
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, rw, rh, 0, fmt, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mk(texEnv, GL_RGBA32F, GL_RGBA);   // terrain start | aircraft hull start | traffic hulls' start (aircraft_hull.cpp) | -
  mk(texEnvDepth, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT);
  if (!fboEnv) glGenFramebuffers(1, &fboEnv);
  glBindFramebuffer(GL_FRAMEBUFFER, fboEnv);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texEnv, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texEnvDepth, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::drawEnvelope(const FrameParams& fp) {
  envOn = false;
  static const bool off = getenv("ENVOFF") != nullptr;   // (debug: every pixel marches from the camera)
  if (off || !progEnv || !vaoEnv || !fboEnv || g_world.tpV0.empty()) return;
  const int N = HM_N, NV = HM_N + 1;
  const float T = HM_TEXEL;
  vec3 cam = fp.camPos;
  // crater rims stand above the bounds (every crater's rim could add up at one point)
  float rim = 0.f;
  for (int i = 0; i < fp.wreck.craterN; i++) rim += 0.22f * fabsf(fp.wreck.crater[i][3]) + 0.3f;
  // the camera must be above the mesh, or rays starting under it could meet the ground before any mesh surface
  {
    float fx = (cam.x + WORLD_HALF) / T - 0.5f, fz = (cam.z + WORLD_HALF) / T - 0.5f;
    int i = std::clamp((int)floorf(fx), 0, N - 1), j = std::clamp((int)floorf(fz), 0, N - 1);
    const std::vector<float>& V = g_world.tpV0;
    float top = std::max(std::max(V[(size_t)j * NV + i], V[(size_t)j * NV + i + 1]), std::max(V[(size_t)(j + 1) * NV + i], V[(size_t)(j + 1) * NV + i + 1]));
    if (cam.y < top + rim + 0.3f) return;
  }
  // quadtree: split a chunk while the camera is within kSplit x its size of it
  const float kSplit = 3.f;
  envInst.clear();
  float rootTop = 0.f; for (float m : g_world.tpM[TP_LEVELS - 1]) rootTop = std::max(rootTop, m);
  // view frustum (left, right, bottom, top, near) from the jittered camera's matrix: a chunk wholly outside it can't
  // be the first surface on any pixel's ray, so it and everything under it is skipped
  mat4 vpc = viewProj(fp);
  float pl[5][4];
  for (int k = 0; k < 5; k++) {
    int r = k / 2; float sg = (k & 1) ? -1.f : 1.f;
    // (the side planes widened by 1% of the view, far more than the sub-pixel jitter)
    for (int c = 0; c < 4; c++) pl[k][c] = (k == 4 ? vpc(3, c) + vpc(2, c) : 1.01f * vpc(3, c) + sg * vpc(r, c));
  }
  auto inView = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
    for (int k = 0; k < 5; k++) {   // the box corner furthest along each plane's normal must be inside it
      float x = pl[k][0] >= 0 ? x1 : x0, y = pl[k][1] >= 0 ? y1 : y0, z = pl[k][2] >= 0 ? z1 : z0;
      if (pl[k][0] * x + pl[k][1] * y + pl[k][2] * z + pl[k][3] < 0.f) return false;
    }
    return true;
  };
  int culled = 0;
  std::function<void(int, int, int)> visit = [&](int L, int ox, int oz) {
    int span = TP_CHUNK << L; float S = span * T;
    float x0 = -WORLD_HALF + 0.5f * T + ox * T, z0 = -WORLD_HALF + 0.5f * T + oz * T;
    float dx = std::max(0.f, std::max(x0 - cam.x, cam.x - (x0 + S))), dz = std::max(0.f, std::max(z0 - cam.z, cam.z - (z0 + S)));
    int Lm = L + 5;   // the level whose single cell is this chunk
    float top = Lm < TP_LEVELS ? g_world.tpM[Lm][(size_t)(oz >> Lm) * (N >> Lm) + (ox >> Lm)] : rootTop;
    float dy = std::max(0.f, cam.y - top - rim);
    if (!inView(x0, x0 + S, -800.f, top + rim + 1.f, z0, z0 + S)) { culled++; return; }
    if (L > 0 && dx * dx + dz * dz + dy * dy < kSplit * kSplit * S * S) {
      int h = span / 2;
      for (int k = 0; k < 4; k++) visit(L - 1, ox + (k & 1) * h, oz + (k >> 1) * h);
    } else envInst.insert(envInst.end(), {(float)ox, (float)oz, (float)L, 0.f});
  };
  visit(TP_LEVELS - 1, 0, 0);
  int n = (int)envInst.size() / 4;
  glBindFramebuffer(GL_FRAMEBUFFER, fboEnv);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  float far[4] = {1e30f, 0, 0, 0};   // no mesh on this pixel: no terrain along its ray anywhere in the world
  glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);   // (the second channel is the aircraft hull's)
  glClearBufferfv(GL_COLOR, 0, far);
  glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
  glUseProgram(progEnv);
  mat4 vp = viewProj(fp);
  glUniformMatrix4fv(glGetUniformLocation(progEnv, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(glGetUniformLocation(progEnv, "uJit"), jitX, jitY);
  glUniform3f(glGetUniformLocation(progEnv, "uCam"), cam.x, cam.y, cam.z);
  glUniform1f(glGetUniformLocation(progEnv, "uRim"), rim);
  {
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (int i = 0; i < fp.wreck.craterN; i++) { const float* c = fp.wreck.crater[i]; float r = c[2] * 2.7f; x0 = std::min(x0, c[0] - r); x1 = std::max(x1, c[0] + r); z0 = std::min(z0, c[1] - r); z1 = std::max(z1, c[1] + r); }
    glUniform3f(glGetUniformLocation(progEnv, "uCraterB"), (x0 + x1) * 0.5f, (z0 + z1) * 0.5f, fp.wreck.craterN ? 0.5f * sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0)) : 0.f);
  }
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texEnvV0); glUniform1i(glGetUniformLocation(progEnv, "uV0"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texEnvM); glUniform1i(glGetUniformLocation(progEnv, "uM"), 1);
  glBindVertexArray(vaoEnv); glBindBuffer(GL_ARRAY_BUFFER, vboEnvInst);
  glBufferData(GL_ARRAY_BUFFER, envInst.size() * sizeof(float), envInst.data(), GL_STREAM_DRAW);
  const int perChunk = TP_CHUNK * TP_CHUNK * 6 + 4 * TP_CHUNK * 6;
  glDrawArraysInstanced(GL_TRIANGLES, 0, perChunk, n);
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  envOn = true; envChunks = n;
  static const bool dbg = getenv("ENVDBG") != nullptr;
  if (dbg) {   // how the mesh covers the screen: pixels with a start distance / underside / nothing
    std::vector<float> px((size_t)rw * rh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboEnv); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, rw, rh, GL_RED, GL_FLOAT, px.data());   // (the first channel)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    int hit = 0, under = 0, none = 0; double sum = 0;
    for (float v : px) { if (v <= 0.f) under++; else if (v > 1e29f) none++; else { hit++; sum += v; } }
    printf("envelope: %d chunks (%d culled), %d tris; pixels: %d start (mean %.0f m), %d underside, %d none\n", n, culled, n * perChunk / 3, hit, hit ? sum / hit : 0.0, under, none);
  }
}
