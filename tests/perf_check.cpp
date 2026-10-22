// Performance check: what the game's own work costs on this machine's CPU, part by part, headless. The islands' build
// and their cache's read; the generated regions' and (EARTH_PACK) the real Earth's charts, built and prepared for the
// renderer as a flight's next chart is; every type's flight model and learned performance; the game's update in flight
// with the traffic on (cruise at 1x, 4x and 16x, the busy field on the ground, the XR-40's sortie, a real-Earth chart);
// the scenery's chunks where they are thickest; the quote's background flight and the hub's estimate; a career's save
// and load; the audio mix; and the memory each leaves behind. Not the GPU's: that is the rendered diagnostics on real
// hardware (diagnostics.bat). EARTH_LIVE=<folder>: the release's pack installed there streamed, as the game installs it,
// and charts built with their land and their GLO-90 terrain fetched live. PERF_ONLY=<words> runs only the parts named,
// PERF_WORLD=<file> keeps the islands in a cache between runs. Built on request (make perf_check), not run by ctest: it
// times, it doesn't judge.
#include "../src/game.h"
#include "../src/earth_charts.h"
#include "../src/earth_install.h"
#include "../src/renderer.h"
#include "test_world.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
static double residentMB() {
#ifdef _WIN32
  return 0.0;
#else
  long pages = 0, resident = 0;
  if (FILE* f = fopen("/proc/self/statm", "r")) { if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0; fclose(f); }
  return resident * (double)sysconf(_SC_PAGESIZE) / 1048576.0;
#endif
}
struct Stats { double med = 0, p99 = 0, max = 0, mean = 0; };
static Stats stats(std::vector<double> v) {
  Stats s; if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.med = v[v.size() / 2]; s.p99 = v[std::min(v.size() - 1, (size_t)std::ceil(v.size() * 0.99) - 1)]; s.max = v.back();
  for (double x : v) s.mean += x;
  s.mean /= v.size();
  return s;
}
static void section(const char* name) { printf("\n== %s (%.0f MB resident)\n", name, residentMB()); fflush(stdout); }

struct GameTest {
  static constexpr float kDt = 1.f / 60.f;
  // a flight's frames on the game's thread: each update timed (µs), the audio thread's mix of the same 60th timed beside it
  struct Run { std::vector<double> update, mix; int slow = 0, slowWithCall = 0, calls = 0, inPlace = 0; };
  static Run fly(Game& g, int frames, const std::function<void(int)>& each = nullptr) {
    Run r; static float abuf[2 * 800];
    const size_t calls0 = g.atc.history.size(); const int inPlace0 = g.atc.decodedInPlace;
    for (int i = 0; i < frames && g.screen == SCR_FLIGHT; i++) {
      if (each) each(i);
      const size_t calls = g.atc.history.size();
      const auto t0 = Clock::now();
      g.update(kDt); g.in.endFrame();
      r.update.push_back(msSince(t0) * 1000.0);
      if (r.update.back() > 8000.0) { r.slow++; r.slowWithCall += g.atc.history.size() != calls; }
      const auto t1 = Clock::now();
      g_audio.render(abuf, 800);
      r.mix.push_back(msSince(t1) * 1000.0);
    }
    r.calls = (int)(g.atc.history.size() - std::min(calls0, g.atc.history.size())); r.inPlace = g.atc.decodedInPlace - inPlace0;
    return r;
  }
  static void row(const char* what, const Run& r, const Game& g) {
    const Stats u = stats(r.update), m = stats(r.mix);
    printf("  %-34s update %7.0f µs median %7.0f p99 %8.0f max   mix %5.0f µs median   (%zu frames, warp %.0fx, traffic %zu; %d over 8 ms, %d as a tower call began;\n"
           "  %-34s %d calls, %d clips decoded on the game's thread)\n",
           what, u.med, u.p99, u.max, m.med, r.update.size(), g.timeAccel, g.traffic.craft.size(), r.slow, r.slowWithCall, "", r.calls, r.inPlace);
    if (r.update.empty()) printf("    (no frames: screen %d, crashed %d, %s)\n", (int)g.screen, (int)g.crashed, g.debriefTitle.c_str());
    fflush(stdout);
  }
  // a ferry from one field to the next, begun 9 km out at 900 m on the autopilot (as the soak flies it)
  static void ferry(Game& g, int spec, int from, int to) {
    Contract c = g_story[0]; c.forceAircraft = -1; c.type = CT_FERRY; c.from = from; c.to = to; c.wps.clear(); c.hints.clear();
    g.startFlight(c, spec, Career::SRC_RENT);
    const Airport& a = g_world.airports[to];
    const vec3 dir = normalize(vec3(a.pos().x - g_world.airports[from].pos().x, 0, a.pos().z - g_world.airports[from].pos().z) + vec3(1e-3f, 0, 0));
    const vec3 start = a.pos() - dir * 30000.f + vec3(0, 900.f, 0);
    const AircraftSpec& s = kAircraft[spec];
    g.plane.reset(&s, start, wrapDeg360(atan2f(dir.x, -dir.z) / DEG), s.maxFuel * .6f, 100, true, s.cruise * .8f);
    g.takeoffAnnounced = true; g.engineAutoStarted = true; g.plane.ctl.throttle = .7f;
    g.apDest = to; g.engageAutopilot();
  }

