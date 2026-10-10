// Specter mesh-build authoring. Shapes, poses and materials remain in their own GLSL modules.
#pragma once
#include "aircraft_mesh_build_research_gear.h"
namespace aircraftBuild { namespace specter {
inline bool partBox(int type, vec3& lo, vec3& hi, float& h) {
  switch (type) {
    case PT_JT_ELEVON: lo = vec3(1.17f, -0.44f, 4.5f); hi = vec3(5.33f, -0.15f, 5.6f); h = 0.006f; return true;    // the XR-30's elevon
    case PT_JT_CANARD: lo = vec3(-0.04f, -0.06f, -0.63f); hi = vec3(1.54f, 0.06f, 0.93f); h = 0.005f; return true;   // its canard
    case PT_JT_RUDDER: {   // its rudder: the (span, chord, thickness) box through the canted fin's frame (jtPartField)
      const float C = cosf(0.42f), S = sinf(0.42f);
      lo = vec3(1e9f, 1e9f, 1e9f); hi = vec3(-1e9f, -1e9f, -1e9f);
      for (int c = 0; c < 8; c++) {
        float sv = (c & 1) ? 2.25f : 0.1f, ch = (c & 2) ? 2.95f : 1.8f, t = (c & 4) ? 0.06f : -0.06f;
        float x = 1.f + C * t + S * sv, y = 0.3f - S * t + C * sv, z = ch + 4.6f;
        lo = vec3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z)); hi = vec3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z));
      }
      h = 0.006f; return true;
    }
  }
  return false;
}
inline int parts(const float*, bool inside, PartInst* out, int) {
  int n = 0;
  if (!inside) {   // the XR-30: elevons, canards, rudders, nozzles; its gear's struts, wheels and bay doors
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {PT_JT_ELEVON, (float)s, 0}; out[n++] = {PT_JT_CANARD, (float)s, 0}; out[n++] = {PT_JT_RUDDER, (float)s, 0}; out[n++] = {PT_JT_NOZZLE, (float)s, 0};
      out[n++] = {PT_JT_LEGM, (float)s, 0}; out[n++] = {PT_JT_WHEELM, (float)s, 0}; out[n++] = {PT_JT_DOORM, (float)s, -1}; out[n++] = {PT_JT_DOORM, (float)s, 1};
    }
    out[n++] = {PT_JT_LEGN, 0, 0}; out[n++] = {PT_JT_WHEELN, 0, 0}; out[n++] = {PT_JT_DOORN, 0, -1}; out[n++] = {PT_JT_DOORN, 0, 1};
    return n;
  }
  out[n++] = {PT_JET_STICK, 0, 0}; out[n++] = {PT_JET_THR, 0, 0}; out[n++] = {PT_WR_PEDAL, -1, 0}; out[n++] = {PT_WR_PEDAL, 1, 0};
  return n;
}
inline PartBakePlan partPlan(int type, const float* M) {
  PartBakePlan p;
  if (partBox(type, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Fixed;
  else if (type == PT_JT_NOZZLE) {
    p.lo = vec3(-0.56f, -0.44f, -0.12f); p.hi = vec3(0.56f, 0.44f, 1.2f);
    p.lattice = 0.005f; p.sampling = PartSampling::Survey;
  }
  else if (researchGearBox(type, M, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Survey;
  return p;
}
inline void appendHullStates(std::vector<HullState>& st, bool inside, bool meshBake) {
  if (inside || meshBake) return;
  // Preserve the authored nozzle sweep, including legacy extremes. This does not enable VTOL.
  for (int i = 1; i <= 6; i++) { HullState h = {{1, 0, 0, 0}, {0, 0, 0, 0}, {1.5707963f * i / 6.f, 0, 0, 0}, {0, 0, 0, 0}}; st.push_back(h); }
  for (int s = -1; s <= 1; s += 2) { HullState h = {{1, 0, 0, 0}, {(float)s, 0, 0, 0}, {1.5707963f, 0, 0, 0}, {0, 0, 0, 0}}; st.push_back(h); }
}
} } // namespace aircraftBuild::specter
