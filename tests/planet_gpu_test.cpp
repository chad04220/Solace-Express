// The round world (planet.h, planet.glsl kPlanet): the flat world laid over a sphere about the camera's nadir, in the
// C++ and in the shaders, kept in step by hand - this holds them there. Run on software GL (EGL / llvmpipe):
// - the shaders' planetPos against the C++'s, everywhere from under the camera to past its horizon;
// - a place keeps its distance along the ground from the nadir and its height over the sea;
// - planetFlat undoes planetPos (the lighting pass's lookups: millimetres near, the floats' spacing far);
// - planetTurn takes the level's up to the sphere's own up there;
// - planetAlt is the height over the sphere along a straight ray;
// - planetHorizon's point is where a sight line from the camera grazes the sea.
// Linux only; it skips (passes) where no EGL is available.
#include "../src/gl.h"
#include "../src/planet.h"
#include "../src/shaders.h"
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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

static int fails = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { fails++; printf("FAIL %s\n", what); } }

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (!initGL()) { puts("planet on the GPU: no EGL here, skipped"); return 0; }
  // the places: rings round the nadir from a metre to a quarter of the way round the small planet, at heights from the
  // sea floor to the mountains' tops (one texel each: x, y, z and how far along the ray to it planetAlt is asked at)
  struct Sample { float x, y, z, s; };
  std::vector<Sample> base;
  for (float d : {1.f, 30.f, 400.f, 3000.f, 20000.f, 90000.f, 300000.f, 900000.f})
    for (int a = 0; a < 7; a++)
      for (float y : {-60.f, 0.f, 3.f, 450.f, 2400.f}) {
        const float ang = a * 0.9f + 0.3f;
        base.push_back({d * cosf(ang), y, d * sinf(ang), 0.f});
      }
  const int W = (int)base.size();
  std::string fs = std::string("#version 330 core\n#define ROUND_WORLD\n") + kPlanet +
                   "uniform sampler2D uSamples; uniform vec3 uCam; uniform int uMode; layout(location=0) out vec4 oColor;\n"
                   "void main(){ vec4 q = texelFetch(uSamples, ivec2(gl_FragCoord.xy), 0); vec3 w = q.xyz + vec3(uCam.x, 0.0, uCam.z);\n"
                   "  if (uMode == 0) oColor = vec4(planetPos(w, uCam), 0.0);\n"
                   "  else if (uMode == 1) oColor = vec4(planetFlat(planetPos(w, uCam), uCam), 0.0);\n"
                   "  else if (uMode == 2) oColor = vec4(planetTurn(vec3(0.0, 1.0, 0.0), w, uCam, 1.0), 0.0);\n"
                   "  else { vec3 c = planetPos(w, uCam); float t = length(c - uCam); oColor = vec4(planetAlt(uCam, (c - uCam)/t, t), 0.0, 0.0, 0.0); }\n"
                   "}\n";
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
  texture(6, GL_RGBA32F, W, 1, GL_RGBA, GL_FLOAT, base.data());
  GLuint target = texture(0, GL_RGBA32F, W, 1, GL_RGBA, GL_FLOAT, nullptr), fbo, vao;
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
  GLenum b0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &b0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("fbo incomplete"); return 1; }
  glGenVertexArrays(1, &vao); glBindVertexArray(vao); glUseProgram(prog);
  glUniform1i(glGetUniformLocation(prog, "uSamples"), 6);
  glViewport(0, 0, W, 1); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
  auto run = [&](const vec3& cam, float R, int mode) {
    glUniform3f(glGetUniformLocation(prog, "uCam"), cam.x, cam.y, cam.z);
    glUniform1f(glGetUniformLocation(prog, "uPlanetR"), R);
    glUniform1i(glGetUniformLocation(prog, "uMode"), mode);
    glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    std::vector<float> out((size_t)W * 4);
    glReadPixels(0, 0, W, 1, GL_RGBA, GL_FLOAT, out.data());
    return out;
  };
  double worstPos = 0, worstFlatNear = 0, worstFlatFar = 0, worstUp = 0, worstAlt = 0, worstKeep = 0;
  for (float R : {6371000.f, 636620.f})
    for (vec3 cam : {vec3(0.f, 500.f, 0.f), vec3(12345.f, 30000.f, -40000.f), vec3(-49000.f, 400000.f, 49000.f)}) {
      const std::vector<float> pos = run(cam, R, 0), flat = run(cam, R, 1), up = run(cam, R, 2), alt = run(cam, R, 3);
      for (int i = 0; i < W; i++) {
        const vec3 w(base[i].x + cam.x, base[i].y, base[i].z + cam.z);
        const double d = hypot((double)base[i].x, (double)base[i].z);
        if (d / R > 1.4) continue;   // (past a quarter of the way round: never drawn)
        // (the floats' own spacing at these coordinates: what any of it can be held to)
        const double M = fabs((double)cam.x) + fabs((double)cam.z) + fabs((double)cam.y) + d, ulp = 1.2e-7 * M;
        // the C++ and the shader agree
        const vec3 c = planetPos(w, cam, R), g(pos[i * 4], pos[i * 4 + 1], pos[i * 4 + 2]);
        worstPos = std::max(worstPos, length(g - c) / (0.005 + 3.0 * ulp));
        // the place keeps its distance along the ground and its height over the sea (in double, from the C++'s)
        const double qx = (double)c.x - cam.x, qz = (double)c.z - cam.z, qy = (double)c.y + R, hq = hypot(qx, qz);
        const double keep = std::max(fabs(atan2(hq, qy) * R - d), fabs(sqrt(hq * hq + qy * qy) - R - w.y));
        worstKeep = std::max(worstKeep, keep / (0.002 + 2.0 * ulp));
        // planetFlat undoes it
        const double back = length(vec3(flat[i * 4], flat[i * 4 + 1], flat[i * 4 + 2]) - w);
        if (d < 50000.0) worstFlatNear = std::max(worstFlatNear, back); else worstFlatFar = std::max(worstFlatFar, back / d);   // (far: a GPU's atan)
        // the level's up turned is the sphere's up there
        const vec3 n = normalize(vec3((float)qx, (float)qy, (float)qz)), u(up[i * 4], up[i * 4 + 1], up[i * 4 + 2]);
        worstUp = std::max(worstUp, (double)length(u - n));
        // planetAlt along the ray to the place: its height
        worstAlt = std::max(worstAlt, fabs((double)alt[i * 4] - w.y) / (0.01 + 2.0 * ulp));
      }
    }
  printf("planetPos: the shader against the C++: worst %.2f of the allowance (5 mm and three of the floats' spacing)\n", worstPos);
  check(worstPos <= 1.0, "the shader's planetPos and the C++'s agree");
  printf("distance along the ground and height kept: worst %.2f of the allowance\n", worstKeep);
  check(worstKeep <= 1.0, "a place keeps its distance from the nadir and its height");
  printf("planetFlat(planetPos): worst %.4f m within 50 km, %.2e of the distance beyond\n", worstFlatNear, worstFlatFar);
  check(worstFlatNear <= 0.01 && worstFlatFar <= 2e-5, "planetFlat undoes planetPos");
  printf("planetTurn of the level's up: worst %.2e off the sphere's\n", worstUp);
  check(worstUp <= 1e-4, "planetTurn takes up to the sphere's up");
  printf("planetAlt along the ray: worst %.2f of the allowance\n", worstAlt);
  check(worstAlt <= 1.0, "planetAlt is the height over the sphere");
  // the horizon: from h up, the sea at planetHorizon(h) round the curve is where a sight line grazes it
  for (float R : {6371000.f, 636620.f})
    for (float h : {2.f, 1000.f, 30000.f, 400000.f}) {
      const vec3 cam(0.f, h, 0.f), c = planetPos(vec3(planetHorizon(h, R), 0.f, 0.f), cam, R);
      const vec3 sight = normalize(c - cam), up = normalize(c - vec3(0.f, -R, 0.f));
      check(fabsf(dot(sight, up)) < 2e-3f, "the horizon is where the sight line grazes the sea");
    }
  check(planetHorizon(1000.f, 0.f) >= 1e9f && planetPos(vec3(5000.f, 10.f, 3.f), vec3(0.f), 0.f).y == 10.f, "radius 0: the flat world");
  printf("planet: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
