// Somnium Volandi - the launch's loading bar: the share of the launch's work that is done.
//
// The launch is a list of steps, each a count of things to do: the shader programs (one each, built or loaded from the
// cache), the islands, the career, the renderer, the menu's scenery, the preview stage, each aircraft's body and the
// hive's craft. The bar advances only with completed things, never with time: it stands still while one long thing is
// being done (a large shader program, the first build of a mesh), and it reaches 100% only when everything has. Each
// step's weight on the bar is the time it took at the last launch (launch_pacing.txt beside the shader cache), so the
// bar's pace follows the real work - a step with as many things as another but ten times the time moves it ten times
// as far; a step with no record yet weighs its count. Each step's time is measured for that, and for startup.log.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class LoadPacer {
public:
  struct Group { std::string label; float done = 0, units = 0, skipped = 0; bool active = false; };
  struct Snapshot {
    float overall = 0, stageFraction = 0, stageDone = 0, stageUnits = 0;
    int step = 0, steps = 0, completedSteps = 0;
    double elapsed = 0, idle = 0;
    std::string label;
    std::string taskLabel;
    float taskDone = 0, taskUnits = 0, taskFraction = -1;
    std::vector<Group> groups;
  };
  // the steps' times at the last launch ("key seconds" a line): the weights of this launch's steps of the same keys
  bool loadTimes(const std::string& path) {
    std::lock_guard<std::mutex> lk(m);
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f) return false;
    char key[128]; double secs = 0;
    while (fscanf(f, "%127s %lf", key, &secs) == 2) if (std::isfinite(secs) && secs >= 0) prior[key] = secs;
    fclose(f);
    return !prior.empty();
  }
  // this launch's steps' times, for the next launch's weights (a step without a time kept at its last)
  bool saveTimes(const std::string& path) const {
    std::lock_guard<std::mutex> lk(m);
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "w");
    if (!f) return false;
    std::map<std::string, double> out = prior;
    for (const Step& s : steps) if (s.took >= 0 && s.skipped <= 0.f) out[s.key] = s.took;
    for (const auto& kv : out) fprintf(f, "%s %.3f\n", kv.first.c_str(), kv.second);
    fclose(f);
    return true;
  }
  // A step of `units` completed things (`key` names it for tookOf). Its weight on the bar: the time it took at the
  // last launch where known, else the optional weight given, else one per thing
  int add(const std::string& key, int units = 1, float weight = 0.f, const std::string& label = {}) {
    std::lock_guard<std::mutex> lk(m);
    Step s; s.key = key; s.label = label.empty() ? key : label; s.units = std::max(units, 1);
    const auto it = prior.find(key);
    s.weight = it != prior.end() ? std::max(0.05f, (float)it->second) : std::isfinite(weight) && weight > 0.f ? weight : (float)s.units;
    steps.push_back(s);
    return (int)steps.size() - 1;
  }
  // step i begins: every step before it is done
  void begin(int i) {
    std::lock_guard<std::mutex> lk(m);
    if (i < 0 || i >= (int)steps.size()) return;
    const double now = clock();
    for (int k = 0; k < i && k < (int)steps.size(); k++) close(k, now);
    cur = i; taskLabel.clear(); taskUnits=taskDone=0;
    if (steps[i].t0 < 0) { steps[i].t0 = now; steps[i].changed = now; }
  }
  // everything is done
  void end() { std::lock_guard<std::mutex> lk(m); const double now = clock(); for (int k = 0; k < (int)steps.size(); k++) close(k, now); cur = -1;taskLabel.clear();taskDone=taskUnits=0; }
  // how many of step i's things are done so far (the shader programs built, the islands)
  void setDone(int i, float units) { std::lock_guard<std::mutex> lk(m); if (i >= 0 && i < (int)steps.size() && std::isfinite(units)) advance(steps[i], units); }
  // Explicitly remove unresolved optional work from the launch. Keep it separate
  // from completed work, so a failed cache helper never claims to have built shaders.
  void skipRemaining(int i) {
    std::lock_guard<std::mutex> lk(m);
    if (i >= 0 && i < (int)steps.size()) steps[i].skipped = steps[i].units - steps[i].done;
  }
  float skippedOf(int i) const {
    std::lock_guard<std::mutex> lk(m);
    return i >= 0 && i < (int)steps.size() ? steps[i].skipped : 0.f;
  }
  // the running step's share done (0..1), where it is one thing that knows how far it is (the menu's scenery frames)
  void setSub(float f) { std::lock_guard<std::mutex> lk(m); if (cur >= 0 && std::isfinite(f)) advance(steps[cur], std::clamp(f,0.f,1.f)*steps[cur].units); }
  // The current bounded batch may restart for the next field/state. Its bar is
  // independent of overall completed steps, and does not credit unfinished work.
  void task(const std::string& label, float done, float units) {
    std::lock_guard<std::mutex> lk(m);
    if(!std::isfinite(done)||!std::isfinite(units)||units<=0)return;
    const float d=std::clamp(done,0.f,units);
    if(cur>=0 && (taskLabel!=label || taskDone!=d || taskUnits!=units))steps[cur].changed=clock();
    taskLabel=label;taskDone=d;taskUnits=units;
  }
  void clearTask() {std::lock_guard<std::mutex> lk(m);taskLabel.clear();taskDone=taskUnits=0;}
  // 0..1: the things done out of all the launch's things; it never steps back
  // (the intro's own thread asks every frame)
  float fraction() const {
    std::lock_guard<std::mutex> lk(m);
    return fractionLocked();
  }
  // One consistent read for the independent intro thread. Group bars expose work
  // within each subsystem; a short single-program stall cannot hide other work.
  Snapshot snapshot() const {
    std::lock_guard<std::mutex> lk(m);
    Snapshot out; out.overall=fractionLocked();out.steps=(int)steps.size();out.step=cur>=0?cur+1:out.steps;
    out.taskLabel=taskLabel;out.taskDone=taskDone;out.taskUnits=taskUnits;
    out.taskFraction=taskUnits>0?taskDone/taskUnits:-1.f;
    const double now=clock();
    for(int i=0;i<(int)steps.size();++i) {
      const Step& s=steps[i];const float units=s.units-s.skipped;
      if(s.done>=units)++out.completedSteps;
      auto it=std::find_if(out.groups.begin(),out.groups.end(),[&](const Group& g){return g.label==s.label;});
      if(it==out.groups.end()){out.groups.push_back({s.label});it=out.groups.end()-1;}
      it->done+=s.done;it->units+=units;it->skipped+=s.skipped;it->active|=i==cur || (s.done>0 && s.done<units);
    }
    if(cur>=0) {
      const Step& s=steps[cur];out.label=s.label;out.stageDone=s.done;out.stageUnits=s.units-s.skipped;
      out.stageFraction=out.stageUnits>0?s.done/out.stageUnits:1.f;
      out.elapsed=s.t0>=0?now-s.t0:0;out.idle=s.changed>=0?now-s.changed:0;
    } else {out.label="Ready for departure";out.stageFraction=out.overall>=1.f?1.f:0.f;}
    return out;
  }
