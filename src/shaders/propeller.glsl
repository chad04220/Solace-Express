//! kPropellerGLSL
//! Shared player/traffic propeller profile. Analytic angular coverage, no extra geometry or blur samples.
// Integral of a periodic blade-shaped pulse, in blade-pitch units. Averaging a complete pitch gives its
// actual covered area, independent of phase: high RPM cannot freeze/reverse into opaque wagon-wheel spokes.
float propPulseIntegral(float x, float duty) {
  float p = x + duty*0.5;
  return floor(p)*duty + min(fract(p), duty);
}

// q, dx and dy are in disc-radius units. Derivatives are supplied before scene-depth rejection, so the
// blade edges stay filtered beside the spinner/airframe and at the atan seam, including oblique views.
vec4 propellerVisual(vec2 q, vec2 dx, vec2 dy, float angle, float blur, float blades, float hub, vec3 light) {
  const float tau = 6.28318530718;
  float r = length(q), rr = max(r*r, 0.0001);
  float radialAA = max((abs(dot(q, dx)) + abs(dot(q, dy)))/max(r, 0.01), 0.0005);
  float angularAA = (abs(q.x*dx.y - q.y*dx.x) + abs(q.x*dy.y - q.y*dy.x))/rr*blades/tau;
  // The root underlaps the existing solid model spinner, rather than painting a dark disc on its nose.
  float root = clamp(hub*0.32, 0.025, 0.08);
  float span = clamp((r - root)/(1.0 - root), 0.0, 1.0);
  // A narrow shank broadens smoothly, then tapers to a rounded tip. Width is measured in metres/radius,
  // not a fixed angle: the old triangular wedges grew wider all the way to their squared-off ends.
  float chord = mix(0.030, 0.082, smoothstep(0.0, 0.34, span))*(1.0 - 0.60*smoothstep(0.42, 1.0, span));
  float tip = clamp((span - 0.91)/0.09, 0.0, 1.0);
  chord *= sqrt(max(1.0 - tip*tip, 0.0));
  float duty = clamp(chord/max(r, 0.025)*blades/3.14159265359, 0.0, 0.8);
  // Angular box integration combines shutter blur with a conservative pixel footprint. Capping at one
  // complete pitch is exact for unresolved rotation and avoids derivative-induced moire on small props.
  float coverage = duty, leading = 0.0;
  // Full-speed traffic and player props take the phase-independent path: no atan or pulse integrals.
  if (blur < 1.0 && angularAA < 1.0) {
    float sweep = (0.10 + 0.025*(blades - 2.0))*span*span;
    float azimuth = r > 0.0001 ? atan(q.y, q.x) : 0.0;
    float phase = fract((azimuth - angle - sweep)*blades/tau);
    float exposure = max(max(angularAA, blur), 0.0001);
    coverage = (propPulseIntegral(phase + exposure*0.5, duty) - propPulseIntegral(phase - exposure*0.5, duty))/exposure;
    float across = abs(fract(phase + 0.5) - 0.5)/max(duty*0.5, 0.0001);
    leading = smoothstep(0.55, 0.90, across)*(1.0 - clamp(blur + angularAA, 0.0, 1.0));
  }
  float rim = (1.0 - smoothstep(1.0 - radialAA, 1.0 + radialAA, r))*smoothstep(root - radialAA, root + radialAA, r);
  float alpha = clamp(coverage, 0.0, 1.0)*rim;
  // Subtle leading-edge wear and painted tips belong to the blades, so their blur has the same coverage.
  vec3 paint = mix(vec3(0.035, 0.041, 0.046), vec3(0.12, 0.13, 0.14), leading*0.45);
  float tipPaint = smoothstep(0.895, 0.915, r)*(1.0 - smoothstep(0.975, 0.99, r));
  paint = mix(paint, vec3(0.64, 0.48, 0.10), tipPaint*0.82);
  return vec4(paint*light, alpha);
}
