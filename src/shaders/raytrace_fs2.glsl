//! kRaytraceFS2
//! Split in two constants: MSVC limits a concatenated string literal to 64 KB.
// ---------------------------------------------------------------- terrain
uniform sampler2D uHMax;  // conservative max height per cell, mip L = 256>>L cells per side
const int HMAXN = 256; const int HMAXL = 5;
// Ray march the heightfield. Cells the ray passes entirely above (per the max-height mip chain) are skipped,
// which keeps grazing rays over lowlands from running out of steps (they used to fall through to the sea).
// gTStart: where the march may begin (the terrain envelope mesh on this pixel - see terrain_envelope.cpp); 1e30 means
// the ray meets no terrain in the world. A start that turns out to be under the ground (it never should) is ignored.
uniform int uEnvOn; uniform int uHullOn;
float gTStart = 1.0;
float traceTerrain(vec3 ro, vec3 rd, float tmax){
  float t = 1.0;
  if (gTStart > 1e29) return -1.0;
  if (gTStart > t && gTStart < tmax) { vec3 ps = ro + rd*gTStart; if (ps.y > terrainH(ps.xz, 7)) t = gTStart; }
  else if (gTStart >= tmax) return -1.0;
  if (ro.y > uMaxH) { if (rd.y >= 0.0) return -1.0; t = max(t, (ro.y - uMaxH)/(-rd.y)); }
  float lt = t, ldh = 0.0; bool skipped = true;
  int maxSteps = uQuality > 1 ? 360 : (uQuality > 0 ? 270 : 190);
  if ((uDbg & 64) != 0) maxSteps /= 2;
  vec2 ird = vec2(abs(rd.x) > 1e-6 ? 1.0/rd.x : 1e9, abs(rd.z) > 1e-6 ? 1.0/rd.z : 1e9);
  for (int i=0;i<360;i++){
    if (i >= maxSteps || t > tmax) break;
    vec3 p = ro + rd*t;
    if (p.y > uMaxH && rd.y > 0.0) return -1.0;
    bool sk = false;
    for (int L = HMAXL - 1; L >= 0; L--) {
      int n = HMAXN >> L; float cs = 2.0*WH/float(n);
      ivec2 ci = clamp(ivec2(floor((p.xz + WH)/cs)), ivec2(0), ivec2(n - 1));
      float mh = texelFetch(uHMax, ci, L).r;
      if (p.y <= mh) continue;
      vec2 c0 = vec2(ci)*cs - WH;
      vec2 te2 = (mix(c0, c0 + cs, step(0.0, rd.xz)) - ro.xz)*ird;
      float te = min(te2.x, te2.y);
      float ty = rd.y < 0.0 ? (mh - ro.y)/rd.y : 1e9;
      float tn = min(te + 0.05 + t*1e-5, ty);
      if (tn > t + 0.01) { t = tn; sk = true; break; }
    }
    if (sk) { skipped = true; continue; }
    int oct = t < 1500.0 ? 7 : (t < 6000.0 ? 6 : 5);
    float h = terrainH(p.xz, oct);
    float dh = p.y - h;
    if (dh < 0.0015*t) {
      if (skipped) return t;
      return lt + (t - lt) * ldh / max(ldh - dh, 1e-4);
    }
    skipped = false; lt = t; ldh = dh;
    float k = t > 4500.0 ? 0.6 : 0.42;
    t += max(dh*k, 0.2 + 0.0015*t);
  }
  // out of steps while skimming just above the ground: count it as a hit rather than showing the sea through hills
  if (t <= tmax) { vec3 p = ro + rd*t; if (p.y - terrainH(p.xz, 5) < 0.02*t) return t; }
  return -1.0;
}
uniform sampler2D uTSh; uniform int uTShOn;   // baked terrain sun shadow (see kTShBakeMain)
float terrainShadow(vec3 ro, vec3 rd, float camT){
  if ((uDbg & 2) != 0) return 1.0;
  if (uTShOn == 1) {   // one lookup instead of a march: the height a point here needs to see the sun, and how far away
    vec2 hd = texture(uTSh, ro.xz/(2.0*WH) + 0.5).xy;   // the terrain that blocks it is (that sets the penumbra)
    return clamp(12.0*length(rd.xz)*(ro.y - hd.x)/max(hd.y, 1.0), 0.0, 1.0);
  }
  float res = 1.0, t = 2.0;
  // distant pixels cover many metres each: fewer steps and octaves are enough there (the profile showed terrain
  // shadows among the most expensive features)
  int n = camT > 6000.0 ? 16 : (camT > 2000.0 ? 26 : 40), oct = camT > 2000.0 ? 3 : 4;
  for (int i=0;i<40;i++){
    if (i >= n) break;
    vec3 p = ro + rd*t;
    if (p.y > uMaxH) break;
    float h = p.y - terrainH(p.xz, oct);
    res = min(res, 12.0*h/t);
    if (res < 0.0) return 0.0;
    t += clamp(h*0.6, 6.0, 450.0);
  }
  return clamp(res, 0.0, 1.0);
}
vec3 terrainNormal(vec2 p, float t){
  float e = max(0.25, t*0.0012);
  int oct = t < 600.0 ? 11 : (t < 3000.0 ? 9 : 7);
  return normalize(vec3(terrainH(p - vec2(e,0.0), oct) - terrainH(p + vec2(e,0.0), oct), 2.0*e,
                        terrainH(p - vec2(0.0,e), oct) - terrainH(p + vec2(0.0,e), oct)));
}
// ---------------------------------------------------------------- environment entities (G-buffer from the raster pass)
uniform sampler2D uGB0; uniform sampler2D uGB1; uniform sampler2D uGB2;
uniform int uShOn; uniform sampler2DShadow uShMap0; uniform sampler2DShadow uShMap1; uniform mat4 uShM0; uniform mat4 uShM1; uniform vec2 uShTexel;
uniform vec4 uShFade; uniform vec4 uShFadeR;   // camera-anchored fade centres (xz, per cascade) and fade radii
uniform float uTreeFar;
vec3 octDec(vec2 e){ vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y); if (n.y < 0.0) n.xz = (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); return normalize(n); }
// sun shadow of trees, rocks and buildings (two cascades, 2x2 PCF in the comparison sampler; normal offset against acne)
float shTap(sampler2DShadow m, vec3 q, float bias){ return texture(m, vec3(q.xy, q.z - bias)); }
float shCascade(int c, vec3 p, vec3 n){
  vec3 pp = p + n*(c == 0 ? uShTexel.x : uShTexel.y)*1.5;
  vec4 h = (c == 0 ? uShM0 : uShM1)*vec4(pp, 1.0);
  vec3 q = h.xyz*0.5 + 0.5;
  vec2 edge = abs(q.xy - 0.5);
  if (max(edge.x, edge.y) > 0.48 || q.z > 1.0) return 1.0;   // (only steep terrain far above or below the centre)
  return c == 0 ? shTap(uShMap0, q, 0.0004) : shTap(uShMap1, q, 0.0006);
}
// Scenery shadows from two cached sun cascades. Each fades out with distance from a point that moves smoothly with
// the camera, well inside the area its map covers: the near cascade hands over to the far one, the far one to none,
// and a map re-rendering as the camera moves on never makes shadows appear or vanish.
float entShadow(vec3 p, vec3 n){
  if (uShOn == 0 || (uDbg & 4) != 0) return 1.0;
  float w0 = 1.0 - smoothstep(uShFadeR.x, uShFadeR.y, length(p.xz - uShFade.xy));
  float w1 = uShOn > 1 ? 1.0 - smoothstep(uShFadeR.z, uShFadeR.w, length(p.xz - uShFade.zw)) : 0.0;
  float s = 1.0;
  if (w0 < 1.0 && w1 > 0.0) s = mix(1.0, shCascade(1, p, n), w1);
  if (w0 > 0.0) s = mix(s, shCascade(0, p, n), w0);
  return s;
}

