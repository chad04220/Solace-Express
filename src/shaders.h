// Air Xpress - GLSL shaders (GPU ray tracer, overlay sprites, post-processing, UI)
#pragma once

static const char* kFullscreenVS = R"(#version 330 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }
)";

// ------------------------------------------------------------------------------------------------
// Shared noise + terrain (must match world.cpp exactly)
static const char* kCommonGLSL = R"(
const float WH = 40000.0;
const int HMN = 1024;
const float TEXEL = 78.125;
const float PI = 3.14159265;
uniform sampler2D uHM;
float hash2i(ivec2 p){ uint h = uint(p.x)*0x8da6b343u + uint(p.y)*0xd8163841u; h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15; return float(h & 0xFFFFFFu)/16777216.0; }
float hash1(float n){ return fract(sin(n)*43758.5453); }
float hash3(vec3 p){ p = fract(p*0.3183099+0.1); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
vec4 baseAt(vec2 p){
  vec2 f = (p + WH)/TEXEL - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl;
  ivec2 mx = ivec2(HMN-1);
  vec4 a = texelFetch(uHM, clamp(i, ivec2(0), mx), 0), b = texelFetch(uHM, clamp(i+ivec2(1,0), ivec2(0), mx), 0);
  vec4 c = texelFetch(uHM, clamp(i+ivec2(0,1), ivec2(0), mx), 0), d = texelFetch(uHM, clamp(i+ivec2(1,1), ivec2(0), mx), 0);
  return (a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y;
}
vec3 noised(vec2 x){
  vec2 f0 = floor(x); ivec2 i = ivec2(f0); vec2 f = x - f0;
  vec2 u = f*f*f*(f*(f*6.0-15.0)+10.0); vec2 du = 30.0*f*f*(f*(f-2.0)+1.0);
  float a = hash2i(i), b = hash2i(i+ivec2(1,0)), c = hash2i(i+ivec2(0,1)), d = hash2i(i+ivec2(1,1));
  float k1 = b-a, k2 = c-a, k4 = a-b-c+d;
  return vec3(-1.0+2.0*(a+k1*u.x+k2*u.y+k4*u.x*u.y), 2.0*du*vec2(k1+k4*u.y, k2+k4*u.x));
}
float terrainFbm(vec2 p, int oct){
  float a = 0.0, b = 1.0; vec2 d = vec2(0.0);
  for(int i=0;i<12;i++){ if(i>=oct) break; vec3 n = noised(p); d += n.yz; a += b*n.x/(1.0+dot(d,d)); b *= 0.5; p = vec2(1.6*p.x-1.2*p.y, 1.2*p.x+1.6*p.y); }
  return a;
}
float terrainH(vec2 p, int oct){ vec4 b = baseAt(p); return b.y < 0.01 ? b.x : b.x + b.y*terrainFbm(p/2200.0, oct); }
float vnoise(vec2 x){ vec2 i=floor(x), f=fract(x); f=f*f*(3.0-2.0*f);
  return mix(mix(hash2i(ivec2(i)),hash2i(ivec2(i)+ivec2(1,0)),f.x), mix(hash2i(ivec2(i)+ivec2(0,1)),hash2i(ivec2(i)+ivec2(1,1)),f.x), f.y); }
float vnoise3(vec3 x){ vec3 i=floor(x), f=fract(x); f=f*f*(3.0-2.0*f);
  return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x), mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
             mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x), mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y), f.z); }
float fbm2(vec2 p, int oct){ float s=0.0, a=0.5; for(int i=0;i<8;i++){ if(i>=oct) break; s+=a*vnoise(p); p=p*2.02+vec2(13.7,-7.1); a*=0.5; } return s; }

// Scene / atmosphere uniforms
uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uNight; uniform float uTime;
uniform float uCloudCover; uniform float uCloudBase; uniform float uFogB; uniform float uWet; uniform float uSnow;
uniform vec2 uWindOff; uniform float uLightning; uniform float uStorm;

vec3 skyColor(vec3 rd){
  vec3 sd = uSunDir;
  float sunH = sd.y;
  float y = max(rd.y, 0.0);
  // optical depths (cheap analytic approximation of Rayleigh + Mie single scattering)
  float odV = 1.0/(y*1.4 + 0.035);
  float odS = 1.0/(max(sunH, -0.08)*1.4 + 0.08);
  vec3 beta = vec3(0.10, 0.23, 0.56);
  vec3 sunExt = exp(-beta*odS*0.45);
  float mu = dot(rd, sd);
  float ray = 0.75*(1.0+mu*mu);
  float g = 0.76; float mie = (1.0-g*g)/(4.0*PI*pow(1.0+g*g-2.0*g*mu, 1.5));
  vec3 inscat = (beta*ray*1.6 + vec3(0.11)*mie*0.9) * sunExt * (1.0-exp(-beta*odV*0.3)) / beta;
  vec3 col = inscat * 2.3 * smoothstep(-0.12, 0.05, sunH);
  // golden-hour glow towards the sun and less magenta in the upper sky (cheap multiple-scattering / ozone hack)
  float low = smoothstep(0.35, 0.0, sunH) * smoothstep(-0.15, 0.0, sunH);
  float hz = pow(clamp(1.0 - y, 0.0, 1.0), 3.0);
  col *= mix(vec3(1.0), vec3(0.7, 0.85, 1.05), low * (1.0 - hz));
  col += vec3(1.0, 0.42, 0.12) * pow(max(mu, 0.0), 4.0) * hz * low * 1.4;
  col += vec3(0.9, 0.55, 0.3) * hz * low * 0.12;
  // twilight / night sky
  vec3 night = vec3(0.006, 0.011, 0.028) * (1.0 - 0.5*y) + vec3(0.02,0.012,0.03)*pow(clamp(1.0-y,0.0,1.0), 6.0)*smoothstep(-0.3, 0.0, sunH);
  col += night;
  // overcast flattening
  float oc = smoothstep(0.55, 1.0, uCloudCover);
  vec3 grey = vec3(0.55,0.58,0.62) * (0.012 + 0.988*smoothstep(-0.12, 0.4, sunH)) * (0.9 - 0.3*uStorm);
  col = mix(col, grey * (0.75 + 0.25*y), oc*0.85);
  // sun disc
  float sunDisc = smoothstep(0.99985, 0.99992, mu);
  col += sunExt * sunDisc * 60.0 * (1.0 - oc);
  // stars
  if (uNight > 0.01 && rd.y > 0.0) {
    vec3 sp = rd*300.0; vec3 ci = floor(sp);
    float st = hash3(ci); vec3 fp = fract(sp) - 0.5;
    float s = smoothstep(0.995, 1.0, st) * smoothstep(0.12, 0.0, length(fp)) * (1.0 - oc);
    col += vec3(0.8,0.85,1.0) * s * 2.0 * uNight * smoothstep(0.0, 0.2, rd.y);
    // moon
    vec3 md = normalize(vec3(-0.4, 0.55, 0.6));
    col += vec3(0.9,0.92,1.0) * smoothstep(0.9993, 0.9996, dot(rd, md)) * 3.0 * uNight * (1.0-oc);
  }
  col += vec3(0.7,0.75,1.0) * uLightning * 0.8 * oc;
  return col;
}
)";

// ------------------------------------------------------------------------------------------------
static const char* kRaytraceFS = R"(
in vec2 vUV;
layout(location=0) out vec4 oColor;
layout(location=1) out float oDepth;
uniform vec2 uRes; uniform vec3 uCamPos; uniform mat3 uCamRot; uniform float uTanHalf; uniform float uAspect;
uniform float uMaxH; uniform int uQuality;
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
// airports
uniform int uApCount; uniform vec4 uAp[16]; uniform vec4 uApDim[16];
uniform int uBoxCount; uniform vec4 uBoxC[64]; uniform vec4 uBoxH[64];
// aircraft
uniform int uPlaneOn; uniform vec3 uPlanePos; uniform mat3 uPlaneRot;
uniform vec4 uM[24]; uniform vec4 uPS; uniform vec4 uCtl; uniform vec4 uPr; uniform vec4 uI0; uniform vec4 uI1; uniform vec4 uI2;
uniform vec3 uColBase; uniform vec3 uColStripe;
uniform vec4 uProp[2]; uniform int uPropCount;
uniform vec3 uLandLightPos; uniform vec3 uLandLightDir; uniform float uLandLight;

// ---------------------------------------------------------------- materials (texture array layers)
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;

float sdBox(vec3 p, vec3 b){ vec3 q = abs(p)-b; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0); }
float sdRoundBox(vec3 p, vec3 b, float r){ vec3 q = abs(p)-b+r; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r; }
float sdCapsule(vec3 p, vec3 a, vec3 b, float r){ vec3 pa=p-a, ba=b-a; float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h)-r; }
float sdRoundCone(vec3 p, vec3 a, vec3 b, float r1, float r2){
  vec3 ba = b - a; float l2 = dot(ba,ba); float rr = r1 - r2; float a2 = l2 - rr*rr; float il2 = 1.0/l2;
  vec3 pa = p - a; float y = dot(pa,ba); float z = y - l2; vec3 xv = pa*l2 - ba*y; float x2 = dot(xv,xv);
  float y2 = y*y*l2; float z2 = z*z*l2; float k = sign(rr)*rr*rr*x2;
  if( sign(z)*a2*z2 > k ) return sqrt(x2 + z2)*il2 - r2;
  if( sign(y)*a2*y2 < k ) return sqrt(x2 + y2)*il2 - r1;
  return (sqrt(x2*a2*il2)+y*rr)*il2 - r1;
}
float sdCylX(vec3 p, float r, float h){ vec2 d = abs(vec2(length(p.yz), p.x)) - vec2(r,h); return min(max(d.x,d.y),0.0) + length(max(d,0.0)); }
mat2 rot(float a){ float c=cos(a), s=sin(a); return mat2(c,-s,s,c); }

// ---------------------------------------------------------------- aircraft (body frame: +x right, +y up, +z aft)
// uM[] layout is written by packModel() in models.cpp.
// Rotation helper: rot2(v, a) rotates v counter-clockwise by a. To rotate GEOMETRY by A we query with rot2(p, -A).
vec2 rot2(vec2 v, float a){ float c = cos(a), s = sin(a); return vec2(c*v.x - s*v.y, s*v.x + c*v.y); }
float smin(float a, float b, float k){ float h = clamp(0.5 + 0.5*(b - a)/k, 0.0, 1.0); return mix(b, a, h) - k*h*(1.0 - h); }
vec2 opU(vec2 a, vec2 b){ return a.x < b.x ? a : b; }
float sdUnevenCapsule2(vec2 p, float r1, float r2, float h){
  p.x = abs(p.x); float b = (r1 - r2)/h; float a = sqrt(max(1.0 - b*b, 1e-4)); float k = dot(p, vec2(-b, a));
  if (k < 0.0) return length(p) - r1;
  if (k > a*h) return length(p - vec2(0.0, h)) - r2;
  return dot(p, vec2(a, b)) - r1;
}
float sdEllipsoid(vec3 p, vec3 r){ float k0 = length(p/r); float k1 = length(p/(r*r)); return k0*(k0 - 1.0)/max(k1, 1e-5); }
float sdRoundCylX(vec3 p, float r, float h, float rr){ vec2 d = vec2(length(p.yz) - r + rr, abs(p.x) - h + rr); return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rr; }