  static int run() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    printf("Performance check: %u hardware threads\n", std::thread::hardware_concurrency());
    const std::string tmp = getenv("PERF_TMP") ? getenv("PERF_TMP") : "/tmp";
    // PERF_ONLY=<words>: only the parts named (world perf model flight tower scenery hub save regions earth live)
    const char* only = getenv("PERF_ONLY");
    auto want = [&](const char* key) { return !only || strstr(only, key) != nullptr; };

    section("the islands");
    if (const char* keep = getenv("PERF_WORLD")) { g_world.build(keep, "perf-check", true); printf("  %s (%s)\n", keep, g_world.fromCache ? "read from it" : "generated into it"); }
    else {
      const std::string cache = tmp + "/perf_world.bin";
      remove(cache.c_str());
      auto t0 = Clock::now(); g_world.build(cache, "perf-check", true);
      const double cold = msSince(t0);
      t0 = Clock::now(); g_world.build(cache, "perf-check", true);
      const double warm = msSince(t0);
      printf("  generated and saved %8.0f ms   read from its cache %6.0f ms (%s)   %zu airports\n", cold, warm,
             g_world.fromCache ? "from the cache" : "NOT from the cache", g_world.airports.size());
      remove(cache.c_str());
    }
    buildStory();
    g_audio.init(48000);
    for (int i : kCareerAircraft) Plane::perf(&kAircraft[i]);   // (as the game's start learns them, or reads them from its cache)

    if (want("perf")) {
      section("each type's learned performance (Plane::perf, once a build; cached after)");
      double total = 0, worst = 0; const char* worstName = "";
      for (int i = 0; i < kAircraftCount; i++) {
        const auto t0 = Clock::now(); Plane::perf(&kAircraft[i]); const double ms = msSince(t0);
        total += ms; if (ms > worst) { worst = ms; worstName = kAircraft[i].name; }
      }
      printf("  all %d types %8.0f ms, the slowest %s %.0f ms\n", kAircraftCount, total, worstName, worst);
    }

    if (want("model")) {
      section("the flight model: one 60th of a second of each type in cruise on the autopilot");
      Weather calm; calm.windSpeed = 0; calm.gust = 0; calm.turbulence = 0;
      vec3 sea(0.f, 0.f, 36000.f);   // (open water, out in the border's sea: nothing to fly into for half an hour)
      for (int k = 0; k < 400 && g_world.height(sea.x, sea.z) > -20.f; k++) sea = vec3(-36000.f + (k % 20) * 3600.f, 0.f, 36000.f - (k / 20) * 3600.f);
      for (int i = 0; i < kAircraftCount; i++) {
        const AircraftSpec& s = kAircraft[i];
        Plane::perf(&s);   // (learned first: not in its first step's time)
        Plane p; p.reset(&s, sea + vec3(0.f, 1500.f, 0.f), 90.f, s.maxFuel * .5f, 100, true, s.cruise);
        p.engineRunning = true; p.engineSpool = .75f; p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f;
        p.apEngage(Plane::AP_HOLD, -1, calm); p.apAlt = p.pos.y; p.apHeading = p.heading();
        std::vector<double> us;
        for (int k = 0; k < 1800; k++) { p.ctl.throttle = .75f; const auto t0 = Clock::now(); p.step(kDt, calm, k * kDt); us.push_back(msSince(t0) * 1000.0); }
        const Stats st = stats(us);
        printf("  %-22s %6.1f µs median %6.0f max   (%s at %.0f m, %.0f m/s)\n", s.name, st.med, st.max, p.ev.crashed ? ("CRASHED: " + p.ev.crashReason).c_str() : "flying", p.pos.y, length(p.vel));
      }
    }

