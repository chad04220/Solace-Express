// Solace Express - the round world. The world itself is flat - the islands' 100 km square repeating on every side
// (world.h WRAP_HALF) - and so are the physics; what is drawn lays it over a sphere about the point under the camera,
// each place keeping its distance from that point (an azimuthal equidistant wrap: planet.glsl kPlanet, in step with
// this). Near the camera nothing moves; further off the ground falls away round the curve, the horizon dips as the
// camera climbs, and from high enough the islands repeat across a round world under a sky gone black.
#pragma once
#include "common.h"
#include <cmath>
#include <cstdlib>

static const float PLANET_RADIUS = 6371000.f;   // the planet's radius (m): the Earth's

// the radius drawn (m; 0: the flat world) - PLANET_RADIUS, or PLANETR's (to compare sizes)
inline float planetRadius() {
  static const float r = getenv("PLANETR") ? std::max(0.f, (float)atof(getenv("PLANETR"))) : PLANET_RADIUS;
  return r;
}
// sin x / x (kPlanet planetSinc: its series where x is small)
inline float planetSinc(float x) { const float x2 = x * x; return x2 < 0.01f ? 1.f - x2 / 6.f * (1.f - x2 / 20.f) : sinf(x) / x; }
// a flat-world point: where it is drawn, seen from cam (kPlanet planetPos)
inline vec3 planetPos(vec3 w, vec3 cam, float R = planetRadius()) {
  const float vx = w.x - cam.x, vz = w.z - cam.z, d = sqrtf(vx * vx + vz * vz);
  if (R <= 0.f || d < 1e-2f) return w;
  const float th = d / R, r = R + w.y, k = r / R * planetSinc(th), sh = planetSinc(0.5f * th);
  return vec3(cam.x + vx * k, w.y - 0.5f * r * th * th * sh * sh, cam.z + vz * k);
}
// how far round the planet (m along the ground from the nadir) a point h up sees the sea's horizon; from height a it
// is seen from as far again beyond it (a mountain's top over the horizon). R 0: never (the flat world's 1e9)
inline float planetHorizon(float h, float R = planetRadius()) {
  if (R <= 0.f) return 1e9f;
  return R * acosf(std::min(1.f, R / (R + std::max(h, 0.f))));
}
