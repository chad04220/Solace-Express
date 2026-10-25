// Somnium Volandi - world: hand-designed archipelago, terrain, airports, weather
#pragma once
#include "common.h"
#include "road_network.h"
#include "bridges.h"
#include "settlements.h"
#include <string>
#include <memory>

namespace procedural { struct Descriptor; class Recipe; }
namespace earthCharts { struct ChartData; }
struct WorldSceneryState;

static const float WORLD_HALF = 40000.0f;  // the islands span [-40km, 40km] on x and z
// ...in 10 km of open sea all round, and past it the open ocean, out to the generated regions around them on the Earth
// (procedural_regions.h: the nearest some 200 km away, drawn on the horizon and solid to fly into). The islands used
// to repeat every 100 km both ways - [-50 km, 50 km) wrapped (wrapCoord) - so the horizon was the islands again in
// every direction; a World can still be made repeating (World::repeating), but none is
static const float WRAP_HALF = 50000.0f;
static const float WRAP_SPAN = 2.0f * WRAP_HALF;
static const float SEA_DEPTH = 80.0f;      // the open sea's depth round the islands' square (the band fades to it)
inline float wrapCoord(float v) { return v - WRAP_SPAN * floorf((v + WRAP_HALF) / WRAP_SPAN); }   // into [-50, 50) km
// the copy of coordinate v nearest ref: the short way round, across the seam where that is shorter (v itself, exactly,
// where it is not; within one period of ref)
inline float nearCopy(float v, float ref) { const float d = v - ref; return d >= WRAP_HALF ? v - WRAP_SPAN : d < -WRAP_HALF ? v + WRAP_SPAN : v; }
static const int HM_N = 2048;              // base heightmap resolution (39 m texels; smoothed, see World::build)
static const float HM_TEXEL = 2.0f * WORLD_HALF / HM_N;
static const float DETAIL_SCALE = 2200.0f; // wavelength of the procedural detail layer
static const int HMAX_N = 256;             // conservative max-height grid for ray-march skipping (mip chain)
static const int HMAX_LEVELS = 5;
static const int TP_LEVELS = 7;           // terrain envelope mesh: cell bounds from HM_N cells (level 0) to 32 per side
static const int TP_CHUNK = 32;           // cells per side of one envelope mesh chunk

// SURF_WATER: a seaplane base's water lane (docs/WINGS_OVER_WATER.md) - the sea, marked by buoys, never ground
enum Surface { SURF_ASPHALT = 0, SURF_GRASS, SURF_GRAVEL, SURF_SNOW, SURF_SAND, SURF_WATER };
inline const char* surfaceName(int s) { static const char* n[] = {"Asphalt", "Grass", "Gravel", "Snow", "Sand", "Water"}; return n[s]; }
inline bool surfaceRough(int s) { return s != SURF_ASPHALT; }

