#include "../src/hangar_catalog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void check(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
  // These are persisted identities, not list positions. Additions must never shift them.
  static_assert(kWavecrest == 8 && kVigil == 9 && kResearchJet == 10 && kReverie == 11 && kSomnus == 12,
                "Existing aircraft indices must remain save/shader compatible");
  static_assert(kThistledown == 13 && kGalleon == 14 && kKittiwake == 15 && kAircraftCount == 16 && kNumAircraft == 12,
                "The conventional aircraft and the amphibian append to the existing roster");
  const char* ids[] = {"dawnlark", "ambler", "scrubrunner", "skerry", "longshore", "archipel", "emissary", "cutwater_s6", "wavecrest_c6",
                      "xr10_vigil", "xr30_nocturne", "xr20_reverie", "xr40_somnus", "thistledown_l4", "galleon_180",
                      "kittiwake_a9"};
  for (int spec = 0; spec < kAircraftCount; ++spec) check(std::strcmp(kAircraft[spec].id, ids[spec]) == 0, "Stable aircraft identity changed");
  check(hangarCatalogCount() == kAircraftCount, "Catalog must include every aircraft once");
  bool seen[kAircraftCount] = {};
  for (int row = 0; row < hangarCatalogCount(); ++row) {
    const int spec = hangarSpecAt(row);
    check(validAircraft(spec) && !seen[spec], "Invalid or repeated catalog aircraft");
    seen[spec] = true;
    check(hangarRowFor(spec) == row, "Catalog mapping must round-trip");
    check(hangarResearchLocked(spec) == (row >= kNumAircraft), "Only research aircraft must remain locked");
    if (row < kNumAircraft) {
      check(spec == careerSpecAt(row) && careerRowFor(spec) == row && isCareerAircraft(spec), "Career mapping must round-trip");
      check(validCareerSelection(spec) == spec, "Every ordinary airframe remains selectable, including appended ids");
    }
    if (row < 9) check(spec == row, "Existing career order must remain unchanged");
  }
  check(hangarSpecAt(9) == kThistledown && hangarSpecAt(10) == kGalleon && hangarSpecAt(11) == kKittiwake,
        "New conventional aircraft and the amphibian precede research teasers");
  const int ordered[] = {kVigil, kReverie, kResearchJet, kSomnus};
  const char* names[] = {"XR-10", "XR-20", "XR-30", "XR-40"};
  for (int i = 0; i < 4; ++i) {
    check(hangarSpecAt(kNumAircraft + i) == ordered[i], "Research designation order is incorrect");
    check(std::strcmp(hangarResearchDesignation(ordered[i]), names[i]) == 0, "Wrong research designation");
    check(!isCareerAircraft(ordered[i]) && isResearchAircraft(ordered[i]) && validCareerSelection(ordered[i]) == 0,
          "Research must stay outside free flight and career regardless of special flag");
  }
  for (int invalid : {-1, kAircraftCount, 1000000})
    check(!validAircraft(invalid) && !isCareerAircraft(invalid) && !isResearchAircraft(invalid) && validCareerSelection(invalid) == 0,
          "Invalid aircraft selections fail closed");
  check(hangarSpecAt(-1) == 0 && hangarSpecAt(999) == kSomnus, "Catalog bounds must be safe");

  // Performance cache rows use the career mapping rather than the stable-index prefix, which contains research.
  const char* cache = "hangar_catalog_perf_test.bin";
  auto writeCache = [&](int count) {
    FILE* f = std::fopen(cache, "wb"); check(f != nullptr, "Create cache fixture");
    char stamp[64] = {}; std::strcpy(stamp, "roster-cache-test");
    uint32_t n = count, size = sizeof(PerfModel);
    std::fwrite(stamp, 1, 64, f); std::fwrite(&n, 4, 1, f); std::fwrite(&size, 4, 1, f);
    for (int row = 0; row < count; ++row) { PerfModel p; p.vs1 = float(1000 + careerSpecAt(row)); std::fwrite(&p, sizeof p, 1, f); }
    std::fclose(f);
  };
  writeCache(9); check(!Plane::perfLoad(cache, "roster-cache-test"), "Old nine-row cache must be relearned");
  writeCache(kNumAircraft); check(Plane::perfLoad(cache, "roster-cache-test"), "Current career cache loads");
  for (int spec : kCareerAircraft) check(Plane::perf(&kAircraft[spec]).vs1 == 1000.f + spec, "Performance cache restores the correct aircraft identity");
  Plane::perfSave(cache, "roster-cache-test");
  FILE* f = std::fopen(cache, "rb"); check(f != nullptr, "Read written performance cache");
  std::fseek(f, 72, SEEK_SET);
  for (int spec : kCareerAircraft) { PerfModel p; check(std::fread(&p, sizeof p, 1, f) == 1 && p.vs1 == 1000.f + spec, "Written cache preserves explicit career order"); }
  std::fclose(f); std::remove(cache);
  std::puts("All 16 identities unique; 12 career/free-flight entries; four locked research entries; stable cache mapping: PASS");
}
