// Somnium Volandi - the real Earth's far terrain: the patches round the viewer and their meshes (earth_far.h)
#include "earth_far.h"
#include "earth_charts.h"
#include "region_travel.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <tuple>

namespace earthFar {
namespace {
constexpr double kMetresPerDegree = 111320.0;
struct Cell { int level, i, j; double south, west, north, size; };
Cell cellOf(int level, int i, int j) {
  const double size = patchDegrees(level), south = -90.0 + i * size;
  return {level, i, j, south, -180.0 + j * size, std::min(90.0, south + size), size};
}
int rows(int level) { return (int)std::ceil(180.0 / patchDegrees(level) - 1e-9); }
int columns(int level) { return (int)std::lround(360.0 / patchDegrees(level)); }
uint64_t fnv(const std::string& s) { uint64_t h = 1469598103934665603ull; for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } return h; }
std::string idOf(int level, int i, int j) {
  return std::string(kIdPrefix) + std::to_string(level) + ":" + std::to_string(i) + ":" + std::to_string(j);
}
bool parse(const std::string& id, int& level, int& i, int& j) {
  if (!isFarId(id) || id.size() > 40) return false;
  const char* s = id.c_str() + std::char_traits<char>::length(kIdPrefix);
  char* end = nullptr;
  const long l = strtol(s, &end, 10); if (end == s || *end != ':') return false;
  const char* t = end + 1; const long a = strtol(t, &end, 10); if (end == t || *end != ':') return false;
  const char* u = end + 1; const long b = strtol(u, &end, 10); if (end == u || *end != '\0') return false;
  if (l < 0 || l >= kLevels || a < 0 || a >= rows((int)l) || b < 0 || b >= columns((int)l)) return false;
  level = (int)l; i = (int)a; j = (int)b;
  return idOf(level, i, j) == id;   // (canonical only)
}
// How far a viewer is from a cell (m): to the cell's nearest latitude and longitude, near enough the nearest point
double distanceTo(const geo::Position& v, const Cell& c) {
  const double lat = std::clamp(v.latitude, c.south, c.north), centre = c.west + c.size * 0.5;
  const double lon = centre + std::clamp(std::remainder(v.longitude - centre, 360.0), -c.size * 0.5, c.size * 0.5);
  return geo::surfaceDistance({v.latitude, v.longitude, 0}, {lat, lon, 0});
}
double horizonDistance(double height) {
  const double h = std::max(0.0, height), r = geo::radius();
  return r * std::atan2(std::sqrt(h / r * (2.0 + h / r)), 1.0);
}
} // namespace

bool isFarId(const std::string& id) { return id.compare(0, std::char_traits<char>::length(kIdPrefix), kIdPrefix) == 0; }

procedural::Descriptor patch(int level, int i, int j) {
  const Cell c = cellOf(level, i, j);
  procedural::Descriptor d;
  d.id = idOf(level, i, j);
  d.origin = {0.5 * (c.south + c.north), std::remainder(c.west + c.size * 0.5, 360.0), 0.0};
  d.seed = fnv(d.id);
  d.address = geo::positionTile(d.origin, procedural::kAddressLevel).tile;
  d.archetype = procedural::Archetype::ContinentalFragments;
  d.scale = procedural::Scale::Large;
  d.authored = false;
  return d;
}

std::optional<procedural::Descriptor> find(const std::string& id) {
  int level, i, j;
  if (!parse(id, level, i, j)) return std::nullopt;
  return patch(level, i, j);
}