private:
  float fractionLocked() const {
    double all = 0, done = 0;
    for (const Step& s : steps) { all += s.weight * (s.units - s.skipped) / s.units; done += s.weight * s.done / s.units; }
    if (all > 0) shown = std::max(shown, done / all);
    return (float)std::min(shown, 1.0);
  }
public:
  // the seconds the steps whose key starts so took (the startup.log summary)
  double tookOf(const std::string& prefix) const {
    std::lock_guard<std::mutex> lk(m);
    double t = 0;
    for (const Step& s : steps) if (s.took >= 0 && s.key.compare(0, prefix.size(), prefix) == 0) t += s.took;
    return t;
  }
private:
  struct Step { std::string key, label; int units = 1; float done = 0.f, skipped = 0.f, weight = 1.f; double t0 = -1, took = -1, changed = -1; };
  std::vector<Step> steps;
  std::map<std::string, double> prior;   // (the last launch's times by key: loadTimes)
  std::string taskLabel;
  float taskDone = 0, taskUnits = 0;
  int cur = -1;
  mutable double shown = 0;
  mutable std::mutex m;
  static double clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  static void advance(Step& s,float units) {
    const float done=std::max(s.done,std::min(units,float(s.units)-s.skipped));
    if(done>s.done){s.done=done;s.changed=clock();}
  }
  void close(int k, double now) {
    Step& s = steps[k];
    s.done = (float)s.units - s.skipped;
    if (s.took < 0) s.took = s.t0 >= 0 ? now - s.t0 : 0.0;
  }
};

// Time-based visual easing follows completed work only. It cannot advance a stalled task
// and never rounds an unfinished launch up to completion.
inline float easeLoadProgress(float shown, float completed, float dt) {
  completed = std::max(0.f, std::min(completed, 1.f));
  if (completed >= 1.f) return 1.f;
  if (!std::isfinite(dt) || dt <= 0.f) return shown;
  return std::min(completed, shown + std::max(0.f, completed - shown) * (1.f - std::exp(-10.f * dt)));
}
