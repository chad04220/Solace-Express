// Air Xpress - aircraft specs and flight dynamics
#pragma once
#include "common.h"
#include "world.h"

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
  int special = 0;             // 1 = XR-9 research jet: fly-by-wire, thrust vectoring, VTOL nozzles, no fuel burn
  float runwayNeeded(float elev) const { return runwayM * (1.0f + elev / 3000.0f); }
};

extern const AircraftSpec kAircraft[];
extern const int kNumAircraft;   // career aircraft (market, rentals, contracts)
static const int kResearchJet = 7; // hidden XR-9, only reachable from the research menu

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
};

class Plane {
public:
  const AircraftSpec* spec = nullptr;
  vec3 pos, vel;          // world
  quat q;                 // body -> world. Body: +x right, +y up, -z forward
  vec3 w;                 // angular velocity (body): x pitch-up, y yaw-left, z roll-left
  Controls ctl;
  float fuel = 0, payload = 0;
  float flaps = 0, gear = 1;  // actual positions
  float rpm = 0, n1 = 0;      // engine state (spooled)
  float engineSpool = 0;      // 0..1 actual power fraction
  bool engineRunning = false; float starterTime = 0;
  bool onGround = false, wasOnGround = false;
  float groundRough = 0;      // 0 asphalt .. 1 rough (for audio/vibration)
  float alpha = 0, beta = 0, airspeed = 0, ias = 0, gLoad = 1, stallWarn = 0;
  vec3 windVel;               // current wind incl. gusts
  float density = 1.225f;
  // ---- autopilot: HOLD (heading / altitude / speed), NAV (to a chosen airport), APPR (approach, flare, rollout)
  enum ApMode { AP_OFF = 0, AP_HOLD, AP_NAV, AP_APPR };
  enum ApStage { APS_NAV = 0, APS_FINAL, APS_FLARE, APS_ROLLOUT, APS_GOAROUND };
  bool apOn = false; int apMode = AP_OFF;
  float apHeading = 0, apAlt = 0, apSpeed = 0, apVS = 0; bool apUseVS = false;
  float apPitchI = 0, apRollI = 0, apThrI = 0.5f, apXI = 0;
  int apAirport = -1, apStage = 0, apLeg = 0; bool apRev = false; float apStageT = 0, apCruiseAlt = 0, apFinalLen = 8000;
  vec3 apLd, apTd;            // landing direction and touchdown point of the chosen runway end
  vec3 apHoldC; float apHoldR = 1500, apHoldAlt = 0, apIntAlt = 0; int apHoldDir = 1, apTurnDir = 0;   // descent orbit and intercept altitude
  bool apDone = false;        // an autoland just finished (the game sets the parking brake)
  std::string apStatus;       // one-line status for the HUD
  void apEngage(int mode, int airport, const Weather& wx);
  void apDisengage() { apOn = false; apMode = AP_OFF; apUseVS = false; }
  float maxG = 1, minG = 1;
  float flightTime = 0;
  float mach = 0, nozzle = 0;  // research jet: Mach number, thrust-vector nozzle angle 0 (aft) .. 1 (straight down)
  FlightEvents ev;
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
  float rangeLeftKm() const;
  float cd0Value() const { return cd0; }
private:
  void substep(float dt, const Weather& wx, float time);
  void apGuidance(float dt);
  void apControl(float dt);
  float apPlan(int airport, bool rev, const Weather& wx, bool commit);
  vec3 gust;
  float cd0 = 0.03f;
};
