// Float hydrodynamics (docs/WINGS_OVER_WATER.md): the Kittiwake A9 on the open sea and at its edges, flown through the
// flight model as a pilot flies it - at rest, a take-off run through the hump onto the step, a landing, the classic
// mistakes (the wheels down for the water, too hard an arrival, a beach at speed), the water rudders, weathervaning, a
// rough sea, a runway met with the wheels up - and a landplane on the water, which still ditches.
#include "../src/aircraft.h"
#include "test_world.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) { std::printf("Water: %s: %s\n", what.c_str(), ok ? "ok" : "FAIL"); failures += !ok; }
Weather wind(float speed = 0.f, float from = 270.f) { Weather w; w.windSpeed = speed; w.windFrom = from; w.gust = w.turbulence = 0; return w; }
const AircraftSpec& K = kAircraft[kKittiwake];
const float kLoad = 600.f;
float fuel() { return K.maxFuel * 0.6f; }   // (read when used: kAircraft is initialised at run time, in its own file)

vec3 openSea() {   // (well clear of every coast)
  for (int i = 0; i < 400; i++) {
    const float a = i * 2.39996f, r = WORLD_HALF * (0.35f + 0.4f * (i % 7) / 6.f);
    const vec3 q(cosf(a) * r, 0, sinf(a) * r); bool ok = true;
    for (int k = -3; k <= 3 && ok; k++) for (int j = -3; j <= 3 && ok; j++) ok = g_world.height(q.x + k * 1500.f, q.z + j * 1500.f) < -2.f;
    if (ok) return q;
  }
  return vec3(WORLD_HALF * 0.75f, 0, WORLD_HALF * 0.75f);
}
// a gently shelving shore: from a point 150 m out over 2-6 m of water, the heading straight in and the distance to the line
bool beach(vec3& from, float& heading) {
  for (int i = 0; i < 4000; i++) {
    const float a = i * 2.39996f, r = 3000.f + (i % 37) * 900.f;
    const vec3 q(cosf(a) * r, 0, sinf(a) * r);
    for (int d = 0; d < 16; d++) {
      const float h = d * 22.5f * DEG; const vec3 dir(sinf(h), 0, -cosf(h));
      const float y0 = g_world.height(q.x, q.z), y1 = g_world.height(q.x + dir.x * 150.f, q.z + dir.z * 150.f), y2 = g_world.height(q.x + dir.x * 260.f, q.z + dir.z * 260.f);
      bool even = y0 < -2.f && y0 > -6.f && y1 > 0.2f && y2 > 0.5f && y2 < 8.f;
      for (float s = 0; s < 150.f && even; s += 15.f) even = g_world.height(q.x + dir.x * s, q.z + dir.z * s) < 0.3f || s > 90.f;
      if (even) { from = q; heading = d * 22.5f; return true; }
    }
  }
  return false;
}
float keelDepth(const Plane& p) { const FloatStations f = floatStations(K); return -(p.pos + p.q.rotate(vec3(f.x, f.keelY, f.stepZ))).y; }
void hold(Plane& p, float pitch, float heading) {   // a pilot's hands: the attitude, wings level, straight
  p.ctl.pitch = clampf((pitch - p.pitchDeg()) * 0.1f - p.w.x * 0.6f, -1, 1);
  p.ctl.roll = clampf(-p.bankDeg() * 0.05f + p.w.z * 0.3f, -1, 1);
  p.ctl.yaw = clampf(wrapAngle((heading - p.heading()) * DEG) * 3.f, -1, 1);
}
float dist2d(vec3 a, vec3 b) { return length(vec3(a.x - b.x, 0, a.z - b.z)); }

