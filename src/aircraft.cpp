// Solace Express - aircraft roster and 6-DOF flight model
#include "aircraft.h"
#include "aero.h"
#include "entities.h"
#include "scenery.h"

// clang-format off
const AircraftSpec kAircraft[] = {
  // (CLa, CD0, gearCD and e are the published figures, kept for reference: the flight model derives its own lift slope,
  // drag and span efficiency from the geometry, aero.cpp)
  // id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
  // power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
  // fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
  {"kestrel", "Kestrel T2", "Two-seat trainer", ENG_PISTON, 1, 4, 2, 750, 2600, 530, 70, 120, 1, 14.9f, 10.1f, 1.5f,
   0.30f, 4.8f, 1.45f, 0.55f, 0.030f, 0.004f, 0.045f, 0.75f, 110000, 22, 25, 30, 50, 45, 400, false, false, false,
   900, 1300, 1900, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 18000, 120,
   7.3f, 0.62f, 1.19f, -0.92f, 0, 0, vec3(0.92f, 0.92f, 0.95f), vec3(0.85f, 0.12f, 0.10f)},
  {"wren", "Wren 180", "Four-seat tourer", ENG_PISTON, 1, 6, 2, 700, 2700, 820, 110, 320, 3, 16.2f, 11.0f, 1.5f,
   0.30f, 4.9f, 1.50f, 0.65f, 0.028f, 0.004f, 0.050f, 0.76f, 180000, 23, 28, 33, 60, 70, 450, false, false, false,
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
  // ---- Codex's aircraft (docs/design/additional-aircraft, proposals on codex/*): Swift S6, Osprey C6, XR-10 Nightjar
  // Swift S6: four-seat retractable touring piston
  {"swift_s6", "Swift S6", "Retractable low-wing tourer", ENG_PISTON, 1, 6, 3, 700.0f, 2700.0f,
  980.0f, 180.0f, 420.0f, 3, 17.10f, 11.40f, 1.552f,
  0.28f, 4.9f, 1.65f, 0.78f, 0.025f, 0.010f, 0.052f, 0.80f,
  260000.0f, 23.0f, 31.0f, 36.0f, 76.0f, 140.0f, 600.0f, false, false, true,
  7600.0f, 6500.0f, 14100.0f, 0.42f, 0.065f, 0.068f, LIC_PPL, 68000, 600,
  8.60f, 0.66f, -1.121212f, -0.155f, 0, 0,
  vec3(0.91f, 0.88f, 0.80f), vec3(0.13f, 0.26f, 0.32f), 0},
  // Osprey C6: six-seat coastal charter piston twin (its cabin trim: plane_sdf.glsl mapOspreyCabinTrim, by kOsprey)
  {"osprey_c6", "Osprey C6", "Six-seat coastal charter twin", ENG_PISTON, 2, 6, 3, 700.0f, 2700.0f, 1420.0f, 220.0f, 270.0f, 5, 22.5f, 12.4f, 1.82f,
  0.30f, 4.9f, 1.65f, 0.80f, 0.028f, 0.006f, 0.060f, 0.78f, 180000.0f, 24.0f, 30.2f, 35.0f, 74.0f, 110.0f, 550.0f, false, false, true,
  9000.0f, 10800.0f, 19800.0f, 0.41f, 0.056f, 0.062f, LIC_CPL, 68000, 650,
  9.8f, 0.90f, -0.68f, -0.80f, 1, 0, vec3(0.91f, 0.88f, 0.79f), vec3(0.82f, 0.24f, 0.06f), 0},
  // ---- the research craft (kNumAircraft stops here)
  // XR-10 Nightjar: conventional twin-jet research demonstrator (special 0: the generic flight model and field)
  {"xr10_nightjar", "XR-10 Nightjar", "Tapered-wing research demonstrator", ENG_JET, 2, 0, 0, 0.0f, 0.0f,
  5700.0f, 1300.0f, 100.0f, 0, 38.18f, 16.60f, 2.508f,
  0.15f, 4.6f, 1.55f, 0.55f, 0.020f, 0.010f, 0.045f, 0.80f,
  30000.0f, 0.0f, 43.0f, 50.0f, 260.0f, 240.0f, 1250.0f, false, false, true,
  52000.0f, 80000.0f, 130000.0f, 0.33f, 0.085f, 0.065f, LIC_ATP, 340000, 0,
  16.30f, 0.80f, -0.9375f, -1.145f, 2, 0,
  vec3(0.14f, 0.18f, 0.20f), vec3(0.20f, 0.78f, 0.70f), 0, 1.30f, 9.f, -4.f},
  // hidden research model: thrust-to-weight ~2.2, supersonic, pitch thrust vectoring (see Plane::substep special path).
  // It lands fast and long on its small unflapped wing: 1,400 m of hard runway (250 m and rough fields once said here,
  // and the autoland took it to fields it overran or broke its gear on - the review of v3.33.0, A1)
  {"xr30_specter", "XR-30 Specter", "Confidential research model", ENG_JET, 2, 0, 0, 0, 0, 9000, 3000, 0, 0, 46.0f, 11.2f, 4.6f,
   0.05f, 3.6f, 1.70f, 0.0f, 0.013f, 0.010f, 0.0f, 0.75f, 132000, 0, 60, 70, 420, 4000, 1400, false, false, true,
   25000, 90000, 110000, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 0, 0,
   17.2f, 1.0f, -0.2f, 1.6f, 2, 1, vec3(0.11f, 0.12f, 0.14f), vec3(0.2f, 0.85f, 1.0f), 1, 2.7f, 40.f, -20.f},
  // XR-20 Mantis: forward-swept single-jet systems demonstrator (special 0: conventional flight model and controls).
  // One centerline 88 kN-rated engine preserves the prior pair's combined thrust (176 kN static at full reheat).
  // Its foreplanes are the model's horizontal tail placed forward; the two fins remain independently ruddered.
  {"xr20_mantis", "XR-20 Mantis", "Forward-swept systems demonstrator", ENG_JET, 1, 0, 0, 0.0f, 0.0f,
   6100.0f, 1800.0f, 0.0f, 0, 30.0f, 13.2f, 2.27f,
   0.25f, 5.0f, 1.55f, 0.60f, 0.026f, 0.012f, 0.070f, 0.78f,
   88000.0f, 0.0f, 55.0f, 62.0f, 330.0f, 750.0f, 1350.0f, false, false, true,
   30000.0f, 70000.0f, 100000.0f, 0.45f, 0.070f, 0.070f, LIC_ATP, 480000, 0,
   16.0f, 1.05f, -0.62f, 0.65f, 2, 0, vec3(0.105f, 0.125f, 0.15f), vec3(0.92f, 0.43f, 0.08f), 0, 1.9f, 14.f, -6.f},
  // hidden stealth aerobatic research model: four tilting thruster pods (power = one pod's dry thrust), T/W ~2.2 dry
  // and ~4.6 boosted, structure good for +90 / -45 g (see Plane::wraithThrust)
  {"xr40_wraith", "XR-40 Wraith", "Stealth aerobatic research model", ENG_JET, 4, 0, 0, 0, 0, 10500, 3000, 0, 0, 52.0f, 12.4f, 5.2f,
   0.03f, 3.4f, 1.60f, 0.0f, 0.012f, 0.010f, 0.0f, 0.72f, 72000, 0, 60, 70, 480, 5000, 200, true, false, true,
   30000, 80000, 100000, 0.90f, 0.090f, 0.070f, LIC_STUDENT, 0, 0,
   16.5f, 1.0f, -0.15f, 0.8f, 2, 1, vec3(0.075f, 0.08f, 0.09f), vec3(0.72f, 0.3f, 1.0f), 2, 4.3f, 90.f, -45.f},
};
// clang-format on
const int kNumAircraft = sizeof(kAircraft) / sizeof(kAircraft[0]) - 4;  // the research craft (XR-10, XR-30, XR-20, XR-40) are not part of the career

// How fast the engines follow the throttle (spool fraction per second): a piston at once, a turbine slowly, the research
// jets' fast-spooling cores in between
float Plane::spoolRate() const {
  const AircraftSpec& s = *spec;
  return s.engineType == ENG_PISTON ? 3.0f : (s.engineType == ENG_TURBOPROP ? 0.7f : (s.special ? 1.6f : 0.45f));
}

// The engines' thrust (N, all of them) at a spool fraction, true airspeed V and forward airspeed vf (m/s), in the air
// the aircraft is in now (density): the physics step's and the autopilot's question alike
float Plane::thrustAt(float spool, float V, float vf, float rho) const {
  const AircraftSpec& s = *spec;
  const float dens = rho > 0.f ? rho : density, sigmaRho = dens / 1.225f;   // (rho: another air's density, for comparisons)
  if (s.special == 2) {
    // four thruster pods: dry up to 85% throttle, boost above; ram compression adds thrust with Mach
    float ab = smoothstepf(0.85f, 1.0f, spool);
    return s.engines * s.power * powf(sigmaRho, 0.5f) * (0.85f * spool + 1.25f * ab) * (1.f + 0.3f * std::min(V / 330.f, 4.f));
  } else if (s.special) {
    // two afterburning turbofans: dry up to 85% throttle, reheat above (2x thrust at 100%, T/W ~4.4)
    float ab = smoothstepf(0.85f, 1.0f, spool);
    return s.engines * s.power * powf(sigmaRho, 0.6f) * (0.82f * spool + 1.18f * ab);   // full reheat doubles thrust
  } else if (s.engineType == ENG_JET && s.designMach > 1.f) {
    // a supersonic research type on conventional controls: dry up to 85% throttle, reheat above, and ram
    // compression keeps the thrust up with Mach
    float ab = smoothstepf(0.85f, 1.0f, spool);
    return s.engines * s.power * powf(sigmaRho, 0.6f) * (0.82f * spool + 1.18f * ab) * (1.f + 0.15f * std::min(V / soundSpeed, 2.5f));
  } else if (s.engineType == ENG_JET) {
    float mach = V / soundSpeed;
    return s.engines * s.power * spool * powf(sigmaRho, 0.75f) * (1.f - 0.3f * mach);
  }
  float P = s.engines * s.power * spool * (s.engineType == ENG_PISTON ? sigmaRho : powf(sigmaRho, 0.75f));
  float thrust = P * 0.8f / sqrtf(vf * vf + s.v0 * s.v0);
  // a propeller can't make more thrust than momentum theory allows for its disk: T = FM (2 rho A P^2)^(1/3) per
  // engine, figure of merit 0.75, diameter from the power it absorbs (1.75 m for a 110 kW trainer, 2.7 m for a
  // Caravan) - it's what limits the static thrust, at speed the power-over-speed term is the smaller
  float Pe = P / s.engines, D = 0.55f * powf(s.power / 1000.f, 0.25f), A = PI * D * D * 0.25f;
  return std::min(thrust, s.engines * 0.75f * cbrtf(2.f * dens * A * Pe * Pe));
}

// The thrust it can turn straight down to hold itself up (N): the XR-40's pods tilt to the vertical; nothing else can
float Plane::liftThrustMax() const { return spec->special == 2 ? thrustAt(1.f, 0.f, 0.f) : 0.f; }

float Plane::fuelFlowMax() const {
  float rangeS = spec->rangeKm * 1000.f / spec->cruise;
  return spec->maxFuel / (rangeS * 0.8f);
}
float Plane::rangeLeftKm() const { return fuel / (fuelFlowMax() * 0.8f) * spec->cruise / 1000.f; }
float Plane::glideRatio() const {   // clean best L/D from the aircraft's own drag build-up (gear as it is)
  if (!spec) return 8.f;
  if (spec->special == 0) {   // (the strip model's, engines stopped: aero_strips.cpp; ice adds its drag at the best glide's lift)
    const AeroGeom& g = aeroGeom(*spec);
    const float ld = g.glideLD[0] + (g.glideLD[1] - g.glideLD[0]) * gear;
    return clampf(1.f / (1.f / ld + 0.035f * fail.ice), 3.f, 30.f);
  }
  const AeroModel& a = aeroModel(*spec);
  float cd0g = cd0 + a.gearDq / spec->wingArea * gear + 0.025f * fail.ice;
  return clampf(0.5f * sqrtf(PI * a.e * a.AR / std::max(cd0g, 0.01f)), 4.f, 25.f);
}
bool Plane::failNow(int kind, int engine) {
  if (!spec || spec->special) return false;   // the research craft are maintained by the programme
  const AircraftSpec& s = *spec;
  engine = std::clamp(engine, 0, std::max(0, std::min(s.engines, 4) - 1));
  switch (kind) {
    case FAIL_ENGINE_PARTIAL: fail.engineHealth[engine] = std::min(fail.engineHealth[engine], 0.45f); return true;
    case FAIL_ENGINE_TOTAL: fail.engineHealth[engine] = 0.f; if (fail.enginesOut(s.engines) >= s.engines) { engineRunning = false; starterTime = 0; } return true;
    case FAIL_ALTERNATOR: fail.alternator = true; return true;
    case FAIL_PITOT: fail.pitot = true; fail.pitotIas = ias; fail.pitotRho = density; return true;
    case FAIL_GEAR_STUCK: if (!s.retract) return false; fail.gearStuck = gear < 0.5f ? 1 : 2; gear = gear < 0.5f ? 0.f : 1.f; return true;
    case FAIL_FLAP_ASYM: if (s.special || s.flapCL <= 0.01f) return false; fail.flapAsym = true; fail.flapAt = flaps; return true;
    case FAIL_ICING: fail.ice = std::max(fail.ice, 0.3f); return true;
    default: return false;
  }
}

void Plane::reset(const AircraftSpec* s, vec3 position, float headingDeg, float fuelKg, float payloadKg, bool airborne, float speed) {
  spec = s; pos = position; fuel = fuelKg; payload = payloadKg;
  for (auto& wheel : wheelMotion) wheel.reset();
  q = quat::axisAngle(vec3(0, 1, 0), -headingDeg * DEG);
  w = vec3(); ctl = Controls(); ev = FlightEvents(); apComfort = false; sceneryHits = true; brakeHold = 0;
  flaps = 0; gear = 1; rpm = 0; n1 = 0; engineSpool = 0; maxG = minG = 1; flightTime = 0;
  fail = Failures(); iceFeed = 0; overG = 0;
  apDisengage(); apDone = false; apOverrun = false; apPitchI = 0; gust = vec3(); rng = Rng(77);
  wxl = wxfield::Local(); wxAir = seamShift = vec3(); windVel = windAvg = gustRot = vec3(); gustBurst = 0; aeroMem = AeroMem(); ctlSurf = vec3();
  // drag comes from the airframe's shape (aero.cpp), evaluated every step at the speed and air density of the moment
  cd0 = aeroCD0(aeroModel(*s), *s, std::max(speed, 30.f), 1.225f, speed / 340.f);
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

// how far the wheels hang below the model's origin (m)
static float gearHeightOf(const AircraftSpec& s) {
  if (s.taildragger) return s.fusRad * 1.0f + 0.45f;
  return s.fusRad * 1.3f + (s.engineType == ENG_JET || s.engines == 2 ? 0.75f : 0.55f);
}

GearStations gearStations(const AircraftSpec& s) {
  const float L = s.fusLen;
  GearStations g;
  g.track = std::max(1.2f, s.span * 0.13f);
  g.noseZ = -0.36f * L;
  g.tailZ = 0.45f * L; g.tailY = 0.11f * L;   // (the tail wheel: 11.3 deg ground attitude)
  if (s.special == 0) {
    // the mains where a designer puts them for the centre of gravity (aero_strips.cpp sets it a static margin ahead of
    // the neutral point): a tricycle's behind it by a tip-back angle of 15 degrees, and far enough that the nose wheel
    // carries no more than a tenth of the wheelbase's share; a taildragger's ahead of it by 12 degrees standing level
    // (23 in its three-point attitude: braking hard on the mains doesn't stand it on its nose), its tail wheel set for
    // the same 11.3 degree ground attitude
    const vec3 cg = aeroGeom(s).cg;
    const float h = gearHeightOf(s) + cg.y;
    if (s.taildragger) {
      g.mainZ = cg.z - h * tanf(12.f * DEG);
      g.tailY = (g.tailZ - g.mainZ) * tanf(11.3f * DEG);
    } else g.mainZ = cg.z + std::max(h * tanf(15.f * DEG), 0.1f * (cg.z - g.noseZ));
  } else {
    // (the XR-30's and XR-40's mains under the middle of their delta wings, a tenth of the length behind the centre of
    // gravity: at 4% they stood near the wings' leading edges)
    g.mainZ = 0.10f * L;
  }
  return g;
}

float Plane::gearHeight() const { return gearHeightOf(*spec); }

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
  // the approach speed a pilot adds for gusts (half the gust factor) - into a headwind; with the wind behind, none (it
  // only lengthens the landing: QA F1's overruns touched down at 77 m/s ground speed on a 62 m/s reference)
  apGustAdd = 0.5f * wx.gust + 2.f * wx.turbulence;
  if (apMode == AP_APPR) apGustAdd *= clampf(1.f + dot(apLd, vec3(sinf(wx.windFrom * DEG), 0, -cosf(wx.windFrom * DEG))) * wx.windSpeed / 4.f, 0.f, 1.f);
  wxl = wxfield::local(wx, pos, agl());   // (the wind's slow parts here: they change over hundreds of metres)
  if (apOn) { apSense(); apGuidance(dt); }   // (what it can do now, then what to do about it)
  // a wind that has turned behind it since the approach was planned: down the final, while there is room to climb
  // away, the stop is checked again by the plan's own rule. Too long now, it goes around, and clear of the ground the
  // approach is planned afresh in the wind there is - the other end, or neither (then it holds, and says why). It
  // never turns for the other end low down, and lands from inside 800 m whatever the wind does (the review of
  // v3.34.0, F4: the Starling, admitted at Cedar Ridge in 5 m/s, landed with the wind swung to 8 m/s behind it and
  // stopped 24 m past the end)
  if (apOn && apMode == AP_APPR && apAirport >= 0) {
    const Airport& a = g_world.airports[apAirport];
    if (apStage == APS_FINAL && apEnv.canGoAround && !apEnv.hover) {
      vec3 rel = pos - apTd; float dist = -(rel.x * apLd.x + rel.z * apLd.z);
      float tw = 0.f;
      if (dist > 800.f && apStopNeed(a, apRev, wx, &tw) > a.length && tw > 0.f) {
        apStage = APS_GOAROUND; apStageT = 0; apWindEvent = 1;
        if (getenv("APDBG")) printf("  go-around at %.0f m: %.1f m/s behind, too long to stop on %.0f m\n", dist, tw, a.length);
      }
    }
    if (apWindEvent == 1 && apStage == APS_NAV) { apEngage(AP_APPR, apAirport, wx); apWindEvent = 2; }
  }
  // a declined autoland is tried again every 20 s while it circles: the wind may have eased (or turned to favour an end)
  if (apOn && apMode == AP_HOLD && apHoldFor >= 0 && (apRetryT -= dt) <= 0.f) {
    apRetryT = 20.f;
    apPlan(apHoldFor, true, wx, false); bool okR = apPlanWhy.empty();
    apPlan(apHoldFor, false, wx, false); bool okF = apPlanWhy.empty();
    if (okR || okF) { apEngage(AP_APPR, apHoldFor, wx); if (apDecline.empty()) apWindEvent = 2; }
  }
  for (int i = 0; i < N && !ev.crashed; i++) substep(h, wx, time + h * i);
  windAvg = flightTime <= 0.f ? windVel : lerp(windAvg, windVel, 1.f - expf(-dt / 1.5f));
  flightTime += dt;
  // the map wraps (world.h): over its seam it comes back in from the other side, and the air it flies in with it (the
  // eddies and gusts go on as they were); the autopilot's plan stays where the field is
  const vec3 seam(wrapCoord(pos.x) - pos.x, 0.f, wrapCoord(pos.z) - pos.z);
  if (seam.x != 0.f || seam.z != 0.f) { pos += seam; wxAir += seam; seamShift += seam; }
}

struct Contact { vec3 p; int kind; };  // kind 0 main L, 1 main R, 2 nose/tail wheel, 3+ structure

void Plane::substep(float dt, const Weather& wx, float time) {
  const AircraftSpec& s = *spec;
  float m = mass();
  float altAgl = agl();
  const Atmosphere atmo = isa(pos.y);   // (the standard atmosphere: aero.cpp)
  density = atmo.rho; soundSpeed = atmo.a;
  float sigmaRho = density / 1.225f;

  // ---------------- wind and turbulence (weather.cpp): the mean wind's profile and veer, the gust bursts, the eddies
  // the air carries (the vertical ones fading in the last wingspan above the ground), the terrain's lift and sink,
  // thermals and storm drafts; and the eddies' gradient across the airframe, which the damping terms below see as
  // a roll, pitch or yaw (a gust under the right wing is the air the right wing meets when it rolls down)
  wxAir += wxfield::driftWind(wx) * dt;
  const wxfield::Sample ws = wxfield::wind(wx, wxl, pos, altAgl, time, wxAir, s.span);
  windVel = ws.v; gust = ws.v - wxfield::meanWind(wx, altAgl); gustBurst = ws.burst;
  {
    const vec3 R = right(), Fw = forward(), Up = up();
    const vec3 dR(dot(ws.gx, R), dot(ws.gy, R), dot(ws.gz, R)), dF(dot(ws.gx, Fw), dot(ws.gy, Fw), dot(ws.gz, Fw));   // the air's change across the span and along the fuselage
    gustRot = vec3(dot(dR, Up), -dot(dF, Up), -dot(dF, R));
  }

  // ---------------- engine
  bool hasFuel = fuel > 0;
  if (!engineRunning && starterTime > 0) {
    starterTime += dt;
    if (starterTime > (s.engineType == ENG_PISTON ? 1.6f : 3.0f) && hasFuel) engineRunning = true;
  }
  if (!hasFuel) engineRunning = false;
  // failures: the engines' health scales the power they make (a stopped engine makes none and, on a single, the
  // engine is simply off; on a twin the dead engine's side yaws the aircraft). The battery runs down without the
  // alternator; ice builds on the airframe in cloud below freezing and melts off in warm air.
  float healthSum = 0; for (int i = 0; i < s.engines && i < 4; i++) healthSum += fail.engineHealth[i];
  float health = s.engines > 0 ? healthSum / s.engines : 1.f;
  if (fail.enginesOut(s.engines) >= s.engines && s.engines > 0) { engineRunning = false; starterTime = 0; }
  if (fail.alternator) fail.battery = std::max(0.f, fail.battery - dt / 420.f);   // seven minutes of battery
  fail.ice = clampf(fail.ice + (iceFeed > 0 ? dt / 240.f : iceFeed < 0 ? -dt / 90.f : 0.f), 0.f, 1.f);
  float target = engineRunning ? ctl.throttle * health : 0.f;
  engineSpool = approach(engineSpool, target, spoolRate(), dt);
  vec3 vaW = vel - windVel;
  vec3 va = q.conj().rotate(vaW);
  float V = length(va);
  airspeed = V; ias = calibratedAirspeed(V, atmo);   // (what the airspeed indicator reads: the pitot's impact pressure)
  if (fail.pitot) ias = fail.pitotIas * sqrtf(fail.pitotRho / std::max(density, 0.1f));   // a blocked pitot: the reading climbs with altitude and falls with descent, never with speed
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
    thrust = thrustAt(engineSpool, V, std::max(0.f, -va.z));
    // (the engines still running burn it: a twin on one engine burns half - the review of v3.44.0, FLT-7)
    const float live = s.engines > 0 ? float(s.engines - fail.enginesOut(s.engines)) / (float)s.engines : 1.f;
    if (!s.special) fuel = std::max(0.f, fuel - fuelFlowMax() * (0.2f + 0.8f * ctl.throttle) * live * dt);
  }

  // ---------------- configuration
  // (a stopped left flap stays where it was, and the right one keeps following the lever: flapLeft)
  flaps = approach(flaps, ctl.flaps * (s.special ? 1.f : aeroGeom(s).flapMax), s.special ? 0.5f : 0.6f, dt);   // (as far as they trim: aero_strips.cpp)
  nozzle = s.special == 2 ? flaps : 0.f;   // the XR-40's F/V keys tilt its thruster pods instead of flaps
  if (s.retract && fail.gearStuck == 0) gear = clampf(gear + (ctl.gearDown ? 1.f : -1.f) * dt / 5.f, 0, 1);
  else if (!s.retract) gear = 1;

  // ---------------- aerodynamics
  vec3 F(0, 0, 0), T(0, 0, 0);  // body-frame force, and torque about the centre of gravity
  vec3 Taero(0, 0, 0), surfMax(0, 0, 0);   // XR-40: passive aerodynamic torque and full-deflection surface authority
  const AeroModel& aero = aeroModel(s);
  const float AR = aero.AR;
  const vec3 cg = s.special == 0 ? aeroGeom(s).cg : vec3();   // (body, from the model's origin)
  stallWarn = 0; wake = AeroWake();
  if (V > 0.5f) {
    alpha = atan2f(-va.y, -va.z);
    beta = asinf(clampf(va.x / V, -1, 1));
    mach = V / atmo.a;
  } else { alpha = 0; beta = 0; }
  if (s.special == 0) {
    // the strip model (aero_strips.cpp): the drawn airframe's surfaces strip by strip, each in its own air - the
    // aircraft's motion and rotation, the gusts across it, the slipstreams - with the fuselage, the engines' thrust
    // along their lines and the propellers' torque, P-factor and gyroscopic moments; summed about the centre of gravity
    const AeroGeom& ag = aeroGeom(s);
    AeroIn in;
    in.va = va; in.w = w;
    {   // the eddies' gradient across the airframe, in the body's axes (the air's velocity change per metre)
      const vec3 B[3] = {right(), up(), forward() * -1.f};
      vec3 col[3];
      for (int j = 0; j < 3; j++) {
        vec3 jw(dot(ws.gx, B[j]), dot(ws.gy, B[j]), dot(ws.gz, B[j]));
        col[j] = vec3(dot(jw, B[0]), dot(jw, B[1]), dot(jw, B[2]));
      }
      in.gx = vec3(col[0].x, col[1].x, col[2].x); in.gy = vec3(col[0].y, col[1].y, col[2].y); in.gz = vec3(col[0].z, col[1].z, col[2].z);
    }
    in.rho = density; in.a = atmo.a; in.mu = atmo.mu; in.agl = altAgl;
    // (a sideslip-to-aileron interconnect where the wing needs one: aero_strips.cpp)
    const float sas = clampf(-ag.betaToAil * beta, -0.3f, 0.3f);
    in.pitch = ctl.pitch; in.roll = ctl.roll + sas; in.yaw = ctl.yaw; in.trim = ctl.trim;
    ctlSurf = vec3(clampf(ctl.pitch + ctl.trim * 0.3f, -1.f, 1.f), clampf(in.roll + ag.ailRig, -1.f, 1.f), clampf(ctl.yaw + ag.rudRig, -1.f, 1.f));
    in.gear = gear; in.ice = fail.ice; in.flapL = flapLeft(); in.flapR = flaps;
    // each engine's share of the thrust and of the shaft power by its health (a dead one makes none, and windmills)
    float hs = 0; for (int e = 0; e < ag.nEng; e++) hs += fail.engineHealth[std::min(e, 3)];
    const float shaft = engineRunning && s.engineType != ENG_JET ? s.engines * s.power * engineSpool * (s.engineType == ENG_PISTON ? sigmaRho : powf(sigmaRho, 0.75f)) : 0.f;
    for (int e = 0; e < ag.nEng; e++) {
      const float h = fail.engineHealth[std::min(e, 3)], share = hs > 1e-3f ? h / hs : 0.f;
      in.thrust[e] = thrust * share; in.power[e] = shaft * share;
      in.dead[e] = !engineRunning || h < 0.05f;
      in.omega[e] = in.dead[e] ? 0.f : rpm * (2.f * PI / 60.f);
    }
    AeroOut ao;
    aeroForces(ag, s, in, aeroMem, dt, ao);
    aeroWakeBuild(ag, in, ao, wake);
    F += ao.F; T += ao.M;
    stallWarn = ao.stall;
  } else if (V > 0.5f) {
    // the research craft on fly-by-wire (the XR-30, XR-40): their coefficients, and the flight control system below
    float qbar = 0.5f * density * V * V;
    // lift: the wing's slope from its aspect ratio and the fuselage, steepening with Mach (Prandtl-Glauert)
    float CLa = aeroCLa(aero, std::min(mach, 0.9f));
    float cl0 = s.CL0 + s.flapCL * flaps;
    float iceCL = 1.f - 0.3f * fail.ice;   // iced: the wing stalls earlier and at a lower CLmax
    float aStall = (s.CLmax * iceCL + s.flapCL * flaps * 0.95f - cl0) / CLa;
    float aNeg = (-1.1f - cl0) / CLa;
    float sig = std::max(smoothstepf(aStall, aStall + 5 * DEG, alpha), smoothstepf(-aNeg, -aNeg + 5 * DEG, -alpha));
    float CL = (1 - sig) * clampf(cl0 + CLa * alpha, -1.2f, s.CLmax * iceCL + s.flapCL * flaps) + sig * 1.05f * sinf(2 * alpha);
    float ge = 1.f;
    if (altAgl < s.span) ge = 1.f + 0.12f * (1.f - altAgl / s.span);
    CL *= ge;
    // drag: the parts' skin friction at this Reynolds number and their form drag (aero.cpp), the gear and flaps, the
    // drag of making lift (the heavier the aircraft, the more), the stalled wing, and the fuselage pushed sideways
    // through the air in a sideslip or broadside at high angle of attack (crossflow)
    cd0 = aeroCD0(aero, s, V, density, mach);
    float sb = sinf(beta), sa = sinf(alpha) * smoothstepf(8.f * DEG, 20.f * DEG, fabsf(alpha));
    float crossCD = 0.72f * (sb * sb * aero.sideArea + sa * sa * aero.planArea) / s.wingArea;
    float CD = cd0 + aero.gearDq / s.wingArea * gear + s.flapCD * flaps + CL * CL / (PI * aero.e * AR) / ge + sig * (0.35f + 1.1f * sinf(alpha) * sinf(alpha)) + crossCD;
    CD += 0.025f * fail.ice;   // the ice's roughness and shape
    if (s.special || s.designMach > 1.f) {   // the research jets: a transonic drag rise, easing supersonic, and a wall near the design Mach
      CD += 0.022f * smoothstepf(0.86f, 1.04f, mach) - 0.007f * smoothstepf(1.2f, 2.2f, mach);
      if (s.designMach > 0.f) { float over = mach - s.designMach; if (over > 0.f) CD += 0.5f * over * over; }
    } else if (s.designMach > 0.f) {   // the barely-subsonic type: its section is clean to the design Mach, then the wall is steep
      float x = mach - (s.designMach - 0.04f);
      CD += x > 0.f ? 30.f * x * x * x : 0.f;
    } else CD += aeroWave(aero, mach, CL);                                                                     // the wing section's drag rise
    float CY = -0.7f * beta;
    vec3 liftDir = normalize(cross(vec3(1, 0, 0), va));
    vec3 dragDir = va * (-1.f / V);
    F += liftDir * (CL * qbar * s.wingArea) + dragDir * (CD * qbar * s.wingArea) + vec3(CY * qbar * s.wingArea, 0, 0);
    aeroWakeElliptic(CL * qbar * s.wingArea, density, V, s.span, s.wingY, s.wingZ, s.engines, thrust, 0.45f * s.fusLen, wake);

    float Vh = std::max(V, 12.f);
    float p = -w.z, qq = w.x, r = -w.y;
    float ph = (p - gustRot.x) * s.span / (2 * Vh), qh = (qq - gustRot.y) * s.chord / (2 * Vh), rh = (r - gustRot.z) * s.span / (2 * Vh);   // (relative to the air's own rotation)
    // trim reference so the aircraft flies hands-off near cruise
    float clCruise = (s.emptyMass + s.maxFuel * 0.6f + s.cargoKg * 0.5f) * G0 / (0.5f * 1.225f * s.cruise * s.cruise * s.wingArea);
    float aCruise = (clCruise - s.CL0) / aeroCLa(aero, s.cruise / 340.f);
    float Cma = -1.1f, Cmq = -16.f;
    float ctlEff = 1.f - 0.4f * sig;
    if (s.special == 2) {
      // XR-40: the airframe's own stability and damping act on it; the fly-by-wire decides the surface deflections
      float Cm0 = Cma * (alpha - aCruise) * 0.4f + Cmq * 0.35f * qh, Cl0 = -0.10f * beta - 0.55f * ph + 0.08f * rh, Cn0 = 0.09f * beta - 0.16f * rh;
      Taero = vec3(Cm0 * qbar * s.wingArea * s.chord, -Cn0 * qbar * s.wingArea * s.span, -Cl0 * qbar * s.wingArea * s.span);
      surfMax = vec3(s.elevPow * s.chord, s.rudPow * s.span, s.ailPow * s.span) * (qbar * s.wingArea * ctlEff);
      T += Taero;
    }
    stallWarn = smoothstepf(aStall - 5 * DEG, aStall - 1.5f * DEG, alpha);
  }
  if (s.special) {
    if (s.special == 1) F += vec3(0, 0, -thrust);   // XR-30: the nozzles vector in pitch only (no vertical flight)
    // fly-by-wire rate command through vectored thrust and reaction jets: authority independent of airspeed
    float Vt = std::max(V, 1.f);
    float hover = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, V);
    // pitch authority comes from the vectoring nozzles (+-29 deg of deflection, doubled in v1.7): ~110 deg/s at Mach 1
    bool wr = s.special == 2;   // XR-40: ~80 g at full stick, 400 deg/s roll
    float pMax = fbwPitchMax(Vt);
    float rMax = fbwRollMax(hover), yMax = wr ? 1.8f : 1.4f;
    vec3 wd(ctl.pitch * pMax + ctl.trim * 0.15f, -ctl.yaw * yMax, -ctl.roll * rMax);
    // the XR-30 on its wheels: its nozzles and jets cannot lever the airframe about the main gear until the wing carries
    // some of the weight (full back stick at a standstill sat it on its tail - "Struck terrain" - and full roll on a
    // wingtip, before the takeoff roll had begun);
    // the authority comes in with the takeoff roll and is whole well before rotation (60 m/s)
    if (s.special == 1 && onGround) { float ga = smoothstepf(25.f, 55.f, V); wd.x *= ga; wd.z *= ga; }   // (and roll: a wingtip)
    if (hover > 0) {  // hands-off attitude hold while hovering
      if (fabsf(ctl.pitch) < 0.05f) wd.x += hover * 2.2f * (0.f - pitchDeg()) * DEG;
      if (fabsf(ctl.roll) < 0.05f) wd.z += hover * 2.2f * bankDeg() * DEG;
    }
    // no g or angle-of-attack limiting: the stick commands rotation directly and the pilot owns the airframe's limits
    float ms0 = m / s.emptyMass, k = onGround ? 4.f : 9.f, kp = onGround ? 4.f : 18.f;   // stiffer pitch loop: snap reversals
    vec3 Ii(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
    if (wr) wraithThrust(F, T, thrust * 0.25f, wd, Taero, surfMax, dt);
    else T += vec3(Ii.x * kp * (wd.x - w.x), Ii.y * k * (wd.y - w.y), Ii.z * k * (wd.z - w.z));
  }

  // ---------------- ground contacts
  float L = s.fusLen, R = s.fusRad;
  float gh = gearHeight();
  const GearStations gst = gearStations(s);   // (where the drawn wheels are too)
  std::vector<Contact> cs;
  bool wheels = gear > 0.95f;
  if (wheels) {
    cs.push_back({vec3(-gst.track, -gh, gst.mainZ), 0}); cs.push_back({vec3(gst.track, -gh, gst.mainZ), 1});
    if (s.taildragger) cs.push_back({vec3(0, -gh + gst.tailY, gst.tailZ), 2});
    else cs.push_back({vec3(0, -gh, gst.noseZ), 2});
  }
  cs.push_back({vec3(0, -R, -0.45f * L), 3});           // nose / prop
  cs.push_back({s.taildragger ? vec3(0, -gh + 0.11f * L + 0.25f, 0.45f * L) : vec3(0, -R * 0.6f, 0.48f * L), 4});  // tail
  cs.push_back({vec3(-s.span * 0.5f, s.wingY * R, s.wingZ), 5});
  cs.push_back({vec3(s.span * 0.5f, s.wingY * R, s.wingZ), 5});
  cs.push_back({vec3(0, -R, 0), 6});                    // belly

  bool wheelContact[3] = {}; float wheelSpeed[3] = {};
  bool anyWheel = false;
  float roughSum = 0;
  vec3 Fw(0, 0, 0), Tw(0, 0, 0);  // world force, body torque
  float kSpring = s.emptyMass * 2.2f * G0 / 0.10f, cDamp = 2.f * 0.8f * sqrtf(kSpring * s.emptyMass * 0.6f);
  vec3 fwdW = forward();
  int nBraked = 0;
  for (const Contact& c : cs) if (c.kind <= 1) nBraked++;
  float holdV = 0; int holdN = 0;   // the held wheels' creep this step (it trims brakeHold)
  for (const Contact& c : cs) {
    vec3 pw = pos + q.rotate(c.p);
    float gy = g_world.height(pw.x, pw.z, 7);
    if (sceneryHits) gy = pitGround(pw.x, pw.z, gy);   // (down into what the game has dug: the player's aircraft, not the background quote's)
    bool water = gy < 0.3f && g_world.onRunway(pw.x, pw.z, 30) < 0;
    float surf = std::max(gy, 0.f);
    float pen = surf - pw.y;
    if (pen <= 0) continue;
    vec3 vc = vel + q.rotate(cross(w, c.p));
    float spd = length(vel);
    if (water && spd > 1.f) { ev.crashed = true; ev.crashReason = "Ditched in the sea"; return; }
    bool bellySkid = false;
    if (c.kind >= 3) {
      // a belly landing (gear up or stuck) is survivable: on a paved runway, wings level, at approach speed and
      // sinking gently, the aircraft skids on its belly and nose and stops; it is damaged, not wrecked
      if ((c.kind == 3 || c.kind == 5 || c.kind == 6) && !wheels && !s.taildragger && s.special == 0) {   // (a low wing's tips touch too: they scrape along with the belly)
        int rw = g_world.onRunway(pw.x, pw.z, 4);
        bool gentle = -vc.y < 2.5f && fabsf(bankDeg()) < 8.f && spd < s.vref * 1.35f && fabsf(pitchDeg()) < 12.f;
        if (ev.bellyLanding || (rw >= 0 && !surfaceRough(g_world.airports[rw].surface) && gentle)) { ev.bellyLanding = true; bellySkid = true; }
      }
      if (c.kind == 4 && wheels && spd > 5) { ev.tailStrike = true; }
      else if (spd > 2.5f && !bellySkid) {
        ev.crashed = true;
        ev.crashReason = c.kind == 3 ? (wheels ? "Prop/nose strike" : "Belly landing - gear was up") : c.kind == 5 ? "Wingtip struck the ground" : c.kind == 6 ? "Belly landing - gear was up" : "Struck terrain";
        if (!onGround && altAgl > 3) ev.crashReason = "Flew into terrain";
        return;
      }
      if (bellySkid && -vc.y > 4.6f) { ev.crashed = true; ev.crashReason = fmt("Belly landing too hard - hit at %.0f fpm", -vc.y * 196.85f); return; }
    }
    if (c.kind <= 2 && -vc.y > 4.6f) { ev.crashed = true; ev.crashReason = fmt("Gear collapsed - hit at %.0f fpm", -vc.y * 196.85f); return; }
    // spring/damper normal force (world up): the oleo's damping builds over its first centimetre and a half, through the
    // tyre's give (full at once, three wheels meeting the runway together at 1.2 m/s read 8.7 g for a frame), and it
    // is stiffer extending than compressing - its rebound orifice is the smaller - so a firm arrival doesn't spring
    // back into the air
    const float cEff = c.kind > 2 ? cDamp : vc.y < 0.f ? cDamp * std::min(pen / 0.015f, 1.f) : cDamp * 2.f;   // (full by the strut's static deflection)
    float nF = std::max(0.f, kSpring * pen - cEff * vc.y);
    if (c.kind >= 3) nF *= 2.f;
    vec3 f(0, nF, 0);
    if (bellySkid) {   // the belly and the nose grind along the runway: heavy friction against the motion, and it counts as on the ground
      vec3 vh(vc.x, 0, vc.z); float vl = length(vh);
      if (vl > 0.3f) f = f - vh * (nF * 0.55f / vl);
      anyWheel = true; roughSum += 0.8f;
      if (engineRunning && s.engineType != ENG_JET) { for (float& hh : fail.engineHealth) hh = 0.f; engineRunning = false; }   // the propeller strikes
    }
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
      wheelContact[c.kind] = true; wheelSpeed[c.kind] = vlong;
      float mu = rw >= 0 && !surfaceRough(g_world.airports[rw].surface) ? 0.8f : 0.55f;
      float rollRes = 0.015f + 0.06f * rough;
      // (the toe brakes on the rudder pedals: steering with the rudder while braking brakes that side's wheel harder and
      // eases the other's - how a taildragger is held straight on its brakes. Its main wheels stand ahead of its centre of
      // gravity, and braked evenly any swing grows: the Bushmaster ground-looped through 60 deg at Summit Pass on full
      // opposite rudder. The parking brake holds both)
      const float bk = c.kind > 1 ? 0.f : ctl.brake >= 0.99f ? 1.f : clampf(ctl.brake * (1.f + (c.kind == 0 ? -0.6f : 0.6f) * ctl.yaw), 0.f, 1.f);
      float brakeF = bk * 0.7f;   // (enough for the main wheels to hold full power when parked)
      // (sideways the tyre's force builds with its slip angle, the full friction at about 6 deg, tan 0.1 - and at a
      // standstill within a few cm/s of slip: looser, a steady side force, the fin in the slipstream's swirl at full
      // power, crept the aircraft sideways on its brakes. A fixed few cm/s at speed too was a slip angle of 0.05 deg
      // at 60 m/s: the main wheels pinned the heading, and full rudder could not take a crab out on the runway)
      f += wr * (-nF * mu * clampf(vlat / std::max(0.05f, fabsf(vlong) * 0.1f), -1, 1));
      if (brakeF > 0.35f && fabsf(vlong) < 0.3f) {
        // parked / held on the brakes: static friction - the wheel holds against whatever pushes it (power, slope)
        // up to the tyre's grip, and slips only past it (a linear ramp to zero at rest let full power creep)
        float hold = -(vlong * m / dt + brakeHold) / std::max(nBraked, 1);
        holdV += vlong; holdN++;
        float muS = (rough > 0.3f ? 0.7f : 0.9f) * bk;
        f += wf * clampf(hold, -nF * muS, nF * muS);
      } else f += wf * (-nF * (rollRes + brakeF) * clampf(vlong / 0.3f, -1, 1));
    } else {
      // sliding structure: heavy friction
      vec3 vh(vc.x, 0, vc.z);
      if (length(vh) > 0.01f) f += normalize(vh) * (-nF * 0.6f);
    }
    Fw += f;
    Tw += cross(c.p - cg, q.conj().rotate(f));
  }
  // (what was left of the creep is pushed back next step too: the held force converges on the steady push, so the
  // wheels stop dead instead of creeping a step's acceleration)
  if (holdN > 0) brakeHold = clampf(brakeHold + holdV / holdN * m / dt * 0.5f, -m * 30.f, m * 30.f); else brakeHold = 0;
  for (int i = 0; i < 3; ++i) wheelMotion[i].step(dt, wheelSpeed[i], wheelContact[i], i < 2 ? ctl.brake : 0.f);
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
  if (altAgl < 200.f && sceneryHits) {
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
  {   // structure: past the limit the airframe takes a sustained overstress (20% over for five seconds, double for one)
    // before it lets go; far past it (2x) it fails at once. Within the limit the stress relaxes.
    float gp = s.gLimitPos(), gn = s.gLimitNeg();
    float over = gLoad > gp ? gLoad / gp - 1.f : gLoad < gn ? gLoad / gn - 1.f : -0.5f;
    overG = clampf(overG + over * dt, 0.f, 2.f);
    bool snap = gLoad > gp * 2.f || gLoad < gn * 2.f;
    if (!anyWheel && (overG >= 1.f || snap)) { ev.crashed = true; ev.crashReason = "Structural failure - overstressed airframe"; return; }
  }
  // the centre of gravity moves under the forces and the airframe turns about it; the model's origin (pos, vel) rides
  // along
  const vec3 cgW = q.rotate(cg);
  vec3 vcg = vel + cross(q.rotate(w), cgW), pcg = pos + cgW;
  vcg += acc * dt;
  pcg += vcg * dt;
  // inertia scales with loading
  float ms = m / s.emptyMass;
  vec3 I(s.Iyy * ms, s.Izz * ms, s.Ixx * ms);
  vec3 tot = T + Tw;
  vec3 Iw(I.x * w.x, I.y * w.y, I.z * w.z);
  vec3 wdot = tot - cross(w, Iw);
  w += vec3(wdot.x / I.x, wdot.y / I.y, wdot.z / I.z) * dt;
  // gear/strut damping: the struts' oscillation in pitch and roll (a yaw on the ground is resisted by the tyres' side
  // forces, by their slip angles: damped here too, the rudder took 8 s to take a 7 deg crab out on the runway)
  if (onGround) { const float k = expf(-2.0f * dt); w.x *= k; w.z *= k; }
  float wl = length(w);
  if (wl > 1e-6f) { q = q * quat::axisAngle(w / wl, wl * dt); q.normalize(); }
  { const vec3 cgW1 = q.rotate(cg); pos = pcg - cgW1; vel = vcg - cross(q.rotate(w), cgW1); }

  // ---------------- autopilot inner loops
  if (apOn) apControl(dt);
}

