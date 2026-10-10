// Solace Express - the airframe's wake (aero_wake.h)
#include "aero_wake.h"
#include "aero.h"
#include <algorithm>

namespace {
// the velocity a straight vortex induces at p: from a to b (or from a on to infinity along t, when inf), circulation
// gam along it, with a core of radius rc (Scully: solid-body inside it, so nothing blows up at the line itself)
vec3 segmentVel(vec3 p, vec3 a, vec3 b, float gam, float rc) {
  const vec3 r0 = b - a, r1 = p - a, r2 = p - b, x = cross(r1, r2);
  const float l1 = length(r1), l2 = length(r2), x2 = dot(x, x), r02 = dot(r0, r0);
  if (l1 < 1e-4f || l2 < 1e-4f || r02 < 1e-8f) return vec3();
  const float k = gam / (4.f * PI) * dot(r0, r1 * (1.f / l1) - r2 * (1.f / l2)) / (x2 + rc * rc * r02);
  return x * k;
}
vec3 semiInfVel(vec3 p, vec3 a, vec3 t, float gam, float rc) {
  const vec3 d = p - a;
  const float along = dot(d, t), dl = length(d);
  const vec3 perp = d - t * along;
  const float h2 = dot(perp, perp);
  if (dl < 1e-4f) return vec3();
  return cross(t, perp) * (gam / (4.f * PI) * (1.f + along / dl) / (h2 + rc * rc));
}
}

