// Somnium Volandi - the flight session: start and end, the controls, the autopilot's hooks, failures, job meters
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

// (a landplane chosen at a seaplane base: nowhere for it to stand, so it starts over the lane instead)
// The field a float plane is coming down at: the nearest within 4 km (an amphibian may land at any, on whichever
// surface it has), or -1
int Game::amphibianField() const {
  float d; const int ai = g_world.nearestAirport(plane.pos.x, plane.pos.z, &d);
  return ai >= 0 && d < 4000.f ? ai : -1;
}

// The gear warning: low and slow near the field with the gear wrong for it - a retractable gear not down at the
// destination; an amphibian's wheels down for a water lane, or up for a runway, while it is coming down (not climbing
// away from the field it left: docs/WINGS_OVER_WATER.md)
bool Game::gearWarning() const {
  const AircraftSpec& s = *plane.spec;
  if (!s.retract || plane.onGround || plane.agl() >= 200 || plane.ias >= s.vref * 1.5f) return false;
  if (s.floats) {
    const int ai = amphibianField();
    return ai >= 0 && plane.vel.y < -0.5f && (g_world.airports[ai].water() ? plane.gear > 0.01f : plane.gear < 0.99f);
  }
  return plane.gear < 0.99f && length(plane.pos - g_world.airports[contract.to].pos()) < 4000.f;
}

bool Game::atDock() const {
  if (!plane.onWater) return false;
  for (const Airport& a : g_world.airports)
    if (a.water() && a.dockLen > 0.f) { const vec3 h = a.dockHead(); if (hypotf(plane.pos.x - h.x, plane.pos.z - h.z) < 15.f) return true; }
  return false;
}

