// Somnium Volandi - a chart of the real Earth built from the Earth pack (docs/REAL_EARTH_PLAN.md section 3): the
// terrain from GLO-90 (or ETOPO where it isn't loaded), less the canopy and the roofs the surface model holds; the sea
// where the land cover says, its floor from ETOPO shelving away from the coast; the land cover as the ground's
// lushness, coldness, farmland and forest; the real airports on their own levelled grounds; the towns from GeoNames;
// the real roads laid on the ground. Then the rest of the World's pipeline as for any chart.
#include "world.h"
#include "airport_layout.h"
#include "earth_charts.h"
#include "region_travel.h"
#include "scenery.h"
#include "world_load_progress.h"
#include <map>
#include <stdexcept>

namespace {
constexpr float kAirportMargin = 3000.f;   // (an airport's runway this far inside the square at least)
constexpr float kRoadMargin = 1500.f;      // (the roads cut this far inside it)
constexpr size_t kMaxAirports = 40, kMaxTowns = 24, kMaxDistricts = 56;
constexpr float kEarthDetailScale = 0.2f;  // (World::detailScale: the data has the relief; the detail adds a little)

// What the surface model holds above the ground for each cover class (m): the canopy of a closed or an open forest,
// the roofs of a built-up area, the scrub of a shrubland
float canopyOf(int cover) {
  if (earth::coverClosedForest(cover)) return 12.f;
  if (earth::coverForest(cover)) return 5.f;
  if (cover == earth::COVER_BUILT) return 4.f;
  if (cover == earth::COVER_SHRUBS) return 1.f;
  return 0.f;
}
// The chart's lakes, per texel (-1: none), with each one's level: the cover's inland water where the surface model holds
// it level. The Copernicus DEM's lakes are flattened, every texel of one at its surface's height to the centimetre, so a
// lake is a stretch of the cover's water holding at least a dozen texels within 10 cm of each other. Its level is
// theirs. From them it takes in every texel at that level or a little under it (the bicubic's undershoot at the
// shore), out to 8 texels past the cover's water (its cells are 330 m, the texels 39). A stretch holding more than one
// level (a reservoir's chain, a river's flattened reaches) gives a lake for each. Water the data doesn't hold level
// stays land: unflattened rivers, and ETOPO's lakes where GLO-90 isn't loaded.
std::vector<int> findLakes(const std::vector<float>& elev, const std::vector<uint8_t>& sea, const std::vector<uint8_t>& cover,
                           const std::vector<uint8_t>& fine, int N, std::vector<float>& levels) {
  const size_t NN = (size_t)N * N;
  std::vector<int> lake(NN, -1);
  std::vector<uint8_t> seen(NN, 0), steps(NN, 0);
  std::vector<size_t> stretch, rest, queue;
  std::vector<float> hs;
  auto water = [&](size_t k) { return !sea[k] && cover[k] == earth::COVER_WATER; };
  auto neighbours = [&](size_t k, auto&& f) {
    const int i = (int)(k % N), j = (int)(k / N);
    if (i > 0) f(k - 1);
    if (i + 1 < N) f(k + 1);
    if (j > 0) f(k - N);
    if (j + 1 < N) f(k + N);
  };
  for (size_t s = 0; s < NN; s++) {
    if (seen[s] || !water(s)) continue;
    if ((s & 1023u) == 0) generationCheckpoint();
    stretch.clear(); queue.assign(1, s); seen[s] = 1;
    for (size_t q = 0; q < queue.size(); q++) {
      stretch.push_back(queue[q]);
      neighbours(queue[q], [&](size_t n) { if (!seen[n] && water(n)) { seen[n] = 1; queue.push_back(n); } });
    }
    size_t fineN = 0; for (size_t k : stretch) fineN += fine[k];
    if (fineN * 2 < stretch.size()) continue;
    for (int reach = 0; reach < 64; reach++) {
      rest.clear(); for (size_t k : stretch) if (lake[k] == -1) rest.push_back(k);
      if (rest.size() < 12) break;
      hs.clear(); for (size_t k : rest) hs.push_back(elev[k]);
      std::sort(hs.begin(), hs.end());
      size_t best = 0, at = 0;
      for (size_t a = 0, b = 0; a < hs.size(); a++) { while (b < hs.size() && hs[b] <= hs[a] + 0.1f) b++; if (b - a > best) { best = b - a; at = a; } }
      if (best < 12) break;
      const float lo = hs[at], hi = hs[at] + 0.1f, level = World::lakeLevelOf(World::lakeCode(hs[at + best / 2]));
      const int id = (int)levels.size();
      queue.clear();
      for (size_t k : rest) if (elev[k] >= lo && elev[k] <= hi) { lake[k] = id; steps[k] = 0; queue.push_back(k); }
      for (size_t q = 0; q < queue.size(); q++) {
        const size_t k = queue[q];
        neighbours(k, [&](size_t n) {
          if (lake[n] != -1 || sea[n] || elev[n] > level + 0.05f || elev[n] < level - 2.5f) return;
          const int st = cover[n] == earth::COVER_WATER ? 0 : steps[k] + 1;
          if (st > 8) return;
          lake[n] = id; steps[n] = (uint8_t)st; queue.push_back(n);
        });
      }
      levels.push_back(level);
    }
  }
  return lake;
}

// The largest of each texel's neighbours within r texels each way (a square), over an N x N grid
void maxFilter(std::vector<float>& v, int n, int r) {
  std::vector<float> tmp(v.size());
  parallelFor(n, [&](int j) {
    for (int i = 0; i < n; i++) {
      float m = -1e30f;
      for (int a = std::max(i - r, 0); a <= std::min(i + r, n - 1); a++) m = std::max(m, v[(size_t)j * n + a]);
      tmp[(size_t)j * n + i] = m;
    }
  });
  parallelFor(n, [&](int i) {
    for (int j = 0; j < n; j++) {
      float m = -1e30f;
      for (int b = std::max(j - r, 0); b <= std::min(j + r, n - 1); b++) m = std::max(m, tmp[(size_t)b * n + i]);
      v[(size_t)j * n + i] = m;
    }
  });
}
// Runway surfaces: the pack's codes to the game's
int surfaceOf(int s, int type) {
  switch (s) {
    case 1: case 2: return SURF_ASPHALT;
    case 3: case 5: return SURF_GRASS;
    case 4: return SURF_GRAVEL;
    case 6: return SURF_SAND;
    case 8: return SURF_SNOW;
    default: return type >= 1 ? SURF_ASPHALT : SURF_GRASS;
  }
}
// A separable box blur of radius r texels, twice (near enough a Gaussian), over an N x N grid
void blur(std::vector<float>& v, int n, int r) {
  std::vector<float> tmp(v.size());
  for (int pass = 0; pass < 2; pass++) {
    parallelFor(n, [&](int j) {
      const float* row = &v[(size_t)j * n];
      float sum = 0; int cnt = 0;
      for (int i = -r; i <= r; i++) if (i >= 0 && i < n) { sum += row[i]; cnt++; }
      for (int i = 0; i < n; i++) {
        tmp[(size_t)j * n + i] = sum / cnt;
        const int out = i - r, in = i + r + 1;
        if (out >= 0) { sum -= row[out]; cnt--; }
        if (in < n) { sum += row[in]; cnt++; }
      }
    });
    parallelFor(n, [&](int i) {
      float sum = 0; int cnt = 0;
      for (int j = -r; j <= r; j++) if (j >= 0 && j < n) { sum += tmp[(size_t)j * n + i]; cnt++; }
      for (int j = 0; j < n; j++) {
        v[(size_t)j * n + i] = sum / cnt;
        const int out = j - r, in = j + r + 1;
        if (out >= 0) { sum -= tmp[(size_t)out * n + i]; cnt--; }
        if (in < n) { sum += tmp[(size_t)in * n + i]; cnt++; }
      }
    });
  }
}
// The lat/lon box the chart's square lies in, with a margin
earth::Box chartBox(const regionTravel::Chart& chart, float half) {
  earth::Box b{90, 180, -90, -180};
  for (int k = 0; k <= 16; k++) {
    const float t = -half + 2.f * half * k / 16.f;
    for (const vec3& p : {vec3(t, 0, -half), vec3(t, 0, half), vec3(-half, 0, t), vec3(half, 0, t)}) {
      const geo::Position g = chart.position(p);
      b.south = std::min(b.south, g.latitude); b.north = std::max(b.north, g.latitude);
      b.west = std::min(b.west, g.longitude); b.east = std::max(b.east, g.longitude);
    }
  }
  b.south -= 0.02; b.north += 0.02; b.west -= 0.03; b.east += 0.03;
  return b;
}
} // namespace

