//! kWraithCockpitSDF
//! The XR-40 cockpit distance field and its clipping checks.
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
  // glass floor: a grid of titanium ribs over the pane (16 mm wide, standing 18 mm proud of the glass: at 12 by 8 mm
  // they slipped between the bake's lattice points in places, and the grid came out with sections missing)
  {
    vec3 l = wrFrame(q, WL_C, WL_N, vec3(0,0,-1));
    vec2 g = abs(fract(l.xy/vec2(0.2, 0.17) + 0.5) - 0.5)*vec2(0.2, 0.17);
    float grid = max(max(min(g.x, g.y) - 0.008, l.z - 0.022), max(wrShape(l.xy, WL_S, 0.1), -l.z));
    if (gCkSkip != 1) res = opU(res, vec2(grid, 72.0));
  }
  // A shallow hex-framed bridge: large centre ADI, engine left and map right. The modules are thick solids,
  // with glass assigned flush to the front face, so extraction cannot create sub-lattice floating screen edges.
  {
    vec3 l = wrFrame(q, WD_C, WD_N, vec3(0,1,0));
    float bridge = sdRoundBox(l - WR_BRIDGE_C, WR_BRIDGE_HALF, WR_BRIDGE_ROUND);
    res = opU(res, vec2(bridge, 65.0));
    for (int i = 0; i < 3; i++) {
      ResearchPanel panel = wraithPanel(i);
      vec3 m = researchPanelFrame(q, panel);
      float body = sdRoundBox(m + vec3(0.0, 0.0, 0.028), vec3(panel.h + vec2(0.020), 0.028), 0.012);
      body = max(body, (abs(m.x) + abs(m.y) - panel.h.x - panel.h.y - 0.040 + panel.corner)*0.70710678);
      bool glass = gCkSkip != 2 && m.z > -0.010 && researchPanelShape(m.xy, panel) < 0.0;
      res = opU(res, vec2(body, glass ? 69.0 : 65.0));
    }
    // State-driven ARM/LASER/BAY/BOMB/CLOAK/PODS/G/ALT tiles sit on a quiet strip below the primary display.
    vec3 a = l - vec3(0.0, -0.155, 0.008);
    float rail = sdRoundBox(a + vec3(0.0, 0.0, 0.027), vec3(0.29, 0.017, 0.027), 0.008);
    vec3 cell = a; cell.x -= (floor(cell.x/0.07) + 0.5)*0.07;
    bool status = gCkSkip != 2 && a.z > -0.006 && abs(cell.x) < 0.028 && abs(a.y) < 0.009 && abs(l.x) < 0.28;
    res = opU(res, vec2(rail, status ? 76.0 : 65.0));
    // The retained hologram is a small left-edge aid, clear of both the flight page and its flight scan.
    vec3 h = wrFrame(q, WR_HOLO_EMITTER, WD_N, vec3(0,1,0));
    vec2 pd = vec2(length(h.xy) - 0.026, abs(h.z + 0.008) - 0.012);
    float pod = min(max(pd.x, pd.y), 0.0) + length(max(pd, 0.0)) - 0.006;
    float ring = length(vec2(length(h.xy) - 0.023, h.z - 0.009)) - 0.004;
    res = opU(res, vec2(smin(pod, ring, 0.006), ring < pod ? 67.0 : 74.0));
    res = opU(res, vec2(sdCapsule(q, vec3(-0.628, -0.427, -0.954), WR_HOLO_EMITTER, 0.020), 65.0));
  }
  // side consoles: angular slabs below the side displays with touch glass, a display and the controls
  {
    vec3 cq = vec3(aq.x - 0.56, q.y + 0.47, q.z + 0.12);
    float slab = max(max(abs(cq.x) - 0.13, abs(cq.y) - 0.04), abs(cq.z) - 0.36);
    slab = max(slab, (-cq.x*0.7 + cq.y) - 0.035);                      // chamfered inner edge
    slab = max(slab, (abs(cq.z) + abs(cq.x)*0.5) - 0.4);
    // (its touch glass and the display on its tilted mount flush in their tops, as the dash's are. The aft touch area
    // leaves the stick base and the throttle's full travel on solid console material)
    bool touch = gCkSkip != 2 && cq.y > 0.03 && abs(cq.x - 0.015) < 0.1 && abs(cq.z - 0.265) < 0.09;
    res = opU(res, vec2(slab, touch ? 68.0 : 65.0));
    vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = rot2(mq.yz, -0.55);
    bool mfd = gCkSkip != 2 && mq.y > -0.006 && wrShape(mq.xz, vec2(0.09, 0.055), 0.02) < 0.0;
    // (a rounded block down into the console: a thin sharp slab meeting it at a slant zigzagged. Its edges round at
    // 14 mm, the display just inside the flat of its top: at 8 mm the bend was finer than the cabin's fine lattice,
    // and its titanium's highlights ran along it as a sawtooth - the owner's report)
    res = opU(res, vec2(sdRoundBox(mq + vec3(0.0, 0.03, 0.0), vec3(0.105, 0.03, 0.07), 0.014), mfd ? 69.0 : 65.0));
  }
  // side stick (right) follows pitch and roll; throttle (left) slides with the throttle
  {
    vec3 sb = q - vec3(0.5, -0.41, -0.06);
    res = opU(res, vec2(sdRoundBox(sb, vec3(0.05, 0.02, 0.08), 0.004), 70.0));   // the stick's base
    res = partAt(res, PT_WR_STICK, vec2(0.0), p);   // the stick and the throttle: rigid parts (plane_parts.glsl)
    res = partAt(res, PT_WR_THR, vec2(0.0), p);
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
    res = partAt(res, PT_WR_PEDAL, vec2(sx, 0.0), p);   // (a rigid part: plane_parts.glsl)
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
  else if (tile <= 11) { skip = 2; ResearchPanel panel = wraithPanel(tile == 10 ? 0 : 2);
    q = wrPanePoint(panel.c, panel.n, vec3(0,1,0), vec3(f*panel.h, 0.0)); inside = researchPanelShape(f*panel.h, panel); }
  else if (tile <= 13) { skip = 2; vec2 m = f*vec2(0.1, 0.09); q = vec3(0.56 + 0.015 + m.x, 0.041 - 0.47, 0.265 + m.y - 0.12); inside = max(abs(m.x) - 0.1, abs(m.y) - 0.09); }
  else if (tile <= 15) { skip = 2; vec2 m = f*vec2(0.09, 0.055); vec3 mq = vec3(m.x, 0.0, m.y); mq.yz = rot2(mq.yz, 0.55);
    vec3 cq = mq + vec3(0.015, 0.075, -0.24); q = vec3(0.56 + cq.x, cq.y - 0.47, cq.z - 0.12); inside = wrShape(m, vec2(0.09, 0.055), 0.02); }
  else if (tile == 16) { skip = 2; vec2 m = f*vec2(0.28, 0.009); q = wrPanePoint(WD_C, WD_N, vec3(0,1,0), vec3(m.x, -0.155 + m.y, 0.008)); inside = max(abs(m.x) - 0.28, abs(m.y) - 0.009); }
  else if (tile <= 18) { q = wrPanePoint(WA_C, WS_N, vec3(0,1,0), vec3(f*WA_S, 0.0)); inside = wrShape(f*WA_S, WA_S, 0.11); }
  else if (tile == 19) { skip = 2; ResearchPanel panel = wraithPanel(1);
    q = wrPanePoint(panel.c, panel.n, vec3(0,1,0), vec3(f*panel.h, 0.0)); inside = researchPanelShape(f*panel.h, panel); }
  else return vec3(0.02);
  if (tile != 10 && tile != 11) q.x *= sgn;
  if (inside > 0.0) return vec3(0.0);
  int keep = gCkSkip; gCkSkip = skip;
  vec2 dm = mapWraithCockpit(q + gM[22].xyz); float d = dm.x;
  gCkSkip = keep;
  int im = int(dm.y + 0.5);
  vec3 bad = im == 64 ? vec3(0.6, 0.0, 0.0) : im == 65 ? vec3(0.6, 0.25, 0.0) : im == 67 ? vec3(0.6, 0.0, 0.6) : im == 66 ? vec3(0.6, 0.6, 0.0)
           : (im == 70 || im == 71) ? vec3(0.0, 0.0, 0.7) : (im >= 41 && im <= 63) ? vec3(0.7) : vec3(0.0, 0.6, 0.6);
  return d < -0.0015 ? bad : vec3(0.0, 0.12 + 0.2*clamp(d*10.0, 0.0, 1.0), 0.0);
}
