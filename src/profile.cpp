// Somnium Volandi - the pilot's profile: achievements, stamps and personal bests (profile.h)
#include "profile.h"
#include "career.h"
#include "procedural_regions.h"
#include "scenery.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

const Achievement kAchievements[] = {
  // flying
  {"first_flight", "Wheels Up", "Complete your first flight", false},
  {"butter", "Butter", "Touch down under 150 fpm", false},
  {"greaser", "Greaser", "Touch down under 60 fpm", false},
  {"butter25", "Pastry Chef", "Make 25 butter landings", false},
  {"grade_s", "Textbook", "Earn an S for a landing", false},
  {"flawless", "Flawless", "Three stars and an S landing on the same job", false},
  {"crosswind", "Crab Walk", "Land with more than 15 kt of crosswind", false},
  {"night", "Night Owl", "Land at night", false},
  {"weather", "In the Soup", "Land in rain or snow with the cloud base under 300 m", false},
  {"deadstick", "Deadstick", "Land at an airport with every engine stopped", false},
  {"by_the_book", "By the Book", "Taxi clear, shut down and set the parking brake at the end of a job", false},
  {"go_around", "Better Safe", "Go around, then land", false},
  {"long_haul", "Long Haul", "Fly a single flight of 150 km or more", false},
  {"warp", "Warp Drive", "Cover 500 km in Time Warp", false},
  {"splashdown", "Splashdown", "Land on the water in a float plane", false},
  // career
  {"lic_ppl", "Private Pilot", "Earn the private pilot licence", false},
  {"lic_cpl", "Commercial Pilot", "Earn the commercial pilot licence", false},
  {"lic_atp", "Airline Transport Pilot", "Earn the airline transport pilot licence", false},
  {"owner", "Owner-Operator", "Buy your first aircraft", false},
  {"fleet3", "Fleet Manager", "Own three aircraft at once", false},
  {"airline", "Airline Founder", "Put an aircraft on a route with a hired pilot", false},
  {"tycoon", "Airline Tycoon", "Your airline earns $1,000,000", false},
  {"millionaire", "Deep Pockets", "Have $1,000,000 in the bank", false},
  {"campaign", "Captain", "Complete the campaign", false},
  {"legend", "Living Legend", "Complete every legendary contract", false},
  // exploring
  {"airports10", "Island Hopper", "Land at 10 different airports", false},
  {"airports_all", "Every Field", "Land at every airport in the islands", false},
  {"harbours_all", "Harbour Master", "Alight at every seaplane base in the islands", false},
  {"sites5", "Sightseer", "Find 5 landmarks", false},
  {"sites_all", "Grand Tour", "Find every landmark in the islands", false},
  {"photo", "Shutterbug", "Complete a photo flight", false},
  {"region_first", "Beyond the Horizon", "Land at an airport beyond the Solace Islands", false},
  {"regions5", "Wayfarer", "Land in 5 regions beyond the islands", false},
  {"region_all", "Charted", "Land at every airport in a region beyond the islands", false},
  {"discover20", "Cartographer", "Find 20 landmarks beyond the islands", false},
  // trials
  {"medal", "On the Podium", "Win a medal in a trial", false},
  {"gold", "Gold Standard", "Win a gold medal in a trial", false},
  {"gold_all", "Golden Wings", "Win gold in every trial", false},
  // the rest
  {"sortie", "Commissioned", "Complete a military sortie", false},
  {"ufo", "Close Encounter", "Meet the visitors", true},
  {"display", "Front Row", "Fly with the Nocturne display pair", true},
  {"classified", "Classified", "Open the research terminal", true},
  {"supersonic", "Through the Barrier", "Fly faster than sound", true},
};
const int kAchievementCount = (int)(sizeof(kAchievements) / sizeof(kAchievements[0]));

const Achievement* findAchievement(const std::string& id) {
  for (int i = 0; i < kAchievementCount; i++) if (id == kAchievements[i].id) return &kAchievements[i];
  return nullptr;
}

bool Profile::earn(const std::string& id) {
  if (!findAchievement(id) || has(id)) return false;
  ach[id] = (long long)time(nullptr);
  return true;
}