// fuselage cross-section (half width, half height, centre y) at body z
vec3 fusSection(float z){
  z = clamp(z, uM[1].x, uM[8].x);
  vec4 a = uM[1], b = uM[2];
  for (int i = 1; i < 8; i++) { a = uM[i]; b = uM[i+1]; if (z <= b.x) break; }
  float t = clamp((z - a.x)/max(b.x - a.x, 1e-3), 0.0, 1.0); t = t*t*(3.0 - 2.0*t);
  return mix(a.yzw, b.yzw, t);
}
float sdFuselage(vec3 p){
  vec3 sec = fusSection(p.z);
  vec2 q = vec2(p.x, p.y - sec.z);
  float rnd = uM[15].z;
  float m = min(sec.x, sec.y);
  float dEll = (length(q/sec.xy) - 1.0)*m;
  float r = m*mix(0.3, 1.0, rnd);
  vec2 d = abs(q) - sec.xy + r;
  float dRR = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
  float d2 = mix(dRR, dEll, rnd);
  float dz = max(uM[1].x - p.z, p.z - uM[8].x);
  return dz > 0.0 ? length(vec2(max(d2, 0.0), dz)) : d2;
}

// Tapered airfoil panel. s = spanwise (>= 0 from root), c = chordwise from root LE (aft +), t = thickness axis.
// cutC: chordwise position (relative to local LE, as chord fraction) behind which the panel is trimmed when s in [cut0, cut1].
float sdPanel(float s, float c, float t, float span, float rc, float tc, float sweep, float th, float cutF, float cut0, float cut1){
  float k = clamp(s/span, 0.0, 1.0);
  float ch = mix(rc, tc, k); float le = sweep*k;
  float r1 = th*ch*0.5, r2 = max(0.004*ch, 0.005);
  float d2 = sdUnevenCapsule2(vec2(t, c - le - r1), r1, r2, max(ch - r1 - r2, 0.01));
  if (s > cut0 && s < cut1) d2 = max(d2, c - le - ch*cutF);
  float ds = s - span;
  float d = length(max(vec2(d2, ds), 0.0)) + min(max(d2, ds), 0.0);
  return max(d, -s - 0.02);
}
// Hinged control surface behind the hinge line. defl = geometry rotation (radians) in the (t, c) plane.
float sdSurface(float s, float c, float t, float span, float rc, float tc, float sweep, float th, float hingeF, float s0, float s1, float defl, float slide){
  float k = clamp(s/span, 0.0, 1.0);
  float ch = mix(rc, tc, k); float le = sweep*k;
  vec2 q = vec2(t, c - (le + ch*hingeF + 0.008 + slide*ch));
  q = rot2(q, -defl);
  float len = ch*(1.0 - hingeF) - 0.01;
  float halfT = max(th*ch*0.5*(0.42 - 0.38*clamp(q.y/len, 0.0, 1.0)), 0.004);
  vec3 b = vec3(max(s0 - s, s - s1), abs(q.x) - halfT, max(-q.y, q.y - len));
  return length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0) - 0.003;
}

