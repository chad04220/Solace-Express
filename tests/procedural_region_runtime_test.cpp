// The real Free Flight mode loads one procedural World, flies it, and restores
// authored Solace before anything may interpret the career's airport indices.
#include "../src/game.h"
#include "../src/scenery.h"
#include "../src/regional_surface.h"
#include "test_world.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

struct GameTest {
  static int run() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
      ("somnium_region_runtime_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    int failed = 0;
    auto check = [&](bool pass, const std::string& label) {
      printf("Region runtime: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass;
    };
    auto bytes = [](const fs::path& file) {
      std::ifstream in(file, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(in), {});
    };
    buildTestWorld(); buildStory();
    std::vector<std::string> originalCodes;
    for (const auto& airport : g_world.airports) originalCodes.emplace_back(airport.code);
    const float originalHeight = g_world.height(1000, -3000);
    const std::string stamp = "region-runtime-v1";
    g_world.saveCache((root / "world.bin").string(), stamp);
    Game game; game.initHeadless(); game.botControl = true; game.set.traffic = false;
    game.cacheDir = root.string(); game.buildStamp = stamp; game.saveDir = root.string();
    game.career.money = 37; game.career.location = (int)originalCodes.size() - 1;
    game.career.attempt = 17; game.career.attemptOpen = true;
    game.career.fleet = {{0, game.career.location, 8.f, .45f}};
    Career::LaunchPlan plan; plan.spec = 0; plan.startAirport = game.career.location;
    Contract job; job.id = "REGION_ISOLATION"; job.title = "Existing career commitment";
    job.from = game.career.location; job.to = 0;
    game.career.accept(job, 0, Career::SRC_OWNED, plan);
    game.career.job->wpDone = 2; game.career.job->jobClockMin = 13.5f;
    const fs::path save = root / "career.sav";
    check(game.career.save(save.string()), "baseline career saved");
    const std::string before = bytes(save);
    const auto mtime = fs::last_write_time(save);
    auto unchanged = [&] {
      const fs::path snapshot = root / "snapshot.sav";
      return game.career.save(snapshot.string()) && bytes(snapshot) == before &&
        bytes(save) == before && fs::last_write_time(save) == mtime && !fs::exists(save.string() + ".bak");
    };
    Career::LaunchPlan explicitJetPlan; explicitJetPlan.fuelLoadKg = kAircraft[kResearchJet].maxFuel * .25f;
    game.launchFuelKg = -1;
    check(game.chosenFuel(job, kResearchJet, Career::SRC_RENT, explicitJetPlan) == explicitJetPlan.fuelLoadKg,
      "research jet honors the accepted initial fuel plan rather than forcing full tanks");
    game.launchFuelKg = kAircraft[kResearchJet].maxFuel * .1f;
    check(game.chosenFuel(job, kResearchJet, Career::SRC_RENT, explicitJetPlan) == game.launchFuelKg,
      "explicit selected fuel takes precedence over a research jet plan");
    game.launchFuelKg = -1;
    bool finiteResearchStarts = true;
    for (int craft : {kVigil, kReverie, kResearchJet, kSomnus}) for (bool airborne : {false, true}) {
      game.resCraft = craft; game.resAirport = 0; game.resAirborne = airborne; game.resCard = -1;
      game.launchResearch();
      finiteResearchStarts &= game.screen == SCR_FLIGHT && game.researchFlight &&
        std::isfinite(game.plane.fuel) && game.plane.fuel == kAircraft[craft].maxFuel && game.plane.fuel > 0;
    }
    check(finiteResearchStarts && unchanged(), "all research aircraft start on runway or airborne with finite full tanks and no career mutation");
    game.pendingCareer = game.career; game.pendingCareer->money += 101;
    game.retryT = 2.99f; game.saveWhy = "Existing pending transaction";
    auto drain = [&] {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
      while (game.screen == SCR_REGION_LOADING && game.regionJob.valid() && std::chrono::steady_clock::now() < deadline) {
        game.update(1.f / 60.f); game.in.endFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      check(game.screen != SCR_REGION_LOADING && game.regionError.empty(), "region loader reached ready state");
      if (game.screen == SCR_REGION_LOADING) throw std::runtime_error("region loader failed: " + game.regionError);
    };
    game.beginFreeFlightSetup();
    const auto catalog = procedural::recommendations();
    std::vector<procedural::Descriptor> generated;
    for (const auto& r : catalog) if (!r.authored) generated.push_back(r);
    if (generated.size() < 2) throw std::runtime_error("need two generated recommendations");
    const auto A = generated.front(), B = generated.back();
    game.chooseFreeRegion(A);
    game.launchFreeFlight();
    check(game.screen == SCR_FREE_FLIGHT && !game.freeFlight, "staged destination cannot fly the old resident terrain");
    // Hold an outstanding quote reader. Selecting another region must remain
    // responsive and must not reset the World until that reader has returned.
    std::promise<void> releaseQuote;
    auto released = releaseQuote.get_future();
    game.quoteJob = std::async(std::launch::async, [f = std::move(released)]() mutable {
      f.wait(); return std::make_pair(1.f, 2.f);
    });
    game.loadFreeRegion(game.freeRegion);
    game.update(.016f); game.in.endFrame();
    check(game.screen == SCR_REGION_LOADING && game.regionLoadStage.load() == 0 && g_world.regionId == "solace" && unchanged(),
      "loading gates simulation and waits for the prior quote without touching career");
    const int firstSelection = game.freeRegion;
    game.loadFreeRegion(0);
    check(game.freeRegion == firstSelection, "repeated load requests cannot replace an in-flight build");
    releaseQuote.set_value(); drain();
    check(g_world.procedural && g_world.regionId == A.id && game.screen == SCR_FREE_FLIGHT && game.freeAirport == 0 && unchanged(),
      "Earth destination publishes the generated World and resets its airport selection");
    const float aHeight = g_world.height(1200, -4000);
    const std::string aCode = g_world.airports.front().code;
    bool launches = true;
    for (int airport = 0; airport < (int)g_world.airports.size(); ++airport) for (bool airborne : {false, true}) {
      game.freeCraft = 0; game.freeAirport = airport; game.freeAirborne = airborne; game.launchFreeFlight();
      launches &= game.screen == SCR_FLIGHT && game.freeFlight && game.isolatedFlight &&
        game.contract.from == airport && game.plane.spec == &kAircraft[0] && game.plane.fuel == kAircraft[0].maxFuel;
      launches &= airborne ? game.plane.agl() >= 899.f :
        g_world.onRunway(game.plane.pos.x, game.plane.pos.z, 10.f) == airport && game.plane.onGround;
      for (int i = 0; i < 3; ++i) { game.update(1.f / 60.f); game.in.endFrame(); }
      game.restartFlight();
      launches &= game.freeFlight && game.isolatedFlight;
      game.endFlight(false, "Test return", OUT_ABANDONED);
      launches &= game.screen == SCR_FREE_FLIGHT && g_world.regionId == A.id;
    }
    check(launches && unchanged(), "every generated airport supports real runway/airborne flight, restart and return with career intact");
    game.chooseFreeRegion(B); game.loadFreeRegion(game.freeRegion); drain();
    check(g_world.regionId == B.id && unchanged(), "second Earth destination replaces the first without career mutation");
    game.chooseFreeRegion(A); game.loadFreeRegion(game.freeRegion); drain();
    check(g_world.regionId == A.id && g_world.height(1200, -4000) == aHeight && aCode == g_world.airports.front().code && unchanged(),
      "A to B to A preserves deterministic geography and airport identity");
    // Fly through a real ownership boundary. Test placement is explicit; the
    // production transition itself must neither teleport nor reset/refuel Plane.
    game.freeAirport = 0; game.freeAirborne = true; game.launchFreeFlight();
    procedural::Descriptor neighbor;
    for (const auto& r : procedural::nearby(A.origin, 12, A.worldSeed))
      if (r.id != A.id && !r.authored && std::abs(std::remainder(r.origin.longitude - A.origin.longitude, 360.)) > 1.) { neighbor = r; break; }
    if (neighbor.id.empty()) throw std::runtime_error("need a neighboring region");
    const vec3 departure = game.plane.pos;
    game.openEarthPicker(); game.chooseFreeRegion(neighbor); game.setEarthDestination(neighbor); game.freeRegionMap = false;
    game.restartFlight();
    check(game.screen == SCR_FLIGHT && game.freeFlight && game.flightRegion.id == A.id && g_world.regionId == A.id &&
      length(game.plane.pos - departure) < .001f && game.freeRegions[game.freeRegion].id == A.id && unchanged(),
      "restart with a pending Earth route uses the resident departure instead of rejecting or loading the target");
    const double bearing = *geo::initialBearing(A.origin, neighbor.origin);
    double low = 0, high = geo::surfaceDistance(A.origin, neighbor.origin);
    for (int i = 0; i < 48; ++i) {
      const double mid = (low + high) * .5;
      if (procedural::describe(geo::destination(A.origin, bearing, mid), A.worldSeed).id == A.id) low = mid;
      else high = mid;
    }
    geo::Position across = geo::destination(A.origin, bearing, high + 25000.); across.height = 1800.;
    const auto expected = procedural::describe(across, A.worldSeed);
    const regionTravel::Chart oldChart(A.origin), newChart(expected.origin);
    game.plane.pos = oldChart.local(across);
    game.plane.vel = vec3(sinf(float(bearing) * DEG) * 95.f, 3.f, -cosf(float(bearing) * DEG) * 95.f);
    game.plane.wxAir = vec3(12, 3, 4); game.plane.fuel = 17.f; game.flightClock = 1234.f;
    game.wx.visibility = 150000.f; // clear-weather visibility must not postpone collision-chart publication into land
    game.plane.fail.engineHealth[0] = .65f; game.plane.flaps = .15f;
    const vec3 oldPosition = game.plane.pos, oldVelocity = game.plane.vel, airRelative = game.plane.pos - game.plane.wxAir;
    game.setEarthDestination(neighbor);
    check(length(game.plane.pos - oldPosition) == 0 && game.earthDestination && game.earthFlight && !g_world.repeating,
      "setting a geographic route never moves the aircraft or turns the chart into a torus");
    const Plane departurePlane = game.plane;
    const vec3 departureCamera = game.camPos; const quat departureCameraQ = game.camQ;
    const regionTravel::Change expectedChange(oldChart, newChart, oldPosition);
    const float oldFuel = game.plane.fuel, oldClock = game.flightClock, oldSpool = game.plane.engineSpool;
    // Retire any eager runway speculation before installing a deliberately late
    // candidate. Waiting is test setup only; production flight never waits here.
    game.retireEarthPrefetch();
    if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    // Prepare the real payload during fixture setup. The gate below tests
    // unresolved-future ownership and retirement, not cold generation speed.
    // Keep the generated cache for the later production prefetch checks.
    auto completedCandidate = std::make_unique<Game::PrefetchedRegion>();
    completedCandidate->region = expected;
    completedCandidate->world = std::make_unique<World>();
    {
      ScopedParallelLimit threadBudget(2);
      completedCandidate->world->buildProcedural(
        expected, (root / "procedural-world.bin").string(), stamp, true);
    }
    std::promise<void> releaseCandidate;
    auto candidateGate = releaseCandidate.get_future();
    game.earthPrefetchId = expected.id;
    game.earthPrefetchCancel = std::make_shared<GenerationCancelState>();
    const auto uncooperativeCancel = game.earthPrefetchCancel;
    // Deliberately omit the cancellation scope in this gated fixture: even an
    // uncooperative stage must never make future destruction block the flight.
    game.earthPrefetchJob = std::async(std::launch::async,
      [f = std::move(candidateGate), candidate = std::move(completedCandidate)]() mutable {
        f.wait();
        return std::move(candidate);
      });
    const auto beforeLate = std::chrono::steady_clock::now();
    game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && game.earthPrefetchJob.valid() && g_world.regionId == A.id &&
      std::chrono::steady_clock::now() - beforeLate < std::chrono::milliseconds(250),
      "crossing before candidate readiness keeps the active chart and never opens a loading screen or joins the worker");
    game.update(.016f); game.in.endFrame();
    check(game.screen == SCR_FLIGHT && game.flightClock > oldClock && length(game.plane.pos - oldPosition) > 0,
      "aircraft physics and flight clock continue while the next region is still building");
    // Real Game/Plane integration at the normal 4x cruise setting. The clock
    // and geographic motion advance while an unready future remains gated;
    // this tests responsiveness, not collision readiness before the next coast.
    const Plane beforeAccelerated = game.plane;
    const int beforeAcceleratedSpec = game.specIdx;
    const float beforeAcceleratedClock = game.flightClock;
    for (int craft : {kGalleon, kVigil}) {
      const auto& spec = kAircraft[craft];
      vec3 start = oldPosition; start.y = 10000.f;
      game.specIdx = craft;
      auto resetCruise = [&] {
        game.plane.reset(&spec, start, float(bearing), spec.maxFuel * .7f, 85.f, true, spec.cruise);
        game.settleAirborneStart(); game.plane.apEngage(Plane::AP_HOLD, -1, game.wx);
        game.plane.apAlt = start.y; game.plane.apHeading = float(bearing); game.timeAccel = 4;
      };
      resetCruise(); game.earthFlight = false;
      // initHeadless deliberately skips production init's performance learning.
      // AP_HOLD first calls Plane::perf through Plane::step/apSense, so preserve
      // that cold cost in a scheduler-disabled baseline rather than attributing
      // it to a join of the (still gated) region future. Warm frames keep the
      // original 250 ms responsiveness cap; no threshold is relaxed.
      double coldBaselineMs = 0, warmBaselineMs = 0;
      bool baselineResponsive = true;
      for (int frame = 0; frame < 31; ++frame) {
        const auto tick = std::chrono::steady_clock::now();
        game.update(1.f / 60); game.in.endFrame();
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick).count();
        if (frame == 0) coldBaselineMs = elapsed;
        else { warmBaselineMs = std::max(warmBaselineMs, elapsed); baselineResponsive &= elapsed < 250.; }
        baselineResponsive &= game.screen == SCR_FLIGHT && !game.crashed && game.timeAccel == 4;
      }
      check(baselineResponsive, std::string(spec.name) + " 4x Game baseline stays responsive after separately recorded cold performance learning");
      resetCruise(); game.earthFlight = true;
      const auto geographicStart = oldChart.position(game.plane.pos);
      const float clockStart = game.flightClock;
      bool acceleratedResponsive = true; double worstUpdate = 0;
      for (int frame = 0; frame < 120; ++frame) {
        const auto tick = std::chrono::steady_clock::now();
        game.update(1.f / 60); game.in.endFrame();
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick).count();
        worstUpdate = std::max(worstUpdate, elapsed);
        acceleratedResponsive &= game.screen == SCR_FLIGHT && !game.crashed && game.timeAccel == 4 &&
          game.earthPrefetchJob.valid() && !game.earthPrefetch && !game.regionJob.valid() &&
          g_world.regionId == A.id && elapsed < 250.;
      }
      const double distance = geo::surfaceDistance(geographicStart, oldChart.position(game.plane.pos));
      check(acceleratedResponsive && std::abs(game.flightClock - clockStart - 8.f) < .05f &&
        distance > spec.cruise * 8 * .7 && distance < spec.cruise * 8 * 1.3 && unchanged(),
        std::string(spec.name) + " actual 4x cruise advances eight simulation seconds without joining the gated candidate (responsiveness only)");
      printf("Region runtime: %s 4x simulated ground speed %.1f m/s, effective geographic speed %.1f m/s, cold baseline %.3f ms, warm baseline max %.3f ms, gated update max %.3f ms\n",
        spec.name, distance / 8., distance / 2., coldBaselineMs, warmBaselineMs, worstUpdate);
    }
    game.plane = beforeAccelerated; game.specIdx = beforeAcceleratedSpec;
    game.flightClock = beforeAcceleratedClock; game.timeAccel = 1;
    // Scheduler-only stress: synthetic 1 km position steps are explicitly not
    // a claim that a trainer's real flight model can attain 1000 m/s.
    const Plane beforeFastStress = game.plane;
    const vec3 fastDirection = normalize(vec3(oldVelocity.x, 0, oldVelocity.z));
    bool fastResponsive = true;
    double worstFastMs = 0;
    for (int step = 0; step < 40; ++step) {
      const float direction = ((step / 5) % 2) ? -1.f : 1.f;
      game.plane.vel = fastDirection * (1000.f * direction);
      game.plane.pos += game.plane.vel; // explicit synthetic one-second displacement
      const auto t0 = std::chrono::steady_clock::now();
      game.updateEarthFlight();
      const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      worstFastMs = std::max(worstFastMs, elapsed);
      fastResponsive &= game.screen == SCR_FLIGHT && g_world.regionId == A.id &&
        game.earthPrefetchJob.valid() && game.earthPrefetchId == expected.id && !game.earthPrefetch &&
        !game.earthRetireJob.valid() && !game.regionJob.valid() && elapsed < 250.;
    }
    check(fastResponsive, "synthetic 1000 m/s progression and abrupt reversals retain one gated candidate without loading screens, joins or piled-up futures");
    printf("Region runtime: synthetic fast scheduler maximum %.3f ms over 40 direction-changing steps\n", worstFastMs);
    game.plane = beforeFastStress;
    // Publish the actual analytic proxy grid while the detailed World future is
    // deliberately gated. The same projected/clamped triangles drive World and
    // Plane collision; publishing here substitutes only for the headless GPU.
    procedural::Recipe proxyRecipe(expected);
    const geo::LocalFrame proxyFrame(expected.origin);
    constexpr int proxyN = 129;
    std::vector<regionalSurface::GridVertex> proxyGrid;
    proxyGrid.reserve(proxyN * proxyN);
    for (int z = 0; z < proxyN; ++z) for (int x = 0; x < proxyN; ++x) {
      const float px = -WORLD_HALF + x * (2.f * WORLD_HALF / (proxyN - 1));
      const float pz = -WORLD_HALF + z * (2.f * WORLD_HALF / (proxyN - 1));
      float base[4]; proxyRecipe.sample(px, pz, base);
      const float h = base[1] < .01f ? base[0] : base[0] + base[1] * terrainFbm(px / DETAIL_SCALE, pz / DETAIL_SCALE, 5);
      proxyGrid.push_back({proxyFrame.toLocal(newChart.position(vec3(px, std::max(h, -.5f), pz))), h});
    }
    const auto proxy = regionalSurface::makeSurface(expected, proxyN, std::move(proxyGrid), regionalSurface::Provenance::Analytic);
    game.updateEarthSurfaceContext();
    check(regionalSurface::mayActivate(proxy), "distant proxy can become drawable while the aircraft is safely over ocean");
    regionalSurface::publishDrawableSurfaces({proxy});
    const auto peak = std::max_element(proxy->grid.begin(), proxy->grid.end(),
      [](const auto& a, const auto& b) { return a.height < b.height; });
    vec3 proxyLand = regionalSurface::projectVertex(*proxy, A.origin, *peak);
    const auto solid = regionalSurface::queryCurrent(proxyLand.x, proxyLand.z);
    check(solid.covered && solid.land && solid.height > 1.f &&
      std::abs(g_world.height(proxyLand.x, proxyLand.z) - solid.height) < .01f && game.earthPrefetchJob.valid(),
      "published foreign proxy terrain is solid while its full collision World is still gated");
    // A direct real Plane descent keeps Game's flight state available for the
    // continuation checks, while exercising the same contact solver it calls.
    const Plane beforeProxyProbe = game.plane;
    game.plane.reset(&kAircraft[0], vec3(proxyLand.x, solid.height + 2.f, proxyLand.z), 0.f,
      kAircraft[0].maxFuel, 85.f, true, 0.f);
    game.plane.pos.y = solid.height + game.plane.gearHeight() + .15f;
    game.plane.vel = vec3(0, -12.f, 0); game.plane.ctl.throttle = 0; game.plane.apDisengage();
    game.updateEarthSurfaceContext();
    Plane collisionProbe = game.plane;
    for (int step = 0; step < 30 && !collisionProbe.ev.crashed; ++step) collisionProbe.step(1.f / 240, game.wx, step / 240.f);
    check(collisionProbe.ev.crashed && regionalSurface::regionPinned(expected.id) &&
      !regionalSurface::mayActivate(proxy) && game.earthPrefetchJob.valid() && g_world.regionId == A.id && unchanged(),
      "real aircraft contact hits the visible foreign proxy and pins its surface instead of descending through late detail");
    game.plane = beforeProxyProbe; game.updateEarthSurfaceContext();
    // A heading reversal during the unresolved future must also stay responsive.
    game.plane.pos = departure; game.plane.vel = -oldVelocity;
    game.earthCourseHold = false; game.earthDestination.reset();
    const auto beforeReverse = std::chrono::steady_clock::now();
    game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && game.earthPrefetchJob.valid() && g_world.regionId == A.id &&
      std::chrono::steady_clock::now() - beforeReverse < std::chrono::milliseconds(250),
      "reversing course does not destroy or join an unresolved prefetch future");
    check(uncooperativeCancel->requested, "heading reprioritization requests cooperative cancellation without waiting for the worker");
    const auto staleReleasedAt = std::chrono::steady_clock::now();
    releaseCandidate.set_value();
    const auto staleDeadline = staleReleasedAt + std::chrono::minutes(5);
    while (game.earthPrefetchJob.valid() && std::chrono::steady_clock::now() < staleDeadline) {
      game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    printf("Region runtime: stale candidate wait %.3f ms, prefetch_valid=%d retirement_valid=%d candidate_present=%d in_flight=%d resident_unchanged=%d error=\"%s\"\n",
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - staleReleasedAt).count(),
      game.earthPrefetchJob.valid(), game.earthRetireJob.valid(), game.earthPrefetch != nullptr,
      game.screen == SCR_FLIGHT, g_world.regionId == A.id, game.earthPrefetchError.c_str());
    check(!game.earthPrefetchJob.valid(), "gated obsolete candidate future is consumed before the retirement deadline");
    check(game.earthPrefetchError.empty(), "gated obsolete candidate completes without preparation errors");
    check(game.screen == SCR_FLIGHT && g_world.regionId == A.id && !game.earthPrefetch && game.earthRetireJob.valid(),
      "completed obsolete candidate is retired off-thread without changing the resident chart");
    if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    // Cancel a real in-progress terrain build through the production retirement
    // path. Its phase atomic is a diagnostic, never a read of partial World data.
    auto realCancel = std::make_shared<GenerationCancelState>();
    game.earthPrefetchCancel = realCancel; game.earthPrefetchId = expected.id;
    std::atomic<bool> observedCancellation{false};
    game.earthPrefetchJob = std::async(std::launch::async, [&, realCancel, expected] {
      ScopedParallelLimit threadBudget(2); ScopedGenerationCancellation cancellation(realCancel);
      auto candidate = std::make_unique<Game::PrefetchedRegion>();
      candidate->region = expected; candidate->world = std::make_unique<World>();
      try { candidate->world->buildProcedural(expected, "", stamp, false); }
      catch (const GenerationCancelled&) { observedCancellation = true; throw; }
      return candidate;
    });
    const auto realStart = std::chrono::steady_clock::now();
    while (realCancel->phase.load() < GenerationPhase::Terrain &&
        game.earthPrefetchJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
        std::chrono::steady_clock::now() - realStart < std::chrono::seconds(10))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto cancelAt = std::chrono::steady_clock::now();
    game.retireEarthPrefetch();
    const double requestMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cancelAt).count();
    while (game.earthRetireJob.valid() && game.earthRetireJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
        std::chrono::steady_clock::now() - cancelAt < std::chrono::seconds(5))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const double cancellationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cancelAt).count();
    check(observedCancellation && cancellationMs < 5000. && requestMs < 250. &&
      game.screen == SCR_FLIGHT && g_world.regionId == A.id && unchanged(),
      "real terrain generation cancels cooperatively and retires off-thread without altering active collision terrain or career");
    printf("Region runtime: real cancellation request %.3f ms, worker retirement %.3f ms\n", requestMs, cancellationMs);
    if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    // A cancellation result is not a route failure: consume it and launch the
    // current owner priority through the actual Game scheduler.
    game.plane = departurePlane; game.flightClock = oldClock;
    auto cancelledResult = std::make_shared<GenerationCancelState>(); cancelledResult->requested = true;
    game.earthPrefetchCancel = cancelledResult; game.earthPrefetchId = expected.id;
    game.earthPrefetchJob = std::async(std::launch::async, [cancelledResult]() -> std::unique_ptr<Game::PrefetchedRegion> {
      ScopedGenerationCancellation cancellation(cancelledResult); generationCheckpoint(); return {};
    });
    const auto consumeBy = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (game.earthPrefetchCancel == cancelledResult && std::chrono::steady_clock::now() < consumeBy) {
      game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(game.earthPrefetchCancel != cancelledResult && game.earthPrefetchJob.valid() &&
      game.earthPrefetchId == expected.id && game.earthPrefetchError.empty() && game.earthPrefetchFailedId.empty() &&
      game.screen == SCR_FLIGHT && g_world.regionId == A.id,
      "cancelled speculation silently restarts the current owner instead of failing the route");
    game.retireEarthPrefetch(); if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    game.plane = departurePlane; game.flightClock = oldClock;
    game.setEarthDestination(neighbor);
    // Keep the pilot close to visible foreign ground as the real detail job
    // finishes. Full promotion must wait instead of replacing contact terrain.
    game.plane.pos = vec3(proxyLand.x, solid.height + game.plane.gearHeight() + 10.f, proxyLand.z);
    game.plane.vel = vec3(); game.plane.onGround = false;
    game.updateEarthSurfaceContext();
    const auto detailDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (!game.earthPrefetch && std::chrono::steady_clock::now() < detailDeadline) {
      game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    game.updateEarthFlight();
    check(game.earthPrefetch && game.earthPromotionDeferred && game.earthFacilitiesPending &&
      regionalSurface::regionPinned(expected.id) && g_world.regionId == A.id && game.screen == SCR_FLIGHT && unchanged(),
      "ready detail and local airports stay pending while the aircraft is near the published collision proxy");
    game.plane.onGround = true; game.updateEarthFlight();
    check(game.earthPrefetch && game.earthPromotionDeferred && g_world.regionId == A.id,
      "landed proxy contact cannot be replaced by full terrain underneath the aircraft");
    game.plane = departurePlane; game.flightClock = oldClock; game.updateEarthSurfaceContext();
    const Plane beforeOceanGate = game.plane;
    game.plane.pos.y = 100.f; game.updateEarthSurfaceContext();
    const regionTravel::Change lowOceanChange(oldChart, newChart, game.plane.pos);
    check(game.earthPrefetch && game.earthPromotionSafe(*game.earthPrefetch->world, lowOceanChange),
      "low-altitude all-water crossing may promote safely without an arbitrary250m ocean altitude restriction");
    game.plane = beforeOceanGate; game.updateEarthSurfaceContext();
    const auto flightDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    bool stayedInFlight = true;
    while (g_world.regionId == A.id && std::chrono::steady_clock::now() < flightDeadline) {
      game.updateEarthFlight(); stayedInFlight &= game.screen == SCR_FLIGHT;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(stayedInFlight && g_world.regionId == expected.id && !game.regionJob.valid(),
      "ready candidate publishes over ocean even at 150 km visibility, without any midflight loading screen");
    const geo::Position resumed = newChart.position(game.plane.pos);
    check(game.screen == SCR_FLIGHT && game.freeFlight && game.isolatedFlight && game.earthFlight &&
      !g_world.repeating && g_world.regionId == expected.id && geo::surfaceDistance(across, resumed) < .1 &&
      game.plane.pos.y == oldPosition.y && length(game.plane.vel - expectedChange.vector(oldVelocity)) < .001f &&
      game.plane.fuel == oldFuel && game.flightClock == oldClock && game.plane.engineSpool == oldSpool &&
      game.plane.fail.engineHealth[0] == .65f && game.plane.flaps == .15f &&
      length((game.plane.pos - game.plane.wxAir) - expectedChange.vector(airRelative)) < .1f && unchanged(),
      "boundary continuation preserves Earth position, altitude, speed, fuel, engine/failure state, clock and turbulence frame");
    bool cameraOffsets = std::abs(expectedChange.headingDelta()) > 1e-6;
    for (int mode : {0, 1, 2}) {
      game.plane = departurePlane; game.camPos = departureCamera; game.camQ = departureCameraQ;
      game.camMode = mode; game.camYaw = .31f; game.lookYaw = .15f;
      game.regionPoseChange = expectedChange; game.continueEarthFlight();
      const float wantedYaw = mode == 2 ? .31f - float(expectedChange.headingDelta()) * DEG : .31f;
      cameraOffsets &= std::abs(game.camYaw - wantedYaw) < 1e-6f && game.lookYaw == .15f;
    }
    check(cameraOffsets, "chase/cockpit preserve body-relative look; orbit alone rotates by chart convergence");
    check(game.contract.from >= 0 && game.contract.to < (int)g_world.airports.size() && game.plane.apAirport == -1 &&
      g_world.wrap(70000.f) == 70000.f && g_world.height(120000.f, 0) < 0,
      "new airport indices are valid and distant open ocean does not repeat islands");
    for (int i = 0; i < 8; ++i) game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && !game.regionJob.valid(), "stationary post-transition evaluation does not ping-pong at a seam");
    // CPU-only companion to the explicitly air-traffic-off EGL diagnostic:
    // relocate near the newly resident airport, then run normal Game physics
    // and traffic spawning so publication cannot leave stale airport references.
    game.retireEarthPrefetch(); game.earthFlight = false; game.set.traffic = true;
    const Airport& residentAirport = g_world.airports.front();
    game.plane.reset(&kAircraft[kGalleon], vec3(residentAirport.x, g_world.height(residentAirport.x, residentAirport.z) + 1200.f, residentAirport.z),
      0.f, kAircraft[kGalleon].maxFuel, 85.f, true, kAircraft[kGalleon].cruise);
    game.settleAirborneStart(); game.plane.apEngage(Plane::AP_HOLD, -1, game.wx);
    game.plane.apAlt = game.plane.pos.y; game.plane.apHeading = 0.f;
    const float trafficClock = game.flightClock;
    for (int i = 0; i < 120; ++i) { game.update(1.f / 60); game.in.endFrame(); }
    bool trafficValid = game.traffic.enabled && !game.traffic.craft.empty();
    int airportTraffic = 0;
    for (const auto& craft : game.traffic.craft) {
      trafficValid &= craft.airport >= -1 && craft.airport < int(g_world.airports.size()) &&
        std::isfinite(craft.pos.x) && std::isfinite(craft.pos.y) && std::isfinite(craft.pos.z) &&
        std::isfinite(craft.vel.x) && std::isfinite(craft.vel.y) && std::isfinite(craft.vel.z);
      airportTraffic += craft.role == TrafficCraft::AIRPORT;
    }
    check(trafficValid && airportTraffic > 0 && game.screen == SCR_FLIGHT && !game.crashed &&
      game.flightClock > trafficClock && unchanged(),
      "normal air traffic spawns and simulates against the new region airports after publication without touching career");
    game.set.traffic = false;
    game.endFlight(false, "End Earth route", OUT_ABANDONED);
    game.cancelFreeFlightSetup();
    check(game.screen == SCR_REGION_LOADING, "main menu waits for authored Solace restoration"); drain();
    bool original = !g_world.procedural && !g_world.repeating && g_world.regionId == "solace" && g_world.airports.size() == originalCodes.size();
    for (size_t i = 0; original && i < originalCodes.size(); ++i) original &= originalCodes[i] == g_world.airports[i].code;
    check(original && originalHeight == g_world.height(1000, -3000) && game.screen == SCR_MENU && unchanged(),
      "Solace terrain and every legacy airport index restored before main menu");
    // Cancel while a requested region is still waiting on a reader. The builder
    // must join, then restore Solace rather than exposing half-built arrays.
    game.beginFreeFlightSetup(); game.chooseFreeRegion(A);
    std::promise<void> releaseCancel;
    auto cancelGate = releaseCancel.get_future();
    game.quoteJob = std::async(std::launch::async, [f = std::move(cancelGate)]() mutable {
      f.wait(); return std::make_pair(1.f, 2.f);
    });
    game.loadFreeRegion(game.freeRegion);
    game.in.pressed[K_ESC] = true; game.update(.016f); game.in.endFrame();
    check(game.regionCancel && game.screen == SCR_REGION_LOADING, "Escape queues a safe return while loading remains gated");
    releaseCancel.set_value(); drain();
    check(!g_world.procedural && game.screen == SCR_MENU && unchanged() && game.pendingCareer &&
      game.pendingCareer->money == game.career.money + 101 && game.saveWhy == "Existing pending transaction",
      "cancel joins loading, restores Solace and preserves the pending career transaction");
    // An Earth route from the islands while a quote still reads them: the flight goes on, and nothing moves the
    // islands from under the reader (the chart changes only past their sea, once the reader is done).
    game.beginFreeFlightSetup(); game.freeAirport = 1; game.freeAirborne = true;
    std::promise<void> releaseLaunchQuote;
    auto launchQuoteGate = releaseLaunchQuote.get_future();
    game.quoteJob = std::async(std::launch::async, [f = std::move(launchQuoteGate)]() mutable {
      f.wait(); return std::make_pair(1.f, 2.f);
    });
    game.launchFreeFlight();
    check(game.screen == SCR_REGION_LOADING && game.freeLaunchPending && !game.freeFlight,
      "initial Free Flight preparation drains a pending career quote before aircraft launch");
    releaseLaunchQuote.set_value(); drain();
    check(game.screen == SCR_FLIGHT && game.freeFlight && game.contract.from == 1 && !game.quoteJob.valid(),
      "initial reader barrier resumes the requested launch and preserves its selected airport");
    check(game.earthFlight && !game.earthDestination, "ordinary Solace Free Flight enables Earth travel without selecting a route");
    const vec3 solaceDeparture = game.plane.pos;
    std::promise<void> releaseTopology;
    auto topologyGate = releaseTopology.get_future();
    game.quoteJob = std::async(std::launch::async, [f = std::move(topologyGate)]() mutable {
      f.wait(); return std::make_pair(1.f, 2.f);
    });
    game.setEarthDestination(A);
    game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && !g_world.repeating && !g_world.procedural && game.quoteJob.valid(),
      "an Earth route from Solace keeps flying while a background quote still reads the islands");
    game.focusLost();
    check(game.paused, "focus loss while a region prepares pauses the active flight normally");
    releaseTopology.set_value();
    const auto topologyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (game.quoteJob.valid() && game.quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < topologyDeadline) {
      game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && game.paused && !g_world.procedural && !g_world.repeating &&
      length(game.plane.pos - solaceDeparture) < .01f && unchanged(),
      "Solace's Earth route goes on without moving the plane or resuming after focus loss");
    game.endFlight(false, "End Solace Earth route", OUT_ABANDONED); game.cancelFreeFlightSetup(); drain();
    check(game.screen == SCR_MENU && !g_world.repeating && !g_world.procedural && g_world.regionId == "solace" && unchanged(),
      "leaving a Solace Earth flight goes straight back to the menu over the same islands");
    game.beginFreeFlightSetup(); game.freeAirborne = true; game.launchFreeFlight();
    game.updateEarthFlight();
    check(game.screen == SCR_FLIGHT && game.earthFlight && !g_world.repeating && !game.earthDestination && unchanged(),
      "ordinary Solace relaunch activates nonrepeating Earth travel without a route picker");
    game.returnToFreeFlight();
    check(game.screen == SCR_FREE_FLIGHT && !game.freeFlight && unchanged(), "ordinary Solace Earth flight returns safely to setup");
    game.cancelFreeFlightSetup(); drain();
    check(game.screen == SCR_MENU && !g_world.repeating && !g_world.procedural && !regionalSurface::flightContextEnabled() && unchanged(),
      "quitting relaunched Solace Free Flight returns to the islands without saving");
    game.retireEarthPrefetch();
    if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    std::error_code error; fs::remove_all(root, error);
    printf("%d procedural region runtime failures\n", failed);
    return failed ? 1 : 0;
  }
};
int main() { try { return GameTest::run(); } catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 2; } }
