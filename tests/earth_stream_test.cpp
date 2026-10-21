// The real Earth streamed (earth_data.h, earth_install.h), with no network: the fixture packaged as the global build
// is (tools/earth2 package), installed streamed from a file address - the core alone - and read from there through
// curl's ranges (earthFetch::fetchRange). Every tile of every layer comes out as the whole pack has it, each piece kept
// in the cache folder once it has come; opened again, it reads them from there and fetches nothing - and past the
// cache's limit, it is trimmed, what was read longest ago first, and fetched again when wanted. A chart built from
// the streamed pack is the chart built from the whole one. With the source gone, what was kept still reads; a piece
// not kept throws, saying why (the source gone: streamStats().gone), and isn't asked for again at once.
#include "../src/earth_install.h"
#include "../src/earth_charts.h"
#include "../src/world.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

static int failed = 0;
static void check(bool pass, const std::string& label) { printf("Earth stream: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass; }
static std::string quote(const std::string& s) { std::string q = "'"; for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; } return q + "'"; }

// every tile the pack's box holds, every layer, the same in both (each null in both, or the same samples or edges)
static bool sameTiles(const earth::Pack& a, const earth::Pack& b, int& tiles) {
  const earth::Box box = a.box();
  bool same = true;
  tiles = 0;
  for (int lat = (int)box.south; lat < (int)box.north; lat++)
    for (int lon = (int)box.west; lon < (int)box.east; lon++) {
      auto ea = a.elevTile(lat, lon), eb = b.elevTile(lat, lon);
      auto ca = a.coverTile(lat, lon), cb = b.coverTile(lat, lon);
      same &= (!ea && !eb) || (ea && eb && ea->s16 == eb->s16);
      same &= (!ca && !cb) || (ca && cb && ca->s8 == cb->s8);
      for (bool local : {false, true}) {
        auto ra = a.roadTile(lat, lon, local), rb = b.roadTile(lat, lon, local);
        same &= ra->size() == rb->size();
        for (size_t k = 0; same && k < ra->size(); k++) same &= (*ra)[k].id == (*rb)[k].id && (*ra)[k].pts == (*rb)[k].pts;
      }
      tiles += (ea != nullptr) + (ca != nullptr);
    }
  return same;
}

int main(int argc, char** argv) {
  const std::string fixture = fs::absolute(argc > 1 ? argv[1] : "tests/fixtures/earth2").string();
  const std::string sourceRoot = argc > 2 ? argv[2] : ".";
  if (std::system("curl --version > /dev/null 2>&1") != 0) { printf("earth_stream_test: no curl, skipped\n"); return 0; }
  std::string why;
  auto whole = earth::Pack::open(fixture, &why);
  check(whole != nullptr, "the fixture opens " + why);
  if (!whole) return 1;
  const fs::path root = fs::temp_directory_path() / ("earth_stream_test_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path dist = root / "dist", install = root / "user" / "earth";
  fs::create_directories(root);
  const std::string py = "cd " + quote(sourceRoot) + " && python3 -m tools.earth2 package " + quote(fixture) + " " + quote(dist.string()) + " --max-bytes 400000 > /dev/null";
  check(std::system(py.c_str()) == 0, "the fixture packaged");
  {
    earthInstall::Installer in("file://" + dist.string() + "/", (root / "work").string(), install.string());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (in.state() == earthInstall::Installer::RUNNING && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(in.state() == earthInstall::Installer::DONE, "installed streamed: " + in.status());
  }
  // the pieces fetched through curl's ranges, counted
  std::atomic<int> fetches{0};
  earth::setRangeFetch([&](const std::string& url, uint64_t offset, uint64_t length, std::vector<uint8_t>& out, std::string* error) {
    fetches++;
    return earthFetch::fetchRange(url, offset, length, out, error);
  });
  {
    auto pack = earth::Pack::open(install.string(), &why);
    check(pack && pack->streamed() && pack->streamStats().kept == 0, "the streamed pack opens, nothing kept yet " + why);
    if (!pack) return 1;
    int tiles = 0;
    const bool same = sameTiles(*whole, *pack, tiles);
    const auto st = pack->streamStats();
    check(same && tiles > 0, fmt("every tile of every layer as the whole pack's (%d raster tiles)", tiles));
    check(st.fetched > 0 && st.fetched == fetches.load() && st.failed == 0 && st.kept == st.bytes && fs::exists(install / "streamed"),
          fmt("fetched piece by piece: %d pieces, %.0f kB, kept in the cache folder", st.fetched, st.bytes / 1e3));
    check(st.bytes <= st.remote, fmt("no more than the bundles themselves (%.0f of %.0f kB)", st.bytes / 1e3, st.remote / 1e3));
  }
  {   // opened again: read from the cache folder, nothing fetched
    fetches = 0;
    auto pack = earth::Pack::open(install.string(), &why);
    int tiles = 0;
    check(pack && sameTiles(*whole, *pack, tiles) && fetches.load() == 0 && pack->streamStats().kept > 0,
          fmt("opened again: the same, from what was kept, %d fetched", fetches.load()));
  }
  {   // past the cache's limit: trimmed when the pack opens, the pieces read longest ago first, to three quarters of it -
      // and what went fetched again when it is wanted, the same
    uint64_t kept = 0;
    { auto pack = earth::Pack::open(install.string()); kept = pack ? pack->streamStats().kept : 0; }
    const uint64_t limit = kept / 2;
    earth::setStreamCacheLimit(limit);
    fetches = 0;
    auto pack = earth::Pack::open(install.string(), &why);
    const uint64_t trimmed = pack ? pack->streamStats().kept : 0;
    int tiles = 0;
    const bool same = pack && sameTiles(*whole, *pack, tiles);
    check(kept > 0 && trimmed <= limit / 4 * 3 && trimmed > 0 && same && fetches.load() > 0,
          fmt("trimmed to its limit (%.0f of %.0f kB kept), what went fetched again, the same (%d fetched)", trimmed / 1e3, kept / 1e3, fetches.load()));
    earth::setStreamCacheLimit(2ull << 30);
  }
  {   // a chart built from the streamed pack (fetching what isn't kept: the cache folder emptied) is the whole pack's
    fs::remove_all(install / "streamed");
    auto streamed = earth::Pack::open(install.string(), &why);
    streamed->setGlo90Cache(fixture + "/glo90");   // (the fine elevation the fetcher would bring: the fixture's own)
    World a, b;
    earthCharts::usePack(whole);
    const auto chart = earthCharts::chartForAirport("KMVY");
    a.buildProcedural(*earthCharts::chartForAirport("KMVY"), "", "", false);
    earthCharts::usePack(streamed);
    fetches = 0;
    b.buildProcedural(*earthCharts::chartForAirport("KMVY"), "", "", false);
    bool same = a.hm == b.hm && a.airports.size() == b.airports.size() && a.towns.size() == b.towns.size() && a.roads.paths.size() == b.roads.paths.size();
    for (size_t i = 0; same && i < a.airports.size(); i++) same &= !strcmp(a.airports[i].code, b.airports[i].code) && a.airports[i].elev == b.airports[i].elev;
    check(chart && same && fetches.load() > 0, fmt("a chart built streamed (%d pieces fetched) is the whole pack's: its ground, airports, towns, roads", fetches.load()));
    earthCharts::usePack(nullptr);
  }
  {   // the source gone: what was kept reads; a piece not kept throws with the reason, and isn't asked for again at once
    { std::ofstream f(install / "stream.txt"); f << "file://" << (root / "gone").string() << "/\n"; }
    fs::path dropped;
    for (const auto& e : fs::recursive_directory_iterator(install / "streamed"))
      if (e.is_regular_file() && e.path().extension() != ".head" && e.path().string().find("/elev/") != std::string::npos) { dropped = e.path(); break; }
    check(!dropped.empty(), "a kept elevation tile to drop " + dropped.filename().string());
    fs::remove(dropped);
    const std::string corner = dropped.filename().string();   // (N41W071: its latitude and longitude)
    const int lat = (corner[0] == 'S' ? -1 : 1) * atoi(corner.substr(1, 2).c_str()), lon = (corner[3] == 'W' ? -1 : 1) * atoi(corner.substr(4, 3).c_str());
    auto pack = earth::Pack::open(install.string(), &why);
    fetches = 0;
    bool kept = true;
    for (int la = (int)whole->box().south; la < (int)whole->box().north; la++)
      for (int lo = (int)whole->box().west; lo < (int)whole->box().east; lo++) if (la != lat || lo != lon) kept &= (pack->coverTile(la, lo) != nullptr) == (whole->coverTile(la, lo) != nullptr);
    check(kept && fetches.load() == 0, "offline: every tile kept still reads, nothing fetched");
    std::string first, second;
    try { pack->elevTile(lat, lon); } catch (const std::exception& e) { first = e.what(); }
    const int asked = fetches.load();
    try { pack->elevTile(lat, lon); } catch (const std::exception& e) { second = e.what(); }
    check(first.find("couldn't be fetched") != std::string::npos && pack->streamStats().gone, "a tile not kept: thrown, the source gone - " + first);
    check(!second.empty() && fetches.load() == asked && asked == 1, fmt("asked for again at once: thrown without a fetch (%d fetched)", fetches.load()));
  }
  earth::setRangeFetch(nullptr);
  std::error_code ec; fs::remove_all(root, ec);
  printf("earth_stream_test: %s\n", failed ? "FAILED" : "passed");
  return failed ? 1 : 0;
}