// The airports of a real-Earth chart (earth_charts.h)
std::shared_ptr<const earthCharts::ChartAirports> earthCharts::chartAirports(const procedural::Descriptor& chartDesc,
                                                                            const std::shared_ptr<const earth::Pack>& pack,
                                                                            const earth::Sampler* given) {
  const regionTravel::Chart chart(chartDesc.origin);
  const earth::Box box = chartBox(chart, WORLD_HALF);
  struct Candidate { earth::AirportRecord rec; float x, z, heading, length, width, elev; int size; };
  // (where its main runway lies on the chart, and which way)
  auto placeRunway = [&](const earth::AirportRecord& a, Candidate& c) {
    const earth::Runway& r = a.runways[0];
    c.size = std::clamp(a.type, 0, 2);
    if (r.endsKnown) {
      const vec3 p1 = chart.local({r.lat1, r.lon1, 0}), p2 = chart.local({r.lat2, r.lon2, 0});
      c.x = 0.5f * (p1.x + p2.x); c.z = 0.5f * (p1.z + p2.z);
      c.heading = wrapDeg360(atan2f(p2.x - p1.x, -(p2.z - p1.z)) / DEG);
      c.length = std::max(length(vec3(p2.x - p1.x, 0, p2.z - p1.z)), 300.f);
    } else {
      const vec3 p = chart.local({a.lat, a.lon, 0});
      c.x = p.x; c.z = p.z;
      // the runway's true heading, or failing that its number's (magnetic: near enough for a strip without one), turned
      // into the chart's (its north at the field, which the chart's -z is not quite, away from the origin)
      float hd = r.hasHeading ? r.heading : 0.f;
      if (!r.hasHeading) { const int n = atoi(r.ident1.c_str()); if (n >= 1 && n <= 36) hd = n * 10.f; }
      hd += (float)chart.bearing(vec3(c.x, 0, c.z), {a.lat + 0.05, a.lon, 0}).value_or(0.0);
      c.heading = wrapDeg360(hd); c.length = std::max(r.length, 300.f);
    }
    c.width = r.width > 5.f ? std::min(r.width, 75.f) : (c.size == 2 ? 45.f : c.size == 1 ? 30.f : 20.f);
  };
  std::vector<Candidate> cands;
  for (auto& a : pack->airports(box)) {
    if (a.runways.empty()) continue;
    Candidate c;
    placeRunway(a, c);
    if (std::max(std::fabs(c.x), std::fabs(c.z)) > WORLD_HALF - kAirportMargin - 0.5f * c.length) continue;
    // the runway's height: the ground's along it (the median of nine points: a stray canopy or a ditch doesn't count).
    // Asked about without the chart's sampler - the career's boards and travel, on the game's thread - the airport's
    // own elevation, and nothing of the ground read (a streamed pack's tiles never fetched there)
    if (given) {
      float hs[9];
      const float hd = c.heading * DEG;
      for (int k = 0; k < 9; k++) {
        const float t = (k / 8.f - 0.5f) * c.length * 0.9f;
        const geo::Position g = chart.position(vec3(c.x + sinf(hd) * t, 0, c.z - cosf(hd) * t));
        hs[k] = given->elevation(g.latitude, g.longitude);
      }
      std::sort(hs, hs + 9);
      c.elev = std::max(hs[4], 1.5f);
    } else c.elev = std::max(a.hasElevation ? a.elevation : 0.f, 1.5f);
    c.rec = std::move(a);
    cands.push_back(std::move(c));
  }
  std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) {
    return a.size != b.size ? a.size > b.size : a.length != b.length ? a.length > b.length : a.rec.ident < b.rec.ident; });
  std::vector<Candidate> chosen;
  for (auto& c : cands) {
    if (chosen.size() >= kMaxAirports) break;
    bool clear = true;
    for (const auto& o : chosen) {
      const float reach = 0.5f * (c.length + o.length) + 1400.f;
      if (hypotf(c.x - o.x, c.z - o.z) < reach) { clear = false; break; }
    }
    if (clear) chosen.push_back(std::move(c));
  }
  // None in the square - the open ocean, the wilds: the nearest beyond it, so the flight has one to name and to steer for
  // (a World has an airport: the game's flight, its GPS and its ATC count on one). It lies outside the square, where
  // nothing of it is built; the chart that holds it builds it when the flight gets there
  if (chosen.empty()) {
    const geo::Position o = chartDesc.origin;
    const earth::AirportRecord* best = nullptr;
    double bestD = 1e18;
    std::vector<earth::AirportRecord> found;
    for (double r : {3.0, 10.0, 30.0, 90.0, 360.0}) {
      const double dLon = r / std::max(0.05, std::cos(o.latitude * geo::kDegrees));
      found = pack->airports(r >= 360.0 || dLon >= 180.0 ? earth::Box{-90, -180, 90, 180} :
                             earth::Box{std::max(-90.0, o.latitude - r), o.longitude - dLon, std::min(90.0, o.latitude + r), o.longitude + dLon});
      for (const auto& a : found) {
        if (a.runways.empty()) continue;
        const double d = geo::surfaceDistance(o, {a.lat, a.lon, 0});
        if (d < bestD || (d == bestD && best && a.ident < best->ident)) { bestD = d; best = &a; }
      }
      if (best && bestD < r * 111000.0) break;   // (nearer than the search reaches: none nearer outside it)
      best = nullptr; bestD = 1e18;
    }
    if (best) {
      Candidate c;
      placeRunway(*best, c);
      c.elev = std::max(best->hasElevation ? best->elevation : 0.f, 1.5f);
      c.rec = *best;
      chosen.push_back(std::move(c));
    }
  }
  auto out = std::make_shared<earthCharts::ChartAirports>();
  out->codes.reserve(chosen.size()); out->names.reserve(chosen.size()); out->blurbs.reserve(chosen.size());
  for (const auto& c : chosen) {
    out->codes.push_back(c.rec.ident);
    out->names.push_back(c.rec.name);
    std::string where = c.rec.municipality;
    if (!c.rec.country.empty()) where += (where.empty() ? "" : ", ") + c.rec.country;
    out->blurbs.push_back(where);
    out->records.push_back(c.rec);
  }
  for (size_t i = 0; i < chosen.size(); i++) {
    const auto& c = chosen[i];
    Airport a{out->codes[i].c_str(), out->names[i].c_str(), c.x, c.z, c.elev, c.heading, c.length, c.width,
              surfaceOf(c.rec.runways[0].surface, c.size), c.size, out->blurbs[i].c_str()};
    a.hospital = c.size >= 1;
    out->airports.push_back(a);
  }
  // their other runways: OurAirports' with both ends known, 250 m or more and not on the water, up to five an airport,
  // each laid on the chart by its ends (the main one over again, its ends the other way about, left out)
  for (size_t i = 0; i < chosen.size(); i++) {
    const auto& c = chosen[i];
    int kept = 0;
    for (size_t k = 1; k < c.rec.runways.size() && kept < 5; k++) {
      const earth::Runway& r = c.rec.runways[k];
      if (!r.endsKnown || r.length < 250.f || r.surface == 7) continue;
      const vec3 p1 = chart.local({r.lat1, r.lon1, 0}), p2 = chart.local({r.lat2, r.lon2, 0});
      ExtraRunway x{(int)i, 0.5f * (p1.x + p2.x), 0.5f * (p1.z + p2.z), wrapDeg360(atan2f(p2.x - p1.x, -(p2.z - p1.z)) / DEG),
                    length(vec3(p2.x - p1.x, 0, p2.z - p1.z)), 0.f, surfaceOf(r.surface, c.size)};
      if (x.length < 250.f || std::max(std::fabs(x.x), std::fabs(x.z)) > WORLD_HALF - 0.5f * x.length - 500.f) continue;
      if (hypotf(x.x - c.x, x.z - c.z) < 30.f && std::fabs(std::remainder(x.heading - c.heading, 180.f)) < 3.f) continue;
      x.width = r.width > 5.f ? std::min(r.width, 75.f) : (c.size == 2 ? 45.f : c.size == 1 ? 30.f : 20.f);
      out->extras.push_back(x);
      kept++;
    }
  }
  return out;

}