// ---------------------------------------------------------------- clouds
// Volumetric cumulus: a coverage field (2D) gives each cloud its footprint; a flat base and a billowing, rounded top
// come from the height profile, two scales of 3D noise carve the billows, and fine 3D detail erodes only the thin
// edges into wisps (the dense cores stay solid).
uniform sampler2D uCloudCov; uniform sampler3D uNoise3;
float cn3(vec3 x){ return textureLod(uNoise3, x*(1.0/32.0), 0.0).r; }   // vnoise3 from the baked volume
float tfbm(vec2 x){ return textureLod(uCloudCov, x*(1.0/16.0), 0.0).r*0.9375; }   // fbm2(x, 4) from the baked map
float cloudDensity(vec3 p, int detail){
  float thick = 900.0 + 900.0*uCloudCover;
  float hf = (p.y - uCloudBase) / thick;
  if (hf < 0.0 || hf > 1.0) return 0.0;
  vec2 q = (p.xz + uWindOff) / 5200.0;
  float cov = textureLod(uCloudCov, q*(1.0/16.0), 0.0).r*0.9375;
  float shape = smoothstep(0.0, 0.07, hf) * smoothstep(1.0, 0.4 - 0.22*uCloudCover, hf);
  float d = cov - (1.05 - uCloudCover*0.75) + shape*0.45 - 0.45;
  if (d < -0.2) return 0.0;
  vec3 w = p + vec3(uWindOff.x, 0.0, uWindOff.y);
  float bill = cn3(w/760.0)*0.6 + cn3(w/270.0 + vec3(11.3, 4.1, 7.7))*0.4;
  d += (bill - 0.55)*0.42*(0.55 + hf);                                       // billows, deeper towards the tops
  if (detail > 0) {
    float e = (cn3(w/95.0 + vec3(3.7, uTime*0.015, 1.9)) - 0.5)*0.17 + (cn3(w/36.0 + vec3(17.1, 9.3, 5.5)) - 0.5)*0.06;
    d += e*(1.0 - smoothstep(0.0, 0.3, d));                                   // wispy edges, solid cores
  }
  return clamp(d*4.5, 0.0, 1.0);
}
float hgPhase(float c, float g){ float g2 = g*g; return (1.0 - g2)/(12.566*pow(max(1.0 + g2 - 2.0*g*c, 1e-4), 1.5)); }
int gCloudLite = 0;   // reflections: half the steps
vec4 traceClouds(vec3 ro, vec3 rd, float tmax, float jitter){
  if (uCloudCover < 0.02 || (uDbg & 1) != 0) return vec4(0.0,0.0,0.0,1.0);
  float thick = 900.0 + 900.0*uCloudCover;
  float yb = uCloudBase, yt = uCloudBase + thick;
  float t0, t1;
  if (abs(rd.y) < 1e-4) { if (ro.y < yb || ro.y > yt) return vec4(0,0,0,1); t0 = 0.0; t1 = 30000.0; }
  else {
    float ta = (yb - ro.y)/rd.y, tb = (yt - ro.y)/rd.y;
    t0 = max(min(ta,tb), 0.0); t1 = max(ta,tb);
  }
  t1 = min(t1, min(tmax, 45000.0));
  if (t1 <= t0) return vec4(0,0,0,1);
  int N = (uQuality > 1 ? 56 : (uQuality > 0 ? 40 : 24)) >> gCloudLite;
  float dt = (t1 - t0)/float(N);
  float T = 1.0; vec3 L = vec3(0.0);
  float mu = dot(rd, uSunDir);
  vec3 sunC = uSunCol; vec3 amb = skyColor(vec3(0.0,1.0,0.0))*1.4 + vec3(0.05);
  vec3 skyH = skyColor(normalize(vec3(rd.x, max(rd.y,0.02), rd.z)));
  // phase: a strong forward lobe (silver linings towards the sun) plus some back-scatter
  float ph0 = mix(hgPhase(mu, 0.8), hgPhase(mu, -0.25), 0.3);
  float ph1 = mix(hgPhase(mu, 0.4), hgPhase(mu, -0.12), 0.3), ph2 = mix(hgPhase(mu, 0.2), hgPhase(mu, -0.06), 0.3);
  float t = t0 + dt*jitter;
  for (int i=0;i<84;i++){
    if (t > t1) break;
    COST(2);
    vec3 p = ro + rd*t;
    float d = cloudDensity(p, 1);
    if (d <= 0.01) { t += dt*1.5; continue; }   // clear air between clouds: longer strides
    {
      // light march towards the sun: optical depth through the cloud above this point
      float od = (cloudDensity(p + uSunDir*60.0, 0)*60.0 + cloudDensity(p + uSunDir*160.0, 0)*100.0
                + cloudDensity(p + uSunDir*340.0, 0)*180.0 + cloudDensity(p + uSunDir*650.0, 0)*310.0)*0.012;
      // multiple scattering (three octaves, each less absorbed and less directional) and the powder darkening of
      // thin edges seen side-on to the sun
      vec3 ms = vec3(exp(-od)*ph0 + 0.5*exp(-od*0.5)*ph1 + 0.25*exp(-od*0.25)*ph2);
      float powder = mix(1.0, 1.0 - exp(-d*7.0), 0.6*(1.0 - max(mu, 0.0)));
      float hf = clamp((p.y - uCloudBase)/thick, 0.0, 1.0);
      vec3 c = sunC*ms*powder*11.0 + amb*(0.3 + 0.7*hf)*(1.0 - 0.4*uStorm)*(0.75 + 0.25*exp(-od*0.3));
      c += vec3(0.8,0.85,1.0)*uLightning*2.0;
      float a = 1.0 - exp(-d*dt*0.016);
      float fogT = exp(-uFogB*t*0.6);
      L += T*a*mix(skyH, c, fogT);
      T *= 1.0 - a;
      if (T < 0.02) break;
    }
    t += dt;
  }
  return vec4(L, T);
}
float cloudShadow(vec3 p){
  if (uCloudCover < 0.05 || (uDbg & 32) != 0) return 1.0;
  vec3 c = p + uSunDir * ((uCloudBase + 500.0 - p.y)/max(uSunDir.y, 0.1));
  float d = cloudDensity(vec3(c.x, uCloudBase + 400.0, c.z), 0);
  return mix(1.0, 0.25, smoothstep(0.0, 0.5, d));
}

// ---------------------------------------------------------------- lighting
vec3 fresnelSchlick(float c, vec3 f0){ float k = clamp(1.0-c, 0.0, 1.0); return f0 + (1.0-f0)*(k*k*k*k*k); }
vec3 pbr(vec3 n, vec3 v, vec3 l, vec3 alb, float rough, float metal, vec3 lightCol){
  vec3 h = normalize(v+l); float nl = max(dot(n,l),0.0), nv = max(dot(n,v),1e-3), nh = max(dot(n,h),0.0), vh = max(dot(v,h),0.0);
  float a = rough*rough, a2 = a*a; float dd = nh*nh*(a2-1.0)+1.0; float D = a2/(PI*dd*dd);
  float k = (rough+1.0)*(rough+1.0)/8.0; float G = nv/(nv*(1.0-k)+k) * nl/(nl*(1.0-k)+k);
  vec3 f0 = mix(vec3(0.04), alb, metal); vec3 F = fresnelSchlick(vh, f0);
  vec3 spec = min(D*G*F/(4.0*nv*max(nl,1e-3)+1e-3), vec3(60.0));  // bounded: tiny glossy parts must not overflow fp16
  vec3 kd = (1.0-F)*(1.0-metal);
  return (kd*alb/PI + spec)*lightCol*nl;
}
vec3 ambientLight(vec3 n){
  vec3 skyUp = skyColor(normalize(vec3(0.3,1.0,0.2)));
  vec3 ground = vec3(0.12,0.11,0.08)*(uSunCol.g + 0.02);
  return mix(ground, skyUp*2.2, n.y*0.5+0.5) + vec3(0.03,0.04,0.07)*uNight*0.4;
}

