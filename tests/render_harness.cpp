#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
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
    else if (what == "debrief") {   // a story delivery's settlement with eleven lines and a new licence (the review of v3.24.0, R9)
      g.career.storyIndex = 3; g.contract = g_story[3]; g.lastSuccess = true; g.stars = 2; g.debriefTitle = "Delivered";
      g.result.landed = true; g.touchdownFpm = 240; g.flightClock = 1312; g.licenseBefore = LIC_STUDENT; g.career.license = LIC_PPL;
      const char* lines[] = {"Contract payment", "Reputation bonus", "On time", "Passenger comfort", "Rental fee", "Positioning", "Fuel uplift", "Landing fee", "Loan payment", "Airline: CAP - NPT route", "Pilot wages"};
      int amt[] = {24000, 2400, 1200, -900, -1800, -350, -720, -150, -2100, 8232, -1900};
      for (int i = 0; i < 11; i++) g.payout.push_back({lines[i], amt[i]});
      g.screen = SCR_DEBRIEF; g.drawDebrief();
    }
    else if (what.rfind("contracts_", 0) == 0) {   // a story job's briefing at ATP with one owned Starling at its airport (the review of v3.24.0, R1)
      const std::string id = what.substr(10);
      for (int i = 0; i < (int)g_story.size(); i++) if (g_story[i].id == id) g.career.storyIndex = i;
      const Contract* st = g.career.nextStory();
      g.career.license = LIC_ATP; g.career.money = 1000000; g.career.location = st ? st->from : 0;
      g.career.fleet.push_back({6, g.career.location, 1000, 1});
      g.hubTab = TAB_CONTRACTS; g.selContract = 0; g.selAircraft = -1;
      for (int f = 0; f < 3; f++) {   // (the frame after the automatic choice adds its cost and fuel rows)
        if (f) { g_ren.uiEnd(); glClear(GL_COLOR_BUFFER_BIT); g_ren.uiBegin(); }
        g.drawHub();
        printf("contracts %s frame %d: selected aircraft %d\n", id.c_str(), f, g.selAircraft);
      }
    }
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
  // the islands and the aircraft performance from the cache beside the shaders (SHADERCACHE), stamped with this binary
  // (any rebuild makes them again): a render needn't generate the world each time
  std::string cacheStamp;
  { struct stat st; char exe[1024] = {}; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0 && stat(exe, &st) == 0) cacheStamp = std::to_string((long long)st.st_size) + "-" + std::to_string((long long)st.st_mtime); }
  const std::string cacheDir = getenv("SHADERCACHE") ? getenv("SHADERCACHE") : "";
  g_world.build(cacheDir.empty() ? std::string() : cacheDir + "/world.bin", cacheStamp);
  const bool perfCached = !cacheDir.empty() && Plane::perfLoad(cacheDir + "/perf.bin", cacheStamp);
  buildStory();
  if (argc > 1 && std::string(argv[1]) == "gauges") {   // display atlas: every gauge and MFD page, without the scene
    std::string fs = worldLibAssembly("") + R"(

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
  if (getenv("DBGOFF")) g_ren.dbgOff = atoi(getenv("DBGOFF"));   // switch renderer features off (Renderer::dbgOff bits)
  auto tInit = std::chrono::steady_clock::now();
  if (!g_ren.init(W, H)) { printf("init failed: %s\n", g_ren.error.c_str()); return 1; }
  g_ren.entSync = !getenv("ENTSTREAM");   // captures generate every scenery chunk in range up front
  printf("renderer ok (shader cache: %d loaded, %d compiled, %.0f s)\n", g_shaderCacheHits.load(), g_shaderCacheMisses.load(),
         std::chrono::duration<double>(std::chrono::steady_clock::now() - tInit).count());
  std::string scene = argc > 1 ? argv[1] : "default";
  // ---- a render server: everything loaded once (the context, the shaders, the islands, the textures, the aircraft
  // meshes, and the scenery as it is generated), then one shot per request. Requests are lines on a named pipe
  // (SERVE_REQ, default /tmp/claude-0/sp/rs.req): "<scene> [frames=N] [taam=N] [bench=N] [dbgoff=N] [out=path]";
  // each reply ("ok <path> <seconds>") goes to SERVE_REP (/tmp/claude-0/sp/rs.rep). tools/render_client.sh sends one.
  if (scene == "serve") {
    if (!perfCached && !cacheDir.empty()) { for (int i = 0; i < kNumAircraft; i++) Plane::perf(&kAircraft[i]); Plane::perfSave(cacheDir + "/perf.bin", cacheStamp); }
    const std::string req = getenv("SERVE_REQ") ? getenv("SERVE_REQ") : "/tmp/claude-0/sp/rs.req";
    const std::string rep = getenv("SERVE_REP") ? getenv("SERVE_REP") : "/tmp/claude-0/sp/rs.rep";
    mkfifo(req.c_str(), 0600); mkfifo(rep.c_str(), 0600);
    if (getenv("PREWARM")) { Game* pw = new Game(); pw->initHeadless(); pw->debugScene("menu"); pw->prewarm([](float, const std::string&) {}, true); delete pw; }
    printf("serving %dx%d: requests on %s\n", W, H, req.c_str()); fflush(stdout);
    for (;;) {
      FILE* in = fopen(req.c_str(), "r"); if (!in) return 1;
      char line[2048];
      std::vector<std::string> reqs;
      while (fgets(line, sizeof line, in)) reqs.push_back(line);
      fclose(in);
      for (std::string r : reqs) {
        while (!r.empty() && (r.back() == '\n' || r.back() == '\r' || r.back() == ' ')) r.pop_back();
        if (r.empty()) continue;
        if (r == "quit") { FILE* o = fopen(rep.c_str(), "w"); if (o) { fprintf(o, "bye\n"); fclose(o); } return 0; }
        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> tok; for (size_t a = 0, b2; a < r.size(); a = b2 + 1) { b2 = r.find(' ', a); if (b2 == std::string::npos) b2 = r.size(); if (b2 > a) tok.push_back(r.substr(a, b2 - a)); }
        std::string sc = tok[0], out = "/tmp/claude-0/sp/shot_" + sc + ".ppm";
        int frames = 0, taam = 0, bench = 0;
        g_ren.dbgOff = 0;
        for (size_t k = 1; k < tok.size(); k++) {
          size_t e = tok[k].find('='); if (e == std::string::npos) continue;
          std::string key = tok[k].substr(0, e), v = tok[k].substr(e + 1);
          if (key == "frames") frames = atoi(v.c_str()); else if (key == "taam") taam = atoi(v.c_str()); else if (key == "bench") bench = atoi(v.c_str());
          else if (key == "dbgoff") g_ren.dbgOff = atoi(v.c_str()); else if (key == "out") out = v;
        }
        g_ren.resetTemporal();
        Game* g = new Game();
        g->initHeadless(); g->debugScene(sc);
        for (int i = 0; i < 3; i++) { g->update(1.f / 30.f); g->render(); }
        for (int i = 0; i < taam; i++) { g->update(1.f / 60.f); g->render(); }
        for (int i = 0; i < frames; i++) g->render();
        glFinish();
        std::string extra;
        if (bench > 0) {
          double wsum[Renderer::kPasses] = {};
          auto b0 = std::chrono::steady_clock::now();
          for (int i = 0; i < bench; i++) { g_ren.syncTiming = true; g->render(); glFinish(); for (int p = 0; p < Renderer::kPasses; p++) wsum[p] += g_ren.passWall[p]; }
          g_ren.syncTiming = false;
          char b[512]; const char* nm[] = {"world", "displays", "feeds", "objects", "proxy", "lighting", "taa", "sprites", "bloom", "shafts", "composite"};
          int n = snprintf(b, sizeof b, " frame %.0f ms:", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count() / bench);
          for (int p = 0; p < Renderer::kPasses && n < (int)sizeof b - 32; p++) n += snprintf(b + n, sizeof b - n, " %s %.0f", nm[p], wsum[p] / bench);
          extra = b;
        }
        g_ren.screenshot(out.c_str());
        delete g;
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("%s -> %s (%.1f s)%s\n", r.c_str(), out.c_str(), secs, extra.c_str()); fflush(stdout);
        FILE* o = fopen(rep.c_str(), "w"); if (o) { fprintf(o, "ok %s %.1f%s\n", out.c_str(), secs, extra.c_str()); fclose(o); }
      }
    }
  }
  if (scene.rfind("multi:", 0) == 0) {   // several scenes from one shader compile: multi:a,b,c
    if (getenv("PREWARM")) { Game* pw = new Game(); pw->initHeadless(); pw->debugScene("menu"); pw->prewarm([](float, const std::string&) {}); delete pw; }   // (every body built once, for all the shots)
    std::string list = scene.substr(6) + ",";
    for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
      std::string sc = list.substr(a, b - a);
      if (sc.empty()) continue;
      if (getenv("ENTSYNC")) g_ren.entSync = true;   // deterministic scenery (for exact image comparisons)
      // scene~hulloff / scene~envoff: that shot only with the switch on, so an A/B pair shares one shader compile
      std::string base = sc.substr(0, sc.find('~'));
      const bool hullOff0 = g_ren.hullOff;
      if (sc.find("~hulloff") != std::string::npos) g_ren.hullOff = true;
      g_ren.resetTemporal();   // each shot starts from scratch: identical to rendering it alone
      Game* g = new Game();
      g->initHeadless(); g->debugScene(base);
      for (int i = 0; i < 3; i++) {
        static bool first = true;   // llvmpipe compiles each program at its first draw: where the first frame's time goes
        g_ren.syncTiming = first;
        auto t0 = std::chrono::steady_clock::now();
        g->update(1.f / 30.f); g->render();
        if (first) {
          glFinish(); g_ren.syncTiming = false; first = false;
          printf("first frame %.0f s: passes", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
          for (int p = 0; p < Renderer::kPasses; p++) printf(" %.0f", g_ren.passWall[p] / 1000);
          printf("\n");
          fflush(stdout);
        }
      }
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
      std::string out = "/tmp/claude-0/sp/shot_" + sc + ".ppm";
      g_ren.screenshot(out.c_str()); printf("wrote %s\n", out.c_str()); fflush(stdout);
      g_ren.hullOff = hullOff0;
      delete g;
    }
    return 0;
  }
  Game game;
  game.initHeadless();
  if (scene == "prewarm") {   // the launch prewarm, then the menu's first frames as a player would see them open
    game.debugScene("menu");
    auto t0 = std::chrono::steady_clock::now();
    game.prewarm([&](float f, const std::string& what) {
      printf("prewarm %3.0f%%  %s  (%.1f s)\n", f * 100.f, what.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); fflush(stdout);
    });
    printf("prewarm: entities pending %d, hull ready %d\n", g_ren.entPending, 1);
    for (int i = 0; i < 70; i++) { game.update(1.f / 60.f); game.render(); }   // into the first shot, past its fade-in
    printf("menu: entities pending %d\n", g_ren.entPending);
    g_ren.screenshot("/tmp/claude-0/sp/shot_prewarm.ppm"); printf("wrote shot_prewarm\n");
    return 0;
  }
  if (getenv("PREWARM")) game.prewarm([](float, const std::string&) {});   // (every aircraft's body built first, as the game's launch does: the traffic drawn from meshes)
  game.debugScene(scene);
  for (int i = 0; i < 3; i++) { game.update(1.f / 30.f); game.render(); }
  if (getenv("TAAM")) for (int i = 0, n = atoi(getenv("TAAM")); i < n; i++) { game.update(1.f / 60.f); game.render(); }   // moving frames
  if (getenv("TAAF")) for (int i = 0, n = atoi(getenv("TAAF")); i < n; i++) game.render();   // extra static frames: let TAA converge
  glFinish();
  if (getenv("BENCH")) {
    for (float& m : g_ren.passMs) m = 0.f;   // (the smoothed pass times start clean: the warm-up frames' bakes stay out of them)
    auto t0 = std::chrono::steady_clock::now();
    int frames = atoi(getenv("BENCH"));
    const bool wall = getenv("BENCHWALL") != nullptr;   // (each pass waited for and timed on the wall clock: exact on any driver)
    double wsum[Renderer::kPasses] = {};
    for (int i = 0; i < frames; i++) {
      g_ren.syncTiming = wall;
      game.render();
      if (wall) { glFinish(); for (int p = 0; p < Renderer::kPasses; p++) wsum[p] += g_ren.passWall[p]; }
    }
    g_ren.syncTiming = false;
    glFinish();
    if (wall) { printf("passes (wall ms):"); const char* nm[] = {"world", "displays", "feeds", "objects", "proxy", "lighting", "taa", "sprites", "bloom", "shafts", "composite"}; for (int p = 0; p < Renderer::kPasses; p++) printf(" %s %.0f", nm[p], wsum[p] / frames); printf("\n"); }
    printf("bench: %.1f ms/frame  (scenery: %d instances, %d chunks, %.2f ms CPU)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames, g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs);
    { const float* pm = g_ren.passMs; printf("passes (GPU ms, smoothed): world %.1f  displays %.1f  feeds %.1f  objects %.1f  shadow proxy %.1f  lighting %.1f  taa %.1f  sprites %.1f  bloom %.1f  shafts %.1f  composite %.1f\n", pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]); }
  }
  std::string out = "/tmp/claude-0/sp/shot_" + scene + ".ppm";
  g_ren.screenshot(out.c_str());
  printf("wrote %s\n", out.c_str());
  return 0;
}