void World::buildEarth(const procedural::Descriptor& chartDesc, const std::string& cachePath, const std::string& stamp, bool save) {
  generationCheckpoint();
  auto pack = earthCharts::pack();
  if (!pack) throw std::runtime_error("The real Earth needs its data pack: " + earthCharts::packError());
  regionRecipe.reset();
  procedural = true; repeating = false; regionId = chartDesc.id; detailScale = kEarthDetailScale;
  auto data = std::make_shared<earthCharts::ChartData>();
  data->descriptor = chartDesc; data->pack = pack;
  const regionTravel::Chart chart(chartDesc.origin);
  // the detail noise's frame (World::noisePoint): this chart's x and z into the anchor's - its azimuthal x and z, in
  // double, from the centre of the 8-degree cell holding the origin - the turn the mean of the two axes'
  {
    const double cell = 8.0;
    const geo::Position anchor{(std::floor(chartDesc.origin.latitude / cell) + 0.5) * cell,
                               (std::floor(chartDesc.origin.longitude / cell) + 0.5) * cell, 0.0};
    auto anchored = [&](vec3 local, double& x, double& z) {
      const geo::Position g = chart.position(local);
      const double d = geo::surfaceDistance(anchor, g), b = geo::initialBearing(anchor, g).value_or(0.0) * geo::kDegrees;
      x = d * std::sin(b); z = -d * std::cos(b);
    };
    double ox, oz, ex, ez, sx, sz;
    anchored(vec3(0, 0, 0), ox, oz); anchored(vec3(2000, 0, 0), ex, ez); anchored(vec3(0, 0, 2000), sx, sz);
    const double turn = 0.5 * (std::atan2(ez - oz, ex - ox) + std::atan2(sz - oz, sx - ox) - 0.5 * geo::kPi);
    noiseCos = (float)std::cos(turn); noiseSin = (float)std::sin(turn); noiseX = (float)ox; noiseZ = (float)oz;
    // (and the same frame exactly, on a grid the natural scenery reads: World::anchorPoint)
    const int G = kAnchorN;
    anchorGrid.assign((size_t)(G + 1) * (G + 1) * 2, 0.0);
    for (int j = 0; j <= G; j++)
      for (int i = 0; i <= G; i++) {
        double ax, az;
        anchored(vec3(-WORLD_HALF + 2.f * WORLD_HALF * i / G, 0, -WORLD_HALF + 2.f * WORLD_HALF * j / G), ax, az);
        anchorGrid[((size_t)j * (G + 1) + i) * 2] = ax; anchorGrid[((size_t)j * (G + 1) + i) * 2 + 1] = az;
      }
  }
  const earth::Box box = chartBox(chart, WORLD_HALF);
  earth::Sampler sampler(pack, box, true);
  data->fineTiles = sampler.fineTiles();

  // ---- the airports (earthCharts::chartAirports: the career's boards read the same)
  const auto picked = earthCharts::chartAirports(chartDesc, pack, &sampler);
  data->codes = picked->codes; data->names = picked->names; data->blurbs = picked->blurbs; data->airports = picked->records;
  airports.clear();
  for (size_t i = 0; i < picked->airports.size(); i++) {
    Airport a = picked->airports[i];
    a.code = data->codes[i].c_str(); a.name = data->names[i].c_str(); a.blurb = data->blurbs[i].c_str();
    airports.push_back(a);
  }
  extraRunways = picked->extras;
  // (the side each airport's buildings and apron stand on, its own in every chart that holds it - never by its place in
  // this chart's list, which the next orders its own way: away from its other runways, else as its ident has it)
  for (size_t i = 0; i < airports.size(); i++) {
    Airport& a = airports[i];
    float across = 0.f;
    for (const ExtraRunway& r : extraRunways) if (r.airport == (int)i) across += aptLocal(a, vec3(r.x, 0, r.z)).y;
    uint32_t h = 2166136261u;
    for (const char* c = a.code; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
    a.side = across > 50.f ? -1 : across < -50.f ? 1 : (h >> 7) & 1 ? 1 : -1;
  }
  {   // the way houses are built here: the country most of its airports are in (the chart's own airport with none)
    std::map<std::string, int> count;
    for (const auto& r : data->airports) count[r.country]++;
    std::string country; int most = 0;
    for (const auto& [c, n] : count) if (n > most) { most = n; country = c; }
    buildingStyle = earthCharts::buildingStyleOf(country, chartDesc.origin.latitude, chartDesc.origin.longitude);
  }

  // ---- the towns: GeoNames' places in the square, the most telling first - the bigger and the nearer the chart's
  // centre, where the flight starts (a place's people over 1 + (its distance / 15 km)^2: a city counts from anywhere in
  // the square, a village by the field before a town at its edge) - a smaller one close by taken in by it
  struct Place { std::string name; float x, z, r; int kind; uint32_t pop; float score; };
  std::vector<Place> places;
  for (const auto& p : pack->places(box)) {
    const vec3 at = chart.local({p.lat, p.lon, 0});
    if (std::max(std::fabs(at.x), std::fabs(at.z)) > WORLD_HALF - 2500.f) continue;
    if (sampler.cover(p.lat, p.lon) == earth::COVER_SEA) continue;
    Place pl; pl.name = p.name; pl.x = at.x; pl.z = at.z; pl.pop = p.population;
    pl.kind = p.population >= 100000 ? 2 : p.population >= 5000 ? 1 : 0;
    pl.r = std::clamp(12.f * powf((float)std::max<uint32_t>(p.population, 300), 0.45f), 150.f, 1800.f);
    const float d = hypotf(at.x, at.z) / 15000.f;
    pl.score = (float)p.population / (1.f + d * d);
    places.push_back(pl);
  }
  std::sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.score != b.score ? a.score > b.score : a.name < b.name; });
  std::vector<Place> kept;
  for (const auto& p : places) {
    if (kept.size() >= kMaxTowns) break;
    bool free = true;
    for (const auto& k : kept) if (hypotf(p.x - k.x, p.z - k.z) < std::max(1.15f * (k.r + p.r), 1500.f)) { free = false; break; }
    for (const auto& a : airports) if (free && hypotf(p.x - a.x, p.z - a.z) < 0.5f * a.length + p.r + 500.f) free = false;
    if (free) kept.push_back(p);
  }
  // ---- the built-up land between them: the cover's built-up class on a 400 m grid, each stretch of it covered by
  // districts - a town each, about 2.6 km apart, named for the nearest place - so a city's suburbs, and the towns grown
  // into each other, are built over as they are rather than left as open fields between the places' circles. The
  // densest first, the nearer the centre the sooner (as the places), none on another town's or an airport's grounds
  {
    constexpr float kCell = 400.f, kReach = 1300.f;
    const int G = (int)(2 * WORLD_HALF / kCell);
    std::vector<uint8_t> built((size_t)G * G, 0);
    auto centreOf = [&](int i) { return -WORLD_HALF + (i + 0.5f) * kCell; };
    for (int j = 0; j < G; j++) for (int i = 0; i < G; i++) {
      int n = 0;
      for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) {
        const geo::Position g = chart.position(vec3(centreOf(i) + (a - 0.5f) * 0.5f * kCell, 0, centreOf(j) + (b - 0.5f) * 0.5f * kCell));
        n += sampler.cover(g.latitude, g.longitude) == earth::COVER_BUILT;
      }
      built[(size_t)j * G + i] = n >= 2;
    }
    const int reachCells = (int)std::ceil(kReach / kCell);
    auto builtNear = [&](int i, int j) {
      int n = 0;
      for (int b = -reachCells; b <= reachCells; b++) for (int a = -reachCells; a <= reachCells; a++) {
        const int u = i + a, v = j + b;
        if (u < 0 || v < 0 || u >= G || v >= G || (a * a + b * b) * kCell * kCell > kReach * kReach) continue;
        n += built[(size_t)v * G + u];
      }
      return n;
    };
    struct Seed { int i, j, n; float score; };
    std::vector<Seed> seeds;
    for (int j = 0; j < G; j++) for (int i = 0; i < G; i++) {
      if (!built[(size_t)j * G + i] || std::max(std::fabs(centreOf(i)), std::fabs(centreOf(j))) > WORLD_HALF - 2500.f) continue;
      const int n = builtNear(i, j);
      if (n < 8) continue;   // (a stretch of at least about 1.3 km^2 round it)
      const float d = hypotf(centreOf(i), centreOf(j)) / 15000.f;
      seeds.push_back({i, j, n, n / (1.f + d * d)});
    }
    std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) { return a.score != b.score ? a.score > b.score : std::tie(a.j, a.i) < std::tie(b.j, b.i); });
    size_t districts = 0;
    for (const Seed& sd : seeds) {
      if (districts >= kMaxDistricts) break;
      const float x = centreOf(sd.i), z = centreOf(sd.j);
      const float r = std::clamp(sqrtf(sd.n * kCell * kCell / PI), 500.f, kReach);
      bool free = true;
      for (const auto& k : kept) if (hypotf(x - k.x, z - k.z) < std::max(0.95f * (k.r + r), 1500.f)) { free = false; break; }
      // (its centre off an airport's grounds: its lots keep off them themselves, airportInfluence)
      for (const auto& a : airports) if (free && hypotf(x - a.x, z - a.z) < 0.5f * a.length + 0.5f * r + 300.f) free = false;
      if (!free) continue;
      // named for the nearest place, a city's district when one is near
      const Place* name = nullptr; float best = 1e30f;
      for (const auto& p : places) { const float d = hypotf(p.x - x, p.z - z); if (d < best) { best = d; name = &p; } }
      int kind = 1;
      for (const auto& k : kept) if (k.kind == 2 && hypotf(x - k.x, z - k.z) < 9000.f) kind = 2;
      Place pl; pl.name = name ? name->name : std::string("Town"); pl.x = x; pl.z = z; pl.r = r; pl.kind = kind; pl.pop = 0; pl.score = sd.score;
      kept.push_back(pl); districts++;
    }
  }
  data->towns.reserve(kept.size());
  for (const auto& p : kept) data->towns.push_back(p.name);
  towns.clear();
  for (size_t i = 0; i < kept.size(); i++) towns.push_back(Town{data->towns[i].c_str(), kept[i].x, kept[i].z, kept[i].r, kept[i].kind});
  earthChart = data;

  // the cache: the chart, the pack's content and the build (and the GLO-90 tiles it found: a chart built before its
  // fine tiles were downloaded is built again once they are)
  uint64_t h = 1469598103934665603ull;
  const std::string key = chartDesc.id + ":earth-" + std::to_string(earthCharts::kGeneratorVersion) + ":" + pack->contentId() +
    ":fine" + std::to_string(data->fineTiles) + ":" + stamp;
  for (unsigned char ch : key) { h ^= ch; h *= 1099511628211ull; }
  char cacheStamp[64]; snprintf(cacheStamp, sizeof(cacheStamp), "earth-%016llx", (unsigned long long)h);
  buildConfigured(cachePath, cacheStamp, save);
}