vec2 mapPlane(vec3 p){
  float L = uM[0].x; int gtype = int(uM[0].y + 0.5); int eng = int(uM[0].z + 0.5); float R = uM[0].w;
  float gear = uPS.x, flaps = uPS.y, steer = uPS.z, inside = uPS.w;
  float cPitch = uCtl.x, cRoll = uCtl.y, cYaw = uCtl.z, cThr = uCtl.w;
  // ---------------- fuselage (hollow with window openings in cockpit view)
  float f = sdFuselage(p);
  vec2 res = vec2(f, 1.0);
  if (inside > 0.5) {
    // hollow cabin with real window openings
    vec4 E = uM[22]; vec4 WS = uM[23]; vec3 sec = fusSection(p.z);
    float shell = abs(f + 0.03) - 0.03;
    float holeWs = sdBox(p - vec3(0.0, WS.z + 1.0, 0.5*(WS.x + WS.y)), vec3(sec.x*1.25, 1.0, 0.5*(WS.y - WS.x)));
    float post = uM[21].z > 1.5 ? min(abs(p.x) - 0.03, abs(abs(p.x) - abs(E.x) - 0.42) - 0.035) : abs(p.x) - 0.025;
    holeWs = max(holeWs, -post);
    float sideTop = sec.z + sec.y*0.78;
    float holeSide = sdBox(p - vec3(0.0, 0.5*(WS.z - 0.12 + sideTop), 0.5*(WS.y + WS.w)), vec3(5.0, 0.5*(sideTop - WS.z + 0.12), 0.5*(WS.w - WS.y)));
    holeSide = max(holeSide, 0.3 - abs(p.x));
    holeSide = max(holeSide, -(abs(p.z - WS.y - 0.04) - 0.025));
    shell = max(shell, -min(holeWs, holeSide));
    res = vec2(shell, 11.0);
  }
  // ---------------- main wing with flaps and ailerons
  {
    vec4 W0 = uM[9], W1 = uM[10], W2 = uM[11];
    float span = W0.x, rc = W0.y, tc = W0.z, sw = W0.w, th = W1.w;
    float s = abs(p.x);
    float t = p.y - (W1.x + s*W1.z);
    float c = p.z - W1.y;
    float sgn = p.x > 0.0 ? 1.0 : -1.0;
    float fus0 = 0.55*R, flapEnd = span*W2.w, ailEnd = span*0.94;
    float wing = sdPanel(s, c, t, span, rc, tc, sw, th, 0.74, fus0, ailEnd);
    float flap = sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, fus0, flapEnd, flaps*0.62, flaps*0.1);
    // right aileron TE goes UP for right roll; left goes down
    float ail = sdSurface(s, c, t, span, rc, tc, sw, th, 0.74, flapEnd + 0.03, ailEnd, -cRoll*sgn*0.33, 0.0);
    float wd = min(wing, min(flap, ail));
    if (W2.z > 0.01) {  // winglet
      float wl = sdPanel(t - 0.02, c - sw - tc*0.15, s - span + 0.05, W2.z, tc*0.85, tc*0.4, 0.55, 0.09, 1.0, 0.0, 0.0);
      wd = smin(wd, wl, 0.08);
    }
    if (uM[15].w > 0.5) {  // leading-edge slats (STOL)
      float sl = sdPanel(s, c + 0.09, t + 0.03, span*0.95, rc*0.16, tc*0.16, sw, 0.5, 1.0, 0.0, 0.0);
      wd = min(wd, max(sl, fus0 + 0.4 - s));
    }
    float fd = res.x;
    if (inside > 0.5) { wd = max(wd, -f); res.x = min(res.x, wd); }
    else res.x = smin(res.x, wd, 0.06*R);
    if (wd < fd) res.y = 2.0;
    // lift struts
    if (W2.x > 0.5) {
      vec3 sec = fusSection(p.z);
      float k = clamp(W2.y/span, 0.0, 1.0); float ch = mix(rc, tc, k); float le = sw*k;
      vec3 top1 = vec3(W2.y, W1.x + W2.y*W1.z - th*ch*0.4, W1.y + le + ch*0.25);
      vec3 base = vec3(0.6*R, -0.35*R, W1.y + rc*0.35);
      vec3 ap = vec3(abs(p.x), p.y, p.z);
      float st = sdCapsule(ap, base, top1, 0.035);
      if (uM[15].w > 0.5) st = min(st, sdCapsule(ap, base, top1 + vec3(0.0, 0.0, ch*0.45), 0.03));
      res = opU(res, vec2(st, 8.0));
    }
  }
  // ---------------- tail
  {
    vec4 V0 = uM[14], V1 = uM[15];
    float s = p.y - V1.x, c = p.z - V1.y, t = p.x;
    float h = V0.x;
    float hasT = uM[13].w;
    float rud0 = hasT > 0.5 ? 0.05 : 0.08*h;
    float fin = sdPanel(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97);
    // right rudder (yaw +) swings the trailing edge to the right (+x)
    float rud = sdSurface(s, c, t, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97, -cYaw*0.42, 0.0);
    float tail = min(fin, rud);
    vec4 H0 = uM[12], H1 = uM[13];
    float hs = abs(p.x), ht = p.y - (H1.x + hs*H1.z), hc = p.z - H1.y;
    float stab = sdPanel(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98);
    // pulling back (pitch +) raises the elevator trailing edge
    float elev = sdSurface(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98, -cPitch*0.4, 0.0);
    tail = min(tail, min(stab, elev));
    if (hasT > 0.5) tail = smin(tail, sdEllipsoid(p - vec3(0.0, H1.x, H1.y + H0.y*0.45), vec3(0.18, 0.2, H0.y*0.55)), 0.08);
    float d = smin(res.x, tail, 0.12*R);
    res = vec2(d, tail < res.x ? 3.0 : res.y);
  }
  // ---------------- engines
  {
    vec4 N0 = uM[16], N1 = uM[17];
    vec4 S0 = uM[1];
    if (eng <= 1) {
      float sr = N1.y;
      float spin = sdRoundCone(p, vec3(0.0, S0.w, S0.x - sr*2.3), vec3(0.0, S0.w, S0.x + 0.05), 0.015, sr);
      res = opU(res, vec2(spin, 16.0));
      if (eng == 0) {
        vec3 sec = fusSection(S0.x + 0.9);
        float ex = sdCapsule(vec3(abs(p.x), p.y, p.z), vec3(0.12, sec.z - sec.y*0.85, S0.x + 0.9), vec3(0.16, sec.z - sec.y - 0.06, S0.x + 1.15), 0.035);
        res = opU(res, vec2(ex, 17.0));
      } else {
        vec3 sec = fusSection(S0.x + 1.2);
        vec3 ap = vec3(abs(p.x), p.y, p.z);
        float ex = sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.22, sec.z + 0.12, S0.x + 1.5), 0.085);
        ex = max(ex, -sdCapsule(ap, vec3(sec.x*0.85, sec.z + 0.05, S0.x + 1.15), vec3(sec.x + 0.4, sec.z + 0.14, S0.x + 1.6), 0.06));
        res = opU(res, vec2(ex, 17.0));
        float lip = sdCapsule(p, vec3(-0.12, S0.w - 0.32, S0.x + 0.45), vec3(0.12, S0.w - 0.32, S0.x + 0.45), 0.07);
        res.x = smin(res.x, lip, 0.08);
      }
    } else if (eng <= 3) {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z);
      float nr = N0.z, z0 = N0.w, len = N1.x;
      float wingY = uM[10].x + N0.x*uM[10].z;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, z0), vec3(0.0, 0.02, z0 + len*0.3), nr*0.72, nr);
      nac = smin(nac, sdRoundCone(np, vec3(0.0, 0.02, z0 + len*0.3), vec3(0.0, wingY - N0.y - nr*0.25, z0 + len), nr, nr*0.35), 0.1);
      if (eng == 3) {
        nac = smin(nac, sdEllipsoid(np - vec3(0.0, -nr*0.75, z0 + 0.45), vec3(nr*0.35, nr*0.22, 0.5)), 0.08);
        float ex = sdCapsule(np, vec3(nr*0.8, 0.1, z0 + len*0.35), vec3(nr*1.05, 0.15, z0 + len*0.5), 0.09);
        res = opU(res, vec2(ex, 17.0));
      }
      res = opU(res, vec2(nac, 5.0));
      float sr = N1.y;
      res = opU(res, vec2(sdRoundCone(np, vec3(0.0, 0.0, z0 - sr*2.3), vec3(0.0, 0.0, z0 + 0.05), 0.015, sr), 16.0));
    } else {
      vec3 np = vec3(abs(p.x) - N0.x, p.y - N0.y, p.z - N0.w);
      float nr = N0.z, len = N1.x;
      float nac = sdRoundCone(np, vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, len), nr, nr*0.78);
)"
R"(      float inlet = sdCapsule(np, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.18), nr*0.82);
      nac = max(nac, -inlet);
      res = opU(res, vec2(nac, 5.0));
      float fan = sdCapsule(np, vec3(0.0, 0.0, 0.2), vec3(0.0, 0.0, 0.3), nr*0.83);
      res = opU(res, vec2(fan, 21.0));
      float cone = sdRoundCone(np, vec3(0.0, 0.0, len - 0.25), vec3(0.0, 0.0, len + 0.3), nr*0.5, 0.04);
      res = opU(res, vec2(cone, 17.0));
      vec3 sec = fusSection(N0.w + len*0.5);
      float px0 = sec.x*0.7, px1 = N0.x - nr*0.8;
      float pylon = sdRoundBox(vec3(abs(p.x) - 0.5*(px0 + px1), p.y - N0.y, p.z - N0.w - len*0.5), vec3(0.5*(px1 - px0) + 0.05, 0.06, len*0.28), 0.04);
      res.x = smin(res.x, pylon, 0.1);
    }
  }
  // ---------------- cargo pod
  if (uM[17].w > 0.5) {
    vec3 sec = fusSection(-0.5);
    float pod = sdRoundBox(p - vec3(0.0, sec.z - sec.y - 0.18, -0.4), vec3(0.42, 0.2, 2.6), 0.17);
    pod = smin(pod, sdEllipsoid(p - vec3(0.0, sec.z - sec.y - 0.2, -3.0), vec3(0.42, 0.22, 0.8)), 0.2);
    res.x = smin(res.x, pod, 0.12);
  }
  // ---------------- landing gear
  if (gear > 0.02) {
    vec4 G0 = uM[18], G1 = uM[19];
    float track = G0.x, wr = G0.y, mz = G0.z, nz = G0.w, gh = G1.x, tz = G1.y;
    bool retract = gtype >= 3;
    float up = retract ? (1.0 - gear) : 0.0;
    vec3 ap = vec3(abs(p.x), p.y, p.z);
    float wy = -gh + wr;
    float lift = up*(gh - R*0.6);
    vec3 wc = vec3(track, wy + lift, mz);
    vec3 secM = fusSection(mz);
    float legs, tyres, spats = 1e5;
    if (gtype == 0) {
      legs = sdCapsule(ap, vec3(secM.x*0.75, secM.z - secM.y*0.8, mz), wc + vec3(-0.06, 0.04, 0.0), 0.03);
      tyres = sdRoundCylX(ap - wc, wr, 0.065, 0.04);
      float sp = sdEllipsoid(ap - wc - vec3(0.0, 0.04, 0.06), vec3(0.1, wr*1.05, wr*1.9));
      spats = max(sp, -(ap.y - (wc.y - wr*0.5)));
    } else if (gtype == 1) {
      legs = sdCapsule(ap, vec3(secM.x*0.7, secM.z - secM.y*0.85, mz), wc + vec3(-0.08, 0.06, 0.0), 0.045);
      tyres = sdRoundCylX(ap - wc, wr, 0.09, 0.05);
      tyres = min(tyres, sdRoundCylX(ap - wc - vec3(0.06, 0.0, 0.0), wr*0.45, 0.04, 0.02));
    } else if (gtype == 2) {
      legs = min(sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz - 0.35), wc, 0.03), sdCapsule(ap, vec3(secM.x*0.8, secM.z - secM.y*0.8, mz + 0.3), wc, 0.03));
      tyres = sdRoundCylX(ap - wc, wr, 0.14, 0.09);
    } else if (gtype == 3) {
      vec3 top = vec3(track, uM[16].y - uM[16].z*0.6, mz);
      legs = sdCapsule(ap, top, vec3(track, wc.y + 0.05, mz), 0.09);
      legs = min(legs, sdCapsule(ap, vec3(track - 0.25, wc.y, mz), vec3(track + 0.25, wc.y, mz), 0.05));
      tyres = min(sdRoundCylX(ap - wc - vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06), sdRoundCylX(ap - wc + vec3(0.22, 0.0, 0.0), wr, 0.11, 0.06));
    } else {
      float wyy = uM[10].x + track*uM[10].z;
      legs = sdCapsule(ap, vec3(track, wyy - 0.1 + lift*0.2, mz), wc + vec3(0.0, 0.05, 0.0), 0.06);
      tyres = sdRoundCylX(ap - wc, wr, 0.1, 0.05);
    }
    // nose / tail wheel (nose wheel steers with the rudder pedals)
    if (G1.z < 0.5) {
      vec3 secN = fusSection(nz);
      float nwr = gtype == 3 ? wr*0.75 : wr*0.85;
      vec3 piv = vec3(0.0, 0.0, nz);
      vec3 q = p - piv; q.xz = rot2(q.xz, -steer);  // geometry rotated by the steering angle
      vec3 nc = vec3(0.0, -gh + nwr + lift, 0.0);
      float nl = sdCapsule(q, vec3(0.0, secN.z - secN.y*0.7, -0.05), nc + vec3(0.0, nwr*0.9, 0.0), gtype >= 3 ? 0.07 : 0.035);
      nl = min(nl, sdCapsule(vec3(abs(q.x), q.yz), vec3(0.06 + (gtype == 3 ? 0.15 : 0.0), nc.y + nwr*0.9, 0.0), vec3(0.06 + (gtype == 3 ? 0.15 : 0.0), nc.y, 0.0), 0.02));
      float nt = gtype == 3 ? min(sdRoundCylX(q - nc - vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04), sdRoundCylX(q - nc + vec3(0.15, 0.0, 0.0), nwr, 0.07, 0.04))
                            : sdRoundCylX(q - nc, nwr, 0.055, 0.035);
      if (gtype == 0) { float sp = sdEllipsoid(q - nc - vec3(0.0, 0.05, 0.06), vec3(0.1, nwr*1.12, nwr*2.2)); spats = min(spats, max(sp, -(q.y - (nc.y - nwr*0.5)))); }
      legs = min(legs, nl); tyres = min(tyres, nt);
    } else {
      vec3 q = p - vec3(0.0, 0.0, tz); q.xz = rot2(q.xz, -steer);
      vec3 tsec = fusSection(tz - 0.3);
      vec3 tc = vec3(0.0, -gh + 0.11*L + 0.1, 0.0);
      legs = min(legs, sdCapsule(q, vec3(0.0, tsec.z - tsec.y*0.6, -0.3), tc + vec3(0.0, 0.03, -0.05), 0.02));
      tyres = min(tyres, sdRoundCylX(q - tc, 0.1, 0.035, 0.02));
    }
    if (retract && gear < 0.06) { legs = 1e5; tyres = 1e5; }
    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
    res = opU(res, vec2(spats, 1.0));
  }
  // ---------------- small details: nav lights, beacon, antennas, pitot
  {
    vec4 W0 = uM[9], W1 = uM[10];
    vec3 tip = vec3(W0.x + 0.02, W1.x + W0.x*W1.z, W1.y + W0.w + W0.z*0.25);
    res = opU(res, vec2(length(vec3(abs(p.x), p.y, p.z) - tip) - 0.045, 18.0));
    vec4 V0 = uM[14], V1 = uM[15];
    res = opU(res, vec2(length(p - vec3(0.0, V1.x + V0.x + 0.04, V1.y + V0.w + V0.z*0.4)) - 0.05, 19.0));
    vec3 sec = fusSection(0.2);
    float ant = sdRoundBox(p - vec3(0.0, sec.z + sec.y + 0.11, 0.2), vec3(0.006, 0.12, 0.05), 0.004);
    res = opU(res, vec2(ant, 13.0));
  }
  // ---------------- cockpit interior (only rendered from inside)
  if (inside > 0.5) {
    vec4 E = uM[22]; float pz = uM[21].w, phw = E.w; int ck = int(uM[21].z + 0.5);
    float panel = sdRoundBox(p - vec3(0.0, E.y - 0.36, pz), vec3(phw, 0.24, 0.045), 0.015);
    res = opU(res, vec2(panel, 10.0));
    vec4 WSg = uM[23];
    float gz0 = min(WSg.x - 0.05, pz - 0.2);
    float glare = sdRoundBox(p - vec3(0.0, E.y - 0.1, 0.5*(gz0 + pz + 0.06)), vec3(phw*0.97, 0.022, 0.5*(pz + 0.06 - gz0)), 0.018);
    res = opU(res, vec2(glare, 14.0));
    float floor_ = sdBox(p - vec3(0.0, E.y - 1.08, E.z), vec3(phw, 0.02, 1.6));
    res = opU(res, vec2(floor_, 11.0));
    // seats
    vec3 sp = vec3(abs(p.x) - abs(E.x), p.y, p.z);
    float seat = sdRoundBox(sp - vec3(0.0, E.y - 0.8, E.z + 0.05), vec3(0.21, 0.06, 0.24), 0.05);
    vec3 bp = sp - vec3(0.0, E.y - 0.4, E.z + 0.37); bp.yz = rot2(bp.yz, 0.18);
    seat = min(seat, sdRoundBox(bp, vec3(0.2, 0.36, 0.055), 0.05));
    seat = min(seat, sdRoundBox(sp - vec3(0.0, E.y + 0.08, E.z + 0.47), vec3(0.11, 0.08, 0.05), 0.04));
    res = opU(res, vec2(seat, 12.0));
    // control yokes: pull moves toward the pilot, roll right turns the yoke clockwise
    float pull = cPitch*0.075;
    vec3 yp = vec3(abs(p.x) - abs(E.x), p.y - (E.y - 0.43), p.z - pz);
    float col = sdCapsule(yp, vec3(0.0, 0.0, 0.02), vec3(0.0, 0.0, 0.2 + pull), 0.018);
    vec3 hp = yp - vec3(0.0, 0.0, 0.22 + pull);
    hp.xy = rot2(hp.xy, cRoll*0.75);
    float yoke = min(col, sdCapsule(hp, vec3(-0.1, 0.0, 0.0), vec3(0.1, 0.0, 0.0), 0.015));
    yoke = min(yoke, sdCapsule(vec3(abs(hp.x), hp.yz), vec3(0.1, 0.0, 0.0), vec3(0.125, 0.085, 0.0), 0.018));
    yoke = min(yoke, sdRoundBox(hp, vec3(0.04, 0.025, 0.025), 0.01));
    res = opU(res, vec2(yoke, 13.0));
    // rudder pedals: right rudder pushes the right pedal forward
    vec3 pp = vec3(p.x - sign(p.x)*abs(E.x), p.y - (E.y - 0.98), p.z - pz - 0.2);
    float side = sign(pp.x);
    pp.z += side*cYaw*0.06;
    pp.x = abs(pp.x) - 0.1;
    pp.yz = rot2(pp.yz, 0.5);
    res = opU(res, vec2(sdRoundBox(pp, vec3(0.04, 0.075, 0.012), 0.008), 13.0));
    // throttle
    if (ck == 0) {
      float zt = pz + 0.05 + 0.1*(1.0 - cThr);
      float thr = min(sdCapsule(p, vec3(0.0, E.y - 0.5, pz + 0.04), vec3(0.0, E.y - 0.5, zt), 0.006), length(p - vec3(0.0, E.y - 0.5, zt)) - 0.022);
      res = opU(res, vec2(thr, 13.0));
    } else {
      float ped = sdRoundBox(p - vec3(0.0, E.y - 0.78, pz + 0.3), vec3(0.11, 0.22, 0.3), 0.03);
      res = opU(res, vec2(ped, 10.0));
      float a = mix(-0.55, 0.6, cThr);
      vec3 piv = vec3(0.0, E.y - 0.56, pz + 0.38);
      vec3 tip = piv + vec3(0.0, 0.14*cos(a), -0.14*sin(a));
      vec3 lp2 = vec3(abs(p.x) - 0.035, p.y, p.z);
      float lev = min(sdCapsule(lp2, piv, tip, 0.008), sdRoundBox(lp2 - tip, vec3(0.025, 0.012, 0.018), 0.008));
      res = opU(res, vec2(lev, 13.0));
    }
    if (ck == 2) res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y + 0.36, E.z - 0.2), vec3(0.22, 0.03, 0.3), 0.02), 14.0));
    float compass = sdRoundBox(p - vec3(0.0, E.y - 0.05, pz - 0.05), vec3(0.04, 0.03, 0.03), 0.01);
    res = opU(res, vec2(compass, 13.0));
  }
  return res;
}
vec3 planeNormal(vec3 p){ const vec2 k = vec2(1,-1); float e = 0.0025;
  return normalize(k.xyy*mapPlane(p+k.xyy*e).x + k.yyx*mapPlane(p+k.yyx*e).x + k.yxy*mapPlane(p+k.yxy*e).x + k.xxx*mapPlane(p+k.xxx*e).x); }

