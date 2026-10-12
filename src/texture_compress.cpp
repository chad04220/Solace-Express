// Somnium Volandi - BC3 (DXT5) block compression (texture_compress.h): a BC1 colour block (two 5:6:5 endpoints along the
// block's principal colour axis, inset a little, then refined by least squares; four colours, 2 bits a texel) and a
// BC4 alpha block (its two extremes, eight values, 3 bits a texel) for each 4x4.
#include "texture_compress.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
inline int expand5(int v) { return (v << 3) | (v >> 2); }
inline int expand6(int v) { return (v << 2) | (v >> 4); }
inline uint16_t pack565(const float c[3]) {
  const int r = std::clamp((int)lrintf(c[0] * 31.f / 255.f), 0, 31), g = std::clamp((int)lrintf(c[1] * 63.f / 255.f), 0, 63),
            b = std::clamp((int)lrintf(c[2] * 31.f / 255.f), 0, 31);
  return (uint16_t)((r << 11) | (g << 5) | b);
}
inline void unpack565(uint16_t v, int c[3]) { c[0] = expand5(v >> 11); c[1] = expand6((v >> 5) & 63); c[2] = expand5(v & 31); }

// The 4-colour palette of two endpoints (BC3's colour block is always 4-colour)
void palette(uint16_t c0, uint16_t c1, int p[4][3]) {
  unpack565(c0, p[0]); unpack565(c1, p[1]);
  for (int k = 0; k < 3; k++) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
}

// Each texel's nearest palette entry; the summed squared error
uint32_t assign(const uint8_t px[16][4], uint16_t c0, uint16_t c1, uint32_t& bits) {
  int p[4][3]; palette(c0, c1, p);
  uint32_t err = 0; bits = 0;
  for (int i = 0; i < 16; i++) {
    int best = 0, bd = 1 << 30;
    for (int k = 0; k < 4; k++) {
      const int dr = px[i][0] - p[k][0], dg = px[i][1] - p[k][1], db = px[i][2] - p[k][2], d = dr * dr + dg * dg + db * db;
      if (d < bd) { bd = d; best = k; }
    }
    bits |= (uint32_t)best << (2 * i); err += (uint32_t)bd;
  }
  return err;
}

void encodeColour(const uint8_t px[16][4], uint8_t* out) {
  float mean[3] = {0, 0, 0};
  for (int i = 0; i < 16; i++) for (int k = 0; k < 3; k++) mean[k] += px[i][k] / 16.f;
  float cov[6] = {0, 0, 0, 0, 0, 0};   // (rr, rg, rb, gg, gb, bb)
  for (int i = 0; i < 16; i++) {
    const float r = px[i][0] - mean[0], g = px[i][1] - mean[1], b = px[i][2] - mean[2];
    cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
  }
  float ax[3] = {1.f, 1.f, 1.f};   // the principal axis, by a few power iterations
  for (int it = 0; it < 6; it++) {
    const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2], y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2],
                z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
    const float m = std::max(std::max(fabsf(x), fabsf(y)), fabsf(z));
    if (m < 1e-6f) break;
    ax[0] = x / m; ax[1] = y / m; ax[2] = z / m;
  }
  const float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
  for (float& v : ax) v /= std::max(al, 1e-6f);
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < 16; i++) {
    const float t = (px[i][0] - mean[0]) * ax[0] + (px[i][1] - mean[1]) * ax[1] + (px[i][2] - mean[2]) * ax[2];
    lo = std::min(lo, t); hi = std::max(hi, t);
  }
  const float inset = (hi - lo) / 16.f; lo += inset; hi -= inset;
  float e0[3], e1[3];
  for (int k = 0; k < 3; k++) { e0[k] = std::clamp(mean[k] + ax[k] * hi, 0.f, 255.f); e1[k] = std::clamp(mean[k] + ax[k] * lo, 0.f, 255.f); }
  uint16_t c0 = pack565(e0), c1 = pack565(e1);
  uint32_t bits, err = assign(px, c0, c1, bits);
  // one least-squares refinement of the endpoints for the indices chosen (texel = a c0 + b c1)
  {
    static const float wa[4] = {1.f, 0.f, 2.f / 3.f, 1.f / 3.f};
    float aa = 0, bb = 0, ab = 0, ax2[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++) {
      const float a = wa[(bits >> (2 * i)) & 3], b = 1.f - a;
      aa += a * a; bb += b * b; ab += a * b;
      for (int k = 0; k < 3; k++) { ax2[k] += a * px[i][k]; bx[k] += b * px[i][k]; }
    }
    const float det = aa * bb - ab * ab;
    if (fabsf(det) > 1e-4f) {
      float r0[3], r1[3];
      for (int k = 0; k < 3; k++) {
        r0[k] = std::clamp((ax2[k] * bb - bx[k] * ab) / det, 0.f, 255.f);
        r1[k] = std::clamp((bx[k] * aa - ax2[k] * ab) / det, 0.f, 255.f);
      }
      const uint16_t n0 = pack565(r0), n1 = pack565(r1);
      uint32_t nb; const uint32_t ne = assign(px, n0, n1, nb);
      if (ne < err) { c0 = n0; c1 = n1; bits = nb; err = ne; }
    }
  }
  if (c0 < c1) {   // (c0 > c1 kept, as a BC1 block's 4-colour mode reads it: indices 0<->1, 2<->3 swapped with them)
    std::swap(c0, c1);
    uint32_t sw = 0;
    for (int i = 0; i < 16; i++) sw |= (((bits >> (2 * i)) & 3) ^ 1u) << (2 * i);
    bits = sw;
  } else if (c0 == c1) bits = 0;
  out[0] = (uint8_t)(c0 & 255); out[1] = (uint8_t)(c0 >> 8); out[2] = (uint8_t)(c1 & 255); out[3] = (uint8_t)(c1 >> 8);
  for (int k = 0; k < 4; k++) out[4 + k] = (uint8_t)(bits >> (8 * k));
}

