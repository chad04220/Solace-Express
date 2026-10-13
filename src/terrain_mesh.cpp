// Somnium Volandi - the terrain mesh and the sea for the raster renderer (renderer.h). A quadtree of 32 x 32-quad
// chunks is picked around the camera every frame: a chunk is split while the camera is within kSplit chunk sizes of
// it, from a root chunk covering the world (2500 m cells) down to 0.3 m cells right under the camera - and along the
// graded roads further still, until its cells are no wider than half the road's platform or a small fraction of the
// distance (the road's level bed and its banks drawn as they are, not a cell's slope across them). The vertex
// shader (kTerrainVS) sets every vertex on the heightfield and morphs chunk edges onto the next coarser level, so
// the levels meet without cracks; skirts hang from every chunk's edges where a road's detail meets the plain levels
// beside it. The fragment shader writes the ground material into the G-buffer. The sea is a radial grid at y = 0
// that rides along with the camera.
#include "renderer.h"
#include "shaders.h"
#include "procedural_regions.h"
#include "region_travel.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace {
const float kSplit = 2.6f;          // split while the camera is nearer than this many chunk sizes
const int kMinLevel = -7;           // finest chunk: 0.305 m cells, 9.8 m across
const float kTerrainFar = 90000.f;  // the flat world's reach (planet.h radius 0): a copy of the islands beyond it is not visited
const int kChunkVerts = TP_CHUNK * TP_CHUNK * 6 + 4 * TP_CHUNK * 6;   // (the grid's cells, then the skirts' quads)
// a chunk wholly past its morph - every vertex the coarser grid lacks slid onto it (kTerrainVS) - drawn on that grid
// alone: the same surface from a quarter of the triangles (the far copies of the islands on the round world)
const int kChunkVertsCoarse = kChunkVerts / 4;
// along the graded roads: cells no wider than half the narrowest road's platform there, or than this much of the
// distance out to 1.2 km (as fine as the plain levels a sixth of the way), the allowance growing with the distance
// beyond - by 2.4 km no finer than the plain levels (further off a road's bed is a line, its paint the pixel's own)
const float kRoadDetail = 0.005f, kRoadDetailNear = 1200.f;
float roadCellAllowed(float d) { return kRoadDetail * d * std::max(1.f, d / kRoadDetailNear); }
// the distance at which a cell of size cs is no longer finer than allowed (its inverse)
float roadCellDistance(float cs) {
  const float x = cs / kRoadDetail;
  return x <= kRoadDetailNear ? x : sqrtf(x * kRoadDetailNear);
}
}

bool Renderer::compileTerrainMesh(const std::function<void()>& step) {
  std::string e;
  setCompileStage("terrain surface");
  progTerrain = linkProgramCached(terrainVSAssembly(""), terrainFSAssembly(""), e);
  if (!progTerrain) { error = "Terrain shader: " + e; return false; }
  if (step) step();
  setCompileStage("ocean surface");
  progWater = linkProgramCached(waterVSAssembly(""), waterFSAssembly(""), e);
  if (!progWater) { error = "Water shader: " + e; return false; }
  if (step) step();
  setCompileStage("distant regional terrain");
  progFarRegion = linkProgramCached(std::string("#version 330 core\n#define ROUND_WORLD\n") + kPlanet + kFarRegionVS,
    std::string("#version 330 core\n") + kFarRegionFS, e);
  if (!progFarRegion) { error = "Distant terrain shader: " + e; return false; }
  if (step) step();
  return true;
}

void Renderer::initTerrainMesh() {
  glGenVertexArrays(1, &vaoTerrain); glBindVertexArray(vaoTerrain);
  glGenBuffers(1, &vboTerrainInst); glBindBuffer(GL_ARRAY_BUFFER, vboTerrainInst);
  glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float) * 4, nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0); glVertexAttribDivisor(0, 1);
  // the chunk's grid as indexed triangles: 33 x 33 vertices shared by the 2048 triangles (the same six corners per
  // cell, in the same order and winding, as the plain vertex list had), so the vertex shader's height and normal
  // samples run once per grid point instead of once per triangle corner (5.6x fewer: Codex's performance review)
  {
    std::vector<uint16_t> idx; idx.reserve(kChunkVerts);
    for (int cell = 0; cell < TP_CHUNK * TP_CHUNK; cell++) for (int k = 0; k < 6; k++) {
      int ox = (k == 2 || k == 3 || k == 5) ? 1 : 0, oy = (k == 1 || k == 4 || k == 5) ? 1 : 0;
      int gx = cell % TP_CHUNK + ox, gy = cell / TP_CHUNK + oy;
      idx.push_back((uint16_t)(gy * (TP_CHUNK + 1) + gx));
    }
    // the skirts: a quad down from each edge's every cell, its lower corners the vertices numbered after the grid's
    // (33 x 33 + edge x 33 + k: the edge's k-th vertex, dropped - kTerrainVS)
    const int NG = (TP_CHUNK + 1) * (TP_CHUNK + 1);
    auto edgeVertex = [&](int e, int k) { return e == 0 ? k : e == 1 ? k * (TP_CHUNK + 1) + TP_CHUNK : e == 2 ? TP_CHUNK * (TP_CHUNK + 1) + k : k * (TP_CHUNK + 1); };
    for (int e = 0; e < 4; e++)
      for (int k = 0; k < TP_CHUNK; k++) {
        const uint16_t a = (uint16_t)edgeVertex(e, k), b = (uint16_t)edgeVertex(e, k + 1);
        const uint16_t c = (uint16_t)(NG + e * (TP_CHUNK + 1) + k + 1), d = (uint16_t)(NG + e * (TP_CHUNK + 1) + k);
        for (uint16_t v : {a, b, c, a, c, d}) idx.push_back(v);
      }
    glGenBuffers(1, &iboTerrain); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
    // the coarse grid: the even grid points' cells and skirts, in the same order and winding
    std::vector<uint16_t> co; co.reserve(kChunkVertsCoarse);
    const int H = TP_CHUNK / 2;
    for (int cell = 0; cell < H * H; cell++) for (int k = 0; k < 6; k++) {
      int ox = (k == 2 || k == 3 || k == 5) ? 1 : 0, oy = (k == 1 || k == 4 || k == 5) ? 1 : 0;
      int gx = 2 * (cell % H + ox), gy = 2 * (cell / H + oy);
      co.push_back((uint16_t)(gy * (TP_CHUNK + 1) + gx));
    }
    for (int e = 0; e < 4; e++)
      for (int k = 0; k < TP_CHUNK; k += 2) {
        const uint16_t a = (uint16_t)edgeVertex(e, k), b = (uint16_t)edgeVertex(e, k + 2);
        const uint16_t c = (uint16_t)(NG + e * (TP_CHUNK + 1) + k + 2), d = (uint16_t)(NG + e * (TP_CHUNK + 1) + k);
        for (uint16_t v : {a, b, c, a, c, d}) co.push_back(v);
      }
    glGenBuffers(1, &iboTerrainCoarse); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrainCoarse);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, co.size() * sizeof(uint16_t), co.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain);   // (the vertex array's own)
  }
  glBindVertexArray(0);
  buildRoadLod();
  reloadFarTerrain(g_world);
  // the sea grid: rings from 2 m out to 300 km - on the round world (planet.h) a quarter of the way round it, past the
  // horizon from any height - each 6% wider, 192 segments; the first ring closes on a centre vertex.
  // (Fine because depth is logarithmic per vertex and only interpolated between: across a triangle 15% deep it was off
  // by 0.3% of the distance, a metre at 300 m - enough for the sea floor, held just under the sea (terrain_vs), to show
  // through it in ovals. At 6% it's 0.05%)
  const float seaFar = std::max(300000.f, planetRadius() * 0.5f * PI * 1.01f);
  const int R = (int)ceilf(logf(seaFar / 2.f) / logf(1.06f)) + 1, SEG = 192;
  std::vector<float> v; std::vector<unsigned> idx;
  v.push_back(0); v.push_back(0);
  for (int r = 0; r < R; r++) {
    float rad = 2.f * powf(1.06f, (float)r);
    for (int s = 0; s < SEG; s++) { float a = 2.f * PI * s / SEG; v.push_back(rad * cosf(a)); v.push_back(rad * sinf(a)); }
  }
  auto at = [&](int r, int s) { return 1u + (unsigned)r * SEG + (unsigned)((s + SEG) % SEG); };
  for (int s = 0; s < SEG; s++) { idx.push_back(0); idx.push_back(at(0, s)); idx.push_back(at(0, s + 1)); }
  for (int r = 0; r + 1 < R; r++)
    for (int s = 0; s < SEG; s++) {
      idx.push_back(at(r, s)); idx.push_back(at(r + 1, s)); idx.push_back(at(r + 1, s + 1));
      idx.push_back(at(r, s)); idx.push_back(at(r + 1, s + 1)); idx.push_back(at(r, s + 1));
    }
  waterIdx = (int)idx.size();
  glGenVertexArrays(1, &vaoWater); glBindVertexArray(vaoWater);
  glGenBuffers(1, &vboWater); glBindBuffer(GL_ARRAY_BUFFER, vboWater);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
  glGenBuffers(1, &iboWater); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
}