// a landing on the open sea from 12 m at 1.05 Vref, flared to the sink asked; the run and how it ended
struct Landing { bool crashed = false; std::string why; float fpm = 0, run = 0; bool afloat = false; };
Landing landOnSea(float sinkAsked, bool wheelsDown, const Weather& w) {
  Plane p; p.reset(&K, openSea() + vec3(0, 12, 0), 90, fuel(), kLoad, true, K.vref * 1.05f);
  p.ctl.gearDown = wheelsDown; p.gear = wheelsDown ? 1.f : 0.f; p.ctl.flaps = p.flaps = 1;
  Landing L; vec3 p0; bool down = false;
  for (float t = 0; t < 90 && !p.ev.crashed; t += 1 / 60.f) {
    const float h = p.agl();
    p.ctl.throttle = h > 3 ? 0.25f : 0.f;
    hold(p, h > 3 ? 2.f : 6.f, 90);
    if (h < 3) p.ctl.pitch = clampf(p.ctl.pitch + (-p.vel.y - sinkAsked) * 0.05f, -1, 1);
    if (sinkAsked > 4.f) p.ctl.pitch = -0.3f;   // (driven in: no flare)
    p.step(1 / 60.f, w, t);
    if (p.ev.touchdown && !down) { down = true; p0 = p.pos; L.fpm = -p.ev.touchdownVs * 196.85f; }
    if (down && length(p.vel) < 1.5f) break;
  }
  L.crashed = p.ev.crashed; L.why = p.ev.crashReason; L.run = down ? dist2d(p.pos, p0) : -1.f; L.afloat = p.onWater;
  return L;
}
}