// ------------------------------------------------------------------ failures (C7)
void Game::rollFailures(const Contract& c, int spec, Career::Source src) {
  failPlan = FailPlan();
  if (!failuresArmed || !set.failures || (c.startAirborne && c.type == CT_LESSON)) return;   // (Settings: random failures off)
  float chance = career.failureChance(src, spec);
  if (chance <= 0.f) return;
  // deterministic per attempt: the same flight retried after a crash gets the same roll, a new attempt a new one
  uint32_t h = 2166136261u; for (char ch : c.id) h = (h ^ (uint8_t)ch) * 16777619u;
  Rng r(h ^ (career.attempt * 2654435761u) ^ (uint32_t)spec * 97u);
  if (r.uni() >= chance) return;
  const AircraftSpec& s = airframeFor(spec, src);
  // which: engines most often, the gear only on retractables, icing only in weather that can make it
  float w[FAIL_COUNT] = {0, 3.f, 2.f, 1.5f, 1.2f, s.retract ? 1.5f : 0.f, 1.f, (c.wx.precip == 2 || (c.wx.cloudCover > 0.6f && c.wx.cloudBase > 1800.f)) ? 1.5f : 0.f};
  float sum = 0; for (float x : w) sum += x;
  float pick = r.uni() * sum; int kind = FAIL_ENGINE_PARTIAL;
  for (int k = 1; k < FAIL_COUNT; k++) { if (pick < w[k]) { kind = k; break; } pick -= w[k]; }
  failPlan.kind = kind; failPlan.engine = s.engines > 1 ? (int)(r.uni() * s.engines) % std::min(s.engines, 4) : 0;
  float est = std::max(launchPlan.minutesEst * 60.f, 240.f);
  failPlan.at = 90.f + r.uni() * clampf(est * 0.6f, 60.f, 1500.f);   // somewhere in the first part of the flight, never on the take-off roll
}
// The job types' live records (C6): the medevac patient and the VIP's comfort drift down with g (turbulence's bumps
// among it), bank and a firm touchdown; the survey counts its time in the altitude band between the first and the last
// checkpoint; the low-vis run checks the alignment the first time the aircraft comes below minimums near the
// destination (a go-around afterwards clears it); night freight notes the landing light at touchdown.
void Game::updateJobMeters(float dt, float gs) {
  (void)gs;
  const Contract& c = contract;
  if (researchFlight || isolatedFlight || militaryFlight || crashed) return;
  bool air = !plane.onGround && takeoffAnnounced;
  if (c.type == CT_MEDEVAC || c.type == CT_VIP) {
    float& m = c.type == CT_MEDEVAC ? result.patient : result.comfort;
    float gLim = c.type == CT_MEDEVAC ? 1.5f : 1.3f, bankLim = 30.f;
    float rate = 0;
    if (air) {
      rate += std::max(0.f, plane.gLoad - gLim) * 0.08f + std::max(0.f, (c.type == CT_MEDEVAC ? 0.6f : 0.75f) - plane.gLoad) * 0.08f;
      rate += std::max(0.f, fabsf(plane.bankDeg()) - bankLim) * 0.003f;
      // (turbulence costs what its gusts put on the aircraft - the g above - and no more: a flat charge for the air
      // itself drained the meter however gently the aircraft was flown, review CAR-1)
    }
    if (plane.ev.touchdown && fabsf(plane.ev.touchdownVs) * 196.85f > 300.f) m -= (fabsf(plane.ev.touchdownVs) * 196.85f - 300.f) / 1500.f;
    m = clampf(m - rate * dt, 0.f, 1.f);
  }
  if (c.type == CT_SURVEY && !c.wps.empty() && air && wpIndex > 0 && wpIndex < (int)c.wps.size()) {
    surveyT += dt; if (fabsf(plane.pos.y - c.wps[0].alt) <= 46.f) surveyInT += dt;
    result.surveyInBand = surveyT > 1.f ? surveyInT / surveyT : 1.f;
    result.surveySec = surveyT; result.surveyInSec = surveyInT;   // (the job's whole: a continued leg starts from its earlier legs' - applyJobLeg)
  }
  if (c.type == CT_IFR && air) {
    const Airport& A = g_world.airports[c.to];
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (result.goArounds > minimumsGoArounds) { minimumsGoArounds = result.goArounds; minimumsChecked = false; result.belowMinimumsUnaligned = false; }
    float minimums = std::max(60.f, wx.cloudBase - A.elev - 30.f);
    if (!minimumsChecked && dA < 8000.f && plane.pos.y - A.elev < minimums) {
      minimumsChecked = true;
      bool rev = dot(plane.vel, A.dir()) < 0.f;
      vec3 ld = rev ? -A.dir() : A.dir(), rel = plane.pos - A.threshold(rev); rel.y = 0;
      float lat = fabsf(rel.x * ld.z - rel.z * ld.x);
      float hErr = fabsf(wrapAngle((atan2f(ld.x, -ld.z) - plane.heading() * DEG))) / DEG;
      bool aligned = lat < A.width * 1.5f + 25.f && hErr < 20.f;
      if (!aligned) { result.belowMinimumsUnaligned = true; toast("Below minimums and not lined up - GO AROUND", vec3(1, 0.45f, 0.35f), true); }
      else toast("Runway in sight", vec3(0.6f, 1, 0.7f));
    }
  }
  if (c.type == CT_NIGHT && plane.ev.touchdown) result.landingLightOn = landingLight;
}
// the research test card: the current step's condition, held for its time where it asks one, then the next
void Game::updateResearchCard(float dt) {
  if (!researchFlight || resCard < 0 || resCardDone || crashed || resCard >= kNumResCards) return;
  const ResCard& C = kResCards[resCard];
  if (resStep >= C.n) return;
  const ResStep& st = C.steps[resStep];
  bool air = !plane.onGround, inst = false;
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  switch (st.kind) {
    case RS_MACH: inst = air && plane.mach >= st.v; break;
    case RS_ALT: inst = plane.pos.y >= st.v; break;
    case RS_G: inst = air && plane.gLoad >= st.v; break;
    case RS_NEGG: inst = air && plane.gLoad <= st.v; break;
    case RS_ROLL: inst = air && fabsf(plane.w.z) / DEG >= st.v; break;
    case RS_SLOW: inst = air && plane.agl() > 150.f && plane.ias <= st.v; break;
    case RS_STALL: inst = air && plane.agl() > 600.f && plane.stallWarn > 0.9f; break;
    case RS_CLIMB: inst = air && plane.vel.y >= st.v; break;
    case RS_HOVER: inst = air && plane.nozzle > 0.7f && length(plane.vel) < 4.f && plane.agl() > 4.f; break;
    case RS_VLAND: inst = plane.onGround && touchedDown && plane.nozzle > 0.7f && gs < 1.f && touchdownFpm < 400.f; break;
    case RS_CLOAK: inst = somnus.stealth >= 0.95f; break;
    case RS_LASER: inst = somnus.shots >= (int)st.v; break;
    case RS_BOMB: inst = somnus.dropped >= (int)st.v; break;
    case RS_LAND: { float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA); inst = plane.onGround && touchedDown && gs < 2.f && ap == resAirport && dA < g_world.airports[ap].length * 0.5f + 600.f; } break;
    case RS_ALPHA: inst = air && plane.alpha / DEG >= st.v; break;
    case RS_SCRAM: { const float tb = plane.tbcc[0] + plane.tbcc[1] + plane.tbcc[2]; inst = air && plane.mach >= st.v && tb > 1.f && plane.tbcc[2] > 0.5f * tb; } break;
    case RS_ROCKET: inst = plane.rocketN > 1.f; break;
    case RS_CONVERT: inst = air && plane.nozzle < 0.05f && plane.fanClutch < 0.05f && plane.ias >= st.v; break;
  }
  resHold = inst ? resHold + dt : 0.f;
  if (!inst || resHold < st.hold) return;
  resStep++; resHold = 0;
  if (resStep >= C.n) {
    resCardDone = true; resDone.insert(C.id);
    // (signed off only when it is on record: a settings file that can't be written says so - the review of v3.44.0, CAR-10)
    if (saveSettings()) toast(fmt("TEST CARD %s COMPLETE - %s SIGNED OFF", C.id, C.title), vec3(0.5f, 1.f, 0.6f));
    else toast(fmt("TEST CARD %s COMPLETE - SIGNED OFF THIS SESSION ONLY: THE RECORD COULD NOT BE SAVED", C.id), vec3(1.f, 0.6f, 0.4f));
    g_audio.trigger(SFX_SUCCESS);
  } else { toast(fmt("STEP %d of %d: %s", resStep + 1, C.n, C.steps[resStep].label), vec3(0.9f, 0.9f, 0.6f)); g_audio.trigger(SFX_CHIME, 0.8f); }
}
void Game::fireFailure(int kind, int engine) {
  if (!plane.failNow(kind, engine)) return;
  if (timeAccel > 1.f) timeAccel = 1.f;   // (a failure is flown at real time)
  const AircraftSpec& s = *plane.spec;
  std::string m = failureName(kind);
  if ((kind == FAIL_ENGINE_PARTIAL || kind == FAIL_ENGINE_TOTAL) && s.engines > 1) m += fmt(" - engine %d", engine + 1);
  if (kind == FAIL_GEAR_STUCK) m += plane.fail.gearStuck == 1 ? " UP" : " DOWN";
  if (kind == FAIL_PITOT) m += " - airspeed unreliable";
  if (kind == FAIL_ALTERNATOR) m += " - on battery, land soon";
  toast(m, vec3(1, 0.45f, 0.35f), false);   // (shown; the annunciator speaks the failure with its guidance, updateComms)
  g_audio.trigger(SFX_BEEP, 1.f);
  if (plane.apOn && (kind == FAIL_ENGINE_TOTAL || kind == FAIL_ENGINE_PARTIAL || kind == FAIL_PITOT)) {
    plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - " + std::string(failureName(kind)), vec3(1, 0.7f, 0.3f));
  }
  result.failureKinds |= 1 << kind;
}
void Game::updateFailures(float dt) {
  (void)dt;
  // ice: in cloud or precipitation below freezing it builds; clear warm air melts it (about 0 C above 2300 m, or in snow)
  // (in the cloud that is drawn there, weather.cpp, or in its rain)
  bool freezing = wx.precip == 2 || plane.pos.y > 2300.f;
  bool inCloud = plane.wxl.cloud > 0.05f;
  bool wet = inCloud || plane.wxl.rain > 0.05f;
  plane.iceFeed = !failuresArmed && plane.fail.ice <= 0.f ? 0.f : (wet && freezing) ? 1.f : (!wet && !freezing) ? -1.f : 0.f;
  if (plane.fail.ice > 0.05f) result.failureKinds |= 1 << FAIL_ICING;
  if (plane.fail.alternator && plane.fail.avionicsDark()) {
    if (plane.apOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot off - battery flat", vec3(1, 0.7f, 0.3f)); }
    if (showMap) { showMap = false; toast("GPS dark - battery flat", vec3(1, 0.45f, 0.35f)); }
  }
  if (failPlan.kind == 0 || failPlan.fired || crashed || plane.onGround) return;
  if (failPlan.kind == FAIL_GEAR_STUCK) {   // waits for the gear to be commanded the other way
    bool wantMove = (plane.ctl.gearDown && plane.gear < 0.99f) || (!plane.ctl.gearDown && plane.gear > 0.01f);
    if (flightClock < failPlan.at || !wantMove) return;
  } else if (flightClock < failPlan.at || plane.agl() < 120.f) return;
  failPlan.fired = true;
  fireFailure(failPlan.kind, failPlan.engine);
}

Game::~Game() {
  regionalSurface::clearFlightContext();
  if (earthPrefetchCancel) earthPrefetchCancel->requested = true;
  // The loader captures this for its stage flag. Join before member destruction,
  // even when the player closes the window halfway through generation.
  if (regionJob.valid()) { try { regionJob.get(); } catch (...) {} }
  if (earthPrefetchJob.valid()) { try { earthPrefetchJob.get(); } catch (...) {} }
  if (earthRetireJob.valid()) { try { earthRetireJob.get(); } catch (...) {} }
  g_sceneryProfiles.reset();   // (the finals profiled for this game's flights: their worker reads the world)
}

std::vector<Game::Annunciator> Game::hudAnnunciators() const {
  const Failures& F = plane.fail;
  const AircraftSpec& spc = *plane.spec;
  std::vector<Annunciator> ann;
  for (int e = 0; e < spc.engines && e < 4; e++) {
    std::string slot = "engine" + std::to_string(e + 1);
    if (F.engineHealth[e] <= 0.f)
      ann.push_back({spc.engines > 1 ? fmt("ENGINE %d FAILED", e + 1) : plane.glideOnly() ? fmt("ENGINE FAILURE  glide %.0f:1, best %.0f kt", plane.glideRatio(), Plane::perf(&spc).vy * MS_TO_KT * 1.1f) : std::string("ENGINE FAILED"), slot, "failed", true});
    else if (F.engineHealth[e] < 0.999f) ann.push_back({spc.engines > 1 ? fmt("ENGINE %d POWER LOSS", e + 1) : std::string("ENGINE POWER LOSS"), slot, "loss", false});
  }
  if (F.alternator) ann.push_back({F.avionicsDark() ? std::string("BATTERY FLAT  no autopilot, no GPS") : fmt("ALTERNATOR  battery %.0f%%", F.battery * 100.f), "alternator", F.avionicsDark() ? "flat" : "battery", F.avionicsDark()});
  if (F.pitot) ann.push_back({"PITOT BLOCKED  airspeed unreliable", "pitot", "blocked", false});
  if (F.gearStuck) ann.push_back({F.gearStuck == 1 ? "GEAR STUCK UP  belly landing: paved, level, slow" : "GEAR STUCK DOWN  slower, more fuel", "gear", F.gearStuck == 1 ? "up" : "down", F.gearStuck == 1});
  if (F.flapAsym) ann.push_back({fmt("FLAP ASYMMETRY  left stuck at %.0f%%: keep the lever there", F.flapAt / std::max(aeroGeom(spc).flapMax, 0.05f) * 100.f), "flaps", "asym", false});
  if (F.ice > 0.05f) ann.push_back({fmt("ICING %.0f%%  leave the cloud, keep speed", F.ice * 100.f), "ice", F.ice > 0.5f ? "severe" : "icing", F.ice > 0.5f});
  return ann;
}

// ------------------------------------------------------------------ flight session
void Game::startFlight(const Contract& c, int spec, Career::Source src, const Career::LaunchPlan* finalized) {
  // All military entry points, including direct starts and pause retries, require
  // the already committed, unsettled career attempt. UI visibility is not access control.
  if (isMilitaryContract(c.type) || src == Career::SRC_MILITARY) {
    const auto& m = career.military;
    if (src != Career::SRC_MILITARY || !isMilitaryContract(c.type) || commitBlocked() ||
        !career.attemptOpen || !m.activeAttempt || m.activeAttempt != career.attempt ||
        m.activeAttempt <= m.lastSettledAttempt || m.activeKind != c.type-CT_MIL_RECON ||
        m.activeAirport != c.from || career.canFly(c,spec) != Career::SRC_MILITARY) return;
  }
  static bool dispWarned = false;   // say once if the cockpit display shader could not be built on this GPU
  if (!g_ren.dispError.empty() && !dispWarned && !headless) { dispWarned = true; toast("Cockpit display shader failed on this GPU (details in startup.log)", vec3(1.f, 0.45f, 0.35f)); }
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  haul = Haul{};   // (a long haul's own state follows its start: beginHaul)
  contract = c; specIdx = spec; source = src;
  launchPlan = finalized ? *finalized : finalizeLaunchPlan(c, spec, src);   // accepted quote, tank load and bill agree
  militaryFlight=isMilitaryContract(c.type); militarySettled=false; militaryHull=100; militarySiteLabels={};
  hiveCombat.reset(1, combatWrap()); combatLoadout.reset();
  combatCollisionWarm=false; combatCollisionWarmMs=0; combatPrefetchCursor=0;
  researchFlight = freeFlight = false;   // isolated launchers set their own mode after the shared reset
  wx = c.wx; timeOfDay = wx.timeOfDay; apRepickT = 0;
  if ((set.air == 0 || calmRetry) && !botControl) {   // (Settings: calm air - a third of the gusts and the turbulence, the fronts' too; a lesson retried in calm air, this once)
    wx.gust *= 0.35f; wx.turbulence *= 0.35f;
    if (calmRetry) { wx.windSpeed = std::min(wx.windSpeed, 4.f); wx.gust = 0.f; }
    if (contract.wxShift) { contract.wxEnd.gust *= 0.35f; contract.wxEnd.turbulence *= 0.35f; }
  }
  calmRetry = false;
  wxStart = wx;
  wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
  cloudWake.clear(); wakeGap = true; wakeOdo = 0; wispAccum = 0; rainNow = mistNow = 0;
  const AircraftSpec& s = airframeFor(spec, src);   // (your own as fitted at the hangar; Free Flight's on floats, if chosen)
  const Airport& a = g_world.airports[c.from];
  // runway into the wind (lessons with rings keep the published runway so the rings line up; the ringless ones, like
  // the cross-country and the checkride, take off into the wind like any other flight)
  float h0 = a.heading;
  bool reverse = false;
  if (c.type != CT_LESSON || c.wps.empty()) {
    float hw0 = cosf((wx.windFrom - h0) * DEG), hw1 = cosf((wx.windFrom - h0 - 180.f) * DEG);
    reverse = hw1 > hw0;
  }
  float hdg = reverse ? h0 + 180.f : h0;
  vec3 start = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
  if (a.water() && s.floats && !c.startAirborne && a.dockLen > 0.f) {   // a float plane at a seaplane base: moored at its dock, facing out
    const FloatStations fs = floatStations(s);
    start = a.berth(2.6f + fs.x + fs.beam * 0.5f); hdg = a.berthHeading();   // (the pontoon's 2.3 m half width, its fenders, a hand's breadth)
  }
  float payloadKg = (float)c.cargoKg + c.pax * 85.f + 85.f;
  combatBasePayload=payloadKg;
  float fuel = launchPlan.fuelLoadKg;
  launchFuelKg = -1;
  g_audio.resetWorld(); ++audioSceneId; audioCameraValid=false; audioPlayerCone=-1e9f; audioBoomCooldown=0; audioMachArmed=true;
  audioThreatAt=audioObjectiveAt=-100; audioExtractSaid=false; commsEndScreen=-1;
  plane.reset(&s, start, hdg, fuel, payloadKg, c.startAirborne, s.cruise);
  plane.apComfort = c.gentle();   // gentle for passengers or a fragile load, else the airframe's whole envelope (the stick is never limited)
  fuelStart = plane.fuel;
  rollFailures(c, spec, src);
  isolatedFlight = false; jobLeg = false; jobClockBase = 0; attemptFrom = c.from;
  wpIndex = 0; flightClock = 0; crashTimer = 0; endTimer = 0; airBreak = false; crashEndT = 7.5f; gTunnel = 0; gTunnelPin = -1;
  surveyT = surveyInT = 0; minimumsChecked = false; minimumsGoArounds = 0; trialT0 = trialT1 = -1; formT = formLost = 0; formDone = false;
  hudPrevIas = 0; hudTrend = 0;
  traffic.reset();
  ufo = Ufo(); ufo.next = 180.f + sparkRng.uni() * 240.f;   // first encounter after 3-7 minutes in the air
  paused = false; showMap = false; landed = completed = crashed = false;
  result = FlightResult();
  timeAccel = 1; camMode = camMode == 1 ? 1 : 0; camYaw = 0; camPitch = 0.12f; camZoom = 1; camArm = 0; camArmV = 0; camSpd = 0;
  lookYaw = 0; lookPitch = -0.13f;
  cockpitFocus = CockpitFocusZoom(); ckZoom = 1.f; // a new flight never inherits a previous display lock
  camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  flapNotch = 0; phase = 0; lastHintPhase = -1; hint.clear();
  joyThrTaken = false; joyThrLast = -1;   // (the throttle lever takes over when it moves, not at the start)
  arrivalAt = -1; arrivalTold = false; engineShutDown = false; engineHold = 0; engineHeld = false; tdStampT = -1.f;
  flightAch.clear(); flightKm = 0; profileT = 0; pbOld = -1.f; pbNew = false; lastMark = LandingMark(); touchdownApplauded = false;
  trialSplits.clear(); ghostRec.clear(); ghostBest.clear();   // (a trial loads its ghost after: startTrial)
  sightReq = -1; sightAsked = sightDone = false; photoFlashT = -10.f;
  if ((c.type == CT_PAX || c.type == CT_VIP) && !c.story && c.from != c.to) {   // a landmark near the way: the passengers may ask
    const Airport& A = g_world.airports[c.from]; const Airport& B = g_world.airports[c.to];
    const auto& sites = profileSites(); float best = 12000.f;
    for (int i = 0; i < (int)sites.size(); i++) {
      const vec3& p = sites[i].pos;
      const float d = length(vec3(p.x - A.x, 0, p.z - A.z)) + length(vec3(p.x - B.x, 0, p.z - B.z)) - length(vec3(B.x - A.x, 0, B.z - A.z));
      if (d < best) { best = d; sightReq = i; }
    }
  }
  takeoffAnnounced = c.startAirborne; touchedDown = false; touchdownFpm = 0; stillTimer = 0; tdRunway = -2; gearAdvised = false;   // (an airborne start has no takeoff to announce)
  engineAutoStarted = false; startDelay = 1.2f;
  parkingBrake = !c.startAirborne;   // a start on the ground is parked: the brake is released to roll
  if (!plane.onGround) settleAirborneStart();
  particles.clear(); wakeRings.clear(); bursts.clear(); pops.clear(); boomT = -1; for (auto& tt : pieceTrail) tt.clear(); trail.clear(); tipTrail[0].clear(); tipTrail[1].clear(); tipOn = false; trailT = 0; wreck.clear(); debris.clear(); craterR = 0; pits.clear();
  lightning = 0; nextLightning = 6; strikes.clear(); lightningGlow = 0.f;
  landingLight = true;
  approachMinAgl = 1e9f; thrPrevAlong = -1e9f; appLow = false; appHigh = false; coaching.clear();
  apWasOn = false; apDest = -1; somnus = SomnusState(); g_scenery.resetDamage();
  licenseBefore = career.license;
  // the player sees a loading screen while the scenery around the start is generated (tests fly straight away)
  screen = headless ? SCR_FLIGHT : SCR_LOADING;
  if (!loadLogging) loadLogBegin();
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadFrames = 0; loadViewsDrawn = false; loadViewsStep = 0; loadWarmStep = 0; loadWarmAt = -1; loadStableFrames = 0; loadBakeSeen = g_ren.bakeCount; loadObservedFrame = -1; loadMap = loadBackdrop = false; dbgCam = dbgFollow = false; benchPin = false;
  atc.cancel(); atcF = AtcFlight(); hintsVoiced.clear(); commsPending.clear(); lessonVoiceWarned = false;   // (the last flight's calls go before this one's announcements)
  // (a hazard call is said only while the hazard holds: a "PULL UP" queued behind the tower played at 1,958 m, climbing -
  // the review of v3.44.0, AUD-3)
  atc.valid = [this](const AtcVoice::Tx& t) { return t.key < 0 || (t.key >= kWarnKey ? t.key - kWarnKey < 4 && warnWas[t.key - kWarnKey] : t.key == atcKey()); };
  failVoiced.clear();
  toast(fmt("%s - %s", a.code, a.name), vec3(0.7f, 0.9f, 1.0f));
  toast(fmt("Runway %02d, %s", a.rwyNumber(reverse), wx.describe().c_str()), vec3(0.8f, 0.8f, 0.8f));
  atcF.dep = c.from; atcF.arr = c.to; atcF.depRev = reverse;
  if (c.startAirborne) atcF.phase = 3;
  if (militaryFlight) setupMilitaryEncounter();
}

void Game::endFlight(bool success, const std::string& reason, FlightOutcome outcome) {
  PartTimer ptEnd(*this, "flight end");
  if (militaryFlight) {
    if(militarySettled) return;
    militarySettled=true;
    success=success && hiveCombat.mission.status==hive::MissionStatus::Success && !crashed && militaryHull>0;
    result.success=success; result.outcome=success?OUT_SUCCESS:outcome; result.failReason=reason; result.flightMin=flightClock/60.f;
    result.fuelUsedKg=std::max(0.f,fuelStart-plane.fuel); result.maxG=plane.maxG; result.minG=plane.minG;
    commit([&](Career& k) { payout=k.settle(contract,specIdx,Career::SRC_MILITARY,result,&stars,&launchPlan); });
    lastSuccess=success; debriefTitle=success?"Military sortie complete":reason.empty()?"Military sortie aborted":reason;
    coaching.clear(); screen=SCR_DEBRIEF; paused=false; showMap=false; debriefT0 = realTime;
    profileFlightEnd(success);
    if (g_music.owns()) g_music.sting(success ? STING_SUCCESS : STING_FAIL); else g_audio.trigger(success?SFX_SUCCESS:SFX_FAIL);
    announceFlightEnd(success?"Military objectives secured":crashed?(reason=="Hull lost to Hive fire"?reason:"Flight ended after impact"):
      reason=="Military objective lost"?reason:"Flight ended");
    return;
  }
  if (freeFlight) {
    const bool impact=crashed;
    profileFlightEnd(success || outcome == OUT_DIVERTED || outcome == OUT_OFF_AIRPORT);
    returnToFreeFlight();
    toast(success || outcome == OUT_DIVERTED || outcome == OUT_OFF_AIRPORT ? "Free flight complete" : reason.empty() || outcome == OUT_ABANDONED ? "Back in the Free Flight hangar" : reason,
          vec3(0.6f, 0.9f, 1.f), false);
    announceFlightEnd(impact?"Flight ended after impact":"Free flight complete");
    return;
  }
  if (researchFlight) {  // research flights never touch the career: back to the research menu
    hiveCombat.reset(1, combatWrap());
    researchFlight = false; paused = false; showMap = false;
    screen = SCR_RESEARCH; resOpened = realTime;
    if (!reason.empty()) toast(reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));
    announceFlightEnd(crashed?"Flight ended after impact":"Research flight complete");
    return;
  }
  debriefTitle.clear();
  result.success = success;
  result.outcome = success ? OUT_SUCCESS : outcome;
  result.failReason = reason;
  result.flightMin = flightClock / 60.f;
  result.maxG = plane.maxG; result.minG = plane.minG;
  result.fuelUsedKg = std::max(0.f, fuelStart - plane.fuel);
  result.fuelLeftKg = std::max(0.f, plane.fuel);
  result.fuelLeftFrac = plane.spec->maxFuel > 0 ? clampf(plane.fuel / plane.spec->maxFuel, 0.f, 1.f) : 1.f;
  result.fuelLeftMin = cruiseMinutes(*plane.spec, plane.fuel);
  result.shutDownAtStand = success && plane.onGround && !plane.engineRunning && parkingBrake && (plane.onWater ? atDock() : g_world.onRunway(plane.pos.x, plane.pos.z, 6.f) < 0);
  result.touchdownFpm = touchdownFpm;
  result.late = contract.timeLimitMin > 0 && (jobClockBase + flightClock) / 60.f > contract.timeLimitMin;
  result.landed = touchedDown && plane.onGround;
  if (!result.landed) result.touchdownFpm = 0;
  if (success && !isolatedFlight) {   // a checkride flown short of its standard is a fail, whatever else went well
    const std::string f = Career::checkrideFault(contract, result);
    if (!f.empty()) { success = false; result.success = false; result.outcome = OUT_CHECKRIDE_FAILED; result.failReason = "Checkride not passed: " + f; debriefTitle = result.failReason; }
  }
  { PartTimer pt(*this, "flight end: coaching"); coaching = landingCoaching(); lastMark = landingMark(result, *plane.spec); }
  if (isolatedFlight) {   // a practice flight or a trial: back to the hub, nothing settled
    profileFlightEnd(success);
    const bool trial = contract.type == CT_TRIAL;
    if (trial) finishTrial(success);
    isolatedFlight = false; paused = false; showMap = false; screen = SCR_HUB; hubTab = TAB_CONTRACTS;
    if (!trial) toast(success ? "Practice flight complete" : reason.empty() ? "Practice flight over" : reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));   // (a trial: its card says it)
    announceFlightEnd(success?"Practice flight complete":"Practice flight over");
    return;
  }
  Contract c = haul.on ? haul.job : contract;   // (a long haul settles as it was accepted)
  haul = Haul{};
  if (c.type == CT_FERRY) { c.story = false; }
  // the settlement is one transaction: the career only changes when the save succeeds; a failed save keeps the
  // settled copy pending (the debrief shows it, and offers the retry), and a retry can never pay twice
  { std::vector<PayoutLine> lines; int st = 0;
    bool jobLeg = career.job && career.job->state == Career::JobState::ACTIVE && career.job->c.id == c.id;
    bool jobGoesOn = false;
    PartTimer pt(*this, "flight end: career settled and saved");
    commit([&](Career& k) {
      k.attemptOpen = false;
      if (!jobLeg) {   // (a free flight flown while a job waits at its stop: the job stays as it is)
        lines = k.settle(c, specIdx, source, result, &st, &launchPlan);
        if (!(k.job && k.job->state == Career::JobState::RECOVERY)) k.job.reset();
        return;
      }
      const AircraftSpec& s = kAircraft[specIdx];
      if (success) { lines = k.settleJob(result, launchPlan, &st); return; }
      // a leg that ended short: the job carries on where the load is (a crash or running dry in the air ends it)
      if (outcome == OUT_DIVERTED && result.divertedTo >= 0) { lines = k.closeLeg(result, launchPlan, result.divertedTo, 0, ""); jobGoesOn = true; }
      else if (outcome == OUT_OFF_AIRPORT && result.landed) {   // the load is brought by road to the nearest airport
        int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z);
        lines = k.closeLeg(result, launchPlan, std::max(ap, 0), source == Career::SRC_LESSON ? 0 : 150 + (int)s.rentFee, "Aircraft and load recovered from the field"); jobGoesOn = true;
      } else if (outcome == OUT_ABANDONED && !crashed) {   // the load goes back to where this leg began
        lines = k.closeLeg(result, launchPlan, attemptFrom, source == Career::SRC_LESSON ? 0 : 150 + (int)s.rentFee, "Load returned to the departure"); jobGoesOn = true;
      } else { lines = k.settle(c, specIdx, source, result, &st, &launchPlan); k.job.reset(); }
    });
    payout = lines; stars = st;
    if (jobGoesOn && career.job) debriefTitle = fmt("%s - job continues from %s", reason.c_str(), g_world.airports[career.job->at].code);
  }
  lastSuccess = success;
  if (c.type == CT_LESSON) { if (success) lessonFails.erase(c.id); else lessonFails[c.id]++; }   // (twice running: the debrief offers calm air)
  if (debriefTitle.empty()) debriefTitle = success ? (c.type == CT_FERRY ? "Flight complete" : "Contract complete!") : (reason.empty() ? "Flight failed" : reason);
  // the profile: the landing's mark, a flight by the book, a flawless one; then the career's milestones
  if (success && result.landed) {
    if (lastMark.letter == 'S') award("grade_s");
    if (lastMark.letter == 'S' && stars == 3 && c.payout > 0) award("flawless");
    if (result.shutDownAtStand) award("by_the_book");
  }
  if (success && c.type == CT_TOUR && !c.story && result.wpDone >= (int)c.wps.size()) award("photo");
  if (success && c.id.rfind("LEG", 0) == 0) {   // a legendary contract: the pilot's, for good
    profile.num["legend." + c.id] = 1.f;
    bool all = true; for (const Contract& l : Career::legendaryContracts()) all &= profile.count("legend." + l.id) > 0.f;
    if (all) award("legend");
  }
  { PartTimer pt(*this, "flight end: profile"); profileFlightEnd(success); profileCareerCheck(); }
  if (g_music.owns()) g_music.sting(success ? STING_SUCCESS : STING_FAIL); else g_audio.trigger(success ? SFX_SUCCESS : SFX_FAIL);
  if (success && c.payout > 0) g_audio.trigger(SFX_CASH);
  debriefT0 = realTime;
  screen = SCR_DEBRIEF;
  paused = false; showMap = false;
  PartTimer pt(*this, "flight end: announced");
  announceFlightEnd(result.outcome==OUT_CHECKRIDE_FAILED?"Checkride not passed":success?"Flight complete":crashed?"Flight ended after impact":"Flight ended",coaching);
}

