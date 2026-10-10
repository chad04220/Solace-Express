// Solace Express - learning each aircraft's envelope by flying it (see PerfModel in aircraft.h)
#include "aircraft.h"
#include "aero.h"
#include <mutex>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
// a patch of open sea away from the islands for the test sorties (flat, no terrain to hit)
vec3 seaPoint() {
  // (found once, whole, before any caller reads it: the aircraft learn their envelopes on several threads at once)
  static const vec3 p = [] {
    vec3 p(WORLD_HALF * 0.75f, 0, WORLD_HALF * 0.75f);
    for (int i = 0; i < 400; i++) {
      float a = i * 2.39996f, r = WORLD_HALF * (0.35f + 0.4f * (i % 7) / 6.f);
      vec3 q(cosf(a) * r, 0, sinf(a) * r);
      bool sea = true;
      for (int k = -2; k <= 2 && sea; k++) for (int j = -2; j <= 2 && sea; j++) sea = g_world.height(q.x + k * 1500.f, q.z + j * 1500.f) < -2.f;
      if (sea) { p = q; break; }
    }
    return p;
  }();
  return p;
}
}

static PerfModel s_perf[kAircraftCount]; static int s_perfState[kAircraftCount] = {};   // 0 not learned, 1 learning (provisional numbers), 2 learned
static std::recursive_mutex s_perfM[kAircraftCount];   // (one per type: different types can be learned on different threads at once)
// The learned models on disk, stamped with the build that learned them (another build learns them again)
bool Plane::perfLoad(const std::string& path, const std::string& stamp) {
  FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
  char st[64] = {}; uint32_t n = 0, sz = 0; PerfModel tmp[kAircraftCount];
  bool ok = fread(st, 1, 64, f) == 64 && stamp == std::string(st, strnlen(st, 64)) && fread(&n, 4, 1, f) == 1 && fread(&sz, 4, 1, f) == 1
            && n == (uint32_t)kNumAircraft && n <= kAircraftCount && sz == sizeof(PerfModel) && fread(tmp, sizeof(PerfModel), n, f) == n;
  fclose(f);
  if (!ok) return false;
  for (uint32_t i = 0; i < n; i++) { std::lock_guard<std::recursive_mutex> lk(s_perfM[careerSpecAt(i)]); const int spec = careerSpecAt(i); if (!s_perfState[spec]) { s_perf[spec] = tmp[i]; s_perfState[spec] = 2; } }
  return true;
}
void Plane::perfSave(const std::string& path, const std::string& stamp) {
  FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
  char st[64] = {}; strncpy(st, stamp.c_str(), 63);
  uint32_t n = (uint32_t)kNumAircraft, sz = sizeof(PerfModel);
  fwrite(st, 1, 64, f); fwrite(&n, 4, 1, f); fwrite(&sz, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) fwrite(&perf(&kAircraft[careerSpecAt(i)]), sizeof(PerfModel), 1, f);
  fclose(f);
}
const PerfModel& Plane::perf(const AircraftSpec* sp) {
  PerfModel* cache = s_perf; int* state = s_perfState; std::recursive_mutex* m = s_perfM;
  int idx = (int)(sp - kAircraft);
  if (idx < 0 || idx >= kAircraftCount) { static PerfModel none; return none; }
  std::lock_guard<std::recursive_mutex> lk(m[idx]);
  if (state[idx]) return cache[idx];   // (while learning, the test sorties fly on the provisional numbers below)
  state[idx] = 1;
  const AircraftSpec& s = *sp;
  PerfModel& P = cache[idx];
  Weather calm; calm.windSpeed = 0; calm.turbulence = 0; calm.gust = 0;
  // The appended production types count cabin occupants as well as hold cargo. In the older
  // learner, 150 kg represented the payload even for an airliner, and full-weight runway tests
  // omitted its passengers. Preserve the established fleet's envelopes while measuring these
  // types at their real reference / design maximum masses.
  const bool fullCabinEnvelope = s.fullCabinEnvelope;
  const float cabinPayload = s.cargoKg + s.pax * 85.f + 85.f;
  const float fuel = s.maxFuel * 0.6f, payload = performanceReferencePayload(s);
  const vec3 sea = seaPoint() + vec3(0, 1200.f, 0);
  const float W = (s.emptyMass + fuel + payload) * G0, rho = isaDensity(1200.f);
  P.vs1 = sqrtf(2.f * W / (rho * s.wingArea * aeroCLmaxFlown(s, 0)));   // (the wing's, unless its controls run out first)
  P.vs0 = sqrtf(2.f * W / (rho * s.wingArea * aeroCLmaxFlown(s, 1)));   // (the flaps as far as they go)
  P.gLimit = s.gLimitPos(); P.gNeg = s.gLimitNeg();
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
  // the rear-up: a full pull at idle from level flight at the fast final's speed, until the belly-up would stop (68 deg,
  // or zooming); how far the nose gets is whether the belly-up sheds the speed at all - a canard that stalls first, or
  // a tail that runs out of authority, holds the angle of attack down and turns the speed into height instead (the
  // revised XR-20 reached 23 deg, zoomed 50 m and arrived on the final 15 m/s fast)
  P.rearPitch = 90.f;
  if (!s.special) {
    Plane p; fresh(p, s.vref * 1.7f, false);
    p.ctl.throttle = 0; p.engineSpool = 0;
    P.rearPitch = 0;
    for (int k = 0; k < 4 * 60 && !p.ev.crashed && p.vel.y < 18.f; k++) {
      p.ctl.pitch = 1.f; p.ctl.throttle = 0; p.step(1 / 60.f, calm, k / 60.f);
      P.rearPitch = std::max(P.rearPitch, p.pitchDeg());
      if (P.rearPitch > 68.f) break;
    }
  }
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
  // 4. cruise: level at 1500 m on the altitude hold, 75% power, mid weight; the true airspeed it settles at
  {
    Plane p; p.reset(&s, seaPoint() + vec3(0, 1500.f, 0), 90.f, s.maxFuel * 0.5f, fullCabinEnvelope ? cabinPayload * 0.5f : s.cargoKg * 0.5f, true, s.cruise);
    p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.engineRunning = true; p.engineSpool = 0.75f;
    p.apEngage(Plane::AP_HOLD, -1, calm); p.apSpeed = 0; p.apAlt = p.pos.y; p.apHeading = p.heading();
    float sum = 0; int n = 0;
    for (int k = 0; k < 150 * 30 && !p.ev.crashed; k++) { p.ctl.throttle = 0.75f; p.step(1 / 30.f, calm, k / 30.f); if (k > 120 * 30) { sum += length(p.vel); n++; } }
    P.cruiseV = n ? sum / n : s.cruise;
  }
  // 5. the take-off and landing rolls at full weight on a paved runway (Solace Capital), brought to sea level
  if (!s.special) {
    const Airport& a = g_world.airports[std::max(g_world.findAirport("CAP"), 0)];
    const float mtowFuel = s.maxFuel * (fullCabinEnvelope ? 0.75f : 1.f);
    const float mtowLoad = fullCabinEnvelope ? s.maxMass() - s.emptyMass - mtowFuel : s.cargoKg;
    const float elevK = 1.f + a.elev / 3000.f;
    {
      Plane p; vec3 st = a.threshold(false) + a.dir() * 30.f; st.y = a.elev + 3.f;
      p.reset(&s, st, a.heading, mtowFuel, mtowLoad, false);
      p.engineRunning = true; p.engineSpool = 0.f; p.sceneryHits = false;
      vec3 p0;
      float t = 0;
      for (; t < 2.f; t += 1 / 60.f) { p.ctl.brake = 1; p.step(1 / 60.f, calm, t); }
      p0 = p.pos; P.toRoll = -1;
      for (; t < 120.f && !p.ev.crashed; t += 1 / 60.f) {
        p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = s.taildragger ? 0.3f : s.takeoffFlap;
        if (p.ias > s.vr) p.ctl.pitch = clampf((10.f - p.pitchDeg()) * 0.08f - p.w.x * 0.5f, -1, 1);
        else if (s.taildragger && p.ias > s.vr * 0.5f) p.ctl.pitch = -0.3f;
        p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
        p.ctl.yaw = clampf(wrapAngle((a.heading - p.heading()) * DEG) * 3.f, -1, 1);
        p.step(1 / 60.f, calm, t);
        if (!p.onGround && p.agl() > 1.f) { P.toRoll = length(vec3(p.pos.x - p0.x, 0, p.pos.z - p0.z)) / elevK; break; }   // (the ground roll to lift-off)
      }
    }
    {
      // full flap, idle, then the brakes as hard as the type takes (a taildragger brakes gently, as the autopilot's
      // rollout does); Vs0 at this weight and the field's air
      float W = (s.emptyMass + mtowFuel + mtowLoad) * G0, rho = isaDensity(a.elev);
      float vs0 = sqrtf(2.f * W / (rho * s.wingArea * aeroCLmaxFlown(s, 1)));
      Plane p; vec3 st = a.threshold(false) + a.dir() * 200.f; st.y = a.elev;
      p.reset(&s, st, a.heading, mtowFuel, mtowLoad, false);
      p.sceneryHits = false;
      for (int i = 0; i < 60; i++) p.step(1 / 60.f, calm, 0.f);   // (settled on its wheels)
      // (the roll starts at touchdown, 1.1 Vs0 - a taildragger's three-point at the stall: at 1.1 Vs0 in that attitude the
      // Bushmaster's full-flap wing flew again - and the approach at 1.3 Vs0 is the air segment below)
      p.vel = p.forward() * (vs0 * (s.taildragger ? 1.f : 1.1f)); p.ctl.flaps = 1; p.flaps = 1; p.ctl.throttle = 0; p.engineRunning = true;
      vec3 p0 = p.pos; P.ldgRoll = -1;
      for (float t = 0; t < 120.f && !p.ev.crashed; t += 1 / 60.f) {
        // (a taildragger as the autopilot's rollout flies it: the stick back, the brakes in as the tail is down and off the
        // moment the nose starts down - held tail-up with the stick forward, it nosed over onto its propeller)
        p.ctl.throttle = 0; p.ctl.pitch = s.taildragger ? (p.ias > s.vref * 0.8f ? 0.15f : 1.f) : 0.f;
        p.ctl.brake = s.taildragger ? 0.55f * smoothstepf(4.f, 9.f, p.pitchDeg()) * (p.w.x < -0.02f ? 0.f : 1.f) : 1.f;
        p.ctl.yaw = clampf(wrapAngle((a.heading - p.heading()) * DEG) * 3.f, -1, 1);
        p.step(1 / 60.f, calm, t);
        // (the landing distance: the approach path from 15 m over the threshold - 3 deg, or the 6.5 deg the autopilot
        // flies a STOL type at (Plane::apPlan) - then this ground roll)
        float air = 15.f / tanf((s.runwayM < 300.f ? 6.5f : 3.f) * DEG);
        if (length(p.vel) < 0.5f) { P.ldgRoll = (air + length(vec3(p.pos.x - p0.x, 0, p.pos.z - p0.z))) / elevK; break; }
      }
    }
  }
  state[idx] = 2;
  return P;
}

bool surfaceOK(const AircraftSpec& s, int surface) {
  if (surface == SURF_ASPHALT) return true;
  if (surface == SURF_GRASS || surface == SURF_SAND) return s.runwayM < 900;
  return s.roughOK;
}
bool runwayOK(const AircraftSpec& s, const Airport& a) { return surfaceOK(s, a.surface) && a.length >= s.runwayNeeded(a.elev); }

float AircraftSpec::runwayNeeded(float elev) const {
  const PerfModel& P = Plane::perf(this);
  float base = special || P.toRoll <= 0 || P.ldgRoll <= 0 ? runwayM : std::max(P.toRoll, P.ldgRoll) * 1.15f;
  // Published dispatch floors retain margin for the flight segment, runway conditions and
  // operational handling. A learned raw ground roll must not admit a 74 t transport to a short strip.
  if (fullCabinEnvelope) base = std::max(base, runwayM);
  return base * (1.0f + elev / 3000.0f);
}
