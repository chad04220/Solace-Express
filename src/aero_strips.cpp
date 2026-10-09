// Solace Express - the strip model (aero.h): the airframe's forces and moments from its drawn shape.
// Methods: strip theory with a Schrenk spanwise loading and lifting-line induced drag; the DATCOM / Helmbold lift slope
// with Prandtl-Glauert below Mach 1 and Ackeret above; thin-aerofoil flap effectiveness with a viscous falloff; a
// first-order separation lag for the stall (Goman-Khrabrov); slender-body (Munk) and crossflow forces on the fuselage;
// actuator-disk slipstream and swirl behind each propeller. The surfaces, their controls and the fuselage are the ones
// models.cpp draws, so what flies is what is seen.
#include "aero.h"
#include "aircraft.h"
#include "models.h"
#include <cstring>
#include <mutex>

namespace {

// the elevator's deflection (rad, trailing edge up for a pull) for a stick and trim of e: further up than down, as a
// light aircraft's (28 deg up, 20 down; plane_parts.glsl elevDefl draws the same)
float elevTravel(float e) { return e * (e > 0.f ? 0.49f : 0.35f); }
// a plain flap's change of angle of attack per radian of deflection (thin aerofoil theory) for its chord fraction
float flapTau(float cf) { float th = acosf(clampf(2.f * cf - 1.f, -1.f, 1.f)); return 1.f - (th - sinf(th)) / PI; }
// the real flap does less than theory, and less again the further it goes (the flow over it separates): DATCOM's
// plain-flap correction, about three quarters of the linear effect at 25-30 degrees, a little over half at 40
float flapEta(float d) { return 0.85f * (1.f - 0.4f * smoothstepf(0.2f, 0.7f, fabsf(d))); }

// a surface's lift slope per radian: DATCOM / Helmbold below Mach 1 (with the half-chord sweep), Ackeret's supersonic
// slope above, joined through the transonic
float liftSlope(float AR, float tanHalf, float M) {
  const float k = 0.95f;   // the section's slope over 2 pi
  auto sub = [&](float m) {
    float b2 = std::max(1.f - m * m, 0.09f);
    return 2.f * PI * AR / (2.f + sqrtf(4.f + AR * AR * b2 / (k * k) * (1.f + tanHalf * tanHalf / b2)));
  };
  auto sup = [&](float m) {
    float bb = sqrtf(std::max(m * m - 1.f, 0.1225f));
    return 4.f / bb * std::max(1.f - 1.f / (2.f * AR * bb), 0.3f);
  };
  if (M < 0.95f) return sub(M);
  if (M > 1.15f) return sup(M);
  float t = (M - 0.95f) / 0.2f;
  return sub(0.95f) * (1.f - t) + sup(1.15f) * t;
}
// ground effect: the share of the induced effects that remains at height h over the ground for span b (McCormick)
float groundFactor(float h, float b) { float x = 16.f * std::max(h, 0.05f) / std::max(b, 0.5f); return x * x / (1.f + x * x); }
// the attached lift's rounding into the stall: linear to 75% of the maximum, then a parabola to it at 1.25
float liftSat(float x) {
  float t = fabsf(x), v = t <= 0.75f ? t : t <= 1.25f ? 0.75f + (t - 0.75f) - (t - 0.75f) * (t - 0.75f) : 1.f;
  return std::copysign(v, x);
}

// a trapezoidal panel of strips: its root's leading edge, its span direction (unit, outboard) and normal, its length,
// root and tip chords, how far aft the tip's leading edge lies, its twist at the tip (linear from the root)
void addPanel(AeroGeom& g, int surf, float side, vec3 rootLE, vec3 spDir, vec3 nrm, float span, float cr, float ct,
              float sweep, float twistTip, int N) {
  const float Sh = span * 0.5f * (cr + ct), ce0 = 4.f * Sh / (PI * span);
  // the quarter-chord line: the flow along it does nothing (simple sweep theory), so a strip's span runs along it and
  // its chord square to it - in a sideslip the wing swept into the wind meets more of the flow, and lifts more
  const vec3 c4 = normalize(spDir * span + vec3(0, 0, sweep + 0.25f * (ct - cr)));
  const vec3 cn = normalize(vec3(0, 0, 1) - c4 * c4.z);
  for (int j = 0; j < N && g.nSt < AeroGeom::kMaxStrips; j++) {
    float eta = (j + 0.5f) / N, chord = cr + (ct - cr) * eta;
    AeroStrip& st = g.st[g.nSt++];
    vec3 le = rootLE + spDir * (eta * span) + vec3(0, 0, sweep * eta);
    st.r = le + vec3(0, 0, 0.25f * chord);
    st.c = cn; st.sp = c4; st.n = nrm; st.cosSw = dot(c4, spDir);
    st.chord = chord; st.area = chord * span / N;
    st.twist = twistTip * eta;
    float ce = ce0 * sqrtf(std::max(1.f - eta * eta, 0.f));
    st.load = 0.5f * (chord + ce) / chord;   // (Schrenk: the planform's loading and the ellipse's, averaged)
    st.side = side; st.surf = surf;
  }
}
// marks the strips of [first, end) whose span (from the panel's root, along it) lies in [s0, s1] with a control
void addControl(AeroGeom& g, int first, int end, vec3 rootLE, vec3 spDir, float span, int N, float s0, float s1, int kind, float cf) {
  for (int i = first; i < end; i++) {
    float a = span * (i - first) / N, b = span * (i - first + 1) / N;
    float cov = std::max(0.f, std::min(b, s1) - std::max(a, s0)) / (b - a);
    if (cov > 0.05f && cov >= g.st[i].cover) { g.st[i].ctl = kind; g.st[i].cf = cf; g.st[i].cover = cov; }
  }
  (void)rootLE; (void)spDir;
}

AeroIn steadyIn(float V, float rho, float alpha) {
  AeroIn in; in.steady = true;
  in.va = vec3(0, -V * sinf(alpha), -V * cosf(alpha));
  in.rho = rho; Atmosphere at = isa(1500.f); in.a = at.a; in.mu = at.mu;
  return in;
}
// the lift (perpendicular to the flow in the plane of symmetry) and the pitching moment of a steady evaluation
float liftOf(const AeroOut& o) { return o.F.y * cosf(o.alpha) - o.F.z * sinf(o.alpha); }

AeroGeom build(const AircraftSpec& s, int idx) {
  AeroGeom g;
  const ModelDef& m = kModels[idx];
  const bool jet = s.engineType == ENG_JET, fast = s.designMach > 1.f;
  // ---------------- the wing: ten strips a side, washed out towards the tips (a jet's more, the supersonic types' not)
  const float wb = m.wing[0], wcr = m.wing[1], wct = m.wing[2], wsw = m.wing[3], wy = m.wing[4], wz = m.wing[5];
  const float dih = m.wing[6] * DEG, washout = fast ? 0.f : jet ? -3.f * DEG : -2.f * DEG;
  const int NW = 10;
  for (int side = -1; side <= 1; side += 2) {
    vec3 sp(side * cosf(dih), sinf(dih), 0), n(-sinf(dih) * side, cosf(dih), 0);
    int first = g.nSt;
    addPanel(g, AS_WING, (float)side, vec3(0, wy, wz), sp, n, wb, wcr, wct, wsw, washout, NW);
    // flaps from the fuselage's side to their share of the span, ailerons outboard of them to 94% (the drawn surfaces:
    // plane_parts.glsl partPoseCockpit, sdSurface)
    float flapEnd = wb * m.flapFrac, flap0 = 0.55f * s.fusRad;
    addControl(g, first, g.nSt, vec3(0, wy, wz), sp, wb, NW, flap0, flapEnd, AC_FLAP, 0.26f);
    addControl(g, first, g.nSt, vec3(0, wy, wz), sp, wb, NW, flapEnd + 0.03f, wb * 0.94f, AC_AIL, 0.26f);
    for (int i = first; i < g.nSt; i++) {   // the fuselage carries the lift through where the wing passes it
      AeroStrip& st = g.st[i];
      st.cm0 = fast ? -0.01f : -0.05f;
      if (fabsf(st.r.x) < modelHalfWidth(m, st.r.z)) st.bodyF = 0.85f;
    }
  }
  {
    AeroSurface& w = g.surf[AS_WING];
    w.first = 0; w.count = g.nSt; w.b = 2.f * wb * cosf(dih); w.S = 2.f * wb * 0.5f * (wcr + wct);
    w.AR = w.b * w.b / w.S; w.sweep = (wsw + 0.5f * (wct - wcr)) / wb;   // (tan of the half-chord sweep)
    w.ac = vec3(0, wy, wz + 0.25f * wcr + 0.4f * wsw);
    w.e = aeroModel(s).e;
  }
  g.wingArea = g.surf[AS_WING].S;
  const float lam = wct / std::max(wcr, 0.01f);
  g.MAC = 2.f / 3.f * wcr * (1.f + lam + lam * lam) / (1.f + lam);
  // ---------------- the horizontal tail, or the XR-20's canards (its "tail" drawn ahead of the wing)
  if (m.ht[0] > 0.f) {
    float hy = m.ht[4], hz = m.ht[5];
    if (m.ttail) { hy = m.vt[4] + m.vt[0] - 0.05f; hz = m.vt[5] + m.vt[3] + (m.vt[2] - m.ht[1]) * 0.5f; }
    const int kind = hz < wz ? AS_CANARD : AS_TAIL;
    const float hd = m.ht[6] * DEG, NT = 4;
    const int first0 = g.nSt;
    for (int side = -1; side <= 1; side += 2) {
      vec3 sp(side * cosf(hd), sinf(hd), 0), n(-sinf(hd) * side, cosf(hd), 0);
      int first = g.nSt;
      addPanel(g, kind, (float)side, vec3(0, hy, hz), sp, n, m.ht[0], m.ht[1], m.ht[2], m.ht[3], 0.f, (int)NT);
      addControl(g, first, g.nSt, vec3(0, hy, hz), sp, m.ht[0], (int)NT, 0.12f, m.ht[0] * 0.98f, AC_ELEV, 0.32f);
      // (a tail's section is symmetric; a canard's is cambered for lift - it carries part of the weight, and has to
      // keep lifting to the wing's stall)
      for (int i = first; i < g.nSt; i++) { g.st[i].clMax = kind == AS_CANARD ? 1.6f : 1.2f; g.st[i].cm0 = kind == AS_CANARD ? -0.06f : 0.f; }
    }
    AeroSurface& t = g.surf[kind];
    t.first = first0; t.count = g.nSt - first0; t.b = 2.f * m.ht[0] * cosf(hd); t.S = m.ht[0] * (m.ht[1] + m.ht[2]);
    t.AR = t.b * t.b / t.S; t.sweep = (m.ht[3] + 0.5f * (m.ht[2] - m.ht[1])) / m.ht[0]; t.e = 0.85f;
    t.ac = vec3(0, hy, hz + 0.25f * m.ht[1] + 0.4f * m.ht[3]);
    g.tailArm = fabsf(t.ac.z - g.surf[AS_WING].ac.z);
    g.kEps = kind == AS_CANARD ? -0.25f : m.ttail ? 0.75f : 1.f;   // (a canard sits in the wing's upwash)
    g.etaTail = kind == AS_CANARD ? 1.f : m.ttail ? 0.98f : 0.92f;
  }
  // ---------------- the fin (the XR-20's two, canted out from its shoulders)
  if (m.vt[0] > 0.f) {
    const float h = m.vt[0], rud0 = m.ttail ? 0.05f : 0.08f * h;
    const int first0 = g.nSt, NF = 4;
    if (idx == kMantis) {
      const float cant = 0.48f, fx = 1.05f;
      for (int side = -1; side <= 1; side += 2) {
        vec3 sp(side * sinf(cant), cosf(cant), 0), n(cosf(cant), -side * sinf(cant), 0);
        int first = g.nSt;
        addPanel(g, AS_FIN, (float)side, vec3(side * fx, m.vt[4], m.vt[5]), sp, n, h, m.vt[1], m.vt[2], m.vt[3], 0.f, NF);
        addControl(g, first, g.nSt, vec3(), sp, h, NF, rud0, h * 0.97f, AC_RUD, 0.34f);
      }
    } else {
      int first = g.nSt;
      addPanel(g, AS_FIN, 0.f, vec3(0, m.vt[4], m.vt[5]), vec3(0, 1, 0), vec3(1, 0, 0), h, m.vt[1], m.vt[2], m.vt[3], 0.f, NF);
      addControl(g, first, g.nSt, vec3(), vec3(0, 1, 0), h, NF, rud0, h * 0.97f, AC_RUD, 0.34f);
    }
    for (int i = first0; i < g.nSt; i++) g.st[i].clMax = 1.2f;
    AeroSurface& f = g.surf[AS_FIN];
    const int nf = idx == kMantis ? 2 : 1;
    f.first = first0; f.count = g.nSt - first0; f.b = h; f.S = nf * h * 0.5f * (m.vt[1] + m.vt[2]);
    f.AR = (m.ttail ? 1.9f : 1.55f) * h * h / (f.S / nf);   // (the fuselage and the tailplane end-plate it)
    f.sweep = (m.vt[3] + 0.5f * (m.vt[2] - m.vt[1])) / h; f.e = 0.8f;
    f.ac = vec3(0, m.vt[4] + 0.45f * h, m.vt[5] + 0.25f * m.vt[1] + 0.45f * m.vt[3]);
  }
  // ---------------- the fuselage, cut along its length
  {
    const float z0 = m.st[0][0], z1 = m.st[7][0];
    const int NS = 14;
    auto areaAt = [&](float z) { float hw, hh, cy; modelSection(m, z, hw, hh, cy); return hw * hh * (PI * m.roundness + 4.f * (1.f - m.roundness)); };
    for (int k = 0; k < NS; k++) {
      float za = z0 + (z1 - z0) * k / NS, zb = z0 + (z1 - z0) * (k + 1) / NS, zc = 0.5f * (za + zb);
      float hw, hh, cy; modelSection(m, zc, hw, hh, cy);
      AeroSegment& sg = g.seg[g.nSeg++];
      sg.z = zc; sg.len = zb - za; sg.area = areaAt(zc); sg.dA = areaAt(zb) - areaAt(za); sg.w = 2.f * hw; sg.h = 2.f * hh; sg.y = cy;
      g.fusVol += sg.area * sg.len;
    }
    for (int i = 0; i < g.nSt; i++) {   // (each strip's fuselage section: the crossflow round it reaches the strips near it)
      AeroStrip& st = g.st[i];
      if (st.r.z < z0 || st.r.z > z1) continue;
      float hw, hh, cy; modelSection(m, st.r.z, hw, hh, cy);
      if (hw > 0.02f && hh > 0.02f) { st.fy = cy; st.fhw = hw; st.fhh = hh; }
    }
    float zv = 0; for (int k = 0; k < g.nSeg; k++) zv += g.seg[k].z * g.seg[k].area * g.seg[k].len;
    zv /= std::max(g.fusVol, 1e-3f);
    float hw, hh, cy; modelSection(m, zv, hw, hh, cy);
    g.bodyDragAt = vec3(0, cy, zv);
  }
  // ---------------- the engines: propellers where the model has them, the jets' nozzles
  {
    float pr[2][4]; int np = modelProps(m, pr);
    const int blades = std::max(s.blades, 2);
    for (int i = 0; i < np && i < s.engines && g.nEng < 4; i++) {
      AeroEngine& e = g.eng[g.nEng++];
      e.pos = vec3(pr[i][0], pr[i][1], pr[i][2]); e.fwd = vec3(0, 0, -1); e.R = pr[i][3]; e.prop = true; e.rot = 1.f;
      e.Ip = blades * 6.f * e.R * e.R * e.R / 3.f;   // (blades of about 6 kg per metre of radius cubed: 2.6 kg m^2 for a trainer's)
    }
    if (np == 0) {
      const int nj = std::max(1, std::min(s.engines, 2));
      for (int i = 0; i < nj && g.nEng < 4; i++) {
        AeroEngine& e = g.eng[g.nEng++];
        // (a pair buried in the fuselage - no nacelles drawn apart - side by side, a third of its half width out)
        const float z = m.nacZ0 + 0.8f * m.nacLen;
        float x = nj == 1 ? 0.f : (i ? 1.f : -1.f) * (m.nacX >= 0.05f ? m.nacX : 0.33f * modelHalfWidth(m, z));
        e.pos = vec3(x, m.nacY, z); e.fwd = vec3(0, 0, -1); e.prop = false;
      }
    }
  }
  return g;
}

// ---------------- calibration: the type's published numbers on its drawn shape
void calibrate(AeroGeom& g, const AircraftSpec& s, int idx) {
  AeroMem mem;
  const Atmosphere a15 = isa(1500.f);
  const float Sm = g.wingArea, scale = s.wingArea / std::max(Sm, 1e-3f);
  AeroOut o;
  // (the propellers' shaft power for a thrust at a speed: the thrust model's 80% efficiency; their speed at cruise)
  const float omegaCruise = (s.engineType == ENG_PISTON ? 0.85f : 1.f) * s.maxRpm * 2.f * PI / 60.f;
  float roll = 0.f, yaw = 0.f;
  auto eval = [&](float V, float rho, float alpha, float flaps, float pitch, float* thrust) {
    AeroIn in = steadyIn(V, rho, alpha); in.flapL = in.flapR = flaps; in.pitch = pitch; in.roll = roll; in.yaw = yaw;
    if (thrust) for (int e = 0; e < g.nEng; e++) {
      in.thrust[e] = thrust[e];
      if (g.eng[e].prop) { in.power[e] = thrust[e] * V / 0.8f; in.omega[e] = omegaCruise; }
    }
    mem.init = false;
    aeroForces(g, s, in, mem, 0.f, o);
    return o;
  };
  // the wing's own lift at zero angle of attack (the type's CL0): its setting over its zero-lift angle
  for (int w = g.surf[AS_WING].first; w < g.surf[AS_WING].first + g.surf[AS_WING].count; w++) g.st[w].clMax = s.CLmax * scale * 1.15f;
  g.wingInc = 2.f * DEG; g.tailInc = 0;
  for (int it = 0; it < 3; it++) {
    eval(60.f, 1.225f, 0.f, 0.f, 0.f, nullptr);
    float clw = o.CLw, slope = liftSlope(g.surf[AS_WING].AR, g.surf[AS_WING].sweep, 0.15f) * 0.95f;
    g.wingInc += (s.CL0 * scale - clw) / std::max(slope, 1.f);
  }
  // the flaps: the type's lift increment at full flap
  g.flapA = 0.2f;
  for (int it = 0; it < 3 && s.flapCL > 0.01f; it++) {
    float l0 = liftOf(eval(40.f, 1.225f, 2.f * DEG, 0.f, 0.f, nullptr)), l1 = liftOf(eval(40.f, 1.225f, 2.f * DEG, 1.f, 0.f, nullptr));
    float dcl = (l1 - l0) / (0.5f * 1.225f * 1600.f * Sm);
    if (dcl > 1e-3f) g.flapA *= clampf(s.flapCL * scale / dcl, 0.3f, 3.f);
  }
  if (s.flapCL <= 0.01f) g.flapA = 0.f;
  // (and their drag: the type's at full flap, on its wing - the deflected flap's own profile drag, about sin^2 of its
  // angle, and the induced drag of the lift crowded inboard - square in the deflection. A generic 0.4 left the
  // full-flap drag a third to a fifth of it: the Bushmaster dived its final at idle, nose 13 deg down, gathering speed)
  {
    float fa = 0; for (int i = 0; i < g.nSt; i++) if (g.st[i].ctl == AC_FLAP) fa += g.st[i].area * g.st[i].cover * 1.1f;
    if (fa > 0.f && g.flapA > 1e-3f && s.flapCD > 0.f) g.flapCdK = s.flapCD * s.wingArea / (fa * g.flapA * g.flapA);
  }
  // the neutral point (with the centre of gravity at the origin for now): where the lift's change acts
  g.cg = vec3();
  // (subsonic, at most Mach 0.5: past Mach 1 the lift's centre moves aft and the margin only grows - where it is
  // smallest is where the centre of gravity has to answer for it)
  auto neutralPoint = [&]() {
    const float V = std::min(s.cruise, 0.5f * a15.a), rho = a15.rho;
    eval(V, rho, 1.f * DEG, 0.f, 0.f, nullptr); float l0 = o.F.y, m0 = o.M.x; vec3 c0 = g.cg;
    eval(V, rho, 3.f * DEG, 0.f, 0.f, nullptr); float l1 = o.F.y, m1 = o.M.x;
    // M about the CG: M_x = M0 - (z - cg.z) L  ->  the neutral point z = cg.z - dM/dL
    return c0.z - (m1 - m0) / std::max(l1 - l0, 1e-3f);
  };
  // the centre of gravity a static margin ahead of the neutral point, the tail set so it flies hands-off at cruise
  // (1500 m, the reference weight, the elevator neutral and the thrust that holds the speed along the engines' lines),
  // and the rigging that flies it straight there
  const float W = (s.emptyMass + s.maxFuel * 0.6f + s.cargoKg * 0.5f) * G0;
  float thr[4] = {};
  auto placeCG = [&](float sm) {
    // (subsonic, at most Mach 0.6: a supersonic type trims its cruise with the elevator - set for Mach 1, the XR-20's
    // canard sat so steep that any up elevator stalled it when slow)
    const float V = std::min(s.cruise, 0.6f * a15.a), rho = a15.rho;
    float alpha = 2.f * DEG;
    for (int pass = 0; pass < 2; pass++) {   // (the neutral point moves a little with the tail's load: twice round)
      g.xNP = neutralPoint();
      float hw, hh, cy; modelSection(kModels[idx], g.xNP - sm * g.MAC, hw, hh, cy);
      g.cg = vec3(0, cy - 0.15f * hh, g.xNP - sm * g.MAC);
      for (int it = 0; it < 12; it++) {
        for (int k = 0; k < 3; k++) {   // the angle of attack for the lift (secant)
          float l0 = liftOf(eval(V, rho, alpha, 0.f, 0.f, thr)), l1 = liftOf(eval(V, rho, alpha + 0.01f, 0.f, 0.f, thr));
          alpha = clampf(alpha + (W - l0) * 0.01f / std::max(l1 - l0, 1.f), -6.f * DEG, 14.f * DEG);
        }
        eval(V, rho, alpha, 0.f, 0.f, thr);
        const float net = o.F.z * cosf(alpha) + o.F.y * sinf(alpha);   // (what's left along the flight path, aft: the thrust falls short by it)
        for (int e = 0; e < g.nEng; e++) thr[e] = std::max(thr[e] + net / std::max(g.nEng, 1), 0.f);
        float m0 = o.M.x;
        g.tailInc += 0.01f; eval(V, rho, alpha, 0.f, 0.f, thr); float m1 = o.M.x; g.tailInc -= 0.01f;
        if (fabsf(m1 - m0) > 1e-3f) g.tailInc = clampf(g.tailInc - m0 * 0.01f / (m1 - m0), -12.f * DEG, 12.f * DEG);
      }
    }
    g.trimV = V; g.trimAlpha = alpha;
    // the rigging: the aileron tab and the rudder offset that fly it straight, wings level and hands-off there (the
    // propeller's torque rolls it, its slipstream's swirl on the fin yaws it; a jet's needs none)
    g.ailRig = g.rudRig = 0.f;
    for (int it = 0; it < 3; it++) {
      eval(V, rho, alpha, 0.f, 0.f, thr); const vec3 m0 = o.M;
      roll += 0.05f; eval(V, rho, alpha, 0.f, 0.f, thr); const vec3 ma = o.M; roll -= 0.05f;
      yaw += 0.05f; eval(V, rho, alpha, 0.f, 0.f, thr); const vec3 mr = o.M; yaw -= 0.05f;
      // [d roll moment, d yaw moment] per unit of aileron and of rudder: solve for both at once
      const float a11 = (ma.z - m0.z) / 0.05f, a12 = (mr.z - m0.z) / 0.05f, a21 = (ma.y - m0.y) / 0.05f, a22 = (mr.y - m0.y) / 0.05f;
      const float det = a11 * a22 - a12 * a21;
      if (fabsf(det) < 1e-6f) break;
      roll = clampf(roll + (-m0.z * a22 + m0.y * a12) / det, -0.2f, 0.2f);
      yaw = clampf(yaw + (-m0.y * a11 + m0.z * a21) / det, -0.2f, 0.2f);
    }
    g.ailRig = roll; g.rudRig = yaw; roll = yaw = 0.f;
  };
  // (the light types' 14%, the turboprops' 12%, the jets' 10%, the supersonic research types' 6%)
  const float smNominal = s.designMach > 1.f ? 0.06f : s.engineType == ENG_JET ? 0.10f : s.engineType == ENG_TURBOPROP ? 0.12f : 0.14f;
  placeCG(smNominal);
  // the maximum lift: the type's CLmax, flaps up, elevator neutral, on the drawn wing (its stall speeds are the type's)
  for (int it = 0; it < 3; it++) {
    float best = 0;
    for (float a = 0.f; a < 26.f * DEG; a += 0.5f * DEG) best = std::max(best, liftOf(eval(40.f, 1.225f, a, 0.f, 0.f, nullptr)));
    float cl = best / (0.5f * 1.225f * 1600.f * Sm);
    if (cl > 0.05f) for (int w = g.surf[AS_WING].first; w < g.surf[AS_WING].first + g.surf[AS_WING].count; w++) g.st[w].clMax *= clampf(s.CLmax * scale / cl, 0.7f, 1.4f);
  }
  // a hair less on the left wing (no two wings are made alike): the stall drops one wing, the same one each time
  for (int w = g.surf[AS_WING].first; w < g.surf[AS_WING].first + g.surf[AS_WING].count; w++) if (g.st[w].side < 0) g.st[w].clMax *= 0.99f;
  // the approach: with full flap at 1.3 Vs0 the elevator must still trim it with room to spare (60% of its travel)
  // Where the drawn tail can't, the masses go further aft - down to a 6% margin - as a designer would ballast it
  {
    const float rho = 1.225f;
    auto trimElevator = [&](float k, float fl) {   // (at k x the stall speed with these flaps)
      const float V = k * sqrtf(2.f * W / (rho * s.wingArea * (s.CLmax + s.flapCL * fl)));
      float alpha = 4.f * DEG, el = 0.f;
      for (int it = 0; it < 25; it++) {
        auto f = [&](float a, float e) {
          AeroIn in = steadyIn(V, rho, a); in.flapL = in.flapR = fl; in.pitch = e;
          mem.init = false; aeroForces(g, s, in, mem, 0.f, o);
          return vec3(liftOf(o), o.M.x, 0.f);
        };
        vec3 r0 = f(alpha, el), ra = f(alpha + 0.01f, el), re = f(alpha, el + 0.02f);
        float a11 = (ra.x - r0.x) / 0.01f, a12 = (re.x - r0.x) / 0.02f, a21 = (ra.y - r0.y) / 0.01f, a22 = (re.y - r0.y) / 0.02f, det = a11 * a22 - a12 * a21;
        if (fabsf(det) < 1e-6f) break;
        alpha += clampf(((W - r0.x) * a22 + r0.y * a12) / det, -0.05f, 0.05f);
        el = clampf(el + clampf((-r0.y * a11 - (W - r0.x) * a21) / det, -0.2f, 0.2f), -1.5f, 1.5f);
      }
      // (only a balance counts: lift within 2% of the weight, the moment within a hundredth of the weight's over the
      // mean chord - an elevator that can't trim it doesn't converge to anything)
      AeroIn in = steadyIn(V, rho, alpha); in.flapL = in.flapR = fl; in.pitch = el;
      mem.init = false; aeroForces(g, s, in, mem, 0.f, o);
      if (fabsf(liftOf(o) - W) > 0.02f * W || fabsf(o.M.x) > 0.01f * W * g.MAC) return 9.f;
      return el;
    };
    const float flaps = s.flapCL > 0.01f ? 1.f : 0.f;
    // (and slowed to 1.1 Vs0 - the flare, the approach to the stall - with a little still to spare)
    auto trims = [&]() { return trimElevator(1.3f, flaps * g.flapMax) < 0.6f && trimElevator(1.1f, flaps * g.flapMax) < 0.92f; };
    for (float sm = smNominal; !trims() && sm > 0.065f; ) { sm = std::max(sm - 0.01f, 0.06f); placeCG(sm); }
    // and where even that won't do (a canard ahead of a flapped wing), the flaps go only as far as the approach trims
    if (flaps > 0.f) {
      while (g.flapMax > 0.05f && trimElevator(1.3f, g.flapMax) >= 0.6f) g.flapMax = std::max(g.flapMax - 0.1f, 0.f);
      if (g.flapMax < 0.05f) g.flapMax = 0.f;
    }
    // the most lift it flies at, trimmed: at each angle of attack the elevator that balances it (bisected; an angle
    // it can't balance it can't fly at), the best lift of those. The published stall speeds are trimmed ones, so the
    // wing's sections are set to give the type's CLmax so (the tail's download costs a few percent) - but by no more
    // than 15%: a canard that stalls before the wing (it is meant to: the nose drops first) sets a lower limit of
    // its own, and the autopilot's stall speeds come from it (aircraft_perf.cpp)
    auto trimmedCLmax = [&](float fl) {
      const float V = 40.f, qS = 0.5f * rho * V * V * s.wingArea;
      float best = 0.f;
      for (float a = 0.f; a < 26.f * DEG; a += 0.5f * DEG) {
        auto m = [&](float e) { AeroIn in = steadyIn(V, rho, a); in.flapL = in.flapR = fl; in.pitch = e; mem.init = false; aeroForces(g, s, in, mem, 0.f, o); return o.M.x; };
        float lo = -1.f, hi = 1.f, mlo = m(lo), mhi = m(hi);
        if ((mlo > 0.f) == (mhi > 0.f)) continue;
        for (int i = 0; i < 12; i++) { float mid = 0.5f * (lo + hi), mm = m(mid); if ((mm > 0.f) == (mlo > 0.f)) { lo = mid; mlo = mm; } else hi = mid; }
        m(0.5f * (lo + hi));
        best = std::max(best, liftOf(o) / qS);
      }
      return best;
    };
    {
      float total = 1.f;
      for (int it = 0; it < 3; it++) {
        const float cl = trimmedCLmax(0.f);
        if (cl < 0.05f) break;
        const float k = clampf(s.CLmax / cl, 1.f / total * 0.95f, 1.15f / total);
        total *= k;
        for (int w = g.surf[AS_WING].first; w < g.surf[AS_WING].first + g.surf[AS_WING].count; w++) g.st[w].clMax *= k;
      }
    }
    g.clMaxTrim[0] = trimmedCLmax(0.f) * 1.005f;   // (a hair over: the wing's own CLmax is the limit where it's met)
    g.clMaxTrim[1] = trimmedCLmax(flaps * g.flapMax) * 1.005f;
    // (flaps that leave it less lift it can trim to than it has clean only cost it: they stay up)
    if (g.flapMax > 0.f && g.clMaxTrim[1] < g.clMaxTrim[0]) { g.flapMax = 0.f; g.clMaxTrim[1] = g.clMaxTrim[0]; }
  }
  // the dihedral effect at 1.3 Vs1 clean: a wing whose own is weaker than a light aircraft's (Cl_beta -0.05 per
  // radian) gets a sideslip-to-aileron interconnect that makes up the difference
  {
    const float rho = 1.225f, V = 1.3f * sqrtf(2.f * W / (rho * s.wingArea * s.CLmax)), qSb = 0.5f * rho * V * V * s.wingArea * s.span;
    float a = 0.f;
    for (int it = 0; it < 8; it++) {   // (the angle of attack for the weight)
      float l0 = liftOf(eval(V, rho, a, 0.f, 0.f, nullptr)), l1 = liftOf(eval(V, rho, a + 0.01f, 0.f, 0.f, nullptr));
      a = clampf(a + (W - l0) * 0.01f / std::max(l1 - l0, 1.f), -4.f * DEG, 14.f * DEG);
    }
    auto rollMoment = [&](float beta, float ail) {
      AeroIn in = steadyIn(V, rho, a); in.va = vec3(V * sinf(beta), -V * sinf(a) * cosf(beta), -V * cosf(a) * cosf(beta)); in.roll = ail;
      mem.init = false; aeroForces(g, s, in, mem, 0.f, o); return -o.M.z / qSb;   // (+: rolling right)
    };
    const float l0 = rollMoment(0.f, 0.f), clb = (rollMoment(0.05f, 0.f) - l0) / 0.05f, cla = (rollMoment(0.f, 0.2f) - l0) / 0.2f;
    if (clb > -0.05f && cla > 1e-4f) g.betaToAil = (clb + 0.05f) / cla;
  }
  // the glide: the best lift over drag with the engines stopped, gear up and down (at the reference weight, sea level -
  // the ratio hardly changes with either)
  for (int gearDown = 0; gearDown < 2; gearDown++) {
    float best = 0.f;
    for (float a = 0.f; a <= 10.f * DEG; a += 0.5f * DEG) {
      float V = 40.f;
      for (int it = 0; it < 6; it++) {   // (the speed that carries the weight at this angle)
        AeroIn in = steadyIn(V, 1.225f, a); in.gear = (float)gearDown;
        for (int e = 0; e < g.nEng; e++) in.dead[e] = true;
        mem.init = false; aeroForces(g, s, in, mem, 0.f, o);
        const float L = liftOf(o);
        if (L > 1.f) V = clampf(V * sqrtf(W / L), 10.f, 400.f);
      }
      const float L = liftOf(o), D = o.F.z * cosf(a) + o.F.y * sinf(a);
      if (D > 1e-3f) best = std::max(best, L / D);
    }
    g.glideLD[gearDown] = clampf(best, 3.f, 30.f);
  }
  // the margin now, with the centre of gravity where it is
  g.xNP = neutralPoint();
  g.staticMargin = (g.xNP - g.cg.z) / g.MAC;
}

}  // namespace

