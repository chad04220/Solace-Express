// Somnium Volandi - the real Earth's data: the Earth pack's bundles, tables and GLO-90 tiles (earth_data.h).
// The formats are tools/earth2's (svb.py, tables.py, roads.py, glo90.py); tests/earth_data_test.cpp reads the
// test fixture with both and compares.
#include "earth_data.h"
#include <cerrno>
#include <list>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>

namespace earth {

// ---------------------------------------------------------------------------------------------------------------
// CRC-32 (zlib's) and inflate (RFC 1950/1951)

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
  static uint32_t table[256];
  static std::once_flag once;
  std::call_once(once, [] {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  });
  crc = ~crc;
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
  return ~crc;
}

namespace {
struct BitReader {
  const uint8_t* p; const uint8_t* end;
  uint64_t bits = 0; int count = 0; int phantom = 0;   // (zero bytes fed past the end: an error if they are consumed)
  BitReader(const uint8_t* a, const uint8_t* b) : p(a), end(b) {}
  void refill() {
    while (count <= 56) {
      uint64_t byte = 0;
      if (p < end) byte = *p++; else phantom++;
      bits |= byte << count; count += 8;
    }
  }
  uint32_t peek(int n) { if (count < n) refill(); return (uint32_t)(bits & ((1ull << n) - 1)); }
  void drop(int n) { bits >>= n; count -= n; }
  uint32_t take(int n) { if (n == 0) return 0; uint32_t v = peek(n); drop(n); return v; }
  bool overrun() const { return phantom * 8 > count; }
  void alignByte() { drop(count & 7); }
};

struct Huffman {
  static constexpr int kLut = 10;
  uint16_t lut[1 << kLut];   // (length << 9) | symbol for codes of up to kLut bits, 0 for longer ones
  uint16_t counts[16];
  uint16_t symbols[320];
  bool build(const uint8_t* lengths, int n) {
    memset(lut, 0, sizeof(lut)); memset(counts, 0, sizeof(counts));
    for (int i = 0; i < n; i++) counts[lengths[i]]++;
    counts[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) { left <<= 1; left -= counts[len]; if (left < 0) return false; }   // (over-subscribed)
    uint16_t offs[16]; offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + counts[len];
    for (int i = 0; i < n; i++) if (lengths[i]) symbols[offs[lengths[i]]++] = (uint16_t)i;
    // canonical codes, bit-reversed into the lookup table
    uint32_t code = 0, next[16] = {};
    for (int len = 1; len < 16; len++) { code = (code + counts[len - 1]) << 1; next[len] = code; }
    for (int i = 0; i < n; i++) {
      const int len = lengths[i];
      if (!len) continue;
      const uint32_t c = next[len]++;
      if (len > kLut) continue;
      uint32_t rev = 0;
      for (int k = 0; k < len; k++) rev |= ((c >> k) & 1) << (len - 1 - k);
      for (uint32_t j = rev; j < (1u << kLut); j += 1u << len) lut[j] = (uint16_t)((len << 9) | i);
    }
    return true;
  }
  int decode(BitReader& br) const {
    const uint32_t look = br.peek(15);
    const uint16_t e = lut[look & ((1 << kLut) - 1)];
    if (e) { br.drop(e >> 9); return e & 511; }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= (look >> (len - 1)) & 1;
      const int count = counts[len];
      if (code - first < count) { br.drop(len); return symbols[index + code - first]; }
      index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
  }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
} // namespace

bool inflate(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) {
  if (inSize < 6) return false;
  const int cmf = in[0], flg = in[1];
  if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 32)) return false;
  BitReader br(in + 2, in + inSize);
  size_t o = 0;
  Huffman lit, dist;
  bool last = false;
  while (!last) {
    last = br.take(1);
    const int type = br.take(2);
    if (type == 0) {
      br.alignByte();
      const uint32_t len = br.take(16), nlen = br.take(16);
      if ((len ^ 0xFFFF) != nlen || o + len > outSize) return false;
      for (uint32_t i = 0; i < len; i++) out[o++] = (uint8_t)br.take(8);
      if (br.overrun()) return false;
      continue;
    }
    if (type == 3) return false;
    if (type == 1) {
      uint8_t l[288], d[30];
      for (int i = 0; i < 144; i++) l[i] = 8;
      for (int i = 144; i < 256; i++) l[i] = 9;
      for (int i = 256; i < 280; i++) l[i] = 7;
      for (int i = 280; i < 288; i++) l[i] = 8;
      for (int i = 0; i < 30; i++) d[i] = 5;
      lit.build(l, 288); dist.build(d, 30);
    } else {
      const int hlit = br.take(5) + 257, hdist = br.take(5) + 1, hclen = br.take(4) + 4;
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t cl[19] = {};
      for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)br.take(3);
      Huffman clh;
      if (!clh.build(cl, 19)) return false;
      uint8_t lens[320] = {};
      for (int i = 0; i < hlit + hdist;) {
        const int sym = clh.decode(br);
        if (sym < 0) return false;
        if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
        int rep = 0, val = 0;
        if (sym == 16) { if (i == 0) return false; val = lens[i - 1]; rep = 3 + br.take(2); }
        else if (sym == 17) rep = 3 + br.take(3);
        else rep = 11 + br.take(7);
        if (i + rep > hlit + hdist) return false;
        while (rep--) lens[i++] = (uint8_t)val;
      }
      if (!lens[256] || !lit.build(lens, hlit) || !dist.build(lens + hlit, hdist)) return false;
    }
    for (;;) {
      const int sym = lit.decode(br);
      if (sym < 0) return false;
      if (sym < 256) { if (o >= outSize) return false; out[o++] = (uint8_t)sym; continue; }
      if (sym == 256) break;
      const int li = sym - 257;
      if (li >= 29) return false;
      const size_t len = kLenBase[li] + br.take(kLenExtra[li]);
      const int ds = dist.decode(br);
      if (ds < 0 || ds >= 30) return false;
      const size_t d = kDistBase[ds] + br.take(kDistExtra[ds]);
      if (d > o || o + len > outSize) return false;
      uint8_t* w = out + o;
      const uint8_t* r = w - d;
      for (size_t i = 0; i < len; i++) w[i] = r[i];
      o += len;
    }
    if (br.overrun()) return false;
  }
  if (o != outSize) return false;
  // the Adler-32 trailer, after the stream's last byte
  br.alignByte();
  uint32_t want = 0;
  for (int i = 0; i < 4; i++) want = (want << 8) | br.take(8);
  if (br.overrun()) return false;
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < outSize;) {
    const size_t stop = std::min(outSize, i + 5552);
    for (; i < stop; i++) { a += out[i]; b += a; }
    a %= 65521; b %= 65521;
  }
  return ((b << 16) | a) == want;
}

