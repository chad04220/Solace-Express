// Somnium Volandi - the regions and the real Earth: worlds loaded, charts slid, the Earth installed and prefetched
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

void Game::initFreeRegions() {
  if (!freeRegions.empty()) return;
  freeRegions.push_back(procedural::solaceDescriptor());
  for (const auto& region : procedural::recommendations())
    if (!region.authored) freeRegions.push_back(region);
}

// The region the flight's chart is drawn about: the one Earth free flight has reached, else the World's own (Solace,
// for a career flight, a lesson, a sortie)
procedural::Descriptor Game::chartRegion() const {
  if ((freeFlight || haul.on) && earthFlight && !flightRegion.id.empty()) return flightRegion;
  if (auto region = procedural::find(g_world.regionId)) return *region;
  return procedural::solaceDescriptor();
}

// The generated regions round the flight on the horizon (Renderer::updateRegionHorizon), whatever the flight: they are
// where the Earth goes on past the islands' sea. On the real Earth, its far patches there (earth_far.h)
void Game::updateHorizon() {
  if (!g_ren.ok || screen != SCR_FLIGHT || g_world.repeating) return;
  g_ren.earthHorizon = realEarthPlay() ? earthCharts::pack() : nullptr;
  const procedural::Descriptor region = chartRegion();
  vec3 camera = dbgCam ? dbgCamPos : camPos;
  if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) camera = plane.pos;
  g_ren.updateRegionHorizon(regionTravel::Chart(region.origin).position(camera), region.worldSeed);
}

void Game::chooseFreeRegion(const procedural::Descriptor& region) {
  initFreeRegions();
  for (int i = 0; i < (int)freeRegions.size(); ++i)
    if (freeRegions[i].id == region.id) { freeRegion = i; return; }
  // A picker visit adds one descriptor, never a terrain map or an unbounded Earth catalog.
  // Retain the small launch catalog and replace the previous ad-hoc choice.
  const size_t fixed = 1 + procedural::recommendations().size();
  if (freeRegions.size() > fixed) freeRegions.resize(fixed);
  freeRegions.push_back(region); freeRegion = (int)freeRegions.size() - 1;
}

void Game::loadFreeRegion(int index, GameScreen after) {
  initFreeRegions();
  if (regionJob.valid() || regionUploading || index < 0 || index >= (int)freeRegions.size()) return;
  const procedural::Descriptor target = freeRegions[index];
  if (regionError.empty() && !quoteJob.valid() && g_world.regionId == target.id) {
    freeRegion = index; screen = after; return;
  }
  // Initial/menu loads may wait on previous speculation, but the flight thread
  // must never destroy a running async future (its destructor would join).
  retireEarthPrefetch();
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  freeRegion = index; loadingRegion = target; regionAfterLoad = after;
  regionPoseChange.reset(); earthFlight = earthCourseHold = false; earthDestination.reset();
  regionLoadTime = 0; regionShown = 0; regionLoadStage = 0; regionError.clear(); regionCancel = false;
  g_worldLoadProgress = 0.f; g_worldLoadTask = "Finishing background work";   // (the builder's own progress, from nothing: not the last region's)
  freeRegionMap = false; screen = SCR_REGION_LOADING;
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  paused = showMap = showRadio = settingsFromPause = false;
  atc.cancel(); commsPending.clear(); toasts.clear(); traffic.reset();
  hiveCombat.reset(1, combatWrap());
  g_audio.setParams(AudioParams{});
  gpsMapValid = loading.map = loading.backdrop = false;
  menuPlanCache = menuPlan::Cache{}; quoteFlown.clear();
  focusList.clear(); focusPrev.clear();
  in.pressed[K_ESC] = in.pressed[K_ENTER] = in.pressed[' '] = false;
  in.mPressed[0] = false; in.buttonsPressed = 0;
  const std::string path = diskless || cacheDir.empty() ? std::string() :
    joinPath(cacheDir, target.authored ? "world.bin" : "procedural-world.bin");
  const std::string stamp = buildStamp;
  const bool save = !diskless, makeMap = g_ren.ok;
  // A flown career quote may still be reading the original World. Moving its
  // future here drains it without freezing the loading screen or publishing a stale quote.
  regionJob = std::async(std::launch::async,
    [this, target, path, stamp, save, makeMap, retired = std::move(earthRetireJob), quote = std::move(quoteJob)]() mutable {
      if (retired.valid()) { try { retired.get(); } catch (...) {} }
      if (quote.valid()) { try { quote.get(); } catch (...) {} }
      sceneryResetWorldCaches(); // joins old-world scenery jobs before any global changes
      g_roadTraffic = RoadTraffic{};
      g_groundPits.clear();
      regionLoadStage = 1;
      // (a real-Earth chart: its fine elevation fetched first where it can be - offline, a few seconds lost, then ETOPO's)
      if (earthCharts::isEarthId(target.id)) if (auto* fetch = glo90()) { g_worldLoadTask = "Fetching the chart's fine elevation"; wantGlo90(target.origin); fetch->settle(40.0); }
      // Release the old terrain first: a second full region is never resident.
      g_world = World{};
      if (target.authored) g_world.build(path, stamp, save);
      else g_world.buildProcedural(target, path, stamp, save);
      if (g_world.airports.empty()) throw std::runtime_error("This region has no usable airports.");
      regionLoadStage = 2;
      // (its textures' and buffers' contents, the map and its levels, made here: the loading screen then only uploads
      // them, a slice a frame, as a flight's next region is published)
      return makeMap ? Renderer::prepareWorldUpload(g_world) : Renderer::WorldUploadData{};
    });
}

std::string Game::screenWorld() const { return screenWorldFor(screen); }
std::string Game::screenWorldFor(GameScreen screen) const {
  const std::string& here = g_world.regionId;
  switch (screen) {
    case SCR_HUB: if (hubTab == TAB_SETTINGS) return std::string();   // (the settings need no world: opened from the menu, none is loaded for them)
      [[fallthrough]];
    case SCR_DEBRIEF: return here == career.region ? std::string() : career.region;
    case SCR_RESEARCH: return here == "solace" ? std::string() : std::string("solace");
    case SCR_MENU: return here == "solace" || (hasSave && here == career.region) ? std::string() : std::string("solace");
    default: return std::string();
  }
}