// One coaching point from what was recorded on the arrival: the most important thing to work on, or what went well
std::string Game::landingCoaching() const {
  const FlightResult& r = result;
  if (r.goArounds > 0 && touchedDown) return r.goArounds == 1 ? "Good call going around: a go-around is always the safe decision when an approach isn't working."
                                                               : fmt("%d go-arounds: set up earlier - on the centreline, at approach speed, by 3 km out.", r.goArounds);
  if (!result.landed || r.tdPastThrM < 0) return "";
  float vref = plane.spec->vref * MS_TO_KT, fpm = touchdownFpm;
  if (touchdownGrade(fpm) == TD_HARD) return fmt("Touchdown at %.0f fpm: hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.", fpm);
  if (r.thrKt > vref * 1.3f) return fmt("You crossed the threshold at %.0f kt (aim for about %.0f): slow down earlier on final - extra speed floats and uses up runway.", r.thrKt, vref + 5.f);
  if (r.tdPastThrM > r.rwyLenM * 0.45f) return fmt("You touched down %.0f m past the threshold, beyond the middle of the runway: aim for the first third.", r.tdPastThrM);
  if (r.stopLeftM >= 0 && r.stopLeftM < r.rwyLenM * 0.12f) return fmt("You stopped with only %.0f m of runway to spare: brake firmly once all wheels are down.", std::max(0.f, r.stopLeftM));
  if (r.thrKt > 0 && r.thrKt < vref * 0.95f) return fmt("Slow over the threshold (%.0f kt, near the stall): keep about %.0f kt until the flare.", r.thrKt, vref + 5.f);
  if (r.thrAglM > 40.f) return fmt("High over the threshold (%.0f m): get down onto a 3 degree path earlier.", r.thrAglM);
  std::string well = "Well flown";
  if (r.thrKt > 0) well += fmt(": %.0f kt over the threshold", r.thrKt);
  well += fmt(", down %.0f m in", r.tdPastThrM);
  if (r.stopLeftM >= 0) well += fmt(", %.0f m to spare", r.stopLeftM);
  return well + ".";
}

// a job's arrival waits at the field for the pilot to park (lessons and checkride lessons, trials and practice
// flights end at the stop - their briefings and recorded lines say so - as do the tests' bots)
bool Game::arrivalWaits() const {
  return !botControl && !isolatedFlight && contract.type != CT_LESSON && contract.type != CT_TRIAL && !researchFlight && !militaryFlight;
}

int Game::computePhase() const {
  if (!plane.spec) return 0;
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (touchedDown && plane.onGround) return 6;
  if ((plane.onGround || flightClock < 1.f) && !takeoffAnnounced) return gs < 3.f ? 0 : 1;   // (settling onto the runway at the start)
  bool finalLeg = contract.wps.empty() || wpIndex >= (int)contract.wps.size() - 1;
  const Airport& d = dest();
  float dd = length(vec3(plane.pos.x - d.x, 0, plane.pos.z - d.z));
  if (finalLeg && !contract.wps.empty() && wpIndex >= (int)contract.wps.size()) finalLeg = true;
  if (finalLeg && (contract.to != contract.from || wpIndex >= (int)contract.wps.size())) {
    if (dd < d.length * 0.5f + 1800.f && plane.agl() < 180.f) return 5;
    if (dd < 6000.f) return 4;
  }
  if (plane.agl() < 300.f && length(vec3(plane.pos.x - g_world.airports[contract.from].x, 0, plane.pos.z - g_world.airports[contract.from].z)) < 4000.f) return 2;
  return 3;
}

// the autopilot is flying a leg where time acceleration is safe (holds, en route, descent orbit - not the approach)
void Game::cycleApDest(int dir) {
  int n = (int)g_world.airports.size();
  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  auto d2 = [&](int i) { float dx = g_world.wrap(g_world.airports[i].x - plane.pos.x), dz = g_world.wrap(g_world.airports[i].z - plane.pos.z); return dx * dx + dz * dz; };   // (the short way round the map)
  std::sort(order.begin(), order.end(), [&](int a, int b) { return d2(a) < d2(b); });
  if (apDest < 0) apDest = haulPending() ? std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z)) : (int)(&dest() - &g_world.airports[0]);   // first pick: the contract's destination (a long haul's not yet on this chart: the nearest)
  else {
    int k = (int)(std::find(order.begin(), order.end(), apDest) - order.begin());
    apDest = order[((k + dir) % n + n) % n];
  }
  g_audio.trigger(SFX_CLICK);
}

// What keeps the flight at real time now ("" nothing): the ground and the approach, low or steep over the terrain, the
// destination or a checkpoint close, a warning, a visitor, combat. The autopilot's cruise may warp lower and nearer
// the field than the pilot's hands (its own approach turns it off)
std::string Game::warpBlocker() const {
  if (militaryFlight || hiveCombat.aliveCount() > 0 || hiveCombat.mission.status == hive::MissionStatus::Active) return "combat runs at real time";
  if (crashed || plane.onGround) return "on the ground";
  if (plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV) return "the autopilot is on its approach";
  if (plane.apOn && plane.apMode == Plane::AP_STUNT) return "aerobatics fly at real time";
  const float dd = length(vec3(g_world.wrap(plane.pos.x - dest().x), 0, g_world.wrap(plane.pos.z - dest().z)));
  if (!apCruising()) {
    if (plane.agl() < 250.f) return "too low over the ground";
    if (dd < 3500.f && !freeFlight) return "nearing the destination";
    for (float t : {5.f, 10.f, 20.f}) {   // (where the path it is on goes in the next 20 s of flight)
      const vec3 q = plane.pos + plane.vel * t;
      if (q.y < std::max(g_world.height(q.x, q.z), 0.f) + 150.f) return "terrain ahead";
    }
  }
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    if (length(vec3(g_world.wrap(plane.pos.x - w.x), plane.pos.y - w.alt, g_world.wrap(plane.pos.z - w.z))) < 1500.f) return "checkpoint ahead";
  }
  if (plane.stallWarn > 0.6f) return "stall warning";
  if (gTunnel > 0.05f) return "high g";
  if (ufo.on) return "something is alongside";
  if (plane.spec && cruiseMinutes(*plane.spec, plane.fuel) < 15.f && !plane.spec->special) return "low fuel";
  return "";
}

