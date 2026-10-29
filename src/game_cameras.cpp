// Somnium Volandi - the cameras: in flight, the menu tour, the preview stage and every menu's view of it
#include "road_traffic.h"
#include "dev_switches.h"
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

// ------------------------------------------------------------------ camera
void Game::updateCamera(float dt) {
  if (!plane.spec) return;
  if (actPressed(ACT_CAMERA)) {
    camMode = (camMode + 1) % 4; camYaw = 0; camPitch = 0.12f; lookYaw = 0; lookPitch = -0.13f;
    hudOn = set.hudCam[camMode];   // (the HUD as it was last left in this view)
    static const char* names[] = {"Chase camera", "Cockpit view", "Orbit camera", "Flyby camera"};
    toast(names[camMode]);
    if (camMode == 3) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 350.f + plane.right() * 40.f + vec3(0, 12, 0);
  }
  if (in.wheel != 0 && showMap) gpsRangeTarget = clampf(gpsRangeTarget * powf(0.8f, in.wheel), 1500.f, 40000.f);
  else if (in.wheel != 0 && !showRadio && camMode == 1) ckZoomT = clampf(ckZoomT * powf(1.2f, in.wheel), 1.f, 4.f);
  else if (in.wheel != 0 && !showRadio) camZoom = clampf(camZoom * powf(0.88f, in.wheel), 0.35f, 4.f);
  bool drag = in.mDown[1] || (camMode == 2 && in.mDown[0]);
  // chase and orbit cameras use reversed pitch: stick up / drag up swings the camera down so the view tilts up
  float pitchDir = (camMode == 0 || camMode == 2) ? -1.f : 1.f;
  float lookK = camMode == 1 ? 1.f / ckZoom : 1.f;   // zoomed in: finer look control
  if (drag) { camYaw -= in.mdx * 0.005f * set.mouseSens * lookK; camPitch = clampf(camPitch + pitchDir * in.mdy * 0.004f * set.mouseSens * lookK, -1.3f, 1.4f); }
  if (in.pad) { float rx = fabsf(in.rx) > 0.2f ? in.rx : 0, ry = fabsf(in.ry) > 0.2f ? in.ry : 0; camYaw -= rx * 2.f * dt; camPitch = clampf(camPitch + pitchDir * ry * 1.5f * dt, -1.3f, 1.4f); }
  camIdle = drag || (in.pad && (fabsf(in.rx) > 0.2f || fabsf(in.ry) > 0.2f)) ? 0.f : camIdle + dt;
  const AircraftSpec& s = *plane.spec;
  float size = std::max(s.fusLen, s.span);
  if (camMode == 0) {
    // the camera stays where it was swung until the view is changed (C) - or, with Settings' recentre, it eases back
    // behind the aircraft once left alone for 4 s (a brushed right stick lost the heading: the review of v3.50.3)
    if (set.camRecentre && camIdle > 4.f) {
      const float k = 1.f - expf(-dt * 1.6f);
      camYaw = wrapAngle(camYaw) * (1.f - k); camPitch += (0.12f - camPitch) * k;
    }
    // follow orientation with lag; reduce roll so the horizon isn't nauseating
    vec3 f = plane.forward();
    if (length(plane.vel) > 15.f && !plane.onGround) f = normalize(lerp(f, normalize(plane.vel), 0.5f));
    float yaw = atan2f(f.x, -f.z), pitch = asinf(clampf(f.y, -1, 1));
    quat target = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), pitch * 0.85f);
    camQ = slerp(camQ, target, 1.f - expf(-4.f * dt));
    // spring arm: it lengthens with airspeed (relative to the type's cruise) so the aircraft sits smaller on screen
    // the faster it goes, stretches when the aircraft accelerates away and compresses when it slows, through a
    // slightly under-damped spring
    float base = (size * 0.85f + 5.f) * camZoom;
    float spd = length(plane.vel), sPrev = camSpd;
    camSpd = camArm <= 0.f ? spd : camSpd + (spd - camSpd) * (1.f - expf(-dt * 3.f));
    float acc = dt > 1e-4f ? (camSpd - sPrev) / dt : 0.f;
    float rel = spd / std::max(s.cruise, 20.f);
    float armT = base * (0.9f + 0.45f * std::min(rel, 1.f) + 0.3f * clampf(rel - 1.f, 0.f, 2.f) + clampf(acc * 0.025f, -0.12f, 0.25f));
    if (camArm <= 0.f) { camArm = armT; camArmV = 0; }
    const float wn = 3.2f, zeta = 0.8f;   // natural frequency (rad/s), damping
    camArmV += (wn * wn * (armT - camArm) - 2.f * zeta * wn * camArmV) * dt;
    camArm += camArmV * dt;
    float dist = std::max(camArm, base * 0.7f);
    quat orbit = camQ * quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist)) + vec3(0, s.fusRad * 0.6f, 0);
  } else if (camMode == 1) {
    if (drag || in.pad) { lookYaw = camYaw; lookPitch = camPitch - 0.12f; }
    else { float rest = plane.spec->special ? -0.24f : -0.13f;   // XR-30: rest the view so the instrument console is in sight
      // head-look: the eyes lead a turn a little (into the bank, and towards the nose when it pitches up)
      float leadYaw = set.headLook && !plane.onGround ? clampf(-plane.bankDeg() / 60.f, -1.f, 1.f) * 0.30f : 0.f;
      float leadPitch = set.headLook && !plane.onGround ? clampf(plane.pitchDeg() / 30.f, -0.5f, 0.5f) * 0.10f : 0.f;
      lookYaw = approach(lookYaw, leadYaw, 2.f, dt); lookPitch = approach(lookPitch, rest + leadPitch, 2.f, dt); camYaw = lookYaw; camPitch = lookPitch + 0.12f; }
    camPos = plane.pos + plane.q.rotate(kModels[typeOf(plane.spec)].eye);
  } else if (camMode == 2) {
    float dist = (size * 1.4f + 8.f) * camZoom;
    quat orbit = quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist));
  } else {
    if (!botControl && length(camPos - plane.pos) > 700.f) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 400.f + plane.right() * 45.f + vec3(0, 8, 0);
  }
  float gh = std::max(g_world.height(camPos.x, camPos.z, 6), 0.f) + 1.5f;
  if (camPos.y < gh) camPos.y = gh;
  // Use the same body-space viewing ray as rendering, never a projected/zoomed screen position.
  // Both mouse drag and controller look reach this path; no camera rotation or eye movement is added.
  const int focusModel = typeOf(plane.spec);
  CockpitFocusTarget focusPanels[24];
  const int focusCount = camMode == 1 ? modelCockpitFocusTargets(focusModel, focusPanels, 24) : 0;
  const vec3 focusAim = quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(
      quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1)));
  const float manualZoom = actDown(ACT_ZOOM) ? std::max(ckZoomT, 2.8f) : ckZoomT;
  ckZoom = cockpitFocus.update(dt, camMode == 1, focusModel, set.cockpitFocusZoom && !dbgCam,
      plane.q.conj().rotate(camPos - plane.pos), focusAim, focusPanels, focusCount, manualZoom);
}