// Link/compile alone can defer a software driver's raster specialization until the first visible island.
// Warm exactly this G-buffer/depth/attribute configuration during initial loading, never during flight staging.
void Renderer::warmFarTerrain() {
  if (!progFarRegion || !activeFarRegion.vao || !fboGB) return;
  FrameParams fp;
  fp.camPos = vec3(0, 160000.f, 0); fp.camRight = vec3(1, 0, 0); fp.camUp = vec3(0, 0, -1); fp.camBack = vec3(0, 1, 0);
  fp.fovY = 1.f;
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  const GLenum targets[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, targets); glViewport(0, 0, std::min(W, 64), std::min(H, 64));
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glClearColor(0, 0, 0, 0); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  drawFarRegions(fp, true); glFinish();
  glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H); glDisable(GL_DEPTH_TEST);
  distantRegionDraws = 0;
}

// Lightweight geography survives full region retirement. At this resolution a whole chart is at most 3.75 MiB;
// fully underwater triangles are omitted. It is prepared only with the candidate's own immutable World.
void Renderer::prepareFarTerrain(const World& world, WorldUploadData& data) {
  generationCheckpoint();
  data.origin = world.regionRecipe ? world.regionRecipe->descriptor.origin : geo::solaceOrigin();
  data.origin.height = 0;
  data.regionId = world.regionId;
  data.worldSeed = world.regionRecipe ? world.regionRecipe->descriptor.worldSeed : procedural::kDefaultWorldSeed;
  data.generatorVersion = world.regionRecipe ? world.regionRecipe->descriptor.generatorVersion : procedural::kGeneratorVersion;
  constexpr int N = 129;
  const float cell = 2.f * WORLD_HALF / (N - 1);
  const regionTravel::Chart chart(data.origin);
  const geo::LocalFrame frame(data.origin);
  std::vector<FarTerrainVertex> grid(size_t(N) * N);
  for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
    if (x == 0) generationCheckpoint();
    auto& v = grid[size_t(z) * N + x];
    const float wx = -WORLD_HALF + x * cell, wz = -WORLD_HALF + z * cell;
    v.height = world.height(wx, wz, 5);
    v.position = frame.toLocal(chart.position(vec3(wx, std::max(v.height, -.5f), wz)));
    float base[4]; world.sampleBase(wx, wz, base);
    v.colour = lerp(vec3(.22f, .23f, .105f), vec3(.105f, .19f, .075f), clampf(base[2], 0.f, 1.f));
    v.colour = lerp(v.colour, vec3(.31f, .29f, .25f), smoothstepf(450.f, 1600.f, v.height));
    const float snowLine = lerpf(1800.f, 500.f, clampf(base[3], 0.f, 1.f));
    v.colour = lerp(v.colour, vec3(.82f, .85f, .87f), smoothstepf(snowLine - 100.f, snowLine + 180.f, v.height));
    if (v.height < 8.f) v.colour = lerp(v.colour, vec3(.46f, .41f, .29f), 1.f - smoothstepf(0.f, 8.f, v.height));
  }
  for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
    if (x == 0) generationCheckpoint();
    const vec3 dx = grid[size_t(z) * N + std::min(x + 1, N - 1)].position - grid[size_t(z) * N + std::max(x - 1, 0)].position;
    const vec3 dz = grid[size_t(std::min(z + 1, N - 1)) * N + x].position - grid[size_t(std::max(z - 1, 0)) * N + x].position;
    grid[size_t(z) * N + x].normal = normalize(cross(dz, dx));
  }
  const auto descriptor = world.regionRecipe ? world.regionRecipe->descriptor : procedural::solaceDescriptor(data.worldSeed);
  data.surface = regionalSurface::makeSurface(descriptor, N, grid, regionalSurface::Provenance::FullDerived);
  data.farVertices.clear();
  auto tri = [&](int a, int b, int c) {
    if (std::max({grid[a].height, grid[b].height, grid[c].height}) < 0.f) return;
    data.farVertices.push_back(grid[a]); data.farVertices.push_back(grid[b]); data.farVertices.push_back(grid[c]);
  };
  for (int z = 0; z < N - 1; ++z) for (int x = 0; x < N - 1; ++x) {
    const int i = z * N + x;
    tri(i, i + N, i + 1); tri(i + 1, i + N, i + N + 1);
  }
}

void Renderer::destroyFarRegion(FarRegionGpu& region) {
  if (region.vao) glDeleteVertexArrays(1, &region.vao);
  if (region.vbo) glDeleteBuffers(1, &region.vbo);
  region = FarRegionGpu{};
}

void Renderer::allocateFarRegion(FarRegionGpu& region) {
  if (region.count <= 0) { region.ready = true; return; }
  glGenVertexArrays(1, &region.vao); glGenBuffers(1, &region.vbo);
  glBindVertexArray(region.vao); glBindBuffer(GL_ARRAY_BUFFER, region.vbo);
  glBufferData(GL_ARRAY_BUFFER, size_t(region.count) * sizeof(FarTerrainVertex), nullptr, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FarTerrainVertex), (void*)offsetof(FarTerrainVertex, position));
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FarTerrainVertex), (void*)offsetof(FarTerrainVertex, normal));
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(FarTerrainVertex), (void*)offsetof(FarTerrainVertex, colour));
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(FarTerrainVertex), (void*)offsetof(FarTerrainVertex, height));
  glBindVertexArray(0);
}

