// Solace Express - aerodynamics from each airframe's geometry
//
// The drag is built up component by component (fuselage, wing, tails, nacelles, struts, gear, cooling, leakage) from
// the shape in the spec: wetted areas, fineness and thickness ratios give the form factors, and the skin friction comes
// from the Reynolds number at the speed and air density of the moment, so the drag of each part changes the way a real
// one does with speed, altitude and size. The lift slope and the span efficiency come from the wing's aspect ratio and
// the fuselage width, and the wave drag from the critical Mach number of the wing section. Weight enters through the
// lift the wing has to make (induced drag). Nothing is tuned to hit a published cruise speed: that comes out.
#pragma once
#include "common.h"

struct AircraftSpec;

// The International Standard Atmosphere (ISO 2533): the troposphere's lapse of 6.5 K per km to 11 km, isothermal to
// 20 km, then 1 K per km. Temperature (K), pressure (Pa), density (kg/m^3), speed of sound (m/s) and dynamic
// viscosity (Pa s, Sutherland) at a geometric height (m; below sea level it extrapolates)
struct Atmosphere { float T, p, rho, a, mu; };
Atmosphere isa(float h);
inline float isaDensity(float h) { return isa(h).rho; }
// the airspeed an airspeed indicator shows (calibrated: the pitot's impact pressure as at sea level, subsonic and,
// past Mach 1, behind the probe's shock), from the true airspeed (m/s) in that air
float calibratedAirspeed(float tas, const Atmosphere& at);

struct AeroModel {
  struct Part { const char* name; float swet, len, ffq, laminar; };   // wetted area, reference length, form factor x interference
  Part parts[8]; int nParts = 0;
  float extraDq = 0;        // drag areas that don't scale with skin friction: struts, cooling, protuberances (m^2)
  float gearDq = 0;         // gear down drag area (m^2)
  float misc = 0;           // leakage and excrescences, fraction of the friction drag
  float tc = 0.15f;         // wing section thickness ratio
  float korn = 0.87f;       // Korn factor of the section (0.87 conventional, ~0.95 supercritical)
  float e = 0.8f;           // span efficiency (Oswald)
  float AR = 7, exposed = 1, fuseFactor = 1;   // aspect ratio, lift-slope factors (exposed area, fuselage lift)
  float sideArea = 1, planArea = 1;            // body side / plan areas for crossflow drag (m^2)
  float roughness = 1e-5f;  // equivalent sand-grain roughness (m)
};

// the aircraft's aerodynamic model (built once per type)
const AeroModel& aeroModel(const AircraftSpec& s);
// zero-lift drag coefficient (on the wing area) at this true airspeed (m/s), air density (kg/m^3) and Mach number,
// clean (no gear, no flaps)
float aeroCD0(const AeroModel& a, const AircraftSpec& s, float V, float rho, float mach);
// the same drag as areas (D/q, m^2) by part: [0] the wing, [1] the horizontal tail (or canard), [2] the fin(s), [3] the
// rest (fuselage, nacelles, struts, cooling, leaks) - the strip model spreads the first three over their strips
void aeroDragAreas(const AeroModel& a, const AircraftSpec& s, float V, float rho, float mu, float mach, float out[4]);
// lift-curve slope (per rad) at this Mach number
float aeroCLa(const AeroModel& a, float mach);
// wave drag of the wing section at this Mach number and lift coefficient
float aeroWave(const AeroModel& a, float mach, float CL);

