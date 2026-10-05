// Optional prototype roster only. These indices are not production/save identifiers.
#pragma once
#include "aircraft.h"
#include "models.h"

namespace candidate {
#ifdef CANDIDATE_CAREER_INSERTION
constexpr int Swift = 7, Nightjar = 8;
#else
constexpr int Swift = 9, Nightjar = 10;
#endif
struct Entry { int index; const char* slug; const char* category; };
inline constexpr Entry entries[] = {
  {Swift, "swift-s6", "traditional"},
  {Nightjar, "xr14-nightjar", "research"}
};
inline constexpr Entry roster[] = {
  {Swift, "swift-s6", "traditional"}, {Nightjar, "xr14-nightjar", "research"},
  {0, "kestrel", "career"}, {1, "wren", "career"}, {2, "bushmaster", "career"},
  {3, "islander", "career"}, {4, "pelican", "career"}, {5, "meridian", "career"},
  {6, "starling", "career"}, {kResearchJet, "xr9-specter", "research"}, {kWraith, "xr11-wraith", "research"}
};
}
