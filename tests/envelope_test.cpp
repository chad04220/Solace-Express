// The terrain envelope mesh must never dip below the rendered terrain: at random points, and densely around the
// steepest and most detailed cells, the envelope's triangle height at every quadtree level has to be at or above the
// exact terrain height (8 octaves of detail, more than the renderer ever uses).
#include "../src/world.h"
#include "test_world.h"
#include <cstdio>
#include <algorithm>

static float vertH(int vi, int vj, int L) {
  const int N = HM_N, NV = HM_N + 1;
  if (L == 0) return g_world.tpV0[(size_t)std::clamp(vj, 0, N) * NV + std::clamp(vi, 0, N)];
  int n = N >> L; float m = 0.f;
  for (int k = 0; k < 4; k++) {
    int ci = std::clamp(vi - (k & 1), 0, n - 1), cj = std::clamp(vj - (k >> 1), 0, n - 1);
    m = std::max(m, g_world.tpM[L][(size_t)cj * n + ci]);
  }
  return m;
}
// the mesh over level-L cells: two triangles per cell split along the (0,1)-(1,0) diagonal (as the vertex shader)
static float envelopeAt(float x, float z, int L) {
  float cs = HM_TEXEL * (1 << L);
  float fx = (x + WORLD_HALF - 0.5f * HM_TEXEL) / cs, fz = (z + WORLD_HALF - 0.5f * HM_TEXEL) / cs;
  int i = (int)floorf(fx), j = (int)floorf(fz); float u = fx - i, v = fz - j;
  float a = vertH(i, j, L), b = vertH(i + 1, j, L), c = vertH(i, j + 1, L), d = vertH(i + 1, j + 1, L);
  return u + v <= 1.f ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1.f - u) + (b - d) * (1.f - v);
}

int main() {
  buildTestWorld();
  int bad = 0, total = 0; float worst = 1e9f, meanGap = 0;
  Rng r(77);
  const float lim = WORLD_HALF - HM_TEXEL;
  for (int k = 0; k < 400000; k++) {
    float x = r.range(-lim, lim), z = r.range(-lim, lim);
    float h = g_world.height(x, z, 8);
    for (int L = 0; L < TP_LEVELS; L++) {
      float e = envelopeAt(x, z, L), gap = e - h;
      total++;
      if (L == 0) meanGap += gap;
      worst = std::min(worst, gap);
      if (gap < 0.f) { if (bad < 12) printf("  !! level %d at (%.1f, %.1f): envelope %.2f below terrain %.2f\n", L, x, z, e, h); bad++; }
    }
  }
  // dense sweep over a mountain flank and a stretch of coast (detail and steep slopes)
  for (float z = -2000.f; z < 2000.f; z += 3.7f)
    for (float x = 6000.f; x < 6400.f; x += 3.1f) {
      float h = g_world.height(x, z, 8), e = envelopeAt(x, z, 0);
      total++; worst = std::min(worst, e - h);
      if (e < h) { if (bad < 12) printf("  !! dense at (%.1f, %.1f): envelope %.2f below terrain %.2f\n", x, z, e, h); bad++; }
    }
  printf("envelope_test: %d samples, %d below the terrain, closest %.3f m, mean gap at level 0 %.2f m: %s\n",
         total, bad, worst, meanGap / 400000.f, bad ? "FAIL" : "ok");
  return bad ? 1 : 0;
}
