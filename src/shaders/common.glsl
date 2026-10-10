//! kCommonGLSL
//! ------------------------------------------------------------------------------------------------
//! Shared noise + terrain (must match world.cpp exactly)

// Work counters for an analysis build (COST_MAP): each COST(k) counts one unit of the expensive
// work of category k in this pixel - 0 terrain height samples, 1 aircraft distance-field samples, 2 cloud march
// steps, 3 effect / light steps. In the game build they compile to nothing.
#ifdef COST_MAP
vec4 gCost = vec4(0.0);
#define COST(k) gCost[k] += 1.0
#else
#define COST(k)
#endif
const float WH = 40000.0;
const int HMN = 2048;
const float TEXEL = 39.0625;
const float PI = 3.14159265;
uniform sampler2D uHM;
uniform vec4 uCrater[24]; uniform int uCraterN;  // impact craters: x, z, radius, depth (negative depth: dark-energy blast)
uniform vec3 uCraterB;   // circle (x, z, radius) around every crater's influence
int craterAt(vec2 p, float k){ if (length(p - uCraterB.xy) > uCraterB.z) return -1;
  for (int i = 0; i < 24; i++) { if (i >= uCraterN) break; if (length(p - uCrater[i].xy) < uCrater[i].z*k) return i; } return -1; }
float craterH(vec2 p){ float h = 0.0;
  if (length(p - uCraterB.xy) > uCraterB.z) return 0.0;
  for (int i = 0; i < 24; i++) {   // (a bowl with a raised lip, its edge ragged in a few lobes: Game::wreckGround's craterShape, exactly)
    if (i >= uCraterN) break;
    vec4 c = uCrater[i]; vec2 q = p - c.xy; float r = length(q);
    if (r > 1.9*c.z) continue;
    float a = atan(q.y, q.x), s = fract(c.x*0.0137 + c.y*0.0191)*6.2832;
    float d = r/(c.z*(1.0 + 0.09*sin(3.0*a + s) + 0.05*sin(5.0*a + 2.3*s)));
    if (d > 1.8) continue;
    float D = abs(c.w);
    h += -D*max(1.0 - d*d, 0.0) + 0.22*D*exp(-(d - 1.0)*(d - 1.0)*14.0); }
  return h; }
float hash2i(ivec2 p){ uint h = uint(p.x)*0x8da6b343u + uint(p.y)*0xd8163841u; h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15; return float(h & 0xFFFFFFu)/16777216.0; }
float hash1(float n){ return fract(sin(n)*43758.5453); }
float hash3(vec3 p){ p = fract(p*0.3183099+0.1); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
// The islands span [-WH, WH], in 10 km of open sea all round, and there the map wraps: what is anywhere is what is at
// its place in [-50, 50) km (world.h wrapCoord, exactly)
vec2 wrapW(vec2 p){ return p - 100000.0*floor((p + 50000.0)/100000.0); }
vec4 baseAt(vec2 p){
  p = wrapW(p);
  vec2 f = (p + WH)/TEXEL - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl;
  ivec2 mx = ivec2(HMN-1);
  vec4 a = texelFetch(uHM, clamp(i, ivec2(0), mx), 0), b = texelFetch(uHM, clamp(i+ivec2(1,0), ivec2(0), mx), 0);
  vec4 c = texelFetch(uHM, clamp(i+ivec2(0,1), ivec2(0), mx), 0), d = texelFetch(uHM, clamp(i+ivec2(1,1), ivec2(0), mx), 0);
  vec4 r = (a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y;
  // past the islands' square: the open sea, settling to 80 m within 3 km (World::sampleBase, the same)
  float o = max(abs(p.x), abs(p.y)) - WH;
  if (o > 0.0) { float k = smoothstep(0.0, 3000.0, o); r.x = mix(r.x, -80.0, k); r.y *= 1.0 - k; }
  return r;
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
// (uRoadId: the old road-id texture's sampler, read by nothing since the road network - roads.glsl's uRoadGrid on the
// same unit - but declared as it was: the aircraft bodies' cache key hashes this library's declarations)
uniform sampler2D uMask; uniform sampler2D uRoadId;
const int MASKN = 2048; const float MTEX = 39.0625;
vec4 maskAt(vec2 p){
  p = wrapW(p);
  vec2 f = (p + WH)/MTEX - 0.5; vec2 fl = floor(f); ivec2 i = ivec2(fl); vec2 t = f - fl; ivec2 mx = ivec2(MASKN-1);
  vec4 a = texelFetch(uMask, clamp(i, ivec2(0), mx), 0), b = texelFetch(uMask, clamp(i+ivec2(1,0), ivec2(0), mx), 0);
  vec4 c = texelFetch(uMask, clamp(i+ivec2(0,1), ivec2(0), mx), 0), d = texelFetch(uMask, clamp(i+ivec2(1,1), ivec2(0), mx), 0);
  return (a*(1.0-t.x)+b*t.x)*(1.0-t.y) + (c*(1.0-t.x)+d*t.x)*t.y;
}
vec4 maskTexel(vec2 p){ return texelFetch(uMask, clamp(ivec2(floor((wrapW(p) + WH)/MTEX)), ivec2(0), ivec2(MASKN-1)), 0); }
float roadGrade(vec2 p, float g);   // the ground with the roads built into it (roads.glsl)
float groundH(vec2 p, int oct){ p = wrapW(p); vec4 b = baseAt(p); return roadGrade(p, b.y < 0.01 ? b.x : b.x + b.y*terrainFbm(p/2200.0, oct)); }
// Terrain height: the bare heightfield (trees, rocks and buildings are separate entities) with the roads built into it,
// plus impact craters
float terrainH(vec2 p, int oct){
  COST(0);
  p = wrapW(p);
  vec4 b = baseAt(p);
  float g = roadGrade(p, b.y < 0.01 ? b.x : b.x + b.y*terrainFbm(p/2200.0, oct));
  if (uCraterN > 0 && g > 0.3) g += craterH(p);
  return g;
}

// Scene / atmosphere uniforms
uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uNight; uniform float uTime;
uniform float uCloudCover; uniform float uCloudBase; uniform float uFogB; uniform float uWet; uniform float uSnow;
uniform vec2 uWindOff; uniform float uLightning; uniform float uStorm;
uniform vec3 uWindV;   // surface wind velocity (m/s, the way the air moves)

vec3 skyColor(vec3 rd){
  vec3 sd = uSunDir;
  float sunH = sd.y;
  float y = max(rd.y, 0.0);
  // optical depths (cheap analytic approximation of Rayleigh + Mie single scattering)
  float odV = 1.0/(y*1.4 + 0.075);
  float odS = 1.0/max(sunH*1.4 + 0.08, 0.02);   // positive and bounded once the sun is down (was negative just below the horizon)
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