std::string Game::careerPlaceName() const {
  if (g_world.regionId == career.region && career.location >= 0 && career.location < (int)g_world.airports.size()) return g_world.airports[career.location].name;
  if (career.atHome()) return "the Solace Islands";
  if (earthCharts::isEarthId(career.region)) if (const Airport* a = procedural::airportOf(career.region, career.location)) return a->name;
  if (auto d = procedural::find(career.region)) return "the " + d->name;
  return career.region;
}

void Game::loadCareerWorld(const std::string& id, GameScreen after) {
  if (regionJob.valid() || regionUploading || g_world.regionId == id) return;
  const auto region = procedural::find(id);
  if (!region) return;
  chooseFreeRegion(*region);
  loadFreeRegion(freeRegion, after);
}

void Game::updateRegionLoading(float dt) {
  regionLoadTime += dt;
  // Cancellation is deferred until the builder joins. No reader may resume in a partial World.
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) regionCancel = true;
  try {
    if (regionUploading) {   // the world's textures and buffers, up to 24 MB of them a frame, then published at once
      if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload(24u << 20, 8.0)) {
        if (g_ren.worldUploadError().empty()) return;
        Renderer::WorldUploadData back;   // (a staging that failed: the world loaded as before, all of it now)
        g_ren.cancelWorldUpload(&back);
        g_ren.reloadWorld(back.minimapLevels.empty() ? std::vector<uint8_t>{} : back.minimapLevels[0]);
      } else g_ren.commitWorldUpload(nullptr, false);
      regionUploading = false;
      finishRegionLoad();
      return;
    }
    if (!regionJob.valid() || regionJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto upload = regionJob.get();
    regionLoadStage = 3;
    if (g_ren.ok) {
      if (g_ren.beginWorldUpload(g_world, std::move(upload))) { regionUploading = true; return; }
      g_ren.reloadWorld(upload.minimapLevels.empty() ? std::vector<uint8_t>{} : upload.minimapLevels[0]);
    }
    finishRegionLoad();
  } catch (const std::exception& e) {
    regionUploading = false; regionError = e.what();
  } catch (...) {
    regionUploading = false; regionError = "The region could not be prepared.";
  }
}

void Game::finishRegionLoad() {
  freeAirport = freeLaunchPending ? std::clamp(freeAirport, 0, (int)g_world.airports.size() - 1) : 0;
  if (!freeEarthIdent.empty()) {   // (the real Earth: the airport chosen, its chart now loaded)
    if (g_world.earthChart) freeAirport = std::max(0, g_world.findAirport(freeEarthIdent.c_str()));
    freeEarthIdent.clear();
  }
  crashed = completed = landed = false;
  dbgCam = dbgFollow = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); wakeRings.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  g_ren.entBudgetMs = 2.5f;
  const bool launchAfterPreparation = freeLaunchPending && !regionCancel;
  freeLaunchPending = false;
  if (regionCancel && !loadingRegion.authored) {   // (a generated region given up: back to the islands)
    loadFreeRegion(0, SCR_MENU); return;
  }
  screen = regionCancel ? SCR_MENU : regionAfterLoad;
  if (screen == SCR_HUB && g_world.regionId == career.region) { career.refreshBoard(); selContract = 0; selAircraft = -1; }   // (the career's region: its board)
  if (launchAfterPreparation && screen == SCR_FREE_FLIGHT) { launchFreeFlight(); return; }
  if (screen == SCR_FREE_FLIGHT) toast(loadingRegion.name + " ready. Choose an airport and take flight.", vec3(.6f, 1.f, .8f), false);
}

void Game::openEarthPicker() {
  if (!freeFlight || crashed) return;
  initFreeRegions();
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  // Nearby choices are descriptors only; every island is generated when reached.
  freeRegions.clear(); freeRegions.push_back(procedural::solaceDescriptor(flightRegion.worldSeed));
  for (const auto& region : procedural::nearby(earthPosition, 12, flightRegion.worldSeed))
    if (!region.authored) freeRegions.push_back(region);
  chooseFreeRegion(earthDestination ? *earthDestination : flightRegion);
  freeRegionMap = true; showMap = showRadio = false; paused = true;
}

// On the real Earth a route goes to a real airport: its chart's place (the runway's centre on the lattice), the
// autopilot holding the course there as for a region
void Game::setEarthAirportRoute(const earth::AirportRecord& airport) {
  setEarthDestination(earthCharts::chartForAirport(airport));
  earthRouteIdent = airport.ident + " " + airport.name;
}

void Game::setEarthDestination(const procedural::Descriptor& region) {
  if (!freeFlight || crashed || screen != SCR_FLIGHT) return;
  earthRouteIdent.clear();
  const auto destination = region; // the picker may replace descriptor storage below
  earthDestination = destination; earthFlight = true;
  earthPrefetchFailedId.clear(); earthPrefetchError.clear(); // selecting a route explicitly retries a failed candidate
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  gpsMapValid = false;
  earthCourseHold = !plane.onGround;
  if (earthCourseHold) {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    earthCourseHold = plane.apOn;
    if (auto heading = regionTravel::Chart(flightRegion.origin).bearing(plane.pos, destination.origin)) plane.apHeading = (float)*heading;
    apDest = -1;
  }
  toast(destination.name + " route set. Nearby regions prepare in the background as you fly.", vec3(.6f, 1.f, .8f));
  if (plane.spec && geo::surfaceDistance(earthPosition, destination.origin) / 1000. >
      plane.nominalRangeKm())
    toast("Route exceeds nominal fuel range. Choose a closer stop or a longer-range aircraft.", vec3(1.f, .7f, .3f));
}

void Game::retireEarthPrefetch() {
  if (earthPrefetchCancel) earthPrefetchCancel->requested = true;
  earthPrefetchCancel.reset();
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.cancelWorldUpload(&retiredGpu);
  if (earthSceneryDraining) { g_scenery.resumeStreaming(); earthSceneryDraining = false; }
  earthPrefetchId.clear(); earthPrefetchFailedId.clear(); earthPrefetchError.clear();
  if (!earthPrefetchJob.valid() && !earthPrefetch) return;
  // A cancelled candidate may still be building. Transfer every owner into a
  // worker instead of blocking on its future or freeing large arrays here.
  earthRetireJob = std::async(std::launch::async,
    [job = std::move(earthPrefetchJob), candidate = std::move(earthPrefetch), retired = std::move(earthRetireJob), gpu = std::move(retiredGpu)]() mutable {
      if (retired.valid()) { try { retired.get(); } catch (...) {} }
      if (job.valid()) { try { auto abandoned = job.get(); } catch (...) {} }
      candidate.reset(); gpu = Renderer::WorldUploadData{};
    });
}

