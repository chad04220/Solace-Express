// Analytic shape regression: no World generation, render context or assets.
#include "../src/procedural_regions.h"
#include <cstring>
#include <deque>
#include <future>
#include <iostream>

namespace {
int checks = 0, failures = 0;
void expect(bool pass, const char* label) {
  ++checks;
  if (!pass) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
std::array<float, 4> at(const procedural::Recipe& r, float x, float z) {
  std::array<float, 4> h; r.sample(x, z, h.data()); return h;
}
struct Frame {
  float c, s;
  explicit Frame(const procedural::Recipe& r) : c(std::cos(r.airports[0].heading * DEG)), s(std::sin(r.airports[0].heading * DEG)) {}
  std::array<float, 4> at(const procedural::Recipe& r, float x, float z) const {
    return ::at(r, x * c - z * s, x * s + z * c);
  }
};
uint64_t fingerprint(const procedural::Recipe& r) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (int z = -37500; z <= 37500; z += 750) for (int x = -37500; x <= 37500; x += 750) {
    const auto h = at(r, float(x), float(z));
    for (float value : h) { uint32_t bits; std::memcpy(&bits, &value, sizeof bits); hash = (hash ^ bits) * UINT64_C(1099511628211); }
  }
  return hash;
}
void atolls() {
  const procedural::Recipe r(*procedural::find("pr1-1-9-19"));
  expect(r.descriptor.archetype == procedural::Archetype::Atolls, "atoll fixture retains catalog identity");
  for (size_t i = 0; i < r.airports.size(); ++i) {
    // Independently recover the original reef frame from the unchanged field
    // and town placements; do not inspect private recipe coefficients.
    const auto& a = r.airports[i]; const auto& town = r.towns[i];
    const float c = std::cos(a.heading * DEG), s = std::sin(a.heading * DEG);
    const float dx = town.x - a.x, dz = town.z - a.z;
    const float rx = -(dx * c + dz * s) / (1.f - std::cos(1.05f));
    const float rz = (-dx * s + dz * c) / std::sin(1.05f);
    const float cx = a.x - rx * c, cz = a.z - rx * s;
    auto polar = [&](float angle, float radius) {
      const float x = rx * radius * std::cos(angle), z = rz * radius * std::sin(angle);
      return at(r, cx + x * c - z * s, cz + x * s + z * c);
    };
    expect(at(r, cx, cz)[0] < -70.f, "atoll has a submerged lagoon");
    float minRadius = 2.f, maxRadius = 0.f, minWidth = 2.f, maxWidth = 0.f;
    int gaps = 0, dryRays = 0;
    for (int k = 0; k < 180; ++k) {
      const float angle = 2.f * PI * k / 180.f;
      if (angle < 1.8f || angle > 5.5f) continue; // inspect unprotected reef
      float lo = 9.f, hi = -9.f;
      for (int j = 0; j <= 200; ++j) {
        const float radius = .5f + .005f * j; const auto h = polar(angle, radius);
        expect(h[0] < 35.f, "irregular reef stays low");
        if (h[0] - 2.f * h[1] > 1.f) { lo = std::min(lo, radius); hi = std::max(hi, radius); }
      }
      if (hi < 0.f) { ++gaps; continue; }
      ++dryRays;
      minRadius = std::min(minRadius, .5f * (lo + hi)); maxRadius = std::max(maxRadius, .5f * (lo + hi));
      minWidth = std::min(minWidth, hi - lo); maxWidth = std::max(maxWidth, hi - lo);
    }
    expect(maxRadius - minRadius > .12f, "uninhabited rim is materially asymmetric");
    expect(maxWidth > 2.f * minWidth && maxWidth > .18f, "reef has both narrow rims and broad islands");
    expect(gaps >= 3 && dryRays > 55, "reef includes open lagoon passages and substantial surviving land");
    for (int j = 0; j <= 42; ++j) {
      const auto h = polar(1.05f * j / 42.f, 1.f);
      expect(h[0] - 2.f * h[1] > 1.f, "inhabited arc remains a continuous dry connection");
    }
    for (float radius : {.85f, 1.f, 1.15f}) {
      const auto a = polar(-PI + .00001f, radius), b = polar(PI - .00001f, radius);
      expect(std::abs(a[0] - b[0]) < .5f, "periodic profile has no angular seam");
    }
    std::cout << "ATOLL " << i << " radial_span=" << maxRadius - minRadius << " widths=" << minWidth << ':' << maxWidth << " gap_rays=" << gaps << '\n';
  }
}
void fjords() {
  const procedural::Recipe r(*procedural::find("pr1-4-17-7")); const Frame frame(r);
  expect(r.descriptor.archetype == procedural::Archetype::Fjords, "fjord fixture retains catalog identity");
  auto center = [&](float z, float x0, float x1) {
    float sum = 0; int n = 0;
    for (float x = x0; x <= x1; x += 100.f) if (frame.at(r, x, z)[0] < -70.f) { sum += x; ++n; }
    expect(n > 10, "fjord cross-section contains a useful deep channel"); return n ? sum / n : 0.f;
  };
  const float mouth = center(10000.f, -15500.f, -8000.f), inland = center(-13000.f, -22000.f, -14000.f);
  expect(std::abs(inland - mouth) > 4500.f, "main fjord has a substantial inland bend");
  for (const auto& p : {vec2(-24000.f, -13000.f), vec2(-18500.f, -13000.f), vec2(6800.f, -9000.f), vec2(12800.f, -9000.f)})
    expect(frame.at(r, p.x, p.y)[0] < -65.f, "both legs of each inland fork are submerged");
  for (const auto& p : {vec2(-21200.f, -13000.f), vec2(10000.f, -9000.f)})
    expect(frame.at(r, p.x, p.y)[0] > 10.f, "fork legs are separated by a substantial headland");
  // Trace ocean-connected water on a conservative near-field raster, then
  // check the new fork legs. This is topology evidence, not a render test.
  constexpr int n = 381; constexpr float step = 200.f;
  std::vector<unsigned char> wet(n * n), seen(n * n); std::deque<int> queue;
  for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
    const int index = z * n + x;
    wet[index] = frame.at(r, -38000.f + x * step, -38000.f + z * step)[0] < -1.f;
    if (wet[index] && (!x || !z || x == n - 1 || z == n - 1)) { seen[index] = 1; queue.push_back(index); }
  }
  while (!queue.empty()) {
    const int index = queue.front(); queue.pop_front(); const int x = index % n, z = index / n;
    for (const auto& d : {std::pair<int,int>{-1,0}, {1,0}, {0,-1}, {0,1}}) {
      const int xx = x + d.first, zz = z + d.second;
      if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
      const int next = zz * n + xx;
      if (wet[next] && !seen[next]) { seen[next] = 1; queue.push_back(next); }
    }
  }
  for (const auto& p : {vec2(-24000.f, -13000.f), vec2(-18500.f, -13000.f), vec2(6800.f, -9000.f), vec2(12800.f, -9000.f)}) {
    const int x = int(std::lround((p.x + 38000.f) / step)), z = int(std::lround((p.y + 38000.f) / step));
    expect(seen[z * n + x], "tributary connects through its trunk to ocean");
  }
  std::cout << "FJORD central_channel_shift=" << inland - mouth << '\n';
}
}
int main() {
  expect(procedural::kGeneratorVersion == 1 && procedural::kRecipeRevision == 3, "stable address version and independent terrain-content revision");
  atolls(); fjords();
  for (const auto& d : procedural::recommendations()) if (!d.authored) {
    const procedural::Recipe a(d), b(d); const uint64_t expected = fingerprint(a);
    expect(a.memoryBytes() < 24576, "profile tables and branches fit the 24 KiB recipe budget (a large region's islands and towns)");
    expect(expected == fingerprint(b), "profile generation is deterministic");
    auto concurrent = std::async(std::launch::async, [&a] { return fingerprint(a); });
    expect(fingerprint(a) == expected && concurrent.get() == expected, "immutable profiles sample consistently across threads");
    for (float edge : {-40000.f, -38000.f, 38000.f, 40000.f}) for (int p = -40000; p <= 40000; p += 250) {
      const auto x = at(a, edge, float(p)), z = at(a, float(p), edge);
      expect(x[0] == -SEA_DEPTH && z[0] == -SEA_DEPTH && !x[1] && !z[1], "square ocean edge and corners remain exact");
    }
  }
  std::cout << "procedural_landforms_test: " << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
