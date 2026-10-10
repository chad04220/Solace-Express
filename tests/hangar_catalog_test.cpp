#include "../src/hangar_catalog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void check(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
  check(hangarCatalogCount() == (kWraith + 1), "Catalog must include every aircraft once");
  bool seen[(kWraith + 1)] = {};
  for (int row = 0; row < hangarCatalogCount(); ++row) {
    const int spec = hangarSpecAt(row);
    check(spec >= 0 && spec < (kWraith + 1) && !seen[spec], "Invalid or repeated catalog aircraft");
    seen[spec] = true;
    check(hangarRowFor(spec) == row, "Catalog mapping must round-trip");
    check(hangarResearchLocked(spec) == (row >= kNumAircraft), "Only research aircraft must remain locked");
    if (row < kNumAircraft) check(spec == row, "Career order must remain unchanged");
  }
  const int ordered[] = {kNightjar, kMantis, kResearchJet, kWraith};
  const char* names[] = {"XR-10", "XR-20", "XR-30", "XR-40"};
  for (int i = 0; i < 4; ++i) {
    check(hangarSpecAt(kNumAircraft + i) == ordered[i], "Research designation order is incorrect");
    check(std::strcmp(hangarResearchDesignation(ordered[i]), names[i]) == 0, "Wrong research designation");
  }
  check(hangarSpecAt(-1) == 0 && hangarSpecAt(999) == kWraith, "Catalog bounds must be safe");
  std::puts("All 13 catalog entries unique; career order retained; four research entries locked in designation order: PASS");
}
