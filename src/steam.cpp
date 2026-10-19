// Somnium Volandi - Steam (steam.h): Valve's flat C API, found at run time. Each interface's accessor is looked up from
// the newest version down (SteamAPI_SteamUserStats_v013, then _v012...), so the DLL of whichever Steamworks SDK ships
// works; the methods are the version-free flat exports (SteamAPI_ISteamUserStats_SetAchievement), unchanged for years
#include "steam.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

std::string steamAchievementName(const std::string& id) {
  std::string s = "ACH_";
  for (char c : id) s += (char)toupper((unsigned char)c);
  return s;
}

namespace steam {
namespace {
typedef void* Iface;
struct CallbackMsg { int user; int callback; unsigned char* param; int size; };   // (CallbackMsg_t)
const int kOverlayActivated = 331, kUserStatsReceived = 1101;   // (GameOverlayActivated_t, UserStatsReceived_t)

struct Api {
#ifdef _WIN32
  HMODULE dll = nullptr;
#endif
  bool on = false, deck = false, overlay = false;
  int pipe = 0;
  Iface stats = nullptr, friends = nullptr, utils = nullptr;
  void (*shutdown)() = nullptr;
  void (*runCallbacks)() = nullptr;
  bool (*setAchievement)(Iface, const char*) = nullptr;
  bool (*storeStats)(Iface) = nullptr;
  bool (*setPresence)(Iface, const char*, const char*) = nullptr;
  void (*clearPresence)(Iface) = nullptr;
  bool (*bigPicture)(Iface) = nullptr;
  bool (*floatingKeyboard)(Iface, int, int, int, int, int) = nullptr;
  void (*dispatchFrame)(int) = nullptr;
  bool (*dispatchNext)(int, CallbackMsg*) = nullptr;
  void (*dispatchFree)(int) = nullptr;
  std::map<std::string, int> waiting;   // API name -> tries
  float retryT = 0.f;
  std::string lastPresence;
};
Api A;
std::string g_status = "Steam: not started";

#ifdef _WIN32
template <class F> bool sym(F& f, const char* name) {
  f = reinterpret_cast<F>(reinterpret_cast<void (*)()>(GetProcAddress(A.dll, name)));
  return f != nullptr;
}
Iface iface(const char* stem, int newest, int oldest) {
  for (int v = newest; v >= oldest; v--) {
    char n[96]; snprintf(n, sizeof n, "SteamAPI_%s_v%03d", stem, v);
    Iface (*get)() = nullptr;
    if (sym(get, n)) if (Iface p = get()) return p;
  }
  return nullptr;
}
#endif
}

bool init(const std::string& exeDir) {
  if (A.on) return true;
#ifdef _WIN32
  const std::string path = exeDir + "\\steam_api64.dll";   // (beside the exe, by its full path: never the search path)
  if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) { g_status = "Steam: off (no steam_api64.dll beside the game)"; return false; }
  A.dll = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!A.dll) { g_status = "Steam: off (steam_api64.dll would not load)"; return false; }
  bool ok = false;
  int (*initFlat)(char*) = nullptr;   // (ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg*): 0 is OK)
  bool (*initOld)() = nullptr;
  std::string why = "Steam isn't running, or the game wasn't started from it";
  if (sym(initFlat, "SteamAPI_InitFlat")) {
    char err[1024] = {};
    ok = initFlat(err) == 0;
    if (!ok && err[0]) why = err;
  } else if (sym(initOld, "SteamAPI_InitSafe") || sym(initOld, "SteamAPI_Init")) ok = initOld();
  if (!ok) { FreeLibrary(A.dll); A = Api(); g_status = "Steam: off (" + why + ")"; return false; }
  sym(A.shutdown, "SteamAPI_Shutdown");
  A.stats = iface("SteamUserStats", 20, 10);
  A.friends = iface("SteamFriends", 25, 15);
  A.utils = iface("SteamUtils", 15, 8);
  sym(A.setAchievement, "SteamAPI_ISteamUserStats_SetAchievement");
  sym(A.storeStats, "SteamAPI_ISteamUserStats_StoreStats");
  sym(A.setPresence, "SteamAPI_ISteamFriends_SetRichPresence");
  sym(A.clearPresence, "SteamAPI_ISteamFriends_ClearRichPresence");
  sym(A.bigPicture, "SteamAPI_ISteamUtils_IsSteamInBigPictureMode");
  sym(A.floatingKeyboard, "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput");
  bool (*isDeck)(Iface) = nullptr;
  if (A.utils && sym(isDeck, "SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck")) A.deck = isDeck(A.utils);
  // (older SDKs fetch the player's stats on request; newer ones at the start by themselves)
  bool (*requestStats)(Iface) = nullptr;
  if (A.stats && sym(requestStats, "SteamAPI_ISteamUserStats_RequestCurrentStats")) requestStats(A.stats);
  // the callbacks: dispatched by hand (only the overlay's and the stats' are read), else Steam's own loop
  void (*dispatchInit)() = nullptr; int (*getPipe)() = nullptr;
  if (sym(dispatchInit, "SteamAPI_ManualDispatch_Init") && sym(A.dispatchFrame, "SteamAPI_ManualDispatch_RunFrame") &&
      sym(A.dispatchNext, "SteamAPI_ManualDispatch_GetNextCallback") && sym(A.dispatchFree, "SteamAPI_ManualDispatch_FreeLastCallback") &&
      sym(getPipe, "SteamAPI_GetHSteamPipe")) {
    dispatchInit(); A.pipe = getPipe();
  } else {
    A.dispatchFrame = nullptr; A.dispatchNext = nullptr; A.dispatchFree = nullptr;
    sym(A.runCallbacks, "SteamAPI_RunCallbacks");
  }
  A.on = true;
  g_status = std::string("Steam: on") + (A.deck ? ", on a Steam Deck" : "") + (A.stats ? "" : ", without achievements (no user stats interface)") +
             (A.friends ? "" : ", without rich presence (no friends interface)");
  return true;
