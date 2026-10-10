// Solace Express - how an airframe comes apart, and how its pieces fly.
//
// The pieces are the aircraft's own components: its propellers, engine nacelles, landing gear legs, wing struts, fin,
// each half of the tail (or canard), each wing, and the fuselage in three - the nose, the centre section and the tail
// cone. Each is a box in the body frame, and a point of the airframe belongs to the first box that holds it, so a
// nacelle comes away from its wing whole, and the wing from the fuselage; whatever no box holds stays with the centre
// section. The boxes are fitted to the surfaces the strip model flies (aero.h): a T-tail's halves at the top of its fin,
// a canard ahead of the wing.
//
// Each piece flies on what it is made of: the strips of its lifting surfaces as flat plates, its fuselage slices as
// bluff bodies, its mass and inertia from its structure, engines and load. The plates' air is the quasi-steady model of
// falling cards and leaves (Andersen, Pesavento & Wang 2005): a lift that follows the angle to the airflow and turns
// round with it, a drag from edge-on to flat-on, the circulation of the plate's own spin (a torn wing autorotates), and
// a centre of pressure that moves back from the quarter chord as the plate turns broadside (it tumbles). Skin panels
// and burning fragments are single plates: they flutter and drift down.
#pragma once
#include "common.h"
#include <vector>

struct AircraftSpec;

enum BreakKind { BK_PROP = 0, BK_NACELLE, BK_GEAR, BK_STRUT, BK_CANARD, BK_TAIL, BK_FIN, BK_WING, BK_NOSE, BK_AFT, BK_CENTRE, BK_COUNT };
static const int kMaxBreakPieces = 16;
struct BreakPiece { vec3 C, H; int kind; int side; };   // box centre and half extents (body), what it is, -1 left 0 centre 1 right

// the components of type s, in the order their boxes claim the airframe (gearDown: the gear is out, 0..1; gearHeight:
// the wheels' bottom below the body origin, m). The centre section is the last, and also has what no box holds.
int breakPieces(const AircraftSpec& s, float gearDown, float gearHeight, BreakPiece out[kMaxBreakPieces]);
// the piece a body point belongs to
int breakOwner(const BreakPiece* p, int n, vec3 b);
const char* breakKindName(int kind);

// ---------------------------------------------------------------- the pieces in the air
struct DebrisPlate { vec3 r, c, n, s; float chord, span; };   // (piece frame, from its centre of mass) quarter-chord point; chordwise, normal, spanwise units
struct DebrisRod { vec3 r; float len, w, h; };                  // a bluff slice along the body's z axis: centre, length, width (x), height (y)
struct DebrisBody {
  float mass = 1;         // kg
  vec3 I = vec3(1);       // principal moments about the body axes through the centre of mass (kg m^2)
  vec3 cg;                // the centre of mass (aircraft body coords: where it was on the whole airframe)
  float blunt = 0;        // the torn ends' area facing along the body axis (m^2)
  vec3 box = vec3(0);     // a piece with no surfaces of its own (a gear leg, a pod): its half extents, flown as a bluff box
  float size = 1;         // its largest extent (m): how fast it can tumble
  std::vector<DebrisPlate> plates; std::vector<DebrisRod> rods;
};
// the bodies of the pieces of type s (mass: what the airframe weighs as it breaks, kg; payload: of it, the load aboard)
void breakBodies(const AircraftSpec& s, const BreakPiece* p, int n, float mass, float payload, DebrisBody out[kMaxBreakPieces]);
// a torn skin panel or a burning fragment: one plate of side `size` m and areal density sigma kg/m^2
DebrisBody debrisPanel(float size, float sigma);
// the air's force (N, world) and moment (N m, world, about the centre of mass) on a body at orientation q moving at v
// with rotation w (rad/s, world) through air of density rho moving at wind
void debrisAir(const DebrisBody& b, const quat& q, vec3 v, vec3 w, vec3 wind, float rho, vec3& F, vec3& M);
// one step of flight under gravity and the air (p: the centre of mass); substepped as finely as the air's grip on the
// body needs (a light panel thrown off at 500 m/s stops in a few hundredths of a second)
void debrisStep(const DebrisBody& b, vec3& p, vec3& v, quat& q, vec3& w, vec3 wind, float rho, float dt);
