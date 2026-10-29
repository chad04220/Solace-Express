// Somnium Volandi - the career's flow: commits, launches, job legs, Free Flight, trials, research launches
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

// The flown time for a job's plan when it's in; else (start) a background flight is begun for it if none is running
void Game::applyQuote(const Contract& c, Career::LaunchPlan& e, bool start) {
  if (quoteJob.valid() && quoteJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) quoteFlown[quoteJobKey] = quoteJob.get();
  if (c.longHaul()) return;   // (across the ocean, beyond this world: the quick estimate is the quote)
  // An ID alone is not a route: FREE, a continued job, or a changed forecast
  // can keep its ID while its estimated time and fuel change.
  // (your own as fitted flies its own quote: its floats, its kits - fitted copies never move, so the worker can hold it)
  const AircraftSpec* airframe = &career.specFor(e.spec, e.src);
  std::string key = menuPlan::quoteInputs(c, e.spec);
  if (!airframe->fit.stock()) key += fmt("|fit %d %u", airframe->fit.floats ? 1 : 0, airframe->fit.kits);
  auto it = quoteFlown.find(key);
  if (it != quoteFlown.end()) { career.useFlownTime(e, c, it->second.first, it->second.second); return; }
  if (!start || quoteJob.valid() || c.forceAircraft >= 0) return;   // (lessons are flown by hand: the quick estimate stands)
  Plane::perf(airframe);   // (learned here first: the worker then only reads it)
  quoteJobKey = key;
  Contract cc = c; int spec = e.spec;
  quoteJob = std::async(std::launch::async, [cc, spec, airframe] { float fuel = -1; float m = simulateFlightMinutes(cc, spec, &fuel, false, airframe); return std::make_pair(m, fuel); });
}

