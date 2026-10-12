// Military career lifecycle: supplied loaners, isolated service accounting, migration and replay safety.
#include "../src/career.h"
#include "../src/military_missions.h"
#include "test_world.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>

static int failures = 0, checks = 0;
static void check(bool ok, const char* label) { ++checks; if (!ok) { ++failures; printf("FAIL: %s\n", label); } }
static const char* path = "military_career_test.sav";
static std::string readFile() { std::ifstream f(path); return std::string(std::istreambuf_iterator<char>(f), {}); }
static void writeFile(const std::string& text) { std::ofstream f(path); f << text; }
static std::string replaceLine(std::string text, const std::string& key, const std::string& line) {
  const size_t p = text.find(key); check(p != std::string::npos, "fixture contains edited field");
  if (p != std::string::npos) text.replace(p, text.find('\n', p) - p + 1, line.empty() ? "" : line + "\n");
  return text;
}
static std::string serialized(const Career& c) { check(c.save(path), "save writes"); return readFile(); }
static std::string civilianState(const Career& c) {
  std::istringstream in(serialized(c)); std::ostringstream out; std::string line;
  while (std::getline(in, line))
    if (line.rfind("military ", 0) && line.rfind("military_progress ", 0) && line.rfind("attempt ", 0) && line.rfind("attempt_open ", 0)) out << line << '\n';
  // These in-memory fields intentionally are not part of the save format.
  for (const auto& s : c.airline.log) out << "log " << s << '\n';
  for (const auto& b : c.board) out << "board " << b.id << ' ' << b.type << ' ' << b.payout << '\n';
  return out.str();
}
static Career populated() {
  Career c; c.newGame(); c.money = 12600; c.license = LIC_ATP; c.reputation = 19;
  c.storyIndex = 3; c.flights = 14; c.landings = 12; c.crashes = 1; c.hours = 7.25f; c.bestLandingFpm = 85;
  c.boardSeed = 42; c.refreshBoard(); c.insured = true;
  c.fleet.push_back({0, c.location, 12.f, 0.7f}); c.fleet.push_back({1, c.location, 44.f, 0.8f});
  c.loan.spec = 0; c.loan.balance = 5000; c.loan.payment = 300; c.loan.missed = 2;
  c.airline.pilots.push_back({"Test Pilot", 2, 140});
  c.airline.routes.push_back({1, c.location, (c.location + 1) % int(g_world.airports.size()), 0, 4, 2100, 5.f});
  c.airline.earned = 2100; c.airline.incidents = 2; c.airline.log.push_back("Keep this civilian history");
  Contract j; j.id = "F_MIL_ISOLATION"; j.title = "Waiting freight"; j.brief = "Preserve this job";
  j.from = c.location; j.to = (c.location + 1) % int(g_world.airports.size()); j.payout = 2300; j.cargoKg = 80;
  Career::LaunchPlan p; p.startAirport = c.location; p.spec = 0; p.src = Career::SRC_OWNED;
  c.accept(j, 0, Career::SRC_OWNED, p); c.job->state = Career::JobState::RECOVERY;
  c.job->jobClockMin = 17.5f; c.job->patient = 0.8f; c.job->comfort = 0.7f;
  return c;
}
static FlightResult success() {
  FlightResult r; r.success = true; r.outcome = OUT_SUCCESS; r.flightMin = 15;
  r.landed = true; r.fuelLeftKg = 0; r.failureKinds = 3; r.bellyLanding = true;
  r.touchdownFpm = 1600; r.holdViolated = true; return r;
}
// Use real accepted settlements to prepare an unlocked fixture, so saved completion records
// remain coherent with the service ledger and do not hide direct-launch gating mistakes.
static Career unlockedFor(Career c, int kind) {
  if (kind != military::LegacyRecon) for (int id : military::kBoardMissionIds) {
    if (id == kind) break;
    const auto m = c.militaryContract(id);
    check(c.beginMilitary(m), "fixture predecessor begins in canonical order");
    c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
  }
  return c;
}
static void rejects(const std::string& text, const Career& baseline, const char* label) {
  Career probe = baseline; const std::string before = serialized(probe); writeFile(text);
  check(!probe.load(path), label); check(serialized(probe) == before, "rejected save does not modify live career");
}
int main() {
  buildTestWorld(); buildStory();
  // Appended enums preserve all pre-military contract/source values.
  check(CT_TRIAL == 10 && CT_MIL_RECON == 11 && Career::SRC_OWNED == 3 && Career::SRC_MILITARY == 4,
        "legacy enum values are stable");
  check(CT_MIL_DEFENSE == 12 && CT_MIL_STRIKE == 13 && military::LegacyRecon == 0 &&
        military::IslandDefense == 1 && military::CommandStrike == 2, "v5 military IDs remain unchanged");
  check(!military::mission(-1) && !military::mission(military::kMissionCount) &&
        !military::missionForContract(CT_TRIAL) && !military::missionForContract(CT_COUNT), "catalogue rejects out-of-range IDs and civilian types");
  bool seen[military::kMissionCount] = {}; bool xrSeen[kAircraftCount] = {};
  for (int id : military::kBoardMissionIds) {
    const auto* m = military::mission(id);
    check(m && !seen[id] && isResearchAircraft(m->aircraft), "board has unique missions with research-only loaners");
    if (m) { seen[id] = true; xrSeen[m->aircraft] = true;
      check(m->id == id && military::missionForContract(m->contractType) == m && isMilitaryContract(m->contractType), "catalogue mission and persisted contract types agree");
      check(m->title[0] && m->role[0] && m->brief[0] && m->loadout[0] && m->reward > 0, "all board missions have complete shared UI metadata");
      check(m->aircraft == kWraith || m->objective == hive::MissionKind::Recon, "non-weapon XR aircraft receive sensor missions only");
    }
  }
  for (int aircraft : kResearchAircraft) check(xrSeen[aircraft], "board offers each XR aircraft");
  check(!seen[military::LegacyRecon], "legacy trainer ID remains reserved outside XR board");
  const auto& high = *military::mission(military::HighAltitudeIntel);
  check(military::sensorEnvelope(high, 4500, 200) && !military::sensorEnvelope(high, 3499, 200) &&
        !military::sensorEnvelope(high, 6501, 200) && !military::sensorEnvelope(high, 4500, 179), "Specter sensor enforces live altitude and speed envelope");
  const auto& hover = *military::mission(military::HoverScan);
  check(military::sensorEnvelope(hover, 1060, 5, 1000) && !military::sensorEnvelope(hover, 1029, 5, 1000) &&
        !military::sensorEnvelope(hover, 1121, 5, 1000) && !military::sensorEnvelope(hover, 1060, 13, 1000), "Wraith hover sensor enforces roof-relative altitude and low speed");
  Career base = populated(); const std::string civil = civilianState(base);
  { // New careers reveal exactly one mission at a time in display order, not MissionId order.
    const int expectedOrder[] = {3, 4, 5, 6, 7, 1, 8, 9, 2};
    Career c = base;
    check(!c.militaryMissionCompleted(-1) && !c.militaryMissionUnlocked(-1) &&
          !c.militaryMissionCompleted(military::kMissionCount) && !c.militaryMissionUnlocked(military::kMissionCount),
          "progress APIs reject invalid mission identities");
    check(c.militaryMissionUnlocked(military::LegacyRecon), "legacy trainer remains available outside board chain");
    for (int index = 0; index < military::kBoardMissionCount; ++index) {
      const int id = military::kBoardMissionIds[index];
      check(id == expectedOrder[index], "display and progression retain canonical stable order");
      const std::string before = serialized(c);
      for (int slot = 0; slot < military::kBoardMissionCount; ++slot) {
        const int otherId = military::kBoardMissionIds[slot];
        const auto m = c.militaryContract(otherId);
        check(c.militaryMissionUnlocked(otherId) == (slot <= index), "only completed prefix and next mission unlocked");
        check(c.militaryMissionCompleted(otherId) == (slot < index), "completion records are per mission");
        if (slot > index) {
          std::string why;
          check(c.canFly(m, m.forceAircraft, &why) == Career::SRC_NONE && why.find("CLASSIFIED") != std::string::npos,
                "locked mission rejected with classified reason at actual launch gate");
          check(!c.beginMilitary(m) && c.plan(m, m.forceAircraft, Career::SRC_MILITARY).src == Career::SRC_NONE,
                "direct begin and quote cannot bypass progression");
          c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
          check(serialized(c) == before, "locked or practice-like settlement cannot invent an accepted attempt");
        }
      }
      const auto m = c.militaryContract(id);
      check(c.beginMilitary(m), "next mission launches in display order");
      const auto active = serialized(c);
      Contract forged = m; forged.id = "MIL_FORGED";
      c.settle(forged, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
      check(serialized(c) == active && !c.militaryMissionCompleted(id), "forged settlement identity never marks completion");
      Career notOpen = c; notOpen.attemptOpen = false;
      notOpen.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
      check(!notOpen.militaryMissionCompleted(id), "closed attempt flag cannot masquerade as genuine career success");
      c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
      check(c.militaryMissionCompleted(id), "only genuine successful settlement records mission identity");
      serialized(c);
      Career loaded; check(loaded.load(path) && loaded.military.completedMissions == c.military.completedMissions,
                           "every progressive completion survives save and restart");
      c = loaded;
      const uint32_t bits = c.military.completedMissions;
      const auto replay = c.militaryContract(military::LocalSurvey);
      check(c.beginMilitary(replay), "completed first mission remains replayable");
      c.settle(replay, replay.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
      check(c.military.completedMissions == bits, "replaying a completed mission never skips the next mission");
      if (index + 2 < military::kBoardMissionCount)
        check(!c.militaryMissionUnlocked(military::kBoardMissionIds[index + 2]), "extra wins and rank do not unlock later identities");
    }
    check(c.military.completedMissions == (military::kKnownMissionMask & ~military::completionBit(military::LegacyRecon)),
          "finished board records all nine and does not fabricate legacy trainer completion");
    c.newGame();
    check(!c.military.completedMissions && c.militaryMissionUnlocked(military::LocalSurvey) &&
          !c.militaryMissionUnlocked(military::RouteRecon), "new game clears all mission progress");
  }
  { // Save rejection keeps the newly earned unlock solely in the candidate until disk commit succeeds.
    Career live = base; const auto m = live.militaryContract(military::LocalSurvey);
    check(live.beginMilitary(m), "transaction fixture begins first mission");
    const auto diskBefore = serialized(live);
    Career candidate = live;
    candidate.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
    const std::string blocker = std::string(path) + ".tmp";
    std::error_code error;
    std::filesystem::create_directory(blocker, error);
    check(!error, "transaction fixture blocks temp-file write");
    check(!candidate.save(path), "failed result save reports failure");
    check(readFile() == diskBefore && !live.military.completedMissions && !live.militaryMissionUnlocked(military::RouteRecon),
          "failed save leaves live career and durable unlock unchanged");
    std::filesystem::remove(blocker, error);
    check(!error && candidate.save(path), "same candidate can commit after save blocker removed");
    Career loaded; check(loaded.load(path) && loaded.militaryMissionCompleted(military::LocalSurvey) &&
          loaded.militaryMissionUnlocked(military::RouteRecon) && !loaded.militaryMissionUnlocked(military::HighAltitudeIntel),
          "successful save retry commits exactly the intended next unlock");
    const auto once = serialized(loaded);
    loaded.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
    check(serialized(loaded) == once, "save retry cannot duplicate settlement or completion");
  }
  for (int kind = 0; kind != military::kMissionCount; ++kind) {
    const Career ready = unlockedFor(base, kind); const auto initial = ready.military;
    Career c = ready; Contract m = c.militaryContract(kind); const auto& mission = *military::mission(kind); const int craft = mission.aircraft;
    check(m.type == CT_MIL_RECON + kind && m.forceAircraft == craft, "mission selects authoritative catalogue loaner");
    c.license = LIC_STUDENT; c.money = -10000; c.fleet.clear(); c.airline.routes.clear();
    check(c.canFly(m, craft) == Career::SRC_MILITARY, "loaner needs no license, money or owned craft");
    check(c.canFly(m, craft == 0 ? kWraith : 0) == Career::SRC_NONE, "wrong craft is rejected");
    check(c.canFly(m, -1) == Career::SRC_NONE && c.canFly(m, kAircraftCount) == Career::SRC_NONE, "out-of-range craft is rejected");
    Contract bad = m; bad.forceAircraft = 1; check(c.canFly(bad, craft) == Career::SRC_NONE, "tampered forced craft is rejected");
    bad = m; bad.id = "MIL_FORGED"; check(!c.beginMilitary(bad), "forged mission identity rejected");
    bad = m; bad.to = (bad.from + 1) % int(g_world.airports.size()); check(!c.beginMilitary(bad), "different staging destination rejected");
    bad = m; bad.from = bad.to = -1; check(!c.beginMilitary(bad), "invalid staging airport rejected");
    auto p = c.plan(m, craft, Career::SRC_MILITARY);
    check(p.src == Career::SRC_MILITARY && p.fees() == 0 && p.fuelCostEst == 0 && p.fuelUpliftKg == 0 &&
          p.fuel == Career::LaunchPlan::FUEL_INCLUDED && p.fuelLoadKg == kAircraft[craft].maxFuel, "mission quote includes loaner and full fuel without fees");
    check(c.plan(m, craft, Career::SRC_RENT).src == Career::SRC_NONE, "civilian source cannot quote military loaner");
    check(c.failureChance(Career::SRC_MILITARY, craft) == 0 && !Career::resumable(m), "loaners have no random maintenance or civilian resumable job");
    c = ready; check(c.beginMilitary(m), "begin mission"); const uint32_t token = c.military.activeAttempt;
    check(token == ready.attempt + 1 && c.attemptOpen && !c.beginMilitary(m), "one live attempt with monotonic token");
    int stars = -1; FlightResult r = success();
    const std::string active = serialized(c);
    c.settle(m, craft == 0 ? kWraith : 0, Career::SRC_MILITARY, r, &stars);
    check(stars == 0 && serialized(c) == active, "wrong craft cannot consume attempt or reward");
    c.settle(m, craft, Career::SRC_OWNED, r, &stars);
    check(serialized(c) == active, "wrong source cannot consume attempt or change civilian state");
    Contract other = c.militaryContract((kind + 1) % military::kMissionCount);
    c.settle(other, other.forceAircraft, Career::SRC_MILITARY, r, &stars);
    check(serialized(c) == active, "wrong mission kind cannot consume attempt");
    Contract ordinary = base.job->c;
    c.settle(ordinary, 0, Career::SRC_MILITARY, r, &stars);
    check(serialized(c) == active, "military source cannot settle civilian job");
    Career loaded; writeFile(active); check(loaded.load(path), "active military v6 save loads");
    check(loaded.military.activeAttempt == token && loaded.attemptOpen && loaded.military.missionSeed == c.military.missionSeed &&
          loaded.military.activeKind == kind && loaded.military.activeAirport == m.from, "active military metadata round-trips");
    const auto lines = c.settle(m, craft, Career::SRC_MILITARY, r, &stars, &p);
    check(stars == 3 && lines.size() == 1 && lines[0].amount == mission.reward, "successful mission pays service reward");
    check(c.military.credits == initial.credits + m.payout && c.military.sorties == initial.sorties + 1 &&
          c.military.successes == initial.successes + 1 && c.military.failures == initial.failures &&
          c.military.intelligence == initial.intelligence + (mission.intelligence ? 1 : 0) && c.military.hours == initial.hours + 0.25f, "military-only totals advance");
    check(!c.attemptOpen && c.military.activeAttempt == 0 && c.military.lastSettledAttempt == token &&
          c.military.activeKind == -1 && c.military.activeAirport == -1 && c.military.missionSeed == 0, "successful settlement clears active state");
    check(civilianState(c) == civil, "success leaves civilian funds, fleet, loan, license, reputation, record, job and airline untouched");
    const std::string completed = serialized(c);
    c.settle(m, craft, Career::SRC_MILITARY, r, &stars); check(serialized(c) == completed, "duplicate settlement is a no-op");
    Career done; writeFile(completed); check(done.load(path), "completed military v6 save loads");
    done.settle(m, craft, Career::SRC_MILITARY, r, &stars);
    check(serialized(done) == completed, "completed save cannot pay again after reload");
    loaded.settle(m, craft, Career::SRC_MILITARY, r, &stars);
    check(serialized(loaded) == completed, "active save settles to same completed state");
    check(c.beginMilitary(m) && c.military.activeAttempt > token, "retry creates new attempt");
    c.settle(m, craft, Career::SRC_MILITARY, r, nullptr);
    check(c.military.successes == initial.successes + 2 && c.military.credits == initial.credits + 2 * m.payout, "new attempt can earn exactly one fresh reward");
    check(c.beginMilitary(m), "every expanded mission can start again after success");
    auto failed = success(); failed.success = false; failed.outcome = OUT_ABANDONED;
    c.settle(m, craft, Career::SRC_MILITARY, failed, nullptr);
    check(c.military.sorties == initial.sorties + 3 && c.military.failures == initial.failures + 1 &&
          c.military.successes == initial.successes + 2 && c.military.credits == initial.credits + 2 * mission.reward && !c.attemptOpen, "every mission failure closes its token without rewarding");
    check(civilianState(c) == civil, "expanded repeated and failed missions preserve civilian state");
  }
  for (FlightOutcome outcome : {OUT_ABANDONED, OUT_CRASHED, OUT_DIVERTED, OUT_OFF_AIRPORT, OUT_OUT_OF_FUEL, OUT_CHECKRIDE_FAILED}) {
    Career c = base; auto m = c.militaryContract(military::LocalSurvey); check(c.beginMilitary(m), "begin failing mission");
    FlightResult r = success(); r.success = false; r.outcome = outcome; int stars = -1;
    c.settle(m, m.forceAircraft, Career::SRC_MILITARY, r, &stars);
    check(c.military.failures == 1 && c.military.successes == 0 && c.military.credits == 0 && stars == 0 && !c.attemptOpen,
          "abort/crash/diversion/fuel failure clears sortie without payout");
    check(!c.military.completedMissions && !c.militaryMissionUnlocked(military::RouteRecon),
          "every failed outcome keeps all later missions classified");
    check(civilianState(c) == civil, "failed sortie never charges repairs, loan or civilian record");
  }
  { Career c = base; auto m = c.militaryContract(0); c.beginMilitary(m); const uint32_t token = c.attempt;
    const auto active = serialized(c); Career reload; writeFile(active); check(reload.load(path), "interrupted save loads");
    reload.abandonMilitaryAttempt(); reload.abandonMilitaryAttempt();
    check(reload.military.activeAttempt == 0 && !reload.attemptOpen && reload.military.lastSettledAttempt == token &&
          reload.military.credits == 0 && reload.military.sorties == 0, "interruption cleanup is idempotent without phantom reward or result");
    int stars; reload.settle(m, 0, Career::SRC_MILITARY, success(), &stars);
    check(reload.military.credits == 0 && reload.beginMilitary(m) && reload.attempt > token, "interrupted token cannot settle; restart has a new token");
  }
  { Career c = base; c.attempt = std::numeric_limits<uint32_t>::max();
    check(!c.beginMilitary(c.militaryContract(0)), "attempt exhaustion cannot wrap replay token");
    c = base; c.attemptOpen = true; check(!c.beginMilitary(c.militaryContract(0)), "existing civilian attempt blocks new military sortie");
    check(!base.beginMilitary(base.militaryContract(-1)) && !base.beginMilitary(base.militaryContract(military::kMissionCount)), "invalid mission kind rejected");
  }
  { Career c = base; for (int i = 0; i < 3; ++i) { auto m = c.militaryContract(military::LocalSurvey); c.beginMilitary(m); c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr); }
    check(c.military.rank == 1 && c.military.successes == 3 && civilianState(c) == civil, "three successes advance service rank only");
  }
  { Career c = base; auto m = c.militaryContract(military::LocalSurvey); c.beginMilitary(m);
    FlightResult r = success(); r.outcome = OUT_CRASHED; r.flightMin = std::numeric_limits<float>::infinity();
    c.settle(m, m.forceAircraft, Career::SRC_MILITARY, r, nullptr);
    check(c.military.credits == 0 && c.military.failures == 1 && c.military.hours == 0,
          "contradictory success flag cannot reward crash; non-finite time cannot poison record");
  }
  const std::string valid = serialized(base);
  Career persistedBase = base; persistedBase.airline.log.clear(); const std::string persistedCivil = civilianState(persistedBase);
  for (int version = 1; version <= 4; ++version) {
    std::string old = replaceLine(valid, "solace_save ", "solace_save " + std::to_string(version));
    old = replaceLine(old, "military ", "");
    old = replaceLine(old, "military_progress ", "");
    if (version <= 2) { old = replaceLine(old, "attempt ", ""); old = replaceLine(old, "attempt_open ", ""); }
    Career loaded; loaded.military.credits = 123; writeFile(old);
    check(loaded.load(path), "legacy v1-v4 career migrates");
    check(loaded.military.credits == 0 && loaded.military.sorties == 0 && loaded.military.activeAttempt == 0 &&
          loaded.military.activeKind == -1 && civilianState(loaded).find("money 12600") != std::string::npos,
          "legacy migration initializes military without erasing civilian career");
    check(civilianState(loaded) == persistedCivil, "migration preserves every serialized civilian field, fleet, loan, job and airline");
    const std::string upgraded = serialized(loaded); check(upgraded.find("solace_save 6\n") == 0, "legacy career saves as v6");
    Career again; check(again.load(path), "upgraded legacy save reloads");
  }
  { // Original compact v1 career, without a fleet count, attempt marker or end marker.
    writeFile("airxpress_save 1\nmoney 900\nlicense 1\nlocation 0\nstory 0\n");
    Career old; check(old.load(path) && old.money == 900 && old.license == LIC_PPL && old.military.credits == 0 && !old.attemptOpen,
                      "original minimal v1 save migrates without inventing military progress");
  }
  { // Old aggregate success totals cannot identify which missions were actually completed.
    const Career veteran = unlockedFor(base, military::CommandStrike);
    auto old = replaceLine(serialized(veteran), "solace_save ", "solace_save 5");
    old = replaceLine(old, "military_progress ", "");
    writeFile(old); Career migrated;
    check(migrated.load(path), "v5 aggregate veteran record migrates");
    const auto& expected = veteran.military; const auto& actual = migrated.military;
    check(actual.rank == expected.rank && actual.credits == expected.credits && actual.sorties == expected.sorties &&
          actual.successes == expected.successes && actual.failures == expected.failures && actual.intelligence == expected.intelligence &&
          actual.hours == expected.hours && actual.lastSettledAttempt == expected.lastSettledAttempt,
          "v5 migration preserves every aggregate military service field");
    check(!actual.completedMissions, "v5 aggregates never fabricate mission completion identities");
    for (int id : military::kBoardMissionIds)
      check(migrated.militaryMissionUnlocked(id) == (id == military::LocalSurvey), "legacy wins and rank do not bypass first mission");
    serialized(migrated); Career again;
    check(again.load(path) && !again.military.completedMissions && again.military.successes == expected.successes,
          "migrated unknown history stays unknown through v6 restart");
  }
  for (int kind = 0; kind < military::kMissionCount; ++kind) {
    auto legacy = replaceLine(valid, "solace_save ", "solace_save 5");
    legacy = replaceLine(legacy, "military_progress ", "");
    legacy = replaceLine(legacy, "attempt ", "attempt 41");
    legacy = replaceLine(legacy, "attempt_open ", "attempt_open 1");
    legacy = replaceLine(legacy, "military ", "military 0 0 0 0 0 0 0 41 0 123 " + std::to_string(kind) + " " + std::to_string(base.location));
    writeFile(legacy); Career loaded;
    check(loaded.load(path) && loaded.military.activeKind == kind && loaded.military.activeAttempt == 41,
          "pre-expansion active v5 save retains original mission identity");
    const auto m = loaded.militaryContract(kind);
    check(m.forceAircraft == military::mission(kind)->aircraft && m.payout == military::mission(kind)->reward, "legacy active mission retains original aircraft and reward");
    check(!loaded.military.completedMissions && loaded.canFly(m, m.forceAircraft) == Career::SRC_MILITARY,
          "migration preserves exact accepted token without inventing completed predecessors");
    Career abandoned = loaded; abandoned.abandonMilitaryAttempt();
    const bool normallyUnlocked = kind == military::LegacyRecon || kind == military::LocalSurvey;
    check(abandoned.militaryMissionUnlocked(kind) == normallyUnlocked &&
          abandoned.beginMilitary(m) == normallyUnlocked && !abandoned.military.completedMissions,
          "abandoning legacy token restores ordered launch gating without a persistent exception");
    serialized(loaded); Career activeAgain;
    check(activeAgain.load(path) && activeAgain.military.activeAttempt == 41 &&
          activeAgain.canFly(m, m.forceAircraft) == Career::SRC_MILITARY,
          "accepted legacy token survives v6 serialization until explicitly closed");
    loaded.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
    check(loaded.military.credits == military::mission(kind)->reward && loaded.military.lastSettledAttempt == 41 && !loaded.attemptOpen,
          "pre-expansion active mission settles once using original meaning");
    check(loaded.military.completedMissions == military::completionBit(kind) && loaded.militaryMissionUnlocked(kind),
          "legacy active success records and makes replayable only its genuine mission identity");
    for (int id : military::kBoardMissionIds) {
      const bool expected = id == military::LocalSurvey || id == kind ||
        (kind == military::LocalSurvey && id == military::RouteRecon);
      check(loaded.militaryMissionUnlocked(id) == expected, "out-of-order legacy success cannot unlock over missing predecessors");
    }
    serialized(loaded); Career sparse;
    check(sparse.load(path) && sparse.military.completedMissions == military::completionBit(kind),
          "sparse genuine legacy completion remains loadable and preserved");
  }
  rejects(replaceLine(valid, "military ", ""), base, "v6 requires military record");
  rejects(replaceLine(valid, "military_progress ", ""), base, "v6 requires explicit completion history");
  rejects(replaceLine(valid, "military_progress ", "military_progress 0\nmilitary_progress 0"), base,
          "duplicate completion records rejected");
  for (const char* bits : {"-1", "1024", "4294967296", "-4294967296", "18446744073709551616", "nan", "+0", "0suffix", "0 1"})
    rejects(replaceLine(valid, "military_progress ", std::string("military_progress ") + bits), base,
            "unknown, negative, overflowing or malformed completion bits rejected");
  rejects(replaceLine(valid, "military_progress ", "military_progress 8"), base,
          "completion without a corresponding recorded success rejected before ledger saturation");
  rejects(replaceLine(valid, "solace_save ", "solace_save 5"), base, "v5 does not accept a disguised v6 completion field");
  const std::string emptyRecord = "military 0 0 0 0 0 0 0 0 0 0 -1 -1";
  rejects(replaceLine(valid, "military ", emptyRecord + "\n" + emptyRecord), base, "duplicate military record rejected");
  const char* corrupt[] = {
    "military -1 0 0 0 0 0 0 0 0 0 -1 -1", "military 11 0 0 0 0 0 0 0 0 0 -1 -1",
    "military 0 -1 0 0 0 0 0 0 0 0 -1 -1", "military 0 0 1 0 0 0 0 0 0 0 -1 -1",
    "military 0 0 0 0 0 1 0 0 0 0 -1 -1", "military 0 0 0 0 0 0 nan 0 0 0 -1 -1",
    "military 0 0 0 0 0 0 inf 0 0 0 -1 -1", "military 0 0 0 0 0 0 -1 0 0 0 -1 -1",
    "military 0 0 0 0 0 0 0 0 0 1 -1 -1", "military 0 0 0 0 0 0 0 0 0 0 0 -1",
    "military 0 0 0 0 0 0 0 0 0 0 -1 0", "military 0 0 0 0 0 0 0 0 999 0 -1 -1",
    "military 0 0 0 0 0 0 0 1 0 5 0 0", "military 0 0 0"
  };
  for (const char* record : corrupt) rejects(replaceLine(valid, "military ", record), base, "corrupt military field rejected");
  { Career active = base; active.beginMilitary(active.militaryContract(military::LocalSurvey)); const auto a = serialized(active);
    rejects(replaceLine(a, "attempt_open ", "attempt_open 0"), base, "active record requires open attempt");
    rejects(replaceLine(a, "attempt ", "attempt 999"), base, "active token must match global attempt");
    auto corruptActive = [&](int kind, int airport, uint32_t last) {
      std::ostringstream s; s << "military 0 0 0 0 0 0 0 " << active.attempt << ' ' << last << " 77 " << kind << ' ' << airport;
      rejects(replaceLine(a, "military ", s.str()), base, "invalid active kind, airport or consumed token rejected");
    };
    corruptActive(military::kMissionCount, base.location, 0); corruptActive(-1, base.location, 0);
    corruptActive(1, -1, 0); corruptActive(1, int(g_world.airports.size()), 0); corruptActive(1, base.location, active.attempt);
  }
  { auto malformed = valid; const auto pos = malformed.find("contract F_MIL_ISOLATION 1 ");
    check(pos != std::string::npos, "civilian contract fixture located");
    if (pos != std::string::npos) malformed.replace(pos, std::string("contract F_MIL_ISOLATION 1 ").size(), "contract F_MIL_ISOLATION 11 ");
    rejects(malformed, base, "military contract cannot masquerade as a saved civilian job");
  }
  for (int kind = 0; kind < military::kMissionCount; ++kind) { Career c = unlockedFor(base, kind);
    c.military.sorties = 2000000000; c.military.successes = 1999999999; c.military.failures = 1; c.military.rank = 10;
    c.military.intelligence = c.military.successes; c.military.credits = 1999999999;
    auto m = c.militaryContract(kind); c.beginMilitary(m); auto r = success();
    if (kind == 2) { r.success = false; r.outcome = OUT_CRASHED; }
    c.settle(m, m.forceAircraft, Career::SRC_MILITARY, r, nullptr);
    check(int64_t(c.military.successes) + c.military.failures == c.military.sorties &&
          c.military.intelligence <= c.military.successes && c.military.credits <= 2000000000,
          "saturated success, failure, intelligence and credit totals stay mutually consistent");
    serialized(c); Career loaded; check(loaded.load(path), "counter saturation must preserve loadable coherent military totals");
  }
  { Career c = base; c.military.sorties = c.military.failures = 2000000000;
    const auto m = c.militaryContract(military::LocalSurvey);
    check(c.beginMilitary(m), "all-failures saturated ledger still permits next mission");
    c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr);
    check(c.military.successes == 0 && c.military.failures == 2000000000 &&
          c.militaryMissionCompleted(military::LocalSurvey) && c.militaryMissionUnlocked(military::RouteRecon),
          "frozen aggregate ledger does not freeze genuine new mission completion");
    serialized(c); Career loaded;
    check(loaded.load(path) && loaded.militaryMissionCompleted(military::LocalSurvey) &&
          loaded.militaryMissionUnlocked(military::RouteRecon), "all-failures saturation preserves loadable mission progress");
  }
  remove(path); remove("military_career_test.sav.bak"); remove("military_career_test.sav.tmp");
  printf("Military career: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
