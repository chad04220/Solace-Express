//! kRaytraceDisplays
// ---------------------------------------------------------------- cockpit displays and gauges
// Every element is a signed distance anti-aliased over the pixel footprint gAA (in the caller's units, set per
// pixel from the hit distance), so needles, scales and lettering stay crisp at any display resolution. Lettering
// and numerals come from the SDF font atlas.
float gAA = 0.002;
float gPixM = 0.001;
bool gDispPx = false;   // this pixel shows a display or gauge face: the post pass doesn't sharpen it   // metres of surface per pixel at the current hit (set before shading cockpit displays)
float aFill(float d){ return clamp(0.5 - d/gAA, 0.0, 1.0); }
float aLine(float d, float w){   // a stroke never thinner than 1.5 px: thin lines stay continuous, dimmed to keep their weight
  float we = max(w, 1.5*gAA);
  return clamp(0.5 - (abs(d) - we*0.5)/gAA, 0.0, 1.0)*sqrt(w/we);
}
float aStroke(float d, float w){ return aLine(d, w); }   // d: unsigned distance to the centre line
float sdSeg(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba)/max(dot(ba, ba), 1e-9), 0.0, 1.0); return length(pa - ba*h); }
float sdBox(vec2 p, vec2 b){ vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRBox(vec2 p, vec2 b, float r){ return sdBox(p, b - r) - r; }
float sdTri(vec2 p, vec2 a, vec2 b, vec2 c){   // signed distance to a triangle
  vec2 e0 = b - a, e1 = c - b, e2 = a - c, v0 = p - a, v1 = p - b, v2 = p - c;
  vec2 pq0 = v0 - e0*clamp(dot(v0, e0)/dot(e0, e0), 0.0, 1.0), pq1 = v1 - e1*clamp(dot(v1, e1)/dot(e1, e1), 0.0, 1.0), pq2 = v2 - e2*clamp(dot(v2, e2)/dot(e2, e2), 0.0, 1.0);
  float s = sign(e0.x*e2.y - e0.y*e2.x);
  vec2 d = min(min(vec2(dot(pq0, pq0), s*(v0.x*e0.y - v0.y*e0.x)), vec2(dot(pq1, pq1), s*(v1.x*e1.y - v1.y*e1.x))), vec2(dot(pq2, pq2), s*(v2.x*e2.y - v2.y*e2.x)));
  return -sqrt(d.x)*sign(d.y);
}
// font: the glyph edge is softened to one screen pixel (0.1 of the SDF = one font pixel)
float fontSoft(float s){ return clamp(0.06*gAA/s, 0.015, 0.3); }
float chAdv(int c){ return (c == 32 || c == 46 || c == 58 || c == 73 || c == 39) ? 13.0 : (c == 77 || c == 87) ? 37.0 : (c == 45 || c == 47) ? 17.0 : (c == 76 || c == 70 || c == 69 || c == 84) ? 25.0 : 29.0; }
// up to 8 characters (codes in a, b; 0 ends), cap height h, vertically centred on p.y = 0;
// align 0: starts at p.x = 0, 1: centred, 2: ends at p.x = 0
// character i of the eight in a, b (selects, not an indexed array: txt is inlined at hundreds of sites and NVIDIA
// runs out of scratch resources when each copy carries its own array)
int chAt(ivec4 a, ivec4 b, int i){ ivec4 v = i < 4 ? a : b; int j = i - (i < 4 ? 0 : 4); return j == 0 ? v.x : j == 1 ? v.y : j == 2 ? v.z : v.w; }
float txt(vec2 p, float h, ivec4 a, ivec4 b, int align){
  float s = h/27.0;
  vec2 f = p/s + vec2(0.0, 13.5);
  if (f.y < -10.0 || f.y > 42.0) return 0.0;
  float tw = 0.0;
  for (int i = 0; i < 8; i++) { int c = chAt(a, b, i); if (c == 0) break; tw += chAdv(c); }
  float x = align == 1 ? -tw*0.5 : align == 2 ? -tw : 0.0;
  if (f.x < x - 6.0 || f.x > x + tw + 6.0) return 0.0;
  float k = gTxtSoft; gTxtSoft = fontSoft(s);
  float on = 0.0;
  for (int i = 0; i < 8; i++) {
    int c = chAt(a, b, i);
    if (c == 0) break;
    float w = chAdv(c);
    if (f.x >= x - 4.0 && f.x < x + w + 4.0) on = max(on, glyphCov(f - vec2(x, 0.0), c));
    x += w;
  }
  gTxtSoft = k;
  return on;
}
float txt4(vec2 p, float h, ivec4 a, int align){ return txt(p, h, a, ivec4(0), align); }
// a whole number (minus sign when negative, optional decimal point before the last dp digits), centred like txt
float numC(vec2 p, float v, float h, int align, int dp){
  bool neg = v < -0.5*pow(0.1, float(dp));
  v = floor(abs(v)*pow(10.0, float(dp)) + 0.5);
  int nd = max(v >= 10000.0 ? 5 : v >= 1000.0 ? 4 : v >= 100.0 ? 3 : v >= 10.0 ? 2 : 1, dp + 1);
  float s = h/27.0, adv = 26.5, pt = dp > 0 ? 12.0 : 0.0, sg = neg ? 16.0 : 0.0;
  float tw = float(nd)*adv + pt + sg;
  vec2 f = p/s + vec2(0.0, 13.5);
  float x = align == 1 ? -tw*0.5 : align == 2 ? -tw : 0.0;
  if (f.y < -10.0 || f.y > 42.0 || f.x < x - 6.0 || f.x > x + tw + 6.0) return 0.0;
  float k = gTxtSoft; gTxtSoft = fontSoft(s);
  float on = 0.0;
  if (neg) on = glyphCov(f - vec2(x, 0.0), 45);
  x += sg;
  float fx = f.x - x;
  int i = int(clamp(floor(fx/adv), 0.0, float(nd - 1)));
  if (dp > 0 && fx > float(nd - dp)*adv) { i = int(clamp(floor((fx - pt)/adv), float(nd - dp), float(nd - 1))); }
  float xi = x + float(i)*adv + (dp > 0 && i >= nd - dp ? pt : 0.0);
  int dg = int(mod(floor(v/pow(10.0, float(nd - 1 - i))), 10.0));
  on = max(on, glyphCov(f - vec2(xi + 0.8, 0.0), 48 + dg));
  if (dp > 0) on = max(on, glyphCov(f - vec2(x + float(nd - dp)*adv - 2.0, 0.0), 46));
  gTxtSoft = k;
  return on;
}
// ---- round dials (angles clockwise from 12 o'clock)
vec2 dirA(float a){ return vec2(sin(a), cos(a)); }
float wrapA(float a, float a0, float a1){ float m = 0.5*(a0 + a1); return m + mod(a - m + 3.14159265, 6.2831853) - 3.14159265; }
float dTicks(vec2 d, float r, float a0, float a1, float n, float r0, float r1, float w){
  float a = wrapA(atan(d.x, d.y), a0, a1);
  float k = clamp(floor((a - a0)/(a1 - a0)*n + 0.5), 0.0, n);
  vec2 u = dirA(a0 + (a1 - a0)*k/n);
  return aStroke(sdSeg(d, u*r*r0, u*r*r1), 2.0*(w*0.5));
}
float dNums(vec2 d, float r, float a0, float a1, float n, float v0, float dv, float vmod, float rr, float h){
  float a = wrapA(atan(d.x, d.y), a0, a1);
  float k = clamp(floor((a - a0)/(a1 - a0)*n + 0.5), 0.0, n);
  vec2 c = dirA(a0 + (a1 - a0)*k/n)*r*rr;
  return numC(d - c, mod(v0 + dv*k, vmod), h, 1, 0);
}
float dArc(vec2 d, float r, float a0, float a1, float w){
  float a = wrapA(atan(d.x, d.y), a0, a1);
  float ac = clamp(a, min(a0, a1), max(a0, a1));
  return aStroke(length(d - dirA(ac)*r), w);
}
// tapered needle with a counterweight tail and a pointed tip
float dNeedle(vec2 d, float ang, float len, float w, float tail){
  vec2 u = dirA(ang);
  float al = dot(d, u), pe = abs(d.x*u.y - d.y*u.x);
  float hw = w*mix(1.0, 0.45, clamp(al/len, 0.0, 1.0));
  float sd = max(pe - hw, max(-tail - al, al - len));
  sd = max(sd, pe - (len - al)*0.8);
  return aFill(sd);
}
// a steam gauge: machined bezel, matt face with a soft vignette; inside: d within the face
vec3 dialFace(vec2 d, float r, out bool inside){
  float rr = length(d);
  inside = rr < r;
  if (rr > r*1.14 + gAA) return vec3(-1.0);
  vec3 face = vec3(0.012, 0.013, 0.015)*(1.25 - 0.4*rr/r);
  float b = d.y/(r*1.14)*0.5 + 0.5;
  vec3 bez = mix(vec3(0.05, 0.05, 0.055), vec3(0.32, 0.32, 0.34), b)*(0.75 + 0.25*sin(atan(d.x, d.y)*48.0)*0.2);
  bez = mix(bez, vec3(0.008), aLine(rr - r*1.0, r*0.02));
  return mix(face, bez, aFill(r - rr));
}
vec3 glassGlare(vec3 c, vec2 d, float r){   // faint reflection across the cover glass
  float g = smoothstep(0.2, 1.0, dot(normalize(d + vec2(1e-5)), normalize(vec2(-0.6, 0.8))))*smoothstep(1.0, 0.55, length(d)/r);
  return c + vec3(0.025, 0.03, 0.035)*g;
}
const vec3 IW = vec3(0.93, 0.94, 0.9), IG = vec3(0.12, 0.78, 0.3), IY = vec3(0.95, 0.78, 0.12), IR = vec3(0.95, 0.15, 0.1), IO = vec3(1.0, 0.55, 0.05);
// Instrument colour (emissive) for a point q on the panel (metres, relative to the pilot's panel centre)
vec3 drawInstruments(vec2 q, int ck, bool pilotSide){
  float ias = uI0.x, alt = uI0.y, hdg = uI0.z, vs = uI0.w;
  float pitch = uI1.x, bank = uI1.y, engF = uI1.z, fuel = uI1.w;
  float lw = 0.00045;   // standard line width
  if (ck == 2) {
    // ---- glass cockpit: primary flight display
    vec2 pd = q - vec2(-0.07, 0.0);
    if (abs(pd.x) < 0.085 && abs(pd.y) < 0.075) {
      vec3 col = vec3(0.01, 0.012, 0.016);
      vec2 ad = pd - vec2(0.0, 0.006);
      float b = bank*0.01745;
      vec2 o = rot2(ad, b);
      float ppd = 0.0022;   // metres per degree of pitch
      float hz = o.y + pitch*ppd;
      if (abs(ad.x) < 0.052 && abs(ad.y) < 0.058) {
        col = hz > 0.0 ? mix(vec3(0.1, 0.33, 0.78), vec3(0.03, 0.14, 0.45), clamp(hz/0.07, 0.0, 1.0)) : mix(vec3(0.45, 0.27, 0.1), vec3(0.22, 0.12, 0.04), clamp(-hz/0.07, 0.0, 1.0));
        col = mix(col, IW, aLine(hz, lw*1.6));
        for (int i = -4; i <= 4; i++) {
          if (i == 0) continue;
          float ly = hz - float(i)*5.0*ppd;
          float hl = (abs(i) % 2 == 0) ? 0.014 : 0.007;
          col = mix(col, IW, aStroke(sdSeg(vec2(o.x, ly), vec2(-hl, 0.0), vec2(hl, 0.0)), 2.0*(lw*0.5)));
          if (abs(i) % 2 == 0) {
            float v = abs(float(i))*5.0;
            col = mix(col, IW, numC(vec2(o.x - sign(o.x)*(hl + 0.0055), ly), v, 0.0028, 1, 0));
          }
        }
        // bank scale and pointer
        float rr = length(ad);
        float ba = atan(ad.x, ad.y);
        if (rr > 0.036 && rr < 0.05 && abs(ba) < 1.1) {
          float tk = 0.0;
          for (int k = 0; k < 9; k++) {
            float ta = radians(k == 0 ? 0.0 : k < 3 ? 10.0*float(k) : k < 4 ? 30.0 : k < 5 ? 45.0 : k < 6 ? 60.0 : 0.0);
            for (int sgn = -1; sgn <= 1; sgn += 2) {
              vec2 u = dirA(ta*float(sgn));
              float len = (k == 0 || k == 3 || k == 5) ? 0.006 : 0.0035;
              tk = max(tk, aStroke(sdSeg(ad, u*0.04, u*(0.04 + len)), 2.0*(lw*0.5)));
            }
          }
          col = mix(col, IW, tk);
        }
        vec2 pu = rot2(ad, b);
        col = mix(col, IY, aFill(sdTri(pu, vec2(0.0, 0.0395), vec2(-0.0028, 0.0345), vec2(0.0028, 0.0345))));
        // aircraft symbol
        float sym = min(sdBox(ad - vec2(-0.021, 0.0), vec2(0.009, 0.0011)), sdBox(ad - vec2(0.021, 0.0), vec2(0.009, 0.0011)));
        sym = min(sym, min(sdBox(ad - vec2(-0.0125, -0.003), vec2(0.0011, 0.0035)), sdBox(ad - vec2(0.0125, -0.003), vec2(0.0011, 0.0035))));
        sym = min(sym, sdBox(ad, vec2(0.0016)));
        col = mix(col, vec3(0.0), aFill(sym - 0.0006));
        col = mix(col, IY, aFill(sym));
      }
      // speed tape (left) and altitude tape (right) with readout windows
      for (int side = 0; side < 2; side++) {
        float cx = side == 0 ? -0.07 : 0.07;
        vec2 t = pd - vec2(cx, 0.006);
        if (abs(t.x) < 0.0135 && abs(t.y) < 0.058) {
          col = vec3(0.07, 0.075, 0.09);
          float val = side == 0 ? ias : alt, stp = side == 0 ? 10.0 : 100.0, ppu = side == 0 ? 0.0011 : 0.00011;
          float vy = val + t.y/ppu;
          float k = floor(vy/stp + 0.5), dy = (vy - k*stp)*ppu;
          float tx = side == 0 ? 0.0135 : -0.0135, dir = side == 0 ? -1.0 : 1.0;
          col = mix(col, IW, aStroke(sdSeg(vec2(t.x, dy), vec2(tx, 0.0), vec2(tx + dir*(mod(k, 2.0) == 0.0 ? 0.004 : 0.0022), 0.0)), 2.0*(lw*0.5)));
          if (mod(k, 2.0) == 0.0 && k*stp >= 0.0) col = mix(col, IW, numC(vec2(t.x - (side == 0 ? 0.0075 : -0.0075), dy), k*stp, 0.0026, side == 0 ? 2 : 0, 0));
          // readout window
          float wb = sdRBox(t, vec2(0.0135, 0.0042), 0.0008);
          if (wb < 0.0) {
            col = vec3(0.0);
            col = mix(col, IW, numC(t - vec2(side == 0 ? 0.0115 : -0.0115, 0.0), val, 0.0042, side == 0 ? 2 : 0, 0));
          }
          col = mix(col, IW, aLine(wb, lw));
        }
      }
      // heading tape
      vec2 hd = pd - vec2(0.0, -0.064);
      if (abs(hd.x) < 0.052 && abs(hd.y) < 0.0095) {
        col = vec3(0.07, 0.075, 0.09);
        float ppd2 = 0.0011, hv = hdg + hd.x/ppd2;
        float k = floor(hv/5.0 + 0.5), dx = (hv - k*5.0)*ppd2;
        col = mix(col, IW, aStroke(sdSeg(vec2(dx, hd.y), vec2(0.0, 0.0095), vec2(0.0, mod(k, 2.0) == 0.0 ? 0.0055 : 0.0075)), 2.0*(lw*0.5)));
        if (mod(k, 6.0) == 0.0) col = mix(col, IW, numC(vec2(dx, hd.y + 0.0005), mod(k*5.0/10.0, 36.0), 0.0032, 1, 0));
        float wb = sdRBox(hd - vec2(0.0, -0.0045), vec2(0.0065, 0.0042), 0.0008);
        if (wb < 0.0) { col = vec3(0.0); col = mix(col, IW, numC(hd - vec2(0.0, -0.0045), mod(hdg, 360.0), 0.0036, 1, 0)); }
        col = mix(col, IW, aLine(wb, lw));
      }
      return col*1.35;
    }
    // ---- navigation display with an engine strip
    vec2 nd = q - vec2(0.12, 0.0);
    if (abs(nd.x) < 0.075 && abs(nd.y) < 0.075) {
      vec3 col = vec3(0.008, 0.012, 0.018);
      vec2 c = nd + vec2(0.0, 0.035);
      float rr = length(c), a = atan(c.x, c.y);
      if (c.y > -0.01) {
        col = mix(col, IW*0.8, aLine(rr - 0.075, lw));
        col = mix(col, IW*0.35, aLine(rr - 0.0375, lw*0.8)*step(0.5, fract(a*12.0)));
        float ah = degrees(a) + hdg;
        float k = floor(ah/5.0 + 0.5), at = radians(k*5.0 - hdg);
        vec2 u = dirA(at);
        col = mix(col, IW, aStroke(sdSeg(c, u*0.075, u*(0.075 - (mod(k, 2.0) == 0.0 ? 0.0045 : 0.0025))), 2.0*(lw*0.5)));
        if (mod(k, 6.0) == 0.0) col = mix(col, IW, numC(rot2(c - u*0.0655, at), mod(k*0.5, 36.0), 0.0032, 1, 0));
      }
      col = mix(col, vec3(1.0, 0.35, 1.0), aStroke(sdSeg(c, vec2(0.0, 0.004), vec2(0.0, 0.074)), 2.0*(lw*0.5)));
      col = mix(col, IY, aFill(sdTri(c, vec2(0.0, 0.006), vec2(-0.0045, -0.005), vec2(0.0045, -0.005))));
      float wb = sdRBox(nd - vec2(0.0, 0.066), vec2(0.008, 0.0045), 0.0008);
      if (wb < 0.0) { col = vec3(0.0); col = mix(col, IW, numC(nd - vec2(0.0, 0.066), mod(hdg, 360.0), 0.0038, 1, 0)); }
      col = mix(col, IW, aLine(wb, lw));
      // engine strip: N1 bar, value and label
      vec2 e = nd - vec2(0.0, -0.064);
      if (abs(e.y) < 0.009) {
        col = vec3(0.02, 0.025, 0.03);
        col = mix(col, IW*0.7, txt4(e - vec2(-0.072, 0.0), 0.003, ivec4(78, 49, 0, 0), 0));
        float bx = clamp(engF, 0.0, 1.1)/1.1;
        float bar = sdBox(e - vec2(-0.012, 0.0), vec2(0.045, 0.003));
        col = mix(col, vec3(0.1), aFill(bar));
        col = mix(col, engF > 1.0 ? IR : IG, aFill(max(bar, e.x + 0.057 - bx*0.09)));
        col = mix(col, IW, aLine(bar, lw));
        col = mix(col, IW, numC(e - vec2(0.072, 0.0), engF*100.0, 0.0034, 2, 0));
      }
      return col*1.35;
    }
    return vec3(-1.0);
  }
  float r = 0.038;
  bool inD;
  vec2 d; vec3 c;
  float nh = 0.0042;   // numeral height on the dials
  // ---- airspeed (kt): white flap arc, green normal, yellow caution, red never-exceed
  d = q - vec2(-0.095, 0.045);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float vmax = 200.0, A0 = -2.6, A1 = 2.6;
      float vso = 45.0, vfe = 85.0, vno = 120.0, vne = 155.0;
      c = mix(c, IW, dArc(d, r*0.9, A0 + vso/vmax*5.2, A0 + vfe/vmax*5.2, r*0.05)*0.85);
      c = mix(c, IG, dArc(d, r*0.85, A0 + vso/vmax*5.2 + 0.08, A0 + vno/vmax*5.2, r*0.06));
      c = mix(c, IY, dArc(d, r*0.85, A0 + vno/vmax*5.2, A0 + vne/vmax*5.2, r*0.06));
      vec2 ur = dirA(A0 + vne/vmax*5.2); c = mix(c, IR, aStroke(sdSeg(d, ur*r*0.78, ur*r*0.96), 2.0*(r*0.025)));
      c = mix(c, IW, dTicks(d, r, A0, A1, 40.0, 0.88, 0.96, lw*0.7));
      c = mix(c, IW, dTicks(d, r, A0, A1, 10.0, 0.8, 0.96, lw*1.2));
      c = mix(c, IW, dNums(d, r, A0 + 5.2*0.2, A1, 4.0, 40.0, 40.0, 1e5, 0.62, nh));
      c = mix(c, IW*0.8, txt4(d - vec2(0.0, -r*0.4), nh*0.55, ivec4(75, 78, 79, 84), 1));
      c = mix(c, vec3(0.0), dNeedle(d - vec2(0.0006, -0.0006), A0 + clamp(ias, 0.0, vmax)/vmax*5.2, r*0.86, r*0.045, r*0.18)*0.6);
      c = mix(c, IW, dNeedle(d, A0 + clamp(ias, 0.0, vmax)/vmax*5.2, r*0.86, r*0.045, r*0.18));
      c = mix(c, vec3(0.08), aFill(length(d) - r*0.07));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- attitude
  d = q - vec2(0.0, 0.045);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float b = bank*0.01745;
      vec2 o = rot2(d, b);
      float ppd = 0.0012, hz = o.y + pitch*ppd;
      c = hz > 0.0 ? mix(vec3(0.18, 0.45, 0.88), vec3(0.06, 0.22, 0.6), clamp(hz/r, 0.0, 1.0)) : mix(vec3(0.5, 0.31, 0.13), vec3(0.24, 0.14, 0.05), clamp(-hz/r, 0.0, 1.0));
      c = mix(c, IW, aLine(hz, lw*1.4));
      for (int i = -4; i <= 4; i++) {
        if (i == 0) continue;
        float ly = hz - float(i)*5.0*ppd, hl = abs(i) % 2 == 0 ? r*0.26 : r*0.12;
        c = mix(c, IW, aStroke(sdSeg(vec2(o.x, ly), vec2(-hl, 0.0), vec2(hl, 0.0)), 2.0*(lw*0.45)));
        if (abs(i) % 2 == 0) c = mix(c, IW, numC(vec2(o.x - sign(o.x)*(hl + r*0.12), ly), abs(float(i))*5.0, nh*0.6, 1, 0));
      }
      // rotating bank ring with ticks, fixed pointer
      float rr = length(d);
      if (rr > r*0.8) {
        c = rr > r*0.8 ? mix(c, hz > -0.0 ? vec3(0.1, 0.3, 0.7) : vec3(0.32, 0.2, 0.08), 0.6) : c;
        float tk = 0.0;
        for (int k = 0; k < 6; k++) {
          float ta = radians(k == 0 ? 0.0 : k == 1 ? 10.0 : k == 2 ? 20.0 : k == 3 ? 30.0 : k == 4 ? 60.0 : 90.0);
          for (int sgn = -1; sgn <= 1; sgn += 2) {
            vec2 u = dirA(ta*float(sgn) - b);
            float len = (k == 3 || k == 4) ? 0.14 : 0.08;
            tk = max(tk, aStroke(sdSeg(d, u*r*0.82, u*r*(0.82 + len)), 2.0*(lw*0.6)));
          }
        }
        c = mix(c, IW, tk);
        c = mix(c, IW, aFill(sdTri(rot2(d, b), vec2(0.0, r*0.82), vec2(-r*0.05, r*0.95), vec2(r*0.05, r*0.95))));
      }
      c = mix(c, IO, aFill(sdTri(d, vec2(0.0, r*0.79), vec2(-r*0.06, r*0.66), vec2(r*0.06, r*0.66))));
      float sym = min(sdBox(d - vec2(-r*0.42, 0.0), vec2(r*0.2, r*0.025)), sdBox(d - vec2(r*0.42, 0.0), vec2(r*0.2, r*0.025)));
      sym = min(sym, length(d) - r*0.045);
      c = mix(c, vec3(0.0), aFill(sym - 0.0005));
      c = mix(c, IO, aFill(sym));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- altimeter: 100 ft needle, 1000 ft needle, 10 000 ft pointer, digital drum
  d = q - vec2(0.095, 0.045);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float P = 3.14159265;
      c = mix(c, IW, dTicks(d, r, -P, P, 50.0, 0.88, 0.96, lw*0.6));
      c = mix(c, IW, dTicks(d, r, -P, P, 10.0, 0.78, 0.96, lw*1.2));
      c = mix(c, IW, dNums(d, r, 0.0, 2.0*P, 10.0, 0.0, 1.0, 10.0, 0.62, nh*1.05));
      vec2 wd = d - vec2(0.0, -r*0.27);
      float wb = sdRBox(wd, vec2(r*0.3, r*0.11), r*0.02);
      if (wb < 0.0) { c = vec3(0.0); c = mix(c, IW, numC(wd - vec2(r*0.27, 0.0), alt, nh*0.62, 2, 0)); }
      c = mix(c, vec3(0.35), aLine(wb, lw));
      c = mix(c, IW*0.8, txt4(d - vec2(0.0, r*0.33), nh*0.5, ivec4(70, 69, 69, 84), 1));
      float a10k = alt/100000.0*2.0*P, a1k = alt/10000.0*2.0*P, a100 = alt/1000.0*2.0*P;
      vec2 u = dirA(a10k);
      c = mix(c, IW, aFill(sdTri(d, u*r*0.98, u*r*0.86 + vec2(u.y, -u.x)*r*0.05, u*r*0.86 - vec2(u.y, -u.x)*r*0.05)));
      c = mix(c, vec3(0.0), dNeedle(d - vec2(0.0006, -0.0006), a1k, r*0.55, r*0.08, r*0.1)*0.6);
      c = mix(c, IW, dNeedle(d, a1k, r*0.55, r*0.08, r*0.1));
      c = mix(c, vec3(0.0), dNeedle(d - vec2(0.0006, -0.0006), a100, r*0.88, r*0.04, r*0.18)*0.6);
      c = mix(c, IW, dNeedle(d, a100, r*0.88, r*0.04, r*0.18));
      c = mix(c, vec3(0.08), aFill(length(d) - r*0.07));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- turn coordinator: banking aircraft symbol, rate marks, slip ball in its tube
  d = q - vec2(-0.095, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float rt = clamp(uI2.x/3.0, -1.5, 1.5)*0.2618;
      for (int sgn = -1; sgn <= 1; sgn += 2) {
        vec2 u0 = dirA(float(sgn)*1.5708), u1 = dirA(float(sgn)*(1.5708 + 0.2618));
        c = mix(c, IW, aStroke(sdSeg(d, u0*r*0.72, u0*r*0.9), 2.0*(lw*0.9)));
        c = mix(c, IW, aStroke(sdSeg(d, u1*r*0.72, u1*r*0.9), 2.0*(lw*0.9)));
      }
      c = mix(c, IW, txt4(d - vec2(-r*0.66, -r*0.3), nh*0.6, ivec4(76, 0, 0, 0), 1));
      c = mix(c, IW, txt4(d - vec2(r*0.66, -r*0.3), nh*0.6, ivec4(82, 0, 0, 0), 1));
      c = mix(c, IW*0.7, txt(d - vec2(0.0, r*0.5), nh*0.45, ivec4(50, 32, 77, 73), ivec4(78, 0, 0, 0), 1));
      vec2 dd = rot2(d, rt);
      float plane = min(sdBox(dd, vec2(r*0.66, r*0.035)), length(dd) - r*0.09);
      plane = min(plane, sdBox(dd - vec2(0.0, r*0.12), vec2(r*0.025, r*0.08)));
      c = mix(c, IW, aFill(plane));
      vec2 tb = d - vec2(0.0, -r*0.48);
      float tube = sdRBox(tb, vec2(r*0.5, r*0.11), r*0.11);
      if (tube < 0.0) c = mix(c, vec3(0.05, 0.05, 0.04), 0.85);
      c = mix(c, IW*0.6, aLine(tube, lw*0.7));
      c = mix(c, IW, aLine(abs(tb.x) - r*0.13, lw*0.8)*step(abs(tb.y), r*0.11));
      vec2 bc = vec2(clamp(-uI2.y*0.0015, -r*0.4, r*0.4), 0.0);
      c = mix(c, vec3(0.02), aFill(length(tb - bc) - r*0.095));
      c = mix(c, vec3(0.25), aFill(length(tb - bc + vec2(r*0.03, -r*0.03)) - r*0.03));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- heading indicator: rotating compass card, fixed lubber and aircraft
  d = q - vec2(0.0, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float P = 3.14159265, ch = radians(hdg);
      vec2 cd = rot2(d, -ch);   // card frame
      c = mix(c, IW, dTicks(cd, r, -P, P, 72.0, 0.88, 0.96, lw*0.6));
      c = mix(c, IW, dTicks(cd, r, -P, P, 36.0, 0.82, 0.96, lw*0.9));
      // labels every 30 degrees, upright along the card
      float a = atan(cd.x, cd.y), k = floor(a/(P/6.0) + 0.5), la = k*P/6.0;
      vec2 lc = rot2(cd - dirA(la)*r*0.66, la);
      int ki = int(mod(k + 12.0, 12.0));
      float lab = ki == 0 ? txt4(lc, nh, ivec4(78, 0, 0, 0), 1) : ki == 3 ? txt4(lc, nh, ivec4(69, 0, 0, 0), 1) : ki == 6 ? txt4(lc, nh, ivec4(83, 0, 0, 0), 1) : ki == 9 ? txt4(lc, nh, ivec4(87, 0, 0, 0), 1)
                : numC(lc, float(ki*3), nh*0.85, 1, 0);
      c = mix(c, ki % 3 == 0 ? IO : IW, lab);
      for (int k2 = 0; k2 < 8; k2++) { vec2 u = dirA(float(k2)*P/4.0); c = mix(c, IO, aStroke(sdSeg(d, u*r*0.97, u*r*1.0), 2.0*(lw))); }
      c = mix(c, IO, aFill(sdTri(d, vec2(0.0, r*0.8), vec2(-r*0.05, r*0.93), vec2(r*0.05, r*0.93))));
      float pl = min(sdBox(d - vec2(0.0, -r*0.05), vec2(r*0.025, r*0.32)), sdBox(d - vec2(0.0, r*0.03), vec2(r*0.28, r*0.03)));
      pl = min(pl, sdBox(d - vec2(0.0, -r*0.33), vec2(r*0.12, r*0.025)));
      c = mix(c, IO, aFill(pl));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- vertical speed (x100 ft/min)
  d = q - vec2(0.095, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float A = 2.97, z = -1.5708;
      c = mix(c, IW, dTicks(d, r, z - A, z + A, 40.0, 0.88, 0.96, lw*0.6));
      c = mix(c, IW, dTicks(d, r, z - A, z + A, 8.0, 0.8, 0.96, lw*1.2));
      float a = wrapA(atan(d.x, d.y), z - A, z + A);
      float k = clamp(floor((a - (z - A))/(2.0*A)*8.0 + 0.5), 0.0, 8.0);
      vec2 lc = dirA(z - A + 2.0*A*k/8.0)*r*0.62;
      c = mix(c, IW, numC(d - lc, abs(k - 4.0)*5.0, nh*0.95, 1, 0));
      c = mix(c, IW*0.8, txt4(d - vec2(r*0.36, r*0.2), nh*0.5, ivec4(85, 80, 0, 0), 1));
      c = mix(c, IW*0.8, txt4(d - vec2(r*0.36, -r*0.2), nh*0.5, ivec4(68, 78, 0, 0), 1));
      c = mix(c, IW*0.6, txt4(d - vec2(-r*0.3, -r*0.28), nh*0.42, ivec4(70, 80, 77, 0), 1));
      float na = z + clamp(vs/2000.0, -1.0, 1.0)*A;
      c = mix(c, vec3(0.0), dNeedle(d - vec2(0.0006, -0.0006), na, r*0.86, r*0.045, r*0.18)*0.6);
      c = mix(c, IW, dNeedle(d, na, r*0.86, r*0.045, r*0.18));
      c = mix(c, vec3(0.08), aFill(length(d) - r*0.07));
      c = glassGlare(c, d, r);
    }
    return c;
  }
  // ---- engine RPM / N1 and fuel (pilot's side)
  if (!pilotSide) return vec3(-1.0);
  int engines = ck == 1 ? 2 : 1;
  for (int e = 0; e < 2; e++) {
    if (e >= engines) break;
    float re = r*0.85;
    d = q - vec2(0.2 + float(e)*0.085, 0.045);
    c = dialFace(d, re, inD);
    if (c.x >= 0.0) {
      if (inD) {
        float A0 = -2.36, A1 = -2.36 + 4.2/1.1*1.1;
        c = mix(c, IG, dArc(d, re*0.86, A0 + 4.2*0.55, A0 + 4.2*0.96, re*0.07));
        vec2 ur = dirA(A0 + 4.2); c = mix(c, IR, aStroke(sdSeg(d, ur*re*0.78, ur*re*0.96), 2.0*(re*0.03)));
        c = mix(c, IW, dTicks(d, re, A0, A0 + 4.2*1.1, 22.0, 0.88, 0.96, lw*0.6));
        c = mix(c, IW, dTicks(d, re, A0, A0 + 4.2*1.1, 11.0, 0.8, 0.96, lw*1.1));
        float a = wrapA(atan(d.x, d.y), A0, A0 + 4.62);
        float k = clamp(floor((a - A0)/(4.2*1.1)*5.5 + 0.5), 0.0, 5.0);
        c = mix(c, IW, numC(d - dirA(A0 + 4.2*1.1*k/5.5)*re*0.6, k*(ck == 1 ? 20.0 : 6.0), nh*0.9, 1, 0));
        c = mix(c, IW*0.8, txt4(d - vec2(0.0, -re*0.42), nh*0.5, ck == 1 ? ivec4(78, 49, 32, 37) : ivec4(82, 80, 77, 0), 1));
        float na = A0 + clamp(engF, 0.0, 1.1)*4.2;
        c = mix(c, vec3(0.0), dNeedle(d - vec2(0.0006, -0.0006), na, re*0.86, re*0.05, re*0.18)*0.6);
        c = mix(c, IW, dNeedle(d, na, re*0.86, re*0.05, re*0.18));
        c = mix(c, vec3(0.08), aFill(length(d) - re*0.08));
        c = glassGlare(c, d, re);
      }
      return c;
    }
  }
  float rf = r*0.7;
  d = q - vec2(0.2, -0.05);
  c = dialFace(d, rf, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float A0 = -1.2, A1 = 1.2;
      c = mix(c, IR, dArc(d, rf*0.86, A0, A0 + 0.42, rf*0.08));
      c = mix(c, IW, dTicks(d, rf, A0, A1, 8.0, 0.82, 0.96, lw*0.8));
      c = mix(c, IW, txt4(d - dirA(A0)*rf*0.6, nh*0.75, ivec4(69, 0, 0, 0), 1));
      c = mix(c, IW, txt4(d - dirA(A1)*rf*0.6, nh*0.75, ivec4(70, 0, 0, 0), 1));
      c = mix(c, IW*0.8, txt4(d - vec2(0.0, -rf*0.42), nh*0.5, ivec4(70, 85, 69, 76), 1));
      float na = A0 + fuel*(A1 - A0);
      c = mix(c, IW, dNeedle(d, na, rf*0.86, rf*0.06, rf*0.15));
      c = mix(c, vec3(0.08), aFill(length(d) - rf*0.1));
      c = glassGlare(c, d, rf);
    }
    return c;
  }
  // ---- radio stack: three LCD rows (COM, NAV, transponder) with standby frequencies (pilot's side only)
  vec2 rs = q - vec2(0.33, 0.0);
  if (pilotSide && abs(rs.x) < 0.07 && abs(rs.y) < 0.09) {
    float row = floor((rs.y + 0.09)/0.06);
    vec2 cell = vec2(rs.x, mod(rs.y + 0.09, 0.06) - 0.03);
    vec3 col = vec3(0.035, 0.036, 0.04);
    float lcd = sdRBox(cell - vec2(-0.006, 0.0), vec2(0.056, 0.012), 0.0015);
    vec3 G2 = vec3(0.15, 1.0, 0.45);
    if (lcd < 0.0) {
      col = vec3(0.004, 0.02, 0.01);
      vec2 lq = cell - vec2(-0.058, 0.0);
      float h = 0.0055;
      if (row == 2.0) { col = mix(col, G2*0.6, txt4(lq - vec2(0.002, 0.0055), h*0.45, ivec4(67, 79, 77, 49), 0));
        col = mix(col, G2, numC(lq - vec2(0.05, -0.002), 118.3, h, 2, 2)); col = mix(col, G2*0.5, numC(lq - vec2(0.104, -0.002), 121.5, h*0.8, 2, 2)); }
      else if (row == 1.0) { col = mix(col, G2*0.6, txt4(lq - vec2(0.002, 0.0055), h*0.45, ivec4(78, 65, 86, 49), 0));
        col = mix(col, G2, numC(lq - vec2(0.05, -0.002), 110.9, h, 2, 2)); col = mix(col, G2*0.5, numC(lq - vec2(0.104, -0.002), 113.2, h*0.8, 2, 2)); }
      else { col = mix(col, G2*0.6, txt4(lq - vec2(0.002, 0.0055), h*0.45, ivec4(88, 80, 68, 82), 0));
        col = mix(col, G2, numC(lq - vec2(0.05, -0.002), 1200.0, h, 2, 0)); col = mix(col, G2*0.6, txt4(lq - vec2(0.06, -0.002), h*0.6, ivec4(65, 76, 84, 0), 0)); }
    }
    col = mix(col, vec3(0.2), aLine(lcd, lw));
    col = mix(col, vec3(0.25), aFill(length(cell - vec2(0.06, 0.0)) - 0.005));
    col = mix(col, vec3(0.08), aFill(length(cell - vec2(0.06, 0.0)) - 0.0035));
    return col;
  }
  return vec3(-1.0);
}
// ---------------------------------------------------------------- XR-9 / XR-11 multi-function display pages
// uv spans [-1, 1] across the glass; gAA is set by the caller to this pixel's footprint in uv units
vec3 mfdTitle(vec3 c, vec2 uv, ivec4 a, ivec4 b){
  const vec3 C = vec3(0.3, 0.8, 1.0);
  c = mix(c, C*0.9, txt(uv - vec2(-0.86, 0.86), 0.085, a, b, 0));
  c = mix(c, C*0.45, aFill(sdBox(uv - vec2(0.0, 0.76), vec2(0.88, 0.004))));
  c = mix(c, C*0.8, aFill(sdBox(uv - vec2(0.8, 0.86), vec2(0.06, 0.02))));
  return c;
}
vec3 mfdPage(int page, vec2 uv){
  const vec3 G = vec3(0.25, 1.0, 0.7), A = vec3(1.0, 0.62, 0.15), W = vec3(0.9, 0.95, 1.0), R = vec3(1.0, 0.25, 0.2), C = vec3(0.3, 0.8, 1.0);
  float lw = 0.012; vec3 c = vec3(0.0);
  float spool = uHud3.x, thr = uHud2.y, ab = smoothstep(0.85, 1.0, spool);
  if (page == 0) {          // ENGINES: twin N1 dials (270 deg), needles, digital readouts, reheat banner
    c = mfdTitle(c, uv, ivec4(69, 78, 71, 73), ivec4(78, 69, 83, 0));
    for (int e = 0; e < 2; e++) {
      vec2 o = uv - vec2(e == 0 ? -0.46 : 0.46, 0.05);
      float rr = 0.36, A0 = -2.356, AS = 4.712;
      c = mix(c, G*0.25, dArc(o, rr, A0, A0 + AS, lw*0.8));
      c = mix(c, spool > 0.85 ? A : G, dArc(o, rr, A0, A0 + AS*clamp(spool, 0.0, 1.0), lw*3.0));
      c = mix(c, R, dArc(o, rr, A0 + AS*0.97, A0 + AS, lw*3.0));
      c = mix(c, W*0.8, dTicks(o, rr, A0, A0 + AS, 10.0, 0.82, 0.92, lw*0.7));
      c = mix(c, W*0.6, dTicks(o, rr, A0, A0 + AS, 20.0, 0.86, 0.92, lw*0.5));
      c = mix(c, W*0.8, dNums(o, rr, A0, A0 + AS, 5.0, 0.0, 2.0, 100.0, 0.66, 0.065));
      c = mix(c, W, dNeedle(o, A0 + AS*clamp(spool, 0.0, 1.05), rr*0.95, 0.022, 0.05));
      c = mix(c, W*0.3, aFill(length(o) - 0.035));
      vec2 bq = o - vec2(0.0, -0.36);
      float wb = sdRBox(bq, vec2(0.2, 0.075), 0.02);
      if (wb < 0.0) c = vec3(0.0);
      c = mix(c, G*0.7, aLine(wb, lw*0.6));
      c = mix(c, G, numC(bq - vec2(0.13, 0.0), spool*100.0, 0.085, 2, 1));
      c = mix(c, W*0.7, txt4(o - vec2(0.0, 0.09), 0.05, e == 0 ? ivec4(78, 49, 32, 76) : ivec4(78, 49, 32, 82), 1));
    }
    float bn = sdRBox(uv - vec2(0.0, -0.78), vec2(0.36, 0.09), 0.03);
    if (ab > 0.01) {
      float fl = 0.6 + 0.4*sin(uTime*20.0);
      c = mix(c, A*0.25*ab, aFill(bn));
      c = mix(c, A*fl, aLine(bn, lw*0.8)*ab);
      c = mix(c, A*fl, txt(uv - vec2(0.0, -0.78), 0.08, ivec4(82, 69, 72, 69), ivec4(65, 84, 0, 0), 1)*ab);
    } else c = mix(c, G*0.4, txt(uv - vec2(0.0, -0.78), 0.06, ivec4(68, 82, 89, 0), ivec4(0), 1));
  } else if (page == 1) {   // POWER: thrust, energy cell, core temperature, nozzle bars with scales + Mach
    c = mfdTitle(c, uv, ivec4(80, 79, 87, 69), ivec4(82, 0, 0, 0));
    for (int b = 0; b < 4; b++) {
      float x0 = -0.66 + float(b)*0.44;
      float lv = b == 0 ? thr : b == 1 ? 1.0 : b == 2 ? 0.35 + 0.6*spool : abs(uHud2.z);
      vec2 o = uv - vec2(x0, -0.12);
      float frame = sdRBox(o, vec2(0.1, 0.5), 0.02);
      vec3 bc = b == 2 && lv > 0.85 ? A : (b == 1 ? C : G);
      c = mix(c, bc*0.08, aFill(frame));
      c = mix(c, bc*0.85, aFill(max(sdBox(o, vec2(0.075, 0.475)), o.y + 0.475 - lv*0.95)));
      c = mix(c, bc*0.6, aLine(frame, lw*0.6));
      float tk = fract((o.y + 0.475)/0.095 + 0.5) - 0.5;
      c = mix(c, W*0.5, aStroke(sdSeg(vec2(o.x, tk*0.095), vec2(0.11, 0.0), vec2(0.15, 0.0)), 2.0*(lw*0.3))*step(abs(o.y), 0.48));
      c = mix(c, W*0.85, numC(o - vec2(0.0, 0.58), lv*100.0, 0.06, 1, 0));
      ivec4 lb = b == 0 ? ivec4(84, 72, 82, 0) : b == 1 ? ivec4(67, 69, 76, 76) : b == 2 ? ivec4(67, 79, 82, 69) : ivec4(78, 79, 90, 0);
      c = mix(c, W*0.6, txt4(o - vec2(0.0, -0.6), 0.05, lb, 1));
    }
    c = mix(c, W*0.6, txt4(uv - vec2(0.42, 0.86), 0.06, ivec4(77, 65, 67, 72), 2));
    c = mix(c, W, numC(uv - vec2(0.72, 0.86), uHud.w, 0.075, 2, 2));
  } else if (page == 2) {   // MAP: heading-up moving terrain map, 6 km range
    float h = radians(uHud.z); vec2 f = vec2(sin(h), -cos(h)), rt = vec2(cos(h), sin(h));
    vec2 wp = uPlanePos.xz + (rt*uv.x + f*uv.y)*6000.0;
    float hh = baseAt(wp).x;
    vec3 land = mix(vec3(0.08, 0.28, 0.1), vec3(0.42, 0.33, 0.18), smoothstep(150.0, 900.0, hh));
    land = mix(land, vec3(0.8), smoothstep(1300.0, 1700.0, hh));
    c = hh < 0.0 ? vec3(0.02, 0.1, 0.26)*(1.0 + hh/600.0) : land*0.75;
    c = mix(c, G*0.7, aLine(length(uv) - 0.5, lw*0.6)*step(0.5, fract(atan(uv.y, uv.x)*12.0)));
    c = mix(c, G*0.7, aLine(length(uv) - 0.95, lw*0.6));
    c = mix(c, G*0.8, numC(uv - vec2(0.36, 0.38), 3.0, 0.05, 1, 0));
    c = mix(c, G*0.8, txt4(uv - vec2(0.43, 0.38), 0.04, ivec4(75, 77, 0, 0), 0));
    float tri = sdTri(uv, vec2(0.0, 0.1), vec2(-0.065, -0.07), vec2(0.065, -0.07));
    c = mix(c, vec3(0.0), aFill(tri - 0.015));
    c = mix(c, W, aFill(tri));
    c = mix(c, W*0.5, aStroke(sdSeg(uv, vec2(0.0, 0.1), vec2(0.0, 0.9)), 2.0*(lw*0.3))*step(0.5, fract(uv.y*8.0)));
    vec2 nn = -vec2(dot(vec2(0.0, -1.0), rt), dot(vec2(0.0, -1.0), f))*0.8;   // north marker
    float nc = length(uv - nn) - 0.075;
    c = mix(c, R*0.3, aFill(nc)); c = mix(c, R, aLine(nc, lw*0.6));
    c = mix(c, W, txt4(uv - nn, 0.07, ivec4(78, 0, 0, 0), 1));
    float wb = sdRBox(uv - vec2(0.0, 0.86), vec2(0.15, 0.08), 0.02);
    if (wb < 0.0) c = vec3(0.0);
    c = mix(c, W*0.7, aLine(wb, lw*0.6));
    c = mix(c, W, numC(uv - vec2(0.0, 0.86), mod(uHud.z, 360.0), 0.08, 1, 0));
  } else if (page == 3) {   // ADI: attitude with a numbered pitch ladder, bank scale and slip-free aircraft symbol
    vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0), rw = uPlaneRot*vec3(1.0, 0.0, 0.0), uw = uPlaneRot*vec3(0.0, 1.0, 0.0);
    float pitch = asin(clamp(fw.y, -1.0, 1.0)), bank = atan(-rw.y, uw.y);
    vec2 o = rot2(uv, bank);
    float ppr = 2.2, hz = o.y + pitch*ppr;
    c = hz > 0.0 ? mix(vec3(0.07, 0.3, 0.72), vec3(0.02, 0.12, 0.4), clamp(hz, 0.0, 1.0)) : mix(vec3(0.42, 0.24, 0.09), vec3(0.2, 0.1, 0.03), clamp(-hz, 0.0, 1.0));
    c = mix(c, W, aLine(hz, lw*1.4));
    for (int k = -6; k <= 6; k++) {
      if (k == 0) continue;
      float y = hz - float(k)*0.0873*ppr, hl = abs(k) % 2 == 0 ? 0.24 : 0.12;
      c = mix(c, W*0.9, aStroke(sdSeg(vec2(o.x, y), vec2(-hl, 0.0), vec2(hl, 0.0)), 2.0*(lw*0.45)));
      if (abs(k) % 2 == 0) c = mix(c, W*0.9, numC(vec2(o.x - sign(o.x)*(hl + 0.1), y), abs(float(k))*5.0, 0.06, 1, 0));
    }
    float rr = length(uv);
    for (int k = 0; k < 6; k++) {
      float ta = radians(k == 0 ? 0.0 : k == 1 ? 10.0 : k == 2 ? 20.0 : k == 3 ? 30.0 : k == 4 ? 45.0 : 60.0);
      for (int sgn = -1; sgn <= 1; sgn += 2) {
        vec2 u = dirA(ta*float(sgn));
        c = mix(c, W, aStroke(sdSeg(uv, u*0.8, u*(k == 0 || k == 3 || k == 5 ? 0.92 : 0.87)), 2.0*(lw*0.45)));
      }
    }
    c = mix(c, W, dArc(uv, 0.8, -1.05, 1.05, lw*0.7));
    c = mix(c, A, aFill(sdTri(rot2(uv, bank), vec2(0.0, 0.79), vec2(-0.05, 0.69), vec2(0.05, 0.69))));
    float sym = min(sdBox(uv - vec2(-0.3, 0.0), vec2(0.15, 0.022)), sdBox(uv - vec2(0.3, 0.0), vec2(0.15, 0.022)));
    sym = min(sym, min(sdBox(uv - vec2(-0.17, -0.05), vec2(0.022, 0.07)), sdBox(uv - vec2(0.17, -0.05), vec2(0.022, 0.07))));
    sym = min(sym, sdBox(uv, vec2(0.03)));
    c = mix(c, vec3(0.0), aFill(sym - 0.015));
    c = mix(c, A, aFill(sym));
    c = mix(c, vec3(0.0, 0.01, 0.02), 0.94*aFill(sdBox(uv - vec2(0.0, -0.9), vec2(1.0, 0.1))));
    c = mix(c, W*0.8, txt4(uv - vec2(-0.88, -0.86), 0.05, ivec4(66, 78, 75, 0), 0));
    c = mix(c, W, numC(uv - vec2(-0.6, -0.86), abs(degrees(bank)), 0.06, 0, 0));
    c = mix(c, W*0.8, txt4(uv - vec2(0.45, -0.86), 0.05, ivec4(80, 67, 72, 0), 0));
    c = mix(c, W, numC(uv - vec2(0.9, -0.86), degrees(pitch), 0.06, 2, 0));
  } else if (page == 4) {   // TVC: side view of the jet, the nozzles' pitch-vector angle on a +-30 deg scale, gear, reheat
    c = mfdTitle(c, uv, ivec4(84, 86, 67, 0), ivec4(0));
    vec2 o = uv - vec2(-0.2, 0.25);
    float body = sdRBox(o, vec2(0.48, 0.055), 0.05);
    body = min(body, sdTri(o, vec2(-0.48, 0.0), vec2(-0.68, -0.01), vec2(-0.48, 0.05)));
    body = min(body, sdTri(o, vec2(0.25, 0.05), vec2(0.42, 0.2), vec2(0.45, 0.05)));
    body = min(body, sdRBox(o - vec2(-0.2, 0.06), vec2(0.12, 0.035), 0.03));
    c = mix(c, C*0.12, aFill(body)); c = mix(c, C*0.85, aLine(body, lw*0.6));
    float a = uHud2.z*1.5708;   // down positive
    vec2 np = o - vec2(0.42, -0.02);
    vec2 nd = vec2(cos(a), -sin(a));
    float noz = sdSeg(np, vec2(0.0), nd*0.14) - 0.035;
    c = mix(c, A*0.3, aFill(noz)); c = mix(c, A, aLine(noz, lw*0.6));
    float along = dot(np, nd), side = abs(np.x*nd.y - np.y*nd.x);
    float ln = 0.18 + 0.5*thr;
    float arrow = max(side - 0.012, max(0.18 - along, along - ln));
    arrow = min(arrow, sdTri(np, nd*(ln + 0.09), nd*ln + vec2(nd.y, -nd.x)*0.045, nd*ln - vec2(nd.y, -nd.x)*0.045));
    c = mix(c, A*(0.6 + 0.4*thr), aFill(arrow));
    vec2 sq = uv - vec2(0.12, 0.23);
    c = mix(c, G*0.4, dArc(sq, 0.62, 1.5708 - 0.5236, 1.5708 + 0.5236, lw*0.6));
    for (int k = -1; k <= 1; k++) {
      float ta = 1.5708 + float(k)*0.5236;
      vec2 u = dirA(ta);
      c = mix(c, W*0.7, aStroke(sdSeg(sq, u*0.62, u*0.68), 2.0*(lw*0.4)));
      c = mix(c, W*0.7, numC(sq - u*0.75, abs(float(k))*30.0, 0.05, 1, 0));
    }
    vec2 u = dirA(1.5708 + clamp(a, -0.5236, 0.5236));
    c = mix(c, A, aFill(sdTri(sq, u*0.6, u*0.52 + vec2(u.y, -u.x)*0.035, u*0.52 - vec2(u.y, -u.x)*0.035)));
    c = mix(c, W*0.7, txt4(uv - vec2(-0.86, -0.42), 0.055, ivec4(78, 79, 90, 0), 0));
    c = mix(c, W, numC(uv - vec2(-0.28, -0.42), degrees(a), 0.07, 2, 0));
    for (int g = 0; g < 3; g++) {
      vec2 gq = uv - vec2(-0.72 + float(g)*0.3, -0.7);
      float gb = sdRBox(gq, vec2(0.13, 0.07), 0.02);
      vec3 gc = uHud2.w > 0.5 ? G : R*0.7;
      c = mix(c, gc*0.25, aFill(gb)); c = mix(c, gc, aLine(gb, lw*0.6));
      c = mix(c, gc, txt(gq, 0.04, g == 0 ? ivec4(78, 79, 83, 69) : g == 1 ? ivec4(76, 69, 70, 84) : ivec4(82, 73, 71, 72), g == 2 ? ivec4(84, 0, 0, 0) : ivec4(0), 1));
    }
    float hb = sdRBox(uv - vec2(0.58, -0.7), vec2(0.28, 0.08), 0.03);
    bool rh = ab > 0.5;
    float pl = rh ? 0.8 + 0.2*sin(uTime*6.0) : 1.0;
    c = mix(c, (rh ? A : G*0.3)*0.2, aFill(hb)); c = mix(c, (rh ? A : G*0.4)*pl, aLine(hb, lw*0.7));
    c = mix(c, (rh ? A : G*0.4)*pl, txt(uv - vec2(0.58, -0.7), 0.055, ivec4(82, 69, 72, 69), ivec4(65, 84, 0, 0), 1));
  } else if (page == 5) {   // G-METER and angle of attack
    c = mfdTitle(c, uv, ivec4(71, 32, 47, 32), ivec4(65, 79, 65, 0));
    float gv = uHud2.x;
    vec2 o = uv - vec2(0.1, -0.15);
    float A0 = -2.356, AS = 4.712, gf = clamp((gv + 3.0)/15.0, 0.0, 1.0);
    c = mix(c, G*0.3, dArc(o, 0.62, A0, A0 + AS, lw*0.8));
    c = mix(c, gv > 9.0 ? R : gv > 6.0 ? A : G, dArc(o, 0.62, A0 + AS*0.2, A0 + AS*gf, lw*3.0));
    c = mix(c, R, dArc(o, 0.62, A0 + AS*0.8, A0 + AS, lw*3.0)*0.6);
    c = mix(c, W*0.8, dTicks(o, 0.62, A0, A0 + AS, 5.0, 0.84, 0.94, lw*0.7));
    c = mix(c, W*0.5, dTicks(o, 0.62, A0, A0 + AS, 15.0, 0.88, 0.94, lw*0.5));
    { float a2 = wrapA(atan(o.x, o.y), A0, A0 + AS); float k = clamp(floor((a2 - A0)/AS*5.0 + 0.5), 0.0, 5.0);
      c = mix(c, W*0.8, numC(o - dirA(A0 + AS*k/5.0)*0.43, k*3.0 - 3.0, 0.06, 1, 0)); }
    c = mix(c, W, dNeedle(o, A0 + AS*gf, 0.6, 0.03, 0.08));
    c = mix(c, W*0.3, aFill(length(o) - 0.05));
    float wb = sdRBox(o - vec2(0.0, -0.45), vec2(0.2, 0.08), 0.02);
    if (wb < 0.0) c = vec3(0.0);
    c = mix(c, W*0.7, aLine(wb, lw*0.6));
    c = mix(c, W, numC(o - vec2(0.15, -0.45), gv, 0.08, 2, 1));
    vec2 aq = uv - vec2(-0.82, -0.1);
    float tape = sdRBox(aq, vec2(0.06, 0.58), 0.02);
    c = mix(c, C*0.08, aFill(tape)); c = mix(c, C*0.6, aLine(tape, lw*0.6));
    float tk = fract(aq.y/0.0967 + 0.5) - 0.5;
    c = mix(c, W*0.5, aStroke(sdSeg(vec2(aq.x, tk*0.0967), vec2(-0.06, 0.0), vec2(-0.02, 0.0)), 2.0*(lw*0.3))*step(abs(aq.y), 0.56));
    float ay = clamp(uHud3.y/30.0, -1.0, 1.0)*0.56;
    c = mix(c, A, aFill(sdTri(aq - vec2(0.0, ay), vec2(-0.05, 0.0), vec2(0.06, 0.04), vec2(0.06, -0.04))));
    c = mix(c, W*0.6, txt4(aq - vec2(0.0, -0.68), 0.045, ivec4(65, 79, 65, 0), 1));
  } else {                  // COMPASS rose with heading and altitude readouts
    c = mfdTitle(c, uv, ivec4(78, 65, 86, 0), ivec4(0));
    float h = radians(uHud.z), P = 3.14159265;
    vec2 cc = uv - vec2(0.0, -0.08);
    vec2 o = rot2(cc, -h);
    c = mix(c, G*0.8, aLine(length(cc) - 0.62, lw*0.7));
    c = mix(c, W*0.8, dTicks(o, 0.62, -P, P, 72.0, 0.92, 1.0, lw*0.4));
    c = mix(c, W, dTicks(o, 0.62, -P, P, 36.0, 0.87, 1.0, lw*0.6));
    float a = atan(o.x, o.y), k = floor(a/(P/6.0) + 0.5), la = k*P/6.0;
    vec2 lc = rot2(o - dirA(la)*0.45, la);
    int ki = int(mod(k + 12.0, 12.0));
    float lab = ki == 0 ? txt4(lc, 0.075, ivec4(78, 0, 0, 0), 1) : ki == 3 ? txt4(lc, 0.075, ivec4(69, 0, 0, 0), 1) : ki == 6 ? txt4(lc, 0.075, ivec4(83, 0, 0, 0), 1) : ki == 9 ? txt4(lc, 0.075, ivec4(87, 0, 0, 0), 1) : numC(lc, float(ki*3), 0.065, 1, 0);
    c = mix(c, ki == 0 ? R : W, lab);
    c = mix(c, A, aFill(sdTri(cc, vec2(0.0, 0.6), vec2(-0.045, 0.7), vec2(0.045, 0.7))));
    float tri = sdTri(cc, vec2(0.0, 0.09), vec2(-0.055, -0.06), vec2(0.055, -0.06));
    c = mix(c, A, aFill(tri));
    float wb = sdRBox(uv - vec2(-0.6, 0.62), vec2(0.27, 0.085), 0.02);
    if (wb < 0.0) c = vec3(0.0);
    c = mix(c, W*0.7, aLine(wb, lw*0.6));
    c = mix(c, W*0.6, txt4(uv - vec2(-0.84, 0.62), 0.045, ivec4(72, 68, 71, 0), 0));
    c = mix(c, W, numC(uv - vec2(-0.36, 0.62), mod(uHud.z, 360.0), 0.08, 2, 0));
    float ab2 = sdRBox(uv - vec2(0.58, 0.62), vec2(0.3, 0.085), 0.02);
    if (ab2 < 0.0) c = vec3(0.0);
    c = mix(c, G*0.7, aLine(ab2, lw*0.6));
    c = mix(c, G*0.6, txt4(uv - vec2(0.31, 0.62), 0.045, ivec4(65, 76, 84, 0), 0));
    c = mix(c, G, numC(uv - vec2(0.85, 0.62), uHud.y*3.28084, 0.075, 2, 0));
  }
  return c;
}
