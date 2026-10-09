// Solace Express - CPU-only benchmark helpers; no driver calls or timing side effects.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace benchmark {
struct GpuSample {
  uint64_t id = 0, frame = 0;
  double ms = 0;
  // An unavailable query must not repeat the previous measurement as a fresh sample.
  bool publish(bool available, uint64_t ns, uint64_t originatingFrame) {
    if (!available || !ns || !originatingFrame) return false;
    ++id; frame = originatingFrame; ms = (double)ns * 1e-6;
    return true;
  }
};
struct Summary {
  size_t count = 0, overBudget = 0;
  double min = 0, median = 0, p95 = 0, p99 = 0, max = 0, mean = 0, low1 = 0;
};
inline Summary summarize(std::vector<double> values) {
  Summary s;
  values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v) || v <= 0; }), values.end());
  if (values.empty()) return s;
  std::sort(values.begin(), values.end()); s.count = values.size();
  auto p = [&](double q) { return values[(size_t)std::ceil(q * values.size()) - 1]; };
  s.min = values.front(); s.median = p(.5); s.p95 = p(.95); s.p99 = p(.99); s.max = values.back();
  double sum = 0; for (double v : values) { sum += v; if (v > 1000.0 / 60.0) ++s.overBudget; }
  s.mean = sum / values.size();
  size_t slow = (size_t)std::ceil(values.size() * .01); sum = 0;
  for (size_t i = values.size() - slow; i < values.size(); ++i) sum += values[i];
  s.low1 = 1000.0 * slow / sum;
  return s;
}
// Legacy comma-separated lists remain valid. Weather uses commas, so lists containing
// @WX use semicolons between scenes. Require all nine weather values, never silently
// turn weather numbers into extra scene names. Plain semicolon lists are also accepted.
inline bool scenes(const std::string& list, std::vector<std::string>& out, std::string& error) {
  const char delimiter = list.find('@') != std::string::npos || list.find(';') != std::string::npos ? ';' : ',';
  for (size_t a = 0; a < list.size();) {
    size_t b = list.find(delimiter, a); if (b == std::string::npos) b = list.size();
    std::string scene = list.substr(a, b - a); a = b + 1;
    if (scene.empty()) continue;
    size_t at = scene.find('@');
    if (at != std::string::npos) {
      float v[9]; int used = 0;
      const std::string wx = scene.substr(at + 1);
      int n = std::sscanf(wx.c_str(), "%f,%f,%f,%f,%f,%f,%f,%f,%f%n", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &used);
      bool finite = n == 9; if (finite) for (float f : v) finite = finite && std::isfinite(f);
      if (!at || !finite || (size_t)used != wx.size()) {
        error = "Use scene@cover,base,precip,storm,wind,from,gust,turbulence,hour with all nine finite values; separate scenes with semicolons.";
        return false;
      }
    }
    out.push_back(scene);
  }
  if (out.empty()) { error = "No benchmark scenes specified."; return false; }
  return true;
}
inline std::string csvField(const std::string& value) {
  std::string result = "\"";
  for (char c : value) { result += c; if (c == '"') result += '"'; }
  return result + '"';
}
}  // namespace benchmark
