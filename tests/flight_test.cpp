#include <string>
// Headless flight-model checks: take-off roll, climb, cruise, stall per aircraft.
#include "../src/aircraft.h"
#include "test_world.h"
#include <cstdlib>
int main(int argc, char** argv) {
  buildTestWorld();
  if (argc > 1 && std::string(argv[1]) == "--table") {   // the README's aircraft table, from the learned performance
    printf("| Aircraft | Type | Seats / Cargo | Range | Cruise | Take-off roll | Landing distance | Runway | Licence |\n|---|---|---|---|---|---|---|---|---|\n");
    static const char* lic[] = {"Student", "PPL", "CPL", "ATP"};
    for (int i : kCareerAircraft) {
      const AircraftSpec& s = kAircraft[i]; const PerfModel& P = Plane::perf(&s);
      printf("| %s | %s | %d / %.0f kg | %.0f km | %.0f kt | %.0f m | %.0f m | %.0f m%s | %s |\n", s.name, s.role, s.pax, s.cargoKg, s.rangeKm, P.cruiseV * MS_TO_KT,
             P.toRoll, P.ldgRoll, s.runwayNeeded(0), s.roughOK ? ", gravel/snow" : " paved", lic[s.license]);
    }
    return 0;
  }
  // --part 1..3: the checks in three pieces CI's sanitizer runners take side by side (0, the default: all of it)
  //   1 the take-offs, the controls, the failures, the research jets   2 the map's seam, the autopilot's holds and its
  //   comfort law   3 the autolands, the aerobatics, the review's flights, the weather
  int part = 0;
  for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--part")) part = atoi(argv[i + 1]);
  auto in = [&](int k) { return part == 0 || part == k; };
  Weather wx; wx.windSpeed = 0; wx.gust = 0; wx.turbulence = 0;
  int fails = 0;
  // FLIGHT_QUICK (the sanitizer job, where every step runs several times slower): the checks made type by type run on
  // the types that between them fly every kind of airframe and engine - a piston single, the twin, the turboprop
  // airliner, the business jet and the research jets - instead of all of them; the research tiers at their lowest and
  // highest altitude, the comfort routes in a single and the jet, the autoland's failures as an engine out and ice.
  // Every section still runs; the full set is the Windows job's and every local run's.
  const bool quick = getenv("FLIGHT_QUICK") != nullptr;
  auto skip = [&](int i) { return quick && i != 0 && i != 3 && i != 5 && i != 6 && i != kResearchJet && i != kWraith; };
  if (strcmp(kAircraft[kOsprey].id, "osprey_c6") != 0 || strcmp(kAircraft[kNightjar].id, "xr10_nightjar") != 0 || strcmp(kAircraft[kResearchJet].id, "xr30_specter") != 0 || strcmp(kAircraft[kMantis].id, "xr20_mantis") != 0 || strcmp(kAircraft[kWraith].id, "xr40_wraith") != 0) { printf("aircraft indices (kOsprey / kNightjar / kResearchJet / kMantis / kWraith) don't match the table\n"); return 1; }
  if (in(1)) for (int ai : kCareerAircraft) {
    if (skip(ai)) continue;
    const AircraftSpec& s = kAircraft[ai];
    int apIdx = g_world.findAirport("CAP");
    const Airport& a = g_world.airports[apIdx];
    Plane p; vec3 start = a.threshold(false) + a.dir() * 30.f;
    float gh = 3.0f; start.y = a.elev + gh;
    p.reset(&s, start, a.heading, s.maxFuel, s.cargoKg * 0.8f, false);
    p.starterTime = 0.01f;
    float t = 0, dt = 1.f / 60.f; float liftoffDist = -1, vLift = 0;
    // settle + start engine
    for (; t < 4; t += dt) p.step(dt, wx, t);
    p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = s.taildragger ? 0.3f : s.takeoffFlap;
    vec3 p0 = p.pos;
    for (; t < 90; t += dt) {
      if (p.ias > s.vr) p.ctl.pitch = clampf((10.f - p.pitchDeg()) * 0.08f - p.w.x * 0.5f, -1, 1);
      else if (s.taildragger && p.ias > s.vr * 0.5f) p.ctl.pitch = -0.3f;
      p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
      p.ctl.yaw = clampf(wrapAngle((a.heading - p.heading()) * DEG) * 3.f, -1, 1);
      p.step(dt, wx, t);
      if (p.ev.crashed) break;
      if (!p.onGround && liftoffDist < 0 && p.agl() > 2) { liftoffDist = length(p.pos - p0); vLift = p.ias; }
      if (p.agl() > 15 && p.agl() < 20) p.ctl.gearDown = false;
      if (p.agl() > 150) break;
    }
    float climbVs = p.vel.y;
    // level cruise at 75% power, autopilot altitude hold
    p.ctl.flaps = 0; p.ctl.throttle = 0.75f; p.apOn = true; p.apAlt = p.pos.y + 200; p.apHeading = p.heading();
    for (float tt = 0; tt < 180 && !p.ev.crashed; tt += dt, t += dt) { p.step(dt, wx, t); if (getenv("DBG") && ai == atoi(getenv("DBG")) && fmodf(tt, 2.f) < dt) printf("  t%3.0f alt %6.0f vs %5.1f ias %5.1f a %5.1f pitchCtl %5.2f bank %5.1f thr %.2f\n", tt, p.pos.y, p.vel.y, p.ias, p.alpha / DEG, p.ctl.pitch, p.bankDeg(), p.engineSpool); }
    float cruiseV = p.ias, cruiseAlpha = p.alpha / DEG;
    float vStall = sqrtf(2 * p.mass() * G0 / (1.225f * s.wingArea * s.CLmax));
    printf("%-16s cd0 %.4f  liftoff %5.0fm @%4.1fm/s (need %4.0f)  climb %5.1fm/s  cruise %5.1fm/s (spec %3.0f) a=%4.1f  stall %4.1f  fuelRange %.0fkm %s\n",
           s.name, p.cd0Value(), liftoffDist, vLift, s.runwayM, climbVs, cruiseV, s.cruise, cruiseAlpha, vStall, p.rangeLeftKm(), p.ev.crashed ? p.ev.crashReason.c_str() : "");
    if (p.ev.crashed || liftoffDist < 0 || liftoffDist > s.runwayM * 0.9f) fails++;
  }
  // Control-direction check: each input must move the aircraft the way its control surface animates
  // (roll +1 = right aileron up -> right bank, pitch +1 = elevator TE up -> nose up, yaw +1 = rudder TE right -> nose right)
  if (in(1)) for (int ai : kCareerAircraft) {
    if (skip(ai)) continue;
    const AircraftSpec& s = kAircraft[ai];
    for (int axis = 0; axis < 3; axis++) {
      Plane p; p.reset(&s, vec3(0, 1500, 0), 90, s.maxFuel * 0.5f, 0, true, s.cruise * 0.9f);
      float h0 = p.heading(), b0 = p.bankDeg(), pt0 = p.pitchDeg();
      for (float t = 0; t < 1.0f; t += 1.f / 120.f) {
        p.ctl.roll = axis == 0 ? 1.f : 0.f; p.ctl.pitch = axis == 1 ? 0.6f : 0.f; p.ctl.yaw = axis == 2 ? 1.f : 0.f;
        p.step(1.f / 120.f, wx, t);
      }
      float d = axis == 0 ? p.bankDeg() - b0 : axis == 1 ? p.pitchDeg() - pt0 : wrapAngle((p.heading() - h0) * DEG) / DEG;
      bool ok = d > (axis == 2 ? 0.3f : 1.0f);
      if (!ok) { printf("CONTROL DIRECTION FAIL %s axis %d delta %.2f\n", s.name, axis, d); fails++; }
    }
  }
  if (in(1)) printf("control directions checked\n");
  // ---------------- failures (C7): an engine-out glide reaches a field, a stuck gear belly landing at Vref is survivable,
  // a twin flies on one engine, a blocked pitot freezes the airspeed, ice costs lift
  if (in(1)) {
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    // Kestrel, engine stopped at 1200 m over flat ground: glide at the best-glide speed and measure the ratio
    const AircraftSpec& s = kAircraft[0];
    Plane p; p.reset(&s, vec3(-6000, 1200 + std::max(g_world.height(-6000, 16000), 0.f), 16000), 0, s.maxFuel * 0.5f, 85, true, s.cruise * 0.8f);
    p.ctl.throttle = 0.6f; for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, i / 240.f);
    bool ok = p.failNow(FAIL_ENGINE_TOTAL, 0) && p.glideOnly();
    float vg = s.vref * 1.15f, y0 = p.pos.y; vec3 p0 = p.pos; float tgl = 0;
    for (int i = 0; i < 90 * 240 && !p.ev.crashed && p.agl() > 400; i++) {
      p.ctl.throttle = 1;   // (the lever does nothing: the engine has stopped)
      p.ctl.pitch = clampf((vg - p.ias) * -0.04f - p.w.x * 0.8f, -1, 1);
      p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
      p.step(1 / 240.f, calm, i / 240.f); tgl = i / 240.f;
    }
    float ratio = length(vec3(p.pos.x - p0.x, 0, p.pos.z - p0.z)) / std::max(y0 - p.pos.y, 1.f);
    ok = ok && !p.ev.crashed && !p.engineRunning && p.engineSpool < 0.01f && ratio > 7.f && ratio < 16.f && fabsf(ratio - p.glideRatio()) < p.glideRatio() * 0.35f;
    printf("Engine-out glide (Kestrel): %.1f:1 over %.0f s, model says %.1f:1 %s\n", ratio, tgl, p.glideRatio(), ok ? "ok" : "FAIL"); fails += !ok;
    // Swift S6 with the gear stuck up: a belly landing on Solace Capital's runway at Vref, wings level, is survivable
    const AircraftSpec& sw = kAircraft[7];
    const Airport& a = g_world.airports[g_world.findAirport("CAP")];
    vec3 st = a.threshold(false) + a.dir() * 300.f; st.y = a.elev + 6.f;
    p.reset(&sw, st, a.heading, sw.maxFuel * 0.3f, 85, true, sw.vref);
    p.ctl.gearDown = false; p.gear = 0; ok = p.failNow(FAIL_GEAR_STUCK, 0) && p.fail.gearStuck == 1;
    p.ctl.gearDown = true; p.ctl.flaps = 1; p.flaps = 1; p.ctl.throttle = 0;
    float tstop = -1, pitchI = 0;
    for (int i = 0; i < 90 * 240 && !p.ev.crashed; i++) {
      // (the pilot glides in at Vref holding an attitude - nose up when fast, down when slow - trimming as it goes,
      // and flares from 4 m, easing the sink to a touch)
      if (p.ev.bellyLanding) p.ctl.pitch = 0.f;
      else {
        float err = p.agl() > 4.f ? clampf(-3.f + (p.ias - sw.vref) * 0.5f, -8.f, 8.f) - p.pitchDeg() : (std::max(-0.8f, -p.agl() * 0.3f) - p.vel.y) * 3.f;
        pitchI = clampf(pitchI + err * 0.02f / 240.f, -0.6f, 0.6f);
        p.ctl.pitch = clampf(err * 0.08f - p.w.x * 0.8f + pitchI, -1, 1);
      }
      p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
      p.ctl.yaw = clampf(wrapAngle((a.heading - p.heading()) * DEG) * 2.f, -1, 1);
      p.step(1 / 240.f, calm, i / 240.f);
      if (p.ev.bellyLanding && length(p.vel) < 0.5f) { tstop = i / 240.f; break; }
    }
    ok = ok && !p.ev.crashed && p.ev.bellyLanding && p.gear < 0.01f && tstop > 0 && g_world.onRunway(p.pos.x, p.pos.z, 10) >= 0;
    printf("Stuck-gear belly landing (Swift S6): %s, stopped after %.0f s %s %s\n", p.ev.bellyLanding ? "skidded" : "no skid", tstop, p.ev.crashReason.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
    // the same landing on a gravel strip, or too fast, wrecks it
    const Airport& hf = g_world.airports[g_world.findAirport("HFS")];
    st = hf.threshold(false) + hf.dir() * 100.f; st.y = hf.elev + 6.f;
    p.reset(&sw, st, hf.heading, sw.maxFuel * 0.3f, 85, true, sw.vref); p.ctl.gearDown = false; p.gear = 0; p.failNow(FAIL_GEAR_STUCK, 0); p.ctl.throttle = 0; p.ctl.flaps = 1; p.flaps = 1;
    for (int i = 0; i < 30 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((-0.8f - p.vel.y) * 0.25f - p.w.x * 0.8f + 0.08f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); }
    ok = p.ev.crashed && !p.ev.bellyLanding;
    printf("Belly landing on gravel is a crash: %s %s\n", p.ev.crashReason.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
    // Islander with the right engine failed: it yaws into the dead engine; with rudder it holds heading and height
    const AircraftSpec& tw = kAircraft[3];
    p.reset(&tw, vec3(-6000, 1500, 16000), 0, tw.maxFuel * 0.5f, 300, true, tw.cruise * 0.85f);
    p.ctl.throttle = 0.75f; for (int i = 0; i < 480; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = p.failNow(FAIL_ENGINE_TOTAL, 1) && !p.glideOnly() && p.engineRunning;
    float yawFree = 0; { Plane q = p; for (int i = 0; i < 480; i++) q.step(1 / 240.f, calm, i / 240.f); yawFree = wrapAngle((q.heading() - p.heading()) * DEG) / DEG; }
    float h0 = p.heading(), y0b = p.pos.y;
    for (int i = 0; i < 40 * 240 && !p.ev.crashed; i++) {
      p.ctl.throttle = 1;
      p.ctl.yaw = clampf(wrapAngle((h0 - p.heading()) * DEG) * 3.f + p.w.y * 2.f, -1, 1);
      p.ctl.roll = clampf((-3.f - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);   // a few degrees into the live engine
      p.ctl.pitch = clampf((0.f - p.vel.y) * 0.1f - p.w.x * 0.8f, -1, 1);
      p.step(1 / 240.f, calm, i / 240.f);
    }
    float herr = wrapAngle((p.heading() - h0) * DEG) / DEG;
    ok = ok && !p.ev.crashed && yawFree > 2.f && fabsf(herr) < 8.f && p.pos.y > y0b - 150.f && p.engineSpool < 0.6f;
    printf("Twin engine-out (Islander, right): hands-off yaw %+.1f deg in 2 s, held %+.1f deg, height %+.0f m, power %.2f %s\n", yawFree, herr, p.pos.y - y0b, p.engineSpool, ok ? "ok" : "FAIL"); fails += !ok;
    {   // ...and on one engine it burns half the fuel at the same throttle (the review of v3.44.0, FLT-7: it burned both's)
      Plane a; a.reset(&tw, vec3(-6000, 1500, 16000), 0, tw.maxFuel * 0.5f, 300, true, tw.cruise * 0.85f);
      Plane b = a; b.failNow(FAIL_ENGINE_TOTAL, 1);
      const float fa = a.fuel, fb = b.fuel;
      for (int i = 0; i < 5 * 240; i++) { a.ctl.throttle = b.ctl.throttle = 0.75f; a.step(1 / 240.f, calm, i / 240.f); b.step(1 / 240.f, calm, i / 240.f); }
      const float r = (fb - b.fuel) / std::max(fa - a.fuel, 1e-6f);
      ok = fabsf(r - 0.5f) < 0.05f;
      printf("Twin engine-out fuel: one engine burns %.2f of both's %s\n", r, ok ? "ok" : "FAIL"); fails += !ok;
    }
    // blocked pitot: the indication stays where it was as the aircraft slows, and reads higher as it climbs
    p.reset(&s, vec3(-6000, 1200, 16000), 0, s.maxFuel * 0.5f, 85, true, s.cruise); p.ctl.throttle = 0.7f;
    for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, i / 240.f);
    float ias0 = p.ias; ok = p.failNow(FAIL_PITOT, 0);
    p.ctl.throttle = 0.2f; for (int i = 0; i < 20 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((2.f - p.vel.y) * 0.1f - p.w.x * 0.8f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); }
    float trueIas = p.airspeed * sqrtf(p.density / 1.225f);
    ok = ok && !p.ev.crashed && fabsf(p.ias - ias0) < ias0 * 0.06f && trueIas < ias0 - 5.f;
    printf("Blocked pitot: reads %.0f kt (was %.0f), truly %.0f kt %s\n", p.ias * MS_TO_KT, ias0 * MS_TO_KT, trueIas * MS_TO_KT, ok ? "ok" : "FAIL"); fails += !ok;
    // ice: the iced wing stalls at a higher speed and the alternator leaves a battery that runs down
    p.reset(&s, vec3(-6000, 1200, 16000), 0, s.maxFuel * 0.5f, 85, true, s.cruise); p.ctl.throttle = 0.5f;
    float stallClean = sqrtf(2 * p.mass() * G0 / (1.225f * s.wingArea * s.CLmax));
    // (at idle, level as long as it can: the slowest it still flew at, or where the stall warning came on)
    auto stallSpeed = [&](Plane q) { q.ctl.throttle = 0.f; float vmin = 1e9f; for (int i = 0; i < 120 * 240 && !q.ev.crashed; i++) { q.ctl.pitch = clampf((0.f - q.vel.y) * 0.15f - q.w.x * 0.8f, -1, 1); q.ctl.roll = clampf(-q.bankDeg() * 0.05f + q.w.z * 0.3f, -1, 1); q.step(1 / 240.f, calm, i / 240.f); if (q.stallWarn > 0.5f) { vmin = std::min(vmin, q.ias); break; } if (i > 10 * 240) vmin = std::min(vmin, q.ias); } return vmin; };
    float vsClean = stallSpeed(p); p.failNow(FAIL_ICING, 0); p.fail.ice = 1.f; float vsIced = stallSpeed(p);
    ok = vsIced > vsClean * 1.04f && vsIced < stallClean * 1.6f;
    printf("Icing: stall warning at %.0f kt clean, %.0f kt iced %s\n", vsClean * MS_TO_KT, vsIced * MS_TO_KT, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(-6000, 1200, 16000), 0, s.maxFuel * 0.5f, 85, true, s.cruise); p.failNow(FAIL_ALTERNATOR, 0);
    for (int i = 0; i < 600 * 60 && !p.fail.avionicsDark(); i++) { p.ctl.pitch = clampf((0.f - p.vel.y) * 0.1f - p.w.x * 0.8f, -1, 1); p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1); p.ctl.throttle = 0.7f; p.step(1 / 60.f, calm, i / 60.f); }
    ok = p.fail.avionicsDark() && p.flightTime >= 0.f && !p.ev.crashed;
    printf("Alternator failure: battery flat after %.0f s %s\n", p.fail.battery <= 0.f ? 420.f : -1.f, ok ? "ok" : "FAIL"); fails += !ok;
    // a split flap: the left one stops up and the right goes down with the lever. The aircraft rolls left (the right
    // wing lifts more), the aileron holds it level, and the autopilot puts the lever back so the right one matches
    p.reset(&s, vec3(-6000, 1200, 16000), 0, s.maxFuel * 0.5f, 85, true, s.vref * 1.3f); p.ctl.throttle = 0.5f;
    float rollI = 0;   // (with an integral: the split's rolling moment wants a standing aileron)
    auto level = [&](bool ail) { p.ctl.pitch = clampf((0.f - p.vel.y) * 0.1f - p.w.x * 0.8f, -1, 1); rollI = ail ? clampf(rollI - p.bankDeg() * 0.05f / 240.f, -1, 1) : 0.f; p.ctl.roll = ail ? clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f + rollI, -1, 1) : 0.f; };
    for (int i = 0; i < 240; i++) { level(true); p.step(1 / 240.f, calm, i / 240.f); }
    ok = p.failNow(FAIL_FLAP_ASYM, 0) && p.flapLeft() == 0.f;
    p.ctl.flaps = 1.f; const float bk0 = p.bankDeg();
    for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) { level(false); p.step(1 / 240.f, calm, i / 240.f); }
    const float rollFree = p.bankDeg() - bk0, split = p.flaps - p.flapLeft();
    float ailHold = 0; int nAil = 0;
    for (int i = 0; i < 15 * 240 && !p.ev.crashed; i++) { level(true); p.step(1 / 240.f, calm, i / 240.f); if (i > 10 * 240) { ailHold += p.ctl.roll; nAil++; } }
    ailHold /= std::max(nAil, 1);
    const float bankHeld = p.bankDeg();
    p.apEngage(Plane::AP_HOLD, -1, calm);
    for (int i = 0; i < 20 * 240 && !p.ev.crashed; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = ok && !p.ev.crashed && rollFree < -3.f && split > 0.5f && fabsf(bankHeld) < 3.f && ailHold > 0.05f && ailHold < 0.9f &&
         fabsf(p.flaps - p.flapLeft()) < 0.02f && fabsf(p.bankDeg()) < 3.f;
    printf("Split flap (left stuck up, split %.2f, held at %+.1f deg): rolls %+.1f deg in 3 s, held with %.0f%% aileron; the autopilot matches the lever (%.2f / %.2f), bank %+.1f %s\n",
           split, bankHeld, rollFree, ailHold * 100.f, p.flapLeft(), p.flaps, p.bankDeg(), ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- XR-20's centerline engine: the former pair's real combined thrust, with one failure channel.
  if (in(1)) {
    const AircraftSpec& single = kAircraft[kMantis];
    AircraftSpec twin = single; twin.engines = 2; twin.power = 44000.f;   // delivered two-engine proposal
    Plane p, old;
    p.spec = &single; old.spec = &twin;
    bool ok = single.engines == 1 && single.power == 88000.f && single.special == 0;
    float maxThrustDelta = 0.f; int samples = 0;
    for (float alt : {0.f, 1500.f, 3000.f, 8000.f, 12000.f, 18000.f})
      for (float spool : {0.f, 0.2f, 0.5f, 0.75f, 0.85f, 0.9f, 0.95f, 1.f})
        for (float speed : {0.f, 60.f, 200.f, 340.f, 600.f, 850.f, 1000.f}) {
          float rho = 1.225f * expf(-alt / 8500.f);
          maxThrustDelta = std::max(maxThrustDelta, fabsf(p.thrustAt(spool, speed, speed, rho) - old.thrustAt(spool, speed, speed, rho)));
          samples++;
        }
    ok = ok && maxThrustDelta == 0.f && fabsf(p.thrustAt(1.f, 0.f, 0.f, 1.225f) - 176000.f) < 0.1f
            && p.fuelFlowMax() == old.fuelFlowMax() && fabsf(p.fuelFlowMax() - 0.990f) < 0.00001f
            && p.spoolRate() == old.spoolRate() && p.spoolRate() == 0.45f;
    printf("XR-20 single core: %d thrust samples, maximum pair delta %.3f N, static reheat %.0f N, fuel %.3f kg/s %s\n",
           samples, maxThrustDelta, p.thrustAt(1.f, 0.f, 0.f, 1.225f), p.fuelFlowMax(), ok ? "ok" : "FAIL"); fails += !ok;
    auto reset = [&](Plane& v, const AircraftSpec* s) {
      v.reset(s, vec3(-39000, 8000, 35000), 0, single.maxFuel * 0.7f, 85, true, 200);
      v.sceneryHits = false; v.gear = 0; v.ctl.gearDown = false; v.ctl.throttle = 1;
    };
    reset(p, &single); reset(old, &twin);
    p.fail.engineHealth[1] = p.fail.engineHealth[2] = p.fail.engineHealth[3] = 0.f;   // nonexistent engines cannot dilute its power
    for (int i = 0; i < 120; i++) { p.step(1 / 60.f, wx, i / 60.f); old.step(1 / 60.f, wx, i / 60.f); }
    p.apSense();
    ok = p.engineRunning && !p.glideOnly() && p.engineSpool == old.engineSpool && p.fuel == old.fuel
         && fabsf(p.apEnv.spool - 1.f / 0.45f) < 0.00001f;
    printf("XR-20 one-channel health: unused slots ignored, spool %.6f, equal fuel burn, AP response %.3f s %s\n",
           p.engineSpool, p.apEnv.spool, ok ? "ok" : "FAIL"); fails += !ok;
    reset(p, &single);
    ok = p.failNow(FAIL_ENGINE_PARTIAL, 0) && p.fail.engineHealth[0] == 0.45f && p.engineRunning;
    for (int i = 0; i < 15 * 60; i++) p.step(1 / 60.f, wx, i / 60.f);
    p.apSense();
    ok = ok && !p.ev.crashed && !p.glideOnly() && p.engineSpool > 0.449f && p.engineSpool < 0.451f
         && p.apEnv.thrustFrac > 0.f && p.apEnv.thrustFrac < 0.25f;
    printf("XR-20 partial core loss: health %.2f, spool %.5f, AP thrust fraction %.3f %s\n",
           p.fail.engineHealth[0], p.engineSpool, p.apEnv.thrustFrac, ok ? "ok" : "FAIL"); fails += !ok;
    reset(p, &single); reset(old, &twin); Plane stopped = p;
    stopped.engineRunning = false; stopped.starterTime = 0;
    float fuelBefore = p.fuel;
    ok = p.failNow(FAIL_ENGINE_TOTAL, 1) && p.glideOnly() && !p.engineRunning && p.fail.engineHealth[0] == 0.f;
    ok = old.failNow(FAIL_ENGINE_TOTAL, 1) && old.engineRunning && !old.glideOnly() && ok;
    for (int i = 0; i < 120; i++) { p.step(1 / 60.f, wx, i / 60.f); stopped.step(1 / 60.f, wx, i / 60.f); old.step(1 / 60.f, wx, i / 60.f); }
    float singleYaw = wrapAngle(p.heading() * DEG) / DEG, twinYaw = wrapAngle(old.heading() * DEG) / DEG;
    p.apSense();
    ok = ok && !p.ev.crashed && !p.engineRunning && p.fuel == fuelBefore && p.apEnv.thrustFrac == 0.f && !p.apEnv.canGoAround
         && length(p.vel - stopped.vel) == 0.f && length(p.w - stopped.w) == 0.f
         && fabsf(singleYaw) < 0.01f && fabsf(twinYaw) > 0.1f;
    p.starterTime = 0.01f; p.step(1 / 60.f, wx, 3.f);
    ok = ok && !p.engineRunning && p.starterTime == 0.f;   // cranking cannot revive a failed core
    printf("XR-20 total core loss: zero powered force/fuel, AP glide only, yaw %+.3f deg (prior twin %+.3f), restart inhibited %s\n",
           singleYaw, twinYaw, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- research tiers: actual sustained level flight, not designMach labels or a diving peak.
  // designMach is the onset of extra drag, not a hard speed cap. Keep the XR-30 / XR-40 physics unchanged.
  if (in(1)) {
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    struct LevelResult { float mach = 0, minAlt = 1e9f, maxAlt = -1e9f, vs = 0, accel = 0; bool intact = true; };
    auto levelMach = [&](int idx, float alt) {
      const AircraftSpec& s = kAircraft[idx];
      Plane p; p.reset(&s, vec3(-39000, alt, 35000), 90, s.maxFuel * 0.7f, 0, true, 200);
      p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0; p.sceneryHits = false;
      LevelResult r; float integral = 0, vFrom = 0; int n = 0;
      for (int i = 0; i < 240 * 60 && !p.ev.crashed; i++) {
        float error = alt - p.pos.y;
        integral = clampf(integral + error / 60.f, -7500.f, 7500.f);   // (enough to hold the supersonic trim change: the lift's centre moves aft)
        p.ctl.pitch = clampf(error * 0.002f - p.vel.y * 0.01f - p.w.x * 0.3f + integral * 0.00004f, -1.f, 1.f);
        p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1.f, 1.f);
        p.step(1 / 60.f, calm, i / 60.f);
        // Recycle horizontal position over the same open sea so chart bounds do not truncate a sustained test.
        // All flight state (altitude, attitude, velocity, fuel, mass, controls and forces) continues normally.
        p.pos.x = -39000; p.pos.z = 35000;
        if (i == 210 * 60) vFrom = p.airspeed;
        if (i >= 210 * 60) { r.mach += p.mach; r.vs += p.vel.y; r.minAlt = std::min(r.minAlt, p.pos.y); r.maxAlt = std::max(r.maxAlt, p.pos.y); n++; }
      }
      r.intact = !p.ev.crashed && p.fuel > 0 && n == 30 * 60;
      if (n) { r.mach /= n; r.vs /= n; } r.accel = (p.airspeed - vFrom) / 30.f;
      return r;
    };
    bool ok = true;
    for (float alt : {1500.f, 3000.f, 8000.f, 12000.f}) {
      if (quick && (alt == 3000.f || alt == 8000.f)) continue;   // (quick: the lowest and the highest)
      LevelResult n = levelMach(kNightjar, alt), m = levelMach(kMantis, alt), s = levelMach(kResearchJet, alt), w = levelMach(kWraith, alt);
      bool stable = true;
      for (const auto& r : {n, m, s, w}) stable = stable && r.intact && fabsf(r.vs) < 0.3f && fabsf(r.accel) < 0.15f && r.minAlt > alt - 40.f && r.maxAlt < alt + 40.f;
      bool tier = stable && n.mach > 1.15f && n.mach < 1.65f && m.mach > 1.75f && m.mach < 2.25f && m.mach > n.mach + 0.35f
                && s.mach > 2.65f && s.mach < 3.1f && s.mach > m.mach + 0.55f && w.mach > 4.f && w.mach > s.mach + 0.8f;
      printf("Sustained research tiers at %.1f km: XR-10 M%.3f XR-20 M%.3f XR-30 M%.3f XR-40 M%.3f; level/settled %s %s\n", alt / 1000.f, n.mach, m.mach, s.mach, w.mach, stable ? "yes" : "NO", tier ? "ok" : "FAIL");
      ok = ok && tier;
    }
    fails += !ok;
    // the overstress: the XR-30 (limited to 40 g) rides 15% over for four seconds while the stress builds (it bleeds
    // its speed before the airframe gives), and a full pull at Mach 3 takes it past twice the limit: that snaps it
    const AircraftSpec& s9 = kAircraft[kResearchJet];
    Plane p; p.reset(&s9, vec3(0, 6000, 0), 90, s9.maxFuel * 0.5f, 85, true, 700); p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0;
    float gmax = 0, ogMax = 0;
    for (int i = 0; i < 4 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((50.f - p.gLoad) * 0.1f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); gmax = std::max(gmax, p.gLoad); ogMax = std::max(ogMax, p.overG); }
    bool held = !p.ev.crashed && gmax > 44.f && ogMax > 0.35f && ogMax < 1.f;
    p.reset(&s9, vec3(0, 6000, 0), 90, s9.maxFuel * 0.5f, 85, true, 1000); p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0; p.ctl.pitch = 1;
    float g2 = 0; for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); g2 = std::max(g2, p.gLoad); }
    ok = held && p.ev.crashed && p.ev.crashReason.find("Structural") != std::string::npos;
    printf("Sustained overstress (XR-30): %.1f g held, stress %.2f of the way to failure, intact; full pull at Mach 3 peaks %.0f g and %s %s\n", gmax, ogMax, g2, p.ev.crashed ? "snaps" : "holds", ok ? "ok" : "FAIL"); fails += !ok;
  }
  // XR-30 research jet: supersonic in level flight, no vertical flight, slow flight on approach, pull limits, roll authority
  if (in(1)) {
    const AircraftSpec& s = kAircraft[kResearchJet];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    Plane p;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 200);
    p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0;
    for (int i = 0; i < 40 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((3000 - p.pos.y) * 0.002f - p.vel.y * 0.01f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); }
    bool ok = !p.ev.crashed && p.mach > 2.4f;
    printf("XR-30 level acceleration: Mach %.2f after 40 s %s\n", p.mach, ok ? "ok" : "FAIL"); fails += !ok;
    // no vertical flight: the flap lever does nothing and the nozzles stay aft, so at a standstill it simply falls
    p.reset(&s, vec3(0, 500, 0), 90, s.maxFuel, 85, true, 0); p.vel = vec3(); p.ctl.flaps = 1; p.ctl.throttle = 0.7f;
    for (int i = 0; i < 4 * 240 && !p.ev.crashed; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = p.nozzle == 0.f && p.pos.y < 450.f;
    printf("XR-30 cannot hover: nozzle %.2f, alt %.0f m after 4 s %s\n", p.nozzle, p.pos.y, ok ? "ok" : "FAIL"); fails += !ok;
    // on the runway: full stick in every axis at a standstill leaves it on its wheels (its fly-by-wire tipped it onto
    // its tail or a wingtip - "Struck terrain" - with no airspeed at all), and a full-throttle roll with the stick
    // held back from the start still rotates and climbs away
    {
      const Airport& a = g_world.airports[g_world.findAirport("CAP")];
      vec3 st = a.threshold(false) + a.dir() * 30.f; st.y = a.elev + 3.f;
      Weather still; still.windSpeed = 0; still.turbulence = 0; still.gust = 0;
      bool parked = true;
      for (float sp : {1.f, -1.f}) {
        p.reset(&s, st, a.heading, s.maxFuel, 85, false); p.starterTime = 0.01f;
        float t = 0;
        for (; t < 4; t += 1 / 120.f) p.step(1 / 120.f, still, t);
        p.ctl.pitch = sp; p.ctl.roll = sp; p.ctl.yaw = sp;
        for (; t < 7 && !p.ev.crashed; t += 1 / 120.f) p.step(1 / 120.f, still, t);
        parked = parked && !p.ev.crashed && p.onGround;
      }
      p.reset(&s, st, a.heading, s.maxFuel, 85, false); p.starterTime = 0.01f;
      float t = 0;
      for (; t < 4; t += 1 / 120.f) p.step(1 / 120.f, still, t);
      p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.pitch = 1;
      for (; t < 60 && !p.ev.crashed && p.agl() < 100; t += 1 / 120.f) {
        if (!p.onGround) p.ctl.pitch = clampf((12.f - p.pitchDeg()) * 0.08f - p.w.x * 0.5f, -1, 1);
        p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
        p.step(1 / 120.f, still, t);
      }
      ok = parked && !p.ev.crashed && p.agl() >= 100;
      printf("XR-30 on the runway: full stick parked %s, stick-back takeoff %s (%.0f m AGL) %s\n", parked ? "stays on its wheels" : "CRASHED",
             p.ev.crashed ? p.ev.crashReason.c_str() : "climbs away", p.agl(), ok ? "ok" : "FAIL"); fails += !ok;
    }
    // approach: gear down at about 145 kt it flies level on its wing alone
    p.reset(&s, vec3(0, 600, 0), 90, s.maxFuel, 85, true, 75); p.ctl.gearDown = true; p.gear = 1;
    for (int i = 0; i < 20 * 240 && !p.ev.crashed; i++) {
      p.ctl.pitch = clampf((600 - p.pos.y) * 0.004f - p.vel.y * 0.03f - p.w.x * 0.5f, -1, 1);
      p.ctl.throttle = clampf(0.3f + (75 - length(p.vel)) * 0.05f, 0, 1);
      p.step(1 / 240.f, calm, i / 240.f);
    }
    ok = !p.ev.crashed && fabsf(p.pos.y - 600) < 40 && fabsf(length(p.vel) - 75) < 10;
    printf("XR-30 approach speed level flight: alt %.0f m, %.0f kt %s\n", p.pos.y, length(p.vel) * MS_TO_KT, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.pitch = 1;
    float gmax = 0;
    for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); gmax = std::max(gmax, p.gLoad); }
    ok = !p.ev.crashed && gmax < 50.f && gmax > 10.f;
    printf("XR-30 full-back pull: peak %.1f g %s\n", gmax, ok ? "ok" : "FAIL"); fails += !ok;
    // no g limiter: a full pull at Mach 1.8 overstresses the airframe and it fails
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 620); p.ctl.throttle = 1; p.ctl.pitch = 1;
    for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = p.ev.crashed && p.ev.crashReason.find("Structural") != std::string::npos;
    printf("XR-30 unlimited pull at Mach 1.8 breaks the airframe: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 340); p.ctl.throttle = 1; p.ctl.pitch = 1;
    float rate = 0;
    for (int i = 0; i < 2 * 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); rate = std::max(rate, p.w.x / DEG); }
    ok = !p.ev.crashed && rate > 100.f;
    printf("XR-30 pitch rate at Mach 1: %.0f deg/s %s\n", rate, ok ? "ok" : "FAIL"); fails += !ok;
    // stick snapped from full back to full forward: the vectoring nozzles reverse the pitch rate quickly
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.pitch = 1;
    for (int i = 0; i < 120; i++) p.step(1 / 240.f, calm, i / 240.f);
    p.ctl.pitch = -1; float trev = -1;
    for (int i = 0; i < 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); if (trev < 0 && p.w.x < -60 * DEG) trev = i / 240.f; }
    ok = !p.ev.crashed && trev > 0 && trev < 0.3f;
    printf("XR-30 pitch reversal to -60 deg/s: %.2f s %s\n", trev, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.roll = 1;
    for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = -p.w.z / DEG > 250.f;
    printf("XR-30 roll rate %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- XR-40 Wraith: four-pod VTOL hover, top speed, roll rate and the structural g it can pull
  if (in(1)) {
    const AircraftSpec& s = kAircraft[kWraith];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    Plane p; p.reset(&s, vec3(0, 800, 0), 0, s.maxFuel, 85, true, 0.1f); p.vel = vec3(); p.ctl.flaps = 1; p.flaps = p.nozzle = 1;
    float I = 0.45f;
    for (int i = 0; i < 60 * 30; i++) { I = clampf(I - p.vel.y * 0.004f / 60.f * 60.f * 0.05f, 0.f, 1.f); p.ctl.throttle = clampf(I - p.vel.y * 0.04f, 0.f, 1.f); p.step(1 / 60.f, calm, i / 60.f); }
    bool ok = !p.ev.crashed && fabsf(p.vel.y) < 1.f && fabsf(p.pitchDeg()) < 2.f && fabsf(p.bankDeg()) < 2.f && length(vec3(p.vel.x, 0, p.vel.z)) < 2.f;
    printf("XR-40 hover: throttle %.2f vs %.2f m/s pitch %.1f bank %.1f drift %.1f m/s %s\n", p.ctl.throttle, p.vel.y, p.pitchDeg(), p.bankDeg(), length(vec3(p.vel.x, 0, p.vel.z)), ok ? "ok" : "FAIL"); fails += !ok;
    p.ctl.roll = 0.6f; for (int i = 0; i < 60; i++) p.step(1 / 60.f, calm, 0);
    ok = -p.w.z / DEG > 25.f; printf("XR-40 hover roll rate (pod thrust differential) %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(-46000, 4000, 0), 90, s.maxFuel, 85, true, 300); p.ctl.throttle = 1; p.apEngage(Plane::AP_HOLD, -1, calm); p.apSpeed = 0;
    for (int i = 0; i < 60 * 40; i++) p.step(1 / 60.f, calm, 0);
    ok = !p.ev.crashed && p.mach > 4.0f; printf("XR-40 top speed Mach %.2f at %.0f m %s %s\n", p.mach, p.pos.y, p.ev.crashReason.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.roll = 1;
    for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, 0);
    ok = -p.w.z / DEG > 330.f; printf("XR-40 roll rate %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 600); p.ctl.throttle = 0.8f; p.ctl.pitch = 1; float mg = 0;
    for (int i = 0; i < 480 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, 0); mg = std::max(mg, p.gLoad); }
    ok = !p.ev.crashed && mg > 60.f; printf("XR-40 full pull at 600 m/s: %.0f g %s\n", mg, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- the map wraps (world.h WRAP_HALF): the islands in 10 km of open sea, repeating every 100 km. Over its
  // seam an aircraft comes back in from the other side and flies on as it was - the same flight whichever copy of the
  // map it is reckoned in (its air comes with it: the eddies and gusts go on as they were) - and the autopilot takes the
  // short way across it
  if (in(2)) {
    const AircraftSpec& s = kAircraft[1];
    Weather wx; wx.windSpeed = 7; wx.windFrom = 250; wx.turbulence = 0.3f; wx.gust = 3;
    Plane a; a.reset(&s, vec3(WRAP_HALF - 400.f, 700.f, 3000.f), 90, s.maxFuel * 0.5f, 85, true, s.cruise * 0.9f);
    a.ctl.throttle = 0.7f;
    Plane b = a; b.pos.x -= WRAP_SPAN; b.wxAir.x -= WRAP_SPAN;   // (the same aircraft a period west: its first step brings it in)
    float crossT = -1.f;
    for (int i = 0; i < 60 * 20; i++) {
      const float x0 = a.pos.x;
      a.step(1 / 60.f, wx, i / 60.f); b.step(1 / 60.f, wx, i / 60.f);
      if (crossT < 0.f && a.pos.x < x0 - WRAP_HALF) crossT = i / 60.f;
    }
    const float dp = length(a.pos - b.pos), dv = length(a.vel - b.vel);
    bool ok = crossT > 0.f && !a.ev.crashed && a.seamShift.x == -WRAP_SPAN && b.seamShift.x == 0.f && dp < 0.5f && dv < 0.05f;
    printf("Map seam: crossed at %.1f s, the same flight reckoned a period away (%.3f m, %.4f m/s apart) %s\n", crossT, dp, dv, ok ? "ok" : "FAIL"); fails += !ok;
    // the autopilot, out over the sea east of the islands, sent to the field furthest west: on east, across the seam
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    int west = 0; for (int i = 1; i < (int)g_world.airports.size(); i++) if (g_world.airports[i].x < g_world.airports[west].x) west = i;
    const Airport& W = g_world.airports[west];
    Plane c; c.reset(&s, vec3(WORLD_HALF + 4000.f, 900.f, W.z), 90, s.maxFuel * 0.8f, 85, true, s.cruise);
    c.ctl.throttle = 0.7f; c.apEngage(Plane::AP_APPR, west, calm);
    auto far = [&]() { return length(vec3(wrapCoord(W.x - c.pos.x), 0.f, wrapCoord(W.z - c.pos.z))); };
    const float d0 = far(); float dev = 0.f;
    for (int i = 0; i < 60 * 180 && !c.ev.crashed; i++) { c.step(1 / 60.f, calm, i / 60.f); dev = std::max(dev, fabsf(wrapAngle((c.heading() - 90.f) * DEG) / DEG)); }
    ok = c.apOn && !c.ev.crashed && c.seamShift.x == -WRAP_SPAN && dev < 60.f && far() < d0 - 5000.f;
    printf("Map seam: autopilot to %s (%.0f km the short way) crossed it east %d, turned at most %.0f deg off, %.0f km to go %s\n",
           W.code, d0 / 1000.f, c.seamShift.x == -WRAP_SPAN, dev, far() / 1000.f, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- autopilot: stable holds in turbulence, and autoland at Solace Capital for every aircraft. The
  // autopilot flies each type to its own envelope (steep banks, hard pulls): what's checked is that it gets there, settles,
  // and never goes past the airframe's limits.
  if (in(2)) for (int i : kCareerAircraft) {
    if (skip(i)) continue;
    const AircraftSpec& s = kAircraft[i];
    Weather wx; wx.windSpeed = 7; wx.windFrom = 200; wx.turbulence = 0.25f; wx.gust = 2;
    Plane p; p.reset(&s, vec3(0, 1800, 2000), 30, s.maxFuel * 0.6f, 100, true, s.cruise * 0.85f);
    p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
    p.apEngage(Plane::AP_HOLD, -1, wx);
    float maxBank = 0, rmsP = 0, maxG = 1, minG = 1; int nP = 0;
    for (int k = 0; k < 120 * 60 && !p.ev.crashed; k++) {
      if (k == 10 * 60) { p.apHeading = wrapDeg360(p.apHeading + 90.f); p.apAlt += 200.f; }
      p.step(1 / 60.f, wx, k / 60.f);
      if (k > 10 * 60) { maxBank = std::max(maxBank, fabsf(p.bankDeg())); maxG = std::max(maxG, p.gLoad); minG = std::min(minG, p.gLoad); }
      if (k > 90 * 60) { rmsP += p.w.z * p.w.z; nP++; }
    }
    float he = fabsf(wrapAngle((p.apHeading - p.heading()) * DEG) / DEG), ae = fabsf(p.apAlt - p.pos.y);
    rmsP = sqrtf(rmsP / std::max(nP, 1)) / DEG;
    const PerfModel& P = Plane::perf(&s);
    bool ok = !p.ev.crashed && he < 3.f && ae < 20.f && maxBank < 88.f && maxG < P.gLimit * 0.95f && minG > P.gNeg * 0.95f && rmsP < 4.f;
    printf("AP hold %-16s hdg err %4.1f  alt err %5.1f m  max bank %4.1f  g %+.1f..%+.1f (limits %+.0f..%+.0f)  roll-rate rms %4.2f deg/s  %s\n", s.name, he, ae, maxBank,
           minG, maxG, P.gNeg, P.gLimit, rmsP, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- comfort law (career flights): the autopilot keeps passengers comfortable - bank <= 25 deg,
  // 0.8..1.3 g - in a 150 deg turn on HOLD and on a NAV route to a landing (the final approach keeps its own limits)
  if (in(2)) for (int i : kCareerAircraft) {
    if (skip(i)) continue;
    const AircraftSpec& s = kAircraft[i];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    Plane p; p.reset(&s, vec3(0, 1800, 2000), 30, s.maxFuel * 0.6f, 100, true, s.cruise * 0.85f);
    p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
    p.apComfort = true; p.apEngage(Plane::AP_HOLD, -1, calm);
    float maxBank = 0, maxG = 1, minG = 1;
    for (int k = 0; k < 240 * 60 && !p.ev.crashed; k++) {   // (a 150 deg turn at 25 deg of bank takes the jet ~2.5 min)
      if (k == 10 * 60) { p.apHeading = wrapDeg360(p.apHeading + 150.f); p.apAlt += 150.f; }
      p.step(1 / 60.f, calm, k / 60.f);
      if (k > 10 * 60) { maxBank = std::max(maxBank, fabsf(p.bankDeg())); maxG = std::max(maxG, p.gLoad); minG = std::min(minG, p.gLoad); }
    }
    float he = fabsf(wrapAngle((p.apHeading - p.heading()) * DEG) / DEG);
    bool ok = !p.ev.crashed && he < 3.f && maxBank <= 26.f && maxG <= 1.3f && minG >= 0.8f;
    printf("AP comfort hold %-16s hdg err %4.1f  max bank %4.1f  g %.2f..%.2f  %s\n", s.name, he, maxBank, minG, maxG, ok ? "ok" : "FAIL"); fails += !ok;
    // route to Solace Capital and land: comfortable all the way to the final approach (quick: a single and the jet - the
    // law is the same for every type)
    if (quick && i != 0 && i != 6) continue;
    int ai = g_world.findAirport("CAP"); const Airport& A = g_world.airports[ai];
    vec3 side(-A.dir().z, 0, A.dir().x);
    vec3 start = A.pos() + side * 14000.f + A.dir() * 3000.f; start.y = std::max(A.elev + 1200.f, g_world.height(start.x, start.z) + 500.f);
    Plane q; q.reset(&s, start, wrapDeg360(A.heading + 120.f), s.maxFuel, 100, true, s.cruise * 0.85f);
    q.ctl.gearDown = !s.retract; q.gear = q.ctl.gearDown ? 1.f : 0.f; q.ctl.throttle = 0.7f;
    q.apComfort = true; q.apEngage(Plane::AP_NAV, ai, calm);
    maxBank = 0; maxG = 1; minG = 1;
    int k = 0;
    for (; k < 1500 * 60 && !q.ev.crashed && !q.apDone; k++) {
      q.step(1 / 60.f, calm, k / 60.f);
      if (k > 5 * 60 && q.apStage == Plane::APS_NAV) { maxBank = std::max(maxBank, fabsf(q.bankDeg())); maxG = std::max(maxG, q.gLoad); minG = std::min(minG, q.gLoad); }
    }
    ok = !q.ev.crashed && q.apDone && maxBank <= 26.f && maxG <= 1.3f && minG >= 0.8f;
    printf("AP comfort route %-16s %s after %4.0f s  en-route max bank %4.1f  g %.2f..%.2f  %s\n", s.name, q.apDone ? "landed" : "NOT DONE", k / 60.f, maxBank, minG, maxG, ok ? "ok" : "FAIL");
    fails += !ok;
  }
  if (in(3)) {
    int ai = g_world.findAirport("CAP"); const Airport& A = g_world.airports[ai];
    for (int i : kCareerAircraft) {
      if (skip(i)) continue;
      const AircraftSpec& s = kAircraft[i];
      Weather wx; wx.windSpeed = 6; wx.windFrom = wrapDeg360(A.heading + 25.f); wx.turbulence = 0.15f;
      vec3 side(-A.dir().z, 0, A.dir().x);
      vec3 start = A.pos() + side * 14000.f + A.dir() * 3000.f; start.y = std::max(A.elev + 1200.f, g_world.height(start.x, start.z) + 500.f);
      Plane p; p.reset(&s, start, wrapDeg360(A.heading + 120.f), s.maxFuel, 100, true, s.cruise * 0.85f);
      p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
      p.apEngage(Plane::AP_NAV, ai, wx);
      float tdVs = 0; bool td = false; int k = 0; int goArounds = 0, lastStage = 0;
      for (; k < 1500 * 60 && !p.ev.crashed && !p.apDone; k++) {
        p.step(1 / 60.f, wx, k / 60.f);
        if (p.ev.touchdown && !td) { td = true; tdVs = -p.ev.touchdownVs; }
        if (p.apStage == Plane::APS_GOAROUND && lastStage != Plane::APS_GOAROUND) goArounds++;
        lastStage = p.apStage;
      }
      vec3 rel = p.pos - A.pos(); float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, vec3(-A.dir().z, 0, A.dir().x)));
      bool ok = !p.ev.crashed && p.apDone && td && tdVs < 3.0f && along < A.length * 0.5f && cross < A.width * 0.5f;
      printf("AP autoland %-16s %s after %4.0f s  touchdown %.1f m/s  stop %4.0f m from centre, %4.1f m off the centreline, go-arounds %d %s%s\n", s.name,
             p.apDone ? "landed" : "NOT DONE", k / 60.f, tdVs, along, cross, goArounds, p.ev.crashed ? p.ev.crashReason.c_str() : "", ok ? " ok" : " FAIL");
      fails += !ok;
    }
  }
  // ---------------- the autopilot flies the aircraft as it is now (ApEnvelope): what it reads for an engine out, a full
  // load, ice and no engines at all, and an autoland at Solace Capital in each of those conditions (one go-around at most)
  if (in(3)) {
    int ai = g_world.findAirport("CAP"); const Airport& A = g_world.airports[ai];
    struct Case { int craft; const char* cond; };
    const Case cases[] = {{3, "engine out"}, {5, "engine out"}, {6, "engine out"}, {4, "full load"}, {5, "full load"}, {0, "iced"}, {5, "iced"}};
    std::string seen;   // (quick: each condition once)
    for (const Case& c : cases) {
      if (quick && (seen.find(c.cond[0]) != std::string::npos || c.cond[0] == 'f')) continue;   // (quick: an engine out and ice)
      seen += c.cond[0];
      const AircraftSpec& s = kAircraft[c.craft];
      Weather wx; wx.windSpeed = 5; wx.windFrom = wrapDeg360(A.heading + 20.f); wx.turbulence = 0.1f;
      vec3 side(-A.dir().z, 0, A.dir().x);
      vec3 start = A.pos() + side * 14000.f + A.dir() * 3000.f; start.y = std::max(A.elev + 1200.f, g_world.height(start.x, start.z) + 500.f);
      const bool eng = c.cond[0] == 'e', heavy = c.cond[0] == 'f', iced = c.cond[0] == 'i';
      Plane p; p.reset(&s, start, wrapDeg360(A.heading + 120.f), s.maxFuel * (heavy ? 0.75f : 0.6f), heavy ? s.cargoKg + s.pax * 85.f : 150.f, true, s.cruise * 0.85f);
      p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
      Plane ref = p; ref.apSense();   // (the same aircraft without the failure)
      if (eng) p.failNow(FAIL_ENGINE_TOTAL, 0);
      if (iced) { p.failNow(FAIL_ICING, 0); p.fail.ice = 0.7f; }
      p.apSense();
      const ApEnvelope& E = p.apEnv; const ApEnvelope& R = ref.apEnv;
      bool read = eng ? E.thrustFrac < 0.65f * R.thrustFrac && E.climb < R.climb * 0.6f && E.canGoAround
                : heavy ? E.vApp > s.vref * 1.2f && E.vs1 > Plane::perf(&s).vs1 * 1.2f && E.ldgDist > Plane::perf(&s).ldgRoll * 1.05f   // (over the learned figures: the stall at the test weight, the landing at full weight - every seat taken is more)
                : E.vs1 > R.vs1 * 1.08f && E.climb < R.climb;
      p.apEngage(Plane::AP_NAV, ai, wx);
      float tdVs = 0; bool td = false; int k = 0, goArounds = 0, lastStage = 0;
      for (; k < 1800 * 60 && !p.ev.crashed && !p.apDone; k++) {
        if (iced) p.fail.ice = std::max(p.fail.ice, 0.7f);   // (holding the ice: no warm air melts it on the way down)
        p.step(1 / 60.f, wx, k / 60.f);
        if (p.ev.touchdown && !td) { td = true; tdVs = -p.ev.touchdownVs; }
        if (p.apStage == Plane::APS_GOAROUND && lastStage != Plane::APS_GOAROUND) goArounds++;
        lastStage = p.apStage;
      }
      vec3 rel = p.pos - A.pos(); float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, side));
      bool ok = read && !p.ev.crashed && p.apDone && td && tdVs < 3.0f && along < A.length * 0.5f && cross < A.width * 0.5f && goArounds <= 1;
      printf("AP live %-16s %-10s reads vApp %4.1f vs1 %4.1f thrust %.2f climb %5.1f%s | %s after %4.0f s  touchdown %.1f m/s  go-arounds %d %s%s\n", s.name, c.cond,
             E.vApp, E.vs1, E.thrustFrac, E.climb, read ? "" : " (wrong)", p.apDone ? "landed" : "NOT DONE", k / 60.f, tdVs, goArounds,
             p.ev.crashed ? p.ev.crashReason.c_str() : "", ok ? " ok" : " FAIL");
      fails += !ok;
    }
    // no engines at all: no climb, so no go-around to fly
    Plane p; p.reset(&kAircraft[3], vec3(0, 1500, 0), 0, kAircraft[3].maxFuel * 0.6f, 150, true, kAircraft[3].cruise * 0.8f);
    p.failNow(FAIL_ENGINE_TOTAL, 0); p.failNow(FAIL_ENGINE_TOTAL, 1); p.apSense();
    bool ok = p.apEnv.climb < 0.f && !p.apEnv.canGoAround;
    printf("AP live %-16s both out   reads climb %.1f, go-around %s  %s\n", kAircraft[3].name, p.apEnv.climb, p.apEnv.canGoAround ? "yes" : "no", ok ? "ok" : "FAIL");
    fails += !ok;
  }
  // ---------------- aerobatics: every figure on a light single, the airliner and the Wraith. It must set itself up,
  // fly the figure inside the airframe's limits and level off into a hold on the heading the figure ends on.
  if (in(3)) for (int si : {0, 5, (int)kWraith}) {
    const AircraftSpec& s = kAircraft[si];
    const PerfModel& P = Plane::perf(&s);
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0;
    for (int f = 0; f < Plane::STUNT_COUNT; f++) {
      Plane p; p.reset(&s, vec3(-WORLD_HALF * 0.55f, 1500, WORLD_HALF * 0.55f), 90, s.maxFuel * 0.6f, 150, true, s.cruise * 0.8f);
      if (g_world.height(p.pos.x, p.pos.z) > 200.f) p.pos.y = g_world.height(p.pos.x, p.pos.z) + 1500.f;
      p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
      p.apStuntBegin(f, calm);
      float h0 = p.heading(), maxG = 1, minG = 1; bool flew = false; int k = 0;
      for (; k < 240 * 60 && !p.ev.crashed && p.apMode == Plane::AP_STUNT; k++) {
        p.step(1 / 60.f, calm, k / 60.f);
        if (p.apStuntStep > 0) { if (!flew) h0 = p.heading(); flew = true; maxG = std::max(maxG, p.gLoad); minG = std::min(minG, p.gLoad); }
      }
      float turned = fabsf(wrapAngle((p.heading() - h0) * DEG) / DEG);
      bool reverses = f == Plane::STUNT_IMMELMANN || f == Plane::STUNT_SPLIT_S || f == Plane::STUNT_WINGOVER;
      bool ok = !p.ev.crashed && flew && p.apMode == Plane::AP_HOLD && p.apStuntAbort.empty() && maxG < P.gLimit && minG > P.gNeg &&
                (reverses ? turned > 120.f : turned < 30.f) && fabsf(p.bankDeg()) < 20.f;
      printf("Stunt %-16s %-12s %5.1f s  g %+5.1f..%+5.1f  heading change %3.0f  %s%s\n", s.name, Plane::stuntName(f), k / 60.f, minG, maxG, turned,
             p.ev.crashed ? p.ev.crashReason.c_str() : p.apStuntAbort.c_str(), ok ? "  ok" : "  FAIL");
      fails += !ok;
    }
  }
  // ---------------- FLT-2 (the review of v3.44.0): the engine stops 2.5 km out on final, 350 m up, and the autoland is
  // engaged again - it flew its usual plan (a climb to a cruise, a descent orbit) without the power for either and
  // ditched. Now it glides the final it is on to the runway; from 20 km out, with nothing in reach, it says so
  for (int far = 0; far < 2 && in(3); far++) {
    const int ai = g_world.findAirport("MDB"); const Airport& A = g_world.airports[ai];
    const AircraftSpec& s = kAircraft[0];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0;
    const float out = far ? 20000.f : 2500.f;
    vec3 at = A.threshold(false) - A.dir() * out; at.y = A.elev + 350.f;
    Plane p; p.reset(&s, at, A.heading, s.maxFuel * 0.6f, 150, true, s.vref * 1.3f);
    p.ctl.gearDown = true; p.gear = 1; p.ctl.throttle = 0.3f;
    for (int e = 0; e < s.engines; e++) p.failNow(FAIL_ENGINE_TOTAL, e);
    p.apEngage(Plane::AP_NAV, ai, calm);
    const bool engaged = p.apOn;
    int k = 0; bool landed = false;
    for (; engaged && k < 300 * 60 && !p.ev.crashed; k++) {
      p.step(1 / 60.f, calm, k / 60.f);
      if (p.onGround && length(p.vel) < 1.f) { landed = true; break; }
    }
    const bool onRwy = g_world.onRunway(p.pos.x, p.pos.z, 5.f) == ai;
    bool ok = far ? (!engaged && !p.apDecline.empty()) : (engaged && landed && onRwy && !p.ev.crashed);
    printf("Dead-stick autoland %s on the MDB final, 350 m up: %s%s\n", far ? "20 km out" : "2.5 km out",
           far ? (engaged ? "engaged" : ("declined: " + p.apDecline).c_str()) : p.ev.crashed ? p.ev.crashReason.c_str() : landed ? (onRwy ? "landed on the runway" : "landed off it") : "still flying",
           ok ? "  ok" : "  FAIL");
    fails += !ok;
  }
  // ---------------- FLT-1 (the review of v3.44.0): a split-S asked for low down. Admitted from 700 m on the structure's
  // load - a pull the research jets' wings can't hold at the figure's entry speed - the XR-20, XR-30 and XR-40 flew into
  // the sea. Asked for at 700 m over the sea now, each climbs first (or gives it up) and none touches the water
  if (in(3)) for (int si : {kMantis, kResearchJet, kWraith}) {
    if (quick && si != kWraith) continue;   // (the sanitizer job: the XR-40, the one Codex flew in the game)
    const AircraftSpec& s = kAircraft[si];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0;
    vec3 sea(-WORLD_HALF * 0.9f, 700.f, WORLD_HALF * 0.9f);   // (open sea, far from any island)
    Plane p; p.reset(&s, sea, 90, s.maxFuel * 0.6f, 150, true, s.cruise * 0.8f);
    p.ctl.gearDown = false; p.gear = 0; p.ctl.throttle = 0.7f;
    p.apStuntBegin(Plane::STUNT_SPLIT_S, calm);
    float minAgl = 1e9f, startedAt = -1; int k = 0;
    for (; k < 240 * 60 && !p.ev.crashed && p.apMode == Plane::AP_STUNT; k++) {
      p.step(1 / 60.f, calm, k / 60.f);
      minAgl = std::min(minAgl, p.pos.y - std::max(g_world.height(p.pos.x, p.pos.z), 0.f));
      if (p.apStuntStep > 0 && startedAt < 0) startedAt = p.pos.y;
    }
    bool ok = !p.ev.crashed && minAgl > 60.f && p.apMode == Plane::AP_HOLD;
    printf("Low split-S %-16s asked at 700 m: began at %.0f m, lowest %.0f m, %s%s\n", s.name, startedAt, minAgl,
           p.ev.crashed ? p.ev.crashReason.c_str() : p.apStuntAbort.empty() ? "flown" : p.apStuntAbort.c_str(), ok ? "  ok" : "  FAIL");
    fails += !ok;
  }
  // ---------------- the weather's fields (weather.cpp): still air is still; the gust bursts reach about the reported
  // gust and no further; the eddies are as strong as the weather asks; the wind climbing a ridge lifts and pours
  // down its lee; it rains only under the clouds; and a replay is exact
  if (in(3)) {
    const int ap = g_world.findAirport("MDB");
    const Airport& A = g_world.airports[ap];
    Weather still; still.windSpeed = 0; still.gust = 0; still.turbulence = 0; still.cloudCover = 0;
    vec3 pa(A.x, A.elev + 300.f, A.z);
    wxfield::Local L0 = wxfield::local(still, pa, 300.f);
    wxfield::Sample s0 = wxfield::wind(still, L0, pa, 300.f, 37.f, vec3(), 11.f);
    bool calmOk = length(s0.v) < 1e-4f && L0.sigma < 1e-4f && length(L0.draft) < 1e-4f;
    Weather gw; gw.windSpeed = 8; gw.windFrom = 270; gw.gust = 4; gw.turbulence = 0.45f; gw.cloudCover = 0;
    vec3 air, pg(A.x, A.elev + 10.f, A.z);
    float peak = 0, sw2 = 0; int n = 0;
    for (int i = 0; i < 240 * 300; i++) {   // five minutes parked: the gusts along the wind
      float t = i / 240.f; air += wxfield::driftWind(gw) * (1 / 240.f);
      wxfield::Local L = wxfield::local(gw, pg, 10.f);
      wxfield::Local Lq = L; Lq.sigma = 0;   // (the bursts alone)
      vec3 v = wxfield::wind(gw, Lq, pg, 10.f, t, air, 11.f).v;
      peak = std::max(peak, v.x);   // (from 270: along +x)
      wxfield::Local Lh = wxfield::local(gw, pg + vec3(0, 290.f, 0), 300.f);
      vec3 ve = wxfield::wind(gw, Lh, pg + vec3(0, 290.f, 0), 300.f, t, air, 11.f).v - Lh.draft;
      sw2 += ve.y * ve.y; n++;
    }
    const float base = length(wxfield::meanWind(gw, 10.f)), sigW = wxfield::local(gw, pg + vec3(0, 290.f, 0), 300.f).sigma, rmsW = sqrtf(sw2 / n);
    bool gustOk = peak > base + 0.6f * gw.gust && peak < base + 1.4f * gw.gust;
    bool turbOk = rmsW > sigW * 0.6f && rmsW < sigW * 1.6f;
    // the strongest lift and sink over the islands in a 12 m/s westerly, 100 m above the ground
    Weather rw; rw.windSpeed = 12; rw.windFrom = 270; rw.turbulence = 0; rw.gust = 0; rw.cloudCover = 0;
    float lift = 0, sink = 0;
    for (float x = -36000; x <= 36000; x += 1500)
      for (float z = -36000; z <= 36000; z += 1500) {
        float g = g_world.height(x, z); if (g < 50.f) continue;
        wxfield::Local L = wxfield::local(rw, vec3(x, g + 100.f, z), 100.f);
        lift = std::max(lift, L.lift); sink = std::min(sink, L.lift);
      }
    bool ridgeOk = lift > 1.5f && sink < -1.f;
    // rain: none where the field has no cloud overhead, some where it has
    Weather sh = gw; sh.cloudCover = 0.4f; sh.precip = 1; sh.windSpeed = 0;
    int wrong = 0, wet = 0;
    for (float x = -36000; x <= 36000; x += 900)
      for (float z = -36000; z <= 36000; z += 900) {
        float r = wxfield::rainAt(sh, vec3(x, 200.f, z)), c = wxfield::cloudColumn(sh, x, z);
        if (r > 0.f && c < -0.25f) wrong++;
        if (r > 0.3f) wet++;
      }
    bool rainOk = wrong == 0 && wet > 0 && wxfield::rainAt(sh, vec3(0, sh.cloudBase + wxfield::cloudThickness(sh) + 10.f, 0)) == 0.f;
    wxfield::Sample a1 = wxfield::wind(gw, wxfield::local(gw, pg, 10.f), pg, 10.f, 12.3f, vec3(40, 0, 0), 11.f), a2 = wxfield::wind(gw, wxfield::local(gw, pg, 10.f), pg, 10.f, 12.3f, vec3(40, 0, 0), 11.f);
    bool replayOk = a1.v.x == a2.v.x && a1.v.y == a2.v.y && a1.v.z == a2.v.z && a1.gy.x == a2.gy.x;
    bool ok = calmOk && gustOk && turbOk && ridgeOk && rainOk && replayOk;
    printf("Weather: still air %s; gusts peak %.1f m/s on %.1f (reported %.0f); eddies %.2f m/s rms for %.2f; ridge lift %+.1f, lee sink %+.1f m/s; rain only under cloud %s (%d wet points); replay %s  %s\n",
           calmOk ? "still" : "MOVING", peak, base, gw.gust, rmsW, sigW, lift, sink, wrong == 0 ? "yes" : "NO", wet, replayOk ? "exact" : "DIFFERS", ok ? "ok" : "FAIL");
    fails += !ok;
  }
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