// ---------------------------------------------------------------------------------------------------------------
// The pack

namespace {
constexpr int kBlock = 10, kTiles = 100, kHeader = 64, kEntry = 24, kCells = 360 * 180;
enum { LAYER_ELEV = 1, LAYER_COVER = 2, LAYER_ROADS = 3, LAYER_LOCAL_ROADS = 4 };
enum { TYPE_INT16 = 1, TYPE_UINT8 = 2, TYPE_BLOB = 3 };
enum { KIND_ABSENT = 0, KIND_ZLIB = 1, KIND_CONSTANT = 2 };

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
int16_t i16(const uint8_t* p) { return (int16_t)u16(p); }
uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int32_t i32(const uint8_t* p) { return (int32_t)u32(p); }
uint64_t u64(const uint8_t* p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
std::string cornerName(int lat, int lon) {
  char b[32];
  snprintf(b, sizeof(b), "%c%02d%c%03d", lat >= 0 ? 'N' : 'S', std::abs(lat), lon >= 0 ? 'E' : 'W', std::abs(lon));
  return b;
}
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(size > 0 ? (size_t)size : 0);
  const bool ok = size >= 0 && fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}
bool fileSize(const std::string& path, long long& size) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  size = (long long)st.st_size;
  return true;
}
std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  const char last = a.back();
  return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

struct Entry { uint64_t offset; uint32_t size, rawSize, crc; uint16_t kind; int16_t constant; };
struct Bundle {
  std::string path;
  int layer = 0, type = 0, south = 0, west = 0, spd = 0, apron = 0;
  Entry entries[kTiles];
  FILE* file = nullptr;
};

// A cache of decoded tiles, least recently used dropped first once its bytes pass the limit
template <class T> struct Lru {
  size_t limit, bytes = 0;
  std::list<std::pair<uint64_t, std::shared_ptr<const T>>> order;
  std::unordered_map<uint64_t, typename decltype(order)::iterator> index;
  explicit Lru(size_t l) : limit(l) {}
  std::shared_ptr<const T> get(uint64_t key) {
    auto it = index.find(key);
    if (it == index.end()) return nullptr;
    order.splice(order.begin(), order, it->second);
    return it->second->second;
  }
  void put(uint64_t key, std::shared_ptr<const T> v, size_t size) {
    if (index.count(key)) return;
    order.emplace_front(key, std::move(v));
    index[key] = order.begin();
    bytes += size;
    while (bytes > limit && order.size() > 1) {
      auto& back = order.back();
      bytes -= sizeOf(*back.second);
      index.erase(back.first);
      order.pop_back();
    }
  }
  static size_t sizeOf(const RasterTile& t) { return t.bytes(); }
  static size_t sizeOf(const Glo90Tile& t) { return t.bytes(); }
  static size_t sizeOf(const std::vector<RoadEdge>& v) {
    size_t s = 0; for (const auto& e : v) s += sizeof(RoadEdge) + e.pts.size() * 16; return s;
  }
};
uint64_t tileKey(int layer, int lat, int lon) { return (uint64_t)layer << 32 | (uint64_t)(uint32_t)((lat + 90) * 360 + (lon + 180)); }
} // namespace

struct Pack::Impl {
  std::mutex lock;   // (the files, the caches, the bundle map)
  std::map<std::string, std::unique_ptr<Bundle>> bundles;   // by relative path, opened when first read
  std::map<std::string, long long> files;                   // pack.idx: relative path -> bytes
  Lru<RasterTile> rasters{192u << 20};
  Lru<std::vector<RoadEdge>> roads{256u << 20};
  Lru<Glo90Tile> glo90{160u << 20};
  std::vector<uint8_t> airports, places, glo90Bits;
  std::string glo90Cache;
  ~Impl() { for (auto& b : bundles) if (b.second && b.second->file) fclose(b.second->file); }

