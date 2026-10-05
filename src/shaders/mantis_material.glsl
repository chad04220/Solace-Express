//! kMantisMaterial
//! The XR-10 Mantis's own materials (ids 110-119: titanium, amber datums, the camera panes and the cabin trim), its
//! sealed cockpit's fixture lighting and the look of its three camera panes with their symbology.
// ---------------------------------------------------------------- cabin trim (Codex's static patterning, body space)
float mantisLine(vec2 p, vec2 a, vec2 b, float w){ vec2 q = p - a; vec2 d = b - a; return 1.0 - smoothstep(w, w + 0.002, length(q - d*clamp(dot(q, d)/dot(d, d), 0.0, 1.0))); }
float mantisGlyph(vec2 p, int c){   // 3x5 block lettering: C A M S T I R E F O 0 1 2
  if (any(lessThan(p, vec2(0.0))) || any(greaterThanEqual(p, vec2(3.0, 5.0)))) return 0.0;
  int k = int(floor(p.x)) + 3*(4 - int(floor(p.y))); int b = 0;
  if (c == 0) b = 29263; else if (c == 1) b = 23530; else if (c == 2) b = 23549; else if (c == 3) b = 31183; else if (c == 4) b = 9367;
  else if (c == 5) b = 29847; else if (c == 6) b = 23275; else if (c == 7) b = 29391; else if (c == 8) b = 4815; else if (c == 9) b = 31599;
  else if (c == 10) b = 29850; else if (c == 11) b = 29671;
  return float((b >> k) & 1);
}
float mantisWord(vec2 uv, int word){
  vec2 t = uv/0.009; int col = int(floor(t.x/4.0)); vec2 q = vec2(mod(t.x, 4.0), t.y); int c = -1;
  if (word == 0) { if (col == 0) c = 0; if (col == 1) c = 1; if (col == 2) c = 2; }                      // CAM
  if (word == 1) { if (col == 0) c = 3; if (col == 1) c = 4; if (col == 2) c = 1; if (col == 3) c = 4; if (col == 4) c = 5; if (col == 5) c = 0; }   // STATIC
  if (word == 2) { if (col == 0) c = 6; if (col == 1) c = 7; if (col == 2) c = 8; }                      // REF
  return c < 0 ? 0.0 : mantisGlyph(q, c);
}
vec3 mantisCabinAlbedo(int mid, vec3 p){
  vec3 amber = vec3(1.0, 0.48, 0.08), graph = vec3(0.035, 0.045, 0.054);
  if (mid == 113) return graph*(0.93 + 0.07*step(0.5, fract(p.z*65.0)));
  if (mid == 114) return vec3(0.11, 0.125, 0.135)*(0.92 + 0.08*step(0.5, fract(p.y*90.0)));
  if (mid == 115) return amber;
  if (mid == 116) return vec3(0.41, 0.29, 0.13)*(0.85 + 0.15*step(0.5, fract(p.y*120.0)));
  if (mid == 118) return vec3(0.009, 0.014, 0.017);
  if (mid == 119) return vec3(0.25, 0.29, 0.31);
  if (mid == 117) {   // the reference instrument panels: REF and four bars, static
    bool side = abs(p.x) > 0.42; vec2 uv = side ? vec2(p.z + 3.66, (abs(p.x) - 0.489)*1.7) : vec2(p.x - sign(p.x)*0.266, p.y - 0.07);
    if (side && p.x < 0.0) uv = -uv;
    float text = mantisWord(uv - vec2(-0.095, 0.025), 2);
    float bars = 0.0;
    for (int i = 0; i < 4; i++) { float y = -0.015 - float(i)*0.017; bars = max(bars, step(abs(uv.y - y), 0.003)*step(-0.095, uv.x)*step(uv.x, 0.055 - float(i)*0.028)); }
    return mix(vec3(0.015, 0.035, 0.042), amber, max(text, bars));
  }
  return graph;
}
// The Mantis's materials: ids 110-119 and its own finish on the shared ids. interior: the cabin ids light as a cabin.
void mantisMaterial(inout Mat m, int mid, vec3 lp, vec3 ln, inout bool interior){
  if (mid == 110) {   // exposed titanium: brushed along the body
    float br = 0.9 + 0.1*step(0.5, fract(lp.z*140.0 + lp.y*7.0));
    m.alb = vec3(0.34, 0.39, 0.43)*br; m.metal = 0.8; m.rough = 0.35;
  } else if (mid == 111) { m.alb = vec3(0.95, 0.43, 0.055); m.rough = 0.48; m.metal = 0.0; }   // amber test datums
  else if (mid == 112) { m.alb = vec3(0.035, 0.17, 0.2); m.rough = 0.22; m.metal = 0.0; interior = true; }   // a camera pane (its picture: mantisScreen)
  else if (mid >= 113 && mid <= 119) {
    m.alb = mantisCabinAlbedo(mid, lp); m.rough = mid == 119 ? 0.35 : 0.48; m.metal = mid == 119 ? 0.5 : 0.0;
    if (mid == 115) m.emit = vec3(1.0, 0.48, 0.08)*0.6;
    if (mid == 117) m.emit = m.alb*0.9;
    interior = true;
  } else if (mid == 1) {   // graphite pressure skin, a little darker and less glossy than the fleet's paint
    m.alb = gColBase; m.rough = 0.42;
  } else if (mid == 2 || mid == 3) { m.alb = gColBase*0.96; m.rough = 0.4; }
  else if (mid == 5) { m.alb = gColBase*0.92; m.rough = 0.38; }
  else if (mid == 13) { m.alb = vec3(0.04); m.rough = 0.6; interior = true; }   // stick and throttle grips
  else if (mid == 61) interior = true;                                           // the pedals
}
// The sealed cabin's light: two amber task lights over the glareshield, the three camera panes lighting the cabin with
// what they show (the sky ahead and either side), the amber console strips, and a faint bounce
vec3 mantisPodLight(vec3 p, vec3 n, vec3 v, Mat m, vec3 E){
  vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0), upw = uPlaneRot*vec3(0.0, 1.0, 0.0), rw = uPlaneRot*vec3(1.0, 0.0, 0.0);
  float day = smoothstep(-0.08, 0.15, uSunDir.y);
  vec3 skyF = skyColor(normalize(fw + upw*0.1))*0.9*(0.3 + 0.7*day) + vec3(0.02, 0.05, 0.06);
  vec3 L = m.alb*vec3(0.03, 0.035, 0.04);
  L += fixtureLight(p, n, v, m, E + vec3(-0.41, -0.24, -0.78), E + vec3(0.41, -0.24, -0.78), skyF*0.26, 3.0, vec3(0.0, 0.0, 1.0));
  for (int i = -1; i <= 1; i += 2) {
    float s = float(i);
    vec3 skyS = skyColor(normalize(rw*s + upw*0.15))*0.9*(0.3 + 0.7*day) + vec3(0.02, 0.05, 0.06);
    L += fixtureLight(p, n, v, m, E + vec3(0.55*s, -0.285, -0.53), E + vec3(0.55*s, -0.285, 0.21), skyS*0.14, 3.0, vec3(-s, 0.0, 0.0));
    L += fixtureLight(p, n, v, m, E + vec3(0.215*s, 0.056, -0.43), E + vec3(0.215*s, 0.056, -0.03), vec3(1.0, 0.55, 0.15)*0.22, 8.0, vec3(0.0, -1.0, 0.0));   // task lights
    L += fixtureLight(p, n, v, m, E + vec3(0.15*s, -0.795, -0.63), E + vec3(0.39*s, -0.795, -0.63), vec3(1.0, 0.48, 0.08)*0.05, 14.0, vec3(0.0, 0.8, 0.6));   // console strips
  }
  return L;
}
// A camera pane's picture with its look and symbology: the front pane carries the boresight, flight-path marker,
// horizon and the airspeed / altitude / heading readouts; the side panes a heading readout and frame ticks.
vec3 mantisScreen(vec3 col, vec3 rd, int id, vec3 sl){
  vec3 E = gM[22].xyz; vec3 q = sl - E;
  bool front = q.z < -0.5;
  col = pow(max(col, vec3(0.0)), vec3(1.04))*vec3(0.96, 1.03, 1.04)*1.08;
  col *= 0.94 + 0.06*sin(sl.y*2100.0);
  float edge = front ? min(0.41 - abs(q.x), 0.177 - abs(q.y + 0.24)) : min(0.37 - abs(q.z + 0.16), 0.172 - abs(q.y + 0.285));
  col *= smoothstep(0.0, 0.03, edge);
  vec3 hc = vec3(1.0, 0.62, 0.2);
  float hud = 0.0;
  vec3 d = transpose(uPlaneRot)*rd;
  vec2 h = vec2(atan(d.x, -d.z), atan(d.y, -d.z));
  float px = 0.0026;
  if (front) {
    hud = max(hud, hudLine(abs(h.y), px)*step(abs(h.x), 0.03)*step(0.01, abs(h.x)));
    hud = max(hud, hudLine(abs(h.x), px)*step(abs(h.y), 0.018)*step(0.01, abs(h.y)));
    vec3 vb = uHudV;
    if (vb.z < -0.1) {
      vec2 fp = vec2(atan(vb.x, -vb.z), atan(vb.y, -vb.z)) - h;
      hud = max(hud, hudLine(abs(length(fp) - 0.013), px));
      hud = max(hud, hudLine(abs(fp.y), px)*step(0.013, abs(fp.x))*step(abs(fp.x), 0.036));
    }
    float wel = asin(clamp(rd.y, -1.0, 1.0));
    vec3 fwd = uPlaneRot*vec3(0.0, 0.0, -1.0);
    float az = atan(rd.x, -rd.z) - atan(fwd.x, -fwd.z); az = mod(az + 3.14159, 6.28318) - 3.14159;
    float k = floor(wel/0.17453 + 0.5), ld = abs(wel - k*0.17453);
    if (k == 0.0) hud = max(hud, hudLine(ld, px*1.4)*step(abs(az), 0.5)*step(0.05, abs(az)));
    else hud = max(hud, hudLine(ld, px)*step(0.07, abs(az))*step(abs(az), 0.18)*(k < 0.0 ? step(0.5, fract(az*45.0)) : 1.0));
    hud = max(hud, hudBox(h, vec2(-0.33, -0.15), vec2(0.07, 0.028), px));
    hud = max(hud, hudNum(h - vec2(-0.385, -0.168), uHud.x*1.94384, 4, vec2(0.02, 0.036)));
    hud = max(hud, hudBox(h, vec2(0.33, -0.15), vec2(0.085, 0.028), px));
    hud = max(hud, hudNum(h - vec2(0.263, -0.168), uHud.y*3.28084, 5, vec2(0.02, 0.036)));
    hud = max(hud, hudNum(h - vec2(-0.04, 0.02), uHud.z, 3, vec2(0.02, 0.036)));
    hud = max(hud, step(abs(h.x + 0.33), 0.007)*step(-0.28, h.y)*step(h.y, -0.28 + 0.1*uHud2.y));   // throttle bar
    if (uHud2.w > 0.5) for (int g = 0; g < 3; g++) hud = max(hud, step(length(h - vec2(0.27 + 0.03*float(g), -0.25)), 0.007));
  } else {
    float hd = mod(degrees(atan(rd.x, -rd.z)) + 360.0, 360.0);
    vec2 u = vec2((q.z + 0.16)*(q.x < 0.0 ? -1.0 : 1.0), q.y + 0.285);
    hud = max(hud, hudNum(u - vec2(-0.03, 0.12), hd, 3, vec2(0.012, 0.02)));
    hud = max(hud, hudLine(abs(u.y), 0.0012)*step(0.3, abs(u.x)));
  }
  return mix(col, hc*1.5, clamp(hud, 0.0, 1.0)*0.85);
}
