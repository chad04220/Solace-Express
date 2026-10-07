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
  bool same = b.fromCache && a.hm == b.hm && a.roadId == b.roadId && a.mask == b.mask && a.tpV0 == b.tpV0;
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
    skip(4); skip(1); skip(1);   // hm, roadId, mask
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
