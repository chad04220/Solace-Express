// Somnium Volandi - the development tools (tool_modes_win32.h): each a function of what WinMain lends it, run instead
// of the game when the command line names it. Lifted out of WinMain (docs/CODE_REWORK_PLAN.md 2.5): the launch reads
// as the launch, and a tool's report is written where its name is.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "tool_modes_win32.h"
#include "benchmark_metrics.h"
#include "dev_switches.h"
#include "game.h"
#include "png_write.h"
#include <algorithm>
#include <array>
#include <thread>

bool toolLaunch(const std::string& cmdLine) {
  return cmdLine.find("--bench ") != std::string::npos || cmdLine.find("--soak ") != std::string::npos || cmdLine.find("--shots ") != std::string::npos || cmdLine.find("--profile ") != std::string::npos || cmdLine.find("--analyze") != std::string::npos || cmdLine.find("--loadshots") != std::string::npos;
}

// the window's messages answered (a long scene would otherwise be marked "Not Responding")
static void pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

  // Analysis: SomniumVolandi.exe --analyze [scenes] - a thorough look at where the frame time goes, written to
  // analysis.txt (with a picture of each scene in the "analysis" folder), all at 1920x1080: CPU vs GPU (update /
  // submit / wait), exact per-pass GPU times (the GPU is waited on at each pass boundary), frame time against render
  // resolution (does it scale with pixel count?) and what each feature costs.
