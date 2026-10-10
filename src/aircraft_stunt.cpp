// Solace Express - aerobatics on the autopilot
//
// Each figure is a short list of steps the pilot flies through the shared inner loops (Plane::apRates): pull through
// so many degrees of pitch at the figure's load factor, roll through so many degrees at full rate, hold a line, and so
// on. Progress is measured by integrating the body rates, so a step means the same thing upright, inverted or pointing
// at the sky. The figure is sized to the airframe: it pulls what the structure takes (less a gust margin) or what the
// wing can lift at that speed, whichever is less, enters at a speed that carries it over the top, and won't start
// until it has the height to finish. A ground check runs the whole time and turns any figure into a recovery.
#include "aircraft.h"
#include <algorithm>

namespace {
struct Step { char op; float a; };   // P pull (deg), R roll (deg), B barrel roll (deg), U pitch up to (deg),
                                      // H hold a line (s), W wingover, X recover to level flight
const Step kFig[Plane::STUNT_COUNT][12] = {
  {{'P', 360}, {'X', 0}},                                                                          // loop
  {{'U', 18}, {'R', 360}, {'X', 0}},                                                               // aileron roll
  {{'U', 25}, {'B', 360}, {'X', 0}},                                                               // barrel roll
  {{'P', 180}, {'R', 180}, {'X', 0}},                                                              // Immelmann
  {{'U', 8}, {'R', 180}, {'P', 180}, {'X', 0}},                                                    // split-S
  {{'P', 225}, {'H', 0.8f}, {'R', 180}, {'H', 1.f}, {'P', 270}, {'H', 0.8f}, {'R', 180}, {'H', 1.f}, {'X', 0}},   // Cuban eight
  {{'U', 30}, {'W', 0}, {'X', 0}},                                                                 // wingover
};
// height each figure needs above the entry (up) and below it (down), in loop radii
const float kUp[Plane::STUNT_COUNT] = {2.2f, 0.3f, 1.2f, 2.2f, 0.3f, 2.2f, 1.f};
const float kDown[Plane::STUNT_COUNT] = {0.3f, 0.5f, 0.8f, 0.2f, 5.f, 0.6f, 0.8f};   // (a split-S picks up speed on the way down)
}

const char* Plane::stuntName(int f) {
  static const char* n[STUNT_COUNT] = {"LOOP", "AILERON ROLL", "BARREL ROLL", "IMMELMANN", "SPLIT-S", "CUBAN EIGHT", "WINGOVER"};
  return f >= 0 && f < STUNT_COUNT ? n[f] : "";
}

void Plane::apStuntBegin(int figure, const Weather& wx) {
  float keepSpeed = apOn && apSpeed > 0 ? apSpeed : spec->cruise * 0.8f;
  apStuntWasOn = apOn; apStuntWasMode = apMode; apStuntWasAirport = apAirport >= 0 ? apAirport : apHoldFor; apStuntEnded = false;
  apEngage(AP_HOLD, -1, wx);
  apMode = AP_STUNT;
  apStunt = ((figure % STUNT_COUNT) + STUNT_COUNT) % STUNT_COUNT; apStuntStep = 0; apStuntAng = 0; apStuntT = 0;
  apStuntDir = (apStunt + (int)(pos.x * 0.01f)) % 2 ? -1 : 1;   // rolls and wingovers go either way
  apStuntSpeedAfter = keepSpeed; apStuntAbort.clear();
  const PerfModel& P = perf(spec);
  apSense();
  // the figure's load factor: what the structure takes less what a gust could add; its entry speed: enough that the
  // wing can still pull a useful load over the top
  float gust = std::max(0.6f, P.gLimit * 0.05f);
  apStuntN = std::max(2.5f, (P.gLimit - gust) * 0.85f);
  // (enough speed that after climbing the figure's height it still flies over the top: 2.8 Vs for a loop; a split-S
  // starts slow because it builds its speed on the way down, a wingover needs only a little over the stall)
  bool down = apStunt == STUNT_SPLIT_S, gentle = apStunt == STUNT_WINGOVER || apStunt == STUNT_ROLL;
  // (from the stall as it is now: heavier or iced, it needs more; a wing that flies at high alpha stalls far below the
  // speed it flies cleanly at, so it gets a proper entry speed)
  apStuntV = apEnv.vs1 * (down ? 1.8f : gentle ? 2.2f : 2.8f);
  if (apEnv.highAlpha) apStuntV = std::max(apStuntV, down ? 110.f : 160.f);
  apStuntHdg = heading();
}

