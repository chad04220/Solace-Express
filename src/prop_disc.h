// AI propeller discs: CPU-side geometry and conservative screen bounds, without a GL context.
#pragma once
#include "common.h"

struct TrafficPropDisc {
  vec3 centre, right, up;  // camera-relative world coordinates (subtract the camera before rotating the hub)
  float radius = 0, angle = 0, blur = 0, hubRadius = 0;
  int blades = 2;
};
struct PropDiscBounds { float x0 = -1, y0 = -1, x1 = 1, y1 = 1; };

// Mirrors modelProps using the existing 32-texel traffic row; no player uniforms or roster-index guesses.
inline int trafficPropGeometry(const float* t, vec3 camera, int blades, TrafficPropDisc out[2]) {
  const int engine = (int)(t[2] + 0.5f);
  const float radius = t[17*4 + 2];
  if (engine < 0 || engine > 3 || !(radius > 0.f) || !std::isfinite(radius)) return 0;
  const vec3 r(t[25*4], t[25*4 + 1], t[25*4 + 2]), u(t[26*4], t[26*4 + 1], t[26*4 + 2]);
  const vec3 b(t[27*4], t[27*4 + 1], t[27*4 + 2]);
  const vec3 rel = vec3(t[24*4], t[24*4 + 1], t[24*4 + 2]) - camera;
  const int count = engine <= 1 ? 1 : 2;
  for (int i = 0; i < count; ++i) {
    const float x = count == 1 ? 0.f : (i ? 1.f : -1.f)*t[16*4];
    const float y = count == 1 ? t[1*4 + 3] : t[16*4 + 1];
    const float z = count == 1 ? t[1*4] - .12f - t[17*4 + 1]*.6f : t[16*4 + 3] - t[17*4 + 1]*1.2f - .08f;
    auto& d = out[i];
    d.centre = rel + r*x + u*y + b*z; d.right = r; d.up = u; d.radius = radius;
    d.hubRadius = t[17*4 + 1];
    d.angle = t[30*4 + 3]; d.blades = std::max(blades, 2);
    // Traffic has no RPM/engine-running field. Its parked .05 throttle is an idle, taxi .25-.3 and flight .45-1.
    // Keep stopped/idle blades distinct, then progressively expose the translucent running disc.
    d.blur = smoothstepf(.08f, .35f, t[29*4 + 3]);
  }
  return count;
}

// Conservative projection of a disc's bounding sphere. Only covered screen rectangles launch fragments; the
// fragment shader intersects the real, oriented disc. This also avoids curved-quad errors in cylindrical feeds.
// c is in camera space (+z behind). Extra NDC padding covers jitter and one raster pixel at the caller.
inline bool propDiscBounds(vec3 c, float radius, float tanY, float aspect, float pano, vec2 pad, PropDiscBounds& out) {
  if (!(radius > 0.f && tanY > 0.f && aspect > 0.f) || !std::isfinite(dot(c,c))) return false;
  float x0 = -1.f, x1 = 1.f, y0 = -1.f, y1 = 1.f;
  auto ratioBounds = [](float lo, float hi, float d0, float d1, float& a, float& b) {
    a = std::min(std::min(lo/d0, lo/d1), std::min(hi/d0, hi/d1));
    b = std::max(std::max(lo/d0, lo/d1), std::max(hi/d0, hi/d1));
  };
  if (pano > 0.f) {
    const float horizontal = sqrtf(c.x*c.x + c.z*c.z);
    if (horizontal > radius + 1e-4f) {
      const float a = atan2f(c.x, -c.z), da = asinf(clampf(radius/horizontal, 0.f, 1.f));
      x0 = (a - da)/pano; x1 = (a + da)/pano;
      // A disc spanning the +/-pi seam can cover both edges of a full cylindrical panorama.
      if (a - da < -PI || a + da > PI) { x0 = -PI/pano; x1 = PI/pano; }
      ratioBounds(c.y - radius, c.y + radius, horizontal - radius, horizontal + radius, y0, y1);
      y0 /= tanY; y1 /= tanY;
    }
  } else {
    const float front = -c.z;
    if (front + radius <= .001f) return false;
    if (front > radius + .001f) {
      ratioBounds(c.x - radius, c.x + radius, front - radius, front + radius, x0, x1);
      ratioBounds(c.y - radius, c.y + radius, front - radius, front + radius, y0, y1);
      x0 /= tanY*aspect; x1 /= tanY*aspect; y0 /= tanY; y1 /= tanY;
    }
  }
  x0 -= pad.x; x1 += pad.x; y0 -= pad.y; y1 += pad.y;
  if (x0 >= 1.f || x1 <= -1.f || y0 >= 1.f || y1 <= -1.f) return false;
  out = {std::max(x0, -1.f), std::max(y0, -1.f), std::min(x1, 1.f), std::min(y1, 1.f)};
  return true;
}
