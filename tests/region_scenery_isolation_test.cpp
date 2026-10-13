// Background regional baking must not change the active flight's scenery tables.
// Flat synthetic worlds exercise the real bakes without generating full terrain.
#include "../src/scenery.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
struct Digest {
  uint64_t value = 1469598103934665603ull;
  template<class T> void add(const T& v) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(v); ++i) { value ^= bytes[i]; value *= 1099511628211ull; }
  }
  void entity(const PlacedEnt& p) { add(p.kind); add(p.e); }
};
uint64_t digest(const World& world) {
  Digest d;
  const auto& lots = settlementLots(world); d.add(lots.size());
  for (const Lot& l : lots) {
    d.add(l.present); d.add(l.cx); d.add(l.cz); d.add(l.hw); d.add(l.hd);
    d.add(l.wallH); d.add(l.roofH); d.add(l.ground); d.add(l.type); d.add(l.ridgeX);
    d.add(l.seed); d.add(l.yaw); d.add(l.town); d.add(l.kind); d.add(l.street); d.add(l.share);
  }
  const auto& trees = settlementTrees(world); d.add(trees.size());
  for (const TreeSpot& t : trees) { d.add(t.x); d.add(t.z); d.add(t.scale); d.add(t.kind); d.add(t.seed); }
  for (const auto* items : {&civicLandmarks(world), &roadFurniture(world), &landmarks(world)}) {
    d.add(items->size()); for (const auto& p : *items) d.entity(p);
  }
  const auto& clearings = landmarkClearings(world); d.add(clearings.size());
  for (const vec3& c : clearings) { d.add(c.x); d.add(c.y); d.add(c.z); }
  const auto& routes = ferryRoutes(world); d.add(routes.size());
  for (const FerryRoute& r : routes) {
    d.add(r.townA); d.add(r.townB); d.add(r.pts.size());
    for (const vec2& p : r.pts) { d.add(p.x); d.add(p.y); }
    for (float a : r.along) d.add(a);
  }
  const auto& state = sceneryState(world);
  for (const auto* chunks : {&state.furniture.chunk, &state.landmarks.chunk}) {
    d.add(chunks->size()); for (int c : *chunks) d.add(c);
  }
  return d.value;
}
void flatWorld(World& world, float x) {
  world.procedural = true; world.repeating = false;
  world.hm.assign((size_t)HM_N * HM_N * 4, 0.f);
  // Above the fort search's 35 m ceiling and below the stone-circle/wind farm floor.
  for (size_t i = 0; i < world.hm.size(); i += 4) { world.hm[i] = 60.f; world.hm[i + 2] = 0.6f; }
  world.towns = {{"Background town", x, 12000.f, 300.f, 1}};
  RoadPath p; p.cls = RC_ROAD;
  p.pts = {{x - 700.f, 12000.f, 60.f}, {x + 700.f, 12000.f, 60.f}}; p.bridge = {0};
  world.roads.paths.push_back(p);
  world.settlements = buildSettlementFields(world);
}
void bake(World& world) {
  sceneryBakeCommunityLots(world);
  sceneryBakeRoadFurniture(world);
  // The synthetic civic fixture makes the landmark build prove it reads this
  // world's earlier bake, rather than the active flight's civic collection.
  const float x = world.towns.front().x;
  sceneryStateForBuild(world).civic.push_back({EK_STADIUM, {x, 60.f, 12000.f, 0.f, 1.f, 1.f, 1.f, .5f}});
  sceneryBakeLandmarks(world);
}
bool indicesBelongTo(const World& world) {
  const auto& lots = settlementLots(world); const auto& trees = settlementTrees(world);
  std::vector<int> ids;
  for (size_t i = 0; i < lots.size(); ++i) {
    lotsIn(world, lots[i].cx - 1.f, lots[i].cz - 1.f, lots[i].cx + 1.f, lots[i].cz + 1.f, ids);
    if (std::find(ids.begin(), ids.end(), (int)i) == ids.end()) return false;
    for (int id : ids) if (id < 0 || id >= (int)lots.size()) return false;
  }
  for (size_t i = 0; i < trees.size(); ++i) {
    treesIn(world, trees[i].x - 1.f, trees[i].z - 1.f, trees[i].x + 1.f, trees[i].z + 1.f, ids);
    if (std::find(ids.begin(), ids.end(), (int)i) == ids.end()) return false;
    for (int id : ids) if (id < 0 || id >= (int)trees.size()) return false;
  }
  for (const auto* items : {&roadFurniture(world), &landmarks(world)}) {
    for (const PlacedEnt& p : *items) {
      const int cx = Scenery::chunkOf(p.e.x), cz = Scenery::chunkOf(p.e.z);
      const auto range = items == &roadFurniture(world) ? roadFurnitureIn(world, cx, cz) : landmarksIn(world, cx, cz);
      bool found = false;
      for (const PlacedEnt* q = range.first; q != range.second; ++q) found |= q == &p;
      if (!found) return false;
    }
  }
  return true;
}
bool activeWrappersMatch() {
  return &settlementLots() == &settlementLots(g_world) &&
         &settlementTrees() == &settlementTrees(g_world) &&
         &civicLandmarks() == &civicLandmarks(g_world) &&
         &roadFurniture() == &roadFurniture(g_world) &&
         &landmarks() == &landmarks(g_world) &&
         &landmarkClearings() == &landmarkClearings(g_world) &&
         &ferryRoutes() == &ferryRoutes(g_world);
}
}