struct Airport {
  const char* code;
  const char* name;
  float x, z;        // runway centre (m)
  float elev;        // m
  float heading;     // degrees true of the runway direction (first designator)
  float length, width;
  int surface;
  int size;          // 0 = strip, 1 = regional, 2 = international
  const char* blurb;
  bool hospital = false;   // a hospital by the field (medevac destinations; set in World::build)
  // the side its buildings and apron stand on, its own (+1 right of its first designator's direction, -1 left): the
  // real Earth's, the same in every chart that holds it (World::buildEarth); 0 by its index, as the islands' have it
  // (aptSide, airport_layout.h)
  int side = 0;
  // a hub: the real Earth's large airports on a runway of 2.8 km or more (World::buildEarth) - a long terminal, its own
  // building every 260 m or so, between the cargo sheds and the AI stands (aptLayout)
  bool hub = false;
  // fuel costs more the further the bowser had to come: hubs x1, regional fields x1.15, strips x1.4, snow and gravel x1.8
  // (a seaplane base's comes by boat: x1.5)
  float fuelPriceMult() const {
    if (surface == SURF_WATER) return 1.5f;
    float m = size >= 2 ? 1.f : size == 1 ? 1.15f : 1.4f; return (surface == SURF_SNOW || surface == SURF_GRAVEL) ? std::max(m, 1.8f) : m;
  }
  // A seaplane base (docs/WINGS_OVER_WATER.md): its "runway" a water lane marked by buoys on the sea, uncontrolled, no
  // ground of its own - it shapes no terrain and its lane carries no paint or lights. Its dock (World::build,
  // seaplaneDocks): where the jetty leaves the shore and the way it runs out over the water (unit, x z), its length;
  // an aircraft moors alongside its head
  bool water() const { return surface == SURF_WATER; }
  float dockX = 0, dockZ = 0, dockDirX = 0, dockDirZ = 0, dockLen = 0;
  vec3 dockHead() const { return vec3(dockX + dockDirX * dockLen, 0.f, dockZ + dockDirZ * dockLen); }
  // derived
  vec3 dir() const { float h = heading * DEG; return vec3(sinf(h), 0, -cosf(h)); }
  vec3 pos() const { return vec3(x, elev, z); }
  vec3 threshold(bool reverse) const { return pos() - dir() * ((reverse ? -1.f : 1.f) * length * 0.5f); }
  int rwyNumber(bool reverse) const { int n = (int)lroundf(wrapDeg360(heading + (reverse ? 180 : 0)) / 10.0f); return n == 0 ? 36 : n; }
};

// A real airport's other runways (World::buildEarth, from OurAirports' ends): its main one is the Airport's own. Its
// centre, heading (degrees true of its first end toward its second), length and width (m), surface (SURF_), and the
// airport it belongs to (World::airports: onRunway answers with that one, whose height its ground is levelled to)
struct ExtraRunway { int airport; float x, z, heading, length, width; int surface; };
// Nothing of a runway reaches the ground beyond a rectangle round it: its approach funnel, 7 km past either end and at
// most 2.41 km either side of the line (halfw + 900 at the funnel's far end), its grounds well inside that (side: wider
// for a runway wider than 3.8 km). A point further from its middle (dx, dz) than that rectangle's corner 50 m out each
// way lies over 49 m past one of its sides: the runway's readings of the ground there (computeTexel's, airportLimits'
// and the rest, World::buildEarthTerrain's) pass it by before any of their trigonometry, its every weight exactly 0
inline bool beyondRunway(float dx, float dz, float length, float side = 2410.f) {
  const float a = length * 0.5f + 7000.f + 50.f, b = side + 50.f;
  return dx * dx + dz * dz > a * a + b * b;
}

struct Town { const char* name; float x, z, r; int kind; };  // kind 0 village, 1 town, 2 city

struct Box { vec3 c, h; int airport; int kind; };  // kind 0 hangar 1 tower 2 terminal 3 shed 4 fuel tank 5 radome
struct Lot;  // procedural town building (scenery.h)

struct Weather {
  float windFrom = 270, windSpeed = 3, gust = 0, turbulence = 0.1f;
  float cloudCover = 0.3f, cloudBase = 1400, visibility = 30000;
  int precip = 0;   // 0 none, 1 rain, 2 snow
  bool storm = false;
  float timeOfDay = 11.0f;  // hours
  // the cloud field's live state, moved on by the game every frame and never saved (weather.h, clouds.glsl): its drift
  // with the wind (m: the cloud pass's uWindOff), its fine detail's drift through the cloud bodies (m: the wisps
  // stream downwind off their edges) and the billows' rise (m: the cumulus boil)
  vec2 cloudDrift; vec3 cloudDetail; float cloudBoil = 0;
  std::string describe() const;
};

