// The career save format is frozen. A fixed career holding every kind of record a save can (the fleet, a loan, the
// insurance, the airline's pilots and routes, the military record, an open freelance job with its forecast and
// waypoints) must write exactly the checked-in tests/fixtures/career_v6.sav, and that file must load and write back
// the same bytes. A career with something beyond the islands (its region, an aircraft parked in one) writes version 7,
// frozen the same way in tests/fixtures/career_v7.sav; one that never left them still writes version 6. A change to the writer fails here: bump the version in Career::save, keep loading every older one,
// and add the new version's fixture beside this one (SOMNIUM_UPDATE_FIXTURES=1 writes it). Players' saves from every
// released version have to keep working.
#include "../src/career.h"
#include "../src/procedural_regions.h"
#include "test_world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { printf("  !! %s\n", what); fails++; } }
// the file's text with its line ends as \n: Windows writes a save in text mode (\r\n), and the fixture is checked out with
// either (the loader reads both: the format is its lines)
static std::string readFile(const std::string& p) {
  std::string s; FILE* f = fopen(p.c_str(), "rb"); if (!f) return s;
  char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f);
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

// equal, or where they part: the first line that differs
static bool same(const std::string& got, const std::string& want) {
  if (got == want) return true;
  size_t a = 0;
  while (a < got.size() && a < want.size() && got[a] == want[a]) a++;
  const size_t ls = a == 0 ? 0 : got.rfind('\n', a - 1) == std::string::npos ? 0 : got.rfind('\n', a - 1) + 1;
  printf("  first difference at byte %zu: wrote \"%s\", frozen \"%s\"\n", a, got.substr(ls, got.find('\n', a) - ls).c_str(), want.substr(ls, want.find('\n', a) - ls).c_str());
  return false;
}

static Career fixedCareer() {
  Career c; c.newGame();
  c.money = 123456; c.license = LIC_ATP; c.reputation = 42; c.location = 2; c.storyIndex = 7;
  c.flights = 31; c.landings = 29; c.crashes = 1; c.hours = 17.25f; c.bestLandingFpm = 64.5f; c.boardSeed = 4242;
  c.attempt = 12; c.attemptOpen = false;
  c.military.rank = 1; c.military.credits = 300; c.military.sorties = 5; c.military.successes = 4; c.military.failures = 1;
  c.military.intelligence = 3; c.military.hours = 3.5f; c.military.lastSettledAttempt = 11;
  c.military.completedMissions = 0x03u;
  auto half = [](int spec) { return std::floor(kAircraft[spec].maxFuel * .5f); };   // (whole kilograms, within the tanks)
  c.fleet = {{1, 2, half(1), 0.92f}, {6, 3, half(6), 1.f}, {kGalleon, 2, half(kGalleon), 0.75f}};
  c.loan = {kGalleon, 50000, 2500, 1, .08f};
  c.insured = true;
  Career::Pilot p; p.name = "R. Mendez"; p.rating = 2; p.wage = 300;
  c.airline.pilots = {p};
  Career::Route r; r.fleetIdx = 1; r.from = 3; r.to = 4; r.pilot = 0; r.flights = 5; r.earned = 4200; r.progressMin = 12.5f;
  c.airline.routes = {r}; c.airline.earned = 4200; c.airline.incidents = 1;
  Career::JobState J;
  J.c.id = "F4242_3"; J.c.type = CT_FERRY; J.c.from = 2; J.c.to = 5; J.c.cargoKg = 350; J.c.pax = 0; J.c.payout = 4100; J.c.timeLimitMin = 75;
  J.c.minLicense = LIC_PPL; J.c.title = "Spare parts to the coast"; J.c.brief = "A crate of spares, needed before dark.";
  J.c.wx.windFrom = 240; J.c.wx.windSpeed = 9; J.c.wx.gust = 3; J.c.wx.cloudCover = .4f; J.c.wx.cloudBase = 900; J.c.wx.visibility = 20000; J.c.wx.timeOfDay = 15.5f;
  J.c.wxShift = true; J.c.wxEnd = J.c.wx; J.c.wxEnd.windFrom = 260; J.c.wxEnd.precip = 1;
  J.c.wps = {{1000.f, -2000.f, 600.f}, {4500.f, 800.f, 900.f}};
  J.spec = 1; J.src = Career::SRC_OWNED; J.state = Career::JobState::ACTIVE; J.at = 2; J.legs = 1; J.wpDone = 1; J.jobClockMin = 22.5f;
  J.maxG = 1.8f; J.minG = .4f; J.maxBank = 38; J.fuelBilledKg = 0; J.id = 77;
  J.plan.spec = 1; J.plan.src = Career::SRC_OWNED; J.plan.startAirport = 2; J.plan.fuelKgEst = 41; J.plan.minutesEst = 31; J.plan.minutesSigma = 4;
  J.plan.fuelLoadKg = half(1); J.plan.flown = true;
  c.job = J;
  return c;
}

