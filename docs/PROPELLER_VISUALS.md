# Propeller visual review

## Scope

The player aircraft and bounded AI/airline-traffic disc pass now use the same production GLSL profile (`src/shaders/propeller.glsl`). It provides curved, tapered blades, rounded tips, subtle edge wear and narrow painted tips. Roots underlap each model's existing solid spinner. The model-authored propeller radius, attachment position and 2/3/4-blade count are unchanged.

All eight propeller aircraft are covered: Kestrel, Wren, Bushmaster, Islander, Pelican, Meridian, Swift and Osprey. Together they have 11 propeller hubs. Jets are excluded. Airline-route craft use `Traffic::fillVisuals` and the same `Renderer::rasterTrafficProps` path as ordinary traffic.

## Rotation and cost

- Angular coverage is integrated analytically over the pixel footprint and rotational exposure. Partial blur becomes translucent arcs while preserving integrated blade coverage.
- At full blur, coverage is independent of angle. A coherent fast path skips angle evaluation and pulse integration, eliminating spinning-spoke aliasing at high speed.
- The player starts blending at 45 RPM and reaches full blur at 650 RPM. The existing simulated RPM and rotation update are unchanged.
- Traffic retains its existing throttle-based blur proxy because its packed visual state has no RPM/engine-running field. Its idle, taxi and running states all use the new profile.
- No new blade geometry, texture fetches or temporal sampling loop was added. The AI pass retains conservative per-disc screen rectangles and scene-depth rejection. This is a low-cost design; it is not a hardware 60 FPS benchmark result.
- Existing `FLEET_ON`, `JET_ON` and `WRAITH_ON` specialization is preserved, and player prop effects are explicitly gated by `FLEET_ON`. The shared source participates in the shader-cache fingerprint.

## Verification

`prop_disc_test` passes 100,504 checks: all eight prop aircraft, all 11 model hubs, matching spinner radius and blade counts, camera-relative transforms, per-traffic state isolation, and bounded perspective/panoramic projection.

`propeller_visual_test` compiles and runs the actual shared production GLSL plus the production traffic fragment shader using a small surfaceless EGL context. It does not compile the whole scene. It passes 1,496,226 checks covering:

- Correct stopped 2/3/4-blade silhouettes and tapered chord
- Finite, bounded alpha and no blade coverage beyond the model radius
- Stopped / partial / full rotational blur
- Integrated coverage conserved within 0.013% for the tested close-up views
- Exactly phase-independent full-speed coverage
- Player-profile / AI-traffic shader parity in every state
- Coverage stability at small projected sizes, including roughly 21-pixel discs

Run with `propeller_visual_test OUTPUT_DIR` to also save nine PPM shader-profile images. The checkerboard contact sheet in `ui-review/propeller-profile.png` is an isolated production-shader diagnostic, not an in-game aircraft screenshot. It deliberately omits the existing solid spinner/airframe so the blade profile can be inspected.

Actual-aircraft showroom comparisons are captured separately by the UI review harness. The showroom intentionally keeps the engine stopped.

The final 1080p production-fleet diagnostic captures were visually inspected for Kestrel (two blades), Bushmaster (three blades) and Meridian (two four-blade propellers). All show improved tapered silhouettes, painted tips and roots attached to the existing spinners. These are actual production aircraft/effects draws with a test-only fleet startup adapter; they do not establish full mixed-fleet driver startup performance.