bool Game::apCruising() const {
  return plane.apOn && (plane.apMode == Plane::AP_HOLD || (plane.apMode == Plane::AP_APPR && plane.apStage == Plane::APS_NAV));
}

void Game::engageAutopilot() {
  if (plane.fail.avionicsDark()) { toast("Autopilot unavailable - battery flat", vec3(1, 0.45f, 0.35f)); return; }
  if (apDest >= 0) {
    plane.apEngage(Plane::AP_NAV, apDest, wx);
    const Airport& a = g_world.airports[apDest];
    if (!plane.apOn) { toast("Autopilot: unable - " + plane.apDecline, vec3(1, 0.45f, 0.35f)); return; }   // (no power, no runway in reach)
    if (plane.apAirport >= 0 && plane.apAirport != apDest) { toast("Autopilot: " + plane.apDecline, vec3(1, 0.75f, 0.35f)); toast(fmt("Autopilot: GLIDING to %s runway %02d", g_world.airports[plane.apAirport].code, g_world.airports[plane.apAirport].rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f)); }
    else if (!plane.apDecline.empty()) toast("Autopilot: unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
    else toast(fmt("Autopilot: AUTOLAND %s runway %02d", a.code, a.rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f));
    toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
  } else {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    if (haulPending() && plane.apOn && earthDestination) {   // a long haul: the autopilot holds the course for its destination
      earthCourseHold = true;
      if (auto heading = regionTravel::Chart(flightRegion.origin).bearing(plane.pos, haul.place)) plane.apHeading = (float)*heading;
      toast(fmt("Autopilot ON: the course for %s, %.0f km - it hands back 9 km out", haul.target.code,
        geo::surfaceDistance(earthPosition, haul.place) / 1000.), vec3(0.6f, 1, 0.6f));
    } else {
      toast("Autopilot ON: the stick trims heading and altitude, throttle sets speed", vec3(0.6f, 1, 0.6f));
      toast("Pick an AUTOLAND airport on the GPS map to fly there", vec3(0.8f, 0.8f, 0.8f));
    }
  }
  g_audio.trigger(SFX_CLICK);
}

void Game::flightControls(float dt) {
  Controls& c = plane.ctl;
  auto key = [&](int k) { return in.down[k]; };
  float pitchIn = ((actKey(ACT_PITCH_UP) || key(K_DOWN)) ? 1.f : 0.f) - ((actKey(ACT_PITCH_DN) || key(K_UP)) ? 1.f : 0.f);
  float rollIn = ((actKey(ACT_ROLL_R) || key(K_RIGHT)) ? 1.f : 0.f) - ((actKey(ACT_ROLL_L) || key(K_LEFT)) ? 1.f : 0.f);
  float yawIn = (actKey(ACT_YAW_R) ? 1.f : 0.f) - (actKey(ACT_YAW_L) ? 1.f : 0.f);
  if (set.invertPitch) pitchIn = -pitchIn;
  float padP = 0, padR = 0, padY = 0;
  if (in.pad) {
    auto dz = [](float v) { return fabsf(v) < 0.12f ? 0.f : (v - (v > 0 ? 0.12f : -0.12f)) / 0.88f; };
    padP = -dz(in.ly) * (set.invertPitch ? -1.f : 1.f); padR = dz(in.lx);
    padY = (actPad(ACT_YAW_R) ? 1.f : 0.f) - (actPad(ACT_YAW_L) ? 1.f : 0.f);
    padP += (actPad(ACT_PITCH_UP) ? 1.f : 0.f) - (actPad(ACT_PITCH_DN) ? 1.f : 0.f);
    padR += (actPad(ACT_ROLL_R) ? 1.f : 0.f) - (actPad(ACT_ROLL_L) ? 1.f : 0.f);
    padP = padP * fabsf(padP) * 0.4f + padP * 0.6f; padR = padR * fabsf(padR) * 0.4f + padR * 0.6f;
  }
  // joystick, pedals and throttle lever (joystick.h): positions, each through its binding's dead zone and curve. The
  // lever has the throttle once it moves, and gives it up to a key, trigger or number key until it moves again
  bool jP = false, jR = false, jY = false, jT = false;
  padP += joyAxis(AX_PITCH, &jP); padR += joyAxis(AX_ROLL, &jR); padY += joyAxis(AX_YAW, &jY);
  const float lever = joyAxis(AX_THROTTLE, &jT);
  if (!jT) { joyThrTaken = false; joyThrLast = -1; }
  else if (joyThrLast < 0) joyThrLast = lever;   // (bound, back, or a new flight: it takes over when it moves, not before)
  const bool leverMoved = jT && fabsf(lever - joyThrLast) > (joyThrTaken ? 0.f : 0.02f);
  bool manual = fabsf(pitchIn) + fabsf(rollIn) > 0 || fabsf(padP) + fabsf(padR) > 0.3f;
  // (a refused autoland's hold too: the pilot asked for a landing, not a hold, and was told the stick takes over - the
  // review of v3.31.0, F3)
  bool apNav = plane.apOn && (plane.apMode == Plane::AP_APPR || plane.apMode == Plane::AP_STUNT || (plane.apMode == Plane::AP_HOLD && !plane.apDecline.empty()));
  // aerobatics: a figure is flown only while the pilot leaves the aircraft alone. Any flight input - a stick, a trigger,
  // a key or button for anything the aircraft does - hands it straight back, wherever the figure has got to; the view,
  // the HUD, the map, the radio and the pause don't (the stunt key itself: below)
  if (plane.apOn && plane.apMode == Plane::AP_STUNT) {
    bool touched = fabsf(pitchIn) + fabsf(rollIn) + fabsf(yawIn) + fabsf(padP) + fabsf(padR) + fabsf(padY) > 0.f || (in.pad && (in.lt > 0.1f || in.rt > 0.1f)) || (leverMoved && (!joyThrTaken || fabsf(lever - joyThrLast) > 0.01f));
    for (int a = 0; a < ACT_COUNT && !touched; a++) if (a != ACT_STUNT && a != ACT_AP && (a < ACT_CAMERA || a > ACT_ZOOM)) touched = actDown(a);   // (the autopilot key turns it all off: below)
    for (int k : {K_PGUP, K_PGDN, K_PLUS, K_MINUS, K_HOME, K_END}) touched = touched || in.down[k];
    for (int k = '0'; k <= '9'; k++) touched = touched || in.down[k];
    if (in.pad && !touched) {   // (and any other button: all but the view's, the instruments' and the pause)
      unsigned keep = PAD_START | set.padBind[ACT_STUNT] | set.padBind[ACT_AP];
      for (int a = ACT_CAMERA; a <= ACT_ZOOM; a++) keep |= set.padBind[a];
      touched = (in.buttonsPressed & ~keep) != 0;
    }
    if (touched) {
      plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
      apNav = false;
    }
  }
  if (plane.apOn) {
    if (apNav) {   // flying a GPS route / autoland: any real stick input hands control back
      if (fabsf(pitchIn + padP) > 0.5f || fabsf(rollIn + padR) > 0.5f) {
        plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
      }
    } else {       // hold mode: the stick moves the targets (A/D heading, W/S altitude)
      plane.apHeading = wrapDeg360(plane.apHeading + (rollIn + padR) * 20.f * dt);
      plane.apAlt = std::max(plane.apAlt + (pitchIn + padP) * 45.f * dt, g_world.height(plane.pos.x, plane.pos.z) + 60.f);
    }
  }
  // Flight assists (Settings): the inputs command rates and the aircraft holds its attitude (Plane::assistControl) -
  // the keyboard and the gamepad by default, every input when asked, and always in time warp; in the air only
  const bool joyFlying = jP || jR;
  // (the research craft's own fly-by-wire already takes rate commands: assisted only in the warp)
  plane.assistOn = !plane.apOn && !plane.onGround && (timeAccel > 1.f || (!plane.spec->special && (set.assist == 2 || (set.assist == 1 && !joyFlying))));
  if (!plane.apOn) {
    float tp = clampf(pitchIn + padP, -1, 1), tr = clampf(rollIn + padR, -1, 1);
    float rate = joyFlying ? 14.f : manual ? 2.8f : 5.f;   // (a joystick's position is the control's, near enough at once)
    if (plane.assistOn) {   // (the commands eased in as the keys are - a tap is a small change - and out at once)
      auto ease = [&](float cur, float tgt) { return approach(cur, tgt, joyFlying ? 14.f : fabsf(tgt) < fabsf(cur) ? 16.f : 4.f, dt); };
      plane.assistPitch = ease(plane.assistPitch, tp); plane.assistRoll = ease(plane.assistRoll, tr);
    } else {
      plane.assistPitch = plane.assistRoll = 0.f;
      c.pitch = approach(c.pitch, tp, rate, dt);
      c.roll = approach(c.roll, tr, rate, dt);
    }
  } else plane.assistPitch = plane.assistRoll = 0.f;
  (void)manual;
  // XR-40 weapons hot: the bumpers fire and bomb, so the rudder is frozen until weapons are safe again
  if (plane.spec->special == 2 && somnus.armed && !plane.onGround) { yawIn = 0; padY = 0; }
  c.yaw = approach(c.yaw, clampf(yawIn + padY, -1, 1), jY ? 14.f : 4.f, dt);
  plane.assistYaw = plane.assistOn ? c.yaw * 0.6f : 0.f;   // (assisted, the rudder pedals add to the coordination)
  // throttle
  float thr = 0;
  if (actDown(ACT_THR_UP) || key(K_PGUP) || key(K_PLUS)) thr += 0.55f;
  if (actDown(ACT_THR_DN) || key(K_PGDN) || key(K_MINUS)) thr -= 0.55f;
  if (in.pad) thr += (in.rt - in.lt) * 0.6f;
  bool numKey = in.pressed['0'];
  for (int k = 1; k <= 9; k++) numKey |= in.pressed['0' + k] && !actKey(ACT_THR_DN);
  const bool leverTakes = leverMoved && !joyThrTaken;
  if (leverMoved) { joyThrTaken = true; joyThrLast = lever; }
  if (thr != 0 || numKey) { joyThrTaken = false; joyThrLast = jT ? lever : -1.f; }   // (the last throttle input wins)
  if (plane.apOn && plane.apMode == Plane::AP_HOLD && plane.apSpeed > 0 && (numKey || leverTakes)) { plane.apSpeed = 0; toast("Autothrottle off - manual throttle", vec3(1, 0.85f, 0.5f)); }
  // (while the autopilot has the throttle the lever waits where it is: handed back, the power stays until it moves)
  if (apNav || (plane.apOn && plane.apSpeed > 0)) { joyThrTaken = false; joyThrLast = jT ? lever : -1.f; }
  if (apNav) {}   // the autopilot has the throttle
  else if (plane.apOn && plane.apSpeed > 0) {   // autothrottle: throttle inputs move the speed target
    const AircraftSpec& sp = *plane.spec;
    plane.apSpeed = clampf(plane.apSpeed + thr * 30.f * dt, sp.vref * 1.2f, sp.cruise * (sp.special ? 1.6f : 1.15f));
  } else {
    c.throttle = clampf(c.throttle + thr * dt, 0, 1);
    if (joyThrTaken) c.throttle = lever;
    for (int k = 1; k <= 9; k++) if (in.pressed['0' + k] && !actKey(ACT_THR_DN)) c.throttle = k / 9.f;
    if (in.pressed['0']) c.throttle = 0;
  }
  // trim
  float tr = (actDown(ACT_TRIM_UP) || key(K_HOME) ? 1.f : 0.f) - (actDown(ACT_TRIM_DN) || key(K_END) ? 1.f : 0.f);
  c.trim = clampf(c.trim + tr * 0.35f * dt, -1, 1);
  // flaps
  auto flapToast = [&]() {
    if (plane.spec->special == 3)   // (the XR-50's lever swivels its nozzles; the lift fan comes in with them, slow)
      toast(flapNotch > 0.99f ? "Nozzles 95 deg, lift fan in - VTOL" : flapNotch > 0.01f ? fmt("Nozzles %d deg, lift fan in - STOVL", (int)lroundf(flapNotch * 95)) : "Nozzles aft - lift fan out", vec3(1.f, 0.72f, 0.3f));
    else if (plane.spec->special == 2) toast(flapNotch > 0.99f ? "Pods 90 deg - VTOL hover" : fmt("Pods %d deg", (int)lroundf(flapNotch * 90)), vec3(0.8f, 0.55f, 1));
    else toast(fmt("Flaps %d%%", (int)lroundf(flapNotch * 100)), vec3(0.8f, 0.9f, 1));
    if (plane.spec->special == 2 && flapNotch > 0.99f && !plane.apOn)
      toast("VTOL: hold " + actLabel(ACT_THR_UP, padActive, true) + "; release to idle", vec3(0.8f, 0.55f, 1));
  };
  if (apNav) flapNotch = c.flaps;   // the autopilot runs the flaps on the approach
  else if (plane.spec->special == 1) flapNotch = 0;   // the XR-30 has no flaps: a delta wing and canards
  else {
    if (actPressed(ACT_FLAPS_DN)) { flapNotch = std::min(1.f, flapNotch + 1.f / 3.f); flapToast(); }
    if (actPressed(ACT_FLAPS_UP)) { flapNotch = std::max(0.f, flapNotch - 1.f / 3.f); flapToast(); }
  }
  c.flaps = flapNotch;
  // gear
  bool wrAir = plane.spec->special == 2 && !plane.onGround;   // XR-40 in the air: gamepad Y is weapons hot / safe instead of the gear
  bool gearPad = actPadP(ACT_GEAR) && !(wrAir && set.padBind[ACT_GEAR] == set.padBind[ACT_WEAPONS]);
  if ((actKeyP(ACT_GEAR) || gearPad || actJoyP(ACT_GEAR) || (!showMap && (in.buttonsPressed & PAD_RIGHT))) && plane.spec->retract) {
    if (plane.onGround && c.gearDown) toast("Gear lever is locked on the ground", vec3(1, 0.6f, 0.4f));
    else { c.gearDown = !c.gearDown; toast(c.gearDown ? "Gear down" : "Gear up", vec3(0.8f, 1, 0.8f)); }
    if (plane.fail.gearStuck) toast(plane.fail.gearStuck == 1 ? "Gear won't come down - it's stuck up" : "Gear won't retract - it's stuck down", vec3(1, 0.45f, 0.35f));
  }
  // brakes: B / D-pad left = parking brake toggle, Space = wheel brakes
  bool& parking = parkingBrake;   // (set by startFlight for a start on the ground)
  if (plane.apDone) {
    plane.apDone = false; parking = true;
    if (plane.apOverrun) toast("Autoland: stopped past the end of the runway - parking brake set", vec3(1, 0.45f, 0.35f));
    else toast(plane.onWater ? (atDock() ? "Autoland complete - moored at the dock" : "Autoland complete - moored") : "Autoland complete - parking brake set", vec3(0.5f, 1, 0.6f));
    plane.apOverrun = false; g_audio.trigger(SFX_AP_DISC, 0.7f);
  }
  // a figure flown (or given up) and level again: aerobatics ends, and the aircraft goes back to what was flying it
  // before - the pilot (the autopilot off), the autopilot's hold, or its route to the field it was flying to (the
  // recorded lines: what the autopilot's own disconnect and engagement say)
  if (plane.apStuntEnded) {
    plane.apStuntEnded = false;
    if (!plane.apOn) {}   // (already handed back)
    else if (!plane.apStuntWasOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f)); }
    else if (plane.apStuntWasAirport >= 0) { const int keep = apDest; apDest = plane.apStuntWasAirport; engageAutopilot(); apDest = keep; }
    else toast("Autopilot ON: the stick trims heading and altitude, throttle sets speed", vec3(0.6f, 1, 0.6f));
  }
  if (plane.spec->special == 2) somnusControls(dt);
  else if (plane.spec->special == 3) morpheusControls(dt);
  // on the water the same control moors the aircraft (once all but stopped: the physics makes the lines fast) and
  // casts it off (docs/WINGS_OVER_WATER.md)
  if (actKeyP(ACT_PARK) || actJoyP(ACT_PARK) || (!showMap && actPadP(ACT_PARK))) {
    if (plane.onWater && !parking && length(vec3(plane.vel.x, 0.f, plane.vel.z)) > 1.5f) toast("Too fast to moor: slow to a stop first", vec3(1, 0.7f, 0.4f));
    else {
      parking = !parking;
      toast(plane.onWater ? (parking ? (atDock() ? "Moored at the dock" : "Moored") : "Cast off") : parking ? "Parking brake SET" : "Parking brake released", vec3(1, 0.85f, 0.5f));
    }
  }
  float wb = std::max(actDown(ACT_BRAKE) ? 1.f : 0.f, joyAxis(AX_BRAKE));   // (toe brakes: as far as they are pressed)
  if (wb > 0 && parking && plane.onGround && !plane.onWater && length(plane.vel) > 2.f) parking = false;
  c.brake = parking ? 1.f : wb;
  plane.moorWanted = parking;
  // autopilot: Z / right stick click. With an autoland airport picked on the GPS it flies there and lands;
  // otherwise it holds heading, altitude and speed
  if (actPressed(ACT_AP)) {
    if (plane.apOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot OFF", vec3(1, 0.7f, 0.3f)); }
    else if (plane.onGround) toast("Autopilot needs to be airborne", vec3(1, 0.6f, 0.4f));
    else engageAutopilot();
  }
  // aerobatics: the autopilot flies the next figure, sized to this aircraft (it climbs or dives for the height and
  // speed first), once; pressed again during it, like any other input, it hands the aircraft back at once
  if (actPressed(ACT_STUNT)) {
    if (plane.onGround) toast("Aerobatics need to be airborne", vec3(1, 0.6f, 0.4f));
    else if (plane.apOn && plane.apMode == Plane::AP_STUNT) {
      plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
    } else {
      plane.apStuntBegin(stuntNext, wx);
      toast(fmt("Aerobatics: %s", Plane::stuntName(plane.apStunt)), vec3(0.6f, 1, 0.6f));
      toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      stuntNext = (stuntNext + 1) % Plane::STUNT_COUNT;
      g_audio.trigger(SFX_CLICK);
    }
  }
  if (actPressed(ACT_LIGHTS)) { landingLight = !landingLight; toast(landingLight ? "Landing lights ON" : "Landing lights OFF"); }
  // the engine key: pressed with the engine stopped, it starts it; held a second with it running, on the ground at a
  // walking pace or slower, it shuts it down (the arrival's shutdown at the stand: a starter press brings it back)
  {
    const float gsNow = length(vec3(plane.vel.x, 0, plane.vel.z));
    if (actDown(ACT_ENGINE) && plane.engineRunning && plane.onGround && gsNow < 3.f && !engineHeld) {
      engineHold += dt;
      if (engineHold >= 1.f) {
        plane.engineRunning = false; plane.starterTime = 0; engineShutDown = true; engineHeld = true;
        toast(plane.spec->engines > 1 ? "Engines shut down" : "Engine shut down", vec3(0.85f, 0.95f, 1.f), false);
        g_audio.trigger(SFX_CLICK);
      }
    } else if (!actDown(ACT_ENGINE)) { engineHold = 0; engineHeld = false; }
    if (actPressed(ACT_ENGINE) && !plane.engineRunning && plane.fuel > 0 && !engineHeld) { plane.starterTime = 0.01f; engineShutDown = false; toast("Engine start"); }
  }
  // time acceleration
  if (actPressed(ACT_TIME)) {
    const std::string why = warpBlocker();
    if (timeAccel >= 16.f) { timeAccel = 1; toast("Time warp off"); }
    else if (!why.empty()) { timeAccel = 1; toast("Time warp unavailable - " + why, vec3(1, 0.7f, 0.4f)); }
    else {
      timeAccel *= 2.f;
      toast(fmt("Time warp x%.0f", timeAccel), vec3(0.45f, 0.9f, 1.f));
      if (timeAccel > 4.f && !warpWarned) {   // (once a session: the cost is the PC's, not the flight's)
        warpWarned = true;
        toast("Above x4 the world streams in much faster: slower PCs may stutter", vec3(0.85f, 0.85f, 0.85f), false);
      }
      if (!plane.apOn) toast("Warp flies assisted: let go and it holds its bank and its climb", vec3(0.85f, 0.95f, 1.f), false);
    }
  }
  // Only the explicitly selected 90-degree hover notch uses momentary manual power. Resolve it after the pod
  // and autopilot switches so entry, stick takeover and AP disengagement cannot carry a stale throttle command
  // for one physics step. Intermediate / forward notches and every engaged autopilot retain their normal law.
  if (!plane.apOn && plane.spec->special == 2 && c.flaps > 0.99f) {
    float power = 0.f;
    if (actOk(ACT_THR_UP)) {
      if (actDown(ACT_THR_UP) || key(K_PGUP) || key(K_PLUS)) power = 1.f;
      for (int k = 1; k <= 9; k++) if (key('0' + k)) power = std::max(power, k / 9.f);
      if (joyThrTaken) power = std::max(power, lever);
      const float rt = in.pad && !vtolRtUnarmed && in.rt > 0.05f ? in.rt : 0.f;
      const float lt = in.pad && !vtolLtUnarmed && in.lt > 0.05f ? in.lt : 0.f;
      power = clampf(std::max(power, rt) - lt, 0.f, 1.f);
      if (actDown(ACT_THR_DN) || key(K_PGDN) || key(K_MINUS) || key('0')) power = 0.f;
    }
    c.throttle = power;   // engineSpool still follows this target at the aircraft's existing spool rate
  }
}

