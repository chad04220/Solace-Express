#pragma once
// Ground depth first (the terrain's depth prepass, renderer.h), kept on or off by measurement on the player's card. It
// pays where the hills hide the scenery: v3.49.3's diagnostics on an RTX 3070 Laptop, 2.5 ms saved on the night
// runway (20% of the frame). Elsewhere it costs the ground's vertices twice, 0.2 to 1 ms. Which it is depends on the
// view, so every so often a trial draws the world with the other choice between two spells of the one kept (A, B, B,
// A: a steady drift cancels), timed by the world pass's GPU timestamps, and keeps the faster. A trial that changes
// nothing doubles the wait to the next, up to a minute or so; a switch, or a new view, brings the next one soon.
// No GL here: the renderer tags each frame's timestamps with frameChoice's tag and hands them back to sample.
#include <algorithm>

struct PrepassAuto {
  static constexpr int kSpell = 12;                     // frames of each A spell; the B spell is two of them
  static constexpr int kTrial = 4 * kSpell;             // the trial's frames: A, B, B, A
  static constexpr int kSettle = 12;                    // frames after it for its timestamps to come back
  static constexpr int kWaitMin = 300, kWaitMax = 4800; // frames between trials (5 s to 80 s at 60 fps)
  bool on = false;                // the choice kept
  int wait = kWaitMin;            // the wait after the next trial that changes nothing (doubled each time)
  int countdown = kWaitMin;       // frames to the next trial
  int frame = -1;                 // the frame within the trial, -1 between trials
  unsigned serial = 1;            // the trial a frame's timestamps belong to (a cancelled trial's are dropped)
  float sig = 0.f;                // what else sets the world pass's time (render scale, foveation): the trial's
  double sum[2] = {}; int n[2] = {};   // world-pass ms: [0] the choice kept, [1] the other
  float lastKept = 0.f, lastOther = 0.f;   // the last finished trial's means (for the diagnostics)
  int trials = 0, switches = 0;

  // The choice for the frame about to be drawn. tag: 0 or 1 for a frame of the trial (its timestamps go to sample
  // with this tag and serial), -1 otherwise. signature: anything else that changes the world pass's time; cut: the view
  // jumped. Either during a trial cancels it, and the next comes soon.
  bool frameChoice(float signature, bool cut, int& tag) {
    tag = -1;
    if (cut) { if (frame >= 0) cancel(); countdown = std::min(countdown, kSpell); wait = kWaitMin; }
    else if (frame >= 0 && signature != sig) cancel();
    if (frame < 0) {
      if (--countdown > 0) return on;
      frame = 0; sig = signature; sum[0] = sum[1] = 0.0; n[0] = n[1] = 0;
    }
    const int f = frame++;
    if (f >= kTrial + kSettle) { finish(); return on; }
    if (f >= kTrial) return on;   // (settling: the choice kept, not counted)
    tag = (f >= kSpell && f < 3 * kSpell) ? 1 : 0;
    return tag ? !on : on;
  }
  void sample(int tag, unsigned trial, float worldMs) {
    if (tag < 0 || tag > 1 || trial != serial || frame < 0 || !(worldMs > 0.f) || worldMs > 60000.f) return;   // (a minute: the software renderer's world pass is a second)
    sum[tag] += worldMs; n[tag]++;
  }
  void cancel() { frame = -1; serial++; countdown = kSpell; }
  void finish() {
    frame = -1; serial++; trials++;
    bool better = false;
    if (n[0] >= kSpell && n[1] >= kSpell) {   // (too few timestamps came back: as if nothing changed)
      const double kept = sum[0] / n[0], other = sum[1] / n[1];
      lastKept = (float)kept; lastOther = (float)other;
      // the other must win clearly: by 3% and a tenth of a millisecond (the frame's own noise is a few percent)
      better = other < kept - std::max(0.1, 0.03 * kept);
    }
    if (better) { on = !on; switches++; wait = kWaitMin; countdown = kWaitMin; }
    else { countdown = wait; wait = std::min(wait * 2, kWaitMax); }
  }
};
