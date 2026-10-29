//! kXr60SDF
//! The XR-60 Phantasos airframe distance field (engine code 8): its faceted knife of a lifting body, the sensor
//! crystal, the keel and its scramjet duct, the ring drive's cone and struts, the lift emitters; its rigid parts' shapes
//! (the blade wings' folding tips and elevons, the fins, the ring's petals and the ring). And the operations pod it is
//! flown from.
#if HAS_PHANTASOS
// ---------------------------------------------------------------- XR-60 Phantasos (engine code 8; docs/XR60.md)
// A knife of black glass. Its section is a cut stone, not an ellipse: a sharp dorsal ridge, an upper chine, the main
// chine at its widest, a flat keel - seven facets, the nose a needle. Out of its aft flanks the blade wings grow
// forward-swept, their outer 2.2 m folding tip blades (parts) that drop for the hover; twin all-moving fins canted out
// on the aft shoulders; a deep ventral keel that is the scramjet's duct; the ring drive at the tail - the body's cone
// inside a spinning ring on three struts, six vectoring petals round it; four flush emitters under the belly; a
// faceted sensor crystal where a canopy would be. No one is aboard: its "cockpit" is the operations pod it is flown
// from (mapXr60Station), drawn round the eye where a cockpit would be.
// Materials: 176 the obsidian skin (its violet sheen, its seams lit cyan with the drive), 177 the knife edges (they
// glow with the heat), 178 the sensor crystal, 179 the ring's and the struts' metal, 180 the lit seams proper, 181
// the petals' hot faces, 182 the emitters, 183 the keel duct's inside, 184 the ring's plasma face, 185 the tip blades'
// lenses. The pod's: 186-189 its monitors' glass (research_cockpit_layout.glsl xr60Pane: main, left, right, chin),
// 190 the telemetry band, 191 the walls and ceiling, 192 the console, 193 the housings, 194 the chair, 195 its metal
// (stands, controls), 196 the ceiling's light discs, 197 the violet accent strips, 198 the floor, 199 the crest and
// the core column's lamps.
const float PX_NOSE = -9.6, PX_TAIL = 7.0;
const vec3 PX_RING = vec3(0.0, 0.0, 7.0);                 // the ring's centre (aircraft.h kXr60Ring)
const float PX_RING_R = 1.10, PX_RING_T = 0.16;           // its major and minor radii
const vec3 PX_ROOT = vec3(1.15, -0.05, 1.6);              // the blade's root: its leading edge at the flank (the right side)
const float PX_DIH = 0.13962634;                          // the blades' dihedral (8 deg)
const float PX_IN_SPAN = 2.85, PX_IN_RC = 4.6, PX_IN_TC = 2.6, PX_IN_SWEEP = -1.1;   // the fixed inner blade (forward swept)
const vec3 PX_HINGE = vec3(4.0, 0.35, 1.8);               // the tip blade's hinge (aircraft.h kXr60TipHinge)
const float PX_TIP_SPAN = 2.2, PX_TIP_RC = 2.0, PX_TIP_TC = 0.30, PX_TIP_SWEEP = -1.7, PX_TIP_LE = -1.0;   // (a narrow claw, raked forward)
const float PX_TIP_CURL = 0.42;                           // the claw's rise toward its tip (m, at the tip)
const vec3 PX_FINROOT = vec3(0.92, 0.50, 4.4); const float PX_FIN_CANT = 0.61086524;   // (35 deg off the vertical)
const vec3 PX_EMIT_F = vec3(1.25, -0.42, -2.6), PX_EMIT_R = vec3(1.45, -0.40, 2.6);     // the right emitters (aircraft.h kXr60Emit)
const float PX_KEEL_Y = -0.95, PX_KEEL_Z = 2.3;

