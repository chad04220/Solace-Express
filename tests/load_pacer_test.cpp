// Keep executable assertions active in Release/CI builds too.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "load_pacer.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

static bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
int main() {
  LoadPacer p;
  int shader = p.add("shaders", 4), world = p.add("world", 1), renderer = p.add("renderer", 11);
  p.begin(shader);
  assert(near(p.fraction(), 0));
  p.setDone(shader, 2);
  assert(near(p.fraction(), 2.f / 16));
  p.setDone(world, 1); // concurrent completion counts without closing shader work
  assert(near(p.fraction(), 3.f / 16));
  p.setDone(shader, 1); assert(near(p.fraction(), 3.f / 16));
  p.setDone(shader, std::numeric_limits<float>::quiet_NaN());
  p.setDone(-1, 3); p.begin(99);
  assert(near(p.fraction(), 3.f / 16));
  p.begin(renderer); assert(near(p.fraction(), 5.f / 16));
  p.setSub(5.f / 11); assert(near(p.fraction(), 10.f / 16));
  p.setSub(0); assert(near(p.fraction(), 10.f / 16));
  p.setSub(std::numeric_limits<float>::quiet_NaN()); assert(near(p.fraction(), 10.f / 16));
  assert(p.fraction() < 1); p.end(); assert(p.fraction() == 1);
  LoadPacer weighted;
  int a = weighted.add("many", 10, 2), b = weighted.add("one", 1, 3);
  weighted.setDone(a, 5); assert(near(weighted.fraction(), 0.2f));
  weighted.setDone(b, 1); assert(near(weighted.fraction(), 0.8f));
  LoadPacer failedHelper;
  int mainPrograms = failedHelper.add("main", 40);
  int optionalCache = failedHelper.add("cache", 40);
  int finalWork = failedHelper.add("remaining", 10);
  failedHelper.setDone(optionalCache, 7);
  failedHelper.skipRemaining(optionalCache);
  assert(failedHelper.skippedOf(optionalCache) == 33);
  assert(near(failedHelper.fraction(), 7.f / 57));
  failedHelper.setDone(mainPrograms, 40);
  assert(near(failedHelper.fraction(), 47.f / 57));
  assert(failedHelper.fraction() < 1); // failed helper must not complete required work
  failedHelper.begin(finalWork); failedHelper.end();
  assert(failedHelper.fraction() == 1);
  assert(failedHelper.skippedOf(optionalCache) == 33); // remains skipped, not built
  // Same elapsed time gives the same smoothing at different refresh rates.
  float x = 0, y = 0;
  for (int i = 0; i < 30; ++i) x = easeLoadProgress(x, 0.8f, 1.f / 30);
  for (int i = 0; i < 144; ++i) y = easeLoadProgress(y, 0.8f, 1.f / 144);
  assert(near(x, y)); assert(x <= 0.8f);
  assert(easeLoadProgress(0, 0, 100) == 0);
  assert(easeLoadProgress(0.9f, 1, 0.001f) == 1);
  assert(easeLoadProgress(0, 0.9999f, 100) < 1);
  std::cout << "Loading progress tests passed\n";
}