  // The bundle holding a layer's tile (nullptr when the pack has no such file)
  Bundle* bundle(const std::string& dir, int layer, int lat, int lon) {
    static const char* names[] = {"", "elev", "cover", "roads", "lroads"};
    const int south = floorDiv(lat, kBlock) * kBlock, west = floorDiv(lon, kBlock) * kBlock;
    const std::string rel = std::string(names[layer]) + "/" + cornerName(south, west) + ".svb";
    auto it = bundles.find(rel);
    if (it != bundles.end()) return it->second.get();
    if (!files.count(rel)) { bundles[rel] = nullptr; return nullptr; }
    auto b = std::make_unique<Bundle>();
    b->path = joinPath(dir, rel);
    b->file = fopen(b->path.c_str(), "rb");
    uint8_t head[kHeader + kEntry * kTiles];
    if (!b->file || fread(head, 1, sizeof(head), b->file) != sizeof(head) || memcmp(head, "SVB2", 4) != 0 || u16(head + 4) != 1)
      throw std::runtime_error(b->path + ": not an Earth pack bundle");
    b->layer = head[6]; b->type = head[7]; b->south = i16(head + 8); b->west = i16(head + 10);
    b->spd = u16(head + 12); b->apron = u16(head + 14);
    if (u32(head + 16) != kTiles || b->south != south || b->west != west) throw std::runtime_error(b->path + ": bundle header doesn't match its name");
    if ((b->layer == LAYER_ELEV && (b->type != TYPE_INT16 || b->spd != kElevSamples || b->apron != kApron)) ||
        (b->layer == LAYER_COVER && (b->type != TYPE_UINT8 || b->spd != kCoverSamples || b->apron != kApron)) ||
        ((layer == LAYER_ROADS || layer == LAYER_LOCAL_ROADS) && b->type != TYPE_BLOB))
      throw std::runtime_error(b->path + ": bundle of an unexpected layout");
    const long long fsize = files[rel];
    for (int i = 0; i < kTiles; i++) {
      const uint8_t* e = head + kHeader + kEntry * i;
      Entry& en = b->entries[i];
      en.offset = u64(e); en.size = u32(e + 8); en.rawSize = u32(e + 12); en.crc = u32(e + 16); en.kind = u16(e + 20); en.constant = i16(e + 22);
      if (en.kind > KIND_CONSTANT || (en.kind == KIND_ZLIB && (en.offset < sizeof(head) || (long long)(en.offset + en.size) > fsize)))
        throw std::runtime_error(b->path + ": bundle index out of range");
    }
    Bundle* raw = b.get();
    bundles[rel] = std::move(b);
    return raw;
  }

  // A tile's raw bytes (checked), with its entry; false when absent
  bool tileBytes(Bundle* b, int lat, int lon, Entry& en, std::vector<uint8_t>& raw) {
    en = b->entries[(lat - b->south) * kBlock + (lon - b->west)];
    if (en.kind != KIND_ZLIB) return en.kind == KIND_CONSTANT;
    std::vector<uint8_t> stored(en.size);
    if (fseek(b->file, (long)en.offset, SEEK_SET) != 0 || fread(stored.data(), 1, stored.size(), b->file) != stored.size())
      throw std::runtime_error(b->path + ": short read");
    raw.resize(en.rawSize);
    if (!inflate(stored.data(), stored.size(), raw.data(), raw.size()) || crc32(raw.data(), raw.size()) != en.crc)
      throw std::runtime_error(b->path + ": damaged tile " + cornerName(lat, lon));
    return true;
  }
};

Pack::~Pack() = default;

std::shared_ptr<Pack> Pack::open(const std::string& dir, std::string* error) {
  auto fail = [&](const std::string& why) -> std::shared_ptr<Pack> { if (error) *error = why; return nullptr; };
  std::vector<uint8_t> idx;
  if (!readFile(joinPath(dir, "pack.idx"), idx)) return fail("no Earth pack in " + dir);
  std::shared_ptr<Pack> pack(new Pack());
  pack->dir_ = dir;
  pack->impl_ = std::make_unique<Impl>();
  std::string text(idx.begin(), idx.end());
  size_t at = 0;
  bool header = false, ended = false;
  while (at < text.size()) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = text.substr(at, nl - at);
    at = nl + 1;
    if (line.empty()) continue;
    if (!header) { if (line != "somnium-earth 2") return fail(dir + ": not a version 2 Earth pack"); header = true; continue; }
    if (line == "end") { ended = true; break; }
    if (line.compare(0, 4, "box ") == 0) {
      int s, w, n, e;
      if (sscanf(line.c_str() + 4, "%d %d %d %d", &s, &w, &n, &e) != 4) return fail(dir + ": bad box line");
      pack->box_ = {double(s), double(w), double(n), double(e)};
    } else if (line.compare(0, 5, "file ") == 0) {
      char rel[512]; long long bytes = 0; char digest[80];
      if (sscanf(line.c_str() + 5, "%511s %lld %79s", rel, &bytes, digest) != 3) return fail(dir + ": bad file line");
      long long actual = -1;
      if (!fileSize(joinPath(dir, rel), actual) || actual != bytes) return fail(dir + ": " + rel + " is missing or of the wrong size");
      pack->impl_->files[rel] = bytes;
      const std::string r = rel;
      if (r.compare(0, 6, "cover/") == 0) pack->hasCover_ = true;
      if (r.compare(0, 7, "lroads/") == 0) pack->hasLocalRoads_ = true;
    }
  }
  if (!header || !ended) return fail(dir + ": incomplete pack.idx");
  Impl& im = *pack->impl_;
  auto table = [&](const char* name, const char* magic, std::vector<uint8_t>& out) -> bool {
    if (!im.files.count(name)) return true;
    if (!readFile(joinPath(dir, name), out) || out.size() < 32 + 8 * (size_t)kCells || memcmp(out.data(), magic, 4) != 0 || u32(out.data() + 4) != 1) return false;
    return true;
  };
  if (!table("airports.bin", "SVA2", im.airports)) return fail(dir + ": airports.bin is damaged");
  if (!table("places.bin", "SVP2", im.places)) return fail(dir + ": places.bin is damaged");
  if (!im.airports.empty()) {   // (the sizes the header promises, and every cell inside the table)
    const uint32_t na = u32(im.airports.data() + 8), nr = u32(im.airports.data() + 12), ns = u32(im.airports.data() + 16);
    if (im.airports.size() != 32 + 8ull * kCells + 48ull * na + 40ull * nr + ns) return fail(dir + ": airports.bin is damaged");
    for (int c = 0; c < kCells; c++) if (u32(im.airports.data() + 32 + 8 * c) + (uint64_t)u32(im.airports.data() + 36 + 8 * c) > na) return fail(dir + ": airports.bin is damaged");
  }
  if (!im.places.empty()) {
    const uint32_t np = u32(im.places.data() + 8), ns = u32(im.places.data() + 12);
    if (im.places.size() != 32 + 8ull * kCells + 24ull * np + ns) return fail(dir + ": places.bin is damaged");
    for (int c = 0; c < kCells; c++) if (u32(im.places.data() + 32 + 8 * c) + (uint64_t)u32(im.places.data() + 36 + 8 * c) > np) return fail(dir + ": places.bin is damaged");
  }
  if (im.files.count("glo90.bits")) {
    if (!readFile(joinPath(dir, "glo90.bits"), im.glo90Bits) || im.glo90Bits.size() != kCells / 8) return fail(dir + ": glo90.bits is damaged");
  }
  return pack;
}

