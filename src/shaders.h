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
uniform vec4 uCrater;  // impact crater: x, z, radius, depth (radius 0 = none)
float craterH(vec2 p){ if (uCrater.z <= 0.0) return 0.0; float d = length(p - uCrater.xy)/uCrater.z; if (d > 1.8) return 0.0;
  return -uCrater.w*max(1.0 - d*d, 0.0) + 0.22*uCrater.w*exp(-(d - 1.0)*(d - 1.0)*14.0); }
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
float vnoise(vec2 x){ vec2 i=floor(x), f=fract(x); f=f*f*(3.0-2.0*f);
  return mix(mix(hash2i(ivec2(i)),hash2i(ivec2(i)+ivec2(1,0)),f.x), mix(hash2i(ivec2(i)+ivec2(0,1)),hash2i(ivec2(i)+ivec2(1,1)),f.x), f.y); }
float vnoise3(vec3 x){ vec3 i=floor(x), f=fract(x); f=f*f*(3.0-2.0*f);
  return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x), mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),
             mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x), mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y), f.z); }
float fbm2(vec2 p, int oct){ float s=0.0, a=0.5; for(int i=0;i<8;i++){ if(i>=oct) break; s+=a*vnoise(p); p=p*2.02+vec2(13.7,-7.1); a*=0.5; } return s; }

// ---------------------------------------------------------------- scenery (mirrors scenery.cpp exactly)
uniform sampler2D uMask; uniform sampler2D uRoadId;
const int MASKN = 2048; const float MTEX = 39.0625; const float SCENERY_DENSITY = 0.3333;
vec4 maskAt(vec2 p){
  vec2 f = (p + WH)/MTEX - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl; ivec2 mx = ivec2(MASKN-1);
  vec4 a = texelFetch(uMask, clamp(i, ivec2(0), mx), 0), b = texelFetch(uMask, clamp(i+ivec2(1,0), ivec2(0), mx), 0);
  vec4 c = texelFetch(uMask, clamp(i+ivec2(0,1), ivec2(0), mx), 0), d = texelFetch(uMask, clamp(i+ivec2(1,1), ivec2(0), mx), 0);
  return (a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y;
}
float forestAt(vec2 p){
  vec2 f = (p + WH)/MTEX - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl; ivec2 mx = ivec2(MASKN-1);
  float a = texelFetch(uRoadId, clamp(i, ivec2(0), mx), 0).g, b = texelFetch(uRoadId, clamp(i+ivec2(1,0), ivec2(0), mx), 0).g;
  float c = texelFetch(uRoadId, clamp(i+ivec2(0,1), ivec2(0), mx), 0).g, d = texelFetch(uRoadId, clamp(i+ivec2(1,1), ivec2(0), mx), 0).g;
  return ((a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y)*0.875;
}
vec4 maskTexel(vec2 p){ return texelFetch(uMask, clamp(ivec2(floor((p + WH)/MTEX)), ivec2(0), ivec2(MASKN-1)), 0); }
float groundH(vec2 p, int oct){ vec4 b = baseAt(p); return b.y < 0.01 ? b.x : b.x + b.y*terrainFbm(p/2200.0, oct); }
// kinds: 1 conifer, 2 broadleaf, 3 palm, 4 rock, 5 sea stack
float coverH(vec2 p, float g, vec4 b, out int kind){
  kind = 0; float best = 0.0;
  vec4 m = maskAt(p);
  if (g < 0.3) {
    if (m.w > 0.5) {
      ivec2 c = ivec2(floor(p/70.0));
      float depthOk = smoothstep(-16.0, -4.0, g)*smoothstep(-0.5, -2.5, g);
      if (hash2i(ivec2(c.x*7+3, c.y*11-5)) < 0.05*SCENERY_DENSITY*depthOk) {
        vec2 cc = (vec2(c) + 0.5 + (vec2(hash2i(c+ivec2(17,0)), hash2i(c+ivec2(0,23))) - 0.5)*0.4)*70.0;
        float r = 7.0 + 8.0*hash2i(c+ivec2(0,31)), H = 14.0 + 30.0*hash2i(c+ivec2(5,9));
        float top = H*smoothstep(r, r*0.7, length(p - cc)*(0.85 + 0.3*vnoise(p*0.12)));
        if (top - g > best) { best = top - g; kind = 5; }
      }
    }
    return best;
  }
  float lush = b.z, cold = b.w, amp = b.y;
  float roadD = m.x*80.0, town = m.y, farm = m.w;
  float fn = forestAt(p);
  float treeline = smoothstep(1500.0 - cold*900.0, 1100.0 - cold*700.0, g);
  float fd = smoothstep(0.42 - 0.1*lush, 0.5 - 0.1*lush, fn)*treeline;
  fd = max(fd, 0.04*treeline);
  fd *= smoothstep(4.0, 9.0, g)*smoothstep(2.5, 8.0, amp)*smoothstep(9.0, 18.0, roadD)*(1.0 - smoothstep(0.03, 0.2, town))*(1.0 - 0.88*farm);
  if (fd > 0.001) {
    ivec2 c = ivec2(floor(p/10.0));
    if (hash2i(ivec2(c.x*3+11, c.y*5-7)) < fd*0.9*SCENERY_DENSITY) {
      vec2 j = vec2(hash2i(c+ivec2(101,-31)), hash2i(c+ivec2(-57,77))) - 0.5;
      vec2 cc = (vec2(c) + 0.5 + j*0.3)*10.0;
      if (uCrater.z > 0.0 && length(cc - uCrater.xy) < uCrater.z*1.7) return best;  // flattened by the impact
      float sp = hash2i(ivec2(c.x*13+1, c.y*7+3)), hv = hash2i(c+ivec2(-3,19));
      bool conifer = cold > 0.45 || g > 650.0 || sp < 0.18;
      bool palm = !conifer && lush > 0.85 && g < 70.0 && sp > 0.35;
      int k; float r, H;
      if (conifer) { k = 1; r = 2.2 + 1.0*hv; H = 9.0 + 9.0*hv; }
      else if (palm) { k = 3; r = 2.4 + 0.6*hv; H = 7.0 + 5.0*hv; }
      else { k = 2; r = 3.6 + 1.6*hv; H = 6.0 + 6.0*hv; }
      vec2 dd = p - cc; float d = length(dd);
      float th = 0.0;
      if (d < r) {
        float q = d/r;
        if (k == 1) th = H*(1.0 - q);
        else if (k == 2) th = H*(0.22 + 0.78*sqrt(1.0 - q*q));
        else { float star = 0.7 + 0.3*cos(atan(dd.y, dd.x)*7.0); th = q < star ? H*(0.82 + 0.18*(1.0 - q/star)) : 0.0; }
      }
      if (th > best) { best = th; kind = k; }
    }
  }
  float rdn = smoothstep(60.0, 200.0, amp)*0.25 + smoothstep(900.0, 1400.0, g)*0.12 + smoothstep(3.0, 0.5, g)*0.04*(1.0 - farm);
  rdn *= smoothstep(10.0, 20.0, roadD)*(1.0 - smoothstep(0.03, 0.2, town));
  if (rdn > 0.001) {
    ivec2 c = ivec2(floor(p/16.0));
    if (hash2i(ivec2(c.x*5-13, c.y*3+29)) < rdn*SCENERY_DENSITY) {
      vec2 cc = (vec2(c) + 0.5 + (vec2(hash2i(c+ivec2(41,-9)), hash2i(c+ivec2(-21,63))) - 0.5)*0.3)*16.0;
      float hr = hash2i(c+ivec2(7,-77));
      float r = 1.6 + 5.0*hr*hr;
      float d = length(p - cc);
      if (d < r) {
        float q = d/r;
        float th = r*0.75*pow(1.0 - q*q, 0.6)*(0.6 + 0.8*vnoise(p*0.9));
        if (th > best) { best = th; kind = 4; }
      }
    }
  }
  return best;
}
// Full height with trees/rocks scaled by an LOD fade (1 = exact, identical to the CPU collision height).
// rayY lets the marcher skip the cover evaluation when far above the canopy.
float terrainHF(vec2 p, int oct, float fade, float rayY){
  vec4 b = baseAt(p);
  float g = b.y < 0.01 ? b.x : b.x + b.y*terrainFbm(p/2200.0, oct);
  if (uCrater.z > 0.0 && g > 0.3) { float cd = length(p - uCrater.xy)/uCrater.z; if (cd < 1.8) g += craterH(p); }
  // trees <= 18 m and rocks <= 7 m above land, sea stacks <= 44 m: above that the cover cannot be hit
  if (fade <= 0.001 || (b.y < 0.01 && g > 0.5) || rayY - g > (g < 0.3 ? 46.0 : 19.0)) return g;
  int k;
  return g + fade*coverH(p, g, b, k);
}
float terrainH(vec2 p, int oct){ return terrainHF(p, oct, 1.0, -1e9); }

// Scene / atmosphere uniforms
uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uNight; uniform float uTime;
uniform float uCloudCover; uniform float uCloudBase; uniform float uFogB; uniform float uWet; uniform float uSnow;
uniform vec2 uWindOff; uniform float uLightning; uniform float uStorm;

vec3 skyColor(vec3 rd){
  vec3 sd = uSunDir;
  float sunH = sd.y;
  float y = max(rd.y, 0.0);
  // optical depths (cheap analytic approximation of Rayleigh + Mie single scattering)
  float odV = 1.0/(y*1.4 + 0.075);
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
uniform vec2 uJit; uniform float uSeed;  // TAA: sub-pixel jitter (uv units) and a per-frame noise seed
uniform float uMaxH; uniform int uQuality;
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
// airports
uniform int uApCount; uniform vec4 uAp[16]; uniform vec4 uApDim[16];
uniform int uBoxCount;
uniform sampler2D uData;  // static scene data: [0,64) roads, [64,192) box centres, [192,320) box sizes, [320,344) town xz bounds, [352,376) town y range
vec4 dataAt(int i){ return texelFetch(uData, ivec2(i, 0), 0); }
// aircraft
uniform int uPlaneOn; uniform vec3 uPlanePos; uniform mat3 uPlaneRot;
uniform vec4 uHud; uniform vec4 uHud2; uniform vec3 uHudV; uniform vec4 uHud3;  // research jet HUD: ias m/s, alt m, hdg, mach | g, throttle, nozzle, gear | velocity dir (body)
// wreckage: pieces of the airframe, each the aircraft SDF clipped to a body-space box with its own transform
uniform int uWreck; uniform vec3 uPcPos[5]; uniform mat3 uPcRot[5]; uniform vec3 uPcC[5]; uniform vec3 uPcH[5];
uniform int uDebN; uniform vec4 uDeb[16]; uniform vec4 uDebQ[16];
int gPI = -1; vec3 gPP; mat3 gPR; vec3 gPC;   // transform of the piece being traced / shaded
uniform vec4 uM[24]; uniform vec4 uPS; uniform vec4 uCtl; uniform vec4 uPr; uniform vec4 uI0; uniform vec4 uI1; uniform vec4 uI2;
uniform vec3 uColBase; uniform vec3 uColStripe;
uniform vec4 uProp[2]; uniform int uPropCount;
uniform vec3 uLandLightPos; uniform vec3 uLandLightDir; uniform float uLandLight;
uniform vec4 uFlame; uniform vec3 uFlameLP; uniform vec3 uFlameLI;  // research jet exhaust: spool, reheat, vector angle, mach | light pos, radiance

// ---------------------------------------------------------------- materials (texture array layers)
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;
const int M_CONCRETE=8, M_TILES=9, M_SLATE=10, M_PLASTER=11, M_BRICK=12, M_LEAVES=13, M_NEEDLES=14, M_PAINT=15;
const int M_METAL=16, M_RUBBER=17, M_PLASTIC=18, M_FABRIC=19, M_CARPET=20, M_LEATHER=21, M_CORRUGATED=22, M_CROP=23, M_WHEAT=24;

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

// ---------------- XR-9 Specter research jet (engine code 5): blended lifting body with chines, cranked delta with
// elevons and lift fans, all-moving canards, canted twin fins, 2D thrust-vectoring nozzles, opaque sensor canopy.
// The cockpit is a sealed pod: the pilot sees outside only through the panoramic and side display screens.
vec2 mapJetCockpit(vec3 p){
  vec4 E4 = uM[22]; vec3 q = p - E4.xyz;
  float cPitch = uCtl.x, cRoll = uCtl.y, cThr = uCtl.w;
  vec2 res = vec2(-sdEllipsoid(q - vec3(0.0, -0.05, 0.25), vec3(0.8, 0.72, 1.45)), 40.0);
  float r = length(q.xz), ang = atan(q.x, -q.z);
  // panoramic front display in a chamfered bezel, annunciator strip above it
  float scr = max(max(abs(r - 0.64) - 0.006, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30);
  res = opU(res, vec2(scr, 41.0));
  float bez = max(max(abs(r - 0.675) - 0.02, abs(ang) - 1.3), abs(q.y - 0.02) - 0.345);
  bez = max(bez, -max(max(r - 0.66, abs(ang) - 1.25), abs(q.y - 0.02) - 0.30));   // window cut-out
  res = opU(res, vec2(bez, 44.0));
  res = opU(res, vec2(max(max(abs(r - 0.648) - 0.004, abs(ang) - 0.62), abs(q.y - 0.348) - 0.016), 49.0));
  // curved instrument console under the display: sloped top with five recessed multi-function displays
  {
    float topY = -0.30 - (0.66 - r)*0.35;
    float con = max(max(r - 0.67, 0.47 - r), max(abs(ang) - 1.18, max((q.y - topY)*0.94, -0.56 - q.y)));
    float k = clamp(floor(ang/0.42 + 0.5), -2.0, 2.0);
    float ar = (ang - k*0.42)*r;
    vec3 rq = vec3(ar, q.y - topY, r - 0.575);
    float rec = sdBox(rq, vec3(0.072, 0.012, 0.052));
    con = max(con, -rec);
    res = opU(res, vec2(con, 44.0));
    res = opU(res, vec2(sdBox(rq + vec3(0.0, 0.006, 0.0), vec3(0.07, 0.002, 0.05)), 45.0));
    // rotary knobs between the displays
    float kk = clamp(floor(ang/0.42), -3.0, 2.0) + 0.5;
    vec3 kq = vec3((ang - kk*0.42)*r, q.y - topY, r - 0.505);
    res = opU(res, vec2(sdCapsule(kq, vec3(0.0), vec3(0.0, 0.018, 0.0), 0.011), 47.0));
  }
  // side display bays beside the pilot (camera feeds) with framed bezels and vents
  vec3 sq = vec3(abs(q.x) - 0.665, q.y - 0.04, q.z - 0.24);
  res = opU(res, vec2(sdBox(sq, vec3(0.005, 0.2, 0.3)), q.x < 0.0 ? 42.0 : 43.0));
  float sbz = sdRoundBox(sq - vec3(0.02, 0.0, 0.0), vec3(0.014, 0.23, 0.33), 0.012);
  sbz = max(sbz, -sdBox(sq - vec3(-0.01, 0.0, 0.0), vec3(0.02, 0.2, 0.3)));
  res = opU(res, vec2(sbz, 44.0));
  // side consoles: shelf with a small display and a grid of backlit keys; stick and throttle mounted on them
  {
    vec3 cq = vec3(abs(q.x) - 0.52, q.y + 0.44, q.z - 0.08);
    float shelf = sdRoundBox(cq, vec3(0.13, 0.05, 0.36), 0.015);
    res = opU(res, vec2(shelf, 44.0));
    res = opU(res, vec2(sdBox(cq - vec3(0.02, 0.051, -0.2), vec3(0.075, 0.002, 0.06)), q.x < 0.0 ? 52.0 : 53.0));
    vec3 bq = cq - vec3(0.0, 0.055, 0.12);
    vec2 cell = clamp(floor(bq.xz/0.032 + 0.5), vec2(-3.0, -2.0), vec2(3.0, 3.0));
    bq.xz -= cell*0.032;
    res = opU(res, vec2(sdRoundBox(bq, vec3(0.012, 0.006, 0.012), 0.003), 54.0));
  }
  // overhead switch panel angled towards the pilot
  {
    vec3 oq = q - vec3(0.0, 0.5, -0.32); oq.yz = rot2(oq.yz, 0.55);
    float ov = sdRoundBox(oq, vec3(0.26, 0.018, 0.13), 0.01);
    res = opU(res, vec2(ov, 44.0));
    vec3 tq = oq + vec3(0.0, 0.02, 0.0);
    vec2 c2 = clamp(floor(tq.xz/vec2(0.05, 0.06) + 0.5), vec2(-4.0, -1.0), vec2(4.0, 1.0));
    tq.xz -= c2*vec2(0.05, 0.06);
    res = opU(res, vec2(sdCapsule(tq, vec3(0.0), vec3(0.0, -0.022, 0.008), 0.0045), 47.0));
    res = opU(res, vec2(sdBox(oq + vec3(0.0, 0.019, 0.0), vec3(0.25, 0.001, 0.125)), 55.0));
  }
)"
R"(  // sculpted seat: shell, bolsters, headrest with light strip, harness
  {
    float seat = sdRoundBox(q - vec3(0.0, -0.6, 0.12), vec3(0.24, 0.06, 0.26), 0.05);
    vec3 bq = q - vec3(0.0, -0.17, 0.42); bq.yz = rot2(bq.yz, 0.22);
    seat = min(seat, sdRoundBox(bq, vec3(0.23, 0.42, 0.05), 0.05));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.25, q.y + 0.3, q.z - 0.3), vec3(0.04, 0.24, 0.12), 0.03));
    seat = min(seat, sdRoundBox(vec3(abs(q.x) - 0.22, q.y + 0.52, q.z - 0.1), vec3(0.035, 0.07, 0.22), 0.03));
    seat = min(seat, sdRoundBox(q - vec3(0.0, 0.2, 0.47), vec3(0.13, 0.1, 0.05), 0.04));
    res = opU(res, vec2(seat, 46.0));
    vec3 hq = vec3(abs(q.x) - 0.1, q.y + 0.05, q.z - 0.36); hq.yz = rot2(hq.yz, 0.22);
    res = opU(res, vec2(sdBox(hq, vec3(0.022, 0.32, 0.006)), 56.0));
  }
  // side stick (right) follows pitch and roll; throttle grip (left) slides with the throttle
  {
    vec3 sb = q - vec3(0.42, -0.375, -0.02);
    float stick = sdRoundBox(sb, vec3(0.045, 0.02, 0.07), 0.015);
    vec3 st = sb; st.yz = rot2(st.yz, -cPitch*0.25); st.xy = rot2(st.xy, cRoll*0.25);
    stick = min(stick, sdCapsule(st, vec3(0.0), vec3(0.0, 0.11, -0.01), 0.016));
    stick = min(stick, sdRoundBox(st - vec3(0.0, 0.15, -0.01), vec3(0.021, 0.042, 0.027), 0.015));
    vec3 tq = q - vec3(-0.42, -0.34, -0.02 + 0.12*(0.5 - cThr));
    stick = min(stick, sdRoundBox(tq, vec3(0.03, 0.04, 0.05), 0.02));
    res = opU(res, vec2(stick, 47.0));
  }
  // LED strips: ceiling spine, under the display bezel, along both consoles, footwell
  float led = sdCapsule(q, vec3(0.0, 0.61, -0.7), vec3(0.0, 0.61, 0.9), 0.01);
  led = min(led, max(max(abs(r - 0.69) - 0.005, abs(ang) - 1.15), abs(q.y + 0.565) - 0.004));
  led = min(led, sdCapsule(vec3(abs(q.x), q.y, q.z), vec3(0.39, -0.395, -0.25), vec3(0.39, -0.395, 0.42), 0.0025));
  res = opU(res, vec2(led, 48.0));
  res = opU(res, vec2(sdCapsule(q, vec3(-0.3, -0.76, -0.55), vec3(0.3, -0.76, -0.55), 0.008), 57.0));
  return res;
}
vec2 mapJet(vec3 p){
  float gear = uPS.x, noz = uPS.y, inside = uPS.w;
  if (inside > 0.5) return mapJetCockpit(p);
  float cPitch = uCtl.x, cRoll = uCtl.y, cYaw = uCtl.z;
  vec3 ap = vec3(abs(p.x), p.y, p.z);
  float sgn = p.x > 0.0 ? 1.0 : -1.0;
  float body = sdFuselage(p);
  body = smin(body, sdEllipsoid(p - vec3(0.0, -0.12, -3.4), vec3(1.75, 0.1, 5.6)), 0.3);    // chines
  body = smin(body, sdEllipsoid(p - vec3(0.0, 0.36, 1.6), vec3(0.55, 0.3, 6.0)), 0.3);       // dorsal spine
  body = smin(body, sdRoundBox(ap - vec3(0.82, -0.12, 4.6), vec3(0.5, 0.42, 3.4), 0.3), 0.35); // engine bays
  body = max(body, -sdRoundBox(ap - vec3(0.95, -0.34, -1.4), vec3(0.3, 0.19, 0.62), 0.08));   // intakes
  vec2 res = vec2(body, 30.0);
  // cranked delta wing with elevons and a lift fan in each wing
  {
    float s = ap.x, t = p.y - (-0.18 - s*0.035), c = p.z + 1.6;
    float wing = sdPanel(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3);
    float elevon = sdSurface(s, c, t, 5.6, 7.2, 1.2, 5.6, 0.04, 0.84, 1.2, 5.3, -cPitch*0.3 - cRoll*sgn*0.3, 0.0);
    float hole = length(ap.xz - vec2(2.5, 1.2)) - 0.62;
    wing = max(min(wing, elevon), -hole);
    float d = smin(res.x, wing, 0.25);
    res = vec2(d, wing < res.x ? 31.0 : res.y);
    // fan: hub and louvres that swivel with the nozzles
    vec3 fq = ap - vec3(2.5, -0.27, 1.2);
    float fan = max(sdCapsule(fq, vec3(0.0, -0.03, 0.0), vec3(0.0, 0.03, 0.0), 0.16), -1.0);
    vec3 lq = fq; lq.z -= 0.17*clamp(floor(lq.z/0.17 + 0.5), -3.0, 3.0);
    lq.yz = rot2(lq.yz, noz*0.9);
    float lv = max(sdBox(lq, vec3(0.62, 0.05, 0.012)), length(fq.xz) - 0.6);
    res = opU(res, vec2(min(fan, lv), 35.0));
  }
  // all-moving canards
  {
    vec3 q = ap - vec3(0.6, -0.02, -6.4); q.yz = rot2(q.yz, cPitch*0.3);
    float can = sdPanel(q.x, q.z + 0.6, q.y, 1.5, 1.5, 0.45, 1.0, 0.05, 1.0, 0.0, 0.0);
    res = opU(res, vec2(can, 31.0));
  }
  // canted twin fins with rudders
  {
    vec3 q = ap - vec3(1.0, 0.3, 4.6); q.xy = rot2(q.xy, 0.42);
    float fin = sdPanel(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2);
    float rud = sdSurface(q.y, q.z, q.x, 2.3, 2.6, 1.0, 1.9, 0.05, 0.7, 0.15, 2.2, -cYaw*0.4*sgn, 0.0);
    float f2 = min(fin, rud);
    res = vec2(smin(res.x, f2, 0.12), f2 < res.x ? 31.0 : res.y);
  }
  // 2D thrust-vectoring nozzles: swivel from aft to straight down, plus pitch vectoring
  {
    vec3 q = ap - vec3(0.82, -0.12, 7.75);
    float a = uFlame.z;   // = nozzle*90 deg - pitch*0.5 rad (vectoring)
    vec2 yz = rot2(q.yz, -a);
    vec3 nq = vec3(q.x, yz.x, yz.y - 0.5);
    float nzl = sdRoundBox(nq, vec3(0.44, 0.31, 0.5), 0.06);
    nzl = max(nzl, -sdBox(nq - vec3(0.0, 0.0, 0.25), vec3(0.36, 0.23, 0.6)));
    res = opU(res, vec2(nzl, 33.0));
    res = opU(res, vec2(sdBox(nq - vec3(0.0, 0.0, -0.35), vec3(0.36, 0.23, 0.02)), 36.0));   // glowing turbine face
  }
  // sensor canopy (opaque gold film) and LED strips along the chines and wing leading edges
  res = opU(res, vec2(sdEllipsoid(p - vec3(0.0, 0.5, -4.6), vec3(0.6, 0.42, 1.9)), 32.0));
  float led = sdCapsule(ap, vec3(1.55, -0.12, -2.6), vec3(0.35, -0.08, -7.6), 0.022);
  led = min(led, sdCapsule(ap, vec3(1.3, -0.24, -0.25), vec3(5.45, -0.39, 3.9), 0.02));
  led = min(led, sdCapsule(ap, vec3(0.62, 0.62, -3.0), vec3(0.3, 0.72, 2.0), 0.015));
  res = opU(res, vec2(led, 34.0));
  // retractable tricycle gear
  if (gear > 0.06) {
    vec4 G0 = uM[18], G1 = uM[19];
    float gh = G1.x, wr = 0.38, lift = (1.0 - gear)*(gh - 0.5);
    vec3 wc = vec3(G0.x, -gh + wr + lift, G0.z);
    float legs = sdCapsule(ap, vec3(G0.x*0.8, -0.3, G0.z), wc + vec3(-0.1, 0.05, 0.0), 0.07);
    float tyres = sdRoundCylX(ap - wc, wr, 0.13, 0.06);
    vec3 nc = vec3(0.0, -gh + 0.33 + lift, G0.w);
    legs = min(legs, sdCapsule(p, vec3(0.0, -0.35, G0.w), nc + vec3(0.0, 0.1, 0.0), 0.06));
    tyres = min(tyres, sdRoundCylX(vec3(abs(p.x) - 0.1, p.y, p.z) - vec3(0.0, nc.y, nc.z), 0.33, 0.07, 0.04));
    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
  }
  return res;
}
vec2 mapPlane(vec3 p){
  if (int(uM[0].z + 0.5) == 5) return mapJet(p);
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
)"
R"(      float spin = sdRoundCone(p, vec3(0.0, S0.w, S0.x - sr*2.3), vec3(0.0, S0.w, S0.x + 0.05), 0.015, sr);
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
      float inlet = sdCapsule(np, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.18), nr*0.82);
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
    // switch row along the lower panel edge (domain repetition)
    vec3 swp = p - vec3(0.0, E.y - 0.565, pz + 0.05);
    float sw = 0.032; float cell = clamp(floor(swp.x/sw + 0.5), -12.0, 12.0);
    swp.x -= cell*sw;
)"
R"(    float sws = sdRoundBox(swp - vec3(0.0, 0.0, 0.01), vec3(0.006, 0.012, 0.012), 0.003);
    sws = max(sws, abs(p.x) - phw*0.85);
    res = opU(res, vec2(sws, 13.0));
    // armrests / door panels
    vec3 secA = fusSection(E.z);
    vec3 ap = vec3(abs(p.x) - (secA.x*0.86), p.y - (E.y - 0.5), p.z - (E.z - 0.15));
    res = opU(res, vec2(sdRoundBox(ap, vec3(0.05, 0.035, 0.38), 0.02), 12.0));
    // sun visors folded against the cabin roof
    vec3 vp = vec3(abs(p.x) - abs(E.x), p.y - (E.y + 0.2), p.z - (E.z - 0.42));
    vp.yz = rot2(vp.yz, 0.25);
    res = opU(res, vec2(sdRoundBox(vp, vec3(0.16, 0.006, 0.07), 0.004), 14.0));
    // overhead console with dome light
    res = opU(res, vec2(sdRoundBox(p - vec3(0.0, E.y + 0.27, E.z - 0.05), vec3(0.09, 0.025, 0.18), 0.015), 14.0));
    float compass = sdRoundBox(p - vec3(0.0, E.y - 0.05, pz - 0.05), vec3(0.04, 0.03, 0.03), 0.01);
    res = opU(res, vec2(compass, 13.0));
  }
  return res;
}
vec2 mapPiece(vec3 p){ vec2 d = mapPlane(p); if (gPI >= 0) d.x = max(d.x, sdBox(p - uPcC[gPI], uPcH[gPI])); return d; }
vec3 planeNormal(vec3 p){ const vec2 k = vec2(1,-1); float e = 0.0025;
  return normalize(k.xyy*mapPiece(p+k.xyy*e).x + k.yyx*mapPiece(p+k.yyx*e).x + k.yxy*mapPiece(p+k.yxy*e).x + k.xxx*mapPiece(p+k.xxx*e).x); }

