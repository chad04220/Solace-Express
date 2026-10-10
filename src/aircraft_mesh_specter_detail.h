// Local extraction detail for the Specter's unchanged 9 mm overhead switch stems.
#pragma once
#include "common.h"

namespace aircraftMesh {
inline constexpr int kSpecterToggleSub = 16; // 62.5 mm / 16 = 3.90625 mm, only this patch
inline constexpr float kSpecterToggleRingSink = .0015f;
inline bool specterToggleDetailEnabled(const float* packed, bool inside) {
  return inside && int(packed[2] + .5f) == 5;
}
// The authored bank in mapJetCockpit: q=p-eye, offset (0,.46,-.25), then
// rotate yz by +.55. This box covers all 27 capsules, including their 4.5 mm
// radius, with at least 5.5 mm to spare. A projected cell radius conservatively
// tests overlap: no capsule can be missed because its lattice cell centre lies
// outside the rotated box. Only the surrounding one-cell ring overlaps the
// existing 7.8125 mm patch; unrelated cabin cells keep their original spacing.
inline bool specterToggleDetailCell(vec3 centre, const float eye[3], float halfCell) {
  const float C = cosf(.55f), S = sinf(.55f);
  const float x = centre.x - eye[0];
  if (fabsf(x) > .210f + halfCell) return false;
  const float y = centre.y - eye[1] - .46f, z = centre.z - eye[2] + .25f;
  const float oy = C*y - S*z, oz = S*y + C*z;
  const float radius = (C + S)*halfCell;
  return oy + radius >= -.052f && oy - radius <= -.010f
      && oz + radius >= -.070f && oz - radius <= .078f;
}
} // namespace aircraftMesh
