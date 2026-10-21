// A chart of the real Earth built from an Earth pack (World::buildEarth, earth_charts.h): the test fixture's at
// Martha's Vineyard by default - its airports where the data has them, its runways on their real headings and levelled,
// the sea where the cover says, the land above it, the towns and the roads laid, every height finite - and the build's
// timings. --pack DIR --airport IDENT builds another (the dev packs), --save FILE keeps its cache.
#include "../src/world.h"
#include "../src/earth_charts.h"
#include "../src/region_travel.h"
#include "../src/scenery.h"
#include "../src/entities.h"
#include <chrono>
#include <map>

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
    printf("  %zu lots, %zu settlements\n", sceneryState(w).lots.size(), w.settlements.size());
    const size_t gpu = roadEntryRows(w.roadGrid).size() * sizeof(float);
    printf("  the road data's upload: %.1f MB\n", gpu / 1048576.0);
    check(gpu < 96u << 20, "the road data within its upload's budget");
  }
  check(w.earthChart != nullptr && w.procedural && w.detailScale < 1.f && w.regionId == chart->id, "a real-Earth World");
  {   // the road grid built on one thread the same as on every core (buildRoadGrid's stages run in parallel)
    RoadGrid one;
    { ScopedParallelLimit single(1); buildRoadGrid(w, w.roads, one); }
    bool same = one.head == w.roadGrid.head && one.list == w.roadGrid.list && one.nearHead == w.roadGrid.nearHead &&
                one.nearList == w.roadGrid.nearList && one.segs.size() == w.roadGrid.segs.size();
    for (size_t i = 0; same && i < one.segs.size(); i++) same = !memcmp(&one.segs[i], &w.roadGrid.segs[i], sizeof(RoadSegment));
    check(same, "the road grid the same on one thread as on many");
  }
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
  {   // the airports' other runways (World::extraRunways): each level at its airport's height along it, onRunway
      // answering with that airport and the runway's own surface, and nothing of the scenery standing on it (its chunks
      // made from this World, as a chart's staged scenery is)
    int levelled = 0, answered = 0, clear = 0;
    Scenery scenery(0, &w);
    for (const ExtraRunway& r : w.extraRunways) {
      const Airport& a = w.airports[r.airport];
      const float hd = r.heading * DEG, s = sinf(hd), c = cosf(hd);
      bool level = true;
      for (float t : {-0.45f, 0.f, 0.45f}) level &= std::abs(w.height(r.x + s * t * r.length, r.z - c * t * r.length) - a.elev) < 1.f;
      levelled += level;
      int surface = -1;
      answered += w.onRunway(r.x + s * 0.3f * r.length, r.z - c * 0.3f * r.length, 0.f, &surface) == r.airport && surface == r.surface;
      bool free = true;
      for (float t = -0.5f; t <= 0.5f; t += 0.05f) {
        const float x = r.x + s * t * r.length, z = r.z - c * t * r.length;
        const Scenery::Chunk* ch = scenery.ensure(Scenery::chunkOf(x), Scenery::chunkOf(z), 2);
        for (int k = 0; ch && k < EK_COUNT; k++)
          for (uint32_t q = ch->off[k]; q < ch->off[k + 1]; q++) {
            const Ent& e = ch->ents[q];
            const float dx = e.x - r.x, dz = e.z - r.z;
            const bool on = fabsf(dx * s - dz * c) < r.length * 0.5f - 10.f && fabsf(dx * c + dz * s) < r.width * 0.5f;
            if (on && free) printf("  on %s's other runway: %s at %.0f %.0f\n", a.code, kEntInfo[k].name, e.x, e.z);
            free &= !on;
          }
      }
      clear += free;
      printf("  %-5s's other runway %5.0f m x %3.0f m  heading %5.1f  surface %d  at %7.0f %7.0f\n", a.code, r.length, r.width, r.heading, r.surface, r.x, r.z);
    }
    const int n = (int)w.extraRunways.size();
    if (ident == "KMVY") check(n > 0, "the airports' other runways");
    check(levelled == n, fmt("each other runway level at its airport's height (%d of %d)", levelled, n));
    check(answered == n, fmt("onRunway answers each with its airport and its own surface (%d of %d)", answered, n));
    check(clear == n, fmt("nothing of the scenery on any (%d of %d clear)", clear, n));
  }
  if (w.earthChart) {   // the real lighthouses (ChartData::landmarks): each standing among the landmarks within 120 m of
                        // its place (the nearest land) as tall as it is, on its ground (at sea: 2 m down at most), in the
                        // scenery chunk it stands in, no building of a settlement's lot within its reach
    int placed = 0, chunked = 0, roomy = 0, seen = 0;
    Scenery scenery(0, &w);
    for (const auto& l : w.earthChart->landmarks) {
      const PlacedEnt* got = nullptr;
      for (const PlacedEnt& p : landmarks(w))
        if (p.kind == EK_LIGHTHOUSE && hypotf(p.e.x - l.x, p.e.z - l.z) < 121.f) got = &p;
      if (!got) { printf("  no lighthouse for %s at %.0f %.0f\n", l.name.c_str(), l.x, l.z); continue; }
      const float g = w.groundHeight(got->e.x, got->e.z, 8), top = got->e.y + kEntInfo[EK_LIGHTHOUSE].h * got->e.sy;
      const float want = std::clamp(l.height > 0.f ? l.height : 16.f, 0.45f * 27.f, 1.6f * 27.f);
      if (got->e.y >= -2.2f && got->e.y <= std::max(g, -2.f) + 0.1f && fabsf(top - std::max(got->e.y, 0.f) - want) < 0.5f) placed++;
      else printf("  %s at %.1f (ground %.1f), its top %.1f, %.1f m tall\n", l.name.c_str(), got->e.y, g, top, l.height);
      const Scenery::Chunk* ch = scenery.ensure(Scenery::chunkOf(got->e.x), Scenery::chunkOf(got->e.z), 1);
      bool in = false, clear = true;
      for (uint32_t q = ch ? ch->off[EK_LIGHTHOUSE] : 0; ch && q < ch->off[EK_LIGHTHOUSE + 1]; q++) in |= ch->ents[q].x == got->e.x && ch->ents[q].z == got->e.z;
      for (int k = EK_HOUSE; ch && k <= EK_GASSTATION; k++)
        for (uint32_t q = ch->off[k]; q < ch->off[k + 1]; q++)
          if (k != EK_LIGHTHOUSE && hypotf(ch->ents[q].x - got->e.x, ch->ents[q].z - got->e.z) < 6.f * got->e.sx) clear = false;
      chunked += in; roomy += clear;
      printf("  lighthouse %-32s %4.1f m  at %7.0f %7.0f (%3.0f m from its place)  foot %5.1f\n", l.name.c_str(), l.height, got->e.x, got->e.z,
             hypotf(got->e.x - l.x, got->e.z - l.z), got->e.y);
      seen++;
    }
    const int n = (int)w.earthChart->landmarks.size();
    if (ident == "KMVY" && (dir.find("fixtures/earth2") != std::string::npos || n > 0)) {   // (a pack built before them: none)
      int named = 0;
      for (const char* want : {"East Chop Light", "West Chop Light", "Edgartown Harbor Light", "Gay Head Light"})
        for (const auto& l : w.earthChart->landmarks) named += l.name == want;
      check(named == 4, fmt("the Vineyard's lights in its chart (%d of 4)", named));
    }
    check(seen == n, fmt("each real lighthouse placed (%d of %d)", seen, n));
    check(placed == seen, fmt("each on its ground and as tall as it is (%d of %d)", placed, seen));
    check(chunked == seen, fmt("each in its scenery chunk (%d of %d)", chunked, seen));
    check(roomy == seen, fmt("no settlement's building within any's reach (%d of %d)", roomy, seen));
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
  {   // the lakes (World::lakes): the ground never under a lake's level where its code is, and water where the natural
      // ground lies under it
    std::map<uint16_t, size_t> levels;
    size_t coded = 0, wet = 0, low = 0;
    for (int j = 0; j < HM_N; j++) for (int i = 0; i < HM_N; i++) {
      const uint16_t c = w.lakes.empty() ? 0 : w.lakes[(size_t)j * HM_N + i];
      if (!c) continue;
      coded++;
      const float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL, level = World::lakeLevelOf(c);
      if (w.height(x, z) < level - 1e-3f) low++;
      if (w.naturalHeight(x, z) < level) { wet++; levels[c]++; check(w.underLake(x, z), "a lake's water is under it"); }
    }
    check(low == 0, fmt("the ground never under a lake's level where it lies (%zu texels under)", low));
    size_t big = 0; for (const auto& l : levels) big += l.second >= 200;
    printf("lakes: %zu levels, %zu of them over 30 ha; %zu texels coded, %zu under water\n", levels.size(), big, coded, wet);
    if (ident == "KBOS" && w.earthChart->fineTiles > 0) check(big >= 5, "Boston's chart has its reservoirs and ponds");   // (GLO-90's: ETOPO has none)
    if (ident == "KMVY" && dir.find("fixtures/earth2") != std::string::npos) check(big >= 3, "the upper Cape's ponds (Mashpee-Wakeby, Johns, Ashumet)");
  }
  if (dir == "tests/fixtures/earth2" || dir.find("fixtures/earth2") != std::string::npos) {
    check(seaN > landN, "the islands' chart is mostly sea");
    check(w.towns.size() >= 3, "towns on the islands");
    size_t nearStart = 0;
    if (at >= 0) for (const Town& t : w.towns) nearStart += hypotf(t.x - w.airports[at].x, t.z - w.airports[at].z) < 10000.f;
    check(nearStart >= 3, fmt("the towns by the start, not only the biggest in the square (%zu within 10 km)", nearStart));
    check(!w.roads.paths.empty(), "roads on the islands");
  }
  const bool fixture = dir.find("fixtures/earth2") != std::string::npos;
  if (fixture || getenv("EARTH_OVERLAP")) {   // the neighbouring chart, 12 lattice steps east: how far their grounds agree
    const auto east = earthCharts::find(earthCharts::idAt(chart->origin.latitude, chart->origin.longitude + 12.0 / earthCharts::kLattice));
    World v; v.buildProcedural(*east, "", "", false);
    const regionTravel::Chart ca(chart->origin), cb(east->origin);
    std::vector<float> d, base;
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; };
    for (int k = 0; k < 20000 && d.size() < 3000; k++) {
      const vec3 a(-15000.f + 60000.f * rnd(), 0, -20000.f + 40000.f * rnd());
      const geo::Position g = ca.position(a);
      const vec3 b = cb.local(g);
      if (std::max(std::fabs(b.x), std::fabs(b.z)) > 30000.f || std::max(std::fabs(a.x), std::fabs(a.z)) > 30000.f) continue;
      const float ha = w.naturalHeight(a.x, a.z), hb = v.naturalHeight(b.x, b.z);
      if (ha < 3.f || hb < 3.f) continue;
      d.push_back(std::fabs(ha - hb));
      float ba[4], bb[4]; w.sampleBase(a.x, a.z, ba); v.sampleBase(b.x, b.z, bb); base.push_back(std::fabs(ba[0] - bb[0]));
    }
    std::sort(base.begin(), base.end());
    if (!base.empty()) printf("  their base heights alone: p50 %.2f, p95 %.2f, max %.2f\n", base[base.size() / 2], base[base.size() * 95 / 100], base.back());
    std::sort(d.begin(), d.end());
    double mean = 0; for (float x : d) mean += x; mean /= std::max<size_t>(d.size(), 1);
    if (!d.empty()) printf("overlap with %s: %zu land points, mean %.2f m, p50 %.2f, p95 %.2f, max %.2f\n", east->id.c_str(), d.size(), mean, d[d.size() / 2], d[d.size() * 95 / 100], d.back());
    // (the detail noise one field in both, World::noisePoint: what is left is the base's texels on two grids)
    check(d.size() > 200 && mean < 0.3 && d[d.size() * 95 / 100] < 1.0f, fmt("neighbouring charts' grounds agree (mean %.2f m)", mean));
  }
  printf("earth_chart_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