static const MenuShot kMenuShots[] = {
  {"PVI", 3, 9.0f, 0.30f, 0, 260.f, 35.f},    // west-coast port, morning, side chase
  {"VCF", 7, 17.6f, 0.25f, 1, 520.f, -20.f},  // the volcano at sunset, an XR-30 roaring past a fixed camera
  {"PMB", 1, 12.5f, 0.20f, 3, 150.f, 60.f},   // the lagoon at noon, trailing chase
  {"SMP", 4, 16.5f, 0.35f, 0, 320.f, -45.f},  // the Spine mountains
  {"CAP", 5, 19.1f, 0.30f, 2, 650.f, 15.f},   // the capital at dusk, lights coming on, high orbit
  {"FJH", 2, 11.0f, 0.45f, 1, 260.f, 80.f},   // the fjord
  {"LHK", 8, 7.0f, 0.25f, 0, 200.f, -70.f},   // Lighthouse Key at dawn, the XR-40
  {"MDB", 0, 14.0f, 0.35f, 3, 210.f, 25.f},   // Meadowbrook farmland
};
const MenuShot& menuShot(float t) { int n = (int)floorf(t / kMenuShotLen); return kMenuShots[((n % 8) + 8) % 8]; }

int Game::hubField() const { return g_world.regionId == career.region && career.location < (int)g_world.airports.size() ? career.location : -1; }
vec3 Game::hubOrbitAt(vec3 centre) const { const float t = realTime * kHubOrbitRate; return centre + vec3(cosf(t) * kHubOrbitR, 0, sinf(t) * kHubOrbitR); }

void Game::menuTour(FrameParams& fp) {
  Plane& demo = cams.tourDemo; int& lastCraft = cams.tourCraft;
  MenuShot S = menuShot(realTime);
  if (prewarmCraft >= 0) S.craft = prewarmCraft;
  float u = realTime - floorf(realTime / kMenuShotLen) * kMenuShotLen;
  int ai = std::max(0, g_world.findAirport(S.ap));
  const Airport& a = g_world.airports[ai];
  if (lastCraft != S.craft) { demo.reset(&kAircraft[S.craft], a.pos() + vec3(0, 500, 0), 0, kAircraft[S.craft].maxFuel, 100, true, kAircraft[S.craft].cruise); lastCraft = S.craft; }
  {   // the next place: its scenery is built in the background while this one plays
    const MenuShot& N = menuShot(realTime + kMenuShotLen);
    fp.prefetchOn = true; fp.prefetchPos = g_world.airports[std::max(0, g_world.findAirport(N.ap))].pos();
  }
  // and the preview stage at the hub's field, which every menu past this one cuts to (the hub's orbit of its field,
  // the hangar, Free Flight, the research terminal): kept resident through the tour, so no cut builds it - what stands
  // within 700 m of a camera is built on the game's thread when missing, some three dozen chunks in a cut's first frame
  if (const PreviewStage st = previewStage(); st.ok) { fp.prefetchNearR = kStageKeepR; fp.prefetchNearPos = st.ground; }
  // the aircraft passes over the airfield at mid-shot, on a heading turned from the runway's
  float hd = a.heading + S.turn;
  vec3 dir(sinf(hd * DEG), 0, -cosf(hd * DEG)), right = normalize(cross(dir, vec3(0, 1, 0)));
  float v = std::min(kAircraft[S.craft].cruise, kAircraft[S.craft].engineType == ENG_JET ? 140.f : 75.f);
  vec3 C = a.pos();
  vec3 p = C + dir * ((u - kMenuShotLen * 0.5f) * v);
  float g = std::max({g_world.height(p.x, p.z), g_world.height(p.x + dir.x * 400.f, p.z + dir.z * 400.f), g_world.height(p.x - dir.x * 400.f, p.z - dir.z * 400.f), 0.f});
  p.y = std::max(C.y + S.alt, g + S.alt * 0.7f);
  float bank = 6.f * sinf(u * 0.35f);
  demo.pos = p;
  demo.q = quat::axisAngle(vec3(0, 1, 0), -hd * DEG) * quat::axisAngle(vec3(0, 0, 1), -bank * DEG) * quat::axisAngle(vec3(1, 0, 0), 1.5f * DEG);
  demo.spec = &kAircraft[S.craft];
  demo.vel = dir * v; demo.onGround = false;
  demo.rpm = 2400; demo.gear = 0.f; demo.flaps = 0; demo.nozzle = 0; demo.ctl = Controls(); demo.ctl.throttle = 0.7f;
  demo.engineRunning = true; demo.engineSpool = 0.75f;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, prewarmInside);
  float size = std::max(demo.spec->span, demo.spec->fusLen), R = size * 2.4f + 10.f;
  vec3 up(0, 1, 0), cam, look;
  if (S.cam == 0) { cam = p + right * (R * 0.9f) + dir * (R * (0.45f - 0.03f * u)) + up * (R * 0.16f); look = p + dir * (size * 0.3f); fp.fovY = 50.f * DEG; }
  else if (S.cam == 1) {   // a fixed camera beside the path, panning as it flies past
    vec3 pm = C + dir * 0.f; pm.y = p.y;
    cam = pm + right * (40.f + size * 3.f) + dir * 90.f + up * -(size * 0.6f);
    cam.y = std::max(cam.y, g_world.height(cam.x, cam.z) + 15.f);
    look = p; fp.fovY = 38.f * DEG;
  } else if (S.cam == 2) {   // high slow orbit over the airfield
    float o = u * 0.03f + 0.6f;
    cam = C + vec3(cosf(o) * 1900.f, S.alt + 450.f, sinf(o) * 1900.f);
    cam.y = std::max(cam.y, g_world.height(cam.x, cam.z) + 300.f);
    look = C + vec3(0, 60.f, 0); fp.fovY = 46.f * DEG;
  } else { cam = p - dir * (R * 1.5f) + up * (R * 0.32f) + right * (R * 0.22f); look = p + dir * (size * 2.f); fp.fovY = 52.f * DEG; }
  fp.camPos = cam;
  vec3 fwd = normalize(look - cam);
  fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, up)); fp.camUp = cross(fp.camRight, fwd);
  fp.vignette = 0.9f;
  fp.fade = smoothstepf(0.f, 0.9f, u) * smoothstepf(kMenuShotLen, kMenuShotLen - 0.9f, u);   // cut through black
}