void Game::saveGame() {
  if (diskless) return;
  if (career.save(joinPath(saveDir, "career.sav"))) hasSave = true;
  else toast("Couldn't save the career (disk full or folder not writable)", vec3(1.f, 0.4f, 0.3f));
}
bool Game::commit(const std::function<void(Career&)>& change) {
  Career cand = pendingCareer ? *pendingCareer : career;
  Career::boardMs = 0;
  { PartTimer pt(*this, "career: the change"); change(cand); }
  if (Career::boardMs >= 0.5) updParts.parts.push_back({"career: job board drawn", Career::boardMs});
  if ((headless && saveDir.empty()) || diskless) { career = cand; pendingCareer.reset(); return true; }   // (tests, tools: no disk)
  bool saved = false;
  { PartTimer pt(*this, "career: saved"); saved = cand.save(joinPath(saveDir, "career.sav")); }
  if (saved) { career = cand; pendingCareer.reset(); hasSave = true; saveWhy.clear(); return true; }
  pendingCareer = cand; retryT = 0;
  saveWhy = "Couldn't save the career (disk full or folder not writable)";
  toast(saveWhy + " - will retry", vec3(1.f, 0.4f, 0.3f));
  return false;
}
bool Game::retryCommit() {
  if (!pendingCareer) return true;
  if (diskless) { career = *pendingCareer; pendingCareer.reset(); return true; }
  if (!pendingCareer->save(joinPath(saveDir, "career.sav"))) return false;
  career = *pendingCareer; pendingCareer.reset(); hasSave = true; saveWhy.clear();
  toast("Career saved", vec3(0.6f, 1.f, 0.7f));
  return true;
}
// A career flight: the save marks the attempt open first (a crash to desktop mid-flight then shows on the next start
// that a flight was interrupted), then the flight starts
// The fuel at take-off: what was chosen on the card, else the plan's - the trip and its reserve (plannedFuelKg; never
// less than an owned aircraft already holds, never more than the tanks). A rental comes full, as far as the weight
// limit lets it with the load on board.
float Game::chosenFuel(const Contract& c, int spec, Career::Source src, const Career::LaunchPlan& p) const {
  const AircraftSpec& s = airframeFor(spec, src);
  if (c.rationKm > 0) return fuelForKm(s, c.rationKm);   // (a fuel challenge's ration: no choice of more)
  if (launchFuelKg > 0) return std::min(launchFuelKg, s.maxFuel);
  if (p.fuelLoadKg >= 0) return clampf(p.fuelLoadKg, 0.f, s.maxFuel);
  float have = 0.f;
  if (src == Career::SRC_OWNED) { int oi = career.ownedIndexFor(spec); if (oi >= 0) have = career.fleet[oi].fuel; }
  if (src != Career::SRC_OWNED) return std::max(s.maxFuelForPayload(c.cargoKg + c.pax * 85.f + 85.f), std::min(plannedFuelKg(s, p.fuelKgEst), s.maxFuel));
  return std::min(std::max(plannedFuelKg(s, p.fuelKgEst), have), s.maxFuel);
}
// A launch's save is its precondition: when it fails, nothing flies and the launch is not left pending to be saved
// later (a retried save would then record an open flight that never started); the card's button tries again
bool Game::commitLaunch(const std::function<void(Career&)>& change) {
  if (commitBlocked()) { hubMsg = "Your last result isn't saved yet: retry the save first."; hubMsgTime = 5; return false; }
  if (commit(change)) return true;
  pendingCareer.reset(); saveWhy.clear();
  hubMsg = "The career could not be saved (disk full or folder not writable): the flight was not started. Try again."; hubMsgTime = 6;
  return false;
}
// Reuse only the expensive, unchanged route calculation. The flown quote, paid
// continuation fees, selected tank and current uplift are applied to a fresh copy.
// Both the visible figures and the Fly gate use this path, including a selection
// or fuel change made later in the same UI frame.
Career::LaunchPlan Game::menuLaunchPlan(const Contract& c, int spec, Career::Source src, bool continuing, bool startQuote) {
  Career::LaunchPlan p = menuPlanCache.get(career, c, spec, src);
  if (continuing) continuationWaivers(p, c, spec, src);
  applyQuote(c, p, startQuote);
  career.planFuel(p, c, chosenFuel(c, spec, src, p));
  return p;
}
Career::LaunchPlan Game::finalizeLaunchPlan(const Contract& c, int spec, Career::Source src) {
  Career::LaunchPlan p = career.plan(c, spec, src);
  applyQuote(c, p, false);   // route estimate first: it must not replace the player's selected uplift afterward
  career.planFuel(p, c, chosenFuel(c, spec, src, p));
  return p;
}
void Game::beginCareerFlight(const Contract& c, int spec, Career::Source src) {
  // (a contract of the world loaded, in the career's own region: the campaign's are the islands' alone)
  if (g_world.regionId != career.region || (c.story && !career.atHome())) { hubMsg = "The campaign is flown in the Solace Islands: travel home to fly it."; hubMsgTime = 5; return; }
  // (the route's plan and the save, then the flight: all behind the loading card - Game::queueLaunch)
  if (!commitBlocked() && queueLaunch(c, spec, LAUNCH_CAREER, [this, c, spec, src] { beginCareerFlight(c, spec, src); })) return;
  const Career::LaunchPlan p = finalizeLaunchPlan(c, spec, src);
  if (!commitLaunch([&](Career& k) {
    k.attempt++; k.attemptOpen = true;
    // (lessons and checkrides are flown whole: no job; a free flight leaves a job waiting at its stop as it is)
    if (Career::resumable(c)) k.accept(c, spec, src, p);
    else if (!(k.job && k.job->state == Career::JobState::RECOVERY)) k.job.reset();
  })) return;
  earthFlight = earthCourseHold = false; earthDestination.reset();   // (a career flight's chart is its region's, unless it is a long haul)
  if (!c.longHaul()) { startFlight(c, spec, src, &p); return; }
  Contract local = c; local.to = c.from;   // (until the destination's region is reached: the departure stands in for it)
  startFlight(local, spec, src, &p);
  beginHaul(c);
}
// A long haul under way: its state, the destination on this chart, and the Earth route the regions ahead are prepared on
void Game::beginHaul(const Contract& c) {
  const auto to = procedural::find(c.toRegion);
  const Airport* target = procedural::airportOf(c.toRegion, c.to);
  if (!to || !target) return;
  haul = Haul{}; haul.on = true; haul.job = c; haul.to = *to; haul.target = *target;
  haul.place = airportPlace(c.toRegion, *target);
  if (auto here = procedural::find(career.region)) flightRegion = *here; else flightRegion = procedural::solaceDescriptor();
  earthFlight = true; earthDestination = *to; earthCourseHold = false;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  chartHaul();
  toast(fmt("Long haul: %.0f km to %s %s, %s", c.haulKm, target->code, target->name, earthCharts::regionPhrase(*to).c_str()), vec3(0.6f, 1.f, 0.8f), false);
  toast("The GPS shows the way: the regions ahead are made ready as you cross", vec3(0.8f, 0.9f, 1.f), false);
}
void Game::chartHaul() {
  if (!haul.on) return;
  const vec3 at = regionTravel::Chart(flightRegion.origin).local(haul.place);
  const Airport* real = procedural::airportOf(haul.job.toRegion, haul.job.to);
  if (real) haul.target = *real;
  haul.target.x = at.x; haul.target.z = at.z;
}
void Game::continueJob(int spec, Career::Source src) {
  if (!career.job || career.job->state != Career::JobState::RECOVERY) return;
  if (!commitBlocked() && queueLaunch(career.job->continuation(), spec, LAUNCH_CAREER, [this, spec, src] { continueJob(spec, src); })) return;
  Contract c = career.job->continuation();
  Career::LaunchPlan p = finalizeLaunchPlan(c, spec, src);
  continuationWaivers(p, c, spec, src);
  bool took = commitLaunch([&](Career& k) {
    Career::JobState& J = *k.job;
    if (spec != J.spec || src != J.src) { J.hirePaid = false; J.ferryPaid = false; }   // a different aircraft: a new hire, its own ferry (C3)
    J.plan = p; J.spec = spec; J.src = src; J.state = Career::JobState::ACTIVE; k.attempt++; k.attemptOpen = true;
  });
  if (!took || !career.job || career.job->state != Career::JobState::ACTIVE) return;   // the save did not take: the job stays as it was, nothing flies on a stale career
  startFlight(c, spec, src, &p);
  applyJobLeg();
}
// the committed job's leg carries on from its checkpoints, its clock and the ride so far, its paid fees waived
void Game::applyJobLeg() {
  if (!career.job || career.job->state != Career::JobState::ACTIVE) return;
  const Career::JobState& J = *career.job;
  wpIndex = std::min(J.wpDone, (int)contract.wps.size()); result.wpDone = wpIndex; jobClockBase = J.jobClockMin * 60.f;
  result.patient = J.patient; result.comfort = J.comfort;
  surveyT = J.surveySec; surveyInT = J.surveyInSec; result.surveySec = surveyT; result.surveyInSec = surveyInT;
  result.surveyHistoryKnown = J.surveyHistoryKnown;
  result.surveyInBand = surveyT > 1.f ? surveyInT / surveyT : 1.f;
  if (J.hirePaid) launchPlan.hire = 0;
  if (J.ferryPaid) launchPlan.ferry = 0;
  jobLeg = true;
}
void Game::continuationWaivers(Career::LaunchPlan& p, const Contract& c, int spec, Career::Source src) const {
  if (!career.job) return;
  const Career::JobState& J = *career.job;
  if (J.positioningPaid) p.positioning = 0;
  if (spec != J.spec || src != J.src) { p.net = c.payout - p.fees() - p.fuelCostEst; return; }   // another aircraft: its hire and its ferry are new
  if (J.hirePaid) p.hire = 0;
  if (J.ferryPaid) p.ferry = 0;
  p.net = c.payout - p.fees() - p.fuelCostEst;
}
// The pause menu's Restart: the same flight from the start, in the mode it was flown in. A practice approach or a
// trial stays off the books, a job's leg carries on from where the job waited (its checkpoints, clock and ride, its
// paid fees), a research sortie relaunches (the review of v3.31.0, C1: Restart made practice and trials career
// flights, and reset a job leg's clock and comfort)
void Game::restartFlight() {
  if (freeFlight) { launchFreeFlight(); return; }
  if (haul.on) { toast("A long haul can't be restarted in flight: end the flight to call it off", vec3(1.f, 0.75f, 0.35f)); return; }
  if (researchFlight) { launchResearch(); return; }
  if (queueLaunch(contract, specIdx, LAUNCH_CAREER, [this] { restartFlight(); })) return;
  if (militaryFlight) { Contract c=contract; auto p=launchPlan; startFlight(c,specIdx,source,&p); return; }
  const bool iso = isolatedFlight, leg = jobLeg;
  Contract c = contract;
  const Career::LaunchPlan p = launchPlan;   // retry the accepted load and price, even if a newer estimate has arrived
  startFlight(c, specIdx, source, &p);
  if (iso) isolatedFlight = true;
  if (leg) applyJobLeg();
}
// The debrief's second button: a job waiting at its stop flies on from there with its clock, ride and paid fees (as
// the hub's Continue does); anything else (a lesson, a checkride, a job that ended) is flown again from the start
void Game::retryFromDebrief() {
  if (isMilitaryContract(contract.type)) { launchMilitary(contract.type-CT_MIL_RECON); return; }
  if (contract.longHaul()) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; return; }   // (a long haul: the board offers it again, or another)
  // the aircraft must still be one the job can be flown in: if it is gone (repossessed, sold) or no longer suits, the
  // hub's chooser offers the ones that can, with the reason (the review of v3.31.0, C2: a repossessed aircraft flew)
  std::string why;
  if (career.job && career.job->state == Career::JobState::RECOVERY) {
    const auto sc = career.canFly(career.job->continuation(), specIdx, &why);
    if (sc == Career::SRC_NONE) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; hubMsg = std::string("Choose another aircraft: ") + kAircraft[specIdx].name + " - " + why; hubMsgTime = 6; return; }
    continueJob(specIdx, sc);
  } else {
    Contract c = contract; auto sc = career.canFly(c, specIdx, &why);
    if (sc == Career::SRC_NONE && source == Career::SRC_LESSON) sc = Career::SRC_LESSON;   // (the school's aircraft, as the hub gives it)
    if (sc == Career::SRC_NONE) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; hubMsg = std::string("Choose another aircraft: ") + kAircraft[specIdx].name + " - " + why; hubMsgTime = 6; return; }
    beginCareerFlight(c, specIdx, sc);
  }
  if (screen == SCR_DEBRIEF) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; }   // (the launch didn't take: its message is on the hub)
}
void Game::releaseJob() {
  if (!career.job || commitBlocked()) return;
  commit([](Career& k) { k.releaseJob(); });
  hubMsg = "Job released: the load stays where it is."; hubMsgTime = 4;
}
void Game::practiseApproach(int spec, Career::Source src) {
  if (!career.job) return;
  Contract c; c.id = "PRACTICE"; c.title = "Practice: " + career.job->c.title; c.type = CT_FERRY; c.from = career.job->at; c.to = career.job->c.to; c.wx = career.job->c.wx;
  c.brief = "An approach flown for practice: nothing here counts for the job or the career.";
  if (queueLaunch(c, spec, LAUNCH_CAREER, [this, spec, src] { practiseApproach(spec, src); })) return;
  startFlight(c, spec, src);
  isolatedFlight = true;
}