void Game::updateEarthSurfaceContext() {
  // every flight: the generated regions on its horizon are solid (a career flight never travels into them, but flies
  // into their ground as into any other)
  if (screen != SCR_FLIGHT || !plane.spec || g_world.repeating) {
    if (regionalSurface::flightContextEnabled()) regionalSurface::clearFlightContext();
    earthFacilitiesPending = earthPromotionDeferred = false;
    return;
  }
  const procedural::Descriptor region = chartRegion();
  // Protect the airframe plus the next maximum replayed frame of travel. Time
  // acceleration multiplies real geographic progress, including at low FPS.
  const float bodyRadius = .6f * std::max(plane.spec->span, plane.spec->fusLen) + plane.gearHeight();
  const float travel = length(vec3(plane.vel.x, 0, plane.vel.z)) * .25f * std::max(timeAccel, 1.f);
  regionalSurface::setFlightContext(region, regionTravel::Chart(region.origin).position(plane.pos),
    bodyRadius + travel, 50.f);
}

bool Game::earthPromotionSafe(const World& candidate, const regionTravel::Change& change) const {
  if (plane.onGround || crashed || !plane.spec || regionalSurface::regionPinned(candidate.regionId)) return false;
  if (g_ren.ok) {
    // Textures may be uploaded while a contact-pinned proxy remains visible.
    // The exact full-derived drawable surface must be safe before World moves.
    const auto surface = g_ren.worldUploadSurface();
    if (!surface || !regionalSurface::mayActivate(surface)) return false;
  }
  const float halfWidth = plane.spec->span * .6f, halfLength = plane.spec->fusLen * .6f;
  const float bodyRadius = std::max(halfWidth, halfLength) + plane.gearHeight();
  const float travel = length(vec3(plane.vel.x, 0, plane.vel.z)) * .25f * std::max(timeAccel, 1.f);
  const float radius = (bodyRadius + travel) * 1.02f + 2.f; // local chart scale/float slack
  const vec3 center = change.point(plane.pos);
  const float x0 = center.x - radius, x1 = center.x + radius, z0 = center.z - radius, z1 = center.z + radius;
  float incomingTop = 0.f;
  if (x0 < WORLD_HALF + 3000.f && x1 > -WORLD_HALF - 3000.f &&
      z0 < WORLD_HALF + 3000.f && z1 > -WORLD_HALF - 3000.f) {
    if (candidate.hmax[0].size() != size_t(HMAX_N) * HMAX_N) return false;
    auto cell = [](float v) { return std::clamp(int(std::floor((v + WORLD_HALF) * HMAX_N / (2.f * WORLD_HALF))), 0, HMAX_N - 1); };
    for (int z = cell(z0); z <= cell(z1); ++z) for (int x = cell(x0); x <= cell(x1); ++x)
      incomingTop = std::max(incomingTop, candidate.hmax[0][size_t(z) * HMAX_N + x]);
  }
  // hmax includes4m of crater allowance, even over perfectly flat sea. The
  // incoming chart starts without craters. Any positive terrain also reserves
  //350m for its ordinary generated building/tree skyline before clearance.
  bool incomingWater = incomingTop <= 4.f;
  if (!incomingWater) incomingTop += 350.f;
  auto obstacle = [&](float loX, float loZ, float hiX, float hiZ, float top) {
    if (hiX < x0 || loX > x1 || hiZ < z0 || loZ > z1) return;
    incomingWater = false; incomingTop = std::max(incomingTop, top);
  };
  for (const auto& box : candidate.boxes) obstacle(box.c.x-box.h.x, box.c.z-box.h.z, box.c.x+box.h.x, box.c.z+box.h.z, box.c.y+box.h.y);
  for (const auto& bridge : candidate.bridges) obstacle(bridge.minX, bridge.minZ, bridge.maxX, bridge.maxZ, bridge.maxY);
  auto placed = [&](const auto& items) { for (const auto& item : items) {
    const auto& info = kEntInfo[item.kind]; const auto& e = item.e;
    const float reach = std::hypot(info.hx * e.sx, info.hz * e.sz);
    obstacle(e.x-reach, e.z-reach, e.x+reach, e.z+reach, e.y+info.h*e.sy);
  } };
  placed(landmarks(candidate)); placed(civicLandmarks(candidate));
  const float oldTop = regionalSurface::currentUpperBound(plane.pos, plane.pos, radius);
  const bool boundClear = plane.pos.y - bodyRadius >= std::max(oldTop, incomingTop) + 250.f;
  const bool boundedWater = incomingWater && oldTop <= 0.f;
  const vec3 right = plane.right(), forward = plane.forward();
  bool allWater = true, clearOfLand = true;
  for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
    const vec3 contact = plane.pos + right * (x * halfWidth) + forward * (z * halfLength) - vec3(0, plane.gearHeight(), 0);
    const vec3 mapped = change.point(contact);
    // Non-global candidate Worlds explicitly bypass the regional fallback.
    const float current = g_world.height(contact.x, contact.z);
    const float incoming = candidate.height(mapped.x, mapped.z);
    allWater &= current < 0.f && incoming < 0.f;
    clearOfLand &= contact.y - std::max(current, 0.f) >= 250.f && mapped.y - std::max(incoming, 0.f) >= 250.f;
  }
  return (allWater && boundedWater) || (clearOfLand && boundClear);
}