// The Fjordhaven reservoir (docs/LIVING_ISLANDS_PLAN.md, Phase C; landmarks.cpp findReservoir): an arch dam across a
// gorge in the northern mountains and the lake behind it. The dam: its chord's middle on the valley floor (x, z), the
// way up the valley (vx, vz), its chord (between its abutments, m), its floor's height there and its crest's above that;
// the water's level; a square round the lake (its centre cx, cz, half its side r). Cached with the world. The lake is
// built into the ground as the roads are (World::height, common.glsl groundH / terrainH): wherever its mask says water
// the ground stands at least at its level - so its shore is wherever the natural ground rises out of it - and the
// terrain paints it as water there
struct Reservoir {
  float on = 0, x = 0, z = 0, vx = 0, vz = 1, chord = 0, crest = 0, floor = 0, level = 0, cx = 0, cz = 0, r = 0;
  float scale() const { return chord / 300.f; }   // (the dam's mesh is drawn for a 300 m chord)
  float archR() const { return 180.f * scale(); }   // its upstream face's radius
  vec2 crown() const;   // its upstream face's middle
  vec2 archCentre() const;   // the centre of its arch, downstream
};
static const int RESERVOIR_FLOATS = 12;
// The lake's mask (landmarks.cpp reservoirMask): over the reservoir's square, per 8 m cell (its centre at x0 + (i + 0.5)
// cell), 1 where the water lies - the cells joined to the dam's face under its level, and two cells on into the ground
// that rises out of it (so the mask's edge lies under the shore) - else 0; read bilinear, water over 0.5. Made again
// from the natural ground whenever the world is built or loaded; the renderer hands it to the shaders (uData)
struct LakeMask { float x0 = 0, z0 = 0, cell = 8.f; int n = 0; std::vector<float> m; };

