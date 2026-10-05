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
        float held = length(vec3(g.plane.vel.x, 0.f, g.plane.vel.z));
        printf("Parking brake at the start (%s): running=%d brake=%.1f speed after 8 s at full power %.2f m/s\n", kAircraft[sp].name, g.plane.engineRunning, g.plane.ctl.brake, held);
        // set on every aircraft, and it holds full power on all of them (a propeller's static thrust is limited by its
        // disk, Plane::substep, so even the STOL types don't drag their brakes)
        if (!(g.plane.engineRunning && g.plane.ctl.brake > 0.99f && held < 0.02f)) fails++;   // (static friction: no creep)
      }
      g.botControl = true;
    }
    // ---- E1: the quote is honest. A4 (the Minister's Jet, an owned Starling) flown on the autopilot after a scripted
    // takeoff lands within the quoted time +- its uncertainty; the job card's plan is the one the flight settles with
    {
      Career keep = g.career;
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
      g.career = keep;
    }
    // ---- E1: lesson hints name the keys bound now, and no recording names a key the player rebound; the ATC history
    // stays bounded over long sessions
    {
      const std::string raw = g_story[0].hints[0];
      std::string def = g.expandHint(raw);
      int keep = g.set.keyBind[ACT_PARK]; g.set.keyBind[ACT_PARK] = 'P';
      std::string reb = g.expandHint(raw);
      g.set.keyBind[ACT_PARK] = keep;
      AtcVoice::Tx tx;
      bool defVoiced = !voices || g.atc.resolve(def, "L1", false, tx) || g.atc.resolve(def, "L1", true, tx);
      bool rebVoiced = voices && (g.atc.resolve(reb, "L1", false, tx) || g.atc.resolve(reb, "L1", true, tx));
      bool ok = def.find("Press B ") != std::string::npos && reb.find("Press P ") != std::string::npos && reb.find("Press B ") == std::string::npos && !rebVoiced && defVoiced;
      printf("Hint after rebinding the parking brake: \"%s\" voiced %d (default voiced %d): %s\n", reb.c_str(), rebVoiced, defVoiced, ok ? "ok" : "FAIL");
      fails += !ok;
      printf("ATC history after the flights so far: %zu lines (limit %zu): %s\n", g.atc.history.size(), g.atc.historyLimit, g.atc.history.size() <= 64 ? "ok" : "FAIL");
      fails += g.atc.history.size() > 64;
    }
    // ---- E5: input contexts and re-arming. A held from the menu into the flight doesn't brake until it's released
    // and pressed again; A held across leaving the pause menu doesn't either; LB+RB doesn't hide the UI while the
    // XR-11's weapons are armed; losing the controller in flight pauses it
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
      // the XR-11 armed: both bumpers held 1.5 s
      g.resCraft = kWraith; g.resAirborne = true; g.launchResearch(); g.wraith.armed = true;
      bool hid0 = g.uiHidden;
      for (int i = 0; i < 90; i++) { pad(PAD_LB | PAD_RB, i == 0 ? (PAD_LB | PAD_RB) : 0); g.update(dt); }
      bool ok4 = g.uiHidden == hid0;
      printf("LB+RB held with the XR-11 armed: UI hidden %d -> %d: %s\n", hid0, g.uiHidden, ok4 ? "ok" : "FAIL"); fails += !ok4;
      pad(0, 0); g.in.pad = false; g.update(dt); g.paused = false;
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
    // ---- transactions: a settlement that can't be saved leaves the career untouched and pending; the retry saves it
    //      once and a second retry can't pay again
    {
      Game& q = g;
      q.career.newGame(); q.career.money = 5000; q.career.license = LIC_PPL; q.career.location = g_story[4].from;
      q.pendingCareer.reset();
      q.saveDir = "no_such_dir_gameplay/x";   // (a save folder that can't be written)
      Contract c = g_story[4]; c.wx = Weather();
      q.beginCareerFlight(c, 1, Career::SRC_RENT);
      bool flagged = !q.career.attemptOpen && q.commitBlocked();   // the attempt marker couldn't be saved either: pending, the career as before
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
      // a lesson is never a job
      fresh(); q.beginCareerFlight(g_story[0], 0, Career::SRC_LESSON); bool noJob = !q.career.job; q.endFlight(false, "x", OUT_ABANDONED);
      remove("career.sav"); remove("career.sav.bak"); q.saveDir.clear(); q.pendingCareer.reset(); q.career.newGame();
      bool ok = accepted && leg1 && reloaded && leg2start && delivered && paidOnce && released && crashEnds && noJob;
      if (!leg1) printf("  leg1: job %d state %d at %d legs %d loc %d money %d (want %d) rep %d maxG %.2f hirePaid %d\n", (int)(bool)q.career.job, q.career.job ? (int)q.career.job->state : -1, q.career.job ? q.career.job->at : -1, q.career.job ? q.career.job->legs : -1, q.career.location, q.career.money, m0 - hire, q.career.reputation, q.career.job ? q.career.job->maxG : 0.f, q.career.job ? (int)q.career.job->hirePaid : 0);
      printf("Resumable job: accepted %d, leg closed %d, reloaded %d, continued %d, delivered %d, paid once %d, released %d, crash ends %d, lesson no job %d: %s\n",
             accepted, leg1, reloaded, leg2start, delivered, paidOnce, released, crashEnds, noJob, ok ? "ok" : "FAIL");
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
    // ---- engine-out landing (C7): the engine stops on a 2.2 km final in an owned Wren; the pilot glides it in. The
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
      vec3 start = thr - dir * 2200.f; start.y = a.elev + 300.f;
      g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref * 1.2f);
      g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3;
      g.plane.ctl.throttle = 0.5f; g.update(dt);
      g.fireFailure(FAIL_ENGINE_TOTAL, 0);
      bool glide = g.plane.glideOnly() && !g.plane.engineRunning && (g.result.failureKinds & (1 << FAIL_ENGINE_TOTAL));
      s_pI = -2.f; float tdFpm = 0;
      for (t = 0; t < 240 && g.screen == SCR_FLIGHT; t += dt) {
        Plane& p = g.plane;
        vec3 rel = p.pos - thr;
        float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
        float agl = p.pos.y - a.elev;
        if (!p.onGround && !g.touchedDown) {
          float hdgT = a.heading - clampf(lat * 0.08f, -20, 20), herr = wrapAngle((hdgT - p.heading()) * DEG) / DEG;
          p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
          // the glide: the speed decides the pitch (best glide, then Vref over the fence), flaps only when the field is made
          float vT = -along > 900.f ? p.spec->vref * 1.15f : p.spec->vref * 1.05f;
          if (-along < 700.f && agl < 120.f) { p.ctl.flaps = 1.f; g.flapNotch = 1.f; }
          float vsT = agl < 7.f ? -0.6f : clampf(-4.f + (vT - p.ias) * -0.6f, -8.f, -0.5f);
          pitchFor(p, vsT, dt);
          p.ctl.throttle = 1.f;   // (nothing answers)
          p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
        } else { p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = 0; p.ctl.roll = 0; p.ctl.yaw = 0; }
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
        g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f;
        for (t = 0; t < 300 && g.screen == SCR_FLIGHT; t += dt) {
          Plane& p = g.plane;
          vec3 rel = p.pos - thr;
          float along = dot(vec3(rel.x, 0, rel.z), dir), lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
          float agl = p.pos.y - a.elev;
          if (!p.onGround && !g.touchedDown) {
            float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
            float hdgT = a.heading - clampf(lat * 0.08f, -20, 20), herr = wrapAngle((hdgT - p.heading()) * DEG) / DEG;
            p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
            float vsT = agl < 7.f ? -0.7f : clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1);
            if (rough && t > 2.f && t < 6.5f) p.ctl.roll = clampf((48.f - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);   // (a steep bank the patient feels)
            pitchFor(p, vsT, dt);
            p.ctl.throttle = agl < 6.f ? 0.f : clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
            p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
          } else { p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = 0; p.ctl.roll = 0; p.ctl.yaw = 0; }
          g.update(dt);
        }
        bool bonus = false, hit = false;
        for (auto& l : g.payout) { if (l.label.find("good shape") != std::string::npos) bonus = true; if (l.label.find("patient") != std::string::npos || l.label.find("Patient in") != std::string::npos) hit = true; }
        bool ok = g.screen == SCR_DEBRIEF && (rough ? g.result.patient < 0.9f : (g.lastSuccess && g.result.patient > 0.9f && bonus && !hit));
        printf("Medevac flight (%s): touchdown %.0f fpm, patient %.0f%%, bonus %d: %s\n", rough ? "steep bank" : "gentle", g.touchdownFpm, g.result.patient * 100.f, bonus, ok ? "ok" : "FAIL"); fails += !ok;
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