float planeBound(){ return max(uM[0].x, uM[9].x*2.0)*0.55 + 1.5; }
void pieceXf(int i){ gPI = i; if (i < 0) { gPP = uPlanePos; gPR = uPlaneRot; gPC = vec3(0.0); } else { gPP = uPcPos[i]; gPR = uPcRot[i]; gPC = uPcC[i]; } }
vec2 tracePieceOnce(vec3 ro, vec3 rd, float tmax, float br){
  vec3 oc = ro - gPP;
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0) return vec2(-1.0);
  h = sqrt(h); float t0 = max(-b-h, 0.0), t1 = min(-b+h, tmax);
  if (t0 > t1) return vec2(-1.0);
  mat3 inv = transpose(gPR);
  vec3 lo = gPC + inv*(ro - gPP), ld = inv*rd;
  float t = t0;
  bool jet = int(uM[0].z + 0.5) == 5;   // XR-9: thin flattened shapes need finer steps
  int steps = uPS.w > 0.5 || jet ? 200 : 120;
  float relax = jet ? 0.65 : 0.8;
  for (int i=0;i<200;i++){
    if (i >= steps) break;
    vec2 d = mapPiece(lo + ld*t);
    if (d.x < 0.0015*max(1.0, t*0.03)) return vec2(t, d.y);
    t += d.x*relax;
    if (t > t1) break;
  }
  return vec2(-1.0);
}
// Leaves gPI/gPP/gPR/gPC set to the piece that was hit (for shading)
vec2 tracePlane(vec3 ro, vec3 rd, float tmax){
  if (uPlaneOn == 0) return vec2(-1.0);
  if (uWreck == 0) { pieceXf(-1); return tracePieceOnce(ro, rd, tmax, planeBound()); }
  vec2 best = vec2(-1.0); int bi = 0;
  for (int i = 0; i < 5; i++) {
    if (i >= uWreck) break;
    pieceXf(i);
    vec2 h = tracePieceOnce(ro, rd, best.x > 0.0 ? best.x : tmax, length(uPcH[i]) + 0.3);
    if (h.x > 0.0 && (best.x < 0.0 || h.x < best.x)) { best = h; bi = i; }
  }
  pieceXf(bi);
  return best;
}
float pieceShadow(vec3 ro, vec3 rd, float br){
  vec3 oc = ro - gPP;
  float b = dot(oc, rd), c = dot(oc,oc) - br*br, h = b*b - c;
  if (h < 0.0 || -b + sqrt(max(h,0.0)) < 0.0) return 1.0;
  h = sqrt(h); float t = max(-b-h, 0.0), t1 = -b+h;
  mat3 inv = transpose(gPR); vec3 lo = gPC + inv*(ro - gPP), ld = inv*rd;
  float res = 1.0;
  for (int i=0;i<56;i++){
    float d = mapPiece(lo + ld*t).x;
    res = min(res, 10.0*d/max(t,0.1));
    if (res < 0.01) return 0.0;
    t += clamp(d, 0.03, 2.0);
    if (t > t1) break;
  }
  return clamp(res, 0.0, 1.0);
}
float planeShadow(vec3 ro, vec3 rd){
  if (uPlaneOn == 0) return 1.0;
  int keep = gPI; vec3 kP = gPP; mat3 kR = gPR; vec3 kC = gPC;
  float res = 1.0;
  if (uWreck == 0) { pieceXf(-1); res = pieceShadow(ro, rd, planeBound()); }
  else for (int i = 0; i < 5; i++) { if (i >= uWreck) break; pieceXf(i); res = min(res, pieceShadow(ro, rd, length(uPcH[i]) + 0.3)); }
  gPI = keep; gPP = kP; gPR = kR; gPC = kC;
  return res;
}
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
      if (pd.x > 0.06) { col = vec3(0.12); float tk = fract((pd.y*4000.0 + alt)/100.0); if (tk < 0.08 && pd.x < 0.068) col = vec3(0.9); if (abs(pd.y) < 0.006) col = vec3(0.0, 0.9, 0.4); }
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
)"
R"(        float rr = length(d)/(r*0.85); float a = atan(d.x, d.y);
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

)";
// Split in two constants: MSVC limits a concatenated string literal to 64 KB.
static const char* kRaytraceFS2 = R"(// ---------------------------------------------------------------- terrain (+ trees, rocks, sea stacks)
float coverFade(float t){ return smoothstep(4500.0, 2500.0, t); }
uniform sampler2D uHMax;  // conservative max height per cell, mip L = 256>>L cells per side
const int HMAXN = 256; const int HMAXL = 5;
// Ray march the heightfield. Cells the ray passes entirely above (per the max-height mip chain) are skipped,
// which keeps grazing rays over lowlands from running out of steps (they used to fall through to the sea).
float traceTerrain(vec3 ro, vec3 rd, float tmax){
  float t = 1.0;
  if (ro.y > uMaxH) { if (rd.y >= 0.0) return -1.0; t = max(t, (ro.y - uMaxH)/(-rd.y)); }
  float lt = t, ldh = 0.0; bool skipped = true;
  int maxSteps = uQuality > 1 ? 360 : (uQuality > 0 ? 270 : 190);
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
    float fade = coverFade(t);
    float h = terrainHF(p.xz, oct, fade, p.y);
    float dh = p.y - h;
    if (dh < 0.0015*t) {
      if (skipped) return t;
      return lt + (t - lt) * ldh / max(ldh - dh, 1e-4);
    }
    skipped = false; lt = t; ldh = dh;
    // trees and boulders are steep: take shorter steps close to the canopy
    float k = (fade > 0.0 && dh < 50.0) ? 0.3 : (t > 4500.0 ? 0.6 : 0.42);
    t += max(dh*k, 0.2 + 0.0015*t);
  }
  // out of steps while skimming just above the ground: count it as a hit rather than showing the sea through hills
  if (t <= tmax) { vec3 p = ro + rd*t; if (p.y - terrainHF(p.xz, 5, 0.0, p.y) < 0.02*t) return t; }
  return -1.0;
}
float terrainShadow(vec3 ro, vec3 rd, float camT){
  float res = 1.0, t = 2.0;
  // trees/rocks only cast shadows near the camera (past ~1 km they are sub-pixel and the fine steps they need are
  // the most expensive part of the shadow ray); the bare heightfield keeps shadowing all the way out
  float fade = coverFade(camT)*smoothstep(1100.0, 650.0, camT);
  float near = fade > 0.001 ? 120.0 : 0.0;
  for (int i=0;i<40;i++){
    vec3 p = ro + rd*t;
    if (p.y > uMaxH) break;
    float h = p.y - terrainHF(p.xz, 4, t < near ? fade : 0.0, p.y);
    res = min(res, 12.0*h/t);
    if (res < 0.0) return 0.0;
    t += clamp(h*0.6, t < near ? 1.5 : 6.0, 450.0);
  }
  return clamp(res, 0.0, 1.0);
}
vec3 terrainNormal(vec2 p, float t){
  float e = max(0.25, t*0.0012);
  int oct = t < 600.0 ? 11 : (t < 3000.0 ? 9 : 7);
  float f = coverFade(t);
  return normalize(vec3(terrainHF(p - vec2(e,0.0), oct, f, -1e9) - terrainHF(p + vec2(e,0.0), oct, f, -1e9), 2.0*e,
                        terrainHF(p - vec2(0.0,e), oct, f, -1e9) - terrainHF(p + vec2(0.0,e), oct, f, -1e9)));
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
  int bits[10] = int[10](0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F);
  int b = bits[d];
  float w = 0.16; float on = 0.0;
  // a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
  if ((b & 1) != 0 && q.y > 1.0 - w) on = 1.0;
  if ((b & 2) != 0 && q.x > 1.0 - w && q.y > 0.5) on = 1.0;
  if ((b & 4) != 0 && q.x > 1.0 - w && q.y < 0.5) on = 1.0;
  if ((b & 8) != 0 && q.y < w) on = 1.0;
  if ((b & 16) != 0 && q.x < w && q.y < 0.5) on = 1.0;
  if ((b & 32) != 0 && q.x < w && q.y > 0.5) on = 1.0;
)"
R"(  if ((b & 64) != 0 && abs(q.y - 0.5) < w*0.5) on = 1.0;
  return on;
}
float rwyDigits(vec2 q, int num){
  // two digits side by side, each 7 m wide x 18 m long, 3 m gap; q relative to the block centre (x across, y along)
  int d0 = num/10, d1 = num - d0*10;
  float a = seg7(vec2((q.x + 8.5)/7.0, q.y/18.0 + 0.5), d0);
  float b = seg7(vec2((q.x - 1.5)/7.0, q.y/18.0 + 0.5), d1);
  return max(a, b);
}