void Renderer::reloadFarTerrain(const World& world) {
  // Only initial/menu loads use this synchronous path. In flight it is staged by pumpWorldUpload.
  destroyFarRegion(activeFarRegion); destroyFarRegion(previousFarRegion);
  WorldUploadData data; prepareFarTerrain(world, data);
  activeFarRegion.origin = data.origin; activeFarRegion.regionId = std::move(data.regionId);
  activeFarRegion.worldSeed = data.worldSeed; activeFarRegion.generatorVersion = data.generatorVersion;
  activeFarRegion.surface = std::move(data.surface);
  activeFarRegion.count = int(data.farVertices.size());
  allocateFarRegion(activeFarRegion);
  if (activeFarRegion.count) {
    glBindBuffer(GL_ARRAY_BUFFER, activeFarRegion.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, data.farVertices.size() * sizeof(FarTerrainVertex), data.farVertices.data());
    activeFarRegion.uploaded = data.farVertices.size() * sizeof(FarTerrainVertex);
  }
  activeFarRegion.ready = activeFarRegion.accepted = true;
  publishFarSurfaces();
}

namespace {
std::string horizonKey(const std::string& id, uint64_t seed, uint32_t version = procedural::kGeneratorVersion) {
  return std::to_string(seed) + ":" + std::to_string(version) + ":" + id;
}
}

void Renderer::beginRegionUploadFrame() {
  worldUploadBytesThisFrame = horizonUploadBytesThisFrame = 0;
  worldUploadMsThisFrame = horizonUploadMsThisFrame = 0;
}

bool Renderer::horizonPinned(const std::string& id, bool requireReady) const {
  // The actually published detailed World stays authoritative even while a different seed is being prepared.
  if (!g_world.repeating && id == g_world.regionId) return true;
  auto matches = [&](const FarRegionGpu& mesh) {
    return mesh.regionId == id && regionIdentityCompatible(id, mesh.worldSeed, mesh.generatorVersion, horizonSeed) &&
      (!requireReady || (mesh.ready && mesh.accepted));
  };
  return matches(activeFarRegion) || (worldStage.active && matches(worldStage.farProxy)) || matches(previousFarRegion);
}

bool Renderer::reserveHorizonSlot(const std::string& key, bool preservePinned) {
  if (horizonCache.count(key) || horizonCache.size() < kHorizonCacheLimit) return true;
  // Keep the current selected neighborhood. Spare entries are a bounded reuse cache for turns and reversals.
  auto victim = horizonCache.end(); double farthest = -1;
  for (auto it = horizonCache.begin(); it != horizonCache.end(); ++it) {
    if (it->second.accepted && regionalSurface::isPinned(it->second.surface)) continue;
    const bool wanted = std::any_of(horizonSelection.regions.begin(), horizonSelection.regions.end(),
      [&](const procedural::Descriptor& d) { return horizonKey(d.id, d.worldSeed, d.generatorVersion) == it->first; });
    if (wanted && !horizonPinned(it->second.regionId, true)) continue;
    const double distance = geo::surfaceDistance(horizonViewer, it->second.origin);
    if (distance > farthest) { victim = it; farthest = distance; }
  }
  if (victim == horizonCache.end() && preservePinned) {
    // A contact representation must survive priority changes, even if the conservative camera selection is full.
    for (auto it = horizonCache.begin(); it != horizonCache.end(); ++it) {
      if (it->second.accepted && regionalSurface::isPinned(it->second.surface)) continue;
      const double distance = geo::surfaceDistance(horizonViewer, it->second.origin);
      if (distance > farthest) { victim = it; farthest = distance; }
    }
  }
  if (victim == horizonCache.end()) return false;
  destroyFarRegion(victim->second); horizonCache.erase(victim); return true;
}

regionalSurface::Handle Renderer::drawableRegionSurface(const std::string& regionId) const {
  auto usable = [&](const FarRegionGpu& mesh) {
    return mesh.regionId == regionId && mesh.ready && mesh.accepted && mesh.vao && mesh.count > 0;
  };
  if (usable(activeFarRegion)) return activeFarRegion.surface;
  if (worldStage.active && usable(worldStage.farProxy)) return worldStage.farProxy.surface;
  if (usable(previousFarRegion)) return previousFarRegion.surface;
  for (const auto& entry : horizonCache) if (usable(entry.second)) return entry.second.surface;
  return {};
}

// One immutable CPU surface is published for each accepted GPU representation. GPU completion alone does not
// reveal terrain under a low aircraft: an existing contact representation wins until the clearance gate opens.
void Renderer::publishFarSurfaces() {
  std::vector<FarRegionGpu*> meshes;
  auto append = [&](FarRegionGpu& mesh) { if (mesh.ready && mesh.surface) meshes.push_back(&mesh); };
  append(activeFarRegion); if (worldStage.active) append(worldStage.farProxy); append(previousFarRegion);
  for (auto& entry : horizonCache) append(entry.second);
  const uint64_t seed = horizonContext ? horizonSeed :
    (g_world.regionRecipe ? g_world.regionRecipe->descriptor.worldSeed : procedural::kDefaultWorldSeed);
  auto compatible = [&](const FarRegionGpu& mesh) {
    return (&mesh == &activeFarRegion && mesh.regionId == g_world.regionId) ||
      regionIdentityCompatible(mesh.regionId, mesh.worldSeed, mesh.generatorVersion, seed) ||
      (mesh.accepted && regionalSurface::isPinned(mesh.surface));
  };
  std::vector<regionalSurface::Handle> selected; selected.reserve(meshes.size());
  std::vector<FarRegionGpu*> accepted; accepted.reserve(meshes.size());
  std::unordered_set<std::string> chosen;
  for (auto* mesh : meshes) {
    if (chosen.count(mesh->regionId) || !compatible(*mesh)) continue;
    if (regionalSurface::flightContextEnabled() && !regionalSurface::mayActivate(mesh->surface) &&
        !regionalSurface::isPinned(mesh->surface)) continue;
    FarRegionGpu* previous = nullptr;
    for (auto* prior : meshes) if (prior->accepted && prior->regionId == mesh->regionId && compatible(*prior)) {
      previous = prior; if (regionalSurface::isPinned(prior->surface)) break;
    }
    FarRegionGpu* use = nullptr;
    if (previous && regionalSurface::isPinned(previous->surface) && previous->surface->key != mesh->surface->key) use = previous;
    else if (mesh->accepted || (previous ? regionalSurface::mayReplace(previous->surface, mesh->surface) :
                               regionalSurface::mayActivate(mesh->surface))) use = mesh;
    else if (previous) use = previous;
    if (!use) continue;
    for (auto* other : meshes) if (other->regionId == use->regionId) other->accepted = other == use;
    use->accepted = true; chosen.insert(use->regionId); selected.push_back(use->surface); accepted.push_back(use);
  }
  for (auto* mesh : meshes) mesh->accepted = std::find(accepted.begin(), accepted.end(), mesh) != accepted.end();
  regionalSurface::publishDrawableSurfaces(std::move(selected));
}