void Game::updateEarthFlight() {
  updateEarthSurfaceContext();
  if (!(freeFlight || haul.on) || !earthFlight || crashed || screen != SCR_FLIGHT) { updateHorizon(); return; }
  const regionTravel::Chart chart(flightRegion.origin);
  earthPosition = chart.position(plane.pos);
  if (earthDestination && earthCourseHold) {
    const geo::Position goal = haul.on ? haul.place : earthDestination->origin;   // (a long haul's: its airport itself)
    if (!plane.apOn || plane.apMode != Plane::AP_HOLD) earthCourseHold = false;
    else if (geo::surfaceDistance(earthPosition, goal) < (haul.on ? 9000. : 2500.)) {
      earthCourseHold = false; plane.apDisengage();
      if (haul.on) toast(fmt("The %s ahead: land at %s %s", haul.to.name.c_str(), haul.target.code, haul.target.name), vec3(.6f, 1.f, .8f));
      else if (!earthRouteIdent.empty()) toast(earthRouteIdent + " ahead: it is on the GPS map", vec3(.6f, 1.f, .8f));
      else toast(earthDestination->id == g_world.regionId ? "Destination region reached. Choose a local airport on the GPS map." :
        "Destination region reached. Local airports remain unavailable while detailed terrain prepares.", vec3(.6f, 1.f, .8f));
    } else if (auto heading = chart.bearing(plane.pos, goal)) plane.apHeading = (float)*heading;
  }
  updateHorizon();
  if (realEarthPlay()) { slideEarthChart(chart); return; }
  auto owner = procedural::describe(earthPosition, flightRegion.worldSeed);
  earthFacilitiesPending = owner.id != g_world.regionId; earthPromotionDeferred = false;
  procedural::Descriptor wanted = owner;
  if (owner.id == flightRegion.id) {
    // Start a neighbor on the runway, not shortly before the seam. Even slow
    // aircraft then provide minutes of ocean travel for CPU/GPU preparation.
    vec3 horizontal(plane.vel.x, 0, plane.vel.z);
    if (length(horizontal) < 10.f) horizontal = plane.forward();
    auto heading = geo::initialBearing(earthPosition, chart.position(plane.pos + normalize(horizontal) * 1000.f));
    if (earthDestination && (earthCourseHold || plane.onGround)) heading = geo::initialBearing(earthPosition, earthDestination->origin);
    if (heading) for (double distance = 25000.; distance <= 700000.; distance += 25000.) {
      auto ahead = procedural::describe(geo::destination(earthPosition, *heading, distance), flightRegion.worldSeed);
      if (ahead.id != flightRegion.id) { wanted = ahead; break; }
    }
  }
  if (earthRetireJob.valid()) {
    if (earthRetireJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthRetireJob.get(); } catch (...) {}
  }
  if (earthSceneryDraining && (!earthPrefetch || owner.id != earthPrefetch->region.id)) {
    g_scenery.resumeStreaming(); earthSceneryDraining = false;
  }
  if (earthPrefetchJob.valid()) {
    // Ask obsolete generation to stop at its next safe checkpoint, but retain
    // its future until ready. Owner priority wins over a speculative heading.
    if (wanted.id != earthPrefetchId && earthPrefetchCancel) earthPrefetchCancel->requested = true;
    if (earthPrefetchJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthPrefetch = earthPrefetchJob.get(); }
    catch (const GenerationCancelled&) { earthPrefetchError.clear(); }
    catch (const std::exception& e) { earthPrefetchError = e.what(); earthPrefetchFailedId = earthPrefetchId; }
    catch (...) { earthPrefetchError = "Region preparation failed"; earthPrefetchFailedId = earthPrefetchId; }
    earthPrefetchCancel.reset();
    if (!earthPrefetchError.empty()) toast("Terrain preparation failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false);
  }
  if (earthPrefetch && earthPrefetch->region.id != wanted.id) { retireEarthPrefetch(); return; }
  if (!earthPrefetch) {
    if (wanted.id == flightRegion.id || wanted.id == earthPrefetchFailedId) return;
    earthPrefetchId = wanted.id; earthPrefetchError.clear();
    const std::string path = diskless || cacheDir.empty() ? std::string() :
      joinPath(cacheDir, wanted.authored ? "world.bin" : "procedural-world.bin");
    const std::string stamp = buildStamp;
    const bool save = !diskless, prepareGpu = g_ren.ok;
    earthPrefetchCancel = std::make_shared<GenerationCancelState>();
    earthPrefetchJob = std::async(std::launch::async, [wanted, path, stamp, save, prepareGpu, cancel = earthPrefetchCancel] {
      ScopedParallelLimit threadBudget(2);
      ScopedGenerationCancellation cancellation(cancel);
      generationCheckpoint();
      auto candidate = std::make_unique<PrefetchedRegion>();
      candidate->region = wanted; candidate->world = std::make_unique<World>();
      if (wanted.authored) candidate->world->build(path, stamp, save);
      else candidate->world->buildProcedural(wanted, path, stamp, save);
      generationCheckpoint();
      candidate->world->repeating = false;
      if (candidate->world->airports.empty()) throw std::runtime_error("Region has no usable airports");
      if (prepareGpu) candidate->upload = Renderer::prepareWorldUpload(*candidate->world);
      generationCheckpoint();
      return candidate;
    });
    return;
  }
  if (g_ren.ok) {
    if (!earthPrefetch->gpuStarted) {
      if (!g_ren.beginWorldUpload(*earthPrefetch->world, std::move(earthPrefetch->upload))) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
        toast("Terrain upload failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false); return;
      }
      earthPrefetch->gpuStarted = true;
    }
    if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload()) {
      if (!g_ren.worldUploadError().empty()) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
        toast("Terrain upload failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false);
      }
      return;
    }
  }
  if (owner.id == flightRegion.id || owner.id != earthPrefetch->region.id) return;
  const regionTravel::Change promotionChange(chart, regionTravel::Chart(earthPrefetch->region.origin), plane.pos);
  if (!earthPromotionSafe(*earthPrefetch->world, promotionChange)) {
    earthPromotionDeferred = true;
    if (earthSceneryDraining) { g_scenery.resumeStreaming(); earthSceneryDraining = false; }
    return;
  }
  // Ownership has a 250 m hysteresis to prevent float round-trip ping-pong.
  vec3 horizontal(plane.vel.x, 0, plane.vel.z);
  if (length(horizontal) > .1f && procedural::describe(chart.position(plane.pos - normalize(horizontal) * 250.f),
      flightRegion.worldSeed).id != owner.id) return;
  // Start the scenery drain only far out over ocean. This is not a visibility
  // cutoff: distant coastlines use the renderer's coarse overlap across handoff.
  // Weather changes cannot delay the collision chart into the next island.
  // If preparation is late, aircraft/physics keep going over open ocean.
  const float dx = std::max(0.f, std::abs(plane.pos.x) - 40000.f);
  const float dz = std::max(0.f, std::abs(plane.pos.z) - 40000.f);
  if (std::sqrt(dx * dx + dz * dz) < 60000.f) return;
  if (quoteJob.valid()) {   // (a background quote still reading the islands: the chart moves only once it is done)
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { earthPromotionDeferred = true; return; }
    try { quoteJob.get(); } catch (...) {}
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  if (!earthSceneryDraining) { g_scenery.beginDrain(); earthSceneryDraining = true; }
  if (!g_scenery.isDrained()) return;
  sceneryResetWorldCaches(); // already drained: no hidden worker join
  g_roadTraffic = RoadTraffic{}; g_groundPits.clear();
  loadingRegion = earthPrefetch->region; regionResumePaused = paused;
  regionPoseChange = promotionChange;
  auto retired = std::make_unique<World>(std::move(g_world));
  earthPrefetch->scenery.reset();   // (a real-Earth chart's staged scenery, were one here: its workers done with the world first)
  g_world = std::move(*earthPrefetch->world);
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.commitWorldUpload(&retiredGpu); // staged resources only; no bulk upload
  earthPrefetch.reset(); earthPrefetchId.clear();
  earthRetireJob = std::async(std::launch::async, [old = std::move(retired), gpu = std::move(retiredGpu)]() mutable { old.reset(); gpu = Renderer::WorldUploadData{}; });
  atc.cancel(); commsPending.clear(); traffic.reset(); hiveCombat.reset(1, combatWrap());
  gpsMapValid = loading.map = loading.backdrop = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); wakeRings.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  continueEarthFlight();
  g_scenery.resumeStreaming(); earthSceneryDraining = false;
}

