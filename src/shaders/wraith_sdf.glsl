//! kWraithSDF
//! The XR-40 Wraith airframe distance field: body, pods, bay, turrets, wing, tail, gear.
// ---------------------------------------------------------------- XR-40 Wraith (engine code 6)
// Faceted stealth airframe: diamond-section fuselage with sharp chines, caret intakes, a faceted canopy, a cranked
// diamond wing with a forward-swept trailing edge and elevons, canted all-moving ruddervators, four tilting thruster
// pods on pylons (intake fans, iris nozzles, vectoring vanes, trunnions and hydraulic tilt actuators), a belly bomb
// bay with clamshell doors and the bomb in its cradle, and two laser turrets that drop out of the forward chines.
// gWr (uWr, or a bake state): [0] pod tilt, [1] pod yaw vane, [2] pod thrust, [3] pod pitch vane, [4] fan angle, bay, lasers, stealth,
// [5] elevon, ruddervator, roll surfaces, laser fire, [6] bomb loaded, cloak sweep, weapons armed, -
const float WR_TUR_YC = -0.1 - 0.12*0.0025443;   // the chine's height at the laser turrets (z -5.1; wrSection)
const vec3 WR_POD[4] = vec3[4](vec3(-2.35, -0.08, -3.3), vec3(2.35, -0.08, -3.3), vec3(-2.75, 0.05, 3.45), vec3(2.75, 0.05, 3.45));
float wrOct(vec2 q){ q = abs(q); return max(max(q.x, q.y), (q.x + q.y)*0.70711); }
// fuselage cross-section at z: half width, chine height, ridge height, belly height
void wrSection(float z, out float W, out float yc, out float top, out float bot){
  W = z < -4.4 ? (z + 8.4)*0.30 : (z < 4.5 ? 1.2 + (z + 4.4)*0.012 : 1.31 - (z - 4.5)*0.13);
  yc = -0.1 - 0.12*smoothstep(-5.0, -8.4, z);
  top = yc + min((z + 8.4)*0.16, 0.62) - max(z - 4.0, 0.0)*0.05;
  bot = yc - min((z + 8.4)*0.12, 0.48) + max(z - 5.0, 0.0)*0.06;
}
float wrBody(vec3 p){
  // the cross-section is only meaningful along the body (ahead of the nose its heights go negative, the half-planes
  // flip and it would leave an invisible wall across the nose plane): evaluate it inside, then add the end caps
  float W, yc, top, bot; wrSection(clamp(p.z, -8.3, 7.8), W, yc, top, bot);
  W = max(W, 0.001);
  vec2 q = vec2(abs(p.x), p.y - yc);
  float h = top - yc, hb = yc - bot, Wb = W*0.42;
  vec2 nU = normalize(vec2(h, W)), nL = normalize(vec2(hb, Wb - W));
  float d = max(dot(q - vec2(W, 0.0), nU), dot(q - vec2(W, 0.0), nL));
  d = max(d, bot - p.y);
  float dz = max(-8.4 - p.z, p.z - 7.8);
  return dz > 0.0 ? length(vec2(max(d, 0.0), dz)) : max(d, dz);
}
// The thruster pods' pieces, each in the pod's own frame q (pivot at the origin, exhaust along +z, before the tilt):
// the nacelle, the fan, the iris and the vanes turn with the pod and are rigid parts with meshes of their own
// (plane_parts.glsl PT_WR_*); the bearing housings, the trunnion shafts and the tilt actuators stay with the airframe.
float wrPodR(float z){ return 0.52 - 0.07*smoothstep(-0.2, 1.3, z); }   // the nacelle's taper
// the nacelle (side: +1 the right-hand pods'; i >= 2: a rear pod, with a mount at both ends): shell, intake lip, core
// glow, and the flanged hubs whose bolt circles turn with the tilt
vec2 wrPodShell(vec3 q, int i, float side){
  // faceted nacelle: an octagonal shell that tapers aft, open at both ends
  float R = wrPodR(q.z);
  float oc = wrOct(q.xy);
  float shell = max(oc - R, abs(q.z + 0.03) - 1.32);
  float duct = max(oc - (R - 0.07), max(-(q.z + 1.5), q.z + 0.82));       // intake duct
  duct = min(duct, max(oc - (R - 0.06), max(0.92 - q.z, q.z - 1.5)));     // nozzle bay
  shell = max(shell, -duct);
  vec2 res = vec2(shell, 83.0);
  // stealth intake lip: a sawtooth chevron edge
  res = opU(res, vec2(max(abs(oc - R + 0.035) - 0.035, abs(q.z + 1.33) - 0.025 - 0.02*abs(fract(atan(q.y, q.x)*1.273) - 0.5)), 84.0));
  // turbine core glow deep in the nacelle
  res = opU(res, vec2(max(oc - (R - 0.08), abs(q.z - 0.86) - 0.015), 86.0));
  int nEnd = i >= 2 ? 2 : 1;
  for (int e = 0; e < 2; e++) {
    if (e >= nEnd) break;
    float ox = q.x*(e == 0 ? -side : side);   // distance out from the pod's centre towards this mount
    vec3 fq = vec3(ox - 0.54, q.y, q.z);
    float flange = max(length(fq.yz) - 0.3, abs(fq.x) - 0.018);
    float bs = 6.28318/8.0, ba = atan(fq.z, fq.y); ba = mod(ba + bs*0.5, bs) - bs*0.5;
    vec2 bp = length(fq.yz)*vec2(cos(ba), sin(ba)) - vec2(0.24, 0.0);
    float bolts = max(length(bp) - 0.025, abs(fq.x - 0.022) - 0.014);
    res = opU(res, vec2(min(flange, bolts), 84.0));
  }
  return res;
}
// intake fan: hub and 14 twisted blades, turned by ang
vec2 wrFan(vec3 q, float ang){
  float R = wrPodR(q.z);
  float r = length(q.xy);
  float a = atan(q.y, q.x) + ang;
  float sec = 6.28318/14.0; float aa = mod(a + sec*0.5, sec) - sec*0.5;
  vec2 bp = rot2(vec2(r*aa, q.z + 0.86), 0.65);
  float blade = max(max(abs(bp.x) - 0.11, abs(bp.y) - 0.012), max(r - (R - 0.08), 0.1 - r));
  float hub = sdEllipsoid(q - vec3(0.0, 0.0, -0.86), vec3(0.15, 0.15, 0.24));
  return vec2(min(blade, hub), 85.0);
}
// iris nozzle: ten petals that open with thrust, with gaps between them (one: the petal round +x alone)
float wrPetals(vec3 q, float thr, bool one){
  float r = length(q.xy);
  float zn = clamp((q.z - 0.95)/0.42, 0.0, 1.0);
  float exitR = 0.28 + 0.1*clamp(thr, 0.0, 1.0);
  float coneR = mix(wrPodR(q.z) - 0.05, exitR, zn);
  float ps = 6.28318/10.0; float pa = atan(q.y, q.x);
  if (!one) pa = mod(pa + ps*0.5, ps) - ps*0.5;
  return max(max(abs(r - coneR) - 0.022, abs(q.z - 1.16) - 0.21), abs(r*pa) - coneR*0.27);
}
// vectoring vanes across the jet: three pitch vanes and two yaw vanes, on pivots in the nozzle exit
vec2 wrVanes(vec3 q, float vane, float yawv, float side){
  float zr = wrPodR(q.z) - 0.12;
  vec2 res = vec2(1e5, 0.0);
  for (int k = -1; k <= 1; k++) {
    vec3 vq = q - vec3(0.0, float(k)*0.13, 1.42);
    vq.yz = rot2(vq.yz, -vane*1.4);
    res = opU(res, vec2(sdBox(vq, vec3(zr*0.9 - abs(float(k))*0.06, 0.012, 0.075)), 84.0));
  }
  for (int k = -1; k <= 1; k += 2) {
    vec3 vq = q - vec3(float(k)*0.11, 0.0, 1.5);
    vq.xz = rot2(vq.xz, -yawv*1.4*side);
    res = opU(res, vec2(sdBox(vq, vec3(0.01, zr*0.75, 0.07)), 84.0));
  }
  return res;
}
// one thruster pod (i): its pieces in the pod's frame (left out of the airframe's mesh bake, gPartMode -2), and the
// mounts and the actuator that stay with the airframe
vec2 wrPod(vec3 p, int i, float lim){
  vec3 P = WR_POD[i];
  if (length(p - P) - 2.2 > lim) return vec2(1e5, 0.0);
  float tilt = gWr[0][i], yawv = gWr[1][i], thr = gWr[2][i], vane = gWr[3][i];
  float side = P.x > 0.0 ? 1.0 : -1.0;
  vec3 q = p - P; q.yz = rot2(q.yz, -tilt);
  vec2 res = vec2(1e5, 0.0);
  if (gPartMode != -2) {
    res = wrPodShell(q, i, side);
    res = opU(res, wrFan(q, gWr[4].x*side));
    res = opU(res, vec2(wrPetals(q, thr, false), 84.0));
    res = opU(res, wrVanes(q, vane, yawv, side));
  }
  // rotating mounts: a bearing housing fixed to the pylon (front pods) or to both walls of the wing well (rear pods)
  // and a trunnion shaft (the flanged hub on the pod is the nacelle's)
  vec3 tq = p - P;
  int nEnd = i >= 2 ? 2 : 1;
  for (int e = 0; e < 2; e++) {
    if (e >= nEnd) break;
    float ox = tq.x*(e == 0 ? -side : side);
    float rr2 = length(tq.yz);
    float housing = max(rr2 - 0.19, abs(ox - 0.69) - 0.09);
    housing = min(housing, max(rr2 - 0.235, abs(ox - 0.615) - 0.02));             // collar lip facing the pod
    housing = max(housing, -max(rr2 - 0.1, abs(ox - 0.6) - 0.05));                // bore for the shaft
    res = opU(res, vec2(housing, 80.0));
    res = opU(res, vec2(max(rr2 - 0.09, abs(ox - 0.58) - 0.06), 92.0));          // trunnion shaft
  }
  // hydraulic tilt actuator: barrel on the pylon (front) or the wing root (rear), chrome rod to a lug on the pod; a
  // rigid part (plane_parts.glsl wrActEnds: it stretches along its axis as the pod tilts)
  if (gPartMode != -2) {
    vec3 anchor, lug; wrActEnds(i, tilt, anchor, lug);
    vec3 ad = lug - anchor; float al = max(length(ad), 1e-4); vec3 axis = ad/al;
    res = opU(res, vec2(min(sdCapsule(p, anchor, anchor + axis*0.65*al, 0.07), sdCapsule(p, anchor + axis*0.6*al, lug, 0.035)), 92.0));
  }
  return res;
}
vec2 mapWraith(vec3 p){
  float gear = gPS.x, inside = gPS.w;
  if (inside > 0.5) return mapWraithCockpit(p);
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float sgn = p.x > 0.0 ? 1.0 : -1.0;
  float body = wrBody(p);
  float W, yc, top, bot; wrSection(p.z, W, yc, top, bot);
  vec2 res = vec2(body, 80.0);
  // caret intakes under the chines
  {
    vec3 iq = vec3(ap.x - 0.95, p.y - (yc - 0.24), p.z + 1.6);
    float face = iq.z + 0.75 - iq.x*0.6;
    float cut = max(sdBox(iq, vec3(0.26, 0.15, 0.9)), face);
    res.x = max(res.x, -cut);
    res = opU(res, vec2(max(sdBox(iq - vec3(0.0, 0.0, 0.25), vec3(0.24, 0.13, 0.7)), -face - 0.04), 93.0));
  }
  // bomb bay: clamshell doors hinged at the outer edges, cavity with frames, the bomb in its cradle
  float bay = gWr[4].y;
  {
    float by = bot + 0.005;
    vec3 cq = vec3(p.x, p.y - (by + 0.24), p.z - 0.1);
    float cav = sdBox(cq, vec3(0.52, 0.25, 1.66));
    if (-cav > res.x) res = vec2(-cav, 88.0); else res.x = max(res.x, -cav);
    // frames across the bay roof
    res = opU(res, vec2(max(sdBox(vec3(p.x, p.y - (by + 0.47), mod(p.z + 1.56, 0.52) - 0.26), vec3(0.52, 0.03, 0.025)), abs(p.z - 0.1) - 1.66), 88.0));
    vec3 dq = vec3(ap.x - 0.53, p.y - by, p.z - 0.1);
    vec2 dr = rot2(dq.xy, -bay*1.75);
    // (the doors and the bomb are rigid parts with meshes of their own: the airframe's bake leaves them out)
    if (gPartMode != -2) res = opU(res, vec2(sdRoundBox(vec3(dr.x + 0.265, dr.y + 0.014, dq.z), vec3(0.265, 0.014, 1.66), 0.004), 81.0));
    float bl = gWr[6].x;
    vec3 bc = vec3(0.0, by + 0.27, 0.1);
    if (bl > 0.01 && gPartMode != -2) res = opU(res, vec2(length(p - bc) - 0.29*bl, 89.0));
    // the cradle stays when the bomb has gone
    float cr = min(sdCapsule(ap, vec3(0.0, by + 0.5, -0.2), vec3(0.22, bc.y + 0.12, -0.2), 0.03), sdCapsule(ap, vec3(0.0, by + 0.5, 0.4), vec3(0.22, bc.y + 0.12, 0.4), 0.03));
    res = opU(res, vec2(cr, 92.0));
  }
  // laser turrets: a hatch opens in each forward chine and the emitter drops out on its arm
  float las = gWr[4].z;
  if (length(ap - vec3(0.95, -0.45, -5.1)) < 2.0 + res.x) {
    vec3 lq = ap - vec3(0.95, WR_TUR_YC - 0.18, -5.1);   // (one frame for the whole turret: its parts are rigid)
    float well = sdBox(lq, vec3(0.2, 0.17, 0.5));
    res.x = max(res.x, -well);   // (always open: the hatch, a rigid part, closes over it)
    if (gPartMode != -2) {   // (the hatch, the emitter and its arm: rigid parts with meshes of their own)
    vec3 hq = lq - vec3(0.2, -0.17, 0.0); hq.xy = rot2(hq.xy, las*1.9);
    res = opU(res, vec2(sdRoundBox(hq + vec3(0.2, -0.012, 0.0), vec3(0.2, 0.012, 0.5), 0.004), 81.0));
    vec3 tq = lq - vec3(0.0, -0.38*las, -0.25*las);
    float house = sdRoundBox(tq, vec3(0.13, 0.1, 0.32), 0.035);
    float arm = sdCapsule(lq, vec3(0.0, 0.05, 0.1), vec3(0.0, -0.38*las + 0.05, -0.25*las + 0.1), 0.045);
    res = opU(res, vec2(min(house, arm), 90.0));
    vec3 bq = tq - vec3(0.0, -0.02, -0.32);
    float barrel = sdCapsule(bq, vec3(0.0), vec3(0.0, 0.0, -0.55 - 0.15*las), 0.045);
    float rings = max(abs(length(bq.xy) - 0.065) - 0.018, abs(mod(bq.z + 0.05, 0.11) - 0.055) - 0.018);
    rings = max(rings, max(bq.z - 0.0, -bq.z - 0.45));
    res = opU(res, vec2(min(barrel, rings), 92.0));
    res = opU(res, vec2(length(bq - vec3(0.0, 0.0, -0.62 - 0.15*las)) - 0.05, 91.0));
    }
  }
  // faceted canopy
  {
    vec3 cq = p - vec3(0.0, top - 0.05, -4.7);
    float can = max(sdEllipsoid(cq, vec3(0.56, 0.4, 1.65)), abs(cq.x)*0.82 + cq.y*0.58 - 0.2);
    can = max(can, -cq.y);
    res = opU(res, vec2(can, 82.0));
    res = opU(res, vec2(max(abs(can) - 0.012, abs(cq.x) - 0.025), 84.0));   // centre frame
  }
  // cranked diamond wing with a forward-swept trailing edge and elevons
  {
    float s = ap.x - 1.0, t = p.y - (yc - 0.02 - s*0.012), c = p.z + 3.0;
    float defl = -gWr[5].x*0.45 - gWr[5].z*sgn*0.45;
    float wing = sdPanel(s, c, t, 5.2, 8.2, 1.35, 4.7, 0.035, 0.8, 1.9, 5.0);
    float elev = gPartMode == -2 ? 1e9 : sdSurface(s, c, t, 5.2, 8.2, 1.35, 4.7, 0.035, 0.8, 1.9, 5.0, defl, 0.0);   // (a rigid part: plane_parts.glsl)
    // nacelle wells: the rear pods swing through slots in the wing (just wider than the pod, as long as its swing)
    vec3 wq = vec3(ap.x - 2.75, p.y - 0.05, p.z - 3.45), fwq = vec3(ap.x - 2.35, p.y + 0.08, p.z + 3.3);
    float well = min(max(abs(wq.x) - 0.6, length(wq.yz) - 1.62), max(abs(fwq.x) - 0.6, length(fwq.yz) - 1.62));  // include the nozzle vanes' swept envelope
    float rim = max(abs(well) - 0.03, wing - 0.025);   // titanium frame around each well
    wing = max(wing, -well); elev = max(elev, -well);
    float w2 = min(wing, elev);
    float d = smin(res.x, w2, 0.12);
    res = vec2(d, w2 < res.x ? (wing < elev ? 80.0 : 81.0) : res.y);
    res = opU(res, vec2(rim, 84.0));
  }
  // canted V-tail: fixed fins with hinged ruddervators on the aft third of the chord (pitch and yaw mixed), which
  // swing on their hinge line instead of the whole fin turning
  {
    vec3 q = ap - vec3(1.05, top - 0.05, 4.4); q.xy = rot2(q.xy, 0.72);
    // (the surface's thickness axis points outboard and down: pulling back swings both trailing edges up and inboard,
    // right rudder swings the right one out and the left one in)
    float dv = -gWr[5].x*0.35 + gWr[5].y*sgn*0.35;
    float fs = q.y + 0.75, span = 3.05, fsw = 1.6*3.05/2.3, hinge = 0.68, s0 = 0.14, s1 = span*0.95;
    float fin = max(sdPanel(fs, q.z, q.x, span, 2.6, 1.1, fsw, 0.04, hinge, s0, s1), -fs);   // root buried in the body
    float rv = gPartMode == -2 ? 1e9 : sdSurface(fs, q.z, q.x, span, 2.6, 1.1, fsw, 0.04, hinge, s0, s1, -dv*1.4, 0.0);   // (a rigid part)
    res = opU(res, vec2(min(fin, rv), 81.0));
  }
  // pylons for the pods: faceted struts from the airframe to each trunnion
  {
    float fpy = sdBox(vec3(ap.x - 1.42, p.y - (-0.1), p.z + 3.3), vec3(0.24, 0.08, 0.36));
    fpy = max(fpy, (abs(p.y + 0.1) + abs(p.z + 3.3)*0.5) - 0.2);                         // faceted
    float rpy = sdBox(vec3(ap.x - 1.75, p.y - 0.0, p.z - 3.45), vec3(0.4, 0.1, 0.45));
    res = opU(res, vec2(min(fpy, rpy), 80.0));
  }
  // thruster pods (the near side's front and rear pod)
  int fi = p.x > 0.0 ? 1 : 0;
  res = opU(res, wrPod(p, fi, res.x));
  res = opU(res, wrPod(p, fi + 2, res.x));
  // edge LEDs along the chines and the wing leading edges (cloaking field emitters)
  float led = sdCapsule(ap, vec3(1.18, yc + 0.0, -4.0), vec3(0.15, -0.2, -8.0), 0.018);
  led = min(led, sdCapsule(ap, vec3(1.25, -0.12, -2.7), vec3(6.1, -0.18, 1.95), 0.016));
  res = opU(res, vec2(led, 87.0));
  // (the retractable tricycle gear: plane_sdf.glsl jtGear, the XR-30's in this airframe's bays, added by mapPlaneBody)
  return res;
}
// The XR-40's rigid parts at rest, each in its own frame (plane_parts.glsl partPose places them): the right-hand pods'
// nacelles, a fan, the vanes, one iris petal (round +x, at half thrust), a bay door, the bomb, the right turret's hatch,
// emitter, barrel tip and arm, and the right elevon and ruddervator (body space)
vec2 wrPartField(int k, vec3 l){
  if (k == PT_WR_PODF) return wrPodShell(l, 1, 1.0);
  if (k == PT_WR_PODR) return wrPodShell(l, 3, 1.0);
  if (k == PT_WR_FAN) return wrFan(l, 0.0);
  if (k == PT_WR_VANEC) return vec2(sdBox(l, vec3(0.33*0.9, 0.012, 0.075)), 84.0);
  if (k == PT_WR_VANEO) return vec2(sdBox(l, vec3(0.33*0.9 - 0.06, 0.012, 0.075)), 84.0);
  if (k == PT_WR_VANEY) return vec2(sdBox(l, vec3(0.01, 0.33*0.75, 0.07)), 84.0);
  if (k == PT_WR_PETAL) return vec2(wrPetals(l, 0.5, true), 84.0);
  if (k == PT_WR_DOOR) return vec2(sdRoundBox(vec3(l.x + 0.265, l.y + 0.014, l.z), vec3(0.265, 0.014, 1.66), 0.004), 81.0);
  if (k == PT_WR_BOMB) return vec2(length(l) - 0.29, 89.0);
  if (k == PT_WR_HATCH) return vec2(sdRoundBox(l + vec3(0.2, -0.012, 0.0), vec3(0.2, 0.012, 0.5), 0.004), 81.0);
  if (k == PT_WR_TURRET || k == PT_WR_MUZZLE) {   // (the turret at full reach; the muzzle: the barrel's sliding tip)
    vec3 bq = l - vec3(0.0, -0.02, -0.32);
    if (k == PT_WR_MUZZLE) return opU(vec2(sdCapsule(bq, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, -0.7), 0.045), 92.0), vec2(length(bq - vec3(0.0, 0.0, -0.77)) - 0.05, 91.0));
    float barrel = sdCapsule(bq, vec3(0.0), vec3(0.0, 0.0, -0.55), 0.045);
    float rings = max(abs(length(bq.xy) - 0.065) - 0.018, abs(mod(bq.z + 0.05, 0.11) - 0.055) - 0.018);
    rings = max(rings, max(bq.z - 0.0, -bq.z - 0.45));
    return opU(vec2(sdRoundBox(l, vec3(0.13, 0.1, 0.32), 0.035), 90.0), vec2(min(barrel, rings), 92.0));
  }
  if (k == PT_WR_ARM) return vec2(sdCapsule(l, vec3(0.0), vec3(0.0, -0.38, -0.25), 0.045), 90.0);
  if (k == PT_WR_ACT) return vec2(min(sdCapsule(l, vec3(0.0), vec3(0.0, 0.0, 0.65), 0.07), sdCapsule(l, vec3(0.0, 0.0, 0.6), vec3(0.0, 0.0, 1.0), 0.035)), 92.0);
  if (k == PT_WR_ELEVON) {
    float s = l.x - 1.0, t = l.y - (-0.1 - 0.02 - s*0.012), c = l.z + 3.0;
    float elev = sdSurface(s, c, t, 5.2, 8.2, 1.35, 4.7, 0.035, 0.8, 1.9, 5.0, 0.0, 0.0);
    vec3 wq = vec3(l.x - 2.75, l.y - 0.05, l.z - 3.45), fwq = vec3(l.x - 2.35, l.y + 0.08, l.z + 3.3);
    float well = min(max(abs(wq.x) - 0.6, length(wq.yz) - 1.62), max(abs(fwq.x) - 0.6, length(fwq.yz) - 1.62));
    return vec2(max(elev, -well), 81.0);
  }
  if (k == PT_WR_RUDV) {
    float W, yc, top, bot; wrSection(l.z, W, yc, top, bot);
    vec3 q = l - vec3(1.05, top - 0.05, 4.4); q.xy = rot2(q.xy, 0.72);
    float fs = q.y + 0.75, span = 3.05, fsw = 1.6*3.05/2.3;
    return vec2(sdSurface(fs, q.z, q.x, span, 2.6, 1.1, fsw, 0.04, 0.68, 0.14, span*0.95, 0.0, 0.0), 81.0);
  }
  return vec2(1e9, 0.0);
}
