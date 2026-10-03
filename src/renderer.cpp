// Air Xpress - OpenGL renderer: GPU ray tracer + sprites + post + UI
#include "renderer.h"
#include "shaders.h"
#include "shaders_wraith_cockpit.h"
#include "font_data.h"
#include "scenery.h"
#include <unordered_map>
#include <cstring>

Renderer g_ren;

static std::unordered_map<std::string, GLint> s_uniCache;
static GLint U(GLuint prog, const char* name) {
  std::string k = std::to_string(prog) + ":" + name;
  auto it = s_uniCache.find(k);
  if (it != s_uniCache.end()) return it->second;
  GLint l = glGetUniformLocation(prog, name);
  s_uniCache[k] = l;
  return l;
}

static GLuint compile(GLenum type, const std::string& src, std::string& err) {
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[8192]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); err += log; return 0; }
  return s;
}
static GLuint program(const std::string& vs, const std::string& fs, std::string& err) {
  GLuint v = compile(GL_VERTEX_SHADER, vs, err), f = compile(GL_FRAGMENT_SHADER, fs, err);
  if (!v || !f) return 0;
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); err += log; return 0; }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}

// ------------------------------------------------------------------ procedural PBR materials
static const int TS = 512;
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

void Renderer::genMaterials() {
  const int L = 30;
  std::vector<uint8_t> alb((size_t)TS * TS * 4 * L), nrm((size_t)TS * TS * 4 * L);
  std::vector<float> hgt((size_t)TS * TS);
  for (int l = 0; l < L; l++) {
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
      size_t o = (((size_t)l * TS + y) * TS + x) * 4;
      alb[o + 0] = (uint8_t)(sqrtf(clampf(c.x, 0, 1)) * 255); alb[o + 1] = (uint8_t)(sqrtf(clampf(c.y, 0, 1)) * 255);
      alb[o + 2] = (uint8_t)(sqrtf(clampf(c.z, 0, 1)) * 255); alb[o + 3] = (uint8_t)(clampf(rough, 0, 1) * 255);
    }
    float strength = (l == 1 || l == 13 || l == 25 ? 6.f : l == 2 || l == 9 || l == 10 || l == 12 || l == 28 ? 5.f : l == 6 || l == 22 || l == 26 || l == 27 || l == 29 ? 4.f : l == 15 || l == 16 ? 0.8f : 2.5f);
    for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) {
      auto H = [&](int i, int j) { return hgt[(size_t)((j + TS) % TS) * TS + (i + TS) % TS]; };
      float dx = (H(x + 1, y) - H(x - 1, y)) * strength, dy = (H(x, y + 1) - H(x, y - 1)) * strength;
      vec3 n = normalize(vec3(-dx, -dy, 1));
      float ao = clampf(1.f - (((H(x - 2, y) + H(x + 2, y) + H(x, y - 2) + H(x, y + 2)) * 0.25f) - H(x, y)) * 2.f, 0, 1);
      size_t o = (((size_t)l * TS + y) * TS + x) * 4;
      nrm[o + 0] = (uint8_t)((n.x * 0.5f + 0.5f) * 255); nrm[o + 1] = (uint8_t)((n.y * 0.5f + 0.5f) * 255);
      nrm[o + 2] = (uint8_t)(clampf(H(x, y), 0, 1) * 255); nrm[o + 3] = (uint8_t)(ao * 255);
    }
  }
  auto up = [&](GLuint& tex, std::vector<uint8_t>& d) {
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TS, TS, L, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    glGetError();  // anisotropy may be unsupported
  };
  up(texAlb, alb); up(texNrm, nrm);
}