// triplanar/planar texture sampling with anti-tiling (two scales)
vec4 texA(vec2 uv, int layer){ return texture(uAlb, vec3(uv, float(layer))); }
vec4 texN(vec2 uv, int layer){ return texture(uNrm, vec3(uv, float(layer))); }
vec4 matSample(vec2 xz, int layer, float scale, out vec3 nTS){
  vec2 uv1 = xz/scale, uv2 = xz/(scale*4.7) + 0.37;
  vec4 a = mix(texA(uv1, layer), texA(uv2, layer), 0.35);
  vec4 n = mix(texN(uv1, layer), texN(uv2, layer), 0.35);
  nTS = vec3(n.xy*2.0-1.0, 1.0);
  return vec4(a.rgb*a.rgb, a.a);  // texture stored gamma-ish, linearise
}

struct Mat { vec3 alb; float rough; float metal; vec3 nrm; vec3 emit; };

// Anti-tiled ground sample: two decorrelated (offset + rotated) lookups mixed by low-frequency noise with a
// height-aware seam, plus a macro-scale layer and the texture's ambient occlusion. h returns the surface height.
vec4 groundSample(vec2 xz, int layer, float scale, out vec3 nTS, out float h){
  vec2 uv1 = xz/scale;
  vec2 uv1b = vec2(0.8253*xz.x - 0.5646*xz.y, 0.5646*xz.x + 0.8253*xz.y)/(scale*1.07) + vec2(0.43, 0.71);
  float k = vnoise(xz/(scale*3.3));
  vec4 a1 = texA(uv1, layer), a1b = texA(uv1b, layer);
  vec4 n1 = texN(uv1, layer), n1b = texN(uv1b, layer);
  float hb = clamp((k - 0.5)*5.0 + (n1b.z - n1.z)*2.0 + 0.5, 0.0, 1.0);
  vec4 a = mix(a1, a1b, hb), n = mix(n1, n1b, hb);
  vec2 uv2 = xz/(scale*5.3) + 0.37;
  a = mix(a, texA(uv2, layer), 0.3); n = mix(n, texN(uv2, layer), 0.3);
  nTS = vec3(n.xy*2.0 - 1.0, 1.0);
  h = n.z;
  return vec4(a.rgb*a.rgb*mix(0.7, 1.0, n.w), a.a);
}
// Height-based layer blend: inside the transition zone the higher surface wins (sand settles in grass gaps,
// snow fills hollows first, rock pokes through) instead of a flat cross-fade.
float hblend(float w, float hBase, float hLayer){
  w = clamp(w, 0.0, 1.0);
  return smoothstep(0.0, 1.0, clamp(w + (hLayer - hBase)*w*(1.0 - w)*3.2, 0.0, 1.0));
}

// triplanar PBR sample (world or local coordinates)
vec4 triSample(vec3 p, vec3 n, int layer, float scale, out vec3 nTS){
  vec3 bw = pow(abs(n), vec3(4.0)); bw /= dot(bw, vec3(1.0));
  vec3 n1, n2, n3;
  vec4 r = matSample(p.zy, layer, scale, n1)*bw.x + matSample(p.xz, layer, scale, n2)*bw.y + matSample(p.xy, layer, scale, n3)*bw.z;
  nTS = n1*bw.x + n2*bw.y + n3*bw.z;
  return r;
}

// Runway / airport surfaces in runway-local coords (u along, v across)
int airportAt(vec2 p, out vec2 uv){
  for (int i=0;i<16;i++){
    if (i >= uApCount) break;
    vec4 a = uAp[i]; vec4 d = uApDim[i];
    vec2 dp = p - a.xy; float s = sin(a.w), c = cos(a.w);
    float u = dp.x*s - dp.y*c, v = dp.x*c + dp.y*s;
    if (abs(u) < d.x*0.5 + 600.0 && abs(v) < 600.0) { uv = vec2(u, v); return i; }
  }
  return -1;
}
// seven-segment runway designator digit; q in [0,1]^2 (x across, y = reading direction)
float seg7(vec2 q, int d){
  if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return 0.0;
  d = clamp(d, 0, 9);
  // a select chain, not a local array: NVIDIA gives every inlined copy of an indexed array its own scratch resource
  int b = d < 5 ? (d == 0 ? 0x3F : d == 1 ? 0x06 : d == 2 ? 0x5B : d == 3 ? 0x4F : 0x66)
                : (d == 5 ? 0x6D : d == 6 ? 0x7D : d == 7 ? 0x07 : d == 8 ? 0x7F : 0x6F);
  float w = 0.16; float on = 0.0;
  // a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
  if ((b & 1) != 0 && q.y > 1.0 - w) on = 1.0;
  if ((b & 2) != 0 && q.x > 1.0 - w && q.y > 0.5) on = 1.0;
  if ((b & 4) != 0 && q.x > 1.0 - w && q.y < 0.5) on = 1.0;
  if ((b & 8) != 0 && q.y < w) on = 1.0;
  if ((b & 16) != 0 && q.x < w && q.y < 0.5) on = 1.0;
  if ((b & 32) != 0 && q.x < w && q.y > 0.5) on = 1.0;
  if ((b & 64) != 0 && abs(q.y - 0.5) < w*0.5) on = 1.0;
  return on;
}
float rwyDigits(vec2 q, int num){
  // two digits side by side, each 7 m wide x 18 m long, 3 m gap; q relative to the block centre (x across, y along)
  int d0 = num/10, d1 = num - d0*10;
  float a = seg7(vec2((q.x + 8.5)/7.0, q.y/18.0 + 0.5), d0);
  float b = seg7(vec2((q.x - 1.5)/7.0, q.y/18.0 + 0.5), d1);
  return max(a, b);
}