// On the water (docs/WINGS_OVER_WATER.md): spray off the floats - heaviest where the bow wave breaks through the hump,
// a thin sheet thrown from the steps once on the plane - mist where the propeller's blast meets the water at high power
// and low speed, and the wake: a patch of foam dropped at each float's stern every sixteenth of a second, spreading and
// fading where it was left, and every half second a ring at its edge (drawn flat on the water: buildSprites)
void Game::updateWaterEffects(float dt, float gs) {
  for (WakeRing& w : wakeRings) w.age += dt;
  wakeRings.erase(std::remove_if(wakeRings.begin(), wakeRings.end(), [](const WakeRing& w) { return w.age >= w.life; }), wakeRings.end());
  if (!plane.spec || !plane.spec->floats || !plane.onWater || crashed) { waterSprayAccum = propMistAccum = 0.f; return; }
  const FloatStations fs = floatStations(*plane.spec);
  const float hump = smoothstepf(1.5f, 6.f, gs) * (1.f - 0.7f * smoothstepf(11.f, 20.f, gs)), plane_ = smoothstepf(12.f, 26.f, gs);
  waterSprayAccum += dt * (hump * 55.f + plane_ * 30.f) * (0.5f + plane.floatWet);
  const float zBreak = fs.bowZ + (fs.stepZ - fs.bowZ) * (0.3f + 0.7f * smoothstepf(8.f, 20.f, gs));   // (the bow wave's break, aft to the step on the plane)
  while (waterSprayAccum > 1.f) {
    waterSprayAccum -= 1.f;
    for (int side = -1; side <= 1; side += 2) {
      const vec3 wp = plane.pos + plane.q.rotate(vec3(side * (fs.x + fs.beam * 0.5f), fs.keelY, zBreak));
      const float u = (rand() % 100) * 0.01f, h = (rand() % 100) * 0.01f;
      spawn(vec3(wp.x, 0.08f, wp.z), plane.vel * (0.35f + 0.3f * plane_) + plane.right() * (side * (1.2f + gs * 0.1f) * (0.6f + 0.6f * u)) + vec3(0, (0.9f + gs * 0.07f) * (0.6f + 0.7f * h), 0),
            0.8f + 0.7f * u, 0.35f, 2.4f + plane_, vec3(0.92f, 0.94f, 0.97f), 0.36f + 0.14f * hump, SPR_SMOKE, 1.3f, -7.f);
    }
  }
  if (plane.engineRunning && plane.engineSpool > 0.55f && gs < 12.f) {   // the propeller's blast on the water
    propMistAccum += dt * 14.f * (plane.engineSpool - 0.5f) * (1.f - gs / 12.f);
    while (propMistAccum > 1.f) {
      propMistAccum -= 1.f;
      const vec3 nose = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
      spawn(vec3(nose.x, 0.3f, nose.z) + plane.right() * ((rand() % 100 - 50) * 0.04f), plane.q.rotate(vec3(0, 0, 6.f)) + vec3(0, 0.8f, 0),
            1.8f, 0.6f, 2.4f, vec3(0.86f, 0.89f, 0.92f), 0.14f, SPR_SMOKE, 1.f, 0.1f);
    }
  } else propMistAccum = 0.f;
  wakeAccum += dt;
  if (gs > 0.6f && wakeAccum > 0.0625f) {
    wakeAccum = 0.f;
    const bool ring = (wakeTick++ % 8) == 0;
    for (int side = -1; side <= 1; side += 2) {
      const vec3 st = plane.pos + plane.q.rotate(vec3(side * fs.x, fs.keelY, fs.sternZ));
      const float a = clampf(gs / 5.f, 0.2f, 1.f) * (0.6f + 0.4f * hump + 0.3f * plane_);
      wakeRings.push_back({vec3(st.x, 0.04f, st.z), 0.f, 5.f + gs * 0.25f, 0.8f + gs * 0.09f, a, false});
      if (ring) wakeRings.push_back({vec3(st.x, 0.05f, st.z), 0.f, 3.f + gs * 0.1f, 1.5f + gs * 0.2f, a, true});
    }
    if (wakeRings.size() > 1200) wakeRings.erase(wakeRings.begin(), wakeRings.begin() + (wakeRings.size() - 1200));
  }
}

