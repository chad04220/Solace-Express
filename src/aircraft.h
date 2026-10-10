// Solace Express - aircraft specs and flight dynamics
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
  float rangeKm, runwayM;      // design range (game scale) and runway need at sea level
  bool roughOK, taildragger, retract;
  float Ixx, Iyy, Izz;         // roll, pitch, yaw inertia (kg m^2)
  float elevPow, ailPow, rudPow;
  int license; int price; int rentFee;  // rentFee 0 = not rentable
  // visual model
  float fusLen, fusRad, wingY, wingZ; int engLayout, tail;
  vec3 colBase, colStripe;
  int special = 0;             // 1 = XR-30 research jet: fly-by-wire, pitch thrust vectoring, no fuel burn; 2 = XR-40 Wraith
  // the research craft's performance tier (0: a conventional type). Below 1 the type is held just under the sound
  // barrier by its drag rise; above 1 the engines have reheat and the drag rise is the research jets'. gPos / gNeg are
  // the structural limits (0: the type's defaults); the airframe takes a sustained overstress before it lets go
  float designMach = 0, gPos = 0, gNeg = 0;
  float gLimitPos() const { return gPos > 0 ? gPos : special == 2 ? 90.f : special ? 50.f : 5.8f; }
  float gLimitNeg() const { return gNeg < 0 ? gNeg : special == 2 ? -45.f : special ? -25.f : -3.f; }
  // the runway it needs: the longer of its learned take-off and landing distances at full weight (Plane::perf), 15% to
  // spare, longer with the field's elevation (runwayM until the type has been learned, and for the research jets)
  float runwayNeeded(float elev) const;
  // the most it may weigh at take-off: full tanks and a full load don't go together (three quarters of the fuel with
  // everything aboard, or all of it with a lighter load)
  float maxMass() const { return emptyMass + maxFuel * 0.75f + cargoKg + pax * 85.f + 85.f; }
  float fuelPriceBase() const { return engineType == ENG_PISTON ? 2.2f : 1.4f; }   // $ per kg at a hub
};

// whether a type may use a field: its surface (grass and sand only for the short-field types, gravel and snow for the
// rough-field ones) and its length (runwayNeeded). The career dispatches by it and the autoland lands by it.
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
extern const int kNumAircraft;   // career aircraft (market, rentals, contracts)
static const int kOsprey = 8;       // the Osprey C6: the only type with its own cabin trim in the field (plane_sdf.glsl)
// the research craft (hidden from the career, only reachable from the research terminal), after the career types
static const int kNightjar = 9;     // XR-10 Nightjar: a conventional twin jet (special 0, the generic field and flight model)
static const int kResearchJet = 10; // XR-30 Specter
static const int kMantis = 11;      // XR-20 Mantis: forward-swept systems demonstrator (special 0; the generic field)
static const int kWraith = 12;      // XR-40 Wraith stealth aerobatic research craft
// XR-40 thruster pods (body coords, +z aft): front left, front right, rear left, rear right pivot points
static const vec3 kWraithPods[4] = {vec3(-2.35f, -0.08f, -3.3f), vec3(2.35f, -0.08f, -3.3f), vec3(-2.75f, 0.05f, 3.45f), vec3(2.75f, 0.05f, 3.45f)};

