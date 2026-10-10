// The islands, generated once for a test run (ctest's "world" fixture: CMakeLists.txt runs this before the tests that
// need it) and written where tests/test_world.h reads them. Always generated afresh, so a run never reads an older
// build's islands; the file is checked to read back before any test relies on it.
#include "test_world.h"
#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: test_world <file>\n"); return 2; }
  remove(argv[1]);
  g_world.build(argv[1], kTestWorldStamp);
  World back;
  const bool ok = !g_world.fromCache && back.loadCache(argv[1], kTestWorldStamp) && back.hm == g_world.hm && back.mask == g_world.mask;
  printf("the run's islands, generated and written to %s: %s\n", argv[1], ok ? "ok" : "FAIL (it doesn't read back)");
  return ok ? 0 : 1;
}
