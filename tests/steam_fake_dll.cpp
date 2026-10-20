// A stand-in for Valve's steam_api64.dll, for steam_test (on Linux built as a shared object): the flat exports src/steam.cpp looks for, each
// call written to a log the test reads back (FakeSteam_Log). The achievements fail until the stats are "in" (two tries),
// the user stats interface answers only as its v013 (the loader must look down from the newest), and the overlay's
// callback is queued on request (FakeSteam_Overlay)
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))   // (the Linux tests: a shared object, the same loader)
#endif

static std::string g_log;
static int g_statsWait = 2;
static bool g_overlay = false;
static unsigned char g_overlayParam[16];
static int g_iface[4];   // (the interfaces: their addresses are all the test needs)
struct CallbackMsg { int user; int callback; unsigned char* param; int size; };
// the leaderboards' calls: each answered by a SteamAPICallCompleted_t on the next frame, its result read by its handle
struct CallCompleted { uint64_t call; int callback; uint32_t size; };
struct BoardFound { uint64_t board; uint8_t found; };
struct ScoreUploaded { uint8_t success; uint64_t board; int32_t score; uint8_t changed; int rankNew, rankPrev; };
struct Answer { int callback; std::vector<uint8_t> bytes; };
static std::deque<std::vector<uint8_t>> g_completed;
static std::vector<uint8_t> g_msg;
static std::map<uint64_t, Answer> g_answers;
static uint64_t g_nextCall = 100;
template <class T> static uint64_t answer(int callback, const T& r) {
  const uint64_t call = g_nextCall++;
  Answer a; a.callback = callback; a.bytes.resize(sizeof r); memcpy(a.bytes.data(), &r, sizeof r); g_answers[call] = a;
  CallCompleted cc = {call, callback, (uint32_t)sizeof r};
  std::vector<uint8_t> m(sizeof cc); memcpy(m.data(), &cc, sizeof cc); g_completed.push_back(m);
  return call;
}

EXPORT int SteamAPI_InitFlat(char* err) { (void)err; g_log += "init;"; return 0; }
EXPORT void SteamAPI_Shutdown() { g_log += "shutdown;"; }
EXPORT void* SteamAPI_SteamUserStats_v013() { return &g_iface[0]; }
EXPORT void* SteamAPI_SteamFriends_v017() { return &g_iface[1]; }
EXPORT void* SteamAPI_SteamUtils_v010() { return &g_iface[2]; }
EXPORT void* SteamAPI_SteamScreenshots_v003() { return &g_iface[3]; }
EXPORT unsigned SteamAPI_ISteamScreenshots_AddScreenshotToLibrary(void* self, const char* file, const char* thumb, int w, int h) {
  g_log += self == &g_iface[3] ? std::string("shot ") + file + (thumb ? " thumb" : "") + " " + std::to_string(w) + "x" + std::to_string(h) + ";" : std::string("shot on the wrong interface;");
  return 7;
}
EXPORT uint64_t SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(void* self, const char* name, int sort, int display) {
  g_log += std::string("find ") + name + " " + std::to_string(sort) + " " + std::to_string(display) + ";";
  BoardFound r = {self == &g_iface[0] ? 555u : 0u, (uint8_t)(self == &g_iface[0])};
  return answer(1104, r);
}
EXPORT uint64_t SteamAPI_ISteamUserStats_UploadLeaderboardScore(void*, uint64_t board, int method, int32_t score, const int32_t*, int) {
  g_log += "upload " + std::to_string(board) + " " + std::to_string(method) + " " + std::to_string(score) + ";";
  ScoreUploaded r = {1, board, score, 1, 3, 0};
  return answer(1106, r);
}
EXPORT int SteamAPI_ISteamUserStats_GetLeaderboardEntryCount(void*, uint64_t board) { return board == 555 ? 42 : 0; }
EXPORT bool SteamAPI_ManualDispatch_GetAPICallResult(int pipe, uint64_t call, void* out, int size, int expected, bool* failed) {
  auto it = g_answers.find(call);
  if (pipe != 7 || it == g_answers.end() || it->second.callback != expected || (size_t)size != it->second.bytes.size()) { g_log += "bad call result;"; return false; }
  memcpy(out, it->second.bytes.data(), it->second.bytes.size()); *failed = false;
  g_answers.erase(it);
  return true;
}
EXPORT bool SteamAPI_ISteamUserStats_SetAchievement(void* self, const char* name) {
  if (self != &g_iface[0]) { g_log += "ach on the wrong interface;"; return false; }
  if (g_statsWait > 0) { g_statsWait--; g_log += "ach wait;"; return false; }
  if (std::string(name).rfind("ACH_NO_SUCH", 0) == 0) return false;   // (a name the app's Steamworks page doesn't have)
  g_log += std::string("ach ") + name + ";";
  return true;
}
EXPORT bool SteamAPI_ISteamUserStats_StoreStats(void* self) { g_log += self == &g_iface[0] ? "store;" : "store on the wrong interface;"; return true; }
EXPORT bool SteamAPI_ISteamFriends_SetRichPresence(void* self, const char* k, const char* v) {
  g_log += self == &g_iface[1] ? std::string("rp ") + k + "=" + v + ";" : std::string("rp on the wrong interface;");
  return true;
}
EXPORT void SteamAPI_ISteamFriends_ClearRichPresence(void*) { g_log += "rpclear;"; }
EXPORT bool SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck(void* self) { return self == &g_iface[2]; }
EXPORT bool SteamAPI_ISteamUtils_IsSteamInBigPictureMode(void*) { return false; }
EXPORT bool SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput(void*, int mode, int x, int y, int w, int h) {
  g_log += "kb " + std::to_string(mode) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " + std::to_string(h) + ";";
  return true;
}
EXPORT void SteamAPI_ManualDispatch_Init() { g_log += "manual;"; }
EXPORT int SteamAPI_GetHSteamPipe() { return 7; }
EXPORT void SteamAPI_ManualDispatch_RunFrame(int pipe) { if (pipe != 7) g_log += "frame on the wrong pipe;"; }
EXPORT bool SteamAPI_ManualDispatch_GetNextCallback(int pipe, CallbackMsg* m) {
  if (pipe == 7 && !g_completed.empty()) {   // (a call's answer: SteamAPICallCompleted_t)
    g_msg = g_completed.front(); g_completed.pop_front();
    m->user = 1; m->callback = 703; m->param = g_msg.data(); m->size = (int)g_msg.size();
    return true;
  }
  if (pipe != 7 || !g_overlay) return false;
  g_overlay = false;
  g_overlayParam[0] = 1;
  m->user = 1; m->callback = 331; m->param = g_overlayParam; m->size = (int)sizeof g_overlayParam;
  return true;
}
EXPORT void SteamAPI_ManualDispatch_FreeLastCallback(int) { g_log += "free;"; }

EXPORT const char* FakeSteam_Log() { return g_log.c_str(); }
EXPORT void FakeSteam_Clear() { g_log.clear(); }
EXPORT void FakeSteam_Overlay() { g_overlay = true; }
