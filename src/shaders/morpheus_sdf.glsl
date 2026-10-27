//! kMorpheusSDF
//! The XR-50 Morpheus airframe distance field (engine code 7): its waverider body, glove and engine shoulders, the
//! rocket, the lift fan's duct; its rigid parts' shapes (the swing wings and their flaperons, the V-tail, the nozzles,
//! the fan and its doors).
#if HAS_MORPHEUS
// ---------------------------------------------------------------- XR-50 Morpheus (engine code 7; docs/XR50.md)
// A hypersonic waverider: a long flat-bottomed forebody whose sharp chines sweep back into a fixed glove 66 deg swept;
// out of the glove's side the swing wings (their pivots inside it: plane_parts.glsl mrPartPose); engine shoulders
// either side of the core, the main gear folded in them; a dorsal spine (the rocket's oxidizer) to the tail, the rocket's
// bell under its end between the two vectoring nozzles; an all-moving V-tail on the shoulders' ends; ventral inlets for
// the combined-cycle engines; the lift fan's duct straight through the body behind the cockpit, closed by its doors; a
// gold-filmed sensor canopy over its camera bay. No one is aboard: its "cockpit" is the ground station it is flown from
// (mapMorpheusStation), drawn round the eye where a cockpit would be.
// Materials: 150 the skin (its thermal coating above, its black tiles below: plane_material.glsl), 151 the carbon-carbon
// leading edges, 152 the sensor canopy, 153 the nozzles' titanium, 154 the amber light strips, 155 the inlets' and the
// duct's insides, 156 the rocket's bell, 157 the engines' hot insides, 158 the fan's blades, 159 the wingtips' lenses.
// The station's: 160-164 its monitors' glass (research_cockpit_layout.glsl morpheusPane: main, left, right, chin,
// telemetry), 165 the walls and ceiling, 166 the desk, 167 the monitors' and the rack's housings, 168 the chair, 169 its
// metal (stands, base, controls), 170 the ceiling's light panels, 171 the accent strips, 172 the floor, 173 the wall's
// sign and uplink lamp, 174 the rack's lamps, 175 the keyboard.