static int analyzeTool(const std::string& cl, ToolHost& host) {
  const size_t k = cl.find("--analyze");
  if (k == std::string::npos) return -1;
  std::string list = "menu,air,storm,night,cockpit,rjet,rjetc,wr_8_0_0_0_1,wr_8_0_-8_0_1_3_2_22.5_2";   // (the last: the XR-40 at night in a storm, hovering over Solace Capital - a scene that holds still: each measurement here is taken a few seconds after the last)
  if (cl.size() > k + 10 && cl[k + 9] == ' ' && cl[k + 10] != '-') { list = cl.substr(k + 10); list = list.substr(0, list.find(' ')); }
  list += ",";
  if (host.fullscreen()) host.toggleFullscreen();
  RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
  SetWindowPos(host.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
  { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
  if (host.swapInterval) host.swapInterval(0);
  char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
  CreateDirectoryA((dir + "\\analysis").c_str(), nullptr);
  FILE* af = fopen((dir + "\\analysis.txt").c_str(), "w");
  if (!af) return 1;
  { BodyLoad r; host.buildBodies(r); }   // (as the game's launch does: the traffic drawn from its meshes, as in flight)
  fprintf(af, "Somnium Volandi performance analysis\nGPU: %s\nCPU threads: %u   Quality: %d   Window: %dx%d\n",
          host.gpu.c_str(), std::thread::hardware_concurrency(), g_ren.quality, g_ren.W, g_ren.H);
  fprintf(af, "\nHow to read this: 'ms' is wall-clock time per frame with the GPU finished (vsync off). Per-pass times are\n"
              "serialized CPU wall times including submit + GPU waits (not isolated GPU durations). Resolution scaling: if a frame\n"
              "takes ~2.2x as long at 100%% as at 67%% (2.2x the pixels), the per-pixel work is the bottleneck.\n");
  auto qpcMs = [&](LARGE_INTEGER a, LARGE_INTEGER b) { return (double)(b.QuadPart - a.QuadPart) / host.freq.QuadPart * 1000.0; };
  auto pump = [&] { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } };
  static const char* kPassName[Renderer::kPasses] = {"world+terrain shadow", "displays", "camera feeds", "objects (airframes)", "airframe shadow proxy", "lighting+clouds+effects", "TAA", "sprites", "bloom", "light shafts", "composite"};
  static const struct { int bit; const char* name; } kFeat[] = {
    {1, "volumetric clouds"}, {2, "terrain shadows"}, {4, "scenery shadow maps"}, {8, "aircraft shadow"}, {16, "point lights"},
    {32, "cloud shadows"}, {64, "half terrain march steps"}, {128, "terrain materials"}, {256, "fog / aerial perspective"},
    {1024, "2K ground photographs (512)"}, {2048, "airfield surfaces"}, {Renderer::kDbgRoads, "roads on the ground"},
    {Renderer::kDbgRoadPaint, "roads' paint (lookup kept)"}, {Renderer::kDbgTowns, "towns' ground and fields"},
    {Renderer::kDbgDetail, "ground detail layers"}, {Renderer::kDbgAniso, "anisotropic filtering"},
    {Renderer::kDbgFoveaMax, "foveated shading ON, max"},   // (switched on, not off: what it saves at its most)
    {Renderer::kDbgPrepassOn, "ground depth first ON"},       // (the prepass switched on: what it saves, or costs)
  };
  struct Summary { std::string sc; double ms, rt, scale; std::string top; };
  std::vector<Summary> sums;
  for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
    std::string sc = list.substr(a, b - a);
    if (sc.empty()) continue;
    pump();
    RECT rc; GetClientRect(host.hwnd, &rc);
    if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
    SetWindowTextA(host.hwnd, ("Somnium Volandi - analysing " + sc).c_str());
    g_ren.stream.sync = true;
    Game* g = new Game();
    g->saveDir = host.game.saveDir; g->diskless = true;
    g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex; g->debugScene(sc);
    g_ren.stream.sync = false;
    auto frames = [&](int n) { for (int i = 0; i < n; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(host.hdc); } };
    auto timed = [&](int n) { frames(8); glFinish(); LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0); frames(n); glFinish(); QueryPerformanceCounter(&t1); return qpcMs(t0, t1) / n; };
    // the probe of the own airframe's shading (5., below) builds a program of its own the first time it is asked
    // for: built here, before anything is timed, and the card given half a second of frames after it to come back
    // to its clocks. v4.0.0's analysis built it in the row's own settling frames - 3 to 11 s each, the airframe's
    // shaders new to the driver after the update - and the GPU, idle through the build, ran the row's first timed
    // frames slow: "costs -3 to -7 ms" in four scenes
    g_ren.dbgOff = Renderer::kProbeMeshShade; frames(1); g_ren.dbgOff = 0;
    {
      LARGE_INTEGER w0, w1; QueryPerformanceCounter(&w0);
      frames(30);
      do { frames(1); QueryPerformanceCounter(&w1); } while (qpcMs(w0, w1) < 500.0);
    }
    fprintf(af, "\n==================== %s ====================\n", sc.c_str());
    // the ground over one frame (renderer.h groundStats): its chunks, what depth was in before it was shaded (the
    // airframe's - from inside, the cockpit's walls - and the ground's own, the prepass), and its fragments: those
    // its shader ran for against those kept (that passed the depth test; the rest were hidden, found so only after
    // they were shaded). With the camera, the aircraft and its fuel, at the scene's start and again at its end: a
    // scene that holds still should read the same twice (v3.49.5's XR-40 hover over the city grew 19 to 24 ms
    // through its measurements, its ground's materials 0.9 to 4.1 ms against v3.49.4's, the picture unchanged)
    const float sceneT0 = g->benchmarkAircraft().clock;
    auto groundLine = [&](const char* when) {
      const double ms = timed(20);
      g_ren.countGround = true; frames(1); g_ren.countGround = false; glFinish();
      double kept = -1, shaded = -1;
      const bool got = g_ren.readGroundCounts(kept, shaded);
      const Renderer::GroundStats& gs = g_ren.groundStats;
      const double px = (double)(int)(g_ren.W * g_ren.renderScale) * (int)(g_ren.H * g_ren.renderScale);
      fprintf(af, "Ground %s (frame %.2f ms): %d chunks + %d coarse; depth in before it: %s%s\n", when, ms, gs.chunks, gs.coarse,
              gs.cockpitFirst ? "the aircraft's (its mesh)" : "not the aircraft's", gs.prepassed ? ", the ground's own" : "");
      if (!got) fprintf(af, "  its fragments: not counted (no ground drawn)\n");
      else if (shaded >= 0)
        fprintf(af, "  its fragments: %.2f M shaded, %.2f M kept (%.0f%% of the %.2f M pixels); %.1f shaded for each kept\n",
                shaded * 1e-6, kept * 1e-6, kept / px * 100.0, px * 1e-6, kept > 0 ? shaded / kept : 0.0);
      else fprintf(af, "  its fragments: %.2f M kept (%.0f%% of the %.2f M pixels); the card doesn't count those shaded\n",
                   kept * 1e-6, kept / px * 100.0, px * 1e-6);
      // the same frame with the ground's depth in first: its shading pass then tests the depth before it shades on
      // any card, so what it shades beyond the kept is the shading's own excess (the 2 x 2 blocks it works in, at
      // the triangles' edges), not hidden ground
      g_ren.dbgOff = Renderer::kDbgPrepassOn; g_ren.countGround = true; frames(1); g_ren.countGround = false; g_ren.dbgOff = 0; glFinish();
      double kept2 = -1, shaded2 = -1;
      if (g_ren.readGroundCounts(kept2, shaded2) && shaded2 >= 0)
        fprintf(af, "  with the ground's depth first: %.2f M shaded, %.2f M kept; %.1f shaded for each kept\n",
                shaded2 * 1e-6, kept2 * 1e-6, kept2 > 0 ? shaded2 / kept2 : 0.0);
      const Game::BenchAircraft A = g->benchmarkAircraft();
      const vec3 c = g_ren.prepassCam;
      fprintf(af, "  camera (%.0f, %.0f, %.0f), aircraft (%.0f, %.0f, %.0f), fuel %.0f kg, engine %s; %.1f s into the scene\n",
              c.x, c.y, c.z, A.pos.x, A.pos.y, A.pos.z, A.fuel, A.engine ? "running" : "stopped", A.clock - sceneT0);
    };
    groundLine("at the start");
    // 1. CPU vs GPU
    {
      double tu = 0, tr = 0, tw = 0; const int N = 30;
      glFinish();
      for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1, t2, t3;
        QueryPerformanceCounter(&t0); g->update(1.f / 60.f);
        QueryPerformanceCounter(&t1); g->render();
        QueryPerformanceCounter(&t2); glFinish();
        QueryPerformanceCounter(&t3); SwapBuffers(host.hdc);
        tu += qpcMs(t0, t1); tr += qpcMs(t1, t2); tw += qpcMs(t2, t3);
      }
      tu /= N; tr /= N; tw /= N;
      fprintf(af, "Frame: %.2f ms (%.1f fps)   CPU update %.2f ms + CPU render/submit %.2f ms, then waiting for the GPU %.2f ms\n",
              tu + tr + tw, 1000.0 / (tu + tr + tw), tu, tr, tw);
      fprintf(af, "  -> %s\n", tw > tu + tr ? "GPU-bound: the CPU finishes long before the GPU" : "CPU-bound or balanced: the CPU side takes as long as the GPU");
      fprintf(af, "Scenery: %d instances drawn from %d chunks, %.2f ms CPU to gather   Render resolution %dx%d\n",
              g_ren.entDrawn, g_ren.stream.chunks, g_ren.entCpuMs, (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale));
    }
    // 2. exact per-pass GPU times
    double passSum[Renderer::kPasses] = {};
    {
      g_ren.syncTiming = true;
      const int N = 20;
      frames(3);
      for (int i = 0; i < N; i++) { frames(1); for (int p = 0; p < Renderer::kPasses; p++) passSum[p] += g_ren.passWall[p] / N; }
      g_ren.syncTiming = false;
      double tot = 0; for (double v : passSum) tot += v;
      fprintf(af, "Serialized pass wall times (CPU submit + GPU wait, %.2f ms total):\n", tot);
      for (int p = 0; p < Renderer::kPasses; p++) fprintf(af, "  %-26s %7.2f ms  %5.1f%%\n", kPassName[p], passSum[p], tot > 0 ? passSum[p] / tot * 100.0 : 0.0);
      // the same passes from the GPU's own timestamps, the frames running as in play (nothing waited for)
      for (float& m : g_ren.passMs) m = 0.f;
      frames(60);
      double gtot = 0; for (float v : g_ren.passMs) gtot += v;
      fprintf(af, "GPU pass times (timestamp queries, frames not waited for: %.2f ms total):\n", gtot);
      for (int p = 0; p < Renderer::kPasses; p++) fprintf(af, "  %-26s %7.2f ms  %5.1f%%\n", kPassName[p], g_ren.passMs[p], gtot > 0 ? g_ren.passMs[p] / gtot * 100.0 : 0.0);
    }
    // 3. resolution scaling
    double ms100 = 0, ms67 = 0;
    {
      static const struct { float scale; const char* name; } kRes[] = {{1.f, "100%"}, {0.85f, " 85%"}, {0.75f, " 75%"}, {0.67f, " 67%"}};
      fprintf(af, "Render resolution (the passes + TAA at a fraction of 1920x1080, upscaled):\n");
      for (const auto& R : kRes) {
        g_ren.setRenderScale(R.scale); frames(4);
        double ms = timed(30);
        if (R.scale == 1.f) ms100 = ms;
        if (R.scale < 0.7f) ms67 = ms;
        fprintf(af, "  %s  %7.2f ms  (%5.1f fps)   pixels x%.2f\n", R.name, ms, 1000.0 / ms, R.scale * R.scale);
      }
      g_ren.setRenderScale(1.f); frames(4);
      double r = ms67 > 0 ? ms100 / ms67 : 0;
      fprintf(af, "  -> 100%% takes %.2fx as long as 67%% (2.23x the pixels): %s\n", r,
              r > 1.8 ? "per-pixel work dominates" : r > 1.35 ? "mostly per-pixel, with a fixed cost besides" : "a fixed per-frame cost dominates (not pixel work)");
    }
    // 4. features, and 5. the probes: each row timed as base, off, off, base, its saving against its own base. A
    // flight's view changes as it is measured, and the GPU's clocks wander: v3.49.2's rows, each against one base
    // taken first, drifted by up to 2 ms over a scene (every feature "saved" about the same). In this order a
    // steady drift cancels
    struct Paired { double base, off; };
    auto paired = [&](int bit) {
      Paired r{0, 0};
      for (int k = 0; k < 4; k++) {
        const bool off = k == 1 || k == 2;
        g_ren.dbgOff = off ? bit : 0;
        frames(3);   // (settled after the switch)
        glFinish(); LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0); frames(12); glFinish(); QueryPerformanceCounter(&t1);
        (off ? r.off : r.base) += qpcMs(t0, t1) / 12 / 2;
      }
      g_ren.dbgOff = 0;
      return r;
    };
    auto rows = [&](const char* title, const char* verb, const auto& list, bool ranked, std::string& top, double& topSave) {
      std::vector<Paired> got; double baseAvg = 0;
      for (const auto& F : list) { got.push_back(paired(F.bit)); baseAvg += got.back().base; }
      baseAvg /= std::max<size_t>(got.size(), 1);
      fprintf(af, "%s; base %.2f ms, each row against its own base (base, off, off, base):\n", title, baseAvg);
      size_t i = 0;
      for (const auto& F : list) {
        const Paired& r = got[i++];
        fprintf(af, "  %-26s %7.2f ms   %s %6.2f ms (%4.1f%%)\n", F.name, r.off, verb, r.base - r.off, (r.base - r.off) / r.base * 100.0);
        if (ranked && r.base - r.off > topSave) { topSave = r.base - r.off; top = F.name; }
      }
    };
    std::string top; double topSave = 0;
    {
      rows("Features: frame time with it switched off", "saves", kFeat, true, top, topSave);
      // 5. where the frame goes: one piece of the work left out at a time (Renderer::kProbe*: the picture is wrong
      // while one is, and these are not settings)
      static const struct { int bit; const char* name; } kProbe[] = {
        {Renderer::kProbeScenery, "scenery (buildings, trees)"}, {Renderer::kProbeTerrain, "terrain"},
        {Renderer::kProbeMarch, "airframe march"}, {Renderer::kProbeMeshShade, "own airframe's shading"},
      };
      std::string unused; double unusedSave = 0;
      rows("Work: frame time with it left out", "costs", kProbe, false, unused, unusedSave);
    }
    // 6. ground depth first as the game decides it in play: one of its trials (prepass_auto.h), timed by the world
    // pass's own GPU timestamps, from "off"
    {
      g_ren.prepareTerrainDepth();
      g_ren.prepassAuto = PrepassAuto(); g_ren.prepassAuto.countdown = 1; g_ren.prepassAutoOn = true;
      frames(PrepassAuto::kTrial + PrepassAuto::kSettle + 2);
      const PrepassAuto& pa = g_ren.prepassAuto;
      if (pa.trials && pa.lastKept > 0.f)
        fprintf(af, "Ground depth first, measured as in play: %s (the world pass %.2f ms without, %.2f ms with)\n",
                pa.on ? "switched ON" : "kept off", pa.lastKept, pa.lastOther);
      else fprintf(af, "Ground depth first, measured as in play: no result (%s)\n", pa.trials ? "too few GPU timestamps came back" : "no trial ran");
      g_ren.prepassAutoOn = false; g_ren.prepassAuto = PrepassAuto();
    }
    groundLine("at the end");
    // a picture of the scene for reference; and where the card has it, the same with foveated shading at its
    // strongest, to see what it costs the picture
    frames(3); glFinish();
    g_ren.screenshotPNG((dir + "\\analysis\\" + sc + ".png").c_str());
    if (g_ren.vrsAvailable) {
      g_ren.dbgOff = Renderer::kDbgFoveaMax; frames(8); glFinish();
      g_ren.screenshotPNG((dir + "\\analysis\\" + sc + "_foveated.png").c_str());
      g_ren.dbgOff = 0;
    }
    double rt = passSum[3];   // (the objects pass: the airframes)
    sums.push_back({sc, ms100, rt, ms67 > 0 ? ms100 / ms67 : 0, top});
    fflush(af);
    delete g;
  }
  fprintf(af, "\n==================== summary ====================\n");
  fprintf(af, "%-16s %9s %9s %12s  %s\n", "scene", "frame ms", "objects", "100%/67%", "most expensive feature");
  for (auto& S : sums) fprintf(af, "%-16s %9.2f %9.2f %12.2f  %s\n", S.sc.c_str(), S.ms, S.rt, S.scale, S.top.c_str());
  fclose(af);
  return 0;
}

  // Profile: SomniumVolandi.exe --profile scene1,scene2,... renders each scene at 1920x1080 once normally and once with
  // each feature switched off (Renderer::dbgOff), and writes profile.txt next to the exe: what every
  // feature costs on this GPU