class World {
public:
  std::vector<Airport> airports;
  std::vector<ExtraRunway> extraRunways;   // (the real Earth's: above; none elsewhere)
  // The way houses are built here (the scenery shader's uStyle): 0 the islands' and the generated regions' own, else a
  // real-Earth chart's by its country (earthCharts::buildingStyleOf)
  int buildingStyle = 0;
  std::vector<Town> towns;
  bool procedural = false;
  bool repeating = false; // the old wrapping chart (above): no World built repeats
  // The procedural detail's share of its amplitude (hm's second channel, common.glsl uDetailScale): all of it on the
  // islands and the generated regions; little on the real Earth, whose data has the relief already - the channel
  // keeps its meaning there (how rough and natural the ground is: the scenery grows on it), only less of it is added
  float detailScale = 1.f;
  // Where the detail noise is read (common.glsl noiseP, uNoiseFrame): a real-Earth chart's x and z turned and moved into
  // the frame of an anchor, the centre of the 8-degree cell holding the chart's origin (World::buildEarth) - so
  // neighbouring charts read one field, agreeing to a fraction of a metre where the flight moves from one to the next.
  // Elsewhere the chart's own x and z
  float noiseCos = 1.f, noiseSin = 0.f, noiseX = 0.f, noiseZ = 0.f;
  vec2 noisePoint(float x, float z) const {
    if (detailScale >= 1.f) return vec2(x / DETAIL_SCALE, z / DETAIL_SCALE);
    return vec2((noiseCos * x - noiseSin * z + noiseX) / DETAIL_SCALE, (noiseSin * x + noiseCos * z + noiseZ) / DETAIL_SCALE);
  }
  // Where a point lies on the Earth in the frame neighbouring real-Earth charts share, in metres: its azimuthal x and z
  // from the anchor (the noise frame's, above), exactly - not the noise frame's turn and shift, which is metres out tens
  // of km from the chart's centre. The natural scenery's cells and the woods' noise are laid in it there, so the next
  // chart grows the same trees in the same places (entities.cpp, World::bakeMask). Read off a grid of the exact values
  // (anchorGrid, kAnchorN cells a side over the square: World::buildEarth), bilinear; elsewhere the chart's own x and z
  static constexpr int kAnchorN = 64;
  std::vector<double> anchorGrid;   // ((kAnchorN + 1)^2 points, x then z; empty off the real Earth)
  bool anchored() const { return !anchorGrid.empty(); }
  vec2 anchorPoint(float x, float z) const;
  float wrap(float v) const { return repeating ? wrapCoord(v) : v; }
  float nearPoint(float v, float ref) const { return repeating ? nearCopy(v, ref) : v; }
  std::string regionId = "solace";
  // Owns procedural names/geometry for every Airport and Town pointer. Copies share immutable storage.
  std::shared_ptr<const procedural::Recipe> regionRecipe;
  std::shared_ptr<WorldSceneryState> sceneryState; // baked placement data belongs to this chart
  // A chart of the real Earth (earth_charts.h, docs/REAL_EARTH_PLAN.md): its pack, its names, the records behind its
  // airports and its land cover. Owns the storage every Airport's and Town's name points into, as regionRecipe does
  std::shared_ptr<earthCharts::ChartData> earthChart;
  // The chart this World is, whichever kind: a real-Earth chart's, a generated region's, else Solace's
  procedural::Descriptor chartDescriptor() const;
  std::vector<Box> boxes;
  std::vector<float> hm;     // RGBA per texel: base height, detail amplitude, lushness, coldness
  // The road network (road_network.h): its paths (bridges, traffic) and the grid the terrain grades and the ground
  // material paints from - per mask texel the road segments that reach it, and the baked forest noise
  RoadNetwork roads;
  RoadGrid roadGrid;
  std::vector<Bridge> bridges;   // a structure under each of the network's bridge spans (bridges.h): made from it, not cached
  std::vector<SettlementField> settlements;   // each settlement's extent (settlements.h): from the ground and the roads, not cached
  std::vector<Farmstead> farms;   // the farmsteads' yards (settlements.h): read back off the network, not cached
  Reservoir reservoir;            // the Fjordhaven dam and its lake (above): cached
  LakeMask lake;                  // where its water lies (above): made from the reservoir, not cached
  void buildLakeMask();
  float lakeMaskAt(float x, float z) const;              // the mask, bilinear (0 off it)
  float lakeGround(float x, float z, float g) const;     // the ground g with the lake in it
  bool underLake(float x, float z) const;                // under the water: nothing grows or stands there
  // A real-Earth chart's lakes (World::buildEarthTerrain): per heightfield texel the level of the lake in it or beside
  // it (lakeCode), 0 where there is none; the ground is never below that level there, and is water where the natural
  // ground lies under it. Empty off the real Earth. Cached
  std::vector<uint16_t> lakes;
  static uint16_t lakeCode(float level) { return (uint16_t)std::clamp(lroundf((level + 1000.f) * 8.f), 1L, 65535L); }
  static float lakeLevelOf(uint16_t code) { return (float)code * 0.125f - 1000.f; }   // (exact in float, as on the GPU)
  uint16_t lakeAt(float x, float z) const;               // the code of the texel holding (x, z)
  std::vector<uint8_t> mask; // RGBA8: road distance, building density, urbanness, farmland / sea-stack flag
  std::vector<float> hmax[HMAX_LEVELS];   // upper bound of the terrain per cell, level L has HMAX_N>>L cells per side
  // cachePath: the generated arrays are read from there when its stamp matches (a launch after the first skips the
  // generation), else generated and written there (save false: not written - the tests, reading the run's own islands)
  void build(const std::string& cachePath = std::string(), const std::string& stamp = std::string(), bool save = true);
  void buildProcedural(const procedural::Descriptor& region, const std::string& cachePath = std::string(),
                       const std::string& stamp = std::string(), bool save = true);
  // A chart of the real Earth (an "earth:" descriptor, earth_charts.h) from the Earth pack: its terrain, coasts, land
  // cover, airports, towns and roads from the data (world_earth.cpp). buildProcedural sends such a descriptor here
  void buildEarth(const procedural::Descriptor& chart, const std::string& cachePath = std::string(),
                  const std::string& stamp = std::string(), bool save = true);
  bool loadCache(const std::string& path, const std::string& stamp);
  bool fromCache = false;   // the last build() read its cache (else it generated, and saved, the world)
  void saveCache(const std::string& path, const std::string& stamp) const;
  // Terrain envelope (the terrain mesh culls its chunks with it, terrain_mesh.cpp): an upper bound of the rendered
  // terrain at every vertex of the HM_N grid through the texel centres (tpV0, (HM_N+1)^2), and per cell (tpM[L])
  std::vector<float> tpV0, tpM[TP_LEVELS];
  void buildHMax();
  void buildEnvelope();
  void bakeMask();
  int fillInlandPits();   // lift ground below the sea that the sea can't reach above it (World::build); returns the pits it found
  void sampleMask(float x, float z, float out[4]) const;   // manual bilinear (matches shader)
  void maskTexel(float x, float z, float out[4]) const;    // nearest texel (matches shader texelFetch)
  float forestAt(float x, float z) const;                  // baked forest-patch noise, manual bilinear (matches shader)
  float groundHeight(float x, float z, int octaves = 8) const;          // same as height()
  int findAirport(const char* code) const;
  // Terrain height (m) at a world position (bare ground, the roads graded into it). octaves controls detail fidelity.
  float height(float x, float z, int octaves = 8) const;
  float naturalHeight(float x, float z, int octaves = 8) const;   // (the same without the roads)
  vec3 normal(float x, float z) const;
  // Base layer sample (manual bilinear, matches the shader exactly)
  void sampleBase(float x, float z, float out[4]) const;
  // Pure raw authored recipe for distant silhouettes only; no World arrays or
  // road/settlement generation. The normal Solace build remains unchanged.
  static void sampleAuthoredBase(float x, float z, float out[4]);
  // Returns airport index if (x,z) lies on a runway (with margin) - its main one, or one of its others - else -1; surface:
  // that runway's own (SURF_)
  int onRunway(float x, float z, float margin = 0, int* surface = nullptr) const;
  float distanceKm(int a, int b) const;
  int nearestAirport(float x, float z, float* distOut = nullptr) const;
private:
  void buildConfigured(const std::string& cachePath, const std::string& stamp, bool save);
  void seaplaneDocks();   // (each seaplane base's dock: Airport::dockX)
  void buildEarthTerrain();             // (world_earth.cpp: hm from the pack, the airports' grounds flattened)
  RoadNetwork buildEarthRoads() const;  // (the pack's roads, laid on that ground)
};

