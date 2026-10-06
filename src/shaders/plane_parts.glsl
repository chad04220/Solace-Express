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
// A wing-retracting main (type 4) folds inboard about a fore-and-aft hinge, until the leg lies along the wing (its
// dihedral) and the wheel lies flat under the wing root, in a streamlined fairing: raised straight up it came through
// the top of a wing a third as thick as the wheel is tall, and even flat the wheel (20 cm across its tyre) is thicker
// than these wings where the gear stands (7 to 15 cm). The hinge is placed so the folded wheel's top stays 2 cm under
// the upper skin; the fairing's floor (its doors) runs 4 cm under the wheel, parallel to the wing.
float wingHalf(float s, float z){   // the wing's half thickness at span s and body z (sdPanel: an uneven capsule from the LE radius to the TE radius)
  vec4 W0 = gM[9], W1 = gM[10];
  float kk = clamp(s/W0.x, 0.0, 1.0), ch = mix(W0.y, W0.z, kk), le = W1.y + W0.w*kk;
  float r1 = W1.w*ch*0.5, r2 = max(0.004*ch, 0.005);
  return mix(r1, r2, clamp((z - le - r1)/max(ch - r1 - r2, 0.01), 0.0, 1.0));
}
struct GearFold { vec3 H; float legLen, dl, xf, floor0; };   // (floor0: the fairing floor's height at x 0, rising with the dihedral)
GearFold gearFold(){
  vec4 G0 = gM[18], G1 = gM[19], W1 = gM[10];
  float track = G0.x, wr = G0.y, mz = G0.z, wy = wr - G1.x, dl = atan(W1.z);
  float yh = W1.x + track*W1.z, legLen = 0.0, xf = track;
  for (int i = 0; i < 4; i++) {   // (the hinge's height sets the leg's length, which sets where the wheel folds to)
    legLen = yh - wy; xf = track - legLen*cos(dl);
    yh = W1.x + xf*W1.z + wingHalf(xf, mz) - 0.12 + legLen*sin(dl);
  }
  GearFold f; f.legLen = yh - wy; f.dl = dl; f.xf = track - f.legLen*cos(dl); f.H = vec3(track, yh, mz);
  f.floor0 = (yh - f.legLen*sin(dl) - 0.14) - f.xf*W1.z;
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
  g.x1 = track + 0.12; g.x0 = max(f.xf - wr - 0.05, 0.05); g.hz = wr + 0.07;
  g.F.R = mat3(1.0, W1.z, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0);
  g.F.T = vec3(0.0, f.floor0, mz);
  g.depth = 0.14 + 0.1;   // (the floor to the folded wheel's top: 2 cm under the upper skin)
  return g;
}
GearWell gearFoldWell(){ return gearFoldWellOf(gearFold()); }
// sdSurface's hinge line (body z) at span s: the flaps' and ailerons'
float wingHingeZ(float s){ vec4 W0 = gM[9], W1 = gM[10]; float k = clamp(s/W0.x, 0.0, 1.0); return W1.y + W0.w*k + 0.74*mix(W0.y, W0.z, k); }
// how far back a folding main's fairing may reach, over its span (from the root to its outboard end; the hinge line is
// straight in the span, so its ends bound it), clear of the flap's hinge line
float gearFairAft(){ return min(wingHingeZ(0.0), wingHingeZ(gM[18].x + 0.12)) - 0.03; }
// These mains stand about where the flaps hinge (and the fold keeps the wheel's station): where the fairing's
// trailing end (gearFoldWellOf's hz and its rounding) would reach under the flap, the flap starts just outboard of
// the fairing instead and the wing root over it stays fixed, so a lowered flap never cuts through the folded gear
bool gearFlapOutboard(){ return int(gM[0].y + 0.5) == 4 && gM[18].z + gM[18].y + 0.1 > gearFairAft(); }
float flapRoot(){ float fr = 0.55*gM[0].w; return gearFlapOutboard() ? max(fr, gM[18].x + 0.16) : fr; }
// a bay the wheel rises straight into (the nose wheel's; type 3's mains', in the nacelle): the opening's centre at the
// skin height of its hinges, its half width and length, its depth above that, and how far the cut reaches below it to
// open a curved belly between the hinges
struct VBay { vec3 c; vec2 h; float depth, below; };
VBay gearVBay(bool nose){
  vec4 G0 = gM[18]; int gtype = int(gM[0].y + 0.5);
  float track = G0.x, wr = G0.y, mz = G0.z, nz = G0.w;
  VBay b;
  if (nose) {
    vec3 sN = fusSection(nz); float nw = gtype == 3 ? wr*0.75 : wr*0.85, nx = gtype == 3 ? 0.3 : 0.14;
    float ey = sN.y*sqrt(max(1.0 - nx*nx/(sN.x*sN.x), 0.0));   // (the belly's height at the hinges: an elliptic section)
    b.c = vec3(0.0, sN.z - ey, nz); b.h = vec2(nx, nw + 0.08); b.depth = 2.0*nw + 0.15 + sN.y - ey; b.below = sN.y - ey + 0.03;
  } else {
    vec2 ns = nacSection(mz); float rr = ns.y, hx = min(0.38, rr*0.7), hy = sqrt(max(rr*rr - hx*hx, 0.0)), sk = ns.x - hy;
    b.c = vec3(track, sk, mz); b.h = vec2(hx, wr + 0.08); b.depth = (ns.x - ns.y + 0.03) + 2.0*wr - sk + 0.1; b.below = rr - hy + 0.03;
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
    if (gtype == 3) { vec2 ns = nacSection(mz); X.R = S; X.T = S*vec3(0.0, up*(ns.x - ns.y + 0.03 + gh), 0.0); }
    else { vec3 H = gearHinge(); mat3 Rf = partRxy(gearFoldAngle()); X.R = S*Rf; X.T = S*(H - Rf*H); }
  } else if (k == PT_GEAR_NOSE) { X.R = partRxz(gPS.z); X.T = vec3(0.0, up*(gh - gM[0].w*0.6), nz); }
  else if (k == PT_GEAR_TAIL) { X.R = partRxz(gPS.z); X.T = vec3(0.0, 0.0, G1.y); }
  else if (k == PT_GEAR_MDOOR && gtype == 4) {   // the fold well's doors, hinged along its long edges fore and aft
    GearWell g = gearFoldWell();
    float s = sd.y, ca = cos(a), sa = sin(a);
    Pose D; D.R = mat3(1.0, 0.0, 0.0,  0.0, ca, -s*sa,  0.0, -sa, -s*ca); D.T = vec3(0.5*(g.x0 + g.x1), 0.0, s*g.hz);
    X = poseMul(g.F, D); X.R = S*X.R; X.T = S*X.T;
  } else {   // a vertical bay's doors, hinged along its sides
    VBay b = gearVBay(k == PT_GEAR_NDOOR);
    float s = sd.y;
    X.R = partMirror(-s)*partRxy(-a); X.T = b.c + vec3(s*b.h.x, 0.0, 0.0);
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
  else if (k == PT_YOKE_WHEEL) { X.R = D*transpose(partRxy(cRoll*0.75)); X.T = O + vec3(0.0, 0.0, 0.22 + pull); }
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
  else if (k == PT_JET_STICK) { X.R = transpose(partRxy(cRoll*0.25)*partRyz(-cPitch*0.25)); X.T = E.xyz + vec3(0.42, -0.375, -0.02); }
  else if (k == PT_JET_THR) X.T = E.xyz + vec3(-0.42, -0.34, -0.02 + 0.12*(0.5 - cThr));
  else if (k == PT_WR_STICK) { X.R = transpose(partRxy(cRoll*0.25)*partRyz(-cPitch*0.25)); X.T = E.xyz + vec3(0.5, -0.41, -0.06); }
  else if (k == PT_WR_THR) X.T = E.xyz + vec3(-0.5, -0.38, -0.08 + 0.13*(0.5 - cThr));
  else if (k == PT_FLAP || k == PT_AILERON) {   // the wing's (plane_sdf.glsl mapPlane)
    vec4 W0 = gM[9], W1 = gM[10], W2 = gM[11];
    float flaps = gPS.y;
    X = k == PT_FLAP ? surfPose(sd.x, W1.x, W1.y, W1.z, W0.x, W0.y, W0.z, W0.w, 0.74, flaps*0.62, flaps*0.1)
                     : surfPose(sd.x, W1.x, W1.y, W1.z, W0.x, W0.y, W0.z, W0.w, 0.74, -cRoll*sd.x*0.33, 0.0);
  } else if (k == PT_ELEVATOR) {
    vec4 H0 = gM[12], H1 = gM[13];
    X = surfPose(sd.x, H1.x, H1.y, H1.z, H0.x, H0.y, H0.z, H0.w, 0.68, -cPitch*0.4, 0.0);
  } else if (k == PT_RUDDER) {   // the fin's frame: span up (y), chord aft (z), thickness across (x)
    vec4 V0 = gM[14], V1 = gM[15];
    mat3 A = mat3(0.0, 0.0, 1.0,   1.0, 0.0, 0.0,   0.0, 1.0, 0.0); vec3 a = vec3(-V1.x, -V1.y, 0.0);
    mat3 B = mat3(0.0, 1.0, 0.0,   0.0, 0.0, 1.0,   1.0, 0.0, 0.0); vec3 b = vec3(0.0, V1.x, V1.y);
    mat3 D; vec3 d; surfDefl(V0.x, V0.y, V0.z, V0.w, 0.66, -cYaw*0.42, 0.0, D, d);
    X.R = B*D*A; X.T = B*(D*a + d) + b;
  }
  else if (k == PT_WR_PEDAL) {
    X.R = mat3(sd.x, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    X.T = E.xyz + vec3(sd.x*0.16, -0.63, -0.78 - sd.x*cYaw*0.04);   // (right rudder, yaw > 0, pushes the right pedal forward, -z: it pulled it back)
  }
  return X;
}
// the research jets' gear bays (the XR-30's and the XR-40's: the same gear, in bays at their own skin heights): a bay's
// centre at the skin (the right main's), its half width and half length
vec3 jtBayC(bool nose){
  bool wr = int(gM[0].z + 0.5) == 6;
  return nose ? vec3(0.0, wr ? -0.43 : -0.39, gM[18].w) : vec3(gM[18].x, wr ? -0.21 : -0.355, gM[18].z);
}
const vec2 JT_MAIN_BAY_H = vec2(0.2, 0.46), JT_NOSE_BAY_H = vec2(0.24, 0.42);
// how far the gear has risen: it rises straight up, the struts shortening and the wheels sliding (the XR-40's folds
// flush with its belly)
float jtGearLift(){ return (1.0 - gPS.x)*(gM[19].x - (int(gM[0].z + 0.5) == 6 ? 0.19 : 0.5)); }
// a strut from its fixed mount A to its foot B (rest), shortened along y by the lift; below 6% extension the gear is
// stowed and drawn as nothing (as the field draws it): the pose collapses to the mount
Pose jtStrutPose(vec3 A, vec3 B, mat3 S){
  float show = gPS.x > 0.06 ? 1.0 : 1e-3, k = (B.y + jtGearLift() - A.y)/min(B.y - A.y, -1e-3);
  mat3 D = show*mat3(1.0, 0.0, 0.0,  0.0, k, 0.0,  0.0, 0.0, 1.0);
  Pose X; X.R = S*D; X.T = S*(A - D*A); return X;
}
Pose jtWheelPose(vec3 C, mat3 S){
  float show = gPS.x > 0.06 ? 1.0 : 1e-3;
  Pose X; X.R = S*mat3(show); X.T = S*(C + vec3(0.0, jtGearLift(), 0.0) - show*C); return X;
}
// a bay's door (hinged as the light aircraft's vertical bays' are: gearPartPose)
Pose jtDoorPose(vec3 c, vec2 h, float s, mat3 S){
  Pose X; X.R = S*partMirror(-s)*partRxy(-gearDoorAngle()); X.T = S*(c + vec3(s*h.x, 0.0, 0.0)); return X;
}
// the XR-30's surfaces, nozzles and gear
Pose jtPartPose(int k, vec2 sd){
  float cPitch = gCtl.x, cRoll = gCtl.y, cYaw = gCtl.z;
  Pose X;
  if (k >= PT_JT_NOZZLE) {
    vec4 G0 = gM[18]; float gh = gM[19].x;
    mat3 S = partMirror(sd.x);
    if (k == PT_JT_NOZZLE) { X.R = S*partRyz(gFlame.z); X.T = S*vec3(0.82, -0.12, 7.75); }   // (about its front edge, by the vectoring angle)
    else if (k == PT_JT_LEGM) X = jtStrutPose(vec3(G0.x*0.8, -0.3, G0.z), vec3(G0.x - 0.1, -gh + 0.43, G0.z), S);
    else if (k == PT_JT_WHEELM) X = jtWheelPose(vec3(G0.x, -gh + 0.38, G0.z), S);
    else if (k == PT_JT_LEGN) X = jtStrutPose(vec3(0.0, -0.35, G0.w), vec3(0.0, -gh + 0.43, G0.w), mat3(1.0));
    else if (k == PT_JT_WHEELN) X = jtWheelPose(vec3(0.0, -gh + 0.33, G0.w), mat3(1.0));
    else if (k == PT_JT_DOORM) X = jtDoorPose(jtBayC(false), JT_MAIN_BAY_H, sd.y, S);
    else X = jtDoorPose(jtBayC(true), JT_NOSE_BAY_H, sd.y, mat3(1.0));
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
    res = vec2(min(sdCapsule(l, vec3(0.0), vec3(0.0, 0.11, -0.01), 0.016), sdRoundBox(l - vec3(0.0, 0.15, -0.01), vec3(0.021, 0.042, 0.027), 0.015)), 47.0);
  } else if (k == PT_JET_THR) res = vec2(sdRoundBox(l, vec3(0.03, 0.04, 0.05), 0.02), 47.0);
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
  } else if (k == PT_ELEVATOR) {
    vec4 H0 = gM[12], H1 = gM[13];
    float hs = l.x, ht = l.y - (H1.x + hs*H1.z), hc = l.z - H1.y;
    res = vec2(sdSurface(hs, hc, ht, H0.x, H0.y, H0.z, H0.w, 0.1, 0.68, 0.12, H0.x*0.98, 0.0, 0.0), 3.0);
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

