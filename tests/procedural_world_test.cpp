// Production-world integration checks for seeded Earth-addressed regions.
// One materialized region per process. The companion report runner checks fresh
// builds against one another in separate processes, without keeping two worlds.
#include "../src/procedural_regions.h"
#include "../src/airport_layout.h"
#include "../src/scenery.h"
#include "../src/hive_world_collision.h"
#include "../src/aircraft.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <exception>
#include <thread>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
constexpr const char* kStamp = "procedural-world-test-v1";
constexpr const char* kSolaceStamp = "somnium-test-run";
int checks = 0, failures = 0;
void check(bool good, const std::string& what) {
  ++checks;
  if (!good) { if (failures < 40) std::printf("FAIL: %s\n", what.c_str()); ++failures; }
}
bool sameFile(const std::string& a, const std::string& b, uint64_t& bytes) {
  FILE* left = std::fopen(a.c_str(), "rb"); FILE* right = std::fopen(b.c_str(), "rb");
  bytes = 0;
  if (!left || !right) { if (left) std::fclose(left); if (right) std::fclose(right); return false; }
  std::array<unsigned char, 65536> x{}, y{};
  bool equal = true;
  for (;;) {
    const size_t nx = std::fread(x.data(), 1, x.size(), left), ny = std::fread(y.data(), 1, y.size(), right);
    if (nx != ny || std::memcmp(x.data(), y.data(), nx)) { equal = false; break; }
    bytes += nx;
    if (nx < x.size()) { equal = !std::ferror(left) && !std::ferror(right); break; }
  }
  std::fclose(left); std::fclose(right); return equal && bytes > 0;
}
template<class T> size_t capacityBytes(const std::vector<T>& v) { return v.capacity() * sizeof(T); }
size_t worldBytes(const World& w) {
  size_t n = sizeof(w) + capacityBytes(w.airports) + capacityBytes(w.towns) + capacityBytes(w.boxes) +
    capacityBytes(w.hm) + capacityBytes(w.mask) + capacityBytes(w.tpV0) + capacityBytes(w.roads.nodes) +
    capacityBytes(w.roads.paths) + capacityBytes(w.roadGrid.segs) + capacityBytes(w.roadGrid.head) +
    capacityBytes(w.roadGrid.list) + capacityBytes(w.roadGrid.nearHead) + capacityBytes(w.roadGrid.nearList) +
    capacityBytes(w.settlements) + capacityBytes(w.farms) + capacityBytes(w.lake.m);
  for (const auto& p : w.roads.paths) n += capacityBytes(p.pts) + capacityBytes(p.bridge);
  for (const auto& f : w.settlements) n += capacityBytes(f.effort);
  for (const auto& v : w.hmax) n += capacityBytes(v);
  for (const auto& v : w.tpM) n += capacityBytes(v);
  if (w.regionRecipe) n += w.regionRecipe->memoryBytes();
  if (w.sceneryState) {
    const auto& s = *w.sceneryState;
    n += sizeof(s) + capacityBytes(s.lots) + capacityBytes(s.trees) + capacityBytes(s.civic) +
      capacityBytes(s.furniture.items) + capacityBytes(s.furniture.chunk) + capacityBytes(s.landmarks.items) +
      capacityBytes(s.landmarks.chunk) + capacityBytes(s.clearings) + capacityBytes(s.ferries);
    for (const auto& entry : s.lotCells) n += capacityBytes(entry.second);
    for (const auto& entry : s.treeCells) n += capacityBytes(entry.second);
    for (const auto& ferry : s.ferries) n += capacityBytes(ferry.pts) + capacityBytes(ferry.along);
  }
  return n;
}
uint64_t activeFingerprint(const World& world) {
  uint64_t h = 1469598103934665603ULL;
  auto bytes = [&](const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
  };
  auto vector = [&](const auto& v) { const size_t n = v.size(); bytes(&n, sizeof(n)); if (n) bytes(v.data(), n * sizeof(v[0])); };
  vector(world.hm); vector(world.mask); vector(world.tpV0); vector(world.roadGrid.segs);
  vector(world.roadGrid.head); vector(world.roadGrid.list); vector(world.roadGrid.nearHead); vector(world.roadGrid.nearList);
  for (const auto& v : world.hmax) vector(v);
  for (const auto& v : world.tpM) vector(v);
  vector(world.roads.nodes);
  for (const auto& p : world.roads.paths) { vector(p.pts); vector(p.bridge); }
  const auto& scenery = sceneryState(world);
  vector(scenery.lots); vector(scenery.trees); vector(scenery.civic);
  vector(scenery.furniture.items); vector(scenery.furniture.chunk);
  vector(scenery.landmarks.items); vector(scenery.landmarks.chunk); vector(scenery.clearings);
  for (const auto& entry : scenery.lotCells) { bytes(&entry.first, sizeof(entry.first)); vector(entry.second); }
  for (const auto& entry : scenery.treeCells) { bytes(&entry.first, sizeof(entry.first)); vector(entry.second); }
  for (const auto& ferry : scenery.ferries) { vector(ferry.pts); vector(ferry.along); }
  return h;
}
void checkRecipe(const procedural::Descriptor& descriptor) {
  procedural::Recipe a(descriptor), b(descriptor);
  bool same = true, finite = true;
  for (int i = 0; i < 4096; ++i) {
    const float x = -WORLD_HALF + float((i * 7919) % 80001);
    const float z = -WORLD_HALF + float((i * 104729) % 80001);
    float p[4], q[4]; a.sample(x, z, p); b.sample(x, z, q);
    same = same && !std::memcmp(p, q, sizeof(p));
    for (float v : p) finite = finite && std::isfinite(v);
  }
  check(same && finite, "same descriptor gives finite bit-identical analytic terrain");
  check(a.airports.size() == b.airports.size() && a.towns.size() == b.towns.size(), "stable recipe metadata counts");
  for (size_t i = 0; i < std::min(a.airports.size(), b.airports.size()); ++i) {
    const Airport& p = a.airports[i]; const Airport& q = b.airports[i];
    check(!std::strcmp(p.code, q.code) && !std::strcmp(p.name, q.name) && p.x == q.x && p.z == q.z &&
      p.elev == q.elev && p.heading == q.heading && p.length == q.length && p.width == q.width,
      "stable recipe airport " + std::to_string(i));
  }
  // World copies must keep the char pointers alive through their shared recipe,
  // without needing to retain the staging World or a copied global terrain map.
  World copied;
  std::string code, town;
  {
    World owner; owner.regionRecipe = std::make_shared<procedural::Recipe>(descriptor);
    owner.airports = owner.regionRecipe->airports; owner.towns = owner.regionRecipe->towns;
    if (!owner.airports.empty()) code = owner.airports.front().code;
    if (!owner.towns.empty()) town = owner.towns.front().name;
    copied = owner;
  }
  check(copied.regionRecipe && !copied.airports.empty() && code == copied.airports.front().code,
    "airport string ownership survives staging-world destruction and World copy");
  check(!copied.towns.empty() && town == copied.towns.front().name,
    "town string ownership survives staging-world destruction and World copy");
  World layout; layout.airports = a.airports;
  for (const Town& t : a.towns)
    check(airportInfluence(layout, t.x, t.z) <= .02f,
      std::string(t.name) + " recipe town center is outside airport settlement exclusion");
  check(a.memoryBytes() < 256u * 1024u, "analytic recipe stays below 256 KiB");
}
void checkAirports() {
  std::set<std::string> codes;
  size_t objects = 0, solidCollisions = 0;
  float worstFlatness = 0, worstApproachExcess = -1e9f;
  check(!g_world.airports.empty(), "regional airports exist");
  for (size_t ai = 0; ai < g_world.airports.size(); ++ai) {
    const Airport& a = g_world.airports[ai]; const std::string label = a.code;
    check(a.code && a.name && codes.insert(a.code).second, label + " unique, owned airport identity");
    check(a.elev > 1 && a.length >= 400 && a.width >= 15, label + " usable dry runway dimensions");
    const AptLayout layout = aptLayout(a, int(ai));
    float flatness = 0;
    for (int along = 0; along <= 32; ++along) for (int cross = -1; cross <= 1; ++cross) {
      const vec3 p = aptWorld(a, a.length * (float(along) / 32 - .5f), a.width * .5f * cross, 0);
      flatness = std::max(flatness, std::abs(g_world.height(p.x, p.z) - a.elev));
      check(g_world.onRunway(p.x, p.z, .05f) == int(ai), label + " runway lookup follows generated geometry");
    }
    worstFlatness = std::max(worstFlatness, flatness);
    check(flatness < .15f, label + " complete runway is flat within 15 cm");
    // Test the apron and taxiway, where airplanes actually leave the runway.
    for (float u : {layout.apU0 + 8.f, (layout.apU0 + layout.apU1) * .5f, layout.apU1 - 8.f})
      for (float v : {layout.apV0 + 5.f, (layout.apV0 + layout.apV1) * .5f, layout.apV1 - 5.f}) {
        const vec3 p = aptWorld(a, u, v * layout.side, 0);
        check(std::abs(g_world.height(p.x, p.z) - a.elev) < .2f, label + " parking/apron pad flatness");
      }
    if (layout.paved) for (int i = 0; i <= 12; ++i) {
      const vec3 p = aptWorld(a, (a.length - 60.f) * (float(i) / 12.f - .5f), layout.twV * layout.side, 0);
      check(std::abs(g_world.height(p.x, p.z) - a.elev) < .2f, label + " taxiway pad flatness");
    }
    for (int side : {-1, 1}) for (int i = 1; i <= 20; ++i) {
      const float d = i * 250.f;
      for (float across : {-.5f, 0.f, .5f}) {
        const vec3 p = aptWorld(a, side * (a.length * .5f + d), across * (250.f + .18f * d), 0);
        const float ceiling = a.elev + 10.f + (a.elev > 600 ? .045f : .032f) * d;
        const float excess = g_world.height(p.x, p.z) - ceiling;
        worstApproachExcess = std::max(worstApproachExcess, excess);
        check(excess < 1.f, label + " terrain remains below approach funnel");
      }
    }
    std::vector<AptItem> items, repeat; airportItems(int(ai), items); airportItems(int(ai), repeat);
    check(items.size() >= (layout.paved ? 60u : 8u), label + " physical airport scenery is populated");
    bool deterministic = items.size() == repeat.size();
    for (size_t i = 0; deterministic && i < items.size(); ++i)
      deterministic = items[i].kind == repeat[i].kind && !std::memcmp(&items[i].e, &repeat[i].e, sizeof(Ent));
    check(deterministic, label + " physical airport placement is deterministic");
    objects += items.size();
    bool collision = false;
    for (const auto& item : items) {
      if (item.kind != EK_HANGAR && item.kind != EK_TERMINAL && item.kind != EK_ARCH_HANGAR && item.kind != EK_FBO) continue;
      const Ent& e = item.e; const auto& spec = kEntInfo[item.kind];
      collision = g_scenery.collide(vec3(e.x, e.y + spec.h * e.sy * .4f, e.z), .25f) != 0;
      if (collision) { ++solidCollisions; break; }
    }
    check(collision, label + " airport building exists in production collision scenery");
    for (int i = 0; i <= 8; ++i) {
      vec3 p = aptWorld(a, a.length * (float(i) / 8 - .5f), 0, a.elev + 3.f);
      check(g_scenery.collide(p, 1.f) == 0, label + " runway centerline clear of scenery");
    }
    vec3 above(a.x, a.elev + 5.f, a.z), below(a.x, a.elev - 5.f, a.z);
    const float hit = hiveWorldSweep(above, below, 0);
    check(hit >= .45f && hit <= .55f, label + " runway terrain stops a production world sweep");
  }
  std::printf("AIRPORTS count=%zu objects=%zu solid_collisions=%zu max_flatness_m=%.6f max_approach_excess_m=%.6f\n",
    g_world.airports.size(), objects, solidCollisions, worstFlatness, worstApproachExcess);
}
void checkSettlementsAndRoads() {
  check(!g_world.towns.empty() && g_world.settlements.size() == g_world.towns.size(), "regional settlements materialize from their own town definitions");
  check(!g_world.roads.paths.empty() && !g_world.roadGrid.segs.empty(), "physical regional roads are generated and indexed");
  for (const auto& town : g_world.towns)
    check(g_world.height(town.x, town.z) > 2.f && !g_world.underLake(town.x, town.z), std::string(town.name) + " town center is dry");
  std::vector<int> lotsPerTown(g_world.towns.size(), 0), collisionPerTown(g_world.towns.size(), 0);
  size_t dryLots = 0;
  for (const Lot& lot : settlementLots()) {
    if (!lot.present) continue;
    check(lot.town >= 0 && size_t(lot.town) < g_world.towns.size(), "lot has regional town identity");
    if (lot.town < 0 || size_t(lot.town) >= g_world.towns.size()) continue;
    ++lotsPerTown[lot.town];
    const bool dry = lot.ground > 0 && g_world.height(lot.cx, lot.cz) > 0 && !g_world.underLake(lot.cx, lot.cz);
    check(dry, "building lot is on dry ground"); dryLots += dry;
    if (collisionPerTown[lot.town] >= 2) continue;
    const auto* chunk = g_scenery.ensure(Scenery::chunkOf(lot.cx), Scenery::chunkOf(lot.cz), 2);
    bool found = false;
    if (chunk && lot.kind >= 0 && lot.kind < EK_COUNT) for (uint32_t i = chunk->off[lot.kind]; i < chunk->off[lot.kind + 1]; ++i) {
      const Ent& e = chunk->ents[i];
      if (std::abs(e.x - lot.cx) > .1f || std::abs(e.z - lot.cz) > .1f) continue;
      const vec3 p(e.x, e.y + kEntInfo[lot.kind].h * e.sy * .4f, e.z);
      found = g_scenery.collide(p, .25f) != 0;
      if (found) { ++collisionPerTown[lot.town]; break; }
    }
    check(found, "regional lot materializes as a collidable production building");
  }
  for (size_t i = 0; i < g_world.towns.size(); ++i) {
    check(lotsPerTown[i] > 0, std::string(g_world.towns[i].name) + " has actual building lots");
    check(collisionPerTown[i] > 0, std::string(g_world.towns[i].name) + " has physical building collisions");
  }
  size_t roadProbes = 0;
  for (size_t i = 0; i < g_world.roadGrid.segs.size() && roadProbes < 64; ++i) {
    const auto& r = g_world.roadGrid.segs[i];
    if (r.flags & RS_BRIDGE) continue;
    const float x = (r.ax + r.bx) * .5f, z = (r.az + r.bz) * .5f, h = g_world.height(x, z);
    check(std::isfinite(h) && h > 0, "road midpoint has a physical dry surface");
    check(roadEdgeDistance(g_world.roadGrid, x, z) <= .1f, "generated road centerline is in production road index");
    const float hit = hiveWorldSweep(vec3(x, h + 4, z), vec3(x, h - 4, z), 0);
    check(hit >= 0 && hit <= .55f, "generated road surface or its furniture stops a production world sweep");
    ++roadProbes;
  }
  check(roadProbes >= 10, "at least ten nonbridge road collision probes exercised");
  std::printf("SETTLEMENTS towns=%zu dry_lots=%zu road_paths=%zu road_segments=%zu road_collision_probes=%zu\n",
    g_world.towns.size(), dryLots, g_world.roads.paths.size(), g_world.roadGrid.segs.size(), roadProbes);
}
void checkWorldBounds() {
  check(g_world.hm.size() == size_t(HM_N) * HM_N * 4 && g_world.mask.size() == size_t(MASK_N) * MASK_N * 4,
    "one fixed-size materialized terrain and scenery mask");
  bool finite = true, ocean = true, periodic = true, remoteOcean = true;
  for (int i = 0; i < 4096; ++i) {
    float x = -50000.f + float((i * 7919) % 100000), z = -50000.f + float((i * 104729) % 100000);
    const float h = g_world.height(x, z);
    finite = finite && std::isfinite(h);
    periodic = periodic && h == g_world.height(x + WRAP_SPAN, z) && h == g_world.height(x, z - WRAP_SPAN);
    remoteOcean = remoteOcean && g_world.height(x + WRAP_SPAN, z) == -SEA_DEPTH &&
      g_world.height(x - WRAP_SPAN, z) == -SEA_DEPTH && g_world.height(x, z + WRAP_SPAN) == -SEA_DEPTH &&
      g_world.height(x, z - WRAP_SPAN) == -SEA_DEPTH;
    if (std::max(std::abs(x), std::abs(z)) >= WORLD_HALF + 3000) ocean = ocean && h == -SEA_DEPTH;
  }
  check(finite, "regional terrain samples are finite");
  check(g_world.repeating == !g_world.procedural, "procedural Earth chart is nonrepeating; authored career chart retains wrapping");
  if (g_world.repeating) check(periodic, "authored terrain retains the exact local-wrap lookup contract");
  else {
    check(remoteOcean, "regional islands do not repeat at the old 100 km chart period");
    for (const Airport& a : g_world.airports) {
      const vec3 remote(a.x + WRAP_SPAN, a.elev, a.z);
      check(g_world.height(remote.x, remote.z) == -SEA_DEPTH, "remote airport image is ocean");
      check(g_world.onRunway(remote.x, remote.z) < 0, "remote airport image has no repeated runway");
      const float collision = hiveWorldSweep(remote + vec3(0, 4, 0), remote - vec3(0, 4, 0), 0);
      check(collision > 1.f, "remote airport image has no phantom terrain/scenery collision");
      std::vector<AptItem> items; airportItems(g_world.findAirport(a.code), items);
      for (const auto& item : items) {
        if (item.kind != EK_HANGAR && item.kind != EK_TERMINAL && item.kind != EK_ARCH_HANGAR && item.kind != EK_FBO) continue;
        const Ent& e = item.e;
        const vec3 center(e.x + WRAP_SPAN, e.y + kEntInfo[item.kind].h * e.sy * .4f, e.z);
        check(hiveWorldSweep(center + vec3(0, 1, 0), center - vec3(0, 1, 0), .25f) > 1.f,
          "remote airport building image has no wrapped scenery collision");
        break;
      }
    }
  }
  check(ocean, "regional outer safety band is open ocean");
  const size_t bytes = worldBytes(g_world);
  check(bytes < size_t(512) * 1024 * 1024, "one-region owned terrain/index allocation estimate stays below 512 MiB");
  std::printf("MEMORY world_owned_capacity_bytes=%zu recipe_bytes=%zu streamed_chunks=%zu\n", bytes,
    g_world.regionRecipe ? g_world.regionRecipe->memoryBytes() : 0, g_scenery.generated());
  // Estimate excludes transient generation allocations, hash-map node overhead, bridge internals and
  // process/GPU resources. The runner reports actual peak process RSS separately.
}
void checkChartTopologyFlight() {
  Weather weather; weather.windSpeed = weather.gust = weather.turbulence = 0;
  weather.storm = false; weather.precip = 0;
  const AircraftSpec& spec = kAircraft[0];
  for (int axis = 0; axis < 2; ++axis) for (int sign : {-1, 1}) {
    vec3 start(45000.f, 2000.f, 45000.f);
    (axis == 0 ? start.x : start.z) = sign * (WRAP_HALF - 1.f);
    const float heading = axis == 0 ? (sign > 0 ? 90.f : 270.f) : (sign > 0 ? 180.f : 0.f);
    Plane plane; plane.reset(&spec, start, heading, spec.maxFuel * .5f, 0, true, std::max(60.f, spec.cruise));
    bool continuous = true;
    for (int i = 0; i < 20; ++i) {
      const vec3 before = plane.pos;
      plane.step(1.f / 60.f, weather, float(i) / 60.f);
      if (!g_world.repeating) continuous = continuous && length(plane.pos - before) < 10.f;
    }
    check(!plane.ev.crashed, "production Plane remains airborne while crossing chart boundary");
    const float along = axis == 0 ? plane.pos.x : plane.pos.z;
    if (g_world.repeating) {
      check(sign * along < -WRAP_HALF + 100.f && length(plane.seamShift) > WRAP_HALF,
        "authored career flight still wraps across chart boundary");
    } else {
      check(sign * along > WRAP_HALF && length(plane.seamShift) == 0 && continuous,
        "production Earth-region flight crosses old chart seam continuously without teleporting");
    }
  }
  std::printf("FLIGHT chart_repeating=%d boundary_crossings=4\n", int(g_world.repeating));
}
int finish() {
#if defined(__linux__) || defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    const auto peakKiB = static_cast<unsigned long long>(usage.ru_maxrss)
#ifdef __APPLE__
      / 1024
#endif
      ;
    std::printf("PROCESS peak_rss_kib=%llu\n", peakKiB);
  }
