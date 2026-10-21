// The real Earth's pack installed from the game (earth_install.h), with no network: the fixture's pack put in zips as
// the global build's are (tools/earth2 package, several of them) and fetched from a file address through curl. The
// pack installs whole and opens where it went; installed again it replaces the folder there; a missing source or a
// damaged zip fails and leaves the pack in place as it was. Tried again, a download goes on from where it stopped -
// the zips unpacked kept, one broken off halfway carried on from its part - unless the list has changed. The zip
// reader refuses a compressed zip, a name that would step out of its folder and a checksum that disagrees. The source
// is read from earth-source.txt beside the game.
#include "../src/earth_install.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

static int failed = 0;
static void check(bool pass, const std::string& label) { printf("Earth install: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass; }
static std::string quote(const std::string& s) { std::string q = "'"; for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; } return q + "'"; }
static earthInstall::Installer::State finish(earthInstall::Installer& in) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while (in.state() == earthInstall::Installer::RUNNING && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  return in.state();
}
static std::string slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void spit(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; }

int main(int argc, char** argv) {
  const std::string fixture = fs::absolute(argc > 1 ? argv[1] : "tests/fixtures/earth2").string();
  const std::string sourceRoot = argc > 2 ? argv[2] : ".";
  if (std::system("curl --version > /dev/null 2>&1") != 0) { printf("earth_install_test: no curl, skipped\n"); return 0; }
  std::string why;
  auto original = earth::Pack::open(fixture, &why);
  check(original != nullptr, "the fixture opens " + why);
  if (!original) return 1;
  const fs::path root = fs::temp_directory_path() / ("earth_install_test_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path dist = root / "dist", work = root / "work", user = root / "user", install = user / "earth";
  fs::create_directories(root);
  // the fixture in zips of a few hundred kB each, as the global build makes them under 2 GB
  const std::string py = "cd " + quote(sourceRoot) + " && python3 -m tools.earth2 package " + quote(fixture) + " " + quote(dist.string()) + " --max-bytes 400000 > /dev/null";
  check(std::system(py.c_str()) == 0 && fs::exists(dist / "SHA256SUMS"), "the fixture packaged");
  int zips = 0;
  for (const auto& e : fs::directory_iterator(dist)) zips += e.path().extension() == ".zip";
  check(zips >= 3, "in several zips (" + std::to_string(zips) + ")");
  const std::string source = "file://" + dist.string() + "/";

  // the source: earth-source.txt beside the game when there is one
  check(earthInstall::sourceFor(root.string()) == earthInstall::kDefaultSource, "no earth-source.txt: the game's own release");
  spit(root / "earth-source.txt", "file://" + dist.string() + "\r\n");
  check(earthInstall::sourceFor(root.string()) == source, "earth-source.txt names the source " + earthInstall::sourceFor(root.string()));

  // installed: whole, opening where it went, the work folder gone
  {
    earthInstall::Installer in(earthInstall::sourceFor(root.string()), work.string(), install.string());
    const bool done = finish(in) == earthInstall::Installer::DONE && in.progress() == 1.f;
    check(done, "installed: " + in.status());
    auto pack = earth::Pack::open(install.string(), &why);
    check(pack && pack->airportCount() == original->airportCount(), "the installed pack opens, its airports all there " + why);
    check(!fs::exists(work) && !fs::exists(install.string() + ".old"), "nothing left of the download");
  }
  // installed again: the folder there replaced, what else it held gone
  spit(install / "stray.txt", "left over");
  {
    earthInstall::Installer in(source, work.string(), install.string());
    const bool done = finish(in) == earthInstall::Installer::DONE && !fs::exists(install / "stray.txt") && earth::Pack::open(install.string());
    check(done, "installed again over the last: " + in.status());
  }
  // a source with nothing there: failed, the pack in place untouched
  {
    earthInstall::Installer in("file://" + (root / "nowhere").string() + "/", work.string(), install.string());
    const bool kept = finish(in) == earthInstall::Installer::FAILED && earth::Pack::open(install.string());
    check(kept, "a missing source fails, the pack kept: " + in.status());
  }
  // a damaged zip (a byte of its data changed, its checksum not): failed, the pack kept, the zips before it kept
  const std::string name2 = "earth-pack-02-of-" + std::string(zips < 10 ? "0" : "") + std::to_string(zips) + ".zip";
  const std::string name3 = "earth-pack-03-of-" + std::string(zips < 10 ? "0" : "") + std::to_string(zips) + ".zip";
  {
    const fs::path bad = root / "bad";
    fs::create_directories(bad);
    for (const auto& e : fs::directory_iterator(dist)) fs::copy_file(e.path(), bad / e.path().filename());
    std::string z = slurp(bad / name2);
    const bool found = z.size() > 4000;
    if (found) z[z.size() / 2] ^= 0x5a;
    spit(bad / name2, z);
    earthInstall::Installer in("file://" + bad.string() + "/", work.string(), install.string());
    const bool kept = found && finish(in) == earthInstall::Installer::FAILED && earth::Pack::open(install.string()) &&
                      !fs::exists(work / name2) && slurp(work / "unpacked.txt").find("earth-pack-01") == 0;
    check(kept, "a damaged zip fails, the pack kept, the zip before it kept unpacked: " + in.status());
  }
  // tried again from the good source (the same list): the zips already unpacked not fetched again
  std::vector<std::string> fetched;
  auto counting = [&](const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>* cancel) {
    fetched.push_back(url.substr(url.rfind('/') + 1));
    return earthFetch::downloadResuming(url, path, error, cancel);
  };
  {
    earthInstall::Installer in(source, work.string(), install.string(), counting);
    const bool done = finish(in) == earthInstall::Installer::DONE && earth::Pack::open(install.string()) && !fs::exists(work);
    bool skipped = std::find(fetched.begin(), fetched.end(), name2) != fetched.end();
    for (const auto& f : fetched) skipped &= f.find("earth-pack-01") != 0;
    check(done && skipped && (int)fetched.size() == zips, fmt("tried again: carried on from the damaged one, %zu fetched (the list and %d zips)", fetched.size(), zips - 1));
  }
  // a download broken off halfway through a zip: tried again, that zip carried on from its part (curl -C -)
  {
    bool broke = false;
    auto breaking = [&](const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>* cancel) {
      if (url.size() > name3.size() && url.compare(url.size() - name3.size(), name3.size(), name3) == 0) {
        const std::string all = slurp(dist / name3);
        spit(path + ".part", all.substr(0, all.size() / 2));
        if (error) *error = "the download broke off";
        broke = true;
        return false;
      }
      return earthFetch::downloadResuming(url, path, error, cancel);
    };
    {
      earthInstall::Installer in(source, work.string(), install.string(), breaking);
      const bool failed = finish(in) == earthInstall::Installer::FAILED && broke && fs::exists(work / (name3 + ".part"));
      check(failed, "broken off halfway through the third zip, its part kept: " + in.status());
    }
    fetched.clear();
    earthInstall::Installer in(source, work.string(), install.string(), counting);
    const bool done = finish(in) == earthInstall::Installer::DONE && earth::Pack::open(install.string());
    check(done && !fetched.empty() && fetched.size() == (size_t)zips - 1 && fetched[1] == name3,
          fmt("tried again: the third zip carried on from its half, whole (%zu fetched)", fetched.size()));
  }
  // the list changed since (the pack built again): what was kept begun again
  {
    auto breaking = [&](const std::string& url, const std::string& path, std::string* error, const std::atomic<bool>* cancel) {
      if (url.find(name3) != std::string::npos) { if (error) *error = "broke off"; return false; }
      return earthFetch::downloadResuming(url, path, error, cancel);
    };
    { earthInstall::Installer in(source, work.string(), install.string(), breaking); finish(in); }
    std::string list = slurp(dist / "SHA256SUMS");
    list[0] = list[0] == '0' ? '1' : '0';   // (another hash at the top: another pack)
    spit(dist / "SHA256SUMS", list);
    fetched.clear();
    earthInstall::Installer in(source, work.string(), install.string(), counting);
    const bool done = finish(in) == earthInstall::Installer::DONE && earth::Pack::open(install.string());
    check(done && (int)fetched.size() == zips + 1, fmt("the list changed: every zip fetched again (%zu)", fetched.size()));
  }
  // the zip reader: a compressed zip, a name stepping out, refused (nothing written outside)
  {
    const fs::path odd = root / "odd";
    fs::create_directories(odd / "into");
    const std::string make = "python3 -I -c " + quote(
        "import sys, zipfile\n"
        "z = zipfile.ZipFile(sys.argv[1] + '/deflated.zip', 'w', zipfile.ZIP_DEFLATED); z.writestr('earth/a.txt', 'x' * 1000); z.close()\n"
        "z = zipfile.ZipFile(sys.argv[1] + '/escape.zip', 'w', zipfile.ZIP_STORED); z.writestr('../escaped.txt', 'out'); z.close()\n"
        "z = zipfile.ZipFile(sys.argv[1] + '/fine.zip', 'w', zipfile.ZIP_STORED); z.writestr('earth/sub/b.txt', 'inside'); z.close()\n") +
        " " + quote(odd.string());
    check(std::system(make.c_str()) == 0, "odd zips made");
    std::string e1, e2, e3;
    const bool deflated = !earthInstall::unzipStored((odd / "deflated.zip").string(), (odd / "into").string(), &e1);
    check(deflated, "a compressed zip refused: " + e1);
    const bool escape = !earthInstall::unzipStored((odd / "escape.zip").string(), (odd / "into").string(), &e2) && !fs::exists(odd / "escaped.txt");
    check(escape, "a name out of its folder refused: " + e2);
    const bool fine = earthInstall::unzipStored((odd / "fine.zip").string(), (odd / "into").string(), &e3) && slurp(odd / "into" / "earth" / "sub" / "b.txt") == "inside";
    check(fine, "a stored zip unpacked, its folders made " + e3);
  }
  std::error_code ec; fs::remove_all(root, ec);
  printf("earth_install_test: %s\n", failed ? "FAILED" : "passed");
  return failed ? 1 : 0;
}
