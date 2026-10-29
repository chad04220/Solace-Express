// Somnium Volandi - development switches: the environment variables the harnesses, the diagnostics tools and A/B
// checks read (HULLDBG, TOD, WX, SHMAPOFF, REGION_* and some ninety more). They are on for every tool, test and
// harness, and off in a player's ordinary launch of the game (platform_win32.cpp turns them off unless --dev is on
// the command line or a tool mode runs), so a variable that happens to be in a player's shell never changes the game.
#pragma once

namespace dev {
void enable(bool on);
bool enabled();
const char* env(const char* name);   // getenv(name) while enabled, else nullptr
inline bool on(const char* name) { return env(name) != nullptr; }
}