void runwayMaterial(int ai, vec2 uv, inout Mat m, vec3 pw, out bool onRw, out bool paved){
  vec4 d = uApDim[ai]; float len = d.x, wid = d.y; int surf = int(d.z); int size = int(d.w);
  onRw = false; paved = false;
  float u = uv.x, v = uv.y;
  vec3 nTS;
  float side = (ai - (ai/2)*2) == 1 ? 1.0 : -1.0;
  float off = wid*0.5 + (size == 2 ? 170.0 : 85.0);
  int padLayer = size == 2 ? M_CONCRETE : M_ASPHALT;
  if (surf == 0 && size > 0) {
    float apronV = side*v;
    float apronEnd = off + (size == 2 ? 90.0 : 60.0);
    if (apronV > wid*0.5 + 20.0 && apronV < apronEnd && abs(u + len*0.05) < len*0.33) {
      vec4 t = matSample(pw.xz, padLayer, 9.0, nTS);
      m.alb = t.rgb*1.1; m.rough = t.a; m.nrm = nTS; m.metal = 0.0; paved = true;
      // concrete slab joints
      vec2 sj = abs(fract(pw.xz/6.0) - 0.5);
      if (size == 2 && max(sj.x, sj.y) > 0.49) m.alb *= 0.8;
      // taxi lane and parking stand lead-in lines
      if (abs(apronV - wid*0.5 - 45.0) < 0.3) m.alb = vec3(0.65,0.5,0.05);
      float st = mod(u + len*0.05, 45.0) - 22.5;
      if (abs(st) < 0.25 && apronV > wid*0.5 + 45.0 && apronV < apronEnd - 8.0) m.alb = vec3(0.65,0.5,0.05);
      if (abs(st) < 6.0 && abs(apronV - apronEnd + 12.0) < 0.25) m.alb = vec3(0.7);
      return;
    }
    if (apronV > wid*0.5 - 1.0 && apronV < off && (abs(u - len*0.28) < 11.0 || abs(u + len*0.38) < 11.0)) {
      vec4 t = matSample(pw.xz, padLayer, 9.0, nTS);
      m.alb = t.rgb*1.05; m.rough = t.a; m.nrm = nTS; m.metal = 0.0; paved = true;
      float cl = abs(u - len*0.28) < 11.0 ? u - len*0.28 : u + len*0.38;
      if (abs(cl) < 0.25) m.alb = vec3(0.65,0.5,0.05);
      if (abs(abs(cl) - 10.0) < 0.2) m.alb = vec3(0.65,0.5,0.05);
      // runway holding position marking (2 solid + 2 dashed)
      float hp = apronV - (wid*0.5 + 22.0);
      if (abs(cl) < 10.0 && ((abs(hp) < 0.2 || abs(hp - 0.6) < 0.2) || ((abs(hp - 1.4) < 0.2 || abs(hp - 2.0) < 0.2) && fract(cl/2.0) < 0.5))) m.alb = vec3(0.7,0.55,0.05);
      return;
    }
  }
  if (abs(u) > len*0.5 + 6.0 || abs(v) > wid*0.5 + 3.0) {
    if (abs(u) < len*0.5 + 60.0 && abs(v) < wid*0.5 + 7.5 && surf == 0) {
      // paved blast pad / shoulders with yellow chevrons
      vec4 t = matSample(pw.xz, M_ASPHALT, 7.0, nTS);
      m.alb = t.rgb*0.9; m.rough = t.a; m.nrm = nTS; paved = true;
      float bu = abs(u) - len*0.5;
      if (bu > 6.0 && fract((bu + abs(v)*1.2)/14.0) < 0.12 && abs(v) < wid*0.5) m.alb = vec3(0.6,0.48,0.05);
      return;
    }
    if (abs(u) < len*0.5 + 120.0 && abs(v) < wid*0.5 + 60.0) {
      vec4 t = matSample(pw.xz, M_GRASS, 5.0, nTS);
      float stripe = step(0.5, fract(u/18.0));
      m.alb = t.rgb*(0.85 + 0.15*stripe)*vec3(0.95,1.05,0.9); m.rough = 0.9; m.nrm = nTS;
    }
    return;
  }
  onRw = true; paved = surf == 0;
  int layer = surf == 0 ? padLayer : surf == 1 ? M_GRASS : surf == 2 ? M_GRAVEL : surf == 3 ? M_SNOW : M_SAND;
  vec4 t = matSample(pw.xz, layer, surf == 0 ? 7.0 : 5.0, nTS);
  m.alb = t.rgb; m.rough = t.a; m.nrm = nTS; m.metal = 0.0;
  if (surf == 1) m.alb *= vec3(0.8,1.0,0.75) * (0.88 + 0.12*step(0.5, fract(u/22.0)));
  if (surf >= 2) { float rut = smoothstep(1.5, 0.3, abs(abs(v) - 2.2)); m.alb *= 1.0 - 0.18*rut; }
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
  float lush = base.z, cold = base.w;
  float slope = 1.0 - n.y;
  float hNoise = fbm2(p.xz/900.0, 4);
  float n2 = fbm2(p.xz/180.0, 3);
  vec3 nTS;
  float g = groundH(p.xz, t < 1500.0 ? 7 : 6);
  int ckind = 0;
  float cover = 0.0;
  if (p.y - g > 0.25 && t < 4500.0) cover = coverH(p.xz, g, base, ckind);
  vec4 msk = maskAt(p.xz);
  // ---- cover: trees, rocks, sea stacks
  if (ckind > 0 && cover > 0.25) {
    float rel = clamp((p.y - g)/max(cover, 1.0), 0.0, 1.0);
    float th = hash2i(ivec2(floor(p.xz/10.0)) + ivec2(5, 5));
    if (ckind <= 3) {
      int layer = ckind == 1 ? M_NEEDLES : M_LEAVES;
      vec4 tx = triSample(p, n, layer, ckind == 1 ? 2.0 : 2.6, nTS);
      vec3 tint = ckind == 1 ? mix(vec3(0.55,0.75,0.6), vec3(0.42,0.6,0.5), th) : ckind == 3 ? vec3(0.8,1.05,0.55) : mix(vec3(0.75,0.95,0.5), vec3(0.55,0.85,0.45), th);
      tint = mix(tint, vec3(0.85,0.7,0.35), smoothstep(0.88, 1.0, th)*(ckind == 2 ? 0.6 : 0.0)*(1.0 - lush));
      m.alb = tx.rgb*tint*mix(0.62, 1.05, rel)*mix(0.38, 1.0, smoothstep(0.12, 0.32, rel));  // shaded understorey / trunks
      m.rough = 0.8; m.nrm = nTS*vec3(2.5, 2.5, 1.0);
      if (ckind == 1 && (cold > 0.6 || uSnow > 0.1)) m.alb = mix(m.alb, vec3(0.9), smoothstep(0.55, 0.85, n.y)*0.7);
      m.emit = m.alb*0.04*vec3(0.3, 1.0, 0.2);   // leaf translucency
    } else {
      vec4 r = triSample(p, n, M_ROCK, 6.0, nTS);
      m.alb = r.rgb*(ckind == 5 ? vec3(0.8, 0.76, 0.7) : mix(vec3(0.62, 0.6, 0.57), vec3(0.55,0.55,0.58), cold));
      m.alb = mix(m.alb, vec3(0.45, 0.5, 0.3), smoothstep(0.6, 0.9, n.y)*0.35*lush);  // lichen / moss on top
      if (ckind == 5) m.alb = mix(m.alb, vec3(0.92), smoothstep(0.85, 0.98, rel)*0.6);
      m.rough = r.a; m.nrm = nTS*vec3(1.5, 1.5, 1.0);
    }
    m.alb *= 1.0 - 0.3*uWet;
    return m;
  }
  // ---- natural ground layers
  float snowLine = mix(1700.0, 350.0, cold) + (hNoise - 0.5)*300.0;
  float wSnow = smoothstep(snowLine - 60.0, snowLine + 60.0, p.y) * smoothstep(0.55, 0.3, slope);
  wSnow = max(wSnow, uSnow*smoothstep(0.6, 0.35, slope)*step(1.0, p.y));
  float wRock = smoothstep(0.30, 0.54, slope + (n2-0.5)*0.18);
  float wSand = smoothstep(4.5 + 3.0*hNoise, 1.0, p.y) * (1.0 - wRock);
  float forestN = forestAt(p.xz);
  float wForest = smoothstep(0.42 - lush*0.1, 0.5 - lush*0.1, forestN) * smoothstep(0.35, 0.2, slope) * smoothstep(4.0, 9.0, p.y) * smoothstep(1500.0 - cold*900.0, 1100.0 - cold*700.0, p.y);
  float wDirt = smoothstep(0.55, 0.7, n2) * (1.0 - wForest) * 0.6;
  float hC, hL;
  vec4 gr = groundSample(p.xz, M_GRASS, 6.0, nTS, hC); vec3 nG = nTS;
)"
R"(  vec3 grassTint = mix(vec3(0.75,0.85,0.45), vec3(0.55,0.95,0.45), lush) * mix(vec3(1.0), vec3(1.15,1.0,0.8), smoothstep(0.4,0.8,hNoise));
  grassTint = mix(grassTint, vec3(0.75,0.8,0.65), cold*0.6);
  grassTint *= 0.92 + 0.16*vnoise(p.xz/23.0);                 // gentle colour variation breaks up repetition
  m.alb = gr.rgb*grassTint; m.rough = gr.a; m.nrm = nG;
  // wildflower and dry patches
  float fl = smoothstep(0.62, 0.7, vnoise(p.xz/35.0))*step(0.93, hash2i(ivec2(floor(p.xz*1.3))));
  m.alb = mix(m.alb, mix(vec3(0.95,0.85,0.2), vec3(0.75,0.35,0.85), step(0.5, vnoise(p.xz/20.0))), fl*(1.0 - cold)*0.7);
  if (wDirt > 0.01) { vec4 d = groundSample(p.xz, M_DIRT, 7.0, nTS, hL); float w = hblend(wDirt, hC, hL);
    m.alb = mix(m.alb, d.rgb, w); m.rough = mix(m.rough, d.a, w); m.nrm = mix(m.nrm, nTS, w); hC = mix(hC, hL, w); }
  if (wForest > 0.01) { vec4 f = groundSample(p.xz, M_FOREST, 9.0, nTS, hL); float w = hblend(wForest*0.6, hC, hL);   // light forest floor between the (now sparser) trees
    m.alb = mix(m.alb, f.rgb*vec3(0.72,0.78,0.56), w); m.rough = mix(m.rough, 0.95, w); m.nrm = mix(m.nrm, nTS, w); hC = mix(hC, hL, w); }
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
  if (uCrater.z > 0.0) {
    float cd = length(p.xz - uCrater.xy)/uCrater.z;
    if (cd < 2.6) {
      float nz = vnoise(p.xz*0.35) + 0.5*vnoise(p.xz*1.7);
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

// building facades (town lots); info = (type, seed, roofTop y, part)
Mat buildingMaterial(vec3 p, vec3 n, vec4 info){
  Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1); m.rough = 0.8;
  float seed = info.y; int type = int(info.x + 0.5); int part = int(info.w + 0.5);
  vec3 nTS;
  if (part == 1) {
    // pitched roof: clay tiles or slate
    bool slate = seed > 0.72;
    vec4 t = triSample(p, n, slate ? M_SLATE : M_TILES, 3.0, nTS);
    vec3 tint = slate ? vec3(0.7, 0.72, 0.78) : mix(vec3(1.0, 0.8, 0.7), vec3(0.8, 0.55, 0.45), fract(seed*7.0));
    m.alb = t.rgb*tint; m.rough = t.a; m.nrm = nTS*vec3(1.6, 1.6, 1.0);
    return m;
  }
  if (abs(n.y) > 0.5) { vec4 t = triSample(p, n, M_CONCRETE, 4.0, nTS); m.alb = t.rgb*0.75; m.rough = t.a; m.nrm = nTS; return m; }
  float u = abs(n.x) > 0.5 ? p.z : p.x;
  float floorH = type == 1 ? 3.6 : 2.9;
  float y = p.y - (info.z - floor((info.z - p.y)/floorH)*floorH);
  float fy = fract((info.z - p.y)/floorH);
  float fx = fract(u/(type == 1 ? 2.6 : 3.2));
  float win = step(0.18, fx)*step(fx, 0.82)*step(0.25, fy)*step(fy, 0.72);
  if (type == 1) {
    vec4 t = triSample(p, n, M_CONCRETE, 4.0, nTS);
    vec3 facade = mix(vec3(0.85, 0.83, 0.8), vec3(0.6, 0.62, 0.66), fract(seed*13.0));
    if (fract(seed*5.0) > 0.6) win = step(0.06, fx)*step(fx, 0.94)*step(0.15, fy)*step(fy, 0.88);  // curtain-wall towers
    m.alb = t.rgb*facade; m.rough = t.a; m.nrm = nTS;
    if (win > 0.5) { m.alb = vec3(0.04, 0.06, 0.08); m.rough = 0.06; m.metal = 0.4; }
  } else {
    bool brick = fract(seed*3.0) > 0.7;
    vec4 t = triSample(p, n, brick ? M_BRICK : M_PLASTER, 2.5, nTS);
    vec3 pal[5] = vec3[5](vec3(0.95,0.93,0.88), vec3(0.95,0.85,0.65), vec3(0.85,0.6,0.45), vec3(0.75,0.85,0.9), vec3(0.9,0.9,0.8));
    m.alb = t.rgb*(brick ? vec3(1.0) : pal[int(fract(seed*11.0)*4.99)]); m.rough = t.a; m.nrm = nTS;
    if (win > 0.5) { m.alb = vec3(0.05, 0.07, 0.09); m.rough = 0.08; m.metal = 0.3; }
    else if (step(0.12, fx)*step(fx, 0.88)*step(0.2, fy)*step(fy, 0.77) > 0.5) m.alb = vec3(0.92);   // window frames
  }
  float lit = step(0.55, hash1(floor(u/2.6)*7.13 + floor((info.z - p.y)/floorH)*3.71 + seed*91.0));
  m.emit = win*lit*vec3(1.0, 0.82, 0.55)*uNight*1.6;
  m.alb *= 1.0 - 0.25*uWet;
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
  // aircraft landing light
  if (uLandLight > 0.0) {
    vec3 lv = p - uLandLightPos; float ld = length(lv); vec3 l = lv/ld;
    float cone = smoothstep(0.93, 0.985, dot(l, uLandLightDir));
    col += pbr(n, v, -l, m.alb, m.rough, m.metal, vec3(1.0,0.95,0.85)*cone*uLandLight*9000.0/(ld*ld + 50.0));
  }
  // research jet exhaust plume light (flickering; lights the tail, the nozzles and the ground while hovering)
  if (uFlameLI.x + uFlameLI.y + uFlameLI.z > 0.0) {
    vec3 lv = uFlameLP - p; float d2 = dot(lv, lv);
    col += pbr(n, v, lv*inversesqrt(max(d2, 1e-4)), m.alb, m.rough, m.metal, uFlameLI/(d2 + 1.5));
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
  vec3 fogCol = skyColor(normalize(vec3(rd.x, 0.06, rd.z)))*vec3(0.88, 0.93, 1.0);
  fogCol += uSunCol*pow(mu, 8.0)*0.2;
  return mix(col, fogCol, clamp(fogAmt, 0.0, 1.0));
}

)";
// Third part (MSVC limits each concatenated string literal to 64 KB).
static const char* kRaytraceFS3 = R"(// ---------------------------------------------------------------- analytic primitives
// slab test against an AABB; returns (tNear, tFar), normal of the entry face
vec2 iBox(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out vec3 n){
  vec3 inv = 1.0/rd;
  vec3 t0 = (bmin - ro)*inv, t1 = (bmax - ro)*inv;
  vec3 tmin = min(t0, t1), tmaxv = max(t0, t1);
  float tN = max(max(tmin.x, tmin.y), tmin.z), tF = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
  n = tN == tmin.x ? vec3(-sign(rd.x),0,0) : tN == tmin.y ? vec3(0,-sign(rd.y),0) : vec3(0,0,-sign(rd.z));
  return vec2(tN, tF);
}
// vertical capped cylinder (base centre c, radius r, height h)
// small debris chunks: oriented boxes
vec3 qrot(vec4 q, vec3 v){ vec3 u = q.yzw; vec3 tt = 2.0*cross(u, v); return v + q.x*tt + cross(u, tt); }
float traceDebris(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out float charred){
  float best = -1.0; nOut = vec3(0,1,0); charred = 0.0;
  for (int i = 0; i < 16; i++) {
    if (i >= uDebN) break;
    vec4 d = uDeb[i]; float sz = abs(d.w);
    vec3 oc = ro - d.xyz; float b = dot(oc, rd), c = dot(oc, oc) - sz*sz*1.6, h = b*b - c;
    if (h < 0.0) continue;
    vec4 qi = vec4(uDebQ[i].x, -uDebQ[i].yzw);
    vec3 lo = qrot(qi, ro - d.xyz), ld = qrot(qi, rd);
    vec3 hs = sz*vec3(1.0, 0.18, 0.6), n;
    vec2 tt = iBox(lo, ld, -hs, hs, n);
    if (tt.x > 0.0 && tt.x < tt.y && tt.x < tmax && (best < 0.0 || tt.x < best)) { best = tt.x; nOut = qrot(uDebQ[i], n); charred = d.w < 0.0 ? 1.0 : 0.0; }
  }
  return best;
}
vec2 iVCyl(vec3 ro, vec3 rd, vec3 c, float r, float h, out vec3 n){
  vec2 o = ro.xz - c.xz; vec2 d = rd.xz;
  float a = dot(d,d), b = dot(o,d), cc = dot(o,o) - r*r;
  float disc = b*b - a*cc;
  n = vec3(0.0);
  if (disc < 0.0 || a < 1e-8) return vec2(1e9, -1e9);
  disc = sqrt(disc);
  float s0 = (-b - disc)/a, s1 = (-b + disc)/a;
  float y0 = (c.y - ro.y)/rd.y, y1 = (c.y + h - ro.y)/rd.y;
  float ya = min(y0, y1), yb = max(y0, y1);
  float tN = max(s0, ya), tF = min(s1, yb);
  if (tN == s0) { vec3 p = ro + rd*tN; n = normalize(vec3(p.x - c.x, 0.0, p.z - c.z)); } else n = vec3(0.0, -sign(rd.y), 0.0);
  return vec2(tN, tF);
}
// cylinder along x (for arched hangar roofs): axis through (y = cy, z = cz)
vec2 iXCyl(vec3 ro, vec3 rd, float cy, float cz, float r, out vec3 n){
  vec2 o = vec2(ro.y - cy, ro.z - cz), d = rd.yz;
  float a = dot(d,d), b = dot(o,d), cc = dot(o,o) - r*r;
  float disc = b*b - a*cc;
  if (disc < 0.0 || a < 1e-8) { n = vec3(0.0); return vec2(1e9, -1e9); }
  disc = sqrt(disc);
  float t0 = (-b - disc)/a;
  vec3 p = ro + rd*t0; n = normalize(vec3(0.0, p.y - cy, p.z - cz));
  return vec2(t0, (-b + disc)/a);
}
// convex polytope: inside where dot(n, p) <= d for all planes (n.xyz, d)
vec2 iConvex(vec3 ro, vec3 rd, vec4 pl[7], int cnt, out vec3 nOut){
  float tN = -1e9, tF = 1e9; nOut = vec3(0.0, 1.0, 0.0);
  for (int i = 0; i < 7; i++) {
    if (i >= cnt) break;
    float den = dot(pl[i].xyz, rd), dist = pl[i].w - dot(pl[i].xyz, ro);
    if (abs(den) < 1e-7) { if (dist < 0.0) return vec2(1e9, -1e9); continue; }
    float t = dist/den;
    if (den < 0.0) { if (t > tN) { tN = t; nOut = pl[i].xyz; } } else tF = min(tF, t);
  }
  return vec2(tN, tF);
}

// ---------------------------------------------------------------- town buildings (lot grid; mirrors World::lotAt)
uniform int uTownCount;
struct Lot { bool present; vec2 c; float hw, hd, wallH, roofH, ground; int type; int ridgeX; float seed; };
Lot lotAt(ivec2 c){
  Lot L; L.present = false;
  L.c = vec2((float(c.x) + 0.5 + (hash2i(ivec2(c.x*3+1, c.y*5+2)) - 0.5)*0.14)*28.0, (float(c.y) + 0.5 + (hash2i(ivec2(c.x*7-3, c.y*3+9)) - 0.5)*0.14)*28.0);
  vec4 m = maskTexel(L.c);
  if (m.y < 0.01 || hash2i(ivec2(c.x*11+5, c.y*13-1)) > m.y*1.25) return L;
  float h3 = hash2i(c+ivec2(3,-7)), h4 = hash2i(c+ivec2(-9,4)), h5 = hash2i(c+ivec2(15,21)), h6 = hash2i(c+ivec2(-31,-2));
  L.seed = hash2i(ivec2(c.x*17+3, c.y*19+5));
  float urban = m.z;
  if (urban > 0.45) { L.type = 1; L.hw = 6.0 + 3.5*h3; L.hd = 6.0 + 3.5*h4; L.wallH = 9.0 + urban*urban*50.0*pow(h5, 1.5); L.roofH = 0.0; L.ridgeX = 0; }
  else { L.type = 0; L.hw = 3.5 + 2.0*h3; L.hd = 4.5 + 2.5*h4; L.wallH = 3.0 + 2.5*h5 + (urban > 0.2 ? 3.0 : 0.0); L.roofH = 1.8 + 1.2*h6; L.ridgeX = L.seed < 0.5 ? 1 : 0; }
  if (m.x*80.0 < max(L.hw, L.hd) + 6.0) return L;
  L.ground = groundH(L.c, 5) - 1.0;
  if (L.ground < 1.5) return L;
  L.present = true;
  return L;
}
// returns t (or -1); info = (type, seed, localY, part) ; part 0 wall, 1 roof
float hitLot(vec3 ro, vec3 rd, Lot L, float tmax, out vec3 n, out vec4 info){
  float top = L.ground + 1.0 + L.wallH;
  vec3 nb; vec2 b = iBox(ro, rd, vec3(L.c.x - L.hw, L.ground, L.c.y - L.hd), vec3(L.c.x + L.hw, top, L.c.y + L.hd), nb);
  float best = tmax; float res = -1.0; info = vec4(0.0);
  if (b.x < b.y && b.y > 0.0 && b.x < best && b.x > 0.0) { best = b.x; res = b.x; n = nb; info = vec4(float(L.type), L.seed, top, 0.0); }
  if (L.type == 0) {
    float ox = L.hw + 0.4, oz = L.hd + 0.4, rh = L.roofH;
    vec4 pl[7];
    pl[0] = vec4(1,0,0, L.c.x + ox); pl[1] = vec4(-1,0,0, -(L.c.x - ox)); pl[2] = vec4(0,0,1, L.c.y + oz); pl[3] = vec4(0,0,-1, -(L.c.y - oz));
    pl[4] = vec4(0,-1,0, -top);
    if (L.ridgeX == 1) { vec3 a = normalize(vec3(0.0, 1.0, rh/oz)), bb = normalize(vec3(0.0, 1.0, -rh/oz));
      pl[5] = vec4(a, dot(a, vec3(L.c.x, top + rh, L.c.y))); pl[6] = vec4(bb, dot(bb, vec3(L.c.x, top + rh, L.c.y))); }
    else { vec3 a = normalize(vec3(rh/ox, 1.0, 0.0)), bb = normalize(vec3(-rh/ox, 1.0, 0.0));
      pl[5] = vec4(a, dot(a, vec3(L.c.x, top + rh, L.c.y))); pl[6] = vec4(bb, dot(bb, vec3(L.c.x, top + rh, L.c.y))); }
    vec3 nr; vec2 r = iConvex(ro, rd, pl, 7, nr);
    if (r.x < r.y && r.x > 0.0 && r.x < best) { best = r.x; res = r.x; n = nr; info = vec4(0.0, L.seed, top, 1.0); }
  } else {
    // rooftop plant room
    vec3 nb2; vec2 b2 = iBox(ro, rd, vec3(L.c.x - L.hw*0.35, top, L.c.y - L.hd*0.3), vec3(L.c.x + L.hw*0.35, top + 3.0, L.c.y + L.hd*0.3), nb2);
    if (b2.x < b2.y && b2.x > 0.0 && b2.x < best) { best = b2.x; res = b2.x; n = nb2; info = vec4(1.0, L.seed, top, 2.0); }
  }
  return res;
}
float traceTowns(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out vec4 info){
  float best = -1.0; float limit = min(tmax, 9000.0);
  for (int i = 0; i < 24; i++) {
    if (i >= uTownCount) break;
    vec4 TB = dataAt(320 + i), TY = dataAt(352 + i);
    vec3 nb; vec2 tb = iBox(ro, rd, vec3(TB.x, TY.x, TB.y), vec3(TB.z, TY.y, TB.w), nb);
    float t0 = max(tb.x, 0.0), t1 = min(tb.y, best > 0.0 ? best : limit);
    if (t0 >= t1) continue;
    vec3 p0 = ro + rd*(t0 + 0.01);
    ivec2 c = ivec2(floor(p0.xz/28.0));
    vec2 sgn = vec2(rd.x >= 0.0 ? 1.0 : -1.0, rd.z >= 0.0 ? 1.0 : -1.0);
    vec2 inv = 1.0/max(abs(rd.xz), vec2(1e-6));
    vec2 nextB = (vec2(c) + max(sgn, 0.0))*28.0;
    vec2 tNext = t0 + (nextB - p0.xz)*sgn*inv;
    vec2 tDelta = 28.0*inv;
    for (int k = 0; k < 90; k++) {
      Lot L = lotAt(c);
      if (L.present) {
        vec3 n; vec4 inf;
        float th = hitLot(ro, rd, L, t1, n, inf);
        if (th > 0.0 && th < t1) { best = th; t1 = th; nOut = n; info = inf; break; }
      }
      if (tNext.x < tNext.y) { if (tNext.x > t1) break; c.x += int(sgn.x); tNext.x += tDelta.x; }
      else { if (tNext.y > t1) break; c.y += int(sgn.y); tNext.y += tDelta.y; }
    }
  }
  return best;
}

// ---------------------------------------------------------------- airport structures (runway frame)
// kinds: 0 arched hangar, 1 control tower, 2 terminal, 3 gabled shed, 4 fuel tank, 5 radome
vec2 traceBoxes(vec3 ro, vec3 rd, float tmax, out vec3 nOut, out float kind, out vec3 localHit){
  float best = tmax; vec2 res = vec2(-1.0);
  // buildings are stored per airport: skip every airport whose world bounds the ray misses (most of them)
  for (int ap = 0; ap < 16; ap++) {
    if (ap >= uApCount) break;
    vec4 A0 = dataAt(384 + ap), A1 = dataAt(400 + ap);   // xyz bounds, w = first box / box count
    vec3 nap; vec2 ab = iBox(ro, rd, A0.xyz, A1.xyz, nap);
    if (ab.x > ab.y || ab.y < 0.0 || ab.x > best || A1.w < 0.5) continue;
    int i0 = int(A0.w + 0.5), i1 = i0 + int(A1.w + 0.5);
  for (int i = i0; i < i1; i++){
    vec4 BC = dataAt(64 + i), BH = dataAt(192 + i);
    int ai = int(BC.w);
    vec4 a = uAp[ai];
    float s = sin(a.w), c = cos(a.w);
    vec3 o = ro - vec3(a.x, a.z, a.y);
    vec3 lo = vec3(o.x*c + o.z*s, o.y, o.x*s - o.z*c) - BC.xyz;
    vec3 ld = vec3(rd.x*c + rd.z*s, rd.y, rd.x*s - rd.z*c);
    vec3 H = BH.xyz; int k = int(BH.w + 0.5);
    // quick reject with the bounding box (generous for towers/radomes)
    vec3 nb; vec3 bh = k == 1 || k == 5 ? vec3(H.x*2.0, H.y*1.2, H.z*2.0) : H;
    vec2 bb = iBox(lo, ld, -bh, bh, nb);
    if (bb.x > bb.y || bb.y < 0.0 || bb.x > best) continue;
    float tHit = 1e9; vec3 nl = nb;
    if (k == 0) {
      float rise = H.y*0.9, wallTop = H.y - rise;
      float R = (H.z*H.z + rise*rise)/(2.0*rise);
      vec3 nc; vec2 cy = iXCyl(lo, ld, H.y - R, 0.0, R, nc);
      vec3 nbx; vec2 bx = iBox(lo, ld, -H, H, nbx);
      float tN = max(bx.x, cy.x), tF = min(bx.y, cy.y);
      if (tN < tF && tF > 0.0) { tHit = tN; nl = tN == cy.x ? nc : nbx; }

    } else if (k == 1) {
      vec3 n1, n2, n3, n4;
      float Ht = H.y*2.0;
      vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x, Ht*0.78, n1);
      vec2 c2 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.78, 0.0), H.x*1.5, Ht*0.15, n2);
      vec2 c3 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.93, 0.0), H.x*1.7, Ht*0.04, n3);
      vec2 c4 = iVCyl(lo, ld, vec3(0.0, -H.y + Ht*0.97, 0.0), 0.12, Ht*0.15, n4);
      if (c1.x < c1.y && c1.x > 0.0 && c1.x < tHit) { tHit = c1.x; nl = n1; }
      if (c2.x < c2.y && c2.x > 0.0 && c2.x < tHit) { tHit = c2.x; nl = n2; }
      if (c3.x < c3.y && c3.x > 0.0 && c3.x < tHit) { tHit = c3.x; nl = n3; }
      if (c4.x < c4.y && c4.x > 0.0 && c4.x < tHit) { tHit = c4.x; nl = n4; }
    } else if (k == 3) {
      vec4 pl[7];
      float rh = H.y*0.5, top = H.y*0.4;
      pl[0] = vec4(1,0,0,H.x); pl[1] = vec4(-1,0,0,H.x); pl[2] = vec4(0,0,1,H.z); pl[3] = vec4(0,0,-1,H.z); pl[4] = vec4(0,-1,0,H.y);
      vec3 n1 = normalize(vec3(rh/H.x, 1.0, 0.0)), n2 = normalize(vec3(-rh/H.x, 1.0, 0.0));
      pl[5] = vec4(n1, dot(n1, vec3(0.0, top + rh, 0.0))); pl[6] = vec4(n2, dot(n2, vec3(0.0, top + rh, 0.0)));
      vec3 nc; vec2 r = iConvex(lo, ld, pl, 7, nc);
      if (r.x < r.y && r.y > 0.0) { tHit = r.x; nl = nc; }
    } else if (k == 4) {
)"
R"(      vec3 n1; vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x, H.y*2.0, n1);
      if (c1.x < c1.y && c1.y > 0.0) { tHit = c1.x; nl = n1; }
    } else if (k == 5) {
      vec3 n1; vec2 c1 = iVCyl(lo, ld, vec3(0.0, -H.y, 0.0), H.x*0.4, H.y*1.1, n1);
      if (c1.x < c1.y && c1.x > 0.0) { tHit = c1.x; nl = n1; }
      vec3 sc = vec3(0.0, -H.y + H.y*1.1 + H.x*0.8, 0.0);
      vec3 oc = lo - sc; float b = dot(oc, ld), cc = dot(oc, oc) - H.x*H.x, h = b*b - cc;
      if (h > 0.0) { float ts = -b - sqrt(h); if (ts > 0.0 && ts < tHit) { tHit = ts; nl = normalize(lo + ld*ts - sc); } }
    } else {
      if (bb.x > 0.0) { tHit = bb.x; nl = nb; }
    }
    if (tHit > 0.0 && tHit < best) {
      best = tHit; res = vec2(tHit, float(i)); kind = float(k);
      nOut = vec3(nl.x*c + nl.z*s, nl.y, nl.x*s - nl.z*c);
      localHit = lo + ld*tHit;
    }
  }
  }
  return res;
}


