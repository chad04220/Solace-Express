  static int stateRegressions() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("solace_state_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    int fails = 0;
    auto check = [&](bool ok, const char* name) { printf("State regression: %s: %s\n", name, ok ? "ok" : "FAIL"); fails += !ok; };
    auto near = [](float a, float b) { return fabsf(a - b) < 0.001f; };
    auto fresh = [&](Game& q) { q.initHeadless(); q.career.money = 50000; q.career.license = LIC_CPL; q.saveDir = root.string(); };
    auto amount = [](const std::vector<PayoutLine>& lines, const std::string& label) { int sum = 0; for (const auto& l : lines) if (l.label.rfind(label, 0) == 0) sum += l.amount; return sum; };
    // S1: a real background-flown quote, a partial existing tank, and a player-selected full load.
    {
      Game q; fresh(q); Contract c = g_story[4]; const int spec = 1;
      q.career.location = c.from; q.career.fleet.push_back({spec, c.from, 20.f, 1.f});
      float routeFuel = -1; float minutes = simulateFlightMinutes(c, spec, &routeFuel);
      const std::string key = menuPlan::quoteInputs(c, spec);
      q.quoteFlown[key] = {minutes, routeFuel}; q.launchFuelKg = kAircraft[spec].maxFuel;
      const float uplift = q.launchFuelKg - 20.f;
      const int expected = (int)(uplift * q.career.fuelPrice(c.from, spec));
      auto card = q.career.plan(c, spec, Career::SRC_OWNED); q.applyQuote(c, card, false);
      const int estimateCost = card.fuelCostEst;
      q.career.planFuel(card, c, q.chosenFuel(c, spec, Career::SRC_OWNED, card));   // the card's production data path
      check(card.fuelCostEst == expected && card.net == c.payout - card.fees() - expected && estimateCost != expected, "card operating costs and net use selected uplift rather than estimated fuel");
      q.beginCareerFlight(c, spec, Career::SRC_OWNED);
      Career saved; bool loaded = saved.load((root / "career.sav").string());
      auto same = [&](const Career::LaunchPlan& p) { return p.flown && near(p.fuelLoadKg, kAircraft[spec].maxFuel) && near(p.fuelUpliftKg, uplift) && p.fuelCostEst == expected && near(p.minutesEst, minutes) && near(p.fuelKgEst, routeFuel); };
      check(minutes > 0 && near(q.plane.fuel, kAircraft[spec].maxFuel) && same(q.launchPlan) && q.career.job && same(q.career.job->plan) && loaded && saved.job && same(saved.job->plan), "selected tank, live plan, committed job and disk quote agree");
      printf("  fuel tank %.3f kg, prior 20.000 kg, uplift %.3f kg, billed $%d; route %.3f kg / %.3f min\n", q.plane.fuel, q.launchPlan.fuelUpliftKg, expected, routeFuel, minutes);
      q.quoteFlown[key] = {minutes * 0.5f, routeFuel * 0.5f}; q.plane.fuel -= 8.f;
      q.restartFlight();
      check(same(q.launchPlan) && near(q.plane.fuel, kAircraft[spec].maxFuel), "restart keeps accepted load and quote despite a newer estimate");
      const int via = g_world.findAirport("CAP");
      q.plane.fuel = 60.f; q.flightClock = 180; q.result.divertedTo = via; q.touchedDown = true; q.touchdownFpm = 200;
      q.endFlight(false, "Diverted", OUT_DIVERTED);
      check(amount(q.payout, "Fuel uplift") == -expected && near(q.career.fleet[0].fuel, 60.f), "diversion bills selected uplift once and retains the real remainder");
      q.launchFuelKg = 90.f; q.continueJob(spec, Career::SRC_OWNED);
      Career resumed; loaded = resumed.load((root / "career.sav").string());
      const int secondCost = (int)(30.f * q.career.fuelPrice(via, spec));
      check(q.launchPlan.startAirport == via && near(q.plane.fuel, 90.f) && near(q.launchPlan.fuelUpliftKg, 30.f) && q.launchPlan.fuelCostEst == secondCost && q.career.job && q.career.job->plan.fuelCostEst == secondCost && loaded && resumed.job && near(resumed.job->plan.fuelUpliftKg, 30.f) && near(resumed.job->plan.fuelLoadKg, 90.f), "continued leg saves and flies its own selected tank and uplift");
      q.flightClock = 120; q.plane.onGround = true; q.touchedDown = true; q.touchdownFpm = 200;
      q.endFlight(true, "", OUT_SUCCESS);
      check(amount(q.payout, "Fuel uplift at CAP") == -secondCost && !q.career.job, "final settlement bills the continuation quote exactly once");
    }
    // S2: use the production meter, diversion, save/reload, continuation, restart and settlement paths.
    {
      Game q; fresh(q); Contract c; c.id = "SURVEY_STATE"; c.title = "Survey state regression"; c.type = CT_SURVEY;
      c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("ORC"); c.payout = 10000; c.minLicense = LIC_PPL;
      for (int i = 0; i < 6; i++) c.wps.push_back({1000.f * i, 0, 500.f});
      q.beginCareerFlight(c, 1, Career::SRC_RENT);
      auto meter = [&](float seconds, bool inside) { q.plane.onGround = false; q.takeoffAnnounced = true; q.plane.pos.y = inside ? 500.f : 650.f; q.updateJobMeters(seconds, 50.f); q.flightClock += seconds; };
      auto divert = [&] { q.result.wpDone = q.wpIndex; q.plane.onGround = true; q.touchedDown = true; q.result.divertedTo = c.from; q.endFlight(false, "Diverted", OUT_DIVERTED); };
      auto reload = [&] { Career saved; bool ok = saved.load((root / "career.sav").string()); if (ok) q.career = saved; return ok; };
      q.wpIndex = 1; meter(300.f, false); divert();
      bool saved = reload(); q.continueJob(1, Career::SRC_RENT);
      check(saved && q.wpIndex == 1 && near(q.surveyT, 300.f) && near(q.surveyInT, 0.f) && near(q.result.surveyInBand, 0.f), "bad altitude exposure survives diversion, disk reload and continuation");
      meter(100.f, true); q.wpIndex = 2; divert(); saved = reload();
      check(saved && q.career.job && near(q.career.job->surveySec, 400.f) && near(q.career.job->surveyInSec, 100.f), "second leg adds seconds, not averages or duplicated cumulative totals");
      {
        std::ifstream input(root / "career.sav"); std::string line, v3;
        while (std::getline(input, line)) {
          if (line.rfind("survey_known ", 0) == 0 || line.rfind("plan2 ", 0) == 0 || line.rfind("military ", 0) == 0) continue;
          if (line.rfind("solace_save ", 0) == 0) line = "solace_save 3";
          v3 += line + "\n";
        }
        input.close(); { std::ofstream out(root / "upstream-v3.sav"); out << v3; }
        Career upgraded; bool ok = upgraded.load((root / "upstream-v3.sav").string());
        check(ok && upgraded.job && upgraded.job->surveyHistoryKnown && near(upgraded.job->surveySec, 400.f)
              && near(upgraded.job->surveyInSec, 100.f), "upstream v3 job3 exposure remains known and exact");
        if (ok) q.career = upgraded;
      }
      q.continueJob(1, Career::SRC_RENT); meter(80.f, false); q.restartFlight();
      check(near(q.surveyT, 400.f) && near(q.surveyInT, 100.f) && near(q.result.surveySec, 400.f) && q.wpIndex == 2, "restart resets only the current leg and retains prior survey history");
      meter(100.f, true); q.wpIndex = 6; divert(); saved = reload(); q.continueJob(1, Career::SRC_RENT);
      meter(120.f, false);   // all checkpoints already passed: no new survey exposure, but old history must score
      check(saved && q.wpIndex == 6 && near(q.surveyT, 500.f) && near(q.surveyInT, 200.f) && near(q.result.surveyInBand, 0.4f), "completed checkpoints preserve the 200/500 weighted ratio on the final leg");
      q.plane.onGround = true; q.touchedDown = true; q.touchdownFpm = 200; q.endFlight(true, "", OUT_SUCCESS);
      check(amount(q.payout, "Survey altitude held") == -3600 && !q.career.job, "final survey payout uses all legs (40 percent, $3600 deduction), no false bonus");
      // A legitimate older v3 recovery save lacks the new fields. Migrate its unknowable history explicitly.
      q.beginCareerFlight(c, 1, Career::SRC_RENT); q.wpIndex = 6; q.flightClock = 300; divert();
      std::ifstream input(root / "career.sav"); std::string line, old;
      while (std::getline(input, line)) { if (line.rfind("job3 ", 0) == 0 || line.rfind("survey_known ", 0) == 0 || line.rfind("survey ", 0) == 0 || line.rfind("plan2 ", 0) == 0 || line.rfind("military ", 0) == 0) continue; if (line.rfind("solace_save ", 0) == 0) line = "solace_save 3"; old += line + "\n"; }
      input.close(); { std::ofstream output(root / "legacy.sav"); output << old; }
      Career legacy; bool migrated = legacy.load((root / "legacy.sav").string()); q.career = legacy;
      check(migrated && q.career.job && q.career.job->wpDone == 6 && !q.career.job->surveyHistoryKnown && near(q.career.job->jobClockMin, 5.f), "older save keeps checkpoints and clock with explicit unknown altitude history");
      q.continueJob(1, Career::SRC_RENT); saved = reload();
      check(saved && q.career.job && !q.career.job->surveyHistoryKnown, "migration marker survives the next current-format save");
      q.flightClock = 120; q.plane.onGround = true; q.touchedDown = true; q.touchdownFpm = 200; q.endFlight(true, "", OUT_SUCCESS);
      bool disclosed = false; for (const auto& l : q.payout) disclosed |= l.label.find("older save") != std::string::npos && l.amount == 0;
      check(disclosed && amount(q.payout, "Survey altitude held") == 0, "legacy history waiver is disclosed and invents neither bonus nor penalty");
    }
    std::error_code ec; fs::remove_all(root, ec);
    return fails;
  }
