// The GL staging contract is checked without a driver: every allocation is empty, every data upload is bounded,
// live handles stay private until ready, and cancellation/error/repeated commits retain at most two world sets.
#include "../src/renderer.h"
#include "../src/scenery.h"
#include "../src/shaders.h"
#include <set>
#include <stdexcept>
#include <thread>
#include "../src/region_travel.h"

namespace {
std::set<GLuint> textures, buffers, vaos;
GLuint nextId = 100, boundTexture = 0;
size_t uploaded = 0, largestUpload = 0, allocations = 0;
GLenum error = GL_NO_ERROR;
bool failAllocation = false;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void generate(GLsizei n, GLuint* ids, std::set<GLuint>& objects) {
  for (int i = 0; i < n; ++i) { ids[i] = nextId++; objects.insert(ids[i]); }
}
void remove(GLsizei n, const GLuint* ids, std::set<GLuint>& objects) {
  for (int i = 0; i < n; ++i) check(objects.erase(ids[i]) == 1, "deleted a live object's name twice");
}
size_t texelBytes(GLenum format, GLenum type) {
  return (format == GL_RGBA ? 4 : 1) * (type == GL_UNSIGNED_BYTE ? 1 : 4);
}
void fakeGL() {
  glGenTextures = [](GLsizei n, GLuint* ids) { generate(n, ids, textures); };
  glDeleteTextures = [](GLsizei n, const GLuint* ids) { remove(n, ids, textures); };
  glBindTexture = [](GLenum, GLuint id) { check(textures.count(id), "texture must exist before binding"); boundTexture = id; };
  glTexImage2D = [](GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void* source) {
    check(source == nullptr, "world stage must never submit monolithic glTexImage data");
    ++allocations; if (failAllocation) { error = GL_OUT_OF_MEMORY; failAllocation = false; }
  };
  glTexSubImage2D = [](GLenum, GLint, GLint, GLint, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* source) {
    check(boundTexture != 77, "active navigation texture modified before commit");
    check(source != nullptr && w > 0 && h > 0, "invalid texture row upload");
    const size_t bytes = size_t(w) * h * texelBytes(format, type);
    uploaded += bytes; largestUpload = std::max(largestUpload, bytes);
  };
  glTexParameteri = [](GLenum, GLenum, GLint) {};
  glActiveTexture = [](GLenum) {};
  glPixelStorei = [](GLenum, GLint) {};
  glGetError = []() -> GLenum { const GLenum e = error; error = GL_NO_ERROR; return e; };
  glGenVertexArrays = [](GLsizei n, GLuint* ids) { generate(n, ids, vaos); };
  glDeleteVertexArrays = [](GLsizei n, const GLuint* ids) { remove(n, ids, vaos); };
  glBindVertexArray = [](GLuint) {};
  glGenBuffers = [](GLsizei n, GLuint* ids) { generate(n, ids, buffers); };
  glDeleteBuffers = [](GLsizei n, const GLuint* ids) { remove(n, ids, buffers); };
  glBindBuffer = [](GLenum, GLuint) {};
  glBufferData = [](GLenum, GLsizeiptr, const void* source, GLenum) {
    check(source == nullptr, "bridge staging must allocate without a monolithic payload");
  };
  glBufferSubData = [](GLenum, GLintptr, GLsizeiptr bytes, const void* source) {
    check(bytes > 0 && source, "invalid bridge upload"); uploaded += size_t(bytes); largestUpload = std::max(largestUpload, size_t(bytes));
  };
  glEnableVertexAttribArray = [](GLuint) {};
  glVertexAttribPointer = [](GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) {};
  glVertexAttribDivisor = [](GLuint, GLuint) {};
}
}

