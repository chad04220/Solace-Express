#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
// Development harness: renders test frames on Linux (Xvfb + Mesa) to validate shaders and visuals.
#include <X11/Xlib.h>
#undef None      // (Xlib's macros are values' names in the game's headers - hive_combat.h's None and
#undef Success   // Success - and unused here)
#include <dlfcn.h>
#include "../src/renderer.h"
#include "../src/shaders.h"
#include "../src/shaders_somnus_cockpit.h"
#include "../src/font_data.h"
#include "../src/world.h"
#include "../src/aircraft.h"
#include "../src/game.h"
#include "../src/earth_charts.h"

typedef struct __GLXFBConfigRec* GLXFBConfig;
typedef struct __GLXcontextRec* GLXContext;
typedef void* (*PFNGETPROC)(const unsigned char*);
static void* s_lib;
static PFNGETPROC s_getProc;
static void* getProc(const char* n) { void* p = s_getProc((const unsigned char*)n); if (!p) p = dlsym(s_lib, n); return p; }

struct GameTest {
  static void clean(Game& g) { g.hudOn = false; g.toasts.clear(); g.hint.clear(); }   // (a picture without the HUD)
  static int researchWarm(Game& game) {   // the research terminal opened from the menu after the launch's prewarm: its warm-up, its bar and the boot sequence, frame by frame on the real clock
    game.debugScene("menu");
    if (getenv("PREWARM")) game.prewarm([](float, const std::string&) {}, true);   // (as the launch does; without it the warm-up bakes the research craft itself)
    g_ren.entSync = false;   // (the scenery streams over frames, as in the game)
    for (int i = 0; i < 3; i++) { game.update(1.f / 30.f); game.render(); }
    game.in.down['U'] = game.in.down['I'] = true; game.in.pressed['U'] = true;
    const float shots[] = {1.0f, 2.2f, 3.3f, 3.85f};
    int shot = 0;
    auto t0 = std::chrono::steady_clock::now(), last = t0;
    for (int f = 0; f < 6000 && (game.resWarm || game.resSeq < 5.5f); f++) {
      auto now = std::chrono::steady_clock::now();
      const float dt = std::min(0.25f, std::max(1.f / 240.f, std::chrono::duration<float>(now - last).count())); last = now;
      game.update(dt); game.render(); game.in.endFrame(); game.in.down['U'] = game.in.down['I'] = false;
      const float bar = std::min(1.f, std::max(0.f, (game.resSeq - Game::kResScan0) / (Game::kResScan1 - Game::kResScan0)));
      printf("frame %4d  %6.2f s  views %2d  pending %4d of %4d  work %5.1f%%  seq %5.2f  bar %5.1f%%%s\n", f, std::chrono::duration<float>(now - t0).count(), game.resWarmFrames,
             g_ren.entPending, game.resPend0, game.resWork * 100.f, game.resSeq, bar * 100.f, game.resWarm ? "" : "  (ready)");
      if (shot < 4 && game.resSeq >= shots[shot]) { char b[128]; snprintf(b, sizeof b, "/tmp/claude-0/sp/reswarm_%d.ppm", shot++); glFinish(); g_ren.screenshot(b); }
    }
    return 0;
  }
  // the XR-40 in flight: outside, then the cockpit, then bombs away, then outside and back in - each frame's update and
  // render timed, and the slow ones named by what they did (programs compiled, bodies baked or loaded, scenery gathered)
  static int somnusHitch(Game& game) {
    for (int i : kCareerAircraft) Plane::perf(&kAircraft[i]);   // (as Game::init learns them: the traffic reads them)
    game.headless = false; g_ren.entSync = false;   // (the launch as the game runs it: its loading screen, and its warm-ups)
    game.resCraft = getenv("HITCHCRAFT") ? atoi(getenv("HITCHCRAFT")) : kSomnus; game.resCard = getenv("HITCHCARD") ? atoi(getenv("HITCHCARD")) : -1;   // (HITCHCRAFT: another research craft) game.resAirborne = true; game.resAirport = 0; game.realTime = 20;
    game.launchResearch();
    int lf = 0; const int b0 = g_ren.bakeCount, m0 = g_shaderCacheMisses;
    for (; lf < 4000 && (game.screen == SCR_LOADING || lf < 3); lf++) { game.update(1.f / 30.f); game.render(); game.framePresented(); }
    printf("loading: %d frames, bodies %d, programs compiled %d, mounts %d\n", lf, g_ren.bakeCount - b0, (int)g_shaderCacheMisses - m0, g_feedMounts[2].ok ? 1 : 0);
    for (int i = 0; i < 20; i++) { game.update(1.f / 30.f); game.render(); game.framePresented(); }
    game.camMode = 0; game.toasts.clear();
    g_ren.profileRegionRender = getenv("HITCHPASSES") != nullptr;
    std::vector<double> all;
    auto frame = [&](int i, const char* what) {
      const int miss0 = g_shaderCacheMisses, bake0 = g_ren.bakeCount; const long long us0 = g_shaderCompileUs;
      const int gen0 = g_scenery.syncBuilt; const double genMs0 = g_scenery.syncBuildMs;
      auto t0 = std::chrono::steady_clock::now();
      game.update(1.f / 30.f);
      auto t1 = std::chrono::steady_clock::now();
      game.render(); glFinish();
      auto t2 = std::chrono::steady_clock::now();
      const double u = std::chrono::duration<double, std::milli>(t1 - t0).count(), r = std::chrono::duration<double, std::milli>(t2 - t1).count();
      all.push_back(u + r);
      printf("frame %3d %-14s update %7.1f ms  render %8.1f ms  programs compiled %d (%.0f ms)  bodies %d  feeds %d  bombs %d  scenery built now %d (%.0f ms)\n",
             i, what, u, r, (int)g_shaderCacheMisses - miss0, (g_shaderCompileUs - us0) / 1000.0, g_ren.bakeCount - bake0,
             g_feedMounts[2].ok ? 1 : 0, (int)game.somnus.bombs.size(), g_scenery.syncBuilt - gen0, g_scenery.syncBuildMs - genMs0);
      if (const char* sh = getenv("HITCHSHOTS")) {   // (HITCHSHOTS=<name>: the view switches' settled frames as pictures)
        if (i == 2 || i == 5) { char b[160]; snprintf(b, sizeof b, "/tmp/claude-0/sp/shot_%s_%s.ppm", sh, i == 2 ? "out" : "in"); g_ren.screenshot(b); printf("wrote %s\n", b); }
      }
      if (g_ren.profileRegionRender) {   // (HITCHPASSES: the render's passes, each timed to its end)
        const auto& a = g_ren.regionAuxRenderMs;
        printf("          world %.0f  ground shadow %.0f  lighting %.0f  aircraft shadow %.0f  traffic shadow %.0f  stores %.0f  displays %.0f  feeds %.0f  objects %.0f  shadow proxy %.0f  post %.0f ms\n",
               g_ren.regionTerrainRenderMs, g_ren.regionShadowRenderMs, g_ren.regionLightingRenderMs, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
      }
      fflush(stdout);
    };
    int i = 0;
    if (getenv("HITCHVIEW")) {   // (the view switches alone: out, in, out, in)
      for (int v = 0; v < 4; v++) { game.camMode = v & 1; for (int j = 0; j < 3; j++, i++) frame(i, j ? (v & 1 ? "cockpit" : "outside") : (v & 1 ? "-> cockpit" : "-> outside")); }
      return 0;
    }
    if (getenv("HITCHBOMB")) {   // (outside only: the drop and the impact)
      game.somnus.armed = true; game.somnus.bombQueue = 1;
      for (; i < 140; i++) { frame(i, game.somnus.bombs.empty() ? "outside" : "bomb falling"); if (i > 20 && game.somnus.bombs.empty() && !game.somnus.blasts.empty() && i > 60) break; }
      return 0;
    }
    for (; i < 8; i++) frame(i, "outside");
    game.camMode = 1;
    for (; i < 20; i++) frame(i, i == 8 ? "-> cockpit" : "cockpit");
    game.somnus.armed = true; game.somnus.bombQueue = 1;
    for (; i < 70; i++) frame(i, i == 20 ? "bomb queued" : "cockpit, bomb");
    game.camMode = 0;
    for (; i < 78; i++) frame(i, i == 70 ? "-> outside" : "outside");
    game.camMode = 1;
    for (; i < 90; i++) frame(i, i == 78 ? "-> cockpit" : "cockpit");
    game.somnus.bombQueue = 1;
    for (; i < 130; i++) frame(i, i == 90 ? "2nd bomb" : "cockpit, bomb");
    std::vector<double> s = all; std::sort(s.begin(), s.end());
    printf("median frame %.1f ms, worst %.1f ms\n", s[s.size() / 2], s.back());
    return 0;
  }
  static size_t particleCount(const Game& g) { return g.particles.size(); }
  static size_t wreckCount(const Game& g) { return g.wreck.size(); }
  static size_t debrisCount(const Game& g) { return g.debris.size(); }
  static int hubFly(Game& game) {   // the hub at the player's airport for HUBFRAMES frames, then a job from there: what the loading screen still has to do
    g_ren.entSync = false;
    game.headless = false; game.screen = SCR_HUB;
    const int hf = getenv("HUBFRAMES") ? atoi(getenv("HUBFRAMES")) : 120;
    for (int i = 0; i < hf; i++) { game.update(1.f / 30.f); game.render(); }
    printf("hub at %s: %d frames, scenery pending %d, chunks %d\n", g_world.airports[game.career.location].code, hf, g_ren.entPending, (int)g_ren.entChunks);
    Contract c = game.career.board.empty() ? Contract() : game.career.board[0];
    if (game.career.board.empty()) { c.from = game.career.location; c.to = (c.from + 1) % (int)g_world.airports.size(); c.title = "Test"; }
    game.startFlight(c, 1, Career::SRC_RENT);
    int frames = 0, pend0 = -1; const int b0 = g_ren.bakeBuilt;
    while (game.screen == SCR_LOADING && game.loadReadyT < 0 && frames < 2000) { game.update(1.f / 30.f); game.render(); if (pend0 < 0) pend0 = g_ren.entPending; frames++; }
    printf("loading from %s: first frame pending %d, ready after %d frames (%.1f s of game time), meshes built %d\n", g_world.airports[c.from].code, pend0, frames, game.loadT, g_ren.bakeBuilt - b0);
    return 0;
  }
  // A session through the menus and into flight, every frame timed (the seamlessness check): the launch's prewarm, then
  // the main menu, each of the hub's tabs and settings pages, Free Flight's setup, the real Earth's picker typed into
  // (with EARTH_PACK), the region map; the flight's loading, then the flight - outside, the cockpit, the GPS, the pause
  // menu, photo mode, time warp - and on the real Earth a slide into the next chart; the debrief and back to the menu.
  // Each frame's update (a 60th of a second of game time, as at 60 fps) and render (the GPU finished) on the wall clock;
  // a segment's median and worst, and every frame well over its segment's median named by what it did on the game's
  // thread: programs compiled, bodies baked, scenery made there. The software renderer's frame is slow throughout:
  // what matters is the frame that stands out of its neighbours. SEAMLESS_MENUS=0 leaves the menus out, SEAMLESS_FLIGHT=0
  // the flight, SEAMLESS_SLIDE=0 the slide
  static int seamless(Game& game) {
    using clk = std::chrono::steady_clock;
    setvbuf(stdout, nullptr, _IOLBF, 0);   // (each line as it comes: a run cut short keeps what it found)
    auto residentMB = [] {
      long pages = 0, resident = 0;
      if (FILE* f = fopen("/proc/self/statm", "r")) { if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0; fclose(f); }
      return resident * (double)sysconf(_SC_PAGESIZE) / 1048576.0;
    };
    for (int i : kCareerAircraft) Plane::perf(&kAircraft[i]);
    game.headless = false; g_ren.entSync = false; game.profile.welcomed = true;
    {   // the launch's prewarm (Game::prewarm) of what the session shows: the menu's scenery and terrain lighting, then the
        // bodies and cockpits of the menu's aircraft and the one flown. The launch warms every craft, but on the software
        // renderer each one's compiled programs stay in memory, some 14 GB for them all: the AI traffic, which would draw
        // the rest, is left off (a frame that compiles or bakes another type here the launch would have spared)
      const auto t0 = clk::now();
      game.debugScene("menu");
      game.set.traffic = false;
      auto offscreen = [&] { g_ren.setOffscreen(true); game.update(1.f / 60.f); game.render(); glFinish(); g_ren.setOffscreen(false); };
      g_ren.entSync = true;
      for (int i = 0; i < 400; i++) { game.realTime = 3.f; offscreen(); if (i >= 2 && g_ren.entPending == 0 && !game.loadingShadowPending()) break; }
      g_ren.entSync = false;
      std::vector<int> crafts{0, 1};
      if (getenv("SEAMLESS_RESEARCH") && atoi(getenv("SEAMLESS_RESEARCH"))) crafts.push_back(kSomnus);   // (the craft the research flight flies, warmed as the launch warms it)
      for (int craft : crafts) for (bool inside : {false, true}) { game.prewarmCraft = craft; game.prewarmInside = inside; game.realTime = 3.f; offscreen(); }
      game.prewarmCraft = -1; game.prewarmInside = false; game.realTime = 0.f;
      printf("prewarm %.1f s, %.0f MB resident\n", std::chrono::duration<double>(clk::now() - t0).count(), residentMB());
    }
    struct Seg { std::string name; std::vector<double> u, r; int flagged = 0; };
    std::vector<Seg> segs;
    int frameNo = 0;
    // the screen as the player sees it: shown at each frame's end and from inside a long bake (platformPresent); the
    // longest it held still, and how often a bake showed it
    auto lastShown = clk::now(); double stillMs = 0, stillWorst = 0; int shownInside = 0;
    auto shown = [&] { const auto n = clk::now(); stillMs = std::max(stillMs, std::chrono::duration<double, std::milli>(n - lastShown).count()); lastShown = n; };
    game.platformPresent = [&] { glFinish(); shown(); shownInside++; };
    auto begin = [&](const std::string& name) { segs.push_back(Seg{name, {}, {}, 0}); printf("-- %s (%.0f MB resident)\n", name.c_str(), residentMB()); };
    auto median = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    auto frame = [&](const char* what = "") {
      Seg& s = segs.back();
      const int miss0 = g_shaderCacheMisses, bake0 = g_ren.bakeCount, gen0 = g_scenery.syncBuilt, inside0 = shownInside;
      stillMs = 0;
      const long long us0 = g_shaderCompileUs; const double genMs0 = g_scenery.syncBuildMs;
      const auto t0 = clk::now();
      game.update(1.f / 60.f);
      const auto t1 = clk::now();
      game.render(); glFinish();
      const auto t2 = clk::now();
      game.framePresented(); game.in.endFrame(); shown(); stillWorst = std::max(stillWorst, stillMs);
      const double u = std::chrono::duration<double, std::milli>(t1 - t0).count(), r = std::chrono::duration<double, std::milli>(t2 - t1).count();
      const double mu = median(s.u), mr = median(s.r);
      const bool slow = s.u.size() >= 3 && (u > std::max(8.0, 3.0 * mu) || r > 1.6 * mr + 40.0);
      s.u.push_back(u); s.r.push_back(r);
      static const bool every = getenv("SEAMLESS_EVERY") && atoi(getenv("SEAMLESS_EVERY"));   // (every frame printed)
      if (slow || every || (s.u.size() <= 3 && (u > 8.0 || r > 400.0))) {
        s.flagged += slow;
        printf("  frame %5d %-22s update %7.1f ms (median %5.1f)  render %7.1f ms (median %6.1f)  compiled %d (%.0f ms)  baked %d  scenery made here %d (%.0f ms)  screen %d  still %.0f ms (shown %d inside)\n",
               frameNo, what, u, mu, r, mr, (int)g_shaderCacheMisses - miss0, (g_shaderCompileUs - us0) / 1000.0, g_ren.bakeCount - bake0,
               g_scenery.syncBuilt - gen0, g_scenery.syncBuildMs - genMs0, (int)game.screen, stillMs, shownInside - inside0);
        fflush(stdout);
      }
      if (frameNo % 100 == 0) printf("  frame %5d: %.0f MB resident\n", frameNo, residentMB());
      frameNo++;
    };
    auto frames = [&](int n, const char* what = "") { for (int i = 0; i < n; i++) frame(what); };
    auto summary = [&] {
      printf("\n%-34s %6s %10s %10s %10s %10s %8s\n", "segment", "frames", "update med", "update max", "render med", "render max", "flagged");
      for (const Seg& s : segs) {
        if (s.u.empty()) continue;
        printf("%-34s %6zu %10.1f %10.1f %10.1f %10.1f %8d\n", s.name.c_str(), s.u.size(), median(s.u), *std::max_element(s.u.begin(), s.u.end()),
               median(s.r), *std::max_element(s.r.begin(), s.r.end()), s.flagged);
      }
    };
    // ---- SEAMLESS_RESEARCH=1: this alone - the research terminal opened from the main menu, warmed, and an XR-40
    // research flight initiated from it at Meadowbrook, on the ground, through its loading screen into the flight
    if (getenv("SEAMLESS_RESEARCH") && atoi(getenv("SEAMLESS_RESEARCH"))) {
      begin("main menu"); game.screen = SCR_MENU; frames(30);
      begin("research: terminal warming");
      game.resCraft = kSomnus; game.resAirport = std::max(0, g_world.findAirport("MDB")); game.resAirborne = false; game.resCard = -1;
      game.screen = SCR_RESEARCH; game.resOpened = game.realTime;
      game.resWarm = true; game.resWarmFrames = 0; game.resBakeSeen = g_ren.bakeCount; game.resWork = 0.f; game.resPend0 = 0;
      for (int i = 0; i < 3000 && game.resWarm; i++) frame("warming");
      begin("research: terminal"); frames(60);
      begin("research: initiate, loading screen");
      game.launchResearch(); stillWorst = 0;
      for (int i = 0; i < 6000 && game.screen == SCR_LOADING && game.loadReadyT < 0; i++) frame("loading");
      frames(30, "establishing shot");
      game.in.pressed[K_ENTER] = true; frame("into the flight");
      printf("loading: the screen held still at most %.0f ms\n", stillWorst);
      begin("research: flight"); frames(120);
      summary();
      return game.screen == SCR_FLIGHT ? 0 : 1;
    }
    // ---- the menus (SEAMLESS_MENUS=0: straight to the flight)
    const bool menus = !(getenv("SEAMLESS_MENUS") && !atoi(getenv("SEAMLESS_MENUS")));
    if (menus) {
    begin("main menu"); game.screen = SCR_MENU; frames(60);
    const char* tabs[] = {"hub: contracts", "hub: hangar", "hub: airline", "hub: logbook", "hub: settings"};
    for (int t = TAB_CONTRACTS; t <= TAB_SETTINGS; t++) { begin(tabs[t]); game.screen = SCR_HUB; game.hubTab = t; frames(20); }
    for (int p = 1; p <= 2; p++) { begin(p == 1 ? "hub: settings, controls" : "hub: settings, joystick"); game.settingsPage = p; frames(12); }
    game.settingsPage = 0;
    begin("free flight: setup"); game.beginFreeFlightSetup(); frames(30);
    if (earthCharts::pack()) {
      begin("free flight: real Earth picker"); game.freeEarthPicker = true; frames(10, "opened");
      for (const char* q : {"BOSTON", "KMVY"}) {
        game.earthQuery.clear();
        for (const char* c = q; *c; c++) { game.in.text = std::string(1, *c); frame("a letter typed"); frames(3); }
      }
      game.freeEarthPicker = false;
    }
    begin("free flight: region map"); game.freeRegionMap = true; frames(30); game.freeRegionMap = false;
    } else { game.beginFreeFlightSetup(); }
    if (getenv("SEAMLESS_FLIGHT") && !atoi(getenv("SEAMLESS_FLIGHT"))) { summary(); return 0; }   // (the menus alone)
    const auto pack = earthCharts::pack();
    // ---- into the flight: the real Earth's KMVY with a pack, else the islands' Solace Capital
    begin("flight: region loading");
    if (pack) {
      const auto found = earthCharts::searchAirports("KMVY", 1);
      if (found.empty()) { printf("no KMVY in the pack\n"); return 1; }
      game.chooseEarthAirport(found[0]);
    }
    for (int i = 0; i < 6000 && game.screen == SCR_REGION_LOADING; i++) frame("region loading");
    if (!game.regionError.empty()) { printf("region failed: %s\n", game.regionError.c_str()); return 1; }
    begin("flight: loading screen");
    game.freeCraft = 0; game.freeAirborne = true;
    game.freeAirport = std::max(0, g_world.findAirport(pack ? "KMVY" : "CAP"));
    game.launchFreeFlight();
    for (int i = 0; i < 6000 && game.screen == SCR_LOADING && game.loadReadyT < 0; i++) frame("loading");
    frames(30, "establishing shot");
    game.in.pressed[K_ENTER] = true; frame("into the flight");
    if (game.screen != SCR_FLIGHT) { printf("not in flight (screen %d)\n", (int)game.screen); return 1; }
    game.botControl = true;
    game.plane.apEngage(Plane::AP_HOLD, -1, game.wx);   // (level, on its heading)
    begin("flight: outside"); game.camMode = 0; frames(120);
    begin("flight: cockpit"); game.camMode = 1; frame("-> cockpit"); frames(60);
    begin("flight: outside again"); game.camMode = 0; frame("-> outside"); frames(30);
    begin("flight: GPS"); game.showMap = true; frame("GPS opened"); frames(40); game.showMap = false; frame("GPS closed");
    begin("flight: pause menu"); game.paused = true; frame("paused"); frames(30); game.paused = false; frame("resumed");
    begin("flight: photo mode"); game.enterPhoto(); frame("photo mode"); frames(30); game.exitPhoto(); frame("photo mode left");
    begin("flight: time warp x4"); game.timeAccel = 4.f; frames(60); game.timeAccel = 1.f;
    if (pack && !(getenv("SEAMLESS_SLIDE") && !atoi(getenv("SEAMLESS_SLIDE")))) {
      begin("flight: the next chart prepared");
      auto place = [&](float x) {
        const float h = std::max(g_world.height(x, 0.f), 0.f) + 600.f;
        game.plane.pos = vec3(x, h, 0.f); game.plane.vel = vec3(70.f, 0.f, 0.f); game.plane.onGround = false;
        game.plane.q = quat::axisAngle(vec3(0, 1, 0), -0.5f * PI);
        game.plane.apEngage(Plane::AP_HOLD, -1, game.wx);
      };
      const std::string from = g_world.regionId;
      place(12000.f);
      const auto deadline = clk::now() + std::chrono::minutes(4);
      while ((!game.earthPrefetch || !game.earthPrefetch->scenery || game.earthPrefetch->scenery->generated() < 1500) && clk::now() < deadline) {
        frame("preparing ahead");
        if (game.plane.pos.x > 16000.f) place(12000.f);   // (kept short of the move while the next chart prepares)
      }
      printf("  the next chart %s after %d frames\n", game.earthPrefetch ? game.earthPrefetch->region.id.c_str() : "(none)", (int)segs.back().u.size());
      begin("flight: the move into it");
      place(18000.f);
      for (int i = 0; i < 3000 && g_world.regionId == from; i++) frame("waiting for the move");
      frame(g_world.regionId != from ? "moved" : "(not moved)");
      begin("flight: after the move"); frames(90);
    }
    begin("debrief"); game.endFlight(false, "Seamlessness check", OUT_ABANDONED); frames(30);
    begin("main menu again"); game.screen = SCR_MENU; frames(30);
    summary();
    return 0;
  }
  // HORIZON=<seconds>: the generated regions round the flight built and uploaded before the shot (Renderer::
  // updateRegionHorizon runs on a worker; a scene's few frames would otherwise show the sea without them)
  static void waitHorizon(Game& g, float seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000.f));
    while (std::chrono::steady_clock::now() < until) {
      g_ren.beginRegionUploadFrame(); g.updateHorizon();
      if (g_ren.horizonWantedMeshes > 0 && g_ren.horizonReadyMeshes >= g_ren.horizonWantedMeshes && !g_ren.horizonCpuPending && g_ren.horizonPendingMeshes == 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    printf("horizon: %d of %d regions ready, %d resident, %u jobs%s%s\n", g_ren.horizonReadyMeshes, g_ren.horizonWantedMeshes, g_ren.horizonResidentMeshes,
           g_ren.horizonJobsStarted, g_ren.horizonError.empty() ? "" : ", error: ", g_ren.horizonError.c_str());
  }
  static void drawUI(Game& g, const std::string& what) {
    g.realTime = 5; g.uiDt = 1.f;
    if (getenv("UISCALE")) g.set.uiScale = (float)atof(getenv("UISCALE"));   // (the review's 140% checks)
    if (what == "controls") { g.hubTab = TAB_SETTINGS; g.settingsPage = 1; if (getenv("CAPTURE")) { g.bindCapture = ACT_GEAR; g.bindCaptureDev = 0; g.bindCaptureT = 3; } g.drawHub(); }
    else if (what == "settings") { g.hubTab = TAB_SETTINGS; g.settingsPage = 0; g.drawHub(); }
    else if (what == "joystick") {   // Settings -> Joystick with a stick and pedals connected, three axes bound, one capturing
      JoyState stick; stick.id = "{A1}"; stick.name = "T.16000M FCS"; stick.axes = 4; stick.buttons = 16;
      for (int a : {0, 1, 5, 6}) stick.hasAxis[a] = true;
      stick.axis[0] = .35f; stick.axis[1] = -.6f; stick.axis[5] = .05f; stick.axis[6] = .4f;
      JoyState pedals; pedals.id = "{B2}"; pedals.name = "TFRP Rudder"; pedals.axes = 3; pedals.buttons = 0;
      for (int a : {0, 1, 5}) pedals.hasAxis[a] = true;
      pedals.axis[0] = -1.f; pedals.axis[1] = -.2f; pedals.axis[5] = -.3f;
      g.in.joys = {stick, pedals};
      auto bind = [&](int ax, const JoyState& j, int a, bool inv, float dead, float curve) { AxisBind& b = g.set.axisBind[ax]; b.dev = j.id; b.devName = j.name; b.axis = a; b.invert = inv; b.dead = dead; b.curve = curve; };
      bind(AX_PITCH, stick, 1, false, .05f, .3f); bind(AX_ROLL, stick, 0, false, .05f, .3f); bind(AX_THROTTLE, stick, 6, true, .03f, 0.f); bind(AX_YAW, pedals, 5, false, .08f, .5f);
      if (getenv("CAPTURE")) { g.axisCapture = AX_BRAKE; g.bindCaptureT = 3; }
      g.hubTab = TAB_SETTINGS; g.settingsPage = 2; g.drawHub();
    }
    else if (what == "menucrash") {   // the main menu after a crash: where its report is
      g.crashReport = "C:\\Users\\Pilot\\AppData\\Roaming\\SomniumVolandi\\crash\\crash-20261018-101500.txt";
      g.platformOpen = [](const std::string&) {};
      g.drawMenu();
    }
    else if (what == "radioempty") { g.stations.clear(); g.saveDir = "C:\\Users\\Pilot\\AppData\\Roaming\\SomniumVolandi"; g.platformOpen = [](const std::string&) {}; g.screen = SCR_HUB; g.showRadio = true; g.hubTab = TAB_CONTRACTS; g.drawRadioPanel(60 * g.S(), 90 * g.S()); }
    else if (what == "welcome") { g.headless = false; g.profile.name = getenv("WNAME") ? getenv("WNAME") : ""; g.drawMenu(); g.headless = true; }   // (the first start)
    else if (what == "milock") { g.career.flights = 4; g.screen = SCR_HUB; g.hubTab = TAB_CONTRACTS; g.hubList = 2; g.drawHub(); }   // (the Military board before ten flights)
    else if (what == "legends") { g.career.finished = true; g.career.license = LIC_ATP; g.career.storyIndex = (int)g_story.size(); g.profile.num["legend.LEG2"] = 1;
      g.screen = SCR_HUB; g.hubTab = TAB_CONTRACTS; g.hubList = 0; g.selContract = 2; g.drawHub(); }
    else if (what == "pause") { g.drawPause(); }
    else if (what == "credits") { g.profile.welcomed = true; g.showCredits = true; g.creditsByHand = true; g.creditsScroll = getenv("CREDITSAT") ? (float)atof(getenv("CREDITSAT")) : 640.f; g.drawMenu(); }   // (CREDITSAT: how far they have rolled)
    else if (what == "pausephoto") { g.in.pad = true; g.padActive = true; g.drawPause(); }   // (the pause menu as a controller shows it)
    else if (what == "demoend" || what == "demomil") {   // the demo's end over the hub, and its Military teaser
      g.demo = true; g.career.license = what == "demoend" ? LIC_PPL : LIC_STUDENT; g.licenceShown = -1; g.screen = SCR_HUB; g.hubTab = TAB_CONTRACTS; g.hubList = what == "demomil" ? 2 : 0; g.drawHub();
    }
    else if (what == "menu") { g.drawMenu(); }
    else if (what == "earthpicker") {   // Free Flight's real-Earth airport picker (EQ: what's typed)
      g.screen = SCR_FREE_FLIGHT; g.freeEarthPicker = true; g.earthQuery = getenv("EQ") ? getenv("EQ") : ""; g.drawFreeFlightSetup(FrameParams{});
    }
    else if (what == "debrief") {   // a story delivery's settlement with eleven lines and a new licence (the review of v3.24.0, R9)
      g.career.storyIndex = 3; g.contract = g_story[3]; g.lastSuccess = true; g.stars = 2; g.debriefTitle = "Delivered";
      g.result.landed = true; g.touchdownFpm = 240; g.flightClock = 1312; g.licenseBefore = LIC_STUDENT; g.career.license = LIC_PPL;
      const char* lines[] = {"Contract payment", "Reputation bonus", "On time", "Passenger comfort", "Rental fee", "Positioning", "Fuel uplift", "Landing fee", "Loan payment", "Airline: CAP - NPT route", "Pilot wages"};
      int amt[] = {24000, 2400, 1200, -900, -1800, -350, -720, -150, -2100, 8232, -1900};
      for (int i = 0; i < 11; i++) g.payout.push_back({lines[i], amt[i]});
      g.result.tdPastThrM = 310; g.result.rwyLenM = 1500; g.result.centerlineErr = 1.2f; g.result.thrKt = 74; g.result.thrAglM = 15; g.result.touchdownFpm = 240;
      g.lastMark = landingMark(g.result, kAircraft[1]); g.plane.spec = &kAircraft[1];
      g.pbNew = true; g.pbOld = 312; g.flightAch = {"butter", "night", "airports10", "by_the_book"};
      g.debriefT0 = getenv("DEBRIEFT") ? g.realTime - (float)atof(getenv("DEBRIEFT")) : 0.f;
      g.screen = SCR_DEBRIEF; g.drawDebrief();
    }
    else if (what == "licence" || what == "finale" || what == "logach" || what == "logpass" || what == "banner") {   // the profile's screens (profile.h)
      g.career.license = LIC_CPL; g.career.flights = 64; g.career.hours = 41.5f; g.career.landings = 63; g.career.bestLandingFpm = 38; g.career.money = 412000;
      g.profile.name = "Chad";
      for (const char* id : {"first_flight", "butter", "night", "lic_ppl", "lic_cpl", "owner", "airports10", "ufo"}) g.profile.ach[id] = 1792400000;
      for (int i = 0; i < 11; i++) g.profile.airports.insert(g_world.airports[i * 2 % (int)g_world.airports.size()].code);
      for (const Site& st : profileSites()) { g.profile.sites.insert(st.id); if (g.profile.sites.size() >= 6) break; }
      g.profile.seen.insert("ufo"); g.profile.num["butters"] = 17; g.profile.num["warpKm"] = 212;
      g.profile.best["dawnlark"] = 38; g.profile.best["ambler"] = 96; g.profile.best["archipel"] = 141;
      g.screen = SCR_HUB; g.hubTab = what == "logach" || what == "logpass" ? TAB_LOGBOOK : TAB_CONTRACTS;
      g.logPage = what == "logach" ? 1 : what == "logpass" ? 2 : 0;
      if (what == "licence") g.licenceShown = LIC_CPL;
      if (getenv("TRIALCARD")) {   // TRIALCARD=medal (0..3): a gate run's card, with splits against a best run
        Game::TrialResult& R = g.trialResult; R.kind = Game::TR_GATES; R.spec = 2; R.medal = atoi(getenv("TRIALCARD")); R.score = 131.4f; R.rank = 1; R.best = 131.4f; R.newBest = true;
        R.splits = {14.2f, 30.9f, 47.1f, 66.0f, 84.3f, 103.8f, 131.4f}; R.bestSplits = {14.6f, 31.2f, 46.5f, 66.9f, 85.0f, 104.9f, 133.0f};
        g.hubList = 1;
      }
      if (what == "finale") { g.career.finished = true; g.career.storyIndex = (int)g_story.size(); g.finaleShown = true; }
      g.drawHub();
      if (what == "licence" || what == "finale" || getenv("TRIALCARD")) { g.realTime += 3.f; g_ren.uiEnd(); glClear(GL_COLOR_BUFFER_BIT); g_ren.uiBegin(); g.drawHub(); }   // (in, once its card has come in)
      if (what == "banner") { g.achShow.push_back({"greaser", 1.2f, true}); g.drawAchievementBanner(); }
    }
    else if (what == "wavecrest_free") {   // the review of v3.31.0, U1: a CPL pilot owning only the Wavecrest, Free Flight MDB -> HFS
      g.career.license = LIC_CPL; g.career.money = 50000; g.career.storyIndex = (int)g_story.size();
      g.career.location = g_world.findAirport("MDB"); g.career.fleet.clear(); g.career.fleet.push_back({8, g.career.location, 1000, 1});
      g.freeDest = g_world.findAirport("HFS");
      g.hubTab = TAB_CONTRACTS; g.selContract = 0; g.selAircraft = -1;
      for (int f = 0; f < 3; f++) {
        if (f) { g_ren.uiEnd(); glClear(GL_COLOR_BUFFER_BIT); g_ren.uiBegin(); }
        g.drawHub();
      }
      printf("wavecrest_free: selected aircraft %d (%s)\n", g.selAircraft, g.selAircraft >= 0 ? kAircraft[g.selAircraft].name : "-");
    }
    else if (what.rfind("contracts_", 0) == 0) {   // a story job's briefing at ATP with one owned Emissary at its airport (the review of v3.24.0, R1)
      const std::string id = what.substr(10);
      for (int i = 0; i < (int)g_story.size(); i++) if (g_story[i].id == id) g.career.storyIndex = i;
      const Contract* st = g.career.nextStory();
      g.career.license = LIC_ATP; g.career.money = 1000000; g.career.location = st ? st->from : 0;
      g.career.fleet.push_back({6, g.career.location, 1000, 1});
      if (getenv("PVI")) { g.career.location = g_world.findAirport("PVI"); g.career.refreshBoard(); printf("board %zu jobs\n", g.career.board.size()); if (getenv("SELLAST")) g.selContract = 99; }
      g.hubTab = TAB_CONTRACTS; if (!getenv("SELLAST")) g.selContract = 0; g.selAircraft = -1;
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
  g_ren.matDir = getenv("MATDIR") ? getenv("MATDIR") : "assets/materials";   // (MATDIR="": the procedural layers)
  if (!g_ren.init(W, H)) { printf("init failed: %s\n%s", g_ren.error.c_str(), g_shaderNotes.c_str()); return 1; }
  if (!g_shaderNotes.empty()) printf("shader notes:\n%s", g_shaderNotes.c_str());   // (programs rejected and what built instead: NVFAIL)
  g_ren.entSync = !getenv("ENTSTREAM");   // captures generate every scenery chunk in range up front
  printf("renderer ok (shader cache: %d loaded, %d compiled, %.0f s)\n", g_shaderCacheHits.load(), g_shaderCacheMisses.load(),
         std::chrono::duration<double>(std::chrono::steady_clock::now() - tInit).count());
  printf("ground photographs: %s\n", g_ren.matEnvScanned ? (g_ren.envMatCompressed ? "2K, block-compressed (BC3)" : "2K, uncompressed") : g_ren.matEnvFallbackScanned ? "512" : "none");
  std::string scene = argc > 1 ? argv[1] : "default";
  // ---- a render server: everything loaded once (the context, the shaders, the islands, the textures, the aircraft
  // meshes, and the scenery as it is generated), then one shot per request. Requests are lines on a named pipe
  // (SERVE_REQ, default /tmp/claude-0/sp/rs.req): "<scene> [frames=N] [taam=N] [bench=N] [dbgoff=N] [out=path] [NAME=value]"
  // (NAME=value: an environment setting for this shot and the ones after it - AT, CAMPITCH, WX, TOD ...; NAME= clears it);
  // (settle=N: N frames a 240th of a second apart, the anti-aliasing settling as for the loading pictures)
  // each reply ("ok <path> <seconds>") goes to SERVE_REP (/tmp/claude-0/sp/rs.rep). tools/render_client.sh sends one.
  if (scene == "serve") {
    if (!perfCached && !cacheDir.empty()) { for (int i : kCareerAircraft) Plane::perf(&kAircraft[i]); Plane::perfSave(cacheDir + "/perf.bin", cacheStamp); }
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
        int frames = 0, taam = 0, bench = 0, settle = 0;
        g_ren.dbgOff = 0;
        for (size_t k = 1; k < tok.size(); k++) {
          size_t e = tok[k].find('='); if (e == std::string::npos) continue;
          std::string key = tok[k].substr(0, e), v = tok[k].substr(e + 1);
          if (key == "frames") frames = atoi(v.c_str()); else if (key == "taam") taam = atoi(v.c_str()); else if (key == "bench") bench = atoi(v.c_str()); else if (key == "settle") settle = atoi(v.c_str());
          else if (key == "dbgoff") g_ren.dbgOff = atoi(v.c_str()); else if (key == "out") out = v;
          else if (!key.empty() && isupper((unsigned char)key[0])) {   // (AT=..., CAMPITCH=..., WX=...: the scene's own settings; NAME= clears one)
            if (v.empty()) unsetenv(key.c_str()); else setenv(key.c_str(), v.c_str(), 1);
          }
        }
        g_ren.resetTemporal();
        Game* g = new Game();
        g->initHeadless(); g->debugScene(sc);
        const bool clean = getenv("HUDOFF") && atoi(getenv("HUDOFF"));   // (HUDOFF=1: no HUD, messages or hints - a clean picture)
        auto tidy = [&] { if (clean) GameTest::clean(*g); };
        tidy();
        for (int i = 0; i < 3; i++) { g->update(1.f / 30.f); tidy(); g->render(); }
        if (getenv("HORIZON")) { GameTest::waitHorizon(*g, (float)atof(getenv("HORIZON"))); fflush(stdout); for (int i = 0; i < 3; i++) g->render(); }
        for (int i = 0; i < taam; i++) { g->update(1.f / 60.f); tidy(); g->render(); }
        for (int i = 0; i < frames; i++) g->render();
        for (int i = 0; i < settle; i++) { g->update(1.f / 240.f); g->render(); }   // (the loading pictures: the TAA settles, barely moving)
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
  if (scene == "hubfly") return GameTest::hubFly(game);
  if (scene == "somnushitch") return GameTest::somnusHitch(game);
  if (scene == "seamless") return GameTest::seamless(game);
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
  if (scene == "researchwarm") return GameTest::researchWarm(game);
  if (getenv("PREWARM")) game.prewarm([](float, const std::string& what) { if (getenv("PREWARMLOG")) printf("prewarm: %s\n", what.c_str()); });   // (every aircraft's body built first, as the game's launch does: the traffic drawn from meshes; PREWARMLOG: what each step says)
  if (getenv("NOTRAFFIC")) game.set.traffic = false;   // (A/B timings of the player's aircraft alone)
  game.debugScene(scene);
  if (getenv("PREPASSAUTO")) {   // ground depth first, measured as in play (PREPASSAUTO=<frames to the first trial>)
    g_ren.prepassAutoOn = true; g_ren.prepareTerrainDepth(); g_ren.prepassAuto.countdown = std::max(1, atoi(getenv("PREPASSAUTO")));
  }
  for (int i = 0; i < 3; i++) { game.update(1.f / 30.f); game.render(); }
  if (getenv("TAAM")) for (int i = 0, n = atoi(getenv("TAAM")); i < n; i++) {   // moving frames
    game.update(1.f / 60.f); game.render();
    const int seq = getenv("SEQ") ? atoi(getenv("SEQ")) : 0;   // (the last SEQ of them saved too: frame-to-frame flicker)
    if (seq > 0 && i >= n - seq) { char b[256]; snprintf(b, sizeof b, "/tmp/claude-0/sp/seq_%s_%d.ppm", scene.c_str(), i - (n - seq)); glFinish(); g_ren.screenshot(b); }
  }
  if (getenv("HORIZON")) { GameTest::waitHorizon(game, (float)atof(getenv("HORIZON"))); for (int i = 0; i < 3; i++) game.render(); }
  if (getenv("TAAF")) for (int i = 0, n = atoi(getenv("TAAF")); i < n; i++) game.render();   // extra static frames: let TAA converge
  if (getenv("SIMFRAMES")) {   // the scene flown on first, simulated but not drawn (a timing scene should hold still)
    for (int i = 0, n = atoi(getenv("SIMFRAMES")); i < n; i++) game.update(1.f / 60.f);
    for (int i = 0; i < 3; i++) game.render();
  }
  if (getenv("GROUNDCOUNT")) {   // the main view's ground over one frame, as the analysis counts it (renderer.h groundStats)
    g_ren.countGround = true; game.render(); g_ren.countGround = false; glFinish();
    double kept = -1, shaded = -1; const bool got = g_ren.readGroundCounts(kept, shaded);
    const Renderer::GroundStats& gs = g_ren.groundStats;
    const Game::BenchAircraft A = game.benchmarkAircraft();
    printf("ground: %d chunks + %d coarse, aircraft depth first %d, own depth first %d; counted %d: %.0f shaded, %.0f kept\n"
           "  camera (%.0f, %.0f, %.0f), aircraft (%.0f, %.0f, %.0f), fuel %.0f kg, engine %d, clock %.1f s\n",
           gs.chunks, gs.coarse, (int)gs.cockpitFirst, (int)gs.prepassed, (int)got, shaded, kept,
           g_ren.prepassCam.x, g_ren.prepassCam.y, g_ren.prepassCam.z, A.pos.x, A.pos.y, A.pos.z, A.fuel, (int)A.engine, A.clock);
  }
  glFinish();
  if (getenv("BENCH")) {
    printf("bench scene: %zu particles, %zu wreck pieces, %zu fragments\n", GameTest::particleCount(game), GameTest::wreckCount(game), GameTest::debrisCount(game));
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
    if (getenv("PREPASSAUTO")) { const PrepassAuto& pa = g_ren.prepassAuto; printf("ground depth first: %s, %d trials, %d switches, last trial %.1f ms kept vs %.1f ms other\n", pa.on ? "on" : "off", pa.trials, pa.switches, pa.lastKept, pa.lastOther); }
    { const float* pm = g_ren.passMs; printf("passes (GPU ms, smoothed): world %.1f  displays %.1f  feeds %.1f  objects %.1f  shadow proxy %.1f  lighting %.1f  taa %.1f  sprites %.1f  bloom %.1f  shafts %.1f  composite %.1f\n", pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]); }
  }
  std::string out = "/tmp/claude-0/sp/shot_" + scene + ".ppm";
  g_ren.screenshot(out.c_str());
  printf("wrote %s\n", out.c_str());
  return 0;
}
