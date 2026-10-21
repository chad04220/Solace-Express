// Somnium Volandi - charts on the real Earth: the lattice, their names, the pack (earth_charts.h)
#include "earth_charts.h"
#include "earth_fetch.h"
#include <cctype>
#include <cstdlib>

namespace earthCharts {
namespace {
std::mutex g_lock;
std::shared_ptr<const earth::Pack> g_pack;
std::string g_error, g_glo90Cache;
std::vector<std::string> g_search;
bool g_opened = false;

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
int latIndex(double lat) { return (int)std::lround(std::clamp(lat, -89.0, 89.0) * kLattice); }
int lonIndex(double lon) {
  int i = (int)std::lround(lon * kLattice);
  const int full = 360 * kLattice;
  i = ((i + full / 2) % full + full) % full - full / 2;   // [-180, 180)
  return i;
}
std::string chartId(int la, int lo) { return std::string(kIdPrefix) + std::to_string(la) + ":" + std::to_string(lo); }
uint64_t fnv(const std::string& s) { uint64_t h = 1469598103934665603ull; for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } return h; }

// The name a chart goes by: the largest place within 30 km of its origin, else its main airport's town, else where
// it is
std::string nameFor(double lat, double lon) {
  auto p = pack();
  if (p) {
    const double dLat = 0.3, dLon = 0.3 / std::max(0.2, std::cos(lat * geo::kDegrees));
    const earth::Box box{lat - dLat, lon - dLon, lat + dLat, lon + dLon};
    const geo::Position at{lat, lon, 0};
    const earth::PlaceRecord* best = nullptr;
    auto places = p->places(box);
    for (const auto& pl : places)
      if (geo::surfaceDistance(at, {pl.lat, pl.lon, 0}) < 30000. && (!best || pl.population > best->population)) best = &pl;
    if (best) return best->name;
    for (const auto& a : p->airports(box)) if (!a.municipality.empty()) return a.municipality;
  }
  char b[64];
  snprintf(b, sizeof(b), "%.2f%c %.2f%c", std::abs(lat), lat >= 0 ? 'N' : 'S', std::abs(lon), lon >= 0 ? 'E' : 'W');
  return b;
}
} // namespace

std::shared_ptr<const earth::Pack> pack() {
  std::lock_guard<std::mutex> g(g_lock);
  if (!g_opened) {
    g_opened = true;
    if (!earth::hasRangeFetch()) earth::setRangeFetch(earthFetch::fetchRange);   // (a streamed pack's pieces, fetched as wanted)
    const char* env = getenv("EARTH_PACK");
    std::vector<std::string> dirs;
    if (env && *env) dirs.push_back(env);
    else { dirs = g_search; dirs.push_back("earth"); }
    for (const auto& dir : dirs) {
      std::string why;
      auto p = earth::Pack::open(dir, &why);
      if (p) { if (!g_glo90Cache.empty()) p->setGlo90Cache(g_glo90Cache); g_pack = std::move(p); g_error.clear(); break; }
      g_error += (g_error.empty() ? "" : "; ") + why;
    }
  }
  return g_pack;
}
void setSearch(std::vector<std::string> dirs, std::string glo90Cache) {
  std::lock_guard<std::mutex> g(g_lock);
  g_search = std::move(dirs); g_glo90Cache = std::move(glo90Cache);
}
std::string glo90Cache() { std::lock_guard<std::mutex> g(g_lock); return g_glo90Cache; }
std::string packError() { pack(); std::lock_guard<std::mutex> g(g_lock); return g_error; }
void reopen() {
  std::lock_guard<std::mutex> g(g_lock);
  g_pack.reset(); g_opened = false; g_error.clear();
}
void usePack(std::shared_ptr<const earth::Pack> p) {
  if (!earth::hasRangeFetch()) earth::setRangeFetch(earthFetch::fetchRange);
  std::lock_guard<std::mutex> g(g_lock);
  g_pack = std::move(p); g_opened = true; g_error = g_pack ? std::string() : "no Earth pack";
}

// How lush the ground grows for each class (hm's third channel, 0 bare .. 1 rainforest)
float lushOf(int cover, double lat) {
  if (earth::coverForest(cover)) {
    const int type = earth::coverForestType(cover);
    const float tropical = std::fabs(lat) < 23.5 && type == 2 ? 1.f : 0.f;   // (evergreen broadleaf in the tropics)
    return earth::coverClosedForest(cover) ? 0.86f + 0.12f * tropical : 0.74f + 0.1f * tropical;
  }
  switch (cover) {
    case earth::COVER_CROPLAND: return 0.72f;
    case earth::COVER_HERBACEOUS: return 0.58f;
    case earth::COVER_WETLAND: return 0.8f;
    case earth::COVER_SHRUBS: return 0.4f;
    case earth::COVER_BUILT: return 0.5f;
    case earth::COVER_MOSS: return 0.28f;
    case earth::COVER_BARE: return 0.08f;
    case earth::COVER_SNOW: return 0.15f;
    default: return 0.55f;
  }
}

bool isEarthId(const std::string& id) { return id.compare(0, 6, kIdPrefix) == 0; }

