// Somnium Volandi - the real Earth's pack installed from the game (docs/REAL_EARTH_PLAN.md, E5): the zips
// tools/earth2 package makes (stored, each unpacking into earth/), listed in SHA256SUMS at a source address - a release
// of the game's repository unless earth-source.txt beside the game names another. Downloaded one after another on a
// thread of its own (earthFetch::downloadResuming: WinHTTP or curl), unpacked into a staging folder, opened as a pack
// there to prove it whole, then moved into place in the user's folder, where the game looks for it
// (earthCharts::setSearch). A download that breaks off goes on from there when tried again: the zips already unpacked
// are kept (unpacked.txt beside them), the one it broke off in carried on from its part - unless the list has changed.
#pragma once
#include "earth_fetch.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace earthInstall {
constexpr const char* kDefaultSource = "https://github.com/chad04220/Solace-Express/releases/download/earth/";
// The source address: the first line of earth-source.txt in dir when there is one, else kDefaultSource (with a
// trailing slash either way)
std::string sourceFor(const std::string& dir);
// A stored zip (no compression: the pack's) unpacked into dir, its folders made; false with the reason - and a zip of
// another kind, or a name that would leave dir, refused
bool unzipStored(const std::string& zip, const std::string& dir, std::string* error = nullptr);

class Installer {
public:
  enum State { RUNNING, DONE, FAILED };
  // from source (an address ending in /), working in workDir (the zips and the staging folder: removed once it is in
  // place, kept when it fails, to go on from), into installDir (replaced only once the new pack has opened)
  Installer(std::string source, std::string workDir, std::string installDir, earthFetch::Glo90Fetcher::Transfer transfer = earthFetch::downloadResuming);
  ~Installer();   // (stops a download under way, between its chunks)
  State state() const { return state_.load(); }
  std::string status() const;   // what it is doing, or why it failed
  float progress() const { return progress_.load(); }   // 0..1, by the zips done
private:
  void run();
  void say(const std::string& s, const std::string& partial = "");
  std::string source_, work_, install_;
  earthFetch::Glo90Fetcher::Transfer transfer_;
  std::atomic<State> state_{RUNNING};
  std::atomic<float> progress_{0.f};
  std::atomic<bool> cancel_{false};
  mutable std::mutex lock_;
  std::string status_, partial_;   // (partial_: the file a download is writing, for its size)
  std::thread worker_;
};
} // namespace earthInstall