// ------------------------------------------------------------------ XR-40 Wraith thruster pods
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

// XR-40 vertical landing: nozzles down, pitch for the along-track speed, bank for the cross-track, throttle for
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


bool Plane::apTowerGoAround() {
  if (!apOn || apAirport < 0 || onGround || apMode == AP_STUNT) return false;
  if (apStage == APS_GOAROUND) return true;
  if (apStage == APS_ROLLOUT || !apEnv.canGoAround) return false;
  if (getenv("APDBG")) printf("  go-around: the tower's (runway occupied)\n");
  apStage = APS_GOAROUND; apStageT = 0;
  return true;
}

void Plane::apEngage(int mode, int airport, const Weather& wx) {
  apOn = mode != AP_OFF; apMode = mode; apDone = false; apWindEvent = 0;
  float spd0 = ias > 1.f ? ias : length(vel);
  apHeading = heading(); apAlt = pos.y; apSpeed = std::max(spd0, spec->vref * 1.3f);
  apPitchI = 0; apRollI = 0; apYawI = 0; apVmcCap = 1; apThrI = ctl.throttle; apXI = 0; apGamI = 0; apTrimEst = ctl.pitch; apUseVS = false; apUpset = false;
  apSpdEst = ias; apVelPrev = vel; apNzCmd = gLoad; apBankOk = 25.f;
  apAirport = airport; apStage = APS_NAV; apStageT = 0; apLeg = 0; apTurnDir = 0; apClimbDir = 0; apBled = false; apBleedT = 1e9f;
  apDecline.clear(); apHoldFor = -1;
  if (mode >= AP_NAV && airport >= 0) {
    // without the power to hold its height (every engine out, or what is left can't: the review of v3.44.0, FLT-2) there
    // is no climbing to a cruise and no descent orbit to fly: a straight-in final it can glide to - the field asked for,
    // either end, else the nearest other within reach - flown from where it is; none: declined, said why, handed back
    apSense();
    if (apEnv.climb < 0.3f) {
      const float L = glideRatio() * 0.8f;   // (a margin on the best glide: the turn onto it, the speed it has to fly)
      auto path = [&](int ai, bool rv) {      // the distance to glide to that threshold, or -1: not a straight-in from here
        const Airport& a = g_world.airports[ai];
        const vec3 ld = rv ? -a.dir() : a.dir(), thr = a.threshold(rv);
        vec3 rel = pos - thr; rel.y = 0;
        rel.x = wrapCoord(rel.x); rel.z = wrapCoord(rel.z);   // (the short way round the map)
        const float along = rel.x * ld.x + rel.z * ld.z, cross = fabsf(rel.x * ld.z - rel.z * ld.x);
        vec3 v = vel; v.y = 0; const float vl = length(v);
        const float align = vl > 1.f ? (v.x * ld.x + v.z * ld.z) / vl : 0.f;
        if (along > -300.f || align < 0.7f || cross > 200.f - along * 0.4f) return -1.f;
        const float d = -along + cross;
        return (pos.y - a.elev - 30.f) * L >= d ? d : -1.f;
      };
      int pick = -1; bool pickRev = false;
      for (bool rv : {false, true}) if (pick < 0 && path(airport, rv) >= 0.f) { pick = airport; pickRev = rv; }
      if (pick < 0) {   // (the others: the nearest within reach)
        float nd = 1e9f;
        for (int ai = 0; ai < (int)g_world.airports.size(); ai++) for (bool rv : {false, true}) {
          const float d = ai == airport ? -1.f : path(ai, rv);
          if (d >= 0.f && d < nd) { nd = d; pick = ai; pickRev = rv; }
        }
      }
      if (pick < 0) {
        apDecline = g_world.airports[airport].code + std::string(": no runway within gliding reach - fly the glide by hand");
        apOn = false; apMode = AP_OFF; apAirport = -1;
        return;
      }
      apAirport = pick; apRev = pickRev; apPlan(pick, pickRev, wx, true);
      apMode = AP_APPR; apStage = APS_FINAL; apStageT = 0;
      if (pick != airport) apDecline = g_world.airports[airport].code + std::string(": out of gliding reach - gliding to ") + g_world.airports[pick].code + " instead";
      return;
    }
    // runway end: the better of the two plans (terrain on the approach, headwind, how far away it is, and whether the
    // aircraft can stop on it and get down to it at all). Neither safe: the autoland is declined, said why, and the
    // autopilot circles clear of the ground (trying again as the wind changes) - the pilot lands by hand or picks another field (QA F1: committing to either
    // end at Cedar Ridge in a gusty quartering wind ended in an overrun or in go-arounds into the hills)
    float sRev = apPlan(airport, true, wx, false); std::string whyRev = apPlanWhy;
    float sFwd = apPlan(airport, false, wx, false); std::string whyFwd = apPlanWhy;
    if (!whyRev.empty() && !whyFwd.empty()) {
      apDecline = g_world.airports[airport].code + std::string(": ") + (whyFwd == whyRev ? whyFwd : whyFwd + " / " + whyRev);
      apMode = AP_HOLD; apAirport = -1; apHoldFor = airport; apRetryT = 20.f;
      return;
    }
    bool rev = whyFwd.empty() == whyRev.empty() ? sRev > sFwd : whyFwd.empty() ? false : true;
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
// What the aircraft can do now (ApEnvelope). The learned envelope was measured once per type, at the test weight (60%
// fuel, 150 kg aboard) in still air at 1200 m; this corrects it for what is different now. The autopilot's every
// decision reads the result - the speeds it flies, how hard it turns, climbs and descends, how steep an approach it
// takes, whether it can go around, how it sheds speed and whether it can come down vertically - so it flies whatever
// it is flying, as it is: heavy or light, high or low, iced, an engine out.
void Plane::apSense() {
  const AircraftSpec& s = *spec;
  const PerfModel& P = perf(spec);
  ApEnvelope& E = apEnv;
  const float mTest = s.emptyMass + s.maxFuel * 0.6f + 150.f, rhoTest = isaDensity(1200.f);
  E.mass = mass(); E.wRatio = E.mass / mTest;
  E.sigma = density / 1.225f;
  // the stall: with the square root of the weight; ice takes up to 30% of the wing's lift
  const float wk = sqrtf(E.wRatio) / sqrtf(std::max(1.f - 0.3f * fail.ice, 0.5f));
  // the approach reference: the type's, but never under 1.23 times the stall it has (FAR 25's VREF; the revised XR-20's
  // canard holds its trimmed lift to 1.27, and its book speed was only 5% above that)
  E.vs0 = P.vs0 * wk; E.vs1 = P.vs1 * wk; E.vApp = std::max(s.vref, 1.23f * P.vs0) * wk;
  // a flap that stopped (the other matched to it: apControl): the approach at that setting's own reference speed, and
  // the landing roll longer by its square
  const float vAppFull = E.vApp;
  if (fail.flapAsym) {
    const float vsAt = E.vs1 + (E.vs0 - E.vs1) * clampf(fail.flapAt / std::max(aeroGeom(s).flapMax, 0.05f), 0.f, 1.f);
    E.vApp = std::max(E.vApp, 1.23f * vsAt);
  }
  // the engines: full thrust now (their health, running or not, this air) over the tests'
  float healthSum = 0; for (int i = 0; i < s.engines && i < 4; i++) healthSum += fail.engineHealth[i];
  const float health = engineRunning || starterTime > 0.f ? (s.engines > 0 ? healthSum / s.engines : 1.f) : 0.f;
  const float vy = std::max(P.vy, 20.f);
  E.thrustFrac = thrustAt(health, vy, vy) / std::max(thrustAt(1.f, vy, vy, rhoTest), 1.f);
  E.spool = 1.f / spoolRate();
  // the climb: the excess power the tests measured, scaled by the thrust there is now, against the power the flight
  // itself takes (half the idle sink: clean, at the best-climb speed), which grows with the weight and thinner air
  const float sc = 0.5f * P.sinkIdle;
  E.climb = (P.roc + sc) * E.thrustFrac / E.wRatio - sc * sqrtf(E.wRatio * (rhoTest / 1.225f) / std::max(E.sigma, 0.1f)) * (1.f + 0.6f * fail.ice);
  // (what it holds for minutes at working speeds: a fraction of the best, less of a big one - fitted to the climbs the
  // autopilot was tuned with: 4 m/s for a trainer, 12 for a bizjet, 30 for the XR-30)
  E.climbPlan = E.climb > 0.f ? std::min(0.6f * E.climb, 4.f + 0.13f * E.climb) : E.climb;
  E.canGoAround = E.climbPlan > 1.f;
  // (the airframe takes 8 g or more - the research craft - and its nose comes up far enough on a full pull to stand it
  // against the air, rather than zooming)
  E.highAlpha = P.gUse >= 8.f && P.rearPitch >= 45.f;
  // the approach: down no faster than it sinks at idle in landing trim at its approach speed (faster gains speed it
  // has to lose again before the flare - the heavy Q400 oscillated about the glidepath into the trees short of Solace
  // Capital at 10 m/s) - except an airframe that sheds speed with a belly-up, which may dive at up to ~8.5 deg; a
  // glidepath no steeper than 0.6 of its idle glide in landing trim, and shallow (3.5 deg) for engines slow to spool -
  // the power stays on and a go-around isn't waiting for them - unless it is built to land short (a STOL airframe)
  const float sinkNow = P.sinkIdle * sqrtf(E.wRatio);
  E.descentMax = clampf(E.highAlpha ? std::max(sinkNow, 0.15f * 1.18f * E.vApp + 1.f) : sinkNow, 5.f, 10.f);
  const float glide = atanf(P.sinkIdle / std::max(1.25f * P.vs0, 10.f)) / DEG;
  E.glideMax = clampf(std::min(0.6f * glide, E.spool >= 2.f ? 3.5f : 6.5f), 3.f, 6.5f);
  if (P.ldgRoll > 0.f && P.ldgRoll < 260.f) E.glideMax = 6.5f;
  E.gUse = P.gUse;
  E.ldgDist = P.ldgRoll * E.mass / (s.emptyMass + s.maxFuel + s.cargoKg) * (E.vApp / vAppFull) * (E.vApp / vAppFull);   // (the learned distance is at full weight and flap)
  E.hover = liftThrustMax() > 1.1f * E.mass * G0;
  E.agile = P.gUse >= 30.f;
  E.rateCmd = s.special != 0;
  E.flaps = s.flapCL > 0.01f;
}

// (the airlines' and the flying schools' rule: stabilised by 1,000 ft, or by 500 ft flying visually - a light aircraft's
// final is a mile or two, and its gate at 330 m held a trainer on a 6 km final for three and a half minutes)
static float apStabHeight(float vApp) { return vApp < 45.f ? 150.f : 300.f; }
float Plane::apStabH() const { return apStabHeight(apEnv.vApp); }
// The turns the autopilot plans with, from the speed it flies them at. Gentle (passengers, a fragile load): a 26 deg
// bank, never tighter than a standard-rate turn. Otherwise flown hard, 45 deg - except fast (a planned speed above
// ~120 m/s), where the localizer's look-ahead, not the turn, sets how far out it settles on the centreline: there no
// tighter than a standard-rate turn either (tighter, the XR-30 and XR-40 swapped outbound and intercept legs without
// end). vPlan: the speed the plan was made for (the hold's), so the geometry doesn't flicker with the speed of the moment
static float apFastK(float vPlan) { return smoothstepf(115.f, 125.f, vPlan); }
static float apTurnR(float V, float vPlan, bool gentle, float hardBank = 45.f) {
  if (gentle) return std::max(V / (3.f * DEG), V * V / (G0 * tanf(26.f * DEG)));
  return std::max(V * V / (G0 * tanf(hardBank * DEG)), V / (3.f * DEG) * apFastK(vPlan));
}
// the descent orbit's radius at the hold speed: 24 deg gentle, 45 hard, and fast never tighter than 17.6 s of flight
// (a tighter one brought the XR-30 to Northpoint at 108 kt and it ran off the end)
static float apHoldRadius(float vh, bool gentle, float hardBank = 45.f) {
  const float k = apFastK(vh);
  const float r = std::max(vh * vh / (G0 * tanf((gentle ? 24.f : hardBank) * DEG)) * 1.15f, vh * 17.6f * k);
  return clampf(r, gentle || k > 0.5f ? 900.f : 400.f, 3500.f);
}
// the hold and intercept speed: well above the approach speed, below cruise
// (a professional's: 1.4 to 1.5 times it, an airliner's manoeuvring speed in the pattern - at 1.8 the Starling's 25 deg
// orbit took three minutes a lap and its pattern ran 12 km out)
static float apHoldSpeed(float vApp, float cruise, bool pro = false) {
  return pro ? std::max(vApp * 1.4f, std::min(cruise * 0.6f, vApp * 1.5f)) : std::max(vApp * 1.45f, std::min(cruise * 0.6f, vApp * 1.8f));
}

static const float kIntRegion = 6000.f;   // how far beyond the gate the approach's intercept region reaches (m)

// the landing distance one end needs in this wind (apPlan, below, says how it was fitted), and the tailwind it reckoned
// with (tw)
float Plane::apStopNeed(const Airport& a, bool rev, const Weather& wx, float* tw) const {
  const ApEnvelope& E = apEnv;
  vec3 ld = rev ? a.dir() * -1.f : a.dir();
  vec3 td = a.threshold(rev) + ld * clampf(a.length * 0.12f, 80.f, 300.f); td.y = a.elev;
  vec3 from(sinf(wx.windFrom * DEG), 0, -cosf(wx.windFrom * DEG));
  const float hw = dot(ld, from) * wx.windSpeed;
  const float t = hw < 0.f ? -hw + 0.5f * wx.gust : 0.f;
  if (tw) *tw = t;
  const float sigma = isaDensity(a.elev) / 1.225f;
  const float vtd = E.vApp / sqrtf(sigma) + t;
  return E.hover ? 0.f : len2(td - a.threshold(rev)) + (spec->taildragger ? 0.27f : 0.2f) * vtd * vtd * (surfaceRough(a.surface) ? 1.1f : 1.f);   // (what holds itself up on its thrust comes down vertically)
}
float Plane::apPlan(int airport, bool rev, const Weather& wx, bool commit) {
  const AircraftSpec& s = *spec;
  apSense();
  const ApEnvelope& E = apEnv;
  const Airport& a = g_world.airports[airport];
  auto H = [](vec3 q) { return g_world.height(q.x, q.z); };
  const vec3 P(nearCopy(pos.x, a.x), pos.y, nearCopy(pos.z, a.z));   // (where the aircraft is, the short way round the map from the field)
  vec3 ld = rev ? a.dir() * -1.f : a.dir(), rr(-ld.z, 0, ld.x);
  vec3 td = a.threshold(rev) + ld * clampf(a.length * 0.12f, 80.f, 300.f); td.y = a.elev;
  // glidepath: 3 degrees, steepened (up to what the type can fly) to clear the ground and trees under the final
  const float maxAng = E.glideMax;
  auto margin = [](float d) { return clampf(d * 0.02f, 8.f, 60.f); };
  float need = tanf(3.f * DEG);
  // (the final clears what stands on the ground too - the trees on a rise short of Harlan Farm Strip's runway 17 stood
  // 25 m above the terrain the glidepath was planned over, and the Osprey flew into them a kilometre short - by half the
  // ground's margin: an obstacle clearance surface, not a second terrain margin on top of the treetops, which closed
  // Far Isle to the Starling)
  auto clear = [&](vec3 q, float d) { return std::max(H(q) + margin(d), g_scenery.obstacleTop(q.x, q.z, 40.f) + clampf(d * 0.01f, 5.f, 20.f)); };
  for (float d = 300.f; d <= 3500.f; d += 50.f) { vec3 q = td - ld * d; need = std::max(need, (clear(q, d) - a.elev) / d); }
  float gs = std::min(need, tanf(maxAng * DEG));
  // the gate - the final approach fix - where the glidepath is 230 m up for a light aircraft, 330-460 m (1,500 ft) for an
  // airliner or a jet (8 s of its approach speed in height), as a professional's is: on the glidepath and configured
  // from there, the approach is stable by the height it should be (apStabH: the gate at 4.5 km put a jet's glidepath
  // capture at 257 m, inside the 300 m it should already be stable by; at 460 m a trainer's final took four and a half
  // minutes); never under the 4.5 km it was, and the ground can still shorten it (below)
  const float gateH = apStabHeight(E.vApp) < 200.f ? 230.f : clampf(E.vApp * 8.f, 330.f, 460.f);
  float F0 = apPro ? clampf(std::max(E.vApp * 100.f, gateH / gs), 4500.f, 9500.f) : clampf(E.vApp * 100.f, 4500.f, 8000.f), F = F0;
  for (float d = 300.f; d <= F0; d += 100.f) {
    vec3 q = td - ld * d;
    if (a.elev + d * gs < clear(q, d)) { F = d - 600.f; break; }
  }
  float blocked = F < 2000.f ? 3000.f : 0.f;
  F = std::max(F, 2000.f);
  float gh = gearHeight();
  float iafAlt = a.elev + gh + F * gs + 20.f;
  // orbit radius: a comfortable turn at holding speed
  const float vh = apHoldSpeed(E.vApp, s.cruise, apPro);
  const float R = apHoldRadius(vh, apComfort, apPro ? apProBank() : 45.f);
  // intercept region: the extended centreline from the gate out to 6 km beyond it (the guidance captures the final and
  // turns back from its outbound leg inside it)
  float intMsa = 0;
  for (float d = F; d <= F + kIntRegion; d += 250.f) for (int k = -2; k <= 2; k++) intMsa = std::max(intMsa, H(td - ld * d + rr * (k * 500.f)));
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
                 + std::max(0.f, len2(c - P) + len2(c - qi) - len2(qi - P)) * 0.12f;
      if (cost < bestCost) { bestCost = cost; bestC = c; bestAlt = hAlt; }
    }
  vec3 from(sinf(wx.windFrom * DEG), 0, -cosf(wx.windFrom * DEG));
  // (the wind weighs 40 per m/s of headwind, the distance to the entry 0.02 per metre: a 10 kt wind is worth about
  //  10 km of flying round to the other end, a light one isn't)
  // the landing distance this end needs, from the threshold to a stop: the aim point (td above), then the float and
  // the roll. Flown by the autopilot - every type it lands, 290 calm landings at every field - the float and the roll
  // together came to 0.20 of the touchdown ground speed squared (s^2/m: about 2.5 m/s^2 of deceleration from the aim
  // point, whatever the type; a taildragger, held tail-up while it's fast, 0.27). The touchdown speed: the approach
  // speed at the weight and ice it has now, in the field's thinner air, with the tailwind and half its gusts on top; a
  // rough surface brakes worse. All of it against the whole runway (the review of v3.33.0, A1: the learned still-air
  // distance x1.6, cut to 90% of the runway, was over twice the props' roll and short of the jets', and only the jets'
  // tailwind was added). QA F1: the Starling overran Cedar Ridge's 1,000 m with a 4-7 m/s tailwind.
  const float hw = dot(ld, from) * wx.windSpeed;
  float tw = 0.f;
  const float ldgNeed = apStopNeed(a, rev, wx, &tw);
  // every field, whatever got it chosen (the GPS offers them all): the runway the type may use at all - the career's own
  // dispatch rule, its surface (surfaceOK) and length (AircraftSpec::runwayNeeded) - comes first, then the landing
  // distance (the review of v3.31.0, F2: the Starling took Gull Rock's 480 m and overran it; of v3.33.0, A1: Harlan
  // Farm's grass and Palm Bay's sand, which the career never sends it to, and it overran them)
  const bool surfBad = !E.hover && !surfaceOK(s, a.surface);
  const float rwyNeed = s.runwayNeeded(a.elev), fieldShort = E.hover ? 0.f : rwyNeed - a.length;
  const float stopShort = fieldShort > 0.f ? 1000.f + fieldShort : ldgNeed - a.length;
  // ...and whether it can get down to the glidepath from the hold: it leaves the orbit at its height and descends from
  // there, may not go below the intercept altitude until the gate, and goes around if still 80 m high 2 km out.
  // What it can lose on the way: nine tenths of its steepest final descent (the guidance's limit) at the approach
  // ground speed
  const float vsMax = E.descentMax, vgApp = std::max(E.vApp * 1.2f - hw, 20.f);
  auto lose = [&](float d) { return std::max(d, 0.f) / vgApp * vsMax * 0.9f; };
  const float gp2k = a.elev + gh + 2000.f * gs, gpF = a.elev + gh + F * gs;
  const float legOut = len2(bestC - (td - ld * (F + 1500.f))) + 1500.f;   // (from leaving the orbit to the gate: the guidance descends all the way)
  const float hAtGate = std::max(bestAlt - lose(legOut), std::max(intAlt, gpF));
  // ...and from the turn-in: the outbound leg and the turn back (the guidance's own geometry: out past the gate by its
  // minimum and a turn's width, a turn and a half across - gentle turns are wide) at the ground's height there plus the
  // en-route margin, then down the final (QA F1: a passenger Starling at Kettle Lake turning in wide over the ridge
  // beyond runway 20 was lifted too high to land, round and round, until it met the ridge)
  const float Rin = apTurnR(vh, vh, apComfort, apPro ? apProBank() : 45.f);   // (as the guidance's)
  const float outReach = F + 1300.f + vh * 15.f + 2.f * Rin, crossReach = 3.4f * Rin;
  float turnMsa = a.elev;
  for (float d = F; d <= outReach; d += 400.f) for (float c = -crossReach; c <= crossReach; c += 500.f) turnMsa = std::max(turnMsa, H(td - ld * d + rr * c));
  const float highAt2k = hAtGate - lose(F - 2000.f) - (gp2k + 75.f);
  // (where the turn-in's ground would lift it well above the intercept altitude, this end is no good for turns this wide:
  // the en-route terrain floor takes it up and the final is never met from there)
  const float turnHigh = turnMsa + 250.f - (intAlt + 150.f);
  // ...and whether it stays on the chart: the orbit was chosen inside it, but the outbound leg and the turn back onto
  // the final reach out beyond the gate - from the gate to the turn back, two turns' width either side (the review of
  // v3.33.0, A1: the passenger Starling flew off the chart's edge from Palm Bay and was lost. Far Isle, in the chart's
  // corner, still fits from the north-west)
  float offChart = 0.f;
  for (int k = 0; k < 4; k++) {
    vec3 q = td - ld * (k & 1 ? outReach : F) + rr * (k & 2 ? 2.f * Rin : -2.f * Rin);
    offChart = std::max(offChart, std::max(fabsf(q.x), fabsf(q.z)) - WORLD_HALF * 1.1f);   // (the flight is lost at 1.2)
  }
  std::string surf = surfaceName(a.surface); for (auto& ch : surf) ch = (char)tolower(ch);
  apPlanWhy = surfBad ? fmt("the %s can't use a %s runway", s.name, surf.c_str())
            : fieldShort > 0.f ? fmt("runway too short for the %s (%.0f m, it needs %.0f m)", s.name, a.length, rwyNeed)
            : stopShort > 0.f ? fmt("runway too short to stop on%s (%.0f of %.0f m)", tw > 0.f ? " with this tailwind" : "", ldgNeed, a.length)
            : highAt2k > 0.f ? "terrain keeps the approach too high to descend onto"
            : turnHigh > 0.f ? (apComfort ? "high ground where it would turn in (gently, for the passengers or the load)" : "high ground where it would turn in")
            : offChart > 0.f ? "the approach would leave the chart" : "";
  // (and a stabilised final: the ground round the intercept holding it above the glidepath at the gate means a dive from
  // there to the glidepath - full flap, the throttle closed, through 300 m above the field at three times the sink of a
  // stabilised approach. Where the other end needs none, a professional takes it: 15 per metre to lose, about as much
  // as a 5 kt headwind for every 15 m. But only what it can't lose between the gate and the height it is to be stable
  // by, at the 1,000 fpm a stable approach may sink at - a trainer can lose 140 m there, and to save itself a 150 m
  // drop the Kestrel flew 15 km to Meadowbrook's far end)
  const float spareDrop = (gateH - apStabHeight(E.vApp)) * std::max(0.f, 5.1f / std::max(E.vApp, 10.f) / std::max(gs, 0.02f) - 1.f);
  const float dive = apPro ? std::max(0.f, intAlt - iafAlt - spareDrop) : 0.f;
  float score = hw * 40.f - bestCost - (F0 - F) * 0.3f - length(bestC - P) * 0.02f - blocked - dive * 15.f
              - (atanf(gs) / DEG - 3.f) * 150.f - (stopShort > 0.f ? 20000.f + stopShort * 20.f : 0.f) - (highAt2k > 0.f ? 20000.f + highAt2k * 20.f : 0.f) - (turnHigh > 0.f ? 20000.f + turnHigh * 20.f : 0.f)
              - (surfBad ? 40000.f : 0.f) - (offChart > 0.f ? 20000.f + offChart * 20.f : 0.f);
  if (getenv("APDBG")) printf("apPlan %s rev %d: intercept alt %.0f m above the field, glidepath at the gate %.0f m; ", a.code, (int)rev, intAlt - a.elev, iafAlt - a.elev);
  if (getenv("APDBG")) printf("apPlan %s rev %d: F %.0f gs %.2f deg blocked %.0f wind %+.1f cost %.0f landing %.0f of %.0f m, %.0f m high at 2 km, score %.0f %s (hold %.0f m above the field, leg %.0f m)\n", a.code, (int)rev, F, atanf(gs) / DEG, blocked, hw, bestCost, ldgNeed, a.length, highAt2k, score, apPlanWhy.c_str(), bestAlt - a.elev, legOut);
  if (commit) {
    apRev = rev; apFinalLen = F; apGs = gs; apHoldC = bestC; apHoldC.y = 0; apHoldR = R; apHoldAlt = bestAlt; apIntAlt = intAlt;
    apLd = ld; apTd = td;
  }
  return score;
}

void Plane::apGuidance(float dt) {
  const AircraftSpec& s = *spec;
  apStageT += dt;
  const ApEnvelope& E = apEnv;
  if (apMode != AP_APPR || apAirport < 0) {   // (hold: altitude unless a vertical speed was asked for, apUseVS)
    if (apHoldFor >= 0) {   // a declined autoland: circling where it is, clear of the ground, until the wind allows the
      // field (Plane::step tries it again) or the pilot takes it (holding its heading it flew on into the hills: the
      // review of v3.34.0, F4)
      apHeading = wrapDeg360(heading() + 6.f); apUseVS = false;
      apAlt = std::max(apAlt, terrainAround() + 300.f);
      apStatus = "UNABLE AUTOLAND " + apDecline + "  //  " + fmt("CIRCLING  ALT %.0f ft  SPD %.0f kt", apAlt * M_TO_FT, apSpeed * MS_TO_KT);
      return;
    }
    apStatus = (apDecline.empty() ? std::string() : "UNABLE AUTOLAND " + apDecline + "  //  ") + fmt("HOLD  HDG %03.0f  ALT %.0f ft  SPD %.0f kt", wrapDeg360(apHeading), apAlt * M_TO_FT, apSpeed * MS_TO_KT);
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
  const float vref = E.vApp;   // (this weight's, this ice's)
  int rwyN = a.rwyNumber(apRev);
  float vnow = std::max(length(vel), 30.f);
  // wind drift (ground track minus heading), smoothed: the centreline legs steer the track, not the heading
  if (len2(vel) > 10.f) apDrift = approach(apDrift, clampf(hdgErrDeg(atan2f(vel.x, -vel.z) / DEG, heading()), -30.f, 30.f), 0.6f, dt);
  switch (apStage) {
    case APS_NAV: {
      vec3 C = apHoldC; C.y = pos.y;
      C.x = nearCopy(C.x, pos.x); C.z = nearCopy(C.z, pos.z);   // (the short way there, across the map's seam if that is shorter)
      float dc = len2(C - pos);
      const float vh = apHoldSpeed(vref, s.cruise, apPro);
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
        // slow down in time to turn tightly (not so early that slow, high-power flight eats the reserve) - a professional
        // starts as far out as it takes to slow from the speed it has at 2 m/s^2, so a jet at 400 m/s is down to its
        // terminal speed before it turns (its turns at 25-60 deg at that speed are tens of km wide: the XR-40 flew off
        // the chart)
        const float vNow = std::max(ias, vh), slowDist = apPro ? (vNow * vNow - vh * vh) / (2.f * 2.f) + vh * 20.f + 2500.f : 2500.f + s.cruise * 20.f;
        // (once slowed, it stays slow unless it is taken well away again: the slowing distance shrinks as the speed comes
        // off, and the Meridian, turning round onto the hold, sped up to cruise and slowed again twice over)
        const bool slowed = apPro && apSpeed > 0.f && apSpeed <= vh + 0.5f;
        apSpeed = dc < slowDist + (slowed ? 3000.f : 0.f) ? vh : s.cruise * 0.85f;
        // climb planning: can this aircraft out-climb the ground ahead on the way? Compare the height needed over
        // the next 12 km of track with what it can reach at a conservative climb gradient. If it can't, climb in a
        // circle (turning towards the lower side) until it can, then carry on.
        {
          vec3 f = d * (1.f / std::max(dc, 1.f));
          float vsCap = std::max(E.climbPlan, 0.5f) * 0.55f;   // (the climb it can hold now, with a margin)
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
              apSpeed = std::max(vh, vref * 1.35f);
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
          // (a professional: or near enough to lose the rest on the way to the gate, at a gentle 3 deg - the Meridian
          // circled for 100 s to lose 35 m)
          const vec3 gate = td - ld * F;
          const float onWay = apPro ? 0.05f * len2(vec3(gate.x - pos.x, 0, gate.z - pos.z)) : 0.f;
          if (pos.y < apHoldAlt + std::max(60.f, onWay)) apLeg = along > -(F + 500.f + vnow * 15.f) ? 3 : 2;
        }
        if (apClimbDir == 0) apStatus = fmt("NAV  %s  RWY %02d  %.1f km", a.code, rwyN, (dc + F) / 1000.f);
      } else {
        // the intercept: the extended centreline is flown with the same steering as the localizer, from far enough
        // out to settle before the gate. Too close in (or on the wrong side), first fly outbound, diverging a little.
        float outMin = F + 500.f + vnow * 15.f;
        float Rt = vnow * vnow / (G0 * tanf((E.agile ? 30.f : 20.f) * DEG));
        float Rturn = apTurnR(vnow, vh, apComfort, apPro ? apProBank() : 45.f);   // NAV turns (apTurnR: gentle, hard, or fast)
        float L1 = vnow * 14.f;
        // (the side is kept from the outbound leg's start: still turning from the hold it can cross the centreline, and a
        // side taken afresh then flipped the line to the far side, which it chased out past 20 km - QA F1, Kettle Lake)
        if (apLeg != 3) apOutSide = 0.f;
        else if (apOutSide == 0.f) apOutSide = cross >= 0 ? 1.f : -1.f;
        float side = apLeg == 3 ? apOutSide : cross >= 0 ? 1.f : -1.f, D = side * 2.4f * Rturn;
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
              (fabsf(hdgErrDeg(intercept(next), heading())) < (apPro ? 90.f : 30.f) || apStageT > 2.f * PI * apHoldR / vh + 20.f)) { apLeg = next; apStageT = 0; }   // (a professional: once down, it turns out for the final from anywhere it need not reverse its turn to - a lap of a 25 deg orbit is minutes)
          apStatus = fmt("HOLD  %s  %s %.0f ft", a.code, pos.y > apHoldAlt + 50.f ? "descending to" : "leaving at", apHoldAlt * M_TO_FT);
        } else {
          if (apLeg == 2 && along > -(outMin - 400.f) && fabsf(cross) > 300.f) apLeg = 3;
          // (or sooner, wherever it is across, once the ground ahead would lift it above the intercept altitude: the plan
          // keeps away from ends with high ground where it turns in, this is the guard - QA F1, Kettle Lake)
          if (apLeg == 3 && along < -(outMin + 800.f) && (fabsf(cross - D) < 400.f || terrainAround() + 250.f > apIntAlt + 100.f)) apLeg = 2;
          apHeading = intercept(apLeg);
          apAlt = std::max(apIntAlt, std::min(apHoldAlt, pos.y));
          apSpeed = vh;
          float hd = hdgErrDeg(heading(), rwyHdg), th = fabsf(hd);
          float lead = Rt * (1.f - cosf(std::min(th, 90.f) * DEG)) + 150.f;
          bool closing = cross * hd < 0.f || fabsf(cross) < 150.f;
          if (apLeg == 2 && along < -1500.f && along > -(F + kIntRegion) && fabsf(cross) < lead && closing && th < 70.f) { apStage = APS_FINAL; apStageT = 0; apXI = 0; }
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
      // the glidepath aims short of the touchdown point by the distance the flare floats (its time constant at this
      // ground speed), so the wheels meet the runway at the touchdown point - but never closer than 60 m past the
      // threshold
      float fromThr = dot(td - (a.pos() - ld * (a.length * 0.5f)), ld);
      float floatM = std::max(length(vec3(vel.x, 0, vel.z)), 20.f) * 1.3f * clampf(2.2f * apPathLag(), 2.5f, 8.f);
      float aimShift = clampf(floatM, 0.f, std::max(fromThr - 60.f, 0.f));
      float gsAlt = a.elev + gh + std::max(dist - aimShift, 0.f) * gs + 1.f;
      float vg = std::max(vel.x * ld.x + vel.z * ld.z, 15.f);
      float err = gsAlt - pos.y;
      apUseVS = true;
      // (with passengers or a fragile load, a stabilized approach: no more than ~500 fpm beyond the glidepath's own
      // descent, whatever the airframe could do - inside the comfort g a deeper dive to the path from a high gate isn't
      // arrested in time, and the Q400 porpoised about it down into the trees short of Solace Capital)
      const float dMax = apComfort ? std::min(E.descentMax, std::max(vg * gs + 2.5f, 5.f)) : E.descentMax;
      apVS = err > 25.f ? 0.3f : clampf(-vg * gs + err * apAltGain(), -dMax, 3.f);
      // low down, never chase the glidepath faster than its own descent (and 1 m/s more at 60 m, none by 15 m): after a
      // gust balloons it near the ground it lands a little long rather than dive for the path - a slow-pitching airframe
      // that nosed over to regain it met the flare at 5 m/s (QA F1: the XR-10's hard landings at Northpoint, Far Isle)
      if (hab < 60.f) apVS = std::max(apVS, -(vg * gs + clampf((hab - 15.f) / 45.f, 0.f, 1.f)));
      // outside the gate the glideslope can run below the safe intercept altitude: hold that until the gate
      if (dist > F && pos.y < apIntAlt + 30.f) apVS = std::max(apVS, clampf((apIntAlt - pos.y) * 0.1f, -1.f, 3.f));
      // and above the ground ahead: the planned corridor ends at the gate, and beyond it the extended glidepath can run
      // into high ground (QA F1: a Starling captured 19 km out at Kettle Lake descended into the ridge on its path)
      float hiAhead = -1e9f;
      for (int i = 1; i <= 8; i++) { vec3 q = pos + vec3(ld.x, 0, ld.z) * (i * 250.f); if (dist - i * 250.f > 400.f) hiAhead = std::max(hiAhead, g_world.height(q.x, q.z)); }
      if (dist > F) apVS = std::max(apVS, clampf((hiAhead + 150.f - pos.y) * 0.1f, -1.f, std::max(E.climbPlan, 1.f)));
      // a short field - the book speed's stop over the ground here (the wind along the runway, this air) needing more
      // than 60% of the runway past the aim point - is landed as short as it can be: full flap, at 1.3 times the stall
      // with it (the speed its learned landing distance was flown at). Full flap at the book speed instead flew the
      // Starling onto Meadowbrook nose first, 3 deg down: at 1.6 times that stall the wing lifts with the nose low
      const float kGs = clampf((vel.x * ld.x + vel.z * ld.z) / std::max(ias, 10.f), 0.5f, 2.f);
      const bool shortField = !fail.flapAsym && clampf(a.length * 0.12f, 80.f, 300.f) + 0.2f * (kGs * vref) * (kGs * vref) * (s.taildragger ? 1.35f : 1.f) * (surfaceRough(a.surface) ? 1.1f : 1.f) > 0.6f * a.length;
      const float vBase = shortField ? std::min(vref, 1.3f * E.vs0) : vref;
      bool high = err < -40.f && dist < F + 1000.f;   // above the glideslope: configure early for the drag
      // (configured by height, as a professional is: approach flap outside the gate, more from it, the landing flap set
      // 80 m above the height it is to be stable by; by distance, a fast aircraft was still reconfiguring through 300 m)
      const float hStab = apStabH();
      const bool stabH = apPro && hab < hStab + 80.f;
      if (high) ctl.flaps = 1.f;
      else if (apPro ? !stabH : dist > F * 0.55f) ctl.flaps = dist > F ? 0.34f : 0.67f;
      else {   // landing flap: what leaves a nose-up attitude (~4.5 deg angle of attack) for a main-wheels-first touchdown
        float clReq = mass() * G0 / (0.5f * 1.225f * apSpeed * apSpeed * s.wingArea);
        float fl = s.flapCL > 0.01f ? (clReq - s.CL0 - aeroCLa(aeroModel(s), ias / 340.f) * 4.5f * DEG) / s.flapCL : 0.f;
        // (a short field: all of it, at its own slower speed, above - full flap at the book speed instead, the Wren
        // came down a gusting final nose low and skipped off the runway)
        fl = shortField ? 1.f : clampf(fl, 0.34f, 1.f);
        ctl.flaps = approach(ctl.flaps, fl, 0.3f, dt);
      }
      if (dist < F + 1500.f || high) ctl.gearDown = true;
      // (and slowed on a planned profile: at the final approach speed by the point where the glidepath is 30 m above the
      // height it is to be stable by - a professional is at it from there - and from there back up the glidepath no faster than this airframe
      // slows on it at idle with its drag out, half of that to spare: the speed a slick jet needs to shed takes it
      // kilometres, a trainer's a few hundred metres; stepped by distance, the XR-10 was still slowing through 300 m)
      float vSched = 0.f;
      if (apPro) {
        const float vFinal = vBase * 1.06f, d330 = std::max(hStab + 30.f - gh, 0.f) / std::max(gs, 0.02f);
        const float aDec = clampf(0.5f * G0 * (1.f / std::max(0.6f * glideRatio(), 3.f) - gs), 0.25f, 1.f);
        vSched = std::min(vBase * (dist > F ? 1.3f : 1.18f), sqrtf(vFinal * vFinal + 2.f * aDec * std::max(dist - d330, 0.f)));
      }
      apSpeed = (apPro ? vSched : dist > F ? vBase * 1.3f : dist > F * 0.5f ? vBase * 1.18f : vBase * 1.06f) + apGustAdd;
      // an airframe that can take it (8 g and more: the research craft), with no passengers or fragile load aboard: down
      // the final fast, then the belly-up to shed it (below); a second approach after a go-around too (a go-around's
      // climb resets apBled)
      apBleedT += dt;
      if (E.highAlpha && !apComfort && !apBled && !apPro) {   // (a professional plans the slowing instead: above)
        apSpeed = std::max(apSpeed, vref * 1.7f);
        if (dist < 4000.f && dist > 1800.f && hab > 80.f && fabsf(cross) < 60.f && ias > vref * 1.35f) {
          apStage = APS_BLEED; apStageT = 0; apBleedPhase = 0; apBled = true;
          if (getenv("APDBG")) printf("  belly-up at %.0f m: %.0f m up, ias %.1f (vref %.1f)\n", dist, hab, ias, vref);
          break;
        }
      }
      // (no go-around when it can't climb away - an engine out, overloaded, iced: it lands from what it has)
      if (E.canGoAround && dist < 2000.f && dist > 250.f && (fabsf(cross) > std::min(80.f, std::max(a.width * 0.5f, 12.f) + dist * 0.03f) || err > 40.f || (err < -80.f && apBleedT > 12.f))) {   // (high just after the belly-up: it comes down steeply)
        if (getenv("APDBG")) printf("  go-around at %.0f m: cross %.0f, %.0f m %s the glidepath\n", dist, cross, fabsf(err), err > 0.f ? "below" : "above");
        apStage = APS_GOAROUND; apStageT = 0;
      }
      {   // (inside the gate the planned corridor is clear, and the glidepath may pass a slope near the field closely: one
          // look 800 m ahead; outside it, the 2 km ahead)
        vec3 ahead = pos + vec3(ld.x, 0, ld.z) * 800.f;
        float g = dist > F ? hiAhead : g_world.height(ahead.x, ahead.z);
        if (E.canGoAround && dist > 1200.f && pos.y < g + 40.f) {
          if (getenv("APDBG")) printf("  go-around at %.0f m: ground ahead (%.0f m below, %.0f m above the field)\n", dist, pos.y - g, pos.y - a.elev);
          apStage = APS_GOAROUND; apStageT = 0; } }
      // what can hold itself up on its thrust (the XR-40) comes to a hover over the touchdown point instead of a fast
      // landing roll (after its belly-up, if it does one), starting to slow where it can stop at a gentle 2 m/s^2 - the
      // hover allows twice that - from the ground speed it has, tailwind included
      float gsAl = std::max(vel.x * ld.x + vel.z * ld.z, 0.f);
      if (E.hover && (apBled || !E.highAlpha || apComfort || apPro) && apStage == APS_FINAL && dist < clampf(gsAl * gsAl / 4.f + 200.f, 1700.f, 6000.f) && dist > 0.f && fabsf(cross) < 60.f) { apStage = APS_HOVER; apStageT = 0; apThrI = ctl.throttle; }
      // flare height: enough for this airframe to round out in time (a heavy one answers the elevator slowly, a small
      // highly loaded wing builds its lift slowly)
      float lag = apPathLag();
      float flareH = clampf(std::max(ias * 0.13f, -vel.y * (1.6f + 1.8f * lag)), 4.f, 30.f);
      if (apComfort && ias < 45.f) flareH *= 1.35f;   // (passengers or a fragile load in a slow aircraft: the round-out begins higher - a slow-pitching twin met a gust's sink a few metres up and could not arrest it, 420 fpm: review F2)
      if (hab < flareH && dist < 1500.f) { apStage = APS_FLARE; apStageT = 0; apFlareMin = hab; }
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      apStatus = fmt("APPR  %s  RWY %02d  %.1f km  GS %+.0f m", a.code, rwyN, dist / 1000.f, -err);
      break;
    }
    case APS_FLARE: {
      // steer the ground track onto the centreline (not the heading: a slow aircraft can float for seconds in the flare
      // and a gusting crosswind would carry it off the side faster than a smoothed crab estimate follows), and take
      // most of the crab out only in the last metre
      if (hab > 1.f) {
        float track = len2(vel) > 3.f ? atan2f(vel.x, -vel.z) / DEG : heading();
        float trackT = rwyHdg - clampf(cross * 0.8f, -15.f, 15.f);
        apHeading = heading() + clampf(hdgErrDeg(trackT, track), -20.f, 20.f);
      } else apHeading = rwyHdg - clampf(cross * 0.25f, -8.f, 8.f) - apDrift * 0.3f;
      // the sink rate comes off exponentially as the height does, at a pace set when the flare starts: the height then
      // over the sink then (so it never asks for more sink than it has), no quicker than this airframe can follow
      if (apStageT <= dt * 1.5f) apFlareTau = std::max(clampf(2.2f * apPathLag(), 2.5f, 8.f), hab / std::max(-vel.y, 0.5f));
      // (how far past the aim point it floats: the runway is running out - from 60 m on, a fast jet's ordinary float)
      const float late = clampf((along - 60.f) / 250.f, 0.f, 1.f);
      // (settling, not skimming: a firmer end to the flare for a fast aircraft, whose every second of float is a long
      // way down the runway, and firmer still the longer it floats)
      {
        // (comfort: a gentler last sink, 0.3 m/s for a light twin - a fragile load breaks at 400 fpm, 2 m/s, and a gust
        // adds a metre a second: review F2, F3; the runway running out still puts it down)
        float settleBase = clampf(0.4f + 0.008f * (ias - 40.f), 0.4f, 1.f) * (apComfort ? 0.6f : 1.f);
        float settle = std::min(settleBase + 0.15f * std::max(apStageT - 2.f * apFlareTau, 0.f), 1.f)
                     + 0.8f * clampf(along / 250.f, 0.f, 1.f);   // (floating past the aim point: put it down, runway is running out)
        // (on the height it will have when the airframe has answered: a slow-pitching jet otherwise follows the law a
        // second late, and a second late at the bottom is the sink of a metre higher up)
        float habAhead = std::max(hab + std::min(vel.y, 0.f) * apPathLag(), 0.f);
        float law = -clampf(habAhead / std::max(apFlareTau, 1.f) + settle, 0.4f, 2.5f);
        // after a gust balloons it, more sink is asked for gradually (holding the attitude and letting it settle), not
        // at once: a slow-pitching airframe that noses over to regain it can't round out a second time
        if (apStageT <= dt * 1.5f) apFlareVs = std::max(vel.y, law);
        apFlareVs = law > apFlareVs ? law : std::max(law, apFlareVs - 0.8f * dt);
        apUseVS = true; apVS = apFlareVs;
        // the main wheels first: low down the nose comes up to at least 2.5 deg (a tricycle sits level on its wheels),
        // holding off and letting the speed bleed - with its flaps out at the book speed the Q400 flew onto Far Isle a
        // degree nose down, and its nosewheel met the runway with its mains at 8.8 g
        // (less sink, never a climb: asked to climb a metre up, the Wren skipped off Fjordhaven's runway and flew on)
        // (...until it is past the aim point: there the runway is running out and it settles on - held off at 0.3 m/s
        // with the power on for the passengers, the loaded Q400 skimmed 850 m in ground effect and ran off Meadowbrook)
        if (!s.taildragger && hab < 4.f) apVS = std::min(apVS + clampf((2.5f - pitchDeg()) * 0.3f, 0.f, 1.5f) * clampf((4.f - hab) / 2.f, 0.f, 1.f) * (1.f - late), std::max(apVS, -0.3f - 1.2f * late));
      }
      // (idle from the start of the flare, unless the passengers or a fragile load are aboard: then the speed is held on
      // the power until the wheels are nearly on - a heavy aircraft floating at idle lost 6 m/s below vref, then its lift, and sank
      // back in at 2.4 m/s from a flare that had brought it to 1.3: review F2, F3)
      apSpeed = apComfort && hab > 0.8f && late < 1.f ? vref * (0.97f - 0.12f * late) : 0.f;   // (and less of it past the aim point)
      // (and a balloon - a gust under it 1.5 m above the lowest it had come - gets the power back on to hold the
      // reference speed while it settles: at idle the XR-10 ballooned to 6 m at Fjordhaven, slowed to 1.14 Vs and fell
      // back in at 3.5 m/s with nothing left to flare with)
      // (not after a touchdown, though - a skip back off the runway, apFlareMin < 0 - which is held and settles: with
      // the power on the Wren flew the length of Fjordhaven a metre up in a gusting tailwind)
      if (apFlareMin >= 0.f) {
        apFlareMin = std::min(apFlareMin, hab);
        if (hab > apFlareMin + 1.5f && hab > 1.f && late < 1.f) apSpeed = std::max(apSpeed, vref * (1.f - 0.12f * late));   // (not once it is well past the aim point)
      }
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      apStatus = fmt("FLARE  %s  RWY %02d", a.code, rwyN);
      break;
    }
    case APS_ROLLOUT:
      apSpeed = 0;
      // bounced back into the air: fly it down again (a rollout's controls would leave it to the gusts)
      if (!onGround && hab > 0.6f && apStageT < 8.f) { apStage = APS_FLARE; apStageT = 0; apYawI = 0; apFlareMin = -1.f; break; }   // (the rollout's steering integral is no rudder trim in the air)
      apStatus = fmt("ROLLOUT  %s  %.0f kt", a.code, length(vel) * MS_TO_KT);
      if (length(vel) < 2.5f) {   // stopped: complete only on the runway; past its end the autoland is over but not a success
        float fromThr = along + clampf(a.length * 0.12f, 80.f, 300.f);   // (td sits past the threshold)
        bool onRwy = fromThr >= -5.f && fromThr <= a.length + 5.f && fabsf(cross) <= std::max(a.width * 0.5f, 12.f) + 8.f;
        apDisengage(); apDone = true; apOverrun = !onRwy; ctl.brake = 1; apStatus = onRwy ? "AUTOLAND COMPLETE" : "AUTOLAND  STOPPED PAST THE RUNWAY";
      }
      break;
    case APS_BLEED: {   // the belly-up (flown in apControl): rear up, then nose back down onto the glidepath
      apHeading = rwyHdg - apDrift; apSpeed = 0;
      ctl.gearDown = true;
      // (over at 68 deg, or slow enough - or climbing hard: a conventional airframe, its pull held to the g limit, turns
      // the speed into height instead of drag - the XR-10 zoomed 200 m and had to go round; the fly-by-wire jets snap up
      // fast enough to stall the wing and go flat against the air)
      if (apBleedPhase == 0 && (pitchDeg() > 68.f || ias < vref * 1.1f || vel.y > 18.f || apStageT > 4.f)) apBleedPhase = 1;
      // (back to the final once the nose is down at the glidepath's attitude; too low for any of it, at once)
      if ((apBleedPhase == 1 && pitchDeg() < 4.f) || hab < 35.f || apStageT > 12.f) { apStage = APS_FINAL; apStageT = 0; apBleedT = 0; apXI = 0; apGamI = 0; }
      apStatus = fmt("APPR  %s  RWY %02d  BELLY UP  %.0f kt", a.code, rwyN, ias * MS_TO_KT);
      break;
    }
    case APS_HOVER: {
      float vAl = vel.x * ld.x + vel.z * ld.z;
      apStatus = fmt("VTOL  %s  RWY %02d  %.0f m  %.0f kt", a.code, rwyN, std::max(dist, 0.f), fabsf(vAl) * MS_TO_KT);
      if (onGround) { apStage = APS_ROLLOUT; apStageT = 0; }
      break;
    }
    case APS_GOAROUND:
      apBled = false;   // (the next approach comes down fast and sheds it again)
      apHeading = rwyHdg; apUseVS = true; apVS = std::max(E.climbPlan, 1.f); apSpeed = vref * 1.35f;   // (the climb it can hold now)
      ctl.flaps = 0.34f;
      if (apStageT > 8.f && s.retract && hab > 30.f && vel.y > 0.5f) ctl.gearDown = false;   // (climbing away clear of the ground: from a balked flare a heavy one sinks back first, and met the runway on its belly)
      // (back to NAV only clear of the ground all round: its turns would otherwise put it into the slope it climbed from)
      if (pos.y > std::max(a.elev + 450.f, apHoldAlt - 30.f) && pos.y > terrainAround() + 200.f) { apStage = APS_NAV; apLeg = 0; apStageT = 0; apTurnDir = 0; }
      apStatus = fmt("GO AROUND  %s", a.code);
      break;
  }
  // the chart's edge: en route or going around (a slow climb straight out past the runway runs a long way), beyond where
  // any plan reaches it turns back for the field (the review of v3.33.0, A1: a passenger Starling flew off the chart
  // from Palm Bay and was lost. The map wraps now, but the plan's patterns stay on this side of its seam)
  if ((apStage == APS_NAV || apStage == APS_GOAROUND) && std::max(fabsf(pos.x), fabsf(pos.z)) > WORLD_HALF * 1.12f)
    apHeading = atan2f(nearCopy(apTd.x, pos.x) - pos.x, -(nearCopy(apTd.z, pos.z) - pos.z)) / DEG;
  // terrain safety while en route and in the go-around: never let the target sit below the ground ahead
  if (apStage == APS_NAV || apStage == APS_GOAROUND) {
    const float hi = terrainAround();
    if (!apUseVS) apAlt = std::max(apAlt, hi + 250.f);
    else if (pos.y < hi + 200.f) apVS = std::max(apVS, 5.f);
  }
}

// the highest ground the autopilot must keep clear of: under it, along its track for 3.6 km, and all round (in a steep
// orbit the velocity points off the circle, the ground that matters is what it turns over)
float Plane::terrainAround() const {
  vec3 f = len2(vel) > 5.f ? vel * (1.f / len2(vel)) : forward();
  float hi = g_world.height(pos.x, pos.z);
  for (int i = 1; i <= 6; i++) { vec3 p = pos + vec3(f.x, 0, f.z) * (i * 600.f); hi = std::max(hi, g_world.height(p.x, p.z)); }
  for (int i = 0; i < 8; i++) { float a = i * PI / 4; for (float r : {700.f, 1500.f}) hi = std::max(hi, g_world.height(pos.x + cosf(a) * r, pos.z + sinf(a) * r)); }
  return hi;
}

// how quickly this airframe's pitch answers at this speed (s): the learned time to the pitch-rate peak at cruise, slower
// in proportion as the speed falls
static float pitchLag(const PerfModel& P, const AircraftSpec& s, float ias) { return P.tQ * clampf(s.cruise / std::max(ias, 15.f), 0.6f, 2.5f); }
// A professional's envelope for this airframe (docs/PILOT.md): an airliner's pilot flies 25 deg of bank and 1.3 g, a test
// pilot in a jet built for 9 g or 40 g manoeuvres at more - smoothly still. 0 for the civil types (5.8 g usable) up to 1
// at 40 g, on the ratio's logarithm: the XR-10 (9 g) 0.23, the XR-20 (14 g) 0.46, the XR-30 and XR-40 1
float Plane::apProScale() const { return clampf(logf(std::max(perf(spec).gUse, 1.f) / 5.8f) / logf(40.f / 5.8f), 0.f, 1.f); }
float Plane::apProG() const { return 1.3f + 1.2f * apProScale(); }      // normal manoeuvring's load factor: 1.3 g .. 2.5 g
float Plane::apProBank() const { return 30.f + 30.f * apProScale(); }   // and its bank: 30 .. 60 deg (an airline's 25 to 30; at 25 an airliner's pattern ran 12 km out)
float Plane::apAltGain() const { return apPro ? std::min(0.15f, 0.22f * apPathGain(true)) : std::min(0.15f, 0.25f / apPitchLag()); }
// how fast the flight-path loop may close (1/s): no faster than this airframe's pitch answers, and - a professional's -
// no faster than the time its path takes to follow the nose allows (the gain times that lag at most 1: apControl damps
// what the lag would leave, with the path's own rate). A heavy airliner's path follows its nose seconds late: its loop
// closed as fast as a trainer's, the Meridian porpoised down its final at 17 s, -12 to +5 m/s, in calm air
float Plane::apPathGain(bool approach) const {
  const float k = std::min(approach ? 0.8f : 1.6f, 1.f / apPitchLag());
  return apPro ? std::min(k, 1.f / apPathLag()) : k;
}
float Plane::apPitchLag() const { return pitchLag(perf(spec), *spec, ias); }
// The flight path follows the nose only as the wing builds the lift: in 2m / (rho V S CLa) - the heavier, the higher
// and the slower, the longer. A light trainer's is shorter than its pitch lag; a research craft's, its small, highly
// loaded wing at its approach speed, two to three times longer (the XR-20 and XR-30 rounded out half as fast as their
// pitch answers and met the runway at 3 m/s)
float Plane::apPathLag() const {
  const AircraftSpec& s = *spec;
  const float V = std::max(length(vel), 15.f);
  const float tth = 2.f * mass() / (std::max(density, 0.1f) * V * s.wingArea * std::max(aeroCLa(aeroModel(s), ias / 340.f), 1.f));
  return std::max(apPitchLag(), tth);
}

// The inner loops every autopilot mode flies through: a pitch rate and a roll rate to hold, tracked with stick and
// aileron scaled by this airframe's learned power at this speed, the elevator kept inside the load factors given, the
// rudder coordinating (the yaw rate that the sideways part of gravity calls for, so it holds upside down and through
// the vertical too) and damping the sideslip.
void Plane::apRates(float qT, float pT, float rollCap, float nzMin, float nzMax, float dt) {
  const AircraftSpec& s = *spec;
  const PerfModel& P = perf(spec);
  const bool fbw = apEnv.rateCmd;   // (its flight controls take rate commands)
  float V = std::max(ias, 15.f), spd = std::max(length(vel), 1.f);
  // roll
  float pRate = -w.z;
  if (fbw) ctl.roll = clampf(pT / fbwRollMax(0.f), -1.f, 1.f);
  else {
    apRollI = clampf(apRollI + (pT - pRate) / std::max(rollCap, 0.1f) * 1.5f * dt, -0.5f, 0.5f);
    ctl.roll = clampf(pT / std::max(rollCap, 0.1f) + apRollI + (pT - pRate) * 0.3f / std::max(rollCap, 0.1f), -1.f, 1.f);
  }
  // pitch: the rate, tracked with stick scaled by the learned pitch power here - and by the weight: the same stick
  // trims the same angle of attack, which pulls fewer g (and bends the path more slowly) the heavier it is. (Taken
  // as learned, the full Q400's g limiter held it to a fifth of a g and it porpoised about the glidepath into the sea)
  float tQv = pitchLag(P, s, V);
  const float loadK = clampf(apEnv.wRatio, 0.5f, 3.f);
  float qPS = std::max(P.qPerStick * clampf(V / s.cruise, 0.3f, 2.f) / loadK, 0.05f);   // rad/s per unit stick here
  float eq = qT - w.x;
  apPitchI = clampf(apPitchI + eq / qPS * dt * 3.f / tQv, -1.f, 1.f);
  ctl.pitch = clampf(apPitchI + eq * 3.f / qPS, -1.f, 1.f);
  float sens = std::max(P.gPerStick * (V / s.cruise) * (V / s.cruise) / loadK, 0.25f);   // g per unit stick here
  // envelope: the stick that holds 1 g here (learned on the fly), and no more stick either side of it than reaches the
  // g limits (sens is the step's peak response, so this keeps short of them even through the overshoot)
  apTrimEst = approach(apTrimEst, ctl.pitch - (gLoad - 1.f) / sens, 1.2f, dt);
  float sHi = apTrimEst + (nzMax - 1.f) / sens, sLo = apTrimEst + (nzMin - 1.f) / sens;
  if (ctl.pitch > sHi) { ctl.pitch = sHi; apPitchI = std::min(apPitchI, sHi); }
  if (ctl.pitch < sLo) { ctl.pitch = sLo; apPitchI = std::max(apPitchI, sLo); }
  ctl.pitch = clampf(ctl.pitch, -1.f, 1.f);
  if (fbw) ctl.pitch = clampf((qT - ctl.trim * 0.15f) / fbwPitchMax(V) + apPitchI * 0.2f, -1.f, 1.f);   // rate command
  // yaw: coordinated (the research jets' rate-command yaw holds zero rate unless fed the turn), sideslip damped
  // (the coordinated body yaw rate is g times the sideways tilt of the body over the speed; w.y < 0 yawing right)
  float tilt = -q.rotate(vec3(1, 0, 0)).y;                                      // sin(bank) cos(pitch), any attitude
  float qn = clampf((s.cruise * s.cruise) / (V * V), 0.3f, 3.f);
  float rErr = w.y + G0 * tilt / spd;
  // (and held in: what a steady yaw needs - a dead engine's thrust, the slipstream's swirl at climb power - builds up
  // as rudder trim, so the sideslip is flown out instead of standing at what the sideslip term alone would answer)
  if (!fbw) apYawI = clampf(apYawI + beta * qn * dt * 1.5f, -0.8f, 0.8f);
  ctl.yaw = fbw ? clampf(G0 * tilt / spd / 1.4f, -1.f, 1.f) : clampf(beta * 2.f + rErr * 1.5f * qn + apYawI, -1.f, 1.f);
}

// The belly-up: throttle closed, wings level, the nose pulled up as hard as the structure takes to ~70 deg - the whole
// underside square to the airflow, a barn door of drag - and then pushed back down, unloaded, to the glidepath's
// attitude. A way to shed 70 knots in a few seconds for an airframe built for it (apEnv.highAlpha: the research craft;
// the others slow down on the throttle).
void Plane::apBellyUp(float dt) {
  const PerfModel& P = perf(spec);
  const float spd = std::max(length(vel), 1.f), V = std::max(ias, 15.f);
  const float nzMax = std::max(P.gLimit * 0.85f, 1.5f), nzMin = std::max(P.gNeg * 0.8f, -1.5f);
  const float pitch = pitchDeg(), bank = bankDeg();
  float rollCap = (apEnv.rateCmd ? fbwRollMax(0.f) : P.rollRate * clampf(V / spec->cruise, 0.25f, 2.f)) * 0.95f;
  const float pT = clampf(-bank * DEG * 3.f, -rollCap, rollCap);
  float qT;
  if (apBleedPhase == 0) qT = std::min(G0 * nzMax / spd, 1.6f);            // rear up: all the pitch rate the g allows
  else qT = clampf((2.f - pitch) * DEG * 1.2f, -1.4f, 0.2f);               // nose back down to the horizon, unloaded
  apRates(qT, pT, rollCap, nzMin, nzMax, dt);
  ctl.throttle = 0.f; apThrI = 0.f;
  if (apEnv.flaps) ctl.flaps = 0.f;
}

void Plane::apControl(float dt) {
  const AircraftSpec& s = *spec;
  const ApEnvelope& E = apEnv;
  const bool fbw = E.rateCmd;
  float V = std::max(ias, 15.f);
  // ground rollout after an autoland: centreline with rudder / nosewheel, nose down, brakes, idle
  if (apMode == AP_APPR && apStage == APS_ROLLOUT) {
    vec3 rr(-apLd.z, 0, apLd.x), rel = pos - apTd;
    float cross = rel.x * rr.x + rel.z * rr.z, he = hdgErrDeg(atan2f(apLd.x, -apLd.z) / DEG, heading());
    ctl.throttle = 0;
    // (a taildragger: the stick a little back while it could still fly, then all the way back to hold the tail down)
    // (the nose down only on the wheels: a skip back off the runway - a jet with full flap still flying at 57 m/s - is
    // held in its attitude while the flaps come up under it; pushed over in the air, the Starling came back down on its
    // nosewheel at 9 g)
    apGroundT = apStageT <= dt * 1.5f || !onGround ? 0.f : apGroundT + dt;
    ctl.pitch = s.taildragger ? (ias > E.vApp * 0.8f ? 0.15f : 1.f) : (apGroundT > 1.2f ? -0.1f : 0.f);
    // wings level with everything the ailerons have (a gust under the upwind wing at 60 kt still lifts it), damped
    ctl.roll = clampf(-bankDeg() * 0.18f + w.z * 0.6f, -1.f, 1.f);
    // (back to the centreline no quicker than over 3 s of the roll: at a research jet's 65 m/s a fixed 0.6 deg a metre
    // swung it from 10 m one side to 22 m the other and off the edge at Far Isle)
    float off = std::min(0.6f * fabsf(cross), atanf(fabsf(cross) / (std::max(length(vel), 8.f) * 3.f)) / DEG * 1.2f);
    he = hdgErrDeg(atan2f(apLd.x, -apLd.z) / DEG - copysignf(std::min(off, 10.f), cross), heading());
    // (and held in: a crosswind weathervanes it into the wind with a moment that grows as fast as the rudder's does,
    // so the rudder that holds the runway heading builds up as an integral - proportional alone stood 3.5 deg off
    // the runway at 60 m/s and the Starling slid 20 m sideways before the nosewheel could steer)
    if (apStageT <= dt * 1.5f) apYawI = 0;
    apYawI = clampf(apYawI + he * 0.04f * dt, -0.8f, 0.8f);
    ctl.yaw = clampf(he * 0.08f + w.y / DEG * 0.12f + apYawI, -1.f, 1.f);   // steer for the centreline, damp the yaw rate
    ctl.brake = clampf((apStageT - 0.6f) * 1.2f, 0.f, s.taildragger ? 0.55f : 1.f);
    // a taildragger's mains stand only a little ahead of its centre of gravity: braked hard with the tail up it goes
    // over onto its propeller. The brakes come in as the tail comes down, and let go the moment the nose starts down
    if (s.taildragger) ctl.brake *= smoothstepf(4.f, 9.f, pitchDeg()) * (w.x < -0.02f ? 0.f : 1.f);
    // flaps up once it is down to stay: the wing stops carrying the weight and the brakes get their grip (at 64 m/s the
    // Starling's full flaps held it to 1.5 m/s^2 of braking for its first ten seconds, and it ran off Meadowbrook's end)
    if (apStageT > 1.f) ctl.flaps = 0.f;
    return;
  }
  if (apMode == AP_APPR && apStage == APS_HOVER) { apHover(dt); return; }
  if (onGround) return;
  if (apMode == AP_STUNT && apStuntFly(dt)) return;
  if (apMode == AP_APPR && apStage == APS_BLEED) { apBellyUp(dt); return; }
  // The pilot flies to the edge of what this aircraft can do (its learned envelope, Plane::perf): the load factor it
  // commands is bounded only by the structure (less what the gusts can add) and by the stall at this speed, the
  // bank by that load factor, the roll rate by the airframe's own. Close to the ground on an approach it stays sane:
  // that's where a wingtip is lost.
  const PerfModel& P = perf(spec);
  float spd = std::max(length(vel), 1.f);
  bool appr = apMode == AP_APPR && apStage >= APS_FINAL && apStage != APS_GOAROUND;
  bool flare = apMode == AP_APPR && apStage == APS_FLARE;
  float gustG = std::max(0.6f, P.gLimit * 0.05f);   // what a gust can add on top of a commanded pull
  float vsFl = E.vs1 + (E.vs0 - E.vs1) * clampf(std::min(flaps, flapLeft()), 0.f, 1.f);   // stall speed now at this flap setting (weight, ice; the lesser of a split pair)
  float stallG = (V / vsFl) * (V / vsFl) * 0.9f;
  float nzMax = std::min(P.gLimit - gustG - P.gLimit * 0.03f, stallG), nzMin = std::max(P.gNeg + gustG + 0.3f, -stallG * 0.5f);
  nzMax = std::max(nzMax, 1.05f);
  // the flare never pushes: after a balloon it holds the attitude and lets the wing settle it (an airframe whose lift
  // follows its nose a second late, pushed to 0.75 g, dropped its nose 5 deg and sank onto the runway at 3.3 m/s: the
  // XR-10 at Cedar Ridge)
  if (flare) nzMin = std::max(nzMin, 0.9f);
  // comfort (career flights): what a passenger or a fragile load accepts - the autopilot only, never the stick
  // (commanded inside 0.85..1.25 g so the response - which overshoots a little - stays within 0.8..1.3)
  // (but a stall or an upset comes first: near the stall, pitched far up or down or banked steeply, the whole envelope
  // is flown until it is well clear again - a 0.85 g floor in a steep, slowing climb held the nose up until the
  // Bushmaster went over the top at 170 deg of bank: review F1)
  const float pd = pitchDeg(), bd = fabsf(bankDeg());
  // (and the chart's edge, for a professional: a turn at its bank that would carry it past the edge - a fast jet's at its
  // speed is kilometres wide - is flown at its sharpest, 2.5 g and 60 deg of bank, not the whole airframe (the XR-20's
  // 14 g is for emergencies): the XR-20 started near Fjordhaven's corner of the chart and was off it in 40 s. Heading
  // out: where it will be in 20 s past the edge, and getting further out - a corner field's own approaches run close to
  // the edge, and flying along it or back in is no reason to turn harder)
  bool edge = false;
  if (apPro && !appr && !onGround) {
    const vec3 ahead = pos + vec3(vel.x, 0, vel.z) * 20.f;
    edge = std::max(fabsf(ahead.x), fabsf(ahead.z)) > WORLD_HALF * 1.05f && std::max(fabsf(ahead.x), fabsf(ahead.z)) > std::max(fabsf(pos.x), fabsf(pos.z));
  }
  apEdge = edge;
  const float proK = edge ? 1.f : apProScale();
  float proG = 1.3f + 1.2f * proK, proBank = 30.f + 30.f * proK;   // (apProG / apProBank, or at the chart's edge their sharpest)
  if (edge) {
    // (and if even that turn is wider than the room left before the chart is lost - an XR-40 at 375 m/s turns 8 km wide
    // at 2.5 g, and from the chart's eastern edge was off it in 45 s - as tight as the room needs, up to 80 deg)
    const float room = WORLD_HALF * 1.15f - std::max(fabsf(pos.x), fabsf(pos.z)), rTurn = std::max(room * 0.7f, 500.f);
    const float tb = spd * spd / (rTurn * G0), nzEdge = std::min(sqrtf(1.f + tb * tb), nzMax);
    if (nzEdge > proG) { proG = nzEdge; proBank = std::min(acosf(1.f / nzEdge) / DEG, 80.f); }
  }
  if (!apPro) {
    if (!flare && (ias < vsFl * 1.08f || fabsf(pd) > 25.f || bd > 45.f)) apUpset = true;
    else if (apUpset && (flare || (ias > vsFl * 1.3f && fabsf(pd) < 12.f && bd < 25.f))) apUpset = false;
  } else {
    // (a professional's upset is beyond what it flies on purpose: 15 deg past the bank it has been allowed - which comes
    // down slowly, so rolling out of the edge's 60 deg to an XR-20's 41 is no upset - and over once it is back inside it.
    // At 41 deg the XR-20 had come back off the edge in a 60 deg turn, been called upset, and pulled 11 g)
    const float bankOk = std::max(apBankOk, apComfort ? 25.f : proBank);
    if (!flare && (ias < vsFl * 1.08f || fabsf(pd) > 25.f || bd > std::max(45.f, bankOk + 15.f))) apUpset = true;
    else if (apUpset && (flare || (ias > vsFl * 1.3f && fabsf(pd) < 12.f && bd < std::max(25.f, bankOk + 5.f)))) apUpset = false;
  }
  // the ground ahead, as a ground-proximity warning sees it: where the path it is on now - straight on, at its climb or
  // descent - comes within 60 m of the terrain in the next 25 s (the final approach aside: there the path meets the
  // runway by design)
  // (a professional's looks along the path it is on - the turn it is in, not a straight line: at 60 deg of bank and 370
  // m/s the XR-40 turned into a ridge its straight line missed. It climbs over what is ahead on it as a matter of
  // course, within its own envelope, and calls an escape only when the climb it makes at its own manoeuvring g would
  // not clear it. Then it rolls wings level and pulls as firmly as clears it, not with the airframe's all: a fast jet's
  // 25 s reach several km, and the whole envelope for every slope out there pulled the XR-40 to 34 g)
  const bool wasEscape = apEscape;
  apEscape = false;
  float escG = nzMax, terrVs = -1e9f;
  if (!appr && !onGround) {
    if (!apPro) for (int i = 1; i <= 5 && !apEscape; i++) {
      const vec3 qa = pos + vel * (i * 5.f);
      apEscape = qa.y < g_world.height(qa.x, qa.z) + 60.f;
    } else {
      const float vh = std::max(sqrtf(vel.x * vel.x + vel.z * vel.z), 1.f), psi = atan2f(vel.x, -vel.z);
      const float bnk = clampf(bankDeg(), -80.f, 80.f) * DEG, w = G0 * tanf(bnk) / vh;   // (the turn it is in, rad/s)
      const float tRoll = fabsf(bnk) / ((15.f + 30.f * apProScale()) * DEG);              // (and the time to roll out of it)
      const float climbMax = std::max(apEnv.climb * 1.5f, spd * 0.34f);                    // (a zoom: 20 deg, trading speed)
      auto along = [&](float t, float tArc) {   // where it is in t s: turning for tArc s, then straight on
        const float ta = std::min(t, tArc), ps = psi + w * ta;
        vec3 q = pos;
        if (fabsf(w) < 1e-4f) q = q + vec3(sinf(psi), 0, -cosf(psi)) * (vh * ta);
        else q = q + vec3(cosf(psi) - cosf(ps), 0, sinf(psi) - sinf(ps)) * (vh / w);
        return q + vec3(sinf(ps), 0, -cosf(ps)) * (vh * (t - ta));
      };
      // the path at a pull of nz up to a climb of vyCap - still in its turn, or rolled out first - and whether it clears
      // the ground by the margin; and the climb rate that would put it over every point of it in time
      auto clears = [&](float nz, bool roll, float margin, float vyCap, float* need) {
        float y = pos.y, vy = vel.y; bool ok = true;
        for (int i = 1; i <= 50; i++) {
          const float t = i * 0.5f, aUp = std::max(0.f, (roll && t > tRoll ? nz : nz * cosf(bnk)) - 1.f) * G0;
          vy = std::min(vy + aUp * 0.5f, std::max(vyCap, vel.y)); y += vy * 0.5f;
          if (i % 5) continue;
          const vec3 q = along(t, roll ? tRoll : 1e9f);
          const float hq = std::max(g_world.height(q.x, q.z), 0.f);   // (the sea's surface, not its bed)
          if (y < hq + margin) ok = false;
          if (need) *need = std::max(*need, (hq + margin + 40.f - pos.y) / t);
          else if (!ok) return false;
        }
        return ok;
      };
      clears(1.f, false, 250.f, vel.y, &terrVs);   // (the climb the ground ahead on its path asks for: kept 250 m and more above it, as the route is)
      // (an escape once climbing away holds until the path rolled out at its own g clears by 120 m, and for 3 s at least:
      // on and off each step, the pull came and went)
      const float vyPro = std::max(vel.y, std::min(terrVs, climbMax));
      if (wasEscape ? apEscT < 3.f || !clears(apProG(), true, 120.f, vyPro, nullptr) : !clears(apProG(), false, 60.f, vyPro, nullptr)) {
        apEscape = true;
        for (float m : {2.f, 3.f, 5.f, 8.f}) {
          const float gk = 1.f + (apProG() - 1.f) * m;
          if (gk >= nzMax) break;
          if (clears(gk, true, 60.f, climbMax, nullptr)) { escG = gk * 1.2f; break; }   // (a fifth to spare: the pull takes a moment to come on)
        }
        float need = 0.f; clears(escG, true, 60.f, climbMax, &need);
        terrVs = std::max(terrVs, need);
      }
      terrVs = std::min(terrVs, climbMax);
    }
  }
  apEscT = apEscape ? apEscT + dt : 0.f;
  // a professional's envelope (docs/PILOT.md): what the job needs, not what the airframe takes - 0.75 to 1.3 g, about
  // 25 deg of bank, rolled at 8 deg/s; gentler still with passengers or a fragile load. A recovery and the ground ahead
  // get more g: the ground as much as clears it, a recovery a g more - but its bank stays its own (the whole envelope's
  // 85 deg and 11 g was a turn, not a recovery)
  const bool proAny = apPro && !flare, pro = proAny && !apUpset && !apEscape;
  // (short final, below 100 m: firm hands - a downdraft there is answered with what it takes, 0.6 to 1.6 g, quickly; in
  // the storm the gentle envelope let the sink build to 5-8 m/s into the flare)
  const bool shortFinal = appr && agl() < 100.f;
  if (pro) {
    nzMax = std::min(nzMax, shortFinal ? std::max(proG, 1.6f) : apComfort ? 1.25f : proG);
    nzMin = std::max(nzMin, shortFinal ? 0.6f : apComfort ? 0.85f : 0.75f);
  }
  else if (proAny && apEscape) { nzMax = std::min(nzMax, std::max(escG, proG)); nzMin = std::max(nzMin, 0.25f); }
  else if (proAny) { nzMax = std::min(nzMax, proG + 1.f); nzMin = std::max(nzMin, 0.25f); }   // (recovering: unloaded, never pushed negative)
  else if (apComfort && !flare) {
    if (!apUpset && !apPro) { nzMax = std::min(nzMax, 1.25f); nzMin = std::max(nzMin, 0.85f); }
    else if (apUpset) nzMin = std::max(nzMin, 0.25f);   // (recovering: unloaded, never pushed negative - a fragile load breaks below 0 g)
  }
  // lateral: heading error -> turn rate -> bank -> roll rate -> aileron
  // (steer the direction the aircraft is moving through the air, heading plus sideslip: the nose swings with every
  // yaw oscillation, chasing it feeds a Dutch roll)
  float herr = hdgErrDeg(apHeading, heading() + (fbw ? 0.f : beta / DEG));
  if (fabsf(herr) > 165.f && fabsf(bankDeg()) > 5.f) herr = copysignf(fabsf(herr), bankDeg());   // a reversal: keep the way it's banked
  else if (fabsf(herr) > 178.f) herr = 178.f;                                                      // (exactly behind: pick right)
  // (the steepest bank that still leaves load factor to spare for climbing: a quarter more than level flight needs, half
  // more when well below the altitude it wants)
  float reserve = !apUseVS && apAlt - pos.y > 80.f ? 1.5f : 1.25f;
  if (apComfort) reserve = std::min(reserve, 1.12f);   // (a 25 deg bank needs 1.1 g: the comfort ceiling leaves little to climb with)
  // (a professional's: 0.05 g to spare - and more, up to half a g, as it falls below the height it wants or sinks when it
  // shouldn't: the height comes before the turn. At 60 deg and 2.5 g the XR-40 turned on down from 955 m to 113 m with
  // its target at 800)
  const float lowBy = apUseVS ? 0.f : (apAlt - pos.y) + std::max(0.f, -vel.y) * 5.f;
  const float proReserve = 1.05f + 0.45f * smoothstepf(20.f, 150.f, lowBy);
  float bankMax = acosf(std::min(0.99f, (proAny ? proReserve : reserve) / std::max(nzMax, 1.01f))) / DEG;
  if (appr) bankMax = std::min(bankMax, agl() < 150.f ? (pro ? 15.f : 20.f) : (pro ? std::min(apProBank(), 30.f) : 35.f));
  bankMax = clampf(bankMax, 10.f, 85.f);
  if (proAny && !appr) bankMax = std::min(bankMax, apComfort ? 25.f : apEscape ? 10.f : proBank);   // (the ground ahead: wings level, and pull)
  else if (apComfort && !apPro && !appr) bankMax = std::min(bankMax, 25.f);   // (the final approach keeps its own)
  if (apPro) apBankOk = std::max(bankMax, apBankOk - 5.f * dt);           // (the bank it has been allowed, coming down at 5 deg/s)
  float maxTurn = G0 * tanf(bankMax * DEG) / spd;                       // rad/s
  float turnT = clampf(herr * DEG * std::min(appr ? 0.25f : 0.6f, 0.25f / P.tRoll), -maxTurn, maxTurn);
  float bankT = clampf(atanf(turnT * spd / G0) / DEG, -bankMax, bankMax);
  if (flare) bankT = clampf(bankT, -4.f, 4.f);
  float bank = bankDeg();
  float rollCap = (fbw ? fbwRollMax(0.f) : P.rollRate * clampf(V / s.cruise, 0.25f, 2.f)) * 0.95f;
  rollCap *= clampf(2.f / std::max(gLoad, 0.5f), 0.35f, 1.f);         // unload, roll, pull: no full-rate rolls under load
  // (rolled in and out smoothly: 8 deg/s, 6 with passengers - low on the final, 15 for the gusts that roll it)
  if (pro) rollCap = std::min(rollCap, (appr && agl() < 150.f ? 15.f : apComfort ? 6.f : 8.f + 22.f * proK) * DEG);
  else if (proAny) rollCap = std::min(rollCap, (15.f + 30.f * proK) * DEG);   // (a recovery rolls briskly, still not at the stops)
  else if (apComfort && !apUpset) rollCap = std::min(rollCap, 15.f * DEG);
  float kBank = std::min(appr ? 1.5f : 3.f, 0.7f / P.tRoll);           // bank loop no faster than the roll mode
  float pT = clampf((bankT - bank) * DEG * kBank, -rollCap, rollCap);
  if (!E.flaps) ctl.flaps = 0;   // no flaps to fly with (the XR-40's lever tilts its pods): keep it up
  // a flap that stopped: the lever back to where it is, so the other one matches it and the wings stay level
  else if (fail.flapAsym) ctl.flaps = clampf(fail.flapAt / std::max(aeroGeom(s).flapMax, 0.05f), 0.f, 1.f);
  // vertical: altitude -> climb rate -> flight path -> load factor -> elevator
  // (the climb it can make now: heavy, high, iced or an engine out, less - and with none to spare it holds what it can)
  float vsUp = std::max(E.climb * 1.1f, 1.f), vsDn = std::max(spd * 0.42f, vsUp);   // dives up to ~25 deg
  if (apComfort && !apUpset) { vsUp = std::min(vsUp, std::max(0.6f * E.climb, 1.f)); vsDn = std::min(vsDn, std::max(spd * 0.1f, 4.f)); }
  else if (pro) vsDn = std::min(vsDn, std::max(spd * 0.12f, 3.5f));   // (a professional's descent: up to 7 deg, 1,000 fpm at a trainer's speed, 2,400 at an airliner's)
  float tQv = pitchLag(P, s, V);                                        // (the airframe answers slower at low speed)
  float kAlt = apAltGain();                                            // outer loops slower than the g loop
  float vsT = apUseVS ? apVS : clampf((apAlt - pos.y) * kAlt, -vsDn, pro ? vsUp : vsUp * 3.f);   // (a professional climbs at the climb the aircraft holds, not a zoom)
  if (apPro) vsT = std::max(vsT, terrVs);   // (the climb the ground ahead on its path needs)
  if (!flare) {
    // energy: climb no harder than the power holds the target speed; below 1.15 Vs put the nose down for speed
    // (and never slower than the best-climb speed Vy while climbing: below it the climb only gets worse, and with the
    // throttle in the pilot's hands that's where it would end up, behind the power curve)
    float vClimb = apSpeed > 0 ? std::min(apSpeed - 3.f, P.vy * 0.95f) : P.vy * 0.95f;
    if (vsT > 0 && (apSpeed <= 0 || ctl.throttle > 0.97f) && ias < vClimb) vsT = std::min(vsT, std::max(0.f, vel.y - (vClimb - ias) * 0.5f));
    float floorV = vsFl * 1.15f;
    if (ias < floorV) vsT = std::min(vsT, (ias - floorV) * 2.f);
  }
  float gT = asinf(clampf(vsT / spd, -0.95f, 0.95f)), g = asinf(clampf(vel.y / spd, -1.f, 1.f));
  // the energy law (TECS, docs/PILOT.md), flying a speed on the autothrottle: the throttle closes the error in the total
  // energy's rate - the flight path's and the acceleration's together - and the elevator its distribution between them,
  // so a speed short of the target with the throttle at its stop is made good with the nose, and a climb is flown at the
  // speed the power holds. The speed and its rate come from the inertial acceleration along the path, the airspeed only
  // correcting their slow drift: a gust moves the airspeed without the aircraft speeding up, and isn't chased
  const bool tecs = proAny && apSpeed > 0.f && apMode != AP_STUNT;   // (a recovery included: switching laws jolted the throttle)
  float eD = gT - g, eE = 0.f;
  {
    const float kIas = sqrtf(std::max(density, 0.05f) / 1.225f);
    const float aAl = dot((vel - apVelPrev) * (1.f / std::max(dt, 1e-4f)), vel * (1.f / spd)) * kIas;
    const float tauV = 3.f;
    if (apSpdEst <= 0.f) apSpdEst = ias;
    apSpdEst += (aAl + (ias - apSpdEst) / tauV) * dt;
    if (tecs) {
      const float vdot = aAl + (ias - apSpdEst) / tauV;
      // (back onto the speed over ~8 s, no harder than 0.06 g - or, in a jet built for it, up to 0.2 g: at 0.06 g the XR-40
      // took eight minutes and 150 km to come down from 400 m/s to its terminal speed, and flew off the chart)
      const float aLim = 0.6f + 1.4f * apProScale();
      // (a gust's gain is let pass, a loss is answered at once, as a pilot does: filtered both ways, a storm's shear took
      // 9 m/s off the Islander 20 m up before the power came, and it flared from a 5 m/s sink)
      const float vMeas = std::min(apSpdEst, ias);
      const float vdotT = clampf((apSpeed - vMeas) * 0.12f, -aLim, aLim);
      eE = (gT - g) + (vdotT - vdot) / G0;
      // (the path first: the elevator weighs the speed lightly while the throttle can still answer it, fully once the
      // throttle is at a stop - unweighted, the nose chased every slowing the engines were still answering, and a calm
      // final porpoised at 10 s)
      // (at a stop: the throttle the energy law asks for beyond its range - asked below idle or above full, not merely
      // near them; a light aircraft descends its final a little above idle, and that is the throttle answering)
      // (on the final the path comes first: fast with the throttle closed it stays on the glidepath and the drag comes out -
      // never up off it to slow; short of power the nose still keeps the speed)
      const float sat = std::max(smoothstepf(1.f, 1.15f, apThrDemand), appr ? 0.f : smoothstepf(0.f, -0.15f, apThrDemand));
      eD = (gT - g) - (0.25f + 0.75f * sat) * (vdotT - vdot) / G0;
    }
  }
  apVelPrev = vel;
  // flight path: proportional plus integral (at 1 g any climb angle is an equilibrium, so the error alone won't close it)
  float kGam = flare ? std::min(1.2f, 2.f / tQv) : apPathGain(appr);   // (the flare wants the path bent now)
  // (held through the flare: a few seconds of rounding out from the approach sink would wind it up, and at a fast
  // jet's speed a full integral is 0.4 g - enough to hold it level metres up, then drop it in) - but not a push: wound
  // down chasing the glidepath through a final's porpoising, it took half the pull out of the round-out, and a loaded
  // Starling met the runway at 3.6 m/s from a flare that asked for 0.4 (review F3)
  float gDot;
  if (!proAny) {
    if (!flare) apGamI = clampf(apGamI + eD * kGam * 0.35f * dt, -0.05f, 0.05f);
    else apGamI = std::max(apGamI, 0.f);
    gDot = clampf(eD * kGam + apGamI, -1.5f, 1.5f);
  } else {
    // (damped with the path's own rate - from the g it is pulling, so with none of the lag between the nose and the path:
    // the loop's gain times the path's lag of it. Without it a heavy airframe's path overshot what the nose had asked
    // for and porpoised; with it the Meridian's and the Starling's finals hold the path to a metre)
    const float gRate = G0 * (gLoad * cosf(clampf(bankDeg(), -85.f, 85.f) * DEG) - cosf(g)) / spd;
    gDot = clampf(eD * kGam + apGamI - apPathLag() * kGam * gRate, -1.5f, 1.5f);
    // (a professional's anti-windup: the integral grows only while the load factor it asks for is inside the limits)
    const float nzWould = (cosf(g) + spd / G0 * gDot) / std::max(cosf(clampf(bankDeg(), -85.f, 85.f) * DEG), 0.1f);
    if ((nzWould <= nzMax || eD <= 0.f) && (nzWould >= nzMin || eD >= 0.f)) apGamI = clampf(apGamI + eD * kGam * 0.35f * dt, -0.05f, 0.05f);
  }
  float cb = cosf(clampf(bank, -85.f, 85.f) * DEG);
  float nzT = clampf((cosf(g) + spd / G0 * gDot) / std::max(cb, 0.1f), nzMin, nzMax);
  // (a professional's g comes on and off smoothly: 0.8 g/s; a recovery, the ground ahead and the flare take it at once)
  apNzCmd = pro ? approach(apNzCmd, nzT, shortFinal ? 3.f : 0.8f, dt) : nzT;
  nzT = apNzCmd;
  // a big bank change (reversing a turn) is flown unloaded: rolling hard while pulling hard couples into pitch
  float bankErr = fabsf(bankT - bank);
  if (bankErr > 25.f) nzT = std::min(nzT, 1.f + (nzT - 1.f) * clampf((60.f - bankErr) / 35.f, 0.15f, 1.f));
  // elevator: the pitch rate that bends the flight path as asked at the commanded load factor (in a turn, the part of
  // the turn rate that lies in the pitch axis), tracked with stick scaled by this airframe's learned pitch power
  float qT = G0 * (nzT - cosf(g) * cb) / spd;
  static thread_local float dbgT = 0; dbgT += dt;   // (per thread: the background quote flies the same autopilot - the review of v3.44.0, RND-8)
  if (getenv("APDBG2") && apMode == AP_APPR && apStage == APS_FINAL && dbgT > 0.5f) { dbgT = 0;
    printf("    fin vsT %5.1f vs %5.1f  gT %5.2f g %5.2f eD %+.3f gI %+.3f  nzT %.2f nz %.2f  pitch %5.1f  ias %5.1f/%5.1f est %5.1f  thr %.2f thrI %.2f flaps %.2f\n",
           vsT, vel.y, gT / DEG, g / DEG, eD, apGamI, nzT, gLoad, pitchDeg(), ias, apSpeed, apSpdEst, ctl.throttle, apThrI, flaps); }
  apRates(qT, pT, rollCap, nzMin, nzMax, dt);
  // autothrottle: everything the engines have (reheat included)
  if (tecs) {   // (the energy law's: the throttle that answers the energy-rate error, scaled by what a throttle's worth of thrust does to it here - less where the engines are slow to follow)
    // (a throttle's worth at the setting it is at: an afterburning type's reheat makes most of its full thrust in the last
    // 15% of the throttle, and its whole thrust over the whole range put the XR-40's at 6 g a throttle - 2.5 times what the
    // dry range gives - so its throttle hardly moved and it never slowed)
    const float s0 = std::max(engineSpool - 0.1f, 0.f), s1 = std::min(engineSpool + 0.1f, 1.f);
    const float kT = std::max((thrustAt(s1, spd, spd) - thrustAt(s0, spd, spd)) / (s1 - s0) / (mass() * G0), 0.03f);
    const float slow = std::min(1.f, 1.5f / std::max(apEnv.spool, 0.5f));
    apThrI = clampf(apThrI + eE / kT * 0.3f * slow * dt, 0.f, 1.f);
    apThrDemand = apThrI + eE / kT * 0.8f * slow;
    ctl.throttle = clampf(apThrDemand, 0.f, 1.f);
  } else if (apSpeed > 0) {
    float e = apSpeed - ias;
    apThrI = clampf(apThrI + e * 0.04f * dt, 0.f, 1.f);
    ctl.throttle = clampf(apThrI + e * 0.12f + (vsT - vel.y) * 0.02f, 0.f, 1.f);
  } else if (apMode == AP_APPR) ctl.throttle = std::max(0.f, ctl.throttle - dt * 0.6f);   // flare: idle
  // (apSpeed 0 outside an approach: the pilot keeps the throttle)
  // a twin with an engine out: full rudder holds the live engine's thrust only down to the minimum control speed.
  // Below it - the rudder at its stop and the sideslip still growing - power comes off the live engine until the
  // rudder holds again (the standard recovery), and goes back on as it does
  const bool asym = s.engines >= 2 && fabsf(fail.engineHealth[0] - fail.engineHealth[1]) > 0.3f;
  if (asym && fabsf(ctl.yaw) > 0.95f && fabsf(beta) > 5.f * DEG) apVmcCap = std::max(0.15f, apVmcCap - dt * 0.5f);
  else apVmcCap = std::min(1.f, apVmcCap + dt * 0.15f);
  if (asym) ctl.throttle = std::min(ctl.throttle, apVmcCap);
}
