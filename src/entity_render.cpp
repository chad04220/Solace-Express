// Air Xpress - environment entity rendering: chunk streaming, culling, LOD selection, instanced G-buffer and
// sun shadow-cascade passes. The ray tracer composites the G-buffer with the traced scene and lights it.
#include "renderer.h"
#include "entity_shaders.h"
#include <chrono>

static GLuint compileEnt(GLenum type, const std::string& src, std::string& err) {
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[8192]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); err += log; return 0; }
  return s;
}
static GLuint linkEnt(const std::string& vs, const std::string& fs, std::string& err) {
  GLuint v = compileEnt(GL_VERTEX_SHADER, vs, err), f = compileEnt(GL_FRAGMENT_SHADER, fs, err);
  if (!v || !f) return 0;
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); err += log; return 0; }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}

bool Renderer::initEntities() {
  std::string hdr = "#version 330 core\n";
  progEnt = linkEnt(hdr + kEntVS, hdr + kEntFS1 + kEntFS2, error);
  progEntSh = linkEnt(hdr + kEntVS, hdr + kEntFS1 + kEntShadowFS, error);
  if (!progEnt || !progEntSh) { error = "Entity shader: " + error; return false; }
  std::vector<EVert> verts;
  buildEntityMeshes(verts, entRange);
  glGenVertexArrays(1, &vaoEnt); glBindVertexArray(vaoEnt);
  glGenBuffers(1, &vboEntMesh); glBindBuffer(GL_ARRAY_BUFFER, vboEntMesh);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(EVert), verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)24);
  glGenBuffers(1, &vboEntInst); glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);
  glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(Ent), nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)0); glVertexAttribDivisor(3, 1);
  glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)16); glVertexAttribDivisor(4, 1);
  glBindVertexArray(0);
  return true;
}