#endif
  std::printf("procedural_world_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
} // namespace
int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--verify-cache-stamp") {
    World cache;
    check(cache.loadCache(argv[2], argv[3]), "complete cache payload is valid under its supplied header stamp");
    return finish();
  }
  if (argc == 4 && std::string(argv[1]) == "--compare-caches") {
    uint64_t bytes = 0; check(sameFile(argv[2], argv[3], bytes), "freshly generated serialized worlds are byte-identical");
    std::printf("CACHE_COMPARE identical_bytes=%llu\n", static_cast<unsigned long long>(bytes)); return finish();
  }
  if (argc == 4 && std::string(argv[1]) == "--preserve-solace") {
    check(std::string(argv[2]) != argv[3], "baseline and fresh output are distinct paths");
    if (failures) return finish();
    FILE* f = std::fopen(argv[2], "rb"); check(f != nullptr, "unchanged baseline cache exists"); if (f) std::fclose(f);
    if (failures) return finish();
    g_world.build("", kSolaceStamp, false); // Explicitly no cache: this is a preservation test, not a reload test.
    check(!g_world.procedural && g_world.repeating && g_world.regionId == "solace" && !g_world.regionRecipe && !g_world.fromCache,
      "original Solace is freshly generated through its authored path");
    checkChartTopologyFlight();
    g_world.saveCache(argv[3], kSolaceStamp);
    uint64_t bytes = 0; check(sameFile(argv[2], argv[3], bytes), "every original Solace cache byte is preserved");
    std::printf("SOMNIUM_PRESERVATION identical_bytes=%llu airports=%zu towns=%zu\n", static_cast<unsigned long long>(bytes),
      g_world.airports.size(), g_world.towns.size()); return finish();
  }
  std::string id = "first", cache, activeCache;
  int threads = 0;
  bool expectCache = false, expectFresh = false, recipeOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--region" && i + 1 < argc) id = argv[++i];
    else if (arg == "--cache" && i + 1 < argc) cache = argv[++i];
    else if (arg == "--expect-cache") expectCache = true;
    else if (arg == "--expect-fresh") expectFresh = true;
    else if (arg == "--recipe-only") recipeOnly = true;
    else if (arg == "--active-cache" && i + 1 < argc) activeCache = argv[++i];
    else if (arg == "--threads" && i + 1 < argc) {
      char* end = nullptr; long value = std::strtol(argv[++i], &end, 10);
      if (!end || *end || value < 1 || value > 256) { std::fprintf(stderr, "--threads must be 1..256\n"); return 2; }
      threads = int(value);
    }
    else { std::fprintf(stderr, "usage: procedural_world_test [--region ID|first] [--cache FILE] [--expect-cache|--expect-fresh] [--recipe-only] [--threads N] [--active-cache SOMNIUM_CACHE]\n"
      "  --preserve-solace BASELINE_CACHE FRESH_OUTPUT_CACHE\n  --compare-caches CACHE_A CACHE_B\n"
      "  --verify-cache-stamp CACHE STAMP\n"); return 2; }
  }
  if (recipeOnly && id == "all") {
    for (const auto& d : procedural::recommendations()) if (!d.authored) {
      std::printf("RECIPE id=%s archetype=%s\n", d.id.c_str(), procedural::archetypeName(d.archetype));
      checkRecipe(d);
    }
    return finish();
  }
  if (id == "first") for (const auto& d : procedural::recommendations()) if (!d.authored) { id = d.id; break; }
  const auto descriptor = procedural::find(id);
  check(descriptor.has_value() && !descriptor->authored, "selected descriptor is a procedural Earth region");
  if (failures) return finish();
  checkRecipe(*descriptor);
  if (failures || recipeOnly) return finish();
  if (!activeCache.empty()) {
    FILE* file = std::fopen(activeCache.c_str(), "rb");
    check(file != nullptr, "active-world baseline cache exists"); if (file) std::fclose(file);
    if (failures) return finish();
    g_world.build(activeCache, kSolaceStamp, false);
    check(g_world.fromCache, "active authored world was loaded without regenerating the baseline");
    if (failures) return finish();
    const auto& airport = g_world.airports.front();
    g_scenery.ensure(Scenery::chunkOf(airport.x), Scenery::chunkOf(airport.z), 2);
  }
  const auto* activeScenery = g_world.sceneryState.get();
  const auto* activeHeight = g_world.hm.data();
  const uint64_t activeBefore = activeFingerprint(g_world);
  const size_t activeChunks = g_scenery.generated(), activeLots = settlementLots().size();
  const int activeStage = 731; g_worldStage.store(activeStage);
  const auto start = std::chrono::steady_clock::now();
  // Generate on a real worker while the main thread keeps reading its active
  // chart. The scoped budget must be entered on that worker (it is thread-local).
  World staged;
  std::atomic<bool> done{false}; std::exception_ptr error;
  std::thread worker([&]() {
    try {
      if (threads) { ScopedParallelLimit limit(threads); staged.buildProcedural(*descriptor, cache, kStamp, !cache.empty()); }
      else staged.buildProcedural(*descriptor, cache, kStamp, !cache.empty());
    } catch (...) { error = std::current_exception(); }
    done.store(true);
  });
  bool activeStable = true; size_t activeReads = 0;
  while (!done.load()) {
    activeStable = activeStable && g_worldStage.load() == activeStage && g_world.sceneryState.get() == activeScenery &&
      g_world.hm.data() == activeHeight && settlementLots().size() == activeLots && g_scenery.generated() == activeChunks;
    if (!g_world.airports.empty()) {
      const auto& airport = g_world.airports.front();
      activeStable = activeStable && std::abs(g_world.height(airport.x, airport.z) - airport.elev) < .15f;
    }
    ++activeReads;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  worker.join();
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (error) { try { std::rethrow_exception(error); } catch (const std::exception& e) { std::printf("FAIL: background build exception: %s\n", e.what()); } ++failures; return finish(); }
  check(activeStable && g_worldStage.load() == activeStage, "worker never changes active world, scenery, or g_worldStage during generation");
  check(activeBefore == activeFingerprint(g_world), "worker preserves every active terrain/scenery fingerprint byte");
  check(g_world.regionId == "solace" && !g_world.procedural, "building a staging World does not replace the active world");
  check(staged.sceneryState && staged.sceneryState.get() != activeScenery && !settlementLots(staged).empty(),
    "staging world owns populated scenery isolated from the active chart");
  check(parallelThreadLimit == 0, "worker thread budget never leaks into main-thread scheduling");
  std::printf("BACKGROUND threads_limit=%d active_chart_populated=%d active_reads=%zu active_chunks=%zu active_lots=%zu build_seconds=%.3f\n",
    threads, int(!activeCache.empty()), activeReads, activeChunks, activeLots, seconds);
  check(staged.procedural && staged.regionId == id && staged.regionRecipe, "staged world owns its regional metadata");
  if (expectCache) check(staged.fromCache, "matching descriptor and stamp replay the regional cache");
  if (expectFresh) check(!staged.fromCache, "fresh or descriptor-mismatched cache requires generation");
  const bool loaded = staged.fromCache;
  sceneryResetWorldCaches();
  g_world = std::move(staged);
  std::printf("REGION id=%s name=%s archetype=%s from_cache=%d build_seconds=%.3f\n", id.c_str(), descriptor->name.c_str(),
    procedural::archetypeName(descriptor->archetype), int(loaded), seconds);
  checkAirports(); checkSettlementsAndRoads(); checkWorldBounds(); checkChartTopologyFlight();
  g_scenery.clear();
  return finish();
}
