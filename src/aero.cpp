// Solace Express - aerodynamics from each airframe's geometry (see aero.h)
// Methods: the component drag build-up and the lift-slope / span-efficiency estimates of Raymer, "Aircraft Design: A
// Conceptual Approach" ch. 12 (DATCOM lift slope, Torenbeek fuselage wetted area, Korn critical Mach).
#include "aero.h"
#include "aircraft.h"
#include "models.h"
#include <mutex>

namespace {
float formFactorWing(float tc, float mach) {   // thin surfaces, max thickness at 30% chord, unswept
  return (1.f + 0.6f / 0.3f * tc + 100.f * tc * tc * tc * tc) * (1.34f * powf(std::max(mach, 0.05f), 0.18f));
}

AeroModel build(const AircraftSpec& s) {
  AeroModel a;
  const bool jet = s.engineType == ENG_JET, research = s.special != 0;
  const float d = 2.f * s.fusRad, L = s.fusLen, f = L / std::max(d, 0.1f);
  const float S = s.wingArea, b = s.span, c = s.chord;
  a.AR = b * b / S;
  // section: thick GA aerofoils, thinner on the jet, very thin on the research craft
  a.tc = research ? 0.05f : jet ? 0.12f : s.engineType == ENG_TURBOPROP ? 0.16f : 0.15f;
  a.korn = jet ? 0.93f : 0.87f;
  a.roughness = research ? 2.0e-5f : jet ? 0.6e-5f : 1.0e-5f;   // radar-absorbent coating / polished / production sheet metal
  float lam = research ? 0.25f : jet ? 0.1f : 0.12f;
  auto add = [&](const char* name, float swet, float len, float ffq, float laminar) { a.parts[a.nParts++] = {name, swet, len, ffq, laminar}; };
  // fuselage: Torenbeek's wetted area of a streamlined body of revolution, form factor from its fineness
  float fusWet = PI * d * L * powf(std::max(1.f - 2.f / f, 0.2f), 2.f / 3.f) * (1.f + 1.f / (f * f));
  add("fuselage", fusWet, L, 1.f + 60.f / (f * f * f) + f / 400.f, 0.f);
  // wing: the part outside the fuselage, both surfaces
  float sExp = std::max(S - d * c, S * 0.6f);
  a.exposed = sExp / S;
  float wingQ = s.wingY > 0.5f ? 1.f : 1.05f;   // high wing clean, low wing filleted
  add("wing", sExp * (1.977f + 0.52f * a.tc), c, formFactorWing(a.tc, 0.2f) / 1.f * wingQ, lam);
  // tails: as they are drawn (models.cpp; the XR-20's canards and twin fins), else typical volume-coefficient sizes
  float sh = research ? 0.08f * S : 0.22f * S, sv = research ? 0.10f * S : 0.14f * S;
  {
    const int idx = (int)(&s - kAircraft);
    if (idx >= 0 && idx < kNumAircraft + 4 && s.special == 0) {
      const ModelDef& m = kModels[idx];
      if (m.ht[0] > 0.f) sh = m.ht[0] * (m.ht[1] + m.ht[2]);
      if (m.vt[0] > 0.f) sv = m.vt[0] * 0.5f * (m.vt[1] + m.vt[2]) * (idx == kMantis ? 2.f : 1.f);
    }
  }
  float tailQ = s.tail == 1 ? 1.08f : 1.04f;
  add("horizontal tail", sh * 2.05f, c * 0.7f, formFactorWing(0.10f, 0.2f) * tailQ, lam);
  add("vertical tail", sv * 2.05f, c * 0.9f, formFactorWing(0.10f, 0.2f) * tailQ, lam);
  // nacelles: wing-mounted props or rear-fuselage jets, sized by the power / thrust they house
  if (!research && s.engLayout != 0) {
    float nd, nl;
    if (jet) { nd = 0.23f * sqrtf(s.power / 1000.f); nl = 3.2f * nd; }
    else if (s.engineType == ENG_TURBOPROP) { nd = 0.6f + 0.5f * sqrtf(s.power / 1e6f); nl = 4.5f * nd; }
    else { nd = 0.55f + 0.3f * sqrtf(s.power / 1e5f); nl = 3.f * nd; }
    add("nacelles", s.engines * 0.9f * PI * nd * nl, nl, (1.f + 0.35f / (nl / nd)) * (jet ? 1.3f : 1.2f), 0.f);
  }
  // drag areas that don't follow skin friction
  if (!s.retract && !jet && s.wingY > 1.f) a.extraDq += 0.06f;                                       // wing struts (faired)
  if (s.engineType == ENG_PISTON) a.extraDq += 0.12e-6f * s.power * s.engines;                         // cooling air
  else if (s.engineType == ENG_TURBOPROP) a.extraDq += 0.03e-6f * s.power * s.engines;                 // intakes, oil cooler
  // landing gear (when down): grows with the weight it carries; fixed gear is faired, retractable gear isn't
  // (rough-field types run big unfaired tyres; the research craft's gear is small for its weight)
  float mtow = s.emptyMass + s.maxFuel + s.cargoKg;
  a.gearDq = (s.retract ? 0.0018f : s.roughOK ? 0.0019f : 0.00128f) * powf(mtow, 2.f / 3.f) * (s.taildragger ? 0.85f : 1.f) * (research ? 0.6f : 1.f);
  a.misc = research ? 0.10f : jet ? 0.04f : s.engineType == ENG_TURBOPROP ? 0.08f : 0.15f;   // antennas, rivets, steps, leaks
  a.fuseFactor = 1.07f * (1.f + d / b) * (1.f + d / b);
  a.sideArea = 0.8f * L * d + sv;
  a.planArea = 0.8f * L * d;
  // span efficiency (Kroo): the planform's own (u ~ 0.99), the fuselage's interference, and the viscous drag that grows
  // with lift, which scales with the airframe's parasite drag
  float cd0 = aeroCD0(a, s, s.cruise, 1.225f, s.cruise / 340.f);
  a.e = clampf(1.f / (1.f / (0.99f * (1.f - 2.f * (d / b) * (d / b))) + 0.38f * cd0 * PI * a.AR), 0.6f, 0.95f);
  return a;
}
}