void encodeAlpha(const uint8_t px[16][4], uint8_t* out) {
  int lo = 255, hi = 0;
  for (int i = 0; i < 16; i++) { lo = std::min(lo, (int)px[i][3]); hi = std::max(hi, (int)px[i][3]); }
  out[0] = (uint8_t)hi; out[1] = (uint8_t)lo;
  uint64_t bits = 0;
  if (hi > lo) {   // eight values: a0, a1, then six between from a0 to a1
    int pal[8] = {hi, lo};
    for (int k = 1; k <= 6; k++) pal[k + 1] = ((7 - k) * hi + k * lo) / 7;
    for (int i = 0; i < 16; i++) {
      int best = 0, bd = 1 << 30;
      for (int k = 0; k < 8; k++) { const int d = abs(px[i][3] - pal[k]); if (d < bd) { bd = d; best = k; } }
      bits |= (uint64_t)best << (3 * i);
    }
  }
  for (int k = 0; k < 6; k++) out[2 + k] = (uint8_t)(bits >> (8 * k));
}
}  // namespace

size_t bc3LevelBytes(int w, int h) { return (size_t)std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * 16; }

void bc3Encode(const uint8_t* rgba, int w, int h, uint8_t* out) {
  const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
  for (int by = 0; by < bh; by++)
    for (int bx = 0; bx < bw; bx++) {
      uint8_t px[16][4];
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
          const int sx = std::min(bx * 4 + x, w - 1), sy = std::min(by * 4 + y, h - 1);
          memcpy(px[y * 4 + x], rgba + ((size_t)sy * w + sx) * 4, 4);
        }
      uint8_t* b = out + ((size_t)by * bw + bx) * 16;
      encodeAlpha(px, b);
      encodeColour(px, b + 8);
    }
}

void bc3DecodeBlock(const uint8_t* block, uint8_t rgba[64]) {
  const int a0 = block[0], a1 = block[1];
  int ap[8] = {a0, a1};
  if (a0 > a1) for (int k = 1; k <= 6; k++) ap[k + 1] = ((7 - k) * a0 + k * a1) / 7;
  else { for (int k = 1; k <= 4; k++) ap[k + 1] = ((5 - k) * a0 + k * a1) / 5; ap[6] = 0; ap[7] = 255; }
  uint64_t abits = 0; for (int k = 0; k < 6; k++) abits |= (uint64_t)block[2 + k] << (8 * k);
  const uint8_t* c = block + 8;
  const uint16_t c0 = (uint16_t)(c[0] | (c[1] << 8)), c1 = (uint16_t)(c[2] | (c[3] << 8));
  int p[4][3]; palette(c0, c1, p);
  const uint32_t cbits = (uint32_t)c[4] | ((uint32_t)c[5] << 8) | ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
  for (int i = 0; i < 16; i++) {
    const int ci = (cbits >> (2 * i)) & 3;
    rgba[i * 4] = (uint8_t)p[ci][0]; rgba[i * 4 + 1] = (uint8_t)p[ci][1]; rgba[i * 4 + 2] = (uint8_t)p[ci][2];
    rgba[i * 4 + 3] = (uint8_t)ap[(abits >> (3 * i)) & 7];
  }
}

std::vector<uint8_t> mipDown(const uint8_t* rgba, int w, int h, int& nw, int& nh) {
  nw = std::max(1, w / 2); nh = std::max(1, h / 2);
  std::vector<uint8_t> o((size_t)nw * nh * 4);
  for (int y = 0; y < nh; y++)
    for (int x = 0; x < nw; x++) {
      const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
      for (int k = 0; k < 4; k++) {
        const int s = rgba[((size_t)y0 * w + x0) * 4 + k] + rgba[((size_t)y0 * w + x1) * 4 + k] + rgba[((size_t)y1 * w + x0) * 4 + k] + rgba[((size_t)y1 * w + x1) * 4 + k];
        o[((size_t)y * nw + x) * 4 + k] = (uint8_t)((s + 2) / 4);
      }
    }
  return o;
}
