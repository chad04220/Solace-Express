// Auto resolution's governor (src/quality_governor.h): it holds the target where the cost is in the pixels, leaves the
// picture alone where it isn't, gives the picture back when there is room, and doesn't hunt in noise.
#include "quality_governor.h"
#include <cmath>
#include <cstdio>
#include <functional>

static int failures = 0;
static void check(bool ok, const char* what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }

struct Sim {
  QualityGovernor g;
  unsigned rng = 777u;
  float noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffff) / 65535.f * 2.f - 1.f; }
  int changes = 0; float lastF = 0.f, lastS = 1.f;
  float overBudget = 0.f, time = 0.f;
  // gpu(f, s, t): the GPU frame time at foveation f, render scale s, time t; 60 fps target (budget 15.33 ms)
  void run(float seconds, const std::function<float(float, float, float)>& gpu, float noiseFrac = 0.02f) {
    const float budget = 1000.f / 60.f * 0.92f;
    for (float t = 0.f; t < seconds; t += 1.f / 60.f) {
      const float ms = gpu(g.foveation, g.scale, time) * (1.f + noiseFrac * noise());
      g.frame(1.f / 60.f, ms, budget);
      if (g.foveation != lastF || g.scale != lastS) { changes++; lastF = g.foveation; lastS = g.scale; }
      if (t > seconds * 0.5f && ms > budget * 1.02f) overBudget += 1.f / 60.f;
      time += 1.f / 60.f;
    }
  }
};

// a frame whose cost is in its pixels (the night runway: terrain materials): fixed + per-pixel, foveation sheds up to 25%
static float pixelBound(float f, float s, float) { return 4.f + 14.f * s * s * (1.f - 0.25f * f); }
// a frame whose cost isn't (the city at night: tens of thousands of buildings): barely moves with either knob
static float fixedBound(float f, float s, float) { return 16.6f + 0.6f * s * s * (1.f - 0.1f * f); }

int main() {
  {
    Sim s; s.g.vrs = true; s.run(60.f, pixelBound);
    check(pixelBound(s.g.foveation, s.g.scale, 0) <= 1000.f / 60.f * 0.92f, "a pixel-bound scene is brought under its budget");
    check(s.g.foveation > 0.f, "foveation is used first where it pays");
    check(s.g.scale >= 0.85f, "and the render scale drops only as far as it must");
    check(s.overBudget < 1.f, "it then holds the target");
    check(s.changes < 12, "without hunting");
  }
  {
    Sim s; s.g.vrs = true; s.run(120.f, fixedBound);
    check(s.g.foveation == 0.f && s.g.scale == 1.f, "a scene whose cost isn't in its pixels keeps the full picture");
    check(s.g.stepsUndone >= 2, "each step that bought nothing was taken back");
    check(s.g.stepsDown <= 6, "and it tries again ever more rarely (a brief step every 20 s doubling to 160 s per knob)");
  }
  {
    Sim s; s.g.vrs = true; s.run(40.f, pixelBound); const bool stepped = s.g.foveation > 0.f || s.g.scale < 1.f;
    s.run(40.f, [](float f, float sc, float) { return 2.f + 6.f * sc * sc * (1.f - 0.25f * f); });
    check(stepped && s.g.foveation == 0.f && s.g.scale == 1.f && s.g.moves.empty(), "when the scene lightens the full picture comes back");
  }
  {
    Sim s; s.g.vrs = false; s.run(60.f, pixelBound);
    check(s.g.foveation == 0.f && s.g.scale < 1.f && pixelBound(0.f, s.g.scale, 0) <= 1000.f / 60.f * 0.92f,
          "without foveated shading the render scale alone holds the target");
  }
  {   // just under the budget, with noise: nothing to do, and nothing done
    Sim s; s.g.vrs = true; s.run(120.f, [](float f, float sc, float) { return 2.f + 12.5f * sc * sc * (1.f - 0.25f * f); }, 0.04f);
    check(s.changes <= 2, "a scene a little under the budget is left alone through 4% noise");
  }
  {   // a scene that turns heavy where a knob had proved futile tries it again
    Sim s; s.g.vrs = true; s.run(30.f, fixedBound);
    s.run(30.f, [](float f, float sc, float) { return 6.f + 18.f * sc * sc * (1.f - 0.25f * f); });
    check(s.g.foveation > 0.f || s.g.scale < 1.f, "a much heavier, pixel-bound scene after a futile one is stepped down again");
  }
  printf(failures ? "quality_governor: %d FAILED\n" : "quality_governor: all passed\n", failures);
  return failures ? 1 : 0;
}