Atmosphere isa(float h) {
  const float g = 9.80665f, R = 287.053f, T0 = 288.15f, p0 = 101325.f;
  float T, p;
  if (h < 11000.f) { T = T0 - 0.0065f * h; p = p0 * powf(T / T0, g / (R * 0.0065f)); }
  else {
    const float T11 = 216.65f, p11 = p0 * powf(T11 / T0, g / (R * 0.0065f));
    if (h < 20000.f) { T = T11; p = p11 * expf(-g * (h - 11000.f) / (R * T11)); }
    else { const float p20 = p11 * expf(-g * 9000.f / (R * T11)); T = T11 + 0.001f * (h - 20000.f); p = p20 * powf(T / T11, -g / (R * 0.001f)); }
  }
  Atmosphere at;
  at.T = T; at.p = p; at.rho = p / (R * T); at.a = sqrtf(1.4f * R * T);
  at.mu = 1.458e-6f * T * sqrtf(T) / (T + 110.4f);
  return at;
}

float calibratedAirspeed(float tas, const Atmosphere& at) {
  const float a0 = 340.294f, p0 = 101325.f;
  const float M = tas / at.a;
  float qc;   // the impact pressure the pitot feels
  if (M < 1.f) qc = at.p * (powf(1.f + 0.2f * M * M, 3.5f) - 1.f);
  else qc = at.p * (166.92158f * powf(M, 7.f) / powf(7.f * M * M - 1.f, 2.5f) - 1.f);   // (Rayleigh: behind the normal shock)
  // the speed at sea level that gives that impact pressure (subsonic; past it, the supersonic formula solved by a few steps)
  float cas = a0 * sqrtf(std::max(5.f * (powf(qc / p0 + 1.f, 2.f / 7.f) - 1.f), 0.f));
  if (cas > a0) {
    float m = cas / a0;
    for (int i = 0; i < 6; i++) { float f = 166.92158f * powf(m, 7.f) / powf(7.f * m * m - 1.f, 2.5f) - 1.f - qc / p0;
                                  float d = (166.92158f * powf(m, 7.f) / powf(7.f * m * m - 1.f, 2.5f)) * (7.f / m - 35.f * m / (7.f * m * m - 1.f)); m -= f / d; }
    cas = m * a0;
  }
  return std::copysign(cas, tas);
}