void Renderer::refreshHorizonStats() {
  horizonWantedMeshes = int(horizonSelection.regions.size()); horizonReadyMeshes = 0;
  horizonSelectionComplete = horizonHasSelection && horizonSelection.complete;
  for (const auto& d : horizonSelection.regions) {
    auto cached = horizonCache.find(horizonKey(d.id, d.worldSeed, d.generatorVersion));
    if (horizonPinned(d.id, true) || (cached != horizonCache.end() && cached->second.ready && cached->second.accepted)) ++horizonReadyMeshes;
  }
  horizonCacheBytes = 0; horizonResidentMeshes = 0; horizonCpuGridBytes = horizonPendingGridBytes = 0; horizonPinnedMeshes = 0;
  std::unordered_set<const regionalSurface::Surface*> grids;
  auto count = [&](const FarRegionGpu& mesh) {
    if (mesh.vbo) { ++horizonResidentMeshes; horizonCacheBytes += size_t(mesh.count) * sizeof(FarTerrainVertex); }
    if (mesh.surface && grids.insert(mesh.surface.get()).second)
      horizonCpuGridBytes += mesh.surface->grid.capacity() * sizeof(regionalSurface::GridVertex);
    if (mesh.accepted && regionalSurface::isPinned(mesh.surface)) ++horizonPinnedMeshes;
  };
  for (const auto& entry : horizonCache) count(entry.second);
  count(activeFarRegion); count(previousFarRegion); count(worldStage.farProxy);
  for (const auto& cpu : horizonPending) if (cpu.surface && grids.insert(cpu.surface.get()).second)
    horizonPendingGridBytes += cpu.surface->grid.capacity() * sizeof(regionalSurface::GridVertex);
  horizonPeakResidentMeshes = std::max(horizonPeakResidentMeshes, horizonResidentMeshes);
  horizonCpuPending = horizonJob.valid(); horizonPendingMeshes = int(horizonPending.size());
}

void Renderer::retainHorizonMesh(FarRegionGpu& mesh) {
  if (!horizonContext || !mesh.ready || !mesh.accepted || !mesh.vbo ||
      !regionIdentityCompatible(mesh.regionId, mesh.worldSeed, mesh.generatorVersion, horizonSeed)) return;
  const std::string key = horizonKey(mesh.regionId, horizonSeed, mesh.generatorVersion);
  auto existing = horizonCache.find(key);
  if (existing != horizonCache.end() && existing->second.accepted &&
      !regionalSurface::mayReplace(existing->second.surface, mesh.surface)) return;
  if (!reserveHorizonSlot(key, regionalSurface::isPinned(mesh.surface))) return;
  auto& target = horizonCache[key]; destroyFarRegion(target);
  target = std::move(mesh); mesh = FarRegionGpu{};
}

void Renderer::updateRegionHorizon(geo::Position viewer, uint64_t worldSeed) {
  using Clock = std::chrono::steady_clock;
  const auto started = Clock::now();
  auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); };
  const size_t available = 2 * 1024 * 1024 > worldUploadBytesThisFrame + horizonUploadBytesThisFrame ?
    2 * 1024 * 1024 - worldUploadBytesThisFrame - horizonUploadBytesThisFrame : 0;
  const size_t budget = std::min(available, size_t(worldStage.active && !worldStage.ready ? 1024 * 1024 : 2 * 1024 * 1024));
  const double timeBudget = std::max(0., std::min(1.5 - worldUploadMsThisFrame - horizonUploadMsThisFrame,
    worldStage.active && !worldStage.ready ? .75 : 1.5));
  const size_t before = horizonUploadBytesThisFrame;
  try {
    // Validates even under the project's fast-math compilation policy.
    (void)geo::direction(viewer);
    if (!horizonContext || horizonSeed != worldSeed) {
      ++horizonGeneration; horizonSeed = worldSeed; horizonContext = true; horizonHasSelection = false;
      horizonPending.clear(); horizonSelection = {}; horizonError.clear();
      for (auto it = horizonCache.begin(); it != horizonCache.end();) {
        if (it->second.accepted && regionalSurface::isPinned(it->second.surface)) ++it;
        else { destroyFarRegion(it->second); it = horizonCache.erase(it); }
      }
      // Do not replace a running std::async future: its destructor would join the worker on this frame.
    }
    horizonViewer = viewer;
    if (horizonJob.valid() && horizonJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      auto batch = horizonJob.get(); // already complete: never waits for generation or a recipe build
      horizonCpuBuildCount += batch.meshes.size();
      if (batch.generation == horizonGeneration) {
        horizonSelection = std::move(batch.selection); horizonSelectedViewer = batch.viewer; horizonHasSelection = true;
        for (auto& mesh : batch.meshes) horizonPending.push_back(std::move(mesh));
        horizonError.clear();
      }
    }
    // Full-derived geometry takes authority only when drawable. An unfinished reversal never removes a resident coast.
    for (auto it = horizonCache.begin(); it != horizonCache.end();) {
      if (!it->second.accepted && horizonPinned(it->second.regionId, true)) { destroyFarRegion(it->second); it = horizonCache.erase(it); }
      else ++it;
    }
    bool allocated = false;
    while (!horizonPending.empty() && elapsed() < timeBudget && horizonUploadBytesThisFrame - before < budget) {
      auto& cpu = horizonPending.front();
      const std::string key = horizonKey(cpu.region.id, cpu.region.worldSeed, cpu.region.generatorVersion);
      if (horizonPinned(cpu.region.id, true)) { horizonPending.pop_front(); continue; }
      if (!reserveHorizonSlot(key)) { horizonSelection.complete = false; horizonPending.pop_front(); continue; }
      auto& gpu = horizonCache[key];
      if (gpu.ready) { horizonPending.pop_front(); continue; }
      if (!gpu.vbo) {
        if (allocated) break; // at most one driver buffer allocation per frame
        gpu.origin = cpu.region.origin; gpu.origin.height = 0; gpu.regionId = cpu.region.id;
        gpu.worldSeed = cpu.region.worldSeed; gpu.generatorVersion = cpu.region.generatorVersion;
        gpu.surface = cpu.surface;
        gpu.count = int(cpu.vertices.size()); allocateFarRegion(gpu); allocated = true;
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Distant terrain allocation failed");
      }
      const size_t total = cpu.vertices.size() * sizeof(FarTerrainVertex);
      while (gpu.uploaded < total && elapsed() < timeBudget && horizonUploadBytesThisFrame - before < budget) {
        const size_t bytes = std::min({total - gpu.uploaded, budget - (horizonUploadBytesThisFrame - before), size_t(256 * 1024)});
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, gpu.uploaded, bytes, reinterpret_cast<const uint8_t*>(cpu.vertices.data()) + gpu.uploaded);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Distant terrain upload failed");
        gpu.uploaded += bytes; horizonUploadBytesThisFrame += bytes; horizonUploadBytesTotal += bytes;
      }
      gpu.ready = gpu.uploaded == total;
      if (!gpu.ready) break;
      horizonPending.pop_front(); // release the <=3.75 MiB CPU vector promptly, not an entire neighborhood
    }
    const bool moved = !horizonHasSelection || geo::surfaceDistance(viewer, horizonSelectedViewer) > 5000. ||
      std::abs(viewer.height - horizonSelectedViewer.height) > 500.;
    bool missing = false;
    for (const auto& d : horizonSelection.regions) {
      auto cached = horizonCache.find(horizonKey(d.id, d.worldSeed, d.generatorVersion));
      if (!horizonPinned(d.id, false) && (cached == horizonCache.end() || !cached->second.ready)) { missing = true; break; }
    }
    if (!horizonJob.valid() && horizonPending.empty() && (moved || missing) && Clock::now() >= horizonRetryAt) {
      std::vector<std::string> known, pinned, protectedCache;
      for (const auto& entry : horizonCache) {
        if (entry.second.ready) known.push_back(entry.first);
        if (entry.second.accepted && regionalSurface::isPinned(entry.second.surface)) {
          protectedCache.push_back(entry.second.regionId); pinned.push_back(entry.second.regionId);
        }
      }
      auto pin = [&](const FarRegionGpu& gpu) {
        if (!gpu.regionId.empty() && regionIdentityCompatible(gpu.regionId, gpu.worldSeed, gpu.generatorVersion, worldSeed)) pinned.push_back(gpu.regionId);
      };
      pin(activeFarRegion); if (worldStage.active) pin(worldStage.farProxy); pin(previousFarRegion);
      if (!g_world.repeating) pinned.push_back(g_world.regionId);
      const uint64_t generation = horizonGeneration;
      horizonJob = std::async(std::launch::async, [viewer, worldSeed, generation, known = std::move(known), pinned = std::move(pinned), protectedCount = protectedCache.size()] {
        HorizonBatch batch; batch.viewer = viewer; batch.generation = generation;
        batch.selection = horizon::select(viewer, worldSeed);
        size_t cacheNeeded = 0;
        auto& regions = batch.selection.regions;
        regions.erase(std::remove_if(regions.begin(), regions.end(), [&](const procedural::Descriptor& d) {
          if (std::find(pinned.begin(), pinned.end(), d.id) != pinned.end()) return false;
          if (++cacheNeeded <= kHorizonCacheLimit - std::min(kHorizonCacheLimit, protectedCount)) return false;
          batch.selection.complete = false; return true;
        }), regions.end());
        batch.meshes.reserve(2);
        for (const auto& d : regions) {
          if (std::find(pinned.begin(), pinned.end(), d.id) != pinned.end() ||
              std::find(known.begin(), known.end(), horizonKey(d.id, d.worldSeed, d.generatorVersion)) != known.end()) continue;
          batch.meshes.push_back(horizon::build(d));
          if (batch.meshes.size() == 2) break;
        }
        return batch;
      });
      ++horizonJobsStarted;
    }
  } catch (const std::exception& e) {
    horizonError = e.what(); horizonRetryAt = Clock::now() + std::chrono::seconds(1);
    for (auto it = horizonCache.begin(); it != horizonCache.end();) {
      if (!it->second.ready) { destroyFarRegion(it->second); it = horizonCache.erase(it); } else ++it;
    }
    horizonPending.clear();
  }
  publishFarSurfaces();
  refreshHorizonStats();
  horizonUploadMsThisFrame += elapsed();
}

