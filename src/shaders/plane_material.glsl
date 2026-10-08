//! kPlaneMaterial
//! The aircraft's materials: liveries, skin detail, registrations, windows, lights, cabins and displays, from the
//! material id and the body-space point.

// Procedural wheel finish, evaluated once for a visible surface. No new material IDs.
// Local coordinates mirror the existing wheel deployment and steering equations.
vec4 gearWheelFrame(vec3 p, out float halfWidth, out bool braked){
  vec4 G0 = gM[18], G1 = gM[19];
  int kind = int(gM[0].y + 0.5), engine = int(gM[0].z + 0.5);
  bool research = RESEARCH_ON && engine >= 5;
  float gh = G1.x;
  float wr = research ? 0.38 : G0.y;
  float mh = research ? JT_TYRE_H : kind == 3 ? 0.11 : kind == 0 ? 0.065 : kind == 1 ? 0.09 : kind == 2 ? 0.14 : 0.10;
  float nwr = research ? 0.33 : kind == 3 ? wr*0.75 : wr*0.85;
  float nh = research || kind == 3 ? 0.07 : 0.055;
  vec3 mq, nq;
  if (research) {   // (the research jets' wheels from their parts' poses: plane_parts.glsl jtMainPose, jtNosePose)
    Pose M = jtMainPose(partMirror(p.x < 0.0 ? -1.0 : 1.0)), N = jtNosePose();
    mq = transpose(M.R)*(p - M.T) - vec3(G0.x, wr - gh, G0.z);
    nq = transpose(N.R)*(p - N.T) - vec3(0.0, nwr - gh, G0.w); nq.x = abs(nq.x) - 0.10;
  } else {   // (the wheels' rest frames from their parts' poses, as they fold: plane_parts.glsl gearPartPose)
    Pose X = gearPartPose(PT_GEAR_MAIN, vec2(p.x < 0.0 ? -1.0 : 1.0, 0.0));
    mq = transpose(X.R)*(p - X.T) - vec3(G0.x, wr - gh, G0.z);
    if (kind == 3) mq.x = abs(mq.x) - 0.22;
    if (G1.z > 0.5) {
      nwr = 0.10; nh = 0.035;
      nq = p - vec3(0.0, 0.0, G1.y); nq.xz = rot2(nq.xz, -gPS.z);
      nq.y += gh - 0.11*gM[0].x - nwr;
    } else {
      Pose N = gearPartPose(PT_GEAR_NOSE, vec2(0.0));
      float ns = max(length(N.R[0]), 1e-3);
      nq = transpose(N.R/ns)*(p - N.T)/ns - vec3(0.0, nwr - gh, 0.0);
      if (kind == 3) nq.x = abs(nq.x) - 0.15;
    }
  }
  braked = dot(mq, mq) < dot(nq, nq);
  halfWidth = braked ? mh : nh;
  return braked ? vec4(mq, wr) : vec4(nq, nwr);
}
Mat gearFinish(vec3 p, int material, Mat m, float footprint){
  if (gPS.x < 0.06) return m;
  float h; bool braked; vec4 f = gearWheelFrame(p, h, braked);
  vec3 q = f.xyz;
  float angle = braked ? (p.x < 0.0 ? gWheel.x : gWheel.y) : gWheel.z;
  q.yz = rot2(q.yz, -angle); // inverse SPIN in the deployed/steered wheel rest frame
  float r = f.w, rad = length(q.yz);
  if (abs(q.x) > h + 0.065 || rad > r + 0.025) return m;
  float px = max(0.0008, footprint);
  if (material == 6) {
    float channel = 1.0 - smoothstep(h*0.028, h*0.028 + px, abs(abs(q.x) - h*0.32));
    channel *= smoothstep(r*0.76, r*0.9, rad);
    float wall = 1.0 - smoothstep(r*0.006, r*0.006 + px, abs(rad - r*0.77));
    wall *= smoothstep(h*0.65, h*0.9, abs(q.x));
    m.alb = mix(vec3(0.045, 0.047, 0.050), vec3(0.008), max(channel, wall*0.55));
    m.rough = 0.88;
  } else if (rad < r*0.67 && abs(q.x) > h - 0.025) {
    m.alb = vec3(0.55, 0.57, 0.60); m.metal = 0.85; m.rough = 0.28;
    float recess = smoothstep(r*0.25, r*0.25 + px, rad)*(1.0 - smoothstep(r*0.46, r*0.46 + px, rad));
    m.alb *= 1.0 - recess*0.62;
    float angle = atan(q.z, q.y), sector = 1.04719755;
    angle -= sector*floor(angle/sector + 0.5);
    vec2 bolt = rad*vec2(cos(angle), sin(angle)) - vec2(r*0.39, 0.0);
    float head = 1.0 - smoothstep(r*0.045, r*0.045 + px, length(bolt));
    m.alb = mix(m.alb, vec3(0.72), head); m.rough = mix(m.rough, 0.20, head);
    if (braked && q.x < -h && rad > r*0.55) { m.alb = vec3(0.24, 0.25, 0.27); m.rough = 0.55; }
  }
  return m;
}

