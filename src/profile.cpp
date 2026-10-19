// Somnium Volandi - the pilot's profile: achievements, stamps and personal bests (profile.h)
#include "profile.h"
#include "career.h"
#include "scenery.h"
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
  {"sites5", "Sightseer", "Find 5 landmarks", false},
  {"sites_all", "Grand Tour", "Find every landmark", false},
  {"photo", "Shutterbug", "Complete a photo flight", false},
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

static std::vector<Site> s_sites;
static bool s_sitesBuilt = false;

void resetProfileSites() { s_sites.clear(); s_sitesBuilt = false; }

const std::vector<Site>& profileSites() {
  if (s_sitesBuilt || g_world.airports.empty() || g_world.procedural) return s_sites;   // (the islands' own: built where they are)
  s_sitesBuilt = true;
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
  return s_sites;
}
