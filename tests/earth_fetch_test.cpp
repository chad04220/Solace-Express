// The real Earth's fine elevation fetched as it is wanted (earth_fetch.h), with no network: a transfer of the test's own
// in place of the download. The tiles a chart reads, nearest first; only those Copernicus has and neither the pack nor
// the cache has yet are fetched, one at a time; a failed one is not tried again; a tile fetched is found by the pack
// (after its own); the fetcher stops mid-download when it goes. And the download itself, through curl, from a file
// address, where curl is found.
#include "../src/earth_fetch.h"
#include "../src/geo.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sys/stat.h>

static int failed = 0;
static void check(bool pass, const std::string& label) { printf("Earth fetch: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass; }
static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

int main(int argc, char** argv) {
  std::string why;
  auto pack = earth::Pack::open(argc > 1 ? argv[1] : "tests/fixtures/earth2", &why);
  check(pack != nullptr, "the pack opens " + why);
  if (!pack) return 1;
  // around Martha's Vineyard: its own tile first, then outwards; each once
  const auto tiles = earthFetch::tilesAround(41.39, -70.61, 60000.0);
  check(!tiles.empty() && tiles[0] == std::make_pair(41, -71), "the tile under the place first");
  check(std::set<std::pair<int, int>>(tiles.begin(), tiles.end()).size() == tiles.size(), fmt("each tile once (%zu)", tiles.size()));
  double last = -1; bool ordered = true, near = true;
  for (const auto& t : tiles) {
    const double d = geo::surfaceDistance({41.39, -70.61, 0}, {std::clamp(41.39, (double)t.first, t.first + 1.0), std::clamp(-70.61, (double)t.second, t.second + 1.0), 0});
    ordered &= d >= last; near &= d <= 60000.0 * 1.02; last = d;
  }
  check(ordered && near, "nearest first, all within reach");
  const auto dateline = earthFetch::tilesAround(0.2, 179.9, 30000.0);
  bool wrapped = false, inRange = true;
  for (const auto& t : dateline) { wrapped |= t.second == -180; inRange &= t.second >= -180 && t.second < 180; }
  check(wrapped && inRange, "across the date line, wrapped");

  // farther out, to Rhode Island: of the 15 tiles the fixture's bitmap lists three (Copernicus has them), the pack
  // carries two; N41W072 is what a chart there would download
  const auto wider = earthFetch::tilesAround(41.39, -70.61, 150000.0);
  const std::pair<int, int> missing{41, -72};
  check(std::find(wider.begin(), wider.end(), missing) != wider.end() && pack->glo90Listed(41, -72) && pack->glo90File(41, -72).empty(),
        fmt("N41W072 within reach, listed, not in the pack (%zu tiles)", wider.size()));
  if (std::system("rm -rf earth_fetch_test_cache")) {}
  std::vector<std::string> asked;
  std::mutex askedLock;
  auto transfer = [&](bool works) {
    return [&, works](const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>*) {
      { std::lock_guard<std::mutex> g(askedLock); asked.push_back(url); }
      if (!works) { if (error) *error = "HTTP 503"; return false; }
      FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
      fputs("tile", f); fclose(f); return true;
    };
  };
  const std::string dir = "earth_fetch_test_cache/glo90", tile = dir + "/" + earth::glo90Name(41, -72) + ".tif";
  {
    earthFetch::Glo90Fetcher fetch(dir, transfer(true));
    check(exists(dir), "the cache folder made");
    fetch.want(*pack, wider);
    check(fetch.settle(10.0) && fetch.pending() == 0, "settled");
    check(asked.size() == 1 && asked[0] == earthFetch::glo90Url(41, -72), fmt("only the listed tile the pack hasn't asked for (%zu asked)", asked.size()));
    check(fetch.fetched() == 1 && fetch.failed() == 0 && exists(tile), "fetched, into the cache");
    fetch.want(*pack, wider);
    fetch.settle(10.0);
    check(asked.size() == 1, "wanted again: not asked for again (the cache has it)");
    pack->setGlo90Cache(dir);
    check(pack->glo90File(41, -72) == tile, "the pack finds the fetched tile");
    check(pack->glo90File(41, -71).find("earth2") != std::string::npos, "and its own before the cache");
    pack->setGlo90Cache("");
  }
  asked.clear();
  {
    earthFetch::Glo90Fetcher fetch("earth_fetch_test_cache/offline", transfer(false));
    fetch.want(*pack, wider);
    check(fetch.settle(10.0) && fetch.failed() == 1 && fetch.fetched() == 0 && fetch.lastError() == "HTTP 503", "a failed download: counted, with its reason");
    check(!exists("earth_fetch_test_cache/offline/" + earth::glo90Name(41, -72) + ".tif"), "nothing in the cache for it");
    fetch.want(*pack, wider);
    fetch.settle(10.0);
    check(asked.size() == 1, "not tried again this session");
  }
  {   // a download under way when the fetcher goes: told to stop, and the game isn't kept waiting
    std::atomic<bool> started{false};
    auto begun = std::chrono::steady_clock::now();
    {
      earthFetch::Glo90Fetcher fetch("earth_fetch_test_cache/slow", [&](const std::string&, const std::string&, std::string* error, const std::atomic<bool>* cancel) {
        started = true;
        while (!cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (error) *error = "cancelled";
        return false;
      });
      fetch.want(*pack, {missing});
      check(!fetch.settle(0.2) && fetch.pending() == 1, "a slow download pending");
      check(started.load(), "under way");
      begun = std::chrono::steady_clock::now();
    }
    check(std::chrono::steady_clock::now() - begun < std::chrono::seconds(2), "stopped when the fetcher goes");
  }
  if (std::system("curl --version > /dev/null 2>&1") == 0) {
    FILE* f = fopen("earth_fetch_test_cache/source.bin", "wb");
    for (int i = 0; i < 100000; i++) fputc(i & 255, f);
    fclose(f);
    char cwd[4096] = {};
    const std::string here = realpath(".", cwd) ? std::string(cwd) : std::string(".");
    std::string e;
    check(earthFetch::download("file://" + here + "/earth_fetch_test_cache/source.bin", "earth_fetch_test_cache/copy.bin", &e) &&
          exists("earth_fetch_test_cache/copy.bin") && !exists("earth_fetch_test_cache/copy.bin.part"), "a download, whole, in its place " + e);
    struct stat st; check(stat("earth_fetch_test_cache/copy.bin", &st) == 0 && st.st_size == 100000, "every byte");
    check(!earthFetch::download("file://" + here + "/earth_fetch_test_cache/none.bin", "earth_fetch_test_cache/none.bin", &e) &&
          !exists("earth_fetch_test_cache/none.bin") && !exists("earth_fetch_test_cache/none.bin.part") && !e.empty(), "a missing one: false, nothing left behind (" + e + ")");
    // resuming (the Earth pack's zips): a part already there carried on from, byte for byte; a failed one's part kept
    {
      FILE* src = fopen("earth_fetch_test_cache/source.bin", "rb"); FILE* half = fopen("earth_fetch_test_cache/resumed.bin.part", "wb");
      for (int i = 0; i < 40000; i++) fputc(fgetc(src), half);
      fclose(half); fclose(src);
    }
    e.clear();
    bool same = earthFetch::downloadResuming("file://" + here + "/earth_fetch_test_cache/source.bin", "earth_fetch_test_cache/resumed.bin", &e) &&
                !exists("earth_fetch_test_cache/resumed.bin.part");
    if (FILE* a = fopen("earth_fetch_test_cache/resumed.bin", "rb")) {
      for (int i = 0; i < 100000 && same; i++) same = fgetc(a) == (i & 255);
      same = same && fgetc(a) == EOF; fclose(a);
    } else same = false;
    check(same, "a download carried on from its part, every byte as it should be " + e);
    if (FILE* p = fopen("earth_fetch_test_cache/gone.bin.part", "wb")) { fputs("kept", p); fclose(p); }
    check(!earthFetch::downloadResuming("file://" + here + "/earth_fetch_test_cache/none.bin", "earth_fetch_test_cache/gone.bin", &e) &&
          exists("earth_fetch_test_cache/gone.bin.part"), "a failed one carried on from: its part kept for the next try");
  } else puts("Earth fetch: no curl here, the download itself not tried");
  if (std::system("rm -rf earth_fetch_test_cache")) {}
  printf("earth_fetch_test: %s\n", failed ? "FAILED" : "passed");
  return failed ? 1 : 0;
}
