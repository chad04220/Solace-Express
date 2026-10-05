//! kRaytraceFS3
// ---------------------------------------------------------------- analytic primitives
// slab test against an AABB; returns (tNear, tFar), normal of the entry face
vec2 iBox(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out vec3 n){
  vec3 inv = 1.0/rd;
  vec3 t0 = (bmin - ro)*inv, t1 = (bmax - ro)*inv;
  vec3 tmin = min(t0, t1), tmaxv = max(t0, t1);
  float tN = max(max(tmin.x, tmin.y), tmin.z), tF = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
  n = tN == tmin.x ? vec3(-sign(rd.x),0,0) : tN == tmin.y ? vec3(0,-sign(rd.y),0) : vec3(0,0,-sign(rd.z));
  return vec2(tN, tF);
}
// vertical capped cylinder (base centre c, radius r, height h)
// small debris chunks: oriented boxes
vec3 qrot(vec4 q, vec3 v){ vec3 u = q.yzw; vec3 tt = 2.0*cross(u, v); return v + q.x*tt + cross(u, tt); }
float traceDebris(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out float charred){
  float best = -1.0; nOut = vec3(0,1,0); charred = 0.0;
  for (int i = 0; i < 16; i++) {
    if (i >= uDebN) break;
    vec4 d = uDeb[i]; float sz = abs(d.w);
    vec3 oc = ro - d.xyz; float b = dot(oc, rd), c = dot(oc, oc) - sz*sz*1.6, h = b*b - c;
    if (h < 0.0) continue;
    vec4 qi = vec4(uDebQ[i].x, -uDebQ[i].yzw);
    vec3 lo = qrot(qi, ro - d.xyz), ld = qrot(qi, rd);
    vec3 hs = sz*vec3(1.0, 0.18, 0.6), n;
    vec2 tt = iBox(lo, ld, -hs, hs, n);
    if (tt.x > 0.0 && tt.x < tt.y && tt.x < tmax && (best < 0.0 || tt.x < best)) { best = tt.x; nOut = qrot(uDebQ[i], n); charred = d.w < 0.0 ? 1.0 : 0.0; }
  }
  return best;
}
vec2 iVCyl(vec3 ro, vec3 rd, vec3 c, float r, float h, out vec3 n){
  vec2 o = ro.xz - c.xz; vec2 d = rd.xz;
  float a = dot(d,d), b = dot(o,d), cc = dot(o,o) - r*r;
  float disc = b*b - a*cc;
  n = vec3(0.0);
  if (disc < 0.0 || a < 1e-8) return vec2(1e9, -1e9);
  disc = sqrt(disc);
  float s0 = (-b - disc)/a, s1 = (-b + disc)/a;
  float y0 = (c.y - ro.y)/rd.y, y1 = (c.y + h - ro.y)/rd.y;
  float ya = min(y0, y1), yb = max(y0, y1);
  float tN = max(s0, ya), tF = min(s1, yb);
  if (tN == s0) { vec3 p = ro + rd*tN; n = normalize(vec3(p.x - c.x, 0.0, p.z - c.z)); } else n = vec3(0.0, -sign(rd.y), 0.0);
  return vec2(tN, tF);
}
// cylinder along x (for arched hangar roofs): axis through (y = cy, z = cz)
vec2 iXCyl(vec3 ro, vec3 rd, float cy, float cz, float r, out vec3 n){
  vec2 o = vec2(ro.y - cy, ro.z - cz), d = rd.yz;
  float a = dot(d,d), b = dot(o,d), cc = dot(o,o) - r*r;
  float disc = b*b - a*cc;
  if (disc < 0.0 || a < 1e-8) { n = vec3(0.0); return vec2(1e9, -1e9); }
  disc = sqrt(disc);
  float t0 = (-b - disc)/a;
  vec3 p = ro + rd*t0; n = normalize(vec3(0.0, p.y - cy, p.z - cz));
  return vec2(t0, (-b + disc)/a);
}
// convex polytope: inside where dot(n, p) <= d for all planes (n.xyz, d)
vec2 iConvex(vec3 ro, vec3 rd, vec4 pl[7], int cnt, out vec3 nOut){
  float tN = -1e9, tF = 1e9; nOut = vec3(0.0, 1.0, 0.0);
  for (int i = 0; i < 7; i++) {
    if (i >= cnt) break;
    float den = dot(pl[i].xyz, rd), dist = pl[i].w - dot(pl[i].xyz, ro);
    if (abs(den) < 1e-7) { if (dist < 0.0) return vec2(1e9, -1e9); continue; }
    float t = dist/den;
    if (den < 0.0) { if (t > tN) { tN = t; nOut = pl[i].xyz; } } else tF = min(tF, t);
  }
  return vec2(tN, tF);
}

// ---------------------------------------------------------------- airport structures (runway frame)
// kinds: 0 arched hangar, 1 control tower, 2 terminal, 3 gabled shed, 4 fuel tank, 5 radome
vec2 traceBoxes(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out float kind, out vec3 localHit){
  float best = tmax; vec2 res = vec2(-1.0);
  // buildings are stored per airport: skip every airport whose world bounds the ray misses (most of them)
  for (int ap = 0; ap < 16; ap++) {
    if (ap >= uApCount) break;
    vec4 A0 = dataAt(384 + ap), A1 = dataAt(400 + ap);   // xyz bounds, w = first box / box count
    vec3 nap; vec2 ab = iBox(ro, rd, A0.xyz, A1.xyz, nap);
    if (ab.x > ab.y || ab.y < 0.0 || ab.x > best || A1.w < 0.5) continue;
    int i0 = int(A0.w + 0.5), i1 = i0 + int(A1.w + 0.5);
  for (int i = i0; i < i1; i++){
    vec4 BC = dataAt(64 + i), BH = dataAt(192 + i);
    int ai = int(BC.w);
    vec4 a = uAp[ai];
    float s = sin(a.w), c = cos(a.w);
    vec3 o = ro - vec3(a.x, a.z, a.y);
    vec3 lo = vec3(o.x*c + o.z*s, o.y, o.x*s - o.z*c) - BC.xyz;
    vec3 ld = vec3(rd.x*c + rd.z*s, rd.y, rd.x*s - rd.z*c);
    vec3 H = BH.xyz; int k = int(BH.w + 0.5);
    // quick reject with the bounding box (generous for towers/radomes)
    vec3 nb; vec3 bh = k == 1 || k == 5 ? vec3(H.x*2.0, H.y*1.2, H.z*2.0) : H;
    vec2 bb = iBox(lo, ld, -bh, bh, nb);
    if (bb.x > bb.y || bb.y < 0.0 || bb.x > best) continue;
    float tHit = 1e9; vec3 nl = nb;
    if (k == 0) {
      float rise = H.y*0.9, wallTop = H.y - rise;
      float R = (H.z*H.z + rise*rise)/(2.0*rise);
      vec3 nc; vec2 cy = iXCyl(lo, ld, H.y - R, 0.0, R, nc);
      vec3 nbx; vec2 bx = iBox(lo, ld, -H, H, nbx);
      float tN = max(bx.x, cy.x), tF = min(bx.y, cy.y);
      if (tN < tF && tF > 0.0) { tHit = tN; nl = tN == cy.x ? nc : nbx; }

    } else if (k == 1) {
      vec3 n1, n2, n3, n4;
      float Ht = H.y*2.0;
      vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x, Ht*0.78, n1);
      vec2 c2 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.78, 0.0), H.x*1.5, Ht*0.15, n2);
      vec2 c3 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.93, 0.0), H.x*1.7, Ht*0.04, n3);
      vec2 c4 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.97, 0.0), 0.12, Ht*0.15, n4);
      if (c1.x < c1.y && c1.x > 0.0 && c1.x < tHit) { tHit = c1.x; nl = n1; }
      if (c2.x < c2.y && c2.x > 0.0 && c2.x < tHit) { tHit = c2.x; nl = n2; }
      if (c3.x < c3.y && c3.x > 0.0 && c3.x < tHit) { tHit = c3.x; nl = n3; }
      if (c4.x < c4.y && c4.x > 0.0 && c4.x < tHit) { tHit = c4.x; nl = n4; }
    } else if (k == 3) {
      vec4 pl[7];
      float rh = H.y*0.5, top = H.y*0.4;
      pl[0] = vec4(1,0,0,H.x); pl[1] = vec4(-1,0,0,H.x); pl[2] = vec4(0,0,1,H.z); pl[3] = vec4(0,0,-1,H.z); pl[4] = vec4(0,-1,0,H.y);
      vec3 n1 = normalize(vec3(rh/H.x, 1.0, 0.0)), n2 = normalize(vec3(-rh/H.x, 1.0, 0.0));
      pl[5] = vec4(n1, dot(n1, vec3(0.0, top + rh, 0.0))); pl[6] = vec4(n2, dot(n2, vec3(0.0, top + rh, 0.0)));
      vec3 nc; vec2 r = iConvex(lo, ld, pl, 7, nc);
      if (r.x < r.y && r.y > 0.0) { tHit = r.x; nl = nc; }
    } else if (k == 4) {
      vec3 n1; vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x, H.y*2.0, n1);
      if (c1.x < c1.y && c1.y > 0.0) { tHit = c1.x; nl = n1; }
    } else if (k == 5) {
      vec3 n1; vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x*0.4, H.y*1.1, n1);
      if (c1.x < c1.y && c1.x > 0.0) { tHit = c1.x; nl = n1; }
      vec3 sc = vec3(0.0, -H.y + H.y*1.1 + H.x*0.8, 0.0);
      vec3 oc = lo - sc; float b = dot(oc, ld), cc = dot(oc, oc) - H.x*H.x, h = b*b - cc;
      if (h > 0.0) { float ts = -b - sqrt(h); if (ts > 0.0 && ts < tHit) { tHit = ts; nl = normalize(lo + ld*ts - sc); } }
    } else {
      if (bb.x > 0.0) { tHit = bb.x; nl = nb; }
    }
    if (tHit > 0.0 && tHit < best) {
      best = tHit; res = vec2(tHit, float(i)); kind = float(k);
      nOut = vec3(nl.x*c + nl.z*s, nl.y, nl.x*s - nl.z*c);
      localHit = lo + ld*tHit;
    }
  }
  }
  return res;
}


