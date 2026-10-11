// The BC3 encoder the 2K ground photographs are packed with (texture_compress.h): a flat block comes back exactly (to
// 5:6:5 in colour), a two-colour block and a smooth gradient closely, alpha to within its eight levels; the block layout
// is DXT5's (alpha first, the colour block's endpoints in 4-colour order); level sizes and the mip chain down to 1x1.
#include "../src/texture_compress.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { std::printf("FAIL: %s\n", what); failures++; } }

// the PSNR of an image's round trip through BC3 (all four channels)
static double roundTrip(const std::vector<uint8_t>& img, int w, int h, int ch0 = 0, int ch1 = 4) {
  std::vector<uint8_t> blocks(bc3LevelBytes(w, h));
  bc3Encode(img.data(), w, h, blocks.data());
  double se = 0; int n = 0;
  const int bw = (w + 3) / 4;
  for (int by = 0; by < (h + 3) / 4; by++)
    for (int bx = 0; bx < bw; bx++) {
      uint8_t d[64]; bc3DecodeBlock(&blocks[((size_t)by * bw + bx) * 16], d);
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
          const int sx = bx * 4 + x, sy = by * 4 + y;
          if (sx >= w || sy >= h) continue;
          for (int k = ch0; k < ch1; k++) { const double e = (double)d[(y * 4 + x) * 4 + k] - img[((size_t)sy * w + sx) * 4 + k]; se += e * e; n++; }
        }
    }
  const double mse = se / std::max(n, 1);
  return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main() {
  check(bc3LevelBytes(2048, 2048) == 2048u * 2048u, "a 2K level a byte a texel");
  check(bc3LevelBytes(2, 2) == 16 && bc3LevelBytes(1, 1) == 16 && bc3LevelBytes(6, 5) == 64, "a small level's whole blocks");
  // flat: exact where 5:6:5 holds the colour; alpha exact
  {
    std::vector<uint8_t> img(16 * 4);
    for (int i = 0; i < 16; i++) { img[i * 4] = 8 * 20 + 4; img[i * 4 + 1] = 4 * 40 + 2; img[i * 4 + 2] = 8 * 10 + 4; img[i * 4 + 3] = 77; }
    std::vector<uint8_t> b(16); bc3Encode(img.data(), 4, 4, b.data());
    uint8_t d[64]; bc3DecodeBlock(b.data(), d);
    bool same = true; for (int i = 0; i < 16; i++) same = same && d[i * 4 + 3] == 77 && abs(d[i * 4] - img[i * 4]) <= 4 && abs(d[i * 4 + 1] - img[i * 4 + 1]) <= 2 && abs(d[i * 4 + 2] - img[i * 4 + 2]) <= 4;
    check(same, "a flat block back as it was");
    const unsigned c0 = b[8] | (b[9] << 8), c1 = b[10] | (b[11] << 8);
    check(c0 >= c1, "the colour block's endpoints in 4-colour order");
  }
  // two colours, two alphas: exact to the endpoints' precision
  {
    std::vector<uint8_t> img(16 * 4);
    for (int i = 0; i < 16; i++) { const bool a = (i * 7 % 5) < 2; img[i * 4] = a ? 255 : 0; img[i * 4 + 1] = a ? 255 : 0; img[i * 4 + 2] = a ? 255 : 0; img[i * 4 + 3] = a ? 200 : 30; }
    check(roundTrip(img, 4, 4) > 45.0, "a two-colour block back exactly");
  }
  // smooth gradients and noise, the size of a mip chain's last levels and a whole odd image
  {
    const int w = 64, h = 48;
    std::vector<uint8_t> img((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        uint8_t* p = &img[((size_t)y * w + x) * 4];
        p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 5); p[2] = (uint8_t)((x + y) * 2); p[3] = (uint8_t)(255 - x * 3);
      }
    const double ps = roundTrip(img, w, h);
    std::printf("gradient: %.1f dB\n", ps);
    check(ps > 36.0, "a smooth gradient close (over 36 dB)");
    srand(7);
    for (auto& v : img) v = (uint8_t)(100 + rand() % 40);   // (a grass-like texture: noise in a narrow band)
    const double pn = roundTrip(img, w, h);
    std::printf("noise: %.1f dB\n", pn);
    check(pn > 28.0, "fine noise close (over 28 dB)");
    std::vector<uint8_t> odd((size_t)7 * 5 * 4, 132);   // (132: a value 5:6:5 holds)
    check(roundTrip(odd, 7, 5) > 45.0, "a level not a multiple of 4 across");
  }
  // the mip chain: box-filtered, halving to 1x1
  {
    std::vector<uint8_t> img((size_t)8 * 8 * 4);
    for (size_t i = 0; i < img.size(); i++) img[i] = (uint8_t)((i / 4) % 2 ? 200 : 100);
    int w = 8, h = 8, levels = 1;
    std::vector<uint8_t> cur = img;
    while (w > 1 || h > 1) { int nw, nh; cur = mipDown(cur.data(), w, h, nw, nh); w = nw; h = nh; levels++; }
    check(levels == 4 && w == 1 && h == 1 && cur[0] == 150, "the mip chain to 1x1, each texel its 2x2's mean");
  }
  std::printf("texture compression: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
