// Military career lifecycle: supplied loaners, isolated service accounting, migration and replay safety.
#include "../src/career.h"
#include "test_world.h"
#include <cstdio>
#include <fstream>
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
    if (line.rfind("military ", 0) && line.rfind("attempt ", 0) && line.rfind("attempt_open ", 0)) out << line << '\n';
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
static void rejects(const std::string& text, const Career& baseline, const char* label) {
  Career probe = baseline; const std::string before = serialized(probe); writeFile(text);
  check(!probe.load(path), label); check(serialized(probe) == before, "rejected save does not modify live career");
}
int main() {
  buildTestWorld(); buildStory();
  // Appended enums preserve all pre-military contract/source values.
  check(CT_TRIAL == 10 && CT_MIL_RECON == 11 && Career::SRC_OWNED == 3 && Career::SRC_MILITARY == 4,
        "legacy enum values are stable");
  Career base = populated(); const std::string civil = civilianState(base);
  for (int kind = 0; kind != 3; ++kind) {
    Career c = base; Contract m = c.militaryContract(kind); const int craft = kind == 0 ? 0 : kWraith;
    check(m.type == CT_MIL_RECON + kind && m.forceAircraft == craft, "mission selects recon trainer or XR-40 combat loaner");
    c.license = LIC_STUDENT; c.money = -10000; c.fleet.clear(); c.airline.routes.clear();
    check(c.canFly(m, craft) == Career::SRC_MILITARY, "loaner needs no license, money or owned craft");
    check(c.canFly(m, craft == 0 ? kWraith : 0) == Career::SRC_NONE, "wrong craft is rejected");
    check(c.canFly(m, -1) == Career::SRC_NONE && c.canFly(m, kAircraftCount) == Career::SRC_NONE, "out-of-range craft is rejected");
    Contract bad = m; bad.forceAircraft = 1; check(c.canFly(bad, craft) == Career::SRC_NONE, "tampered forced craft is rejected");
    bad = m; bad.to = (bad.from + 1) % int(g_world.airports.size()); check(!c.beginMilitary(bad), "different staging destination rejected");
    bad = m; bad.from = bad.to = -1; check(!c.beginMilitary(bad), "invalid staging airport rejected");
    auto p = c.plan(m, craft, Career::SRC_MILITARY);
    check(p.src == Career::SRC_MILITARY && p.fees() == 0 && p.fuelCostEst == 0 && p.fuelUpliftKg == 0 &&
          p.fuel == Career::LaunchPlan::FUEL_INCLUDED && p.fuelLoadKg == kAircraft[craft].maxFuel, "mission quote includes loaner and full fuel without fees");
    check(c.plan(m, craft, Career::SRC_RENT).src == Career::SRC_NONE, "civilian source cannot quote military loaner");
    check(c.failureChance(Career::SRC_MILITARY, craft) == 0 && !Career::resumable(m), "loaners have no random maintenance or civilian resumable job");
    c = base; check(c.beginMilitary(m), "begin mission"); const uint32_t token = c.military.activeAttempt;
    check(token == base.attempt + 1 && c.attemptOpen && !c.beginMilitary(m), "one live attempt with monotonic token");
    int stars = -1; FlightResult r = success();
    const std::string active = serialized(c);
    c.settle(m, craft == 0 ? kWraith : 0, Career::SRC_MILITARY, r, &stars);
    check(stars == 0 && serialized(c) == active, "wrong craft cannot consume attempt or reward");
    c.settle(m, craft, Career::SRC_OWNED, r, &stars);
    check(serialized(c) == active, "wrong source cannot consume attempt or change civilian state");
    Contract other = c.militaryContract((kind + 1) % 3);
    c.settle(other, other.forceAircraft, Career::SRC_MILITARY, r, &stars);
    check(serialized(c) == active, "wrong mission kind cannot consume attempt");
    Contract ordinary = base.job->c;
    c.settle(ordinary, 0, Career::SRC_MILITARY, r, &stars);
    check(serialized(c) == active, "military source cannot settle civilian job");
    Career loaded; writeFile(active); check(loaded.load(path), "active military v5 save loads");
    check(loaded.military.activeAttempt == token && loaded.attemptOpen && loaded.military.missionSeed == c.military.missionSeed &&
          loaded.military.activeKind == kind && loaded.military.activeAirport == m.from, "active military metadata round-trips");
    const auto lines = c.settle(m, craft, Career::SRC_MILITARY, r, &stars, &p);
    check(stars == 3 && lines.size() == 1 && lines[0].amount == 600 + 600 * kind, "successful mission pays service reward");
    check(c.military.credits == m.payout && c.military.sorties == 1 && c.military.successes == 1 && c.military.failures == 0 &&
          c.military.intelligence == (kind == 0 ? 1 : 0) && c.military.hours == 0.25f, "military-only totals advance");
    check(!c.attemptOpen && c.military.activeAttempt == 0 && c.military.lastSettledAttempt == token &&
          c.military.activeKind == -1 && c.military.activeAirport == -1 && c.military.missionSeed == 0, "successful settlement clears active state");
    check(civilianState(c) == civil, "success leaves civilian funds, fleet, loan, license, reputation, record, job and airline untouched");
    const std::string completed = serialized(c);
    c.settle(m, craft, Career::SRC_MILITARY, r, &stars); check(serialized(c) == completed, "duplicate settlement is a no-op");
    Career done; writeFile(completed); check(done.load(path), "completed military v5 save loads");
    done.settle(m, craft, Career::SRC_MILITARY, r, &stars);
    check(serialized(done) == completed, "completed save cannot pay again after reload");
    loaded.settle(m, craft, Career::SRC_MILITARY, r, &stars);
    check(serialized(loaded) == completed, "active save settles to same completed state");
    check(c.beginMilitary(m) && c.military.activeAttempt > token, "retry creates new attempt");
    c.settle(m, craft, Career::SRC_MILITARY, r, nullptr);
    check(c.military.successes == 2 && c.military.credits == 2 * m.payout, "new attempt can earn exactly one fresh reward");
  }
  for (FlightOutcome outcome : {OUT_ABANDONED, OUT_CRASHED, OUT_DIVERTED, OUT_OFF_AIRPORT, OUT_OUT_OF_FUEL}) {
    Career c = base; auto m = c.militaryContract(1); check(c.beginMilitary(m), "begin failing mission");
    FlightResult r = success(); r.success = false; r.outcome = outcome; int stars = -1;
    c.settle(m, kWraith, Career::SRC_MILITARY, r, &stars);
    check(c.military.failures == 1 && c.military.successes == 0 && c.military.credits == 0 && stars == 0 && !c.attemptOpen,
          "abort/crash/diversion/fuel failure clears sortie without payout");
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
    check(!base.beginMilitary(base.militaryContract(-1)) && !base.beginMilitary(base.militaryContract(3)), "invalid mission kind rejected");
  }
  { Career c = base; for (int i = 0; i < 3; ++i) { auto m = c.militaryContract(i); c.beginMilitary(m); c.settle(m, m.forceAircraft, Career::SRC_MILITARY, success(), nullptr); }
    check(c.military.rank == 1 && c.military.successes == 3 && civilianState(c) == civil, "three successes advance service rank only");
  }
  { Career c = base; auto m = c.militaryContract(1); c.beginMilitary(m);
    FlightResult r = success(); r.outcome = OUT_CRASHED; r.flightMin = std::numeric_limits<float>::infinity();
    c.settle(m, kWraith, Career::SRC_MILITARY, r, nullptr);
    check(c.military.credits == 0 && c.military.failures == 1 && c.military.hours == 0,
          "contradictory success flag cannot reward crash; non-finite time cannot poison record");
  }
  const std::string valid = serialized(base);
  Career persistedBase = base; persistedBase.airline.log.clear(); const std::string persistedCivil = civilianState(persistedBase);
  for (int version = 1; version <= 4; ++version) {
    std::string old = replaceLine(valid, "solace_save ", "solace_save " + std::to_string(version));
    old = replaceLine(old, "military ", "");
    if (version <= 2) { old = replaceLine(old, "attempt ", ""); old = replaceLine(old, "attempt_open ", ""); }
    Career loaded; loaded.military.credits = 123; writeFile(old);
    check(loaded.load(path), "legacy v1-v4 career migrates");
    check(loaded.military.credits == 0 && loaded.military.sorties == 0 && loaded.military.activeAttempt == 0 &&
          loaded.military.activeKind == -1 && civilianState(loaded).find("money 12600") != std::string::npos,
          "legacy migration initializes military without erasing civilian career");
    check(civilianState(loaded) == persistedCivil, "migration preserves every serialized civilian field, fleet, loan, job and airline");
    const std::string upgraded = serialized(loaded); check(upgraded.find("solace_save 5\n") == 0, "legacy career saves as v5");
    Career again; check(again.load(path), "upgraded legacy save reloads");
  }
  { // Original compact v1 career, without a fleet count, attempt marker or end marker.
    writeFile("airxpress_save 1\nmoney 900\nlicense 1\nlocation 0\nstory 0\n");
    Career old; check(old.load(path) && old.money == 900 && old.license == LIC_PPL && old.military.credits == 0 && !old.attemptOpen,
                      "original minimal v1 save migrates without inventing military progress");
  }
  rejects(replaceLine(valid, "military ", ""), base, "v5 requires military record");
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
  { Career active = base; active.beginMilitary(active.militaryContract(1)); const auto a = serialized(active);
    rejects(replaceLine(a, "attempt_open ", "attempt_open 0"), base, "active record requires open attempt");
    rejects(replaceLine(a, "attempt ", "attempt 999"), base, "active token must match global attempt");
    auto corruptActive = [&](int kind, int airport, uint32_t last) {
      std::ostringstream s; s << "military 0 0 0 0 0 0 0 " << active.attempt << ' ' << last << " 77 " << kind << ' ' << airport;
      rejects(replaceLine(a, "military ", s.str()), base, "invalid active kind, airport or consumed token rejected");
    };
    corruptActive(3, base.location, 0); corruptActive(-1, base.location, 0);
    corruptActive(1, -1, 0); corruptActive(1, int(g_world.airports.size()), 0); corruptActive(1, base.location, active.attempt);
  }
  { auto malformed = valid; const auto pos = malformed.find("contract F_MIL_ISOLATION 1 ");
    check(pos != std::string::npos, "civilian contract fixture located");
    if (pos != std::string::npos) malformed.replace(pos, std::string("contract F_MIL_ISOLATION 1 ").size(), "contract F_MIL_ISOLATION 11 ");
    rejects(malformed, base, "military contract cannot masquerade as a saved civilian job");
  }
  for (int kind = 0; kind < 3; ++kind) { Career c = base;
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
  remove(path); remove("military_career_test.sav.bak"); remove("military_career_test.sav.tmp");
  printf("Military career: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
