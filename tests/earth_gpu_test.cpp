// A real-Earth chart's ground as the shaders build it (common.glsl groundH: its base, the detail noise in the anchor's
// frame (uNoiseFrame) at the Earth's small share of it (uDetailScale), the roads graded in, the lakes' levels) against
// the C++ (World::height, under the flight model and the scenery): the test fixture's Martha's Vineyard chart, over the
// whole square, on its lakes and along its roads, the same to a couple of centimetres. On software GL (EGL / llvmpipe),
// as road_grade_gpu_test; it skips (passes) where no EGL is available.
#include "../src/gl.h"
#include "../src/shaders.h"
#include "../src/world.h"
#include "../src/earth_charts.h"
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

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (!initGL()) { puts("the real Earth's ground on the GPU: no EGL here, skipped"); return 0; }
  std::string why;
  auto pack = earth::Pack::open(argc > 1 ? argv[1] : "tests/fixtures/earth2", &why);
  if (!pack) { printf("no pack: %s\n", why.c_str()); return 1; }
  earthCharts::usePack(pack);
  const auto chart = earthCharts::chartForAirport("KMVY");
  if (!chart) return 1;
  World w; w.buildProcedural(*chart, "", "", false);
  // the places: all over the square; across the lakes' shores; along the roads, on their platforms and banks
  struct Sample { float x, z, pad0, pad1; };
  std::vector<Sample> s;
  uint32_t seed = 99;
  auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; };
  for (int k = 0; k < 12000; k++) s.push_back({-39000.f + 78000.f * rnd(), -39000.f + 78000.f * rnd(), 0, 0});
  int lakePlaces = 0;
  for (int j = 0; j < HM_N && lakePlaces < 4000; j += 3) for (int i = 0; i < HM_N && lakePlaces < 4000; i += 3) {
    if (w.lakes.empty() || !w.lakes[(size_t)j * HM_N + i]) continue;
    const float x = -WORLD_HALF + (i + rnd()) * HM_TEXEL, z = -WORLD_HALF + (j + rnd()) * HM_TEXEL;
    s.push_back({x, z, 0, 0}); lakePlaces++;
  }
  for (size_t i = 0; i < w.roadGrid.segs.size(); i += 97) {
    const RoadSegment& r = w.roadGrid.segs[i];
    const float dx = r.bx - r.ax, dz = r.bz - r.az, L = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / L, nz = dx / L;
    for (float o : {0.f, 3.f, -9.f, 20.f}) s.push_back({r.ax + dx * 0.5f + nx * o, r.az + dz * 0.5f + nz * o, 0, 0});
  }
  const int W = 1024, H = (int)((s.size() + W - 1) / W);
  s.resize((size_t)W * H, Sample{0.f, 0.f, 0.f, 0.f});
  std::string fs = std::string("#version 330 core\n#define GROUND_ROADS\n") + kCommonGLSL + kSceneUniforms + kRoads +
                   "uniform sampler2D uSamples; layout(location=0) out vec4 oColor;\n"
                   "void main(){ vec4 q = texelFetch(uSamples, ivec2(gl_FragCoord.xy), 0); oColor = vec4(groundH(q.xy, 8), float(earthLakeCode(q.xy)), 0.0, 0.0); }\n";
  GLuint vs = compile(GL_VERTEX_SHADER, kFullscreenVS), f = compile(GL_FRAGMENT_SHADER, fs);
  if (!vs || !f) return 1;
  GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, f); glLinkProgram(prog);
  GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok); if (!ok) { puts("link failed"); return 1; }
  auto texture = [](int unit, GLenum internal, int tw, int th, GLenum format, GLenum type, const void* data) {
    GLuint t; glGenTextures(1, &t); glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, tw, th, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
  };
  // as the renderer uploads them: the heightfield; the road grid with the forest and the lakes' codes beside it
  // (Renderer::roadGridTexels); the road entries and segments (Renderer's uData, row 0 texel 3 the segments' row)
  texture(3, GL_RGBA32F, HM_N, HM_N, GL_RGBA, GL_FLOAT, w.hm.data());
  std::vector<uint32_t> grid((size_t)RoadGrid::N * RoadGrid::N * 2, 0);
  for (size_t i = 0; i < (size_t)RoadGrid::N * RoadGrid::N; i++) {
    grid[2 * i] = i < w.roadGrid.head.size() ? w.roadGrid.head[i] : 0u;
    grid[2 * i + 1] = (i < w.roadGrid.forest.size() ? w.roadGrid.forest[i] : 0u) | (i < w.lakes.size() ? (uint32_t)w.lakes[i] << 16 : 0u);
  }
  texture(4, GL_RG32UI, RoadGrid::N, RoadGrid::N, GL_RG_INTEGER, GL_UNSIGNED_INT, grid.data());
  std::vector<float> rows(ROAD_DATA_W * 4, 0.f);
  { int segRow = 0; const std::vector<float> e = roadEntryRows(w.roadGrid, &segRow); rows[12] = (float)segRow; rows.insert(rows.end(), e.begin(), e.end()); }
  texture(5, GL_RGBA32F, ROAD_DATA_W, (int)(rows.size() / 4 / ROAD_DATA_W), GL_RGBA, GL_FLOAT, rows.data());
  texture(6, GL_RGBA32F, W, H, GL_RGBA, GL_FLOAT, s.data());
  GLuint target = texture(0, GL_RGBA32F, W, H, GL_RGBA, GL_FLOAT, nullptr), fbo, vao;
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  GLenum b0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &b0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("fbo incomplete"); return 1; }
  glGenVertexArrays(1, &vao); glBindVertexArray(vao); glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "uHM"), 3); glUniform1i(glGetUniformLocation(prog, "uRoadGrid"), 4);
  glUniform1i(glGetUniformLocation(prog, "uData"), 5); glUniform1i(glGetUniformLocation(prog, "uSamples"), 6);
  glUniform1i(glGetUniformLocation(prog, "uUnwrappedWorld"), 1);
  glUniform1f(glGetUniformLocation(prog, "uDetailScale"), w.detailScale);
  glUniform4f(glGetUniformLocation(prog, "uNoiseFrame"), w.noiseCos, w.noiseSin, w.noiseX, w.noiseZ);
  glViewport(0, 0, W, H); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
  std::vector<float> gpu((size_t)W * H * 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, gpu.data());
  int bad = 0, lakeBad = 0, onLakes = 0; float worst = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i].x == 0.f && s[i].z == 0.f) continue;
    const float* o = &gpu[i * 4];
    const float cpu = w.height(s[i].x, s[i].z, 8), d = fabsf(o[0] - cpu);
    worst = std::max(worst, d);
    if (!(d <= 0.02f) && bad++ < 12) printf("  at %.1f %.1f: the C++ %.3f, the shader %.3f\n", s[i].x, s[i].z, cpu, o[0]);
    const uint16_t code = w.lakeAt(s[i].x, s[i].z);
    onLakes += code != 0;
    if ((uint32_t)lroundf(o[1]) != code && lakeBad++ < 6) printf("  lake code at %.1f %.1f: the C++ %u, the shader %.0f\n", s[i].x, s[i].z, code, o[1]);
  }
  printf("the real Earth's ground on the GPU: %zu places (%d on lakes), worst difference %.4f m, %d over 2 cm, %d lake codes unlike: %s\n",
         s.size(), onLakes, worst, bad, lakeBad, bad || lakeBad ? "FAIL" : "ok");
  return bad || lakeBad || onLakes < 500 ? 1 : 0;
}
