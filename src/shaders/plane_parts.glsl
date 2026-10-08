//! kPlaneParts
//! The rigid moving parts: the light aircraft's flaps, ailerons, elevators and rudder, and in the cockpits the yokes, the rudder pedals, the throttle knob or levers and the flap lever (the
//! light aircraft), the side sticks and throttles (the XR-30 and the XR-40) and the XR-40's pedals. Each is a solid
//! piece that only slides or turns with a control, so it has its own shape in its own frame (partField) and a pose
//! from the controls (partPose: body = R*local + T). The aircraft's field places each part by its pose
//! (plane_sdf.glsl, wraith_cockpit_sdf.glsl); the mesh bake (aircraft_mesh.cpp) leaves the parts out of the airframe
//! (gPartMode -2) and bakes each part alone in its own frame (gPartMode = the part), and the raster passes draw each
//! part's mesh at the pose a small pass computes once a frame from these same functions (kPartPoseFS) - the march
//! and the objects pass see exactly the shapes the meshes show.
const int PT_YOKE_SHAFT = 0, PT_YOKE_WHEEL = 1, PT_PEDAL = 2, PT_THR_KNOB = 3, PT_THR_LEVER = 4, PT_FLAP_LEVER = 5,
          PT_JET_STICK = 6, PT_JET_THR = 7, PT_WR_STICK = 8, PT_WR_THR = 9, PT_WR_PEDAL = 10,
          PT_FLAP = 11, PT_AILERON = 12, PT_ELEVATOR = 13, PT_RUDDER = 14,   // (the light aircraft's control surfaces)
          // the XR-40's (wraith_sdf.glsl wrPartField): its four pods' nacelles, fans, vanes and iris petals, the bay
          // doors and the bomb, the laser turrets' hatches, emitters, barrel tips and arms, the elevons and ruddervators
          PT_WR_PODF = 15, PT_WR_PODR = 16, PT_WR_FAN = 17, PT_WR_VANEC = 18, PT_WR_VANEO = 19, PT_WR_VANEY = 20,
          PT_WR_PETAL = 21, PT_WR_DOOR = 22, PT_WR_BOMB = 23, PT_WR_HATCH = 24, PT_WR_TURRET = 25, PT_WR_MUZZLE = 26,
          PT_WR_ARM = 27, PT_WR_ELEVON = 28, PT_WR_RUDV = 29,
          PT_JT_ELEVON = 30, PT_JT_CANARD = 31, PT_JT_RUDDER = 32,   // the XR-30's (plane_sdf.glsl jtPartField)
          // the landing gear of the aircraft drawn from the packed model (plane_sdf.glsl gearPartField): a retracting main
          // leg with its wheels, the nose and tail wheels (steered), the bays' doors
          PT_GEAR_MAIN = 33, PT_GEAR_NOSE = 34, PT_GEAR_TAIL = 35, PT_GEAR_MDOOR = 36, PT_GEAR_NDOOR = 37,
          // the rest of the XR-30's moving pieces (plane_sdf.glsl jtPartField): its two vectoring nozzles, its gear's struts
          // (they shorten as they retract), wheels and bay doors
          // (the gear's struts, wheels and doors serve the XR-40 too: it has the same gear in other bays)
          PT_JT_NOZZLE = 38, PT_JT_LEGM = 39, PT_JT_WHEELM = 40, PT_JT_LEGN = 41, PT_JT_WHEELN = 42, PT_JT_DOORM = 43, PT_JT_DOORN = 44,
          PT_WR_ACT = 45;   // the XR-40's pods' hydraulic tilt actuators (stretched along their axis as the pods tilt)