vec2 mrPartField(int k, vec3 l);
// a part in the whole airframe's field, at its pose (a flaperon's is affine: its hinge's sweep shears it)
vec2 mrPartAt(vec2 res, int k, vec2 sd, vec3 p){
  Pose X = mrPartPose(k, sd);
  return opU(res, mrPartField(k, (k == PT_MR_FLAPERON ? inverse(X.R) : transpose(X.R))*(p - X.T)));
}
// the glove: a thick 66 deg delta from the chines to the swing wings' pivots (sdPanel: its span from the centreline,
// its chord from the root's leading edge) - and how far a point is behind its leading edge
const float MR_GLOVE_SPAN = 3.05, MR_GLOVE_LE0 = -8.4, MR_GLOVE_RC = 13.6, MR_GLOVE_TC = 6.0, MR_GLOVE_SWEEP = 6.6, MR_GLOVE_Y = -0.07;
float mrGlove(vec3 p){ return sdPanel(abs(p.x), p.z - MR_GLOVE_LE0, p.y - MR_GLOVE_Y, MR_GLOVE_SPAN, MR_GLOVE_RC, MR_GLOVE_TC, MR_GLOVE_SWEEP, 0.042, 1.0, 0.0, 0.0); }
float mrGloveLE(vec3 p){ return p.z - (MR_GLOVE_LE0 + MR_GLOVE_SWEEP*clamp(abs(p.x)/MR_GLOVE_SPAN, 0.0, 1.0)); }
// the forebody's chines: a blade from the nose tip widening to meet the glove's leading edge, along the section's
// lower third (the waverider's compression surface is the flat beneath)
float mrChine(vec3 p){
  float z = clamp(p.z, -11.78, -6.6);
  vec3 sec = fusSection(z);
  float w = 0.13*(z + 11.78), y0 = sec.z - 0.30*sec.y;
  vec2 d = vec2((abs(p.x) - w)*0.992, abs(p.y - y0) - 0.04);
  float e = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - 0.012;
  return max(e, max(p.z - -6.4, -11.82 - p.z));
}
// the canopy: an elliptical section pitched along the forebody's own top line (fusSection's crest), so it leaves the
// nose's line tangentially, stands 0.26 m over it above the pilot - a 20 deg windscreen - and runs back over the lift
// fan's door into the spine's flat top at under 5 deg. Its rise is a smoothstep in and out (no slope at either end);
// where it has none it is sunk 0.1 m under the skin, so the blend leaves no ridge along the top
float mrCanopyRise(float z){ return z <= -7.3 ? smoothstep(-9.8, -7.3, z) : 1.0 - smoothstep(-7.3, -3.2, z); }
float mrCanopy(vec3 p){
  if (p.z < -10.0 || p.z > -3.0) return 1.0;   // (past its ends: buried, nothing to add)
  vec3 sec = fusSection(p.z);
  float rise = mrCanopyRise(p.z), depth = 0.24;
  vec2 ab = vec2(mix(0.22, 0.50, rise), depth + 0.26*rise - 0.10*(1.0 - rise));
  vec2 q = vec2(p.x, p.y - (sec.z + sec.y - depth));
  return (length(q/ab) - 1.0)*min(ab.x, ab.y)*0.85;   // (0.85: the section's centre climbs the nose and its size changes along it)
}
// the outer skin with nothing cut into it: the doors that close its openings follow it exactly (rule 4: AIRCRAFT_AUTHORING.md)
float mrShell(vec3 p){
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float b = sdFuselage(p);
  b = smin(b, mrChine(p), 0.12);
  b = smin(b, mrGlove(p), 0.28);
  b = smin(b, sdEllipsoid(ap - vec3(1.35, -0.10, 2.4), vec3(0.85, 0.42, 5.2)), 0.30);    // the engine shoulders (mrShoulderTop)
  b = smin(b, sdCapsule(ap, vec3(0.78, -0.04, 0.6), vec3(0.78, -0.04, 7.05), 0.53), 0.22);   // the engines' bays
  b = smin(b, sdEllipsoid(p - vec3(0.0, 0.55, 3.0), vec3(0.42, 0.32, 6.25)), 0.25);     // the dorsal spine: the rocket's oxidizer
  b = smin(b, sdEllipsoid(ap - vec3(1.0, 0.38, 7.45), vec3(0.24, 0.24, 1.45)), 0.14);   // the V-tail's root fairings
  b = smin(b, sdRoundBox(p - vec3(0.0, -0.64, -1.0), vec3(1.02, 0.16, 2.9), 0.13), 0.32);   // the ventral inlet's cowl (both engines')
  b = smin(b, mrCanopy(p), 0.16);   // the sensor canopy
  return b;
}
// ---- the ground station (eye at the origin: x right, y up, -z ahead): a sealed operations room 2.7 m across, the
// operator seated at a desk with the monitor bank before them - the main monitor ahead, one either side angled in, the
// chin monitor in the desk's console and the telemetry strip above - a side stick on the desk to the right, the
// throttle to the left, pedals under it; the chair, a rack of the uplink's equipment beside it, light panels overhead
#if HAS_CABIN
vec2 mapMorpheusStation(vec3 p){
  vec3 q = p - gM[22].xyz;
  // the room: its walls, ceiling and floor (everything outside it solid)
  vec2 res = vec2(-sdRoundBox(q - vec3(0.0, -0.12, 0.20), vec3(1.35, 1.10, 1.25), 0.10), q.y < -1.15 ? 172.0 : 165.0);
  // the desk: its top (0.82 m off the floor), side panels and modesty panel; an accent strip under its front edge
  float desk = sdRoundBox(q - vec3(0.0, -0.425, -0.62), vec3(1.10, 0.025, 0.32), 0.012);
  desk = min(desk, sdRoundBox(vec3(abs(q.x) - 1.08, q.y + 0.83, q.z + 0.62), vec3(0.02, 0.39, 0.30), 0.008));
  desk = min(desk, sdRoundBox(q - vec3(0.0, -0.80, -0.92), vec3(1.06, 0.36, 0.015), 0.006));
  res = opU(res, vec2(desk, 166.0));
  res = opU(res, vec2(sdCapsule(q, vec3(-1.04, -0.455, -0.312), vec3(1.04, -0.455, -0.312), 0.006), 171.0));
  // the monitor bank: each a housing 45 mm deep with a 22 mm bezel standing 15 mm proud of its glass (the window's
  // frame: cabin_windows.glsl kWinGlass, kWinFrameTop), on a neck down to the desk; the chin monitor in a sloped
  // console on the desk, the telemetry strip on an arm from the wall
  for (int i = 0; i < MS_PANES; i++) {
    ResearchPanel P = morpheusPane(i);
    vec3 l = researchPanelFrame(q, P);
    float sh = researchPanelShape(l.xy, P);
    float body = sdRoundBox(l - vec3(0.0, 0.0, i == 3 ? -0.05 : -0.013), vec3(P.h + vec2(0.022), i == 3 ? 0.069 : 0.032), 0.008);
    body = max(body, -max(sh, 0.004 - l.z));   // (the opening down to the glass)
    res = opU(res, vec2(body, sh < 0.0 && l.z < 0.010 ? 160.0 + float(i) : 167.0));
    vec3 back = P.c - P.n*0.05;
    if (i <= 2) {
      float neck = sdCapsule(q, back, vec3(back.x, -0.40, back.z), 0.022);
      neck = min(neck, sdRoundBox(q - vec3(back.x, -0.393, back.z), vec3(0.12, 0.007, 0.09), 0.005));
      res = opU(res, vec2(neck, 169.0));
    } else if (i == 4) res = opU(res, vec2(sdCapsule(q, back, vec3(back.x, back.y + 0.03, -1.04), 0.018), 169.0));
  }
  // the keyboard; the stick's base and the throttle's quadrant (a slot its grip rides in), the stick and the grip
  // themselves rigid parts (plane_parts.glsl: the XR-50's pose them here); the pedals under the desk on their bar
  res = opU(res, vec2(sdRoundBox(q - vec3(0.0, -0.392, -0.355), vec3(0.19, 0.008, 0.055), 0.004), q.y > -0.387 ? 175.0 : 167.0));
  res = opU(res, vec2(sdRoundBox(q - vec3(0.42, -0.392, -0.40), vec3(0.055, 0.012, 0.075), 0.01), 167.0));
  {
    float quad = sdRoundBox(q - vec3(-0.42, -0.385, -0.40), vec3(0.055, 0.02, 0.11), 0.01);
    quad = max(quad, -sdBox(q - vec3(-0.42, -0.365, -0.40), vec3(0.012, 0.02, 0.085)));
    res = opU(res, vec2(quad, 167.0));
  }
  res = partAt(res, PT_JET_STICK, vec2(0.0), p);
  res = partAt(res, PT_JET_THR, vec2(0.0), p);
  res = partAt(res, PT_WR_PEDAL, vec2(-1.0, 0.0), p); res = partAt(res, PT_WR_PEDAL, vec2(1.0, 0.0), p);
  res = opU(res, vec2(sdCapsule(q, vec3(-0.26, -1.195, -0.56), vec3(0.26, -1.195, -0.56), 0.02), 169.0));
  // the chair: its seat, its backrest and headrest (reclined 7 deg), the arms on their posts; the gas lift and the
  // five-spoked base on its casters
  {
    float ch = sdRoundBox(q - vec3(0.0, -0.80, 0.12), vec3(0.25, 0.045, 0.24), 0.04);
    vec3 bq = q - vec3(0.0, -0.42, 0.36); bq.yz = rot2(bq.yz, -0.12);
    ch = min(ch, sdRoundBox(bq, vec3(0.235, 0.36, 0.045), 0.045));
    vec3 hq = q - vec3(0.0, 0.0, 0.32); hq.yz = rot2(hq.yz, -0.12);
    ch = min(ch, sdRoundBox(hq, vec3(0.13, 0.075, 0.035), 0.03));
    ch = min(ch, sdRoundBox(vec3(abs(q.x) - 0.31, q.y + 0.55, q.z - 0.14), vec3(0.035, 0.022, 0.16), 0.02));
    res = opU(res, vec2(ch, 168.0));
    float mt = sdCapsule(vec3(abs(q.x), q.yz), vec3(0.31, -0.57, 0.20), vec3(0.26, -0.78, 0.22), 0.016);
    mt = min(mt, sdCapsule(vec3(abs(q.x), q.yz), vec3(0.06, -0.10, 0.36), vec3(0.06, -0.06, 0.37), 0.012));   // (the headrest's posts)
    mt = min(mt, sdCapsule(q, vec3(0.0, -0.85, 0.14), vec3(0.0, -1.10, 0.14), 0.028));
    for (int k = 0; k < 5; k++) {
      float a = float(k)*1.2566371;
      vec3 tip = vec3(0.30*sin(a), -1.16, 0.14 + 0.30*cos(a));
      mt = min(mt, sdCapsule(q, vec3(0.0, -1.12, 0.14), tip, 0.02));
      mt = min(mt, length(q - tip - vec3(0.0, -0.03, 0.0)) - 0.03);
    }
    res = opU(res, vec2(mt, 169.0));
  }
  // the room's fittings: the two light panels in the ceiling, the accent strip along the front wall's top, the sign
  // and the uplink's lamp over the monitors, the equipment rack beside the chair (its lamps on its face)
  res = opU(res, vec2(sdRoundBox(vec3(abs(q.x) - 0.55, q.y - 0.965, q.z - 0.15), vec3(0.16, 0.015, 0.45), 0.008), 170.0));
  res = opU(res, vec2(sdCapsule(q, vec3(-1.25, 0.925, -1.02), vec3(1.25, 0.925, -1.02), 0.008), 171.0));
  res = opU(res, vec2(sdRoundBox(q - vec3(0.0, 0.72, -1.04), vec3(0.50, 0.06, 0.012), 0.006), 173.0));
  {
    float rack = sdRoundBox(q - vec3(-1.17, -0.62, 0.55), vec3(0.17, 0.60, 0.28), 0.01);
    res = opU(res, vec2(rack, q.x > -1.004 && abs(q.z - 0.55) < 0.25 && abs(q.y + 0.62) < 0.56 ? 174.0 : 167.0));
  }
  return res;
}
#endif
vec2 mapMorpheus(vec3 p){
#if HAS_CABIN
  if (gPS.w > 0.5) return mapMorpheusStation(p);   // (no one aboard: the ground station it is flown from)
#endif
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float shell = mrShell(p);
  vec2 res = vec2(shell, 150.0);
  // the leading edges: carbon-carbon along the glove's and the chines' edges and on the nose (they glow when hot)
  if (shell < 0.05) {
    float le = mrGloveLE(p);
    bool edge = (le < 0.22 && abs(p.y - MR_GLOVE_Y) < 0.16 && ap.x > 0.35) || p.z < -10.9 || (p.z < -6.5 && abs(p.x) > 0.13*(p.z + 11.78) - 0.10);
    if (edge) res.y = 151.0;
    if (p.z > -9.6 && p.z < -5.75 && mrCanopy(p) < 0.015 && sdFuselage(p) > 0.02) res.y = 152.0;   // (the gold film: the canopy proper, its fairing aft the skin's)
  }
  // the inlet: one wide dark mouth under the body, the forebody's flat its compression ramp, splitting behind it for the
  // two engines
  {
    vec3 q = p - vec3(0.0, -0.69, -3.45);
    float mouth = sdRoundBox(q, vec3(0.90, 0.10, 0.62), 0.04);
    mouth = max(mouth, -sdRoundBox(q - vec3(0.0, 0.0, 0.62), vec3(0.03, 0.2, 0.4), 0.02));   // (the splitter)
    if (mouth < res.x + 0.04) { res.x = -smin(-res.x, mouth, 0.03); if (-mouth >= res.x - 0.002) res.y = 155.0; }
  }
  // the lift fan's duct, straight through the body: its doors (parts) close it flush
  {
    float duct = max(length(p.xz - MR_FAN.xz) - MR_FAN_R, abs(p.y - 0.10) - 1.2);
    if (duct < res.x + 0.05) { res.x = -smin(-res.x, duct, 0.02); if (-duct >= res.x - 0.002) res.y = 155.0; }
  }
  // the engines' exits: the last turbine stage and the hot cone deep in each bay's end, where the nozzle swivels on
  {
    vec3 q = ap - vec3(0.78, -0.04, 7.4);
    float bore = max(length(q.xy) - 0.40, -q.z - 0.6);
    if (bore < res.x + 0.05) {
      res.x = max(res.x, -bore);
      float cone = sdRoundCone(q, vec3(0.0, 0.0, -0.55), vec3(0.0, 0.0, -0.05), 0.24, 0.05);
      res = opU(res, vec2(max(cone, length(q.xy) - 0.38), 157.0));
      res = opU(res, vec2(max(abs(q.z + 0.58) - 0.02, length(q.xy) - 0.41), 157.0));   // (the turbine's face)
    }
  }
  // the roll posts: a nozzle under each glove, flush (its doors open with the fan: the shader's glow)
  {
    vec3 q = ap - vec3(2.65, -0.17, 1.4);
    float post = sdRoundBox(q, vec3(0.14, 0.08, 0.20), 0.03);
    if (post < res.x + 0.03) { res.x = max(res.x, -post); if (-post >= res.x - 0.002) res.y = 157.0; }
  }
  // the rocket: its bell between the nozzles under the spine's end (a niobium cone, open at the back)
  {
    vec3 q = p - vec3(0.0, 0.28, 8.95);
    if (dot(q, q) < 1.6) {
      float outer = sdRoundCone(q, vec3(0.0, 0.0, -0.9), vec3(0.0, 0.0, 0.0), 0.15, 0.32);
      float inner = sdRoundCone(q, vec3(0.0, 0.0, -0.88), vec3(0.0, 0.0, 0.02), 0.12, 0.29);
      float bell = max(max(outer, -inner), q.z);
      res = opU(res, vec2(bell, 156.0));
      res.x = smin(res.x, sdCapsule(q, vec3(0.0, 0.0, -1.6), vec3(0.0, 0.0, -0.75), 0.17), 0.08);   // (its chamber, faired into the spine)
    }
  }
  // the amber light strips along the chines, just above their edges (the glove's edge glows in its paint)
  {
    float z = clamp(p.z, -11.0, -6.6);
    vec3 sec = fusSection(z);
    float strip = sdCapsule(ap, vec3(0.13*(-11.0 + 11.78) - 0.06, sec.z - 0.30*sec.y + 0.045, -11.0), vec3(0.13*(-6.6 + 11.78) - 0.06, fusSection(-6.6).z - 0.30*fusSection(-6.6).y + 0.045, -6.6), 0.016);
    res = opU(res, vec2(strip, 154.0));
  }
  // the moving parts where they are: each at its pose (left out of the airframe's own bake: gPartMode -2)
  if (gPartMode == -1) {
    for (int i = 0; i < 2; i++) {
      vec2 sd = vec2(i == 0 ? -1.0 : 1.0, 0.0);
      res = mrPartAt(res, PT_MR_WING, sd, p); res = mrPartAt(res, PT_MR_FLAPERON, sd, p); res = mrPartAt(res, PT_MR_TAIL, sd, p);
      res = mrPartAt(res, PT_MR_NOZZLE, sd, p); res = mrPartAt(res, PT_MR_FANBOT, sd, p);
    }
    res = mrPartAt(res, PT_MR_FANDOOR, vec2(0.0), p); res = mrPartAt(res, PT_MR_FAN, vec2(0.0), p);
  }
  return res;
}
// ---- the parts, each in its own frame (plane_parts.glsl mrPartPose)
// the wing panel: from its pivot, its leading edge 0.8 m ahead of the pivot line all along it, its chord 2.4 m at the
// root and 1 m at the tip, 5% thick; the flaperon cut out of its trailing quarter from 1.2 m to 5.4 m
const float MR_WING_SPAN = 5.8, MR_WING_RC = 2.4, MR_WING_TC = 1.0, MR_WING_LE = -0.8;
vec2 mrWingField(vec3 l){
  float c = l.z - MR_WING_LE;
  float w = sdPanel(l.x, c, l.y, MR_WING_SPAN, MR_WING_RC, MR_WING_TC, 0.0, 0.05, 0.75, 1.2, 5.4);
  vec2 r = vec2(w, c < 0.1 ? 151.0 : 150.0);
  // the tip's lens: red or green by the side the panel is on (plane_material.glsl)
  r = opU(r, vec2(sdCapsule(l, vec3(MR_WING_SPAN - 0.02, 0.0, MR_WING_LE + 0.55), vec3(MR_WING_SPAN - 0.02, 0.0, MR_WING_LE + 0.75), 0.03), 159.0));
  return r;
}
vec2 mrPartField(int k, vec3 l){
  if (k == PT_MR_WING) return mrWingField(l);
  if (k == PT_MR_FLAPERON) return vec2(sdSurface(l.x, l.z - MR_WING_LE, l.y, MR_WING_SPAN, MR_WING_RC, MR_WING_TC, 0.0, 0.05, 0.75, 1.2, 5.4, 0.0, 0.0), 150.0);
  if (k == PT_MR_TAIL) {   // its span from the pivot, 39% of the root's chord back from its leading edge: 2.9 m, 2.7 to 0.85 m of chord
    float c = l.z + 1.05;
    return vec2(sdPanel(l.x, c, l.y, 2.9, 2.7, 0.85, 1.9, 0.045, 1.0, 0.0, 0.0), c - 1.9*clamp(l.x/2.9, 0.0, 1.0) < 0.1 ? 151.0 : 150.0);
  }
  if (k == PT_MR_NOZZLE) {   // the swivelling nozzle: a convergent-divergent tube of overlapping petals, its liner soot-dark
    float r = length(l.xy), z = l.z;
    float outer = r - mix(0.50, 0.44, clamp(z/1.15, 0.0, 1.0));
    float a = atan(l.y, l.x);
    outer += 0.006*smoothstep(0.35, 1.0, z)*abs(fract(a*14.0/6.2831853) - 0.5);   // (the petals' overlaps)
    float inner = 0.41 - r + 0.03*exp(-pow((z - 0.55)/0.25, 2.0));                 // (the throat)
    float tube = max(max(outer, inner), max(-z, z - 1.15));
    vec2 res = vec2(tube, inner > outer - 0.01 ? 157.0 : 153.0);
    res = opU(res, vec2(max(abs(z - 0.08) - 0.07, abs(r - 0.49) - 0.035), 153.0));   // (the swivel's bearing ring)
    return res;
  }
  if (k == PT_MR_FANDOOR) {   // the skin over the opening, hinged at its aft edge: an even layer just inside the skin
    vec3 b = l + vec3(0.0, MR_FAN_TOP, MR_FAN.z + 0.68);
    float band = abs(mrShell(b) + 0.03) - 0.03;
    return vec2(max(max(band, length(b.xz - MR_FAN.xz) - (MR_FAN_R + 0.05)), abs(b.y - MR_FAN_TOP - 0.06) - 0.31), 150.0);   // (with the canopy's fairing over it)
  }
  if (k == PT_MR_FAN) {   // the fan: its hub and 18 twisted blades, turning about its axis (y)
    float rr = length(l.xz), a = atan(l.z, l.x), sec = 6.2831853/18.0;
    a -= sec*floor(a/sec + 0.5);
    vec2 bq = rr*vec2(cos(a), sin(a));
    vec3 q = vec3(bq.x - 0.39, l.y, bq.y); q.yz = rot2(q.yz, 0.5 - 0.5*q.x);
    float blade = sdRoundBox(q, vec3(0.20, 0.008, 0.07), 0.006);
    float hub = sdRoundCylX(l.yxz, 0.19, 0.13, 0.04);
    return opU(vec2(max(blade, rr - 0.6), 158.0), vec2(hub, 153.0));
  }
  if (k == PT_MR_FANBOT) {   // a bottom door (the right one: hinged at its outboard edge), the skin's layer below the duct
    vec3 b = l + vec3(MR_FAN_R + 0.02, MR_FAN_BOT, MR_FAN.z);
    float band = abs(mrShell(b) + 0.03) - 0.03;
    float hf = max(length(b.xz - MR_FAN.xz) - (MR_FAN_R + 0.04), -b.x + 0.004);
    return vec2(max(max(band, hf), abs(b.y - MR_FAN_BOT) - 0.25), 150.0);
  }
  return vec2(1e9, 0.0);
}
#endif