int main() {
  World empty;
  check(settlementLots(empty).empty() && roadFurniture(empty).empty() && landmarks(empty).empty(), "unbuilt worlds have empty scenery");
  check(!empty.sceneryState, "read-only empty queries do not allocate or publish state");
  const auto emptyRange = landmarksIn(empty, 0, 0);
  check(emptyRange.first == nullptr && emptyRange.second == nullptr, "empty chunk queries have safe empty ranges");

  auto& active = sceneryStateForBuild(g_world);
  Lot lot{}; lot.present = true; lot.cx = 16.f; lot.cz = 24.f; lot.kind = EK_HOUSE;
  active.lots.push_back(lot); active.lotCells[0] = {0};
  active.trees.push_back({8.f, 12.f, 1.f, EK_OAK, .25f}); active.treeCells[0] = {0};
  const PlacedEnt sentinel{EK_STADIUM, {32.f, 60.f, 48.f, 0.f, 1.f, 1.f, 1.f, .75f}};
  active.civic.push_back(sentinel); active.landmarks.build({sentinel}); active.furniture.build({sentinel});
  active.clearings.push_back(vec3(32.f, 48.f, 105.f));
  FerryRoute route; route.townA = 0; route.townB = 1; route.pts = {{0.f, 0.f}, {100.f, 0.f}}; route.along = {0.f, 100.f};
  active.ferries.push_back(route);
  const uint64_t activeBefore = digest(g_world);
  const auto* activeState = g_world.sceneryState.get();
  check(activeWrappersMatch(), "all runtime wrappers resolve to the active World");

  World a, b; flatWorld(a, -12000.f); flatWorld(b, 12000.f);
  bake(a); bake(b);
  const uint64_t aBefore = digest(a), bBefore = digest(b);
  check(!settlementLots(a).empty() && !settlementLots(b).empty(), "both synthetic towns generate real lots");
  check(!roadFurniture(a).empty() && !roadFurniture(b).empty(), "both synthetic roads generate furniture");
  check(!settlementTrees(a).empty() && !settlementTrees(b).empty(), "both synthetic towns generate planted trees");
  check(aBefore != bBefore, "different charts own different generated scenery");
  check(digest(g_world) == activeBefore, "sequential candidate baking leaves active scenery untouched");

  a.sceneryState.reset(); b.sceneryState.reset();
  std::atomic<int> finished{0}; std::atomic<bool> start{false};
  auto background = [&](World& world) {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    bake(world); finished.fetch_add(1, std::memory_order_release);
  };
  std::thread ta(background, std::ref(a)), tb(background, std::ref(b));
  start.store(true, std::memory_order_release);
  bool unchanged = true; int reads = 0;
  while (finished.load(std::memory_order_acquire) != 2) {
    unchanged &= digest(g_world) == activeBefore && activeWrappersMatch() && g_world.sceneryState.get() == activeState;
    std::vector<int> ids; lotsIn(0.f, 0.f, 32.f, 32.f, ids);
    unchanged &= ids == std::vector<int>{0};
    treesIn(0.f, 0.f, 32.f, 32.f, ids); unchanged &= ids == std::vector<int>{0};
    std::vector<std::pair<int, Ent>> boats; ferryInstances(1.f, boats); unchanged &= boats.size() == 1;
    ++reads; std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  ta.join(); tb.join();
  check(reads > 0 && unchanged && digest(g_world) == activeBefore, "active flight queries stay unchanged during simultaneous bakes");
  check(digest(a) == aBefore && digest(b) == bBefore, "parallel baking exactly matches each sequential result");
  check(a.sceneryState != b.sceneryState && a.sceneryState.get() != activeState && b.sceneryState.get() != activeState, "each built World owns independent scenery state");
  check(indicesBelongTo(a) && indicesBelongTo(b), "lot, tree, furniture and landmark indices belong to their own World");
  check(landmarks(a).size() == 1 && landmarks(a).front().e.x == -12000.f &&
        landmarks(b).size() == 1 && landmarks(b).front().e.x == 12000.f, "landmark baking reads its own civic landmarks");
  check(landmarkClearings(a).size() == 1 && landmarkClearings(a).front().x == -12000.f &&
        landmarkClearings(b).size() == 1 && landmarkClearings(b).front().x == 12000.f, "landmark clearings are chart-local");
  check(ferryRoutes(a).empty() && ferryRoutes(b).empty() && ferryRoutes().size() == 1, "background routes cannot clear active ferries");

  World copy = g_world;
  check(copy.sceneryState == g_world.sceneryState, "World copies initially share immutable scenery");
  sceneryBakeCommunityLots(copy);
  check(copy.sceneryState != g_world.sceneryState && settlementLots(copy).empty(), "rebaking a copied World detaches its state");
  check(digest(g_world) == activeBefore, "detaching a copied World preserves the active state");

  const auto* readyState = a.sceneryState.get();
  g_world = std::move(a);
  check(g_world.sceneryState.get() == readyState && digest(g_world) == aBefore, "publishing a candidate moves its exact prepared scenery");
  check(activeWrappersMatch() && settlementLots().front().cx < 0.f, "runtime queries follow the new World immediately after publication");
  check(digest(b) == bBefore, "publishing one candidate does not change another ready candidate");
  std::printf("regional scenery isolation: %d checks, %d failures; %d active-flight reads during parallel baking\n", checks, failures, reads);
  return failures ? 1 : 0;
}