float planeBound(){ return max(uM[0].x, uM[9].x*2.0)*0.55 + 1.5; }
vec2 tracePlane(vec3 ro, vec3 rd, float tmax){
  if (uPlaneOn == 0) return vec2(-1.0);
  vec3 oc = ro - uPlanePos; float br = planeBound();
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0) return vec2(-1.0);
  h = sqrt(h); float t0 = max(-b-h, 0.0), t1 = min(-b+h, tmax);
  if (t0 > t1) return vec2(-1.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float t = t0;
  int steps = uPS.w > 0.5 ? 160 : 120;
  for (int i=0;i<160;i++){
    if (i >= steps) break;
    vec2 d = mapPlane(lo + ld*t);
    if (d.x < 0.0015*max(1.0, t*0.03)) return vec2(t, d.y);
    t += d.x*0.8;
    if (t > t1) break;
  }
  return vec2(-1.0);
}
float planeShadow(vec3 ro, vec3 rd){
  if (uPlaneOn == 0) return 1.0;
  vec3 oc = ro - uPlanePos; float br = planeBound();
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0 || -b + sqrt(max(h,0.0)) < 0.0) return 1.0;
  h = sqrt(h); float t = max(-b-h, 0.0), t1 = -b+h;
  mat3 inv = transpose(uPlaneRot); vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float res = 1.0;
  for (int i=0;i<56;i++){
    float d = mapPlane(lo + ld*t).x;
    res = min(res, 10.0*d/max(t,0.1));
    if (res < 0.01) return 0.0;
    t += clamp(d, 0.03, 2.0);
    if (t > t1) break;
  }
  return clamp(res, 0.0, 1.0);
}

// ---------------------------------------------------------------- cockpit instruments (drawn on the panel face)
vec3 dialFace(vec2 d, float r, out bool inside){
  float rr = length(d)/r; inside = rr < 1.0;
  if (rr > 1.12) return vec3(-1.0);
  if (rr > 1.0) return vec3(0.18, 0.18, 0.19);
  return vec3(0.015);
}
float needle(vec2 d, float r, float ang, float len, float w){
  vec2 nv = vec2(sin(ang), cos(ang));
  float along = dot(d, nv)/r, perp = abs(d.x*nv.y - d.y*nv.x)/r;
  return step(-0.12, along)*step(along, len)*step(perp, w*(1.0 - along*0.6));
}
float ticks(vec2 d, float r, float n, float a0, float a1, float inner){
  float a = atan(d.x, d.y); float rr = length(d)/r;
  if (a < a0 || a > a1 || rr < inner || rr > 0.95) return 0.0;
  float f = fract((a - a0)/(a1 - a0)*n + 0.5);
  return step(abs(f - 0.5), 0.07);
}
// Returns instrument colour (emissive) for a point on the panel face; q relative to the pilot's panel centre.
vec3 drawInstruments(vec2 q, int ck, bool pilotSide){
  vec3 col = vec3(-1.0);
  float ias = uI0.x, alt = uI0.y, hdg = uI0.z, vs = uI0.w;
  float pitch = uI1.x, bank = uI1.y, engF = uI1.z, fuel = uI1.w;
  if (ck == 2) {
    // glass cockpit: PFD and navigation display
    vec2 pd = q - vec2(-0.07, 0.0);
    if (abs(pd.x) < 0.085 && abs(pd.y) < 0.075) {
      float b = bank*0.01745;
      float hz = dot(pd, vec2(-sin(b), cos(b))) + pitch*0.0022;
      col = hz > 0.0 ? vec3(0.12, 0.35, 0.8) : vec3(0.45, 0.28, 0.12);
      for (int i = -2; i <= 2; i++) { if (i == 0) continue; float ly = hz - float(i)*0.022; if (abs(ly) < 0.0012 && abs(dot(pd, vec2(cos(b), sin(b)))) < 0.018) col = vec3(1.0); }
      if (abs(hz) < 0.001) col = vec3(1.0);
      if (abs(pd.y) < 0.003 && abs(pd.x) > 0.008 && abs(pd.x) < 0.03) col = vec3(1.0, 0.8, 0.1);
      // speed / altitude tapes with scrolling ticks
      if (pd.x < -0.06) { col = vec3(0.12); float tk = fract((pd.y*400.0 + ias)/10.0); if (tk < 0.08 && pd.x > -0.068) col = vec3(0.9); if (abs(pd.y) < 0.006) col = vec3(0.0, 0.9, 0.4); }
)"
R"(      if (pd.x > 0.06) { col = vec3(0.12); float tk = fract((pd.y*4000.0 + alt)/100.0); if (tk < 0.08 && pd.x < 0.068) col = vec3(0.9); if (abs(pd.y) < 0.006) col = vec3(0.0, 0.9, 0.4); }
      if (pd.y < -0.064) { col = vec3(0.1); float tk = fract((pd.x*600.0 + hdg)/10.0); if (tk < 0.1) col = vec3(0.8); if (abs(pd.x) < 0.002) col = vec3(1.0, 0.9, 0.2); }
      return col*1.4;
    }
    vec2 nd = q - vec2(0.12, 0.0);
    if (abs(nd.x) < 0.075 && abs(nd.y) < 0.075) {
      col = vec3(0.01, 0.015, 0.02);
      vec2 c = nd + vec2(0.0, 0.045);
      float rr = length(c);
      float a = atan(c.x, c.y)*57.2958 + hdg;
      if (rr > 0.085 && rr < 0.09 && c.y > 0.0) col = vec3(0.85);
      if (rr > 0.078 && rr < 0.09 && c.y > 0.0 && fract(a/10.0) < 0.06) col = vec3(0.85);
      if (abs(c.x) < 0.0015 && c.y > 0.0 && c.y < 0.09) col = vec3(1.0, 0.3, 1.0);
      if (abs(c.x) < 0.008 && abs(c.y) < 0.008) col = vec3(1.0, 0.9, 0.2);
      // engine strip
      if (nd.y < -0.06 && abs(nd.x) < 0.07) { col = vec3(0.05); if (nd.x + 0.07 < engF*0.14) col = vec3(0.1, 0.8, 0.3); }
      return col*1.4;
    }
    return col;
  }
  float r = 0.038;
  bool inD;
  // Airspeed
  vec2 d = q - vec2(-0.095, 0.045);
  vec3 c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float a = atan(d.x, d.y); float rr = length(d)/r;
      float vmax = 200.0;
      float g0 = -2.6 + 45.0/vmax*5.2, g1 = -2.6 + 115.0/vmax*5.2;
      if (rr > 0.86 && rr < 0.95 && a > g0 && a < g1) c = vec3(0.1, 0.7, 0.2);
      if (rr > 0.86 && rr < 0.95 && a > g1 && a < g1 + 0.6) c = vec3(0.9, 0.8, 0.1);
      c += vec3(0.85)*ticks(d, r, 20.0, -2.6, 2.6, 0.78);
      c = mix(c, vec3(0.95), needle(d, r, -2.6 + clamp(ias, 0.0, vmax)/vmax*5.2, 0.85, 0.05));
    }
    return c;
  }
  // Attitude
  d = q - vec2(0.0, 0.045);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float b = bank*0.01745;
      float hz = dot(d, vec2(-sin(b), cos(b))) + pitch*0.0012;
      c = hz > 0.0 ? vec3(0.15, 0.4, 0.85) : vec3(0.5, 0.3, 0.12);
      if (abs(hz) < 0.0009) c = vec3(1.0);
      for (int i = -2; i <= 2; i++) { if (i == 0) continue; if (abs(hz - float(i)*0.012) < 0.0007 && abs(dot(d, vec2(cos(b), sin(b)))) < 0.01) c = vec3(1.0); }
      if (abs(d.y) < 0.0018 && abs(d.x) > 0.006 && abs(d.x) < 0.02) c = vec3(1.0, 0.6, 0.0);
      if (length(d) < 0.002) c = vec3(1.0, 0.6, 0.0);
      c += vec3(0.85)*ticks(d, r, 6.0, -1.05, 1.05, 0.85);
    }
    return c;
  }
  // Altimeter
  d = q - vec2(0.095, 0.045);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      c += vec3(0.85)*ticks(d, r, 50.0, -3.1416, 3.1416, 0.85);
      c += vec3(0.85)*ticks(d, r, 10.0, -3.1416, 3.1416, 0.74);
      c = mix(c, vec3(0.95), needle(d, r, alt/1000.0*6.2832, 0.88, 0.04));
      c = mix(c, vec3(0.95), needle(d, r, alt/10000.0*6.2832, 0.55, 0.08));
    }
    return c;
  }
  // Turn coordinator
  d = q - vec2(-0.095, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      vec2 dd = rot2(d, clamp(uI2.x/3.0, -1.5, 1.5)*0.26);
      if (abs(dd.y) < 0.0025 && abs(dd.x) < 0.026) c = vec3(0.95);
      if (abs(dd.x) < 0.003 && dd.y > 0.0 && dd.y < 0.008) c = vec3(0.95);
      vec2 bc = vec2(clamp(-uI2.y*0.0015, -0.016, 0.016), -0.022);
      if (abs(d.y + 0.022) < 0.005 && abs(d.x) < 0.02) c = vec3(0.25);
      if (length(d - bc) < 0.0045) c = vec3(0.02);
      if (abs(abs(d.x) - 0.006) < 0.0007 && abs(d.y + 0.022) < 0.005) c = vec3(0.9);
    }
    return c;
  }
  // Heading indicator (rotating card)
  d = q - vec2(0.0, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      float a = atan(d.x, d.y)*57.2958 + hdg; float rr = length(d)/r;
      float f = fract(a/10.0);
      if (rr > 0.8 && rr < 0.95 && (f < 0.07 || f > 0.93)) c = vec3(0.85);
      float f3 = fract(a/30.0);
      if (rr > 0.66 && rr < 0.95 && (f3 < 0.025 || f3 > 0.975)) c = vec3(0.95);
      if (rr > 0.6 && rr < 0.95 && (fract(a/360.0) < 0.012 || fract(a/360.0) > 0.988)) c = vec3(1.0, 0.25, 0.1);
      if (abs(d.x) < 0.0015 && d.y > 0.0 && rr < 0.6) c = vec3(1.0, 0.6, 0.0);
      if (abs(d.y) < 0.0015 && abs(d.x) < 0.01) c = vec3(1.0, 0.6, 0.0);
    }
    return c;
  }
  // Vertical speed
  d = q - vec2(0.095, -0.05);
  c = dialFace(d, r, inD);
  if (c.x >= 0.0) {
    if (inD) {
      c += vec3(0.85)*ticks(d, r, 8.0, -1.5708 - 2.97, -1.5708 + 2.97, 0.8);
      c = mix(c, vec3(0.95), needle(d, r, -1.5708 + clamp(vs/2000.0, -1.0, 1.0)*2.97, 0.85, 0.05));
    }
    return c;
  }
  // engine: RPM / N1 and fuel
  int engines = ck == 1 ? 2 : 1;
  for (int e = 0; e < 2; e++) {
    if (e >= engines) break;
    d = q - vec2(0.2 + float(e)*0.085, 0.045);
    c = dialFace(d, r*0.85, inD);
    if (c.x >= 0.0) {
      if (inD) {
        float rr = length(d)/(r*0.85); float a = atan(d.x, d.y);
        if (rr > 0.84 && rr < 0.95 && a > 0.9 && a < 1.7) c = vec3(0.1, 0.7, 0.2);
        if (rr > 0.84 && rr < 0.95 && a > 1.7 && a < 1.8) c = vec3(0.9, 0.1, 0.1);
        c += vec3(0.85)*ticks(d, r*0.85, 10.0, -2.36, 2.36, 0.8);
        c = mix(c, vec3(0.95), needle(d, r*0.85, -2.36 + clamp(engF, 0.0, 1.1)*4.2, 0.85, 0.05));
      }
      return c;
    }
  }
  d = q - vec2(0.2, -0.05);
  c = dialFace(d, r*0.7, inD);
  if (c.x >= 0.0) {
    if (inD) {
      c += vec3(0.85)*ticks(d, r*0.7, 4.0, -1.2, 1.2, 0.75);
      float rr = length(d)/(r*0.7); float a = atan(d.x, d.y);
      if (rr > 0.8 && rr < 0.95 && a < -0.85) c = vec3(0.9, 0.1, 0.1);
      c = mix(c, vec3(0.95), needle(d, r*0.7, -1.2 + fuel*2.4, 0.85, 0.06));
    }
    return c;
  }
  // radio stack / annunciators (centre)
  vec2 rs = q - vec2(0.33, 0.0);
  if (abs(rs.x) < 0.07 && abs(rs.y) < 0.09) {
    float row = floor((rs.y + 0.09)/0.045);
    vec2 cell = vec2(rs.x, mod(rs.y + 0.09, 0.045) - 0.0225);
    col = vec3(0.04);
    if (abs(cell.y) < 0.008 && abs(cell.x) < 0.045) col = vec3(0.05, 0.6, 0.25)*(0.6 + 0.4*step(0.5, fract(cell.x*60.0 + row)));
    if (abs(cell.y) < 0.006 && abs(cell.x - 0.058) < 0.006) col = vec3(0.3);
    return col;
  }
  return col;
}

