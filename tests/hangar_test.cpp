// The hangar's mechanic (docs/HANGAR.md): a type as one airframe of it is fitted - floats in place of its wheels, the
// kits - flown through the flight model and bought, swapped and kept by the career.
#include "../src/aircraft.h"
#include "../src/career.h"
#include "../src/models.h"
#include "test_world.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) { std::printf("Hangar: %s: %s\n", what.c_str(), ok ? "ok" : "FAIL"); failures += !ok; }
Weather calm() { Weather w; w.windSpeed = w.gust = w.turbulence = 0; return w; }
vec3 openSea() {   // (well clear of every coast)
  for (int i = 0; i < 400; i++) {
    const float a = i * 2.39996f, r = WORLD_HALF * (0.35f + 0.4f * (i % 7) / 6.f);
    const vec3 q(cosf(a) * r, 0, sinf(a) * r); bool ok = true;
    for (int k = -3; k <= 3 && ok; k++) for (int j = -3; j <= 3 && ok; j++) ok = g_world.height(q.x + k * 1500.f, q.z + j * 1500.f) < -2.f;
    if (ok) return q;
  }
  return vec3(WORLD_HALF * 0.75f, 0, WORLD_HALF * 0.75f);
}
int saveVersion(const char* path) {   // (the save's first line, its key and version: the line ends as the platform writes it)
  FILE* f = fopen(path, "r"); char key[64] = {}; int v = -1;
  if (f) { if (fscanf(f, "%63s %d", key, &v) != 2 || std::string(key) != "somnium_save") v = -1; fclose(f); }
  return v;
}
float cruiseAt75(const AircraftSpec& s) {   // (level at 1,500 m on the altitude hold, 75% power: the speed it settles at)
  Plane q; q.reset(&s, vec3(30000, 1500, 30000), 90, s.designFuel() * 0.5f, s.cargoKg * 0.5f, true, s.cruise);
  q.ctl.gearDown = !s.retract; q.gear = q.ctl.gearDown ? 1.f : 0.f; q.engineRunning = true; q.engineSpool = 0.75f;
  q.apEngage(Plane::AP_HOLD, -1, calm()); q.apSpeed = 0; q.apAlt = q.pos.y; q.apHeading = 90;
  float sum = 0; int n = 0;
  for (int k = 0; k < 120 * 30; k++) { q.ctl.throttle = 0.75f; q.step(1 / 30.f, calm(), k / 30.f); if (k > 90 * 30) { sum += length(q.vel); n++; } }
  return n ? sum / n : 0.f;
}
}