void Renderer::genMinimap() {
  const int N = 1024;
  std::vector<uint8_t> img((size_t)N * N * 4);
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    float x = -WORLD_HALF + (i + 0.5f) * 2 * WORLD_HALF / N, z = -WORLD_HALF + (j + 0.5f) * 2 * WORLD_HALF / N;
    float h = g_world.groundHeight(x, z, 5);
    float hx = g_world.groundHeight(x + 80, z, 5) - h;
    float b[4]; g_world.sampleBase(x, z, b);
    vec3 c;
    if (h < 0) { float d = clampf(-h / 60.f, 0, 1); c = lerp(vec3(0.20f, 0.55f, 0.62f), vec3(0.05f, 0.16f, 0.30f), d); }
    else {
      c = lerp(vec3(0.38f, 0.58f, 0.30f), vec3(0.30f, 0.50f, 0.28f), b[2]);
      c = lerp(c, vec3(0.62f, 0.56f, 0.42f), smoothstepf(300, 1100, h));
      c = lerp(c, vec3(0.92f, 0.93f, 0.95f), smoothstepf(lerpf(1700, 400, b[3]), lerpf(1900, 600, b[3]), h));
      float shade = clampf(1.f - hx * 0.02f, 0.6f, 1.3f);
      c = c * shade;
      if (h < 4) c = lerp(c, vec3(0.85f, 0.80f, 0.62f), 0.6f);
    }
    float mk[4]; g_world.sampleMask(x, z, mk);
    if (h > 0 && mk[1] > 0.05f) c = lerp(c, vec3(0.78f, 0.72f, 0.66f), smoothstepf(0.05f, 0.4f, mk[1]));
    if (h > 0 && mk[3] > 0.2f) c = lerp(c, vec3(0.62f, 0.62f, 0.32f), mk[3] * 0.35f);
    if (mk[0] * ROAD_RANGE < 14.f) c = vec3(0.35f, 0.33f, 0.3f);
    if (g_world.onRunway(x, z, 40) >= 0) c = vec3(0.12f, 0.12f, 0.14f);
    size_t o = ((size_t)j * N + i) * 4;
    img[o] = (uint8_t)(clampf(c.x, 0, 1) * 255); img[o + 1] = (uint8_t)(clampf(c.y, 0, 1) * 255); img[o + 2] = (uint8_t)(clampf(c.z, 0, 1) * 255); img[o + 3] = 255;
  }
  glGenTextures(1, &minimapTex); glBindTexture(GL_TEXTURE_2D, minimapTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool Renderer::init(int w, int h) {
  std::string vsFS = kFullscreenVS;
  std::string rt = std::string("#version 330 core\n") + (getenv("CLIPDBG") ? "#define WR_CLIPDEBUG\n" : "") + (getenv("CLIPATLAS") ? "#define WR_CLIPATLAS\n" : "") + kCommonGLSL + kRaytraceFS + kRaytraceFS2 + kRaytraceUfo + kRaytraceFS3 + kRaytraceWraith + kRaytraceWraithCockpit;
  progRT = program(vsFS, rt, error);
  if (!progRT) { error = "Ray tracer shader: " + error; return false; }
  progSprite = program(kSpriteVS, kSpriteFS, error);
  progBright = program(vsFS, kBrightFS, error);
  progBlur = program(vsFS, kBlurFS, error);
  progPost = program(vsFS, kPostFS, error);
  progTAA = program(vsFS, kTaaFS, error);
  progUI = program(kUIVS, kUIFS, error);
  if (!progSprite || !progBright || !progBlur || !progPost || !progUI || !progTAA) { error = "Shader: " + error; return false; }

  glGenVertexArrays(1, &vaoEmpty);
  glGenVertexArrays(1, &vaoSprite); glGenBuffers(1, &vboSprite);
  glBindVertexArray(vaoSprite); glBindBuffer(GL_ARRAY_BUFFER, vboSprite);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)20);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)36);
  glGenVertexArrays(1, &vaoUI); glGenBuffers(1, &vboUI);
  glBindVertexArray(vaoUI); glBindBuffer(GL_ARRAY_BUFFER, vboUI);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)8);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)16);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)32);
  glBindVertexArray(0);

  // heightmap
  glGenTextures(1, &texHM); glBindTexture(GL_TEXTURE_2D, texHM);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, HM_N, HM_N, 0, GL_RGBA, GL_FLOAT, g_world.hm.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glGenTextures(1, &texMask); glBindTexture(GL_TEXTURE_2D, texMask);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MASK_N, MASK_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_world.mask.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &texRoadId); glBindTexture(GL_TEXTURE_2D, texRoadId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, MASK_N, MASK_N, 0, GL_RG, GL_UNSIGNED_BYTE, g_world.roadId.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // max-height mip chain: lets the terrain ray march skip cells it flies over
  glGenTextures(1, &texHMax); glBindTexture(GL_TEXTURE_2D, texHMax);
  for (int L = 0; L < HMAX_LEVELS; L++) glTexImage2D(GL_TEXTURE_2D, L, GL_R32F, HMAX_N >> L, HMAX_N >> L, 0, GL_RED, GL_FLOAT, g_world.hmax[L].data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, HMAX_LEVELS - 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, texHM);
  {
    std::vector<V4> d(384, V4{0, 0, 0, 0});
    for (int i = 0; i < (int)g_roads.size() && i < 64; i++) d[i] = {g_roads[i].ax, g_roads[i].az, g_roads[i].bx, g_roads[i].bz};
    for (int i = 0; i < (int)g_world.boxes.size() && i < 128; i++) {
      const Box& b = g_world.boxes[i];
      d[64 + i] = {b.c.x, b.c.y, b.c.z, (float)b.airport};
      d[192 + i] = {b.h.x, b.h.y, b.h.z, (float)b.kind};
    }
    // [384,400) / [400,416): world bounds of each airport's buildings + their box range (lets the shader skip airports)
    d.resize(416, V4{0, 0, 0, 0});
    int nb = std::min(128, (int)g_world.boxes.size());
    for (int ap = 0; ap < 16 && ap < (int)g_world.airports.size(); ap++) {
      const Airport& A = g_world.airports[ap];
      float s = sinf(A.heading * DEG), c = cosf(A.heading * DEG);
      vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f); int first = -1, count = 0;
      for (int i = 0; i < nb; i++) {
        const Box& b = g_world.boxes[i];
        if (b.airport != ap) continue;
        if (first < 0) first = i;
        count = i - first + 1;
        float r = length(vec3(b.h.x * 2.f, 0, b.h.z * 2.f)) + 2.f;   // generous: towers/radomes, rotation
        for (int k = 0; k < 4; k++) {
          float lx = b.c.x + ((k & 1) ? r : -r), lz = b.c.z + ((k & 2) ? r : -r);
          // inverse of the shader's airport frame: local (x, z) -> world
          float wx = A.x + lx * c + lz * s, wz = A.z + lx * s - lz * c;
          lo.x = std::min(lo.x, wx); hi.x = std::max(hi.x, wx); lo.z = std::min(lo.z, wz); hi.z = std::max(hi.z, wz);
        }
        lo.y = std::min(lo.y, A.elev + b.c.y - b.h.y * 1.3f - 2.f); hi.y = std::max(hi.y, A.elev + b.c.y + b.h.y * 1.3f + 2.f);
      }
      if (first >= 0) { d[384 + ap] = {lo.x, lo.y, lo.z, (float)first}; d[400 + ap] = {hi.x, hi.y, hi.z, (float)count}; }
    }
    glGenTextures(1, &texData); glBindTexture(GL_TEXTURE_2D, texData);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)d.size(), 1, 0, GL_RGBA, GL_FLOAT, d.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  maxH = 0;
  for (size_t i = 0; i < g_world.hm.size(); i += 4) maxH = std::max(maxH, g_world.hm[i] + g_world.hm[i + 1] * 1.5f);
  maxH += 20;
  // font
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glGenTextures(1, &texFont); glBindTexture(GL_TEXTURE_2D, texFont);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_W, FONT_H, 0, GL_RED, GL_UNSIGNED_BYTE, FONT_PIX);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  genMaterials();
  genMinimap();
  if (!initEntities()) return false;
  W = w; H = h;
  createTargets();
  ok = true;
  return true;
}

