// Somnium Volandi - the scenery streamer: which chunks the view wants (the trees to farDetail, everything to farAll,
// nearest first), the ones the worker threads finished since last frame, the near ones built here and now, the rest
// built here inside the frame's budget when the workers' queue is full, and the places the camera will cut to
// prefetched. Held by the renderer (Renderer::stream) and run once a main-view frame from drawEntities, which draws
// what is there afterwards. (docs/CODE_REWORK_PLAN.md 2.6: the first of the renderer's parts to stand on its own.)
#pragma once
#include "common.h"
#include <functional>
#include <vector>

struct SceneryStreamer {
  bool sync = false;        // generate every chunk in range before drawing (headless captures, the tools' scenes)
  float budgetMs = 2.5f;    // this frame's time for chunks beyond 700 m built on this thread (raised on loading screens)
  int pending = 0;          // chunks still to generate about the camera, after this frame (the loading screens wait on it)
  int chunks = 0;           // chunks generated so far (g_scenery.generated(): the F3 readout)
  int frame = 0;            // main-view frames so far: a chunk's lastUse, and the trim every 240th
  int installed = 0;        // chunks installed since the world was reset

  struct Copy { float ox, oz; };   // a copy of the islands across the map's seam (world.h WRAP_SPAN): its offset
  // what the view wants: the camera, the two reaches (entRangesFor) and the chunk radius of the farther one
  struct View { vec3 cam; float farDetail, farAll; int rad; const std::vector<Copy>* copies; };
  // the place the camera will cut to (FrameParams::prefetch*): the near part of a second place first, if any
  struct Prefetch { bool on = false; vec3 pos; float nearR = 0.f; vec3 nearPos; };

  void reset() { pending = chunks = frame = installed = 0; }
  // one main-view frame; `arrived(cx, cz)` is called for every chunk installed (the shadow cascades it falls in re-render)
  void update(const View& v, const Prefetch& pf, const std::function<void(int, int)>& arrived);
};
