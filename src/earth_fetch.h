// Somnium Volandi - the real Earth's fine elevation fetched as it is wanted (docs/REAL_EARTH_PLAN.md section 2):
// Copernicus GLO-90's 1-degree tiles from the AWS open data bucket, into a cache folder the pack reads after its own
// (earth::Pack::setGlo90Cache). One download at a time on a thread of its own, nearest first; a tile is written beside
// its place and renamed into it when whole, so a reader never sees half of one. Offline, nothing is fetched and the
// charts are built from ETOPO as before; a chart built before its tiles came is built again with them (its cache key
// counts the tiles it found, World::buildEarth).
#pragma once
#include "earth_data.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace earthFetch {
// The tile's address on the bucket (copernicus-dem-90m)
std::string glo90Url(int lat, int lon);
// A whole download of url into path, or false with the reason: WinHTTP on Windows, curl elsewhere (as the shell finds
// it). Gives up when *cancel is set, between the response's chunks
bool download(const std::string& url, const std::string& path, std::string* error = nullptr, const std::atomic<bool>* cancel = nullptr);
// The same, carrying on where a download into path broke off: its part file (path + ".part") kept when it fails, and
// added to the next time (an HTTP range; curl's -C -) - the Earth pack's zips, gigabytes each (earth_install.h). No
// time limit but a stall: under a kilobyte a second for a minute
bool downloadResuming(const std::string& url, const std::string& path, std::string* error = nullptr, const std::atomic<bool>* cancel = nullptr);
// The bytes [offset, offset + length) of the file at url, into out - exactly those, or false with the reason ("HTTP 404
// from ..." when it isn't there): a streamed Earth pack's pieces (earth::setRangeFetch), each a range of a zip of the
// release. An HTTP range over WinHTTP on Windows, its session kept so its connections are reused; curl's -r elsewhere
bool fetchRange(const std::string& url, uint64_t offset, uint64_t length, std::vector<uint8_t>& out, std::string* error = nullptr);

class Glo90Fetcher {
public:
  using Transfer = std::function<bool(const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>* cancel)>;
  // into dir (created when needed); transfer: download unless a test gives its own
  explicit Glo90Fetcher(std::string dir, Transfer transfer = download);
  ~Glo90Fetcher();
  // The tiles wanted now, in order (earlier first): those Copernicus has (the pack's bitmap) and the cache hasn't, nor
  // has failed this session, replace the queue
  void want(const earth::Pack& pack, const std::vector<std::pair<int, int>>& tiles);
  // Tiles queued or downloading, fetched so far, and failed (with the last reason)
  int pending() const;
  int fetched() const { return fetched_.load(); }
  int failed() const { return failed_.load(); }
  std::string lastError() const;
  const std::string& dir() const { return dir_; }
  // Waits until nothing is pending or the time is up (a loading screen's few seconds); true when nothing is
  bool settle(double seconds);
private:
  void run();
  std::string dir_;
  Transfer transfer_;
  mutable std::mutex lock_;
  std::condition_variable wake_, idle_;
  std::deque<std::pair<int, int>> queue_;
  std::vector<uint64_t> failedTiles_;
  bool busy_ = false, stop_ = false;
  std::atomic<bool> cancel_{false};
  std::atomic<int> fetched_{0}, failed_{0};
  std::string lastError_;
  std::thread worker_;
};
// The tiles a chart reads (World::buildEarth: its square and a margin): those with a part within radiusMetres of the
// place, nearest first
std::vector<std::pair<int, int>> tilesAround(double lat, double lon, double radiusMetres);
} // namespace earthFetch
