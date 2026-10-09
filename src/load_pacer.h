// Solace Express - the launch's loading bar: the share of the launch's steps that is done.
//
// The launch is a list of steps, each a count of things to do: the shader programs (one each, built or loaded from the
// cache) with the islands beside them, the career, the renderer, the menu, and each aircraft's mesh. The bar shows
// how many of those are done out of all of them, not a guess from how long they took last time: it stands still while
// one long thing is being done (a large shader program, the first build of a mesh), and it reaches 100% only when
// everything has. Each step's time is still measured, for the summary in startup.log.
#pragma once
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

class LoadPacer {
public:
  // a step of `units` things (`key` names it for tookOf)
  int add(const std::string& key, int units = 1) {
    std::lock_guard<std::mutex> lk(m);
    Step s; s.key = key; s.units = std::max(units, 1);
    steps.push_back(s);
    return (int)steps.size() - 1;
  }
  // step i begins: every step before it is done
  void begin(int i) {
    std::lock_guard<std::mutex> lk(m);
    const double now = clock();
    for (int k = 0; k < i && k < (int)steps.size(); k++) close(k, now);
    cur = i; if (steps[i].t0 < 0) steps[i].t0 = now;
  }
  // everything is done
  void end() { std::lock_guard<std::mutex> lk(m); const double now = clock(); for (int k = 0; k < (int)steps.size(); k++) close(k, now); cur = -1; }
  // how many of step i's things are done so far (the shader programs built, the islands)
  void setDone(int i, float units) { std::lock_guard<std::mutex> lk(m); steps[i].done = std::max(steps[i].done, std::min(units, (float)steps[i].units)); }
  // the running step's share done (0..1), where it is one thing that knows how far it is (the menu's scenery frames)
  void setSub(float f) { std::lock_guard<std::mutex> lk(m); if (cur >= 0) steps[cur].done = std::max(steps[cur].done, std::min(std::max(f, 0.f), 1.f) * steps[cur].units); }
  // 0..1: the things done out of all the launch's things; it never steps back
  // (the intro's own thread asks every frame)
  float fraction() const {
    std::lock_guard<std::mutex> lk(m);
    double all = 0, done = 0;
    for (const Step& s : steps) { all += s.units; done += s.done; }
    if (all > 0) shown = std::max(shown, done / all);
    return (float)std::min(shown, 1.0);
  }
  // the seconds the steps whose key starts so took (the startup.log summary)
  double tookOf(const std::string& prefix) const {
    std::lock_guard<std::mutex> lk(m);
    double t = 0;
    for (const Step& s : steps) if (s.took >= 0 && s.key.compare(0, prefix.size(), prefix) == 0) t += s.took;
    return t;
  }
private:
  struct Step { std::string key; int units = 1; float done = 0.f; double t0 = -1, took = -1; };
  std::vector<Step> steps;
  int cur = -1;
  mutable double shown = 0;
  mutable std::mutex m;
  static double clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  void close(int k, double now) {
    Step& s = steps[k];
    s.done = (float)s.units;
    if (s.took < 0) s.took = s.t0 >= 0 ? now - s.t0 : 0.0;
  }
};
