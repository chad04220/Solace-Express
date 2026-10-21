// The real Earth's data reader (src/earth_data.cpp) against the test fixture (tests/fixtures/earth2: real data over
// Martha's Vineyard and Nantucket) and what the Python reference read from it (expect.txt, tools/earth2/fixture.py):
// inflate, the bundles, the GLO-90 tiles, the sampling, the airport, place, landmark and road tables - and the reader's
// refusals.
#include "../src/earth_data.h"
#include <cinttypes>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) {
  checks++;
  if (!ok) { failures++; if (failures < 40) printf("FAIL %s\n", what.c_str()); }
}
static std::vector<uint8_t> readAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void testInflate(const std::string& dir) {
  const auto v = readAll(dir + "/inflate.bin");
  check(!v.empty(), "inflate vectors exist");
  size_t at = 0; int n = 0;
  while (at + 12 <= v.size()) {
    uint32_t raw, crc, z;
    memcpy(&raw, &v[at], 4); memcpy(&crc, &v[at + 4], 4); memcpy(&z, &v[at + 8], 4);
    at += 12;
    std::vector<uint8_t> out(raw + 1);
    const bool ok = earth::inflate(&v[at], z, out.data(), raw);
    check(ok && earth::crc32(out.data(), raw) == crc, fmt("inflate vector %d (%u bytes from %u)", n, raw, z));
    // a stream must fill its size exactly, and a damaged one is refused
    if (raw > 0) check(!earth::inflate(&v[at], z, out.data(), raw - 1), fmt("inflate vector %d refuses a short buffer", n));
    check(!earth::inflate(&v[at], z, out.data(), raw + 1), fmt("inflate vector %d refuses a long buffer", n));
    if (z > 8) {
      std::vector<uint8_t> bad(v.begin() + at, v.begin() + at + z);
      bad[z - 2] ^= 0x5A;   // (the Adler-32)
      check(!earth::inflate(bad.data(), z, out.data(), raw), fmt("inflate vector %d refuses a bad checksum", n));
      check(!earth::inflate(&v[at], z / 2, out.data(), raw), fmt("inflate vector %d refuses a truncated stream", n));
    }
    at += z; n++;
  }
  check(n >= 30, "inflate: every vector read");
  // garbage never crashes and is never accepted as a stream
  Rng rng(99);
  std::vector<uint8_t> junk(4096), out(65536);
  int accepted = 0;
  for (int i = 0; i < 300; i++) {
    for (auto& b : junk) b = (uint8_t)rng.next();
    junk[0] = 0x78; junk[1] = 0x9C;
    accepted += earth::inflate(junk.data(), junk.size(), out.data(), out.size());
  }
  check(accepted == 0, "inflate: random streams refused");
  const char* hello = "hello";
  check(earth::crc32((const uint8_t*)hello, 5) == 0x3610A686u, "crc32 of hello");
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "tests/fixtures/earth2";
  testInflate(dir);
  std::string why;
  auto pack = earth::Pack::open(dir, &why);
  check(pack != nullptr, "the fixture opens: " + why);
  if (!pack) { printf("earth_data_test: %d checks, %d failures\n", checks, failures); return 1; }
  check(pack->hasCover() && pack->hasLocalRoads(), "the fixture has cover and local roads");
  check(pack->box().south == 41 && pack->box().west == -72 && pack->box().north == 42 && pack->box().east == -69, "the fixture's box");
  check(!earth::Pack::open(dir + "/no-such-dir", &why) && !why.empty(), "a missing pack is refused with a reason");

  earth::Sampler sampler(pack, earth::Box{41.0, -72.0, 42.0, -69.0});
  check(sampler.fineTiles() == 2, fmt("two GLO-90 tiles loaded (%d)", sampler.fineTiles()));
  std::ifstream ex(dir + "/expect.txt");
  check(ex.good(), "expect.txt exists");
  std::string line;
  int elev = 0, coarse = 0, cover = 0, glo = 0, airports = 0, places = 0, edges = 0, landmarks = 0;
  const std::vector<earth::LandmarkRecord> lms = pack->landmarks(earth::Box{41, -72, 42, -69});
  double worst = 0;
  std::vector<earth::AirportRecord> aps = pack->airports(earth::Box{41, -72, 42, -69});
  std::unordered_map<std::string, const earth::AirportRecord*> byIdent;
  for (const auto& a : aps) byIdent[a.ident] = &a;
  std::vector<earth::PlaceRecord> pls = pack->places(earth::Box{41, -72, 42, -69});
  std::unordered_map<uint64_t, const earth::RoadEdge*> roadById[2];
  std::vector<earth::RoadEdge> rd[2] = {pack->roads(earth::Box{41, -72, 42, -69}, false), pack->roads(earth::Box{41, -72, 42, -69}, true)};
  for (int k = 0; k < 2; k++) for (const auto& e : rd[k]) roadById[k][e.id] = &e;
  while (std::getline(ex, line)) {
    std::istringstream s(line);
    std::string kind; s >> kind;
    if (kind == "elev" || kind == "coarse") {
      double lat, lon, want; s >> lat >> lon >> want;
      const double got = kind == "elev" ? sampler.elevation(lat, lon) : sampler.coarseElevation(lat, lon);
      worst = std::max(worst, std::abs(got - want));
      check(std::abs(got - want) < 2e-3, fmt("%s at %.5f, %.5f: %.4f, the reference %.4f", kind.c_str(), lat, lon, got, want));
      (kind == "elev" ? elev : coarse)++;
    } else if (kind == "cover") {
      double lat, lon; int want; s >> lat >> lon >> want;
      check(sampler.cover(lat, lon) == want, fmt("cover at %.5f, %.5f: %d, the reference %d", lat, lon, sampler.cover(lat, lon), want));
      cover++;
    } else if (kind == "glo90") {
      int lat, lon, rows, cols, r, c; double want; s >> lat >> lon >> rows >> cols >> r >> c >> want;
      auto t = pack->glo90Tile(lat, lon);
      check(t && t->rows == rows && t->cols == cols, fmt("GLO-90 %d %d is %dx%d", lat, lon, rows, cols));
      if (t && t->rows == rows && t->cols == cols) check(std::abs(t->at(r, c) - want) < 1e-4, fmt("GLO-90 %d %d sample %d %d", lat, lon, r, c));
      glo++;
    } else if (kind == "airports") {
      size_t na, nr; s >> na >> nr;
      size_t runways = 0; for (const auto& a : aps) runways += a.runways.size();
      check(aps.size() == na && runways == nr, fmt("airports: %zu with %zu runways, the reference %zu and %zu", aps.size(), runways, na, nr));
    } else if (kind == "airport") {
      std::string ident; double lat, lon; int elevM, type, nrw, len, heading;
      s >> ident >> lat >> lon >> elevM >> type >> nrw >> len >> heading;
      auto it = byIdent.find(ident);
      check(it != byIdent.end(), "airport " + ident + " found");
      if (it != byIdent.end()) {
        const auto& a = *it->second;
        check(std::abs(a.lat - lat) < 1e-6 && std::abs(a.lon - lon) < 1e-6 && a.type == type && (int)a.runways.size() == nrw &&
              (elevM == -32768 ? !a.hasElevation : a.hasElevation && a.elevation == elevM), "airport " + ident + " as the reference has it");
        if (nrw) check((int)a.runways[0].length == len && (heading == 0xFFFF ? !a.runways[0].hasHeading : a.runways[0].hasHeading && std::abs(a.runways[0].heading - heading * .1f) < 1e-3f),
                       "airport " + ident + "'s main runway");
      }
      airports++;
    } else if (kind == "places") {
      size_t n; s >> n;
      check(pls.size() == n, fmt("places: %zu, the reference %zu", pls.size(), n));
    } else if (kind == "place") {
      std::string name; uint32_t pop; double lat, lon; s >> name >> pop >> lat >> lon;
      for (auto& ch : name) if (ch == '_') ch = ' ';
      bool found = false;
      for (const auto& p : pls) found |= p.name == name && p.population == pop && std::abs(p.lat - lat) < 1e-6 && std::abs(p.lon - lon) < 1e-6;
      check(found, "place " + name);
      places++;
    } else if (kind == "landmarks") {
      size_t n; s >> n;
      check(lms.size() == n, fmt("landmarks: %zu, the reference %zu", lms.size(), n));
    } else if (kind == "landmark") {
      std::string name; double lat, lon; int k, dm; s >> name >> lat >> lon >> k >> dm;
      for (auto& ch : name) if (ch == '_') ch = ' ';
      if (name == "-") name.clear();
      bool found = false;
      for (const auto& m : lms) found |= m.name == name && m.kind == k && std::abs(m.height - dm * .1f) < 1e-3f && std::abs(m.lat - lat) < 1e-6 && std::abs(m.lon - lon) < 1e-6;
      check(found, "landmark " + name);
      landmarks++;
    } else if (kind == "roads" || kind == "lroads") {
      size_t n, pts; s >> n >> pts;
      const int k = kind == "lroads";
      size_t got = 0; for (const auto& e : rd[k]) got += e.pts.size();
      check(rd[k].size() == n && got == pts, fmt("%s: %zu edges of %zu points, the reference %zu and %zu", kind.c_str(), rd[k].size(), got, n, pts));
    } else if (kind == "edge") {
      std::string layer; uint64_t id; int cls, surf, n; long long a0, a1, b0, b1;
      s >> layer >> id >> cls >> surf >> n >> a0 >> a1 >> b0 >> b1;
      const int k = layer == "lroads";
      auto it = roadById[k].find(id);
      check(it != roadById[k].end(), fmt("edge %" PRIu64 " found", id));
      if (it != roadById[k].end()) {
        const auto& e = *it->second;
        check(e.cls == cls && e.surface == surf && (int)e.pts.size() == n && std::llround(e.pts[0].first * 1e7) == a0 &&
              std::llround(e.pts[0].second * 1e7) == a1 && std::llround(e.pts.back().first * 1e7) == b0 && std::llround(e.pts.back().second * 1e7) == b1,
              fmt("edge %" PRIu64 " as the reference has it", id));
      }
      edges++;
    }
  }
  check(elev > 200 && coarse > 200 && cover > 200 && glo >= 6 && airports > 3 && places > 10 && edges > 20 && landmarks > 20, "expect.txt read through");
  // the sampler's sense: Martha's Vineyard airport stands at about 20 m on GLO-90, the sea off Nantucket is below 0
  check(std::abs(sampler.elevation(41.393, -70.614) - 20.f) < 12.f, fmt("KMVY's ground %.1f m", sampler.elevation(41.393, -70.614)));
  check(sampler.cover(41.2, -70.3) == earth::COVER_SEA, "open sea south of the islands");
  check(sampler.fine(41.4, -70.6) && !sampler.fine(41.7, -70.6) && !sampler.fine(41.4, -71.5), "GLO-90 where the fixture has it");
  // a damaged bundle is refused, never read through
  {
    const std::string tmp = "earth_data_test_damaged";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp + "/elev");
    // the index and the elevation bundle, one of its tiles' bytes flipped
    const auto idx = readAll(dir + "/pack.idx");
    std::string text(idx.begin(), idx.end()), keep;
    std::istringstream ls(text);
    std::string l;
    while (std::getline(ls, l)) if (l.rfind("file elev/N40W080.svb", 0) == 0 || l.rfind("somnium", 0) == 0 || l.rfind("box", 0) == 0 || l == "end") keep += l + "\n";
    std::ofstream(tmp + "/pack.idx", std::ios::binary) << keep;
    auto b = readAll(dir + "/elev/N40W080.svb");
    check(b.size() > 3000, "the elevation bundle exists");
    if (b.size() > 3000) {
      b[b.size() - 50] ^= 0x33;
      std::ofstream(tmp + "/elev/N40W080.svb", std::ios::binary).write((const char*)b.data(), b.size());
      auto bad = earth::Pack::open(tmp, &why);
      check(bad != nullptr, "the damaged pack opens (its sizes are right): " + why);
      bool threw = false;
      try {
        for (int lon = -72; bad && lon < -70; lon++) bad->elevTile(41, lon);
      } catch (const std::runtime_error&) { threw = true; }
      check(threw, "a damaged tile throws, never decodes");
    }
  }
  printf("earth_data_test: worst sample difference %.2e m; %d checks, %d failures\n", worst, checks, failures);
  return failures ? 1 : 0;
}
