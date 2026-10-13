#pragma once
// Auto resolution's governor (Settings, Render resolution: Auto). It holds the frame-rate target on the GPU by stepping
// the picture down only as far as that pays. Two knobs: foveated shading (the screen's edges shaded coarser, where the
// card can: renderer.h foveation) and the render scale (the whole frame, which TAA then upscales).
//
// Every half second it compares the GPU's frame time, averaged, with the budget. Over it, one step down: the edges first,
// then the knobs take turns. The next half second checks what that step bought. A step that saved next to nothing is
// taken back, and that knob is left alone for a while: where a frame's cost isn't in its pixels - a city's tens of
// thousands of buildings, the cockpit's displays - coarser shading or fewer pixels only spoil the picture. (v3.49.4's
// diagnostics, RTX 3070 Laptop at High: hovering over Solace Capital at night, 67% resolution saved 2.4 ms of 17 and
// foveation at its strongest 0.3; the old controller drove both to the floor there for nothing.) Under the budget with
// room for what the newest step saved, that step is undone. It starts at the full picture. No GL here.
#include <algorithm>
#include <vector>

struct QualityGovernor {
  static constexpr float kWindow = 0.5f;                   // seconds of GPU samples per decision
  static constexpr float kFoveaStep = 0.5f;                // two steps from none to the strongest
  static constexpr float kScaleStep = 0.1f, kScaleMin = 0.5f;
  static constexpr float kHold = 20.f;                     // seconds a knob that bought nothing is left alone (doubling
                                                           // each time it buys nothing again, to 160 s)
  enum Knob { FOVEA = 0, SCALE = 1 };
  struct Move { int knob; float gain; };                   // gain: GPU ms the step was measured to save (< 0: not yet)

  bool vrs = false;              // the card can do foveated shading (and the player wants it)
  float foveation = 0.f, scale = 1.f;
  std::vector<Move> moves;       // the steps down in force, newest last
  float now = 0.f, t = 0.f, sum = 0.f; int n = 0;
  bool checking = false; float before = 0.f;
  float futileUntil[2] = {0.f, 0.f}, futileAt[2] = {0.f, 0.f}; int futileRuns[2] = {0, 0};
  int stepsDown = 0, stepsUp = 0, stepsUndone = 0;   // (for the tests and the diagnostics)

  void reset() { const bool v = vrs; *this = QualityGovernor(); vrs = v; }

  // dt: this frame's time; gpuMs: the latest GPU frame time (<= 0: none yet); budget: the GPU ms the target allows
  void frame(float dt, float gpuMs, float budget) {
    now += dt; t += dt;
    if (!vrs && foveation > 0.f) { foveation = 0.f; drop(FOVEA); }
    if (gpuMs > 0.f) { sum += gpuMs; n++; }
    if (t < kWindow || n == 0) return;
    const float avg = sum / n; t = 0.f; sum = 0.f; n = 0;
    if (checking) {   // what the newest step bought
      checking = false;
      Move& m = moves.back();
      m.gain = before - avg;
      if (m.gain < std::max(0.12f, 0.015f * before)) {   // (next to nothing: back it goes, and that knob rests)
        futileUntil[m.knob] = now + kHold * float(1 << std::min(futileRuns[m.knob], 3)); futileAt[m.knob] = before;
        futileRuns[m.knob]++;
        undo(m.knob); moves.pop_back(); stepsUndone++;
      } else futileRuns[m.knob] = 0;
      return;
    }
    // a knob that bought nothing may pay again once the scene is much heavier than it was then
    for (int k = 0; k < 2; k++) if (futileUntil[k] > now && avg > futileAt[k] * 1.25f) { futileUntil[k] = 0.f; futileRuns[k] = 0; }
    if (avg > budget) {
      const int k = pick();
      if (k >= 0) { apply(k); moves.push_back({k, -1.f}); checking = true; before = avg; stepsDown++; }
    } else if (!moves.empty()) {   // room for the newest step's cost again (with a tenth to spare): undo it
      const Move& m = moves.back();
      const float cost = m.gain > 0.f ? m.gain * 1.1f : budget * 0.15f;
      if (avg + cost < budget * 0.95f) { undo(m.knob); moves.pop_back(); stepsUp++; }
    }
  }

private:
  bool canStep(int k) const {
    if (futileUntil[k] > now) return false;
    return k == FOVEA ? vrs && foveation < 1.f - 1e-4f : scale > kScaleMin + 1e-4f;
  }
  int pick() const {   // the edges first; then whichever knob has been stepped less
    int nf = 0, ns = 0;
    for (const Move& m : moves) (m.knob == FOVEA ? nf : ns)++;
    const bool f = canStep(FOVEA), s = canStep(SCALE);
    if (f && (!s || nf <= ns)) return FOVEA;
    return s ? SCALE : -1;
  }
  void apply(int k) {
    if (k == FOVEA) foveation = std::min(1.f, foveation + kFoveaStep);
    else scale = std::max(kScaleMin, scale - kScaleStep);
  }
  void undo(int k) {
    if (k == FOVEA) foveation = std::max(0.f, foveation - kFoveaStep);
    else scale = std::min(1.f, scale + kScaleStep);
  }
  void drop(int k) {   // (foveation became unavailable: forget its steps)
    moves.erase(std::remove_if(moves.begin(), moves.end(), [k](const Move& m) { return m.knob == k; }), moves.end());
    checking = false;
  }
};
