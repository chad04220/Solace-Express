// Solace Express - learning each aircraft's envelope by flying it (see PerfModel in aircraft.h)
#include "aircraft.h"
#include <mutex>

namespace {
// a patch of open sea away from the islands for the test sorties (flat, no terrain to hit)
vec3 seaPoint() {
  static vec3 p; static bool found = false;
  if (!found) {
    found = true; p = vec3(WORLD_HALF * 0.75f, 0, WORLD_HALF * 0.75f);
    for (int i = 0; i < 400; i++) {
      float a = i * 2.39996f, r = WORLD_HALF * (0.35f + 0.4f * (i % 7) / 6.f);
      vec3 q(cosf(a) * r, 0, sinf(a) * r);
      bool sea = true;
      for (int k = -2; k <= 2 && sea; k++) for (int j = -2; j <= 2 && sea; j++) sea = g_world.height(q.x + k * 1500.f, q.z + j * 1500.f) < -2.f;
      if (sea) { p = q; break; }
    }
  }
  return p;
}
}

const PerfModel& Plane::perf(const AircraftSpec* sp) {
  static PerfModel cache[16]; static int state[16] = {};   // 0 not learned, 1 learning (provisional numbers), 2 learned
  static std::recursive_mutex m;
  int idx = (int)(sp - kAircraft);
  std::lock_guard<std::recursive_mutex> lk(m);
  if (idx < 0 || idx >= 16) { static PerfModel none; return none; }
  if (state[idx]) return cache[idx];   // (while learning, the test sorties fly on the provisional numbers below)
  state[idx] = 1;
  const AircraftSpec& s = *sp;
  PerfModel& P = cache[idx];
  Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
  const float fuel = s.maxFuel * 0.6f, payload = 150.f;
  const vec3 sea = seaPoint() + vec3(0, 1200.f, 0);
  const float W = (s.emptyMass + fuel + payload) * G0, rho = 1.225f * expf(-1200.f / 8500.f);
  P.vs1 = sqrtf(2.f * W / (rho * s.wingArea * s.CLmax));
  P.vs0 = sqrtf(2.f * W / (rho * s.wingArea * (s.CLmax + s.flapCL)));
  P.gLimit = s.special == 2 ? 90.f : s.special ? 50.f : 5.8f; P.gNeg = s.special == 2 ? -45.f : s.special ? -25.f : -3.f;
  // provisional numbers, used by the inner loops while the test sorties below fly
  P.vy = s.vref * 1.4f; P.roc = s.special ? 30.f : s.engineType == ENG_JET ? 12.f : 4.f; P.sinkIdle = 4.f;
  P.rollRate = s.special ? 5.f : 1.f; P.gPerStick = 3.f; P.tG = 1.f; P.tRoll = 0.5f; P.qPerStick = 0.6f; P.tQ = 0.5f; P.gPull = P.gLimit; P.gUse = std::min(P.gLimit * 0.8f, 3.8f); P.bankMax = 60.f;
  auto fresh = [&](Plane& p, float v, bool dirty) {
    p.reset(&s, sea, 90.f, fuel, payload, true, v);
    p.ctl.gearDown = dirty || !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.flaps = dirty ? 1.f : 0.f; p.flaps = p.ctl.flaps;
    p.ctl.throttle = 0.7f; p.engineRunning = true; p.engineSpool = 0.7f;
  };
  // 1. elevator and aileron power: open-loop steps at cruise against an identical aircraft left alone. These come
  //    first because the closed-loop sorties below fly on them.
  if (!s.special) {
    float pr[150], qr[150];
    auto run = [&](float pitch, float roll, float* g, float& pMax) {
      Plane p; fresh(p, s.cruise, false);
      pMax = 0;
      for (int k = 0; k < 150; k++) { p.ctl.pitch = pitch; p.ctl.roll = roll; if (!p.ev.crashed) p.step(1 / 60.f, calm, k / 60.f); g[k] = p.gLoad; pr[k] = fabsf(p.w.z); qr[k] = p.w.x; pMax = std::max(pMax, pr[k]); }
    };
    float g0[150], g1[150], pm;
    float q0[150];
    run(0.f, 0.f, g0, pm);
    for (int k = 0; k < 150; k++) q0[k] = qr[k];
    run(0.2f, 0.f, g1, pm);
    P.qPerStick = 0.05f; P.tQ = 0.5f;
    for (int k = 0; k < 150; k++) if ((qr[k] - q0[k]) / 0.2f > P.qPerStick) { P.qPerStick = (qr[k] - q0[k]) / 0.2f; P.tQ = std::max(0.15f, k / 60.f); }
    P.gPerStick = 0.3f; P.tG = 0.5f;
    for (int k = 0; k < 150; k++) if ((g1[k] - g0[k]) / 0.2f > P.gPerStick) { P.gPerStick = (g1[k] - g0[k]) / 0.2f; P.tG = std::max(0.25f, k / 60.f); }
    run(0.f, 1.f, g1, pm);
    P.rollRate = std::max(0.2f, pm);
    P.tRoll = 1.f;
    for (int k = 0; k < 150; k++) if (pr[k] > pm * 0.63f) { P.tRoll = std::max(0.1f, k / 60.f); break; }
  } else { P.gPerStick = 40.f; P.tG = 0.3f; P.tRoll = 0.15f; P.qPerStick = 3.f; P.tQ = 0.2f; Plane p; fresh(p, s.cruise, false); P.rollRate = p.fbwRollMax(0.f); }
  P.gPull = std::min(P.gLimit, 1.f + P.gPerStick);
  P.gUse = std::max(1.6f, std::min(P.gLimit, P.gPull));
  P.bankMax = clampf(acosf(1.f / P.gUse) / DEG, 35.f, 85.f);
  // speed held with the elevator on the autopilot's inner loops, throttle as given; returns the specific excess power
  // (energy height rate, m/s: what it would climb at if it held the speed exactly)
  auto excessPower = [&](float v, bool dirty, float thr, float secs) {
    Plane p; fresh(p, v, dirty);
    p.apEngage(Plane::AP_HOLD, -1, calm); p.apSpeed = 0; p.apUseVS = true; p.apVS = 0; p.apHeading = p.heading();
    const int n = (int)(secs * 60), from = n / 2;
    float e0 = 0;
    for (int k = 0; k < n && !p.ev.crashed; k++) {
      p.apVS = clampf(p.vel.y + (p.ias - v) * 0.4f, -60.f, 60.f);
      p.ctl.throttle = thr;
      p.step(1 / 60.f, calm, k / 60.f);
      p.ctl.throttle = thr;
      float e = p.pos.y + dot(p.vel, p.vel) / (2.f * G0);
      if (k == from) e0 = e;
      if (k == n - 1) return (e - e0) / ((n - 1 - from) / 60.f);
    }
    return -99.f;   // crashed
  };
  // 2. best climb: full power over a range of speeds (reheat included)
  P.roc = 0; P.vy = s.vref * 1.4f;
  for (float k = 1.15f; k <= 3.2f; k += 0.15f) {
    float v = std::min(s.vref * k, s.cruise * 1.1f);
    float ps = excessPower(v, false, 1.f, 20.f);
    ps = std::min(ps, v * 0.9f);
    if (ps > P.roc) { P.roc = ps; P.vy = v; }
    if (v >= s.cruise * 1.1f) break;
  }
  // 3. idle descent, full flap and gear at 1.25 Vs0 (or Vref)
  P.sinkIdle = std::max(0.5f, -excessPower(std::max(P.vs0 * 1.25f, s.vref * 0.95f), true, 0.f, 16.f));
  state[idx] = 2;
  return P;
}
