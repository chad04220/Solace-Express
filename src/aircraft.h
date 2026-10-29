// Somnium Volandi - aircraft specs and flight dynamics
#pragma once
#include "common.h"
#include "world.h"
#include "wheel_motion.h"
#include "weather.h"
#include "aero.h"
#include "aero_wake.h"

enum EngineType { ENG_PISTON = 0, ENG_TURBOPROP, ENG_JET };
enum License { LIC_STUDENT = 0, LIC_PPL, LIC_CPL, LIC_ATP };
inline const char* licenseName(int l) { static const char* n[] = {"Student Permit", "Private Pilot (PPL)", "Commercial Pilot (CPL)", "Airline Transport (ATP)"}; return n[l]; }

class Scenery;

// One airframe as it is fitted (docs/HANGAR.md): floats in place of its wheels, and the mechanic's kits, each a bit of
// kits. A fitted airframe flies as a copy of its type's row with the changes made (fitted, below); a stock one is the
// row itself, so everything keyed by the roster stays as it was.
enum Kit { KIT_ENGINE = 0, KIT_PROP, KIT_AERO, KIT_LIGHT, KIT_TANKS, KIT_STOL, KIT_COUNT };
struct Fit {
  bool floats = false; uint32_t kits = 0;
  bool stock() const { return !floats && !kits; }
  bool has(int kit) const { return (kits >> kit) & 1u; }
  bool operator==(const Fit& o) const { return floats == o.floats && kits == o.kits; }
  bool operator!=(const Fit& o) const { return !(*this == o); }
};

struct AircraftSpec {
  const char* id; const char* name; const char* role;
  int engineType, engines, cylinders, blades;
  float idleRpm, maxRpm;       // prop rpm (piston) / N1 % scale for turbines
  float emptyMass, maxFuel, cargoKg; int pax;
  float wingArea, span, chord;
  float CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, oswald;
  float power;                 // W per engine (prop) or N thrust per engine (jet)
  float v0;                    // static-thrust knee speed for props (m/s)
  float vr, vref, cruise;      // rotate, approach, cruise speeds (m/s)
  float rangeKm, runwayM;      // nominal range (jets: efficient cruise, no reserve) and sea-level runway need
  bool roughOK, taildragger, retract;
  float Ixx, Iyy, Izz;         // roll, pitch, yaw inertia (kg m^2)
  float elevPow, ailPow, rudPow;
  int license; int price; int rentFee;  // rentFee 0 = not rentable
  // visual model
  float fusLen, fusRad, wingY, wingZ; int engLayout, tail;
  vec3 colBase, colStripe;
  int special = 0;             // 1 = XR-30 research jet: fly-by-wire, pitch thrust vectoring; 2 = XR-40 Somnus (both consume fuel);
                               // 3 = XR-50 Morpheus: swing wing, combined-cycle engines and a rocket, VTOL (aircraft_morpheus.cpp)
                               // 4 = XR-60 Phantasos: a plasma ring drive, an MHD scramjet, field lift and torque (aircraft_xr60.cpp)
  // the research craft's performance tier (0: a conventional type). Below 1 the type is held just under the sound
  // barrier by its drag rise; above 1 the engines have reheat and the drag rise is the research jets'. gPos / gNeg are
  // the structural limits (0: the type's defaults); the airframe takes a sustained overstress before it lets go
  float designMach = 0, gPos = 0, gNeg = 0;
  // Learn the full passenger/cargo envelope and retain the published runway minimum.
  // Existing types keep their calibrated reference loading until separately revalidated.
  bool fullCabinEnvelope = false;
  float gearHeightM = 0;       // positive: authored wheel contact height; 0: derive from legacy geometry
  float takeoffFlap = 0.2f;    // recommended take-off setting; taildraggers retain their 0.3 default
  float gearTrackM = 0;       // positive: main-wheel half-track; 0: derive from legacy geometry
  // Jet fuel model: kg / (N s), sea-level static dry / reheat TSFC. Zero preserves
  // the legacy piston/turboprop model. These are engineering assumptions, not engine certificates.
  float jetDryTsfc = 0, jetWetTsfc = 0;
  float fuelCruiseAlt = 0, fuelCruiseTas = 0, fuelCruiseSpool = 0;
  float maxTakeoffMass = 0;     // explicit kg; zero preserves the legacy payload-based envelope
  // amphibious floats (docs/WINGS_OVER_WATER.md): it floats, takes off and lands on the water, and its wheels (retract)
  // fold up into the floats; floatStations sets them out, and its model row's gear type is 5 to match
  bool floats = false;
  // a fitted airframe (Fit): the copy's type - its roster row (typeOf) - and its fit; the kits' effects the aerodynamics
  // and the engines apply: the airframe's parasite drag, a propeller's efficiency
  int fittedFrom = -1; Fit fit;
  float dragFactor = 1.f, propFactor = 1.f;
  // the fuel the airframe was designed and learned at: its trim, its rigging, the weights its envelope and its runway
  // figures were measured at, a legacy type's weight limit. maxFuel is what its tanks hold - the real figure since
  // v4.1, when the islands' scaled-down tanks (these) gave way to real ones; the flight stays as it was designed
  float designFuel() const;
  float maxFuelForPayload(float payloadKg) const { return std::max(0.f, std::min(maxFuel, maxMass() - emptyMass - std::max(0.f, payloadKg))); }
  // (0: a light aircraft's normal category, +3.8 / -1.52 flaps up; every type in the table sets its own)
  float gLimitPos() const { return gPos > 0 ? gPos : 3.8f; }
  float gLimitNeg() const { return gNeg < 0 ? gNeg : -1.52f; }
  // the runway it needs: the longer of its learned take-off and landing distances at full weight (Plane::perf), 15% to
  // spare, longer with the field's elevation (runwayM until the type has been learned, and for the research jets)
  float runwayNeeded(float elev) const;
  float waterRunNeeded(float elev) const;   // (a seaplane base's water lane: aircraft_perf.cpp)
  // Maximum take-off mass: jets have an explicit envelope. Legacy non-jets keep the
  // original three-quarter-tank/full-load convention. Full tanks can require less payload.
  float maxMass() const { return maxTakeoffMass > 0 ? maxTakeoffMass : emptyMass + designFuel() * 0.75f + cargoKg + pax * 85.f + 85.f; }
  float fuelPriceBase() const { return engineType == ENG_PISTON ? 2.2f : 1.4f; }   // $ per kg at a hub
};

