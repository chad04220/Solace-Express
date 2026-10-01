// Air Xpress - world: hand-designed archipelago, terrain, airports, weather
#pragma once
#include "common.h"

static const float WORLD_HALF = 40000.0f;  // map spans [-40km, 40km] on x and z
static const int HM_N = 1024;              // base heightmap resolution
static const float HM_TEXEL = 2.0f * WORLD_HALF / HM_N;
static const float DETAIL_SCALE = 2200.0f; // wavelength of the procedural detail layer

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
  // derived
  vec3 dir() const { float h = heading * DEG; return vec3(sinf(h), 0, -cosf(h)); }
  vec3 pos() const { return vec3(x, elev, z); }
  vec3 threshold(bool reverse) const { return pos() - dir() * ((reverse ? -1.f : 1.f) * length * 0.5f); }
  int rwyNumber(bool reverse) const { int n = (int)lroundf(wrapDeg360(heading + (reverse ? 180 : 0)) / 10.0f); return n == 0 ? 36 : n; }
};

struct Box { vec3 c, h; int airport; int kind; };  // centre, half extent in runway frame; kind 0 hangar 1 tower 2 terminal 3 house

struct Weather {
  float windFrom = 270, windSpeed = 3, gust = 0, turbulence = 0.1f;
  float cloudCover = 0.3f, cloudBase = 1400, visibility = 30000;
  int precip = 0;   // 0 none, 1 rain, 2 snow
  bool storm = false;
  float timeOfDay = 11.0f;  // hours
  std::string describe() const;
};

class World {
public:
  std::vector<Airport> airports;
  std::vector<Box> boxes;
  std::vector<float> hm;     // RGBA per texel: base height, detail amplitude, lushness, coldness
  void build();
  int findAirport(const char* code) const;
  // Terrain height (m) at world position. octaves controls detail fidelity.
  float height(float x, float z, int octaves = 8) const;
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
