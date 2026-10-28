// The career on the real Earth (docs/REAL_EARTH_PLAN.md, E7), over the test fixture's pack: a new career takes the
// real Earth beyond the islands; its travel lists real airports, each at its own chart; the islands' board carries a
// long haul to one of them, flown out of the islands on the real Earth's charts, arriving in the chart about that
// airport and delivered there - the career goes on from that field. The save keeps it (version 8), loads it again,
// and a career whose chart can't be read without the pack is taken home rather than lost.
#include "../src/game.h"
#include "../src/earth_charts.h"
#include "test_world.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

struct GameTest {
  static int run(const std::string& dir) {
    int failed = 0;
    auto check = [&](bool pass, const std::string& label) {
      printf("Career on the Earth: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass;
    };
    std::string why;
    auto pack = earth::Pack::open(dir, &why);
    check(pack != nullptr, "the pack opens " + why);
    if (!pack) return 1;
    earthCharts::usePack(pack);
    const fs::path root = fs::temp_directory_path() / ("earth_career_test_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    buildTestWorld();
    const std::string stamp = "earth-career-v1";
    g_world.saveCache((root / "world.bin").string(), stamp);
    Game game; game.initHeadless(); game.botControl = true; game.set.traffic = false;
    game.cacheDir = root.string(); game.buildStamp = stamp; game.saveDir = root.string();

    // a new career: the real Earth beyond the islands
    int jet = -1; for (int t : kCareerAircraft) if (!strcmp(kAircraft[t].id, "emissary")) jet = t;
    game.career = Career{}; game.career.newGame();
    check(game.career.realEarth && game.career.atHome(), "a new career takes the real Earth (the pack is there)");
    game.career.license = LIC_ATP; game.career.money = 400000;
    game.career.location = g_world.findAirport("PVI");
    game.career.fleet = {{jet, game.career.location, kAircraft[jet].maxFuel, 1.f}};
    // its travel: real airports, each at its chart, nearest first
    const auto dests = game.career.destinations();
    bool allReal = !dests.empty(), named = true, ordered = true;
    double last = 0;
    for (const auto& d : dests) {
      allReal &= earthCharts::isEarthId(d.id) && !d.home;
      const Airport* a = procedural::airportOf(d.id, d.airport);
      named &= a && strlen(a->code) >= 3 && std::max(fabsf(a->x), fabsf(a->z)) < WORLD_HALF;
      ordered &= d.km >= last; last = d.km;
    }
    check(allReal && named && ordered, fmt("travel: %zu real airports, each in its chart, nearest first (%s %.0f km)", dests.size(),
                                         dests.empty() ? "-" : dests[0].name.c_str(), dests.empty() ? 0. : dests[0].km));
    // the board: a long haul to one of them
    game.career.refreshBoard();
    Contract job;
    for (const Contract& c : game.career.board) if (c.longHaul() && earthCharts::isEarthId(c.toRegion) && job.id.empty()) job = c;
    check(!job.id.empty() && game.career.canFly(job, jet) == Career::SRC_OWNED, "a long haul to a real airport on the islands' board " + job.title);
    if (job.id.empty()) return 1;
    const Airport* target = procedural::airportOf(job.toRegion, job.to);
    const auto toChart = procedural::find(job.toRegion);
    check(target && toChart, fmt("to %s %s, %.0f km", target ? target->code : "?", target ? target->name : "?", job.haulKm));
    if (!target || !toChart) return 1;
    const std::string code = target->code;
    // flown: out of the islands on the real Earth
    game.beginCareerFlight(job, jet, Career::SRC_OWNED);
    check(game.screen == SCR_FLIGHT && game.haulPending() && g_world.regionId == "solace" && game.realEarthPlay(), "launched from the islands, on the real Earth");
    {
      const Airport& from = g_world.airports[job.from];
      const float far = length(vec3(game.dest().x - from.x, 0, game.dest().z - from.z)) / 1000.f;
      check(std::abs(far - job.haulKm) < 15.f && code == game.dest().code, fmt("its airport charted from the islands (%.0f km)", far));
    }
    // placed aloft 50 km short of it, as if flown there: the chart about it prepared and moved into
    {
      const geo::Position home = geo::solaceOrigin(), there = airportPlace(job.toRegion, *target);
      const double bearing = *geo::initialBearing(there, home);
      geo::Position near = geo::destination(there, bearing, 50000.0); near.height = 1800.;
      const regionTravel::Chart chart(game.flightRegion.origin);
      game.plane.pos = chart.local(near);
      const vec3 ahead = chart.local(there) - game.plane.pos;
      game.plane.vel = normalize(vec3(ahead.x, 0, ahead.z)) * 120.f;
      game.plane.onGround = false; game.takeoffAnnounced = true;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
      bool onlyEarth = true;
      while (!game.haul.arrived && std::chrono::steady_clock::now() < deadline) {
        game.updateEarthFlight();
        onlyEarth &= game.earthPrefetchId.empty() || game.earthPrefetchId == "solace" || earthCharts::isEarthId(game.earthPrefetchId);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      check(onlyEarth, "never a generated region wanted");
      check(game.haul.arrived && g_world.regionId == job.toRegion && game.contract.to == job.to && !strcmp(game.dest().code, code.c_str()),
            "arrived in the chart about its airport, the destination its own runway " + g_world.regionId);
    }
    const int money = game.career.money;
    game.endFlight(true, "");
    check(game.career.region == job.toRegion && game.career.location == job.to && game.career.money > money &&
          game.career.fleet[0].region == job.toRegion && game.career.fleet[0].location == job.to, "delivered: the career and its aircraft there");
    check(game.careerPlaceName() == target->name, "the hub names the field: " + game.careerPlaceName());
    // the save keeps it
    const std::string save = (root / "career.sav").string();
    check(game.career.save(save), "saved");
    {
      std::ifstream in(save); std::string head; std::getline(in, head);
      check(head == "somnium_save 8", "as version 8 (" + head + ")");
      Career back;
      check(back.load(save) && back.realEarth && back.region == job.toRegion && back.location == job.to &&
            back.fleet.size() == 1 && back.fleet[0].region == job.toRegion, "loaded again, where it was");
      // from there: home among the destinations, first
      const auto onward = back.destinations();
      check(!onward.empty() && onward[0].home, "home first among the destinations from there");
      std::string m;
      const bool home = back.travel(onward[0], &m) && back.atHome();
      check(home, "flown home: " + m);
      const bool kept = !back.chooseWorld(false, &m);
      check(kept, "the world beyond kept while an aircraft is parked out there: " + m);
    }
    // a career whose chart can't be read (its pack gone): taken home, not lost
    {
      std::ifstream in(save); std::stringstream all; all << in.rdbuf();
      std::string text = all.str();
      const std::string other = "earth:1000:-2000";   // (a chart nothing has read)
      for (size_t at; (at = text.find(job.toRegion)) != std::string::npos;) text.replace(at, job.toRegion.size(), other);
      const std::string lost = (root / "lost.sav").string();
      { std::ofstream out(lost); out << text; }
      earthCharts::usePack(nullptr);
      Career back;
      check(back.load(lost) && back.atHome() && back.location >= 0 && back.location < solaceAirportCount() &&
            back.fleet.size() == 1 && back.fleet[0].region == "solace", "without the pack: loaded at home, its aircraft too");
      earthCharts::usePack(pack);
    }
    // its board there: cross-country work (Contract::crossCountry) - small fields beyond the chart, each in its own -
    // and the nearest flown: out of this chart, into the one about its field, delivered there
    {
      game.career.refreshBoard();
      const auto mine = procedural::find(game.career.region);
      const regionTravel::Chart here(mine ? mine->origin : geo::solaceOrigin());
      // (a small field: one the airline's travel doesn't list)
      const auto airline = game.career.destinations();
      auto served = [&](const Contract& c) { for (const auto& d : airline) if (d.id == c.toRegion && d.airport == c.to) return true; return false; };
      int cross = 0, small = 0; bool beyond = true, named = true; Contract x;
      for (const Contract& c : game.career.board) {
        if (!c.crossCountry()) continue;
        cross++;
        named &= c.title.rfind("Cross-country: ", 0) == 0;
        const Airport* B = procedural::airportOf(c.toRegion, c.to);
        const vec3 at = B ? here.local(airportPlace(c.toRegion, *B)) : vec3(0.f);
        beyond &= B && std::max(fabsf(at.x), fabsf(at.z)) >= WORLD_HALF && c.haulKm >= 30.f && c.haulKm <= 200.f;
        if (served(c)) continue;
        small++;
        if (x.id.empty() || c.haulKm < x.haulKm) x = c;
      }
      // (an isolated field - Bermuda, the global pack's nearest to the islands - has no small field within 180 km and no
      // other in its chart: nothing to draw a cross-country from, so its board holds the airline's long hauls alone)
      const bool isolated = cross == 0 && procedural::airportCount(game.career.region) <= 1;
      if (isolated) printf("Career on the Earth: its board there: no cross-country work (no field within reach of %s): skipped\n", game.careerPlaceName().c_str());
      else check(small > 0 && beyond && named, fmt("its board there: %d cross-country jobs, %d to small fields beyond the chart (%s, %.0f km)", cross, small, x.title.c_str(), x.haulKm));
      int si = -1; Career::Source src = Career::SRC_NONE;
      for (int t : kCareerAircraft) if (si < 0 && !x.id.empty() && (src = game.career.canFly(x, t)) != Career::SRC_NONE) si = t;
      if (!isolated) check(si >= 0, "an aircraft for it");
      if (si >= 0) {
        const Airport* B = procedural::airportOf(x.toRegion, x.to);
        const std::string field = B->code;
        game.beginCareerFlight(x, si, src);
        check(game.screen == SCR_FLIGHT && game.haulPending() && game.realEarthPlay() && field == game.dest().code,
              "flown from there, on the real Earth, to " + field);
        const geo::Position there = airportPlace(x.toRegion, *B), from = regionTravel::Chart(game.flightRegion.origin).position(game.plane.pos);
        geo::Position near = geo::destination(there, *geo::initialBearing(there, from), 12000.0); near.height = 900.;
        const regionTravel::Chart chart(game.flightRegion.origin);
        game.plane.pos = chart.local(near);
        const vec3 ahead = chart.local(there) - game.plane.pos;
        game.plane.vel = normalize(vec3(ahead.x, 0, ahead.z)) * 70.f;
        game.plane.onGround = false; game.takeoffAnnounced = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
        while (!game.haul.arrived && std::chrono::steady_clock::now() < deadline) {
          game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(game.haul.arrived && g_world.regionId == x.toRegion && !strcmp(game.dest().code, field.c_str()),
              "moved into the chart about its field, the destination its runway " + g_world.regionId);
        const int had = game.career.money;
        game.endFlight(true, "");
        check(game.career.region == x.toRegion && game.career.location == x.to && game.career.money > had, "delivered: the career there");
      }
    }
    // an old career (a save with no world line) keeps the generated regions until it is changed, at home
    {
      Career old; old.newGame(); old.realEarth = false;
      std::string m;
      const bool changed = old.chooseWorld(true, &m) && old.realEarth;
      check(changed, "an old career changed to the real Earth, at home: " + m);
    }
    game.retireEarthPrefetch();
    if (game.earthRetireJob.valid()) game.earthRetireJob.get();
    std::error_code ec; fs::remove_all(root, ec);
    printf("earth_career_test: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
  }
};

int main(int argc, char** argv) { return GameTest::run(argc > 1 ? argv[1] : "tests/fixtures/earth2"); }