vec2 pxPartField(int k, vec3 l);
Pose pxPartPose(int k, vec2 sd);
// a part in the whole airframe's field, at its pose (the elevon's is affine: its hinge's sweep shears it)
vec2 pxPartAt(vec2 res, int k, vec2 sd, vec3 p){
  Pose X = pxPartPose(k, sd);
  return opU(res, pxPartField(k, (k == PT_PX_ELEVON ? inverse(X.R) : transpose(X.R))*(p - X.T)));
}
// the faceted section at body z (fusSection's half width, half height and centre): the ridge, the upper chine, the
// main chine and the keel's corner in the (|x|, y) half plane, the keel flat between the corners. A convex polygon as
// the greatest of its edges' half planes (exact inside and along the facets; a bound at the corners)
float pxSection(vec3 p){
  vec3 sec = fusSection(p.z);
  float w = max(sec.x, 0.01), h = max(sec.y, 0.01);
  vec2 q = vec2(abs(p.x), p.y - sec.z);
  vec2 R = vec2(0.0, h), U = vec2(0.78*w, 0.50*h), C = vec2(w, -0.05*h), K = vec2(0.45*w, -0.72*h);
  vec2 e1 = U - R, e2 = C - U, e3 = K - C;
  float d = dot(q - R, normalize(vec2(-e1.y, e1.x)));
  d = max(d, dot(q - U, normalize(vec2(-e2.y, e2.x))));
  d = max(d, dot(q - C, normalize(vec2(-e3.y, e3.x))));
  d = max(d, -(q.y + 0.72*h));
  d = max(d, max(PX_NOSE - p.z, p.z - PX_TAIL));
  return 0.8*d - 0.015;   // (0.8: the section changes along z; the facets' corners eased 1.5 cm)
}
// the fixed inner blade, in the body (the right side; mirrored): from its root at the flank, forward swept, dihedral,
// its elevon cut out of the trailing 28% from 0.2 m to 2.7 m of span
float pxInnerBlade(vec3 p){
  vec3 q = vec3(abs(p.x), p.y, p.z) - PX_ROOT;
  q.xy = rot2(q.xy, -PX_DIH);
  return sdPanel(q.x, q.z, q.y, PX_IN_SPAN, PX_IN_RC, PX_IN_TC, PX_IN_SWEEP, 0.045, 0.72, 0.2, 2.7);
}
// the sensor crystal: an elongated octahedron set into the forebody's ridge, a cut gem lit from inside
float pxCrystal(vec3 p){
  vec3 sec = fusSection(-6.3);
  vec3 q = p - vec3(0.0, sec.z + sec.y - 0.08, -6.3);
  q.z *= 0.42;
  return 0.42*(abs(q.x) + abs(q.y) + abs(q.z) - 0.40)*0.57735;
}
// the keel: a deep blade under the body, knife-edged at its front and along its bottom, the scramjet's duct inside
// it. Its section a flat-sided diamond: a short flat flank, the upper facet into the belly, the lower to the edge
float pxKeel(vec3 p){
  float hw = 0.04 + 0.19*smoothstep(-2.4, 0.6, p.z), h = 0.44;
  vec2 q = vec2(abs(p.x), p.y - PX_KEEL_Y);
  float d = max(dot(q - vec2(hw, 0.08), normalize(vec2(h - 0.08, hw))), dot(q - vec2(hw, -0.12), normalize(vec2(h - 0.12, -hw))));
  d = max(d, q.x - hw);
  d = max(d, abs(p.z - PX_KEEL_Z) - 4.3);
  return 0.85*d - 0.02;
}
// the outer skin with nothing cut into it (rule 4: AIRCRAFT_AUTHORING.md)
float pxShell(vec3 p){
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float b = pxSection(p);
  b = smin(b, sdRoundCone(p, vec3(0.0, 0.0, 5.3), vec3(0.0, 0.0, 7.9), 0.64, 0.10), 0.25);   // the drive's cone, inside the ring
  b = smin(b, pxCrystal(p), 0.07);
  b = smin(b, pxKeel(p), 0.16);
  b = smin(b, pxInnerBlade(p), 0.20);
  b = smin(b, sdEllipsoid(ap - vec3(PX_FINROOT.x, PX_FINROOT.y - 0.10, 4.7), vec3(0.20, 0.17, 1.35)), 0.12);   // the fins' root fairings
  {   // the dorsal spine: a thin blade standing 10 cm proud of the ridge from the crystal to the fins
    vec3 sec = fusSection(p.z);
    b = smin(b, 0.85*sdBox(vec3(p.x, p.y - (sec.z + sec.y) - 0.02, p.z - 1.0), vec3(0.014, 0.10, 3.4)), 0.05);
  }
  return b;
}
// ---- the operations pod (eye at the origin: x right, y up, -z ahead): a sealed hexagonal capsule 2.5 m across, the
// operator seated before a console that wraps round them - the main monitor ahead, one either side curved in, the
// chin monitor low in the console and the telemetry band above - a side stick on the console to the right, the throttle
// to the left, pedals under it; the chair, the core column with its lamps behind to the left, light discs overhead,
// violet strips along the ceiling's edge and under the console
#if HAS_CABIN
float pxHex(vec2 q, float r){   // (a regular hexagon of apothem r, its flat walls fore and aft)
  vec2 a = abs(q);
  return max(a.y - r, dot(a, vec2(0.8660254, 0.5)) - r);
}
vec2 mapXr60Station(vec3 p){
  vec3 q = p - gM[22].xyz;
  // the capsule: its six walls, ceiling and floor (everything outside it solid)
  vec2 res = vec2(-max(pxHex(q.xz - vec2(0.0, 0.15), 1.25), abs(q.y + 0.08) - 1.12) - 0.03, q.y < -1.15 ? 198.0 : 191.0);
  float r = length(q.xz), ang = atan(q.x, -q.z);
  float wrap = (abs(ang) - 1.25)*0.6;   // (the console's and the strips' arc: 72 deg either side of ahead)
  // the console: an arc 0.78 m from the eye, 0.48 m deep, its modesty panel below; the accent strip under its edge
  float desk = max(max(abs(r - 0.78) - 0.24, abs(q.y + 0.42) - 0.03), wrap) - 0.01;
  desk = min(desk, max(max(abs(r - 1.0) - 0.015, abs(q.y + 0.80) - 0.36), wrap));
  res = opU(res, vec2(desk, 192.0));
  res = opU(res, vec2(max(max(abs(r - 1.02) - 0.008, abs(q.y + 0.455) - 0.008), wrap), 197.0));
  // the monitor bank: each a housing 45 mm deep with a 22 mm bezel standing 15 mm proud of its glass, the camera
  // monitors windows (cabin_windows.glsl); on a neck down to the console; the telemetry band on an arm from the wall
  for (int i = 0; i < MS_PANES; i++) {
    ResearchPanel P = xr60Pane(i);
    vec3 l = researchPanelFrame(q, P);
    float sh = researchPanelShape(l.xy, P);
    float body = sdRoundBox(l - vec3(0.0, 0.0, i == 3 ? -0.05 : -0.013), vec3(P.h + vec2(0.022), i == 3 ? 0.069 : 0.032), 0.008);
    body = max(body, -max(sh, 0.004 - l.z));
    res = opU(res, vec2(body, sh < 0.0 && l.z < 0.010 ? (i == 4 ? 190.0 : 186.0 + float(i)) : 193.0));
    vec3 back = P.c - P.n*0.05;
    if (i <= 2) {
      float neck = sdCapsule(q, back, vec3(back.x, -0.40, back.z), 0.022);
      neck = min(neck, sdRoundBox(q - vec3(back.x, -0.393, back.z), vec3(0.11, 0.007, 0.08), 0.005));
      res = opU(res, vec2(neck, 195.0));
    } else if (i == 4) res = opU(res, vec2(sdCapsule(q, back, vec3(back.x, back.y + 0.03, -1.06), 0.018), 195.0));
  }
  // the stick's base and the throttle's quadrant (a slot its grip rides in); the stick and the grip themselves rigid
  // parts (plane_parts.glsl: the XR-60's pose them here); the pedals under the console on their bar
  res = opU(res, vec2(sdRoundBox(q - vec3(0.40, -0.392, -0.42), vec3(0.055, 0.012, 0.075), 0.01), 193.0));
  {
    float quad = sdRoundBox(q - vec3(-0.40, -0.385, -0.42), vec3(0.055, 0.02, 0.11), 0.01);
    quad = max(quad, -sdBox(q - vec3(-0.40, -0.365, -0.42), vec3(0.012, 0.02, 0.085)));
    res = opU(res, vec2(quad, 193.0));
  }
  res = partAt(res, PT_JET_STICK, vec2(0.0), p);
  res = partAt(res, PT_JET_THR, vec2(0.0), p);
  res = partAt(res, PT_WR_PEDAL, vec2(-1.0, 0.0), p); res = partAt(res, PT_WR_PEDAL, vec2(1.0, 0.0), p);
  res = opU(res, vec2(sdCapsule(q, vec3(-0.26, -1.165, -0.56), vec3(0.26, -1.165, -0.56), 0.02), 195.0));
  // the chair: a shell seat and its backrest (reclined 7 deg), a headrest, the arms on their posts; a pedestal on a
  // hexagonal base
  {
    float ch = sdRoundBox(q - vec3(0.0, -0.80, 0.12), vec3(0.25, 0.045, 0.24), 0.04);
    vec3 bq = q - vec3(0.0, -0.42, 0.36); bq.yz = rot2(bq.yz, -0.12);
    ch = min(ch, sdRoundBox(bq, vec3(0.235, 0.36, 0.045), 0.045));
    vec3 hq = q - vec3(0.0, 0.0, 0.32); hq.yz = rot2(hq.yz, -0.12);
    ch = min(ch, sdRoundBox(hq, vec3(0.13, 0.075, 0.035), 0.03));
    ch = min(ch, sdRoundBox(vec3(abs(q.x) - 0.31, q.y + 0.55, q.z - 0.14), vec3(0.035, 0.022, 0.16), 0.02));
    res = opU(res, vec2(ch, 194.0));
    float mt = sdCapsule(vec3(abs(q.x), q.yz), vec3(0.31, -0.57, 0.20), vec3(0.26, -0.78, 0.22), 0.016);
    mt = min(mt, sdCapsule(vec3(abs(q.x), q.yz), vec3(0.06, -0.10, 0.36), vec3(0.06, -0.06, 0.37), 0.012));
    mt = min(mt, sdCapsule(q, vec3(0.0, -0.85, 0.14), vec3(0.0, -1.12, 0.14), 0.04));
    mt = min(mt, max(pxHex(q.xz - vec2(0.0, 0.14), 0.30), abs(q.y + 1.13) - 0.02) - 0.01);
    res = opU(res, vec2(mt, 195.0));
  }
  // the capsule's fittings: two light discs in the ceiling, the violet strip round the ceiling's edge, the crest over
  // the bank, the core column behind to the left (its lamps on its face)
  res = opU(res, vec2(max(length(q.xz - vec2(sign(q.x)*0.52, 0.15)) - 0.22, abs(q.y - 0.99) - 0.012), 196.0));
  res = opU(res, vec2(max(abs(pxHex(q.xz - vec2(0.0, 0.15), 1.25) + 0.05) - 0.012, abs(q.y - 0.94) - 0.012), 197.0));
  res = opU(res, vec2(sdRoundBox(q - vec3(0.0, 0.66, -1.02), vec3(0.42, 0.055, 0.012), 0.006), 199.0));
  {
    float col = max(pxHex(q.xz - vec2(-0.92, 0.62), 0.17), abs(q.y + 0.45) - 0.72) - 0.01;
    res = opU(res, vec2(col, q.x > -0.80 && abs(q.z - 0.62) < 0.10 && abs(q.y + 0.45) < 0.68 ? 199.0 : 193.0));
  }
  return res;
}
#endif
vec2 mapXr60(vec3 p){
#if HAS_CABIN
  if (gPS.w > 0.5) return mapXr60Station(p);   // (no one aboard: the operations pod it is flown from)
#endif
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float shell = pxShell(p);
  vec2 res = vec2(shell, 176.0);
  if (shell < 0.05) {
    // the knife edges: the ridge, the chines and the nose, the blades' leading edges (they glow with the heat)
    vec3 sec = fusSection(p.z);
    float w = max(sec.x, 0.01), h = max(sec.y, 0.01);
    vec2 q = vec2(ap.x, p.y - sec.z);
    bool edge = p.z < -8.9 || abs(q.x - w) + abs(q.y + 0.05*h) < 0.07 || (q.x < 0.06 && q.y > 0.6*h);
    {
      vec3 bq = ap - PX_ROOT; bq.xy = rot2(bq.xy, -PX_DIH);
      float k = clamp(bq.x/PX_IN_SPAN, 0.0, 1.0);
      if (bq.x > -0.05 && bq.x < PX_IN_SPAN + 0.05 && abs(bq.y) < 0.14 && bq.z - PX_IN_SWEEP*k < 0.12) edge = true;
    }
    if (p.z > 7.6 || (p.z > 5.5 && length(p.xy) < 0.08)) edge = true;   // (the cone's tip)
    if (edge) res.y = 177.0;
    if (p.z > -7.0 && p.z < -5.6 && pxCrystal(p) < 0.012) res.y = 178.0;
    if (p.z > 6.0 && p.z < 7.5 && length(p.xy) < 0.75 && pxSection(p) > 0.03) res.y = 179.0;   // (the cone: the drive's metal)
  }
  // the keel's duct: a slot straight through it from its mouth to the expansion ramp, lined in the duct's dark metal
  {
    vec3 q = p - vec3(0.0, PX_KEEL_Y - 0.08, 2.5);
    float duct = sdRoundBox(q, vec3(0.085, 0.16, 4.5), 0.02);
    if (duct < res.x + 0.04) { res.x = -smin(-res.x, duct, 0.02); if (-duct >= res.x - 0.002) res.y = 183.0; }
  }
  // the lift emitters: four flush discs set 6 cm into the belly, the field's face at the top of each recess
  {
    vec3 e = ap.z < 0.0 ? PX_EMIT_F : PX_EMIT_R;
    float cyl = max(length(ap.xz - e.xz) - 0.40, abs(ap.y - e.y) - 0.06);
    if (cyl < res.x + 0.03) { res.x = max(res.x, -cyl); if (-cyl >= res.x - 0.002) res.y = 182.0; }
  }
  // the ring's struts: three from the cone to the ring (top, lower left, lower right)
  {
    float a = atan(p.y, p.x), sec = 2.0943951;
    a = a - sec*floor((a - 1.5707963)/sec + 0.5) - 1.5707963;   // (the nearest strut's angle, folded onto the top one)
    vec2 rq = length(p.xy)*vec2(cos(a), sin(a));
    vec3 sp = vec3(rq.x, rq.y, p.z);
    float strut = sdCapsule(sp, vec3(0.0, 0.40, 6.25), vec3(0.0, PX_RING_R - 0.02, 7.0), 0.065);
    res = opU(res, vec2(strut, 179.0));
  }
  // the moving parts where they are: each at its pose (left out of the airframe's own bake: gPartMode -2)
  if (gPartMode == -1) {
    for (int i = 0; i < 2; i++) {
      vec2 sd = vec2(i == 0 ? -1.0 : 1.0, 0.0);
      res = pxPartAt(res, PT_PX_TIP, sd, p); res = pxPartAt(res, PT_PX_ELEVON, sd, p); res = pxPartAt(res, PT_PX_FIN, sd, p);
    }
    for (int k = 0; k < 6; k++) res = pxPartAt(res, PT_PX_PETAL, vec2(float(k), 0.0), p);
    res = pxPartAt(res, PT_PX_RING, vec2(0.0), p);
  }
  return res;
}
// ---- the parts, each in its own frame (plane_parts.glsl pxPartPose)
vec2 pxPartField(int k, vec3 l){
  if (k == PT_PX_TIP) {   // the tip claw from its hinge: raked forward, its chord 2.0 m to 0.3, curling up toward its tip; its lens at the tip
    vec3 b = l; b.y -= PX_TIP_CURL*(l.x/PX_TIP_SPAN)*(l.x/PX_TIP_SPAN);
    float c = l.z - PX_TIP_LE;
    float w = 0.92*sdPanel(l.x, c, b.y, PX_TIP_SPAN, PX_TIP_RC, PX_TIP_TC, PX_TIP_SWEEP, 0.045, 1.0, 0.0, 0.0);
    float k2 = clamp(l.x/PX_TIP_SPAN, 0.0, 1.0);
    vec2 r = vec2(w, c - PX_TIP_SWEEP*k2 < 0.10 || l.x > PX_TIP_SPAN - 0.08 ? 177.0 : 176.0);
    r = opU(r, vec2(sdCapsule(b, vec3(PX_TIP_SPAN - 0.06, 0.0, -2.62), vec3(PX_TIP_SPAN - 0.06, 0.0, -2.42), 0.03), 185.0));
    return r;
  }
  if (k == PT_PX_ELEVON) return vec2(sdSurface(l.x, l.z, l.y, PX_IN_SPAN, PX_IN_RC, PX_IN_TC, PX_IN_SWEEP, 0.045, 0.72, 0.2, 2.7, 0.0, 0.0), 176.0);
  if (k == PT_PX_FIN) {   // its span from the pivot, 35% of the root's chord back from its leading edge: 2.1 m, 2.6 to 0.55 m of chord, raked 2.2 m
    float c = l.z + 0.9;
    return vec2(sdPanel(l.x, c, l.y, 2.1, 2.6, 0.55, 2.2, 0.05, 1.0, 0.0, 0.0), c - 2.2*clamp(l.x/2.1, 0.0, 1.0) < 0.1 ? 177.0 : 176.0);
  }
  if (k == PT_PX_PETAL) {   // a petal: a tapered plate hinged on the ring, its inner face the plasma's
    float taper = 1.0 - 0.25*clamp(l.z, 0.0, 1.0);
    float plate = 0.9*sdRoundBox(vec3(l.x - 0.10, l.y/taper, l.z - 0.52), vec3(0.05, 0.38, 0.50), 0.03);
    return vec2(plate, l.x < 0.09 ? 181.0 : 179.0);
  }
  if (k == PT_PX_RING) {   // the ring: a torus about its axis (z), its outer face cut in twelve lugs that show its spin; its inner face the plasma's
    float a = atan(l.y, l.x), rr = length(l.xy);
    float t = PX_RING_T*(1.0 + 0.12*cos(12.0*a)*smoothstep(PX_RING_R, PX_RING_R + 0.1, rr));
    float ring = 0.9*(length(vec2(rr - PX_RING_R, l.z)) - t);
    return vec2(ring, rr < PX_RING_R - 0.04 ? 184.0 : 179.0);
  }
  return vec2(1e9, 0.0);
}
#endif
