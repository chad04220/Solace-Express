// Scenery streaming on the worker threads, as the renderer drives it: every chunk a flight wants about its start (the
// trees to farDetail, the buildings to farAll, quality 2's ranges) requested nearest first, the queue refilled once a
// frame at 60 fps. The v4.4.1 diagnostics showed a loading screen making exactly 12 chunks a frame for 11 s: the queue
// held two chunks a worker, and most chunks (sea) are made in 0.02 ms, so the workers sat idle for most of every
// frame. The queue is capped by the work in it now (Scenery::Async::jobLoad), so the run streams at the workers' pace.
#include "../src/entities.h"
#include "test_world.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

struct Need { float d; int cx, cz, level; };

int main() {
  int failed = 0;
  auto check = [&](bool pass, const std::string& label) { printf("Scenery streaming: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass; };
  buildTestWorld();
  if (g_scenery.workers() == 0) { printf("Scenery streaming: no worker threads on this machine: skipped\n"); return 0; }
  const int ai = std::max(0, g_world.findAirport("CAP"));
  const Airport& a = g_world.airports[ai];
  vec3 cam = a.pos() + a.dir() * 1500.f;   // (a research sortie's airborne start)
  const float farDetail = 8100.f, farAll = 18300.f;
  std::vector<Need> need;
  const int rad = (int)ceilf(farAll / Scenery::CH) + 1, lcx = Scenery::chunkOf(cam.x), lcz = Scenery::chunkOf(cam.z);
  for (int dz = -rad; dz <= rad; dz++)
    for (int dx = -rad; dx <= rad; dx++) {
      const int cx = lcx + dx, cz = lcz + dz;
      if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
      const float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
      const float ex = std::max(std::max(x0 - cam.x, cam.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - cam.z, cam.z - z0 - Scenery::CH), 0.f);
      const float d = sqrtf(ex * ex + ez * ez);
      const int want = d < farDetail ? 2 : d < farAll ? 1 : 0;
      if (want) need.push_back({d, cx, cz, want});
    }
  std::sort(need.begin(), need.end(), [](const Need& x, const Need& y) { return x.d < y.d; });
  int n2 = 0; for (const Need& n : need) n2 += n.level == 2;
  printf("Scenery streaming: %zu chunks wanted (%d with trees), %d workers\n", need.size(), n2, g_scenery.workers());
  const auto start = std::chrono::steady_clock::now();
  int frames = 0, most = 0, pending = 0;
  std::vector<int> got;
  for (;;) {
    const auto f0 = std::chrono::steady_clock::now();
    got.clear(); g_scenery.pump(got);
    most = std::max(most, (int)got.size());
    pending = 0;
    for (const Need& n : need) {
      const Scenery::Chunk* c = g_scenery.get(n.cx, n.cz);
      if (c && c->level >= n.level) continue;
      pending++;
      if (!g_scenery.request(n.cx, n.cz, n.level)) break;   // queue full: the rest next frame
    }
    if (pending == 0) break;
    frames++;
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) break;
    std::this_thread::sleep_until(f0 + std::chrono::microseconds(16667));   // (the renderer refills once a frame)
  }
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  printf("Scenery streaming: %zu chunks in %.2f s, %d frames (%.0f a frame, %d at the most)\n", need.size(), s, frames, frames ? need.size() / (double)frames : 0., most);
  check(pending == 0, "every chunk about the start made within a minute");
  // (two a worker a frame was the old cap: 12 a frame on six workers, 11 s for a sortie's 8,000. The level-1 chunks
  // alone, four fifths of them, are made faster than any queue of a few a worker can be refilled.) The decisive check
  // is the most installed in one frame: the queue's cap, whatever the machine. The average is judged only outside
  // CI's quick sanitizer runs (FLIGHT_QUICK), where the chunks themselves are made several times slower on a shared
  // runner, so the run is bound by the workers, not the queue
  check(most > 2 * g_scenery.workers(), "more than two chunks a worker installed in one frame");
  if (getenv("FLIGHT_QUICK")) printf("Scenery streaming: the average a frame not judged (FLIGHT_QUICK: the sanitizers' run)\n");
  else check(frames < (int)need.size() / (6 * g_scenery.workers()), fmt("far more than %d chunks a frame", 6 * g_scenery.workers()));
  printf("scenery_streaming_test: %s\n", failed ? "FAILED" : "passed");
  return failed ? 1 : 0;
}
