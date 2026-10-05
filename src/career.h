// Solace Express - career: licenses, contracts, fleet, money, save/load
#pragma once
#include "common.h"
#include "world.h"
#include "aircraft.h"
#include <optional>

enum ContractType { CT_LESSON = 0, CT_CARGO, CT_PAX, CT_MEDEVAC, CT_VIP, CT_TOUR, CT_FERRY };
inline const char* contractTypeName(int t) { static const char* n[] = {"Lesson", "Cargo", "Passengers", "Medevac", "VIP Charter", "Scenic Tour", "Free Flight"}; return n[t]; }

struct Waypoint { float x, z, alt; };  // alt = metres MSL (ring centre)

struct Contract {
  std::string id, title, brief;
  int chapter = 0, type = CT_CARGO;
  int from = 0, to = 0;
  int cargoKg = 0, pax = 0;
  int payout = 0;
  float timeLimitMin = 0;
  int minLicense = LIC_STUDENT;
  bool ownedOnly = false, fragile = false;
  int grantLicense = -1;
  int forceAircraft = -1;      // lessons use a specific aircraft (free)
  Weather wx;
  std::vector<Waypoint> wps;
  std::vector<std::string> hints;  // lesson hints by phase (see Game::phase)
  bool startAirborne = false;
  bool story = false;
  int repBonusPct = 0;         // freelance: the reputation bonus already in the payout
};

struct OwnedPlane { int spec; int location; float fuel; float condition; };

// How a flight ended. Only a crash counts against the record and costs repairs; a safe diversion, an off-airport
// landing, running dry on the ground or abandoning the job just fail the contract.
enum FlightOutcome { OUT_SUCCESS = 0, OUT_ABANDONED, OUT_DIVERTED, OUT_OFF_AIRPORT, OUT_OUT_OF_FUEL, OUT_CRASHED };

struct FlightResult {
  bool success = false;
  FlightOutcome outcome = OUT_SUCCESS;
  std::string failReason;
  bool landed = false;          // flight ended on the ground after a touchdown
  float touchdownFpm = 0, maxG = 1, minG = 1, maxBank = 0, flightMin = 0;
  bool late = false;
  float centerlineErr = 0;
  float fuelUsedKg = 0;
  int divertedTo = -1;          // OUT_DIVERTED: the airport the aircraft stopped at
  // the arrival, for the debrief's coaching (-1: not recorded, e.g. no landing at the destination)
  float thrKt = -1, thrAglM = -1;   // airspeed and height crossing the landing threshold
  float tdPastThrM = -1;            // touchdown point past the threshold
  float stopLeftM = -1, rwyLenM = 0;   // runway left ahead when the aircraft stopped, and the runway's length
  int goArounds = 0;                // climbed away from a low approach without touching down
};

struct PayoutLine { std::string label; int amount; };

class Career {
public:
  int money = 600;
  int license = LIC_STUDENT;
  int reputation = 0;
  int location = 0;
  int storyIndex = 0;          // next story contract
  int flights = 0, landings = 0, crashes = 0;
  float hours = 0, bestLandingFpm = 9999;
  std::vector<OwnedPlane> fleet;
  std::vector<Contract> board;  // freelance jobs at current location
  uint32_t boardSeed = 1;
  bool finished = false;
  uint32_t attempt = 0;        // monotonic per career: one per flight begun
  bool attemptOpen = false;    // a flight was in progress when this career was saved (the save before the flight)

  void newGame();
  const Contract* nextStory() const;
  void refreshBoard();
  // Which aircraft can fly a contract and how it would be sourced
  enum Source { SRC_NONE = 0, SRC_LESSON, SRC_RENT, SRC_OWNED };
  Source canFly(const Contract& c, int specIdx, std::string* why = nullptr) const;
  int ownedIndexFor(int specIdx) const;
  // Settle a finished flight; returns lines for the debrief
  // (plan: the launch plan the flight was started with - its fixed fees are charged exactly as quoted; without one
  // they are worked out now)

