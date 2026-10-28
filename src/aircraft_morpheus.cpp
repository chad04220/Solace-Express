// Somnium Volandi - the XR-50 Morpheus's flight model (special 3; docs/XR50.md). Plane::substep calls in for its
// aerodynamics (morpheusAero) and its thrust (morpheusThrust); thrustAt, fuelFlowAt and liftThrustMax ask the engines
// here too, so the autopilot and the learned envelope fly the same machine.
//
// What it is: a swing wing the schedule sweeps from 16 deg (the hover, take-off and landing, slow flight) back to 68
// deg (supersonic and beyond), blown over its trailing edges (supercirculation) to three times a plain wing's lift; two
// combined-cycle engines - turbofans with reheat to Mach 3, ramjets through Mach 5, scramjets to about 7.5 - and between
// them a rocket with its own oxidizer for the climb out of the atmosphere; nozzles that vector 20 deg in pitch and yaw
// and swivel 95 deg down, with a lift fan behind the sensor bay and roll posts under the wing roots, for the hover;
// reaction jets for the thin air; leading edges that the air heats past 1,600 C. No one is aboard: it is flown from a
// ground station (docs/XR50.md), so nothing in it is held back for a body. Twice the XR-40 in everything that makes
// it aggressive - its thrust for its weight, its pitch and roll rates, its loads - its engines, like the XR-40's pods,
// are fiction: five times a real engine's thrust for the fuel. Its fly-by-wire commands rates (a load factor up and
// away) and turns them into what the control surfaces, the nozzles, the fan, the posts and the jets can each give at
// the moment, so it goes soft where the air is thin and slow until the propulsion takes over.
#include "aircraft.h"

namespace {
const float kCapture = 1.8f, kQMax = 85000.f;            // the inlets' capture area, both (m^2), and the combustors' pressure limit (Pa of q)
const float kRocketVac = kMorpheusRocketVac, kRocketExit = 0.55f;   // the rocket's thrust in vacuum (N) and its bell's exit area (m^2)
const float kRocketIsp = 5400.f, kRocketOF = 2.4f;        // its specific impulse in vacuum (s: fiction, fifteen times a LOX engine's, so the
                                                          // same 3.4 t of oxidizer gives 1,620 kN for about 150 s) and oxidizer-to-fuel ratio
const float kFanShare = 0.8f, kPostShare = 0.06f;         // the lift fan's and each roll post's thrust at full dry power, sea level, for the engines' rated thrust
const float kVecMax = 20.f * DEG, kSwivelMax = 95.f * DEG;
const float kRamTsfc = 2.5e-5f, kScramTsfc = 3.6e-5f;     // kg / (N s): an Isp of ~4,100 s and ~2,800 (fiction: a real ramjet's is ~1,600, a scramjet's ~1,100)
const vec3 kRcsMax(210000.f, 180000.f, 135000.f);         // the reaction jets' moments (N m: pitch, yaw, roll)
const float kCirc = 3.7f;                                 // the supercirculation's lift, fully blown, over a plain wing's (1 + kCirc)

// the right nozzle's force direction for a swivel-plus-pitch angle a and a yaw angle y (the jet the other way)
vec3 nozzleDir(float a, float y) { return vec3(sinf(y), sinf(a) * cosf(y), -cosf(a) * cosf(y)); }
}

// the swept wing's span (m): the panel from its pivot, its tip's leading and trailing corners averaged (morpheus_sdf.glsl
// draws the panel: 5.8 m from its pivot, its leading edge 0.8 m ahead of the pivot line, the tip's chord a metre)
float morpheusSpan(float sweep) { return 2.f * (kMorpheusPivot.x + 5.8f * cosf(sweep) + 0.3f * sinf(sweep)); }
float morpheusRocketSeconds(float oxidizerKg, float throttle) {
  const float oxFlow = std::max(0.4f, throttle) * kRocketVac / (kRocketIsp * G0) * kRocketOF / (1.f + kRocketOF);
  return std::max(0.f, oxidizerKg) / oxFlow;
}
// the right wingtip's lamp: at the panel's tip, mid chord, swung aft about the pivot with the sweep
vec3 morpheusWingTip(float sweep) {
  const float s = 5.8f, c = -0.15f;   // (span from the pivot, chord from the pivot line: the panel's own frame - its lens)
  return vec3(kMorpheusPivot.x + s * cosf(sweep) - c * sinf(sweep), kMorpheusPivot.y - 0.02f, kMorpheusPivot.z + s * sinf(sweep) + c * cosf(sweep));
}

