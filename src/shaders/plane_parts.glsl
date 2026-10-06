//! kPlaneParts
//! The rigid moving parts: the light aircraft's flaps, ailerons, elevators and rudder, and in the cockpits the yokes, the rudder pedals, the throttle knob or levers and the flap lever (the
//! light aircraft), the side sticks and throttles (the XR-30 and the XR-40) and the XR-40's pedals. Each is a solid
//! piece that only slides or turns with a control, so it has its own shape in its own frame (partField) and a pose
//! from the controls (partPose: body = R*local + T). The aircraft's field places each part by its pose
//! (plane_sdf.glsl, wraith_cockpit_sdf.glsl); the mesh bake (aircraft_mesh.cpp) leaves the parts out of the airframe
//! (gPartMode -2) and bakes each part alone in its own frame (gPartMode = the part), and the raster passes draw each
//! part's mesh at the pose a small pass computes once a frame from these same functions (kPartPoseFS) - the march
//! and the ray tracer see exactly the shapes the meshes show.
const int PT_YOKE_SHAFT = 0, PT_YOKE_WHEEL = 1, PT_PEDAL = 2, PT_THR_KNOB = 3, PT_THR_LEVER = 4, PT_FLAP_LEVER = 5,
          PT_JET_STICK = 6, PT_JET_THR = 7, PT_WR_STICK = 8, PT_WR_THR = 9, PT_WR_PEDAL = 10,
          PT_FLAP = 11, PT_AILERON = 12, PT_ELEVATOR = 13, PT_RUDDER = 14;   // (the light aircraft's control surfaces)
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
// sd: the instance (x: which seat or which side, -1 or 1; y: the left or right pedal of the pair)
Pose partPose(int k, vec2 sd){
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
// each part's shape in its own frame: distance and material id
vec2 partField(int k, vec3 l){
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
    float fus0 = 0.55*gM[0].w, flapEnd = span*W2.w, ailEnd = span*0.94;
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
// a part in the whole aircraft's field: at its pose, or left out (the static bake)
vec2 partAt(vec2 res, int k, vec2 sd, vec3 p){
  if (gPartMode != -1) return res;
  Pose X = partPose(k, sd);
  return opU(res, partField(k, transpose(X.R)*(p - X.T)));
}
