// Somnium Volandi - the pilot's own record, kept across careers: a name, the achievements, the airports landed at, the
// landmarks found and the things seen, each aircraft's softest touchdown, and a few running counts. Saved in
// profile.cfg beside the settings, as plain "key value" lines (a career's own numbers stay in its save)
#pragma once
#include "common.h"
#include <map>
#include <set>
#include <string>
#include <vector>

struct Achievement {
  const char* id;
  const char* title;
  const char* desc;
  bool secret;   // its description stays hidden until it is earned
};
extern const Achievement kAchievements[];
extern const int kAchievementCount;
const Achievement* findAchievement(const std::string& id);

struct Profile {
  std::string name;
  bool welcomed = false;                  // the first start's welcome has been seen
  std::map<std::string, long long> ach;   // earned: id -> when (seconds since 1970)
  std::set<std::string> airports;         // codes of the airports landed at
  std::set<std::string> sites;            // landmarks found (Site ids)
  std::set<std::string> seen;             // sightings: "ufo", "display"
  std::map<std::string, float> best;      // per aircraft id: the softest touchdown (fpm)
  std::map<std::string, float> num;       // running counts: butters, warpKm, ...
  bool has(const std::string& id) const { return ach.count(id) > 0; }
  bool earn(const std::string& id);       // true the first time (an unknown id: false)
  float count(const std::string& k) const { auto it = num.find(k); return it == num.end() ? 0.f : it->second; }
  float add(const std::string& k, float v = 1.f) { return num[k] += v; }
  bool load(const std::string& path);
  bool save(const std::string& path) const;
  // The islands' stamps are their airports' codes and their landmarks' ids; a region's begin with its id
  // ("pr1-0-0-17:UMA", "pr1-0-0-17/crater0"), so a code or a name may repeat from one region to the next
  int islandAirports() const;
  int islandSites() const;
  int sitesBeyond() const;                          // landmarks found in the regions beyond the islands
  std::vector<std::string> regionsLanded() const;   // the regions with an airport stamp, by id
  std::vector<std::string> regionsExplored() const; // ... those, and those with a landmark found or flown over
  int regionAirports(const std::string& region) const;
  int regionSites(const std::string& region) const;
};

struct Airport;
std::string airportStamp(const Airport& a);   // its key in Profile::airports, in g_world
// A region's airports, wherever g_world is (its recipe made the first time, and its count kept; 0 for an unknown id)
int regionAirportCount(const std::string& region);

// The landmarks a pilot can find in g_world: each one's name and a point at its middle. In the islands, from the
// world as it was built (the wind farm, the stone circle, the wreck, the platform, the observatory, the star fort,
// Lighthouse Key, the racetrack, the dam, the stadium, the container port, the strait bridge). In a region, the
// same kinds where it has them, and its own sights (procedural::Recipe::features: craters, summits, lagoons, fjords,
// the strait). Built on first use for each world
struct Site { std::string id, name; vec3 pos; float radius; };
const std::vector<Site>& profileSites();
void resetProfileSites();   // (the list is built again on its next use)
int solaceSiteCount();      // the islands' landmarks (-1 until their list has been built)
