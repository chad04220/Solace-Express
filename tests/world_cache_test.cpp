// The islands' cache (World::saveCache / loadCache): a valid file reads back identical, and a wrong stamp, a truncated
// file, a derived array of the wrong size or trailing bytes are all rejected (the world is then generated again).
#include <limits>
#include "../src/world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } else printf("ok: %s\n", what); } while (0)

static std::vector<char> readAll(const std::string& p) { std::vector<char> b; FILE* f = fopen(p.c_str(), "rb"); if (!f) return b; fseek(f, 0, SEEK_END); b.resize(ftell(f)); fseek(f, 0, SEEK_SET); if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear(); fclose(f); return b; }
static void writeAll(const std::string& p, const std::vector<char>& b) { FILE* f = fopen(p.c_str(), "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f); }

int main() {
  const std::string path = "world_cache_test.bin";
  remove(path.c_str());
  World a; a.build(path, "stampA");
  CHECK(!a.fromCache, "first build generates");
  World b; b.build(path, "stampA");
  bool same = b.fromCache && a.hm == b.hm && a.mask == b.mask && a.tpV0 == b.tpV0 && a.roadGrid.head == b.roadGrid.head &&
              a.roadGrid.list == b.roadGrid.list && a.roadGrid.nearHead == b.roadGrid.nearHead && a.roadGrid.nearList == b.roadGrid.nearList &&
              a.roadGrid.segs.size() == b.roadGrid.segs.size() && a.roads.paths.size() == b.roads.paths.size() && a.roads.nodes.size() == b.roads.nodes.size();
  for (size_t i = 0; same && i < a.roadGrid.segs.size(); i++) same = !memcmp(&a.roadGrid.segs[i], &b.roadGrid.segs[i], sizeof(RoadSegment));
  for (size_t i = 0; same && i < a.roads.paths.size(); i++) same = a.roads.paths[i].pts.size() == b.roads.paths[i].pts.size() && a.roads.paths[i].bridge == b.roads.paths[i].bridge &&
    !memcmp(a.roads.paths[i].pts.data(), b.roads.paths[i].pts.data(), a.roads.paths[i].pts.size() * sizeof(RoadPoint));
  CHECK(!a.roadGrid.segs.empty() && a.height(-29000.f, 10200.f, 8) == b.height(-29000.f, 10200.f, 8), "the road network is generated, cached and graded in");
  for (int L = 0; L < HMAX_LEVELS; L++) same = same && a.hmax[L] == b.hmax[L];
  for (int L = 0; L < TP_LEVELS; L++) same = same && a.tpM[L] == b.tpM[L];
  CHECK(same, "second build reads every array back identical");
  World c; CHECK(!c.loadCache(path, "stampB"), "another build's stamp is rejected");
  const std::vector<char> good = readAll(path);
  const std::string bad = "world_cache_bad.bin";
  { std::vector<char> t(good.begin(), good.begin() + good.size() / 2); writeAll(bad, t); World d; CHECK(!d.loadCache(bad, "stampA"), "a truncated file is rejected"); }
  { std::vector<char> t = good; t.push_back(0); writeAll(bad, t); World d; CHECK(!d.loadCache(bad, "stampA"), "trailing bytes are rejected"); }
  {   // a NaN height in an otherwise well-formed file (the review of v3.44.0, WLD-4): the first height, after the header and hm's count
    std::vector<char> t = good; const float nan = std::numeric_limits<float>::quiet_NaN(); memcpy(&t[4 + 64 + 8], &nan, 4);
    writeAll(bad, t); World d; CHECK(!d.loadCache(bad, "stampA") && d.hm.empty(), "a NaN height is rejected, nothing published");
  }
  {   // the height bounds' first level written as empty: its size field zero, its data gone
    const size_t head = 4 + 64;
    size_t off = head;
    auto skip = [&](size_t elem) { uint64_t n; memcpy(&n, &good[off], 8); off += 8 + n * elem; };
    skip(4); skip(1);   // hm, mask
    // the road network: its nodes, its paths (each a class/from/to header, its points, its bridge flags), then the grid
    skip(sizeof(RoadNode));
    { uint64_t np; memcpy(&np, &good[off], 8); off += 8; for (uint64_t k = 0; k < np; k++) { off += 12; skip(sizeof(RoadPoint)); skip(1); } }
    skip(sizeof(RoadSegment)); skip(4); skip(4); skip(4); skip(4);
    uint64_t n0; memcpy(&n0, &good[off], 8);
    std::vector<char> t(good.begin(), good.begin() + off);
    uint64_t zero = 0; t.insert(t.end(), (char*)&zero, (char*)&zero + 8);
    t.insert(t.end(), good.begin() + off + 8 + n0 * 4, good.end());
    writeAll(bad, t);
    World d; CHECK(!d.loadCache(bad, "stampA") && d.hm.empty(), "an empty height-bound array is rejected, nothing published");
  }
  {   // the map wraps (world.h WRAP_HALF): the islands in 10 km of open sea all round, the whole repeating every 100 km
      // both ways - the same ground a period on in any direction, the sea settled to its depth 3 km out, and no step
      // where the islands' square ends
    bool periodic = true, sea = true; float step = 0.f;
    for (int i = 0; i < 2000; i++) {
      const float x = -50000.f + (float)((i * 7919) % 1000) * 100.f, z = -50000.f + (float)((i * 104729) % 1000) * 100.f;   // (whole metres: a period on is exact)
      const float h = a.height(x, z);
      periodic = periodic && h == a.height(x + WRAP_SPAN, z) && h == a.height(x, z - WRAP_SPAN) && h == a.height(x - 2.f * WRAP_SPAN, z + WRAP_SPAN);
      if (std::max(fabsf(x), fabsf(z)) >= WORLD_HALF + 3000.f) sea = sea && h == -SEA_DEPTH;
    }
    for (float t = -WORLD_HALF; t <= WORLD_HALF; t += 500.f)
      for (int side = 0; side < 4; side++) {
        const float e = side & 1 ? WORLD_HALF : -WORLD_HALF;
        auto at = [&](float off) { return side < 2 ? a.height(e + (side & 1 ? off : -off), t) : a.height(t, e + (side & 1 ? off : -off)); };
        for (float o = -400.f; o < 3400.f; o += 50.f) step = std::max(step, fabsf(at(o + 50.f) - at(o)));
      }
    CHECK(periodic, "the map repeats every 100 km both ways");
    CHECK(sea, "past the islands' square, the open sea at its depth");
    printf("  (steepest 50 m step from the islands' edge out to the open sea: %.1f m)\n", step);
    CHECK(step < 15.f, "no cliff where the islands' square ends");
  }
  remove(path.c_str()); remove(bad.c_str());
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
