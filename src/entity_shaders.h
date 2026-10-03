// Air Xpress - environment entity shaders: instanced meshes rasterised into a G-buffer (lit later by the ray
// tracer, which also traces their shadows on the terrain) and into the sun's shadow cascades.
#pragma once

static const char* kEntVS = R"(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec4 aAux;   // part, ao, u, v
layout(location=3) in vec4 iA; layout(location=4) in vec4 iB;   // position + yaw | scale + seed
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform float uTime; uniform int uKind; uniform int uShadowPass;
out vec3 vW; out vec3 vL; out vec3 vLN; out vec4 vAux;
flat out vec4 vInst;   // seed, yaw, scale y, instance height
flat out vec3 vScale;
void main(){
  vec3 lp = aPos*iB.xyz;
  // foliage sways a little in the wind, more towards the top and the frond tips
  if (uKind <= 6 && (aAux.x < 3.5 || aAux.x > 17.5) && uShadowPass == 0) {
    float h = max(aPos.y, 0.0)/14.0;
    float ph = uTime*(1.1 + 0.4*fract(iB.w*7.0)) + iA.x*0.05 + iA.z*0.04;
    lp.xz += vec2(sin(ph), cos(ph*0.83))*0.06*h*h*iB.y + (aAux.x > 1.5 && aAux.x < 2.5 ? vec2(0.0, sin(ph*2.3 + aPos.x))*0.08*aAux.w : vec2(0.0));
  }
  float c = cos(iA.w), s = sin(iA.w);
  vec3 wp = vec3(c*lp.x + s*lp.z, lp.y, -s*lp.x + c*lp.z) + iA.xyz;
  vec3 ln = normalize(aNrm/iB.xyz);
  vW = wp; vL = lp; vLN = ln; vAux = aAux;
  vInst = vec4(iB.w, iA.w, iB.y, iA.y); vScale = iB.xyz;
  gl_Position = uVP*vec4(wp, 1.0);
  if (uShadowPass == 0) {
    gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the ray tracer's sub-pixel jitter
    gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;   // logarithmic depth: 0.3 m .. 40 km
  }
}
)";

// Shared material code (G-buffer pass; the shadow pass only uses the cut-outs)
static const char* kEntFS1 = R"(
in vec3 vW; in vec3 vL; in vec3 vLN; in vec4 vAux; flat in vec4 vInst; flat in vec3 vScale;
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
uniform int uKind; uniform vec3 uCam; uniform float uRwyLights; uniform float uNight; uniform float uWet; uniform float uSnow; uniform float uTime;
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;
const int M_CONCRETE=8, M_TILES=9, M_SLATE=10, M_PLASTER=11, M_BRICK=12, M_LEAVES=13, M_NEEDLES=14, M_PAINT=15;
const int M_METAL=16, M_CORRUGATED=22, M_BARK=25, M_PLANKS=26, M_LITTER=27, M_SHINGLES=28, M_SIDING=29;
const int K_FIR=0, K_SPRUCE=1, K_PINE=2, K_OAK=3, K_BIRCH=4, K_PALM=5, K_BUSH=6, K_BOULDER=7, K_BLOCK=8, K_SLAB=9, K_OUTCROP=10, K_SPIRE=11, K_SEASTACK=12;
const int K_HOUSE=13, K_HIP=14, K_LHOUSE=15, K_FARM=16, K_TOWNHOUSE=17, K_SHOP=18, K_APART=19, K_OFFICE=20, K_TOWER=21, K_SKY=22;
const int K_WAREHOUSE=23, K_BARN=24, K_SILO=25, K_CHURCH=26, K_WATERTOWER=27, K_LIGHTHOUSE=28, K_GAS=29;
const int P_BARK=0, P_LEAF=1, P_FROND=2, P_NEEDLE=3, P_ROCK=4, P_WALL=5, P_ROOF=6, P_TRIM=7, P_GLASS=8, P_METAL=9, P_DOOR=10, P_BRICK=11;
const int P_AWNING=12, P_WOOD=13, P_DARK=14, P_LAMP=15, P_SIGN=16, P_CANOPY=17, P_LEAFCARD=18, P_RLAMP=19;
float hsh(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float hsh3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)))*43758.5453); }
float vn3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f);
  return mix(mix(mix(hsh3(i), hsh3(i + vec3(1,0,0)), f.x), mix(hsh3(i + vec3(0,1,0)), hsh3(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(hsh3(i + vec3(0,0,1)), hsh3(i + vec3(1,0,1)), f.x), mix(hsh3(i + vec3(0,1,1)), hsh3(i + vec3(1,1,1)), f.x), f.y), f.z); }
// foliage cut-outs: holes through the clumps towards their silhouettes (leafy edges, dappled shadows)
bool leafCut(float viewEdge){
  int part = int(vAux.x + 0.5);
  if (part == P_LEAF) {
    float n = vn3(vL*2.3 + vInst.x*17.0)*0.55 + vn3(vL*7.3 - vInst.x*9.0)*0.45;
    return n < 0.24 + 0.4*viewEdge;
  }
  if (part == P_LEAFCARD && uKind == K_PINE) {   // a tuft of needles radiating from the shoot
    vec2 q = vec2(fract(vAux.z), vAux.w)*2.0 - 1.0; float r = length(q);
    float a = atan(q.y, q.x)/6.2832 + 0.5;
    float k = fract(a*46.0 + r*0.6 + hsh(vec2(floor(vAux.z), vInst.x))*7.0);
    return r > 0.95 - 0.25*hsh(vec2(floor(a*46.0), floor(vAux.z))) || r < 0.06 || k > 0.32;
  }
  if (part == P_LEAFCARD) {   // a spray of leaves: jittered ellipses on a 3x3 grid, each at its own angle
    vec2 uv0 = vec2(fract(vAux.z), vAux.w);
    if (length(uv0 - 0.5) > 0.47 - 0.12*vn3(vec3(uv0*5.0, floor(vAux.z) + vInst.x*7.0))) return true;   // ragged round spray
    vec2 uv = uv0*5.0, c0 = floor(uv);
    float best = 9.0;
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
      vec2 c = c0 + vec2(i, j);
      float h = hsh(c + floor(vAux.z)*7.0 + vInst.x*13.0);
      vec2 ctr = c + 0.5 + (vec2(h, fract(h*17.3)) - 0.5)*0.6;
      float a = h*6.2832, ca = cos(a), sa = sin(a);
      vec2 d = uv - ctr; d = vec2(ca*d.x + sa*d.y, -sa*d.x + ca*d.y);
      best = min(best, length(d/vec2(0.5, 0.23)) + 0.15*step(abs(d.y), 0.02));
    }
    return best > 1.0;
  }
  if (part == P_FROND) {   // leaflets either side of the midrib
    float a = abs(vAux.z);
    if (a < 0.07) return false;
    float l = fract(vAux.w*34.0 + a*2.2);
    return l > 0.58 || a > 0.97 - 0.25*vAux.w*vAux.w;
  }
  return false;
}
)";

