// Solace Express - aircraft roster and 6-DOF flight model
#include "aircraft.h"
#include "entities.h"
#include "scenery.h"

// clang-format off
const AircraftSpec kAircraft[] = {
  // id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
  // power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
  // fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
  {"kestrel", "Kestrel T2", "Two-seat trainer", ENG_PISTON, 1, 4, 2, 750, 2600, 530, 70, 120, 1, 14.9f, 10.1f, 1.5f,
   0.30f, 4.8f, 1.45f, 0.55f, 0.030f, 0.004f, 0.045f, 0.75f, 82000, 22, 25, 30, 50, 45, 400, false, false, false,
   900, 1300, 1900, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 18000, 120,
   7.3f, 0.62f, 1.19f, -0.92f, 0, 0, vec3(0.92f, 0.92f, 0.95f), vec3(0.85f, 0.12f, 0.10f)},
  {"wren", "Wren 180", "Four-seat tourer", ENG_PISTON, 1, 6, 2, 700, 2700, 820, 110, 320, 3, 16.2f, 11.0f, 1.5f,
   0.30f, 4.9f, 1.50f, 0.65f, 0.028f, 0.004f, 0.050f, 0.76f, 135000, 23, 28, 33, 60, 70, 450, false, false, false,
   1500, 2000, 3000, 0.40f, 0.058f, 0.060f, LIC_PPL, 30000, 250,
   8.3f, 0.68f, 1.18f, -1.07f, 0, 0, vec3(0.95f, 0.95f, 0.92f), vec3(0.10f, 0.30f, 0.70f)},
  {"bush", "Bushmaster STOL", "Backcountry taildragger", ENG_PISTON, 1, 6, 3, 700, 2700, 760, 120, 480, 4, 21.5f, 12.4f, 1.75f,
   0.35f, 5.0f, 1.85f, 0.95f, 0.034f, 0.006f, 0.070f, 0.74f, 220000, 18, 18, 24, 55, 72, 220, true, true, false,
   1500, 2100, 3200, 0.42f, 0.065f, 0.070f, LIC_CPL, 40000, 450,
   8.0f, 0.70f, 1.17f, -1.05f, 0, 0, vec3(0.95f, 0.75f, 0.10f), vec3(0.12f, 0.12f, 0.12f)},
  {"islander", "Islander Twin", "Nine-seat utility twin", ENG_PISTON, 2, 6, 2, 700, 2700, 1750, 260, 900, 9, 30.2f, 14.9f, 2.05f,
   0.32f, 4.9f, 1.55f, 0.70f, 0.036f, 0.006f, 0.060f, 0.76f, 195000, 22, 26, 31, 65, 90, 420, true, false, false,
   9000, 9500, 17000, 0.40f, 0.055f, 0.060f, LIC_CPL, 85000, 900,
   10.9f, 0.85f, 1.12f, -1.38f, 1, 0, vec3(0.96f, 0.96f, 0.96f), vec3(0.05f, 0.55f, 0.45f)},
  {"pelican", "Pelican Caravan", "Single turboprop hauler", ENG_TURBOPROP, 1, 0, 3, 1100, 1900, 2150, 420, 1400, 12, 25.9f, 15.9f, 1.95f,
   0.32f, 5.0f, 1.60f, 0.90f, 0.030f, 0.005f, 0.060f, 0.78f, 540000, 30, 31, 38, 85, 130, 550, true, false, false,
   12000, 14000, 24000, 0.40f, 0.052f, 0.060f, LIC_CPL, 120000, 1600,
   11.5f, 0.92f, 1.09f, -1.83f, 0, 0, vec3(0.95f, 0.95f, 0.95f), vec3(0.85f, 0.45f, 0.05f)},
  {"meridian", "Meridian Q400", "Regional turboprop airliner", ENG_TURBOPROP, 2, 0, 4, 900, 1300, 12000, 1500, 4500, 40, 54.0f, 27.4f, 2.1f,
   0.30f, 5.2f, 1.50f, 0.80f, 0.026f, 0.010f, 0.060f, 0.80f, 1800000, 40, 52, 60, 140, 170, 1100, false, false, true,
   180000, 300000, 450000, 0.40f, 0.048f, 0.055f, LIC_ATP, 600000, 8000,
   26.0f, 1.35f, 0.93f, -0.38f, 1, 1, vec3(0.96f, 0.96f, 0.98f), vec3(0.08f, 0.18f, 0.45f)},
  {"starling", "Starling 500 Jet", "Light business jet", ENG_JET, 2, 0, 0, 0, 0, 4600, 1100, 700, 7, 30.0f, 15.9f, 2.0f,
   0.25f, 5.0f, 1.40f, 0.75f, 0.022f, 0.012f, 0.070f, 0.80f, 15000, 0, 55, 62, 200, 260, 1250, false, false, true,
   30000, 60000, 85000, 0.42f, 0.050f, 0.055f, LIC_ATP, 260000, 0,
   14.0f, 0.95f, -0.58f, 1.0f, 2, 1, vec3(0.97f, 0.97f, 0.97f), vec3(0.55f, 0.08f, 0.12f)},
  // hidden research model: thrust-to-weight ~2.2, supersonic, pitch thrust vectoring (see Plane::substep special path)
  {"xr9", "XR-9 Specter", "Confidential research model", ENG_JET, 2, 0, 0, 0, 0, 9000, 3000, 0, 0, 46.0f, 11.2f, 4.6f,
   0.05f, 3.6f, 1.70f, 0.0f, 0.013f, 0.010f, 0.0f, 0.75f, 118000, 0, 60, 70, 420, 4000, 250, true, false, true,
   25000, 90000, 110000, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 0, 0,
   17.2f, 1.0f, -0.2f, 1.6f, 2, 1, vec3(0.11f, 0.12f, 0.14f), vec3(0.2f, 0.85f, 1.0f), 1},
  // hidden stealth aerobatic research model: four tilting thruster pods (power = one pod's dry thrust), T/W ~2.2 dry
  // and ~4.6 boosted, structure good for +90 / -45 g (see Plane::wraithThrust)
  {"xr11", "XR-11 Wraith", "Stealth aerobatic research model", ENG_JET, 4, 0, 0, 0, 0, 10500, 3000, 0, 0, 52.0f, 12.4f, 5.2f,
   0.03f, 3.4f, 1.60f, 0.0f, 0.012f, 0.010f, 0.0f, 0.72f, 62000, 0, 60, 70, 480, 5000, 200, true, false, true,
   30000, 80000, 100000, 0.90f, 0.090f, 0.070f, LIC_STUDENT, 0, 0,
   16.5f, 1.0f, -0.15f, 0.8f, 2, 1, vec3(0.075f, 0.08f, 0.09f), vec3(0.72f, 0.3f, 1.0f), 2},
};
// clang-format on
const int kNumAircraft = sizeof(kAircraft) / sizeof(kAircraft[0]) - 2;  // the research craft are not part of the career

float Plane::fuelFlowMax() const {
  float rangeS = spec->rangeKm * 1000.f / spec->cruise;
  return spec->maxFuel / (rangeS * 0.8f);
}
float Plane::rangeLeftKm() const { return fuel / (fuelFlowMax() * 0.8f) * spec->cruise / 1000.f; }

void Plane::reset(const AircraftSpec* s, vec3 position, float headingDeg, float fuelKg, float payloadKg, bool airborne, float speed) {
  spec = s; pos = position; fuel = fuelKg; payload = payloadKg;
  q = quat::axisAngle(vec3(0, 1, 0), -headingDeg * DEG);
  w = vec3(); ctl = Controls(); ev = FlightEvents();
  flaps = 0; gear = 1; rpm = 0; n1 = 0; engineSpool = 0; maxG = minG = 1; flightTime = 0;
  apDisengage(); apDone = false; apPitchI = 0; gust = vec3(); rng = Rng(77);
  // Calibrate parasitic drag so 75% power yields the published cruise speed at sea level
  {
    float V = s->cruise, qS = 0.5f * 1.225f * V * V * s->wingArea;
    float W = (s->emptyMass + s->maxFuel * 0.6f + s->cargoKg * 0.5f) * G0;
    float CL = W / qS, AR = s->span * s->span / s->wingArea;
    float T = s->engineType == ENG_JET ? s->engines * s->power * 0.75f * (1.f - 0.3f * V / 340.f)
                                       : s->engines * s->power * 0.75f * 0.8f / sqrtf(V * V + s->v0 * s->v0);
    float cl0 = s->CL0, a = (CL - cl0) / s->CLa;
    cd0 = T / qS - CL * CL / (PI * s->oswald * AR) - (s->retract ? 0.f : s->gearCD) - 0.4f * 0.f - 0 * a;
    cd0 = std::max(cd0, 0.012f);
    if (s->special) cd0 = 0.0125f;   // research jet: clean low-drag airframe, wave drag added in flight
  }
  nozzle = 0; mach = 0;
  if (airborne) {
    vel = forward() * speed; engineRunning = true; engineSpool = 0.7f; ctl.throttle = 0.7f;
    rpm = s->engineType == ENG_PISTON ? s->maxRpm * 0.88f : s->maxRpm; n1 = 90;
    onGround = wasOnGround = false; gear = s->retract ? 0 : 1; ctl.gearDown = !s->retract;
  } else {
    vel = vec3(); engineRunning = false; starterTime = 0;
    pos.y = g_world.height(pos.x, pos.z, 7) + gearHeight() + (s->taildragger ? 0.25f : 0.05f);
    onGround = wasOnGround = true; ctl.brake = 1;
    if (s->taildragger) q = q * quat::axisAngle(vec3(1, 0, 0), atanf(0.2f) - 0.005f);  // sit on mains + tail wheel
  }
}

