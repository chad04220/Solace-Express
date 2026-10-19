// A stand-in for Valve's steam_api64.dll, for steam_test on Windows: the flat exports src/steam.cpp looks for, each
// call written to a log the test reads back (FakeSteam_Log). The achievements fail until the stats are "in" (two tries),
// the user stats interface answers only as its v013 (the loader must look down from the newest), and the overlay's
// callback is queued on request (FakeSteam_Overlay)
#include <string>
#define EXPORT extern "C" __declspec(dllexport)

static std::string g_log;
static int g_statsWait = 2;
static bool g_overlay = false;
static unsigned char g_overlayParam[16];
static int g_iface[3];   // (the interfaces: their addresses are all the test needs)
struct CallbackMsg { int user; int callback; unsigned char* param; int size; };

EXPORT int SteamAPI_InitFlat(char* err) { (void)err; g_log += "init;"; return 0; }
EXPORT void SteamAPI_Shutdown() { g_log += "shutdown;"; }
EXPORT void* SteamAPI_SteamUserStats_v013() { return &g_iface[0]; }
EXPORT void* SteamAPI_SteamFriends_v017() { return &g_iface[1]; }
EXPORT void* SteamAPI_SteamUtils_v010() { return &g_iface[2]; }
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