void aeroWakeBuild(const AeroGeom& g, const AeroIn& in, const AeroOut& out, AeroWake& w) {
  w = AeroWake();
  const float V = length(in.va);
  w.V = V;
  w.span = g.surf[AS_WING].b;
  w.core = std::max(0.04f * w.span, 0.3f);
  if (V < 3.f) return;
  w.aft = in.va * (-1.f / V);
  // ---------------- the surfaces: each a chain of strips along its span (a wing or tailplane tip to tip across the
  // fuselage, each fin root to tip - its root on the fuselage sheds nothing, the fuselage standing in for its mirror)
  struct Shed { vec3 p; float gam; };
  struct Lump { vec3 p; float gam, wt; int surf, chain; };
  Lump lumps[24]; int nl = 0, chainId = 0;
  Shed wingShed[AeroGeom::kMaxStrips + 1]; int nws = 0; float wingG = 0;   // (the wing's, for the pair it rolls up into)
  auto chain = [&](const int* idx, int n, bool shedRoot) {
    if (n < 2) return;
    vec3 te[AeroGeom::kMaxStrips]; float G[AeroGeom::kMaxStrips];
    for (int k = 0; k < n; k++) { const AeroStrip& st = g.st[idx[k]]; te[k] = st.r + st.c * (0.75f * st.chord); }
    for (int k = 0; k < n; k++) {
      const AeroStrip& st = g.st[idx[k]];
      const vec3 e = normalize(k + 1 < n ? te[k + 1] - te[k] : te[k] - te[k - 1]);
      const vec3 bhat = normalize(cross(st.n, w.aft));   // (the bound vortex's direction: Kutta-Joukowski, lift along n)
      G[k] = out.gam[idx[k]] * dot(bhat, e);              // (its circulation along the chain)
    }
    // where the fuselage carries the lift through, the circulation goes on across it at the level beside it (its
    // dip there sheds next to nothing in a real wing-body: the fuselage turns the flow as the wing would)
    float Gs[AeroGeom::kMaxStrips];
    for (int k = 0; k < n; k++) {
      Gs[k] = G[k];
      if (g.st[idx[k]].bodyF >= 1.f) continue;
      for (int d = 1; d < n; d++) {
        if (k - d >= 0 && g.st[idx[k - d]].bodyF >= 1.f) { Gs[k] = G[k - d]; break; }
        if (k + d < n && g.st[idx[k + d]].bodyF >= 1.f) { Gs[k] = G[k + d]; break; }
      }
    }
    for (int k = 0; k < n; k++) G[k] = Gs[k];
    Shed sh[AeroGeom::kMaxStrips + 1]; int ns = 0; float gmax = 0;
    if (shedRoot) sh[ns++] = {te[0] - (te[1] - te[0]) * 0.5f, -G[0]};
    for (int k = 0; k + 1 < n; k++) sh[ns++] = {(te[k] + te[k + 1]) * 0.5f, G[k] - G[k + 1]};
    sh[ns++] = {te[n - 1] + (te[n - 1] - te[n - 2]) * 0.5f, G[n - 1]};
    for (int k = 0; k < ns; k++) gmax = std::max(gmax, fabsf(sh[k].gam));
    if (g.st[idx[0]].surf == AS_WING) { for (int k = 0; k < ns; k++) wingShed[nws++] = sh[k]; for (int k = 0; k < n; k++) wingG += G[k]; }
    if (gmax < 0.05f) { chainId++; return; }
    // runs turning one way, lumped at their centroids (what is shed in between too weak to start one joins the run it
    // is in). A run splits where the shedding dips and rises again - a flap's edge and the tip beyond it each roll up
    // a vortex of their own before the two wind together
    // (each half from the middle out: the same on both sides)
    Lump cur{vec3(), 0.f, 0.f, g.st[idx[0]].surf, chainId}; bool open = false; float sign = 0, peak = 0, valley = 0;
    auto flush = [&]() { if (open && cur.wt > 0.f && nl < 24) { cur.p = cur.p * (1.f / cur.wt); lumps[nl++] = cur; } cur = Lump{vec3(), 0.f, 0.f, g.st[idx[0]].surf, chainId}; open = false; };
    auto run = [&](int k0, int k1, int step) {
      for (int k = k0; k != k1; k += step) {
        const float a = fabsf(sh[k].gam), s = sh[k].gam >= 0.f ? 1.f : -1.f;
        if (a > 0.06f * gmax && open && (s != sign || (valley < 0.45f * peak && a > 2.2f * valley && a > 0.2f * gmax))) flush();
        if (!open) { if (a <= 0.06f * gmax) continue; open = true; sign = s; peak = valley = a; }
        cur.p += sh[k].p * a; cur.wt += a; cur.gam += sh[k].gam;
        if (a > peak) peak = valley = a; else valley = std::min(valley, a);
      }
      flush();
    };
    if (shedRoot) { const int mid = ns / 2; run(mid - 1, -1, -1); run(mid, ns, 1); }   // (tip to tip: ns is even, the middle boundary is the fuselage's)
    else run(0, ns, 1);
    chainId++;
  };
  int idx[AeroGeom::kMaxStrips];
  for (int surf : {AS_WING, AS_TAIL, AS_CANARD}) {
    int n = 0;
    for (int i = g.nSt - 1; i >= 0; i--) if (g.st[i].surf == surf && g.st[i].side < 0.f) idx[n++] = i;   // (left: tip to root)
    for (int i = 0; i < g.nSt; i++) if (g.st[i].surf == surf && g.st[i].side > 0.f) idx[n++] = i;        // (right: root to tip)
    chain(idx, n, true);
  }
  for (float side : {-1.f, 0.f, 1.f}) {
    int n = 0;
    for (int i = 0; i < g.nSt; i++) if (g.st[i].surf == AS_FIN && g.st[i].side == side) idx[n++] = i;
    chain(idx, n, false);
  }
  // the strongest of them, in each chain's order along its span (the bound vortices join neighbours: a wing or tailplane
  // from left to right, a fin from root to tip)
  std::stable_sort(lumps, lumps + nl, [](const Lump& a, const Lump& b) {
    if (a.chain != b.chain) return a.chain < b.chain;
    return a.surf == AS_FIN ? a.p.y < b.p.y : a.p.x < b.p.x;
  });
  bool keep[24] = {};
  float gTop = 0; for (int k = 0; k < nl; k++) gTop = std::max(gTop, fabsf(lumps[k].gam));
  for (int m = 0; m < std::min(nl, AeroWake::kMaxVortex); m++) {
    int best = -1;
    for (int k = 0; k < nl; k++) if (!keep[k] && (best < 0 || fabsf(lumps[k].gam) > fabsf(lumps[best].gam))) best = k;
    if (best < 0 || fabsf(lumps[best].gam) < 0.02f * gTop + 0.05f) break;
    keep[best] = true;
  }
  for (int k = 0; k < nl; k++) if (keep[k]) w.v[w.nv++] = {lumps[k].p, lumps[k].gam, lumps[k].surf, lumps[k].chain};
  // the wing's vortices as the pair they roll up into (Betz): each half's vorticity shed the way its tip's turns, at
  // its centroid - the lift's circulation, about pi/4 of the span apart on an elliptic wing
  const float ls = wingG >= 0.f ? 1.f : -1.f;
  float gR = 0, gL = 0, xR = 0, xL = 0, y = 0, z = 0, a = 0;
  for (int k = 0; k < nws; k++) {
    const Shed& sh = wingShed[k];
    if (sh.p.x >= 0.f && sh.gam * ls > 0.f) { gR += sh.gam; xR += sh.p.x * sh.gam; }
    else if (sh.p.x < 0.f && sh.gam * ls < 0.f) { gL += sh.gam; xL += sh.p.x * sh.gam; }
    else continue;
    const float m = fabsf(sh.gam); y += sh.p.y * m; z += sh.p.z * m; a += m;
  }
  if (fabsf(gR) > 1e-3f && fabsf(gL) > 1e-3f) {
    w.gamPair = 0.5f * (gR - gL); w.b0 = xR / gR - xL / gL; w.pairY = y / a; w.pairZ = z / a;
  }
  // ---------------- the engines: a propeller's slipstream (actuator disk) and its swirl, a jet's hot exhaust
  for (int e = 0; e < g.nEng && w.nj < 4; e++) {
    const AeroEngine& en = g.eng[e];
    const float T = std::max(in.thrust[e], 0.f), vax = std::max(V, 1.f);
    if (en.prop) {
      const float A = PI * en.R * en.R, dv = sqrtf(vax * vax + 2.f * T / (in.rho * A)) - vax;
      const float Q = in.omega[e] > 1.f ? in.power[e] / in.omega[e] : 0.f;
      const float vth = std::min(Q / (in.rho * A * std::max(vax + 0.5f * dv, 3.f) * 0.7f * en.R), 0.5f * dv + 2.f);
      w.j[w.nj++] = {en.pos, en.R, dv, -en.rot * vth, false};   // (rot +1: clockwise from behind, as aero_strips.cpp's swirl)
    } else {
      const float R = 0.35f, A = PI * R * R;   // (a business jet's nozzle)
      w.j[w.nj++] = {en.pos, R, sqrtf(vax * vax + 2.f * T / (in.rho * A)) - vax, 0.f, true};
    }
  }
}

