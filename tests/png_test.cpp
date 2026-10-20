// PNG files (png_write.h): what is written reads back exactly (stb_image) - one pixel, an odd size, a flat picture,
// pure noise (no repeats to find) and a picture like the game's (a sky's gradient, textured ground, flat panels);
// the game-like one, and a flat one, far smaller than their raw pixels; and how long a 1080p frame takes
#define STB_IMAGE_IMPLEMENTATION
#include "../src/third_party/stb_image.h"
#include "../src/png_write.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

// a picture like a frame of the game: sky above (a smooth gradient), ground below (value noise in greens and browns),
// a flat dark panel with a bright border (the HUD's)
static std::vector<uint8_t> gameLike(int W, int H) {
  std::vector<uint8_t> px((size_t)W * H * 3);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    uint8_t* p = &px[((size_t)y * W + x) * 3];
    const float v = (float)y / H;   // (bottom row first: y = 0 is the bottom)
    if (v > 0.45f) { const float t = (v - 0.45f) / 0.55f; p[0] = (uint8_t)(150 - 90 * t); p[1] = (uint8_t)(190 - 70 * t); p[2] = (uint8_t)(235 - 20 * t); }
    else {
      const float n = 0.5f + 0.25f * sinf(x * 0.05f) * cosf(y * 0.07f) + 0.25f * sinf(x * 0.013f + y * 0.021f);
      const int j = (int)(rnd() % 9) - 4;
      p[0] = (uint8_t)std::min(255, std::max(0, (int)(70 + 60 * n) + j)); p[1] = (uint8_t)std::min(255, std::max(0, (int)(95 + 70 * n) + j)); p[2] = (uint8_t)std::min(255, std::max(0, (int)(45 + 30 * n) + j));
    }
    if (x > W / 20 && x < W / 4 && y > H / 20 && y < H / 6) {
      const bool edge = x == W / 20 + 1 || x == W / 4 - 1 || y == H / 20 + 1 || y == H / 6 - 1;
      p[0] = edge ? 232 : 10; p[1] = edge ? 188 : 16; p[2] = edge ? 120 : 24;
    }
  }
  return px;
}

int main() {
  int fails = 0;
  auto check = [&](bool ok, const std::string& what) { printf("%s: %s\n", what.c_str(), ok ? "ok" : "FAIL"); fails += !ok; };
  auto roundTrip = [&](int W, int H, const std::vector<uint8_t>& px, size_t* bytes) {
    const std::vector<uint8_t> png = encodePNG(W, H, px);
    if (bytes) *bytes = png.size();
    int w = 0, h = 0, n = 0;
    unsigned char* got = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 3);
    if (!got) { printf("  stb_image: %s\n", stbi_failure_reason()); return false; }
    bool same = w == W && h == H;
    for (int y = 0; same && y < H; y++)
      for (int x = 0; same && x < W * 3; x++) same = got[(size_t)y * W * 3 + x] == px[(size_t)(H - 1 - y) * W * 3 + x];   // (written bottom row first, read top first)
    stbi_image_free(got);
    return same;
  };
  {
    std::vector<uint8_t> one = {200, 100, 50};
    check(roundTrip(1, 1, one, nullptr), "One pixel reads back");
  }
  {
    std::vector<uint8_t> odd(7 * 5 * 3); for (auto& v : odd) v = (uint8_t)rnd();
    check(roundTrip(7, 5, odd, nullptr), "7x5 of noise reads back");
  }
  {
    std::vector<uint8_t> flat((size_t)640 * 360 * 3, 37); size_t b = 0;
    const bool ok = roundTrip(640, 360, flat, &b);
    check(ok && b < flat.size() / 100, "A flat 640x360 picture reads back, at under 1% of its pixels (" + std::to_string(b) + " bytes)");
  }
  {
    std::vector<uint8_t> noise((size_t)300 * 200 * 3); for (auto& v : noise) v = (uint8_t)rnd(); size_t b = 0;
    const bool ok = roundTrip(300, 200, noise, &b);
    check(ok && b < noise.size() * 1.15, "300x200 of pure noise reads back, hardly larger than its pixels (" + std::to_string(b) + " bytes)");
  }
  {
    const std::vector<uint8_t> g = gameLike(640, 360); size_t b = 0;
    const bool ok = roundTrip(640, 360, g, &b);
    check(ok && b < g.size() / 2, "A game-like 640x360 frame reads back, at under half its pixels (" + std::to_string(b * 100 / g.size()) + "%)");
  }
  {
    const std::vector<uint8_t> g = gameLike(1920, 1080);
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<uint8_t> png = encodePNG(1920, 1080, g);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const char* path = "png_test.png";
    const bool wrote = writePNG(path, 1920, 1080, g);
    int w = 0, h = 0, n = 0;
    unsigned char* got = stbi_load(path, &w, &h, &n, 3);
    const bool read = got && w == 1920 && h == 1080;
    if (got) stbi_image_free(got);
    remove(path);
    printf("  a 1080p frame: %zu KB of %zu KB raw, in %.0f ms\n", png.size() / 1024, g.size() / 1024, ms);
    check(wrote && read, "A 1080p frame written to a file reads back from it");
  }
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
