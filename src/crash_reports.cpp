// The portable half of the crash reports: the marker the last session left, and keeping the folder bounded
#include "crash.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace fs = std::filesystem;

std::string crashTakePending(const std::string& crashDir) {
  std::error_code ec;
  const fs::path marker = fs::path(crashDir) / "last_crash.txt";
  std::ifstream f(marker);
  if (!f) return std::string();
  std::string report;
  std::getline(f, report);
  f.close();
  fs::remove(marker, ec);
  while (!report.empty() && (report.back() == '\r' || report.back() == '\n' || report.back() == ' ')) report.pop_back();
  return fs::exists(fs::path(report), ec) ? report : std::string();   // (a report deleted since is not one to point at)
}

void crashPrune(const std::string& crashDir, int keep) {
  std::error_code ec;
  std::map<std::string, std::vector<fs::path>> reports;   // stem -> its files; the stems sort oldest first
  for (const auto& e : fs::directory_iterator(crashDir, ec)) {
    const std::string name = e.path().filename().string();
    if (name.compare(0, 6, "crash-") != 0 || name.size() < 21) continue;
    reports[name.substr(0, 21)].push_back(e.path());   // "crash-YYYYMMDD-HHMMSS"
  }
  int extra = (int)reports.size() - std::max(0, keep);
  for (auto it = reports.begin(); extra > 0 && it != reports.end(); ++it, --extra)
    for (const fs::path& p : it->second) fs::remove(p, ec);
}
