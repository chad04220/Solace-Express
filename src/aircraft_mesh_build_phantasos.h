// Phantasos (the XR-60) mesh-build authoring. Shapes, poses and materials remain in their own GLSL modules
// (xr60_sdf.glsl pxPartField, plane_parts.glsl pxPartPose).
#pragma once
#include "aircraft_mesh_build_research_gear.h"
namespace aircraftBuild { namespace phantasos {
// each part in its own frame: the tip blade from its hinge (x along its span outward, z aft, unfolded); the elevon
// in the blade's frame from its hinge (x along the span from the root, z aft); a fin from its pivot (x along its span,
// y through it, z aft, uncanted); a petal from its hinge on the ring (x along the ring's radius outward, z aft); the
// ring from its centre, turning about z
inline bool partBox(int type, vec3& lo, vec3& hi, float& h) {
  switch (type) {
    case PT_PX_TIP: lo = vec3(-0.12f, -0.14f, -2.85f); hi = vec3(2.42f, 0.56f, 1.20f); h = 0.006f; return true;
    case PT_PX_ELEVON: lo = vec3(0.10f, -0.08f, 0.60f); hi = vec3(2.80f, 0.08f, 4.70f); h = 0.005f; return true;
    case PT_PX_FIN: lo = vec3(-0.12f, -0.10f, -1.02f); hi = vec3(2.22f, 0.10f, 2.00f); h = 0.006f; return true;
    case PT_PX_PETAL: lo = vec3(-0.06f, -0.46f, -0.08f); hi = vec3(0.30f, 0.46f, 1.12f); h = 0.005f; return true;
    case PT_PX_RING: lo = vec3(-1.32f, -1.32f, -0.22f); hi = vec3(1.32f, 1.32f, 0.22f); h = 0.006f; return true;
  }
  return false;
}
inline int parts(const float*, bool inside, PartInst* out, int) {
  int n = 0;
  if (!inside) {   // the XR-60: per side its tip blade, elevon and fin; the ring's six petals and the ring; its gear (the XR-30's parts)
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {PT_PX_TIP, (float)s, 0}; out[n++] = {PT_PX_ELEVON, (float)s, 0}; out[n++] = {PT_PX_FIN, (float)s, 0};
      out[n++] = {PT_JT_LEGM, (float)s, 0}; out[n++] = {PT_JT_WHEELM, (float)s, 0}; out[n++] = {PT_JT_DOORM, (float)s, -1}; out[n++] = {PT_JT_DOORM, (float)s, 1};
    }
    for (int k = 0; k < 6; k++) out[n++] = {PT_PX_PETAL, (float)k, 0};
    out[n++] = {PT_PX_RING, 0, 0};
    out[n++] = {PT_JT_LEGN, 0, 0}; out[n++] = {PT_JT_WHEELN, 0, 0}; out[n++] = {PT_JT_DOORN, 0, -1}; out[n++] = {PT_JT_DOORN, 0, 1};
    return n;
  }
  // (its operations pod: the side stick, the throttle and the pedals, at the console - plane_parts.glsl)
  out[n++] = {PT_JET_STICK, 0, 0}; out[n++] = {PT_JET_THR, 0, 0}; out[n++] = {PT_WR_PEDAL, -1, 0}; out[n++] = {PT_WR_PEDAL, 1, 0};
  return n;
}
inline PartBakePlan partPlan(int type, const float* M) {
  PartBakePlan p;
  if (partBox(type, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Fixed;
  else if (researchGearBox(type, M, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Survey;
  return p;
}
// (everything that moves on it is a rigid part: its airframe's field is the same in every state)
inline void appendHullStates(std::vector<HullState>&, bool, bool) {}
} } // namespace aircraftBuild::phantasos
