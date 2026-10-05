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
  if (strcmp(kAircraft[kOsprey].id, "osprey_c6") != 0 || strcmp(kAircraft[kNightjar].id, "xr8_nightjar") != 0 || strcmp(kAircraft[kResearchJet].id, "xr9") != 0 || strcmp(kAircraft[kWraith].id, "xr11") != 0) { printf("aircraft indices (kOsprey / kNightjar / kResearchJet / kWraith) don't match the table\n"); return 1; }
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
  // XR-9 research jet: supersonic in level flight, no vertical flight, slow flight on approach, pull limits, roll authority
  {
    const AircraftSpec& s = kAircraft[kResearchJet];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    Plane p;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 200);
    p.ctl.throttle = 1; p.ctl.gearDown = false; p.gear = 0;
    for (int i = 0; i < 40 * 240 && !p.ev.crashed; i++) { p.ctl.pitch = clampf((3000 - p.pos.y) * 0.002f - p.vel.y * 0.01f, -1, 1); p.step(1 / 240.f, calm, i / 240.f); }
    bool ok = !p.ev.crashed && p.mach > 2.0f;
    printf("XR-9 level acceleration: Mach %.2f after 40 s %s\n", p.mach, ok ? "ok" : "FAIL"); fails += !ok;
    // no vertical flight: the flap lever does nothing and the nozzles stay aft, so at a standstill it simply falls
    p.reset(&s, vec3(0, 500, 0), 90, s.maxFuel, 85, true, 0); p.vel = vec3(); p.ctl.flaps = 1; p.ctl.throttle = 0.7f;
    for (int i = 0; i < 4 * 240 && !p.ev.crashed; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = p.nozzle == 0.f && p.pos.y < 450.f;
    printf("XR-9 cannot hover: nozzle %.2f, alt %.0f m after 4 s %s\n", p.nozzle, p.pos.y, ok ? "ok" : "FAIL"); fails += !ok;
    // approach: gear down at about 145 kt it flies level on its wing alone
    p.reset(&s, vec3(0, 600, 0), 90, s.maxFuel, 85, true, 75); p.ctl.gearDown = true; p.gear = 1;
    for (int i = 0; i < 20 * 240 && !p.ev.crashed; i++) {
      p.ctl.pitch = clampf((600 - p.pos.y) * 0.004f - p.vel.y * 0.03f - p.w.x * 0.5f, -1, 1);
      p.ctl.throttle = clampf(0.3f + (75 - length(p.vel)) * 0.05f, 0, 1);
      p.step(1 / 240.f, calm, i / 240.f);
    }
    ok = !p.ev.crashed && fabsf(p.pos.y - 600) < 40 && fabsf(length(p.vel) - 75) < 10;
    printf("XR-9 approach speed level flight: alt %.0f m, %.0f kt %s\n", p.pos.y, length(p.vel) * MS_TO_KT, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.pitch = 1;
    float gmax = 0;
    for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); gmax = std::max(gmax, p.gLoad); }
    ok = !p.ev.crashed && gmax < 50.f && gmax > 10.f;
    printf("XR-9 full-back pull: peak %.1f g %s\n", gmax, ok ? "ok" : "FAIL"); fails += !ok;
    // no g limiter: a full pull at Mach 1.8 overstresses the airframe and it fails
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 620); p.ctl.throttle = 1; p.ctl.pitch = 1;
    for (int i = 0; i < 3 * 240 && !p.ev.crashed; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = p.ev.crashed && p.ev.crashReason.find("Structural") != std::string::npos;
    printf("XR-9 unlimited pull at Mach 1.8 breaks the airframe: %s\n", ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 340); p.ctl.throttle = 1; p.ctl.pitch = 1;
    float rate = 0;
    for (int i = 0; i < 2 * 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); rate = std::max(rate, p.w.x / DEG); }
    ok = !p.ev.crashed && rate > 100.f;
    printf("XR-9 pitch rate at Mach 1: %.0f deg/s %s\n", rate, ok ? "ok" : "FAIL"); fails += !ok;
    // stick snapped from full back to full forward: the vectoring nozzles reverse the pitch rate quickly
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.pitch = 1;
    for (int i = 0; i < 120; i++) p.step(1 / 240.f, calm, i / 240.f);
    p.ctl.pitch = -1; float trev = -1;
    for (int i = 0; i < 240 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, i / 240.f); if (trev < 0 && p.w.x < -60 * DEG) trev = i / 240.f; }
    ok = !p.ev.crashed && trev > 0 && trev < 0.3f;
    printf("XR-9 pitch reversal to -60 deg/s: %.2f s %s\n", trev, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.roll = 1;
    for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, i / 240.f);
    ok = -p.w.z / DEG > 250.f;
    printf("XR-9 roll rate %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
  }
  // ---------------- XR-11 Wraith: four-pod VTOL hover, top speed, roll rate and the structural g it can pull
  {
    const AircraftSpec& s = kAircraft[kWraith];
    Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
    Plane p; p.reset(&s, vec3(0, 800, 0), 0, s.maxFuel, 85, true, 0.1f); p.vel = vec3(); p.ctl.flaps = 1; p.flaps = p.nozzle = 1;
    float I = 0.45f;
    for (int i = 0; i < 60 * 30; i++) { I = clampf(I - p.vel.y * 0.004f / 60.f * 60.f * 0.05f, 0.f, 1.f); p.ctl.throttle = clampf(I - p.vel.y * 0.04f, 0.f, 1.f); p.step(1 / 60.f, calm, i / 60.f); }
    bool ok = !p.ev.crashed && fabsf(p.vel.y) < 1.f && fabsf(p.pitchDeg()) < 2.f && fabsf(p.bankDeg()) < 2.f && length(vec3(p.vel.x, 0, p.vel.z)) < 2.f;
    printf("XR-11 hover: throttle %.2f vs %.2f m/s pitch %.1f bank %.1f drift %.1f m/s %s\n", p.ctl.throttle, p.vel.y, p.pitchDeg(), p.bankDeg(), length(vec3(p.vel.x, 0, p.vel.z)), ok ? "ok" : "FAIL"); fails += !ok;
    p.ctl.roll = 0.6f; for (int i = 0; i < 60; i++) p.step(1 / 60.f, calm, 0);
    ok = -p.w.z / DEG > 25.f; printf("XR-11 hover roll rate (pod thrust differential) %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(-46000, 4000, 0), 90, s.maxFuel, 85, true, 300); p.ctl.throttle = 1; p.apEngage(Plane::AP_HOLD, -1, calm); p.apSpeed = 0;
    for (int i = 0; i < 60 * 40; i++) p.step(1 / 60.f, calm, 0);
    ok = !p.ev.crashed && p.mach > 3.0f; printf("XR-11 top speed Mach %.2f at %.0f m %s %s\n", p.mach, p.pos.y, p.ev.crashReason.c_str(), ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 250); p.ctl.throttle = 0.8f; p.ctl.roll = 1;
    for (int i = 0; i < 240; i++) p.step(1 / 240.f, calm, 0);
    ok = -p.w.z / DEG > 330.f; printf("XR-11 roll rate %.0f deg/s %s\n", -p.w.z / DEG, ok ? "ok" : "FAIL"); fails += !ok;
    p.reset(&s, vec3(0, 3000, 0), 90, s.maxFuel, 85, true, 600); p.ctl.throttle = 0.8f; p.ctl.pitch = 1; float mg = 0;
    for (int i = 0; i < 480 && !p.ev.crashed; i++) { p.step(1 / 240.f, calm, 0); mg = std::max(mg, p.gLoad); }
    ok = !p.ev.crashed && mg > 60.f; printf("XR-11 full pull at 600 m/s: %.0f g %s\n", mg, ok ? "ok" : "FAIL"); fails += !ok;
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
