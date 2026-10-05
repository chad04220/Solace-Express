// Solace Express - career: licenses, contracts, fleet, money, save/load
#pragma once
#include "common.h"
#include "world.h"
#include "aircraft.h"
#include <optional>

// C6: the freelance board posts medevacs (a patient who must be flown gently and fast), VIP charters (a live comfort
// meter), night freight (both ends lit, the landing light on for the touchdown), low-visibility runs (cloud base and
// visibility at minimums: be lined up when you break out, or go around) and surveys (a ring pattern at one altitude)
enum ContractType { CT_LESSON = 0, CT_CARGO, CT_PAX, CT_MEDEVAC, CT_VIP, CT_TOUR, CT_FERRY, CT_NIGHT, CT_IFR, CT_SURVEY, CT_TRIAL, CT_COUNT };
inline const char* contractTypeName(int t) { static const char* n[] = {"Lesson", "Cargo", "Passengers", "Medevac", "VIP Charter", "Scenic Tour", "Free Flight", "Night Freight", "Low-Vis Run", "Survey", "Trial"}; return n[t]; }

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
  // C8: where the weather is going. With wxShift the conditions drift from wx to wxEnd over the flight's estimated
  // time (wind direction and strength, cloud, visibility, precipitation); the brief shows the forecast
  Weather wxEnd; bool wxShift = false;
  std::vector<Waypoint> wps;
  std::vector<std::string> hints;  // lesson hints by phase (see Game::phase)
  bool startAirborne = false;
  bool story = false;
  int repBonusPct = 0;         // freelance: the reputation bonus already in the payout
  bool courtesy = false;       // the client flies you to the departure (no positioning ticket): the recovery job
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
  float fuelUsedKg = 0, fuelLeftKg = -1;   // (fuelLeftKg < 0: not recorded; an owned aircraft keeps what is left)
  int divertedTo = -1;          // OUT_DIVERTED: the airport the aircraft stopped at
  // the arrival, for the debrief's coaching (-1: not recorded, e.g. no landing at the destination)
  float thrKt = -1, thrAglM = -1;   // airspeed and height crossing the landing threshold
  float tdPastThrM = -1;            // touchdown point past the threshold
  float stopLeftM = -1, rwyLenM = 0;   // runway left ahead when the aircraft stopped, and the runway's length
  int goArounds = 0;                // climbed away from a low approach without touching down
  // the rest of the record the settlement scores (C9 / C10): fuel left at the end (fraction of the tanks), a
  // shutdown off the runway with the brake set, and the tower's instructions ignored
  float fuelLeftFrac = 1.f;
  bool shutDownAtStand = false;     // stopped clear of the runway, engine off, parking brake set
  bool holdViolated = false;        // moved off (or took off) while told to hold
  bool landedAgainstGoAround = false;   // touched down after the tower said go around
  // C7: what broke on the flight (bits 1 << FailureKind) and whether it ended on its belly
  int failureKinds = 0;
  bool bellyLanding = false;
  // C6: the job types' own records. patient / comfort run from 1 down as the ride gets rough (medevac, VIP); the
  // landing light at touchdown (night freight); an approach continued below minimums without being lined up (low-vis);
  // the share of the survey pattern flown inside the altitude band
  float patient = 1.f, comfort = 1.f;
  bool landingLightOn = true;
  bool belowMinimumsUnaligned = false;
  float surveyInBand = 1.f;
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
  // Financing: one loan at a time, on one aircraft. A quarter down, the rest plus interest in equal payments taken
  // at every settlement (success or not); three payments missed in a row and the aircraft is repossessed.
  struct Loan { int spec = -1; int balance = 0; int payment = 0; int missed = 0; float rate = 0.1f; bool open() const { return spec >= 0 && balance > 0; } } loan;
  static const int kLoanTerm = 24;   // payments
  float loanRate() const { return clampf(0.12f - reputation * 0.0015f, 0.08f, 0.12f); }   // 12% down to 8% with reputation
  int downPayment(int specIdx) const { return kAircraft[specIdx].price / 4; }
  int loanPayment(int specIdx) const { int p = kAircraft[specIdx].price - downPayment(specIdx); return (int)((p * (1.f + loanRate())) / kLoanTerm) / 10 * 10 + 10; }
  bool finance(int specIdx, std::string* msg);    // the aircraft on a loan
  bool buyUsed(int specIdx, std::string* msg);    // a used one at 65%, at two thirds condition
  int usedPrice(int specIdx) const { return kAircraft[specIdx].price * 65 / 100 / 10 * 10; }
  void payLoan(std::vector<PayoutLine>& L);       // one payment at a settlement (called by settle / closeLeg)
  bool attemptOpen = false;    // a flight was in progress when this career was saved (the save before the flight)

  void newGame();
  const Contract* nextStory() const;
  void refreshBoard();
  // Which aircraft can fly a contract and how it would be sourced
  enum Source { SRC_NONE = 0, SRC_LESSON, SRC_RENT, SRC_OWNED };
  // Failures and maintenance (C7). An owned aircraft wears with the hours and the hard landings; its condition sets
  // the chance something breaks on a flight (rentals a fixed low chance, lessons none). Repairs after a failure or a
  // belly landing are charged unless the insurance (a premium per flight) is on; a service restores the condition.
  bool insured = false;
  float failureChance(Source src, int specIdx) const;   // probability that one thing breaks on the next flight
  int insurancePremium(int specIdx) const { return std::max(40, kAircraft[specIdx].price / 500 / 10 * 10); }
  int serviceCost(int fleetIdx) const;
  bool service(int fleetIdx, std::string* msg);
  void wear(std::vector<PayoutLine>& L, int specIdx, Source src, const FlightResult& r);   // condition loss, repairs, premium
  static int repairCost(int specIdx, int failureKinds, bool belly);
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
    float fuelUpliftKg = 0;    // FUEL_PURCHASED: what is bought at the departure (the tanks' shortfall to the chosen fuel)
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
  // There is always a way to earn: a board job the player can fly for a positive net, or a free lesson next in the
  // story. When neither holds (a strip no rentable aircraft can use, a licence the board has nothing for, a balance
  // that can't cover the fees) the board gets a recovery job: light mail, no deadline, mild daylight weather, a
  // rentable licensed aircraft, from the nearest field such an aircraft can use (a courtesy ride there), paying a
  // net of at least $150 after the fees and a hard landing.
  bool earningPath() const;
  int netQuick(const Contract& c, int specIdx, Source src) const;   // payout less the fees and a fuel estimate (no autopilot planning)
  Contract recoveryContract() const;   // (.payout == 0 when none can be made: no licensed aircraft can use any field)
  // fuel for an owned aircraft is bought at the departure: the plan's fuel cost becomes the uplift (the shortfall of
  // its tanks to the fuel chosen for the flight, at that airport's price); a rental comes with its tanks full
  void planFuel(LaunchPlan& e, const Contract& c, float fuelKg) const;
  float fuelPrice(int airport, int specIdx) const { return kAircraft[specIdx].fuelPriceBase() * g_world.airports[airport].fuelPriceMult(); }
  bool refuel(int fleetIdx, std::string* msg);   // the hangar: fill the owned aircraft parked here at this airport's price
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
