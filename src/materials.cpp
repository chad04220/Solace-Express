// The material texture layers every surface samples (Renderer::genMaterials uploads them as two texture arrays): per
// layer an albedo (stored as the square root of linear, alpha the roughness) and a normal map (x, y as DirectX's,
// blue the height, alpha the ambient occlusion). A layer comes from the scanned set in assets/materials when it has
// one (tools/pack_materials.py packs it) and from the procedural generator here when it doesn't - and the generator
// is what the packer matches each scan's average colour, roughness and relief to (tools/material_dump).
#include "materials.h"
#include "world.h"   // hash2i
#include <vector>

static const char* const kNames[kMatLayers] = {
  "grass", "forest", "rock", "sand", "snow", "asphalt", "gravel", "dirt", "concrete", "tiles", "slate", "plaster", "brick",
  "leaves", "needles", "paint", "metal", "rubber", "plastic", "fabric", "carpet", "leather", "corrugated", "crop", "wheat",
  "bark", "planks", "litter", "shingles", "siding"};
const char* materialName(int l) { return l >= 0 && l < kMatLayers ? kNames[l] : "?"; }

static const int TS = kMatTS;
static float pnoise(float x, float y, int P, int seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
  auto h = [&](int i, int j) { i = ((i % P) + P) % P; j = ((j % P) + P) % P; return hash2i(i + seed * 1013, j - seed * 7919); };
  float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  return lerpf(lerpf(h(ix, iy), h(ix + 1, iy), ux), lerpf(h(ix, iy + 1), h(ix + 1, iy + 1), ux), uy);
}
static float pfbm(float u, float v, int baseP, int oct, int seed, float gain = 0.5f) {
  float s = 0, a = 0.5f, n = 0; int P = baseP;
  for (int i = 0; i < oct; i++) { s += a * pnoise(u * P, v * P, P, seed + i); n += a; a *= gain; P *= 2; }
  return s / n;
}
static float pworley(float u, float v, int P, int seed, float* id = nullptr) {
  float x = u * P, y = v * P; int ix = (int)floorf(x), iy = (int)floorf(y);
  float best = 9, bid = 0;
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    int cx = ix + i, cy = iy + j; int wx = ((cx % P) + P) % P, wy = ((cy % P) + P) % P;
    float px = cx + hash2i(wx + seed, wy), py = cy + hash2i(wx, wy + seed * 3);
    float d = (px - x) * (px - x) + (py - y) * (py - y);
    if (d < best) { best = d; bid = hash2i(wx * 7 + seed, wy * 13); }
  }
  if (id) *id = bid;
  return sqrtf(best);
}

