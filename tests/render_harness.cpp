#include <chrono>
// Development harness: renders test frames on Linux (Xvfb + Mesa) to validate shaders and visuals.
#include <X11/Xlib.h>
#include <dlfcn.h>
#include "../src/renderer.h"
#include "../src/shaders.h"
#include "../src/shaders_wraith_cockpit.h"
#include "../src/font_data.h"
#include "../src/world.h"
#include "../src/aircraft.h"
#include "../src/game.h"

typedef struct __GLXFBConfigRec* GLXFBConfig;
typedef struct __GLXcontextRec* GLXContext;
typedef void* (*PFNGETPROC)(const unsigned char*);
static void* s_lib;
static PFNGETPROC s_getProc;
static void* getProc(const char* n) { void* p = s_getProc((const unsigned char*)n); if (!p) p = dlsym(s_lib, n); return p; }

struct GameTest {
  static void drawUI(Game& g, const std::string& what) {
    g.realTime = 5; g.uiDt = 1.f;
    if (what == "controls") { g.hubTab = TAB_SETTINGS; g.settingsPage = 1; if (getenv("CAPTURE")) { g.bindCapture = ACT_GEAR; g.bindCaptureDev = 0; g.bindCaptureT = 3; } g.drawHub(); }
    else if (what == "settings") { g.hubTab = TAB_SETTINGS; g.settingsPage = 0; g.drawHub(); }
    else if (what == "pause") { g.drawPause(); }
    else if (what == "menu") { g.drawMenu(); }
  }
};

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
  if (argc > 1 && std::string(argv[1]) == "gauges") {   // display atlas: every gauge and MFD page, without the ray tracer
    std::string fs = std::string("#version 330 core\n") + kCommonGLSL + kRaytraceFS + kRaytraceFS2 + kRaytraceUfo + kRaytraceText + kRaytraceDisplays + [] { std::string f3 = kRaytraceFS3; size_t m = f3.find("void main("); if (m != std::string::npos) f3.replace(m, 10, "void mainRT("); return f3; }() + kRaytraceWraith + kRaytraceWraithCockpit + R"(

uniform int uMode;
void main(){
  vec2 px = gl_FragCoord.xy;
  vec3 c = vec3(0.03);
  if (uMode == 0) {   // top: steam panel; bottom: glass panel (panel metres)
    bool top = px.y > uRes.y*0.5;
    float mpp = 0.6/uRes.x;
    vec2 q = vec2(px.x - uRes.x*0.5, px.y - (top ? 0.75 : 0.25)*uRes.y)*mpp + vec2(0.12, 0.0);
    gAA = mpp*0.55;
    vec3 acc = vec3(0.0); float cov = 0.0;
    for (int si = 0; si < 4; si++) {
      vec2 o = (vec2(si & 1, si >> 1) - 0.5)*mpp*0.7;
      vec3 c4 = drawInstruments(q + o, top ? 0 : 2, true);
      if (c4.x >= 0.0) { acc += c4; cov += 1.0; }
    }
    if (cov > 0.0) c = mix(c, acc/cov, cov*0.25);
  } else {            // 4 x 2 grid of MFD pages
    vec2 cell = vec2(uRes.x/4.0, uRes.y/2.0);
    vec2 id = floor(px/cell), f = (px - id*cell)/cell.y*2.0 - vec2(cell.x/cell.y, 1.0);
    int page = int(id.x) + (1 - int(id.y))*4;
    vec2 uv = f/0.92;
    if (page < 7 && abs(uv.x) < 1.0 && abs(uv.y) < 1.0) {
      float fp = 2.0/cell.y/0.92;
      gAA = fp*0.55;
      c = 0.25*(mfdPage(page, uv + vec2(-0.25, -0.75)*fp) + mfdPage(page, uv + vec2(0.75, -0.25)*fp) + mfdPage(page, uv + vec2(0.25, 0.75)*fp) + mfdPage(page, uv + vec2(-0.75, 0.25)*fp));
      c += vec3(0.01, 0.03, 0.04);
    }
  }
  oColor = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2)), 1.0);
}
)";
    std::string err;
    GLuint prog = linkProgramCached(kFullscreenVS, fs, err);
    if (!prog) { printf("atlas shader: %s\n", err.substr(0, 3000).c_str()); return 1; }
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    GLuint ft; glGenTextures(1, &ft); glBindTexture(GL_TEXTURE_2D, ft); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_W, FONT_H, 0, GL_RED, GL_UNSIGNED_BYTE, FONT_PIX);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uFontTex"), 0);
    glUniform2f(glGetUniformLocation(prog, "uRes"), (float)W, (float)H);
    glUniform4f(glGetUniformLocation(prog, "uI0"), 112.f, 3450.f, 247.f, 650.f);
    glUniform4f(glGetUniformLocation(prog, "uI1"), 4.f, 14.f, 0.82f, 0.62f);
    glUniform4f(glGetUniformLocation(prog, "uI2"), 2.4f, 3.f, 0, 0);
    glUniform4f(glGetUniformLocation(prog, "uHud"), 240.f, 1200.f, 247.f, 0.77f);
    glUniform4f(glGetUniformLocation(prog, "uHud2"), 2.4f, 0.9f, 0.45f, 1.f);
    glUniform4f(glGetUniformLocation(prog, "uHud3"), 0.93f, 6.f, 0, 0);
    float c = cosf(0.25f), sn = sinf(0.25f), cp = cosf(0.08f), sp = sinf(0.08f);
    float rot[9] = {c, sn, 0, -sn * cp, c * cp, sp, sn * sp, -c * sp, cp};   // a little bank and pitch
    glUniformMatrix3fv(glGetUniformLocation(prog, "uPlaneRot"), 1, GL_FALSE, rot);
    glUniform3f(glGetUniformLocation(prog, "uPlanePos"), -4000.f, 1200.f, 9000.f);
    glUniform1f(glGetUniformLocation(prog, "uTime"), 3.f);
    GLuint hm; glGenTextures(1, &hm); glBindTexture(GL_TEXTURE_2D, hm);   // baseAt reads the heightmap
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, HM_N, HM_N, 0, GL_RGBA, GL_FLOAT, g_world.hm.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, hm); glUniform1i(glGetUniformLocation(prog, "uHM"), 1);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ft);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H);
    for (int mode = 0; mode < 2; mode++) {
      glUniform1i(glGetUniformLocation(prog, "uMode"), mode);
      glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
      g_ren.W = W; g_ren.H = H;
      std::string out = std::string("/tmp/claude-0/sp/shot_gauges") + (mode ? "_mfd" : "_panel") + ".ppm";
      g_ren.screenshot(out.c_str()); printf("wrote %s\n", out.c_str());
    }
    return 0;
  }
  {   // UI-only captures (no scene shaders): "intro_<progress>_<t>", "ui_controls", "ui_settings", "ui_pause"
    std::string sc = argc > 1 ? argv[1] : "";
    if (sc.rfind("intro", 0) == 0 || sc.rfind("ui_", 0) == 0) {
      if (!g_ren.initUI(W, H)) { printf("ui init failed: %s\n", g_ren.error.c_str()); return 1; }
      static Game game; game.initHeadless();
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H); glClearColor(0.02f, 0.04f, 0.07f, 1); glClear(GL_COLOR_BUFFER_BIT);
      g_ren.uiBegin();
      if (sc.rfind("intro", 0) == 0) {
        float pr = 0.4f, t = 5.f; sscanf(sc.c_str(), "intro_%f_%f", &pr, &t);
        GLuint icon = 0;
        if (FILE* f = fopen("/tmp/claude-0/sp/lt/icon.rgba", "rb")) { std::vector<uint8_t> px(256 * 256 * 4); if (fread(px.data(), 1, px.size(), f) == px.size()) icon = g_ren.makeTexture(px.data(), 256, 256); fclose(f); }
        game.shaderFirstRun = true;
        game.drawIntro(pr, "COMPILING SHADERS  4 / 10   //   GENERATING THE SOLACE ISLANDS", t, icon, 1.f);
      } else {
        if (FILE* f = fopen("/tmp/claude-0/sp/lt/icon.rgba", "rb")) { std::vector<uint8_t> px(256 * 256 * 4); if (fread(px.data(), 1, px.size(), f) == px.size()) game.iconTex = g_ren.makeTexture(px.data(), 256, 256); fclose(f); }
        GameTest::drawUI(game, sc.substr(3));
      }
      g_ren.uiEnd(); glFinish();
      std::string out = "/tmp/claude-0/sp/shot_" + sc + ".ppm";
      g_ren.screenshot(out.c_str()); printf("wrote %s\n", out.c_str());
      return 0;
    }
  }
  if (getenv("SHADERCACHE")) g_shaderCacheDir = getenv("SHADERCACHE");   // test the program-binary cache
  g_ren.renderScale = getenv("RSCALE") ? (float)atof(getenv("RSCALE")) : 1.0f; g_ren.quality = 1;
  if (!g_ren.init(W, H)) { printf("init failed: %s\n", g_ren.error.c_str()); return 1; }
  g_ren.entSync = !getenv("ENTSTREAM");   // captures generate every scenery chunk in range up front
  printf("renderer ok (shader cache: %d loaded, %d compiled)\n", g_shaderCacheHits.load(), g_shaderCacheMisses.load());
  std::string scene = argc > 1 ? argv[1] : "default";
  if (scene.rfind("multi:", 0) == 0) {   // several scenes from one shader compile: multi:a,b,c
    std::string list = scene.substr(6) + ",";
    for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
      std::string sc = list.substr(a, b - a);
      if (sc.empty()) continue;
      Game* g = new Game();
      g->initHeadless(); g->debugScene(sc);
      for (int i = 0; i < 3; i++) { g->update(1.f / 30.f); g->render(); }
      for (int i = 0; i < 6; i++) g->render();
      glFinish();
      if (getenv("TIMEIT")) {   // average frame time over a few frames (GPU finished each time)
        g->render(); glFinish();
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 4; i++) { g->update(1.f / 60.f); g->render(); }
        glFinish();
        printf("time %s: %.0f ms/frame\n", sc.c_str(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 4);
        fflush(stdout);
      }
      if (getenv("COSTMAP") && g_ren.buildCostProgram()) {   // the analysis build's per-pixel work counters
        g_ren.costMap = true; g->render(); glFinish(); g_ren.costMap = false;
        std::vector<float> cm; int w = 0, h = 0; g_ren.readCostMap(cm, w, h);
        double m[4] = {}; float mx[4] = {};
        for (size_t i = 0; i < (size_t)w * h; i++) for (int c = 0; c < 4; c++) { m[c] += cm[i * 4 + c]; mx[c] = std::max(mx[c], cm[i * 4 + c]); }
        printf("cost %s %dx%d: terrain %.1f (max %.0f)  aircraft %.1f (max %.0f)  clouds %.1f (max %.0f)  fx %.1f (max %.0f)\n", sc.c_str(), w, h,
               m[0] / (w * h), mx[0], m[1] / (w * h), mx[1], m[2] / (w * h), mx[2], m[3] / (w * h), mx[3]);
        g->render(); glFinish();
      }
      std::string out = "/tmp/claude-0/sp/shot_" + sc + ".ppm";
      g_ren.screenshot(out.c_str()); printf("wrote %s\n", out.c_str()); fflush(stdout);
      delete g;
    }
    return 0;
  }
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
