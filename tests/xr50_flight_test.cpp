// The XR-50 Morpheus's flight model (docs/XR50.md; src/aircraft_morpheus.cpp), flown through Plane::step by a simple
// test pilot: its swing wing's schedule, each propulsion mode's reach (the turbofans' Mach 3, the scramjets' corridor),
// the leading edges' heat and its limit, the rocket's climb out of the atmosphere, the hover on its lift fan and nozzles,
// the post-stall manoeuvring its vectoring allows - and that it is twice the XR-40 in what makes either aggressive: its
// thrust for its weight, its pull and push, its roll, each measured on both in the same air. "--report" prints them.
#include "../src/aircraft.h"
#include "test_world.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
int failures = 0;
bool report = false;
void check(bool ok, const char* what) { if (!ok) { printf("FAIL: %s\n", what); ++failures; } }
const Weather calm{};
const float dt = 1.f / 120.f;
void trace(const Plane& p, float t, const char* tag);

// a level start over the open sea, wings level, heading east, at a height and Mach number (the XR-50, or another)
void start(Plane& p, float alt, float mach, float fuelFrac = 0.7f, int type = kMorpheus) {
  const vec3 at(-WORLD_HALF * 0.9f, alt, -WORLD_HALF * 0.9f);
  p.reset(&kAircraft[type], at, 90.f, kAircraft[type].maxFuel * fuelFrac, 85.f, true, mach * isa(alt).a);
  p.sceneryHits = false; p.engineRunning = true; p.engineSpool = 1.f; p.ctl.throttle = 1.f;
}
// the stick held at a setting for a few seconds from a level start: the most load (g, the most either way by the sign of
// the pitch) and the fastest roll (rad/s) it makes
float pull(int type, float alt, float mach, float pitch, float roll, float secs, float* rollMax = nullptr) {
  Plane p; start(p, alt, mach, 0.7f, type);
  float gMost = 0.f, wMax = 0.f;
  for (float t = 0.f; t < secs && !p.ev.crashed; t += dt) {
    p.ctl.pitch = pitch; p.ctl.roll = roll; p.ctl.yaw = 0.f; p.step(dt, calm, t); trace(p, t, type != kMorpheus ? "other" : pitch < 0.f ? "push" : roll != 0.f ? "roll" : "pull");
    gMost = pitch < 0.f ? std::min(gMost, p.gLoad) : std::max(gMost, p.gLoad); wMax = std::max(wMax, fabsf(p.w.z));
  }
  if (rollMax) *rollMax = wMax;
  return p.ev.crashed ? 0.f : gMost;
}
// the test pilot: a flight path angle (deg) held through the fly-by-wire's pitch rate, damped on the flight path's own
// rate (fast, the path lags the nose by many seconds: undamped it porpoised), the wings level
void fly(Plane& p, float gammaT, float t = 0.f) {
  const float V = std::max(length(p.vel), 1.f), gamma = asinf(clampf(p.vel.y / V, -1.f, 1.f)) / DEG;
  const float gdot = (p.gLoad * cosf(p.bankDeg() * DEG) - cosf(gamma * DEG)) * G0 / V / DEG;
  const float K = clampf(0.6f * 150.f / V, 0.08f, 0.6f);
  const float q = clampf(K * (gammaT - gamma) - 2.f * gdot, -12.f, 12.f) * DEG;
  p.ctl.pitch = clampf(q / p.fbwPitchMax(V), -0.9f, 0.9f);   // (short of the stop: the post-stall gate)
  p.ctl.roll = clampf(-p.bankDeg() * 0.04f, -1.f, 1.f); p.ctl.yaw = 0.f;
  p.step(dt, calm, t);
  trace(p, t, "fly");
}
// level at a height: the climb or descent that brings it there
void hold(Plane& p, float alt, float t = 0.f) {
  const float V = std::max(length(p.vel), 1.f), vs = clampf((alt - p.pos.y) * 0.08f, -80.f, 80.f);
  fly(p, asinf(clampf(vs / V, -0.5f, 0.5f)) / DEG, t);
}
// XR50TRACE: a line a second of what it is doing
void trace(const Plane& p, float t, const char* tag) {
  static const bool on = getenv("XR50TRACE") != nullptr, every = on && !strcmp(getenv("XR50TRACE"), "all");
  if (!on || (fmodf(t, 1.f) >= dt && !every)) return;
  printf("  %s t %5.1f alt %7.0f M %5.2f a %5.1f pitch %5.1f g %5.1f tf %4.0f ram %4.0f scr %4.0f rkt %4.0f fan %4.0f skin %5.0f sw %4.1f cd0 %.4f %s\n", tag, t, p.pos.y, p.mach,
         p.alpha / DEG, p.pitchDeg(), p.gLoad, p.tbcc[0] / 1e3f, p.tbcc[1] / 1e3f, p.tbcc[2] / 1e3f, p.rocketN / 1e3f, p.fanN / 1e3f, p.skinT - 273.15f, p.sweep / DEG, p.cd0Value(),
         p.ev.crashed ? p.ev.crashReason.c_str() : "");
}

