// End-to-end gameplay test: a scripted pilot flies Lesson 1 and then lands at a destination,
// exercising the real game loop (completion detection, scoring, payout, story progression).
#include "../src/game.h"
#include <cstdlib>
#include <cmath>
// attitude-based vertical-speed controller (same structure as the in-game autopilot)
static float s_pI = 0;
static void pitchFor(Plane& p, float vsT, float dt, float maxPitch = 14.f) {
  s_pI = clampf(s_pI + (vsT - p.vel.y) * 0.15f * dt, -8.f, 12.f);
  float pitchT = clampf(s_pI + 0.8f * (vsT - p.vel.y), -10.f, maxPitch);
  p.ctl.pitch = clampf(0.07f * (pitchT - p.pitchDeg()) - 1.2f * p.w.x, -1, 1);
}
struct GameTest {
  static int run() {
    g_world.build(); buildStory();
    g_audio.init(48000);
    Game g; g.initHeadless(); g.botControl = true;
    int fails = 0;
#ifdef SOLACE_ASSETS
    bool voices = g.atc.load(std::string(SOLACE_ASSETS) + "/voice");   // the tower voices (the audio is rendered below, as the audio thread would)
#else
    bool voices = false;
#endif
    static float abuf[2 * 4096];
    if (voices) {   // the voice lines resolve for the game's messages, fixed and assembled from fragments
      struct Case { const char* msg; const char* mission; bool pad, want; } cases[] = {
        {"Checkpoint 2 of 6", "", false, true}, {"Flaps 33%", "", false, true}, {"Gear down", "", false, true},
        {"Runway 05, Wind 050@4kt, scattered 4500ft, vis 10+km, 09:30", "", false, true}, {"MDB - Meadowbrook Field", "", false, true},
        {"BUTTER!  42 fpm", "", false, true}, {"Gear collapsed - hit at 812 fpm", "", false, true}, {"SPLASH 2 - Wren 180 down", "", false, true},
        {"3 aircraft caught in the blast", "", false, true}, {"ENGINE OFF - press I to restart", "", false, true}, {"STALL", "", false, true},
        {"SPECTRE: That's the show - Specters breaking off. Fly safe!", "", false, true}, {"Pods 60 deg", "", false, true},
        {"Nice! Hold a gentle climb about 7 degrees nose-up. Fly through the green rings.", "L1", false, true},
        {"Press B to release the parking brake, then hold SHIFT (or gamepad RT) to add full throttle.", "L1", true, true},    // gamepad wording
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
    for (int wxi = 0; wxi < 2; wxi++) {
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
    g.startFlight(g_story[0], 0, Career::SRC_LESSON);
    const float dt = 1.f / 60.f;
    float t = 0;
    for (; t < 400 && g.screen == SCR_FLIGHT; t += dt) {
      Plane& p = g.plane;
      p.ctl.brake = t < 3 ? 1.f : 0.f;
      p.ctl.throttle = t < 3 ? 0.f : 1.f;
      // steer toward the next ring
      vec3 tgt = g.wpIndex < (int)g.contract.wps.size() ? vec3(g.contract.wps[g.wpIndex].x, g.contract.wps[g.wpIndex].alt, g.contract.wps[g.wpIndex].z) : p.pos + p.forward() * 100.f;
      vec3 to = tgt - p.pos;
      float brg = atan2f(to.x, -to.z) / DEG;
      float herr = wrapAngle((brg - p.heading()) * DEG) / DEG;
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

    // ---- Approach and landing at Orchard Valley (contract C1 style), Wren rental
    g.career.license = LIC_PPL; g.career.storyIndex = 4; g.career.location = g_world.findAirport("ORC");
    int money0 = g.career.money;
    Contract c = g_story[4];  // C1 ORC -> MDB
    c.wx.windSpeed = 2; c.wx.gust = 0; c.wx.turbulence = 0.05f;
    g.startFlight(c, 1, Career::SRC_RENT);
    const Airport& a = g_world.airports[c.to];
    vec3 dir = a.dir();
    vec3 thr = a.threshold(false);
    vec3 start = thr - dir * 3500.f; start.y = a.elev + 3500.f * tanf(3.f * DEG) + 15.f;
    g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
    g.takeoffAnnounced = true; g.engineAutoStarted = true;
    g.atcF.phase = 3; g.atc.history.clear();   // (placed on final: an inbound start, as the game's airborne starts are)
    g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f;
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
        float herr = wrapAngle((hdgT - p.heading()) * DEG) / DEG;
        p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
        float vsT = agl < 7.f ? -0.7f : clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1);
        pitchFor(p, vsT, dt);
        p.ctl.throttle = agl < 6.f ? 0.f : clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
        p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
      } else { p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = 0; p.ctl.roll = 0; p.ctl.yaw = 0; }
      g.update(dt); audio(dt);
      if (getenv("TRACE") && fmodf(t, 0.5f) < dt && t < 14) printf("  t%4.1f gnd %d ias %5.1f agl %6.1f along %6.0f lat %5.1f pitch %5.1f vs %5.1f thr %.2f bank %5.1f ctlP %5.2f ctlR %5.2f flap %.2f alpha %5.1f\n", t, p.onGround, p.ias, agl, along, lat, p.pitchDeg(), p.vel.y, p.ctl.throttle, p.bankDeg(), p.ctl.pitch, p.ctl.roll, p.flaps, p.alpha/DEG);
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
      g.traffic.craft.clear();
      for (float tt = 0; tt < 30; tt += dt) { rollout(g.contract.from, 600.f); g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0; g.update(dt); audio(dt); }
      bool held = towerSaid("Hold position") && !towerSaid("cleared for takeoff");
      g.set.traffic = false;   // (and the field empties)
      for (float tt = 0; tt < 15; tt += dt) { g.plane.ctl.brake = 1; g.plane.ctl.throttle = 0; g.update(dt); audio(dt); }
      bool cleared = towerSaid("cleared for takeoff");
      for (auto& x : g.atc.history) printf("   voice: %s\n", x.c_str());
      printf("Departure hold for traffic: %s\n", held && cleared ? "ok" : "FAIL"); fails += !(held && cleared);
    }
    if (voices) {   // arrival: the runway is occupied on final (continue), and still occupied on short final (go around)
      g.career.location = g_world.findAirport("ORC");
      g.startFlight(c, 1, Career::SRC_RENT);
      g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
      g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true; g.atc.history.clear();
      g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f;
      g.traffic.craft.clear(); g.set.traffic = true;
      for (t = 0; t < 150 && g.screen == SCR_FLIGHT && !g.plane.onGround; t += dt) {
        rollout(c.to, 300.f);
        Plane& p = g.plane;
        vec3 rel = p.pos - thr; float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
        float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
        float herr = wrapAngle((a.heading - clampf(lat * 0.08f, -20, 20) - p.heading()) * DEG) / DEG;
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
    g.set.traffic = trafficSet;
    // ---- a lesson starts with the parking brake set: full throttle doesn't move the aircraft until it's released
    {
      g.botControl = false;
      for (int sp = 0; sp < kNumAircraft; sp++) {   // every career aircraft, from a free flight at Solace Capital
        Contract fc = g_story[0]; fc.forceAircraft = -1; fc.type = CT_FERRY; fc.from = fc.to = g_world.findAirport("CAP"); fc.wps.clear(); fc.hints.clear();
        g.startFlight(sp == 0 ? g_story[0] : fc, sp, sp == 0 ? Career::SRC_LESSON : Career::SRC_RENT);
        for (int i = 0; i < 60 * 8; i++) { g.plane.ctl.throttle = 1.f; g.update(dt); }
        float held = length(g.plane.vel);
        printf("Parking brake at the start (%s): running=%d brake=%.1f speed after 8 s at full power %.2f m/s\n", kAircraft[sp].name, g.plane.engineRunning, g.plane.ctl.brake, held);
        // set on every aircraft, and it holds full power on all of them (a propeller's static thrust is limited by its
        // disk, Plane::substep, so even the STOL types don't drag their brakes)
        if (!(g.plane.engineRunning && g.plane.ctl.brake > 0.99f && held < 0.5f)) fails++;
      }
      g.botControl = true;
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
int main() { return GameTest::run(); }