static void makeTex(GLuint& t, int w, int h, GLenum ifmt, GLenum fmt, GLenum type, GLenum filter) {
  if (t) glDeleteTextures(1, &t);
  glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Render-resolution targets: the ray tracer's colour (+ TAA class in alpha) and depth
void Renderer::createRenderTargets() {
  rw = std::max(64, (int)(W * renderScale)); rh = std::max(64, (int)(H * renderScale));
  makeTex(texRaw, rw, rh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  makeTex(texDepth, rw, rh, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST);
  if (!fboScene) glGenFramebuffers(1, &fboScene);
  glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texRaw, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texDepth, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  createGBuffer();
}

// Display-resolution targets: TAA history + the upscaled scene that sprites, bloom and the composite work on
void Renderer::createTargets() {
  createRenderTargets();
  makeTex(texColor, W, H, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  for (int i = 0; i < 2; i++) {
    makeTex(texHist[i], W, H, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    if (!fboTAA[i]) glGenFramebuffers(1, &fboTAA[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texHist[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texColor, 0);
  }
  histValid = false;
  if (!fboSprite) glGenFramebuffers(1, &fboSprite);
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
  bw = std::max(16, W / 4); bh = std::max(16, H / 4);
  for (int i = 0; i < 2; i++) {
    makeTex(texBloom[i], bw, bh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    if (!fboBloom[i]) glGenFramebuffers(1, &fboBloom[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboBloom[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texBloom[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setRenderScale(float s) {
  if (fabsf(s - renderScale) < 1e-4f) return;
  renderScale = s;
  if (ok) createRenderTargets();
}

void Renderer::resize(int w, int h) {
  if (w < 16 || h < 16) return;
  W = w; H = h;
  if (ok) createTargets();
}

mat4 Renderer::viewProj(const FrameParams& fp) const {
  mat4 view;
  view(0, 0) = fp.camRight.x; view(0, 1) = fp.camRight.y; view(0, 2) = fp.camRight.z;
  view(1, 0) = fp.camUp.x; view(1, 1) = fp.camUp.y; view(1, 2) = fp.camUp.z;
  view(2, 0) = fp.camBack.x; view(2, 1) = fp.camBack.y; view(2, 2) = fp.camBack.z;
  view(0, 3) = -dot(fp.camRight, fp.camPos); view(1, 3) = -dot(fp.camUp, fp.camPos); view(2, 3) = -dot(fp.camBack, fp.camPos);
  return perspective(fp.fovY, (float)W / H, 0.5f, 90000.f) * view;
}

bool Renderer::project(const FrameParams& fp, vec3 p, float& sx, float& sy) const {
  mat4 vp = viewProj(fp);
  float x = vp(0, 0) * p.x + vp(0, 1) * p.y + vp(0, 2) * p.z + vp(0, 3);
  float y = vp(1, 0) * p.x + vp(1, 1) * p.y + vp(1, 2) * p.z + vp(1, 3);
  float w = vp(3, 0) * p.x + vp(3, 1) * p.y + vp(3, 2) * p.z + vp(3, 3);
  if (w < 0.1f) return false;
  sx = (x / w * 0.5f + 0.5f) * W; sy = (1.f - (y / w * 0.5f + 0.5f)) * H;
  return true;
}

void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {
  if (!gpuQ[0]) glGenQueries(4, gpuQ);
  {
    int rq = (gpuQi + 1) % 4;   // issued three frames ago
    if (gpuQUsed[rq]) {
      GLint avail = 0; glGetQueryObjectiv(gpuQ[rq], GL_QUERY_RESULT_AVAILABLE, &avail);
      if (avail) { GLuint64 ns = 0; glGetQueryObjectui64v(gpuQ[rq], GL_QUERY_RESULT, &ns); gpuMs = (float)(ns * 1e-6); gpuQUsed[rq] = false; }
    }
  }
  glBeginQuery(GL_TIME_ELAPSED, gpuQ[gpuQi]);
  // TAA: Halton(2,3) sub-pixel jitter and a golden-ratio noise seed, both changing every frame
  frameNo++;
  {
    auto halton = [](int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    int hi = (frameNo % 8) + 1;
    jitX = (halton(hi, 2) - 0.5f) / rw; jitY = (halton(hi, 3) - 0.5f) / rh;
  }
  // ------------------------------------------------ environment entities: shadow cascades + G-buffer
  drawEntities(fp);
  // ------------------------------------------------ ray trace
  glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
  GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, bufs);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  GLuint p = progRT;
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHM); glUniform1i(U(p, "uHM"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb); glUniform1i(U(p, "uAlb"), 1);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm); glUniform1i(U(p, "uNrm"), 2);
  glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texMask); glUniform1i(U(p, "uMask"), 3);
  glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, texRoadId); glUniform1i(U(p, "uRoadId"), 4);
  glActiveTexture(GL_TEXTURE0 + 6); glBindTexture(GL_TEXTURE_2D, texHMax); glUniform1i(U(p, "uHMax"), 6);
  {   // AI traffic: one row of 32 texels per aircraft
    if (!texTraffic) {
      glGenTextures(1, &texTraffic); glBindTexture(GL_TEXTURE_2D, texTraffic);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 32, kMaxTrafficDrawn, 0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glActiveTexture(GL_TEXTURE0 + 7); glBindTexture(GL_TEXTURE_2D, texTraffic);
    int n = std::min(fp.trafficN, kMaxTrafficDrawn);
    if (n > 0) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 32, n, GL_RGBA, GL_FLOAT, fp.traffic[0].t);
    glUniform1i(U(p, "uTraffic"), 7); glUniform1i(U(p, "uTrafficN"), n);
  }
  glUniform1i(U(p, "uUfoOn"), fp.ufoOn ? 1 : 0);
  if (fp.ufoOn) {
    glUniform3f(U(p, "uUfoPos"), fp.ufoPos.x, fp.ufoPos.y, fp.ufoPos.z);
    glUniformMatrix3fv(U(p, "uUfoRot"), 1, GL_FALSE, fp.ufoRot);
    glUniform4fv(U(p, "uUfoAnim"), 1, fp.ufoAnim);
  }
  glUniform2f(U(p, "uRes"), (float)rw, (float)rh);
  glUniform2f(U(p, "uJit"), jitX, jitY);
  glUniform1f(U(p, "uSeed"), fmodf(frameNo * 0.618034f, 1.f));
  glUniform3f(U(p, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  float cr[9] = {fp.camRight.x, fp.camRight.y, fp.camRight.z, fp.camUp.x, fp.camUp.y, fp.camUp.z, fp.camBack.x, fp.camBack.y, fp.camBack.z};
  glUniformMatrix3fv(U(p, "uCamRot"), 1, GL_FALSE, cr);
  glUniform1f(U(p, "uTanHalf"), tanf(fp.fovY * 0.5f));
  glUniform1f(U(p, "uAspect"), (float)W / H);
  glUniform1f(U(p, "uMaxH"), maxH);
  glUniform1i(U(p, "uQuality"), quality);
  glUniform1f(U(p, "uTime"), fp.time);
  glUniform3f(U(p, "uSunDir"), fp.sunDir.x, fp.sunDir.y, fp.sunDir.z);
  glUniform3f(U(p, "uSunCol"), fp.sunCol.x, fp.sunCol.y, fp.sunCol.z);
  glUniform1f(U(p, "uNight"), fp.night);
  glUniform1f(U(p, "uCloudCover"), fp.cloudCover);
  glUniform1f(U(p, "uCloudBase"), fp.cloudBase);
  glUniform1f(U(p, "uFogB"), fp.fogB);
  glUniform1f(U(p, "uWet"), fp.wet);
  glUniform1f(U(p, "uSnow"), fp.snow);
  glUniform1f(U(p, "uLightning"), fp.lightning);
  glUniform1f(U(p, "uStorm"), fp.storm);
  glUniform2f(U(p, "uWindOff"), fp.windOff.x, fp.windOff.y);
  // airports + buildings
  {
    int n = std::min(16, (int)g_world.airports.size());
    float ap[64], dim[64];
    for (int i = 0; i < n; i++) {
      const Airport& a = g_world.airports[i];
      ap[i * 4] = a.x; ap[i * 4 + 1] = a.z; ap[i * 4 + 2] = a.elev; ap[i * 4 + 3] = a.heading * DEG;
      dim[i * 4] = a.length; dim[i * 4 + 1] = a.width; dim[i * 4 + 2] = (float)a.surface; dim[i * 4 + 3] = (float)a.size;
    }
    glUniform1i(U(p, "uApCount"), n);
    glUniform4fv(U(p, "uAp"), n, ap); glUniform4fv(U(p, "uApDim"), n, dim);
    glUniform1i(U(p, "uBoxCount"), std::min(128, (int)g_world.boxes.size()));
    glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, texData); glUniform1i(U(p, "uData"), 5);
  }
  const PlaneVisual& pv = fp.plane;
  glUniform1i(U(p, "uPlaneOn"), pv.on ? 1 : 0);
  if (pv.on) {
    glUniform3f(U(p, "uPlanePos"), pv.pos.x, pv.pos.y, pv.pos.z);
    glUniformMatrix3fv(U(p, "uPlaneRot"), 1, GL_FALSE, pv.rot);
    glUniform4fv(U(p, "uM"), 24, pv.M);
    glUniform4fv(U(p, "uPS"), 1, pv.PS); glUniform4fv(U(p, "uCtl"), 1, pv.Ctl); glUniform4fv(U(p, "uPr"), 1, pv.Pr);
    glUniform4fv(U(p, "uI0"), 1, pv.I0); glUniform4fv(U(p, "uI1"), 1, pv.I1); glUniform4fv(U(p, "uI2"), 1, pv.I2);
    glUniform3f(U(p, "uColBase"), pv.colBase.x, pv.colBase.y, pv.colBase.z);
    glUniform3f(U(p, "uColStripe"), pv.colStripe.x, pv.colStripe.y, pv.colStripe.z);
    glUniform1i(U(p, "uPropCount"), pv.propCount);
    glUniform4fv(U(p, "uHud"), 1, pv.hud); glUniform4fv(U(p, "uHud2"), 1, pv.hud2); glUniform3f(U(p, "uHudV"), pv.hudV[0], pv.hudV[1], pv.hudV[2]); glUniform4fv(U(p, "uHud3"), 1, pv.hud3);
    if (pv.propCount) glUniform4fv(U(p, "uProp"), pv.propCount, &pv.prop[0][0]);
  }
  {
    const WreckVisual& wv = fp.wreck;
    glUniform1i(U(p, "uWreck"), pv.on ? wv.pieces : 0);
    if (pv.on && wv.pieces > 0) {
      float P[15], C[15], Hh[15];
      for (int i = 0; i < wv.pieces; i++) { P[i*3] = wv.pos[i].x; P[i*3+1] = wv.pos[i].y; P[i*3+2] = wv.pos[i].z; C[i*3] = wv.C[i].x; C[i*3+1] = wv.C[i].y; C[i*3+2] = wv.C[i].z; Hh[i*3] = wv.H[i].x; Hh[i*3+1] = wv.H[i].y; Hh[i*3+2] = wv.H[i].z; }
      glUniform3fv(U(p, "uPcPos"), wv.pieces, P);
      glUniform3fv(U(p, "uPcC"), wv.pieces, C);
      glUniform3fv(U(p, "uPcH"), wv.pieces, Hh);
      glUniformMatrix3fv(U(p, "uPcRot"), wv.pieces, GL_FALSE, &wv.rot[0][0]);
    }
    glUniform1i(U(p, "uDebN"), wv.debris);
    if (wv.debris > 0) { glUniform4fv(U(p, "uDeb"), wv.debris, &wv.deb[0][0]); glUniform4fv(U(p, "uDebQ"), wv.debris, &wv.debQ[0][0]); }
    glUniform1i(U(p, "uCraterN"), wv.craterN);
    if (wv.craterN > 0) {
      glUniform4fv(U(p, "uCrater"), wv.craterN, &wv.crater[0][0]);
      // a circle around them all lets the terrain skip the crater loop everywhere else
      float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
      for (int i = 0; i < wv.craterN; i++) { const float* c = wv.crater[i]; float r = c[2] * 2.7f; x0 = std::min(x0, c[0] - r); x1 = std::max(x1, c[0] + r); z0 = std::min(z0, c[1] - r); z1 = std::max(z1, c[1] + r); }
      glUniform3f(U(p, "uCraterB"), (x0 + x1) * 0.5f, (z0 + z1) * 0.5f, 0.5f * sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0)));
    }
  }
  glUniform3f(U(p, "uLandLightPos"), fp.landLightPos.x, fp.landLightPos.y, fp.landLightPos.z);
  glUniform3f(U(p, "uLandLightDir"), fp.landLightDir.x, fp.landLightDir.y, fp.landLightDir.z);
  glUniform1f(U(p, "uLandLight"), fp.landLight);
  glUniform4fv(U(p, "uFlame"), 1, pv.flame);
  glUniform4fv(U(p, "uWr"), 7, &pv.wr[0][0]);
  {
    const FxVisual& fx = fp.fx;
    glUniform1i(U(p, "uFxBeams"), fx.beams); glUniform1i(U(p, "uFxBombs"), fx.bombs); glUniform1i(U(p, "uFxBlasts"), fx.blasts);
    if (fx.beams) { glUniform4fv(U(p, "uBeamA"), fx.beams, &fx.beamA[0][0]); glUniform4fv(U(p, "uBeamB"), fx.beams, &fx.beamB[0][0]); }
    if (fx.bombs) glUniform4fv(U(p, "uBombs"), fx.bombs, &fx.bomb[0][0]);
    if (fx.blasts) { glUniform4fv(U(p, "uBlast"), fx.blasts, &fx.blast[0][0]); glUniform4fv(U(p, "uBlastI"), fx.blasts, &fx.blastI[0][0]); }
    glUniform4fv(U(p, "uPip"), 1, fx.pip);
    glUniform4fv(U(p, "uFeed"), 1, fx.feed);
  }
  {   // entity G-buffer and the sun shadow cascades
    for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, texGB[i]); }
    glUniform1i(U(p, "uGB0"), 8); glUniform1i(U(p, "uGB1"), 9); glUniform1i(U(p, "uGB2"), 10);
    bool sh = fp.sunDir.y > 0.03f && shValid[0];
    glUniform1i(U(p, "uShOn"), sh ? (shValid[1] ? 2 : 1) : 0);
    for (int c = 0; c < 2; c++) { glActiveTexture(GL_TEXTURE0 + 11 + c); glBindTexture(GL_TEXTURE_2D, texSh[c]); }
    glUniform1i(U(p, "uShMap0"), 11); glUniform1i(U(p, "uShMap1"), 12);
    glUniformMatrix4fv(U(p, "uShM0"), 1, GL_FALSE, shVP[0].m); glUniformMatrix4fv(U(p, "uShM1"), 1, GL_FALSE, shVP[1].m);
    glUniform2f(U(p, "uShTexel"), shR[0] * 2.f / std::max(shRes, 1), shR[1] * 2.f / std::max(shRes, 1));
    glUniform4f(U(p, "uShFade"), shIdeal[0].x, shIdeal[0].z, shIdeal[1].x, shIdeal[1].z);
    glUniform4f(U(p, "uShFadeR"), shR[0] * kShFade0, shR[0] * kShFade1, shR[1] * kShFade0, shR[1] * kShFade1);
    glUniform1f(U(p, "uTreeFar"), entTreeFar);
  }
  glUniform3f(U(p, "uFlameLP"), fp.flameLightPos.x, fp.flameLightPos.y, fp.flameLightPos.z);
  glUniform3f(U(p, "uFlameLI"), fp.flameLight.x, fp.flameLight.y, fp.flameLight.z);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ------------------------------------------------ temporal AA resolve (before the sprites: particles never smear)
  {
    int cur = histIdx ^ 1;
    if (length(fp.camPos - prevCamPos) > 400.f) histValid = false;   // camera cut
    glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[cur]);
    glDrawBuffers(2, bufs);
    glViewport(0, 0, W, H);
    glUseProgram(progTAA);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRaw); glUniform1i(U(progTAA, "uRaw"), 0);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progTAA, "uDepth"), 1);
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texHist[histIdx]); glUniform1i(U(progTAA, "uHist"), 2);
    glUniform2f(U(progTAA, "uRes"), (float)W, (float)H);
    glUniform2f(U(progTAA, "uRawRes"), (float)rw, (float)rh);
    glUniform2f(U(progTAA, "uJit"), jitX, jitY);
    glUniform1f(U(progTAA, "uHistValid"), histValid ? 1.f : 0.f);
    glUniform3f(U(progTAA, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
    glUniformMatrix3fv(U(progTAA, "uCamRot"), 1, GL_FALSE, cr);
    glUniform3f(U(progTAA, "uPrevCamPos"), prevCamPos.x, prevCamPos.y, prevCamPos.z);
    glUniformMatrix3fv(U(progTAA, "uPrevCamRot"), 1, GL_FALSE, prevCamRot);
    glUniform1f(U(progTAA, "uTanHalf"), tanf(fp.fovY * 0.5f));
    glUniform1f(U(progTAA, "uAspect"), (float)W / H);
    const PlaneVisual& pv2 = fp.plane;
    vec3 pp = pv2.on ? pv2.pos : prevPlanePos;
    glUniform3f(U(progTAA, "uPlanePos"), pp.x, pp.y, pp.z);
    glUniformMatrix3fv(U(progTAA, "uPlaneRot"), 1, GL_FALSE, pv2.on ? pv2.rot : prevPlaneRot);
    glUniform3f(U(progTAA, "uPrevPlanePos"), prevPlanePos.x, prevPlanePos.y, prevPlanePos.z);
    glUniformMatrix3fv(U(progTAA, "uPrevPlaneRot"), 1, GL_FALSE, prevPlaneRot);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    histIdx = cur; histValid = true;
    prevCamPos = fp.camPos; memcpy(prevCamRot, cr, sizeof cr);
    if (pv2.on) { prevPlanePos = pv2.pos; memcpy(prevPlaneRot, pv2.rot, sizeof prevPlaneRot); }
  }

  // ------------------------------------------------ sprites
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  GLenum one = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &one);
  glViewport(0, 0, W, H);
  glEnable(GL_BLEND);
  glUseProgram(progSprite);
  mat4 vp = viewProj(fp);
  glUniformMatrix4fv(U(progSprite, "uViewProj"), 1, GL_FALSE, vp.m);
  glUniform3f(U(progSprite, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progSprite, "uDepth"), 0);
  glUniform2f(U(progSprite, "uRes"), (float)W, (float)H);
  glUniform3f(U(progSprite, "uSunDir"), fp.sunDir.x, fp.sunDir.y, fp.sunDir.z);
  glUniform3f(U(progSprite, "uSunCol"), fp.sunCol.x, fp.sunCol.y, fp.sunCol.z);
  float amb = 0.08f + 0.35f * clampf(fp.sunDir.y + 0.1f, 0, 1);
  glUniform3f(U(progSprite, "uAmb"), amb * 0.8f, amb * 0.9f, amb * 1.1f);
  glUniform1f(U(progSprite, "uFogB"), fp.fogB);
  glUniform1f(U(progSprite, "uTime"), fp.time);
  glBindVertexArray(vaoSprite);
  glBindBuffer(GL_ARRAY_BUFFER, vboSprite);
  if (!alphaSprites.empty()) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBufferData(GL_ARRAY_BUFFER, alphaSprites.size() * sizeof(SpriteVert), alphaSprites.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)alphaSprites.size());
  }
  if (!addSprites.empty()) {
    glBlendFunc(GL_ONE, GL_ONE);
    glBufferData(GL_ARRAY_BUFFER, addSprites.size() * sizeof(SpriteVert), addSprites.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)addSprites.size());
  }
  glDisable(GL_BLEND);

  // ------------------------------------------------ bloom
  glBindVertexArray(vaoEmpty);
  glViewport(0, 0, bw, bh);
  glBindFramebuffer(GL_FRAMEBUFFER, fboBloom[0]);
  glUseProgram(progBright);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texColor);
  glUniform1i(U(progBright, "uTex"), 0); glUniform2f(U(progBright, "uTexel"), 1.f / W * 1.5f, 1.f / H * 1.5f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glUseProgram(progBlur);
  glUniform1i(U(progBlur, "uTex"), 0);
  for (int it = 0; it < 2; it++) {
    glBindFramebuffer(GL_FRAMEBUFFER, fboBloom[1]); glBindTexture(GL_TEXTURE_2D, texBloom[0]);
    glUniform2f(U(progBlur, "uDir"), 1.f / bw, 0); glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, fboBloom[0]); glBindTexture(GL_TEXTURE_2D, texBloom[1]);
    glUniform2f(U(progBlur, "uDir"), 0, 1.f / bh); glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // ------------------------------------------------ composite to backbuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, W, H);
  glUseProgram(progPost);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texColor); glUniform1i(U(progPost, "uScene"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texBloom[0]); glUniform1i(U(progPost, "uBloom"), 1);
  glUniform1f(U(progPost, "uExposure"), fp.exposure);
  glUniform1f(U(progPost, "uTime"), fp.time);
  glUniform2f(U(progPost, "uRes"), (float)W, (float)H);
  glUniform1f(U(progPost, "uRainLens"), fp.rainLens);
  glUniform1f(U(progPost, "uFade"), fp.fade);
  glUniform1f(U(progPost, "uVignette"), fp.vignette);
  glUniform1f(U(progPost, "uGLoad"), fp.gLoad);
  float sx = 0, sy = 0; vec3 sp = fp.camPos + fp.sunDir * 10000.f;
  bool vis = fp.sunDir.y > -0.02f && project(fp, sp, sx, sy) && sx > -0.2f * W && sx < 1.2f * W && sy > -0.2f * H && sy < 1.2f * H;
  glUniform2f(U(progPost, "uSunScreen"), sx / W, 1.f - sy / H);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progPost, "uDepthTex"), 2);
  glUniform1f(U(progPost, "uSunVisible"), vis && !fp.sealedCockpit ? (1.f - smoothstepf(0.5f, 0.9f, fp.cloudCover)) * smoothstepf(-0.02f, 0.1f, fp.sunDir.y) : 0.f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glEndQuery(GL_TIME_ELAPSED);
  gpuQUsed[gpuQi] = true; gpuQi = (gpuQi + 1) % 4;
}

