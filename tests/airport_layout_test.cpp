// Airport ground plans and furniture: nothing stands on a runway, taxiway or exit (or the runway's graded strip),
// buildings and parked aircraft don't overlap each other, the AI stands are clear, and every airport got furnished.
#include "../src/airport_layout.h"
#include "../src/entities.h"
#include <cstdio>

struct OBB { float u, v, hu, hv, cu, su; };   // runway-local centre, half extents along its own axes, axis direction

static OBB toLocal(const Airport& a, const AptItem& it) {
  const EntKindInfo& I = kEntInfo[it.kind];
  vec2 l = aptLocal(a, vec3(it.e.x, 0, it.e.z));
  // the entity's +x axis in runway-local terms
  vec3 ax(cosf(it.e.yaw), 0, -sinf(it.e.yaw));
  vec2 la = aptLocal(a, vec3(a.x, 0, a.z) + ax);
  return {l.x, l.y, I.hx * it.e.sx, I.hz * it.e.sz, la.x, la.y};
}
// separating-axis test of two rotated rectangles in the (u, v) plane
static bool overlap(const OBB& A, const OBB& B, float pad) {
  vec2 ax[4] = {vec2(A.cu, A.su), vec2(-A.su, A.cu), vec2(B.cu, B.su), vec2(-B.su, B.cu)};
  vec2 d(B.u - A.u, B.v - A.v);
  for (auto& n : ax) {
    float ra = A.hu * fabsf(n.x * A.cu + n.y * A.su) + A.hv * fabsf(n.x * -A.su + n.y * A.cu);
    float rb = B.hu * fabsf(n.x * B.cu + n.y * B.su) + B.hv * fabsf(n.x * -B.su + n.y * B.cu);
    if (fabsf(d.x * n.x + d.y * n.y) > ra + rb + pad) return false;
  }
  return true;
}
static OBB rect(float u0, float u1, float v0, float v1) { return {(u0 + u1) * 0.5f, (v0 + v1) * 0.5f, (u1 - u0) * 0.5f, (v1 - v0) * 0.5f, 1, 0}; }
static bool smallFixture(int k) { return k == EK_RWYLIGHT || k == EK_PAPI || k == EK_FENCE || k == EK_WINDSOCK; }
static bool vehicle(int k) { return k == EK_CAR || k == EK_TRUCK; }

int main() {
  g_world.build();
  int fails = 0, total = 0;
  for (int ai = 0; ai < (int)g_world.airports.size(); ai++) {
    const Airport& a = g_world.airports[ai];
    AptLayout L = aptLayout(a, ai);
    std::vector<AptItem> items; airportItems(ai, items);
    float hl = a.length * 0.5f, hw = a.width * 0.5f;
    std::vector<OBB> keepOut = {rect(-hl - 60.f, hl + 60.f, -hw - 25.f, hw + 25.f)};   // runway and its graded strip
    if (L.paved) {
      float s = L.side;
      auto sideRect = [&](float u0, float u1, float v0, float v1) { return s > 0 ? rect(u0, u1, v0, v1) : rect(u0, u1, -v1, -v0); };
      keepOut.push_back(sideRect(-hl + 25.f - L.twHW - 2.f, hl - 25.f + L.twHW + 2.f, L.twV - L.twHW - 2.f, L.twV + L.twHW + 2.f));
      for (int e = 0; e < L.nExit; e++) keepOut.push_back(sideRect(L.exitU[e] - L.twHW - 2.f, L.exitU[e] + L.twHW + 2.f, hw, L.twV));
      // AI stands: lead-in line to the stop bar, 40 m deep
      for (float su = L.standU0 + 22.5f; su < L.standU1; su += 45.f) keepOut.push_back(sideRect(su - 14.f, su + 14.f, L.apV1 - 52.f, L.apV1 - 8.f));
    }
    int n = 0, bad = 0;
    std::vector<OBB> solid;
    for (auto& it : items) {
      if (it.kind == EK_RWYLIGHT) continue;
      n++;
      OBB o = toLocal(a, it);
      if (!smallFixture(it.kind))
        for (size_t k = 0; k < keepOut.size(); k++)
          if (overlap(o, keepOut[k], 0.f)) { printf("  !! %s: %s at (%.0f, %.0f) on keep-out %zu\n", a.code, kEntInfo[it.kind].name, o.u, o.v, k); bad++; break; }
      if (smallFixture(it.kind) || vehicle(it.kind) || it.kind == EK_JETBRIDGE) continue;
      for (auto& p : solid) if (overlap(o, p, 0.5f)) { printf("  !! %s: %s at (%.0f, %.0f) overlaps another item\n", a.code, kEntInfo[it.kind].name, o.u, o.v); bad++; break; }
      solid.push_back(o);
    }
    printf("%s %-26s %4d items%s\n", a.code, a.name, n, bad ? "  <-- problems" : "");
    if (n < (L.paved ? 60 : 8)) { printf("  !! %s: only %d items\n", a.code, n); bad++; }
    fails += bad; total += n;
  }
  printf("airport_layout_test: %d items, %s (%d problems)\n", total, fails ? "FAIL" : "ok", fails);
  return fails ? 1 : 0;
}