const AeroGeom& aeroGeom(const AircraftSpec& s) {
  static AeroGeom cache[16]; static bool built[16] = {};
  static std::recursive_mutex mtx;
  std::lock_guard<std::recursive_mutex> lk(mtx);
  int idx = (int)(&s - kAircraft);
  if (idx < 0 || idx >= kNumAircraft + 4) {
    // a copy of a type's spec (a variant under test): its type's drawn airframe, with the copy's own numbers (engines,
    // power, weights), built afresh while the copy changes
    struct Var { const AircraftSpec* p = nullptr; AircraftSpec spec{}; AeroGeom g; };
    static Var var[8]; static int next = 0;
    int ti = -1;
    for (int i = 0; i < kNumAircraft + 4 && ti < 0; i++) if (s.id && kAircraft[i].id && strcmp(s.id, kAircraft[i].id) == 0) ti = i;
    if (ti < 0) { static AeroGeom none; return none; }
    for (Var& v : var) if (v.p == &s && memcmp(&v.spec, &s, sizeof(s)) == 0) return v.g;
    Var& v = var[next]; next = (next + 1) % 8;
    v.p = &s; memcpy(&v.spec, &s, sizeof(s));
    v.g = build(s, ti); calibrate(v.g, s, ti);
    return v.g;
  }
  if (!built[idx]) {
    cache[idx] = build(s, idx); calibrate(cache[idx], s, idx); built[idx] = true;
    // the gear's drag where the gear stands (its stations are set for this centre of gravity: aircraft.cpp)
    const GearStations gst = gearStations(s);
    cache[idx].gearAt = vec3(0, -s.fusRad - 0.35f, 0.7f * gst.mainZ + 0.3f * (s.taildragger ? gst.tailZ : gst.noseZ));
  }
  return cache[idx];
}

