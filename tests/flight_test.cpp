#include <string>
// Headless flight-model checks: take-off roll, climb, cruise, stall per aircraft.
#include "../src/aircraft.h"
#include <cstdlib>
int main(int argc, char** argv) {
  g_world.build();
  if (argc > 1 && std::string(argv[1]) == "--table") {   // the README's aircraft table, from the learned performance
    printf("| Aircraft | Type | Seats / Cargo | Range | Cruise | Take-off roll | Landing distance | Runway | Licence |\n|---|---|---|---|---|---|---|---|---|\n");
    static const char* lic[] = {"Student", "PPL", "CPL", "ATP"};
    for (int i = 0; i < kNumAircraft; i++) {
      const AircraftSpec& s = kAircraft[i]; const PerfModel& P = Plane::perf(&s);
      printf("| %s | %s | %d / %.0f kg | %.0f km | %.0f kt | %.0f m | %.0f m | %.0f m%s | %s |\n", s.name, s.role, s.pax, s.cargoKg, s.rangeKm, P.cruiseV * MS_TO_KT,
             P.toRoll, P.ldgRoll, s.runwayNeeded(0), s.roughOK ? ", gravel/snow" : " paved", lic[s.license]);
    }
    return 0;
  }
  Weather wx; wx.windSpeed = 0; wx.gust = 0; wx.turbulence = 0;
  int fails = 0;
  if (strcmp(kAircraft[kOsprey].id, "osprey_c6") != 0 || strcmp(kAircraft[kNightjar].id, "xr10_nightjar") != 0 || strcmp(kAircraft[kResearchJet].id, "xr30_specter") != 0 || strcmp(kAircraft[kMantis].id, "xr20_mantis") != 0 || strcmp(kAircraft[kWraith].id, "xr40_wraith") != 0) { printf("aircraft indices (kOsprey / kNightjar / kResearchJet / kMantis / kWraith) don't match the table\n"); return 1; }
  for (int ai = 0; ai < kNumAircraft; ai++) {
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
    p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = s.taildragger ? 0.3f : 0.2f;
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
  for (int ai = 0; ai < kNumAircraft; ai++) {
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
  printf("control directions checked\n");
  // ---------------- failures (C7): an engine-out glide reaches a field, a stuck gear belly landing at Vref is survivable,
  // a twin flies on one engine, a blocked pitot freezes the airspeed, ice costs lift
  {
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
    float tstop = -1;
    for (int i = 0; i < 90 * 240 && !p.ev.crashed; i++) {
      p.ctl.pitch = p.ev.bellyLanding ? 0.f : clampf((std::max(-0.8f, -p.agl() * 0.3f) - p.vel.y) * 0.25f - p.w.x * 0.8f + 0.08f, -1, 1);
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
    for (int i = 0; i < 600 * 60 && !p.fail.avionicsDark(); i++) { p.ctl.pitch = clampf((0.f - p.vel.y) * 0.1f - p.w.x * 0.8f, -1, 1); p.ctl.throttle = 0.7f; p.step(1 / 60.f, calm, i / 60.f); }
    ok = p.fail.avionicsDark() && p.flightTime >= 0.f && !p.ev.crashed;
    printf("Alternator failure: battery flat after %.0f s %s\n", p.fail.battery <= 0.f ? 420.f : -1.f, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- the research register's performance tiers: the XR-10 just subsonic, the XR-20 supersonic, the XR-30
  // and the XR-40 above them in that order; and the structure takes a short overstress but not a sustained one
  {
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    auto topMach = [&](int idx, float alt) {
      const AircraftSpec& s = kAircraft[idx];
      Plane p; p.reset(&s, vec3(-40000, alt, 0), 90, s.maxFuel * 0.7f, 85, true, 200); p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0;
      float m = 0;
      for (int i = 0; i < 100 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((alt - p.pos.y) * 0.002f - p.vel.y * 0.01f - p.w.x * 0.3f, -1, 1); p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); m = std::max(m, p.mach); }
      return m;
    };
    float m8 = topMach(kNightjar, 8000.f), m10 = topMach(kMantis, 8000.f), m9 = topMach(kResearchJet, 8000.f), m11 = topMach(kWraith, 8000.f);
    bool ok = m8 > 0.93f && m8 < 1.0f && m10 > 1.7f && m10 < 2.2f && m9 > 2.5f && m9 > m10 + 0.4f && m11 > 4.0f && m11 > m9 + 0.8f;
    printf("Research tiers at 8 km: XR-10 Mach %.2f  XR-20 Mach %.2f  XR-30 Mach %.2f  XR-40 Mach %.2f %s\n", m8, m10, m9, m11, ok ? "ok" : "FAIL"); fails += !ok;
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
  {
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
  {
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
  // ---------------- autopilot: stable holds in turbulence, and autoland at Solace Capital for every aircraft. The
  // autopilot flies each type to its own envelope (steep banks, hard pulls): what's checked is that it gets there, settles,
  // and never goes past the airframe's limits.
  for (int i = 0; i < 9; i++) {
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
  for (int i = 0; i < kNumAircraft; i++) {
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
    // route to Solace Capital and land: comfortable all the way to the final approach
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
  {
    int ai = g_world.findAirport("CAP"); const Airport& A = g_world.airports[ai];
    for (int i = 0; i < 9; i++) {
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
  {
    int ai = g_world.findAirport("CAP"); const Airport& A = g_world.airports[ai];
    struct Case { int craft; const char* cond; };
    const Case cases[] = {{3, "engine out"}, {5, "engine out"}, {6, "engine out"}, {4, "full load"}, {5, "full load"}, {0, "iced"}, {5, "iced"}};
    for (const Case& c : cases) {
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
  for (int si : {0, 5, (int)kWraith}) {
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
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