bool Profile::load(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return false;
  *this = Profile();
  char line[512];
  while (fgets(line, sizeof line, f)) {
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    const size_t sp = s.find(' ');
    if (sp == std::string::npos) continue;
    const std::string k = s.substr(0, sp), v = s.substr(sp + 1);
    if (k == "name") name = v.substr(0, 24);
    else if (k == "welcomed") welcomed = v == "1";
    else if (k == "ach") { const size_t t = v.find(' '); const std::string id = v.substr(0, t); if (findAchievement(id)) ach[id] = t == std::string::npos ? 0 : atoll(v.c_str() + t + 1); }
    else if (k == "airport") airports.insert(v);
    else if (k == "site") sites.insert(v);
    else if (k == "seen") seen.insert(v);
    else if (k.rfind("best.", 0) == 0) best[k.substr(5)] = (float)atof(v.c_str());
    else if (k.rfind("num.", 0) == 0) num[k.substr(4)] = (float)atof(v.c_str());
  }
  fclose(f);
  return true;
}

bool Profile::save(const std::string& path) const {
  std::string t = "name " + name + "\nwelcomed " + (welcomed ? "1" : "0") + "\n";
  for (auto& a : ach) t += fmt("ach %s %lld\n", a.first.c_str(), a.second);
  for (auto& a : airports) t += "airport " + a + "\n";
  for (auto& a : sites) t += "site " + a + "\n";
  for (auto& a : seen) t += "seen " + a + "\n";
  for (auto& b : best) t += fmt("best.%s %.1f\n", b.first.c_str(), b.second);
  for (auto& n : num) t += fmt("num.%s %.3f\n", n.first.c_str(), n.second);
  const std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  const bool ok = fwrite(t.data(), 1, t.size(), f) == t.size();
  if (fclose(f) != 0 || !ok) { remove(tmp.c_str()); return false; }
  return replaceFile(tmp, path);
}

static bool beyond(const std::string& key, char mark) { return key.find(mark) != std::string::npos; }
static bool inRegion(const std::string& key, const std::string& region, char mark) {
  return key.size() > region.size() && key[region.size()] == mark && key.compare(0, region.size(), region) == 0;
}
int Profile::islandAirports() const { int n = 0; for (auto& a : airports) n += !beyond(a, ':'); return n; }
// the islands' stamps of one kind - its land airports, or its seaplane bases (docs/WINGS_OVER_WATER.md) - and how many
// there are to collect
int Profile::islandStamps(bool water) const {
  int n = 0;
  for (int i = 0; i < solaceAirportCount(); i++) { const Airport& a = solaceAirport(i); n += a.water() == water && airports.count(a.code); }
  return n;
}
int islandFields(bool water) { int n = 0; for (int i = 0; i < solaceAirportCount(); i++) n += solaceAirport(i).water() == water; return n; }
int Profile::islandSites() const { int n = 0; for (auto& s : sites) n += !beyond(s, '/'); return n; }
int Profile::sitesBeyond() const { return (int)sites.size() - islandSites(); }
std::vector<std::string> Profile::regionsLanded() const {
  std::vector<std::string> out;
  for (auto& a : airports) {
    const size_t c = a.find(':');
    if (c != std::string::npos && (out.empty() || out.back() != a.substr(0, c))) out.push_back(a.substr(0, c));   // (the set keeps a region's together)
  }
  return out;
}
std::vector<std::string> Profile::regionsExplored() const {
  std::set<std::string> ids;
  for (auto& a : airports) { const size_t c = a.find(':'); if (c != std::string::npos) ids.insert(a.substr(0, c)); }
  for (auto& st : sites) { const size_t c = st.find('/'); if (c != std::string::npos) ids.insert(st.substr(0, c)); }
  for (auto& sg : seen) if (sg.rfind("region:", 0) == 0) ids.insert(sg.substr(7));
  return std::vector<std::string>(ids.begin(), ids.end());
}
int Profile::regionAirports(const std::string& region) const { int n = 0; for (auto& a : airports) n += inRegion(a, region, ':'); return n; }
int Profile::regionSites(const std::string& region) const { int n = 0; for (auto& s : sites) n += inRegion(s, region, '/'); return n; }

std::string airportStamp(const Airport& a) { return g_world.procedural ? g_world.regionId + ":" + a.code : std::string(a.code); }

int regionAirportCount(const std::string& region) { return procedural::airportCount(region); }

static std::vector<Site> s_sites;
static std::string s_sitesWorld;   // (the world the list is of, by its region id: empty when there is none)
static int s_solaceSites = -1;

void resetProfileSites() { s_sites.clear(); s_sitesWorld.clear(); }
int solaceSiteCount() { return s_solaceSites; }

