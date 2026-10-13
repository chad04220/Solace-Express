// Ground depth first's measuring (src/prepass_auto.h): it finds the faster choice, keeps it through noise and a steady
// drift, backs off when nothing changes, drops a trial something else disturbed, and never acts on too few timestamps.
#include "prepass_auto.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <deque>
#include <functional>

static int failures = 0;
static void check(bool ok, const char* what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }

struct Sim {
  PrepassAuto pa;
  struct Pending { int tag; unsigned serial; float ms; };
  std::deque<Pending> queue;   // timestamps come back seven frames late, as from the renderer's ring
  unsigned rng = 12345u;
  float noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffff) / 65535.f * 2.f - 1.f; }
  int onFrames = 0, frames = 0;
  // cost(on, frame) is the world pass's time; keep: the share of timestamps that come back (1 = all)
  void run(int n, const std::function<float(bool, int)>& cost, float noiseFrac = 0.03f, float keep = 1.f,
           const std::function<float(int)>& signature = nullptr, const std::function<bool(int)>& cut = nullptr) {
    for (int i = 0; i < n; i++, frames++) {
      int tag;
      const bool on = pa.frameChoice(signature ? signature(frames) : 1.f, cut ? cut(frames) : false, tag);
      onFrames += on;
      const float ms = cost(on, frames) * (1.f + noiseFrac * noise());
      const bool back = (noise() * 0.5f + 0.5f) < keep;
      queue.push_back({back ? tag : -1, pa.serial, ms});
      if (queue.size() > 7) { const Pending p = queue.front(); queue.pop_front(); pa.sample(p.tag, p.serial, p.ms); }
    }
  }
};

int main() {
  {   // the night runway: the prepass saves 2.5 ms of 12
    Sim s; s.run(PrepassAuto::kWaitMin + PrepassAuto::kTrial + PrepassAuto::kSettle + 2, [](bool on, int) { return on ? 9.5f : 12.f; });
    check(s.pa.on && s.pa.switches == 1, "where it pays, the first trial switches the prepass on");
    s.run(60000, [](bool on, int) { return on ? 9.5f : 12.f; });
    check(s.pa.on && s.pa.switches == 1, "and keeps it on through every later trial");
    check(s.pa.trials < 25, "the wait between trials grows while nothing changes");
  }
  {   // a town from low level: the prepass costs 0.5 ms of 10
    Sim s; s.run(60000, [](bool on, int) { return on ? 10.5f : 10.f; });
    check(!s.pa.on && s.pa.switches == 0, "where it costs, it stays off");
    check(s.onFrames < 60000 / 50, "the trials' own cost is small (under 2% of the frames drawn the other way)");
  }
  {   // a slow card (the software renderer: a second a frame) is measured like any other
    Sim s; s.run(2000, [](bool on, int) { return on ? 1300.f : 1100.f; });
    check(!s.pa.on && s.pa.trials > 0 && s.pa.lastKept > 1000.f && s.pa.lastOther > s.pa.lastKept, "a second-long world pass is still timed");
  }
  {   // nothing to choose between: noise alone never switches it
    Sim s; s.run(200000, [](bool, int) { return 10.f; }, 0.06f);
    check(s.pa.switches == 0, "6% noise with equal costs never switches it");
  }
  {   // a drift (the GPU's clocks, the view) cancels: A, B, B, A. A slow wave of 8 ms: 1.2 ms over a trial at its
      // steepest, falling as often as rising - timed A then B, the dearer choice would win the falling trials
    Sim s; s.run(200000, [](bool on, int f) { return (on ? 10.2f : 10.f) + 8.f * sinf(f * 6.2831853f / 2000.f); });
    check(!s.pa.on && s.pa.switches == 0, "a drift of a millisecond over a trial does not make the dearer choice look cheaper");
  }
  {   // the scene changes: from the runway (pays) to the climb out over the town (costs), and it follows
    Sim s; auto cost = [](bool on, int f) { return f < 30000 ? (on ? 9.5f : 12.f) : (on ? 10.6f : 10.f); };
    s.run(30000, cost); const bool first = s.pa.on;
    s.run(30000, cost, 0.03f, 1.f, nullptr, [](int f) { return f == 30000; });
    check(first && !s.pa.on, "after the view changes it follows: on over the runway, off over the town");
  }
  {   // the render scale changes under every trial (the auto-resolution controller at work): never acted on
    Sim s; s.run(30000, [](bool on, int) { return on ? 9.f : 12.f; }, 0.03f, 1.f,
                 [](int f) { return (float)((f / 20) % 2); });
    check(!s.pa.on && s.pa.switches == 0, "a trial disturbed by a render-scale or foveation change is dropped");
  }
  {   // too few timestamps come back (a driver that drops them): as if nothing changed, and backing off
    Sim s; s.run(60000, [](bool on, int) { return on ? 9.f : 12.f; }, 0.03f, 0.3f);
    check(!s.pa.on && s.pa.trials < 25, "with a third of the timestamps it acts on nothing, and backs off");
  }
  {   // a cancelled trial's late timestamps are not counted in the next
    PrepassAuto pa; int tag;
    for (int i = 0; i < PrepassAuto::kWaitMin; i++) pa.frameChoice(1.f, false, tag);
    const unsigned first = pa.serial;
    pa.frameChoice(1.f, true, tag);   // (a camera cut: the trial dropped)
    pa.sample(1, first, 1.f);
    check(pa.serial != first && pa.n[1] == 0 && pa.frame < 0, "a cut drops the trial and its timestamps");
  }
  printf(failures ? "prepass_auto: %d FAILED\n" : "prepass_auto: all passed\n", failures);
  return failures ? 1 : 0;
}