#else
  (void)exeDir;
  g_status = "Steam: off (not on this platform)";
  return false;
#endif
}

void shutdown() {
  if (!A.on) return;
  if (A.clearPresence && A.friends) A.clearPresence(A.friends);
  if (A.shutdown) A.shutdown();
#ifdef _WIN32
  FreeLibrary(A.dll);
#endif
  A = Api();
  g_status = "Steam: shut down";
}

bool active() { return A.on; }
const std::string& status() { return g_status; }

void tick(float dt) {
  if (!A.on) return;
  if (A.dispatchFrame) {
    A.dispatchFrame(A.pipe);
    CallbackMsg m = {};
    while (A.dispatchNext(A.pipe, &m)) {
      if (m.callback == kOverlayActivated && m.param && m.size >= 1 && m.param[0]) A.overlay = true;   // (m_bActive, its first byte)
      if (m.callback == kUserStatsReceived) A.retryT = 0.f;   // (the stats are in: the queue at once)
      A.dispatchFree(A.pipe);
    }
  } else if (A.runCallbacks) A.runCallbacks();
  A.retryT -= dt;
  if (A.waiting.empty() || A.retryT > 0.f || !A.stats || !A.setAchievement) return;
  A.retryT = 2.f;
  bool stored = false;
  for (auto it = A.waiting.begin(); it != A.waiting.end();) {
    if (A.setAchievement(A.stats, it->first.c_str())) { stored = true; it = A.waiting.erase(it); }
    else if (++it->second >= 30) it = A.waiting.erase(it);   // (a name Steamworks doesn't know: given up until the next start)
    else ++it;
  }
  if (stored && A.storeStats) A.storeStats(A.stats);
}

void unlock(const std::string& id) {
  if (!A.on) return;
  A.waiting.emplace(steamAchievementName(id), 0);
  A.retryT = std::min(A.retryT, 0.f);
}

void sync(const std::vector<std::string>& ids) { for (const std::string& id : ids) unlock(id); }
int pending() { return (int)A.waiting.size(); }

void presence(const std::string& token, const Pairs& values) {
  if (!A.on || !A.friends || !A.setPresence) return;
  std::string sig = token;
  for (const auto& kv : values) sig += "\n" + kv.first + "=" + kv.second;
  if (sig == A.lastPresence) return;
  A.lastPresence = sig;
  if (A.clearPresence) A.clearPresence(A.friends);
  for (const auto& kv : values) A.setPresence(A.friends, kv.first.c_str(), kv.second.c_str());
  A.setPresence(A.friends, "steam_display", token.c_str());
}

bool onDeck() { return A.on && A.deck; }

bool showKeyboard(int x, int y, int w, int h) {
  if (!A.on || !A.utils || !A.floatingKeyboard) return false;
  if (!A.deck && !(A.bigPicture && A.bigPicture(A.utils))) return false;   // (a desk has a keyboard)
  return A.floatingKeyboard(A.utils, 0, x, y, w, h);   // (k_EFloatingGamepadTextInputModeModeSingleLine)
}

bool overlayOpened() { const bool o = A.overlay; A.overlay = false; return o; }
}