// ---------------------------------------------------------------- XR-9 display screens and HUD
// ---------------------------------------------------------------- XR-9 cockpit display pages (procedural)
// ---------------------------------------------------------------- cockpit lighting
// Every interior light comes from a modelled fixture (a lens, LED strip or display): it is treated as a line light
// along that fixture, lighting from the nearest point on it with PBR shading and a soft falloff, so light never
// appears to float in mid-air or pool into a hot spot. Body-space positions, normals and view vector.
vec3 fixtureLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 a, vec3 b, vec3 c, float k, vec3 dir){
  vec3 ab = b - a; float h = clamp(dot(p - a, ab)/max(dot(ab, ab), 1e-6), 0.0, 1.0);
  vec3 d = a + ab*h - p; float dl = max(length(d), 1e-3);
  float beam = dot(dir, dir) > 0.5 ? smoothstep(0.2, 0.75, dot(-d/dl, dir)) : 1.0;   // recessed: lights only the way its lens faces
  return pbr(n, v, d/dl, m.alb, max(m.rough, 0.18), m.metal, c*(9.4*beam/(1.0 + dl*dl*k)));
}
// XR-9 sealed pod: panoramic display, two warm ceiling light bars, cyan spine and console strips, amber footwell, MFDs
vec3 podLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E){
  vec3 L = m.alb*vec3(0.03, 0.04, 0.055);                                       // faint bounce
  L += fixtureLight(p, n, v, m, E + vec3(-0.45, 0.02, -0.47), E + vec3(0.45, 0.02, -0.47), vec3(0.42, 0.55, 0.68)*0.55, 6.0, vec3(0.0, 0.0, 1.0));
  for (int i = -1; i <= 1; i += 2) {
    float sx = float(i);
    L += fixtureLight(p, n, v, m, E + vec3(0.2*sx, 0.545, -0.04), E + vec3(0.2*sx, 0.545, 0.34), vec3(1.0, 0.8, 0.58)*0.2, 9.0, vec3(0.0, -1.0, 0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.39*sx, -0.395, -0.25), E + vec3(0.39*sx, -0.395, 0.42), gColStripe*0.05, 14.0, vec3(-0.6*sx, 0.8, 0.0));
  }
  L += fixtureLight(p, n, v, m, E + vec3(0.0, 0.50, -0.55), E + vec3(0.0, 0.60, 0.65), gColStripe*0.06, 10.0, vec3(0.0, -1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.24, -0.60, -0.42), E + vec3(0.24, -0.60, -0.42), vec3(1.0, 0.5, 0.15)*0.12, 10.0, vec3(0.0, 0.7, 0.7));
  L += fixtureLight(p, n, v, m, E + vec3(-0.42, -0.33, -0.42), E + vec3(0.42, -0.33, -0.42), vec3(0.3, 0.75, 0.6)*0.1, 12.0, vec3(0.0, 0.6, 0.8));
  return L;
}
// Light-aircraft / airliner cabin at night: glareshield LED strip floods the panel, dim amber dome light
vec3 cabinLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E, float pz, float phw){
  vec3 L = fixtureLight(p, n, v, m, vec3(-phw*0.88, E.y - 0.13, pz + 0.07), vec3(phw*0.88, E.y - 0.13, pz + 0.07), vec3(1.0, 0.84, 0.62)*(0.015 + 0.16*uNight), 18.0, normalize(vec3(0.0, -1.0, 0.35)));
  L += fixtureLight(p, n, v, m, vec3(0.0, gCab0.z - 0.024, E.z - 0.09), vec3(0.0, gCab0.z - 0.024, E.z - 0.05), vec3(1.0, 0.72, 0.45)*0.12*uNight, 8.0, vec3(0.0, -1.0, 0.0));
  return L;
}
// Cheap ambient occlusion from the cockpit's own distance field (3 taps along the normal)
float interiorAO(vec3 p, vec3 n){
  if ((uDbg & 512) != 0) return 1.0;
  float occ = 0.0, w = 1.0;
  for (int i = 1 + gZero; i <= 3; i++) { float h = 0.02*float(i*i); occ += (h - mapPlane(p + n*h).x)*w; w *= 0.55; }
  return clamp(1.0 - 3.5*occ, 0.3, 1.0);
}
vec3 jetScreen(vec3 col, vec3 rd, int id, vec3 sl){
  vec3 E = gM[22].xyz; vec3 q = sl - E;
  // display look: slight contrast and cool grade, scanlines, darkened edges
  col = pow(max(col, vec3(0.0)), vec3(1.05))*vec3(0.95, 1.02, 1.06)*1.08;
  col *= 0.93 + 0.07*sin(sl.y*1900.0);
  float edge;
  if (id == 41) { float ang = atan(q.x, -q.z); edge = min(1.25 - abs(ang), (0.30 - abs(q.y - 0.02))*2.0); }
  else edge = min(0.3 - abs(q.z - 0.24), 0.2 - abs(q.y - 0.04));
  col *= smoothstep(0.0, 0.05, edge);
  vec3 hc = vec3(0.35, 1.0, 0.72);
  float hud = 0.0;
  if (id == 41) {
    vec3 d = transpose(uPlaneRot)*rd;
    vec2 h = vec2(atan(d.x, -d.z), atan(d.y, -d.z));                 // body-frame angles (rad)
    float px = 0.0025;
    // boresight and flight-path marker
    hud = max(hud, hudLine(abs(h.y), px)*step(abs(h.x), 0.025)*step(0.008, abs(h.x)));
    hud = max(hud, hudLine(abs(h.x), px)*step(abs(h.y), 0.015)*step(0.008, abs(h.y)));
    vec3 vb = uHudV;
    if (vb.z < -0.1) {
      vec2 fp = vec2(atan(vb.x, -vb.z), atan(vb.y, -vb.z)) - h;
      float rr = length(fp);
      hud = max(hud, hudLine(abs(rr - 0.012), px));
      hud = max(hud, hudLine(abs(fp.y), px)*step(0.012, abs(fp.x))*step(abs(fp.x), 0.035));
      hud = max(hud, hudLine(abs(fp.x), px)*step(-0.026, fp.y)*step(fp.y, -0.012));
    }
    // world-conformal horizon and pitch ladder (dashed below the horizon)
    float wel = asin(clamp(rd.y, -1.0, 1.0));
    vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0);
    float az = atan(rd.x, -rd.z) - atan(fw.x, -fw.z); az = mod(az + 3.14159, 6.28318) - 3.14159;
    float k = floor(wel/0.17453 + 0.5);
    float ld = abs(wel - k*0.17453);
    if (k == 0.0) hud = max(hud, hudLine(ld, px*1.4)*step(abs(az), 0.7)*step(0.04, abs(az)));
    else {
      float seg = step(0.06, abs(az))*step(abs(az), 0.2)*(k < 0.0 ? step(0.5, fract(az*45.0)) : 1.0);
      hud = max(hud, hudLine(ld, px)*seg);
      hud = max(hud, hudLine(abs(abs(az) - 0.2), px)*step(abs(wel - k*0.17453 + sign(k)*0.012), 0.012));
    }
    // heading tape across the top
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    if (h.y > 0.335 && h.y < 0.365 && abs(h.x) < 0.38) {
      float f10 = abs(fract(hd/10.0 + 0.5) - 0.5)*10.0;
      float tall = abs(fract(hd/30.0 + 0.5) - 0.5)*30.0 < 0.5 ? 1.0 : 0.0;
      hud = max(hud, step(f10*0.01745, px*0.8)*step(h.y, 0.35 + 0.015*tall));
    }
    hud = max(hud, hudNum(h - vec2(-0.044, 0.372), uHud.z, 3, vec2(0.022, 0.04)));
    hud = max(hud, hudBox(h, vec2(0.0, 0.392), vec2(0.056, 0.03), px));
    // airspeed (kt) and altitude (ft) boxes, Mach and G below, nozzle angle and throttle readouts
    hud = max(hud, hudBox(h, vec2(-0.36, 0.0), vec2(0.075, 0.03), px));
    hud = max(hud, hudNum(h - vec2(-0.418, -0.02), uHud.x*1.94384, 4, vec2(0.022, 0.04)));
    hud = max(hud, hudBox(h, vec2(0.38, 0.0), vec2(0.092, 0.03), px));
    hud = max(hud, hudNum(h - vec2(0.305, -0.02), uHud.y*3.28084, 5, vec2(0.022, 0.04)));
    hud = max(hud, hudNum(h - vec2(-0.41, -0.085), uHud.w*100.0, 3, vec2(0.014, 0.025)));
    hud = max(hud, step(length(h - vec2(-0.3895, -0.084)), 0.0025));          // Mach decimal point
    hud = max(hud, hudNum(h - vec2(-0.41, -0.13), abs(uHud2.x)*10.0, 3, vec2(0.014, 0.025)));
    hud = max(hud, step(length(h - vec2(-0.3705, -0.129)), 0.0025));          // G decimal point
    vec2 nb = h - vec2(0.33, -0.16);                                          // thrust-vector angle arc (+-30 deg)
    float na = atan(-nb.y, nb.x); float nr = length(nb);
    hud = max(hud, hudLine(abs(nr - 0.06), px)*step(-0.5236, na)*step(na, 0.5236)*0.6);
    float nzA = uHud2.z*1.5708;
    hud = max(hud, hudLine(abs(nb.x*sin(nzA) + nb.y*cos(nzA)), px*1.5)*step(nr, 0.06)*step(0.0, nb.x*cos(nzA) - nb.y*sin(nzA)));
    hud = max(hud, step(abs(h.x + 0.36), 0.008)*step(-0.3, h.y)*step(h.y, -0.3 + 0.12*uHud2.y));   // throttle bar
    hud = max(hud, hudBox(h, vec2(-0.36, -0.24), vec2(0.008, 0.06), px*0.8));
    if (uHud2.w > 0.5) for (int g = 0; g < 3; g++) hud = max(hud, step(length(h - vec2(0.28 + 0.03*float(g), -0.25)), 0.008));
  } else {
    // side cameras: frame ticks and a heading readout
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    vec2 u = vec2(q.z - 0.24, q.y - 0.04);
    hud = max(hud, hudNum(u - vec2(-0.03, 0.15), hd, 3, vec2(0.012, 0.02)));
    hud = max(hud, hudLine(abs(u.y), 0.0012)*step(0.25, abs(u.x)));
  }
  return mix(col, hc*1.6, clamp(hud, 0.0, 1.0)*0.85);
}
// ---------------------------------------------------------------- research jet exhaust plumes
// Volumetric emission marched through each plume (body space). Dry thrust: a blue core whose length and brightness
// follow the throttle, an orange-tipped flame from mid power and pale shock cells towards full military power. Reheat
// (phasing in from ~70% spool): a translucent blue-violet shell at the
// nozzle, a train of white-yellow shock diamonds (Mach disks joined by the expansion / compression cones, spaced
// wider as the pressure ratio climbs with Mach) inside an orange flame that flares, flickers and reddens downstream.
float gPlumeT = 1.0;   // light from behind that gets through the flames (reheat gas and soot absorb a little)
vec3 plumeOne(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, float jit){
  float sp = gFlame.x, ab = gFlame.y;
  float L = mix(1.6, 4.5, sp) + 8.0*ab;
  vec3 c = o + ax*(L*0.5); float br = L*0.5 + 1.0;
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return vec3(0.0);
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return vec3(0.0);
  vec3 ay = normalize(cross(ax, vec3(1.0, 0.0, 0.0)));
  float spacing = 0.8 + 0.3*clamp(gFlame.w, 0.0, 2.5);
  // a camera inside the jet (chase view right behind in reheat) sees the flame ahead of it, not a glow all around
  vec3 co = lo - o; float cax = dot(co, ax), crad = length(co - ax*cax);
  float camIn = smoothstep(3.5, 1.5, crad)*smoothstep(-1.0, 0.5, cax)*smoothstep(L + 6.0, L, cax);
  float dt = (t1 - t0)/28.0;   // 28 jittered steps (the TAA smooths the rest): 40 made the plume pixels the costliest
  vec3 acc = vec3(0.0);        // on screen behind the XR-9
  for (int i = 0; i < 28; i++) {
    COST(3);
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax);
    if (x < -0.05 || x > L) continue;
    float u = max(x, 0.0)/L;
    float nx = q.x, ny = dot(q, ay);
    float cell = fract(x/spacing), ncell = x/spacing;
    // 2D nozzle: a flat jet that rounds out and spreads downstream, pinched at every shock cell in reheat
    float pinch = 1.0 - 0.14*ab*(0.5 + 0.5*cos(cell*6.2832))*(1.0 - u);
    float wx = mix(0.37, 0.55, u)*(1.0 + 0.5*ab*u)*pinch, wy = mix(0.25, 0.55, u)*(1.0 + 0.5*ab*u)*pinch;
    float e2 = (nx*nx)/(wx*wx) + (ny*ny)/(wy*wy);
    if (e2 > 4.0) continue;
    float r = sqrt(e2);
    float turb = vnoise(vec2(x*2.2 - uTime*55.0, nx*4.0 + ny*6.0))*0.65 + vnoise(vec2(x*5.5 - uTime*95.0, ny*9.0 - nx*7.0))*0.35;
    float lip = smoothstep(-0.05, 0.08, x);
    // shock diamonds: Mach disk mid-cell plus the converging / diverging cone edges, fading cell by cell
    float dc = abs(cell - 0.5)*2.0;
    float decay = exp(-ncell*0.38)*smoothstep(0.15, 0.6, ncell);
    float bead = exp(-pow((cell - 0.5)/0.16, 2.0) - r*r*5.0);                      // the bright Mach disk region
    float cones = exp(-pow((r - 0.55*(1.0 - dc) - 0.05)/0.06, 2.0))*smoothstep(0.9, 0.6, r)*0.35;
    float diam = (bead + cones)*decay;
    // reheat
    float shell = exp(-pow((r - 0.8)/0.22, 2.0))*(1.0 - smoothstep(0.0, 0.3, u));
    float flame = exp(-e2*1.3)*smoothstep(0.2, 0.75, turb + 0.45*(1.0 - u))*smoothstep(0.02, 0.18, u);
    vec3 fCol = mix(vec3(1.0, 0.5, 0.14), vec3(0.85, 0.16, 0.04), smoothstep(0.4, 1.0, u));
    vec3 e = ab*(vec3(0.75, 0.38, 0.95)*shell*0.9 + vec3(1.0, 0.8, 0.45)*diam*7.0 + fCol*flame*(4.0 - 2.4*u)
                 + vec3(1.0, 0.62, 0.3)*exp(-e2*5.0)*(1.0 - smoothstep(0.0, 0.5, u))*1.2);
    // dry: a blue core that grows with the throttle, an orange-tipped flame from mid power, pale shock cells near full
    e += (1.0 - ab)*(vec3(0.3, 0.5, 1.0)*exp(-e2*2.5)*(1.0 - u)*3.2*sp
                     + mix(vec3(1.0, 0.55, 0.25), fCol, u)*flame*(1.8 - 0.8*u)*smoothstep(0.3, 0.85, sp)
                     + vec3(0.65, 0.78, 1.0)*diam*3.5*smoothstep(0.5, 0.95, sp));
    float tcam = t0 + (float(i) + jit)*dt;
    acc += e*lip*pow(1.0 - u, 0.8)*mix(1.0, smoothstep(3.0, 14.0, tcam), camIn)*dt;
    gPlumeT *= exp(-ab*(flame*0.9 + shell*0.3)*lip*dt);
  }
  acc *= (0.9 + 0.1*sin(uTime*63.0))*0.35;
  return acc/(1.0 + max(acc.r, max(acc.g, acc.b))*0.45);   // gentle hue-preserving roll-off keeps the orange orange
}
void shadeWraith(inout Mat m, int mid, vec3 lp, vec3 ln, float t);
vec3 wraithPlumes(vec3 ro, vec3 rd, float tmax, float jit);
vec3 vaporCone(vec3 col, vec3 ro, vec3 rd, float tmax, float jit);
vec3 cloakSkin(vec3 world, vec3 n, vec3 rd, vec3 lp, float front);
vec3 weaponsFx(vec3 col, vec3 ro, vec3 rd, float t);
void shadeWraithCockpit(inout Mat m, int mid, vec3 lp, vec3 ln, vec3 E);
vec3 wraithPodLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E);
vec3 wrHolo(vec3 ro, vec3 rd, float tmax);
vec3 wraithScreen(vec3 col, vec3 rd, int id, vec3 sl);
// research jet displays: each shows the picture of a camera on the airframe (camera_feeds.cpp, feed_cameras.h)
uniform sampler2D uFeedTex; uniform int uFeedOn;
uniform vec4 uFeedTile[13], uFeedR[13], uFeedU[13], uFeedB[13];   // atlas tile | right + tanX | up + tanY | back + has a picture
uniform float uFeedSkip;   // drawing a camera's picture: its lens sits just outside the skin, the airframe march starts past it
vec3 feedScreen(int id, vec3 sl, out vec3 rdc, out bool bomb);
vec3 wrFeedOverlay(vec3 col, vec3 sl);
float wrClip(vec3 lp, int mid);
vec3 wrClipAtlas(vec2 uv);
vec3 jetPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (gFlame.x < 0.02) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float a = gFlame.z;
  vec3 ax = vec3(0.0, -sin(a), cos(a));
  vec3 col = vec3(0.0);
  for (int s = -1; s <= 1; s += 2) {
    vec3 o = vec3(0.82*float(s), -0.12, 7.75) + ax*1.0;   // nozzle exit (pivot + 1 m along the swivelled axis)
    col += plumeOne(lo, ld, tmax, o, ax, jit);
  }
  return col;
}
void main(){
  gZero = min(uQuality, 0);
  loadMain();
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = camRay(ndc);
  vec3 ro = uCamPos;
  float jitter = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uSeed);   // interleaved gradient noise, rotated per frame
  float tmax = 80000.0;
  // research jet cockpit: display screens show the outside world (re-traced without the airframe); the rest of
  // the sealed pod hides everything beyond it
  bool onScr = false; int scrId = 0; vec3 scrL = vec3(0.0);
  // pod = a pixel on the sealed cockpit's interior: it is shaded from the cockpit alone, and nothing outside the
  // aircraft (terrain, water, buildings, clouds, shadows) is traced for it. Only the display screens see out.
  // In any cockpit view the aircraft is traced first: a pixel that lands on the cabin (or on a wing seen through a
  // window) needs nothing from outside, so only rays leaving through the windows trace the world.
  bool pod = false, cockpitView = uPlaneOn == 1 && gPS.w > 0.5 && uWreck == 0; vec2 h0 = vec2(-1.0);
  // the aircraft hull was rasterized along exactly this ray: start the airframe march where it is (0: on its inside)
  // (0: march from the camera; the first uHullNear metres are always marched, the hull's faces there are ignored)
  float hullT = 0.0;
  if (uHullOn == 1 && uWreck == 0) { float hv = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).g; hullT = hv > 1e29 ? hv : (hv > 0.0 ? max(uHullNear, hv*0.999 - 0.1) : 0.0); }
  if (uFeedSkip > 0.0 && hullT == 0.0) hullT = uFeedSkip;
  // The airframe along this camera ray, traced once for every use below (the cockpit, the cloak, the outside view):
  // each call site would be another inlined copy of the march and the airframe's distance.
  bool jetC = int(gM[0].z + 0.5) >= 5;
  // (outside the cockpit, no further than the nearest rasterized tree / rock / building: past it the airframe is hidden,
  // the cloak's bent ray included)
  float tOpq = tmax;
  if (!cockpitView) { float gb = texelFetch(uGB0, ivec2(gl_FragCoord.xy), 0).x; if (gb > 0.0) tOpq = min(tmax, gb + 0.5); }
  vec2 hTop = tracePlaneHull(ro, rd, cockpitView ? (jetC ? 6.0 : planeBound()*2.0) : tOpq, hullT);
  int hTopPiece = gPI;   // (traffic tracing moves the piece transform; restored before shading)
  if (cockpitView) {
    bool jet = jetC;
    h0 = hTop;
#ifdef HULL_DEBUG
    if ((uDbg & 1024) != 0) {   // (debug: the hull's start against a march from the camera - red: the hull skipped a hit,
      vec2 hf = tracePlane(ro, rd, jet ? 6.0 : planeBound()*2.0);   // blue: a hit moved, green: the hull found one the camera's march didn't)
      vec3 dc = vec3(0.0);
      if (hf.x > 0.0 && h0.x < 0.0) dc = hullT > 1e29 ? vec3(1.0, 0.0, 0.0) : vec3(1.0, 1.0, 0.0);   // (yellow: skipped from a start)
      else if (hf.x < 0.0 && h0.x > 0.0) dc = vec3(0.0, 1.0, 0.0);
      else if (hf.x > 0.0 && abs(hf.x - h0.x) > 0.01) dc = vec3(0.0, 0.3, 1.0)*clamp(abs(hf.x - h0.x)*10.0, 0.3, 1.0);
      oColor = vec4(dc*50.0, 0.0); oDepth = 1.0; oCloudMask = 0.0;
      if ((uDbg & 2048) != 0) oColor = vec4(hullT, hf.x, h0.x, texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).g);   // (raw, for a readback)
      if ((uDbg & 4096) != 0 && hf.x > 0.0) {   // (the 6.25 cm hull voxel holding the hit: its centre's distance, how far the hit is from it)
        pieceXf(-1); vec3 lp = transpose(gPR)*(ro + rd*hf.x - gPP);
        float org = -ceil(planeBound())*1.0; vec3 c = vec3(org) + (floor((lp - org)/0.0625) + 0.5)*0.0625;
        oColor = vec4(mapPlane(c).x, length(c - lp), mapPlane(lp).x, float(int(mapPlane(lp).y + 0.5)));
        oDepth = lp.x; oCloudMask = lp.y;
      }
      return;
    }