namespace {
std::shared_ptr<const RasterTile> loadRaster(Pack::Impl& im, const std::string& dir, int layer, int lat, int lon) {
  const uint64_t key = tileKey(layer, lat, lon);
  std::lock_guard<std::mutex> g(im.lock);
  if (auto t = im.rasters.get(key)) return t;
  Bundle* b = im.bundle(dir, layer, lat, lon);
  if (!b) return nullptr;
  Entry en; std::vector<uint8_t> raw;
  if (!im.tileBytes(b, lat, lon, en, raw)) return nullptr;
  auto t = std::make_shared<RasterTile>();
  t->lat = lat; t->lon = lon; t->n = b->spd;
  const size_t count = (size_t)t->width() * t->width();
  if (layer == LAYER_ELEV) {
    t->s16.resize(count);
    if (en.kind == KIND_CONSTANT) std::fill(t->s16.begin(), t->s16.end(), en.constant);
    else {
      if (raw.size() != count * 2) throw std::runtime_error(b->path + ": tile of the wrong size");
      const int w = t->width();
      for (int r = 0; r < w; r++) {   // (each row delta coded, wrapping at 16 bits)
        uint16_t v = 0;
        for (int c = 0; c < w; c++) {
          const size_t i = (size_t)r * w + c;
          v = (uint16_t)(c == 0 ? u16(&raw[2 * i]) : v + u16(&raw[2 * i]));
          t->s16[i] = (int16_t)v;
        }
      }
    }
  } else {
    if (en.kind == KIND_CONSTANT) t->s8.assign(count, (uint8_t)en.constant);
    else {
      if (raw.size() != count) throw std::runtime_error(b->path + ": tile of the wrong size");
      t->s8 = std::move(raw);
    }
  }
  im.rasters.put(key, t, t->bytes());
  return t;
}

uint64_t readVarint(const std::vector<uint8_t>& raw, size_t& at) {
  uint64_t v = 0; int shift = 0;
  for (;;) {
    if (at >= raw.size() || shift > 63) throw std::runtime_error("road tile: truncated");
    const uint8_t b = raw[at++];
    v |= (uint64_t)(b & 0x7F) << shift; shift += 7;
    if (!(b & 0x80)) return v;
  }
}
int64_t unzigzag(uint64_t v) { return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

std::shared_ptr<const std::vector<RoadEdge>> decodeRoads(const std::vector<uint8_t>& raw, const std::string& where) {
  auto out = std::make_shared<std::vector<RoadEdge>>();
  if (raw.size() < 4) throw std::runtime_error(where + ": short road tile");
  const uint32_t count = u32(raw.data());
  size_t at = 4;
  out->reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    if (at + 20 > raw.size()) throw std::runtime_error(where + ": truncated road tile");
    RoadEdge e;
    e.id = u64(&raw[at]); e.cls = raw[at + 8]; e.surface = raw[at + 9];
    const int n = u16(&raw[at + 10]);
    int64_t lat = i32(&raw[at + 12]), lon = i32(&raw[at + 16]);
    at += 20;
    if (n < 2) throw std::runtime_error(where + ": road edge of one point");
    e.pts.reserve(n);
    e.pts.emplace_back(lat * 1e-7, lon * 1e-7);
    for (int k = 1; k < n; k++) {
      lat += unzigzag(readVarint(raw, at)); lon += unzigzag(readVarint(raw, at));
      e.pts.emplace_back(lat * 1e-7, lon * 1e-7);
    }
    out->push_back(std::move(e));
  }
  if (at != raw.size()) throw std::runtime_error(where + ": road tile has trailing bytes");
  return out;
}

std::string str(const std::vector<uint8_t>& t, size_t stringsAt, uint32_t off) {
  const size_t at = stringsAt + off;
  if (at >= t.size()) return std::string();
  const uint8_t* p = &t[at];
  const size_t n = strnlen((const char*)p, t.size() - at);
  return std::string((const char*)p, n);
}
// The 1-degree cells a box covers (clamped to the poles; never across the antimeridian)
template <class F> void forCells(const Box& b, F&& f) {
  const int la0 = std::max(-90, (int)std::floor(b.south)), la1 = std::min(89, (int)std::ceil(b.north) - 1);
  const int lo0 = std::max(-180, (int)std::floor(b.west)), lo1 = std::min(179, (int)std::ceil(b.east) - 1);
  for (int la = la0; la <= la1; la++) for (int lo = lo0; lo <= lo1; lo++) f(la, lo, (la + 90) * 360 + (lo + 180));
}
} // namespace

