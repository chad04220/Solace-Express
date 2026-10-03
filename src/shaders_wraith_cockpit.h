// Solace Express - XR-11 Wraith cockpit (GLSL, appended to the ray tracer after kRaytraceWraith)
#pragma once

// A faceted sealed cabin wrapped in see-through angular displays: a three-pane front wrap, tall side displays with aft
// displays behind them (to watch the flanks and the rear quarter), an
// overhead pane, a sloped chin pane and a glass floor in the footwell. Every display re-traces the world along the
// view ray, so looking down through the floor shows the ground, the bombs falling and the blasts below.
// Screen ids: 41 front, 42 left, 43 right (side + aft displays), 61 floor, 62 overhead, 63 chin. Interior materials 64-79.
static const char* kRaytraceWraithCockpit = R"(// ---------------------------------------------------------------- XR-11 cockpit
uniform vec4 uPip;   // bomb impact prediction (world) + valid flag
uniform vec4 uFeed;     // bomb camera look-at point (world) + active flag
uniform vec4 uFeedCam;  // bomb camera position (world) + tan of its half field of view
// a flat display pane: c centre, n facing the pilot, up hint, half size, corner chamfer. x = pane, y = raised bezel
vec3 wrFrame(vec3 q, vec3 c, vec3 n, vec3 up){ vec3 t = normalize(cross(up, n)), b = cross(n, t); vec3 d = q - c; return vec3(dot(d, t), dot(d, b), dot(d, n)); }
float wrShape(vec2 l, vec2 hs, float ch){ vec2 a = abs(l); return max(max(a.x - hs.x, a.y - hs.y), (a.x + a.y - (hs.x + hs.y - ch))*0.70711); }
vec2 wrPane(vec3 q, vec3 c, vec3 n, vec3 up, vec2 hs, float ch){
  vec3 l = wrFrame(q, c, n, up);
  float s = wrShape(l.xy, hs, ch);
  return vec2(max(s, abs(l.z) - 0.004), max(max(s - 0.034, -s), abs(l.z - 0.006) - 0.013));
}
// pane layout (cabin frame: eye at the origin, -z forward)
const vec3 WF_C = vec3(0.0, 0.07, -1.2);    const vec3 WF_N = vec3(0.0, 0.2425, 0.9701);  const vec2 WF_S = vec2(0.4, 0.33);
const vec3 WW_C = vec3(0.6, 0.07, -0.93);   const vec3 WW_N = vec3(-0.7686, 0.1774, 0.6147); const vec2 WW_S = vec2(0.24, 0.33);
const vec3 WS_C = vec3(0.8, -0.075, -0.4);  const vec3 WS_N = vec3(-1.0, 0.0, 0.0);      const vec2 WS_S = vec2(0.34, 0.345);
const vec3 WA_C = vec3(0.8, -0.075, 0.34);  const vec2 WA_S = vec2(0.25, 0.345);   // aft side displays (same facing as WS)
const vec3 WO_C = vec3(0.0, 0.403, -0.55);  const vec3 WO_N = vec3(0.0, -1.0, 0.0);       const vec2 WO_S = vec2(0.42, 0.36);
const vec3 WC_C = vec3(0.0, -0.5, -1.0);    const vec3 WC_N = vec3(0.0, 0.7509, 0.6604);  const vec2 WC_S = vec2(0.38, 0.2);
const vec3 WL_C = vec3(0.0, -0.775, -0.66); const vec3 WL_N = vec3(0.0, 1.0, 0.0);       const vec2 WL_S = vec2(0.4, 0.34);
const vec3 WD_C = vec3(0.0, -0.34, -0.99);  const vec3 WD_N = vec3(0.0, 0.6, 0.8);
const vec3 WB_C = vec3(0.47, -0.705, 0.4);   const vec3 WB_N = vec3(-0.3714, 0.9285, 0.0); const vec2 WB_S = vec2(0.14, 0.2);
// distance (negative inside) to the edge of the display a point lies on, for the HUD frame and vignette
float wrScreenEdge(vec3 q, int id){
  if (id == 41) {
    float c = wrShape(wrFrame(q, WF_C, WF_N, vec3(0,1,0)).xy, WF_S, 0.1);
    vec3 aq = vec3(abs(q.x), q.y, q.z);
    return min(c, wrShape(wrFrame(aq, WW_C, WW_N, vec3(0,1,0)).xy, WW_S, 0.16));
  }
  if (id == 42 || id == 43) { vec3 aq = vec3(abs(q.x), q.y, q.z);
    return min(wrShape(wrFrame(aq, WS_C, WS_N, vec3(0,1,0)).xy, WS_S, 0.13), wrShape(wrFrame(aq, WA_C, WS_N, vec3(0,1,0)).xy, WA_S, 0.11)); }
  if (id == 61) return q.z > 0.1 ? wrShape(wrFrame(vec3(abs(q.x), q.y, q.z), WB_C, WB_N, vec3(0,0,-1)).xy, WB_S, 0.07) : wrShape(wrFrame(q, WL_C, WL_N, vec3(0,0,-1)).xy, WL_S, 0.1);
  if (id == 62) return wrShape(wrFrame(q, WO_C, WO_N, vec3(0,0,-1)).xy, WO_S, 0.12);
  return wrShape(wrFrame(q, WC_C, WC_N, vec3(0,1,0)).xy, WC_S, 0.07);
}
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
  vec2 sF = wrPane(q, WF_C, WF_N, vec3(0,1,0), WF_S, 0.1);
  vec2 sW = wrPane(aq, WW_C, WW_N, vec3(0,1,0), WW_S, 0.16);
  vec2 sS = wrPane(aq, WS_C, WS_N, vec3(0,1,0), WS_S, 0.13);
  vec2 sA = wrPane(aq, WA_C, WS_N, vec3(0,1,0), WA_S, 0.11);
  sS = vec2(min(sS.x, sA.x), min(sS.y, sA.y));
  vec2 sO = wrPane(q, WO_C, WO_N, vec3(0,0,-1), WO_S, 0.12);
  vec2 sC = wrPane(q, WC_C, WC_N, vec3(0,1,0), WC_S, 0.07);
  vec2 sL = wrPane(q, WL_C, WL_N, vec3(0,0,-1), WL_S, 0.1);
  vec2 sB = wrPane(aq, WB_C, WB_N, vec3(0,0,-1), WB_S, 0.07);   // floor panes either side of the seat, behind the consoles
  sL = vec2(min(sL.x, sB.x), min(sL.y, sB.y));
  if (gCkSkip != 1) {
  res = opU(res, vec2(min(sF.x, sW.x), 41.0));
  res = opU(res, vec2(sS.x, sx < 0.0 ? 42.0 : 43.0));
  res = opU(res, vec2(sO.x, 62.0));
  res = opU(res, vec2(sC.x, 63.0));
  res = opU(res, vec2(sL.x, 61.0));
  res = opU(res, vec2(min(min(min(sF.y, sW.y), min(sS.y, sO.y)), min(sC.y, sL.y)), 65.0));
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

// ---------------------------------------------------------------- materials
float wrHex(vec2 p, out vec2 id){   // distance to the edge of a hexagonal cell (0 at the edge)
  vec2 r = vec2(1.0, 1.7320508), h = r*0.5;
  vec2 a = mod(p, r) - h, b = mod(p - h, r) - h;
  vec2 g = dot(a, a) < dot(b, b) ? a : b;
  id = p - g;
  vec2 ag = abs(g);
  return 0.5 - max(dot(ag, normalize(vec2(1.0, 1.7320508))), ag.x);
}
vec3 wrUiPanel(vec2 uv, float seed){   // touch glass: rows of rounded icons, a slider, a status line
  vec3 c = vec3(0.0);
  vec2 g = vec2(uv.x*6.0, uv.y*4.0); vec2 id = floor(g), f = fract(g) - 0.5;
  float h = hash2i(ivec2(id) + ivec2(int(seed*97.0), 7));
  float icon = 1.0 - smoothstep(0.26, 0.3, max(abs(f.x), abs(f.y)) - 0.06*step(0.5, h));
  float ring = 1.0 - smoothstep(0.02, 0.04, abs(length(f) - 0.25));
  vec3 ic = h < 0.2 ? vec3(1.0, 0.45, 0.25) : h < 0.5 ? vec3(0.6, 0.4, 1.0) : vec3(0.3, 0.9, 1.0);
  c += ic*(icon*0.18 + ring*0.6*step(0.7, h))*(0.6 + 0.4*step(0.5, fract(uTime*0.3 + h*5.0)));
  return c;
}
void shadeWraithCockpit(inout Mat m, int mid, vec3 lp, vec3 ln, vec3 E){
  vec3 q = lp - E, nT; vec4 tx;
  float pulse = 0.82 + 0.18*sin(uTime*2.2);
  vec3 cyan = vec3(0.25, 0.85, 1.0), vio = vec3(0.65, 0.35, 1.0);
  m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0.0, 0.0, 1.0);
  if (mid == 64) {   // hex-tiled composite wall panels with machined grooves; a few cells glow
    vec2 uv = abs(ln.x) > 0.6 ? q.zy : (abs(ln.y) > 0.6 ? q.xz : q.xy);
    vec2 id; float e = wrHex(uv*13.0, id);
    tx = triSample(lp, ln, M_PLASTIC, 0.3, nT);
    float hk = hash2i(ivec2(id*3.0) + ivec2(5, 9));
    m.alb = vec3(0.022, 0.024, 0.03)*(0.85 + 0.3*hk)*(0.8 + 0.4*tx.r);
    m.rough = 0.38 + 0.2*hk; m.metal = 0.3; m.nrm = nT*0.6;
    if (e < 0.06) { m.alb *= 0.35; m.rough = 0.7; }
    if (e > 0.06 && hk > 0.965) m.emit = mix(cyan, vio, step(0.985, hk))*0.25*(0.6 + 0.4*sin(uTime*1.3 + hk*20.0));
  } else if (mid == 65 || mid == 72) {   // anodised titanium frames: machined flutes, fasteners, an emitter line
    tx = triSample(lp*2.0, ln, M_METAL, 0.5, nT);
    m.alb = vec3(0.075, 0.078, 0.088)*(0.8 + 0.4*tx.r); m.metal = 0.85; m.rough = 0.3 + 0.15*tx.a; m.nrm = nT*0.5;
    float fl = abs(fract((q.x + q.y*0.7 + q.z*0.4)*60.0) - 0.5);
    if (fl < 0.05) m.alb *= 0.7;
    if (mid == 72) { m.alb *= 0.8; m.emit = vio*0.35*pulse*step(0.0045, abs(fract((q.x + q.z)*25.0) - 0.5) - 0.49); }
  } else if (mid == 66) {   // seat: dark perforated hide in hexagonal quilting, glowing seams
    tx = triSample(lp, ln, M_LEATHER, 0.25, nT);
    vec2 id; float e = wrHex((abs(ln.x) > 0.6 ? q.zy : abs(ln.y) > 0.6 ? q.xz : q.xy)*22.0, id);
    m.alb = vec3(dot(tx.rgb, vec3(0.33)))*vec3(0.17, 0.17, 0.2); m.rough = 0.62; m.nrm = nT;
    if (e < 0.035) { m.alb *= 0.5; if (q.y > -0.55 && q.y < 0.1) m.emit = cyan*0.08*pulse; }
    vec2 pf = (abs(ln.x) > 0.6 ? q.zy : abs(ln.y) > 0.6 ? q.xz : q.xy)*90.0; if (length(fract(pf) - 0.5) < 0.18) m.alb *= 0.45;   // perforations
  } else if (mid == 67) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = mix(cyan, vio, 0.5 + 0.5*sin(q.z*6.0 - uTime*1.5))*1.6*pulse; }
  else if (mid == 68) {   // touch glass on the consoles
    vec3 cq = vec3(abs(q.x) - 0.56, q.y + 0.47, q.z + 0.12);
    vec2 uv = vec2((cq.x - 0.015)/0.1, (cq.z - 0.265)/0.09)*0.5 + 0.5;
    m.alb = vec3(0.01); m.rough = 0.04; m.emit = wrUiPanel(uv, q.x > 0.0 ? 0.3 : 0.7)*1.5; gDispPx = true;
  } else if (mid == 69) {   // multi-function displays (dash and consoles)
    int page; vec2 uv;
    if (q.y > -0.4) {   // dash pair
      vec3 l = wrFrame(q, WD_C, WD_N, vec3(0,1,0));
      page = l.x < 0.0 ? 0 : 2; uv = vec2((abs(l.x) - 0.33)/0.15*sign(l.x), (l.y + 0.005)/0.07);
    } else {
      vec3 cq = vec3(abs(q.x) - 0.56, q.y + 0.47, q.z + 0.12);
      vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = rot2(mq.yz, -0.55);
      page = q.x < 0.0 ? 1 : 3; uv = vec2(mq.x/0.09*sign(q.x), -mq.z/0.055);
    }
    // 4x supersampled over the pixel's footprint on the glass, every element anti-aliased: crisp at any resolution
    float fp = gPixM/(q.y > -0.4 ? 0.07 : 0.055);
    gAA = fp*0.55;
    vec3 sc = pageTex(page, uv, fp)*vec3(0.85, 1.0, 1.15);
    float edge = smoothstep(1.0, 0.93, max(abs(uv.x), abs(uv.y)));
    sc = sc*edge + vec3(0.008, 0.02, 0.035)*edge;
    m.alb = vec3(0.01); m.rough = 0.05; m.emit = sc*1.5; gDispPx = true;
  } else if (mid == 70) {   // grips, knobs and toggles: rubberised with machined caps
    tx = triSample(lp, ln, M_RUBBER, 0.08, nT); m.alb = tx.rgb*0.25; m.rough = tx.a; m.nrm = nT; m.metal = 0.1;
    if (ln.y > 0.7) { m.alb = vec3(0.3); m.metal = 0.9; m.rough = 0.25; }
  } else if (mid == 71) { tx = triSample(lp*3.0, ln, M_METAL, 0.3, nT); m.alb = tx.rgb*0.35; m.metal = 0.9; m.rough = 0.3; m.nrm = nT;
    if (abs(fract(q.y*40.0) - 0.5) < 0.12) m.alb *= 0.5; }
  else if (mid == 73) { m.alb = vec3(0.05); m.emit = vio*2.2*pulse; }
  else if (mid == 74) { m.alb = vec3(0.04); m.metal = 0.9; m.rough = 0.2; }
  else if (mid == 76) {   // annunciators: lit by the craft's state
    vec3 l = wrFrame(q, WD_C, WD_N, vec3(0,1,0));
    int c = int(clamp(floor(l.x/0.07) + 4.0, 0.0, 7.0));
    float on = c == 0 ? uWr[6].z : c == 1 ? step(0.97, uWr[4].z) : c == 2 ? step(0.9, uWr[4].y) : c == 3 ? step(0.99, uWr[6].x) :
               c == 4 ? step(0.01, uWr[4].w) : c == 5 ? step(0.05, max(max(uWr[0].x, uWr[0].y), max(uWr[0].z, uWr[0].w))) :
               c == 6 ? step(7.0, abs(uHud2.x))*step(0.5, fract(uTime*3.0)) : step(uHud.y, 150.0)*step(0.5, fract(uTime*2.0));
    vec3 tc = c <= 3 ? vec3(1.0, 0.3, 0.35) : c == 4 ? vio : c == 5 ? cyan : vec3(1.0, 0.6, 0.15);
    m.alb = vec3(0.015); m.rough = 0.1; m.emit = tc*(0.04 + 2.2*on);
  }
  else { m.alb = vec3(0.02); m.rough = 0.6; m.emit = cyan*0.35*pulse*step(0.5, fract(q.y*14.0 + 0.25)); }   // vents
}
// cabin light: the big displays light it with what they show (sky ahead and above, ground below), plus the emitter
// strips, headrest slits and a soft key from the overhead rail
vec3 wraithPodLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E){
  vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0), upw = uPlaneRot*vec3(0.0, 1.0, 0.0);
  float day = smoothstep(-0.08, 0.15, uSunDir.y);
  vec3 skyF = skyColor(normalize(fw + upw*0.15))*0.9, skyU = skyColor(normalize(upw + fw*0.2))*0.9;
  vec3 ground = vec3(0.16, 0.2, 0.12)*(uSunCol*max(uSunDir.y, 0.0)*1.5 + vec3(0.03))*(0.5 + 0.5*day);
  vec3 L = m.alb*vec3(0.02, 0.025, 0.035);
  L += fixtureLight(p, n, v, m, E + vec3(-0.38, 0.07, -1.18), E + vec3(0.38, 0.07, -1.18), skyF*0.32, 2.2, vec3(0.0, 0.0, 1.0));
  for (int i = -1; i <= 1; i += 2) {
    float s = float(i);
    L += fixtureLight(p, n, v, m, E + vec3(0.79*s, -0.03, -0.7), E + vec3(0.79*s, -0.03, -0.05), mix(skyF, ground, 0.5)*0.18, 2.5, vec3(-s, 0.0, 0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.8*s, 0.315, -1.2), E + vec3(0.8*s, 0.315, 0.7), vec3(0.25, 0.85, 1.0)*0.035, 9.0, vec3(0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.8*s, -0.675, -1.2), E + vec3(0.8*s, -0.675, 0.7), vec3(0.65, 0.35, 1.0)*0.04, 9.0, vec3(0.0));
  }
  L += fixtureLight(p, n, v, m, E + vec3(-0.35, 0.405, -0.55), E + vec3(0.35, 0.405, -0.55), skyU*0.22, 2.5, vec3(0.0, -1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.3, -0.77, -0.66), E + vec3(0.3, -0.77, -0.66), ground*0.6 + vec3(0.65, 0.35, 1.0)*0.015, 3.0, vec3(0.0, 1.0, 0.0));
  L += fixtureLight(p, n, v, m, E + vec3(-0.15, 0.37, 0.15), E + vec3(0.15, 0.37, 0.35), vec3(0.85, 0.9, 1.0)*0.07, 7.0, vec3(0.0, -1.0, 0.0));
  return L;
}

