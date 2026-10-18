// Crash reports, the portable half: the marker a crash leaves is said once and only for a report that exists, and the
// folder keeps the newest reports, each with all its files
#include "../src/crash.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs = std::filesystem;
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { printf("  !! %s\n", what); fails++; } }
static void touch(const fs::path& p, const std::string& text = "x") { std::ofstream(p) << text; }
int main() {
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / "somnium_crash_report_test";
  fs::remove_all(dir, ec); fs::create_directories(dir, ec);
  const std::string d = dir.string();
  check(crashTakePending(d).empty(), "no marker: nothing to say");
  {   // a crash's report and its marker: said once
    const fs::path txt = dir / "crash-20261018-101500.txt";
    touch(txt); touch(dir / "crash-20261018-101500.dmp"); touch(dir / "crash-20261018-101500-startup.log");
    touch(dir / "last_crash.txt", txt.string() + "\r\n");
    check(crashTakePending(d) == txt.string(), "the marker names the report");
    check(!fs::exists(dir / "last_crash.txt"), "the marker is cleared");
    check(crashTakePending(d).empty(), "the report is said once");
  }
  {   // a marker whose report was deleted since: not pointed at
    touch(dir / "last_crash.txt", (dir / "crash-19990101-000000.txt").string());
    check(crashTakePending(d).empty() && !fs::exists(dir / "last_crash.txt"), "a marker for a missing report is dropped");
  }
  {   // fourteen reports: the ten newest are kept, all three files of each; other files are left alone
    for (int i = 0; i < 13; i++) {
      char stem[32]; snprintf(stem, sizeof stem, "crash-20261019-1200%02d", i);
      touch(dir / (std::string(stem) + ".txt")); touch(dir / (std::string(stem) + ".dmp")); touch(dir / (std::string(stem) + "-startup.log"));
    }
    touch(dir / "notes.txt");
    crashPrune(d, 10);
    int txt = 0, dmp = 0, log = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
      const std::string n = e.path().filename().string();
      if (n.compare(0, 6, "crash-") != 0) continue;
      txt += n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0;
      dmp += n.size() > 4 && n.compare(n.size() - 4, 4, ".dmp") == 0;
      log += n.size() > 12 && n.compare(n.size() - 12, 12, "-startup.log") == 0;
    }
    check(txt == 10 && dmp == 10 && log == 10, "ten reports kept, each whole");
    check(!fs::exists(dir / "crash-20261018-101500.txt") && !fs::exists(dir / "crash-20261019-120002.dmp"), "the oldest went first");
    check(fs::exists(dir / "crash-20261019-120012.txt") && fs::exists(dir / "crash-20261019-120003-startup.log"), "the newest stay");
    check(fs::exists(dir / "notes.txt"), "files that aren't reports are left alone");
  }
  crashPrune((dir / "missing").string());   // (no folder yet: nothing to do, no throw)
  fs::remove_all(dir, ec);
  printf("Crash reports: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
