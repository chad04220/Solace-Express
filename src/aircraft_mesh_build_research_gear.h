// Existing research landing-gear bounds, shared by Specter and Wraith.
#pragma once
#include "aircraft_mesh_build_types.h"
namespace aircraftBuild {
inline bool researchGearBox(int type, const float* M, vec3& lo, vec3& hi, float& h) {
  auto m = [&](int i, int c) { return M[i * 4 + c]; };
  const float track = m(18, 0), mz = m(18, 2), gh = m(19, 0);
  switch (type) {
    // Shared research gear at rest: extended legs and wheels in body space, doors in their hinge frames.
    case PT_JT_LEGM: lo = vec3(track - 0.4f, -gh - 0.15f, mz - 0.8f); hi = vec3(track + 0.4f, 0.2f, mz + 0.6f); h = 0.005f; return true;   // (a main leg from its hinge, raked or not)
    case PT_JT_WHEELM: lo = vec3(track - 0.35f, -gh - 0.15f, mz - 0.55f); hi = vec3(track + 0.35f, -gh + 0.95f, mz + 0.55f); h = 0.005f; return true;
    case PT_JT_LEGN: lo = vec3(-0.35f, -gh - 0.1f, m(18, 3) - 0.45f); hi = vec3(0.35f, 0.6f, m(18, 3) + 0.45f); h = 0.005f; return true;   // (the nose leg from its pivot, up in the fuselage)
    case PT_JT_WHEELN: lo = vec3(-0.35f, -gh - 0.15f, m(18, 3) - 0.5f); hi = vec3(0.35f, -gh + 0.85f, m(18, 3) + 0.5f); h = 0.004f; return true;
    case PT_JT_DOORM: case PT_JT_DOORN: lo = vec3(-0.06f, -0.06f, -1.5f); hi = vec3(0.5f, 0.03f, 1.5f); h = 0.004f; return true;   // (the swing and fold wells' long doors)
  }
  return false;
}
} // namespace aircraftBuild
