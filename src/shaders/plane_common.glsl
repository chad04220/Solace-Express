//! kPlaneCommon
//! Aircraft geometry helpers shared by the distance fields and the materials: primitives, the fuselage section spline,
//! livery paint, skin seams and rivets, the cabin fit.
// AF_LIGHT: a program for the light aircraft and airliners alone. The research jets' fields, materials, displays
// and lights become dead code the compiler drops, so every pixel of the big shared passes pays for less code.
#ifdef AF_LIGHT
#define RESEARCH_ON false
#else
#define RESEARCH_ON true
#endif
// The hit relative to the camera, exactly (gRelSet): the mesh pass's from its body-space vertex position, the march's
// from its ray. The world point holds it only to the float spacing of world metres - 4 mm at the map's edges, a third
// of a degree at arm's length - and what was taken from it (a cockpit's markings, gauge and bezel edges, panel
// outlines, the cabin sun map's texels) came out stair-stepped there, the steps crawling as the aircraft moved.
bool gRelSet = false; vec3 gRel = vec3(0.0);
float sdBox(vec3 p, vec3 b){ vec3 q = abs(p)-b; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0); }
float sdRoundBox(vec3 p, vec3 b, float r){ vec3 q = abs(p)-b+r; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r; }
float sdCapsule(vec3 p, vec3 a, vec3 b, float r){ vec3 pa=p-a, ba=b-a; float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0); return length(pa-ba*h)-r; }
float sdTorus(vec3 p, vec2 t){ return length(vec2(length(p.xz) - t.x, p.y)) - t.y; }
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
// gM[] layout is written by packModel() in models.cpp.
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

// fuselage cross-section (half width, half height, centre y) at body z: a monotone cubic through the stations
// (Fritsch-Butland slopes, Hermite segments), so the profile flows smoothly from nose to tail without bulging past
// the stations or flattening at each one. Mirrored on the CPU by stationAt() in models.cpp.
float fbSlope(float d0, float d1, float h0, float h1){
  return d0*d1 <= 0.0 ? 0.0 : 3.0*(h0 + h1)/((2.0*h1 + h0)/d0 + (h1 + 2.0*h0)/d1);
}
vec3 fbSlope3(vec4 a, vec4 b, vec4 c){
  float h0 = max(b.x - a.x, 1e-3), h1 = max(c.x - b.x, 1e-3);
  vec3 d0 = (b.yzw - a.yzw)/h0, d1 = (c.yzw - b.yzw)/h1;
  return vec3(fbSlope(d0.x, d1.x, h0, h1), fbSlope(d0.y, d1.y, h0, h1), fbSlope(d0.z, d1.z, h0, h1));
}
vec3 fusSection(float z){
  z = clamp(z, gM[1].x, gM[8].x);
  int i = 1;
  for (int k = 1; k < 8; k++) { i = k; if (z <= gM[k+1].x) break; }
  vec4 a = gM[i], b = gM[i+1];
  float h = max(b.x - a.x, 1e-3), t = clamp((z - a.x)/h, 0.0, 1.0);
  vec3 dd = (b.yzw - a.yzw)/h;
  vec3 ma = i > 1 ? fbSlope3(gM[i-1], a, b) : dd*0.5, mb = i < 7 ? fbSlope3(a, b, gM[i+2]) : dd*0.5;
  float t2 = t*t, t3 = t2*t;
  return a.yzw*(2.0*t3 - 3.0*t2 + 1.0) + ma*h*(t3 - 2.0*t2 + t) + b.yzw*(-2.0*t3 + 3.0*t2) + mb*h*(t3 - t2);
}
// Fuselage livery: a cheat line of constant width that follows the fuselage centreline (so it sweeps up with the
// tail cone), fading in on the cowling and tapering only where the tail cone gets too slim to carry it, with a
// pinstripe above it and a grey belly. Returns the paint colour; shared by the renderer and the geometry previewer.
// Skin seams: 1 on a line of a family every `sp` (half width w), anti-aliased over the pixel's footprint px, fading
// out where lines would crowd into the pixel; rivets: 1 on a row of dots every `ds` along a seam, offset `o` from it
float seamLine(float x, float sp, float w, float px){ float d = abs(x - sp*floor(x/sp + 0.5)); return (1.0 - smoothstep(w, w + px, d))*smoothstep(sp*0.25, sp*0.06, px); }
float rivetRow(float x, float y, float sp, float o, float ds, float px){
  float dy = abs(abs(y - sp*floor(y/sp + 0.5)) - o), dx = abs(x - ds*floor(x/ds + 0.5));
  return (1.0 - smoothstep(0.0028, 0.0028 + px, length(vec2(dx, dy))))*smoothstep(0.008, 0.002, px);
}
vec3 fuselagePaint(vec3 lp, vec3 sec){
  float hc = 0.0; for (int i = 2; i <= 7; i++) hc = max(hc, gM[i].z);   // cabin half height
  float w = min(hc*0.07, sec.y*0.32);                                     // half width of the cheat line
  float c = sec.z - min(hc*0.12, sec.y*0.4);                              // its centre
  float fade = smoothstep(gM[2].x - 0.1, gM[3].x, lp.z);                  // grows in over the cowling
  float d = abs(lp.y - c);
  vec3 col = gColBase;
  if (lp.y - sec.z < -0.72*sec.y) col = mix(gColBase, vec3(0.62, 0.64, 0.66), 0.5);   // belly
  if (d < w*fade) col = gColStripe;
  float pc = c + w + hc*0.035, pw = hc*0.012;                             // pinstripe
  if (abs(lp.y - pc) < pw*fade && pc + pw < sec.z + sec.y*0.8) col = mix(gColStripe, vec3(1.0), 0.35);
  return col;
}
// An ellipse inset from the skin is a conservative fit for every rounded/boxy cabin cross-section.
float cabinRoof(vec3 sec, float x){ float k = abs(x)/max(sec.x - 0.035, 0.01); return sec.z + (sec.y - 0.035)*sqrt(max(1.0 - k*k, 0.0)); }
float cabinWidth(vec3 sec, float y){ float k = (y - sec.z)/max(sec.y - 0.035, 0.01); return (sec.x - 0.035)*sqrt(max(1.0 - k*k, 0.0)); }


