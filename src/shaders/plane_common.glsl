//! kPlaneCommon
//! Aircraft geometry helpers shared by the distance fields and the materials: primitives, the fuselage section spline,
//! livery paint, skin seams and rivets, the cabin fit.
// AF_LIGHT: a program for the light aircraft and airliners alone. The research jets' fields, materials, displays
// and lights become dead code the compiler drops, so every pixel of the big shared passes pays for less code.
// AF_JET / AF_WRAITH: the XR-30's or the XR-40's own, for the aircraft mesh pass (each aircraft is a draw of its own,
// with its own build): the light aircraft's fields, cabins and liveries and the other research jet's are dropped too. A
// GPU reserves registers for the worst path in a program whether a pixel takes it or not: the XR-40's cockpit, drawn
// with every aircraft's code, slowed by 3 ms as the light aircraft's cockpits grew (v3.34 to v3.38, RTX 3070 Laptop).
// AF_MODEL n: one aircraft's own build (n its index in kAircraft, aircraft.h), with its family's switch above and its
// packed model as constants (scene_uniforms.glsl gM): the aircraft mesh pass and the bakes draw each aircraft with its
// own (shaders.h aircraftDefines), so a program holds that aircraft's code and nothing of any other's.
// FLEET_ON: the light aircraft and airliners; JET_ON, WRAITH_ON: the XR-30, the XR-40; RESEARCH_ON: either of them.
// The same switches for the preprocessor (0 or 1), where code the build leaves out goes from the program's source
// itself (shader_prune.h settles the conditionals before it prunes): HAS_FLEET, HAS_JET, HAS_WRAITH, HAS_RESEARCH.
#if defined(AF_WRAITH)
#define RESEARCH_ON true
#define FLEET_ON false
#define JET_ON false
#define WRAITH_ON true
#define HAS_FLEET 0
#define HAS_JET 0
#define HAS_WRAITH 1
#elif defined(AF_JET)
#define RESEARCH_ON true
#define FLEET_ON false
#define JET_ON true
#define WRAITH_ON false
#define HAS_FLEET 0
#define HAS_JET 1
#define HAS_WRAITH 0
#elif defined(AF_LIGHT)
#define RESEARCH_ON false
#define FLEET_ON true
#define JET_ON false
#define WRAITH_ON false
#define HAS_FLEET 1
#define HAS_JET 0
#define HAS_WRAITH 0
#else
#define RESEARCH_ON true
#define FLEET_ON true
#define JET_ON true
#define WRAITH_ON true
#define HAS_FLEET 1
#define HAS_JET 1
#define HAS_WRAITH 1
#endif
#define HAS_RESEARCH (HAS_JET || HAS_WRAITH)
// AF_OUTSIDE: a build that never sees the aircraft from inside - the outside bodies' builder - has no cabin code: an
// edit to a cockpit changes no outside body's builder, so no outside body is built again for it (HAS_CABIN)
#ifdef AF_OUTSIDE
#define HAS_CABIN 0
#else
#define HAS_CABIN 1
#endif
// HAS_<type> for the preprocessor (HAS_SWIFT, HAS_MANTIS...): the build carries that type's own code - every build
// of its family but another aircraft's own. HAS_FLEET_CABIN: the career types' and the XR-10's authored cabins
// (cockpit_layout.glsl fleetCabin). MODEL_IS(n) in code: the player's aircraft is type n (gModelId; traffic is
// -1), false as it compiles in another aircraft's own build.
#ifdef AF_MODEL
#define HAS_KESTREL (AF_MODEL == 0)
#define HAS_WREN (AF_MODEL == 1)
#define HAS_BUSHMASTER (AF_MODEL == 2)
#define HAS_ISLANDER (AF_MODEL == 3)
#define HAS_PELICAN (AF_MODEL == 4)
#define HAS_MERIDIAN (AF_MODEL == 5)
#define HAS_STARLING (AF_MODEL == 6)
#define HAS_SWIFT (AF_MODEL == 7)
#define HAS_OSPREY (AF_MODEL == 8)
#define HAS_NIGHTJAR (AF_MODEL == 9)
#define HAS_MANTIS (AF_MODEL == 11)
#define HAS_FLEET_CABIN (AF_MODEL < 10)
#define MODEL_IS(n) ((n) == AF_MODEL && gModelId == (n))
#else
#define HAS_KESTREL HAS_FLEET
#define HAS_WREN HAS_FLEET
#define HAS_BUSHMASTER HAS_FLEET
#define HAS_ISLANDER HAS_FLEET
#define HAS_PELICAN HAS_FLEET
#define HAS_MERIDIAN HAS_FLEET
#define HAS_STARLING HAS_FLEET
#define HAS_SWIFT HAS_FLEET
#define HAS_OSPREY HAS_FLEET
#define HAS_NIGHTJAR HAS_FLEET
#define HAS_MANTIS HAS_FLEET
#define HAS_FLEET_CABIN HAS_FLEET
#define MODEL_IS(n) (gModelId == (n))
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
float sdEllipsoid(vec3 p, vec3 r){ float k0 = length(p/r); float k1 = length(p/(r*r)); return k1 > 1e-5 ? k0*(k0 - 1.0)/k1 : -min(r.x, min(r.y, r.z)); }
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
  // the segment [gM[i], gM[i+1]] that holds z (the first whose end is at or past it) and the stations either side of
  // it, picked with constant indices: an array indexed by a variable lives in the GPU's per-thread memory, and gM is
  // copied on every pixel of every airframe pass - a loop index here put half a kilobyte a pixel through memory
  int i = 7; bool f = false;
  vec4 am = gM[6], a = gM[7], b = gM[8], bp = gM[8];
  if (!f && z <= gM[2].x) { f = true; i = 1; am = gM[1]; a = gM[1]; b = gM[2]; bp = gM[3]; }
  if (!f && z <= gM[3].x) { f = true; i = 2; am = gM[1]; a = gM[2]; b = gM[3]; bp = gM[4]; }
  if (!f && z <= gM[4].x) { f = true; i = 3; am = gM[2]; a = gM[3]; b = gM[4]; bp = gM[5]; }
  if (!f && z <= gM[5].x) { f = true; i = 4; am = gM[3]; a = gM[4]; b = gM[5]; bp = gM[6]; }
  if (!f && z <= gM[6].x) { f = true; i = 5; am = gM[4]; a = gM[5]; b = gM[6]; bp = gM[7]; }
  if (!f && z <= gM[7].x) { f = true; i = 6; am = gM[5]; a = gM[6]; b = gM[7]; bp = gM[8]; }
  float h = max(b.x - a.x, 1e-3), t = clamp((z - a.x)/h, 0.0, 1.0);
  vec3 dd = (b.yzw - a.yzw)/h;
  vec3 ma = i > 1 ? fbSlope3(am, a, b) : dd*0.5, mb = i < 7 ? fbSlope3(a, b, bp) : dd*0.5;
  float t2 = t*t, t3 = t2*t;
  return a.yzw*(2.0*t3 - 3.0*t2 + 1.0) + ma*h*(t3 - 2.0*t2 + t) + b.yzw*(-2.0*t3 + 3.0*t2) + mb*h*(t3 - t2);
}
// The Mantis remains in the shared engine-4 path. Its centerline glass cockpit is a stable packed-model
// discriminator, including traffic/bake passes that do not carry uModelId. No roster index or uniform layout changes.
bool isMantis(){ return int(gM[0].z + 0.5) == 4 && abs(gM[22].x) < 0.001 && gM[13].y < -3.0; }
const float MT_CANT = 0.48;
const float MT_FIN_X = 1.05;   // fin roots stay on the shoulders when the engine moves to the centerline
const vec3 MT_INLET = vec3(0.0, 1.05, -0.65);
vec3 mantisNozzle(){ return vec3(0.0,gM[16].y,gM[16].w+gM[17].x); }
// The aft navigation lamp sits on the upper nozzle rim, clear of the open exhaust.
vec3 mantisTailLight(){ return mantisNozzle()+vec3(0,gM[16].z*.88+.02,-.04); }
vec3 mantisFinTop(){ return vec3(MT_FIN_X + sin(MT_CANT)*gM[14].x, gM[15].x + cos(MT_CANT)*gM[14].x + 0.04, gM[15].y + gM[14].w + gM[14].z*0.4); }
// One uninterrupted front pane and two side panes. The canopy rail is below the pilot's sightline.
float mantisWindow(vec3 p){
  vec4 W=gM[23];
  float front=max(max(W.x-p.z,p.z-W.y),W.z-p.y);
  float side=max(max(W.y+0.045-p.z,p.z-W.w),W.z-0.08-p.y);
  return min(front,side);
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
  float hc = max(max(max(gM[2].z, gM[3].z), max(gM[4].z, gM[5].z)), max(max(gM[6].z, gM[7].z), 0.0));   // cabin half height (the tallest station; constant indices, as in fusSection)
  float w = min(hc*0.07, sec.y*0.32);                                     // half width of the cheat line
  float c = sec.z - min(hc*0.12, sec.y*0.4);                              // its centre
  float fade = smoothstep(gM[2].x - 0.1, gM[3].x, lp.z);                  // grows in over the cowling
  float d = abs(lp.y - c);
  vec3 col = gColBase;
  if (isMantis()) {
    // Graphite body, angular amber shoulder stripe and anti-glare nose. The form, rather than a recolor, leads.
    col=vec3(.105,.125,.15);
    if(lp.y < sec.z-.45*sec.y) col=vec3(.055,.065,.082);
    float stripe=lp.y-sec.z-(.07+.035*clamp(lp.z+4.0,0.0,8.0));
    if(abs(stripe)<.045 && lp.z>-6.2 && lp.z<5.7) col=vec3(.92,.43,.08);
    if(lp.z<-6.6 || (lp.y>sec.z+.50*sec.y && lp.z<-4.8)) col=vec3(.038,.046,.057);
    return col;
  }
  if (lp.y - sec.z < -0.72*sec.y) col = mix(gColBase, vec3(0.62, 0.64, 0.66), 0.5);   // belly
  if (d < w*fade) col = gColStripe;
  float pc = c + w + hc*0.035, pw = hc*0.012;                             // pinstripe
  if (abs(lp.y - pc) < pw*fade && pc + pw < sec.z + sec.y*0.8) col = mix(gColStripe, vec3(1.0), 0.35);
  return col;
}
// An ellipse inset from the skin is a conservative fit for every rounded/boxy cabin cross-section.
float cabinRoof(vec3 sec, float x){ float k = abs(x)/max(sec.x - 0.035, 0.01); return sec.z + (sec.y - 0.035)*sqrt(max(1.0 - k*k, 0.0)); }
float cabinWidth(vec3 sec, float y){ float k = (y - sec.z)/max(sec.y - 0.035, 0.01); return (sec.x - 0.035)*sqrt(max(1.0 - k*k, 0.0)); }


