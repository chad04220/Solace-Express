// Somnium Volandi - the XR-60 Phantasos's flight model (special 4; docs/XR60.md). Plane::substep calls in for its
// aerodynamics (xr60Aero) and its drive (xr60Thrust); thrustAt, fuelFlowAt and liftThrustMax ask the drive here too,
// so the autopilot and the learned envelope fly the same machine.
//
// What it is: a knife of a lifting body with forward-swept blade wings whose tips fold down for the hover; a ring
// drive at its tail - an annular plasma thruster that gives its whole thrust from a standstill and in a vacuum alike
// (it needs no air; in dense air at speed the inflow quenches it, and the keel's MHD-bypass scramjet carries the
// craft on to Mach 12); a surge bank that triples the ring for forty seconds; four field emitters under the body that
// hold it up with no moving parts; a circulation field over the whole wing that raises its lift to seven times a plain
// wing's; a plasma sheath that halves the friction and the heat; and field torque where there is no air to push on.
// Its fuel is metallic hydrogen, burned at a fiction's rate (as the XR-50's engines are fiction). No one is aboard: it
// is flown from an operations pod, and it is three times the XR-50 in everything that makes a research craft quick.
// Its fly-by-wire commands rates (a load factor up and away) and turns them into what the surfaces, the ring's
// vectoring, the emitters and the field can each give at the moment.
#include "aircraft.h"

namespace {
const float kCapture = 1.1f, kQMax = 160000.f;            // the keel's capture area (m^2) and its combustors' pressure limit (Pa of q)
const float kScramTsfc = 1.8e-5f;                         // kg / (N s): the scramjet's hydrogen (fiction: a real one's is ~1,100 s of Isp; this is ~5,700)
const float kEmitShare = 0.6f;                            // the emitters' thrust, all four, at full ring power, for the ring's rated thrust
const float kRingVec = 35.f * DEG;                        // the ring's vectoring, pitch and yaw
const float kSurgeGain = 2.f;                             // what the surge adds to the ring's thrust (x3 in all)
const vec3 kFieldMax(900000.f, 700000.f, 500000.f);       // the field torque's moments (N m: pitch, yaw, roll), with the core at power
const float kCirc = 6.f;                                  // the circulation field's lift over a plain wing's (1 + kCirc)
const float kSpanSpread = 12.4f, kSpanFolded = 9.5f;      // the span with the tip blades spread and folded down (m)

// the ring's force direction for a pitch angle a (positive: the stream down) and a yaw angle y (the stream to the right)
vec3 ringDir(float a, float y) { return vec3(sinf(y), sinf(a) * cosf(y), -cosf(a) * cosf(y)); }
}

Xr60Thrust xr60Drive(const AircraftSpec& s, float throttle, float V, float rho, float a, float surge) {
  Xr60Thrust t;
  const float M = V / std::max(a, 1.f), q = 0.5f * rho * V * V, thr = clampf(throttle, 0.f, 1.f);
  // the ring: its rated thrust at any speed and in any air, no spool (the throttle is the current), the surge on top.
  // In dense air at speed the inflow quenches the annulus (from Mach 1.5, mostly by 4, where there is dynamic pressure
  // to speak of, and all but wholly deep in the air past 200 kPa of it): there the scramjet carries it, and the ring
  // comes back as the air thins
  const float quench = 0.85f * smoothstepf(1.5f, 4.f, M) * smoothstepf(2e3f, 12e3f, q) + 0.13f * smoothstepf(30e3f, 200e3f, q);
  t.ring = s.engines * s.power * thr * (1.f + kSurgeGain * clampf(surge, 0.f, 1.f)) * (1.f - quench);
  // the scramjet: thrust in proportion to the dynamic pressure the keel captures (a coefficient on q times its area), the
  // fuel set by the throttle, its combustors held to 160 kPa - the MHD generator ahead of them takes a third of the
  // inflow's energy out, which is why they stand twice the XR-50's. It lights from Mach 2.5, and its coefficient falls
  // past Mach 9 to the balance of thrust and drag a little past Mach 12. It needs the air dense enough to burn in
  const float w = smoothstepf(2.5f, 3.5f, M), qc = std::min(q, kQMax);
  const float ct = 1.5f * clampf(1.f - (M - 9.f) / 3.4f, 0.f, 1.1f) * smoothstepf(6e3f, 15e3f, q);
  t.scram = kCapture * qc * w * ct * thr;
  return t;
}
float xr60FuelFlow(const AircraftSpec& s, const Xr60Thrust& t) {
  // (the ring's reaction mass at its consumption - the same lit or cold, the core keeps the fields up - and the
  // scramjet's hydrogen at its own)
  const float idle = s.engines * s.power * s.jetDryTsfc * 0.02f;
  return std::max(idle, t.ring * s.jetDryTsfc + t.scram * kScramTsfc);
}
// the thrust the emitters can turn straight down at full power: 0.6 of the ring's rated, in any air
float xr60HoverMax(const AircraftSpec& s, float) { return kEmitShare * s.engines * s.power; }

