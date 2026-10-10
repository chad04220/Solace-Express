// CPU mirror of the main-gear rigid poses in shaders/plane_parts.glsl.
// Used once at breakup so debris bounds and mass centres follow the visible gear.
#pragma once
#include "aircraft.h"
#include "models.h"

namespace gearBreakup {
struct Pose { vec3 hinge; quat rotation; };
inline float up(float gear) { return clampf((1.f - gear) * 1.25f, 0.f, 1.f); }
inline float dihedral(const ModelDef& m) { return tanf(m.wing[6] * DEG); }

inline float wingHalf(const ModelDef& m, float span, float z) {
  const float k = clampf(span / m.wing[0], 0.f, 1.f);
  const float chord = lerpf(m.wing[1], m.wing[2], k), le = m.wing[5] + m.wing[3] * k;
  const float r1 = m.wing[7] * chord * .5f, r2 = std::max(.004f * chord, .005f);
  return lerpf(r1, r2, clampf((z - le - r1) / std::max(chord - r1 - r2, .01f), 0.f, 1.f));
}
inline float stowGap(const ModelDef& m, float span, float z, float radius, float slope) {
  const float a = .55f * radius, b = .9f * radius;
  float y = wingHalf(m, span, z) - .16f;
  y = std::min(y, std::min(wingHalf(m, span + a, z) + a * slope, wingHalf(m, span - a, z) - a * slope) - .145f);
  y = std::min(y, std::min(wingHalf(m, span, z + a), wingHalf(m, span, z - a)) - .145f);
  y = std::min(y, std::min(wingHalf(m, span + b, z) + b * slope, wingHalf(m, span - b, z) - b * slope) - .116f);
  return std::min(y, std::min(wingHalf(m, span, z + b), wingHalf(m, span, z - b)) - .116f);
}
inline float wingHingeZ(const ModelDef& m, float span) {
  const float k = clampf(span / m.wing[0], 0.f, 1.f);
  return m.wing[5] + m.wing[3] * k + .74f * lerpf(m.wing[1], m.wing[2], k);
}
// Centre y and radius of the turboprop nacelle at body z.
inline vec2 nacSection(const ModelDef& m, float z) {
  const float z1 = m.nacZ0 + m.nacLen * .3f;
  if (z < z1) {
    const float t = clampf((z - m.nacZ0) / std::max(z1 - m.nacZ0, .01f), 0.f, 1.f);
    return vec2(m.nacY + .02f * t, lerpf(m.nacR * .72f, m.nacR, t));
  }
  const float wingY = m.wing[4] + m.nacX * dihedral(m);
  const float t = clampf((z - z1) / std::max(m.nacLen * .7f, .01f), 0.f, 1.f);
  return vec2(m.nacY + lerpf(.02f, wingY - m.nacY - m.nacR * .25f, t), lerpf(m.nacR, m.nacR * .35f, t));
}
struct NacFold { vec3 hinge; float angle, z0, z1, zs; vec2 stowed; };
inline NacFold nacFold(const ModelDef& m, const GearStations& gs, float gh) {
  const float tz = m.nacZ0 + m.nacLen * .3f - .2f;
  const vec2 W(m.wheelR - gh, gs.mainZ), T(nacSection(m, tz).x, tz), middle = (W + T) * .5f, delta = T - W;
  float pivotZ = gs.mainZ, pivotY = 0.f, angle = 0.f;
  for (int i = 0; i < 3; ++i) {
    const vec2 section = nacSection(m, pivotZ);
    pivotY = section.x - section.y + .22f;
    pivotZ = middle.y - delta.x * (pivotY - middle.x) / (fabsf(delta.y) > 1e-4f ? delta.y : 1e-4f);
    const vec2 a = W - vec2(pivotY, pivotZ), b = T - vec2(pivotY, pivotZ);
    angle = atan2f(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y);
  }
  return {vec3(gs.track, pivotY, pivotZ), angle, tz - m.wheelR - .1f, tz + m.wheelR + .12f, pivotZ + .2f, T};
}
// Research nose-gear pivot uses the authored belly, including the Wraith facets.
inline float jetBelly(const ModelDef& m, float z, float x) {
  if (m.engine == 6) {
    const float zc = clampf(z, -8.3f, 7.8f), yc = -.1f - .12f * smoothstepf(-5.f, -8.4f, zc);
    const float width = zc < -4.4f ? (zc + 8.4f) * .30f : zc < 4.5f ? 1.2f + (zc + 4.4f) * .012f : 1.31f - (zc - 4.5f) * .13f;
    const float bottom = yc - std::min((zc + 8.4f) * .12f, .48f) + std::max(zc - 5.f, 0.f) * .06f;
    return bottom + std::max(fabsf(x) - width * .42f, 0.f) * (yc - bottom) / std::max(width * .58f, 1e-3f);
  }
  float hw, hh, cy; modelSection(m, z, hw, hh, cy);
  return cy - hh * sqrtf(std::max(1.f - x * x / (hw * hw), 0.f));
}
inline float jetWingTop(const ModelDef& m, float x, float z) {
  const bool wraith = m.engine == 6;
  const float span = wraith ? x - 1.f : x, chordPos = wraith ? z + 3.f : z + 1.6f;
  const float k = clampf(span / (wraith ? 5.2f : 5.6f), 0.f, 1.f);
  const float chord = lerpf(wraith ? 8.2f : 7.2f, wraith ? 1.35f : 1.2f, k);
  const float le = (wraith ? 4.7f : 5.6f) * k, r1 = (wraith ? .035f : .04f) * chord * .5f, r2 = std::max(.004f * chord, .005f);
  const float half = lerpf(r1, r2, clampf((chordPos - le - r1) / std::max(chord - r1 - r2, .01f), 0.f, 1.f));
  return (wraith ? -.12f - .12f * smoothstepf(-5.f, -8.4f, z) - span * .012f : -.18f - span * .035f) + half;
}
inline float jetStowY(const ModelDef& m, float x, float z) {
  float y = jetWingTop(m, x, z) - .16f;
  for (float radius : {.21f, .34f}) {
    const float edge = std::min({jetWingTop(m, x + radius, z), jetWingTop(m, x - radius, z),
                                 jetWingTop(m, x, z + radius), jetWingTop(m, x, z - radius)});
    y = std::min(y, edge - (radius < .3f ? .145f : .116f));
  }
  return y;
}
// Fore/aft swing plus the axle's twist about the leg. Matches gearSwingR.
inline quat swingRotation(vec3 v, float direction, float amount) {
  const float angle = direction * 1.5707963f - atan2f(v.z, v.y) + 3.14159265f;
  const float chi = angle - 6.2831853f * floorf(angle / 6.2831853f) - 3.14159265f;
  const vec3 axis = normalize(v);
  const quat whole = quat::axisAngle(vec3(1, 0, 0), chi) * quat::axisAngle(axis, 1.5707963f);
  const float twist = whole.rotate(vec3(1, 0, 0)).y < 0.f ? 1.5707963f : -1.5707963f;
  return quat::axisAngle(vec3(1, 0, 0), chi * amount) * quat::axisAngle(axis, twist * smoothstepf(0.f, .6f, amount));
}

// Right main in body coordinates. Apply H + R*(point-H), then mirror x for left.
inline Pose mainPose(const AircraftSpec& s, const ModelDef& m, const GearStations& gs, float gh, float gear) {
  const float amount = up(gear), track = gs.track, mz = gs.mainZ, radius = m.wheelR;
  const vec3 wheel(track, radius - gh, mz);
  if (s.special) {
    const bool wraith = m.engine == 6;
    const float direction = wraith ? -1.f : 1.f;
    vec3 hinge(track, -.2f, wraith ? mz - .5f : mz);
    for (int i = 0; i < 2; ++i) hinge.y = jetStowY(m, track, hinge.z + direction * length(vec3(track, .38f - gh, mz) - hinge));
    return {hinge, swingRotation(vec3(track, .38f - gh, mz) - hinge, direction, amount)};
  }
  if (m.gear <= 2) {
    float hw, hh, cy; modelSection(m, mz, hw, hh, cy);
    return {vec3(hw * (m.gear == 2 ? .8f : m.gear == 0 ? .75f : .7f), cy - hh * (m.gear == 1 ? .85f : .8f), mz), quat()};
  }
  if (m.gear == 3) {
    const NacFold f = nacFold(m, gs, gh);
    return {f.hinge, quat::axisAngle(vec3(1, 0, 0), amount * f.angle)};
  }
  const bool atlas = &s == &kAircraft[kAtlas];
  if (atlas) {
    // Same swept two-axis trunnion as atlasGearFoldR. partRxz is a
    // negative-Y rotation; preserving that sign and multiplication order
    // keeps partially extended debris aligned with the baked rigid assembly.
    const vec3 hinge(track, -1.30f, 2.72f), v = wheel - hinge;
    const float dx = 1.85f - track, dz = -sqrtf(std::max(v.y*v.y + v.z*v.z - dx*dx, .01f));
    const float yaw = atan2f(v.y*dz - v.z*dx, v.y*dx + v.z*dz);
    return {hinge, quat::axisAngle(vec3(0, 1, 0), -yaw*smoothstepf(0.f, .65f, amount)) *
                   quat::axisAngle(vec3(0, 0, 1), -1.5707963f*smoothstepf(.25f, 1.f, amount))};
  }
  const float slope = dihedral(m), fairAft = std::min(wingHingeZ(m, 0.f), wingHingeZ(m, track + .12f)) - .03f;
  if (m.gear == 4 && mz + radius + .03f > fairAft + .005f) {
    const float z = std::min(mz, std::min(wingHingeZ(m, track - radius), wingHingeZ(m, track + radius)) - .13f);
    vec3 hinge(track, m.wing[4] + track * slope, z);
    for (int i = 0; i < 2; ++i) {
      const float targetZ = z - length(wheel - hinge);
      hinge.y = m.wing[4] + track * slope + stowGap(m, track, targetZ, radius, slope);
    }
    return {hinge, swingRotation(wheel - hinge, -1.f, amount)};
  }
  const float dl = atanf(slope);
  float height = m.wing[4] + track * slope;
  for (int i = 0; i < 4; ++i) {
    const float leg = height - wheel.y, foldedX = track - leg * cosf(dl);
    height = m.wing[4] + foldedX * slope + stowGap(m, foldedX, mz, radius, 0.f) + leg * sinf(dl);
  }
  return {vec3(track, height, mz), quat::axisAngle(vec3(0, 0, 1), amount * (dl - 1.5707963f))};
}
} // namespace gearBreakup