// On the real Earth: a chart of it, or Solace in a Free Flight with the pack there (and the generated islands not chosen)
bool Game::realEarthPlay() const {
  if (!earthFlight) return false;
  if (earthCharts::isEarthId(flightRegion.id)) return true;
  if (flightRegion.id != "solace" || !earthCharts::pack()) return false;
  if (haul.on) return career.realEarth;   // (a career's long haul out of the islands: its world beyond them)
  return freeFlight && !freeGeneratedEarth;
}

// The real Earth's fine elevation (earth_fetch.h): the fetcher made when first wanted, and the tiles a chart at a place reads
earthFetch::Glo90Fetcher* Game::glo90() {
  if (!glo90Fetch && !earthCharts::glo90Cache().empty() && earthCharts::pack())
    glo90Fetch = std::make_unique<earthFetch::Glo90Fetcher>(earthCharts::glo90Cache());
  return glo90Fetch.get();
}
void Game::wantGlo90(const geo::Position& where) {
  auto pack = earthCharts::pack();
  if (auto* fetch = glo90(); fetch && pack) fetch->want(*pack, earthFetch::tilesAround(where.latitude, where.longitude, WORLD_HALF * 1.5f));
}

const std::vector<Game::BeyondAirport>& Game::beyondAirports() {
  if (earthBeyondFor == flightRegion.id) return earthBeyond;
  earthBeyondFor = flightRegion.id; earthBeyond.clear();
  const auto pack = earthCharts::pack();
  if (!pack || !earthCharts::isEarthId(flightRegion.id)) return earthBeyond;
  const regionTravel::Chart chart(flightRegion.origin);
  const double lat = flightRegion.origin.latitude, lon = flightRegion.origin.longitude, reach = 120000.0;
  const double dLat = reach / 111000.0 + 0.1, dLon = dLat / std::max(std::cos(lat * PI / 180.0), 0.2);
  for (const earth::AirportRecord& a : pack->airports({lat - dLat, lon - dLon, lat + dLat, lon + dLon})) {
    if (a.runways.empty()) continue;
    const vec3 p = chart.local({a.lat, a.lon, 0.0});
    // (one in the square is g_world's, or one the chart's selection left out: never there to land on)
    if (std::max(fabsf(p.x), fabsf(p.z)) < WORLD_HALF || hypotf(p.x, p.z) > reach) continue;
    const earth::Runway& r = a.runways[0];
    vec3 along;
    if (r.endsKnown) along = chart.local({r.lat2, r.lon2, 0.0}) - chart.local({r.lat1, r.lon1, 0.0});
    else along = chart.local(geo::destination({a.lat, a.lon, 0.0}, r.hasHeading ? r.heading : 0.0, 1000.0)) - p;
    along.y = 0;
    BeyondAirport b;
    b.pos = vec2(p.x, p.z);
    const float span = length(along);
    b.dir = span > 1.f ? vec2(along.x / span, along.z / span) : vec2(0.f, 1.f);
    b.length = std::max(r.length, 300.f); b.size = a.type; b.ident = a.ident;
    earthBeyond.push_back(std::move(b));
  }
  return earthBeyond;
}

