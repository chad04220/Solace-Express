// Headless flight-model checks: take-off roll, climb, cruise, stall per aircraft.
#include "../src/aircraft.h"
#include <cstdlib>
int main() {
  g_world.build();
  Weather wx; wx.windSpeed = 0; wx.gust = 0; wx.turbulence = 0;
  int fails = 0;
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
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