bool partIsJet(int k){ return (k >= PT_JT_ELEVON && k <= PT_JT_RUDDER) || (k >= PT_JT_NOZZLE && k <= PT_JT_DOORN); }
bool partIsWraith(int k){ return (k >= PT_WR_PODF && k <= PT_WR_RUDV) || k == PT_WR_ACT; }
const vec3 WRP_POD[4] = vec3[4](vec3(-2.35, -0.08, -3.3), vec3(2.35, -0.08, -3.3), vec3(-2.75, 0.05, 3.45), vec3(2.75, 0.05, 3.45));   // (wraith_sdf.glsl WR_POD)
int gPartMode = -1;   // -1: the whole aircraft, its parts posed; -2: without its parts; >= 0: that part alone, in its own frame
struct Pose { mat3 R; vec3 T; };   // (R is a rotation for the cockpit parts; for a control surface an affine map: its deflection
                                   // about a swept, tapered hinge shears it a little, exactly as sdSurface does)
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
// the hinged surface's deflection in its own (span, chord, thickness) frame: sdSurface's rest shape -> deflected,
// u = D*u0 + d (the hinge line and the slide are linear in the span, so this is exact)
void surfDefl(float span, float rc, float tc, float sweep, float hingeF, float defl, float slide, out mat3 D, out vec3 d){
  float h0 = rc*hingeF + 0.008, h1 = (sweep + (tc - rc)*hingeF)/span;
  float sl0 = slide*rc, sl1 = slide*(tc - rc)/span;
  float cd = cos(defl), sd = sin(defl);
  D = mat3(1.0, h1*(1.0 - cd) + sl1, sd*h1,   // (columns: the span, chord and thickness coordinates' coefficients)
           0.0, cd, -sd,
           0.0, sd, cd);
  d = vec3(0.0, h0*(1.0 - cd) + sl0, sd*h0);
}
// a wing or tailplane's (span, chord, thickness) frame on one side, from the body: u = A*p + a (the dihedral as a shear)
void surfFrame(float sgn, float yOff, float zOff, float dih, out mat3 A, out vec3 a){
  A = mat3(sgn, 0.0, -dih*sgn,   0.0, 0.0, 1.0,   0.0, 1.0, 0.0);
  a = vec3(0.0, -zOff, -yOff);
}
void surfFrameInv(float sgn, float yOff, float zOff, float dih, out mat3 B, out vec3 b){
  B = mat3(sgn, dih, 0.0,   0.0, 0.0, 1.0,   0.0, 1.0, 0.0);
  b = vec3(0.0, yOff, zOff);
}
// the posed surface: its rest mesh is the right side's, in body space; body = B_side * (D * (A_right * l + a) + d) + b
Pose surfPose(float sgn, float yOff, float zOff, float dih, float span, float rc, float tc, float sweep, float hingeF, float defl, float slide){
  mat3 A, B, D; vec3 a, b, d;
  surfFrame(1.0, yOff, zOff, dih, A, a);
  surfFrameInv(sgn, yOff, zOff, dih, B, b);
  surfDefl(span, rc, tc, sweep, hingeF, defl, slide, D, d);
  Pose X; X.R = B*D*A; X.T = B*(D*a + d) + b;
  return X;
}
mat3 partRyz(float b){ float c = cos(b), s = sin(b); return mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c); }   // (as rot2 on .yz)
mat3 partRxy(float a){ float c = cos(a), s = sin(a); return mat3(c, s, 0.0, -s, c, 0.0, 0.0, 0.0, 1.0); }  // (as rot2 on .xy)
mat3 partRxz(float b){ float c = cos(b), s = sin(b); return mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c); }  // (as rot2 on .xz)
mat3 partMirror(float sx){ return mat3(sx, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0); }
// a hinged surface in a general affine frame (u = A*p + a: span, chord, thickness), the rest mesh the right side's:
// As is the frame on this side (the same offsets), D, d its deflection (surfDefl)
Pose surfPoseAffine(mat3 Ar, mat3 As, vec3 a, mat3 D, vec3 d){
  mat3 Bi = inverse(As); Pose X; X.R = Bi*D*Ar; X.T = Bi*(D*a + d - a); return X;
}
// a pod's tilt actuator: its barrel's anchor on the pylon (front pods) or the wing root (rear), the rod's lug on the pod
void wrActEnds(int i, float tilt, out vec3 A, out vec3 L){
  vec3 P = WRP_POD[i]; float side = P.x > 0.0 ? 1.0 : -1.0;
  A = P + (i < 2 ? vec3(-side*0.85, 0.05, -0.32) : vec3(-side*0.8, 0.0, -1.0));
  vec3 l = vec3(-side*0.42, 0.36, -0.75); l.yz = rot2(l.yz, tilt); L = P + l;
}
// The XR-40's parts (sd.x: the pod, 0-3, or the side, -1 / 1; sd.y: which vane or petal). A pod's pieces ride its
// tilt about the trunnion axis (x) through its pivot; the left pods' nacelles are the right ones' mirror image
Pose wrPartPose(int k, vec2 sd){
  Pose X; X.R = mat3(1.0); X.T = vec3(0.0);
  if (k == PT_WR_ACT) {   // its unit-length mesh along z, laid from the anchor to the lug (any roll about the axis: it is round)
    vec3 A, L; wrActEnds(int(sd.x + 0.5), gWr[0][int(sd.x + 0.5)], A, L);
    vec3 d = L - A; float al = max(length(d), 1e-4); vec3 a = d/al;
    vec3 v = normalize(cross(a, vec3(1.0, 0.0, 0.0))), u = cross(v, a);
    X.R = mat3(u, v, a*al); X.T = A;
    return X;
  }
  if (k <= PT_WR_PETAL) {
    int i = int(sd.x + 0.5); vec3 P = WRP_POD[i]; float side = P.x > 0.0 ? 1.0 : -1.0;
    mat3 Rt = partRyz(gWr[0][i]);
    X.T = P;
    if (k == PT_WR_PODF || k == PT_WR_PODR) X.R = Rt*partMirror(side);
    else if (k == PT_WR_FAN) X.R = Rt*partRxy(-gWr[4].x*side);
    else if (k == PT_WR_VANEC || k == PT_WR_VANEO) { X.R = Rt*partRyz(gWr[3][i]*1.4); X.T = P + Rt*vec3(0.0, sd.y*0.13, 1.42); }
    else if (k == PT_WR_VANEY) { X.R = Rt*partRxz(gWr[1][i]*1.4*side); X.T = P + Rt*vec3(sd.y*0.11, 0.0, 1.5); }
    else {   // a petal: turned round the nozzle to its place, and about its root by the thrust (baked at half thrust)
      float rootR = 0.52 - 0.07*smoothstep(-0.2, 1.3, 0.95) - 0.05;
      float exitR = 0.28 + 0.1*clamp(gWr[2][i], 0.0, 1.0);
      float al = atan(exitR - rootR, 0.42) - atan(0.33 - rootR, 0.42);
      vec3 root = vec3(rootR, 0.0, 0.95);
      mat3 Rp = partRxz(-al), Rz = partRxy(sd.y*0.62831853);
      X.R = Rt*Rz*Rp; X.T = P + Rt*Rz*(root - Rp*root);
    }
    return X;
  }
  mat3 S = partMirror(sd.x);
  float las = gWr[4].z;
  vec3 L0 = vec3(0.95, -0.1 - 0.12*0.0025443 - 0.18, -5.1);   // (wraith_sdf.glsl: the turret's frame)
  if (k == PT_WR_DOOR) { X.R = S*partRxy(gWr[4].y*1.75); X.T = S*vec3(0.53, -0.575, 0.1); }
  else if (k == PT_WR_BOMB) { X.R = mat3(max(gWr[6].x, 1e-3)); X.T = vec3(0.0, -0.305, 0.1); }
  else if (k == PT_WR_HATCH) { X.R = S*partRxy(-las*1.9); X.T = S*(L0 + vec3(0.2, -0.17, 0.0)); }
  else if (k == PT_WR_TURRET) { X.R = S; X.T = S*(L0 + vec3(0.0, -0.38*las, -0.25*las)); }
  else if (k == PT_WR_MUZZLE) { X.R = S; X.T = S*(L0 + vec3(0.0, -0.38*las, -0.25*las + 0.15*(1.0 - las))); }
  else if (k == PT_WR_ARM) {   // (it telescopes: scaled along its own axis)
    vec3 u = normalize(vec3(0.0, -0.38, -0.25));
    X.R = S*(mat3(1.0) + (max(las, 1e-3) - 1.0)*outerProduct(u, u)); X.T = S*(L0 + vec3(0.0, 0.05, 0.1));
  } else if (k == PT_WR_ELEVON) {   // the wing's frame (s, c, t) = (|x| - 1, z + 3, y + 0.12 + 0.012 s)
    mat3 Ar = mat3(1.0, 0.0, 0.012, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0); vec3 a = vec3(-1.0, 3.0, 0.108);
    mat3 D; vec3 d; surfDefl(5.2, 8.2, 1.35, 4.7, 0.8, -gWr[5].x*0.45 - gWr[5].z*sd.x*0.45, 0.0, D, d);
    X = surfPoseAffine(Ar, Ar*S, a, D, d);
  } else if (k == PT_WR_RUDV) {   // the canted fin's frame: (span, chord, thickness) from the body, its root line following the tail's taper
    float C = cos(0.72), Sn = sin(0.72);
    mat3 Ar = mat3(Sn, 0.0, C,   C, 0.0, -Sn,   0.05*C, 1.0, -0.05*Sn);
    vec3 a = vec3(-1.05*Sn - 0.67*C + 0.75, -4.4, -1.05*C + 0.67*Sn);
    float dv = -gWr[5].x*0.35 + gWr[5].y*sd.x*0.35;
    mat3 D; vec3 d; surfDefl(3.05, 2.6, 1.1, 1.6*3.05/2.3, 0.68, -dv*1.4, 0.0, D, d);
    X = surfPoseAffine(Ar, Ar*S, a, D, d);
  }
  return X;
}
// turboprop nacelle section at body z (mirrors the engine code: a front cone to 30% of the length, then a rear cone
// that rises towards the wing and shrinks to 35%): returns (centre y, radius)
vec2 nacSection(float z){
  vec4 N0 = gM[16], N1 = gM[17];
  float nr = N0.z, z0 = N0.w, len = N1.x, z1 = z0 + len*0.3;
  if (z < z1) { float s = clamp((z - z0)/max(z1 - z0, 0.01), 0.0, 1.0); return vec2(N0.y + 0.02*s, mix(nr*0.72, nr, s)); }
  float wingY = gM[10].x + N0.x*gM[10].z, s = clamp((z - z1)/max(len*0.7, 0.01), 0.0, 1.0);
  return vec2(N0.y + mix(0.02, wingY - N0.y - nr*0.25, s), mix(nr, nr*0.35, s));
}
Pose poseMul(Pose a, Pose b){ Pose X; X.R = a.R*b.R; X.T = a.R*b.T + a.T; return X; }
// The landing gear (the packed model's: gM[18] track, wheel radius, main and nose stations; gM[19] gear height, tail
// wheel station, taildragger). The legs travel until gear 0.2, then the doors close over them.
float gearUp(){ return clamp((1.0 - gPS.x)*1.25, 0.0, 1.0); }
float gearDoorAngle(){ return smoothstep(0.0, 0.2, gPS.x)*1.45; }
// In the cockpit view a retracting nose wheel shrinks away as it folds: stowed along the belly it would lie in the
// footwell of a cabin drawn hollow
float gearNoseShow(){
  bool inCabin = gPS.w > 0.5 && int(gM[0].y + 0.5) >= 3;
  return inCabin ? 1.0 - smoothstep(0.3, 0.8, gearUp()) : 1.0;
}
// a rotation by a about the unit axis k
mat3 rotAxis(vec3 k, float a){ float c = cos(a), s = sin(a); return c*mat3(1.0) + s*mat3(0.0, k.z, -k.y,  -k.z, 0.0, k.x,  k.y, -k.x, 0.0) + (1.0 - c)*mat3(k*k.x, k*k.y, k*k.z); }   // (k k^T written out)
// A leg swinging fore or aft (dir -1 forward, +1 aft) about a crosswise pivot at its top, through straight down, until
// it lies along the chord, while its wheel turns a quarter about the leg to lie flat (a wing is thinner than the
// wheel is tall): the turn at retraction u, from the leg's rest direction v (the pivot to the wheel's centre). The
// wheel's outboard face (and what the leg carries on that side) ends underneath. The turn is done in the first 60% of
// the swing, while the wheel still hangs below the wing (turning as it went in, the last of it lifted the rim through
// the upper skin)
mat3 gearSwingR(vec3 v, float dir, float u){
  float chi = mod(dir*1.5707963 - atan(v.z, v.y) + 3.14159265, 6.2831853) - 3.14159265;
  vec3 k = normalize(v);
  float tw = (partRyz(chi)*rotAxis(k, 1.5707963)*vec3(1.0, 0.0, 0.0)).y < 0.0 ? 1.5707963 : -1.5707963;
  return partRyz(chi*u)*rotAxis(k, tw*smoothstep(0.0, 0.6, u));
}
// A wing-retracting main (type 4) folds inboard about a fore-and-aft hinge, until the leg lies along the wing (its
// dihedral) and the wheel lies flat under the wing root, in a streamlined fairing: raised straight up it came through
// the top of a wing a third as thick as the wheel is tall, and even flat the wheel (20 cm across its tyre) is thicker
// than these wings where the gear stands (7 to 15 cm). The hinge is placed so all of the folded wheel, its hub caps too,
// stays under the upper skin (gearStowGap); the fairing's floor (its doors) runs 4 cm under its lower caps, parallel to
// the wing.
float wingHalf(float s, float z){   // the wing's half thickness at span s and body z (sdPanel: an uneven capsule from the LE radius to the TE radius)
  vec4 W0 = gM[9], W1 = gM[10];
  float kk = clamp(s/W0.x, 0.0, 1.0), ch = mix(W0.y, W0.z, kk), le = W1.y + W0.w*kk;
  float r1 = W1.w*ch*0.5, r2 = max(0.004*ch, 0.005);
  return mix(r1, r2, clamp((z - le - r1)/max(ch - r1 - r2, 0.01), 0.0, 1.0));
}
// How high a stowed main wheel's centre may lie under the wing's upper skin, laid flat with its centre at span s, body
// z (r: its radius; slope: how much the chord plane rises outboard, relative to the wheel - the dihedral's for a wheel
// lying flat in the body's frame, 0 for one lying in the wing's): the most of each part's height under the skin
// across the wheel - its hub caps and brake disc (4.4 cm proud of the tyre: plane_sdf.glsl gearWheelDetails), its rims,
// its tyre (10 cm half width) - each 1.6 cm under it, where the wing thins aft and the dihedral lowers it inboard.
// (Measured at the wheel's centre only, for the tyre alone, the caps came through the top of the wing, stowed.)
float gearStowGap(float s, float z, float r, float slope){
  float a = 0.55*r, b = 0.9*r;
  float y = wingHalf(s, z) - 0.16;
  y = min(y, min(wingHalf(s + a, z) + a*slope, wingHalf(s - a, z) - a*slope) - 0.145);
  y = min(y, min(wingHalf(s, z + a), wingHalf(s, z - a)) - 0.145);
  y = min(y, min(wingHalf(s + b, z) + b*slope, wingHalf(s - b, z) - b*slope) - 0.116);
  y = min(y, min(wingHalf(s, z + b), wingHalf(s, z - b)) - 0.116);
  return y;
}
struct GearFold { vec3 H; float legLen, dl, xf, floor0; };   // (floor0: the fairing floor's height at x 0, rising with the dihedral)
GearFold gearFold(){
  vec4 G0 = gM[18], G1 = gM[19], W1 = gM[10];
  float track = G0.x, wr = G0.y, mz = G0.z, wy = wr - G1.x, dl = atan(W1.z);
  float yh = W1.x + track*W1.z, legLen = 0.0, xf = track;
  for (int i = 0; i < 4; i++) {   // (the hinge's height sets the leg's length, which sets where the wheel folds to)
    legLen = yh - wy; xf = track - legLen*cos(dl);
    yh = W1.x + xf*W1.z + gearStowGap(xf, mz, wr, 0.0) + legLen*sin(dl);
  }
  GearFold f; f.legLen = yh - wy; f.dl = dl; f.xf = track - f.legLen*cos(dl); f.H = vec3(track, yh, mz);
  f.floor0 = (yh - f.legLen*sin(dl) - 0.185) - f.xf*W1.z;
  return f;
}
vec3 gearHinge(){ return gearFold().H; }
float gearFoldAngle(){ return gearUp()*(atan(gM[10].z) - 1.5707963); }
// the fairing and its well (the right side's; F: well -> body): x spanwise, y up from the fairing's floor, z fore and
// aft from the gear's station; x0..x1, +-hz, depth up to 2 cm under the upper skin
struct GearWell { Pose F; float x0, x1, hz, depth; };
GearWell gearFoldWellOf(GearFold f){
  vec4 G0 = gM[18], W1 = gM[10];
  float track = G0.x, wr = G0.y, mz = G0.z;
  GearWell g;
  g.x1 = track + 0.12; g.x0 = max(f.xf - wr - 0.05, 0.05); g.hz = wr + 0.03;
  g.F.R = mat3(1.0, W1.z, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0);
  g.F.T = vec3(0.0, f.floor0, mz);
  g.depth = 0.185 + 0.14;   // (the floor to the folded wheel's hub caps: under the upper skin, gearStowGap)
  return g;
}
GearWell gearFoldWell(){ return gearFoldWellOf(gearFold()); }
// sdSurface's hinge line (body z) at span s: the flaps' and ailerons'
float wingHingeZ(float s){ vec4 W0 = gM[9], W1 = gM[10]; float k = clamp(s/W0.x, 0.0, 1.0); return W1.y + W0.w*k + 0.74*mix(W0.y, W0.z, k); }
// how far back a folding main's fairing may reach, over its span (from the root to its outboard end; the hinge line is
// straight in the span, so its ends bound it), clear of the flap's hinge line
float gearFairAft(){ return min(wingHingeZ(0.0), wingHingeZ(gM[18].x + 0.12)) - 0.03; }
float flapRoot(){ return 0.55*gM[0].w; }
// The inward fold keeps the wheel's station, and on some of these wings the mains stand near the flaps' hinge line (at
// their physics' station, 4% of the length aft of the datum): the well came across it, a bay in the flaps (it once
// moved the flaps outboard of it). Those swing forward into the wing instead, the wheel turned flat ahead of the
// hinge line (the Swift's, the XR-10's): the hinge H at the leg's top, as far aft as leaves the well 10 cm clear of
// the hinge line (the leg raked aft at rest), and at the height that keeps all of the flat wheel under the upper skin
// there (gearStowGap); T the stowed wheel's centre; the well from z0 to z1 along a shallow fairing whose floor - fy, 4
// cm under the wheel's lower caps - carries the doors, hw its half width
bool gearSwingMain(){ return int(gM[0].y + 0.5) == 4 && gM[18].z + gM[18].y + 0.03 > gearFairAft() + 0.005; }
struct GearSwing { vec3 H, T; float z0, z1, fy, hw; };
GearSwing gearSwingOf(){
  vec4 G0 = gM[18], W1 = gM[10];
  float track = G0.x, wr = G0.y, mz = G0.z;
  vec3 W = vec3(track, wr - gM[19].x, mz);
  float zh = min(mz, min(wingHingeZ(track - wr), wingHingeZ(track + wr)) - 0.13);
  GearSwing g; g.H = vec3(track, W1.x + track*W1.z, zh);
  float hy = g.H.y;
  for (int i = 0; i < 2; i++) { float tz = zh - length(W - vec3(track, hy, zh)); hy = W1.x + track*W1.z + gearStowGap(track, tz, wr, W1.z); }
  g.H.y = hy;
  g.T = g.H - vec3(0.0, 0.0, length(W - g.H));
  g.z0 = g.T.z - wr - 0.06; g.z1 = zh + 0.1; g.hw = wr + 0.05; g.fy = g.H.y - 0.185;
  return g;
}
// The retracting nose leg folds aft, a quarter turn about a crosswise pivot at its top, into a bay along the belly.
// (It rose straight up as far as the gear is tall: through the cabin floor, and in the shorter noses up to the
// windscreen.) The pivot stands the nose wheel's radius and 7 cm over the belly - the higher of the belly where the
// leg stands and where the wheel comes to rest - so the stowed wheel lies inside the fuselage. x: the pivot's height,
// y: the leg's length (the pivot to the wheel's centre)
float gearNoseR(){ return int(gM[0].y + 0.5) == 3 ? gM[18].y*0.75 : gM[18].y*0.85; }
vec2 gearNoseFold(){
  float nz = gM[18].w, nwr = gearNoseR(), wy = nwr - gM[19].x;
  vec3 s0 = fusSection(nz);
  float py = s0.z - s0.y + nwr + 0.07;
  for (int i = 0; i < 2; i++) { vec3 s1 = fusSection(nz + py - wy); py = max(s0.z - s0.y, s1.z - s1.y) + nwr + 0.07; }
  return vec2(py, py - wy);
}
// A leg folding about a crosswise pivot (body x) at height py, from its wheel's centre at rest W to its stowed place T
// (both (y, z)): the pivot as far from both - on their bisector - so one turn takes the wheel from one to the other.
// x: the pivot's z, y: the turn (about +x, as partRyz)
vec2 gearSwing(vec2 W, vec2 T, float py){
  vec2 m = 0.5*(W + T), d = T - W;
  float pz = m.y - d.x*(py - m.x)/(abs(d.y) > 1e-4 ? d.y : 1e-4);
  vec2 a = W - vec2(py, pz), b = T - vec2(py, pz);
  return vec2(pz, atan(a.x*b.y - a.y*b.x, dot(a, b)));
}
// A nacelle main (type 3, the right side's) folds forward into its nacelle: its wheels from their rest to the
// nacelle's fullest section, about a pivot 22 cm over the nacelle's floor - on the bisector, so the leg stands raked a
// little forward. (It rose straight up as far as the gear is tall, and the leg's top came out through the top of the
// wing.) P: the pivot, ang: the full turn, T: the wheels' stowed centre (y, z); z0..z1: the wheel bay along the
// nacelle's floor, z1..zs: the slot the leg swings through, closed by a door on the leg
struct NacFold { vec3 P; float ang; vec2 T; float z0, z1, zs; };
NacFold gearNacFold(){
  vec4 G0 = gM[18]; float wr = G0.y;
  float tz = gM[16].w + gM[17].x*0.3 - 0.2;
  vec2 W = vec2(wr - gM[19].x, G0.z), T = vec2(nacSection(tz).x, tz), sw = vec2(G0.z, 0.0);
  float py = 0.0;
  for (int i = 0; i < 3; i++) { vec2 ns = nacSection(sw.x); py = ns.x - ns.y + 0.22; sw = gearSwing(W, T, py); }
  NacFold f; f.P = vec3(G0.x, py, sw.x); f.ang = sw.y; f.T = T;
  f.z0 = tz - wr - 0.1; f.z1 = tz + wr + 0.12; f.zs = sw.x + 0.2;
  return f;
}
// the nacelle's skin under a main (x from its centre line, body y, z): the distance to its round section, below the axis
float gearNacSkin(float x, float y, float z){ vec2 ns = nacSection(z); return max(length(vec2(x, y - ns.x)) - ns.y, y - ns.x); }
// a bay the gear swings into (the nose wheel's, along the belly; type 3's mains', in the nacelle's floor): the opening's
// centre at the skin height of its hinges, its half width and length, its depth above that, how far the cut reaches
// below it to open a curved belly between the hinges, and its pitch (about +x: the nacelle's floor slopes)
struct VBay { vec3 c; vec2 h; float depth, below, pitch; };
float gearBellyAt(vec3 sN, float x){ return sN.z - sN.y*sqrt(max(1.0 - x*x/(sN.x*sN.x), 0.0)); }   // (an elliptic section's lower side at x across)
VBay gearVBay(bool nose){
  vec4 G0 = gM[18]; int gtype = int(gM[0].y + 0.5);
  float track = G0.x, wr = G0.y, mz = G0.z, nz = G0.w;
  VBay b;
  if (!nose && gtype == 4) {   // a main swung into the wing: along its fairing's floor
    GearSwing g = gearSwingOf();
    b.c = vec3(track, g.fy, 0.5*(g.z0 + g.z1)); b.h = vec2(g.hw, 0.5*(g.z1 - g.z0)); b.depth = g.H.y + 0.13 - g.fy; b.below = 0.03; b.pitch = 0.0;
  } else if (nose) {   // from just ahead of the pivot to behind the stowed wheel; its hinges at the highest skin along it
    float nw = gearNoseR(), nx = gtype == 3 ? 0.3 : 0.14;
    vec2 nf = gearNoseFold();
    // (its hinges along the chord of the belly's line from end to end: at the station it narrows towards the nose)
    float z0 = nz - 0.14, z1 = nz + nf.y + nw + 0.1;
    vec3 sA = fusSection(z0), sB = fusSection(0.5*(z0 + z1)), sC = fusSection(z1);
    vec3 hk = vec3(gearBellyAt(sA, nx), gearBellyAt(sB, nx), gearBellyAt(sC, nx));   // (the belly's height at the hinges, at either end and midway)
    vec3 lo = vec3(sA.z - sA.y, sB.z - sB.y, sC.z - sC.y);
    float cy = 0.5*(hk.x + hk.z), sag = max(hk.y - cy, 0.0);   // (a belly that rises above the chord at its middle: the doors sit that much higher, never out of the skin)
    b.c = vec3(0.0, cy + sag, 0.5*(z0 + z1)); b.h = vec2(nx, 0.5*length(vec2(z1 - z0, hk.z - hk.x))); b.pitch = -atan(hk.z - hk.x, z1 - z0);
    b.depth = nf.x + nw + 0.06 - b.c.y; b.below = b.c.y - min(min(lo.x, lo.y), lo.z) + 0.03;
  } else {   // the wheel bay: its hinges along the chord of the nacelle's floor, either end
    NacFold f = gearNacFold();
    vec2 n0 = nacSection(f.z0), n1 = nacSection(f.z1);
    float hx = min(0.42, min(n0.y, n1.y)*0.7);
    float y0 = n0.x - sqrt(max(n0.y*n0.y - hx*hx, 0.0)), y1 = n1.x - sqrt(max(n1.y*n1.y - hx*hx, 0.0));
    float len = length(vec2(f.z1 - f.z0, y1 - y0));
    b.c = vec3(track, 0.5*(y0 + y1), 0.5*(f.z0 + f.z1)); b.h = vec2(hx, 0.5*len); b.pitch = -atan(y1 - y0, f.z1 - f.z0);
    b.depth = f.T.x + wr + 0.06 - b.c.y; b.below = max(n0.y, n1.y) - sqrt(max(max(n0.y, n1.y)*max(n0.y, n1.y) - hx*hx, 0.0)) + 0.08;
  }
  return b;
}
// the gear's parts' poses (sd.x: the side, sd.y: which door)
Pose gearPartPose(int k, vec2 sd){
  vec4 G0 = gM[18], G1 = gM[19];
  int gtype = int(gM[0].y + 0.5);
  float mz = G0.z, nz = G0.w, gh = G1.x;
  float up = gtype >= 3 ? gearUp() : 0.0, a = gearDoorAngle();
  mat3 S = partMirror(sd.x);
  Pose X; X.R = mat3(1.0); X.T = vec3(0.0);
  if (k == PT_GEAR_MAIN) {
    if (gtype == 3) { NacFold f = gearNacFold(); mat3 Rf = partRyz(up*f.ang); X.R = S*Rf; X.T = S*(f.P - Rf*f.P); }
    else if (gearSwingMain()) { GearSwing g = gearSwingOf(); mat3 Rf = gearSwingR(vec3(G0.x, G0.y - gh, mz) - g.H, -1.0, up); X.R = S*Rf; X.T = S*(g.H - Rf*g.H); }
    else { vec3 H = gearHinge(); mat3 Rf = partRxy(gearFoldAngle()); X.R = S*Rf; X.T = S*(H - Rf*H); }
  } else if (k == PT_GEAR_NOSE) {   // (about its pivot, from the shape's rest frame at the station)
    X.R = partRxz(gPS.z)*max(gearNoseShow(), 1e-3); X.T = vec3(0.0, 0.0, nz);
    if (gtype >= 3) { vec3 pl = vec3(0.0, gearNoseFold().x, 0.0); X.R = partRyz(-1.5707963*up)*X.R; X.T = X.T + (pl - X.R*pl); }
  }
  else if (k == PT_GEAR_TAIL) { X.R = partRxz(gPS.z); X.T = vec3(0.0, 0.0, G1.y); }
  else if (k == PT_GEAR_MDOOR && gtype == 4 && !gearSwingMain()) {   // the fold well's doors, hinged along its long edges fore and aft
    GearWell g = gearFoldWell();
    float s = sd.y, ca = cos(a), sa = sin(a);
    Pose D; D.R = mat3(1.0, 0.0, 0.0,  0.0, ca, -s*sa,  0.0, -sa, -s*ca); D.T = vec3(0.5*(g.x0 + g.x1), 0.0, s*g.hz);
    X = poseMul(g.F, D); X.R = S*X.R; X.T = S*X.T;
  } else {   // a vertical bay's doors, hinged along its sides
    VBay b = gearVBay(k == PT_GEAR_NDOOR);
    float s = sd.y;
    mat3 Rp = partRyz(b.pitch);
    X.R = Rp*partMirror(-s)*partRxy(-a); X.T = b.c + Rp*vec3(s*b.h.x, 0.0, 0.0);
    if (k == PT_GEAR_MDOOR) { X.R = S*X.R; X.T = S*X.T; }
  }
  return X;
}
mat3 partLever(float a){ float c = cos(a), s = sin(a); return mat3(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c); }   // local +y along (0, cos a, -sin a)
// the light aircraft's centre pedestal (plane_sdf.glsl): its centre, half width, half height and half depth
void partPedestal(out vec3 pc, out float pw, out float ph, out float pd){
  vec4 E = gM[22]; float pz = gM[21].w; int ck = int(gM[21].z + 0.5);
  pw = ck == 0 ? 0.075 : 0.11; ph = 0.22; pd = ck == 0 ? 0.24 : 0.32;
  pc = vec3(0.0, E.y - 0.84 + (0.22 - ph), pz + 0.06 + pd);
}
// the pedals' height: clear of the floor where the belly curves up towards the firewall
float partPedalY(){
  vec4 E = gM[22]; float pz = gM[21].w;
  vec3 sP = fusSection(pz + 0.2);
  float kx = clamp((abs(E.x) + 0.1)/max(sP.x - 0.035, 0.01), 0.0, 0.98);
  float floorY = max(E.y - 1.06, sP.z - (sP.y - 0.035)*sqrt(1.0 - kx*kx) + 0.03);
  return max(E.y - 0.98, floorY + 0.09);
}
// sd: the instance (x: which seat or which side, -1 or 1; y: the left or right pedal of the pair). The cockpit's
// controls and the light aircraft's surfaces (the airframe field places the cockpit's in place: partAt below)
Pose partPoseCockpit(int k, vec2 sd){
  vec4 E = gM[22]; float pz = gM[21].w;
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z, cThr = gCtl.w;
  Pose X; X.R = mat3(1.0); X.T = vec3(0.0);
  mat3 D = mat3(-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);   // the yokes' frame: x mirrored, each in its pilot's own frame
  vec3 O = vec3(sd.x*abs(E.x), E.y - 0.43, pz);
  float pull = cPitch*0.075;
  if (k == PT_YOKE_SHAFT) { X.R = D; X.T = O + vec3(0.0, 0.0, pull); }
  else if (k == PT_YOKE_WHEEL) { X.R = D*transpose(partRxy(-cRoll*0.75)); X.T = O + vec3(0.0, 0.0, 0.22 + pull); }   // (roll right, cRoll > 0: clockwise as the pilot sees it - D's mirror turns the angle round)
  else if (k == PT_PEDAL) {   // right rudder (yaw > 0) pushes the right pedal forward (-z), left rudder the left
    float q = sd.y;
    X.R = mat3(q, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    X.T = vec3(sd.x*abs(E.x) + q*0.1, partPedalY(), pz + 0.2 - q*cYaw*0.06);
  } else if (k == PT_THR_KNOB) X.T = vec3(0.0, E.y - 0.5, pz + 0.05 + 0.1*(1.0 - cThr));
  else if (k == PT_THR_LEVER || k == PT_FLAP_LEVER) {
    vec3 pc; float pw, ph, pd; partPedestal(pc, pw, ph, pd);
    if (k == PT_THR_LEVER) {
      mat3 Dq = mat3(sd.x, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
      X.R = Dq*partLever(mix(-0.55, 0.6, cThr));
      X.T = Dq*vec3(0.035, pc.y + ph - 0.02, pc.z - pd*0.35);
    } else {
      X.R = partLever(mix(0.3, -0.5, gPS.y));
      X.T = vec3(pw*0.6, pc.y + ph - 0.02, pc.z + pd*0.1);
    }
  }
  else if (k == PT_JET_STICK) { X.R = transpose(partRxy(cRoll*0.25)*partRyz(-cPitch*0.25)); X.T = E.xyz + (isMantis()?vec3(.43,-.535,-.28):vec3(0.42, -0.375, -0.02)); }
  else if (k == PT_JET_THR) X.T = E.xyz + (isMantis()?vec3(-.43,-.505,-.28+0.12*(0.5-cThr)):vec3(-0.42, -0.34, -0.02 + 0.12*(0.5 - cThr)));
  else if (k == PT_WR_STICK) { X.R = transpose(partRxy(cRoll*0.25)*partRyz(-cPitch*0.25)); X.T = E.xyz + vec3(0.5, -0.41, -0.06); }
  else if (k == PT_WR_THR) X.T = E.xyz + vec3(-0.5, -0.38, -0.08 + 0.13*(0.5 - cThr));
  else if (k == PT_FLAP || k == PT_AILERON) {   // the wing's (plane_sdf.glsl mapPlane)
    vec4 W0 = gM[9], W1 = gM[10], W2 = gM[11];
    float flaps = gPS.y;
    X = k == PT_FLAP ? surfPose(sd.x, W1.x, W1.y, W1.z, W0.x, W0.y, W0.z, W0.w, 0.74, flaps*0.62, flaps*0.1)
                     : surfPose(sd.x, W1.x, W1.y, W1.z, W0.x, W0.y, W0.z, W0.w, 0.74, -cRoll*sd.x*0.33, 0.0);
  } else if (k == PT_ELEVATOR) {
    vec4 H0 = gM[12], H1 = gM[13];
    X = surfPose(sd.x, H1.x, H1.y, H1.z, H0.x, H0.y, H0.z, H0.w, 0.68, (isMantis() ? cPitch : -cPitch)*0.4, 0.0);
  } else if (k == PT_RUDDER) {   // the fin's frame: span up (y), chord aft (z), thickness across (x)
    vec4 V0 = gM[14], V1 = gM[15];
    mat3 A = mat3(0.0, 0.0, 1.0,   1.0, 0.0, 0.0,   0.0, 1.0, 0.0); vec3 a = vec3(-V1.x, -V1.y, 0.0);
    mat3 B = mat3(0.0, 1.0, 0.0,   0.0, 0.0, 1.0,   1.0, 0.0, 0.0); vec3 b = vec3(0.0, V1.x, V1.y);
    mat3 D; vec3 d; surfDefl(V0.x, V0.y, V0.z, V0.w, 0.66, -cYaw*0.42*(isMantis()?sd.x:1.0), 0.0, D, d);
    X.R = B*D*A; X.T = B*(D*a + d) + b;
    if(isMantis()) {
      // Rest mesh stays in the generic fin frame. Two instances cant/translate it onto the fixed airframe shoulders.
      mat3 C=partMirror(sd.x)*partRxy(-MT_CANT); vec3 P=vec3(0.0,V1.x,V1.y);
      X.T=C*(X.T-P)+vec3(sd.x*MT_FIN_X,V1.x,V1.y); X.R=C*X.R;
    }
  }
  else if (k == PT_WR_PEDAL) {
    X.R = mat3(sd.x, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    X.T = E.xyz + vec3(sd.x*0.16, -0.63, -0.78 - sd.x*cYaw*0.04);   // (right rudder, yaw > 0, pushes the right pedal forward, -z: it pulled it back)
  }
  return X;
}
// The research jets' retracting tricycle gear (the XR-30's and the XR-40's, under their own airframes). The mains swing
// into the wing with the wheel turned flat (gearSwingR): the XR-30's aft (its main stands just behind the swept
// leading edge), the XR-40's forward, its leg raked aft at rest so the wheel comes to lie where the wing is thicker.
// The nose leg folds aft along the belly. (They rose straight up, the struts shortening, through wings a third as thick
// as the wheel is tall: the wheel showed through the top of the wing until it was drawn as nothing below 6%.)
// JtGear: the main's hinge H (the right side's), its swing dir (-1 forward) and the stowed wheel's centre T; its well
// along a shallow fairing's floor (centre mc, half size mh, depth md); the nose's pivot P and its well along the belly
// (centre nc, half size nh, pitch np, depth nd, how far the cut reaches below the hinges nb)
const float JT_TYRE_H = 0.10;   // (the main tyres' half width: slim enough to lie flat in the wing)
struct JtGear { vec3 H, T, mc, P, nc; vec2 mh, nh; float dir, md, np, nd, nb; };
// the belly's height at body z, x across (the XR-40's faceted section: wraith_sdf.glsl wrSection; the XR-30's round)
float jtBelly(float z, float x){
  if (int(gM[0].z + 0.5) == 6) {
    float zc = clamp(z, -8.3, 7.8), yc = -0.1 - 0.12*smoothstep(-5.0, -8.4, zc);
    float W = zc < -4.4 ? (zc + 8.4)*0.30 : (zc < 4.5 ? 1.2 + (zc + 4.4)*0.012 : 1.31 - (zc - 4.5)*0.13);
    float bot = yc - min((zc + 8.4)*0.12, 0.48) + max(zc - 5.0, 0.0)*0.06;
    return bot + max(abs(x) - W*0.42, 0.0)*(yc - bot)/max(W*0.58, 1e-3);
  }
  vec3 s = fusSection(z); return s.z - s.y*sqrt(max(1.0 - x*x/(s.x*s.x), 0.0));
}
// the jet's wing's upper skin at span x, body z (sdPanel's frame and sizes: plane_sdf.glsl mapJet, wraith_sdf.glsl
// mapWraith with wrSection's centre line)
float jtWingTop(float x, float z){
  bool wr = int(gM[0].z + 0.5) == 6;
  float s = wr ? x - 1.0 : x, c = wr ? z + 3.0 : z + 1.6;
  float span = wr ? 5.2 : 5.6, rc = wr ? 8.2 : 7.2, tc = wr ? 1.35 : 1.2, sweep = wr ? 4.7 : 5.6, th = wr ? 0.035 : 0.04;
  float k = clamp(s/span, 0.0, 1.0), ch = mix(rc, tc, k), le = sweep*k, r1 = th*ch*0.5, r2 = max(0.004*ch, 0.005);
  float hw = mix(r1, r2, clamp((c - le - r1)/max(ch - r1 - r2, 0.01), 0.0, 1.0));
  return (wr ? -0.12 - 0.12*smoothstep(-5.0, -8.4, z) - s*0.012 : -0.18 - s*0.035) + hw;
}
// the stowed main wheel's centre height, lying flat with its centre at (x, z): as gearStowGap, all of it under the skin
float jtStowY(float x, float z){
  float y = jtWingTop(x, z) - 0.16;
  y = min(y, min(min(jtWingTop(x + 0.21, z), jtWingTop(x - 0.21, z)), min(jtWingTop(x, z + 0.21), jtWingTop(x, z - 0.21))) - 0.145);
  y = min(y, min(min(jtWingTop(x + 0.34, z), jtWingTop(x - 0.34, z)), min(jtWingTop(x, z + 0.34), jtWingTop(x, z - 0.34))) - 0.116);
  return y;
}
JtGear jtGearOf(){
  bool wr = int(gM[0].z + 0.5) == 6;
  vec4 G0 = gM[18]; float gh = gM[19].x, track = G0.x, mz = G0.z, nz = G0.w;
  JtGear g;
  vec3 W = vec3(track, 0.38 - gh, mz);
  g.dir = wr ? -1.0 : 1.0;
  g.H = vec3(track, -0.2, wr ? mz - 0.5 : mz);   // (its height: where the flat wheel lies under the wing's upper skin, hub caps and all)
  float hy = g.H.y;
  for (int i = 0; i < 2; i++) hy = jtStowY(track, g.H.z + g.dir*length(W - vec3(track, hy, g.H.z)));
  g.H.y = hy;
  g.T = g.H + vec3(0.0, 0.0, g.dir*length(W - g.H));
  float z0 = g.dir < 0.0 ? g.T.z - 0.44 : g.H.z - 0.12, z1 = g.dir < 0.0 ? g.H.z + 0.12 : g.T.z + 0.44;
  float fy = g.H.y - JT_TYRE_H - 0.085;
  g.mc = vec3(track, fy, 0.5*(z0 + z1)); g.mh = vec2(0.41, 0.5*(z1 - z0)); g.md = g.H.y + JT_TYRE_H + 0.03 - fy;
  float nwy = 0.33 - gh, py = jtBelly(nz, 0.0) + 0.40;
  for (int i = 0; i < 2; i++) py = max(jtBelly(nz, 0.0), jtBelly(nz + py - nwy, 0.0)) + 0.40;
  g.P = vec3(0.0, py, nz);
  float n0 = nz - 0.16, n1 = nz + py - nwy + 0.43;
  float b0 = jtBelly(n0, 0.24), b1 = jtBelly(n1, 0.24), cy = 0.5*(b0 + b1), sag = max(jtBelly(0.5*(n0 + n1), 0.24) - cy, 0.0);
  g.nc = vec3(0.0, cy + sag, 0.5*(n0 + n1)); g.nh = vec2(0.24, 0.5*length(vec2(n1 - n0, b1 - b0))); g.np = -atan(b1 - b0, n1 - n0);
  g.nd = py + 0.38 - g.nc.y;
  g.nb = g.nc.y - min(min(jtBelly(n0, 0.0), jtBelly(n1, 0.0)), jtBelly(0.5*(n0 + n1), 0.0)) + 0.03;
  return g;
}
// the main's leg and wheel, swung as one (S: the side); the nose's, folded about its pivot
Pose jtMainPose(mat3 S){
  JtGear g = jtGearOf();
  mat3 R = gearSwingR(vec3(gM[18].x, 0.38 - gM[19].x, gM[18].z) - g.H, g.dir, gearUp());
  Pose X; X.R = S*R; X.T = S*(g.H - R*g.H); return X;
}
Pose jtNosePose(){ JtGear g = jtGearOf(); mat3 R = partRyz(-1.5707963*gearUp()); Pose X; X.R = R; X.T = g.P - R*g.P; return X; }
// a bay's door (hinged along its side as the light aircraft's are: gearPartPose), pitched with the bay
Pose jtDoorPose(vec3 c, vec2 h, float s, mat3 S, float pitch){
  mat3 Rp = partRyz(pitch);
  Pose X; X.R = S*Rp*partMirror(-s)*partRxy(-gearDoorAngle()); X.T = S*(c + Rp*vec3(s*h.x, 0.0, 0.0)); return X;
}
// the XR-30's surfaces, nozzles and gear
Pose jtPartPose(int k, vec2 sd){
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z;
  Pose X;
  if (k >= PT_JT_NOZZLE) {
    vec4 G0 = gM[18]; float gh = gM[19].x;
    mat3 S = partMirror(sd.x);
    if (k == PT_JT_NOZZLE) { X.R = S*partRyz(gFlame.z); X.T = S*vec3(0.82, -0.12, 7.75); }   // (about its front edge, by the vectoring angle)
    else if (k == PT_JT_LEGM || k == PT_JT_WHEELM) X = jtMainPose(S);
    else if (k == PT_JT_LEGN || k == PT_JT_WHEELN) X = jtNosePose();
    else if (k == PT_JT_DOORM) { JtGear g = jtGearOf(); X = jtDoorPose(g.mc, g.mh, sd.y, S, 0.0); }
    else { JtGear g = jtGearOf(); X = jtDoorPose(g.nc, g.nh, sd.y, mat3(1.0), g.np); }
    return X;
  }
  if (k == PT_JT_ELEVON) {   // the wing's frame (s, c, t) = (|x|, z + 1.6, y + 0.18 + 0.035 s)
    mat3 Ar = mat3(1.0, 0.0, 0.035, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0); vec3 a = vec3(0.0, 1.6, 0.18);
    mat3 D; vec3 d; surfDefl(5.6, 7.2, 1.2, 5.6, 0.84, -cPitch*0.3 - cRoll*sd.x*0.3, 0.0, D, d);
    X = surfPoseAffine(Ar, Ar*partMirror(sd.x), a, D, d);
  } else if (k == PT_JT_CANARD) {   // all-moving, about its spanwise pivot
    mat3 S = partMirror(sd.x);
    X.R = S*partRyz(-cPitch*0.3); X.T = S*vec3(0.6, -0.02, -6.4);
  } else {   // the rudder, in the canted fin's frame
    float C = cos(0.42), Sn = sin(0.42);
    mat3 Ar = mat3(Sn, 0.0, C,   C, 0.0, -Sn,   0.0, 1.0, 0.0); vec3 a = vec3(-Sn - 0.3*C, -4.6, -C + 0.3*Sn);
    mat3 D; vec3 d; surfDefl(2.3, 2.6, 1.0, 1.9, 0.7, -cYaw*0.4*sd.x, 0.0, D, d);
    X = surfPoseAffine(Ar, Ar*partMirror(sd.x), a, D, d);
  }
  return X;
}
// any part's pose (the pose pass, the bake): each family's own function, so a call site that places one family never
// inlines the others' code
Pose partPose(int k, vec2 sd){
  if (partIsWraith(k)) return wrPartPose(k, sd);
  if (partIsJet(k)) return jtPartPose(k, sd);
  if (k >= PT_GEAR_MAIN && k <= PT_GEAR_NDOOR) return gearPartPose(k, sd);
  return partPoseCockpit(k, sd);
}
#ifdef PART_BAKE
vec2 gearPartField(int k, vec3 l);   // (plane_sdf.glsl)
vec2 wrPartField(int k, vec3 l);   // (wraith_sdf.glsl)
vec2 jtPartField(int k, vec3 l);   // (plane_sdf.glsl)
#endif
// each part's shape in its own frame: distance and material id (the cockpit's controls and the light aircraft's surfaces)
vec2 partFieldCockpit(int k, vec3 l){
  vec2 res = vec2(1e9, 0.0);
  if (k == PT_YOKE_SHAFT) res = vec2(sdCapsule(l, vec3(0.0, 0.0, -0.06), vec3(0.0, 0.0, 0.2), 0.017), 60.0);   // (it slides through the panel)
  else if (k == PT_YOKE_WHEEL) {
    float hub = sdRoundBox(l, vec3(0.06, 0.03, 0.022), 0.015);
    float horns = sdCapsule(vec3(abs(l.x), l.yz), vec3(0.05, 0.0, 0.0), vec3(0.118, 0.012, 0.0), 0.016);
    float grips = sdCapsule(vec3(abs(l.x), l.yz), vec3(0.124, 0.0, 0.0), vec3(0.13, 0.095, 0.0), 0.02);
    res = vec2(smin(hub, horns, 0.02), 66.0);
    res = opU(res, vec2(grips, 61.0));
    res = opU(res, vec2(length(vec3(abs(l.x) - 0.128, l.y - 0.105, l.z + 0.004)) - 0.009, 68.0));   // PTT / trim switches
    res = opU(res, vec2(sdCylX(l.zyx + vec3(0.024, 0.0, 0.0), 0.02, 0.003), 60.0));               // hub badge
  } else if (k == PT_PEDAL) {
    vec3 pr = l; pr.yz = rot2(pr.yz, 0.5);
    float psz = int(gM[21].z + 0.5) == 0 ? 0.8 : 1.0;   // smaller pedals in the cramped light-aircraft footwells
    res = vec2(sdRoundBox(pr, vec3(0.045, 0.08, 0.01)*psz, 0.008), 61.0);
    res = opU(res, vec2(sdCapsule(l, vec3(0.0, 0.05, -0.03), vec3(0.0, 0.2, -0.21), 0.011), 60.0));   // arm up to the footwell wall
  } else if (k == PT_THR_KNOB) {   // push-pull: the shaft slides through the panel
    res = vec2(min(sdCapsule(l, vec3(0.0, 0.0, -0.16), vec3(0.0), 0.006), length(l) - 0.022), 66.0);
  } else if (k == PT_THR_LEVER) {
    res = vec2(sdCapsule(l, vec3(0.0), vec3(0.0, 0.16, 0.0), 0.008), 60.0);
    res = opU(res, vec2(sdRoundBox(l - vec3(0.0, 0.16, 0.0), vec3(0.03, 0.014, 0.02), 0.009), 66.0));
  } else if (k == PT_FLAP_LEVER) {
    res = vec2(min(sdCapsule(l, vec3(0.0), vec3(0.0, 0.11, 0.0), 0.006), sdRoundBox(l - vec3(0.0, 0.11, 0.0), vec3(0.022, 0.006, 0.012), 0.004)), 60.0);
  } else if (k == PT_JET_STICK) {
    res = vec2(min(sdCapsule(l, vec3(0.0), vec3(0.0, 0.11, -0.01), 0.016), sdRoundBox(l - vec3(0.0, 0.15, -0.01), vec3(0.021, 0.042, 0.027), 0.015)), isMantis()?61.0:47.0);
  } else if (k == PT_JET_THR) res = vec2(sdRoundBox(l, vec3(0.03, 0.04, 0.05), 0.02), isMantis()?61.0:47.0);
  else if (k == PT_WR_STICK) {
    float stick = sdCapsule(l, vec3(0.0), vec3(0.0, 0.09, -0.01), 0.014);
    vec3 gq = l - vec3(0.0, 0.14, -0.015);
    float grip = max(sdBox(gq, vec3(0.022, 0.05, 0.03)), (abs(gq.x) + abs(gq.z))*0.70711 - 0.03);   // faceted grip
    grip = min(grip, sdBox(gq - vec3(0.0, 0.055, -0.01), vec3(0.018, 0.012, 0.022)));
    res = vec2(min(stick, grip), 70.0);
    res = opU(res, vec2(sdCapsule(gq - vec3(0.0, 0.066, -0.016), vec3(-0.008, 0.0, 0.0), vec3(0.008, 0.0, 0.0), 0.006), 67.0));   // trigger lights
  } else if (k == PT_WR_THR) {
    res = vec2(max(sdBox(l, vec3(0.032, 0.045, 0.06)), (abs(l.y) + abs(l.z))*0.70711 - 0.07), 70.0);
    res = opU(res, vec2(sdBox(l - vec3(0.0, 0.046, -0.02), vec3(0.02, 0.002, 0.03)), 67.0));
  } else if (k == PT_FLAP || k == PT_AILERON) {   // at rest, the right wing's (body space)
    vec4 W0 = gM[9], W1 = gM[10], W2 = gM[11];
    float span = W0.x, sv = l.x, t = l.y - (W1.x + sv*W1.z), c = l.z - W1.y;
    float fus0 = flapRoot(), flapEnd = span*W2.w, ailEnd = span*0.94;
    res = vec2(k == PT_FLAP ? sdSurface(sv, c, t, span, W0.y, W0.z, W0.w, W1.w, 0.74, fus0, flapEnd, 0.0, 0.0)
                            : sdSurface(sv, c, t, span, W0.y, W0.z, W0.w, W1.w, 0.74, flapEnd + 0.03, ailEnd, 0.0, 0.0), 2.0);
    // from the cockpit, outside the fuselage only, as the wing is (mapPlane): a high wing's flap starts over the cabin
    // roof, and its inboard end hung through it as a black plate at the top of the windscreen
    if (gPS.w > 0.5) { vec3 sec = fusSection(l.z); res.x = max(res.x, -(length(vec2(l.x/sec.x, (l.y - sec.z)/sec.y)) - 1.0)*min(sec.x, sec.y)); }
  } else if (k == PT_ELEVATOR) {
    vec4 H0 = gM[12], H1 = gM[13];
    float hs = l.x, ht = l.y - (H1.x + hs*H1.z), hc = l.z - H1.y;
    res = vec2(sdSurface(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98, 0.0, 0.0), 3.0);
    // A forward canard crosses the cockpit's z range. Match the cabin field's clipping when its rigid mesh
    // is drawn from inside, as the wing flaps already do, so the inboard elevator cannot enter the footwell.
    if (isMantis() && gPS.w > 0.5) { vec3 sec=fusSection(l.z); res.x=max(res.x,-(length(vec2(l.x/sec.x,(l.y-sec.z)/sec.y))-1.0)*min(sec.x,sec.y)); }
  } else if (k == PT_RUDDER) {
    vec4 V0 = gM[14], V1 = gM[15];
    float h = V0.x, rud0 = gM[13].w > 0.5 ? 0.05 : 0.08*h;
    res = vec2(sdSurface(l.y - V1.x, l.z - V1.y, l.x, h, V0.y, V0.z, V0.w, 0.11, 0.66, rud0, h*0.97, 0.0, 0.0), 3.0);
  } else if (k == PT_WR_PEDAL) {
    float ped = max(sdBox(l, vec3(0.05, 0.075, 0.012)), (abs(l.x) + abs(l.y))*0.70711 - 0.08);
    ped = min(ped, sdCapsule(l, vec3(0.0, -0.07, 0.02), vec3(0.0, -0.12, 0.1), 0.012));
    res = vec2(ped, 71.0);
  }
  return res;
}
#ifdef PART_BAKE
// any part's shape (the mesh bake only, PART_BAKE: one part alone in its own frame)
vec2 partField(int k, vec3 l){
  if (partIsWraith(k)) return wrPartField(k, l);
  if (partIsJet(k)) return jtPartField(k, l);
  if (k >= PT_GEAR_MAIN && k <= PT_GEAR_NDOOR) return gearPartField(k, l);
  return partFieldCockpit(k, l);
}
#endif
// a part in the whole aircraft's field: at its pose, or left out (the static bake). The cockpit's controls (rotations)
vec2 partAt(vec2 res, int k, vec2 sd, vec3 p){
  if (gPartMode != -1) return res;
  Pose X = partPoseCockpit(k, sd);
  return opU(res, partFieldCockpit(k, transpose(X.R)*(p - X.T)));
}

