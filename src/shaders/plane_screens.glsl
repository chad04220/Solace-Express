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
