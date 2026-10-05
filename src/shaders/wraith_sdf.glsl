//! kWraithSDF
//! The XR-11 Wraith airframe distance field: body, pods, bay, turrets, wing, tail, gear.
// ---------------------------------------------------------------- XR-11 Wraith (engine code 6)
// Faceted stealth airframe: diamond-section fuselage with sharp chines, caret intakes, a faceted canopy, a cranked
// diamond wing with a forward-swept trailing edge and elevons, canted all-moving ruddervators, four tilting thruster
// pods on pylons (intake fans, iris nozzles, vectoring vanes, trunnions and hydraulic tilt actuators), a belly bomb
// bay with clamshell doors and the bomb in its cradle, and two laser turrets that drop out of the forward chines.
// uWr: [0] pod tilt, [1] pod yaw vane, [2] pod thrust, [3] pod pitch vane, [4] fan angle, bay, lasers, stealth,
// [5] elevon, ruddervator, roll surfaces, laser fire, [6] bomb loaded, cloak sweep, weapons armed, -
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
// one thruster pod (i), evaluated in its own frame: pivot at the origin, exhaust along +z
vec2 wrPod(vec3 p, int i, float lim){
  vec3 P = WR_POD[i];
  if (length(p - P) - 2.2 > lim) return vec2(1e5, 0.0);
  float tilt = uWr[0][i], yawv = uWr[1][i], thr = uWr[2][i], vane = uWr[3][i];
  float side = P.x > 0.0 ? 1.0 : -1.0;
  vec3 q = p - P; q.yz = rot2(q.yz, -tilt);
  // faceted nacelle: an octagonal shell that tapers aft, open at both ends
  float R = 0.52 - 0.07*smoothstep(-0.2, 1.3, q.z);
  float oc = wrOct(q.xy);
  float shell = max(oc - R, abs(q.z + 0.03) - 1.32);
  float duct = max(oc - (R - 0.07), max(-(q.z + 1.5), q.z + 0.82));       // intake duct
  duct = min(duct, max(oc - (R - 0.06), max(0.92 - q.z, q.z - 1.5)));     // nozzle bay
  shell = max(shell, -duct);
  vec2 res = vec2(shell, 83.0);
  // stealth intake lip: a sawtooth chevron edge
  res = opU(res, vec2(max(abs(oc - R + 0.035) - 0.035, abs(q.z + 1.33) - 0.025 - 0.02*abs(fract(atan(q.y, q.x)*1.273) - 0.5)), 84.0));
  // intake fan: hub and 14 twisted blades turning with the spool
  float r = length(q.xy);
  float a = atan(q.y, q.x) + uWr[4].x*(side > 0.0 ? 1.0 : -1.0);
  float sec = 6.28318/14.0; float aa = mod(a + sec*0.5, sec) - sec*0.5;
  vec2 bp = rot2(vec2(r*aa, q.z + 0.86), 0.65);
  float blade = max(max(abs(bp.x) - 0.11, abs(bp.y) - 0.012), max(r - (R - 0.08), 0.1 - r));
  float hub = sdEllipsoid(q - vec3(0.0, 0.0, -0.86), vec3(0.15, 0.15, 0.24));
  res = opU(res, vec2(min(blade, hub), 85.0));
  // turbine core glow deep in the nacelle
  res = opU(res, vec2(max(oc - (R - 0.08), abs(q.z - 0.86) - 0.015), 86.0));
  // iris nozzle: ten petals that open with thrust, with gaps between them
  {
    float zn = clamp((q.z - 0.95)/0.42, 0.0, 1.0);
    float exitR = 0.28 + 0.1*clamp(thr, 0.0, 1.0);
    float coneR = mix(R - 0.05, exitR, zn);
    float ps = 6.28318/10.0; float pa = mod(atan(q.y, q.x) + ps*0.5, ps) - ps*0.5;
    float pet = max(max(abs(r - coneR) - 0.022, abs(q.z - 1.16) - 0.21), abs(r*pa) - coneR*0.27);
    res = opU(res, vec2(pet, 84.0));
  }
  // vectoring vanes across the jet: three pitch vanes and two yaw vanes, on pivots in the nozzle exit
  {
    float zr = R - 0.12;
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
  }
  // rotating mounts: a bearing housing fixed to the pylon (front pods) or to both walls of the wing well (rear pods),
  // a trunnion shaft, and a flanged hub on the pod whose bolt circle turns with the tilt
  vec3 tq = p - P;
  int nEnd = i >= 2 ? 2 : 1;
  for (int e = 0; e < 2; e++) {
    if (e >= nEnd) break;
    float ox = tq.x*(e == 0 ? -side : side);   // distance out from the pod's centre towards this mount
    float rr2 = length(tq.yz);
    float housing = max(rr2 - 0.19, abs(ox - 0.69) - 0.09);
    housing = min(housing, max(rr2 - 0.235, abs(ox - 0.615) - 0.02));             // collar lip facing the pod
    housing = max(housing, -max(rr2 - 0.1, abs(ox - 0.6) - 0.05));                // bore for the shaft
    res = opU(res, vec2(housing, 80.0));
    res = opU(res, vec2(max(rr2 - 0.09, abs(ox - 0.58) - 0.06), 92.0));          // trunnion shaft
    vec3 fq = vec3(ox - 0.54, q.y, q.z);                                           // pod frame: turns with the tilt
    float flange = max(length(fq.yz) - 0.3, abs(fq.x) - 0.018);
    float bs = 6.28318/8.0, ba = atan(fq.z, fq.y); ba = mod(ba + bs*0.5, bs) - bs*0.5;
    vec2 bp = length(fq.yz)*vec2(cos(ba), sin(ba)) - vec2(0.24, 0.0);
    float bolts = max(length(bp) - 0.025, abs(fq.x - 0.022) - 0.014);
    res = opU(res, vec2(min(flange, bolts), 84.0));
  }
  // hydraulic tilt actuator: barrel on the pylon (front) or the wing root (rear), chrome rod to a lug on the pod
  vec3 anchor = P + (i < 2 ? vec3(-side*0.85, 0.05, -0.32) : vec3(-side*0.8, 0.0, -1.0));
  vec3 lugL = vec3(-side*0.42, 0.36, -0.75); lugL.yz = rot2(lugL.yz, tilt);
  vec3 lug = P + lugL;
  vec3 ad = lug - anchor; float al = length(ad);
  vec3 axis = ad/max(al, 1e-4); float barrel = min(0.6, al*0.65);
  float act = min(sdCapsule(p, anchor, anchor + axis*barrel, 0.07), sdCapsule(p, anchor + axis*max(barrel - 0.05, 0.0), lug, 0.035));
  res = opU(res, vec2(act, 92.0));
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
  float bay = uWr[4].y;
  {
    float by = bot + 0.005;
    vec3 cq = vec3(p.x, p.y - (by + 0.24), p.z - 0.1);
    float cav = sdBox(cq, vec3(0.52, 0.25, 1.66));
    if (-cav > res.x) res = vec2(-cav, 88.0); else res.x = max(res.x, -cav);
    // frames across the bay roof
    res = opU(res, vec2(max(sdBox(vec3(p.x, p.y - (by + 0.47), mod(p.z + 1.56, 0.52) - 0.26), vec3(0.52, 0.03, 0.025)), abs(p.z - 0.1) - 1.66), 88.0));
    vec3 dq = vec3(ap.x - 0.53, p.y - by, p.z - 0.1);
    vec2 dr = rot2(dq.xy, -bay*1.75);
    float door = sdRoundBox(vec3(dr.x + 0.265, dr.y + 0.014, dq.z), vec3(0.265, 0.014, 1.66), 0.004);
    res = opU(res, vec2(door, 81.0));
    float bl = uWr[6].x;
    if (bl > 0.01) {
      vec3 bc = vec3(0.0, by + 0.27, 0.1);
      res = opU(res, vec2(length(p - bc) - 0.29*bl, 89.0));
      float cr = min(sdCapsule(ap, vec3(0.0, by + 0.5, -0.2), vec3(0.22, bc.y + 0.12, -0.2), 0.03), sdCapsule(ap, vec3(0.0, by + 0.5, 0.4), vec3(0.22, bc.y + 0.12, 0.4), 0.03));
      res = opU(res, vec2(cr, 92.0));
    }
  }
  // laser turrets: a hatch opens in each forward chine and the emitter drops out on its arm
  float las = uWr[4].z;
  if (length(ap - vec3(0.95, -0.45, -5.1)) < 2.0 + res.x) {
    vec3 lq = ap - vec3(0.95, yc - 0.18, -5.1);
    float well = sdBox(lq, vec3(0.2, 0.17, 0.5));
    if (las > 0.02) res.x = max(res.x, -well);
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
    float defl = -uWr[5].x*0.45 - uWr[5].z*sgn*0.45;
    float wing = sdPanel(s, c, t, 5.2, 8.2, 1.35, 4.7, 0.035, 0.8, 1.9, 5.0);
    float elev = sdSurface(s, c, t, 5.2, 8.2, 1.35, 4.7, 0.035, 0.8, 1.9, 5.0, defl, 0.0);
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
    float dv = -uWr[5].x*0.35 + uWr[5].y*sgn*0.35;
    float fs = q.y + 0.75, span = 3.05, fsw = 1.6*3.05/2.3, hinge = 0.68, s0 = 0.14, s1 = span*0.95;
    float fin = max(sdPanel(fs, q.z, q.x, span, 2.6, 1.1, fsw, 0.04, hinge, s0, s1), -fs);   // root buried in the body
    float rv = sdSurface(fs, q.z, q.x, span, 2.6, 1.1, fsw, 0.04, hinge, s0, s1, -dv*1.4, 0.0);
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
  // retractable tricycle gear
  {   // gear bays: mains outboard, nose bay with twin doors
    vec4 G0 = gM[18];
    float open = smoothstep(0.0, 0.2, gear);
    res = gearBay(ap, res, vec3(G0.x, -0.21, G0.z), vec2(0.2, 0.46), 0.7, 0.03, open);    // skin heights measured
    res = gearBay(p, res, vec3(0.0, -0.43, G0.w), vec2(0.24, 0.42), 0.7, 0.05, open);
  }
  if (gear > 0.06) {
    vec4 G0 = gM[18], G1 = gM[19];
    float gh = G1.x, wr = 0.38, lift = (1.0 - gear)*(gh - 0.19);   // wheels fold up flush with the belly (skin at -0.21)
    vec3 wc = vec3(G0.x, -gh + wr + lift, G0.z);
    float legs = sdCapsule(ap, vec3(G0.x*0.8, -0.3, G0.z), wc + vec3(-0.1, 0.05, 0.0), 0.07);
    float tyres = sdRoundCylX(ap - wc, wr, 0.13, 0.06);
    vec3 nc = vec3(0.0, -gh + 0.33 + lift, G0.w);
    legs = min(legs, sdCapsule(p, vec3(0.0, -0.35, G0.w), nc + vec3(0.0, 0.1, 0.0), 0.06));
    tyres = min(tyres, sdRoundCylX(vec3(abs(p.x) - 0.1, p.y, p.z) - vec3(0.0, nc.y, nc.z), 0.33, 0.07, 0.04));
    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
  }
  return res;
}