int main() {
  buildTestWorld(); buildStory();
  // ---- what fits: floats for the high-winged pistons and turboprops light enough to carry them; the kits for every
  // career type, a propeller's and the STOL kit for the propeller types; nothing for the research craft
  {
    int floatTypes = 0; bool right = true;
    for (int t = 0; t < kAircraftCount; t++) {
      const bool want = t == 0 || t == 1 || t == 2 || t == 3 || t == 4 || t == kThistledown;
      right = right && floatKitFor(t) == want; floatTypes += floatKitFor(t);
    }
    check(right && floatTypes == 6, "floats for the Dawnlark, Ambler, Scrubrunner, Skerry, Longshore and Thistledown");
    check(!kitFor(6, KIT_PROP) && !kitFor(kGalleon, KIT_STOL) && kitFor(6, KIT_ENGINE) && kitFor(1, KIT_STOL) && !kitFor(kSomnus, KIT_ENGINE), "the kits by type");
    bool stock = true;
    for (int t = 0; t < kAircraftCount; t++) stock = stock && &fitted(t, Fit{}) == &kAircraft[t] && typeOf(&kAircraft[t]) == t;
    check(stock, "a stock airframe is its type's own row");
    const AircraftSpec& a = fitted(1, Fit{true, 1u << KIT_TANKS});
    check(&a == &fitted(1, Fit{true, 1u << KIT_TANKS}) && typeOf(&a) == 1 && a.fittedFrom == 1 && a.fit.floats && a.fit.has(KIT_TANKS), "a fitted airframe is made once, and knows its type and its fit");
    check(&fitted(kSomnus, Fit{true, 63}) == &kAircraft[kSomnus], "nothing fits a research craft");
  }
  // ---- the kits' effects, and the burn: long-range tanks hold more and burn no faster; a tuned engine burns more
  {
    const AircraftSpec& s = kAircraft[1];
    const AircraftSpec& eng = fitted(1, Fit{false, 1u << KIT_ENGINE}), &aero = fitted(1, Fit{false, 1u << KIT_AERO}), &light = fitted(1, Fit{false, 1u << KIT_LIGHT});
    const AircraftSpec& tanks = fitted(1, Fit{false, 1u << KIT_TANKS}), &stol = fitted(1, Fit{false, 1u << KIT_STOL});
    check(fabsf(eng.power / s.power - 1.08f) < 1e-4f && aero.dragFactor < 0.95f && light.emptyMass < s.emptyMass * 0.96f && fabsf(tanks.maxFuel / s.maxFuel - 1.3f) < 1e-4f && stol.CLmax > s.CLmax * 1.09f,
          "each kit does what the shop says");
    check(tanks.maxMass() == s.maxMass() && light.maxMass() == s.maxMass(), "the weight limit is the type's, whatever is fitted");
    Plane a; a.spec = &s; Plane b; b.spec = &tanks; Plane c; c.spec = &eng;
    check(fabsf(a.fuelFlowAt(0.7f, s.cruise, 1.225f) - b.fuelFlowAt(0.7f, s.cruise, 1.225f)) < 1e-7f && fabsf(cruiseMinutes(tanks, tanks.maxFuel) / cruiseMinutes(s, s.maxFuel) - 1.3f) < 0.01f,
          "long-range tanks: the same burn, 30% more hours");
    check(fabsf(c.fuelFlowAt(0.7f, s.cruise, 1.225f) / a.fuelFlowAt(0.7f, s.cruise, 1.225f) - 1.08f) < 1e-3f, "a tuned engine burns as much more as it makes");
    const float v0 = cruiseAt75(s), vAll = cruiseAt75(fitted(1, Fit{false, (1u << KIT_COUNT) - 1})), vFloats = cruiseAt75(fitted(1, Fit{true, 0}));
    std::printf("  Ambler at 75%%: stock %.1f m/s, every kit %.1f, on floats %.1f\n", v0, vAll, vFloats);
    check(vAll > v0 * 1.04f && vFloats < v0, "every kit flies it faster; floats slower");
  }
  // ---- each type on floats: its floats under the belly, its propeller clear of the water, afloat at its draft, and off
  // the water onto the step and into the air
  const vec3 sea = openSea();
  for (int t = 0; t < kAircraftCount; t++) {
    if (!floatKitFor(t)) continue;
    const AircraftSpec& s = fitted(t, Fit{true, 0});
    const FloatStations f = floatStations(s); const GearStations g = gearStations(s);
    float hw = 0, hh = 0, cy = 0; modelSection(kModels[t], g.mainZ, hw, hh, cy);
    float props[2][4]; const int np = modelProps(kModels[t], props);
    Plane p; p.reset(&s, sea, 90, s.maxFuel * 0.3f, s.cargoKg * 0.3f, false);
    for (int k = 0; k < 600; k++) p.step(1 / 60.f, calm(), k / 60.f);
    const float water = -p.pos.y;   // (the surface, in the body frame, level-ish at rest)
    const float propLow = np ? props[0][1] - props[0][3] : 1.f, draft = water - f.keelY, clear = propLow - water;
    p.engineRunning = true; p.ctl.brake = 0; p.ctl.flaps = s.takeoffFlap; vec3 p0 = p.pos; float run = -1;
    for (int k = 0; k < 60 * 60 && !p.ev.crashed; k++) {
      p.ctl.throttle = 1; p.ctl.pitch = p.ias > s.vr ? clampf((8.f - p.pitchDeg()) * 0.08f, -1, 1) : (p.ias > s.vr * 0.4f ? 0.15f : 0.4f);
      p.ctl.roll = clampf(-p.bankDeg() * 0.05f, -1, 1); p.ctl.yaw = clampf(wrapAngle((90.f - p.heading()) * DEG) * 3.f, -1, 1);
      p.step(1 / 60.f, calm(), k / 60.f);
      if (!p.onWater && p.pos.y + f.keelY > 1.f) { run = length(vec3(p.pos.x - p0.x, 0, p.pos.z - p0.z)); break; }
    }
    std::printf("  %s on floats: deck %.2f under a belly at %.2f, propeller tips %.2f m over the water, draft %.2f of %.2f, off the water in %.0f m%s\n",
                s.name, f.keelY + f.depth, cy - hh, clear, draft, f.depth, run, p.ev.crashed ? (" " + p.ev.crashReason).c_str() : "");
    check(f.keelY + f.depth < cy - hh - 0.3f, std::string(s.name) + ": the floats hang below the belly on struts");
    check(np == 0 || clear > 0.35f, std::string(s.name) + ": its propeller clears the water at rest");
    check(draft > 0.25f * f.depth && draft < 0.7f * f.depth, std::string(s.name) + ": it floats at its draft");
    check(!p.ev.crashed && run > 0 && run < 1200.f, std::string(s.name) + ": it takes off from the water");
    check(s.waterRunNeeded(0) > 0 && s.waterRunNeeded(0) < 1300.f && surfaceOK(s, SURF_WATER) && surfaceOK(s, SURF_ASPHALT), std::string(s.name) + ": water lanes and runways both");
  }
  // ---- the career: bought, fitted and swapped where it stands, flown to the water, saved and loaded
  {
    Career c; c.newGame(); c.license = LIC_ATP; c.money = 1000000;
    const int mdb = c.location;
    int water = -1; for (int i = 0; i < (int)g_world.airports.size() && water < 0; i++) if (g_world.airports[i].water()) water = i;
    std::string m;
    check(c.buy(1, &m) && c.ownedIndexFor(1) == 0, "an Ambler bought at Meadowbrook");
    Contract job; job.id = "FLOATJOB"; job.type = CT_CARGO; job.from = mdb; job.to = water; job.cargoKg = 60; job.payout = 2000; job.minLicense = LIC_PPL; job.wx = calm();
    std::string why; const auto before = c.canFly(job, 1, &why);
    check(before == Career::SRC_NONE && why.find("floats only") != std::string::npos, "on wheels it can't take a job to a seaplane base: " + why);
    const int m0 = c.money;
    const bool bought = c.buyFloats(0, &m);
    check(bought && c.fleet[0].floatKit && c.fleet[0].fit.floats && m0 - c.money == c.floatKitPrice(1), "floats bought and fitted at a land field: " + m);
    check(c.airframe(0).floats && c.specFor(1, Career::SRC_OWNED).floats && !c.specFor(1, Career::SRC_RENT).floats, "your own flies on its floats; a rental of the type on its wheels");
    check(c.canFly(job, 1, &why) == Career::SRC_OWNED, "on floats it takes the job to the water");
    const int m1 = c.money;
    check(c.buyKit(0, KIT_TANKS, &m) && c.fleet[0].fit.has(KIT_TANKS) && m1 - c.money == c.kitPrice(1, KIT_TANKS) && !c.buyKit(0, KIT_TANKS, &m), "long-range tanks fitted, once");
    check(c.airframe(0).maxFuel > kAircraft[1].maxFuel * 1.29f, "its tanks as fitted");
    check(c.saleValue(0) > (int)(kAircraft[1].price * 7 / 10 * (0.6f + 0.4f)), "what was fitted is worth half again at the sale");
    c.location = water;
    check(!c.swapFloats(0, false, &m) && c.fleet[0].fit.floats, "nothing is done to it while it's parked elsewhere");
    c.fleet[0].location = water;
    const bool swapped = c.swapFloats(0, false, &m);
    check(!swapped && m.find("land field") != std::string::npos, "on the water its floats stay on: " + m);
    c.location = c.fleet[0].location = mdb;
    const int m2 = c.money;
    check(c.swapFloats(0, false, &m) && !c.fleet[0].fit.floats && c.fleet[0].floatKit && m2 - c.money == c.floatSwapFee(1), "back on its wheels for the fee, the floats in store");
    check(c.swapFloats(0, true, &m) && c.fleet[0].fit.floats, "and on its floats again");
    // the save: version 9 with the fit; a career with nothing fitted writes what it always did
    const char* path = "hangar_test.sav";
    check(c.save(path), "the fitted career saves");
    check(saveVersion(path) == 9, "as version 9");
    Career d; d.newGame();
    check(d.load(path) && d.fleet.size() == 1 && d.fleet[0].fit == c.fleet[0].fit && d.fleet[0].floatKit && d.airframe(0).maxFuel == c.airframe(0).maxFuel, "loaded: its floats, its kits, its tanks");
    Career e; e.newGame(); e.license = LIC_ATP; e.money = 100000; e.buy(1, &m);
    check(e.save(path), "a career with nothing fitted saves");
    check(saveVersion(path) == 6, "as version 6, as before");
    remove(path); remove((std::string(path) + ".bak").c_str()); remove((std::string(path) + ".tmp").c_str());
  }
  std::printf("Hangar: %d failures\n", failures);
  return failures ? 1 : 0;
}