// Main-menu Free Flight never quotes, accepts, saves or settles a career job. A fresh
// zero-fee plan supplies full tanks even without a licence, money or an owned aircraft.
void Game::beginFreeFlightSetup() {
  initFreeRegions();
  if (auto region = procedural::find(g_world.regionId)) chooseFreeRegion(*region);
  freeRegionMap = false;
  freeCraft = validCareerSelection(freeCraft);
  freeAirport = g_world.airports.empty() ? 0 : std::clamp(freeAirport, 0, (int)g_world.airports.size() - 1);
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  hiveCombat.reset(1, combatWrap());
  paused = showMap = showRadio = settingsFromPause = false;
  confirmNew = confirmRes = false;
  atc.cancel(); commsPending.clear(); toasts.clear();
  screen = SCR_FREE_FLIGHT;
}

void Game::cancelFreeFlightSetup() {
  if (screen != SCR_FREE_FLIGHT) return;
  freeRegionMap = false;
  if (!screenWorldFor(SCR_MENU).empty()) { loadFreeRegion(0, SCR_MENU); return; }   // (the menu stands in the islands, or the career's region)
  screen = SCR_MENU;
  // No hub return/save path: opening or closing this mode cannot create a career save.
}

// The research craft's launch sites step through the land fields alone: a seaplane base has nowhere to stand one
int Game::stepResearchSite(int from, int step) const {
  const int n = (int)g_world.airports.size();
  int at = from;
  for (int k = 0; k < n; k++) { at = ((at + step) % n + n) % n; if (!g_world.airports[at].water()) return at; }
  return from;
}

