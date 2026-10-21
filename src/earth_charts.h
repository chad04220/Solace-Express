// Somnium Volandi - charts on the real Earth (docs/REAL_EARTH_PLAN.md section 3): where they lie, what they are
// called, the Earth pack they are built from, and what a chart built from it holds besides the World's own arrays.
// A real-Earth chart is a procedural::Descriptor with an "earth:" id, so Free Flight, the loading screen and the far
// terrain take it as they take a generated region; World::buildEarth builds it.
#pragma once
#include "earth_data.h"
#include "procedural_regions.h"

namespace earthCharts {
constexpr int kLattice = 32;                  // a chart's origin snaps to 1/32 of a degree each way
constexpr uint32_t kGeneratorVersion = 5;     // the real Earth's charts' content (their cache, never an id): 2, the built-up districts; 3, the airports' other runways; 4, the woods' noise in the shared frame; 5, the airports' own sides
constexpr const char* kIdPrefix = "earth:";

// The Earth pack the game reads: EARTH_PACK when set, else "earth" beside the game's working folder (opened when
// first asked for; nullptr, with packError(), when there is none)
std::shared_ptr<const earth::Pack> pack();
std::string packError();
void usePack(std::shared_ptr<const earth::Pack> pack);   // (the tests and tools: a pack of their own)
void reopen();   // (the search made again: a pack installed since, earth_install.h)
// Where the game looks for the pack, before "earth" in its working folder (beside the executable, in the user's and
// the cache's folders), and the folder fetched GLO-90 tiles go to (earth_fetch.h): set at startup, before pack()
void setSearch(std::vector<std::string> dirs, std::string glo90Cache);
std::string glo90Cache();

bool isEarthId(const std::string& id);
// A region as a sentence names it: "the Solace Islands", "the <generated region>", or a real-Earth chart's place bare
std::string regionPhrase(const procedural::Descriptor& region);
// How lush the ground grows for a land cover class at a latitude (World::hm's third channel, 0 bare .. 1 rainforest)
float lushOf(int cover, double lat);
// The chart whose origin is the lattice point nearest (lat, lon), named for the largest place near it
procedural::Descriptor chartAt(double lat, double lon);
std::string idAt(double lat, double lon);   // (its id alone: no name looked up)
std::optional<procedural::Descriptor> find(const std::string& id);
// Solace keeps its own chart in the real Earth (docs/REAL_EARTH_PLAN.md, E6): within kSolaceReach of its origin the
// chart to fly into is the islands' ("solace"), so no real-Earth chart is centred there and theirs reach only open
// ocean. Elsewhere it is idAt's
constexpr double kSolaceReach = 110000.0;
std::string slideIdAt(double lat, double lon);
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
// The airports a chart has, as World::buildEarth lays them: OurAirports' in its square, each on its main runway, the
// biggest first, none on another's grounds - or, with none there, the nearest beyond it. Without the rest of the chart:
// the career's boards and travel read them (procedural::airportOf). sampler: the chart's own (nullptr: one made here)
struct ChartAirports {
  std::vector<std::string> codes, names, blurbs;
  std::vector<earth::AirportRecord> records;
  std::vector<Airport> airports;   // (their code, name and blurb point into the vectors above)
  std::vector<ExtraRunway> extras;   // (their other runways, each with its airport's index above)
};
std::shared_ptr<const ChartAirports> chartAirports(const procedural::Descriptor& chart, const std::shared_ptr<const earth::Pack>& pack,
                                                   const earth::Sampler* sampler = nullptr);
// The way a country builds its houses (World::buildingStyle, the scenery shader's uStyle): 1 North America and the
// suburbs like it (Australia, New Zealand), 2 western and central Europe, 3 the Mediterranean and Latin America's
// colonial towns, 4 the Nordic countries, 5 the tropics, 6 East Asia, 7 the dry lands; a country not named, by its
// latitude (the tropics, else 2). country: ISO 3166-1 alpha-2, as OurAirports and GeoNames give it
enum BuildingStyle { STYLE_ISLANDS = 0, STYLE_NORTH_AMERICA, STYLE_EUROPE, STYLE_MEDITERRANEAN, STYLE_NORDIC, STYLE_TROPICS, STYLE_EAST_ASIA, STYLE_DRY };
int buildingStyleOf(const std::string& country, double lat, double lon);
// The land cover as the mask's two fields, per mask texel (World::bakeMask): how wooded (0 open .. 1 closed forest)
// and how farmed (cropland 1, grassland a little), blurred across the cover's 330 m cells
void maskFields(const ChartData& chart, std::vector<float>& forest, std::vector<float>& farm);
} // namespace earthCharts
