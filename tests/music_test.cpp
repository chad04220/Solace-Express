// The game's own score (music.h): every mood plays for a minute with no silence, no clipping and nothing that isn't a
// number; it changes mood without a click; every stinger sounds over silence and over a mood; the same seed gives the
// same notes; and it costs well under a tenth of a core
#include "../src/music.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
  int fails = 0;
  const float sr = 48000.f;
  const char* names[] = {"none", "menu", "hub", "cruise", "finale"};
  for (int m = 1; m < MOOD_COUNT; m++) {
    MusicGen g(sr, 11); g.setMood(m);
    std::vector<float> b((size_t)sr * 2);
    double sum = 0, ms = 0; float peak = 0; int bad = 0, quietSec = 0;
    for (int sec = 0; sec < 60; sec++) {
      const auto t0 = std::chrono::steady_clock::now();
      g.render(b.data(), (int)sr);
      ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      double s2 = 0;
      for (float v : b) { if (!std::isfinite(v)) bad++; s2 += v * v; peak = std::max(peak, fabsf(v)); }
      sum += s2;
      if (sec >= 2 && sqrt(s2 / b.size()) < 0.005) quietSec++;   // (after its pads have swelled in)
    }
    const double rms = sqrt(sum / (60.0 * b.size()));
    const bool ok = bad == 0 && peak < 0.86f && rms > 0.03 && quietSec == 0 && ms / 60 < 100.0;
    printf("%-7s rms %.3f, peak %.3f, silent seconds %d, non-finite %d, %.1f ms of CPU a second: %s\n", names[m], rms, peak, quietSec, bad, ms / 60, ok ? "ok" : "FAIL");
    fails += !ok;
  }
  {   // a change of mood: no step between one sample and the next bigger than the music makes by itself
    MusicGen g(sr, 5); g.setMood(MOOD_MENU);
    std::vector<float> b((size_t)sr * 2 * 4);
    g.render(b.data(), (int)sr * 4);
    float maxStep = 0;
    for (size_t i = 2; i < b.size(); i++) maxStep = std::max(maxStep, fabsf(b[i] - b[i - 2]));
    g.setMood(MOOD_HUB);
    g.render(b.data(), (int)sr * 4);
    float switchStep = 0;
    for (size_t i = 2; i < b.size(); i++) switchStep = std::max(switchStep, fabsf(b[i] - b[i - 2]));
    g.setMood(MOOD_NONE);
    g.render(b.data(), (int)sr * 4);
    double tail = 0; for (size_t i = b.size() - (size_t)sr; i < b.size(); i++) tail += b[i] * b[i];
    const bool ok = switchStep < std::max(0.25f, maxStep * 2.f) && sqrt(tail / sr) < 0.01;
    printf("Mood change: largest step %.3f (menu alone %.3f); fades to %.4f rms after a stop: %s\n", switchStep, maxStep, sqrt(tail / sr), ok ? "ok" : "FAIL");
    fails += !ok;
  }
  const char* sn[] = {"success", "fail", "licence", "achievement", "medal"};
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
