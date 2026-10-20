// Somnium Volandi - Steam (steam.h): Valve's flat C API, found at run time. Each interface's accessor is looked up from
// the newest version down (SteamAPI_SteamUserStats_v013, then _v012...), so the DLL of whichever Steamworks SDK ships
// works; the methods are the version-free flat exports (SteamAPI_ISteamUserStats_SetAchievement), unchanged for years
#include "steam.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX   // (MSVC's windows.h would make std::min a macro)
#endif
#include <windows.h>
#define STEAM_LOADER 1
#elif defined(SOMNIUM_STEAM_DLOPEN)   // (steam_test on Linux: the stand-in DLL built as a shared object, the same loader)
#include <dlfcn.h>
#include <unistd.h>
#define STEAM_LOADER 1
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
const int kCallCompleted = 703, kBoardFound = 1104, kScoreUploaded = 1106;   // (SteamAPICallCompleted_t, LeaderboardFindResult_t, LeaderboardScoreUploaded_t)
// the call results read, as Steam lays them out (8-byte packing on Windows)
struct CallCompleted { uint64_t call; int callback; uint32_t size; };
struct BoardFound { uint64_t board; uint8_t found; };
struct ScoreUploaded { uint8_t success; uint64_t board; int32_t score; uint8_t changed; int rankNew, rankPrev; };
// a score on its way to a leaderboard: the board found (or made), then the score sent, each a call Steam answers later
struct Submit { std::string name; int display = 1, score = 0; uint64_t call = 0; int stage = 0; };   // (stage 0: finding, 1: uploading)

struct Api {
  void* dll = nullptr;
  bool on = false, deck = false, overlay = false;
  int pipe = 0;
  Iface stats = nullptr, friends = nullptr, utils = nullptr, shots = nullptr;
  void (*shutdown)() = nullptr;
  void (*runCallbacks)() = nullptr;
  bool (*setAchievement)(Iface, const char*) = nullptr;
  bool (*storeStats)(Iface) = nullptr;
  bool (*setPresence)(Iface, const char*, const char*) = nullptr;
  void (*clearPresence)(Iface) = nullptr;
  bool (*bigPicture)(Iface) = nullptr;
  bool (*floatingKeyboard)(Iface, int, int, int, int, int) = nullptr;
  unsigned (*addShot)(Iface, const char*, const char*, int, int) = nullptr;   // (a ScreenshotHandle back)
  void (*dispatchFrame)(int) = nullptr;
  bool (*dispatchNext)(int, CallbackMsg*) = nullptr;
  void (*dispatchFree)(int) = nullptr;
  bool (*callResult)(int, uint64_t, void*, int, int, bool*) = nullptr;
  uint64_t (*findBoard)(Iface, const char*, int, int) = nullptr;
  uint64_t (*uploadScore)(Iface, uint64_t, int, int32_t, const int32_t*, int) = nullptr;
  int (*boardCount)(Iface, uint64_t) = nullptr;
  void (*overlayStore)(Iface, unsigned, int) = nullptr;
  std::vector<Submit> submits;
  std::map<std::string, uint64_t> boards;   // a leaderboard's handle, once found
  std::map<std::string, BoardScore> results;
  std::map<std::string, int> waiting;   // API name -> tries
  float retryT = 0.f;
  std::string lastPresence;
};
Api A;
std::string g_status = "Steam: not started";

