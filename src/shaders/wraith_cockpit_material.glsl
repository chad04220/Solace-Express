//! kWraithCockpitMaterial
//! The XR-40 cockpit's materials, hologram, camera-feed display mapping and head-up symbology.
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
    float hk = hash2i(ivec2(floor(id*3.0 + 0.5)) + ivec2(5, 9));   // (rounded: half the cells' centres are whole numbers, and cut down to an
                                                                    // integer they flipped between two cells pixel by pixel - a glowing cell came out speckled)
    m.alb = vec3(0.022, 0.024, 0.03)*(0.85 + 0.3*hk)*(0.8 + 0.4*tx.r);
    m.rough = 0.62 + 0.16*hk; m.metal = 0.10; m.nrm = nT*0.6;
    float gv = mix(clamp((0.06 - e)/max(gPixG*13.0, 1e-4) + 0.5, 0.0, 1.0), 0.23, smoothstep(0.3, 0.8, gPixG*13.0));   // the grooves
    m.alb *= mix(1.0, 0.35, gv); m.rough = mix(m.rough, 0.7, gv);
    if (e > 0.06 && hk > 0.965) m.emit = mix(cyan, vio, step(0.985, hk))*0.025*(0.6 + 0.4*sin(uTime*1.3 + hk*20.0));
  } else if (mid == 65 || mid == 72) {   // anodised titanium frames: machined flutes, fasteners, an emitter line
    tx = triSample(lp*2.0, ln, M_METAL, 0.5, nT);
    m.alb = vec3(0.075, 0.078, 0.088)*(0.8 + 0.4*tx.r); m.metal = 0.65; m.rough = 0.48 + 0.12*tx.a; m.nrm = nT*0.5;
    m.alb *= 1.0 - 0.3*aaLines((q.x + q.y*0.7 + q.z*0.4)*60.0, 0.05, gPixG*77.0);   // (machined flutes)
    if (mid == 72) { m.alb *= 0.8; m.emit = vio*0.04*pulse*aaLines((q.x + q.z)*25.0 + 0.5, 0.0055, gPixG*35.0); }
  } else if (mid == 66) {   // seat: dark perforated hide in hexagonal quilting, glowing seams
    tx = triSample(lp, ln, M_LEATHER, 0.25, nT);
    vec2 id; float e = wrHex((abs(ln.x) > 0.6 ? q.zy : abs(ln.y) > 0.6 ? q.xz : q.xy)*22.0, id);
    m.alb = vec3(dot(tx.rgb, vec3(0.33)))*vec3(0.17, 0.17, 0.2); m.rough = 0.62; m.nrm = nT;
    float sv = mix(clamp((0.035 - e)/max(gPixG*22.0, 1e-4) + 0.5, 0.0, 1.0), 0.14, smoothstep(0.3, 0.8, gPixG*22.0));   // the seams
    m.alb *= mix(1.0, 0.5, sv); if (q.y > -0.55 && q.y < 0.1) m.emit = cyan*0.012*pulse*sv;
    vec2 pf = (abs(ln.x) > 0.6 ? q.zy : abs(ln.y) > 0.6 ? q.xz : q.xy)*90.0;
    m.alb *= 1.0 - 0.55*aaDisc(length(fract(pf) - 0.5), 0.18, gPixG*90.0, 0.1);   // perforations
  } else if (mid == 67) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = mix(cyan, vio, 0.5 + 0.5*sin(q.z*6.0 - uTime*1.5))*0.22*pulse; }
  else if (mid == 68) {   // touch glass on the consoles
    vec3 cq = vec3(abs(q.x) - 0.56, q.y + 0.47, q.z + 0.12);
    vec2 uv = vec2((cq.x - 0.015)/0.1, (cq.z - 0.265)/0.09)*0.5 + 0.5;
    m.alb = vec3(0.01); m.rough = 0.04; m.emit = wrUiPanel(uv, q.x > 0.0 ? 0.3 : 0.7)*1.5; gDispPx = true;
  } else if (mid == 69) {   // multi-function displays (dash and consoles)
    int page; vec2 uv; float panelScale;
    if (q.z < -0.7) {
      ResearchPanel panel = wraithPanel(wraithPanelIndex(q));
      vec3 l = researchPanelFrame(q, panel);
      page = panel.page; uv = l.xy/panel.h; panelScale = min(panel.h.x, panel.h.y);
    } else {
      vec3 cq = vec3(abs(q.x) - 0.56, q.y + 0.47, q.z + 0.12);
      vec3 mq = cq - vec3(0.015, 0.075, -0.24); mq.yz = rot2(mq.yz, -0.55);
      page = q.x < 0.0 ? 1 : 6; uv = vec2(mq.x/0.09*sign(q.x), -mq.z/0.055); panelScale = 0.055;
    }
    // 4x supersampled over the pixel's footprint on the glass, every element anti-aliased: crisp at any resolution
    float fp = gPixM/panelScale;
    gAA = fp*0.55;
    vec3 sc = pageTex(page, uv, fp)*vec3(0.85, 1.0, 1.15);
    float edge = smoothstep(1.0, 0.93, max(abs(uv.x), abs(uv.y)));
    sc = sc*edge + vec3(0.008, 0.02, 0.035)*edge;
    m.alb = vec3(0.01); m.rough = 0.05; m.emit = sc*(page == 3 ? 1.35 : 1.10); gDispPx = true;
  } else if (mid == 70) {   // grips, knobs and toggles: rubberised with machined caps
    tx = triSample(lp, ln, M_RUBBER, 0.08, nT); m.alb = tx.rgb*0.25; m.rough = tx.a; m.nrm = nT; m.metal = 0.1;
    if (ln.y > 0.7) { m.alb = vec3(0.3); m.metal = 0.9; m.rough = 0.25; }
  } else if (mid == 71) { tx = triSample(lp*3.0, ln, M_METAL, 0.3, nT); m.alb = tx.rgb*0.35; m.metal = 0.9; m.rough = 0.3; m.nrm = nT;
    m.alb *= 1.0 - 0.5*aaLines(q.y*40.0, 0.12, gPixG*40.0); }
  else if (mid == 73) { m.alb = vec3(0.05); m.emit = vio*0.12*pulse; }
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
  else { m.alb = vec3(0.02); m.rough = 0.6; m.emit = cyan*0.05*pulse*step(0.5, fract(q.y*14.0 + 0.25)); }   // vents
}