// The airframe material at a hit. p: world point, t: its distance (the pixel footprint), mid: the material id the
// distance field returned (11 becomes 1 on the cabin top from outside), trafHit: a traffic aircraft (the globals hold
// it). Out: the material, the shading normal (world), the body-space point and normal, and the two lighting classes:
// a cabin interior (sun through the windows and the cabin fixtures) or a sealed research pod (fixtures only).
void planeMaterialN(vec3 p, vec3 rd, float t, inout int mid, bool trafHit, vec3 lnIn, out Mat m, out vec3 n, out vec3 lp, out vec3 ln, out bool interior, out bool podMat);
void planeMaterial(vec3 p, vec3 rd, float t, inout int mid, bool trafHit, out Mat m, out vec3 n, out vec3 lp, out vec3 ln, out bool interior, out bool podMat){
  vec3 lp0 = gPC + transpose(gPR)*(p - gPP);
  planeMaterialN(p, rd, t, mid, trafHit, planeNormal(lp0), m, n, lp, ln, interior, podMat);
}
// (the same with the body-space normal given: the mesh pass carries it per vertex, the march computes it from the field)
void planeMaterialN(vec3 p, vec3 rd, float t, inout int mid, bool trafHit, vec3 lnIn, out Mat m, out vec3 n, out vec3 lp, out vec3 ln, out bool interior, out bool podMat){
  mat3 inv = transpose(gPR);
  lp = gPC + inv*(gRelSet ? gRel + (uCamPos - gPP) : p - gPP);   // (plane_common.glsl gRelSet)
  ln = lnIn;
  n = gPR*ln;
  if (mid == 11) { vec3 sc = fusSection(lp.z); vec3 rad = vec3(lp.x, lp.y - sc.z, 0.0); if (dot(ln, rad) > 0.55*length(rad) && lp.y > gM[22].y - 0.9) mid = 1; }
  m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
  m.alb = gColBase; m.rough = 0.28;
  interior = (mid >= 10 && mid <= 14) || (mid >= 40 && mid < 94);   // (94+: the light fixtures, outside)
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
  else if (RESEARCH_ON && mid >= 80 && mid < 94) shadeWraith(m, mid, lp, ln, t);
  else if (RESEARCH_ON && mid >= 61 && mid < 80 && int(gM[0].z + 0.5) == 6) {   // XR-40 cockpit
    gPixM = t*uTanHalf*2.0/uRes.y; gPixG = gPixM/max(abs(dot(ln, transpose(uPlaneRot)*rd)), 0.2);
    shadeWraithCockpit(m, mid, lp, ln, E.xyz);
  }
  else if (RESEARCH_ON && mid >= 30 && mid < 60) {  // XR-30 research jet surfaces
    gPixM = t*uTanHalf*2.0/uRes.y; gPixG = gPixM/max(abs(dot(ln, transpose(uPlaneRot)*rd)), 0.2);
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
      vec2 wv = vec2(lp.x + lp.z, lp.y - lp.z)*27.5;   // (a checker of 1.8 cm squares: two square waves, crossed)
      float wa = aaSquare(wv.x, gPixG*39.0), wb = aaSquare(wv.y, gPixG*39.0);
      tx = triSample(lp, ln, M_FABRIC, 4.0, nT); m.nrm = mix(vec3(0.0, 0.0, 1.0), nT, 0.3);
      m.alb = vec3(0.03, 0.032, 0.036)*(0.8 + 0.4*(wa + wb - 2.0*wa*wb)); m.rough = 0.3; m.metal = 0.2;
      float rib = aaLines((lp.z - E.z)*4.0 + 0.5, 0.04, gPixG*4.0);
      m.alb = mix(m.alb, vec3(0.07, 0.075, 0.08), rib); m.metal = mix(m.metal, 0.7, rib);
      if (abs(lp.y - (E.y - 0.18)) < 0.004) m.emit = gColStripe*1.4*pulse;
    }
    else if (mid >= 41 && mid <= 43) { m.alb = vec3(0.0); m.rough = 0.05; }
    else if (mid == 44) {  // bezels and consoles: satin composite with machined edges and fasteners
      vec2 hx = lp.xz*45.0 + vec2(lp.y*30.0);
      tx = triSample(lp, ln, M_PLASTIC, 0.4, nT); m.nrm = nT;
      float tile = aaSquare(hx.x + floor(hx.y)*0.5, gPixG*60.0);
      m.alb = vec3(0.028, 0.03, 0.034)*(0.9 + 0.2*mix(tile, 0.5, smoothstep(0.3, 0.8, gPixG*60.0)))*(0.7 + 0.6*tx.r); m.rough = mix(0.42, tx.a, 0.4); m.metal = 0.35;
      vec3 qd = lp - E.xyz; float rr = length(qd.xz), an = atan(qd.x, -qd.z);
      if (rr < 0.7) m.alb *= 1.0 + 1.2*aaLines(an*9.0, 0.025, gPixG*9.0/max(rr, 0.1));   // panel seams
      float sc = aaDisc(length(vec2(fract(an*18.0) - 0.5, (qd.y - 0.36)*90.0)), 0.12, gPixG*max(18.0/max(rr, 0.1), 90.0), 0.0);   // screws
      m.alb = mix(m.alb, vec3(0.35), sc); m.metal = mix(m.metal, 1.0, sc); m.rough = mix(m.rough, 0.25, sc);
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
    else if (mid == 54) {  // backlit keys, flush in the shelf's top: 24 mm caps with a lit legend, light leaking round them
      vec3 qd = lp - E.xyz; vec3 cq = vec3(abs(qd.x) - 0.52, qd.y + 0.44, qd.z - 0.08) - vec3(0.0, 0.055, 0.12);
      vec2 cell = clamp(floor(cq.xz/0.032 + 0.5), vec2(-3.0, -2.0), vec2(3.0, 3.0)); float hk = hash2i(ivec2(cell) + ivec2(qd.x < 0.0 ? 11 : 37, 5));
      vec2 f = cq.xz - cell*0.032;
      vec3 kc = hk < 0.15 ? vec3(1.0, 0.55, 0.15) : hk < 0.25 ? vec3(0.3, 1.0, 0.5) : gColStripe*0.6;
      float fw = gPixG, big = smoothstep(0.3, 0.8, fw/0.032);   // (a pixel spanning most of a key: its average)
      float dk = length(max(abs(f) - 0.009, 0.0)) - 0.003;
      float key = mix(clamp(0.5 - dk/fw, 0.0, 1.0), 0.56, big);
      float rim = mix(clamp(1.0 - abs(dk + 0.0012)/max(fw, 0.0012), 0.0, 1.0), 0.0, big);   // the cap's bevelled edge catching the light
      float leg = mix(clamp(0.5 - (max(abs(f.x) - 0.006, abs(f.y + 0.003) - 0.0014))/fw, 0.0, 1.0), 0.03, big);
      float on = 0.12 + 0.5*step(0.85, hk)*step(0.5, fract(uTime*0.7 + hk*3.0));
      m.alb = mix(vec3(0.008), vec3(0.045, 0.047, 0.05) + 0.06*rim, key); m.rough = mix(0.7, 0.35, key); m.metal = 0.0;
      m.emit = kc*(on*(0.35*key + 2.2*leg) + 0.05*(1.0 - key));
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
    else if (mid == 78) {   // the compass card behind its window: cream, a tick every 5 degrees (a long one and its numerals
                            // every 30), the red lubber line, lit from inside at night
      float cx = lp.x, cy = lp.y - (E.y - 0.048);
      float u = cx/0.0045, tick = abs(fract(u) - 0.5)*0.0045, longT = abs(fract(u/6.0) - 0.5)*0.027;
      m.alb = vec3(0.78, 0.74, 0.62); m.rough = 0.15;
      if ((tick < 0.0005 && cy > 0.004) || (longT < 0.0007 && cy > -0.002)) m.alb = vec3(0.05);
      if (longT < 0.004 && cy < -0.003 && cy > -0.011 && fract(cx/0.0016) < 0.45) m.alb = vec3(0.06);   // the numerals under them
      if (abs(cx) < 0.0008) m.alb = vec3(0.75, 0.05, 0.03);   // lubber line
      m.emit = m.alb*vec3(1.0, 0.8, 0.55)*(0.05 + 0.3*uNight);   // (a dim lamp: at 0.9 it was the brightest thing in the cabin at night)
    }
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
  if (mid == 6 || mid == 8) m = gearFinish(lp, mid, m, t*2.0*uTanHalf/uRes.y);
  if (gModelId == 8) {   // the Osprey C6's cabin: ivory composite, copper trim, tobacco leather, cocoa textile (its own fittings and the shared ones)
    if (mid >= 120 && mid <= 124) {
      m.alb = ospreyCabinAlbedo(mid); m.metal = mid == 121 ? 0.85 : 0.0; m.rough = mid == 121 ? 0.3 : mid == 122 ? 0.5 : mid == 124 ? 0.2 : 0.75; m.nrm = vec3(0.0, 0.0, 1.0);
      if (mid == 124) m.emit = vec3(1.0, 0.9, 0.7)*0.4; interior = true;
    }
    else if (mid == 11) m.alb = vec3(0.58, 0.54, 0.44); else if (mid == 12) m.alb = vec3(0.30, 0.115, 0.052);
    else if (mid == 63) m.alb = vec3(0.72, 0.66, 0.53); else if (mid == 69) m.alb = vec3(0.062, 0.040, 0.028);
  }
  n = applyTS(n, m.nrm, interior ? 0.35 : 0.12);
  int engM = int(gM[0].z + 0.5);
  podMat = RESEARCH_ON && !trafHit && mid >= 40 && mid < 80 && engM >= 5;   // research jets only: light aircraft use ids 60+ for their cockpits
}
