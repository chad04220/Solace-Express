// Somnium Volandi - the real Earth's pack installed from the game (earth_install.h)
#include "earth_install.h"
#include "earth_data.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace earthInstall {
namespace {
struct File {
  FILE* f = nullptr;
  ~File() { if (f) fclose(f); }
  bool seek(uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, (long long)at, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)at, SEEK_SET) == 0;
#endif
  }
  bool read(void* to, size_t n) { return fread(to, 1, n, f) == n; }
  uint64_t size() {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END); return (uint64_t)_ftelli64(f);
#else
    fseeko(f, 0, SEEK_END); return (uint64_t)ftello(f);
#endif
  }
};
uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint64_t u64(const uint8_t* p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }
// a name a zip may hold here: relative, no step out of the folder it unpacks into
bool safeName(const std::string& n) {
  if (n.empty() || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return false;
  size_t at = 0;
  while (at <= n.size()) {
    size_t end = n.find_first_of("/\\", at);
    if (end == std::string::npos) end = n.size();
    if (n.compare(at, end - at, "..") == 0 && end - at == 2) return false;
    at = end + 1;
  }
  return true;
}
} // namespace

std::string sourceFor(const std::string& dir) {
  std::string source = kDefaultSource;
  if (FILE* f = fopen((dir.empty() ? std::string("earth-source.txt") : dir + "/earth-source.txt").c_str(), "r")) {
    char line[1024] = {};
    if (fgets(line, sizeof line, f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (!s.empty()) source = s;
    }
    fclose(f);
  }
  if (source.back() != '/') source += '/';
  return source;
}

bool unzipStored(const std::string& zip, const std::string& dir, std::string* error) {
  auto fail = [&](const std::string& why) { if (error) *error = zip + ": " + why; return false; };
  File z; z.f = fopen(zip.c_str(), "rb");
  if (!z.f) return fail("can't be read");
  const uint64_t size = z.size();
  // the end of the central directory: its record within the last 64 kB (a comment's length) of the file
  const uint64_t tail = std::min<uint64_t>(size, 65536 + 22);
  std::vector<uint8_t> end(tail);
  if (tail < 22 || !z.seek(size - tail) || !z.read(end.data(), tail)) return fail("not a zip");
  long eocd = -1;
  for (long i = (long)tail - 22; i >= 0; i--) if (u32(&end[i]) == 0x06054b50u) { eocd = i; break; }
  if (eocd < 0) return fail("not a zip");
  uint64_t entries = u16(&end[eocd + 10]), cdSize = u32(&end[eocd + 12]), cdAt = u32(&end[eocd + 16]);
  if (eocd >= 20 && u32(&end[eocd - 20]) == 0x07064b50u) {   // (zip64: its own record, which the locator points to)
    const uint64_t at = u64(&end[eocd - 20 + 8]);
    uint8_t r[56];
    if (!z.seek(at) || !z.read(r, 56) || u32(r) != 0x06064b50u) return fail("its zip64 directory is damaged");
    entries = u64(r + 32); cdSize = u64(r + 40); cdAt = u64(r + 48);
  }
  if (cdAt + cdSize > size || entries > 1000000) return fail("its directory is damaged");
  std::vector<uint8_t> cd(cdSize);
  if (!z.seek(cdAt) || !z.read(cd.data(), cdSize)) return fail("its directory is damaged");
  std::vector<uint8_t> buf(1 << 20);
  size_t p = 0;
  for (uint64_t e = 0; e < entries; e++) {
    if (p + 46 > cd.size() || u32(&cd[p]) != 0x02014b50u) return fail("its directory is damaged");
    const uint16_t method = u16(&cd[p + 10]), nameLen = u16(&cd[p + 28]), extraLen = u16(&cd[p + 30]), commentLen = u16(&cd[p + 32]);
    const uint32_t crc = u32(&cd[p + 16]);
    uint64_t csize = u32(&cd[p + 20]), usize = u32(&cd[p + 24]), local = u32(&cd[p + 42]);
    if (p + 46 + nameLen + extraLen > cd.size()) return fail("its directory is damaged");
    const std::string name((const char*)&cd[p + 46], nameLen);
    for (size_t x = p + 46 + nameLen; x + 4 <= p + 46 + nameLen + extraLen;) {   // (zip64 sizes and offset)
      const uint16_t id = u16(&cd[x]), len = u16(&cd[x + 2]);
      if (id == 1) {
        size_t q = x + 4;
        if (usize == 0xFFFFFFFFu && q + 8 <= x + 4 + len) { usize = u64(&cd[q]); q += 8; }
        if (csize == 0xFFFFFFFFu && q + 8 <= x + 4 + len) { csize = u64(&cd[q]); q += 8; }
        if (local == 0xFFFFFFFFu && q + 8 <= x + 4 + len) { local = u64(&cd[q]); q += 8; }
      }
      x += 4 + len;
    }
    p += 46 + nameLen + extraLen + commentLen;
    if (!safeName(name)) return fail("holds a name outside its folder: " + name);
    const fs::path out = fs::path(dir) / fs::u8path(name);
    std::error_code ec;
    if (name.back() == '/') { fs::create_directories(out, ec); continue; }
    if (method != 0) return fail("is compressed (" + name + "): the pack's zips are stored");
    if (csize != usize) return fail("its sizes disagree (" + name + ")");
    uint8_t lh[30];
    if (!z.seek(local) || !z.read(lh, 30) || u32(lh) != 0x04034b50u) return fail("its entry " + name + " is damaged");
    const uint64_t data = local + 30 + u16(lh + 26) + u16(lh + 28);
    if (data + csize > size || !z.seek(data)) return fail("its entry " + name + " is cut short");
    fs::create_directories(out.parent_path(), ec);
    FILE* o = fopen(out.string().c_str(), "wb");
    if (!o) return fail("can't write " + out.string());
    uint32_t c = 0;
    bool ok = true;
    for (uint64_t left = csize; left && ok;) {
      const size_t n = (size_t)std::min<uint64_t>(left, buf.size());
      ok = z.read(buf.data(), n) && fwrite(buf.data(), 1, n, o) == n;
      c = earth::crc32(buf.data(), n, c);
      left -= n;
    }
    ok = (fclose(o) == 0) && ok;
    if (!ok) return fail("couldn't unpack " + name);
    if (c != crc) return fail(name + " is damaged (its checksum)");
  }
  return true;
}

Installer::Installer(std::string source, std::string workDir, std::string installDir, earthFetch::Glo90Fetcher::Transfer transfer)
    : source_(std::move(source)), work_(std::move(workDir)), install_(std::move(installDir)), transfer_(std::move(transfer)) {
  if (source_.empty() || source_.back() != '/') source_ += '/';
  say("Fetching the Earth pack's list");
  worker_ = std::thread([this] { run(); });
}
Installer::~Installer() { cancel_ = true; if (worker_.joinable()) worker_.join(); }
std::string Installer::status() const {
  std::lock_guard<std::mutex> g(lock_);
  if (partial_.empty()) return status_;
  std::error_code ec;   // (a download under way: how much of it has come, by its part file)
  const uintmax_t got = fs::file_size(partial_, ec);
  return ec ? status_ : status_ + ", " + std::to_string(got >> 20) + " MB";
}
void Installer::say(const std::string& s, const std::string& partial) { std::lock_guard<std::mutex> g(lock_); status_ = s; partial_ = partial; }

void Installer::run() {
  std::error_code ec;
  const fs::path staging = fs::path(work_) / "staging", marker = fs::path(work_) / "unpacked.txt";
  // (a failure keeps what came: tried again, the download goes on from the zip it broke off in - its part, the zips
  // unpacked before it. keep false: begun again from nothing, the work folder removed)
  auto fail = [&](const std::string& why, bool keep) { if (!keep) fs::remove_all(work_, ec); say(why); state_ = FAILED; };
  auto slurp = [](const fs::path& p) { std::string s; if (FILE* f = fopen(p.string().c_str(), "rb")) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); } return s; };
  fs::create_directories(work_, ec);
  // the list, fetched each time: a download begun from another (the pack since built again) is begun again
  const fs::path sums = fs::path(work_) / "SHA256SUMS", fresh = fs::path(work_) / "SHA256SUMS.new";
  std::string why;
  if (!transfer_(source_ + "SHA256SUMS", fresh.string(), &why, &cancel_)) return fail("The Earth pack couldn't be fetched: " + why, true);
  if (slurp(fresh) != slurp(sums)) {
    fs::remove_all(staging, ec); fs::remove(marker, ec);
    for (const auto& e : fs::directory_iterator(work_, ec)) {
      const std::string n = e.path().filename().string();
      if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".zip") == 0 || n.compare(n.size() - 5, 5, ".part") == 0)) fs::remove(e.path(), ec);
    }
  }
  fs::rename(fresh, sums, ec);
  std::vector<std::string> zips;
  if (FILE* f = fopen(sums.string().c_str(), "r")) {
    char line[512];
    while (fgets(line, sizeof line, f)) {
      char hash[129] = {}, name[256] = {};
      if (sscanf(line, "%128s %255s", hash, name) == 2 && strchr(name, '/') == nullptr && strchr(name, '\\') == nullptr) zips.push_back(name);
    }
    fclose(f);
  }
  if (zips.empty()) return fail("The Earth pack's list is empty or damaged", false);
  std::set<std::string> unpacked;
  if (fs::exists(staging, ec)) {
    std::istringstream in(slurp(marker));
    for (std::string line; std::getline(in, line);) if (!line.empty()) unpacked.insert(line);
  } else fs::remove(marker, ec);
  fs::create_directories(staging, ec);
  for (size_t i = 0; i < zips.size(); i++) {
    if (unpacked.count(zips[i])) { progress_ = float(i + 1) / float(zips.size()); continue; }
    if (cancel_) return fail("Cancelled", true);
    const std::string zip = (fs::path(work_) / zips[i]).string();
    say("Downloading the Earth pack: " + zips[i] + " (" + std::to_string(i + 1) + " of " + std::to_string(zips.size()) + ")", zip + ".part");
    if (!transfer_(source_ + zips[i], zip, &why, &cancel_))
      return fail("The Earth pack couldn't be downloaded: " + why + " (tried again, it goes on from there)", true);
    say("Unpacking " + zips[i]);
    if (!unzipStored(zip, staging.string(), &why)) {
      fs::remove(zip, ec);   // (that one fetched again: the rest kept)
      return fail("The Earth pack couldn't be unpacked: " + why, true);
    }
    fs::remove(zip, ec);
    if (FILE* f = fopen(marker.string().c_str(), "a")) { fprintf(f, "%s\n", zips[i].c_str()); fclose(f); }
    progress_ = float(i + 1) / float(zips.size());
  }
  say("Checking the Earth pack");
  const fs::path whole = staging / "earth";
  { std::string bad; if (!earth::Pack::open(whole.string(), &bad)) return fail("The Earth pack is incomplete: " + bad, false); }
  // in place: a folder there before (a pack that wouldn't open) set aside, and put back if the new one can't go there
  const fs::path old = install_ + ".old";
  fs::remove_all(old, ec);
  fs::create_directories(fs::path(install_).parent_path(), ec);
  const bool hadOne = fs::exists(install_, ec);
  if (hadOne) { fs::rename(install_, old, ec); if (ec) return fail("The old Earth folder couldn't be moved aside: " + ec.message(), true); }
  fs::rename(whole, install_, ec);
  if (ec) {
    const std::string why = ec.message();
    if (hadOne) fs::rename(old, install_, ec);
    return fail("The Earth pack couldn't be put in place: " + why, true);
  }
  fs::remove_all(old, ec);
  fs::remove_all(staging, ec);
  fs::remove(sums, ec); fs::remove(marker, ec);
  fs::remove(work_, ec);   // (only when empty)
  say("The real Earth is installed");
  state_ = DONE;
}
} // namespace earthInstall
