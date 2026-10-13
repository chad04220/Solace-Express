#include "regional_surface.h"
#include "distant_regions.h"
#include "region_travel.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace regionalSurface {
namespace {
const double kBoundsRadius = 1.4142135623730950488 * WORLD_HALF;
struct Snapshot {
  bool enabled = false;
  procedural::Descriptor active;
  geo::Position aircraft;
  float radius = 0, clearance = 50;
  std::vector<Handle> surfaces;
};
std::shared_ptr<const Snapshot> state = std::make_shared<const Snapshot>();
std::shared_ptr<const Snapshot> read() { return std::atomic_load_explicit(&state, std::memory_order_acquire); }
void write(std::shared_ptr<Snapshot> next) {
  std::atomic_store_explicit(&state, std::shared_ptr<const Snapshot>(std::move(next)), std::memory_order_release);
}
bool finite(float x) { uint32_t bits; std::memcpy(&bits, &x, sizeof bits); return (bits & 0x7f800000u) != 0x7f800000u; }
void hashBytes(uint64_t& hash, const void* data, size_t bytes) {
  const auto* p = static_cast<const unsigned char*>(data);
  while (bytes--) { hash ^= *p++; hash *= UINT64_C(1099511628211); }
}
bool compatible(const Snapshot& s, const Handle& surface) {
  return surface && surface->key.generatorVersion == s.active.generatorVersion &&
    (surface->key.regionId == "solace" || surface->key.worldSeed == s.active.worldSeed);
}
bool protectedOverlap(const Snapshot& s, const Handle& surface) {
  return s.enabled && compatible(s, surface) &&
    geo::surfaceDistance(s.aircraft, surface->origin) <= kBoundsRadius + s.radius + 2.;
}
bool safe(const Snapshot& s, const Handle& surface) {
  if (!surface) return false;
  if (!s.enabled) return true;
  if (!compatible(s, surface)) return false;
  // A whole-chart maximum is deliberately conservative: a few contact probes
  // cannot establish absence of a sharp intervening ridge or a shifted coast.
  return !protectedOverlap(s, surface) ||
    s.aircraft.height - s.radius - s.clearance > std::max(0.f, surface->maxHeight);
}
struct Projection {
  vec3 basis[4];
  explicit Projection(const Surface& surface, geo::Position origin) {
    const geo::FrameChange change(geo::LocalFrame(surface.origin), geo::LocalFrame(origin));
    basis[0] = change.vector(vec3(1, 0, 0)); basis[1] = change.vector(vec3(0, 1, 0));
    basis[2] = change.vector(vec3(0, 0, 1)); basis[3] = change.point(vec3());
  }
  // A single kernel keeps the same vertex arithmetic independent of whether a
  // caller uses it as A/B/C or as a neighboring cell's corner under fast math.
#if defined(_MSC_VER)
  __declspec(noinline)
#elif defined(__GNUC__)
  __attribute__((noinline))
#endif
  vec3 point(const GridVertex& v) const {
    // The renderer uploads the same four float basis columns. Preserve their
    // float arithmetic rather than recovering altitude from radius subtraction.
    const vec3 q = basis[0] * v.position.x + basis[1] * v.position.y + basis[2] * v.position.z + basis[3];
    const float horizontal = std::sqrt(q.x * q.x + q.z * q.z);
    const float angle = std::atan2(horizontal, float(geo::radius()) + q.y);
    const vec2 bearing = horizontal > .001f ? vec2(q.x / horizontal, q.z / horizontal) : vec2();
    return vec3(bearing.x * angle * float(geo::radius()), std::max(v.height, -.5f),
                bearing.y * angle * float(geo::radius()));
  }
};
Sample sampleImpl(const Handle& surface, geo::Position origin, float x, float z,
                  const geo::Position* knownGeographic = nullptr) {
  Sample result;
  if (!surface || !finite(x) || !finite(z)) return result;
  const auto geographic = knownGeographic ? *knownGeographic : regionTravel::Chart(origin).position(vec3(x, 0, z));
  if (geo::surfaceDistance(geographic, surface->origin) > kBoundsRadius + 2.) return result;
  const vec3 local = regionTravel::Chart(surface->origin).local(geographic);
  const int n = surface->resolution;
  const double cell = 2. * WORLD_HALF / (n - 1);
  const int ix = int(std::floor((local.x + WORLD_HALF) / cell));
  const int iz = int(std::floor((local.z + WORLD_HALF) / cell));
  const Projection projection(*surface, origin);
  Sample edgeFallback;
  double closestEdge = std::numeric_limits<double>::infinity();
  // The inverse chart locates a nearby grid cell. Barycentrics must nevertheless
  // use projected vertices: projection is nonlinear between the grid corners.
  // Adjacent cells cover float roundoff and that sub-cell chord displacement.
  for (int j = std::max(0, iz - 1); j <= std::min(n - 2, iz + 1); ++j)
    for (int i = std::max(0, ix - 1); i <= std::min(n - 2, ix + 1); ++i) {
      const size_t k = size_t(j) * n + i;
      const size_t corners[4] = {k, k + n, k + 1, k + n + 1};
      vec3 projected[4];
      for (int corner=0;corner<4;++corner) projected[corner]=projection.point(surface->grid[corners[corner]]);
      const size_t triangles[2][3] = {{0, 1, 2}, {2, 1, 3}};
      for (const auto& triangle : triangles) {
        const auto& va = surface->grid[corners[triangle[0]]];
        const auto& vb = surface->grid[corners[triangle[1]]];
        const auto& vc = surface->grid[corners[triangle[2]]];
        const vec3 a = projected[triangle[0]], b = projected[triangle[1]], c = projected[triangle[2]];
        const double ux = double(b.x) - a.x, uz = double(b.z) - a.z;
        const double vx = double(c.x) - a.x, vz = double(c.z) - a.z;
        const double px = double(x) - a.x, pz = double(z) - a.z;
        const double determinant = ux * vz - uz * vx;
        if (std::abs(determinant) < 1e-12) continue;
        double wb = (px * vz - pz * vx) / determinant;
        double wc = (ux * pz - uz * px) / determinant;
        double wa = 1. - wb - wc;
        // Float chart coordinates have centimetre-scale ULPs far from the
        // origin. GPU arithmetic and fast-math CPU reassociation may differ by
        // a few ULPs on a shared edge. Scale coverage slack to that precision,
        // rather than opening cracks with a fixed barycentric epsilon. Reuse
        // each projected corner above so a cell's diagonal is exactly shared.
        const double scale = std::max({1.f,std::abs(a.x),std::abs(a.z),std::abs(b.x),
          std::abs(b.z),std::abs(c.x),std::abs(c.z)});
        // Per-edge barycentric gradients turn this into a true spatial bound:
        // no accepted query lies more than0.75m beyond any triangle edge. At
        // the tested1,200km separation the float-driven allowance is <0.6m.
        const double coordinateSlack = std::min(.75,4. * std::numeric_limits<float>::epsilon() * scale);
        const double inverseArea = coordinateSlack / std::abs(determinant);
        const double ea=inverseArea*std::sqrt((vx-ux)*(vx-ux)+(vz-uz)*(vz-uz));
        const double eb=inverseArea*std::sqrt(vx*vx+vz*vz), ec=inverseArea*std::sqrt(ux*ux+uz*uz);
        if (wa < -ea || wb < -eb || wc < -ec) continue;
        const bool inside=wa>=0. && wb>=0. && wc>=0.;
        const double edgeDistance=inside?0.:coordinateSlack*std::max({0.,-wa/ea,-wb/eb,-wc/ec});
        if (!inside && edgeDistance>=closestEdge) continue;
        // Coverage tolerance must not extrapolate beyond the actual triangle's
        // corner heights or manufacture a new peak at an edge.
        wa=std::max(0.,wa); wb=std::max(0.,wb); wc=std::max(0.,wc);
        const double total=wa+wb+wc; wa/=total; wb/=total; wc/=total;
        Sample candidate; candidate.covered = true; candidate.key = surface->key;
        // Same omission and clamped vertex interpolation as the vertex/fragment
        // shader. In particular, interpolating raw seabed heights is incorrect.
        const float height = float(wa * a.y + wb * b.y + wc * c.y);
        if (std::max({va.height, vb.height, vc.height}) >= 0.f && height >= 0.f) {
          candidate.land = true; candidate.height = height;
          candidate.normal = normalize(cross(b - a, c - a));
          if (candidate.normal.y < 0) candidate.normal = -candidate.normal;
        }
        if (inside) return candidate;
        // Never flatten the interior of the actual adjacent face just because
        // an earlier face accepts it within numerical edge slack. Use the
        // nearest clamped edge only after every nearby exact face is ruled out.
        closestEdge=edgeDistance; edgeFallback=std::move(candidate);
      }
    }
  return edgeFallback;
}
} // namespace

