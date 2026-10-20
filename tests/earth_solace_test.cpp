// The Solace Islands in the real Atlantic (docs/REAL_EARTH_PLAN.md, E6), over the test fixture's pack: a Free Flight
// out of the islands with the pack there goes on into the real Earth's charts - over open water, never a generated
// region - and back into the islands' own chart, the aircraft where it was on the Earth each time. No real-Earth chart
// is centred near them; their far terrain is the real Earth's patches, and the islands' silhouette is drawn from the
// real Earth's charts. With the generated islands chosen, the flight is as it was.
#include "../src/game.h"
#include "../src/earth_charts.h"
#include "../src/earth_far.h"
#include "test_world.h"
#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

struct GameTest {
  static int run(const std::string& dir) {
    int failed = 0;
    auto check = [&](bool pass, const std::string& label) {
      printf("Solace on the Earth: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass;
    };
    std::string why;
    auto pack = earth::Pack::open(dir, &why);
    check(pack != nullptr, "the pack opens " + why);
    if (!pack) return 1;
    earthCharts::usePack(pack);
    // the lattice: the islands' chart within their reach, the real Earth's beyond
    const geo::Position solace = geo::solaceOrigin();
    auto at = [&](double km, double bearing) { return geo::destination(solace, bearing, km * 1000.0); };
    check(earthCharts::slideIdAt(solace.latitude, solace.longitude) == "solace", "the islands' own chart at their place");
    check(earthCharts::slideIdAt(at(100, 270).latitude, at(100, 270).longitude) == "solace", "and 100 km out");
    const auto beyond = at(120, 270);
    check(earthCharts::isEarthId(earthCharts::slideIdAt(beyond.latitude, beyond.longitude)), "a real-Earth chart 120 km out");
    // the far terrain: the islands' silhouette from a real-Earth chart in sight of them, never from their own
    {
      const auto chart = earthCharts::chartAt(beyond.latitude, beyond.longitude);
      geo::Position viewer = beyond; viewer.height = 3000;
      const auto from = earthFar::select(viewer, chart.origin, pack->box(), 61);
      check(!from.regions.empty() && from.regions[0].id == "solace", "the islands' silhouette drawn from the chart beside them");
      viewer = solace; viewer.height = 3000;
      const auto home = earthFar::select(viewer, solace, pack->box(), 61);
      check(std::none_of(home.regions.begin(), home.regions.end(), [](const procedural::Descriptor& d) { return d.authored; }),
            "not from their own");
      viewer = at(1500, 270); viewer.height = 1000;
      const auto far = earthFar::select(viewer, earthCharts::chartAt(viewer.latitude, viewer.longitude).origin, pack->box(), 61);
      check(std::none_of(far.regions.begin(), far.regions.end(), [](const procedural::Descriptor& d) { return d.authored; }),
            "nor from beyond the horizon");
    }
    // the islands, read from a cache as the game reads them (the run's own when ctest made them)
    const fs::path root = fs::temp_directory_path() / ("earth_solace_test_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    buildTestWorld();
    const std::string stamp = "earth-solace-v1";
    g_world.saveCache((root / "world.bin").string(), stamp);
    Game game; game.initHeadless(); game.botControl = true; game.set.traffic = false;
    game.cacheDir = root.string(); game.buildStamp = stamp; game.saveDir = root.string();
    game.debugScene("region-solace");
    check(game.screen == SCR_FLIGHT && g_world.regionId == "solace" && game.flightRegion.id == "solace", "Free Flight from the islands");
    check(game.realEarthPlay(), "on the real Earth (the pack is there)");
    game.freeGeneratedEarth = true;
    check(!game.realEarthPlay(), "or the generated islands round them, when chosen");
    game.freeGeneratedEarth = false;
    bool onlyEarth = true;
    // flies along x at 600 m, out of (or into) the chart it is in, until the chart ahead is ready and it has moved
    auto fly = [&](vec3 start, float vx, const std::string& what, const std::string& into) {
      const std::string from = g_world.regionId;
      const regionTravel::Chart before(game.flightRegion.origin);
      game.plane.pos = vec3(start.x, std::max(0.f, g_world.height(start.x, start.z)) + 600.f, start.z);
      game.plane.vel = vec3(vx, 0.f, 0.f); game.plane.onGround = false; game.crashed = false;
      game.plane.q = quat::axisAngle(vec3(0, 1, 0), vx > 0 ? -0.5f * PI : 0.5f * PI);
      const geo::Position where = before.position(game.plane.pos);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(4);
      while (g_world.regionId == from && std::chrono::steady_clock::now() < deadline) {
        game.updateEarthFlight();
        onlyEarth &= game.earthPrefetchId.empty() || game.earthPrefetchId == "solace" || earthCharts::isEarthId(game.earthPrefetchId);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      check(g_world.regionId != from && game.earthPrefetchError.empty(), what + ": moved on " + game.earthPrefetchError);
      check(into == "earth" ? earthCharts::isEarthId(g_world.regionId) : g_world.regionId == into, what + ": into " + g_world.regionId);
      check(game.flightRegion.id == g_world.regionId && game.screen == SCR_FLIGHT, what + ": still flying");
      const regionTravel::Chart after(game.flightRegion.origin);
      check(geo::surfaceDistance(after.position(game.plane.pos), where) < 1.0, what + ": where it was on the Earth");
      check(geo::surfaceDistance(game.flightRegion.origin, solace) < 1000.0 ||
            geo::surfaceDistance(game.flightRegion.origin, solace) > earthCharts::kSolaceReach - 3000.0, what + ": no real-Earth chart near the islands");
      game.update(1.f / 60.f); game.in.endFrame();
      check(game.screen == SCR_FLIGHT && !game.crashed, what + ": flies on");
    };
    // out west, past the islands' sea: the chart ahead the real Earth's, over open water
    fly(vec3(-95000.f, 0, 0), -70.f, "west out of the islands", "earth");
    check(g_world.height(game.plane.pos.x, game.plane.pos.z) < 0.f, "over the sea");
    // and back: 50 km west of them, flying east, the islands' own chart again
    {
      const vec3 islands = regionTravel::Chart(game.flightRegion.origin).local(solace);
      fly(islands + vec3(-50000.f, 0, 0), 70.f, "east into the islands", "solace");
      check(g_world.airports.size() > 3, "the islands' airports there");
    }
    // a slow aircraft just off the islands' west coast isn't moved while they are under it
    {
      game.plane.pos = vec3(-WORLD_HALF + 15000.f, 800.f, 0.f); game.plane.vel = vec3(-60.f, 0, 0);
      for (int i = 0; i < 400; i++) { game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
      check(g_world.regionId == "solace", "not moved off the islands while over them");
    }
    check(onlyEarth, "never a generated region wanted");
    std::error_code ec; fs::remove_all(root, ec);
    printf("earth_solace_test: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
  }
};

int main(int argc, char** argv) { return GameTest::run(argc > 1 ? argv[1] : "tests/fixtures/earth2"); }