#endif
    if (h0.x > 0.0) {
      int id0 = int(h0.y + 0.5);
      if (jet && ((id0 >= 41 && id0 <= 43) || (id0 >= 61 && id0 <= 63))) { onScr = true; scrId = id0; scrL = transpose(uPlaneRot)*(ro + rd*h0.x - uPlanePos); }
      else { pod = true; tmax = h0.x + 0.05; }
    }
  }
  if (onScr) {   // a display: its camera's picture, with the display's own look and symbology over it
    bool bomb; vec3 rdc;
    vec3 col = feedScreen(scrId, scrL, rdc, bomb);
    bool wr = int(gM[0].z + 0.5) == 6;
    col = bomb ? wrFeedOverlay(col, scrL) : wr ? wraithScreen(col, rdc, scrId, scrL) : jetScreen(col, rdc, scrId, scrL);
    if (wr) col += wrHolo(ro, rd, h0.x);   // the hologram floats inside the cabin, in front of the displays
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), 0.0); oDepth = h0.x; oCloudMask = 0.0;
#ifdef COST_MAP
    oColor = gCost;
#endif
    return;
  }
  // XR-11 cloak: a pixel on the cloaked craft sees the world behind it along a slightly bent ray
  bool cloak = false; vec3 ckN = vec3(0.0), ckLp = vec3(0.0), rd0 = rd, ro0 = ro; float ckT = 0.0;
  if (uWr[4].w > 0.001 && uPlaneOn == 1 && uWreck == 0 && !cockpitView && int(gM[0].z + 0.5) == 6) {
    vec2 hc = hTop;
    if (hc.x > 0.0) {
      vec3 hp = ro + rd*hc.x; ckLp = transpose(uPlaneRot)*(hp - uPlanePos);
      if (ckLp.z < uWr[6].y) {
        cloak = true; ckT = hc.x; ckN = uPlaneRot*planeNormal(ckLp);
        ro = hp + rd*0.05; rd = normalize(rd - (ckN - rd*dot(ckN, rd))*0.035);
      }
    }
  }
  vec3 roV = ro, rdV = rd;
  // environment entities: the raster pass already found the nearest tree / rock / building on this pixel
  vec4 g0 = vec4(0.0);
  if (!pod) g0 = texelFetch(uGB0, ivec2(gl_FragCoord.xy), 0);
  if (cloak) g0.x = g0.x > ckT ? g0.x - ckT : 0.0;   // seen through the cloak (the ray now starts on its skin)
  float tE = g0.x > 0.0 && g0.x < tmax ? g0.x : -1.0;
  if (uEnvOn == 1 && !pod && !cloak) {   // the envelope was rasterized along exactly this pixel's ray
    float te = texelFetch(uEnv, ivec2(gl_FragCoord.xy), 0).r;
    gTStart = te > 1e29 ? te : (te > 0.0 ? max(1.0, te*0.999 - 1.0) : 1.0);
  }
  float tT = pod ? -1.0 : traceTerrain(ro, rd, tE > 0.0 ? tE + 1.0 : tmax);
  gTStart = 1.0;
  float tW = (!pod && rd.y < 0.0 && ro.y > 0.0) ? -ro.y/rd.y : -1.0;
  vec3 bn; float bkind = 0.0; vec3 bl;
  vec2 bh = pod ? vec2(-1.0) : traceBoxes(ro, rd, tT > 0.0 ? tT : tmax, bn, bkind, bl);
  int trafK = -1; vec2 trafH = vec2(-1.0);
  vec2 ph = onScr || cloak || (cockpitView && !pod) ? vec2(-1.0) : (pod ? h0 : hTop);
  float t = 1e9; int hit = 0;
  if (tT > 0.0) { t = tT; hit = 1; }
  if (tW > 0.0 && tW < t) { t = tW; hit = 2; }
  if (bh.x > 0.0 && bh.x < t) { t = bh.x; hit = 3; }
  if (tE > 0.0 && tE < t) { t = tE; hit = 5; }
  if (ph.x > 0.0 && ph.x < t) { t = ph.x; hit = 4; }
  // traffic: only as far as the nearest opaque hit found so far
  if (!pod && uTrafficN > 0) { gTrafCamRay = !cloak; trafH = traceTraffic(ro, rd, t < 1e8 ? t : tmax, trafK); gTrafCamRay = false; loadMain(); pieceXf(hTopPiece); }
  float tU = (uUfoOn == 1 && !pod) ? traceUfo(ro, rd, t < 1e8 ? t : tmax) : -1.0;
  bool ufoHit = tU > 0.0 && tU < t;
  if (ufoHit) { t = tU; hit = 8; }
  bool trafHit = false;
  if (trafH.x > 0.0 && trafH.x < t) { t = trafH.x; hit = 4; ph = trafH; trafHit = true; loadTraffic(trafK); trafficXf(trafK); }
  vec3 dn; float dChar = 0.0;
  float tD = uDebN > 0 && !pod ? traceDebris(ro, rd, t < 1e8 ? t : tmax, dn, dChar) : -1.0;
  if (tD > 0.0 && tD < t) { t = tD; hit = 6; }
  vec3 col;
  float taaFlag = hit == 8 ? 0.2 : hit == 4 ? (uWreck > 0 || trafHit ? 0.2 : 0.5) : (hit == 6 ? 0.2 : 1.0);   // 1 world, 0.5 rigid with the aircraft, 0.2 moving, 0 no history
  if (onScr) taaFlag = 0.0;
  if (hit == 0) { col = skyColor(rd); t = 1e6; }
  else if (hit == 8) { col = applyFog(shadeUfo(ro + rd*t, rd, t), ro, rd, t); }
  else {
    vec3 p = ro + rd*t;
    float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
    // (one call site for the ground and the scenery: each is another inlined copy of the aircraft's shape)
    float trafSh = uTrafficN > 0 && (hit == 1 || hit == 5) && sunVis > 0.0 ? trafficShadow(p) : 1.0;
    if (hit == 1) {
      vec3 n = terrainNormal(p.xz, t);
      vec4 base = baseAt(p.xz);
      Mat m = terrainMaterial(p, n, t, base);
      vec3 ns = applyTS(n, m.nrm, t < 2000.0 ? 0.6 : 0.25);
      float sh = sunVis > 0.0 ? terrainShadow(p + n*0.5, uSunDir, t) : 0.0;
      if (sh > 0.0) sh *= entShadow(p, n);
      if (t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
      sh *= trafSh;
      sh *= cloudShadow(p);
      col = shadeSurface(p, ns, rd, m, sh);
    } else if (hit == 2) {
      // ocean
      float depth = max(-groundH(p.xz, 5), 0.0);
      // wave normals band-limited to the pixel footprint (fades each octave before it can alias into sparkle)
      float foot = t*2.0*uTanHalf/uRes.y;                         // metres per pixel at this distance
      vec2 w = p.xz*0.05 + uTime*vec2(0.3, 0.2);
      float a1 = 1.0 - smoothstep(2.5, 7.0, foot), a2 = 1.0 - smoothstep(0.8, 2.2, foot), a3 = 1.0 - smoothstep(0.15, 0.45, foot), a4 = 1.0 - smoothstep(0.05, 0.14, foot);
      vec3 n1 = noised(w*1.0); vec3 n2 = noised(w*3.1 + 5.0); vec3 n3 = noised(p.xz*0.9 + uTime*vec2(-0.9, 0.7));
      vec3 n4 = a4 > 0.0 ? noised(p.xz*3.3 + uTime*vec2(1.3, -1.1)) : vec3(0.0);
      vec3 n0 = noised(p.xz*0.004 + uTime*vec2(0.02, 0.013));      // long swell, never aliases
      float amp = 0.12 + 0.12*uStorm + 0.04*uWet;
      vec2 sl = (n1.yz*0.6*a1 + n2.yz*0.3*a2)*amp + n3.yz*0.035*a3 + n4.yz*0.012*a4 + n0.yz*0.02;
      vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
      float rough = clamp(foot*0.02 + (1.0 - a2)*0.25, 0.0, 0.6);   // lost wave detail becomes statistical roughness
      vec3 v = -rd;
      float fk = clamp(1.0 - dot(n, v), 0.0, 1.0); float fres = 0.02 + 0.98*fk*fk*fk*fk*fk;
      vec3 r = reflect(rd, n); r.y = abs(r.y);
      vec3 refl = skyColor(r);
      // reflected clouds (cheap)
      if (uCloudCover > 0.05 && uQuality > 0) { gCloudLite = 1; vec4 cl = traceClouds(p, r, 30000.0, 0.5); gCloudLite = 0; refl = refl*cl.a + cl.rgb; }
      float sh = sunVis > 0.0 ? terrainShadow(p + vec3(0,1,0), uSunDir, t) * cloudShadow(p) * entShadow(p, vec3(0,1,0)) : 0.0;
      vec4 base = baseAt(p.xz);
      vec3 deep = mix(vec3(0.004,0.03,0.06), vec3(0.003,0.02,0.035), base.w);
      vec3 shallow = mix(vec3(0.02,0.16,0.17), vec3(0.03,0.30,0.29), base.z) * (1.0 - 0.7*base.w);
      vec3 water = mix(shallow, deep, smoothstep(0.0, 18.0, depth));
      vec3 lit = water*(uSunCol*max(uSunDir.y,0.0)*1.0*sh + ambientLight(vec3(0,1,0))*0.35);
      float foam = smoothstep(0.7, 0.0, depth) * smoothstep(0.55, 0.85, vnoise(p.xz*0.15 + uTime*0.4) + 0.3*sin(depth*4.0 - uTime*1.5));
      lit = mix(lit, vec3(0.85)*(uSunCol*max(uSunDir.y,0.0)*1.5 + ambientLight(vec3(0,1,0))), foam*0.8);
      vec3 h = normalize(v + uSunDir);
      float ex = mix(900.0, 40.0, rough/0.6);
      float spec = pow(max(dot(n, h), 0.0), ex)*(ex + 8.0)/(900.0 + 8.0)*120.0 + pow(max(dot(n,h),0.0), 90.0*(1.0 - rough))*1.5;
      col = mix(lit, refl, fres) + uSunCol*spec*sh*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
      // night: runway/town light reflections handled by overlay pass glow
    } else if (hit == 5) {
      // tree, rock or building from the G-buffer
      vec3 n = octDec(g0.yz);
      vec4 g1 = texelFetch(uGB1, ivec2(gl_FragCoord.xy), 0), g2 = texelFetch(uGB2, ivec2(gl_FragCoord.xy), 0);
      Mat m; m.alb = g1.rgb*g1.rgb; m.rough = g1.a; m.metal = g2.a; m.emit = g2.rgb*g2.rgb*8.0; m.nrm = vec3(0,0,1);
      int cls = int(g0.w + 0.5);
      p = ro + rd*t;
      float sh = sunVis > 0.0 ? terrainShadow(p + n*0.5 + vec3(0.0, 0.5, 0.0), uSunDir, t) : 0.0;
      if (sh > 0.0) sh *= entShadow(p, n)*cloudShadow(p);
      if (sh > 0.0 && t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
      sh *= trafSh;
      col = shadeSurface(p, n, rd, m, sh);
      if (cls == 1) {   // foliage: light through the leaves when the sun is behind them, and a soft wrap
        float back = pow(max(dot(rd, uSunDir), 0.0), 3.0)*0.9 + 0.12*max(dot(-n, uSunDir), 0.0);
        col += m.alb*vec3(0.85, 1.0, 0.55)*uSunCol*sh*back*1.6;
      }
    } else if (hit == 3) {
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1); m.rough = 0.7; m.alb = vec3(0.7);
      vec3 nn = bn;
      vec3 nTS = vec3(0,0,1);
      {
        int k = int(bkind + 0.5); vec3 lh = bl; vec3 H = dataAt(192 + int(bh.y)).xyz;
        if (k == 0) {        // arched hangar: corrugated metal skin, big sliding doors facing the runway
          vec4 tx = triSample(lh*vec3(1.0, 1.0, 1.0), nn, M_CORRUGATED, 2.0, nTS);
          m.alb = tx.rgb*vec3(0.75, 0.78, 0.8); m.rough = tx.a; m.metal = 0.7; m.nrm = nTS;
          if (abs(bn.y) < 0.6 && abs(lh.z) < H.z*0.85 && lh.y < H.y*0.2 && abs(abs(lh.x) - H.x) < 0.3) {
            m.alb = vec3(0.35, 0.4, 0.45); if (fract(lh.z/4.0) < 0.03) m.alb *= 0.5; }
        } else if (k == 1) { // control tower: concrete shaft, glass cab
          float Ht = H.y*2.0, yy = lh.y + H.y;
          vec4 tx = triSample(lh, nn, M_CONCRETE, 3.0, nTS); m.alb = tx.rgb; m.rough = tx.a; m.nrm = nTS;
          if (yy > Ht*0.78 && yy < Ht*0.93 && abs(bn.y) < 0.5) { m.alb = vec3(0.03, 0.06, 0.07); m.rough = 0.04; m.metal = 0.5; m.emit = vec3(0.3, 0.7, 0.45)*uNight*0.6; }
          if (yy > Ht*0.93) m.alb = vec3(0.25);
          if (yy > Ht*0.97) { m.alb = vec3(0.8, 0.1, 0.1); m.emit = vec3(1.0, 0.1, 0.05)*step(0.5, fract(uTime*0.7))*2.0; }
        } else if (k == 2) { // terminal: glass curtain wall over a concrete base
          vec4 tx = triSample(lh, nn, M_CONCRETE, 4.0, nTS); m.alb = tx.rgb*0.95; m.rough = tx.a; m.nrm = nTS;
          if (abs(bn.y) < 0.5 && lh.y > -H.y + 1.0) {
            float mul = step(0.04, fract(lh.z/2.4))*step(0.06, fract((lh.y + H.y)/3.2));
            m.alb = mix(vec3(0.6), vec3(0.04, 0.07, 0.1), mul); m.rough = mix(0.4, 0.04, mul); m.metal = 0.5*mul;
            m.emit = vec3(1.0, 0.88, 0.7)*uNight*0.9*mul;
          }
          if (bn.y > 0.5) { m.alb = vec3(0.5); }
        } else if (k == 3) { // gabled shed / FBO house
          vec4 tx = triSample(lh, nn, abs(bn.y) > 0.3 ? M_TILES : M_PLASTER, 2.5, nTS);
          m.alb = tx.rgb*(abs(bn.y) > 0.3 ? vec3(0.9, 0.6, 0.5) : vec3(0.95, 0.93, 0.88)); m.rough = tx.a; m.nrm = nTS;
          if (abs(bn.y) < 0.3 && fract(lh.z/2.5) > 0.6 && lh.y > -H.y*0.5 && lh.y < 0.0) { m.alb = vec3(0.05); m.rough = 0.08; m.emit = vec3(1.0,0.8,0.5)*uNight*1.5; }
        } else if (k == 4) { // fuel tank
          vec4 tx = triSample(lh, nn, M_METAL, 3.0, nTS); m.alb = tx.rgb*vec3(0.95); m.rough = 0.35; m.metal = 0.6; m.nrm = nTS;
          if (abs(lh.y) < 0.5) m.alb = vec3(0.8, 0.15, 0.1);
        } else {             // radar dome on a pylon
          m.alb = vec3(0.92); m.rough = 0.5;
          if (lh.y < 0.0) { vec4 tx = triSample(lh, nn, M_METAL, 2.0, nTS); m.alb = tx.rgb*0.7; m.metal = 0.6; m.nrm = nTS; }
        }
      }
      vec3 ns = applyTS(nn, m.nrm, 0.5);
      float sh = sunVis > 0.0 ? terrainShadow(p + nn*0.3, uSunDir, t) : 0.0;
      col = shadeSurface(p, ns, rd, m, sh*cloudShadow(p));
    } else if (hit == 6) {
      // debris chunk: torn painted skin or charred metal
      vec3 nT; vec4 tx = triSample(p*2.0, dn, M_METAL, 1.0, nT);
      Mat m; m.metal = 0.5; m.emit = vec3(0.0); m.nrm = nT;
      float burn = vnoise(p.xz*3.0 + p.y);
      m.alb = dChar > 0.5 ? vec3(0.03, 0.028, 0.026)*(0.6 + burn) : gColBase*tx.rgb*(0.3 + 0.4*burn);
      m.rough = dChar > 0.5 ? 0.9 : 0.45;
      float sh = sunVis > 0.0 ? terrainShadow(p + dn*0.05, uSunDir, t) : 0.0;
      col = shadeSurface(p, applyTS(dn, m.nrm, 0.4), rd, m, sh);
    } else {
      // aircraft (or one wreck piece: gP* hold the transform of the piece that was hit)
      mat3 inv = transpose(gPR);
      vec3 lp = gPC + inv*(p - gPP);
      vec3 ln = planeNormal(lp);
      vec3 n = gPR*ln;
      int mid = int(ph.y + 0.5);
      if (mid == 11) { vec3 sc = fusSection(lp.z); vec3 rad = vec3(lp.x, lp.y - sc.z, 0.0); if (dot(ln, rad) > 0.55*length(rad) && lp.y > gM[22].y - 0.9) mid = 1; }
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
      m.alb = gColBase; m.rough = 0.28;
      bool interior = (mid >= 10 && mid <= 14) || (mid >= 40 && mid < 94);   // (94+: the light fixtures, outside)
      vec3 sec = fusSection(lp.z);
      vec4 WS = gM[23]; vec4 E = gM[22];
      int ck = int(gM[21].z + 0.5);
      if (mid == 1) {
        float yr = (lp.y - sec.z)/sec.y;
        bool body = lp.z > gM[1].x + 0.05 && lp.z < gM[8].x - 0.05 && abs(lp.x) < sec.x + 0.05 && abs(yr) < 1.05;
        if (body) {
          m.alb = fuselagePaint(lp, sec);
          {   // skin panels: frames every 0.85 m around the body, stringer seams along it every 45 degrees round the
              // section; rivet rows beside each; the cabin door's outline and handle on either side
            float px = t*2.0*uTanHalf/uRes.y;
            float rr = 0.5*(sec.x + sec.y), arc = atan(lp.y - sec.z, abs(lp.x))*rr;
            float sm = max(seamLine(lp.z, 0.85, 0.0035, px), seamLine(arc, rr*0.7854, 0.0035, px));
            float rv = max(rivetRow(arc, lp.z, 0.85, 0.018, 0.045, px), rivetRow(lp.z, arc, rr*0.7854, 0.018, 0.045, px));
            float dz0 = WS.y + 0.03, dz1 = min(WS.w + 0.06, dz0 + 1.05), dy0 = sec.z - sec.y*0.5, dy1 = sec.z + sec.y*0.8 + 0.03;
            if (abs(lp.x) > sec.x*0.55 && lp.z > dz0 - 0.05 && lp.z < dz1 + 0.05) {
              vec2 dq = abs(vec2(lp.z - 0.5*(dz0 + dz1), lp.y - 0.5*(dy0 + dy1))) - 0.5*vec2(dz1 - dz0, dy1 - dy0) + 0.06;
              float dd = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - 0.06;   // rounded-corner door outline
              sm = max(sm, 1.0 - smoothstep(0.004, 0.004 + px, abs(dd)));
              vec2 hq = abs(vec2(lp.z - (dz1 - 0.12), lp.y - (sec.z + 0.02))) - vec2(0.055, 0.012);
              if (max(hq.x, hq.y) < 0.0) { m.alb = vec3(0.55, 0.56, 0.58); m.metal = 0.8; m.rough = 0.25; }   // handle
            }
            // single engine: the cowling's oil-access door on top, and a ring of quarter-turn fasteners where the cowling
            // meets the cabin
            if (int(gM[0].z + 0.5) <= 1 && WS.x - gM[1].x > 0.8) {
              float zc = gM[1].x + 0.25 + 0.35*(WS.x - gM[1].x - 0.8);   // the door's centre, a little behind the spinner
              vec2 oq = abs(vec2(lp.x, lp.z - zc)) - vec2(0.12, 0.16) + 0.03;
              float od = length(max(oq, 0.0)) + min(max(oq.x, oq.y), 0.0) - 0.03;
              if (yr > 0.45) sm = max(sm, 1.0 - smoothstep(0.003, 0.003 + px, abs(od)));
              float zs = WS.x - 0.12;   // the cowling's aft seam
              sm = max(sm, (1.0 - smoothstep(0.003, 0.003 + px, abs(lp.z - zs)))*step(-0.2, yr));
              float fd = length(vec2(arc - 0.09*floor(arc/0.09 + 0.5), lp.z - zs + 0.02));
              rv = max(rv, (1.0 - smoothstep(0.005, 0.005 + px, fd))*smoothstep(0.012, 0.003, px)*step(-0.2, yr));
            }
            m.alb *= 1.0 - 0.32*sm; m.alb *= 1.0 + 0.12*rv; m.rough = mix(m.rough, 0.18, rv);
            // registration on the rear fuselage sides, "SX-" and three letters (the player's from uReg, the one
            // the towers call; each traffic aircraft its own), reading front to back from either side
            int nwr = int(gM[20].x + 0.5);
            float za = (nwr > 0 ? gM[20].z : WS.w) + 0.3, zb = gM[15].y - 0.15;
            if (zb - za > 0.6 && abs(lp.x) > sec.x*0.45 && lp.z > za && lp.z < zb) {
              vec3 sc = fusSection(0.5*(za + zb));
              float hc = min(0.34, min(sc.y*0.5, (zb - za)/(6.0*0.7))), adv = hc*0.7, len = 6.0*adv;
              float z0 = 0.5*(za + zb) - 0.5*len, base = sc.z + sc.y*0.32 - 0.5*hc;
              float u = lp.x < 0.0 ? lp.z - z0 : z0 + len - lp.z, v = lp.y - base;
              if (u > 0.0 && u < len && v > -0.2*hc && v < 1.2*hc) {
                int ci = int(u/adv);
                float hs = fract(sin(dot(vec3(gM[9].x, gM[0].x, gOwn ? 0.0 : float(gTrafK) + 1.0), vec3(12.9898, 78.233, 37.719)))*43758.5453);
                int ch = ci == 0 ? 83 : ci == 1 ? 88 : ci == 2 ? 45 : gOwn ? int(ci == 3 ? uReg.x : ci == 4 ? uReg.y : uReg.z) : 65 + int(fract(hs*float(3 + 7*ci))*25.99);
                float s = hc/29.0;   // metres per font pixel
                float soft = gTxtSoft; gTxtSoft = clamp(0.5*px/s, 0.09, 0.45);
                float cov = glyphCov(vec2((u - float(ci)*adv - 0.5*adv)/s + 13.92, v/s), ch)*smoothstep(hc*0.3, hc*0.1, px);
                gTxtSoft = soft;
                vec3 ink = dot(m.alb, vec3(0.3, 0.55, 0.15)) > 0.35 ? vec3(0.05, 0.055, 0.065) : vec3(0.92);
                m.alb = mix(m.alb, ink, cov);
              }
            }
          }
          float post = ck == 2 ? min(abs(lp.x) - 0.03, abs(abs(lp.x) - abs(E.x) - 0.42) - 0.035) : abs(lp.x) - 0.025;
          bool ws = lp.z > WS.x && lp.z < WS.y && lp.y > WS.z;
          float sideTop = sec.z + sec.y*0.78;
          bool sideW = lp.z > WS.y && lp.z < WS.w && lp.y > WS.z - 0.12 && lp.y < sideTop && abs(lp.x) > 0.3 && abs(lp.z - WS.y - 0.04) > 0.025;
          bool frame = (ws && post <= 0.0) || (lp.z > WS.x - 0.03 && lp.z < WS.w + 0.03 && lp.y > WS.z - 0.15 && lp.y < sideTop + 0.03 && abs(lp.x) > 0.3 && !sideW && lp.z > WS.y);
          if ((ws && post > 0.0) || sideW) { m.alb = vec3(0.012, 0.016, 0.02); m.rough = 0.03; m.metal = 0.2; }
          else if (frame) m.alb *= 0.55;
          int nw = int(gM[20].x + 0.5);
          if (nw > 0 && abs(lp.x) > sec.x*0.4 && lp.z > gM[20].y && lp.z < gM[20].z) {
            float pw = (gM[20].z - gM[20].y)/float(nw);
            vec2 wq = vec2(mod(lp.z - gM[20].y, pw) - pw*0.5, lp.y - (sec.z + gM[20].w));
            vec2 hs = gM[21].xy; float rr = min(hs.x, hs.y)*0.7;
            vec2 dq = abs(wq) - hs + rr; float wd = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - rr;
            if (wd < 0.0) { m.alb = vec3(0.02, 0.025, 0.03); m.rough = 0.05; m.emit = vec3(1.0, 0.85, 0.6)*uNight*0.5; }
            else if (wd < 0.022) m.alb *= 0.7;
          }
          if (ck == 2 && lp.z < WS.x && lp.z > WS.x - 2.0 && yr > 0.2) { m.alb = vec3(0.02); m.rough = 0.85; }
          if (int(gM[0].z + 0.5) == 0 && lp.z < gM[1].x + 0.3 && ln.z < -0.4 && abs(lp.x) > 0.11 && abs(lp.x) < sec.x*0.8 && abs(yr + 0.15) < 0.35) m.alb = vec3(0.02);
          if (int(gM[0].z + 0.5) == 1 && lp.z < gM[1].x + 0.7 && lp.y < sec.z - sec.y*0.45 && ln.z < -0.3) m.alb = vec3(0.02);
        }
      } else if (mid == 2) {
        float s = abs(lp.x); float k = clamp(s/gM[9].x, 0.0, 1.0);
        float ch = mix(gM[9].y, gM[9].z, k); float le = gM[9].w*k;
        float cc = (lp.z - gM[10].y - le)/ch;
        m.alb = gColBase*0.98;
        if (s > gM[9].x*0.9) m.alb = gColStripe;
        if (gM[19].w > 0.5 && cc < 0.045) { m.alb = vec3(0.06); m.rough = 0.6; }
        {   // ribs every 0.8 m, the spars' seams along the span, rivets down them; a fuel cap on top of each wing
          float px = t*2.0*uTanHalf/uRes.y;
          float sm = seamLine(s, 0.8, 0.0035, px)*step(0.05, cc);
          sm = max(sm, (1.0 - smoothstep(0.0035, 0.0035 + px, abs(cc - 0.22)*ch))*smoothstep(0.2, 0.05, px));
          sm = max(sm, (1.0 - smoothstep(0.0035, 0.0035 + px, abs(cc - 0.68)*ch))*smoothstep(0.2, 0.05, px));
          float rv = max(rivetRow(s, (cc - 0.22)*ch, 1e3, 0.0, 0.05, px), rivetRow(s, (cc - 0.68)*ch, 1e3, 0.0, 0.05, px));
          m.alb *= 1.0 - 0.3*sm; m.alb *= 1.0 + 0.12*rv;
          if (ln.y > 0.4) {
            float fc = length(vec2(s - gM[9].x*0.42, (cc - 0.32)*ch));
            if (fc < 0.045) { m.alb = vec3(0.6, 0.61, 0.63); m.metal = 0.85; m.rough = 0.3; if (fc > 0.036 || abs(lp.z - (gM[10].y + le + 0.32*ch)) < 0.005) m.alb *= 0.45; }
          }
        }
        if (gM[11].x > 0.5 && s < 1.6 && ln.y > 0.5 && cc < 0.7) m.alb *= 0.9;
      } else if (mid == 3) {
        m.alb = gColBase;
        float tailTop = gM[15].x + gM[14].x;
        // fin colour panel: its lower edge runs parallel to the fin's leading-edge sweep, matching the cheat line
        float fh = (lp.y - gM[15].x)/max(gM[14].x, 0.1), fle = gM[15].y + gM[14].w*clamp(fh, 0.0, 1.0);
        if (fh > 0.42 - 0.18*clamp((lp.z - fle)/max(gM[14].y, 0.1), 0.0, 1.0) && abs(lp.x) < 0.25) m.alb = gColStripe;
        if (lp.y > tailTop - 0.12 && abs(lp.x) < 0.25) m.alb = vec3(0.9);
      } else if (mid == 5) {
        m.alb = gColBase*0.96; m.rough = 0.3;
        if (int(gM[0].z + 0.5) == 4 && lp.z < gM[16].w + 0.3) { m.alb = vec3(0.85); m.metal = 1.0; m.rough = 0.18; }
      } else if (mid == 6) { m.alb = vec3(0.025); m.rough = 0.85; }
      else if (mid == 8) { m.alb = gM[11].x > 0.5 && length(lp.xz) > 1.2 && lp.y > -0.3 ? gColBase*0.95 : vec3(0.6, 0.61, 0.63); m.metal = 0.5; m.rough = 0.35; }
      else if (mid == 10) {
        m.alb = vec3(0.075); m.rough = 0.6;
        if (ln.z > 0.6) {
          bool pilot = lp.x*E.x >= 0.0;
          vec2 q = vec2(pilot ? lp.x - E.x : lp.x + E.x - coShift(E, ck), lp.y - (E.y - 0.32));
          float px = t*2.0*uTanHalf/uRes.y;   // panel metres per pixel
          vec4 pt = panelTex(q, px);
          if (!pilot && ck < 2 && q.x > 0.16) pt = vec4(0.0);   // copilot: the six-pack only
          if (pt.a > 0.003) { gDispPx = true; vec3 ic = pt.rgb/pt.a; float k = pt.a; m.alb = mix(m.alb, ic*0.25, k); m.emit = ic*(0.3 + 0.6*uNight)*k; m.rough = mix(m.rough, 0.12, k); }
        }
      }
      else if (mid == 11) { m.alb = lp.y < E.y - 1.0 ? vec3(0.08, 0.08, 0.09) : vec3(0.5, 0.49, 0.46); m.rough = 0.85; }
      else if (mid == 12) { m.alb = vec3(0.09, 0.1, 0.14)*(0.9 + 0.2*step(0.5, fract(lp.y*12.0))); m.rough = 1.0; }
      else if (mid == 13) { m.alb = vec3(0.035); m.rough = 0.4; }
      else if (mid == 14) { m.alb = vec3(0.018); m.rough = 0.95; }
      else if (mid == 16) { m.alb = gM[0].x > 9.0 ? gColBase*0.9 : gColStripe; m.metal = 0.5; m.rough = 0.2; }
      else if (mid == 17) { m.alb = vec3(0.09, 0.075, 0.06); m.metal = 0.7; m.rough = 0.55; }
      else if (mid == 18) { m.alb = vec3(0.1); m.emit = (lp.x < 0.0 ? vec3(1.0, 0.05, 0.02) : vec3(0.05, 1.0, 0.15))*(0.5 + 2.0*uNight); m.rough = 0.1; }
      else if (mid == 19) { m.alb = vec3(0.3, 0.02, 0.02); m.emit = vec3(1.0, 0.05, 0.02)*step(0.88, fract(uTime))*3.0; m.rough = 0.1; }
      else if (mid == 21) {
        vec2 fq = vec2(abs(lp.x) - gM[16].x, lp.y - gM[16].y);
        float bl = step(0.5, fract(atan(fq.y, fq.x)*22.0/6.2832 + length(fq)*2.0));
        m.alb = mix(vec3(0.04), vec3(0.22), bl); m.metal = 0.9; m.rough = 0.3;
        if (length(fq) < gM[16].z*0.25) m.alb = vec3(0.05);
      }
      if (mid == 94) { m.alb = vec3(0.07, 0.07, 0.075); m.metal = 0.7; m.rough = 0.3; m.emit = vec3(0.0); }   // fixture housing
      else if (mid >= 95 && mid <= 100) {   // lens: clear glossy dome over the lamp, tinted glass, glowing when lit
        int li = mid - 95; float tint = uLensD[li].w;
        m.alb = tint < 0.5 ? vec3(0.25, 0.02, 0.02) : tint < 1.5 ? vec3(0.02, 0.22, 0.06) : vec3(0.3);
        m.metal = 0.0; m.rough = 0.04; m.nrm = vec3(0.0, 0.0, 1.0);
        m.emit = uLensC[li].rgb;
      }
      else if (mid >= 101 && mid <= 104) {   // traffic fixtures: nav lights steady, strobes and beacon flashing
        float k = float(gTrafK), lk = 3.0 + 40.0*uNight;
        bool strobe = fract((uTime*0.77 + k*0.13)/1.3) < 0.05/1.3, bcn = fract(uTime + k*0.37) < 0.1;
        m.metal = 0.0; m.rough = 0.04; m.nrm = vec3(0.0, 0.0, 1.0);
        if (mid == 101) { m.alb = vec3(0.25, 0.02, 0.02); m.emit = vec3(1.0, 0.08, 0.04)*lk + (strobe ? vec3(20.0) : vec3(0.0)); }
        else if (mid == 102) { m.alb = vec3(0.02, 0.22, 0.06); m.emit = vec3(0.1, 1.0, 0.25)*lk + (strobe ? vec3(20.0) : vec3(0.0)); }
        else if (mid == 103) { m.alb = vec3(0.3); m.emit = vec3(1.0, 0.97, 0.9)*lk; }
        else { m.alb = vec3(0.25, 0.02, 0.02); m.emit = bcn ? vec3(30.0, 1.5, 0.6) : vec3(0.0); }
      }
      else if (mid >= 80 && mid < 94) shadeWraith(m, mid, lp, ln, t);
      else if (mid >= 61 && mid < 80 && int(gM[0].z + 0.5) == 6) { gPixM = t*uTanHalf*2.0/uRes.y; shadeWraithCockpit(m, mid, lp, ln, E.xyz); }   // XR-11 cockpit
      else if (mid >= 30 && mid < 60) {  // XR-9 research jet surfaces
        vec3 nT; vec4 tx;
        float pulse = 0.75 + 0.25*sin(uTime*2.5);
        if (mid == 30 || mid == 31) {
          tx = triSample(lp, ln, M_PAINT, 0.7, nT);
          m.alb = gColBase*tx.rgb*1.6; m.rough = 0.55; m.metal = 0.08; m.nrm = nT;   // matte radar-absorbent coating: doesn't mirror the sky
          vec2 pl = abs(fract(lp.xz/vec2(0.9, 1.3)) - 0.5);           // panel seams
          if (max(pl.x, pl.y) > 0.49) m.alb *= 0.55;
          if (mid == 31 && abs(lp.x) > 4.6) m.alb = mix(m.alb, gColStripe*0.6, 0.6);
          if (mid == 30 && lp.z < -8.0) m.alb = vec3(0.03);              // radar nose cap
        } else if (mid == 32) { m.alb = vec3(0.3, 0.2, 0.06); m.metal = 0.95; m.rough = 0.06; }
        else if (mid == 33) {
          tx = triSample(lp, ln, M_METAL, 0.8, nT); m.alb = tx.rgb*vec3(0.2, 0.19, 0.2); m.metal = 0.6; m.rough = 0.5; m.nrm = nT;
          float heat = gCtl.w*gCtl.w;
          m.alb = mix(m.alb, vec3(0.16, 0.11, 0.17), 0.4*heat);   // heat-tinted titanium
        } else if (mid == 34) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = gColStripe*(1.2 + 2.0*uNight)*pulse; }
        else if (mid == 36) {   // turbine stage and tail cone: dark heat-blued metal glowing with the exhaust heat
          float ab = gFlame.y, sp = gFlame.x;
          m.alb = vec3(0.012, 0.011, 0.012); m.metal = 0.3; m.rough = 0.75;
          m.emit = vec3(1.0, 0.32, 0.08)*(0.15*sp*sp) + mix(vec3(1.0, 0.45, 0.12), vec3(1.0, 0.8, 0.55), ab)*ab*3.5;
        }
        else if (mid == 37) {   // afterburner internals and liner: scorched metal, red-hot in reheat towards the turbine
          float ab = gFlame.y, sp = gFlame.x, deep = smoothstep(0.6, -0.35, dot(lp - vec3(sign(lp.x)*0.82, -0.12, 7.75), vec3(0.0, -sin(gFlame.z), cos(gFlame.z))) - 0.5);
          m.alb = vec3(0.014, 0.013, 0.012); m.metal = 0.2; m.rough = 0.85;   // soot-black
          m.emit = vec3(1.0, 0.3, 0.07)*(0.05*sp*sp + 1.4*ab)*deep;
        }
        else if (mid == 40) {  // sealed pod: carbon weave between structural ribs
          vec2 wv = floor(vec2(lp.x + lp.z, lp.y - lp.z)*55.0);
          tx = triSample(lp, ln, M_FABRIC, 4.0, nT); m.nrm = mix(vec3(0.0, 0.0, 1.0), nT, 0.3);
          m.alb = vec3(0.03, 0.032, 0.036)*(0.8 + 0.4*mod(wv.x + wv.y, 2.0)); m.rough = 0.3; m.metal = 0.2;
          float rib = abs(fract((lp.z - E.z)*4.0) - 0.5);
          if (rib > 0.46) { m.alb = vec3(0.07, 0.075, 0.08); m.metal = 0.7; m.rough = 0.3; }
          if (abs(lp.y - (E.y - 0.18)) < 0.004) m.emit = gColStripe*1.4*pulse;
        }
        else if (mid >= 41 && mid <= 43) { m.alb = vec3(0.0); m.rough = 0.05; }
        else if (mid == 44) {  // bezels and consoles: satin composite with machined edges and fasteners
          vec2 hx = lp.xz*45.0 + vec2(lp.y*30.0);
          tx = triSample(lp, ln, M_PLASTIC, 0.4, nT); m.nrm = nT;
          m.alb = vec3(0.028, 0.03, 0.034)*(0.9 + 0.2*step(0.5, fract(hx.x + floor(hx.y)*0.5)))*(0.7 + 0.6*tx.r); m.rough = mix(0.42, tx.a, 0.4); m.metal = 0.35;
          vec3 qd = lp - E.xyz; float rr = length(qd.xz), an = atan(qd.x, -qd.z);
          if (abs(fract(an*9.0) - 0.5) < 0.025 && rr < 0.7) m.alb *= 2.2;                 // panel seams
          if (length(vec2(fract(an*18.0) - 0.5, (qd.y - 0.36)*90.0)) < 0.12) { m.alb = vec3(0.35); m.metal = 1.0; m.rough = 0.25; }  // screws
        }
        else if (mid == 45 || mid == 52 || mid == 53) {  // multi-function displays
          vec3 qd = lp - E.xyz; int page; vec2 uv;
          if (mid == 45) {
            float rr = length(qd.xz), an = atan(qd.x, -qd.z);
            float k = clamp(floor(an/0.42 + 0.5), -2.0, 2.0);
            page = int(k) + 2; uv = vec2((an - k*0.42)*rr/0.07, (rr - 0.575)/0.05);
          } else {
            vec3 cq = vec3(abs(qd.x) - 0.52, qd.y + 0.44, qd.z - 0.08);
            page = mid == 52 ? 5 : 6; uv = vec2((cq.x - 0.02)/0.075*sign(qd.x), -(cq.z + 0.2)/0.06);
          }
          // 4x supersampled over this pixel's footprint on the panel: crisp at any display resolution
          float fp = t*uTanHalf*2.0/uRes.y/(mid == 45 ? 0.07 : 0.06);   // the true pixel size: TAA smooths the foreshortened axis
          gAA = fp*0.55;
          vec3 sc = pageTex(page, uv, fp);
          float edge = smoothstep(1.0, 0.92, max(abs(uv.x), abs(uv.y)));
          sc = sc*edge + vec3(0.01, 0.03, 0.04)*edge;                                         // dark-blue backlight
          m.alb = vec3(0.01); m.rough = 0.06; m.metal = 0.0; m.emit = sc*1.5; gDispPx = true;
        }
        else if (mid == 46) { tx = triSample(lp, ln, M_LEATHER, 0.3, nT); m.alb = vec3(dot(tx.rgb, vec3(0.33)))*vec3(0.3, 0.32, 0.36); m.rough = tx.a; m.nrm = nT;
          if (abs(abs(lp.x - E.x) - 0.16) < 0.005) m.emit = gColStripe*0.9*pulse;
          if (lp.y > E.y + 0.12 && abs(lp.x - E.x) < 0.1 && ln.z > 0.5) m.emit = gColStripe*1.2; }
        else if (mid == 47) { tx = triSample(lp, ln, M_RUBBER, 0.1, nT); m.alb = tx.rgb*0.3; m.rough = tx.a; m.metal = 0.1; m.nrm = nT; if (lp.y > E.y - 0.24 && ln.y > 0.3) m.emit = vec3(1.0, 0.45, 0.1)*0.8; }
        else if (mid == 48) { m.alb = vec3(0.1); m.emit = gColStripe*1.1*pulse; }
        else if (mid == 49) {  // annunciator strip: GEAR, BRK, AB, TVC, MACH, G, LOW ALT, SYS
          vec3 qd = lp - E.xyz; float an = atan(qd.x, -qd.z);
          int cell = int(clamp(floor((an + 0.62)/0.155), 0.0, 7.0));
          float cx = abs(fract((an + 0.62)/0.155) - 0.5), cy = abs(qd.y - 0.348)/0.016;
          bool inCell = cx < 0.42 && cy < 0.75;
          float ab = smoothstep(0.85, 1.0, uHud3.x);
          vec3 on = cell == 0 ? (uHud2.w > 0.5 ? vec3(0.2, 1.0, 0.3) : vec3(0.0)) :
                    cell == 1 ? vec3(0.0) :
                    cell == 2 ? vec3(1.0, 0.5, 0.1)*ab :
                    cell == 3 ? vec3(0.2, 0.7, 1.0)*step(0.1, abs(uHud2.z)) :   // thrust vectoring past 9 deg
                    cell == 4 ? vec3(0.4, 0.6, 1.0)*step(1.0, uHud.w) :
                    cell == 5 ? vec3(1.0, 0.15, 0.1)*step(9.0, abs(uHud2.x))*step(0.5, fract(uTime*3.0)) :
                    cell == 6 ? vec3(1.0, 0.15, 0.1)*step(uHud3.w, 60.0)*step(0.5, uHud2.z*0.0 + 1.0 - uHud2.w) :
                                vec3(0.2, 1.0, 0.3)*0.6;
          m.alb = vec3(0.02); m.rough = 0.1;
          m.emit = inCell ? on*2.0 + vec3(0.025, 0.03, 0.035) : vec3(0.0);
        }
        else if (mid == 54) {  // backlit keys
          vec3 qd = lp - E.xyz; vec3 cq = vec3(abs(qd.x) - 0.52, qd.y + 0.44, qd.z - 0.08) - vec3(0.0, 0.055, 0.12);
          vec2 cell = floor(cq.xz/0.032 + 0.5); float hk = hash2i(ivec2(cell) + ivec2(qd.x < 0.0 ? 11 : 37, 5));
          vec3 kc = hk < 0.15 ? vec3(1.0, 0.55, 0.15) : hk < 0.25 ? vec3(0.3, 1.0, 0.5) : gColStripe*0.6;
          m.alb = vec3(0.04); m.rough = 0.4; m.emit = ln.y > 0.6 ? kc*(0.12 + 0.5*step(0.85, hk)*step(0.5, fract(uTime*0.7 + hk*3.0))) : vec3(0.0);
        }
        else if (mid == 55) {  // overhead panel face: status LEDs beside each switch
          vec3 qd = lp - E.xyz - vec3(0.0, 0.5, -0.32); qd.yz = rot2(qd.yz, 0.55);
          vec2 c2 = floor(qd.xz/vec2(0.05, 0.06) + 0.5); vec2 f2 = qd.xz - c2*vec2(0.05, 0.06);
          float hk = hash2i(ivec2(c2) + ivec2(3, 9));
          float led = step(length(f2 - vec2(0.016, -0.018)), 0.0035);
          m.alb = vec3(0.025); m.rough = 0.5;
          m.emit = led*(hk < 0.7 ? vec3(0.2, 1.0, 0.35) : vec3(1.0, 0.6, 0.15)*step(0.5, fract(uTime + hk)))*1.5;
          if (abs(f2.y + 0.03) < 0.0015 && abs(f2.x) < 0.02) m.emit = vec3(0.4, 0.5, 0.55)*0.6;   // engraved labels
        }
        else if (mid == 56) { tx = triSample(lp, ln, M_FABRIC, 0.08, nT); m.alb = tx.rgb*vec3(0.18, 0.19, 0.21); m.rough = 0.9; m.nrm = nT; if (abs(fract(lp.y*40.0) - 0.5) < 0.04) m.emit = vec3(1.0, 0.45, 0.1)*0.25; }
        else if (mid == 57) { m.alb = vec3(0.1); m.emit = vec3(1.0, 0.5, 0.15)*1.8; }
        else if (mid == 58) { m.alb = vec3(0.7, 0.66, 0.6); m.rough = 0.3; m.emit = vec3(1.0, 0.8, 0.58)*0.55; }   // ceiling light diffusers
      }
      else if (mid >= 60) {  // light-aircraft / airliner cockpit parts (PBR texture sets)
        vec3 nT; vec4 tx;
        if (mid == 60) { tx = triSample(lp, ln, M_METAL, 0.25, nT); m.alb = tx.rgb*vec3(0.62, 0.63, 0.65); m.metal = 0.9; m.rough = clamp(tx.a*0.6, 0.15, 0.5); m.nrm = nT; }
        else if (mid == 61) { tx = triSample(lp, ln, M_RUBBER, 0.12, nT); m.alb = tx.rgb*0.6; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 63) {
          tx = triSample(lp, ln, ck == 2 ? M_LEATHER : M_PLASTIC, ck == 2 ? 0.3 : 0.35, nT);
          m.alb = tx.rgb*(ck == 2 ? vec3(0.3, 0.32, 0.36) : vec3(0.5, 0.48, 0.45)); m.rough = tx.a; m.nrm = nT;
          if (abs(fract(lp.y*6.0) - 0.5) < 0.012) m.alb *= 0.6;                      // panel seams / stitching
        }
        else if (mid == 64) {   // light lenses: glareshield strip (cool white), dome and map lights (warm)
          bool glare = lp.y < E.y;
          m.alb = vec3(0.65, 0.62, 0.58); m.rough = 0.25;
          m.emit = (glare ? vec3(1.0, 0.86, 0.66) : vec3(1.0, 0.72, 0.45))*(0.25 + 1.8*uNight);
        }
        else if (mid == 65) {   // radio / transponder stack: three units with amber frequency windows and knobs
          m.alb = vec3(0.03); m.rough = 0.45; m.metal = 0.3;
          vec2 rq = vec2(lp.x, lp.y - (E.y - 0.505));
          if (ln.z > 0.5) {
            float row = clamp(floor((rq.y + 0.06)/0.04), 0.0, 2.0); float ry = rq.y + 0.06 - row*0.04 - 0.02;
            if (abs(ry) < 0.0175 && abs(rq.y) < 0.059) m.alb = vec3(0.05);
            vec2 dw = vec2(rq.x + 0.045, ry);
            if (abs(dw.x) < 0.055 && abs(dw.y) < 0.009) {               // 7-segment-ish frequency digits
              vec2 dc = vec2(fract((dw.x + 0.055)/0.0122), (dw.y + 0.009)/0.018);
              float on = step(0.3, hash2i(ivec2(floor((dw.x + 0.055)/0.0122), int(row)*7 + int(dc.y*3.0))));
              float seg = step(abs(dc.x - 0.5), 0.32)*(step(abs(dc.y - 0.5), 0.42))*max(step(abs(dc.x - 0.5), 0.12), step(abs(fract(dc.y*2.0) - 0.5), 0.12));
              m.emit = vec3(1.0, 0.55, 0.15)*seg*on*1.6; m.alb = vec3(0.01);
            }
            vec2 kq = vec2(rq.x - 0.075, ry);
            if (length(kq) < 0.013) { m.alb = vec3(0.12); m.rough = 0.35; m.metal = 0.6; if (abs(kq.x) < 0.0012 && kq.y > 0.0) m.alb = vec3(0.8); }
          }
        }
        else if (mid == 66) { tx = triSample(lp, ln, M_PLASTIC, 0.2, nT); m.alb = tx.rgb*0.12; m.rough = mix(tx.a, 0.45, 0.5); m.nrm = nT; }
        else if (mid == 67) {   // centre engine / systems display (glass cockpits)
          vec2 uv = vec2(lp.x/0.085, (lp.y - (E.y - 0.31))/0.085);
          vec3 sc = ln.z > 0.5 ? pageTex(0, uv, t*2.0*uTanHalf/uRes.y/0.085)*smoothstep(1.0, 0.92, max(abs(uv.x), abs(uv.y))) : vec3(0.0);
          m.alb = vec3(0.01); m.rough = 0.06; m.emit = (sc + vec3(0.005, 0.012, 0.02))*1.3;
        }
        else if (mid == 68) { tx = triSample(lp, ln, M_PLASTIC, 0.2, nT); m.alb = tx.rgb*vec3(0.55, 0.05, 0.04); m.rough = 0.35; m.nrm = nT; }
        else if (mid == 69) { tx = triSample(lp, ln, M_FABRIC, 0.08, nT); m.alb = tx.rgb*vec3(0.2, 0.21, 0.24); m.rough = 0.9; m.nrm = nT; }
      }
      else {
        vec3 nT; vec4 tx;
        if (mid == 1 || mid == 2 || mid == 3 || mid == 5) {
          if (m.rough > 0.1) { tx = triSample(lp, ln, M_PAINT, 0.9, nT); m.alb *= tx.rgb*1.03; m.rough = mix(m.rough, tx.a, 0.6); m.nrm = nT; }
        }
        else if (mid == 6) { tx = triSample(lp, ln, M_RUBBER, 0.35, nT); m.alb = tx.rgb; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 8 || mid == 16 || mid == 17) { tx = triSample(lp, ln, M_METAL, 0.6, nT); m.alb *= tx.rgb*1.4; m.rough = mix(m.rough, tx.a, 0.5); m.nrm = nT; }
        else if (mid == 10 && m.emit.x + m.emit.y + m.emit.z <= 0.0) { tx = triSample(lp, ln, M_PLASTIC, 0.25, nT); m.alb = tx.rgb*0.9; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 11) {   // carpeted floor, fabric headliner, moulded plastic side walls
          bool flr = lp.y < E.y - 1.0, roof = lp.y > E.y + 0.12;
          tx = triSample(lp, ln, flr ? M_CARPET : (roof ? M_FABRIC : M_PLASTIC), flr ? 0.4 : (roof ? 0.25 : 0.5), nT);
          m.alb = flr ? tx.rgb*0.8 : tx.rgb*(roof ? vec3(0.95, 0.93, 0.88) : (ck == 2 ? vec3(0.62, 0.62, 0.62) : vec3(0.85, 0.82, 0.77))); m.rough = flr || roof ? 0.95 : tx.a; m.nrm = nT*0.7;
        }
        else if (mid == 12) {   // seats: leather (glass cockpits) or cloth, with stitched panels
          tx = triSample(lp, ln, ck == 2 ? M_LEATHER : M_FABRIC, 0.3, nT); m.alb = tx.rgb*(ck == 2 ? 1.2 : 1.0); m.rough = tx.a; m.nrm = nT;
          if (abs(fract(lp.y*7.0 + lp.z*2.0) - 0.5) < 0.02) m.alb *= 0.65;
        }
        else if (mid == 13) { tx = triSample(lp, ln, M_PLASTIC, 0.2, nT); m.alb = tx.rgb*0.6; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 14) { tx = triSample(lp, ln, M_CARPET, 0.15, nT); m.alb = tx.rgb*0.22; m.rough = 0.95; m.nrm = nT; }   // anti-glare flocking
      }
      if (uWreck > 0 && !trafHit) {  // fire-blackened, buckled skin with a few glowing embers near the breaks
        float burn = vnoise(lp.xz*2.3 + lp.y*1.7) + 0.5*vnoise(lp.yz*5.1);
        float cut = 1.0 - smoothstep(0.0, 0.6, -sdBox(lp - uPcC[gPI], uPcH[gPI]));
        float k = clamp(0.25 + 0.55*burn + 0.5*cut, 0.0, 1.0);
        m.alb = mix(m.alb, vec3(0.025, 0.022, 0.02), k); m.rough = mix(m.rough, 0.95, k); m.metal *= 1.0 - k;
        m.emit += vec3(1.0, 0.32, 0.06)*pow(clamp(burn*cut*0.9, 0.0, 1.0), 5.0)*(1.5 + sin(uTime*7.0 + lp.x*9.0))*3.0;
      }
      n = applyTS(n, m.nrm, interior ? 0.35 : 0.12);
      bool podMat = mid >= 40 && mid < 80 && int(gM[0].z + 0.5) >= 5 && !trafHit;   // research jets only: light aircraft use ids 60+ for their cockpits
      // sun shadow: the terrain's is one value for the whole intact airframe (from the CPU); inside the cabin the
      // self-shadow ray only needs to get out through the cabin and the wing above it, not cross the whole aircraft
      float sh = 0.0;
      if (sunVis > 0.0 && !podMat) {
        float tsh = (trafHit || uWreck > 0) ? terrainShadow(p, uSunDir, t) : uPlaneTSh;
        if (tsh > 0.0 && !trafHit) { gShMax = interior ? 3.5 : 1e9; tsh *= planeShadow(p + n*0.02, uSunDir); gShMax = 1e9; }
        // (and the scenery's: an airframe parked by a hangar or under trees sits in the same shadow as the ground
        // round it - the aircraft aren't in the scenery cascades, so this never shadows the airframe itself)
        sh = tsh > 0.0 ? tsh*cloudShadow(p)*entShadow(p, n) : 0.0;
      }
      if (podMat) {  // sealed research cockpit: lit only by its modelled fixtures, low and moody
        mat3 inv = transpose(gPR);
        col = (int(gM[0].z + 0.5) == 6 ? wraithPodLight(lp, inv*n, inv*(-rd), m, E.xyz) : podLight(lp, inv*n, inv*(-rd), m, E.xyz))*interiorAO(lp, ln) + m.emit;
      } else if (interior) {
        vec3 v = -rd; mat3 inv = transpose(gPR);
        float ao = interiorAO(lp, ln);
        vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
        col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*sh*3.2)
            + (m.alb*(1.0 - m.metal)*(ambientLight(n)*0.4 + ambientLight(vec3(0.0, 1.0, 0.0))*0.2)
               + skyColor(normalize(reflect(rd, n) + vec3(0.0, 0.3, 0.0)))*F*(1.0 - m.rough)*(1.0 - m.rough)*0.35)*ao
            + cabinLight(lp, inv*n, inv*v, m, E.xyz, gM[21].w, E.w)*ao + m.emit;
      } else {
        col = shadeSurface(p, n, rd, m, sh);
        vec3 h = normalize(-rd + uSunDir);
        col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0*float(mid <= 5);
      }
    }
    if (!pod) col = applyFog(col, ro, rd, t);
    if (trafHit) loadMain();
  }
  // propeller discs (motion-blurred), composited over scene
  if (uPlaneOn == 1 && uWreck == 0) {
    mat3 inv = transpose(uPlaneRot);
    vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
    for (int i=0;i<2;i++){
      if (i >= uPropCount) break;
      vec4 pr = uProp[i];
      if (abs(ld.z) < 1e-4) continue;
      float tp = (pr.z - lo.z)/ld.z;
      if (tp < 0.0 || tp > t) continue;
      vec3 hp = lo + ld*tp - pr.xyz;
      float r = length(hp.xy);
      if (r > pr.w) continue;
      float blades = uPr.z; float blur = uPr.y;
      float ang = atan(hp.y, hp.x) - uPr.x;
      float bl = smoothstep(0.86, 0.95, cos(blades*ang*0.5*2.0)) * smoothstep(pr.w, pr.w*0.9, r);
      float a = mix(bl, 0.10 + 0.08*smoothstep(0.6, 1.0, cos(blades*ang)) + 0.25*smoothstep(pr.w*0.95, pr.w, r), blur);
      vec3 pc = vec3(0.04)*(uSunCol*max(uSunDir.y,0.0) + 0.2) + vec3(0.6,0.6,0.1)*smoothstep(pr.w*0.9, pr.w, r)*0.3;
      col = mix(col, pc, clamp(a, 0.0, 1.0)*0.85);
    }
  }
  // research jet exhaust plumes (additive, depth-limited by the scene)
  if (uPlaneOn == 1 && uWreck == 0 && uVapor.x > 0.01) { vec3 c0 = col; col = vaporCone(col, ro, rd, t, jitter); if (dot(abs(col - c0), vec3(1.0)) > 0.02) taaFlag = min(taaFlag, 0.2); }
  // (the research jets' flames are laid over the clouds below, not under them: the clouds on this ray lie mostly
  // beyond the aircraft, and composited afterwards they covered the reheat and the pods' plasma)
  vec3 plE = vec3(0.0); float plT = 1.0;
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && int(gM[0].z + 0.5) == 5) { plE = jetPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  if (uPlaneOn == 1 && uWreck == 0 && gPS.w < 0.5 && int(gM[0].z + 0.5) == 6) { plE = wraithPlumes(ro, rd, t, jitter); plT = gPlumeT; }
  bool plume = plE.r + plE.g + plE.b > 0.01 || plT < 0.99;
  if (plE.r + plE.g + plE.b > 0.03) taaFlag = min(taaFlag, 0.2);
  if (!pod && uFxBeams + uFxBombs + uFxBlasts > 0) col = weaponsFx(col, ro, rd, t);
  // clouds (a cloaked craft: the skin first, then one cloud march along the whole camera ray through it)
  if (cloak) { col = cloakSkin(col, ckN, rd0, ckLp, ckLp.z - uWr[6].y + 0.8); col = applyFog(col, ro0, rd0, ckT); }
  // ordinary world pixels leave their clouds to the quarter-resolution cloud pass (composited before the TAA); the
  // cabin marches them here
  bool wrCk = cockpitView && int(gM[0].z + 0.5) == 6;
  bool cloudLater = uCloudSplit == 1 && !pod && !wrCk && !plume;   // (a plume pixel marches its own, to go under the flame)
  vec4 cl = pod || cloudLater ? vec4(0.0, 0.0, 0.0, 1.0) : traceClouds(cloak ? ro0 : ro, cloak ? rd0 : rd, cloak ? t + ckT : t, jitter);
  col = col*cl.a + cl.rgb;
  if (plume) col = col*plT + plE;
  oCloudMask = cloudLater ? 1.0 : 0.0;
  if (wrCk) col += wrHolo(roV, rdV, pod ? t : h0.x);   // the hologram floats inside the cabin, in front of everything
  if (cloak) { t += ckT; taaFlag = 0.5; }
#ifdef WR_CLIPATLAS
  col = wrClipAtlas(vUV); t = 1.0;
#endif
#ifdef WR_CLIPDEBUG
  if (wrCk && (onScr || pod)) {   // red: a display or gauge surface inside other cockpit geometry
    vec3 hp = transpose(uPlaneRot)*(uCamPos + rdV*h0.x - uPlanePos);
    int m0 = int(h0.y + 0.5);
    if ((m0 >= 41 && m0 <= 43) || (m0 >= 61 && m0 <= 63) || m0 == 68 || m0 == 69 || m0 == 76) {
      if (wrClip(hp, m0) < -0.0015) col = vec3(1e3, 0.0, 0.0); else col = vec3(0.0, 0.05, 0.0) + col*0.3;
    }
  }
#endif
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  if (gDispPx && taaFlag > 0.4 && taaFlag < 0.6) taaFlag = 0.55;
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taaFlag);
  oDepth = t;
#ifdef COST_MAP
  oColor = gCost;   // analysis build: the work counters instead of the colour
#endif
}