// Airport ground plan: mirrors aptLayout() in airport_layout.h (taxiway, exits, apron, car parks; strip parking).
// Returns true when it painted the point. No local arrays (see seg7).
float lineM(float d, float hw){ return step(abs(d), hw); }
bool aptGround(int ai, vec2 uv, vec3 pw, int surf, int size, float len, float wid, inout Mat m){
  float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
  float u = uv.x, vv = uv.y*side, hw = wid*0.5;
  vec3 nTS;
  vec3 yel = vec3(0.62, 0.47, 0.05), wht = vec3(0.8);
  if (surf == 0 && size > 0) {
    bool big = size == 2;
    float twHW = big ? 11.5 : 7.5, twV = hw + (big ? 95.0 : 62.0);
    float apV0 = twV + twHW, apV1 = apV0 + (big ? 120.0 : 85.0);
    float apU0 = -len*(big ? 0.26 : 0.22), apU1 = len*(big ? 0.17 : 0.14);
    float bldV = apV1 + 2.0, lotV0 = bldV + (big ? 48.0 : 34.0), lotV1 = lotV0 + (big ? 50.0 : 34.0);
    float ap = apU1 - apU0;
    float termU = apU0 + ap*(big ? 0.51 : 0.47), termHL = min(ap*(big ? 0.17 : 0.1), big ? 130.0 : 45.0);
    float tieU0 = big ? apU0 + ap*0.2 : apU0 + 12.0, tieU1 = apU0 + ap*(big ? 0.31 : 0.32);
    float standU0 = apU0 + ap*(big ? 0.72 : 0.6), standU1 = apU1 - (big ? 25.0 : 20.0);
    int padLayer = big ? M_CONCRETE : M_ASPHALT;
    // nearest exit (signed offset of u from its centreline)
    float e0 = -(len*0.5 - 25.0), e1 = big ? -len*0.22 : -len*0.12, e2 = big ? len*0.05 : len*0.22, e3 = big ? len*0.27 : 1e9, e4 = len*0.5 - 25.0;
    float ce = u - e0;
    if (abs(u - e1) < abs(ce)) ce = u - e1; if (abs(u - e2) < abs(ce)) ce = u - e2;
    if (abs(u - e3) < abs(ce)) ce = u - e3; if (abs(u - e4) < abs(ce)) ce = u - e4;
    bool onTw = abs(vv - twV) < twHW + 2.0 && abs(u) < len*0.5 - 25.0 + twHW + 2.0;
    bool onEx = abs(ce) < twHW + 2.0 && vv > hw + 3.0 && vv < twV;
    bool onAp = vv >= apV0 && vv < apV1 + 1.0 && u > apU0 && u < apU1;
    float lu0 = termU - termHL - 24.0, lu1 = termU + termHL + 24.0;
    bool onLot = vv > bldV + (big ? 40.0 : 28.0) && vv < lotV1 + 2.0 && u > lu0 && u < lu1;
    if (!(onTw || onEx || onAp || onLot)) return false;
    vec4 t = matSample(pw.xz, onLot ? M_ASPHALT : (onAp ? padLayer : M_ASPHALT), 8.0, nTS);
    m.alb = t.rgb*(onLot ? 0.85 : 1.02); m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
    float paint = 0.0; vec3 pc = yel;
    if (onLot) {
      if (vv > lotV0 - 0.5) {   // stalls either side of each aisle, 2.7 m wide; kerbs at the ends
        float mm = mod(vv - lotV0, 16.0);
        bool stall = mm < 5.5 || mm > 10.5;
        if (stall && abs(fract((u - lu0)/2.7) - 0.5)*2.7 > 1.27) { paint = 1.0; pc = wht; }
        if (abs(mm - 5.5) < 0.08 || abs(mm - 10.5) < 0.08) { paint = 1.0; pc = wht; }
      } else if (abs(vv - (bldV + (big ? 44.0 : 31.0))) < 0.1 && fract(u/6.0) < 0.5) { paint = 1.0; pc = wht; }   // forecourt road
      if (abs(u - lu0) < 0.4 || abs(u - lu1) < 0.4) { m.alb = vec3(0.6, 0.6, 0.58); }
    } else if (onAp && !onTw) {
      // weathered pavement: slab joints, patched panels, rubber and fuel staining
      m.alb *= big ? 0.78 : 0.95;
      m.alb *= 0.88 + 0.2*vnoise(pw.xz/23.0) - 0.1*smoothstep(0.6, 0.9, vnoise(pw.xz/7.0 + 3.1));
      if (big) { vec2 sj = abs(fract(pw.xz/6.0) - 0.5); if (max(sj.x, sj.y) > 0.49) m.alb *= 0.78;
        vec2 slab = floor(pw.xz/6.0); m.alb *= 0.93 + 0.12*hash2i(ivec2(slab)); }
      // AI stands: lead-in line from the taxiway, stop bar, oil stains
      if (u > standU0 && u < standU1) {
        float su = mod(u - standU0 - 22.5, 45.0) - 22.5;
        if (abs(su) < 0.15 && vv < apV1 - 14.0) paint = 1.0;
        if (abs(vv - (apV1 - 14.0)) < 0.15 && abs(su) < 3.0) paint = 1.0;
        m.alb *= 1.0 - 0.35*smoothstep(0.55, 0.85, vnoise(pw.xz*0.4))*smoothstep(10.0, 2.0, length(vec2(su, vv - apV1 + 24.0)));
      }
      // gates in front of the terminal (international)
      if (big) {
        float n = max(2.0, floor(termHL*2.0/54.0)), span = n*54.0, gu = u - (termU - span*0.5);
        if (gu > 0.0 && gu < span) {
          float gs = mod(gu, 54.0) - 27.0;
          if (abs(gs) < 0.15 && vv > bldV - 70.0 && vv < bldV - 7.0) paint = 1.0;
          if (abs(vv - (bldV - 7.5)) < 0.15 && abs(gs) < 2.5) paint = 1.0;
          if (abs(abs(gs) - 27.0) < 0.12 && vv > bldV - 60.0) { paint = 1.0; pc = vec3(0.7, 0.08, 0.06); }   // stand boundaries
        }
      }
      // GA tie-down rows
      if (u > tieU0 && u < tieU1) {
        float tu = mod(u - tieU0 - 7.0, 14.0) - 7.0;
        float r0 = vv - (apV0 + 24.0), r1 = vv - (apV0 + 46.0);
        if ((abs(r0) < 0.1 || abs(r1) < 0.1) && abs(tu) < 4.5) paint = 1.0;
        if (abs(tu) < 0.1 && (abs(r0) < 3.0 || abs(r1) < 3.0)) paint = 1.0;
      }
      // taxilane along the apron front, red equipment line in front of the buildings
      if (abs(vv - (apV0 + 8.0)) < 0.15) paint = 1.0;
      if (abs(vv - (bldV - 4.0)) < 0.1) { paint = 1.0; pc = vec3(0.7, 0.08, 0.06); }
      // floodlight pools from the masts along the back of the apron (airport_scenery.cpp: every 75 / 90 m, heads ~19 m up)
      if (uRwyLights > 0.01) {
        float stp = big ? 90.0 : 75.0, u0 = apU0 + 30.0, hh = big ? 23.0 : 16.5;
        float k = clamp(floor((u - u0)/stp + 0.5), 0.0, floor((apU1 - u0)/stp));
        vec3 E = vec3(0.0);
        for (int j = -1; j <= 1; j++) {
          float mu = u0 + (k + float(j))*stp;
          if (mu < u0 - 1.0 || mu > apU1 || (big && abs(mu - termU) < termHL + 12.0)) continue;
          float du = u - mu, dv = vv - (apV1 - 1.0), d2 = du*du + dv*dv + hh*hh;
          E += vec3(hh/(d2*sqrt(d2)))*(dv < 0.0 ? 1.0 : 0.3);   // aimed out over the apron
        }
        m.emit += m.alb*vec3(1.0, 0.86, 0.62)*E*uRwyLights*900.0;
      }
    } else {
      // parallel taxiway and exits: centreline, edge lines, holding position markings on the exits
      if (onTw && abs(vv - twV) < 0.15) paint = 1.0;
      if (onTw && abs(abs(vv - twV) - (twHW - 0.4)) < 0.1 && !(onEx && vv < twV) && !(onAp)) paint = 1.0;
      if (onEx) {
        if (abs(ce) < 0.15) paint = 1.0;
        if (abs(abs(ce) - (twHW - 0.4)) < 0.1 && vv < twV - twHW) paint = 1.0;
        float hp = vv - (hw + 26.0);
        if (abs(ce) < twHW && ((abs(hp) < 0.15 || abs(hp - 0.45) < 0.15) || ((abs(hp - 1.05) < 0.15 || abs(hp - 1.5) < 0.15) && fract(ce/1.8) < 0.5))) paint = 1.0;
      }
      // dark shoulders
      if ((onTw && abs(vv - twV) > twHW) || (onEx && abs(ce) > twHW)) m.alb *= 0.75;
    }
    m.alb = mix(m.alb, pc, paint*0.9); m.rough = mix(m.rough, 0.6, paint);
    return true;
  }
  // ---- strips: parking area beside the strip, a worn track to it
  float pu = -len*0.18;
  float apV0 = hw + 16.0, apV1 = hw + 70.0;
  bool inPark = vv > apV0 && vv < apV1 + 4.0 && abs(u - pu) < 60.0;
  bool track = vv > hw && vv < apV0 + 1.0 && abs(u - pu + 20.0) < 5.0;
  if (!(inPark || track)) return false;
  float edge = smoothstep(0.0, 4.0, min(min(vv - apV0, apV1 + 4.0 - vv), 60.0 - abs(u - pu)));
  if (surf == 0) {   // small paved field: asphalt pad with tie-down lines
    vec4 t = matSample(pw.xz, M_ASPHALT, 7.0, nTS); m.alb = t.rgb; m.rough = t.a; m.nrm = nTS;
    float tu = mod(u - pu + 60.0, 15.0) - 7.5;
    if (inPark && ((abs(tu) < 0.1 && abs(vv - apV0 - 26.0) < 3.0) || abs(vv - apV0 - 26.0) < 0.1)) m.alb = mix(m.alb, yel, 0.9);
    return true;
  }
  if (surf == 1) {   // grass: a mown parking area, bare wheel tracks
    vec4 t = matSample(pw.xz, M_GRASS, 5.0, nTS);
    vec3 g = t.rgb*vec3(1.08, 1.15, 0.82)*(0.88 + 0.12*step(0.5, fract((u - pu)/5.0)));
    float worn = track ? smoothstep(4.0, 1.0, abs(abs(u - pu + 20.0) - 1.4))*0.7 : smoothstep(0.6, 0.85, vnoise(pw.xz*0.15))*0.35;
    m.alb = mix(m.alb, mix(g, vec3(0.33, 0.28, 0.18), worn), track ? 1.0 : edge); m.nrm = mix(m.nrm, nTS, edge);
    return true;
  }
  int layer = surf == 2 ? M_GRAVEL : surf == 3 ? M_SNOW : M_SAND;
  vec4 t = matSample(pw.xz, layer, 5.0, nTS);
  float w = track ? smoothstep(5.0, 3.0, abs(u - pu + 20.0)) : edge;
  m.alb = mix(m.alb, t.rgb*(surf == 3 ? 0.92 : 1.0), w); m.rough = mix(m.rough, t.a, w); m.nrm = mix(m.nrm, nTS, w);
  return true;
}

