#include "distant_regions.h"
#include "region_travel.h"
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace horizon {
namespace {
constexpr size_t kCatalogSize = 6 * (1 << (2 * procedural::kAddressLevel)) + 1;
constexpr double kSqrt2 = 1.4142135623730950488;
constexpr double kDistanceSlack = .001; // one millimetre for angular round-off
struct Center { geo::Double3 direction; char id[18]{}; };
static_assert(sizeof(Center) * kCatalogSize < 320 * 1024, "center catalog must stay small");
static_assert(sizeof(Vertex) == 10 * sizeof(float), "GPU vertex layout changed");

bool finite(double x) {
  uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
  return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
const std::array<Center, kCatalogSize>& catalog() {
  // All six faces make poles, date-line aliases and cube seams ordinary cases.
  // Initialization constructs only temporary descriptors, never terrain/Worlds.
  // Reserved addresses are resolved by the catalog's own canonical policy.
  static const auto centers = [] {
    std::array<Center, kCatalogSize> out{};
    size_t i = 0;
    constexpr int side = 1 << procedural::kAddressLevel;
    for (int f = 0; f < 6; ++f) for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x, ++i) {
      const auto p = geo::fromDirection(geo::tileDirection({geo::Face(f), procedural::kAddressLevel, x, y}, .5, .5));
      const auto d = procedural::describe(p);
      if (d.authored) continue; // Solace is inserted once, at its authored origin
      out[i].direction = geo::direction(d.origin);
      std::memcpy(out[i].id, d.id.c_str(), d.id.size() + 1);
    }
    out.back().direction = geo::direction(geo::solaceOrigin());
    std::memcpy(out.back().id, "solace", 7);
    return out;
  }();
  return centers;
}
double horizonDistance(double height) {
  // atan2 avoids acos losing tiny positive altitudes to a rounded-to-one ratio.
  // Scale the tangent by radius or height so DBL_MAX cannot overflow either.
  const double h = std::max(0.0, height), r = geo::radius();
  if (h > r) { const double q = r / h; return r * std::atan2(std::sqrt(1.0 + 2.0 * q), q); }
  const double q = h / r;
  return r * std::atan2(std::sqrt(q * (2.0 + q)), 1.0);
}
procedural::Descriptor canonical(const procedural::Descriptor& region) {
  const auto d = procedural::find(region.id, region.worldSeed);
  // Validate geography before arithmetic so fast-math builds reject NaN too.
  geo::direction(region.origin);
  if (!d || d->authored != region.authored || d->generatorVersion != region.generatorVersion ||
      d->seed != region.seed || d->archetype != region.archetype || region.origin.height != 0 ||
      d->address.face != region.address.face || d->address.level != region.address.level ||
      d->address.x != region.address.x || d->address.y != region.address.y ||
      geo::surfaceDistance(d->origin, region.origin) > kDistanceSlack)
    throw std::invalid_argument("unsupported or noncanonical distant region");
  return *d;
}
} // namespace

Selection select(geo::Position camera, uint64_t worldSeed, size_t maxCount,
                 double altitudeLookahead, double summitHeight) {
  const auto direction = geo::direction(camera); // validates the complete input
  if (!finite(altitudeLookahead) || altitudeLookahead < 0 || !finite(summitHeight) || summitHeight < 0)
    throw std::invalid_argument("invalid distant region horizon options");
  maxCount = std::min(maxCount, kMaxRegions);
  const double height = std::max(0.0, camera.height);
  const double ahead = height > std::numeric_limits<double>::max() - altitudeLookahead
    ? std::numeric_limits<double>::max() : height + altitudeLookahead;
  const double horizon = horizonDistance(ahead) + horizonDistance(std::max(summitHeight, kSummitHeight));
  const double generatedReach = std::min(geo::kPi * geo::radius(), horizon + kSqrt2 * procedural::kOceanEdge);
  const double authoredReach = std::min(geo::kPi * geo::radius(), horizon + kSqrt2 * WORLD_HALF);
  const double generatedCos = std::cos(std::min(geo::kPi, (generatedReach + kDistanceSlack) / geo::radius()));
  const double authoredCos = std::cos(std::min(geo::kPi, (authoredReach + kDistanceSlack) / geo::radius()));
  struct Candidate { const Center* center = nullptr; double distance = 0; };
  std::array<Candidate, kMaxRegions> nearest{};
  size_t count = 0, eligible = 0;
  auto less = [](const Candidate& a, const Candidate& b) {
    return a.distance == b.distance ? std::strcmp(a.center->id, b.center->id) < 0 : a.distance < b.distance;
  };
  const auto& centers = catalog();
  for (const auto& center : centers) {
    if (!center.id[0]) continue;
    const bool authored = &center == &centers.back();
    const double dot = std::clamp(geo::dot(direction, center.direction), -1.0, 1.0);
    if (dot < (authored ? authoredCos : generatedCos)) continue;
    const double distance = geo::radius() * std::atan2(geo::length(geo::cross(direction, center.direction)), dot);
    if (distance > (authored ? authoredReach : generatedReach) + kDistanceSlack) continue;
    ++eligible;
    if (!maxCount) continue;
    const Candidate candidate{&center, distance};
    size_t at = count;
    while (at && less(candidate, nearest[at - 1])) --at;
    if (at == maxCount) continue;
    const size_t end = std::min(count, maxCount - 1);
    for (size_t i = end; i > at; --i) nearest[i] = nearest[i - 1];
    nearest[at] = candidate;
    count = std::min(count + 1, maxCount);
  }
  Selection result;
  result.complete = eligible <= maxCount;
  result.reachMetres = authoredReach;
  result.regions.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto d = procedural::find(nearest[i].center->id, worldSeed);
    if (!d) throw std::logic_error("distant center catalog disagrees with region catalog");
    result.regions.push_back(*d);
  }
  return result;
}

