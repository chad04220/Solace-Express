//! kEntFS2

layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1; layout(location=2) out vec4 oG2; layout(location=3) out vec4 oG3;
// triplanar sample in object space: linear albedo, roughness; bumped object-space normal
vec3 triS(vec3 p, vec3 n, int layer, float sc, float bump, inout vec3 nb, out float rough){
  vec3 w = pow(abs(n), vec3(4.0)); w /= dot(w, vec3(1.0));
  vec4 ax = texture(uAlb, vec3(p.zy/sc, float(layer))), ay = texture(uAlb, vec3(p.xz/sc, float(layer))), az = texture(uAlb, vec3(p.xy/sc, float(layer)));
  vec4 nx = texture(uNrm, vec3(p.zy/sc, float(layer))), ny = texture(uNrm, vec3(p.xz/sc, float(layer))), nz = texture(uNrm, vec3(p.xy/sc, float(layer)));
  vec2 tx = nx.xy*2.0 - 1.0, ty = ny.xy*2.0 - 1.0, tz = nz.xy*2.0 - 1.0;
  nb = normalize(nb + (w.x*vec3(0.0, tx.y, tx.x)*sign(n.x) + w.y*vec3(ty.x, 0.0, ty.y)*sign(n.y) + w.z*vec3(tz.x, tz.y, 0.0)*sign(n.z))*bump);
  vec4 a = ax*w.x + ay*w.y + az*w.z;
  rough = a.a;
  return a.rgb*a.rgb*mix(0.75, 1.0, nx.w*w.x + ny.w*w.y + nz.w*w.z);
}
vec2 octEnc(vec3 n){ n /= abs(n.x) + abs(n.y) + abs(n.z); vec2 e = n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); return e; }
vec3 pal(float s, vec3 a, vec3 b, vec3 c, vec3 d){ float k = fract(s)*4.0; return k < 1.0 ? a : k < 2.0 ? b : k < 3.0 ? c : d; }
// procedural windows on a facade: returns 1 inside a pane, frame in .y; cell id in .zw
vec4 windowGrid(vec2 q, vec2 cell, vec2 pane, float y0){
  vec2 g = vec2(q.x/cell.x, (q.y - y0)/cell.y);
  vec2 f = fract(g) - 0.5, id = floor(g);
  vec2 hs = pane/cell*0.5;
  float inside = step(abs(f.x), hs.x)*step(abs(f.y), hs.y)*step(0.0, g.y);
  float fr = step(abs(f.x), hs.x + 0.06)*step(abs(f.y), hs.y + 0.06)*step(0.0, g.y)*(1.0 - inside);
  return vec4(inside, fr, id);
}
void main(){
  vec3 V = normalize(uCam - vW);
  float yaw = vInst.y, cy = cos(yaw), sy = sin(yaw);
  vec3 n0 = normalize(vLN);
  vec3 wn0 = vec3(cy*n0.x + sy*n0.z, n0.y, -sy*n0.x + cy*n0.z);
  // two-sided: shade the side facing the camera (leaf cards keep their crown-wide normal so the crown stays round)
  if (dot(wn0, V) < 0.0 && int(vAux.x + 0.5) != P_LEAFCARD) { n0 = -n0; wn0 = -wn0; }
  float dist = length(uCam - vW);
  // Retain the near silhouette, then smoothly retire subpixel edge perforation instead of
  // switching every crown in the 700 m band on one frame.
  float edgeDetail = 1.0 - smoothstep(500.0, 700.0, dist);
  if (leafCut(edgeDetail > 0.0 ? pow(1.0 - abs(dot(wn0, V)), 1.5)*edgeDetail : 0.0)) discard;
  int part = int(vAux.x + 0.5);
  float seed = vInst.x, ao = vAux.y;
  vec3 alb = vec3(0.5); float rough = 0.8, metal = 0.0, cls = 2.0; vec3 emit = vec3(0.0);
  vec3 nb = n0; float r0;
  vec3 lp = vL;
  float wy = vW.y;
  if (part == P_LEAFCARD) {
    cls = 1.0;
    float h = hsh(floor(vec2(fract(vAux.z), vAux.w)*5.0) + floor(vAux.z)*7.0 + vInst.x*13.0);
    vec3 tint = uKind == K_OAK ? vec3(0.07, 0.12, 0.035) : uKind == K_BIRCH ? vec3(0.11, 0.17, 0.045) : uKind == K_PINE ? vec3(0.045, 0.085, 0.06)
              : uKind == K_SPRUCE ? vec3(0.038, 0.072, 0.066) : uKind == K_FIR ? vec3(0.042, 0.08, 0.058) : vec3(0.075, 0.12, 0.04);
    alb = tint*mix(0.75, 1.3, h)*mix(0.82, 1.12, fract(seed*5.3));
    if (uKind >= K_OAK) alb = mix(alb, alb*vec3(1.6, 1.05, 0.55), smoothstep(0.8, 1.0, fract(seed*13.7))*0.8);   // broadleaf trees turning (never the conifers)
    alb *= mix(0.6, 1.0, ao);
    rough = 0.6;
    nb = n0;
  } else if (part == P_LEAF || part == P_NEEDLE || part == P_FROND) {
    cls = 1.0;
    if (part == P_NEEDLE) {
      alb = triS(lp, n0, M_NEEDLES, 0.9, 1.4, nb, rough);
      // a lumpy noise normal and shade, finer than the leaves': a needle mass reads as many small tufts and shoots,
      // not one smooth cushion
      // (within a few hundred metres: further off it is finer than a pixel, and the forest's far pixels are many)
      float tuft = 1.0 - smoothstep(250.0, 400.0, dist);
      if (tuft > 0.0) {
        vec3 q = lp*4.6 + vInst.x*11.0;
        vec3 g = vec3(vn3(q + vec3(0.6, 0.0, 0.0)) - vn3(q - vec3(0.6, 0.0, 0.0)), vn3(q + vec3(0.0, 0.6, 0.0)) - vn3(q - vec3(0.0, 0.6, 0.0)), vn3(q + vec3(0.0, 0.0, 0.6)) - vn3(q - vec3(0.0, 0.0, 0.6)));
        nb = normalize(nb + g*1.2*tuft);
        alb *= mix(1.0, mix(0.68, 1.12, vn3(lp*6.1 - vInst.x*3.0)), tuft);
      }
      vec3 tint = (uKind == K_SPRUCE ? vec3(0.5, 0.7, 0.68) : uKind == K_PINE ? vec3(0.5, 0.68, 0.56) : vec3(0.55, 0.76, 0.62))*mix(0.85, 1.1, vAux.z);
      alb *= tint*mix(0.85, 1.15, fract(seed*7.31));
      if (uSnow > 0.05 || wy > 1500.0) alb = mix(alb, vec3(0.85, 0.88, 0.92), smoothstep(0.35, 0.8, n0.y)*max(uSnow, smoothstep(1500.0, 1900.0, wy))*0.85);
    } else if (part == P_FROND) {
      alb = triS(lp, n0, M_LEAVES, 1.2, 0.5, nb, rough)*vec3(0.85, 1.05, 0.55);
      alb = mix(alb, vec3(0.35, 0.3, 0.12), smoothstep(0.75, 1.0, vAux.w)*0.6 + step(abs(vAux.z), 0.07)*0.5);
    } else {
      // leaf masses: fine leaf texture, plus a lumpy noise normal so a clump reads as many small sprays
      alb = triS(lp, n0, M_LEAVES, 0.75, 1.6, nb, rough);
      // Match the needles' detail budget: unchanged at <=250 m, smoothly absent beyond 400 m.
      // These seven noise evaluations were previously paid by every distant broadleaf fragment.
      float leafDetail = 1.0 - smoothstep(250.0, 400.0, dist);
      float sprayShade = 0.925;   // mean of the existing [0.7, 1.15] modulation
      if (leafDetail > 0.0) {
        vec3 q = lp*3.1 + vInst.x*11.0;
        vec3 g = vec3(vn3(q + vec3(0.7, 0.0, 0.0)) - vn3(q - vec3(0.7, 0.0, 0.0)), vn3(q + vec3(0.0, 0.7, 0.0)) - vn3(q - vec3(0.0, 0.7, 0.0)), vn3(q + vec3(0.0, 0.0, 0.7)) - vn3(q - vec3(0.0, 0.0, 0.7)));
        nb = normalize(nb + g*(1.4*leafDetail));
        sprayShade = mix(sprayShade, mix(0.7, 1.15, vn3(lp*5.3 - vInst.x*3.0)), leafDetail);
      }
      vec3 tint = uKind == K_OAK ? vec3(0.5, 0.68, 0.34) : uKind == K_BIRCH ? vec3(0.7, 0.86, 0.38) : vec3(0.48, 0.62, 0.32);
      float hue = fract(seed*13.7);
      tint = mix(tint, tint*vec3(1.2, 0.92, 0.6), smoothstep(0.8, 1.0, hue));   // a few trees turning
      tint *= mix(0.78, 1.12, fract(seed*5.3))*mix(0.85, 1.12, vAux.z);       // tree and clump variation
      alb *= tint*sprayShade;
      if (uSnow > 0.05) alb = mix(alb, vec3(0.8), smoothstep(0.5, 0.9, n0.y)*uSnow*0.6);
    }
    alb *= ao;
    rough = 0.72;
  } else if (part == P_BARK) {
    if (uKind == K_BIRCH) {
      alb = vec3(0.78, 0.76, 0.72)*(0.85 + 0.15*vn3(lp*vec3(8.0, 1.0, 8.0)));
      float mark = smoothstep(0.72, 0.8, vn3(vec3(lp.x*6.0, lp.y*9.0, lp.z*6.0)));
      alb = mix(alb, vec3(0.06), mark);
      rough = 0.7;
    } else if (uKind == K_PALM) {
      alb = triS(lp, n0, M_BARK, 1.0, 0.8, nb, rough)*vec3(0.95, 0.85, 0.7);
      alb *= 0.75 + 0.25*smoothstep(0.2, 0.5, fract(lp.y*2.6));
    } else {
      alb = triS(lp*vec3(1.0, 0.5, 1.0), n0, M_BARK, 0.8, 1.2, nb, rough)*(uKind == K_PINE ? vec3(1.05, 0.8, 0.65) : vec3(0.8, 0.75, 0.7));
    }
    alb *= ao;
  } else if (part == P_ROCK) {
    float sc = uKind == K_SEASTACK || uKind == K_SPIRE ? 7.0 : uKind == K_OUTCROP ? 4.5 : 2.2;
    alb = triS(lp, n0, M_ROCK, sc, 1.3, nb, rough);
    vec3 tint = uKind == K_SPIRE ? vec3(0.98, 0.72, 0.55) : uKind == K_SEASTACK ? mix(vec3(0.72, 0.66, 0.58), vec3(0.6, 0.6, 0.62), fract(seed*3.1)) : mix(vec3(0.78, 0.76, 0.74), vec3(0.88, 0.84, 0.78), fract(seed*3.1));
    if (uKind == K_SPIRE || uKind == K_SEASTACK) tint *= 0.82 + 0.22*sin(lp.y*(uKind == K_SPIRE ? 2.4 : 1.3) + vn3(lp*0.4)*2.5);   // strata
    alb *= tint*ao;
    float top = smoothstep(0.55, 0.85, nb.y);
    if (wy < 1100.0 && uKind != K_SEASTACK) alb = mix(alb, vec3(0.16, 0.22, 0.08)*(0.8 + 0.4*vn3(lp*3.0)), top*0.55*smoothstep(0.35, 0.7, vn3(lp*1.3 + seed*9.0)));   // moss
    if (uKind == K_SEASTACK) { alb = mix(alb, vec3(0.92, 0.9, 0.85), top*smoothstep(20.0, 26.0, lp.y)*0.7); alb *= mix(0.55, 1.0, smoothstep(0.0, 2.5, wy)); }   // guano, wet base
    float sn = max(uSnow, smoothstep(1400.0, 1800.0, wy));
    if (sn > 0.0) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.45, 0.8, nb.y)*sn);
  } else if (uKind >= K_HANGAR) {
    // ---------------------------------------------------------------- airport buildings, aircraft, vehicles, furniture
    vec3 mp = lp/vScale;   // mesh coordinates (the instance scale removed)
    bool sideX = abs(n0.x) > 0.5;
    float u = sideX ? lp.z : lp.x;
    float s2 = fract(seed*13.31), s3 = fract(seed*3.77);
    vec3 livery = s3 < 0.5 ? pal(s3*2.0, vec3(0.7, 0.07, 0.06), vec3(0.06, 0.2, 0.55), vec3(0.05, 0.4, 0.2), vec3(0.9, 0.5, 0.05))
                           : pal(s3*2.0 - 1.0, vec3(0.04, 0.08, 0.25), vec3(0.0, 0.45, 0.5), vec3(0.45, 0.06, 0.25), vec3(0.15, 0.15, 0.17));
    if (part == P_WALL) {
      if (uKind == K_TERMINAL || uKind == K_CTRL || uKind == K_MAST) alb = triS(lp, n0, M_CONCRETE, 4.0, 0.5, nb, rough)*vec3(0.9, 0.89, 0.86);
      else if (uKind == K_FBO) alb = triS(lp, n0, M_PLASTER, 2.5, 0.6, nb, rough)*pal(s2, vec3(0.95, 0.93, 0.88), vec3(0.85, 0.88, 0.92), vec3(0.93, 0.86, 0.74), vec3(0.8, 0.82, 0.8));
      else if (uKind == K_JETBRIDGE) {
        alb = triS(vec3(lp.x, lp.z, lp.y), n0, M_CORRUGATED, 2.0, 0.3, nb, rough)*vec3(0.78, 0.8, 0.82); metal = 0.4;
        if (sideX && mp.y > 4.3 && mp.y < 5.3 && mp.z > -7.5 && mp.z < 8.0) { alb = vec3(0.05, 0.07, 0.09); rough = 0.1; cls = 3.0; nb = n0; emit = vec3(1.0, 0.95, 0.85)*uNight*0.8; }
      } else {   // hangars and sheds: profiled steel cladding with vertical ribs
        vec3 tint = pal(s2, vec3(0.82, 0.84, 0.86), vec3(0.6, 0.67, 0.74), vec3(0.84, 0.8, 0.68), vec3(0.6, 0.66, 0.6));
        if (uKind == K_ARCH) tint = vec3(0.74, 0.74, 0.7);
        alb = triS(vec3(lp.x, lp.z, lp.y), n0, M_CORRUGATED, 3.0, 0.7, nb, rough)*tint; metal = 0.3;
        if (uKind == K_HANGAR && sideX && mp.y > 7.0 && mp.y < 8.3 && fract(mp.z/4.0) < 0.7 && abs(mp.z) < 14.5) {   // clerestory
          alb = vec3(0.06, 0.08, 0.1); rough = 0.1; cls = 3.0; nb = n0; metal = 0.0; emit = vec3(1.0, 0.92, 0.75)*uNight*0.7; }
        alb *= mix(0.8, 1.0, smoothstep(0.0, 1.2, mp.y));   // splash dirt along the base
      }
      alb *= 1.0 - 0.3*uWet;
    } else if (part == P_ROOF) {
      vec3 rp = abs(n0.x) > abs(n0.z) ? vec3(lp.z, lp.y, lp.x) : lp;
      alb = triS(rp, n0, M_CORRUGATED, 2.0, 0.8, nb, rough)*(uKind == K_THANGAR ? vec3(0.58, 0.62, 0.64) : uKind == K_ARCH ? vec3(0.7, 0.71, 0.7) : vec3(0.74, 0.76, 0.77));
      metal = 0.5; rough = max(rough, 0.35);
      alb *= 1.0 - 0.25*smoothstep(0.6, 0.9, vn3(lp*0.15 + seed))*0.6;   // weathering streaks
      if (uSnow > 0.05) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.3, 0.6, n0.y)*uSnow);
    } else if (part == P_DOOR) {
      alb = triS(vec3(lp.x, lp.z, lp.y), n0, M_CORRUGATED, 1.2, 0.6, nb, rough);
      float bay = uKind == K_THANGAR ? floor((mp.x + 24.0)/12.0) : 0.0;
      alb *= pal(fract(seed*5.1 + bay*0.37), vec3(0.88, 0.88, 0.86), vec3(0.22, 0.36, 0.58), vec3(0.52, 0.55, 0.58), vec3(0.72, 0.7, 0.62));
      if (uKind == K_HANGAR && abs(fract((mp.x + 19.6)/6.53) - 0.5) > 0.49) alb *= 0.45;   // sliding panel joints
      metal = 0.35;
    } else if (part == P_GLASS) {
      cls = 3.0; rough = 0.06; metal = 0.1; nb = n0;
      alb = vec3(0.05, 0.08, 0.1);
      if (uKind == K_TERMINAL) {
        float mul = abs(fract(u/1.6) - 0.5), tr = abs(fract(mp.y/3.85) - 0.5);
        if (mul > 0.47 || tr > 0.48) { alb = vec3(0.32, 0.34, 0.36); rough = 0.35; metal = 0.6; cls = 2.0; }
        else { alb = vec3(0.06, 0.1, 0.12); emit = vec3(1.0, 0.9, 0.74)*uNight*(1.1 + 0.4*hsh(floor(vec2(u/1.6, mp.y/3.85))))*1.2; }
      } else if (uKind == K_CTRL) { alb = vec3(0.04, 0.09, 0.08); emit = vec3(0.5, 0.9, 0.7)*uNight*0.25; }
      else if (uKind == K_FBO || uKind == K_ARCH) emit = vec3(1.0, 0.88, 0.68)*uNight*0.6;
    } else if (part == P_METAL) {
      metal = 0.7; rough = 0.4;
      if (uKind == K_FUELTANK || uKind == K_PUMP) { alb = vec3(0.86, 0.86, 0.84); metal = 0.1; rough = 0.45;
        if (uKind == K_PUMP && abs(mp.y - 1.65) < 0.12) alb = vec3(0.75, 0.08, 0.06); }
      else if (uKind == K_TRUCK) { alb = vec3(0.78); metal = 0.9; rough = 0.25; }
      else if (uKind == K_AIRLINER) { alb = vec3(0.72, 0.73, 0.75); metal = 0.35; rough = 0.35; }
      else alb = triS(lp, n0, M_METAL, 2.0, 0.3, nb, rough)*0.75;
    } else if (part == P_PAINT) {
      metal = 0.0; rough = 0.3;
      if (uKind == K_CAR) alb = s2 < 0.5 ? pal(s2*2.0, vec3(0.85), vec3(0.6, 0.62, 0.64), vec3(0.04), vec3(0.55, 0.06, 0.05)) : pal(s2*2.0 - 1.0, vec3(0.08, 0.16, 0.38), vec3(0.75, 0.75, 0.78), vec3(0.1, 0.22, 0.14), vec3(0.3, 0.32, 0.35));
      else if (uKind == K_TRUCK) alb = pal(s2, vec3(0.88), vec3(0.75, 0.1, 0.07), vec3(0.85, 0.65, 0.05), vec3(0.88));
      else if (uKind == K_GAPLANE) {
        alb = s2 < 0.75 ? vec3(0.88, 0.88, 0.86) : vec3(0.9, 0.85, 0.65);
        if (abs(n0.y) < 0.6 && mp.y > 1.22 && mp.y < 1.42 && mp.z < 3.0) alb = livery;   // fuselage stripe
        if (mp.y > 1.97 && abs(mp.x) > 4.6) alb = livery;                               // wing tips
      } else {   // airliner: white top, grey belly, cheat line, cabin windows, cockpit
        alb = vec3(0.9, 0.9, 0.9);
        if (mp.y < 2.0 && abs(mp.x) < 2.0) alb = vec3(0.6, 0.62, 0.66);
        if (mp.y > 2.75 && mp.y < 2.95 && abs(mp.x) < 2.1) alb = livery;
        if (abs(mp.x) > 4.0 && mp.y < 2.7) alb = livery*0.9 + 0.05;   // nacelles in the airline colour
        bool win = mp.y > 3.3 && mp.y < 3.68 && mp.z > -10.5 && mp.z < 12.5 && abs(fract(mp.z/0.53) - 0.5) < 0.22 && abs(mp.x) > 1.4;
        bool ck = mp.z > 16.2 && mp.z < 17.7 && mp.y > 3.15 && mp.y < 3.65 && abs(mp.x) > 0.25;
        if (win || ck) { alb = vec3(0.03, 0.04, 0.05); rough = 0.08; cls = 3.0; nb = n0; emit = win ? vec3(1.0, 0.9, 0.7)*uNight*0.5 : vec3(0.0); }
        if (abs(abs(mp.z - 11.6) - 0.5) < 0.04 && mp.y > 2.2 && mp.y < 4.3 && abs(mp.x) > 1.5) alb *= 0.5;   // forward door outline
      }
    } else if (part == P_STRIPE) { alb = livery; rough = 0.3; }
    else if (part == P_SOCK) { float b = floor(clamp((mp.x - 0.15)/0.72, 0.0, 4.99)); alb = mod(b, 2.0) < 0.5 ? vec3(0.95, 0.3, 0.03) : vec3(0.92); rough = 0.85; }
    else if (part == P_BEACON) {   // aerodrome beacon: alternating white and green beams sweeping round
      float a = atan(lp.z, lp.x) - uTime*1.6;
      float w = pow(max(cos(a), 0.0), 12.0), g = pow(max(-cos(a), 0.0), 12.0);
      alb = vec3(0.15); rough = 0.05; cls = 3.0; nb = n0;
      emit = (vec3(1.0, 0.97, 0.9)*w + vec3(0.1, 1.0, 0.35)*g)*(0.6 + 14.0*uNight);
    }
    else if (part == P_FENCE) { alb = vec3(0.55, 0.57, 0.58); metal = 0.7; rough = 0.45; }
    else if (part == P_OBST) {   // obstruction marking: red and white bands, a red light on top at night
      float hb = uKind == K_WINDSOCK ? 1.25 : 3.0;
      alb = mod(floor(mp.y/hb), 2.0) < 0.5 ? vec3(0.95) : vec3(0.75, 0.1, 0.06); rough = 0.5;
      if (uKind != K_WINDSOCK && mp.y > (uKind == K_CTRL ? 33.5 : uKind == K_MAST ? 11.5 : 1e9)) emit = vec3(1.0, 0.08, 0.04)*uNight*(4.0 + 3.0*step(0.5, fract(uTime*0.8)));
    }
    else if (part == P_LAMP) { alb = vec3(0.9); rough = 0.1; cls = 3.0; nb = n0; emit = vec3(1.0, 0.93, 0.8)*(0.15 + 10.0*uNight); }
    else if (part == P_SIGN) { alb = uKind == K_PUMP ? vec3(0.75, 0.1, 0.06) : vec3(0.08, 0.22, 0.55); rough = 0.4; emit = alb*uNight*2.5; }
    else if (part == P_CANOPY) { alb = vec3(0.9); if (n0.y < -0.5) emit = vec3(1.0, 0.97, 0.9)*uNight*2.5; rough = 0.4; }
    else if(part>=27 && part<33) {
      // Finish follows the wheel's rest coordinates; its mesh/normal rotate together in both passes.
      vec2 q=mp.yz-vAux.zw;float r=max(vAux.y,.01),rad=length(q)/r;
      float aa=max(fwidth(rad),.002);
      alb=vec3(.025);rough=.85;
      if(abs(n0.x)>.85 && rad<.62) {
        alb=vec3(.48,.50,.53);metal=.8;rough=.3;
        float a=atan(q.y,q.x),sector=1.04719755;
        float spoke=.5+.5*cos(a*6.0);alb*=mix(.32,1.0,smoothstep(.35,.65,spoke));
        float hub=1.0-smoothstep(.18,.18+aa,rad);alb=mix(alb,vec3(.6),hub);
      }
    }
    else if (part == P_DARK) { alb = uKind == K_GAPLANE || uKind == K_AIRLINER || uKind == K_CAR || uKind == K_TRUCK ? vec3(0.03) : triS(lp, n0, M_GRAVEL, 2.0, 0.5, nb, rough)*0.4; rough = 0.8; }
    else { alb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nb, rough)*vec3(0.88, 0.86, 0.82); }   // P_TRIM: concrete
    if (uSnow > 0.05 && part != P_GLASS && part != P_ROOF) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  } else {
    // ---------------------------------------------------------------- buildings
    vec3 sc3 = vec3(1.0);
    bool sideX = abs(n0.x) > 0.5;
    float u = sideX ? lp.z : lp.x, v = lp.y;
    float s1 = fract(seed*7.13), s2 = fract(seed*13.31), s3 = fract(seed*3.77);
    if (part == P_WALL) {
      int layer = M_PLASTER; float tsc = 2.5; vec3 tint = vec3(1.0);
      if (uKind == K_HOUSE || uKind == K_HIP || uKind == K_LHOUSE || uKind == K_FARM) {
        if (s1 < 0.3) { layer = M_BRICK; tint = mix(vec3(1.0), vec3(0.85, 0.75, 0.7), s2); }
        else if (s1 < 0.55 || uKind == K_FARM) { layer = M_SIDING; tint = pal(s2, vec3(0.95, 0.95, 0.92), vec3(0.75, 0.85, 0.9), vec3(0.95, 0.88, 0.7), vec3(0.7, 0.8, 0.7)); tsc = 3.0; }
        else tint = pal(s2, vec3(0.97, 0.95, 0.9), vec3(0.98, 0.88, 0.7), vec3(0.92, 0.78, 0.66), vec3(0.88, 0.9, 0.86));
      } else if (uKind == K_TOWNHOUSE) {
        float unit = floor((lp.x/vScale.x + 9.0)/6.0);
        layer = fract(unit*0.37 + seed) < 0.5 ? M_BRICK : M_PLASTER;
        tint = pal(fract(unit*0.61 + seed), vec3(0.95, 0.85, 0.7), vec3(0.85, 0.6, 0.5), vec3(0.75, 0.82, 0.88), vec3(0.95, 0.93, 0.88));
      } else if (uKind == K_SHOP || uKind == K_GAS) { tint = pal(s2, vec3(0.9, 0.88, 0.84), vec3(0.85, 0.7, 0.55), vec3(0.7, 0.75, 0.8), vec3(0.95, 0.9, 0.8)); }
      else if (uKind == K_APART) { layer = s1 < 0.4 ? M_BRICK : M_CONCRETE; tint = s1 < 0.4 ? vec3(0.9, 0.8, 0.75) : pal(s2, vec3(0.92, 0.9, 0.85), vec3(0.85, 0.78, 0.7), vec3(0.8, 0.82, 0.85), vec3(0.95, 0.85, 0.75)); tsc = 4.0; }
      else if (uKind == K_TOWER) { layer = M_CONCRETE; tint = pal(s2, vec3(0.85, 0.83, 0.8), vec3(0.7, 0.68, 0.66), vec3(0.9, 0.86, 0.78), vec3(0.6, 0.62, 0.66)); tsc = 5.0; }
      else if (uKind == K_WAREHOUSE) { layer = M_CORRUGATED; tint = pal(s2, vec3(0.75, 0.78, 0.8), vec3(0.55, 0.62, 0.72), vec3(0.85, 0.82, 0.72), vec3(0.6, 0.65, 0.6)); tsc = 4.0; }
      else if (uKind == K_CHURCH) { layer = M_CONCRETE; tint = vec3(0.86, 0.8, 0.7); tsc = 1.6; }
      else if (uKind == K_LIGHTHOUSE) { tint = fract((v - 1.5)/5.2) < 0.5 ? vec3(0.95) : vec3(0.75, 0.08, 0.06); }
      vec3 p2 = layer == M_CORRUGATED ? vec3(lp.x, lp.z, lp.y) : lp;
      alb = triS(p2, n0, layer, tsc, 0.7, nb, rough)*tint;
      if (uKind == K_CHURCH) alb *= 0.85 + 0.15*step(0.06, fract(v/0.55))*step(0.04, fract(u/1.1 + floor(v/0.55)*0.5));   // ashlar courses
      // windows
      vec2 cell = vec2(2.7, 2.9), pane = vec2(1.1, 1.35); float y0 = 0.45, top = 1e9; float litP = 0.35;
      if (uKind == K_HOUSE) top = 5.6*vInst.z - 0.4;
      else if (uKind == K_HIP) { top = 3.4*vInst.z - 0.3; cell.y = 3.0; }
      else if (uKind == K_LHOUSE) top = 5.2*vInst.z - 0.4;
      else if (uKind == K_FARM) { top = 6.2*vInst.z - 0.4; cell = vec2(2.6, 3.0); pane = vec2(0.95, 1.5); }
      else if (uKind == K_TOWNHOUSE) { cell = vec2(2.0, 3.2); pane = vec2(1.0, 1.7); y0 = 0.6; }
      else if (uKind == K_SHOP || uKind == K_GAS) { top = 3.3; cell = vec2(3.2, 3.0); pane = vec2(1.4, 1.2); y0 = 0.3; if (sideX) top = 0.0; }
      else if (uKind == K_APART) { cell = vec2(2.8, 3.4); pane = vec2(1.5, 1.6); y0 = 0.5; top = 20.6*vInst.z; litP = 0.45; }
      else if (uKind == K_TOWER) { cell = vec2(1.6, 3.6); pane = vec2(1.45, 2.3); y0 = 0.3; litP = 0.4; }
      else if (uKind == K_WAREHOUSE) { cell = vec2(3.0, 6.0); pane = vec2(2.6, 0.6); y0 = 1.8; top = 6.0*vInst.z; litP = 0.15; }
      else if (uKind == K_CHURCH) { cell = vec2(3.2, 12.0); pane = vec2(1.0, 3.6); y0 = -2.4; top = 6.4; litP = 0.7; }
      else if (uKind == K_LIGHTHOUSE) { cell = vec2(9.0, 6.5); pane = vec2(0.5, 0.8); y0 = 4.0; }
      vec4 wg = windowGrid(vec2(u, v), cell, pane, y0);
      // arched church windows
      if (uKind == K_CHURCH && wg.x > 0.5) { vec2 f = vec2(fract(u/cell.x) - 0.5, (v - y0)/cell.y - wg.w - 0.5); if (f.y*cell.y > 1.3 && length(vec2(f.x*cell.x, f.y*cell.y - 1.3)) > 0.5) wg.x = 0.0; }
      if (v < top && wg.x > 0.5) {
        float lit = step(1.0 - litP, hsh(wg.zw + seed*31.0 + (sideX ? 7.0 : 0.0) + sign(n0.x + n0.z)*3.0));
        vec3 glass = uKind == K_CHURCH ? vec3(0.12, 0.08, 0.2) : vec3(0.04, 0.06, 0.08);
        alb = glass; rough = 0.08; metal = 0.0; cls = 3.0; nb = n0;
        vec3 lc = mix(vec3(1.0, 0.78, 0.5), vec3(0.85, 0.9, 1.0), step(0.75, hsh(wg.zw + 3.0)));
        if (uKind == K_CHURCH) lc = vec3(1.0, 0.6, 0.35);
        emit = lc*lit*uNight*(1.2 + 0.8*hsh(wg.zw - 5.0));
      } else if (v < top && wg.y > 0.5) { alb = uKind == K_TOWER || uKind == K_APART ? alb*0.6 : vec3(0.9, 0.9, 0.88)*pal(s3, vec3(1.0), vec3(1.0), vec3(0.3, 0.35, 0.45), vec3(0.45, 0.3, 0.2)); rough = 0.5; nb = n0; }
      alb *= 1.0 - 0.3*uWet;
    } else if (part == P_ROOF) {
      int layer = M_TILES; vec3 tint = vec3(1.0); float tsc = 3.0;
      if (uKind == K_BARN || uKind == K_WAREHOUSE) { layer = M_CORRUGATED; tint = uKind == K_BARN ? pal(s2, vec3(0.55, 0.15, 0.1), vec3(0.4, 0.42, 0.45), vec3(0.6, 0.6, 0.62), vec3(0.3, 0.32, 0.3)) : vec3(0.8); metal = 0.5; }
      else if (uKind == K_CHURCH) { layer = M_SLATE; tint = lp.y > 18.0*vInst.z ? vec3(0.45, 0.75, 0.62) : vec3(0.8, 0.82, 0.88); metal = lp.y > 18.0*vInst.z ? 0.3 : 0.0; }
      else if (s3 < 0.4) { layer = M_TILES; tint = mix(vec3(1.0, 0.8, 0.7), vec3(0.75, 0.5, 0.42), s2); }
      else if (s3 < 0.65) { layer = M_SLATE; tint = vec3(0.75, 0.77, 0.84); }
      else { layer = M_SHINGLES; tint = pal(s2, vec3(0.45, 0.45, 0.48), vec3(0.5, 0.36, 0.3), vec3(0.32, 0.38, 0.32), vec3(0.6, 0.58, 0.55)); }
      // Texture position, projection weights and the normal accumulator use the same roof frame.
      bool swapRoof = abs(n0.x) > abs(n0.z);
      vec3 rp = swapRoof ? vec3(lp.z, lp.y, lp.x) : lp;
      if (swapRoof) nb = vec3(nb.z, nb.y, nb.x);
      alb = triS(rp, swapRoof ? vec3(n0.z, n0.y, n0.x) : n0, layer, tsc, 1.0, nb, rough)*tint;
      if (swapRoof) nb = vec3(nb.z, nb.y, nb.x);
      alb *= 1.0 - 0.25*uWet;
      if (uSnow > 0.05) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.3, 0.6, n0.y)*uSnow);
    } else if (part == P_GLASS) {
      cls = 3.0; rough = 0.05; metal = 0.1;
      vec3 tint = pal(s2, vec3(0.05, 0.09, 0.12), vec3(0.06, 0.1, 0.09), vec3(0.08, 0.08, 0.1), vec3(0.1, 0.08, 0.06));
      alb = tint;
      if (uKind == K_OFFICE || uKind == K_SKY) {
        float fl = fract((v - 0.3)/3.7), mul = fract(u/1.55);
        bool spandrel = fl < 0.24, mullion = mul < 0.05;
        if (spandrel || mullion) { alb = spandrel ? tint*2.2 + vec3(0.05) : vec3(0.35); rough = 0.35; metal = 0.6; }
        else { float lit = step(0.6, hsh(vec2(floor(u/1.55), floor((v - 0.3)/3.7)) + seed*17.0 + sign(n0.x)*5.0 + sign(n0.z)*11.0));
          emit = vec3(0.95, 0.95, 1.0)*lit*uNight*1.3; }
      } else if (uKind == K_SHOP) { emit = vec3(1.0, 0.85, 0.6)*uNight*1.6; alb = vec3(0.05, 0.06, 0.07); }
      else if (uKind == K_GAS) emit = vec3(1.0, 0.95, 0.85)*uNight*1.8;
      else if (uKind == K_APART) { alb = vec3(0.12, 0.16, 0.18); rough = 0.12; }
    } else if (part == P_METAL) {
      metal = 0.7;
      if (uKind == K_SILO) { alb = triS(vec3(lp.x, lp.z, lp.y), n0, M_CORRUGATED, 2.0, 0.6, nb, rough)*vec3(0.85); rough = 0.4; }
      else if (uKind == K_WATERTOWER) { alb = pal(s2, vec3(0.75, 0.85, 0.9), vec3(0.85), vec3(0.6, 0.75, 0.6), vec3(0.85, 0.8, 0.7))*0.85; rough = 0.45; metal = 0.3; }
      else if (uKind == K_LIGHTHOUSE) { alb = vec3(0.55, 0.08, 0.05); rough = 0.4; metal = 0.3; }
      else { alb = triS(lp, n0, M_METAL, 2.0, 0.3, nb, rough)*0.8; }
    } else if (part == P_DOOR) {
      alb = pal(s3, vec3(0.35, 0.2, 0.1), vec3(0.15, 0.25, 0.4), vec3(0.6, 0.12, 0.1), vec3(0.85)); rough = 0.5;
      if (uKind == K_WAREHOUSE || uKind == K_LHOUSE && abs(lp.x - 4.2*vScale.x) < 1.5) { alb = triS(vec3(lp.x, lp.z, lp.y), n0, M_CORRUGATED, 1.0, 0.5, nb, rough)*vec3(0.75, 0.75, 0.72); metal = 0.4; }
      if (uKind == K_BARN) { alb = vec3(0.5, 0.1, 0.07); float d = abs(abs(fract(u/4.4 + 0.5) - 0.5)*4.4 - abs(v - 2.3)*1.1); if (d < 0.18) alb = vec3(0.9); }
    } else if (part == P_BRICK) { alb = triS(lp, n0, M_BRICK, 1.4, 0.8, nb, rough)*vec3(0.85, 0.75, 0.7); }
    else if (part == P_TRIM) { alb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nb, rough)*(uKind == K_LIGHTHOUSE ? vec3(0.95) : vec3(0.88, 0.86, 0.82)); if (uKind == K_FARM || uKind == K_HOUSE) alb = vec3(0.92); }
    else if (part == P_WOOD) {
      alb = triS(abs(n0.y) > 0.5 ? lp : vec3(lp.x, lp.y, lp.z), n0, M_PLANKS, 2.0, 0.8, nb, rough);
      alb *= uKind == K_BARN ? pal(s2, vec3(0.75, 0.18, 0.12), vec3(0.62, 0.16, 0.1), vec3(0.55, 0.45, 0.35), vec3(0.7, 0.2, 0.12)) : vec3(0.75, 0.62, 0.5);
      if (uKind == K_BARN && abs(u) > (sideX ? 9.0*vScale.z : 6.0*vScale.x) - 0.35) alb = vec3(0.9);   // white corner boards
    }
    else if (part == P_AWNING) { alb = mix(pal(s2, vec3(0.7, 0.1, 0.1), vec3(0.1, 0.35, 0.2), vec3(0.15, 0.25, 0.55), vec3(0.8, 0.55, 0.1)), vec3(0.92), step(0.5, fract(lp.x/0.9))); rough = 0.85; }
    else if (part == P_DARK) {
      alb = triS(lp, n0, M_GRAVEL, 2.0, 0.5, nb, rough)*0.45;
      if (n0.y > 0.75 && (uKind == K_SHOP || uKind == K_APART || uKind == K_OFFICE || uKind == K_TOWNHOUSE)) {
        // Seeded flat-roof finishes: weathered bitumen or warm reflective membrane. Keep the existing grain and
        // normal sample; no new texture, pass, instance or sub-pixel pattern is needed to break up the roof field.
        vec3 finish = mix(vec3(0.055, 0.066, 0.078), vec3(0.27, 0.25, 0.21), step(0.58, s2));
        float grain = 0.82 + 0.35*clamp(dot(alb, vec3(0.2126, 0.7152, 0.0722))*5.0, 0.0, 1.0);
        alb = finish*grain*(1.0 - 0.22*uWet);
        rough = mix(0.9, 0.48, uWet);
      }
    }
    else if (part == P_LAMP) { alb = vec3(0.1); rough = 0.05; cls = 3.0; emit = vec3(1.0, 0.9, 0.6)*(0.4 + 9.0*uNight)*(0.6 + 0.4*step(0.0, sin(atan(lp.z, lp.x) - uTime*1.2))); }
    else if (part == P_RLAMP) {   // runway light globe: tinted glass, the lamp glowing through it when the lights are on
      int ci = int(seed);
      vec3 lc = ci == 1 ? vec3(1.0, 0.7, 0.25) : ci == 2 ? vec3(0.15, 1.0, 0.35) : ci == 3 ? vec3(1.0, 0.12, 0.08) : ci == 4 ? vec3(0.15, 0.3, 1.0) : vec3(1.0, 0.93, 0.78);
      alb = mix(vec3(0.6), lc, 0.5)*0.4; rough = 0.05; metal = 0.0; cls = 3.0;
      emit = lc*uRwyLights*(1.0 + 7.0*smoothstep(0.0, 0.03, lp.y - 0.33));
    }
    else if (part == P_PAPI) {   // PAPI lens: white seen from above the unit's threshold angle, red below; sharp transition
      vec3 toC = uCam - vW; float ang = degrees(atan(toC.y, length(toC.xz)));
      vec2 face = vec2(sin(vInst.y), cos(vInst.y));
      float front = smoothstep(0.0, 0.2, dot(normalize(toC.xz), face));
      vec3 lc = mix(vec3(1.0, 0.08, 0.05), vec3(1.0, 0.95, 0.88), smoothstep(-0.05, 0.05, ang - seed));
      alb = vec3(0.05); rough = 0.05; metal = 0.0; cls = 3.0;
      emit = lc*front*8.0;
    }
    else if (part == P_SIGN) { alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; emit = alb*uNight*2.5; }
    else if (part == P_CANOPY) { alb = vec3(0.92); if (n0.y < -0.5) emit = vec3(1.0, 0.98, 0.95)*uNight*3.0; if (abs(n0.y) < 0.5 && lp.y < 4.95) alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; }
    if (uSnow > 0.05 && part != P_GLASS && part != P_ROOF) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  }
  vec3 wn = vec3(cy*nb.x + sy*nb.z, nb.y, -sy*nb.x + cy*nb.z);
  if (dot(wn, V) < -0.2) wn = normalize(wn + V*0.5);   // bumped normals must not face away from the camera
  oG0 = vec4(dist, octEnc(normalize(wn)), cls == 1.0 ? 4.0 : 3.0);   // GB_FOLIAGE / GB_ENTITY (kGBuffer)
  oG1 = vec4(sqrt(clamp(alb, 0.0, 1.0)), clamp(rough, 0.03, 1.0));
  oG2 = vec4(max(emit, vec3(0.0)), metal);
  oG3 = vec4(1.0, 1.0, 1.0, 0.0);
}
