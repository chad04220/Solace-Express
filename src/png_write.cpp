// Somnium Volandi - PNG files (png_write.h): RFC 2083's filters, RFC 1950/1951's zlib stream with one fixed-Huffman block
#include "png_write.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
struct Bits {
  std::vector<uint8_t>& out;
  uint32_t acc = 0; int n = 0;
  void put(uint32_t v, int count) { acc |= v << n; n += count; while (n >= 8) { out.push_back((uint8_t)acc); acc >>= 8; n -= 8; } }
  void huff(uint32_t code, int len) { uint32_t r = 0; for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; } put(r, len); }   // (a code goes most significant bit first)
  void flush() { if (n > 0) out.push_back((uint8_t)acc); acc = 0; n = 0; }
};

void symbol(Bits& b, int s) {   // the fixed literal / length code (RFC 1951, 3.2.6)
  if (s < 144) b.huff(0x30 + s, 8);
  else if (s < 256) b.huff(0x190 + s - 144, 9);
  else if (s < 280) b.huff(s - 256, 7);
  else b.huff(0xC0 + s - 280, 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void repeat(Bits& b, int len, int dist) {
  int li = 28; while (kLenBase[li] > len) li--;
  symbol(b, 257 + li);
  if (kLenExtra[li]) b.put((uint32_t)(len - kLenBase[li]), kLenExtra[li]);
  int di = 29; while (kDistBase[di] > dist) di--;
  b.huff((uint32_t)di, 5);
  if (kDistExtra[di]) b.put((uint32_t)(dist - kDistBase[di]), kDistExtra[di]);
}

void deflate(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
  Bits b{out};
  b.put(1, 1); b.put(1, 2);   // the last block, fixed Huffman codes: the whole stream in one
  const int kWin = 32768, kHashBits = 15, kChain = 24;
  std::vector<int32_t> head((size_t)1 << kHashBits, -1), prev(kWin, -1);
  auto hash = [&](size_t i) { return (uint32_t)((d[i] | d[i + 1] << 8 | d[i + 2] << 16) * 2654435761u) >> (32 - kHashBits); };
  auto insert = [&](size_t i) { if (i + 3 > n) return; const uint32_t h = hash(i); prev[i & (kWin - 1)] = head[h]; head[h] = (int32_t)i; };
  size_t i = 0;
  while (i < n) {
    int bestLen = 0, bestDist = 0;
    if (i + 3 <= n) {
      const size_t maxLen = std::min<size_t>(258, n - i);
      int32_t cand = head[hash(i)];
      for (int chain = kChain; cand >= 0 && (int64_t)i - cand <= kWin && chain > 0; chain--) {
        if (d[cand + bestLen] == d[i + bestLen]) {   // (a candidate that can't beat the best is passed over at once)
          size_t l = 0;
          while (l < maxLen && d[cand + l] == d[i + l]) l++;
          if ((int)l > bestLen) { bestLen = (int)l; bestDist = (int)(i - cand); if (l == maxLen) break; }
        }
        cand = prev[cand & (kWin - 1)];
      }
    }
    if (bestLen >= 3) {
      repeat(b, bestLen, bestDist);
      for (int k = 0; k < bestLen; k++) insert(i + k);
      i += bestLen;
    } else {
      symbol(b, d[i]);
      insert(i);
      i++;
    }
  }
  symbol(b, 256);   // (the end of the block)
  b.flush();
}

int paeth(int a, int b, int c) {
  const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint32_t crc32(const uint8_t* d, size_t n, uint32_t c) {
  static uint32_t t[256]; static bool init = false;
  if (!init) { for (uint32_t k = 0; k < 256; k++) { uint32_t v = k; for (int j = 0; j < 8; j++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; t[k] = v; } init = true; }
  c = ~c;
  for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
  return ~c;
}
}

std::vector<uint8_t> encodePNG(int W, int H, const std::vector<uint8_t>& px) {
  // the rows, top first, each led by the filter that leaves the smallest residuals (the sum of their sizes as signed bytes)
  const size_t row = (size_t)W * 3;
  std::vector<uint8_t> raw;
  raw.reserve((row + 1) * H);
  std::vector<uint8_t> cand[4];
  for (auto& c : cand) c.resize(row);
  static const uint8_t kType[4] = {0, 1, 2, 4};
  for (int y = H - 1; y >= 0; y--) {
    const uint8_t* cur = px.data() + (size_t)y * row;
    const uint8_t* up = y < H - 1 ? px.data() + (size_t)(y + 1) * row : nullptr;   // (the row above in the picture)
    long best = -1; int pick = 0;
    for (int f = 0; f < 4; f++) {
      long sum = 0;
      for (size_t x = 0; x < row; x++) {
        const int a = x >= 3 ? cur[x - 3] : 0, b = up ? up[x] : 0, c = x >= 3 && up ? up[x - 3] : 0;
        const int pred = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : paeth(a, b, c);
        const uint8_t v = (uint8_t)(cur[x] - pred);
        cand[f][x] = v;
        sum += v < 128 ? v : 256 - v;
      }
      if (best < 0 || sum < best) { best = sum; pick = f; }
    }
    raw.push_back(kType[pick]);
    raw.insert(raw.end(), cand[pick].begin(), cand[pick].end());
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  deflate(raw.data(), raw.size(), z);
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  const uint32_t ad = (b << 16) | a;
  z.push_back((uint8_t)(ad >> 24)); z.push_back((uint8_t)(ad >> 16)); z.push_back((uint8_t)(ad >> 8)); z.push_back((uint8_t)ad);
  std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
  auto be32 = [&](uint32_t v) { png.push_back((uint8_t)(v >> 24)); png.push_back((uint8_t)(v >> 16)); png.push_back((uint8_t)(v >> 8)); png.push_back((uint8_t)v); };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    be32((uint32_t)d.size());
    const size_t at = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), d.begin(), d.end());
    be32(crc32(png.data() + at, png.size() - at, 0));
  };
  const std::vector<uint8_t> ih = {(uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W, (uint8_t)(H >> 24), (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
  chunk("IHDR", ih); chunk("IDAT", z); chunk("IEND", {});
  return png;
}

bool writePNG(const char* path, int W, int H, const std::vector<uint8_t>& px) {
  const std::vector<uint8_t> png = encodePNG(W, H, px);
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  return fclose(f) == 0 && ok;
}