// checkpoint gate i: centre and half-extent axes (gate faces along the leg leading to it)
bool Game::ringGeom(int i, vec3& c, vec3& ax, vec3& ay) const {
  if (i < 0 || i >= (int)contract.wps.size()) return false;
  const Waypoint& w = contract.wps[i];
  c = vec3(w.x, w.alt, w.z);
  vec3 prev = i == 0 ? g_world.airports[contract.from].pos() : vec3(contract.wps[i - 1].x, contract.wps[i - 1].alt, contract.wps[i - 1].z);
  vec3 nd = normalize(vec3(c.x - prev.x, 0, c.z - prev.z) + vec3(0.001f, 0, 0));
  ax = normalize(cross(nd, vec3(0, 1, 0))) * 90.f; ay = vec3(0, 90.f, 0);
  return true;
}

void Game::updateShake(float dt) {
  shakeTrauma = std::max(0.f, shakeTrauma - dt * 1.6f);
  float b = 0.f;
  const bool live = screen == SCR_FLIGHT && !paused && !crashed && plane.spec && timeAccel <= 1.f;
  if (live) {
    const float hp = plane.gLoad - gLp; gLp += (plane.gLoad - gLp) * (1.f - expf(-dt * 2.5f));
    b += clampf(fabsf(hp) * 0.55f, 0.f, 0.5f);                        // (the air's bumps, felt as g)
    b += smoothstepf(0.45f, 1.f, plane.stallWarn) * 0.45f;            // (the stall's buffet)
    b += gTunnel * 0.35f;                                             // (near the airframe's limit)
    if (plane.onGround) {
      const float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
      b += clampf(plane.groundRough, 0.f, 1.f) * smoothstepf(2.f, 30.f, gs) * 0.35f + smoothstepf(5.f, 50.f, gs) * 0.05f;
    }
  } else gLp = plane.spec ? plane.gLoad : 1.f;
  shakeBuffet += (b - shakeBuffet) * (1.f - expf(-dt * 8.f));
  if (!live) shakeBuffet = std::min(shakeBuffet, 0.f + b);
  shakeT += dt;
  const float on = live || (screen == SCR_FLIGHT && crashed && !paused) ? 1.f : 0.f;
  rumbleLo = set.rumble ? on * clampf(shakeTrauma * shakeTrauma * 1.2f + shakeBuffet * 0.5f, 0.f, 1.f) : 0.f;
  rumbleHi = set.rumble ? on * clampf(shakeBuffet * 0.9f + shakeTrauma * 0.3f, 0.f, 1.f) : 0.f;
}