float Plane::gearHeight() const {
  const AircraftSpec& s = *spec;
  if (s.taildragger) return s.fusRad * 1.0f + 0.45f;
  return s.fusRad * 1.3f + (s.engineType == ENG_JET || s.engines == 2 ? 0.75f : 0.55f);
}

float Plane::heading() const { vec3 f = forward(); return wrapDeg360(atan2f(f.x, -f.z) / DEG); }
float Plane::pitchDeg() const { return asinf(clampf(forward().y, -1, 1)) / DEG; }
float Plane::bankDeg() const {
  vec3 r = right(), f = forward();
  vec3 upH = normalize(cross(r, f));
  return atan2f(-r.y, upH.y) / DEG;
}
float Plane::agl() const { float h = g_world.height(pos.x, pos.z, 6); return pos.y - std::max(h, 0.f); }

void Plane::step(float dt, const Weather& wx, float time) {
  const int N = std::max(1, (int)ceilf(dt / (1.f / 240.f)));
  float h = dt / N;
  ev.touchdown = false;
  if (apOn) apGuidance(dt);
  for (int i = 0; i < N && !ev.crashed; i++) substep(h, wx, time + h * i);
  flightTime += dt;
}

struct Contact { vec3 p; int kind; };  // kind 0 main L, 1 main R, 2 nose/tail wheel, 3+ structure