// ---------------------------------------------------------------- the strip model (aero.cpp aeroForces)
// The airframe as it is drawn (models.cpp): every lifting surface cut into spanwise strips - the wing, the horizontal
// tail (or the canard), the fin(s) - each with its own position, chord, sweep, dihedral and twist, and the control
// surfaces where the model has them (the spans, chords and deflections the drawn surfaces move by). Each step every
// strip meets its own air: the aircraft's motion and rotation through it, the gusts across the airframe, the
// propellers' slipstream and its swirl, the wing's downwash at the tail. Its lift comes from its angle of attack
// through the surface's lift slope (aspect ratio, sweep, Mach, the ground), the spanwise loading of its planform, and
// a stall of its own (separation that lags the angle and recovers late); its drag from the drag build-up above, the
// lift it makes and the deflection of its control surface. The fuselage adds its slender-body moments and its
// crossflow drag, the engines their thrust along their own lines, a propeller its torque, P-factor and gyroscopic
// moment. Everything is summed about the centre of gravity: the stability, the damping, the control power, the trim
// changes, the stall and the spin come out of the shape, not out of coefficients.
enum AeroSurfaceKind { AS_WING = 0, AS_TAIL, AS_FIN, AS_CANARD, AS_COUNT };
enum AeroControlKind { AC_NONE = 0, AC_AIL, AC_FLAP, AC_ELEV, AC_RUD };
struct AeroStrip {
  vec3 r;                 // the quarter-chord point (body, from the model's origin)
  vec3 c, n, sp;          // unit vectors: chordwise (leading to trailing edge), normal (lift positive towards), spanwise (outboard)
  float chord = 1, area = 0;
  float twist = 0;        // geometric twist from the root (rad, + nose up)
  float load = 1;         // the strip's share of the surface's lift coefficient (Schrenk: planform and ellipse averaged)
  float clMax = 1.4f;     // section maximum lift coefficient
  float cm0 = 0;          // section moment about the quarter chord (camber)
  float bodyF = 1;        // its lift where the fuselage carries it through
  float cf = 0;           // control surface chord fraction
  float cover = 0;        // the share of the strip's span it covers
  float side = 0;         // -1 left, 1 right, 0 centre
  float cosSw = 1;        // the cosine of its quarter-chord line's sweep (sp follows that line; c is square to it)
  float fy = 0, fhw = 0, fhh = 0;   // the fuselage's section at the strip's station: centre height, half width and height (0: none)
  int surf = AS_WING, ctl = AC_NONE;
};
struct AeroSurface { int first = 0, count = 0; float S = 0, b = 0, AR = 1, e = 0.8f, sweep = 0; vec3 ac; };
struct AeroEngine { vec3 pos, fwd; float R = 0, Ip = 0, rot = 1; bool prop = false; };
struct AeroSegment { float z, len, area, dA, w, h, y; };   // the fuselage cut along its length
struct AeroGeom {
  static const int kMaxStrips = 64;
  AeroStrip st[kMaxStrips]; int nSt = 0;
  AeroSurface surf[AS_COUNT];
  AeroSegment seg[16]; int nSeg = 0; float fusVol = 0;
  AeroEngine eng[4]; int nEng = 0;
  vec3 cg;                // the centre of gravity at the reference loading (body, from the model's origin)
  vec3 bodyDragAt, gearAt;
  float wingInc = 0;      // the wing root's angle above its zero lift (rad: what lifts it at zero angle of attack)
  float tailInc = 0;      // the horizontal tail's (or canard's) setting (rad): the type trims hands-off at cruise
  float ailRig = 0, rudRig = 0;   // the factory rigging (control units): the aileron tab and the rudder offset that cancel
                                  // the propeller's torque and its slipstream's swirl at cruise (drawn with the surfaces)
  float glideLD[2] = {10.f, 8.f};   // the best glide ratio with the engines stopped (a single's propeller windmilling, a twin's
                                    // feathered): gear up, gear down
  float betaToAil = 0;    // aileron per radian of sideslip (a stability augmenter's): where the wing's own dihedral effect is
                          // too weak or turned round (forward sweep), it rolls away from the sideslip as a dihedral wing would
  float clMaxTrim[2] = {9.f, 9.f};   // the highest lift coefficient it can be trimmed to, clean and with its flaps (a
                                     // canard's stall can come first; 9: the wing's own stall)
  float flapMax = 1;      // the most flap the elevator trims on the approach: the flaps stop there (a canard's, mostly)
  float flapA = 0;        // the flaps' change of the zero-lift angle on their strips at full deflection (rad)
  float flapCdK = 0.4f;   // the flapped strips' drag per (flapA x deflection)^2: the type's full-flap drag (its profile drag
                          // and the induced drag of the lift crowded inboard)
  float tailArm = 1, kEps = 1, etaTail = 0.92f;   // wing to tail (m), the downwash's share there, the tail's dynamic pressure ratio
  float MAC = 1, xNP = 0, staticMargin = 0;       // mean aerodynamic chord (m), the neutral point (body z) and the margin at the CG
  float wingArea = 0;     // the drawn wing's area (m^2)
  float trimV = 0, trimAlpha = 0;   // the trim it was set for: true airspeed (m/s, 1500 m, reference weight), angle of attack (rad)
};
// built once per type from its model (special 0 types; the XR-30 and XR-40 fly their own fly-by-wire model)
const AeroGeom& aeroGeom(const AircraftSpec& s);
// the highest lift coefficient it flies steady at, clean (flaps 0) or with its flaps out as far as they go (1): the
// wing's maximum and the flaps', unless the controls run out first (a canard's stall)
float aeroCLmaxFlown(const AircraftSpec& s, int flaps);
// what the aerodynamics remember between steps: the strips' separation, the downwash on its way to the tail
struct AeroMem { float f[AeroGeom::kMaxStrips]; float eps = 0, clw = 0; bool init = false; };
struct AeroIn {
  vec3 va, w;             // body: the aircraft's velocity through the air at the model's origin, its rotation (rad/s)
  vec3 gx, gy, gz;        // body: the rows of the gusts' gradient across the airframe (the air's velocity change per metre)
  float rho = 1.225f, a = 340.3f, mu = 1.79e-5f, agl = 1e4f;   // (agl: the model origin's height over the ground)
  float pitch = 0, roll = 0, yaw = 0, trim = 0, gear = 0, ice = 0;
  float flapL = 0, flapR = 0;   // each side's flaps, 0..1 (a flap that stuck holds its side)
  float thrust[4] = {}, power[4] = {}, omega[4] = {};   // per engine: thrust (N), shaft power (W), propeller speed (rad/s)
  bool dead[4] = {};      // an engine that isn't turning its propeller over (it windmills)
  bool steady = false;    // the separation at its static value (the calibration; no memory)
};
struct AeroOut { vec3 F, M; float alpha = 0, beta = 0, CLw = 0, stall = 0, qbar = 0; };   // (stall: the warner, 0..1)
// the forces (body) and moments (about the centre of gravity, g.cg) on the airframe this step
void aeroForces(const AeroGeom& g, const AircraftSpec& s, const AeroIn& in, AeroMem& mem, float dt, AeroOut& out);