// ------------------------------------------------------------------ UI
void Renderer::uiBegin() { ui.clear(); curImg = 0; }

static void quad(std::vector<UIVert>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, vec3 c, float a, float mode, float hx = 0, float hy = 0, float p = 0, const vec3* c2 = nullptr) {
  vec3 b = c2 ? *c2 : c;
  UIVert q[4] = {{x0, y0, u0, v0, c.x, c.y, c.z, a, mode, hx, hy, p}, {x1, y0, u1, v0, c.x, c.y, c.z, a, mode, hx, hy, p},
                 {x1, y1, u1, v1, b.x, b.y, b.z, a, mode, hx, hy, p}, {x0, y1, u0, v1, b.x, b.y, b.z, a, mode, hx, hy, p}};
  v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
}

void Renderer::rect(float x, float y, float w, float h, vec3 c, float a, float radius) {
  if (radius <= 0) { quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, c, a, 0); return; }
  quad(ui, x, y, x + w, y + h, -w * 0.5f, -h * 0.5f, w * 0.5f, h * 0.5f, c, a, 4 + std::min(radius, std::min(w, h) * 0.5f) * 0.001f, w * 0.5f, h * 0.5f);
}

void Renderer::rectGrad(float x, float y, float w, float h, vec3 top, vec3 bottom, float a, float radius) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x, y, x + w, y + h, -w * 0.5f, -h * 0.5f, w * 0.5f, h * 0.5f, top, a, 4 + r * 0.001f, w * 0.5f, h * 0.5f, 0, &bottom);
}