int main() {
  buildTestWorld();
  const vec3 sea = openSea();
  {   // at rest: it floats level-ish at its draft, and stays so
    Plane p; p.reset(&K, sea, 90, fuel(), kLoad, false);
    bool still = true;
    for (int i = 0; i < 60 * 20 && !p.ev.crashed; i++) { p.step(1 / 60.f, wind(), i / 60.f); if (i > 60 * 8) still = still && fabsf(p.vel.y) < 0.2f; }
    const FloatStations f = floatStations(K);
    std::printf("  at rest: %.2f m in at the step (of %.2f), pitch %+.1f deg\n", keelDepth(p), f.depth, p.pitchDeg());
    check(!p.ev.crashed && p.onWater && p.onGround, "it floats (afloat, down, not flying)");
    check(keelDepth(p) > 0.3f * f.depth && keelDepth(p) < 0.65f * f.depth, "its floats draw between a third and two thirds of their depth");
    check(fabsf(p.pitchDeg()) < 6.f && fabsf(p.bankDeg()) < 1.f && still, "it sits level and still");
  }
  float landRoll = 0.f;
  {   // the same aircraft on a runway, its wheels down: the land roll the water run is compared with
    const Airport& a = g_world.airports[g_world.findAirport("CAP")];
    Plane p; vec3 st = a.threshold(false) + a.dir() * 30.f; st.y = a.elev + 3.f; p.reset(&K, st, a.heading, fuel(), kLoad, false);
    p.engineRunning = true; float t = 0;
    for (; t < 2.f; t += 1 / 60.f) { p.ctl.brake = 1; p.step(1 / 60.f, wind(), t); }
    const vec3 p0 = p.pos;
    for (; t < 60 && !p.ev.crashed; t += 1 / 60.f) {
      p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = K.takeoffFlap;
      hold(p, p.ias > K.vr ? 10.f : p.pitchDeg(), a.heading);
      p.step(1 / 60.f, wind(), t);
      if (!p.onGround && p.agl() > 1.f) break;
    }
    landRoll = dist2d(p.pos, p0);
    check(!p.ev.crashed && !p.onGround && landRoll < 400.f, "on its wheels it takes off from a runway (" + std::to_string((int)landRoll) + " m)");
  }
  {   // the take-off run: the bows up through the hump, onto the step, then off
    Plane p; p.reset(&K, sea, 90, fuel(), kLoad, false);
    for (int i = 0; i < 120; i++) p.step(1 / 60.f, wind(), i / 60.f);
    p.engineRunning = true;
    const vec3 p0 = p.pos; float t = 0, wetHump = 0, wetLate = 1, lastV = 0, humpAcc = 99, startAcc = 0;
    for (; t < 60 && !p.ev.crashed; t += 1 / 60.f) {
      p.ctl.throttle = 1; p.ctl.flaps = K.takeoffFlap; p.ctl.gearDown = false;
      const float v = length(p.vel);
      hold(p, v < 12.f ? 9.f : p.ias < K.vr ? 5.f : 9.f, 90);
      p.step(1 / 60.f, wind(), t);
      const float acc = (length(p.vel) - lastV) * 60.f; lastV = length(p.vel);
      if (t > 0.5f && t < 2.f) startAcc = std::max(startAcc, acc);
      if (v > 5.f && v < 16.f) humpAcc = std::min(humpAcc, acc);
      if (v > 7.f && v < 9.f) wetHump = p.floatWet;
      if (v > 25.f && v < 27.f) wetLate = p.floatWet;
      if (!p.onGround && p.agl() > 3.f) break;
    }
    const float run = dist2d(p.pos, p0);
    std::printf("  take-off: %.0f m in %.1f s (the land roll %.0f m); acceleration %.2f at the start, %.2f at the hump; wetted %.2f at the hump, %.2f on the step\n",
                run, t, landRoll, startAcc, humpAcc, wetHump, wetLate);
    check(!p.ev.crashed && !p.onGround, "it takes off from the water");
    check(run < 600.f && run > 1.3f * landRoll && run < 2.6f * landRoll, "the water run is longer than the land roll, as a float plane's is");
    check(humpAcc < 0.8f * startAcc, "the hump: the acceleration sags as the bow wave builds");
    check(wetLate < 0.5f * wetHump, "on the step it rides on a fraction of the float it ploughed with");
  }
  {   // landings
    const Landing L = landOnSea(0.9f, false, wind());
    std::printf("  water landing: %.0f fpm, %s, run %.0f m\n", L.fpm, L.crashed ? L.why.c_str() : "stopped", L.run);
    check(!L.crashed && L.afloat && L.run > 80.f && L.run < 450.f, "it lands on the water and stops afloat within 450 m");
    const Landing W = landOnSea(0.9f, true, wind());
    check(W.crashed && W.why.find("wheels down") != std::string::npos, "landed with the wheels down it goes over on its nose (" + W.why + ")");
    const Landing H = landOnSea(7.f, false, wind());
    check(H.crashed && H.why.find("too hard") != std::string::npos, "driven onto the water it breaks (" + H.why + ")");
  }
  {   // the water rudders: at taxi power, full rudder turns it round
    Plane p; p.reset(&K, sea, 90, fuel(), kLoad, false);
    p.engineRunning = true; float h0 = 0;
    for (float t = 0; t < 40 && !p.ev.crashed; t += 1 / 60.f) { p.ctl.throttle = 0.25f; p.ctl.yaw = t > 15 ? 1.f : 0.f; p.step(1 / 60.f, wind(), t); if (fabsf(t - 15.f) < 0.01f) h0 = p.heading(); }
    const float turned = wrapDeg360(p.heading() - h0);
    check(!p.ev.crashed && turned > 90.f && turned < 330.f, "the water rudders turn it on the water (" + std::to_string((int)turned) + " deg in 25 s)");
  }
  {   // weathervaning: left alone, it turns into the wind
    Plane p; p.reset(&K, sea, 0, fuel(), kLoad, false);
    for (float t = 0; t < 90 && !p.ev.crashed; t += 1 / 60.f) p.step(1 / 60.f, wind(8, 270), t);
    check(!p.ev.crashed && fabsf(wrapAngle((p.heading() - 270.f) * DEG)) < 25.f * DEG, "left alone it weathervanes into the wind");
  }
  {   // a rough sea: it rides it out, the chop the open water's
    Plane p; p.reset(&K, sea, 90, fuel(), kLoad, false);
    for (float t = 0; t < 30 && !p.ev.crashed; t += 1 / 60.f) p.step(1 / 60.f, wind(11, 200), t);
    check(!p.ev.crashed && p.onWater && p.seaChop > 0.2f, "it rides out a rough open sea");
    const Landing L = landOnSea(0.9f, false, wind(9, 90));
    check(!L.crashed, "it lands on a choppy sea into the wind (" + (L.crashed ? L.why : std::string("ok")) + ")");
  }
  {   // a runway with the wheels up: it slides to a stop on its keels, the floats scraped
    const Airport& a = g_world.airports[g_world.findAirport("CAP")];
    Plane p; vec3 st = a.threshold(false) + a.dir() * 300.f; st.y = a.elev + 12.f;
    p.reset(&K, st, a.heading, fuel(), kLoad, true, K.vref * 1.05f);
    p.ctl.gearDown = false; p.gear = 0; p.ctl.flaps = p.flaps = 1;
    for (float t = 0; t < 60 && !p.ev.crashed; t += 1 / 60.f) {   // (flown down and flared as onto the water)
      const float h = p.pos.y - a.elev - 2.45f;
      p.ctl.throttle = h > 3 ? 0.25f : 0.f;
      const vec3 off = p.pos - a.pos(); const float across = off.x * a.dir().z * -1.f + off.z * a.dir().x;   // (right of the centre line)
      hold(p, h > 3 ? 2.f : 6.f, a.heading - clampf(across * 0.4f, -8.f, 8.f));
      if (h < 3) p.ctl.pitch = clampf(p.ctl.pitch + (-p.vel.y - 0.9f) * 0.05f, -1, 1);
      p.step(1 / 60.f, wind(), t);
      if (p.onGround && length(p.vel) < 0.5f) break;
    }
    std::printf("  wheels up on a runway: %s, belly %d, %.1f m/s, on the ground %d\n", p.ev.crashed ? p.ev.crashReason.c_str() : "not crashed", (int)p.ev.bellyLanding, length(p.vel), (int)p.onGround);
    check(!p.ev.crashed && p.ev.bellyLanding && length(p.vel) < 0.5f, "landed on a runway with the wheels up it slides to a stop on its keels, damaged");
  }
  {   // a beach: in at a walking pace it grounds gently; at speed it is aground
    vec3 from; float hdg;
    if (!beach(from, hdg)) check(false, "a gently shelving beach in the test islands");
    else for (float speed : {1.5f, 16.f}) {
      Plane p; p.reset(&K, from, hdg, fuel(), kLoad, false);
      for (int i = 0; i < 120; i++) p.step(1 / 60.f, wind(), i / 60.f);
      p.vel = p.forward() * speed; p.engineRunning = true;
      for (float t = 0; t < 120 && !p.ev.crashed; t += 1 / 60.f) {   // (in at a walk on the engine's idle; at speed on the step, on power)
        p.ctl.throttle = speed < 5 ? 0.08f : 0.7f; hold(p, speed < 5 ? p.pitchDeg() : 4.f, hdg); p.ctl.yaw = 0;
        p.step(1 / 60.f, wind(), t); if (t > 5 && length(p.vel) < 0.3f) break;
      }
      std::printf("  beach at %.1f m/s: %s, %.1f m/s, keel ground %d, afloat %d, ground under it %.2f\n", speed, p.ev.crashed ? p.ev.crashReason.c_str() : "not crashed", length(p.vel), (int)p.onGround, (int)p.onWater, g_world.height(p.pos.x, p.pos.z));
      if (speed < 5) check(!p.ev.crashed, "run in onto a beach at a walking pace it grounds gently");
      else check(p.ev.crashed && p.ev.crashReason == "Ran aground", "run onto a beach at speed it is aground (" + p.ev.crashReason + ")");
    }
  }
  {   // a landplane on the water ditches, as before
    Plane p; p.reset(&kAircraft[1], sea + vec3(0, 4, 0), 90, 60, 100, true, kAircraft[1].vref);
    p.ctl.throttle = 0; p.ctl.flaps = p.flaps = 1;
    for (float t = 0; t < 30 && !p.ev.crashed; t += 1 / 60.f) { hold(p, 4.f, 90); p.step(1 / 60.f, wind(), t); }
    check(p.ev.crashed && p.ev.crashReason == "Ditched in the sea", "a landplane on the water ditches");
  }
  std::printf("%s\n", failures ? "WATER TEST FAILED" : "water test passed");
  return failures ? 1 : 0;
}
