// Somnium Volandi - the real Earth's far terrain (docs/REAL_EARTH_PLAN.md section 4): beyond a chart, out to the
// horizon, patches of ground drawn from the Earth pack and never from a World. A patch is a cell of a degree lattice -
// half a degree a side at its finest, doubling four times to 8 - chosen as a quadtree round the viewer (finer nearer),
// each a 65 x 65 grid on the parallels and meridians from ETOPO's heights and coloured by the land cover. They ride
// the generated regions' far pass (distant_regions.h, Renderer::updateRegionHorizon): a patch is a
// procedural::Descriptor with an "earthfar:" id, its mesh a horizon::Mesh, its collision a geographic
// regionalSurface. Neighbours of one level share their edges exactly; skirts hide the cracks between levels.
#pragma once
#include "distant_regions.h"
#include "earth_data.h"
#include <optional>

namespace earthFar {
constexpr int kLevels = 5;                 // half a degree a side, doubling to 8 degrees (45 of them round the Earth)
constexpr double kFinestDegrees = 0.5;
constexpr int kResolution = 64;            // cells a side (65 x 65 vertices)
constexpr double kSplit = 0.4;            // a patch nearer than this many of its own sides is drawn as its four
constexpr const char* kIdPrefix = "earthfar:";

inline double patchDegrees(int level) { return kFinestDegrees * double(1 << level); }
bool isFarId(const std::string& id);
// The patch's descriptor (its origin the cell's centre), from a canonical id ("earthfar:<level>:<i>:<j>", i the
// cell's row of the lattice north from 90 S, j its column east from 180 W)
std::optional<procedural::Descriptor> find(const std::string& id);
procedural::Descriptor patch(int level, int i, int j);

// The patches to draw round a viewer: the lattice's cells within the horizon (the viewer's and a 3 km summit's) over
// the pack's box, finer nearer, none wholly inside the chart square round chartOrigin (WORLD_HALF each way: the chart
// is drawn there), nearest first and at most maxCount (complete false when more were wanted). The Solace Islands come
// first when they are in sight and the chart isn't theirs: their own silhouette (horizon::build), not a patch
horizon::Selection select(geo::Position viewer, const geo::Position& chartOrigin, const earth::Box& data,
                          size_t maxCount);
// A patch's mesh from the pack: its vertices in the frame of its origin, as the far pass draws them; its collision
// surface the same grid
horizon::Mesh build(const procedural::Descriptor& patch, const std::shared_ptr<const earth::Pack>& pack);
} // namespace earthFar