MorpheusThrust morpheusEngines(const AircraftSpec& s, float spool, float V, float rho, float a) {
  MorpheusThrust t;
  const float M = V / std::max(a, 1.f), sigma = clampf(rho / 1.225f, 0.f, 1.3f), q = 0.5f * rho * V * V;
  const float dry = clampf(spool / 0.85f, 0.f, 1.f), ab = smoothstepf(0.85f, 1.f, spool);
  // the turbofans: their rated dry thrust at 85% throttle, 1.6 times it in full reheat; the ram rise of the inlet's
  // recovery to Mach 2.4, then their compressor faces too hot - by Mach 3.3 the inlet's doors have handed the air to
  // the ram duct
  const float tfK = s.engines * s.power * powf(sigma, 0.7f) * (1.f + 0.32f * std::min(M, 2.4f)) * (1.f - smoothstepf(2.4f, 3.3f, M));
  t.tfDry = tfK * dry; t.tf = tfK * (dry + 0.6f * ab);
  // the ram and scram combustion: thrust in proportion to the dynamic pressure the inlets capture (a thrust coefficient
  // on q times their area), the fuel-air ratio set by the throttle - and the engines' control holds the combustors to
  // their pressure limit, 85 kPa of q (deeper in the air the thrust stops growing and the drag doesn't: fast low down,
  // they run out of thrust long before the heat would end it). The ramjet lights from Mach 1.5 and fades past 3.5; from
  // Mach 4.3 to 5.3 the combustion goes supersonic, and the scramjet's falls away with Mach - to the balance of thrust
  // and drag a little past Mach 7, where below 27 km or so the leading edges are past their limit. Each needs the air
  // dense enough to burn in: a ramjet some 8 kPa of q, a scramjet 15
  const float lit = dry, w = smoothstepf(4.3f, 5.3f, M), qc = std::min(q, kQMax);
  const float ctRam = 1.6f * smoothstepf(1.5f, 2.5f, M) * (1.f - 0.15f * std::max(M - 3.5f, 0.f)) * smoothstepf(4e3f, 12e3f, q);
  const float ctScram = 1.3f * clampf(1.f - (M - 5.8f) / 2.4f, 0.f, 1.1f) * smoothstepf(9e3f, 20e3f, q);
  t.ram = kCapture * qc * (1.f - w) * std::max(ctRam, 0.f) * lit;
  t.scram = kCapture * qc * w * ctScram * lit;
  return t;
}
float morpheusFuelFlow(const AircraftSpec& s, const MorpheusThrust& t, float M, float rho) {
  // (the turbofans' dry thrust at their dry consumption, and what reheat adds at what makes full reheat's whole thrust
  // burn at the wet figure)
  const float mk = 1.f + 0.35f * std::min(M, 3.f), abTsfc = (1.6f * s.jetWetTsfc - s.jetDryTsfc) / 0.6f;
  const float idle = s.engines * s.power * s.jetDryTsfc * 0.045f * powf(clampf(rho / 1.225f, 0.001f, 1.3f), 0.65f);
  return std::max(idle, (t.tfDry * s.jetDryTsfc + (t.tf - t.tfDry) * abTsfc) * mk + t.ram * kRamTsfc + t.scram * kScramTsfc);
}
// the thrust it can direct straight down at full dry power in this air, the fan and the nozzles in the split that
// balances it in pitch (the fan's arm 5 m ahead of the centre of gravity, the nozzles' 7.4 m behind it)
float morpheusHoverMax(const AircraftSpec& s, float rho) {
  const float sig7 = powf(clampf(rho / 1.225f, 0.f, 1.3f), 0.7f);
  const float fan = kFanShare * s.engines * s.power * sig7, aft = s.engines * s.power * sig7 * 0.62f;   // (the nozzles and the posts: the core's 62% the fan leaves it)
  const float fanShare = kMorpheusNozzle.z / (kMorpheusNozzle.z - kMorpheusFan.z);
  return std::min(fan / fanShare, aft / (1.f - fanShare));
}