bool Plane::xr60Aero(const Atmosphere& atmo, vec3 va, float V, float altAgl, float dt, vec3& F, vec3& Taero, vec3& surfMax) {
  const AircraftSpec& s = *spec;
  const AeroModel& aero = aeroModel(s);
  const bool lit = engineRunning && fuel > 0.f;
  // the tip blades: down for the hover, take-off and landing (the lever off its stop, or the gear down and slow), at
  // 70 deg in a second and a half; spread otherwise
  const bool slow = nozzle > 0.05f || (gear > 0.5f && airspeed < 110.f);
  tipFold = approach(tipFold, slow ? 1.f : 0.f, 0.65f, dt);
  // ---- the sheath: the skin ionises its own boundary layer with the core at power (the core, not the throttle: it is
  // whole at idle) and loses it with the drive cold, over a second or two. It halves the heating and the friction
  sheath = approach(sheath, lit ? 1.f : 0.f, 0.6f, dt);
  // ---- the skin's heat: the XR-50's law (the recovery temperature against the radiative equilibrium of a Sutton-Graves
  // heating, over a thermal lag) with the sheath standing half the heating off, and a metamaterial skin good to
  // 2,500 C. Past that it fails
  {
    const float Tr = atmo.T * (1.f + 0.18f * mach * mach);
    const float qc = 1.74e-4f * sqrtf(std::max(atmo.rho, 0.f) / 0.05f) * V * V * V * (1.f - 0.5f * sheath);
    const float Trad = powf(std::max(qc, 1.f) / (0.85f * 5.67e-8f), 0.25f);
    const float Teq = 1.f / powf(powf(1.f / Tr, 4.f) + powf(1.f / Trad, 4.f), 0.25f);
    skinT += (std::max(Teq, atmo.T) - skinT) * std::min(dt / 15.f, 1.f);
    const float over = (skinT - kXr60SkinLimit) / 200.f;
    heatOver = clampf(heatOver + (over > 0.f ? over : -0.05f) * dt / 12.f, 0.f, 1.f);
    if (heatOver >= 1.f && !onGround) { ev.crashed = true; ev.crashReason = "Skin failed - the airframe overheated through its sheath"; return false; }
  }
  // ---- the circulation field: plasma actuators along the whole wing and the elevons, the XR-50's blown slots without
  // the slots (nothing to melt: it closes nowhere). From 0.5 at idle to 1 at full power, none with the core cold. It
  // raises the whole lift curve to seven times a plain wing's, and the elevons' roll power with it
  circ = lit ? 0.5f + 0.5f * clampf(engineSpool, 0.f, 1.f) : 0.f;
  const float blow = 1.f + kCirc * circ;
  Taero = vec3(); surfMax = vec3();
  if (V <= 0.5f) return true;
  const float qbar = 0.5f * density * V * V, M = mach;
  const float b = kSpanSpread + (kSpanFolded - kSpanSpread) * tipFold, AR = b * b / s.wingArea;
  // lift: DATCOM's slope for this aspect ratio and the half-chord's sweep (forward 22 deg at the leading edge, 12 at
  // the half chord; the Mach held at 0.85 below the drag rise), Ackeret's supersonic, and beneath them both the body's
  // lift (never below 0.6 a radian)
  const float Mc = std::min(M, 0.85f), th = tanf(12.f * DEG);
  const float claSub = 2.f * PI * AR / (2.f + sqrtf(4.f + AR * AR * (1.f - Mc * Mc + th * th)));
  const float claSup = 3.4f / sqrtf(std::max(M * M - 1.f, 0.3f));
  const float CLa = std::max(claSub + (std::min(claSub, claSup) - claSub) * smoothstepf(0.95f, 1.3f, M), 0.6f);
  const float cl0 = s.CL0, clMax = 1.6f - 0.2f * tipFold - 0.3f * fail.ice;
  const float aStall = (clMax - cl0) / CLa, aNeg = (-1.1f - cl0) / CLa;
  const float sig = std::max(smoothstepf(aStall, aStall + 6.f * DEG, alpha), smoothstepf(-aNeg, -aNeg + 5.f * DEG, -alpha));
  // (past the stall the chines' vortices carry it on: it stalls softly, the lifting body's way)
  float CL = blow * ((1.f - sig) * clampf(cl0 + CLa * alpha, -1.2f, clMax) + sig * 1.15f * sinf(2.f * alpha));
  const float hyp = smoothstepf(2.5f, 5.f, M);
  CL += hyp * 1.3f * sinf(alpha) * fabsf(sinf(alpha)) * cosf(alpha);   // (hypersonic: the compression surface's lift, Newton's)
  const float ge = altAgl < b ? 1.f + 0.12f * (1.f - altAgl / b) : 1.f;
  CL *= ge;
  // drag: the parts' friction and form (aero.cpp), less the sheath's share of the friction; the gear; the lift's - the
  // span's loading subsonic, the lift tilted back by the angle it flies at supersonic; the stalled wing, the body
  // broadside; the drag rise through the sound barrier (sharp: the blades are knife-edged) and the wall past the
  // design Mach
  cd0 = aeroCD0(aero, s, V, density, M) * (1.f - 0.35f * sheath);
  const float sb = sinf(beta), sa = sinf(alpha) * smoothstepf(8.f * DEG, 20.f * DEG, fabsf(alpha));
  const float crossCD = 0.65f * (sb * sb * aero.sideArea + sa * sa * aero.planArea) / s.wingArea;
  const float sup = smoothstepf(1.05f, 1.6f, M);
  float CD = cd0 + aero.gearDq / s.wingArea * gear + 0.02f * tipFold + (1.f - sup) * CL * CL / (PI * aero.e * AR) / ge + sup * CL * CL / (CLa * blow)
           + sig * (0.35f + 1.1f * sinf(alpha) * sinf(alpha)) + crossCD + 0.025f * fail.ice;
  CD += 0.014f * smoothstepf(0.9f, 1.05f, M) - 0.005f * smoothstepf(1.2f, 2.2f, M);
  { const float over = M - s.designMach; if (over > 0.f) CD += 0.5f * over * over; }
  const float CY = -0.6f * beta;
  const vec3 liftDir = normalize(cross(vec3(1, 0, 0), va)), dragDir = va * (-1.f / V);
  F += liftDir * (CL * qbar * s.wingArea) + dragDir * (CD * qbar * s.wingArea) + vec3(CY * qbar * s.wingArea, 0, 0);
  aeroWakeElliptic(CL * qbar * s.wingArea, density, V, b, s.wingY, s.wingZ, 1, ringN + scramN, 0.45f * s.fusLen, wake);
  // the airframe's own moments: stable in pitch about 3 deg of angle of attack, its pitch, roll and yaw damping, the
  // dihedral effect (the blades' tips rise) and the fins' weathercock - the fly-by-wire flies through them
  const float Vh = std::max(V, 12.f), p = -w.z, qq = w.x, r = -w.y;
  const float ph = (p - gustRot.x) * b / (2.f * Vh), qh = (qq - gustRot.y) * s.chord / (2.f * Vh), rh = (r - gustRot.z) * b / (2.f * Vh);
  const float Cm0 = -0.40f * (alpha - 3.f * DEG) - 5.0f * qh, Cl0 = -0.12f * beta - 0.50f * ph + 0.08f * rh, Cn0 = 0.10f * beta - 0.18f * rh;
  Taero = vec3(Cm0 * qbar * s.wingArea * s.chord, -Cn0 * qbar * s.wingArea * b, -Cl0 * qbar * s.wingArea * b);
  // what the control surfaces can give: the elevons (in the field) in pitch and roll, the all-moving fins in yaw -
  // the less of it stalled
  const float ctlEff = 1.f - 0.4f * sig;
  surfMax = vec3(s.elevPow * s.chord * (1.f + 0.5f * circ), s.rudPow * b, s.ailPow * b * blow) * (qbar * s.wingArea * ctlEff);
  stallWarn = smoothstepf(aStall - 5.f * DEG, aStall - 1.5f * DEG, alpha) * (1.f - hyp);
  return true;
}

