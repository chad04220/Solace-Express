// Somnium Volandi - the real Earth's fine elevation fetched as it is wanted (earth_fetch.h)
#include "earth_fetch.h"
#include "geo.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <direct.h>
#endif

namespace earthFetch {
namespace {
std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  return (a.back() == '/' || a.back() == '\\') ? a + b : a + "/" + b;
}
bool exists(const std::string& path) { struct stat st; return stat(path.c_str(), &st) == 0 && st.st_size > 0; }
void makeDirs(const std::string& dir) {
  for (size_t i = 1; i <= dir.size(); i++) {
    if (i < dir.size() && dir[i] != '/' && dir[i] != '\\') continue;
    const std::string part = dir.substr(0, i);
#ifdef _WIN32
    _mkdir(part.c_str());
#else
    mkdir(part.c_str(), 0755);
#endif
  }
}
uint64_t key(int lat, int lon) { return (uint64_t)(uint32_t)(lat + 90) << 32 | (uint32_t)(lon + 180); }

#ifdef _WIN32
std::wstring wide(const std::string& s) {
  std::wstring w(s.size(), L'\0');
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
  w.resize(n > 0 ? n : 0);
  return w;
}
#endif
} // namespace

std::string glo90Url(int lat, int lon) {
  const std::string name = earth::glo90Name(lat, lon);
  return "https://copernicus-dem-90m.s3.amazonaws.com/" + name + "/" + name + ".tif";
}

bool download(const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>* cancel) {
  auto fail = [&](const std::string& why) { if (error) *error = why; std::remove((path + ".part").c_str()); return false; };
  const std::string part = path + ".part";
#ifdef _WIN32
  const std::wstring wurl = wide(url);
  URL_COMPONENTS u = {}; u.dwStructSize = sizeof u;
  wchar_t host[256] = {}, rest[2048] = {};
  u.lpszHostName = host; u.dwHostNameLength = 256; u.lpszUrlPath = rest; u.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u)) return fail("not an address: " + url);
  HINTERNET session = WinHttpOpen(L"SomniumVolandi/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return fail("no network session");
  WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
  HINTERNET connect = WinHttpConnect(session, host, u.nPort, 0);
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", rest, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
  auto close = [&] { if (request) WinHttpCloseHandle(request); if (connect) WinHttpCloseHandle(connect); WinHttpCloseHandle(session); };
  if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) { close(); return fail("no answer from " + url); }
  DWORD status = 0, size = sizeof status;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) { close(); return fail("HTTP " + std::to_string(status) + " from " + url); }
  FILE* f = fopen(part.c_str(), "wb");
  if (!f) { close(); return fail("can't write " + part); }
  std::vector<char> buf(1 << 16);
  bool ok = true;
  for (;;) {
    if (cancel && cancel->load()) { ok = false; break; }
    DWORD got = 0;
    if (!WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &got)) { ok = false; break; }
    if (got == 0) break;
    if (fwrite(buf.data(), 1, got, f) != got) { ok = false; break; }
  }
  fclose(f); close();
  if (!ok) return fail(cancel && cancel->load() ? "cancelled" : "the download broke off");
  std::remove(path.c_str());
  if (!MoveFileA(part.c_str(), path.c_str())) return fail("can't place " + path);
  return true;
#else
  // (curl, as the shell finds it: the paths and the address are the game's own, quoted)
  auto quote = [](const std::string& s) { std::string q = "'"; for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; } return q + "'"; };
  if (cancel && cancel->load()) return fail("cancelled");
  const std::string cmd = "curl -sfL --max-time 180 -o " + quote(part) + " " + quote(url) + " 2>/dev/null";
  if (std::system(cmd.c_str()) != 0 || !exists(part)) return fail("no download from " + url);
  if (std::rename(part.c_str(), path.c_str()) != 0) return fail("can't place " + path);
  return true;
#endif
}

Glo90Fetcher::Glo90Fetcher(std::string dir, Transfer transfer) : dir_(std::move(dir)), transfer_(std::move(transfer)) {
  makeDirs(dir_);
  worker_ = std::thread([this] { run(); });
}
Glo90Fetcher::~Glo90Fetcher() {
  { std::lock_guard<std::mutex> g(lock_); stop_ = true; queue_.clear(); }
  cancel_ = true;
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}
void Glo90Fetcher::want(const earth::Pack& pack, const std::vector<std::pair<int, int>>& tiles) {
  std::deque<std::pair<int, int>> next;
  for (const auto& t : tiles) {
    if (!pack.glo90Listed(t.first, t.second) || !pack.glo90File(t.first, t.second).empty()) continue;
    if (exists(join(dir_, earth::glo90Name(t.first, t.second) + ".tif"))) continue;
    next.push_back(t);
  }
  std::lock_guard<std::mutex> g(lock_);
  queue_.clear();
  for (const auto& t : next) if (std::find(failedTiles_.begin(), failedTiles_.end(), key(t.first, t.second)) == failedTiles_.end()) queue_.push_back(t);
  wake_.notify_all();
}
int Glo90Fetcher::pending() const { std::lock_guard<std::mutex> g(lock_); return (int)queue_.size() + (busy_ ? 1 : 0); }
std::string Glo90Fetcher::lastError() const { std::lock_guard<std::mutex> g(lock_); return lastError_; }
bool Glo90Fetcher::settle(double seconds) {
  std::unique_lock<std::mutex> g(lock_);
  return idle_.wait_for(g, std::chrono::duration<double>(seconds), [&] { return queue_.empty() && !busy_; });
}
void Glo90Fetcher::run() {
  for (;;) {
    std::pair<int, int> t;
    {
      std::unique_lock<std::mutex> g(lock_);
      idle_.notify_all();
      wake_.wait(g, [&] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      t = queue_.front(); queue_.pop_front(); busy_ = true;
    }
    const std::string path = join(dir_, earth::glo90Name(t.first, t.second) + ".tif");
    std::string why;
    const bool ok = exists(path) || transfer_(glo90Url(t.first, t.second), path, &why, &cancel_);
    std::lock_guard<std::mutex> g(lock_);
    busy_ = false;
    if (ok) fetched_++;
    else { failed_++; lastError_ = why; failedTiles_.push_back(key(t.first, t.second)); }
  }
}

std::vector<std::pair<int, int>> tilesAround(double lat, double lon, double radiusMetres) {
  const double dLat = radiusMetres / 111320.0, dLon = dLat / std::max(0.05, std::cos(lat * geo::kDegrees));
  std::vector<std::pair<std::pair<int, int>, double>> found;
  for (int a = (int)std::floor(lat - dLat); a <= (int)std::floor(lat + dLat); a++) {
    if (a < -90 || a > 89) continue;
    for (int b = (int)std::floor(lon - dLon); b <= (int)std::floor(lon + dLon); b++) {
      const int w = ((b + 180) % 360 + 360) % 360 - 180;
      const double cLat = std::clamp(lat, (double)a, a + 1.0), cLon = std::clamp(lon, (double)b, b + 1.0);
      const double d = geo::surfaceDistance({lat, lon, 0}, {cLat, cLon, 0});
      if (d <= radiusMetres) found.push_back({{a, w}, d});   // (the box's corners beyond reach left out)
    }
  }
  std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.second != y.second ? x.second < y.second : x.first < y.first; });
  std::vector<std::pair<int, int>> out;
  for (const auto& f : found) out.push_back(f.first);
  return out;
}
} // namespace earthFetch
