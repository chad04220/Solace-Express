// The islands for a test. Under ctest the tests that fly over them share the ones generated once for the run
// (tests/test_world.cpp, the "world" fixture in CMakeLists.txt; SOLACE_TEST_WORLD names the file) instead of each
// generating them again - under the sanitizers that was about 100 s a test. Run on its own, a test generates them.
#pragma once
#include "../src/world.h"
#include <cstdio>
#include <cstdlib>

static const char* const kTestWorldStamp = "solace-test-run";   // (the file is made new every run: no build to tell apart)

inline void buildTestWorld() {
#ifdef _MSC_VER
#pragma warning(suppress : 4996)   // (getenv: only read here)
#endif
  const char* path = getenv("SOLACE_TEST_WORLD");
  g_world.build(path ? path : "", kTestWorldStamp, false);   // (read, never written: the tests run side by side)
  if (path && *path && !g_world.fromCache) printf("(the run's islands, %s, were missing or rejected: generated them)\n", path);
}