// What an aircraft can do, learned by flying it: Plane::perf() flies a few short test sorties through the flight model
// (once per type, cached) and measures its envelope. The autopilot flies to these numbers, so it pushes every type to
// its own limits and re-learns them by itself if the flight model changes.
// Where the wheels touch, in the body frame: one set of numbers for the physics' ground contacts (Plane::step) and the
// drawn model (models.cpp packModel), so an aircraft always stands on the wheels it shows - the mains' half track and
// station, the nose wheel's station, a taildragger's tail wheel (station, height above the mains' contact)
struct GearStations { float track, mainZ, noseZ, tailZ, tailY; };
GearStations gearStations(const AircraftSpec& s);

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
  bool sceneryHits = true;   // collide with trees and buildings (off for the quote's background flight: the scenery isn't thread-safe)
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
  enum ApStage { APS_NAV = 0, APS_FINAL, APS_FLARE, APS_ROLLOUT, APS_GOAROUND, APS_HOVER, APS_BLEED };
  // (APS_BLEED: the research craft come down the final fast and, a few km out, rear up belly-first into the airflow -
  // throttle closed, nose 70 deg up - to shed the speed, then drop the nose back onto the glidepath)
  int apBleedPhase = 0; bool apBled = false; float apBleedT = 1e9f;   // (pitching up / nose back down; done this approach; s since)
  float apFlareMin = 0;   // the lowest the flare has been (m above the runway): a balloon is measured from it
  float apGroundT = 0;   // s the rollout has had its wheels down without a skip (the nose waits for it)
  bool apOn = false; int apMode = AP_OFF;
  float apHeading = 0, apAlt = 0, apSpeed = 0, apVS = 0; bool apUseVS = false;
  float apPitchI = 0, apRollI = 0, apYawI = 0, apVmcCap = 1, apThrI = 0.5f, apXI = 0, apGamI = 0, apTrimEst = 0, apFlareTau = 0, apFlareVs = 0, apGustAdd = 0;
  int apAirport = -1, apStage = 0, apLeg = 0; float apOutSide = 0;   // (apOutSide: the side of the centreline the outbound leg keeps to, +-1; 0 off that leg)
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
  void apDisengage() { apOn = false; apMode = AP_OFF; apUseVS = false; }
  float maxG = 1, minG = 1;
  float overG = 0;   // sustained overstress (grows past the structural limit, decays within it; the airframe fails at 1)
  float flightTime = 0;
  float mach = 0, nozzle = 0;  // research jet: Mach number, thrust-vector nozzle angle 0 (aft) .. 1 (straight down)
  // XR-40: each pod's pitch tilt (rad, 0 = thrust aft, pi/2 = thrust down, incl. vane vectoring), yaw vane (rad),
  // thrust fraction of full boost, fan angle; control-surface deflections (-1..1: pitch, yaw, roll) as allocated
  float podTilt[4] = {0, 0, 0, 0}, podYaw[4] = {0, 0, 0, 0}, podThr[4] = {0, 0, 0, 0}, podVane[4] = {0, 0, 0, 0}, fanAngle = 0;
  vec3 surf;
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
  float pitchDeg() const;
  float bankDeg() const;   // positive right wing down
  float agl() const;
  float gearHeight() const;
  float mass() const { return spec->emptyMass + fuel + payload; }
  float fuelFlowMax() const;   // kg/s at full throttle
  float spoolRate() const;     // how fast the engines follow the throttle (spool fraction per second)
  float thrustAt(float spool, float V, float vf, float rho = -1.f) const;   // all engines' thrust (N) at this spool, airspeed and forward airspeed, in the air it is in (or rho's)
  float liftThrustMax() const; // the thrust it can direct straight down (N): vectored lift
  float rangeLeftKm() const;
  float cd0Value() const { return cd0; }
  // fly-by-wire rate command of the research craft: full-stick pitch and roll rates (rad/s)
  float fbwPitchMax(float V) const { V = std::max(V, 1.f); return spec->special == 2 ? clampf(80.f * G0 / V, 2.0f, 6.0f) : clampf(66.f * G0 / V, 1.8f, 5.2f); }
  static const PerfModel& perf(const AircraftSpec* s);   // learned once per type (aircraft_perf.cpp)
  static bool perfLoad(const std::string& path, const std::string& stamp);   // every type's, from a cache of this build's
  static void perfSave(const std::string& path, const std::string& stamp);
  float fbwRollMax(float hover) const { return (spec->special == 2 ? 7.0f : 5.5f) * (1.f - 0.6f * hover); }
  ApEnvelope apEnv;            // what it can do now (apSense)
  void apSense();              // read it: the learned envelope, corrected for weight, air, configuration and condition
private:
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
  void wraithThrust(vec3& F, vec3& T, float podThrust, vec3 wd, vec3 Taero, vec3 surfMax, float dt);
  vec3 gust;
  float cd0 = 0.03f;
};
