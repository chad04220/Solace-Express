// Somnium Volandi - the real Earth's data (docs/REAL_EARTH_PLAN.md section 2): the Earth pack tools/earth2 builds,
// read as it is - 1-degree elevation and land-cover tiles in 10-degree bundles, the airport, place and road tables -
// and the Copernicus GLO-90 elevation tiles (Cloud Optimized GeoTIFFs, in the pack or the download cache).
// Thread safe: the tiles are cached behind a lock and shared; a Sampler holds the ones a box needs and reads them
// without one (a chart's build samples millions of points).
#pragma once
#include "common.h"
#include <map>

namespace earth {
constexpr int kElevSamples = 120, kCoverSamples = 336, kApron = 1;

// CGLS-LC100's discrete classes (the pack's cover layer)
enum Cover : uint8_t {
  COVER_UNKNOWN = 0, COVER_SHRUBS = 20, COVER_HERBACEOUS = 30, COVER_CROPLAND = 40, COVER_BUILT = 50, COVER_BARE = 60,
  COVER_SNOW = 70, COVER_WATER = 80, COVER_WETLAND = 90, COVER_MOSS = 100,
  // forests, closed (111-116) and open (121-126): evergreen needleleaf, evergreen broadleaf, deciduous needleleaf,
  // deciduous broadleaf, mixed, unknown
  COVER_FOREST_CLOSED = 111, COVER_FOREST_OPEN = 121, COVER_SEA = 200
};
inline bool coverForest(int c) { return (c >= 111 && c <= 116) || (c >= 121 && c <= 126); }
inline bool coverClosedForest(int c) { return c >= 111 && c <= 116; }
inline int coverForestType(int c) { return coverForest(c) ? (c % 10) : 0; }   // 1..6 as above, 0 not a forest
inline bool coverWater(int c) { return c == COVER_WATER || c == COVER_SEA; }

// A box of latitude and longitude (degrees, south < north, west < east, never across the antimeridian)
struct Box {
  double south = 0, west = 0, north = 0, east = 0;
  bool contains(double lat, double lon) const { return lat >= south && lat < north && lon >= west && lon < east; }
};

// One 1-degree raster tile: (n + 2 kApron)^2 cell-centred samples, rows from north to south. Stored row r, column c
// (from -kApron) is centred at latitude lat + 1 - (r + 0.5) / n, longitude lon + (c + 0.5) / n
struct RasterTile {
  int lat = 0, lon = 0, n = 0;   // the south-west corner, samples per degree
  std::vector<int16_t> s16;      // elevation (m)
  std::vector<uint8_t> s8;       // cover classes
  int width() const { return n + 2 * kApron; }
  int at(int r, int c) const {
    const size_t i = (size_t)(r + kApron) * width() + (c + kApron);
    return s16.empty() ? (int)s8[i] : (int)s16[i];
  }
  size_t bytes() const { return s16.size() * 2 + s8.size(); }
};

// A Copernicus GLO-90 tile: point samples, (r, c) at latitude north - r dLat, longitude west + c dLon (r, c from 0;
// the tile holds its north and west edges, its neighbours the south and east ones). A tile may hold only some of its
// degree's rows (the test fixture's is cropped): rowStart is its first row's place among the whole tile's
struct Glo90Tile {
  int lat = 0, lon = 0, rows = 0, cols = 0, rowStart = 0;
  double north = 0, west = 0, dLat = 0, dLon = 0;
  std::vector<float> h;
  float at(int r, int c) const { return h[(size_t)r * cols + c]; }
  size_t bytes() const { return h.size() * 4; }
};
// The COG reader (classic TIFF; Float32, uncompressed or Deflate, predictor none or floating point, tiled or in
// strips; GeoTIFF pixel scale and tie point). nullptr with a reason on anything else
std::shared_ptr<Glo90Tile> readGlo90(const std::string& path, std::string* error = nullptr);
std::string glo90Name(int lat, int lon);   // Copernicus_DSM_COG_30_N41_00_W071_00_DEM

struct Runway {
  double lat1 = 0, lon1 = 0, lat2 = 0, lon2 = 0;   // the two ends, when endsKnown
  bool endsKnown = false, lighted = false;
  float length = 0, width = 0;                      // m (width 0: unknown)
  int surface = 0;                                  // 0 unknown, 1 asphalt, 2 concrete, 3 grass, 4 gravel, 5 dirt, 6 sand, 7 water, 8 snow or ice
  // (known or not by flag, never NaN: the game builds with fast math)
  bool hasElev1 = false, hasElev2 = false, hasHeading = false;
  float elev1 = 0, elev2 = 0;                       // m, at each end
  float heading = 0;                                // degrees true of end 1 toward end 2
  std::string ident1, ident2;
  float displaced1 = 0, displaced2 = 0;             // m
};
struct AirportRecord {
  std::string ident, name, municipality, country, iata;
  double lat = 0, lon = 0;
  bool hasElevation = false;
  float elevation = 0;     // m
  int type = 0;            // 0 small, 1 medium, 2 large
  bool scheduled = false;
  uint32_t id = 0;         // OurAirports'
  std::vector<Runway> runways;   // the longest hard runway first, then by length
};
struct PlaceRecord {
  std::string name, country;
  double lat = 0, lon = 0;
  uint32_t population = 0, id = 0;   // id: GeoNames'
  int kind = 0;                      // 0 place, 1 seat of a minor division, 2 of a first-order one, 3 capital
};
struct RoadEdge {
  uint64_t id = 0;
  int cls = 0;        // GRIP: 1 highway, 2 primary, 3 secondary, 4 tertiary, 5 local, 0 unspecified
  int surface = 0;    // 1 paved, 2 unpaved, 0 unknown
  std::vector<std::pair<double, double>> pts;   // (lat, lon), junction to junction
};

class Pack {
public:
  // The pack in dir (its pack.idx); nullptr, with the reason, when there is none or it can't be read
  static std::shared_ptr<Pack> open(const std::string& dir, std::string* error = nullptr);
  ~Pack();
  const std::string& dir() const { return dir_; }
  Box box() const { return box_; }
  bool hasCover() const { return hasCover_; }
  bool hasLocalRoads() const { return hasLocalRoads_; }
  // Tiles, cached; nullptr where the pack has none (outside its box). Throws std::runtime_error on a damaged one
  std::shared_ptr<const RasterTile> elevTile(int lat, int lon) const;
  std::shared_ptr<const RasterTile> coverTile(int lat, int lon) const;
  std::shared_ptr<const std::vector<RoadEdge>> roadTile(int lat, int lon, bool local) const;
  // GLO-90: whether Copernicus has a tile there (the pack's bitmap; false without one), where it is on disk ("" when
  // it isn't), and the tile itself (cached; nullptr when not on disk or unreadable)
  bool glo90Listed(int lat, int lon) const;
  std::string glo90File(int lat, int lon) const;
  void setGlo90Cache(const std::string& dir);   // (a folder of downloaded tiles, searched after the pack's own)
  std::shared_ptr<const Glo90Tile> glo90Tile(int lat, int lon) const;
  // The tables, by box
  std::vector<AirportRecord> airports(const Box& box) const;
  std::vector<PlaceRecord> places(const Box& box) const;
  std::vector<RoadEdge> roads(const Box& box, bool local) const;   // each edge once, those whose bounds reach the box
  size_t airportCount() const;
  size_t placeCount() const;
  size_t cachedBytes() const;
  struct Impl;
private:
  Pack() = default;
  std::string dir_;
  Box box_;
  bool hasCover_ = false, hasLocalRoads_ = false;
  std::unique_ptr<Impl> impl_;
};

// The data over a box, held for lock-free sampling: the elevation tiles (and GLO-90's where it is on disk) and the
// cover over the box and a sample's reach beyond it
class Sampler {
public:
  Sampler(std::shared_ptr<const Pack> pack, const Box& box, bool fine = true);
  // The best height there (m): GLO-90 where it is loaded (bicubic), else ETOPO's 30" (bicubic)
  float elevation(double lat, double lon) const;
  float coarseElevation(double lat, double lon) const;   // ETOPO alone
  bool fine(double lat, double lon) const;                // GLO-90 is what elevation() reads there
  int cover(double lat, double lon) const;                // nearest (COVER_UNKNOWN without the layer)
  const Box& box() const { return box_; }
  int fineTiles() const { return fineCount_; }
private:
  std::shared_ptr<const Pack> pack_;
  Box box_;
  int lat0_ = 0, lon0_ = 0, nLat_ = 0, nLon_ = 0;   // the tiles held: [lat0, lat0 + nLat) x [lon0, lon0 + nLon)
  std::vector<std::shared_ptr<const RasterTile>> elev_, cover_;
  std::vector<std::shared_ptr<const Glo90Tile>> fine_;
  int fineCount_ = 0;
  int slot(int lat, int lon) const;
  float etopoSample(long row, long col) const;     // global sample indices (row from the north pole, col from 180 W)
  float glo90Point(int lat, int lon, long row, double lonDeg, bool& ok) const;
};

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);
// zlib (RFC 1950) stream into exactly outSize bytes; false when it isn't one or doesn't fill them exactly
bool inflate(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize);
} // namespace earth
