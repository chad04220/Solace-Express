// Steam (steam.h): every achievement has its Steamworks API name, a row in steam/achievements.csv exactly as the game
// has it (the upload table) and both icons; every status the game sends is a token of steam/rich_presence_english.vdf,
// and every value a token fills in is one the game sets; without Steam's DLL every call does nothing. On Windows, given
// the folder of the stand-in DLL (steam_fake_dll.cpp): the real loader against it - started, an achievement waiting
// for the stats then stored, a batch stored once, an unknown name given up on, the status sent on a change only, the
// overlay's callback, the Deck's keyboard, shut down
#include "../src/profile.h"
#include "../src/steam.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
static bool exists(const std::string& path) { std::ifstream f(path, std::ios::binary); return f.good(); }

// one CSV line's fields (quotes and doubled quotes, as Python's csv module writes them)
static std::vector<std::string> csvFields(const std::string& line) {
  std::vector<std::string> out; std::string cur; bool q = false;
  for (size_t i = 0; i < line.size(); i++) {
    const char c = line[i];
    if (q) { if (c == '"') { if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; } else q = false; } else cur += c; }
    else if (c == '"') q = true;
    else if (c == ',') { out.push_back(cur); cur.clear(); }
    else if (c != '\r') cur += c;
  }
  out.push_back(cur);
  return out;
}

// every quoted string in the text that starts with the prefix, without the quotes
static std::set<std::string> quoted(const std::string& text, const std::string& prefix) {
  std::set<std::string> out;
  for (size_t k = text.find("\"" + prefix); k != std::string::npos; k = text.find("\"" + prefix, k + 1)) {
    const size_t e = text.find('"', k + 1);
    if (e != std::string::npos) out.insert(text.substr(k + 1, e - k - 1));
  }
  return out;
}