// ---------------------------------------------------------------- terrain
float traceTerrain(vec3 ro, vec3 rd, float tmax){
  float t = 1.0;
  if (ro.y > uMaxH) { if (rd.y >= 0.0) return -1.0; t = max(t, (ro.y - uMaxH)/(-rd.y)); }
  float lt = t, ldh = 0.0;
  int maxSteps = uQuality > 1 ? 320 : (uQuality > 0 ? 220 : 150);
  for (int i=0;i<320;i++){
    if (i >= maxSteps || t > tmax) break;
    vec3 p = ro + rd*t;
    if (p.y > uMaxH && rd.y > 0.0) return -1.0;
    int oct = t < 1500.0 ? 7 : (t < 6000.0 ? 6 : 5);
    float h = terrainH(p.xz, oct);
    float dh = p.y - h;
    if (dh < 0.0015*t) {
      if (i == 0) return t;
      return lt + (t - lt) * ldh / max(ldh - dh, 1e-4);
    }
    lt = t; ldh = dh;
    t += max(dh*0.42, 0.25 + 0.0015*t);
  }
  return -1.0;
}
float terrainShadow(vec3 ro, vec3 rd){
  float res = 1.0, t = 4.0;
  for (int i=0;i<36;i++){
    vec3 p = ro + rd*t;
    if (p.y > uMaxH) break;
    float h = p.y - terrainH(p.xz, 4);
    res = min(res, 14.0*h/t);
    if (res < 0.0) return 0.0;
    t += clamp(h*0.6, 6.0, 450.0);
  }
  return clamp(res, 0.0, 1.0);
}
vec3 terrainNormal(vec2 p, float t){
  float e = max(0.6, t*0.0015);
  int oct = t < 600.0 ? 11 : (t < 3000.0 ? 9 : 7);
  float h = terrainH(p, oct);
  return normalize(vec3(terrainH(p - vec2(e,0.0), oct) - terrainH(p + vec2(e,0.0), oct), 2.0*e, terrainH(p - vec2(0.0,e), oct) - terrainH(p + vec2(0.0,e), oct)));
}

// ---------------------------------------------------------------- clouds
float cloudDensity(vec3 p, int detail){
  float thick = 900.0 + 900.0*uCloudCover;
  float hf = (p.y - uCloudBase) / thick;
  if (hf < 0.0 || hf > 1.0) return 0.0;
  vec2 q = (p.xz + uWindOff) / 5200.0;
  float cov = fbm2(q, 4);
  float shape = smoothstep(0.0, 0.12, hf) * smoothstep(1.0, 0.35 - 0.2*uCloudCover, hf);
  float d = cov - (1.05 - uCloudCover*0.75) + shape*0.45 - 0.45;
  if (d < -0.15) return 0.0;
  if (detail > 0) d -= (vnoise3(p/320.0 + vec3(uTime*0.01)) - 0.5)*0.22 + (vnoise3(p/90.0) - 0.5)*0.08;
  return clamp(d*3.2, 0.0, 1.0);
}
vec4 traceClouds(vec3 ro, vec3 rd, float tmax, float jitter){
  if (uCloudCover < 0.02) return vec4(0.0,0.0,0.0,1.0);
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
  int N = uQuality > 1 ? 48 : (uQuality > 0 ? 32 : 20);
  float dt = (t1 - t0)/float(N);
  float T = 1.0; vec3 L = vec3(0.0);
  float mu = dot(rd, uSunDir);
  float phase = mix(0.08, 0.35*pow(max(mu,0.0), 12.0) + 0.1, 0.6);
  vec3 sunC = uSunCol; vec3 amb = skyColor(vec3(0.0,1.0,0.0))*1.4 + vec3(0.05);
  float t = t0 + dt*jitter;
  for (int i=0;i<48;i++){
    if (i >= N) break;
    vec3 p = ro + rd*t;
    float d = cloudDensity(p, 1);
    if (d > 0.01) {
      float ds = cloudDensity(p + uSunDir*120.0, 0) + cloudDensity(p + uSunDir*350.0, 0)*0.5;
      float sh = exp(-ds*2.2);
      float hf = clamp((p.y - uCloudBase)/thick, 0.0, 1.0);
      vec3 c = sunC*sh*(phase*3.0)*(1.0 - exp(-d*4.0)) + amb*(0.35 + 0.65*hf)*(1.0 - 0.4*uStorm);
      c += vec3(0.8,0.85,1.0)*uLightning*2.0;
      float a = 1.0 - exp(-d*dt*0.012);
      float fogT = exp(-uFogB*t*0.6);
      vec3 skyH = skyColor(normalize(vec3(rd.x, max(rd.y,0.02), rd.z)));
      L += T*a*mix(skyH, c, fogT);
      T *= 1.0 - a;
      if (T < 0.02) break;
    }
    t += dt;
  }
  return vec4(L, T);
}
float cloudShadow(vec3 p){
  if (uCloudCover < 0.05) return 1.0;
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
  vec3 spec = D*G*F/(4.0*nv*max(nl,1e-3)+1e-3);
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

)"
R"(void runwayMaterial(int ai, vec2 uv, inout Mat m, vec3 pw, out bool onRw){
  vec4 d = uApDim[ai]; float len = d.x, wid = d.y; int surf = int(d.z); int size = int(d.w);
  onRw = false;
  float u = uv.x, v = uv.y;
  vec3 nTS;
  // apron/taxiway for paved airports
  float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
  float off = wid*0.5 + (size == 2 ? 170.0 : 85.0);
  if (surf == 0 && size > 0) {
    float apronV = side*v;
    if (apronV > wid*0.5 + 20.0 && apronV < off + (size==2?90.0:60.0) && abs(u + len*0.05) < len*0.33) {
      vec4 t = matSample(pw.xz, M_ASPHALT, 9.0, nTS);
      m.alb = t.rgb*1.15; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
      if (abs(apronV - wid*0.5 - 45.0) < 0.3 && abs(u + len*0.05) < len*0.32) m.alb = vec3(0.6,0.5,0.05);
      return;
    }
    // taxiway connectors
    if (apronV > wid*0.5 - 1.0 && apronV < off && (abs(u - len*0.28) < 11.0 || abs(u + len*0.38) < 11.0)) {
      vec4 t = matSample(pw.xz, M_ASPHALT, 9.0, nTS);
      m.alb = t.rgb*1.05; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
      if (abs(abs(u - len*0.28) < 11.0 ? u - len*0.28 : u + len*0.38) < 0.25) m.alb = vec3(0.55,0.45,0.05);
      return;
    }
  }
  if (abs(u) > len*0.5 + 6.0 || abs(v) > wid*0.5 + 3.0) {
    // mown grass strip around runway
    if (abs(u) < len*0.5 + 120.0 && abs(v) < wid*0.5 + 60.0) {
      vec4 t = matSample(pw.xz, M_GRASS, 5.0, nTS);
      float stripe = step(0.5, fract(u/18.0));
      m.alb = t.rgb*(0.85 + 0.15*stripe)*vec3(0.95,1.05,0.9); m.rough = 0.9; m.nrm = nTS;
    }
    return;
  }
  onRw = true;
  int layer = surf == 0 ? M_ASPHALT : surf == 1 ? M_GRASS : surf == 2 ? M_GRAVEL : surf == 3 ? M_SNOW : M_SAND;
  vec4 t = matSample(pw.xz, layer, surf == 0 ? 7.0 : 5.0, nTS);
  m.alb = t.rgb; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
  if (surf == 1) m.alb *= vec3(0.8,1.0,0.75) * (0.88 + 0.12*step(0.5, fract(u/22.0)));
  if (surf == 0) {
    // tyre marks in touchdown zones
    float tz = smoothstep(len*0.5 - 80.0, len*0.5 - 200.0, abs(u)) * smoothstep(len*0.5 - 650.0, len*0.5 - 300.0, abs(u));
    float tyre = tz * smoothstep(wid*0.3, 0.0, abs(abs(v) - 3.5)) * (0.5 + 0.5*vnoise(vec2(u*0.05, v*2.0)));
    m.alb *= 1.0 - 0.55*tyre;
    m.rough = mix(m.rough, 0.45, tyre);
    float paint = 0.0;
    float au = abs(u), hl = len*0.5;
    // centreline dashes
    if (abs(v) < 0.45 && fract(u/50.0) < 0.6 && au < hl - 70.0) paint = 1.0;
    // edge lines
    if (abs(abs(v) - (wid*0.5 - 1.0)) < 0.45) paint = 1.0;
    // threshold piano keys
    if (au > hl - 50.0 && au < hl - 12.0 && abs(v) < wid*0.5 - 3.0 && fract((v + wid*0.5)/3.6) < 0.5) paint = 1.0;
    // aiming point blocks
    if (au > hl - 380.0 && au < hl - 320.0 && abs(abs(v) - wid*0.25) < 2.5) paint = 1.0;
    // touchdown zone bars
    if (au > hl - 300.0 && au < hl - 150.0 && fract(au/75.0) < 0.3 && abs(abs(v) - wid*0.22) < 2.5 && wid > 25.0) paint = 1.0;
    // runway number block (simple)
    if (au > hl - 95.0 && au < hl - 65.0 && abs(v) < 4.0 && fract(v/2.0) < 0.6) paint = 1.0;
    m.alb = mix(m.alb, vec3(0.85), paint*0.9);
    m.rough = mix(m.rough, 0.6, paint);
  }
}

