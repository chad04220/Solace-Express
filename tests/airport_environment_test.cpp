// Deterministic living-airport scenery, a usable landside entrance, and safe detail footprints.
// This supplements airport_layout_test's runway/taxiway/AI-stand and solid-overlap checks.
#include "../src/airport_layout.h"
#include "../src/entities.h"
#include "test_world.h"
#include <cstdio>
#include <cstring>

struct Footprint { float u0, u1, v0, v1; };
static Footprint footprint(const Airport& a, const AptLayout& L, const AptItem& item) {
  const Ent& e = item.e; const EntKindInfo& info = kEntInfo[item.kind];
  Footprint b{1e9f, -1e9f, 1e9f, -1e9f};
  for (int corner = 0; corner < 4; corner++) {
    float x = (corner & 1 ? 1.f : -1.f) * info.hx * e.sx;
    float z = (corner & 2 ? 1.f : -1.f) * info.hz * e.sz;
    vec2 p = aptLocal(a, {e.x + cosf(e.yaw) * x + sinf(e.yaw) * z, 0,
                         e.z - sinf(e.yaw) * x + cosf(e.yaw) * z});
    p.y *= L.side;
    b.u0 = std::min(b.u0, p.x); b.u1 = std::max(b.u1, p.x);
    b.v0 = std::min(b.v0, p.y); b.v1 = std::max(b.v1, p.y);
  }
  return b;
}
static bool overlap(const Footprint& a, const Footprint& b, float pad = 0.f) {
  return a.u1 + pad > b.u0 && a.u0 - pad < b.u1 && a.v1 + pad > b.v0 && a.v0 - pad < b.v1;
}
static bool addedDetail(const AptItem& it) {
  // Dedicated new sizes distinguish authored service details from the original airside furniture.
  return it.kind == EK_BUSH || (it.kind == EK_WAREHOUSE && it.e.sx < 1.f) ||
         (it.kind == EK_FBO && fabsf(it.e.sx - .7f) < .001f) ||
         (it.kind == EK_FLOODMAST && it.e.sy <= .4f) ||
         (it.kind == EK_TRUCK && fabsf(it.e.sy - .85f) < .001f);
}
int main() {
  buildTestWorld();
  int failed = 0, checks = 0, additions = 0;
  auto check = [&](bool ok, const char* code, const char* what) {
    checks++; if (!ok) { failed++; printf("FAIL: %s %s\n", code, what); }
  };
  check(g_world.airports.size() == 16, "ALL", "all sixteen airport definitions present");
  for (int ai = 0; ai < (int)g_world.airports.size(); ai++) {
    const Airport& a = g_world.airports[ai]; const Airport before = a;
    const AptLayout L = aptLayout(a, ai);
    std::vector<AptItem> items, repeat; airportItems(ai, items); airportItems(ai, repeat);
    check(items.size() == repeat.size(), a.code, "stable object count");
    for (size_t i = 0; i < std::min(items.size(), repeat.size()); i++)
      check(items[i].kind == repeat[i].kind && !std::memcmp(&items[i].e, &repeat[i].e, sizeof(Ent)), a.code, "bit-identical repeated placement");
    check(a.x == before.x && a.z == before.z && a.elev == before.elev && a.heading == before.heading &&
          a.length == before.length && a.width == before.width && a.surface == before.surface,
          a.code, "scenery leaves runway geometry and surface untouched");
    int cars = 0, trucks = 0, service = 0, lamps = 0, detailCount = 0;
    std::vector<Footprint> footprints; for (const AptItem& it : items) footprints.push_back(footprint(a, L, it));
    const Footprint gate{L.termU - 8.f, L.termU + 8.f, L.lotV0, L.lotV1 + 20.f};
    for (size_t i = 0; i < items.size(); i++) {
      const AptItem& it = items[i]; const Ent& e = it.e; const Footprint b = footprints[i];
      cars += it.kind == EK_CAR; trucks += it.kind == EK_TRUCK;
      service += it.kind == EK_WAREHOUSE && e.sx < 1.f;
      lamps += it.kind == EK_FLOODMAST && e.sy <= .4f;
      check(std::isfinite(e.x) && std::isfinite(e.y) && std::isfinite(e.z) && std::isfinite(e.yaw) &&
            std::isfinite(e.seed) && e.sx > 0 && e.sy > 0 && e.sz > 0, a.code, "finite positive instance transform");
      if (L.paved) check(!overlap(b, gate), a.code, "16 m landside entry clear through car park and fence");
      if (!addedDetail(it)) continue;
      additions++; detailCount++;
      const Footprint strip{-a.length * .5f - 60.f, a.length * .5f + 60.f, -a.width * .5f - 25.f, a.width * .5f + 25.f};
      check(!overlap(b, strip), a.code, "new details outside runway graded strip");
      if (L.paved) {
        const Footprint movement{-a.length * .5f, a.length * .5f, L.twV - L.twHW - 3.f, L.apV1 - 8.f};
        check(!overlap(b, movement), a.code, "new details outside apron movement and taxi lanes");
      }
      float end = std::max(fabsf(b.u0), fabsf(b.u1)) - a.length * .5f;
      float innerV = b.v0 <= 0 && b.v1 >= 0 ? 0 : std::min(fabsf(b.v0), fabsf(b.v1));
      check(!(end > -60.f && innerV < 250.f + .18f * std::max(0.f, end)), a.code, "no new approach-funnel obstacle");
      float low = 1e9f, high = -1e9f;
      for (int q = 0; q < 5; q++) {
        float u = q == 4 ? (b.u0 + b.u1) * .5f : q & 1 ? b.u0 : b.u1;
        float v = q == 4 ? (b.v0 + b.v1) * .5f : q & 2 ? b.v0 : b.v1;
        vec3 p = aptWorld(a, u, v * L.side, 0); float h = g_world.groundHeight(p.x, p.z, 7);
        low = std::min(low, h); high = std::max(high, h);
      }
      check(low > .2f && high - low < 1.26f, a.code, "new detail grounded on dry low-slope existing ground");
      for (size_t j = 0; j < items.size(); j++) if (i != j)
        check(!overlap(b, footprints[j], .6f), a.code, "new detail separated from all existing furniture");
    }
    check(trucks > 0 && service > 0 && lamps > 0 && detailCount >= 3, a.code, "service-yard and lighting improvements present");
    if (L.paved) check(cars >= 20, a.code, "passenger/staff parking remains populated");
    printf("%s: %zu objects, %d new service/landscape fixtures, %d cars, %d support vehicles\n", a.code, items.size(), detailCount, cars, trucks);
  }
  printf("airport_environment_test: %d checks, %d new fixtures, %d failures\n", checks, additions, failed);
  return failed ? 1 : 0;
}