int main(int argc, char** argv) {
  int fails = 0;
  auto check = [&](bool ok, const std::string& what) { printf("%s: %s\n", what.c_str(), ok ? "ok" : "FAIL"); fails += !ok; };
  const std::string src = SOMNIUM_SOURCE_DIR;

  check(steamAchievementName("butter") == "ACH_BUTTER" && steamAchievementName("lic_atp") == "ACH_LIC_ATP", "API names: ACH_ and the id in capitals");
  {   // the upload table and the icons
    std::ifstream f(src + "/steam/achievements.csv");
    std::string line; std::getline(f, line);
    const std::vector<std::string> head = csvFields(line);
    std::vector<std::vector<std::string>> rows;
    while (std::getline(f, line)) if (!line.empty()) rows.push_back(csvFields(line));
    bool same = head.size() == 7 && head[0] == "api_name" && (int)rows.size() == kAchievementCount;
    std::set<std::string> names;
    int icons = 0;
    for (int i = 0; same && i < kAchievementCount; i++) {
      const Achievement& a = kAchievements[i];
      const std::vector<std::string>& r = rows[i];
      if (r.size() != 7 || r[0] != steamAchievementName(a.id) || r[1] != a.title || r[2] != a.desc || r[3] != (a.secret ? "1" : "0")) {
        printf("  row %d differs from the game's %s\n", i + 1, a.id); same = false; break;
      }
      names.insert(r[0]);
      icons += exists(src + "/steam/" + r[5]) && exists(src + "/steam/" + r[6]);
    }
    check(same, std::to_string(rows.size()) + " rows in steam/achievements.csv, each as the game has it (tools/steam/make_steam_assets.py writes it)");
    check(same && (int)names.size() == kAchievementCount, "Every API name once");
    check(icons == kAchievementCount, std::to_string(icons) + " of " + std::to_string(kAchievementCount) + " achievements with both icons, earned and locked");
  }
  {   // the status: the game's tokens and the localization file's
    const std::string vdf = slurp(src + "/steam/rich_presence_english.vdf"), game = slurp(src + "/src/game_profile.cpp");
    const std::set<std::string> have = quoted(vdf, "#Status_"), sent = quoted(game, "#Status_");
    bool all = !sent.empty();
    for (const std::string& t : sent) if (!have.count(t)) { printf("  %s is sent but isn't in the file\n", t.c_str()); all = false; }
    bool used = true;
    for (const std::string& t : have) if (!sent.count(t)) { printf("  %s is in the file but never sent\n", t.c_str()); used = false; }
    check(all, std::to_string(sent.size()) + " statuses sent, each a token of steam/rich_presence_english.vdf");
    check(used, "Every token of the file in use");
    std::set<std::string> keys;   // the values the game fills in: values.push_back({"key", ...})
    for (size_t k = game.find("push_back({\""); k != std::string::npos; k = game.find("push_back({\"", k + 1)) {
      const size_t b = k + 12, e = game.find('"', b);
      keys.insert(game.substr(b, e - b));
    }
    bool filled = true;
    for (size_t k = vdf.find('%'); k != std::string::npos; k = vdf.find('%', k + 1)) {
      const size_t e = vdf.find('%', k + 1);
      if (e == std::string::npos) break;
      const std::string key = vdf.substr(k + 1, e - k - 1);
      if (!keys.count(key)) { printf("  %%%s%% is never set\n", key.c_str()); filled = false; }
      k = e;
    }
    check(filled && !keys.empty(), "Every %value% a token fills in is one the game sets");
  }
  {   // without the DLL: nothing
    const bool on = steam::init(src + "/no/such/folder");
    steam::unlock("butter"); steam::tick(1.f); steam::presence("#Status_Menu", {});
    check(!on && !steam::active() && steam::pending() == 0 && !steam::onDeck() && !steam::showKeyboard(0, 0, 10, 10) && !steam::overlayOpened(),
          "No steam_api64.dll: off, and every call does nothing (" + steam::status() + ")");
  }
#ifdef _WIN32
  if (argc > 1) {   // the loader against the stand-in
    std::string dir = argv[1];
    for (char& c : dir) if (c == '/') c = '\\';
    HMODULE keep = LoadLibraryA((dir + "\\steam_api64.dll").c_str());   // (a hold of the test's own: the log outlives steam::shutdown)
    auto fn = [&](const char* n) { return reinterpret_cast<void (*)()>(GetProcAddress(keep, n)); };
    auto logOf = reinterpret_cast<const char* (*)()>(fn("FakeSteam_Log"));
    auto clear = reinterpret_cast<void (*)()>(fn("FakeSteam_Clear"));
    auto overlay = reinterpret_cast<void (*)()>(fn("FakeSteam_Overlay"));
    if (!keep || !logOf || !clear || !overlay) { printf("The stand-in DLL isn't in %s: FAIL\n", dir.c_str()); return 1; }
    auto log = [&] { std::string s = logOf(); clear(); return s; };
    const bool on = steam::init(dir);
    check(on && steam::active() && steam::onDeck() && log() == "init;manual;", "Started from the folder's DLL, by hand-dispatched callbacks, on a Deck (" + steam::status() + ")");
    steam::unlock("butter"); steam::unlock("butter");
    const int waiting = steam::pending();
    steam::tick(0.1f); steam::tick(0.5f); steam::tick(2.f);
    const int stillWaiting = steam::pending();
    steam::tick(2.f);
    check(waiting == 1 && stillWaiting == 1 && steam::pending() == 0 && log() == "ach wait;ach wait;ach ACH_BUTTER;store;",
          "An achievement waits for the stats (tried every 2 s), then is set and stored; earned twice, sent once");
    steam::sync({"greaser", "night"}); steam::tick(0.f);
    check(steam::pending() == 0 && log() == "ach ACH_GREASER;ach ACH_NIGHT;store;", "Those already earned, sent at the start: set together, stored once");
    steam::unlock("no_such_thing");
    for (int i = 0; i < 40; i++) steam::tick(2.f);
    check(steam::pending() == 0 && log().empty(), "A name the Steamworks page doesn't have: given up on after a minute, nothing stored");
    steam::presence("#Status_Menu", {}); steam::presence("#Status_Menu", {});
    steam::presence("#Status_Job", {{"aircraft", "Skerry Twin"}, {"from", "Glacier Station"}, {"to", "Solace Capital"}});
    check(log() == "rpclear;rp steam_display=#Status_Menu;rpclear;rp aircraft=Skerry Twin;rp from=Glacier Station;rp to=Solace Capital;rp steam_display=#Status_Job;",
          "The status sent on a change only, its values before its token");
    overlay();
    const bool before = steam::overlayOpened();
    steam::tick(0.016f);
    const bool opened = steam::overlayOpened(), again = steam::overlayOpened();
    check(!before && opened && !again && log() == "free;", "The overlay's opening read from its callback, once");
    check(steam::showKeyboard(10, 20, 300, 40) && log() == "kb 0 10 20 300 40;", "The Deck's floating keyboard over the field, one line");
    steam::shutdown();
    steam::unlock("butter");
    check(!steam::active() && steam::pending() == 0 && log() == "rpclear;shutdown;", "Shut down: the status cleared, Steam's shutdown called, then nothing");
    FreeLibrary(keep);
  }
#else
  (void)argc; (void)argv;
#endif
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