static const char* kEntFS2 = R"(
layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1; layout(location=2) out vec4 oG2;
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
  if (leafCut(dist < 700.0 ? pow(1.0 - abs(dot(wn0, V)), 1.5) : 0.0)) discard;
  int part = int(vAux.x + 0.5);
  float seed = vInst.x, ao = vAux.y;
  vec3 alb = vec3(0.5); float rough = 0.8, metal = 0.0, cls = 2.0; vec3 emit = vec3(0.0);
  vec3 nb = n0; float r0;
  vec3 lp = vL;
  float wy = vW.y;
  if (part == P_LEAFCARD) {
    cls = 1.0;
    float h = hsh(floor(vec2(fract(vAux.z), vAux.w)*5.0) + floor(vAux.z)*7.0 + vInst.x*13.0);
    vec3 tint = uKind == K_OAK ? vec3(0.07, 0.12, 0.035) : uKind == K_BIRCH ? vec3(0.11, 0.17, 0.045) : uKind == K_PINE ? vec3(0.045, 0.085, 0.06) : vec3(0.075, 0.12, 0.04);
    alb = tint*mix(0.75, 1.3, h)*mix(0.82, 1.12, fract(seed*5.3));
    alb = mix(alb, alb*vec3(1.6, 1.05, 0.55), smoothstep(0.8, 1.0, fract(seed*13.7))*0.8);   // trees turning
    alb *= mix(0.6, 1.0, ao);
    rough = 0.6;
    nb = n0;
  } else if (part == P_LEAF || part == P_NEEDLE || part == P_FROND) {
    cls = 1.0;
    if (part == P_NEEDLE) {
      alb = triS(lp, n0, M_NEEDLES, 0.9, 1.4, nb, rough);
      vec3 tint = (uKind == K_SPRUCE ? vec3(0.5, 0.7, 0.68) : uKind == K_PINE ? vec3(0.5, 0.68, 0.56) : vec3(0.55, 0.76, 0.62))*mix(0.85, 1.1, vAux.z);
      alb *= tint*mix(0.85, 1.15, fract(seed*7.31));
      if (uSnow > 0.05 || wy > 1500.0) alb = mix(alb, vec3(0.85, 0.88, 0.92), smoothstep(0.35, 0.8, n0.y)*max(uSnow, smoothstep(1500.0, 1900.0, wy))*0.85);
    } else if (part == P_FROND) {
      alb = triS(lp, n0, M_LEAVES, 1.2, 0.5, nb, rough)*vec3(0.85, 1.05, 0.55);
      alb = mix(alb, vec3(0.35, 0.3, 0.12), smoothstep(0.75, 1.0, vAux.w)*0.6 + step(abs(vAux.z), 0.07)*0.5);
    } else {
      // leaf masses: fine leaf texture, plus a lumpy noise normal so a clump reads as many small sprays
      alb = triS(lp, n0, M_LEAVES, 0.75, 1.6, nb, rough);
      vec3 q = lp*3.1 + vInst.x*11.0;
      vec3 g = vec3(vn3(q + vec3(0.7, 0.0, 0.0)) - vn3(q - vec3(0.7, 0.0, 0.0)), vn3(q + vec3(0.0, 0.7, 0.0)) - vn3(q - vec3(0.0, 0.7, 0.0)), vn3(q + vec3(0.0, 0.0, 0.7)) - vn3(q - vec3(0.0, 0.0, 0.7)));
      nb = normalize(nb + g*1.4);
      vec3 tint = uKind == K_OAK ? vec3(0.5, 0.68, 0.34) : uKind == K_BIRCH ? vec3(0.7, 0.86, 0.38) : vec3(0.48, 0.62, 0.32);
      float hue = fract(seed*13.7);
      tint = mix(tint, tint*vec3(1.2, 0.92, 0.6), smoothstep(0.8, 1.0, hue));   // a few trees turning
      tint *= mix(0.78, 1.12, fract(seed*5.3))*mix(0.85, 1.12, vAux.z);       // tree and clump variation
      float spray = vn3(lp*5.3 - vInst.x*3.0);
      alb *= tint*mix(0.7, 1.15, spray);
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
  } else {
)"
R"(    // ---------------------------------------------------------------- buildings
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
)"
R"(    } else if (part == P_ROOF) {
      int layer = M_TILES; vec3 tint = vec3(1.0); float tsc = 3.0;
      if (uKind == K_BARN || uKind == K_WAREHOUSE) { layer = M_CORRUGATED; tint = uKind == K_BARN ? pal(s2, vec3(0.55, 0.15, 0.1), vec3(0.4, 0.42, 0.45), vec3(0.6, 0.6, 0.62), vec3(0.3, 0.32, 0.3)) : vec3(0.8); metal = 0.5; }
      else if (uKind == K_CHURCH) { layer = M_SLATE; tint = lp.y > 18.0*vInst.z ? vec3(0.45, 0.75, 0.62) : vec3(0.8, 0.82, 0.88); metal = lp.y > 18.0*vInst.z ? 0.3 : 0.0; }
      else if (s3 < 0.4) { layer = M_TILES; tint = mix(vec3(1.0, 0.8, 0.7), vec3(0.75, 0.5, 0.42), s2); }
      else if (s3 < 0.65) { layer = M_SLATE; tint = vec3(0.75, 0.77, 0.84); }
      else { layer = M_SHINGLES; tint = pal(s2, vec3(0.45, 0.45, 0.48), vec3(0.5, 0.36, 0.3), vec3(0.32, 0.38, 0.32), vec3(0.6, 0.58, 0.55)); }
      // roof texture runs down the slope: project along the roof's own axes
      vec3 rp = abs(n0.x) > abs(n0.z) ? vec3(lp.z, lp.y, lp.x) : lp;
      alb = triS(rp, abs(n0.x) > abs(n0.z) ? vec3(n0.z, n0.y, n0.x) : n0, layer, tsc, 1.0, nb, rough)*tint;
      if (abs(n0.x) > abs(n0.z)) nb = vec3(nb.z, nb.y, nb.x);
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
    else if (part == P_DARK) { alb = triS(lp, n0, M_GRAVEL, 2.0, 0.5, nb, rough)*0.45; }
    else if (part == P_LAMP) { alb = vec3(0.1); rough = 0.05; cls = 3.0; emit = vec3(1.0, 0.9, 0.6)*(0.4 + 9.0*uNight)*(0.6 + 0.4*step(0.0, sin(atan(lp.z, lp.x) - uTime*1.2))); }
    else if (part == P_RLAMP) {   // runway light globe: tinted glass, the lamp glowing through it when the lights are on
      int ci = int(seed);
      vec3 lc = ci == 1 ? vec3(1.0, 0.7, 0.25) : ci == 2 ? vec3(0.15, 1.0, 0.35) : ci == 3 ? vec3(1.0, 0.12, 0.08) : vec3(1.0, 0.93, 0.78);
      alb = mix(vec3(0.6), lc, 0.5)*0.4; rough = 0.05; metal = 0.0; cls = 3.0;
      emit = lc*uRwyLights*(1.0 + 7.0*smoothstep(0.0, 0.03, lp.y - 0.33));
    }
    else if (part == P_SIGN) { alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; emit = alb*uNight*2.5; }
    else if (part == P_CANOPY) { alb = vec3(0.92); if (n0.y < -0.5) emit = vec3(1.0, 0.98, 0.95)*uNight*3.0; if (abs(n0.y) < 0.5 && lp.y < 4.95) alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; }
    if (uSnow > 0.05 && part != P_GLASS && part != P_ROOF) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  }
  vec3 wn = vec3(cy*nb.x + sy*nb.z, nb.y, -sy*nb.x + cy*nb.z);
  if (dot(wn, V) < -0.2) wn = normalize(wn + V*0.5);   // bumped normals must not face away from the camera
  oG0 = vec4(dist, octEnc(normalize(wn)), cls);
  oG1 = vec4(sqrt(clamp(alb, 0.0, 1.0)), clamp(rough, 0.03, 1.0));
  oG2 = vec4(sqrt(clamp(emit*0.125, 0.0, 1.0)), metal);
}
)";

static const char* kEntShadowFS = R"(
void main(){ if (leafCut(0.12)) discard; }
)";
