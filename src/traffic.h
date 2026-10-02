// Air Xpress - AI traffic: airport circuits (park, taxi, take off, fly the pattern, land, taxi in), cruising
// traffic, XR-9 research formations ripping past, and an aerobatic display team.
#pragma once
#include "common.h"
#include <vector>
#include <string>

struct TrafficVisual;

struct TrafficCraft {
  enum Role { AIRPORT, CRUISER, FORMATION, STUNT, ESCORT };
  enum State { PARKED, TAXI_OUT, HOLD, LINEUP, TAKEOFF, CLIMB, CIRCUIT, FINAL, ROLLOUT, TAXI_IN, FLY };
  int spec = 0;               // kAircraft index
  int role = AIRPORT, state = PARKED;
  vec3 pos, vel; quat q;      // world position (CG), velocity, orientation (body -> world)
  float hdg = 0, pitch = 0, bank = 0;   // kinematic attitude for airport / cruise traffic (radians)
  float speed = 0, vs = 0;    // along-track speed and vertical speed (m/s)
  float gear = 1, flaps = 0, throttle = 0.2f, propAngle = 0, ab = 0;
  float ctlPitch = 0, ctlRoll = 0, ctlYaw = 0;
  float timer = 0;
  int airport = -1, stand = 0;
  std::vector<vec3> path; int wp = 0;   // taxi / circuit waypoints (world, y = target altitude when flying)
  vec3 colBase, colStripe;
  // formations: wingmen follow a leader at a fixed body-frame offset
  int leader = -1; vec3 offset;
  // display team: manoeuvre sequencer
  int man = -1, seg = 0; float segT = 0; vec3 smokeCol; bool smoke = false;
  float boomCD = 0, flybyCD = 0;
  float escSide = 1, escBlend = 0; vec3 escUp = vec3(0, 1, 0), escRel, escFrom;   // escort: which side of the player it works, smoothed lift direction
  bool alive = true;
  int id = 0;
};

struct TrafficPuff { vec3 p, v, col; float life, size, grow, alpha; int kind; };

class Traffic {
public:
  std::vector<TrafficCraft> craft;
  std::vector<TrafficPuff> puffs;     // particles to emit this frame (smoke, exhaust glow, dust)
  std::vector<std::pair<vec3, float>> booms;   // sonic booms this frame (position, intensity)
  std::vector<float> flybys;                    // close high-speed passes of the escort this frame (intensity)
  std::vector<std::string> radio;               // escort radio calls this frame (the game shows them)
  bool enabled = true;
  void reset();
  // player: position, velocity, on ground; returns true if the player collided with traffic (mid-air)
  bool update(float dt, vec3 playerPos, vec3 playerVel, bool playerOnGround, float playerSpan);
  int fillVisuals(vec3 camPos, TrafficVisual* out, int maxN, int* order) const;   // nearest first
  int count(int role) const;
  // O+P entertainment: a pair of XR-9s flies a display around the player until dismissed or the player lands
  void spawnEscort(vec3 player, vec3 playerVel);
  void dismissEscort();
  bool escortActive() const { return count(TrafficCraft::ESCORT) > 0 && escAct != ESC_LEAVE; }
  bool escortStop = false;    // set by the game: the player is landing / low, the pair says goodbye
  enum EscAct { ESC_JOIN = 0, ESC_FORM, ESC_HELIX, ESC_CROSS, ESC_LOOPS, ESC_SPLIT, ESC_LEAVE };
  int escAct = ESC_JOIN; float escT = 0;
private:
  float spawnT = 0, formationT = 40.f, stuntT = 0, t = 0;
  int nextId = 1;
  uint32_t seed = 4242;
  float rnd();
  float rnd(float a, float b) { return a + (b - a) * rnd(); }
  void spawnAirport(int ai, vec3 player);
  void spawnCruiser(vec3 player);
public:
  void spawnFormation(vec3 player, vec3 playerVel);
private:
  void spawnDisplayTeam(int ai);
  void updateAirport(TrafficCraft& c, float dt, vec3 playerPos, bool playerOnGround);
  void updateFlight(TrafficCraft& c, float dt, vec3 target, float tgtSpeed, float maxBank);
  void updateStunt(TrafficCraft& c, float dt);
  void updateEscorts(float dt, vec3 player, vec3 playerVel);
  float escDur(int act) const;
  void escMark();
  vec3 escRel(int act, float t, float side, float* roll, bool* smoke) const;
  vec3 escF = vec3(0, 0, -1); float escV = 60.f, escLift = 0; int escNext = 0, escLeft = 0;
  void groundTaxi(TrafficCraft& c, float dt, float maxSpeed, vec3 playerPos, bool playerOnGround);
  void attitudeToQuat(TrafficCraft& c) const;
  bool runwayBusy(int ai, int self, vec3 playerPos, bool playerOnGround) const;
};
