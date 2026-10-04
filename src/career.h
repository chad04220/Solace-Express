// Solace Express - career: licenses, contracts, fleet, money, save/load
#pragma once
#include "common.h"
#include "world.h"
#include "aircraft.h"

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

  void newGame();
  const Contract* nextStory() const;
  void refreshBoard();
  // Which aircraft can fly a contract and how it would be sourced
  enum Source { SRC_NONE = 0, SRC_LESSON, SRC_RENT, SRC_OWNED };
  Source canFly(const Contract& c, int specIdx, std::string* why = nullptr) const;
  int ownedIndexFor(int specIdx) const;
  // Settle a finished flight; returns lines for the debrief
  std::vector<PayoutLine> settle(const Contract& c, int specIdx, Source src, const FlightResult& r, int* stars);
  bool buy(int specIdx, std::string* msg);
  bool sell(int fleetIdx, std::string* msg);
  int positioningCost(const Contract& c) const;
  int ferryCost(const Contract& c, int specIdx) const;
  // What a job is likely to be worth with an aircraft: time, fuel, the fees settle() will charge, the net, and its
  // main difficulty (an estimate: the real flight decides)
  struct Estimate { float minutes = 0, fuelKg = 0; int fees = 0, fuelCost = 0, net = 0; std::string challenge; };
  Estimate estimate(const Contract& c, int specIdx, Source src) const;
  int repBonusPct() const { return std::min(15, reputation / 4); }   // +1% per 4 reputation, at most +15%
  bool save(const std::string& path) const;
  bool load(const std::string& path);
};

extern std::vector<Contract> g_story;
void buildStory();
bool surfaceOK(const AircraftSpec& s, int surface);
bool runwayOK(const AircraftSpec& s, const Airport& a);