// ---------------------------------------------------------------- hologram: a rotating wireframe globe over the dash
vec3 wrHolo(vec3 ro, vec3 rd, float tmax){
  vec3 c = uPlanePos + uPlaneRot*(gM[22].xyz + WR_HOLO_CENTRE);
  float R = WR_HOLO_RADIUS;
  vec3 oc = ro - c; float b = dot(oc, rd), h = b*b - dot(oc, oc) + R*R;
  vec3 col = vec3(0.0);
  // projection cone from the emitter
  vec3 e = uPlanePos + uPlaneRot*(gM[22].xyz + WR_HOLO_EMITTER);
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
  return col*0.55*(0.85 + 0.15*sin(uTime*30.0));
}

// ---------------------------------------------------------------- belly camera on the front floor pane
// While a bomb falls or goes off, the footwell floor shows the bomb camera (Game::updateBombCam).
// floor pane coordinates [-1, 1] and the pane's aspect (the chin-side footwell pane or a pane beside the seat)
vec3 wrFloorUV(vec3 q){
  if (q.z > 0.1) return vec3(wrFrame(vec3(abs(q.x), q.y, q.z), WB_C, WB_N, vec3(0,0,-1)).xy/WB_S*vec2(sign(q.x), 1.0), WB_S.x/WB_S.y);
  return vec3(wrFrame(q, WL_C, WL_N, vec3(0,0,-1)).xy/WL_S, WL_S.x/WL_S.y);
}
// ---------------------------------------------------------------- camera feeds on the displays
// Which camera feeds a point q (cockpit frame) on display id, and where on its picture ([-1, 1], y up). The slots and
// the pane geometry are feed_cameras.h's (feedRig): a left pane's camera is the mirror of the right one, its picture
// not mirrored.
int feedSlot(int id, vec3 q, out vec2 uv){
  float sx = q.x < 0.0 ? -1.0 : 1.0; bool L = q.x < 0.0;
  if (int(gM[0].z + 0.5) == 5) {   // XR-30: the panoramic display (one panoramic camera) and the side bays
    if (id == 41) { uv = vec2(0.0); return 0; }   // the panoramic display: one panoramic camera (feedScreen maps it)
    uv = vec2((q.z - 0.24)/0.3*sx, (q.y - 0.04)/0.2);
    return L ? 3 : 4;
  }
  vec3 aq = vec3(abs(q.x), q.y, q.z);
  if (id == 41) {   // the front display: one wide camera, the picture laid on it as seen from the eye
    uv = vec2(q.x/uFeedR[0].w, q.y/uFeedU[0].w)/max(-q.z, 0.1);
    return 0;
  }
  if (id == 42 || id == 43) {
    vec3 f = wrFrame(aq, WS_C, WS_N, vec3(0,1,0)), a = wrFrame(aq, WA_C, WS_N, vec3(0,1,0));
    if (wrShape(f.xy, WS_S, 0.13) <= wrShape(a.xy, WA_S, 0.11)) { uv = f.xy/WS_S*vec2(sx, 1.0); return L ? 3 : 4; }
    uv = a.xy/WA_S*vec2(sx, 1.0); return L ? 5 : 6;
  }
  if (id == 62) { uv = wrFrame(q, WO_C, WO_N, vec3(0,0,-1)).xy/WO_S; return 7; }
  if (id == 63) { uv = wrFrame(q, WC_C, WC_N, vec3(0,1,0)).xy/WC_S; return 8; }
  if (q.z > 0.1) { uv = wrFrame(aq, WB_C, WB_N, vec3(0,0,-1)).xy/WB_S*vec2(sx, 1.0); return L ? 10 : 11; }
  uv = wrFrame(q, WL_C, WL_N, vec3(0,0,-1)).xy/WL_S; return 9;
}
// The picture on a point of a display, and the camera's ray through it (rdc: the symbology is drawn conformal to it).
// While the XR-40's bomb camera exists the floor panes show it instead of the belly cameras (bomb = true).
vec3 feedScreen(int id, vec3 sl, out vec3 rdc, out bool bomb){
  vec3 q = sl - gM[22].xyz;
  vec2 uv; int s = feedSlot(id, q, uv);
  bomb = int(gM[0].z + 0.5) == 6 && id == 61 && uFeed.w > 0.5 && uFeedB[12].w > 0.5;
  if (bomb) { vec3 fu = wrFloorUV(q); uv = fu.xy*vec2(fu.z/(WL_S.x/WL_S.y), 1.0); s = 12; }
  else {   // a window: the point of the picture the eye sees through this point of the display (feed_cameras.h)
    vec3 d = uPlaneRot*q; float x = dot(d, uFeedR[s].xyz), y = dot(d, uFeedU[s].xyz), z = dot(d, -uFeedB[s].xyz);
    if (uFeedR[s].w < 0.0) uv = vec2(atan(x, z)/(-uFeedR[s].w), y/max(length(vec2(x, z)), 1e-3)/uFeedU[s].w);   // a panorama
    else uv = vec2(x/uFeedR[s].w, y/uFeedU[s].w)/max(z, 1e-3);
  }
  if (uFeedR[s].w < 0.0) { float a = uv.x*(-uFeedR[s].w);
    rdc = normalize(-uFeedB[s].xyz*cos(a) + uFeedR[s].xyz*sin(a) + uFeedU[s].xyz*(uv.y*uFeedU[s].w)); }
  else rdc = normalize(-uFeedB[s].xyz + uFeedR[s].xyz*(uv.x*uFeedR[s].w) + uFeedU[s].xyz*(uv.y*uFeedU[s].w));
  if (uFeedOn == 0 || uFeedB[s].w < 0.5) return vec3(0.002, 0.004, 0.007);   // no picture yet: a dark panel
  if (abs(uv.x) > 1.0 || abs(uv.y) > 1.0) return vec3(0.0);
  vec4 T = uFeedTile[s];
  vec2 hp = 0.5/vec2(textureSize(uFeedTex, 0));
  vec2 auv = T.xy + clamp((uv*0.5 + 0.5)*T.zw, hp, T.zw - hp);
  return textureLod(uFeedTex, auv, 0.0).rgb;
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
    // bomb impact prediction: a diamond on the ground and its range
    if (uPip.w > 0.5) {
      vec3 dd = normalize(uPip.xyz - uCamPos);
      vec3 u = normalize(cross(dd, vec3(0.0, 1.0, 0.0) + vec3(1e-4, 0.0, 0.0))), v = cross(u, dd);
      float dz = dot(rd, dd);
      if (dz > 0.5) {
        vec2 s = vec2(dot(rd, u), dot(rd, v))/dz;
        float rng = length(uPip.xyz - uCamPos);
        float dia = abs(abs(s.x) + abs(s.y) - 0.022);
        float on = uWr[4].y > 0.9 ? 1.0 : 0.55;
        warn = max(warn, hudLine(dia, px*1.3)*on);
        warn = max(warn, step(length(s), 0.003)*on);
        hud = max(hud, hudNum(s - vec2(0.03, -0.012), rng, 4, vec2(0.012, 0.02)));
      }
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