void aeroWakeElliptic(float lift, float rho, float V, float span, float wingY, float wingZ, int engines, float thrust, float tailZ, AeroWake& w) {
  w = AeroWake();
  w.V = V; w.span = span; w.core = std::max(0.04f * span, 0.3f);
  if (V < 3.f) return;
  w.b0 = 0.25f * PI * span;
  w.gamPair = lift / (rho * V * w.b0);
  w.pairY = wingY; w.pairZ = wingZ;
  w.v[0] = {vec3(-0.5f * w.b0, wingY, wingZ), -w.gamPair, AS_WING, 0};
  w.v[1] = {vec3(0.5f * w.b0, wingY, wingZ), w.gamPair, AS_WING, 0};
  w.nv = 2;
  const int n = std::max(1, std::min(engines, 4));
  for (int e = 0; e < n; e++) {
    const float R = 0.45f, A = PI * R * R, T = std::max(thrust, 0.f) / n;
    const float x = n == 1 ? 0.f : (e - 0.5f * (n - 1)) * 1.1f;
    w.j[w.nj++] = {vec3(x, wingY, tailZ), R, sqrtf(V * V + 2.f * T / (rho * A)) - V, 0.f, true};
  }
}

vec3 aeroWakeInduced(const AeroWake& w, const AeroGeom* g, vec3 p) {
  vec3 v(0, 0, 0);
  if (w.V < 3.f) return v;
  // the trailing vortices, and the bound vortex along each surface between them (its circulation there: what the
  // vortices before it on the chain have shed, the other way round)
  float run = 0; int ch = -1;
  for (int k = 0; k < w.nv; k++) {
    const AeroWake::Vortex& a = w.v[k];
    v += semiInfVel(p, a.r, w.aft, a.gam, w.core);
    if (a.chain != ch) { ch = a.chain; run = 0; }
    run += a.gam;
    if (k + 1 < w.nv && w.v[k + 1].chain == ch) v += segmentVel(p, a.r, w.v[k + 1].r, -run, w.core);
  }
  // the engines' wash: inside each stream, the air blown aft over the free air's speed (a slipstream contracting to
  // about 0.75 of the disk, then spreading and slowing as it mixes) and a propeller's swirl
  for (int e = 0; e < w.nj; e++) {
    const AeroWake::Jet& j = w.j[e];
    const vec3 d = p - j.r;
    const float along = dot(d, w.aft);
    if (along < -0.5f * j.R) continue;
    const vec3 rad = d - w.aft * along;
    const float rr = length(rad), x = std::max(along, 0.f) / j.R;
    const float Rw = j.R * (j.hot ? 1.f + 0.12f * x : 0.75f + 0.06f * x);
    if (rr > Rw) continue;
    const float decay = expf(-x / (j.hot ? 25.f : 40.f)) * (j.R * j.R) / (Rw * Rw) * smoothstepf(-0.5f, 0.f, along / j.R);
    v += w.aft * (j.dv * decay * (1.f - (rr / Rw) * (rr / Rw)) * 1.6f);
    if (j.swirl != 0.f && rr > 1e-3f) v += cross(w.aft, rad * (1.f / rr)) * (j.swirl * decay * rr / Rw);
  }
  // the fuselage: a line of sources and sinks, each as strong as the airspeed times the change of the body's section
  // across it (the nose pushes the air out of the way, the tail lets it close in again)
  if (g) for (int k = 0; k < g->nSeg; k++) {
    const AeroSegment& sg = g->seg[k];
    const vec3 d = p - vec3(0, sg.y, sg.z);
    const float r2 = dot(d, d) + 0.25f * sg.area;
    v += d * (w.V * sg.dA / (4.f * PI) / (r2 * sqrtf(r2)));
  }
  return v;
}