void Plane::xr60Thrust(vec3& F, vec3& T, vec3 Taero, vec3 surfMax, float V, float dt) {
  const AircraftSpec& s = *spec;
  const bool lit = engineRunning && fuel > 0.f;
  // ---- the surge: lit, it triples the ring until its bank is spent or it is shut off (a tenth of a second either
  // way); off, the bank recharges at a sixth of the rate it spends
  {
    const bool can = surgeOn && surgeBank > 0.f && lit;
    surgeN = approach(surgeN, can ? 1.f : 0.f, 6.f, dt);
    if (!can) surgeOn = false;
    surgeBank = clampf(surgeBank - surgeN * dt / kXr60SurgeSeconds + (surgeOn ? 0.f : kXr60SurgeRecharge * dt / kXr60SurgeSeconds), 0.f, 1.f);
  }
  const Xr60Thrust e = lit ? xr60Drive(s, engineSpool, V, density, soundSpeed, surgeN) : Xr60Thrust{};
  // ---- the lever: the emitters' share. It hands the ring's stream to the emitters - at its stop all of it (no forward
  // thrust, 0.6 of the rated straight down between the four), so the hover is the ring's power turned down. The
  // emitters are symmetric about the centre of gravity: balanced in pitch and roll by construction
  emitShare = approach(emitShare, smoothstepf(0.02f, 0.5f, nozzle), 1.2f, dt);
  const float ringAll = e.ring, emitAll = lit ? kEmitShare * s.engines * s.power * clampf(engineSpool, 0.f, 1.f) * (1.f + kSurgeGain * surgeN) * emitShare : 0.f;
  ringN = ringAll * (1.f - emitShare); scramN = e.scram;
  ringSpin = fmodf(ringSpin + (lit ? 12.f + 134.f * engineSpool : 0.f) * dt, 2.f * PI * 64.f);
  // ---- the fly-by-wire: no one is aboard to spare. The stick commands a pitch rate - up and away the rate of a 500 g
  // pull, slow 8 to 24 rad/s the ring holds past the stall - and roll and yaw rates (2,400 and 600 deg/s: three times
  // the XR-50's); hands off in the hover it holds the attitude level. Its one limit is the airframe's: the rate that
  // holds 95% of the 2,000 g structure either way, less what it is over by
  const float Vt = std::max(V, 1.f);
  const float hover = smoothstepf(0.3f, 0.7f, nozzle) * smoothstepf(70.f, 30.f, V);
  vec3 wd(ctl.pitch * fbwPitchMax(Vt) + ctl.trim * 0.15f, -ctl.yaw * 10.5f, -ctl.roll * fbwRollMax(hover));
  {
    const float gHi = 0.95f * s.gLimitPos(), gLo = 0.95f * s.gLimitNeg();
    wd.x = clampf(wd.x, gLo * G0 / Vt + (gLo - gLoad) * 0.02f, gHi * G0 / Vt + (gHi - gLoad) * 0.02f);
  }
  const float ms0 = mass() / s.emptyMass, kr = onGround ? 4.f : 54.f, kp = onGround ? 4.f : 84.f;   // (three times the XR-50's: it snaps)
  const vec3 I(s.Iyy * ms0, s.Izz * ms0, s.Ixx * ms0);
  // the angle of attack limiter: 35 deg - the wing's lift, the controls whole - until the stick is pulled hard against
  // its stop, through the gate into post-stall flight to 90 deg (where the ring holds what the elevons no longer can);
  // never more than 25 deg negative. It closes the angle's rate in as it nears a limit, to what the pitch authority it
  // has now (the elevons', the ring's) can stop there
  if (V > 15.f && hover < 0.5f) {
    const float aLim = (ctl.pitch > 0.95f ? 90.f : 35.f) * DEG, aNegLim = -25.f * DEG, path = gLoad * G0 / Vt;
    const float brake = std::max(0.3f * (surfMax.x + ringN * sinf(kRingVec) * kXr60Ring.z + kFieldMax.x) / I.x, 1.f);
    auto room = [brake](float d) { return d > 0.f ? sqrtf(2.f * brake * d) : d * 6.f; };
    wd.x = clampf(wd.x, path - room(alpha - aNegLim), path + room(aLim - alpha));
  }
  if (hover > 0) {
    if (fabsf(ctl.pitch) < 0.05f) wd.x += hover * 2.2f * (0.f - pitchDeg()) * DEG;
    if (fabsf(ctl.roll) < 0.05f) wd.z += hover * 2.2f * bankDeg() * DEG;
  }
  // (on its wheels the airframe can't be levered about them until the wing carries some of the weight)
  const float ga = onGround && hover < 0.5f ? smoothstepf(25.f, 55.f, V) : 1.f;
  wd.x *= ga; wd.z *= ga;
  const vec3 tDes(I.x * kp * (wd.x - w.x), I.y * kr * (wd.y - w.y), I.z * kr * (wd.z - w.z));
  // the drive's own moments at its nominal settings are none (the ring fires through the centre of gravity, the
  // emitters balance), so what the controls must make is the whole of it
  vec3 rem = tDes - Taero;
  // 1) the control surfaces, as far as the dynamic pressure gives them
  auto take = [](float n, float mx) { return mx > 1.f ? clampf(n, -mx, mx) : 0.f; };
  const vec3 tSurf(take(rem.x, surfMax.x), take(rem.y, surfMax.y), take(rem.z, surfMax.z));
  surf = vec3(surfMax.x > 1.f ? tSurf.x / surfMax.x : 0.f, surfMax.y > 1.f ? -tSurf.y / surfMax.y : 0.f, surfMax.z > 1.f ? -tSurf.z / surfMax.z : 0.f);
  rem = rem - tSurf;
  // 2) the ring's vectoring: its stream turned in pitch and yaw about the ring's centre, 7 m behind the centre of
  // gravity (the stream down lifts the tail: the nose goes down), within its 35 deg, the petals slewing at 300 deg/s
  {
    const float arm = ringN * kXr60Ring.z;
    const float wantP = arm > 100.f ? -asinf(clampf(rem.x / arm, -0.573f, 0.573f)) : 0.f;   // (asin's argument inside sin 35 deg)
    const float wantY = arm > 100.f ? asinf(clampf(rem.y / arm, -0.573f, 0.573f)) : 0.f;
    ringPitch = approach(ringPitch, clampf(wantP, -kRingVec, kRingVec), 300.f * DEG, dt);
    ringYaw = approach(ringYaw, clampf(wantY, -kRingVec, kRingVec), 300.f * DEG, dt);
  }
  const vec3 fRing = ringDir(ringPitch, ringYaw) * ringN, tRing = cross(kXr60Ring, fRing);
  rem = rem - tRing;
  // 3) the hover's own: the fore and aft emitter pairs trade their thrust for pitch (+-50%), the left and right pairs
  // for roll. On the ground the wheels hold the attitude: they stay at their nominal settings
  float uP = 0.f, uR = 0.f;
  if (!onGround && emitAll > 100.f) {
    uP = clampf(rem.x / (emitAll * -kXr60Emit[0].z), -0.5f, 0.5f); rem.x -= uP * emitAll * -kXr60Emit[0].z;
    uR = clampf(rem.z / (emitAll * kXr60Emit[1].x), -0.5f, 0.5f); rem.z -= uR * emitAll * kXr60Emit[1].x;
  }
  emitN[0] = 0.25f * emitAll * (1.f + uP) * (1.f - uR); emitN[1] = 0.25f * emitAll * (1.f + uP) * (1.f + uR);
  emitN[2] = 0.25f * emitAll * (1.f - uP) * (1.f - uR); emitN[3] = 0.25f * emitAll * (1.f - uP) * (1.f + uR);
  // 4) the field torque: the tips' emitters and the ring's field, with the core at power, make up what is left -
  // everywhere, in any air or none
  const float fieldOn = lit ? 0.5f + 0.5f * clampf(engineSpool, 0.f, 1.f) : 0.f;
  field = vec3(clampf(rem.x / kFieldMax.x, -1.f, 1.f), clampf(rem.y / kFieldMax.y, -1.f, 1.f), clampf(rem.z / kFieldMax.z, -1.f, 1.f)) * fieldOn;
  // ---- the forces and their moments about the centre of gravity
  F += fRing; T += tRing;
  F += vec3(0, 0, -scramN);   // (the scramjet's flow leaves by the keel's expansion ramp)
  for (int i = 0; i < 4; i++) { const vec3 f(0, emitN[i], 0); F += f; T += cross(kXr60Emit[i], f); }
  T += tSurf + vec3(field.x * kFieldMax.x, field.y * kFieldMax.y, field.z * kFieldMax.z);
}
