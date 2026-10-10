// The islands' cache (World::saveCache / loadCache): a valid file reads back identical, and a wrong stamp, a truncated
// file, a derived array of the wrong size or trailing bytes are all rejected (the world is then generated again).
#include "../src/world.h"
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
  remove(path.c_str()); remove(bad.c_str());
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
