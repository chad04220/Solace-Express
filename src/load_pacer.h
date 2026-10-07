// Solace Express - the launch's loading bar, paced by how long each step took last time.
//
// The launch is a list of steps (the shaders and the islands, the renderer, the menu, each aircraft's mesh), each with
// the seconds it is expected to take: what it took on the last launch that did the same kind of work (built from
// scratch or read from a cache: the two are remembered apart), else the average of the steps of its group that have
// run this time, else a default. The bar shows the expected seconds done over the expected total, so it moves at an
// even pace instead of sitting on the step that does most of the work; a step with its own count of items (the
// shaders) moves by that within its share, else by the time it has run against what it was expected to take.
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
  // 0..1 of the whole launch
  // (the intro's own thread asks every frame: the bar moves on while a step holds the main thread)
  float fraction() const {
    std::lock_guard<std::mutex> lk(m);
    double total = 0, done = 0;
    for (size_t i = 0; i < steps.size(); i++) {
      double e = expectOf((int)i);
      total += e;
      if (steps[i].took >= 0) done += e;
      else if ((int)i == cur) {
        double run = clock() - steps[i].t0;
        double f = sub >= 0.f ? sub : std::min(0.97, run / std::max(e, 1e-3));
        done += e * std::min(1.0, std::max(0.0, f));
      }
    }
    return total > 0 ? (float)std::min(1.0, done / total) : 0.f;
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