// full power level at a height until the speed stops growing: the Mach number it settles at
float topMach(float alt, float mach0, float secs, float* skin = nullptr) {
  Plane p; start(p, alt, mach0);
  float best = 0.f;
  for (float t = 0.f; t < secs && !p.ev.crashed; t += dt) { p.ctl.throttle = 1.f; hold(p, alt, t); best = std::max(best, p.mach); }
  if (skin) *skin = p.skinT;
  return p.ev.crashed ? -best : best;
}
}

int main(int argc, char** argv) {
  report = argc > 1 && !strcmp(argv[1], "--report");
  buildTestWorld();
  const AircraftSpec& s = kAircraft[kMorpheus];
  check(s.special == 3 && kMorpheus == 16 && !strcmp(s.id, "xr50_morpheus"), "the XR-50 is the roster's stable index 16, special 3");

  // ---- the schedule: forward slow, aft fast, forward again with the lever down
  {
    Plane p; start(p, 9000.f, 0.5f);
    p.ctl.throttle = p.engineSpool = 0.2f;   // (in full reheat it is past Mach 0.72 in two seconds)
    for (float t = 0.f; t < 3.f; t += dt) hold(p, 9000.f, t);
    const float slow = p.sweep;
    start(p, 12000.f, 1.8f);
    for (float t = 0.f; t < 12.f; t += dt) hold(p, 12000.f, t);
    const float fast = p.sweep;
    if (report) printf("sweep: %.0f deg at Mach 0.5, %.0f at Mach %.2f\n", slow / DEG, fast / DEG, p.mach);
    check(fabsf(slow - kMorpheusSweepMin) < 1.f * DEG, "the wing is forward below Mach 0.7");
    check(fabsf(fast - kMorpheusSweepMax) < 1.f * DEG, "the wing is fully aft at Mach 1.8");
    check(morpheusSpan(kMorpheusSweepMin) > 15.f && morpheusSpan(kMorpheusSweepMax) < 10.f, "the span folds from over 15 m to under 10");
  }
  // ---- each propulsion mode's reach
  {
    float skin = 0.f;
    const float m10 = topMach(10000.f, 0.9f, 240.f), m18 = topMach(18000.f, 2.2f, 300.f), m27 = topMach(27000.f, 6.0f, 300.f, &skin);
    if (report) printf("top Mach: %.2f at 10 km, %.2f at 18 km, %.2f at 27 km (leading edges %.0f C)\n", m10, m18, m27, skin - 273.15f);
    check(m10 > 2.0f && m10 < 4.3f, "the turbofans in reheat and the ramjets take it past Mach 2 at 10 km - and the combustors' limit stops it short of Mach 4.3");
    check(m18 > 4.f, "the ramjets take it past Mach 4 at 18 km");
    check(m27 > 6.8f && m27 < 7.6f, "the scramjets hold it past Mach 6.8 in their corridor, at 27 km");
  }
  {   // the heat: Mach 7.3 held at 24 km burns the leading edges away; the same at 29 km does not
    Plane lo; start(lo, 24000.f, 7.3f);
    float t = 0.f;
    for (; t < 150.f && !lo.ev.crashed; t += dt) { lo.ctl.throttle = 1.f; hold(lo, 24000.f, t); }
    Plane hi; start(hi, 29000.f, 7.3f);
    for (float u = 0.f; u < 150.f && !hi.ev.crashed; u += dt) { hi.ctl.throttle = 1.f; hold(hi, 29000.f, u); }
    if (report) printf("heat: Mach 7.3 at 24 km %s after %.0f s (%.0f C, Mach %.2f); at 29 km %.0f C at Mach %.2f, %s\n", lo.ev.crashed ? "burned through" : "survived", t,
                       lo.skinT - 273.15f, lo.mach, hi.skinT - 273.15f, hi.mach, hi.ev.crashed ? hi.ev.crashReason.c_str() : "intact");
    check(lo.ev.crashed && lo.ev.crashReason.find("burned") != std::string::npos, "Mach 7.3 at 24 km overheats the leading edges");
    check(!hi.ev.crashed && hi.skinT < kMorpheusSkinLimit, "higher, at 29 km, they stay inside their limit");
  }
  {   // endurance: the fuel it burns held at a speed and height (the throttle on the Mach number, proportional and
      // integral), over the last minute of three, in minutes of its tanks
    float spool = 0.f, held = 0.f;
    auto minutes = [&spool, &held](float alt, float mach) {
      Plane p; start(p, alt, mach);
      float used = 0.f, t = 0.f, integ = 0.5f;
      for (; t < 180.f && !p.ev.crashed; t += dt) {
        const float err = mach - p.mach;
        integ = clampf(integ + err * 0.4f * dt, 0.f, 1.f);
        p.ctl.throttle = clampf(integ + err * 4.f, 0.f, 1.f);
        const float f0 = p.fuel; hold(p, alt, t);
        if (t > 120.f) used += f0 - p.fuel;
      }
      spool = p.engineSpool; held = p.mach;
      return used > 0.f ? kAircraft[kMorpheus].maxFuel / (used / 60.f) / 60.f : 0.f;
    };
    const float scram = minutes(25000.f, 6.5f), scramHeld = held, cruise = minutes(12000.f, 0.9f);
    const float planned = cruiseMinutes(kAircraft[kMorpheus], kAircraft[kMorpheus].maxFuel);
    if (report) printf("endurance: %.0f min at Mach %.2f on the scramjets (25 km), %.0f min at Mach %.2f (12 km, spool %.3f; the planner's %.0f)\n", scram, scramHeld,
                       cruise, held, spool, planned);
    check(scram > 30.f && cruise > 600.f, "the tanks last over half an hour on the scramjets and ten hours at cruise");
    check(fabsf(planned / cruise - 1.f) < 0.15f, "the fuel planner's cruise is the one it flies");
  }
  // ---- the rocket: from the scramjets' corridor, a pull to 35 deg and the rocket lit - out of the atmosphere
  {
    Plane p; start(p, 27000.f, 6.8f);
    float apogee = 0.f, oxStart = p.oxidizer, t = 0.f;
    bool lit = false;
    for (; t < 400.f && !p.ev.crashed; t += dt) {
      p.ctl.throttle = 1.f;
      if (t > 3.f && !lit) { p.rocketLit = true; lit = true; }
      fly(p, 35.f, t);   // (in the thin air the nose points where the jets put it: the path follows only below)
      apogee = std::max(apogee, p.pos.y);
      if (p.pos.y < apogee - 2000.f) break;   // (over the top and on the way down)
    }
    if (report) printf("rocket: apogee %.1f km, oxidizer %.0f of %.0f kg left, rocket %s\n", apogee / 1000.f, p.oxidizer, oxStart, p.rocketLit ? "lit" : "out");
    check(apogee > 80000.f, "the rocket takes it above 80 km");
    check(p.oxidizer < 1.f && !p.rocketLit, "the rocket burns until its oxidizer is gone, then goes out");
  }
  // ---- the hover: the lever down, it holds itself up on its fan and nozzles, level, for 30 s
  {
    Plane p; start(p, 300.f, 0.f, 0.5f);
    p.vel = vec3(); p.ctl.flaps = p.flaps = p.nozzle = 1.f; p.ctl.gearDown = true; p.gear = 1.f;
    p.fanClutch = 1.f; p.nzSwivel = 95.f * DEG;   // (established in the hover)
    // (the test pilot's throttle: from what holds its weight, its gains for the thrust a throttle's worth makes)
    const float wt = p.mass() * G0 / p.liftThrustMax();
    float hMin = 1e9f, hMax = -1e9f, attMax = 0.f, thrI = 0.85f * wt;
    p.engineSpool = thrI;
    for (float t = 0.f; t < 40.f && !p.ev.crashed; t += dt) {
      p.ctl.flaps = 1.f; p.ctl.pitch = 0.f; p.ctl.roll = 0.f; p.ctl.yaw = 0.f;
      const float e = (300.f - p.pos.y) * 0.3f - p.vel.y;
      thrI = clampf(thrI + e * 0.05f * wt * dt, 0.f, 1.f); p.ctl.throttle = clampf(thrI + e * 0.15f * wt, 0.f, 1.f);
      p.step(dt, calm, t); trace(p, t, "hover");
      if (t > 10.f) { hMin = std::min(hMin, p.pos.y); hMax = std::max(hMax, p.pos.y); attMax = std::max(attMax, std::max(fabsf(p.pitchDeg()), fabsf(p.bankDeg()))); }
    }
    if (report) printf("hover: %.1f..%.1f m, attitude within %.1f deg, throttle %.2f, fan %.0f kN, nozzles %.0f deg, hover max %.0f kN vs weight %.0f kN\n",
                       hMin, hMax, attMax, p.ctl.throttle, p.fanN / 1000.f, p.nzSwivel / DEG, p.liftThrustMax() / 1000.f, p.mass() * G0 / 1000.f);
    check(!p.ev.crashed && hMax - hMin < 6.f && attMax < 5.f, "it hovers level within a few metres");
    check(p.liftThrustMax() > 1.1f * p.mass() * G0, "the autopilot sees it can hover (and lands vertically)");
  }
  // ---- twice the XR-40: its thrust for its weight at full tanks (static, in full reheat and full boost); the most load
  // a full pull and a full push make from the same start, subsonic and at Mach 1.6 low down; the fastest roll
  {
    auto twr = [](int type) { Plane p; start(p, 0.f, 0.f, 1.f, type); return p.thrustAt(1.f, 0.f, 0.f, 1.225f) / (p.mass() * G0); };
    const float tw50 = twr(kMorpheus), tw40 = twr(kSomnus);
    const float g50 = pull(kMorpheus, 3000.f, 0.95f, 1.f, 0.f, 4.f), g40 = pull(kSomnus, 3000.f, 0.95f, 1.f, 0.f, 4.f);
    const float f50 = pull(kMorpheus, 1000.f, 1.6f, 1.f, 0.f, 2.f), f40 = pull(kSomnus, 1000.f, 1.6f, 1.f, 0.f, 2.f);
    const float n50 = pull(kMorpheus, 1000.f, 1.6f, -1.f, 0.f, 2.f), n40 = pull(kSomnus, 1000.f, 1.6f, -1.f, 0.f, 2.f);
    float r50 = 0.f, r40 = 0.f;
    pull(kMorpheus, 3000.f, 0.9f, 0.f, 1.f, 2.f, &r50); pull(kSomnus, 3000.f, 0.9f, 0.f, 1.f, 2.f, &r40);
    if (report) printf("vs the XR-40: thrust/weight %.2f / %.2f; full pull at Mach 0.95 %.0f g / %.0f, at Mach 1.6 %.0f g / %.0f; full push %.0f g / %.0f; roll %.0f deg/s / %.0f\n",
                       tw50, tw40, g50, g40, f50, f40, n50, n40, r50 / DEG, r40 / DEG);
    check(tw50 > 1.95f * tw40, "twice the XR-40's thrust for its weight");
    check(g50 > 1.9f * g40 && f50 > 1.9f * f40 && f50 >= 160.f, "twice the XR-40's pull, 160 g and more fast and low");
    check(n50 < 1.9f * n40 && n50 <= -80.f, "twice the XR-40's push, past -80 g");
    check(r50 > 1.9f * r40 && r50 / DEG > 760.f, "twice the XR-40's roll, past 760 deg/s");
    check(f50 < kAircraft[kMorpheus].gLimitPos() && n50 > kAircraft[kMorpheus].gLimitNeg(), "inside its 600 g structure either way");
  }
  // ---- post-stall: slow, a full pull stands it past 70 deg of angle of attack, and it recovers
  {
    Plane p; start(p, 4000.f, 0.35f);
    float aMax = 0.f, t = 0.f;
    for (; t < 3.f; t += dt) { p.ctl.pitch = 1.f; p.ctl.roll = 0.f; p.step(dt, calm, t); trace(p, t, "post"); aMax = std::max(aMax, p.alpha / DEG); }
    for (; t < 15.f; t += dt) hold(p, 4000.f, t);
    if (report) printf("post-stall: %.0f deg of angle of attack, then %s at %.0f m\n", aMax, p.ev.crashed ? "crashed" : "flying", p.pos.y);
    check(aMax > 70.f && !p.ev.crashed, "the vectoring takes it past 70 deg of angle of attack and back");
  }
  printf(failures ? "xr50_flight_test: %d failures\n" : "xr50_flight_test: all passed\n", failures);
  return failures ? 1 : 0;
}