static int profileTool(const std::string& cl, ToolHost& host) {
  const size_t k = cl.find("--profile ");
  if (k == std::string::npos) return -1;
  std::string list = cl.substr(k + 10); list = list.substr(0, list.find(' ')) + ",";
  if (host.fullscreen()) host.toggleFullscreen();
  RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
  SetWindowPos(host.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
  { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
  if (host.swapInterval) host.swapInterval(0);
  char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
  FILE* pf = fopen((dir + "\\profile.txt").c_str(), "w");
  static const struct { int bit; const char* name; } kFeat[] = {
    {0, "everything on"}, {1, "volumetric clouds"}, {2, "terrain shadows"}, {4, "scenery shadow maps"}, {8, "aircraft shadow"},
    {16, "point lights"}, {32, "cloud shadows"}, {64, "half terrain march steps"}, {128, "terrain materials"}, {256, "fog / aerial perspective"},
    {1024, "2K ground photographs (512)"}, {2048, "airfield surfaces"}, {Renderer::kDbgRoads, "roads on the ground"},
    {Renderer::kDbgRoadPaint, "roads' paint (lookup kept)"}, {Renderer::kDbgTowns, "towns' ground and fields"},
    {Renderer::kDbgDetail, "ground detail layers"}, {Renderer::kDbgAniso, "anisotropic filtering"},
  };
  if (pf) fprintf(pf, "GPU: %s\nRender %dx%d, quality %d. Each line: frame time with that feature off, and what it saves.\n", host.gpu.c_str(), g_ren.W, g_ren.H, g_ren.quality);
  g_ren.stream.sync = true;
  for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
    std::string sc = list.substr(a, b - a);
    if (sc.empty()) continue;
    RECT rc; GetClientRect(host.hwnd, &rc);
    if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
    if (pf) fprintf(pf, "\n%s\n", sc.c_str());
    double base = 0;
    for (const auto& F : kFeat) {
      MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
      SetWindowTextA(host.hwnd, ("Somnium Volandi - profiling " + sc + ": " + F.name).c_str());
      Game* g = new Game();
      g->saveDir = host.game.saveDir; g->diskless = true;
      g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex; g->debugScene(sc);
      g_ren.stream.sync = false; g_ren.dbgOff = F.bit;
      for (int i = 0; i < 30; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(host.hdc); }
      glFinish();
      LARGE_INTEGER f0, f1; QueryPerformanceCounter(&f0);
      const int N = 60;
      for (int i = 0; i < N; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(host.hdc); pump(); }
      glFinish(); QueryPerformanceCounter(&f1);
      double ms = (double)(f1.QuadPart - f0.QuadPart) / host.freq.QuadPart * 1000.0 / N;
      if (F.bit == 0) base = ms;
      if (pf) {
        if (F.bit == 0) fprintf(pf, "  %-26s %7.2f ms   (world %.2f, objects %.2f, shadow proxy %.2f, lighting %.2f)\n", F.name, ms, g_ren.passMs[0], g_ren.passMs[3], g_ren.passMs[4], g_ren.passMs[5]);
        else fprintf(pf, "  %-26s %7.2f ms   saves %6.2f ms\n", F.name, ms, base - ms);
        fflush(pf);
      }
      g_ren.dbgOff = 0; g_ren.stream.sync = true;
      delete g;
    }
  }
  if (pf) fclose(pf);
  return 0;
}

  // Soak: SomniumVolandi.exe --soak MINUTES [--out soak.txt] flies one long session on this PC: one game throughout, a new
  // scene every two minutes (the benchmark's: the islands, the forest, a storm, the night, the cockpits, the research
  // craft), and every 10 s a line of the frame rate, the 1% low, the process's memory, handles and GDI objects. The end
  // says whether the memory or the handles grew (more than 20 MB or 50 handles an hour, after the first quarter) and
  // the exit code is 1 if they did (diagnostics.bat soak)
static int soakTool(const std::string& cl, ToolHost& host) {
  const size_t k = cl.find("--soak ");
  if (k == std::string::npos) return -1;
  const int minutes = std::clamp(atoi(cl.c_str() + k + 7), 1, 24 * 60);
  char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
  std::string outName = "soak.txt"; size_t ko = cl.find("--out ");
  if (ko != std::string::npos) { outName = cl.substr(ko + 6); outName = outName.substr(0, outName.find(' ')); }
  const bool absoluteOut = (outName.size() > 1 && outName[1] == ':') || outName.rfind("\\\\", 0) == 0;
  FILE* sf = fopen((absoluteOut ? outName : dir + "\\" + outName).c_str(), "w");
  if (!sf) { MessageBoxA(host.hwnd, "Cannot open the soak output file.", "Soak", MB_ICONERROR); return 2; }
  fprintf(sf, "Somnium Volandi soak, %d minutes; build %s\nGPU: %s\n\n", minutes, host.game.version.c_str(), host.gpu.c_str());
  static const char* cycle[] = {"air", "forest", "storm", "night", "cockpit", "hud", "rjet", "rjetc", "sunset", "mountain", "snow", "jet"};
  Game* g = new Game();
  g->saveDir = host.game.saveDir; g->diskless = true; g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex;
  LARGE_INTEGER f0, t0, last; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&t0); last = t0;
  std::vector<float> window;   // this 10 s's frame times (ms)
  std::vector<std::array<double, 3>> marks;   // (minutes, private MB, handles)
  double nextLog = 10, nextScene = 0; int sceneIx = -1;
  auto secs = [&](LARGE_INTEGER t) { return (double)(t.QuadPart - t0.QuadPart) / f0.QuadPart; };
  while (!host.game.quit) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    const double t = secs(now);
    if (t >= minutes * 60.0) break;
    if (t >= nextScene) {
      sceneIx = (sceneIx + 1) % (int)(sizeof cycle / sizeof cycle[0]);
      g->debugScene(cycle[sceneIx]);
      g->soakFlyOn();   // (it flies on)
      SetWindowTextA(host.hwnd, fmt("Somnium Volandi - soak %.0f / %d min - %s", t / 60.0, minutes, cycle[sceneIx]).c_str());
      nextScene = t + 120;
    }
    g->update(1.f / 60.f); g->render(); SwapBuffers(host.hdc); pump();
    QueryPerformanceCounter(&now);
    window.push_back((float)((now.QuadPart - last.QuadPart) * 1000.0 / f0.QuadPart)); last = now;
    if (secs(now) >= nextLog) {
      nextLog += 10;
      std::vector<float> w = window; std::sort(w.begin(), w.end()); window.clear();
      double mean = 0; for (float x : w) mean += x; mean /= std::max<size_t>(1, w.size());
      const size_t worst = std::max<size_t>(1, (w.size() + 99) / 100); double slow = 0; for (size_t i = w.size() - std::min(worst, w.size()); i < w.size(); i++) slow += w[i];
      PROCESS_MEMORY_COUNTERS_EX pm = {}; pm.cb = sizeof pm; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
      DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
      const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
      const double priv = pm.PrivateUsage / 1048576.0, mins = secs(now) / 60.0;
      marks.push_back({mins, priv, (double)handles});
      fprintf(sf, "%7.2f min  %-10s %6.1f fps  1%% low %6.1f fps  private %7.0f MB  working set %7.0f MB  handles %5lu  GDI %4lu\n",
              mins, cycle[std::max(0, sceneIx)], mean > 0 ? 1000.0 / mean : 0.0, slow > 0 ? 1000.0 * std::min(worst, w.size()) / slow : 0.0,
              priv, pm.WorkingSetSize / 1048576.0, (unsigned long)handles, (unsigned long)gdi);
      fflush(sf);
    }
  }
  delete g;
  // the trend after the first quarter (the first use of every scene's bodies, programs and scenery is not a leak)
  auto slope = [&](int col) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (const auto& r : marks) if (r[0] >= minutes * 0.25) { sx += r[0]; sy += r[col]; sxx += r[0] * r[0]; sxy += r[0] * r[col]; m++; }
    const double d = m * sxx - sx * sx;
    return m >= 3 && d > 0 ? (m * sxy - sx * sy) / d * 60.0 : 0.0;   // per hour
  };
  const double memRate = slope(1), handleRate = slope(2);
  const bool steady = memRate < 20.0 && handleRate < 50.0;
  fprintf(sf, "\nTrend after the first quarter: memory %+.1f MB/hour, handles %+.1f/hour: %s\n", memRate, handleRate, steady ? "steady" : "GROWING");
  fclose(sf);
  return steady ? 0 : 1;
}

  // Benchmark: SomniumVolandi.exe --bench scene1,scene2,... [--size WxH] times each scene (wall clock with the GPU flushed,
  // plus the GPU time of every pass) and writes bench.txt next to the exe. Optional --bench-frames N
  // and --bench-csv add long runs and raw CPU/app-present/fresh delayed GPU samples; no per-frame waits.