float Renderer::activeTerrainFarBlend(const FrameParams& fp) const {
  // Both charts are already in this same coarse representation before ownership changes over their ocean gap.
  const uint64_t activeSeed = g_world.regionRecipe ? g_world.regionRecipe->descriptor.worldSeed : procedural::kDefaultWorldSeed;
  if (g_world.repeating || !activeFarRegion.ready || !activeFarRegion.accepted || activeFarRegion.count <= 0 || activeFarRegion.regionId != g_world.regionId ||
      !regionIdentityCompatible(activeFarRegion.regionId, activeFarRegion.worldSeed, activeFarRegion.generatorVersion, activeSeed)) return 0.f;
  return regionTerrainLodBlend(hypotf(fp.camPos.x, fp.camPos.z));
}

void Renderer::drawFarRegions(const FrameParams& fp, bool warm) {
  distantRegionDraws = 0;
  if (!progFarRegion || (!warm && g_world.repeating) || (dbgOff & kProbeTerrain)) return;
  const geo::Position activeOrigin = g_world.regionRecipe ? g_world.regionRecipe->descriptor.origin : geo::solaceOrigin();
  const geo::LocalFrame activeFrame(activeOrigin);
  glUseProgram(progFarRegion);
  const mat4 vp = viewProj(fp), view = viewMat(fp);
  glUniformMatrix4fv(U(progFarRegion, "uVP"), 1, GL_FALSE, vp.m);
  glUniformMatrix4fv(U(progFarRegion, "uPanoView"), 1, GL_FALSE, view.m);
  glUniform2f(U(progFarRegion, "uPano"), fp.pano, fp.panoTanY);
  glUniform2f(U(progFarRegion, "uJit"), jitX, jitY);
  glUniform1f(U(progFarRegion, "uLogC"), logDepthC());
  glUniform1f(U(progFarRegion, "uGeoRadius"), float(geo::radius()));
  glUniform1f(U(progFarRegion, "uPlanetR"), fp.pano > 0.f ? 0.f : planetRadius());
  glUniform3f(U(progFarRegion, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  auto draw = [&](const FarRegionGpu& mesh, float coverage = 1.f) {
    if (!mesh.ready || !mesh.accepted || !mesh.vao || mesh.count <= 0 || coverage <= 0.f) return;
    glUniform1f(U(progFarRegion, "uRegionCoverage"), coverage);
    const geo::FrameChange change(geo::LocalFrame(mesh.origin), activeFrame);
    const vec3 basis[4] = {change.vector(vec3(1, 0, 0)), change.vector(vec3(0, 1, 0)),
      change.vector(vec3(0, 0, 1)), change.point(vec3())};
    mat4 transform;
    for (int column = 0; column < 4; ++column) {
      transform(0, column) = basis[column].x; transform(1, column) = basis[column].y; transform(2, column) = basis[column].z;
    }
    glUniformMatrix4fv(U(progFarRegion, "uRegionTransform"), 1, GL_FALSE, transform.m);
    glBindVertexArray(mesh.vao); glDrawArrays(GL_TRIANGLES, 0, mesh.count); ++distantRegionDraws;
  };
  draw(activeFarRegion, warm ? 1.f : activeTerrainFarBlend(fp));
  const uint64_t currentSeed = horizonContext ? horizonSeed :
    (g_world.regionRecipe ? g_world.regionRecipe->descriptor.worldSeed : procedural::kDefaultWorldSeed);
  auto compatible = [&](const FarRegionGpu& mesh) {
    return regionIdentityCompatible(mesh.regionId, mesh.worldSeed, mesh.generatorVersion, currentSeed) ||
      (mesh.accepted && regionalSurface::isPinned(mesh.surface));
  };
  if (worldStage.active && worldStage.farProxy.regionId != g_world.regionId && compatible(worldStage.farProxy)) draw(worldStage.farProxy);
  const bool stagedPriorDrawable = worldStage.active && compatible(worldStage.farProxy) && worldStage.farProxy.ready &&
    worldStage.farProxy.accepted && worldStage.farProxy.vao && worldStage.farProxy.count > 0 && previousFarRegion.regionId == worldStage.farProxy.regionId;
  if (previousFarRegion.regionId != g_world.regionId && !stagedPriorDrawable && compatible(previousFarRegion)) draw(previousFarRegion);
  if (!warm && horizonContext) {
    const auto viewer = regionTravel::Chart(activeOrigin).position(fp.camPos);
    const double reach = geo::radius() * (acos(geo::radius() / (geo::radius() + std::max(0., viewer.height))) +
      acos(geo::radius() / (geo::radius() + horizon::kSummitHeight))) + sqrt(2.) * WORLD_HALF;
    for (const auto& entry : horizonCache) {
      const auto& mesh = entry.second;
      if (compatible(mesh) && !horizonPinned(mesh.regionId, true) && geo::surfaceDistance(viewer, mesh.origin) <= reach) draw(mesh);
    }
  }
}

std::vector<std::vector<uint8_t>> Renderer::prepareRoadLod(const World& world) {
  generationCheckpoint();
  std::vector<std::vector<uint8_t>> roadLod;
  const int N0 = HM_N; const float T = HM_TEXEL;
  roadLod.assign(1, std::vector<uint8_t>((size_t)N0 * N0, 0));
  std::vector<uint8_t>& L0 = roadLod[0];
  auto cell = [&](float v) { return std::clamp((int)floorf((v + WORLD_HALF) / T), 0, N0 - 1); };
  for (const RoadSegment& s : world.roadGrid.segs) {
    generationCheckpoint();
    if (s.flags & (RS_BRIDGE | RS_NOGRADE)) continue;
    const float P = roadSpec(s.cls).halfPlatform, R = P + 20.f;
    // (where the grading leaves the ground much as it was - a road across level land, its bed the ground itself - the
    // plain levels draw it as well as any)
    bool shaped = false;
    for (int q = 0; q <= 4 && !shaped; q++) {
      const float t = q / 4.f, x = s.ax + (s.bx - s.ax) * t, z = s.az + (s.bz - s.az) * t, hr = s.ah + (s.bh - s.ah) * t;
      const float dx = s.bx - s.ax, dz = s.bz - s.az, l = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / l * (P + 10.f), nz = dx / l * (P + 10.f);
      shaped = fabsf(world.naturalHeight(x, z, 6) - hr) > 0.5f || fabsf(world.naturalHeight(x + nx, z + nz, 6) - world.naturalHeight(x - nx, z - nz, 6)) > 1.f;
    }
    if (!shaped) continue;
    const uint8_t v = (uint8_t)std::clamp(ceilf(P), 1.f, 255.f);
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L2 = std::max(dx * dx + dz * dz, 1e-6f);
    for (int j = cell(std::min(s.az, s.bz) - R); j <= cell(std::max(s.az, s.bz) + R); j++)
      for (int i = cell(std::min(s.ax, s.bx) - R); i <= cell(std::max(s.ax, s.bx) + R); i++) {
        if (i == cell(std::min(s.ax, s.bx) - R)) generationCheckpoint();
        const float cx = -WORLD_HALF + (i + 0.5f) * T, cz = -WORLD_HALF + (j + 0.5f) * T;
        const float t = std::clamp(((cx - s.ax) * dx + (cz - s.az) * dz) / L2, 0.f, 1.f);
        if (hypotf(s.ax + dx * t - cx, s.az + dz * t - cz) > R + T * 0.7072f) continue;
        uint8_t& c = L0[(size_t)j * N0 + i]; if (!c || v < c) c = v;
      }
  }
  for (int L = 1; (N0 >> L) >= 1; L++) {
    const int n = N0 >> L, pn = n * 2;
    const std::vector<uint8_t>& P = roadLod[L - 1];
    std::vector<uint8_t> C((size_t)n * n, 0);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
      if (i == 0) generationCheckpoint();
      uint8_t m = 0;
      for (int q = 0; q < 4; q++) { const uint8_t v = P[(size_t)(2 * j + (q >> 1)) * pn + 2 * i + (q & 1)]; if (v && (!m || v < m)) m = v; }
      C[(size_t)j * n + i] = m;
    }
    roadLod.push_back(std::move(C));
  }
  return roadLod;
}

void Renderer::buildRoadLod() { roadLod = prepareRoadLod(g_world); }

// The chunks for this view, into terrInst (origin x, z, cell size, the distance its parent stops splitting at - 0: the
// plain levels'): the quadtree from the world down, culled against the frustum, split by distance, and along the
// graded roads further. Chunk height bounds come from the envelope's cell maxima (world.cpp), one level-0 texel for
// every sub-texel chunk.
void Renderer::selectTerrainChunks(const FrameParams& fp) {
  const int N = HM_N; const float T = HM_TEXEL;
  vec3 cam = fp.camPos;
  float rim = 0.f;
  for (int i = 0; i < fp.wreck.craterN; i++) rim += 0.22f * fabsf(fp.wreck.crater[i][3]) + 0.3f;
  float rootTop = 0.f; for (float m : g_world.tpM[TP_LEVELS - 1]) rootTop = std::max(rootTop, m);
  mat4 vpc = viewProj(fp);
  float pl[5][4];
  for (int k = 0; k < 5; k++) {
    int r = k / 2; float sg = (k & 1) ? -1.f : 1.f;
    for (int c = 0; c < 4; c++) pl[k][c] = (k == 4 ? vpc(3, c) + vpc(2, c) : 1.01f * vpc(3, c) + sg * vpc(r, c));
  }
  auto inView = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
    if (fp.pano > 0.f) return true;   // (a panorama sees most of the way round)
    for (int k = 0; k < 5; k++) {
      float x = pl[k][0] >= 0 ? x1 : x0, y = pl[k][1] >= 0 ? y1 : y0, z = pl[k][2] >= 0 ? z1 : z0;
      if (pl[k][0] * x + pl[k][1] * y + pl[k][2] * z + pl[k][3] < 0.f) return false;
    }
    return true;
  };
  terrInst.clear(); terrInstCoarse.clear();
  // the round world (planet.h): a chunk is drawn where the curve takes it - its bounds those of its corners there, out
  // by the sagitta of the arc it spans - and not at all past the horizon (the sea's from the camera, and as far again
  // as its own top sees over it)
  const float Rp = fp.pano > 0.f ? 0.f : planetRadius(), camHorizon = planetHorizon(cam.y, Rp);
  auto inViewCurved = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
    vec3 lo(1e30f), hi(-1e30f);
    for (int k = 0; k < 8; k++) {
      const vec3 c = planetPos(vec3(k & 1 ? x1 : x0, k & 2 ? y1 : y0, k & 4 ? z1 : z0), cam, Rp);
      lo = vec3(std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)); hi = vec3(std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z));
    }
    const float sag = ((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0)) / (4.f * Rp) + 1.f;
    return inView(lo.x - sag, hi.x + sag, lo.y - sag, hi.y + sag, lo.z - sag, hi.z + sag);
  };
  float copyX = 0.f, copyZ = 0.f;   // the copy of the islands being visited (its offset: the map repeats, world.h WRAP_HALF)
  // L: the chunk's level (cells of T * 2^L); x0, z0: its origin (m); byRoad: its parent split for a road's sake
  std::function<void(int, float, float, bool)> visit = [&](int L, float x0, float z0, bool byRoad) {
    float cs = T * ldexpf(1.f, L), S = cs * TP_CHUNK;
    // height bound: the envelope's cell maximum at the chunk's size (L >= 0), or of the texel it lies in (L < 0)
    int ox = (int)floorf((x0 - copyX + WORLD_HALF) / T + 0.01f), oz = (int)floorf((z0 - copyZ + WORLD_HALF) / T + 0.01f);
    int Lm = std::max(L, 0) + 5; float top;
    if (Lm >= TP_LEVELS) top = rootTop;
    else { int n = N >> Lm; int i = std::clamp(ox >> Lm, 0, n - 1), j = std::clamp(oz >> Lm, 0, n - 1); top = g_world.tpM[Lm][(size_t)j * n + i]; }
    top += rim;
    float dx = std::max(0.f, std::max(x0 - cam.x, cam.x - (x0 + S))), dz = std::max(0.f, std::max(z0 - cam.z, cam.z - (z0 + S)));
    if (Rp > 0.f) {
      if (dx * dx + dz * dz > (camHorizon + planetHorizon(top, Rp)) * (camHorizon + planetHorizon(top, Rp))) return;
      if (!inViewCurved(x0, x0 + S, -800.f, top + 1.f, z0, z0 + S)) return;
    } else if (!inView(x0, x0 + S, -800.f, top + 1.f, z0, z0 + S)) return;
    float dy = std::max(0.f, cam.y - top);
    const float d2 = dx * dx + dz * dz + dy * dy;
    // the narrowest graded road across the chunk (the chunk spans one cell at level L + 5; a smaller one lies in one texel)
    float road = 0.f;
    if (!roadLod.empty()) {
      const int m = std::clamp(L + 5, 0, (int)roadLod.size() - 1), n = N >> m;
      const int i = ox >> m, j = oz >> m;
      if (i >= 0 && j >= 0 && i < n && j < n) road = roadLod[m][(size_t)j * n + i];
    }
    const bool plain = d2 < kSplit * kSplit * S * S;
    const bool forRoad = !plain && road > 0.f && cs > std::max(0.5f * road, roadCellAllowed(sqrtf(d2)));
    if (L > kMinLevel && (plain || forRoad)) {
      float h = S * 0.5f;
      for (int k = 0; k < 4; k++) visit(L - 1, x0 + (k & 1) * h, z0 + (k >> 1) * h, forRoad);
    } else {
      // (a chunk with a road on it there for the road's sake morphs onto its parent's grid by the distance the parent
      // stops splitting for the road; the rest - plain, or a road's neighbour - by the plain levels')
      const float merge = byRoad && road > 0.f ? roadCellDistance(2.f * cs) : 0.f;
      // (wholly past its morph - kTerrainVS's m reaches 1 at 0.95 of the distance it morphs by - every vertex the
      // parent lacks lies on the parent's grid: the coarse grid draws the same surface)
      const float Dm = 0.95f * (merge > 0.f ? merge : kSplit * 2.f * S);
      const bool coarse = d2 >= Dm * Dm;
      std::vector<float>& to = coarse ? terrInstCoarse : terrInst;
      to.insert(to.end(), {x0, z0, cs, merge});
    }
  };
  // the islands, and their copies across the map's seam as far as the view reaches - on the round world to the
  // horizon, every copy of them the curve brings into sight from up there (the vertex shader's heights wrap, so a
  // copy's chunks are the islands' own ground)
  const float reach = Rp > 0.f ? camHorizon + planetHorizon(rootTop + rim, Rp) : kTerrainFar;
  const int K = g_world.repeating ? std::min((int)ceilf((reach + WRAP_HALF) / WRAP_SPAN), 64) : 0;
  for (int kz = -K; kz <= K; kz++) for (int kx = -K; kx <= K; kx++) {
    copyX = kx * WRAP_SPAN; copyZ = kz * WRAP_SPAN;
    const float ex = std::max(std::max(-WORLD_HALF + copyX - cam.x, cam.x - (WORLD_HALF + copyX)), 0.f), ez = std::max(std::max(-WORLD_HALF + copyZ - cam.z, cam.z - (WORLD_HALF + copyZ)), 0.f);
    if ((kx != 0 || kz != 0) && ex * ex + ez * ez > reach * reach) continue;
    visit(TP_LEVELS - 1, -WORLD_HALF + copyX, -WORLD_HALF + copyZ, false);
  }
  // nearest first, each list: the depth test then turns away the ground a nearer chunk has already hidden before its
  // material is worked out. In the quadtree's order (and the copies' from -K to K) the far copies of the islands came
  // first - from the ground, every one of them out to the horizon shaded in the same few rows over it, then hidden
  // (the v3.48.0 diagnostics' night scene on a runway: its terrain material 1.9 ms in v3.43, 5.5 ms)
  auto nearestFirst = [&](std::vector<float>& v) {
    const size_t n = v.size() / 4;
    std::vector<std::pair<float, uint32_t>> order(n);
    for (size_t i = 0; i < n; i++) {
      const float h = 0.5f * v[i * 4 + 2] * TP_CHUNK, dx = v[i * 4] + h - cam.x, dz = v[i * 4 + 1] + h - cam.z;
      order[i] = {dx * dx + dz * dz, (uint32_t)i};
    }
    std::sort(order.begin(), order.end());
    std::vector<float> sorted(v.size());
    for (size_t i = 0; i < n; i++) std::copy_n(&v[order[i].second * 4], 4, &sorted[i * 4]);
    v.swap(sorted);
  };
  static const bool unsorted = getenv("TERRUNSORTED") != nullptr;   // (debug: the old order, to compare)
  if (!unsorted) { nearestFirst(terrInst); nearestFirst(terrInstCoarse); }
  terrChunks = (int)terrInst.size() / 4; terrChunksCoarse = (int)terrInstCoarse.size() / 4;
  static const bool chunkDbg = getenv("CHUNKDBG") != nullptr;   // (debug: the chunks drawn, and how many copies of the islands they span)
  if (chunkDbg) printf("terrain: %d chunks + %d coarse, copies out to %d, reach %.0f km (camera %.0f m up)\n", terrChunks, terrChunksCoarse, K, reach * 0.001f, cam.y);
}

