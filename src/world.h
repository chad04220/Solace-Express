// Solace Express - world: hand-designed archipelago, terrain, airports, weather
#pragma once
#include "common.h"
#include "road_network.h"
#include <string>

static const float WORLD_HALF = 40000.0f;  // map spans [-40km, 40km] on x and z
static const int HM_N = 2048;              // base heightmap resolution (39 m texels; smoothed, see World::build)
static const float HM_TEXEL = 2.0f * WORLD_HALF / HM_N;
static const float DETAIL_SCALE = 2200.0f; // wavelength of the procedural detail layer
static const int HMAX_N = 256;             // conservative max-height grid for ray-march skipping (mip chain)
static const int HMAX_LEVELS = 5;
static const int TP_LEVELS = 7;           // terrain envelope mesh: cell bounds from HM_N cells (level 0) to 32 per side
static const int TP_CHUNK = 32;           // cells per side of one envelope mesh chunk

enum Surface { SURF_ASPHALT = 0, SURF_GRASS, SURF_GRAVEL, SURF_SNOW, SURF_SAND };
inline const char* surfaceName(int s) { static const char* n[] = {"Asphalt", "Grass", "Gravel", "Snow", "Sand"}; return n[s]; }
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
  // fuel costs more the further the bowser had to come: hubs x1, regional fields x1.15, strips x1.4, snow and gravel x1.8
  float fuelPriceMult() const { float m = size >= 2 ? 1.f : size == 1 ? 1.15f : 1.4f; return (surface == SURF_SNOW || surface == SURF_GRAVEL) ? std::max(m, 1.8f) : m; }
  // derived
  vec3 dir() const { float h = heading * DEG; return vec3(sinf(h), 0, -cosf(h)); }
  vec3 pos() const { return vec3(x, elev, z); }
  vec3 threshold(bool reverse) const { return pos() - dir() * ((reverse ? -1.f : 1.f) * length * 0.5f); }
  int rwyNumber(bool reverse) const { int n = (int)lroundf(wrapDeg360(heading + (reverse ? 180 : 0)) / 10.0f); return n == 0 ? 36 : n; }
};

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

class World {
public:
  std::vector<Airport> airports;
  std::vector<Box> boxes;
  std::vector<float> hm;     // RGBA per texel: base height, detail amplitude, lushness, coldness
  // The road network (road_network.h): its paths (bridges, traffic) and the grid the terrain grades and the ground
  // material paints from - per mask texel the road segments that reach it, and the baked forest noise
  RoadNetwork roads;
  RoadGrid roadGrid;
  std::vector<uint8_t> mask; // RGBA8: road distance, building density, urbanness, farmland / sea-stack flag
  std::vector<float> hmax[HMAX_LEVELS];   // upper bound of the terrain per cell, level L has HMAX_N>>L cells per side
  // cachePath: the generated arrays are read from there when its stamp matches (a launch after the first skips the
  // generation), else generated and written there
  void build(const std::string& cachePath = std::string(), const std::string& stamp = std::string());
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
  bool lotAt(int i, int j, Lot& out) const;                 // a town lot with a building on it (entities.cpp picks the building)
  int findAirport(const char* code) const;
  // Terrain height (m) at a world position (bare ground, the roads graded into it). octaves controls detail fidelity.
  float height(float x, float z, int octaves = 8) const;
  float naturalHeight(float x, float z, int octaves = 8) const;   // (the same without the roads)
  vec3 normal(float x, float z) const;
  // Base layer sample (manual bilinear, matches the shader exactly)
  void sampleBase(float x, float z, float out[4]) const;
  // Returns airport index if (x,z) lies on a runway (with margin), else -1
  int onRunway(float x, float z, float margin = 0) const;
  float distanceKm(int a, int b) const;
  int nearestAirport(float x, float z, float* distOut = nullptr) const;
};

// Noise shared with GLSL (keep in sync with shaders.h)
float hash2i(int x, int y);
void noised(float px, float pz, float& v, float& dx, float& dz);
float terrainFbm(float px, float pz, int octaves);

extern World g_world;
#include <atomic>
extern std::atomic<int> g_worldStage;   // World::build: 0 not begun, 1 reading the cache, 2 generating, 3 done (the loading screen's wording)
