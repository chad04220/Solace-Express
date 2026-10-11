// Exact, menu-only memoization of the expensive autopilot-based launch estimate.
// Aircraft definitions and the generated world are immutable during a game. Mutable
// terrain/scenery damage is part of the key; streaming residency is deliberately not.
#pragma once
#include "career.h"
#include "entities.h"
#include <type_traits>

namespace menuPlan {
struct Key {
  std::string bytes;
  template<class T> void add(T v) {
    static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "serialize scalar fields, never struct padding");
    bytes.append(reinterpret_cast<const char*>(&v), sizeof v);
  }
  void add(const std::string& s) { add(s.size()); bytes += s; }
  void add(vec2 v) { add(v.x); add(v.y); }
  void add(vec3 v) { add(v.x); add(v.y); add(v.z); }
  void weather(const Weather& w) {
    add(w.windFrom); add(w.windSpeed); add(w.gust); add(w.turbulence);
    add(w.cloudCover); add(w.cloudBase); add(w.visibility); add(w.precip); add(w.storm); add(w.timeOfDay);
    add(w.cloudDrift); add(w.cloudDetail); add(w.cloudBoil);
  }
  void contract(const Contract& c) {
    add(c.id); add(c.type); add(c.from); add(c.to); add(c.cargoKg); add(c.pax); add(c.payout);
    add(c.timeLimitMin); add(c.minLicense); add(c.ownedOnly); add(c.fragile); add(c.forceAircraft);
    add(c.requireSpec); add(c.courtesy); add(c.startAirborne); add(c.wpStart); add(c.wxShift);
    weather(c.wx); weather(c.wxEnd);
    add(c.wps.size()); for (const auto& p : c.wps) { add(p.x); add(p.z); add(p.alt); }
  }
  void world() {
    // Airport data can also be changed by tests; include the actual values rather
    // than pointers or a lossy hash. No key is persisted between game launches.
    add(g_world.airports.size());
    for (const Airport& a : g_world.airports) {
      add(std::string(a.code)); add(a.x); add(a.z); add(a.elev); add(a.heading);
      add(a.length); add(a.width); add(a.surface); add(a.size);
    }
    add(g_groundPits.size()); for (const auto& p : g_groundPits) { add(p.x); add(p.z); add(p.R); add(p.D); }
    add(g_scenery.wreckRev); add(g_scenery.craters.size()); for (auto p : g_scenery.craters) add(p);
  }
};

inline std::string quoteInputs(const Contract& c, int spec) {
  Key k; k.bytes.reserve(2048); k.add(spec); k.contract(c); k.world();
  return std::move(k.bytes);
}

inline std::string inputs(const Career& career, const Contract& c, int spec, Career::Source src) {
  Key k; k.bytes.reserve(2048); k.add(spec); k.add(src); k.contract(c); k.world();
  k.add(career.location); k.add(career.money < 1500); k.add(career.boardSeed);
  k.add(career.fleet.size());
  for (const auto& p : career.fleet) { k.add(p.spec); k.add(p.location); k.add(p.fuel); k.add(p.condition); }
  for (float v : kEstK) k.add(v);
  return std::move(k.bytes);
}

struct Cache {
  std::string key;
  Career::LaunchPlan base;
  unsigned builds = 0;
  const Career::LaunchPlan& get(const Career& career, const Contract& c, int spec, Career::Source src) {
    std::string next = inputs(career, c, spec, src);
    if (!builds || key != next) {
      base = career.plan(c, spec, src); key = std::move(next); ++builds;
    }
    return base;
  }
};
} // namespace menuPlan