// The hangar's stand (docs/HANGAR.md): where the selected aircraft is shown in the live world - on the apron in front of
// one of an airport's own hangars, nose out of its door, its tail a few metres short of it. The aircraft turns in place
// there (the preview's turntable), so the circle it sweeps must be clear of everything the airport has placed (its
// parked aircraft, floodlight masts, vehicles, other buildings), and so must the way to the camera, out over the apron
// at the preview's azimuth. Of the hangars that leave both clear, the biggest building is chosen.
struct HangarStand { bool ok = false; vec3 ground; float heading = 0; };
static HangarStand hangarStandAt(int ai, const AircraftSpec& s) {
  HangarStand best; float bestScore = -1e9f;
  if (ai < 0 || ai >= (int)g_world.airports.size() || g_world.airports[ai].water()) return best;
  const Airport& a = g_world.airports[ai];
  std::vector<AptItem> items; airportItems(ai, items);
  const float swept = 0.5f * std::max(s.span, s.fusLen) * 1.05f + 1.f;   // (the turntable's circle, a metre to spare)
  // the clearance of a circle from an item's footprint (its box turned by its yaw: Placer::put)
  auto clearOf = [&](const AptItem& it, vec3 c, float r) {
    const EntKindInfo& I = kEntInfo[it.kind];
    const float hx = I.hx * it.e.sx, hz = I.hz * it.e.sz;
    if (std::max(hx, hz) < 0.4f || I.h * it.e.sy < 0.6f) return true;   // (edge lights, buoys: flat to the eye)
    const float dx = c.x - it.e.x, dz = c.z - it.e.z, cs = cosf(it.e.yaw), sn = sinf(it.e.yaw);
    const float lx = cs * dx - sn * dz, lz = sn * dx + cs * dz;
    const float ox = std::max(fabsf(lx) - hx, 0.f), oz = std::max(fabsf(lz) - hz, 0.f);
    return ox * ox + oz * oz > r * r;
  };
  for (size_t h = 0; h < items.size(); h++) {
    const AptItem& hg = items[h];
    if (hg.kind != EK_HANGAR && hg.kind != EK_ARCH_HANGAR && hg.kind != EK_T_HANGAR) continue;
    const EntKindInfo& I = kEntInfo[hg.kind];
    // (every hangar faces the runway: Placer::facing - out of its door is across the runway's line, towards it)
    const vec3 rel = vec3(hg.e.x - a.x, 0, hg.e.z - a.z), along = a.dir();
    vec3 out = -(rel - along * dot(rel, along));
    if (length(out) < 1.f) continue;
    out = normalize(out);
    const vec3 door = vec3(hg.e.x, 0, hg.e.z) + out * (I.hz * hg.e.sz);
    const vec3 c = door + out * (swept + 2.f);
    bool clear = true;
    for (size_t j = 0; j < items.size() && clear; j++) if (j != h) clear = clearOf(items[j], c, swept);
    // the way to the camera: out over the apron at the preview's azimuth (hangarApronCamera), as far as its lens puts it
    // (some 4.4 lengths: the craft fills the stage through a long lens) - what stands above the lowest sight line there
    const float az = MenuPreviewView::defaultYaw, reach = 4.6f * std::max(s.span, s.fusLen) + 10.f;
    const vec3 toCam = normalize(out * cosf(az) + vec3(-out.z, 0, out.x) * sinf(az));   // (the nose's way, turned to its right)
    for (float d = swept + 2.f; d < reach && clear; d += 3.f)
      for (size_t j = 0; j < items.size() && clear; j++)
        if (j != h && kEntInfo[items[j].kind].h * items[j].e.sy > 1.f + d * 0.04f) clear = clearOf(items[j], c + toCam * d, 1.5f);
    if (!clear) continue;
    // level ground under the circle, dry land
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 9; k++) {
      const float t = k * 2.f * PI / 8.f, r = k == 8 ? 0.f : swept;
      const float g = g_world.groundHeight(c.x + cosf(t) * r, c.z + sinf(t) * r);
      lo = std::min(lo, g); hi = std::max(hi, g);
    }
    if (hi - lo > 0.6f || lo < 0.3f) continue;
    const float score = (hg.kind == EK_HANGAR ? 1000.f : hg.kind == EK_ARCH_HANGAR ? 500.f : 0.f) + I.hx * hg.e.sx * I.h * hg.e.sy;
    if (score <= bestScore) continue;
    bestScore = score; best.ok = true;
    best.ground = vec3(c.x, g_world.groundHeight(c.x, c.z), c.z);
    best.heading = atan2f(out.x, -out.z) / DEG;
  }
  return best;
}
// The preview stage (Game::previewStage): the stand at the hub's field that clears the largest craft in the roster, so
// it is the same place for every craft; a field with none, the nearest with one. Kept until the world or the field changes
Game::PreviewStage Game::previewStage() const {
  CameraState::PreviewCache& C = cams.previewCache;
  int& lastAi = C.ai; std::string& lastRegion = C.region; size_t& lastN = C.n; vec3& lastPos = C.pos; PreviewStage& stage = C.stage;
  if (g_world.airports.empty()) return PreviewStage{};
  const int home = hubField();
  const int ai = home >= 0 ? home : std::max(0, g_world.findAirport("CAP"));
  const vec3 at = g_world.airports[ai].pos();   // (the same world: its region, its fields)
  if (ai == lastAi && lastRegion == g_world.regionId && lastN == g_world.airports.size() && length(lastPos - at) < .01f) return stage;
  lastAi = ai; lastRegion = g_world.regionId; lastN = g_world.airports.size(); lastPos = at;
  AircraftSpec largest = kAircraft[0];
  for (int i = 1; i < kAircraftCount; i++) { largest.span = std::max(largest.span, kAircraft[i].span); largest.fusLen = std::max(largest.fusLen, kAircraft[i].fusLen); }
  auto place = [&](int field) {
    const HangarStand st = hangarStandAt(field, largest);
    stage = PreviewStage{}; stage.ok = st.ok; stage.airport = field; stage.ground = st.ground; stage.heading = st.heading;
    return st.ok;
  };
  if (place(ai)) return stage;
  std::vector<std::pair<float, int>> near;
  for (int i = 0; i < (int)g_world.airports.size(); i++) if (i != ai) near.push_back({length(g_world.airports[i].pos() - at), i});
  std::sort(near.begin(), near.end());
  for (size_t k = 0; k < near.size() && k < 8; k++) if (place(near[k].second)) return stage;
  stage = PreviewStage{}; stage.airport = ai;
  return stage;
}

