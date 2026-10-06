//! kPlaneSDF
//! The aircraft distance fields: light aircraft and airliners from the packed model (mapPlaneBody), the XR-30 (mapJet) and
//! its sealed cockpit (mapJetCockpit), and the light fixtures (mapPlane).
// Landing-gear bay: with the gear out (open > 0) a dark well is cut into the skin above the opening (c: centre of the
// opening, h: half width / half length, depth upwards) and two doors, hinged along the bay's long edges, swing down.
// Closed, the skin is untouched.
// turboprop nacelle section at body z (mirrors the engine code: a front cone to 30% of the length, then a rear cone
// that rises towards the wing and shrinks to 35%): returns (centre y, radius)
vec2 nacSection(float z){
  vec4 N0 = gM[16], N1 = gM[17];
  float nr = N0.z, z0 = N0.w, len = N1.x, z1 = z0 + len*0.3;
  if (z < z1) { float s = clamp((z - z0)/max(z1 - z0, 0.01), 0.0, 1.0); return vec2(N0.y + 0.02*s, mix(nr*0.72, nr, s)); }
  float wingY = gM[10].x + N0.x*gM[10].z, s = clamp((z - z1)/max(len*0.7, 0.01), 0.0, 1.0);
  return vec2(N0.y + mix(0.02, wingY - N0.y - nr*0.25, s), mix(nr, nr*0.35, s));
}
// c.y is the skin height at the hinges; the cut reaches `below` further down to open a curved belly between them.
vec2 gearBay(vec3 q, vec2 res, vec3 c, vec2 h, float depth, float below, float open){
  if (open <= 0.001) return res;
  vec3 r = q - c;
  float well = sdBox(r - vec3(0.0, 0.5*(depth - below), 0.0), vec3(h.x, 0.5*(depth + below), h.y));
  well = max(well, res.x + 0.03);   // the well stays inside the structure: a skin is left on the far side of a thin wing
  if (-well > res.x) res = vec2(-well, 6.0); else res.x = max(res.x, -well);
  float a = open*1.45, t = 0.012;
  for (int k = 0; k < 2; k++) {
    float s = k == 0 ? 1.0 : -1.0;
    vec2 v = rot2(vec2(-s*(r.x - s*h.x), r.y), a);   // door frame: u inward from the hinge, y up (door rotated down by a)
    float door = sdBox(vec3(v.x - h.x*0.5, v.y + t, r.z), vec3(h.x*0.5, t, h.y - 0.01));
    res = opU(res, vec2(door, 5.0));
  }
  return res;
}
float sdFuselage(vec3 p){
  vec3 sec = fusSection(p.z);
  vec2 q = vec2(p.x, p.y - sec.z);
  float rnd = gM[15].z;
  float m = min(sec.x, sec.y);
  float dEll = (length(q/sec.xy) - 1.0)*m;
  float r = m*mix(0.3, 1.0, rnd);
  vec2 d = abs(q) - sec.xy + r;
  float dRR = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
  float d2 = mix(dRR, dEll, rnd);
  float dz = max(gM[1].x - p.z, p.z - gM[8].x);
  // Signed caps keep the nose/tail shell closed and make the cap rim a small rolled edge.
  float roll = min(m*0.2, 0.025);
  vec2 cap = vec2(d2, dz) + roll;
  return min(max(cap.x, cap.y), 0.0) + length(max(cap, 0.0)) - roll;
}

// Tapered airfoil panel. s = spanwise (>= 0 from root), c = chordwise from root LE (aft +), t = thickness axis.
// cutC: chordwise position (relative to local LE, as chord fraction) behind which the panel is trimmed when s in [cut0, cut1].
float sdPanel(float s, float c, float t, float span, float rc, float tc, float sweep, float th, float cutF, float cut0, float cut1){
  float k = clamp(s/span, 0.0, 1.0);
  float ch = mix(rc, tc, k); float le = sweep*k;
  float r1 = th*ch*0.5, r2 = max(0.004*ch, 0.005);
  float d2 = sdUnevenCapsule2(vec2(t, c - le - r1), r1, r2, max(ch - r1 - r2, 0.01));
  if (s > cut0 && s < cut1) d2 = max(d2, c - le - ch*cutF);
  float ds = s - span;
  d2 = -smin(-d2, -ds, min(th*ch*0.2, 0.025));  // round only the fixed panel's outer tip
  float d = length(max(vec2(d2, ds), 0.0)) + min(max(d2, ds), 0.0);
  return max(d, -s - 0.02);
}
// Hinged control surface behind the hinge line. defl = geometry rotation (radians) in the (t, c) plane.
float sdSurface(float s, float c, float t, float span, float rc, float tc, float sweep, float th, float hingeF, float s0, float s1, float defl, float slide){
  float k = clamp(s/span, 0.0, 1.0);
  float ch = mix(rc, tc, k); float le = sweep*k;
  vec2 q = vec2(t, c - (le + ch*hingeF + 0.008 + slide*ch));
  q = rot2(q, -defl);
  float len = ch*(1.0 - hingeF) - 0.01;
  float halfT = max(th*ch*0.5*(0.42 - 0.38*clamp(q.y/len, 0.0, 1.0)), 0.004);
  vec3 b = vec3(max(s0 - s, s - s1), abs(q.x) - halfT, max(-q.y, q.y - len));
  return length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0) - 0.003;
}

// Shared fleet gear geometry. The original wheel envelope and contact centres stay intact.
// Fine tread, recess rings and bolt heads are shaded once at a hit, not in every ray step.
vec2 gearWheelDetails(vec3 q, vec2 res, float r, float h, bool braked){
  if (sdBox(q, vec3(h + 0.08, r*0.69 + 0.04, r*0.69 + 0.04)) >= res.x) return res;
  vec3 face = vec3(abs(q.x) - h - 0.004, q.yz);
  float rim = sdRoundCylX(face, r*0.55, 0.018, 0.007);
  float cap = sdRoundCylX(face - vec3(0.020, 0.0, 0.0), r*0.23, 0.020, 0.008);
  float metal = min(rim, cap);
  if (braked) metal = min(metal, sdRoundCylX(q + vec3(h + 0.022, 0.0, 0.0), r*0.65, 0.012, 0.005));
  return opU(res, vec2(metal, 8.0));
}
vec2 gearLegDetails(vec3 p, vec2 res, vec3 upper, vec3 lower, float shaft, bool oleo){
  vec3 centre = (upper + lower)*0.5;
  if (sdBox(p - centre, abs(upper - lower)*0.5 + vec3(0.32, 0.15, 0.32)) >= res.x) return res;
  float metal = sdCapsule(p, mix(upper, lower, 0.86), mix(upper, lower, 0.97), shaft*1.45);
  if (oleo) {
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.12), mix(upper, lower, 0.53), shaft*1.5));
    float scale = clamp(shaft/0.06, 0.65, 1.5);
    vec3 elbow = mix(upper, lower, 0.72) + vec3(0.0, 0.0, 0.12*scale);
    metal = min(metal, sdCapsule(p, mix(upper, lower, 0.56), elbow, 0.013*scale));
    metal = min(metal, sdCapsule(p, elbow, mix(upper, lower, 0.87), 0.013*scale));
    metal = min(metal, sdCapsule(p, upper + vec3(-0.18*scale, 0.0, -0.18*scale), mix(upper, lower, 0.37), 0.020*scale));
  }
  return opU(res, vec2(metal, 8.0));
}

