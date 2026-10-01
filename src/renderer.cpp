// Air Xpress - OpenGL renderer: GPU ray tracer + sprites + post + UI
#include "renderer.h"
#include "shaders.h"
#include "font_data.h"
#include <unordered_map>

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
  const int L = 8;
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
        default: {  // dirt
          float n = pfbm(u, v, 4, 5, 81), g = pnoise(u * 200, v * 200, 200, 82);
          c = lerp(vec3(0.25f, 0.18f, 0.11f), vec3(0.42f, 0.32f, 0.20f), n) * (0.9f + 0.2f * g);
          h = n * 0.7f + g * 0.3f; rough = 0.95f; break; }
      }
      hgt[(size_t)y * TS + x] = h;
      size_t o = (((size_t)l * TS + y) * TS + x) * 4;
      alb[o + 0] = (uint8_t)(sqrtf(clampf(c.x, 0, 1)) * 255); alb[o + 1] = (uint8_t)(sqrtf(clampf(c.y, 0, 1)) * 255);
      alb[o + 2] = (uint8_t)(sqrtf(clampf(c.z, 0, 1)) * 255); alb[o + 3] = (uint8_t)(clampf(rough, 0, 1) * 255);
    }
    float strength = (l == 1 ? 6.f : l == 2 ? 5.f : l == 6 ? 4.f : 2.5f);
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
  const int N = 512;
  std::vector<uint8_t> img((size_t)N * N * 4);
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    float x = -WORLD_HALF + (i + 0.5f) * 2 * WORLD_HALF / N, z = -WORLD_HALF + (j + 0.5f) * 2 * WORLD_HALF / N;
    float h = g_world.height(x, z, 5);
    float hx = g_world.height(x + 120, z, 5) - h;
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
  std::string rt = std::string("#version 330 core\n") + kCommonGLSL + kRaytraceFS;
  progRT = program(vsFS, rt, error);
  if (!progRT) { error = "Ray tracer shader: " + error; return false; }
  progSprite = program(kSpriteVS, kSpriteFS, error);
  progBright = program(vsFS, kBrightFS, error);
  progBlur = program(vsFS, kBlurFS, error);
  progPost = program(vsFS, kPostFS, error);
  progUI = program(kUIVS, kUIFS, error);
  if (!progSprite || !progBright || !progBlur || !progPost || !progUI) { error = "Shader: " + error; return false; }

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
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)32);
  glBindVertexArray(0);

  // heightmap
  glGenTextures(1, &texHM); glBindTexture(GL_TEXTURE_2D, texHM);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, HM_N, HM_N, 0, GL_RGBA, GL_FLOAT, g_world.hm.data());
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