std::shared_ptr<const RasterTile> Pack::elevTile(int lat, int lon) const { return loadRaster(*impl_, dir_, LAYER_ELEV, lat, lon); }
std::shared_ptr<const RasterTile> Pack::coverTile(int lat, int lon) const {
  return hasCover_ ? loadRaster(*impl_, dir_, LAYER_COVER, lat, lon) : nullptr;
}

std::shared_ptr<const std::vector<RoadEdge>> Pack::roadTile(int lat, int lon, bool local) const {
  const int layer = local ? LAYER_LOCAL_ROADS : LAYER_ROADS;
  const uint64_t key = tileKey(layer, lat, lon);
  std::lock_guard<std::mutex> g(impl_->lock);
  if (auto t = impl_->roads.get(key)) return t;
  Bundle* b = impl_->bundle(dir_, layer, lat, lon);
  std::shared_ptr<const std::vector<RoadEdge>> t;
  Entry en; std::vector<uint8_t> raw;
  if (b && impl_->tileBytes(b, lat, lon, en, raw) && en.kind == KIND_ZLIB) t = decodeRoads(raw, b->path);
  else t = std::make_shared<std::vector<RoadEdge>>();
  impl_->roads.put(key, t, Lru<std::vector<RoadEdge>>::sizeOf(*t));
  return t;
}

std::vector<RoadEdge> Pack::roads(const Box& box, bool local) const {
  std::vector<RoadEdge> out;
  std::unordered_map<uint64_t, bool> seen;
  forCells(box, [&](int la, int lo, int) {
    auto t = roadTile(la, lo, local);
    for (const RoadEdge& e : *t) {
      if (seen.count(e.id)) continue;
      double s = 90, w = 180, n = -90, ea = -180;
      for (const auto& p : e.pts) { s = std::min(s, p.first); n = std::max(n, p.first); w = std::min(w, p.second); ea = std::max(ea, p.second); }
      if (n < box.south || s >= box.north || ea < box.west || w >= box.east) continue;
      seen[e.id] = true;
      out.push_back(e);
    }
  });
  return out;
}

std::vector<AirportRecord> Pack::airports(const Box& box) const {
  std::vector<AirportRecord> out;
  const auto& t = impl_->airports;
  if (t.empty()) return out;
  const uint32_t na = u32(t.data() + 8), nr = u32(t.data() + 12);
  const size_t recAt = 32 + 8ull * kCells, runAt = recAt + 48ull * na, strAt = runAt + 40ull * nr;
  forCells(box, [&](int, int, int cell) {
    const uint32_t first = u32(&t[32 + 8 * cell]), count = u32(&t[36 + 8 * cell]);
    for (uint32_t i = first; i < first + count; i++) {
      const uint8_t* r = &t[recAt + 48ull * i];
      AirportRecord a;
      a.lat = i32(r) * 1e-7; a.lon = i32(r + 4) * 1e-7;
      if (!box.contains(a.lat, a.lon)) continue;
      const int16_t elev = i16(r + 8);
      a.hasElevation = elev != -32768; a.elevation = a.hasElevation ? (float)elev : 0.f;
      a.type = r[10];
      const int nrw = r[11];
      const uint32_t firstRunway = u32(r + 12);
      a.ident = str(t, strAt, u32(r + 16)); a.name = str(t, strAt, u32(r + 20)); a.municipality = str(t, strAt, u32(r + 24));
      a.country = str(t, strAt, u32(r + 28)); a.iata = str(t, strAt, u32(r + 32));
      a.id = u32(r + 36); a.scheduled = u32(r + 40) & 1;
      if (firstRunway + (uint64_t)nrw > nr) continue;
      for (int k = 0; k < nrw; k++) {
        const uint8_t* w = &t[runAt + 40ull * (firstRunway + k)];
        Runway rw;
        // (tables.py RUNWAY: ends 0-15, length 16, width 18, surface 20, flags 21, elevations 22 and 24, heading 26,
        // idents 28 and 32, displaced thresholds 36 and 38)
        rw.endsKnown = w[21] & 4; rw.lighted = w[21] & 1;
        if (rw.endsKnown) { rw.lat1 = i32(w) * 1e-7; rw.lon1 = i32(w + 4) * 1e-7; rw.lat2 = i32(w + 8) * 1e-7; rw.lon2 = i32(w + 12) * 1e-7; }
        rw.length = u16(w + 16); rw.width = u16(w + 18); rw.surface = w[20];
        rw.hasElev1 = i16(w + 22) != -32768; rw.elev1 = rw.hasElev1 ? (float)i16(w + 22) : 0.f;
        rw.hasElev2 = i16(w + 24) != -32768; rw.elev2 = rw.hasElev2 ? (float)i16(w + 24) : 0.f;
        rw.hasHeading = u16(w + 26) != 0xFFFF; rw.heading = rw.hasHeading ? u16(w + 26) * 0.1f : 0.f;
        rw.ident1 = std::string((const char*)w + 28, strnlen((const char*)w + 28, 4));
        rw.ident2 = std::string((const char*)w + 32, strnlen((const char*)w + 32, 4));
        rw.displaced1 = u16(w + 36); rw.displaced2 = u16(w + 38);
        a.runways.push_back(rw);
      }
      out.push_back(std::move(a));
    }
  });
  return out;
}