void Renderer::rectOutline(float x, float y, float w, float h, vec3 c, float a, float radius, float th) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x - 1, y - 1, x + w + 1, y + h + 1, -w * 0.5f - 1, -h * 0.5f - 1, w * 0.5f + 1, h * 0.5f + 1, c, a, 5 + r * 0.001f, w * 0.5f, h * 0.5f, th);
}

void Renderer::glow(float x, float y, float w, float h, vec3 c, float a, float radius, float soft) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x - soft, y - soft, x + w + soft, y + h + soft, -w * 0.5f - soft, -h * 0.5f - soft, w * 0.5f + soft, h * 0.5f + soft, c, a, 6 + r * 0.001f, w * 0.5f, h * 0.5f, soft);
}

void Renderer::line(float x0, float y0, float x1, float y1, float th, vec3 c, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-3f) return;
  float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
  UIVert q[4] = {{x0 + nx, y0 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}, {x1 + nx, y1 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0},
                 {x1 - nx, y1 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}, {x0 - nx, y0 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}};
  ui.push_back(q[0]); ui.push_back(q[1]); ui.push_back(q[2]); ui.push_back(q[0]); ui.push_back(q[2]); ui.push_back(q[3]);
}

float Renderer::textWidth(const std::string& s, float size) const {
  float sc = size / FONT_EM, w = 0;
  for (unsigned char ch : s) { if (ch < 32 || ch > 126) ch = '?'; w += FONT_ADV[ch - 32] * sc; }
  return w;
}

