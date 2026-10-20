// A chart of the real Earth built from an Earth pack (World::buildEarth, earth_charts.h): the test fixture's at
// Martha's Vineyard by default - its airports where the data has them, its runways on their real headings and levelled,
// the sea where the cover says, the land above it, the towns and the roads laid, every height finite - and the build's
// timings. --pack DIR --airport IDENT builds another (the dev packs), --save FILE keeps its cache.
#include "../src/world.h"
#include "../src/earth_charts.h"
#include "../src/region_travel.h"
#include <chrono>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) {
  checks++;
  if (!ok) { failures++; if (failures < 40) printf("FAIL %s\n", what.c_str()); }
}

int main(int argc, char** argv) {
  std::string dir = "tests/fixtures/earth2", ident = "KMVY", save;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--pack" && i + 1 < argc) dir = argv[++i];
    else if (a == "--airport" && i + 1 < argc) ident = argv[++i];
    else if (a == "--save" && i + 1 < argc) save = argv[++i];
    else if (i == 1) dir = a;
  }
  std::string why;
  auto pack = earth::Pack::open(dir, &why);
  check(pack != nullptr, "the pack opens: " + why);
  if (!pack) return 1;
  earthCharts::usePack(pack);
  const auto chart = earthCharts::chartForAirport(ident);
  check(chart.has_value(), "a chart for " + ident);
  if (!chart) return 1;
  printf("chart %s (%s) at %.5f, %.5f\n", chart->id.c_str(), chart->name.c_str(), chart->origin.latitude, chart->origin.longitude);
  check(earthCharts::find(chart->id).has_value() && earthCharts::find(chart->id)->id == chart->id, "the chart's id finds it again");
  check(!earthCharts::find("earth:01:2") && !earthCharts::find("earth:1:2x") && !earthCharts::find("earth:99999:0"), "malformed ids refused");
  const auto t0 = std::chrono::steady_clock::now();
  World w;
  w.buildProcedural(*chart, save, save.empty() ? "" : "earth-chart-test", !save.empty());
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  size_t roadPts = 0; for (const auto& p : w.roads.paths) roadPts += p.pts.size();
  size_t bridges = 0; for (const auto& p : w.roads.paths) for (auto b : p.bridge) bridges += b;
  printf("built in %.1f s (%s): %zu airports, %zu towns, %zu roads (%zu points, %zu bridge segments), %zu road grid entries, %d GLO-90 tiles\n",
         secs, w.fromCache ? "from the cache" : "generated", w.airports.size(), w.towns.size(), w.roads.paths.size(), roadPts, bridges,
         w.roadGrid.list.size(), w.earthChart ? w.earthChart->fineTiles : -1);
  {   // (the roads' share of the road grid, by class)
    double len[RC_COUNT] = {}; size_t ent[RC_COUNT] = {}, segs[RC_COUNT] = {};
    for (const auto& p : w.roads.paths) for (size_t k = 1; k < p.pts.size(); k++) len[p.cls] += hypotf(p.pts[k].x - p.pts[k - 1].x, p.pts[k].z - p.pts[k - 1].z);
    for (uint32_t si : w.roadGrid.list) ent[w.roadGrid.segs[si].cls]++;
    for (const auto& s : w.roadGrid.segs) segs[s.cls]++;
    for (int c = 0; c < RC_COUNT; c++) if (len[c] > 0) printf("  class %d: %.0f km, %zu segments, %zu grid entries\n", c, len[c] / 1000, segs[c], ent[c]);
    const size_t gpu = roadEntryRows(w.roadGrid).size() * sizeof(float);
    printf("  the road data's upload: %.1f MB\n", gpu / 1048576.0);
    check(gpu < 96u << 20, "the road data within its upload's budget");
  }
  check(w.earthChart != nullptr && w.procedural && w.detailScale < 1.f && w.regionId == chart->id, "a real-Earth World");
  // the airport we came for, on its real heading, levelled at its height
  int at = w.findAirport(ident.c_str());
  check(at >= 0, ident + " is one of the chart's airports");
  for (const Airport& a : w.airports) {
    const float g = w.height(a.x, a.z);
    check(std::abs(g - a.elev) < 0.5f, fmt("%s's runway at its height (%.1f, ground %.1f)", a.code, a.elev, g));
    const float hd = a.heading * DEG;
    for (float t : {-0.45f, 0.45f}) {
      const float x = a.x + sinf(hd) * t * a.length, z = a.z - cosf(hd) * t * a.length;
      check(std::abs(w.height(x, z) - a.elev) < 1.0f, fmt("%s's runway level to its ends", a.code));
    }
    printf("  %-5s %-40s %5.0f m x %3.0f m  heading %5.1f  elev %6.1f  size %d  at %7.0f %7.0f\n", a.code, a.name, a.length, a.width, a.heading, a.elev, a.size, a.x, a.z);
  }
  if (at >= 0 && w.earthChart) {   // the real runway's heading, true, against the chart's at the field
    const auto& rec = w.earthChart->airports[at];
    const auto& r = rec.runways[0];
    if (r.endsKnown && r.hasHeading) {
      const regionTravel::Chart c(chart->origin);
      const vec3 focus(w.airports[at].x, 0, w.airports[at].z);
      const geo::Position here = c.position(focus);
      const float north = (float)c.bearing(focus, {here.latitude + 0.05, here.longitude, 0}).value_or(0.0);
      const float d = std::fabs(std::remainder(w.airports[at].heading - north - r.heading, 180.f));
      check(d < 1.5f, fmt("%s's runway on its real heading (%.1f true, the chart's %.1f less north %.1f)", ident.c_str(), r.heading, w.airports[at].heading, north));
    }
  }
  for (const Town& t : w.towns) printf("  town %-28s %d r %4.0f at %7.0f %7.0f ground %.1f\n", t.name, t.kind, t.r, t.x, t.z, w.height(t.x, t.z));
  // every height finite, the sea below 0 and the land above it
  int bad = 0, seaN = 0, landN = 0;
  for (size_t k = 0; k < w.hm.size(); k += 4) {
    if (!std::isfinite(w.hm[k]) || !std::isfinite(w.hm[k + 1]) || !std::isfinite(w.hm[k + 2]) || !std::isfinite(w.hm[k + 3])) bad++;
    (w.hm[k] < 0.f ? seaN : landN)++;
  }
  check(bad == 0, fmt("every texel finite (%d not)", bad));
  printf("land %.1f%%, sea %.1f%%\n", 100.0 * landN / (landN + seaN), 100.0 * seaN / (landN + seaN));
  check(landN > 0, "some land");
  if (dir == "tests/fixtures/earth2" || dir.find("fixtures/earth2") != std::string::npos) {
    check(seaN > landN, "the islands' chart is mostly sea");
    check(w.towns.size() >= 3, "towns on the islands");
    check(!w.roads.paths.empty(), "roads on the islands");
  }
  printf("earth_chart_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