bool aeroWakeCatch(const AeroWake& w, vec3 pos, const quat& q, vec3 p, AirSwirl& s) {
  if (w.b0 < 0.1f || fabsf(w.gamPair) < 0.1f) return false;
  const vec3 b = q.conj().rotate(p - pos);
  const float side = b.x >= 0.f ? 1.f : -1.f;
  const vec3 core(side * 0.5f * w.b0, w.pairY, b.z);
  const vec3 d = b - core;
  if (d.x * d.x + d.y * d.y > w.b0 * w.b0) return false;
  s.c = pos + q.rotate(core);
  s.ax = q.rotate(w.aft);
  s.gam = side * w.gamPair;   // (lifting, the right one turns anticlockwise seen from behind: the air goes down between them)
  s.core = w.core;
  s.sink = q.rotate(vec3(0, w.gamPair >= 0.f ? -aeroWakeSink(w) : aeroWakeSink(w), 0));   // (away from the lift)
  return true;
}

vec3 aeroSwirlStep(AirSwirl& s, vec3 p, float dt) {
  const vec3 d = p - s.c;   // (the air round the core goes down with it)
  s.c += s.sink * dt;
  const float along = dot(d, s.ax);
  const vec3 rad = d - s.ax * along;
  const float r2 = dot(rad, rad);
  if (r2 < 1e-6f) return s.c + s.ax * along;
  // the angle it turns this step (Lamb-Oseen: the circulation's share inside its radius, round that circle), and the
  // core growing as the vortex diffuses
  const float om = s.gam / (2.f * PI * r2) * (1.f - expf(-r2 / (s.core * s.core)));
  const float a = om * dt, ca = cosf(a), sa = sinf(a);
  const vec3 rot = rad * ca + cross(s.ax, rad) * sa;
  s.core = sqrtf(s.core * s.core + 4.f * 2e-4f * fabsf(s.gam) * dt);
  return s.c + s.ax * along + rot;
}
