// Solace Express - shared, CPU-testable scenery range policy (metres).
#pragma once
#include "entities.h"

struct EntRanges { float tree, bush, rock, big, build, t0, t1, r0, r1, b0, b1, sh0, sh1; int shRes; };

// Keep the original cheaper policy for auxiliary camera feeds and shadow mesh selection.
// Main-view foliage can reach farther without also raising those passes' geometry budgets.
inline EntRanges entBaseRangesFor(int q) {
  if (q <= 0) return {2600, 800, 1700, 9000, 9000, 190, 900, 280, 1100, 550, 2800, 300, 1600, 2048};
  if (q == 1) return {4500, 1300, 2800, 13000, 13000, 260, 1300, 380, 1600, 850, 4000, 420, 2600, 2048};
  return {7000, 1800, 3800, 18000, 18000, 360, 1800, 480, 2000, 1300, 5500, 520, 3600, 4096};
}

inline EntRanges entRangesFor(int q, bool cameraFeed = false) {
  if (cameraFeed) return entBaseRangesFor(0);
  EntRanges r = entBaseRangesFor(q);
  // About 10-12% farther at every foliage step, rather than a large global multiplier.
  // Placement density, distance thinning, meshes, other scenery and shadow maps are unchanged.
  if (q <= 0) { r.t0 = 210; r.t1 = 1000; r.tree = 2900; r.bush = 900; }
  else if (q == 1) { r.t0 = 290; r.t1 = 1450; r.tree = 5000; r.bush = 1450; }
  else { r.t0 = 400; r.t1 = 2000; r.tree = 7800; r.bush = 2000; }
  return r;
}

inline float entRangeOf(const EntRanges& r, int k) {
  if (k == EK_RWYLIGHT) return 1800.f;   // beyond this a glint sprite stands in
  if (k == EK_PAPI) return 3500.f;
  if (k == EK_CAR || k == EK_FUEL_PUMP) return std::min(r.build, 1600.f);
  if (k == EK_FENCE) return std::min(r.build, 1100.f);
  if (k == EK_WINDSOCK || k == EK_TRUCK || k == EK_LOCALIZER) return std::min(r.build, 2500.f);
  if (k == EK_GA_PLANE || k == EK_JETBRIDGE || k == EK_MAST || k == EK_FLOODMAST || k == EK_BEACON) return std::min(r.build, 4500.f);
  if (k == EK_BUSH) return r.bush;
  if (entClass(k) == EC_TREE) return r.tree;
  if (k <= EK_SLAB) return r.rock;
  if (entClass(k) == EC_ROCK) return r.big;
  return r.build;
}

inline void entLodLimits(const EntRanges& r, int k, float& l0, float& l1) {
  int c = entClass(k);
  if (c == EC_TREE) { l0 = r.t0; l1 = r.t1; if (k == EK_BUSH) { l0 *= 0.6f; l1 *= 0.5f; } }
  else if (c == EC_ROCK) { l0 = r.r0 * (k >= EK_OUTCROP ? 2.5f : 1.f); l1 = r.r1 * (k >= EK_OUTCROP ? 3.f : 1.f); }
  else { l0 = r.b0; l1 = r.b1; }
}

inline int entLodAt(float distance, float l0, float l1) { return distance < l0 ? 0 : distance < l1 ? 1 : 2; }
