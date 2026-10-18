// Soak: a long session in one game, headless - leg after leg on the autopilot with the traffic and the tower voices on,
// every career type in turn, an XR-40 sortie with its weapons between them - and nothing it keeps grows without end:
// the process's memory after the first legs, the particles, toasts, radio history, pending calls, traffic and the
// XR-40's bolts, bombs, blasts and craters all stay bounded (looked at every second of every leg, not only at its end),
// and no state turns non-finite. The session has to have used them: the towers spoke, the XR-40 fired and bombed.
// SOAK_LEGS sets the session's length (12 by default; a release candidate's soak runs hundreds). The rendered soak on
// real hardware is diagnostics.bat soak.
#include "../src/game.h"
#include "test_world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif
#if defined(__SANITIZE_ADDRESS__)
#define SOAK_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SOAK_SANITIZED 1
#endif
#endif

static double residentMB() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc = {};
  return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.WorkingSetSize / 1048576.0 : 0.0;
#else
  long pages = 0, resident = 0;
  if (FILE* f = fopen("/proc/self/statm", "r")) { if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0; fclose(f); }
  return resident * (double)sysconf(_SC_PAGESIZE) / 1048576.0;
#endif
}

struct SoakTest {
  static int run() {
    buildTestWorld(); buildStory();
    g_audio.init(48000);
    const int legs = std::max(3, getenv("SOAK_LEGS") ? atoi(getenv("SOAK_LEGS")) : 12);
    const float dt = 1.f / 60.f;
    Game g; g.initHeadless(); g.diskless = true; g.botControl = false; g.set.traffic = true;
    int fails = 0, landed = 0, shots = 0, dropped = 0, sorties = 0;
#ifdef SOMNIUM_ASSETS
    const bool voices = g.atc.load(std::string(SOMNIUM_ASSETS) + "/voice");   // (without them every call is dropped unsaid)
#else
    const bool voices = false;
#endif
    if (!voices) { printf("  !! the tower voices did not load (assets/voice): the radio would go untested\n"); fails++; }
    std::vector<int> airports;
    for (const char* code : {"CAP", "MDB", "ORC", "PVI", "GLR"}) { const int a = g_world.findAirport(code); if (a >= 0) airports.push_back(a); }
    double mem0 = 0, memMax = 0;
    size_t worst[6] = {}, peak[6] = {};   // particles, toasts, ATC history, pending calls, traffic, XR-40 weapons
    auto sizes = [&](size_t s[6]) {
      const Game::SomnusState& W = g.somnus;
      s[0] = g.particles.size(); s[1] = g.toasts.size(); s[2] = g.atc.history.size(); s[3] = g.commsPending.size();
      s[4] = g.traffic.craft.size(); s[5] = W.bolts.size() + W.bombs.size() + W.blasts.size() + W.craters.size() + W.scorch.size();
    };
    bool finite = true;
    static float abuf[2 * 4096];
    // a frame: the game, then the audio thread's share of it while a voice is speaking (it plays out, so the next call
    // can start; the rest of the mix costs more than the game and its queue is bounded), and each second what the
    // lists hold
    auto frame = [&](float t) {
      g.update(dt); g.in.endFrame();
      if (g_audio.voiceBusy()) g_audio.render(abuf, 800);
      finite &= std::isfinite(g.plane.pos.x) && std::isfinite(g.plane.vel.y);
      if (fmodf(t, 1.f) < dt) { size_t now[6]; sizes(now); for (int i = 0; i < 6; i++) peak[i] = std::max(peak[i], now[i]); }
    };
    for (int leg = 0; leg < legs; leg++) {
      const bool sortie = leg % 4 == 3;   // every fourth leg: the XR-40 with its weapons
      const int from = airports[leg % airports.size()], to = airports[(leg + 1) % airports.size()];
      std::fill(std::begin(peak), std::end(peak), 0);
      if (sortie) {
        g.resCraft = kSomnus; g.resAirborne = true; g.resAirport = from; g.launchResearch();
        g.somnus.armed = true;
        for (float t = 0; t < 120.f && g.screen == SCR_FLIGHT; t += dt) {
          g.in = Input();
          if (fmodf(t, 2.f) < dt) g.somnus.bombQueue = std::min(3, g.somnus.bombQueue + 1);
          g.in.mDown[0] = fmodf(t, 1.f) < .5f;   // (the lasers in bursts)
          frame(t);
        }
        sorties++; shots += g.somnus.shots; dropped += g.somnus.dropped;
        g.in = Input();
      } else {
        const int spec = kCareerAircraft[leg % kNumAircraft];
        Contract c = g_story[0]; c.forceAircraft = -1; c.type = CT_FERRY; c.from = from; c.to = to; c.wps.clear(); c.hints.clear();
        g.startFlight(c, spec, Career::SRC_RENT);
        const Airport& a = g_world.airports[to];
        const vec3 dir = normalize(vec3(a.pos().x - g_world.airports[from].pos().x, 0, a.pos().z - g_world.airports[from].pos().z) + vec3(1e-3f, 0, 0));
        const vec3 start = a.pos() - dir * 9000.f + vec3(0, 900.f, 0);
        const AircraftSpec& s = kAircraft[spec];
        g.plane.reset(&s, start, wrapDeg360(atan2f(dir.x, -dir.z) / DEG), s.maxFuel * .6f, 100, true, s.cruise * .8f);
        g.takeoffAnnounced = true; g.engineAutoStarted = true; g.plane.ctl.throttle = .7f;
        g.apDest = to; g.engageAutopilot();
        for (float t = 0; t < 420.f && g.screen == SCR_FLIGHT; t += dt) {
          frame(t);
          if (g.plane.onGround && length(g.plane.vel) < 1.f) { landed++; break; }
        }
      }
      { size_t now[6]; sizes(now); for (int i = 0; i < 6; i++) peak[i] = std::max(peak[i], now[i]); }
      for (int i = 0; i < 6; i++) worst[i] = std::max(worst[i], peak[i]);
      if (g.screen == SCR_FLIGHT) g.endFlight(false, "soak", OUT_ABANDONED);
      g.screen = SCR_HUB; g.in = Input();
      for (int i = 0; i < 30; i++) frame(1.5f);   // (back at the hub a moment)
      const double mb = residentMB();
      if (leg == 1) mem0 = mb;   // (after the first legs: every type's first use, the world's chunks, the voices)
      memMax = std::max(memMax, mb);
      printf("  leg %3d %-18s %4.0f MB  most in the leg: particles %zu toasts %zu history %zu pending %zu traffic %zu XR-40 weapons %zu\n", leg + 1,
             sortie ? "XR-40 sortie" : kAircraft[kCareerAircraft[leg % kNumAircraft]].name, mb, peak[0], peak[1], peak[2], peak[3], peak[4], peak[5]);
      fflush(stdout);
    }
    const double growth = memMax - mem0;
#ifdef SOAK_SANITIZED
    const bool memOk = true;   // (the sanitizers keep freed memory quarantined: the process's size says nothing here)
#else
    const bool memOk = mem0 > 0 && growth < 96.0;
#endif
    const bool bounded = worst[0] <= 9000 && worst[1] <= 5 && worst[2] <= g.atc.historyLimit && worst[3] < 64 && worst[4] < 400 && worst[5] < 4096 &&
                         g.atc.cachedBytes() <= g.atc.cacheLimit;
    const bool flew = landed >= (legs - legs / 4) / 2;   // (most legs end on the runway; a few don't in the time given)
    const bool used = worst[2] > 0 && (sorties == 0 || (shots > 0 && dropped > 0));   // (a list never filled proves nothing)
    printf("Soak, %d legs: memory %.0f MB after the first legs, %.0f MB at most (+%.0f MB) %s; lists bounded %d (particles %zu, toasts %zu, history %zu, pending %zu, traffic %zu, XR-40 weapons %zu); %d landed; state finite %d\n",
           legs, mem0, memMax, growth, memOk ? "ok" : "GROWING", bounded, worst[0], worst[1], worst[2], worst[3], worst[4], worst[5], landed, finite);
    printf("Soak, what it used: the towers' history reached %zu calls, %.1f MB of voice clips kept (budget %.0f MB); %d XR-40 sorties fired %d bolts and dropped %d bombs: %s\n",
           worst[2], g.atc.cachedBytes() / 1048576.0, g.atc.cacheLimit / 1048576.0, sorties, shots, dropped, used ? "ok" : "UNUSED");
    fails += !memOk + !bounded + !flew + !finite + !used;
    printf("Soak: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
  }
};

int main() { return SoakTest::run(); }