void Game::updateFlight(float dt) {
  updateShake(paused ? 0.f : dt);
  if (paused) { rumbleLo = rumbleHi = 0.f; return; }
  if (!botControl) flightControls(dt);
  // automatic engine start sequence after the scene fades in
  if (!engineAutoStarted && !contract.startAirborne) {
    startDelay -= dt;
    if (startDelay <= 0) { engineAutoStarted = true; plane.starterTime = 0.01f; toast("Starting engine..."); }
  }
  bool wasRunning = plane.engineRunning;
  // (time acceleration is switched off before this frame's step, not after it: a 4x frame on the approach was one
  // too many)
  if (timeAccel > 1) {
    const std::string why = warpBlocker();
    if (!why.empty()) { timeAccel = 1; toast("Time warp off - " + why, vec3(1, 0.8f, 0.45f)); }
  }
  if(militaryFlight || hiveCombat.aliveCount()>0 || hiveCombat.mission.status==hive::MissionStatus::Active) timeAccel=1;
  float simDt = dt * timeAccel;
  vec3 prevPos = plane.pos;
  refreshGroundPits();   // (what the wheels, the weapons and the wreckage meet this frame)
  moveClouds(simDt);
  if (!crashed) {
    // (time warp: the frame's flight in steps of at most x4 - the rate the autopilot's and the assists' loops, the
    // weather and the failures were made at - ending early at a touchdown or an impact, which the frame then sees)
    const int chunks = std::max(1, (int)ceilf(timeAccel / 4.f - 1e-3f));
    const float h = simDt / chunks;
    for (int k = 0; k < chunks; k++) {
    updateWeather(h);
    updateFailures(h);
    updateResearchCard(h);
    { PartTimer pt(*this, "flight model"); plane.step(h, wx, gameTime + h * k); }
    if (plane.seamShift.x != 0.f || plane.seamShift.z != 0.f) { prevPos += plane.seamShift; followSeam(plane.seamShift); plane.seamShift = vec3(); }
    if (benchPin) { plane.pos = benchPinPos; plane.q = benchPinQ; plane.w = vec3(); }   // (a timing scene held in place: flying, going nowhere)
    if (plane.apWindEvent != apWindSaid) {   // (the autopilot went around for a wind that turned behind it on the final: Plane::step)
      if (plane.apWindEvent == 1) toast("Autopilot: too much tailwind to stop on this runway - going around", vec3(1, 0.75f, 0.35f));
      else if (plane.apWindEvent == 2) {
        if (!plane.apDecline.empty()) {
          g_audio.trigger(SFX_AP_DISC);
          toast("Autopilot: the wind has shifted - unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
          toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
        } else if (plane.apAirport >= 0) toast(fmt("Autopilot: the wind has shifted - now runway %02d at %s", g_world.airports[plane.apAirport].rwyNumber(plane.apRev), g_world.airports[plane.apAirport].code), vec3(0.6f, 1, 0.6f));
        plane.apWindEvent = 0;
      }
      apWindSaid = plane.apWindEvent;
    }
    flightClock += h;
    timeOfDay += h / 3600.f;
    if (plane.ev.bellyLanding && !result.bellyLanding) { result.bellyLanding = true; toast("Belly landing - hold it straight", vec3(1, 0.6f, 0.3f)); g_audio.triggerLocal(SFX_METAL, vec3(0,-plane.gearHeight(),0), .7f); }
    if (plane.ev.touchdown || plane.ev.crashed) { simDt = h * (k + 1); break; }
    }
  }
  gameTime += simDt;
  if (plane.engineRunning && !wasRunning) {
    // starter catches: puff of smoke from the exhausts
    vec3 ex = plane.pos + plane.q.rotate(vec3(0.4f, -plane.spec->fusRad * 0.6f, -plane.spec->fusLen * 0.35f));
    for (int i = 0; i < 14; i++) spawn(ex, plane.q.rotate(vec3(0.8f + i * 0.05f, -0.5f, 2.f)) + vec3(0, 0.6f, 0), 2.5f, 0.6f, 1.6f, vec3(0.55f, 0.58f, 0.62f), 0.55f, SPR_SMOKE, 1.5f, 0.3f);
    if (!takeoffAnnounced && contract.type == CT_LESSON) toast("Engine running. Release the parking brake with " + actLabel(ACT_PARK, padPrompts(), true) + ".", vec3(0.7f, 1, 0.7f));   // ("B" by default)
    else if (!takeoffAnnounced) toast("Engine running.", vec3(0.7f, 1, 0.7f));   // (the takeoff clearance is the tower's: updateAtc)
  }
  // gear / flap motor cues
  if (prevGear > 0.99f && plane.gear < 0.99f) g_audio.triggerLocal(SFX_GEAR_CLUNK, vec3(0,-plane.gearHeight(),0), .6f);
  if (prevGear < 0.99f && plane.gear >= 0.99f) g_audio.triggerLocal(SFX_GEAR_CLUNK, vec3(0,-plane.gearHeight(),0), 1.f);
  prevGear = plane.gear; prevFlaps = plane.flaps;
  // weather effects: in a storm a stroke every 6 to 21 s, 600 m to 4 km off - down to the ground from the cloud's base
  // (two in five), crawling along under the base (one in five), or hidden in the cloud, lighting it from inside
  if (wx.storm) {
    nextLightning -= dt;
    if (nextLightning <= 0) {
      nextLightning = 6.f + (rand() % 1000) * .015f;
      const float angle = (rand() % 6283) * .001f, range = 600.f + (rand() % 3400), kind = (rand() % 1000) * .001f;
      const float base = std::max(wx.cloudBase, 300.f);
      const vec3 foot = camPos + vec3(cosf(angle) * range, 0.f, sinf(angle) * range);
      const float drift = (rand() % 1000) * 0.6f, da = (rand() % 6283) * .001f;
      vec3 top = foot + vec3(cosf(da) * drift, 0.f, sinf(da) * drift); top.y = base;
      const uint32_t seed = (uint32_t)rand() * 2654435761u + 1u;
      if (kind < 0.4f) makeStrike(top, vec3(foot.x, std::max(g_world.height(foot.x, foot.z), 0.f), foot.z), true, seed);
      else if (kind < 0.6f) {   // (a crawler: a few hundred metres to a couple of km along under the base)
        const float L = 500.f + (rand() % 1500), a2 = (rand() % 6283) * .001f;
        makeStrike(top - vec3(0, 20.f, 0), top + vec3(cosf(a2) * L, -40.f - (rand() % 80), sinf(a2) * L), false, seed);
      } else makeStrike(top + vec3(0, 300.f, 0), top + vec3(0, 300.f, 0), false, seed);   // (in the cloud: no channel seen)
      const Strike& S = strikes.back();
      g_audio.triggerAt(SFX_THUNDER, (S.top + S.foot) * 0.5f, .85f);   // the mixer delays the sound by the actual distance / speed of sound
    }
  }
  // the strokes' light: the flash over everything, as bright as the nearest stroke is close; the newest one's in its cloud
  {
    float flash = 0.f;
    for (Strike& S : strikes) {
      S.t += dt;
      const float d = length((S.top + S.foot) * 0.5f - camPos), f = S.flash(S.t);
      flash = std::max(flash, f * clampf(2400.f / std::max(d, 1.f), 0.3f, 1.2f) * (S.a.empty() ? 0.6f : 1.f));
    }
    while (!strikes.empty() && strikes.front().t > 1.4f) strikes.erase(strikes.begin());
    lightning = flash;
    lightningGlow = strikes.empty() ? 0.f : strikes.back().flash(strikes.back().t);
    if (!strikes.empty()) lightningTop = strikes.back().top;
  }
  { PartTimer pt(*this, "cloud wake"); updateCloudWake(simDt); }

  updateUfo(simDt);
  // AI traffic
  traffic.enabled = set.traffic && !militaryFlight;
  traffic.audioObserver = dbgCam ? dbgCamPos : camPos;
  if(!militaryFlight) airlineTraffic(simDt);
  if (traffic.update(simDt, plane.pos, plane.vel, plane.onGround || crashed, plane.spec->span) && !crashed && !plane.ev.crashed) {
    plane.ev.crashed = true; plane.ev.crashReason = "Mid-air collision";
  }
  for (auto& f : traffic.puffs) spawn(f.p, f.v, f.life, f.size, f.grow, f.col, f.alpha, f.kind, 1.f, 0.f);
  for (auto& b : traffic.booms) { g_audio.triggerAt(SFX_BOOM, b.first, b.second); shake(0.3f * clampf(b.second, 0.f, 1.f)); }
  { PartTimer pt(*this, "XR-40 systems"); updateSomnus(simDt); }
  updateHive(simDt);
  if(screen!=SCR_FLIGHT) return;
  // Continuous, individually tracked traffic engines provide the pass and its real Doppler shift.
  for (auto& m : traffic.radio) toast(m, vec3(1.f, 0.78f, 0.3f));
  // entertainment: O + P held for a second while flying summons the Nocturne display pair (again: sends them home).
  // They fly their show around you and bow out when you land or settle onto an approach
  {
    bool flying = !crashed && !plane.onGround;
    if (flying && in.down['O'] && in.down['P']) {
      escortSummon += dt;
      if (escortSummon >= 1.f && !escortLatch) {
        escortLatch = true;
        if (traffic.escortActive()) traffic.dismissEscort();
        else if (traffic.escortStop) toast("Nocturne display pair: climb away from the field first", vec3(1.f, 0.78f, 0.3f));
        else { traffic.spawnEscort(plane.pos, plane.vel); g_audio.trigger(SFX_CHIME, 0.8f); }
      }
    } else { escortSummon = 0; escortLatch = false; }
    float dn; int na = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dn);
    (void)na;
    bool onApproach = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV && plane.apStage != Plane::APS_GOAROUND;
    traffic.escortStop = crashed || plane.onGround || onApproach || (plane.agl() < 150.f && dn < 4000.f);
    // the formation run (C12): the pair joins once the player is up; the run clocks the time with them alongside and
    // the time the player was not a steady platform (steep bank, hard g, a quick roll)
    if (contract.type == CT_TRIAL && contract.id == trialId(TR_FORMATION) && !formDone) {
      if (flying && !traffic.escortActive() && !traffic.escortStop && plane.agl() > 300.f && formT <= 0.f) { traffic.spawnEscort(plane.pos, plane.vel); g_audio.trigger(SFX_CHIME, 0.8f); }
      if (traffic.escortActive() && traffic.escAct != Traffic::ESC_JOIN) {
        formT += simDt;
        bool steady = fabsf(plane.bankDeg()) < 35.f && fabsf(plane.gLoad - 1.f) < 0.5f && fabsf(plane.w.z) < 0.7f && !plane.onGround;
        if (!steady) formLost += simDt;
        if (formT >= kFormationRunS) {
          formDone = true; traffic.dismissEscort();
          toast(fmt("Formation run complete - %.0f s out of a steady platform. Land back at %s.", formLost, g_world.airports[contract.to].code), vec3(0.5f, 1.f, 0.6f)); g_audio.trigger(SFX_SUCCESS);
        }
      }
    }
  }
  {
    // g-force lens (g_lens.glsl): a clear view up to 80% of the airframe's limit load either way; from there a dark red
    // iris closes over it, most of the way by the limit itself and the rest as the airframe takes the overstress that breaks
    // it (Plane::step: overG fails it at 1, twice the limit at once), so it is fully over the view as the airframe lets go
    const float frac = plane.gLoad >= 0.f ? plane.gLoad / plane.spec->gLimitPos() : plane.gLoad / plane.spec->gLimitNeg();
    const float stress = clampf(std::max(plane.overG, frac - 1.f), 0.f, 1.f);
    // (not at the XR-50's ground station: no one there feels its loads - its telemetry strip shows them)
    const bool station = camMode == 1 && plane.spec->special == 3;
    const float target = crashed || station ? 0.f : 0.85f * clampf((frac - 0.8f) / 0.2f, 0.f, 1.f) + 0.15f * stress;
    gTunnel = gTunnelPin >= 0.f ? gTunnelPin : approach(gTunnel, target, target > gTunnel ? 4.f : crashed ? 1.2f : 2.f, dt);
  }
  if (crashed) {
    crashTimer += dt;
    // skip the crash sequence: A on the gamepad (or Enter / Space) goes straight to the results
    if (screen == SCR_FLIGHT && crashTimer > 0.8f && ((in.buttonsPressed & PAD_A) || in.pressed[K_ENTER] || in.pressed[' '])) {
      in.buttonsPressed &= ~PAD_A; in.pressed[K_ENTER] = in.pressed[' '] = false;   // don't let the same press click the results screen
      in.mPressed[0] = false; in.mDown[0] = false; padHoldA = false;
      endFlight(false, plane.ev.crashReason);
      return;
    }
    { PartTimer pt(*this, "wreck"); updateWreck(dt); }
    camYaw += dt * 0.12f;   // slow orbit around the crash site
    if (crashTimer > crashEndT && screen == SCR_FLIGHT) endFlight(false, plane.ev.crashReason);
    return;
  }
  if (plane.ev.crashed) {
    vec3 impactVel = plane.vel;
    crashed = true;
    toast(plane.ev.crashReason, vec3(1, 0.4f, 0.3f));
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.5f;
    bool air = plane.ev.crashReason.find("Structural") != std::string::npos && plane.agl() > 4.f;   // overstressed in flight: it comes apart in the air
    g_audio.triggerAt(water && !air ? SFX_SPLASH : air ? SFX_METAL : SFX_IMPACT, plane.pos, 1.f);
    if (!water && plane.fuel > 1.f) g_audio.triggerAt(SFX_CRASH, plane.pos, clampf(.35f+sqrtf(plane.fuel)*.035f,.35f,1.f));
    { PartTimer pt(*this, "break-up"); breakUp(impactVel, water, air); }
    plane.vel = vec3(); plane.w = vec3();
    if (camMode == 1 || camMode == 3) camMode = 2;
    camZoom = std::max(camZoom, air ? 2.6f : 1.8f); camPitch = 0.3f;
    return;
  }
  // takeoff
  if (!takeoffAnnounced && !plane.onGround && plane.agl() > 8.f) {
    takeoffAnnounced = true;
    toast("Positive climb!", vec3(0.7f, 1, 0.7f));
  }
  // the arrival at the destination, for the debrief's coaching: speed and height over the threshold of the runway
  // end being flown towards, where it touched down, and any go-around from a low approach
  if (!researchFlight) {
    const Airport& A = g_world.airports[contract.to];
    bool rev = dot(plane.vel, A.dir()) < 0.f;
    vec3 ld = rev ? -A.dir() : A.dir(), rel = plane.pos - A.threshold(rev); rel.y = 0;
    float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (!plane.onGround && lat < A.width * 2.f + 30.f && thrPrevAlong < 0.f && along >= 0.f && along < 300.f) {
      result.thrKt = plane.ias * MS_TO_KT; result.thrAglM = plane.pos.y - A.elev - plane.gearHeight();
    }
    thrPrevAlong = lat < 600.f ? along : -1e9f;
    // a go-around: down low on an approach (having been up and away first, and descending: not the climb-out from the
    // same field) and then climbing back above 150 m without touching down
    if (!plane.onGround && plane.agl() > 150.f) appHigh = true;
    if (appHigh && !plane.onGround && dA < 3000.f && plane.agl() < 60.f && plane.vel.y < 0.f) appLow = true;
    if (appLow && !plane.onGround && plane.agl() > 150.f) { appLow = false; result.goArounds++; result.thrKt = -1; }
    if (plane.ev.touchdown && takeoffAnnounced && lat < A.width && along > -50.f && along < A.length) { result.tdPastThrM = std::max(0.f, along); result.rwyLenM = A.length; result.centerlineErr = lat; }
    if (plane.ev.touchdown && takeoffAnnounced && atcF.goAround && !atcF.goAroundUnable) result.landedAgainstGoAround = true;
    if (plane.onGround) { appLow = false; appHigh = false; if (result.tdPastThrM >= 0 && length(plane.vel) > 1.f) result.stopLeftM = A.length - along; }   // (while still rolling: its direction says which end)
  }
  // an amphibian's gear advisory (docs/WINGS_OVER_WATER.md): once on each approach, sinking through 150 m within 4 km
  // of a field, what the gear is set for against what the field is; re-armed on the ground or climbing back through 300 m
  if (plane.spec->floats && plane.spec->retract) {
    const float agl = plane.agl();
    if (plane.onGround || agl > 300.f) gearAdvised = false;
    else if (!gearAdvised && agl < 150.f && plane.vel.y < -0.5f) {
      const int ai = amphibianField();
      if (ai >= 0) {
        gearAdvised = true;
        const bool water = g_world.airports[ai].water(), down = plane.gear > 0.5f;
        if (water != down) toast(water ? "Gear is up for a water landing" : "Gear is down for a runway landing", vec3(0.6f, 1, 0.7f));
        else { toast(water ? "Gear is DOWN - a water landing: raise it" : "Gear is UP - a runway landing: lower it", vec3(1, 0.5f, 0.3f)); g_audio.trigger(SFX_BEEP); }
      }
    }
  }
  // touchdown
  if (plane.ev.touchdown && takeoffAnnounced) {
    float fpm = -plane.ev.touchdownVs * 196.85f;
    touchdownFpm = fpm; touchedDown = true;
    tdRunway = g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);   // (the arrival's evidence: the last touchdown's runway, or -1 off every runway)
    const GearStations audioGear = gearStations(*plane.spec);
    const AudioSurface contactSurface=tdRunway>=0?AudioSurface(g_world.airports[tdRunway].surface):AudioSurface::Grass;
    // A belly arrival has no tyres to spin up; its metal contact is emitted above. Floats meet the water with a splash
    if (plane.onWater) {
      g_audio.triggerAt(SFX_SPLASH, plane.pos, clampf(fpm / 500.f, .25f, .9f));
      const FloatStations fs = floatStations(*plane.spec);   // (a sheet of spray off each float where its keel met the water)
      const float k = clampf(fpm / 300.f, 0.5f, 2.f);
      for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < (int)(18 * k); i++) {
          const vec3 wp = plane.pos + plane.q.rotate(vec3(side * (fs.x + fs.beam * 0.5f), fs.keelY, fs.stepZ + (rand() % 100) * 0.02f - 1.f));
          spawn(vec3(wp.x, 0.05f, wp.z), plane.vel * 0.45f + plane.right() * (side * (1.f + (rand() % 100) * 0.03f) * k) + vec3(0, (1.5f + (rand() % 100) * 0.02f) * k, 0),
                0.9f + (rand() % 100) * 0.006f, 0.3f, 2.2f, vec3(0.9f, 0.92f, 0.95f), 0.4f, SPR_SMOKE, 1.4f, -7.f);
        }
    }
    else if(!result.bellyLanding) for (float side : {-1.f,1.f}) g_audio.touchdown(
      plane.pos+plane.q.rotate(vec3(side*audioGear.track,-plane.gearHeight(),audioGear.mainZ)),
      clampf(fpm/600.f,.15f,.8f),length(vec3(plane.vel.x,0,plane.vel.z)),contactSurface);
    // (the rating on the touchdown scale the settlement pays by: career.h touchdownGrade - spoken, and stamped on the HUD)
    callout(fmt("%s  %.0f fpm", touchdownGradeCall(touchdownGrade(fpm)), fpm));
    tdStampT = realTime; tdStampFpm = fpm; arrivalTold = false;
    profileTouchdown(fpm, tdRunway);   // (airport stamps, butter, the type's softest: game_profile.cpp)
    shake(clampf(fabsf(fpm) / 750.f, 0.12f, 0.8f));
    float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
    for (int side = -1; side <= 1 && !plane.onWater; side += 2) {
      const GearStations gst = gearStations(*plane.spec);   // (at the main wheels: their smoke; on the water, none)
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ));
      int n = (int)clampf(gs * 0.3f, 4, 18);
      for (int i = 0; i < n; i++) spawn(wp, plane.vel * 0.3f + vec3((rand() % 100 - 50) * 0.02f, 0.5f, (rand() % 100 - 50) * 0.02f), 2.5f + (rand() % 100) * 0.01f, 0.6f, 1.8f, vec3(0.75f, 0.75f, 0.76f), 0.45f, SPR_SMOKE, 2.f, 0.2f);
    }
  }
  if (!plane.onGround) result.maxBank = std::max(result.maxBank, fabsf(plane.bankDeg()));
  profileTick(dt);   // (landmarks found, sightings, the distance flown)
  updateSightseeing();
  if (contract.type == CT_TRIAL && trialT0 >= 0.f && trialT1 < 0.f)   // (the ghost's track: where this run was, every kGhostDt s from the first gate)
    while ((float)ghostRec.size() * kGhostDt <= flightClock - trialT0 && ghostRec.size() < 100000) ghostRec.push_back(plane.pos);
  // rolling dust / spray
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  updateJobMeters(simDt, gs);   // (simulated time: at 4x the patient rides four seconds of turbulence a frame)
  if (plane.onGround && plane.groundRough > 0.3f && gs > 4.f) {
    dustAccum += dt * gs * 0.6f;
    int rw = g_world.onRunway(plane.pos.x, plane.pos.z, 3);
    int surf = rw >= 0 ? g_world.airports[rw].surface : SURF_GRASS;
    vec3 col = surf == SURF_SAND ? vec3(0.8f, 0.72f, 0.55f) : surf == SURF_SNOW ? vec3(0.95f, 0.96f, 1.f) : surf == SURF_GRAVEL ? vec3(0.55f, 0.5f, 0.45f) : vec3(0.45f, 0.42f, 0.32f);
    while (dustAccum > 1) {
      dustAccum -= 1;
      int side = rand() & 1 ? 1 : -1;
      const GearStations gst = gearStations(*plane.spec);   // (behind the main wheels)
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ + 0.5f));
      spawn(wp, plane.vel * 0.15f + vec3(0, 0.8f, 0), 2.5f, 0.8f, 2.2f, col, surf == SURF_GRASS ? 0.15f : 0.35f, SPR_SMOKE, 1.5f, 0.1f);
    }
  }
  // a wet runway in the rain: spray thrown up behind the main wheels at speed, a mist trailing off them
  if (plane.onGround && wx.precip == 1 && rainNow > 0.1f && plane.groundRough <= 0.3f && gs > 12.f) {
    sprayAccum += dt * gs * 0.45f * std::min(rainNow * 1.5f, 1.f);
    const GearStations gst = gearStations(*plane.spec);
    while (sprayAccum > 1) {
      sprayAccum -= 1;
      const int side = rand() & 1 ? 1 : -1;
      const vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ + 0.6f));
      spawn(wp, plane.vel * 0.55f + plane.right() * (side * (1.f + (rand() % 100) * 0.03f)) + vec3(0, 1.f + (rand() % 100) * 0.02f, 0),
            0.9f + (rand() % 100) * 0.008f, 0.4f, 2.6f, vec3(0.86f, 0.88f, 0.92f), 0.32f, SPR_SMOKE, 2.6f, -0.4f);
    }
  } else sprayAccum = 0.f;
  updateWaterEffects(simDt, gs);
  // prop wash dust on rough ground
  if (plane.onGround && plane.groundRough > 0.3f && plane.engineSpool > 0.5f && gs < 15.f && (rand() % 3 == 0)) {
    vec3 bp = plane.pos + plane.q.rotate(vec3((rand() % 100 - 50) * 0.04f, -plane.gearHeight(), 3.f));
    spawn(bp, plane.q.rotate(vec3(0, 0.3f, 8.f)), 1.5f, 0.6f, 2.f, vec3(0.6f, 0.55f, 0.45f), 0.18f, SPR_SMOKE, 2.f, 0.f);
  }
  // wingtip vapour in humid air under g: a continuous ribbon from each tip. Points are dropped at the tips and stay
  // in the air mass (drifting with the wind); the strip between them widens and thins out as it ages. Each is caught
  // by its tip's vortex as it is dropped and winds round its core as the pair sinks (aero_wake.h), so the ribbon
  // curls inboard and down behind the wing - faster the harder the wing is pulling.
  for (int sd = 0; sd < 2; sd++) {
    auto& tt = tipTrail[sd];
    for (auto& q : tt) {
      q.age += simDt; q.p += plane.windVel * simDt;
      if (q.caught) { q.sw.c += plane.windVel * simDt; q.p = aeroSwirlStep(q.sw, q.p, simDt); }
    }
    while (!tt.empty() && tt.front().age > 1.8f) tt.erase(tt.begin());
  }
  float vapK = plane.onGround ? 0.f : clampf((plane.gLoad - 1.7f) / 1.5f, 0.f, 1.f) * ((wx.precip > 0 || plane.pos.y > wx.cloudBase - 300.f) ? 1.f : 0.f);
  if (!plane.onGround && plane.wxl.cloud > 0.03f) vapK = std::max(vapK, 0.35f * smoothstepf(0.03f, 0.3f, plane.wxl.cloud) * smoothstepf(20.f, 50.f, plane.airspeed));   // (in cloud the tips' vortices stir it at any g)
  if (vapK > 0.f || tipOn) {
    if (vapK > 0.f && !tipOn) tipSeg++;
    vec3 tip = plane.spec->special == 3 ? morpheusWingTip(plane.sweep) : plane.spec->special == 2 ? kSomnusWingTip : plane.spec->special ? kJetWingTip : modelWingTip(kModels[typeOf(plane.spec)]);
    for (int sd = 0; sd < 2; sd++) {
      float sx = sd ? 1.f : -1.f;
      TipPt t{plane.pos + plane.q.rotate(vec3(sx * tip.x, tip.y, tip.z + 0.3f)), 0.f, vapK, tipSeg};
      t.caught = aeroWakeCatch(plane.wake, plane.pos, plane.q, t.p, t.sw);
      tipTrail[sd].push_back(t);
      if (tipTrail[sd].size() > 400) tipTrail[sd].erase(tipTrail[sd].begin());
    }
    tipOn = vapK > 0.f;
  }
  if (plane.spec->special) { PartTimer pt(*this, "jet effects"); jetEffects(simDt); }
  // GPS breadcrumb trail
  trailT += dt;
  if (trailT > 2.f && !plane.onGround) { trailT = 0; trail.push_back(vec2(plane.pos.x, plane.pos.z)); if (trail.size() > 500) trail.erase(trail.begin()); }
  // waypoints
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    vec3 wp(w.x, w.alt, w.z);
    vec3 gc, gx, gy;
    if (ringGeom(wpIndex, gc, gx, gy)) {
      // energy sparks drifting around the active gate's rim
      sparkAccum += dt * 70.f;
      Rng& r = sparkRng;
      while (sparkAccum >= 1.f) {
        sparkAccum -= 1.f;
        float a = r.range(0, 6.2832f), rr = r.range(0.8f, 0.92f);
        vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
        vec3 tang = gx * (-sinf(a) / 90.f) + gy * (cosf(a) / 90.f);
        vec3 nrm = normalize(cross(gx, gy));
        const size_t n0 = particles.size();
        spawn(gc + radial * (rr * 90.f), tang * r.range(6.f, 16.f) + radial * r.range(-8.f, 2.f) + nrm * r.range(-4.f, 4.f), r.range(0.8f, 1.6f), r.range(2.5f, 5.f), -1.5f,
              vec3(0.3f, 1.f, 0.6f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, 0.f);
        if (particles.size() > n0) particles.back().gate = true;
      }
    }
    float cpDist = length(plane.pos - wp);
    {   // the closest approach of this frame's movement to the ring centre: a fast aircraft can't skip through a ring
      vec3 d = plane.pos - prevPos; float L2 = dot(d, d);
      if (L2 > 1e-4f) { float u = clampf(dot(wp - prevPos, d) / L2, 0.f, 1.f); cpDist = std::min(cpDist, length(wp - (prevPos + d * u))); }
    }
    if (cpDist < 110.f) {
      // gate burst: shockwave + a shower of sparks flung outward
      if (ringGeom(wpIndex, gc, gx, gy)) {
        bursts.push_back({gc, gx, gy, vec3(0.3f, 1.f, 0.6f), 0.f});
        Rng& r = sparkRng;
        for (int k = 0; k < 160; k++) {
          float a = r.range(0, 6.2832f);
          vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
          vec3 nrm = normalize(cross(gx, gy));
          const size_t n0 = particles.size();
          spawn(gc + radial * 78.f, radial * r.range(30.f, 90.f) + nrm * r.range(-25.f, 25.f) + plane.vel * 0.3f, r.range(0.8f, 1.8f), r.range(3.f, 6.f), -1.5f,
                (k % 3 ? vec3(0.4f, 1.f, 0.7f) : vec3(1.f, 1.f, 0.8f)) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 1.4f, -3.f);
          if (particles.size() > n0) particles.back().gate = true;
        }
      }
      wpIndex++; result.wpDone = wpIndex; shake(0.1f);
      if (contract.type == CT_TOUR && !contract.story && wpIndex >= (int)contract.wps.size()) {   // the photo flight's shot
        photoFlashT = realTime; g_audio.trigger(SFX_CLICK, 1.f);
        toast("Click! The photographer has the shot", vec3(1.f, 0.95f, 0.8f), false);
      }
      if (contract.type == CT_TRIAL) {
        if (wpIndex == 1) trialT0 = flightClock;
        if (wpIndex >= (int)contract.wps.size()) trialT1 = flightClock;
        if (wpIndex >= 2 && trialT0 >= 0.f) {   // the split at this gate, against the best run's
          const float sp = flightClock - trialT0; trialSplits.push_back(sp);
          int kind = -1; for (int k = 0; k < TR_COUNT; k++) if (contract.id == trialId(k)) kind = k;
          auto it = kind >= 0 ? trialBestSplits.find(trialKey(kind)) : trialBestSplits.end();
          const size_t i = trialSplits.size() - 1;
          if (it != trialBestSplits.end() && i < it->second.size() && it->second[i] > 0.f) {
            const float d = sp - it->second[i];
            toast(fmt("Gate %d  %s  %+.1f s", wpIndex, trialScore(TR_GATES, sp).c_str(), d), d <= 0.f ? vec3(0.45f, 1.f, 0.6f) : vec3(1.f, 0.55f, 0.4f), false);
          } else toast(fmt("Gate %d  %s", wpIndex, trialScore(TR_GATES, sp).c_str()), vec3(0.6f, 0.9f, 1.f), false);
        }
      }
      if (contract.type == CT_TRIAL && g_music.owns()) g_music.note(wpIndex - 1); else g_audio.trigger(SFX_CHIME);   // (a gate run climbs a scale, gate by gate)
      toast(wpIndex < (int)contract.wps.size() ? fmt("Checkpoint %d of %d", wpIndex, (int)contract.wps.size()) : "All checkpoints passed!", vec3(0.5f, 1, 0.7f));
      if (contract.wps.size() && wpIndex >= (int)contract.wps.size() && contract.id == "L1") { endTimer = 2.f; }
    }
  }
  if (endTimer > 0) { endTimer -= dt; if (endTimer <= 0) { endFlight(true, ""); return; } }
  // fuel warnings
  static bool lowFuelWarned = false;
  if (flightClock < 0.1f) lowFuelWarned = false;
  // (twenty minutes left at cruise: tanks sized for real flying hold hours, and a short hop leaves with a part load)
  if (!lowFuelWarned && cruiseMinutes(*plane.spec, plane.fuel) < 20.f) { lowFuelWarned = true; g_audio.trigger(SFX_BEEP); toast("LOW FUEL", vec3(1, 0.5f, 0.2f)); }
  // completion
  if (plane.onGround && takeoffAnnounced && gs < 2.5f && !researchFlight && !militaryFlight) {
    stillTimer += dt;
    if (stillTimer > 1.2f) {
      float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
      bool atField = ap >= 0 && dA < g_world.airports[ap].length * 0.5f + 600.f;
      // an arrival is a landing on that field's runway (taxiing off to the apron afterwards is fine): the last
      // touchdown's runway, or where it stands when no touchdown was seen (a flight placed on the ground); stopping
      // near a field after landing beside or beyond its runway is an off-field landing, the load recovered by road
      const int rwy = tdRunway != -2 ? tdRunway : g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);
      // checkpoints still to fly hold back only the arrival: on a field's runway the flight takes off again to go on,
      // or the leg ends there with the parking brake set (a safe diversion - the job carries the checkpoints flown);
      // anywhere else it is a field landing like any other (the review of v3.44.0, CAR-5: the leg could not be closed)
      const bool wpsLeft = wpIndex < (int)contract.wps.size(), onField = atField && rwy == ap;
      // a job's arrival waits for the parking brake: the pilot taxis in and parks (lessons, trials and practice
      // flights end at the stop, as their briefings say; arrivalWaits)
      const bool waits = arrivalWaits() && onField && !parkingBrake;
      arrivalAt = onField ? ap : -1;
      if (onField && wpsLeft && !parkingBrake) {
        if (stillTimer >= 1.25f && stillTimer - dt < 1.25f) {   // (the first line as recorded for the voice)
          toast("Checkpoints remaining - take off again to continue", vec3(1, 0.8f, 0.4f));
          toast(expandHint(plane.onWater ? "Or moor ({parkingBrake}) to end the leg here" : "Or set the parking brake ({parkingBrake}) to end the leg here", padPrompts(), true), vec3(1, 0.8f, 0.4f), false);
        }
      } else if (waits) {
        if (!arrivalTold) {
          arrivalTold = true;
          if (arrivedAt(ap)) {
            toast(fmt("Welcome to %s", g_world.airports[ap].name), vec3(0.6f, 1, 0.7f), false);
            toast(expandHint(plane.onWater ? "Taxi to the dock, then moor ({parkingBrake}) to finish" : "Taxi clear of the runway, then set the parking brake ({parkingBrake}) to finish", padPrompts(), true), vec3(0.85f, 0.95f, 1.f), false);
            if (contract.payout > 0 && !engineShutDown) toast(expandHint("Shut down first (hold {engine}) for the airmanship bonus", padPrompts(), true), vec3(0.85f, 0.95f, 1.f), false);
          } else toast(expandHint(fmt(plane.onWater ? "Alighted at %s: moor ({parkingBrake}) to end the flight here, or take off again" : "Landed at %s: set the parking brake ({parkingBrake}) to end the flight here, or take off again", g_world.airports[ap].name), padPrompts(), true), vec3(1, 0.8f, 0.4f), false);
        }
      } else if (onField && arrivedAt(ap) && !wpsLeft) { endFlight(true, ""); return; }
      else if (onField) { result.divertedTo = ap; endFlight(false, fmt(wpsLeft ? "Leg ended at %s with checkpoints remaining" : "Diverted to %s", g_world.airports[ap].name), OUT_DIVERTED); return; }
      else if (stillTimer > 3.f) { endFlight(false, atField ? "Landed off the runway" : "Landed off-airport", OUT_OFF_AIRPORT); return; }
    }
  } else { stillTimer = 0; if (gs > 4.f) arrivalAt = -1; }
  if (militaryFlight && plane.fuel<=0 && plane.onGround && gs<1.f) { endFlight(false,"Military sortie out of fuel",OUT_OUT_OF_FUEL); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && !takeoffAnnounced) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && takeoffAnnounced) {
    // stopped dry: done, unless this is the destination with nothing left to fly (the completion check above ends
    // that one as a success). With checkpoints still to collect there's no way to take off again.
    float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
    bool wpsLeft = wpIndex < (int)contract.wps.size();
    if (wpsLeft) { endFlight(false, "Out of fuel with checkpoints remaining", OUT_OUT_OF_FUEL); return; }
    if (!(arrivedAt(ap) && dA < 2000)) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  }
  (void)prevPos;
  // tutorial hints
  updateLessonHint();
}

void Game::updateLessonHint() {
  phase = computePhase();
  if (contract.hints.size() <= (size_t)phase || contract.hints[phase].empty()) return;
  hint = expandHint(contract.hints[phase], padPrompts(), true);   // every frame: device switches and rebinding stay current
  if (phase == lastHintPhase) return;
  lastHintPhase = phase;
  const std::string key = contract.id + ".phase" + std::to_string(phase);
  if (set.showHints && std::find(hintsVoiced.begin(), hintsVoiced.end(), key) == hintsVoiced.end()) {
    hintsVoiced.push_back(key);
    commsPending.push_back({hint, contract.id, phase});   // exact lesson identity, never a fixed keyboard/controller alias
  }
}

// A start in the air (a career leg that opens airborne, an airborne research sortie): the gear up where it retracts, the
// parking brake and the wheel brakes off, so nothing holds the aircraft back or hangs out in the wind from the first frame
void Game::settleAirborneStart() {
  parkingBrake = false;
  plane.ctl.brake = 0.f; plane.brakeHold = 0;
  if (plane.spec && plane.spec->retract) { plane.ctl.gearDown = false; plane.gear = 0.f; }
}
