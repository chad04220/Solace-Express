#include "procedural_regions.h"
#include "airport_layout.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace procedural {
namespace {
uint64_t mix(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
struct Random {
  uint64_t state;
  uint64_t next() { state = mix(state); return state; }
  float unit() { return float(next() >> 40) / 16777216.f; }
  float range(float a, float b) { return a + (b - a) * unit(); }
  int integer(int n) { return int(next() % uint64_t(n)); }
};
constexpr int kSide = 1 << kAddressLevel;
int tileKey(geo::Tile t) { return int(t.face) * kSide * kSide + t.y * kSide + t.x; }
bool reserved(geo::Position p) { return geo::surfaceDistance(p, geo::solaceOrigin()) < 150000.; }
const char* prefixes[] = {"Aster", "Briar", "Cairn", "Dawn", "Ember", "Gannet", "Harrier", "Indigo",
  "Juniper", "Kestrel", "Lumen", "Morrow", "Nacre", "Oriel", "Peregrine", "Quartz",
  "Rowan", "Sable", "Tern", "Umber", "Vesper", "Willow", "Yarrow", "Zephyr"};
const char* suffixes[] = {"Islands", "Volcanic Chain", "Fjords", "Atolls", "Continental Isles"};
Descriptor fromTile(geo::Tile t, uint64_t worldSeed) {
  Descriptor d;
  d.address = t;
  d.origin = geo::fromDirection(geo::tileDirection(t, .5, .5));
  if (reserved(d.origin)) return solaceDescriptor(worldSeed);
  d.worldSeed = worldSeed;
  d.seed = mix(worldSeed ^ mix(uint64_t(tileKey(t))) ^ mix(kGeneratorVersion));
  char id[40]; std::snprintf(id, sizeof(id), "pr%u-%d-%d-%d", kGeneratorVersion, int(t.face), t.x, t.y);
  d.id = id;
  const double lat = std::abs(d.origin.latitude);
  const uint64_t choice = mix(d.seed);
  if (lat > 52.) d.archetype = (choice % 3 == 0) ? Archetype::ContinentalFragments : Archetype::Fjords;
  else if (lat < 28.) {
    const Archetype tropical[] = {Archetype::Atolls, Archetype::Archipelago, Archetype::VolcanicChain, Archetype::ContinentalFragments};
    d.archetype = tropical[choice % 4];
  } else {
    const Archetype temperate[] = {Archetype::Archipelago, Archetype::VolcanicChain, Archetype::ContinentalFragments};
    d.archetype = temperate[choice % 3];
  }
  d.name = std::string(prefixes[(d.seed >> 9) % 24]) + " " + suffixes[int(d.archetype)];
  // (its own stream: the size changes nothing above - the address, the seed, the archetype and the name)
  const uint64_t size = mix(d.seed ^ UINT64_C(0x5343414c45000001)) % 100;
  d.scale = size < 30 ? Scale::Small : size < 68 ? Scale::Medium : Scale::Large;
  return d;
}
geo::Tile neighbor(geo::Tile t, int dx, int dy) {
  const auto sample = geo::sampleAddress(t, dx < 0 ? -1 : dx > 0 ? geo::kTileSamples : 128,
                                           dy < 0 ? -1 : dy > 0 ? geo::kTileSamples : 128);
  return {sample.face, kAddressLevel, sample.column / geo::kTileSamples, sample.row / geo::kTileSamples};
}
float smooth(float a, float b, float x) { return smoothstepf(a, b, x); }
float ellipseDistance(float x, float z, float rx, float rz) {
  return std::sqrt((x * x) / (rx * rx) + (z * z) / (rz * rz));
}
float cubic(float a, float b, float c, float d, float t) {
  // Periodic Catmull-Rom interpolation avoids angular corners or zero-slope
  // dimples at every profile knot. Inputs have a conservative bounded envelope.
  return b + .5f * t * (c - a + t * (2.f * a - 5.f * b + 4.f * c - d +
    t * (3.f * (b - c) + d - a)));
}
}

const char* scaleName(Scale scale) {
  switch (scale) {
    case Scale::Small: return "Small";
    case Scale::Medium: return "Medium";
    case Scale::Large: return "Large";
  }
  return "Unknown";
}
const char* archetypeName(Archetype type) {
  switch (type) {
    case Archetype::Archipelago: return "Archipelago";
    case Archetype::VolcanicChain: return "Volcanic chain";
    case Archetype::Fjords: return "Fjords";
    case Archetype::Atolls: return "Atolls";
    case Archetype::ContinentalFragments: return "Continental fragments";
  }
  return "Unknown";
}
Descriptor solaceDescriptor(uint64_t worldSeed) {
  Descriptor d;
  d.id = "solace"; d.name = "The Solace Islands"; d.origin = geo::solaceOrigin();
  d.address = geo::positionTile(d.origin, kAddressLevel).tile;
  d.worldSeed = worldSeed; d.seed = 0; d.authored = true; d.scale = Scale::Large;
  return d;
}
Descriptor describe(geo::Position p, uint64_t worldSeed) {
  const auto tile = geo::positionTile(p, kAddressLevel).tile; // validates p, including poles and antimeridian
  if (geo::surfaceDistance(p, geo::solaceOrigin()) <= 71000.) return solaceDescriptor(worldSeed);
  return fromTile(tile, worldSeed);
}
std::optional<Descriptor> find(const std::string& id, uint64_t worldSeed) {
  if (id == "solace") return solaceDescriptor(worldSeed);
  // Parsing bounded canonical components avoids integer overflow in scanf/strtol
  // on untrusted IDs, and rejects path separators and trailing text.
  if (id.size() < 9 || id.size() > 17 || id.compare(0, 4, "pr1-") != 0) return std::nullopt;
  std::array<int, 3> fields{}; size_t cursor = 4;
  for (int i = 0; i < 3; ++i) {
    size_t start = cursor;
    while (cursor < id.size() && id[cursor] >= '0' && id[cursor] <= '9') {
      if (cursor - start >= 2) return std::nullopt;
      fields[i] = fields[i] * 10 + id[cursor++] - '0';
    }
    if (cursor == start || (cursor - start > 1 && id[start] == '0')) return std::nullopt;
    if (i < 2 && (cursor == id.size() || id[cursor++] != '-')) return std::nullopt;
  }
  if (cursor != id.size() || fields[0] > 5 || fields[1] >= kSide || fields[2] >= kSide) return std::nullopt;
  Descriptor d = fromTile({geo::Face(fields[0]), kAddressLevel, fields[1], fields[2]}, worldSeed);
  if (d.id != id) return std::nullopt; // reserved addresses have no competing generated identity
  return d;
}
std::vector<Descriptor> nearby(geo::Position p, size_t count, uint64_t worldSeed) {
  const auto start = geo::positionTile(p, kAddressLevel).tile;
  count = std::min(count, kMaxNearby);
  if (!count) return {};
  // Four-connected cube neighbors use geo's exact seam ownership. A six-ring
  // search visits at most 85 tiles, even at cube corners; never all 6,144 cells.
  std::vector<geo::Tile> frontier{start};
  std::unordered_set<int> visited; visited.reserve(96); visited.insert(tileKey(start));
  std::vector<Descriptor> result{solaceDescriptor(worldSeed)}; result.reserve(96);
  for (int ring = 0; ring <= 6; ++ring) {
    std::vector<geo::Tile> next;
    for (geo::Tile tile : frontier) {
      Descriptor d = fromTile(tile, worldSeed);
      if (!d.authored) result.push_back(std::move(d));
      if (ring == 6) continue;
      for (const auto& step : {std::pair<int,int>{-1,0}, {1,0}, {0,-1}, {0,1}}) {
        const auto adjacent = neighbor(tile, step.first, step.second);
        if (visited.insert(tileKey(adjacent)).second) next.push_back(adjacent);
      }
    }
    frontier.swap(next);
  }
  std::sort(result.begin(), result.end(), [&](const Descriptor& a, const Descriptor& b) {
    const double da = geo::surfaceDistance(p, a.origin), db = geo::surfaceDistance(p, b.origin);
    return da == db ? a.id < b.id : da < db;
  });
  if (result.size() > count) result.resize(count);
  return result;
}
std::vector<Descriptor> recommendations(uint64_t worldSeed) {
  auto result = nearby(geo::solaceOrigin(), 7, worldSeed);
  // Global discovery suggestions remain descriptors only. Ensure one example of
  // every supported form without making the menu depend on terrain generation.
  const std::pair<geo::Position, Archetype> anchors[] = {
    {{10., 163., 0.}, Archetype::Atolls}, {{20., -156., 0.}, Archetype::VolcanicChain},
    {{65., 8., 0.}, Archetype::Fjords}, {{-42., 146., 0.}, Archetype::ContinentalFragments},
    {{-18., -171., 0.}, Archetype::Archipelago}};
  for (const auto& anchor : anchors) {
    for (const Descriptor& d : nearby(anchor.first, kMaxNearby, worldSeed)) {
      if (d.archetype != anchor.second || d.authored) continue;
      if (std::none_of(result.begin(), result.end(), [&](const Descriptor& a) { return a.id == d.id; })) result.push_back(d);
      break;
    }
  }
  return result;
}

Recipe::Recipe(const Descriptor& d) : descriptor(d) {
  const auto canonical = find(d.id, d.worldSeed);
  if (!canonical || canonical->authored || d.generatorVersion != kGeneratorVersion || canonical->seed != d.seed ||
      canonical->archetype != d.archetype || geo::surfaceDistance(canonical->origin, d.origin) > .001)
    throw std::invalid_argument("unsupported or noncanonical procedural region");
  descriptor = *canonical;
  Random random{d.seed};
  const float rotation = random.range(-PI, PI);
  rotationSin_ = std::sin(rotation); rotationCos_ = std::cos(rotation);
  roughPhase_ = random.range(0.f, 2.f * PI);
  const float latitude = float(std::abs(d.origin.latitude));
  cold_ = smooth(43.f, 78.f, latitude);
  lush_ = clampf(.86f - .52f * cold_ + random.range(-.12f, .10f), .2f, .96f);
  if (d.archetype == Archetype::Atolls) { lush_ = .92f; cold_ = 0.f; }
  // (the reserves bound every region: the names' storage must never move once an Airport or Town points into it)
  land_.reserve(16); channels_.reserve(10); townPads_.reserve(kMaxTowns);
  airports.reserve(kMaxAirports); towns.reserve(kMaxTowns); airportCodes_.reserve(kMaxAirports); airportNames_.reserve(kMaxAirports); townNames_.reserve(kMaxTowns);
  const Scale scale = descriptor.scale;
  auto point = [&](float x, float z) { return vec2(x * rotationCos_ - z * rotationSin_, x * rotationSin_ + z * rotationCos_); };
  auto land = [&](float x, float z, float rx, float rz, float h, int shape) {
    land_.push_back({x, z, rx, rz, h, random.range(0.f, 2.f * PI), shape});
  };
  auto field = [&](float x, float z, float heading, float elev, float len, int size, int surface, float tx, float tz, float townRadius, int townKind = -1) {
    const size_t i = airports.size();
    const vec2 a = point(x, z), t = point(tx, tz);
    // Codes are compact, unique within the active world, and independent of the
    // menu order. They are fictional identifiers rather than real ICAO claims.
    char code[8]; std::snprintf(code, sizeof(code), "%c%c%c", char('A' + (d.seed >> 12) % 26), char('A' + (d.seed >> 22) % 26), char('A' + i));
    airportCodes_.emplace_back(code);
    static const char* settlements[] = {"Harbor", "Landing", "Haven", "Point", "Cove", "Reach", "Sound", "Bay", "Strand", "Quay"};
    townNames_.push_back(std::string(prefixes[(d.seed >> 9) % 24]) + " " + settlements[i % 10]);
    airportNames_.push_back(townNames_.back() + (size >= 2 ? " International" : size > 0 ? " Regional" : " Airstrip"));
    Airport airport{airportCodes_.back().c_str(), airportNames_.back().c_str(), a.x, a.y, elev,
      wrapDeg360(heading + rotation / DEG), len, size >= 2 ? 45.f : size > 0 ? 32.f : 22.f, surface, size,
      "A seeded Earth-region field. Check runway length and terrain before arrival."};
    airport.hospital = i == 0;
    airports.push_back(airport);
    towns.push_back({townNames_.back().c_str(), t.x, t.y, townRadius, townKind >= 0 ? townKind : size > 0 ? 1 : 0});
    townPads_.push_back({t.x, t.y, townRadius + 100.f, elev + 3.f});
  };
  if (d.archetype == Archetype::Archipelago) {
    if (scale == Scale::Small) {   // two or three islands close together, a field on each
      const int n = 2 + random.integer(2);
      for (int i = 0; i < n; ++i) {
        const float angle = (2.f * PI * i) / n + random.range(-.3f, .3f);
        const float distance = random.range(7000.f, 10000.f);
        const float x = std::cos(angle) * distance, z = std::sin(angle) * distance;
        const float rx = random.range(3600.f, 4700.f), rz = random.range(3200.f, 4300.f);
        land(x, z, rx, rz, random.range(220.f, 560.f), 0);
        field(x, z + .42f * rz, 90.f, 30.f, i ? 800.f : 1400.f, i ? 0 : 1,   // (the town abeam the runway's middle)
          i % 2 ? SURF_GRASS : SURF_ASPHALT, x, z + .42f * rz + 1350.f, i ? 220.f : 330.f);
      }
      land(random.range(-2500.f, 2500.f), random.range(-2500.f, 2500.f), random.range(800.f, 1500.f),
        random.range(700.f, 1300.f), random.range(30.f, 90.f), 0);
    } else if (scale == Scale::Large) {   // a main island - the city, the international airport - in a ring of islands
      const float mrx = random.range(13000.f, 15000.f), mrz = random.range(11000.f, 13000.f);
      land(0.f, 0.f, mrx, mrz, random.range(800.f, 1300.f), 0);
      field(-.10f * mrx, .40f * mrz, 90.f, 30.f, 2800.f, 2, SURF_ASPHALT, -.10f * mrx + 900.f, .40f * mrz + 2700.f, 1200.f, 2);
      const int n = 5 + random.integer(3);
      for (int i = 0; i < n; ++i) {
        const float angle = (2.f * PI * i) / n + random.range(-.12f, .12f);
        const float distance = random.range(22500.f, 25500.f);
        const float x = std::cos(angle) * distance, z = std::sin(angle) * distance;
        const float rx = random.range(4800.f, 6500.f), rz = random.range(4400.f, 6000.f);
        land(x, z, rx, rz, random.range(300.f, 820.f), 0);
        field(x, z + .45f * rz, 90.f, 30.f, i % 2 ? 900.f : 1500.f, i % 2 ? 0 : 1,
          i % 3 ? SURF_GRASS : SURF_ASPHALT, x + 600.f, z + .45f * rz + 1700.f, i % 2 ? 280.f : 450.f);
        const float gap = angle + PI / n;   // (an islet in the gap to the next)
        land(std::cos(gap) * 19000.f, std::sin(gap) * 19000.f, random.range(900.f, 1900.f), random.range(800.f, 1600.f),
          random.range(40.f, 140.f), 0);
      }
    } else {
      const int n = 4 + random.integer(3);
      for (int i = 0; i < n; ++i) {
        const float angle = (2.f * PI * i) / n + random.range(-.13f, .13f);
        const float distance = random.range(14500.f, 20500.f);
        const float x = std::cos(angle) * distance, z = std::sin(angle) * distance;
        const float rx = random.range(5500.f, 7600.f), rz = random.range(4800.f, 6800.f);
        land(x, z, rx, rz, random.range(250.f, 720.f), 0);
        field(x, z + .45f * rz, 90.f, random.range(25.f, 65.f), i ? 850.f : 1600.f, i ? 0 : 1,
          i % 3 ? SURF_GRASS : SURF_ASPHALT, x + 600.f, z + .45f * rz + 1500.f, i ? 280.f : 500.f);
      }
      for (int i = 0; i < 3; ++i) land(random.range(-10500.f, 10500.f), random.range(-10500.f, 10500.f),
        random.range(1100.f, 2300.f), random.range(900.f, 1900.f), random.range(40.f, 130.f), 0);
    }
  } else if (d.archetype == Archetype::VolcanicChain) {
    if (scale == Scale::Small) {   // two cones, joined at their feet
      for (int i = 0; i < 2; ++i) {
        const float x = i ? 6500.f : -6500.f, z = (i ? 4200.f : -4200.f) + random.range(-1200.f, 1200.f);
        const float rx = random.range(5200.f, 6400.f), rz = random.range(5200.f, 6600.f);
        land(x, z, rx, rz, random.range(1000.f, 1900.f), 1);
        field(x + .72f * rx, z, 0.f, 90.f, i ? 900.f : 1500.f, i ? 0 : 1, i ? SURF_GRAVEL : SURF_ASPHALT,
          x + .20f * rx, z + .80f * rz, i ? 260.f : 400.f);
      }
    } else if (scale == Scale::Large) {   // five cones down a long chain; a lava plain off its middle with the city
      land(7000.f, -9500.f, 10500.f, 7000.f, random.range(160.f, 260.f), 0);
      field(7000.f, -7400.f, 90.f, 30.f, 2700.f, 2, SURF_ASPHALT, 7900.f, -4700.f, 1150.f, 2);
      for (int i = 0; i < 5; ++i) {   // (inside 31 km of the middle whichever way the region turns: the ocean edge is square)
        const float x = -22500.f + 11250.f * i, z = -14000.f + 7000.f * i + random.range(-1200.f, 1200.f);
        const bool end = i == 0 || i == 4;
        const float rx = end ? random.range(5000.f, 5800.f) : random.range(5800.f, 7000.f);
        const float rz = end ? random.range(5000.f, 5800.f) : random.range(5800.f, 7200.f);
        land(x, z, rx, rz, random.range(1400.f, 2800.f), 1);
        if (i == 2) continue;   // (the middle cone stands over the plain: no field on it)
        const bool regional = i == 1 || i == 3;
        field(x + .72f * rx, z, 0.f, 90.f, regional ? 1500.f : 1000.f, regional ? 1 : 0, regional ? SURF_ASPHALT : SURF_GRAVEL,
          x + .20f * rx, z + .80f * rz, regional ? 420.f : 280.f);
      }
    } else {
      const int n = 4;
      for (int i = 0; i < n; ++i) {
        const float x = -21500.f + 14000.f * i, z = -16500.f + 10500.f * i + random.range(-1800.f, 1800.f);
        const float rx = random.range(6100.f, 7900.f), rz = random.range(6200.f, 8100.f);
        land(x, z, rx, rz, random.range(1200.f, 2400.f), 1);
        field(x + .72f * rx, z, 0.f, 90.f + i * 20.f, i == 1 ? 1700.f : 1000.f, i == 1 ? 1 : 0,
          i == 1 ? SURF_ASPHALT : SURF_GRAVEL, x + .20f * rx, z + .80f * rz, i == 1 ? 450.f : 280.f);
      }
    }
  } else if (d.archetype == Archetype::Fjords) {
    if (scale == Scale::Small) {   // one mountain block cut by three fjords, two fields between their mouths
      bandZ_ = 6500.f;
      land(0.f, -3500.f, 18000.f, 17000.f, 1150.f, 0);
      land(-15500.f, -15000.f, 6500.f, 7500.f, 950.f, 0);
      for (int i = 0; i < 3; ++i) channels_.push_back({-12000.f + 12000.f * i, 15000.f,
        random.range(21000.f, 26000.f), random.range(850.f, 1300.f), random.range(-1200.f, 1200.f)});
      for (int i = 0; i < 2; ++i) {
        const float x = -6000.f + 12000.f * i, z = 6000.f + 1500.f * i;
        field(x, z, 0.f, 60.f, i ? 1000.f : 1500.f, i ? 0 : 1, i ? SURF_GRAVEL : SURF_ASPHALT, x + 1750.f, z - 450.f, i ? 300.f : 500.f);
      }
    } else if (scale == Scale::Large) {   // the medium's coast, higher, with a city, and a strip up on the high plateau
      land(0.f, -6500.f, 31000.f, 31000.f, 1500.f, 0);
      land(-25000.f, -20000.f, 9000.f, 12000.f, 1250.f, 0);
      land(22000.f, -21000.f, 11500.f, 11000.f, 1900.f, 0);
      for (int i = 0; i < 5; ++i) channels_.push_back({-24000.f + 12000.f * i, 28000.f,
        random.range(36500.f, 46500.f), random.range(1000.f, 1650.f), random.range(-1600.f, 1600.f)});
      for (int i = 0; i < 4; ++i) {
        const float x = -18000.f + 12000.f * i, z = 9500.f + (i % 2) * 2200.f;
        if (i == 0) field(x, z, 0.f, 65.f, 2600.f, 2, SURF_ASPHALT, x + 2300.f, z - 450.f, 1050.f, 2);
        else field(x, z, 0.f, 65.f, 1150.f, 0, i % 2 ? SURF_GRAVEL : SURF_ASPHALT, x + 1750.f, z - 450.f, 350.f);
      }
      field(0.f, -21000.f, 90.f, 900.f, 1300.f, 0, SURF_GRAVEL, 0.f, -19200.f, 260.f);
    } else {
      land(0.f, -6500.f, 30500.f, 30500.f, 1450.f, 0);
      land(-25000.f, -20000.f, 9000.f, 12000.f, 1200.f, 0);
      land(22000.f, -21000.f, 11500.f, 11000.f, 1800.f, 0);
      for (int i = 0; i < 5; ++i) channels_.push_back({-24000.f + 12000.f * i, 28000.f,
        random.range(36500.f, 46500.f), random.range(1000.f, 1650.f), random.range(-1600.f, 1600.f)});
      for (int i = 0; i < 4; ++i) {
        const float x = -18000.f + 12000.f * i, z = 9500.f + (i % 2) * 2200.f;
        field(x, z, 0.f, 65.f + i * 12.f, i ? 1150.f : 1750.f, i ? 0 : 1,
          i ? SURF_GRAVEL : SURF_ASPHALT, x + 1750.f, z - 450.f, i ? 350.f : 650.f);
      }
    }
  } else if (d.archetype == Archetype::Atolls) {
    auto atollField = [&](float cx, float cz, float rx, float rz, bool main) {
      field(cx + rx, cz, 0.f, 7.f, main ? 1050.f : 700.f, 0, main ? SURF_ASPHALT : SURF_SAND,
        cx + rx * std::cos(1.05f), cz + rz * std::sin(1.05f), 170.f);
    };
    if (scale == Scale::Small) {   // two atolls
      const float centers[][2] = {{-9000.f, -6500.f}, {9500.f, 7000.f}};
      for (int i = 0; i < 2; ++i) {
        const float rx = random.range(5000.f, 6500.f), rz = random.range(4600.f, 6000.f);
        land(centers[i][0], centers[i][1], rx, rz, random.range(12.f, 23.f), 2);
        atollField(centers[i][0], centers[i][1], rx, rz, i == 0);
      }
    } else if (scale == Scale::Large) {   // a great atoll in the middle and six round it
      const float rx = random.range(7600.f, 8800.f), rz = random.range(7000.f, 8200.f);
      land(0.f, 0.f, rx, rz, random.range(14.f, 24.f), 2);
      field(rx, 0.f, 0.f, 7.f, 1500.f, 1, SURF_ASPHALT, rx * std::cos(1.05f), rz * std::sin(1.05f), 260.f);
      for (int i = 0; i < 6; ++i) {
        const float angle = i * PI / 3.f + random.range(-.10f, .10f);
        const float cx = std::cos(angle) * 23000.f, cz = std::sin(angle) * 23000.f;
        const float ax = random.range(4800.f, 6200.f), az = random.range(4400.f, 5800.f);
        land(cx, cz, ax, az, random.range(12.f, 23.f), 2);
        atollField(cx, cz, ax, az, false);
      }
    } else {
      const float centers[][2] = {{-18000.f,-15000.f}, {14000.f,-16000.f}, {-13000.f,16000.f}, {19000.f,15000.f}};
      for (int i = 0; i < 4; ++i) {
        const float rx = random.range(5500.f, 7300.f), rz = random.range(5000.f, 6700.f);
        land(centers[i][0], centers[i][1], rx, rz, random.range(12.f, 23.f), 2);
        field(centers[i][0] + rx, centers[i][1], 0.f, 7.f, i ? 700.f : 1050.f, 0,
          i ? SURF_SAND : SURF_ASPHALT, centers[i][0] + rx * std::cos(1.05f), centers[i][1] + rz * std::sin(1.05f), 170.f);
      }
    }
  } else {
    if (scale == Scale::Small) {   // one long island, a field at each end
      const float rx = random.range(9500.f, 12000.f), rz = random.range(14000.f, 17000.f);
      land(0.f, 0.f, rx, rz, random.range(450.f, 750.f), 0);
      field(-.25f * rx, .40f * rz, 0.f, 40.f, 1600.f, 1, SURF_ASPHALT, -.25f * rx + 1600.f, .40f * rz + 300.f, 520.f);
      field(.20f * rx, -.45f * rz, 90.f, 40.f, 900.f, 0, SURF_GRASS, .20f * rx, -.45f * rz - 1350.f, 280.f);
    } else if (scale == Scale::Large) {   // the medium's fragments, larger, with a city and two more fields
      land(-16500.f, -2000.f, 17000.f, 29500.f, 650.f, 0);
      land(15000.f, 8000.f, 17000.f, 23500.f, 900.f, 0);
      land(17500.f, -20000.f, 12000.f, 13000.f, 1500.f, 0);
      channels_.push_back({0.f, 34000.f, 71000.f, 1700.f, 2800.f});
      field(-17000.f, 14000.f, 0.f, 80.f, 2800.f, 2, SURF_ASPHALT, -19400.f, 13500.f, 1250.f, 2);
      field(-15000.f, -15500.f, 90.f, 160.f, 1100.f, 0, SURF_GRASS, -15000.f, -17300.f, 350.f);
      field(15500.f, 15000.f, 90.f, 95.f, 1700.f, 1, SURF_ASPHALT, 15500.f, 17500.f, 600.f);
      field(18500.f, -20000.f, 0.f, 210.f, 900.f, 0, SURF_GRAVEL, 20500.f, -20000.f, 300.f);
      field(-21000.f, 1500.f, 90.f, 60.f, 1300.f, 1, SURF_ASPHALT, -21000.f, 3200.f, 450.f);
      field(13000.f, -1500.f, 0.f, 60.f, 1200.f, 1, SURF_ASPHALT, 14800.f, -1500.f, 420.f);
    } else {
      land(-16000.f, -2000.f, 16200.f, 28000.f, 570.f, 0);
      land(14500.f, 8000.f, 16000.f, 22700.f, 800.f, 0);
      land(17000.f, -20000.f, 11500.f, 12600.f, 1400.f, 0);
      channels_.push_back({0.f, 34000.f, 71000.f, 1600.f, 2800.f});
      field(-17000.f, 14000.f, 0.f, 80.f, 2000.f, 1, SURF_ASPHALT, -19000.f, 14000.f, 750.f);
      field(-15000.f, -15500.f, 90.f, 160.f, 1100.f, 0, SURF_GRASS, -15000.f, -17300.f, 350.f);
      field(15500.f, 15000.f, 90.f, 95.f, 1700.f, 1, SURF_ASPHALT, 15500.f, 17500.f, 600.f);
      field(18500.f, -20000.f, 0.f, 210.f, 900.f, 0, SURF_GRAVEL, 20500.f, -20000.f, 300.f);
      }
  }
  // Shape variation owns a separate random stream. The original layout above,
  // including every airport/town coordinate and text, retains its exact draws.
  Random shapeRandom{mix(d.seed ^ UINT64_C(0x434f41535452494d))};
  for (Landmass& island : land_) {
    std::vector<float> routeBearings;
    if (island.shape != 2) {
      routeBearings.reserve(airports.size() * 5);
      for (size_t j = 0; j < airports.size(); ++j) for (int k = 0; k <= 4; ++k) {
        const float x = lerpf(airports[j].x, towns[j].x, .25f * k);
        const float z = lerpf(airports[j].z, towns[j].z, .25f * k);
        const float dx = (x * rotationCos_ + z * rotationSin_ - island.x) / island.rx;
        const float dz = (-x * rotationSin_ + z * rotationCos_ - island.z) / island.rz;
        const float r = std::hypot(dx, dz);
        if (r > .68f && r < 1.20f) routeBearings.push_back(std::atan2(dz, dx));
      }
    }
    const float p1 = shapeRandom.range(-PI, PI), p2 = shapeRandom.range(-PI, PI);
    const float p3 = shapeRandom.range(-PI, PI), p4 = shapeRandom.range(-PI, PI);
    for (int i = 0; i < kCoastSamples; ++i) {
      const float angle = -PI + 2.f * PI * i / kCoastSamples;
      const float broad = std::sin(angle + p1), bays = std::sin(2.f * angle + p2);
      const float coves = std::sin(5.f * angle + p3), small = std::sin(11.f * angle + p4);
      const float width = clampf(.5f + .32f * bays + .18f * coves, 0.f, 1.f);
      CoastSample& s = island.coast[i];
      if (island.shape == 2) {
        // Keep the inhabited eastern arc continuous: runway-to-town roads can
        // follow the reef, while uninhabited arcs become spits and wider motu.
        const float free = smooth(.90f, 1.25f, std::abs(wrapAngle(angle - .52f)));
        const float oldRadius = 1.f + .018f * std::sin(5.f * angle + island.phase) * smooth(.15f, .6f, std::abs(angle));
        s.radius = lerpf(oldRadius, 1.f + .110f * broad + .065f * bays + .025f * coves, free);
        const float inner = .012f + .120f * width * width * width;
        const float outer = .065f + .200f * width * width;
        s.inner = lerpf(std::max(.048f, inner), inner, free);
        s.outer = lerpf(std::max(.145f, outer), outer, free);
        // A few narrow, uninhabited reef sections remain submerged between
        // individual motu rather than making every lagoon a continuous ring.
        const float breaks = (1.f - smooth(.15f, .50f, width)) * smooth(.15f, .65f, -std::sin(8.f * angle + p4));
        s.elevation = lerpf(1.f, .70f + .45f * width - 1.25f * breaks, free);
      } else {
        s.radius = 1.f + .060f * broad + .090f * bays + .045f * coves + .015f * small;
        s.inner = .82f + .065f * width;
        s.outer = 1.055f + .070f * width;
        s.elevation = 1.f;
        // Pads alone cannot preserve a dry route if its surrounding coast is
        // eroded away. Retain the original shore across the existing coastal
        // access corridors, blending gently back to varied uninhabited coast.
        float protect = 0.f;
        for (float bearing : routeBearings)
          protect = std::max(protect, 1.f - smooth(.06f, .18f, std::abs(wrapAngle(angle - bearing))));
        const float oldCoast = 1.f + .085f * std::sin(3.f * angle + island.phase) + .045f * std::sin(7.f * angle - island.phase);
        s.radius = lerpf(s.radius, oldCoast, protect);
        s.inner = lerpf(s.inner, .83f, protect); s.outer = lerpf(s.outer, 1.11f, protect);
      }
    }
    if (island.shape == 2) {
      const int count = 1 + shapeRandom.integer(3);
      for (int i = 0; i < count; ++i) {
        const float angle = count == 1 ? shapeRandom.range(2.f, 4.95f) :
          2.05f + 2.8f * i / (count - 1) + shapeRandom.range(-.16f, .16f);
        island.gaps[i] = {wrapAngle(angle), shapeRandom.range(.11f, .25f)};
      }
    }
  }
  const size_t trunks = channels_.size();
  const float fjordK = scale == Scale::Small ? .62f : 1.f;   // (a small region's fjords: shorter, their mouths closer)
  for (size_t i = 0; i < trunks; ++i) {
    Channel& c = channels_[i];
    c.originalLength = c.length;
    c.turn = shapeRandom.range(-5200.f, 5200.f);
    c.sway = shapeRandom.range(-4000.f, 4000.f);
    c.widthVariation = shapeRandom.range(-.24f, .24f);
    if (d.archetype == Archetype::Fjords) {
      const float mouths[] = {3500.f, -6500.f, 5200.f, -6500.f, -1800.f};
      const float lengths[] = {34000.f, 49000.f, 38000.f, 45000.f, 33000.f};
      c.mouth = (mouths[i] + shapeRandom.range(-1300.f, 1300.f)) * fjordK;
      c.mouthWidth = shapeRandom.range(1.35f, 2.30f);
      c.length = (lengths[i] + shapeRandom.range(0.f, 6000.f)) * fjordK;
    }
  }
  if (d.archetype == Archetype::Fjords) {
    // Tributaries join trunks well inland of all protected settlements. Their
    // mouth is exactly on the trunk centerline, so no disconnected water cuts.
    for (size_t i = 1; i < trunks - 1; ++i) {
      const Channel parent = channels_[i];
      const float along = std::max(31000.f * fjordK, shapeRandom.range(.67f, .79f) * parent.length);
      const float t = along / parent.length, oldT = clampf(along / parent.originalLength, 0.f, 1.f);
      const float z = parent.z - along;
      const float free = smooth(3000.f, 8500.f, std::abs(z - bandZ_));
      const float offset = parent.bend * std::sin(oldT * 4.2f + roughPhase_) +
        free * (parent.turn * t + parent.sway * 4.f * t * (1.f - t) + parent.mouth * (1.f - t) * (1.f - t) * (1.f - t));
      Channel branch{parent.x + offset, z, shapeRandom.range(9000.f, 15000.f) * fjordK,
        parent.width * shapeRandom.range(.55f, .85f), 0.f};
      const float angle = (i % 2 ? -1.f : 1.f) * shapeRandom.range(.65f, 1.f);
      branch.axisX = std::sin(angle); branch.axisZ = -std::cos(angle);
      branch.sway = shapeRandom.range(-1300.f, 1300.f);
      branch.widthVariation = shapeRandom.range(-.20f, .20f); branch.branch = true;
      channels_.push_back(branch);
    }
  }
  // Every field at the height of its own ground (the mean along its runway, never below 6 m) and every town's pad at
  // its own: the land is levelled for them where they are, not cut down to a fixed height round them
  for (Airport& a : airports) {
    const vec3 dir = a.dir();
    float sum = 0.f;
    for (int k = -4; k <= 4; ++k) {
      const float t = k / 4.f * a.length * .5f;
      sum += std::max(natural(a.x + dir.x * t, a.z + dir.z * t), 6.f);
    }
    a.elev = std::round(sum / 9.f);
  }
  for (TownPad& pad : townPads_) pad.elevation = std::max(natural(pad.x, pad.z), 4.f) + 1.f;
  // Villages and towns away from the fields - two on a small region, four on a medium, ten on a large - on dry, gentle
  // ground inland of the shore, outside every field's grounds and approaches, and well apart (their own stream)
  Random townRandom{mix(d.seed ^ UINT64_C(0x56494c4c41474553))};
  static const char* places[] = {"Ridge", "Vale", "Ford", "Hollow", "Mill", "Cross", "Falls", "Bluff",
                                 "Springs", "Wick", "Field", "Mere", "Down", "Glen", "Rise", "Brook"};
  const int villages = scale == Scale::Small ? 2 : scale == Scale::Medium ? 4 : 10;
  const int market = scale == Scale::Large ? 3 : scale == Scale::Medium ? 1 : 0;   // (the first few are towns)
  const float highest = cold_ > .5f ? 260.f : 480.f;
  const float gentle = d.archetype == Archetype::VolcanicChain ? 60.f : 35.f;   // (a cone's lower flanks are its only room)
  for (int attempt = 0, placed = 0; attempt < 1500 && placed < villages && towns.size() < kMaxTowns; ++attempt) {
    const bool town = placed < market;
    const float radius = town ? townRandom.range(520.f, 760.f) : townRandom.range(200.f, 380.f);
    const float x = townRandom.range(-31000.f, 31000.f), z = townRandom.range(-31000.f, 31000.f);
    if (std::max(std::abs(x), std::abs(z)) > 31500.f - radius) continue;
    const float h = natural(x, z);
    if (h < 14.f || h > highest) continue;
    bool ok = true;
    for (int k = 0; k < 8 && ok; ++k) {   // gentle: near its centre's height all over it
      const float angle = k * PI / 4.f, edge = natural(x + std::cos(angle) * radius, z + std::sin(angle) * radius);
      ok = edge > 6.f && std::abs(edge - h) < gentle;
    }
    for (int k = 0; k < 12 && ok; ++k) {   // inland: dry all round, past the edge of its levelled ground
      const float angle = k * PI / 6.f, reach = radius + 650.f;
      ok = natural(x + std::cos(angle) * reach, z + std::sin(angle) * reach) > 3.f;
    }
    for (size_t i = 0; i < airports.size() && ok; ++i) {   // off the grounds, out of the approaches
      const Airport& a = airports[i];
      const vec3 dir = a.dir();
      const float dx = x - a.x, dz = z - a.z;
      const float u = std::abs(dx * dir.x + dz * dir.z), v = std::abs(-dx * dir.z + dz * dir.x), beyond = u - a.length * .5f;
      if (beyond > 0.f && beyond < 7800.f && v < 250.f + .18f * beyond + 1100.f + radius) ok = false;
      const float du = std::max(0.f, u - (a.length * .5f + 260.f)), dv = std::max(0.f, v - (a.size == 2 ? 420.f : 240.f));
      if (std::hypot(du, dv) < 900.f + radius) ok = false;
    }
    for (size_t i = 0; i < towns.size() && ok; ++i) ok = std::hypot(x - towns[i].x, z - towns[i].z) > towns[i].r + radius + 2200.f;
    if (!ok) continue;
    townNames_.push_back(std::string(prefixes[(d.seed >> 9) % 24]) + " " + places[(placed + (d.seed >> 17)) % 16]);
    towns.push_back({townNames_.back().c_str(), x, z, radius, town ? 1 : 0});
    townPads_.push_back({x, z, radius + 100.f, h + 1.f});
    ++placed;
  }
}

float Recipe::shapeHeight(float px, float pz) const {
  float height = -SEA_DEPTH;
  for (const Landmass& island : land_) {
    const float dx = px - island.x, dz = pz - island.z;
    if (std::abs(dx) > 1.50f * island.rx || std::abs(dz) > 1.50f * island.rz) continue;
    const float r = ellipseDistance(dx, dz, island.rx, island.rz);
    const float angle = std::atan2(dz / island.rz, dx / island.rx);
    const float position = (angle + PI) * (kCoastSamples / (2.f * PI));
    const int index = int(position); const float fraction = position - index;
    const CoastSample& a = island.coast[(index + kCoastSamples - 1) % kCoastSamples];
    const CoastSample& b = island.coast[index % kCoastSamples];
    const CoastSample& c = island.coast[(index + 1) % kCoastSamples];
    const CoastSample& e = island.coast[(index + 2) % kCoastSamples];
    const CoastSample coast{cubic(a.radius, b.radius, c.radius, e.radius, fraction),
      cubic(a.inner, b.inner, c.inner, e.inner, fraction), cubic(a.outer, b.outer, c.outer, e.outer, fraction),
      cubic(a.elevation, b.elevation, c.elevation, e.elevation, fraction)};
    float h;
    if (island.shape == 2) {
      const float reef = 1.f - smooth(coast.inner, coast.outer, std::abs(r - coast.radius));
      float cover = 1.f;
      for (const ReefGap& gap : island.gaps) if (gap.halfWidth > 0.f)
        cover *= smooth(.45f * gap.halfWidth, gap.halfWidth, std::abs(wrapAngle(angle - gap.angle)));
      h = lerpf(-SEA_DEPTH, island.height * coast.elevation, reef * cover);
    } else {
      const float shore = r / coast.radius;
      const float landWeight = 1.f - smooth(coast.inner, coast.outer, shore);
      if (island.shape == 1) {
        const float cone = std::max(0.f, 1.f - r);
        // Lower central caldera encircled by the summit rim; bounded dry crater
        // survives the existing inland-pit treatment without creating a lake.
        const float crater = 1.f - .48f * (1.f - smooth(.05f, .16f, r));
        h = lerpf(-SEA_DEPTH, 30.f + island.height * cone * cone * crater, landWeight);
      } else {
        const float ridge = .74f + .18f * std::sin(dx / 1700.f + island.phase) * std::sin(dz / 2300.f - island.phase);
        const float hill = std::max(0.f, 1.f - shore * shore);
        h = lerpf(-SEA_DEPTH, 20.f + island.height * hill * hill * ridge, landWeight);
      }
    }
    height = std::max(height, h);
  }
  for (const Channel& channel : channels_) {
    const float dx = px - channel.x, dz = pz - channel.z;
    const float along = dx * channel.axisX + dz * channel.axisZ;
    // Trunk mouths continue all the way to open ocean. An artificial start
    // plane would cut straight harbour-wall shapes into a perturbed headland.
    if ((channel.branch && along < -1500.f) || along > channel.length + 2500.f) continue;
    const float t = clampf(along / channel.length, 0.f, 1.f);
    const float oldT = channel.branch ? t : clampf(along / channel.originalLength, 0.f, 1.f);
    // Keep the old trunk cross-section across the inhabited band. Stronger
    // bends begin beyond it; the usual final grading still owns flight safety.
    const float free = channel.branch ? 1.f : smooth(3000.f, 8500.f, std::abs(pz - bandZ_));
    const float remaining = 1.f - t;
    const float center = (channel.branch ? 0.f : channel.bend * std::sin(oldT * 4.2f + roughPhase_)) +
      free * (channel.turn * t + channel.sway * 4.f * t * remaining + channel.mouth * remaining * remaining * remaining);
    const float across = -dx * channel.axisZ + dz * channel.axisX - center;
    const float width = channel.width * lerpf(1.6f, .45f, lerpf(oldT, t, free)) *
      (1.f + free * (channel.widthVariation * (4.f * t * remaining + (across < 0.f ? -.35f : .35f)) +
        (channel.mouthWidth - 1.f) * remaining * remaining * remaining * remaining));
    const float cap = 1.f - smooth(channel.length - 2000.f, channel.length + 1500.f, along);
    const float bank = channel.branch ? 1100.f : lerpf(2100.f, 1500.f + 900.f * t, free);
    const float mouthCap = channel.branch ? smooth(-1500.f, 0.f, along) : 1.f;
    const float cut = (1.f - smooth(width, width + bank, std::abs(across))) * cap * mouthCap;
    height = lerpf(height, -SEA_DEPTH, cut);
  }
  return height;
}
float Recipe::natural(float x, float z) const {
  const float edge = std::max(std::abs(x), std::abs(z));
  if (edge >= kOceanEdge || !std::isfinite(x) || !std::isfinite(z)) return -SEA_DEPTH;
  const float h = shapeHeight(x * rotationCos_ + z * rotationSin_, -x * rotationSin_ + z * rotationCos_);
  return lerpf(-SEA_DEPTH, h, 1.f - smooth(34000.f, kOceanEdge, edge));
}
void Recipe::sample(float x, float z, float out[4]) const {
  // The entire outer 2 km is exactly ocean, before the existing 10 km wrapper.
  // Return early to keep remote water cheap, and to preserve the shared boundary.
  const float edge = std::max(std::abs(x), std::abs(z));
  if (edge >= kOceanEdge || !std::isfinite(x) || !std::isfinite(z)) {
    out[0] = -SEA_DEPTH; out[1] = 0.f; out[2] = lush_; out[3] = cold_; return;
  }
  const float px = x * rotationCos_ + z * rotationSin_, pz = -x * rotationSin_ + z * rotationCos_;
  float height = shapeHeight(px, pz);
  float amplitude = descriptor.archetype == Archetype::Atolls ? .65f :
    clampf(5.f + std::max(0.f, height) * .032f, 5.f, 65.f);
  amplitude *= smooth(-15.f, 45.f, height);
  // Towns are separately graded clearings, not airport-centred city masks.
  for (const TownPad& town : townPads_) {
    const float distance = std::hypot(x - town.x, z - town.z);
    const float weight = 1.f - smooth(town.radius, town.radius + 550.f, distance);
    height = lerpf(height, town.elevation, weight); amplitude *= 1.f - weight;
  }
  for (const Airport& airport : airports) {
    const vec2 uv = aptLocal(airport, {x, 0.f, z});
    const float u = std::abs(uv.x), v = std::abs(uv.y);
    const float halfLength = airport.length * .5f + 280.f;
    const float halfWidth = airport.size >= 2 ? 520.f : airport.size > 0 ? 370.f : 260.f;   // (an international's wider apron)
    const float padDistance = std::hypot(std::max(0.f, u - halfLength), std::max(0.f, v - halfWidth));
    const float pad = 1.f - smooth(0.f, descriptor.archetype == Archetype::Atolls ? 210.f : 650.f, padDistance);
    height = lerpf(height, airport.elev, pad); amplitude *= 1.f - pad;
    const float beyond = u - airport.length * .5f;
    if (beyond > 0.f && beyond < 7500.f) {
      const float halfFunnel = 300.f + .20f * beyond;
      const float weight = (1.f - smooth(halfFunnel, halfFunnel + 650.f, v)) *
                           (1.f - smooth(6000.f, 7500.f, beyond));
      const float ceiling = airport.elev + 8.f + .030f * beyond;
      // Reserve detail's full conservative +/-2 envelope below the obstacle
      // clearance surface; flattening cannot hide a hill in the detail layer.
      const float target = std::min(height, ceiling - 2.f * amplitude - 2.f);
      height = lerpf(height, target, weight);
      amplitude *= 1.f - .9f * weight;
    }
  }
  const float ocean = 1.f - smooth(34000.f, kOceanEdge, edge);
  out[0] = lerpf(-SEA_DEPTH, height, ocean);
  out[1] = amplitude * ocean;
  out[2] = lush_; out[3] = cold_;
}
size_t Recipe::memoryBytes() const {
  size_t bytes = sizeof(*this) + land_.capacity() * sizeof(Landmass) + channels_.capacity() * sizeof(Channel) +
    townPads_.capacity() * sizeof(TownPad) + airports.capacity() * sizeof(Airport) + towns.capacity() * sizeof(Town);
  for (const auto* names : {&airportCodes_, &airportNames_, &townNames_}) {
    bytes += names->capacity() * sizeof(std::string);
    for (const auto& name : *names) bytes += name.capacity() + 1;
  }
  return bytes + descriptor.id.capacity() + descriptor.name.capacity() + 2;
}
} // namespace procedural
