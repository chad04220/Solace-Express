#include "../src/procedural_regions.h"
#include "../src/airport_layout.h"
#include <array>
#include <cstring>
#include <future>
#include <iostream>
#include <set>
#include <deque>
#include <stdexcept>

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* name) {
  ++checks;
  if (!ok) { ++failures; if (failures < 35) std::cerr << "FAIL: " << name << '\n'; }
}
std::array<float, 4> texel(const procedural::Recipe& r, float x, float z) {
  std::array<float, 4> out{}; r.sample(x, z, out.data()); return out;
}
uint64_t fingerprint(const procedural::Recipe& recipe) {
  uint64_t hash = 1469598103934665603ULL;
  for (int z = -37000; z <= 37000; z += 1700) for (int x = -37000; x <= 37000; x += 1700) {
    const auto t = texel(recipe, float(x), float(z));
    for (float v : t) { uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); hash = (hash ^ bits) * 1099511628211ULL; }
  }
  return hash;
}
float airportExclusion(const procedural::Recipe& recipe, float x, float z) {
  float influence = 0.f;
  for (const Airport& a : recipe.airports) {
    const vec2 uv = aptLocal(a, {x, 0.f, z});
    const float u = std::abs(uv.x), v = std::abs(uv.y), d = u - a.length * .5f;
    if (d > 0.f && d < 7500.f) {
      const float old = (1.f - smoothstepf(250.f + .18f * d, 1150.f + .18f * d, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
      const float next = (1.f - smoothstepf(300.f + .20f * d, 950.f + .20f * d, v)) * (1.f - smoothstepf(6000.f, 7500.f, d));
      influence = std::max(influence, std::max(old, next));
    }
    const float du = std::max(0.f, u - a.length * .5f - 260.f), dv = std::max(0.f, v - (a.size == 2 ? 420.f : 240.f));
    influence = std::max(influence, 1.f - smoothstepf(0.f, 550.f, std::hypot(du, dv)));
  }
  return influence;
}
bool dryConnection(const procedural::Recipe& recipe, const Airport& a, const Town& t) {
  // A bounded local flood test, not a pre-baked road. In particular, it must
  // follow an atoll's curved reef rather than bridge the lagoon's wet chord.
  const float cell = 140.f, x0 = std::min(a.x, t.x) - 2400.f, z0 = std::min(a.z, t.z) - 2400.f;
  const int nx = int((std::abs(a.x - t.x) + 4800.f) / cell) + 1;
  const int nz = int((std::abs(a.z - t.z) + 4800.f) / cell) + 1;
  std::vector<unsigned char> dry(size_t(nx) * nz), seen(dry.size());
  for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
    const auto h = texel(recipe, x0 + x * cell, z0 + z * cell);
    dry[size_t(z) * nx + x] = h[0] - 2.f * h[1] > 1.f;
  }
  const int start = int(std::lround((a.z - z0) / cell)) * nx + int(std::lround((a.x - x0) / cell));
  const int goal = int(std::lround((t.z - z0) / cell)) * nx + int(std::lround((t.x - x0) / cell));
  std::deque<int> queue{start}; seen[start] = 1;
  while (!queue.empty()) {
    const int index = queue.front(); queue.pop_front(); if (index == goal) return true;
    const int x = index % nx, z = index / nx;
    for (const auto& step : {std::pair<int,int>{-1,0}, {1,0}, {0,-1}, {0,1}}) {
      const int xx = x + step.first, zz = z + step.second;
      if (xx < 0 || xx >= nx || zz < 0 || zz >= nz) continue;
      const int next = zz * nx + xx;
      if (dry[next] && !seen[next]) { seen[next] = 1; queue.push_back(next); }
    }
  }
  return false;
}
void checkAirports(const procedural::Recipe& recipe) {
  expect(recipe.airports.size() >= 3 && recipe.airports.size() <= 6, "bounded useful airport population");
  expect(recipe.towns.size() == recipe.airports.size(), "a separate settlement for each field");
  for (size_t i = 0; i < recipe.airports.size(); ++i) {
    const Airport& a = recipe.airports[i]; const Town& t = recipe.towns[i];
    expect(a.code && std::strlen(a.code) == 3 && a.name && std::strlen(a.name) > 5, "owned airport name and compact code");
    expect(std::hypot(t.x - a.x, t.z - a.z) > 1200.f, "town is physically separate from runway");
    expect(airportExclusion(recipe, t.x, t.z) < .02f, "town center clears airport scenery exclusion");
    expect(dryConnection(recipe, a, t), "airport and town share a dry land route");
    for (float u : {-a.length * .5f - 120.f, -a.length * .5f, 0.f, a.length * .5f, a.length * .5f + 120.f}) {
      for (float v : {-200.f, -a.width * .5f, 0.f, a.width * .5f, 200.f}) {
        const vec3 p = aptWorld(a, u, v, 0.f); const auto b = texel(recipe, p.x, p.z);
        expect(b[0] > 3.f && std::abs(b[0] - a.elev) < .02f && b[1] < .001f, "dry exact runway and safety apron");
      }
    }
    const AptLayout layout = aptLayout(a, int(i));
    for (float u : {layout.apU0, layout.apU1, layout.termU}) for (float v : {layout.apV0, layout.apV1, layout.bldV + 50.f}) {
      const vec3 p = aptWorld(a, u, layout.side * v, 0.f); const auto b = texel(recipe, p.x, p.z);
      expect(std::abs(b[0] - a.elev) < .02f && b[1] < .001f, "existing airport layout fits flat pad");
    }
    for (float sign : {-1.f, 1.f}) for (float distance : {350.f, 1000.f, 2500.f, 4000.f, 5800.f}) {
      for (float side : {-.85f, 0.f, .85f}) {
        const vec3 p = aptWorld(a, sign * (a.length * .5f + distance), side * (300.f + .2f * distance), 0.f);
        const auto b = texel(recipe, p.x, p.z);
        expect(b[0] + 2.f * b[1] <= a.elev + 8.f + .03f * distance + .1f, "entire approach funnel stays beneath obstacle ceiling");
      }
    }
    for (int j = 0; j < 12; ++j) {
      const float angle = 2.f * PI * j / 12.f;
      const float x = t.x + .9f * t.r * std::cos(angle), z = t.z + .9f * t.r * std::sin(angle);
      expect(airportExclusion(recipe, x, z) < .02f, "town footprint clears runway/approach exclusions");
      const auto b = texel(recipe, x, z);
      expect(b[0] > 3.f && std::abs(b[0] - a.elev - 3.f) < .03f && b[1] < .001f, "dry flat separate town footprint");
    }
  }
}
}
int main() {
  using namespace procedural;
  const Descriptor original = solaceDescriptor();
  expect(original.authored && original.id == "solace", "Solace authored identity preserved");
  expect(describe(geo::solaceOrigin()).authored, "Solace origin resolves to original");
  expect(find("solace")->origin.longitude == -58., "Solace chart anchor preserved");
  for (const char* id : {"", "pr2-0-1-1", "pr1-6-0-0", "pr1-0-32-0", "pr1-0--1-0", "pr1-0-01-0", "pr1-0-1-1/../../", "pr1-999999999999999-0-0"})
    expect(!find(id), "invalid or unsafe catalog ID rejected");
  expect(nearby(geo::solaceOrigin(), 0).empty(), "zero requested discoveries allocate no entries");
  expect(nearby(geo::solaceOrigin(), 100000).size() == kMaxNearby, "discovery request has hard upper bound");
  std::set<std::string> identities;
  int reservedCells = 0;
  for (int face = 0; face < 6; ++face) for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
    const auto position = geo::fromDirection(geo::tileDirection({geo::Face(face), kAddressLevel, x, y}, .5, .5));
    const auto d = describe(position);
    if (d.authored) { ++reservedCells; continue; }
    expect(identities.insert(d.id).second, "global addresses are unique");
    expect(find(d.id) && find(d.id)->seed == d.seed, "every descriptor independently addressable");
    expect(geo::surfaceDistance(d.origin, geo::solaceOrigin()) >= 150000., "generated wrapper cannot overlap Solace");
  }
  expect(identities.size() > 6100 && reservedCells > 0, "thousands of lightweight Earth-addressed regions");
  for (double latitude : {-90., -89.999, -45., 0., 45., 89.999, 90.}) {
    expect(describe({latitude, 180., 0.}).id == describe({latitude, -180., 99999.}).id, "canonical antimeridian and height-independent addressing");
    const auto near = nearby({latitude, 180., 0.});
    std::set<std::string> unique;
    double previous = -1.;
    for (const auto& d : near) {
      expect(unique.insert(d.id).second, "no duplicate entries across cube seams");
      const double distance = geo::surfaceDistance({latitude, 180., 0.}, d.origin);
      expect(distance >= previous, "discoveries sorted by great-circle distance"); previous = distance;
      expect(distance < 1250000., "bounded discovery remains local at poles and seams");
    }
  }
  expect(describe({90., -124., 0.}).id == describe({90., 73., 0.}).id, "north pole longitude aliases");
  expect(describe({-90., -124., 0.}).id == describe({-90., 73., 0.}).id, "south pole longitude aliases");
  const auto suggested = recommendations();
  expect(suggested.front().authored, "suggestions begin with original Solace");
  std::set<int> forms;
  std::set<uint64_t> heights;
  std::vector<Descriptor> examples;
  for (const Descriptor& d : suggested) {
    if (d.authored) continue;
    if (forms.insert(int(d.archetype)).second) examples.push_back(d);
  }
  expect(forms.size() == 5, "suggestions include every distinct landform");
  for (const auto& d : examples) {
    const Recipe a(d), b(d);
    const uint64_t before = fingerprint(a);
    expect(before == fingerprint(b), "same seed/version/address exactly reproduces sample bits");
    expect(heights.insert(before).second, "different landforms have different terrain signatures");
    expect(a.memoryBytes() < 16384, "analytic recipe has bounded kilobyte footprint");
    checkAirports(a);
    float top = -1000.f; size_t dry = 0;
    for (int z = -34000; z <= 34000; z += 850) for (int x = -34000; x <= 34000; x += 850) {
      const auto t = texel(a, float(x), float(z));
      for (float v : t) expect(std::isfinite(v), "sample channels are finite");
      expect(t[0] >= -SEA_DEPTH - .01f && t[1] >= 0.f && t[2] >= 0.f && t[2] <= 1.f && t[3] >= 0.f && t[3] <= 1.f, "terrain channels within documented bounds");
      top = std::max(top, t[0]); if (t[0] > 0.f) ++dry;
    }
    expect(dry > 100, "region has meaningful flyable land");
    if (d.archetype == Archetype::Atolls) expect(top < 35.f, "atolls remain low reef landscapes");
    if (d.archetype == Archetype::VolcanicChain) expect(top > 800.f, "volcanoes have substantial caldera relief");
    if (d.archetype == Archetype::Fjords) expect(top > 650.f, "fjords have mountain relief");
    for (float edge : {-50000.f, -40000.f, -38000.f, 38000.f, 40000.f, 50000.f}) {
      for (float along : {-50000.f, -21000.f, 0.f, 17500.f, 50000.f}) {
        const auto x = texel(a, edge, along), z = texel(a, along, edge);
        expect(x[0] == -SEA_DEPTH && z[0] == -SEA_DEPTH && x[1] == 0.f && z[1] == 0.f, "every wrapper edge is exactly open ocean");
      }
    }
    std::vector<std::future<uint64_t>> parallel;
    for (int i = 0; i < 4; ++i) parallel.push_back(std::async(std::launch::async, [&a] { return fingerprint(a); }));
    for (auto& f : parallel) expect(f.get() == before, "recipe samples are thread-safe and order-independent");
    const Descriptor differentSeed = *find(d.id, d.worldSeed + 1);
    const Recipe changed(differentSeed);
    expect(fingerprint(changed) != before, "world seed changes terrain but retains geographic address");
    expect(geo::surfaceDistance(differentSeed.origin, d.origin) == 0., "world seed never moves the geographic address");
    expect(fingerprint(a) == before, "other region generation cannot change an existing recipe");
  }
  for (uint64_t seed : {1ULL, 2ULL, 17ULL, 63ULL, 255ULL, 1024ULL, 54321ULL, 0xffffffffffffffffULL}) {
    std::set<int> checked;
    for (const auto& d : recommendations(seed)) if (!d.authored && checked.insert(int(d.archetype)).second) checkAirports(Recipe(d));
    expect(checked.size() == 5, "each seed still offers every landform");
  }
  // Preserve legacy char pointers for the exact shared owner lifetime used by World.
  auto owner = std::make_shared<Recipe>(examples.front());
  const auto fields = owner->airports; const std::string name = fields.front().name;
  std::shared_ptr<const Recipe> immutableOwner = owner; owner.reset();
  expect(name == fields.front().name, "airport strings survive shared recipe ownership");
  bool rejected = false; try { Recipe cannotGenerateSolace(original); } catch (const std::invalid_argument&) { rejected = true; }
  expect(rejected, "authored region cannot accidentally be regenerated");
  std::cout << "procedural_regions_test: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
