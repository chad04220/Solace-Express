// Somnium Volandi - the game: its launch, each frame's update and render
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
#include "jobs.h"

void Game::init(bool buildWorld, const std::function<void(float, const std::string&)>& progress) {
  const float units = float(kNumAircraft + 4);
  auto report = [&](int done, const std::string& detail) { if (progress) progress(done / units, detail); };
  report(0, "Preparing career systems");
  if (buildWorld) g_world.build();
  buildStory();
  loadSettings();
  applyUiPalette();
  wantPacing = true;   // (the frame-rate target from the settings)
  loadStations();
  report(1, "Settings, story and radio stations ready");
  {   // every type's performance, learned by flying it, on threads side by side (the job board needs the career ones now);
    // kept with the cache, so a launch after the first reads them
    const std::string pf = cacheDir.empty() ? std::string() : joinPath(cacheDir, "perf.bin");
    if (pf.empty() || !Plane::perfLoad(pf, buildStamp)) {
      std::vector<std::thread> th;
      std::atomic<int> completed{0};
      const std::vector<const AircraftSpec*> airframes = Plane::perfAirframes();   // (the career types, and each on floats where they fit)
      for (const AircraftSpec* a : airframes) th.emplace_back([a, &completed] { Plane::perf(a); completed.fetch_add(1); });
      if (progress) {
        while (completed.load() < (int)airframes.size()) {
          const int n = completed.load() * kNumAircraft / (int)airframes.size();
          report(1 + n, fmt("Flight performance: %d / %d airframes checked", completed.load(), (int)airframes.size()));
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
      }
      for (auto& t : th) t.join();
      if (!pf.empty()) Plane::perfSave(pf, buildStamp);
    }
  }
  report(1 + kNumAircraft, "Aircraft performance data ready");
  career.newGame();
  std::string sav = joinPath(saveDir, "career.sav");
  hasSave = !diskless && career.load(sav);   // (a tool's flights never touch the player's career: a fresh one, in memory)
  if (!hasSave && !diskless && career.load(sav + ".bak")) { hasSave = true; toast("Career save was damaged: restored the previous save"); }
  if (hasSave && career.attemptOpen) {   // the last session ended inside a flight: the career stands as it was before it
    toast(fmt("Your last flight was interrupted: you are back at %s", careerPlaceName().c_str()), vec3(1.f, 0.8f, 0.4f));
    // an accepted job whose leg never ended waits where the leg began (the hub's recovery card offers it again)
    commit([](Career& k) { if (k.military.activeAttempt) { k.abandonMilitaryAttempt(); return; } k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
  }
  if (!hasSave) {
    career.newGame();
    // keep an unreadable save aside rather than overwriting it with a new career
    if (FILE* f = diskless ? nullptr : fopen(sav.c_str(), "r")) { fclose(f); remove((sav + ".damaged").c_str()); rename(sav.c_str(), (sav + ".damaged").c_str()); toast("Career save couldn't be read: kept as career.sav.damaged"); }
  }
  loadProfile();
  report(2 + kNumAircraft, "Career and recovery checks complete");
  radio.init();
  radio.setVolume(radioLevel()); radioSentVol = radioLevel();
  if (!diskless) g_music.start((float)g_audio.sampleRate);   // (the game's own score: music.h)
  report(3 + kNumAircraft, "Radio initialized");
  atc.load(assetDir + "/voice");   // tower voices (absent: the towers stay silent)
  camQ = quat();
  report(4 + kNumAircraft, "Voice library indexed");
}

void Game::initHeadless() {
  headless = true;
  // Multi-scene diagnostics can start a fresh Game while another generated region
  // is resident: a fresh career there (its board drawn when the hub opens).
  if (g_world.procedural) { career = Career{}; career.region = g_world.regionId; career.location = 0; }
  else career.newGame();
  resetMenus();
  set.resMode = 0; failuresArmed = false; g_ren.foveation = 0.f; g_ren.prepassAutoOn = false;
}   // tools and tests render at the full resolution; nothing breaks unless the test breaks it

void Game::shutdown() {
  regionalSurface::clearFlightContext();
  retireEarthPrefetch();
  jobs::drain(earthRetireJob, "retiring the Earth prefetch at shutdown");
  jobs::drain(regionJob, "the region build at shutdown");
  g_sceneryProfiles.reset();
  saveSettings(); radio.shutdown(); g_music.own(false); g_music.stop(); g_ren.releaseLoadingPreview();
}

// ------------------------------------------------------------------ main update / render
std::string Game::slowestUpdateText() const {
  std::string t = fmt("%.1f ms", slowestUpdate.total);
  for (const auto& p : slowestUpdate.parts) t += fmt("; %s %.1f", p.first, p.second);
  return t;
}
void Game::resetSlowestUpdate() { slowestUpdate = UpdateParts(); }

void Game::update(float dt) {
  // (the update timed whole and part by part: the slowest kept, for the benchmark to say what a hitch was spent on)
  // (an aircraft type's performance learned in an update is named as a part of it: that's a flight model's test sorties)
  struct Whole { Game& g; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~Whole() {
      Plane::perfNoting(false);
      for (const auto& n : Plane::perfNoted()) g.updParts.parts.push_back({learnedPart(n.first), n.second});
      g.updParts.total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (g.updParts.total > g.slowestUpdate.total) g.slowestUpdate = g.updParts;
      if (g.loadLogging) g.lastUpdate = g.updParts;
    }
    static const char* learnedPart(int spec) {
      static std::string names[kAircraftCount];
      if (names[spec].empty()) names[spec] = fmt("performance learned: %s", kAircraft[spec].name);
      return names[spec].c_str();
    } } whole{*this};
  updParts.parts.clear();
  Plane::perfNoting(true);
  if (loadLogging) loadLogTick();
  g_ren.beginRegionUploadFrame(); // one shared detail + horizon GPU budget per displayed frame
  if (screen != SCR_FLIGHT) { rumbleLo = rumbleHi = 0.f; shakeTrauma = shakeBuffet = 0.f; }   // (the motors and the shake are the flight's)
  if (screen != SCR_REGION_LOADING) { PartTimer pt(*this, "career world"); const std::string want = screenWorld(); if (!want.empty()) loadCareerWorld(want, screen); }
  if (screen == SCR_REGION_LOADING) {
    realTime += std::min(dt, .1f); lastDt = std::min(dt, .05f);
    if (in.pressed[K_F11]) wantFullscreenToggle = true;
    gamepadMenus(lastDt); focusNavigate(); radio.poll(); updateMusic(); updateSteam(dt);
    updateRegionLoading(std::min(dt, .25f));
    return;
  }
  { PartTimer pt(*this, "earth surface"); updateEarthSurfaceContext(); } // establish the collision snapshot protection before physics reads World
  if (pendingCareer && (screen == SCR_HUB || screen == SCR_DEBRIEF || screen == SCR_MENU)) {   // a settlement or purchase that couldn't be saved: tried again now and then
    retryT += dt;
    if (retryT > 3.f) { retryT = 0; PartTimer pt(*this, "career save retry"); retryCommit(); }
  }
  { PartTimer pt(*this, "earth install"); updateEarthInstall(); }
  // the render resolution: native, a fixed scale, or adjusted to hold the frame-rate target (Settings)
  fpsAvg = lerpf(fpsAvg, dt, 1.f - expf(-dt * 3.f));   // (a third of a second, whatever the frame rate)
  maxFrameWin = std::max(maxFrameWin, dt); maxFrameT += dt;
  if (maxFrameT >= 1.f) { maxFrameMs = maxFrameWin * 1000.f; maxFrameWin = 0; maxFrameT = 0; }
  if (!headless && g_ren.ok) {   // render resolution: native, a fixed scale, or Auto: held to the frame-rate target
    static const float fixedScale[5] = {1.f, 1.f, 0.85f, 0.75f, 0.67f};
    float want = fixedScale[std::clamp(set.resMode, 0, 4)];
    // Auto (quality_governor.h): the screen's edges coarser first where the card has foveated shading, then the render
    // scale, each step kept only if the GPU's frame time shows it paid; back the other way when there is room
    autoRes.vrs = set.foveate && g_ren.vrsAvailable;
    if (set.resMode == 1 && !photo.on) {
      // (the GPU budget of the frame-rate target, with 8% to spare)
      autoRes.frame(dt, g_ren.gpuMs, 1000.f / effectiveHz() * 0.92f);
      g_ren.foveation = autoRes.foveation;
      want = autoRes.scale;
    } else { autoRes.reset(); g_ren.foveation = 0.f; }
    if (photo.on) want = 1.f;   // (photo mode: every pixel at the full resolution, all of it fully shaded)
    want = floorf(want * 20.f + 0.5f) / 20.f;   // 5% steps: the targets are rebuilt only when it changes
    if (fabsf(g_ren.renderScale - want) > 1e-3f) g_ren.setRenderScale(want);
  }
  // the flight sim replays the whole frame time in steps of at most 50 ms, so the simulation keeps pace with the
  // clock down to 4 fps; a longer hitch (window drag, alt-tab) is dropped rather than replayed. UI and animation
  // timers just use the clamped step.
  float simDt = std::min(dt, 0.25f);
  dt = std::min(dt, 0.05f);
  lastDt = dt;
  realTime += dt;
  updateBindCapture(dt);
  armInputs();
  // gamepad: both bumpers held together for a second hides the whole flight UI; again brings it back (not while the
  // XR-40's weapons are armed: the bumpers are its triggers then; the pause menu has the same switch)
  bool bumpersFree = !(screen == SCR_FLIGHT && plane.spec && plane.spec->special == 2 && somnus.armed) && !photo.on;
  if (in.pad && bumpersFree && (in.buttons & PAD_LB) && (in.buttons & PAD_RB)) {
    bumperHold += dt;
    if (bumperHold >= 1.f && !bumperFired) { bumperFired = true; uiHidden = !uiHidden; g_audio.trigger(SFX_CLICK); }
  } else { bumperHold = 0; bumperFired = false; }
  for (auto& t : toasts) t.t += dt;
  while (!toasts.empty() && toasts.front().t > 5.f) toasts.erase(toasts.begin());
  hubMsgTime = std::max(0.f, hubMsgTime - dt);
  if (in.pressed[K_F11]) wantFullscreenToggle = true;
  if (in.pressed[0x72]) showPerf = !showPerf;   // F3
  gamepadMenus(dt);
  focusNavigate();
  if (screen == SCR_HUB && !(hubTab == TAB_CONTRACTS && hubList == 2) && in.pad && !((in.buttons & PAD_LB) && (in.buttons & PAD_RB))) {   // the shoulders step through the hub's tabs
    const int nTabs = TAB_SETTINGS + 1;
    if (in.buttonsPressed & PAD_RB) { hubTab = (hubTab + 1) % nTabs; g_audio.trigger(SFX_CLICK); }
    if (in.buttonsPressed & PAD_LB) { hubTab = (hubTab + nTabs - 1) % nTabs; g_audio.trigger(SFX_CLICK); }
  }
  bool padCombo = in.pad && (in.buttons & PAD_LS) && (in.buttons & PAD_RS) && (in.buttonsPressed & (PAD_LS | PAD_RS));
  if (screen == SCR_MENU && ((in.down['U'] && in.down['I'] && (in.pressed['U'] || in.pressed['I'])) || padCombo)) {
    screen = SCR_RESEARCH; resOpened = realTime; g_audio.trigger(SFX_BEEP); award("classified");
    resWarm = true; resWarmFrames = 0; resBakeSeen = g_ren.bakeCount;   // the terminal's boot screen shows at once; the craft and its airport warm behind it
    resWork = 0.f; resPend0 = 0;
  }
  if (screen == SCR_RESEARCH && resWarm) {   // warming: each craft drawn once over the preview stage, outside and from the cockpit (the launch built their bodies: these frames verify them resident, and bake what a tool's run has not); done when a frame bakes nothing and nothing is pending (sceneWork)
    g_ren.entBudgetMs = 14.f;
    const SceneWork work = sceneWork(resBakeSeen);
    // how much of it is done, for the boot screen's bar and its scans (sceneProgress, as the flight's loading screen):
    // the craft views drawn (render() has drawn resWarmFrames frames, the first the boot screen alone, then one view
    // each and the selected craft until the check below), the scenery chunks made out of the most the stage has waited
    // on (counted from the first frame of it), and its terrain lighting
    const int nViews = 4 * resCraftCount() + 3, f = resWarmFrames;
    const bool site = f >= 2;
    if (site) resPend0 = std::max(resPend0, work.pending);
    const float views = clampf((f - 1.f) / nViews, 0.f, 1.f);
    resWork = std::max(resWork, sceneProgress(views, site ? work.pending : std::max(1, resPend0), resPend0, site && !work.shadow, f > nViews && work.ready() ? 1.f : 0.f));
    if (resWarmFrames > nViews && work.ready()) { resWarm = false; resWarmCraft = -1; resWarmCk = false; g_ren.entBudgetMs = 2.5f; resWork = 1.f; }
  }
  if (screen == SCR_FLIGHT && (actKeyP(ACT_RADIO) || actJoyP(ACT_RADIO) || (!paused && actPadP(ACT_RADIO)))) showRadio = !showRadio;
  { PartTimer pt(*this, "radio"); radio.poll(); }
  { PartTimer pt(*this, "music"); updateMusic(); }
  { PartTimer pt(*this, "steam"); updateSteam(dt); }
  if (screen == SCR_HUB) { static float careerCheckT = 0; if ((careerCheckT += dt) > 1.f) { careerCheckT = 0; profileCareerCheck(); } }   // (licences, the fleet, the airline: achievements)
  for (size_t i = 0; i < achShow.size() && i < 1; i++) achShow[i].t += dt;   // (the banner on show)
  if (!achShow.empty() && achShow.front().t > 4.5f) achShow.erase(achShow.begin());
  if (screen == SCR_FLIGHT) {
    if (photo.on) updatePhoto(dt);   // (photo mode: the flight stays paused under it, and the keys are its own)
    else if (actPressed(ACT_PHOTO) && !settingsFromPause && !freeRegionMap && !freeEarthPicker) enterPhoto();
    else if (freeEarthPicker && in.pressed[K_ESC]) {}   // (the airport picker's own: its query cleared, else it closes)
    else if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START)) {
      if (freeRegionMap) freeRegionMap = false;
      else if (freeEarthPicker) freeEarthPicker = false;
      else if (showMap) showMap = false; else if (showRadio) showRadio = false;
      else if (paused && settingsFromPause && !(in.buttonsPressed & PAD_START)) settingsFromPause = false;   // B / Esc: back to the pause menu
      else { paused = !paused; settingsFromPause = false; if (paused && !atcF.lastCall.empty()) atcF.lastBeforePause = true; }
    }
    if (!paused && actPressed(ACT_MAP)) { if (plane.fail.avionicsDark() && !showMap) toast("GPS dark - battery flat", vec3(1, 0.45f, 0.35f)); else { showMap = !showMap; g_audio.trigger(SFX_CLICK); } }
    if (showMap && !paused) {   // GPS open: Tab / D-pad pick the autoland airport, Enter / A engages the autopilot to it
      if (in.pressed[K_TAB] || (in.buttonsPressed & PAD_RIGHT)) cycleApDest(in.down[K_SHIFT] ? -1 : 1);
      if (in.buttonsPressed & PAD_LEFT) cycleApDest(-1);
      if ((in.pressed[K_ENTER] || (in.buttonsPressed & PAD_A)) && apDest >= 0 && !plane.onGround) engageAutopilot();
    } else if (!photo.on && actPressed(ACT_MINIMAP)) { showMinimap = !showMinimap; toast(showMinimap ? "Minimap shown" : "Minimap hidden"); }
    if (!photo.on && actPressed(ACT_HUD)) { hudOn = !hudOn; set.hudCam[std::clamp(camMode, 0, 3)] = hudOn; }
    if (!paused) {
      int nSim = std::max(1, (int)ceilf(simDt / 0.05f - 1e-4f));
      Input frameIn; bool saved = false;   // key presses act once: the catch-up steps see the held state without the press edges
      for (int k = 0; k < nSim && screen == SCR_FLIGHT && !paused; k++) {
        float h = simDt / nSim;
        { PartTimer pt(*this, "flight"); updateFlight(h); }
        if (screen == SCR_FLIGHT) { PartTimer pt(*this, "particles"); updateParticles(h * timeAccel); }
        if (k == 0 && nSim > 1) { frameIn = in; saved = true; in.endFrame(); }
      }
      if (saved) in = frameIn;
      if (screen == SCR_FLIGHT) { PartTimer pt(*this, "camera"); updateCamera(dt); }
      if (screen == SCR_FLIGHT && !crashed) {   // the finals the autopilot may plan next, profiled ahead (Plane::wantFinals): the GPS pick, the field it flies to, the job's
        if (apDest >= 0) Plane::wantFinals(apDest);
        if (plane.apAirport >= 0) Plane::wantFinals(plane.apAirport);
        if (plane.apHoldFor >= 0) Plane::wantFinals(plane.apHoldFor);
        if (!haulPending() && contract.to >= 0 && contract.to < (int)g_world.airports.size()) Plane::wantFinals(contract.to);
      }
      if (plane.spec && wreck.empty()) {
        float rps = plane.spec->engineType == ENG_TURBOPROP ? plane.rpm / 60.f : plane.rpm / 60.f;
        propAngle = fmodf(propAngle + rps * 2 * PI * dt * (plane.rpm < 400 ? 1.f : 0.0f) + (plane.rpm >= 400 ? dt * 3.f : 0.f), 2 * PI * 100);
      }
    }
  } else if (screen == SCR_LOADING) {
    PartTimer pt(*this, "loading"); updateLoading(dt);
  } else if (screen == SCR_DEBRIEF) {
    if (crashed) { PartTimer pt(*this, "debrief wreck"); crashTimer += dt; updateWreck(dt); updateParticles(dt); }   // the wreck keeps settling/sinking behind the results
  } else {
    // one light and sky for every menu past the main menu's tour (the hub, the hangar, Free Flight, the research
    // terminal, the service board): the preview stage's terrain shadow is baked once for it, at the launch, and no
    // cut between them bakes another (the research terminal's sortie keeps its own time and weather for the flight)
    wx = Weather(); wx.cloudCover = 0.35f; wx.cloudBase = 1500; wx.visibility = 45000; wx.windSpeed = 4;
    timeOfDay = screen == SCR_MENU && prewarmStage == 0 ? menuShot(realTime).tod : kStageTimeOfDay;
    if (screen == SCR_MENU && prewarmStage == 0) wx.cloudCover = menuShot(realTime).cloud;
    cloudOff = cloudOff + vec2(dt * 8.f, dt * 3.f);
    wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
  }
  if (screen == SCR_REGION_LOADING) return;
  { PartTimer pt(*this, "earth flight"); updateEarthFlight(); } // once per displayed frame, including paused background preparation
  { PartTimer pt(*this, "comms"); updateComms(dt); }
  { PartTimer pt(*this, "audio"); feedAudio(); }
}

void Game::render() {
  // (timed, for the loading log: how long the frame's render took, and the bodies baked and programs compiled in it)
  struct Timed { Game& g; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now(); int bake0 = g_ren.bakeCount, miss0 = g_shaderCacheMisses; long long us0 = g_shaderCompileUs;
    ~Timed() { g.lastRenderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); g.lastRenderBaked = g_ren.bakeCount - bake0;
               g.lastRenderCompiled = g_shaderCacheMisses - miss0; g.lastRenderCompileMs = (g_shaderCompileUs - us0) / 1000.0; } } timed{*this};
  if (screen != SCR_REGION_LOADING) { const std::string want = screenWorld(); if (!want.empty()) loadCareerWorld(want, screen); }
  if (screen == SCR_REGION_LOADING) {
    g_ren.clearScreen(); g_ren.uiBegin(); drawRegionLoading(); drawPadCursor(); g_ren.uiEnd();
    return;
  }
  if (screen == SCR_LOADING && launchCardPending()) { renderLaunchCard(); return; }
  if (screen == SCR_LOADING && launchSetUpShown) { launchSetUpShown = false; renderLaunchCard(); return; }
  // (a flight set up without the card - restarted in place, a tool's: its loading screen alone first too, so its
  // backdrop is in hand before the scene's first frame, the one most likely to bake, can show it from inside a bake)
  if (screen == SCR_LOADING && !loading.backdrop && !headless) { renderLaunchCard(); return; }
  // every menu draws its scene live (the main menu's tour, the hub's airport, the research terminal's preview): the
  // scenery a flight opens with streams in while the player chooses. vid: a full-screen picture instead of the scene
  // (the research terminal's boot screen)
  unsigned vid = 0;
  FrameParams fp;
  // the research terminal warming up: its first frame is the boot screen alone (nothing of the scene, so it is on the
  // screen the instant the combo is held); the frames after it draw the preview, which streams the airport and bakes
  // the craft's shells - during a bake the renderer calls back and the boot screen is drawn and shown from inside it
  if (screen == SCR_RESEARCH && resWarm) {
    if (resWarmFrames == 0) vid = 1;
    resWarmFrames++;
    // every research craft, outside and from the cockpit, one a frame: each one's shells bake now behind the boot
    // screen, so picking one in the terminal never stalls on a bake (the XR-40's, with the most moving parts, froze
    // the game for seconds when it was first selected); then all of them again, each now drawn with its baked body:
    // the first draw with a type's own mesh build costs the driver its compile (seconds for the research craft's,
    // the stall picking one - the XR-50's - still had), and in the first round each frame drew a body still baking,
    // so that build first ran when the craft was picked. Then the selected craft again
    const int item = resWarmFrames - 2, nItems = 4 * resCraftCount();
    resWarmCraft = item >= 0 && item < nItems ? resCraftAt((item % (2 * resCraftCount())) / 2) : -1;
    resWarmCk = item >= 0 && item < nItems && (item & 1);
    resLoadTask.clear();resTaskDone=resTaskUnits=0;
    g_ren.onMeshProgress=[this](float f,const std::string& task){
      const float views=clampf((std::max(0,resWarmFrames-2)+f)/(4.f*resCraftCount()+3.f),0.f,1.f);
      resWork=std::max(resWork,sceneProgress(views,resPend0>0?std::max(0,g_ren.entPending):1,resPend0,resWarmFrames>=2&&!loadingShadowPending(),0.f));
      resLoadTask=task;resTaskDone=resTaskUnits=1;
    };
    g_ren.onMeshBatch=[this](int done,int total,const std::string& task){resLoadTask=task;resTaskDone=done;resTaskUnits=total;};
    g_ren.bakeYield = [this] {
      GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
      // (the sequence runs on the real clock: a frame from inside a bake moves it on by the time that has passed)
      static auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      realTime += std::min(0.1f, std::chrono::duration<float>(now - last).count()); last = now;
      g_ren.clearScreen();
      FrameParams f0; g_ren.uiBegin(); drawResearch(f0); g_ren.uiEnd();
      if (platformPresent) platformPresent();
      glUseProgram((GLuint)prog);
    };
  } else if (loadingRedrawsInsideBakes()) {
    // the loading screen, drawn and shown from inside a long bake or warm-up (the hive craft, the XR-40's cockpit):
    // on its own the screen would hold still until the flight opened. (Its backdrop - the shipped picture, or the
    // aerial map - was decoded or rendered by a frame before: never from inside a bake. It waited for the map alone,
    // which only a field without a picture renders: every flight in the islands has one, so their screens stood
    // still through every bake)
    g_ren.bakeYield = [this] {
      GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
      static auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count()); last = now;
      realTime += dt; loading.t += dt;
      g_ren.clearScreen();
      g_ren.uiBegin(); drawLoading(); g_ren.uiEnd();
      platformPresent(); ++loadShownInside;
      glUseProgram((GLuint)prog);
    };
  } else if (g_ren.bakeYield && screen != SCR_RESEARCH) g_ren.bakeYield = nullptr;
  if (!vid) {
    // The loading screen, under its card (and under the frame drawn after them): the flight's views once each - inside
    // and out, and in the XR-40 a store of each kind and a young blast - before it opens. Building a body or linking a
    // program isn't all of it: drivers finish a program on its first draw. The first look inside the XR-40 took 90 s
    // on the software renderer, each look after it under one; its first bomb seen from outside as long.
    if (screen == SCR_LOADING && !headless && loading.readyT < 0 && loading.frames >= 2 && loading.warmStep >= loadWarmItems() && !loading.viewsDrawn && plane.spec && !crashed) {
      const int cm = camMode, baked = g_ren.bakeCount; const vec3 cp = camPos;
      const float size = std::max(plane.spec->span, plane.spec->fusLen);
      // (one a frame, the loading screen drawn and shown between them; the view the flight opens with last: the frame
      // after cuts from the other)
      const int order[2] = {cm == 1 ? 1 : 0, cm == 1 ? 0 : 1};
      for (int view : {order[std::min(loading.viewsStep, 1)]}) {
        camMode = view;
        camPos = view == 1 ? plane.pos + plane.q.rotate(kModels[typeOf(plane.spec)].eye)
                           : cm != 1 ? cp : plane.pos + plane.q.rotate(vec3(0, size * 0.3f, size * 1.4f));
        FrameParams w = buildFrame();
        if (plane.spec->special == 2) {
          const vec3 ahead = -w.camBack;
          w.fx.bombs = 3;
          for (int k = 0; k < 3; k++) {   // (kinds: 0 plasma, 1 penetrator, 2 EMP)
            const vec3 at = w.camPos + ahead * 14.f + w.camRight * ((k - 1) * 2.f);
            float* o = w.fx.bomb[k]; o[0] = at.x; o[1] = at.y; o[2] = at.z; o[3] = .29f;
            float* st = w.fx.bombStyle[k]; st[0] = st[1] = st[2] = 1.f; st[3] = (float)k;
            const float rot[9] = {w.camRight.x, w.camRight.y, w.camRight.z, w.camUp.x, w.camUp.y, w.camUp.z, w.camBack.x, w.camBack.y, w.camBack.z};
            std::copy(rot, rot + 9, w.fx.bombRot[k]);
          }
          const vec3 at = w.camPos + ahead * 400.f;
          w.fx.blasts = 1; float* o = w.fx.blast[0]; o[0] = at.x; o[1] = at.y; o[2] = at.z; o[3] = 100.f;
          float* I = w.fx.blastI[0]; I[0] = 0.2f; I[1] = 1.f; I[2] = I[3] = 0.f;
        }
        std::vector<SpriteVert> a, b;
        buildSprites(w, a, b);
        g_ren.renderScene(w, a, b);
      }
      camMode = cm; camPos = cp;
      g_ren.dropHistory();
      if (g_ren.bakeCount == baked) loading.viewsStep++;   // (a body built at its end was not drawn yet: again, next frame)
      loading.viewsDrawn = loading.viewsStep >= 2;
    }
    fp = buildFrame();
    std::vector<SpriteVert> a, b;
    if (!fp.hangarPreview) {
      buildSprites(fp, a, b);
      const size_t own = a.size(); if (!g_world.procedural) volcano::append(fp, g_ren.quality, a, b);
      if (a.size() != own) sortAlphaQuads(a, fp.camPos);   // (only with the plume in view: the rest comes sorted)
    }
    g_ren.renderScene(fp, a, b);
    // The current, assigned aircraft is already rendered. Retain that clean scene before menu text or
    // panels can cover it, so Deploy can show the same aircraft instantly without another render/decode.
    if (screen == SCR_HUB && hubTab == TAB_CONTRACTS && hubList == 2)
      g_ren.retainLoadingPreview(fp, militarySelection);
    if (screen == SCR_LOADING && loading.readyT < 0) ++loading.frames;
  } else g_ren.clearScreen();
  if(screen==SCR_RESEARCH){g_ren.onMeshProgress=nullptr;g_ren.onMeshBatch=nullptr;}
  if (screen == SCR_LOADING) g_ren.bakeYield = nullptr;   // (only this frame's scene: an update never draws the loading screen)
  if (screen == SCR_RESEARCH && !resWarm) g_ren.bakeYield = nullptr;
  if (sceneOnly) return;
  if (photo.on && photo.capture && screen == SCR_FLIGHT) {   // (the shot: the scene just drawn, before any of the UI)
    photo.capture = 0;
    photo.last = savePhoto(); photo.savedT = realTime;
    if (!photo.last.empty()) g_audio.trigger(SFX_CHIME, 0.5f);
  }
  g_ren.uiBegin();
  if (vid > 1) {   // cover the window, cropping the 16:9 picture as needed
    float W = (float)g_ren.W, H = (float)g_ren.H, ar = 16.f / 9.f, sa = W / H;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (sa > ar) { float k = ar / sa; v0 = 0.5f - 0.5f * k; v1 = 0.5f + 0.5f * k; } else { float k = sa / ar; u0 = 0.5f - 0.5f * k; u1 = 0.5f + 0.5f * k; }
    g_ren.image(vid, 0, 0, W, H, u0, v0, u1, v1, 1.f);
  }
  uiDt = clampf(realTime - uiLastT, 0.f, 0.1f); uiLastT = realTime;
  switch (screen) {
    case SCR_MENU: drawMenu(); break;
    case SCR_FREE_FLIGHT: drawFreeFlightSetup(fp); break;
    case SCR_HUB: drawHub(); break;
    case SCR_FLIGHT:
      if (photo.on) { drawPhotoPanel(); break; }   // (photo mode: its own panel, or nothing)
      if (!uiHidden || paused || showMap) {   // (the HUD under the g-force lens, flushed on its own: the map and the pause menu stay clear of it)
        g_ren.flushUIPublic(); g_ren.uiGLoad = fp.gLoad; g_ren.uiTime = fp.time;
        drawHud(fp); g_ren.flushUIPublic(); g_ren.uiGLoad = 0;
        if (!uiHidden && !showMap) drawLandingStamp();
        if (realTime - photoFlashT < 0.4f) { const float k = 1.f - (realTime - photoFlashT) / 0.4f; g_ren.rect(0, 0, (float)g_ren.W, (float)g_ren.H, vec3(1, 1, 1), 0.75f * k * k); }   // (the photo flight's shot)
        drawMapOverlay();
      }
      if (paused) drawPause();
      break;
    case SCR_DEBRIEF: drawDebrief(); break;
    case SCR_RESEARCH: drawResearch(fp); break;
    case SCR_LOADING: drawLoading(); break;
    case SCR_REGION_LOADING: break;   // (drawn and returned above, before the scene)
  }
  if (!(uiHidden && screen == SCR_FLIGHT && !paused) && !photo.on) { drawToasts(); drawAchievementBanner(); }
  drawPadCursor();
  if (!headless && !photo.on) {   // always-on frame-rate counter, top right
    float s = S(), fps = 1.f / std::max(fpsAvg, 1e-4f);
    std::string t = fmt("%.0f FPS", fps);
    float ts = 13 * s, tw = g_ren.textWidth(t, ts);
    vec3 c = fps >= 57.f ? vec3(0.45f, 1.f, 0.6f) : fps >= 40.f ? vec3(1.f, 0.82f, 0.3f) : vec3(1.f, 0.4f, 0.35f);
    g_ren.rect(g_ren.W - tw - 18 * s, 4 * s, tw + 14 * s, ts + 8 * s, vec3(0, 0, 0), 0.45f, 3 * s);
    g_ren.text(g_ren.W - 11 * s, 7 * s, ts, t, c, 1, 2, false);
  }
  if (showPerf && !photo.on) {
    float s = S();
    std::string t = fmt("%.0f fps  %.1f ms   GPU %s   res %.0f%% (%dx%d)", 1.f / std::max(fpsAvg, 1e-4f), fpsAvg * 1000.f,
                        g_ren.gpuMs > 0 ? fmt("%.1f ms", g_ren.gpuMs).c_str() : "n/a", g_ren.renderScale * 100.f,
                        (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale));
    t += fmt("   worst %.1f ms   target %d fps   scenery %d drawn, %d chunks, %.1f ms CPU", maxFrameMs, effectiveHz(), g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs);
    t += fmt("   particles %d  debris %d  wreck %d  traffic %d  comms %d", (int)particles.size(), (int)debris.size(), (int)wreck.size(), (int)traffic.craft.size(), (int)atc.history.size());
    const float* pm = g_ren.passMs;
    std::string t2 = fmt("GPU ms:  world %.1f   displays %.1f   feeds %.1f   objects %.1f   shadow proxy %.1f   lighting+clouds %.1f   TAA %.1f   sprites %.1f   bloom %.1f   shafts %.1f   composite %.1f",
                         pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]);
    float tw = std::max(g_ren.textWidth(t, 14 * s), g_ren.textWidth(t2, 14 * s)) + 20 * s;
    g_ren.rect(6 * s, 6 * s, tw, 46 * s, vec3(0, 0, 0), 0.6f);
    g_ren.text(14 * s, 10 * s, 14 * s, t, fpsAvg > 1.05f / effectiveHz() ? vec3(1, 0.5f, 0.3f) : vec3(0.5f, 1, 0.6f), 1, 0, false);
    g_ren.text(14 * s, 30 * s, 14 * s, t2, vec3(0.75f, 0.85f, 1.f), 1, 0, false);
  }
  g_ren.uiEnd();
  // Deploy, Fly, Initiate, Take flight and Restart are handled by the menus' UI, after this frame's scene was drawn.
  // Replace that entire menu frame now, so the very next SwapBuffers shows the loading card, never a frozen menu.
  if (screen == SCR_LOADING && launchCardPending()) renderLaunchCard();
}
