//! kRtPrims
//! Analytic primitives: boxes, cylinders, convex solids, the debris chunks and the (unused) airport boxes.
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
