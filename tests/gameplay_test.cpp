// End-to-end gameplay test: a scripted pilot flies Lesson 1 and then lands at a destination,
// exercising the real game loop (completion detection, scoring, payout, story progression).
#include "../src/game.h"
#include "test_world.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
// attitude-based vertical-speed controller (same structure as the in-game autopilot)
// the pilot steers by the track over the ground (a heading held against a sideslip or a crosswind drifts off the line)
static float trackDeg(const Plane& p) { return wrapDeg360(atan2f(p.vel.x, -p.vel.z) / DEG); }
static float s_pI = 0, s_eI = 0;
static void pitchFor(Plane& p, float vsT, float dt, float maxPitch = 14.f) {
  s_pI = clampf(s_pI + (vsT - p.vel.y) * 0.15f * dt, -8.f, 12.f);
  float pitchT = clampf(s_pI + 0.8f * (vsT - p.vel.y), -10.f, maxPitch);
  // (and trims as it goes: holding an attitude with the flaps out, or slow, takes a steady push or pull)
  s_eI = clampf(s_eI + 0.03f * (pitchT - p.pitchDeg()) * dt, -0.7f, 0.7f);
  p.ctl.pitch = clampf(0.07f * (pitchT - p.pitchDeg()) - 1.2f * p.w.x + s_eI, -1, 1);
}
struct GameTest {
#include "state_regression.inc"
#include "free_flight_regression.inc"
#include "lesson_voice_regression.inc"
  static int run(int part = 0) {
    buildTestWorld(); buildStory();
    g_audio.init(48000);
    // part (--part 1..4): the test in four pieces CI runs side by side, each from a fresh game (0, the default: all
    // of it, one after another). 1 the state, free-flight and voice regressions; 2 Lesson 1, the landing, the towers;
    // 3 the parking brake, the quote and the fuel estimates; 4 the rest
    auto in = [&](int k) { return part == 0 || part == k; };
    Game g; g.initHeadless(); g.botControl = true;
    if (getenv("SOLACE_FREE_FLIGHT_ONLY")) return freeFlightRegressions();
    int fails = getenv("SOLACE_VOICE_ONLY") || !in(1) ? 0 : stateRegressions() + freeFlightRegressions();
    if (getenv("SOLACE_STATE_ONLY")) { printf("%d state failures\n", fails); return fails; }
#ifdef SOLACE_ASSETS
    bool voices = g.atc.load(std::string(SOLACE_ASSETS) + "/voice");   // the tower voices (the audio is rendered below, as the audio thread would)
    if (!voices) { printf("Voice index: FAIL (assets/voice/voice_index.txt did not load; the voice checks below need it)\n"); fails++; }
#else
    bool voices = false;
#endif
    if (in(1) || getenv("SOLACE_VOICE_ONLY")) fails += controlVoiceRegressions();
    if (getenv("SOLACE_VOICE_ONLY")) { printf("%d voice failures\n", fails); return fails; }
    static float abuf[2 * 4096];
    if (voices && in(1)) {   // the voice lines resolve for the game's messages, fixed and assembled from fragments
      struct Case { const char* msg; const char* mission; bool pad, want; } cases[] = {
        {"Checkpoint 2 of 6", "", false, true}, {"Flaps 33%", "", false, true}, {"Gear down", "", false, true},
        {"Runway 05, Wind 050@4kt, scattered 4500ft, vis 10+km, 09:30", "", false, true}, {"MDB - Meadowbrook Field", "", false, true},
        {"BUTTER!  42 fpm", "", false, true}, {"Gear collapsed - hit at 812 fpm", "", false, true}, {"SPLASH 2 - Wren 180 down", "", false, true},
        {"3 aircraft caught in the blast", "", false, true}, {"ENGINE OFF - press I to restart", "", false, true}, {"STALL", "", false, true},
        {"SPECTRE: That's the show - Specters breaking off. Fly safe!", "", false, true}, {"Pods 60 deg", "", false, true},
        {"Nice! Hold a gentle climb about 7 degrees nose-up. Fly through the green rings.", "L1", false, true},
        {"Press B to release the parking brake, then hold SHIFT (or gamepad RT) to add full throttle.", "L1", true, false},   // lesson speech uses semantic phase identity
        {"Press B to release the parking brake, then hold SHIFT (or gamepad RT) to add full throttle.", "L1", false, false},  // not for keyboard
        {"Engine running. Cleared for takeoff runway 05.", "", false, false},   // the towers clear takeoff
        {"Something the packs never recorded", "", false, false}};
      int bad = 0;
      for (auto& c : cases) { AtcVoice::Tx tx; if (g.atc.resolve(c.msg, c.mission, c.pad, tx) != c.want) { printf("   resolve '%s' pad %d: expected %d\n", c.msg, c.pad, c.want); bad++; } }
      printf("Voice line resolution (%d cases): %s\n", (int)(sizeof(cases) / sizeof(cases[0])), bad ? "FAIL" : "ok"); fails += bad > 0;
    }
    static std::vector<int16_t> rec;   // ATCWAV=<file>: the mix the player hears, written out at the end
    auto audio = [&](float dt) {
      int n = std::min(4096, (int)(dt * 48000.f + 0.5f)); g_audio.render(abuf, n);
      if (getenv("ATCWAV")) for (int i = 0; i < n * 2; i++) rec.push_back((int16_t)(clampf(abuf[i], -1, 1) * 32767));
    };
    // ---- sun: finite light for every second of the day, in clear and overcast weather
    if (in(1)) for (int wxi = 0; wxi < 2; wxi++) {
      g.wx.cloudCover = wxi ? 1.f : 0.f; g.wx.storm = wxi == 1;
      int bad = 0;
      for (int sec = 0; sec < 86400; sec++) {
        vec3 d, c; float n;
        g.computeSun(sec / 3600.f, d, c, n);
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) || !std::isfinite(n) || c.x < 0 || c.y < 0 || c.z < 0 || c.x > 1.01f || c.y > 1.01f || c.z > 1.01f) bad++;
      }
      printf("Sun sweep (%s): %d bad samples\n", wxi ? "storm" : "clear", bad);
      if (bad) fails++;
    }
    g.wx = Weather();
    // ---- Lesson 1: takeoff and fly through the rings
    const float dt = 1.f / 60.f;
    float t = 0;
    if (in(2)) {   // part 2: Lesson 1 flown, then the landing and the towers
      g.startFlight(g_story[0], 0, Career::SRC_LESSON);
      for (; t < 400 && g.screen == SCR_FLIGHT; t += dt) {
        Plane& p = g.plane;
        p.ctl.brake = t < 3 ? 1.f : 0.f;
        p.ctl.throttle = t < 3 ? 0.f : 1.f;
        // steer toward the next ring
        vec3 tgt = g.wpIndex < (int)g.contract.wps.size() ? vec3(g.contract.wps[g.wpIndex].x, g.contract.wps[g.wpIndex].alt, g.contract.wps[g.wpIndex].z) : p.pos + p.forward() * 100.f;
        vec3 to = tgt - p.pos;
        float brg = atan2f(to.x, -to.z) / DEG;
        float herr = wrapAngle((brg - trackDeg(p)) * DEG) / DEG;
        if (p.onGround) { p.ctl.yaw = clampf(herr * 0.1f, -1, 1); p.ctl.roll = 0; p.ctl.pitch = p.ias > p.spec->vr ? 0.6f : 0.f; }
        else {
          float bankT = clampf(herr * 1.5f, -25, 25);
          p.ctl.roll = clampf((bankT - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
          float vsT = clampf((tgt.y - p.pos.y) * 0.08f, -3, 4);
          if (p.ias < p.spec->vr + 4) vsT = std::min(vsT, 1.f);
          pitchFor(p, vsT, dt);
          p.ctl.yaw = 0;
        }
        g.update(dt); audio(dt);
        if (getenv("TRACE") && fmodf(t, 3.f) < dt) printf("  t%3.0f gnd %d ias %5.1f agl %6.1f hdg %5.1f pitch %5.1f bank %5.1f vs %5.1f thr %.2f rpm %4.0f run %d\n", t, p.onGround, p.ias, p.agl(), p.heading(), p.pitchDeg(), p.bankDeg(), p.vel.y, p.ctl.throttle, p.rpm, p.engineRunning);
      }
      printf("Lesson 1: screen=%d success=%d wp=%d/%zu t=%.0fs story=%d money=%d %s\n", g.screen, g.lastSuccess, g.wpIndex, g.contract.wps.size(), t, g.career.storyIndex, g.career.money, g.debriefTitle.c_str());
      if (!(g.screen == SCR_DEBRIEF && g.lastSuccess && g.career.storyIndex == 1)) fails++;
      if (voices) {   // the departure tower: greeting, takeoff clearance, then (a circuit lesson) remain in the pattern
        std::vector<std::string> h;
        for (auto& x : g.atc.history) { printf("   voice: %s\n", x.c_str()); if (x.rfind("TWR ", 0) == 0) h.push_back(x.substr(4)); }
        // (the bot rolls 3 s in: then the takeoff clearance supersedes a greeting not yet said)
        size_t k = !h.empty() && h[0].find("Advise when ready to taxi") != std::string::npos ? 1 : 0;
        bool ok = h.size() >= k + 2 && h[k].find("cleared for takeoff") != std::string::npos && h[k + 1].find("Remain in the pattern") != std::string::npos;
        printf("Lesson 1 tower calls: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      // Lesson 1 ends in the air: no landing bonus or penalty may be applied
      for (auto& l : g.payout) if (l.label.find("landing") != std::string::npos) { printf("FAIL: landing line '%s' on an airborne finish\n", l.label.c_str()); fails++; }
    }

    // ---- Approach and landing at Orchard Valley (contract C1 style), Wren rental
    g.career.license = LIC_PPL; g.career.storyIndex = 4; g.career.location = g_world.findAirport("ORC");
    int money0 = g.career.money;
    Contract c = g_story[4];  // C1 ORC -> MDB
    c.wx.windSpeed = 2; c.wx.gust = 0; c.wx.turbulence = 0.05f;
    const Airport& a = g_world.airports[c.to];
    vec3 dir = a.dir();
    vec3 thr = a.threshold(false);
    vec3 start = thr - dir * 3500.f; start.y = a.elev + 3500.f * tanf(3.f * DEG) + 15.f;
    if (in(2)) {   // part 2 (cont.): the landing at Orchard Valley, the towers and the traffic
      g.startFlight(c, 1, Career::SRC_RENT);
      g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
      g.takeoffAnnounced = true; g.engineAutoStarted = true;
      g.atcF.phase = 3; g.atc.history.clear();   // (placed on final: an inbound start, as the game's airborne starts are)
      g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f; s_eI = 0.f;
      float tdFpm = 0;
      for (t = 0; t < 300 && g.screen == SCR_FLIGHT; t += dt) {
        Plane& p = g.plane;
        vec3 rel = p.pos - thr;
        float along = dot(vec3(rel.x, 0, rel.z), dir);          // negative before threshold
        float lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
        float agl = p.pos.y - a.elev;
        if (!p.onGround && !g.touchedDown) {
          float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
          float hdgT = a.heading - clampf(lat * 0.08f, -20, 20);
          float herr = wrapAngle((hdgT - trackDeg(p)) * DEG) / DEG;
          p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
          float vsT = agl < 7.f ? -0.7f : clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1);
          pitchFor(p, vsT, dt);
          p.ctl.throttle = agl < 6.f ? 0.f : clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
          p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
        } else {   // (down: flaps up, the forward pressure eased off - let go at once it ballooned back into the air)
          p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = std::min(0.f, p.ctl.pitch + 0.4f * dt); p.ctl.roll = 0; p.ctl.yaw = 0; p.ctl.flaps = 0; g.flapNotch = 0;
        }
        g.update(dt); audio(dt);
        if (getenv("TRACE") && fmodf(t, 1.0f) < dt && t < 140) printf("  t%4.1f gnd %d ias %5.1f agl %6.1f along %6.0f lat %5.1f pitch %5.1f vs %5.1f thr %.2f bank %5.1f ctlP %5.2f ctlR %5.2f flap %.2f alpha %5.1f\n", t, p.onGround, p.ias, agl, along, lat, p.pitchDeg(), p.vel.y, p.ctl.throttle, p.bankDeg(), p.ctl.pitch, p.ctl.roll, p.flaps, p.alpha/DEG);
        if (g.touchedDown && tdFpm == 0) tdFpm = g.touchdownFpm;
      }
      int total = g.career.money - money0;
      printf("Landing: screen=%d success=%d touchdown=%.0f fpm  stars=%d  money %+d  location=%s  %s\n", g.screen, g.lastSuccess, tdFpm, g.stars, total,
             g_world.airports[g.career.location].code, g.debriefTitle.c_str());
      for (auto& l : g.payout) printf("   %-30s %d\n", l.label.c_str(), l.amount);
      {   // the arrival was recorded for the debrief's coaching
        const FlightResult& r = g.result;
        printf("Arrival: %.0f kt / %.0f m over the threshold, down %.0f m in, %.0f m to spare of %.0f, go-arounds %d\n   coaching: %s\n",
               r.thrKt, r.thrAglM, r.tdPastThrM, r.stopLeftM, r.rwyLenM, r.goArounds, g.coaching.c_str());
        bool ok = r.thrKt > 30 && r.tdPastThrM >= 0 && r.stopLeftM > 0 && r.rwyLenM > 0 && r.goArounds == 0 && !g.coaching.empty();
        printf("Arrival recorded for coaching: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      if (voices) {   // started inbound on a 3.5 km final: the approach call, then cleared to land, then exit the runway
        std::vector<std::string> h;
        for (auto& x : g.atc.history) { printf("   voice: %s\n", x.c_str()); if (x.rfind("TWR ", 0) == 0) h.push_back(x.substr(4)); }
        // (placed straight onto final, the approach call is superseded by the landing clearance before it can be said)
        size_t k = !h.empty() && h[0].find("straight-in runway") != std::string::npos ? 1 : 0;
        bool ok = h.size() >= k + 2 && h[k].find("cleared to land") != std::string::npos && h[k + 1].find("Exit the runway") != std::string::npos;
        printf("Arrival tower calls: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      if (!(g.screen == SCR_DEBRIEF && g.lastSuccess && g.career.location == c.to)) fails++;
      // ---- the towers and the AI traffic: an aircraft on its landing roll down the runway
      // a traffic aircraft on its landing roll, "along" metres past the threshold (held there: placed again every frame)
      auto rollout = [&](int ai, float along) {
        const Airport& ap = g_world.airports[ai];
        TrafficCraft tc; tc.id = 9999; tc.spec = 0; tc.role = TrafficCraft::AIRPORT; tc.state = TrafficCraft::ROLLOUT; tc.airport = ai;
        tc.pos = ap.threshold(false) + ap.dir() * along; tc.pos.y = ap.elev + 1.f; tc.speed = 9.f; tc.hdg = ap.heading * DEG;
        for (auto& o : g.traffic.craft) if (o.id == 9999) { o = tc; return; }
        g.traffic.craft.push_back(tc);
      };
      bool trafficSet = g.set.traffic; g.set.traffic = true;
      auto towerSaid = [&](const char* what) {
        for (auto& x : g.atc.history) if (x.rfind("TWR ", 0) == 0 && x.find(what) != std::string::npos) return true;
        return false;
      };
      if (voices) {   // departure: lined up, traffic on the runway: hold; then cleared once it's gone
        g.startFlight(g_story[0], 0, Career::SRC_LESSON);
        g.traffic.craft.clear(); g.commsPending.clear();   // (without the airport and weather announcements: the exchange is timed against the traffic)
        for (float tt = 0; tt < 30; tt += dt) { rollout(g.contract.from, 600.f); g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0; g.update(dt); audio(dt); }
        bool held = towerSaid("Hold position") && !towerSaid("cleared for takeoff");
        g.set.traffic = false;   // (and the field empties)
        for (float tt = 0; tt < 15; tt += dt) { g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0; g.update(dt); audio(dt); }
        bool cleared = towerSaid("cleared for takeoff");
        for (auto& x : g.atc.history) printf("   voice: %s\n", x.c_str());
        printf("Departure hold for traffic: %s\n", held && cleared ? "ok" : "FAIL"); fails += !(held && cleared);
      }
      if (voices) {   // FLT-6 (the v3.44.0 review): the hold counts from where the aircraft is once the call has been heard - creeping
                      // while it is still being said is no violation; moving 50 m after it is
        g.startFlight(g_story[0], 0, Career::SRC_LESSON);
        g.traffic.craft.clear(); g.commsPending.clear(); g.atc.history.clear(); g.set.traffic = true;
        const vec3 dirR = g_world.airports[g.contract.from].dir();
        bool spoken = false, early = false, heard = false;
        for (float tt = 0; tt < 40 && !heard; tt += dt) {
          rollout(g.contract.from, 600.f);
          g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0;
          if (g.atcF.holding && !g.atcF.holdHeard) { spoken = true; g.plane.pos += dirR * 0.3f; }   // (rolling on through the call)
          g.update(dt); audio(dt);
          if (g.result.holdViolated) early = true;
          heard = g.atcF.holding && g.atcF.holdHeard;
        }
        bool late = false;
        if (heard) { for (int i = 0; i < 30 && !g.result.holdViolated; i++) { rollout(g.contract.from, 600.f); g.plane.pos += dirR * 2.f; g.update(dt); audio(dt); } late = g.result.holdViolated; }
        g.traffic.craft.clear(); g.set.traffic = false;
        const bool ok = spoken && heard && !early && late;
        printf("Hold from where it was heard: creeping through the call %d fined %d, moving once heard fined %d: %s\n", spoken, early, late, ok ? "ok" : "FAIL"); fails += !ok;
        g.endFlight(false, "test", OUT_ABANDONED); g.screen = SCR_HUB;
      }
      if (voices) {   // arrival: the runway is occupied on final (continue), and still occupied on short final (go around)
        g.career.location = g_world.findAirport("ORC");
        g.startFlight(c, 1, Career::SRC_RENT);
        g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true; g.atc.history.clear();
        g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f; s_eI = 0.f;
        g.traffic.craft.clear(); g.set.traffic = true;
        for (t = 0; t < 150 && g.screen == SCR_FLIGHT && !g.plane.onGround; t += dt) {
          rollout(c.to, 300.f);
          Plane& p = g.plane;
          vec3 rel = p.pos - thr; float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
          float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
          float herr = wrapAngle((a.heading - clampf(lat * 0.08f, -20, 20) - trackDeg(p)) * DEG) / DEG;
          p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
          pitchFor(p, clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1), dt);
          p.ctl.throttle = clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
          g.update(dt); audio(dt);
        }
        for (auto& x : g.atc.history) printf("   voice: %s\n", x.c_str());
        bool ok = towerSaid("runway is occupied") && towerSaid("Go around") && !towerSaid("cleared to land");
        ok = ok && g.atcF.lastCall.find("Go around") != std::string::npos && g.atcF.lastValid && g.atcF.lastApt == c.to;   // the HUD recall line
        printf("Arrival with the runway occupied: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      if (voices) {   // FLT-4 (the v3.44.0 review): traffic onto the runway after the landing clearance takes it back - a go-around
                      // on short final; and a departure held past 90 s is cleared only once the tower has the runway cleared
        g.career.location = g_world.findAirport("ORC");
        g.startFlight(c, 1, Career::SRC_RENT);
        g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true; g.atc.history.clear();
        g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f; s_eI = 0.f;
        g.traffic.craft.clear(); g.set.traffic = true;
        bool clearedFirst = false, incursion = false;
        for (t = 0; t < 150 && g.screen == SCR_FLIGHT && !g.plane.onGround; t += dt) {
          Plane& p = g.plane;
          vec3 rel = p.pos - thr; float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
          if (!incursion && towerSaid("cleared to land")) clearedFirst = true;
          if (clearedFirst && along > -600.f) incursion = true;   // (an aircraft rolls onto the runway on short final)
          if (incursion) rollout(c.to, 300.f);
          float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
          float herr = wrapAngle((a.heading - clampf(lat * 0.08f, -20, 20) - trackDeg(p)) * DEG) / DEG;
          p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
          pitchFor(p, clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1), dt);
          p.ctl.throttle = clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
          g.update(dt); audio(dt);
          if (incursion && g.atcF.goAround) break;
        }
        const bool late = clearedFirst && incursion && towerSaid("Go around") && g.atcF.goAround;
        // the departure: lined up, an aircraft stuck on the runway for good - held past 90 s, the tower clears it off
        g.startFlight(g_story[0], 0, Career::SRC_LESSON);
        g.traffic.craft.clear(); g.commsPending.clear(); g.atc.history.clear();
        rollout(g.contract.from, 600.f);
        bool clearedOntoIt = false, gone = false;
        for (float tt = 0; tt < 130 && !towerSaid("cleared for takeoff"); tt += dt) {
          bool on = false; for (auto& o : g.traffic.craft) if (o.id == 9999 && o.alive) on = true;
          if (on) rollout(g.contract.from, 600.f); else gone = true;   // (stuck there while it lasts)
          g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0; g.update(dt); audio(dt);
          bool still = false; for (auto& o : g.traffic.craft) if (o.id == 9999 && o.alive) still = true;
          if (!still) gone = true;
          if (still && towerSaid("cleared for takeoff")) clearedOntoIt = true;
        }
        const bool longHold = gone && towerSaid("cleared for takeoff") && !clearedOntoIt;
        if (!longHold) { printf("  long hold: gone %d, cleared %d, onto it %d, phase %d, trafficT %.0f\n", gone, towerSaid("cleared for takeoff"), clearedOntoIt, g.atcF.phase, g.atcF.trafficT); for (auto& x : g.atc.history) printf("   voice: %s\n", x.c_str()); }
        g.set.traffic = false; g.traffic.craft.clear();
        printf("Runway occupancy after clearance: late incursion takes the landing clearance back %d, a stuck runway is cleared before the departure %d: %s\n",
               late, longHold, late && longHold ? "ok" : "FAIL"); fails += !(late && longHold);
        g.endFlight(false, "test", OUT_ABANDONED); g.screen = SCR_HUB;
      }
      {   // FLT-5 (the v3.44.0 review): an autoland to a GPS alternate talks to the alternate's tower; the contract keeps its own
        g.career.location = g_world.findAirport("ORC");
        g.startFlight(c, 1, Career::SRC_RENT);
        const int alt = g_world.findAirport("CAP");
        const Airport& B = g_world.airports[alt];
        vec3 p0 = B.threshold(false) - B.dir() * 6000.f; p0.y = B.elev + 400.f;
        g.plane.reset(&kAircraft[1], p0, B.heading, 60, 150, true, kAircraft[1].cruise * 0.8f);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 4; g.atcF.airborne = true; g.atc.history.clear();
        g.traffic.craft.clear(); g.set.traffic = false;
        g.update(dt);
        g.apDest = alt; g.engageAutopilot();
        for (int i = 0; i < 10; i++) g.update(dt);
        const bool ok = c.to != alt && g.plane.apOn && g.atcF.arr == alt && g.atcF.phase >= 3 && g.contract.to == c.to;
        printf("Tower for a GPS alternate: arrival tower %s (contract %s), phase %d: %s\n", g_world.airports[g.atcF.arr].code, g_world.airports[g.contract.to].code, g.atcF.phase, ok ? "ok" : "FAIL"); fails += !ok;
        g.endFlight(false, "test", OUT_ABANDONED); g.screen = SCR_HUB;
      }
      {   // AUD-1..4 (the v3.44.0 review): the radio follows the master volume; a pause mid-call brings the music back up; a
          // hazard call is dropped once the hazard has passed; a cut-off or empty voice clip is refused, not overread
        Game q; q.initHeadless(); q.startFlight(g_story[0], 0, Career::SRC_LESSON);
        q.set.master = 0.5f; q.set.radioVol = 0.8f; q.voiceDuck = 0.f; q.feedAudio();
        const bool master = fabsf(q.radio.volume() - 0.4f) < 0.01f;
        q.voiceDuck = 1.f; q.paused = true;
        for (int i = 0; i < 240; i++) { q.updateComms(1.f / 60.f); q.feedAudio(); }
        const bool unducked = q.voiceDuck < 0.01f && fabsf(q.radio.volume() - 0.4f) < 0.01f;
        q.paused = false;
        AtcVoice::Tx w; w.key = Game::kWarnKey + 1;
        q.warnWas[1] = true; const bool holds = q.atc.valid(w);
        q.warnWas[1] = false; const bool passed = !q.atc.valid(w);
        namespace fs = std::filesystem;
        const fs::path vd = fs::temp_directory_path() / "solace_bad_voice"; fs::create_directories(vd);
        { std::ofstream ix(vd / "voice_index.txt"); ix << "bad.trunc\ttrunc.wav\tTruncated.\nbad.empty\tempty.wav\tEmpty.\n"; }
        { std::ofstream f(vd / "trunc.wav", std::ios::binary); f.write("RIFF\x24\0\0\0WAVEfmt \x10\0\0\0\x01\0", 22); }   // (the fmt chunk cut off)
        { std::ofstream f(vd / "empty.wav", std::ios::binary); const char h[] = "RIFF\x24\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x40\x1f\0\0\x80\x3e\0\0\x02\0\x10\0data\0\0\0\0"; f.write(h, sizeof h - 1); }
        AtcVoice v; const bool refused = v.load(vd.string()) && !v.decodes("bad.trunc") && !v.decodes("bad.empty");
        std::error_code ec; fs::remove_all(vd, ec);
        const bool ok = master && unducked && holds && passed && refused;
        printf("Audio: radio at master x radio %d, unducked after a pause %d, hazard call kept while it holds %d and dropped once passed %d, bad clips refused %d: %s\n",
               master, unducked, holds, passed, refused, ok ? "ok" : "FAIL"); fails += !ok;
      }
      {   // WLD-1..3 (the v3.44.0 review): a plasma crater is dug into the ground the aircraft and the wreckage meet; what eight
          // bombs flattened stays flattened; a wreck on a chunk seam is the business of the chunks either side of it
        Game q; q.initHeadless(); q.botControl = true; q.set.traffic = false;
        q.resCraft = kWraith; q.resAirborne = false; q.launchResearch();
        const Airport& a = g_world.airports[q.contract.from];
        vec3 p0 = a.pos() + vec3(-a.dir().z, 0, a.dir().x) * 600.f; p0.y = g_world.height(p0.x, p0.z);
        const float g0 = p0.y;
        q.detonate(p0, false); q.refreshGroundPits();
        const bool dug = q.wreckGround(p0.x, p0.z) < g0 - 7.f;
        // the aircraft set down at the crater's floor rests there (on the old ground it was 8 m under it, and thrown up)
        Plane& pl = q.plane;
        pl.reset(pl.spec, vec3(p0.x, 0.f, p0.z), a.heading, pl.spec->maxFuel, 85.f, false, 0.f);
        pl.pos.y = q.wreckGround(p0.x, p0.z) + pl.gearHeight() + 0.05f;   // (reset stands it on the islands as built)
        pl.ctl.brake = 1; float rise = 0;
        for (int i = 0; i < 120; i++) { pl.step(1.f / 60.f, q.wx, 0.f); rise = std::max(rise, pl.pos.y - (q.wreckGround(p0.x, p0.z) + pl.gearHeight())); }
        const bool rests = rise < 1.f && !pl.ev.crashed;
        // eight bombs: the first one's flattened circle is still there
        for (int k = 1; k < 8; k++) { vec3 pk = p0 + vec3(300.f * k, 0, 0); pk.y = g_world.height(pk.x, pk.z); q.detonate(pk, false); }
        q.refreshGroundPits();
        bool kept = false; for (const vec3& c : g_scenery.craters) if (fabsf(c.x - p0.x) < 1.f && fabsf(c.y - p0.z) < 1.f) kept = true;
        // a tree a few centimetres inside a chunk's west edge, destroyed: both chunks either side of the seam know
        const int cx = 150, cz = 150;
        Ent e{Scenery::chunkX0(cx) + 0.05f, 0.f, Scenery::chunkX0(cz) + 100.f, 0.f, 1.f, 1.f, 1.f, 0.37f};
        const bool gone = g_scenery.damage(e, 0, 100) && g_scenery.destroyed(e);
        const bool seam = gone && g_scenery.chunkAffected(cx, cz) && g_scenery.chunkAffected(cx - 1, cz);
        g_scenery.resetDamage(); q.wraith = Game::WraithState(); q.refreshGroundPits();
        const bool ok = dug && rests && kept && seam;
        printf("Craters and wrecks: crater dug into the ground %d, aircraft rests on its floor %d (rose %.1f m), the first bomb's damage kept after eight %d, a seam wreck in both chunks %d: %s\n",
               dug, rests, rise, kept, seam, ok ? "ok" : "FAIL"); fails += !ok;
      }
      {   // the map wraps (world.h WRAP_HALF): flown east over its seam, the aircraft comes in from the west with what is round
          // it - the camera, its smoke - and the flight goes on (past 48 km it used to be lost)
        Game q; q.initHeadless(); q.botControl = true; q.set.traffic = false;
        q.resCraft = 1; q.resAirborne = true; q.launchResearch();
        Plane& pl = q.plane;
        pl.reset(pl.spec, vec3(WRAP_HALF - 300.f, 900.f, 2000.f), 90.f, pl.spec->maxFuel * 0.5f, 85.f, true, pl.spec->cruise);
        pl.ctl.throttle = 0.7f;
        for (int i = 0; i < 30; i++) q.update(dt);   // (the camera behind it)
        q.spawn(pl.pos - pl.forward() * 20.f, vec3(), 60.f, 2.f, 0.f, vec3(0.6f, 0.6f, 0.6f), 0.5f, SPR_SMOKE);
        float camFar = 0.f, smokeFar = 0.f; bool crossed = false;
        for (int i = 0; i < 60 * 8 && q.screen == SCR_FLIGHT; i++) {
          q.update(dt);
          crossed = crossed || pl.pos.x < 0.f;
          camFar = std::max(camFar, length(q.camPos - pl.pos));
          if (!q.particles.empty()) smokeFar = std::max(smokeFar, length(q.particles.front().p - pl.pos));
        }
        const bool ok = crossed && q.screen == SCR_FLIGHT && !q.crashed && pl.seamShift.x == 0.f && camFar < 150.f && smokeFar < 1000.f;
        printf("Map seam: crossed %d, still flying %d, camera within %.0f m of the aircraft, its smoke within %.0f m: %s\n",
               crossed, q.screen == SCR_FLIGHT && !q.crashed, camFar, smokeFar, ok ? "ok" : "FAIL"); fails += !ok;
      }
      if (voices) {   // FLT-3 (the v3.44.0 review): the same, on the autopilot's autoland - the tower's go-around is flown (it
                      // landed anyway, and the player was fined $500 for it); and an aircraft that can't climb away lands unfined
        // (the load past which the Wren can't climb away at approach speed - the autopilot's own sense of it, apEnv)
        float unableLoad = 150;
        for (float kg = 150; kg <= 1500; kg += 50) {
          Plane q; q.reset(&kAircraft[1], start, a.heading, 60, kg, true, kAircraft[1].vref + 6); q.apEngage(Plane::AP_NAV, c.to, c.wx);
          q.step(dt, c.wx, 0.f); if (!q.apEnv.canGoAround) { unableLoad = kg; break; }
        }
        for (int unable = 0; unable < 2; unable++) {
          g.career.location = g_world.findAirport("ORC");
          Contract cw = c; cw.wx.windFrom = a.heading; cw.wx.windSpeed = 4; cw.wx.gust = 0;   // (down the runway: the tower's end and the autopilot's)
          g.startFlight(cw, 1, Career::SRC_RENT);
          g.plane.reset(&kAircraft[1], start, a.heading, 60, unable ? unableLoad : 150, true, kAircraft[1].vref + 6);   // (unable: loaded past climbing away)
          g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true; g.atc.history.clear();
          g.traffic.craft.clear(); g.set.traffic = true;
          g.apDest = c.to; g.engageAutopilot();
          g.plane.apRev = false; g.plane.apStage = Plane::APS_FINAL; g.plane.apStageT = 0;   // (established on the final it was placed on)
          bool down = false, called = false; float climbedTo = 0, tCall = -1;
          for (t = 0; t < 120 && g.screen == SCR_FLIGHT; t += dt) {
            rollout(c.to, 300.f);
            g.update(dt); audio(dt);
            if (!called && towerSaid("Go around")) { called = true; tCall = t; }
            if (g.plane.onGround) down = true;
            if (called) climbedTo = std::max(climbedTo, g.plane.agl());
            if (unable ? down : climbedTo > 150.f) break;
          }
          const bool fined = g.result.landedAgainstGoAround;
          g.set.traffic = false; g.traffic.craft.clear();
          bool ok = called && !fined && (unable ? down : (!down && climbedTo > 150.f));
          printf("Autoland, runway occupied%s: go-around called at %.0f s, %s, fined %d: %s\n", unable ? " (unable to climb away)" : "", tCall,
                 unable ? (down ? "landed" : "not down") : (down ? "landed" : fmt("climbed away to %.0f m", climbedTo).c_str()), fined, ok ? "ok" : "FAIL"); fails += !ok;
          g.endFlight(false, "test", OUT_ABANDONED); g.screen = SCR_HUB;
        }
      }
      g.set.traffic = trafficSet;
    }
    if (in(3)) {   // part 3: the parking brake on every type, the honest quote, the fuel estimates
      // ---- a lesson starts with the parking brake set: full throttle doesn't move the aircraft until it's released
      {
        g.botControl = false;
        for (int sp = 0; sp < kNumAircraft; sp++) {   // every career aircraft, from a free flight at Solace Capital
          Contract fc = g_story[0]; fc.forceAircraft = -1; fc.type = CT_FERRY; fc.from = fc.to = g_world.findAirport("CAP"); fc.wps.clear(); fc.hints.clear();
          g.startFlight(sp == 0 ? g_story[0] : fc, sp, sp == 0 ? Career::SRC_LESSON : Career::SRC_RENT);
          for (int i = 0; i < 60 * 8; i++) { g.plane.ctl.throttle = 0.4f; g.update(dt); }
          float held = length(vec3(g.plane.vel.x, 0.f, g.plane.vel.z));
          printf("Parking brake at the start (%s): running=%d brake=%.1f speed after 8 s at run-up power %.2f m/s\n", kAircraft[sp].name, g.plane.engineRunning, g.plane.ctl.brake, held);
          // set on every aircraft, and it holds run-up power on all of them (a fixed-pitch propeller at a run-up's 1700 rpm
          // takes about a third of its power). Full power it needn't: only the mains are braked, and the thrust line, a
          // metre and more over the wheels, and a high wing's slipstream take weight off them - a light type with power to
          // spare slides on its locked tyres, as a real one would
          if (!(g.plane.engineRunning && g.plane.ctl.brake > 0.99f && held < 0.02f)) fails++;   // (static friction: no creep)
        }
        g.botControl = true;
      }
      // ---- E1: the quote is honest. A4 (the Minister's Jet, an owned Starling) flown on the autopilot after a scripted
      // takeoff lands within the quoted time +- its uncertainty; the job card's plan is the one the flight settles with
      {
        Career keep = g.career;
        // (with no traffic: it measures the quote, and an AI aircraft in its path, as the game's traffic stood after the
        // flights before it, ended it in a mid-air collision when the flights before it were in another part)
        const bool trafficWas = g.set.traffic; g.set.traffic = false;
        int a4 = -1; for (size_t i = 0; i < g_story.size(); i++) if (g_story[i].id == "A4") a4 = (int)i;
        const Contract& c = g_story[a4];
        int spec = -1; for (int i = 0; i < kNumAircraft; i++) if (std::string(kAircraft[i].id) == "starling") spec = i;
        g.career.license = LIC_ATP; g.career.location = c.from; g.career.money = 500000;
        g.career.fleet.clear(); g.career.fleet.push_back({spec, c.from, kAircraft[spec].maxFuel, 0.f});
        Career::LaunchPlan pl = g.career.plan(c, spec, Career::SRC_OWNED);
        float quick = pl.minutesEst, simFuel = -1, sim = simulateFlightMinutes(c, spec, &simFuel);
        g.career.useFlownTime(pl, c, sim, simFuel);
        g.startFlight(c, spec, Career::SRC_OWNED);
        const AircraftSpec& s = kAircraft[spec];
        Plane& p = g.plane; bool ap = false;
        for (int k = 0; k < 30 * 60 * 40 && g.screen == SCR_FLIGHT; k++) {
          if (!ap) {
            p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = 0.15f;
            p.ctl.pitch = p.ias > s.vref * 0.95f ? clampf(0.08f * (10.f - p.pitchDeg()), -1, 1) : 0.f;
            if (p.agl() > 150.f) { p.ctl.flaps = 0; p.ctl.gearDown = false; p.apEngage(Plane::AP_NAV, c.to, g.wx); p.apComfort = true; ap = true; }
          }
          g.update(1.f / 30.f);
        }
        float flown = g.flightClock / 60.f;
        bool ok = g.result.success && pl.flown && fabsf(flown - pl.minutesEst) <= pl.minutesSigma && g.launchPlan.fees() == pl.fees();
        printf("   A4 quick estimate %.1f min (en route %.1f, climb %.1f, approach %.1f); flown in the background %.1f min\n", quick, pl.tCruise, pl.tClimb, pl.tApproach, sim);
        printf("A4 on the autopilot: flown %.1f min, quoted %.1f +- %.1f min (limit %.0f), success %d: %s\n", flown, pl.minutesEst, pl.minutesSigma, c.timeLimitMin,
               g.result.success, ok ? "ok" : "FAIL");
        fails += !ok;
        g.career = keep; g.set.traffic = trafficWas;
      }
      // ---- the quick fuel estimate covers what the flight burns without overstating it: the PPL checkride in the
      // Kestrel (quoted 78 kg for its 70 kg tanks - "expect to run dry" - when it burns 43), and legs in a single, a
      // turboprop and the jet, each flown on the autopilot in the background as a job quote is. (Legs the autopilot flies
      // in about the time it planned: when it goes around or circles longer than planned - the A4 leg, 11 minutes planned
      // and 17 flown - the burn per minute still matches, the minutes don't)
      {
        int ok = 0, n = 0;
        for (auto [id, specId] : std::vector<std::pair<const char*, const char*>>{{"L4", "kestrel"}, {"C2", "wren"}, {"C7", "pelican"}, {"C2", "starling"}}) {
          int si = -1; for (int i = 0; i < kNumAircraft; i++) if (std::string(kAircraft[i].id) == specId) si = i;
          const Contract* c = nullptr; for (auto& k : g_story) if (k.id == id) c = &k;
          if (si < 0 || !c) { printf("   fuel estimate: %s / %s not found\n", id, specId); n++; continue; }
          Career::LaunchPlan pl = g.career.plan(*c, si, Career::SRC_RENT);
          float burn = -1, m = simulateFlightMinutes(*c, si, &burn);
          const float r = burn > 0 ? pl.fuelKgEst / burn : -1.f;
          const bool good = m > 0 && r >= 0.95f && r <= 1.5f && pl.fuelKgEst <= kAircraft[si].maxFuel;
          printf("   fuel estimate %s in the %s: quoted %.1f kg, burned %.1f kg on the autopilot (x%.2f), tanks %.0f kg%s\n", id, kAircraft[si].name, pl.fuelKgEst, burn, r, kAircraft[si].maxFuel, good ? "" : "  <-");
          ok += good; n++;
        }
        printf("Quick fuel estimate: %d of %d legs quoted at 0.95-1.5x the burn and inside the tanks: %s\n", ok, n, ok == n ? "ok" : "FAIL"); fails += ok != n;
      }
    }
    if (in(4)) {   // part 4: hints, inputs, careers, jobs, trials, the airline, the tools, the review checks
      // ---- E1: lesson hints name the keys bound now, and no recording names a key the player rebound; the ATC history
      // stays bounded over long sessions
      {
        const std::string raw = g_story[0].hints[0];
        std::string def = g.expandHint(raw);
        int keep = g.set.keyBind[ACT_PARK]; g.set.keyBind[ACT_PARK] = 'P';
        std::string reb = g.expandHint(raw);
        g.set.keyBind[ACT_PARK] = keep;
        AtcVoice::Tx tx;
        bool defVoiced = !voices || g.atc.resolveLesson("L1", 0, def, tx);
        bool rebVoiced = !voices || g.atc.resolveLesson("L1", 0, reb, tx);
        bool ok = def.find("Press B ") != std::string::npos && reb.find("Press P ") != std::string::npos && reb.find("Press B ") == std::string::npos && rebVoiced && defVoiced;
        printf("Hint after rebinding the parking brake: \"%s\" voiced %d (default voiced %d): %s\n", reb.c_str(), rebVoiced, defVoiced, ok ? "ok" : "FAIL");
        fails += !ok;
        // with a gamepad the hint names its buttons as bound (D-pad Left parks, B is the flaps) and RT for the throttle,
        // the GPS names the autopilot's binding, and the spoken line is control-independent
        std::string padT = g.expandHint(raw, true);
        bool padVoiced = !voices || g.atc.resolveLesson("L1", 0, padT, tx);
        int keepAp = g.set.keyBind[ACT_AP]; g.set.keyBind[ACT_AP] = 'P';
        std::string apKey = g.actLabel(ACT_AP, false), apPad = g.actLabel(ACT_AP, true);
        g.set.keyBind[ACT_AP] = keepAp;
        ok = padT.find("D-PAD LEFT") != std::string::npos && padT.find("hold RT") != std::string::npos && padT.find("Press B ") == std::string::npos &&
             padT.find("SHIFT") == std::string::npos && padT.find("(or gamepad") == std::string::npos && padVoiced && apKey == "P" && apPad == "RS CLICK";
        printf("Hint with a gamepad: \"%s\" voiced %d; autopilot named %s / %s: %s\n", padT.c_str(), padVoiced, apKey.c_str(), apPad.c_str(), ok ? "ok" : "FAIL");
        fails += !ok;
        printf("ATC history after the flights so far: %zu lines (limit %zu): %s\n", g.atc.history.size(), g.atc.historyLimit, g.atc.history.size() <= 64 ? "ok" : "FAIL");
        fails += g.atc.history.size() > 64;
      }
      // ---- E5: input contexts and re-arming. A held from the menu into the flight doesn't brake until it's released
      // and pressed again; A held across leaving the pause menu doesn't either; LB+RB doesn't hide the UI while the
      // XR-40's weapons are armed; losing the controller in flight pauses it
      {
        g.botControl = false;
        Contract fc = g_story[0]; fc.forceAircraft = -1; fc.type = CT_FERRY; fc.from = fc.to = g_world.findAirport("CAP"); fc.wps.clear(); fc.hints.clear();
        auto pad = [&](unsigned held, unsigned pressed) { g.in.pad = true; g.in.buttons = held; g.in.buttonsPressed = pressed; };
        g.screen = SCR_HUB; g.paused = false;
        pad(PAD_A, PAD_A); g.update(dt); pad(PAD_A, 0); g.update(dt);   // A pressed on a menu, still held
        g.startFlight(fc, 0, Career::SRC_RENT); g.parkingBrake = false;
        float held = 0; for (int i = 0; i < 30; i++) { pad(PAD_A, 0); g.update(dt); held = std::max(held, g.plane.ctl.brake); }
        pad(0, 0); g.update(dt);
        pad(PAD_A, PAD_A); g.update(dt); float again = g.plane.ctl.brake;
        bool ok1 = held == 0.f && again > 0.5f;
        printf("A held from the menu into the flight: brake %.1f while held, %.1f when pressed again: %s\n", held, again, ok1 ? "ok" : "FAIL"); fails += !ok1;
        pad(0, 0); g.update(dt);
        g.paused = true; pad(0, 0); g.update(dt); pad(PAD_A, PAD_A); g.update(dt);   // A pressed in the pause menu...
        g.paused = false; float leak = 0; for (int i = 0; i < 10; i++) { pad(PAD_A, 0); g.update(dt); leak = std::max(leak, g.plane.ctl.brake); }   // ...held after resuming
        bool ok2 = leak == 0.f;
        printf("A held across leaving the pause menu: brake %.1f: %s\n", leak, ok2 ? "ok" : "FAIL"); fails += !ok2;
        pad(0, 0); g.update(dt);
        g.in.pad = false; g.update(dt);
        bool ok3 = g.paused;
        printf("Controller lost in flight: paused %d: %s\n", g.paused, ok3 ? "ok" : "FAIL"); fails += !ok3;
        g.paused = false;
        // aerobatics: one figure, then the aircraft is the pilot's again (the autopilot off, as it was); any flight input
        // during a figure - a throttle key here - hands it back at once; begun from the autopilot's route, the route resumes
        {
          g.in = Input();
          const int cap = g_world.findAirport("CAP");
          auto airborne = [&] {
            g.startFlight(fc, 1, Career::SRC_RENT); g.parkingBrake = false;
            g.plane.reset(&kAircraft[1], g_world.airports[cap].pos() + vec3(0, 1800, 0), 90, 60, 85, true, kAircraft[1].cruise * 0.9f);
            g.takeoffAnnounced = true; g.engineAutoStarted = true; g.plane.ctl.throttle = 0.7f;
          };
          auto press = [&](int act) { int k = g.set.keyBind[act]; g.in.down[k] = g.in.pressed[k] = true; g.update(dt); g.in.endFrame(); g.in.down[k] = false; };
          auto fly = [&](float secs) {   // (and the frame after it, which hands the aircraft back)
            for (int i = 0; i < (int)(secs / dt) && g.plane.apOn && g.plane.apMode == Plane::AP_STUNT && g.screen == SCR_FLIGHT; i++) { g.update(dt); g.in.endFrame(); }
            g.update(dt); g.in.endFrame();
          };
          airborne(); press(ACT_STUNT);
          const bool began = g.plane.apOn && g.plane.apMode == Plane::AP_STUNT;
          fly(240.f);
          const bool once = began && !g.plane.apOn && g.plane.apStuntAbort.empty() && g.screen == SCR_FLIGHT && !g.plane.ev.crashed;
          airborne(); press(ACT_STUNT); for (int i = 0; i < 60; i++) { g.update(dt); g.in.endFrame(); }
          const bool still = g.plane.apOn && g.plane.apMode == Plane::AP_STUNT;
          press(ACT_THR_UP);
          const bool taken = still && !g.plane.apOn;
          airborne(); const int keepDest = g.apDest; g.apDest = g_world.findAirport("MDB"); g.engageAutopilot(); press(ACT_STUNT);
          fly(240.f);
          const bool resumed = g.plane.apOn && g.plane.apMode == Plane::AP_APPR && g.plane.apAirport == g.apDest;
          g.apDest = keepDest; g.plane.apDisengage(); g.in = Input();
          printf("Aerobatics: %s flown once, then the pilot's %d; a throttle key mid-figure hands it back %d; from the autopilot's route, the route resumes %d: %s\n",
                 Plane::stuntName(0), once, taken, resumed, once && taken && resumed ? "ok" : "FAIL");
          fails += !(once && taken && resumed);
        }
        // the XR-40 armed: both bumpers held 1.5 s
        g.resCraft = kWraith; g.resAirborne = true; g.launchResearch(); g.wraith.armed = true;
        bool hid0 = g.uiHidden;
        for (int i = 0; i < 90; i++) { pad(PAD_LB | PAD_RB, i == 0 ? (PAD_LB | PAD_RB) : 0); g.update(dt); }
        bool ok4 = g.uiHidden == hid0;
        printf("LB+RB held with the XR-40 armed: UI hidden %d -> %d: %s\n", hid0, g.uiHidden, ok4 ? "ok" : "FAIL"); fails += !ok4;
        pad(0, 0); g.in.pad = false; g.update(dt); g.paused = false;
        g.botControl = true;
      }
      // ---- the pause menu's Restart keeps the flight's mode: a trial stays off the books (the review of v3.31.0, C1);
      // the debrief's retry never launches an aircraft the job can't be flown in (C2)
      {
        Game r; r.initHeadless(); r.botControl = true;
        Contract c = g_story[0]; c.id = "TRIALTEST";
        r.startFlight(c, 0, Career::SRC_LESSON); r.isolatedFlight = true;
        r.update(1.f / 60.f);
        r.restartFlight();
        bool ok1 = r.isolatedFlight;
        printf("Restart of a trial: still off the books %d: %s\n", r.isolatedFlight, ok1 ? "ok" : "FAIL"); fails += !ok1;
        Game q; q.initHeadless(); q.botControl = true;
        q.contract = g_story[1]; q.specIdx = 5; q.source = Career::SRC_RENT;   // a Meridian on a student's lesson: not allowed
        q.screen = SCR_DEBRIEF; const int att = q.career.attempt;
        q.retryFromDebrief();
        bool ok2 = q.screen == SCR_HUB && q.career.attempt == att && !q.career.attemptOpen;
        printf("Debrief retry in an aircraft the job can't use: screen %d, attempt %d -> %d (%s): %s\n", q.screen, att, q.career.attempt, q.hubMsg.c_str(), ok2 ? "ok" : "FAIL"); fails += !ok2;
      }
      // ---- a start in the air: gear stowed and every brake off, a research sortie's too (startFlight parks it first)
      {
        Game r; r.initHeadless(); r.botControl = true;
        bool ok = true;
        for (int craft : {kResearchJet, kWraith}) {
          r.resCraft = craft; r.resAirborne = true; r.launchResearch();
          r.update(1.f / 60.f);
          bool good = !r.parkingBrake && r.plane.ctl.brake == 0.f && !r.plane.onGround && (!r.plane.spec->retract || (!r.plane.ctl.gearDown && r.plane.gear < 0.01f));
          printf("Airborne research start (%s): parking brake %d, brake %.1f, gear %.2f lever %s: %s\n", r.plane.spec->name, r.parkingBrake, r.plane.ctl.brake, r.plane.gear, r.plane.ctl.gearDown ? "down" : "up", good ? "ok" : "FAIL");
          ok = ok && good;
        }
        fails += !ok;
      }
      // ---- a research runway start takes its own fuel, not the job card's choice, which stays for the career (the
      //      review of v3.44.0, CAR-7: a 7 kg Kestrel selection launched the XR-10 and XR-20 with 7 kg)
      {
        Game r; r.initHeadless(); r.botControl = true; r.set.traffic = false;
        bool ok = true;
        for (int craft : {kNightjar, kMantis}) {
          r.resCraft = craft; r.resAirborne = false; r.launchFuelKg = -1; r.launchResearch();
          const float own = r.plane.fuel;
          r.launchFuelKg = 7.f; r.launchResearch();
          const bool good = own > 50.f && fabsf(r.plane.fuel - own) < 0.5f && r.launchFuelKg == 7.f;
          printf("Research runway start (%s): %.0f kg, %.0f kg with a 7 kg career choice, choice kept %d: %s\n", r.plane.spec->name, own, r.plane.fuel, r.launchFuelKg == 7.f, good ? "ok" : "FAIL");
          ok = ok && good;
        }
        fails += !ok;
      }
      // ---- low frame rates keep simulated time: 5 s of 5 fps frames is 5 s of flight
      {
        g.startFlight(g_story[0], 0, Career::SRC_LESSON);
        float c0 = g.flightClock;
        for (int i = 0; i < 25; i++) g.update(0.2f);
        float got = g.flightClock - c0;
        printf("5 fps timing: %.2f s simulated for 5.00 s of frames\n", got);
        if (fabsf(got - 5.f) > 0.05f) fails++;
      }
      // ---- stopped dry with checkpoints left ends the flight (was a stall) and isn't a crash
      {
        g.startFlight(g_story[0], 0, Career::SRC_LESSON);
        int crashes0 = g.career.crashes;
        g.takeoffAnnounced = true; g.touchedDown = true; g.wpIndex = 0; g.plane.fuel = 0;
        for (int i = 0; i < 600 && g.screen == SCR_FLIGHT; i++) g.update(1.f / 60.f);
        printf("Dry with checkpoints left: screen=%d outcome=%d crashes %+d (%s)\n", g.screen, (int)g.result.outcome, g.career.crashes - crashes0, g.debriefTitle.c_str());
        if (g.screen != SCR_DEBRIEF || g.result.outcome != OUT_OUT_OF_FUEL || g.career.crashes != crashes0) fails++;
      }
      // ---- transactions: a settlement that can't be saved leaves the career untouched and pending; the retry saves it
      //      once and a second retry can't pay again
      {
        Game& q = g;
        q.career.newGame(); q.career.money = 5000; q.career.license = LIC_PPL; q.career.location = g_story[4].from;
        q.pendingCareer.reset();
        q.saveDir = "no_such_dir_gameplay/x";   // (a save folder that can't be written)
        Contract c = g_story[4]; c.wx = Weather();
        const int scr0 = q.screen;
        q.beginCareerFlight(c, 1, Career::SRC_RENT);
        bool flagged = !q.career.attemptOpen && !q.commitBlocked() && q.screen == scr0;   // the attempt marker couldn't be saved: nothing flies, nothing pends
        q.saveDir.clear(); q.beginCareerFlight(c, 1, Career::SRC_RENT); q.saveDir = "no_such_dir_gameplay/x";   // (launched; the settlement's save then fails)
        q.plane.pos = g_world.airports[c.to].pos() + vec3(0, 0.1f, 0); q.plane.onGround = true; q.touchedDown = true; q.takeoffAnnounced = true;
        int before = q.career.money;
        q.endFlight(true, "", OUT_SUCCESS);
        bool pend = q.commitBlocked() && q.career.money == before && q.screen == SCR_DEBRIEF && !q.payout.empty();
        q.saveDir = ".";
        bool saved = q.retryCommit() && !q.commitBlocked();
        int after = q.career.money;
        bool once = q.retryCommit() && q.career.money == after && after != before && !q.career.attemptOpen;
        remove("career.sav"); remove("career.sav.bak");
        q.saveDir.clear(); q.pendingCareer.reset();
        printf("Transactions: marker pending %d, settlement pending %d, retry saved %d, no double pay %d: %s\n", flagged, pend, saved, once, flagged && pend && saved && once ? "ok" : "FAIL");
        fails += !(flagged && pend && saved && once);
      }
      // ---- resumable jobs: a diversion closes a leg (the hire charged once, no payment, no reputation loss), the job
      //      survives a save and reload, the next leg continues from the diversion airport and the delivery pays once;
      //      a release leaves the load where it is; a crash ends the job with repairs
      {
        Game& q = g;
        int from = g_world.findAirport("ORC"), via = g_world.findAirport("CAP"), to = g_world.findAirport("MDB");
        Contract c; c.id = "JOBTEST"; c.title = "Test freight"; c.type = CT_CARGO; c.from = from; c.to = to; c.cargoKg = 40; c.payout = 1000; c.minLicense = LIC_PPL; c.wx = Weather();
        auto fresh = [&] { q.pendingCareer.reset(); q.career.newGame(); q.career.money = 5000; q.career.license = LIC_PPL; q.career.location = from; q.career.reputation = 5; q.saveDir = "."; };
        auto landAt = [&](int ap) { q.plane.pos = g_world.airports[ap].pos() + vec3(0, 0.1f, 0); q.plane.onGround = true; q.touchedDown = true; q.takeoffAnnounced = true; q.plane.vel = vec3(0, 0, 0); };
        const int hire = (int)kAircraft[1].rentFee;
        fresh();
        q.beginCareerFlight(c, 1, Career::SRC_RENT);
        bool accepted = q.career.job && q.career.job->state == Career::JobState::ACTIVE && q.career.job->at == from;
        int m0 = q.career.money;
        q.plane.maxG = 1.6f; landAt(via); q.result.divertedTo = via;
        q.endFlight(false, "Diverted to Solace Capital", OUT_DIVERTED);
        bool leg1 = q.career.job && q.career.job->state == Career::JobState::RECOVERY && q.career.job->at == via && q.career.job->legs == 1
                    && q.career.location == via && q.career.money == m0 - hire && q.career.reputation == 5 && q.career.job->maxG >= 1.6f && q.career.job->hirePaid;
        // quit and reload: the job comes back whole
        Career r; r.newGame(); bool reloaded = r.load("career.sav") && r.job && r.job->state == Career::JobState::RECOVERY && r.job->at == via && r.job->c.id == "JOBTEST" && r.job->c.payout == 1000 && r.job->legs == 1;
        q.career = r;
        q.continueJob(1, Career::SRC_RENT);
        bool leg2start = q.career.job && q.career.job->state == Career::JobState::ACTIVE && q.contract.from == via && q.launchPlan.hire == 0 && q.screen != SCR_HUB;
        int m1 = q.career.money;
        landAt(to);
        q.endFlight(true, "", OUT_SUCCESS);
        bool delivered = !q.career.job && q.career.location == to && q.career.money >= m1 + 1000 && q.career.money <= m1 + 1200 && q.career.flights == 2 && q.lastSuccess;
        bool paidOnce = true; int pays = 0; for (auto& l : q.payout) if (l.label == "Contract payment") pays++; paidOnce = pays == 1;
        // release: the load stays at the diversion airport, nothing charged
        fresh(); q.beginCareerFlight(c, 1, Career::SRC_RENT); landAt(via); q.result.divertedTo = via; q.endFlight(false, "Diverted", OUT_DIVERTED);
        int m2 = q.career.money; q.releaseJob();
        bool released = !q.career.job && q.career.money == m2 && q.career.location == via;
        // a crash ends the job: repairs (the deductible on a rental) and a crash on the record
        fresh(); q.beginCareerFlight(c, 1, Career::SRC_RENT); q.crashed = true; q.endFlight(false, "Crashed", OUT_CRASHED);
        bool crashEnds = !q.career.job && q.career.crashes == 1;
        // QA C3 / C4 / C6: the leg carries its checkpoints, the patient's state and charges the ferry once
        Contract mv = c; mv.id = "MEDTEST"; mv.type = CT_MEDEVAC; mv.payout = 2000; mv.wps.push_back({g_world.airports[via].x, g_world.airports[via].z, 500.f}); mv.wps.push_back({g_world.airports[to].x, g_world.airports[to].z, 500.f});
        fresh(); q.career.fleet.push_back({1, to, kAircraft[1].maxFuel, 1.f});   // (an owned Wren parked at the destination: a ferry to the departure)
        q.beginCareerFlight(mv, 1, Career::SRC_OWNED);
        int ferryQuote = q.launchPlan.ferry;
        q.wpIndex = 1; q.result.wpDone = 1; q.result.patient = 0.16f;
        int m3 = q.career.money; landAt(via); q.result.divertedTo = via; q.endFlight(false, "Diverted", OUT_DIVERTED);
        bool carried = q.career.job && q.career.job->wpDone == 1 && fabsf(q.career.job->patient - 0.16f) < 1e-3f && q.career.job->ferryPaid;
        int ferryLines = 0; for (auto& l : q.payout) if (l.label.rfind("Ferry", 0) == 0) ferryLines++;
        bool ferryOnce = ferryQuote > 0 && ferryLines == 1 && q.career.money <= m3 - ferryQuote + 1;
        q.continueJob(1, Career::SRC_OWNED);
        bool resumed = q.career.job && q.wpIndex == 1 && fabsf(q.result.patient - 0.16f) < 1e-3f && q.launchPlan.ferry == 0;
        landAt(to); q.endFlight(true, "", OUT_SUCCESS);
        bool noBonus = true; for (auto& l : q.payout) { if (l.label.rfind("Ferry", 0) == 0) ferryLines++; if (l.label.find("good shape") != std::string::npos) noBonus = false; }
        bool legState = carried && ferryOnce && resumed && noBonus && ferryLines == 1;
        if (!legState) printf("  leg state: carried %d (wp %d patient %.2f ferryPaid %d) ferryOnce %d (quote %d lines %d) resumed %d noBonus %d\n", carried, q.career.job ? q.career.job->wpDone : -1, q.career.job ? q.career.job->patient : -1.f, q.career.job ? (int)q.career.job->ferryPaid : -1, ferryOnce, ferryQuote, ferryLines, resumed, noBonus);
        // QA C2: a session that ends inside a leg leaves the job waiting where the leg began (RECOVERY), not stranded ACTIVE
        fresh(); q.beginCareerFlight(c, 1, Career::SRC_RENT);
        { Career r2; r2.newGame(); bool okl = r2.load("career.sav"); bool interrupted = okl && r2.attemptOpen && r2.job && r2.job->state == Career::JobState::ACTIVE;
          q.career = r2; q.pendingCareer.reset();
          q.commit([](Career& k) { k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });   // (what Game::init does on such a save)
          legState = legState && interrupted && q.career.job && q.career.job->state == Career::JobState::RECOVERY && q.career.job->at == from; }
        // a lesson is never a job
        fresh(); q.beginCareerFlight(g_story[0], 0, Career::SRC_LESSON); bool noJob = !q.career.job; q.endFlight(false, "x", OUT_ABANDONED);
        remove("career.sav"); remove("career.sav.bak"); q.saveDir.clear(); q.pendingCareer.reset(); q.career.newGame();
        bool ok = accepted && leg1 && reloaded && leg2start && delivered && paidOnce && released && crashEnds && noJob && legState;
        if (!leg1) printf("  leg1: job %d state %d at %d legs %d loc %d money %d (want %d) rep %d maxG %.2f hirePaid %d\n", (int)(bool)q.career.job, q.career.job ? (int)q.career.job->state : -1, q.career.job ? q.career.job->at : -1, q.career.job ? q.career.job->legs : -1, q.career.location, q.career.money, m0 - hire, q.career.reputation, q.career.job ? q.career.job->maxG : 0.f, q.career.job ? (int)q.career.job->hirePaid : 0);
        printf("Resumable job: accepted %d, leg closed %d, reloaded %d, continued %d, delivered %d, paid once %d, released %d, crash ends %d, lesson no job %d, leg state carried %d: %s\n",
               accepted, leg1, reloaded, leg2start, delivered, paidOnce, released, crashEnds, noJob, legState, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      // ---- scoring and compliance lines from the recorded arrival
      {
        Career c; c.newGame(); c.money = 100000; c.license = LIC_ATP; c.reputation = 10;
        int spec = 1; c.fleet.push_back({spec, g_story[4].from, kAircraft[spec].maxFuel, 0.f});
        auto has = [](const std::vector<PayoutLine>& L, const char* label) { for (auto& l : L) if (l.label == label) return true; return false; };
        FlightResult good; good.success = true; good.landed = true; good.touchdownFpm = 200; good.flightMin = 10; good.tdPastThrM = 300; good.rwyLenM = 1500; good.centerlineErr = 1.f;
        good.thrKt = kAircraft[spec].vref * MS_TO_KT + 4.f; good.thrAglM = 15.f; good.fuelLeftFrac = 0.4f; good.shutDownAtStand = true;
        int st = 0; Career t = c; auto L = t.settle(g_story[4], spec, Career::SRC_OWNED, good, &st);
        bool okGood = has(L, "Touchdown in the zone") && has(L, "On the centreline") && has(L, "Stable approach") && has(L, "Taxied clear and shut down") && has(L, "Fuel reserve kept") && t.reputation > c.reputation;
        FlightResult poor = good; poor.tdPastThrM = 900; poor.centerlineErr = 6.f; poor.thrKt = kAircraft[spec].vref * MS_TO_KT + 30.f; poor.fuelLeftFrac = 0.02f; poor.shutDownAtStand = false; poor.holdViolated = true; poor.landedAgainstGoAround = true;
        Career u = c; L = u.settle(g_story[4], spec, Career::SRC_OWNED, poor, &st);
        bool okPoor = has(L, "Floated past the midpoint") && !has(L, "On the centreline") && !has(L, "Stable approach") && has(L, "Landed on fumes") && has(L, "Took off against a hold instruction")
                      && has(L, "Landed against a go-around instruction") && u.reputation < c.reputation;
        printf("Scoring lines: airmanship rewarded %d, violations charged %d: %s\n", okGood, okPoor, okGood && okPoor ? "ok" : "FAIL");
        fails += !(okGood && okPoor);
      }
      // ---- fuel is bought at uplift for an owned aircraft and what is left stays in its tanks
      {
        Game& q = g;
        int from = g_world.findAirport("ORC"), to = g_world.findAirport("MDB");
        Contract c; c.id = "FUELTEST"; c.title = "Fuel test"; c.type = CT_CARGO; c.from = from; c.to = to; c.cargoKg = 40; c.payout = 1000; c.minLicense = LIC_PPL; c.wx = Weather();
        q.pendingCareer.reset(); q.career.newGame(); q.career.money = 50000; q.career.license = LIC_PPL; q.career.location = from; q.saveDir.clear();
        q.career.fleet.push_back({1, from, 20.f, 0.f});   // an owned Wren with 20 kg in the tanks
        q.launchFuelKg = -1;
        q.beginCareerFlight(c, 1, Career::SRC_OWNED);
        float fuel0 = q.plane.fuel, uplift = q.launchPlan.fuelUpliftKg; int cost = q.launchPlan.fuelCostEst;
        bool planned = fuel0 > 20.f && fabsf(uplift - (fuel0 - 20.f)) < 1.f && cost > 0 && fabsf(cost - uplift * q.career.fuelPrice(from, 1)) < 2.f;
        q.plane.fuel = fuel0 * 0.6f;   // (the flight burns 40%)
        q.plane.pos = g_world.airports[to].pos() + vec3(0, 0.1f, 0); q.plane.onGround = true; q.touchedDown = true; q.takeoffAnnounced = true;
        int m0 = q.career.money;
        q.endFlight(true, "", OUT_SUCCESS);
        bool billed = false; for (auto& l : q.payout) if (l.label.rfind("Fuel uplift", 0) == 0 && l.amount == -cost) billed = true;
        bool kept = !q.career.fleet.empty() && fabsf(q.career.fleet[0].fuel - fuel0 * 0.6f) < 0.5f;
        bool noConsumption = true; for (auto& l : q.payout) if (l.label == "Fuel") noConsumption = false;
        // choosing less fuel on the card: the arrows' value is what the tanks hold at take-off
        q.career.fleet[0].location = from; q.career.location = from; q.launchFuelKg = kAircraft[1].maxFuel * 0.5f;
        q.beginCareerFlight(c, 1, Career::SRC_OWNED);
        bool chosen = fabsf(q.plane.fuel - kAircraft[1].maxFuel * 0.5f) < 0.5f && q.launchFuelKg < 0;
        q.endFlight(false, "x", OUT_ABANDONED); q.career.newGame(); q.pendingCareer.reset();
        bool ok = planned && billed && kept && noConsumption && chosen;
        printf("Fuel planning: uplift quoted %d, billed as quoted %d, remainder kept %d, no consumption bill %d, chosen fuel loaded %d: %s (money %+d)\n", planned, billed, kept, noConsumption, chosen, ok ? "ok" : "FAIL", q.career.money - m0);
        fails += !ok;
      }
      // ---- settle: only crashes count as crashes and cost repairs
      {
        Career c; c.newGame(); c.money = 100000; c.license = LIC_ATP;
        int spec = 2; c.fleet.push_back({spec, g_story[4].from, kAircraft[spec].maxFuel, 0.f});
        const FlightOutcome outs[] = {OUT_ABANDONED, OUT_DIVERTED, OUT_OFF_AIRPORT, OUT_OUT_OF_FUEL, OUT_CRASHED};
        for (FlightOutcome o : outs) {
          Career t = c; FlightResult r; r.success = false; r.outcome = o; r.failReason = "x"; int st = 0;
          auto lines = t.settle(g_story[4], spec, Career::SRC_OWNED, r, &st);
          bool repaired = false; for (auto& l : lines) if (l.label == "Repairs") repaired = true;
          bool wantCrash = o == OUT_CRASHED;
          printf("  settle outcome %d: crashes %+d repairs %d\n", (int)o, t.crashes - c.crashes, repaired);
          if ((t.crashes != c.crashes) != wantCrash || repaired != wantCrash) fails++;
        }
      }
      // ---- engine-out landing (C7): the engine stops on a 2.8 km final in an owned Wren; the pilot glides it in. The
      // flight succeeds, the settlement pays the emergency bonus and charges the repair (or the insurance covers it),
      // and the aircraft's condition falls with the hours
      for (int ins = 0; ins < 2; ins++) {
        g.career.newGame(); g.pendingCareer.reset(); g.career.license = LIC_PPL; g.career.storyIndex = 4; g.career.location = g_world.findAirport("ORC");
        g.career.money = 50000; g.career.fleet.push_back({1, g.career.location, kAircraft[1].maxFuel, 1.f}); g.career.insured = ins == 1;
        int money0 = g.career.money;
        Contract c = g_story[4]; c.wx.windSpeed = 1; c.wx.gust = 0; c.wx.turbulence = 0.02f;
        g.startFlight(c, 1, Career::SRC_OWNED);
        const Airport& a = g_world.airports[c.to];
        vec3 dir = a.dir(), thr = a.threshold(false);
        vec3 start = thr - dir * 2800.f; start.y = a.elev + 300.f;   // (the glide from here reaches the runway's first half)
        g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref * 1.2f);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3;
        g.plane.ctl.throttle = 0.5f; g.update(dt);
        g.fireFailure(FAIL_ENGINE_TOTAL, 0);
        bool glide = g.plane.glideOnly() && !g.plane.engineRunning && (g.result.failureKinds & (1 << FAIL_ENGINE_TOTAL));
        s_pI = -2.f; s_eI = 0.f; float tdFpm = 0;
        for (t = 0; t < 240 && g.screen == SCR_FLIGHT; t += dt) {
          Plane& p = g.plane;
          vec3 rel = p.pos - thr;
          float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
          float agl = p.pos.y - a.elev;
          if (!p.onGround && !g.touchedDown) {
            float hdgT = a.heading - clampf(lat * 0.08f, -20, 20), herr = wrapAngle((hdgT - trackDeg(p)) * DEG) / DEG;
            p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
            // the glide: the speed decides the pitch (best glide, then Vref over the fence), flaps only when the field is made
            float vT = -along > 900.f ? p.spec->vref * 1.15f : p.spec->vref * 1.05f;
            if (-along < 700.f && agl < 120.f) { p.ctl.flaps = 1.f; g.flapNotch = 1.f; }
            float vsT = agl < 7.f ? -0.6f : clampf(-4.f + (vT - p.ias) * -0.6f, -8.f, -0.5f);
            pitchFor(p, vsT, dt);
            p.ctl.throttle = 1.f;   // (nothing answers)
            p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
          } else {   // (down: flaps up, the forward pressure eased off - let go at once it ballooned back into the air)
            p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = std::min(0.f, p.ctl.pitch + 0.4f * dt); p.ctl.roll = 0; p.ctl.yaw = 0; p.ctl.flaps = 0; g.flapNotch = 0;
          }
          g.update(dt);
          if (g.touchedDown && tdFpm == 0) tdFpm = g.touchdownFpm;
        }
        bool repair = false, covered = false, emergency = false; int premium = 0;
        for (auto& l : g.payout) { if (l.label.rfind("Repairs:", 0) == 0) repair = l.amount < 0; if (l.label.find("covered by insurance") != std::string::npos) covered = true; if (l.label == "Emergency handled") emergency = l.amount > 0; if (l.label == "Insurance premium") premium = -l.amount; }
        float cond = g.career.fleet.empty() ? -1.f : g.career.fleet[0].condition;
        bool ok = glide && g.screen == SCR_DEBRIEF && g.lastSuccess && g.career.location == c.to && emergency && cond > 0.f && cond < 1.f
                  && (ins ? covered && !repair && premium > 0 : repair && !covered && premium == 0);
        printf("Engine-out landing (%s): success=%d touchdown %.0f fpm, emergency bonus %d, repair %d, covered %d, premium %d, condition %.3f, money %+d %s\n",
               ins ? "insured" : "uninsured", g.lastSuccess, tdFpm, emergency, repair, covered, premium, cond, g.career.money - money0, ok ? "ok" : "FAIL");
        if (!ok) for (auto& l : g.payout) printf("   %-40s %d\n", l.label.c_str(), l.amount);
        fails += !ok;
      }
      {   // the roll: lessons never break; an owned aircraft's chance rises as its condition falls; a service restores it
        Career k; k.newGame(); k.license = LIC_CPL; k.money = 100000; k.fleet.push_back({1, k.location, kAircraft[1].maxFuel, 1.f});
        float pNew = k.failureChance(Career::SRC_OWNED, 1); k.fleet[0].condition = 0.3f; float pWorn = k.failureChance(Career::SRC_OWNED, 1);
        std::string m; bool sv = k.service(0, &m);
        bool ok = k.failureChance(Career::SRC_LESSON, 0) == 0.f && pNew > 0.f && pWorn > pNew * 1.5f && sv && k.fleet[0].condition == 1.f && k.money < 100000 && k.failureChance(Career::SRC_RENT, 1) > 0.f;
        printf("Failure chance: new %.3f, worn %.3f, lesson 0, serviced (%s) %s\n", pNew, pWorn, m.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- freelance job types (C6): the board posts each kind where the licence and the fields allow it, the special
      // kinds are well formed, each kind's scoring lines fire from the record, and a medevac flown gently delivers its
      // patient in good shape
      {
        int seen[CT_COUNT] = {}; bool wellFormed = true; int boards = 0;
        Career k; k.newGame(); k.license = LIC_CPL; k.money = 500000;
        for (int loc = 0; loc < (int)g_world.airports.size(); loc++) for (int seed = 0; seed < 12; seed++) {
          k.location = loc; k.boardSeed = 1000 + seed * 7 + loc; k.refreshBoard(); boards++;
          for (auto& c : k.board) {
            if (c.type < 0 || c.type >= CT_COUNT) { wellFormed = false; continue; }
            seen[c.type]++;
            const Airport& A = g_world.airports[c.from]; const Airport& B = g_world.airports[c.to];
            if (c.type == CT_MEDEVAC && !(B.hospital && c.timeLimitMin > 0 && c.pax == 1)) { printf("   bad medevac %s\n", c.title.c_str()); wellFormed = false; }
            if (c.type == CT_NIGHT && !(A.size >= 1 && B.size >= 1 && (c.wx.timeOfDay >= 21.f || c.wx.timeOfDay <= 5.f))) { printf("   bad night job %s tod %.1f\n", c.title.c_str(), c.wx.timeOfDay); wellFormed = false; }
            if (c.type == CT_IFR && !(c.wx.cloudCover > 0.9f && c.wx.visibility <= 3000.f && c.wx.cloudBase - B.elev >= 80.f && c.wx.cloudBase - B.elev <= 190.f)) { printf("   bad low-vis job %s base %.0f vis %.0f\n", c.title.c_str(), c.wx.cloudBase - B.elev, c.wx.visibility); wellFormed = false; }
            if (c.type == CT_SURVEY) { if (c.wps.size() != 6) wellFormed = false; for (auto& w : c.wps) if (w.alt < g_world.height(w.x, w.z) + 200.f || fabsf(w.alt - c.wps[0].alt) > 0.5f) { printf("   survey ring low or uneven %s\n", c.title.c_str()); wellFormed = false; } }
            if (c.type == CT_VIP && c.pax < 1) wellFormed = false;
          }
        }
        bool ok = wellFormed && seen[CT_MEDEVAC] > 0 && seen[CT_VIP] > 0 && seen[CT_NIGHT] > 0 && seen[CT_IFR] > 0 && seen[CT_SURVEY] > 0 && seen[CT_CARGO] > seen[CT_SURVEY];
        printf("Freelance job types over %d boards: cargo %d pax %d medevac %d vip %d night %d low-vis %d survey %d, well formed %d: %s\n", boards, seen[CT_CARGO], seen[CT_PAX], seen[CT_MEDEVAC], seen[CT_VIP], seen[CT_NIGHT], seen[CT_IFR], seen[CT_SURVEY], wellFormed, ok ? "ok" : "FAIL"); fails += !ok;
        // a PPL sees no medevac, VIP or low-vis work
        Career p; p.newGame(); p.license = LIC_PPL; int bad = 0;
        for (int seed = 0; seed < 30; seed++) { p.boardSeed = 50 + seed; p.refreshBoard(); for (auto& c : p.board) if (c.type == CT_MEDEVAC || c.type == CT_VIP || c.type == CT_IFR) bad++; }
        printf("PPL board keeps the CPL kinds off: %s\n", bad ? "FAIL" : "ok"); fails += bad > 0;
      }
      {   // scoring lines per type
        auto has = [](const std::vector<PayoutLine>& L, const char* sub) { for (auto& l : L) if (l.label.find(sub) != std::string::npos) return true; return false; };
        Career base; base.newGame(); base.license = LIC_ATP; base.money = 100000;
        Contract c = g_story[4]; c.story = false; c.payout = 2000; c.timeLimitMin = 0; c.pax = 1; c.cargoKg = 50;
        FlightResult good; good.success = true; good.landed = true; good.touchdownFpm = 150; good.flightMin = 10; good.tdPastThrM = 300; good.rwyLenM = 1500; good.centerlineErr = 1.f;
        int st = 0; bool ok = true; std::vector<PayoutLine> L;
        c.type = CT_MEDEVAC; { Career t = base; L = t.settle(c, 1, Career::SRC_RENT, good, &st); ok = ok && has(L, "good shape") && st == 3; }
        { FlightResult r = good; r.patient = 0.2f; Career t = base; L = t.settle(c, 1, Career::SRC_RENT, r, &st); ok = ok && has(L, "distress") && st <= 1; }
        c.type = CT_VIP; { Career t = base; L = t.settle(c, 1, Career::SRC_RENT, good, &st); ok = ok && has(L, "delighted"); }
        { FlightResult r = good; r.comfort = 0.3f; Career t = base; L = t.settle(c, 1, Career::SRC_RENT, r, &st); ok = ok && has(L, "displeased"); }
        c.type = CT_NIGHT; c.pax = 0; { FlightResult r = good; r.landingLightOn = false; Career t = base; L = t.settle(c, 1, Career::SRC_RENT, r, &st); ok = ok && has(L, "landing light"); }
        c.type = CT_IFR; { FlightResult r = good; r.belowMinimumsUnaligned = true; Career t = base; L = t.settle(c, 1, Career::SRC_RENT, r, &st); ok = ok && has(L, "below minimums"); }
        { Career t = base; L = t.settle(c, 1, Career::SRC_RENT, good, &st); ok = ok && has(L, "to minimums"); }
        c.type = CT_SURVEY; { FlightResult r = good; r.surveyInBand = 0.5f; Career t = base; L = t.settle(c, 1, Career::SRC_RENT, r, &st); ok = ok && has(L, "held 50%"); int amt = 0; for (auto& l : L) if (l.label.find("held 50%") != std::string::npos) amt = l.amount; ok = ok && amt == -2000 * 50 * 6 / 1000; }
        { Career t = base; L = t.settle(c, 1, Career::SRC_RENT, good, &st); ok = ok && has(L, "altitude held"); }
        printf("Job type scoring lines: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
        if (!ok) for (auto& l : L) printf("   %-40s %d\n", l.label.c_str(), l.amount);
      }
      {   // a medevac flown gently: the patient meter stays high and the bonus is paid; a hard touchdown costs it
        for (int rough = 0; rough < 2; rough++) {
          g.career.newGame(); g.pendingCareer.reset(); g.career.license = LIC_CPL; g.career.storyIndex = 4; g.career.location = g_world.findAirport("ORC");
          Contract c = g_story[4]; c.story = false; c.type = CT_MEDEVAC; c.pax = 1; c.cargoKg = 60; c.timeLimitMin = 0; c.title = "Medevac test";
          c.wx.windSpeed = 2; c.wx.gust = 0; c.wx.turbulence = 0.05f;
          g.startFlight(c, 1, Career::SRC_RENT);
          const Airport& a = g_world.airports[c.to];
          vec3 dir = a.dir(), thr = a.threshold(false);
          vec3 start = thr - dir * 3500.f; start.y = a.elev + 3500.f * tanf(3.f * DEG) + 15.f;
          g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
          g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3;
          g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f; s_eI = 0.f;
          for (t = 0; t < 300 && g.screen == SCR_FLIGHT; t += dt) {
            Plane& p = g.plane;
            vec3 rel = p.pos - thr;
            float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
            float agl = p.pos.y - a.elev;
            if (!p.onGround && !g.touchedDown) {
              float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
              float hdgT = a.heading - clampf(lat * 0.08f, -20, 20), herr = wrapAngle((hdgT - trackDeg(p)) * DEG) / DEG;
              p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
              float vsT = agl < 7.f ? -0.7f : clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1);
              if (rough && t > 2.f && t < 6.5f) p.ctl.roll = clampf((48.f - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);   // (a steep bank the patient feels)
              pitchFor(p, vsT, dt);
              p.ctl.throttle = agl < 6.f ? 0.f : clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
              p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
            } else {   // (down: flaps up, the forward pressure eased off - let go at once it ballooned back into the air)
              p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = std::min(0.f, p.ctl.pitch + 0.4f * dt); p.ctl.roll = 0; p.ctl.yaw = 0; p.ctl.flaps = 0; g.flapNotch = 0;
            }
            g.update(dt);
          }
          bool bonus = false, hit = false;
          for (auto& l : g.payout) { if (l.label.find("good shape") != std::string::npos) bonus = true; if (l.label.find("patient") != std::string::npos || l.label.find("Patient in") != std::string::npos) hit = true; }
          bool ok = g.screen == SCR_DEBRIEF && (rough ? g.result.patient < 0.9f : (g.lastSuccess && g.result.patient > 0.9f && bonus && !hit));
          printf("Medevac flight (%s): touchdown %.0f fpm, patient %.0f%%, bonus %d: %s\n", rough ? "steep bank" : "gentle", g.touchdownFpm, g.result.patient * 100.f, bonus, ok ? "ok" : "FAIL"); fails += !ok;
        }
      }
      // ---- dynamic weather (C8): the wind swings round over the flight; on the autopilot the approach is re-planned
      // for the other runway end while there is room, and the tower reads the current wind
      {
        g.career.newGame(); g.pendingCareer.reset(); g.career.license = LIC_CPL; g.career.location = g_world.findAirport("ORC");
        Contract c = g_story[4]; c.story = false; c.timeLimitMin = 0;
        const Airport& B = g_world.airports[c.to];
        c.wx.windFrom = B.heading; c.wx.windSpeed = 6.f; c.wx.gust = 0; c.wx.turbulence = 0.02f;   // on the nose for the first runway end
        c.wxShift = true; c.wxEnd = c.wx; c.wxEnd.windFrom = wrapDeg360(B.heading + 180.f); c.wxEnd.windSpeed = 9.f; c.wxEnd.precip = 1; c.wxEnd.cloudCover = 0.8f;
        g.startFlight(c, 1, Career::SRC_RENT);
        g.launchPlan.minutesEst = 2.f;   // (a short flight: the front arrives in about two minutes)
        vec3 start = B.pos() - B.dir() * 14000.f; start.y = B.elev + 900.f;
        g.plane.reset(&kAircraft[1], start, B.heading, 60, 150, true, kAircraft[1].cruise * 0.8f);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3;
        g.plane.apEngage(Plane::AP_NAV, c.to, g.wx);
        bool rev0 = g.plane.apRev;
        float wind0 = g.wx.windFrom; bool repicked = false, rained = false; float tRe = -1;
        for (t = 0; t < 300 && g.screen == SCR_FLIGHT; t += dt) {
          g.update(dt);
          if (g.plane.apRev != rev0 && !repicked) { repicked = true; tRe = t; }
          if (g.wx.precip == 1) rained = true;
        }
        float turned = fabsf(wrapAngle((g.wx.windFrom - wind0) * DEG)) / DEG;
        bool ok = !rev0 && repicked && turned > 150.f && rained && g.wx.windSpeed > 8.f;
        printf("Dynamic weather: wind turned %.0f deg, %.0f kt, rain %d, autopilot re-picked the runway at %.0f s (rev %d -> %d): %s\n", turned, g.wx.windSpeed * MS_TO_KT, rained, tRe, rev0, g.plane.apRev, ok ? "ok" : "FAIL"); fails += !ok;
        g.endFlight(false, "x", OUT_ABANDONED); g.career.newGame(); g.pendingCareer.reset();
      }
      // ---- trials (C12): the gate courses clear the ground, the spot landing and the STOL contest score the touchdown,
      // the board keeps the five best in order, and a trial never touches the career
      {
        g.career.newGame(); g.pendingCareer.reset(); g.career.location = g_world.findAirport("CAP");
        bool ok = true;
        for (int k : {Game::TR_GATES, Game::TR_DAILY}) {
          Contract c = g.trialContract(k);
          if (c.wps.size() != 8 || c.type != CT_TRIAL) ok = false;
          for (auto& w : c.wps) if (w.alt < g_world.height(w.x, w.z) + 100.f) { printf("   gate low at %.0f,%.0f\n", w.x, w.z); ok = false; }
        }
        Contract sp = g.trialContract(Game::TR_SPOT), st = g.trialContract(Game::TR_STOL);
        ok = ok && sp.from == g.career.location && st.from == g_world.findAirport("SMP") && sp.payout == 0;
        int money0 = g.career.money, flights0 = g.career.flights;
        g.startFlight(sp, 0, Career::SRC_LESSON); g.isolatedFlight = true;
        g.result.landed = true; g.result.tdPastThrM = 340.f; g.touchdownFpm = 160.f; g.touchedDown = true;
        g.endFlight(true, "", OUT_SUCCESS);
        auto& L = g.trialBest["T_SPOT"];
        bool scored = L.size() == 1 && fabsf(L[0] - (40.f + 40.f)) < 0.5f && g.screen == SCR_HUB && g.career.money == money0 && g.career.flights == flights0;
        g.startFlight(sp, 0, Career::SRC_LESSON); g.isolatedFlight = true; g.result.landed = true; g.result.tdPastThrM = 305.f; g.touchdownFpm = 100.f; g.touchedDown = true; g.endFlight(true, "", OUT_SUCCESS);
        bool sorted = L.size() == 2 && L[0] < L[1] && fabsf(L[0] - 30.f) < 0.5f;
        g.startFlight(st, 2, Career::SRC_RENT); g.isolatedFlight = true; g.result.landed = true; g.result.tdPastThrM = 60.f; g.result.stopLeftM = 350.f; g.result.rwyLenM = 600.f; g.touchedDown = true; g.endFlight(true, "", OUT_SUCCESS);
        bool roll = g.trialBest["T_STOL"].size() == 1 && fabsf(g.trialBest["T_STOL"][0] - 190.f) < 0.5f;
        g.startFlight(g.trialContract(Game::TR_GATES), 0, Career::SRC_LESSON); g.isolatedFlight = true; g.trialT0 = 10.f; g.trialT1 = 112.f; g.wpIndex = 8; g.result.landed = true; g.touchedDown = true; g.endFlight(true, "", OUT_SUCCESS);
        bool gates = g.trialBest["T_GATES"].size() == 1 && fabsf(g.trialBest["T_GATES"][0] - 102.f) < 0.01f && g.hubMsg.find("1:42") != std::string::npos;
        // the formation run: the score is the time out of a steady platform once the three minutes are up; cut short, no score
        Contract fr = g.trialContract(Game::TR_FORMATION);
        g.startFlight(fr, 0, Career::SRC_LESSON); g.isolatedFlight = true; g.formT = 180.f; g.formLost = 12.5f; g.result.landed = true; g.touchedDown = true; g.endFlight(true, "", OUT_SUCCESS);
        g.startFlight(fr, 0, Career::SRC_LESSON); g.isolatedFlight = true; g.formT = 90.f; g.formLost = 1.f; g.result.landed = true; g.touchedDown = true; g.endFlight(true, "", OUT_SUCCESS);
        bool form = fr.from == g.career.location && g.trialBest["T_FORM"].size() == 1 && fabsf(g.trialBest["T_FORM"][0] - 12.5f) < 0.01f && g.hubMsg.find("DNF") != std::string::npos;
        // QA S6: the settings file is read twice at startup (once early for fullscreen): the trial boards stay the file's
        std::string sd = g.saveDir; g.saveDir = "."; g.settingsWritten.clear(); g.saveSettings();
        size_t nSpot = g.trialBest["T_SPOT"].size(); g.loadSettings(); g.loadSettings();
        bool twice = g.trialBest["T_SPOT"].size() == nSpot && g.trialBest["T_FORM"].size() == 1 && nSpot == 2;
        remove("./settings.cfg"); g.saveDir = sd; g.settingsWritten.clear();
        ok = ok && scored && sorted && roll && gates && form && twice;
        printf("Trials: courses %d, spot scored %d, board sorted %d, STOL roll %d, gate time %d, formation %d, settings read twice %d (%s): %s\n", ok || true, scored, sorted, roll, gates, form, twice, g.hubMsg.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
        g.trialBest.clear();
      }
      // ---- the airline (C11): with the ATP, a hired pilot flies an owned aircraft on a route, a leg (fares less fuel and
      // wages, the aircraft swapping ends and wearing) for each of its leg's minutes you fly; the aircraft can't be flown or
      // sold while on the route; recall frees it; a worn aircraft with a weak pilot has incidents over time
      {
        Career k; k.newGame(); k.license = LIC_ATP; k.money = 400000; k.location = g_world.findAirport("CAP");
        int spec = 5;   // the Q400: a real airliner
        k.fleet.push_back({spec, k.location, kAircraft[spec].maxFuel, 1.f});
        std::string m; bool noLic = false;
        { Career s = k; s.license = LIC_CPL; noLic = !s.hirePilot(s.pilotCandidates()[0], &m); }
        auto cands = k.pilotCandidates();
        bool hired = cands.size() == 3 && k.hirePilot(cands[0], &m);
        int pvi = g_world.findAirport("PVI"), cap = g_world.findAirport("CAP");
        bool assigned = k.assignRoute(0, pvi, 0, &m);
        Contract big; big.from = cap; big.to = pvi; big.type = CT_CARGO; big.cargoKg = 500;   // (a job the Q400 could fly)
        std::string why; bool blocked = k.canFly(big, spec, &why) == Career::SRC_NONE && why.find("route") != std::string::npos && !k.sell(0, &m);
        FlightResult r; r.success = true; r.landed = true; r.touchdownFpm = 200; r.flightMin = 10; int st = 0; int money0 = k.money;
        Contract c = g_story[4]; c.story = false; c.payout = 0;
        auto L = k.settle(c, 1, Career::SRC_RENT, r, &st);
        bool line = false; int airNet = 0; for (auto& l : L) if (l.label.find("Airline:") != std::string::npos) { line = true; airNet += l.amount; }
        bool ticked = line && k.airline.routes[0].flights == 1 && k.fleet[0].location == pvi && k.airline.routes[0].from == pvi && k.fleet[0].condition < 1.f && k.money - money0 == airNet + (L.empty() ? 0 : 0) - 0 + (k.money - money0 - airNet);
        ticked = line && k.airline.routes[0].flights == 1 && k.fleet[0].location == pvi && k.fleet[0].condition < 1.f;
        bool noAbort = true;   // (the review of v3.24.0, R3: flights abandoned before take-off, or off-field, fly no route)
        for (int i = 0; i < 3; i++) {
          FlightResult ab; ab.outcome = i == 2 ? OUT_OFF_AIRPORT : OUT_ABANDONED; ab.flightMin = i == 0 ? 0.f : 4.f; ab.landed = i == 2;
          const int f0 = k.airline.routes[0].flights; auto LA = k.settle(c, 1, Career::SRC_RENT, ab, &st);
          for (auto& l : LA) if (l.label.find("Airline:") != std::string::npos) noAbort = false;
          noAbort = noAbort && k.airline.routes[0].flights == f0;
        }
        ticked = ticked && noAbort;
        bool recalled = k.recallRoute(0, &m) && k.routeOf(0) < 0 && k.canFly(big, spec, &why) == Career::SRC_OWNED;
        // incidents: a rating-1 pilot in a worn aircraft, many days
        Career w = k; w.fleet[0].condition = 0.4f; Career::Pilot weak; weak.name = "T. Test"; weak.rating = 1; weak.wage = 120; w.hirePilot(weak, &m); int wp = (int)w.airline.pilots.size() - 1;
        bool reassigned = w.assignRoute(0, w.fleet[0].location == pvi ? cap : pvi, wp, &m);
        bool once = true;   // QA S5: a repair is its own line; the route line is the fares less fuel and wages (the repair not taken twice)
        for (int i = 0; i < 60; i++) {
          std::vector<PayoutLine> LL; int e0 = w.airline.routes[0].earned; w.flights++; w.boardSeed++; w.airlineTick(LL, w.routeLegMinutes(w.airline.routes[0]));
          for (auto& l : LL) if (l.label.find("route flight") != std::string::npos && l.amount != w.airline.routes[0].earned - e0) once = false;
        }
        bool incidents = reassigned && w.airline.incidents > 0 && w.airline.routes[0].flights == 60 && once;
        // CAR-2 (the v3.44.0 review): the airline flies while the player does - ten 90 s circuits back to the field they
        // left are 15 minutes of its flying, not ten legs of every route (they paid +$12,394 a circuit)
        Career c2 = w; c2.airline.routes[0].progressMin = 0.f; const int legs0 = c2.airline.routes[0].flights;
        Contract back; back.from = back.to = c2.location; back.type = CT_CARGO; back.payout = 0;
        for (int i = 0; i < 10; i++) {
          FlightResult cr; cr.outcome = OUT_DIVERTED; cr.landed = true; cr.flightMin = 1.5f; cr.divertedTo = c2.location; int s2 = 0;
          c2.settle(back, 1, Career::SRC_RENT, cr, &s2);
        }
        const float legMin = c2.routeLegMinutes(c2.airline.routes[0]);
        const int circuitLegs = c2.airline.routes[0].flights - legs0;
        bool paced = legMin > 5.f && circuitLegs == (int)floorf(15.f / legMin) && circuitLegs <= 2;
        bool ok = noLic && hired && assigned && blocked && ticked && recalled && incidents && paced;
        printf("Airline: licence gate %d, hired %d, assigned %d, aircraft locked %d, tick %d (net %d), recall %d, incidents %d over 60 days (repair charged once %d), ten 90 s circuits fly %d leg%s of %.1f min (paced by time %d): %s\n", noLic, hired, assigned, blocked, ticked, airNet, recalled, w.airline.incidents, once, circuitLegs, circuitLegs == 1 ? "" : "s", legMin, paced, ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- QA S7: the VIP's comfort (and the patient) ride simulated time: a minute of a too-steep bank (33 degrees, 3
      // past the VIP's limit) at 4x costs what it costs at 1x (the meters ran on real time, a quarter of the damage)
      {
        float dmg[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
          float accel = k == 0 ? 1.f : 4.f;
          Contract v = g_story[4]; v.story = false; v.type = CT_VIP; v.pax = 1; v.payout = 2000; v.wx.turbulence = 0.05f; v.wx.windSpeed = 4; v.wx.gust = 0;
          g.career.license = LIC_CPL; g.career.location = v.from;
          g.startFlight(v, 1, Career::SRC_RENT); g.isolatedFlight = false;
          const Airport& d = g_world.airports[v.to];
          vec3 pos = d.threshold(false) - d.dir() * 30000.f; pos.y = d.elev + 1500.f;
          g.plane.reset(&kAircraft[1], pos, d.heading, 60, 150, true, kAircraft[1].vref + 15);
          g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true;
          g.timeAccel = accel; s_pI = 0.f; s_eI = 0.f;
          for (float st = 0; st < 60.f && g.screen == SCR_FLIGHT; st += dt * accel) {
            Plane& p = g.plane;
            p.ctl.roll = clampf((33.f - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
            pitchFor(p, 0.f, dt * accel);
            p.ctl.throttle = clampf(0.6f + (kAircraft[1].vref + 15 - p.ias) * 0.05f, 0, 1); p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
            g.update(dt);
          }
          dmg[k] = 1.f - g.result.comfort;
          g.endFlight(false, "test", OUT_CRASHED); g.screen = SCR_HUB; g.timeAccel = 1;
        }
        bool ok = dmg[0] > 0.01f && fabsf(dmg[1] / std::max(dmg[0], 1e-6f) - 1.f) < 0.25f;
        printf("Job meters at 1x / 4x: comfort lost %.3f / %.3f over a simulated minute: %s\n", dmg[0], dmg[1], ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- diagnostics.bat and the other tools (diskless): their flights - crashes among them - never reach the player's
      // career. A tool session started on a folder holding a save flies a fresh career in memory, and leaves the save's
      // bytes, and the folder, as they were (the owner's v3.44 diagnostics charged their career for the crash scenes)
      {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path("diskless_test"); std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        Career mine; mine.newGame(); mine.money = 123456; mine.license = LIC_ATP;
        const std::string sav = (dir / "career.sav").string();
        const bool wrote = mine.save(sav);
        auto bytes = [](const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
        const std::string before = bytes(sav);
        Game d; d.headless = true; d.diskless = true; d.saveDir = dir.string(); d.init(false, nullptr);
        const bool fresh = !d.hasSave && d.career.money != 123456;
        Contract c = g_story[4]; c.story = false; c.payout = 1000; c.title = "Tool crash";
        d.career.license = LIC_CPL; d.career.location = c.from;
        d.startFlight(c, 1, Career::SRC_RENT); d.takeoffAnnounced = true;
        d.endFlight(false, "Crashed (tool scene)", OUT_CRASHED); d.saveGame(); d.saveSettings(); d.shutdown();
        int files = 0; for (auto& e : fs::directory_iterator(dir, ec)) { (void)e; files++; }
        const bool untouched = wrote && bytes(sav) == before && files == 1;
        fs::remove_all(dir, ec);
        bool ok = fresh && untouched;
        printf("Tools keep off the career: fresh career in memory %d, the save's bytes and folder untouched %d (%d file%s): %s\n", fresh, untouched, files, files == 1 ? "" : "s", ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- UI-1 (the v3.44.0 review): the radio panel owns the pointer over it. A click on a station reached the hub
      // beneath it first - one at 1080p over the Hangar bought a Wren ($30,000, saved) - and in an XR-40 flight armed the
      // weapons and fired. Clicks over the whole panel, on every tab: nothing beneath it moves
      {
        const int W0 = g_ren.W, H0 = g_ren.H; g_ren.W = 1920; g_ren.H = 1080;
        const std::string sd = g.saveDir; g.saveDir = ".";
        const auto st0 = g.stations; g.stations.clear();
        for (int i = 0; i < 16; i++) g.stations.push_back({fmt("Test FM %d", i), "test://"});
        g.career.newGame(); g.career.money = 1000000; g.screen = SCR_HUB; g.showRadio = true; g.focusNav = false;
        int moved = 0, clicks = 0;
        for (int tab = TAB_CONTRACTS; tab <= TAB_SETTINGS; tab++) {
          g.hubTab = tab; g.in.mx = g.in.my = -1; g_ren.uiBegin(); g.drawHub();   // (the panel's place, from its first frame)
          for (int j = 0; j < 12; j++) for (int i = 0; i < 10; i++) {
            const int money = g.career.money; const size_t fleet = g.career.fleet.size(); const bool loan = g.career.loan.open();
            g.in.mx = g.radioRect[0] + (i + 0.5f) * g.radioRect[2] / 10.f; g.in.my = g.radioRect[1] + (j + 0.5f) * g.radioRect[3] / 12.f;
            g.in.mPressed[0] = true; g_ren.uiBegin(); g.drawHub(); g.in.mPressed[0] = false; clicks++;
            if (g.career.money != money || g.career.fleet.size() != fleet || g.career.loan.open() != loan || g.hubTab != tab || g.screen != SCR_HUB) {
              moved++; g.career.newGame(); g.career.money = 1000000; g.screen = SCR_HUB; g.hubTab = tab;
            }
          }
        }
        // the XR-40, weapons safe, the radio open: holding the mouse button over it neither arms nor fires
        g.wraith = Game::WraithState(); g.showRadio = true; g.in.mDown[0] = true; g.in.mPressed[0] = true;
        g.wraithControls(dt);
        const bool safe = !g.wraith.armed && !g.wraith.wantFire;
        g.in.mDown[0] = g.in.mPressed[0] = false; g.showRadio = false; g.wraith = Game::WraithState();
        remove("./settings.cfg"); g.saveDir = sd; g.settingsWritten.clear(); g.stations = st0; g_ren.W = W0; g_ren.H = H0; g_ren.uiBegin();
        bool ok = moved == 0 && clicks == 600 && safe;
        printf("Radio panel owns its clicks: %d of %d clicks over it moved the hub beneath, XR-40 weapons stay safe %d: %s\n", moved, clicks, safe, ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- UI-4 (the v3.44.0 review): an 800 x 600 window at 140%: every settings control on both pages lies inside it
      {
        const int W0 = g_ren.W, H0 = g_ren.H; const float ui0 = g.set.uiScale, page0 = (float)g.settingsPage;
        g_ren.W = 800; g_ren.H = 600; g.set.uiScale = 1.4f; g.screen = SCR_HUB; g.hubTab = TAB_SETTINGS; g.showRadio = false; g.focusNav = false;
        int outside = 0, seen = 0;
        for (int page = 0; page < 2; page++) {
          g.settingsPage = page; g.focusList.clear(); g.in.mx = g.in.my = -1; g_ren.uiBegin(); g.drawHub();
          for (auto& f : g.focusList) { seen++; if (f.x < 0.f || f.x + f.w > (float)g_ren.W + 0.5f) outside++; }
        }
        g.settingsPage = (int)page0; g.set.uiScale = ui0; g_ren.W = W0; g_ren.H = H0; g_ren.uiBegin(); g.focusList.clear();
        const bool ok = seen > 20 && outside == 0;
        printf("Settings at 800x600, 140%%: %d of %d controls outside the window: %s\n", outside, seen, ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- UI-5 (the v3.44.0 review): the hub's tabs and the key-binding cells are in the keyboard / D-pad walk - Enter on
      //      a focused tab opens it, on a focused cell (one scrolled out of view too) it starts listening for the key
      {
        const int W0 = g_ren.W, H0 = g_ren.H; g_ren.W = 1920; g_ren.H = 1080;
        g.screen = SCR_HUB; g.hubTab = TAB_CONTRACTS; g.showRadio = false; g.bindCapture = -1; g.in = Input();
        auto frame = [&] { g.focusList.clear(); g_ren.uiBegin(); g.drawHub(); };
        g.focusNav = true; g.focusId = 0x7AB000u + 3u; frame();
        g.in.pressed[K_ENTER] = true; frame(); g.in.pressed[K_ENTER] = false;
        const bool tab = g.hubTab == 3;
        g.hubTab = TAB_SETTINGS; g.settingsPage = 1; frame();
        const int act = ACT_COUNT - 1;   // (the last row: below the list's first screen)
        g.focusId = 0xB1D00000u + (uint32_t)act * 2u; frame();
        bool listed = false; for (auto& f : g.focusList) listed |= f.id == g.focusId;
        g.in.pressed[K_ENTER] = true; frame(); g.in.pressed[K_ENTER] = false;
        const bool cell = listed && g.bindCapture == act && g.bindCaptureDev == 0;
        g.bindCapture = -1; g.focusNav = false; g.settingsPage = 0; g.screen = SCR_MENU; g.focusList.clear();
        g_ren.W = W0; g_ren.H = H0; g_ren.uiBegin();
        const bool ok = tab && cell;
        printf("Keyboard reach: Enter opens a focused hub tab %d, starts binding a focused cell scrolled out of view %d: %s\n", tab, cell, ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- CAR-1 (the v3.44.0 review): rough air costs the patient only what the aircraft is put through - five minutes
      // straight and level on the autopilot in P4's turbulence (0.55) left the patient at 10%, "in distress" whatever the
      // pilot did
      {
        Contract v = g_story[4]; v.story = false; v.type = CT_MEDEVAC; v.pax = 1; v.payout = 2000; v.timeLimitMin = 0; v.wx.turbulence = 0.55f; v.wx.windSpeed = 6; v.wx.gust = 0;
        g.career.license = LIC_CPL; g.career.location = v.from;
        g.startFlight(v, 1, Career::SRC_RENT); g.isolatedFlight = false;
        const Airport& d = g_world.airports[v.to];
        vec3 pos = d.threshold(false) - d.dir() * 40000.f; pos.y = d.elev + 1500.f;
        g.plane.reset(&kAircraft[1], pos, d.heading, 60, 150, true, kAircraft[1].vref + 15);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true;
        g.plane.apOn = true; g.plane.apMode = Plane::AP_HOLD; g.plane.apHeading = d.heading; g.plane.apAlt = pos.y; g.plane.apSpeed = kAircraft[1].vref + 15;
        g.timeAccel = 4;
        float minG = 9, maxG = -9;
        for (float st = 0; st < 300.f && g.screen == SCR_FLIGHT; st += dt * 4) { g.update(dt); minG = std::min(minG, g.plane.gLoad); maxG = std::max(maxG, g.plane.gLoad); }
        const float patient = g.result.patient;
        g.endFlight(false, "test", OUT_CRASHED); g.screen = SCR_HUB; g.timeAccel = 1;
        bool ok = patient > 0.85f;
        printf("Medevac in rough air (turbulence 0.55, 5 min on the autopilot, g %.2f..%.2f): patient %.0f%%: %s\n", minG, maxG, patient * 100.f, ok ? "ok" : "FAIL"); fails += !ok;
      }
      // ---- a diversion leaves you (and your aircraft) where you landed
      {
        Career t; t.newGame(); t.license = LIC_ATP;
        int spec = 2, at = g_world.findAirport("CAP"); t.fleet.push_back({spec, g_story[4].from, kAircraft[spec].maxFuel, 0.f});
        FlightResult r; r.outcome = OUT_DIVERTED; r.divertedTo = at; int st = 0;
        t.settle(g_story[4], spec, Career::SRC_OWNED, r, &st);
        bool ok = t.location == at && t.fleet[0].location == at;
        printf("Diverted: career and aircraft at the diversion airport: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      if (voices) {   // the strips have no tower: a flight from Meadowbrook to Harlan Farm hears only Meadowbrook
        g.startFlight(g_story[2], 0, Career::SRC_LESSON);   // L3: MDB -> HFS
        g.atc.history.clear(); g.atcF.phase = 3; g.atcF.airborne = true;
        const Airport& h = g_world.airports[g_story[2].to];
        g.plane.reset(&kAircraft[0], h.threshold(false) - h.dir() * 3000.f + vec3(0, h.elev + 160.f, 0), h.heading, 60, 150, true, kAircraft[0].vref + 6);
        for (float tt = 0; tt < 20; tt += dt) { g.update(dt); audio(dt); }
        bool silent = true; for (auto& x : g.atc.history) if (x.rfind("TWR ", 0) == 0) silent = false;
        printf("No tower at a farm strip: %s\n", silent ? "ok" : "FAIL"); fails += !silent;
      }
      if (voices) {   // every indexed clip decodes; the research cards, failure annunciators, aerobatics and every craft's SPLASH line resolve
        int bad = 0, n = 0;
        for (auto& id : g.atc.indexed()) { n++; if (!g.atc.decodes(id)) { if (bad < 5) printf("   clip does not decode: %s\n", id.c_str()); bad++; } }
        printf("Voice clips decode (%d): %s\n", n, bad ? "FAIL" : "ok"); fails += bad > 0;
        std::vector<std::string> msgs;
        for (int i = 0; i < Game::kNumResCards; i++) {
          const Game::ResCard& C = Game::kResCards[i];
          std::string nm = kAircraft[C.craft].name; for (char& ch : nm) ch = (char)toupper((unsigned char)ch);
          msgs.push_back(nm + " // TEST CARD " + C.title); msgs.push_back(nm + " // RESEARCH FLIGHT");
          for (int k = 0; k < C.n; k++) msgs.push_back(fmt("STEP %d of %d: %s", k + 1, C.n, C.steps[k].label));
          msgs.push_back(fmt("TEST CARD %s COMPLETE - %s SIGNED OFF", C.id, C.title));
        }
        for (int e = 1; e <= 4; e++) { msgs.push_back(fmt("ENGINE %d FAILED", e)); msgs.push_back(fmt("ENGINE %d POWER LOSS", e)); }
        for (const char* m : {"ENGINE FAILED", "ENGINE POWER LOSS", "ENGINE FAILURE  glide 12:1, best 85 kt", "ENGINE FAILURE  glide 9:1, best 120 kt",
                              "ALTERNATOR  battery 60%", "ALTERNATOR  battery 7%", "BATTERY FLAT  no autopilot, no GPS", "PITOT BLOCKED  airspeed unreliable",
                              "GEAR STUCK UP  belly landing: paved, level, slow", "GEAR STUCK DOWN  slower, more fuel", "FLAP ASYMMETRY  hold the wing up",
                              "ICING 30%  leave the cloud, keep speed", "ICING 100%  leave the cloud, keep speed",
                              "Autopilot disconnected - Engine failure", "Autopilot off - battery flat", "GPS dark - battery flat", "Gear won't come down - it's stuck up",
                              "Belly landing - hold it straight", "Belly landing too hard - hit at 812 fpm", "Belly landing too hard - hit at -1250 fpm",
                              "Autopilot: the wind has shifted - now runway 05 at MDB", "Autopilot: the wind has shifted - now runway 23 at ORC",
                              "Rain has started", "Snow has set in", "Aerobatics: recovering to level flight", "Aerobatics need to be airborne",
                              "MACH 1 - SONIC BOOM", "CLOAK ENGAGED", "PLASMA BOMB AWAY", "Pods 90 deg - VTOL hover", "Thrust vector 90 deg - VTOL hover"}) msgs.push_back(m);
        for (int f = 0; f < Plane::STUNT_COUNT; f++) msgs.push_back(fmt("Aerobatics: %s", Plane::stuntName(f)));
        int nAll = kNumAircraft + 4;   // (the career fleet and the four research craft)
        for (int i = 0; i < nAll; i++) msgs.push_back(fmt("SPLASH %d - %s down", i + 1, kAircraft[i].name));
        bad = 0;
        for (auto& m : msgs) { AtcVoice::Tx tx; if (!g.atc.resolve(m, "", false, tx)) { printf("   no voice line for '%s'\n", m.c_str()); bad++; } }
        printf("Voice coverage (%d messages): %s\n", (int)msgs.size(), bad ? "FAIL" : "ok"); fails += bad > 0;
        // exact pod settings coalesce like the dynamic ones; the compact HUD label speaks the fuller recorded line
        AtcVoice::Tx tx;
        bool pods = g.atc.resolve("Pods 90 deg - VTOL hover", "", false, tx) && tx.group == "lever";
        bool alias = g.atc.resolve("BATTERY FLAT  no autopilot, no GPS", "", false, tx) && tx.ids.size() == 1 && g.atc.text(tx.ids[0]) == "BATTERY FLAT - no autopilot, no GPS";
        printf("Pod setting grouped %d, HUD alias spoken in full %d: %s\n", pods, alias, pods && alias ? "ok" : "FAIL"); fails += !(pods && alias);
      }
      {   // XR-20 has one real engine: its live HUD and spoken warning must not retain a second-engine channel.
        Plane saved = g.plane;
        const AircraftSpec& s = kAircraft[kMantis];
        g.plane.reset(&s, vec3(-39000, 8000, 35000), 0, s.maxFuel * 0.7f, 85, true, 200);
        g.plane.failNow(FAIL_ENGINE_PARTIAL, 0);
        auto partial = g.hudAnnunciators();
        bool ok = s.engines == 1 && partial.size() == 1 && partial[0].text == "ENGINE POWER LOSS";
        g.plane.failNow(FAIL_ENGINE_TOTAL, 0);
        auto total = g.hudAnnunciators();
        ok = ok && total.size() == 1 && total[0].text.rfind("ENGINE FAILURE  glide ", 0) == 0 && g.plane.glideOnly() && !g.plane.engineRunning;
        if (voices) {
          AtcVoice::Tx tx;
          for (const auto& ann : partial) ok = g.atc.resolve(ann.text, "", false, tx) && ok;
          for (const auto& ann : total) ok = g.atc.resolve(ann.text, "", false, tx) && ok;
        }
        printf("XR-20 single-engine HUD: partial %zu / total %zu warning, live glide guidance and voice %s\n", partial.size(), total.size(), ok ? "ok" : "FAIL"); fails += !ok;
        g.plane = saved;
      }
      if (voices) {   // the startup announcements survive startFlight's comms reset; a mechanical engine failure gives glide guidance, not restart advice
        Contract c = g_story[4]; c.wx = Weather();
        g.startFlight(c, 1, Career::SRC_OWNED);
        int pend = (int)g.commsPending.size();
        bool announced = pend >= 2 && g.commsPending[0].text.find(g_world.airports[c.from].code) == 0 && g.commsPending[1].text.rfind("Runway ", 0) == 0;
        const Airport& a = g_world.airports[c.to];
        vec3 start = a.threshold(false) - a.dir() * 3000.f; start.y = a.elev + 400.f;
        g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref * 1.2f);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.flightClock = 5.f;
        g.atc.historyLimit = 0; g.atc.history.clear();
        for (float t = 0; t < 1.f; t += dt) { g.update(dt); audio(dt); }
        g.fireFailure(FAIL_ENGINE_TOTAL, 0);
        for (float t = 0; t < 14.f; t += dt) { g.plane.ctl.pitch = 0.1f; g.update(dt); audio(dt); }
        bool restart = false, glide = false;
        for (auto& x : g.atc.history) { if (x.find("ENGINE OFF - press") != std::string::npos) restart = true; if (x.find("ENGINE FAILURE") != std::string::npos || x.find("Engine failure") != std::string::npos) glide = true; }
        for (auto& x : g.atc.history) printf("   voice: %s\n", x.c_str());
        bool ok = announced && !restart && glide;
        printf("Startup announcements pending %d, engine failure spoken %d, restart advice %d: %s\n", announced, glide, restart, ok ? "ok" : "FAIL"); fails += !ok;
        g.screen = SCR_MENU; g.update(dt);
      }
      if (voices) {   // E6: a tower call made for one flight state is dropped if the state has moved on before it is said
        bool hints0 = g.set.showHints; g.set.showHints = false;   // (no instructor lines competing for the channel)
        g.startFlight(g_story[2], 0, Career::SRC_LESSON); g.atc.history.clear(); g.atc.dropped = 0;
        AtcVoice::Tx stale; stale.ids = {g.atc.line(0, "greeting_morning")}; stale.text = "stale clearance"; stale.prio = 70; stale.key = 999;   // (a state the flight is no longer in)
        AtcVoice::Tx fresh = stale; fresh.text = "fresh call"; fresh.key = -1; fresh.prio = 60;   // (both ahead of the instructor's hints, the stale one first)
        g.atc.say(stale); g.atc.say(fresh);
        for (float tt = 0; tt < 30; tt += dt) { g.update(dt); audio(dt); }   // (the instructor's own line goes first; the channel is one at a time)
        bool saidFresh = false, saidStale = false; for (auto& h : g.atc.history) { if (h.find("fresh call") != std::string::npos) saidFresh = true; if (h.find("stale clearance") != std::string::npos) saidStale = true; }
        bool ok = g.atc.dropped >= 1 && saidFresh && !saidStale;
        printf("Stale tower call dropped, fresh one said (dropped %d, fresh %d, stale %d): %s\n", g.atc.dropped, saidFresh, saidStale, ok ? "ok" : "FAIL"); fails += !ok;
        g.endFlight(false, "x", OUT_ABANDONED); g.career.newGame(); g.pendingCareer.reset(); g.set.showHints = hints0;
      }
      {   // E5.4: the arrow keys walk the registered buttons: the first press takes the top-left one, the next the nearest below
        g.screen = SCR_MENU; g.paused = false; g.in = Input(); g.focusNav = false; g.focusScreen = -1;
        auto buttons = [&]() { g.focusList = {{11u, 100.f, 300.f, 200.f, 40.f}, {22u, 100.f, 400.f, 200.f, 40.f}, {33u, 400.f, 300.f, 200.f, 40.f}}; };
        buttons(); g.focusNavigate(); g.in.endFrame();              // (a frame with the buttons drawn)
        buttons(); g.in.pressed[K_DOWN] = true; g.focusNavigate(); g.in.endFrame();
        bool first = g.focusNav && g.focusId == 11u;
        buttons(); g.in.pressed[K_DOWN] = true; g.focusNavigate(); g.in.endFrame();
        bool down = g.focusId == 22u;
        buttons(); g.in.pressed[K_UP] = true; g.focusNavigate(); g.in.endFrame();
        buttons(); g.in.pressed[K_RIGHT] = true; g.focusNavigate(); g.in.endFrame();
        bool right = g.focusId == 33u;
        buttons(); g.in.mdx = 5.f; g.focusNavigate(); g.in.endFrame();
        bool mouse = !g.focusNav;
        bool ok = first && down && right && mouse;
        printf("Menu focus navigation: first %d, down %d, right %d, mouse releases %d: %s\n", first, down, right, mouse, ok ? "ok" : "FAIL"); fails += !ok;
      }
      {   // UI-3 (the v3.44.0 review): a settings toggle keeps its focus when its label flips (its identity is its setting)
        Game q; q.initHeadless(); q.screen = SCR_MENU;
        auto idOf = [&](const char* l) { q.focusList.clear(); q.button(100, 100, 200, 32, l); return q.focusList.empty() ? 0u : q.focusList.back().id; };
        const uint32_t on = idOf("On##Air traffic"), off = idOf("Off##Air traffic"), other = idOf("Off##Units"), plain = idOf("Off");
        const bool ok = on && on == off && off != other && plain != off;
        printf("Toggle focus across its label: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
      }
      {   // the career's launches and their saves (the review of v3.24.0, R4-R6): a launch whose save fails flies nothing and
          // leaves nothing pending; a cancelled loading screen leaves the job waiting at its stop; a free flight beside a
          // waiting job leaves it as it is, at launch and at settlement; the debrief's retry flies a waiting job on
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "solace_lifecycle_test";
        std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root, ec);
        const std::string bad = (root / "absent" / "x").string(), good = root.string();
        auto fresh = [&](Game& q) { q.initHeadless(); q.career.license = LIC_PPL; q.career.storyIndex = 4; q.screen = SCR_HUB; };
        auto waitingJob = [&](Game& q) {
          const Contract c = g_story[4]; auto p = q.career.plan(c, 1, Career::SRC_RENT); q.career.accept(c, 1, Career::SRC_RENT, p);
          q.career.job->state = Career::JobState::RECOVERY; q.career.job->jobClockMin = 5; q.career.job->comfort = 0.3f; q.career.job->legs = 1; q.career.job->hirePaid = true;
        };
        Contract freeF; freeF.id = "FREE"; freeF.type = CT_FERRY; freeF.from = g_story[4].from; freeF.to = g_story[4].to;
        bool newFail, resumeFail, cancel, freeKeep, retryKeep;
        { Game q; fresh(q); q.saveDir = bad; q.beginCareerFlight(g_story[4], 1, Career::SRC_RENT);
          newFail = q.screen == SCR_HUB && !q.pendingCareer && !q.career.job && !q.career.attemptOpen; }
        { Game q; fresh(q); waitingJob(q); q.saveDir = bad; q.continueJob(1, Career::SRC_RENT);
          bool held = q.screen == SCR_HUB && !q.pendingCareer && q.career.job && q.career.job->state == Career::JobState::RECOVERY;
          q.saveDir = good; q.retryCommit();
          resumeFail = held && q.career.job && q.career.job->state == Career::JobState::RECOVERY && !q.career.attemptOpen; }
        { Game q; fresh(q); q.beginCareerFlight(g_story[4], 1, Career::SRC_RENT);
          q.screen = SCR_LOADING; q.in.pressed[K_ESC] = true; q.updateLoading(0.1f); q.in.endFrame();
          cancel = q.screen == SCR_HUB && q.career.job && q.career.job->state == Career::JobState::RECOVERY && !q.career.attemptOpen; }
        { Game q; fresh(q); waitingJob(q); const std::string id = q.career.job->c.id;
          q.beginCareerFlight(freeF, 1, Career::SRC_RENT);
          bool atLaunch = q.career.job && q.career.job->c.id == id;
          q.endFlight(false, "Abandoned", OUT_ABANDONED);
          freeKeep = atLaunch && q.career.job && q.career.job->c.id == id && q.career.job->state == Career::JobState::RECOVERY && q.career.job->jobClockMin == 5.f; }
        { Game q; fresh(q); waitingJob(q); q.specIdx = 1; q.source = Career::SRC_RENT; q.contract = q.career.job->c; q.screen = SCR_DEBRIEF;
          q.retryFromDebrief();
          retryKeep = q.career.job && q.career.job->state == Career::JobState::ACTIVE && q.career.job->jobClockMin == 5.f && q.career.job->legs == 1 && q.career.job->comfort == 0.3f && q.career.job->hirePaid; }
        fs::remove_all(root, ec);
        bool ok = newFail && resumeFail && cancel && freeKeep && retryKeep;
        printf("Career launches: failed save flies nothing %d, failed continue keeps the job %d, cancelled loading %d, free flight keeps the job %d, debrief retry continues %d: %s\n",
               newFail, resumeFail, cancel, freeKeep, retryKeep, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // an arrival is a landing on the destination's runway (the review of v3.24.0, R2): stopped 400 m beside it is an
          // off-field landing; a runway touchdown then a taxi to the apron 150 m off the centreline delivers
        auto arrive = [&](float lateral, int td) {
          Game q; q.initHeadless(); q.career.license = LIC_PPL; q.career.storyIndex = 4; q.botControl = true; q.set.traffic = false;
          Contract c = g_story[4]; c.wx = Weather(); c.wx.windSpeed = 0; c.wx.gust = 0; c.wx.turbulence = 0; c.wps.clear();
          q.beginCareerFlight(c, 0, Career::SRC_RENT);
          const Airport& a = g_world.airports[c.to]; vec3 d = a.dir(); vec3 loc = a.pos() + vec3(-d.z, 0, d.x) * lateral;
          loc.y = g_world.height(loc.x, loc.z) + q.plane.gearHeight() - 0.035f;
          q.takeoffAnnounced = true; q.touchedDown = true; q.touchdownFpm = 100; q.engineAutoStarted = true; q.plane.sceneryHits = false;
          q.tdRunway = td == -3 ? c.to : td;
          for (int i = 0; i < 100 && q.screen == SCR_FLIGHT; i++) {
            q.plane.pos = loc; q.plane.vel = vec3(); q.plane.w = vec3(); q.plane.q = quat::axisAngle(vec3(0, 1, 0), -a.heading * DEG);
            q.plane.onGround = true; q.plane.ctl = Controls(); q.plane.ctl.brake = 1; q.plane.ctl.gearDown = true; q.update(0.05f);
          }
          return q.screen == SCR_DEBRIEF && q.lastSuccess;
        };
        bool beside = !arrive(400.f, -2), besideTd = !arrive(400.f, -1), apron = arrive(150.f, -3), onRwy = arrive(0.f, -2);
        bool ok = beside && besideTd && apron && onRwy;
        printf("Arrivals: 400 m beside the runway refused %d (touched down there %d), runway then apron delivers %d, stopped on the runway delivers %d: %s\n",
               beside, besideTd, apron, onRwy, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // a checkpoint job's leg ends safely at a field (the review of v3.44.0, CAR-5): stopped on a runway with checkpoints
          // left it waits to take off again, and the parking brake ends the leg there with the checkpoint flown carried; a
          // hold instruction ignored on that leg is still charged at the delivery, through a save and reload (CAR-6)
        Game q; q.initHeadless(); q.career.license = LIC_PPL; q.career.money = 50000; q.botControl = true; q.set.traffic = false;
        const int via = g_world.findAirport("CAP");
        Contract c; c.id = "CAR5_TEST"; c.title = "Checkpoint freight"; c.type = CT_CARGO; c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("ORC");
        c.cargoKg = 40; c.payout = 1000; c.minLicense = LIC_PPL; c.wx = Weather(); c.wx.windSpeed = 0; c.wx.gust = 0; c.wx.turbulence = 0;
        for (int i = 0; i < 2; i++) c.wps.push_back({g_world.airports[c.to].x + 3000.f * i, g_world.airports[c.to].z, 600.f});
        q.career.location = c.from;
        q.beginCareerFlight(c, 1, Career::SRC_RENT);
        q.wpIndex = 1; q.result.wpDone = 1; q.result.holdViolated = true;
        q.takeoffAnnounced = true; q.touchedDown = true; q.touchdownFpm = 100; q.engineAutoStarted = true; q.plane.sceneryHits = false; q.tdRunway = -2; q.parkingBrake = false;
        const Airport& a = g_world.airports[via]; vec3 loc = a.pos(); loc.y = g_world.height(loc.x, loc.z) + q.plane.gearHeight() - 0.035f;
        auto hold = [&](int frames) {
          for (int i = 0; i < frames && q.screen == SCR_FLIGHT; i++) {
            q.plane.pos = loc; q.plane.vel = vec3(); q.plane.w = vec3(); q.plane.q = quat::axisAngle(vec3(0, 1, 0), -a.heading * DEG);
            q.plane.onGround = true; q.plane.ctl = Controls(); q.plane.ctl.brake = 1; q.plane.ctl.gearDown = true; q.update(0.05f);
          }
        };
        hold(100);
        bool told = false; for (auto& t : q.toasts) told |= t.text.find("parking brake") != std::string::npos;
        const bool waits = q.screen == SCR_FLIGHT && told;
        q.parkingBrake = true; hold(100);
        const bool closed = q.screen == SCR_DEBRIEF && q.career.job && q.career.job->state == Career::JobState::RECOVERY && q.career.job->at == via
                            && q.career.job->wpDone == 1;
        const bool kept = closed && q.career.job->holdViolated;
        bool reloaded = false, charged = false, resumed = false;
        if (closed) {
          const std::string path = "car6_test.sav";
          Career r; reloaded = q.career.save(path) && r.load(path) && r.job && r.job->holdViolated == q.career.job->holdViolated && r.job->wpDone == 1;
          remove(path.c_str());
          q.career = r; q.continueJob(1, Career::SRC_RENT);
          resumed = q.career.job && q.wpIndex == 1 && !q.result.holdViolated;
          q.wpIndex = 2; q.result.wpDone = 2; q.flightClock = 300.f; q.plane.onGround = true; q.touchedDown = true; q.touchdownFpm = 200.f;
          q.endFlight(true, "", OUT_SUCCESS);
          for (auto& l : q.payout) charged |= l.label == "Took off against a hold instruction" && l.amount < 0;
        }
        const bool ok = waits && closed && kept && reloaded && resumed && charged;
        printf("Checkpoint job leg: waits on the runway %d, parking brake ends the leg %d, hold violation kept %d, reloaded %d, continued %d, charged at delivery %d: %s\n",
               waits, closed, kept, reloaded, resumed, charged, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // a job's next leg is planned over the checkpoints still to fly (the review of v3.44.0, CAR-4): its range check,
          // time and fuel are those of the same contract without the checkpoints flown, and it keeps their numbers
        Career k; k.newGame(); k.license = LIC_ATP; k.money = 100000;
        Contract c; c.id = "CAR4_TEST"; c.type = CT_SURVEY; c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("ORC"); c.cargoKg = 40; c.payout = 3000; c.minLicense = LIC_PPL;
        const Airport& A = g_world.airports[c.from];
        for (int i = 0; i < 6; i++) c.wps.push_back({A.x + 9000.f * cosf(i * 1.0472f), A.z + 9000.f * sinf(i * 1.0472f), A.elev + 600.f});
        const int si = 1; bool same = true, numbered = true;
        for (int done : {0, 3, 6}) {
          Career::JobState J; J.c = c; J.at = g_world.findAirport("CAP"); J.wpDone = done; J.spec = si;
          const Contract cont = J.continuation();
          Contract direct = cont; direct.wps.erase(direct.wps.begin(), direct.wps.begin() + done); direct.wpStart = 0;
          std::string w1, w2; k.location = J.at;
          const Career::Source s1 = k.canFly(cont, si, &w1), s2 = k.canFly(direct, si, &w2);
          const Career::LaunchPlan p1 = k.plan(cont, si, Career::SRC_RENT), p2 = k.plan(direct, si, Career::SRC_RENT);
          const bool eq = s1 == s2 && w1 == w2 && fabsf(p1.minutesEst - p2.minutesEst) < 1e-3f && fabsf(p1.fuelKgEst - p2.fuelKgEst) < 1e-3f;
          if (!eq) printf("  continuation after %d checkpoints: %d '%s' %.2f min %.1f kg, direct %d '%s' %.2f min %.1f kg\n", done, (int)s1, w1.c_str(), p1.minutesEst, p1.fuelKgEst, (int)s2, w2.c_str(), p2.minutesEst, p2.fuelKgEst);
          same = same && eq;
          numbered = numbered && cont.wps.size() == c.wps.size() && cont.wpStart == done;
        }
        const bool ok = same && numbered;
        printf("Next leg planned over the checkpoints left: same as without those flown %d, checkpoints keep their numbers %d: %s\n", same, numbered, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // a refused autoland (the review of v3.31.0, F3): the Starling asked to land at Gull Rock's 480 m is told it can't
          // and circles clear of the ground - and, as the toast says, the stick takes the aircraft back
        Game q; q.initHeadless(); q.career.license = LIC_ATP; q.botControl = false; q.set.traffic = false;
        Contract c = g_story[4]; c.wx = Weather(); c.wx.windSpeed = 0; c.wx.gust = 0; c.wx.turbulence = 0; c.wps.clear(); c.startAirborne = true;
        q.beginCareerFlight(c, 6, Career::SRC_RENT);
        q.plane.pos.y += 900.f; q.plane.onGround = false; q.takeoffAnnounced = true; q.engineAutoStarted = true;
        q.update(1.f / 60.f);   // (into the flight's input context: keys already held when it changes are set aside)
        q.apDest = g_world.findAirport("GLR");
        q.engageAutopilot();
        const bool held = q.plane.apOn && q.plane.apMode == Plane::AP_HOLD && !q.plane.apDecline.empty();
        bool told = false; for (auto& t : q.toasts) told |= t.text.find("hands control back") != std::string::npos;
        q.in.down[K_DOWN] = true;
        for (int i = 0; i < 5 && q.plane.apOn; i++) q.update(1.f / 60.f);
        q.in.down[K_DOWN] = false;
        const bool ok = q.screen == SCR_FLIGHT && held && told && !q.plane.apOn;
        printf("Refused autoland: holds %d (%s), says the stick takes over %d, stick takes over %d: %s\n", held, q.plane.apDecline.c_str(), told, !q.plane.apOn, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // the fuel chosen is the fuel billed (the review of v3.34.0, S1): an owned Wren with its flown quote in, launched
          // with full tanks rather than the quote's uplift - the tanks, the plan's uplift and charge and the job's saved plan agree
        Game q; q.initHeadless(); q.career.license = LIC_CPL; q.career.money = 50000; q.career.location = g_world.findAirport("ORC");
        Contract c = g_story[4]; const int si = 1; q.career.fleet.push_back({si, c.from, 0.f, 1.f});
        float kg = -1; float minutes = simulateFlightMinutes(c, si, &kg);
        q.quoteFlown[fmt("%s|%d|%d|%d", c.id.c_str(), si, c.from, c.to)] = {minutes, kg};
        q.launchFuelKg = kAircraft[si].maxFuel;
        const int expect = (int)(kAircraft[si].maxFuel * q.career.fuelPrice(c.from, si));
        q.beginCareerFlight(c, si, Career::SRC_OWNED);
        const bool tank = fabsf(q.plane.fuel - kAircraft[si].maxFuel) < 0.5f, uplift = fabsf(q.launchPlan.fuelUpliftKg - kAircraft[si].maxFuel) < 0.5f;
        const bool charge = abs(q.launchPlan.fuelCostEst - expect) <= 1, saved = q.career.job && q.career.job->plan.fuelCostEst == q.launchPlan.fuelCostEst;
        const bool ok = kg > 0.f && kg < kAircraft[si].maxFuel * 0.9f && tank && uplift && charge && saved;
        printf("Chosen fuel billed: quote %.0f kg, tanks %.0f kg, uplift %.0f kg, charge $%d of $%d, job's plan agrees %d: %s\n", kg, q.plane.fuel, q.launchPlan.fuelUpliftKg,
               q.launchPlan.fuelCostEst, expect, saved, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // a survey's altitude record survives a diversion (the review of v3.34.0, S2): 300 s outside the band, the
          // checkpoints done, diverted, the job continued and finished - the time out of the band still counts
        Game q; q.initHeadless(); q.career.license = LIC_PPL; q.career.money = 50000;
        Contract c; c.id = "SURVEY_TEST"; c.title = "Survey test"; c.type = CT_SURVEY; c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("ORC");
        c.pax = 1; c.cargoKg = 40; c.payout = 10000; c.minLicense = LIC_PPL;
        for (int i = 0; i < 6; i++) c.wps.push_back({1000.f * i, 0.f, 500.f});
        q.beginCareerFlight(c, 1, Career::SRC_RENT); q.plane.onGround = false; q.takeoffAnnounced = true; q.wpIndex = 1; q.plane.pos.y = 650.f;
        q.updateJobMeters(300.f, 50.f);
        const float before = q.surveyT;
        q.wpIndex = 6; q.result.wpDone = 6; q.flightClock = 300.f; q.plane.onGround = true; q.touchedDown = true; q.result.divertedTo = c.from;
        q.endFlight(false, "Diverted", OUT_DIVERTED);
        q.continueJob(1, Career::SRC_RENT);
        const float carried = q.surveyT, carriedIn = q.surveyInT;
        q.flightClock = 120.f; q.plane.onGround = true; q.touchedDown = true; q.touchdownFpm = 200.f;
        q.endFlight(true, "", OUT_SUCCESS);
        bool bonus = false, docked = false;
        for (auto& l : q.payout) { bonus |= l.label == "Survey altitude held"; docked |= l.label.find("Survey altitude held ") == 0 && l.amount < 0; }
        const bool ok = before >= 299.f && carried >= 299.f && carriedIn < 1.f && !bonus && docked;
        printf("Survey record across a diversion: %.0f s out of band before, %.0f s (%.0f in band) carried on, bonus %d, docked %d: %s\n", before, carried, carriedIn, bonus, docked, ok ? "ok" : "FAIL");
        fails += !ok;
      }
      {   // a checkride's standard gates its licence (the review of v3.24.0, R8): 600 fpm passes, just over fails, as do a
          // take-off against a hold and a landing against a go-around; a failed one leaves the licence and the story as they were
        int ride = -1; for (int i = 0; i < (int)g_story.size() && ride < 0; i++) if (g_story[i].grantLicense == LIC_PPL) ride = i;
        auto fly = [&](float fpm, bool hold, bool ga) {
          Career k; k.newGame(); k.storyIndex = ride;
          FlightResult r; r.success = true; r.landed = true; r.touchdownFpm = fpm; r.holdViolated = hold; r.landedAgainstGoAround = ga; r.fuelLeftFrac = 0.6f; r.flightMin = 20;
          int st = 0; k.settle(g_story[ride], 0, Career::SRC_LESSON, r, &st);
          return k.license == LIC_PPL && k.storyIndex == ride + 1;
        };
        bool pass = fly(600.f, false, false), hard = !fly(601.f, false, false), hold = !fly(200.f, true, false), ga = !fly(200.f, false, true), all = !fly(700.f, true, true);
        bool ok = ride >= 0 && pass && hard && hold && ga && all;
        printf("Checkride standard: 600 fpm passes %d, 601 fpm fails %d, hold violation fails %d, go-around ignored fails %d, all three fail %d: %s\n", pass, hard, hold, ga, all, ok ? "ok" : "FAIL");
        fails += !ok;
      }
    }
    if (const char* wpath = getenv("ATCWAV")) if (FILE* f = fopen(wpath, "wb")) {
      uint32_t bytes = (uint32_t)(rec.size() * 2), v;
      fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
      uint32_t fmt[4] = {16, 1u | (2u << 16), 48000, 48000 * 4}; fwrite(fmt, 4, 4, f); uint16_t ba[2] = {4, 16}; fwrite(ba, 2, 2, f);
      fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(rec.data(), 2, rec.size(), f); fclose(f);
    }
    printf("%d failures\n", fails);
    return fails;
  }
};
int main(int argc, char** argv) {
  int part = 0;
  for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--part")) part = atoi(argv[i + 1]);
  return GameTest::run(part);
}