// ---------------------------------------------------------------- hologram: a rotating wireframe globe over the dash
vec3 wrHolo(vec3 ro, vec3 rd, float tmax){
  vec3 c = uPlanePos + uPlaneRot*(gM[22].xyz + vec3(0.0, -0.21, -0.93));
  float R = 0.07;
  vec3 oc = ro - c; float b = dot(oc, rd), h = b*b - dot(oc, oc) + R*R;
  vec3 col = vec3(0.0);
  // projection cone from the emitter
  vec3 e = uPlanePos + uPlaneRot*(gM[22].xyz + vec3(0.0, -0.335, -0.975));
  vec3 ax = normalize(c - e); vec3 w = ro - e; float bb = dot(rd, ax), dd = dot(rd, w), ee = dot(ax, w), den = 1.0 - bb*bb;
  float sA = den > 1e-5 ? (ee - bb*dd)/den : 0.0, sR = max(dot(e + ax*sA - ro, rd), 0.0);
  float L = length(c - e);
  if (sA > 0.0 && sA < L && sR < tmax) { float dist = length(ro + rd*sR - (e + ax*sA)); float rad = 0.008 + R*0.9*sA/L; col += vec3(0.2, 0.7, 1.0)*0.18*smoothstep(rad, 0.0, dist); }
  if (h < 0.0) return col;
  h = sqrt(h);
  mat3 inv = transpose(uPlaneRot);
  for (int k = 0; k < 2; k++) {
    float t = -b + (k == 0 ? -h : h);
    if (t < 0.0 || t > tmax) continue;
    vec3 s = inv*(ro + rd*t - c)/R;
    float a = atan(s.z, s.x) + uTime*0.6, lat = asin(clamp(s.y, -1.0, 1.0));
    float lines = max(smoothstep(0.93, 0.985, abs(fract(a*12.0/6.2832) - 0.5)*2.0), smoothstep(0.93, 0.985, abs(fract(lat*8.0/3.1416) - 0.5)*2.0));
    float land = step(0.62, vnoise(vec2(a*2.5, lat*4.0) + 3.0));
    float coast = land*(1.0 - step(0.66, vnoise(vec2(a*2.5, lat*4.0) + 3.0)));
    col += mix(vec3(0.2, 0.8, 1.0), vec3(0.75, 0.45, 1.0), land)*(0.32*lines + 0.03*land + 0.4*coast)*(k == 0 ? 1.0 : 0.35);
  }
  return col*(0.85 + 0.15*sin(uTime*30.0));
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
  if (tile == 0) { q = wrPanePoint(WF_C, WF_N, vec3(0,1,0), vec3(f*WF_S, 0.0)); inside = wrShape(f*WF_S, WF_S, 0.1); }
  else if (tile <= 2) { q = wrPanePoint(WW_C, WW_N, vec3(0,1,0), vec3(f*WW_S, 0.0)); inside = wrShape(f*WW_S, WW_S, 0.16); }
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
// ---------------------------------------------------------------- belly camera on the front floor pane
// While a bomb falls or goes off, the footwell floor shows the bomb camera (Game::updateBombCam).
// floor pane coordinates [-1, 1] and the pane's aspect (the chin-side footwell pane or a pane beside the seat)
vec3 wrFloorUV(vec3 q){
  if (q.z > 0.1) return vec3(wrFrame(vec3(abs(q.x), q.y, q.z), WB_C, WB_N, vec3(0,0,-1)).xy/WB_S*vec2(sign(q.x), 1.0), WB_S.x/WB_S.y);
  return vec3(wrFrame(q, WL_C, WL_N, vec3(0,0,-1)).xy/WL_S, WL_S.x/WL_S.y);
}
bool wrFeedRay(vec3 sl, inout vec3 ro, inout vec3 rd){
  if (uFeed.w < 0.5) return false;
  vec3 q = sl - gM[22].xyz;
  vec3 fu = wrFloorUV(q);
  vec2 uv = fu.xy*vec2(fu.z, 1.0);
  vec3 cam = uFeedCam.xyz;   // the separate bomb camera, horizon level
  vec3 f = normalize(uFeed.xyz - cam);
  vec3 r = normalize(cross(f, vec3(0.0, 1.0, 0.0)) + vec3(1e-4, 0.0, 0.0));
  vec3 u = cross(r, f);
  ro = cam; rd = normalize(f + (r*uv.x + u*uv.y)*uFeedCam.w);
  return true;
}
vec3 wrFeedOverlay(vec3 col, vec3 sl){
  vec3 q = sl - gM[22].xyz;
  vec2 uv = wrFloorUV(q).xy;
  col = col/(1.0 + dot(col, vec3(0.3, 0.55, 0.15))*0.6);   // the sensor compresses highlights: a blast doesn't white it out
  col = mix(vec3(dot(col, vec3(0.3, 0.55, 0.15))), col, 0.55)*vec3(0.95, 1.05, 1.12);   // sensor look
  float aa = 0.006; gAA = aa;
  vec2 a = abs(uv);
  float br = aLine(max(a.x, a.y) - 0.22, 0.02)*smoothstep(0.13 - aa, 0.13 + aa, min(a.x, a.y));   // tracking brackets
  br = max(br, aLine(a.x, 0.01)*smoothstep(0.06 + aa, 0.06 - aa, a.y)*smoothstep(0.02 - aa, 0.02 + aa, a.y));
  br = max(br, aLine(a.y, 0.01)*smoothstep(0.06 + aa, 0.06 - aa, a.x)*smoothstep(0.02 - aa, 0.02 + aa, a.x));
  float frame = aLine(max(a.x, a.y) - 0.905, 0.012);
  float rec = smoothstep(0.035 + aa, 0.035 - aa, length(uv - vec2(-0.78, 0.78)))*step(0.5, fract(uTime*1.5));
  float lbl = txt(uv - vec2(-0.7, 0.78), 0.06, ivec4(66, 79, 77, 66), ivec4(32, 67, 65, 77), 0);
  vec3 c = mix(col, vec3(1.0, 0.3, 0.4)*2.0, clamp(br + rec, 0.0, 1.0)*0.9);
  c = mix(c, vec3(0.3, 0.95, 1.0)*1.5, lbl);
  return mix(c, vec3(0.3, 0.95, 1.0)*1.5, frame*0.8);
}
// ---------------------------------------------------------------- display look and HUD
vec3 wraithScreen(vec3 col, vec3 rd, int id, vec3 sl){
  vec3 q = sl - gM[22].xyz;
  col = pow(max(col, vec3(0.0)), vec3(1.04))*vec3(0.95, 1.01, 1.07)*1.1;
  float ed = -wrScreenEdge(q, id);
  col *= smoothstep(0.0, 0.04, ed);
  vec3 hc = vec3(0.3, 0.95, 1.0), wc = vec3(1.0, 0.3, 0.4), vc = vec3(0.75, 0.45, 1.0);
  float hud = 0.0, warn = 0.0, viol = 0.0;
  hud = max(hud, (1.0 - smoothstep(0.0012, 0.0028, abs(ed - 0.012)))*0.6);   // inner frame line
  vec3 d = transpose(uPlaneRot)*rd;
  vec2 h = vec2(atan(d.x, -d.z), atan(d.y, -d.z));
  float px = 0.0022;
  if (id == 41) {
    // boresight cross and flight-path marker
    hud = max(hud, hudLine(abs(h.y), px)*step(abs(h.x), 0.03)*step(0.01, abs(h.x)));
    hud = max(hud, hudLine(abs(h.x), px)*step(abs(h.y), 0.018)*step(0.01, abs(h.y)));
    vec3 vb = uHudV;
    if (vb.z < -0.1) {
      vec2 fp = vec2(atan(vb.x, -vb.z), atan(vb.y, -vb.z)) - h;
      float rr = max(abs(fp.x), abs(fp.y)*1.3);   // angular (diamond) marker
      hud = max(hud, hudLine(abs(abs(fp.x) + abs(fp.y) - 0.014), px));
      hud = max(hud, hudLine(abs(fp.y), px)*step(0.014, abs(fp.x))*step(abs(fp.x), 0.04));
    }
    // world-conformal horizon and an angular pitch ladder
    float wel = asin(clamp(rd.y, -1.0, 1.0));
    vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0);
    float az = atan(rd.x, -rd.z) - atan(fw.x, -fw.z); az = mod(az + 3.14159, 6.28318) - 3.14159;
    float k = floor(wel/0.17453 + 0.5), ld = abs(wel - k*0.17453);
    if (k == 0.0) hud = max(hud, hudLine(ld, px*1.4)*step(abs(az), 0.75)*step(0.05, abs(az)));
    else {
      float seg = step(0.07, abs(az))*step(abs(az), 0.21)*(k < 0.0 ? step(0.5, fract(az*45.0)) : 1.0);
      hud = max(hud, hudLine(ld, px)*seg);
      hud = max(hud, hudLine(abs(abs(az) - 0.21 - (wel - k*0.17453)*sign(k)), px)*step(abs(wel - k*0.17453 + sign(k)*0.013), 0.013));   // slanted tips
    }
    // heading, speed (kt) and altitude (ft) in chamfered boxes; Mach and G under the speed box
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    if (h.y > 0.325 && h.y < 0.355 && abs(h.x) < 0.4) {
      float f10 = abs(fract(hd/10.0 + 0.5) - 0.5)*10.0;
      float tall = abs(fract(hd/30.0 + 0.5) - 0.5)*30.0 < 0.5 ? 1.0 : 0.0;
      hud = max(hud, step(f10*0.01745, px*0.8)*step(h.y, 0.34 + 0.015*tall));
    }
    hud = max(hud, hudNum(h - vec2(-0.044, 0.362), uHud.z, 3, vec2(0.022, 0.04)));
    hud = max(hud, hudLine(abs(wrShape(h - vec2(0.0, 0.382), vec2(0.058, 0.03), 0.015)), px));
    hud = max(hud, hudLine(abs(wrShape(h - vec2(-0.37, 0.0), vec2(0.078, 0.032), 0.016)), px));
    hud = max(hud, hudNum(h - vec2(-0.43, -0.02), uHud.x*1.94384, 4, vec2(0.022, 0.04)));
    hud = max(hud, hudLine(abs(wrShape(h - vec2(0.39, 0.0), vec2(0.094, 0.032), 0.016)), px));
    hud = max(hud, hudNum(h - vec2(0.315, -0.02), uHud.y*3.28084, 5, vec2(0.022, 0.04)));
    hud = max(hud, hudNum(h - vec2(-0.42, -0.085), uHud.w*100.0, 3, vec2(0.014, 0.025)));
    hud = max(hud, step(length(h - vec2(-0.3995, -0.084)), 0.0025));
    hud = max(hud, hudNum(h - vec2(-0.42, -0.13), abs(uHud2.x)*10.0, 3, vec2(0.014, 0.025)));
    hud = max(hud, step(length(h - vec2(-0.3805, -0.129)), 0.0025));
    // weapons: laser pipper at the 650 m convergence point (red when the turrets are out), cloak arc (violet)
    float armed = uWr[6].z, las = uWr[4].z;
    if (armed > 0.5) {
      float pr = length(h - vec2(0.0, -0.002));
      float tick = step(0.85, abs(cos(atan(h.y, h.x)*4.0)));
      warn = max(warn, hudLine(abs(pr - 0.03), px*1.2)*(las > 0.97 ? 1.0 : step(0.5, fract(uTime*4.0))));
      warn = max(warn, hudLine(abs(pr - 0.042), px)*tick);
      warn = max(warn, step(pr, 0.0025));
    }
    float st = uWr[4].w;
    if (st > 0.01) { float ca = atan(h.y + 0.3, h.x); float cr = length(h + vec2(0.0, 0.3));
      viol = max(viol, hudLine(abs(cr - 0.05), px*1.3)*step(-1.5708 - st*3.1416, ca - 1.5708)*step(ca - 1.5708, -1.5708 + st*3.1416) ); }
  } else if (id == 61 || id == 63) {
    // bomb impact prediction: a diamond on the ground with the blast ring, and a fall line from the bay
    if (uPip.w > 0.5) {
      vec3 dd = normalize(uPip.xyz - uCamPos);
      vec3 u = normalize(cross(dd, vec3(0.0, 1.0, 0.0) + vec3(1e-4, 0.0, 0.0))), v = cross(u, dd);
      float dz = dot(rd, dd);
      if (dz > 0.5) {
        vec2 s = vec2(dot(rd, u), dot(rd, v))/dz;
        float rng = length(uPip.xyz - uCamPos), ring = 100.0/rng;
        float dia = abs(abs(s.x) + abs(s.y) - 0.022);
        float on = uWr[4].y > 0.9 ? 1.0 : 0.55;
        warn = max(warn, hudLine(dia, px*1.3)*on);
        warn = max(warn, hudLine(abs(length(s) - ring), px)*step(0.5, fract(atan(s.y, s.x)*6.0/3.1416))*on);
        warn = max(warn, step(length(s), 0.003)*on);
        hud = max(hud, hudNum(s - vec2(0.03, -0.012), rng, 4, vec2(0.012, 0.02)));
      }
    }
    // ground-stabilised range rings under the craft
    if (id == 61) {
      float tdown = (uCamPos.y - max(uPip.y, 0.0))/max(-rd.y, 0.05);
      vec3 g = uCamPos + rd*tdown;
      float rr = length(g.xz - uCamPos.xz);
      hud = max(hud, hudLine(abs(fract(rr/100.0 + 0.5) - 0.5)*100.0, 0.06*tdown*px*20.0)*0.35*step(rd.y, -0.1));
    }
  } else if (id == 62) {
    // overhead: a heading ring
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    hud = max(hud, hudLine(abs(fract(hd/15.0 + 0.5) - 0.5)*15.0, 0.25)*step(abs(rd.y - 0.85), 0.01)*0.6);
  } else {
    // side displays: heading readout and edge ticks
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    bool aft = q.z > 0.05;
    vec2 u = vec2(q.z - (aft ? 0.34 : -0.38), q.y + 0.03);
    hud = max(hud, hudNum(u - vec2(-0.03, 0.33), hd, 3, vec2(0.014, 0.024)));
    hud = max(hud, hudLine(abs(u.y), 0.0012)*step(aft ? 0.21 : 0.3, abs(u.x)));
  }
  // alien tracker on every display: an angular violet bracket around the UFO with its range
  if (uUfoOn == 1) {
    vec3 dd = uUfoPos - uCamPos; float rng = length(dd); dd /= rng;
    vec3 u = normalize(cross(dd, vec3(0.0, 1.0, 0.0) + vec3(1e-4, 0.0, 0.0))), v = cross(u, dd);
    float dz = dot(rd, dd);
    if (dz > 0.6) {
      vec2 s = vec2(dot(rd, u), dot(rd, v))/dz;
      float b = max(14.0/rng, 0.03);
      vec2 a = abs(s);
      viol = max(viol, hudLine(abs(max(a.x, a.y) - b), px*1.3)*step(b*0.55, min(a.x, a.y)));   // corner brackets only
      viol = max(viol, hudLine(abs(a.x + a.y - b*0.35), px)*step(max(a.x, a.y), b*0.35));
      viol = max(viol, hudNum(s - vec2(-0.02, -b - 0.03), rng, 4, vec2(0.012, 0.02)));
    }
  }
  col = mix(col, hc*2.4, clamp(hud, 0.0, 1.0)*0.9);
  col = mix(col, wc*2.4, clamp(warn, 0.0, 1.0)*0.9);
  col = mix(col, vc*2.0, clamp(viol, 0.0, 1.0)*0.85);
  return col;
}
)";