void runwayMaterial(int ai, vec2 uv, inout Mat m, vec3 pw, out bool onRw, out bool paved){
  vec4 d = uApDim[ai]; float len = d.x, wid = d.y; int surf = int(d.z); int size = int(d.w);
  onRw = false; paved = false;
  float u = uv.x, v = uv.y;
  vec3 nTS;
  float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
  float off = wid*0.5 + (size == 2 ? 170.0 : 85.0);
  int padLayer = size == 2 ? M_CONCRETE : M_ASPHALT;
  if (abs(v) > wid*0.5 + 3.0 && aptGround(ai, uv, pw, surf, size, len, wid, m)) { paved = surf == 0; return; }
  if (abs(u) > len*0.5 + 6.0 || abs(v) > wid*0.5 + 3.0) {
    if (abs(u) < len*0.5 + 60.0 && abs(v) < wid*0.5 + 7.5 && surf == 0) {
      // paved blast pad / shoulders with yellow chevrons
      vec4 t = matSample(pw.xz, M_ASPHALT, 7.0, nTS);
      m.alb = t.rgb*0.9; m.rough = t.a; m.nrm = nTS; paved = true;
      float bu = abs(u) - len*0.5;
      if (bu > 6.0 && fract((bu + abs(v)*1.2)/14.0) < 0.12 && abs(v) < wid*0.5) m.alb = vec3(0.6,0.48,0.05);
      return;
    }
    if (abs(u) < len*0.5 + 120.0 && abs(v) < wid*0.5 + 60.0 && (surf == 0 || surf == 1)) {
      vec4 t = matSample(pw.xz, M_GRASS, 5.0, nTS);
      float stripe = step(0.5, fract(u/18.0));
      m.alb = t.rgb*(0.85 + 0.15*stripe)*vec3(0.95,1.05,0.9); m.rough = 0.9; m.nrm = nTS;
      if (surf != 0) m.alb *= vec3(0.78, 0.82, 0.7);   // longer, darker rough beside an unpaved strip: the strip stands out
    }
    return;
  }
  onRw = true; paved = surf == 0;
  int layer = surf == 0 ? padLayer : surf == 1 ? M_GRASS : surf == 2 ? M_GRAVEL : surf == 3 ? M_SNOW : M_SAND;
  vec4 t = matSample(pw.xz, layer, surf == 0 ? 7.0 : 5.0, nTS);
  m.alb = t.rgb; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
  if (surf == 1) {   // mown strip: short, lighter, yellower grass in lengthwise mowing bands, worn wheel tracks
    m.alb *= vec3(1.12, 1.18, 0.82) * (0.84 + 0.16*step(0.5, fract((v + wid*0.5)/4.5)));
    float track = smoothstep(1.4, 0.4, abs(abs(v) - 1.3))*(0.7 + 0.3*vnoise(vec2(u*0.08, v)));
    m.alb = mix(m.alb, vec3(0.32, 0.27, 0.17), track*0.55);
    float worn = smoothstep(len*0.5 - 40.0, len*0.5 - 160.0, abs(u))*smoothstep(len*0.5 - 260.0, len*0.5 - 120.0, abs(u));
    m.alb = mix(m.alb, vec3(0.36, 0.3, 0.2), worn*smoothstep(0.55, 0.8, vnoise(pw.xz*0.25))*0.6);   // bare patches at touchdown
  }
  if (surf >= 2) { float rut = smoothstep(1.5, 0.3, abs(abs(v) - 2.2)); m.alb *= 1.0 - 0.18*rut; }
  if (surf != 0) {   // white edge boards every 50 m and a row of them across each threshold, as on real unpaved strips
    float au = abs(u), hl = len*0.5;
    float eb = abs(fract((u + hl)/50.0 + 0.5) - 0.5)*50.0;
    bool board = (eb < 0.9 && abs(abs(v) - wid*0.5) < 0.35)
              || (au > hl - 4.0 && au < hl - 2.6 && abs(fract(v/4.0 + 0.5) - 0.5)*4.0 < 0.9);
    if (board) { m.alb = vec3(0.85); m.rough = 0.6; m.nrm = vec3(0,0,1); }
  }
  if (surf == 0) {
    if (size == 2) { vec2 sj = vec2(abs(fract(u/7.5) - 0.5), abs(fract(v/7.5) - 0.5)); if (max(sj.x, sj.y) > 0.49) m.alb *= 0.78; }
    else { float gr = smoothstep(0.42, 0.5, abs(fract(u/0.04) - 0.5)); m.nrm.y += gr*0.2; }
    float tz = smoothstep(len*0.5 - 80.0, len*0.5 - 200.0, abs(u)) * smoothstep(len*0.5 - 650.0, len*0.5 - 300.0, abs(u));
    float tyre = tz * smoothstep(wid*0.3, 0.0, abs(abs(v) - 3.5)) * (0.5 + 0.5*vnoise(vec2(u*0.05, v*2.0)));
    m.alb *= 1.0 - 0.55*tyre;
    m.rough = mix(m.rough, 0.45, tyre);
    float paint = 0.0;
    float au = abs(u), hl = len*0.5;
    if (abs(v) < 0.45 && fract(u/50.0) < 0.6 && au < hl - 70.0) paint = 1.0;
    if (abs(abs(v) - (wid*0.5 - 1.0)) < 0.45) paint = 1.0;
    if (au > hl - 50.0 && au < hl - 12.0 && abs(v) < wid*0.5 - 3.0 && fract((v + wid*0.5)/3.6) < 0.5) paint = 1.0;
    if (au > hl - 380.0 && au < hl - 320.0 && abs(abs(v) - wid*0.25) < 2.5) paint = 1.0;
    if (au > hl - 300.0 && au < hl - 150.0 && fract(au/75.0) < 0.3 && abs(abs(v) - wid*0.22) < 2.5 && wid > 25.0) paint = 1.0;
    // runway designators, readable from the approach end
    float hdg = uAp[ai].w*57.29578;
    int n0 = int(floor(mod(hdg, 360.0)/10.0 + 0.5)); if (n0 == 0) n0 = 36;
    int n1 = int(floor(mod(hdg + 180.0, 360.0)/10.0 + 0.5)); if (n1 == 0) n1 = 36;
    if (u < -hl + 100.0 && u > -hl + 55.0) paint = max(paint, rwyDigits(vec2(v, u - (-hl + 78.0)), n0));
    if (u > hl - 100.0 && u < hl - 55.0) paint = max(paint, rwyDigits(vec2(-v, -(u - (hl - 78.0))), n1));
    m.alb = mix(m.alb, vec3(0.86), paint*0.92);
    m.rough = mix(m.rough, 0.55, paint);
  }
  // pools of light from the edge and threshold lights (lamps 0.35 m up on their stalks: E = I h / d^3)
  if (uRwyLights > 0.01) {
    float hl = 0.35;
    float ku = clamp(floor((u + len*0.5)/60.0 + 0.5), 0.0, floor(len/60.0));
    float du = u - (-len*0.5 + ku*60.0), dv = abs(v) - (wid*0.5 + 1.5);
    float d2 = du*du + dv*dv + hl*hl;
    vec3 lc = abs(-len*0.5 + ku*60.0) > len*0.5 - 600.0 && size > 0 ? vec3(1.0, 0.7, 0.25) : vec3(1.0, 0.93, 0.78);
    vec3 E = lc*hl/(d2*sqrt(d2));
    float tu = abs(u) - (len*0.5 + 1.0), tv = v - clamp(floor(v/3.0 + 0.5)*3.0, -wid*0.5, wid*0.5);
    float t2 = tu*tu + tv*tv + hl*hl;
    E += (u < 0.0 ? vec3(0.15, 1.0, 0.35) : vec3(1.0, 0.12, 0.08))*hl/(t2*sqrt(t2))*step(abs(v), wid*0.5 + 1.0);
    m.emit += m.alb*E*uRwyLights*6.0;
  }
}

