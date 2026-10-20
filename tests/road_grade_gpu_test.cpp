// The ground with the roads built into it, as the shaders build it (roads.glsl roadGrade, under every terrain vertex)
// against the C++ (road_network.cpp roadGrade, under the flight model and the scenery): the same to a centimetre on
// the platforms, banks and bridge ends of the whole network, run on software GL (EGL / llvmpipe). The two are kept in
// step by hand; this is what holds them there. And the road the ground paints (roads.glsl roadQuery), which reads only
// the entries paving its texel, against every entry of it read here: the same road, across it to a centimetre, the
// same junctions. Linux only; it skips (passes) where no EGL is available.
#include "../src/gl.h"
#include "../src/shaders.h"
#include "../src/world.h"
#include "test_world.h"
#include <dlfcn.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>

static void* lib;
static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib, n); }
template <class F> static F sym(const char* n) { return reinterpret_cast<F>(dlsym(lib, n)); }
static bool initGL() {
  lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL); if (!lib) return false;
  eglProc = sym<void* (*)(const char*)>("eglGetProcAddress"); if (!eglProc) return false;
  auto gd = reinterpret_cast<void* (*)(unsigned, void*, const int*)>(eglProc("eglGetPlatformDisplayEXT")); if (!gd) return false;
  void* d = gd(0x31DD, nullptr, nullptr); int ma, mi;   // EGL_PLATFORM_SURFACELESS_MESA
  if (!sym<unsigned (*)(void*, int*, int*)>("eglInitialize")(d, &ma, &mi)) return false;
  sym<unsigned (*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[] = {0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038}; void* c; int n;
  if (!sym<unsigned (*)(void*, const int*, void**, int, int*)>("eglChooseConfig")(d, ca, &c, 1, &n) || !n) return false;
  const int sa[] = {0x3057, 16, 0x3056, 16, 0x3038}, at[] = {0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038};
  void* s = sym<void* (*)(void*, void*, const int*)>("eglCreatePbufferSurface")(d, c, sa);
  void* x = sym<void* (*)(void*, void*, void*, const int*)>("eglCreateContext")(d, c, nullptr, at);
  const char* m = nullptr;
  return x && sym<unsigned (*)(void*, void*, void*, void*)>("eglMakeCurrent")(d, s, s, x) && glLoad(proc, &m);
}
// roads.glsl roadQuery, from the texel's whole list: whether on a road's paving or verge, the nearest's across and
// class, a second road's within a metre (junction); the graded roads' nearest platform edge (edge)
struct RoadRef { bool on; float across, best, other, edge, second, side; int cls; };   // (second: the next nearest segment's, any road's; side: off its line, past an end)
static RoadRef roadRef(const RoadGrid& g, float x, float z) {
  RoadRef r{false, 1e6f, 1e9f, 1e9f, 1e9f, 1e9f, 1e9f, 1};
  const uint32_t h = g.head[(size_t)RoadGrid::cell(z) * RoadGrid::N + RoadGrid::cell(x)];
  int path = -1;
  for (uint32_t k = RoadGrid::first(h), e = k + RoadGrid::count(h); k < e; k++) {
    const RoadSegment& s = g.segs[g.list[k]];
    if (s.flags & RS_BRIDGE) continue;
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L = std::max(hypotf(dx, dz), 1e-3f);
    const float tr = ((x - s.ax) * dx + (z - s.az) * dz) / (L * L), t = std::clamp(tr, 0.f, 1.f);
    if (((s.flags & RS_ENDA) && tr < 0.f) || ((s.flags & RS_ENDB) && tr > 1.f)) continue;
    const float qx = x - (s.ax + dx * t), qz = z - (s.az + dz * t), d = hypotf(qx, qz), ed = d - roadSpec(s.cls).halfPlatform;
    if (!(s.flags & RS_NOGRADE)) r.edge = std::min(r.edge, ed);
    if (ed < r.best) {
      r.second = r.best;
      if (s.path != path) { r.other = r.best; path = s.path; }
      r.best = ed; r.cls = s.cls; r.side = (dx * qz - dz * qx) / L; r.across = d * (r.side < 0.f ? -1.f : 1.f);
    } else {
      r.second = std::min(r.second, ed);
      if (s.path != path) r.other = std::min(r.other, ed);
    }
  }
  r.on = r.best < 0.f;
  return r;
}

