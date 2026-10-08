// Solace Express - the launch's loading bar, paced by how long each step took last time.
//
// The launch is a list of steps (the shaders and the islands, the renderer, the menu, each aircraft's mesh), each with
// the seconds it is expected to take: what it took on the last launch that did the same kind of work (built from
// scratch or read from a cache: the two are remembered apart), else the average of the steps of its group that have
// run this time, else a default. The bar moves by the seconds still expected (fraction), so it runs at an even pace
// from start to finish instead of sitting on the step that does most of the work.
#pragma once
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class LoadPacer {
public:
  // `file`: where the measured times live (empty: defaults only, nothing saved)
  void load(const std::string& file) {
    std::lock_guard<std::mutex> lk(m);
    path = file; prev.clear();
    if (path.empty()) return;
    if (FILE* f = fopen(path.c_str(), "r")) {
      char key[160]; float s;
      while (fscanf(f, "%159s %f", key, &s) == 2) if (s >= 0.f && s < 3600.f) prev[key] = s;
      fclose(f);
    }
  }
  // a step: `key` names it in the times file, `group` shares an average between steps of the same kind (the aircraft
  // meshes), `fresh` whether it will build from scratch this time (its time is remembered apart from a cache read's)
  int add(const std::string& key, const std::string& group, bool fresh, float defaultSec) {
    std::lock_guard<std::mutex> lk(m);
    Step s; s.key = key + (fresh ? ":b" : ":c"); s.group = group + (fresh ? ":b" : ":c"); s.def = defaultSec;
    auto it = prev.find(s.key);
    s.known = it != prev.end();
    s.expect = s.known ? std::max(it->second, 0.02f) : defaultSec;
    steps.push_back(s);
    return (int)steps.size() - 1;
  }
  // a step begins (ends the one running, if any)
  void begin(int i) {
    std::lock_guard<std::mutex> lk(m);
    double now = clock();
    if (cur >= 0 && cur != i) finish(cur, now);
    cur = i; steps[i].t0 = now; sub = -1.f;
  }
  void end() { std::lock_guard<std::mutex> lk(m); if (cur >= 0) finish(cur, clock()); cur = -1; }
  // the running step turned out to do the other kind of work (it found its cache missing or unreadable): its time is
  // remembered under that kind, and its expectation follows it
  void markFresh(int i, bool fresh) {
    std::lock_guard<std::mutex> lk(m);
    Step& s = steps[i];
    const std::string want = fresh ? ":b" : ":c";
    if (s.key.size() < 2 || s.key.compare(s.key.size() - 2, 2, want) == 0) return;
    s.key.replace(s.key.size() - 2, 2, want); s.group.replace(s.group.size() - 2, 2, want);
    auto it = prev.find(s.key);
    s.known = it != prev.end();
    s.expect = s.known ? std::max(it->second, 0.02f) : (fresh ? std::max(s.def, 10.f) : s.def);
  }
  // the running step's own measure of how far it is (0..1), if it has one
  void setSub(float f) { std::lock_guard<std::mutex> lk(m); sub = f; }
  // 0..1 of the whole launch: the share done moves at the pace the time still expected allows - (1 - done) over the
  // seconds left - so it runs on evenly through every step, a little faster when a step ends early and slower, never
  // still, when one overruns. (Each step's own share, held at 97% while it overran, left the bar standing at one
  // number through the longest steps.) It never steps back
  // (the intro's own thread asks every frame: the bar moves on while a step holds the main thread)
  float fraction() const {
    std::lock_guard<std::mutex> lk(m);
    const double now = clock();
    double left = 0; bool open = false;
    for (size_t i = 0; i < steps.size(); i++) {
      if (steps[i].took >= 0) continue;
      open = true;
      const double e = expectOf((int)i);
      if ((int)i != cur) { left += e; continue; }
      const double run = now - steps[i].t0;
      // (the running step: what its own count says is left, else its expected time less what it has run; an overrun
      // still expects a tenth of its time and a third as long again as it has overrun so far)
      left += std::max(sub >= 0.f ? e * (1.0 - std::min(1.0, (double)sub)) : e - run, e * 0.1 + std::max(run - e, 0.0) * 0.3);
    }
    if (!open) { shown = 1.0; return 1.f; }
    if (tLast >= 0 && left > 1e-3) shown += (1.0 - shown) * std::min(1.0, (now - tLast) / left);
    tLast = now;
    return (float)std::min(shown, 1.0);
  }
  // what each step took, for the next launch (and a summary for the startup log)
  void save() const {
    std::lock_guard<std::mutex> lk(m);
    if (path.empty()) return;
    std::map<std::string, float> out = prev;
    for (const Step& s : steps) if (s.took >= 0) out[s.key] = (float)s.took;
    if (FILE* f = fopen(path.c_str(), "w")) { for (auto& kv : out) fprintf(f, "%s %.3f\n", kv.first.c_str(), kv.second); fclose(f); }
  }
  double tookOf(const std::string& prefix) const {
    std::lock_guard<std::mutex> lk(m);
    double t = 0;
    for (const Step& s : steps) if (s.took >= 0 && s.key.compare(0, prefix.size(), prefix) == 0) t += s.took;
    return t;
  }
private:
  struct Step { std::string key, group; float def = 1.f, expect = 1.f; bool known = false; double t0 = 0, took = -1; };
  std::vector<Step> steps;
  std::map<std::string, float> prev;
  std::string path;
  int cur = -1; float sub = -1.f;
  mutable double shown = 0, tLast = -1;   // (fraction's: the share shown, and when it was last asked)
  mutable std::mutex m;
  static double clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  void finish(int i, double now) { steps[i].took = now - steps[i].t0; }
  // a step's expected seconds: its own from the last launch; else what the finished steps of its group took on
  // average this time (the first aircraft built tells how long the rest will take); else its default
  double expectOf(int i) const {
    const Step& s = steps[i];
    if (s.took >= 0) return s.known ? s.expect : s.took;   // (a step done keeps the share it was given, so the bar never steps back)
    if (s.known) return s.expect;
    double sum = 0; int n = 0;
    for (const Step& o : steps) if (o.took >= 0 && o.group == s.group) { sum += o.took; n++; }
    return n ? sum / n : s.def;
  }
};