// World-explicit variants are required while building a different regional world.
float airportInfluence(const World& world, float x, float z);
// How strongly a point is kept clear of streets and buildings: an airport's grounds and approach funnels - or on the
// real Earth its grounds alone, where the funnels cap a building's height instead (airportGroundsCap), as real
// approaches do over the houses under them
float airportExclusion(const World& world, float x, float z);
// The least height over the sea the ground must have to be built on: 2.5 m (clear of the beaches), but on the real
// Earth 1 m where its cover is built-up - a city's waterfront, its filled land, stand that low
float buildableHeight(const World& world, float x, float z);
// The islands' own airports, wherever g_world is (the airline's routes fly there while a career is away)
int solaceAirportCount();
const Airport& solaceAirport(int i);
float solaceDistanceKm(int a, int b);
void airportGroundsCap(const World& world, float x, float z, float& grounds, float& cap);
float airportFunnelCeiling(const World& world, float x, float z);

// Noise shared with GLSL (keep in sync with shaders.h)
float hash2i(int x, int y);
void noised(float px, float pz, float& v, float& dx, float& dz);
float terrainFbm(float px, float pz, int octaves);

extern World g_world;
// The World scenery is being made from on this thread: g_world, or the real Earth's next chart while its scenery is
// staged before the move (entities.h Scenery::stage)
const World& sceneryWorld();
// The ground the game has dug this flight - the crash crater, the pieces' pits, plasma craters, laser scorches: what the
// renderer draws (common.glsl craterH) - x, z, radius, depth (its sign the shader's flag). The main thread's: what touches
// the ground in play (the aircraft, its weapons, the wreckage) comes down into them; the background quote flights, on
// the islands as built, never read it (the review of v3.44.0, WLD-1: an 8 m plasma crater was drawn over the old floor)
struct GroundPit { float x, z, R, D; };
extern std::vector<GroundPit> g_groundPits;
bool craterShape(float x, float z, float cx, float cz, float R, float D, float& h);   // a pit's floor and rim at x, z (adds to h)
float pitGround(float x, float z, float g);   // g, the ground's height at x, z, with the pits dug into it
#include <atomic>
extern std::atomic<int> g_worldStage;   // World::build: 0 not begun, 1 reading the cache, 2 generating, 3 done (the loading screen's wording)