void Plane::apStuntStop() {
  if (apMode != AP_STUNT) return;
  apStuntAbort = "stopped";
  if (apStuntStep == 0) { apMode = AP_HOLD; apUseVS = false; apAlt = pos.y; apHeading = heading(); apSpeed = apStuntSpeedAfter; return; }
  const Step* fig = kFig[apStunt];
  int i = 0; while (i < 11 && fig[i].op != 'X') i++;
  apStuntStep = i + 1; apStuntAng = 0;
}

bool Plane::apStuntFly(float dt) {
  const AircraftSpec& s = *spec;
  const PerfModel& P = perf(spec);
  apStuntT += dt;
  float spd = std::max(length(vel), 1.f), V = std::max(ias, 15.f);
  vec3 upB = q.rotate(vec3(0, 1, 0)), rightB = q.rotate(vec3(1, 0, 0));
  float upY = upB.y;                                         // 1 upright and level, -1 inverted
  const ApEnvelope& E = apEnv;
  float stallG = (V / E.vs1) * (V / E.vs1) * 0.9f;
  float nPull = std::min(apStuntN, std::max(stallG * 0.8f, 0.5f));   // over the top: float over, don't stall it
  float rollCap = (E.rateCmd ? fbwRollMax(0.f) : P.rollRate * clampf(V / s.cruise, 0.25f, 2.f)) * 0.95f;
  float gust = std::max(0.6f, P.gLimit * 0.05f);
  float nzMax = std::min(P.gLimit - gust - P.gLimit * 0.03f, std::max(stallG, 1.05f)), nzMin = std::max(P.gNeg + gust + 0.3f, -std::max(stallG, 0.5f) * 0.5f);
  float R = apStuntV * apStuntV / (G0 * std::max(apStuntN - 1.f, 1.f));   // the figure's loop radius
  float ground = std::max(g_world.height(pos.x, pos.z), g_world.height(pos.x + vel.x * 4.f, pos.z + vel.z * 4.f));
  float agl = pos.y - std::max(ground, 0.f);
  float margin = E.spool >= 2.f ? 200.f : 120.f;   // (engines slow to spool: more room to recover in)
  const Step* fig = kFig[apStunt];
  if (s.retract) ctl.gearDown = false;
  if (!E.rateCmd) ctl.flaps = 0;   // (a fly-by-wire craft schedules its own)
  auto finish = [&](const char* why) {
    if (why) apStuntAbort = why;
    apMode = AP_HOLD; apUseVS = false; apAlt = std::max(pos.y, ground + 150.f); apHeading = heading(); apSpeed = apStuntSpeedAfter;
    apStatus = why ? fmt("STUNT ABORTED  %s", why) : fmt("%s COMPLETE", stuntName(apStunt));
    apStuntEnded = true;   // (level again: the game hands the aircraft back as it was before the figure)
  };

  // ---- setting up: the speed and height the figure needs, wings level, before it starts
  if (apStuntStep == 0) {
    float need = margin + kDown[apStunt] * R;
    bool fast = ias >= apStuntV * 0.97f, high = agl >= need;
    if (apStunt == STUNT_SPLIT_S) fast = fast && ias <= apStuntV * 1.3f;   // (slow enough not to finish into the ground)
    bool level = fabsf(bankDeg()) < 10.f && fabsf(pitchDeg()) < 12.f;
    apHeading = apStuntHdg; apSpeed = apStuntV * 1.05f;
    if (!high) { apUseVS = false; apAlt = ground + need + 80.f; }                                     // climb first
    else if (!fast && ias < apStuntV && agl > need + 250.f) { apUseVS = true; apVS = -spd * 0.3f; }     // dive for speed
    else if (apUseVS) { apUseVS = false; apAlt = pos.y; }                                                 // (then hold here)
    if (fast && high && level) { apStuntStep = 1; apStuntAng = 0; apStuntT = 0; return apStuntFly(0); }
    if (apStuntT > 150.f) { finish("not enough performance for it here"); return false; }
    apStatus = fmt("STUNT  %s  setting up: %s %.0f/%.0f kt, %.0f/%.0f m", stuntName(apStunt), fast ? "speed ok" : "speed", ias * MS_TO_KT,
                   apStuntV * MS_TO_KT, agl, need);
    return false;   // the normal loops fly the setup
  }

  // ---- ground: if a pull-out from here at the figure's load factor (after rolling upright) wouldn't clear it, recover
  Step st = fig[apStuntStep - 1];
  if (st.op != 'X') {
    float sink = -vel.y;
    if (sink > 0) {
      float gam = asinf(clampf(sink / spd, 0.f, 1.f));
      float Rp = spd * spd / (G0 * std::max(nPull - 1.f, 0.5f));
      float rollT = upY < 0.5f ? fabsf(bankDeg()) * DEG / std::max(rollCap, 0.2f) : 0.f;
      float needH = Rp * (1.f - cosf(gam)) + sink * (rollT + 0.6f) + margin * 0.5f;
      if (agl < needH) { apStuntAbort = "ground"; apStuntStep = (int)(std::find_if(fig, fig + 12, [](const Step& x) { return x.op == 'X'; }) - fig) + 1; st = {'X', 0}; }
    }
  }

  // ---- the step
  float qT = 0, pT = 0, thr = 1.f;
  // keep the wings square to the figure's plane: the wing axis where it was when the step began (roll error about the
  // nose, right-handed: rolling right lowers the right wing)
  if (apStuntAng == 0 && (st.op == 'P' || st.op == 'U')) apStuntRight0 = rightB;
  float rollErr = dot(cross(rightB, apStuntRight0), forward());
  float wingsLevel = clampf(rollErr * 3.f, -rollCap, rollCap);
  auto pullAt = [&](float n) { return G0 * (n - upY) / spd; };    // pitch rate that bends the path at load factor n
  bool next = false;
  switch (st.op) {
    case 'P':
      qT = pullAt(nPull); pT = wingsLevel;
      apStuntAng += std::max(w.x * dt / DEG, 1e-4f);
      if (vel.y < -0.3f * spd && ias > apStuntV * 1.3f) thr = 0.3f;     // coming down the back: don't build more speed
      if (apStunt == STUNT_SPLIT_S) thr = 0.f;
      next = apStuntAng >= st.a;
      apStatus = fmt("STUNT  %s  pull %.1f g  %.0f/%.0f deg", stuntName(apStunt), gLoad, apStuntAng, st.a);
      break;
    case 'R': case 'B': {
      // roll through the angle: full rate, then onto the bank it should end at (a roll is judged by where it stops)
      if (apStuntAng == 0) apStuntBank0 = bankDeg();
      float rate = st.op == 'R' ? rollCap : std::min(rollCap * 0.6f, 2.f * PI / 6.f);
      float endErr = wrapAngle((apStuntBank0 + apStuntDir * st.a - bankDeg()) * DEG);   // rad
      pT = apStuntDir * rate;
      if (apStuntAng >= st.a - 45.f) pT = clampf(endErr * 4.f, -rate, rate);
      // aileron roll: hold the attitude (a straight line); barrel roll: a steady pull that bends the path round a
      // helix, the part of gravity across it taken out so it neither sinks nor balloons
      qT = st.op == 'R' ? 0.f : G0 * (1.f + (std::min(nPull, 3.f) - 1.f) * 0.5f) / spd;
      apStuntAng += fabsf(w.z) * dt / DEG;
      next = apStuntAng >= st.a - 45.f && fabsf(endErr) < 6.f * DEG;
      apStatus = fmt("STUNT  %s  roll %.0f/%.0f deg", stuntName(apStunt), apStuntAng, st.a);
      break;
    }
    case 'U': {   // (much faster than the figure was sized for, a shallower pitch-up does: the energy is there)
      float target = st.a * clampf(apStuntV / std::max(ias, 1.f), 0.35f, 1.f);
      qT = pullAt(std::min(nPull, 1.f + (apStuntN - 1.f) * 0.6f)); pT = wingsLevel;
      apStuntAng += 1e-4f;
      next = pitchDeg() >= target;
      apStatus = fmt("STUNT  %s  pitch up %.0f/%.0f", stuntName(apStunt), pitchDeg(), target);
      break;
    }
    case 'H':
      qT = 0; pT = 0; apStuntAng += dt;
      next = apStuntAng >= st.a;
      apStatus = fmt("STUNT  %s", stuntName(apStunt));
      break;
    case 'W': {   // roll steeply over the top of a climbing turn and let the nose fall through the horizon
      // pull for the turn, banked so the lift's vertical part is half a g and the nose falls through the horizon
      float nW = std::min(nPull, 1.f + (apStuntN - 1.f) * 0.35f);
      float bankT = apStuntDir * acosf(std::min(1.f, 0.5f / std::max(nW, 0.5f))) / DEG;
      pT = clampf((bankT - bankDeg()) * DEG * 3.f, -rollCap, rollCap);
      float cb = std::max(cosf(bankDeg() * DEG), 0.03f);
      qT = pullAt(std::min(nW, std::max(1.2f, 0.5f / cb)));
      float turned = fabsf(wrapAngle((heading() - apStuntHdg) * DEG) / DEG);
      next = turned > 165.f || (turned > 120.f && pitchDeg() < -10.f);
      apStatus = fmt("STUNT  %s  %.0f/180 deg", stuntName(apStunt), turned);
      break;
    }
    case 'X': {   // back to level flight: inverted, roll upright (unloaded); nose high, ease it down; nose low, pull
      float bank = bankDeg(), pitch = pitchDeg();
      bool steepDown = pitch < -60.f;                         // pointing at the ground: just pull, whatever the bank
      if (!steepDown && fabsf(bank) > 100.f) { pT = fabsf(bank) > 175.f ? rollCap : clampf(-bank * DEG * 3.f, -rollCap, rollCap); qT = 0; }
      else {
        pT = steepDown ? 0.f : clampf(-bank * DEG * 3.f, -rollCap, rollCap);
        bool noseLow = vel.y < -0.5f || pitch < -2.f;
        if (noseLow) qT = pullAt(nPull);
        else if (pitch > 12.f) qT = std::max(-pitch * DEG * 0.6f, G0 * (-2.f - upY) / spd);   // lower the nose to the horizon
        else qT = pullAt(upY);                                   // level: hold the line
        if (!noseLow && pitch <= 12.f && fabsf(bank) < 15.f) { apRates(qT, pT, rollCap, nzMin, nzMax, dt); ctl.throttle = 1.f; finish(apStuntAbort.empty() ? nullptr : apStuntAbort.c_str()); return true; }
      }
      if (vel.y < -0.3f * spd && ias > apStuntV * 1.3f) thr = 0.f;
      apStatus = apStuntAbort.empty() ? fmt("STUNT  %s  recovering", stuntName(apStunt)) : fmt("STUNT  %s  ABORT (%s): recovering", stuntName(apStunt), apStuntAbort.c_str());
      break;
    }
  }
  apRates(qT, pT, rollCap, nzMin, nzMax, dt);
  if (ias > apStuntV * 1.6f) thr = std::min(thr, 0.4f);   // far faster than the figure needs: don't add more
  ctl.throttle = thr;
  if (next) { apStuntStep++; apStuntAng = 0; }
  return true;
}