const AeroModel& aeroModel(const AircraftSpec& s) {
  static AeroModel cache[16]; static bool built[16] = {};
  static std::mutex m;
  int idx = (int)(&s - kAircraft);
  if (idx < 0 || idx >= 16) { static thread_local AeroModel tmp; tmp = build(s); return tmp; }
  std::lock_guard<std::mutex> lk(m);
  if (!built[idx]) { cache[idx] = build(s); built[idx] = true; }
  return cache[idx];
}

float aeroCD0(const AeroModel& a, const AircraftSpec& s, float V, float rho, float mach) {
  const float mu = 1.79e-5f;
  V = std::max(V, 5.f);
  float fric = 0;
  for (int i = 0; i < a.nParts; i++) {
    const AeroModel::Part& p = a.parts[i];
    // skin friction: laminar run then turbulent, the turbulent part no lower than the surface roughness allows
    float Re = rho * V * p.len / mu, ReCut = 38.21f * powf(p.len / a.roughness, 1.053f);
    float Ret = std::min(Re, ReCut), lg = log10f(std::max(Ret, 1e4f));
    float cfTurb = 0.455f / (powf(lg, 2.58f) * powf(1.f + 0.144f * mach * mach, 0.65f));
    float cfLam = 1.328f / sqrtf(std::max(Re, 1e4f));
    float cf = p.laminar * cfLam + (1.f - p.laminar) * cfTurb;
    float ffq = p.ffq;
    if (p.name[0] == 'w' || p.name[0] == 'h' || p.name[0] == 'v')   // surfaces: the Mach part of the form factor
      ffq *= powf(std::max(mach, 0.05f) / 0.2f, 0.18f);
    fric += cf * ffq * p.swet;
  }
  return (fric * (1.f + a.misc) + a.extraDq) / s.wingArea;
}

void aeroDragAreas(const AeroModel& a, const AircraftSpec& s, float V, float rho, float mu, float mach, float out[4]) {
  V = std::max(V, 5.f);
  out[0] = out[1] = out[2] = out[3] = 0.f;
  for (int i = 0; i < a.nParts; i++) {
    const AeroModel::Part& p = a.parts[i];
    float Re = rho * V * p.len / mu, ReCut = 38.21f * powf(p.len / a.roughness, 1.053f);
    float Ret = std::min(Re, ReCut), lg = log10f(std::max(Ret, 1e4f));
    float cfTurb = 0.455f / (powf(lg, 2.58f) * powf(1.f + 0.144f * mach * mach, 0.65f));
    float cfLam = 1.328f / sqrtf(std::max(Re, 1e4f));
    float cf = p.laminar * cfLam + (1.f - p.laminar) * cfTurb;
    float ffq = p.ffq;
    const bool surface = p.name[0] == 'w' || p.name[0] == 'h' || p.name[0] == 'v';
    if (surface) ffq *= powf(std::max(mach, 0.05f) / 0.2f, 0.18f);
    float dq = cf * ffq * p.swet * (1.f + a.misc);
    out[p.name[0] == 'w' ? 0 : p.name[0] == 'h' ? 1 : p.name[0] == 'v' ? 2 : 3] += dq;
  }
  out[3] += a.extraDq;
  (void)s;
}

float aeroCLa(const AeroModel& a, float mach) {
  float beta2 = std::max(1.f - mach * mach, 0.15f), eta = 0.95f;
  float cla = 2.f * PI * a.AR / (2.f + sqrtf(4.f + a.AR * a.AR * beta2 / (eta * eta)));
  return cla * std::min(a.exposed * a.fuseFactor, 0.98f);
}

float aeroWave(const AeroModel& a, float mach, float CL) {
  float mdd = a.korn - a.tc - 0.1f * std::max(CL, 0.f), mcrit = mdd - 0.108f;   // (0.1/80)^(1/3)
  float x = mach - mcrit;
  return x > 0 ? 20.f * x * x * x * x : 0.f;
}
