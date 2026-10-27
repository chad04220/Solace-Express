//! kPlaneScreens
//! The XR-30's display look and head-up symbology.
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
#if HAS_MORPHEUS
// The XR-50's ground station: the symbology over its camera monitors (research_cockpit_layout.glsl morpheusPane 0-3:
// main, left, right, chin), each a window to the world its camera sees with the remote pilot's overlay on the glass.
// q: the point (eye at the origin), rd: the world ray through it. rgb its colour, a its coverage
// a monitor's glass, inside its outline: which (-1: none). (The mesh's fragment of the glass is the one kept where the
// symbology is lit: what lies behind the window stays cut)
int morpheusGlassPane(vec3 q){
  for (int i = 0; i < 4; i++) {
    ResearchPanel P = morpheusPane(i);
    vec3 l = researchPanelFrame(q, P);
    if (abs(l.z - 0.004) < 0.006 && researchPanelShape(l.xy, P) < 0.0) return i;
  }
  return -1;
}
vec4 morpheusHud(int pane, vec3 q, vec3 rd){
  ResearchPanel P = morpheusPane(pane);
  vec3 l = researchPanelFrame(q, P);
  vec2 u = l.xy, hs = P.h;                         // (monitor metres, its centre the origin)
  vec3 d = transpose(uPlaneRot)*rd;
  vec2 h = vec2(atan(d.x, -d.z), atan(d.y, -d.z));   // (body-frame angles of the ray, rad)
  const vec3 GRN = vec3(0.22, 1.0, 0.45), AMB = vec3(1.0, 0.68, 0.22), RED = vec3(1.0, 0.22, 0.16);
  float hud = 0.0, lab = 0.0, warn = 0.0, w = 0.0018, px = 0.0024;
  // the frame: corner brackets 6 cm along each edge, 2 cm in
  {
    vec2 a = abs(u) - (hs - 0.02);
    hud = max(hud, hudLine(abs(max(a.x, a.y)), w)*step(min(a.x, a.y), 0.0)*step(-0.06, min(a.x, a.y)));
  }
  float tH = pane == 0 ? 0.020 : 0.016;            // (labels' cap height, m)
  vec2 tl = vec2(-hs.x + 0.035, hs.y - 0.045);       // (the top-left label's pen)
  // the camera's label, top left; the uplink and the recording, top right
  lab = max(lab, txt(u - tl, tH, ivec4(67, 65, 77, 32), ivec4(49 + pane, 0, 0, 0), 0));
  lab = max(lab, txt(u - tl - vec2(tH*5.56, 0.0), tH, pane == 0 ? ivec4(78, 79, 83, 69) : pane == 1 ? ivec4(80, 79, 82, 84) : pane == 2 ? ivec4(83, 84, 66, 68) : ivec4(67, 72, 73, 78), ivec4(0), 0));
  if (pane == 0) {
    // "UPLINK", the link's latency in ms under it (a little jitter on it); the recording's lamp and "REC" bottom right
    vec2 tr = vec2(hs.x - 0.035, hs.y - 0.045);
    lab = max(lab, txt(u - tr, tH, ivec4(85, 80, 76, 73), ivec4(78, 75, 0, 0), 2));
    lab = max(lab, txt(u - tr - vec2(0.0, -tH*1.7), tH, ivec4(77, 83, 0, 0), ivec4(0), 2));
    hud = max(hud, hudNum(u - vec2(hs.x - 0.128, hs.y - 0.091), 36.0 + 5.0*fract(sin(floor(uTime*3.0)*12.9898)*43758.5), 2, vec2(0.012, 0.022)));
    warn = max(warn, step(length(u - vec2(hs.x - 0.12, -hs.y + 0.045)), 0.008)*step(0.5, fract(uTime*1.2)));
    lab = max(lab, txt(u - vec2(hs.x - 0.035, -hs.y + 0.045), tH, ivec4(82, 69, 67, 0), ivec4(0), 2));
  }
  if (pane == 0 || pane == 3) {
    // the boresight; the flight-path marker where the aircraft is going
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
  }
  // the horizon and the pitch ladder, conformal to the world (dashed below the horizon); on the side cameras the
  // horizon alone
  {
    float wel = asin(clamp(rd.y, -1.0, 1.0));
    vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0);
    float az = atan(rd.x, -rd.z) - atan(fw.x, -fw.z); az = mod(az + 3.14159, 6.28318) - 3.14159;
    float k = floor(wel/0.17453 + 0.5), ld = abs(wel - k*0.17453);
    if (k == 0.0) hud = max(hud, hudLine(ld, px*1.4)*(pane == 0 ? step(abs(az), 0.75)*step(0.05, abs(az)) : step(0.5, fract(az*12.0))));
    else if (pane == 0) {
      float seg = step(0.07, abs(az))*step(abs(az), 0.22)*(k < 0.0 ? step(0.5, fract(az*45.0)) : 1.0);
      hud = max(hud, hudLine(ld, px)*seg);
      hud = max(hud, hudLine(abs(abs(az) - 0.22), px)*step(abs(wel - k*0.17453 + sign(k)*0.012), 0.012));
    }
  }
  float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);   // (the ray's heading)
  if (pane == 0) {
    // the heading tape along the top; the speed (kt) and the altitude (ft) in boxes either side, Mach and g under the
    // speed, the throttle's bar at the left edge
    if (u.y > hs.y - 0.105 && u.y < hs.y - 0.075 && abs(u.x) < 0.26) {
      float f10 = abs(fract(hd/10.0 + 0.5) - 0.5)*10.0, tall = abs(fract(hd/30.0 + 0.5) - 0.5)*30.0 < 0.5 ? 1.0 : 0.0;
      hud = max(hud, step(f10*0.01745, px*0.8)*step(u.y, hs.y - 0.093 + 0.012*tall));
    }
    hud = max(hud, hudNum(u - vec2(-0.033, hs.y - 0.145), uHud.z, 3, vec2(0.016, 0.030)));
    hud = max(hud, hudBox(u, vec2(0.0, hs.y - 0.13), vec2(0.042, 0.022), w));
    vec2 sp = vec2(-hs.x + 0.13, 0.0), al = vec2(hs.x - 0.15, 0.0);
    hud = max(hud, hudBox(u, sp, vec2(0.068, 0.024), w));
    hud = max(hud, hudNum(u - sp - vec2(-0.058, -0.015), uHud.x*1.94384, 4, vec2(0.018, 0.030)));
    hud = max(hud, hudBox(u, al, vec2(0.085, 0.024), w));
    hud = max(hud, hudNum(u - al - vec2(-0.075, -0.015), uHud.y*3.28084, 5, vec2(0.018, 0.030)));
    vec2 mp = sp + vec2(-0.058, -0.056), gp = sp + vec2(-0.058, -0.090);   // ("M" and "G" pens; their numbers after)
    hud = max(hud, max(txt(u - mp, 0.017, ivec4(77, 0, 0, 0), ivec4(0), 0), numC(u - mp - vec2(0.03, 0.0), uHud.w, 0.017, 0, 2)));
    float g = abs(uHud2.x), gw = step(400.0, g);
    float gn = max(txt(u - gp, 0.017, ivec4(71, 0, 0, 0), ivec4(0), 0), numC(u - gp - vec2(0.03, 0.0), g, 0.017, 0, 1));
    hud = max(hud, gn*(1.0 - gw)); warn = max(warn, gn*gw);
    vec2 tb = vec2(-hs.x + 0.045, -0.12);
    hud = max(hud, hudBox(u, tb, vec2(0.008, 0.08), w));
    hud = max(hud, step(abs(u.x - tb.x), 0.005)*step(tb.y - 0.08, u.y)*step(u.y, tb.y - 0.08 + 0.16*uHud2.y));
  } else if (pane <= 2) {
    // the side cameras: the heading they look along, top centre
    hud = max(hud, hudNum(u - vec2(-0.025, hs.y - 0.06), hd, 3, vec2(0.013, 0.024)));
  } else {
    // the chin camera: the radar altitude (ft) under the boresight, the nozzles' state when they are down
    hud = max(hud, hudNum(u - vec2(-0.045, -hs.y + 0.025), uHud3.w*3.28084, 5, vec2(0.012, 0.022)));
    hud = max(hud, hudBox(u, vec2(0.0, -hs.y + 0.036), vec2(0.052, 0.018), w));
    if (uHud2.z > 0.3) lab = max(lab, txt(u - vec2(hs.x - 0.035, hs.y - 0.045), tH, ivec4(86, 84, 79, 76), ivec4(0), 2));   // "VTOL"
  }
  float a = clamp(max(max(hud, lab), warn), 0.0, 1.0);
  vec3 c = warn > 0.5 ? RED : lab > hud ? AMB : GRN;
  return vec4(c*1.6, a);
}
#endif