// the same career away: in the Umber Islands (a large generated archipelago), its job there, one aircraft parked there
// and the route's still in the islands
static Career awayCareer() {
  Career c = fixedCareer();
  c.region = "pr1-0-0-17"; c.location = 4;
  c.fleet[0].region = c.region; c.fleet[0].location = 4;
  c.job->c.from = 4; c.job->c.to = 6; c.job->at = 4; c.job->plan.startAirport = 4;
  return c;
}

int main() {
  buildTestWorld(); buildStory();
  const std::string fixture = std::string(SOMNIUM_SOURCE_DIR) + "/tests/fixtures/career_v6.sav";
  const char* tmp = "save_format_test.sav";
  const Career c = fixedCareer();
  check(c.save(tmp), "the fixed career writes");
  const std::string got = readFile(tmp);
  if (getenv("SOMNIUM_UPDATE_FIXTURES")) {
    if (FILE* f = fopen(fixture.c_str(), "wb")) { fwrite(got.data(), 1, got.size(), f); fclose(f); printf("wrote %s\n", fixture.c_str()); }
  }
  const std::string want = readFile(fixture);
  check(!want.empty(), "the fixture is there (tests/fixtures/career_v6.sav)");
  check(got.rfind("somnium_save 6\n", 0) == 0, "the writer is still version 6 (a new version needs its own fixture, and v6 must keep loading)");
  check(same(got, want), "the fixed career writes exactly the frozen v6 bytes");
  // the frozen file loads, keeps every record, and writes the same bytes back
  Career r; r.newGame();
  check(r.load(fixture), "the frozen v6 save loads");
  check(r.money == 123456 && r.license == LIC_ATP && r.fleet.size() == 3 && r.fleet[2].spec == kGalleon && r.loan.open() && r.insured, "its career, fleet, loan and insurance");
  check(r.airline.pilots.size() == 1 && r.airline.pilots[0].name == "R. Mendez" && r.airline.routes.size() == 1 && r.airline.routes[0].earned == 4200, "its airline");
  check(r.military.rank == 1 && r.military.completedMissions == 0x03u, "its military record");
  check(r.job && r.job->c.id == "F4242_3" && r.job->c.wxShift && r.job->c.wps.size() == 2 && r.job->c.brief == "A crate of spares, needed before dark.", "its open job, forecast and waypoints");
  check(r.save(tmp) && same(readFile(tmp), want), "loaded and saved again, the same bytes");
  {   // version 7: the career away
    const std::string fixture7 = std::string(SOMNIUM_SOURCE_DIR) + "/tests/fixtures/career_v7.sav";
    const Career a = awayCareer();
    check(a.save(tmp), "the career away writes");
    const std::string got7 = readFile(tmp);
    if (getenv("SOMNIUM_UPDATE_FIXTURES")) {
      if (FILE* f = fopen(fixture7.c_str(), "wb")) { fwrite(got7.data(), 1, got7.size(), f); fclose(f); printf("wrote %s\n", fixture7.c_str()); }
    }
    const std::string want7 = readFile(fixture7);
    check(!want7.empty(), "the fixture is there (tests/fixtures/career_v7.sav)");
    check(got7.rfind("somnium_save 7\nregion pr1-0-0-17\n", 0) == 0, "a career away writes version 7, its region first");
    check(same(got7, want7), "the career away writes exactly the frozen v7 bytes");
    Career b; b.newGame();
    check(b.load(fixture7), "the frozen v7 save loads");
    check(b.region == "pr1-0-0-17" && b.location == 4 && b.fleet[0].region == "pr1-0-0-17" && b.fleet[0].location == 4 && b.fleet[1].region == "solace" &&
          b.airline.routes.size() == 1 && b.job && b.job->at == 4 && b.job->c.to == 6, "its region, its aircraft's regions, the route in the islands, the job there");
    check(b.save(tmp) && same(readFile(tmp), want7), "loaded and saved again, the same bytes");
    // a region's airport the save names but its recipe no longer has: the career at its main airport, the job released
    std::string moved = want7;
    auto swap = [&](const std::string& from, const std::string& to) { const size_t i = moved.find(from); if (i != std::string::npos) moved.replace(i, from.size(), to); };
    swap("\nlocation 4\n", "\nlocation 9\n");
    swap("\ncontract F4242_3 6 4 6 ", "\ncontract F4242_3 6 4 9 ");
    if (FILE* f = fopen(tmp, "wb")) { fwrite(moved.data(), 1, moved.size(), f); fclose(f); }
    Career m; m.newGame();
    check(m.load(tmp) && m.region == "pr1-0-0-17" && m.location == procedural::mainAirport("pr1-0-0-17") && !m.job,
          "an airport the region no longer has: the career waits at its main airport, the job's released");
  }
  remove(tmp); remove((std::string(tmp) + ".bak").c_str()); remove((std::string(tmp) + ".tmp").c_str());
  printf("Save format v6 and v7: %s\n", fails ? "FAIL" : "frozen");
  return fails ? 1 : 0;
}
