// The real Earth's far terrain (earth_far.h) over the test fixture's pack: the patches round a viewer at Martha's
// Vineyard - none overlapping another, none wholly inside the chart square, the finest nearest, every place on the
// horizon over the data in one of them - and a patch's mesh: finite, its collision surface the grid it was built
// from, its edges the same heights as its neighbour's along them.
#include "../src/earth_far.h"
#include "../src/earth_charts.h"
#include "../src/region_travel.h"
#include <set>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) {
  checks++;
  if (!ok) { failures++; if (failures < 40) printf("FAIL %s\n", what.c_str()); }
}
struct Cell { int level, i, j; double south, west, north, east; };
static Cell cellOf(const std::string& id) {
  Cell c{};
  sscanf(id.c_str(), "earthfar:%d:%d:%d", &c.level, &c.i, &c.j);
  const double s = earthFar::patchDegrees(c.level);
  c.south = -90 + c.i * s; c.north = std::min(90.0, c.south + s); c.west = -180 + c.j * s; c.east = c.west + s;
  return c;
}
static bool contains(const Cell& c, double lat, double lon) { return lat >= c.south && lat < c.north && lon >= c.west && lon < c.east; }

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "tests/fixtures/earth2";
  std::string why;
  auto pack = earth::Pack::open(dir, &why);
  check(pack != nullptr, "the pack opens: " + why);
  if (!pack) return 1;
  earthCharts::usePack(pack);
  // ids
  check(earthFar::find("earthfar:0:262:218").has_value(), "a patch's id finds it");
  check(!earthFar::find("earthfar:0:262:0218") && !earthFar::find("earthfar:5:0:0") && !earthFar::find("earthfar:0:360:0") &&
        !earthFar::find("earthfar:0:1:1x"), "malformed ids refused");
  const auto byFind = procedural::find("earthfar:2:66:54");
  check(byFind && byFind->id == "earthfar:2:66:54", "procedural::find knows the patches");

  // the selection round Martha's Vineyard, 500 m up
  const auto chart = earthCharts::chartForAirport("KMVY");
  check(chart.has_value(), "the KMVY chart");
  if (!chart) return 1;
  const geo::Position viewer{41.393, -70.614, 500};
  const auto sel = earthFar::select(viewer, chart->origin, pack->box(), 61);
  printf("%zu patches (%s), reach %.0f km\n", sel.regions.size(), sel.complete ? "complete" : "capped", sel.reachMetres / 1000);
  check(!sel.regions.empty() && sel.regions.size() <= 61, "some patches, within the cache");
  std::vector<Cell> cells;
  int perLevel[earthFar::kLevels] = {};
  for (const auto& d : sel.regions) {
    check(earthFar::isFarId(d.id) && earthFar::find(d.id).has_value(), "a canonical patch: " + d.id);
    cells.push_back(cellOf(d.id)); perLevel[cells.back().level]++;
  }
  for (int l = 0; l < earthFar::kLevels; l++) printf("  level %d (%.1f deg): %d\n", l, earthFar::patchDegrees(l), perLevel[l]);
  for (size_t a = 0; a < cells.size(); a++) for (size_t b = a + 1; b < cells.size(); b++) {
    const Cell &p = cells[a], &q = cells[b];
    const bool overlap = p.south < q.north - 1e-9 && q.south < p.north - 1e-9 && p.west < q.east - 1e-9 && q.west < p.east - 1e-9;
    check(!overlap, "no two patches overlap: " + sel.regions[a].id + " " + sel.regions[b].id);
  }
  check(!cells.empty() && cells.front().level == 0, "the nearest patch the finest");
  const regionTravel::Chart local(chart->origin);
  for (const Cell& c : cells) {
    bool inside = true;
    for (double la : {c.south, c.north}) for (double lo : {c.west, c.east}) {
      const vec3 p = local.local({la, lo, 0});
      inside = inside && std::max(std::fabs(p.x), std::fabs(p.z)) < WORLD_HALF;
    }
    check(!inside, "no patch wholly inside the chart");
    const auto& box = pack->box();
    check(c.south < box.north && c.north > box.south && c.west < box.east && c.east > box.west, "every patch over the data");
  }
  // every place within the horizon over the data, outside the chart, in exactly one patch
  int places = 0;
  for (double r : {45e3, 70e3, 120e3, 200e3}) for (int k = 0; k < 24; k++) {
    const double a = k * 2 * geo::kPi / 24;
    const geo::Position p = local.position(vec3(float(r * sin(a)), 0, float(-r * cos(a))));
    const auto& box = pack->box();
    if (p.latitude <= box.south || p.latitude >= box.north || p.longitude <= box.west || p.longitude >= box.east) continue;
    const vec3 q = local.local(p);
    if (std::max(std::fabs(q.x), std::fabs(q.z)) < WORLD_HALF) continue;
    if (r > sel.reachMetres) continue;
    int in = 0; for (const Cell& c : cells) in += contains(c, p.latitude, p.longitude);
    places++;
    check(in == 1, "a place on the horizon in one patch");
  }
  check(places > 20, "places to look at");

  // over a whole Earth's data, from the ground to the edge of space: always within the far pass's cache
  for (double h : {0.0, 3000.0, 11000.0, 30000.0, 100000.0}) {
    const auto all = earthFar::select({41.393, -70.614, h}, chart->origin, earth::Box{-90, -180, 90, 180}, 1000);
    int n[earthFar::kLevels] = {};
    for (const auto& d : all.regions) n[cellOf(d.id).level]++;
    printf("  at %6.0f m: %zu patches (%d %d %d %d %d), reach %.0f km\n", h, all.regions.size(), n[0], n[1], n[2], n[3], n[4], all.reachMetres / 1000);
    check(all.regions.size() <= 61, fmt("at %.0f m the patches fit the cache (%zu)", h, all.regions.size()));
  }
  // a patch's mesh: the finest one nearest the mainland west of the islands, and its eastern neighbour
  const int I = (int)std::floor((41.6 + 90) / 0.5), J = (int)std::floor((-71.0 + 180) / 0.5);
  const auto pa = earthFar::patch(0, I, J), pb = earthFar::patch(0, I, J + 1);
  const auto ma = earthFar::build(pa, pack), mb = earthFar::build(pb, pack);
  printf("patch %s: %zu vertices; %s: %zu\n", pa.id.c_str(), ma.vertices.size(), pb.id.c_str(), mb.vertices.size());
  check(!ma.vertices.empty() && ma.vertices.size() <= horizon::kMaxMeshVertices, "a mesh within the far pass's bound");
  bool finite = true;
  for (const auto& v : ma.vertices) finite = finite && std::isfinite(v.height) && std::isfinite(v.position.x + v.position.y + v.position.z) && std::isfinite(v.normal.y);
  check(finite, "every vertex finite");
  check(ma.surface && ma.surface->geographic && ma.surface->resolution == earthFar::kResolution + 1, "its collision surface the grid");
  if (ma.surface && mb.surface) {
    const int N = ma.surface->resolution;
    float worst = 0; int land = 0;
    for (int r = 0; r < N; r++) {
      const float a = ma.surface->grid[(size_t)r * N + N - 1].height, b = mb.surface->grid[(size_t)r * N].height;
      worst = std::max(worst, std::fabs(a - b)); land += a > 0;
    }
    check(worst == 0.f, fmt("the shared edge the same heights (worst %.3f m)", worst));
    check(land > 0, "land along the edge");
    // sampled through the regional surface, at a grid vertex well inside: that vertex's height
    const int r = N / 2, k = N / 2;
    const double lat = ma.surface->latN - r * ma.surface->dLat, lon = ma.surface->lonW + k * ma.surface->dLon;
    const vec3 at = local.local({lat, lon, 0});
    const auto s = regionalSurface::sample(ma.surface, chart->origin, at.x, at.z);
    const float want = ma.surface->grid[(size_t)r * N + k].height;
    check(s.covered, "the patch covers its own middle");
    if (want > 0) check(s.land && std::fabs(s.height - want) < 2.f, fmt("the collision height its grid's (%.1f against %.1f)", s.height, want));
  }
  printf("earth_far_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