void Game::menuBackgroundCamera(FrameParams& fp) {
  if (prewarmStage > 0) {   // the launch streaming the stage: the stand, then the circuit's quarters (prewarm)
    if (prewarmStage == 1) hangarPreviewCamera(fp);
    else { MenuPreviewView v; v.yaw = (prewarmStage - 2) * 0.5f * PI; stageFlyingCamera(fp, v, kResearchJet, g_ren.W * 0.5f, g_ren.H * 0.5f, g_ren.H * 0.3f, false); }
    return;
  }
  if (screen == SCR_MENU && !dev::env("MENUORBIT")) { menuTour(fp); return; }
  if (screen == SCR_RESEARCH) { researchPreviewCamera(fp); return; }
  if (screen == SCR_HUB && hubTab == TAB_CONTRACTS && hubList == 2) { militaryPreviewCamera(fp); return; }
  if (screen == SCR_FREE_FLIGHT || (screen == SCR_HUB && hubTab == TAB_HANGAR)) {
    hangarPreviewCamera(fp);
    // Free Flight: the departure picked streamed while it is picked (the stand is at the hub's field: researchPreviewCamera)
    if (screen == SCR_FREE_FLIGHT && freeAirport >= 0 && freeAirport < (int)g_world.airports.size()) fp.prefetchPos = g_world.airports[freeAirport].pos();
    return;
  }
  // An Ambler circles over the islands while the camera chases it in a slow arc
  Plane& demo = cams.amblerDemo; bool& initd = cams.amblerInit;
  if (!initd) { demo.reset(&kAircraft[screen == SCR_MENU ? 1 : 0], vec3(0, 600, 0), 0, 50, 100, true, 60); initd = true; }
  const int ap = hubField();
  vec3 centre = screen == SCR_MENU || ap < 0 ? vec3(-6000, 0, 11000) : g_world.airports[ap].pos();
  const float t = realTime * kHubOrbitRate;
  float alt = (screen == SCR_MENU ? 380.f : 320.f) + std::max(centre.y, 0.f);
  vec3 p = hubOrbitAt(centre); p.y = std::max(alt, g_world.height(p.x, p.z) + 250.f);
  vec3 vdir = normalize(vec3(-sinf(t), 0, cosf(t)));
  float hdg = atan2f(vdir.x, -vdir.z) / DEG;
  demo.pos = p;
  demo.q = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG) * quat::axisAngle(vec3(0, 0, 1), -18.f * DEG) * quat::axisAngle(vec3(1, 0, 0), 2.f * DEG);
  bool res = screen == SCR_RESEARCH;
  demo.spec = &kAircraft[res ? resCraft : 1];
  demo.rpm = 2400; demo.gear = res ? 0.f : 1.f; demo.flaps = 0; demo.nozzle = 0; demo.ctl = Controls(); demo.ctl.throttle = res ? 0.6f : 0.f;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, false);
  float ca = realTime * 0.05f, cr = res ? 24.f : 16.f;
  vec3 off = vec3(cosf(ca) * cr, (res ? 5.f : 3.5f) + 2.f * sinf(ca * 0.7f), sinf(ca) * cr) + vdir * -6.f;
  fp.camPos = p + off;
  vec3 fwd = normalize(p - fp.camPos + vdir * 4.f);
  fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, fwd);
  fp.fovY = 50.f * DEG;
  fp.vignette = 0.9f;
  if (screen == SCR_HUB) {   // the hub's flights start at either end of this runway: their scenery streams in while the player chooses, so the loading screen has nothing left to build
    const Airport& A = g_world.airports[ap];
    fp.prefetchOn = true; fp.prefetchPos = A.threshold(((int)(realTime * 2.f)) & 1);
    // and first the preview stage, which the hangar's tab and the service board cut to: kept resident
    if (const PreviewStage st = previewStage(); st.ok) { fp.prefetchNearPos = st.ground; fp.prefetchNearR = kStageKeepR; }
  }
}