// hm from the pack: (base height, detail amplitude, lushness, coldness) per texel, and the land cover kept for the mask
void World::buildEarthTerrain() {
  auto& data = *earthChart;
  const int N = HM_N;
  const size_t NN = (size_t)N * N;
  const regionTravel::Chart chart(data.descriptor.origin);
  earth::Sampler sampler(data.pack, chartBox(chart, WORLD_HALF), true);
  std::vector<float> elev(NN), bathy(NN, 0.f), lat(NN);
  std::vector<uint8_t> sea(NN, 0), fine(NN, 0);
  data.cover.assign(NN, 0);
  const bool reports = this == &g_world;
  std::atomic<int> rows{0};
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++) {
      const size_t k = (size_t)j * N + i;
      const float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL;
      const geo::Position g = chart.position(vec3(x, 0, z));
      const float e = sampler.elevation(g.latitude, g.longitude);
      int c = sampler.cover(g.latitude, g.longitude);
      elev[k] = e; lat[k] = (float)g.latitude; fine[k] = sampler.fine(g.latitude, g.longitude);
      // the sea: the cover's open sea, and its water at the sea's level - the harbours, the estuaries, the salt ponds
      // behind the dunes (water at a lake's height stays the lake it is)
      const bool isSea = (c == earth::COVER_SEA && e <= 2.f) || (c == earth::COVER_WATER && e <= 1.5f) ||
                         (c == earth::COVER_UNKNOWN && e <= 0.f);
      sea[k] = isSea;
      if (isSea) bathy[k] = sampler.coarseElevation(g.latitude, g.longitude);
      data.cover[k] = (uint8_t)c;
    }
    if (reports) worldLoadProgress(.02f + .14f * float(rows.fetch_add(1) + 1) / N);
  });
  generationCheckpoint();
  // the lakes (findLakes); one at the sea's level beside it is the sea's (a salt pond behind the dunes, a lagoon)
  std::vector<float> levels;
  std::vector<int> lakeOf = findLakes(elev, sea, data.cover, fine, N, levels);
  {
    std::vector<uint8_t> tidal(levels.size(), 0);
    for (size_t k = 0; k < NN; k++) {
      const int id = lakeOf[k];
      if (id < 0 || levels[id] >= 2.5f || tidal[id]) continue;
      const int i = (int)(k % N), j = (int)(k / N);
      for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const int a = i + di, b = j + dj;
        if (a >= 0 && b >= 0 && a < N && b < N && sea[(size_t)b * N + a]) tidal[id] = 1;
      }
    }
    for (size_t k = 0; k < NN; k++) if (lakeOf[k] >= 0 && tidal[lakeOf[k]]) { sea[k] = 1; bathy[k] = -1.f; lakeOf[k] = -1; }
  }
  // the canopy and the roofs the surface model holds, blurred across the classes' edges
  std::vector<float> canopy(NN);
  for (size_t k = 0; k < NN; k++) canopy[k] = sea[k] ? 0.f : canopyOf(data.cover[k]);
  blur(canopy, N, 3);
  // how far each sea texel is from the land (m, a chamfer distance): the floor shelves away from the coast
  std::vector<float> toLand(NN, 1e9f);
  for (size_t k = 0; k < NN; k++) if (!sea[k]) toLand[k] = 0.f;
  const float T = HM_TEXEL, D = T * 1.41421356f;
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    float& d = toLand[(size_t)j * N + i];
    if (i > 0) d = std::min(d, toLand[(size_t)j * N + i - 1] + T);
    if (j > 0) { d = std::min(d, toLand[(size_t)(j - 1) * N + i] + T); if (i > 0) d = std::min(d, toLand[(size_t)(j - 1) * N + i - 1] + D); if (i + 1 < N) d = std::min(d, toLand[(size_t)(j - 1) * N + i + 1] + D); }
  }
  for (int j = N - 1; j >= 0; j--) for (int i = N - 1; i >= 0; i--) {
    float& d = toLand[(size_t)j * N + i];
    if (i + 1 < N) d = std::min(d, toLand[(size_t)j * N + i + 1] + T);
    if (j + 1 < N) { d = std::min(d, toLand[(size_t)(j + 1) * N + i] + T); if (i + 1 < N) d = std::min(d, toLand[(size_t)(j + 1) * N + i + 1] + D); if (i > 0) d = std::min(d, toLand[(size_t)(j + 1) * N + i - 1] + D); }
  }
  generationCheckpoint();
  hm.assign(NN * 4, 0.f);
  std::vector<float> lush(NN);
  for (size_t k = 0; k < NN; k++) {
    float* t = &hm[k * 4];
    if (sea[k]) {
      t[0] = std::min(std::clamp(bathy[k], -200.f, -1.f), -0.8f - 0.004f * std::min(toLand[k], 5e4f));
      t[1] = 0.f;
    } else {
      // (land never below the sea, the sea everywhere y = 0; and the canopy or the roofs never taken off below 2 m where
      // the surface model stands over it - a city's waterfront, its filled land, stay dry and buildable)
      t[0] = std::max(std::max(elev[k] - canopy[k], std::min(elev[k], 2.f)), 0.6f);
      t[1] = 1.f;   // (the amplitude itself below)
    }
    lush[k] = sea[k] ? 0.f : earthCharts::lushOf(data.cover[k], lat[k]);
  }
  blur(lush, N, 1);
  // the lakes into the ground: each one's bed a metre and a half under its level, flat; the land within 6 texels of it
  // at least a metre and a half over it (the canopy taken off a wooded shore would leave it under the water), unless it
  // lies under the lake to begin with (below a dam); and the code of its level on its texels and on those beside them
  // (World::lakes): the ground never under the level there, and water where the natural ground lies under it
  lakes.clear();
  std::vector<uint8_t> nearLake;
  if (!levels.empty()) {
    lakes.assign(NN, 0);
    std::vector<float> top(NN, -1e9f);
    for (size_t k = 0; k < NN; k++) {
      if (lakeOf[k] < 0) continue;
      const float level = levels[lakeOf[k]];
      hm[k * 4] = level - 1.5f; hm[k * 4 + 1] = 0.f;
      top[k] = level + 1.5f;
      lakes[k] = World::lakeCode(level);
    }
    maxFilter(top, N, 6);
    nearLake.assign(NN, 0);
    for (size_t k = 0; k < NN; k++) {
      if (lakeOf[k] >= 0 || sea[k] || top[k] < -1e8f || elev[k] < top[k] - 4.f) continue;
      hm[k * 4] = std::max(hm[k * 4], top[k]);
      nearLake[k] = 1;
    }
    for (size_t k = 0; k < NN; k++) {
      if (lakeOf[k] < 0) continue;
      const int i = (int)(k % N), j = (int)(k / N);
      for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
        const int a = i + di, b = j + dj;
        if (a < 0 || b < 0 || a >= N || b >= N) continue;
        const size_t n = (size_t)b * N + a;
        if (lakeOf[n] < 0 && nearLake[n]) lakes[n] = std::max(lakes[n], lakes[k]);
      }
    }
  }
  // the detail's amplitude from the lie of the land: rougher ground more of it; where the fine data isn't there (ETOPO
  // only, 0.9 km) much more, standing in for the relief it hasn't got
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++) {
      const size_t k = (size_t)j * N + i;
      float* t = &hm[k * 4];
      if (t[1] <= 0.f) continue;
      const int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, N - 1), j0 = std::max(j - 1, 0), j1 = std::min(j + 1, N - 1);
      const float gx = (hm[((size_t)j * N + i1) * 4] - hm[((size_t)j * N + i0) * 4]) / ((i1 - i0) * HM_TEXEL);
      const float gz = (hm[((size_t)j1 * N + i) * 4] - hm[((size_t)j0 * N + i) * 4]) / ((j1 - j0) * HM_TEXEL);
      const float slope = sqrtf(gx * gx + gz * gz);
      t[1] = fine[k] ? 8.f + 14.f * smoothstepf(0.03f, 0.4f, slope) : 14.f + 60.f * smoothstepf(0.02f, 0.3f, slope);
      t[1] *= smoothstepf(0.6f, 3.f, t[0]);   // (the shore's flats: little)
      if (!nearLake.empty() && nearLake[k]) t[1] = std::min(t[1], 4.f);   // (nor much by a lake: no hollows under its level)
    }
  });
  // the beaches: the low land within two texels of the sea, its lushness all but none - the ground material's sand
  // keeps to them on the real Earth (terrain_material.glsl), not to every flat a few metres up (a marsh, a harbour's
  // landfill, an airfield)
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    const size_t k = (size_t)j * N + i;
    if (sea[k] || hm[k * 4] > 3.5f) continue;
    bool shore = false;
    for (int dj = -2; dj <= 2 && !shore; dj++) for (int di = -2; di <= 2 && !shore; di++) {
      const int a = i + di, b = j + dj;
      shore = a >= 0 && b >= 0 && a < N && b < N && sea[(size_t)b * N + a];
    }
    if (shore) lush[k] = std::min(lush[k], 0.12f);
  }
  // lushness and coldness: the cover, the latitude and the height (the snowline about 5 km up in the tropics, at sea
  // level past 75 degrees)
  for (size_t k = 0; k < NN; k++) {
    float* t = &hm[k * 4];
    t[2] = lush[k];
    const float alat = std::fabs(lat[k]);
    const float snowline = 5200.f * std::clamp(1.f - (alat - 20.f) / 55.f, 0.f, 1.f);
    const float coldLat = 0.85f * smoothstepf(52.f, 72.f, alat);
    const float coldHigh = smoothstepf(snowline - 900.f, snowline, t[0]);
    t[3] = std::max({coldLat, coldHigh, data.cover[k] == earth::COVER_SNOW ? 1.f : 0.f});
    if (sea[k]) t[3] = coldLat;
  }
  generationCheckpoint();
  // the airports' grounds: levelled to the runway's height, and the approaches kept clear of the ground (computeTexel's
  // rules, at a real approach surface's 1 in 20)
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++) {
      float* t = &hm[((size_t)j * N + i) * 4];
      const float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL;
      for (const Airport& a : airports) {
        const float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
        const float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
        const float d = u - a.length * 0.5f;
        if (d > 0 && d < 7000) {
          const float halfw = 250.f + 0.18f * d;
          const float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
          const float cap = a.elev + 10.f + 0.05f * d;
          if (t[0] > cap) { t[0] = lerpf(t[0], cap, wc); t[1] = lerpf(t[1], std::min(t[1], 4.f), wc); }
        }
        const float hu = a.length * 0.5f + 260.f, hv = a.size == 2 ? 420.f : 240.f;
        const float du = std::max(0.f, u - hu), dv = std::max(0.f, v - hv);
        const float w = 1.f - smoothstepf(0.f, 550.f, sqrtf(du * du + dv * dv));
        if (w <= 0.f) continue;
        t[0] = lerpf(t[0], a.elev, w);
        t[1] *= 1.f - w;
        t[2] = lerpf(t[2], std::max(t[2], 0.5f), w);
      }
      // (each airport's other runways the same, on its height: their approaches clear, a strip of level ground along them)
      for (const ExtraRunway& r : extraRunways) {
        const float elev = airports[r.airport].elev;
        const float hd = r.heading * DEG, dx = x - r.x, dz = z - r.z;
        const float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
        const float d = u - r.length * 0.5f;
        if (d > 0 && d < 7000) {
          const float halfw = 250.f + 0.18f * d;
          const float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
          const float cap = elev + 10.f + 0.05f * d;
          if (t[0] > cap) { t[0] = lerpf(t[0], cap, wc); t[1] = lerpf(t[1], std::min(t[1], 4.f), wc); }
        }
        const float du = std::max(0.f, u - (r.length * 0.5f + 150.f)), dv = std::max(0.f, v - (r.width * 0.5f + 90.f));
        const float w = 1.f - smoothstepf(0.f, 400.f, sqrtf(du * du + dv * dv));
        if (w <= 0.f) continue;
        t[0] = lerpf(t[0], elev, w);
        t[1] *= 1.f - w;
        t[2] = lerpf(t[2], std::max(t[2], 0.5f), w);
      }
    }
  });
}