float aeroCLmaxFlown(const AircraftSpec& s, int flaps) {
  const float fl = flaps ? (s.special ? 1.f : aeroGeom(s).flapMax) : 0.f, wing = s.CLmax + s.flapCL * fl;
  return s.special ? wing : std::min(wing, aeroGeom(s).clMaxTrim[flaps ? 1 : 0]);
}

void aeroForces(const AeroGeom& g, const AircraftSpec& s, const AeroIn& in, AeroMem& mem, float dt, AeroOut& out) {
  out = AeroOut();
  const float V = length(in.va);
  // (standing still the engines still pull and their slipstreams still blow over the tail: no early way out)
  if (V > 0.5f) { out.alpha = atan2f(-in.va.y, -in.va.z); out.beta = asinf(clampf(in.va.x / V, -1.f, 1.f)); }
  out.qbar = 0.5f * in.rho * V * V;
  const float M = V / in.a, rho = in.rho;
  const vec3 uHat = V > 1e-3f ? in.va * (-1.f / V) : vec3();   // the air's direction past the airframe
  if (!mem.init) { for (int i = 0; i < g.nSt; i++) mem.f[i] = 1.f; mem.eps = 0; mem.clw = 0; mem.init = true; }
  auto gustAt = [&](vec3 r) { return vec3(dot(in.gx, r), dot(in.gy, r), dot(in.gz, r)); };   // the air's own velocity there, over the CG's
  vec3 F(0, 0, 0), Mo(0, 0, 0);
  const AeroModel& am = aeroModel(s);
  float dq[4]; aeroDragAreas(am, s, V, rho, in.mu, M, dq);
  // ---------------- the surfaces' lift slopes (aspect ratio, sweep, Mach, the ground under them)
  float slope[AS_COUNT] = {}, ge[AS_COUNT] = {}, cdp[AS_COUNT] = {};
  for (int k = 0; k < AS_COUNT; k++) {
    const AeroSurface& sf = g.surf[k];
    if (sf.count == 0) continue;
    ge[k] = k == AS_FIN ? 1.f : groundFactor(in.agl + sf.ac.y, sf.b);
    slope[k] = liftSlope(sf.AR / std::max(ge[k], 0.05f), sf.sweep, M);
  }
  cdp[AS_WING] = dq[0] / std::max(g.surf[AS_WING].S, 0.1f);
  cdp[AS_TAIL] = cdp[AS_CANARD] = dq[1] / std::max(g.surf[AS_TAIL].S + g.surf[AS_CANARD].S, 0.1f);
  cdp[AS_FIN] = dq[2] / std::max(g.surf[AS_FIN].S, 0.1f);
  // ---------------- the propellers' slipstreams (actuator disk) and their swirl
  struct Wash { vec3 pos, aft, fwd; float R, dv, vth, rot; } wash[4]; int nw = 0;
  for (int e = 0; e < g.nEng; e++) {
    const AeroEngine& en = g.eng[e];
    if (!en.prop) continue;
    vec3 u = (in.va + cross(in.w, en.pos)) * -1.f;
    float vax = std::max(dot(u, en.fwd * -1.f), 0.f), A = PI * en.R * en.R, T = std::max(in.thrust[e], 0.f);
    float dv = sqrtf(vax * vax + 2.f * T / (rho * A)) - vax;
    float Q = in.omega[e] > 1.f ? in.power[e] / in.omega[e] : 0.f;
    float vth = Q / (rho * A * std::max(vax + 0.5f * dv, 3.f) * 0.7f * en.R);
    wash[nw++] = {en.pos, en.fwd * -1.f, en.fwd, en.R, dv, std::min(vth, 0.5f * dv + 2.f), en.rot};
  }
  // ---------------- pass 1: each strip's flow and lift coefficient
  struct Local { vec3 up, uh; float q, cl, x, f, aPlate, dctl, cover; } loc[AeroGeom::kMaxStrips];
  float CLs[AS_COUNT] = {}, maxX = 0;
  const float wingCLlag = mem.clw;   // (the downwash at the tail is the wing's lift of a moment ago: it takes tail arm / V to get there)
  const float eps = g.kEps * 2.f * wingCLlag / (PI * std::max(g.surf[AS_WING].AR, 1.f)) * ge[AS_WING];
  const float elev = clampf(in.pitch + in.trim * 0.3f, -1.f, 1.f);
  for (int pass = 0; pass < 2; pass++) {   // (the wing first: in a steady evaluation the tail meets the downwash of this lift)
    for (int i = 0; i < g.nSt; i++) {
      const AeroStrip& st = g.st[i];
      if ((pass == 0) != (st.surf == AS_WING)) continue;
      vec3 u = (in.va + cross(in.w, st.r) - gustAt(st.r)) * -1.f;
      if (st.fhw > 0.f) {
        // the fuselage turns the crossflow round it (potential flow round a cylinder of its section's size): in a
        // sideslip the air coming over the top lifts a high wing's upwind root and pushes down the other (a low wing's
        // the other way round - why low wings carry more dihedral), it swirls round the fin's root, and in a climb it
        // washes up round the wing's roots
        const vec3 ra(0, st.fy, st.r.z);
        const vec3 ua = (in.va + cross(in.w, ra) - gustAt(ra)) * -1.f;
        float dx = st.r.x, dy = st.r.y - st.fy;
        if ((dx * dx) / (st.fhw * st.fhw) + (dy * dy) / (st.fhh * st.fhh) > 1.f) {
          const float R2 = st.fhw * st.fhh, d2 = std::max(dx * dx + dy * dy, R2), k = R2 / (d2 * d2);
          const float A = k * (dx * dx - dy * dy), B = k * 2.f * dx * dy;
          u.x += -ua.x * A - ua.y * B; u.y += -ua.x * B + ua.y * A;
        }
      }
      // in a slipstream: faster air, turning with the propeller. The swirl turns the flow over only the propeller's
      // width of the surface: a patch that narrow answers as a wing of its own small aspect ratio would (its tip
      // vortices take most of the change back) - the whole surface's slope on it took more swirl out of the stream
      // than the propeller's torque ever put in, and rolled a high wing against the torque
      vec3 sw(0, 0, 0); float swk = 1.f;
      for (int k = 0; k < nw; k++) {
        vec3 d = st.r - wash[k].pos; float along = dot(d, wash[k].aft);
        if (along < -0.3f * wash[k].R) continue;
        vec3 rad = d - wash[k].aft * along; float rr = length(rad);
        if (rr > 0.92f * wash[k].R) continue;
        float dev = 0.6f + 0.4f * smoothstepf(0.f, wash[k].R, along);
        u += wash[k].aft * (wash[k].dv * dev);
        if (rr > 1e-3f) sw += cross(wash[k].fwd, rad * (1.f / rr)) * (wash[k].rot * wash[k].vth * dev);
        const float arP = 1.84f * wash[k].R / st.chord, arS = std::max(g.surf[st.surf].AR, 0.5f);
        swk = std::min(swk, clampf(arP / (arP + 2.f) * (arS + 2.f) / arS, 0.2f, 1.f));
      }
      vec3 up = u - st.sp * dot(u, st.sp);
      float aGeo = atan2f(dot(up, st.n), dot(up, st.c));
      if (sw.x != 0.f || sw.y != 0.f || sw.z != 0.f) {
        const vec3 us = u + sw, ups = us - st.sp * dot(us, st.sp);
        aGeo += swk * (atan2f(dot(ups, st.n), dot(ups, st.c)) - aGeo);
        up = ups;
      }
      float vs2 = dot(up, up), vs = sqrtf(std::max(vs2, 1e-6f));
      vec3 uh = up * (1.f / vs);
      // the setting: the wing's incidence and twist, the tail's (in the wing's downwash), the canard's (in its upwash)
      float inc = st.twist;
      if (st.surf == AS_WING) inc += g.wingInc;
      else if (st.surf == AS_TAIL) inc += g.tailInc - (in.steady ? g.kEps * 2.f * CLs[AS_WING] / (PI * std::max(g.surf[AS_WING].AR, 1.f)) * ge[AS_WING] : eps);
      // (a canard sits ahead of the wing in its upwash: that field is there at once - no lag)
      else if (st.surf == AS_CANARD) inc += g.tailInc - g.kEps * 2.f * CLs[AS_WING] / (PI * std::max(g.surf[AS_WING].AR, 1.f)) * ge[AS_WING];
      // the control surface (deflection + trailing edge down, towards -n)
      float defl = 0;
      if (st.ctl == AC_AIL) defl = -st.side * clampf(in.roll + g.ailRig, -1.f, 1.f) * 0.33f;
      else if (st.ctl == AC_ELEV) { const float ed = elevTravel(elev); defl = st.surf == AS_CANARD ? ed : -ed; }
      else if (st.ctl == AC_RUD) defl = -0.5f * clampf(in.yaw + g.rudRig, -1.f, 1.f);   // (29 deg each way, as drawn: plane_parts.glsl)
      float dctl = flapTau(st.cf) * flapEta(defl) * defl * st.cover;
      float flap = st.ctl == AC_FLAP ? (st.side < 0 ? in.flapL : in.flapR) * st.cover : 0.f;
      // (in the plane square to the sweep: the streamwise settings come out larger, and the surface's slope - its
      // lift per streamwise angle - is the section's there times the sweep's cosine)
      float aEff = aGeo + inc / st.cosSw + dctl + g.flapA * flap;
      float a = slope[st.surf] * st.load / st.cosSw;
      // (a deflected surface raises the section's maximum lift too, on the side it deflects towards: a plain flap's
      // about 60% of the lift it adds - the canard pulled to full lift stalled sooner instead of lifting more)
      float clMax = st.clMax * (1.f - 0.3f * in.ice) + 0.9f * a * g.flapA * flap + 0.6f * a * std::max(dctl * (aGeo + inc >= 0.f ? 1.f : -1.f), 0.f);
      float x = a * aEff / std::max(clMax, 0.1f);
      // separation: its static share at this angle, reached with a lag (fast to separate, slow to reattach)
      float f0 = 1.f - smoothstepf(1.25f, 1.25f + 5.f * DEG * a / std::max(clMax, 0.1f), fabsf(x));
      float f = f0;
      if (!in.steady) {
        float tau = (f0 < mem.f[i] ? 3.f : 8.f) * st.chord / (2.f * std::max(vs, 5.f));
        f = mem.f[i] + (f0 - mem.f[i]) * (1.f - expf(-dt / tau));
        mem.f[i] = f;
      }
      float clAtt = clMax * (liftSat(x) + 0.3f * std::copysign(smoothstepf(1.25f, 2.f, fabsf(x)), x));   // (lagging separation: the dynamic overshoot)
      float cl = clAtt * f;
      Local& L = loc[i];
      L.up = up; L.uh = uh; L.q = 0.5f * rho * vs2 * (st.surf == AS_TAIL ? g.etaTail : 1.f);
      L.cl = cl; L.x = x; L.f = f; L.aPlate = aGeo + inc / st.cosSw + 0.4f * dctl + 0.6f * g.flapA * flap; L.dctl = dctl; L.cover = flap;
      // the surface's lift coefficient (the strips' lift over its area, on the flight's dynamic pressure: a strip in a
      // slipstream carries more)
      const float qr = std::min(L.q / std::max(out.qbar, 0.5f * rho * 25.f), 4.f);
      CLs[st.surf] += (cl * st.bodyF + (1.f - f) * (1.6f * sinf(L.aPlate) + 0.45f * sinf(2.f * L.aPlate)) * cosf(L.aPlate)) * st.area * qr;
      if (st.surf == AS_WING) maxX = std::max(maxX, x);
    }
    if (pass == 0) for (int k = 0; k < AS_COUNT; k++) if (g.surf[k].S > 0.f && k == AS_WING) CLs[k] /= g.surf[k].S;
  }
  for (int k = 1; k < AS_COUNT; k++) if (g.surf[k].S > 0.f) CLs[k] /= g.surf[k].S;
  out.CLw = CLs[AS_WING];
  out.stall = smoothstepf(0.8f, 0.95f, maxX);   // (the stall warner: about 7% above the stalling speed it sounds)
  // the downwash on its way to the tail: the wing's lift arrives there tail arm / V later
  if (!in.steady) mem.clw += (out.CLw - mem.clw) * (1.f - expf(-dt * V / std::max(g.tailArm, 0.5f)));
  else mem.clw = out.CLw;
  // ---------------- pass 2: the strips' forces
  const float sup = smoothstepf(0.85f, 1.2f, M);   // (supersonic: the lift's centre moves back to mid-chord)
  for (int i = 0; i < g.nSt; i++) {
    const AeroStrip& st = g.st[i]; const Local& L = loc[i];
    const float f = L.f;
    float area = st.area * (1.f + 0.1f * L.cover);   // (a Fowler flap slides out: more wing)
    const float qS = L.q * area;
    // attached: lift across the flow, the profile drag, the drag of the lift (its own and the surface's induced drag),
    // the deflected surface's drag
    vec3 nPerp = st.n - L.uh * dot(st.n, L.uh); float nl = length(nPerp); nPerp = nl > 1e-4f ? nPerp * (1.f / nl) : st.n;
    float ai = CLs[st.surf] / (PI * std::max(g.surf[st.surf].AR, 0.5f) * g.surf[st.surf].e) * ge[st.surf];
    float cdAtt = cdp[st.surf] + 0.006f * L.cl * L.cl + L.cl * ai + 0.3f * L.dctl * L.dctl + g.flapCdK * (g.flapA * L.cover) * (g.flapA * L.cover);
    // separated: the flat plate's normal force, and its friction (L.cl already carries the attached share)
    float ap = L.aPlate, CN = 1.6f * sinf(ap) + 0.45f * sinf(2.f * ap);
    vec3 Fsep = st.n * (CN * qS) + L.uh * (cdp[st.surf] * qS);
    vec3 Fs = nPerp * (L.cl * st.bodyF * qS) + L.uh * (cdAtt * qS * f) + Fsep * (1.f - f);
    // the section's own moment: camber, the deflected surface, the stalled section's centre of pressure, the
    // supersonic shift
    // (a deflected surface's moment is about a fifth of the lift it adds, nose down - thin aerofoil theory for a quarter
    // to a third of the chord; a Fowler flap's, sliding back as it goes down, a quarter)
    float cm = st.cm0 * f - 0.2f * slope[st.surf] * st.load / st.cosSw * L.dctl - 0.25f * slope[st.surf] * st.load / st.cosSw * g.flapA * L.cover * f
             - 0.2f * CN * (1.f - f) - 0.25f * L.cl * sup;
    vec3 Msec = cross(st.n, st.c) * (cm * qS * st.chord);
    F += Fs; Mo += cross(st.r - g.cg, Fs) + Msec;
  }
  // ---------------- the fuselage: its slender-body (Munk) forces and its crossflow drag, slice by slice
  for (int k = 0; k < g.nSeg; k++) {
    const AeroSegment& sg = g.seg[k];
    vec3 r(0, sg.y, sg.z);
    vec3 u = (in.va + cross(in.w, r) - gustAt(r)) * -1.f;
    float uax = u.z; vec3 uc(u.x, u.y, 0); float ucl = length(uc);
    vec3 Fk = uc * (rho * uax * sg.dA * (sg.dA < 0.f ? 0.4f : 1.f));   // (the aft body's share is mostly lost to its boundary layer)
    Fk += vec3(uc.x * sg.h, uc.y * sg.w, 0) * (0.5f * rho * ucl * 0.7f * sg.len);
    F += Fk; Mo += cross(r - g.cg, Fk);
  }
  // ---------------- the body's drag (the fuselage, the nacelles, struts, cooling and leaks), the gear's, the ice's,
  // the transonic and supersonic wave drag
  {
    float dBody = dq[3] + 0.025f * s.wingArea * in.ice, CDw = 0;
    if (s.designMach > 1.f) {
      CDw = 0.022f * smoothstepf(0.86f, 1.04f, M) - 0.007f * smoothstepf(1.2f, 2.2f, M);
      float over = M - s.designMach; if (over > 0.f) CDw += 0.5f * over * over;
    } else if (s.designMach > 0.f) { float x = M - (s.designMach - 0.04f); CDw = x > 0.f ? 30.f * x * x * x : 0.f; }
    else CDw = aeroWave(am, M, out.CLw);
    vec3 Fb = uHat * (out.qbar * (dBody + CDw * s.wingArea));
    vec3 Fg = uHat * (out.qbar * am.gearDq * in.gear);
    F += Fb + Fg; Mo += cross(g.bodyDragAt - g.cg, Fb) + cross(g.gearAt - g.cg, Fg);
  }
  // ---------------- the engines: thrust along their lines; a propeller's torque, P-factor, gyroscopic moment and normal
  // force; a stopped propeller windmilling
  for (int e = 0; e < g.nEng; e++) {
    const AeroEngine& en = g.eng[e];
    vec3 Ft = en.fwd * in.thrust[e];
    F += Ft; Mo += cross(en.pos - g.cg, Ft);
    if (!en.prop) continue;
    vec3 u = (in.va + cross(in.w, en.pos)) * -1.f;
    float A = PI * en.R * en.R, ul = length(u);
    if (in.omega[e] > 1.f) {
      vec3 spin = en.fwd * (en.rot * in.omega[e]);   // (clockwise from behind: spinning about its thrust axis)
      Mo += en.fwd * (-en.rot * in.power[e] / in.omega[e]);   // the torque's reaction: rolls it the other way
      Mo -= cross(in.w, spin * en.Ip);                        // gyroscopic
      float ap = atan2f(u.y, std::max(dot(u, en.fwd * -1.f), 1.f));   // the disk's angle of attack: the descending blade bites harder
      Mo += vec3(0, en.rot * in.thrust[e] * 0.25f * en.R * sinf(ap), 0);
    }
    vec3 uin = u - en.fwd * dot(u, en.fwd);   // the disk's normal force, along the crossflow (destabilising ahead of the CG)
    vec3 Fn = uin * (0.5f * rho * ul * A * 0.05f * std::max(s.blades, 2));
    // a stopped engine's propeller: a single's windmills (fixed pitch, or no oil pressure to feather it), a twin's is
    // feathered, edge on to the air
    if (in.dead[e]) Fn += uHat * (0.5f * rho * ul * ul * A * (s.engines > 1 ? 0.012f : 0.08f));
    F += Fn; Mo += cross(en.pos - g.cg, Fn);
  }
  out.F = F; out.M = Mo;
}