std::vector<PlaceRecord> Pack::places(const Box& box) const {
  std::vector<PlaceRecord> out;
  const auto& t = impl_->places;
  if (t.empty()) return out;
  const uint32_t np = u32(t.data() + 8);
  const size_t recAt = 32 + 8ull * kCells, strAt = recAt + 24ull * np;
  forCells(box, [&](int, int, int cell) {
    const uint32_t first = u32(&t[32 + 8 * cell]), count = u32(&t[36 + 8 * cell]);
    for (uint32_t i = first; i < first + count; i++) {
      const uint8_t* r = &t[recAt + 24ull * i];
      PlaceRecord p;
      p.lat = i32(r) * 1e-7; p.lon = i32(r + 4) * 1e-7;
      if (!box.contains(p.lat, p.lon)) continue;
      p.population = u32(r + 8); p.name = str(t, strAt, u32(r + 12)); p.id = u32(r + 16); p.kind = r[20];
      p.country = std::string((const char*)r + 22, strnlen((const char*)r + 22, 2));
      out.push_back(std::move(p));
    }
  });
  return out;
}

size_t Pack::airportCount() const { return impl_->airports.empty() ? 0 : u32(impl_->airports.data() + 8); }
size_t Pack::placeCount() const { return impl_->places.empty() ? 0 : u32(impl_->places.data() + 8); }
size_t Pack::cachedBytes() const {
  std::lock_guard<std::mutex> g(impl_->lock);
  return impl_->rasters.bytes + impl_->roads.bytes + impl_->glo90.bytes;
}

std::string glo90Name(int lat, int lon) {
  char b[64];
  snprintf(b, sizeof(b), "Copernicus_DSM_COG_30_%c%02d_00_%c%03d_00_DEM", lat >= 0 ? 'N' : 'S', std::abs(lat), lon >= 0 ? 'E' : 'W', std::abs(lon));
  return b;
}
bool Pack::glo90Listed(int lat, int lon) const {
  const auto& bits = impl_->glo90Bits;
  if (bits.empty() || lat < -90 || lat > 89 || lon < -180 || lon > 179) return false;
  const int i = (lat + 90) * 360 + (lon + 180);
  return (bits[i >> 3] >> (i & 7)) & 1;
}
void Pack::setGlo90Cache(const std::string& dir) { std::lock_guard<std::mutex> g(impl_->lock); impl_->glo90Cache = dir; }
std::string Pack::glo90File(int lat, int lon) const {
  const std::string name = glo90Name(lat, lon) + ".tif";
  long long size = 0;
  if (impl_->files.count("glo90/" + name)) return joinPath(dir_, "glo90/" + name);
  std::string cache;
  { std::lock_guard<std::mutex> g(impl_->lock); cache = impl_->glo90Cache; }
  if (!cache.empty() && fileSize(joinPath(cache, name), size) && size > 0) return joinPath(cache, name);
  return std::string();
}
std::shared_ptr<const Glo90Tile> Pack::glo90Tile(int lat, int lon) const {
  const uint64_t key = tileKey(9, lat, lon);
  {
    std::lock_guard<std::mutex> g(impl_->lock);
    if (auto t = impl_->glo90.get(key)) return t;
  }
  const std::string path = glo90File(lat, lon);
  if (path.empty()) return nullptr;
  std::string why;
  std::shared_ptr<const Glo90Tile> t = readGlo90(path, &why);   // (decoded outside the lock: tens of ms)
  if (!t) return nullptr;
  if (t->lat != lat || t->lon != lon) return nullptr;
  std::lock_guard<std::mutex> g(impl_->lock);
  if (auto again = impl_->glo90.get(key)) return again;
  impl_->glo90.put(key, t, t->bytes());
  return t;
}

// ---------------------------------------------------------------------------------------------------------------
// GLO-90 tiles

