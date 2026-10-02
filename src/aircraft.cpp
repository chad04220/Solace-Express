// Air Xpress - aircraft roster and 6-DOF flight model
#include "aircraft.h"
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
   0.35f, 5.0f, 1.85f, 0.95f, 0.034f, 0.006f, 0.070f, 0.74f, 220000, 18, 18, 24, 55, 60, 220, true, true, false,
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
  // hidden research model: thrust-to-weight ~2.2, supersonic, VTOL thrust vectoring (see Plane::substep special path)
  {"xr9", "XR-9 Specter", "Confidential research model", ENG_JET, 2, 0, 0, 0, 0, 9000, 3000, 0, 0, 46.0f, 11.2f, 4.6f,
   0.05f, 3.6f, 1.70f, 0.0f, 0.013f, 0.010f, 0.0f, 0.75f, 118000, 0, 60, 70, 420, 4000, 250, true, false, true,
   25000, 90000, 110000, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 0, 0,
   17.2f, 1.0f, -0.2f, 1.6f, 2, 1, vec3(0.11f, 0.12f, 0.14f), vec3(0.2f, 0.85f, 1.0f), 1},
};
// clang-format on
const int kNumAircraft = sizeof(kAircraft) / sizeof(kAircraft[0]) - 1;  // the research jet is not part of the career

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
  apOn = false; apPitchI = 0; gust = vec3(); rng = Rng(77);
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
    if (s.special) {
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
  if (s.special) nozzle = flaps;   // the research jet's F/V keys swivel the thrust-vector nozzles instead of flaps
  if (s.retract) gear = clampf(gear + (ctl.gearDown ? 1.f : -1.f) * dt / 5.f, 0, 1);
  else gear = 1;

  // ---------------- aerodynamics
  vec3 F(0, 0, 0), T(0, 0, 0);  // body-frame force and torque
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
    stallWarn = smoothstepf(aStall - 5 * DEG, aStall - 1.5f * DEG, alpha);
  } else { alpha = 0; beta = 0; }
  if (s.special) {
    // thrust-vectoring nozzles swivel from aft (0) to straight down (1) for vertical flight
    float a = nozzle * 0.5f * PI;
    F += vec3(0, sinf(a), -cosf(a)) * thrust;
    // fly-by-wire rate command through vectored thrust and reaction jets: authority independent of airspeed
    float Vt = std::max(V, 1.f);
    float hover = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, V);
    float pMax = clampf(33.f * G0 / Vt, 0.9f, 2.6f);           // inertially damped research cell: ~50 deg/s at Mach 1
    float rMax = 5.5f * (1.f - 0.6f * hover), yMax = 1.4f;
    vec3 wd(ctl.pitch * pMax + ctl.trim * 0.15f, -ctl.yaw * yMax, -ctl.roll * rMax);
    if (hover > 0) {  // hands-off attitude hold while hovering
      if (fabsf(ctl.pitch) < 0.05f) wd.x += hover * 2.2f * (0.f - pitchDeg()) * DEG;
      if (fabsf(ctl.roll) < 0.05f) wd.z += hover * 2.2f * bankDeg() * DEG;
    }
    // g limiter: keep the angle of attack inside +30 / -12 g (or the stall)
    if (V > 20.f && !onGround) {
      float qS = 0.5f * density * V * V * s.wingArea, W = m * G0;
      float aHi = std::min((30.f * W / qS - s.CL0) / s.CLa, (s.CLmax - s.CL0) / s.CLa);
      float aLo = std::max((-12.f * W / qS - s.CL0) / s.CLa, (-1.1f - s.CL0) / s.CLa);
      wd.x = clampf(wd.x, (aLo - alpha) * 6.f - 0.2f, (aHi - alpha) * 6.f + 0.2f);
    }
    float ms0 = m / s.emptyMass, k = onGround ? 4.f : 9.f;
    vec3 Ii(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
    T += vec3(Ii.x * k * (wd.x - w.x), Ii.y * k * (wd.y - w.y), Ii.z * k * (wd.z - w.z));
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
        { float bb[4]; g_world.sampleBase(pw.x, pw.z, bb); float gg = g_world.groundHeight(pw.x, pw.z, 7); int ck = 0;
          if (g_world.cover(pw.x, pw.z, gg, bb, &ck) > 0.5f) ev.crashReason = ck <= COV_PALM ? "Crashed into trees" : "Hit a rock formation"; }
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
      float brakeF = (c.kind <= 1 ? ctl.brake * 0.55f : 0.f);
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
  if (altAgl < 90.f && g_world.hitsBuilding(pos, s.span * 0.3f)) { ev.crashed = true; ev.crashReason = "Collided with a building"; return; }

  // ---------------- integrate
  vec3 Fworld = q.rotate(F) + Fw + vec3(0, -m * G0, 0);
  vec3 acc = Fworld / m;
  vec3 accBody = q.conj().rotate(acc + vec3(0, G0, 0));
  gLoad = accBody.y / G0;
  if (!onGround) { maxG = std::max(maxG, gLoad); minG = std::min(minG, gLoad); }
  if (!anyWheel && (s.special ? (gLoad > 40.f || gLoad < -20.f) : (gLoad > 5.8f || gLoad < -3.f))) { ev.crashed = true; ev.crashReason = "Structural failure - overstressed airframe"; return; }
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

  // ---------------- autopilot (heading + altitude hold)
  if (apOn && !onGround) {
    float herr = wrapAngle((apHeading - heading()) * DEG) / DEG;
    float bankT = clampf(herr * 1.2f, -25.f, 25.f);
    float bank = bankDeg();
    ctl.roll = clampf((bankT - bank) * 0.04f - (-w.z) * 0.6f, -0.6f, 0.6f);
    float vsT = clampf((apAlt - pos.y) * 0.05f, -4.f, 4.f);
    apPitchI = clampf(apPitchI + (vsT - vel.y) * 0.12f * dt, -8.f, 12.f);
    float pitchT = clampf(apPitchI + 0.8f * (vsT - vel.y), -10.f, 15.f);
    ctl.pitch = clampf(0.06f * (pitchT - pitchDeg()) - 1.2f * w.x, -0.8f, 0.8f);
    ctl.yaw = 0;
  }
}