// exact road distance using the baked nearest-segment ids
float roadDist(vec2 p, out float along){
  vec2 f = (p + WH)/MTEX - 0.5; ivec2 i = ivec2(floor(f));
  float best = 1e9; along = 0.0;
  for (int k = 0; k < 4; k++) {
    ivec2 o = ivec2(k & 1, k >> 1);
    int id = int(texelFetch(uRoadId, clamp(i + o, ivec2(0), ivec2(MASKN-1)), 0).r*255.0 + 0.5) - 1;
    if (id < 0) continue;
    vec4 s = dataAt(id);
    vec2 ab = s.zw - s.xy; float L2 = dot(ab, ab);
    float tt = clamp(dot(p - s.xy, ab)/max(L2, 1e-3), 0.0, 1.0);
    float dd = length(s.xy + ab*tt - p);
    if (dd < best) { best = dd; along = tt*sqrt(L2); }
  }
  return best;
}

// farmland: Voronoi field patchwork with crop rows and hedgerows
void fieldMaterial(vec2 p, float farm, inout Mat m){
  vec2 q = p/170.0 + vec2(vnoise(p/600.0), vnoise(p/600.0 + 7.3))*0.6;
  vec2 g = floor(q), f = fract(q);
  float d1 = 9.0, d2 = 9.0; vec2 id = vec2(0.0);
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec2 o = vec2(i, j); vec2 c = g + o;
    vec2 r = o + vec2(hash2i(ivec2(c) + ivec2(91, 7)), hash2i(ivec2(c) + ivec2(-3, 55))) - f;
    float d = dot(r, r);
    if (d < d1) { d2 = d1; d1 = d; id = c; } else if (d < d2) d2 = d;
  }
  float border = sqrt(d2) - sqrt(d1);
  float hsh = hash2i(ivec2(id) + ivec2(13, 31));
  float ang = hash2i(ivec2(id) + ivec2(-77, 4))*3.1416;
  float rows = sin(dot(p, vec2(cos(ang), sin(ang)))*2.0*3.1416/2.6);
  vec3 nTS; vec3 c; float rough = 0.9;
  if (hsh < 0.22) { vec4 t = matSample(p, M_CROP, 4.0, nTS); c = t.rgb*mix(0.8, 1.1, rows*0.5 + 0.5); }
  else if (hsh < 0.42) { vec4 t = matSample(p, M_WHEAT, 4.0, nTS); c = t.rgb*(0.92 + 0.08*rows); }
  else if (hsh < 0.56) { vec4 t = matSample(p, M_DIRT, 4.0, nTS); c = t.rgb*mix(0.75, 1.1, rows*0.5 + 0.5); rough = 0.97; }
  else if (hsh < 0.66) { c = mix(vec3(0.75, 0.68, 0.08), vec3(0.85, 0.78, 0.12), rows*0.5 + 0.5); nTS = vec3(0,0,1); }
  else if (hsh < 0.76) { vec4 t = matSample(p, M_DIRT, 4.0, nTS); vec4 v2 = matSample(p, M_LEAVES, 3.0, nTS); c = mix(t.rgb*0.9, v2.rgb*vec3(0.6,0.9,0.5), smoothstep(0.2, 0.6, rows)); }
  else { vec4 t = matSample(p, M_GRASS, 5.0, nTS); c = t.rgb*vec3(0.85, 1.0, 0.7);
    float fl = step(0.985, hash2i(ivec2(floor(p*1.5)))); c = mix(c, hsh > 0.9 ? vec3(0.9, 0.2, 0.15) : vec3(0.95, 0.9, 0.95), fl*0.8); }
  float hedge = smoothstep(0.035, 0.012, border);
  c = mix(c, vec3(0.06, 0.14, 0.04), hedge);
  m.alb = mix(m.alb, c, farm);
  m.rough = mix(m.rough, rough, farm);
  m.nrm = mix(m.nrm, nTS, farm*0.6);
}

