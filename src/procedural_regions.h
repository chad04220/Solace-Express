// Seeded, analytic island regions at stable Earth addresses. These are invented
// landscapes on the real globe, not an elevation/raster model of real coastlines.
#pragma once
#include "geo.h"
#include "world.h"
#include <array>
#include <memory>
#include <optional>

namespace procedural {
constexpr uint32_t kGeneratorVersion = 1;
// Landscape-content/cache revision, deliberately separate from the stable
// catalog/address version above. Never mix this into IDs or layout seeds.
constexpr uint32_t kRecipeRevision = 3;   // (3: three sizes, fields and towns on their own ground, villages)
constexpr uint64_t kDefaultWorldSeed = 0x534f4c4143452026ULL;
constexpr int kAddressLevel = 5;
constexpr size_t kMaxNearby = 24;
constexpr float kOceanEdge = 38000.f;
constexpr size_t kMaxAirports = 10, kMaxTowns = 24;   // (a large region: its fields' towns and the villages besides)

enum class Archetype { Archipelago, VolcanicChain, Fjords, Atolls, ContinentalFragments };
const char* archetypeName(Archetype type);
// How much land a region holds: a few islands and two or three fields, an archipelago, or islands on Solace's own scale
// with a city and an international airport. From the seed, apart from the address, the archetype and the name.
enum class Scale { Small, Medium, Large };
const char* scaleName(Scale scale);

struct Descriptor {
  std::string id, name;
  geo::Position origin;
  uint64_t worldSeed = kDefaultWorldSeed, seed = 0;
  uint32_t generatorVersion = kGeneratorVersion;
  geo::Tile address{geo::Face::PX, kAddressLevel, 0, 0};
  Archetype archetype = Archetype::Archipelago;
  Scale scale = Scale::Medium;
  bool authored = false;
};

Descriptor solaceDescriptor(uint64_t worldSeed = kDefaultWorldSeed);
// Cube address containing p. Reserved cells intersecting Solace's safety buffer
// resolve to the original authored region. Height never affects the address.
Descriptor describe(geo::Position p, uint64_t worldSeed = kDefaultWorldSeed);
// Strict, canonical IDs only; unknown versions/addresses return no descriptor.
std::optional<Descriptor> find(const std::string& id, uint64_t worldSeed = kDefaultWorldSeed);
// Closest descriptors in a bounded six-hop neighborhood, sorted by great-circle
// distance with ID ties. At most kMaxNearby results; no heightmaps or global list.
std::vector<Descriptor> nearby(geo::Position p, size_t count = 12, uint64_t worldSeed = kDefaultWorldSeed);
std::vector<Descriptor> recommendations(uint64_t worldSeed = kDefaultWorldSeed);
// A region's airports, and its main one (the first of its largest), by id wherever g_world is: the islands' from
// their table, a generated region's from its recipe (made once, the answer kept). 0 and -1 for an unknown id
int airportCount(const std::string& id, uint64_t worldSeed = kDefaultWorldSeed);
int mainAirport(const std::string& id, uint64_t worldSeed = kDefaultWorldSeed);
// ... and one of them by index (nullptr for an unknown id or index): it stays where it is for the process, so a long
// haul's destination can be named and flown to before its region is reached
const Airport* airportOf(const std::string& id, int index, uint64_t worldSeed = kDefaultWorldSeed);

class Recipe {
public:
  explicit Recipe(const Descriptor& descriptor);
  Recipe(const Recipe&) = delete;
  Recipe& operator=(const Recipe&) = delete;
  Recipe(Recipe&&) = delete;
  Recipe& operator=(Recipe&&) = delete;
  Descriptor descriptor;
  std::vector<Airport> airports;
  std::vector<Town> towns;
  // RGBA: base height, procedural detail amplitude, lushness, coldness. Pure,
  // thread safe and allocation free. Exact sea floor at/outside the ocean edge.
  void sample(float x, float z, float out[4]) const;
  // The land alone at x, z (m, the region's chart): the islands and the channels, without the fields' and the towns'
  // levelled ground or the approaches cut below their funnels
  float natural(float x, float z) const;
  // A sight worth the flight: a volcano's crater, a summit, an atoll's lagoon, a fjord's head, the strait. Its key is
  // its own within the region (crater0, summit1...); x, z on the region's chart, y the ground to see it from, radius
  // how far it spreads (m). Worked out from the layout when asked, never kept: the recipe's memory stays as it is
  struct Feature { std::string key, name; float x, y, z, radius; };
  std::vector<Feature> features() const;
  // The region's own name for its places: "Umber" for the Umber Islands
  const char* prefix() const;
  size_t memoryBytes() const;
private:
  static constexpr int kCoastSamples = 64;
  struct CoastSample { float radius, inner, outer, elevation; };
  struct ReefGap { float angle = 0, halfWidth = 0; };
  struct Landmass {
    float x, z, rx, rz, height, phase;
    int shape; // 0 rolling island, 1 caldera, 2 reef ring
    std::array<CoastSample, kCoastSamples> coast{};
    std::array<ReefGap, 3> gaps{};
  };
  struct Channel {
    float x, z, length, width, bend;
    float turn = 0, sway = 0, widthVariation = 0;
    float mouth = 0, mouthWidth = 1, originalLength = 0;
    float axisX = 0, axisZ = -1;
    bool branch = false;
  };
  struct TownPad { float x, z, radius, elevation; };
  std::vector<Landmass> land_;
  std::vector<Channel> channels_;
  std::vector<TownPad> townPads_;
  // Reserve all string storage before binding legacy Airport/Town char pointers.
  std::vector<std::string> airportCodes_, airportNames_, townNames_;
  float rotationSin_ = 0, rotationCos_ = 1, lush_ = .6f, cold_ = 0;
  float roughPhase_ = 0;
  float bandZ_ = 10500.f;   // (fjords: the inhabited band across the channels, where they keep their straight cross-section)
  float shapeHeight(float px, float pz) const;   // (natural, in the layout's own frame)
};
} // namespace procedural