float Renderer::text(float x, float y, float size, const std::string& s, vec3 c, float a, int align, bool shadow) {
  float sc = size / FONT_EM;
  float w = textWidth(s, size);
  if (align == 1) x -= w * 0.5f; else if (align == 2) x -= w;
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    float cx = x;
    for (unsigned char ch : s) {
      if (ch < 32 || ch > 126) ch = '?';
      int gi = ch - 32;
      if (ch != ' ') {
        int col = gi % 16, row = gi / 16;
        float u0 = (float)(col * FONT_CELLW) / FONT_W, v0 = (float)(row * FONT_CELLH) / FONT_H;
        float u1 = (float)((col + 1) * FONT_CELLW) / FONT_W, v1 = (float)((row + 1) * FONT_CELLH) / FONT_H;
        float x0 = cx - FONT_PAD * sc, y0 = y - FONT_PAD * sc;
        float off = pass == 0 ? size * 0.06f : 0;
        quad(ui, x0 + off, y0 + off, x0 + FONT_CELLW * sc + off, y0 + FONT_CELLH * sc + off, u0, v0, u1, v1, c, a, pass == 0 ? 2.f : 1.f);
      }
      cx += FONT_ADV[gi] * sc;
    }
  }
  return w;
}

void Renderer::image(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float a) {
  if (curImg && curImg != tex) flushUI();
  curImg = tex;
  quad(ui, x, y, x + w, y + h, u0, v0, u1, v1, vec3(1, 1, 1), a, 3);
}

void Renderer::flushUI() {
  if (ui.empty()) return;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, W, H);
  glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(progUI);
  glUniform2f(U(progUI, "uScreen"), (float)W, (float)H);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texFont); glUniform1i(U(progUI, "uFont"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, curImg ? curImg : texFont); glUniform1i(U(progUI, "uImg"), 1);
  glBindVertexArray(vaoUI); glBindBuffer(GL_ARRAY_BUFFER, vboUI);
  glBufferData(GL_ARRAY_BUFFER, ui.size() * sizeof(UIVert), ui.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ui.size());
  glActiveTexture(GL_TEXTURE0);
  ui.clear();
}

void Renderer::uiEnd() { flushUI(); glDisable(GL_BLEND); curImg = 0; }

bool Renderer::screenshot(const char* path) {
  std::vector<uint8_t> px((size_t)W * H * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (int y = H - 1; y >= 0; y--) fwrite(&px[(size_t)y * W * 3], 1, (size_t)W * 3, f);
  fclose(f);
  return true;
}
