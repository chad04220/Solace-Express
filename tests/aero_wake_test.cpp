// The airframe's wake (aero_wake.cpp) per type, from steady strip-model evaluations: the wing's vortices roll up into
// a pair as strong and as far apart as the lift says (Kutta-Joukowski; near an elliptic loading's pi/4 of the span -
// less on a tapered, washed-out wing, or a twin's with its slipstreams' lift inboard), turning the right way; flaps add vortices at their outer edges, turning with the tips'; rolling moves the load
// across; the tailplane's vortices turn against the wing's when it pushes down; a sideslip sets the fin's going;
// the air goes down between the tips, up outside them and round the nose; a propeller's slipstream blows aft and turns.
#include "../src/aero.h"
#include "../src/aero_wake.h"
#include "../src/aircraft.h"
#include "../src/models.h"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what, const char* name) {
  if (!ok) { printf("  FAIL %s: %s\n", name, what); fails++; }
}
// a steady evaluation at this angle of attack (and sideslip), flaps and roll
static void evalAt(const AeroGeom& g, const AircraftSpec& s, float V, float a, float b, float flap, float roll, AeroIn& in, AeroOut& o) {
  AeroMem mem; in = AeroIn(); in.steady = true; in.rho = 1.0f;
  in.va = vec3(V * sinf(b), -V * sinf(a) * cosf(b), -V * cosf(a) * cosf(b));
  in.flapL = in.flapR = flap; in.roll = roll;
  for (int e = 0; e < g.nEng; e++) { in.thrust[e] = 0.25f * s.power / std::max(V, 20.f); in.power[e] = 0.6f * s.power; in.omega[e] = 2400.f * 2.f * PI / 60.f; }
  aeroForces(g, s, in, mem, 0.f, o);
}
static float wingLift(const AeroGeom& g, const AeroOut& o, float rho, float V) {   // the wing's strips' lift upward (N): rho V circulation span
  float L = 0;
  for (int i = 0; i < g.nSt; i++) if (g.st[i].surf == AS_WING) L += rho * V * o.gam[i] * g.st[i].area / g.st[i].chord * g.st[i].n.y;
  return L;
}