std::shared_ptr<Glo90Tile> readGlo90(const std::string& path, std::string* error) {
  auto fail = [&](const std::string& why) -> std::shared_ptr<Glo90Tile> { if (error) *error = path + ": " + why; return nullptr; };
  std::vector<uint8_t> f;
  if (!readFile(path, f)) return fail("can't read");
  if (f.size() < 16 || memcmp(f.data(), "II", 2) != 0 || u16(&f[2]) != 42) return fail("not a little-endian classic TIFF");
  const uint32_t ifd = u32(&f[4]);
  if (ifd + 2 > f.size()) return fail("bad directory offset");
  const int count = u16(&f[ifd]);
  if (ifd + 2 + 12ull * count + 4 > f.size()) return fail("bad directory");
  std::map<int, std::vector<double>> tags;
  for (int i = 0; i < count; i++) {
    const uint8_t* e = &f[ifd + 2 + 12 * i];
    const int tag = u16(e), type = u16(e + 2);
    const uint32_t n = u32(e + 4);
    const int size = type == 3 ? 2 : type == 4 ? 4 : type == 12 ? 8 : type == 16 ? 8 : 1;
    if (n > (1u << 24)) return fail("oversized tag");
    const uint64_t bytes = (uint64_t)size * n;
    const uint8_t* v = e + 8;
    if (bytes > 4) { const uint32_t off = u32(e + 8); if (off + bytes > f.size()) return fail("tag outside the file"); v = &f[off]; }
    std::vector<double> vals;
    if (type == 3 || type == 4 || type == 12 || type == 16) {
      vals.resize(n);
      for (uint32_t k = 0; k < n; k++) {
        if (type == 3) vals[k] = u16(v + 2 * k);
        else if (type == 4) vals[k] = u32(v + 4 * k);
        else if (type == 16) vals[k] = (double)u64(v + 8 * k);
        else { double d; memcpy(&d, v + 8 * k, 8); vals[k] = d; }
      }
    }
    tags[tag] = std::move(vals);
  }
  auto one = [&](int tag, double dflt) { auto it = tags.find(tag); return it == tags.end() || it->second.empty() ? dflt : it->second[0]; };
  const int width = (int)one(256, 0), height = (int)one(257, 0);
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return fail("bad size");
  if (one(258, 0) != 32 || one(339, 1) != 3 || one(277, 1) != 1) return fail("not one Float32 sample per pixel");
  const int compression = (int)one(259, 1), predictor = (int)one(317, 1);
  if (compression != 1 && compression != 8 && compression != 32946) return fail("unsupported compression");
  if (predictor != 1 && predictor != 3) return fail("unsupported predictor");
  const bool tiled = tags.count(322);
  const int tw = tiled ? (int)one(322, 0) : width, th = tiled ? (int)one(323, 0) : (int)one(278, height);
  if (tw <= 0 || th <= 0 || tw > 8192 || th > 8192) return fail("bad tile size");
  const auto& offs = tags[tiled ? 324 : 273];
  const auto& counts = tags[tiled ? 325 : 279];
  const int across = (width + tw - 1) / tw, down = (height + th - 1) / th;
  if ((int)offs.size() < across * down || counts.size() < offs.size()) return fail("tile list too short");
  const auto& scale = tags[33550];
  const auto& tie = tags[33922];
  if (scale.size() < 2 || tie.size() < 6) return fail("no georeference");
  auto t = std::make_shared<Glo90Tile>();
  t->rows = height; t->cols = width;
  t->dLon = scale[0]; t->dLat = scale[1];
  // GLO-90 is PixelIsPoint: the tie point is the first sample itself
  t->west = tie[3] - tie[0] * t->dLon; t->north = tie[4] + tie[1] * t->dLat;
  t->lat = (int)std::floor(t->north - 1e-9); t->lon = (int)std::lround(t->west);
  if (std::abs(t->dLat * 1200.0 - 1.0) > 1e-9 || std::abs(t->west - t->lon) > 1e-6 || std::abs(t->dLon * width - 1.0) > 1e-6) return fail("not a GLO-90 tile's grid");
  const double start = (t->lat + 1 - t->north) / t->dLat;
  t->rowStart = (int)std::lround(start);
  if (std::abs(start - t->rowStart) > 1e-6 || t->rowStart + height > 1200) return fail("rows off the tile's grid");
  t->h.assign((size_t)width * height, 0.f);
  std::vector<uint8_t> buf((size_t)tw * th * 4);
  for (int ty = 0; ty < down; ty++) for (int tx = 0; tx < across; tx++) {
    const int k = ty * across + tx;
    const uint64_t off = (uint64_t)offs[k], n = (uint64_t)counts[k];
    const int rows = tiled ? th : std::min(th, height - ty * th);
    const size_t want = (size_t)tw * rows * 4;
    if (off + n > f.size()) return fail("tile outside the file");
    if (compression == 1) { if (n < want) return fail("short tile"); memcpy(buf.data(), &f[off], want); }
    else if (!inflate(&f[off], n, buf.data(), want)) return fail("damaged tile");
    for (int r = 0; r < rows; r++) {
      uint8_t* row = &buf[(size_t)r * tw * 4];
      if (predictor == 3) {   // (bytes by significance across the row, then byte differences)
        for (int i = 1; i < tw * 4; i++) row[i] = (uint8_t)(row[i] + row[i - 1]);
      }
      const int gy = ty * th + r;
      if (gy >= height) break;
      for (int c = 0; c < tw; c++) {
        const int gx = tx * tw + c;
        if (gx >= width) break;
        uint32_t bits;
        if (predictor == 3) bits = (uint32_t)row[c] << 24 | (uint32_t)row[tw + c] << 16 | (uint32_t)row[2 * tw + c] << 8 | row[3 * tw + c];
        else bits = u32(row + 4 * c);
        float v; memcpy(&v, &bits, 4);
        t->h[(size_t)gy * width + gx] = std::isfinite(v) && v > -12000.f && v < 9000.f ? v : 0.f;
      }
    }
  }
  return t;
}

// ---------------------------------------------------------------------------------------------------------------
// Sampling

namespace {
// Catmull-Rom weights at t in [0, 1)
inline void cubicWeights(double t, double w[4]) {
  const double t2 = t * t, t3 = t2 * t;
  w[0] = -0.5 * t3 + t2 - 0.5 * t;
  w[1] = 1.5 * t3 - 2.5 * t2 + 1.0;
  w[2] = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
  w[3] = 0.5 * t3 - 0.5 * t2;
}
} // namespace

Sampler::Sampler(std::shared_ptr<const Pack> pack, const Box& box, bool fine) : pack_(std::move(pack)), box_(box) {
  // the tiles over the box, a tile beyond it each way (a bicubic sample's reach, and the apron's)
  lat0_ = std::max(-90, (int)std::floor(box.south) - 1);
  const int lat1 = std::min(89, (int)std::ceil(box.north));
  lon0_ = (int)std::floor(box.west) - 1;
  const int lon1 = (int)std::ceil(box.east);
  nLat_ = lat1 - lat0_ + 1; nLon_ = lon1 - lon0_ + 1;
  elev_.resize((size_t)nLat_ * nLon_); cover_.resize(elev_.size()); fine_.resize(elev_.size());
  for (int a = 0; a < nLat_; a++) for (int b = 0; b < nLon_; b++) {
    const int lat = lat0_ + a, lon = ((lon0_ + b + 180) % 360 + 360) % 360 - 180;
    const size_t i = (size_t)a * nLon_ + b;
    elev_[i] = pack_->elevTile(lat, lon);
    cover_[i] = pack_->coverTile(lat, lon);
    if (fine && pack_->glo90Listed(lat, lon)) { fine_[i] = pack_->glo90Tile(lat, lon); fineCount_ += fine_[i] != nullptr; }
  }
}