int main() {
  try {
    fakeGL();
    for (size_t rows : {size_t(1), size_t(2), size_t(3), size_t(4), size_t(5), size_t(8), size_t(16), size_t(17), size_t(32), size_t(256), size_t(257)}) {
      const size_t padded = Renderer::worldDataStorageRows(rows);
      check(padded >= rows && padded <= rows + 2 && padded >= 3 && (padded & (padded - 1)) != 0,
        "world data preserves content with at most two padding rows and stable non-POT sampler identity");
    }
    check(Renderer::worldDataStorageRows(16) == 17 && Renderer::worldDataStorageRows(17) == 17,
      "actual crossing's 16/17-row tables share the same storage dimensions");
    check(Renderer::regionIdentityCompatible("pr1-4-17-7", 123, procedural::kGeneratorVersion, 123) &&
      !Renderer::regionIdentityCompatible("pr1-4-17-7", 123, procedural::kGeneratorVersion, 124) &&
      !Renderer::regionIdentityCompatible("pr1-4-17-7", 123, procedural::kGeneratorVersion + 1, 123) &&
      Renderer::regionIdentityCompatible("solace", 123, procedural::kGeneratorVersion, 124),
      "draw dispatch rejects stale generated seeds/versions while authored geometry is seed-independent");
    check(Renderer::terrainShadowRowBudget(true, true, false, 2048) == 8 &&
      Renderer::terrainShadowRowBudget(true, true, true, 2048) == 8 &&
      Renderer::terrainShadowRowBudget(true, false, true, 3) == 3 &&
      Renderer::terrainShadowRowBudget(false, true, false, 2048) == 2048 &&
      Renderer::terrainShadowRowBudget(false, false, false, 2048) == 64,
      "streamed first shadow bake remains bounded even during a fade; startup and steady behavior preserved");
    check(Renderer::regionTerrainLodBlend(89999.f) == 0.f && Renderer::regionTerrainLodBlend(90000.f) == 0.f &&
      Renderer::regionTerrainLodBlend(100000.f) == .5f && Renderer::regionTerrainLodBlend(110000.f) == 1.f &&
      Renderer::regionTerrainLodBlend(120000.f) == 1.f, "far terrain blend endpoints and midpoint");
    auto ditherFunction = [](std::string source) {
      const size_t first = source.find("float regionLodDither()");
      check(first != std::string::npos, "both terrain passes require common dither");
      return source.substr(first, source.find('}', first) - first + 1);
    };
    check(ditherFunction(kTerrainFS) == ditherFunction(kFarRegionFS), "full/coarse shader dither functions are identical");
    check(std::string(kTerrainFS).find("regionLodDither() < uFarRegionBlend") != std::string::npos &&
      std::string(kFarRegionFS).find("regionLodDither() >= uRegionCoverage") != std::string::npos, "complementary shader discard inequalities");
    for (float blend : {0.f, .125f, .5f, .75f, .999f, 1.f}) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
      auto digit = [&](int bit) { return (((x >> bit) & 1) ^ ((y >> bit) & 1)) * 2 + ((y >> bit) & 1); };
      const float threshold = (digit(0) * 16 + digit(1) * 4 + digit(2) + .5f) / 64.f;
      const bool full = blend < 1.f && threshold >= blend, coarse = blend > 0.f && threshold < blend;
      check(int(full) + int(coarse) == 1, "LOD transition has neither coverage holes nor duplicate samples");
    }
    World candidate; candidate.repeating = false;
    candidate.hm.resize(size_t(HM_N) * HM_N * 4);
    for (int z = 0; z < HM_N; ++z) for (int x = 0; x < HM_N; ++x)
      candidate.hm[(size_t(z) * HM_N + x) * 4] = abs(x - HM_N / 2) < HM_N / 12 && abs(z - HM_N / 2) < HM_N / 12 ? 100.f : -100.f;
    candidate.mask.resize(size_t(MASK_N) * MASK_N * 4);
    for (size_t i = 0; i < candidate.mask.size(); i += 4) candidate.mask[i] = 255;
    for (int level = 0; level < HMAX_LEVELS; ++level) candidate.hmax[level].assign(size_t(HMAX_N >> level) * (HMAX_N >> level), -100.f);
    // The active World intentionally has no terrain: accidentally consulting g_world in any CPU builder fails.
    check(g_world.hm.empty(), "test requires an empty active world");
    {
      auto cancelled = std::make_shared<GenerationCancelState>(); cancelled->requested = true;
      ScopedGenerationCancellation scope(cancelled); bool threw = false;
      try { (void)Renderer::prepareWorldUpload(candidate); } catch (const GenerationCancelled&) { threw = true; }
      check(threw, "cancelled CPU upload packing stops before allocating payloads");
    }
    auto payload = Renderer::prepareWorldUpload(candidate);
    const size_t dataRows = payload.data.size() / ROAD_DATA_W;
    check(dataRows >= 3 && (dataRows & (dataRows - 1)) != 0, "packed upload data is non-POT before either initial or staged GL upload");
    for (size_t i = payload.data.size() - ROAD_DATA_W; i < payload.data.size(); ++i) {
      const auto& v = payload.data[i];
      check(v.x == 0 && v.y == 0 && v.z == 0 && v.w == 0, "padding rows contain only zero texels");
    }
    check(payload.ceiling == 120.f && payload.minimapLevels.size() == 11 && !payload.roadLod.empty(), "explicit CPU candidate preparation");
    check(payload.surface && payload.surface->key.provenance == regionalSurface::Provenance::FullDerived &&
      payload.surface->grid.size() == 129 * 129, "full-derived upload retains its exact immutable collision grid");
    check(!payload.farVertices.empty() && payload.farVertices.size() * sizeof(Renderer::FarTerrainVertex) <= 4 * 1024 * 1024,
      "bounded candidate-only coarse island mesh");
    check(payload.minimapLevels.front()[2] > payload.minimapLevels.front()[0], "candidate ocean minimap is blue");
    // Exercise budgeted bridge bytes even without requiring any bridge geometry generation in this fixture.
    payload.bridgeVertices.resize(20000);
    const size_t expected = candidate.hm.size() * sizeof(float) + candidate.mask.size() + payload.roadHead.size() * sizeof(uint32_t) +
      payload.data.size() * sizeof(Renderer::V4) + payload.bridgeVertices.size() * sizeof(EVert) +
      payload.farVertices.size() * sizeof(Renderer::FarTerrainVertex);
    size_t allBytes = expected;
    for (const auto& level : candidate.hmax) allBytes += level.size() * sizeof(float);
    for (const auto& level : payload.minimapLevels) allBytes += level.size();
    Renderer renderer; renderer.ok = true; renderer.minimapTex = 77; textures.insert(77);
    check(renderer.beginWorldUpload(candidate, std::move(payload)), "begin candidate upload");
    check(!renderer.commitWorldUpload(), "partial world must never publish");
    check(!renderer.pumpWorldUpload(0, 1000), "zero-byte budget cannot progress");
    constexpr size_t budget = 384 * 1024;
    unsigned frames = 0;
    while (!renderer.worldUploadReady()) {
      const size_t before = uploaded, priorAllocations = allocations;
      renderer.pumpWorldUpload(budget, 1000);
      check(renderer.worldUploadError().empty(), "unexpected staging error");
      check(uploaded - before <= budget && uploaded - before == renderer.worldUploadBytesThisFrame, "exact per-frame byte budget and counters");
      check(allocations - priorAllocations <= 1, "at most one driver texture allocation per pump");
      check(renderer.minimapTex == 77, "active navigation remains unchanged until ready");
      check(textures.size() <= 7, "only active and one staged texture set");
      check(++frames < 2000, "budgeted upload eventually completes");
    }
    check(uploaded == allBytes && renderer.worldUploadBytesTotal == allBytes, "all world texture/mip/bridge bytes uploaded exactly once");
    check(largestUpload <= 256 * 1024 && frames > 100, "small transfer batches span many flight frames");
    Renderer::WorldUploadData retired;
    check(renderer.commitWorldUpload(&retired) && renderer.minimapTex != 77 && textures.size() == 6, "ready commit replaces handles and releases old textures");
    check(renderer.streamingShadowPending(), "world publication requests an incremental shadow replacement");
    check(buffers.size() == 3 && vaos.size() == 2, "one published bridge and coarse terrain dataset");
    check(!retired.roadHead.empty() && !retired.minimapLevels.empty() && !retired.farVertices.empty(), "retired CPU payload can be released off thread");
    const GLuint activeMap = renderer.minimapTex;
    for (int repetition = 0; repetition < 3; ++repetition) {
      auto next = Renderer::prepareWorldUpload(candidate);
      check(renderer.beginWorldUpload(candidate, std::move(next)), "restart staging");
      renderer.pumpWorldUpload(budget, 1000);
      renderer.cancelWorldUpload();
      check(renderer.minimapTex == activeMap && textures.size() == 6 && buffers.size() == 3, "cancellation preserves active resources without leaks");
    }
    auto failing = Renderer::prepareWorldUpload(candidate);
    check(renderer.beginWorldUpload(candidate, std::move(failing)), "begin failing candidate");
    failAllocation = true;
    for (int attempt = 0; attempt < 100 && renderer.worldUploadError().empty(); ++attempt) renderer.pumpWorldUpload(budget, 1000);
    check(!renderer.worldUploadReady() && !renderer.worldUploadError().empty(), "allocation failure is observable");
    check(renderer.minimapTex == activeMap && textures.size() == 6 && !renderer.worldUploadReady(), "failed candidate cannot replace active world or leak");
    auto repeated = Renderer::prepareWorldUpload(candidate);
    check(renderer.beginWorldUpload(candidate, std::move(repeated)), "retry after failed upload");
    while (!renderer.worldUploadReady()) { renderer.pumpWorldUpload(2 * 1024 * 1024, 1000); check(renderer.worldUploadError().empty(), "retry failed"); }
    check(renderer.commitWorldUpload() && textures.size() == 6 && buffers.size() == 4 && vaos.size() == 3, "repeated publication retains one full dataset and two bounded coarse meshes");
    // Real recipe jobs (not deferred/faked futures): the flight thread polls while upload budgets and cache
    // bounds remain observable. High-altitude startup intentionally settles asynchronously over several frames.
    geo::Position horizonViewer{25., 145., 16000.};
    const uint64_t seed = procedural::kDefaultWorldSeed;
    const auto wanted = horizon::select(horizonViewer, seed);
    check(wanted.complete && wanted.regions.size() > 3, "high-altitude fixture needs a genuine all-direction neighborhood");
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed);
    check(renderer.horizonCpuPending && renderer.horizonJobsStarted == 1 && renderer.horizonCpuBuildCount == 0,
      "initial horizon call schedules real CPU work without a synchronous build");
    const auto began = std::chrono::steady_clock::now();
    unsigned horizonFrames = 0; double worstHorizonUpdate = 0;
    auto settle = [&](geo::Position viewer, uint64_t worldSeed, bool pumpDetailed) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
      do {
        renderer.beginRegionUploadFrame();
        const auto frameStart = std::chrono::steady_clock::now();
        renderer.updateRegionHorizon(viewer, worldSeed);
        worstHorizonUpdate = std::max(worstHorizonUpdate, std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - frameStart).count());
        if (pumpDetailed) renderer.pumpWorldUpload();
        check(renderer.worldUploadBytesThisFrame + renderer.horizonUploadBytesThisFrame <= 2 * 1024 * 1024,
          "horizon and detailed uploads share one frame byte allowance");
        check(renderer.horizonError.empty() && renderer.worldUploadError().empty(), "asynchronous neighborhood upload failed");
        check(renderer.horizonResidentMeshes <= 64 && renderer.horizonPeakResidentMeshes <= 64 &&
          renderer.horizonCacheBytes <= 64 * horizon::kMaxMeshVertices * sizeof(horizon::Vertex), "bounded all-direction GPU cache");
        check(renderer.horizonPendingMeshes <= 2, "CPU upload queue holds only one two-mesh batch");
        check(renderer.horizonCpuGridBytes <= 64 * 129 * 129 * sizeof(regionalSurface::GridVertex) &&
          renderer.horizonPendingGridBytes <= 2 * 129 * 129 * sizeof(regionalSurface::GridVertex),
          "immutable collision grids have a separate bounded resident/transient CPU budget");
        check(std::chrono::steady_clock::now() < deadline, "real asynchronous neighborhood settles without joining");
        ++horizonFrames;
        if (!renderer.horizonCpuPending && renderer.horizonPendingMeshes == 0 && renderer.horizonWantedMeshes > 0 &&
            renderer.horizonReadyMeshes == renderer.horizonWantedMeshes && (!pumpDetailed || renderer.worldUploadReady())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (true);
    };
    auto simultaneous = Renderer::prepareWorldUpload(candidate);
    check(renderer.beginWorldUpload(candidate, std::move(simultaneous)), "stage a detailed chart alongside neighborhood jobs");
    settle(horizonViewer, seed, true);
    check(renderer.horizonSelectionComplete && renderer.horizonWantedMeshes == int(wanted.regions.size()),
      "every selected all-direction region becomes ready");
    const double initialSettleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    const unsigned initialSettleFrames = horizonFrames;
    const auto builds = renderer.horizonCpuBuildCount;
    const auto jobs = renderer.horizonJobsStarted;
    for (int frame = 0; frame < 16; ++frame) { renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed); }
    check(renderer.horizonCpuBuildCount == builds && renderer.horizonJobsStarted == jobs,
      "settled stationary horizon does not rebuild recipes");
    renderer.cancelWorldUpload();
    // A selected visible chart first has an analytic mesh, then its ready staged full-derived mesh takes over.
    // Cancelling that detailed upload must retain the staged silhouette instead of leaving an empty coast.
    const auto analytic = horizon::build(wanted.regions.front());
    check(analytic.surface && analytic.surface->key.provenance == regionalSurface::Provenance::Analytic,
      "analytic CPU mesh and collision surface share one immutable identity");
    const auto& grid = analytic.surface->grid;
    const size_t topIndex = size_t(std::max_element(grid.begin(), grid.end(), [](const auto& a, const auto& b) {
      return a.height < b.height;
    }) - grid.begin());
    const vec3 localContact(-WORLD_HALF + float(topIndex % 129) * (2.f * WORLD_HALF / 128), 0,
      -WORLD_HALF + float(topIndex / 129) * (2.f * WORLD_HALF / 128));
    auto protectedPosition = regionTravel::Chart(analytic.region.origin).position(localContact);
    protectedPosition.height = analytic.surface->maxHeight + 10.f;
    auto contactActive = procedural::nearby(analytic.region.origin, 2, seed).back();
    check(contactActive.id != analytic.region.id, "contact fixture uses a foreign owner");
    const vec3 contactQuery = regionTravel::Chart(contactActive.origin).local(protectedPosition);
    regionalSurface::setFlightContext(contactActive, protectedPosition, 20.f, 50.f);
    auto pinnedSample = regionalSurface::queryCurrent(contactQuery.x, contactQuery.z);
    check(pinnedSample.covered && pinnedSample.key == analytic.surface->key, "collision observes the already-drawable analytic proxy");
    auto paired = [&](const regionalSurface::Key& key) {
      const auto drawable = renderer.drawableRegionSurface(key.regionId);
      return drawable && drawable->key == key && regionalSurface::queryCurrent(contactQuery.x, contactQuery.z).key == key;
    };
    check(paired(pinnedSample.key), "registry identity exactly matches the accepted resident GPU representation");
    candidate.regionId = wanted.regions.front().id;
    candidate.regionRecipe = std::make_shared<procedural::Recipe>(wanted.regions.front());
    auto visibleCandidate = Renderer::prepareWorldUpload(candidate);
    check(renderer.beginWorldUpload(candidate, std::move(visibleCandidate)), "stage a selected visible neighbor");
    settle(horizonViewer, seed, true);
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed);
    check(renderer.horizonReadyMeshes == renderer.horizonWantedMeshes, "ready staged silhouette preserves analytic coverage");
    auto protectedSample = regionalSurface::queryCurrent(contactQuery.x, contactQuery.z);
    check(protectedSample.key == pinnedSample.key && paired(pinnedSample.key) && renderer.horizonPinnedMeshes > 0,
      "GPU-ready full-derived candidate cannot replace a pinned visible/collision analytic version");
    renderer.cancelWorldUpload();
    check(paired(pinnedSample.key), "cancelling an unaccepted full candidate preserves the accepted analytic GPU/collision pair");
    auto pinnedRetry = Renderer::prepareWorldUpload(candidate);
    check(renderer.beginWorldUpload(candidate, std::move(pinnedRetry)), "restage while prior analytic contact stays protected");
    settle(horizonViewer, seed, true);
    check(paired(pinnedSample.key), "restaging leaves the protected analytic representation unchanged");
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon({-40., -35., 28000.}, seed);
    settle({-40., -35., 28000.}, seed, false);
    check(paired(pinnedSample.key) && renderer.horizonPinnedMeshes > 0,
      "protected surface remains resident outside a detached camera's horizon selection");
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed);
    settle(horizonViewer, seed, false);
    const auto fullSurface = renderer.worldUploadSurface();
    check(fullSurface && fullSurface->key.provenance == regionalSurface::Provenance::FullDerived,
      "staged surface identity is available before World publication");
    protectedPosition.height = std::max(fullSurface->maxHeight, analytic.surface->maxHeight) + 1000.f;
    regionalSurface::setFlightContext(contactActive, protectedPosition, 20.f, 50.f);
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed);
    check(paired(fullSurface->key), "clearance gained with enabled flight context replaces GPU and collision versions together");
    protectedPosition.height = fullSurface->maxHeight + 10.f;
    regionalSurface::setFlightContext(contactActive, protectedPosition, 20.f, 50.f);
    const auto fullSample = regionalSurface::queryCurrent(contactQuery.x, contactQuery.z);
    check(fullSample.covered && fullSample.key == fullSurface->key && paired(fullSurface->key), "safe clearance release activates the paired full-derived mesh/grid");
    const auto cancelBuilds = renderer.horizonCpuBuildCount;
    const auto cancelJobs = renderer.horizonJobsStarted;
    renderer.cancelWorldUpload();
    check(renderer.horizonReadyMeshes == renderer.horizonWantedMeshes, "cancelling ready staging preserves visible coast coverage immediately");
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed);
    check(renderer.horizonReadyMeshes == renderer.horizonWantedMeshes && renderer.horizonCpuBuildCount == cancelBuilds &&
      renderer.horizonJobsStarted == cancelJobs, "cancelled staged coast remains resident without a rebuild gap");
    check(paired(fullSample.key),
      "cancelling pinned full-derived staging retains the identical drawable/collision token");
    regionalSurface::clearFlightContext();
    // Reverse priority and seed before a newly launched job completes. Its future stays alive until ready;
    // stale results are discarded, then the new seed's descriptors populate the same bounded cache.
    geo::Position reversal{-40., -35., 28000.};
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(reversal, seed);
    check(renderer.horizonCpuPending, "relocation schedules a new bounded future");
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(horizonViewer, seed + 1);
    check(renderer.horizonCpuPending && renderer.horizonReadyMeshes == 0, "seed reset retains running worker without publishing stale results");
    settle(horizonViewer, seed + 1, false);
    check(renderer.horizonWantedMeshes == int(horizon::select(horizonViewer, seed + 1).regions.size()) && renderer.horizonSelectionComplete,
      "new seed fully replaces stale neighborhood identity");
    // Traverse enough unrelated neighborhoods to force eviction. Residents remain bounded, then revisiting a
    // previous neighborhood either reuses it or rebuilds it asynchronously without coverage starvation.
    for (double lon : {-150., -90., -30., 30., 90., 145.}) {
      geo::Position moved{25., lon, 28000.};
      renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon(moved, seed + 1);
      settle(moved, seed + 1, false);
    }
    // A late first proxy must not reveal new land (or invisible collision) beneath an aircraft.
    const auto unseen = procedural::nearby({-60., 0., 0.}, 1, seed + 1).front();
    check(!renderer.drawableRegionSurface(unseen.id), "unsafe first-reveal fixture has no existing drawable proxy");
    candidate.regionId = unseen.id;
    candidate.regionRecipe = std::make_shared<procedural::Recipe>(unseen);
    auto unseenPayload = Renderer::prepareWorldUpload(candidate);
    const auto unseenSurface = unseenPayload.surface;
    const auto unseenActive = procedural::nearby(unseen.origin, 2, seed + 1).back();
    check(unseenActive.id != unseen.id, "unsafe first reveal uses a foreign chart");
    auto lowAircraft = unseen.origin; lowAircraft.height = unseenSurface->maxHeight + 10.f;
    const auto unseenQuery = regionTravel::Chart(unseenActive.origin).local(lowAircraft);
    regionalSurface::setFlightContext(unseenActive, lowAircraft, 20.f, 50.f);
    check(renderer.beginWorldUpload(candidate, std::move(unseenPayload)), "begin late first proxy under protected aircraft");
    while (!renderer.worldUploadReady()) {
      renderer.beginRegionUploadFrame(); renderer.pumpWorldUpload();
      check(renderer.worldUploadError().empty(), "unsafe first-reveal upload failed");
    }
    check(!renderer.drawableRegionSurface(unseen.id) && !regionalSurface::queryCurrent(unseenQuery.x, unseenQuery.z).covered,
      "fully uploaded unsafe first proxy publishes neither visible geometry nor invisible collision");
    lowAircraft.height = unseenSurface->maxHeight + 1000.f;
    regionalSurface::setFlightContext(unseenActive, lowAircraft, 20.f, 50.f);
    renderer.beginRegionUploadFrame(); renderer.updateRegionHorizon({25., 145., 28000.}, seed + 1);
    const auto firstDrawable = renderer.drawableRegionSurface(unseen.id);
    const auto firstContact = regionalSurface::queryCurrent(unseenQuery.x, unseenQuery.z);
    check(firstDrawable && firstDrawable->key == unseenSurface->key && firstContact.covered && firstContact.key == firstDrawable->key,
      "safe clearance activates a previously absent GPU/collision pair atomically");
    renderer.cancelWorldUpload(); regionalSurface::clearFlightContext();
    check(largestUpload <= 256 * 1024, "all distant VBO transfers retain the small per-call bound");
    printf("Initial high-altitude neighborhood: ready=%d frames=%u elapsed_ms=%.3f\n", int(wanted.regions.size()), initialSettleFrames, initialSettleMs);
    printf("Horizon asynchronous settle: initial wanted=%zu, final wanted=%d, resident/peak=%d/%d, jobs=%u builds=%zu bytes=%zu frames=%u elapsed_ms=%.3f worst_update_ms=%.3f\n",
      wanted.regions.size(), renderer.horizonWantedMeshes, renderer.horizonResidentMeshes, renderer.horizonPeakResidentMeshes,
      renderer.horizonJobsStarted, renderer.horizonCpuBuildCount, renderer.horizonUploadBytesTotal, horizonFrames,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(), worstHorizonUpdate);
    printf("Resident collision grids: %zu bytes, pending=%zu bytes, published=%zu bytes\n",
      renderer.horizonCpuGridBytes, renderer.horizonPendingGridBytes, regionalSurface::residentGridBytes());
    printf("Publication CPU ms: begin=%.3f cancel=%.3f commit=%.3f\n", renderer.worldUploadBeginMs, renderer.worldUploadCancelMs, renderer.worldUploadCommitMs);
    printf("PASS world upload: %zu bytes in %u budgeted frames; CPU isolation, empty allocations, 256 KiB batches, ready-only publication, cancellation, failure recovery, bounded handles\n", allBytes, frames);
    return 0;
  } catch (const std::exception& e) { fprintf(stderr, "FAIL world upload: %s\n", e.what()); return 1; }
}
