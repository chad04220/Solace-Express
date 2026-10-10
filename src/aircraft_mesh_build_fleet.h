// Fleet mesh-build authoring. Shapes, poses and materials remain in their own GLSL modules.
#pragma once
#include "aircraft_mesh_build_types.h"
#include "aircraft.h"
namespace aircraftBuild { namespace fleet {
inline bool surfaceBox(int type, const float* M, vec3& lo, vec3& hi) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  const float R = m(0, 3);
  float span, rc, tc, sw, yOff, zOff, dih = 0.f, s0, s1, hf; bool fin = false;
  if (type == PT_FLAP || type == PT_AILERON) {
    span = m(9, 0); rc = m(9, 1); tc = m(9, 2); sw = m(9, 3); yOff = m(10, 0); zOff = m(10, 1); dih = m(10, 2); hf = 0.74f;
    const float flapEnd = span * m(11, 3);
    if (type == PT_FLAP) { s0 = int(m(19,3)+.5f)/2==kAtlas ? 4.05f : 0.55f * R; s1 = flapEnd; } else { s0 = flapEnd + 0.03f; s1 = span * 0.94f; }
  } else if (type == PT_ELEVATOR) {
    span = m(12, 0); rc = m(12, 1); tc = m(12, 2); sw = m(12, 3); yOff = m(13, 0); zOff = m(13, 1); dih = m(13, 2); hf = 0.68f; s0 = 0.12f; s1 = span * 0.98f;
  } else {
    span = m(14, 0); rc = m(14, 1); tc = m(14, 2); sw = m(14, 3); yOff = m(15, 0); zOff = m(15, 1); hf = 0.66f;
    s0 = m(13, 3) > 0.5f ? 0.05f : 0.08f * span; s1 = span * 0.97f; fin = true;
  }
  if (!(span > 0.f) || s1 <= s0) return false;
  float c0 = 1e9f, c1 = -1e9f;
  for (float sv : {s0, s1}) { float k = std::clamp(sv / span, 0.f, 1.f), ch = rc + (tc - rc) * k, le = sw * k; c0 = std::min(c0, le + ch * hf); c1 = std::max(c1, le + ch); }
  // Match sdSurface's maximum section at the hinge. Wide-chord transport controls exceed
  // the old fixed 12 cm half-thickness; a truncated survey box silently shaved their sides.
  const float k0=std::clamp(s0/span,0.f,1.f), k1=std::clamp(s1/span,0.f,1.f);
  const float maxChord=std::max(rc+(tc-rc)*k0,rc+(tc-rc)*k1);
  const float thickness=type==PT_FLAP||type==PT_AILERON?m(10,3):fin?.11f:.10f;
  const float tt = std::max(.12f,maxChord*thickness*.21f+.003f), mg = 0.03f;
  if (!fin) { lo = vec3(s0 - mg, yOff + std::min(dih * s0, dih * s1) - tt, zOff + c0 - mg); hi = vec3(s1 + mg, yOff + std::max(dih * s0, dih * s1) + tt, zOff + c1 + mg); }
  else { lo = vec3(-tt, yOff + s0 - mg, zOff + c0 - mg); hi = vec3(tt, yOff + s1 + mg, zOff + c1 + mg); }
  return true;
}
inline bool gearPartBox(int type, const float* M, vec3& lo, vec3& hi, float& h) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  const float track = m(18, 0), wr = m(18, 1), mz = m(18, 2), gh = m(19, 0);
  const int gtype = (int)(m(0, 1) + 0.5f);
  const bool atlas = int(m(19,3)+.5f)/2 == kAtlas;
  if(type==PT_GEAR_MAIN && gtype<=2){lo=vec3(0.f,-gh-.1f,mz-wr-.7f);hi=vec3(track+.3f,1.5f,mz+wr+.7f);h=.005f;return true;}
  if(atlas) {
    // Atlas gearFold().H.z is 2.72 m; include the complete trunnion capsule.
    if(type==PT_GEAR_MAIN){lo=vec3(track-.65f,-gh-.1f,std::min(mz-1.28f,2.72f-.20f));hi=vec3(track+.65f,1.5f,mz+1.28f);h=.008f;return true;}
    if(type==PT_GEAR_NOSE){lo=vec3(-.52f,-gh-.1f,-.70f);hi=vec3(.52f,1.0f,.70f);h=.007f;return true;}
    if(type==PT_GEAR_MDOOR){lo=vec3(-.02f,-.06f,-1.36f);hi=vec3(2.12f,.03f,1.36f);h=.006f;return true;}
    if(type==PT_GEAR_NDOOR){lo=vec3(-.05f,-.06f,-2.3f);hi=vec3(.5f,.03f,2.3f);h=.006f;return true;}
    if(type==PT_ATLAS_FAN){const float r=m(16,2);lo=vec3(-r,-r,-.38f);hi=vec3(r,r,.16f);h=.006f;return true;}
  }
  switch (type) {
    case PT_GEAR_MAIN: lo = vec3(track - 0.5f, -gh - 0.1f, mz - wr - (gtype == 3 ? 1.5f : 0.6f)); hi = vec3(track + 0.5f, gtype == 3 ? 2.2f : 1.5f, mz + wr + (gtype == 3 ? 2.0f : 0.6f)); h = 0.005f; return true;   // (a nacelle main's raked leg and its door)
    case PT_GEAR_NOSE: lo = vec3(-0.45f, -gh - 0.1f, -1.0f); hi = vec3(0.45f, 1.5f, 1.0f); h = 0.005f; return true;
    case PT_GEAR_TAIL: lo = vec3(-0.3f, -gh - 0.1f, -0.8f); hi = vec3(0.3f, 1.2f, 0.6f); h = 0.004f; return true;
    case PT_GEAR_MDOOR: if (gtype == 4) { lo = vec3(-2.5f, -0.06f, -1.3f); hi = vec3(2.5f, 0.03f, 1.3f); }   // (a fold well's door from its fore-and-aft hinge, or a swing well's from its side)
             else { lo = vec3(-0.06f, -0.06f, -wr - 0.4f); hi = vec3(0.5f, 0.03f, wr + 0.4f); }
             h = 0.004f; return true;
    case PT_GEAR_NDOOR: lo = vec3(-0.06f, -0.06f, -1.7f); hi = vec3(0.4f, 0.03f, 1.7f); h = 0.004f; return true;   // (the nose bay's doors run the folded leg's length)
  }
  return false;
}
inline int parts(const float* M, bool inside, PartInst* out, int model) {
  const int eng = (int)(M[2] + 0.5f);
  if (eng > 6) return 0;
  const bool mantis = eng == 4 && fabsf(M[22 * 4]) < 0.001f && M[13 * 4 + 1] < -3.f;
  int n = 0;
   // the light aircraft's (and the XR-10's and XR-20's) control surfaces, outside and from the cockpit
  for (int s = -1; s <= 1; s += 2) { out[n++] = {PT_FLAP, (float)s, 0}; out[n++] = {PT_AILERON, (float)s, 0}; out[n++] = {PT_ELEVATOR, (float)s, 0}; }   // flap, aileron, elevator
  if (mantis) { out[n++] = {PT_RUDDER, -1, 0}; out[n++] = {PT_RUDDER, 1, 0}; } else out[n++] = {PT_RUDDER, 0, 0};   // the rudder (the XR-20's canted pair)
  if(int(M[19*4+3]+.5f)/2==kAtlas) {out[n++]={PT_ATLAS_FAN,-1,0};out[n++]={PT_ATLAS_FAN,1,0};}
  // and their gear: a retracting main leg a side and its bay's two doors; the nose wheel (and its doors) or the tail wheel
  const int gtype = (int)(M[1] + 0.5f); const bool tail = M[19 * 4 + 2] > 0.5f;
  for (int s = -1; s <= 1; s += 2) {
    out[n++] = {PT_GEAR_MAIN, (float)s, 0};
    if (gtype >= 3) { out[n++] = {PT_GEAR_MDOOR, (float)s, -1}; out[n++] = {PT_GEAR_MDOOR, (float)s, 1}; }
  }
  if (tail) out[n++] = {PT_GEAR_TAIL, 0, 0};
  else { out[n++] = {PT_GEAR_NOSE, 0, 0}; if (gtype >= 3) { out[n++] = {PT_GEAR_NDOOR, 0, -1}; out[n++] = {PT_GEAR_NDOOR, 0, 1}; } }
  if (!inside) return n;
  if (mantis) { out[n++] = {PT_JET_STICK, 0, 0}; out[n++] = {PT_JET_THR, 0, 0}; out[n++] = {PT_PEDAL, 0, -1}; out[n++] = {PT_PEDAL, 0, 1}; return n; }   // the XR-20: side stick, throttle, one pair of pedals
  for (int s = -1; s <= 1; s += 2) { out[n++] = {PT_YOKE_SHAFT, (float)s, 0}; out[n++] = {PT_YOKE_WHEEL, (float)s, 0}; }   // the yokes: shaft, wheel
  for (int s = -1; s <= 1; s += 2) for (int q = -1; q <= 1; q += 2) out[n++] = {PT_PEDAL, (float)s, (float)q};   // the pedals
  if ((int)(M[21 * 4 + 2] + 0.5f) == 0) {
    if(model==0 || model==1) { out[n++]={PT_THR_KNOB,-1,0};out[n++]={PT_THR_KNOB,1,0}; } // linked trainer side throttles
    else out[n++] = {PT_THR_KNOB, 0, 0};
  }
  else { if (eng != 1) out[n++] = {PT_THR_LEVER, -1, 0}; out[n++] = {PT_THR_LEVER, 1, 0}; out[n++] = {PT_FLAP_LEVER, 0, 0}; }   // one power lever for the single turboprop, otherwise linked pair
  return n;
}
inline PartBakePlan partPlan(int type, const float* M) {
  PartBakePlan p;
  if (gearPartBox(type, M, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Survey;
  else if (type >= PT_FLAP && type <= PT_RUDDER) {
    p.sampling = surfaceBox(type, M, p.lo, p.hi) ? PartSampling::Fixed : PartSampling::Empty;
    p.lattice = 0.006f;
  }
  return p;
}
inline void appendHullStates(std::vector<HullState>&, bool, bool) {}
} } // namespace aircraftBuild::fleet
