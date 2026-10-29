// Somnium Volandi - background jobs joined after their result is no longer wanted: a region build at shutdown, a
// quote the menu moved on from, a prefetch retired by a new heading. Each used to be `try { job.get(); } catch (...) {}`:
// a job that failed left no trace (docs/CODE_REWORK_PLAN.md 2.9). drain() joins the future, treats the generation
// cancellation as the nothing it is, and logs anything else where the platform says (startup.log on Windows; the
// tests' and tools' stderr otherwise).
#pragma once
#include "common.h"
#include <cstdio>
#include <future>
#include <string>

namespace jobs {
inline void (*g_sink)(const std::string&) = nullptr;   // where a drained job's failure is written (setLog)
inline void setLog(void (*sink)(const std::string&)) { g_sink = sink; }
inline void log(const std::string& line) { if (g_sink) g_sink(line); else fprintf(stderr, "job: %s\n", line.c_str()); }
// joins `job` if it holds one, its value dropped: GenerationCancelled says nothing, anything else is logged as `what`
template <class T> void drain(std::future<T>& job, const char* what) {
  if (!job.valid()) return;
  try { (void)job.get(); }
  catch (const GenerationCancelled&) {}
  catch (const std::exception& e) { log(std::string(what) + ": " + e.what()); }
  catch (...) { log(std::string(what) + ": failed"); }
}
}   // namespace jobs