procedural::Descriptor World::chartDescriptor() const {
  if (earthChart) return earthChart->descriptor;
  if (regionRecipe) return regionRecipe->descriptor;
  return procedural::solaceDescriptor();
}

// The pack's roads in the square, each laid on the ground (graded, bridged over water) as the network's own are
RoadNetwork World::buildEarthRoads() const {
  RoadNetwork net;
  const auto& data = *earthChart;
  const regionTravel::Chart chart(data.descriptor.origin);
  for (const Town& t : towns) net.nodes.push_back({t.x, t.z, t.kind, 0});
  for (const Airport& a : airports)
    if (std::max(std::fabs(a.x), std::fabs(a.z)) < WORLD_HALF) net.nodes.push_back({a.x, a.z, 3, 0});   // (not one beyond the square)
  const auto edges = data.pack->roads(chartBox(chart, WORLD_HALF - kRoadMargin), false);
  const float lim = WORLD_HALF - kRoadMargin;
  for (const auto& e : edges) {
    generationCheckpoint();
    RoadClass cls = e.cls == 1 ? RC_HIGHWAY : e.cls == 2 || e.cls == 3 ? RC_ROAD : e.cls == 5 ? RC_TRACK : RC_LANE;
    std::vector<vec2> run;
    auto flush = [&]() {
      if (run.size() >= 2) {
        float len = 0; for (size_t k = 1; k < run.size(); k++) len += length(run[k] - run[k - 1]);
        if (len >= 60.f) {
          const float h0 = groundHeight(run.front().x, run.front().y), h1 = groundHeight(run.back().x, run.back().y);
          RoadPath p = layRoad(*this, cls, run, std::max(h0, 0.6f), std::max(h1, 0.6f), {}, true);
          if (p.pts.size() >= 2) net.paths.push_back(std::move(p));
        }
      }
      run.clear();
    };
    for (const auto& ll : e.pts) {
      const vec3 p = chart.local({ll.first, ll.second, 0});
      if (std::max(std::fabs(p.x), std::fabs(p.z)) > lim) { flush(); continue; }
      if (!run.empty() && length(vec2(p.x, p.z) - run.back()) < 1.f) continue;
      run.push_back(vec2(p.x, p.z));
    }
    flush();
    if (net.paths.size() >= 60000) break;   // (RoadSegment's 16-bit path index)
  }
  return net;
}

void earthCharts::maskFields(const ChartData& chart, std::vector<float>& forest, std::vector<float>& farm) {
  const int N = HM_N;
  const size_t NN = (size_t)N * N;
  forest.assign(NN, 0.f); farm.assign(NN, 0.f);
  if (chart.cover.size() != NN) return;
  for (size_t k = 0; k < NN; k++) {
    const int c = chart.cover[k];
    forest[k] = earth::coverClosedForest(c) ? 1.f : earth::coverForest(c) ? 0.6f : c == earth::COVER_WETLAND || c == earth::COVER_SHRUBS ? 0.25f : 0.f;
    farm[k] = c == earth::COVER_CROPLAND ? 1.f : c == earth::COVER_HERBACEOUS ? 0.35f : 0.f;
  }
  blur(forest, N, 2); blur(farm, N, 2);
}
