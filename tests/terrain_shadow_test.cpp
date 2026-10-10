// The baked terrain sun shadow (kTShBakeMain) against a brute-force scan: runs the production bake shader on software
// GL (EGL / llvmpipe) at the shadow-map texel of every airport, for low suns from eight directions, and checks that it
// never leaves a texel in full sun where a 20 m scan out to 30 km finds terrain blocking the sun. (Codex's render
// review reproduced exactly that: the bake stopped at 8.2 km, so mountains 11-23 km away cast no shadow at sunset.)
// Linux only; it skips (passes) where no EGL is available.
#include "../src/gl.h"
#include "../src/shaders.h"
#include "../src/world.h"
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
static GLuint compile(GLenum type, const std::string& src) {
  GLuint sh = glCreateShader(type); const char* p = src.c_str(); glShaderSource(sh, 1, &p, nullptr); glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); printf("shader compile failed: %s\n", log); return 0; }
  return sh;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (!initGL()) { puts("terrain shadow bake: no EGL here, skipped"); return 0; }
  g_world.build();
  // the bake shader with what it uses from the shared shader code: the common terrain code, the roads graded into it
  // (with the scene uniforms they read: uData) and the max-height mip chain
  std::string fs = std::string("#version 330 core\n") + kCommonGLSL + kSceneUniforms + kRoads + "layout(location=0) out vec4 oColor;\n" +
                   "uniform sampler2D uHMax; const int HMAXN = " + std::to_string(HMAX_N) + "; const int HMAXL = " + std::to_string(HMAX_LEVELS) + ";\n" + kTShBakeMain;
  GLuint vs = compile(GL_VERTEX_SHADER, kFullscreenVS), f = compile(GL_FRAGMENT_SHADER, fs);
  if (!vs || !f) return 1;
  GLuint p = glCreateProgram(); glAttachShader(p, vs); glAttachShader(p, f); glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { puts("link failed"); return 1; }
  const int N = 2048;
  GLuint hm, hmax, target, fbo, vao;
  glGenTextures(1, &hm); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hm);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, HM_N, HM_N, 0, GL_RGBA, GL_FLOAT, g_world.hm.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &hmax); glActiveTexture(GL_TEXTURE0 + 6); glBindTexture(GL_TEXTURE_2D, hmax);
  for (int L = 0; L < HMAX_LEVELS; L++) glTexImage2D(GL_TEXTURE_2D, L, GL_R32F, HMAX_N >> L, HMAX_N >> L, 0, GL_RED, GL_FLOAT, g_world.hmax[L].data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, HMAX_LEVELS - 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // the road network, as the renderer uploads it (unit 4 the grid, unit 5 the entries)
  GLuint grid, data;
  glGenTextures(1, &grid); glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, grid);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, RoadGrid::N, RoadGrid::N, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, g_world.roadGrid.head.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  std::vector<float> rows(ROAD_DATA_W * 4, 0.f); { const std::vector<float> e = roadEntryRows(g_world.roadGrid); rows.insert(rows.end(), e.begin(), e.end()); }
  glGenTextures(1, &data); glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, data);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ROAD_DATA_W, (GLsizei)(rows.size() / 4 / ROAD_DATA_W), 0, GL_RGBA, GL_FLOAT, rows.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &target); glBindTexture(GL_TEXTURE_2D, target);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, nullptr);
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  GLenum b = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &b);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("fbo incomplete"); return 1; }
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hm);   // (creating the target took unit 0)
  glGenVertexArrays(1, &vao); glBindVertexArray(vao); glUseProgram(p);
  glUniform1i(glGetUniformLocation(p, "uHM"), 0); glUniform1i(glGetUniformLocation(p, "uHMax"), 6);
  glUniform1i(glGetUniformLocation(p, "uRoadGrid"), 4); glUniform1i(glGetUniformLocation(p, "uData"), 5);
  glUniform1i(glGetUniformLocation(p, "uCraterN"), 0); glUniform1f(glGetUniformLocation(p, "uBakeN"), (float)N);
  float maxH = 0; for (size_t i = 0; i < g_world.hm.size(); i += 4) maxH = std::max(maxH, g_world.hm[i] + g_world.hm[i + 1] * 1.5f);
  glUniform1f(glGetUniformLocation(p, "uMaxH"), maxH + 20.f);
  glViewport(0, 0, N, N); glEnable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  const float texel = 2.f * WORLD_HALF / N;
  int cases = 0, missed = 0, shaded = 0;
  for (const Airport& a : g_world.airports)
    for (float elev : {2.f, 4.f, 8.f, 15.f})
      for (int k = 0; k < 8; k++) {
        float az = k * 45.f;
        int px = (int)floorf((a.x + WORLD_HALF) / texel), py = (int)floorf((a.z + WORLD_HALF) / texel);
        float x = -WORLD_HALF + (px + 0.5f) * texel, z = -WORLD_HALF + (py + 0.5f) * texel;
        vec3 sun(sinf(az * DEG) * cosf(elev * DEG), sinf(elev * DEG), -cosf(az * DEG) * cosf(elev * DEG));
        glUniform3f(glGetUniformLocation(p, "uBakeSun"), sun.x, sun.y, sun.z);
        glScissor(px, py, 1, 1); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        float hd[2]; glReadPixels(px, py, 1, 1, GL_RG, GL_FLOAT, hd);
        // the reference: every 20 m out to 30 km (or the world's edge) along the sun's direction
        float ry = g_world.height(x, z, 4) + 1.f, ref = -6e4f, dist = 1.f, lxz = length(vec2(sun.x, sun.z));
        for (float t = 3.f * texel; t <= 30000.f; t += 20.f) {
          float qx = x + sun.x / lxz * t, qz = z + sun.z / lxz * t;
          if (fabsf(qx) > WORLD_HALF || fabsf(qz) > WORLD_HALF) break;
          float hh = g_world.height(qx, qz, 4) - t * sun.y / lxz;
          if (hh > ref) { ref = hh; dist = t; }
        }
        float vBake = clampf(12.f * lxz * (ry - hd[0]) / std::max(hd[1], 1.f), 0.f, 1.f);
        float vRef = clampf(12.f * lxz * (ry - ref) / std::max(dist, 1.f), 0.f, 1.f);
        cases++; shaded += vRef == 0.f;
        if (!std::isfinite(hd[0]) || !std::isfinite(hd[1])) { printf("non-finite bake at %s\n", a.code); return 1; }
        if (vBake > 0.1f && vRef == 0.f) {
          missed++;
          printf("  missed: %s sun %.0f deg at %.0f: bake horizon %.0f m (blocker %.0f m), reference %.0f m (blocker %.0f m)\n", a.code, elev, az, hd[0], hd[1], ref, dist);
        }
      }
  printf("terrain shadow bake: %d cases (%d in a mountain's shadow), %d left in the sun that shouldn't be: %s\n", cases, shaded, missed, missed ? "FAIL" : "ok");
  return missed ? 1 : 0;
}