bool Game::freeStartsAirborne() const {
  return freeAirport >= 0 && freeAirport < (int)g_world.airports.size() && g_world.airports[freeAirport].water() &&
         validAircraft(freeCraft) && !airframeFor(freeCraft, Career::SRC_NONE).floats;
}

Contract Game::freeFlightContract() const {
  Contract c;
  c.id = "FREE_FLIGHT"; c.title = g_world.procedural && freeRegion >= 0 && freeRegion < (int)freeRegions.size() ? "Free Flight / " + freeRegions[freeRegion].name : "Free Flight"; c.type = CT_FERRY;
  c.from = c.to = freeAirport; c.startAirborne = freeAirborne || freeStartsAirborne();
  c.brief = "Fly anywhere. Full tanks, no fees, and no career progress or penalties.";
  c.wx = Weather(); c.wx.timeOfDay = 12.f; c.wx.windSpeed = 3.f;
  c.wx.gust = 0; c.wx.turbulence = 0.05f; c.wx.cloudCover = 0.15f; c.wx.visibility = 60000.f;
  return c;
}

void Game::launchFreeFlight() {
  if (screen != SCR_FREE_FLIGHT && !freeFlight) return;
  if (freeFlight && !flightRegion.id.empty()) chooseFreeRegion(flightRegion);
  if (!freeRegions.empty() && (freeRegion < 0 || freeRegion >= (int)freeRegions.size() ||
      freeRegions[freeRegion].id != g_world.regionId)) return;
  // Validate at the boundary too: a stale selection or direct call must never unlock research.
  if (!isCareerAircraft(freeCraft) ||
      freeAirport < 0 || freeAirport >= (int)g_world.airports.size()) return;
  if (!freeFlight && quoteJob.valid()) {
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      // Initial launch preparation may wait; an already flying Earth session never does.
      freeLaunchPending = true; loadFreeRegion(freeRegion); return;
    }
    jobs::drain(quoteJob, "the job board's quote before Free Flight");
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  const AircraftSpec& s = airframeFor(freeCraft, Career::SRC_NONE);
  // (the flight's setup and its first scene behind the loading card: Game::queueLaunch)
  if (queueLaunch(freeFlightContract(), freeCraft, LAUNCH_FREE, [this] { launchFreeFlight(); })) return;
  Contract c = freeFlightContract();
  Career::LaunchPlan p;
  p.spec = freeCraft; p.src = Career::SRC_NONE; p.startAirport = freeAirport; p.fuelLoadKg = s.maxFuel;
  const float careerFuelChoice = launchFuelKg;
  startFlight(c, freeCraft, Career::SRC_NONE, &p);
  launchFuelKg = careerFuelChoice;
  freeFlight = isolatedFlight = true;
  flightRegion = freeRegions.empty() ? procedural::solaceDescriptor() : freeRegions[freeRegion];
  // Every Free Flight departure participates in Earth travel: fly off the islands and on into the next region.
  earthFlight = true;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  failPlan = FailPlan();
  if (c.startAirborne) {
    const Airport& a = g_world.airports[freeAirport];
    vec3 pos = plane.pos + plane.forward() * 1500.f;
    pos.y = std::max(a.elev, g_world.height(pos.x, pos.z)) + 900.f;
    plane.reset(&s, pos, plane.heading(), s.maxFuel, 85.f, true, s.cruise);
    settleAirborneStart();
    engineAutoStarted = takeoffAnnounced = true;
    camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  }
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  toasts.clear();
  toast(std::string(s.name) + " / FREE FLIGHT", vec3(0.6f, 1.f, 0.8f), false);
  toast(c.startAirborne ? "Already airborne. Fly anywhere, then return from the pause menu." :
        plane.onWater ? expandHint("Full tanks, moored at the dock. Cast off ({parkingBrake}) when ready.", padPrompts(), true) : "Full tanks. Release the parking brake when ready.", vec3(0.8f, 0.9f, 1.f));
}

