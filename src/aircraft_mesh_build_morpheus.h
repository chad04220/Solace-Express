// Morpheus (the XR-50) mesh-build authoring. Shapes, poses and materials remain in their own GLSL modules
// (morpheus_sdf.glsl mrPartField, plane_parts.glsl mrPartPose).
#pragma once
#include "aircraft_mesh_build_research_gear.h"
namespace aircraftBuild { namespace morpheus {
// each part in its own frame: the wing panel and its flaperon in the panel's (from its pivot: x along the span, z aft,
// unswept); a V-tail surface in its own (from its pivot: x along the span, y through it, z aft); a nozzle from its
// swivel along +z; the fan's top door from its hinge (aft), a bottom door from its hinge (outboard), the fan from its hub
inline bool partBox(int type, vec3& lo, vec3& hi, float& h) {
  switch (type) {
    case PT_MR_WING: lo = vec3(-0.15f, -0.11f, -1.12f); hi = vec3(5.95f, 0.11f, 1.42f); h = 0.007f; return true;
    case PT_MR_FLAPERON: lo = vec3(1.15f, -0.07f, 0.12f); hi = vec3(5.45f, 0.07f, 1.45f); h = 0.005f; return true;
    case PT_MR_TAIL: lo = vec3(-0.12f, -0.11f, -1.15f); hi = vec3(2.95f, 0.11f, 2.0f); h = 0.006f; return true;
    case PT_MR_NOZZLE: lo = vec3(-0.56f, -0.56f, -0.12f); hi = vec3(0.56f, 0.56f, 1.22f); h = 0.005f; return true;
    case PT_MR_FANDOOR: lo = vec3(-0.72f, -0.06f, -1.32f); hi = vec3(0.72f, 0.30f, 0.06f); h = 0.005f; return true;
    case PT_MR_FAN: lo = vec3(-0.6f, -0.2f, -0.6f); hi = vec3(0.6f, 0.2f, 0.6f); h = 0.004f; return true;
    case PT_MR_FANBOT: lo = vec3(-0.66f, -0.05f, -0.66f); hi = vec3(0.04f, 0.05f, 0.66f); h = 0.005f; return true;
  }
  return false;
}
inline int parts(const float*, bool inside, PartInst* out, int) {
  int n = 0;
  if (!inside) {   // the XR-50: per side its wing panel, flaperon, V-tail surface, nozzle and fan door below; the fan and its top door; its gear (the XR-30's parts)
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {PT_MR_WING, (float)s, 0}; out[n++] = {PT_MR_FLAPERON, (float)s, 0}; out[n++] = {PT_MR_TAIL, (float)s, 0};
      out[n++] = {PT_MR_NOZZLE, (float)s, 0}; out[n++] = {PT_MR_FANBOT, (float)s, 0};
      out[n++] = {PT_JT_LEGM, (float)s, 0}; out[n++] = {PT_JT_WHEELM, (float)s, 0}; out[n++] = {PT_JT_DOORM, (float)s, -1}; out[n++] = {PT_JT_DOORM, (float)s, 1};
    }
    out[n++] = {PT_MR_FANDOOR, 0, 0}; out[n++] = {PT_MR_FAN, 0, 0};
    out[n++] = {PT_JT_LEGN, 0, 0}; out[n++] = {PT_JT_WHEELN, 0, 0}; out[n++] = {PT_JT_DOORN, 0, -1}; out[n++] = {PT_JT_DOORN, 0, 1};
    return n;
  }
  // (its cockpit is the XR-30's sealed pod: the side stick, the throttle and the pedals)
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
} } // namespace aircraftBuild::morpheus
