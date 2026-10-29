// The XR-60 Phantasos's flight model (docs/XR60.md; src/aircraft_xr60.cpp), flown through Plane::step by a simple test
// pilot: the ring drive's reach in air and none, the scramjet's corridor to Mach 12 and the sheath's heat limit, the
// surge and its bank, the hover on its field emitters, the post-stall manoeuvring its ring allows - and that it is the
// XR-50 several times over in what makes either quick: its thrust for its weight, its pull and push, its roll, each
// measured on both in the same air. "--report" prints them.
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

// a level start over the open sea, wings level, heading east, at a height and Mach number (the XR-60, or another)
void start(Plane& p, float alt, float mach, float fuelFrac = 0.7f, int type = kPhantasos) {
  const vec3 at(-WORLD_HALF * 0.9f, alt, -WORLD_HALF * 0.9f);
  p.reset(&kAircraft[type], at, 90.f, kAircraft[type].maxFuel * fuelFrac, 85.f, true, mach * isa(alt).a);
  p.sceneryHits = false; p.engineRunning = true; p.engineSpool = 1.f; p.ctl.throttle = 1.f;
  if (type == kPhantasos) { p.sheath = 1.f; p.circ = 1.f; }   // (the core at power: its fields up)
}
// the stick held at a setting for a few seconds from a level start: the most load (g, the most either way by the sign of
// the pitch) and the fastest roll (rad/s) it makes
float pull(int type, float alt, float mach, float pitch, float roll, float secs, float* rollMax = nullptr) {
  Plane p; start(p, alt, mach, 0.7f, type);
  float gMost = 0.f, wMax = 0.f;
  for (float t = 0.f; t < secs && !p.ev.crashed; t += dt) {
    p.ctl.pitch = pitch; p.ctl.roll = roll; p.ctl.yaw = 0.f; p.step(dt, calm, t); trace(p, t, type != kPhantasos ? "other" : pitch < 0.f ? "push" : roll != 0.f ? "roll" : "pull");
    gMost = pitch < 0.f ? std::min(gMost, p.gLoad) : std::max(gMost, p.gLoad); wMax = std::max(wMax, fabsf(p.w.z));
  }
  if (rollMax) *rollMax = wMax;
  return p.ev.crashed ? 0.f : gMost;
}
// the test pilot: a flight path angle (deg) held through the fly-by-wire's pitch rate, damped on the flight path's own
// rate, the wings level
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
// XR60TRACE: a line a second of what it is doing
void trace(const Plane& p, float t, const char* tag) {
  static const bool on = getenv("XR60TRACE") != nullptr, every = on && !strcmp(getenv("XR60TRACE"), "all");
  if (!on || (fmodf(t, 1.f) >= dt && !every)) return;
  printf("  %s t %5.1f alt %7.0f M %5.2f a %5.1f pitch %5.1f g %6.1f ring %5.0f scr %4.0f srg %.2f bank %.2f emit %4.0f skin %5.0f sh %.2f cd0 %.4f %s\n", tag, t, p.pos.y, p.mach,
         p.alpha / DEG, p.pitchDeg(), p.gLoad, p.ringN / 1e3f, p.scramN / 1e3f, p.surgeN, p.surgeBank, (p.emitN[0] + p.emitN[1] + p.emitN[2] + p.emitN[3]) / 1e3f,
         p.skinT - 273.15f, p.sheath, p.cd0Value(), p.ev.crashed ? p.ev.crashReason.c_str() : "");
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
  const AircraftSpec& s = kAircraft[kPhantasos];
  check(s.special == 4 && kPhantasos == 17 && !strcmp(s.id, "xr60_phantasos"), "the XR-60 is the roster's stable index 17, special 4");

  // ---- the drive: the ring gives its whole thrust from a standstill and in the vacuum alike; the scramjet carries it
  // where the air quenches the ring
  {
    const Xr60Thrust still = xr60Drive(s, 1.f, 0.f, 1.225f, 340.f, 0.f), vac = xr60Drive(s, 1.f, 3000.f, 0.f, 300.f, 0.f);
    const Xr60Thrust fast = xr60Drive(s, 1.f, 9.f * 301.f, 0.018f, 301.f, 0.f), surge = xr60Drive(s, 1.f, 0.f, 1.225f, 340.f, 1.f);
    if (report) printf("drive: standing %.0f kN, vacuum %.0f kN, Mach 9 at 30 km ring %.0f + scram %.0f kN, surge standing %.0f kN\n",
                       still.ring / 1e3f, vac.ring / 1e3f, fast.ring / 1e3f, fast.scram / 1e3f, surge.ring / 1e3f);
    check(fabsf(still.ring - s.power) < 1.f && fabsf(vac.ring - s.power) < 1.f, "the ring's rated thrust standing and in a vacuum");
    check(fast.ring < 0.3f * s.power && fast.scram > 100e3f, "in dense air at speed the ring is quenched and the scramjet carries it");
    check(fabsf(surge.ring - 3.f * s.power) < 1.f, "the surge triples the ring");
  }
  // ---- the corridor: its top speed at each height, and the heat
  {
    float skin = 0.f;
    const float m10 = topMach(10000.f, 0.9f, 240.f), m20 = topMach(20000.f, 3.f, 300.f), m32 = topMach(32000.f, 8.0f, 300.f, &skin);
    if (report) printf("top Mach: %.2f at 10 km, %.2f at 20 km%s, %.2f at 32 km (skin %.0f C)\n", m10, fabsf(m20), m20 < 0.f ? " (then the heat ended it: too fast too low)" : "", m32, skin - 273.15f);
    check(m10 > 3.f, "past Mach 3 at 10 km");
    check(fabsf(m20) > 7.f, "past Mach 7 at 20 km (where full throttle held on burns it: the corridor)");
    check(m32 > 11.5f && m32 < 13.5f, "the scramjet holds it past Mach 11.5 in its corridor, at 32 km, and the wall past its design Mach stops it");
  }
  {   // the heat: Mach 12 held at 22 km fails the skin; the same at 30 km does not (the throttle on the Mach number)
    auto holdMach = [](Plane& p, float alt, float mach, float secs) {
      float integ = 0.6f, t = 0.f;
      for (; t < secs && !p.ev.crashed; t += dt) {
        const float err = mach - p.mach;
        integ = clampf(integ + err * 0.4f * dt, 0.f, 1.f);
        p.ctl.throttle = clampf(integ + err * 4.f, 0.f, 1.f);
        hold(p, alt, t);
      }
      return t;
    };
    Plane lo; start(lo, 22000.f, 12.f);
    const float t = holdMach(lo, 22000.f, 12.f, 150.f);
    Plane hi; start(hi, 30000.f, 12.f);
    holdMach(hi, 30000.f, 12.f, 150.f);
    if (report) printf("heat: Mach 12 at 22 km %s after %.0f s (%.0f C, Mach %.2f); at 30 km %.0f C at Mach %.2f, %s\n", lo.ev.crashed ? "failed" : "survived", t,
                       lo.skinT - 273.15f, lo.mach, hi.skinT - 273.15f, hi.mach, hi.ev.crashed ? hi.ev.crashReason.c_str() : "intact");
    check(lo.ev.crashed && lo.ev.crashReason.find("overheated") != std::string::npos, "Mach 12 at 22 km overheats the skin through its sheath");
    check(!hi.ev.crashed && hi.skinT < kXr60SkinLimit, "higher, at 30 km, it stays inside its limit");
  }
  {   // endurance: the fuel it burns held at a speed and height, over the last minute of three, in minutes of its tanks
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
      return used > 0.f ? kAircraft[kPhantasos].maxFuel / (used / 60.f) / 60.f : 0.f;
    };
    const float scram = minutes(30000.f, 10.f), scramHeld = held, cruise = minutes(20000.f, 2.f);
    const float planned = cruiseMinutes(kAircraft[kPhantasos], kAircraft[kPhantasos].maxFuel);
    Plane p; start(p, 0.f, 0.f, 1.f);
    const float full = kAircraft[kPhantasos].maxFuel / p.fuelFlowAt(1.f, 0.f, 1.225f) / 60.f;
    if (report) printf("endurance: %.0f min at Mach %.2f on the scramjet (30 km), %.0f min at Mach %.2f (20 km, spool %.3f; the planner's %.0f); %.0f min of full ring thrust\n",
                       scram, scramHeld, cruise, held, spool, planned, full);
    check(scram > 30.f && cruise > 300.f && full > 20.f && full < 60.f, "the tanks last over half an hour on the scramjet, hours at cruise, under an hour of full ring thrust");
    check(fabsf(planned / cruise - 1.f) < 0.15f, "the fuel planner's cruise is the one it flies");
  }
  // ---- the surge, and the ring that needs no air: from the corridor a pull to 40 deg with the surge lit - out of the
  // atmosphere on the ring alone, the bank spent in 40 s and recharging after
  {
    Plane p; start(p, 30000.f, 10.f);
    float apogee = 0.f, t = 0.f, bankMin = 1.f, surgeEnd = -1.f;
    for (; t < 400.f && !p.ev.crashed; t += dt) {
      p.ctl.throttle = 1.f;
      if (t > 2.f && !p.surgeOn && surgeEnd < 0.f && p.surgeBank > 0.5f) p.surgeOn = true;
      if (p.surgeN < 0.01f && t > 10.f && surgeEnd < 0.f) surgeEnd = t;
      bankMin = std::min(bankMin, p.surgeBank);
      fly(p, 40.f, t);
      apogee = std::max(apogee, p.pos.y);
      if (p.pos.y < apogee - 2000.f) break;
    }
    if (report) printf("surge: apogee %.1f km, the bank down to %.2f, the surge out at %.0f s, bank %.2f at the end, ring %.0f kN at apogee's air\n",
                       apogee / 1000.f, bankMin, surgeEnd, p.surgeBank, p.thrustAt(1.f, 3000.f, 3000.f, 0.f) / 1e3f);
    check(apogee > 100000.f, "the ring takes it above 100 km");
    check(bankMin < 0.02f && surgeEnd > 35.f && surgeEnd < 50.f && !p.surgeOn, "the surge spends its bank in about 40 s, then goes out");
    check(p.surgeBank > bankMin + 0.1f, "the bank recharges while the surge is off");
  }
  // ---- the hover: the lever down, it holds itself up on its emitters, level, for 30 s
  {
    Plane p; start(p, 300.f, 0.f, 0.5f);
    p.vel = vec3(); p.ctl.flaps = p.flaps = p.nozzle = 1.f; p.ctl.gearDown = true; p.gear = 1.f;
    p.emitShare = 1.f; p.tipFold = 1.f;   // (established in the hover)
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
    if (report) printf("hover: %.1f..%.1f m, attitude within %.1f deg, throttle %.3f, emitters %.0f kN, ring %.0f kN forward, hover max %.0f kN vs weight %.0f kN\n",
                       hMin, hMax, attMax, p.ctl.throttle, (p.emitN[0] + p.emitN[1] + p.emitN[2] + p.emitN[3]) / 1000.f, p.ringN / 1000.f, p.liftThrustMax() / 1000.f, p.mass() * G0 / 1000.f);
    check(!p.ev.crashed && hMax - hMin < 6.f && attMax < 5.f, "it hovers level within a few metres");
    check(p.ringN < 1000.f, "with the lever at its stop the ring makes no forward thrust");
    check(p.liftThrustMax() > 1.1f * p.mass() * G0, "the autopilot sees it can hover (and lands vertically)");
  }
  // ---- the XR-50 several times over: its thrust for its weight at full tanks (static); the most load a full pull and a
  // full push make from the same start, subsonic and at Mach 1.6 low down; the fastest roll
  {
    auto twr = [](int type) { Plane p; start(p, 0.f, 0.f, 1.f, type); return p.thrustAt(1.f, 0.f, 0.f, 1.225f) / (p.mass() * G0); };
    const float tw60 = twr(kPhantasos), tw50 = twr(kMorpheus);
    const float g60 = pull(kPhantasos, 3000.f, 0.95f, 1.f, 0.f, 4.f), g50 = pull(kMorpheus, 3000.f, 0.95f, 1.f, 0.f, 4.f);
    const float f60 = pull(kPhantasos, 1000.f, 1.6f, 1.f, 0.f, 2.f), f50 = pull(kMorpheus, 1000.f, 1.6f, 1.f, 0.f, 2.f);
    const float n60 = pull(kPhantasos, 1000.f, 1.6f, -1.f, 0.f, 2.f), n50 = pull(kMorpheus, 1000.f, 1.6f, -1.f, 0.f, 2.f);
    float r60 = 0.f, r50 = 0.f;
    pull(kPhantasos, 3000.f, 0.9f, 0.f, 1.f, 2.f, &r60); pull(kMorpheus, 3000.f, 0.9f, 0.f, 1.f, 2.f, &r50);
    if (report) printf("vs the XR-50: thrust/weight %.2f / %.2f; full pull at Mach 0.95 %.0f g / %.0f, at Mach 1.6 %.0f g / %.0f; full push %.0f g / %.0f; roll %.0f deg/s / %.0f\n",
                       tw60, tw50, g60, g50, f60, f50, n60, n50, r60 / DEG, r50 / DEG);
    check(tw60 > 2.5f * tw50, "two and a half times the XR-50's thrust for its weight and more");
    check(g60 > 2.5f * g50 && f60 > 2.5f * f50 && f60 >= 500.f, "two and a half times the XR-50's pull and more, 500 g fast and low");
    check(n60 < 2.5f * n50 && n60 <= -400.f, "two and a half times the XR-50's push and more, past -400 g");
    check(r60 > 2.5f * r50 && r60 / DEG > 2400.f, "two and a half times the XR-50's roll and more, past 2,400 deg/s");
    check(f60 < kAircraft[kPhantasos].gLimitPos() && n60 > kAircraft[kPhantasos].gLimitNeg(), "inside its 2,000 g structure either way");
  }
  // ---- post-stall: slow, a full pull stands it past 70 deg of angle of attack, and it recovers
  {
    Plane p; start(p, 4000.f, 0.35f);
    float aMax = 0.f, t = 0.f;
    for (; t < 3.f; t += dt) { p.ctl.pitch = 1.f; p.ctl.roll = 0.f; p.step(dt, calm, t); trace(p, t, "post"); aMax = std::max(aMax, p.alpha / DEG); }
    for (; t < 15.f; t += dt) hold(p, 4000.f, t);
    if (report) printf("post-stall: %.0f deg of angle of attack, then %s at %.0f m\n", aMax, p.ev.crashed ? "crashed" : "flying", p.pos.y);
    check(aMax > 70.f && !p.ev.crashed, "the ring takes it past 70 deg of angle of attack and back");
  }
  printf(failures ? "xr60_flight_test: %d failures\n" : "xr60_flight_test: all passed\n", failures);
  return failures ? 1 : 0;
}
