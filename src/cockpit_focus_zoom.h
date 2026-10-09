// CPU-only gaze-centred cockpit magnification. No screen-space or zoomed-FOV feedback.
#pragma once
#include "common.h"

struct CockpitFocusTarget {
  vec3 center, normal;   // body-space surface centre and outward normal, toward the pilot
  vec2 half;            // visible display half width/height, excluding its housing
};

struct CockpitFocusZoom {
  int target = -1, aircraft = -1;
  float zoom = 1.f;

  // Map the unzoomed aim ray onto the physical display, then measure its exact centre distance
  // in normalized display bounds. This continuous angular-aim mapping is independent of FOV,
  // viewport size, aircraft attitude and zoom. The elliptical core excludes the bezel/corners.
  static float proximity(vec3 eye, vec3 aim, const CockpitFocusTarget& p) {
    vec3 n = normalize(p.normal), right = normalize(cross(vec3(0, 1, 0), n));
    if (length(right) < .5f) right = vec3(1, 0, 0); // horizontal console: width along body x
    if (length(n) < .5f || p.half.x <= 0.f || p.half.y <= 0.f) return 1e6f;
    float facing = dot(aim, n);
    if (facing >= -1e-5f || dot(eye - p.center, n) <= 0.f) return 1e6f;
    float distance = dot(p.center - eye, n) / facing;
    if (distance <= 0.f) return 1e6f;
    vec3 offset = eye + aim * distance - p.center;
    float x = dot(offset, right) / p.half.x;
    float y = dot(offset, cross(n, right)) / p.half.y;
    return sqrtf(x*x + y*y);
  }

  float update(float dt, bool cockpit, int model, bool enabled, vec3 eye, vec3 aim,
               const CockpitFocusTarget* panels, int count, float manualZoom) {
    if (!cockpit || model != aircraft) {
      target = -1; zoom = 1.f; aircraft = cockpit ? model : -1;
    }
    if (!cockpit) return 1.f;
    float best = 1.f, current = 1e6f; int candidate = -1;
    if (enabled) for (int i=0; i<count; ++i) {
      float r = proximity(eye, aim, panels[i]);
      if (i == target) current = r;
      if (r < best) { best = r; candidate = i; }
    }
    // Retain the previous panel identity through edge jitter; another must be clearly closer.
    // Magnification below uses the nearest-centre envelope instead of this identity: retaining
    // an old panel must never produce a step in desired zoom when the selection finally switches.
    if (!enabled || target < 0 || target >= count || current > 1.12f ||
        (candidate >= 0 && best + .15f < current)) target = candidate;
    // Each panel contributes a continuous distance-based curve. Their maximum (minimum
    // normalized centre distance) is also continuous, including between overlapping panels.
    // r=1 at the display edge: no auto zoom; r<=.12 in the stable centre core: full 1.8x.
    float amount = enabled ? 1.f - smoothstepf(.12f, 1.f, best) : 0.f;
    float desired = std::max(clampf(manualZoom, 1.f, 4.f), 1.f + .8f * amount);
    // One exact exponential, including manual input, avoids double smoothing and frame-rate drift.
    // Looking away smoothly returns to the user's wheel setting, never modifies that setting.
    const float rate = manualZoom >= 1.f + .8f * amount ? 10.f : (desired > zoom ? 7.f : 9.f);
    zoom = approach(zoom, desired, rate, std::max(dt, 0.f));
    return zoom;
  }
};
