// Immutable collision counterparts of drawable Earth-region silhouette meshes.
// No World construction, GL calls, career data, or on-demand terrain generation.
#pragma once
#include "geo.h"
#include "procedural_regions.h"
#include <memory>

namespace horizon { struct Vertex; }
namespace regionalSurface {
enum class Provenance : uint8_t { Analytic, FullDerived };
struct Key {
  std::string regionId;
  uint64_t worldSeed = 0, contentHash = 0;
  uint32_t generatorVersion = 0;
  Provenance provenance = Provenance::Analytic;
  bool operator==(const Key& other) const;
  bool operator!=(const Key& other) const { return !(*this == other); }
};
// Retain precisely the floats supplied to the far vertex shader. The position
// already uses the clamped altitude; raw height controls triangle omission.
struct GridVertex { vec3 position; float height = 0; };
static_assert(sizeof(GridVertex) == 16, "regional collision grid must remain compact");
struct Surface {
  Key key;
  geo::Position origin;
  geo::Double3 direction;
  int resolution = 0;
  std::vector<GridVertex> grid;
  float maxHeight = 0;
};
using Handle = std::shared_ptr<const Surface>;
Handle makeSurface(const procedural::Descriptor& region, int resolution,
                   const std::vector<horizon::Vertex>& grid, Provenance provenance);
Handle makeSurface(const procedural::Descriptor& region, int resolution,
                   std::vector<GridVertex>&& grid, Provenance provenance);
struct Sample {
  bool covered = false, land = false;
  float height = -SEA_DEPTH;
  vec3 normal = vec3(0, 1, 0);
  Key key;
};
// Projection matches far_region_vs.glsl BEFORE the camera-relative visual bend.
// Physics remains in the game's existing level surface chart.
vec3 projectVertex(const Surface& surface, geo::Position activeOrigin, const GridVertex& vertex);
Sample sample(const Handle& surface, geo::Position activeOrigin, float x, float z);

// Main-thread publication only. Readers acquire immutable snapshots. Publishing
// a handle means its corresponding GPU representation is accepted and drawable.
// The caller must apply mayActivate/mayReplace before exposing a new version.
void publishDrawableSurfaces(std::vector<Handle> surfaces);
void setFlightContext(const procedural::Descriptor& activeRegion, geo::Position protectedAircraft,
                      float protectedRadius, float clearance = 50.f);
void clearFlightContext();
bool flightContextEnabled();
bool appliesTo(const World& world);
Sample queryCurrent(float x, float z);
// Conservative bound for foreign terrain sweeps. Zero with no enabled context.
float currentUpperBound(vec3 a, vec3 b, float radius);
bool isPinned(const Handle& surface);
bool regionPinned(const std::string& regionId);
bool mayActivate(const Handle& surface);
bool mayReplace(const Handle& previous, const Handle& replacement);
size_t residentSurfaceCount();
size_t residentGridBytes();
} // namespace regionalSurface