int buildingStyleOf(const std::string& country, double lat, double lon) {
  auto in = [&](const char* list) {   // (two letters at a time)
    for (const char* c = list; c[0] && c[1]; c += 2) if (country.size() == 2 && country[0] == c[0] && country[1] == c[1]) return true;
    return false;
  };
  if (country == "US" && lon < -103.0 && lat < 36.5) return STYLE_DRY;   // (the south-west: stucco and adobe)
  if (in("USCAAUNZ")) return STYLE_NORTH_AMERICA;
  if (in("NOSEFIISFOGLEELV")) return STYLE_NORDIC;
  if (in("JPCNKRKPTWHKMOMN")) return STYLE_EAST_ASIA;
  if (in("SAAEQAKWOMBHYEIRIQEGLYDZMATNJOILSYPKAFTMUZKZEHMRNETDSDML")) return STYLE_DRY;
  if (in("ESPTITGRMTCYHRMEALMKTRMCADSMVALBMXARCLUYPE")) return STYLE_MEDITERRANEAN;
  if (in("GBIENLBEDEFRLUCHATDKPLCZSKHUSIROBGRSUABYLTMDBARS")) return STYLE_EUROPE;
  return std::fabs(lat) < 25.0 ? STYLE_TROPICS : STYLE_EUROPE;
}

static procedural::Descriptor descriptorAt(int la, int lo) {
  procedural::Descriptor d;
  d.id = chartId(la, lo);
  d.origin = {double(la) / kLattice, double(lo) / kLattice, 0.0};
  d.name = nameFor(d.origin.latitude, d.origin.longitude);
  d.seed = fnv(d.id);
  d.generatorVersion = procedural::kGeneratorVersion;   // (the far terrain's identity checks: one with the regions')
  d.address = geo::positionTile(d.origin, procedural::kAddressLevel).tile;
  d.archetype = procedural::Archetype::ContinentalFragments;
  d.scale = procedural::Scale::Large;
  d.authored = false;
  return d;
}

procedural::Descriptor chartAt(double lat, double lon) { return descriptorAt(latIndex(lat), lonIndex(lon)); }
std::string idAt(double lat, double lon) { return chartId(latIndex(lat), lonIndex(lon)); }
std::string regionPhrase(const procedural::Descriptor& region) {
  if (region.authored) return "the Solace Islands";
  return isEarthId(region.id) ? region.name : "the " + region.name;
}
std::string slideIdAt(double lat, double lon) {
  if (geo::surfaceDistance({lat, lon, 0}, geo::solaceOrigin()) < kSolaceReach) return "solace";
  return idAt(lat, lon);
}

std::optional<procedural::Descriptor> find(const std::string& id) {
  if (!isEarthId(id) || id.size() > 32) return std::nullopt;
  const char* s = id.c_str() + 6;
  char* end = nullptr;
  const long la = strtol(s, &end, 10);
  if (end == s || *end != ':') return std::nullopt;
  const char* t = end + 1;
  const long lo = strtol(t, &end, 10);
  if (end == t || *end != '\0') return std::nullopt;
  if (la < -89L * kLattice || la > 89L * kLattice || lo < -180L * kLattice || lo >= 180L * kLattice) return std::nullopt;
  if (chartId((int)la, (int)lo) != id) return std::nullopt;   // (canonical only: no leading zeros or signs)
  return descriptorAt((int)la, (int)lo);
}

std::optional<procedural::Descriptor> chartForAirport(const std::string& ident) {
  for (const auto& a : searchAirports(ident, 1)) {
    if (lower(a.ident) != lower(ident)) break;
    double lat = a.lat, lon = a.lon;
    if (!a.runways.empty() && a.runways[0].endsKnown) { lat = 0.5 * (a.runways[0].lat1 + a.runways[0].lat2); lon = 0.5 * (a.runways[0].lon1 + a.runways[0].lon2); }
    return chartAt(lat, lon);
  }
  return std::nullopt;
}

std::vector<earth::AirportRecord> searchAirports(const std::string& query, size_t limit) {
  std::vector<earth::AirportRecord> out;
  auto p = pack();
  const std::string q = lower(query);
  if (!p) return out;
  const earth::Box all = p->box();
  std::vector<earth::AirportRecord> named;
  for (auto& a : p->airports(all)) {
    if (q.empty()) { named.push_back(std::move(a)); continue; }   // (nothing typed: the largest first)
    const std::string ident = lower(a.ident);
    if (ident == q || lower(a.iata) == q) { out.push_back(std::move(a)); continue; }
    if (lower(a.name).find(q) != std::string::npos || lower(a.municipality).find(q) != std::string::npos) named.push_back(std::move(a));
  }
  auto bigger = [](const earth::AirportRecord& x, const earth::AirportRecord& y) {
    const float lx = x.runways.empty() ? 0.f : x.runways[0].length, ly = y.runways.empty() ? 0.f : y.runways[0].length;
    return x.type != y.type ? x.type > y.type : lx != ly ? lx > ly : x.ident < y.ident;
  };
  std::sort(out.begin(), out.end(), bigger);
  std::sort(named.begin(), named.end(), bigger);
  for (auto& a : named) { if (out.size() >= limit) break; out.push_back(std::move(a)); }
  if (out.size() > limit) out.resize(limit);
  return out;
}
} // namespace earthCharts