// ---------------- XR-30 Specter research jet (engine code 5): blended lifting body with chines, cranked delta with
// elevons, all-moving canards, canted twin fins, 2D pitch-vectoring nozzles, opaque sensor canopy.
// The cockpit is a sealed pod: the pilot sees outside only through the panoramic and side display screens.
vec2 mapJetCockpit(vec3 p){
  vec4 E4 = gM[22]; vec3 q = p - E4.xyz;
  float cPitch = gCtl.x, cRoll = gCtl.y, cThr = gCtl.w;
  vec2 res = vec2(-sdEllipsoid(q - vec3(0.0, -0.05, 0.25), vec3(0.8, 0.72, 1.45)), 40.0);
  float r = length(q.xz), ang = atan(q.x, -q.z);
  // panoramic front display in a chamfered bezel, annunciator strip above it
  float scr = max(max(abs(r - 0.64) - 0.006, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30);
  res = opU(res, vec2(scr, 41.0));
  float bez = max(max(abs(r - 0.675) - 0.02, abs(ang) - 1.3), abs(q.y - 0.02) - 0.345);
  bez = max(bez, -max(max(r - 0.66, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30));   // window cut-out
  res = opU(res, vec2(bez, 44.0));
  res = opU(res, vec2(max(max(abs(r - 0.648) - 0.004, abs(ang) - 0.62), abs(q.y - 0.348) - 0.016), 49.0));
  // curved instrument console under the display: sloped top with five recessed multi-function displays
  {
    float topY = -0.30 - (0.66 - r)*0.35;
    float con = max(max(r - 0.67, 0.47 - r), max(abs(ang) - 1.18, max((q.y - topY)*0.94, -0.56 - q.y)));
    float k = clamp(floor(ang/0.42 + 0.5), -2.0, 2.0);
    float ar = (ang - k*0.42)*r;
    vec3 rq = vec3(ar, q.y - topY, r - 0.575);
    float rec = sdBox(rq, vec3(0.072, 0.012, 0.052));
    con = max(con, -rec);
    res = opU(res, vec2(con, 44.0));
    res = opU(res, vec2(sdBox(rq + vec3(0.0, 0.006, 0.0), vec3(0.07, 0.002, 0.05)), 45.0));
    // rotary knobs between the displays
    float kk = clamp(floor(ang/0.42), -3.0, 2.0) + 0.5;
    vec3 kq = vec3((ang - kk*0.42)*r, q.y - topY, r - 0.505);
    res = opU(res, vec2(sdCapsule(kq, vec3(0.0), vec3(0.0, 0.018, 0.0), 0.011), 47.0));
  }
  // side display bays beside the pilot (camera feeds) with framed bezels and vents
  vec3 sq = vec3(abs(q.x) - 0.635, q.y - 0.04, q.z - 0.24);  // bring the bezel corners inside the curved pod
  res = opU(res, vec2(sdBox(sq, vec3(0.005, 0.2, 0.3)), q.x < 0.0 ? 42.0 : 43.0));
  float sbz = sdRoundBox(sq - vec3(0.02, 0.0, 0.0), vec3(0.014, 0.23, 0.33), 0.012);
  sbz = max(sbz, -sdBox(sq - vec3(-0.01, 0.0, 0.0), vec3(0.02, 0.2, 0.3)));
  res = opU(res, vec2(sbz, 44.0));
  // side consoles: shelf with a small display and a grid of backlit keys; stick and throttle mounted on them
  {
    vec3 cq = vec3(abs(q.x) - 0.52, q.y + 0.44, q.z - 0.08);
    float shelf = sdRoundBox(cq, vec3(0.13, 0.05, 0.36), 0.015);
    res = opU(res, vec2(shelf, 44.0));
    res = opU(res, vec2(sdBox(cq - vec3(0.02, 0.051, -0.2), vec3(0.075, 0.002, 0.06)), q.x < 0.0 ? 52.0 : 53.0));
    vec3 bq = cq - vec3(0.0, 0.055, 0.12);
    vec2 cell = clamp(floor(bq.xz/0.032 + 0.5), vec2(-3.0, -2.0), vec2(3.0, 3.0));
    bq.xz -= cell*0.032;
    res = opU(res, vec2(sdRoundBox(bq, vec3(0.012, 0.006, 0.012), 0.003), 54.0));
  }
  // overhead switch panel angled towards the pilot
  {
    vec3 oq = q - vec3(0.0, 0.46, -0.25); oq.yz = rot2(oq.yz, 0.55);
    float ov = sdRoundBox(oq, vec3(0.26, 0.018, 0.13), 0.01);
    res = opU(res, vec2(ov, 44.0));
    vec3 tq = oq + vec3(0.0, 0.02, 0.0);
    vec2 c2 = clamp(floor(tq.xz/vec2(0.05, 0.06) + 0.5), vec2(-4.0, -1.0), vec2(4.0, 1.0));
    tq.xz -= c2*vec2(0.05, 0.06);
    res = opU(res, vec2(sdCapsule(tq, vec3(0.0), vec3(0.0, -0.022, 0.008), 0.0045), 47.0));
    res = opU(res, vec2(sdBox(oq + vec3(0.0, 0.019, 0.0), vec3(0.25, 0.001, 0.125)), 55.0));
  }
  // sculpted seat: shell, bolsters, headrest with light strip, harness
  {
    float seat = sdRoundBox(q - vec3(0.0, -0.6, 0.12), vec3(0.24, 0.06, 0.26), 0.05);
    vec3 bq = q - vec3(0.0, -0.17, 0.42); bq.yz = rot2(bq.yz, 0.22);
    seat = min(seat, sdRoundBox(bq, vec3(0.23, 0.42, 0.05), 0.05));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.25, q.y + 0.3, q.z - 0.3), vec3(0.04, 0.24, 0.12), 0.03));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.22, q.y + 0.52, q.z - 0.1), vec3(0.035, 0.07, 0.22), 0.03));
    seat = min(seat, sdRoundBox(q - vec3(0.0, 0.2, 0.47), vec3(0.13, 0.1, 0.05), 0.04));
    res = opU(res, vec2(seat, 46.0));
    vec3 hq = vec3(abs(q.x) - 0.1, q.y + 0.05, q.z - 0.36); hq.yz = rot2(hq.yz, 0.22);
    res = opU(res, vec2(sdBox(hq, vec3(0.022, 0.32, 0.006)), 56.0));
  }
  // side stick (right) follows pitch and roll; throttle grip (left) slides with the throttle
  {
    vec3 sb = q - vec3(0.42, -0.375, -0.02);
    res = opU(res, vec2(sdRoundBox(sb, vec3(0.045, 0.02, 0.07), 0.015), 47.0));   // the stick's base
    res = partAt(res, PT_JET_STICK, vec2(0.0), p);   // the stick and the throttle grip: rigid parts (plane_parts.glsl)
    res = partAt(res, PT_JET_THR, vec2(0.0), p);
  }
  // LED strips: ceiling spine, under the display bezel, along both consoles, footwell
  float led = sdCapsule(q, vec3(0.0, 0.50, -0.55), vec3(0.0, 0.60, 0.65), 0.01);
  led = min(led, max(max(abs(r - 0.69) - 0.005, abs(ang) - 1.15), abs(q.y + 0.565) - 0.004));
  led = min(led, sdCapsule(vec3(abs(q.x), q.y, q.z), vec3(0.39, -0.395, -0.25), vec3(0.39, -0.395, 0.42), 0.0025));
  res = opU(res, vec2(led, 48.0));
  res = opU(res, vec2(sdCapsule(q, vec3(-0.24, -0.60, -0.42), vec3(0.24, -0.60, -0.42), 0.008), 57.0));
  // recessed ceiling light bars either side of the spine: machined housings with warm diffuser lenses (id 58)
  {
    vec3 lq = vec3(abs(q.x) - 0.2, q.y - 0.58, q.z - 0.15);
    res = opU(res, vec2(sdRoundBox(lq, vec3(0.034, 0.035, 0.21), 0.01), 44.0));
    res = opU(res, vec2(sdRoundBox(lq + vec3(0.0, 0.035, 0.0), vec3(0.022, 0.003, 0.19), 0.002), 58.0));
  }
  return res;
}
vec2 mapJet(vec3 p){
  float gear = gPS.x, inside = gPS.w;
  if (inside > 0.5) return mapJetCockpit(p);
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z;
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float sgn = p.x > 0.0 ? 1.0 : -1.0;
  float body = sdFuselage(p);
  body = smin(body, sdEllipsoid(p - vec3(0.0, -0.12, -3.4), vec3(1.75, 0.1, 5.6)), 0.3);    // chines
  body = smin(body, sdEllipsoid(p - vec3(0.0, 0.36, 1.6), vec3(0.55, 0.3, 6.0)), 0.3);       // dorsal spine
  body = smin(body, sdRoundBox(ap - vec3(0.82, -0.12, 4.6), vec3(0.5, 0.42, 3.4), 0.3), 0.35); // engine bays
  body = max(body, -sdRoundBox(ap - vec3(0.95, -0.34, -1.4), vec3(0.3, 0.19, 0.62), 0.08));   // intakes
  vec2 res = vec2(body, 30.0);
  // cranked delta wing with elevons
  {
    float s = ap.x, t = p.y - (-0.18 - s*0.035), c = p.z + 1.6;
    float wing = sdPanel(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3);
    float elevon = sdSurface(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3, -cPitch*0.3 - cRoll*sgn*0.3, 0.0);
    wing = min(wing, elevon);
    float d = smin(res.x, wing, 0.25);
    res = vec2(d, wing < res.x ? 31.0 : res.y);
  }
  // all-moving canards
  {
    vec3 q = ap - vec3(0.6, -0.02, -6.4); q.yz = rot2(q.yz, cPitch*0.3);
    float can = sdPanel(q.x, q.z + 0.6, q.y, 1.5, 1.5, 0.45, 1.0, 0.05, 1.0, 0.0, 0.0);
    res = opU(res, vec2(can, 31.0));
  }
  // canted twin fins with rudders
  {
    vec3 q = ap - vec3(1.0, 0.3, 4.6); q.xy = rot2(q.xy, 0.42);
    float fin = sdPanel(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2);
    float rud = sdSurface(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2, -cYaw*0.4*sgn, 0.0);
    float f2 = min(fin, rud);
    res = vec2(smin(res.x, f2, 0.12), f2 < res.x ? 31.0 : res.y);
  }
  // 2D thrust-vectoring nozzles: they vector in pitch with the stick
  {
    vec3 q = ap - vec3(0.82, -0.12, 7.75);
    float a = gFlame.z;   // = -pitch*0.5 rad (vectoring)
    vec2 yz = rot2(q.yz, -a);
    vec3 nq = vec3(q.x, yz.x, yz.y - 0.5);
    float nzl = sdRoundBox(nq, vec3(0.44, 0.31, 0.5), 0.06);
    float cav = sdBox(nq - vec3(0.0, 0.0, 0.25), vec3(0.36, 0.23, 0.6));
    res.x = max(res.x, -cav);   // the duct is hollow right down to the turbine: no airframe inside it
    float nzlIn = -cav;   // the cavity walls (inside the duct) are soot-black, not the outer finish
    nzl = max(nzl, nzlIn);
    res = opU(res, vec2(nzl, nzlIn > sdRoundBox(nq, vec3(0.44, 0.31, 0.5), 0.06) - 0.001 ? 37.0 : 33.0));
    // exhaust section, deepest first: the last turbine stage behind a hot tail cone, afterburner spray bars and two
    // flame-holder rings, a ribbed liner, and the convergent-divergent flaps that form the 2D throat
    if (cav < 0.05) {
      vec3 iq = nq - vec3(0.0, 0.0, -0.35);
      float ang = atan(iq.y, iq.x), rr = length(iq.xy);
      float disc = max(sdBox(iq, vec3(0.36, 0.23, 0.02)), -iq.z - 0.02);
      float blades = max(max(abs(fract(ang*23.0/6.2832 + rr*1.5) - 0.5)*rr*0.27 - 0.006, abs(iq.z - 0.03) - 0.012), rr - 0.225);
      float hub = sdRoundCone(iq, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.24), 0.1, 0.02);
      res = opU(res, vec2(min(min(disc, blades), hub), 36.0));
      vec3 hq = nq - vec3(0.0, 0.0, -0.08);
      float gut = min(sdTorus(hq.xzy, vec2(0.17, 0.012)), sdTorus(hq.xzy, vec2(0.085, 0.01)));          // V-gutter rings
      vec2 bq = hq.xy; float ba = atan(bq.y, bq.x); ba = mod(ba + 0.3927, 0.7854) - 0.3927;
      vec2 br2 = length(bq)*vec2(cos(ba), sin(ba));
      float bars = max(sdBox(vec3(br2.x - 0.13, br2.y, hq.z + 0.04), vec3(0.11, 0.006, 0.006)), max(abs(nq.x) - 0.35, abs(nq.y) - 0.22));
      res = opU(res, vec2(min(gut, bars), 37.0));
      float wall = -cav;   // distance into the walls from the cavity
      float ribs = max(abs(fract(nq.z/0.11) - 0.5)*0.11 - 0.01, -(wall + 0.012));
      ribs = max(ribs, max(nq.z - 0.02, -0.3 - nq.z));
      res = opU(res, vec2(ribs, 37.0));
      // C-D flaps: the duct narrows to the throat at z 0.28, then opens slightly to the exit
      float hz = 0.23 - 0.075*exp(-pow((nq.z - 0.28)/0.16, 2.0));
      float flap = max(max((hz - abs(nq.y))*0.9, abs(nq.x) - 0.36), max(0.06 - nq.z, nq.z - 0.56));
      res = opU(res, vec2(flap, 37.0));
    }
  }
  // sensor canopy (opaque gold film) and LED strips along the chines and wing leading edges
  // Lower the crown by 11 cm and blend the shoulders, retaining the opaque sensor film.
  float canopy = sdEllipsoid(p - vec3(0.0, 0.44, -4.6), vec3(0.62, 0.37, 2.0));
  res = vec2(smin(res.x, canopy, 0.16), canopy < res.x ? 32.0 : res.y);
  float led = sdCapsule(ap, vec3(1.55, -0.12, -2.6), vec3(0.35, -0.08, -7.6), 0.022);
  led = min(led, sdCapsule(ap, vec3(1.3, -0.24, -0.25), vec3(5.45, -0.39, 3.9), 0.02));
  led = min(led, sdCapsule(ap, vec3(0.62, 0.62, -3.0), vec3(0.3, 0.72, 2.0), 0.015));
  res = opU(res, vec2(led, 34.0));
  // retractable tricycle gear
  {   // gear bays: mains outboard, nose bay with twin doors
    vec4 G0 = gM[18];
    float open = smoothstep(0.0, 0.2, gear);
    res = gearBay(ap, res, vec3(G0.x, -0.355, G0.z), vec2(0.2, 0.46), 0.8, 0.06, open);   // skin heights measured
    res = gearBay(p, res, vec3(0.0, -0.39, G0.w), vec2(0.24, 0.42), 0.7, 0.05, open);
  }
  if (gear > 0.06) {
    vec4 G0 = gM[18], G1 = gM[19];
    float gh = G1.x, wr = 0.38, lift = (1.0 - gear)*(gh - 0.5);
    vec3 wc = vec3(G0.x, -gh + wr + lift, G0.z);
    float legs = sdCapsule(ap, vec3(G0.x*0.8, -0.3, G0.z), wc + vec3(-0.1, 0.05, 0.0), 0.07);
    float tyres = sdRoundCylX(ap - wc, wr, 0.13, 0.06);
    vec3 nc = vec3(0.0, -gh + 0.33 + lift, G0.w);
    legs = min(legs, sdCapsule(p, vec3(0.0, -0.35, G0.w), nc + vec3(0.0, 0.1, 0.0), 0.06));
    tyres = min(tyres, sdRoundCylX(vec3(abs(p.x) - 0.1, p.y, p.z) - vec3(0.0, nc.y, nc.z), 0.33, 0.07, 0.04));
    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
    res = gearWheelDetails(ap - wc, res, wr, 0.13, true);
    res = gearLegDetails(ap, res, vec3(G0.x*0.8, -0.3, G0.z), wc + vec3(-0.1, 0.05, 0.0), 0.07, true);
    vec3 nq = vec3(abs(p.x) - 0.1, p.y, p.z) - nc;
    res = gearWheelDetails(nq, res, 0.33, 0.07, false);
    res = gearLegDetails(p, res, vec3(0.0, -0.35, G0.w), nc + vec3(0.0, 0.1, 0.0), 0.06, true);
  }
  return res;
}
vec2 mapWraith(vec3 p);
vec2 mapWraithCockpit(vec3 p);
vec2 mapPlaneBody(vec3 p);
// light fixtures: a faired housing set into the airframe with a domed lens facing out along the light's axis
vec2 mapPlane(vec3 p){
  COST(1);
  vec2 res = mapPlaneBody(p);
  if (gPS.w > 0.5) return res;
  if (!gOwn) {   // traffic: the same fixtures, placed from the packed model (wingtips, fin top, tail cone)
    bool jet = int(gM[0].z + 0.5) == 5;
    vec3 tip = jet ? vec3(5.67, -0.38, 4.4) : vec3(gM[9].x + 0.07, gM[10].x + gM[9].x*gM[10].z, gM[10].y + gM[9].w + gM[9].z*0.25);
    vec3 fin = jet ? vec3(0.0, 0.67, 1.6) : vec3(0.0, gM[15].x + gM[14].x + 0.04, gM[15].y + gM[14].w + gM[14].z*0.4);
    vec3 tl = jet ? vec3(0.0, 0.45, 7.6) : vec3(0.0, gM[8].w, gM[8].x + 0.03);
    vec3 ap = vec3(abs(p.x), p.y, p.z);
    for (int i = 0; i < 3; i++) {
      vec3 c = i == 0 ? tip : (i == 1 ? fin : tl);
      vec3 d = i == 0 ? vec3(1.0, 0.0, 0.0) : (i == 1 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
      vec3 q = (i == 0 ? ap : p) - c;
      if (dot(q, q) > 0.09) continue;
      res = opU(res, vec2(sdRoundCone(q, -d*0.16, -d*0.025, 0.06, 0.05), 94.0));
      res = opU(res, vec2(max(length(q + d*0.012) - 0.05, -dot(q, d) - 0.012), i == 0 ? (p.x < 0.0 ? 101.0 : 102.0) : (i == 1 ? 104.0 : 103.0)));
    }
    return res;
  }
  for (int i = 0; i < 6; i++) {
    if (i >= uLensN) break;
    vec3 q = p - uLensP[i].xyz;
    if (dot(q, q) > 0.09) continue;
    vec3 d = uLensD[i].xyz;
    float housing = sdRoundCone(q, -d*0.16, -d*0.025, 0.06, 0.05);
    float lens = max(length(q + d*0.012) - 0.05, -dot(q, d) - 0.012);
    res = opU(res, vec2(housing, 94.0));
    res = opU(res, vec2(lens, 95.0 + float(i)));
  }
  return res;
}
// The Osprey C6's cabin trim (Codex, proposals/aircraft/osprey-mantis): static fittings in body coordinates (+z aft),
// drawn for that type only (gModelId == kOsprey in aircraft.h).
// IDs 120 ivory composite, 121 copper anodized trim, 122 tobacco upholstery,
// 123 dark cocoa textile, 124 warm-white lens. Static geometry ONLY.
vec2 mapOspreyCabinTrim(vec3 p) {
  vec2 r=vec2(1e5,120.0);
  // Copper brow sits below the existing glareshield and above all gauge faces.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.590,-2.516),vec3(.48,.009,.008),.004),121.0));
  // Sculpted ivory sill and copper inset: well below the side-window opening.
  vec3 q=vec3(abs(p.x),p.y,p.z);
  r=opU(r,vec2(sdRoundBox(q-vec3(.598,.125,-1.93),vec3(.023,.080,.47),.016),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.570,.154,-1.93),vec3(.009,.008,.40),.004),121.0));
  // Cocoa inset map pocket on each door. Recessed opening is geometry, not paint.
  float pocket=sdRoundBox(q-vec3(.580,-.090,-1.90),vec3(.025,.100,.245),.014);
  pocket=max(pocket,-sdRoundBox(q-vec3(.555,-.040,-1.90),vec3(.022,.052,.205),.012));
  r=opU(r,vec2(pocket,123.0));
  // Aft floor extension, centre aisle runner and paired copper edge strips.
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.330,.075),vec3(.53,.014,1.435),.008),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,-.308,.15),vec3(.095,.008,1.31),.004),123.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.109,-.306,.15),vec3(.004,.008,1.31),.004),121.0));
  // Four passenger seats, two abreast in two rows, inspired by the twin's pilot seats.
  for(int row=0;row<2;row++) {
    float z=-.38+float(row)*.92;
    vec3 s=vec3(abs(p.x)-.325,p.y,p.z-z);
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.105,0),vec3(.173,.065,.245),.035),122.0));
    vec3 b=s-vec3(0,.20,.235); b.yz=rot2(b.yz,-.12);
    r=opU(r,vec2(sdRoundBox(b,vec3(.163,.255,.050),.035),120.0));
    r=opU(r,vec2(sdRoundBox(b-vec3(0,0,-.052),vec3(.133,.220,.018),.018),122.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,.480,.270),vec3(.100,.055,.045),.028),122.0));
    // broad sewn channels (minimum 8 mm thickness); lower frame terminates on floor.
    for(int rib=0;rib<3;rib++) {
      float x=float(rib-1)*.072;
      r=opU(r,vec2(sdRoundBox(b-vec3(x,0,-.073),vec3(.004,.180,.004),.004),123.0));
    }
    vec3 leg=vec3(abs(s.x)-.118,s.y+.247,s.z);
    r=opU(r,vec2(sdRoundBox(leg,vec3(.013,.064,.175),.008),121.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.028,-.025),vec3(.142,.010,.015),.006),123.0));
    r=opU(r,vec2(sdRoundBox(s-vec3(0,-.015,-.025),vec3(.024,.008,.022),.006),121.0));
  }
  // Low cabin side liners and luggage-wall inlay stay under the windows.
  r=opU(r,vec2(sdRoundBox(q-vec3(.585,.040,.20),vec3(.022,.190,1.19),.018),120.0));
  r=opU(r,vec2(sdRoundBox(q-vec3(.557,.202,.20),vec3(.008,.008,1.14),.004),121.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.548),vec3(.405,.340,.014),.025),120.0));
  r=opU(r,vec2(sdRoundBox(p-vec3(0,.23,1.525),vec3(.215,.140,.008),.016),123.0));
  // Physical containment: every custom point is at least 90 mm inside the outer SDF.
  r.x=max(r.x,sdFuselage(p)+.090);
  return r;
}
vec3 ospreyCabinAlbedo(int id) {
  if(id==120)return vec3(.82,.76,.62);
  if(id==121)return vec3(.54,.235,.095);
  if(id==122)return vec3(.30,.115,.052);
  if(id==123)return vec3(.062,.040,.028);
  return vec3(.95,.82,.58);
}
const int kOspreyModel = 8;   // (aircraft.h kOsprey)
vec2 mapPlaneBody(vec3 p){
  if (gPartMode >= 0) return partField(gPartMode, p);   // (the mesh bake: one rigid part alone, in its own frame)
  if (int(gM[0].z + 0.5) == 5) return mapJet(p);
  if (int(gM[0].z + 0.5) == 6) return mapWraith(p);
  float L = gM[0].x; int gtype = int(gM[0].y + 0.5); int eng = int(gM[0].z + 0.5); float R = gM[0].w;
  float gear = gPS.x, flaps = gPS.y, steer = gPS.z, inside = gPS.w;
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z, cThr = gCtl.w;
  // ---------------- fuselage (hollow with window openings in cockpit view)
  float f = sdFuselage(p);
  vec2 res = vec2(f, 1.0);
  if (inside > 0.5) {
    // hollow cabin with real window openings
    vec4 E = gM[22]; vec4 WS = gM[23]; vec3 sec = fusSection(p.z);
    float shell = abs(f + 0.03) - 0.03;
    float holeWs = sdBox(p - vec3(0.0, WS.z + 1.0, 0.5*(WS.x + WS.y)), vec3(sec.x*1.25, 1.0, 0.5*(WS.y - WS.x)));
    float post = gM[21].z > 1.5 ? min(abs(p.x) - 0.03, abs(abs(p.x) - abs(E.x) - 0.42) - 0.035) : abs(p.x) - 0.025;
    holeWs = max(holeWs, -post);
    float sideTop = sec.z + sec.y*0.78;
    float holeSide = sdBox(p - vec3(0.0, 0.5*(WS.z - 0.12 + sideTop), 0.5*(WS.y + WS.w)), vec3(5.0, 0.5*(sideTop - WS.z + 0.12), 0.5*(WS.w - WS.y)));
    holeSide = max(holeSide, 0.3 - abs(p.x));
    holeSide = max(holeSide, -(abs(p.z - WS.y - 0.04) - 0.025));
    shell = max(shell, -min(holeWs, holeSide));
    res = vec2(shell, 11.0);
    // rear bulkhead: a trimmed baggage wall closes the cabin behind the last seats / windows (instead of looking
    // straight down the hollow tail cone)
    float zB = gM[20].x > 0.5 ? gM[20].z + 0.15 : WS.w + (gM[21].z > 0.5 ? 0.9 : 0.75);
    res = opU(res, vec2(max(f + 0.03, abs(p.z - zB) - 0.02), 63.0));
  }
  // Each part below is skipped when its bounding box is farther than the nearest surface found so far (plus its blend
  // radius): its own distance can only be larger, so the result is unchanged - but a sample in the cabin no longer
  // evaluates the wingtips, the tail and the wheels (the cockpit view takes ~30 such samples per pixel).
  // ---------------- main wing with flaps and ailerons
  vec4 W0b = gM[9], W1b = gM[10], W2b = gM[11];
  float wy0 = min(W1b.x, W1b.x + W0b.x*W1b.z), wy1 = max(W1b.x, W1b.x + W0b.x*W1b.z);
  float wyLo = (W2b.x > 0.5 ? min(wy0, -0.35*R) : wy0) - 0.45 - W2b.z, wyHi = wy1 + 0.45 + W2b.z;
  float wz0 = W1b.y - 0.45, wz1 = W1b.y + max(W0b.y, W0b.w + W0b.z) + 0.45;
  if (sdBox(p - vec3(0.0, 0.5*(wyLo + wyHi), 0.5*(wz0 + wz1)), vec3(W0b.x + 0.35, 0.5*(wyHi - wyLo), 0.5*(wz1 - wz0))) < res.x + 0.06*R + 0.1) {
    vec4 W0 = gM[9], W1 = gM[10], W2 = gM[11];
    float span = W0.x, rc = W0.y, tc = W0.z, sw = W0.w, th = W1.w;
    float s = abs(p.x);
    float t = p.y - (W1.x + s*W1.z);
    float c = p.z - W1.y;
    float sgn = p.x > 0.0 ? 1.0 : -1.0;
    float fus0 = 0.55*R, flapEnd = span*W2.w, ailEnd = span*0.94;
    float wing = sdPanel(s, c, t, span, rc, tc, sw, th, 0.74, fus0, ailEnd);
    float flap = sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, fus0, flapEnd, flaps*0.62, flaps*0.1);
    // right aileron TE goes UP for right roll; left goes down
    float ail = sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, flapEnd + 0.03, ailEnd, -cRoll*sgn*0.33, 0.0);
    float wd = min(wing, min(flap, ail));
    if (W2.z > 0.01) {  // winglet
      float wl = sdPanel(t - 0.02, c - sw - tc*0.15, s - span + 0.05, W2.z, tc*0.85, tc*0.4, 0.55, 0.09, 1.0, 0.0, 0.0);
      wd = smin(wd, wl, 0.08);
    }
    if (gM[15].w > 0.5) {  // leading-edge slats (STOL)
      float sl = sdPanel(s, c + 0.09, t + 0.03, span*0.95, rc*0.16, tc*0.16, sw, 0.5, 1.0, 0.0, 0.0);
      wd = min(wd, max(sl, fus0 + 0.4 - s));
    }
    float fd = res.x;
    if (inside > 0.5) { wd = max(wd, -f); res.x = min(res.x, wd); }
    else res.x = smin(res.x, wd, 0.08*R);
    if (wd < fd) res.y = 2.0;
    // lift struts
    if (W2.x > 0.5) {
      vec3 sec = fusSection(p.z);
      float k = clamp(W2.y/span, 0.0, 1.0); float ch = mix(rc, tc, k); float le = sw*k;
      vec3 top1 = vec3(W2.y, W1.x + W2.y*W1.z - th*ch*0.4, W1.y + le + ch*0.25);
      vec3 base = vec3(0.6*R, -0.35*R, W1.y + rc*0.35);
      vec3 ap = vec3(abs(p.x), p.y, p.z);
      float st = sdCapsule(ap, base, top1, 0.035);
      if (gM[15].w > 0.5) st = min(st, sdCapsule(ap, base, top1 + vec3(0.0, 0.0, ch*0.45), 0.03));
      if (inside > 0.5) st = max(st, -f);  // the exterior attachment must not protrude into the hollow cabin
      res = opU(res, vec2(st, 8.0));
    }
  }
  // ---------------- tail
  vec4 V0b = gM[14], V1b = gM[15], H0b = gM[12], H1b = gM[13];
  float tz0 = min(V1b.y, H1b.y) - 0.35, tz1 = max(V1b.y + max(V0b.y, V0b.w + V0b.z), H1b.y + max(H0b.y, H0b.w + H0b.z)) + 0.35;
  float ty0 = min(V1b.x, H1b.x - H0b.x*abs(H1b.z)) - 0.45, ty1 = max(V1b.x + V0b.x, H1b.x + H0b.x*abs(H1b.z)) + 0.45;
  if (sdBox(p - vec3(0.0, 0.5*(ty0 + ty1), 0.5*(tz0 + tz1)), vec3(max(H0b.x, 0.3) + 0.35, 0.5*(ty1 - ty0), 0.5*(tz1 - tz0))) < res.x + 0.12*R) {
    vec4 V0 = gM[14], V1 = gM[15];
    float s = p.y - V1.x, c = p.z - V1.y, t = p.x;
    float h = V0.x;
    float hasT = gM[13].w;
    float rud0 = hasT > 0.5 ? 0.05 : 0.08*h;
    float fin = sdPanel(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97);
    // right rudder (yaw +) swings the trailing edge to the right (+x)
    float rud = sdSurface(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97, -cYaw*0.42, 0.0);
    float tail = min(fin, rud);
    vec4 H0 = gM[12], H1 = gM[13];
    float hs = abs(p.x), ht = p.y - (H1.x + hs*H1.z), hc = p.z - H1.y;
    float stab = sdPanel(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98);
    // pulling back (pitch +) raises the elevator trailing edge
    float elev = sdSurface(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98, -cPitch*0.4, 0.0);
    tail = min(tail, min(stab, elev));
    if (hasT > 0.5) tail = smin(tail, sdEllipsoid(p - vec3(0.0, H1.x, H1.y + H0.y*0.45), vec3(0.18, 0.2, H0.y*0.55)), 0.08);
    float d = smin(res.x, tail, 0.12*R);
    res = vec2(d, tail < res.x ? 3.0 : res.y);
  }
  // ---------------- engines
  {
    vec4 N0 = gM[16], N1 = gM[17];
    vec4 S0 = gM[1];
    float eb;   // distance to the engines' bounding box
    if (eng <= 1) { float sr0 = max(N1.y, 0.1); eb = sdBox(p - vec3(0.0, S0.w, S0.x + 0.5*(1.75 - sr0*2.3)), vec3(0.95, 1.1, 0.5*(1.75 + sr0*2.3) + 0.25)); }
    else if (eng <= 3) { vec3 nq = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z); float yr = abs(gM[10].x + N0.x*gM[10].z - N0.y);
                         eb = sdBox(nq - vec3(0.0, 0.0, N0.w + 0.5*(N1.x - N1.y*2.3)), vec3(N0.z + 0.6, N0.z + yr + 0.6, 0.5*(N1.x + N1.y*2.3) + 0.4)); }
    else { vec3 nq = vec3(abs(p.x), p.y - N0.y, p.z - N0.w); eb = sdBox(nq - vec3(0.5*(N0.x + N0.z), 0.0, 0.5*N1.x), vec3(0.5*(N0.x + N0.z) + 0.3, N0.z + 0.4, 0.5*N1.x + 0.75)); }
    if (eb > res.x + 0.12) {}
    else if (eng <= 1) {
      float sr = N1.y;
      float spin = sdRoundCone(p, vec3(0.0, S0.w, S0.x - sr*2.3), vec3(0.0, S0.w, S0.x + 0.05), 0.015, sr);
      float spinJoin = smin(res.x, spin, 0.015);
      res = vec2(spinJoin, spin < res.x ? 16.0 : res.y);
      if (eng == 0) {
        vec3 sec = fusSection(S0.x + 0.9);
        float ex = sdCapsule(vec3(abs(p.x), p.y, p.z), vec3(0.12, sec.z - sec.y*0.85, S0.x + 0.9), vec3(0.16, sec.z - sec.y - 0.06, S0.x + 1.15), 0.035);
        res = opU(res, vec2(ex, 17.0));
      } else {
        vec3 sec = fusSection(S0.x + 1.2);
        vec3 ap = vec3(abs(p.x), p.y, p.z);
        float ex = sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.22, sec.z + 0.12, S0.x + 1.5), 0.085);
        ex = max(ex, -sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.4, sec.z + 0.14, S0.x + 1.6), 0.06));
        res = opU(res, vec2(ex, 17.0));
        float lip = sdCapsule(p, vec3(-0.12, S0.w - 0.32, S0.x + 0.45), vec3(0.12, S0.w - 0.32, S0.x + 0.45), 0.07);
        res.x = smin(res.x, lip, 0.08);
      }
    } else if (eng <= 3) {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z);
      float nr = N0.z, z0 = N0.w, len = N1.x;
      float wingY = gM[10].x + N0.x*gM[10].z;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, z0), vec3(0.0, 0.02, z0 + len*0.3), nr*0.72, nr);
      nac = smin(nac, sdRoundCone(np, vec3(0.0, 0.02, z0 + len*0.3), vec3(0.0, wingY - N0.y - nr*0.25, z0 + len), nr, nr*0.35), 0.1);
      if (eng == 3) {
        nac = smin(nac, sdEllipsoid(np - vec3(0.0, -nr*0.75, z0 + 0.45), vec3(nr*0.35, nr*0.22, 0.5)), 0.08);
        float ex = sdCapsule(np, vec3(nr*0.8, 0.1, z0 + len*0.35), vec3(nr*1.05, 0.15, z0 + len*0.5), 0.09);
        res = opU(res, vec2(ex, 17.0));
      }
      float nacJoin = smin(res.x, nac, nr*0.08);
      res = vec2(nacJoin, nac < res.x ? 5.0 : res.y);
      float sr = N1.y;
      res = opU(res, vec2(sdRoundCone(np, vec3(0.0, 0.0, z0 - sr*2.3), vec3(0.0, 0.0, z0 + 0.05), 0.015, sr), 16.0));
    } else {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z - N0.w);
      float nr = N0.z, len = N1.x;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, len), nr, nr*0.78);
      float inlet = sdCapsule(np, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.18), nr*0.82);
      nac = max(nac, -inlet);
      float exhaust = sdCapsule(np, vec3(0.0, 0.0, len - 0.15), vec3(0.0, 0.0, len + 1.0), nr*0.6);   // (the tailpipe: the cone below sat sealed inside the cap)
      nac = max(nac, -exhaust);
      res = opU(res, vec2(nac, 5.0));
      float fan = sdCapsule(np, vec3(0.0, 0.0, 0.2), vec3(0.0, 0.0, 0.3), nr*0.83);
      res = opU(res, vec2(fan, 21.0));
      float cone = sdRoundCone(np, vec3(0.0, 0.0, len - 0.25), vec3(0.0, 0.0, len + 0.3), nr*0.5, 0.04);
      res = opU(res, vec2(cone, 17.0));
      vec3 sec = fusSection(N0.w + len*0.5);
      float px0 = sec.x*0.7, px1 = N0.x - nr*0.8;
      float pylon = sdRoundBox(vec3(abs(p.x) - 0.5*(px0 + px1), p.y - N0.y, p.z - N0.w - len*0.5), vec3(0.5*(px1 - px0) + 0.05, 0.06, len*0.28), 0.04);
      res.x = smin(res.x, pylon, 0.1);
    }
  }
  // ---------------- cargo pod
  if (gM[17].w > 0.5) {
    vec3 sec = fusSection(-0.5);
    float pod = sdRoundBox(p - vec3(0.0, sec.z - sec.y - 0.18, -0.4), vec3(0.42, 0.2, 2.6), 0.17);
    pod = smin(pod, sdEllipsoid(p - vec3(0.0, sec.z - sec.y - 0.2, -3.0), vec3(0.42, 0.22, 0.8)), 0.2);
    res.x = smin(res.x, pod, 0.12);
  }
  // ---------------- landing gear (fixed types hang below the lower fuselage: a plane bounds them)
  float gearTop = 1e5;
  if (int(gM[0].y + 0.5) <= 2) { vec3 sg0 = fusSection(gM[18].z), sg1 = fusSection(gM[19].z < 0.5 ? gM[18].w : gM[19].y);
                                  gearTop = max(sg0.z - sg0.y*0.5, sg1.z - sg1.y*0.5) + 0.12; }
  if (gear > 0.02 && p.y - gearTop < res.x) {
    vec4 G0 = gM[18], G1 = gM[19];
    float track = G0.x, wr = G0.y, mz = G0.z, nz = G0.w, gh = G1.x, tz = G1.y;
    bool retract = gtype >= 3;
    float up = retract ? (1.0 - gear) : 0.0;
    vec3 ap = vec3(abs(p.x), p.y, p.z);
    float wy = -gh + wr;
    float liftN = up*(gh - R*0.6);
    // mains: into the wing / body (type 4) or up inside the engine nacelle (type 3)
    vec2 nsec = gtype == 3 ? nacSection(mz) : vec2(0.0);
    float upY = gtype == 3 ? nsec.x - nsec.y + 0.03 : -R*0.6;   // retracted wheel bottom: just inside the nacelle belly / at the wing
    float lift = up*(upY + gh);
    vec3 wc = vec3(track, wy + lift, mz);
    vec3 secM = fusSection(mz);
    float legs, tyres, spats = 1e5;
    if (retract) {   // wheel wells with doors, open whenever the gear is out
      float open = smoothstep(0.0, 0.2, gear);
      float hx = gtype == 3 ? 0.38 : 0.17, sk, below;
      if (gtype == 3) {   // nacelle: its section at the gear station; hinges where the round belly meets them
        float rr = nsec.y; hx = min(hx, rr*0.7);
        float hy = sqrt(max(rr*rr - hx*hx, 0.0));
        sk = nsec.x - hy; below = rr - hy + 0.03;
      } else {            // wing underside at the gear track
        // local half thickness of the panel section (sdPanel: an uneven capsule from the LE radius to the TE radius)
        float kk = clamp(track/gM[9].x, 0.0, 1.0), ch = mix(gM[9].y, gM[9].z, kk), le = gM[10].y + gM[9].w*kk;
        float r1 = gM[10].w*ch*0.5, r2 = max(0.004*ch, 0.005);
        float cc = clamp((mz - le - r1)/max(ch - r1 - r2, 0.01), 0.0, 1.0);
        sk = gM[10].x + track*gM[10].z - mix(r1, r2, cc); below = 0.03;
      }
      res = gearBay(ap, res, vec3(track, sk, mz), vec2(hx, wr + 0.08), upY + 2.0*wr - sk + 0.1, below, open);
      if (G1.z < 0.5) {
        vec3 sN = fusSection(nz); float nw = gtype == 3 ? wr*0.75 : wr*0.85, nx = gtype == 3 ? 0.3 : 0.14;
        float ey = sN.y*sqrt(max(1.0 - nx*nx/(sN.x*sN.x), 0.0));   // belly height at the hinges (elliptic section)
        res = gearBay(p, res, vec3(0.0, sN.z - ey, nz), vec2(nx, nw + 0.08), 2.0*nw + 0.15 + sN.y - ey, sN.y - ey + 0.03, open);
      }
    }
    if (gtype == 0) {
      legs = sdCapsule(ap, vec3(secM.x*0.75, secM.z - secM.y*0.8, mz), wc + vec3(-0.06, 0.04, 0.0), 0.03);
      tyres = sdRoundCylX(ap - wc, wr, 0.065, 0.04);
      float sp = sdEllipsoid(ap - wc - vec3(0.0, 0.04, 0.06), vec3(0.1, wr*1.05, wr*1.9));
      spats = max(sp, -(ap.y - (wc.y - wr*0.5)));
    } else if (gtype == 1) {
      legs = sdCapsule(ap, vec3(secM.x*0.7, secM.z - secM.y*0.85, mz), wc + vec3(-0.08, 0.06, 0.0), 0.045);
      tyres = sdRoundCylX(ap - wc, wr, 0.09, 0.05);
      tyres = min(tyres, sdRoundCylX(ap - wc - vec3(0.06, 0.0, 0.0), wr*0.45, 0.04, 0.02));
    } else if (gtype == 2) {
      legs = min(sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz - 0.35), wc, 0.03), sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz + 0.3), wc, 0.03));
      tyres = sdRoundCylX(ap - wc, wr, 0.14, 0.09);
    } else if (gtype == 3) {
      vec3 top = vec3(track, nsec.x, mz);   // the leg goes up into the nacelle
      legs = sdCapsule(ap, top, vec3(track, wc.y + 0.05, mz), 0.09);
      legs = min(legs, sdCapsule(ap, vec3(track - 0.25, wc.y, mz), vec3(track + 0.25, wc.y, mz), 0.05));
      tyres = min(sdRoundCylX(ap - wc - vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06), sdRoundCylX(ap - wc + vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06));
    } else {
      float wyy = gM[10].x + track*gM[10].z;
      legs = sdCapsule(ap, vec3(track, wyy - 0.1 + lift*0.2, mz), wc + vec3(0.0, 0.05, 0.0), 0.06);
      tyres = sdRoundCylX(ap - wc, wr, 0.1, 0.05);
    }
    // nose / tail wheel (nose wheel steers with the rudder pedals)
    if (G1.z < 0.5) {
      vec3 secN = fusSection(nz);
      float nwr = gtype == 3 ? wr*0.75 : wr*0.85;
      vec3 piv = vec3(0.0, 0.0, nz);
      vec3 q = p - piv; q.xz = rot2(q.xz, -steer);  // geometry rotated by the steering angle
      vec3 nc = vec3(0.0, -gh + nwr + (retract ? liftN : lift), 0.0);
      float nl = sdCapsule(q, vec3(0.0, secN.z - secN.y*0.7, -0.05), nc + vec3(0.0, nwr*0.9, 0.0), gtype >= 3 ? 0.07 : 0.035);
      nl = min(nl, sdCapsule(vec3(abs(q.x), q.yz), vec3(0.06 + (gtype == 3 ? 0.15 : 0.0), nc.y + nwr*0.9, 0.0), vec3(0.06 + (gtype == 3 ? 0.15 : 0.0), nc.y, 0.0), 0.02));
      float nt = gtype == 3 ? min(sdRoundCylX(q - nc - vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04), sdRoundCylX(q - nc + vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04))
                            : sdRoundCylX(q - nc, nwr, 0.055, 0.035);
      if (gtype == 0) { float sp = sdEllipsoid(q - nc - vec3(0.0, 0.05, 0.06), vec3(0.1, nwr*1.12, nwr*2.2)); spats = min(spats, max(sp, -(q.y - (nc.y - nwr*0.5)))); }
      legs = min(legs, nl); tyres = min(tyres, nt);
      if (!retract || gear >= 0.06) {
        float halfWidth = gtype == 3 ? 0.07 : 0.055;
        vec3 wheelQ = gtype == 3 ? vec3(abs(q.x) - 0.15, q.yz) - nc : q - nc;
        res = gearWheelDetails(wheelQ, res, nwr, halfWidth, false);
        res = gearLegDetails(q, res, vec3(0.0, secN.z - secN.y*0.7, -0.05), nc + vec3(0.0, nwr*0.9, 0.0), gtype >= 3 ? 0.07 : 0.035, true);
        if (gtype == 0) spats = max(spats, -sdCylX(q - nc, nwr*0.54, 0.12));
      }
    } else {
      vec3 q = p - vec3(0.0, 0.0, tz); q.xz = rot2(q.xz, -steer);
      vec3 tsec = fusSection(tz - 0.3);
      vec3 tc = vec3(0.0, -gh + 0.11*L + 0.1, 0.0);
      legs = min(legs, sdCapsule(q, vec3(0.0, tsec.z - tsec.y*0.6, -0.3), tc + vec3(0.0, 0.03, -0.05), 0.02));
      tyres = min(tyres, sdRoundCylX(q - tc, 0.1, 0.035, 0.02));
      res = gearWheelDetails(q - tc, res, 0.1, 0.035, false);
      res = gearLegDetails(q, res, vec3(0.0, tsec.z - tsec.y*0.6, -0.3), tc + vec3(0.0, 0.03, -0.05), 0.02, false);
    }
    if (retract && gear < 0.06) { legs = 1e5; tyres = 1e5; }
    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
    if (!retract || gear >= 0.06) {
      float halfWidth = gtype == 3 ? 0.11 : gtype == 0 ? 0.065 : gtype == 1 ? 0.09 : gtype == 2 ? 0.14 : 0.1;
      vec3 wheelQ = ap - wc;
      if (gtype == 3) wheelQ.x = abs(wheelQ.x) - 0.22;
      res = gearWheelDetails(wheelQ, res, wr, halfWidth, true);
      if (gtype == 0) spats = max(spats, -sdCylX(ap - wc, wr*0.54, 0.12));
      vec3 mount = gtype == 3 ? vec3(track, nsec.x, mz) : gtype >= 4 ? vec3(track, gM[10].x + track*gM[10].z - 0.1 + lift*0.2, mz) :
                   vec3(secM.x*(gtype == 2 ? 0.8 : gtype == 0 ? 0.75 : 0.7), secM.z - secM.y*(gtype == 1 ? 0.85 : 0.8), mz);
      vec3 ankle = gtype >= 3 ? wc + vec3(0.0, 0.05, 0.0) : gtype == 2 ? wc : wc + vec3(gtype == 0 ? -0.06 : -0.08, gtype == 1 ? 0.06 : 0.04, 0.0);
      float shaft = gtype == 3 ? 0.09 : gtype >= 4 ? 0.06 : gtype == 1 ? 0.045 : 0.03;
      res = gearLegDetails(ap, res, mount, ankle, shaft, retract);
    }
    res = opU(res, vec2(spats, 1.0));
  }
  // ---------------- small details: nav lights, beacon, antennas, pitot
  {
    vec4 W0 = gM[9], W1 = gM[10];
    vec3 tip = vec3(W0.x + 0.02, W1.x + W0.x*W1.z, W1.y + W0.w + W0.z*0.25);
    res = opU(res, vec2(length(vec3(abs(p.x), p.y, p.z) - tip) - 0.045, 18.0));
    vec4 V0 = gM[14], V1 = gM[15];
    res = opU(res, vec2(length(p - vec3(0.0, V1.x + V0.x + 0.04, V1.y + V0.w + V0.z*0.4)) - 0.05, 19.0));
    vec3 sec = fusSection(0.2);
    float ant = sdRoundBox(p - vec3(0.0, sec.z + sec.y + 0.11, 0.2), vec3(0.006, 0.12, 0.05), 0.004);
    res = opU(res, vec2(ant, 8.0));   // (8: exterior metal - 13 is a cockpit material, lit as if inside the cabin)
    float span = W0.x, rcd = W0.y, tcd = W0.z, swp = W0.w, thk = W1.w;
    {   // pitot tube under the left wing: a faired mast and the probe pointing into the airflow
      float ks = 0.62, ch = mix(rcd, tcd, ks), le = W1.y + swp*ks, sx = -span*ks;
      float yu = W1.x + span*ks*W1.z - thk*ch*0.42;   // just inside the wing's underside at the quarter chord
      vec3 mb = vec3(sx, yu - 0.1, le + ch*0.3);
      vec3 q = p - mb;
      if (dot(q, q) < 0.16) {
        float pit = sdRoundBox(vec3(q.x, q.y - 0.055, q.z - 0.01), vec3(0.006, 0.06, 0.022), 0.005);
        pit = min(pit, sdCapsule(q, vec3(0.0, 0.0, 0.02), vec3(0.0, 0.0, -0.2), 0.008));
        res = opU(res, vec2(pit, 8.0));
      }
    }
    {   // static dischargers: two wicks off each wing's trailing edge near the tip
      float ks = 0.915, ch = mix(rcd, tcd, ks), te = W1.y + swp*ks + ch;
      vec3 q = vec3(abs(p.x) - span*ks, p.y - (W1.x + span*ks*W1.z), p.z - te);
      if (dot(q, q) < 0.09) {
        float dz = (tcd - rcd)/span*0.035*span + swp/span*0.035*span;   // the trailing edge's slope over the wicks' spacing
        float wk = min(sdCapsule(q, vec3(-0.035*span, 0.0, -dz - 0.01), vec3(-0.035*span, 0.0, -dz + 0.11), 0.0035),
                       sdCapsule(q, vec3(0.035*span, 0.0, dz - 0.01), vec3(0.035*span, 0.0, dz + 0.11), 0.0035));
        res = opU(res, vec2(wk, 6.0));
      }
    }
    {   // VHF blade antenna under the belly, raked aft
      float za = gM[23].w + 0.5; vec3 sb = fusSection(za);
      vec3 q = p - vec3(0.0, sb.z - sb.y - 0.06, za);
      if (dot(q, q) < 0.04) {
        q.zy = rot2(q.zy, -0.35);
        res = opU(res, vec2(sdRoundBox(q, vec3(0.004, 0.07, 0.035), 0.003), 8.0));
      }
    }
  }
  // ---------------- cockpit interior (only rendered from inside)
  // Ids: 10 panel (instruments drawn on it), 11 shell/floor, 12 seats, 13 controls, 14 glareshield & overhead,
  // 60 brushed metal, 61 rubber, 63 trim panels, 64 light lenses, 65 radio stack, 66 satin black (bezels, knobs),
  // 67 centre engine display (glass cockpits), 68 red knobs / buttons, 69 harness webbing
  if (inside > 0.5) {
    vec4 E = gM[22]; float pz = gM[21].w, phw = E.w; int ck = int(gM[21].z + 0.5);
    float pf = pz + 0.045;                                             // panel face (towards the pilot)
    float panel = sdRoundBox(p - vec3(0.0, E.y - 0.36, pz), vec3(phw, 0.24, 0.045), 0.015);
    panel = max(panel, f + 0.06);  // contour the panel corners to the inside of the cowling
    res = opU(res, vec2(panel, 10.0));
    // raised bezels framing each pilot's instrument cluster
    {
      float cx = ck == 2 ? 0.02 : (ck == 1 ? 0.09 : 0.05), hx = ck == 2 ? 0.2 : (ck == 1 ? 0.255 : 0.215), hy = ck == 2 ? 0.098 : 0.112;
      bool co = p.x*E.x < 0.0;
      if (co) hx = coCluster(ck).y;
      float sx = !co ? E.x + cx : -E.x + coCluster(ck).x + coShift(E, ck);   // copilot cluster: other seat, kept on the panel
      vec2 fq = vec2(p.x - sx, p.y - (E.y - 0.32));
      vec2 dq = abs(fq) - vec2(hx, hy) + 0.02; float fr = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - 0.02;
      float frame = max(abs(fr) - 0.007, abs(p.z - pf - 0.006) - 0.006);
      res = opU(res, vec2(frame, 66.0));
    }
    // glareshield with a warm LED strip under its lip that floods the panel
    vec4 WSg = gM[23];
    float gz0 = min(WSg.x - 0.05, pz - 0.2);
    float glare = sdRoundBox(p - vec3(0.0, E.y - 0.1, 0.5*(gz0 + pz + 0.06)), vec3(phw*0.97, 0.022, 0.5*(pz + 0.06 - gz0)), 0.018);
    glare = max(glare, f + 0.055);
    res = opU(res, vec2(glare, 14.0));
    res = opU(res, vec2(sdCapsule(p, vec3(-phw*0.88, E.y - 0.124, pz + 0.07), vec3(phw*0.88, E.y - 0.124, pz + 0.07), 0.0035), 64.0));
    // centre: radio / transponder stack below the clusters; glass cockpits add an engine display between the PFDs
    res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y - 0.505, pf + 0.012), vec3(0.115, 0.06, 0.016), 0.004), 65.0));
    if (ck == 2) res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y - 0.31, pf + 0.008), vec3(0.085, 0.085, 0.01), 0.004), 67.0));
    // eyeball air vents at the panel corners
    {
      vec3 vq = vec3(abs(p.x) - gCab1.w, p.y - (E.y - 0.19), p.z - pf);
      res = opU(res, vec2(max(sdRoundCylX(vq.zyx, 0.03, 0.012, 0.004), -sdRoundCylX(vq.zyx - vec3(0.012, 0.0, 0.0), 0.02, 0.01, 0.002)), 60.0));
      res = opU(res, vec2(length(vq - vec3(0.0, 0.0, 0.004)) - 0.019, 66.0));
    }
    float floor_ = sdBox(p - vec3(0.0, E.y - 1.08, E.z), vec3(phw, 0.02, 1.6));
    floor_ = max(floor_, f + 0.04);
    res = opU(res, vec2(floor_, 11.0));
    // seats: pan with a front roll, bolstered back, headrest, rails, lap belt and shoulder harness
    // (each group below is skipped when its bounding box is farther than the nearest surface so far, as outside)
    vec3 sp = vec3(abs(p.x) - abs(E.x), p.y, p.z);
    float sbTop = max(gCab0.y + 0.16, E.y + 0.06);
    if (sdBox(sp - vec3(0.0, 0.5*(E.y - 1.08 + sbTop), E.z + 0.13), vec3(max(gCab0.x, 0.2) + 0.08, 0.5*(sbTop - E.y + 1.08), 0.49)) < res.x) {
      float sw = gCab0.x;
      float seat = sdRoundBox(sp - vec3(0.0, E.y - 0.8, E.z + 0.05), vec3(sw, 0.055, 0.24), 0.05);
      seat = smin(seat, sdCapsule(sp, vec3(-sw + 0.02, E.y - 0.77, E.z - 0.17), vec3(sw - 0.02, E.y - 0.77, E.z - 0.17), 0.05), 0.025);
      vec3 bp = sp - vec3(0.0, E.y - 0.4, E.z + 0.37); bp.yz = rot2(bp.yz, -0.18);   // reclined 10 deg: the top leans aft (+z)
      float bw = mix(min(sw, 0.2), min(sw, 0.145), smoothstep(-0.14, 0.28, bp.y));
      float back = sdRoundBox(bp, vec3(bw, 0.36, 0.05), 0.045);
      back = smin(back, sdRoundBox(vec3(abs(bp.x) - bw + 0.025, bp.y + 0.05, bp.z + 0.03), vec3(0.03, 0.26, 0.07), 0.03), 0.025);   // tapered upper bolsters clear the curved roof
      seat = min(seat, back);
      if (gCab0.y > -50.0) {   // headrest on two posts above the seat back
        vec3 hr = sp - vec3(0.0, gCab0.y, E.z + 0.455); hr.yz = rot2(hr.yz, -0.18);   // tilted with the back
        seat = min(seat, sdRoundBox(hr, vec3(0.11, 0.075, 0.045), 0.035));
        seat = min(seat, sdCapsule(vec3(abs(sp.x) - 0.06, sp.y, sp.z), vec3(0.0, E.y - 0.07, E.z + 0.43), vec3(0.0, gCab0.y - 0.05, E.z + 0.446), 0.008));
      }
      res = opU(res, vec2(seat, 12.0));
      float railX = min(0.15, sw*0.7);
      float rails = sdRoundBox(vec3(abs(sp.x) - railX, sp.y - (E.y - 1.045), sp.z - E.z), vec3(0.012, 0.018, 0.34), 0.004);
      rails = min(rails, sdRoundBox(vec3(abs(sp.x) - railX, sp.y - (E.y - 0.94), sp.z - E.z - 0.05), vec3(0.01, 0.09, 0.012), 0.003));
      res = opU(res, vec2(rails, 60.0));
      vec3 hq = vec3(abs(sp.x) - 0.11, sp.y - (E.y - 0.43), sp.z - (E.z + 0.31)); hq.yz = rot2(hq.yz, -0.18);
      float belt = sdBox(hq, vec3(0.022, 0.33, 0.004));
      belt = min(belt, sdBox(vec3(sp.x, sp.y - (E.y - 0.73), sp.z - (E.z - 0.05)), vec3(sw - 0.01, 0.022, 0.004)));
      res = opU(res, vec2(belt, 69.0));
      res = opU(res, vec2(sdRoundBox(vec3(sp.x, sp.y - (E.y - 0.73), sp.z - (E.z - 0.055)), vec3(0.03, 0.02, 0.006), 0.004), 60.0));   // buckle
    }
    // control yokes: pull moves toward the pilot, roll right turns the yoke clockwise - both yokes alike (each in the
    // pilot's own frame, not a mirror image: the copilot's turns the same way, as the linked controls do)
    {
      float pull = cPitch*0.075, ys = p.x < 0.0 ? -1.0 : 1.0;
      vec3 yp = vec3(-(p.x - ys*abs(E.x)), p.y - (E.y - 0.43), p.z - pz);
      if (sdBox(yp - vec3(0.0, 0.0, 0.17), vec3(0.19, 0.19, 0.2)) < res.x) {
      res = opU(res, vec2(sdCylX(yp.zyx - vec3(0.05, 0.0, 0.0), 0.03, 0.012), 66.0));            // shaft collar
      res = partAt(res, PT_YOKE_SHAFT, vec2(ys, 0.0), p);   // the shaft and the wheel: rigid parts (plane_parts.glsl)
      res = partAt(res, PT_YOKE_WHEEL, vec2(ys, 0.0), p);
      }
    }
    // rudder pedals with toe brakes on metal arms: right rudder pushes the right pedal forward
    {
      // stand clear of the real floor there: in small cabins the belly curves up towards the firewall
      vec3 sP = fusSection(pz + 0.2);
      float kx = clamp((abs(E.x) + 0.1)/max(sP.x - 0.035, 0.01), 0.0, 0.98);
      float floorY = max(E.y - 1.06, sP.z - (sP.y - 0.035)*sqrt(1.0 - kx*kx) + 0.03);
      float pyc = max(E.y - 0.98, floorY + 0.09);
      if (sdBox(vec3(abs(p.x) - abs(E.x), p.y - pyc - 0.075, p.z - pz - 0.15), vec3(0.18, 0.19, 0.21)) < res.x) {
      float ps = p.x < 0.0 ? -1.0 : 1.0;
      res = partAt(res, PT_PEDAL, vec2(ps, p.x - ps*abs(E.x) < 0.0 ? -1.0 : 1.0), p);   // (a rigid part: plane_parts.glsl)
      }
    }
    // footwell wall: closes the space between the floor and the panel's lower edge; the pedals hang from it
    {
      float fy0 = E.y - 1.08, fy1 = E.y - 0.58;
      float fw = sdBox(p - vec3(0.0, 0.5*(fy0 + fy1), pz - 0.05), vec3(phw, 0.5*(fy1 - fy0), 0.04));
      res = opU(res, vec2(max(fw, f + 0.04), 11.0));
    }
    // centre pedestal: trim wheel, fuel selector; throttle (push-pull knobs or levers), mixture, flap lever
    {
      float pw = ck == 0 ? 0.075 : 0.11, ph = 0.22, pd = ck == 0 ? 0.24 : 0.32;  // all pedestals meet the floor; controls keep their original top height
      vec3 pc = vec3(0.0, E.y - 0.84 + (0.22 - ph), pz + 0.06 + pd);
      if (sdBox(p - vec3(0.0, E.y - 0.76, pz + 0.04 + pd), vec3(0.23, 0.35, pd + 0.08)) < res.x) {
      res = opU(res, vec2(sdRoundBox(p - pc, vec3(pw, ph, pd), 0.03), 63.0));
      vec3 tw = p - vec3(pw + 0.004, pc.y + ph*0.2, pc.z + pd*0.35);
      res = opU(res, vec2(sdCylX(tw, 0.075, 0.012), 66.0));
      res = opU(res, vec2(sdRoundCylX((p - vec3(0.0, pc.y + ph + 0.012, pc.z + pd*0.4)).yxz, 0.035, 0.012, 0.004), 66.0));   // fuel selector
      if (ck == 0) {
        res = partAt(res, PT_THR_KNOB, vec2(0.0), p);   // (a rigid part: plane_parts.glsl)
        float mix_ = min(sdCapsule(p, vec3(0.06, E.y - 0.55, pz + 0.04), vec3(0.06, E.y - 0.55, pz + 0.08), 0.005), length(p - vec3(0.06, E.y - 0.55, pz + 0.085)) - 0.018);
        res = opU(res, vec2(mix_, 68.0));
      } else {
        res = partAt(res, PT_THR_LEVER, vec2(p.x < 0.0 ? -1.0 : 1.0, 0.0), p);   // the throttle levers and the flap lever: rigid parts (plane_parts.glsl)
        res = partAt(res, PT_FLAP_LEVER, vec2(0.0), p);
      }
      }
    }
    if (ck == 2) res = opU(res, vec2(sdRoundBox(p - vec3(0.0, gCab1.z, E.z - 0.2), vec3(0.22, 0.03, 0.3), 0.02), 14.0));
    // switch row along the lower panel edge (domain repetition)
    {
      vec3 swp = p - vec3(0.0, E.y - 0.565, pz + 0.05);
      if (sdBox(swp - vec3(0.0, 0.0, 0.01), vec3(phw, 0.03, 0.03)) < res.x) {
      float sw = 0.032; float cell = clamp(floor(swp.x/sw + 0.5), -12.0, 12.0);
      swp.x -= cell*sw;
      float sws = sdRoundBox(swp - vec3(0.0, 0.0, 0.01), vec3(0.006, 0.012, 0.012), 0.003);
      sws = max(sws, abs(p.x) - phw*0.85);
      sws = max(sws, -(abs(p.x) - 0.13));                              // leave the radio stack clear
      res = opU(res, vec2(sws, 13.0));
      }
    }
    // side trim panels with armrests, door handles and map pockets
    {
      float wx = gCab0.w;
      if (sdBox(vec3(abs(p.x) - wx, p.y - (E.y - 0.73), p.z - (E.z - 0.2)), vec3(0.09, 0.36, 0.68)) < res.x) {
      vec3 ap = vec3(abs(p.x) - wx, p.y - (E.y - 0.5), p.z - (E.z - 0.15));
      res = opU(res, vec2(sdRoundBox(ap, vec3(0.05, 0.035, 0.38), 0.02), 12.0));
      float trim = sdRoundBox(vec3(abs(p.x) - wx + 0.02, p.y - (E.y - 0.8), p.z - (E.z - 0.25)), vec3(0.025, 0.26, 0.6), 0.02);
      trim = max(trim, f + 0.045);
      res = opU(res, vec2(trim, 63.0));
      res = opU(res, vec2(sdCapsule(vec3(abs(p.x) - wx + 0.012, p.y - (E.y - 0.42), p.z - (E.z - 0.42)), vec3(0.0), vec3(0.0, 0.0, 0.11), 0.009), 60.0));
      }
    }
    // sun visors folded against the cabin roof
    vec3 vp = vec3(p.x - sign(p.x)*abs(E.x), p.y - gCab1.x, p.z - (E.z - 0.30));
    if (sdBox(vp, vec3(0.16, 0.16, 0.16)) < res.x) {
      vp.xy = rot2(vp.xy, sign(p.x)*gCab1.y);  // fold against the curved roof, rather than through it
      vp.yz = rot2(vp.yz, 0.25);
      res = opU(res, vec2(max(sdRoundBox(vp, vec3(0.13, 0.006, 0.05), 0.004), f + 0.055), 14.0));
    }
    // overhead console: dome light and two map lights (modelled lenses - the cabin's night lighting)
    {
      vec3 oc = p - vec3(0.0, ck == 2 ? gCab1.z - 0.075 : gCab0.z, E.z - 0.05);   // (under the glass cockpit's overhead plate, which hid its lenses)
      if (sdBox(oc, vec3(0.12, 0.06, 0.22)) < res.x) {
      res = opU(res, vec2(sdRoundBox(oc, vec3(0.09, 0.025, 0.18), 0.015), 14.0));
      res = opU(res, vec2(sdRoundCylX((oc + vec3(0.0, 0.024, 0.02)).yxz, 0.04, 0.004, 0.002), 64.0));
      res = opU(res, vec2(sdRoundCylX((vec3(abs(oc.x) - 0.06, oc.y + 0.024, oc.z - 0.12)).yxz, 0.013, 0.004, 0.002), 64.0));
      }
    }
    float compass = sdRoundBox(p - vec3(0.0, E.y - 0.05, pz - 0.05), vec3(0.04, 0.03, 0.03), 0.01);
    res = opU(res, vec2(compass, 66.0));
  }
  if (inside > 0.5 && gModelId == kOspreyModel) res = opU(res, mapOspreyCabinTrim(p));   // the Osprey's cabin trim
  return res;
}
