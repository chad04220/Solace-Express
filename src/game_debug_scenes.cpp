// Somnium Volandi - development scenes for the harnesses and diagnostics.bat (Game::debugScene)
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

// ------------------------------------------------------------------ development scenes for the render harness
// An airport's detail views (apv_ scenes, REGION_APV): 0 over the apron, 1 at the terminal's front, 2 along the stands,
// 3 from past the runway's end
static void airportView(int ai, int view, vec3& cam, vec3& look) {
  const Airport& a = g_world.airports[ai];
  AptLayout L = aptLayout(a, ai);
  float mid = 0.5f * (L.apU0 + L.apU1);
  if (view == 0) { cam = aptWorld(a, L.apU0 - 120.f, L.side * (L.twV - 60.f), 0); cam.y = a.elev + 70.f; look = aptWorld(a, mid, L.side * L.bldV, a.elev + 5.f); }
  else if (view == 1) { cam = aptWorld(a, L.termU - 140.f, L.side * (L.apV0 + 10.f), 0); cam.y = a.elev + 4.f; look = aptWorld(a, L.termU + 40.f, L.side * L.bldV, a.elev + 6.f); }
  else if (view == 2) { cam = aptWorld(a, L.standU0 - 60.f, L.side * (L.twV - 30.f), 0); cam.y = a.elev + 25.f; look = aptWorld(a, L.standU1, L.side * L.bldV, a.elev + 4.f); }
  else { cam = aptWorld(a, -a.length * 0.5f - 300.f, -L.side * 60.f, 0); cam.y = a.elev + 160.f; look = aptWorld(a, 0, L.side * L.apV0, a.elev); }
}
static std::string s_wxScene;   // a debug scene's weather: "<scene>@<WX>" (as the WX variable), for the render server's shots
void Game::debugScene(const std::string& name) {
  if (name.find('@') != std::string::npos) { s_wxScene = name.substr(name.find('@') + 1); debugScene(name.substr(0, name.find('@'))); s_wxScene.clear(); return; }
  if (name.compare(0, 7, "region-") == 0) {
    std::string regionId = name.substr(7);
    const bool ocean = regionId.size() > 6 && regionId.compare(regionId.size() - 6, 6, "_ocean") == 0;
    if (ocean) regionId.resize(regionId.size() - 6);
    auto descriptor = procedural::find(regionId);
    if (!descriptor) throw std::invalid_argument("Unknown region scene: " + name);
    beginFreeFlightSetup(); chooseFreeRegion(*descriptor); loadFreeRegion(freeRegion);
    while (screen == SCR_REGION_LOADING && (regionJob.valid() || regionUploading)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10)); updateRegionLoading(.01f);
    }
    if (!regionError.empty()) throw std::runtime_error(regionError);
    freeCraft = 0; freeAirport = 0; freeAirborne = true;
    if (const char* code = getenv("REGION_AIRPORT")) freeAirport = std::max(0, g_world.findAirport(code));   // (REGION_AIRPORT=<code>: start there)
    launchFreeFlight();
    camMode = 2; camYaw = .65f; camPitch = .2f; camZoom = 3.f; hudOn = false;
    if (ocean) {
      // Explicit review fixture beyond the land chart, looking east away from it.
      // No normal route-selection or region-transition code teleports a flight.
      plane.pos = vec3(140000.f, 2000.f, 0);
      plane.q = quat::axisAngle(vec3(0, 1, 0), -.5f * PI);
      plane.vel = plane.forward() * plane.spec->cruise;
      camYaw = -.5f * PI; camPitch = .15f;
    }
    if (getenv("REGION_ALT")) plane.pos.y = std::max(plane.pos.y, (float)atof(getenv("REGION_ALT")));
    if (getenv("REGION_TOD")) timeOfDay = (float)atof(getenv("REGION_TOD"));   // (REGION_TOD=<hours>: the time of day there)
    if (getenv("REGION_STYLE")) g_world.buildingStyle = atoi(getenv("REGION_STYLE"));   // (REGION_STYLE=<n>: its houses built another way)
    for (int i = 0; i < 8; ++i) updateCamera(.1f);
    // REGION_VIEW="x,y,z,lookx,looky,lookz": a fixed camera over the region (its overview, for a review shot)
    if (const char* view = getenv("REGION_VIEW")) {
      vec3 p, l;
      if (sscanf(view, "%f,%f,%f,%f,%f,%f", &p.x, &p.y, &p.z, &l.x, &l.y, &l.z) == 6) { dbgCam = true; dbgFollow = false; dbgCamPos = p; dbgCamLook = l; }
    }
    // REGION_APV=<view>: an airport detail view of REGION_AIRPORT's field (airportView)
    if (const char* apv = getenv("REGION_APV"); apv && getenv("REGION_AIRPORT")) {
      airportView(freeAirport, atoi(apv), dbgCamPos, dbgCamLook); dbgCam = true; dbgFollow = false;
    }
    // REGION_GPS=<range m>: the GPS open over the region (its airports and landmarks)
    if (const char* gps = getenv("REGION_GPS")) { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = std::max(2000.f, (float)atof(gps)); }
    toasts.clear(); botControl = true;
    if (getenv("REGION_HUB")) {   // REGION_HUB=1: the career's hub in the region (a CPL career travelled there, its board drawn)
      endFlight(false, "Region hub", OUT_ABANDONED);
      career = Career{}; career.license = LIC_CPL; career.money = 25000; career.storyIndex = 12;
      career.region = descriptor->id; career.location = procedural::mainAirport(descriptor->id); career.refreshBoard();
      freeFlight = isolatedFlight = false; screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 0; selContract = std::min(2, (int)career.board.size() + 1);
      dbgCam = false; realTime = 20; toasts.clear();
    }
    printf("Region scene: %s / %s / %zu airports / %zu towns / actual Free Flight\n",
      descriptor->id.c_str(), descriptor->name.c_str(), g_world.airports.size(), g_world.towns.size());
    return;
  }
  career.newGame(); career.license = LIC_ATP;
  if (name == "menu") { screen = SCR_MENU; realTime = 20; return; }
  if (name.compare(0, 5, "menuT") == 0) { screen = SCR_MENU; realTime = (float)atof(name.c_str() + 5); return; }   // the menu tour at t seconds
  if (name == "hub") { screen = SCR_HUB; realTime = 20; return; }
  if (name == "military") { screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 2; militarySelection = 0; realTime = 20; return; }   // the service board's preview
  if (name.compare(0, 5, "free_") == 0) { freeCraft = std::clamp(atoi(name.c_str() + 5), 0, kAircraftCount - 1); screen = SCR_FREE_FLIGHT; realTime = 20; return; }   // Free Flight's: free_<type>
  if (name.compare(0, 7, "hangar_") == 0) {   // the hangar's preview of one aircraft: hangar_<type>[_<airport code>]
    char code[8] = {}; int t = 1; sscanf(name.c_str() + 7, "%d_%7s", &t, code);
    if (code[0] && g_world.findAirport(code) >= 0) career.location = g_world.findAirport(code);
    screen = SCR_HUB; hubTab = TAB_HANGAR; selHangar = std::clamp(t, 0, kAircraftCount - 1); realTime = 20; return;
  }
  if (name == "hangarfit") {   // the hangar's mechanic (docs/HANGAR.md): your own Ambler on floats with long-range tanks
    std::string m; career.money = 60000; career.buy(1, &m); career.buyFloats(0, &m); career.buyKit(0, KIT_TANKS, &m);
    screen = SCR_HUB; hubTab = TAB_HANGAR; selHangar = 1; realTime = 20; return;
  }
  if (name.compare(0, 4, "jcam") == 0) {  // XR-30 close-up from an orbit angle: jcam<yaw deg>_<pitch deg>
    float yawD = 0, pitD = 10, thrP = -1, nozP = 0, zoom = 0.55f, tod = -1; sscanf(name.c_str() + 4, "%f_%f_%f_%f_%f_%f", &yawD, &pitD, &thrP, &nozP, &zoom, &tod);
    if (tod >= 0) resTime = tod;
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudCover = 0.3f;
    if (getenv("JCLOUD")) { wx.cloudCover = (float)atof(getenv("JCLOUD")); wx.cloudBase = plane.pos.y + 250.f; }   // cloud checks
    if (thrP >= 0) {   // optional throttle (and nozzle) percent: let the exhaust plumes and sparks develop
      plane.ctl.throttle = thrP / 100.f; plane.engineSpool = thrP / 100.f; plane.ctl.flaps = plane.flaps = plane.nozzle = nozP / 100.f;
      for (int i = 0; i < 30; i++) { realTime += 1 / 60.f; update(1 / 60.f); plane.engineSpool = thrP / 100.f; }
    }
    if (getenv("JMACH")) {   // flying at a forced Mach number (vapour cone checks)
      float M = (float)atof(getenv("JMACH"));
      for (int i = 0; i < 10; i++) { plane.vel = plane.forward() * (M * 330.f); realTime += 1 / 60.f; update(1 / 60.f); }
      plane.vel = plane.forward() * (M * 330.f); plane.mach = M;
    }
    if (getenv("WRSPARKS")) { int n = 0; for (auto& q : particles) if (q.kind == SPR_SPARK && n < 16) { vec3 b = plane.q.conj().rotate(q.p - plane.pos), v = plane.q.conj().rotate(q.v - plane.vel); printf("spark body (%.1f, %.1f, %.1f) rel v (%.0f, %.0f, %.0f) age %.2f\n", b.x, b.y, b.z, v.x, v.y, v.z, q.maxLife - q.life); n++; } }
    camMode = 2; camYaw = yawD * DEG; camPitch = pitD * DEG; camZoom = zoom; hudOn = false;
    for (int i = 0; i < 5; i++) updateCamera(0.1f);
    toasts.clear(); return;
  }
  if (name == "vapour") {  // XR-30 pulling g near the cloud base: wingtip vapour must trail behind the tips
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudBase = 300; wx.cloudCover = 0.2f;
    plane.vel = plane.forward() * 280.f; plane.ctl.throttle = 0.9f; botControl = true; plane.ctl.pitch = 0.6f; plane.ctl.gearDown = false; plane.gear = 0;
    for (int i = 0; i < 40; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    camMode = 2; camYaw = 1.2f; camPitch = 0.25f; camZoom = 1.2f; for (int i = 0; i < 5; i++) updateCamera(0.1f); toasts.clear();
    { int ni = 0; float dmin = 1e9f; for (auto& tt : tipTrail) for (auto& q : tt) { ni++; dmin = std::min(dmin, length(q.p - plane.pos)); } printf("vapour: %d ribbon points, nearest %.1f m from the CG, g=%.1f\n", ni, dmin, plane.gLoad); }
    return;
  }
  if (name == "pad") {  // gamepad menu navigation self-test
    auto frame = [&](unsigned btn) { in.pad = true; in.buttonsPressed = btn & ~in.buttons; in.buttons = btn; realTime += 1 / 30.f; update(1 / 30.f); render(); in.endFrame(); };
    screen = SCR_MENU; realTime = 20;
    frame(PAD_LS | PAD_RS); frame(0);
    printf("pad: combo -> research menu: %s\n", screen == SCR_RESEARCH ? "ok" : "FAIL");
    for (int i = 0; i < 50; i++) frame(0);           // let the access sequence finish
    int site = resAirport; frame(PAD_RB); frame(0);
    printf("pad: RB steps launch site: %s\n", resAirport == stepResearchSite(site, 1) ? "ok" : "FAIL");
    frame(PAD_B); frame(0);
    printf("pad: B backs out to main menu: %s\n", screen == SCR_MENU ? "ok" : "FAIL");
    screen = SCR_HUB; hubTab = 0; frame(0);
    float s = S(); float tx = 20 * s + 1 * (150 * s + 10 * s) + 75 * s, ty = 76 * s + 19 * s;
    in.lx = 1.f; float x0 = in.mx; frame(0); in.lx = 0;
    printf("pad: stick moves cursor: %s\n", in.mx > x0 ? "ok" : "FAIL");
    in.mx = tx; in.my = ty; frame(PAD_A); frame(0);
    printf("pad: A clicks the Hangar tab: %s\n", hubTab == 1 ? "ok" : "FAIL");
    in.lx = 0.5f; frame(0); in.lx = 0; in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.4f;
    // crash, then A skips the crash sequence straight to the results (and the press doesn't click through them)
    resAirborne = true; launchResearch();
    plane.vel = plane.forward() * 620.f; botControl = true; plane.ctl.pitch = 1;
    for (int i = 0; i < 600 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    bool wasCrash = crashed && screen == SCR_FLIGHT;
    in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.75f; frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the research crash back to the research menu: %s\n", wasCrash && screen == SCR_RESEARCH ? "ok" : "FAIL");
    // career flight: overstress it, skip with A, land on the results screen without clicking through
    startFlight(g_story[0], 1, Career::SRC_OWNED);
    plane.pos.y += 1500.f; plane.vel = plane.forward() * 95.f; plane.onGround = false; botControl = true; plane.ctl.pitch = -1;
    for (int i = 0; i < 900 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    wasCrash = crashed && screen == SCR_FLIGHT;
    frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the career crash to the results: %s\n", wasCrash && screen == SCR_DEBRIEF ? "ok" : "FAIL");
    return;
  }
  if (name == "research" || name == "research40" || name == "research10" || name == "research20" || name == "research50") {   // the terminal, settled (selection decrypted)
    screen = SCR_RESEARCH; realTime = 30; resOpened = 20; resAuthed = true;
    resCraft = name == "research40" ? kSomnus : name == "research10" ? kVigil : name == "research20" ? kReverie : name == "research50" ? kMorpheus : kResearchJet; resLastCraft = resCraft; resSelT = 20; resAirport = std::max(0, g_world.findAirport("CAP"));
    resSeqOpened = resOpened; resSeq = realTime - resOpened; resSeqAt = realTime; resWarm = false; resWork = 1.f;   // (the boot sequence's own clock long past the shutters: drawResearch)
    return;
  }
  if (name.rfind("researchscan", 0) == 0) {   // the biometric sequence at a moment: researchscan<tenths of a second into the sequence>, the warm-up as far on as the scans show
    screen = SCR_RESEARCH; realTime = 30; resAuthed = false; resOpened = realTime - atoi(name.c_str() + 12) / 10.f;
    resSeq = realTime - resOpened; resSeqAt = realTime; resSeqOpened = resOpened;
    resWarm = resSeq < kResScan1; resWork = clampf((resSeq - kResScan0) / (kResScan1 - kResScan0), 0.f, 1.f); resWarmFrames = 1 + (int)(resWork * 4.f * (4 * resCraftCount() + 3)); resWarmCraft = -1;
    return;
  }
  if (name.compare(0, 3, "wr_") == 0) {   // XR-40: wr_<mode>_<cam yaw>_<cam pitch>_<cam dist>_<seconds>[_<airport>_<weather>_<hour>_<over town>]
    // modes: 0 cruise, 1 hover, 2 parked, 3 cloak spreading, 4 cloaked, 5 turrets out + bay open, 6 lasers firing,
    // 7 plasma bomb (camera on the impact), 8 cockpit. The optional tail: the research flight's airport, weather (0 clear,
    // 1 cloud, 2 storm) and hour, and 1 to fly it in low over the town nearest that airport (wr_8_0_-8_0_1_3_2_22.5_1:
    // the cockpit at night in a storm, coming in over Solace Capital)
    int mode = 0, site = -1, wxSel = -1, overTown = 0; float yawD = 210, pitD = 12, dist = 30, secs = 1.5f, hour = -1.f;
    sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f_%d_%d_%f_%d", &mode, &yawD, &pitD, &dist, &secs, &site, &wxSel, &hour, &overTown);
    const int siteWas = resAirport, wxWas = resWx;
    if (site >= 0) resAirport = std::min(site, (int)g_world.airports.size() - 1);
    if (wxSel >= 0) resWx = std::min(wxSel, 2);
    resCraft = kSomnus; realTime = 20; resAirborne = mode != 2; resTime = getenv("TOD") ? (float)atof(getenv("TOD")) : hour >= 0.f ? hour : 12.f; launchResearch();
    resAirport = siteWas; resWx = wxWas;   // (the research menu's own choices: this scene's are for this flight only)
    // (over town 1: 3 km short of the town's centre, 450 m over it, heading across it at 200 m/s. 2: the same flight
    // over its centre, held there - flying on, going nowhere - a scene that holds still, for timing the worst of it: in
    // the analysis the flypast was over the city for some measurements and short of it for others, and a hover on the
    // pods climbed away from it)
    const bool pinTown = overTown == 2 && resAirborne;
    if (overTown && resAirborne && !g_world.towns.empty()) {
      const Airport& a = g_world.airports[std::clamp(site, 0, (int)g_world.airports.size() - 1)];
      const Town* T = &g_world.towns[0]; float best = 1e18f;
      for (const Town& town : g_world.towns) { const float d = (town.x - a.x) * (town.x - a.x) + (town.z - a.z) * (town.z - a.z); if (d < best) { best = d; T = &town; } }
      const vec3 c(T->x, 0.f, T->z), from = c + normalize(vec3(a.x - T->x, 0.f, a.z - T->z)) * 3000.f;
      const float hdg = atan2f(c.x - from.x, -(c.z - from.z)) / DEG;
      const vec3 at = pinTown ? c : from;
      plane.reset(&kAircraft[resCraft], vec3(at.x, std::max(g_world.height(at.x, at.z), 0.f) + 450.f, at.z), hdg, kAircraft[resCraft].maxFuel, 85, true, 200.f);
      plane.ctl.throttle = 0.7f; settleAirborneStart();
      camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 4, 26));
      if (pinTown) { benchPin = true; benchPinPos = plane.pos; benchPinQ = plane.q; }
    }
    botControl = true;
    if (mode == 1) { plane.ctl.flaps = 1; flapNotch = 1; plane.flaps = plane.nozzle = 1; plane.vel = vec3(); plane.ctl.throttle = 0.66f; plane.engineRunning = true; plane.engineSpool = 0.66f; }
    else if (mode != 2) { plane.apEngage(Plane::AP_HOLD, -1, wx); }
    if (mode == 3 || mode == 4) somnus.cloakOn = true;
    if (getenv("WRCLOUD")) { wx.cloudCover = (float)atof(getenv("WRCLOUD")); wx.cloudBase = plane.pos.y - 60.f; }   // craft inside the cloud deck
    if (mode == 5 || mode == 6) { somnus.armed = true; }
    if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.apSpeed = 900.f; }
    if (getenv("WRSPD")) { plane.vel = plane.forward() * (float)atof(getenv("WRSPD")); plane.apSpeed = (float)atof(getenv("WRSPD")); }
    if (mode == 5) somnus.bayHold = 100.f;
    // 9: cockpit looking down while a bomb falls (secs after release); 10: the same, secs after it goes off
    bool bombCk = mode == 9 || mode == 10 || (mode == 8 && getenv("WRBOMB"));
    if (mode == 7 || bombCk) { somnus.bombQueue = 1; somnus.armed = bombCk; }
    if (getenv("WRUFO")) { startUfo(); ufo.side = (float)atof(getenv("WRUFO")); }   // UFO alongside (side -1 / 1)
    float firstDrop = 0; vec3 blastAt;
    int frames = (int)((mode == 3 ? 0.55f : mode == 7 || mode == 10 ? 60.f : mode == 9 ? 30.f : secs) * 60.f);
    float dropT = -1, boomAt = -1;
    for (int i = 0; i < frames; i++) {
      if (mode == 9 || mode == 10) {
        if (dropT < 0 && !somnus.bombs.empty()) dropT = realTime;
        if (boomAt < 0 && !somnus.blasts.empty()) boomAt = realTime;
        if (mode == 9 && dropT >= 0 && realTime - dropT > secs) break;
        if (mode == 10 && boomAt >= 0 && realTime - boomAt > secs) break;
      }
      realTime += 1 / 60.f; update(1 / 60.f);
      if (mode == 6) somnus.wantFire = true;   // trigger held
      if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.engineSpool = std::max(plane.engineSpool, plane.ctl.throttle * 0.98f); }
      if (mode == 7 && !somnus.blasts.empty() && firstDrop == 0) { firstDrop = realTime; blastAt = somnus.blasts[0].p; }
      if (mode == 7 && firstDrop > 0 && realTime - firstDrop > secs) break;
    }
    if (mode == 6) {
      for (auto& b : somnus.bolts) b.life = b.age + 1.f;   // keep the bolts in flight bright for the screenshot
      // the streak of the youngest visible bolt must start at its lens, however fast the craft flies
      // (in the craft's frame the tail sits on the lens's firing line, 2000 m/s x age - streak out from it)
      float worstOff = 0, worstAlong = 0; int n = 0;
      for (auto& b : somnus.bolts) if (b.len > 0.05f && !b.hit && b.age < 0.5f) {
        float off = 1e9f, alongErr = 0;
        for (int s = 0; s < 2; s++) {
          vec3 lens = plane.pos + plane.q.rotate(vec3(s ? 0.95f : -0.95f, -0.68f, -6.44f));
          vec3 r = b.h - b.d * b.len - lens; float al = dot(r, b.d), o = length(r - b.d * al);
          if (o < off) { off = o; alongErr = al - (2000.f * b.age - b.len); }
        }
        worstOff = std::max(worstOff, off); worstAlong = std::max(worstAlong, fabsf(alongErr)); n++;
      }
      printf("wr: speed %.0f m/s, %d young bolts: streak tails off the firing line by <= %.2f m, along-error <= %.2f m\n", length(plane.vel), n, worstOff, worstAlong);
    }
    if (getenv("WRSPARKS")) { int n = 0; for (auto& q : particles) if (q.kind == SPR_SPARK && n < 12) { vec3 b = plane.q.conj().rotate(q.p - plane.pos); printf("spark body (%.1f, %.1f, %.1f) life %.2f\n", b.x, b.y, b.z, q.life); n++; } }
    if (ufo.on) { vec3 r = plane.q.conj().rotate(ufo.pos - plane.pos); printf("wr: ufo t %.1f at craft-frame (%.0f, %.0f, %.0f) m\n", ufo.t, r.x, r.y, r.z); }
    toasts.clear(); hint.clear();
    for (auto& b : somnus.blasts) printf("wr: blast age %.2f R %.0f at %.0f %.0f %.0f particles %d\n", b.age, b.R, b.p.x, b.p.y, b.p.z, (int)particles.size());
    printf("wr: mode %d stealth %.2f lasers %.2f bay %.2f bombs %d blasts %d tilt %.2f %.2f %.2f %.2f thr %.2f\n", mode, somnus.stealth, somnus.lasers, somnus.bay, (int)somnus.bombs.size(), (int)somnus.blasts.size(),
           plane.podTilt[0], plane.podTilt[1], plane.podTilt[2], plane.podTilt[3], plane.podThr[0]);
    if (mode == 8 || mode == 9 || mode == 10) { camMode = 1; lookYaw = yawD * DEG; lookPitch = pitD * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f; return; }
    hudOn = false; dbgCam = true;
    float h = plane.heading() * DEG, yw = h + yawD * DEG, pt = pitD * DEG;
    vec3 focus = mode == 7 && firstDrop > 0 ? blastAt + vec3(0, 30.f, 0) : plane.pos;
    dbgCamLook = focus; dbgCamPos = focus + vec3(sinf(yw) * cosf(pt), sinf(pt), -cosf(yw) * cosf(pt)) * dist;
    if (!(mode == 7 && firstDrop > 0)) { dbgFollow = true; dbgFollowOff = dbgCamPos - plane.pos; }
    return;
  }
  if (name == "rboom") {   // the XR-30 the moment it goes through Mach 1, the chase camera behind it: the boom's collar and rings
    realTime = 20; resAirborne = true; launchResearch();
    for (int i = 0; i < 60; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    plane.vel = plane.forward() * 346.f; audioMachArmed = true; toasts.clear();
    return;
  }
  if (name == "rjet" || name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") { realTime = 20; resAirborne = true; launchResearch(); if (name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") camMode = 1; for (int i = 0; i < 90; i++) { realTime += 1 / 30.f; update(1 / 30.f); } toasts.clear(); if (name == "rjetl") lookYaw = 1.75f; if (name == "rjetr") { lookYaw = -1.2f; lookPitch = -0.6f; } if (name == "rjetd") lookPitch = -0.75f; return; }
  if (name == "radio") { loadStations(); screen = SCR_HUB; showRadio = true; realTime = 20; radioScroll = 6; return; }
  if (name.size() == 4 && name.compare(0, 3, "hub") == 0) { screen = SCR_HUB; hubTab = name[3] - '0'; realTime = 20; return; }
  Contract c = g_story[0];
  int spec = 0;
  if (name.size() == 3 && name[0] == 'm' && ((name[1] >= '0' && name[1] <= '9') || (name[1] >= 'a' && name[1] < 'a' + kAircraftCount - 10))) {
    // model inspection: m<aircraft><view>  views: e = exterior 3/4 front, r = controls deflected from behind,
    // c = cockpit, g = on the ground, s = side, d = cockpit with controls deflected, f = a split flap (the left one
    // stuck up, the right one down) from behind
    spec = name[1] >= 'a' ? name[1] - 'a' + 10 : name[1] - '0'; char v = name[2];   // (aircraft 10 on: a, b, c ...)
    c.wx = Weather(); c.wx.cloudCover = 0.25f; c.wx.timeOfDay = 10.5f; c.wx.windSpeed = 0; c.wx.turbulence = 0;
    startFlight(c, spec, Career::SRC_OWNED);
    realTime = 10; engineAutoStarted = true;
    const AircraftSpec& s = kAircraft[spec];
    if (v != 'g') { plane.reset(&s, vec3(-6000, 700, 16000), 0, s.maxFuel, 100, true, (v == 'r' || v == 'd' || v == 'b') ? s.vref * 1.3f : s.cruise); plane.gear = 1; plane.ctl.gearDown = true; }
    else { plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 900; plane.n1 = 60; }
    takeoffAnnounced = true;
    float size = std::max(s.fusLen, s.span);
    camMode = 2; camZoom = (v == 'g' ? 0.6f : 0.42f);
    camYaw = 3.14159f - 0.75f; camPitch = 0.18f;
    if (v == 'r' || v == 'd' || v == 'b') { botControl = true; plane.ctl.roll = 1; plane.ctl.pitch = 1; plane.ctl.yaw = 1; plane.ctl.flaps = v == 'b' ? 0.f : 1.f; plane.flaps = plane.ctl.flaps; camYaw = v == 'b' ? 0.f : -0.55f; camPitch = v == 'b' ? 0.22f : 0.35f; if (v == 'b') camZoom = 0.3f; }
    if (v == 's') { camYaw = 1.5708f; camPitch = 0.05f; }
    if (v == 'f') { plane.failNow(FAIL_FLAP_ASYM, 0); botControl = true; plane.ctl.flaps = 1.f; plane.flaps = aeroGeom(s).flapMax; camYaw = 0.f; camPitch = 0.3f; camZoom = 0.3f; }
    if (v == 'c' || v == 'd') { camMode = 1; lookYaw = 0; lookPitch = v == 'd' ? -0.45f : -0.13f; }
    if (v == 'y' || v == 'p' || v == 'a') {
      botControl = true; plane.ctl = Controls(); plane.ctl.throttle = 0.6f;
      if (v == 'y') plane.ctl.yaw = 1; if (v == 'p') plane.ctl.pitch = 1; if (v == 'a') plane.ctl.roll = 1;
      camMode = 3;
      vec3 off = v == 'y' ? vec3(0, size * 1.6f, size * 0.9f) : v == 'p' ? vec3(size * 1.5f, 0.3f, 0) : vec3(0, size * 0.25f, size * 1.8f);
      hint.clear(); toasts.clear(); hudOn = false;
      camPos = plane.pos + off;
      return;
    }
    (void)size;
    hint.clear(); toasts.clear(); hudOn = false;
    camPos = plane.pos;
    updateCamera(0.016f);
    if (v == 'd') { lookPitch = -0.35f; }
    return;
  }
  if (name == "storm") { c = g_story[g_story.size() - 1]; spec = 5; }
  if (name == "jet") { c = g_story[g_story.size() - 4]; spec = 6; }
  if (name == "night") { c = g_story[11]; c.wx.timeOfDay = 21.5f; spec = 1; }
  if (name == "snow") { c = g_story[20]; spec = 2; }
  if (const char* sp = getenv("SPEC")) {   // (SPEC=<career or research aircraft>: that one flies the scene)
    for (int t : kCareerAircraft) if (t == atoi(sp)) spec = t;
    for (int t : kResearchAircraft) if (t == atoi(sp)) spec = t;
  }
  startFlight(c, spec, Career::SRC_OWNED);
  if (const char* w = !s_wxScene.empty() ? s_wxScene.c_str() : getenv("WX")) {   // weather checks: WX="cover,base m,precip,storm,wind kt,from deg,gust kt,turbulence,time of day"
    float v[9] = {wx.cloudCover, wx.cloudBase, (float)wx.precip, wx.storm ? 1.f : 0.f, wx.windSpeed * MS_TO_KT, wx.windFrom, wx.gust * MS_TO_KT, wx.turbulence, wx.timeOfDay};
    sscanf(w, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
    wx.cloudCover = v[0]; wx.cloudBase = v[1]; wx.precip = (int)v[2]; wx.storm = v[3] > 0.5f; wx.windSpeed = v[4] / MS_TO_KT; wx.windFrom = v[5];
    wx.gust = v[6] / MS_TO_KT; wx.turbulence = v[7]; wx.timeOfDay = timeOfDay = v[8]; wx.visibility = wx.precip ? 9000.f : 40000.f;
    wxStart = wx; contract.wx = wx; contract.wxShift = false;
  }
  realTime = 10;
  plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 1000; engineAutoStarted = true;
  {
    float px, pz, agl, hdg; char cm = 'c';
    float bagl = 300, blook = -70, bafter = 0.25f;
    if (sscanf(name.c_str(), "wrbomb_%f_%f_%f", &bagl, &blook, &bafter) >= 1) {   // XR-40 cockpit: drop a bomb and watch it go off through the glass floor
      resCraft = kSomnus; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      plane.pos.y = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + bagl;
      plane.apEngage(Plane::AP_HOLD, -1, wx); plane.apAlt = plane.pos.y; botControl = true;
      somnus.armed = true;
      for (int i = 0; i < 90; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
      somnus.bombQueue = 1;
      float since = -1;
      for (int i = 0; i < 60 * 40; i++) {
        realTime += 1 / 60.f; update(1 / 60.f);
        if (since < 0 && !somnus.blasts.empty()) since = 0;
        if (since >= 0) { since += 1 / 60.f; if (since >= bafter) break; }
        if (since < 0 && bafter < 0 && !somnus.bombs.empty() && somnus.bombs[0].t > -bafter) break;   // negative: a falling bomb
      }
      printf("wrbomb: bombs %d blasts %d pip %d\n", (int)somnus.bombs.size(), (int)somnus.blasts.size(), 0);
      camMode = 1; lookYaw = 0; lookPitch = blook * DEG; camYaw = 0; camPitch = lookPitch + 0.12f; hudOn = true; toasts.clear(); hint.clear();
      return;
    }
    float fx, fz, fsec = 3, fpitch = 28, fyaw = 0;
    if (sscanf(name.c_str(), "lasertest_%f_%f_%f_%f_%f", &fx, &fz, &fsec, &fpitch, &fyaw) >= 2) {   // XR-40 firing at the ground ahead from a fixed hover
      resCraft = kSomnus; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      float g = std::max(g_world.height(fx, fz), 0.f);
      vec3 pos(fx, g + 70.f, fz);
      quat q = quat::axisAngle(vec3(0, 1, 0), -fyaw * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG);
      somnus.armed = true; botControl = true;
      for (int i = 0; i < (int)((fsec + 1.f) * 60.f); i++) {
        plane.pos = pos; plane.q = q; plane.vel = vec3(); plane.w = vec3();
        if (i > 60) somnus.wantFire = true;
        realTime += 1 / 60.f; update(1 / 60.f);
        if ((i & 7) == 0) { plane.q = quat::axisAngle(vec3(0, 1, 0), (-fyaw + 6.f * sinf(i * 0.05f)) * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG); q = plane.q; }   // sweep the aim
      }
      printf("lasertest: %d destroyed, %d scorch pits, %d bolts in flight\n", somnus.wrecked, (int)somnus.scorch.size(), (int)somnus.bolts.size());
      vec3 f = q.rotate(vec3(0, 0, -1)); vec3 tgt = pos + f * (70.f / std::max(-f.y, 0.2f));
      hudOn = false; dbgCam = true; dbgFollow = false; toasts.clear(); hint.clear();
      float cd = getenv("LTDIST") ? (float)atof(getenv("LTDIST")) : 1.f;
      dbgCamLook = tgt; dbgCamPos = tgt + vec3(70.f, 45.f, 40.f) * cd;
      for (int i = 0; i < 5; i++) updateCamera(0.1f);
      return;
    }
    float yw, pt, dist, lh = 8, tod = -1;
    if (sscanf(name.c_str(), "look_%f_%f_%f_%f_%f_%f_%f", &px, &pz, &yw, &pt, &dist, &lh, &tod) >= 5) {   // free camera on a ground point
      if (tod >= 0) timeOfDay = tod;
      plane.reset(&kAircraft[1], vec3(px + 4000.f, std::max(g_world.height(px + 4000.f, pz), 0.f) + 900.f, pz), 90, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; hudOn = false; hint.clear(); toasts.clear(); dbgCam = true; dbgFollow = false;
      dbgCamLook = vec3(px, std::max(g_world.height(px, pz), 0.f) + lh, pz);
      dbgCamPos = dbgCamLook + vec3(sinf(yw * DEG) * cosf(pt * DEG), sinf(pt * DEG), -cosf(yw * DEG) * cosf(pt * DEG)) * dist;
      dbgCamPos.y = std::max(dbgCamPos.y, std::max(g_world.height(dbgCamPos.x, dbgCamPos.z), 0.f) + 2.f);
      for (int i = 0; i < 5; i++) updateCamera(0.1f);
      return;
    }
    // the weather in flight: straight and level on the autopilot from (x, altitude, z) for some seconds (through the
    // cloud and rain WX sets), then the chase camera (c), the cockpit (k) or a look back along the path flown from
    // beside the aircraft (b: its wake through the cloud; t from above and behind; w inside the channel half a km back;
    // o off to the side of the hole it came out of the cloud by): wxfly_<x>_<z>_<alt m>_<hdg>_<seconds>_<cam>_<aircraft>
    float fsecs = 0; int fspec = 1;
    if (sscanf(name.c_str(), "wxfly_%f_%f_%f_%f_%f_%c_%d", &px, &pz, &agl, &hdg, &fsecs, &cm, &fspec) >= 5) {
      fspec = validCareerSelection(fspec);
      plane.reset(&kAircraft[fspec], vec3(px, agl, pz), hdg, kAircraft[fspec].maxFuel, 100, true, kAircraft[fspec].cruise);
      takeoffAnnounced = true; camQ = plane.q; hudOn = false; hint.clear(); botControl = true;
      plane.apEngage(Plane::AP_HOLD, -1, wx); plane.apAlt = agl; plane.apHeading = hdg;
      if (cm == 'k') camMode = 1;
      // (negative seconds: on until that long after it came out of a cloud it went through, at most two minutes)
      float inT = 0, outT = 0;
      for (int i = 0; i < (int)(fabsf(fsecs) * 60.f) || (fsecs < 0.f && i < 7200); i++) {
        if (crashed) break;
        realTime += 1 / 60.f; update(1 / 60.f);
        if (plane.wxl.cloud > 0.2f) { inT += 1 / 60.f; outT = 0; } else if (inT > 2.f && plane.wxl.cloud < 0.02f) outT += 1 / 60.f;
        if (fsecs < 0.f && inT > 2.f && outT >= -fsecs) break;
      }
      printf("wxfly: at (%.0f, %.0f, %.0f) %.0f kt, cloud here %.2f, rain %.2f, eddies %.2f m/s, lift %+.1f thermal %+.1f, wake points %d\n", plane.pos.x, plane.pos.y, plane.pos.z,
             plane.airspeed * MS_TO_KT, plane.wxl.cloud, plane.wxl.rain, plane.wxl.sigma, plane.wxl.lift, plane.wxl.thermal, (int)cloudWake.size());
      toasts.clear();
      if (cm == 't' && cloudWake.size() >= 5) {   // (from above and ahead, looking down along the path: its trench through a cloud top)
        const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake.back().c - off, B = cloudWake[cloudWake.size() - 5].c - off;
        dbgCam = true; dbgFollow = false; dbgCamPos = A + normalize(A - B) * 120.f + vec3(0, 140.f, 0); dbgCamLook = (A + B) * 0.5f;
        for (float h : {140.f, 260.f, 420.f}) {   // (high enough to be out of the cloud)
          const vec3 cp = A + normalize(A - B) * (0.85f * h) + vec3(0, h, 0);
          if (wxfield::cloudDensity(wx, cp, true) < 0.01f && wxfield::cloudDensity(wx, (cp + dbgCamLook) * 0.5f + vec3(0, 40.f, 0), true) < 0.05f) { dbgCamPos = cp; break; }
        }
      }
      if (cm == 'w' && cloudWake.size() >= 9) {   // (inside the channel half a kilometre back, looking on back along it)
        const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake[cloudWake.size() - 5].c - off, B = cloudWake[cloudWake.size() - 9].c - off;
        dbgCam = true; dbgFollow = false; dbgCamPos = A + vec3(0, 4.f, 0); dbgCamLook = B;
      }
      // (a camera in clear air with a clear view of a point: the first of some places round it with no cloud on the line
      // to it, short of the last stretch)
      auto clearView = [&](vec3 at, vec3 d, float short_) {
        const vec3 sd = normalize(cross(d, vec3(0, 1, 0)));
        for (float dist : {350.f, 600.f, 950.f}) for (float az : {35.f, -35.f, 65.f, -65.f, 15.f, 90.f}) for (float el : {6.f, 20.f, 40.f}) {
          const vec3 cp = at + (d * cosf(az * DEG) + sd * sinf(az * DEG)) * (dist * cosf(el * DEG)) + vec3(0, dist * sinf(el * DEG), 0);
          bool clear = true;
          for (float t = 0.f; t < 1.f && clear; t += 0.02f) { const vec3 q = cp + (at - cp) * t; if (length(q - at) > short_ && wxfield::cloudDensity(wx, q, true) > 0.03f) clear = false; }
          if (clear) return cp;
        }
        return at + d * 400.f + vec3(0, 60.f, 0);
      };
      if (cm == 'o' && cloudWake.size() >= 2) {   // (off to the side, at the hole where the path came out of the cloud)
        const vec3 off(cloudOff.x, 0, cloudOff.y);
        int e = (int)cloudWake.size() - 1;
        while (e > 0 && wxfield::cloudDensity(wx, cloudWake[e].c - off, true) < 0.25f) e--;
        const vec3 E = cloudWake[e].c - off, d = normalize(cloudWake.back().c - cloudWake[std::max(e - 1, 0)].c);
        dbgCam = true; dbgFollow = false; dbgCamPos = clearView(E, d, 90.f); dbgCamLook = E;
      }
      if (cm == 'b') {   // (down the wake's own line: the path drifts a little off the heading)
        dbgCam = true; dbgFollow = false; vec3 f = plane.forward(); dbgCamPos = plane.pos - f * 8.f + vec3(0, 2.f, 0); dbgCamLook = plane.pos - f * 400.f;
        if (cloudWake.size() >= 5) {
          const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake.back().c - off, B = cloudWake[cloudWake.size() - 5].c - off;
          dbgCamPos = A + normalize(A - B) * 40.f + vec3(0, 1.f, 0); dbgCamLook = B;
        }
      }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      return;
    }
    if (sscanf(name.c_str(), "at_%f_%f_%f_%f_%c", &px, &pz, &agl, &hdg, &cm) >= 4) {
      plane.reset(&kAircraft[1], vec3(px, std::max(g_world.height(px, pz), 0.f) + agl, pz), hdg, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; camQ = plane.q; hudOn = false; hint.clear(); toasts.clear();
      if (cm == 'k') camMode = 1;
      if (cm == 'f') { camMode = 3; botControl = true; camPos = plane.pos + vec3(0, 40, 0) - plane.forward() * 60.f; }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      return;
    }
  }
  if (name == "air" || name == "sunset" || name == "mountain" || name == "cockpit" || name == "jet" || name == "storm" || name == "snow" || name == "hud" || name == "forest" || name == "seam" || name == "seamgps" || name == "at") {
    vec3 p(-4000, 600, 9000); float hdg = 40;
    if (name == "forest") { p = vec3(-960, 270, 23500); hdg = 0; }   // (low, north up the island's longest forest: 9 km of it ahead, 130-250 m below - what the trees cost, their fades and detail cross-fades)
    if (name == "sunset") { timeOfDay = 18.2f; p = vec3(-26000, 300, 14000); hdg = 270; }
    if (name == "mountain") { p = vec3(-20000, 1900, -9000); hdg = 60; }
    if (name == "jet") { p = vec3(8000, 900, 6000); hdg = 80; }
    if (name == "storm") { p = vec3(10000, 700, 10000); hdg = 120; }
    if (name == "snow") { p = vec3(20000, 600, -24000); hdg = 80; }
    if (name == "at") {   // AT="x,z,height above the ground,heading": anywhere (CAMPITCH tilts the chase camera down, degrees)
      float x = 0, z = 0, agl = 300, h = 0; if (getenv("AT")) sscanf(getenv("AT"), "%f,%f,%f,%f", &x, &z, &agl, &h);
      p = vec3(x, std::max(g_world.height(x, z), 0.f) + agl, z); hdg = h;
    }
    if (name == "seam" || name == "seamgps") { p = vec3(WRAP_HALF - 1500.f, getenv("SEAMALT") ? (float)atof(getenv("SEAMALT")) : 1500.f, 4000.f); hdg = getenv("SEAMHDG") ? (float)atof(getenv("SEAMHDG")) : 90.f; }   // (out east of the islands, past where the map used to wrap: the open ocean ahead)
    plane.reset(&kAircraft[spec], p, hdg, kAircraft[spec].maxFuel, 100, true, kAircraft[spec].cruise);
    takeoffAnnounced = true;
    camQ = plane.q;
    if (name == "cockpit") camMode = 1;
    if (name == "storm" && getenv("STRIKE")) {   // STRIKE=<metres ahead>,<degrees off the nose>,<seconds into it at the shot>: a stroke to the ground
      float R = 1500.f, off = 0.f, at = 0.06f; sscanf(getenv("STRIKE"), "%f,%f,%f", &R, &off, &at);
      const float a = (hdg + off) * DEG;
      const vec3 foot = p + vec3(sinf(a) * R, 0.f, -cosf(a) * R);
      makeStrike(vec3(foot.x + 150.f, std::max(wx.cloudBase, 300.f), foot.z - 80.f), vec3(foot.x, std::max(g_world.height(foot.x, foot.z), 0.f), foot.z), true, 7);
      strikes.back().t = at - 0.1f;   // (the shot's three frames, a 30th of a second apart, on)
      nextLightning = 1e9f;
    }
    if (name == "hud" && getenv("TDSTAMP")) { tdStampT = realTime - 0.6f; tdStampFpm = (float)atof(getenv("TDSTAMP")); }   // (TDSTAMP=fpm: the touchdown stamp)
    if (name == "hud") {   // HUDDEMO=1: every warning, a tower call, failures and messages lit, to look at the layout
      if (getenv("HUDDEMO")) {
        hudDemo = true;
        atcF.lastCall = "Ambler Alfa Bravo, runway 05, wind 040 at 8, cleared to land."; atcF.lastApt = contract.to; atcF.lastT = flightClock - 42.f;
        plane.fail.engineHealth[0] = 0.4f; plane.fail.alternator = true; plane.fail.battery = 0.6f; plane.fail.ice = 0.3f;
        toast("Checkpoint 2 of 6", vec3(0.5f, 1, 0.7f)); toast("Gear down", vec3(0.8f, 1, 0.8f)); toast("Time acceleration off", vec3(0.8f, 0.8f, 0.8f));
        hint = "Hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.";
        plane.apOn = true; plane.apMode = Plane::AP_HOLD; plane.apHeading = hdg + 25.f; plane.apAlt = 900.f; plane.apStatus = fmt("HDG %03.0f  ALT %.0f", hdg + 25.f, 900.f * M_TO_FT);
        showMinimap = true;
      }
    }
  }
  if (name == "rings" || name == "ringburst") {
    // (RINGWP: the gates already flown through; RINGDIST, RINGUP: the start that far before the next, that high above its centre)
    if (getenv("RINGWP")) wpIndex = std::min(std::max(atoi(getenv("RINGWP")), 0), std::max((int)contract.wps.size() - 1, 0));
    vec3 c, ax, ay; ringGeom(wpIndex, c, ax, ay);
    vec3 nd = normalize(cross(vec3(0, 1, 0), ax));
    float dist = name == "rings" ? 420.f : 160.f;
    if (getenv("RINGDIST")) dist = (float)atof(getenv("RINGDIST"));
    float hdg = atan2f(nd.x, -nd.z) / DEG;
    plane.reset(&kAircraft[0], c - nd * dist - vec3(0, getenv("RINGUP") ? -(float)atof(getenv("RINGUP")) : 12.f, 0), hdg, kAircraft[0].maxFuel, 100, true, kAircraft[0].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true;
    if (getenv("RINGCOCKPIT")) camMode = 1;
    plane.ctl = Controls(); plane.ctl.throttle = 0.7f;
    if (getenv("SIGHT")) { sightReq = atoi(getenv("SIGHT")); sightAsked = true; sightDone = false; }   // (SIGHT=<site>: the passengers' request on the HUD)
    if (getenv("GHOST")) {   // (a gate run's ghost alongside: the best run 80 m ahead and 30 m to the right)
      contract.type = CT_TRIAL; trialT0 = flightClock; trialT1 = -1.f; ghostBest.clear();
      const vec3 rt = normalize(cross(nd, vec3(0, 1, 0)));
      for (int i = 0; i < 400; i++) ghostBest.push_back(plane.pos + nd * (80.f + i * kGhostDt * kAircraft[0].cruise * 1.05f) + rt * 30.f);
    }
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < (name == "rings" ? 60 : 42); i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (getenv("PHOTO")) {   // (PHOTO=<look 0-4>: photo mode over the scene; PHOTOFRAME=<0-3>; PHOTOPANEL=0: without its panel; PHOTOSHOT=<folder>: the shot taken, saved there)
      botControl = false; enterPhoto(); botControl = true;
      photoLook(atoi(getenv("PHOTO")));
      if (getenv("PHOTOFRAME")) photo.frame = atoi(getenv("PHOTOFRAME"));
      if (getenv("PHOTOPANEL")) photo.panel = atoi(getenv("PHOTOPANEL")) != 0;
      if (getenv("PHOTOSHOT")) { photoDir = getenv("PHOTOSHOT"); photo.capture = 1; }
      if (getenv("PHOTOPAD")) in.pad = true;   // (PHOTOPAD: the panel's controller prompts)
    }
    return;
  }
  if (name.compare(0, 8, "airbreak") == 0) {   // XR-30 overstressed at speed: breaks up in the air; airbreak<seconds after>
    bool cessna = name.size() > 8 && name[8] == 'c';   // airbreakc<s>: a light aircraft pushed over hard in a dive instead
    float after = name.size() > 8 + cessna ? atof(name.c_str() + 8 + cessna) : 3.f;
    resAirborne = true; realTime = 20; launchResearch(); hudOn = false;
    const int bt = getenv("BRKTYPE") ? atoi(getenv("BRKTYPE")) : 1;   // (debug: the type that breaks up, for airbreakc)
    if (cessna) { plane.reset(&kAircraft[bt], plane.pos, 90, kAircraft[bt].maxFuel, 100, true, kAircraft[bt].cruise); researchFlight = false; }
    const float agl = getenv("BRKALT") ? (float)atof(getenv("BRKALT")) : 2500.f;   // (debug: a lower break-up, its pieces down in seconds)
    plane.pos.y = std::max(getenv("BRKALT") ? 0.f : plane.pos.y, std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + agl);
    plane.vel = plane.forward() * (cessna ? 95.f : 620.f); plane.ctl.throttle = 1; plane.engineSpool = 1; botControl = true; plane.ctl.pitch = cessna ? -1.f : 1.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.25f; camZoom = 1.2f;
    float tb = -1, tEnd = -1;
    for (int i = 0; i < 60 * 200; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("airbreak: broke up (%s), %d pieces, %.0f m AGL\n", plane.ev.crashReason.c_str(), (int)wreck.size(), plane.agl()); }
      if (crashed && tEnd < 0 && crashEndT < 1e8f) { tEnd = crashTimer; float sp = 0; for (auto& w : wreck) sp = std::max(sp, length(w.v));
        float big = craterR; for (const Pit& p : pits) big = std::max(big, p.R);
        printf("airbreak: all pieces down %.1f s after the break-up (%zu craters, the largest %.1f m) %s\n", crashTimer, pits.size(), big, crashTimer > 5.f ? "ok" : "FAIL"); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("airbreak: FAIL - no break-up\n");
    if (getenv("BRKZOOM")) camZoom = (float)atof(getenv("BRKZOOM"));   // (debug: closer in)
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 5, "crash") == 0) {   // crash<type>_<seconds after>: type flown into the ground in a steep dive at cruise speed
    int idx = 1; float after = 1.f; sscanf(name.c_str() + 5, "%d_%f", &idx, &after);
    idx = std::clamp(idx, 0, kAircraftCount - 1);
    if (isResearchAircraft(idx)) { resCraft = idx; resAirborne = true; realTime = 20; launchResearch(); hudOn = false; }
    else plane.reset(&kAircraft[idx], vec3(-960, 0, 23500), 0, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise);
    {   // (over land, and land where it comes down: some 150 m on)
      const vec3 f = normalize(vec3(plane.forward().x, 0, plane.forward().z));
      const vec3 o = g_world.airports.empty() ? vec3(-960, 0, 23500) : g_world.airports[0].pos() + vec3(250, 0, 250);   // (open ground by an airport)
      for (int k = 0; k < 400; k++) {
        const vec3 p0 = vec3(o.x + 97.f * (k % 20), 0, o.z - 113.f * (k / 20)), p1 = p0 + f * 150.f;
        if (g_world.height(p0.x, p0.z) > 15.f && g_world.height(p1.x, p1.z) > 15.f) { plane.pos.x = p0.x; plane.pos.z = p0.z; break; }
      }
    }
    plane.pos.y = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + 180.f;
    plane.q = quat::axisAngle(plane.right(), -50.f * DEG) * plane.q;
    plane.vel = plane.forward() * (isResearchAircraft(idx) ? 250.f : kAircraft[idx].cruise); plane.ctl.throttle = 1; botControl = true; plane.ctl.pitch = 0.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.3f; camZoom = getenv("BRKZOOM") ? (float)atof(getenv("BRKZOOM")) : 1.8f;
    float tb = -1, vHit = 0;
    for (int i = 0; i < 60 * 60; i++) {
      if (!crashed) vHit = length(plane.vel);
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("crash: %s, %d pieces, %.0f m/s\n", plane.ev.crashReason.c_str(), (int)wreck.size(), vHit); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("crash: FAIL - never hit\n");
    if (getenv("WRECKDBG"))   // each piece where it ended: down or not, at rest, how far it dug in, its lowest corner against the ground there
      for (const WreckPiece& w : wreck) {
        float low = 1e9f, gnd = 0.f;
        const vec3 lo = w.body.lo - w.body.cg, hi = w.body.hi - w.body.cg;
        for (int k = 0; k < 8; k++) {
          const vec3 cw = w.c + w.q.rotate(vec3((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z) * 0.9f);
          if (cw.y - wreckGround(cw.x, cw.z) < low) { low = cw.y - wreckGround(cw.x, cw.z); gnd = wreckGround(cw.x, cw.z); }
        }
        printf("  piece kind %d: %s%s, %.2f m dug in, lowest corner %+.2f m from the ground (%.1f m; %.1f under the surface's), %.1f m/s\n", w.kind, w.landed ? "down" : "in the air",
               w.rest ? ", at rest" : "", w.bury, low, gnd, g_world.height(w.c.x, w.c.z, 6) - gnd, length(w.v));
      }
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 3, "ckv") == 0) {   // cockpit view of aircraft N: ckv<N>_<look yaw deg>_<look pitch deg>_<hour>[_<roll input -1..1>[_<flaps 0..1>]][@<x>,<z>]
    int idx = 0; float ly = 0, lpch = -8, hour = 11, roll = 0, fl = 0; sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f_%f", &idx, &ly, &lpch, &hour, &roll, &fl);
    if (idx == kResearchJet) { resAirborne = true; resTime = hour; launchResearch(); }
    else { timeOfDay = hour; plane.reset(&kAircraft[idx], vec3(-4000, 600, 9000), 40, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise); camQ = plane.q; takeoffAnnounced = true; }
    size_t at = name.find('@');   // (ckv...@x,z: out at x, z - by the map's edge, where world-space floats are coarsest)
    float ax = 0, az = 0;
    if (at != std::string::npos && sscanf(name.c_str() + at + 1, "%f,%f", &ax, &az) == 2) { plane.pos = vec3(ax, std::max(g_world.height(ax, az), 0.f) + 600.f, az); camQ = plane.q; }
    camMode = 1; hint.clear(); toasts.clear();
    for (int i = 0; i < 4; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (const char* cs = getenv("CKCTL")) {   // (debug: the controls held at pitch,roll,yaw,throttle - yokes, pedals and levers at their stops,
      // the aircraft held where it was while they get there: flown, full aft stick at cruise breaks a jet up)
      float cp = 0, cr = 0, cy = 0, ct = 0.7f; sscanf(cs, "%f,%f,%f,%f", &cp, &cr, &cy, &ct);
      botControl = true;
      const vec3 p0 = plane.pos, v0 = plane.vel; const quat q0 = plane.q;
      for (int i = 0; i < 20; i++) {
        plane.ctl.pitch = cp; plane.ctl.roll = cr; plane.ctl.yaw = cy; plane.ctl.throttle = ct; plane.engineSpool = ct;
        realTime += 1 / 30.f; update(1 / 30.f);
        plane.pos = p0; plane.vel = v0; plane.q = q0; plane.w = vec3(); plane.gLoad = 1.f;
      }
      gTunnel = 0;
    }
    lookYaw = ly * DEG; lookPitch = lpch * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f;
    if (!getenv("CKCTL")) { plane.ctl.roll = roll; botControl = roll != 0.f || fl != 0.f; }   // (a held roll input: the yokes turn)
    if (fl != 0.f) { plane.ctl.flaps = fl; plane.flaps = fl; }
    toasts.clear(); hint.clear(); return;
  }
  if (name.compare(0, 3, "brk") == 0) {   // an in-flight break-up: brk<aircraft>_<seconds after>_<view yaw deg>: the pieces, seen from 30 m
    int idx = 0; float secs = 0.4f, vy = 90.f; sscanf(name.c_str() + 3, "%d_%f_%f", &idx, &secs, &vy);
    Contract c; c.from = 0; c.to = 1; c.title = "Break-up"; c.wx = Weather(); c.wx.timeOfDay = 13.f; c.wx.cloudCover = 0.1f;
    realTime = 20; startFlight(c, idx, Career::SRC_OWNED);
    plane.reset(&kAircraft[idx], vec3(-4000, 900, 9000), 40, kAircraft[idx].maxFuel * 0.5f, 100, true, kAircraft[idx].cruise * 0.7f);
    takeoffAnnounced = true; camQ = plane.q;
    plane.ev.crashed = true; plane.ev.crashReason = "Structural failure";
    for (float tt = 0; tt < secs + 1 / 30.f; tt += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    vec3 cen; for (auto& w : wreck) cen = cen + w.c; if (!wreck.empty()) cen = cen * (1.f / wreck.size());
    vec3 off(sinf(vy * DEG) * 30.f, 12.f, -cosf(vy * DEG) * 30.f);
    dbgCam = true; dbgFollow = false; dbgCamPos = cen + off; dbgCamLook = cen;
    toasts.clear(); hint.clear(); uiHidden = true; hudOn = false; return;
  }
  if (name.compare(0, 3, "trf") == 0) {   // AI traffic: trf<seconds>_<view> at Solace Capital; view 0 = airport overview, k = chase craft k-1
    float secs = 60; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &secs, &view);
    set.traffic = getenv("NOTRF") == nullptr;
    int ai = g_world.findAirport("CAP"); const Airport& a = g_world.airports[ai];
    float h = a.heading * DEG, sn = sinf(h), cs = cosf(h), side = aptSide(a, ai);
    auto W = [&](float u, float v) { return vec3(a.x + u * sn + v * cs, 0, a.z - u * cs + v * sn); };
    vec3 pp = W(a.length * 0.47f, side * (a.width * 0.5f + 130.f)); pp.y = g_world.height(pp.x, pp.z) + 1.5f;
    plane.reset(&kAircraft[1], pp, a.heading, kAircraft[1].maxFuel, 100, false, 0);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 15.f;
    for (float tt = 0; tt < secs; tt += 1 / 20.f) { realTime += 1 / 20.f; update(1 / 20.f); }
    const char* st[] = {"PARKED", "TAXI_OUT", "HOLD", "LINEUP", "TAKEOFF", "CLIMB", "CIRCUIT", "FINAL", "ROLLOUT", "TAXI_IN", "FLY"};
    const char* rl[] = {"airport", "cruiser", "formation", "stunt", "escort"};
    for (int i = 0; i < (int)traffic.craft.size(); i++) { const TrafficCraft& c = traffic.craft[i];
      printf("trf %2d %-9s %-17s %-8s agl %6.0f spd %5.1f dist %6.0f man %d\n", i, rl[c.role], kAircraft[c.spec].name, st[c.state], c.pos.y - g_world.height(c.pos.x, c.pos.z), c.speed, length(c.pos - plane.pos), c.man); }
    dbgCam = true; hudOn = false; toasts.clear();
    if (view == 0) { dbgCamLook = W(0, side * 60.f); dbgCamLook.y = a.elev; dbgCamPos = W(-a.length * 0.2f, -side * 700.f); dbgCamPos.y = a.elev + 250.f; }
    else if (view - 1 < (int)traffic.craft.size()) { const TrafficCraft& c = traffic.craft[view - 1]; float sz = kAircraft[c.spec].fusLen;
      dbgCamLook = c.pos; dbgCamPos = c.pos + c.q.rotate(vec3(sz * 0.9f, sz * 0.35f, sz * 1.6f)); }
    return;
  }
  if (name == "ufosummon") {   // J + K held for a second while flying summons the UFO
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['J'] = true; in.down['K'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && ufo.on) early = true; }
    in.down['J'] = in.down['K'] = false;
    printf("ufosummon: %s\n", ufo.on && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escsummon") {   // O + P held for a second while flying summons the display pair
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['O'] = true; in.down['P'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && traffic.count(TrafficCraft::ESCORT)) early = true; }
    in.down['O'] = in.down['P'] = false;
    printf("escsummon: %s\n", traffic.count(TrafficCraft::ESCORT) == 2 && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escdismiss") {   // O + P again mid-helix: the pair blends out of the act, waves and leaves without jumps
    plane.reset(&kAircraft[1], vec3(-4000, 900, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worstJump = 0; std::vector<vec3> last;
    for (float x = 0; x < 50.f; x += 1 / 60.f) {
      if (fabsf(x - 27.f) < 1e-3f) traffic.dismissEscort();
      realTime += 1 / 60.f; update(1 / 60.f);
      std::vector<vec3> now; std::vector<float> spd;
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT) { now.push_back(c.pos); spd.push_back(c.speed); }
      if (now.size() == last.size()) for (size_t i = 0; i < now.size(); i++) worstJump = std::max(worstJump, length(now[i] - last[i]) * 60.f / std::max(spd[i], 30.f));
      last = now;
    }
    // a frame's step must match the jet's own speed (no teleports when the act is cut short)
    printf("escdismiss: worst step / speed %.2f, %d escorts left %s\n", worstJump, traffic.count(TrafficCraft::ESCORT),
           worstJump < 1.3f && traffic.count(TrafficCraft::ESCORT) == 0 ? "ok" : "FAIL");
    return;
  }
  if (name.compare(0, 3, "esc") == 0) {   // display pair at t seconds: esc<t>_<view>_<aircraft> (0 chase, 1 cockpit, 2 side camera)
    float tt = 20; int view = 0, sp = 1; sscanf(name.c_str() + 3, "%f_%d_%d", &tt, &view, &sp);
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worst = 0;
    for (float x = 0; x < tt; x += 1 / 60.f) {
      realTime += 1 / 60.f; update(1 / 60.f);
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT && length(c.vel) > 1.f) worst = std::max(worst, acosf(clampf(dot(c.q.rotate(vec3(0, 0, -1)), normalize(c.vel)), -1.f, 1.f)) / DEG);
    }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT)
      printf("esc: act %d t %.1f  jet %+.0f  %.0f m from the player  %.0f m/s  agl %.0f\n", traffic.escAct, traffic.escT, c.escSide, length(c.pos - plane.pos), c.speed, c.pos.y - g_world.height(c.pos.x, c.pos.z));
    printf("esc: worst nose-off-path %.1f deg %s\n", worst, worst < 2.f ? "ok" : "FAIL");
    toasts.clear();
    if (view == 1) { camMode = 1; lookYaw = 0; lookPitch = -0.05f; camYaw = 0; camPitch = 0.12f; }
    else if (view == 2) { hudOn = false; dbgCam = true; vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0))); dbgCamPos = plane.pos + r * 160.f + vec3(0, 40, 0) - f * 60.f; dbgCamLook = plane.pos + f * 60.f; }
    return;
  }
  if (name.compare(0, 3, "ufo") == 0) {   // UFO encounter at t seconds: ufo<t>_<view> (0 chase-style, 1 cockpit, 2 close-up of the hatch)
    float tt = 12; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &tt, &view);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    botControl = true; plane.ctl.throttle = 0.75f;
    startUfo(); ufo.side = 1.f;
    for (float x = 0; x < tt; x += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    toasts.clear(); hudOn = false;
    if (view == 1) { camMode = 1; lookYaw = -80.f * DEG; lookPitch = 0.f; camYaw = lookYaw; camPitch = 0.12f; }
    else { dbgCam = true; dbgCamLook = ufo.pos + ufo.up * 1.5f;
      dbgCamPos = view == 2 ? ufo.pos + ufo.right * 9.f + ufo.up * 2.5f : plane.pos - ufo.right * 3.f + ufo.up * 2.f; }
    printf("ufo: t %.1f hatch %.2f laugh %.2f wave %.2f, %.1f m from the player\n", ufo.t, ufo.hatch, ufo.laugh, ufo.wave, length(ufo.pos - plane.pos));
    return;
  }
  if (name.compare(0, 3, "xrf") == 0) {   // XR-30 formation pass: xrf<seconds after spawn>; camera at the player looking at the leader
    float secs = 20; sscanf(name.c_str() + 3, "%f", &secs);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = 14.f;
    traffic.reset(); traffic.spawnFormation(plane.pos, vec3());
    for (float tt = 0; tt < secs; tt += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::FORMATION && c.leader < 0) {
      printf("xrf: leader %.0f m from the player, %.0f m/s\n", length(c.pos - plane.pos), c.speed);
      dbgCam = true; dbgCamPos = plane.pos + vec3(0, 3, 0); dbgCamLook = c.pos; break; }
    hudOn = false; toasts.clear(); return;
  }
  if (name.compare(0, 4, "gtun") == 0) {   // g-force lens at a forced strength (percent, held), chase view: gtun<pct>
    resAirborne = true; realTime = 20; launchResearch();
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    gTunnel = gTunnelPin = atof(name.c_str() + 4) / 100.f; toasts.clear(); return;
  }
  // loading-screen pictures (--loadshots): loadshot_<CODE> an airport from an elevated three-quarter view with the aircraft
  // on its runway; loadshot_air_<n> aircraft n in flight, filmed from alongside. Afternoon light, a little cloud.
  if (name.compare(0, 13, "loadshot_air_") == 0) {
    static const char* kSpot[kAircraftCount] = {"MDB", "PMB", "ORC", "KLO", "FJH", "CAP", "PVI", "VCF", "LHK", "SMP", "GLS", "NPT", "HFS", "ORC", "CAP", "GLR", "VBY"};   // a different place each (the Kittiwake low over the sea by Gull Rock, the XR-50 over the bay)
    int n = std::clamp(atoi(name.c_str() + 13), 0, kAircraftCount - 1);
    resCraft = n; resAirborne = true; resTime = 16.3f; resWx = 0; resAirport = std::max(0, g_world.findAirport(kSpot[n]));
    launchResearch();
    {   // at its own cruise speed, holding height (the research launch's 200 m/s tears a light aircraft apart)
      const Airport& a = g_world.airports[resAirport];
      vec3 p = a.pos() + a.dir() * 1500.f; p.y = std::max(a.elev, g_world.height(p.x, p.z)) + 450.f;
      plane.reset(&kAircraft[n], p, a.heading, kAircraft[n].maxFuel, 85, true, kAircraft[n].cruise);
      plane.ctl.throttle = 0.7f; botControl = true; plane.apEngage(Plane::AP_HOLD, -1, wx);
    }
    wx.cloudCover = 0.3f; realTime = 20;
    for (int i = 0; i < 20; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    float size = std::max(plane.spec->span, plane.spec->fusLen), R = size * 1.5f + 6.f;
    dbgCam = true; dbgFollow = true;
    dbgFollowOff = plane.right() * (R * 0.85f) + plane.forward() * (R * 0.55f) + vec3(0, R * 0.16f, 0);
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 4, "gav_") == 0) {   // an aircraft parked on the runway, orbit view: gav_<spec>_<yaw>_<pitch>_<dist>[_<gear>[_<lift m>]]
    int sp = 0; float yawD = 120, pitD = 10, dist = 0, gearAt = -1, lift = 0;
    sscanf(name.c_str() + 4, "%d_%f_%f_%f_%f_%f", &sp, &yawD, &pitD, &dist, &gearAt, &lift);
    sp = std::clamp(sp, 0, kAircraftCount - 1);
    Contract c; c.from = g_world.findAirport("CAP"); c.to = g_world.findAirport("MDB"); c.title = "Aircraft check";
    c.wx = Weather(); c.wx.timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.5f; c.wx.cloudCover = 0.2f; c.wx.visibility = 60000;
    realTime = 20;
    if (kAircraft[sp].special) { resCraft = sp; resAirport = c.from; resAirborne = false; resTime = c.wx.timeOfDay; resWx = 0; resCard = -1; launchResearch(); camMode = 0; }   // (the research craft as their flights show them; from outside: their flights start in the cockpit, and its cabin alone was drawn)
    else startFlight(c, sp, Career::SRC_OWNED);
    for (int i = 0; i < 30; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (dist <= 0) dist = kAircraft[sp].span * 1.15f;
    float y = yawD * DEG, pt = pitD * DEG;
    vec3 off = (plane.forward() * cosf(y) + plane.right() * sinf(y)) * (dist * cosf(pt)) + vec3(0, dist * sinf(pt), 0);
    dbgCam = true; dbgFollow = true; dbgFollowOff = off; dbgCamPos = plane.pos + off; dbgCamLook = plane.pos;
    toasts.clear(); hint.clear(); uiHidden = true; hudOn = false;
    camMode = getenv("GAVOUT") ? 0 : 1;   // (the cabin model shows the interior through the windows; GAVOUT: the outside model)
    if (const char* cs = getenv("GAVCTL")) {   // (debug: the controls held at pitch,roll,yaw,flaps - the surfaces' directions)
      float cp = 0, cr = 0, cy = 0, cf = 0; sscanf(cs, "%f,%f,%f,%f", &cp, &cr, &cy, &cf);
      botControl = true; plane.ctl.pitch = cp; plane.ctl.roll = cr; plane.ctl.yaw = cy; plane.ctl.flaps = cf; plane.flaps = cf;
      const int n = getenv("GAVCTLN") ? atoi(getenv("GAVCTLN")) : 60;   // (the settle time: the controls' smoothing)
      for (int i = 0; i < n; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    }
    if (const char* th = getenv("GAVTHR")) {   // (debug: the engines running at this throttle - reheat from 0.85 on the jets that have it)
      const float t = clampf((float)atof(th), 0.f, 1.f);
      botControl = true; plane.engineRunning = true; plane.ctl.throttle = t; plane.engineSpool = t; plane.ctl.brake = 1;
      plane.rpm = plane.spec->engineType == ENG_JET ? 24.f + 76.f * t : plane.spec->maxRpm * 0.9f; plane.n1 = 24.f + 76.f * t;
    }
    if (gearAt >= 0) {   // (the gear held part way: lifted clear of the runway for the few frames that follow)
      plane.gear = std::min(gearAt, 1.f); plane.ctl.gearDown = gearAt >= 0.5f; plane.pos.y += 2.f + lift; plane.vel = vec3();   // (lift: high enough to look up at its underside)
      dbgCamPos = plane.pos + off;
    }
    return;
  }
  if (name.compare(0, 5, "wcam_") == 0) {   // a free camera over the world: wcam_<x>_<z>_<height above ground>_<yaw>_<pitch>[_<hour>] (m, deg; yaw 0 north)
    float x = 0, z = 0, hgt = 300, yaw = 0, pitch = -30, tod = 11;
    sscanf(name.c_str() + 5, "%f_%f_%f_%f_%f_%f", &x, &z, &hgt, &yaw, &pitch, &tod);
    Contract c; c.from = 0; c.to = 1; c.title = "World";
    c.wx = Weather(); c.wx.timeOfDay = tod; c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; c.wx.windSpeed = 4; c.wx.windFrom = 200;
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    vec3 cam(x, std::max(g_world.height(x, z), 0.f) + hgt, z);
    vec3 dir(sinf(yaw * DEG) * cosf(pitch * DEG), sinf(pitch * DEG), -cosf(yaw * DEG) * cosf(pitch * DEG));
    dbgCam = true; dbgFollow = false; dbgCamPos = cam; dbgCamLook = cam + dir * 100.f;
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 4, "apv_") == 0 && name.size() >= 9) {   // airport detail views: apv_<CODE>_<view>_<hour>
    int ai = std::max(0, g_world.findAirport(name.substr(4, 3).c_str()));
    int view = atoi(name.c_str() + 8);
    size_t us = name.find('_', 8);
    float tod = us != std::string::npos ? (float)atof(name.c_str() + us + 1) : 15.f;
    Contract c; c.from = ai; c.to = (ai + 1) % (int)g_world.airports.size(); c.title = "Airport";
    c.wx = Weather(); c.wx.timeOfDay = tod; c.wx.cloudCover = 0.25f; c.wx.visibility = 60000; c.wx.windSpeed = 6; c.wx.windFrom = 200;
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    airportView(ai, view, dbgCamPos, dbgCamLook);
    dbgCam = true; dbgFollow = false;
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 9, "loadshot_") == 0) {
    int ai = std::max(0, g_world.findAirport(name.substr(9).c_str()));
    Contract c; c.from = ai; c.to = (ai + 1) % (int)g_world.airports.size(); c.title = "Airport";
    c.wx = Weather(); c.wx.timeOfDay = 16.3f; c.wx.cloudCover = 0.3f; c.wx.visibility = 60000;
    const Airport& a = g_world.airports[ai];
    realTime = 20; startFlight(c, a.water() ? kKittiwake : 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    vec3 ctr = a.pos(), d = a.dir(), r = normalize(cross(d, vec3(0, 1, 0)));
    float len = a.length;
    vec3 cam = ctr + r * (len * 0.3f + 200.f) - d * (len * 0.4f + 120.f);
    cam.y = std::max(a.elev, g_world.height(cam.x, cam.z)) + 90.f + len * 0.03f;
    vec3 look = ctr + d * (len * 0.05f) + vec3(0, 5.f, 0);
    if (a.water() && a.dockLen > 0.f) {   // a seaplane base: from the shore beside its dock, on the berth's side, the aircraft
      // moored there with the water beyond
      const vec3 o(a.dockDirX, 0.f, a.dockDirZ), side = normalize(cross(o, vec3(0, 1, 0)));
      cam = vec3(a.dockX, 0.f, a.dockZ) - o * 10.f + side * 32.f;
      cam.y = std::max(g_world.height(cam.x, cam.z), 0.f) + 9.f;
      look = a.berth(4.6f) + o * 4.f + vec3(0, 1.2f, 0);
    }
    dbgCam = true; dbgFollow = false; dbgCamPos = cam; dbgCamLook = look;
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 5, "wtko_") == 0) {   // the Kittiwake's take-off run on Verde Bay's lane, t seconds in, from the chase camera: wtko_<s>[_<camYaw>[_<type>]]
    // (another type: on its floats from the hangar's mechanic, docs/HANGAR.md)
    float t = 8.f, yaw = 0.6f; int type = kKittiwake;
    sscanf(name.c_str() + 5, "%f_%f_%d", &t, &yaw, &type);
    if (!validAircraft(type) || (type != kKittiwake && !floatKitFor(type))) type = kKittiwake;
    const int ai = std::max(0, g_world.findAirport("VBY")); const Airport& a = g_world.airports[ai];
    Contract c; c.from = c.to = ai; c.title = "Water";
    c.wx = Weather(); c.wx.timeOfDay = 15.5f; c.wx.windSpeed = 4; c.wx.windFrom = a.heading; c.wx.cloudCover = 0.3f; c.wx.visibility = 60000;
    freeFloats = true; realTime = 20; startFlight(c, type, Career::SRC_NONE);
    const AircraftSpec& k = fitted(type, Fit{true, 0});
    plane.reset(&k, a.threshold(false) + a.dir() * 60.f, a.heading, k.maxFuel * 0.6f, 500.f, false);
    plane.engineRunning = true; engineAutoStarted = true; parkingBrake = false; botControl = true;
    for (float s = 0; s < t; s += 1 / 30.f) {
      plane.ctl.throttle = 1; plane.ctl.flaps = k.takeoffFlap; plane.ctl.gearDown = false; plane.ctl.yaw = 0;
      plane.ctl.pitch = clampf((6.f - plane.pitchDeg()) * 0.08f - plane.w.x * 0.5f, -1.f, 1.f); plane.ctl.roll = clampf(-plane.bankDeg() * 0.05f, -1.f, 1.f);
      realTime += 1 / 30.f; update(1 / 30.f);
    }
    camMode = 2; camYaw = yaw; camPitch = 0.16f; camZoom = 0.55f;
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 3, "apt") == 0 && name.size() >= 6) {   // on the runway of an airport: apt<CODE>, e.g. aptHFS
    int ai = g_world.findAirport(name.substr(3, 3).c_str());
    Contract c; c.from = std::max(ai, 0); c.to = (c.from + 1) % (int)g_world.airports.size(); c.title = "Airport check";
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 30; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    toasts.clear(); return;
  }
  if (name == "loading" || name == "loadingready" || name == "loadingair") {   // the pre-flight loading screen: card / live shot
    if (name == "loadingair") { resAirborne = true; resCraft = kResearchJet; launchResearch(); }
    else { Contract c = career.board.empty() ? Contract() : career.board[0]; if (career.board.empty()) { c.from = 0; c.to = 1; c.title = "Lesson 1: Takeoff and Climb"; } startFlight(c, 0, Career::SRC_OWNED); }
    screen = SCR_LOADING; loadT = 0; loadReadyT = -1; loadMap = loadBackdrop = false; toasts.clear(); hint.clear();
    if (name != "loading") { loadT = 3.f; loadReadyT = 0.f; loadShown = 1.f; }
    return;
  }
  if (name == "seacrash") {  // ditches into deep sea: the wreck must float briefly, then sink to the seabed
    float gx = 0, gz = 0;
    for (int i = 0; i < 4000 && g_world.height(gx, gz) > -25.f; i++) { gx = -30000.f + (i % 63) * 1000.f; gz = -30000.f + (i / 63) * 1000.f; }
    plane.reset(&kAircraft[1], vec3(gx, 50.f, gz), 30, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[1].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    float hiW = -1e9f, hiD = -1e9f;
    for (int i = 0; i < 60 * 40; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (i == 60 * 2) { for (auto& w : wreck) hiW = std::max(hiW, w.c.y); }
    }
    float topW = -1e9f; for (auto& w : wreck) topW = std::max(topW, w.c.y);
    for (auto& d : debris) hiD = std::max(hiD, d.p.y);
    printf("seacrash: seabed %.0f m, highest piece %.1f m at 2 s, %.1f m at 40 s, highest fragment %.1f m %s\n", g_world.height(gx, gz), hiW, topW, hiD,
           crashed && hiW > -2.f && topW < -3.f && hiD < -3.f ? "ok" : "FAIL");
    toasts.clear(); return;
  }
  if (name.compare(0, 5, "crash") == 0) {
    // flies into a field nose-down, then advances the wreck simulation by the given number of tenths of a second
    int sp = name.size() > 6 ? name[5] - '0' : 1;
    float after = name.size() > 7 ? atof(name.c_str() + 7) * 0.1f : 1.f;
    float gx = -9000, gz = 11500;
    plane.reset(&kAircraft[sp], vec3(gx, g_world.height(gx, gz) + 60.f, gz), 30, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[sp].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    for (float tt = 0; tt < after; tt += 1 / 60.f) { realTime += 1 / 60.f; crashTimer = std::min(crashTimer, 5.f); update(1 / 60.f); }
    toasts.clear();
    return;
  }
  if (name == "top") { camMode = 2; camYaw = 0.3f; camPitch = 1.35f; camZoom = 4.f; }
  if (name == "orbit") { camMode = 2; camYaw = 2.3f; camPitch = 0.25f; camZoom = 0.6f; timeOfDay = 9.0f; }
  for (int i = 0; i < 30; i++) updateCamera(0.1f);
  if (name == "hud" && getenv("PADPROMPTS")) { in.pad = true; padActive = true; }   // (PADPROMPTS: the hints as a controller shows them)
  if (name == "hud") hint = contract.hints.size() ? expandHint(contract.hints[2], padPrompts(), true) : "";
  if (name == "gps" || name == "pause" || name == "minimap") {
    plane.reset(&kAircraft[1], vec3(-4000, 600, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
    for (int i = 0; i < 60; i++) trail.push_back(vec2(-4000 - sinf(40 * DEG) * i * 110.f + sinf(i * 0.1f) * 300.f, 9000 + cosf(40 * DEG) * i * 110.f));
    if (name == "gps") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    if (name == "pause") paused = true;
    if (name == "minimap") showMinimap = true;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "at" && getenv("CAMPITCH")) { camPitch = (float)atof(getenv("CAMPITCH")) * DEG; camZoom = 2.5f; for (int i = 0; i < 30; i++) updateCamera(0.1f); }
  if (name == "at" && getenv("CAMYAW")) {   // CAMYAW=<deg>: the orbit camera that far round from dead astern (CAMZOOM: its distance)
    camMode = 2; camYaw = (float)atof(getenv("CAMYAW")) * DEG; camPitch = getenv("CAMPITCH") ? (float)atof(getenv("CAMPITCH")) * DEG : .12f;
    camZoom = getenv("CAMZOOM") ? (float)atof(getenv("CAMZOOM")) : .5f; for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "seamgps") { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = 25000.f; }
  if (name == "longhaul" || name == "longhaulgps") {   // a long haul under way: an ATP's Emissary 20 km out of Port Verde
    int jet = -1; for (int t : kCareerAircraft) if (!strcmp(kAircraft[t].id, "emissary")) jet = t;
    career = Career{}; career.newGame(); career.license = LIC_ATP; career.money = 400000; career.location = g_world.findAirport("PVI");
    career.fleet = {{jet, career.location, kAircraft[jet].maxFuel, 1.f}}; career.refreshBoard();
    for (const Contract& c : career.board) if (c.longHaul() && career.canFly(c, jet) == Career::SRC_OWNED) { botControl = false; beginCareerFlight(c, jet, Career::SRC_OWNED); break; }
    if (haul.on) {
      const Airport& a = g_world.airports[contract.from];
      vec3 to(dest().x - a.x, 0, dest().z - a.z); to = to * (1.f / std::max(length(to), 1.f));
      plane.reset(plane.spec, a.pos() + to * 20000.f + vec3(0, 3000, 0), atan2f(to.x, -to.z) / DEG, plane.spec->maxFuel * 0.8f, 100, true, plane.spec->cruise);
      settleAirborneStart(); takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
      if (name == "longhaulgps") { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = 120000.f; }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      printf("Long haul scene: %s, %.0f km to %s %s\n", contract.title.c_str(), haul.job.haulKm, dest().code, dest().name);
    }
  }
  if (name == "gpsap" || name == "apfinal" || name == "apvtol") {   // autopilot: GPS autoland pick, then the approach
    int sp = name == "apvtol" ? 8 : 4;   // (the XR-40: the research jet that lands vertically)
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise * 0.8f);
    plane.engineRunning = true; plane.engineSpool = 0.7f; plane.ctl.throttle = 0.7f;
    takeoffAnnounced = true; engineAutoStarted = true; camQ = plane.q; hint.clear();
    apDest = g_world.findAirport("CAP"); engageAutopilot();
    int target = name == "gpsap" ? -1 : name == "apfinal" ? Plane::APS_FINAL : Plane::APS_HOVER;
    for (int i = 0; i < (target < 0 ? 90 : 60 * 1500); i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (target >= 0 && plane.apStage == target && plane.apStageT > (target == Plane::APS_HOVER ? 14.f : 25.f)) break;
    }
    toasts.clear();
    if (name == "gpsap") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
}
