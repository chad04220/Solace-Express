// The pilot's profile (profile.h) and the debrief's landing mark: the mark's scale, end to end; the profile kept and
// read back; every landmark a pilot can find, inside the islands; a touchdown's stamp, butter and the type's softest;
// a landmark found from the air; the settlement's "Flawless flight" only for three stars and an S; and a test's or a
// tool's session never writing the player's profile
#include "../src/game.h"
#include "test_world.h"
#include <cstdio>
#include <cstdlib>

struct GameTest {
  static int run() {
    buildTestWorld(); buildStory(); g_audio.init(48000);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("%s: %s\n", what.c_str(), ok ? "ok" : "FAIL"); fails += !ok; };
    const AircraftSpec& s = kAircraft[1];
    const float vref = s.vref * MS_TO_KT;
    {   // the mark
      FlightResult r; r.landed = true; r.touchdownFpm = 120; r.tdPastThrM = 300; r.rwyLenM = 1500; r.centerlineErr = 1; r.thrKt = vref + 5; r.thrAglM = 15;
      LandingMark m = landingMark(r, s);
      check(m.score == 100 && m.letter == 'S', fmt("A butter in the zone on the centreline from a stable approach: %d %c", m.score, m.letter ? m.letter : '-'));
      r.touchdownFpm = 300; r.tdPastThrM = 600; r.centerlineErr = 4; r.thrKt = vref + 20; r.thrAglM = 30;
      m = landingMark(r, s);
      check(m.score == 25 + 12 + 9 + 10 && m.letter == 'C', fmt("A good touchdown past the first third, 4 m off, a little fast and high: %d %c", m.score, m.letter ? m.letter : '-'));
      r.touchdownFpm = 700; r.tdPastThrM = 900; r.centerlineErr = 12; r.thrKt = vref + 40; r.thrAglM = 60;
      m = landingMark(r, s);
      check(m.score == 0 && m.letter == 'D', fmt("Hard, long, off the centreline, fast and high: %d %c", m.score, m.letter ? m.letter : '-'));
      r.landed = false;
      check(landingMark(r, s).score < 0, "No touchdown at the destination: no mark");
    }
    const std::string dir = "profile_test_dir";
    {   // kept and read back
#ifdef _WIN32
      system(("mkdir " + dir + " 2> NUL").c_str());
#else
      system(("mkdir -p " + dir).c_str());
#endif
      remove((dir + "/profile.cfg").c_str());
      Profile p; p.name = "Ada"; p.welcomed = true;
      const bool first = p.earn("butter"), again = p.earn("butter"), unknown = p.earn("no_such_thing");
      p.airports.insert("MDB"); p.airports.insert("FAR"); p.sites.insert("windfarm"); p.seen.insert("ufo"); p.best["dawnlark"] = 42.5f; p.add("butters", 3);
      const bool saved = p.save(dir + "/profile.cfg");
      Profile q; const bool loaded = q.load(dir + "/profile.cfg");
      check(first && !again && !unknown && saved && loaded && q.name == "Ada" && q.welcomed && q.has("butter") && q.ach.size() == 1 && q.airports.size() == 2 &&
            q.sites.count("windfarm") && q.seen.count("ufo") && fabsf(q.best["dawnlark"] - 42.5f) < 0.05f && fabsf(q.count("butters") - 3.f) < 1e-3f,
            "The profile is kept and read back; an achievement is earned once, an unknown one never");
    }
    {   // the landmarks
      const auto& sites = profileSites();
      bool inside = true; std::set<std::string> ids;
      for (const Site& st : sites) { inside &= fabsf(st.pos.x) < WORLD_HALF && fabsf(st.pos.z) < WORLD_HALF && std::isfinite(st.pos.y) && !st.name.empty(); ids.insert(st.id); }
      printf("  %zu landmarks:", sites.size()); for (const Site& st : sites) printf(" %s", st.id.c_str()); printf("\n");
      check(sites.size() >= 10 && inside && ids.size() == sites.size(), fmt("%d landmarks to find, each inside the islands, each once", (int)sites.size()));
    }
    {   // a session in play: a touchdown's stamp and achievements; a landmark found; nothing written without a save folder
      remove("profile.cfg");
      Game g; g.initHeadless(); g.set.traffic = false; g.failuresArmed = false;
      Contract c; c.id = "PROF"; c.title = "Profile"; c.type = CT_FERRY; c.startAirborne = true;
      c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("FAR"); c.wx = Weather(); c.wx.windSpeed = 2.f; c.wx.timeOfDay = 12.f;
      g.startFlight(c, 1, Career::SRC_RENT);
      g.screen = SCR_FLIGHT;
      const int far = g_world.findAirport("FAR");
      g.profileTouchdown(55.f, far);
      const bool stamped = g.profile.airports.count("FAR") == 1;
      const bool butter = g.profile.has("butter") && g.profile.has("greaser") && g.flightAch.size() >= 2 && !g.achShow.empty();
      const bool best = fabsf(g.profile.best[kAircraft[1].id] - 55.f) < 0.01f && !g.pbNew;   // (the type's first: not a "new best")
      g.profileTouchdown(40.f, far);
      const bool beat = g.pbNew && fabsf(g.pbOld - 55.f) < 0.01f;
      FILE* f = fopen("profile.cfg", "r"); const bool wrote = f != nullptr; if (f) fclose(f);
      check(stamped && butter && best && beat && !wrote, "A touchdown at Far Isle: its stamp, Butter and Greaser, the type's softest, then a new best; no profile written by a test");
      // over a landmark, low
      const auto& sites = profileSites();
      if (!sites.empty()) {
        const Site& st = sites[0];
        g.plane.reset(&s, vec3(st.pos.x, std::max(st.pos.y, 0.f) + 600.f, st.pos.z), 90.f, s.maxFuel, 100.f, true, s.cruise);
        for (int i = 0; i < 90 && !g.profile.sites.count(st.id); i++) { g.update(1.f / 60.f); g.in.endFrame(); }
        bool told = false; for (auto& t : g.toasts) told |= t.text.find("Landmark found") != std::string::npos;
        check(g.profile.sites.count(st.id) == 1 && told, "Flying over " + st.name + ": found, and the pilot told");
      }
    }
    {   // with a save folder the profile is written (and a tool's diskless session still never is)
      Game g; g.initHeadless(); g.saveDir = dir;
      remove((dir + "/profile.cfg").c_str());
      g.award("night");
      Profile q; const bool kept = q.load(dir + "/profile.cfg") && q.has("night");
      Game t; t.initHeadless(); t.saveDir = dir; t.diskless = true;
      remove((dir + "/profile.cfg").c_str());
      t.award("night");
      FILE* f = fopen((dir + "/profile.cfg").c_str(), "r"); const bool toolWrote = f != nullptr; if (f) fclose(f);
      check(kept && !toolWrote, "An achievement is saved at once; a tool's session writes nothing");
    }
    {   // the settlement: "Flawless flight" for three stars and an S, not for less
      Career k; k.newGame(); k.license = LIC_ATP; k.refreshBoard();
      Contract c = k.board.empty() ? g_story[1] : k.board[0];
      c.type = CT_CARGO; c.pax = 0; c.fragile = false; c.timeLimitMin = 0; c.story = false; if (c.payout <= 0) c.payout = 2000;
      FlightResult r; r.success = true; r.landed = true; r.touchdownFpm = 120; r.tdPastThrM = 300; r.rwyLenM = 1500; r.centerlineErr = 1;
      r.thrKt = vref + 5; r.thrAglM = 15; r.fuelLeftFrac = 0.5f; r.maxG = 1.2f; r.minG = 0.8f; r.flightMin = 20;
      int stars = 0;
      auto lines = k.settle(c, 1, Career::SRC_RENT, r, &stars);
      bool flawless = false; for (auto& l : lines) flawless |= l.label == "Flawless flight";
      Career k2; k2.newGame(); k2.license = LIC_ATP;
      r.centerlineErr = 6; r.tdPastThrM = 700;   // (an A at best)
      int stars2 = 0; auto lines2 = k2.settle(c, 1, Career::SRC_RENT, r, &stars2);
      bool flawless2 = false; for (auto& l : lines2) flawless2 |= l.label == "Flawless flight";
      check(stars == 3 && flawless && !flawless2, fmt("Flawless flight paid for three stars and an S (%d stars), not for an A", stars));
    }
    {   // the trials: a gate run finished - its card, the medal, the board; the splits and the ghost kept for the next run
      Game g; g.initHeadless(); g.saveDir = dir; g.set.traffic = false;
      remove((dir + "/ghost_T_GATES.bin").c_str()); remove((dir + "/settings.cfg").c_str());
      g.startTrial(Game::TR_GATES, 1, Career::SRC_LESSON);
      float m[3]; g.trialMedalMarks(Game::TR_GATES, 1, m);
      const bool marks = m[0] > 60.f && m[0] < 400.f && m[1] > m[0] && m[2] > m[1];
      g.trialT0 = 10.f; g.trialT1 = 10.f + m[0] * 0.98f;   // (just inside gold)
      for (int i = 0; i < 6; i++) g.trialSplits.push_back(10.f + i * 12.f);
      for (int i = 0; i < 50; i++) g.ghostRec.push_back(vec3((float)i, 100.f, 0.f));
      g.wpIndex = (int)g.contract.wps.size(); g.touchedDown = true; g.plane.onGround = true;
      g.endFlight(true, "");
      const Game::TrialResult& R = g.trialResult;
      const bool card = R.kind == Game::TR_GATES && R.medal == 3 && R.newBest && R.rank == 1 && fabsf(R.score - m[0] * 0.98f) < 0.01f && g.screen == SCR_HUB && g.profile.has("gold");
      Game h; h.initHeadless(); h.saveDir = dir; h.loadSettings(); h.set.traffic = false;
      h.startTrial(Game::TR_GATES, 1, Career::SRC_LESSON);
      const bool kept = h.trialBest.count("T_GATES") && h.trialBestSplits["T_GATES"].size() == 6 && fabsf(h.trialBestSplits["T_GATES"][3] - 46.f) < 0.01f && h.ghostBest.size() == 50;
      // a slower run: no new best, the best's splits kept, its ghost kept
      h.trialT0 = 5.f; h.trialT1 = 5.f + m[2] * 1.2f; h.trialSplits = {1, 2, 3}; h.ghostRec.assign(10, vec3());
      h.wpIndex = (int)h.contract.wps.size(); h.touchedDown = true; h.plane.onGround = true;
      h.endFlight(true, "");
      const bool slower = h.trialResult.medal == 0 && !h.trialResult.newBest && h.trialResult.rank == 2 && h.trialBestSplits["T_GATES"].size() == 6;
      Game k; k.initHeadless(); k.saveDir = dir; k.startTrial(Game::TR_GATES, 1, Career::SRC_LESSON);
      const bool ghostKept = k.ghostBest.size() == 50;
      const std::string daily = g.trialKey(Game::TR_DAILY);
      check(marks && card && kept && slower && ghostKept && daily.rfind("T_DAILY.", 0) == 0,
            fmt("A gate run inside %.0f s: gold, a new best, its splits and ghost kept; a slower one after it rank 2, no medal; the daily's board is the day's (%s)", m[0], daily.c_str()));
    }
    {   // the photo flights on the board: over a landmark, two gates; the shot paid, a missed one costs half
      Career k; k.newGame(); k.license = LIC_CPL; int found = 0; Contract photo;
      for (int seed = 1; seed < 400 && !found; seed++) {
        k.boardSeed = (uint32_t)seed; k.location = seed % (int)g_world.airports.size(); k.refreshBoard();
        for (auto& c : k.board) if (c.type == CT_TOUR) { photo = c; found++; break; }
      }
      bool sane = found && photo.wps.size() == 2 && photo.pax == 1 && photo.payout > 0 && photo.title.rfind("Photo flight: ", 0) == 0;
      if (sane) for (auto& w : photo.wps) sane &= w.alt > std::max(g_world.height(w.x, w.z), 0.f) + 200.f;
      FlightResult r; r.success = true; r.landed = true; r.touchdownFpm = 200; r.fuelLeftFrac = 0.5f; r.flightMin = 20; r.wpDone = 2;
      Career a; a.newGame(); a.license = LIC_CPL; int st = 0; auto L1 = a.settle(photo, 1, Career::SRC_RENT, r, &st);
      r.wpDone = 1; Career b; b.newGame(); b.license = LIC_CPL; int st2 = 0; auto L2 = b.settle(photo, 1, Career::SRC_RENT, r, &st2);
      bool paid = false, missed = false; for (auto& l : L1) paid |= l.label == "The photographs"; for (auto& l : L2) missed |= l.label.rfind("No photographs", 0) == 0;
      check(sane && paid && missed && st2 < st, "A photo flight on the board: " + (found ? photo.title : std::string("none")) + ", two gates clear of the ground; the shot paid, a missed one costs half");
    }
    {   // the passengers' request: the landmark asked for, flown by - the tip
      Game g; g.initHeadless(); g.set.traffic = false; g.failuresArmed = false;
      Contract c; c.id = "PAXT"; c.title = "Pax"; c.type = CT_PAX; c.pax = 4; c.payout = 3000; c.startAirborne = true;
      c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("CAP"); c.wx = Weather();
      g.startFlight(c, 1, Career::SRC_RENT); g.screen = SCR_FLIGHT; g.takeoffAnnounced = true;
      const int req = g.sightReq;
      bool asked = false, tipped = false;
      if (req >= 0) {
        const Site& st = profileSites()[req];
        g.plane.reset(&kAircraft[1], vec3(st.pos.x + 8000.f, std::max(st.pos.y, 0.f) + 600.f, st.pos.z), 90.f, kAircraft[1].maxFuel, 100.f, true, kAircraft[1].cruise);
        g.updateSightseeing(); asked = g.sightAsked && !g.sightDone;
        g.plane.pos = vec3(st.pos.x, std::max(st.pos.y, 0.f) + 600.f, st.pos.z);
        g.updateSightseeing(); tipped = g.sightDone && g.result.sightseen;
      }
      Career k; k.newGame(); k.license = LIC_CPL; FlightResult r; r.success = true; r.landed = true; r.touchdownFpm = 200; r.fuelLeftFrac = 0.5f; r.sightseen = true; int st = 0;
      bool tip = false; for (auto& l : k.settle(c, 1, Career::SRC_RENT, r, &st)) tip |= l.label.rfind("Sightseeing tip", 0) == 0;
      check(req >= 0 && asked && tipped && tip, fmt("A passenger job MDB-CAP: asked for %s, flown by, the tip paid", req >= 0 ? profileSites()[req].name.c_str() : "nothing"));
    }
    {   // the legendary contracts: five, each flyable by its type (the career autopilot flies each to its end)
      const auto L = Career::legendaryContracts();
      int flown = 0; std::string which;
      for (const Contract& c : L) {
        const int spec = c.requireSpec >= 0 ? c.requireSpec : 5;   // (each names its type)
        const float m = simulateFlightMinutes(c, spec);
        printf("  %s (%s): %.1f min\n", c.title.c_str(), kAircraft[spec].name, m);
        if (m > 0.f && (c.timeLimitMin <= 0.f || m <= c.timeLimitMin)) flown++; else which += " " + c.id;
      }
      check(L.size() == 5 && flown == 5, fmt("The five legendary contracts, each flown to its end by the autopilot (and inside its limit)%s", which.c_str()));
    }
    printf("%d failures\n", fails);
    return fails ? 1 : 0;
  }
};

int main() { return GameTest::run(); }
