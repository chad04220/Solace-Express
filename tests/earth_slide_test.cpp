// Free Flight on the real Earth slides from chart to chart under the aircraft (Game::slideEarthChart,
// docs/REAL_EARTH_PLAN.md section 5), over the test fixture: out from Martha's Vineyard's chart the next is prepared
// ahead along the track, on the lattice (never a generated region); past the line between the two centres the flight
// moves into it - the aircraft where it was on the Earth, on its heading, at its height, still flying - and a low one
// keeps its height over the ground.
#include "../src/game.h"
#include "../src/earth_charts.h"
#include <chrono>
#include <thread>

struct GameTest {
  static int run(const std::string& dir) {
    int failed = 0;
    auto check = [&](bool pass, const std::string& label) {
      printf("Earth slide: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass;
    };
    std::string why;
    auto pack = earth::Pack::open(dir, &why);
    check(pack != nullptr, "the pack opens " + why);
    if (!pack) return 1;
    earthCharts::usePack(pack);
    const auto A = earthCharts::chartForAirport("KMVY");
    check(A.has_value(), "KMVY's chart");
    if (!A) return 1;
    Game game; game.initHeadless(); game.botControl = true; game.set.traffic = false; game.cacheDir.clear();
    game.debugScene("region-" + A->id);
    check(game.screen == SCR_FLIGHT && g_world.regionId == A->id && game.earthFlight && game.flightRegion.id == A->id,
          "Free Flight over Martha's Vineyard's chart");
    bool onlyCharts = true;
    // flies east, out from the centre, over the Sound towards Nantucket: the next chart prepared ahead
    auto slide = [&](float startX, float moveX, float above, const std::string& what) {
      const std::string from = g_world.regionId;
      const regionTravel::Chart before(game.flightRegion.origin);
      auto place = [&](float x) {
        game.plane.pos = vec3(x, g_world.height(x, 0.f) + above, 0.f);
        game.plane.vel = vec3(70.f, 0.f, 0.f); game.plane.onGround = false; game.crashed = false;
        game.plane.q = quat::axisAngle(vec3(0, 1, 0), -0.5f * PI);
      };
      place(startX);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
      while (!game.earthPrefetch && std::chrono::steady_clock::now() < deadline) {
        game.updateEarthFlight();
        onlyCharts &= game.earthPrefetchId.empty() || earthCharts::isEarthId(game.earthPrefetchId);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      check(game.earthPrefetch != nullptr && game.earthPrefetchError.empty(), what + ": the next chart prepared ahead " + game.earthPrefetchError);
      if (!game.earthPrefetch) return;
      const std::string next = game.earthPrefetch->region.id;
      const vec3 centre = before.local(game.earthPrefetch->region.origin);
      check(earthCharts::isEarthId(next) && centre.x > startX + 12000.f && std::fabs(centre.z) < 8000.f,
            what + ": a chart of the lattice, along the track (" + next + fmt(" at %.0f %.0f)", centre.x, centre.z));
      check(g_world.regionId == from, what + ": not moved into before it is nearer");
      place(moveX);
      const geo::Position where = before.position(game.plane.pos);
      const float y = game.plane.pos.y, ground = g_world.height(game.plane.pos.x, game.plane.pos.z);
      const double bearing = *geo::initialBearing(where, before.position(game.plane.pos + game.plane.vel * 100.f));   // (true: the charts' grids turn)
      for (int i = 0; i < 200 && g_world.regionId == from; i++) { game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
      check(g_world.regionId == next && game.flightRegion.id == next && game.screen == SCR_FLIGHT, what + ": moved into it, still flying");
      const regionTravel::Chart after(game.flightRegion.origin);
      check(geo::surfaceDistance(after.position(game.plane.pos), where) < 1.0, what + ": where it was on the Earth");
      const double now = *geo::initialBearing(after.position(game.plane.pos), after.position(game.plane.pos + game.plane.vel * 100.f));
      check(std::fabs(std::remainder(now - bearing, 360.0)) < 0.2, fmt("%s: on its heading (%.2f, %.2f)", what.c_str(), bearing, now));
      const float newGround = g_world.height(game.plane.pos.x, game.plane.pos.z);
      if (above < 30.f) check(std::fabs((game.plane.pos.y - newGround) - (y - ground)) < 0.05f, fmt("%s: its height over the ground kept (%.2f m, was %.2f)", what.c_str(), game.plane.pos.y - newGround, y - ground));
      else check(std::fabs(game.plane.pos.y - y) < 0.5f, fmt("%s: its altitude kept (%.1f, was %.1f)", what.c_str(), game.plane.pos.y, y));
      printf("  %s -> %s (%s); the grounds there %.2f and %.2f m\n", from.c_str(), next.c_str(), game.flightRegion.name.c_str(), ground, newGround);
      game.update(1.f / 60.f); game.in.endFrame();
      check(game.screen == SCR_FLIGHT && !game.crashed, what + ": flies on");
    };
    slide(12000.f, 18000.f, 600.f, "at 600 m");
    slide(12000.f, 18000.f, 20.f, "at 20 m");
    check(onlyCharts, "never a generated region wanted on the real Earth");
    printf("earth_slide_test: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
  }
};

int main(int argc, char** argv) { return GameTest::run(argc > 1 ? argv[1] : "tests/fixtures/earth2"); }