void materialProcLayer(int l, uint8_t* alb, uint8_t* nrm) {
  std::vector<float> hgt((size_t)TS * TS);
  for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) {
    float u = (x + 0.5f) / TS, v = (y + 0.5f) / TS;
    vec3 c; float rough = 0.9f, h = 0;
    switch (l) {
      case 0: {  // grass
        float n1 = pfbm(u, v, 4, 5, 11), n2 = pfbm(u, v, 32, 3, 12), blades = pnoise(u * 256, v * 64, 256, 13) * pnoise(u * 96, v * 256, 96, 14);
        c = lerp(vec3(0.20f, 0.33f, 0.09f), vec3(0.33f, 0.45f, 0.14f), n1) * (0.8f + 0.4f * n2);
        c = lerp(c, vec3(0.42f, 0.40f, 0.22f), smoothstepf(0.62f, 0.75f, n1) * 0.5f);
        c = c * (0.85f + 0.3f * blades);
        h = n2 * 0.5f + blades * 0.5f; rough = 0.88f + 0.08f * n2; break; }
      case 1: {  // forest canopy
        float id; float w = pworley(u, v, 10, 21, &id); float w2 = pworley(u, v, 24, 22);
        float crown = 1.f - smoothstepf(0.0f, 0.75f, w);
        float leaf = pfbm(u, v, 64, 3, 23);
        c = lerp(vec3(0.05f, 0.11f, 0.04f), vec3(0.14f, 0.26f, 0.08f), crown * (0.6f + 0.4f * id)) * (0.75f + 0.5f * leaf);
        c = c * (0.85f + 0.3f * (1 - w2));
        h = crown * 0.8f + leaf * 0.2f; rough = 0.92f; break; }
      case 2: {  // rock
        float n = pfbm(u, v, 4, 6, 31, 0.55f), strata = sinf((v * 24 + n * 6) * PI) * 0.5f + 0.5f;
        float cr = pworley(u, v, 8, 32);
        c = lerp(vec3(0.30f, 0.28f, 0.26f), vec3(0.52f, 0.48f, 0.42f), n) * (0.85f + 0.15f * strata);
        c = c * (0.7f + 0.3f * smoothstepf(0.02f, 0.12f, cr));
        h = n * 0.7f + strata * 0.15f + smoothstepf(0.0f, 0.15f, cr) * 0.15f; rough = 0.75f + 0.2f * n; break; }
      case 3: {  // sand
        float n = pfbm(u, v, 8, 4, 41), rip = sinf((u * 40 + pfbm(u, v, 4, 3, 42) * 8) * PI * 2) * 0.5f + 0.5f, g = pnoise(u * 400, v * 400, 400, 43);
        c = lerp(vec3(0.72f, 0.64f, 0.48f), vec3(0.86f, 0.80f, 0.64f), n) * (0.92f + 0.12f * g);
        h = rip * 0.4f + n * 0.4f + g * 0.2f; rough = 0.85f; break; }
      case 4: {  // snow
        float n = pfbm(u, v, 4, 5, 51), g = pnoise(u * 300, v * 300, 300, 52);
        c = lerp(vec3(0.80f, 0.84f, 0.90f), vec3(0.95f, 0.96f, 0.98f), n);
        h = n; rough = 0.55f + 0.25f * g; break; }
      case 5: {  // asphalt
        float n = pfbm(u, v, 4, 4, 61), agg = pnoise(u * 380, v * 380, 380, 62), patch = pfbm(u, v, 2, 3, 63);
        float crack = smoothstepf(0.015f, 0.0f, fabsf(pfbm(u, v, 6, 4, 64) - 0.5f)) * smoothstepf(0.55f, 0.7f, patch);
        float base = 0.11f + 0.06f * n + 0.05f * agg;
        if (patch > 0.62f) base *= 0.8f;
        c = vec3(base, base, base * 1.04f) * (1.f - crack * 0.6f);
        h = agg * 0.6f + n * 0.4f - crack; rough = 0.82f - 0.1f * agg; break; }
      case 6: {  // gravel
        float id; float w = pworley(u, v, 48, 71, &id);
        float peb = 1.f - smoothstepf(0.2f, 0.6f, w);
        c = lerp(vec3(0.33f, 0.30f, 0.27f), vec3(0.58f, 0.54f, 0.48f), id) * (0.6f + 0.4f * peb);
        h = peb; rough = 0.9f; break; }
      case 7: {  // dirt
        float n = pfbm(u, v, 4, 5, 81), g = pnoise(u * 200, v * 200, 200, 82);
        c = lerp(vec3(0.25f, 0.18f, 0.11f), vec3(0.42f, 0.32f, 0.20f), n) * (0.9f + 0.2f * g);
        h = n * 0.7f + g * 0.3f; rough = 0.95f; break; }
      case 8: {  // concrete
        float n = pfbm(u, v, 4, 5, 91), agg = pnoise(u * 300, v * 300, 300, 92), st = pfbm(u, v, 2, 3, 93);
        float b = 0.52f + 0.1f * n + 0.05f * agg - 0.08f * smoothstepf(0.55f, 0.75f, st);
        c = vec3(b, b * 0.99f, b * 0.96f); h = agg * 0.5f + n * 0.5f; rough = 0.78f + 0.1f * agg; break; }
      case 9: {  // clay roof tiles (rows of curved tiles, staggered)
        float rv = v * 14.f, row = floorf(rv), fv = rv - row;
        float cu = u * 20.f + (fmodf(row, 2.f) ? 0.5f : 0.f), fu = cu - floorf(cu);
        float prof = sinf(fu * PI);
        float id = hash2i((int)floorf(cu), (int)row);
        c = lerp(vec3(0.55f, 0.22f, 0.12f), vec3(0.72f, 0.36f, 0.2f), id) * (0.65f + 0.35f * prof) * (0.75f + 0.25f * smoothstepf(0.f, 0.25f, fv));
        h = prof * 0.6f + fv * 0.4f; rough = 0.7f; break; }
      case 10: {  // slate roof
        float rv = v * 18.f, row = floorf(rv), fv = rv - row;
        float cu = u * 12.f + (fmodf(row, 2.f) ? 0.5f : 0.f), fu = cu - floorf(cu);
        float id = hash2i((int)floorf(cu) + 7, (int)row);
        float gap = smoothstepf(0.f, 0.04f, fu) * smoothstepf(1.f, 0.96f, fu);
        c = lerp(vec3(0.16f, 0.17f, 0.2f), vec3(0.28f, 0.29f, 0.33f), id) * (0.5f + 0.5f * gap) * (0.85f + 0.15f * fv);
        h = fv * 0.6f + gap * 0.4f; rough = 0.55f + 0.2f * id; break; }
      case 11: {  // plaster / render
        float n = pfbm(u, v, 3, 5, 111), f = pnoise(u * 250, v * 250, 250, 112), st = pfbm(u, v, 2, 4, 113);
        float b = 0.86f + 0.06f * n + 0.03f * f - 0.1f * smoothstepf(0.6f, 0.8f, st) * smoothstepf(0.6f, 0.0f, v);
        c = vec3(b, b * 0.98f, b * 0.95f); h = n * 0.5f + f * 0.5f; rough = 0.9f; break; }
      case 12: {  // brick
        float rv = v * 24.f, row = floorf(rv), fv = rv - row;
        float cu = u * 12.f + (fmodf(row, 2.f) ? 0.5f : 0.f), fu = cu - floorf(cu);
        float mortar = (fv < 0.12f || fu < 0.05f) ? 1.f : 0.f;
        float id = hash2i((int)floorf(cu) + 3, (int)row + 11), n = pnoise(u * 200, v * 200, 200, 121);
        c = mortar > 0.5f ? vec3(0.62f, 0.6f, 0.56f) : lerp(vec3(0.45f, 0.18f, 0.12f), vec3(0.62f, 0.3f, 0.2f), id) * (0.85f + 0.3f * n);
        h = mortar > 0.5f ? 0.f : 0.7f + 0.3f * n; rough = 0.85f; break; }
      case 13: {  // broadleaf foliage clusters
        float id; float w = pworley(u, v, 22, 131, &id); float w2 = pworley(u, v, 60, 132);
        float leaf = 1.f - smoothstepf(0.f, 0.7f, w2);
        c = lerp(vec3(0.07f, 0.16f, 0.04f), vec3(0.22f, 0.4f, 0.1f), leaf * (0.5f + 0.5f * id)) * (0.6f + 0.6f * (1.f - w));
        h = (1.f - w) * 0.6f + leaf * 0.4f; rough = 0.75f; break; }
      case 14: {  // conifer needles
        float s1 = pnoise(u * 160, v * 40, 160, 141), s2 = pnoise(u * 40, v * 160, 40, 142), n = pfbm(u, v, 8, 3, 143);
        c = lerp(vec3(0.04f, 0.1f, 0.06f), vec3(0.12f, 0.24f, 0.13f), s1 * s2 + 0.3f * n);
        h = s1 * s2 * 0.7f + n * 0.3f; rough = 0.85f; break; }
      case 15: {  // aircraft paint: white with fine orange peel
        float op = pnoise(u * 180, v * 180, 180, 151), n = pfbm(u, v, 4, 3, 152);
        c = vec3(0.97f - 0.02f * n); h = op * 0.15f; rough = 0.18f + 0.06f * op; break; }
      case 16: {  // brushed metal
        float br = pnoise(u * 400, v * 6, 400, 161), n = pfbm(u, v, 4, 4, 162);
        c = vec3(0.62f + 0.08f * br + 0.05f * n); h = br * 0.1f; rough = 0.25f + 0.12f * br; break; }
      case 17: {  // tyre rubber with tread grooves
        float g = fabsf(sinf(u * PI * 24.f)) < 0.2f ? 1.f : 0.f, n = pnoise(u * 300, v * 300, 300, 171);
        c = vec3(0.04f + 0.015f * n) * (1.f - 0.4f * g); h = 1.f - g; rough = 0.92f; break; }
      case 18: {  // cockpit plastic with leather-like grain
        float id; float w = pworley(u, v, 80, 181, &id);
        c = vec3(0.1f + 0.02f * id); h = smoothstepf(0.f, 0.5f, w) * 0.5f; rough = 0.55f + 0.1f * w; break; }
      case 19: {  // seat fabric weave
        float wx = sinf(u * PI * 160.f), wy = sinf(v * PI * 160.f);
        float weave = ((int)floorf(u * 80.f) + (int)floorf(v * 80.f)) & 1 ? wx : wy;
        float n = pfbm(u, v, 4, 3, 191);
        c = lerp(vec3(0.12f, 0.14f, 0.2f), vec3(0.2f, 0.22f, 0.3f), 0.5f + 0.4f * weave) * (0.9f + 0.2f * n);
        h = 0.5f + 0.5f * weave; rough = 1.0f; break; }
      case 20: {  // carpet
        float n = pnoise(u * 350, v * 350, 350, 201), m = pfbm(u, v, 4, 3, 202);
        c = vec3(0.09f, 0.09f, 0.1f) * (0.75f + 0.5f * n) * (0.9f + 0.2f * m); h = n; rough = 1.0f; break; }
      case 21: {  // leather
        float id; float w = pworley(u, v, 40, 211, &id); float n = pfbm(u, v, 4, 4, 212);
        c = lerp(vec3(0.25f, 0.14f, 0.07f), vec3(0.4f, 0.24f, 0.13f), n) * (0.85f + 0.15f * smoothstepf(0.f, 0.4f, w));
        h = smoothstepf(0.f, 0.4f, w); rough = 0.5f; break; }
      case 22: {  // corrugated metal sheeting
        float r = 0.5f + 0.5f * sinf(u * PI * 2.f * 40.f), n = pfbm(u, v, 3, 4, 221), st = pfbm(u, v, 2, 3, 222);
        c = vec3(0.6f + 0.1f * n) * (0.85f + 0.15f * r) - vec3(0.12f, 0.1f, 0.05f) * smoothstepf(0.6f, 0.8f, st);
        h = r; rough = 0.4f + 0.2f * n; break; }
      case 23: {  // crop rows (green)
        float rows = 0.5f + 0.5f * sinf(u * PI * 2.f * 24.f), n = pfbm(u, v, 16, 3, 231);
        c = lerp(vec3(0.3f, 0.22f, 0.12f), vec3(0.2f, 0.45f, 0.1f), smoothstepf(0.3f, 0.7f, rows) * (0.7f + 0.3f * n));
        h = rows; rough = 0.9f; break; }
      case 24: {  // wheat
        float s1 = pnoise(u * 300, v * 30, 300, 241), n = pfbm(u, v, 4, 4, 242);
        c = lerp(vec3(0.62f, 0.5f, 0.22f), vec3(0.82f, 0.7f, 0.38f), s1 * 0.6f + n * 0.4f);
        h = s1; rough = 0.85f; break; }
      case 25: {  // bark: deep vertical furrows broken into plates
        float w = pfbm(u, v, 4, 3, 251) * 0.6f;
        float fur = fabsf(sinf((u * 22.f + w * 4.f) * PI));
        float plates = smoothstepf(0.15f, 0.55f, fur) * (0.7f + 0.3f * pnoise(u * 40, v * 10, 40, 252));
        float n = pfbm(u, v, 16, 3, 253);
        c = lerp(vec3(0.09f, 0.07f, 0.05f), vec3(0.36f, 0.3f, 0.24f), plates * (0.7f + 0.3f * n));
        c = lerp(c, vec3(0.3f, 0.36f, 0.22f), smoothstepf(0.62f, 0.8f, pfbm(u, v, 3, 3, 254)) * 0.35f);   // lichen
        h = plates * 0.8f + n * 0.2f; rough = 0.92f; break; }
      case 26: {  // weathered wood planks
        float row = floorf(v * 8.f), fv = v * 8.f - row;
        float id = hash2i((int)row, 261), grain = pnoise(u * 6 + id * 7.f, v * 160, 6, 262) * 0.6f + pnoise(u * 60, v * 400, 60, 263) * 0.4f;
        float gap = smoothstepf(0.f, 0.06f, fv) * smoothstepf(1.f, 0.94f, fv);
        float b = (0.55f + 0.25f * id) * (0.75f + 0.35f * grain) * (0.45f + 0.55f * gap);
        c = vec3(b, b * 0.93f, b * 0.85f);
        h = gap * (0.7f + 0.3f * grain); rough = 0.85f; break; }
      case 27: {  // forest floor: leaf litter, needles and twigs over dark soil
        float id; float w = pworley(u, v, 48, 271, &id);
        float leaf = 1.f - smoothstepf(0.1f, 0.5f, w);
        float n = pfbm(u, v, 4, 4, 272), tw = smoothstepf(0.985f, 1.f, pnoise(u * 300, v * 30, 300, 273)) + smoothstepf(0.985f, 1.f, pnoise(u * 30, v * 300, 30, 274));
        c = lerp(vec3(0.1f, 0.075f, 0.05f), lerp(vec3(0.38f, 0.26f, 0.12f), vec3(0.42f, 0.36f, 0.16f), id), leaf * (0.6f + 0.4f * n));
        c = lerp(c, vec3(0.2f, 0.26f, 0.1f), smoothstepf(0.55f, 0.75f, pfbm(u, v, 3, 3, 275)) * 0.5f);   // moss patches
        c = lerp(c, vec3(0.3f, 0.22f, 0.14f), clampf(tw, 0, 1) * 0.7f);
        h = leaf * 0.6f + n * 0.3f + tw * 0.2f; rough = 0.95f; break; }
      case 28: {  // asphalt shingles
        float rv = v * 16.f, row = floorf(rv), fv = rv - row;
        float cu = u * 8.f + (fmodf(row, 2.f) ? 0.5f : 0.f), fu = cu - floorf(cu);
        float id = hash2i((int)floorf(cu) + 9, (int)row + 3), g = pnoise(u * 300, v * 300, 300, 281);
        float edge = smoothstepf(0.f, 0.06f, fu) * smoothstepf(1.f, 0.94f, fu);
        float b = (0.75f + 0.3f * id) * (0.8f + 0.25f * g) * (0.55f + 0.45f * smoothstepf(0.f, 0.3f, fv)) * (0.7f + 0.3f * edge);
        c = vec3(b * 0.5f, b * 0.5f, b * 0.52f);
        h = fv * 0.5f + edge * 0.3f + g * 0.2f; rough = 0.9f; break; }
      default: {  // lap siding: overlapping horizontal boards
        float rv = v * 20.f, row = floorf(rv), fv = rv - row;
        float n = pfbm(u, v, 4, 3, 291), g = pnoise(u * 200, v * 20, 200, 292);
        float b = 0.86f * (0.55f + 0.45f * powf(fv, 0.6f)) * (0.95f + 0.05f * n) * (0.97f + 0.03f * g);
        c = vec3(b);
        h = fv; rough = 0.7f; (void)row; break; }
    }
    hgt[(size_t)y * TS + x] = h;
    size_t o = ((size_t)y * TS + x) * 4;
    alb[o + 0] = (uint8_t)(sqrtf(clampf(c.x, 0, 1)) * 255); alb[o + 1] = (uint8_t)(sqrtf(clampf(c.y, 0, 1)) * 255);
    alb[o + 2] = (uint8_t)(sqrtf(clampf(c.z, 0, 1)) * 255); alb[o + 3] = (uint8_t)(clampf(rough, 0, 1) * 255);
  }
  float strength = (l == 1 || l == 13 || l == 25 ? 6.f : l == 2 || l == 9 || l == 10 || l == 12 || l == 28 ? 5.f : l == 6 || l == 22 || l == 26 || l == 27 || l == 29 ? 4.f : l == 15 || l == 16 ? 0.8f : 2.5f);
  for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) {
    auto H = [&](int i, int j) { return hgt[(size_t)((j + TS) % TS) * TS + (i + TS) % TS]; };
    float dx = (H(x + 1, y) - H(x - 1, y)) * strength, dy = (H(x, y + 1) - H(x, y - 1)) * strength;
    vec3 n = normalize(vec3(-dx, -dy, 1));
    float ao = clampf(1.f - (((H(x - 2, y) + H(x + 2, y) + H(x, y - 2) + H(x, y + 2)) * 0.25f) - H(x, y)) * 2.f, 0, 1);
    size_t o = ((size_t)y * TS + x) * 4;
    nrm[o + 0] = (uint8_t)((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)((n.y * 0.5f + 0.5f) * 255);
    nrm[o + 2] = (uint8_t)(clampf(H(x, y), 0, 1) * 255); nrm[o + 3] = (uint8_t)(ao * 255);
  }
}
