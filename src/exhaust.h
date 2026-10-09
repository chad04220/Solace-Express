// Shared, bounded exhaust geometry/state for plume shading, local light and CPU checks.
#pragma once
#include "aircraft.h"
#include "models.h"

struct ExhaustVisual {
  int count = 0;                    // active nozzles only; zero skips the exhaust effects pass
  float exit[4][4] = {};             // body-space exit xyz, horizontal half-width / radius
  float axis[4][4] = {};             // normalized body-space exhaust direction xyz, vertical half-width / radius
  float power[4][4] = {};            // actual spool, reheat, per-engine intensity, Mach
  int engine[4] = {};                // source engine index after inactive nozzles are removed
};

inline float exhaustFinite(float x, float lo, float hi) {
  return std::isfinite(x) ? clampf(x, lo, hi) : lo;
}
inline bool hasReheat(const AircraftSpec& s) {
  return s.engineType == ENG_JET && (s.special != 0 || s.designMach > 1.f);
}
inline float exhaustEngineHealth(const Plane& p, int i) {
  return p.spec && p.engineRunning && p.fuel > 0.f && i >= 0 && i < p.spec->engines && i < 4
         ? exhaustFinite(p.fail.engineHealth[i], 0.f, 1.f) : 0.f;
}
inline float exhaustPodThrust(const Plane& p, int i) {
  if (i < 0 || i >= 4) return 0.f;
  return exhaustFinite(p.podThr[i], 0.f, 1.6f)*exhaustEngineHealth(p, i);
}
inline float exhaustNozzleAngle(const Plane& p) {
  return exhaustFinite(p.nozzle, 0.f, 1.f)*(.5f*PI)
         - exhaustFinite(p.ctl.pitch + p.ctl.trim*.3f, -1.f, 1.f)*.5f;
}
inline vec3 exhaustPosition(const ExhaustVisual& e, int i) {
  return vec3(e.exit[i][0], e.exit[i][1], e.exit[i][2]);
}
inline vec3 exhaustDirection(const ExhaustVisual& e, int i) {
  return vec3(e.axis[i][0], e.axis[i][1], e.axis[i][2]);
}

inline ExhaustVisual buildExhaustVisual(const Plane& p, const ModelDef& m) {
  ExhaustVisual out;
  if (!p.spec || !hasReheat(*p.spec) || !p.engineRunning || !(p.fuel > 0.f)) return out;
  const AircraftSpec& s = *p.spec;
  const float spool = exhaustFinite(p.engineSpool, 0.f, 1.f);
  if (spool < .02f) return out;
  // This is the same activation interval as Plane::thrustAt, including the XR-40's boost.
  const float reheat = smoothstepf(.85f, 1.f, spool);
  const int n = std::min(std::max(s.engines, 0), 4);
  for (int i = 0; i < n; ++i) {
    const float health = exhaustEngineHealth(p, i);
    if (health <= 0.f) continue;
    vec3 pos, dir(0, 0, 1);
    float rx = 0.f, ry = 0.f, intensity = health;
    if (s.special == 2) {
      // The exit moves with the pod, while the jet also follows both vane axes. Match the iris's live radius.
      const float tilt = exhaustFinite(p.podTilt[i], -.5f, .5f*PI + .5f);
      const float a = tilt + exhaustFinite(p.podVane[i], -.7f, .7f);
      const float yaw = exhaustFinite(p.podYaw[i], -.7f, .7f);
      const float thrust = exhaustPodThrust(p, i);
      if (thrust <= 0.f) continue;
      pos = kWraithPods[i] + vec3(0, -sinf(tilt), cosf(tilt))*1.57f;
      dir = vec3(-sinf(yaw), -sinf(a)*cosf(yaw), cosf(a)*cosf(yaw));
      rx = ry = .28f + .1f*clampf(thrust, 0.f, 1.f);
      // Differential thrust modulates emission without falsely enabling boost below actual reheat spool.
      intensity = clampf(thrust*1.3f, 0.f, 1.6f);
    } else if (s.special == 1) {
      // jtNozzleShape's aft lip is 1.06 m behind its moving front-edge pivot; its bore is 0.36 x 0.23 m.
      const float a = exhaustNozzleAngle(p);
      dir = vec3(0, -sinf(a), cosf(a));
      pos = vec3(i == 0 ? -.82f : .82f, -.12f, 7.75f) + dir*1.06f;
      rx = .36f; ry = .23f;
    } else if (m.engine == 4) {
      // Revised Mantis has exactly one circular centerline nozzle; ordinary twins retain their actual nacelles.
      const bool single = s.engines == 1;
      pos = vec3(single ? 0.f : (i == 0 ? -m.nacX : m.nacX), m.nacY, m.nacZ0 + m.nacLen);
      rx = ry = m.nacR*(single ? .72f : .6f);
    } else continue;
    if (!(rx > 0.f && ry > 0.f) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue;
    const int j = out.count++;
    for (int k = 0; k < 3; ++k) { out.exit[j][k] = pos[k]; out.axis[j][k] = dir[k]; }
    out.exit[j][3] = clampf(rx, .05f, 2.f); out.axis[j][3] = clampf(ry, .05f, 2.f);
    out.power[j][0] = spool; out.power[j][1] = reheat; out.power[j][2] = intensity;
    out.power[j][3] = exhaustFinite(p.mach, 0.f, 5.f); out.engine[j] = i;
  }
  return out;
}