// The real Earth's pack installed from the game (earth_install.h): its zips from the source beside the game names (a
// release of the game's own otherwise), downloaded and unpacked in the user's folder, there beside where they go -
// the core alone, the land streamed as it is flown, unless whole (every zip: to fly with no connection)
bool Game::startEarthInstall(bool whole) {
  if (diskless || saveDir.empty() || (earthInstaller && earthInstaller->state() == earthInstall::Installer::RUNNING) || earthReopenJob.valid()) return false;
#ifdef _WIN32
  const std::string sep = "\\";
#else
  const std::string sep = "/";
#endif
  earthInstaller.reset();
  earthInstaller = std::make_unique<earthInstall::Installer>(earthInstall::sourceFor(assetDir), saveDir + sep + "earth-download", saveDir + sep + "earth", whole);
  return true;
}
void Game::updateEarthInstall() {
  if (!earthInstaller || earthInstaller->state() != earthInstall::Installer::DONE) return;
  // the new pack opened on a worker (its index, its tables and its cache folder read there), the installer's line still
  // up meanwhile; the user's earth folder is among those the game looks in
  if (!earthReopenJob.valid()) { earthReopenJob = std::async(std::launch::async, [] { earthCharts::reopen(); }); return; }
  if (earthReopenJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  try { earthReopenJob.get(); } catch (...) {}
  earthInstaller.reset();
  if (auto pack = earthCharts::pack())
    toast(pack->streamed() ? "The real Earth is ready: its land comes as you fly. Fly it from Free Flight, or out beyond the islands" :
                             "The real Earth is installed: fly it from Free Flight, or out beyond the islands", vec3(.6f, 1.f, .8f), false);
  else toast("The real Earth was installed but doesn't open: " + earthCharts::packError(), vec3(1.f, .5f, .35f), false);
}

// The real Earth (earth_charts.h, docs/REAL_EARTH_PLAN.md section 5): one chart after another under the flight, its
// edge never in reach. Past 5 km from its chart's centre the next is prepared ahead along the track - a minute's flying
// on, 20 to 60 km, on the lattice; one already preparing, or ready, kept while it is within 8 km of that - and the flight
// moves into it once nearer its centre than this one's by 2 km: over land or sea, at any height, its height over the
// ground kept when it is low (the two charts' grounds a few metres apart). Never on the ground
void Game::slideEarthChart(const regionTravel::Chart& chart) {
  earthFacilitiesPending = earthPromotionDeferred = false;
  vec3 horizontal(plane.vel.x, 0, plane.vel.z);
  const float speed = length(horizontal);
  if (speed < 10.f) { horizontal = plane.forward(); horizontal.y = 0; }
  if (length(horizontal) < 1e-3f) horizontal = vec3(0, 0, -1);
  horizontal = normalize(horizontal);
  const float out = hypotf(plane.pos.x, plane.pos.z);
  std::string wanted = flightRegion.id;   // (ids alone each frame: a chart's name is looked up once, when it starts)
  {   // (and every two seconds the fine elevation the next chart will read, fetched ahead of it)
    const vec3 ahead = plane.pos + horizontal * std::clamp(speed * 60.f, 20000.f, 60000.f);
    glo90WantT -= 1.f / 60.f;
    if (glo90WantT <= 0.f) { glo90WantT = 2.f; wantGlo90(chart.position(ahead)); }
  }
  if (out > 5000.f && !plane.onGround && !crashed) {
    const vec3 aim = plane.pos + horizontal * std::clamp(speed * 60.f, 20000.f, 60000.f);
    auto near = [&](const geo::Position& origin) { const vec3 c = chart.local(origin); return hypotf(c.x - aim.x, c.z - aim.z) < 8000.f; };
    // (a route's destination chart once the aim is within 40 km of it: the flight arrives in the chart about its airport)
    const bool toDestination = earthDestination && earthCharts::isEarthId(earthDestination->id) && [&] {
      const vec3 c = chart.local(earthDestination->origin); return hypotf(c.x - aim.x, c.z - aim.z) < 40000.f; }();
    if (toDestination) wanted = earthDestination->id;
    else if (earthPrefetch && near(earthPrefetch->region.origin)) wanted = earthPrefetch->region.id;
    else if (earthPrefetchJob.valid() && near(earthPrefetchOrigin)) wanted = earthPrefetchId;
    else { const geo::Position g = chart.position(aim); wanted = earthCharts::slideIdAt(g.latitude, g.longitude); }
  }
  if (earthRetireJob.valid()) {
    if (earthRetireJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthRetireJob.get(); } catch (...) {}
  }
  if (earthPrefetchJob.valid()) {
    if (wanted != earthPrefetchId && earthPrefetchCancel) earthPrefetchCancel->requested = true;
    if (earthPrefetchJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthPrefetch = earthPrefetchJob.get(); }
    catch (const GenerationCancelled&) { earthPrefetchError.clear(); }
    catch (const std::exception& e) { earthPrefetchError = e.what(); earthPrefetchFailedId = earthPrefetchId; }
    catch (...) { earthPrefetchError = "Chart preparation failed"; earthPrefetchFailedId = earthPrefetchId; }
    earthPrefetchCancel.reset();
    if (!earthPrefetchError.empty()) toast("The terrain ahead couldn't be prepared: " + earthPrefetchError, vec3(1.f, .65f, .3f), false);
  }
  if (earthPrefetch && earthPrefetch->region.id != wanted) { retireEarthPrefetch(); return; }
  if (!earthPrefetch) {
    if (wanted == flightRegion.id || wanted == earthPrefetchFailedId) return;
    const auto next = wanted == "solace" ? std::optional<procedural::Descriptor>(procedural::solaceDescriptor(flightRegion.worldSeed)) :
                                           earthCharts::find(wanted);
    if (!next) return;
    earthPrefetchId = wanted; earthPrefetchOrigin = next->origin; earthPrefetchError.clear();
    const bool prepareGpu = g_ren.ok;
    earthPrefetchCancel = std::make_shared<GenerationCancelState>();
    // (built on two threads, never written to the cache: a flight lays down a chart every few minutes. The islands are
    // read from theirs, as the launch reads them)
    const std::string solacePath = diskless || cacheDir.empty() ? std::string() : joinPath(cacheDir, "world.bin");
    earthPrefetchJob = std::async(std::launch::async, [wanted = *next, prepareGpu, cancel = earthPrefetchCancel, solacePath,
                                                       stamp = buildStamp, save = !diskless] {
      ScopedParallelLimit threadBudget(2);
      ScopedGenerationCancellation cancellation(cancel);
      generationCheckpoint();
      auto candidate = std::make_unique<PrefetchedRegion>();
      candidate->region = wanted; candidate->world = std::make_unique<World>();
      if (wanted.authored) candidate->world->build(solacePath, stamp, save);
      else candidate->world->buildProcedural(wanted, std::string(), std::string(), false);
      generationCheckpoint();
      candidate->world->repeating = false;
      if (prepareGpu) candidate->upload = Renderer::prepareWorldUpload(*candidate->world);
      generationCheckpoint();
      return candidate;
    });
    return;
  }
  if (g_ren.ok) {
    if (!earthPrefetch->gpuStarted) {
      if (!g_ren.beginWorldUpload(*earthPrefetch->world, std::move(earthPrefetch->upload))) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error; return;
      }
      earthPrefetch->gpuStarted = true;
    }
    if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload()) {
      if (!g_ren.worldUploadError().empty()) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
      }
      return;
    }
  }
  // the move: nearer the new chart's centre than this one's by 2 km, and in the air - out of the islands over open water
  // (their chart holds them, its neighbours only the sea round them)
  const vec3 centre = chart.local(earthPrefetch->region.origin);
  auto moves = [&](vec3 p) {
    return hypotf(p.x - centre.x, p.z - centre.z) <= hypotf(p.x, p.z) - 2000.f &&
           (!flightRegion.authored || std::max(fabsf(p.x), fabsf(p.z)) >= WORLD_HALF - 10000.f);
  };
  // the new chart's scenery staged first, on workers of its own (Scenery::stage), about where the move will put the
  // aircraft: where the track first meets it, ten minutes on at most, else where the aircraft is. The move waits for what
  // stands within 1.5 km of it, so the land comes across with its buildings and trees rather than emptied and filling in
  // round the aircraft - unless the flight is 20 km out already, when it moves regardless
  int unstaged = 0;
  {
    auto& staged = earthPrefetch->scenery;
    if (!staged) staged = std::make_unique<Scenery>(2, earthPrefetch->world.get());
    if (!earthSceneryDraining) staged->resumeStreaming();   // (a move begun and put off: streaming again)
    vec3 at = plane.pos;
    const vec3 track(plane.vel.x, 0.f, plane.vel.z);
    for (int t = 0; t <= 600; t += 2) if (moves(plane.pos + track * (float)t)) { at = plane.pos + track * (float)t; break; }
    const EntRanges R = entRangesFor(g_ren.quality);
    const float farAll = std::max(R.big, R.build) + 300.f, farDetail = std::max(std::max(R.tree, R.rock), R.bush) + 300.f;
    unstaged = staged->stage(regionTravel::Chart(earthPrefetch->region.origin).local(chart.position(at)), farDetail, farAll, 1500.f);
  }
  if (!moves(plane.pos)) return;
  if (unstaged > 0 && out < 20000.f) return;
  if (plane.onGround || crashed || !plane.spec) { earthPromotionDeferred = true; return; }
  if (quoteJob.valid()) {
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { earthPromotionDeferred = true; return; }
    try { quoteJob.get(); } catch (...) {}
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  if (!earthSceneryDraining) { g_scenery.beginDrain(); earthPrefetch->scenery->beginDrain(); earthSceneryDraining = true; }
  if (!g_scenery.isDrained() || !earthPrefetch->scenery->isDrained()) return;
  const float above = plane.pos.y - g_world.height(plane.pos.x, plane.pos.z);
  const std::string wasNamed = flightRegion.name;
  sceneryResetWorldCaches();
  g_roadTraffic = RoadTraffic{}; g_groundPits.clear();
  loadingRegion = earthPrefetch->region; regionResumePaused = paused;
  regionPoseChange = regionTravel::Change(chart, regionTravel::Chart(earthPrefetch->region.origin), plane.pos);
  earthCarried.clear();
  for (const Airport& a : g_world.airports) earthCarried.push_back({a.code ? a.code : "", a.elev});
  auto retired = std::make_unique<World>(std::move(g_world));
  g_world = std::move(*earthPrefetch->world);
  g_scenery.adopt(*earthPrefetch->scenery);   // (its chunks, made from the world now g_world)
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.commitWorldUpload(&retiredGpu);
  earthPrefetch.reset(); earthPrefetchId.clear(); earthPrefetchFailedId.clear();
  earthRetireJob = std::async(std::launch::async, [old = std::move(retired), gpu = std::move(retiredGpu)]() mutable { old.reset(); gpu = Renderer::WorldUploadData{}; });
  atc.cancel(); commsPending.clear(); hiveCombat.reset(1, combatWrap());
  traffic.carry(*regionPoseChange, [&](int old, float* lift) { return carriedAirport(old, lift); });   // (where they were)
  gpsMapValid = loading.map = loading.backdrop = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); wakeRings.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  continueEarthFlight();
  earthCarried.clear();
  if (above < 30.f) {   // (low: its height over this chart's ground what it was over the last's)
    const float lift = g_world.height(plane.pos.x, plane.pos.z) + above - plane.pos.y;
    plane.pos.y += lift; plane.wxAir.y += lift; camPos.y += lift; dbgCamPos.y += lift; dbgCamLook.y += lift;
  }
  if (flightRegion.name == wasNamed) while (!toasts.empty() && toasts.back().text.rfind("Entering ", 0) == 0) toasts.pop_back();
  g_scenery.resumeStreaming(); earthSceneryDraining = false;
}