// The hangar's preview (the career's hangar, Free Flight's aircraft): the selected aircraft parked in the live world in
// front of a hangar at the airport you're at, its own size against the building's and everything around it (docs/HANGAR.md).
// A locked research teaser, and a world with no stand, keep the dedicated showroom, isolated from the flight simulation.
// Selection changes only the displayed airframe; purchasing, fuel, wear and the active flight remain untouched.
void Game::hangarPreviewCamera(FrameParams& fp) {
  const int craft = prewarmStage > 0 ? 0 : screen == SCR_FREE_FLIGHT ? validCareerSelection(freeCraft) : std::clamp(selHangar, 0, kAircraftCount - 1);
  hangarView.select(craft, screen == SCR_FREE_FLIGHT);
  const AircraftSpec& sp = kAircraft[craft];
  Plane& demo = cams.hangarDemo;
  if (demo.spec != &sp) demo.reset(&sp, vec3(0), 0, sp.maxFuel, 85, false, 0);
  if (!hangarResearchLocked(craft)) {   // the preview stage: the same stand for the hangar's aircraft and Free Flight's
    const PreviewStage st = previewStage();
    if (st.ok) { hangarApronCamera(fp, demo, st.ground, st.heading); return; }
  }
  // Spawn clearance is for physics to settle; a static showroom must sit on the
  // rendered tyres immediately. The main-wheel centres are (wr-gh, mainZ).
  // gearTailShape uses (-gh + tailY + .1, tailZ), radius .1 (gearStations: the
  // physics' tail contact, which the drawn wheel stands on). Solve the main/tail
  // bottom heights together; pitching about their X axles leaves each circular
  // tyre's vertical support radius unchanged.
  const float gh = demo.gearHeight(), wr = kModels[craft].wheelR;
  float pitch = 0.f, height = gh;
  if (sp.taildragger) {
    const GearStations stations = gearStations(sp);
    const float dy = stations.tailY + .1f - wr, dz = stations.tailZ - stations.mainZ;
    const float r = sqrtf(dy*dy + dz*dz);
    pitch = atan2f(dy, dz) + asinf(clampf((wr-.1f)/r, -1.f, 1.f));
    height = wr - (wr-gh)*cosf(pitch) + stations.mainZ*sinf(pitch);
  }
  // Turn the parked airframe about world-up, rather than orbiting through the
  // showroom's opaque walls. Yaw after parking pitch preserves all tyre heights.
  demo.q = quat::axisAngle(vec3(0, 1, 0), -(hangarView.yaw-MenuPreviewView::defaultYaw)) * quat::axisAngle(vec3(1, 0, 0), pitch);
  demo.pos = vec3(0, height, 0);
  demo.vel = vec3(0); demo.onGround = true; demo.engineRunning = false;
  demo.gear = 1.f; demo.flaps = 0; demo.nozzle = 0; demo.rpm = 0; demo.engineSpool = 0; demo.n1 = 0;
  demo.ctl = Controls(); demo.ctl.throttle = 0; demo.ctl.gearDown = true;
  fillPlaneVisual(fp.plane, demo, 0.f, false);
  fp.hangarPreview = true; fp.hangarClassified = hangarResearchLocked(craft); fp.hangarOrigin = vec3(0);
  fp.hangarSize = std::max(12.f, std::max(sp.span, sp.fusLen));
  fp.prefetchOn = false; fp.cloudCover = 0; fp.fogB = 0; fp.wet = 0; fp.snow = 0; fp.storm = 0;
  fp.lightning = 0; fp.wind = vec3(0); fp.windSock = vec3(0); fp.planeTerrSh = 1;
  fp.sunDir = normalize(vec3(-0.25f, 0.65f, -0.72f)); fp.sunCol = vec3(0.30f, 0.34f, 0.39f); fp.night = 0;
  fp.trafficN = 0; fp.ufoOn = false; fp.feedRig = 0; fp.dispMode = 0; fp.landLight = 0;
  fp.plN = 0;
  for (int side : {-1, 1}) for (float z : {-0.35f, 0.45f}) {
    FrameParams::PointLight& light = fp.pl[fp.plN++];
    light.pos = vec3(side * 0.55f, 0.645f, z) * fp.hangarSize;
    light.radius = fp.hangarSize * 0.045f;   // emitter size, not a light range
    light.col = vec3(0.72f, 0.85f, 1.f) * (fp.hangarSize * fp.hangarSize * 0.30f);
    light.cosCut = -2; light.dir = vec3(0, -1, 0); light.shadow = side == 1 && z < 0 ? fp.hangarSize * 1.5f : 0;
  }
  if (fp.hangarClassified) {
    // The dedicated silhouette pass ignores materials/emission as well; keep the
    // physical scene entirely shut down so no effects can reveal the airframe.
    fp.plN = 0; fp.sunCol = vec3(0); fp.sealedCockpit = true;
    fp.plane.lensN = 0; fp.plane.exhaust = ExhaustVisual{};
    std::fill(&fp.plane.flame[0], &fp.plane.flame[0]+4, 0.f);
    std::fill(&fp.plane.vapor[0], &fp.plane.vapor[0]+4, 0.f);
    std::memset(fp.plane.wr, 0, sizeof(fp.plane.wr));
  }
  const float s = S(), W = float(std::max(g_ren.W, 1)), H = float(std::max(g_ren.H, 1));
  const HangarLayout L = hangarLayout(24*s, 126*s, W-48*s, H-146*s, s);
  const float cx = L.previewX + L.previewWidth * 0.5f, cy = L.previewY + L.previewHeight * 0.5f;
  const float radius = std::max(sp.span, sp.fusLen) * 0.65f;
  const float pixels = std::max(1.f, std::min(L.previewWidth, L.previewHeight) * 0.40f);
  const vec3 focus = demo.pos + vec3(0, sp.fusRad * 0.45f, 0);
  const float elevation = clampf(hangarView.pitch,.04f,.35f);
  const vec3 viewingRay(sinf(MenuPreviewView::defaultYaw)*cosf(elevation),sinf(elevation),-cosf(MenuPreviewView::defaultYaw)*cosf(elevation));
  // Keep the eye below the lowest roof beams and inside the side walls even on
  // tall/narrow windows. Zoom changes only the lens, never this safe position.
  const float roomDistance = std::min((fp.hangarSize * .54f - focus.y) / viewingRay.y,
                                      fp.hangarSize * 1.10f / viewingRay.x);
  const float fitDistance = H * .5f * radius / (pixels * tanf(17.f * DEG));
  const float distance = std::max(radius * 1.25f, std::min(fitDistance, roomDistance));
  // A close camera needs the sphere's angular radius, not its flat radius/distance.
  // This keeps elevated views inside the stage even near the showroom roof.
  const float tangentDistance=std::sqrt(std::max(distance*distance-radius*radius,radius*radius*.25f));
  const float th = H * .5f * radius / (pixels * tangentDistance * clampf(hangarView.zoom, MenuPreviewView::minZoom, MenuPreviewView::maxZoom));
  fp.fovY = 2.f * atanf(th);
  const vec3 C = focus + viewingRay * distance;
  const float nx = (cx-W*.5f)/(W*.5f), ny = (H*.5f-cy)/(H*.5f);
  const auto axes=menuPreviewAxes(focus-C,nx*th*W/H,ny*th);
  fp.camPos=C;fp.camBack=-axes.forward;fp.camRight=axes.right;fp.camUp=axes.up;
  fp.exposure = 1.f; fp.vignette = 0.32f;
}

