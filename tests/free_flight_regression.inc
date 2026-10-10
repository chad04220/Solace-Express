  static int freeFlightRegressions() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("solace_free_flight_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    int fails = 0;
    auto check = [&](bool ok, const std::string& name) { printf("Free Flight: %s: %s\n", name.c_str(), ok ? "ok" : "FAIL"); fails += !ok; };
    auto near = [](float a, float b) { return fabsf(a - b) < 0.001f; };
    auto bytes = [](const fs::path& path) { std::ifstream f(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); };
    auto snapshot = [&](const Career& c) { const auto path = root / "snapshot.sav"; if (!c.save(path.string())) return std::string("SAVE FAILED"); return bytes(path); };

    // All airports and all nine ordinary airframes are available to a new, penniless
    // student without a fleet. Every start is a fresh aircraft with full tanks.
    {
      Game q; q.initHeadless(); q.botControl = true; q.set.traffic = false;
      q.career.money = 0; q.career.fleet.clear(); q.saveDir = (root / "new_player").string(); fs::create_directories(q.saveDir);
      q.selHangar = kResearchJet; q.selAircraft = 3; q.launchFuelKg = 12.f;
      const std::string before = snapshot(q.career);
      q.beginFreeFlightSetup();
      bool all = q.screen == SCR_FREE_FLIGHT, ground = true, air = true, noFees = true;
      int launches = 0;
      for (int craft = 0; craft < kNumAircraft; ++craft) for (int airport = 0; airport < (int)g_world.airports.size(); ++airport) for (int airborne = 0; airborne < 2; ++airborne) {
        q.freeCraft = craft; q.freeAirport = airport; q.freeAirborne = airborne != 0; q.launchFreeFlight(); ++launches;
        all &= q.screen == SCR_FLIGHT && q.freeFlight && q.isolatedFlight && !q.researchFlight && q.specIdx == craft && q.plane.spec == &kAircraft[craft];
        all &= q.contract.from == airport && q.contract.to == airport && q.contract.startAirborne == (airborne != 0) && q.contract.wps.empty() && !q.contract.story;
        noFees &= q.source == Career::SRC_NONE && q.launchPlan.fees() == 0 && q.launchPlan.fuelCostEst == 0 && q.launchPlan.net == 0 && near(q.plane.fuel, kAircraft[craft].maxFuel) && q.failPlan.kind == 0;
        if (airborne) air &= !q.plane.onGround && q.plane.agl() >= 899.f && q.plane.engineRunning && q.takeoffAnnounced && !q.parkingBrake && near(q.plane.ctl.brake, 0.f) && near(length(q.plane.vel), kAircraft[craft].cruise) && (!kAircraft[craft].retract || (near(q.plane.gear, 0.f) && !q.plane.ctl.gearDown));
        else ground &= q.plane.onGround && q.parkingBrake && q.plane.ctl.brake > 0.99f && g_world.onRunway(q.plane.pos.x, q.plane.pos.z, 10.f) == airport;
        q.endFlight(false, "", OUT_ABANDONED);
        all &= q.screen == SCR_FREE_FLIGHT && !q.freeFlight && !q.isolatedFlight;
      }
      check(kNumAircraft == 9 && launches == 2 * kNumAircraft * (int)g_world.airports.size() && all, fmt("all %d aircraft/airport/start combinations launch and return", launches));
      check(ground && air && noFees, "runway parking, terrain-safe cruise starts, full tanks and zero fees");
      check(snapshot(q.career) == before && q.selHangar == kResearchJet && q.selAircraft == 3 && near(q.launchFuelKg, 12.f), "new-player career and career UI selections unchanged");
      q.cancelFreeFlightSetup();
      check(q.screen == SCR_MENU && !q.hasSave && !fs::exists(fs::path(q.saveDir) / "career.sav"), "setup/back never creates a career save");

      // Research includes two craft whose special flag is zero: reject by catalog
      // range, not merely by that flag, including out-of-range/negative indices.
      q.beginFreeFlightSetup();
      bool rejected = true;
      for (int craft : {-1, kNightjar, kResearchJet, kMantis, kWraith, 1000000}) {
        q.freeCraft = craft; q.launchFreeFlight();
        rejected &= q.screen == SCR_FREE_FLIGHT && !q.freeFlight;
      }
      q.freeCraft = 0;
      for (int airport : {-1, (int)g_world.airports.size(), 1000000}) {
        q.freeAirport = airport; q.launchFreeFlight();
        rejected &= q.screen == SCR_FREE_FLIGHT && !q.freeFlight;
      }
      check(rejected && snapshot(q.career) == before, "all research aircraft and invalid selections rejected without side effects");
      q.freeCraft = kWraith; q.freeAirport = -17; q.beginFreeFlightSetup();
      check(q.freeCraft >= 0 && q.freeCraft < kNumAircraft && q.freeAirport == 0, "stale setup choices are clamped to available ordinary aircraft/airports");
    }

    // Preserve both an ACTIVE and a RECOVERY job, its paid fees/checkpoints, an
    // owned aircraft's fuel/wear, a loan, airline, progress and exact save bytes.
    for (Career::JobState::State state : {Career::JobState::ACTIVE, Career::JobState::RECOVERY}) {
      Game q; q.initHeadless(); q.botControl = true; q.set.traffic = false; q.failuresArmed = true;
      const std::string label = state == Career::JobState::ACTIVE ? "active job" : "recovery job";
      const fs::path dir = root / label; fs::create_directories(dir); q.saveDir = dir.string();
      q.career.money = 13; q.career.license = LIC_STUDENT; q.career.reputation = 7; q.career.storyIndex = 2;
      q.career.flights = 12; q.career.landings = 8; q.career.crashes = 2; q.career.hours = 4.25f; q.career.bestLandingFpm = 77.f;
      q.career.fleet = {{1, 1, 13.f, 0.31f}}; q.career.loan = {1, 21000, 1200, 2, 0.12f}; q.career.insured = true;
      q.career.airline.pilots.push_back({"Test Pilot", 2, 120}); q.career.airline.routes.push_back({0, 0, 1, 0, 3, 999});
      q.career.airline.earned = 999; q.career.airline.log = {"Existing airline log"};
      Contract c = g_story[4]; c.id = "FREE_ISOLATION_JOB"; c.story = false;
      c.wps = {{0, 0, 500}, {1000, 0, 500}, {2000, 0, 500}};
      Career::LaunchPlan p; p.spec = 1; p.src = Career::SRC_OWNED; p.startAirport = c.from; p.fuelLoadKg = 13;
      q.career.accept(c, 1, Career::SRC_OWNED, p); q.career.job->state = state; q.career.job->wpDone = 2;
      q.career.job->jobClockMin = 17.5f; q.career.job->hirePaid = q.career.job->ferryPaid = true;
      q.career.attempt = 47; q.career.attemptOpen = state == Career::JobState::ACTIVE;
      q.selHangar = 7; q.selAircraft = 1; q.launchFuelKg = 42.f; q.hasSave = true;
      const fs::path save = dir / "career.sav";
      check(q.career.save(save.string()), label + ": baseline save created");
      Career verified; check(verified.load(save.string()), label + ": baseline is a valid reloadable career save");
      const std::string before = bytes(save); const auto modified = fs::last_write_time(save);
      auto unchanged = [&] { return snapshot(q.career) == before && bytes(save) == before && fs::last_write_time(save) == modified && !fs::exists(save.string() + ".bak") && q.career.airline.log.size() == 1 && q.career.airline.log[0] == "Existing airline log" && q.hasSave && !q.pendingCareer && q.selHangar == 7 && q.selAircraft == 1 && near(q.launchFuelKg, 42.f); };
      q.beginFreeFlightSetup(); q.freeCraft = 6; q.freeAirport = 2; q.freeAirborne = true; q.launchFreeFlight();
      for (int i = 0; i < 12; ++i) { q.update(1.f / 60.f); q.in.endFrame(); }
      check(q.screen == SCR_FLIGHT && q.freeFlight && !q.jobLeg && q.jobClockBase == 0 && q.wpIndex == 0 && q.failPlan.kind == 0 && unchanged(), label + ": launch and live update ignore job/ownership/licence/money");
      q.plane.fuel = 1.f; q.restartFlight();
      check(q.freeFlight && q.isolatedFlight && q.contract.startAirborne && near(q.plane.fuel, kAircraft[q.freeCraft].maxFuel) && q.plane.agl() >= 899 && unchanged(), label + ": restart/refill retains free mode and charges nothing");
      q.paused = q.showMap = q.showRadio = true;
      q.endFlight(false, "Abandoned flight", OUT_ABANDONED);
      check(q.screen == SCR_FREE_FLIGHT && !q.freeFlight && !q.isolatedFlight && !q.paused && !q.showMap && !q.showRadio && unchanged(), label + ": pause return has no settlement or save");
      q.freeAirborne = false; q.launchFreeFlight();
      q.takeoffAnnounced = q.touchedDown = true; q.tdRunway = q.freeAirport;
      // Plane::reset gives the tyres a small spawn clearance. Let them settle and
      // then satisfy the real stopped timer rather than assuming first-frame contact.
      for (int i = 0; i < 240 && q.screen == SCR_FLIGHT; ++i) {
        q.update(1.f / 60.f); q.in.endFrame();
        if (i == 0) printf("Free Flight: %s: first-frame wheel contact %d, stopped timer %.3f, career unchanged %d\n", label.c_str(), q.plane.onGround, q.stillTimer, unchanged());
      }
      check(q.screen == SCR_FREE_FLIGHT && !q.crashed && q.plane.onGround && q.stillTimer > 1.2f && q.plane.fuel > 0 && unchanged(), label + ": real stopped-on-runway completion preserves career");
      for (FlightOutcome outcome : {OUT_SUCCESS, OUT_DIVERTED, OUT_OFF_AIRPORT, OUT_OUT_OF_FUEL, OUT_CRASHED}) {
        q.launchFreeFlight(); q.flightClock = 1000; q.plane.fuel = 0; q.touchedDown = true; q.touchdownFpm = 1400; q.result.divertedTo = 1;
        q.endFlight(outcome == OUT_SUCCESS, "Test flight ended", outcome);
        check(q.screen == SCR_FREE_FLIGHT && unchanged(), label + fmt(": outcome %d bypasses scoring, debt, XP/progress and save", (int)outcome));
      }
      q.freeAirborne = true; q.launchFreeFlight(); q.crashed = true; q.crashTimer = q.crashEndT + 1; q.plane.ev.crashReason = "Test crash";
      q.update(1.f / 60.f); q.in.endFrame();
      check(q.screen == SCR_FREE_FLIGHT && unchanged(), label + ": crash timer returns to free hangar without penalties");
      q.launchFreeFlight(); q.screen = SCR_LOADING; q.in.pressed[K_ESC] = true; q.updateLoading(1.f / 60.f); q.in.endFrame();
      check(q.screen == SCR_FREE_FLIGHT && !q.freeFlight && unchanged(), label + ": loading cancellation does not close or save existing career attempt");
      q.cancelFreeFlightSetup();
      check(q.screen == SCR_MENU && unchanged(), label + ": back to main menu preserves career save byte-for-byte");

      // A failed career transaction stays pending; Free Flight neither depends on it
      // nor silently commits it while the setup, loading or flight screen is active.
      q.pendingCareer = q.career; q.pendingCareer->money += 111; q.retryT = 2.99f; q.saveWhy = "Pending test save";
      const std::string pending = snapshot(*q.pendingCareer);
      q.beginFreeFlightSetup(); q.update(0.05f); q.in.endFrame(); q.launchFreeFlight(); q.update(0.05f); q.in.endFrame();
      q.endFlight(false, "", OUT_ABANDONED); q.update(0.05f); q.in.endFrame(); q.cancelFreeFlightSetup();
      check(q.screen == SCR_MENU && snapshot(q.career) == before && q.pendingCareer && snapshot(*q.pendingCareer) == pending && bytes(save) == before && fs::last_write_time(save) == modified && q.saveWhy == "Pending test save", label + ": unsaved career transaction neither blocks nor leaks into Free Flight");
    }
    std::error_code ec; fs::remove_all(root, ec);
    printf("%d Free Flight failures\n", fails);
    return fails;
  }