static int benchTool(const std::string& cl, ToolHost& host) {
  const size_t k = cl.find("--bench ");
  if (k == std::string::npos) return -1;
  std::string list = cl.substr(k + 8);
  if (!list.empty() && list[0] == '"') { list = list.substr(1); list = list.substr(0, list.find('"')); }
  else list = list.substr(0, list.find(' '));
  std::vector<std::string> scenes; std::string sceneError;
  if (!benchmark::scenes(list, scenes, sceneError)) { MessageBoxA(host.hwnd, sceneError.c_str(), "Invalid benchmark scenes", MB_ICONERROR); return 2; }
  int sampleFrames = 120; size_t kf = cl.find("--bench-frames ");
  if (kf != std::string::npos) {
    char* end = nullptr; const char* first = cl.c_str() + kf + 15;
    long nFrames = strtol(first, &end, 10);
    if (end == first || (*end && *end != ' ') || nFrames < 2 || nFrames > 1000000) {
      MessageBoxA(host.hwnd, "--bench-frames must be an integer from 2 to 1000000.", "Invalid benchmark length", MB_ICONERROR); return 2;
    }
    sampleFrames = (int)nFrames;
  }
  const bool writeCsv = cl.find("--bench-csv") != std::string::npos;
  int sw2 = 0, sh2 = 0; size_t kz = cl.find("--size ");
  if (kz != std::string::npos) sscanf(cl.c_str() + kz + 7, "%dx%d", &sw2, &sh2);
  if (kz != std::string::npos && cl.compare(kz + 7, 6, "native") == 0) { if (!host.fullscreen()) host.toggleFullscreen(); }   // the whole desktop
  else if (sw2 > 64 && sh2 > 64) {
    if (host.fullscreen()) host.toggleFullscreen();
    if (cl.find("--fullscreen") != std::string::npos) {   // borderless at the monitor's origin, exactly the requested size (1920x1080 fills a 1080p screen)
      SetWindowLong(host.hwnd, GWL_STYLE, (GetWindowLong(host.hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
      SetWindowPos(host.hwnd, HWND_TOP, 0, 0, sw2, sh2, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
      RECT wr = {0, 0, sw2, sh2}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
      SetWindowPos(host.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
    }
  }
  { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
  { RECT rc; GetClientRect(host.hwnd, &rc); if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom); }
  const bool swapOffAccepted = host.swapInterval && host.swapInterval(0);   // request unlocked; driver/compositor may still pace
  char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
  std::string outName = "bench.txt"; size_t ko = cl.find("--out ");
  if (ko != std::string::npos) {
    outName = cl.substr(ko + 6);
    if (!outName.empty() && outName[0] == '"') { outName = outName.substr(1); outName = outName.substr(0, outName.find('"')); }   // (a quoted path)
    else outName = outName.substr(0, outName.find(' '));
  }
  const bool absoluteOut = (outName.size() > 1 && outName[1] == ':') || outName.rfind("\\\\", 0) == 0;
  const std::string outputPath = absoluteOut ? outName : dir + "\\" + outName;
  FILE* bf = fopen(outputPath.c_str(), "w");
  if (!bf) { MessageBoxA(host.hwnd, "Cannot open benchmark output file.", "Benchmark output", MB_ICONERROR); return 2; }
  FILE* csv = writeCsv ? fopen((outputPath + ".frames.csv").c_str(), "w") : nullptr;
  if (writeCsv && !csv) { fclose(bf); MessageBoxA(host.hwnd, "Cannot open benchmark CSV output file.", "Benchmark output", MB_ICONERROR); return 2; }
  if (csv) fprintf(csv, "scene,frame_index,render_frame_serial,frame_wall_ms,present_interval_ms,update_ms,render_submit_ms,swapbuffers_ms,pump_ms,gpu_sample_id,gpu_sample_origin_frame,gpu_render_scene_ms,display_width,display_height,render_width,render_height,render_scale,quality,scenery_pending,terrain_shadow_pending,bake_count,native_1080p_valid,scene_status,frame_start_qpc,present_start_qpc\n");
  if (bf) fprintf(bf, "GPU: %s\nDesktop %dx%d, render %dx%d, quality %d\n\n", host.gpu.c_str(), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), g_ren.W, g_ren.H, g_ren.quality);
  fprintf(bf, "Benchmark samples: %d; fixed simulation dt 1/60 s; QPC frequency %lld Hz\n", sampleFrames, (long long)host.freq.QuadPart);
  fprintf(bf, "Pacing: WGL swap interval 0 requested (%s); software limiter NOT used. Driver/compositor overrides remain possible.\n", swapOffAccepted ? "accepted" : "unconfirmed");
  fprintf(bf, "Frame wall includes CPU work, driver blocking, SwapBuffers and message pump. App present intervals are NOT display intervals.\n"
              "Batch average includes one final GPU drain; per-frame samples do not force GPU completion.\n"
              "GPU samples are raw delayed renderScene queries, excluding later UI/presentation; per-pass snapshots remain EMA diagnostics.\n"
              "Percentiles use nearest rank; 1%% low = reciprocal mean of the slowest ceil(1%% * sample count) intervals.\n");
  auto qpcMs = [&](LARGE_INTEGER x, LARGE_INTEGER y) { return (double)(y.QuadPart - x.QuadPart) * 1000.0 / host.freq.QuadPart; };
  auto printSummary = [&](const char* label, const std::vector<double>& values, bool frameRate) {
    const auto m = benchmark::summarize(values);
    if (!m.count) { fprintf(bf, "  %s: n=0 unavailable\n", label); return; }
    fprintf(bf, "  %s: n=%zu min=%.3f median=%.3f p95=%.3f p99=%.3f max=%.3f mean=%.3f ms", label, m.count, m.min, m.median, m.p95, m.p99, m.max, m.mean);
    if (frameRate && m.count) fprintf(bf, " 1%%low=%.2f fps >16.667ms=%zu/%zu", m.low1, m.overBudget, m.count);
    fprintf(bf, "\n");
  };
  auto printWeather = [&](Game* g, const char* phase) {
    const Weather& w = g->benchmarkWeather();
    fprintf(bf, "  weather %s: cover=%.4f base=%.2f precip=%d storm=%d wind_kt=%.3f from=%.3f gust_kt=%.3f turbulence=%.4f hour=%.4f visibility=%.1f\n",
            phase, w.cloudCover, w.cloudBase, w.precip, w.storm ? 1 : 0, w.windSpeed * MS_TO_KT, w.windFrom, w.gust * MS_TO_KT, w.turbulence, g->benchmarkTimeOfDay(), w.visibility);
  };
  if (cl.find("--nobodies") == std::string::npos) {   // (--nobodies: the shader compile timing alone)
    BodyLoad r; host.buildBodies(r);
    if (bf) {
      fprintf(bf, "aircraft bodies: %d ready-built (the aircraft folder), %d from this PC's mesh cache, %d built from scratch",
              r.ready, r.cached, r.built);
      if (r.refused) fprintf(bf, "; %d packaged bodies missing or refused (compile.log says why)", r.refused);
      fprintf(bf, "\n  %.1f s in all; of it %.1f s compiling %d shader programs for them (each aircraft type's own: compiled once"
                  " after an update, then loaded from the cache)", r.secs, r.programSecs + r.helperSecs, r.programs + r.helperPrograms);
      if (r.helpers) fprintf(bf, ": %d of them by %d helper processes side by side in %.1f s, %d here in %.1f s", r.helperPrograms, r.helpers, r.helperSecs, r.programs, r.programSecs);
      fprintf(bf, "\n\n");
      fflush(bf);
    }
  }
  if (bf) {   // (the career types' performance, which the job board plans with: learned at the launch, or by a scene)
    int learned = 0; std::string missing;
    for (int i : kCareerAircraft) { if (Plane::perfLearned(&kAircraft[i])) learned++; else missing += std::string(missing.empty() ? "" : ", ") + kAircraft[i].name; }
    fprintf(bf, "career aircraft performance learned before the scenes: %d of %d%s%s\n\n", learned, kNumAircraft, missing.empty() ? "" : "; not: ", missing.c_str());
  }
  g_ren.stream.sync = true;
  g_ren.syncTiming = false;   // normal async GPU queries; never serialized per-pass debug timing
  bool allValid = true;
  for (const std::string& sc : scenes) {
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    RECT rc; GetClientRect(host.hwnd, &rc);
    if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
    SetWindowTextA(host.hwnd, ("Somnium Volandi - benchmarking " + sc).c_str());
    // a scene's first frame may bake an airframe's meshes (the XR-30's, a minute or more): the bake pumps the window's
    // messages between its batches, so Windows doesn't mark the game "Not Responding" and freeze the screen
    bool bakeNoted = false;
    g_ren.bakeYield = [&host, &sc, &bakeNoted] { if (!bakeNoted) { bakeNoted = true; SetWindowTextA(host.hwnd, ("Somnium Volandi - baking the airframe meshes for " + sc + " (once; cached afterwards)").c_str()); } pump(); };
    Game* g = new Game();
    g->saveDir = host.game.saveDir; g->diskless = true;
    g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex; g->debugScene(sc);
    g_ren.setRenderScale(1.f);   // explicit benchmark invariant; does not change saved settings
    fprintf(bf, "\nScene %s; requested WX=%s\n", sc.c_str(), dev::env("WX") ? dev::env("WX") : "(scene/default)");
    printWeather(g, "preset");
    g_ren.stream.sync = false;
    // warm until the scene is built: at least 40 frames, then until 20 frames in a row bake nothing and stream
    // nothing (a body, the scenery, the terrain shadow), at most 1200
    int warm = 0, quiet = 0; const int bakes0 = g_ren.bakeCount;
    for (; warm < 1200 && quiet < 20 && !host.game.quit; warm++) {
      const int bc = g_ren.bakeCount;
      g->update(1.f / 60.f); g->render(); SwapBuffers(host.hdc); pump();
      const bool busy = g_ren.bakeCount != bc || g_ren.stream.pending > 0 || g_ren.tshPending();
      quiet = warm >= 39 && !busy ? quiet + 1 : 0;
    }
    const int warmBakes = g_ren.bakeCount - bakes0;
    g_ren.bakeYield = nullptr;
    SetWindowTextA(host.hwnd, ("Somnium Volandi - benchmarking " + sc).c_str());
    struct Sample {
      LARGE_INTEGER begin{}, updateEnd{}, presentBegin{}, presentEnd{}, end{};
      uint64_t renderFrame = 0, gpuId = 0, gpuOrigin = 0;
      double gpu = 0; float scale = 1;
      int width = 0, height = 0, pending = 0, terrainPending = 0, bakes = 0, quality = 0;
      bool native1080 = false; int sceneStatus = 0;
    };
    std::vector<Sample> samples; samples.reserve(sampleFrames);
    const uint64_t firstGpuFrame = g_ren.gpuFrameSerial + 1;
    const uint64_t overwritten0 = g_ren.gpuSamplesOverwritten;
    uint64_t seenGpu = g_ren.gpuSample.id;
    const int measuredBakes0 = g_ren.bakeCount;
    const int measuredHits0 = g_shaderCacheHits, measuredMisses0 = g_shaderCacheMisses;   // (programs loaded from the cache, and compiled, during the measured frames: a switch of craft loading one is a hitch)
    const int measuredSceneStatus = g->benchmarkSceneStatus();
    glFinish();
    LARGE_INTEGER f0, f1; QueryPerformanceCounter(&f0);
    g->resetSlowestUpdate();
    for (int i = 0; i < sampleFrames && !host.game.quit; i++) {
      Sample s; QueryPerformanceCounter(&s.begin);
      g->update(1.f / 60.f); QueryPerformanceCounter(&s.updateEnd);
      s.width = g_ren.W; s.height = g_ren.H; s.scale = g_ren.renderScale; s.quality = g_ren.quality;
      g->render(); QueryPerformanceCounter(&s.presentBegin);
      SwapBuffers(host.hdc); QueryPerformanceCounter(&s.presentEnd);
      pump();
      RECT client; GetClientRect(host.hwnd, &client);
      s.native1080 = client.right == 1920 && client.bottom == 1080 && g_ren.W == s.width && g_ren.H == s.height && s.width == 1920 && s.height == 1080 && fabsf(s.scale - 1.f) < 1e-6f;
      s.pending = g_ren.stream.pending; s.terrainPending = g_ren.tshPending() ? 1 : 0; s.bakes = g_ren.bakeCount;
      s.renderFrame = g_ren.gpuFrameSerial; s.sceneStatus = g->benchmarkSceneStatus();
      const auto& gpuSample = g_ren.gpuSample;
      if (gpuSample.id != seenGpu) {
        seenGpu = gpuSample.id;
        if (gpuSample.frame >= firstGpuFrame && gpuSample.frame <= s.renderFrame) {
          s.gpuId = gpuSample.id; s.gpuOrigin = gpuSample.frame; s.gpu = gpuSample.ms;
        }
      }
      QueryPerformanceCounter(&s.end);
      samples.push_back(s);
    }
    glFinish(); QueryPerformanceCounter(&f1);
    const int N = (int)samples.size();
    double ms = N ? qpcMs(f0, f1) / N : 0;
    std::vector<double> wall, present, update, submit, swap, gpuRaw;
    int nativeFrames = 0, streamFrames = 0, stableSceneFrames = 0;
    for (size_t i = 0; i < samples.size(); i++) {
      const Sample& s = samples[i];
      const double fw = qpcMs(s.begin, s.end), u = qpcMs(s.begin, s.updateEnd), r = qpcMs(s.updateEnd, s.presentBegin);
      const double sw = qpcMs(s.presentBegin, s.presentEnd), pump = qpcMs(s.presentEnd, s.end);
      const double pi = i ? qpcMs(samples[i - 1].presentBegin, s.presentBegin) : 0;
      wall.push_back(fw); update.push_back(u); submit.push_back(r); swap.push_back(sw);
      if (i) present.push_back(pi); if (s.gpuId) gpuRaw.push_back(s.gpu);
      stableSceneFrames += s.sceneStatus >= 0 && s.sceneStatus == measuredSceneStatus;
      nativeFrames += s.native1080; streamFrames += s.pending > 0 || s.terrainPending;
      if (csv) {
        fprintf(csv, "%s,%zu,%llu,%.6f,", benchmark::csvField(sc).c_str(), i, (unsigned long long)s.renderFrame, fw);
        if (i) fprintf(csv, "%.6f", pi); else fprintf(csv, "NA");
        fprintf(csv, ",%.6f,%.6f,%.6f,%.6f,", u, r, sw, pump);
        if (s.gpuId) fprintf(csv, "%llu,%llu,%.6f", (unsigned long long)s.gpuId, (unsigned long long)s.gpuOrigin, s.gpu);
        else fprintf(csv, "NA,NA,NA");
        fprintf(csv, ",%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%lld,%lld\n", s.width, s.height, (int)(s.width * s.scale), (int)(s.height * s.scale), s.scale, s.quality, s.pending, s.terrainPending, s.bakes, s.native1080 ? 1 : 0, s.sceneStatus, (long long)s.begin.QuadPart, (long long)s.presentBegin.QuadPart);
      }
    }
    const bool complete = N == sampleFrames && quiet >= 20 && stableSceneFrames == N && !host.game.quit;
    allValid = allValid && complete;
    fprintf(bf, "  capture complete=%s quiet_warmup=%s native1080_frames=%d/%d pending_stream_frames=%d measured_body_bakes=%d stable_scene_frames=%d/%d\n",
            complete ? "yes" : "no", quiet >= 20 ? "yes" : "no", nativeFrames, N, streamFrames, g_ren.bakeCount - measuredBakes0, stableSceneFrames, N);
    fprintf(bf, "  fresh_gpu_samples=%zu/%d query_overwrites=%llu; trailing pending GPU samples not force-read\n", gpuRaw.size(), N, (unsigned long long)(g_ren.gpuSamplesOverwritten - overwritten0));
    printSummary("frame work wall", wall, false); printSummary("app present interval", present, true);
    printSummary("CPU update wall", update, false); printSummary("CPU render/submit wall", submit, false);
    if (bf && !update.empty() && *std::max_element(update.begin(), update.end()) > 20.0)   // (a hitch in the update: what it was spent on)
      fprintf(bf, "  slowest update, part by part (ms): %s\n", g->slowestUpdateText().c_str());
    printSummary("SwapBuffers wall", swap, false); printSummary("GPU renderScene raw delayed", gpuRaw, false);
    printWeather(g, "measured-end");
    if (bf) {
      const float* pm = g_ren.passMs;
      fprintf(bf, "%-22s %6.2f ms/frame (%5.1f fps)   GPU %6.2f ms: world %.2f  displays %.2f  feeds %.2f  objects %.2f  shadow proxy %.2f  lighting %.2f  taa %.2f  sprites %.2f  bloom %.2f  shafts %.2f  composite %.2f\n",
              sc.c_str(), ms, ms > 0 ? 1000.0 / ms : 0, g_ren.gpuMs, pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]);
      fprintf(bf, "%-22s (warmed %d frames until built; %d bodies built during it; measured frames: %d shader programs loaded from the cache, %d compiled)\n", "", warm, warmBakes,
              (int)g_shaderCacheHits - measuredHits0, (int)g_shaderCacheMisses - measuredMisses0);
      fflush(bf);
    }
    g_ren.stream.sync = true;
    delete g;
    if (host.game.quit) break;
  }
  const bool ioFailed = ferror(bf) || (csv && ferror(csv));
  bool closeFailed = fclose(bf) != 0;
  if (csv) closeFailed = fclose(csv) != 0 || closeFailed;
  return allValid && !ioFailed && !closeFailed ? 0 : 2;
}

  // Loading-screen pictures: SomniumVolandi.exe --loadshots renders every airport (and every aircraft in flight) at
  // 1920x1080 with all scenery generated and the anti-aliasing settled, and saves them at 1280x720 (averaged down) in
  // the "loading" folder next to the exe, where the pre-flight loading screen picks them up.
static int loadshotsTool(const std::string& cl, ToolHost& host) {
  if (cl.find("--loadshots") == std::string::npos) return -1;
  if (host.fullscreen()) host.toggleFullscreen();
  RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
  SetWindowPos(host.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
  std::string dir = host.game.assetDir + "\\loading";
  CreateDirectoryA(dir.c_str(), nullptr);
  std::vector<std::pair<std::string, std::string>> jobs;   // scene, file
  for (const Airport& a : g_world.airports) jobs.push_back({std::string("loadshot_") + a.code, a.code});
  for (int i = 0; i < kAircraftCount; i++) jobs.push_back({"loadshot_air_" + std::to_string(i), "air_" + std::to_string(i)});
  int done = 0;
  for (auto& J : jobs) {
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    RECT rc; GetClientRect(host.hwnd, &rc);
    if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
    SetWindowTextA(host.hwnd, ("Somnium Volandi - rendering loading pictures " + std::to_string(++done) + " / " + std::to_string(jobs.size()) + ": " + J.second).c_str());
    g_ren.stream.sync = true;
    Game* g = new Game();
    g->saveDir = host.game.saveDir; g->diskless = true; g->assetDir = host.game.assetDir;
    g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex; g->debugScene(J.first);
    for (int i = 0; i < 4; i++) { g->update(1.f / 30.f); g->render(); }
    for (int i = 0; i < 40; i++) { g->update(1.f / 240.f); g->render(); }   // the TAA settles (barely moving)
    glFinish();
    int w = g_ren.W, h = g_ren.H;
    std::vector<uint8_t> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    SwapBuffers(host.hdc);
    // average down to 1280x720 (or keep a smaller window's size)
    int ow = std::min(1280, w), oh = std::min(720, h);
    std::vector<uint8_t> out((size_t)ow * oh * 3);
    for (int y = 0; y < oh; y++)
      for (int x = 0; x < ow; x++) {
        int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow), y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
        int acc[3] = {0, 0, 0}, n = 0;
        for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const uint8_t* q = &px[((size_t)yy * w + xx) * 3]; acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2]; n++; }
        for (int c = 0; c < 3; c++) out[((size_t)y * ow + x) * 3 + c] = (uint8_t)(acc[c] / n);
      }
    writePNG((dir + "\\" + J.second + ".png").c_str(), ow, oh, out);
    delete g;
  }
  return 0;
}

  // Development captures: SomniumVolandi.exe --shots scene1,scene2,... [--size 1920x1080] renders each debug scene
  // (see Game::debugScene) with all scenery generated up front and saves shots\<scene>.png next to the exe.
static int shotsTool(const std::string& cl, ToolHost& host) {
  const size_t k = cl.find("--shots ");
  if (k == std::string::npos) return -1;
  std::string list = cl.substr(k + 8); list = list.substr(0, list.find(' ')) + ",";
  int sw2 = 0, sh2 = 0; size_t kz = cl.find("--size ");
  if (kz != std::string::npos) sscanf(cl.c_str() + kz + 7, "%dx%d", &sw2, &sh2);
  if (sw2 > 64 && sh2 > 64) {
    if (host.fullscreen()) host.toggleFullscreen();
    RECT wr = {0, 0, sw2, sh2}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(host.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
  }
  char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/")) + "\\shots";
  CreateDirectoryA(dir.c_str(), nullptr);
  { BodyLoad r; host.buildBodies(r); }
  g_ren.stream.sync = true;
  for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
    std::string sc = list.substr(a, b - a);
    if (sc.empty()) continue;
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    RECT rc; GetClientRect(host.hwnd, &rc);
    if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
    SetWindowTextA(host.hwnd, ("Somnium Volandi - rendering " + sc).c_str());
    Game* g = new Game();
    g->saveDir = host.game.saveDir; g->diskless = true;
    g->initHeadless(); g->voicesOff = true; g->iconTex = host.iconTex; g->debugScene(sc);
    for (int i = 0; i < 3; i++) { g->update(1.f / 30.f); g->render(); }
    for (int i = 0; i < 24; i++) g->render();   // TAA settles
    glFinish();
    g_ren.screenshotPNG((dir + "\\" + sc + ".png").c_str());
    SwapBuffers(host.hdc);
    delete g;
  }
  return 0;
}

int runToolMode(const std::string& cl, ToolHost& host) {
  // in the order the command line was read before they were functions: the first that recognises its switch runs
  static int (*const tools[])(const std::string&, ToolHost&) = {analyzeTool, profileTool, soakTool, benchTool, loadshotsTool, shotsTool};
  for (auto tool : tools) { const int code = tool(cl, host); if (code >= 0) return code; }
  return -1;
}
