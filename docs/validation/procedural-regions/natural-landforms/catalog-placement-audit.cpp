#include "procedural_regions.h"
#include "airport_layout.h"
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
int main(){int regions=0;for(int face=0;face<6;++face)for(int y=0;y<32;++y)for(int x=0;x<32;++x){char id[32];std::snprintf(id,sizeof id,"pr1-%d-%d-%d",face,x,y);auto d=procedural::find(id);if(!d)continue;int before=failures;checkAirports(procedural::Recipe(*d));++regions;if(failures!=before)std::cerr<<"REGION_FAILURE "<<id<<" additional="<<failures-before<<"\n";}std::cout<<"catalog_placement: regions="<<regions<<" checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;}