Mat terrainMaterial(vec3 p, vec3 n, float t, vec4 base){
  Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
  float lush = base.z, cold = base.w;
  float slope = 1.0 - n.y;
  float hNoise = fbm2(p.xz/900.0, 4);
  float n2 = fbm2(p.xz/180.0, 3);
  vec3 nTS;
  // layered material weights
  float snowLine = mix(1700.0, 350.0, cold) + (hNoise - 0.5)*300.0;
  float wSnow = smoothstep(snowLine - 60.0, snowLine + 60.0, p.y) * smoothstep(0.55, 0.3, slope);
  wSnow = max(wSnow, uSnow*smoothstep(0.6, 0.35, slope)*step(1.0, p.y));
  float wRock = smoothstep(0.32, 0.5, slope + (n2-0.5)*0.15);
  float wSand = smoothstep(4.5 + 3.0*hNoise, 1.0, p.y) * (1.0 - wRock);
  float forestN = fbm2(p.xz/1400.0 + 3.1, 5);
  float wForest = smoothstep(0.48 - lush*0.12, 0.56 - lush*0.12, forestN) * smoothstep(0.28, 0.15, slope) * smoothstep(5.0, 25.0, p.y) * smoothstep(1500.0 - cold*900.0, 900.0 - cold*600.0, p.y);
  float wDirt = smoothstep(0.55, 0.7, n2) * (1.0 - wForest) * 0.6;
  vec4 g = matSample(p.xz, M_GRASS, 6.0, nTS); vec3 nG = nTS;
  vec3 grassTint = mix(vec3(0.75,0.85,0.45), vec3(0.55,0.95,0.45), lush) * mix(vec3(1.0), vec3(1.15,1.0,0.8), smoothstep(0.4,0.8,hNoise));
  grassTint = mix(grassTint, vec3(0.75,0.8,0.65), cold*0.6);
  m.alb = g.rgb*grassTint; m.rough = g.a; m.nrm = nG;
  if (wDirt > 0.01) { vec4 d = matSample(p.xz, M_DIRT, 7.0, nTS); m.alb = mix(m.alb, d.rgb, wDirt); m.rough = mix(m.rough, d.a, wDirt); m.nrm = mix(m.nrm, nTS, wDirt); }
  if (wForest > 0.01) { vec4 f = matSample(p.xz, M_FOREST, 14.0, nTS);
    vec3 ft = mix(vec3(0.7,0.85,0.6), vec3(0.55,1.0,0.6), lush); ft = mix(ft, vec3(0.6,0.75,0.7), cold);
    m.alb = mix(m.alb, f.rgb*ft, wForest); m.rough = mix(m.rough, 0.85, wForest); m.nrm = mix(m.nrm, nTS*vec3(2.0,2.0,1.0), wForest); }
  if (wSand > 0.01) { vec4 s = matSample(p.xz, M_SAND, 6.0, nTS); m.alb = mix(m.alb, s.rgb*mix(vec3(1.0), vec3(1.08,1.04,0.95), lush), wSand); m.rough = mix(m.rough, s.a, wSand); m.nrm = mix(m.nrm, nTS, wSand); }
  if (wRock > 0.01) {
    // triplanar rock on cliffs
    vec3 bw = pow(abs(n), vec3(4.0)); bw /= dot(bw, vec3(1.0));
    vec3 n1, n2_, n3;
    vec4 r = matSample(p.zy, M_ROCK, 18.0, n1)*bw.x + matSample(p.xz, M_ROCK, 18.0, n2_)*bw.y + matSample(p.xy, M_ROCK, 18.0, n3)*bw.z;
    vec3 rockTint = mix(vec3(1.0,0.95,0.88), vec3(0.75,0.72,0.72), cold);
    if (base.y > 300.0 && lush > 0.9) rockTint = vec3(0.55,0.5,0.5);  // volcanic basalt
    m.alb = mix(m.alb, r.rgb*rockTint, wRock); m.rough = mix(m.rough, r.a, wRock); m.nrm = mix(m.nrm, n1*bw.x + n2_*bw.y + n3*bw.z, wRock);
  }
  if (wSnow > 0.01) { vec4 s = matSample(p.xz, M_SNOW, 8.0, nTS); m.alb = mix(m.alb, s.rgb, wSnow); m.rough = mix(m.rough, s.a, wSnow); m.nrm = mix(m.nrm, nTS, wSnow); }
  // airport surfaces
  vec2 auv; int ai = airportAt(p.xz, auv);
  if (ai >= 0) {
    bool onRw;
    runwayMaterial(ai, auv, m, p, onRw);
    // town around larger airports
    float size = uApDim[ai].w;
    float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
    if (size > 0.5 && !onRw) {
      vec2 tv = vec2(auv.x, auv.y*side);
      float town = smoothstep(560.0, 420.0, abs(tv.x)) * smoothstep(250.0, 330.0, tv.y);
      if (town > 0.01 && p.y > 2.0) {
        vec2 cell = floor(tv/vec2(26.0, 22.0)); vec2 f = fract(tv/vec2(26.0, 22.0));
        float hsh = hash2i(ivec2(cell) + ivec2(ai*131));
        float roof = step(0.18, f.x)*step(f.x, 0.82)*step(0.2, f.y)*step(f.y, 0.8)*step(0.3, hsh);
        vec3 rc = hsh > 0.7 ? vec3(0.55,0.22,0.15) : hsh > 0.5 ? vec3(0.5,0.5,0.52) : vec3(0.72,0.66,0.58);
        m.alb = mix(m.alb, mix(vec3(0.3,0.3,0.32), rc, roof), town);
        m.rough = mix(m.rough, 0.7, town);
        m.emit += vec3(1.0,0.7,0.35)*roof*town*step(0.75, fract(hsh*13.0))*uNight*4.0;
        m.emit += vec3(1.0,0.75,0.4)*town*(1.0-roof)*step(0.94, fract((f.x+f.y)*3.0))*uNight*1.0;
      }
    }
  }
  // wet look in rain
  m.alb *= 1.0 - 0.35*uWet*(1.0-wSnow);
  m.rough = mix(m.rough, m.rough*0.35, uWet*(1.0-wSnow));
  return m;
}

vec3 applyTS(vec3 n, vec3 nTS, float strength){
  vec3 t = normalize(cross(n, vec3(0.0,0.0,1.0)));
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
  // aircraft landing light
  if (uLandLight > 0.0) {
    vec3 lv = p - uLandLightPos; float ld = length(lv); vec3 l = lv/ld;
    float cone = smoothstep(0.93, 0.985, dot(l, uLandLightDir));
    col += pbr(n, v, -l, m.alb, m.rough, m.metal, vec3(1.0,0.95,0.85)*cone*uLandLight*9000.0/(ld*ld + 50.0));
  }
  col += m.emit;
  col += m.alb*vec3(0.7,0.75,1.0)*uLightning*0.4;
  return col;
}

vec3 applyFog(vec3 col, vec3 ro, vec3 rd, float t){
  // height-dependent haze (analytic integral of exp(-y/H))
  float H = 1400.0;
  float b = 1.0/H;
  float dens = uFogB * exp(-max(ro.y, 0.0)*b);
  float fogAmt = rd.y*t*b > 1e-4 ? dens*(1.0 - exp(-t*rd.y*b))/(rd.y*b) : dens*t;
  fogAmt = 1.0 - exp(-fogAmt - t*uFogB*0.04);
  float mu = max(dot(rd, uSunDir), 0.0);
  vec3 fogCol = skyColor(normalize(vec3(rd.x, 0.03, rd.z)));
  fogCol += uSunCol*pow(mu, 8.0)*0.25;
  return mix(col, fogCol, clamp(fogAmt, 0.0, 1.0));
}

// buildings
vec2 traceBoxes(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out float kind, out vec3 localHit){
  float best = tmax; vec2 res = vec2(-1.0);
  for (int i=0;i<64;i++){
    if (i >= uBoxCount) break;
    int ai = int(uBoxC[i].w);
    vec4 a = uAp[ai];
    float s = sin(a.w), c = cos(a.w);
    // world -> runway frame: x = across (v), z = along (u)
    vec3 o = ro - vec3(a.x, a.z, a.y);
    vec3 lo = vec3(o.x*c + o.z*s, o.y, o.x*s - o.z*c) - uBoxC[i].xyz;
    vec3 ld = vec3(rd.x*c + rd.z*s, rd.y, rd.x*s - rd.z*c);
    vec3 m = 1.0/ld; vec3 k = abs(m)*uBoxH[i].xyz; vec3 n = m*lo;
    vec3 t1 = -n - k, t2 = -n + k;
    float tN = max(max(t1.x, t1.y), t1.z), tF = min(min(t2.x, t2.y), t2.z);
    if (tN > tF || tF < 0.0 || tN > best) continue;
    best = tN; res = vec2(tN, float(i)); kind = uBoxH[i].w;
    vec3 nl = -sign(ld)*step(t1.yzx, t1.xyz)*step(t1.zxy, t1.xyz);
    // back to world
    nOut = vec3(nl.x*c + nl.z*s, nl.y, nl.x*s - nl.z*c);
    localHit = lo + ld*tN;
  }
  return res;
}

