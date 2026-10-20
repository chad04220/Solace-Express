// Somnium Volandi - charts on the real Earth (docs/REAL_EARTH_PLAN.md section 3): where they lie, what they are
// called, the Earth pack they are built from, and what a chart built from it holds besides the World's own arrays.
// A real-Earth chart is a procedural::Descriptor with an "earth:" id, so Free Flight, the loading screen and the far
// terrain take it as they take a generated region; World::buildEarth builds it.
#pragma once
#include "earth_data.h"
#include "procedural_regions.h"

namespace earthCharts {
constexpr int kLattice = 32;                  // a chart's origin snaps to 1/32 of a degree each way
constexpr uint32_t kGeneratorVersion = 1;     // the real Earth's charts' content (their cache, never an id)
constexpr const char* kIdPrefix = "earth:";

// The Earth pack the game reads: EARTH_PACK when set, else "earth" beside the game's working folder (opened when
// first asked for; nullptr, with packError(), when there is none)
std::shared_ptr<const earth::Pack> pack();
std::string packError();
void usePack(std::shared_ptr<const earth::Pack> pack);   // (the tests and tools: a pack of their own)

bool isEarthId(const std::string& id);
// How lush the ground grows for a land cover class at a latitude (World::hm's third channel, 0 bare .. 1 rainforest)
float lushOf(int cover, double lat);
// The chart whose origin is the lattice point nearest (lat, lon), named for the largest place near it
procedural::Descriptor chartAt(double lat, double lon);
std::optional<procedural::Descriptor> find(const std::string& id);
// The chart to start from at an airport (its runway's centre on the lattice point nearest it)
std::optional<procedural::Descriptor> chartForAirport(const std::string& ident);
// Airports by ident (exact, any case) and then by name or town (containing the query, any case), largest first;
// with nothing asked, the largest of all
std::vector<earth::AirportRecord> searchAirports(const std::string& query, size_t limit = 20);

// What a real-Earth chart's World owns besides its arrays: the pack, the airports' and towns' names (the Airport and
// Town records point into them), the real records behind its airports, and the land cover per heightfield texel
struct ChartData {
  procedural::Descriptor descriptor;
  std::shared_ptr<const earth::Pack> pack;
  std::vector<std::string> codes, names, blurbs, towns;
  std::vector<earth::AirportRecord> airports;   // World::airports' records, by index
  std::vector<uint8_t> cover;                   // per HM_N^2 texel (World::buildEarth: the mask's farmland and forest)
  int fineTiles = 0;                            // GLO-90 tiles it was built with
};
// The land cover as the mask's two fields, per mask texel (World::bakeMask): how wooded (0 open .. 1 closed forest)
// and how farmed (cropland 1, grassland a little), blurred across the cover's 330 m cells
void maskFields(const ChartData& chart, std::vector<float>& forest, std::vector<float>& farm);
} // namespace earthCharts