// ---------------------------------------------------------------- XR-9 display screens and HUD
float hudLine(float d, float w){ return 1.0 - smoothstep(w*0.5, w*1.5, d); }
float hudNum(vec2 q, float v, int nd, vec2 cell){   // q: origin at the field's lower-left, digits left to right
  float on = 0.0; v = max(floor(v + 0.5), 0.0);
  for (int i = 0; i < 6; i++) {
    if (i >= nd) break;
    float pw = pow(10.0, float(nd - 1 - i));
    int dg = int(mod(floor(v/pw), 10.0));
    vec2 c = (q - vec2(float(i)*cell.x*1.35, 0.0))/cell;
    on = max(on, seg7(c, dg));
  }
  return on;
}
float hudBox(vec2 q, vec2 c, vec2 h, float w){ vec2 d = abs(q - c) - h; return hudLine(abs(max(d.x, d.y)), w); }
// ---------------------------------------------------------------- XR-9 cockpit display pages (procedural)
float mLine(float d, float w){ return 1.0 - smoothstep(w*0.5, w*1.5, d); }
float mArc(vec2 uv, float r, float a0, float a1, float w){   // angles from +x, counter-clockwise
  float a = atan(uv.y, uv.x); return mLine(abs(length(uv) - r), w)*step(a0, a)*step(a, a1);
}
vec3 mfdPage(int page, vec2 uv){
  const vec3 G = vec3(0.25, 1.0, 0.7), A = vec3(1.0, 0.62, 0.15), W = vec3(0.9, 0.95, 1.0), R = vec3(1.0, 0.25, 0.2), C = vec3(0.3, 0.8, 1.0);
  float w = 0.035; vec3 c = vec3(0.0);
  float spool = uHud3.x, thr = uHud2.y, ab = smoothstep(0.85, 1.0, spool);
  if (page == 0) {          // ENGINES: twin N1 arcs with needles and readouts, reheat flag
    for (int e = 0; e < 2; e++) {
      vec2 o = uv - vec2(e == 0 ? -0.52 : 0.52, 0.12);
      float a = atan(o.y, o.x), rr = length(o);
      float aa = a < -1.6 ? a + 6.2832 : a;               // gauge sweeps 270 deg from 225 deg down to -45 deg
      float frac = (3.927 - aa)/4.712;
      float arcOn = mLine(abs(rr - 0.38), w*1.6)*step(0.0, frac)*step(frac, 1.0);
      c += (frac < spool ? (frac > 0.85 ? A : G) : G*0.2)*arcOn;
      float na = 3.927 - spool*4.712; vec2 nd = vec2(cos(na), sin(na));
      c += W*mLine(abs(o.x*nd.y - o.y*nd.x), w)*step(0.0, dot(o, nd))*step(rr, 0.34);
      c += G*hudNum(o - vec2(-0.2, -0.5), spool*100.0, 3, vec2(0.1, 0.17));
    }
    c += A*ab*step(abs(uv.x), 0.25)*step(abs(uv.y + 0.82), 0.1)*(0.6 + 0.4*sin(uTime*20.0));
  } else if (page == 1) {   // POWER: thrust, energy cell, core temperature, nozzle bars + Mach readout
    for (int b = 0; b < 4; b++) {
      float x0 = -0.75 + float(b)*0.5;
      float lv = b == 0 ? thr : b == 1 ? 1.0 : b == 2 ? 0.35 + 0.6*spool : uHud2.z;
      vec2 o = uv - vec2(x0, -0.2);
      float frame = mLine(abs(max(abs(o.x) - 0.12, abs(o.y) - 0.55)), w);
      float fill = step(abs(o.x), 0.09)*step(o.y + 0.52, lv*1.04);
      c += G*0.6*frame + (b == 2 && lv > 0.85 ? A : (b == 1 ? C : G))*fill*0.8;
    }
    c += W*hudNum(uv - vec2(-0.45, 0.55), uHud.w*100.0, 3, vec2(0.12, 0.2));
    c += W*step(length(uv - vec2(-0.27, 0.56)), 0.025);
  } else if (page == 2) {   // MAP: heading-up moving terrain map, 6 km range
    float h = radians(uHud.z); vec2 f = vec2(sin(h), -cos(h)), rt = vec2(cos(h), sin(h));
    vec2 wp = uPlanePos.xz + (rt*uv.x + f*uv.y)*6000.0;
    float hh = baseAt(wp).x;
    vec3 land = mix(vec3(0.1, 0.35, 0.12), vec3(0.45, 0.35, 0.2), smoothstep(150.0, 900.0, hh));
    land = mix(land, vec3(0.85), smoothstep(1300.0, 1700.0, hh));
    c = hh < 0.0 ? vec3(0.02, 0.12, 0.3)*(1.0 + hh/400.0) : land*(0.6 + 0.4*fract(hh/100.0 + 0.0)*0.3);
    c *= 0.9; c += G*0.6*mLine(abs(length(uv) - 0.5), w*0.6);
    vec2 a = uv; float tri = step(abs(a.x)*1.6 + a.y*0.8, 0.09)*step(-0.1, a.y);
    c = mix(c, W, tri);
    vec2 nn = -vec2(dot(vec2(0.0, -1.0), rt), dot(vec2(0.0, -1.0), f))*0.85;   // north marker
    c = mix(c, R, step(length(uv - nn), 0.06));
  } else if (page == 3) {   // ADI: attitude with pitch ladder and bank scale
    vec3 fw = uPlaneRot*vec3(0.0, 0.0, -1.0), rw = uPlaneRot*vec3(1.0, 0.0, 0.0), uw = uPlaneRot*vec3(0.0, 1.0, 0.0);
    float pitch = asin(clamp(fw.y, -1.0, 1.0)), bank = atan(-rw.y, uw.y);
    vec2 o = rot2(uv, bank);
    float hz = o.y + pitch*2.2;
    c = hz > 0.0 ? vec3(0.05, 0.25, 0.65) : vec3(0.4, 0.22, 0.08);
    c = mix(c, W, mLine(abs(hz), w));
    for (int k = -3; k <= 3; k++) { if (k == 0) continue; float y = hz - float(k)*0.17453*2.2*0.5; c = mix(c, W*0.85, mLine(abs(y), w*0.6)*step(abs(o.x), k % 2 == 0 ? 0.25 : 0.12)); }
    c = mix(c, A, (mLine(abs(uv.y), w*1.5)*step(0.12, abs(uv.x))*step(abs(uv.x), 0.45)) + step(length(uv), 0.04));
    c = mix(c, W, mArc(uv, 0.85, 0.785, 2.356, w*0.8));
    c = mix(c, A, step(abs(atan(o.x, o.y)), 0.06)*step(abs(length(uv) - 0.78), 0.05));
  } else if (page == 4) {   // VTOL: silhouette, nozzle vector, thrust arrow, gear lights
    vec2 o = uv - vec2(-0.15, 0.25);
    c += C*0.8*mLine(abs(length(o/vec2(0.7, 0.12)) - 1.0)*0.12, w);
    float a = uHud2.z*1.5708; vec2 nd = vec2(cos(a), -sin(a));
    vec2 tq = uv - vec2(0.55, 0.22);
    float along = dot(tq, nd), side = abs(tq.x*nd.y - tq.y*nd.x);
    c += A*mLine(side, w*1.8)*step(0.0, along)*step(along, 0.15 + 0.55*thr);
    c += G*0.4*mArc(uv - vec2(0.55, 0.22), 0.45, -1.5708, 0.0, w*0.6);
    for (int g = 0; g < 3; g++) { vec2 gq = uv - vec2(-0.5 + float(g)*0.25, -0.65); c += (uHud2.w > 0.5 ? G : R*0.6)*step(max(abs(gq.x), abs(gq.y)), 0.08); }
    c += (uHud2.z > 0.99 ? C : G*0.3)*step(abs(uv.x - 0.55), 0.3)*step(abs(uv.y + 0.65), 0.08)*(uHud2.z > 0.99 ? 0.8 + 0.2*sin(uTime*6.0) : 1.0);
  } else if (page == 5) {   // G-METER and angle of attack
    float gv = uHud2.x;
    float a = 3.1416 - clamp((gv + 3.0)/15.0, 0.0, 1.0)*3.1416;
    vec2 o = uv - vec2(0.0, -0.25);
    c += G*0.6*mArc(o, 0.7, 0.0, 3.1416, w);
    c += R*mArc(o, 0.7, 0.0, 3.1416*(1.0 - 12.0/15.0), w*2.5);
    vec2 nd = vec2(cos(a), sin(a));
    c += W*mLine(abs(o.x*nd.y - o.y*nd.x), w)*step(0.0, dot(o, nd))*step(length(o), 0.66);
    c += W*hudNum(uv - vec2(-0.3, -0.85), abs(gv)*10.0, 3, vec2(0.15, 0.25));
    c += A*step(abs(uv.x + 0.85), 0.05)*step(abs(uv.y - (clamp(uHud3.y/30.0, -1.0, 1.0)*0.7)), 0.05);
  } else {                  // COMPASS rose, heading and altitude readouts
    float h = radians(uHud.z);
    vec2 o = rot2(uv - vec2(0.0, -0.1), -h);
    float rr = length(uv - vec2(0.0, -0.1)), a = atan(o.x, o.y);
    c += G*0.8*mLine(abs(rr - 0.62), w)*1.0 + G*step(abs(rr - 0.56), 0.05)*step(abs(fract(a/0.5236 + 0.5) - 0.5), 0.04);
    c += R*step(length(o - vec2(0.0, 0.5)), 0.05);
    c += A*step(abs(uv.x), 0.03)*step(abs(uv.y - 0.6), 0.1);
    c += W*hudNum(uv - vec2(-0.25, -0.2), uHud.z, 3, vec2(0.13, 0.22));
    c += G*hudNum(uv - vec2(-0.5, -0.9), uHud.y*3.28084, 5, vec2(0.12, 0.18));
  }
  return c;
}
// Emissive light inside the sealed cockpit: main display glow, LED strips, footwell, console screens
vec3 cockpitLight(vec3 lp, vec3 ln, vec3 E){
  vec3 L = vec3(0.012, 0.016, 0.022);
  vec3 P[6] = vec3[6](E + vec3(0.0, 0.03, -0.5), E + vec3(0.0, 0.58, 0.1), E + vec3(0.0, -0.72, -0.5), E + vec3(-0.45, -0.36, 0.1), E + vec3(0.45, -0.36, 0.1), E + vec3(0.0, -0.3, -0.5));
  vec3 Cl[6] = vec3[6](vec3(0.5, 0.62, 0.75)*1.4, uColStripe*0.45, vec3(1.0, 0.5, 0.15)*0.5, uColStripe*0.3, uColStripe*0.3, vec3(0.3, 0.9, 0.65)*0.35);
  for (int i = 0; i < 6; i++) {
    vec3 d = P[i] - lp; float dl = length(d);
    L += Cl[i]*max(dot(ln, d/dl), 0.0)*(0.6 + 0.4*float(i == 0))/(1.0 + dl*dl*7.0);
  }
  return L;
}
vec3 jetScreen(vec3 col, vec3 rd, int id, vec3 sl){
  vec3 E = uM[22].xyz; vec3 q = sl - E;
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
)"
R"(      float tall = abs(fract(hd/30.0 + 0.5) - 0.5)*30.0 < 0.5 ? 1.0 : 0.0;
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
    vec2 nb = h - vec2(0.33, -0.16);                                          // nozzle angle arc
    float na = atan(-nb.y, nb.x); float nr = length(nb);
    hud = max(hud, hudLine(abs(nr - 0.06), px)*step(0.0, na)*step(na, 1.5708)*0.6);
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
// ---------------------------------------------------------------- research jet exhaust plumes
// Volumetric emission marched through each plume (body space). Dry thrust: a tight blue-white plasma cone.
// Reheat: a long white-hot core in an amber envelope, threaded with bright Mach disks (shock diamonds),
// with turbulent flicker scrolling downstream.
vec3 plumeOne(vec3 lo, vec3 ld, float tmax, vec3 o, vec3 ax, float jit){
  float sp = uFlame.x, ab = uFlame.y;
  float L = mix(2.0, 6.0, sp) + 9.0*ab;
  vec3 c = o + ax*(L*0.5); float br = L*0.5 + 0.9;
  vec3 oc = lo - c; float b = dot(oc, ld), h = b*b - dot(oc, oc) + br*br;
  if (h <= 0.0) return vec3(0.0);
  h = sqrt(h); float t0 = max(-b - h, 0.0), t1 = min(-b + h, tmax);
  if (t1 <= t0) return vec3(0.0);
  vec3 ay = normalize(cross(ax, vec3(1.0, 0.0, 0.0)));
  float spacing = 0.95 + 0.35*clamp(uFlame.w, 0.0, 2.5);
  vec3 cOut = mix(vec3(0.12, 0.28, 1.0), vec3(1.0, 0.3, 0.05), ab);
  vec3 cIn = mix(vec3(0.55, 0.8, 1.0), vec3(1.0, 0.7, 0.35), ab);
  vec3 cDisk = vec3(0.75, 0.88, 1.0);
  float dt = (t1 - t0)/32.0;
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 32; i++) {
    vec3 q = lo + ld*(t0 + (float(i) + jit)*dt) - o;
    float x = dot(q, ax);
    if (x < -0.05 || x > L) continue;
    float u = max(x, 0.0)/L;
    float nx = q.x, ny = dot(q, ay);
    // 2D nozzle: a flat rectangular jet that rounds out and tapers downstream; shocks pinch the reheat plume
    float ph = x/spacing;
    float pinch = 1.0 - 0.18*ab*(0.5 + 0.5*cos(ph*6.2832))*(1.0 - u);
    float wx = mix(0.40, 0.15, u)*pinch*(1.0 + 0.6*ab*u), wy = mix(0.27, 0.15, u)*pinch*(1.0 + 0.8*ab*u);
    float e2 = (nx*nx)/(wx*wx) + (ny*ny)/(wy*wy);
    if (e2 > 9.0) continue;
    float turb = vnoise(vec2(x*2.6 - uTime*48.0, nx*5.0 + ny*7.0)) * 0.6 + vnoise(vec2(x*6.0 - uTime*90.0, ny*11.0 - nx*6.0))*0.4;
    float fall = smoothstep(0.0, 0.06, u + 0.02)*pow(1.0 - u, 1.4);
    float env = exp(-e2*1.5)*smoothstep(0.25, 0.75, turb + 0.35*(1.0 - u));
    float core = exp(-e2*4.0)*smoothstep(1.0, 0.2, u + 0.25*(1.0 - ab));
    float disk = pow(0.5 + 0.5*cos((ph - 0.15)*6.2832), 8.0)*exp(-e2*3.0)*smoothstep(0.3, 0.8, ph)*(1.0 - smoothstep(0.3, 0.8, u));
    vec3 e = cOut*env*(1.3 - 0.4*ab) + cIn*core*(7.0 - 1.0*ab) + cDisk*disk*(4.0*sp + 8.0*ab);
    acc += e*fall*dt;
  }
  acc *= (0.25 + 0.75*sp)*(0.9 + 0.1*sin(uTime*63.0));
  return acc/(1.0 + max(acc.r, max(acc.g, acc.b))*0.2);   // hue-preserving roll-off: looking down the plume stays amber
}
vec3 jetPlumes(vec3 ro, vec3 rd, float tmax, float jit){
  if (uFlame.x < 0.02) return vec3(0.0);
  mat3 inv = transpose(uPlaneRot);
  vec3 lo = inv*(ro - uPlanePos), ld = inv*rd;
  float a = uFlame.z;
  vec3 ax = vec3(0.0, -sin(a), cos(a));
  vec3 col = vec3(0.0);
  for (int s = -1; s <= 1; s += 2) {
    vec3 o = vec3(0.82*float(s), -0.12, 7.75) + ax*1.0;   // nozzle exit (pivot + 1 m along the swivelled axis)
    col += plumeOne(lo, ld, tmax, o, ax, jit);
  }
  return col;
}
void main(){
  vec2 ndc = (vUV + uJit)*2.0 - 1.0;
  vec3 rd = normalize(uCamRot * vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 ro = uCamPos;
  float jitter = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + uSeed);   // interleaved gradient noise, rotated per frame
  float tmax = 80000.0;
  // research jet cockpit: display screens show the outside world (re-traced without the airframe); the rest of
  // the sealed pod hides everything beyond it
  bool onScr = false; int scrId = 0; vec3 scrL = vec3(0.0);
  // pod = a pixel on the sealed cockpit's interior: it is shaded from the cockpit alone, and nothing outside the
  // aircraft (terrain, water, buildings, clouds, shadows) is traced for it. Only the display screens see out.
  bool pod = false; vec2 h0 = vec2(-1.0);
  if (uPlaneOn == 1 && uPS.w > 0.5 && int(uM[0].z + 0.5) == 5 && uWreck == 0) {
    h0 = tracePlane(ro, rd, 6.0);
    if (h0.x > 0.0) {
      int id0 = int(h0.y + 0.5);
      if (id0 >= 41 && id0 <= 43) { onScr = true; scrId = id0; scrL = transpose(uPlaneRot)*(ro + rd*h0.x - uPlanePos); }
      else { pod = true; tmax = h0.x + 0.05; }
    }
  }
  float tT = pod ? -1.0 : traceTerrain(ro, rd, tmax);
  float tW = (!pod && rd.y < 0.0 && ro.y > 0.0) ? -ro.y/rd.y : -1.0;
  vec3 bn; float bkind = 0.0; vec3 bl;
  vec2 bh = pod ? vec2(-1.0) : traceBoxes(ro, rd, tT > 0.0 ? tT : tmax, bn, bkind, bl);
  vec2 ph = onScr ? vec2(-1.0) : (pod ? h0 : tracePlane(ro, rd, tmax));
  float tSoFar = min(tT > 0.0 ? tT : (tW > 0.0 ? tW : tmax), tmax);
  vec3 tn; vec4 tinfo;
  float tB = pod ? -1.0 : traceTowns(ro, rd, tSoFar, tn, tinfo);
  float t = 1e9; int hit = 0;
  if (tT > 0.0) { t = tT; hit = 1; }
  if (tW > 0.0 && tW < t) { t = tW; hit = 2; }
  if (bh.x > 0.0 && bh.x < t) { t = bh.x; hit = 3; }
  if (tB > 0.0 && tB < t) { t = tB; hit = 5; }
  if (ph.x > 0.0 && ph.x < t) { t = ph.x; hit = 4; }
  vec3 dn; float dChar = 0.0;
  float tD = uDebN > 0 && !pod ? traceDebris(ro, rd, t < 1e8 ? t : tmax, dn, dChar) : -1.0;
  if (tD > 0.0 && tD < t) { t = tD; hit = 6; }
  vec3 col;
  float taaFlag = hit == 4 ? (uWreck > 0 ? 0.2 : 0.5) : (hit == 6 ? 0.2 : 1.0);   // 1 world, 0.5 rigid with the aircraft, 0.2 moving, 0 no history
  if (onScr) taaFlag = 0.0;
  if (hit == 0) { col = skyColor(rd); t = 1e6; }
  else {
    vec3 p = ro + rd*t;
    float sunVis = smoothstep(-0.05, 0.05, uSunDir.y);
    if (hit == 1) {
      vec3 n = terrainNormal(p.xz, t);
      vec4 base = baseAt(p.xz);
      Mat m = terrainMaterial(p, n, t, base);
      vec3 ns = applyTS(n, m.nrm, t < 2000.0 ? 0.6 : 0.25);
      float sh = sunVis > 0.0 ? terrainShadow(p + n*0.5, uSunDir, t) : 0.0;
      if (t < 3000.0) sh *= planeShadow(p + n*0.2, uSunDir);
      sh *= cloudShadow(p);
      col = shadeSurface(p, ns, rd, m, sh);
    } else if (hit == 2) {
      // ocean
      float depth = max(-groundH(p.xz, 5), 0.0);
      // wave normals band-limited to the pixel footprint (fades each octave before it can alias into sparkle)
      float foot = t*2.0*uTanHalf/uRes.y;                         // metres per pixel at this distance
      vec2 w = p.xz*0.05 + uTime*vec2(0.3, 0.2);
      float a1 = 1.0 - smoothstep(2.5, 7.0, foot), a2 = 1.0 - smoothstep(0.8, 2.2, foot), a3 = 1.0 - smoothstep(0.15, 0.45, foot), a4 = 1.0 - smoothstep(0.05, 0.14, foot);
      vec3 n1 = noised(w*1.0); vec3 n2 = noised(w*3.1 + 5.0); vec3 n3 = noised(p.xz*0.9 + uTime*vec2(-0.9, 0.7));
      vec3 n4 = a4 > 0.0 ? noised(p.xz*3.3 + uTime*vec2(1.3, -1.1)) : vec3(0.0);
      vec3 n0 = noised(p.xz*0.004 + uTime*vec2(0.02, 0.013));      // long swell, never aliases
      float amp = 0.12 + 0.12*uStorm + 0.04*uWet;
      vec2 sl = (n1.yz*0.6*a1 + n2.yz*0.3*a2)*amp + n3.yz*0.035*a3 + n4.yz*0.012*a4 + n0.yz*0.02;
      vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
      float rough = clamp(foot*0.02 + (1.0 - a2)*0.25, 0.0, 0.6);   // lost wave detail becomes statistical roughness
      vec3 v = -rd;
      float fk = clamp(1.0 - dot(n, v), 0.0, 1.0); float fres = 0.02 + 0.98*fk*fk*fk*fk*fk;
      vec3 r = reflect(rd, n); r.y = abs(r.y);
      vec3 refl = skyColor(r);
      // reflected clouds (cheap)
      if (uCloudCover > 0.05 && uQuality > 0) { vec4 cl = traceClouds(p, r, 30000.0, 0.5); refl = refl*cl.a + cl.rgb; }
      float sh = sunVis > 0.0 ? terrainShadow(p + vec3(0,1,0), uSunDir, t) * cloudShadow(p) : 0.0;
      vec4 base = baseAt(p.xz);
      vec3 deep = mix(vec3(0.004,0.03,0.06), vec3(0.003,0.02,0.035), base.w);
      vec3 shallow = mix(vec3(0.02,0.16,0.17), vec3(0.03,0.30,0.29), base.z) * (1.0 - 0.7*base.w);
      vec3 water = mix(shallow, deep, smoothstep(0.0, 18.0, depth));
      vec3 lit = water*(uSunCol*max(uSunDir.y,0.0)*1.0*sh + ambientLight(vec3(0,1,0))*0.35);
      float foam = smoothstep(0.7, 0.0, depth) * smoothstep(0.55, 0.85, vnoise(p.xz*0.15 + uTime*0.4) + 0.3*sin(depth*4.0 - uTime*1.5));
      lit = mix(lit, vec3(0.85)*(uSunCol*max(uSunDir.y,0.0)*1.5 + ambientLight(vec3(0,1,0))), foam*0.8);
      vec3 h = normalize(v + uSunDir);
      float ex = mix(900.0, 40.0, rough/0.6);
      float spec = pow(max(dot(n, h), 0.0), ex)*(ex + 8.0)/(900.0 + 8.0)*120.0 + pow(max(dot(n,h),0.0), 90.0*(1.0 - rough))*1.5;
      col = mix(lit, refl, fres) + uSunCol*spec*sh*(1.0 - smoothstep(0.5, 1.0, uCloudCover));
)"
R"(      // night: runway/town light reflections handled by overlay pass glow
    } else if (hit == 3 || hit == 5) {
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1); m.rough = 0.7; m.alb = vec3(0.7);
      vec3 nn = hit == 3 ? bn : tn;
      vec3 nTS = vec3(0,0,1);
      if (hit == 5) m = buildingMaterial(p, nn, tinfo);
      else {
        int k = int(bkind + 0.5); vec3 lh = bl; vec3 H = dataAt(192 + int(bh.y)).xyz;
        if (k == 0) {        // arched hangar: corrugated metal skin, big sliding doors facing the runway
          vec4 tx = triSample(lh*vec3(1.0, 1.0, 1.0), nn, M_CORRUGATED, 2.0, nTS);
          m.alb = tx.rgb*vec3(0.75, 0.78, 0.8); m.rough = tx.a; m.metal = 0.7; m.nrm = nTS;
          if (abs(bn.y) < 0.6 && abs(lh.z) < H.z*0.85 && lh.y < H.y*0.2 && abs(abs(lh.x) - H.x) < 0.3) {
            m.alb = vec3(0.35, 0.4, 0.45); if (fract(lh.z/4.0) < 0.03) m.alb *= 0.5; }
        } else if (k == 1) { // control tower: concrete shaft, glass cab
          float Ht = H.y*2.0, yy = lh.y + H.y;
          vec4 tx = triSample(lh, nn, M_CONCRETE, 3.0, nTS); m.alb = tx.rgb; m.rough = tx.a; m.nrm = nTS;
          if (yy > Ht*0.78 && yy < Ht*0.93 && abs(bn.y) < 0.5) { m.alb = vec3(0.03, 0.06, 0.07); m.rough = 0.04; m.metal = 0.5; m.emit = vec3(0.3, 0.7, 0.45)*uNight*0.6; }
          if (yy > Ht*0.93) m.alb = vec3(0.25);
          if (yy > Ht*0.97) { m.alb = vec3(0.8, 0.1, 0.1); m.emit = vec3(1.0, 0.1, 0.05)*step(0.5, fract(uTime*0.7))*2.0; }
        } else if (k == 2) { // terminal: glass curtain wall over a concrete base
          vec4 tx = triSample(lh, nn, M_CONCRETE, 4.0, nTS); m.alb = tx.rgb*0.95; m.rough = tx.a; m.nrm = nTS;
          if (abs(bn.y) < 0.5 && lh.y > -H.y + 1.0) {
            float mul = step(0.04, fract(lh.z/2.4))*step(0.06, fract((lh.y + H.y)/3.2));
            m.alb = mix(vec3(0.6), vec3(0.04, 0.07, 0.1), mul); m.rough = mix(0.4, 0.04, mul); m.metal = 0.5*mul;
            m.emit = vec3(1.0, 0.88, 0.7)*uNight*0.9*mul;
          }
          if (bn.y > 0.5) { m.alb = vec3(0.5); }
        } else if (k == 3) { // gabled shed / FBO house
          vec4 tx = triSample(lh, nn, abs(bn.y) > 0.3 ? M_TILES : M_PLASTER, 2.5, nTS);
          m.alb = tx.rgb*(abs(bn.y) > 0.3 ? vec3(0.9, 0.6, 0.5) : vec3(0.95, 0.93, 0.88)); m.rough = tx.a; m.nrm = nTS;
          if (abs(bn.y) < 0.3 && fract(lh.z/2.5) > 0.6 && lh.y > -H.y*0.5 && lh.y < 0.0) { m.alb = vec3(0.05); m.rough = 0.08; m.emit = vec3(1.0,0.8,0.5)*uNight*1.5; }
        } else if (k == 4) { // fuel tank
          vec4 tx = triSample(lh, nn, M_METAL, 3.0, nTS); m.alb = tx.rgb*vec3(0.95); m.rough = 0.35; m.metal = 0.6; m.nrm = nTS;
          if (abs(lh.y) < 0.5) m.alb = vec3(0.8, 0.15, 0.1);
        } else {             // radar dome on a pylon
          m.alb = vec3(0.92); m.rough = 0.5;
          if (lh.y < 0.0) { vec4 tx = triSample(lh, nn, M_METAL, 2.0, nTS); m.alb = tx.rgb*0.7; m.metal = 0.6; m.nrm = nTS; }
        }
      }
      vec3 ns = applyTS(nn, m.nrm, 0.5);
      float sh = sunVis > 0.0 ? terrainShadow(p + nn*0.3, uSunDir, t) : 0.0;
      col = shadeSurface(p, ns, rd, m, sh*cloudShadow(p));
    } else if (hit == 6) {
      // debris chunk: torn painted skin or charred metal
      vec3 nT; vec4 tx = triSample(p*2.0, dn, M_METAL, 1.0, nT);
      Mat m; m.metal = 0.5; m.emit = vec3(0.0); m.nrm = nT;
      float burn = vnoise(p.xz*3.0 + p.y);
      m.alb = dChar > 0.5 ? vec3(0.03, 0.028, 0.026)*(0.6 + burn) : uColBase*tx.rgb*(0.3 + 0.4*burn);
      m.rough = dChar > 0.5 ? 0.9 : 0.45;
      float sh = sunVis > 0.0 ? terrainShadow(p + dn*0.05, uSunDir, t) : 0.0;
      col = shadeSurface(p, applyTS(dn, m.nrm, 0.4), rd, m, sh);
    } else {
      // aircraft (or one wreck piece: gP* hold the transform of the piece that was hit)
      mat3 inv = transpose(gPR);
      vec3 lp = gPC + inv*(p - gPP);
      vec3 ln = planeNormal(lp);
      vec3 n = gPR*ln;
      int mid = int(ph.y + 0.5);
      if (mid == 11) { vec3 sc = fusSection(lp.z); vec3 rad = vec3(lp.x, lp.y - sc.z, 0.0); if (dot(ln, rad) > 0.55*length(rad) && lp.y > uM[22].y - 0.9) mid = 1; }
      Mat m; m.metal = 0.0; m.emit = vec3(0.0); m.nrm = vec3(0,0,1);
      m.alb = uColBase; m.rough = 0.28;
      bool interior = (mid >= 10 && mid <= 14) || mid >= 40;
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
          float px = t*2.0*uTanHalf/uRes.y;   // panel metres per pixel
          vec3 ic = vec3(0.0); float cov = 0.0;
          for (int si = 0; si < 4; si++) {
            vec2 o = (vec2(si & 1, si >> 1) - 0.5)*px*0.7;
            vec3 c4 = drawInstruments(q + o, ck, pilot);
            if (c4.x >= 0.0) { ic += c4; cov += 1.0; }
          }
          if (cov > 0.0) { ic /= cov; float k = cov*0.25; m.alb = mix(m.alb, ic*0.25, k); m.emit = ic*(0.3 + 0.6*uNight)*k; m.rough = mix(m.rough, 0.12, k); }
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
      if (mid >= 30) {  // XR-9 research jet surfaces
        vec3 nT; vec4 tx;
        float pulse = 0.75 + 0.25*sin(uTime*2.5);
        if (mid == 30 || mid == 31) {
          tx = triSample(lp, ln, M_PAINT, 0.7, nT);
          m.alb = uColBase*tx.rgb*1.6; m.rough = 0.55; m.metal = 0.08; m.nrm = nT;   // matte radar-absorbent coating: doesn't mirror the sky
          vec2 pl = abs(fract(lp.xz/vec2(0.9, 1.3)) - 0.5);           // panel seams
          if (max(pl.x, pl.y) > 0.49) m.alb *= 0.55;
          if (mid == 31 && abs(lp.x) > 4.6) m.alb = mix(m.alb, uColStripe*0.6, 0.6);
          if (mid == 30 && lp.z < -8.0) m.alb = vec3(0.03);              // radar nose cap
)"
R"(        } else if (mid == 32) { m.alb = vec3(0.3, 0.2, 0.06); m.metal = 0.95; m.rough = 0.06; }
        else if (mid == 33) {
          tx = triSample(lp, ln, M_METAL, 0.8, nT); m.alb = tx.rgb*vec3(0.2, 0.19, 0.2); m.metal = 0.6; m.rough = 0.5; m.nrm = nT;
          float heat = uCtl.w*uCtl.w;
          m.alb = mix(m.alb, vec3(0.16, 0.11, 0.17), 0.4*heat);   // heat-tinted titanium
        } else if (mid == 34) { m.alb = vec3(0.05); m.rough = 0.2; m.emit = uColStripe*(1.2 + 2.0*uNight)*pulse; }
        else if (mid == 35) { m.alb = vec3(0.06); m.metal = 0.8; m.rough = 0.35; m.emit = vec3(0.25, 0.6, 1.0)*uPS.y*uCtl.w*2.5; }
        else if (mid == 36) { float ab = uFlame.y, sp = uFlame.x; m.alb = vec3(0.02); m.emit = mix(vec3(0.35, 0.6, 1.0), vec3(1.0, 0.82, 0.6), ab)*(0.3 + 9.0*sp*sp + 16.0*ab); }
        else if (mid == 40) {  // sealed pod: carbon weave between structural ribs
          vec2 wv = floor(vec2(lp.x + lp.z, lp.y - lp.z)*55.0);
          m.alb = vec3(0.03, 0.032, 0.036)*(0.8 + 0.4*mod(wv.x + wv.y, 2.0)); m.rough = 0.35; m.metal = 0.2;
          float rib = abs(fract((lp.z - E.z)*4.0) - 0.5);
          if (rib > 0.46) { m.alb = vec3(0.07, 0.075, 0.08); m.metal = 0.7; m.rough = 0.3; }
          if (abs(lp.y - (E.y - 0.18)) < 0.004) m.emit = uColStripe*1.4*pulse;
        }
        else if (mid >= 41 && mid <= 43) { m.alb = vec3(0.0); m.rough = 0.05; }
        else if (mid == 44) {  // bezels and consoles: satin composite with machined edges and fasteners
          vec2 hx = lp.xz*45.0 + vec2(lp.y*30.0);
          m.alb = vec3(0.028, 0.03, 0.034)*(0.9 + 0.2*step(0.5, fract(hx.x + floor(hx.y)*0.5))); m.rough = 0.42; m.metal = 0.35;
          vec3 qd = lp - E.xyz; float rr = length(qd.xz), an = atan(qd.x, -qd.z);
          if (abs(fract(an*9.0) - 0.5) < 0.025 && rr < 0.7) m.alb *= 2.2;                 // panel seams
          if (length(vec2(fract(an*18.0) - 0.5, (qd.y - 0.36)*90.0)) < 0.12) { m.alb = vec3(0.35); m.metal = 1.0; m.rough = 0.25; }  // screws
        }
        else if (mid == 45 || mid == 52 || mid == 53) {  // multi-function displays
          vec3 qd = lp - E.xyz; int page; vec2 uv;
          if (mid == 45) {
            float rr = length(qd.xz), an = atan(qd.x, -qd.z);
            float k = clamp(floor(an/0.42 + 0.5), -2.0, 2.0);
            page = int(k) + 2; uv = vec2((an - k*0.42)*rr/0.07, (rr - 0.575)/0.05);
          } else {
            vec3 cq = vec3(abs(qd.x) - 0.52, qd.y + 0.44, qd.z - 0.08);
            page = mid == 52 ? 5 : 6; uv = vec2((cq.x - 0.02)/0.075*sign(qd.x), -(cq.z + 0.2)/0.06);
          }
          vec3 sc = mfdPage(page, uv);
          float edge = smoothstep(1.0, 0.92, max(abs(uv.x), abs(uv.y)));
          sc = sc*edge + vec3(0.01, 0.03, 0.04)*edge;                                         // dark-blue backlight
          sc *= 0.92 + 0.08*sin(uv.y*160.0);                                                  // pixel rows
          m.alb = vec3(0.01); m.rough = 0.06; m.metal = 0.0; m.emit = sc*1.5;
        }
        else if (mid == 46) { tx = triSample(lp, ln, M_LEATHER, 0.3, nT); m.alb = vec3(dot(tx.rgb, vec3(0.33)))*vec3(0.3, 0.32, 0.36); m.rough = tx.a; m.nrm = nT;
          if (abs(abs(lp.x - E.x) - 0.16) < 0.005) m.emit = uColStripe*0.9*pulse;
          if (lp.y > E.y + 0.12 && abs(lp.x - E.x) < 0.1 && ln.z > 0.5) m.emit = uColStripe*1.2; }
        else if (mid == 47) { m.alb = vec3(0.035); m.rough = 0.55; m.metal = 0.3; if (lp.y > E.y - 0.24 && ln.y > 0.3) m.emit = vec3(1.0, 0.45, 0.1)*0.8; }
        else if (mid == 48) { m.alb = vec3(0.1); m.emit = uColStripe*1.1*pulse; }
        else if (mid == 49) {  // annunciator strip: GEAR, BRK, AB, VTOL, MACH, G, LOW ALT, SYS
          vec3 qd = lp - E.xyz; float an = atan(qd.x, -qd.z);
          int cell = int(clamp(floor((an + 0.62)/0.155), 0.0, 7.0));
          float cx = abs(fract((an + 0.62)/0.155) - 0.5), cy = abs(qd.y - 0.348)/0.016;
          bool inCell = cx < 0.42 && cy < 0.75;
          float ab = smoothstep(0.85, 1.0, uHud3.x);
          vec3 on = cell == 0 ? (uHud2.w > 0.5 ? vec3(0.2, 1.0, 0.3) : vec3(0.0)) :
                    cell == 1 ? vec3(0.0) :
                    cell == 2 ? vec3(1.0, 0.5, 0.1)*ab :
                    cell == 3 ? vec3(0.2, 0.7, 1.0)*step(0.5, uHud2.z) :
                    cell == 4 ? vec3(0.4, 0.6, 1.0)*step(1.0, uHud.w) :
                    cell == 5 ? vec3(1.0, 0.15, 0.1)*step(9.0, abs(uHud2.x))*step(0.5, fract(uTime*3.0)) :
                    cell == 6 ? vec3(1.0, 0.15, 0.1)*step(uHud3.w, 60.0)*step(0.5, uHud2.z*0.0 + 1.0 - uHud2.w) :
                                vec3(0.2, 1.0, 0.3)*0.6;
          m.alb = vec3(0.02); m.rough = 0.1;
          m.emit = inCell ? on*2.0 + vec3(0.025, 0.03, 0.035) : vec3(0.0);
        }
        else if (mid == 54) {  // backlit keys
          vec3 qd = lp - E.xyz; vec3 cq = vec3(abs(qd.x) - 0.52, qd.y + 0.44, qd.z - 0.08) - vec3(0.0, 0.055, 0.12);
          vec2 cell = floor(cq.xz/0.032 + 0.5); float hk = hash2i(ivec2(cell) + ivec2(qd.x < 0.0 ? 11 : 37, 5));
          vec3 kc = hk < 0.15 ? vec3(1.0, 0.55, 0.15) : hk < 0.25 ? vec3(0.3, 1.0, 0.5) : uColStripe*0.6;
          m.alb = vec3(0.04); m.rough = 0.4; m.emit = ln.y > 0.6 ? kc*(0.12 + 0.5*step(0.85, hk)*step(0.5, fract(uTime*0.7 + hk*3.0))) : vec3(0.0);
        }
        else if (mid == 55) {  // overhead panel face: status LEDs beside each switch
          vec3 qd = lp - E.xyz - vec3(0.0, 0.5, -0.32); qd.yz = rot2(qd.yz, 0.55);
          vec2 c2 = floor(qd.xz/vec2(0.05, 0.06) + 0.5); vec2 f2 = qd.xz - c2*vec2(0.05, 0.06);
          float hk = hash2i(ivec2(c2) + ivec2(3, 9));
          float led = step(length(f2 - vec2(0.016, -0.018)), 0.0035);
          m.alb = vec3(0.025); m.rough = 0.5;
          m.emit = led*(hk < 0.7 ? vec3(0.2, 1.0, 0.35) : vec3(1.0, 0.6, 0.15)*step(0.5, fract(uTime + hk)))*1.5;
          if (abs(f2.y + 0.03) < 0.0015 && abs(f2.x) < 0.02) m.emit = vec3(0.4, 0.5, 0.55)*0.6;   // engraved labels
        }
        else if (mid == 56) { m.alb = vec3(0.06, 0.065, 0.07); m.rough = 0.9; if (abs(fract(lp.y*40.0) - 0.5) < 0.04) m.emit = vec3(1.0, 0.45, 0.1)*0.25; }
        else if (mid == 57) { m.alb = vec3(0.1); m.emit = vec3(1.0, 0.5, 0.15)*1.8; }
      }
      else {
        vec3 nT; vec4 tx;
        if (mid == 1 || mid == 2 || mid == 3 || mid == 5) {
          if (m.rough > 0.1) { tx = triSample(lp, ln, M_PAINT, 0.9, nT); m.alb *= tx.rgb*1.03; m.rough = mix(m.rough, tx.a, 0.6); m.nrm = nT; }
        }
        else if (mid == 6) { tx = triSample(lp, ln, M_RUBBER, 0.35, nT); m.alb = tx.rgb; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 8 || mid == 16 || mid == 17) { tx = triSample(lp, ln, M_METAL, 0.6, nT); m.alb *= tx.rgb*1.4; m.rough = mix(m.rough, tx.a, 0.5); m.nrm = nT; }
        else if (mid == 10 && m.emit.x + m.emit.y + m.emit.z <= 0.0) { tx = triSample(lp, ln, M_PLASTIC, 0.25, nT); m.alb = tx.rgb*0.9; m.rough = tx.a; m.nrm = nT; }
        else if (mid == 11) {
          bool flr = lp.y < E.y - 1.0;
          tx = triSample(lp, ln, flr ? M_CARPET : M_PLASTER, flr ? 0.4 : 0.8, nT);
          m.alb = flr ? tx.rgb : tx.rgb*vec3(0.72, 0.7, 0.66); m.rough = flr ? tx.a : 0.85; m.nrm = nT*0.5;
        }
        else if (mid == 12) { tx = triSample(lp, ln, ck == 2 ? M_LEATHER : M_FABRIC, 0.35, nT); m.alb = tx.rgb*(ck == 2 ? 1.2 : 1.0); m.rough = tx.a; m.nrm = nT; }
        else if (mid == 13 || mid == 14) { tx = triSample(lp, ln, M_PLASTIC, 0.2, nT); m.alb = tx.rgb*(mid == 14 ? 0.3 : 0.6); m.rough = mix(tx.a, 0.9, mid == 14 ? 0.6 : 0.0); m.nrm = nT; }
      }
      if (uWreck > 0) {  // fire-blackened, buckled skin with a few glowing embers near the breaks
        float burn = vnoise(lp.xz*2.3 + lp.y*1.7) + 0.5*vnoise(lp.yz*5.1);
        float cut = 1.0 - smoothstep(0.0, 0.6, -sdBox(lp - uPcC[gPI], uPcH[gPI]));
        float k = clamp(0.25 + 0.55*burn + 0.5*cut, 0.0, 1.0);
        m.alb = mix(m.alb, vec3(0.025, 0.022, 0.02), k); m.rough = mix(m.rough, 0.95, k); m.metal *= 1.0 - k;
        m.emit += vec3(1.0, 0.32, 0.06)*pow(clamp(burn*cut*0.9, 0.0, 1.0), 5.0)*(1.5 + sin(uTime*7.0 + lp.x*9.0))*3.0;
      }
      n = applyTS(n, m.nrm, interior ? 0.35 : 0.12);
      float sh = sunVis > 0.0 && mid < 40 ? planeShadow(p + n*0.02, uSunDir) * terrainShadow(p, uSunDir, 50.0) * cloudShadow(p) : 0.0;   // the sealed pod sees no sun
      if (mid >= 40) {  // sealed research cockpit: lit only by its displays and LED strips
        vec3 v = -rd;
        vec3 Li = cockpitLight(lp, ln, E.xyz);
        col = m.alb*Li*3.0 + m.emit;
        vec3 sDir = normalize(E.xyz + vec3(0.0, 0.03, -0.5) - lp);                          // reflection of the main display
        vec3 hh = normalize(v + gPR*sDir);
        float gl = pow(max(dot(n, hh), 0.0), mix(6.0, 220.0, 1.0 - m.rough));
        col += vec3(0.35, 0.45, 0.55)*gl*(1.0 - m.rough)*(0.04 + 0.25*m.metal);
      } else if (interior) {
        vec3 v = -rd;
        col = pbr(n, v, uSunDir, m.alb, m.rough, m.metal, uSunCol*sh*3.2) + m.alb*(ambientLight(n)*0.45 + ambientLight(vec3(0.0,1.0,0.0))*0.25) + m.emit;
      } else {
        col = shadeSurface(p, n, rd, m, sh);
        vec3 h = normalize(-rd + uSunDir);
        col += uSunCol*pow(max(dot(n, h), 0.0), 400.0)*sh*3.0*float(mid <= 5);
      }
    }
    if (!pod) col = applyFog(col, ro, rd, t);
  }
  // propeller discs (motion-blurred), composited over scene
  if (uPlaneOn == 1 && uWreck == 0) {
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
  // research jet exhaust plumes (additive, depth-limited by the scene)
  if (uPlaneOn == 1 && uWreck == 0 && uPS.w < 0.5 && int(uM[0].z + 0.5) == 5) col += jetPlumes(ro, rd, t, jitter);
  // clouds
)"
R"(  vec4 cl = pod ? vec4(0.0, 0.0, 0.0, 1.0) : traceClouds(ro, rd, t, jitter);
  col = col*cl.a + cl.rgb;
  if (onScr) col = jetScreen(col, rd, scrId, scrL);
  if (any(isnan(col)) || any(isinf(col)) || !(col.r + col.g + col.b < 1e7)) col = vec3(0.0);
  oColor = vec4(clamp(col, vec3(0.0), vec3(3e4)), taaFlag);
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
uniform sampler2D uDepth; uniform vec2 uRes; uniform vec3 uSunDir; uniform vec3 uSunCol; uniform vec3 uAmb; uniform float uFogB; uniform float uTime;
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
    float a = (exp(-r2*6.0) + 0.15*exp(-r2*1.5) - 0.0335)*(1.0 - smoothstep(0.6, 1.0, r2)); a = max(a, 0.0);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 2) { // checkpoint gate: segmented counter-rotating bands, sweeping highlight, inward pulses
    float r = sqrt(r2), ang = atan(c.y, c.x), act = vCol.a, T = uTime;
    float rim = smoothstep(0.03, 0.0, abs(r - 0.86)) + 0.45*exp(-abs(r - 0.86)*22.0);
    float outer = smoothstep(0.022, 0.0, abs(r - 0.935))*step(0.38, fract(ang*12.0/6.2832 + T*0.22));
    float inner = smoothstep(0.014, 0.0, abs(r - 0.77))*step(0.55, fract(ang*36.0/6.2832 - T*0.45))*0.8;
    float sweep = pow(max(cos(ang - T*2.4), 0.0), 18.0)*smoothstep(0.12, 0.0, abs(r - 0.86))*1.8;
    float sweep2 = pow(max(cos(ang + T*1.7 + 3.1416), 0.0), 30.0)*smoothstep(0.05, 0.0, abs(r - 0.935))*1.2;
    float pw = fract(T*0.55);
    float pulse = smoothstep(0.025, 0.0, abs(r - mix(0.84, 0.15, pw)))*(1.0 - pw)*0.7;
    float ticks = smoothstep(0.03, 0.0, abs(r - 0.70))*step(0.9, fract(ang*4.0/6.2832 + 0.125))*1.2;
    float film = 0.07*smoothstep(0.86, 0.3, r)*(0.55 + 0.45*sin(r*34.0 - T*5.0));
    float a = rim*1.25 + outer + inner + (sweep + sweep2 + pulse + ticks + film)*act;
    a *= 1.0 - smoothstep(0.97, 1.0, r);
    o = vec4(vCol.rgb*a*(0.35 + 0.65*act), 0.0);
  } else if (kind == 6) { // expanding shockwave / halo ring
    float r = sqrt(r2);
    float a = smoothstep(0.05, 0.0, abs(r - 0.88)) + 0.4*exp(-abs(r - 0.88)*12.0);
    a *= 1.0 - smoothstep(0.97, 1.0, r);
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
  } else if (kind == 7) { // spark: hot core with a small cross flare
    float a = exp(-r2*14.0) + 0.5*exp(-abs(c.x)*22.0)*exp(-c.y*c.y*3.0) + 0.5*exp(-abs(c.y)*22.0)*exp(-c.x*c.x*3.0);
    a = max(a - 0.02, 0.0)*(1.0 - smoothstep(0.7, 1.0, r2));
    o = vec4(vCol.rgb*a*vCol.a, 0.0);
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
  o.a *= soft; if (kind == 1 || kind == 2 || kind == 4 || kind == 6 || kind == 7) o.rgb *= soft;
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
  c = clamp(c, vec3(0.0), vec3(3e4));
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  if (!(l >= 0.0 && l < 1e6)) { oColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
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
// Temporal anti-aliasing resolve: reprojects the previous frame (world points through the camera; aircraft pixels
// through the aircraft's own motion), clamps it to the current neighbourhood and blends. Averages away the per-frame
// jitter of the ray marcher (clouds, distant terrain, water sparkle) and sub-pixel geometry.
static const char* kTaaFS = R"(#version 330 core
in vec2 vUV; layout(location=0) out vec4 oHist; layout(location=1) out vec4 oColor;
uniform sampler2D uRaw; uniform sampler2D uDepth; uniform sampler2D uHist; uniform vec2 uRes; uniform float uHistValid;
uniform vec2 uRawRes; uniform vec2 uJit;   // ray-trace resolution (<= uRes: temporal upscaling) and this frame's jitter
uniform vec3 uCamPos; uniform mat3 uCamRot; uniform vec3 uPrevCamPos; uniform mat3 uPrevCamRot; uniform float uTanHalf; uniform float uAspect;
uniform vec3 uPlanePos; uniform mat3 uPlaneRot; uniform vec3 uPrevPlanePos; uniform mat3 uPrevPlaneRot;
vec3 toY(vec3 c){ c = c/(1.0 + max(c.r, max(c.g, c.b))); return vec3(0.25*c.r + 0.5*c.g + 0.25*c.b, 0.5*c.r - 0.5*c.b, -0.25*c.r + 0.5*c.g - 0.25*c.b); }
vec3 fromY(vec3 y){ vec3 c = vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); return c/max(1.0 - max(c.r, max(c.g, c.b)), 1e-3); }
// 5-tap Catmull-Rom history fetch: keeps the accumulated image sharp
vec3 histCR(vec2 uv){
  vec2 sp = uv*uRes, tp = floor(sp - 0.5) + 0.5, f = sp - tp;
  vec2 w0 = f*(-0.5 + f*(1.0 - 0.5*f)), w1 = 1.0 + f*f*(-2.5 + 1.5*f), w2 = f*(0.5 + f*(2.0 - 1.5*f)), w3 = f*f*(-0.5 + 0.5*f);
  vec2 w12 = w1 + w2, t0 = (tp - 1.0)/uRes, t3 = (tp + 2.0)/uRes, t12 = (tp + w2/w12)/uRes;
  vec3 r = texture(uHist, vec2(t12.x, t0.y)).rgb*w12.x*w0.y + texture(uHist, vec2(t0.x, t12.y)).rgb*w0.x*w12.y
         + texture(uHist, t12).rgb*w12.x*w12.y + texture(uHist, vec2(t3.x, t12.y)).rgb*w3.x*w12.y + texture(uHist, vec2(t12.x, t3.y)).rgb*w12.x*w3.y;
  float ws = w12.x*w0.y + w0.x*w12.y + w12.x*w12.y + w3.x*w12.y + w12.x*w3.y;
  return max(r/ws, vec3(0.0));
}
void main(){
  // the raw frame was traced at a lower resolution, offset by this frame's jitter: reconstruct it at this pixel
  bool up = uRawRes.x < uRes.x - 0.5;
  vec2 ruv = vUV - uJit;
  ivec2 ip = up ? clamp(ivec2(floor(ruv*uRawRes)), ivec2(0), ivec2(uRawRes) - 1) : ivec2(gl_FragCoord.xy);
  vec4 cur = texelFetch(uRaw, ip, 0);
  float flag = cur.a;
  if (up) cur.rgb = texture(uRaw, ruv).rgb;
  vec3 m1 = vec3(0.0), m2 = vec3(0.0), cy = toY(cur.rgb);
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec3 y = toY(texelFetch(uRaw, clamp(ip + ivec2(i, j), ivec2(0), ivec2(uRawRes) - 1), 0).rgb);
    m1 += y; m2 += y*y;
  }
  m1 /= 9.0; vec3 sd = sqrt(max(m2/9.0 - m1*m1, 0.0));
  float t = texelFetch(uDepth, ip, 0).r;
  vec2 ndc = vUV*2.0 - 1.0;
  vec3 rd = normalize(uCamRot*vec3(ndc.x*uTanHalf*uAspect, ndc.y*uTanHalf, -1.0));
  vec3 d;
  if (t > 9e5) d = transpose(uPrevCamRot)*rd;          // sky: a direction, only camera rotation matters
  else {
    vec3 P = uCamPos + rd*t;
    if (flag > 0.4 && flag < 0.6) P = uPrevPlaneRot*(transpose(uPlaneRot)*(P - uPlanePos)) + uPrevPlanePos;
    d = transpose(uPrevCamRot)*(P - uPrevCamPos);
  }
  vec2 puv = d.z < -1e-4 ? vec2(d.x/(-d.z)/(uTanHalf*uAspect), d.y/(-d.z)/uTanHalf)*0.5 + 0.5 : vec2(-1.0);
  bool valid = uHistValid > 0.5 && flag > 0.1 && all(greaterThan(puv, vec2(0.0))) && all(lessThan(puv, vec2(1.0)));
  vec3 res = cur.rgb;
  if (valid) {
    vec3 hy = toY(histCR(puv));
    float k = flag > 0.9 ? 1.25 : 0.9;                  // tighter clamp for moving parts
    hy = clamp(hy, m1 - k*sd - 0.002, m1 + k*sd + 0.002);
    float motion = length((puv - vUV)*uRes);
    float a = mix(0.08, 0.3, clamp(motion/12.0, 0.0, 1.0));   // fast motion: lean on the new frame, less smear
    if (flag < 0.4) a = max(a, 0.4);
    res = fromY(mix(hy, cy, a));
  }
  oHist = vec4(res, flag);
  oColor = vec4(res, 1.0);
}
)";