void Game::returnToFreeFlight() {
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  retireEarthPrefetch();
  if (!flightRegion.id.empty()) chooseFreeRegion(flightRegion);
  earthFlight = earthCourseHold = false; earthDestination.reset(); freeRegionMap = freeEarthPicker = false; earthRouteIdent.clear();
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  hiveCombat.reset(1, combatWrap());
  paused = showMap = showRadio = settingsFromPause = false;
  dbgCam = dbgFollow = false;
  atc.cancel(); commsPending.clear(); toasts.clear();
  screen = SCR_FREE_FLIGHT;
}

// Return a service loaner without leaving weapon effects or damaged scenery behind on the civilian hub.
// Closing an unflown attempt is saved transactionally; a failed save remains pending and blocks another launch.
void Game::returnToMilitary() {
  pendingMilitaryLaunch.reset();
  militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
  if (career.military.activeAttempt && !commitBlocked())
    commit([](Career& k) { k.abandonMilitaryAttempt(); });
  militaryFlight = militarySettled = researchFlight = freeFlight = isolatedFlight = false;
  paused = showMap = showRadio = settingsFromPause = false;
  crashed = completed = landed = false; dbgCam = dbgFollow = false;
  hiveCombat.reset(1, combatWrap()); combatLoadout.reset(); militaryHull = 100;
  combatCollisionWarm = false; combatCollisionWarmMs = 0; combatPrefetchCursor = 0;
  somnus = SomnusState(); wreck.clear(); debris.clear(); pops.clear(); particles.clear(); wakeRings.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  tipOn = airBreak = false; trailT = 0; wakeGap = true; wakeOdo = 0;
  craterR = craterD = 0; boomT = -1; g_groundPits.clear(); g_scenery.craters.clear(); g_scenery.resetDamage();
  traffic.reset(); atc.cancel(); commsPending.clear(); toasts.clear();
  g_ren.entBudgetMs = 2.5f;
  source = Career::SRC_NONE; screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 2;
  in.pressed[K_ESC] = in.pressed[K_ENTER] = in.pressed[' '] = false;
  in.mPressed[0] = false; in.buttonsPressed &= ~(PAD_A | PAD_B | PAD_START);
}

// your airline's scheduled flights (C11): when a route's line passes within 15 km of you and none of its flights
// is in the air, one appears on the line a way off and flies on to the route's destination in the company livery
void Game::airlineTraffic(float dt) {
  airlineTrafficT -= dt;
  if (airlineTrafficT > 0 || researchFlight || freeFlight || career.airline.routes.empty() || !traffic.enabled || plane.onGround) return;
  airlineTrafficT = 20.f;
  for (size_t ri = 0; ri < career.airline.routes.size(); ri++) {
    const Career::Route& r = career.airline.routes[ri];
    if (r.fleetIdx < 0 || r.fleetIdx >= (int)career.fleet.size() || traffic.routeFlying((int)ri)) continue;
    vec3 a = g_world.airports[r.from].pos(), b = g_world.airports[r.to].pos(); a.y = b.y = 0;
    vec3 ab = b - a; float L = length(ab); if (L < 1.f) continue;
    vec3 p0 = vec3(plane.pos.x, 0, plane.pos.z);
    float t = clampf(dot(p0 - a, ab) / (L * L), 0.05f, 0.95f);
    vec3 q = a + ab * t; float dist = length(q - p0);
    if (dist > 15000.f || dist < 2500.f) continue;
    float alt = std::max(g_world.height(q.x, q.z), 0.f) + 900.f + 150.f * (ri % 3);
    traffic.spawnRoute(career.fleet[r.fleetIdx].spec, vec3(q.x, alt, q.z), r.to, (int)ri);
    break;
  }
}

