#include "dev_switches.h"
#include <cstdlib>

namespace dev {
static bool g_on = true;
void enable(bool on) { g_on = on; }
bool enabled() { return g_on; }
const char* env(const char* name) {
  if (!g_on) return nullptr;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
  return std::getenv(name);
}
}
