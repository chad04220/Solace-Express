// Region scenery uses the World passed to it, and active entity caches do not survive a world switch.
#include "../src/scenery.h"
#include "../src/airport_layout.h"
#include <cstdio>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
void flatWorld(World& world) {
  world.procedural = true;
  world.hm.assign((size_t)HM_N * HM_N * 4, 0.f);
  for (size_t i = 0; i < world.hm.size(); i += 4) { world.hm[i] = 30.f; world.hm[i + 2] = 0.6f; }
}
Airport airfield(float x, float z) {
  return {"ZZ01", "Region test field", x, z, 30.f, 0.f, 800.f, 24.f, SURF_GRASS, 0, "Test field"};
}
RoadPath road(float ax, float az, float bx, float bz) {
  RoadPath path; path.cls = RC_ROAD;
  path.pts = {{ax, az, 30.f}, {bx, bz, 30.f}};
  path.bridge = {0};
  return path;
}
bool sameEnt(const Ent& a, const Ent& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.yaw == b.yaw &&
         a.sx == b.sx && a.sy == b.sy && a.sz == b.sz && a.seed == b.seed;
}
bool sameItems(const std::vector<AptItem>& a, const std::vector<AptItem>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) if (a[i].kind != b[i].kind || !sameEnt(a[i].e, b[i].e)) return false;
  return true;
}
bool contains(const Scenery::Chunk& chunk, const AptItem& item) {
  for (uint32_t i = chunk.off[item.kind]; i < chunk.off[item.kind + 1]; ++i) {
    const Ent& e = chunk.ents[i];
    if (sameEnt(e, item.e)) return true;
  }
  return false;
}
}

int main() {
  World local;
  flatWorld(local);
  local.towns = {{"Region Town", 12000.f, 12000.f, 300.f, 1}, {"Region Village", 14500.f, 12000.f, 240.f, 0}};
  local.roads.paths.push_back(road(11700.f, 12000.f, 14800.f, 12000.f));
  local.settlements = buildSettlementFields(local);
  check(local.settlements.size() == 2, "settlement count comes from the supplied world");
  check(local.settlements[0].cx == local.towns[0].x && local.settlements[1].cz == local.towns[1].z,
        "settlement centres come from the supplied world");
  check(settlementShare(local.settlements, 12000.f, 12000.f) < 0.1f, "the custom town grows on its own terrain");
  sceneryBakeCommunityLots(local);
  check(!settlementLots(local).empty(), "local-world road generates existing building assets");
  bool localLots = true;
  for (const Lot& lot : settlementLots(local)) localLots &= lot.town >= 0 && lot.town < 2 && lot.cx > 11000.f && lot.cx < 15500.f;
  check(localLots, "every lot belongs to a town in the supplied world");

  const RoadNetwork network = buildRoadNetwork(local);
  check(network.nodes.size() == local.towns.size(), "road node count comes from the supplied towns");
  check(network.nodes[0].x == local.towns[0].x && network.nodes[1].z == local.towns[1].z,
        "road endpoints belong to custom towns rather than Solace");
  check(!network.paths.empty(), "custom towns receive a terrain-routed road");

  local.airports.push_back(airfield(12000.f, 12000.f));
  check(airportInfluence(local, 12000.f, 12000.f) == 1.f, "custom airport protects its own grounds");
  const RoadPoint a{12000.f, 11950.f, 30.f}, b{12000.f, 12050.f, 30.f};
  check(roadByAirfield(local, a, b, RC_ROAD), "road airfield protection uses the supplied world");
  check(!roadByAirfield(a, b, RC_ROAD), "legacy road helper retains original Solace airport bounds");
  local.settlements = buildSettlementFields(local);
  check(settlementShare(local.settlements, 12000.f, 12000.f) > 1.f, "custom settlement avoids its own runway grounds");
  RoadNetwork crossing; crossing.paths.push_back(road(a.x, a.z, b.x, b.z));
  buildRoadGrid(local, crossing, local.roadGrid);
  check(!local.roadGrid.segs.empty() && (local.roadGrid.segs[0].flags & RS_NOGRADE), "custom runway road is painted without grading");

  local.towns = {{"Solace Capital", 12000.f, 12000.f, 300.f, 1}, {"Fjordhaven", 14500.f, 12000.f, 240.f, 0}};
  local.airports.clear();
  local.settlements = buildSettlementFields(local);
  sceneryBakeCommunityLots(local);
  check(civicLandmarks(local).empty(), "procedural town names cannot clone authored Solace civic landmarks");
  Reservoir reservoir;
  check(!findReservoir(local, reservoir), "authored reservoir is excluded from procedural regions");
  local.towns[0].name = "Lighthouse Key";
  float x = 0.f, z = 0.f, yaw = 0.f;
  check(!findLighthouseSite(local, x, z, yaw), "authored lighthouse is excluded from procedural regions");

  sceneryResetWorldCaches();
  g_world = std::move(local);
  g_world.towns.clear(); g_world.settlements.clear(); g_world.roads = {}; g_world.roadGrid = {};
  g_world.mask.assign((size_t)MASK_N * MASK_N * 4, 0);
  g_world.roadGrid.head.assign((size_t)MASK_N * MASK_N, 0);
  sceneryBakeCommunityLots(g_world);
  check(communityAt(kTowns[0].x, kTowns[0].z) == -1, "empty region does not inherit Solace communities");
  g_world.towns = {{"Current Town", 12000.f, 12000.f, 300.f, 1}};
  check(communityAt(12000.f, 12000.f) == 0, "runtime community lookup follows the active world's towns");
  check(communityAt(kTowns[0].x, kTowns[0].z) == -1, "runtime community lookup has no original-town fallback");
  g_world.towns.clear();

  for (float airportX : {12000.f, -12000.f, 12000.f}) {
    sceneryResetWorldCaches();
    g_world.airports = {airfield(airportX, 12000.f)};
    std::vector<AptItem> items;
    airportItems(0, items);
    bool verified = false;
    for (const AptItem& item : items) {
      if (item.kind != EK_ARCH_HANGAR && item.kind != EK_HANGAR) continue;
      Scenery::Chunk* chunk = g_scenery.ensure(Scenery::chunkOf(item.e.x), Scenery::chunkOf(item.e.z), 1);
      verified = chunk && contains(*chunk, item);
      break;
    }
    check(verified, "A-B-A region switch rebuilds airport fixture cache for the active region");
    // Exercise clearing while workers may still be using the old world's airport cache.
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx)
        g_scenery.request(Scenery::chunkOf(airportX) + dx, Scenery::chunkOf(12000.f) + dz, 2);
    sceneryResetWorldCaches();
    std::vector<int> installed;
    g_scenery.pump(installed);
    check(installed.empty() && g_scenery.generated() == 0, "reset drains old-world workers and drops their completed chunks");
  }
  g_world.airports = {airfield(12000.f, 12000.f)};
  std::vector<AptItem> generic, named;
  airportItems(0, generic);
  for (const char* code : {"ORC", "VCF", "HFS", "GLS", "SMP", "PMB", "GLR"}) {
    g_world.airports[0].code = code;
    airportItems(0, named);
    check(sameItems(generic, named), "generated code collision cannot activate a Solace-specific airport layout");
  }
  g_world.procedural = false;
  for (const char* code : {"ORC", "VCF"}) {
    g_world.airports[0].code = code;
    airportItems(0, named);
    check(!sameItems(generic, named), "authored airport keeps its original place-specific fixtures");
  }
  g_world.procedural = true;
  sceneryResetWorldCaches();
  std::printf("region scenery: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
