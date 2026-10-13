// Seeded, analytic island regions at stable Earth addresses. These are invented
// landscapes on the real globe, not an elevation/raster model of real coastlines.
#pragma once
#include "geo.h"
#include "world.h"
#include <memory>
#include <optional>

namespace procedural {
constexpr uint32_t kGeneratorVersion = 1;
constexpr uint64_t kDefaultWorldSeed = 0x534f4c4143452026ULL;
constexpr int kAddressLevel = 5;
constexpr size_t kMaxNearby = 24;
constexpr float kOceanEdge = 38000.f;

enum class Archetype { Archipelago, VolcanicChain, Fjords, Atolls, ContinentalFragments };
const char* archetypeName(Archetype type);

struct Descriptor {
  std::string id, name;
  geo::Position origin;
  uint64_t worldSeed = kDefaultWorldSeed, seed = 0;
  uint32_t generatorVersion = kGeneratorVersion;
  geo::Tile address{geo::Face::PX, kAddressLevel, 0, 0};
  Archetype archetype = Archetype::Archipelago;
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
  size_t memoryBytes() const;
private:
  struct Landmass {
    float x, z, rx, rz, height, phase;
    int shape; // 0 rolling island, 1 caldera, 2 reef ring
  };
  struct Channel { float x, z, length, width, bend; };
  struct TownPad { float x, z, radius, elevation; };
  std::vector<Landmass> land_;
  std::vector<Channel> channels_;
  std::vector<TownPad> townPads_;
  // Reserve all string storage before binding legacy Airport/Town char pointers.
  std::vector<std::string> airportCodes_, airportNames_, townNames_;
  float rotationSin_ = 0, rotationCos_ = 1, lush_ = .6f, cold_ = 0;
  float roughPhase_ = 0;
};
} // namespace procedural