// The stand in the live world (hangarPreviewCamera): the aircraft on its tyres on the apron, turned on the turntable,
// and the camera out over the apron at the showroom's azimuth, elevation and lens - the hangar behind the aircraft, the
// field and the sky round it as the hub has them (the time of day, the weather), the dimensions painted on the apron
void Game::hangarApronCamera(FrameParams& fp, Plane& demo, vec3 ground, float heading) {
  const AircraftSpec& sp = *demo.spec;
  const int craft = std::max(0, typeOf(&sp));
  const float gh = demo.gearHeight(), wr = kModels[craft].wheelR;   // (on its tyres: the showroom's solve)
  float pitch = 0.f, height = gh;
  if (sp.taildragger) {
    const GearStations stations = gearStations(sp);
    const float dy = stations.tailY + .1f - wr, dz = stations.tailZ - stations.mainZ;
    const float r = sqrtf(dy*dy + dz*dz);
    pitch = atan2f(dy, dz) + asinf(clampf((wr-.1f)/r, -1.f, 1.f));
    height = wr - (wr-gh)*cosf(pitch) + stations.mainZ*sinf(pitch);
  }
  const quat stand = quat::axisAngle(vec3(0, 1, 0), -heading * DEG);   // (nose out of the hangar's door)
  demo.q = stand * quat::axisAngle(vec3(0, 1, 0), -(hangarView.yaw-MenuPreviewView::defaultYaw)) * quat::axisAngle(vec3(1, 0, 0), pitch);
  demo.pos = ground + vec3(0, height, 0);
  demo.vel = vec3(0); demo.onGround = true; demo.engineRunning = false;
  demo.gear = 1.f; demo.flaps = 0; demo.nozzle = 0; demo.rpm = 0; demo.engineSpool = 0; demo.n1 = 0;
  demo.ctl = Controls(); demo.ctl.throttle = 0; demo.ctl.gearDown = true;
  fillPlaneVisual(fp.plane, demo, 0.f, false);
  const float size = std::max(sp.span, sp.fusLen);
  fp.hangarApron = true; fp.hangarSize = std::max(12.f, size);
  fp.hangarOrigin = ground + vec3(0, 0.03f, 0);   // (the paint a few centimetres over the apron)
  fp.trafficN = 0; fp.ufoOn = false; fp.feedRig = 0; fp.dispMode = 0; fp.landLight = 0;   // (nothing taxis through the stand)
  fp.prefetchOn = true; fp.prefetchPos = ground; fp.prefetchNearPos = ground; fp.prefetchNearR = kStageKeepR;   // (the stage kept built: the circuit's chunks too)
  if (fp.night > 0.05f) {   // after dark, the hangar's two floodlights over its door
    const vec3 fwd = stand.rotate(vec3(0, 0, -1)), right = stand.rotate(vec3(1, 0, 0));
    const float back = 0.5f * size * 1.05f + 3.f, reach = back + 2.f;
    for (int side : {-1, 1}) if (fp.plN < 12) {
      FrameParams::PointLight& light = fp.pl[fp.plN++];
      light.pos = ground - fwd * back + right * (side * std::max(4.f, 0.3f * size)) + vec3(0, std::max(6.f, 0.28f * size), 0);
      light.radius = 0.35f; light.col = vec3(1.f, 0.9f, 0.74f) * (reach * reach * 0.9f * fp.night);
      light.cosCut = -2; light.dir = vec3(0, -1, 0); light.shadow = side == 1 ? reach * 2.f : 0;
    }
  }
  const float s = S(), W = float(std::max(g_ren.W, 1)), H = float(std::max(g_ren.H, 1));
  const HangarLayout L = hangarLayout(24*s, 126*s, W-48*s, H-146*s, s);
  const float cx = L.previewX + L.previewWidth * 0.5f, cy = L.previewY + L.previewHeight * 0.5f;
  const float radius = size * 0.65f;
  const float pixels = std::max(1.f, std::min(L.previewWidth, L.previewHeight) * 0.40f);
  const vec3 focus = demo.pos + vec3(0, sp.fusRad * 0.45f, 0);
  const float elevation = clampf(hangarView.pitch, .04f, .35f);
  const vec3 viewingRay = stand.rotate(vec3(sinf(MenuPreviewView::defaultYaw)*cosf(elevation), sinf(elevation), -cosf(MenuPreviewView::defaultYaw)*cosf(elevation)));
  const float fitDistance = H * .5f * radius / (pixels * tanf(17.f * DEG));
  const float distance = std::max(radius * 1.25f, fitDistance);
  const float tangentDistance = std::sqrt(std::max(distance*distance-radius*radius, radius*radius*.25f));
  const float th = H * .5f * radius / (pixels * tangentDistance * clampf(hangarView.zoom, MenuPreviewView::minZoom, MenuPreviewView::maxZoom));
  fp.fovY = 2.f * atanf(th);
  vec3 C = focus + viewingRay * distance;
  C.y = std::max(C.y, g_world.groundHeight(C.x, C.z) + 1.2f);
  const float nx = (cx-W*.5f)/(W*.5f), ny = (H*.5f-cy)/(H*.5f);
  const auto axes = menuPreviewAxes(focus-C, nx*th*W/H, ny*th);
  fp.camPos = C; fp.camBack = -axes.forward; fp.camRight = axes.right; fp.camUp = axes.up;
  fp.vignette = 0.32f;
}

