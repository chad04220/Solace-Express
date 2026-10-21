// Free Flight on the real Earth slides from chart to chart under the aircraft (Game::slideEarthChart,
// docs/REAL_EARTH_PLAN.md section 5), over the test fixture: out from Martha's Vineyard's chart the next is prepared
// ahead along the track, on the lattice (never a generated region); past the line between the two centres the flight
// moves into it - the aircraft where it was on the Earth, on its heading, at its height, still flying - and a low one
// keeps its height over the ground. The new chart's scenery is staged before the move (Scenery::stage): what stands
// within 1.5 km of the aircraft is there the moment it moves, each chunk as the new chart's World makes it.
#include "../src/game.h"
#include "../src/earth_charts.h"
#include "../src/airport_layout.h"
#include "../src/entity_lod.h"
#include <chrono>
#include <cstring>
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
      {   // (as the flight would fly on to the move: its scenery staged meanwhile, out to the island's east end)
        const auto staging = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (game.earthPrefetch && (!game.earthPrefetch->scenery || game.earthPrefetch->scenery->generated() < 1500) &&
               std::chrono::steady_clock::now() < staging) {
          game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(game.earthPrefetch && game.earthPrefetch->scenery && g_world.regionId == from,
              fmt("%s: its scenery staged ahead (%zu chunks)", what.c_str(), game.earthPrefetch && game.earthPrefetch->scenery ? game.earthPrefetch->scenery->generated() : 0));
      }
      {   // the same trees, bushes and boulders in both charts where they overlap on land (Martha's Vineyard's state forest,
          // in both, away from the towns, whose streets each chart lays out its own way): laid in the frame neighbouring
          // charts share, each of this chart's within 400 m stands in the next one too, of its kind, within a metre
        const geo::Position town{41.385, -70.565, 0};
        const World& next = *game.earthPrefetch->world;
        const regionTravel::Chart nextChart(game.earthPrefetch->region.origin);
        Scenery here(0, &g_world), there(0, &next);
        const vec3 p = before.local(town);
        int seen = 0, matched = 0;
        for (int cz = Scenery::chunkOf(p.z - 400.f); cz <= Scenery::chunkOf(p.z + 400.f); cz++)
          for (int cx = Scenery::chunkOf(p.x - 400.f); cx <= Scenery::chunkOf(p.x + 400.f); cx++) {
            const Scenery::Chunk* c = here.ensure(cx, cz, 2);
            for (int k = 0; c && k <= EK_SLAB; k++)
              for (uint32_t q = c->off[k]; q < c->off[k + 1]; q++) {
                const Ent& e = c->ents[q];
                if (hypotf(e.x - p.x, e.z - p.z) > 400.f) continue;
                seen++;
                const vec3 o = nextChart.local(before.position(vec3(e.x, 0, e.z)));
                bool found = false;
                for (int dz = Scenery::chunkOf(o.z - 2.f); dz <= Scenery::chunkOf(o.z + 2.f) && !found; dz++)
                  for (int dx = Scenery::chunkOf(o.x - 2.f); dx <= Scenery::chunkOf(o.x + 2.f) && !found; dx++) {
                    const Scenery::Chunk* d = there.ensure(dx, dz, 2);
                    for (uint32_t r = d ? d->off[k] : 0; d && r < d->off[k + 1] && !found; r++) found = hypotf(d->ents[r].x - o.x, d->ents[r].z - o.z) < 1.f;
                  }
                matched += found;
              }
          }
        const float share = seen ? float(matched) / float(seen) : 0.f;
        if (from == A->id) check(seen > 100 && share > 0.9f, fmt("%s: the same trees and stones in both charts (%d of %d, %.0f%%)", what.c_str(), matched, seen, 100.f * share));
      }
      place(moveX);
      const geo::Position where = before.position(game.plane.pos);
      const float y = game.plane.pos.y, ground = g_world.height(game.plane.pos.x, game.plane.pos.z);
      const double bearing = *geo::initialBearing(where, before.position(game.plane.pos + game.plane.vel * 100.f));   // (true: the charts' grids turn)
      // what refers to an airport both charts hold (Game::carriedAirport), the nearest the aircraft: the autopilot flying
      // to it, the GPS's pick, the tower's landing clearance, an aircraft in its circuit - each carried to it in the new
      // chart, by its ident; an aircraft of a field the new chart hasn't, gone. And every such airport's apron on the
      // same side of its runway in both (Airport::side)
      const World& nextWorld = *game.earthPrefetch->world;
      int both = -1, onlyHere = -1, sidesBoth = 0, sidesSame = 0;
      float lift = 0.f, nearest = 1e30f;
      for (int i = 0; i < (int)g_world.airports.size(); i++) {
        const Airport& a = g_world.airports[i];
        const int j = nextWorld.findAirport(a.code);
        if (j < 0) { if (onlyHere < 0) onlyHere = i; continue; }
        sidesBoth++; sidesSame += aptLayout(a, i).side == aptLayout(nextWorld.airports[j], j).side;
        const float d = hypotf(a.x - game.plane.pos.x, a.z - game.plane.pos.z);
        if (d < nearest) { nearest = d; both = i; lift = nextWorld.airports[j].elev - a.elev; }
      }
      check(sidesBoth > 0 && sidesSame == sidesBoth, fmt("%s: each airport both charts hold, its apron on the same side in both (%d of %d)", what.c_str(), sidesSame, sidesBoth));
      const std::string bothCode = both >= 0 ? g_world.airports[both].code : "";
      geo::Position circuitAt{};
      float circuitY = 0.f;
      if (both >= 0) {
        game.plane.apEngage(Plane::AP_NAV, both, game.wx); game.apDest = both;
        game.atcF = Game::AtcFlight{}; game.atcF.dep = game.atcF.arr = both; game.atcF.phase = 5; game.atcF.airborne = true;
        const Airport& a = g_world.airports[both];
        TrafficCraft c; c.role = TrafficCraft::AIRPORT; c.state = TrafficCraft::CIRCUIT; c.airport = both;
        c.pos = vec3(a.x + 900.f, a.elev + 300.f, a.z); c.vel = vec3(0.f, 0.f, -40.f); c.path = {c.pos + vec3(0.f, 0.f, -1500.f)};
        circuitAt = before.position(c.pos); circuitY = c.pos.y;
        game.traffic.craft.clear(); game.traffic.craft.push_back(c);
        if (onlyHere >= 0) { c.airport = onlyHere; game.traffic.craft.push_back(c); }
      }
      const auto waited = std::chrono::steady_clock::now();
      while (g_world.regionId == from && std::chrono::steady_clock::now() - waited < std::chrono::minutes(2)) {
        game.updateEarthFlight(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      const double staging = std::chrono::duration<double>(std::chrono::steady_clock::now() - waited).count();
      check(g_world.regionId == next && game.flightRegion.id == next && game.screen == SCR_FLIGHT, fmt("%s: moved into it, still flying (its scenery staged in %.1f s)", what.c_str(), staging));
      {   // the scenery there at once: within 1.5 km of the aircraft every chunk made as far as it is drawn, and those
          // taken over the new chart's own - a chunk made now from g_world the same, entity for entity
        const EntRanges R = entRangesFor(g_ren.quality);
        const float farDetail = std::max(std::max(R.tree, R.rock), R.bush) + 300.f;
        const vec3 p = game.plane.pos;
        int missing = 0, compared = 0, same = 0, held = 0;
        for (int cz = 0; cz < Scenery::NC; cz++)
          for (int cx = 0; cx < Scenery::NC; cx++) {
            const Scenery::Chunk* c = g_scenery.get(cx, cz);
            held += c != nullptr;
            const float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
            const float ex = std::max(std::max(x0 - p.x, p.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - p.z, p.z - z0 - Scenery::CH), 0.f);
            if (hypotf(ex, ez) < 1500.f && (!c || c->level < (hypotf(ex, ez) < farDetail ? 2 : 1))) missing++;
            if (!c || c->ents.empty() || compared >= 12) continue;
            Scenery fresh(0);
            const Scenery::Chunk* f = fresh.ensure(cx, cz, c->level);
            compared++;
            same += f->ents.size() == c->ents.size() && !memcmp(f->ents.data(), c->ents.data(), c->ents.size() * sizeof(Ent));
          }
        check(missing == 0, fmt("%s: the scenery within 1.5 km there at the move (%d chunks taken over, %d missing near)", what.c_str(), held, missing));
        check(compared > 0 && same == compared, fmt("%s: made from the new chart (%d of %d chunks as g_world makes them)", what.c_str(), same, compared));
      }
      const regionTravel::Chart after(game.flightRegion.origin);
      check(geo::surfaceDistance(after.position(game.plane.pos), where) < 1.0, what + ": where it was on the Earth");
      if (both >= 0) {
        const int ap = game.plane.apAirport;
        check(game.plane.apOn && (game.plane.apMode == Plane::AP_NAV || game.plane.apMode == Plane::AP_APPR) && ap >= 0 &&
              bothCode == g_world.airports[ap].code && game.apDest == ap, what + ": the autopilot flies on to " + bothCode + ", the GPS's pick");
        check(game.atcF.phase == 5 && game.atcF.arr == ap && game.atcF.dep == ap, what + ": still cleared to land there");
        const auto& craft = game.traffic.craft;
        check(craft.size() == 1 && craft[0].airport == ap && geo::surfaceDistance(after.position(craft[0].pos), circuitAt) < 1.0 &&
              std::fabs(craft[0].pos.y - (circuitY + lift)) < 0.05f && craft[0].path.size() == 1,
              fmt("%s: its circuit's aircraft where it was, over its ground there (%zu carried, %s)", what.c_str(), craft.size(), onlyHere >= 0 ? "one gone" : "none to go"));
      } else check(false, what + ": an airport both charts hold");
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
    {   // the GPS map's airports beyond the chart (Game::beyondAirports): from Nantucket's, Martha's Vineyard's among them,
        // where the Earth has it on this chart, and none in the square (those are g_world's)
      const auto& beyond = game.beyondAirports();
      const regionTravel::Chart here(game.flightRegion.origin);
      bool outside = true, placed = false;
      for (const auto& b : beyond) {
        outside &= std::max(fabsf(b.pos.x), fabsf(b.pos.y)) >= WORLD_HALF && hypotf(b.pos.x, b.pos.y) <= 120000.f;
        if (b.ident == "KMVY") {
          const auto mvy = pack->airports({41.3, -70.7, 41.5, -70.5});
          for (const auto& a : mvy) if (a.ident == "KMVY") { const vec3 p = here.local({a.lat, a.lon, 0.0}); placed = hypotf(p.x - b.pos.x, p.z - b.pos.y) < 1.f; }
        }
      }
      check(!beyond.empty() && outside && placed, fmt("the GPS map's airports beyond the chart: %zu, KMVY among them in its place", beyond.size()));
    }
    check(onlyCharts, "never a generated region wanted on the real Earth");
    printf("earth_slide_test: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
  }
};

int main(int argc, char** argv) { return GameTest::run(argc > 1 ? argv[1] : "tests/fixtures/earth2"); }
