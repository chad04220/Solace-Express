//! kEnemyArchon
vec2 ccBoss(vec3 p, vec4 state) {
  vec2 h = vec2(1e5, 110.0);
  vec3 a = vec3(abs(p.x), p.y, p.z);

  vec2 keel[8] = vec2[8](
    vec2(-3.74, -8.50), vec2(3.74, -8.50),
    vec2(6.08, -2.00), vec2(5.54, 9.32),
    vec2(2.36, 18.20), vec2(-2.36, 18.20),
    vec2(-5.54, 9.32), vec2(-6.08, -2.00));
  float body = ccPlate(p - vec3(0.0, -0.22, 0.0), keel, 8, 1.30, 0.25);
  ccAdd(h, body, 110.0);

  // The long bow arms are solid swept hulls. Their aft roots overlap both keel
  // and shoulders; the open fork is exterior space, not a hole into a cabin.
  vec2 fork[8] = vec2[8](
    vec2(2.58, 2.00), vec2(3.60, -9.00),
    vec2(4.10, -16.70), vec2(5.37, -18.63),
    vec2(6.88, -17.13), vec2(8.02, -8.00),
    vec2(8.57, -1.02), vec2(6.40, 3.20));
  float bow = ccPlate(a - vec3(0.0, -0.06, 0.0), fork, 8, 1.27, 0.24);
  ccAdd(h, bow, 110.0);

  vec2 shoulder[8] = vec2[8](
    vec2(0.0, -5.50), vec2(6.50, -6.91),
    vec2(12.30, -4.49), vec2(16.71, -0.20),
    vec2(16.42, 4.00), vec2(10.62, 8.00),
    vec2(5.72, 11.70), vec2(0.0, 8.00));
  float wing = ccPlate(a - vec3(0.0, -0.32, 0.0), shoulder, 8, 0.91, 0.23);
  ccAdd(h, wing, 110.0);

  vec2 armor[8] = vec2[8](
    vec2(5.02, -5.55), vec2(6.62, -6.19),
    vec2(11.87, -3.92), vec2(15.93, 0.02),
    vec2(15.62, 3.58), vec2(10.18, 7.23),
    vec2(5.85, 10.38), vec2(4.90, 7.14));
  ccAdd(h, ccPlate(a - vec3(0.0, 0.63, 0.0), armor, 8, 0.24, 0.14), 112.0);
  float shoulders = ccPlate(a - vec3(0.0, 0.82, 0.0), armor, 8, 0.22, 0.065);
  ccAdd(h, shoulders, 111.0);
  for (int k = 0; k < 4; ++k) {
    float z = -2.66 + float(k)*1.96;
    ccHeavySkin(h, a, shoulders, vec3(11.10, 1.04, z),
                vec3(3.84, 0.23, 0.072), 0.021, 113.0);
    ccHeavySkin(h, a, shoulders, vec3(7.50, 1.08, z + 0.26),
                vec3(0.40, 0.21, 0.055), 0.024, 112.0);
  }

  // A broad command wedge is sculpted in all three axes. Its sloping foreface
  // and tapered flanks replace the support's thin split-vane architecture.
  vec2 deckPlan[8] = vec2[8](
    vec2(-1.76, -8.00), vec2(1.76, -8.00),
    vec2(4.49, -2.52), vec2(3.66, 8.78),
    vec2(1.90, 12.18), vec2(-1.90, 12.18),
    vec2(-3.66, 8.78), vec2(-4.49, -2.52));
  float deck = ccPlate(p - vec3(0.0, 1.17, 0.0), deckPlan, 8, 0.62, 0.19);
  ccAdd(h, deck, 111.0);
  ccHeavySkin(h, a, deck, vec3(3.30, 1.85, 3.04),
              vec3(0.32, 0.26, 5.72), 0.020, 112.0);

  vec2 keepPlan[8] = vec2[8](
    vec2(-0.98, -8.35), vec2(0.98, -8.35),
    vec2(3.34, -2.24), vec2(3.06, 6.05),
    vec2(1.39, 10.75), vec2(-1.39, 10.75),
    vec2(-3.06, 6.05), vec2(-3.34, -2.24));
  vec2 keepProfile[8] = vec2[8](
    vec2(-8.80, 0.83), vec2(-5.80, 2.37),
    vec2(-0.80, 4.25), vec2(4.20, 4.25),
    vec2(8.90, 2.45), vec2(10.80, 0.83),
    vec2(10.80, 0.83), vec2(10.80, 0.83));
  float profile = ccPlate(vec3(p.z, p.x, p.y), keepProfile, 6, 3.70, 0.20);
  float keep = max(profile, ccPlate(p - vec3(0.0, 2.0, 0.0), keepPlan, 8, 3.60, 0.15));
  ccAdd(h, keep, 111.0);
  ccHeavySkin(h, a, keep, vec3(2.91, 2.40, 0.97),
              vec3(0.80, 0.20, 4.28), 0.028, 112.0);
  ccHeavySkin(h, p, keep, vec3(0.0, 2.64, -5.19),
              vec3(2.20, 0.105, 2.00), 0.035, 113.0);
  ccHeavySkin(h, p, keep, vec3(0.0, 2.67, -5.24),
              vec3(1.76, 0.038, 2.00), 0.055, 114.0);
  // A tapered dorsal command-array recess follows the wedge, not a cabin roof.
  vec2 commandArray[8] = vec2[8](
    vec2(-0.18, -2.10), vec2(0.18, -2.10),
    vec2(0.84, 0.28), vec2(0.49, 3.00),
    vec2(0.0, 4.10), vec2(-0.49, 3.00),
    vec2(-0.84, 0.28), vec2(-0.18, -2.10));
  float array = ccPlate(p - vec3(0.0, 4.26, 0.0), commandArray, 7, 0.21, 0.04);
  ccAdd(h, max(keep - 0.025, array), 112.0);
  float innerArray = ccPlate(p - vec3(0.0, 4.44, 0.0), commandArray, 7, 0.23, 0.01);
  ccAdd(h, max(keep - 0.044, innerArray + 0.075), 117.0);

  // Swept aft crown rails anchor deep into the central hull. Their diagonal
  // power conduits and pointed profile distinguish the command ship at range.
  vec2 crown[8] = vec2[8](
    vec2(-0.74, 0.75), vec2(3.01, 2.02),
    vec2(8.03, 6.55), vec2(9.10, 6.13),
    vec2(10.55, 1.40), vec2(9.10, 0.75),
    vec2(9.10, 0.75), vec2(9.10, 0.75));
  float command = ccPlate(vec3(p.z, a.x - 3.70, p.y), crown, 6, 0.64, 0.11);
  ccAdd(h, command, 110.0);
  vec2 railFrame[8] = vec2[8](
    vec2(3.32, 1.94), vec2(7.94, 6.03),
    vec2(8.61, 5.72), vec2(5.00, 2.21),
    vec2(5.00, 2.21), vec2(5.00, 2.21),
    vec2(5.00, 2.21), vec2(5.00, 2.21));
  float conduit = ccPlate(vec3(p.z, a.x - 4.34, p.y), railFrame, 4, 0.12, 0.018);
  ccAdd(h, max(command - 0.027, conduit), 112.0);
  vec2 railInset[8] = vec2[8](
    vec2(4.11, 2.62), vec2(7.94, 5.86),
    vec2(8.17, 5.64), vec2(4.74, 2.66),
    vec2(4.74, 2.66), vec2(4.74, 2.66),
    vec2(4.74, 2.66), vec2(4.74, 2.66));
  float innerConduit = ccPlate(vec3(p.z, a.x - 4.37, p.y), railInset, 4, 0.12, 0.014);
  ccAdd(h, max(command - 0.045, innerConduit), 117.0);

  // Functional black flank radiators are shallow insets in the shoulder shell.
  ccHeavySkin(h, a, wing, vec3(16.35, -0.13, 1.97),
              vec3(0.90, 0.40, 1.25), 0.022, 113.0);
  for (int k = 0; k < 4; ++k) {
    ccHeavySkin(h, a, wing, vec3(16.37, -0.13, 1.08 + float(k)*0.56),
                vec3(0.91, 0.35, 0.06), 0.039, 112.0);
  }

  // Structural bow armor remains thick where it joins the citadel's shoulders.
  vec2 bowArmor[8] = vec2[8](
    vec2(3.33, 0.76), vec2(4.26, -9.00),
    vec2(4.60, -16.10), vec2(5.42, -17.45),
    vec2(6.30, -16.35), vec2(7.32, -7.77),
    vec2(7.82, -1.33), vec2(6.03, 2.20));
  ccAdd(h, ccPlate(a - vec3(0.0, 1.15, 0.0), bowArmor, 8, 0.27, 0.13), 112.0);
  float outriggers = ccPlate(a - vec3(0.0, 1.36, 0.0), bowArmor, 8, 0.23, 0.055);
  ccAdd(h, outriggers, 111.0);
  for (int k = 0; k < 4; ++k) {
    float z = -13.30 + float(k)*2.65;
    ccHeavySkin(h, a, outriggers, vec3(5.52, 1.60, z),
                vec3(1.68, 0.22, 0.080), 0.020, 113.0);
  }

  // Two large shoulder nacelles plus a smaller pair under the fork. All four
  // upper surfaces intersect load-bearing hull; nothing serves as landing gear.
  vec2 shoulderPod = ccPod(a, vec3(12.12, -1.62, 1.40), 1.95, 5.18);
  ccAdd(h, shoulderPod.x, shoulderPod.y);
  vec2 bowPod = ccPod(a, vec3(5.80, -1.45, -9.30), 1.28, 3.45);
  ccAdd(h, bowPod.x, bowPod.y);

  // Central siege-core aperture between the mandibles. The shallow pocket is
  // cut only into its copper housing, never through the structural keel.
  // A solid amber lens positively overlaps the intact keel by 6 cm, and sits
  // 12 cm behind the collar lip. This is a sealed energy port, not an open duct.
  vec3 coreQ = p - vec3(0.0, 0.08, -8.62);
  vec2 coreOutline[8] = vec2[8](
    vec2(-0.414, -1.0), vec2(0.414, -1.0),
    vec2(1.0, -0.414), vec2(1.0, 0.414),
    vec2(0.414, 1.0), vec2(-0.414, 1.0),
    vec2(-1.0, 0.414), vec2(-1.0, -0.414));
  vec3 coreAxial = vec3(coreQ.x, coreQ.z, coreQ.y);
  float collar = ccPlate(coreAxial, coreOutline, 8, 0.36, 0.04);
  float pocket = ccCylinderY(coreAxial - vec3(0.0, -0.31, 0.0), 0.94, 0.23);
  ccAdd(h, max(collar, -pocket), 112.0);
  vec3 rimQ = coreAxial - vec3(0.0, -0.15, 0.0);
  float rim = max(ccCylinderY(rimQ, 0.932, 0.11),
                 -ccCylinderY(rimQ, 0.855, 0.14));
  ccAdd(h, rim, 113.0);
  ccAdd(h, ccCylinderY(coreAxial - vec3(0.0, -0.155, 0.0), 0.85, 0.085), 116.0);

  // Recess-like sensor/emitter glazing is a closed solid skin over the bow.
  ccHeavySkin(h, a, bow, vec3(5.38, 0.17, -18.57),
              vec3(0.94, 0.38, 0.72), 0.040, 112.0);
  ccHeavySkin(h, a, bow, vec3(5.38, 0.17, -18.61),
              vec3(0.71, 0.15, 0.77), 0.065, 116.0);
  ccHeavySkin(h, a, deck, vec3(0.97, 1.42, -7.94),
              vec3(0.75, 0.12, 0.40), 0.042, 114.0);
  ccHeavySkin(h, p, keep, vec3(0.0, 3.04, -4.27),
              vec3(0.81, 0.20, 0.70), 0.033, 114.0);
  for (int k = 0; k < 5; ++k) {
    float z = 10.60 + float(k)*1.26;
    ccHeavySkin(h, a, body, vec3(3.82, 0.21, z),
                vec3(1.80, 0.50, 0.09), 0.025, 112.0);
  }
  return h;
}