void Plane::substep(float dt, const Weather& wx, float time) {
  const AircraftSpec& s = *spec;
  float m = mass();
  float altAgl = agl();
  density = 1.225f * expf(-pos.y / 8500.f);
  float sigmaRho = density / 1.225f;

  // ---------------- wind and turbulence
  float prof = clampf(powf(std::max(altAgl, 1.f) / 10.f, 0.14f), 0.35f, 1.6f);
  float wf = wx.windFrom * DEG;
  vec3 baseWind = vec3(-sinf(wf), 0, cosf(wf)) * (wx.windSpeed * prof);
  float gv, d1, d2, gv2, gv3;
  noised(time * 0.35f, 1.7f, gv, d1, d2);
  noised(time * 0.27f, 9.1f, gv2, d1, d2);
  noised(time * 1.9f, 4.4f, gv3, d1, d2);
  float turb = wx.turbulence * (1.f + 1.5f * smoothstepf(300.f, 0.f, altAgl) * (altAgl > 3 ? 1.f : 0.f));
  if (wx.storm) turb += 0.6f;
  gust = vec3(gv * wx.gust * 0.7f, gv3 * turb * 2.2f, gv2 * wx.gust * 0.7f) + normalize(baseWind) * (std::max(0.f, gv) * wx.gust * 0.5f);
  windVel = baseWind + gust;

  // ---------------- engine
  bool hasFuel = fuel > 0;
  if (!engineRunning && starterTime > 0) {
    starterTime += dt;
    if (starterTime > (s.engineType == ENG_PISTON ? 1.6f : 3.0f) && hasFuel) engineRunning = true;
  }
  if (!hasFuel) engineRunning = false;
  float spoolRate = s.engineType == ENG_PISTON ? 3.0f : (s.engineType == ENG_TURBOPROP ? 0.7f : (s.special ? 1.6f : 0.45f));
  float target = engineRunning ? ctl.throttle : 0.f;
  engineSpool = approach(engineSpool, target, spoolRate, dt);
  vec3 vaW = vel - windVel;
  vec3 va = q.conj().rotate(vaW);
  float V = length(va);
  airspeed = V; ias = V * sqrtf(sigmaRho);
  if (s.engineType == ENG_PISTON) {
    float tr = engineRunning ? s.idleRpm + (s.maxRpm * 0.86f - s.idleRpm) * powf(engineSpool, 0.7f) + s.maxRpm * 0.12f * clampf(-va.z / s.cruise, 0, 1.3f) * sqrtf(engineSpool)
                             : (starterTime > 0 && !engineRunning ? 180.f + 40.f * sinf(time * 9.f) : clampf(-va.z * 18.f, 0, 1500));
    rpm = approach(rpm, tr, 4.f, dt);
  } else if (s.engineType == ENG_TURBOPROP) {
    float tn = engineRunning ? 64.f + 36.f * engineSpool : (starterTime > 0 ? 18.f + 6 * starterTime : 0.f);
    n1 = approach(n1, tn, engineRunning ? 1.5f : 0.4f, dt);
    rpm = approach(rpm, engineRunning ? s.idleRpm + (s.maxRpm - s.idleRpm) * smoothstepf(0.f, 0.6f, engineSpool) : n1 * 8.f, 1.2f, dt);
  } else {
    float tn = engineRunning ? 24.f + 76.f * engineSpool : (starterTime > 0 ? 6.f + 5 * starterTime : 0.f);
    n1 = approach(n1, tn, engineRunning ? 1.2f : 0.35f, dt);
    rpm = n1;
  }
  float thrust = 0;
  if (engineRunning) {
    if (s.special == 2) {
      // four thruster pods: dry up to 85% throttle, boost above; ram compression adds thrust with Mach
      float ab = smoothstepf(0.85f, 1.0f, engineSpool);
      thrust = s.engines * s.power * powf(sigmaRho, 0.5f) * (0.85f * engineSpool + 1.25f * ab) * (1.f + 0.3f * std::min(V / 330.f, 4.f));
    } else if (s.special) {
      // two afterburning turbofans: dry up to 85% throttle, reheat above (2x thrust at 100%, T/W ~4.4)
      float ab = smoothstepf(0.85f, 1.0f, engineSpool);
      thrust = s.engines * s.power * powf(sigmaRho, 0.6f) * (0.82f * engineSpool + 1.18f * ab);   // full reheat doubles thrust
    } else if (s.engineType == ENG_JET) {
      float mach = V / 340.f;
      thrust = s.engines * s.power * engineSpool * powf(sigmaRho, 0.75f) * (1.f - 0.3f * mach);
    } else {
      float P = s.engines * s.power * engineSpool * (s.engineType == ENG_PISTON ? sigmaRho : powf(sigmaRho, 0.75f));
      float vf = std::max(0.f, -va.z);
      thrust = P * 0.8f / sqrtf(vf * vf + s.v0 * s.v0);
    }
    if (!s.special) fuel = std::max(0.f, fuel - fuelFlowMax() * (0.2f + 0.8f * ctl.throttle) * dt);
  }

  // ---------------- configuration
  flaps = approach(flaps, ctl.flaps, s.special ? 0.5f : 0.6f, dt);
  nozzle = s.special == 2 ? flaps : 0.f;   // the XR-11's F/V keys tilt its thruster pods instead of flaps
  if (s.retract) gear = clampf(gear + (ctl.gearDown ? 1.f : -1.f) * dt / 5.f, 0, 1);
  else gear = 1;

  // ---------------- aerodynamics
  vec3 F(0, 0, 0), T(0, 0, 0);  // body-frame force and torque
  vec3 Taero(0, 0, 0), surfMax(0, 0, 0);   // XR-11: passive aerodynamic torque and full-deflection surface authority
  float AR = s.span * s.span / s.wingArea;
  stallWarn = 0;
  if (V > 0.5f) {
    alpha = atan2f(-va.y, -va.z);
    beta = asinf(clampf(va.x / V, -1, 1));
    float qbar = 0.5f * density * V * V;
    float cl0 = s.CL0 + s.flapCL * flaps;
    float aStall = (s.CLmax + s.flapCL * flaps * 0.95f - cl0) / s.CLa;
    float aNeg = (-1.1f - cl0) / s.CLa;
    float sig = std::max(smoothstepf(aStall, aStall + 5 * DEG, alpha), smoothstepf(-aNeg, -aNeg + 5 * DEG, -alpha));
    float CL = (1 - sig) * clampf(cl0 + s.CLa * alpha, -1.2f, s.CLmax + s.flapCL * flaps) + sig * 1.05f * sinf(2 * alpha);
    float ge = 1.f;
    if (altAgl < s.span) ge = 1.f + 0.12f * (1.f - altAgl / s.span);
    CL *= ge;
    float CD = cd0 + s.gearCD * gear + s.flapCD * flaps + CL * CL / (PI * s.oswald * AR) / ge + sig * (0.35f + 1.1f * sinf(alpha) * sinf(alpha)) + 0.4f * beta * beta;
    mach = V / (340.f * sqrtf(std::max(1.f - pos.y / 44000.f, 0.6f)));
    if (s.special) CD += 0.022f * smoothstepf(0.86f, 1.04f, mach) - 0.007f * smoothstepf(1.2f, 2.2f, mach);  // transonic drag rise
    float CY = -0.7f * beta;
    vec3 liftDir = normalize(cross(vec3(1, 0, 0), va));
    vec3 dragDir = va * (-1.f / V);
    F += liftDir * (CL * qbar * s.wingArea) + dragDir * (CD * qbar * s.wingArea) + vec3(CY * qbar * s.wingArea, 0, 0);

    float Vh = std::max(V, 12.f);
    float p = -w.z, qq = w.x, r = -w.y;
    float ph = p * s.span / (2 * Vh), qh = qq * s.chord / (2 * Vh), rh = r * s.span / (2 * Vh);
    // trim reference so the aircraft flies hands-off near cruise
    float clCruise = (s.emptyMass + s.maxFuel * 0.6f + s.cargoKg * 0.5f) * G0 / (0.5f * 1.225f * s.cruise * s.cruise * s.wingArea);
    float aCruise = (clCruise - s.CL0) / s.CLa;
    float Cma = -1.1f, Cmq = -16.f;
    float ctlEff = 1.f - 0.4f * sig;
    float Cm = Cma * (alpha - aCruise) + Cmq * qh + s.elevPow * (ctl.pitch + ctl.trim * 0.4f) * ctlEff - 0.06f * flaps;
    // prop-wash over the tail keeps the elevator alive at low speed
    float wash = s.engineType != ENG_JET ? clampf(thrust / (m * G0) * 2.f, 0, 1) * smoothstepf(30.f, 5.f, V) : 0.f;
    Cm += s.elevPow * ctl.pitch * wash * 1.5f;
    if (onGround) Cm += s.elevPow * ctl.pitch * 1.2f;  // main-gear pivot geometry helps rotation
    float Cl = -0.10f * beta - 0.55f * ph + 0.08f * rh + s.ailPow * ctl.roll * ctlEff;
    float Cn = 0.09f * beta - 0.16f * rh + s.rudPow * ctl.yaw * (1.f + wash) - 0.012f * ctl.roll;
    if (s.engines == 1 && s.engineType != ENG_JET) Cn -= 0.008f * engineSpool * smoothstepf(45.f, 10.f, V);
    // stall wing-drop
    if (sig > 0.3f) { float dv, a, b; noised(time * 0.8f, 2.2f, dv, a, b); Cl += sig * 0.04f * dv; }
    float L = Cl * qbar * s.wingArea * s.span, M = Cm * qbar * s.wingArea * s.chord, Nn = Cn * qbar * s.wingArea * s.span;
    if (!s.special) T += vec3(M, -Nn, -L);
    else if (s.special == 2) {
      // XR-11: the airframe's own stability and damping act on it; the fly-by-wire decides the surface deflections
      float Cm0 = Cma * (alpha - aCruise) * 0.4f + Cmq * 0.35f * qh, Cl0 = -0.10f * beta - 0.55f * ph + 0.08f * rh, Cn0 = 0.09f * beta - 0.16f * rh;
      Taero = vec3(Cm0 * qbar * s.wingArea * s.chord, -Cn0 * qbar * s.wingArea * s.span, -Cl0 * qbar * s.wingArea * s.span);
      surfMax = vec3(s.elevPow * s.chord, s.rudPow * s.span, s.ailPow * s.span) * (qbar * s.wingArea * ctlEff);
      T += Taero;
    }
    stallWarn = smoothstepf(aStall - 5 * DEG, aStall - 1.5f * DEG, alpha);
  } else { alpha = 0; beta = 0; }
  if (s.special) {
    if (s.special == 1) F += vec3(0, 0, -thrust);   // XR-9: the nozzles vector in pitch only (no vertical flight)
    // fly-by-wire rate command through vectored thrust and reaction jets: authority independent of airspeed
    float Vt = std::max(V, 1.f);
    float hover = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, V);
    // pitch authority comes from the vectoring nozzles (+-29 deg of deflection, doubled in v1.7): ~110 deg/s at Mach 1
    bool wr = s.special == 2;   // XR-11: ~80 g at full stick, 400 deg/s roll
    float pMax = fbwPitchMax(Vt);
    float rMax = fbwRollMax(hover), yMax = wr ? 1.8f : 1.4f;
    vec3 wd(ctl.pitch * pMax + ctl.trim * 0.15f, -ctl.yaw * yMax, -ctl.roll * rMax);
    if (hover > 0) {  // hands-off attitude hold while hovering
      if (fabsf(ctl.pitch) < 0.05f) wd.x += hover * 2.2f * (0.f - pitchDeg()) * DEG;
      if (fabsf(ctl.roll) < 0.05f) wd.z += hover * 2.2f * bankDeg() * DEG;
    }
    // no g or angle-of-attack limiting: the stick commands rotation directly and the pilot owns the airframe's limits
    float ms0 = m / s.emptyMass, k = onGround ? 4.f : 9.f, kp = onGround ? 4.f : 18.f;   // stiffer pitch loop: snap reversals
    vec3 Ii(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
    if (wr) wraithThrust(F, T, thrust * 0.25f, wd, Taero, surfMax, dt);
    else T += vec3(Ii.x * kp * (wd.x - w.x), Ii.y * k * (wd.y - w.y), Ii.z * k * (wd.z - w.z));
  } else F += vec3(0, 0, -thrust);

  // ---------------- ground contacts
  float L = s.fusLen, R = s.fusRad;
  float gh = gearHeight();
  float track = std::max(1.2f, s.span * 0.13f);
  std::vector<Contact> cs;
  bool wheels = gear > 0.95f;
  if (wheels) {
    if (s.taildragger) {
      cs.push_back({vec3(-track, -gh, -0.10f * L), 0}); cs.push_back({vec3(track, -gh, -0.10f * L), 1});
      cs.push_back({vec3(0, -gh + 0.11f * L, 0.45f * L), 2});  // tail wheel: 11.3 deg ground attitude
    } else {
      cs.push_back({vec3(-track, -gh, 0.04f * L), 0}); cs.push_back({vec3(track, -gh, 0.04f * L), 1});
      cs.push_back({vec3(0, -gh, -0.36f * L), 2});
    }
  }
  cs.push_back({vec3(0, -R, -0.45f * L), 3});           // nose / prop
  cs.push_back({s.taildragger ? vec3(0, -gh + 0.11f * L + 0.25f, 0.45f * L) : vec3(0, -R * 0.6f, 0.48f * L), 4});  // tail
  cs.push_back({vec3(-s.span * 0.5f, s.wingY * R, s.wingZ), 5});
  cs.push_back({vec3(s.span * 0.5f, s.wingY * R, s.wingZ), 5});
  cs.push_back({vec3(0, -R, 0), 6});                    // belly

  bool anyWheel = false;
  float roughSum = 0;
  vec3 Fw(0, 0, 0), Tw(0, 0, 0);  // world force, body torque
  float kSpring = s.emptyMass * 2.2f * G0 / 0.10f, cDamp = 2.f * 0.8f * sqrtf(kSpring * s.emptyMass * 0.6f);
  vec3 fwdW = forward();
  for (const Contact& c : cs) {
    vec3 pw = pos + q.rotate(c.p);
    float gy = g_world.height(pw.x, pw.z, 7);
    bool water = gy < 0.3f && g_world.onRunway(pw.x, pw.z, 30) < 0;
    float surf = std::max(gy, 0.f);
    float pen = surf - pw.y;
    if (pen <= 0) continue;
    vec3 vc = vel + q.rotate(cross(w, c.p));
    float spd = length(vel);
    if (water && spd > 1.f) { ev.crashed = true; ev.crashReason = "Ditched in the sea"; return; }
    if (c.kind >= 3) {
      if (c.kind == 4 && wheels && spd > 5) { ev.tailStrike = true; }
      else if (spd > 2.5f) {
        ev.crashed = true;
        ev.crashReason = c.kind == 3 ? (wheels ? "Prop/nose strike" : "Belly landing - gear was up") : c.kind == 5 ? "Wingtip struck the ground" : c.kind == 6 ? "Belly landing - gear was up" : "Struck terrain";
        if (!onGround && altAgl > 3) ev.crashReason = "Flew into terrain";
        return;
      }
    }
    if (c.kind <= 2 && -vc.y > 4.6f) { ev.crashed = true; ev.crashReason = fmt("Gear collapsed - hit at %.0f fpm", -vc.y * 196.85f); return; }
    // spring/damper normal force (world up)
    float nF = std::max(0.f, kSpring * pen - cDamp * vc.y);
    if (c.kind >= 3) nF *= 2.f;
    vec3 f(0, nF, 0);
    if (c.kind <= 2) {
      anyWheel = true;
      int rw = g_world.onRunway(pw.x, pw.z, 2);
      float rough = rw >= 0 ? (surfaceRough(g_world.airports[rw].surface) ? 0.6f : 0.f) : 1.f;
      roughSum += rough;
      if (rw < 0) {
        float dA; int ai = g_world.nearestAirport(pw.x, pw.z, &dA);
        bool grounds = ai >= 0 && dA < g_world.airports[ai].length * 0.5f + 300.f && fabsf(gy - g_world.airports[ai].elev) < 3.f;
        vec3 n = g_world.normal(pw.x, pw.z);
        if (!grounds && spd > 8.f && (!s.roughOK || n.y < 0.97f)) { ev.crashed = true; ev.crashReason = "Landed off-airport on rough terrain"; return; }
        if (n.y < 0.9f && spd > 3.f) { ev.crashed = true; ev.crashReason = "Overturned on a steep slope"; return; }
      }
      // wheel heading (steerable nose/tail wheel)
      vec3 wf = vec3(fwdW.x, 0, fwdW.z);
      if (c.kind == 2) {
        float steer = -ctl.yaw * 0.45f * smoothstepf(30.f, 4.f, spd);
        if (s.taildragger) steer = -steer;
        wf = quat::axisAngle(vec3(0, 1, 0), steer).rotate(wf);
      }
      wf = normalize(wf);
      vec3 wr = cross(wf, vec3(0, 1, 0));
      float vlong = dot(vc, wf), vlat = dot(vc, wr);
      float mu = rw >= 0 && !surfaceRough(g_world.airports[rw].surface) ? 0.8f : 0.55f;
      float rollRes = 0.015f + 0.06f * rough;
      float brakeF = (c.kind <= 1 ? ctl.brake * 0.7f : 0.f);   // (enough for the main wheels to hold full power when parked)
      f += wr * (-nF * mu * clampf(vlat / 0.4f, -1, 1));
      f += wf * (-nF * (rollRes + brakeF) * clampf(vlong / 0.3f, -1, 1));
    } else {
      // sliding structure: heavy friction
      vec3 vh(vc.x, 0, vc.z);
      if (length(vh) > 0.01f) f += normalize(vh) * (-nF * 0.6f);
    }
    Fw += f;
    Tw += cross(c.p, q.conj().rotate(f));
  }
  wasOnGround = onGround;
  onGround = anyWheel;
  groundRough = anyWheel ? roughSum / 3.f : 0.f;
  if (anyWheel && !wasOnGround && flightTime > 2.f) {
    ev.touchdown = true; ev.touchdownVs = vel.y; ev.touchdownSpeed = length(vel);
  }

  // buildings
  for (const Box& b : g_world.boxes) {
    const Airport& a = g_world.airports[b.airport];
    float hd = a.heading * DEG, dx = pos.x - a.x, dz = pos.z - a.z;
    float u = dx * sinf(hd) - dz * cosf(hd), v = dx * cosf(hd) + dz * sinf(hd);
    // box frame: x = across (right of runway direction), z = along runway direction
    float bx = v - b.c.x, bz = u - b.c.z, by = pos.y - a.elev - b.c.y;
    float r = s.span * 0.3f;
    if (fabsf(bx) < b.h.x + r && fabsf(bz) < b.h.z + r && fabsf(by) < b.h.y + R) {
      ev.crashed = true; ev.crashReason = "Collided with a building"; return;
    }
  }
  // trees, rock formations and buildings (separate entities standing on the terrain)
  if (altAgl < 200.f) {
    int hk = g_scenery.collide(pos, s.span * 0.12f);
    for (size_t i = 0; !hk && i < cs.size(); i++) hk = g_scenery.collide(pos + q.rotate(cs[i].p), 0.3f);
    if (hk) {
      int k = hk - 1, cl = entClass(k);
      ev.crashed = true;
      ev.crashReason = cl == EC_TREE ? (k == EK_BUSH ? "Ploughed into the scrub" : "Crashed into trees") : cl == EC_ROCK ? "Hit a rock formation" : fmt("Collided with a %s", k == EK_SILO ? "silo" : k == EK_CHURCH ? "church" : k == EK_LIGHTHOUSE ? "lighthouse" : k == EK_WATERTOWER ? "water tower" : "building");
      return;
    }
  }

  // ---------------- integrate
  vec3 Fworld = q.rotate(F) + Fw + vec3(0, -m * G0, 0);
  vec3 acc = Fworld / m;
  vec3 accBody = q.conj().rotate(acc + vec3(0, G0, 0));
  gLoad = accBody.y / G0;
  if (!onGround) { maxG = std::max(maxG, gLoad); minG = std::min(minG, gLoad); }
  if (!anyWheel && (s.special == 2 ? (gLoad > 90.f || gLoad < -45.f) : s.special ? (gLoad > 50.f || gLoad < -25.f) : (gLoad > 5.8f || gLoad < -3.f))) { ev.crashed = true; ev.crashReason = "Structural failure - overstressed airframe"; return; }
  vel += acc * dt;
  pos += vel * dt;
  // inertia scales with loading
  float ms = m / s.emptyMass;
  vec3 I(s.Iyy * ms, s.Izz * ms, s.Ixx * ms);
  vec3 tot = T + Tw;
  vec3 Iw(I.x * w.x, I.y * w.y, I.z * w.z);
  vec3 wdot = tot - cross(w, Iw);
  w += vec3(wdot.x / I.x, wdot.y / I.y, wdot.z / I.z) * dt;
  if (onGround) w *= expf(-2.0f * dt);  // gear/strut damping
  float wl = length(w);
  if (wl > 1e-6f) { q = q * quat::axisAngle(w / wl, wl * dt); q.normalize(); }
  if (pos.x < -WORLD_HALF * 1.2f || pos.x > WORLD_HALF * 1.2f || pos.z < -WORLD_HALF * 1.2f || pos.z > WORLD_HALF * 1.2f) {
    ev.crashed = true; ev.crashReason = "Flew beyond the charted area and ran out of options";
  }

  // ---------------- autopilot inner loops
  if (apOn) apControl(dt);
}