// ------------------------------------------------------------------ trials (C12)
const float Game::kFormationRunS = 180.f;
Contract Game::trialContract(int kind) const {
  Contract c; c.type = CT_TRIAL; c.payout = 0; c.minLicense = LIC_STUDENT; c.id = trialId(kind); c.title = trialName(kind);
  int home = career.location;
  c.from = c.to = home;
  c.wx = Weather(); c.wx.windSpeed = 3.f; c.wx.cloudCover = 0.3f; c.wx.timeOfDay = 10.f;
  if (kind == TR_STOL) { int smp = g_world.findAirport("SMP"); if (smp >= 0) c.from = c.to = smp; }
  const Airport& A = g_world.airports[c.from];
  if (kind == TR_SPOT) c.brief = fmt("Take off, fly a circuit and put the wheels down as close as you can to the mark 300 m past the threshold at %s, as softly as you can. Score: metres from the mark plus a quarter of the touchdown rate in fpm. Lower is better.", A.name);
  else if (kind == TR_FORMATION) c.brief = fmt("Take off from %s and climb above 300 m: the Nocturne display pair joins you and flies its show around you for three minutes. Hold a steady platform for them - wings near level, no yanking, no diving at the ground - and land back here when they bow out. Score: the seconds you spent out of a steady platform. Lower is better.", A.name);
  else if (kind == TR_STOL) c.brief = "Summit Pass, 600 m of gravel at 5,400 ft. Take off, come round and land as short as you can: the score is the landing roll from touchdown to a stop. Lower is better. The Scrubrunner is the natural choice.";
  else {
    // the gate course: eight rings out from the runway, each bending a little, hugging the ground
    uint32_t seed = kind == TR_DAILY ? (uint32_t)(time(nullptr) / 86400) * 2654435761u : 0x5eedu + (uint32_t)home * 131u;
    Rng r(seed);
    vec3 p = A.pos() + A.dir() * 1800.f; float hdg = atan2f(A.dir().x, -A.dir().z);
    for (int k = 0; k < 8; k++) {
      hdg += (r.uni() - 0.5f) * 0.9f;
      p = p + vec3(sinf(hdg), 0, -cosf(hdg)) * 1100.f;
      float hmax = 0; for (int q = 0; q < 8; q++) { float a = q * 0.785f; hmax = std::max(hmax, g_world.height(p.x + cosf(a) * 250.f, p.z + sinf(a) * 250.f)); }
      float alt = std::max(hmax, A.elev) + 140.f + r.uni() * 80.f;
      c.wps.push_back({p.x, p.z, alt});
    }
    c.brief = kind == TR_DAILY ? fmt("Today's course: eight gates laid out from %s, new every day. The clock runs from the first gate to the last; land back here when you are through. Lower is better.", A.name)
                               : fmt("Eight gates low over the country out of %s. The clock runs from the first gate to the last; land back here when you are through. Lower is better.", A.name);
  }
  return c;
}
std::string Game::trialScore(int kind, float v) const {
  if (v < 0) return "DNF";
  if (kind == TR_SPOT) return fmt("%.0f pts", v);
  if (kind == TR_STOL) return fmt("%.0f m roll", v);
  if (kind == TR_FORMATION) return fmt("%.0f s out", v);
  return fmt("%d:%02d.%d", (int)v / 60, (int)v % 60, (int)(v * 10) % 10);
}
std::string Game::trialBoard(int kind, int spec) const {
  if (kind == TR_DAILY) {   // (the same course for everyone that day, in whatever aircraft)
    char d[16] = ""; const time_t t = time(nullptr);
    if (const tm* g = gmtime(&t)) strftime(d, sizeof d, "%Y-%m-%d", g);
    return std::string(trialName(kind)) + " - " + d;
  }
  return std::string(trialName(kind)) + " - " + kAircraft[std::clamp(spec, 0, kAircraftCount - 1)].name;
}
std::string Game::trialKey(int kind) const {
  if (kind != TR_DAILY) return trialId(kind);
  return fmt("%s.%u", trialId(kind), (unsigned)(time(nullptr) / 86400));   // (today's course: its own board)
}
void Game::trialMedalMarks(int kind, int spec, float m[3]) const {
  const AircraftSpec& s = kAircraft[std::clamp(spec, 0, kAircraftCount - 1)];
  if (kind == TR_SPOT) { m[0] = 25.f; m[1] = 50.f; m[2] = 90.f; return; }
  if (kind == TR_FORMATION) { m[0] = 3.f; m[1] = 10.f; m[2] = 25.f; return; }
  if (kind == TR_STOL) { const float r = std::max(s.runwayM, 150.f); m[0] = r * 0.5f; m[1] = r * 0.75f; m[2] = r * 1.1f; return; }
  // the gate runs: the course from the first gate to the last at the type's cruise speed
  const Contract c = trialContract(kind);
  float len = 0.f;
  for (size_t i = 1; i < c.wps.size(); i++) len += length(vec3(c.wps[i].x - c.wps[i - 1].x, c.wps[i].alt - c.wps[i - 1].alt, c.wps[i].z - c.wps[i - 1].z));
  const float t = len / std::max(s.cruise, 20.f);
  m[0] = t; m[1] = t * 1.2f; m[2] = t * 1.5f;
}
int Game::trialMedal(int kind, int spec, float score) const {
  if (score < 0.f) return 0;
  float m[3]; trialMedalMarks(kind, spec, m);
  return score <= m[0] ? 3 : score <= m[1] ? 2 : score <= m[2] ? 1 : 0;
}
static std::string ghostPath(const std::string& dir, const std::string& key) { return (dir.empty() ? std::string() : dir + "/") + "ghost_" + key + ".bin"; }
void Game::loadGhost(int kind) {
  ghostBest.clear();
  if (diskless || (headless && saveDir.empty()) || (kind != TR_GATES && kind != TR_DAILY)) return;
  FILE* f = fopen(ghostPath(saveDir, trialKey(kind)).c_str(), "rb");
  if (!f) return;
  uint32_t n = 0;
  if (fread(&n, 4, 1, f) == 1 && n > 0 && n < 200000) {
    ghostBest.resize(n);
    if (fread(ghostBest.data(), sizeof(vec3), n, f) != n) ghostBest.clear();
  }
  fclose(f);
}
void Game::saveGhost(int kind) {
  if (diskless || (headless && saveDir.empty()) || ghostRec.empty()) return;
  if (kind == TR_DAILY) {   // (yesterday's course's ghost goes with it)
    const unsigned day = (unsigned)(time(nullptr) / 86400);
    for (unsigned d = day - 7; d < day; d++) remove(ghostPath(saveDir, fmt("%s.%u", trialId(kind), d)).c_str());
  }
  FILE* f = fopen(ghostPath(saveDir, trialKey(kind)).c_str(), "wb");
  if (!f) return;
  const uint32_t n = (uint32_t)ghostRec.size();
  fwrite(&n, 4, 1, f); fwrite(ghostRec.data(), sizeof(vec3), n, f);
  fclose(f);
}
void Game::startTrial(int kind, int spec, int src) {
  if (g_world.procedural) return;   // (the trials' courses are the islands')
  startFlight(trialContract(kind), spec, (Career::Source)src); isolatedFlight = true;   // (a trial: off the books)
  trialResult = TrialResult(); trialResult.kind = -1;
  trialSplits.clear(); ghostRec.clear(); loadGhost(kind);
}
void Game::finishTrial(bool success) {
  int kind = -1; for (int k = 0; k < TR_COUNT; k++) if (contract.id == trialId(k)) kind = k;
  if (kind < 0) return;
  float score = -1;
  if (success && result.landed) {
    if (kind == TR_SPOT && result.tdPastThrM >= 0) score = fabsf(result.tdPastThrM - 300.f) + fabsf(touchdownFpm) * 0.25f;
    else if (kind == TR_STOL && result.tdPastThrM >= 0 && result.stopLeftM >= 0) score = std::max(0.f, result.rwyLenM - result.tdPastThrM - result.stopLeftM);
    else if ((kind == TR_GATES || kind == TR_DAILY) && trialT0 >= 0 && trialT1 > trialT0 && wpIndex >= (int)contract.wps.size()) score = trialT1 - trialT0;
    else if (kind == TR_FORMATION && formT >= kFormationRunS) score = formLost;
  }
  TrialResult& R = trialResult;
  R = TrialResult(); R.kind = kind; R.spec = specIdx; R.src = (int)source; R.score = score; R.splits = trialSplits;
  const std::string key = trialKey(kind);
  R.bestSplits = trialBestSplits.count(key) ? trialBestSplits[key] : std::vector<float>();
  if (score < 0) R.why = result.failReason.empty() ? (success ? "The run didn't count: land back on the runway to finish" : "Did not finish") : result.failReason;
  std::string msg = std::string(trialName(kind)) + ": " + trialScore(kind, score);
  if (score >= 0) {
    auto& L = trialBest[key];
    L.push_back(score); std::sort(L.begin(), L.end()); if (L.size() > 5) L.resize(5);
    int rank = 1; for (float v : L) { if (v < score) rank++; }
    R.rank = rank; R.best = L[0]; R.newBest = rank == 1;
    msg += rank == 1 ? "  -  a new best!" : fmt("  -  rank %d (best %s)", rank, trialScore(kind, L[0]).c_str());
    if (R.newBest) { if (!trialSplits.empty()) trialBestSplits[key] = trialSplits; saveGhost(kind); }
    // the medal, the profile's best of it for this trial, the achievements
    R.medal = trialMedal(kind, specIdx, score);
    if (R.medal > 0) {
      const std::string mk = std::string("medal.") + trialId(kind);
      if (R.medal > profile.count(mk)) profile.num[mk] = (float)R.medal;
      award("medal");
      if (R.medal == 3) award("gold");
      bool all = true; for (int k = 0; k < TR_COUNT; k++) all &= profile.count(std::string("medal.") + trialId(k)) >= 3.f;
      if (all) award("gold_all");
      saveProfile();
      const char* names[] = {"", "BRONZE", "SILVER", "GOLD"};
      msg += std::string("  -  ") + names[R.medal];
    }
    saveSettings();
    if (!botControl && !diskless) {   // the worldwide board on Steam (the times in milliseconds, the rest whole)
      R.board = trialBoard(kind, specIdx);
      const bool ms = kind == TR_GATES || kind == TR_DAILY || kind == TR_FORMATION;
      steam::submitScore(R.board, ms, (int)lroundf(ms ? score * 1000.f : score));
    }
  }
  if (g_music.owns()) g_music.sting(R.medal > 0 ? STING_MEDAL : score >= 0 ? STING_SUCCESS : STING_FAIL);
  hubMsg = msg; hubMsgTime = 8;
}
// ------------------------------------------------------------------ XR-30 research flights
Contract Game::researchContract() const {
  Contract c;
  const AircraftSpec& rs = kAircraft[resCraft];
  std::string num = rs.name; num = num.substr(0, num.find(' '));   // "XR-30"
  c.id = num; c.id.erase(std::remove(c.id.begin(), c.id.end(), '-'), c.id.end()); c.title = num + " Research Flight"; c.type = CT_FERRY;
  if (resCard >= 0 && kResCards[resCard].craft == resCraft) c.title = std::string(kResCards[resCard].id) + "  " + kResCards[resCard].title;
  c.from = c.to = resAirport; c.payout = 0;
  c.wx = Weather(); c.wx.timeOfDay = resTime; c.wx.windSpeed = 3; c.wx.turbulence = 0.05f;
  if (resWx == 0) { c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; }
  else if (resWx == 1) { c.wx.cloudCover = 0.6f; c.wx.cloudBase = 1300; }
  else { c.wx.cloudCover = 0.95f; c.wx.cloudBase = 800; c.wx.precip = 1; c.wx.storm = true; c.wx.windSpeed = 9; c.wx.gust = 5; c.wx.turbulence = 0.5f; c.wx.visibility = 9000; }
  return c;
}