static GLuint compile(GLenum type, const std::string& src) {
  GLuint sh = glCreateShader(type); const char* p = src.c_str(); glShaderSource(sh, 1, &p, nullptr); glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); printf("shader compile failed: %s\n", log); return 0; }
  return sh;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (!initGL()) { puts("road grade on the GPU: no EGL here, skipped"); return 0; }
  buildTestWorld();
  const RoadGrid& grid = g_world.roadGrid;
  // the places: across every few segments' platforms and out over their banks, before, along and past their ends; and
  // past every bridge's joints, under the deck
  struct Sample { float x, z, g, pad; };
  std::vector<Sample> s;
  for (size_t i = 0; i < grid.segs.size(); i += 3) {
    const RoadSegment& r = grid.segs[i];
    const float dx = r.bx - r.ax, dz = r.bz - r.az, L = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / L, nz = dx / L, P = roadSpec(r.cls).halfPlatform;
    for (float t : {-0.3f, 0.f, 0.4f, 1.f, 1.2f})
      for (float o : {0.f, 0.6f * P, -(P + 0.5f), P + 3.f, -(P + 12.f), P + 30.f}) {
        const float x = r.ax + dx * t + nx * o, z = r.az + dz * t + nz * o;
        s.push_back({x, z, g_world.naturalHeight(x, z), 0.f});
      }
  }
  for (const Bridge& b : g_world.bridges)
    for (int end = 0; end < 2; end++) {
      const RoadPoint& p = end ? b.deck.back() : b.deck.front();
      const float ux = end ? -b.outX : b.inX, uz = end ? -b.outZ : b.inZ;   // (into the bridge)
      for (float e : {-0.5f, 0.5f, 1.5f, 3.f, 6.f, 10.f}) for (float o : {0.f, 0.7f * b.halfDeck}) {
        const float x = p.x + ux * e - uz * o, z = p.z + uz * e + ux * o;
        s.push_back({x, z, g_world.naturalHeight(x, z), 0.f});
      }
    }
  const int W = 1024, H = (int)((s.size() + W - 1) / W);
  s.resize((size_t)W * H, Sample{0.f, 0.f, 0.f, 0.f});
  std::string fs = std::string("#version 330 core\n#define GROUND_ROADS\n") + kCommonGLSL + kSceneUniforms + kRoads +
                   "uniform sampler2D uSamples; layout(location=0) out vec4 oColor;\n"
                   "void main(){ vec4 q = texelFetch(uSamples, ivec2(gl_FragCoord.xy), 0); RoadHit r = roadQuery(q.xy);\n"
                   "  oColor = vec4(roadGrade(q.xy, q.z), r.on ? r.across : 1e6, min(r.edge, 5.0), float(r.cls) + (r.junction ? 8.0 : 0.0)); }\n";
  GLuint vs = compile(GL_VERTEX_SHADER, kFullscreenVS), f = compile(GL_FRAGMENT_SHADER, fs);
  if (!vs || !f) return 1;
  GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, f); glLinkProgram(prog);
  GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok); if (!ok) { puts("link failed"); return 1; }
  auto texture = [](int unit, GLenum internal, int w, int h, GLenum format, GLenum type, const void* data) {
    GLuint t; glGenTextures(1, &t); glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
  };
  // the road network, as the renderer uploads it (unit 4 the grid, unit 5 the entries); the samples on unit 6
  texture(4, GL_R32UI, RoadGrid::N, RoadGrid::N, GL_RED_INTEGER, GL_UNSIGNED_INT, grid.head.data());
  std::vector<float> rows(ROAD_DATA_W * 4, 0.f); { int segRow = 0; const std::vector<float> e = roadEntryRows(grid, &segRow); rows[12] = (float)segRow; rows.insert(rows.end(), e.begin(), e.end()); }
  texture(5, GL_RGBA32F, ROAD_DATA_W, (int)(rows.size() / 4 / ROAD_DATA_W), GL_RGBA, GL_FLOAT, rows.data());
  texture(6, GL_RGBA32F, W, H, GL_RGBA, GL_FLOAT, s.data());
  GLuint target = texture(0, GL_RGBA32F, W, H, GL_RGBA, GL_FLOAT, nullptr), fbo, vao;
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  GLenum b0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &b0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("fbo incomplete"); return 1; }
  glGenVertexArrays(1, &vao); glBindVertexArray(vao); glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "uRoadGrid"), 4); glUniform1i(glGetUniformLocation(prog, "uData"), 5);
  glUniform1i(glGetUniformLocation(prog, "uSamples"), 6);
  glViewport(0, 0, W, H); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
  std::vector<float> gpu((size_t)W * H * 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, gpu.data());
  int bad = 0, graded = 0; float worst = 0;
  int roadBad = 0, on = 0, junctions = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i].x == 0.f && s[i].z == 0.f) continue;
    const float* o = &gpu[i * 4];
    const float cpu = roadGrade(grid, s[i].x, s[i].z, s[i].g), d = fabsf(o[0] - cpu);
    graded += fabsf(cpu - s[i].g) > 0.01f;
    worst = std::max(worst, d);
    if (!(d <= 0.01f)) { if (bad++ < 12) printf("  at %.1f %.1f (ground %.2f): the C++ %.3f, the shader %.3f\n", s[i].x, s[i].z, s[i].g, cpu, o[0]); }
    // the painted road: alike wherever the answer isn't on a knife-edge (a platform's edge, a junction's metre, two
    // segments as near, or on a dead end's line past its end - which side of it the rounding decides)
    const RoadRef r = roadRef(grid, s[i].x, s[i].z);
    const bool gOn = o[1] < 1e5f, gJunction = o[3] >= 7.5f;
    const int gCls = (int)lroundf(o[3]) & 7;
    const bool sure = fabsf(r.best) > 0.01f, sureJ = sure && fabsf(r.other - 1.f) > 0.01f;
    const bool edgeOk = std::min(r.edge, 5.f) > 0.5f && o[2] > 0.5f ? true : fabsf(std::min(r.edge, 5.f) - o[2]) <= 0.01f;
    const bool tie = r.second - r.best <= 0.01f;
    const bool same = (!sure || gOn == r.on) && (!(gOn && r.on && sure && !tie) || (fabsf((fabsf(r.side) > 0.01f ? o[1] : fabsf(o[1])) - (fabsf(r.side) > 0.01f ? r.across : fabsf(r.across))) <= 0.01f && gCls == r.cls)) &&
                      (!sureJ || gJunction == (r.on && r.other < 1.f)) && edgeOk;
    on += r.on; junctions += r.on && r.other < 1.f;
    if (!same && roadBad++ < 12)
      printf("  road at %.1f %.1f: all its texel's entries %s across %.3f class %d junction %d edge %.3f; the shader %s across %.3f class %d junction %d edge %.3f\n",
             s[i].x, s[i].z, r.on ? "on" : "off", r.across, r.cls, (int)(r.on && r.other < 1.f), r.edge, gOn ? "on" : "off", o[1], gCls, (int)gJunction, o[2]);
  }
  printf("road grade on the GPU: %zu places (%d graded), worst difference %.4f m, %d over a centimetre: %s\n", s.size(), graded, worst, bad, bad ? "FAIL" : "ok");
  printf("the painted road on the GPU (the texel's paving entries alone): %d places on a road, %d at junctions, %d unlike every entry's answer: %s\n",
         on, junctions, roadBad, roadBad ? "FAIL" : "ok");
  return bad || roadBad || graded < 1000 || on < 1000 ? 1 : 0;
}