int main() {
  printf("%-16s %7s %7s %6s %6s %6s %5s %7s %7s %7s\n", "type", "gamPair", "KJ", "b0/b", "sink", "nv", "flapv", "tailGam", "finGam", "wash");
  for (int i = 0; i < kAircraftCount; i++) {
    const AircraftSpec& s = kAircraft[i];
    if (s.special != 0) continue;
    const AeroGeom& g = aeroGeom(s);
    const float V = 1.6f * std::max(s.vref, 25.f) / 1.3f;
    AeroIn in; AeroOut o; AeroWake w;
    evalAt(g, s, V, 6.f * DEG, 0.f, 0.f, 0.f, in, o);
    aeroWakeBuild(g, in, o, w);
    // the pair: as strong as the wing's lift over rho V b0, about pi/4 of the span apart, the right one anticlockwise
    const float L = wingLift(g, o, in.rho, V);
    const float kj = L / (in.rho * V * std::max(w.b0, 0.1f));
    float tailGam = 0, finGam = 0;
    for (int k = 0; k < w.nv; k++) if (w.v[k].surf == AS_TAIL && w.v[k].r.x > 0.f) tailGam += w.v[k].gam;
    check(w.gamPair > 0.f && fabsf(w.gamPair - kj) < 0.2f * kj, "pair circulation = wing lift / (rho V b0)", s.name);
    // Betz spacing follows the actual loading. Pi/4 is an elliptic special case, not a
    // lower bound: the Atlas's highly tapered, washed-out wing rolls up nearer the centre.
    float peakGamma = 0;
    for (int k = 0; k < g.nSt; ++k) if (g.st[k].surf == AS_WING) peakGamma = std::max(peakGamma, fabsf(o.gam[k]));
    const float loadSpan = L / (in.rho * V * std::max(peakGamma, .01f));
    printf("  spacing %s actual%.3f expected-from-loading%.3f\n",s.name,w.b0/w.span,loadSpan/w.span);
    check(w.b0 > 0.f && w.b0 < w.span && fabsf(w.b0-loadSpan)<.2f*loadSpan,
          "pair spacing conserves actual spanwise lift and peak circulation", s.name);
    // between the tips the air goes down, outside them up (the vortices alone); ahead of the nose it is pushed aside
    const vec3 mid = aeroWakeInduced(w, nullptr, vec3(0.f, w.pairY, w.pairZ + 0.3f * w.span));
    const vec3 out = aeroWakeInduced(w, nullptr, vec3(0.75f * w.span, w.pairY, w.pairZ + 0.3f * w.span));
    check(mid.y < 0.f && out.y > 0.f, "downwash inside the tips, upwash outside", s.name);
    float zNose = 1e9f; for (int k = 0; k < g.nSeg; k++) zNose = std::min(zNose, g.seg[k].z - 0.5f * g.seg[k].len);
    const vec3 nose = aeroWakeInduced(w, &g, vec3(0.6f, g.seg[0].y, zNose + 0.3f));
    check(nose.x > 0.f, "the nose pushes the air aside", s.name);
    // flaps: correctly oriented outer-edge vortices, whether distinct or merged with the tip wake
    AeroWake wf;
    AeroIn cleanIn; AeroOut cleanOut;
    evalAt(g, s, V, 4.f * DEG, 0.f, 0.f, 0.f, cleanIn, cleanOut);
    const float cleanLiftAtFlapAngle = wingLift(g, cleanOut, cleanIn.rho, V);
    evalAt(g, s, V, 4.f * DEG, 0.f, 1.f, 0.f, in, o);
    aeroWakeBuild(g, in, o, wf);
    int inboard = 0, flapped = 0;
    for (int k = 0; k < g.nSt; k++) flapped += g.st[k].ctl == AC_FLAP;
    // Locate flap vortices against the authored flap boundary, not the outermost vortex
    // centroid: on a strongly tapered wing the tip and outer-flap wake can merge into one.
    const float flapEdge = kModels[i].wing[0] * kModels[i].flapFrac;
    for (int k = 0; k < wf.nv; k++) if (wf.v[k].surf == AS_WING &&
        fabsf(fabsf(wf.v[k].r.x)-flapEdge) < .25f * .5f * wf.span && wf.v[k].gam * wf.v[k].r.x > 0.f) inboard++;
    if (flapped && g.flapA > 0.f) {
      check(inboard >= 2, "flap-edge vortices", s.name);
      check(wingLift(g, o, in.rho, V) > 1.08f * cleanLiftAtFlapAngle,
            "flaps increase circulation/lift at the same airspeed and angle", s.name);
    }
    // rolling right (right aileron up): the right side's vortex weakens against the left's
    AeroWake wr;
    evalAt(g, s, V, 6.f * DEG, 0.f, 0.f, 1.f, in, o);
    aeroWakeBuild(g, in, o, wr);
    float xr = 0, gr = 0, xl = 0, gl = 0;   // (where each side's vorticity turning as its tip's is: the load's centroid)
    for (int k = 0; k < wr.nv; k++) if (wr.v[k].surf == AS_WING) {
      if (wr.v[k].r.x > 0.f && wr.v[k].gam > 0.f) { xr += wr.v[k].r.x * wr.v[k].gam; gr += wr.v[k].gam; }
      if (wr.v[k].r.x < 0.f && wr.v[k].gam < 0.f) { xl += wr.v[k].r.x * wr.v[k].gam; gl -= wr.v[k].gam; }
    }
    check(gr > 0.f && gl > 0.f && xr / gr < xl / gl, "aileron moves the load across", s.name);
    // a sideslip from the right (nose left of the flight path): the fin lifts and trails a vortex
    AeroWake ws;
    evalAt(g, s, V, 4.f * DEG, 8.f * DEG, 0.f, 0.f, in, o);
    aeroWakeBuild(g, in, o, ws);
    for (int k = 0; k < ws.nv; k++) if (ws.v[k].surf == AS_FIN) finGam += fabsf(ws.v[k].gam);
    check(finGam > 0.5f, "the fin's vortex in a sideslip", s.name);
    // the slipstream: blown aft behind each propeller
    float wash = 0;
    for (int e = 0; e < w.nj; e++) if (!w.j[e].hot) wash = std::max(wash, dot(aeroWakeInduced(w, &g, w.j[e].r + w.aft * (2.f * w.j[e].R)), w.aft));
    if (g.nEng && g.eng[0].prop) check(wash > 1.f, "propeller slipstream", s.name);
    printf("%-16s %7.1f %7.1f %6.2f %6.2f %6d %5d %7.1f %7.1f %7.1f\n", s.name, w.gamPair, kj, w.b0 / std::max(w.span, 0.1f), aeroWakeSink(w), w.nv, inboard, tailGam, finGam, wash);
  }
  // The exact elliptic special case is still checked, independently of arbitrary planforms.
  {
    AeroWake e; aeroWakeElliptic(10000.f,1.2f,50.f,12.f,0,0,1,0,5,e);
    check(fabsf(e.b0-PI*12.f/4.f)<.00001f && fabsf(e.gamPair*1.2f*50.f*e.b0-10000.f)<.01f,
          "elliptic pair spacing and Kutta-Joukowski lift", "elliptic reference");
  }
  // the air a vortex catches turns round it the right way and stays at its radius
  {
    AeroWake w; w.b0 = 10.f; w.gamPair = 60.f; w.span = 12.f; w.core = 0.5f; w.V = 50.f;
    AirSwirl sw; const quat q; const vec3 pos(0, 0, 0);
    const vec3 p0(5.f, 1.f, 20.f);   // (above the right core)
    bool caught = aeroWakeCatch(w, pos, q, p0, sw);
    vec3 p = p0;
    for (int k = 0; k < 10; k++) p = aeroSwirlStep(sw, p, 0.01f);
    check(caught && p.x < p0.x && fabsf(length(p - sw.c) - 1.f) < 0.05f, "a caught parcel turns inboard over the right core", "swirl");
  }
  printf(fails ? "%d failures\n" : "all wake checks passed\n", fails);
  return fails ? 1 : 0;
}