// The ground's depth prepass's program: the terrain's vertex shader, and a fragment shader that only drops the ground
// under the sea (seen from above it). Not among the launch's
// programs (shader_loading_contract counts those): a flight's loading screen builds it, so the prepass's trials
// (prepass_auto.h) never compile anything mid-flight
void Renderer::prepareTerrainDepth() {
  if (progTerrainDepth || terrainDepthTried || !progTerrain) return;
  terrainDepthTried = true;
  std::string de; progTerrainDepth = linkProgramCached(terrainVSAssembly(""), terrainDepthFSAssembly(), de);
}
void Renderer::prepareTerrainDither() {
  if (progTerrainDither || terrainDitherTried || !progTerrain) return;
  terrainDitherTried = true;
  std::string de; progTerrainDither = linkProgramCached(terrainVSAssembly(""), terrainFSAssembly("#define FAR_REGION_DITHER\n"), de);
}

// The analysis's count of the ground's shading (renderer.h countGround): the fragments that passed the depth test, and
// where the card counts them the fragment shader's runs. Many more runs than fragments kept is ground shaded for
// nothing: hidden, but found so only after its shader ran (or thrown away by the shader's own discards)
void Renderer::beginGroundCount() {
  if (!qGround[0]) glGenQueries(2, qGround);
  if (qShadedAvail < 0) {
    bool ext = false;
    if (glGetIntegerv && glGetStringi) {
      GLint ne = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &ne);
      for (GLint i = 0; i < ne && !ext; i++) { const GLubyte* e = glGetStringi(GL_EXTENSIONS, (GLuint)i); ext = e && !strcmp((const char*)e, "GL_ARB_pipeline_statistics_query"); }
    }
    for (int i = 0; i < 16 && glGetError() != 0; i++) {}
    qShadedAvail = ext ? 1 : 0;
  }
  glBeginQuery(GL_SAMPLES_PASSED, qGround[0]);
  if (qShadedAvail == 1) {
    glBeginQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB, qGround[1]);
    if (glGetError() != 0) qShadedAvail = 0;   // (listed, but refused: kept counted alone)
  }
}
void Renderer::endGroundCount() {
  glEndQuery(GL_SAMPLES_PASSED);
  if (qShadedAvail == 1) glEndQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB);
  qGroundIssued = true;
}
bool Renderer::readGroundCounts(double& keptPx, double& shadedPx) {
  keptPx = shadedPx = -1.0;
  if (!qGroundIssued) return false;
  qGroundIssued = false;
  GLuint64 n = 0; glGetQueryObjectui64v(qGround[0], GL_QUERY_RESULT, &n); keptPx = (double)n;
  if (qShadedAvail == 1) { n = 0; glGetQueryObjectui64v(qGround[1], GL_QUERY_RESULT, &n); shadedPx = (double)n; }
  return true;
}

