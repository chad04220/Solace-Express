//! kEnemyCantor
vec2 ccSupport(vec3 p, vec4 state) {
  vec2 h = vec2(1e5, 110.0);
  vec3 a = vec3(abs(p.x), p.y, p.z);

  // The keel runs uninterrupted from the sealed sensor prow to the blunt stern.
  vec2 keel[8] = vec2[8](
    vec2(-0.92, -7.40), vec2(0.92, -7.40),
    vec2(2.48, -4.30), vec2(2.65, 1.90),
    vec2(1.52, 7.20), vec2(-1.52, 7.20),
    vec2(-2.65, 1.90), vec2(-2.48, -4.30));
  float body = ccPlate(p - vec3(0.0, -0.20, 0.0), keel, 8, 0.68, 0.14);
  ccAdd(h, body, 110.0);

  // Deep swept shoulders are load-bearing hull, rather than thin aircraft wings.
  vec2 shoulder[8] = vec2[8](
    vec2(0.0, -4.85), vec2(2.75, -4.18),
    vec2(6.82, -1.85), vec2(7.00, 0.40),
    vec2(5.82, 3.04), vec2(2.10, 5.45),
    vec2(0.0, 4.00), vec2(0.0, -4.85));
  float wing = ccPlate(a - vec3(0.0, -0.10, 0.0), shoulder, 7, 0.44, 0.12);
  ccAdd(h, wing, 110.0);

  // A copper gasket peeks from below inset basalt shoulder armor. Both layers
  // overlap the shoulder; their visible seam never opens the structural hull.
  vec2 armor[8] = vec2[8](
    vec2(1.14, -3.73), vec2(2.73, -3.63),
    vec2(6.42, -1.60), vec2(6.53, 0.30),
    vec2(5.44, 2.56), vec2(2.25, 4.66),
    vec2(1.14, 3.48), vec2(1.14, -3.73));
  float gasket = ccPlate(a - vec3(0.0, 0.35, 0.0), armor, 7, 0.17, 0.09);
  ccAdd(h, gasket, 112.0);
  float shoulders = ccPlate(a - vec3(0.0, 0.46, 0.0), armor, 7, 0.15, 0.035);
  ccAdd(h, shoulders, 111.0);
  ccHeavySkin(h, a, shoulders, vec3(4.34, 0.65, -1.96),
              vec3(1.61, 0.20, 0.075), 0.016, 113.0);
  ccHeavySkin(h, a, shoulders, vec3(4.40, 0.65, 1.85),
              vec3(1.37, 0.20, 0.055), 0.018, 112.0);

  // Dorsal armored saddle carries both relay vanes on wide buried roots.
  vec2 saddle[8] = vec2[8](
    vec2(-0.70, -5.56), vec2(0.70, -5.56),
    vec2(1.82, -2.94), vec2(1.88, 2.08),
    vec2(1.02, 5.80), vec2(-1.02, 5.80),
    vec2(-1.88, 2.08), vec2(-1.82, -2.94));
  float deck = ccPlate(p - vec3(0.0, 0.61, 0.0), saddle, 8, 0.36, 0.12);
  ccAdd(h, deck, 111.0);

  // Fixed, outward-canted vane frames. The split crown has swept oblique edges
  // and tapered inset arrays; there are no window rows or stacked cabin boxes.
  vec3 rq = vec3((a.x - 0.66)*0.976296 - (p.y - 0.81)*0.216440,
                 (a.x - 0.66)*0.216440 + (p.y - 0.81)*0.976296,
                 p.z - 0.35);
  vec2 crown[8] = vec2[8](
    vec2(-3.33, -0.30), vec2(-2.51, 0.61),
    vec2(-0.85, 2.48), vec2(0.20, 2.73),
    vec2(1.06, 1.79), vec2(3.44, 0.13),
    vec2(3.61, -0.30), vec2(-3.33, -0.30));
  float relay = ccPlate(vec3(rq.z, rq.x, rq.y), crown, 7, 0.22, 0.065);
  ccAdd(h, relay, 111.0);
  vec2 relayFrame[8] = vec2[8](
    vec2(-2.29, 0.32), vec2(-0.61, 2.15),
    vec2(0.12, 2.36), vec2(0.84, 1.63),
    vec2(2.40, 0.45), vec2(1.84, 0.30),
    vec2(1.84, 0.30), vec2(1.84, 0.30));
  float frame = ccPlate(vec3(rq.z, rq.x - 0.22, rq.y), relayFrame, 6, 0.065, 0.018);
  ccAdd(h, max(relay - 0.022, frame), 112.0);
  vec2 relayInset[8] = vec2[8](
    vec2(-1.86, 0.48), vec2(-0.48, 1.99),
    vec2(0.12, 2.15), vec2(0.70, 1.55),
    vec2(1.93, 0.55), vec2(1.54, 0.46),
    vec2(1.54, 0.46), vec2(1.54, 0.46));
  float array = ccPlate(vec3(rq.z, rq.x - 0.24, rq.y), relayInset, 6, 0.065, 0.015);
  ccAdd(h, max(relay - 0.041, array), 117.0);
  // Recessed diagonal conductors divide one antenna array, rather than glazing.
  for (int k = 0; k < 3; ++k) {
    vec3 stripe = vec3(rq.x, rq.y*0.819152 - rq.z*0.573576,
                       rq.y*0.573576 + rq.z*0.819152);
    float cut = ccBox(stripe, vec3(0.24, 0.35 + float(k)*0.51, 0.30),
                      vec3(0.075, 0.035, 2.95), 0.014);
    ccAdd(h, max(max(relay - 0.050, array - 0.009), cut), 113.0);
  }

  // Integral ventral levitation nacelles: upper halves are buried in shoulders.
  vec2 pod = ccPod(a, vec3(4.92, -0.86, 0.25), 1.04, 2.86);
  ccAdd(h, pod.x, pod.y);

  // Closed, segmented sensor brow. No window, transparent cockpit, or aperture.
  ccHeavySkin(h, a, body, vec3(0.45, 0.12, -7.48),
              vec3(0.54, 0.25, 0.26), 0.032, 112.0);
  ccHeavySkin(h, a, body, vec3(0.45, 0.12, -7.51),
              vec3(0.42, 0.09, 0.29), 0.052, 114.0);
  ccHeavySkin(h, p, deck, vec3(0.0, 1.04, -4.28),
              vec3(0.49, 0.19, 0.66), 0.025, 113.0);
  ccHeavySkin(h, p, deck, vec3(0.0, 1.07, -4.28),
              vec3(0.35, 0.22, 0.50), 0.047, 117.0);
  for (int k = 0; k < 3; ++k) {
    float z = 4.44 + float(k)*0.68;
    ccHeavySkin(h, a, body, vec3(1.51, 0.03, z),
                vec3(0.34, 0.25, 0.07), 0.020, 112.0);
  }
  return h;
}

// ARCHON: a forked command citadel with two connected armored forward outriggers.
// Nominal envelope: 33.9 m span, 37.4 m length, 9.4 m overall height.
// Actual lowest pod point: -2.712 m; conservative minY: -2.80 m. Radius 24 m.
