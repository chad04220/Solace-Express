// Somnium Volandi - Steam: achievements, rich presence, the overlay's pause and the Deck's on-screen keyboard, through
// Valve's own steam_api64.dll when it sits beside the exe (the Steam depot ships it) and Steam is running. Loaded at
// run time, from the exe's folder only: without it (the GitHub and itch.io builds, Linux, the tests and tools) every
// call here does nothing, and nothing else changes. No DRM: launched outside Steam the game simply runs
#pragma once
#include <string>
#include <utility>
#include <vector>

// An achievement's Steamworks API name: "butter" -> "ACH_BUTTER" (steam/achievements.csv lists them for the upload)
std::string steamAchievementName(const std::string& id);

namespace steam {
typedef std::vector<std::pair<std::string, std::string>> Pairs;

bool init(const std::string& exeDir);   // true once Steam is running and the game is one of the player's apps
void shutdown();
bool active();
const std::string& status();            // a line for startup.log: on, off and why
void tick(float dt);                    // Steam's callbacks; queued achievements set and stored
void unlock(const std::string& id);     // queued: set as soon as Steam has the player's stats (retried a minute)
void sync(const std::vector<std::string>& ids);   // every achievement already earned, sent again at the start
int pending();                          // achievements still waiting on Steam (the tests)
// The status friends see: a token of steam/rich_presence_english.vdf and the values it fills in. Sent on a change only
void presence(const std::string& token, const Pairs& values);
bool onDeck();
bool showKeyboard(int x, int y, int w, int h);   // the Deck's (or Big Picture's) floating keyboard over a text field: it types as keys
bool overlayOpened();                   // the Steam overlay opened since the last call (the flight pauses)
}