horizon::Selection select(geo::Position viewer, const geo::Position& chartOrigin, const earth::Box& data, size_t maxCount) {
  (void)geo::direction(viewer);   // (validates it)
  const double reach = horizonDistance(viewer.height + horizon::kAltitudeLookahead) + horizonDistance(horizon::kSummitHeight);
  const regionTravel::Chart chart(chartOrigin);
  auto inChart = [&](const Cell& c) {   // (wholly inside the chart's square: the chart is drawn there)
    for (double a : {0.0, 0.5, 1.0}) for (double b : {0.0, 0.5, 1.0}) {
      const vec3 p = chart.local({c.south + (c.north - c.south) * a, c.west + c.size * b, 0});
      if (std::max(std::fabs(p.x), std::fabs(p.z)) > WORLD_HALF - 1000.f) return false;
    }
    return true;
  };
  auto hasData = [&](const Cell& c) {
    return c.south < data.north && c.north > data.south && c.west < data.east && c.west + c.size > data.west;
  };
  struct Pick { Cell cell; double distance; };
  std::vector<Pick> picks;
  auto visit = [&](auto&& self, int level, int i, int j) -> void {
    const Cell c = cellOf(level, i, j);
    if (c.south >= 90.0 || !hasData(c)) return;
    const double d = distanceTo(viewer, c);
    if (d > reach || inChart(c)) return;
    if (level > 0 && d < kSplit * c.size * kMetresPerDegree) {
      for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) self(self, level - 1, 2 * i + a, 2 * j + b);
      return;
    }
    picks.push_back({c, d});
  };
  const int top = kLevels - 1;
  const double size = patchDegrees(top), reachDegrees = reach / kMetresPerDegree + size;
  const int i0 = std::max(0, (int)std::floor((viewer.latitude - reachDegrees + 90.0) / size));
  const int i1 = std::min(rows(top) - 1, (int)std::floor((viewer.latitude + reachDegrees + 90.0) / size));
  const double widest = std::min(89.0, std::fabs(viewer.latitude) + reachDegrees);
  const double lonSpan = reachDegrees / std::max(std::cos(widest * geo::kDegrees), 0.02);
  const int n = columns(top);
  const int j0 = lonSpan >= 180.0 ? 0 : (int)std::floor((viewer.longitude - lonSpan + 180.0) / size);
  const int j1 = lonSpan >= 180.0 ? n - 1 : (int)std::floor((viewer.longitude + lonSpan + 180.0) / size);
  for (int i = i0; i <= i1; i++)
    for (int jj = j0; jj <= std::min(j1, j0 + n - 1); jj++) visit(visit, top, i, ((jj % n) + n) % n);
  std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
    return a.distance != b.distance ? a.distance < b.distance :
      std::tie(a.cell.level, a.cell.i, a.cell.j) < std::tie(b.cell.level, b.cell.i, b.cell.j); });
  horizon::Selection out;
  out.reachMetres = reach;
  // the Solace Islands, at their place in the Atlantic, as the generated regions' far pass draws them (their own
  // silhouette, horizon::build): first, within sight, from any chart but theirs
  const bool solace = geo::surfaceDistance(viewer, geo::solaceOrigin()) <= reach + std::sqrt(2.0) * WORLD_HALF &&
                      geo::surfaceDistance(chartOrigin, geo::solaceOrigin()) > 1000.0;
  if (solace && maxCount > 0) out.regions.push_back(procedural::solaceDescriptor());
  for (size_t k = 0; k < picks.size() && out.regions.size() < maxCount; k++) out.regions.push_back(patch(picks[k].cell.level, picks[k].cell.i, picks[k].cell.j));
  out.complete = picks.size() + (solace ? 1 : 0) <= maxCount;
  return out;
}

