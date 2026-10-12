// Keep executable assertions active in Release/CI builds too.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "load_pacer.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

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
  // The independent intro sees one coherent plan, grouped subsystems and a
  // restarting task bar, without crediting a GPU batch as a finished mesh.
  LoadPacer observed;
  int first=observed.add("mesh0",9,9,"AIRCRAFT"),second=observed.add("mesh1",9,9,"AIRCRAFT");
  int optional=observed.add("cache",4,0,"SHADER CACHE");
  observed.begin(first);observed.setSub(3.f/9);observed.task("Surface field / GPU rows",32,128);
  auto snap=observed.snapshot();assert(snap.step==1&&snap.steps==3&&snap.groups.size()==2);
  assert(near(snap.stageFraction,1.f/3)&&near(snap.taskFraction,.25f));
  assert(near(snap.groups[0].done,3)&&near(snap.groups[0].units,18));
  const float before=observed.fraction();observed.task("Next field / GPU rows",0,64);
  assert(observed.fraction()==before&&observed.snapshot().taskFraction==0);
  observed.task("Invalid",std::numeric_limits<float>::quiet_NaN(),0);
  assert(observed.snapshot().taskLabel=="Next field / GPU rows");
  observed.begin(second);assert(observed.snapshot().taskFraction<0);
  std::thread writer([&]{for(int i=1;i<=90;++i){observed.setSub(i/90.f);observed.task("GPU rows",float(i),90);}});
  for(int i=0;i<500;++i){auto read=observed.snapshot();assert(read.overall>=before&&read.overall<=1&&read.groups[0].done<=18);}
  writer.join();observed.skipRemaining(optional);observed.end();
  snap=observed.snapshot();assert(snap.overall==1&&snap.stageFraction==1&&snap.taskFraction<0);
  assert(snap.groups[1].units==0&&snap.groups[1].done==0&&snap.groups[1].skipped==4); // skipped work never becomes completed
  std::cout << "Loading progress tests passed\n";
}