// ------------------------------------------------------------------ XR-11 Wraith thruster pods
// Four pods pivot from thrust-aft (0) to thrust-down (pi/2). Each carries vanes that deflect its jet in pitch and yaw,
// and can trim its own thrust. The fly-by-wire turns the stick's rate demand into an angular acceleration, takes
// what the control surfaces can give (authority grows with dynamic pressure), then solves for the pod controls that
// produce the rest from the pods' real positions and thrust: differential thrust front/rear and left/right,
// collective pitch and roll vanes, yaw vanes and differential tilt. Whatever can't be produced simply isn't: at low
// speed and low power the controls really do go soft.
namespace {
struct PodCmd { float diff[4], dp[4], dy[4], dt[4]; };
void podForces(const PodCmd& c, float tilt, float Tp, vec3& F, vec3& Tq, float* outTilt, float* outYaw, float* outThr, float* outVane = nullptr) {
  F = vec3(0, 0, 0); Tq = vec3(0, 0, 0);
  for (int i = 0; i < 4; i++) {
    float a = tilt + c.dp[i] + c.dt[i], y = c.dy[i];
    vec3 d(sinf(y), sinf(a) * cosf(y), -cosf(a) * cosf(y));   // jet force direction (opposite the exhaust)
    float T = Tp * (1.f + c.diff[i]);
    vec3 f = d * T;
    F += f; Tq += cross(kWraithPods[i], f);
    if (outTilt) { outTilt[i] = tilt + c.dt[i]; outYaw[i] = y; outThr[i] = T; outVane[i] = c.dp[i]; }
  }
}
// virtual control k (0..5) at unit strength u -> per-pod commands
void applyVirtual(PodCmd& c, int k, float u) {
  for (int i = 0; i < 4; i++) {
    float fr = i < 2 ? 1.f : -1.f, lr = (i & 1) ? -1.f : 1.f;   // front +1 / rear -1, left +1 / right -1
    switch (k) {
      case 0: c.diff[i] += 0.40f * u * fr; break;   // thrust front vs rear (pitch in the hover)
      case 1: c.diff[i] += 0.45f * u * lr; break;   // thrust left vs right (roll in the hover, yaw when aft)
      case 2: c.dp[i] += 0.45f * u * -fr; break;    // pitch vanes, opposed front / rear (pitch when aft)
      case 3: c.dp[i] += 0.40f * u * lr; break;     // pitch vanes, opposed left / right (roll when aft)
      case 4: c.dy[i] += 0.30f * u * -fr; break;    // yaw vanes, opposed front / rear (yaw)
      case 5: c.dt[i] += 0.25f * u * lr; break;     // differential tilt (yaw in the hover)
    }
  }
}
}

