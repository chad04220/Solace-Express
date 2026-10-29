// Somnium Volandi - the loading screen and the launch: readiness, prewarm, queued launches, the loading log
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include "airport_layout.h"
#include "entities.h"
#include "entity_lod.h"
#include "hive_world_collision.h"
#include "scenery.h"
#include "regional_surface.h"
#include "models.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "military_missions.h"
#include "game_internal.h"

// ------------------------------------------------------------------ pre-flight loading screen
// After a flight is chosen the world is held still while the scenery around the start position is generated (with a
// bigger per-frame budget), behind a card showing an aerial image of the airport. Once nothing is left to stream it
// cross-fades into a live establishing shot of the aircraft, and the player starts the flight when ready.
bool Game::loadingShadowPending() const {
  vec3 direction, colour; float night;
  computeSun(timeOfDay, direction, colour, night);
  return g_ren.tshRequiredPending(direction.y);
}
Game::SceneWork Game::sceneWork(int& bakeSeen) const {
  SceneWork w;
  w.pending = std::max(0, g_ren.entPending);
  w.shadow = loadingShadowPending();
  w.baked = bakeSeen != g_ren.bakeCount; bakeSeen = g_ren.bakeCount;
  return w;
}
void Game::updateLoading(float dt) {
  // Back always wins over setup, including the update immediately after the first presented card.
  if (pendingLaunch) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) {
      cancelQueuedLaunch();
      in.pressed[K_ESC] = false; in.buttonsPressed &= ~PAD_B;   // (the menu returned to doesn't take it as its own Back)
      return;
    }
    loadT += dt;
    if (!launchFramePresented) return;
    PendingLaunch L = std::move(*pendingLaunch);
    pendingLaunch.reset();
    launchFrameDrawn = launchFramePresented = false;
    // the launch runs from where it was asked for: one that doesn't take (a save that fails, a selection gone stale)
    // leaves the player there with its message; one that does sets up the flight and its loading screen again
    screen = L.from; paused = L.paused;
    launchRunning = true; L.begin(); launchRunning = false;
    // (the flight set up: its loading screen as it now stands goes up on its own before the first frame of its scene,
    // which can take a while - the driver finishing the flight's programs on their first draw - so it never stands
    // still on the card of a flight not yet begun)
    launchSetUpShown = screen == SCR_LOADING;
    if (L.kind == LAUNCH_CAREER && screen == SCR_DEBRIEF) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; }   // (the debrief's retry didn't take: its message is on the hub)
    return;
  }
  if (pendingMilitaryLaunch) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) { returnToMilitary(); return; }
    loadT += dt;
    if (!militaryLaunchFramePresented) return;
    const Contract c = *pendingMilitaryLaunch;
    pendingMilitaryLaunch.reset();
    militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
    if (!beginMilitaryLaunch(c)) returnToMilitary();
    else launchSetUpShown = screen == SCR_LOADING;   // (its loading screen alone first, as a queued launch's: above)
    return;   // the first real scene and collision warmup still use the normal readiness path
  }
  if (militaryFlight || isMilitaryContract(contract.type)) {
    const auto& m = career.military;
    if (!militaryFlight || source != Career::SRC_MILITARY || commitBlocked() ||
        !career.attemptOpen || !m.activeAttempt || m.activeAttempt != career.attempt ||
        m.activeAttempt <= m.lastSettledAttempt || m.activeKind != contract.type-CT_MIL_RECON ||
        m.activeAirport != contract.from || career.canFly(contract,specIdx) != Career::SRC_MILITARY) {
      returnToMilitary(); return;
    }
  }
  if (militaryFlight && (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B))) { returnToMilitary(); return; }
  warmCombatCollision();
  loadT += dt;
  if (!headless) g_ren.prepareTerrainDepth();   // (ground depth first's program, for its trials in flight: renderer.h)
  if (!headless && earthFlight) g_ren.prepareTerrainDither();   // (the ground's program for the Earth regions' far blends)
  if (loadReadyT < 0) {
    dbgCam = false;
    updateCamera(dt);   // stream around the camera the flight will open with
    g_ren.entBudgetMs = 14.f;
    const SceneWork work = sceneWork(loadBakeSeen);
    loadPend0 = std::max(loadPend0, work.pending);
    // Observe completed scene frames; elapsed time alone is not evidence that any work finished.
    if (loadFrames != loadObservedFrame) {
      const bool viewsIn = headless || loadViewsDrawn || crashed || !plane.spec;   // (the flight's views each drawn once: render)
      if (loadFrames >= 2 && work.ready() && viewsIn) ++loadStableFrames;
      else loadStableFrames = 0;
      loadObservedFrame = loadFrames;
    }
    const float completed = sceneProgress(loadSetupDone(), loadFrames >= 2 ? work.pending : std::max(1, loadPend0), loadPend0, loadFrames >= 2 && !work.shadow, std::min(loadStableFrames / 2.f, 1.f));
    loadShown = std::max(loadShown, std::min(completed, 0.99f));
    if (loadStableFrames >= 2) { loadReadyT = loadT; loadShown = 1.f; g_ren.entBudgetMs = 2.5f; }
    // a research sortie: the preview has already streamed its airport, so the flight opens at once (no establishing shot)
    if (loadReadyT >= 0 && researchFlight && !headless) {
      screen = SCR_FLIGHT; dbgCam = false;
      if (!resAirborne) { camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15)); }
      return;
    }
  } else {
    // establishing shot: a slow orbit round the aircraft on the ground, or a camera flying alongside in the air
    float t = loadT - loadReadyT, size = std::max(plane.spec->span, plane.spec->fusLen);
    dbgCam = true; dbgFollow = false;
    if (plane.onGround) {
      float a = plane.heading() * DEG + 2.2f + t * 0.07f, R = size * 1.9f + 10.f;
      dbgCamPos = plane.pos + vec3(sinf(a) * R, R * 0.32f + 2.f, -cosf(a) * R);
      dbgCamLook = plane.pos + vec3(0, size * 0.1f, 0);
    } else {
      float R = size * 2.2f + 8.f;
      dbgCamPos = plane.pos + plane.right() * (R * 0.85f) + plane.forward() * (R * (0.7f - 0.04f * t)) + vec3(0, R * 0.18f, 0);
      dbgCamLook = plane.pos + plane.forward() * (size * 0.2f);
    }
    bool go = in.pressed[K_ENTER] || in.pressed[' '] || in.mPressed[0] || (in.buttonsPressed & PAD_A);
    if (go && t > 0.5f) {
      screen = SCR_FLIGHT; dbgCam = false;
      camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
      in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false; in.buttonsPressed &= ~PAD_A;
      g_audio.trigger(SFX_CLICK);
    }
  }
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) {   // back out to where the flight was chosen
    dbgCam = false; g_ren.entBudgetMs = 2.5f;
    if (freeFlight) { returnToFreeFlight(); return; }
    if (militaryFlight) { returnToMilitary(); return; }
    // a career flight that never left the loading screen: its attempt closes and its job waits at its stop, as after
    // an interrupted session (nothing is charged: no leg was flown); an unsaved close shows as pending and blocks launches
    if (career.attemptOpen && !isolatedFlight && !researchFlight)
      commit([](Career& k) { if (k.military.activeAttempt) { k.abandonMilitaryAttempt(); return; } k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
    screen = researchFlight ? SCR_RESEARCH : SCR_HUB;
    militaryFlight=false; hiveCombat.reset(1, combatWrap());
    researchFlight = false;   // (nothing of the cancelled session carries into the next flight)
  }
}

std::vector<std::pair<int, bool>> Game::prewarmItems(bool allCraft) {
  std::vector<std::pair<int, bool>> todo;
  for (int i = 0; i < kAircraftCount; i++) if (allCraft || !kAircraft[i].special) { todo.push_back({i, false}); todo.push_back({i, true}); }
  return todo;
}
int Game::prewarmHiveItems(bool allCraft) { return allCraft ? kEnemyCraftTypes : 0; }
std::string Game::prewarmLabel(int craft, bool inside, int stage) {
  const std::string what = std::string("the ") + kAircraft[craft].name + (inside ? "'s cockpit" : "'s body");
  switch (stage) {
    case 1: return "Loaded " + what + ", ready-built";
    case 2: return "Loaded " + what + " from this PC's cache";
    case 3: return "Building " + what + "  (once: kept for the next launch)";
    case 4: return std::string("Compiling the ") + kAircraft[craft].name + "'s shader programs  (once after an update)";
    default: return "Preparing " + what;
  }
}
void Game::prewarm(const std::function<void(float, const std::string&)>& progress, bool allCraft, LoadPacer* pace, int menuStep, const std::vector<int>* itemSteps, int stageStep) {
  if (screen != SCR_MENU) return;
  bool sync = g_ren.entSync;
  // (offscreen only for these frames: the intro may draw through the same UI path in between)
  auto frame = [&] { g_ren.setOffscreen(true); update(1.f / 60.f); render(); glFinish(); g_ren.setOffscreen(false); };
  // the tour's first place: every scenery chunk in range, the shadow maps and the terrain shadow
  g_ren.entSync = true;
  realTime = 3.f;
  if (pace && menuStep >= 0) pace->begin(menuStep);
  progress(pace ? pace->fraction() : 0.f, "Loading the menu's scenery and its terrain shadow");
  int firstPending = 0;
  for (int i = 0; !quit; i++) {
    realTime = 3.f; frame();
    firstPending = std::max(firstPending, g_ren.entPending);
    const bool ready = i >= 2 && g_ren.entPending == 0 && !loadingShadowPending();
    if (pace) {
      const float chunks = firstPending > 0 ? 1.f - float(g_ren.entPending) / firstPending : (i >= 2 ? 1.f : 0.f);
      pace->setSub(ready ? 1.f : std::min(0.95f, 0.8f * chunks + (loadingShadowPending() ? 0.f : 0.15f)));
      progress(pace->fraction(), fmt("Preparing menu scenery  (%d chunks remaining)%s", g_ren.entPending, loadingShadowPending() ? " / baking terrain lighting" : ""));
    }
    if (ready) break;
  }
  g_ren.entSync = sync;
  // every light aircraft's body (outside, and the cockpit's when that is in use; with allCraft the research jets' too):
  // a frame that wants one bakes it, or loads it from the cache
  const std::vector<std::pair<int, bool>> todo = prewarmItems(allCraft);
  for (size_t k = 0; k < todo.size() && !quit; k++) {
    prewarmCraft = todo[k].first; prewarmInside = todo[k].second;
    if (pace && itemSteps && k < itemSteps->size()) pace->begin((*itemSteps)[k]);
    // worded as it goes: where its body came from (ready-built, this PC's cache, or built), and its shader programs
    // compiling - after an update, the most of this step's time
    const std::string count = fmt("   %d of %d", (int)k + 1, (int)todo.size());
    bool saying = false;   // (the launch's progress draws a frame, which may compile a program of its own: not said again)
    auto say = [&](int stage) {
      if (saying) return;
      saying = true; progress(pace ? pace->fraction() : 0.35f + 0.65f * k / todo.size(), prewarmLabel(prewarmCraft, prewarmInside, stage) + count); saying = false;
    };
    say(0);
    g_ren.onBodyLoaded = [&](bool packaged) { say(packaged ? 1 : 2); };
    g_ren.onBakeStart = [&] { say(3); };
    g_ren.onMeshProgress = [&](float f,const std::string& task) {
      if(pace){pace->setSub(f);pace->task(task,1,1);}say(3);
    };
    g_ren.onMeshBatch = [&](int done,int all,const std::string& task) {if(pace)pace->task(task,float(done),float(all));};
    g_onProgramBuild = [&](const std::string&) { say(4); };
    realTime = 3.f; frame();   // bakes it at the end of the frame, if this view uses one
    if(pace)pace->setSub(1.f);
    g_ren.onBodyLoaded = nullptr; g_ren.onBakeStart = nullptr; g_ren.onMeshProgress = nullptr; g_ren.onMeshBatch = nullptr; g_onProgramBuild = nullptr;
  }
  prewarmCraft = -1; prewarmInside = false;
  // the preview stage (previewStage): the stand's scenery and the circuit's over it, and its terrain shadow, built now
  // and kept, so the hangar, Free Flight, the research terminal and the service board open on it with nothing to build
  if (previewStage().ok) {
    if (pace && stageStep >= 0) pace->begin(stageStep);
    g_ren.entSync = true;
    int pend0 = 0; const int quarters = 5;
    for (prewarmStage = 1; prewarmStage <= quarters && !quit; prewarmStage++) {
      for (int i = 0; !quit; i++) {
        realTime = 3.f; frame();
        pend0 = std::max(pend0, g_ren.entPending);
        const bool ready = i >= 1 && g_ren.entPending == 0 && (prewarmStage > 1 || !loadingShadowPending());
        if (pace) {
          const float chunks = pend0 > 0 ? 1.f - float(g_ren.entPending) / pend0 : (i >= 1 ? 1.f : 0.f);
          pace->setSub(std::min(0.98f, (prewarmStage - 1 + (ready ? 1.f : 0.8f * chunks)) / quarters));
          progress(pace->fraction(), fmt("Preparing the preview stage  (%d chunks remaining)%s", g_ren.entPending, prewarmStage == 1 && loadingShadowPending() ? " / baking its terrain lighting" : ""));
        }
        if (ready) break;
      }
    }
    prewarmStage = 0;
    g_ren.entSync = sync;
    if (pace) pace->setSub(1.f);
  }
  // the hive's craft (the XR-40's research sorties and Defense Command fly against them): built once after an update,
  // then read from this PC's cache - here, under the intro, so neither launch has one to build behind its loading screen
  // (the first research flight after an update built all four there, seconds each)
  const int hiveN = prewarmHiveItems(allCraft);
  for (int k = 0; k < hiveN && !quit; k++) {
    const size_t step = todo.size() + k;
    if (pace && itemSteps && step < itemSteps->size()) pace->begin((*itemSteps)[step]);
    progress(pace ? pace->fraction() : 1.f, fmt("Preparing the hive craft   %d of %d", k + 1, hiveN));
    g_ren.onBakeStart = [&] { progress(pace ? pace->fraction() : 1.f, fmt("Building the hive craft  (once after an update: kept for the next launch)   %d of %d", k + 1, hiveN)); };
    g_ren.warmEnemyType(k);
    g_ren.onBakeStart = nullptr;
    if (pace) pace->setSub(1.f);
  }
  realTime = 0.f;
  if (!quit) {
    if (pace) pace->end();
    progress(pace ? pace->fraction() : 1.f, "Ready");
  }
}

void Game::renderLaunchCard() {
  // No buildFrame, scene streaming, mesh baking, collision queries or picture decoding on this path.
  // CPU-only tests use the same card builder without issuing GL commands.
  if (g_ren.ok) g_ren.clearScreen();
  g_ren.uiBegin();
  drawLoading();
  if (g_ren.ok) g_ren.uiEnd();
  if (pendingMilitaryLaunch) militaryLaunchFrameDrawn = true;
  if (pendingLaunch) launchFrameDrawn = true;
}

void Game::framePresented() {
  if (screen == SCR_LOADING && pendingMilitaryLaunch && militaryLaunchFrameDrawn)
    militaryLaunchFramePresented = true;
  if (screen == SCR_LOADING && pendingLaunch && launchFrameDrawn)
    launchFramePresented = true;
  militaryLaunchFrameDrawn = launchFrameDrawn = false;
}

void Game::loadLogBegin() {
  if (headless || saveDir.empty()) return;
  loadLog.clear(); loadLogging = true; loadShownInside = 0; loadLogStart = loadLogLast = std::chrono::steady_clock::now();
}
void Game::loadLogTick() {
  const auto now = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(now - loadLogLast).count();
  loadLogLast = now;
  if (screen == SCR_LOADING && loadLog.size() < 4000) {
    LoadLogFrame L{ms, launchCardPending(), launchSetUpShown, loadFrames, std::max(0, g_ren.entPending), loadViewsStep, loadingShadowPending(), loadWarmStep, loadShownInside};
    L.upd = lastUpdate.total; L.rnd = lastRenderMs; L.baked = lastRenderBaked; L.compiled = lastRenderCompiled; L.compileMs = lastRenderCompileMs;
    for (const auto& p : lastUpdate.parts) if (p.second >= 20.0) L.parts += fmt("%s%s %.0f", L.parts.empty() ? "" : ", ", p.first, p.second);
    loadLog.push_back(std::move(L));
    loadShownInside = 0;
    return;
  }
  loadLogging = false;
  if (!loadLog.empty()) {   // (the last frame of it - the flight's first - with what its update and render did, as any frame's)
    LoadLogFrame L{ms, false, false, loadFrames, std::max(0, g_ren.entPending), loadViewsStep, false, loadWarmStep, loadShownInside};
    L.upd = lastUpdate.total; L.rnd = lastRenderMs; L.baked = lastRenderBaked; L.compiled = lastRenderCompiled; L.compileMs = lastRenderCompileMs;
    for (const auto& p : lastUpdate.parts) if (p.second >= 20.0) L.parts += fmt("%s%s %.0f", L.parts.empty() ? "" : ", ", p.first, p.second);
    loadLog.push_back(std::move(L));
  }
  loadShownInside = 0;
  FILE* f = fopen((saveDir + "/loading.log").c_str(), "w");
  if (!f) return;
  const double total = std::chrono::duration<double>(now - loadLogStart).count();
  fprintf(f, "Somnium Volandi - the loading screen of the last launch: %s, %s\n%.2f s from the card to %s, %zu frames\n\n",
          contract.title.c_str(), plane.spec ? plane.spec->name : "-", total, screen == SCR_FLIGHT ? "the flight" : "leaving it", loadLog.size());
  fprintf(f, "frame       ms  what was on the screen\n");
  for (size_t i = 0; i < loadLog.size(); i++) {
    const LoadLogFrame& L = loadLog[i];
    if (L.card) fprintf(f, "%5zu %8.1f  the card (the flight not set up yet)", i, L.ms);
    else fprintf(f, "%5zu %8.1f  %s: scene frame %d, warmed %d of %d, %d scenery chunks to make, views drawn %d of 2%s%s", i, L.ms, L.setUp ? "set up, the loading screen alone" : i + 1 == loadLog.size() ? (screen == SCR_FLIGHT ? "the last, the flight's first frame" : "the last") : "loading",
                 L.frames, std::min(L.warm, loadWarmItems()), loadWarmItems(), L.pending, std::min(L.views, 2), L.shadows ? ", terrain shadows to do" : "",
                 L.shown ? fmt(", shown %d times from inside a bake", L.shown).c_str() : "");
    // (a frame over 50 ms: its update and render, and what each did)
    if (L.ms > 50.0) {
      fprintf(f, "  [update %.0f ms%s%s; render %.0f ms", L.upd, L.parts.empty() ? "" : ": ", L.parts.c_str(), L.rnd);
      if (L.baked) fprintf(f, ", %d bodies baked", L.baked);
      if (L.compiled) fprintf(f, ", %d programs compiled (%.0f ms)", L.compiled, L.compileMs);
      fprintf(f, "]");
    }
    fprintf(f, "\n");
  }
  fclose(f);
}

bool Game::queueLaunch(const Contract& card, int spec, LaunchKind kind, std::function<void()> begin) {
  if (headless || launchRunning) return false;
  if (launchCardPending()) return true;   // (one launch at a time: a second press while the card is up does nothing)
  loadLogBegin();
  PendingLaunch L; L.card = card; L.spec = spec; L.kind = kind; L.from = screen; L.paused = paused; L.begin = std::move(begin);
  pendingLaunch = std::move(L);
  launchFrameDrawn = launchFramePresented = false;
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadFrames = 0; loadViewsDrawn = false; loadViewsStep = 0; loadWarmStep = 0; loadWarmAt = -1;
  loadStableFrames = 0; loadObservedFrame = -1; loadMap = loadBackdrop = false; launchSetUpShown = false;
  screen = SCR_LOADING;
  in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false;
  in.buttonsPressed &= ~(PAD_A | PAD_START);
  return true;
}
void Game::cancelQueuedLaunch() {
  if (!pendingLaunch) return;
  screen = pendingLaunch->from; paused = pendingLaunch->paused;
  pendingLaunch.reset();
  launchFrameDrawn = launchFramePresented = false;
}
