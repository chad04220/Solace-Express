#include "../../src/benchmark_metrics.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
int main() {
  benchmark::GpuSample q;
  assert(!q.publish(false, 10000000, 1) && q.id == 0);
  assert(q.publish(true, 10000000, 7) && q.id == 1 && q.frame == 7 && q.ms == 10);
  assert(!q.publish(false, 50000000, 8) && q.id == 1 && q.frame == 7 && q.ms == 10);
  assert(!q.publish(true, 0, 8) && q.id == 1);
  assert(!q.publish(true, 1000000, 0) && q.id == 1);
  assert(q.publish(true, 10000000, 9) && q.id == 2 && q.frame == 9); // equal timings can still be fresh
  std::vector<double> v(199, 10); v.push_back(100);
  auto s = benchmark::summarize(v);
  assert(s.count == 200 && s.median == 10 && s.p95 == 10 && s.p99 == 10 && s.max == 100 && s.overBudget == 1);
  assert(std::fabs(s.low1 - 1000.0 / 55.0) < 1e-10);
  assert(benchmark::summarize({0, -1, std::numeric_limits<double>::quiet_NaN()}).count == 0);
  std::vector<std::string> scenes; std::string error;
  assert(benchmark::scenes("air,storm,night", scenes, error) && scenes.size() == 3);
  scenes.clear(); assert(benchmark::scenes("air@0.9,800,1,1,12,200,5,0.3,12;night", scenes, error) && scenes.size() == 2);
  scenes.clear(); assert(!benchmark::scenes("air@0.9,800,1,1,12,200,5,0.3,12,night", scenes, error));
  scenes.clear(); assert(!benchmark::scenes("air@0.9,800", scenes, error));
  scenes.clear(); assert(!benchmark::scenes("air@nan,800,1,1,12,200,5,0.3,12", scenes, error));
  scenes.clear(); assert(!benchmark::scenes("", scenes, error));
  assert(benchmark::csvField("a,\"b") == "\"a,\"\"b\"");
  std::puts("benchmark metrics: freshness, invalid samples, percentiles, CSV and scene parsing passed");
}