bool Key::operator==(const Key& other) const {
  return regionId == other.regionId && worldSeed == other.worldSeed && generatorVersion == other.generatorVersion &&
    contentHash == other.contentHash && provenance == other.provenance;
}
Handle makeSurface(const procedural::Descriptor& region, int resolution,
                   const std::vector<horizon::Vertex>& grid, Provenance provenance) {
  std::vector<GridVertex> compact; compact.reserve(grid.size());
  for (const auto& v : grid) compact.push_back({v.position, v.height});
  return makeSurface(region, resolution, std::move(compact), provenance);
}
Handle makeSurface(const procedural::Descriptor& region, int resolution,
                   std::vector<GridVertex>&& grid, Provenance provenance) {
  if (resolution < 2 || resolution > horizon::kMeshResolution || grid.size() != size_t(resolution) * resolution)
    throw std::invalid_argument("invalid regional collision grid");
  const auto canonical = procedural::find(region.id, region.worldSeed);
  if (!canonical || canonical->generatorVersion != region.generatorVersion || canonical->seed != region.seed ||
      geo::surfaceDistance(canonical->origin, region.origin) > .001 || region.origin.height != 0)
    throw std::invalid_argument("noncanonical regional collision identity");
  auto out = std::make_shared<Surface>();
  out->origin = canonical->origin; out->direction = geo::direction(out->origin); out->resolution = resolution;
  out->key.regionId = region.id; out->key.worldSeed = region.worldSeed;
  out->key.generatorVersion = region.generatorVersion; out->key.provenance = provenance;
  out->key.contentHash = UINT64_C(1469598103934665603);
  for (const auto& v : grid) {
    if (!finite(v.height) || !finite(v.position.x) || !finite(v.position.y) || !finite(v.position.z))
      throw std::invalid_argument("nonfinite regional collision vertex");
    out->maxHeight = std::max(out->maxHeight, v.height);
    hashBytes(out->key.contentHash, &v, sizeof v);
  }
  hashBytes(out->key.contentHash, &resolution, sizeof resolution);
  out->grid = std::move(grid);
  return out;
}
vec3 projectVertex(const Surface& surface, geo::Position activeOrigin, const GridVertex& vertex) {
  return Projection(surface, activeOrigin).point(vertex);
}
Sample sample(const Handle& surface, geo::Position activeOrigin, float x, float z) {
  return sampleImpl(surface, activeOrigin, x, z);
}
void publishDrawableSurfaces(std::vector<Handle> surfaces) {
  if (surfaces.size() > horizon::kMaxRegions) throw std::invalid_argument("regional collision snapshot exceeds64 surfaces");
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (!surfaces[i]) throw std::invalid_argument("empty drawable regional collision surface");
    for (size_t j = 0; j < i; ++j)
      if (surfaces[i]->key.regionId == surfaces[j]->key.regionId && surfaces[i]->key.worldSeed == surfaces[j]->key.worldSeed)
        throw std::invalid_argument("two drawable collision versions for one region");
  }
  auto next = std::make_shared<Snapshot>(*read()); next->surfaces = std::move(surfaces); write(std::move(next));
}
void setFlightContext(const procedural::Descriptor& activeRegion, geo::Position aircraft, float radius, float clearance) {
  (void)geo::direction(aircraft); (void)geo::direction(activeRegion.origin);
  if (!finite(radius) || !finite(clearance) || radius < 0 || clearance < 0)
    throw std::invalid_argument("invalid regional collision protection");
  const auto canonical = procedural::find(activeRegion.id, activeRegion.worldSeed);
  if (!canonical || canonical->generatorVersion != activeRegion.generatorVersion || canonical->seed != activeRegion.seed ||
      geo::surfaceDistance(canonical->origin, activeRegion.origin) > .001)
    throw std::invalid_argument("invalid active regional collision identity");
  auto next = std::make_shared<Snapshot>(*read());
  next->enabled = true; next->active = *canonical; next->aircraft = aircraft;
  next->radius = radius; next->clearance = clearance; write(std::move(next));
}
void clearFlightContext() { auto next = std::make_shared<Snapshot>(); write(std::move(next)); }
bool flightContextEnabled() { return read()->enabled; }
bool appliesTo(const World& world) {
  if (&world != &g_world || world.repeating) return false;
  const auto s = read();
  if (!s->enabled || world.regionId != s->active.id) return false;
  if (!world.regionRecipe) {
    const auto origin = geo::solaceOrigin();
    return world.regionId == "solace" && s->active.generatorVersion == procedural::kGeneratorVersion &&
      origin.latitude == s->active.origin.latitude && origin.longitude == s->active.origin.longitude &&
      origin.height == s->active.origin.height;
  }
  const auto& descriptor = world.regionRecipe->descriptor;
  return descriptor.generatorVersion == s->active.generatorVersion && descriptor.worldSeed == s->active.worldSeed &&
    descriptor.origin.latitude == s->active.origin.latitude && descriptor.origin.longitude == s->active.origin.longitude &&
    descriptor.origin.height == s->active.origin.height;
}
Sample queryCurrent(float x, float z) {
  const auto s = read(); Sample result;
  if (!s->enabled || !finite(x) || !finite(z)) return result;
  const auto geographic = regionTravel::Chart(s->active.origin).position(vec3(x, 0, z));
  const auto direction = geo::direction(geographic);
  const double cutoff = std::cos((kBoundsRadius + 2.) / geo::radius());
  for (const auto& surface : s->surfaces) {
    if (!compatible(*s, surface) || surface->key.regionId == s->active.id) continue;
    if (geo::dot(direction, surface->direction) < cutoff) continue;
    auto candidate = sampleImpl(surface, s->active.origin, x, z, &geographic);
    if (candidate.covered) return candidate;
  }
  return result;
}
float currentUpperBound(vec3 a, vec3 b, float radius) {
  const auto s = read(); float top = 0;
  if (!s->enabled) return top;
  // The spherical exponential map is non-expansive within the chart domain:
  // this geographic ball conservatively encloses the entire local XZ capsule.
  // It also covers a supplied AABB diagonal, as used by publication guards.
  const vec3 middle(float((double(a.x) + b.x) * .5), 0, float((double(a.z) + b.z) * .5));
  const double reach = std::hypot(double(b.x) - a.x, double(b.z) - a.z) * .5 + std::max(0.f, radius);
  const auto center = regionTravel::Chart(s->active.origin).position(middle);
  for (const auto& surface : s->surfaces)
    if (compatible(*s, surface) && surface->key.regionId != s->active.id &&
        geo::surfaceDistance(center, surface->origin) <= reach + kBoundsRadius + 2.)
      top = std::max(top, surface->maxHeight);
  return top;
}
bool isPinned(const Handle& surface) { const auto s = read(); return protectedOverlap(*s, surface) && !safe(*s, surface); }
bool regionPinned(const std::string& regionId) {
  const auto s = read();
  for (const auto& surface : s->surfaces)
    if (surface->key.regionId == regionId && protectedOverlap(*s, surface) && !safe(*s, surface)) return true;
  return false;
}
bool mayActivate(const Handle& surface) { return safe(*read(), surface); }
bool mayReplace(const Handle& previous, const Handle& replacement) {
  if (!replacement) return false;
  if (previous && previous->key == replacement->key) return true;
  const auto s = read();
  return !(protectedOverlap(*s, previous) && !safe(*s, previous)) && safe(*s, replacement);
}
size_t residentSurfaceCount() { return read()->surfaces.size(); }
size_t residentGridBytes() {
  size_t result = 0; for (const auto& surface : read()->surfaces) result += surface->grid.capacity() * sizeof(GridVertex); return result;
}
} // namespace regionalSurface