// forward for the hover, take-off and landing (the lever off its stop, or the gear down and slow) and below Mach 0.72;
// back with the Mach number to fully aft by 1.45, where the drag rise is the swept wing's and not the straight one's
float Plane::sweepTarget() const {
  if (nozzle > 0.05f || (gear > 0.5f && airspeed < 120.f)) return kMorpheusSweepMin;
  return kMorpheusSweepMin + (kMorpheusSweepMax - kMorpheusSweepMin) * smoothstepf(0.72f, 1.45f, mach);
}

bool Plane::morpheusAero(const Atmosphere& atmo, vec3 va, float V, float altAgl, float dt, vec3& F, vec3& Taero, vec3& surfMax) {
  const AircraftSpec& s = *spec;
  const AeroModel& aero = aeroModel(s);
  // the wing's sweep (8 deg/s, an F-14's) and its slats and flaps: out with the wing forward and slow
  sweep = approach(sweep, sweepTarget(), 8.f * DEG, dt);
  const bool slow = nozzle > 0.05f || (gear > 0.5f && airspeed < 110.f);
  hiLift = approach(hiLift, slow && sweep < kMorpheusSweepMin + 6.f * DEG ? 1.f : 0.f, 0.5f, dt);
  // ---- the leading edges' heat: the air brought to rest against them (the recovery temperature) and the heating of a
  // 5 cm leading edge (Sutton-Graves), the temperature where its glow radiates that heat away - the skin settles at the
  // lower of the two, over its thermal mass's 20 s or so. Past 1,650 C the carbon-carbon burns away
  {
    const float Tr = atmo.T * (1.f + 0.18f * mach * mach);
    const float qc = 1.74e-4f * sqrtf(std::max(atmo.rho, 0.f) / 0.05f) * V * V * V;
    const float Trad = powf(std::max(qc, 1.f) / (0.85f * 5.67e-8f), 0.25f);
    const float Teq = 1.f / powf(powf(1.f / Tr, 4.f) + powf(1.f / Trad, 4.f), 0.25f);
    skinT += (std::max(Teq, atmo.T) - skinT) * std::min(dt / 20.f, 1.f);
    const float over = (skinT - kMorpheusSkinLimit) / 150.f;
    heatOver = clampf(heatOver + (over > 0.f ? over : -0.05f) * dt / 12.f, 0.f, 1.f);
    if (heatOver >= 1.f && !onGround) { ev.crashed = true; ev.crashReason = "Leading edges burned through - the airframe overheated"; return false; }
  }
  // ---- supercirculation: the engines' bleed blown out of slots over the rounded trailing edges of the wing and its
  // flaperons (circulation control, a Coanda jet) raises the whole lift curve - to three times a plain wing's with the
  // engines at full dry power, less as they slow, none with them out - and the flaperons' roll power with it (blown one
  // side against the other). Past Mach 2.5 the slots close (by Mach 4 against the heat; the forebody's compression lift
  // takes over). It is what no wing does: the lift for 160 g at Mach 2 low down and for 800 deg/s of roll
  circ = (engineRunning && fuel > 0.f ? 0.4f + 0.6f * clampf(engineSpool / 0.85f, 0.f, 1.f) : 0.f) * (1.f - smoothstepf(2.5f, 4.f, mach));
  const float blow = 1.f + kCirc * circ;
  Taero = vec3(); surfMax = vec3();
  if (V <= 0.5f) return true;
  const float qbar = 0.5f * density * V * V, M = mach;
  const float b = morpheusSpan(sweep), AR = b * b / s.wingArea, k = (sweep - kMorpheusSweepMin) / (kMorpheusSweepMax - kMorpheusSweepMin);
  // lift: DATCOM's slope for this aspect ratio and the half-chord's sweep (the Mach held at 0.85 below the drag rise),
  // Ackeret's supersonic, and beneath them both the waverider's body lift (it never falls below 0.6 a radian)
  const float Mc = std::min(M, 0.85f), th = tanf(sweep - 8.f * DEG);
  const float claSub = 2.f * PI * AR / (2.f + sqrtf(4.f + AR * AR * (1.f - Mc * Mc + th * th)));
  const float claSup = 3.4f / sqrtf(std::max(M * M - 1.f, 0.3f));
  const float CLa = std::max(claSub + (std::min(claSub, claSup) - claSub) * smoothstepf(0.95f, 1.3f, M), 0.6f);
  const float cl0 = s.CL0 + 0.35f * hiLift;
  const float clMax = 1.2f + 0.25f * (1.f - k) + 0.45f * hiLift - 0.3f * fail.ice;
  const float aStall = (clMax - cl0) / CLa, aNeg = (-1.0f - cl0) / CLa;
  const float sig = std::max(smoothstepf(aStall, aStall + 6.f * DEG, alpha), smoothstepf(-aNeg, -aNeg + 5.f * DEG, -alpha));
  // (past the stall the swept wing's vortices carry it on: it stalls softly, the delta's way)
  float CL = blow * ((1.f - sig) * clampf(cl0 + CLa * alpha, -1.1f, clMax) + sig * (1.05f + 0.25f * k) * sinf(2.f * alpha));
  const float hyp = smoothstepf(2.5f, 5.f, M);
  CL += hyp * 1.2f * sinf(alpha) * fabsf(sinf(alpha)) * cosf(alpha);   // (hypersonic: the forebody's compression lift, Newton's)
  const float ge = altAgl < b ? 1.f + 0.12f * (1.f - altAgl / b) : 1.f;
  CL *= ge;
  // drag: the parts' friction and form (aero.cpp), the gear, the slats and flaps; the lift's - the span's loading
  // subsonic (the worse swept, its span the shorter), the lift tilted back by the angle it flies at supersonic; the
  // stalled wing, the body broadside; the drag rise through the sound barrier, the larger with the wing forward (its
  // sections meet the shock unswept), and the wall past the design Mach
  cd0 = aeroCD0(aero, s, V, density, M);
  const float sb = sinf(beta), sa = sinf(alpha) * smoothstepf(8.f * DEG, 20.f * DEG, fabsf(alpha));
  const float crossCD = 0.72f * (sb * sb * aero.sideArea + sa * sa * aero.planArea) / s.wingArea;
  const float sup = smoothstepf(1.05f, 1.6f, M);
  float CD = cd0 + aero.gearDq / s.wingArea * gear + 0.045f * hiLift + (1.f - sup) * CL * CL / (PI * aero.e * AR) / ge + sup * CL * CL / (CLa * blow)
           + sig * (0.35f + 1.1f * sinf(alpha) * sinf(alpha)) + crossCD + 0.025f * fail.ice;
  CD += (1.4f - 0.8f * k) * (0.020f * smoothstepf(0.86f, 1.04f, M) - 0.006f * smoothstepf(1.2f, 2.2f, M));
  { const float over = M - s.designMach; if (over > 0.f) CD += 0.5f * over * over; }
  const float CY = -0.7f * beta;
  const vec3 liftDir = normalize(cross(vec3(1, 0, 0), va)), dragDir = va * (-1.f / V);
  F += liftDir * (CL * qbar * s.wingArea) + dragDir * (CD * qbar * s.wingArea) + vec3(CY * qbar * s.wingArea, 0, 0);
  aeroWakeElliptic(CL * qbar * s.wingArea, density, V, b, s.wingY, s.wingZ, s.engines, tbcc[0] + tbcc[1] + tbcc[2], 0.45f * s.fusLen, wake);
  // the airframe's own moments: stable in pitch about 4 deg of angle of attack, its pitch, roll and yaw damping, the
  // dihedral effect and the V-tail's weathercock - the fly-by-wire flies through them
  const float Vh = std::max(V, 12.f), p = -w.z, qq = w.x, r = -w.y;
  const float ph = (p - gustRot.x) * b / (2.f * Vh), qh = (qq - gustRot.y) * s.chord / (2.f * Vh), rh = (r - gustRot.z) * b / (2.f * Vh);
  const float Cm0 = -0.44f * (alpha - 4.f * DEG) - 5.6f * qh, Cl0 = -0.10f * beta - 0.55f * ph + 0.08f * rh, Cn0 = 0.09f * beta - 0.16f * rh;
  Taero = vec3(Cm0 * qbar * s.wingArea * s.chord, -Cn0 * qbar * s.wingArea * b, -Cl0 * qbar * s.wingArea * b);
  // what the control surfaces can give: the all-moving V-tail in pitch and yaw, the flaperons (blown) and the V-tail's
  // differential in roll - the less of it stalled
  const float ctlEff = 1.f - 0.4f * sig;
  surfMax = vec3(s.elevPow * s.chord, s.rudPow * b, s.ailPow * b * blow) * (qbar * s.wingArea * ctlEff);
  stallWarn = smoothstepf(aStall - 5.f * DEG, aStall - 1.5f * DEG, alpha) * (1.f - hyp);
  return true;
}