static const char* kPostFS = R"(#version 330 core
in vec2 vUV; out vec4 oColor;
uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uTime; uniform vec2 uRes;
uniform float uRainLens; uniform vec2 uSunScreen; uniform float uSunVisible; uniform float uFade; uniform float uVignette; uniform float uGLoad;
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
  // FXAA (console variant) on the HDR scene, using a tonemapped luma estimate
  vec2 tp = 1.0/vec2(textureSize(uScene, 0));
  vec3 cM = texture(uScene, uv).rgb;
  vec3 cNW = texture(uScene, uv + vec2(-1.0, -1.0)*tp).rgb, cNE = texture(uScene, uv + vec2(1.0, -1.0)*tp).rgb;
  vec3 cSW = texture(uScene, uv + vec2(-1.0, 1.0)*tp).rgb, cSE = texture(uScene, uv + vec2(1.0, 1.0)*tp).rgb;
  vec3 lw = vec3(0.299, 0.587, 0.114);
  float lM = sqrt(dot(cM, lw)/(1.0 + dot(cM, lw))), lNW = sqrt(dot(cNW, lw)/(1.0 + dot(cNW, lw))), lNE = sqrt(dot(cNE, lw)/(1.0 + dot(cNE, lw)));
  float lSW = sqrt(dot(cSW, lw)/(1.0 + dot(cSW, lw))), lSE = sqrt(dot(cSE, lw)/(1.0 + dot(cSE, lw)));
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE))), lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  vec3 scene = cM;
  if (lMax - lMin > max(0.02, lMax*0.1)) {
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float red = max((lNW + lNE + lSW + lSE)*0.03125, 1.0/128.0);
    dir = clamp(dir/(min(abs(dir.x), abs(dir.y)) + red), -8.0, 8.0)*tp;
    vec3 rA = 0.5*(texture(uScene, uv + dir*(1.0/3.0 - 0.5)).rgb + texture(uScene, uv + dir*(2.0/3.0 - 0.5)).rgb);
    vec3 rB = rA*0.5 + 0.25*(texture(uScene, uv - dir*0.5).rgb + texture(uScene, uv + dir*0.5).rgb);
    float lB = sqrt(dot(rB, lw)/(1.0 + dot(rB, lw)));
    scene = (lB < lMin || lB > lMax) ? rA : rB;
  }
  vec3 c = scene + texture(uBloom, uv).rgb*0.9;
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
  // g-force tunnel: a red rim that deepens to dark red and then black at the screen edge and closes in as g builds
  if (uGLoad > 0.002) {
    float asp = uRes.x/uRes.y;
    float r = length(vv*vec2(asp, 1.0))/length(vec2(0.5*asp, 0.5));       // 0 centre .. 1 corner
    float g = uGLoad*(1.0 + 0.04*sin(uTime*7.5)*uGLoad);                 // a faint heartbeat pulse at high g
    float reach = mix(0.95, 0.12, g);
    float k = clamp((r - reach)/max(1.05 - reach, 0.05), 0.0, 1.0);    // depth into the band
    vec3 band = mix(vec3(0.7, 0.03, 0.02), vec3(0.22, 0.0, 0.0), smoothstep(0.0, 0.45, k));
    band = mix(band, vec3(0.0), smoothstep(0.35, 0.85, k));
    float a = smoothstep(0.0, 0.3, k)*clamp(0.2 + 0.9*g, 0.0, 1.0);
    c = mix(c*mix(vec3(1.0), vec3(1.0, 0.62, 0.58), g*0.35), band, a);
  }
  c += (h21(vUV*uRes + fract(uTime)*100.0) - 0.5)/255.0*2.0;
  c *= uFade;
  oColor = vec4(c, 1.0);
}
)";