// Draws the terrain and the sea into the bound G-buffer (depth-tested against what is there already)
void Renderer::drawTerrainMesh(const FrameParams& fp, int mode) {
  if (!progTerrain || !vaoTerrain) return;
  const bool pre = mode == 1, after = mode == 2;
  if (pre && !progTerrainDepth) {   // (built the first time the prepass is asked for, if no loading screen has: renderer.h)
    prepareTerrainDepth();
    if (!progTerrainDepth) return;
  }
  const bool distantActive = activeTerrainIsDistant(fp);
  if (!after) {   // (after the prepass: its chunks, already in the buffer)
    if (!distantActive) selectTerrainChunks(fp);
    else { terrInst.clear(); terrInstCoarse.clear(); terrChunks = terrChunksCoarse = 0; }
  }
  // A nonrepeating Earth chart can have no land in view. The ocean must still draw (and the prepass has nothing to do).
  if (pre && terrInst.empty() && terrInstCoarse.empty()) return;
  // the shading pass after a prepass: only where the ground's own depth went in and nothing nearer has since (the
  // scenery); equal depth, the vertex shader's position being invariant. (No prepass while an Earth region's far blend
  // dithers the near terrain out: drawEntities)
  glEnable(GL_DEPTH_TEST); glDepthFunc(after ? GL_LEQUAL : GL_LESS); glDepthMask(after ? GL_FALSE : GL_TRUE);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  if (pre) glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  mat4 vp = viewProj(fp), view = viewMat(fp);
  // the shading pass's program: the one without a discard (the card tests the depth before it shades), unless an
  // Earth region's far blend is dithering the ground out (prepareTerrainDither)
  const float farBlend = activeTerrainFarBlend(fp);
  GLuint p = pre ? progTerrainDepth : farBlend > 0.f && progTerrainDither ? progTerrainDither : progTerrain;
  setRT(p, fp);
  glUniform1f(U(p, "uFarRegionBlend"), farBlend);
  glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, vp.m);
  glUniformMatrix4fv(U(p, "uPanoView"), 1, GL_FALSE, view.m);
  glUniform2f(U(p, "uPano"), fp.pano, fp.panoTanY);
  glUniform2f(U(p, "uJit"), jitX, jitY);
  glUniform1f(U(p, "uLogC"), logDepthC());
  glUniform1f(U(p, "uSplit"), kSplit);
  glBindVertexArray(vaoTerrain); glBindBuffer(GL_ARRAY_BUFFER, vboTerrainInst);
  // (one buffer: the full chunks, then the coarse ones - drawn from there with the coarse grid's indices)
  const size_t fineBytes = terrInst.size() * sizeof(float), coarseBytes = terrInstCoarse.size() * sizeof(float);
  if (!after) {
    glBufferData(GL_ARRAY_BUFFER, fineBytes + coarseBytes, nullptr, GL_STREAM_DRAW);
    if (fineBytes) glBufferSubData(GL_ARRAY_BUFFER, 0, fineBytes, terrInst.data());
    if (coarseBytes) glBufferSubData(GL_ARRAY_BUFFER, fineBytes, coarseBytes, terrInstCoarse.data());
  }
  const bool counted = countGround && !pre && !feedPass && fp.pano <= 0.f;   // (the main view's shading: the analysis)
  if (counted) beginGroundCount();
  glEnable(GL_CLIP_DISTANCE0);   // (the sea floor cut away at sea level: kTerrainVS)
  if (!(dbgOff & kProbeTerrain)) {
    if (terrChunks) glDrawElementsInstanced(GL_TRIANGLES, kChunkVerts, GL_UNSIGNED_SHORT, nullptr, terrChunks);
    if (terrChunksCoarse) {
      glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)fineBytes);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrainCoarse);
      glDrawElementsInstanced(GL_TRIANGLES, kChunkVertsCoarse, GL_UNSIGNED_SHORT, nullptr, terrChunksCoarse);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    }
  }
  glDisable(GL_CLIP_DISTANCE0);
  if (counted) endGroundCount();
  glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  if (pre) { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glBindVertexArray(0); glDisable(GL_DEPTH_TEST); return; }   // (the sea comes with the shading pass)
  // the sea
  if (progWater && vaoWater && fp.camPos.y > -0.5f) {
    p = progWater;
    setRT(p, fp);
    glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(U(p, "uPanoView"), 1, GL_FALSE, view.m);
    glUniform2f(U(p, "uPano"), fp.pano, fp.panoTanY);
    glUniform2f(U(p, "uJit"), jitX, jitY);
    glUniform1f(U(p, "uLogC"), logDepthC());
    glActiveTexture(GL_TEXTURE0 + 9); glBindTexture(GL_TEXTURE_2D_ARRAY, texWaves); glUniform1i(U(p, "uWaves"), 9);
    glUniform3f(U(p, "uWaveRms"), waveRms[0], waveRms[1], waveRms[2]);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vaoWater);
    glDrawElements(GL_TRIANGLES, waterIdx, GL_UNSIGNED_INT, nullptr);
  }
  drawFarRegions(fp);
  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);
}
