#include <chrono>
// Development harness: renders test frames on Linux (Xvfb + Mesa) to validate shaders and visuals.
#include <X11/Xlib.h>
#include <dlfcn.h>
#include "../src/renderer.h"
#include "../src/aircraft.h"
#include "../src/game.h"

typedef struct __GLXFBConfigRec* GLXFBConfig;
typedef struct __GLXcontextRec* GLXContext;
typedef void* (*PFNGETPROC)(const unsigned char*);
static void* s_lib;
static PFNGETPROC s_getProc;
static void* getProc(const char* n) { void* p = s_getProc((const unsigned char*)n); if (!p) p = dlsym(s_lib, n); return p; }

int main(int argc, char** argv) {
  int W = argc > 2 ? atoi(argv[2]) : 960, H = argc > 3 ? atoi(argv[3]) : 540;
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) { printf("no display\n"); return 1; }
  s_lib = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
  s_getProc = (PFNGETPROC)dlsym(s_lib, "glXGetProcAddressARB");
  auto chooseFB = (GLXFBConfig * (*)(Display*, int, const int*, int*)) dlsym(s_lib, "glXChooseFBConfig");
  auto createCtx = (GLXContext(*)(Display*, GLXFBConfig, GLXContext, int, const int*))s_getProc((const unsigned char*)"glXCreateContextAttribsARB");
  auto makeCur = (int (*)(Display*, unsigned long, unsigned long, GLXContext, int))0;
  auto makeCurrent = (int (*)(Display*, unsigned long, GLXContext))dlsym(s_lib, "glXMakeCurrent");
  auto createPbuf = (unsigned long (*)(Display*, GLXFBConfig, const int*))dlsym(s_lib, "glXCreatePbuffer");
  (void)makeCur;
  int fbAttr[] = {0x8010 /*GLX_DRAWABLE_TYPE*/, 0x4 /*PBUFFER*/, 0x8011 /*RENDER_TYPE*/, 1, 8, 8, 9, 8, 10, 8, 0};
  int n = 0; GLXFBConfig* cfg = chooseFB(dpy, DefaultScreen(dpy), fbAttr, &n);
  if (!cfg || !n) { printf("no fbconfig\n"); return 1; }
  int ctxAttr[] = {0x2091, 3, 0x2092, 3, 0x9126, 1 /*core*/, 0};
  GLXContext ctx = createCtx(dpy, cfg[0], 0, 1, ctxAttr);
  int pbAttr[] = {0x8041, W, 0x8040, H, 0};
  unsigned long pb = createPbuf(dpy, cfg[0], pbAttr);
  makeCurrent(dpy, pb, ctx);
  const char* missing = nullptr;
  if (!glLoad(getProc, &missing)) { printf("missing %s\n", missing); return 1; }
  printf("GL: %s\n", glGetString(GL_RENDERER));
  g_world.build();
  buildStory();
  g_ren.renderScale = getenv("RSCALE") ? (float)atof(getenv("RSCALE")) : 1.0f; g_ren.quality = 1;
  if (!g_ren.init(W, H)) { printf("init failed: %s\n", g_ren.error.c_str()); return 1; }
  g_ren.entSync = !getenv("ENTSTREAM");   // captures generate every scenery chunk in range up front
  printf("renderer ok\n");
  std::string scene = argc > 1 ? argv[1] : "default";
  Game game;
  game.initHeadless();
  game.debugScene(scene);
  for (int i = 0; i < 3; i++) { game.update(1.f / 30.f); game.render(); }
  if (getenv("TAAM")) for (int i = 0, n = atoi(getenv("TAAM")); i < n; i++) { game.update(1.f / 60.f); game.render(); }   // moving frames
  if (getenv("TAAF")) for (int i = 0, n = atoi(getenv("TAAF")); i < n; i++) game.render();   // extra static frames: let TAA converge
  glFinish();
  if (getenv("BENCH")) {
    auto t0 = std::chrono::steady_clock::now();
    int frames = atoi(getenv("BENCH"));
    for (int i = 0; i < frames; i++) game.render();
    glFinish();
    printf("bench: %.1f ms/frame  (scenery: %d instances, %d chunks, %.2f ms CPU)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames, g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs);
  }
  std::string out = "/tmp/claude-0/sp/shot_" + scene + ".ppm";
  g_ren.screenshot(out.c_str());
  printf("wrote %s\n", out.c_str());
  return 0;
}
