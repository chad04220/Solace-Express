//! kRaytraceWraith
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
    float dv = uWr[5].x*0.35 + uWr[5].y*sgn*0.35;
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
// XR-11 surfaces (PBR texture sets): radar-absorbent faceted skin with sawtooth panel seams, smoked gold canopy,
// heat-tinted titanium nozzles and vanes, glowing turbine cores, violet cloak-emitter strips, a dark bay, the
// dark-energy bomb, laser housings and their emitter lenses, chrome actuator rods
void shadeWraith(inout Mat m, int mid, vec3 lp, vec3 ln, float t){
  vec3 nT; vec4 tx;
  float pulse = 0.75 + 0.25*sin(uTime*2.5);
  if (mid == 80 || mid == 81 || mid == 83) {
    tx = triSample(lp, ln, M_PAINT, 0.6, nT);
    vec3 base = gColBase*(0.85 + 0.3*tx.r);
    // radar-absorbent coating: matte charcoal with a faint iridescent sheen and fine tile seams
    float tile = max(abs(fract(lp.x*1.6 + lp.y*0.4) - 0.5), abs(fract(lp.z*1.1) - 0.5));
    vec2 sz = vec2(lp.z*0.9 + abs(lp.x)*0.9, lp.z*0.9 - abs(lp.x)*0.9);       // sawtooth (chevron) panel lines
    float saw = min(abs(fract(sz.x) - 0.5), abs(fract(sz.y) - 0.5));
    m.alb = base; m.rough = mix(0.5, 0.72, tx.a); m.metal = 0.15; m.nrm = nT;
    if (tile > 0.49) m.alb *= 0.75;
    if (saw < 0.008 && t < 80.0 && abs(fract(lp.z*0.25) - 0.5) < 0.2) m.alb *= 0.6;   // sawtooth access-panel seams
    if (mid == 81) { m.alb *= 0.82; m.rough = 0.62; }                        // control surfaces: slightly darker
    if (mid == 83) { m.alb = base*0.9; m.metal = 0.3; m.rough = 0.45; }       // pod shells
    if (abs(lp.x) < 0.04 && ln.y > 0.6) m.alb = mix(m.alb, gColStripe*0.3, 0.6); // spine stripe
  }
  else if (mid == 82) { m.alb = vec3(0.28, 0.2, 0.08); m.metal = 0.95; m.rough = 0.08; }   // smoked gold film
  else if (mid == 84) {
    tx = triSample(lp, ln, M_METAL, 0.8, nT); m.alb = tx.rgb*vec3(0.24, 0.23, 0.25); m.metal = 0.85; m.rough = clamp(tx.a*0.7, 0.18, 0.55); m.nrm = nT;
    float heat = clamp(uWr[2].x + uWr[2].y + uWr[2].z + uWr[2].w, 0.0, 4.0)*0.25;
    m.alb = mix(m.alb, vec3(0.2, 0.12, 0.24), 0.45*heat);                     // heat-blued titanium
  }
  else if (mid == 85) { tx = triSample(lp, ln, M_METAL, 1.4, nT); m.alb = tx.rgb*vec3(0.4, 0.41, 0.43); m.metal = 1.0; m.rough = 0.25; m.nrm = nT; }
  else if (mid == 86) { float th = clamp((uWr[2].x + uWr[2].y + uWr[2].z + uWr[2].w)*0.25, 0.0, 1.5);
    m.alb = vec3(0.02); m.emit = mix(vec3(0.35, 0.3, 1.0), vec3(0.95, 0.6, 1.0), th)*(0.4 + 9.0*th*th); }
  else if (mid == 87) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = gColStripe*(1.0 + 2.0*uNight)*pulse*(1.0 + 3.0*uWr[4].w); }
  else if (mid == 88) { tx = triSample(lp, ln, M_METAL, 0.5, nT); m.alb = tx.rgb*vec3(0.07, 0.07, 0.08); m.metal = 0.6; m.rough = 0.5; m.nrm = nT;
    if (abs(fract(lp.z*2.0) - 0.5) < 0.03) m.emit = gColStripe*0.4*uWr[4].y; }                       // bay lights when open
  else if (mid == 89) {   // dark-energy bomb: black glassy core, violet plasma veins crawling over it
    vec3 bc = lp - vec3(0.0, -0.3, 0.1);
    float vein = vnoise3(bc*9.0 + vec3(0.0, uTime*2.0, 0.0)) + 0.5*vnoise3(bc*21.0 - vec3(uTime*3.0));
    m.alb = vec3(0.005); m.rough = 0.05; m.metal = 0.0;
    m.emit = vec3(0.55, 0.15, 1.0)*pow(smoothstep(0.75, 1.15, vein), 2.0)*6.0 + vec3(0.2, 0.7, 1.0)*pow(smoothstep(1.05, 1.3, vein), 3.0)*8.0;
  }
  else if (mid == 90) { tx = triSample(lp, ln, M_METAL, 0.9, nT); m.alb = tx.rgb*vec3(0.12, 0.12, 0.13); m.metal = 0.8; m.rough = 0.4; m.nrm = nT;
    if (abs(fract(lp.z*14.0) - 0.5) < 0.08) m.alb *= 0.5; }                                           // cooling slots
  else if (mid == 91) { float f = uWr[5].w; m.alb = vec3(0.1, 0.02, 0.02); m.rough = 0.02; m.emit = vec3(1.0, 0.15, 0.25)*(0.6*uWr[4].z + 25.0*f); }
  else if (mid == 92) { m.alb = vec3(0.75, 0.76, 0.78); m.metal = 1.0; m.rough = 0.12; }
  else if (mid == 93) { m.alb = vec3(0.01); m.rough = 0.9; }
}
// four round plasma jets, one per pod (thrust fractions in uWr[2]): a white-cyan core in a violet sheath that
// swirls slowly, with bright standing shock rings; longer, hotter and tighter-ringed in boost
vec3 plumeRound(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, float sp, float ab, float jit){
  float L = mix(3.0, 6.0, sp) + 8.0*ab;
  vec3 c = o + ax*(L*0.5); float br = L*0.5 + 0.7;
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return vec3(0.0);
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return vec3(0.0);
  vec3 bx = normalize(cross(ax, abs(ax.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), by = cross(ax, bx);
  float spacing = 0.75 - 0.2*ab;
  float dt = (t1 - t0)/28.0; vec3 acc = vec3(0.0);
  for (int i = 0; i < 28; i++) {
    COST(3);
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax); if (x < -0.05 || x > L) continue;
    float u = max(x, 0.0)/L;
    vec3 rq = q - ax*x; float rr = length(rq);
    float w = mix(0.33, 0.2, u)*(1.0 + 0.9*ab*u);
    float r = rr/w; if (r > 2.2) continue;
    float ang = atan(dot(rq, by), dot(rq, bx));
    float helix = 0.5 + 0.5*sin(ang*3.0 - x*5.0 + uTime*24.0);
    float flick = vnoise(vec2(x*3.0 - uTime*60.0, ang*2.0 + rr*6.0));
    float cell = fract(x/spacing);
    float ring = exp(-pow((cell - 0.5)/0.06, 2.0))*exp(-pow((r - 0.5)/0.2, 2.0))*exp(-x/spacing*0.35)*step(0.4, x/spacing);
    float core = exp(-r*r*7.0)*pow(1.0 - u, 0.6);
    float sheath = exp(-pow((r - 0.7)/0.3, 2.0))*(0.55 + 0.45*helix)*(0.6 + 0.4*flick);
    vec3 shCol = mix(vec3(0.42, 0.22, 1.0), vec3(0.95, 0.3, 0.85), smoothstep(0.3, 1.0, u));
    vec3 e = vec3(0.75, 0.92, 1.0)*core*(7.0 + 5.0*ab) + shCol*sheath*(2.6 + 1.6*ab) + vec3(0.8, 0.9, 1.0)*ring*(4.0 + 6.0*ab);
    acc += e*smoothstep(-0.05, 0.08, x)*pow(1.0 - u, 1.2)*smoothstep(1.0, 4.0, t0 + (float(i) + jit)*dt)*dt;
    gPlumeT *= exp(-(sheath*0.35 + core*0.5)*sp*dt);
  }
  return acc*(0.5 + 0.7*sp);
}
vec3 wraithPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (gFlame.x < 0.02) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  vec3 col = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    float a = uWr[0][i] + uWr[3][i], y = uWr[1][i], a0 = uWr[0][i];
    vec3 ax = normalize(vec3(-sin(y), -sin(a)*cos(y), cos(a)*cos(y)));
    vec3 o = WR_POD[i] + vec3(0.0, -sin(a0), cos(a0))*1.5;
    float th = clamp(uWr[2][i], 0.0, 1.6);
    col += plumeRound(lo, ld, tmax, o, ax, clamp(th*1.3, 0.0, 1.0), gFlame.y, jit);
  }
  return col/(1.0 + max(col.r, max(col.g, col.b))*0.15);
}
// Transonic vapour cone (Prandtl-Glauert condensation): near Mach 1 in humid air the pressure drop behind the shock
// condenses a shell of fog around the airframe. A sharp leading edge at the shock, a bell that flares and thins aft,
// streaky and flickering, lit by the sun and the sky.
vec3 vaporCone(vec3 col, vec3 ro, vec3 rd, float tmax, float jit){
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float z0 = uVapor.y, R0 = uVapor.z, Lc = uVapor.w;
  vec3 c = vec3(0.0, 0.0, z0 + Lc*0.5); float br = length(vec2(R0*1.8, Lc*0.5 + 0.5));
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return col;
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return col;
  vec3 sunB = inv*uSunDir;
  vec3 lit = uSunCol*(0.75 + 0.5*pow(max(dot(rd, uSunDir), 0.0), 6.0))*max(uSunDir.y + 0.1, 0.0)*1.3 + skyColor(vec3(0.0, 1.0, 0.0))*1.1 + vec3(0.02);
  float dt = (t1 - t0)/24.0, T = 1.0; vec3 L = vec3(0.0);
  for (int i = 0; i < 24; i++) {
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt);
    float z = q.z - z0;
    if (z < -0.4 || z > Lc) continue;
    float zn = max(z, 0.0)/Lc;
    float rc = R0*(1.0 + 0.55*zn);
    float r = length(q.xy);
    float ang = atan(q.y, q.x);
    float shell = exp(-pow((r - rc)/(0.16*rc), 2.0));
    float front = smoothstep(-0.35, 0.05, z);                        // sharp edge at the shock
    float aft = exp(-zn*2.6);
    float streak = 0.35 + 0.65*vnoise(vec2(ang*9.0, zn*3.0 - uTime*6.0))*(0.7 + 0.3*vnoise(vec2(ang*31.0, uTime*20.0)));
    float dens = uVapor.x*shell*front*aft*streak*streak*4.0;
    float a = 1.0 - exp(-dens*dt);
    float self = 0.75 + 0.25*clamp(dot(normalize(vec3(q.xy, 0.0) + 1e-4), sunB.xyz), -1.0, 1.0);   // sunny side brighter
    L += T*a*lit*self; T *= 1.0 - a;
    if (T < 0.02) break;
  }
  return col*T + L;
}
// cloaked skin: the world seen through the craft (already traced along the bent ray) with a faint glassy rim,
// a shimmer of the hexagonal emitter lattice and a bright wavefront where the cloak is still spreading
vec3 cloakSkin(vec3 world, vec3 n, vec3 rd, vec3 lp, float front){
  float fres = pow(1.0 - abs(dot(n, -rd)), 4.0);
  vec3 r = reflect(rd, n);
  vec2 hx = vec2(lp.x*3.0 + lp.z*1.5, lp.z*2.6 - lp.y*3.0);
  float lat = smoothstep(0.46, 0.5, max(abs(fract(hx.x) - 0.5), abs(fract(hx.y) - 0.5)));
  float shimmer = 0.5 + 0.5*sin(uTime*4.0 + lp.z*3.0 + lp.x*5.0);
  vec3 col = world*0.97 + skyColor(r)*fres*0.1;
  col += gColStripe*(lat*0.012*shimmer + fres*0.015);
  col += gColStripe*exp(-abs(front)*6.0)*1.5*step(front, 50.0);   // the wavefront of the cloak sweeping along the craft
  return col;
}
// ---------------------------------------------------------------- XR-11 weapons in the world
// Laser bolts: a white-hot core in a crimson sheath, glowing along the beam (closest approach of the view ray to
// each beam segment, cut by the scene depth). Dark-energy bombs: black spheres wrapped in crawling violet plasma
// with a halo. Detonations: an expanding shell of violet fire around a collapsing black core, a flat shock ring
// and arcing filaments; the core swallows the light behind it.
vec3 weaponsFx(vec3 col, vec3 ro, vec3 rd, float t){
  for (int i = 0; i < 16; i++) {
    if (i >= uFxBeams) break;
    vec3 a = uBeamA[i].xyz, b = uBeamB[i].xyz; float r = uBeamA[i].w, I = uBeamB[i].w;
    vec3 u = b - a; float L = length(u); u /= max(L, 1e-3);
    vec3 w0 = ro - a; float bb = dot(rd, u), dd = dot(rd, w0), ee = dot(u, w0), den = 1.0 - bb*bb;
    float sR = den > 1e-5 ? (bb*ee - dd)/den : 0.0, sB = den > 1e-5 ? (ee - bb*dd)/den : ee;
    sB = clamp(sB, 0.0, L); sR = max(dot(a + u*sB - ro, rd), 0.0);
    if (sR > t) continue;
    float d = length(ro + rd*sR - (a + u*sB));
    float core = exp(-d*d/(r*r*0.25)), halo = pow(r*r/(d*d + r*r), 1.6);
    float flick = 0.85 + 0.15*sin(uTime*90.0 + sB*0.3);
    col += (vec3(1.0, 0.9, 0.95)*core*6.0 + vec3(1.0, 0.08, 0.2)*halo*1.6)*I*flick;
  }
  for (int i = 0; i < 8; i++) {
    if (i >= uFxBombs) break;
    vec3 c = uBombs[i].xyz; float R = uBombs[i].w;
    vec3 oc = ro - c; float b = dot(oc, rd), h = b*b - dot(oc, oc) + R*R;
    float tc = -b; if (tc < 0.0) continue;
    float dmin = length(oc + rd*tc);
    if (h > 0.0 && -b - sqrt(h) < t) {
      vec3 n = normalize(oc + rd*(-b - sqrt(h)));
      float fres = pow(1.0 - abs(dot(n, rd)), 2.5);
      float vein = vnoise3(n*5.0 + vec3(uTime*1.7)) + 0.5*vnoise3(n*13.0 - vec3(uTime*2.9));
      col = vec3(0.003) + vec3(0.6, 0.18, 1.0)*(fres*3.0 + pow(smoothstep(0.8, 1.2, vein), 2.0)*5.0) + vec3(0.25, 0.75, 1.0)*pow(smoothstep(1.1, 1.35, vein), 3.0)*6.0;
    } else if (tc < t) col += vec3(0.5, 0.15, 1.0)*exp(-(dmin - R)/(R*0.7))*0.9;
  }
  for (int i = 0; i < 6; i++) {
    if (i >= uFxBlasts) break;
    vec3 c = uBlast[i].xyz; float R = uBlast[i].w, age = uBlastI[i].x, I = uBlastI[i].y;   // c: ground zero
    float g = 1.0 - (1.0 - age)*(1.0 - age)*(1.0 - age);   // fast early growth
    float rise = smoothstep(0.05, 1.0, age);               // the fireball lifts off into a rolling cap
    vec3 oc = ro - c; float b = dot(oc, rd);
    float tc = max(-b, 0.0), dmin = length(oc + rd*tc);
    // the first instant: a white-violet flash that swamps everything around it
    if (tc < t + R) col += vec3(1.0, 0.85, 1.0)*exp(-dmin*dmin/(R*R*0.5))*max(0.0, 1.0 - age*7.0)*10.0*I;
    // condensation dome: a thin white shell racing out ahead of the fireball in the first moments (far side first)
    float dome = max(0.0, 1.0 - age*4.0);
    if (dome > 0.0) {
      float Rs = R*(0.4 + 4.5*sqrt(age)), hs = b*b - dot(oc, oc) + Rs*Rs;
      if (hs > 0.0) {
        hs = sqrt(hs);
        for (int s = 1; s >= 0; s--) {
          float ts = s == 0 ? -b - hs : -b + hs;
          if (ts <= 0.0 || ts > t) continue;
          vec3 n = (ro + rd*ts - c)/Rs;
          float limb = pow(1.0 - abs(dot(n, rd)), 3.0), up = smoothstep(-0.05, 0.25, n.y);   // the lower half is underground
          col = col*(1.0 - 0.3*limb*dome*up) + vec3(0.9, 0.88, 1.0)*(0.06 + 1.5*limb)*dome*up*I;
        }
      }
    }
    // on the ground: the shock ring racing outward and the blasted ground glowing violet-white, then cooling
    if (abs(rd.y) > 1e-3) {
      float tr = (c.y + 1.0 - ro.y)/rd.y;
      if (tr > 0.0 && tr < t + 3.0) {
        float rr = length((ro + rd*tr - c).xz), ring = R*(0.6 + 5.0*sqrt(age));
        col += vec3(0.65, 0.3, 1.0)*exp(-pow((rr - ring)/(R*0.08), 2.0))*(1.0 - age)*(1.0 - age)*4.0*I;
        col += vec3(0.8, 0.45, 1.0)*exp(-rr*rr/(R*R*0.35))*exp(-age*3.5)*2.5*I;
      }
    }
    // the cloud: fireball -> torus cap on a stem, a volume with plasma emission that cools into dark smoke
    float Hc = R*(0.25*g + 2.8*rise);                                // cap height above ground zero
    float sb = R*(0.2 + 0.85*g)*(1.0 - 0.45*rise);                   // fireball radius
    float cRm = R*(0.1 + 0.8*rise), cRr = R*(0.38 + 0.12*rise);      // torus major / minor radius
    float stemOn = smoothstep(0.04, 0.25, age);
    vec3 bc = c + vec3(0.0, Hc*0.5, 0.0);
    float bR = max(Hc*0.5 + max(sb, cRr)*1.2, length(vec2(cRm + cRr*1.2, Hc*0.5 + cRr*1.2)));
    vec3 ob = ro - bc; float bb2 = dot(ob, rd), h = bb2*bb2 - dot(ob, ob) + bR*bR;
    if (h <= 0.0) continue;
    h = sqrt(h); float t0 = max(-bb2 - h, 0.0), t1 = min(-bb2 + h, t);
    if (t1 <= t0) continue;
    const int NS = 28;
    float dt = (t1 - t0)/float(NS), tr = 1.0; vec3 acc = vec3(0.0);
    float jit = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float life = smoothstep(1.0, 0.72, age);                         // the whole cloud thins out at the end
    for (int k = 0; k < NS; k++) {
      COST(3);
      vec3 q = ro + rd*(t0 + (float(k) + jit)*dt) - c;
      vec3 qc = q - vec3(0.0, Hc, 0.0);
      float ball = length(qc)/sb;
      float tor = length(vec2(length(qc.xz) - cRm, qc.y*1.3))/cRr;
      float sw = R*(0.12 + 0.14*clamp(q.y/max(Hc, 1.0), 0.0, 1.0))*stemOn;
      float stem = (q.y > -R*0.1 && q.y < Hc) ? length(q.xz)/max(sw, 1e-3) : 9.0;
      float x0 = min(min(ball, mix(9.0, tor, rise)), stem);
      if (x0 > 1.5) continue;
      // rolling turbulence: noise advected outward from the core and up the stem
      vec3 flow = qc/R*2.4 - normalize(qc + vec3(1e-3))*uTime*0.9 + vec3(0.0, -uTime*0.6, 0.0);
      float nz = vnoise3(flow) + 0.5*vnoise3(flow*2.3 + vec3(7.1)) + 0.25*vnoise3(flow*5.1 - vec3(3.3));
      float x = x0 + (nz - 0.875)*0.45;
      float dens = smoothstep(1.0, 0.5, x)*life;
      if (dens <= 0.0) continue;
      // temperature: everything is hot at first, later only the core of the cap and the lower stem still glow
      float temp = exp(-age*2.8)*(1.3 - 0.7*clamp(x, 0.0, 1.0)) + 0.4*exp(-age*1.4)*smoothstep(0.7, 0.1, x)*(0.6 + 0.6*nz);
      vec3 eCol = mix(vec3(0.3, 0.05, 0.85), vec3(0.75, 0.35, 1.0), smoothstep(0.15, 0.6, temp));
      eCol = mix(eCol, vec3(1.0, 0.92, 1.0), smoothstep(0.7, 1.25, temp));
      float fil = pow(clamp(1.0 - abs(vnoise3(q/R*4.0 + vec3(0.0, uTime*2.5, 0.0)) - 0.5)*9.0, 0.0, 1.0), 6.0)*exp(-age*2.0);
      vec3 e = (eCol*temp*temp*(0.5 + 0.7*nz)*14.0 + vec3(0.55, 0.9, 1.0)*fil*12.0)*dens*I;
      float sig = dens*(0.6 + 0.6*nz)*mix(0.5, 3.5, smoothstep(0.08, 0.55, age))*5.0;
      // smoke body: dim and cool, lit from above by the sky and from inside by what still burns
      vec3 smokeC = vec3(0.05, 0.045, 0.06)*(0.6 + 0.6*smoothstep(-R, R, qc.y)) + vec3(0.35, 0.12, 0.7)*temp*0.4;
      acc += tr*(e + smokeC*sig)*dt/R;
      tr *= exp(-sig*dt/R);
      if (tr < 0.01) break;
    }
    col = col*tr + acc;
  }
  return col;
}