// whether a type may use a field: its surface (grass and sand only for the short-field types, gravel and snow for the
// rough-field ones, a seaplane base's water lane for float planes alone) and its length (runwayNeeded; on the water
// waterRunNeeded). The career dispatches by it and the autoland lands by it.
bool surfaceOK(const AircraftSpec& s, int surface);
bool runwayOK(const AircraftSpec& s, const Airport& a);

extern const AircraftSpec kAircraft[];
// the aircraft's registration: "SX-" and three letters, painted on the rear fuselage (shaders.h, uReg) and the
// call sign the towers use for it (Game::updateAtc)
inline std::string registrationOf(const AircraftSpec& s) {
  uint32_t h = 2166136261u; for (const char* c = s.id; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  std::string r = "SX-";
  for (int i = 0; i < 3; i++) { h = h * 1664525u + 1013904223u; r += (char)('A' + (h >> 8) % 26); }
  return r;
}
static const int kWavecrest = 8;       // the Wavecrest C6: the only type with its own cabin trim in the field (plane_sdf.glsl)
// Stable aircraft identities: append new types; never renumber existing saves, diagnostics or shader models.
// Research craft are reachable only from the research terminal, independent of their flight-model special flag.
static const int kVigil = 9;     // XR-10 Vigil: a conventional twin jet (special 0, the generic field and flight model)
static const int kResearchJet = 10; // XR-30 Nocturne
static const int kReverie = 11;      // XR-20 Reverie: forward-swept systems demonstrator (special 0; the generic field)
static const int kSomnus = 12;      // XR-40 Somnus stealth aerobatic research craft
static constexpr int kThistledown = 13; // Thistledown L4: four-seat piston tourer
static constexpr int kGalleon = 14;    // Galleon 180: twin-underwing transport
static constexpr int kKittiwake = 15;  // Kittiwake A9: single-turboprop amphibian on floats (docs/WINGS_OVER_WATER.md)
static constexpr int kMorpheus = 16;   // XR-50 Morpheus: variable-sweep hypersonic VTOL research craft (special 3, engine code 7: docs/XR50.md)
static constexpr int kPhantasos = 17;  // XR-60 Phantasos: a plasma-driven remotely piloted hypersonic craft (special 4, engine code 8: docs/XR60.md)
static constexpr int kAircraftCount = 18; // all aircraft, including research
static constexpr int kResearchAircraft[] = {kVigil, kReverie, kResearchJet, kSomnus, kMorpheus, kPhantasos};
static constexpr int kCareerAircraft[] = {0, 1, 2, 3, 4, 5, 6, 7, kWavecrest, kThistledown, kGalleon, kKittiwake};
static constexpr int kNumAircraft = sizeof(kCareerAircraft) / sizeof(kCareerAircraft[0]); // career roster count, never an index bound
inline constexpr bool validAircraft(int spec) { return spec >= 0 && spec < kAircraftCount; }
inline constexpr int careerRowFor(int spec) {
  for (int row = 0; row < kNumAircraft; ++row) if (kCareerAircraft[row] == spec) return row;
  return -1;
}
inline constexpr bool isCareerAircraft(int spec) { return careerRowFor(spec) >= 0; }
inline constexpr bool isResearchAircraft(int spec) {
  for (int research : kResearchAircraft) if (research == spec) return true;
  return false;
}
inline constexpr int careerSpecAt(int row) { return row >= 0 && row < kNumAircraft ? kCareerAircraft[row] : kCareerAircraft[0]; }
inline constexpr int validCareerSelection(int spec) { return isCareerAircraft(spec) ? spec : kCareerAircraft[0]; }
inline float performanceReferencePayload(const AircraftSpec& s) {
  return s.fullCabinEnvelope ? 0.5f * (s.cargoKg + s.pax * 85.f + 85.f) : 150.f;
}
// What a type burns in cruise (kg/s: Plane::nominalFuelFlowKgS), the minutes a quantity of fuel lasts at it, and the fuel
// a flight is planned with: the trip with a tenth to spare and the reserve the rules ask - 30 minutes of cruise, 45 for a
// jet - never more than the tanks hold
float cruiseBurnKgS(const AircraftSpec& s);
inline float cruiseMinutes(const AircraftSpec& s, float fuelKg) { return std::max(0.f, fuelKg) / std::max(cruiseBurnKgS(s), 1e-6f) / 60.f; }
float plannedFuelKg(const AircraftSpec& s, float tripKg);
float fuelForKm(const AircraftSpec& s, float km);   // the fuel for so many kilometres of cruise (no reserve), never more than the tanks
// The roster row a spec is (a fitted copy's: its type's), and a type as one airframe of it is fitted: the row itself for
// a stock one, else a copy with the floats and the kits' changes made - one for each fit, made once and never moved
// (airframe_fit.cpp). Which types take floats (a high wing, piston or turboprop, light enough: the Kittiwake has its own)
// and which take each kit
int typeOf(const AircraftSpec* s);   // (-1: none - a copy of no row)
const AircraftSpec& fitted(int type, Fit fit);
bool floatKitFor(int type);
bool kitFor(int type, int kit);
const char* kitName(int kit, const AircraftSpec& type);    // the shop's names and what each does, as the type has it
const char* kitBlurb(int kit, const AircraftSpec& type);
// XR-40 thruster pods (body coords, +z aft): front left, front right, rear left, rear right pivot points
static const vec3 kSomnusPods[4] = {vec3(-2.35f, -0.08f, -3.3f), vec3(2.35f, -0.08f, -3.3f), vec3(-2.75f, 0.05f, 3.45f), vec3(2.75f, 0.05f, 3.45f)};
// XR-50 Morpheus (docs/XR50.md; body coords, +z aft): where its forces act, the same for the flight model
// (aircraft_morpheus.cpp) and the drawn airframe (plane_sdf.glsl mapMorpheus): the right swing wing's pivot, the lift
// fan behind the cockpit, the right nozzle's swivel, the right roll post, the rocket's exit; the wing's sweep, forward
// (take-off, landing, the hover) to aft (supersonic and beyond); the rocket's oxidizer when full (kg) and its thrust in
// vacuum (N)
static const vec3 kMorpheusPivot(2.1f, -0.06f, -0.4f), kMorpheusFan(0.f, 0.05f, -5.f), kMorpheusNozzle(0.78f, -0.02f, 7.4f);
static const vec3 kMorpheusPost(2.65f, -0.36f, 1.4f), kMorpheusRocket(0.f, 0.28f, 8.95f);
static const float kMorpheusSweepMin = 16.f * DEG, kMorpheusSweepMax = 68.f * DEG, kMorpheusOxidizer = 3400.f, kMorpheusRocketVac = 1620000.f;
// the leading edges' temperature limits (K): the warning, and past the other the carbon-carbon starts to burn away
static const float kMorpheusSkinWarn = 1823.f, kMorpheusSkinLimit = 1923.f;
// its lamps (body coords): the right wingtip's, on the swing wing where it is at a sweep; the tail light on the boat
// tail's end over the rocket's bell, the beacon on its top behind the lift fan, the left landing light in the glove's
// leading edge
vec3 morpheusWingTip(float sweep);
static const vec3 kMorpheusTail(0.f, 0.53f, 8.38f), kMorpheusBeacon(0.f, 0.93f, -1.4f), kMorpheusLanding(-1.32f, -0.22f, -3.6f);
// Its combined-cycle engines' thrust by mode (N, both engines; tfDry the turbofans' without reheat) at a spool, a true
// airspeed, the air's density and its speed of sound; what they burn making it (kg/s); the thrust it can turn straight
// down in this air, balanced in pitch (N); its span at a sweep (m) (aircraft_morpheus.cpp)
struct MorpheusThrust { float tf = 0, tfDry = 0, ram = 0, scram = 0; };
MorpheusThrust morpheusEngines(const AircraftSpec& s, float spool, float V, float rho, float a);
float morpheusFuelFlow(const AircraftSpec& s, const MorpheusThrust& t, float M, float rho);
float morpheusHoverMax(const AircraftSpec& s, float rho);
float morpheusSpan(float sweep);
float morpheusRocketSeconds(float oxidizerKg, float throttle);   // how long its oxidizer lasts at a throttle (s)
// XR-60 Phantasos (docs/XR60.md; body coords, +z aft): where its forces act, the same for the flight model
// (aircraft_xr60.cpp) and the drawn airframe (xr60_sdf.glsl mapXr60): the ring drive's centre at the tail, the four
// ventral lift emitters (front left, front right, rear left, rear right), the right blade wing's root and its folding
// tip's hinge; the surge bank's seconds when full and its recharge rate (seconds of surge per second, off); the
// skin's temperature limits (K: the warning, and past the other the metamaterial fails); its lamps (the right tip
// blade's lens at the hinge, the tail light over the ring, the beacon on the dorsal ridge, the left landing light in
// the chine)
static const vec3 kXr60Ring(0.f, 0.f, 7.0f), kXr60Emit[4] = {vec3(-1.25f, -0.42f, -2.6f), vec3(1.25f, -0.42f, -2.6f), vec3(-1.45f, -0.40f, 2.6f), vec3(1.45f, -0.40f, 2.6f)};
static const vec3 kXr60Root(1.15f, -0.05f, 1.6f), kXr60TipHinge(4.0f, 0.35f, 1.8f);
static const float kXr60SurgeSeconds = 40.f, kXr60SurgeRecharge = 1.f / 6.f, kXr60SkinWarn = 2623.f, kXr60SkinLimit = 2773.f;
static const vec3 kXr60Tip(6.14f, 0.50f, -0.70f), kXr60Tail(0.f, 1.30f, 7.05f), kXr60Beacon(0.f, 0.72f, -1.6f), kXr60Landing(-0.70f, -0.12f, -4.6f);
// the right tip blade's lens, folded down by f (0 spread .. 1 at 70 deg) about its hinge's chord line
inline vec3 xr60TipLamp(float f) {
  const vec3 l = kXr60Tip - kXr60TipHinge; const float a = -70.f * DEG * f;
  return kXr60TipHinge + vec3(l.x * cosf(a) - l.y * sinf(a), l.x * sinf(a) + l.y * cosf(a), l.z);
}
// Its drive by mode (N): the ring's thrust (and the lift emitters' share of it: what the lever takes), the scramjet's,
// at a throttle, a true airspeed, the air's density and its speed of sound, surge or not; what it burns (kg/s); the
// thrust the emitters can turn straight down in this air (N) (aircraft_xr60.cpp)
struct Xr60Thrust { float ring = 0, scram = 0; };
Xr60Thrust xr60Drive(const AircraftSpec& s, float throttle, float V, float rho, float a, float surge);
float xr60FuelFlow(const AircraftSpec& s, const Xr60Thrust& t);
float xr60HoverMax(const AircraftSpec& s, float rho);

// What an aircraft can do, learned by flying it: Plane::perf() flies a few short test sorties through the flight model
// (once per type, cached) and measures its envelope. The autopilot flies to these numbers, so it pushes every type to
// its own limits and re-learns them by itself if the flight model changes.
// Where the wheels touch, in the body frame: one set of numbers for the physics' ground contacts (Plane::step) and the
// drawn model (models.cpp packModel), so an aircraft always stands on the wheels it shows - the mains' half track and
// station, the nose wheel's station, a taildragger's tail wheel (station, height above the mains' contact)
struct GearStations { float track, mainZ, noseZ, tailZ, tailY; };
GearStations gearStations(const AircraftSpec& s);
// A float plane's two floats, in the body frame, the same for the physics' water (Plane::substep) and the drawn model
// (models.cpp packModel): each float's centre line at x = +-x; its keel at the step keelY (below the origin); from its
// bow (bowZ) through the step (stepZ, where the main wheels stand) to its stern (sternZ); its beam and its depth keel to
// deck at the step; and how far the amphibious wheels stand below the keel when they are down
struct FloatStations { float x, keelY, bowZ, stepZ, sternZ, beam, depth, wheelDrop; };
// a float's proportions: its length a share of the fuselage's; then, of its own length, the share ahead of the step,
// the nose wheel's station from the bow, its beam and its depth (plane_sdf.glsl draws it from the same numbers)
static constexpr float kFloatLength = 0.82f, kFloatBowShare = 0.58f, kFloatNoseWheel = 0.16f, kFloatBeam = 0.105f, kFloatDepth = 0.10f;
FloatStations floatStations(const AircraftSpec& s);
// the keel's height above keelY at a body z (the bow's sweep up forward of the step, the afterbody's rise behind it),
// and the float's half beam there (tapering to the bow and the stern)
float floatKeelRise(const FloatStations& f, float z);
float floatHalfBeam(const FloatStations& f, float z);

struct PerfModel {
  float vs1 = 0, vs0 = 0;          // stall speed clean / full flap (m/s), from the wing's CLmax at the test weight
  float vy = 0, roc = 0;           // best-climb speed and the climb rate there at full power (m/s)
  float sinkIdle = 0;              // descent rate gliding at idle, full flap and gear, 1.25 Vs0 (m/s, positive down)
  float rollRate = 0;              // roll rate at full aileron at cruise (rad/s; it scales with airspeed)
  float gPerStick = 0;             // load factor change per unit of elevator at cruise (it scales with dynamic pressure)
  float tG = 0;                    // how long the airframe takes to answer the elevator (s, step to peak g)
  float qPerStick = 0, tQ = 0;     // pitch rate per unit of elevator at cruise (rad/s, scales with airspeed), time to it (s)
  float tRoll = 0;                 // roll-mode time constant (s, full aileron to 63% of the roll rate)
  float gPull = 0;                 // load factor a full aft stick reaches at cruise (before any limiter)
  float gLimit = 0, gNeg = 0;      // structural limits (positive, negative)
  float gUse = 0;                  // what the pilot may pull: within the structure, the stall and its pull authority
  float rearPitch = 0;             // the nose-up attitude a full pull at idle reaches from level flight at 1.7 Vref (deg)
  float bankMax = 0;               // steepest bank it may hold in a level turn (deg)
  float cruiseV = 0;               // level true airspeed at 75% power, 1500 m, mid weight (m/s)
  float toRoll = 0, ldgRoll = 0;   // at full weight, sea level, no wind: ground roll to lift-off, and landing distance from 15 m (3 deg path at 1.3 Vs0) to a stop on the brakes (m)
};

// The autopilot's live reading of what this aircraft can do now (Plane::apSense, every step it flies): the learned
// envelope (PerfModel: measured once per type, at a test weight in still air at 1200 m) corrected for the weight it
// carries, the air it is in, its configuration and its condition - engines failed or running rough, ice on the wings.
// Every decision the autopilot makes reads these; none asks which aircraft it is.
struct ApEnvelope {
  float mass = 0, wRatio = 1, sigma = 1;  // kg; weight over the test weight; air density over sea level's
  float vs0 = 0, vs1 = 0;                  // stall IAS now, full flap and clean (weight, ice)
  float vApp = 0;                          // the approach reference speed now (IAS): the type's, for this weight and ice
  float thrustFrac = 1;                    // full thrust now over the tests': engine health, the air
  float climb = 0;                         // best steady climb rate now at full power (m/s; negative: it can't hold height)
  float climbPlan = 0;                     // a climb it holds for minutes at working speeds (planning, the go-around)
  float descentMax = 0;                    // steepest descent on the approach without gaining speed (m/s)
  float glideMax = 0;                      // steepest glidepath it flies (deg): its idle glide, and its engines' response
  float spool = 0;                         // how long its engines take to answer the throttle (s)
  float gUse = 0;                          // the load factor it may pull (the structure, its pull authority)
  float ldgDist = 0;                       // landing distance from 15 m in still air at this weight, sea level (m)
  bool hover = false;                      // it can hold its weight on its thrust alone
  bool highAlpha = false;                  // strong and agile enough to rear up and shed speed on the final (the belly-up)
  bool agile = false;                      // built for extreme manoeuvres (steeper intercepts)
  bool rateCmd = false;                    // its flight controls take rate commands (fly-by-wire), not surface deflections
  bool flaps = true;                       // it has flaps to fly the approach with
  bool canGoAround = true;                 // it can climb away from a missed approach
};

struct Controls {
  float pitch = 0, roll = 0, yaw = 0;  // -1..1 (pitch +1 = nose up, roll +1 = right, yaw +1 = right)
  float throttle = 0;                  // 0..1
  float trim = 0;                      // -1..1
  float brake = 0;
  float flaps = 0;                     // 0..1 target
  bool gearDown = true;
};

struct FlightEvents {
  bool touchdown = false; float touchdownVs = 0; float touchdownSpeed = 0;
  bool crashed = false; std::string crashReason;
  bool tailStrike = false;
  bool bellyLanding = false;   // the aircraft is skidding on its belly (gear up or stuck) - survivable on a runway at approach speed
};

// What can break on an aircraft (C7). The game rolls them per flight from the aircraft's condition and fires them with
// Plane::fail(); the flight model applies them every step. One bit per kind (FAIL_ENGINE_* per engine index in
// engineHealth), the whole set describes the aircraft's state for the HUD, the audio and the settlement.
enum FailureKind { FAIL_NONE = 0, FAIL_ENGINE_PARTIAL, FAIL_ENGINE_TOTAL, FAIL_ALTERNATOR, FAIL_PITOT, FAIL_GEAR_STUCK, FAIL_FLAP_ASYM, FAIL_ICING, FAIL_COUNT };
inline const char* failureName(int k) { static const char* n[] = {"", "Engine power loss", "Engine failure", "Alternator failure", "Pitot blocked", "Landing gear stuck", "Flap asymmetry", "Airframe icing"}; return n[k]; }
struct Failures {
  float engineHealth[4] = {1, 1, 1, 1};   // per engine: 1 sound, 0.5 running rough at half power, 0 stopped
  bool alternator = false;               // the battery alone feeds the avionics: it runs down in a few minutes
  float battery = 1;                     // 1 full .. 0 flat (avionics dark: no autopilot, no GPS)
  bool pitot = false; float pitotIas = 0, pitotRho = 1.225f;   // blocked: the airspeed indication is frozen at what it read (it then reads like an altimeter)
  int gearStuck = 0;                     // 0 no, 1 stuck up (won't extend), 2 stuck down (won't retract)
  bool flapAsym = false; float flapAt = 0;   // the left flap stopped where it was; the right one still follows the lever (a split: a rolling moment)
  float ice = 0;                         // 0..1 airframe ice: lift lost, drag added (grows in cloud below freezing, melts in warm air)
  bool any() const { for (float h : engineHealth) if (h < 1) return true; return alternator || pitot || gearStuck || flapAsym || ice > 0.05f; }
  bool avionicsDark() const { return alternator && battery <= 0.f; }
  int enginesOut(int n) const { int k = 0; for (int i = 0; i < n && i < 4; i++) if (engineHealth[i] <= 0.f) k++; return k; }
};

class Plane {
public:
  const AircraftSpec* spec = nullptr;
  vec3 pos, vel;          // world
  quat q;                 // body -> world. Body: +x right, +y up, -z forward
  vec3 w;                 // angular velocity (body): x pitch-up, y yaw-left, z roll-left
  Controls ctl;
  float fuel = 0, payload = 0;
  float flaps = 0, gear = 1;  // actual positions (flaps: the right one's, and the left's unless it stopped: flapLeft)
  float flapLeft() const { return fail.flapAsym ? fail.flapAt : flaps; }
  float rpm = 0, n1 = 0;      // engine state (spooled)
  float engineSpool = 0;      // 0..1 actual power fraction
  bool engineRunning = false; float starterTime = 0;
  WheelMotion wheelMotion[3]; // main left, main right, nose/tail; cosmetic simulation state
  bool onGround = false, wasOnGround = false;
  // a float plane on the water: afloat (onGround then too - it is down and steering on its rudders, not flying), how
  // far the floats are in (0 dry .. 1 the whole float down to its deck: on the step at speed little more than the keel),
  // and the chop it rides (m, the sea's local amplitude: sheltered water calm, the open coast rough in the wind)
  bool onWater = false; float floatWet = 0, seaChop = 0;
  // moored (docs/WINGS_OVER_WATER.md): asked for by the parking brake on the water, made once the aircraft is afloat
  // and all but stopped there - two lines, from each float's bow and stern station, held to where they were made fast
  bool moorWanted = false, moored = false; vec3 moorBow, moorStern;
  bool sceneryHits = true;   // collide with trees and buildings (off for the quote's background flight: the scenery isn't thread-safe)
  // the trees and buildings its autopilot plans the final over (apPlan): nullptr the game's (g_scenery); a background
  // flight's own, made on its thread (the game's is the game thread's alone: its chunks are replaced and trimmed there).
  // apTrees false: the ground alone (a plan never flown - the menu's quick estimate - that would otherwise build the
  // scenery round a far field's final on the game thread, some 30 ms of a frame)
  Scenery* obstacles = nullptr;
  bool apTrees = true;
  bool apComfort = false;   // the autopilot flies for passengers (career flights): gentle bank, g, roll and climb; the stick is never limited
  bool apUpset = false;     // slow or steep enough that recovering comes before comfort (apControl)
  bool apPro = false;       // the pilot rework's first stage (docs/PILOT.md), off until it passes the checkride: a professional's envelope (30 deg of bank and 1.3 g in a civil type, up to 60 deg and 2.5 g in a research jet, smooth), the energy law, the ground ahead along its turn, a stabilised final (the gate at 230-460 m, configured and slowed by height, the path loop damped to the airframe's lag); false: the autopilot as it was (pilot_exam: PILOT=new)
  float apStabH() const;   // the height a professional is stabilised on the final by: 150 m (500 ft) in a light aircraft flown by eye, 300 m (1,000 ft) in an airliner or a jet
  bool apEscape = false;    // the path it is on meets the ground within ~25 s: the whole envelope to climb away (a ground-proximity warning; a professional's: along its turn, as firm a pull as clears it)
  float apEscT = 0.f;       // how long it has been climbing away from the ground (the professional's escape is held 3 s at least)
  bool apEdge = false;      // heading out past the chart's edge: a professional's sharpest turn (2.5 g, 60 deg) to come back
  float apBankOk = 25.f;    // the bank a professional has been allowed lately (coming down slowly): beyond it by 15 deg is an upset
  float apNzCmd = 1.f;      // the load factor commanded, eased (the professional's g changes come on smoothly)
  float apSpdEst = 0.f, apThrDemand = 0.5f; vec3 apVelPrev;   // (apThrDemand: the throttle the energy law last asked for, before its stops)   // the energy law's airspeed with the gusts filtered off, and last step's velocity (its inertial acceleration)
  float brakeHold = 0;   // the steady push the parked brakes are holding (N along the nose), learned while held
  float groundRough = 0;      // 0 asphalt .. 1 rough (for audio/vibration)
  float alpha = 0, beta = 0, airspeed = 0, ias = 0, gLoad = 1, stallWarn = 0;
  vec3 windVel;               // current wind incl. gusts
  vec3 windAvg;               // the wind smoothed over a couple of seconds (the HUD's readout: the eddies don't flicker it)
  wxfield::Local wxl;         // the slow parts of the wind here (weather.h: the terrain's lift and sink, thermals, the cloud, the rain), every step
  vec3 wxAir;                 // the air mass's drift since the flight began: the eddies and gusts ride it
  vec3 seamShift;             // where the map's seam brought it back in (world.h WRAP_HALF): what step() moved it by, summed
                              // until the game takes it (Game::followSeam: what it places in the world comes with it)
  vec3 ctlSurf;               // the control surfaces as the physics deflects them (-1..1: x elevator, y ailerons, z rudder): the
                              // stick and trim, the factory rigging and any augmenter - what the renderer draws
  vec3 gustRot;               // the eddies' rotation across the airframe, as body rates (rad/s): x roll right, y pitch up, z yaw right
  float gustBurst = 0;        // the gust burst under way, 0..1 (1: the reported peak)
  AeroMem aeroMem;             // the strip model's memory: each strip's separation, the downwash on its way to the tail (aero.h)
  AeroWake wake;               // the vortices its surfaces trail and its engines' wash, this step (aero_wake.h)
  float density = 1.225f, soundSpeed = 340.3f;   // the air the aircraft is in (the standard atmosphere at its height)
  // ---- autopilot: HOLD (heading / altitude / speed), NAV (to a chosen airport), APPR (approach, flare, rollout)
  enum ApMode { AP_OFF = 0, AP_HOLD, AP_NAV, AP_APPR, AP_STUNT };
  // aerobatic figures the autopilot flies (aircraft_stunt.cpp), each sized to this airframe's envelope
  enum Stunt { STUNT_LOOP = 0, STUNT_ROLL, STUNT_BARREL, STUNT_IMMELMANN, STUNT_SPLIT_S, STUNT_CUBAN, STUNT_WINGOVER, STUNT_COUNT };
  static const char* stuntName(int figure);
  enum ApStage { APS_NAV = 0, APS_FINAL, APS_FLARE, APS_ROLLOUT, APS_GOAROUND, APS_HOVER, APS_BLEED, APS_DOCK };   // (APS_DOCK: taxiing to a seaplane base's dock)
  // (APS_BLEED: the research craft come down the final fast and, a few km out, rear up belly-first into the airflow -
  // throttle closed, nose 70 deg up - to shed the speed, then drop the nose back onto the glidepath)
  int apBleedPhase = 0; bool apBled = false; float apBleedT = 1e9f;   // (pitching up / nose back down; done this approach; s since)
  float apFlareMin = 0;   // the lowest the flare has been (m above the runway): a balloon is measured from it
  float apGroundT = 0;   // s the rollout has had its wheels down without a skip (the nose waits for it)
  bool apOn = false; int apMode = AP_OFF;
  float apHeading = 0, apAlt = 0, apSpeed = 0, apVS = 0; bool apUseVS = false;
  float apPitchI = 0, apRollI = 0, apYawI = 0, apVmcCap = 1, apThrI = 0.5f, apXI = 0, apGamI = 0, apTrimEst = 0, apFlareTau = 0, apFlareVs = 0, apGustAdd = 0;
  int apAirport = -1, apStage = 0, apLeg = 0; float apOutSide = 0;   // (apOutSide: the side of the centreline the outbound leg keeps to, +-1; 0 off that leg)
  bool apTerminalRejoin = false; // keep the missed-approach return at the planned terminal speed
  bool apRev = false; float apStageT = 0, apCruiseAlt = 0, apFinalLen = 8000;
  vec3 apLd, apTd;            // landing direction and touchdown point of the chosen runway end
  vec3 apHoldC; float apHoldR = 1500, apHoldAlt = 0, apIntAlt = 0; int apHoldDir = 1, apTurnDir = 0, apClimbDir = 0; float apGs = 0.0524f, apDrift = 0;   // descent orbit and intercept altitude
  bool apDone = false;        // an autoland just finished (the game sets the parking brake)
  bool apOverrun = false;     // ...and it stopped past the runway's end: over, not a success
  std::string apStatus;       // one-line status for the HUD
  std::string apPlanWhy;   // after apPlan: why that runway end is unsafe ("" safe): too short for the wind, too high to descend onto
  int apHoldFor = -1; float apRetryT = 0;   // a declined autoland's field: circling clear of the ground, tried again every 20 s
  int apWindEvent = 0;      // the wind turned behind it on the final: 1 going around, 2 then planned afresh (the game says so and clears it)
  std::string apDecline;   // set by apEngage when neither end is safe: the autoland is declined (the autopilot holds instead), and why
  void apEngage(int mode, int airport, const Weather& wx);
  void apDockTaxi(float dt);   // after an autoland on a water lane: out round the pontoon's head and in alongside it
  // The tower's go-around (the runway occupied on short final) while it flies an approach: abandoned for a climb-out and
  // a fresh approach. False when it can't climb away (it lands from what it has) or isn't flying one (review FLT-3)
  bool apTowerGoAround();
  float splitSDrop(float V0, float rollRate) const;   // the height a split-S from V0 takes (aircraft_stunt.cpp)
  // start a figure: the autopilot first gets the speed and height it needs (diving or climbing), then flies it and
  // levels off into a hold. Any ground in the way aborts it into a recovery.
  void apStuntBegin(int figure, const Weather& wx);
  void apStuntStop();   // cut the figure short: recover to level flight (or just hold, while still setting up)
  int apStunt = 0, apStuntStep = 0, apStuntDir = 1;   // figure, its step (0 setting up), roll direction
  float apStuntAng = 0, apStuntT = 0, apStuntV = 0, apStuntN = 0, apStuntHdg = 0, apStuntSpeedAfter = 0, apStuntBank0 = 0;
  vec3 apStuntRight0;          // the wing axis when the pull started: a figure keeps it, whatever the attitude
  std::string apStuntAbort;   // why the last figure was cut short ("" if it wasn't)
  // one figure at a time: what was flying before it (the autopilot on or off, its mode and field), restored once the
  // figure has recovered to level flight (apStuntEnded, which the game answers)
  bool apStuntWasOn = false, apStuntEnded = false; int apStuntWasMode = 0, apStuntWasAirport = -1;
  void apDisengage() { apOn = false; apMode = AP_OFF; apUseVS = false; apTerminalRejoin = false; }
  float maxG = 1, minG = 1;
  float overG = 0;   // sustained overstress (grows past the structural limit, decays within it; the airframe fails at 1)
  float flightTime = 0;
  float mach = 0, nozzle = 0;  // research jet: Mach number, thrust-vector nozzle angle 0 (aft) .. 1 (straight down)
  // XR-40: each pod's pitch tilt (rad, 0 = thrust aft, pi/2 = thrust down, incl. vane vectoring), yaw vane (rad),
  // thrust fraction of full boost, fan angle; control-surface deflections (-1..1: pitch, yaw, roll) as allocated
  float podTilt[4] = {0, 0, 0, 0}, podYaw[4] = {0, 0, 0, 0}, podThr[4] = {0, 0, 0, 0}, podVane[4] = {0, 0, 0, 0}, fanAngle = 0;
  vec3 surf;
  // XR-50 (aircraft_morpheus.cpp): the wing's sweep (rad) and its slats and flaps (0..1, run out with the wing forward and
  // slow); the rocket's oxidizer (kg), lit or not, its thrust (N); the air-breathing engines' thrust by mode (turbofan,
  // ramjet, scramjet: N); the leading edges' temperature (K) and how far past their limit they have been (1: burned
  // through); the lift fan's clutch (0..1) and its thrust and the roll posts' (N); the nozzles' swivel down and each
  // one's pitch and yaw vectoring (rad); the reaction jets' commands (pitch, yaw, roll: -1..1); the supercirculation's
  // blowing (0..1: the wing's lift and roll authority raised to 1 + 2 circ times a plain wing's)
  float sweep = 0, hiLift = 0, circ = 0, oxidizer = 0, rocketThr = 0, rocketN = 0; bool rocketLit = false;
  float tbcc[3] = {0, 0, 0}, skinT = 288.f, heatOver = 0, fanClutch = 0, fanN = 0, postN[2] = {0, 0};
  float nzSwivel = 0, nzPitch[2] = {0, 0}, nzYaw[2] = {0, 0}; vec3 rcs;
  float sweepTarget() const;   // the sweep the schedule asks for now (Mach, the lever, the gear)
  // XR-60 (aircraft_xr60.cpp): the ring drive's thrust (N) and the scramjet's; the surge bank (0..1 of its 40 s), lit or
  // not, and the surge it is making (0 off .. 1 full); the plasma sheath (0..1: with the core at power); the lift
  // emitters' thrust (N, each: front left, front right, rear left, rear right) and the lever's share (0..1); the
  // ring's vectoring (rad: pitch, yaw) and the ring's spin angle (rad); the tip blades' fold (0 spread .. 1 down); the
  // field torque's commands (pitch, yaw, roll: -1..1)
  float ringN = 0, scramN = 0, surgeBank = 1, surgeN = 0, sheath = 0, emitN[4] = {0, 0, 0, 0}, emitShare = 0; bool surgeOn = false;
  float ringPitch = 0, ringYaw = 0, ringSpin = 0, tipFold = 0; vec3 field;
  FlightEvents ev;
  Failures fail;
  float iceFeed = 0;   // set by the game each step: 1 in cloud or precipitation below freezing, -1 in clear warm air
  // break something now (FailureKind; the engine index for the engine kinds). Returns false when it can't apply
  // (a jet has no pitot ice? it does; a fixed-gear aircraft has no gear to stick; a research craft never fails)
  bool failNow(int kind, int engine = 0);
  bool glideOnly() const { return spec && fail.enginesOut(spec->engines) >= spec->engines; }   // every engine stopped
  float glideRatio() const;   // best L/D of this airframe, clean (for the HUD's glide range)
  Rng rng;

  void reset(const AircraftSpec* s, vec3 position, float headingDeg, float fuelKg, float payloadKg, bool airborne, float speed = 0);
  void step(float dt, const Weather& wx, float time);
  vec3 forward() const { return q.rotate(vec3(0, 0, -1)); }
  vec3 up() const { return q.rotate(vec3(0, 1, 0)); }
  vec3 right() const { return q.rotate(vec3(1, 0, 0)); }
  float heading() const;   // degrees
  float touchHeight(const Airport& a) const;   // what meets that field under the origin: the wheels, or a float plane's keels on a water lane
  float pitchDeg() const;
  float bankDeg() const;   // positive right wing down
  float agl() const;
  float gearHeight() const;
  float mass() const { return spec->emptyMass + fuel + payload + oxidizer; }
  float fuelFlowMax() const;   // kg/s, all engines, sea-level static maximum (includes reheat)
  float fuelFlowAt(float spool, float tas, float rho = -1.f, float live = 1.f) const; // shared simulation/planning kg/s
  float fuelFlowKgS() const;   // present engine state; zero with engines off / tanks empty
  float nominalFuelFlowKgS() const; // published efficient-cruise reference, all engines
  float nominalRangeKm() const; // remaining fuel, reference cruise, no reserve
  float spoolRate() const;     // how fast the engines follow the throttle (spool fraction per second)
  float thrustAt(float spool, float V, float vf, float rho = -1.f) const;   // all engines' thrust (N) at this spool, airspeed and forward airspeed, in the air it is in (or rho's)
  float liftThrustMax() const; // the thrust it can direct straight down (N): vectored lift
  float rangeLeftKm() const;
  float cd0Value() const { return cd0; }
  // fly-by-wire rate command of the research craft: full-stick pitch and roll rates (rad/s)
  // (the XR-50, flown from the ground with no one aboard, has twice the XR-40's: the rate of a 160 g pull, 4 to 12 rad/s,
  // and 14 rad/s of roll)
  // (the XR-60, three times the XR-50: the rate of a 500 g pull, 8 to 24 rad/s, and 44 rad/s of roll)
  float fbwPitchMax(float V) const {
    V = std::max(V, 1.f);
    if (spec->special == 4) return clampf(500.f * G0 / V, 8.0f, 24.0f);
    if (spec->special == 3) return clampf(160.f * G0 / V, 4.0f, 12.0f);
    return spec->special == 2 ? clampf(80.f * G0 / V, 2.0f, 6.0f) : clampf(66.f * G0 / V, 1.8f, 5.2f);
  }
  static const PerfModel& perf(const AircraftSpec* s);   // learned once per type (aircraft_perf.cpp)
  // a type learned while noting is on (the game's update, which names it as a part of the update): its index and ms
  static void perfNoting(bool on);
  static bool perfLearned(const AircraftSpec* s);
  static std::vector<const AircraftSpec*> perfAirframes();   // the airframes learned at the start: the career types, and each on floats where they fit   // (learned already: perf() would return at once)
  static std::vector<std::pair<int, double>> perfNoted();   // (the notes since the last call)
  // The trees and buildings over both of a field's finals, as apPlan samples them, profiled ahead on a worker
  // (g_sceneryProfiles) for the plans made later in flight: asked for as the field becomes a destination. A plan made
  // before its profile is in samples the game's scenery as it did, building what is missing there
  static void wantFinals(int airport);
  static bool perfLoad(const std::string& path, const std::string& stamp);   // every type's, from a cache of this build's
  static void perfSave(const std::string& path, const std::string& stamp);
  float fbwRollMax(float hover) const { return (spec->special == 2 ? 7.0f : spec->special == 3 ? 14.0f : spec->special == 4 ? 44.0f : 5.5f) * (1.f - 0.6f * hover); }
  // Assisted flying (the game's Flight assists, and always in time warp): the pilot's inputs (-1..1) command a roll rate
  // and a pitch rate; let go, the aircraft holds its bank (wings level from under 10 deg) and its flight path, and the
  // rudder coordinates (assistYaw adds the pilot's own). Flown through the autopilot's inner loops (apRates) inside the
  // airframe's envelope, the nose lowered near the stall; off on the ground and under the autopilot
  bool assistOn = false; float assistRoll = 0, assistPitch = 0, assistYaw = 0;
  float assistBank = 0, assistGamma = 0; bool assistHeld = false;
  ApEnvelope apEnv;            // what it can do now (apSense)
  void apSense();              // read it: the learned envelope, corrected for weight, air, configuration and condition
private:
  double fuelBurnRemainder = 0; // keeps sub-gram jet burn at 240 Hz from rounding away in large tanks
  void substep(float dt, const Weather& wx, float time);
  void apGuidance(float dt);
  void apControl(float dt);
  bool apStuntFly(float dt);   // true when it flew the controls itself this step (false: setting up, normal loops)
  void apRates(float qT, float pT, float rollCap, float nzMin, float nzMax, float dt);   // the shared inner loops
  float apAltGain() const;   // altitude error -> climb rate (1/s), as fast as this airframe's pitch answers at this speed
  float apPathGain(bool approach) const;   // flight-path error -> its rate (1/s): as fast as the pitch answers, slow enough for the path's lag to settle it
  float apProScale() const; float apProG() const; float apProBank() const;   // a professional's envelope for this airframe: 0 civil .. 1 (40 g); its manoeuvring g and bank
  float terrainAround() const;   // the highest ground to keep clear of: under it, ahead along its track, and all round
  float apPitchLag() const;  // how long this airframe's pitch takes to answer at this speed (s)
  float apPathLag() const;   // and its flight path: the pitch's lag, or the wing's in building the lift, the slower (s)
  float apPlan(int airport, bool rev, const Weather& wx, bool commit);
  float apStopNeed(const Airport& a, bool rev, const Weather& wx, float* tw) const;
  void apHover(float dt);
  void apBellyUp(float dt);   // the research craft's speed-shedding pitch-up on the final (APS_BLEED)
  void assistControl(float dt);   // the assisted flying's law (assistOn)
  void somnusThrust(vec3& F, vec3& T, float podThrust, vec3 wd, vec3 Taero, vec3 surfMax, float dt);
  // XR-50 (aircraft_morpheus.cpp): the wing, its high-lift devices and the leading edges' heat; its lift and drag and
  // the airframe's own moments (Taero), the control surfaces' authority (surfMax); false when the heat has destroyed it
  bool morpheusAero(const Atmosphere& atmo, vec3 va, float V, float altAgl, float dt, vec3& F, vec3& Taero, vec3& surfMax);
  // its engines, nozzles, lift fan, roll posts, rocket and reaction jets, and the fly-by-wire that commands them all
  void morpheusThrust(vec3& F, vec3& T, vec3 Taero, vec3 surfMax, float V, float dt);
  // XR-60 (aircraft_xr60.cpp): the sheath and the skin's heat; its lift and drag (the circulation field's lift) and the
  // airframe's own moments, the surfaces' authority; false when the heat has destroyed it
  bool xr60Aero(const Atmosphere& atmo, vec3 va, float V, float altAgl, float dt, vec3& F, vec3& Taero, vec3& surfMax);
  // its ring drive, scramjet, surge, lift emitters and field torque, and the fly-by-wire that commands them all
  void xr60Thrust(vec3& F, vec3& T, vec3 Taero, vec3 surfMax, float V, float dt);
  vec3 gust;
  float cd0 = 0.03f;
  float seaFetchT = -1.f, seaFetch = 0.f;   // (the open water upwind, looked for once a second)
  // this step: a keel met the ground (a beach, the shallows, a runway with the wheels up), its fastest speed over it,
  // and whether it was a runway's
  bool floatGround = false, floatGroundRunway = false; float floatGroundSpeed = 0.f;
  // the floats on the water (or on the ground, wheels up): their forces into Fw (world) and Tw (body, about cg); true
  // when they touch, afloat set when it is the water (substep's ground contacts)
  bool floatContacts(float dt, const Weather& wx, float time, vec3 cg, vec3& Fw, vec3& Tw, bool& afloat);
};
