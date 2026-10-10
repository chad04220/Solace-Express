#pragma once
#include "aircraft.h"
#include <algorithm>

// The career hangar includes classified teasers without expanding career's buyable roster.
// Keep this boundary separate from the research terminal's existing flight permissions.
inline int hangarCatalogCount() { return kNumAircraft + 4; }
inline int hangarSpecAt(int row) {
  const int research[] = {kNightjar, kMantis, kResearchJet, kWraith};
  row = std::clamp(row, 0, hangarCatalogCount() - 1);
  return row < kNumAircraft ? row : research[row - kNumAircraft];
}
inline int hangarRowFor(int spec) {
  for (int row = 0; row < hangarCatalogCount(); ++row) if (hangarSpecAt(row) == spec) return row;
  return 0;
}
// Future special-mission authorization should be integrated here. For now every research
// airframe is unconditionally locked in the career hangar, regardless of money or licence.
inline bool hangarResearchLocked(int spec) { return spec >= kNumAircraft; }
inline const char* hangarResearchDesignation(int spec) {
  return spec == kNightjar ? "XR-10" : spec == kMantis ? "XR-20" : spec == kResearchJet ? "XR-30" : "XR-40";
}