    section("the game's update in flight, the traffic on (headless: the scenery's streaming is the renderer's)");
    Game g; g.initHeadless(); g.diskless = true; g.botControl = false; g.set.traffic = true;
#ifdef SOMNIUM_ASSETS
    g.atc.load(std::string(SOMNIUM_ASSETS) + "/voice");
#endif
    if (getenv("PERF_WARM_VOICES")) {   // (every clip decoded beforehand: what's left of a call's start is its own)
      g.atc.cacheLimit = size_t(2) << 30;
      for (const auto& id : g.atc.indexed()) g.atc.decodes(id);
      printf("  every voice clip decoded beforehand: %.0f MB\n", g.atc.cachedBytes() / 1048576.0);
    }
    const int cap = std::max(0, g_world.findAirport("CAP")), mdb = std::max(0, g_world.findAirport("MDB")), pvi = std::max(0, g_world.findAirport("PVI"));
    if (want("flight")) {
      for (float warp : {1.f, 4.f, 16.f}) {
        ferry(g, 1, mdb, cap);
        fly(g, 120);   // (settled: the traffic spawned, the first calls made)
        g.timeAccel = warp;
        const Run r = fly(g, 1800);
        char what[64]; snprintf(what, sizeof what, "cruise to CAP, %.0fx", warp);
        row(what, r, g);
        g.timeAccel = 1.f;
        if (g.screen == SCR_FLIGHT) g.endFlight(false, "perf", OUT_ABANDONED);
        g.screen = SCR_HUB;
      }
      {
        ferry(g, 6, pvi, cap);   // (an airliner)
        fly(g, 120);
        row("airliner cruise to CAP, 1x", fly(g, 1800), g);
        if (g.screen == SCR_FLIGHT) g.endFlight(false, "perf", OUT_ABANDONED);
        g.screen = SCR_HUB;
      }
      {   // on the ground at the capital, engine running, brakes on: the field's traffic and the tower about it
        Contract c = g_story[0]; c.forceAircraft = -1; c.type = CT_FERRY; c.from = cap; c.to = mdb; c.wps.clear(); c.hints.clear();
        g.startFlight(c, 1, Career::SRC_RENT);
        g.engineAutoStarted = true; g.plane.engineRunning = true; g.plane.ctl.brake = 1.f;
        fly(g, 120);
        row("on the ground at CAP", fly(g, 1800), g);
        if (g.screen == SCR_FLIGHT) g.endFlight(false, "perf", OUT_ABANDONED);
        g.screen = SCR_HUB;
      }
      {   // the XR-40, its lasers in bursts and a bomb every two seconds
        g.resCraft = kSomnus; g.resAirborne = true; g.resAirport = mdb; g.launchResearch();
        g.somnus.armed = true;
        const Run r = fly(g, 1800, [&](int i) {
          g.in = Input();
          if (i % 120 == 0) g.somnus.bombQueue = std::min(3, g.somnus.bombQueue + 1);
          g.in.mDown[0] = (i % 60) < 30;
        });
        row("XR-40 sortie, firing", r, g);
        g.in = Input();
        if (g.screen == SCR_FLIGHT) g.endFlight(false, "perf", OUT_ABANDONED);
        g.screen = SCR_HUB;
      }
    }

    if (want("tower")) {
      section("the tower's voice clips: each decoded and resampled the first time it is said");
      AtcVoice v; std::vector<double> ms; double secs = 0;
#ifdef SOMNIUM_ASSETS
      v.load(std::string(SOMNIUM_ASSETS) + "/voice");
#endif
      const auto ids = v.indexed();
      for (size_t i = 0; i < ids.size() && ms.size() < 60; i += std::max<size_t>(1, ids.size() / 60)) {
        const auto t0 = Clock::now(); if (v.decodes(ids[i])) { ms.push_back(msSince(t0)); }
      }
      secs = v.cachedBytes() / 4.0 / 48000.0;
      const Stats st = stats(ms);
      printf("  %zu clips indexed; %zu decoded: %.1f ms median, %.1f mean, %.1f max (%.1f s of speech, %.1f ms a second of it)\n", ids.size(), ms.size(),
             st.med, st.mean, st.max, secs, secs > 0 ? st.mean * ms.size() / secs : 0.0);
    }

