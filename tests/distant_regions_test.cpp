#include "../src/distant_regions.h"
#include "../src/region_travel.h"
#include "../src/scenery.h"
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; if (failures < 30) std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F&& function, const char* message) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument&) { rejected = true; }
  check(rejected, message);
}
bool finite(float x) {
  uint32_t bits; std::memcpy(&bits, &x, sizeof(bits));
  return (bits & 0x7f800000u) != 0x7f800000u;
}
bool finite(vec3 v) { return finite(v.x) && finite(v.y) && finite(v.z); }
uint64_t fingerprint(const horizon::Mesh& mesh) {
  uint64_t hash = 1469598103934665603ULL;
  for (const auto& v : mesh.vertices) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(v); ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
  }
  return hash;
}
std::vector<procedural::Descriptor> independentCatalog(uint64_t seed) {
  std::vector<procedural::Descriptor> out{procedural::solaceDescriptor(seed)};
  constexpr int side = 1 << procedural::kAddressLevel;
  for (int f = 0; f < 6; ++f) for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
    // The canonical ID lookup is independent of production's describe()/center
    // initialization; reserved Solace addresses correctly have no generated ID.
    const auto id = "pr1-" + std::to_string(f) + "-" + std::to_string(x) + "-" + std::to_string(y);
    if (auto d = procedural::find(id, seed)) out.push_back(*d);
  }
  return out;
}
double independentHorizon(double h) {
  // atan2(sqrt(h*(2R+h)),R) is the same horizon angle as acos(R/(R+h)),
  // evaluated independently and stably near sea level.
  h = std::max(0.0, h);
  return geo::radius() * std::atan2(std::sqrt(h * (2 * geo::radius() + h)), geo::radius());
}
std::vector<procedural::Descriptor> independentVisible(const std::vector<procedural::Descriptor>& all,
                                                      geo::Position p, double ahead = 0, double summit = 3000) {
  std::vector<procedural::Descriptor> out;
  const double horizon = independentHorizon(std::max(0.0, p.height) + ahead) + independentHorizon(summit);
  for (const auto& d : all) {
    const double half = d.authored ? double(WORLD_HALF) : double(procedural::kOceanEdge);
    if (geo::surfaceDistance(p, d.origin) <= horizon + std::hypot(half, half) + .001) out.push_back(d);
  }
  std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
    const double da = geo::surfaceDistance(p, a.origin), db = geo::surfaceDistance(p, b.origin);
    return da == db ? a.id < b.id : da < db;
  });
  return out;
}
std::vector<std::string> ids(const std::vector<procedural::Descriptor>& regions) {
  std::vector<std::string> out;
  for (const auto& d : regions) out.push_back(d.id);
  return out;
}
void checkSelection(const std::vector<procedural::Descriptor>& all, geo::Position p, double ahead, size_t limit = 64) {
  const auto expected = independentVisible(all, p, ahead);
  const auto a = horizon::select(p, all.front().worldSeed, limit, ahead);
  const auto b = horizon::select(p, all.front().worldSeed, limit, ahead);
  check(ids(a.regions) == ids(b.regions), "selection has deterministic distance/ID order");
  check(a.complete == (expected.size() <= std::min(limit, horizon::kMaxRegions)), "completeness reports every truncation");
  check(a.regions.size() == std::min({expected.size(), limit, horizon::kMaxRegions}), "exact bounded selected count");
  std::set<std::string> selected;
  double previous = -1;
  for (const auto& d : a.regions) {
    check(selected.insert(d.id).second, "no duplicated/reserved alias");
    const double distance = geo::surfaceDistance(p, d.origin);
    check(distance >= previous - .000001, "nearest-first distance order"); previous = distance;
    check(distance <= a.reachMetres + .001, "selected centers respect reported reach");
    check(d.worldSeed == all.front().worldSeed, "selected descriptors retain requested world seed");
  }
  if (a.complete) {
    for (const auto& d : expected) check(selected.count(d.id) != 0, "includes every independently counted visible center");
  } else if (!a.regions.empty()) {
    // Equal-distance poles can differ in the last round-off bit between the
    // independent surfaceDistance and production's cached-vector calculation.
    const double last = geo::surfaceDistance(p, a.regions.back().origin);
    for (const auto& d : expected) if (!selected.count(d.id))
      check(geo::surfaceDistance(p, d.origin) >= last - .000001, "truncation keeps nearest centers");
  }
}
void checkMesh(const procedural::Descriptor& d) {
  const auto mesh = horizon::buildMesh(d);
  check(mesh.region.id == d.id && mesh.region.seed == d.seed, "mesh preserves region identity");
  check(!mesh.vertices.empty() && mesh.vertices.size() % 3 == 0, "mesh contains complete land triangles");
  check(mesh.vertices.size() <= horizon::kMaxMeshVertices, "mesh vertex count bounded");
  check(mesh.vertices.capacity() * sizeof(horizon::Vertex) <= 3932160, "expanded allocation bounded to 3.75 MiB");
  const geo::LocalFrame frame(d.origin);
  const regionTravel::Chart chart(d.origin);
  std::unique_ptr<procedural::Recipe> recipe;
  if (!d.authored) recipe = std::make_unique<procedural::Recipe>(d);
  float top = 0;
  for (const auto& v : mesh.vertices) {
    check(finite(v.position) && finite(v.normal) && finite(v.colour) && finite(v.height), "all mesh channels finite");
    check(std::abs(length(v.normal) - 1.f) < .00002f && v.normal.y > 0, "outward finite unit normal");
    check(v.colour.x >= 0 && v.colour.x <= 1 && v.colour.y >= 0 && v.colour.y <= 1 &&
          v.colour.z >= 0 && v.colour.z <= 1, "mesh colours bounded");
    top = std::max(top, v.height);
  }
  for (size_t i = 0; i < mesh.vertices.size(); i += 3)
    check(std::max({mesh.vertices[i].height, mesh.vertices[i + 1].height, mesh.vertices[i + 2].height}) >= 0,
          "fully submerged triangles omitted");
  // Spot-check the inverse geography and pure five-octave source, independently
  // of the builder, including submerged corners on surviving shore triangles.
  for (size_t i = 0; i < mesh.vertices.size(); i += 173) {
    const auto& v = mesh.vertices[i];
    const auto p = frame.toPosition(v.position);
    const auto local = chart.local(p);
    const float cell = 2.f * WORLD_HALF / (horizon::kMeshResolution - 1);
    const float x = std::round((local.x + WORLD_HALF) / cell) * cell - WORLD_HALF;
    const float z = std::round((local.z + WORLD_HALF) / cell) * cell - WORLD_HALF;
    float b[4]; if (recipe) recipe->sample(x, z, b); else World::sampleAuthoredBase(x, z, b);
    const float expected = b[1] < .01f ? b[0] : b[0] + b[1] * terrainFbm(x / DETAIL_SCALE, z / DETAIL_SCALE, 5);
    check(std::abs(expected - v.height) < .001f, "mesh height comes directly from pure source");
    check(std::abs(p.height - std::max(v.height, -.5f)) < .01, "vertex uses curved ECEF placement and shore clamp");
    check(std::abs(local.x - x) < .01f && std::abs(local.z - z) < .01f, "vertex is on the 129-grid surface chart");
  }
  check(top < horizon::kSummitHeight, "sampled silhouette stays below conservative summit");
  if (!d.authored && d.archetype == procedural::Archetype::Atolls) check(top < 35.f, "atoll silhouette remains low");
  check(fingerprint(mesh) == fingerprint(horizon::buildMesh(d)), "fresh meshes are bitwise reproducible");
}
long long peakResidentBytes() {
#if defined(__unix__) || defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#if defined(__APPLE__)
  return usage.ru_maxrss;
#else
  return 1024LL * usage.ru_maxrss;
#endif
#else
  return -1;
#endif
}
int benchmark(int argc, char** argv) {
  if (argc != 6 && argc != 7) {
    std::cerr << "Usage: distant_regions_test --benchmark COUNT LAT LON HEIGHT [LOOKAHEAD]\n"; return 2;
  }
  const int count = std::stoi(argv[2]);
  if (count < 1 || count > int(horizon::kMaxRegions)) return 2;
  const geo::Position camera{std::stod(argv[3]), std::stod(argv[4]), std::stod(argv[5])};
  const double lookahead = argc == 7 ? std::stod(argv[6]) : 0;
  const auto started = std::chrono::steady_clock::now();
  const auto selected = horizon::select(camera, procedural::kDefaultWorldSeed, count, lookahead);
  const auto selectedAt = std::chrono::steady_clock::now();
  if (selected.regions.size() != size_t(count)) { std::cerr << "Viewpoint has fewer regions than requested\n"; return 2; }
  std::vector<horizon::Mesh> meshes; meshes.reserve(selected.regions.size());
  size_t resident = 0, grids = 0, vertices = 0;
  for (const auto& d : selected.regions) {
    meshes.push_back(horizon::buildMesh(d));
    resident += meshes.back().vertices.capacity() * sizeof(horizon::Vertex);
    if (meshes.back().surface)
      grids += meshes.back().surface->grid.capacity() * sizeof(regionalSurface::GridVertex);
    vertices += meshes.back().vertices.size();
  }
  const auto ended = std::chrono::steady_clock::now();
  check(g_world.hm.empty() && g_world.mask.empty() && g_world.tpV0.empty() && !g_world.sceneryState,
        "fresh far set allocates no full-World arrays or scenery");
  std::cout << "{\"regions\":" << meshes.size() << ",\"vertices\":" << vertices
            << ",\"cameraHeightMetres\":" << camera.height << ",\"altitudeLookaheadMetres\":" << lookahead
            << ",\"meshResidentBytes\":" << resident << ",\"collisionGridBytes\":" << grids
            << ",\"combinedMeshArrayBytes\":" << resident + grids << ",\"peakProcessResidentBytes\":" << peakResidentBytes()
            << ",\"catalogSelectionMs\":" << std::chrono::duration<double, std::milli>(selectedAt - started).count()
            << ",\"freshMeshBuildMs\":" << std::chrono::duration<double, std::milli>(ended - selectedAt).count()
            << ",\"worldHeightmapBytes\":" << g_world.hm.capacity() * sizeof(float)
            << ",\"completeAtRequestedCount\":" << (selected.complete ? "true" : "false") << "}\n";
  return failures ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--benchmark") return benchmark(argc, argv);
  const auto all = independentCatalog(procedural::kDefaultWorldSeed);
  check(all.size() > 6100 && all.size() <= 6145, "independent catalog includes complete geography");
  std::vector<geo::Position> positions{{0, 0, 0}, {32.5, -58, 0}, {0, 180, 0}, {0, -180, 0},
    {90, 0, 0}, {-90, 180, 0}, {89.999, -172, 0}, {-89.999, 45, 0}, {45, 45, 0}, {-45, -135, 0}};
  // Exact seams and all eight cube corners.
  for (int face = 0; face < 6; ++face) for (double edge : {-1., 1.}) {
    positions.push_back(geo::fromDirection(geo::faceDirection(geo::Face(face), edge, 0)));
    positions.push_back(geo::fromDirection(geo::faceDirection(geo::Face(face), 0, edge)));
    positions.push_back(geo::fromDirection(geo::faceDirection(geo::Face(face), edge, edge)));
    positions.push_back(geo::fromDirection(geo::faceDirection(geo::Face(face), edge, -edge)));
  }
  for (auto p : positions) for (double h : {-100., 0., 14000., 16000., 20000., 25000., 28000., 30000.}) {
    p.height = h;
    checkSelection(all, p, 0);
    checkSelection(all, p, 3000);
  }
  checkSelection(all, {31, 44, 1000000}, 3000);
  checkSelection(all, {31, 44, 1000000}, 3000, 1000000);
  checkSelection(all, {32.5, -58, 16000}, 3000, 0);
  checkSelection(all, {32.5, -58, 16000}, 3000, 1);
  check(!horizon::select({31, 44, 1000000}).complete, "high-altitude cap cannot silently claim whole horizon");
  check(ids(horizon::select({90, -170, 16000}).regions) == ids(horizon::select({90, 85, 16000}).regions), "pole longitude aliases");
  check(ids(horizon::select({-90, -170, 16000}).regions) == ids(horizon::select({-90, 85, 16000}).regions), "south-pole aliases");
  check(ids(horizon::select({0, 180, 16000}).regions) == ids(horizon::select({0, -180, 16000}).regions), "date-line aliases");
  check(ids(horizon::select({0, 0, 1000}, procedural::kDefaultWorldSeed, 64, 3000).regions) ==
        ids(horizon::select({0, 0, 4000}, procedural::kDefaultWorldSeed, 64, 0).regions), "lookahead preloads exact future altitude horizon");
  check(ids(horizon::select({0, 0, 16000}, procedural::kDefaultWorldSeed, 64, 0, 0).regions) ==
        ids(horizon::select({0, 0, 16000}, procedural::kDefaultWorldSeed, 64, 0, 3000).regions), "small requested summit cannot defeat conservative coverage");
  // Even an almost-zero positive altitude has a nonzero horizon. acos(R/(R+h))
  // would round this one to zero and incorrectly drop this boundary descriptor.
  auto tiny = geo::destination(all[1].origin, 23,
    independentHorizon(1e-10) + independentHorizon(3000) + std::hypot(38000., 38000.) - .0005);
  tiny.height = 1e-10;
  const auto tinyRegions = ids(horizon::select(tiny, procedural::kDefaultWorldSeed, 64, 0).regions);
  check(std::find(tinyRegions.begin(), tinyRegions.end(), all[1].id) != tinyRegions.end(),
        "near-zero altitude cannot lose a boundary-visible region through cancellation");
  for (uint64_t seed : std::array<uint64_t, 3>{0, 999, UINT64_MAX}) {
    const auto seeded = independentCatalog(seed);
    checkSelection(seeded, {58, 179.99, 16000}, 3000);
  }
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  for (auto p : {geo::Position{nan, 0, 0}, {0, inf, 0}, {0, 0, nan}, {91, 0, 0}, {0, 181, 0}, {0, 0, -geo::radius()}})
    rejects([&] { horizon::select(p); }, "malformed viewer rejected before catalog math");
  for (double bad : {-1., nan, inf}) {
    rejects([&] { horizon::select({}, 1, 64, bad); }, "malformed altitude lookahead rejected");
    rejects([&] { horizon::select({}, 1, 64, 3000, bad); }, "malformed summit option rejected");
  }
  const auto huge = horizon::select({0, 0, std::numeric_limits<double>::max()}, 1, 64,
                                    std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
  check(huge.regions.size() == 64 && !huge.complete, "enormous finite options stay capped without overflow");

  // Deliberately recognizable live-world state: far work must leave it alone.
  g_world.regionId = "live-world-sentinel";
  g_world.hm = {1, 2, 3, 4}; g_world.mask = {5, 6}; g_world.tpV0 = {7, 8};
  g_world.sceneryState = std::make_shared<WorldSceneryState>();
  g_world.sceneryState->clearings = {vec3(13, 14, 15)};
  const auto* state = g_world.sceneryState.get();
  const auto* heightmap = g_world.hm.data();
  std::vector<procedural::Descriptor> forms{procedural::solaceDescriptor()};
  std::set<procedural::Archetype> seen;
  for (const auto& d : procedural::recommendations()) if (!d.authored && seen.insert(d.archetype).second) forms.push_back(d);
  check(forms.size() == 6, "authored Solace and all five generated forms tested");
  for (const auto& d : forms) checkMesh(d);
  const auto target = forms.back();
  for (int bad : {-100, 0, 1, 130, 2147483647})
    rejects([&] { horizon::buildMesh(target, bad); }, "unbounded/invalid mesh resolution rejected");
  for (int field = 0; field < 7; ++field) {
    auto bad = target;
    if (field == 0) bad.seed ^= 1;
    if (field == 1) bad.authored = true;
    if (field == 2) bad.origin.latitude = nan;
    if (field == 3) bad.origin.height = 5;
    if (field == 4) bad.address.x = 999;
    if (field == 5) bad.generatorVersion = 2;
    if (field == 6) bad.id = "../invalid";
    rejects([&] { horizon::buildMesh(bad); }, "noncanonical descriptor rejected");
  }
  auto fakeSolace = procedural::solaceDescriptor(); fakeSolace.seed = 1;
  rejects([&] { horizon::buildMesh(fakeSolace); }, "authored descriptor also validated");
  check(horizon::buildMesh(target, 2).vertices.size() <= 6, "minimum mesh remains bounded");
  std::vector<std::future<uint64_t>> workers;
  for (int i = 0; i < 3; ++i) workers.push_back(std::async(std::launch::async, [target] { return fingerprint(horizon::buildMesh(target)); }));
  const auto digest = fingerprint(horizon::buildMesh(target));
  for (auto& worker : workers) check(worker.get() == digest, "simultaneous mesh builds are deterministic");
  check(g_world.regionId == "live-world-sentinel" && g_world.hm.data() == heightmap &&
        g_world.hm == std::vector<float>({1, 2, 3, 4}) && g_world.mask == std::vector<uint8_t>({5, 6}) &&
        g_world.tpV0 == std::vector<float>({7, 8}), "far work never replaces/mutates live World arrays");
  check(g_world.sceneryState.get() == state && state->clearings.size() == 1 &&
        state->clearings.front().x == 13 && state->clearings.front().y == 14 && state->clearings.front().z == 15 &&
        state->lots.empty() && state->trees.empty() && state->furniture.items.empty() && state->landmarks.items.empty(),
        "far work never mutates live scenery caches");
  std::cout << "distant_regions_test: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
