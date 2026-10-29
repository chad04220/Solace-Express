// Scenery streaming on the worker threads, as the renderer drives it: every chunk a flight wants about its start (the
// trees to farDetail, the buildings to farAll, quality 2's ranges) requested nearest first, the queue refilled once a
// frame at 60 fps. The v4.4.1 diagnostics showed a loading screen making exactly 12 chunks a frame for 11 s: the queue
// held two chunks a worker, and most chunks (sea) are made in 0.02 ms, so the workers sat idle for most of every
// frame. The queue is capped by the work in it now (Scenery::Async::jobLoad), so the run streams at the workers' pace:
// judged against this machine's own single-threaded time for the same chunks (test_main.h TimeBudget), not a number.
#include "../src/entities.h"
#include "test_main.h"
#include "test_world.h"
#include <algorithm>
#include <thread>
#include <vector>

struct Need { float d; int cx, cz, level; };

int main() {
  TestRun t("Scenery streaming");
  buildTestWorld();
  if (g_scenery.workers() == 0) { t.note("no worker threads on this machine: skipped"); return t.done(); }
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
  t.note(fmt("%zu chunks wanted (%d with trees), %d workers", need.size(), n2, g_scenery.workers()));
  // the reference: the same chunks made on this thread alone
  TimeBudget single;
  for (const Need& n : need) g_scenery.ensure(n.cx, n.cz, n.level);
  const double singleS = single.seconds();
  t.note(fmt("made on one thread in %.2f s", singleS));
  g_scenery.clear();
  // the worker path, refilled once a 60 Hz frame as the renderer does
  TimeBudget run;
  int frames = 0, most = 0, pending = 0;
  std::vector<int> got;
  for (;;) {
    const auto f0 = TimeBudget::clock::now();
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
    if (run.seconds() > 60.) break;
    std::this_thread::sleep_until(f0 + std::chrono::microseconds(16667));   // (the renderer refills once a frame)
  }
  const double s = run.seconds();
  t.note(fmt("%zu chunks in %.2f s, %d frames (%.0f a frame, %d at the most)", need.size(), s, frames, frames ? need.size() / (double)frames : 0., most));
  t.check(pending == 0, "every chunk about the start made within a minute");
  // the decisive check, whatever the machine: the queue no longer caps a frame at two chunks a worker (12 on six
  // workers: 11 s for a sortie's 8,000)
  t.check(most > 2 * g_scenery.workers(), "more than two chunks a worker installed in one frame");
  // and the workers, refilled once a frame, beat one thread with no refill at all (a queue starving them took 15x
  // longer than one thread here). Judged against this machine's own time; reported but not judged under FLIGHT_QUICK
  TimeBudget::within(t, s, singleS, 1.0, "the worker path against one thread");
  return t.done();
}