horizon::Mesh build(const procedural::Descriptor& descriptor, const std::shared_ptr<const earth::Pack>& pack) {
  int level, ci, cj;
  if (!pack || !parse(descriptor.id, level, ci, cj)) throw std::invalid_argument("not a real-Earth far patch");
  const Cell c = cellOf(level, ci, cj);
  horizon::Mesh mesh;
  mesh.region = patch(level, ci, cj);
  const int R = kResolution, N = R + 1;
  const double dLat = (c.north - c.south) / R, dLon = c.size / R;
  earth::Sampler sampler(pack, earth::Box{c.south - 0.02, c.west - 0.02, c.north + 0.02, c.west + c.size + 0.02}, false);
  const geo::LocalFrame frame(mesh.region.origin);
  std::vector<horizon::Vertex> grid((size_t)N * N);
  for (int r = 0; r < N; r++) {
    generationCheckpoint();
    for (int k = 0; k < N; k++) {
      horizon::Vertex& v = grid[(size_t)r * N + k];
      const double lat = c.north - r * dLat, lon = std::remainder(c.west + k * dLon, 360.0);
      // (as a chart's ground (World::buildEarthTerrain): the sea where the cover says, and its water at the sea's
      // level; beyond the pack's data, the sea)
      const bool present = sampler.hasElevation(lat, lon);
      const float e = present ? sampler.coarseElevation(lat, lon) : -100.f;
      const int cover = present ? sampler.cover(lat, lon) : earth::COVER_SEA;
      const bool sea = !present || (cover == earth::COVER_SEA && e <= 2.f) || (cover == earth::COVER_WATER && e <= 1.5f) ||
                       (cover == earth::COVER_UNKNOWN && e <= 0.f);
      v.height = sea ? std::min(e, -1.f) : std::max(e, 0.6f);
      v.position = frame.toLocal({lat, lon, std::max(v.height, -0.5f)});
      // the far pass's palette (Renderer::prepareFarTerrain), from the cover's lushness, the latitude and the height
      const float alat = (float)std::fabs(lat);
      const float snowline = 5200.f * std::clamp(1.f - (alat - 20.f) / 55.f, 0.f, 1.f);
      const float cold = std::max(0.85f * smoothstepf(52.f, 72.f, alat), smoothstepf(snowline - 900.f, snowline, v.height));
      const float lush = sea ? 0.f : earthCharts::lushOf(cover, lat);
      v.colour = lerp(vec3(.22f, .23f, .105f), vec3(.105f, .19f, .075f), lush);
      v.colour = lerp(v.colour, vec3(.31f, .29f, .25f), smoothstepf(450.f, 1600.f, v.height));
      const float snowLine = lerpf(1800.f, 500.f, cold);
      v.colour = lerp(v.colour, vec3(.82f, .85f, .87f), smoothstepf(snowLine - 100.f, snowLine + 180.f, v.height));
      if (cover == earth::COVER_WATER) v.colour = vec3(.035f, .07f, .08f);
      else if (cover == earth::COVER_BUILT) v.colour = lerp(v.colour, vec3(.33f, .32f, .3f), .55f);
      else if (cover == earth::COVER_BARE) v.colour = lerp(v.colour, vec3(.46f, .41f, .29f), .8f);
      else if (cover == earth::COVER_SNOW) v.colour = vec3(.82f, .85f, .87f);
    }
  }
  for (int r = 0; r < N; r++) for (int k = 0; k < N; k++) {
    const vec3 dx = grid[(size_t)r * N + std::min(k + 1, N - 1)].position - grid[(size_t)r * N + std::max(k - 1, 0)].position;
    const vec3 dz = grid[(size_t)std::min(r + 1, N - 1) * N + k].position - grid[(size_t)std::max(r - 1, 0) * N + k].position;
    grid[(size_t)r * N + k].normal = normalize(cross(dz, dx));
  }
  double reach = 0;
  for (double lat : {c.south, c.north}) for (double lon : {c.west, c.west + c.size})
    reach = std::max(reach, geo::surfaceDistance(mesh.region.origin, {lat, lon, 0}));
  std::vector<regionalSurface::GridVertex> compact; compact.reserve(grid.size());
  for (const auto& v : grid) compact.push_back({v.position, v.height});
  mesh.surface = regionalSurface::makeGeographicSurface(mesh.region, N, std::move(compact), c.north, c.west, dLat, dLon, reach + 2000.0);
  // the triangles, as the generated regions' (horizon::buildMesh): any with dry ground; then a skirt down from every
  // edge (both faces), the cracks between a patch and a coarser neighbour hidden under it
  auto dry = [&](const horizon::Vertex& a, const horizon::Vertex& b, const horizon::Vertex& d) { return std::max({a.height, b.height, d.height}) >= 0.f; };
  auto triangle = [&](const horizon::Vertex& a, const horizon::Vertex& b, const horizon::Vertex& d) {
    if (dry(a, b, d)) { mesh.vertices.push_back(a); mesh.vertices.push_back(b); mesh.vertices.push_back(d); }
  };
  mesh.vertices.reserve((size_t)R * R * 6 + (size_t)R * 4 * 12);
  for (int r = 0; r < R; r++) for (int k = 0; k < R; k++) {
    const size_t i = (size_t)r * N + k;
    triangle(grid[i], grid[i + N], grid[i + 1]); triangle(grid[i + 1], grid[i + N], grid[i + N + 1]);
  }
  const float skirt = 50.f + 0.05f * float(dLat * kMetresPerDegree);
  auto lowered = [&](const horizon::Vertex& v) { horizon::Vertex w = v; w.height = v.height - skirt; return w; };
  auto edge = [&](size_t a, size_t b) {
    const horizon::Vertex A = grid[a], B = grid[b], A2 = lowered(A), B2 = lowered(B);
    triangle(A, B, A2); triangle(B, B2, A2);
    triangle(A, A2, B); triangle(B, A2, B2);
  };
  for (int k = 0; k < R; k++) {
    edge((size_t)k, (size_t)k + 1);                                       // (the north edge)
    edge((size_t)R * N + k, (size_t)R * N + k + 1);                       // (the south)
    edge((size_t)k * N, (size_t)(k + 1) * N);                             // (the west)
    edge((size_t)k * N + R, (size_t)(k + 1) * N + R);                     // (the east)
  }
  return mesh;
}
} // namespace earthFar