// The military terminal shows the exact mission-assigned airframe through the research scene's native renderer.
// Its demo plane never enters the career fleet or the active flight session.
void Game::militaryPreviewCamera(FrameParams& fp) {
  const Contract c = career.militaryContract(militarySelection);
  const bool classified = !career.militaryMissionUnlocked(militarySelection);
  const int craft = c.forceAircraft >= 0 && c.forceAircraft < kAircraftCount ? c.forceAircraft : 0;
  militaryView.select(craft,militarySelection);
  const ResLayout L = militaryLayout();
  const float W = std::max(g_ren.W,1), H = std::max(g_ren.H,1);
  const float s=std::min(S(),std::min(H/720.f,W/1120.f));
  const float pixels=std::max(1.f,.40f*std::min(L.px1-L.px0,L.bot-L.top-135*s));
  stageFlyingCamera(fp,militaryView,craft,L.cx,L.cy,pixels,false);   // (over the preview stage, as the research terminal's)
  if (c.from >= 0 && c.from < (int)g_world.airports.size()) fp.prefetchPos = g_world.airports[c.from].pos();   // (the mission's field streamed while it is on the board: researchPreviewCamera)
  fp.vignette=.45f;
  // Reuse the hangar's geometry-only outline. Registrations, paint, cockpit
  // screens and effects cannot leak into a classified preview.
  // FrameParams is rebuilt each frame, so an unlocked selection immediately returns
  // to its normal outdoor aircraft render without retaining classified materials.
  if (classified) {
    const float D=length(fp.camPos-resPrevPos);
    fp.hangarPreview = fp.hangarClassified = true;
    // Keep the whole interactive orbit inside the room. A normal-sized showroom
    // puts its opaque walls between the long terminal camera and the aircraft.
    fp.hangarSize = std::max(12.f,D*3.f);
    fp.hangarOrigin = resPrevPos-vec3(0,fp.hangarSize*.28f,0);
    fp.prefetchOn=false; fp.cloudCover=fp.fogB=fp.wet=fp.snow=fp.storm=0;
    fp.lightning=0; fp.wind=fp.windSock=vec3(0); fp.planeTerrSh=1;
    fp.plN=0; fp.sunCol=vec3(0); fp.sealedCockpit=true;
    fp.landLight=0; fp.plane.lensN=0; fp.plane.exhaust=ExhaustVisual{};
    std::fill(std::begin(fp.plane.flame),std::end(fp.plane.flame),0.f);
    std::fill(std::begin(fp.plane.vapor),std::end(fp.plane.vapor),0.f);
    std::memset(fp.plane.wr,0,sizeof(fp.plane.wr));
    // The terminal's olive glass is darker than the civilian hangar overlay.
    // Lift only this material-free outline so it stays discernible there.
    fp.exposure=3.f; fp.vignette=.25f;
  }
}

// A loading preview is a camera over the actual frozen sortie, never a substitute aircraft or a separate simulation.
// The same frame still streams the mission world and contributes to the existing readiness checks.
void Game::militaryLoadingCamera(FrameParams& fp) {
  const AircraftSpec& sp = *plane.spec;
  fillPlaneVisual(fp.plane,plane,propAngle,false);
  fp.dispMode=fp.feedRig=0; fp.fovY=32.f*DEG;
  const float W=std::max(g_ren.W,1), H=std::max(g_ren.H,1), th=tanf(fp.fovY*.5f);
  const LoadingLayout L=loadingLayout(W,H);
  const float D=std::max(sp.span,sp.fusLen)*2.7f;
  const vec3 P=plane.pos, C=P+plane.right()*(D*.7f)+plane.forward()*(D*.65f)+vec3(0,D*.3f,0);
  const vec3 f=normalize(P-C), r=normalize(cross(f,vec3(0,1,0))), u=cross(r,f);
  const float centerY=(L.artTop+L.artBottom)*.5f, ny=(H*.5f-centerY)/(H*.5f);
  const vec3 aim=P-r*(.1f*D*th*W/H)-u*(ny*D*th);
  const vec3 look=normalize(aim-C);
  fp.camPos=C; fp.camBack=-look; fp.camRight=normalize(cross(look,vec3(0,1,0))); fp.camUp=cross(fp.camRight,look);
  fp.prefetchOn=true; fp.prefetchPos=plane.pos; fp.vignette=.35f;
}