Mat terrainMaterial(vec3 p, vec3 n, float t, vec4 base){
  Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
  if ((uDbg & 128) != 0) { m.alb = vec3(0.2, 0.3, 0.12); m.rough = 0.9; return m; }
  float lush = base.z, cold = base.w;
  float slope = 1.0 - n.y;
  float hNoise = tfbm(p.xz/900.0);
  float n2 = tfbm(p.xz/180.0 + vec2(5.3, 2.9));
  vec3 nTS;
  vec4 msk = maskAt(p.xz);
  // ---- natural ground layers
  float snowLine = mix(1700.0, 350.0, cold) + (hNoise - 0.5)*300.0;
  float wSnow = smoothstep(snowLine - 60.0, snowLine + 60.0, p.y) * smoothstep(0.55, 0.3, slope);
  wSnow = max(wSnow, uSnow*smoothstep(0.6, 0.35, slope)*step(1.0, p.y));
  float wRock = smoothstep(0.30, 0.54, slope + (n2-0.5)*0.18);
  float wSand = smoothstep(4.5 + 3.0*hNoise, 1.0, p.y) * (1.0 - wRock);
  float forestN = forestAt(p.xz);
  float wForest = smoothstep(0.42 - lush*0.1, 0.5 - lush*0.1, forestN) * smoothstep(0.35, 0.2, slope) * smoothstep(4.0, 9.0, p.y) * smoothstep(1500.0 - cold*900.0, 1100.0 - cold*700.0, p.y);
  wForest *= smoothstep(2.0, 3.0, base.y);   // airport grounds are cleared (no trees there either: amp < 2.5)
  float wDirt = smoothstep(0.55, 0.7, n2) * (1.0 - wForest) * 0.6;
  float hC, hL;
  vec4 gr = groundSample(p.xz, M_GRASS, 6.0, nTS, hC); vec3 nG = nTS;
  // grass colour at several scales: lush meadow, olive and dry grass by moisture (low noise fields, drier on slopes and
  // up high), with mown / grazed patches and streaks; keeps distant hills from reading as one flat green
  float dry = (tfbm(p.xz/2600.0 + 4.7) - 0.5)*1.6 + (vnoise(p.xz/420.0) - 0.5)*0.7 + (vnoise(p.xz/130.0) - 0.5)*0.45 + slope*1.6 - lush*0.55 + 0.42
            + smoothstep(250.0, 900.0, p.y)*0.25;
  vec3 grassTint = mix(vec3(0.6, 0.8, 0.42), vec3(0.78, 0.78, 0.44), smoothstep(0.25, 0.65, dry));
  grassTint = mix(grassTint, vec3(0.98, 0.84, 0.52), smoothstep(0.65, 1.0, dry)*0.8);
  grassTint = mix(grassTint, vec3(0.75,0.8,0.65), cold*0.6);
  grassTint *= 0.84 + 0.22*vnoise(p.xz/90.0) + 0.1*vnoise(p.xz/23.0);   // patchy brightness breaks up repetition
  grassTint *= 1.0 - 0.18*smoothstep(0.08, 0.3, slope);                   // steeper ground: coarser, shadowed tufts
  vec3 grB = mix(vec3(dot(gr.rgb, vec3(0.2126, 0.7152, 0.0722))), gr.rgb, 0.68);   // real grass is far less saturated than the raw texture
  m.alb = grB*grassTint*1.05; m.rough = gr.a; m.nrm = nG;
  // wildflower and dry patches
  // round flower heads up close; further out only the patch's tint survives (no aliasing squares)
  vec2 fc = floor(p.xz*1.3), ff = fract(p.xz*1.3) - 0.5 - (vec2(hash2i(ivec2(fc) + ivec2(3, 1)), hash2i(ivec2(fc) + ivec2(-5, 7))) - 0.5)*0.5;
  float flPatch = smoothstep(0.62, 0.7, vnoise(p.xz/35.0));
  float fl = flPatch*mix(0.12, step(0.9, hash2i(ivec2(fc)))*smoothstep(0.2, 0.1, length(ff)), smoothstep(120.0, 40.0, t));
  float flNear = smoothstep(120.0, 40.0, t);   // far away a meadow in flower only warms slightly (no grey-violet patches)
  vec3 flCol = mix(vec3(0.95,0.85,0.2), mix(vec3(0.95,0.85,0.2), vec3(0.75,0.35,0.85), step(0.5, vnoise(p.xz/20.0))), flNear);
  m.alb = mix(m.alb, flCol*m.alb/max(dot(m.alb, vec3(0.333)), 1e-3)*mix(0.33, 1.0, flNear), fl*(1.0 - cold)*mix(0.35, 0.7, flNear));
  if (wDirt > 0.01) { vec4 d = groundSample(p.xz, M_DIRT, 7.0, nTS, hL); float w = hblend(wDirt, hC, hL);
    m.alb = mix(m.alb, d.rgb, w); m.rough = mix(m.rough, d.a, w); m.nrm = mix(m.nrm, nTS, w); hC = mix(hC, hL, w); }
  if (wForest > 0.01) {
    // forest floor under the trees: leaf litter, needles and moss
    vec4 f = groundSample(p.xz, M_LITTER, 4.0, nTS, hL); float w = hblend(wForest*0.85, hC, hL);
    vec3 litter = f.rgb*mix(vec3(1.0), vec3(0.8, 0.9, 0.85), cold)*0.6;   // shaded by the canopy (no bright speckle between the trees)
    m.alb = mix(m.alb, litter, w); m.rough = mix(m.rough, 0.95, w); m.nrm = mix(m.nrm, nTS, w); hC = mix(hC, hL, w);
    // beyond the tree draw distance the forest is the ground's own canopy texture, faded in as the trees thin out
    float rag = smoothstep(0.25, 0.75, wForest + (vnoise(p.xz/38.0) - 0.5)*0.9);   // ragged canopy margins
    float far = smoothstep(uTreeFar*0.45, uTreeFar*0.95, t)*rag;
    if (far > 0.01) { vec4 cn = groundSample(p.xz, M_FOREST, 26.0, nTS, hL);
      vec3 tint = mix(vec3(0.6, 0.78, 0.46), vec3(0.5, 0.64, 0.5), max(cold, smoothstep(500.0, 900.0, p.y)));
      tint *= 0.85 + 0.3*vnoise(p.xz/160.0);   // stands of different age and species
      m.alb = mix(m.alb, cn.rgb*tint*0.8, far); m.rough = mix(m.rough, 0.9, far); m.nrm = mix(m.nrm, nTS, far); }
  }
  if (msk.w > 0.05 && p.y > 0.5) fieldMaterial(p.xz, msk.w*(1.0 - wRock), m);
  if (wSand > 0.01) { vec4 s = groundSample(p.xz, M_SAND, 6.0, nTS, hL); float w = hblend(wSand, hC, 1.0 - hL);
    m.alb = mix(m.alb, s.rgb*mix(vec3(1.0), vec3(1.08,1.04,0.95), lush), w); m.rough = mix(m.rough, s.a, w); m.nrm = mix(m.nrm, nTS, w); hC = mix(hC, hL, w); }
  if (wRock > 0.01) {
    vec3 nr; vec4 r = triSample(p, n, M_ROCK, 18.0, nr);
    float w = hblend(wRock, hC, dot(r.rgb, vec3(0.6))*1.6);
    vec3 rockTint = mix(vec3(1.0,0.95,0.88), vec3(0.75,0.72,0.72), cold);
    if (base.y > 300.0 && lush > 0.9) rockTint = vec3(0.55,0.5,0.5);
    m.alb = mix(m.alb, r.rgb*rockTint, w); m.rough = mix(m.rough, r.a, w); m.nrm = mix(m.nrm, nr, w); hC = mix(hC, 0.8, w);
  }
  if (wSnow > 0.01) { vec4 s = groundSample(p.xz, M_SNOW, 8.0, nTS, hL); float w = hblend(wSnow, hC, 1.0 - hC);   // snow fills hollows first
    m.alb = mix(m.alb, s.rgb, w); m.rough = mix(m.rough, s.a, w); m.nrm = mix(m.nrm, nTS, w); }
  // ---- towns: streets, pavements, gardens and plazas
  if (msk.y > 0.04 && p.y > 1.0) {
    vec2 lf = fract(p.xz/28.0); vec2 e = abs(lf - 0.5)*28.0;
    // streets run along every 3rd lot boundary in x and every 2nd in z; other boundaries are garden hedges
    vec2 li = floor(p.xz/28.0 + 0.5);
    float sx = mod(li.x, 3.0) == 0.0 ? e.x : 0.0, sz = mod(li.y, 2.0) == 0.0 ? e.y : 0.0;
    float edge = max(sx, sz);
    if (max(e.x, e.y) > 13.6 && edge < 10.5) { m.alb = mix(m.alb, vec3(0.08, 0.16, 0.06), smoothstep(0.04, 0.15, msk.y)); }
    float townW = smoothstep(0.04, 0.15, msk.y);
    vec4 tx; vec3 c;
    if (edge > 10.5) { tx = matSample(p.xz, M_ASPHALT, 6.0, nTS); c = tx.rgb*0.9; if (abs(min(e.x, e.y) - 0.0) < 0.15 && edge > 12.0) c = vec3(0.7); }
    else if (edge > 9.0) { tx = matSample(p.xz, M_CONCRETE, 3.0, nTS); c = tx.rgb; }
    else if (msk.z > 0.45) { tx = matSample(p.xz, M_CONCRETE, 4.0, nTS); c = tx.rgb*0.95; }
    else { tx = matSample(p.xz, M_GRASS, 4.0, nTS); c = tx.rgb*vec3(0.8, 1.0, 0.65); }
    m.alb = mix(m.alb, c, townW); m.rough = mix(m.rough, tx.a, townW); m.nrm = mix(m.nrm, nTS, townW);
    // street lamps pools at night
    vec2 corner = vec2(sx, sz) - 13.0;
    m.emit += vec3(1.0, 0.75, 0.4)*smoothstep(8.0, 0.0, length(corner))*uNight*0.35*townW;
  }
  // ---- roads (exact geometry from the baked segment ids)
  if (msk.x < 0.25) {
    float along; float rd = roadDist(p.xz, along);
    if (rd < 6.0) {
      vec4 tx = matSample(p.xz, rd < 4.0 ? M_ASPHALT : M_GRAVEL, 6.0, nTS);
      float a = smoothstep(6.0, 4.6, rd);
      vec3 c = tx.rgb*(rd < 4.0 ? 0.85 : 1.0);
      if (abs(rd - 3.55) < 0.12) c = vec3(0.75);
      if (rd < 0.11 && fract(along/12.0) < 0.5) c = vec3(0.85, 0.75, 0.3);
      m.alb = mix(m.alb, c, a); m.rough = mix(m.rough, tx.a, a); m.nrm = mix(m.nrm, nTS, a);
    }
  }
  // ---- airport surfaces
  vec2 auv; int ai = airportAt(p.xz, auv);
  if (ai >= 0) { bool onRw, paved; runwayMaterial(ai, auv, m, p, onRw, paved); }
  // ---- impact crater: churned earth, scorched blast ring, smouldering embers in the pit
  int ci = uCraterN > 0 ? craterAt(p.xz, 2.6) : -1;
  if (ci >= 0) {
    vec4 cr = uCrater[ci]; float cd = length(p.xz - cr.xy)/cr.z;
    if (cr.w < 0.0) {   // dark-energy crater: fused black glass, violet embers glowing in the cracks
      float nz = vnoise(p.xz*0.5) + 0.5*vnoise(p.xz*2.3);
      float glass = smoothstep(1.3, 0.6, cd*(0.85 + 0.3*nz)), scorch = smoothstep(2.6, 1.0, cd*(0.8 + 0.35*nz));
      m.alb = mix(m.alb, vec3(0.012, 0.01, 0.016), max(scorch*0.9, glass));
      m.rough = mix(m.rough, 0.08, glass); m.metal = mix(m.metal, 0.3, glass); m.nrm = mix(m.nrm, vec3(0,0,1), glass*0.8);
      float crack = pow(clamp(1.0 - abs(vnoise(p.xz*1.3 + 7.0) - 0.5)*7.0, 0.0, 1.0), 4.0);
      m.emit += vec3(0.6, 0.2, 1.0)*crack*glass*(1.5 + sin(uTime*2.0 + nz*6.0));
    } else if (cd < 2.6) {
      vec2 cq = (p.xz - cr.xy)/max(cr.z, 1.0);   // noise in crater units: small pits get ragged edges too
      float nz = vnoise(cq*3.5 + cr.xy*0.1) + 0.5*vnoise(cq*13.0);
      float scorch = smoothstep(2.6, 1.1, cd*(0.8 + 0.35*nz));
      vec3 dirt = mix(vec3(0.16, 0.12, 0.09), vec3(0.08, 0.065, 0.05), nz*0.7);
      m.alb = mix(m.alb, dirt, smoothstep(1.25, 0.9, cd));
      m.alb = mix(m.alb, vec3(0.02, 0.018, 0.016), scorch*0.85);
      m.rough = mix(m.rough, 0.97, scorch); m.nrm = mix(m.nrm, vec3(0,0,1), scorch*0.5);
      m.emit += vec3(1.0, 0.3, 0.05)*pow(clamp(vnoise(p.xz*2.5 + uTime*0.15)*smoothstep(0.55, 0.1, cd), 0.0, 1.0), 8.0)*2.0;
    }
  }
  // wet look in rain
  m.alb *= 1.0 - 0.35*uWet*(1.0-wSnow);
  m.rough = mix(m.rough, m.rough*0.35, uWet*(1.0-wSnow));
  return m;
}