  bool buy(int specIdx, std::string* msg);
  bool sell(int fleetIdx, std::string* msg);
  int positioningCost(const Contract& c) const;
  int ferryCost(const Contract& c, int specIdx) const;
  // The launch plan of a job with an aircraft: what it will cost (the fixed fees, quoted exactly and charged as quoted
  // by settle), how long it will take and the fuel (estimates, with their uncertainty), the net and the main difficulty.
  // One plan is used by the job card, the launch and the settlement.
  struct LaunchPlan {
    int spec = 0; Source src = SRC_NONE; int startAirport = 0;
    int positioning = 0, ferry = 0, hire = 0;          // fixed, quoted exactly at acceptance
    enum FuelPolicy { FUEL_INCLUDED, FUEL_BILL_CONSUMED, FUEL_PURCHASED } fuel = FUEL_INCLUDED;
    float fuelKgEst = 0, minutesEst = 0, minutesSigma = 0;
    int fuelCostEst = 0, net = 0;                      // net = payout - fixed fees - fuelCostEst
    std::string challenge;
    float tCruise = 0, tClimb = 0, tOrbit = 0, tApproach = 0;   // the estimate's parts (min): en route, climbing, the descent orbit, the approach
    bool flown = false;   // minutesEst is the job flown on the autopilot in the background (else the quick estimate)
    int fees() const { return positioning + ferry + hire; }
    bool mayBeLate(float timeLimitMin) const { return timeLimitMin > 0 && minutesEst + minutesSigma > timeLimitMin; }
  };
  LaunchPlan plan(const Contract& c, int specIdx, Source src) const;
  // A job is a contract accepted and not yet settled. A flight that ends short of the destination (a diversion, a
  // field landing, an abandoned leg) closes a leg: that leg's costs are charged, the clock and the load's treatment
  // carry over, and the job waits at the airport the load is at for the next leg. The payment comes once, when the
  // load reaches the destination.
  struct JobState {
    Contract c;                 // the terms, frozen at acceptance
    LaunchPlan plan;            // the quote of the first leg
    int spec = 0; Source src = SRC_NONE;
    enum State { READY = 0, ACTIVE, RECOVERY, DONE, FAILED, CANCELLED } state = READY;
    int at = 0;                 // the airport the load / party is at now
    int legs = 0, wpDone = 0;
    float jobClockMin = 0;      // cumulative simulated minutes against the deadline
    float maxG = 1, minG = 1, maxBank = 0; bool fragileHit = false;
    float fuelBilledKg = 0;
    bool hirePaid = false, positioningPaid = false;
    uint32_t id = 0;
    Contract continuation() const { Contract k = c; k.from = at; k.startAirborne = false; return k; }   // the next leg's contract
  };
  std::optional<JobState> job;   // one at a time
  // what happens to a contract when a leg ends short: whole retake (lessons, checkrides), resume (the usual), resume
  // against the running clock (timed, VIP), resume with the destination requirement kept (medevac)
  enum JobPolicy { POL_UNSET = 0, POL_RESUME, POL_RETAKE, POL_RESUME_CLOCK, POL_MEDEVAC };
  static JobPolicy policyOf(const Contract& c);
  static bool resumable(const Contract& c) { JobPolicy p = policyOf(c); return p != POL_RETAKE && p != POL_UNSET; }
  void accept(const Contract& c, int specIdx, Source src, const LaunchPlan& plan);   // -> a job, ACTIVE at c.from
  // a leg that ended short: this leg's costs (fees not yet paid, this leg's fuel for an owned aircraft, a recovery fee
  // when the load had to be brought to an airport), the clock and the aggregates carried, the job RECOVERY at "at"
  std::vector<PayoutLine> closeLeg(const FlightResult& r, const LaunchPlan& plan, int at, int recoveryFee, const char* recoveryLabel);
  // the load delivered: the payment once, lateness against the job clock, the deductions from the whole job
  std::vector<PayoutLine> settleJob(const FlightResult& r, const LaunchPlan& plan, int* stars);
  void releaseJob();          // CANCELLED: the load stays where it is, nothing is charged
  void useFlownTime(LaunchPlan& e, const Contract& c, float minutes, float fuelKg = -1) const;
  void finishPlan(LaunchPlan& e, const Contract& c, float minutes, float fuelKg = -1) const;
  std::vector<PayoutLine> settle(const Contract& c, int specIdx, Source src, const FlightResult& r, int* stars, const LaunchPlan* plan = nullptr);
  using Estimate = LaunchPlan;
  LaunchPlan estimate(const Contract& c, int specIdx, Source src) const { return plan(c, specIdx, src); }
  int repBonusPct() const { return std::min(15, reputation / 4); }   // +1% per 4 reputation, at most +15%
  bool save(const std::string& path) const;
  bool load(const std::string& path);
};

extern std::vector<Contract> g_story;
// The job flown headless on the career autopilot (as startFlight sets it up: the runway into the wind, full tanks,
// the load), a scripted takeoff, the checkpoints in turn, then the autopilot's approach and landing: the minutes it
// takes, or a negative number if it didn't get there. About half a second of CPU: callers run it off the UI thread.
float simulateFlightMinutes(const Contract& c, int specIdx, float* fuelKgOut = nullptr);
extern float kEstK[5];   // the flight-time estimate's fitted weights (Career::plan)
void buildStory();
bool surfaceOK(const AircraftSpec& s, int surface);
bool runwayOK(const AircraftSpec& s, const Airport& a);
