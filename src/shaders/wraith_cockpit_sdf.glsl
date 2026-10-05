//! kWraithCockpitSDF
//! The XR-11 cockpit distance field and its clipping checks.
int gCkSkip = 0;   // clipping checks: 1 = without the displays, 2 = without the gauges
vec2 mapWraithCockpit(vec3 p){
  vec4 E4 = gM[22]; vec3 q = p - E4.xyz;
  float cPitch = gCtl.x, cRoll = gCtl.y, cThr = gCtl.w;
  vec3 aq = vec3(abs(q.x), q.y, q.z);
  float sx = q.x < 0.0 ? -1.0 : 1.0;
  // faceted cabin shell: an octagonal tunnel, the pilot inside it
  float yy = q.y + 0.18;
  float sec = max(max(aq.x - 0.82, abs(yy) - 0.6), (aq.x + abs(yy))*0.70711 - 0.93);
  float shell = max(sec, max(-1.34 - q.z, q.z - 0.74));
  vec2 res = vec2(-shell, 64.0);
  // structural ribs hugging the shell, framing the displays
  float ribs = max(abs(q.z + 0.8) - 0.018, -shell - 0.042);
  ribs = min(ribs, max(abs(q.z - 0.02) - 0.03, -shell - 0.05));
  ribs = min(ribs, max(abs(q.z - 0.66) - 0.02, -shell - 0.04));
  ribs = max(ribs, 0.56 - aq.x);   // side walls only: the floor glass and the overhead pane stay clear
  res = opU(res, vec2(ribs, 65.0));
  // displays and their raised chamfered frames
  vec2 sF; { vec3 l = wrFront(q); float sh = wrFrontShape(q, l.xy);
    sF = vec2(max(sh, abs(l.z) - 0.004), max(max(sh - 0.034, -sh), abs(l.z - 0.006) - 0.013)); }
  vec2 sS = wrPane(aq, WS_C, WS_N, vec3(0,1,0), WS_S, 0.13);
  vec2 sA = wrPane(aq, WA_C, WS_N, vec3(0,1,0), WA_S, 0.11);
  sS = vec2(min(sS.x, sA.x), min(sS.y, sA.y));
  vec2 sO = wrPane(q, WO_C, WO_N, vec3(0,0,-1), WO_S, 0.12);
  vec2 sC = wrPane(q, WC_C, WC_N, vec3(0,1,0), WC_S, 0.07);
  vec2 sL = wrPane(q, WL_C, WL_N, vec3(0,0,-1), WL_S, 0.1);
  vec2 sB = wrPane(aq, WB_C, WB_N, vec3(0,0,-1), WB_S, 0.07);   // floor panes either side of the seat, behind the consoles
  sL = vec2(min(sL.x, sB.x), min(sL.y, sB.y));
  if (gCkSkip != 1) {
  res = opU(res, vec2(sF.x, 41.0));
  res = opU(res, vec2(sS.x, sx < 0.0 ? 42.0 : 43.0));
  res = opU(res, vec2(sO.x, 62.0));
  res = opU(res, vec2(sC.x, 63.0));
  res = opU(res, vec2(sL.x, 61.0));
  res = opU(res, vec2(min(min(min(sF.y, sS.y), sO.y), min(sC.y, sL.y)), 65.0));
  }
  // glass floor: a grid of thin titanium ribs over the pane
  {
    vec3 l = wrFrame(q, WL_C, WL_N, vec3(0,0,-1));
    vec2 g = abs(fract(l.xy/vec2(0.2, 0.17) + 0.5) - 0.5)*vec2(0.2, 0.17);
    float grid = max(max(min(g.x, g.y) - 0.006, l.z - 0.012), max(wrShape(l.xy, WL_S, 0.1), -l.z));
    if (gCkSkip != 1) res = opU(res, vec2(grid, 72.0));
  }
  // dash: an angular carbon blade under the front wrap with two displays and the hologram emitter between them
  {
    vec3 l = wrFrame(q, WD_C, WD_N, vec3(0,1,0));
    float blade = max(max(abs(l.x) - 0.66, abs(l.y) - 0.1), abs(l.z + 0.02) - 0.022);
    blade = max(blade, (abs(l.x)*0.6 + abs(l.y) - 0.48));                                // swept ends
    res = opU(res, vec2(blade, 65.0));
    vec3 m = vec3(abs(l.x) - 0.33, l.y + 0.005, l.z - 0.003);
    if (gCkSkip != 2) res = opU(res, vec2(max(wrShape(m.xy, vec2(0.15, 0.07), 0.035), abs(m.z) - 0.0015), 69.0));
    vec3 h = vec3(l.x, l.y + 0.01, l.z);
    float pod = max(length(h.xy) - 0.055, abs(h.z - 0.012) - 0.012);
    res = opU(res, vec2(pod, 74.0));
    res = opU(res, vec2(max(abs(length(h.xy) - 0.035) - 0.004, abs(h.z - 0.026) - 0.003), 67.0));
    // annunciator tiles along the blade's upper edge: ARM, LASER, BAY, BOMB, CLOAK, PODS, G, ALT
    vec3 a = vec3(l.x, l.y - 0.083, l.z - 0.001);
    float cell = clamp(floor(a.x/0.07 + 0.5), -3.5, 3.5);
    a.x -= (floor(a.x/0.07) + 0.5)*0.07;
    if (gCkSkip != 2) res = opU(res, vec2(max(max(abs(a.x) - 0.028, abs(a.y) - 0.009), max(abs(l.x) - 0.28, abs(a.z) - 0.0015)), 76.0));
  }
  // side consoles: angular slabs below the side displays with touch glass, a display and the controls
  {
    vec3 cq = vec3(aq.x - 0.56, q.y + 0.47, q.z + 0.12);
    float slab = max(max(abs(cq.x) - 0.13, abs(cq.y) - 0.04), abs(cq.z) - 0.36);
    slab = max(slab, (-cq.x*0.7 + cq.y) - 0.035);                      // chamfered inner edge
    slab = max(slab, (abs(cq.z) + abs(cq.x)*0.5) - 0.4);
    res = opU(res, vec2(slab, 65.0));
    // Aft touch area leaves the stick base and the throttle's full travel on solid console material.
    if (gCkSkip != 2) res = opU(res, vec2(max(max(abs(cq.x - 0.015) - 0.1, abs(cq.y - 0.041) - 0.0015), abs(cq.z - 0.265) - 0.09), 68.0));
    vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = rot2(mq.yz, -0.55);
    if (gCkSkip != 2) res = opU(res, vec2(max(wrShape(mq.xz, vec2(0.09, 0.055), 0.02), abs(mq.y) - 0.0015), 69.0));
    res = opU(res, vec2(sdBox(mq + vec3(0.0, 0.012, 0.0), vec3(0.105, 0.012, 0.07)), 65.0));
  }
  // side stick (right) follows pitch and roll; throttle (left) slides with the throttle
  {
    vec3 sb = q - vec3(0.5, -0.41, -0.06);
    float st0 = sdRoundBox(sb, vec3(0.05, 0.02, 0.08), 0.004);
    vec3 st = sb; st.yz = rot2(st.yz, -cPitch*0.25); st.xy = rot2(st.xy, cRoll*0.25);
    float stick = min(st0, sdCapsule(st, vec3(0.0), vec3(0.0, 0.09, -0.01), 0.014));
    vec3 gq = st - vec3(0.0, 0.14, -0.015);
    float grip = max(sdBox(gq, vec3(0.022, 0.05, 0.03)), (abs(gq.x) + abs(gq.z))*0.70711 - 0.03);   // faceted grip
    grip = min(grip, sdBox(gq - vec3(0.0, 0.055, -0.01), vec3(0.018, 0.012, 0.022)));
    res = opU(res, vec2(min(stick, grip), 70.0));
    res = opU(res, vec2(sdCapsule(gq - vec3(0.0, 0.066, -0.016), vec3(-0.008, 0.0, 0.0), vec3(0.008, 0.0, 0.0), 0.006), 67.0));   // trigger lights
    vec3 tq = q - vec3(-0.5, -0.38, -0.08 + 0.13*(0.5 - cThr));
    float thr = max(sdBox(tq, vec3(0.032, 0.045, 0.06)), (abs(tq.y) + abs(tq.z))*0.70711 - 0.07);
    res = opU(res, vec2(thr, 70.0));
    res = opU(res, vec2(sdBox(tq - vec3(0.0, 0.046, -0.02), vec3(0.02, 0.002, 0.03)), 67.0));
    res = opU(res, vec2(sdBox(q - vec3(-0.5, -0.425, -0.08), vec3(0.008, 0.004, 0.11)), 75.0));   // throttle slot
  }
  // fighter seat: angular shell, bolsters, a headrest with glowing slits; titanium rails down to the floor
  {
    vec3 bq = q - vec3(0.0, -0.6, 0.13);
    float seat = max(sdBox(bq, vec3(0.25, 0.07, 0.27)), (abs(bq.x) + abs(bq.y))*0.70711 - 0.25);
    vec3 kq = q - vec3(0.0, -0.17, 0.43); kq.yz = rot2(kq.yz, 0.22);
    seat = min(seat, max(sdBox(kq, vec3(0.24, 0.42, 0.06)), (abs(kq.x)*0.8 + abs(kq.y)*0.4) - 0.34));
    seat = min(seat, max(sdBox(vec3(aq.x - 0.27, q.y + 0.32, q.z - 0.3), vec3(0.045, 0.24, 0.13)), (abs(q.y + 0.32) + abs(q.z - 0.3))*0.70711 - 0.25));
    seat = min(seat, max(sdBox(vec3(aq.x - 0.23, q.y + 0.52, q.z - 0.1), vec3(0.04, 0.06, 0.22)), (q.y + 0.52) - (q.z - 0.1)*0.2 - 0.03));
    vec3 hq = q - vec3(0.0, 0.2, 0.47); hq.yz = rot2(hq.yz, 0.22);
    seat = min(seat, max(sdBox(hq, vec3(0.15, 0.11, 0.05)), (abs(hq.x) + abs(hq.y))*0.70711 - 0.15));
    res = opU(res, vec2(seat, 66.0));
    res = opU(res, vec2(sdBox(vec3(abs(hq.x) - 0.06, hq.y, hq.z + 0.05), vec3(0.008, 0.07, 0.004)), 73.0));
    res = opU(res, vec2(sdBox(vec3(aq.x - 0.2, q.y + 0.71, q.z - 0.12), vec3(0.018, 0.07, 0.3)), 65.0));
    vec3 xq = vec3(abs(q.x) - 0.1, q.y + 0.05, q.z - 0.36); xq.yz = rot2(xq.yz, 0.22);
    res = opU(res, vec2(sdBox(xq, vec3(0.022, 0.32, 0.006)), 66.0));   // harness straps
  }
  // rudder pedals over the glass floor
  {
    vec3 pq = vec3(aq.x - 0.16, q.y + 0.63, q.z + 0.78 - sx*gCtl.z*0.04);
    float ped = max(sdBox(pq, vec3(0.05, 0.075, 0.012)), (abs(pq.x) + abs(pq.y))*0.70711 - 0.08);
    ped = min(ped, sdCapsule(pq, vec3(0.0, -0.07, 0.02), vec3(0.0, -0.12, 0.1), 0.012));
    res = opU(res, vec2(ped, 71.0));
  }
  // overhead switch rail behind the overhead pane: faceted toggles and status lights
  {
    vec3 oq = q - vec3(0.0, 0.39, 0.25);
    res = opU(res, vec2(max(sdBox(oq, vec3(0.2, 0.025, 0.2)), (abs(oq.x) + abs(oq.z))*0.70711 - 0.26), 65.0));
    vec3 tq = oq + vec3(0.0, 0.03, 0.0);
    vec2 c2 = clamp(floor(tq.xz/vec2(0.05, 0.07) + 0.5), vec2(-3.0, -2.0), vec2(3.0, 2.0));
    tq.xz -= c2*vec2(0.05, 0.07);
    res = opU(res, vec2(sdCapsule(tq, vec3(0.0), vec3(0.0, -0.022, 0.008), 0.0045), 70.0));
    res = opU(res, vec2(sdBox(tq - vec3(0.0, 0.003, 0.025), vec3(0.008, 0.002, 0.004)), 67.0));
  }
  // emitter strips along the shell's chamfer edges and the rear bulkhead vents
  float led = 1e5;
  for (int i = 0; i < 2; i++) {
    float ey = i == 0 ? 0.315 : -0.675;
    led = min(led, length(vec2(aq.x - 0.808, q.y - ey)) - 0.006 + max(abs(q.z + 0.3) - 1.0, 0.0));
  }
  res = opU(res, vec2(led, 67.0));
  {
    vec3 vq = vec3(aq.x - 0.3, q.y + 0.1, q.z - 0.735);
    vq.y = mod(vq.y + 0.035, 0.07) - 0.035;
    res = opU(res, vec2(max(sdBox(vq, vec3(0.12, 0.012, 0.012)), abs(q.y + 0.1) - 0.24), 75.0));
  }
  return res;
}