void Renderer::createTargets() {
  rw = std::max(64, (int)(W * renderScale)); rh = std::max(64, (int)(H * renderScale));
  makeTex(texColor, rw, rh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  makeTex(texDepth, rw, rh, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST);
  if (!fboScene) glGenFramebuffers(1, &fboScene);
  glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texDepth, 0);
  if (!fboSprite) glGenFramebuffers(1, &fboSprite);
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
  bw = std::max(16, rw / 4); bh = std::max(16, rh / 4);
  for (int i = 0; i < 2; i++) {
    makeTex(texBloom[i], bw, bh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    if (!fboBloom[i]) glGenFramebuffers(1, &fboBloom[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboBloom[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texBloom[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
  glUniform2f(U(p, "uRes"), (float)rw, (float)rh);
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
    int nb = std::min(64, (int)g_world.boxes.size());
    std::vector<float> bc(nb * 4), bh2(nb * 4);
    for (int i = 0; i < nb; i++) {
      const Box& b = g_world.boxes[i];
      bc[i * 4] = b.c.x; bc[i * 4 + 1] = b.c.y; bc[i * 4 + 2] = b.c.z; bc[i * 4 + 3] = (float)b.airport;
      bh2[i * 4] = b.h.x; bh2[i * 4 + 1] = b.h.y; bh2[i * 4 + 2] = b.h.z; bh2[i * 4 + 3] = (float)b.kind;
    }
    glUniform1i(U(p, "uBoxCount"), nb);
    if (nb) { glUniform4fv(U(p, "uBoxC"), nb, bc.data()); glUniform4fv(U(p, "uBoxH"), nb, bh2.data()); }
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
    if (pv.propCount) glUniform4fv(U(p, "uProp"), pv.propCount, &pv.prop[0][0]);
  }
  glUniform3f(U(p, "uLandLightPos"), fp.landLightPos.x, fp.landLightPos.y, fp.landLightPos.z);
  glUniform3f(U(p, "uLandLightDir"), fp.landLightDir.x, fp.landLightDir.y, fp.landLightDir.z);
  glUniform1f(U(p, "uLandLight"), fp.landLight);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ------------------------------------------------ sprites
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  GLenum one = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &one);
  glViewport(0, 0, rw, rh);
  glEnable(GL_BLEND);
  glUseProgram(progSprite);
  mat4 vp = viewProj(fp);
  glUniformMatrix4fv(U(progSprite, "uViewProj"), 1, GL_FALSE, vp.m);
  glUniform3f(U(progSprite, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progSprite, "uDepth"), 0);
  glUniform2f(U(progSprite, "uRes"), (float)rw, (float)rh);
  glUniform3f(U(progSprite, "uSunDir"), fp.sunDir.x, fp.sunDir.y, fp.sunDir.z);
  glUniform3f(U(progSprite, "uSunCol"), fp.sunCol.x, fp.sunCol.y, fp.sunCol.z);
  float amb = 0.08f + 0.35f * clampf(fp.sunDir.y + 0.1f, 0, 1);
  glUniform3f(U(progSprite, "uAmb"), amb * 0.8f, amb * 0.9f, amb * 1.1f);
  glUniform1f(U(progSprite, "uFogB"), fp.fogB);
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
  glUniform1i(U(progBright, "uTex"), 0); glUniform2f(U(progBright, "uTexel"), 1.f / rw * 1.5f, 1.f / rh * 1.5f);
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
  float sx = 0, sy = 0; vec3 sp = fp.camPos + fp.sunDir * 10000.f;
  bool vis = fp.sunDir.y > -0.02f && project(fp, sp, sx, sy) && sx > -0.2f * W && sx < 1.2f * W && sy > -0.2f * H && sy < 1.2f * H;
  glUniform2f(U(progPost, "uSunScreen"), sx / W, 1.f - sy / H);
  glUniform1f(U(progPost, "uSunVisible"), vis ? (1.f - smoothstepf(0.5f, 0.9f, fp.cloudCover)) * smoothstepf(-0.02f, 0.1f, fp.sunDir.y) : 0.f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
}

// ------------------------------------------------------------------ UI
void Renderer::uiBegin() { ui.clear(); curImg = 0; }

static void quad(std::vector<UIVert>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, vec3 c, float a, float mode, float hx = 0, float hy = 0) {
  UIVert q[4] = {{x0, y0, u0, v0, c.x, c.y, c.z, a, mode, hx, hy}, {x1, y0, u1, v0, c.x, c.y, c.z, a, mode, hx, hy},
                 {x1, y1, u1, v1, c.x, c.y, c.z, a, mode, hx, hy}, {x0, y1, u0, v1, c.x, c.y, c.z, a, mode, hx, hy}};
  v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
}

void Renderer::rect(float x, float y, float w, float h, vec3 c, float a, float radius) {
  if (radius <= 0) { quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, c, a, 0); return; }
  quad(ui, x, y, x + w, y + h, -w * 0.5f, -h * 0.5f, w * 0.5f, h * 0.5f, c, a, 4 + std::min(radius, std::min(w, h) * 0.5f) * 0.001f, w * 0.5f, h * 0.5f);
}

void Renderer::line(float x0, float y0, float x1, float y1, float th, vec3 c, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-3f) return;
  float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
  UIVert q[4] = {{x0 + nx, y0 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0}, {x1 + nx, y1 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0},
                 {x1 - nx, y1 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0}, {x0 - nx, y0 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0}};
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
