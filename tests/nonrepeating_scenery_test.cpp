// The same local chart can retain legacy wrap behavior or sit once on the Earth.
#include "../src/scenery.h"
#include "../src/road_traffic.h"
#include "../src/hive_world_collision.h"
#include <chrono>
#include <cstdio>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
}
int main() {
  sceneryResetWorldCaches();
  g_world = World();
  g_world.procedural = true;
  g_world.hm.assign((size_t)HM_N * HM_N * 4, 0.f);
  for (size_t i = 0; i < g_world.hm.size(); i += 4) g_world.hm[i] = -SEA_DEPTH;
  g_world.mask.assign((size_t)MASK_N * MASK_N * 4, 0);
  g_world.roadGrid.head.assign((size_t)MASK_N * MASK_N, 0);
  g_world.hmax[0].assign((size_t)HMAX_N * HMAX_N, 0.f);

  // One deliberately conspicuous collider at the west side of the chart.
  const Ent tower{-35000.f, 0.f, 12000.f, 0.f, 1.f, 1.f, 1.f, .5f};
  Scenery::Chunk* chunk = g_scenery.ensure(Scenery::chunkOf(tower.x), Scenery::chunkOf(tower.z), 2);
  chunk->ents = {tower};
  for (int k = 0; k <= EK_COUNT; ++k) chunk->off[k] = k > EK_TOWER ? 1 : 0;
  chunk->ymin = 0.f; chunk->ymax = kEntInfo[EK_TOWER].h;
  chunk->reach = 20.f; chunk->hmax = kEntInfo[EK_TOWER].h;
  chunk->markChanged();
  check(g_scenery.collide(vec3(tower.x, 30.f, tower.z), .5f) == EK_TOWER + 1, "local chart collider is real");
  g_world.repeating = true;
  check(hiveWorldSweep(vec3(64900.f, 30.f, 12000.f), vec3(65100.f, 30.f, 12000.f), 0.f) <= 1.f,
        "legacy repeated Solace collision still sees its nearest chart copy");
  g_world.repeating = false;
  check(hiveWorldSweep(vec3(64900.f, 30.f, 12000.f), vec3(65100.f, 30.f, 12000.f), 0.f) > 1.f,
        "nonrepeating chart has no phantom collider in its old 100km copy");
  check(hiveWorldSweep(vec3(65000.f, 30.f, 12000.f), vec3(65000.f, 30.f, 12000.f), .5f) > 1.f,
        "stationary remote query does not wrap into the old chart");
  for (float side : {-1.f, 1.f}) {
    check(hiveWorldSweep(vec3(side * 49000.f, 40.f, 12000.f), vec3(side * 51000.f, 40.f, 12000.f), 0.f) > 1.f,
          "crossing a former 50km seam stays over open ocean");
    const float fraction = hiveWorldSweep(vec3(side * 51000.f, 30.f, 12000.f), vec3(side * 51000.f, -30.f, 12000.f), 0.f);
    check(fraction > .49f && fraction < .51f, "remote ocean still has its physical sea surface");
  }
  for (float remote : {-1e20f, 1e20f}) {
    float mask[4]; g_world.sampleMask(remote, remote, mask);
    check(mask[0] == 1.f && mask[1] == 0.f && mask[2] == 0.f && mask[3] == 0.f,
          "distant interpolated mask is unpopulated sea before integer conversion");
    g_world.maskTexel(remote, remote, mask);
    check(mask[0] == 1.f && mask[1] == 0.f && mask[2] == 0.f && mask[3] == 0.f,
          "distant nearest mask is unpopulated sea before integer conversion");
    check(g_world.forestAt(remote, remote) == 0.f && RoadGrid::cell(remote) == (remote < 0.f ? 0 : RoadGrid::N - 1),
          "distant forest and road-grid lookups clamp before integer conversion");
  }
  const auto started = std::chrono::steady_clock::now();
  const size_t generated = g_scenery.generated();
  for (float remote : {100000.f, 1e8f, 1e20f}) {
    check(g_scenery.collide(vec3(remote, 40.f, remote), 1.f) == 0, "remote point query stays outside local chunks");
    check(g_scenery.obstacleTop(remote, remote, 30.f) < 0.f, "remote obstacle query stays outside local chunks");
    check(g_scenery.sweepSphere(vec3(remote, 40.f, remote), normalize(vec3(1.f, 0.f, 1.f)), 1e8f, 1.f) < 0.f,
          "long remote ocean sweep does not enumerate unbounded empty chunk coordinates");
  }
  check(g_scenery.generated() == generated, "remote ocean queries generate no local scenery");
  const double oceanSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

  RoadPath road; road.cls = RC_ROAD;
  road.pts = {{-35500.f, 12000.f, 30.f}, {-34500.f, 12000.f, 30.f}};
  road.bridge = {0}; g_world.roads.paths = {road};
  RoadTraffic traffic;
  g_world.repeating = true;
  traffic.update(g_world, vec3(65000.f, 100.f, 12000.f), .1f);
  check(!traffic.all().empty(), "legacy road traffic follows the repeating camera copy");
  g_world.repeating = false;
  traffic.update(g_world, vec3(65000.f, 100.f, 12000.f), .1f);
  check(traffic.all().empty(), "road traffic is absent from a nonrepeating chart's phantom copy");
  traffic.update(g_world, vec3(1e20f, 100.f, 1e20f), .1f);
  check(traffic.all().empty(), "remote traffic query clamps before integer conversion");
  g_scenery.trim(vec3(65000.f, 100.f, 12000.f), 1500.f, 3000.f, 1000);
  check(g_scenery.get(Scenery::chunkOf(tower.x), Scenery::chunkOf(tower.z)) == nullptr,
        "distant nonrepeating scenery is trimmed using actual chart distance");
  sceneryResetWorldCaches();
  std::printf("nonrepeating scenery: %d checks, %d failures, remote ocean queries %.6f s\n", checks, failures, oceanSeconds);
  return failures ? 1 : 0;
}