Mesh buildMesh(const procedural::Descriptor& descriptor, int resolution) {
  if (resolution < 2 || resolution > kMeshResolution)
    throw std::invalid_argument("distant mesh resolution must be between 2 and 129");
  Mesh mesh; mesh.region = canonical(descriptor);
  std::unique_ptr<procedural::Recipe> recipe;
  if (!mesh.region.authored) recipe = std::make_unique<procedural::Recipe>(mesh.region);
  const regionTravel::Chart chart(mesh.region.origin);
  const geo::LocalFrame frame(mesh.region.origin);
  const float cell = 2.f * WORLD_HALF / (resolution - 1);
  std::vector<Vertex> grid(size_t(resolution) * resolution);
  for (int z = 0; z < resolution; ++z) for (int x = 0; x < resolution; ++x) {
    auto& v = grid[size_t(z) * resolution + x];
    const float wx = -WORLD_HALF + x * cell, wz = -WORLD_HALF + z * cell;
    float base[4];
    if (recipe) recipe->sample(wx, wz, base); else World::sampleAuthoredBase(wx, wz, base);
    v.height = base[1] < .01f ? base[0] : base[0] + base[1] * terrainFbm(wx / DETAIL_SCALE, wz / DETAIL_SCALE, 5);
    v.position = frame.toLocal(chart.position(vec3(wx, std::max(v.height, -.5f), wz)));
    v.colour = lerp(vec3(.22f, .23f, .105f), vec3(.105f, .19f, .075f), clampf(base[2], 0.f, 1.f));
    v.colour = lerp(v.colour, vec3(.31f, .29f, .25f), smoothstepf(450.f, 1600.f, v.height));
    const float snowLine = lerpf(1800.f, 500.f, clampf(base[3], 0.f, 1.f));
    v.colour = lerp(v.colour, vec3(.82f, .85f, .87f), smoothstepf(snowLine - 100.f, snowLine + 180.f, v.height));
    if (v.height < 8.f) v.colour = lerp(v.colour, vec3(.46f, .41f, .29f), 1.f - smoothstepf(0.f, 8.f, v.height));
  }
  for (int z = 0; z < resolution; ++z) for (int x = 0; x < resolution; ++x) {
    const vec3 dx = grid[size_t(z) * resolution + std::min(x + 1, resolution - 1)].position -
                    grid[size_t(z) * resolution + std::max(x - 1, 0)].position;
    const vec3 dz = grid[size_t(std::min(z + 1, resolution - 1)) * resolution + x].position -
                    grid[size_t(std::max(z - 1, 0)) * resolution + x].position;
    grid[size_t(z) * resolution + x].normal = normalize(cross(dz, dx));
  }
  mesh.surface = regionalSurface::makeSurface(mesh.region, resolution, grid, regionalSurface::Provenance::Analytic);
  auto dry = [&](int a, int b, int c) { return std::max({grid[a].height, grid[b].height, grid[c].height}) >= 0.f; };
  size_t count = 0;
  for (int z = 0; z < resolution - 1; ++z) for (int x = 0; x < resolution - 1; ++x) {
    const int i = z * resolution + x;
    count += 3 * (int(dry(i, i + resolution, i + 1)) + int(dry(i + 1, i + resolution, i + resolution + 1)));
  }
  // Exact reservation avoids capacity doubling above the advertised mesh bound.
  mesh.vertices.reserve(count);
  auto triangle = [&](int a, int b, int c) {
    if (dry(a, b, c)) { mesh.vertices.push_back(grid[a]); mesh.vertices.push_back(grid[b]); mesh.vertices.push_back(grid[c]); }
  };
  for (int z = 0; z < resolution - 1; ++z) for (int x = 0; x < resolution - 1; ++x) {
    const int i = z * resolution + x;
    triangle(i, i + resolution, i + 1); triangle(i + 1, i + resolution, i + resolution + 1);
  }
  return mesh;
}
} // namespace horizon