#ifdef STEAM_LOADER
#ifdef _WIN32
const char* const kDll = "\\steam_api64.dll";
void* openLib(const std::string& path) {   // (by its full path: never the search path)
  if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
  return (void*)LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
void closeLib(void* l) { FreeLibrary((HMODULE)l); }
template <class F> bool sym(F& f, const char* name) {
  f = reinterpret_cast<F>(reinterpret_cast<void (*)()>(GetProcAddress((HMODULE)A.dll, name)));
  return f != nullptr;
}
#else
const char* const kDll = "/steam_api64.so";
void* openLib(const std::string& path) { return access(path.c_str(), F_OK) == 0 ? dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL) : nullptr; }
void closeLib(void* l) { dlclose(l); }
template <class F> bool sym(F& f, const char* name) { f = reinterpret_cast<F>(dlsym(A.dll, name)); return f != nullptr; }
#endif
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
#ifdef STEAM_LOADER
  const std::string path = exeDir + kDll;   // (beside the exe)
  A.dll = openLib(path);
  if (!A.dll) { g_status = "Steam: off (no steam_api64.dll beside the game, or it would not load)"; return false; }
  bool ok = false;
  int (*initFlat)(char*) = nullptr;   // (ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg*): 0 is OK)
  bool (*initOld)() = nullptr;
  std::string why = "Steam isn't running, or the game wasn't started from it";
  if (sym(initFlat, "SteamAPI_InitFlat")) {
    char err[1024] = {};
    ok = initFlat(err) == 0;
    if (!ok && err[0]) why = err;
  } else if (sym(initOld, "SteamAPI_InitSafe") || sym(initOld, "SteamAPI_Init")) ok = initOld();
  if (!ok) { closeLib(A.dll); A = Api(); g_status = "Steam: off (" + why + ")"; return false; }
  sym(A.shutdown, "SteamAPI_Shutdown");
  A.stats = iface("SteamUserStats", 20, 10);
  A.friends = iface("SteamFriends", 25, 15);
  A.utils = iface("SteamUtils", 15, 8);
  A.shots = iface("SteamScreenshots", 6, 2);
  sym(A.setAchievement, "SteamAPI_ISteamUserStats_SetAchievement");
  sym(A.storeStats, "SteamAPI_ISteamUserStats_StoreStats");
  sym(A.setPresence, "SteamAPI_ISteamFriends_SetRichPresence");
  sym(A.clearPresence, "SteamAPI_ISteamFriends_ClearRichPresence");
  sym(A.bigPicture, "SteamAPI_ISteamUtils_IsSteamInBigPictureMode");
  sym(A.floatingKeyboard, "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput");
  sym(A.addShot, "SteamAPI_ISteamScreenshots_AddScreenshotToLibrary");
  sym(A.findBoard, "SteamAPI_ISteamUserStats_FindOrCreateLeaderboard");
  sym(A.uploadScore, "SteamAPI_ISteamUserStats_UploadLeaderboardScore");
  sym(A.boardCount, "SteamAPI_ISteamUserStats_GetLeaderboardEntryCount");
  sym(A.overlayStore, "SteamAPI_ISteamFriends_ActivateGameOverlayToStore");
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
    sym(A.callResult, "SteamAPI_ManualDispatch_GetAPICallResult");   // (the leaderboards' answers: without it, no leaderboards)
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
#ifdef STEAM_LOADER
  closeLib(A.dll);
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
      if (m.callback == kCallCompleted && m.param && m.size >= (int)sizeof(CallCompleted) && A.callResult) {
        CallCompleted cc; memcpy(&cc, m.param, sizeof cc);
        for (size_t i = 0; i < A.submits.size(); i++) {
          Submit& sb = A.submits[i];
          if (sb.call != cc.call) continue;
          bool failed = false;
          if (sb.stage == 0) {
            BoardFound r = {};
            if (A.callResult(A.pipe, cc.call, &r, (int)sizeof r, kBoardFound, &failed) && !failed && r.found && r.board) {
              A.boards[sb.name] = r.board;
              sb.call = A.uploadScore(A.stats, r.board, 1, sb.score, nullptr, 0);   // (k_ELeaderboardUploadScoreMethodKeepBest)
              sb.stage = 1;
              if (sb.call) break;
            }
            A.results[sb.name].failed = true;
          } else {
            ScoreUploaded r = {};
            BoardScore& out = A.results[sb.name];
            if (A.callResult(A.pipe, cc.call, &r, (int)sizeof r, kScoreUploaded, &failed) && !failed && r.success) {
              out.done = true; out.rank = r.rankNew; out.improved = r.changed != 0;
              out.total = A.boardCount ? A.boardCount(A.stats, r.board) : 0;
            } else out.failed = true;
          }
          A.submits.erase(A.submits.begin() + (long)i);
          break;
        }
      }
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

void submitScore(const std::string& board, bool timeMs, int score) {
  if (!A.on || !A.stats || !A.findBoard || !A.uploadScore || !A.callResult || board.empty()) return;
  A.results[board] = BoardScore();
  Submit sb; sb.name = board; sb.display = timeMs ? 3 : 1; sb.score = score;   // (k_ELeaderboardDisplayTypeTimeMilliSeconds, Numeric)
  auto known = A.boards.find(board);
  if (known != A.boards.end()) { sb.stage = 1; sb.call = A.uploadScore(A.stats, known->second, 1, score, nullptr, 0); }
  else sb.call = A.findBoard(A.stats, board.c_str(), 1, sb.display);   // (k_ELeaderboardSortMethodAscending: lowest is best)
  if (sb.call) A.submits.push_back(sb); else A.results[board].failed = true;
}

bool boardScore(const std::string& board, BoardScore& out) {
  auto it = A.results.find(board);
  if (it == A.results.end()) return false;
  out = it->second;
  return true;
}

bool openStore(unsigned appId) {
  if (!A.on || !A.friends || !A.overlayStore || !appId) return false;
  A.overlayStore(A.friends, appId, 0);   // (k_EOverlayToStoreFlag_None)
  return true;
}

bool addScreenshot(const std::string& path, int w, int h) {
  if (!A.on || !A.shots || !A.addShot) return false;
  return A.addShot(A.shots, path.c_str(), nullptr, w, h) != 0;   // (no thumbnail of our own: Steam makes one)
}
}