    if (want("scenery")) {
      section("the scenery: chunks of 256 m made at their full detail (on the workers; within 700 m, the game's thread)");
      struct Place { const char* name; float x, z; };
      const Airport& A = g_world.airports[cap];
      const Place places[] = {{"Solace Capital", A.x + 1500.f, A.z + 1500.f}, {"the capital's centre", -3200.f, -1200.f},
                              {"forest", -24000.f, -5000.f}, {"Meadowbrook farmland", g_world.airports[mdb].x + 2000.f, g_world.airports[mdb].z},
                              {"open sea", 36000.f, 36000.f}};
      for (const Place& pl : places) {
        Scenery s(0);
        std::vector<double> ms; size_t ents = 0;
        const int cx0 = Scenery::chunkOf(pl.x), cz0 = Scenery::chunkOf(pl.z);
        for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
          const auto t0 = Clock::now(); Scenery::Chunk* c = s.ensure(cx0 + dx, cz0 + dz, 2); ms.push_back(msSince(t0));
          if (c) ents += c->ents.size();
        }
        const Stats st = stats(ms);
        printf("  %-24s %6.2f ms a chunk (median), %6.2f mean, %6.2f max   %5zu things a chunk\n", pl.name, st.med, st.mean, st.max, ents / ms.size());
      }
    }

    if (want("hub")) {
      section("the hub: the board's estimates (the game's thread) and their flown quotes (a worker's)");
      g.career = Career{}; g.career.newGame(); g.career.location = cap; g.career.license = LIC_ATP; g.career.money = 1000000;
      g.career.storyIndex = (int)g_story.size(); g.career.refreshBoard();
      printf("  the board at CAP: %zu jobs\n", g.career.board.size());
      std::vector<double> plan, quote;
      int n = 0;
      for (const Contract& c : g.career.board) {
        if (c.longHaul() || n >= 8) continue;
        const int spec = c.forceAircraft >= 0 ? c.forceAircraft : 1;
        const int loops = getenv("PERF_PLAN_LOOP") ? atoi(getenv("PERF_PLAN_LOOP")) : 0;   // (a profiler's samples)
        for (int k = 0; k < loops; k++) g.career.plan(c, spec, Career::SRC_RENT);
        auto t0 = Clock::now(); g.career.plan(c, spec, Career::SRC_RENT); plan.push_back(msSince(t0));
        t0 = Clock::now(); float fuel = -1; simulateFlightMinutes(c, spec, &fuel); quote.push_back(msSince(t0));
        n++;
      }
      const Stats p = stats(plan), q = stats(quote);
      printf("  %d jobs: estimate %.2f ms median, %.2f max   flown quote %.0f ms median, %.0f max\n", n, p.med, p.max, q.med, q.max);
    }

    if (want("save")) {
      section("a career's save and load");
      const std::string path = tmp + "/perf_career.sav";
      std::vector<double> sv, ld;
      for (int i = 0; i < 20; i++) {
        auto t0 = Clock::now(); g.career.save(path); sv.push_back(msSince(t0));
        Career k; t0 = Clock::now(); k.load(path); ld.push_back(msSince(t0));
      }
      printf("  save %.2f ms median   load %.2f ms median\n", stats(sv).med, stats(ld).med);
      remove(path.c_str());
    }

    if (want("regions")) {
      section("generated regions: built and prepared for the renderer (a region's loading, or a flight's next region)");
      const auto regions = procedural::nearby(procedural::solaceDescriptor().origin, 12);
      bool done[3] = {};
      for (const auto& d : regions) {
        const int k = (int)d.scale;
        if (d.authored || done[k]) continue;
        done[k] = true;
        auto w = std::make_unique<World>();
        auto t0 = Clock::now(); w->buildProcedural(d, std::string(), std::string(), false); const double build = msSince(t0);
        t0 = Clock::now(); auto up = Renderer::prepareWorldUpload(*w); const double prep = msSince(t0);
        printf("  %-7s %-22s built %7.0f ms, prepared %6.0f ms   %zu airports   %.0f MB resident\n", procedural::scaleName(d.scale), d.name.c_str(),
               build, prep, w->airports.size(), residentMB());
      }
    }

    if (const char* dir = getenv("EARTH_PACK"); dir && want("earth")) {
      section("the real Earth's charts (EARTH_PACK)");
      std::string why;
      auto pack = earth::Pack::open(dir, &why);
      if (!pack) printf("  the pack doesn't open: %s\n", why.c_str());
      else {
        earthCharts::usePack(pack);
        const auto A = earthCharts::chartForAirport("KMVY");
        if (A) {
          for (int rep = 0; rep < 4; rep++) {   // (the same chart again and again: what a slide leaves behind shows as growth)
            auto w = std::make_unique<World>();
            auto t0 = Clock::now(); w->buildProcedural(*A, std::string(), std::string(), false); const double build = msSince(t0);
            t0 = Clock::now(); auto up = Renderer::prepareWorldUpload(*w); const double prep = msSince(t0);
            printf("  %s (%s) built %6.0f ms, prepared %5.0f ms   %zu airports   %.0f MB resident\n", A->id.c_str(), A->name.c_str(), build, prep,
                   w->airports.size(), residentMB());
          }
          g.debugScene("region-" + A->id);
          g.botControl = true; g.plane.apEngage(Plane::AP_HOLD, -1, g.wx);
          fly(g, 120);
          row("cruise over KMVY's chart, 1x", fly(g, 1800), g);
        } else printf("  no KMVY chart in the pack\n");
      }
    }
    if (const char* live = getenv("EARTH_LIVE"); live && want("live")) {
      section("the real Earth, live: the release's pack streamed (EARTH_LIVE)");
      namespace fs = std::filesystem;
      auto bytesIn = [](const std::string& dir) {
        uintmax_t n = 0; std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
          if (it->is_regular_file(ec)) n += it->file_size(ec);
        return n / 1048576.0;
      };
      const std::string root = live, work = root + "/work", inst = root + "/earth", glo = root + "/glo90";
      auto t0 = Clock::now();
      {
        earthInstall::Installer installer(earthInstall::kDefaultSource, work, inst, false);
        while (installer.state() == earthInstall::Installer::RUNNING) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        printf("  installed (streamed) in %.1f s: %s   %.1f MB on disk\n", msSince(t0) / 1000.0,
               installer.state() == earthInstall::Installer::DONE ? "done" : installer.status().c_str(), bytesIn(inst));
      }
      std::string why;
      auto pack = earth::Pack::open(inst, &why);
      if (!pack) printf("  the installed pack doesn't open: %s\n", why.c_str());
      else {
        pack->setGlo90Cache(glo); earthCharts::usePack(pack);
        for (const char* ident : {"KMVY", "KACK"}) {
          const auto A = earthCharts::chartForAirport(ident);
          if (!A) { printf("  no chart for %s\n", ident); continue; }
          {   // its GLO-90 tiles, as a flight's loading asks for them (Game::wantGlo90)
            earthFetch::Glo90Fetcher fetch(glo);
            t0 = Clock::now();
            fetch.want(*pack, earthFetch::tilesAround(A->origin.latitude, A->origin.longitude, WORLD_HALF * 1.5f));
            fetch.settle(120.0);
            printf("  %s: GLO-90 tiles %d fetched, %d failed in %.1f s   (%.1f MB of tiles kept)\n", ident, fetch.fetched(), fetch.failed(),
                   msSince(t0) / 1000.0, bytesIn(glo));
          }
          for (int rep = 0; rep < 2; rep++) {
            const double before = bytesIn(inst + "/streamed");
            auto w = std::make_unique<World>();
            t0 = Clock::now();
            try { w->buildProcedural(*A, std::string(), std::string(), false); }
            catch (const std::exception& e) { printf("  %s: the chart failed: %s\n", ident, e.what()); break; }
            printf("  %s's chart %s: built in %.1f s, %.2f MB of land streamed for it   %zu airports\n", ident, rep ? "again (cached)" : "first",
                   msSince(t0) / 1000.0, bytesIn(inst + "/streamed") - before, w->airports.size());
          }
        }
      }
    }
    section("done");
    return 0;
  }
};

int main() { return GameTest::run(); }