// clipping check (debug builds): is this display / gauge surface point buried inside other cockpit geometry?
float wrClip(vec3 lp, int mid){
  int keep = gCkSkip;
  gCkSkip = (mid >= 41 && mid <= 43) || (mid >= 61 && mid <= 63) ? 1 : 2;
  float d = mapWraithCockpit(lp).x;
  gCkSkip = keep;
  return d;
}
// Clipping atlas (debug builds): the screen is tiled with every display and gauge surface, unrolled flat. Each pixel
// is a point on one of them: red if it lies inside other cockpit geometry, green if clear, black off the surface.
vec3 wrPanePoint(vec3 c, vec3 n, vec3 up, vec3 l){ vec3 t = normalize(cross(up, n)), b = cross(n, t); return c + t*l.x + b*l.y + n*l.z; }
vec3 wrClipAtlas(vec2 uv){
  vec2 g = uv*vec2(6.0, 5.0); int tile = int(floor(g.y))*6 + int(floor(g.x)); vec2 f = fract(g)*2.2 - 1.1;   // f in [-1.1, 1.1]; the top row stays clear for the game HUD
  vec3 q; float inside; int skip = 1;
  float sgn = (tile == 1 || tile == 3 || tile == 8 || tile == 10 || tile == 12 || tile == 14 || tile == 17) ? -1.0 : 1.0;
  if (tile == 0) { vec2 l = f*WF_S; float a = l.x/WF_R; q = vec3(WF_C.x + sin(a)*WF_R, WF_C.y + l.y, WF_C.z - cos(a)*WF_R); inside = wrFrontShape(q, l); }
  else if (tile <= 2) return vec3(0.02);
  else if (tile <= 4) { q = wrPanePoint(WS_C, WS_N, vec3(0,1,0), vec3(f*WS_S, 0.0)); inside = wrShape(f*WS_S, WS_S, 0.13); }
  else if (tile == 5) { q = wrPanePoint(WO_C, WO_N, vec3(0,0,-1), vec3(f*WO_S, 0.0)); inside = wrShape(f*WO_S, WO_S, 0.12); }
  else if (tile == 6) { q = wrPanePoint(WC_C, WC_N, vec3(0,1,0), vec3(f*WC_S, 0.0)); inside = wrShape(f*WC_S, WC_S, 0.07); }
  else if (tile == 7) { q = wrPanePoint(WL_C, WL_N, vec3(0,0,-1), vec3(f*WL_S, 0.0)); inside = wrShape(f*WL_S, WL_S, 0.1); }
  else if (tile <= 9) { q = wrPanePoint(WB_C, WB_N, vec3(0,0,-1), vec3(f*WB_S, 0.0)); inside = wrShape(f*WB_S, WB_S, 0.07); }
  else if (tile <= 11) { skip = 2; vec2 m = f*vec2(0.15, 0.07); q = wrPanePoint(WD_C, WD_N, vec3(0,1,0), vec3(0.33 + m.x, m.y - 0.005, 0.003)); inside = wrShape(m, vec2(0.15, 0.07), 0.035); }
  else if (tile <= 13) { skip = 2; vec2 m = f*vec2(0.1, 0.09); q = vec3(0.56 + 0.015 + m.x, 0.041 - 0.47, 0.265 + m.y - 0.12); inside = max(abs(m.x) - 0.1, abs(m.y) - 0.09); }
  else if (tile <= 15) { skip = 2; vec2 m = f*vec2(0.09, 0.055); vec3 mq = vec3(m.x, 0.0, m.y); mq.yz = rot2(mq.yz, 0.55);
    vec3 cq = mq + vec3(0.015, 0.075, -0.24); q = vec3(0.56 + cq.x, cq.y - 0.47, cq.z - 0.12); inside = wrShape(m, vec2(0.09, 0.055), 0.02); }
  else if (tile == 16) { skip = 2; vec2 m = f*vec2(0.28, 0.009); q = wrPanePoint(WD_C, WD_N, vec3(0,1,0), vec3(m.x, 0.083 + m.y, 0.001)); inside = max(abs(m.x) - 0.28, abs(m.y) - 0.009); }
  else if (tile <= 18) { q = wrPanePoint(WA_C, WS_N, vec3(0,1,0), vec3(f*WA_S, 0.0)); inside = wrShape(f*WA_S, WA_S, 0.11); }
  else return vec3(0.02);
  q.x *= sgn;
  if (inside > 0.0) return vec3(0.0);
  int keep = gCkSkip; gCkSkip = skip;
  vec2 dm = mapWraithCockpit(q + gM[22].xyz); float d = dm.x;
  gCkSkip = keep;
  int im = int(dm.y + 0.5);
  vec3 bad = im == 64 ? vec3(0.6, 0.0, 0.0) : im == 65 ? vec3(0.6, 0.25, 0.0) : im == 67 ? vec3(0.6, 0.0, 0.6) : im == 66 ? vec3(0.6, 0.6, 0.0)
           : (im == 70 || im == 71) ? vec3(0.0, 0.0, 0.7) : (im >= 41 && im <= 63) ? vec3(0.7) : vec3(0.0, 0.6, 0.6);
  return d < -0.0015 ? bad : vec3(0.0, 0.12 + 0.2*clamp(d*10.0, 0.0, 1.0), 0.0);
}