// A region's: its own sights, then the landmarks it has, each kind's pieces gathered into the places they stand (a
// region may have two wind farms), named for the region
static void regionSites() {
  const std::string& rid = g_world.regionId;
  const std::string place = g_world.regionRecipe->prefix();
  for (const auto& f : g_world.regionRecipe->features())
    s_sites.push_back({rid + "/" + f.key, f.name, vec3(f.x, f.y, f.z), f.radius});
  struct K { int kind; const char* id; const char* name; float radius; };
  const K kinds[] = {
    {EK_WINDTURBINE, "windfarm", "wind farm", 1200.f},
    {EK_BLOCK, "stonecircle", "stone circle", 600.f},
    {EK_SHIPWRECK, "wreck", "wreck", 600.f},
    {EK_PLATFORM, "platform", "offshore platform", 700.f},
    {EK_STARFORT, "starfort", "star fort", 700.f},
    {EK_GRANDSTAND, "racetrack", "racetrack", 1000.f},
  };
  for (const K& k : kinds) {
    struct Group { vec3 first, sum; int n; };
    std::vector<Group> groups;
    for (const PlacedEnt& p : landmarks()) {
      if (p.kind != k.kind) continue;
      const vec3 at(p.e.x, p.e.y, p.e.z);
      Group* g = nullptr;
      for (Group& o : groups) if (length(vec3(at.x - o.first.x, 0, at.z - o.first.z)) < 5000.f) { g = &o; break; }
      if (!g) { groups.push_back({at, vec3(), 0}); g = &groups.back(); }
      g->sum += at; g->n++;
    }
    for (size_t i = 0; i < groups.size(); ++i) {
      const vec3 c = groups[i].sum / (float)groups[i].n;
      std::string name = "The " + place + " " + k.name;
      if (groups.size() > 1) {   // (two of a kind: told apart by where they stand)
        static const char* compass[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
        name += std::string(", ") + compass[((int)lroundf(atan2f(c.x, -c.z) / (PI / 4.f)) + 8) % 8];
      }
      s_sites.push_back({rid + "/" + k.id + (i ? std::to_string(i + 1) : std::string()), name, c, k.radius});
    }
  }
  for (const Bridge& b : g_world.bridges)
    if (b.main == BM_STAYED) {
      s_sites.push_back({rid + "/straitbridge", "The " + place + " bridge", vec3((b.minX + b.maxX) * 0.5f, b.maxY, (b.minZ + b.maxZ) * 0.5f), 900.f});
      break;
    }
}

const std::vector<Site>& profileSites() {
  static const std::vector<Site> none;
  if (g_world.airports.empty()) return none;
  if (s_sitesWorld == g_world.regionId) return s_sites;   // (each world's own: built where it is)
  s_sites.clear(); s_sitesWorld = g_world.regionId;
  if (g_world.procedural) {
    if (g_world.regionRecipe) regionSites();
    return s_sites;
  }
  // each landmark's kind, gathered to the middle of its pieces (the wind farm's turbines, the racetrack's stands)
  struct K { int kind; const char* id; const char* name; float radius; bool civic; };
  const K kinds[] = {
    {EK_WINDTURBINE, "windfarm", "The wind farm", 1200.f, false},
    {EK_BLOCK, "stonecircle", "The stone circle", 600.f, false},
    {EK_SHIPWRECK, "wreck", "The wreck on the reef", 600.f, false},
    {EK_PLATFORM, "platform", "The offshore platform", 700.f, false},
    {EK_OBSERVATORY, "observatory", "The observatory", 600.f, false},
    {EK_STARFORT, "starfort", "The star fort", 700.f, false},
    {EK_GRANDSTAND, "racetrack", "The racetrack", 1000.f, false},
    {EK_DAM, "dam", "The Fjordhaven dam", 900.f, false},
    {EK_STADIUM, "stadium", "Solace Capital stadium", 700.f, true},
    {EK_GANTRY, "port", "Port Verde container port", 900.f, true},
  };
  for (const K& k : kinds) {
    vec3 sum; int n = 0;
    for (const PlacedEnt& p : k.civic ? civicLandmarks() : landmarks())
      if (p.kind == k.kind) { sum += vec3(p.e.x, p.e.y, p.e.z); n++; }
    if (n) s_sites.push_back({k.id, k.name, sum / (float)n, k.radius});
  }
  float lx, lz, lyaw;
  if (findLighthouseSite(g_world, lx, lz, lyaw)) s_sites.push_back({"lighthouse", "Lighthouse Key", vec3(lx, std::max(g_world.height(lx, lz), 0.f), lz), 600.f});
  for (const Bridge& b : g_world.bridges)
    if (b.main == BM_STAYED) {
      s_sites.push_back({"straitbridge", "The strait bridge", vec3((b.minX + b.maxX) * 0.5f, b.maxY, (b.minZ + b.maxZ) * 0.5f), 900.f});
      break;
    }
  s_solaceSites = (int)s_sites.size();
  return s_sites;
}
