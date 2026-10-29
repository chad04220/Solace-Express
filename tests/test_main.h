// What every test in this folder writes for itself: a check that prints its label and counts failures, the summary
// line, and - for the tests that judge how long something took - a time budget that is measured against this machine
// rather than assumed. (Most tests still carry their own check lambda; new tests and the ones touched use this.)
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

struct TestRun {
  const char* name;   // printed before each check and in the summary line
  int failed = 0, checks = 0;
  explicit TestRun(const char* n) : name(n) { setvbuf(stdout, nullptr, _IONBF, 0); }
  bool check(bool pass, const std::string& label) {
    printf("%s: %s: %s\n", name, label.c_str(), pass ? "ok" : "FAIL"); checks++; failed += !pass; return pass;
  }
  void note(const std::string& line) { printf("%s: %s\n", name, line.c_str()); }
  // the exit code, after the summary line every test ends with
  int done() { printf("%s: %s (%d checks, %d failed)\n", name, failed ? "FAILED" : "passed", checks, failed); return failed ? 1 : 0; }
};

// A wall-clock budget judged against this machine, not a number chosen on another: the caller measures a reference
// piece of work first (a single-threaded baseline, a hundred of the things it then times a thousand of) and the budget
// is that times a factor. Under FLIGHT_QUICK (CI's sanitizer shards: four tests to a shared runner, every allocation
// checked) wall-clock judgements are reported, never failed - that run looks for memory errors, not for speed.
struct TimeBudget {
  using clock = std::chrono::steady_clock;
  clock::time_point t0 = clock::now();
  double seconds() const { return std::chrono::duration<double>(clock::now() - t0).count(); }
  static bool judged() { return getenv("FLIGHT_QUICK") == nullptr; }
  // true when `took` is within `factor` of `reference` seconds (or the run is one that doesn't judge time)
  static bool within(TestRun& t, double took, double reference, double factor, const std::string& label) {
    const bool ok = took <= reference * factor;
    if (!judged()) { t.note(label + ": " + std::to_string(took) + " s against " + std::to_string(reference * factor) + " s, not judged (FLIGHT_QUICK)"); return true; }
    return t.check(ok, label + ": " + std::to_string(took) + " s within " + std::to_string(reference * factor) + " s");
  }
};