void Game::continueEarthFlight() {
  g_audio.resetWorld(); audioCameraValid = false;
  if (!regionPoseChange) throw std::runtime_error("Missing Earth-flight frame transition.");
  const auto& change = *regionPoseChange;
  // The aircraft is never reset/refuelled: body-space dynamics, engine state,
  // controls, failures, fuel and clocks survive the chart handoff unchanged.
  const vec3 oldPosition = plane.pos;
  const vec3 airRelative = oldPosition - plane.wxAir;
  const vec3 cloudRelative = oldPosition + vec3(cloudOff.x, 0, cloudOff.y);
  plane.pos = change.point(plane.pos); plane.vel = change.vector(plane.vel); plane.q = change.attitude(plane.q);
  plane.windVel = change.vector(plane.windVel); plane.windAvg = change.vector(plane.windAvg);
  plane.wxAir = plane.pos - change.vector(airRelative); plane.wxl.draft = change.vector(plane.wxl.draft);
  plane.apVelPrev = change.vector(plane.apVelPrev); plane.apHeading = change.heading(plane.apHeading);
  plane.apStuntHdg = change.heading(plane.apStuntHdg); plane.apStuntRight0 = change.vector(plane.apStuntRight0);
  plane.apLd = change.vector(plane.apLd); plane.apTd = change.point(plane.apTd); plane.apHoldC = change.point(plane.apHoldC);
  plane.seamShift = vec3();
  // (an approach to a field the new chart holds too - the next real-Earth chart's, found by its ident - flown on, its
  // touchdown and its heights on that chart's ground; any other holds)
  float apLift = 0.f;
  const int apTo = plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) ? carriedAirport(plane.apAirport, &apLift) : -1;
  if (apTo >= 0) { plane.apTd.y += apLift; plane.apHoldAlt += apLift; plane.apIntAlt += apLift; }
  else if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR)) {
    plane.apMode = Plane::AP_HOLD; plane.apHeading = plane.heading(); plane.apAlt = plane.pos.y;
    plane.apStatus = "HOLD / NEW REGION";
  }
  plane.apAirport = apTo; plane.apHoldFor = carriedAirport(plane.apHoldFor); plane.apStuntWasAirport = carriedAirport(plane.apStuntWasAirport);
  if ((plane.apStuntWasMode == Plane::AP_NAV || plane.apStuntWasMode == Plane::AP_APPR) && plane.apStuntWasAirport < 0) plane.apStuntWasMode = Plane::AP_HOLD;
  // (the tower's exchange carried on with the same field, when the new chart holds it - and, on a long haul, it is the
  // one the flight is to land at)
  const AtcFlight tower = atcF;
  auto carryTower = [&](int arrival) {
    const int arr = carriedAirport(tower.arr), dep = carriedAirport(tower.dep);
    if (arr < 0 || (arrival >= 0 && arr != arrival)) return;
    atcF = tower; atcF.arr = arr; atcF.dep = dep >= 0 ? dep : arr; atcF.lastApt = carriedAirport(tower.lastApt);
    atcF.holdPos = change.point(tower.holdPos);
    if (dep < 0 && atcF.phase < 3) atcF.phase = 3;
  };
  camPos = change.point(camPos); camQ = change.attitude(camQ); camVel = change.vector(camVel);
  dbgCamPos = change.point(dbgCamPos); dbgCamLook = change.point(dbgCamLook);
  // Chase/cockpit yaw is body-relative; only the orbit camera stores world yaw.
  if (camMode == 2) camYaw -= (float)change.headingDelta() * DEG;
  wx.windFrom = change.heading(wx.windFrom); wxStart.windFrom = change.heading(wxStart.windFrom);
  contract.wx.windFrom = change.heading(contract.wx.windFrom); contract.wxEnd.windFrom = change.heading(contract.wxEnd.windFrom);
  const vec3 drift = change.vector(cloudRelative) - plane.pos; cloudOff = vec2(drift.x, drift.z);
  cloudDet = change.vector(cloudDet); wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet;
  flightRegion = loadingRegion; earthFlight = true;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  if (haul.on) {   // a long haul: on toward its destination, or there - the job's own airport the flight lands at
    const int near = std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z));
    // (there: the destination's own chart, or on the real Earth any chart that holds its airport - in the square, not
    // one beyond it)
    int at = flightRegion.id == haul.job.toRegion && haul.job.to < (int)g_world.airports.size() ? haul.job.to : -1;
    if (at < 0 && earthCharts::isEarthId(haul.job.toRegion) && haul.target.code) {
      const int i = g_world.findAirport(haul.target.code);
      if (i >= 0 && std::max(fabsf(g_world.airports[i].x), fabsf(g_world.airports[i].z)) < WORLD_HALF) at = i;
    }
    haul.arrived = at >= 0;
    contract.from = near; contract.to = haul.arrived ? at : near;
    chartHaul();
    attemptFrom = near; wpIndex = 0; apDest = carriedAirport(apDest); tdRunway = -2;
    atcF = AtcFlight{}; atcF.dep = near; atcF.arr = contract.to; atcF.phase = plane.onGround ? 0 : 3;
    carryTower(contract.to);
    somnus = SomnusState{}; ufo = Ufo{}; ufo.next = 180.f;
    tipOn = false; trailT = 0; wakeGap = true; wakeOdo = 0;
    craterR = craterD = 0; boomT = -1;
    regionPoseChange.reset();
    paused = regionResumePaused; screen = SCR_FLIGHT;
    earthFacilitiesPending = earthPromotionDeferred = false; updateEarthSurfaceContext();
    if (haul.arrived) toast(fmt("Over %s: land at %s %s", earthCharts::regionPhrase(flightRegion).c_str(), dest().code, dest().name), vec3(.6f, 1.f, .8f));
    else toast(fmt("Over %s: on to %s", earthCharts::regionPhrase(flightRegion).c_str(), earthCharts::regionPhrase(haul.to).c_str()), vec3(.6f, 1.f, .8f));
    return;
  }
  freeAirport = std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z));
  contract.from = contract.to = freeAirport; contract.wps.clear();
  contract.title = "Free Flight / " + flightRegion.name;
  launchPlan.startAirport = attemptFrom = freeAirport; wpIndex = 0; apDest = carriedAirport(apDest); tdRunway = -2;
  atcF = AtcFlight{}; atcF.dep = atcF.arr = freeAirport; atcF.phase = plane.onGround ? 0 : 3;
  carryTower(-1);
  somnus = SomnusState{}; ufo = Ufo{}; ufo.next = 180.f;
  tipOn = false; trailT = 0; wakeGap = true; wakeOdo = 0;
  craterR = craterD = 0; boomT = -1;
  regionPoseChange.reset();
  freeFlight = isolatedFlight = true; paused = regionResumePaused; screen = SCR_FLIGHT;
  earthFacilitiesPending = earthPromotionDeferred = false; updateEarthSurfaceContext();
  toast("Entering " + flightRegion.name + ".", vec3(.6f, 1.f, .8f));
}

int Game::carriedAirport(int old, float* lift) const {
  if (lift) *lift = 0.f;
  if (old < 0 || old >= (int)earthCarried.size() || earthCarried[old].code.empty()) return -1;
  const int i = g_world.findAirport(earthCarried[old].code.c_str());
  if (i >= 0 && lift) *lift = g_world.airports[i].elev - earthCarried[old].elev;
  return i;
}

int Game::atcStation(int airport) const {
  if (g_world.earthChart && airport >= 0 && airport < (int)g_world.airports.size() && g_world.airports[airport].code) {
    uint32_t h = 2166136261u;   // (the real Earth's: by its ident, the same voice in every chart that holds it)
    for (const char* c = g_world.airports[airport].code; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
    return (int)(h % AtcVoice::kVoices);
  }
  return (airport * 7 + 2) % AtcVoice::kVoices;
}