// Swift eye/body moves independently of retained world cabin furniture.
float swiftPreservedFurnitureY(){ return MODEL_IS(7)?.520:gM[22].y; }

void loadCabinFit(){
  vec3 E = gM[22].xyz;
  float seatDrop=uCabinSeatFit.y;
  float sw = clamp(cabinWidth(fusSection(E.z + 0.05), E.y - seatDrop) - abs(E.x) - 0.015, 0.145, 0.21);
  if(MODEL_IS(0) || MODEL_IS(1))sw=min(sw,.165); // same cap as CPU rail fitting
  if(MODEL_IS(2))sw=min(sw,.180); // matched Bushmaster pan/rail fit
  if(MODEL_IS(7))sw=min(sw,.180); // matched supported Swift pan/rail fit
  // headrest: mounted on top of the reclined seat back (centre ~4 cm above eye level, 45 cm aft); dropped altogether
  // (-100) where the cabin roof wouldn't clear it by 8 cm
  float hy = E.y + 0.057;   // along the reclined back's axis, just above its top (top at E.y - 0.046, E.z + 0.434)
  if (cabinRoof(fusSection(E.z + 0.455), abs(E.x) + 0.11) < hy + 0.075 + 0.08) hy = -100.0;
  float ly = cabinRoof(fusSection(E.z - 0.05), 0.09) - 0.035;
  float wx = max(cabinWidth(fusSection(E.z - 0.15), swiftPreservedFurnitureY() - 0.5) - 0.05, 0.2);
  gCab0 = vec4(sw, hy, ly, wx);
  vec3 vs = fusSection(E.z - 0.30); float hw = max(vs.x - 0.035, 0.01), hh = max(vs.y - 0.035, 0.01);   // visors: at the windshield top, ahead of the eye
  float k = clamp(abs(E.x)/hw, 0.0, 0.95);
  float slope = atan(hh*k/(hw*sqrt(max(1.0 - k*k, 0.01))));
  float vy = cabinRoof(vs, abs(E.x)) - 0.05;
  float oy = cabinRoof(fusSection(E.z - 0.2), 0.22) - 0.03;
  float vx = min(gM[22].w - 0.07, cabinWidth(fusSection(gM[21].w), swiftPreservedFurnitureY() - 0.19) - 0.04);
  gCab1 = vec4(vy, slope, oy, max(vx, 0.1));
  gCab2=uCabinFootFit;gCabSeat=uCabinSeatFit;

}
// (the copies written out, element by element, for the same reason as fusSection's: no index the compiler has to
// work out at run time, so the arrays stay in registers or are read straight from the uniforms)
void loadMain(){ gOwn = true; gModelId = uModelId; gWheel = uWheel;
#ifndef AF_MODEL
  gM[0] = uM[0]; gM[1] = uM[1]; gM[2] = uM[2]; gM[3] = uM[3]; gM[4] = uM[4]; gM[5] = uM[5]; gM[6] = uM[6]; gM[7] = uM[7]; gM[8] = uM[8]; gM[9] = uM[9]; gM[10] = uM[10]; gM[11] = uM[11]; gM[12] = uM[12]; gM[13] = uM[13]; gM[14] = uM[14]; gM[15] = uM[15]; gM[16] = uM[16]; gM[17] = uM[17]; gM[18] = uM[18]; gM[19] = uM[19]; gM[20] = uM[20]; gM[21] = uM[21]; gM[22] = uM[22]; gM[23] = uM[23];
#endif
  gWr[0] = uWr[0]; gWr[1] = uWr[1]; gWr[2] = uWr[2]; gWr[3] = uWr[3]; gWr[4] = uWr[4]; gWr[5] = uWr[5]; gWr[6] = uWr[6];
  gPS = uPS; gFlapDL = uPr.w; gCtl = uCtl; gColBase = uColBase; gColStripe = uColStripe; gFlame = uFlame;
#if HAS_CABIN
  if (gPS.w > 0.5 && gM[0].z < 4.5) loadCabinFit();
#endif
}
int gTrafK = 0;
void loadTraffic(int k){
  gOwn = false; gTrafK = k; gModelId = -1;
  gWheel = vec3(texelFetch(uTraffic, ivec2(25, k), 0).w, texelFetch(uTraffic, ivec2(26, k), 0).w, texelFetch(uTraffic, ivec2(27, k), 0).w);
#ifndef AF_MODEL
  gM[0] = texelFetch(uTraffic, ivec2(0, k), 0); gM[1] = texelFetch(uTraffic, ivec2(1, k), 0); gM[2] = texelFetch(uTraffic, ivec2(2, k), 0); gM[3] = texelFetch(uTraffic, ivec2(3, k), 0); gM[4] = texelFetch(uTraffic, ivec2(4, k), 0); gM[5] = texelFetch(uTraffic, ivec2(5, k), 0); gM[6] = texelFetch(uTraffic, ivec2(6, k), 0); gM[7] = texelFetch(uTraffic, ivec2(7, k), 0); gM[8] = texelFetch(uTraffic, ivec2(8, k), 0); gM[9] = texelFetch(uTraffic, ivec2(9, k), 0); gM[10] = texelFetch(uTraffic, ivec2(10, k), 0); gM[11] = texelFetch(uTraffic, ivec2(11, k), 0); gM[12] = texelFetch(uTraffic, ivec2(12, k), 0); gM[13] = texelFetch(uTraffic, ivec2(13, k), 0); gM[14] = texelFetch(uTraffic, ivec2(14, k), 0); gM[15] = texelFetch(uTraffic, ivec2(15, k), 0); gM[16] = texelFetch(uTraffic, ivec2(16, k), 0); gM[17] = texelFetch(uTraffic, ivec2(17, k), 0); gM[18] = texelFetch(uTraffic, ivec2(18, k), 0); gM[19] = texelFetch(uTraffic, ivec2(19, k), 0); gM[20] = texelFetch(uTraffic, ivec2(20, k), 0); gM[21] = texelFetch(uTraffic, ivec2(21, k), 0); gM[22] = texelFetch(uTraffic, ivec2(22, k), 0); gM[23] = texelFetch(uTraffic, ivec2(23, k), 0);
#endif
  gPS = texelFetch(uTraffic, ivec2(28, k), 0); gFlapDL = 0.0; gCtl = texelFetch(uTraffic, ivec2(29, k), 0);
  gWr[0] = uWr[0]; gWr[1] = uWr[1]; gWr[2] = uWr[2]; gWr[3] = uWr[3]; gWr[4] = uWr[4]; gWr[5] = uWr[5]; gWr[6] = uWr[6];
  vec4 c0 = texelFetch(uTraffic, ivec2(30, k), 0), c1 = texelFetch(uTraffic, ivec2(31, k), 0);
  gColBase = c0.rgb; gColStripe = c1.rgb;
  gFlame = vec4(gCtl.w, c1.w, gPS.y*1.5708 - gCtl.x*0.5, 0.0);
}