vec3 applyTS(vec3 n, vec3 nTS, float strength){
  vec3 t = normalize(cross(n, abs(n.z) < 0.9 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0)));
  vec3 b = cross(t, n);
  return normalize(n + (t*nTS.x + b*nTS.y)*strength);
}

vec3 shadeSurface(vec3 p, vec3 n, vec3 rd, Mat m, float shadow){
  vec3 v = -rd;
  vec3 col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*shadow*3.2);
  col += m.alb*ambientLight(n)*(0.55 + 0.45*n.y);
  // moonlight
  vec3 md = normalize(vec3(-0.4, 0.55, 0.6));
  col += pbr(n, v, md, m.alb, m.rough, m.metal, vec3(0.05,0.07,0.12)*uNight);
  // specular environment reflection
  vec3 r = reflect(rd, n);
  vec3 F = fresnelSchlick(max(dot(n, v), 0.0), mix(vec3(0.04), m.alb, m.metal));
  col += skyColor(normalize(vec3(r.x, abs(r.y), r.z)))*F*(1.0-m.rough)*(1.0-m.rough)*0.8;
  // point and spot lights (exhaust flames, landing lights, nav lights, beacon, strobes, blasts) with ray-traced
  // shadows from the aircraft: shadow rays only where a light contributes visibly
  for (int i = 0; i < 12; i++) {
    if (i >= uPLN || (uDbg & 16) != 0) break;
    COST(3);
    vec3 lv = uPLP[i].xyz - p; float d2 = dot(lv, lv), d = sqrt(d2); vec3 l = lv/max(d, 1e-4);
    float ndl = dot(n, l);
    if (ndl <= 0.0) continue;
    vec3 E = uPLC[i].rgb/(d2 + uPLP[i].w*uPLP[i].w);
    if (uPLC[i].w > -1.5) E *= smoothstep(uPLC[i].w, mix(uPLC[i].w, 1.0, 0.3), dot(-l, uPLD[i].xyz));
    float lum = max(E.r, max(E.g, E.b))*ndl;
    if (lum < 0.0015) continue;
    if (uPLD[i].w > 0.0 && lum > 0.004) {
      gShMax = d - uPLD[i].w; gShK = clamp(d/max(uPLP[i].w, 0.02), 6.0, 80.0);
      E *= planeShadow(p + n*0.03, l);
      gShMax = 1e9; gShK = 10.0;
    }
    col += pbr(n, v, l, m.alb, m.rough, m.metal, E);
  }
  col += m.emit;
  col += m.alb*vec3(0.7,0.75,1.0)*uLightning*0.4;
  return col;
}

// optical depth along a ray through an exponential layer of scale height H: integral of exp(-y/H) over the path
float layerDepth(float y0, float dy, float t, float H){
  float a = exp(-max(y0, 0.0)/H), k = dy*t/H;
  return abs(k) > 1e-3 ? a*H*(1.0 - exp(-k))/dy : a*t;
}
// Aerial perspective: Rayleigh scattering (blue light scatters most, so distance turns hills blue and drains their
// contrast) plus a low haze layer whose density follows the weather's visibility and that glows around the sun.
// Both thin out with altitude. The in-scattered light is the horizon sky's, so far terrain melts into the sky.
vec3 applyFog(vec3 col, vec3 ro, vec3 rd, float t){
  if ((uDbg & 256) != 0) return col;
  float odR = layerDepth(ro.y, rd.y, t, 8000.0), odM = layerDepth(ro.y, rd.y, t, 1100.0);
  vec3 bR = vec3(5.8e-6, 13.5e-6, 33.1e-6);
  float bM = 3e-6 + uFogB*0.8;
  vec3 tau = bR*odR + vec3(bM*odM) + uFogB*0.03*t;
  vec3 T = exp(-tau);
  float mu = dot(rd, uSunDir), mp = max(mu, 0.0);
  vec3 fogCol = skyColor(normalize(vec3(rd.x, 0.06, rd.z)))*vec3(0.9, 0.94, 1.0);
  // forward scattering by the haze: a broad warm glow towards the sun, stronger the hazier the air
  float hazeW = clamp(bM*odM/max(dot(tau, vec3(0.333)), 1e-6), 0.0, 1.0);
  fogCol += uSunCol*(pow(mp, 8.0)*0.22 + pow(mp, 2.5)*0.07*hazeW);
  return col*T + fogCol*(1.0 - T);
}