void Game::launchResearch() {
  // (the flight's setup and its first scene - the craft's programs compiled, its airfield streamed - behind the
  // loading card, not behind a frozen terminal: Game::queueLaunch)
  { Contract card = researchContract(); card.startAirborne = resAirborne;
    if (queueLaunch(card, resCraft, LAUNCH_RESEARCH, [this] { launchResearch(); })) return; }
  bool wr = resCraft == kSomnus;
  const AircraftSpec& rs = kAircraft[resCraft];
  if (resCard >= 0 && kResCards[resCard].craft != resCraft) resCard = -1;
  resStep = 0; resHold = 0; resBest = 0; resCardDone = false;
  Contract c = researchContract();
  // (its own fuel, never the job card's choice: that stays for the career's next flight - the review of v3.44.0, CAR-7)
  const float careerFuelChoice = launchFuelKg;
  launchFuelKg = -1;
  Career::LaunchPlan researchPlan;
  researchPlan.spec = resCraft; researchPlan.src = Career::SRC_OWNED;
  researchPlan.startAirport = resAirport; researchPlan.fuelLoadKg = resCard >= 0 && kResCards[resCard].fuelKg > 0 ? kResCards[resCard].fuelKg : rs.maxFuel;
  startFlight(c, resCraft, Career::SRC_OWNED, &researchPlan);
  launchFuelKg = careerFuelChoice;
  researchFlight = true;
  combatLoadout.reset(practiceForward,practiceBomb);
  toasts.clear();
  { std::string nm = rs.name; for (char& ch : nm) ch = (char)toupper((unsigned char)ch); toast(nm + (resCard >= 0 ? std::string(" // TEST CARD ") + kResCards[resCard].title : std::string(" // RESEARCH FLIGHT")), wr ? vec3(0.75f, 0.45f, 1.f) : rs.special ? vec3(0.4f, 0.9f, 1) : vec3(0.35f, 0.95f, 0.8f)); }
  if (resCard >= 0) toast(fmt("STEP 1 of %d: %s", kResCards[resCard].n, kResCards[resCard].steps[0].label), vec3(0.9f, 0.9f, 0.6f));
  if (resAirborne) {
    const Airport& a = g_world.airports[resAirport];
    vec3 p = plane.pos + a.dir() * 1500.f; p.y = std::max(a.elev, g_world.height(p.x, p.z)) + 900.f;
    plane.reset(&kAircraft[resCraft], p, plane.heading(), kAircraft[resCraft].maxFuel, 85, true, 200.f);
    plane.ctl.throttle = 0.7f; takeoffAnnounced = true;
    settleAirborneStart();   // (startFlight parked it for a start on the ground)
    camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 4, 26));
  } else if (wr) toast(expandHint("{flapsDown}/{flapsUp}: tilt the four thruster pods fully down for vertical takeoff",padPrompts(),true), vec3(0.7f, 0.9f, 1));
  combatBasePayload=plane.payload; syncCombatPayload();
  prevMach = 0;
}