// ------------------------------------------------------------------------------------------------
static const char* kUIVS = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aCol; layout(location=3) in vec4 aMode;
uniform vec2 uScreen; out vec2 vUV; out vec4 vCol; out float vMode; out vec2 vHalf; out float vP;
void main(){ vUV = aUV; vCol = aCol; vMode = aMode.x; vHalf = aMode.yz; vP = aMode.w; gl_Position = vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }
)";
static const char* kUIFS = R"(#version 330 core
in vec2 vUV; in vec4 vCol; in float vMode; in vec2 vHalf; in float vP; out vec4 oColor;
uniform sampler2D uFont; uniform sampler2D uImg;
float sdRR(vec2 p, vec2 h, float r){ vec2 q = abs(p) - h + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
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
  else if (vMode < 5.0) { float d = sdRR(vUV, vHalf, (vMode - 4.0)*1000.0); oColor = vec4(vCol.rgb, vCol.a*clamp(0.5 - d, 0.0, 1.0)); }
  else if (vMode < 6.0) { float d = sdRR(vUV, vHalf, (vMode - 5.0)*1000.0); oColor = vec4(vCol.rgb, vCol.a*clamp(0.5*vP + 0.5 - abs(d + 0.5*vP), 0.0, 1.0)); }
  else { float d = sdRR(vUV, vHalf, (vMode - 6.0)*1000.0); float k = clamp(1.0 - max(d, 0.0)/max(vP, 1.0), 0.0, 1.0);
    oColor = vec4(vCol.rgb, vCol.a*k*k*step(0.0, d)); }
}
)";
