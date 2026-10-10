// Scenery raycast vs collision: wherever a densely sampled sphere sweep (collide every 5 cm) touches something, the
// analytic raycast must report a hit no later than that point - in particular a grazing pass through a tree crown that
// a coarse sampled sweep would step over.
#include "../src/entities.h"
#include "test_world.h"
#include <cstdio>
int main() {
  buildTestWorld();
  uint32_t rs = 12345;
  auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / 16777216.f; };
  int checked = 0, missed = 0, grazing = 0;
  // sample entities of every class around a few places with trees, rocks and buildings
  const vec2 spots[] = {vec2(-8000, 14600), vec2(-20000, -5000), vec2(1500, -3500), vec2(-14200, 4400), vec2(21000, 7000)};
  for (vec2 sp : spots) {
    Scenery::Chunk* ch = g_scenery.ensure(Scenery::chunkOf(sp.x), Scenery::chunkOf(sp.y), 2);
    if (!ch) continue;
    for (int k = 0; k < EK_COUNT; k++) {
      if (k == EK_RWYLIGHT || k == EK_PAPI) continue;
      for (uint32_t i = ch->off[k]; i < ch->off[k + 1] && i < ch->off[k] + 6; i++) {
        const Ent& e = ch->ents[i];
        const EntKindInfo& I = kEntInfo[k];
        float R = std::max(I.hx * e.sx, I.hz * e.sz);
        for (int n = 0; n < 40; n++) {
          // a short level-ish segment passing near the entity at a random height and offset (often grazing)
          float ang = rnd() * 6.2832f, off = (rnd() * 2.f - 1.f) * R * 1.3f, y = e.y + rnd() * I.h * e.sy;
          vec3 dir(cosf(ang), (rnd() - 0.5f) * 0.2f, sinf(ang)); dir = normalize(dir);
          vec3 side(-dir.z, 0, dir.x);
          vec3 a = vec3(e.x, y, e.z) + side * off - dir * (R + 6.f);
          float L = 2.f * (R + 6.f);
          float fine = -1.f;
          for (float t = 0; t <= L; t += 0.05f) if (g_scenery.collide(a + dir * t, 0.4f)) { fine = t; break; }
          int kh = 0; float t = g_scenery.raycast(a, dir, L, &kh);
          checked++;
          if (fine >= 0.f && (t < 0.f || t > fine + 0.06f)) {
            missed++;
            if (missed <= 5) printf("  !! %s: sweep hits at %.2f, raycast %.2f\n", I.name, fine, t);
          }
          // coarse sampling (the old 2.5 m steps) would have missed this one
          if (fine >= 0.f) { bool coarse = false; for (float tt = 0; tt <= L; tt += 2.5f) if (g_scenery.collide(a + dir * tt, 0.4f)) coarse = true; if (!coarse) grazing++; }
        }
      }
    }
  }
  printf("entity_raycast_test: %d segments, %d grazing passes a 2.5 m sweep misses, %d missed by the raycast: %s\n", checked, grazing, missed, missed ? "FAIL" : "ok");
  return missed || checked < 100 ? 1 : 0;
}