void loadCabinFit(){
  vec3 E = gM[22].xyz;
  float sw = clamp(cabinWidth(fusSection(E.z + 0.05), E.y - 0.8) - abs(E.x) - 0.015, 0.145, 0.21);
  // headrest: mounted on top of the reclined seat back (centre ~4 cm above eye level, 45 cm aft); dropped altogether
  // (-100) where the cabin roof wouldn't clear it by 8 cm
  float hy = E.y + 0.057;   // along the reclined back's axis, just above its top (top at E.y - 0.046, E.z + 0.434)
  if (cabinRoof(fusSection(E.z + 0.455), abs(E.x) + 0.11) < hy + 0.075 + 0.08) hy = -100.0;
  float ly = cabinRoof(fusSection(E.z - 0.05), 0.09) - 0.035;
  float wx = max(cabinWidth(fusSection(E.z - 0.15), E.y - 0.5) - 0.05, 0.2);
  gCab0 = vec4(sw, hy, ly, wx);
  vec3 vs = fusSection(E.z - 0.30); float hw = max(vs.x - 0.035, 0.01), hh = max(vs.y - 0.035, 0.01);   // visors: at the windshield top, ahead of the eye
  float k = clamp(abs(E.x)/hw, 0.0, 0.95);
  float slope = atan(hh*k/(hw*sqrt(max(1.0 - k*k, 0.01))));
  float vy = cabinRoof(vs, abs(E.x)) - 0.05;
  float oy = cabinRoof(fusSection(E.z - 0.2), 0.22) - 0.03;
  float vx = min(gM[22].w - 0.07, cabinWidth(fusSection(gM[21].w), E.y - 0.19) - 0.04);
  gCab1 = vec4(vy, slope, oy, max(vx, 0.1));
}
void loadMain(){ gOwn = true; gModelId = uModelId; gWheel = uWheel; for (int i = 0; i < 24; i++) gM[i] = uM[i]; for (int i = 0; i < 7; i++) gWr[i] = uWr[i]; gPS = uPS; gCtl = uCtl; gColBase = uColBase; gColStripe = uColStripe; gFlame = uFlame; if (gPS.w > 0.5 && gM[0].z < 4.5) loadCabinFit(); }
int gTrafK = 0;
void loadTraffic(int k){
  gOwn = false; gTrafK = k; gModelId = -1;
  gWheel = vec3(texelFetch(uTraffic, ivec2(25, k), 0).w, texelFetch(uTraffic, ivec2(26, k), 0).w, texelFetch(uTraffic, ivec2(27, k), 0).w);
  for (int i = 0; i < 24; i++) gM[i] = texelFetch(uTraffic, ivec2(i, k), 0);
  gPS = texelFetch(uTraffic, ivec2(28, k), 0); gCtl = texelFetch(uTraffic, ivec2(29, k), 0);
  for (int i = 0; i < 7; i++) gWr[i] = uWr[i];
  vec4 c0 = texelFetch(uTraffic, ivec2(30, k), 0), c1 = texelFetch(uTraffic, ivec2(31, k), 0);
  gColBase = c0.rgb; gColStripe = c1.rgb;
  gFlame = vec4(gCtl.w, c1.w, gPS.y*1.5708 - gCtl.x*0.5, 0.0);
}
