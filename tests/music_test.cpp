// The game's own score (music.h): every mood plays for a minute with no silence, no clipping and nothing that isn't a
// number; it changes mood without a click; every stinger sounds over silence and over a mood; the same seed gives the
// same notes; and it costs well under a tenth of a core
#include "../src/music.h"
#include <cmath>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif
#if defined(__SANITIZE_ADDRESS__)
#define MUSIC_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MUSIC_SANITIZED 1
#endif
#endif
#ifdef MUSIC_SANITIZED
static const double kCpuBudgetMs = 1e9;   // (the sanitizers' instrumentation costs several times the score itself: the cost is printed, not judged)
#else
static const double kCpuBudgetMs = 100.0;   // a tenth of a core at most
#endif

// The CPU time this thread has used (ms): the score's own cost, whatever else the machine is running (a wall clock
// counted the time the test waited for a core too)
static double threadCpuMs() {
#ifdef _WIN32
  FILETIME created, ended, kernel, user;
  GetThreadTimes(GetCurrentThread(), &created, &ended, &kernel, &user);
  ULARGE_INTEGER k, u;
  k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
  return (double)(k.QuadPart + u.QuadPart) / 1e4;
#else
  timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
#endif
}

int main() {
  int fails = 0;
  const float sr = 48000.f;
  const char* names[] = {"none", "menu", "hub", "cruise", "finale"};
  for (int m = 1; m < MOOD_COUNT; m++) {
    MusicGen g(sr, 11); g.setMood(m);
    std::vector<float> b((size_t)sr * 2);
    double sum = 0, ms = 0; float peak = 0; int bad = 0, quietSec = 0;
    for (int sec = 0; sec < 60; sec++) {
      const double t0 = threadCpuMs();
      g.render(b.data(), (int)sr);
      ms += threadCpuMs() - t0;
      double s2 = 0;
      for (float v : b) { if (!std::isfinite(v)) bad++; s2 += v * v; peak = std::max(peak, fabsf(v)); }
      sum += s2;
      if (sec >= 2 && sqrt(s2 / b.size()) < 0.005) quietSec++;   // (after its pads have swelled in)
    }
    const double rms = sqrt(sum / (60.0 * b.size()));
    const bool ok = bad == 0 && peak < 0.86f && rms > 0.03 && quietSec == 0 && ms / 60 < kCpuBudgetMs;
    printf("%-7s rms %.3f, peak %.3f, silent seconds %d, non-finite %d, %.1f ms of CPU a second: %s\n", names[m], rms, peak, quietSec, bad, ms / 60, ok ? "ok" : "FAIL");
    fails += !ok;
  }
  {   // a change of mood: no step between one sample and the next bigger than the music makes by itself - at the switch
      // (where cutting the old mood's notes would click) than the old mood's, after it than either mood's own (the
      // hub's drums come in at once)
    auto maxStep = [](const std::vector<float>& b, size_t from, size_t to) {
      float m = 0; for (size_t i = std::max<size_t>(from, 2); i < to; i++) m = std::max(m, fabsf(b[i] - b[i - 2])); return m;
    };
    MusicGen g(sr, 5); g.setMood(MOOD_MENU);
    std::vector<float> b((size_t)sr * 2 * 4);
    g.render(b.data(), (int)sr * 4);
    const float menuStep = maxStep(b, 0, b.size());
    g.setMood(MOOD_HUB);
    g.render(b.data(), (int)sr * 4);
    const size_t edge = (size_t)(sr * 0.05f) * 2;
    const float atSwitch = maxStep(b, 0, edge), after = maxStep(b, edge, b.size());
    MusicGen h(sr, 5); h.setMood(MOOD_HUB);
    std::vector<float> c((size_t)sr * 2 * 4); h.render(c.data(), (int)sr * 4);
    const float hubStep = maxStep(c, 0, c.size());
    g.setMood(MOOD_NONE);
    g.render(b.data(), (int)sr * 4);
    double tail = 0; for (size_t i = b.size() - (size_t)sr; i < b.size(); i++) tail += b[i] * b[i];
    const bool ok = atSwitch < std::max(0.25f, menuStep * 2.f) && after < std::max(0.25f, std::max(menuStep, hubStep) * 2.f) && sqrt(tail / sr) < 0.01;
    printf("Mood change: largest step at the switch %.3f (menu alone %.3f), after it %.3f (hub alone %.3f); fades to %.4f rms after a stop: %s\n",
           atSwitch, menuStep, after, hubStep, sqrt(tail / sr), ok ? "ok" : "FAIL");
    fails += !ok;
  }
  const char* sn[STING_COUNT] = {"success", "fail", "licence", "achievement", "medal", "applause"};
  for (int k = 0; k < STING_COUNT; k++) {
    MusicGen quiet(sr, 3); quiet.sting(k);
    std::vector<float> b((size_t)sr * 2 * 3); quiet.render(b.data(), (int)sr * 3);
    double s2 = 0; float peak = 0; for (float v : b) { s2 += v * v; peak = std::max(peak, fabsf(v)); }
    MusicGen over(sr, 3); over.setMood(MOOD_HUB);
    std::vector<float> w((size_t)sr * 2 * 3); over.render(w.data(), (int)sr * 3);
    over.sting(k); over.render(w.data(), (int)sr * 3);
    float peak2 = 0; bool fin = true; for (float v : w) { peak2 = std::max(peak2, fabsf(v)); fin &= std::isfinite(v); }
    const bool ok = sqrt(s2 / b.size()) > 0.02 && peak < 0.86f && peak2 < 0.86f && fin;
    printf("Stinger %-11s over silence rms %.3f peak %.3f, over the hub peak %.3f: %s\n", sn[k], sqrt(s2 / b.size()), peak, peak2, ok ? "ok" : "FAIL");
    fails += !ok;
  }
  {   // the same seed, the same music
    MusicGen a(sr, 42), b(sr, 42); a.setMood(MOOD_FINALE); b.setMood(MOOD_FINALE);
    std::vector<float> x((size_t)sr * 2 * 5), y((size_t)sr * 2 * 5);
    a.render(x.data(), (int)sr * 5); b.render(y.data(), (int)sr * 5);
    const bool ok = x == y;
    printf("Same seed, same notes: %s\n", ok ? "ok" : "FAIL");
    fails += !ok;
  }
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
