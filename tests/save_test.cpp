// Career save robustness: damaged or incomplete saves are rejected without touching the live career, a failed write
// reports false and leaves the previous save intact, and a good save round-trips.
#include "../src/career.h"
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <direct.h>
#define rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { printf("  !! %s\n", what); fails++; } }
static void writeFile(const std::string& p, const char* text) { FILE* f = fopen(p.c_str(), "w"); fputs(text, f); fclose(f); }
static std::string readFile(const std::string& p) { std::string s; FILE* f = fopen(p.c_str(), "r"); if (!f) return s; char b[512]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
int main() {
  g_world.build(); buildStory();
  const std::string p = "save_test_career.sav";
  Career live; live.newGame(); live.money = -1234; live.storyIndex = 5; live.license = LIC_PPL; live.fleet.push_back({1, 2, 50.f, 0.f});
  check(live.save(p), "good save writes");
  Career r; r.newGame();
  check(r.load(p), "good save loads");
  check(r.money == -1234 && r.storyIndex == 5 && r.license == LIC_PPL && r.fleet.size() == 1 && r.fleet[0].location == 2, "round trip keeps the values (including a negative balance)");

  // each of these must be rejected and leave the career as it was
  const char* bad[] = {
    "solace_save 1\nlicense 1\nplane kestrel 999 70\n",                                   // fleet airport out of range
    "solace_save 1\n",                                                                    // header only
    "solace_save 2\nmoney 5\nlicense 0\nlocation 0\nstory 0\n",                           // unknown version
    "solace_save 1\nmoney 5\nlicense 9\nlocation 0\nstory 0\n",                           // bad license
    "solace_save 1\nmoney 5\nlicense 0\nlocation -3\nstory 0\n",                          // bad location
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 999\n",                         // story past the end
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nhours nan\n",                // non-finite
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nplane nosuchplane 0 10\n",   // unknown aircraft
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nplane kestrel 0\n",          // truncated line
    "solace_save 1\nmoney 5\nlicense 0\nlocation\n",                                      // truncated value
    "garbage",
  };
  int i = 0;
  for (const char* b : bad) {
    writeFile("save_test_bad.sav", b);
    Career c = r;
    bool ok = c.load("save_test_bad.sav");
    char msg[64]; snprintf(msg, sizeof msg, "damaged save %d rejected", i++);
    check(!ok && c.money == r.money && c.storyIndex == r.storyIndex && c.fleet.size() == r.fleet.size(), msg);
  }
  // old saves without the end marker still load
  writeFile("save_test_old.sav", "airxpress_save 1\nmoney 900\nlicense 1\nrep 3\nlocation 1\nstory 4\nplane kestrel 1 20\n");
  { Career c; c.newGame(); check(c.load("save_test_old.sav") && c.money == 900 && c.fleet.size() == 1, "legacy save loads"); }

  // a failed write keeps the previous save
  std::string before = readFile(p);
  Career big = r; big.money = 42;
  // (not /dev/full: an atomic writer would rename its temp file over the device node)
  std::string blocked = p + ".tmp";
#ifdef _WIN32
  _mkdir(blocked.c_str());
#else
  mkdir(blocked.c_str(), 0755);
#endif
  check(!big.save(p), "write that can't create its temp file reports failure");
  rmdir(blocked.c_str());
  check(!big.save("no_such_dir/x/career.sav"), "write to a missing folder reports failure");
  check(readFile(p) == before, "previous save untouched");
  // a second save keeps the first as .bak
  check(big.save(p), "second save");
  check(readFile(p + ".bak") == before, "previous save kept as .bak");
  remove(p.c_str()); remove((p + ".bak").c_str()); remove("save_test_bad.sav"); remove("save_test_old.sav");
  printf("save_test: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
  return fails ? 1 : 0;
}
