// CPU-only distant scenery. No World heightmaps, roads, entity caches or GL state
// are built here; a caller can keep one or two detailed Worlds independently.
#pragma once
#include "procedural_regions.h"
#include "regional_surface.h"

namespace horizon {
constexpr size_t kMaxRegions = 64;
constexpr int kMeshResolution = 129;
constexpr size_t kMaxMeshVertices = size_t(kMeshResolution - 1) * (kMeshResolution - 1) * 6;
constexpr double kAltitudeLookahead = 3000.0;
constexpr double kSummitHeight = 3000.0;

struct Vertex { vec3 position, normal, colour; float height; };
struct Mesh {
  procedural::Descriptor region;
  regionalSurface::Handle surface;
  // ECEF-relative position and normal in region.origin's east/up/south frame,
  // matching Renderer::prepareFarTerrain. Fully submerged triangles are omitted.
  std::vector<Vertex> vertices;
};
struct Selection {
  std::vector<procedural::Descriptor> regions;
  bool complete = true; // false when a requested/hard cap omitted any candidate
  double reachMetres = 0; // greatest conservative center reach, including Solace
};

// All directions, before FOV/fog culling. Horizons include altitudeLookahead and
// the terrain summit, then the square footprint's diagonal, not its half-width.
// The immutable center-only catalog is < 320 KiB; result storage is capped at64.
// Summit values below 3 km are raised to that conservative generator bound.
// Invalid positions/nonfinite or negative options throw std::invalid_argument.
// complete is coverage of this conservative horizon, never a full-planet claim.
Selection select(geo::Position camera, uint64_t worldSeed = procedural::kDefaultWorldSeed,
                 size_t maxCount = kMaxRegions, double altitudeLookahead = kAltitudeLookahead,
                 double summitHeight = kSummitHeight);

// An approximate silhouette, using the pure recipe/raw authored base and five
// terrain-noise octaves. It deliberately omits full-World smoothing/road grading.
// Resolution must be in [2,129]. Maximum expanded output is 3.75 MiB, with one
// additional <= 651 KiB grid while building and a retained <= 261 KiB exact
// collision grid per mesh. Noncanonical descriptors throw.
Mesh buildMesh(const procedural::Descriptor& region, int resolution = kMeshResolution);
inline Mesh build(const procedural::Descriptor& region, int resolution = kMeshResolution) {
  return buildMesh(region, resolution);
}
} // namespace horizon
