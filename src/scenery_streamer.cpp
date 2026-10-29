// Somnium Volandi - the scenery streamer (scenery_streamer.h)
#include "scenery_streamer.h"
#include "entities.h"
#include <algorithm>
#include <chrono>

void SceneryStreamer::update(const View& v, const Prefetch& pf, const std::function<void(int, int)>& arrived) {
  frame++;
  // ------------------------------------------------ the chunks missing about the camera, by distance
  struct Need { float d; int cx, cz, level; };
  std::vector<Need> need;
  for (const Copy& cp : *v.copies) {
    const vec3 camL = v.cam - vec3(cp.ox, 0.f, cp.oz);   // (the camera as that copy of the islands sees it)
    const int lcx = Scenery::chunkOf(camL.x), lcz = Scenery::chunkOf(camL.z);
    for (int dz = -v.rad; dz <= v.rad; dz++)
      for (int dx = -v.rad; dx <= v.rad; dx++) {
        int cx = lcx + dx, cz = lcz + dz;
        if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
        float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
        float ex = std::max(std::max(x0 - camL.x, camL.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - camL.z, camL.z - z0 - Scenery::CH), 0.f);
        float d = sqrtf(ex * ex + ez * ez);
        int want = d < v.farDetail ? 2 : d < v.farAll ? 1 : 0;
        if (!want) continue;
        Scenery::Chunk* c = g_scenery.get(cx, cz);
        if (c) c->lastUse = frame;
        if (!c || c->level < want) need.push_back({d, cx, cz, want});
      }
  }
  auto added = [&](int cx, int cz) { installed++; arrived(cx, cz); };
  // chunks the worker threads finished since last frame
  std::vector<int> got;
  g_scenery.pump(got);
  for (int idx : got) {
    int cx = idx % Scenery::NC, cz = idx / Scenery::NC;
    g_scenery.get(cx, cz)->lastUse = frame;
    added(cx, cz);
  }
  if (!got.empty())   // drop the ones that just arrived from the list
    need.erase(std::remove_if(need.begin(), need.end(), [](const Need& n) { Scenery::Chunk* c = g_scenery.get(n.cx, n.cz); return c && c->level >= n.level; }), need.end());
  pending = (int)need.size();
  if (!need.empty()) {
    std::sort(need.begin(), need.end(), [](const Need& a, const Need& b) { return a.d < b.d; });
    auto t0 = std::chrono::steady_clock::now();
    for (const Need& n : need) {
      // close chunks are generated here and now so they are never missing; the rest go to the worker threads
      // (or, without workers, are generated here within the frame's time budget). A loading screen (its budget
      // raised: Game::updateLoading) builds on here too once the queue is full, in the time this thread would
      // spend waiting for the display
      bool now = sync || n.d <= 700.f;
      if (!now && g_scenery.request(n.cx, n.cz, n.level)) continue;
      if (!now && g_scenery.workers() > 0 && budgetMs < 10.f) break;   // queue full: ask again next frame
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (!now && ms > budgetMs) break;
      g_scenery.ensure(n.cx, n.cz, n.level)->lastUse = frame;
      pending--;
      added(n.cx, n.cz);
    }
  }
  // ------------------------------------------------ the next place the camera will cut to (the menu tour): its chunks go to
  // the worker threads now, and are kept. First the near part of each place it may cut to (the tour's next, nearR's),
  // what the cut's first frame would otherwise build itself (the 700 m above), then the rest of the tour's next, once
  // the queue has room
  auto prefetch = [&](vec3 pc, float within) {
    const int pcx = Scenery::chunkOf(pc.x), pcz = Scenery::chunkOf(pc.z), prad = std::min(v.rad, (int)ceilf(within / Scenery::CH) + 1);   // (all of it, as the view there will want it: the flight's loading screen waits for every chunk)
    for (int dz = -prad; dz <= prad; dz++)
      for (int dx = -prad; dx <= prad; dx++) {
        int cx = pcx + dx, cz = pcz + dz;
        if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
        float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
        float ex = std::max(std::max(x0 - pc.x, pc.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - pc.z, pc.z - z0 - Scenery::CH), 0.f);
        float d = sqrtf(ex * ex + ez * ez);
        int want = d < v.farDetail ? 2 : d < v.farAll ? 1 : 0;
        if (!want || d > within) continue;
        Scenery::Chunk* c = g_scenery.get(cx, cz);
        if (c) { c->lastUse = frame; if (c->level >= want) continue; }
        if (!g_scenery.request(cx, cz, want)) return false;   // queue full: more next frame
      }
    return true;
  };
  if (pf.on && g_scenery.workers() > 0 && (pf.nearR <= 0.f || prefetch(pf.nearPos, pf.nearR)) && prefetch(pf.pos, 1000.f)) prefetch(pf.pos, v.farAll);
  if (frame % 240 == 0) g_scenery.trim(v.cam, v.farDetail + 1200.f, v.farAll + 2500.f, frame);
  chunks = (int)g_scenery.generated();
}
