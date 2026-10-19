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
    printf("%d failures\n", fails);
    return fails ? 1 : 0;
  }
};

int main() { return GameTest::run(); }