void main(){
  vec2 ndc = vUV*2.0 - 1.0;
  vec3 rd = normalize(uCamRot * vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 ro = uCamPos;
  float jitter = hash1(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + fract(uTime)*7.0);
  float tmax = 80000.0;
  float tT = traceTerrain(ro, rd, tmax);
  float tW = (rd.y < 0.0 && ro.y > 0.0) ? -ro.y/rd.y : -1.0;
  vec3 bn; float bkind = 0.0; vec3 bl;
  vec2 bh = traceBoxes(ro, rd, tT > 0.0 ? tT : tmax, bn, bkind, bl);
  vec2 ph = tracePlane(ro, rd, tmax);
  float t = 1e9; int hit = 0;
  if (tT > 0.0) { t = tT; hit = 1; }
  if (tW > 0.0 && tW < t) { t = tW; hit = 2; }
  if (bh.x > 0.0 && bh.x < t) { t = bh.x; hit = 3; }
  if (ph.x > 0.0 && ph.x < t) { t = ph.x; hit = 4; }
  vec3 col;
  if (hit == 0) { col = skyColor(rd); t = 1e6; }
  else {
    vec3 p = ro + rd*t;
    float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
    if (hit == 1) {
      vec3 n = terrainNormal(p.xz, t);
      vec4 base = baseAt(p.xz);
      Mat m = terrainMaterial(p, n, t, base);
      vec3 ns = applyTS(n, m.nrm, t < 2000.0 ? 0.6 : 0.25);
      float sh = sunVis > 0.0 ? terrainShadow(p + n*1.0, uSunDir) : 0.0;
      if (t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
      sh *= cloudShadow(p);
      col = shadeSurface(p, ns, rd, m, sh);
    } else if (hit == 2) {
      // ocean
      float depth = max(-terrainH(p.xz, 5), 0.0);
      vec2 w = p.xz*0.05 + uTime*vec2(0.3, 0.2);
      vec3 n1 = noised(w*1.0); vec3 n2 = noised(w*3.1 + 5.0); vec3 n3 = noised(p.xz*0.9 + uTime*vec2(-0.9, 0.7));
      float amp = 0.12 + 0.12*uStorm + 0.04*uWet;
      vec3 n = normalize(vec3(-(n1.y*0.6 + n2.y*0.3)*amp - n3.y*0.03*smoothstep(400.0, 0.0, t), 1.0, -(n1.z*0.6 + n2.z*0.3)*amp - n3.z*0.03*smoothstep(400.0, 0.0, t)));
      n = normalize(mix(n, vec3(0,1,0), smoothstep(800.0, 12000.0, t)*0.8));
      vec3 v = -rd;
      float fk = clamp(1.0 - dot(n, v), 0.0, 1.0); float fres = 0.02 + 0.98*fk*fk*fk*fk*fk;
      vec3 r = reflect(rd, n); r.y = abs(r.y);
      vec3 refl = skyColor(r);
      // reflected clouds (cheap)
      if (uCloudCover > 0.05 && uQuality > 0) { vec4 cl = traceClouds(p, r, 30000.0, 0.5); refl = refl*cl.a + cl.rgb; }
)"
R"(      float sh = sunVis > 0.0 ? terrainShadow(p + vec3(0,1,0), uSunDir) * cloudShadow(p) : 0.0;
      vec4 base = baseAt(p.xz);
      vec3 deep = mix(vec3(0.004,0.03,0.06), vec3(0.003,0.02,0.035), base.w);
      vec3 shallow = mix(vec3(0.02,0.16,0.17), vec3(0.03,0.30,0.29), base.z) * (1.0 - 0.7*base.w);
      vec3 water = mix(shallow, deep, smoothstep(0.0, 18.0, depth));
      vec3 lit = water*(uSunCol*max(uSunDir.y,0.0)*1.0*sh + ambientLight(vec3(0,1,0))*0.35);
      float foam = smoothstep(0.7, 0.0, depth) * smoothstep(0.55, 0.85, vnoise(p.xz*0.15 + uTime*0.4) + 0.3*sin(depth*4.0 - uTime*1.5));
      lit = mix(lit, vec3(0.85)*(uSunCol*max(uSunDir.y,0.0)*1.5 + ambientLight(vec3(0,1,0))), foam*0.8);
      vec3 h = normalize(v + uSunDir);
      float spec = pow(max(dot(n, h), 0.0), 900.0)*120.0 + pow(max(dot(n,h),0.0), 90.0)*1.5;
      col = mix(lit, refl, fres) + uSunCol*spec*sh*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
      // night: runway/town light reflections handled by overlay pass glow
    } else if (hit == 3) {
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
      int k = int(bkind);
      vec3 lh = bl;
      if (k == 0) { // hangar: corrugated metal
        m.alb = vec3(0.62,0.64,0.66); m.rough = 0.45; m.metal = 0.6;
        if (abs(bn.y) < 0.5 && fract(lh.z*1.2) < 0.15) m.alb *= 0.85;
        if (bn.y > 0.5) m.alb = vec3(0.45,0.45,0.48);
      } else if (k == 1) { // control tower
        m.alb = vec3(0.85,0.85,0.82); m.rough = 0.6;
        if (lh.y > 0.62*uBoxH[int(bh.y)].y*2.0 - uBoxH[int(bh.y)].y) { m.alb = vec3(0.05,0.08,0.1); m.rough = 0.05; m.emit = vec3(0.4,0.8,0.5)*uNight*0.6; }
      } else if (k == 2) { // terminal with windows
        m.alb = vec3(0.8,0.78,0.74); m.rough = 0.55;
        if (abs(bn.y) < 0.5 && fract(lh.y/3.0) > 0.35 && fract(lh.y/3.0) < 0.8) { m.alb = vec3(0.06,0.09,0.12); m.rough = 0.08; m.emit = vec3(1.0,0.85,0.6)*uNight*1.2; }
      } else { m.alb = vec3(0.85,0.82,0.75); m.rough = 0.8; if (bn.y > 0.5) m.alb = vec3(0.6,0.25,0.18);
        if (abs(bn.y) < 0.5 && fract(lh.z/2.5) > 0.6 && lh.y > -1.0 && lh.y < 1.0) m.emit = vec3(1.0,0.8,0.5)*uNight*2.0; }
      float sh = sunVis > 0.0 ? terrainShadow(p + bn*0.5, uSunDir) : 0.0;
      col = shadeSurface(p, bn, rd, m, sh*cloudShadow(p));
    } else {
      // aircraft
      mat3 inv = transpose(uPlaneRot);
      vec3 lp = inv*(p - uPlanePos);
      vec3 ln = planeNormal(lp);
      vec3 n = uPlaneRot*ln;
      int mid = int(ph.y + 0.5);
      if (mid == 11) { vec3 sc = fusSection(lp.z); vec3 rad = vec3(lp.x, lp.y - sc.z, 0.0); if (dot(ln, rad) > 0.55*length(rad) && lp.y > uM[22].y - 0.9) mid = 1; }
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
      m.alb = uColBase; m.rough = 0.28;
      bool interior = mid >= 10 && mid <= 14;
      vec3 sec = fusSection(lp.z);
      vec4 WS = uM[23]; vec4 E = uM[22];
      int ck = int(uM[21].z + 0.5);
      if (mid == 1) {
        float yr = (lp.y - sec.z)/sec.y;
        bool body = lp.z > uM[1].x + 0.05 && lp.z < uM[8].x - 0.05 && abs(lp.x) < sec.x + 0.05 && abs(yr) < 1.05;
        if (body) {
          if (yr > -0.22 && yr < 0.0 && lp.z > uM[2].x) m.alb = uColStripe;
          if (yr > 0.06 && yr < 0.11 && lp.z > uM[2].x) m.alb = mix(uColStripe, vec3(1.0), 0.35);
          if (yr < -0.72) m.alb = mix(uColBase, vec3(0.62, 0.64, 0.66), 0.5);
          if (fract(lp.z/0.85) < 0.01) m.alb *= 0.8;
          float post = ck == 2 ? min(abs(lp.x) - 0.03, abs(abs(lp.x) - abs(E.x) - 0.42) - 0.035) : abs(lp.x) - 0.025;
          bool ws = lp.z > WS.x && lp.z < WS.y && lp.y > WS.z;
          float sideTop = sec.z + sec.y*0.78;
          bool sideW = lp.z > WS.y && lp.z < WS.w && lp.y > WS.z - 0.12 && lp.y < sideTop && abs(lp.x) > 0.3 && abs(lp.z - WS.y - 0.04) > 0.025;
          bool frame = (ws && post <= 0.0) || (lp.z > WS.x - 0.03 && lp.z < WS.w + 0.03 && lp.y > WS.z - 0.15 && lp.y < sideTop + 0.03 && abs(lp.x) > 0.3 && !sideW && lp.z > WS.y);
          if ((ws && post > 0.0) || sideW) { m.alb = vec3(0.012, 0.016, 0.02); m.rough = 0.03; m.metal = 0.2; }
          else if (frame) m.alb *= 0.55;
          int nw = int(uM[20].x + 0.5);
          if (nw > 0 && abs(lp.x) > sec.x*0.4 && lp.z > uM[20].y && lp.z < uM[20].z) {
            float pw = (uM[20].z - uM[20].y)/float(nw);
            vec2 wq = vec2(mod(lp.z - uM[20].y, pw) - pw*0.5, lp.y - (sec.z + uM[20].w));
            vec2 hs = uM[21].xy; float rr = min(hs.x, hs.y)*0.7;
            vec2 dq = abs(wq) - hs + rr; float wd = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - rr;
            if (wd < 0.0) { m.alb = vec3(0.02, 0.025, 0.03); m.rough = 0.05; m.emit = vec3(1.0, 0.85, 0.6)*uNight*0.5; }
            else if (wd < 0.022) m.alb *= 0.7;
          }
          if (ck == 2 && lp.z < WS.x && lp.z > WS.x - 2.0 && yr > 0.2) { m.alb = vec3(0.02); m.rough = 0.85; }
          if (int(uM[0].z + 0.5) == 0 && lp.z < uM[1].x + 0.3 && ln.z < -0.4 && abs(lp.x) > 0.11 && abs(lp.x) < sec.x*0.8 && abs(yr + 0.15) < 0.35) m.alb = vec3(0.02);
          if (int(uM[0].z + 0.5) == 1 && lp.z < uM[1].x + 0.7 && lp.y < sec.z - sec.y*0.45 && ln.z < -0.3) m.alb = vec3(0.02);
        }
      } else if (mid == 2) {
        float s = abs(lp.x); float k = clamp(s/uM[9].x, 0.0, 1.0);
        float ch = mix(uM[9].y, uM[9].z, k); float le = uM[9].w*k;
        float cc = (lp.z - uM[10].y - le)/ch;
        m.alb = uColBase*0.98;
        if (s > uM[9].x*0.9) m.alb = uColStripe;
        if (uM[19].w > 0.5 && cc < 0.045) { m.alb = vec3(0.06); m.rough = 0.6; }
        if (fract(s/0.8) < 0.01 && cc > 0.05) m.alb *= 0.86;
        if (uM[11].x > 0.5 && s < 1.6 && ln.y > 0.5 && cc < 0.7) m.alb *= 0.9;
      } else if (mid == 3) {
        m.alb = uColBase;
        float tailTop = uM[15].x + uM[14].x;
        if (lp.y > uM[15].x + uM[14].x*0.5 && abs(lp.x) < 0.25) m.alb = uColStripe;
        if (lp.y > tailTop - 0.12 && abs(lp.x) < 0.25) m.alb = vec3(0.9);
      } else if (mid == 5) {
        m.alb = uColBase*0.96; m.rough = 0.3;
        if (int(uM[0].z + 0.5) == 4 && lp.z < uM[16].w + 0.3) { m.alb = vec3(0.85); m.metal = 1.0; m.rough = 0.18; }
      } else if (mid == 6) { m.alb = vec3(0.025); m.rough = 0.85; }
      else if (mid == 8) { m.alb = uM[11].x > 0.5 && length(lp.xz) > 1.2 && lp.y > -0.3 ? uColBase*0.95 : vec3(0.6, 0.61, 0.63); m.metal = 0.5; m.rough = 0.35; }
      else if (mid == 10) {
        m.alb = vec3(0.075); m.rough = 0.6;
        if (ln.z > 0.6) {
          bool pilot = lp.x*E.x >= 0.0;
          vec2 q = vec2(pilot ? lp.x - E.x : lp.x + E.x, lp.y - (E.y - 0.32));
          if (!pilot && ck == 0) q.x = lp.x + E.x - 0.33 + 0.33;
          vec3 ic = drawInstruments(q, ck, pilot);
          if (ic.x >= 0.0) { m.alb = ic*0.25; m.emit = ic*(0.3 + 0.6*uNight); m.rough = 0.12; }
        }
      }
      else if (mid == 11) { m.alb = lp.y < E.y - 1.0 ? vec3(0.08, 0.08, 0.09) : vec3(0.5, 0.49, 0.46); m.rough = 0.85; }
      else if (mid == 12) { m.alb = vec3(0.09, 0.1, 0.14)*(0.9 + 0.2*step(0.5, fract(lp.y*12.0))); m.rough = 1.0; }
      else if (mid == 13) { m.alb = vec3(0.035); m.rough = 0.4; }
      else if (mid == 14) { m.alb = vec3(0.018); m.rough = 0.95; }
      else if (mid == 16) { m.alb = uM[0].x > 9.0 ? uColBase*0.9 : uColStripe; m.metal = 0.5; m.rough = 0.2; }
      else if (mid == 17) { m.alb = vec3(0.09, 0.075, 0.06); m.metal = 0.7; m.rough = 0.55; }
      else if (mid == 18) { m.alb = vec3(0.1); m.emit = (lp.x < 0.0 ? vec3(1.0, 0.05, 0.02) : vec3(0.05, 1.0, 0.15))*(0.5 + 2.0*uNight); m.rough = 0.1; }
      else if (mid == 19) { m.alb = vec3(0.3, 0.02, 0.02); m.emit = vec3(1.0, 0.05, 0.02)*step(0.88, fract(uTime))*3.0; m.rough = 0.1; }
      else if (mid == 21) {
        vec2 fq = vec2(abs(lp.x) - uM[16].x, lp.y - uM[16].y);
        float bl = step(0.5, fract(atan(fq.y, fq.x)*22.0/6.2832 + length(fq)*2.0));
        m.alb = mix(vec3(0.04), vec3(0.22), bl); m.metal = 0.9; m.rough = 0.3;
        if (length(fq) < uM[16].z*0.25) m.alb = vec3(0.05);
      }
      float sh = sunVis > 0.0 ? planeShadow(p + n*0.02, uSunDir) * terrainShadow(p, uSunDir) * cloudShadow(p) : 0.0;
      if (interior) {
        vec3 v = -rd;
        col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*sh*3.2) + m.alb*(ambientLight(n)*0.45 + ambientLight(vec3(0.0,1.0,0.0))*0.25) + m.emit;
      } else {
        col = shadeSurface(p, n, rd, m, sh);
        vec3 h = normalize(-rd + uSunDir);
        col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0*float(mid <= 5);
      }
    }
    col = applyFog(col, ro, rd, t);
  }
  // propeller discs (motion-blurred), composited over scene
  if (uPlaneOn == 1) {
    mat3 inv = transpose(uPlaneRot);
    vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
    for (int i=0;i<2;i++){
      if (i >= uPropCount) break;
      vec4 pr = uProp[i];
      if (abs(ld.z) < 1e-4) continue;
      float tp = (pr.z - lo.z)/ld.z;
      if (tp < 0.0 || tp > t) continue;
      vec3 hp = lo + ld*tp - pr.xyz;
      float r = length(hp.xy);
      if (r > pr.w) continue;
      float blades = uPr.z; float blur = uPr.y;
      float ang = atan(hp.y, hp.x) - uPr.x;
      float bl = smoothstep(0.86, 0.95, cos(blades*ang*0.5*2.0)) * smoothstep(pr.w, pr.w*0.9, r);
      float a = mix(bl, 0.10 + 0.08*smoothstep(0.6, 1.0, cos(blades*ang)) + 0.25*smoothstep(pr.w*0.95, pr.w, r), blur);
      vec3 pc = vec3(0.04)*(uSunCol*max(uSunDir.y,0.0) + 0.2) + vec3(0.6,0.6,0.1)*smoothstep(pr.w*0.9, pr.w, r)*0.3;
      col = mix(col, pc, clamp(a, 0.0, 1.0)*0.85);
    }
  }
  // clouds
  vec4 cl = traceClouds(ro, rd, t, jitter);
  col = col*cl.a + cl.rgb;
  if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
  oColor = vec4(col, 1.0);
  oDepth = t;
}
)";

