// Shared military board, loaner, objective and equipment catalogue. IDs are persisted in v5+ saves.
#pragma once
#include "career.h"
#include "hive_combat.h"
#include "hive_loadout.h"

namespace military {
enum MissionId {
  LegacyRecon = 0, IslandDefense = 1, CommandStrike = 2,
  LocalSurvey = 3, RouteRecon = 4, HighAltitudeIntel = 5, HoverScan = 6,
  Interception = 7, HeavyDefense = 8, RelaySuppression = 9, StratosphericRecon = 10
};
struct MissionSpec {
  int id, contractType, aircraft;
  const char* title;
  const char* role;
  const char* brief;
  const char* loadout;
  int reward;
  hive::MissionKind objective;
  hive::ForwardSet forward;
  hive::BombSet bomb;
  bool intelligence;
  float scanRange = 0, scanSeconds = 0;
  float minAltitude = 0, maxAltitude = 100000; // MSL, except HoverScan: metres above target roof.
  float minSpeed = 0, maxSpeed = 100000;       // ground speed in m/s, measured from the live velocity.
};
inline constexpr MissionSpec kMissions[] = {
  {LegacyRecon, CT_MIL_RECON, 0, "Hive reconnaissance", "Introductory reconnaissance",
   "Photograph three marked airport structures. Keep each site ahead and within 1.5 km for five seconds of clear sensor view, then return to extraction.",
   "Unarmed reconnaissance sensors", 600, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::Plasma, true, 1500, 5},
  {IslandDefense, CT_MIL_DEFENSE, kSomnus, "Defend the island", "Installation defense",
   "Protect the marked control tower through three attack waves of Needle interceptors, Bastion bombers and a Cantor relay. Clear hostiles and live ordnance, then extract.",
   "Pulse cannon / Plasma bombs", 1200, hive::MissionKind::Defense, hive::ForwardSet::Pulse, hive::BombSet::Plasma, false},
  {CommandStrike, CT_MIL_STRIKE, kSomnus, "Strike the command group", "Command strike",
   "Destroy the designated Archon command craft and Cantor shield relay. A Needle escort guards them. Only designated targets are required; return to extraction after the strike.",
   "Pulse cannon / Penetrator bombs", 1800, hive::MissionKind::Strike, hive::ForwardSet::Pulse, hive::BombSet::Penetrator, false},
  {LocalSurvey, CT_MIL_LOCAL_SURVEY, kVigil, "Vigil local survey", "Local security survey",
   "Survey two marked structures at the staging airport. Keep each ahead and within 1.6 km for three seconds of clear sensor view. The XR-10 is unarmed; return to extraction with both recordings.",
   "Unarmed short-range survey sensors", 800, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::Plasma, true, 1600, 3},
  {RouteRecon, CT_MIL_ROUTE_RECON, kReverie, "Reverie island circuit", "Multi-airport reconnaissance",
   "Reconnoitre three nearby airports in the XR-20. Record each marked structure from within 2.4 km with four seconds of clear forward sensor view, then return to the staging extraction gate.",
   "Unarmed route-reconnaissance sensors", 1100, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::Plasma, true, 2400, 4},
  {HighAltitudeIntel, CT_MIL_HIGH_INTEL, kResearchJet, "Nocturne high-speed intelligence", "High-altitude intelligence",
   "Record two airport sites from 3,500-6,500 m MSL while maintaining at least 350 kt ground speed (180 m/s). Keep each site ahead, visible and within 7 km for three seconds, then return to the high-altitude extraction gate.",
   "Unarmed long-range intelligence sensors", 1500, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::Plasma, true, 7000, 3, 3500, 6500, 180},
  {HoverScan, CT_MIL_HOVER_SCAN, kSomnus, "Somnus precision hover scan", "Close-range hover intelligence",
   "Use the XR-40's tilting pods to hold below 23 kt ground speed (12 m/s), 30-120 m above the marked roof. Keep the structure ahead, visible and within 250 m for eight seconds, then extract. No enemies are assigned.",
   "Precision sensors / Pulse cannon / EMP stores", 1300, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::EMP, true, 250, 8, 30, 120, 0, 12},
  {Interception, CT_MIL_INTERCEPT, kSomnus, "Somnus interceptor sweep", "Armed interception",
   "Intercept three designated Needle fighters on separated approaches. Use the supplied Kinetic gun against their agile hulls. Destroy all three designated targets and return to extraction.",
   "Kinetic gun / EMP stores", 1400, hive::MissionKind::Strike, hive::ForwardSet::Kinetic, hive::BombSet::EMP, false},
  {HeavyDefense, CT_MIL_HEAVY_DEFENSE, kSomnus, "Somnus bomber defense", "Heavy installation defense",
   "Protect the marked hangar through four bomber-led waves with relay and fighter support. Prioritise the Cantor relay in wave three. Hold fire to charge the Heavy cannon; use supplied Plasma stores against bombers. Clear every wave and live hostile ordnance, then extract.",
   "Heavy cannon / Plasma bombs", 2200, hive::MissionKind::Defense, hive::ForwardSet::Heavy, hive::BombSet::Plasma, false},
  {RelaySuppression, CT_MIL_RELAY_STRIKE, kSomnus, "Somnus relay suppression", "Shield-relay strike",
   "Destroy three designated Cantor shield relays protected by two Bastion escorts. EMP stores disrupt shields but do not damage hulls; finish each relay with the Heavy cannon, then extract. Escorts are optional targets.",
   "Heavy cannon / EMP stores", 2000, hive::MissionKind::Strike, hive::ForwardSet::Heavy, hive::BombSet::EMP, false},
  {StratosphericRecon, CT_MIL_STRAT_RECON, kMorpheus, "Morpheus supersonic sweep", "Supersonic wide-area reconnaissance",
   "Sweep three airfields in the XR-50 at Mach 2: record each from 5,000-8,000 m MSL at no less than 1,260 kt ground speed (650 m/s). Keep each site ahead, visible and within 10 km for two seconds - nose down a little to hold it in the sensor's view - then slow down for the extraction gate at 6,500 m.",
   "Unarmed wide-area strategic sensors", 2600, hive::MissionKind::Recon, hive::ForwardSet::Pulse, hive::BombSet::Plasma, true, 10000, 2, 5000, 8000, 650}
};
inline constexpr int kMissionCount = sizeof(kMissions) / sizeof(kMissions[0]);
// This is both display and progression order. Never substitute numerical MissionId order.
// The legacy trainer stays loadable/replayable but does not advance the XR board's progression.
inline constexpr int kBoardMissionIds[] = {LocalSurvey, RouteRecon, HighAltitudeIntel, HoverScan, Interception, IslandDefense, HeavyDefense, RelaySuppression, CommandStrike, StratosphericRecon};
inline constexpr int kBoardMissionCount = sizeof(kBoardMissionIds) / sizeof(kBoardMissionIds[0]);
static_assert(kMissionCount < 32, "Persisted mission completion bits must fit uint32_t");
inline constexpr uint32_t kKnownMissionMask = (uint32_t(1) << kMissionCount) - 1;
inline constexpr uint32_t completionBit(int id) {
  return id >= 0 && id < kMissionCount ? uint32_t(1) << id : 0;
}
inline constexpr const MissionSpec* mission(int id) {
  return id >= 0 && id < kMissionCount ? &kMissions[id] : nullptr;
}
inline constexpr const MissionSpec* missionForContract(int type) {
  const auto* m = mission(type - CT_MIL_RECON);
  return m && m->contractType == type ? m : nullptr;
}
// Shared with the actual sensor path and headless tests. The hover site has one roof-relative envelope.
// The high sorties measure ground speed (a climb or a dive doesn't count): the XR-30's and the XR-50's.
inline constexpr bool groundSpeedSortie(int id) { return id == HighAltitudeIntel || id == StratosphericRecon; }
inline bool sensorEnvelope(const MissionSpec& m, float altitudeMSL, float speed, float targetRoof = 0) {
  const float altitude = m.id == HoverScan ? altitudeMSL - targetRoof : altitudeMSL;
  return m.objective == hive::MissionKind::Recon && altitude >= m.minAltitude && altitude <= m.maxAltitude &&
    speed >= m.minSpeed && speed <= m.maxSpeed;
}
static_assert(LegacyRecon == 0 && IslandDefense == 1 && CommandStrike == 2, "Never renumber persisted mission IDs");
static_assert(CT_MIL_RECON == 11 && CT_MIL_DEFENSE == 12 && CT_MIL_STRIKE == 13, "Never renumber persisted contract types");
} // namespace military
