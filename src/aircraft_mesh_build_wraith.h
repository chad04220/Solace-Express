// Wraith mesh-build authoring. Shapes, poses and materials remain in their own GLSL modules.
#pragma once
#include "aircraft_mesh_build_research_gear.h"
namespace aircraftBuild { namespace wraith {
inline bool partBox(int type, vec3& lo, vec3& hi, float& h) {
  switch (type) {
    case PT_WR_PODF: case PT_WR_PODR: lo = vec3(-0.62f, -0.58f, -1.42f); hi = vec3(0.62f, 0.58f, 1.34f); h = 0.008f; return true;
    case PT_WR_FAN: lo = vec3(-0.46f, -0.46f, -1.13f); hi = vec3(0.46f, 0.46f, -0.59f); h = 0.005f; return true;
    case PT_WR_VANEC: lo = vec3(-0.31f, -0.026f, -0.09f); hi = vec3(0.31f, 0.026f, 0.09f); h = 0.003f; return true;
    case PT_WR_VANEO: lo = vec3(-0.25f, -0.026f, -0.09f); hi = vec3(0.25f, 0.026f, 0.09f); h = 0.003f; return true;
    case PT_WR_VANEY: lo = vec3(-0.024f, -0.26f, -0.085f); hi = vec3(0.024f, 0.26f, 0.085f); h = 0.003f; return true;
    case PT_WR_PETAL: lo = vec3(0.24f, -0.17f, 0.92f); hi = vec3(0.5f, 0.17f, 1.4f); h = 0.004f; return true;
    case PT_WR_DOOR: lo = vec3(-0.545f, -0.04f, -1.68f); hi = vec3(0.015f, 0.012f, 1.68f); h = 0.006f; return true;
    case PT_WR_BOMB: lo = vec3(-0.31f, -0.31f, -0.31f); hi = vec3(0.31f, 0.31f, 0.31f); h = 0.006f; return true;
    case PT_WR_HATCH: lo = vec3(-0.415f, -0.014f, -0.515f); hi = vec3(0.015f, 0.036f, 0.515f); h = 0.004f; return true;
    case PT_WR_TURRET: lo = vec3(-0.175f, -0.145f, -0.94f); hi = vec3(0.175f, 0.145f, 0.365f); h = 0.004f; return true;
    case PT_WR_MUZZLE: lo = vec3(-0.06f, -0.08f, -1.15f); hi = vec3(0.06f, 0.04f, -0.66f); h = 0.003f; return true;
    case PT_WR_ARM: lo = vec3(-0.05f, -0.43f, -0.3f); hi = vec3(0.05f, 0.05f, 0.05f); h = 0.003f; return true;
    case PT_WR_ELEVON: lo = vec3(2.87f, -0.25f, 2.77f); hi = vec3(6.03f, -0.06f, 4.46f); h = 0.006f; return true;
    case PT_WR_ACT: lo = vec3(-0.08f, -0.08f, -0.08f); hi = vec3(0.08f, 0.08f, 1.08f); h = 0.003f; return true;   // a tilt actuator (unit length: its pose stretches it)
    case PT_WR_RUDV: {   // the right ruddervator: its (span, chord, thickness) box through the canted fin's frame (wrPartField)
      const float C = cosf(0.72f), S = sinf(0.72f);
      lo = vec3(1e9f, 1e9f, 1e9f); hi = vec3(-1e9f, -1e9f, -1e9f);
      for (int c = 0; c < 8; c++) {
        float fs = (c & 1) ? 2.95f : 0.1f, ch = (c & 2) ? 3.25f : 1.75f, t = (c & 4) ? 0.07f : -0.07f;
        float z = ch + 4.4f, qy = fs - 0.75f;
        float x = C * t + S * qy + 1.05f, y = -S * t + C * qy + 0.67f - 0.05f * z;
        lo = vec3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z)); hi = vec3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z));
      }
      h = 0.006f; return true;
    }
  }
  return false;
}
inline int parts(const float*, bool inside, PartInst* out, int) {
  int n = 0;
  if (!inside) {   // the XR-40: per pod its nacelle, fan, vanes, ten iris petals and tilt actuator; the bay doors, the bomb, the turrets, the elevons and ruddervators, the gear
    for (int i = 0; i < 4; i++) {
      out[n++] = {i < 2 ? PT_WR_PODF : PT_WR_PODR, (float)i, 0}; out[n++] = {PT_WR_FAN, (float)i, 0}; out[n++] = {PT_WR_VANEC, (float)i, 0};
      for (int k = -1; k <= 1; k += 2) { out[n++] = {PT_WR_VANEO, (float)i, (float)k}; out[n++] = {PT_WR_VANEY, (float)i, (float)k}; }
      for (int j = 0; j < 10; j++) out[n++] = {PT_WR_PETAL, (float)i, (float)j};
    }
    out[n++] = {PT_WR_BOMB, 0, 0};
    for (int s = -1; s <= 1; s += 2) {
      out[n++] = {PT_WR_DOOR, (float)s, 0}; out[n++] = {PT_WR_HATCH, (float)s, 0}; out[n++] = {PT_WR_TURRET, (float)s, 0}; out[n++] = {PT_WR_MUZZLE, (float)s, 0}; out[n++] = {PT_WR_ARM, (float)s, 0};
      out[n++] = {PT_WR_ELEVON, (float)s, 0}; out[n++] = {PT_WR_RUDV, (float)s, 0};
      out[n++] = {PT_JT_LEGM, (float)s, 0}; out[n++] = {PT_JT_WHEELM, (float)s, 0}; out[n++] = {PT_JT_DOORM, (float)s, -1}; out[n++] = {PT_JT_DOORM, (float)s, 1};   // its gear (the XR-30's parts)
    }
    out[n++] = {PT_JT_LEGN, 0, 0}; out[n++] = {PT_JT_WHEELN, 0, 0}; out[n++] = {PT_JT_DOORN, 0, -1}; out[n++] = {PT_JT_DOORN, 0, 1};
    for (int i = 0; i < 4; i++) out[n++] = {PT_WR_ACT, (float)i, 0};   // the pods' tilt actuators
    return n;
  }
  out[n++] = {PT_WR_STICK, 0, 0}; out[n++] = {PT_WR_THR, 0, 0}; out[n++] = {PT_WR_PEDAL, -1, 0}; out[n++] = {PT_WR_PEDAL, 1, 0};
  return n;
}
inline PartBakePlan partPlan(int type, const float* M) {
  PartBakePlan p;
  if (partBox(type, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Fixed;
  else if (researchGearBox(type, M, p.lo, p.hi, p.lattice)) p.sampling = PartSampling::Survey;
  return p;
}
inline void appendHullStates(std::vector<HullState>& st, bool inside, bool meshBake) {
  if (inside) return;
  const bool partsOnly = meshBake;
   // the XR-40: its pods, vanes, fan, bay, turrets and bomb
    auto addWr = [&](float tilt, float yawv, float thr, float vane, float fan, float bay, float las, float bomb) {
      HullState h = {{1, 0, 0, 0}, {0, 0, 0, 0}, {tilt, yawv, thr, vane}, {fan, bay, las, bomb}};
      st.push_back(h);
    };
    if (!partsOnly) for (int i = 1; i <= 8; i++) addWr(i / 8.f, 0, 0, 0, 0, 0, 0, 1);   // pod tilt (the actuators: rigid parts too)
    if (!partsOnly) for (int i = 0; i <= 4; i++) { float s = i * 0.5f - 1.f; addWr(0, s, 0, 0, 0, 0, 0, 1); addWr(0, 0, 0, s, 0, 0, 0, 1); }   // vanes
    if (!partsOnly) {
      for (int i = 1; i <= 4; i++) addWr(0, 0, i * 0.4f, 0, 0, 0, 0, 1);                 // thrust (the iris)
      for (int i = 1; i <= 7; i++) addWr(0, 0, 0, 0, i * 0.7854f, 0, 0, 1);              // the fans round
      for (int i = 1; i <= 4; i++) { addWr(0, 0, 0, 0, 0, i * 0.25f, 0, 1); addWr(0, 0, 0, 0, 0, 0, i * 0.25f, 1); }   // bay, turrets
      addWr(0, 0, 0, 0, 0, 1, 0, 0);                                                     // bay open, bomb away
    }
  }
} } // namespace aircraftBuild::wraith