// G-buffer at the ray-trace resolution: (distance, octahedral normal, class) | albedo, roughness | emission, metal
void Renderer::createGBuffer() {
  auto mk = [&](GLuint& t, GLenum ifmt, GLenum fmt, GLenum type) {
    if (t) glDeleteTextures(1, &t);
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, rw, rh, 0, fmt, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mk(texGB[0], GL_RGBA32F, GL_RGBA, GL_FLOAT);
  mk(texGB[1], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  mk(texGB[2], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  mk(texGBDepth, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
  if (!fboGB) glGenFramebuffers(1, &fboGB);
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  for (int i = 0; i < 3; i++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, texGB[i], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texGBDepth, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace {
struct EntRanges { float tree, bush, rock, big, build, t0, t1, r0, r1, b0, b1, sh0, sh1; int shRes; };
EntRanges rangesFor(int q) {
  if (q <= 0) return {2200, 700, 1500, 8000, 8000, 170, 800, 250, 1000, 500, 2500, 170, 1100, 1024};
  if (q == 1) return {3600, 1100, 2400, 12000, 12000, 240, 1100, 350, 1400, 800, 3500, 230, 1800, 2048};
  return {5500, 1600, 3400, 16000, 16000, 340, 1500, 450, 1800, 1200, 5000, 300, 2600, 2048};
}
float rangeOf(const EntRanges& R, int k) {
  if (k == EK_BUSH) return R.bush;
  if (entClass(k) == EC_TREE) return R.tree;
  if (k <= EK_SLAB) return R.rock;
  if (entClass(k) == EC_ROCK) return R.big;
  return R.build;
}
void lodLimits(const EntRanges& R, int k, float& l0, float& l1) {
  int c = entClass(k);
  if (c == EC_TREE) { l0 = R.t0; l1 = R.t1; if (k == EK_BUSH) { l0 *= 0.6f; l1 *= 0.5f; } }
  else if (c == EC_ROCK) { l0 = R.r0 * (k >= EK_OUTCROP ? 2.5f : 1.f); l1 = R.r1 * (k >= EK_OUTCROP ? 3.f : 1.f); }
  else { l0 = R.b0; l1 = R.b1; }
}
struct Draw { int kind, lod; size_t first; int count; };
}  // namespace

void Renderer::drawEntities(const FrameParams& fp) {
  if (!progEnt) return;
  auto tStart = std::chrono::steady_clock::now();
  entFrame++;
  const EntRanges R = rangesFor(quality);
  const float farAll = std::max(R.big, R.build) + 300.f, farDetail = std::max(std::max(R.tree, R.rock), R.bush) + 300.f;
  vec3 cam = fp.camPos;
  const int ccx = Scenery::chunkOf(cam.x), ccz = Scenery::chunkOf(cam.z);
  const int rad = (int)ceilf(farAll / Scenery::CH) + 1;
  // ------------------------------------------------ streaming: queue missing chunks by distance, generate within a time budget
  struct Need { float d; int cx, cz, level; };
  std::vector<Need> need;
  for (int dz = -rad; dz <= rad; dz++)
    for (int dx = -rad; dx <= rad; dx++) {
      int cx = ccx + dx, cz = ccz + dz;
      if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
      float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
      float ex = std::max(std::max(x0 - cam.x, cam.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - cam.z, cam.z - z0 - Scenery::CH), 0.f);
      float d = sqrtf(ex * ex + ez * ez);
      int want = d < farDetail ? 2 : d < farAll ? 1 : 0;
      if (!want) continue;
      Scenery::Chunk* c = g_scenery.get(cx, cz);
      if (c) c->lastUse = entFrame;
      if (!c || c->level < want) need.push_back({d, cx, cz, want});
    }
  if (!need.empty()) {
    std::sort(need.begin(), need.end(), [](const Need& a, const Need& b) { return a.d < b.d; });
    auto t0 = std::chrono::steady_clock::now();
    for (const Need& n : need) {
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (!entSync && n.d > 700.f && ms > 2.5) break;   // close chunks are never left missing
      g_scenery.ensure(n.cx, n.cz, n.level)->lastUse = entFrame;
      entGenCount++;
      // a new chunk inside a shadow cascade's area makes that cascade re-render
      for (int c = 0; c < 2; c++) {
        float r = (c == 0 ? R.sh0 : R.sh1) * 1.6f + 300.f;
        float x0 = Scenery::chunkX0(n.cx), z0 = Scenery::chunkX0(n.cz);
        float ex = std::max(std::max(x0 - shCenter[c].x, shCenter[c].x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - shCenter[c].z, shCenter[c].z - z0 - Scenery::CH), 0.f);
        if (ex < r && ez < r) shGen[c] = -1;
      }
    }
  }
  if (entFrame % 240 == 0) g_scenery.trim(cam, farDetail + 1200.f, farAll + 2500.f, entFrame);
  entChunks = (int)g_scenery.generated();

  double tStream = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
  // ------------------------------------------------ frustum
  mat4 vp = viewProj(fp);
  float pl[6][4];
  for (int i = 0; i < 3; i++)
    for (int s = 0; s < 2; s++) {
      float* P = pl[i * 2 + s]; float sg = s ? -1.f : 1.f;
      for (int c = 0; c < 4; c++) P[c] = vp(3, c) + sg * vp(i, c);
    }
  auto boxVisible = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
    for (auto& P : pl) {
      float x = P[0] >= 0 ? x1 : x0, y = P[1] >= 0 ? y1 : y0, z = P[2] >= 0 ? z1 : z0;
      if (P[0] * x + P[1] * y + P[2] * z + P[3] < 0) return false;
    }
    return true;
  };

  // ------------------------------------------------ shadow cascades: which need re-rendering
  bool sunUp = fp.sunDir.y > 0.03f;
  vec3 L = normalize(fp.sunDir);
  float cR[2] = {R.sh0, R.sh1};
  bool shDirty[2] = {false, false};
  vec3 fwdH = normalize(vec3(-fp.camBack.x, 0, -fp.camBack.z) + vec3(1e-4f, 0, 0));
  if (shRes != R.shRes) {   // (re)create the cascade maps
    shRes = R.shRes;
    for (int c = 0; c < 2; c++) {
      if (texSh[c]) glDeleteTextures(1, &texSh[c]);
      glGenTextures(1, &texSh[c]); glBindTexture(GL_TEXTURE_2D, texSh[c]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, shRes, shRes, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (!fboSh[c]) glGenFramebuffers(1, &fboSh[c]);
      glBindFramebuffer(GL_FRAMEBUFFER, fboSh[c]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texSh[c], 0);
      GLenum none = GL_NONE; glDrawBuffers(1, &none);
      shValid[c] = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  {   // craters added or cleared: the scenery they flattened leaves the shadow maps too
    static size_t lastCraters = 0; static float lastSum = 0;
    float sum = (float)g_scenery.wreckRev * 1013.f; for (const vec3& c : g_scenery.craters) sum += c.x + c.y * 3.f + c.z * 7.f;
    if (g_scenery.craters.size() != lastCraters || sum != lastSum) { shGen[0] = shGen[1] = -1; lastCraters = g_scenery.craters.size(); lastSum = sum; }
  }
  vec3 newCenter[2];
  for (int c = 0; c < 2 && sunUp; c++) {
    newCenter[c] = cam + fwdH * (cR[c] * 0.45f);
    float moved = length(vec3(newCenter[c].x - shCenter[c].x, 0, newCenter[c].z - shCenter[c].z));
    shAge[c]++;
    shDirty[c] = !shValid[c] || moved > cR[c] * 0.12f || fabsf(newCenter[c].y - shCenter[c].y) > cR[c] * 0.25f || dot(L, shSun[c]) < 0.99998f ||
                 shGen[c] < 0 || shR[c] != cR[c] || shAge[c] > 600;
  }

  // ------------------------------------------------ gather instances into (pass, kind, lod) buckets
  static std::vector<Ent> bucket[3][EK_COUNT][ENT_LODS];   // pass 0 view, 1/2 shadow cascades
  for (auto& a : bucket) for (auto& b : a) for (auto& v : b) v.clear();
  bool anyCrater = g_scenery.anyGone();
  for (int dz = -rad; dz <= rad; dz++)
    for (int dx = -rad; dx <= rad; dx++) {
      Scenery::Chunk* ch = g_scenery.get(ccx + dx, ccz + dz);
      if (!ch || ch->ents.empty()) continue;
      float x0 = Scenery::chunkX0(ccx + dx), z0 = Scenery::chunkX0(ccz + dz), x1 = x0 + Scenery::CH, z1 = z0 + Scenery::CH;
      float ex = std::max(std::max(x0 - cam.x, cam.x - x1), 0.f), ez = std::max(std::max(z0 - cam.z, cam.z - z1), 0.f);
      float ey = std::max(std::max(ch->ymin - cam.y, cam.y - ch->ymax), 0.f);
      float dmin = sqrtf(ex * ex + ez * ez + ey * ey);
      bool inView = dmin < farAll && boxVisible(x0 - 12, ch->ymin, z0 - 12, x1 + 12, ch->ymax, z1 + 12);
      bool inSh[2] = {false, false};
      for (int c = 0; c < 2; c++)
        if (shDirty[c]) {
          // the cascade box, swept along the sun direction (tall things outside it still cast into it)
          float r = cR[c] + 60.f;
          float hx = std::max(std::max(x0 - newCenter[c].x, newCenter[c].x - x1), 0.f), hz = std::max(std::max(z0 - newCenter[c].z, newCenter[c].z - z1), 0.f);
          inSh[c] = hx < r + 150.f && hz < r + 150.f;
        }
      if (!inView && !inSh[0] && !inSh[1]) continue;
      for (int k = 0; k < EK_COUNT; k++) {
        uint32_t b = ch->off[k], e = ch->off[k + 1];
        if (b == e) continue;
        float far = rangeOf(R, k), l0, l1;
        lodLimits(R, k, l0, l1);
        bool thin = entClass(k) == EC_TREE || k <= EK_SLAB;
        bool viewK = inView && dmin < far;
        if (!viewK && !inSh[0] && !inSh[1]) continue;
        for (uint32_t i = b; i < e; i++) {
          const Ent& en = ch->ents[i];
          if (anyCrater && g_scenery.destroyed(en)) continue;
          float ddx = en.x - cam.x, ddy = en.y - cam.y, ddz = en.z - cam.z;
          float d = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
          int lod = d < l0 ? 0 : d < l1 ? 1 : 2;
          if (viewK && d < far) {
            // thin out towards the far limit (the ground texture takes over the look of distant forest)
            bool keep = !thin || fmodf(en.seed * 7.13f, 1.f) >= smoothstepf(far * 0.45f, far, d) * 0.92f;
            if (keep) bucket[0][k][lod].push_back(en);
          }
          for (int c = 0; c < 2; c++)
            if (inSh[c]) {
              float sx = en.x - newCenter[c].x, sz = en.z - newCenter[c].z;
              float er = kEntInfo[k].h * en.sy + std::max(kEntInfo[k].hx * en.sx, kEntInfo[k].hz * en.sz);
              if (fabsf(sx) > cR[c] * 1.45f + er || fabsf(sz) > cR[c] * 1.45f + er) continue;
              int sl = c == 0 ? std::min(lod, 1) : (entClass(k) == EC_BUILDING ? 1 : 2);
              if (c == 1 && thin && kEntInfo[k].h * en.sy < 3.f) continue;   // boulders and bushes don't reach the far cascade
              bucket[1 + c][k][sl].push_back(en);
            }
        }
      }
    }

  double tGather = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
  // ------------------------------------------------ upload all buckets at once
  entStage.clear();
  std::vector<Draw> draws[3];
  for (int p = 0; p < 3; p++)
    for (int k = 0; k < EK_COUNT; k++)
      for (int l = 0; l < ENT_LODS; l++) {
        auto& v = bucket[p][k][l];
        if (v.empty() || entRange[k].count[l] == 0) continue;
        draws[p].push_back({k, l, entStage.size(), (int)v.size()});
        entStage.insert(entStage.end(), v.begin(), v.end());
      }
  entDrawn = 0;
  for (auto& d : draws[0]) entDrawn += d.count;
  glBindVertexArray(vaoEnt);
  glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);
  glBufferData(GL_ARRAY_BUFFER, std::max<size_t>(entStage.size(), 1) * sizeof(Ent), entStage.empty() ? nullptr : entStage.data(), GL_STREAM_DRAW);
  auto issue = [&](GLuint prog, const std::vector<Draw>& list) {
    GLint uk = glGetUniformLocation(prog, "uKind");
    for (const Draw& d : list) {
      glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.first * sizeof(Ent)));
      glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.first * sizeof(Ent) + 16));
      glUniform1i(uk, d.kind);
      glDrawArraysInstanced(GL_TRIANGLES, entRange[d.kind].first[d.lod], entRange[d.kind].count[d.lod], d.count);
    }
  };
  auto bindMats = [&](GLuint prog) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb); glUniform1i(glGetUniformLocation(prog, "uAlb"), 0);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm); glUniform1i(glGetUniformLocation(prog, "uNrm"), 1);
  };
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);

  // ------------------------------------------------ shadow cascades
  for (int c = 0; c < 2; c++) {
    if (!shDirty[c]) continue;
    // light-space basis; the centre snaps to whole texels so static shadows never crawl
    vec3 up = fabsf(L.y) > 0.99f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 sx = normalize(cross(up, L)), sy = cross(L, sx);
    float texel = 2.f * cR[c] / shRes;
    vec3 ctr = newCenter[c];
    ctr.y = std::max(g_world.groundHeight(ctr.x, ctr.z, 4), 0.f);
    float a = floorf(dot(ctr, sx) / texel) * texel, b = floorf(dot(ctr, sy) / texel) * texel, cz = dot(ctr, L);
    ctr = sx * a + sy * b + L * cz;
    const float D = 3000.f;
    mat4 view = lookAt(ctr + L * D, ctr, sy);
    mat4 proj; float r = cR[c];
    proj(0, 0) = 1.f / r; proj(1, 1) = 1.f / r; proj(2, 2) = -2.f / (2.f * D); proj(2, 3) = -1.f; proj(3, 3) = 1.f;
    shVP[c] = proj * view;
    shCenter[c] = newCenter[c]; shSun[c] = L; shGen[c] = 0; shAge[c] = 0; shR[c] = cR[c]; shValid[c] = true;
    glBindFramebuffer(GL_FRAMEBUFFER, fboSh[c]);
    glViewport(0, 0, shRes, shRes);
    glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(progEntSh);
    bindMats(progEntSh);
    glUniformMatrix4fv(glGetUniformLocation(progEntSh, "uVP"), 1, GL_FALSE, shVP[c].m);
    glUniform1i(glGetUniformLocation(progEntSh, "uShadowPass"), 1);
    glUniform1f(glGetUniformLocation(progEntSh, "uTime"), fp.time);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.f);
    issue(progEntSh, draws[1 + c]);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // ------------------------------------------------ G-buffer
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
  glDrawBuffers(3, gb);
  glViewport(0, 0, rw, rh);
  glClearColor(0, 0, 0, 0); glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!draws[0].empty()) {
    glUseProgram(progEnt);
    bindMats(progEnt);
    glUniformMatrix4fv(glGetUniformLocation(progEnt, "uVP"), 1, GL_FALSE, vp.m);
    glUniform2f(glGetUniformLocation(progEnt, "uJit"), jitX, jitY);
    glUniform1f(glGetUniformLocation(progEnt, "uLogC"), 2.f / log2f(40000.f + 1.f));
    glUniform1f(glGetUniformLocation(progEnt, "uTime"), fp.time);
    glUniform1i(glGetUniformLocation(progEnt, "uShadowPass"), 0);
    glUniform3f(glGetUniformLocation(progEnt, "uCam"), cam.x, cam.y, cam.z);
    glUniform1f(glGetUniformLocation(progEnt, "uNight"), fp.night);
    glUniform1f(glGetUniformLocation(progEnt, "uWet"), fp.wet);
    glUniform1f(glGetUniformLocation(progEnt, "uSnow"), fp.snow);
    issue(progEnt, draws[0]);
  }
  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  static const bool dbg = getenv("ENTDBG") != nullptr;
  if (dbg) printf("ent: stream %.2f gather %.2f total %.2f ms, %d view / %d+%d shadow inst, dirty %d%d\n", tStream, tGather - tStream,
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count(), entDrawn, (int)0, (int)0, (int)shDirty[0], (int)shDirty[1]);
  entCpuMs = lerpf(entCpuMs, (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count(), 0.1f);
}