void Plane::morpheusThrust(vec3& F, vec3& T, vec3 Taero, vec3 surfMax, float V, float dt) {
  const AircraftSpec& s = *spec;
  const bool lit = engineRunning && fuel > 0.f;
  const Atmosphere atmo = isa(pos.y);
  const MorpheusThrust e = lit ? morpheusEngines(s, engineSpool, V, density, soundSpeed) : MorpheusThrust{};
  tbcc[0] = e.tf; tbcc[1] = e.ram; tbcc[2] = e.scram;
  // ---- the lever: the nozzles swivel down (95 deg at its stop) as the lift fan's clutch engages - slow and below Mach
  // 0.45 only (it is a shaft from the engines' fans, its doors open into the airflow), the nozzles no further than the
  // fan is in, so the fan and the nozzles come in together and the airframe stays balanced. With the nozzles off the
  // aft stop, reheat is inhibited; the fan takes 38% of the cores' dry thrust and makes 0.8 of their rated (840 kN), the
  // roll posts bleed 0.06 each from what is left
  const bool fanOk = mach < 0.45f && !onWater;
  fanClutch = approach(fanClutch, fanOk ? smoothstepf(0.04f, 0.4f, nozzle) : 0.f, 0.9f, dt);
  nzSwivel = approach(nzSwivel, std::min(nozzle * kSwivelMax, (fanOk ? 10.f + 85.f * fanClutch : 30.f) * DEG), 50.f * DEG, dt);
  const float abCut = smoothstepf(5.f * DEG, 20.f * DEG, nzSwivel);
  const float c = fanClutch, sig7 = powf(clampf(density / 1.225f, 0.f, 1.3f), 0.7f), dryFrac = lit ? clampf(engineSpool / 0.85f, 0.f, 1.f) : 0.f;
  const float rated = s.engines * s.power, fanNom = c * kFanShare * rated * dryFrac * sig7, postAll = c * 2.f * kPostShare * rated * dryFrac * sig7;
  const float nozzleN = std::max(0.f, e.tfDry * (1.f - 0.38f * c) + (e.tf - e.tfDry) * (1.f - abCut) - postAll);
  fanAngle = fmodf(fanAngle + c * (6.f + 90.f * engineSpool) * dt, 2.f * PI * 64.f);
  // ---- the rocket: lit, it runs at the throttle (40% at the least) until its oxidizer is gone; its thrust is its
  // vacuum thrust less the air's pressure on its exit
  {
    const bool can = rocketLit && oxidizer > 0.f && fuel > 0.f;
    rocketThr = approach(rocketThr, can ? std::max(0.4f, ctl.throttle) : 0.f, 3.f, dt);
    if (!can) rocketLit = false;
    rocketN = rocketThr * std::max(kRocketVac - atmo.p * kRocketExit, 0.f);
    const float mdot = rocketThr * kRocketVac / (kRocketIsp * G0);
    oxidizer = std::max(0.f, oxidizer - mdot * kRocketOF / (1.f + kRocketOF) * dt);
    fuel = std::max(0.f, fuel - mdot / (1.f + kRocketOF) * dt);
  }
  // ---- the fly-by-wire: no one is aboard to spare. The stick commands a pitch rate - up and away the rate of a 160 g
  // pull, slow a plain 4 to 12 rad/s the vectoring holds past the stall - and roll and yaw rates (800 and 200 deg/s:
  // twice the XR-40's); hands off in the hover it holds the attitude level. Its one limit is the airframe's: the rate
  // that holds 95% of the structure's load either way, less what it is over by
  const float Vt = std::max(V, 1.f);
  const float hover = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, V);
  vec3 wd(ctl.pitch * fbwPitchMax(Vt) + ctl.trim * 0.15f, -ctl.yaw * 3.6f, -ctl.roll * fbwRollMax(hover));
  {
    const float gHi = 0.95f * s.gLimitPos(), gLo = 0.95f * s.gLimitNeg();
    wd.x = clampf(wd.x, gLo * G0 / Vt + (gLo - gLoad) * 0.02f, gHi * G0 / Vt + (gHi - gLoad) * 0.02f);
  }
  const float ms0 = mass() / s.emptyMass, kr = onGround ? 4.f : 18.f, kp = onGround ? 4.f : 28.f;   // (twice the XR-40's: it snaps)
  const vec3 I(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
  // the angle of attack limiter: 32 deg - the wing's lift, the controls whole - until the stick is pulled hard against
  // its stop, through the gate into post-stall flight to 90 deg (where the nozzles hold what the tail no longer can);
  // never more than 25 deg negative. At these rates a limit met head-on is a limit overshot: it closes the angle's rate
  // in as it nears one (the body's rate over the flight path's, which the load turns at), to what the pitch authority it
  // has now (the tail's, the nozzles') can stop there
  if (V > 15.f && hover < 0.5f) {
    const float aLim = (ctl.pitch > 0.95f ? 90.f : 32.f) * DEG, aNegLim = -25.f * DEG, path = gLoad * G0 / Vt;
    const float brake = std::max(0.3f * (surfMax.x + nozzleN * sinf(kVecMax) * kMorpheusNozzle.z) / I.x, 1.f);
    auto room = [brake](float d) { return d > 0.f ? sqrtf(2.f * brake * d) : d * 6.f; };
    wd.x = clampf(wd.x, path - room(alpha - aNegLim), path + room(aLim - alpha));
  }
  if (hover > 0) {
    if (fabsf(ctl.pitch) < 0.05f) wd.x += hover * 2.2f * (0.f - pitchDeg()) * DEG;
    if (fabsf(ctl.roll) < 0.05f) wd.z += hover * 2.2f * bankDeg() * DEG;
  }
  // (on its wheels the airframe can't be levered about them until the wing carries some of the weight: the authority
  // comes in with the take-off roll, whole well before rotation)
  const float ga = onGround && hover < 0.5f ? smoothstepf(25.f, 55.f, V) : 1.f;
  wd.x *= ga; wd.z *= ga;
  const vec3 tDes(I.x * kp * (wd.x - w.x), I.y * kr * (wd.y - w.y), I.z * kr * (wd.z - w.z));
  // the propulsion's own moments at its nominal settings (the hover's fan and nozzles all but balance), so what is left
  // is what the controls must make
  auto nozzleT = [&](float pc, float pd, float yc, vec3* force) {
    vec3 t, f;
    for (int i = 0; i < 2; i++) {
      const float sd = i ? 1.f : -1.f;
      const vec3 fi = nozzleDir(nzSwivel + pc + sd * pd, yc) * (0.5f * nozzleN), r(sd * kMorpheusNozzle.x, kMorpheusNozzle.y, kMorpheusNozzle.z);
      f += fi; t += cross(r, fi);
    }
    if (force) *force = f;
    return t;
  };
  const vec3 postR(kMorpheusPost.x, kMorpheusPost.y, kMorpheusPost.z), postL(-kMorpheusPost.x, kMorpheusPost.y, kMorpheusPost.z);
  const vec3 tNoz0 = nozzleT(0.f, 0.f, 0.f, nullptr);
  const vec3 tBase = tNoz0 + cross(kMorpheusFan, vec3(0, fanNom, 0)) + cross(postR, vec3(0, 0.5f * postAll, 0)) + cross(postL, vec3(0, 0.5f * postAll, 0));
  vec3 rem = tDes - Taero - tBase;
  // 1) the control surfaces, as far as the dynamic pressure gives them
  auto take = [](float n, float mx) { return mx > 1.f ? clampf(n, -mx, mx) : 0.f; };
  const vec3 tSurf(take(rem.x, surfMax.x), take(rem.y, surfMax.y), take(rem.z, surfMax.z));
  surf = vec3(surfMax.x > 1.f ? tSurf.x / surfMax.x : 0.f, surfMax.y > 1.f ? -tSurf.y / surfMax.y : 0.f, surfMax.z > 1.f ? -tSurf.z / surfMax.z : 0.f);
  rem = rem - tSurf;
  // 2) the nozzles' vectoring: collective pitch, differential pitch, collective yaw - what each does to the moments,
  // measured about where they are now, then the least-squares settings within their 20 deg (damped, so a weak axis
  // isn't driven to its stops)
  float u[3] = {0.f, 0.f, 0.f};
  if (nozzleN > 100.f) {
    float J[3][3];
    for (int k = 0; k < 3; k++) {
      const float h = 0.05f;
      const vec3 d = (nozzleT(k == 0 ? h : 0.f, k == 1 ? h : 0.f, k == 2 ? h : 0.f, nullptr) - tNoz0) * (1.f / h);
      J[0][k] = d.x; J[1][k] = d.y; J[2][k] = d.z;
    }
    double A[3][3], g[3];   // (in double: at full power its terms pass a float's range cubed)
    for (int i = 0; i < 3; i++) {
      g[i] = double(J[0][i]) * rem.x + double(J[1][i]) * rem.y + double(J[2][i]) * rem.z;
      for (int j = 0; j < 3; j++) A[i][j] = double(J[0][i]) * J[0][j] + double(J[1][i]) * J[1][j] + double(J[2][i]) * J[2][j];
    }
    const double lam = 1e-3 * (A[0][0] + A[1][1] + A[2][2]) + 1.0;
    for (int i = 0; i < 3; i++) A[i][i] += lam;
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(det) > 1e-20) {
      for (int k = 0; k < 3; k++) {   // (Cramer's rule: column k replaced by g)
        double B[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) B[i][j] = j == k ? g[i] : A[i][j];
        u[k] = clampf(float((B[0][0] * (B[1][1] * B[2][2] - B[1][2] * B[2][1]) - B[0][1] * (B[1][0] * B[2][2] - B[1][2] * B[2][0]) + B[0][2] * (B[1][0] * B[2][1] - B[1][1] * B[2][0])) / det), -kVecMax, kVecMax);
      }
    }
  }
  for (int i = 0; i < 2; i++) {   // (the actuators slew at 180 deg/s: stop to stop in under a quarter of a second)
    const float sd = i ? 1.f : -1.f;
    nzPitch[i] = approach(nzPitch[i], clampf(u[0] + sd * u[1], -kVecMax, kVecMax), 180.f * DEG, dt);
    nzYaw[i] = approach(nzYaw[i], u[2], 180.f * DEG, dt);
  }
  vec3 fNoz;
  const vec3 tNoz = nozzleT(0.5f * (nzPitch[0] + nzPitch[1]), 0.5f * (nzPitch[1] - nzPitch[0]), 0.5f * (nzYaw[0] + nzYaw[1]), &fNoz);
  rem = rem - (tNoz - tNoz0);
  // 3) the hover's own: the fan's inlet vanes trade its thrust (+20 / -30%) for pitch, the roll posts differentially for
  // roll. On the ground the wheels hold the attitude: they stay at their nominal settings
  float uFan = 0.f, uPost = 0.f;
  if (!onGround && fanNom > 100.f) { uFan = clampf(rem.x / (fanNom * -kMorpheusFan.z), -0.3f, 0.2f); rem.x -= uFan * fanNom * -kMorpheusFan.z; }
  if (!onGround && postAll > 100.f) { uPost = clampf(rem.z / (postAll * kMorpheusPost.x), -1.f, 1.f); rem.z -= uPost * postAll * kMorpheusPost.x; }
  fanN = fanNom * (1.f + uFan);
  postN[0] = 0.5f * postAll * (1.f - uPost); postN[1] = 0.5f * postAll * (1.f + uPost);
  // 4) the reaction jets: armed high and where the air is too thin to steer by (below some 9 kPa of dynamic pressure
  // above 12 km), they make up what is left
  const float rcsArm = smoothstepf(9000.f, 3000.f, 0.5f * density * V * V) * smoothstepf(12000.f, 18000.f, pos.y);
  rcs = vec3(clampf(rem.x / kRcsMax.x, -1.f, 1.f), clampf(rem.y / kRcsMax.y, -1.f, 1.f), clampf(rem.z / kRcsMax.z, -1.f, 1.f)) * rcsArm;
  // ---- the forces and their moments about the centre of gravity
  F += fNoz; T += tNoz;
  F += vec3(0, 0, -(e.ram + e.scram + rocketN));   // (the ram and scram jets' flow leaves by the aft body's expansion ramp)
  { const vec3 f(0, fanN, 0); F += f; T += cross(kMorpheusFan, f); }
  { const vec3 fl(0, postN[0], 0), fr(0, postN[1], 0); F += fl + fr; T += cross(postL, fl) + cross(postR, fr); }
  T += tSurf + vec3(rcs.x * kRcsMax.x, rcs.y * kRcsMax.y, rcs.z * kRcsMax.z);
}