void Plane::wraithThrust(vec3& F, vec3& T, float Tp, vec3 wd, vec3 Taero, vec3 surfMax, float dt) {
  const AircraftSpec& s = *spec;
  float ms0 = mass() / s.emptyMass, k = onGround ? 4.f : 9.f, kp = onGround ? 4.f : 18.f;
  vec3 I(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
  vec3 tDes(I.x * kp * (wd.x - w.x), I.y * k * (wd.y - w.y), I.z * k * (wd.z - w.z));
  vec3 need = tDes - Taero;   // cancel the airframe's own moments too
  // 1) control surfaces: elevons (pitch, roll) and ruddervators (yaw)
  // (below ~15 m/s the surfaces have nothing to work with: they stay faired instead of flailing at full throw)
  auto alloc = [](float n, float mx) { return mx > 3e4f ? clampf(n / mx, -1.f, 1.f) * smoothstepf(3e4f, 1.5e5f, mx) : 0.f; };
  vec3 u(alloc(need.x, surfMax.x), alloc(need.y, surfMax.y), alloc(need.z, surfMax.z));
  vec3 tSurf(u.x * surfMax.x, u.y * surfMax.y, u.z * surfMax.z);
  surf = vec3(u.x, -u.y, -u.z);   // as stick-style deflections: pitch up, yaw right, roll right
  // 2) thrust: base forces at the commanded tilt, then the pod controls for the remainder (bounded least squares)
  float tilt = nozzle * 0.5f * PI;
  PodCmd c0 = {};
  vec3 F0, T0;
  podForces(c0, tilt, Tp, F0, T0, nullptr, nullptr, nullptr);
  vec3 rem = need - tSurf - T0;
  float J[3][6];
  for (int kk = 0; kk < 6; kk++) {
    PodCmd c = {}; applyVirtual(c, kk, 1.f);
    vec3 Fk, Tk; podForces(c, tilt, Tp, Fk, Tk, nullptr, nullptr, nullptr);
    vec3 d = Tk - T0; J[0][kk] = d.x; J[1][kk] = d.y; J[2][kk] = d.z;
  }
  // weighted allocation (minimum sum of u^2 / wt): in the hover, differential thrust does the work and the visible
  // controls (vanes, pod yaw, differential tilt) only top it up, so a stick input doesn't twist every pod at once.
  // Yaw still needs them; differential tilt is the cleaner-looking way, the yaw vanes come last.
  float hv = smoothstepf(0.4f, 0.9f, nozzle);
  float wt[6] = {1.f, 1.f, 1.f - 0.92f * hv, 1.f - 0.92f * hv, 1.f - 0.85f * hv, 1.f - 0.5f * hv};
  float um[6] = {1.f, 1.f, 1.f - 0.7f * hv, 1.f - 0.7f * hv, 1.f - 0.6f * hv, 1.f};   // and they stay within a modest throw
  float uv[6] = {0, 0, 0, 0, 0, 0}; bool free_[6] = {true, true, true, true, true, true};
  vec3 r = rem;
  for (int pass = 0; pass < 3; pass++) {
    // u = J^T (J J^T + lambda I)^-1 r over the unsaturated controls
    float A[3][3] = {};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { for (int kk = 0; kk < 6; kk++) if (free_[kk]) A[i][j] += wt[kk] * J[i][kk] * J[j][kk]; }
    float lam = 1e3f + 0.01f * (A[0][0] + A[1][1] + A[2][2]);   // damped: weak controls aren't driven into saturation
    for (int i = 0; i < 3; i++) A[i][i] += lam;
    float det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabsf(det) < 1e-12f) break;
    float inv[3][3] = {
      {(A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det, (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det, (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det},
      {(A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det, (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det, (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det},
      {(A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det, (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det, (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det}};
    float y[3];
    for (int i = 0; i < 3; i++) y[i] = inv[i][0] * r.x + inv[i][1] * r.y + inv[i][2] * r.z;
    bool sat = false;
    for (int kk = 0; kk < 6; kk++) {
      if (!free_[kk]) continue;
      float du = wt[kk] * (J[0][kk] * y[0] + J[1][kk] * y[1] + J[2][kk] * y[2]);
      uv[kk] += du;
      if (fabsf(uv[kk]) > um[kk]) { uv[kk] = clampf(uv[kk], -um[kk], um[kk]); free_[kk] = false; sat = true; }
    }
    if (!sat) break;
    // residual left after saturation, for the controls still free
    vec3 got(0, 0, 0);
    for (int kk = 0; kk < 6; kk++) got += vec3(J[0][kk], J[1][kk], J[2][kk]) * uv[kk];
    r = rem - got;
  }
  // on the ground the wheels hold the attitude and the nosewheel steers: the pods stay squared up and only push.
  // (Otherwise the fly-by-wire fights the gear's reaction moments with full vane and differential-tilt deflection,
  // and parked pods sit visibly skewed.) The authority comes back smoothly as the craft lifts off.
  if (onGround) for (int kk = 0; kk < 6; kk++) uv[kk] = 0.f;
  PodCmd c = {};
  for (int kk = 0; kk < 6; kk++) applyVirtual(c, kk, uv[kk]);
  vec3 Fp, Tpq;
  podForces(c, tilt, Tp, Fp, Tpq, podTilt, podYaw, podThr, podVane);
  float Tfull = s.power * 2.1f;
  for (int i = 0; i < 4; i++) podThr[i] /= Tfull;
  fanAngle = fmodf(fanAngle + (6.f + 90.f * engineSpool) * dt, 2 * PI * 64.f);
  F += Fp;
  T += tSurf + Tpq;
}

// ------------------------------------------------------------------ autopilot
static float hdgErrDeg(float target, float cur) { return wrapAngle((target - cur) * DEG) / DEG; }
static float len2(vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }

// XR-11 vertical landing: nozzles down, pitch for the along-track speed, bank for the cross-track, throttle for
// height, then straight down onto the touchdown point
void Plane::apHover(float dt) {
  vec3 rr(-apLd.z, 0, apLd.x), rel = pos - apTd;
  float dist = -(rel.x * apLd.x + rel.z * apLd.z), cross = rel.x * rr.x + rel.z * rr.z;
  float vAl = vel.x * apLd.x + vel.z * apLd.z, vCr = vel.x * rr.x + vel.z * rr.z;
  float hab = pos.y - gearHeight() - apTd.y;
  ctl.flaps = 1.f; ctl.gearDown = true; ctl.brake = 0;
  float hov = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, length(vel));
  // along track: close on the point at a comfortable deceleration
  float vT = (dist > 0 ? 1.f : -1.f) * std::min(sqrtf(2.f * 1.3f * fabsf(dist)), fabsf(dist) * 0.35f);
  vT = clampf(vT, -10.f, 90.f);
  float ax = clampf((vT - vAl) * 0.5f, -4.f, 3.f);
  float pitchT = clampf(-atanf(ax / G0) / DEG, -12.f, 18.f) + (1.f - hov) * 4.f;
  // across: bank towards the centreline
  float ay = clampf(-cross * 0.12f - vCr * 0.6f, -3.f, 3.f);
  float bankT = clampf(atanf(ay / G0) / DEG, -12.f, 12.f);
  float pMax = fbwPitchMax(length(vel)), rMax = fbwRollMax(hov);
  float q = (clampf((pitchT - pitchDeg()) * 1.5f, -20.f, 20.f)) * DEG;
  // near-neutral stick engages the jet's own hover attitude hold (towards level): cancel it so ours is the only loop
  float cp = (q - ctl.trim * 0.15f) / pMax;
  if (fabsf(cp) < 0.05f) cp = clampf(cp + hov * 2.2f * pitchDeg() * DEG / pMax, -0.0499f, 0.0499f);
  ctl.pitch = clampf(cp, -1.f, 1.f);
  float cr = clampf((bankT - bankDeg()) * 1.5f, -20.f, 20.f) * DEG / rMax;
  if (fabsf(cr) < 0.05f) cr = clampf(cr + hov * 2.2f * bankDeg() * DEG / rMax, -0.0499f, 0.0499f);
  ctl.roll = clampf(cr, -1.f, 1.f);
  ctl.yaw = clampf(hdgErrDeg(atan2f(apLd.x, -apLd.z) / DEG, heading()) * 0.08f, -0.5f, 0.5f);
  // height: hold a gentle descending profile in, then straight down once over the point and slow
  bool over = fabsf(dist) < 25.f && fabsf(vAl) < 3.f && fabsf(cross) < 10.f;
  float altT = apTd.y + gearHeight() + clampf(dist * 0.05f, 12.f, 90.f);
  float vsT = over ? (hab > 4.f ? -1.6f : -0.6f) : clampf((altT - pos.y) * 0.4f, -4.f, 4.f);
  float e = vsT - vel.y;
  apThrI = clampf(apThrI + e * 0.06f * dt, 0.f, 1.f);
  ctl.throttle = clampf(apThrI + e * 0.1f, 0.f, 1.f);
}


void Plane::apEngage(int mode, int airport, const Weather& wx) {
  apOn = mode != AP_OFF; apMode = mode; apDone = false;
  float spd0 = ias > 1.f ? ias : length(vel);
  apHeading = heading(); apAlt = pos.y; apSpeed = std::max(spd0, spec->vref * 1.3f);
  apPitchI = 0; apRollI = 0; apThrI = ctl.throttle; apXI = 0; apUseVS = false;
  apAirport = airport; apStage = APS_NAV; apStageT = 0; apLeg = 0; apTurnDir = 0; apClimbDir = 0;
  if (mode >= AP_NAV && airport >= 0) {
    // runway end: the better of the two plans (terrain on the approach, headwind, and how far away it is)
    bool rev = apPlan(airport, true, wx, false) > apPlan(airport, false, wx, false);
    apPlan(airport, rev, wx, true);
    const Airport& a = g_world.airports[airport];
    // cruise: clear the highest ground on the way by 350 m, and at least 700 m above the field
    float hi = a.elev;
    vec3 d = apHoldC - pos;
    for (int i = 0; i <= 40; i++) { vec3 p = pos + d * (i / 40.f); hi = std::max(hi, g_world.height(p.x, p.z)); }
    apCruiseAlt = std::max(std::max(hi + 350.f, apHoldAlt), std::min(pos.y, a.elev + 2500.f));
    apMode = AP_APPR;
  }
}

// plan an approach to one runway end: the final approach length that clears the terrain, a descent orbit over the
// lowest ground near the approach, and the altitude to intercept the final from. Returns a score (higher is better).
float Plane::apPlan(int airport, bool rev, const Weather& wx, bool commit) {
  const AircraftSpec& s = *spec;
  const Airport& a = g_world.airports[airport];
  auto H = [](vec3 q) { return g_world.height(q.x, q.z); };
  vec3 ld = rev ? a.dir() * -1.f : a.dir(), rr(-ld.z, 0, ld.x);
  vec3 td = a.threshold(rev) + ld * clampf(a.length * 0.12f, 80.f, 300.f); td.y = a.elev;
  // glidepath: 3 degrees, steepened (up to what the type can fly) to clear the ground and trees under the final
  float F0 = clampf(s.vref * 100.f, 4500.f, 8000.f), F = F0;
  bool jetT = s.engineType == ENG_JET && !s.special;
  float maxAng = jetT ? 3.5f : s.special ? 4.f : s.runwayM < 300.f ? 6.5f : 5.5f;
  auto margin = [](float d) { return clampf(d * 0.02f, 8.f, 60.f); };
  float need = tanf(3.f * DEG);
  for (float d = 300.f; d <= 3500.f; d += 50.f) { vec3 q = td - ld * d; need = std::max(need, (H(q) + margin(d) - a.elev) / d); }
  float gs = std::min(need, tanf(maxAng * DEG));
  for (float d = 300.f; d <= F0; d += 100.f) {
    vec3 q = td - ld * d;
    if (a.elev + d * gs < H(q) + margin(d)) { F = d - 600.f; break; }
  }
  float blocked = F < 2000.f ? 3000.f : 0.f;
  F = std::max(F, 2000.f);
  float gh = gearHeight();
  float iafAlt = a.elev + gh + F * gs + 20.f;
  // orbit radius: a comfortable turn at holding speed
  float vh = std::max(s.vref * 1.45f, std::min(s.cruise * 0.6f, s.vref * 1.8f));
  float R = clampf(vh * vh / (G0 * tanf((s.special ? 40.f : 24.f) * DEG)) * 1.15f, 900.f, 3500.f);
  // intercept region: the extended centreline from the gate out to 4 km beyond it
  float intMsa = 0;
  for (float d = F; d <= F + 6000.f; d += 250.f) for (int k = -2; k <= 2; k++) intMsa = std::max(intMsa, H(td - ld * d + rr * (k * 500.f)));
  float intAlt = std::max(iafAlt, intMsa + 250.f);
  vec3 bestC = td - ld * (F + R + 2000.f); float bestCost = 1e9f, bestAlt = intAlt;
  for (float al = -3000.f; al <= F + R + vh * 15.f + 9000.f; al += 750.f)
    for (float cr = -7000.f; cr <= 7000.f; cr += 750.f) {
      if (fabsf(cr) < R * 0.5f && al < F + R + 1000.f) continue;   // keep the orbit off the final approach itself
      float near = al - R < F + 500.f + vh * 15.f ? 800.f : 0.f;     // too close in: an outbound leg first
      vec3 c = td - ld * al + rr * cr;
      if (fabsf(c.x) > WORLD_HALF - R - 1500.f || fabsf(c.z) > WORLD_HALF - R - 1500.f) continue;   // stay on the chart
      float m = H(c);
      for (int k = 0; k < 16; k++) { float an = k * PI / 8; vec3 q = c + vec3(cosf(an), 0, sinf(an)) * (R + 600.f); m = std::max(m, H(q)); }
      for (int k = 0; k < 8; k++) { float an = k * PI / 4; vec3 q = c + vec3(cosf(an), 0, sinf(an)) * (R * 0.6f); m = std::max(m, H(q)); }
      vec3 qi = td - ld * (F + 1500.f);
      if (len2(c - qi) < R + 1200.f) continue;   // leave the orbit with room to line up
      for (int i = 1; i < 8; i++) { vec3 q = c + (qi - c) * (i / 8.f); m = std::max(m, H(q)); }
      float hAlt = std::max(intAlt, m + 280.f);
      // ...and how far out of the way it is from where the aircraft is now (every km flown is fuel)
      float cost = (hAlt - iafAlt) * 3.f + std::max(0.f, hAlt - iafAlt - 150.f) * 10.f + len2(c - qi) * 0.15f + near
                 + std::max(0.f, len2(c - pos) + len2(c - qi) - len2(qi - pos)) * 0.12f;
      if (cost < bestCost) { bestCost = cost; bestC = c; bestAlt = hAlt; }
    }
  vec3 from(sinf(wx.windFrom * DEG), 0, -cosf(wx.windFrom * DEG));
  float score = dot(ld, from) * wx.windSpeed * 40.f - bestCost - (F0 - F) * 0.3f - len2(bestC - pos) * 0.02f - blocked
              - (atanf(gs) / DEG - 3.f) * 150.f;
  if (commit) {
    apRev = rev; apFinalLen = F; apGs = gs; apHoldC = bestC; apHoldC.y = 0; apHoldR = R; apHoldAlt = bestAlt; apIntAlt = intAlt;
    apLd = ld; apTd = td;
  }
  return score;
}

void Plane::apGuidance(float dt) {
  const AircraftSpec& s = *spec;
  apStageT += dt;
  bool jet = s.engineType == ENG_JET;
  if (apMode != AP_APPR || apAirport < 0) {
    apUseVS = false;
    apStatus = fmt("HOLD  HDG %03.0f  ALT %.0f ft  SPD %.0f kt", wrapDeg360(apHeading), apAlt * M_TO_FT, apSpeed * MS_TO_KT);
    return;
  }
  const Airport& a = g_world.airports[apAirport];
  vec3 ld = apLd, td = apTd;
  vec3 rr(-ld.z, 0, ld.x), rel = pos - td;
  float along = rel.x * ld.x + rel.z * ld.z, cross = rel.x * rr.x + rel.z * rr.z, dist = -along;
  float rwyHdg = atan2f(ld.x, -ld.z) / DEG;
  float gh = gearHeight(), hab = pos.y - gh - a.elev;
  const float gs = apGs;
  float F = apFinalLen;
  float vref = s.vref;
  int rwyN = a.rwyNumber(apRev);
  float vnow = std::max(length(vel), 30.f);
  // wind drift (ground track minus heading), smoothed: the centreline legs steer the track, not the heading
  if (len2(vel) > 10.f) apDrift = approach(apDrift, clampf(hdgErrDeg(atan2f(vel.x, -vel.z) / DEG, heading()), -30.f, 30.f), 0.6f, dt);
  switch (apStage) {
    case APS_NAV: {
      vec3 C = apHoldC; C.y = pos.y;
      float dc = len2(C - pos);
      float vh = std::max(vref * 1.45f, std::min(s.cruise * 0.6f, vref * 1.8f));
      apUseVS = false;
      ctl.flaps = 0.f;
      ctl.gearDown = !s.retract;
      if (apLeg == 0) {
        // en route to the descent orbit, descending along a gentle profile but never below the ground on the way
        apHeading = atan2f(C.x - pos.x, -(C.z - pos.z)) / DEG;
        // a big turn back (after a go-around, say): turn the way with the lower ground under the arc
        float he = hdgErrDeg(apHeading, heading());
        if (fabsf(he) < 60.f) apTurnDir = 0;
        else if (apTurnDir == 0 && fabsf(he) > 90.f) {
          float vt = std::max(length(vel), 30.f), Rt = vt * vt / (G0 * tanf(25.f * DEG)), hh = heading() * DEG, best = 1e9f;
          for (int d = -1; d <= 1; d += 2) {
            vec3 f(sinf(hh), 0, -cosf(hh)), r(cosf(hh) * d, 0, sinf(hh) * d), c = pos + r * Rt, m(0, 0, 0); float hm = 0;
            for (int k = 0; k <= 8; k++) { float an = k * PI / 8; vec3 q = c - r * (Rt * cosf(an)) + f * (Rt * sinf(an)); hm = std::max(hm, g_world.height(q.x, q.z)); }
            if (hm + (d * he < 0 ? 150.f : 0.f) < best) { best = hm + (d * he < 0 ? 150.f : 0.f); apTurnDir = d; }
          }
        }
        if (apTurnDir != 0 && fabsf(he) >= 60.f) {
          float turn = apTurnDir > 0 ? wrapDeg360(apHeading - heading()) : wrapDeg360(heading() - apHeading);
          apHeading = heading() + apTurnDir * std::min(turn, 60.f);
        }
        float msa = 0;
        vec3 d = C - pos; float n = std::max(1.f, dc / 250.f);
        vec3 sd = dc > 1.f ? vec3(-d.z, 0, d.x) * (1.f / dc) : vec3(1, 0, 0);
        for (float i = 0; i <= n; i++) for (int k = -1; k <= 1; k++) { vec3 q = pos + d * (i / n) + sd * (k * 600.f); msa = std::max(msa, g_world.height(q.x, q.z)); }
        apAlt = std::max(clampf(apHoldAlt + (dc - apHoldR) * 0.06f, apHoldAlt, std::max(apCruiseAlt, apHoldAlt)), msa + 330.f);
        apSpeed = dc < 2500.f + s.cruise * 20.f ? vh : s.cruise * 0.85f;   // slow down in time to turn tightly (not so early that slow, high-power flight eats the reserve)
        // climb planning: can this aircraft out-climb the ground ahead on the way? Compare the height needed over
        // the next 12 km of track with what it can reach at a conservative climb gradient. If it can't, climb in a
        // circle (turning towards the lower side) until it can, then carry on.
        {
          vec3 f = d * (1.f / std::max(dc, 1.f));
          float vsCap = (s.special ? 30.f : jet ? 12.f : s.engineType == ENG_TURBOPROP ? 6.f : 4.f) * 0.55f;
          float grad = vsCap / std::max(length(vel), 30.f), short_ = -1e9f, needTop = 0;
          for (int i = 1; i <= 24; i++) {
            float dd = i * 500.f; if (dd > dc + 500.f) break;
            vec3 q = pos + f * dd;
            float need = g_world.height(q.x, q.z) + 250.f;
            needTop = std::max(needTop, need);
            short_ = std::max(short_, need - (pos.y + dd * grad));
          }
          if (apClimbDir == 0 && short_ > 0.f) {
            // circle the side with the lower ground
            float vt = std::max(length(vel), 30.f), Rt = vt * vt / (G0 * tanf(25.f * DEG)), hh = heading() * DEG, best = 1e9f;
            for (int sd = -1; sd <= 1; sd += 2) {
              vec3 r(cosf(hh) * sd, 0, sinf(hh) * sd), c = pos + r * Rt; float hm = 0;
              for (int k = 0; k < 12; k++) { float an = k * PI / 6; vec3 q = c + vec3(cosf(an), 0, sinf(an)) * (Rt + 300.f); hm = std::max(hm, g_world.height(q.x, q.z)); }
              if (hm < best) { best = hm; apClimbDir = sd; }
            }
          }
          if (apClimbDir != 0) {
            if (short_ < -80.f) apClimbDir = 0;   // clear (with some margin): resume the track
            else {
              apHeading = heading() + apClimbDir * 60.f;
              apAlt = std::max(apAlt, needTop);
              apSpeed = std::max(vh, s.vref * 1.35f);
              apStatus = fmt("NAV  %s  climbing to %.0f ft before the high ground", a.code, needTop * M_TO_FT);
            }
          }
        }
        if (fabsf(he) > 60.f) apSpeed = std::min(apSpeed, std::max(vh, s.cruise * 0.4f));   // and for big turns
        if (dc < apHoldR + 300.f) {
          apLeg = 1; apStageT = 0;
          vec3 r = pos - C;   // orbit the way we are already turning
          apHoldDir = (r.x * vel.z - r.z * vel.x) > 0 ? -1 : 1;
          // already down at the orbit's height: no need to circle (a full orbit costs minutes of fuel), go for the final
          if (pos.y < apHoldAlt + 60.f) apLeg = along > -(F + 500.f + vnow * 15.f) ? 3 : 2;
        }
        if (apClimbDir == 0) apStatus = fmt("NAV  %s  RWY %02d  %.1f km", a.code, rwyN, (dc + F) / 1000.f);
      } else {
        // the intercept: the extended centreline is flown with the same steering as the localizer, from far enough
        // out to settle before the gate. Too close in (or on the wrong side), first fly outbound, diverging a little.
        float outMin = F + 500.f + vnow * 15.f;
        float Rt = vnow * vnow / (G0 * tanf((s.special ? 30.f : 20.f) * DEG));
        float Rturn = std::max(vnow / (3.f * DEG), vnow * vnow / (G0 * tanf((s.special ? 45.f : 26.f) * DEG)));   // NAV turns
        float L1 = vnow * 14.f;
        float side = cross >= 0 ? 1.f : -1.f, D = side * 2.4f * Rturn;
        // outbound: follow a line parallel to the centreline, a turn's width off it, so the turn back rolls out on it
        auto intercept = [&](int leg) {
          return (leg == 3 ? rwyHdg + 180.f + clampf(atanf((cross - D) / L1) / DEG * 1.2f, -45.f, 45.f)
                           : rwyHdg - clampf(atanf(cross / L1) / DEG * 1.2f, -55.f, 55.f)) - apDrift;
        };
        if (apLeg == 1) {
          // descent orbit: circle the low ground until down at the intercept altitude, then leave towards the final
          float th = atan2f(pos.x - C.x, -(pos.z - C.z)) / DEG;
          apHeading = th + apHoldDir * (90.f + clampf((dc - apHoldR) / apHoldR * 80.f, -60.f, 60.f));
          apAlt = apHoldAlt;
          apSpeed = vh;
          int next = along > -outMin ? 3 : 2;
          if (pos.y < apHoldAlt + 50.f && apStageT > 5.f &&
              (fabsf(hdgErrDeg(intercept(next), heading())) < 30.f || apStageT > 2.f * PI * apHoldR / vh + 20.f)) { apLeg = next; apStageT = 0; }
          apStatus = fmt("HOLD  %s  %s %.0f ft", a.code, pos.y > apHoldAlt + 50.f ? "descending to" : "leaving at", apHoldAlt * M_TO_FT);
        } else {
          if (apLeg == 2 && along > -(outMin - 400.f) && fabsf(cross) > 300.f) apLeg = 3;
          if (apLeg == 3 && along < -(outMin + 800.f) && fabsf(cross - D) < 400.f) apLeg = 2;
          apHeading = intercept(apLeg);
          apAlt = std::max(apIntAlt, std::min(apHoldAlt, pos.y));
          apSpeed = vh;
          float hd = hdgErrDeg(heading(), rwyHdg), th = fabsf(hd);
          float lead = Rt * (1.f - cosf(std::min(th, 90.f) * DEG)) + 150.f;
          bool closing = cross * hd < 0.f || fabsf(cross) < 150.f;
          if (apLeg == 2 && along < -1500.f && fabsf(cross) < lead && closing && th < 70.f) { apStage = APS_FINAL; apStageT = 0; apXI = 0; }
          apStatus = fmt("NAV  %s  RWY %02d  %s  %.1f km", a.code, rwyN, apLeg == 3 ? "outbound" : "intercept", dist / 1000.f);
        }
      }
      break;
    }
    case APS_FINAL: {
      // localizer: steer for a point a fixed time ahead on the centreline (gentle at any speed), plus a wind trim
      float L1 = std::max(length(vel), 30.f) * 14.f;
      if (fabsf(cross) < 200.f) apXI = clampf(apXI + cross * dt * 0.002f, -5.f, 5.f);
      apHeading = rwyHdg - clampf(atanf(cross / L1) / DEG * 1.2f + apXI, -40.f, 40.f) - apDrift;
      float gsAlt = a.elev + gh + std::max(dist, 0.f) * gs + 1.f;
      float vg = std::max(vel.x * ld.x + vel.z * ld.z, 15.f);
      float err = gsAlt - pos.y;
      apUseVS = true;
      apVS = err > 25.f ? 0.3f : clampf(-vg * gs + err * 0.15f, jet ? -10.f : -6.f, 3.f);
      // outside the gate the glideslope can run below the safe intercept altitude: hold that until the gate
      if (dist > F && pos.y < apIntAlt + 30.f) apVS = std::max(apVS, clampf((apIntAlt - pos.y) * 0.1f, -1.f, 3.f));
      bool high = err < -40.f && dist < F + 1000.f;   // above the glideslope: configure early for the drag
      if (high) ctl.flaps = 1.f;
      else if (dist > F * 0.55f) ctl.flaps = dist > F ? 0.34f : 0.67f;
      else {   // landing flap: what leaves a nose-up attitude (~4.5 deg angle of attack) for a main-wheels-first touchdown
        float clReq = mass() * G0 / (0.5f * 1.225f * apSpeed * apSpeed * s.wingArea);
        float fl = s.flapCL > 0.01f ? (clReq - s.CL0 - s.CLa * 4.5f * DEG) / s.flapCL : 0.f;
        ctl.flaps = approach(ctl.flaps, clampf(fl, 0.34f, 1.f), 0.3f, dt);
      }
      if (dist < F + 1500.f || high) ctl.gearDown = true;
      apSpeed = dist > F ? vref * 1.3f : dist > F * 0.5f ? vref * 1.18f : vref * 1.06f;
      if (dist < 2000.f && dist > 250.f && (fabsf(cross) > std::min(80.f, std::max(a.width * 0.5f, 12.f) + dist * 0.03f) || err > 40.f || err < -80.f)) { apStage = APS_GOAROUND; apStageT = 0; }
      { vec3 ahead = pos + vec3(ld.x, 0, ld.z) * 800.f; if (dist > 1200.f && pos.y < g_world.height(ahead.x, ahead.z) + 40.f) { apStage = APS_GOAROUND; apStageT = 0; } }
      // the XR-11 comes to a hover over the touchdown point instead of a fast landing roll
      if (s.special == 2 && dist < 1700.f && dist > 0.f && fabsf(cross) < 60.f) { apStage = APS_HOVER; apStageT = 0; apThrI = ctl.throttle; }
      float flareH = clampf(std::max(ias * 0.13f, -vel.y * 2.2f), 4.f, 14.f);
      if (hab < flareH && dist < 1500.f) { apStage = APS_FLARE; apStageT = 0; }
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      apStatus = fmt("APPR  %s  RWY %02d  %.1f km  GS %+.0f m", a.code, rwyN, dist / 1000.f, -err);
      break;
    }
    case APS_FLARE:
      apHeading = rwyHdg - clampf(cross * 0.05f, -6.f, 6.f) - apDrift * 0.5f;   // half the crab out before touchdown
      apUseVS = true; apVS = -clampf(hab * 0.22f + 0.25f, 0.3f, 2.5f);
      apSpeed = 0;
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      apStatus = fmt("FLARE  %s  RWY %02d", a.code, rwyN);
      break;
    case APS_ROLLOUT:
      apSpeed = 0;
      apStatus = fmt("ROLLOUT  %s  %.0f kt", a.code, length(vel) * MS_TO_KT);
      if (length(vel) < 2.5f) { apDisengage(); apDone = true; ctl.brake = 1; apStatus = "AUTOLAND COMPLETE"; }
      break;
    case APS_HOVER: {
      float vAl = vel.x * ld.x + vel.z * ld.z;
      apStatus = fmt("VTOL  %s  RWY %02d  %.0f m  %.0f kt", a.code, rwyN, std::max(dist, 0.f), fabsf(vAl) * MS_TO_KT);
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      break;
    }
    case APS_GOAROUND:
      apHeading = rwyHdg; apUseVS = true; apVS = jet ? 9.f : 4.f; apSpeed = vref * 1.35f;
      ctl.flaps = 0.34f;
      if (apStageT > 8.f && s.retract) ctl.gearDown = false;
      if (pos.y > std::max(a.elev + 450.f, apHoldAlt - 30.f)) { apStage = APS_NAV; apLeg = 0; apStageT = 0; apTurnDir = 0; }
      apStatus = fmt("GO AROUND  %s", a.code);
      break;
  }
  // terrain safety while en route and in the go-around: never let the target sit below the ground ahead
  if (apStage == APS_NAV || apStage == APS_GOAROUND) {
    vec3 f = len2(vel) > 5.f ? vel * (1.f / len2(vel)) : forward();
    float hi = 0;
    for (int i = 1; i <= 6; i++) { vec3 p = pos + vec3(f.x, 0, f.z) * (i * 600.f); hi = std::max(hi, g_world.height(p.x, p.z)); }
    if (!apUseVS) apAlt = std::max(apAlt, hi + 250.f);
    else if (pos.y < hi + 200.f) apVS = std::max(apVS, 5.f);
  }
}

void Plane::apControl(float dt) {
  const AircraftSpec& s = *spec;
  bool fbw = s.special != 0;
  float V = std::max(ias, 15.f);
  float vn = clampf(V / s.cruise, 0.3f, 2.5f);
  // ground rollout after an autoland: centreline with rudder / nosewheel, nose down, brakes, idle
  if (apMode == AP_APPR && apStage == APS_ROLLOUT) {
    vec3 rr(-apLd.z, 0, apLd.x), rel = pos - apTd;
    float cross = rel.x * rr.x + rel.z * rr.z, he = hdgErrDeg(atan2f(apLd.x, -apLd.z) / DEG, heading());
    ctl.throttle = 0;
    ctl.pitch = s.taildragger ? 0.35f : (apStageT > 1.2f ? -0.1f : 0.f);
    ctl.roll = clampf(-bankDeg() * 0.05f, -0.4f, 0.4f);
    he = hdgErrDeg(atan2f(apLd.x, -apLd.z) / DEG - clampf(cross * 0.6f, -10.f, 10.f), heading());
    ctl.yaw = clampf(he * 0.08f + w.y / DEG * 0.12f, -1.f, 1.f);   // steer for the centreline, damp the yaw rate
    ctl.brake = clampf((apStageT - 0.6f) * 1.2f, 0.f, s.taildragger ? 0.55f : 1.f);
    return;
  }
  if (apMode == AP_APPR && apStage == APS_HOVER) { apHover(dt); return; }
  if (onGround) return;
  // lateral: heading -> bank -> roll rate (gains scale with airspeed: control power grows with dynamic pressure)
  float herr = hdgErrDeg(apHeading, heading());
  float bankMax = apMode == AP_APPR && apStage >= APS_FINAL ? (fbw ? 30.f : 20.f) : (fbw ? 45.f : 26.f);
  float turnT = clampf(herr * 0.2f, -3.f, 3.f) * DEG;   // heading error -> turn rate (deg/s), rate-one turn at most
  float bankT = clampf(atanf(turnT * V / G0) / DEG, -bankMax, bankMax);
  if (apMode == AP_APPR && apStage == APS_FLARE) bankT = clampf(bankT, -4.f, 4.f);
  float bank = bankDeg(), pRate = -w.z / DEG;
  float pT = clampf((bankT - bank) * 0.8f, -10.f, 10.f);
  if (fbw) ctl.roll = clampf(pT * DEG / fbwRollMax(0.f), -1.f, 1.f);
  else {
    apRollI = clampf(apRollI + (pT - pRate) * 0.006f * dt / vn, -0.3f, 0.3f);
    ctl.roll = clampf(apRollI + (pT - pRate) * 0.03f / vn, -0.6f, 0.6f);
  }
  if (fbw) ctl.flaps = 0;   // the research jets have no flaps (the XR-11's lever tilts its pods): keep it up
  // vertical: altitude -> vertical speed -> flight path -> pitch rate
  float vsUp = fbw ? 30.f : s.engineType == ENG_JET ? 12.f : s.engineType == ENG_TURBOPROP ? 6.f : 4.f;
  float vsT = apUseVS ? apVS : clampf((apAlt - pos.y) * 0.08f, -vsUp * 1.2f, vsUp);
  if (!(apMode == AP_APPR && apStage == APS_FLARE)) {
    // energy first: only climb as hard as the speed allows, and below 1.1 Vref put the nose down to recover speed
    if (apSpeed > 0 && vsT > 0 && ctl.throttle > 0.95f) vsT *= clampf((ias - s.vref * 1.2f) / std::max(apSpeed - s.vref * 1.2f, 3.f), 0.f, 1.f);
    float floorV = apSpeed > 0 ? std::min(s.vref * 1.1f, std::max(apSpeed * 0.93f, s.vref * 0.97f)) : s.vref * 0.97f;
    if (ias < floorV) vsT = std::min(vsT, (ias - floorV) * 1.5f);
  }
  float spd = std::max(length(vel), 1.f);
  float gT = asinf(clampf(vsT / spd, -0.5f, 0.5f)) / DEG, g = asinf(clampf(vel.y / spd, -1.f, 1.f)) / DEG;
  float turn = G0 * tanf(clampf(bank, -60.f, 60.f) * DEG) * sinf(clampf(bank, -60.f, 60.f) * DEG) / spd / DEG;   // pull needed to hold height in a turn
  float qT = clampf((gT - g) * 0.7f, -4.f, 4.f) + turn;
  float qRate = w.x / DEG;
  if (fbw) {
    float pMax = fbwPitchMax(V);
    ctl.pitch = clampf((qT * DEG - ctl.trim * 0.15f) / pMax, -1.f, 1.f);
  } else {
    apPitchI = clampf(apPitchI + (qT - qRate) * 0.012f * dt / (vn * vn) + (gT - g) * 0.004f * dt / (vn * vn), -0.6f, 0.6f);
    ctl.pitch = clampf(apPitchI + (qT - qRate) * 0.05f / (vn * vn), -0.8f, 0.8f);
  }
  // the research jet's rate-command yaw channel holds zero yaw rate: feed it the coordinated turn rate
  ctl.yaw = fbw ? clampf(G0 * sinf(clampf(bank, -80.f, 80.f) * DEG) / spd / 1.4f, -1.f, 1.f) : 0.f;
  // autothrottle
  if (apSpeed > 0) {
    float e = apSpeed - ias;
    float maxThr = fbw && !(apMode == AP_APPR && apStage == APS_GOAROUND) ? 0.84f : 1.f;   // no reheat on autopilot
    apThrI = clampf(apThrI + e * 0.02f * dt, 0.f, maxThr);
    ctl.throttle = clampf(apThrI + e * 0.08f + (vsT - vel.y) * 0.015f, 0.f, maxThr);
  } else if (apMode == AP_APPR) ctl.throttle = std::max(0.f, ctl.throttle - dt * 0.6f);   // flare: idle
  // (apSpeed 0 outside an approach: the pilot keeps the throttle)
}