// ------------------------------------------------------------------------------------------------
// Overlay pass: world-space sprites (particles, lights, rings) depth-tested against the ray-traced depth
static const char* kSpriteVS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec2 aKind;
uniform mat4 uViewProj; uniform vec3 uCamPos;
out vec2 vUV; out vec4 vCol; out float vDist; out vec2 vKind; out vec3 vWorld;
void main(){ vUV = aUV; vCol = aCol; vKind = aKind; vWorld = aPos; vDist = length(aPos - uCamPos); gl_Position = uViewProj*vec4(aPos, 1.0); }
)";
static const char* kSpriteFS = R"(#version 330 core
in vec2 vUV; in vec4 vCol; in float vDist; in vec2 vKind; in vec3 vWorld;
out vec4 oColor;
uniform sampler2D uDepth; uniform vec2 uRes; uniform vec3 uSunDir; uniform vec3 uSunCol; uniform vec3 uAmb; uniform float uFogB;
void main(){
  float sceneT = texture(uDepth, gl_FragCoord.xy/uRes).r;
  int kind = int(vKind.x + 0.5);
  float soft = clamp((sceneT - vDist)/max(vKind.y, 0.05), 0.0, 1.0);
  if (soft <= 0.0) discard;
  vec2 c = vUV*2.0 - 1.0; float r2 = dot(c,c);
  vec4 o;
  if (kind == 0) {        // soft smoke / dust puff (lit)
    float a = smoothstep(1.0, 0.0, r2); a *= a;
    vec3 n = normalize(vec3(c, sqrt(max(1.0-r2, 0.0))));
    float l = 0.55 + 0.45*max(dot(n, normalize(uSunDir + vec3(0,0.3,0))), 0.0);
    o = vec4(vCol.rgb*(uSunCol*l*1.2*max(uSunDir.y+0.1,0.0) + uAmb), vCol.a*a);
  } else if (kind == 1) { // additive glow light
    float a = exp(-r2*6.0) + 0.15*exp(-r2*1.5);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 2) { // waypoint ring
    float r = sqrt(r2);
    float a = smoothstep(0.08, 0.0, abs(r - 0.88)) + 0.25*smoothstep(0.25, 0.0, abs(r - 0.88));
    float chev = step(0.75, r)*step(r, 1.0)*step(0.5, fract(atan(c.y,c.x)*8.0/6.2831));
    o = vec4(vCol.rgb*(a + chev*0.2)*vCol.a, 0.0);
  } else if (kind == 3) { // rain streak
    float a = smoothstep(1.0, 0.0, abs(c.x)) * smoothstep(1.0, 0.6, abs(c.y));
    o = vec4(vCol.rgb*(uAmb*2.0 + 0.1), vCol.a*a);
  } else if (kind == 4) { // fire (additive)
    float a = smoothstep(1.0, 0.0, r2);
    o = vec4(vCol.rgb*a*a*vCol.a*3.0, 0.0);
  } else {                // snow flake
    float a = smoothstep(1.0, 0.2, r2);
    o = vec4(vCol.rgb*(uAmb*2.5 + uSunCol*0.3), vCol.a*a);
  }
  float fog = exp(-vDist*uFogB*0.5);
  o.rgb *= fog; o.a *= mix(1.0, fog, 0.5);
  o.a *= soft; if (kind == 1 || kind == 2 || kind == 4) o.rgb *= soft;
  oColor = o;
}
)";

// ------------------------------------------------------------------------------------------------
static const char* kBrightFS = R"(#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uTexel;
void main(){
  vec3 c = vec3(0.0);
  for (int i=-1;i<=1;i++) for (int j=-1;j<=1;j++) c += texture(uTex, vUV + vec2(i,j)*uTexel).rgb;
  c /= 9.0;
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  oColor = vec4(c*smoothstep(1.2, 4.0, l)/max(l,1e-3)*min(l, 40.0)*0.25, 1.0);
}
)";
static const char* kBlurFS = R"(#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uDir;
void main(){
  const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
  vec3 c = texture(uTex, vUV).rgb*w[0];
  for (int i=1;i<5;i++){ c += texture(uTex, vUV + uDir*float(i)*1.5).rgb*w[i]; c += texture(uTex, vUV - uDir*float(i)*1.5).rgb*w[i]; }
  oColor = vec4(c, 1.0);
}
)";
static const char* kPostFS = R"(#version 330 core
in vec2 vUV; out vec4 oColor;
uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uTime; uniform vec2 uRes;
uniform float uRainLens; uniform vec2 uSunScreen; uniform float uSunVisible; uniform float uFade; uniform float uVignette;
vec3 aces(vec3 x){ const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14; return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0); }
float h21(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
void main(){
  vec2 uv = vUV;
  // raindrops on the lens
  if (uRainLens > 0.0) {
    vec2 g = uv*vec2(uRes.x/uRes.y, 1.0)*9.0; vec2 id = floor(g); vec2 f = fract(g) - 0.5;
    float r = h21(id); float life = fract(uTime*0.15 + r);
    vec2 o = vec2(h21(id+1.3)-0.5, h21(id+2.7)-0.5)*0.6;
    float d = length(f - o);
    float drop = smoothstep(0.14, 0.06, d) * step(0.55, r) * (1.0 - life) * uRainLens;
    uv += (f - o)*drop*0.08;
  }
  vec3 c = texture(uScene, uv).rgb + texture(uBloom, uv).rgb*0.9;
  // subtle sun glare / lens flare ghosts
  if (uSunVisible > 0.0) {
    vec2 sd = uv - uSunScreen; sd.x *= uRes.x/uRes.y;
    c += vec3(1.0,0.85,0.6)*exp(-dot(sd,sd)*12.0)*0.35*uSunVisible;
    for (int i=1;i<4;i++){ vec2 gp = mix(uSunScreen, vec2(0.5), float(i)*0.55); vec2 gd = (uv - gp)*vec2(uRes.x/uRes.y,1.0);
      c += vec3(0.4,0.6,1.0)*smoothstep(0.05*float(i), 0.0, length(gd))*0.03*uSunVisible; }
  }
  c *= uExposure;
  c = aces(c);
  c = pow(c, vec3(1.0/2.2));
  vec2 vv = vUV - 0.5; c *= 1.0 - dot(vv,vv)*uVignette;
  c += (h21(vUV*uRes + fract(uTime)*100.0) - 0.5)/255.0*2.0;
  c *= uFade;
  oColor = vec4(c, 1.0);
}
)";

// ------------------------------------------------------------------------------------------------
static const char* kUIVS = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec3 aMode;
uniform vec2 uScreen; out vec2 vUV; out vec4 vCol; out float vMode; out vec2 vHalf;
void main(){ vUV = aUV; vCol = aCol; vMode = aMode.x; vHalf = aMode.yz; gl_Position = vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }
)";
static const char* kUIFS = R"(#version 330 core
in vec2 vUV; in vec4 vCol; in float vMode; in vec2 vHalf; out vec4 oColor;
uniform sampler2D uFont; uniform sampler2D uImg;
void main(){
  if (vMode < 0.5) { oColor = vCol; }
  else if (vMode < 1.5) {
    float d = texture(uFont, vUV).r;
    float w = fwidth(d)*0.75;
    float a = smoothstep(0.5 - w, 0.5 + w, d);
    oColor = vec4(vCol.rgb, vCol.a*a);
  } else if (vMode < 2.5) {
    float d = texture(uFont, vUV).r;  // soft shadow for text
    oColor = vec4(0.0, 0.0, 0.0, vCol.a*smoothstep(0.25, 0.55, d)*0.6);
  } else if (vMode < 3.5) { oColor = texture(uImg, vUV)*vCol; }
  else { float r = (vMode - 4.0)*1000.0; vec2 q = abs(vUV) - vHalf + r; float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    oColor = vec4(vCol.rgb, vCol.a*clamp(0.5 - d, 0.0, 1.0)); }
}
)";