// The flying preview (the research terminal's range, the service board): the craft flying a circuit over the preview
// stage - round the stand at a speed that reads as motion over the apron, banked into its turn, over the highest
// ground under the circuit - and the camera riding with it (drag to turn round it, wheel to zoom), level with the
// horizon, turned with its heading but not its bank, framed so the craft sits in the middle of the preview ring.
// (It flew a 10 km circuit through the chosen launch site's field, in the sortie's own light: every site and every
// time of day streamed and lit its own scenery, and the terminal cut to it from the menu with a stall)
void Game::stageFlyingCamera(FrameParams& fp, MenuPreviewView& view, int craft, float cx, float cy, float pixels, bool cockpit) {
  const PreviewStage st = previewStage();
  const AircraftSpec& sp = kAircraft[std::clamp(craft, 0, kAircraftCount - 1)];
  Plane& demo = cams.stageDemo;
  if (demo.spec != &sp) demo.reset(&sp, vec3(0, 600, 0), 0, 50, 85, true, 150);
  const vec3 centre = st.ok ? st.ground : g_world.airports.empty() ? vec3(0) : g_world.airports[std::clamp(st.airport, 0, (int)g_world.airports.size() - 1)].pos();
  const float R = kStageCircuitR, v = clampf(0.3f * sp.cruise, 65.f, 95.f), bankDeg = atanf(v * v / (9.81f * R)) / DEG;
  auto at = [&](float t) { return vec3(centre.x + cosf(t) * R, 0.f, centre.z + sinf(t) * R); };
  constexpr int kN = CameraState::kStageAltSamples;
  vec3& altCentre = cams.stageAltCentre; auto& alt = cams.stageAlt;
  if (length(altCentre - centre) > 0.01f) {   // (the height round the circuit: the highest ground across a 500 m band, smoothed into a flyable profile)
    altCentre = centre;
    std::array<float, kN> g{}, m{};
    for (int k = 0; k < kN; k++) {
      const float t = k * 2.f * PI / kN; g[k] = 0.f;
      for (int j = -2; j <= 2; j++) { const float r = R + j * 125.f; g[k] = std::max(g[k], g_world.height(centre.x + cosf(t) * r, centre.z + sinf(t) * r)); }
    }
    for (int k = 0; k < kN; k++) { m[k] = g[k]; for (int j = -6; j <= 6; j++) m[k] = std::max(m[k], g[((k + j) % kN + kN) % kN]); }
    for (int pass = 0; pass < 4; pass++) {
      std::array<float, kN> b = m;
      for (int k = 0; k < kN; k++) b[k] = std::max(m[k], 0.25f * m[(k + kN - 1) % kN] + 0.5f * m[k] + 0.25f * m[(k + 1) % kN]);
      m = b;
    }
    for (int k = 0; k < kN; k++) alt[k] = std::max(m[k], centre.y) + kStageCircuitAgl;
  }
  auto altAt = [&](float t) {
    float f = t / (2.f * PI) * kN; f -= floorf(f / kN) * kN;
    const int k0 = (int)f % kN, k1 = (k0 + 1) % kN; const float u = f - floorf(f);
    return alt[k0] + (alt[k1] - alt[k0]) * u;
  };
  const float th0 = v * realTime / R;   // (round the circuit at its speed)
  vec3 P = at(th0); P.y = altAt(th0) + 4.f * sinf(realTime * 0.21f);
  const float climb = (altAt(th0 + 200.f / R) - altAt(th0 - 200.f / R)) / 400.f;   // (the profile's slope: its nose follows)
  const vec3 along = normalize(vec3(-sinf(th0), 0.f, cosf(th0)));
  const float hdg = atan2f(along.x, -along.z) / DEG;
  const quat yawQ = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG);
  // banked into the turn (the wing's lift towards the circle's centre), a little life in the roll and the pitch
  const float roll = bankDeg + 1.5f * sinf(realTime * 0.53f), nose = 3.f + atanf(climb) / DEG + 0.6f * sinf(realTime * 0.37f);
  quat q = yawQ * quat::axisAngle(vec3(0, 0, 1), -roll * DEG) * quat::axisAngle(vec3(1, 0, 0), nose * DEG);
  if (dot(q.rotate(vec3(0, 1, 0)), centre - vec3(P.x, 0, P.z)) < 0.f) q = yawQ * quat::axisAngle(vec3(0, 0, 1), roll * DEG) * quat::axisAngle(vec3(1, 0, 0), nose * DEG);
  demo.q = q; demo.pos = P; demo.vel = (along + vec3(0, climb, 0)) * v;
  demo.gear = 0; demo.ctl = Controls(); demo.ctl.throttle = 0.55f; demo.ctl.gearDown = false;
  demo.engineRunning = true; demo.engineSpool = 0.55f; demo.n1 = 75.f; demo.onGround = false;
  demo.rpm = 2400; demo.flaps = 0; demo.nozzle = 0;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, cockpit);
  resPrevPos = P; resPrevQ = demo.q;
  fp.trafficN = 0; fp.ufoOn = false; fp.feedRig = 0; fp.dispMode = 0; fp.landLight = 0;   // (nothing else over the stage)
  // the stage kept built round the stand: the circuit's chunks, and the hub's field's
  fp.prefetchOn = true; fp.prefetchPos = centre; fp.prefetchNearPos = centre; fp.prefetchNearR = kStageKeepR;
  // the orbit: the craft in the middle of the ring
  const float W = (float)std::max(g_ren.W, 1), H = (float)std::max(g_ren.H, 1);
  const float ext = std::max(sp.span, sp.fusLen) * 0.65f;   // (the craft fills the ring)
  fp.fovY = 32.f * DEG;
  const float th = tanf(fp.fovY * 0.5f);
  const float D = (H * 0.5f / std::max(1.f, pixels)) * ext / th / clampf(view.zoom, MenuPreviewView::minZoom, MenuPreviewView::maxZoom);
  const float yaw = view.yaw, pit = clampf(view.pitch, -.5f, 1.f);
  vec3 C = P + yawQ.rotate(vec3(sinf(yaw) * cosf(pit), sinf(pit), -cosf(yaw) * cosf(pit))) * D;
  C.y = std::max(C.y, g_world.height(C.x, C.z) + 5.f);
  const auto axes = menuPreviewAxes(P - C, (cx - W * .5f) / (W * .5f) * th * W / H, (H * .5f - cy) / (H * .5f) * th);
  fp.camPos = C; fp.camBack = -axes.forward; fp.camRight = axes.right; fp.camUp = axes.up;
}

void Game::researchPreviewCamera(FrameParams& fp) {
  researchView.select(resCraft);
  const int craft = resWarmCraft >= 0 ? resWarmCraft : resCraft;
  const AircraftSpec& sp = kAircraft[craft];
  const ResLayout L = researchLayout();
  const float pixels = std::max(1.f, .40f * std::min(L.px1 - L.px0, L.bot - L.top - 56 * S()));
  stageFlyingCamera(fp, researchView, craft, L.cx, L.cy, pixels, resWarm && resWarmCk);   // (warming up, each craft's cockpit for a frame too: its cabin shell drawn now, not when the sortie opens)
  // the sortie's own site streamed while the terminal is open (its loading screen waits for every chunk about where it
  // opens - some eight thousand to the quality's far range - and the stage is at the hub's field, not the site)
  if (resAirport >= 0 && resAirport < (int)g_world.airports.size()) { const Airport& a = g_world.airports[resAirport]; fp.prefetchPos = resAirborne ? a.pos() + a.dir() * 1500.f : a.pos(); }
  if (sp.special == 2) {
    // Preview the chosen hardware, not the live flight's old loadout. This is
    // visual-only: no ammunition, encounter or career state changes in the terminal.
    fp.plane.wr[6][3] = float(practiceForward);
    fp.plane.wrBombSet = int(practiceBomb);
    fp.plane.wr[6][0] = 1.f;
    if (!resWarm && resCard < 0) {
      fp.plane.wr[4][1] = 1.f; // open payload bay for equipment inspection
      fp.plane.wr[4][2] = 1.f; // deployed forward hardware, weapons remain safe
    }
  }
  fp.vignette = 0.75f;
}
