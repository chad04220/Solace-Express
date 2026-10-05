//! kRaytraceUfo
//! ------------------------------------------------------------------------------------------------
//! UFO encounter: saucer with a cabin dome whose hatch slides open on two dancing aliens (own piece: MSVC 64 KB limit)

uniform int uUfoOn; uniform vec3 uUfoPos; uniform mat3 uUfoRot;
uniform vec4 uUfoAnim;   // hatch open 0..1, animation clock (s), laugh 0..1, wave 0..1
// One alien, feet at the origin, facing +x. side = -1/+1 picks which arm waves and phases the dance.
vec2 mapAlien(vec3 a, float side){
  float T = uUfoAnim.y, laugh = uUfoAnim.z, wave = uUfoAnim.w, dance = (1.0 - laugh)*(1.0 - wave);
  float ph = T*5.2 + side*1.3;
  float bounce = abs(sin(ph))*0.09*dance + abs(sin(T*14.0))*0.03*laugh;
  float sway = sin(ph*0.5)*0.13*dance;
  a.y -= bounce; a.z -= sway*0.4;
  // legs: hips swing, knees bend with the bounce
  float hipZ = 0.085;
  vec3 hip1 = vec3(0.0, 0.52, -hipZ + sway*0.5), hip2 = vec3(0.0, 0.52, hipZ + sway*0.5);
  vec3 kn1 = vec3(0.08 + bounce, 0.27, -0.1), kn2 = vec3(0.08 + bounce*0.5, 0.27, 0.1);
  float legs = min(min(sdCapsule(a, hip1, kn1, 0.045), sdCapsule(a, kn1, vec3(0.0, 0.03, -0.11), 0.04)),
                   min(sdCapsule(a, hip2, kn2, 0.045), sdCapsule(a, kn2, vec3(0.0, 0.03, 0.11), 0.04)));
  legs = min(legs, sdEllipsoid(vec3(abs(a.z) - 0.11, a.y - 0.02, a.x - 0.05).zyx, vec3(0.1, 0.03, 0.05)));   // feet
  float torso = sdEllipsoid(a - vec3(0.0, 0.76, sway*0.6), vec3(0.15, 0.26, 0.19));
  vec3 sh = vec3(0.0, 0.94, sway*0.7);
  float neck = sdCapsule(a, sh, sh + vec3(0.0, 0.18, sway*0.2), 0.035);
  // arms: dancing (alternate up and down), laughing (holding the belly), waving (one arm up, hand sweeping)
  float arms = 1e9;
  for (int k = -1; k <= 1; k += 2) {
    float fk = float(k);
    vec3 s0 = sh + vec3(0.0, 0.0, 0.17*fk);
    vec3 hd = s0 + vec3(0.12, 0.3 + 0.28*sin(ph + fk*1.5708), 0.22*fk);
    vec3 hl = vec3(0.17, 0.66, 0.06*fk);
    vec3 hw = s0 + vec3(0.05, 0.55, 0.25*fk + 0.13*sin(T*11.0));
    vec3 rest = s0 + vec3(0.08, -0.35, 0.08*fk);
    bool waver = fk == side;
    vec3 hand = mix(mix(hd, hl, laugh), waver ? hw : rest, wave);
    vec3 el = mix(s0, hand, 0.5) + vec3(-0.04, -0.05, 0.1*fk);
    arms = min(arms, min(sdCapsule(a, s0, el, 0.032), sdCapsule(a, el, hand, 0.028)));
    arms = min(arms, length(a - hand) - 0.045);
  }
  // head: big cranium; laughing throws it back with a shake
  vec3 hc = sh + vec3(0.0, 0.38, sway*0.3);
  vec3 hq = a - hc;
  hq.xy = rot2(hq.xy, laugh*(0.35 + 0.08*sin(T*24.0)));
  hq.xz = rot2(hq.xz, dance*0.25*sin(ph*0.5));
  float head = smin(sdEllipsoid(hq - vec3(0.0, 0.06, 0.0), vec3(0.24, 0.27, 0.25)), sdEllipsoid(hq - vec3(0.06, -0.12, 0.0), vec3(0.13, 0.12, 0.12)), 0.08);
  float body = smin(smin(legs, torso, 0.06), min(neck, arms), 0.04);
  vec2 res = vec2(smin(body, head, 0.05), 72.0);
  // big black almond eyes and a mouth (open wide when laughing)
  vec3 eq = vec3(hq.x - 0.19, hq.y + 0.0, abs(hq.z) - 0.1);
  eq.yz = rot2(eq.yz, 0.45);
  float eyes = sdEllipsoid(eq, vec3(0.05, 0.075, 0.045));
  res = opU(res, vec2(eyes, 73.0));
  float mouth = sdEllipsoid(hq - vec3(0.18, -0.16, 0.0), vec3(0.03, 0.01 + 0.035*laugh + 0.01*wave, 0.05));
  if (mouth < res.x) res = vec2(mouth, 74.0);
  return res;
}
vec2 mapUfo(vec3 p){
  float hatch = uUfoAnim.x, T = uUfoAnim.y;
  float r = length(p.xz);
  // lens-shaped hull with a rim torus, a raised upper deck and an engine bulge underneath
  float hull = sdEllipsoid(p, vec3(7.4, 1.05, 7.4));
  hull = smin(hull, length(vec2(r - 7.15, p.y)) - 0.32, 0.25);
  hull = smin(hull, sdEllipsoid(p - vec3(0.0, 0.55, 0.0), vec3(4.4, 0.7, 4.4)), 0.5);
  hull = smin(hull, sdEllipsoid(p - vec3(0.0, -0.75, 0.0), vec3(3.0, 0.85, 3.0)), 0.4);
  vec2 res = vec2(hull, 70.0);
  // rim light ring and underside glow ring
  res = opU(res, vec2(length(vec2(r - 7.42, p.y)) - 0.12, 75.0));
  res = opU(res, vec2(length(vec2(r - 2.6, p.y + 1.25)) - 0.16, 76.0));
  // cabin dome: a hollow hemisphere; the hatch on the +x side slides up over the top as it opens
  vec3 dq = p - vec3(0.0, 0.95, 0.0);
  float rD = 3.1, thick = 0.09;
  float dOut = length(dq) - rD;
  float shell = max(abs(dOut + thick) - thick, -dq.y);
  vec3 hb = vec3(1.0, 1.05, 1.35);                                        // hatch half-extents (cut-out box)
  float cut = sdRoundBox(dq - vec3(rD, 1.15, 0.0), hb, 0.3);
  float shellCut = max(shell, -cut);
  vec3 dr = dq; dr.xz = rot2(dr.xz, -hatch*1.7);                           // the door slides round the dome
  float door = max(max(abs(length(dr) - rD) - thick*0.9, -dr.y), sdRoundBox(dr - vec3(rD, 1.15, 0.0), hb - 0.04, 0.3));
  res = opU(res, vec2(shellCut, 71.0));
  res = opU(res, vec2(door, 78.0));
  res = opU(res, vec2(sdCapsule(dq, vec3(0.0, rD - 0.1, 0.0), vec3(0.0, rD + 0.9, 0.0), 0.04), 70.0));   // antenna
  res = opU(res, vec2(length(dq - vec3(0.0, rD + 0.95, 0.0)) - 0.11, 75.0));
  // cabin interior: dance floor and the two aliens (only when the hatch is opening)
  if (hatch > 0.01 && length(dq) < rD + 0.2) {
    res = opU(res, vec2(max(abs(dq.y) - 0.04, length(dq.xz) - rD + 0.05), 77.0));
    const float AS = 1.45;   // cartoon-sized aliens, readable from the cockpit
    vec2 a1 = mapAlien((dq - vec3(0.7, 0.04, -0.7))/AS, -1.0); a1.x *= AS;
    vec2 a2 = mapAlien((dq - vec3(0.8, 0.04, 0.75))/AS, 1.0); a2.x *= AS;
    res = opU(res, opU(a1, a2));
  }
  return res;
}
float traceUfo(vec3 ro, vec3 rd, float tmax){
  vec3 oc = ro - uUfoPos; float b = dot(oc, rd), h = b*b - dot(oc, oc) + 81.0;
  if (h < 0.0) return -1.0;
  h = sqrt(h); float t = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t > t1) return -1.0;
  mat3 inv = transpose(uUfoRot); vec3 lo = inv*(ro - uUfoPos), ld = inv*rd;
  for (int i = 0; i < 140; i++) {
    float d = mapUfo(lo + ld*t).x;
    if (d < 0.002*max(1.0, t*0.02)) return t;
    t += d*0.85;
    if (t > t1) break;
  }
  return -1.0;
}
vec3 shadeUfo(vec3 p, vec3 rd, float t){
  mat3 inv = transpose(uUfoRot);
  vec3 lp = inv*(p - uUfoPos);
  const vec2 k = vec2(1, -1); float e = 0.004;
  vec3 ln = normalize(k.xyy*mapUfo(lp + k.xyy*e).x + k.yyx*mapUfo(lp + k.yyx*e).x + k.yxy*mapUfo(lp + k.yxy*e).x + k.xxx*mapUfo(lp + k.xxx*e).x);
  int mid = int(mapUfo(lp).y + 0.5);
  vec3 n = uUfoRot*ln;
  float T = uUfoAnim.y, r = length(lp.xz), ang = atan(lp.z, lp.x);
  Mat m; m.alb = vec3(0.7); m.rough = 0.3; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0, 0, 1);
  vec3 nT; vec4 tx;
  vec3 disco = 0.5 + 0.5*cos(T*3.0 + vec3(0.0, 2.1, 4.2));
  if (mid == 70) {        // polished hull: brushed metal with concentric and radial panel lines, glowing portholes
    tx = triSample(lp, ln, M_METAL, 0.5, nT); m.alb = tx.rgb*vec3(0.78, 0.8, 0.84); m.metal = 0.95; m.rough = clamp(tx.a*0.4, 0.1, 0.3); m.nrm = nT;
    if (abs(fract(r/1.2) - 0.5) > 0.47 || abs(fract(ang*12.0/6.2832) - 0.5) > 0.485) m.alb *= 0.55;
    vec2 pq = vec2(fract(ang*16.0/6.2832) - 0.5, (lp.y - 0.95)/0.18);
    if (r > 4.6 && r < 5.6 && length(vec2(pq.x*2.2, pq.y)) < 0.5 && lp.y > 0.5) { m.alb = vec3(0.02); m.metal = 0.0; m.rough = 0.05; m.emit = vec3(0.4, 1.0, 0.7)*1.4; }
  } else if (mid == 71 || mid == 78) {   // cabin dome and its door: iridescent tinted alloy
    float fr = pow(1.0 - abs(dot(normalize(-rd), n)), 2.0);
    m.alb = mix(vec3(0.25, 0.12, 0.45), vec3(0.1, 0.6, 0.65), fr); m.metal = 0.85; m.rough = 0.12;
    if (dot(ln, normalize(lp - vec3(0.0, 0.95, 0.0))) < 0.0) { m.alb = vec3(0.06, 0.05, 0.09); m.metal = 0.3; m.rough = 0.5; m.emit = disco*0.06; }   // inside
    if (mid == 78 && abs(abs(lp.z) - 0.9) < 0.03) m.emit = vec3(0.5, 1.0, 0.8);   // door edge strips
  } else if (mid == 75) { // rim lights chasing round the saucer + antenna beacon
    float chase = step(0.55, fract(ang*24.0/6.2832 - T*1.6));
    m.alb = vec3(0.1); m.rough = 0.2; m.emit = (lp.y > 3.0 ? vec3(1.0, 0.2, 0.2)*step(0.5, fract(T*1.5)) : mix(vec3(0.2, 1.0, 0.5), vec3(1.0, 0.85, 0.3), chase))*3.0;
  } else if (mid == 76) { m.alb = vec3(0.1); m.emit = vec3(0.3, 0.9, 1.0)*(1.8 + 0.8*sin(T*6.0)); }   // underside glow
  else if (mid == 77) {   // dance floor: flashing tiles
    vec2 c = floor(lp.xz/0.5); float h = fract(sin(dot(c, vec2(12.9898, 78.233)) + floor(T*4.0)*7.13)*43758.5453);
    m.alb = vec3(0.04); m.rough = 0.2; m.emit = (0.5 + 0.5*cos(h*6.2832 + vec3(0.0, 2.1, 4.2)))*step(0.35, h)*1.2;
  } else if (mid == 72) { tx = triSample(lp*6.0, ln, M_LEATHER, 1.0, nT); m.alb = vec3(0.32, 0.72, 0.36)*(0.75 + 0.5*tx.r); m.rough = 0.45; m.nrm = nT; m.emit = vec3(0.02, 0.06, 0.02); }
  else if (mid == 73) { m.alb = vec3(0.005); m.rough = 0.04; m.metal = 0.0; }
  else if (mid == 74) { m.alb = vec3(0.25, 0.02, 0.05); m.rough = 0.6; }
  n = applyTS(n, m.nrm, 0.2);
  float sh = cloudShadow(p);
  vec3 col = shadeSurface(p, n, rd, m, sh);
  // cabin interior lit by the disco floor and a cool ceiling light
  if (mid >= 72 && mid <= 74 || mid == 77) {
    vec3 L1 = normalize(uUfoRot*vec3(0.0, 1.0, 0.0));
    col += m.alb*(disco*0.8*max(dot(n, -L1), 0.0) + vec3(0.6, 0.9, 1.0)*0.7*max(dot(n, L1), 0.0) + 0.15);
  }
  return col;
}