int Sampler::slot(int lat, int lon) const {
  const int a = lat - lat0_;
  int b = lon - lon0_;
  if (b < 0) b += 360; else if (b >= 360) b -= 360;
  if (a < 0 || a >= nLat_ || b < 0 || b >= nLon_) return -1;
  return a * nLon_ + b;
}

float Sampler::etopoSample(long row, long col) const {
  // global indices: row 0 centred 0.5 / 120 degree below the north pole, col 0 that far east of 180 W
  const long rows = 180L * kElevSamples, cols = 360L * kElevSamples;
  row = std::clamp(row, 0L, rows - 1);
  col = ((col % cols) + cols) % cols;
  const int lat = 89 - (int)(row / kElevSamples), lon = (int)(col / kElevSamples) - 180;
  const int s = slot(lat, lon);
  if (s < 0 || !elev_[s]) return 0.f;
  return (float)elev_[s]->at((int)(row % kElevSamples), (int)(col % kElevSamples));
}

float Sampler::coarseElevation(double lat, double lon) const {
  const double fr = (90.0 - lat) * kElevSamples - 0.5, fc = (lon + 180.0) * kElevSamples - 0.5;
  const long r0 = (long)std::floor(fr), c0 = (long)std::floor(fc);
  double wr[4], wc[4];
  cubicWeights(fr - r0, wr); cubicWeights(fc - c0, wc);
  double h = 0;
  for (int i = 0; i < 4; i++) {
    double row = 0;
    for (int j = 0; j < 4; j++) row += wc[j] * etopoSample(r0 - 1 + i, c0 - 1 + j);
    h += wr[i] * row;
  }
  return (float)h;
}

// One GLO-90 sample row of the tile covering it (global row from the north pole at 1/rows per degree), at a longitude
// (linear between that row's samples: exact on a sample, and across a change in columns per degree at a band edge)
float Sampler::glo90Point(int, int, long row, double lonDeg, bool& ok) const {
  const long perDeg = 1200;
  const int lat = 89 - (int)std::floor((double)row / perDeg);
  const int lonT = (int)std::floor(lonDeg);
  const int s = slot(lat, lonT);
  if (s < 0) { ok = false; return 0.f; }
  const auto& t = fine_[s];
  if (!t) {   // (a hole: Copernicus has no tile there - open sea - or it isn't loaded)
    if (pack_->glo90Listed(lat, ((lonT + 180) % 360 + 360) % 360 - 180)) ok = false;
    return 0.f;
  }
  const int r = (int)(row - (long)(89 - lat) * perDeg) - t->rowStart;
  if (r < 0 || r >= t->rows) { ok = false; return 0.f; }   // (a cropped tile: not this row)
  const double fc = (lonDeg - t->west) / t->dLon;
  const int c0 = std::clamp((int)std::floor(fc), 0, t->cols - 1);
  const double f = fc - c0;
  const float a = t->at(r, c0);
  if (f <= 1e-9) return a;
  float b;
  if (c0 + 1 < t->cols) b = t->at(r, c0 + 1);
  else { bool ok2 = true; b = glo90Point(0, 0, row, t->west + 1.0 + 1e-12, ok2); if (!ok2) b = a; }
  return (float)(a + (b - a) * f);
}

bool Sampler::fine(double lat, double lon) const {
  const int s = slot((int)std::floor(lat), (int)std::floor(lon));
  if (s < 0 || !fine_[s]) return false;
  const auto& t = *fine_[s];
  const long r = (long)std::floor((90.0 - lat) * 1200.0) - (long)(89 - t.lat) * 1200 - t.rowStart;
  return r >= 1 && r + 2 < t.rows;   // (a cropped tile's rows, with a bicubic's reach)
}

float Sampler::elevation(double lat, double lon) const {
  const int s = slot((int)std::floor(lat), (int)std::floor(lon));
  if (s < 0 || !fine_[s]) return coarseElevation(lat, lon);
  const auto& t = *fine_[s];
  // rows run 1/rows of a degree apart from the north pole; columns this tile's
  const double perDeg = 1200.0;
  const double fr = (90.0 - lat) * perDeg;
  const long r0 = (long)std::floor(fr);
  double wr[4], wc[4];
  cubicWeights(fr - r0, wr);
  const double fc = (lon - t.west) / t.dLon;
  const long c0 = (long)std::floor(fc);
  cubicWeights(fc - c0, wc);
  bool ok = true;
  double h = 0;
  for (int i = 0; i < 4; i++) {
    double row = 0;
    for (int j = 0; j < 4; j++) row += wc[j] * glo90Point(0, 0, r0 - 1 + i, t.west + (c0 - 1 + j) * t.dLon, ok);
    h += wr[i] * row;
  }
  if (!ok) return coarseElevation(lat, lon);   // (next to a tile that is listed but not on disk)
  return (float)h;
}

int Sampler::cover(double lat, double lon) const {
  const int la = (int)std::floor(lat), lo = (int)std::floor(lon);
  const int s = slot(la, lo);
  if (s < 0 || !cover_[s]) return COVER_UNKNOWN;
  const auto& t = *cover_[s];
  const int r = std::clamp((int)std::floor((la + 1 - lat) * kCoverSamples), 0, kCoverSamples - 1);
  const int c = std::clamp((int)std::floor((lon - lo) * kCoverSamples), 0, kCoverSamples - 1);
  return t.at(r, c);
}
} // namespace earth
