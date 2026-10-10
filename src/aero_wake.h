// Solace Express - the airframe's wake: the vortices its surfaces trail and its engines' wash
//
// Each step the strip model's circulation (aero.h AeroOut::gam) becomes the vortices the surfaces leave behind them.
// Wherever the circulation changes along a surface's span, the change leaves the trailing edge as vorticity
// (lifting-line theory), and each run of it turning one way is lumped into one line vortex at its centroid. So a
// clean wing trails a vortex from near each tip, rolled up about pi/4 of the span apart. Flaps add a pair at their
// outer edges, turning the same way as the tips'. An aileron moves load to one side and the tips' vortices with it.
// The tailplane trails a weaker pair the other way round, and a fin trails one in a sideslip. Propellers blow a
// slipstream that turns with them; jets a hot exhaust. The fuselage pushes the air aside (a line of sources, slender
// body theory).
// From these come the air's motion round the aircraft and in its wake: the cloud wisps it flies through, its tips'
// vapour, and the channel it leaves in a cloud (game.cpp).
// Body axes as the models use them: x right, y up, z aft (forward is -z), from the model's origin.
#pragma once
#include "common.h"

struct AeroGeom; struct AeroIn; struct AeroOut;

struct AeroWake {
  static const int kMaxVortex = 10;
  // a trailing vortex: where it leaves the trailing edge, its circulation about the aft direction (m^2/s; seen from
  // behind, + turns the air anticlockwise: the right tip's), the surface it comes from (AeroSurfaceKind) and its chain
  // (vortices of one chain are joined by the bound vortex along the surface between them)
  struct Vortex { vec3 r; float gam; int surf, chain; };
  // an engine's wash: where it leaves, its radius (m), its speed over the air (m/s), a propeller's swirl at the edge of
  // its slipstream (m/s, + turning as the right tip's vortex does) and whether it is a jet's hot exhaust
  struct Jet { vec3 r; float R, dv, swirl; bool hot; };
  Vortex v[kMaxVortex]; int nv = 0;
  Jet j[4]; int nj = 0;
  vec3 aft = vec3(0, 0, 1);   // the way the air streams past (body)
  float V = 0;                // the airspeed (m/s)
  // the wing's vortices as the pair they roll up into: their circulation (m^2/s), spacing (m), the height and station of
  // the pair's centre (body), the span (m) and the cores' radius (m)
  float gamPair = 0, b0 = 0, pairY = 0, pairZ = 0, span = 0, core = 0.3f;
};

// from the strip model's evaluation this step (the wing's strips, the tails', the fins'; the engines' thrust and power)
void aeroWakeBuild(const AeroGeom& g, const AeroIn& in, const AeroOut& out, AeroWake& w);
// the research jets on fly-by-wire (no strips): an elliptically loaded wing's pair from the lift, and their exhaust
void aeroWakeElliptic(float lift, float rho, float V, float span, float wingY, float wingZ, int engines, float thrust, float tailZ, AeroWake& w);
// the air's velocity at p (body) set going by the airframe, relative to the still air (body axes): the trailing vortices
// as straight lines aft from where they leave, the bound vortices between them, the engines' wash and (with g) the
// fuselage's displacement. Good near the airframe (within a span or two behind it: in a turn its vortices curve away)
vec3 aeroWakeInduced(const AeroWake& w, const AeroGeom* g, vec3 p);
// the main pair's sinking speed (m/s): each vortex carried down by the other's field
inline float aeroWakeSink(const AeroWake& w) { return w.b0 > 0.1f ? fabsf(w.gamPair) / (2.f * PI * w.b0) : 0.f; }

// the air a vortex has caught behind the aircraft, turning round its core: the core (world), the vortex's axis (unit,
// aft), its circulation (m^2/s, about the axis), its core radius (m) and the pair's sinking velocity (world, m/s).
// Faster near the core, slower further out (a Lamb-Oseen vortex), the core carried down with the pair.
struct AirSwirl { vec3 c, ax, sink; float gam = 0, core = 0.3f; };
// catches the point p (world) in the nearer of the pair's vortices if it is close enough to one (within about the
// pair's spacing); pos and q place the aircraft. False when no vortex has it.
bool aeroWakeCatch(const AeroWake& w, vec3 pos, const quat& q, vec3 p, AirSwirl& s);
// moves p one step round its vortex (the core moves on with the pair; the air drifts with the wind outside this)
vec3 aeroSwirlStep(AirSwirl& s, vec3 p, float dt);
