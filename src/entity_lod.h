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

// A tree's change of detail level is cross-faded, never switched: over the last kEntLodFade of the distance to each
// switch, the nearer model dissolves out as the farther one dissolves in - complementary halves of one screen-door
// (ent_vs.glsl / ent_fs2.glsl), so every pixel is one or the other. Only the farther, cheaper model is drawn the extra
// way; the nearer one still ends at the switch.
constexpr float kEntLodFade = 0.15f;
inline bool entLodFades(int k) { return entClass(k) == EC_TREE; }
inline float entLodT(float d, float L) {   // 0 the nearer model .. 1 the farther one (smoothstep, as the shader's)
  float t = std::min(std::max((d - L * (1.f - kEntLodFade)) / (L * kEntLodFade), 0.f), 1.f); return t * t * (3.f - 2.f * t);
}
// the farther level also drawn at this distance, fading in (-1: none)
inline int entLodAlso(float d, float l0, float l1) {
  if (d >= l0 * (1.f - kEntLodFade) && d < l0) return 1;
  if (d >= l1 * (1.f - kEntLodFade) && d < l1) return 2;
  return -1;
}
// whether any distance in [dmin, dmax] is inside a fade (then a chunk's instances can't go to one level in a block)
inline bool entLodSpanFades(float dmin, float dmax, float l0, float l1) {
  return (dmax >= l0 * (1.f - kEntLodFade) && dmin < l0) || (dmax >= l1 * (1.f - kEntLodFade) && dmin < l1);
}
// the share of the screen-door each level keeps at this distance: values in [lo, hi)
inline void entLodKeep(int lod, float d, float l0, float l1, float& lo, float& hi) {
  if (lod > 2) { lo = hi = 0.f; return; } // extra close slot is handled by entDetailKeep
  const float t0 = entLodT(d, l0), t1 = entLodT(d, l1);
  lo = lod == 0 ? t0 : lod == 1 ? t1 : 0.f;
  hi = lod == 0 ? 1.f : lod == 1 ? t0 : t1;
}

// Close inspection detail is an additional slot, not a shorter replacement for
// the established three distance tiers. Shadows and camera feeds retain those tiers.
constexpr int kEntCloseLod = 3;
inline float entCloseLimit(const EntRanges& r, int k) {
  const float q = std::clamp(r.b0 / 850.f, .7f, 1.2f);
  const auto& i = kEntInfo[k];
  if (entClass(k) == EC_TREE) return (k == EK_BUSH ? 38.f : 78.f)*q;
  if (entClass(k) == EC_ROCK) return (k >= EK_OUTCROP ? 155.f : 55.f)*q;
  if (k == EK_CAR) return 85.f*q;
  if (k == EK_TRUCK) return 120.f*q;
  return std::clamp(std::max(i.h, std::max(i.hx,i.hz)*2.f)*7.f, 55.f, 280.f)*q;
}
inline int entDetailAt(float d, float close, float l0, float l1) {
  return close > 0.f && d < close ? kEntCloseLod : entLodAt(d,l0,l1);
}
inline int entDetailAlso(int kind, float d, float close, float l0, float l1) {
  if (close > 0.f && d < close) return d >= close*(1.f-kEntLodFade) ? 0 : -1;
  return entLodFades(kind) ? entLodAlso(d,l0,l1) : -1;
}
inline bool entDetailSpanFades(int kind, float a, float b, float close, float l0, float l1) {
  return (close > 0.f && b >= close*(1.f-kEntLodFade) && a < close) ||
    (entLodFades(kind) && entLodSpanFades(a,b,l0,l1));
}
inline void entDetailKeep(int kind,int lod,float d,float close,float l0,float l1,float& lo,float& hi) {
  if (lod == kEntCloseLod) { lo = close > 0.f ? entLodT(d,close) : 1.f; hi=1.f; return; }
  if (entLodFades(kind)) entLodKeep(lod,d,l0,l1,lo,hi);
  else { lo=0.f; hi=lod==entLodAt(d,l0,l1)?1.f:0.f; }
  if (lod==0 && close>0.f) hi*=entLodT(d,close);
}
