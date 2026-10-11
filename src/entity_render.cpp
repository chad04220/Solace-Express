// Solace Express - environment entity rendering: chunk streaming, culling, LOD selection, instanced G-buffer and
// sun shadow-cascade passes. The lighting pass lights the G-buffer.
#include "renderer.h"
#include "entity_shaders.h"
#include "entity_lod.h"
#include "entity_bounds.h"
#include <chrono>

namespace {
EntLocalBounds closeEntityBounds[EK_COUNT],allEntityBounds[EK_COUNT];
std::vector<EntChunkBoundsCache> entityChunkBounds;
}

bool Renderer::initEntities() {
  std::vector<EVert> verts;
  buildEntityMeshes(verts, entRange);
  entBuildLocalBounds(verts,entRange,closeEntityBounds);
  entBuildLocalBounds(verts,entRange,allEntityBounds,true);
  entityChunkBounds.assign(size_t(Scenery::NC)*Scenery::NC,EntChunkBoundsCache{});
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

// The bridges' meshes, built where they stand from the world's bridges (bridge_mesh.h), with an instance buffer for the
// copies of the islands they are drawn in (an instance's position: the copy's offset)
void Renderer::uploadBridges() {
  std::vector<EVert> verts;
  buildBridgeMeshes(g_world.bridges, verts, bridgeRange);
  bridgeFrom = g_world.bridges.data(); bridgeFromN = g_world.bridges.size();
  if (!vaoBridge) { glGenVertexArrays(1, &vaoBridge); glGenBuffers(1, &vboBridge); glGenBuffers(1, &vboBridgeInst); }
  glBindVertexArray(vaoBridge);
  glBindBuffer(GL_ARRAY_BUFFER, vboBridge);
  glBufferData(GL_ARRAY_BUFFER, std::max<size_t>(verts.size(), 1) * sizeof(EVert), verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(EVert), (void*)24);
  glBindBuffer(GL_ARRAY_BUFFER, vboBridgeInst);
  glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(Ent), nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)0); glVertexAttribDivisor(3, 1);
  glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)16); glVertexAttribDivisor(4, 1);
  glBindVertexArray(0);
}

// G-buffer at the render resolution (kGBuffer): (distance, octahedral normal, class) | sqrt albedo, roughness | emission (HDR), metal | ambient occlusion
void Renderer::createGBuffer() {
  auto mk = [&](GLuint& t, GLenum ifmt, GLenum fmt, GLenum type) {
    if (t) glDeleteTextures(1, &t);
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, W, H, 0, fmt, type, nullptr);   // (full view size: the render scale uses a corner)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mk(texGB[0], GL_RGBA32F, GL_RGBA, GL_FLOAT);
  mk(texGB[1], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  mk(texGB[2], GL_RGBA16F, GL_RGBA, GL_FLOAT);   // emission (HDR), metal
  mk(texGB[3], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);   // ambient occlusion, - (the raster passes' extras)
  mk(texGBDepth, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
  mk(texGB[4], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);   // the raster renderer's shadow proxy (kShadowProxyFS), its own target
  if (!fboShProxy) glGenFramebuffers(1, &fboShProxy);
  glBindFramebuffer(GL_FRAMEBUFFER, fboShProxy);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texGB[4], 0);
  if (!fboGB) glGenFramebuffers(1, &fboGB);
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  for (int i = 0; i < 4; i++) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, texGB[i], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texGBDepth, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace {
struct Draw { int kind, lod; size_t first; int count; int vehicle = -1; };
}  // namespace

void Renderer::drawEntities(const FrameParams& fp) {
  if (!progEnt[0]) return;
  auto tStart = std::chrono::steady_clock::now();
  // a camera feed (camera_feeds.cpp): only its G-buffer, at the shortest draw distances; streaming, the shadow
  // cascades and the statistics stay the main view's
  if (!feedPass) entFrame++;
  const EntRanges R = entRangesFor(quality, feedPass);
  const EntRanges shadowRanges = entBaseRangesFor(quality);
  const float farAll = std::max(R.big, R.build) + 300.f, farDetail = std::max(std::max(R.tree, R.rock), R.bush) + 300.f;
  vec3 cam = fp.camPos;
  const int rad = (int)ceilf(farAll / Scenery::CH) + 1;
  // the islands and, near the map's seam, their copies across it (the map repeats: world.h WRAP_HALF): each one's
  // chunks are the islands' own, looked up from where the camera is relative to that copy and drawn moved by its offset
  struct Copy { float ox, oz; };
  std::vector<Copy> copies;
  for (int kz = -1; kz <= 1; kz++) for (int kx = -1; kx <= 1; kx++) {
    const float ox = kx * WRAP_SPAN, oz = kz * WRAP_SPAN;
    const float ex = std::max(std::max(-WORLD_HALF + ox - cam.x, cam.x - (WORLD_HALF + ox)), 0.f), ez = std::max(std::max(-WORLD_HALF + oz - cam.z, cam.z - (WORLD_HALF + oz)), 0.f);
    if (ex * ex + ez * ez < farAll * farAll) copies.push_back({ox, oz});
  }
  // how far beyond a shadow cascade a chunk's casters can still throw shadow into it: its tallest caster times the
  // shadow length per metre of height at this sun, plus how far footprints spill past the chunk. (A fixed pad
  // dropped tall buildings whose chunk began just outside it before the per-caster test could accept them.)
  const vec3 Lsun = normalize(fp.sunDir);
  const float shReach = sqrtf(std::max(1.f - Lsun.y * Lsun.y, 0.f)) / std::max(Lsun.y, 0.15f);   // shadow length per metre of height
  auto chunkShPad = [&](const Scenery::Chunk* ch) { return ch ? ch->hmax * shReach + ch->reach + 60.f : 300.f; };
  // ------------------------------------------------ streaming: queue missing chunks by distance, generate within a time budget
  struct Need { float d; int cx, cz, level; };
  if (!feedPass) {
    std::vector<Need> need;
    for (const Copy& cp : copies) {
      const vec3 camL = cam - vec3(cp.ox, 0.f, cp.oz);   // (the camera as that copy of the islands sees it)
      const int lcx = Scenery::chunkOf(camL.x), lcz = Scenery::chunkOf(camL.z);
      for (int dz = -rad; dz <= rad; dz++)
        for (int dx = -rad; dx <= rad; dx++) {
          int cx = lcx + dx, cz = lcz + dz;
          if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
          float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
          float ex = std::max(std::max(x0 - camL.x, camL.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - camL.z, camL.z - z0 - Scenery::CH), 0.f);
          float d = sqrtf(ex * ex + ez * ez);
          int want = d < farDetail ? 2 : d < farAll ? 1 : 0;
          if (!want) continue;
          Scenery::Chunk* c = g_scenery.get(cx, cz);
          if (c) c->lastUse = entFrame;
          if (!c || c->level < want) need.push_back({d, cx, cz, want});
        }
    }
    // a new chunk inside a shadow cascade's area makes that cascade re-render
    auto added = [&](int cx, int cz) {
      entGenCount++;
      for (int c = 0; c < 2; c++) {
        // only where its shadows can show: the shader fades each cascade out at kShFade1 of its radius, plus room for
        // tall casters just outside. (A wider test re-rendered the far 4096^2 map nearly every frame in flight, since
        // tree chunks keep streaming in a few km ahead.)
        float r = (c == 0 ? R.sh0 : R.sh1) * kShFade1 + std::max(300.f, chunkShPad(g_scenery.get(cx, cz)));
        for (const Copy& cp : copies) {
          float x0 = Scenery::chunkX0(cx) + cp.ox, z0 = Scenery::chunkX0(cz) + cp.oz;
          float ex = std::max(std::max(x0 - shCenter[c].x, shCenter[c].x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - shCenter[c].z, shCenter[c].z - z0 - Scenery::CH), 0.f);
          if (ex < r && ez < r) shGen[c] = -1;
        }
      }
    };
    // chunks the worker threads finished since last frame
    std::vector<int> got;
    g_scenery.pump(got);
    for (int idx : got) {
      int cx = idx % Scenery::NC, cz = idx / Scenery::NC;
      g_scenery.get(cx, cz)->lastUse = entFrame;
      added(cx, cz);
    }
    if (!got.empty())   // drop the ones that just arrived from the list
      need.erase(std::remove_if(need.begin(), need.end(), [](const Need& n) { Scenery::Chunk* c = g_scenery.get(n.cx, n.cz); return c && c->level >= n.level; }), need.end());
    entPending = (int)need.size();
    if (!need.empty()) {
      std::sort(need.begin(), need.end(), [](const Need& a, const Need& b) { return a.d < b.d; });
      auto t0 = std::chrono::steady_clock::now();
      for (const Need& n : need) {
        // close chunks are generated here and now so they are never missing; the rest go to the worker threads
        // (or, without workers, are generated here within the frame's time budget)
        bool now = entSync || n.d <= 700.f;
        if (!now && g_scenery.request(n.cx, n.cz, n.level)) continue;
        if (!now && g_scenery.workers() > 0) break;   // queue full: ask again next frame
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!now && ms > entBudgetMs) break;
        g_scenery.ensure(n.cx, n.cz, n.level)->lastUse = entFrame;
        entPending--;
        added(n.cx, n.cz);
      }
    }
    // the next place the camera will cut to (the menu tour): its near chunks go to the worker threads now, and are kept
    if (fp.prefetchOn && g_scenery.workers() > 0) {
      vec3 pc = fp.prefetchPos;
      const int pcx = Scenery::chunkOf(pc.x), pcz = Scenery::chunkOf(pc.z), prad = rad;   // (all of it, as the view there will want it: the flight's loading screen waits for every chunk)
      for (int dz = -prad; dz <= prad; dz++)
        for (int dx = -prad; dx <= prad; dx++) {
          int cx = pcx + dx, cz = pcz + dz;
          if (cx < 0 || cz < 0 || cx >= Scenery::NC || cz >= Scenery::NC) continue;
          float x0 = Scenery::chunkX0(cx), z0 = Scenery::chunkX0(cz);
          float ex = std::max(std::max(x0 - pc.x, pc.x - x0 - Scenery::CH), 0.f), ez = std::max(std::max(z0 - pc.z, pc.z - z0 - Scenery::CH), 0.f);
          float d = sqrtf(ex * ex + ez * ez);
          int want = d < farDetail ? 2 : d < farAll ? 1 : 0;
          if (!want) continue;
          Scenery::Chunk* c = g_scenery.get(cx, cz);
          if (c) { c->lastUse = entFrame; if (c->level >= want) continue; }
          if (!g_scenery.request(cx, cz, want)) break;   // queue full: more next frame
        }
    }
    if (entFrame % 240 == 0) g_scenery.trim(cam, farDetail + 1200.f, farAll + 2500.f, entFrame);
    entChunks = (int)g_scenery.generated();
  }

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
  if (shRes != R.shRes && !feedPass) {   // (re)create the cascade maps
    shRes = R.shRes;
    for (int c = 0; c < 2; c++) {
      if (texSh[c]) glDeleteTextures(1, &texSh[c]);
      glGenTextures(1, &texSh[c]); glBindTexture(GL_TEXTURE_2D, texSh[c]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, shRes, shRes, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      // depth comparison in the sampler: one linear lookup is the 2x2 PCF the shader used to do with four fetches
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
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
  if (!feedPass) {
    for (int c = 0; c < 2; c++) shIdeal[c] = cam + fwdH * (cR[c] * 0.45f);   // the shader fades shadows around these
    entTreeFar = R.tree;
  }
  for (int c = 0; c < 2 && sunUp && !feedPass; c++) {
    newCenter[c] = shIdeal[c];
    float moved = length(vec3(newCenter[c].x - shCenter[c].x, 0, newCenter[c].z - shCenter[c].z));
    shAge[c]++;
    shDirty[c] = !shValid[c] || moved > cR[c] * 0.12f || fabsf(newCenter[c].y - shCenter[c].y) > cR[c] * 0.25f || dot(L, shSun[c]) < 0.99998f ||
                 shGen[c] < 0 || shR[c] != cR[c] || shAge[c] > 600;
  }

  // Refresh only cascades affected by a changed dynamic vehicle. Removing a vehicle refreshes its old shadow.
  uint64_t nextGroundKey[2] = {};
  for(int c=0;c<2 && sunUp && !feedPass;++c) {
    vec3 center=shDirty[c]?newCenter[c]:shCenter[c];
    nextGroundKey[c]=groundVehicleShadowKey(fp.groundVehicles,center,cR[c],shReach);
    if(nextGroundKey[c]!=groundShadowKey[c]) shDirty[c]=true;
  }
  // ------------------------------------------------ the bridges: each in range and in view, in each copy of the islands; and
  // those that throw shadow into a cascade being redrawn
  if (g_worldStage >= 3 && (bridgeFrom != (const void*)g_world.bridges.data() || bridgeFromN != g_world.bridges.size())) uploadBridges();
  struct BridgeDraw { int bridge, copy; };
  std::vector<BridgeDraw> bridgeView, bridgeSh[2];
  {
    const float far = std::max(R.build, R.big);
    for (size_t bi = 0; bi < bridgeRange.size() && bi < g_world.bridges.size(); bi++) {
      if (!bridgeRange[bi].count) continue;
      const Bridge& b = g_world.bridges[bi];
      for (size_t ci = 0; ci < copies.size(); ci++) {
        const float x0 = b.minX + copies[ci].ox, x1 = b.maxX + copies[ci].ox, z0 = b.minZ + copies[ci].oz, z1 = b.maxZ + copies[ci].oz;
        const float dx = std::max({x0 - cam.x, cam.x - x1, 0.f}), dz = std::max({z0 - cam.z, cam.z - z1, 0.f});
        if (dx * dx + dz * dz < far * far && boxVisible(x0, b.minY, z0, x1, b.maxY, z1)) bridgeView.push_back({(int)bi, (int)ci});
        for (int c = 0; c < 2 && sunUp && !feedPass; c++) {
          const float pad = (b.maxY - b.minY) * shReach + 60.f, r = cR[c] + pad;
          if (shDirty[c] && x1 > newCenter[c].x - r && x0 < newCenter[c].x + r && z1 > newCenter[c].z - r && z0 < newCenter[c].z + r) bridgeSh[c].push_back({(int)bi, (int)ci});
        }
      }
    }
  }
  // ------------------------------------------------ gather instances into (pass, kind, lod) buckets
  static std::vector<Ent> bucket[3][EK_COUNT][ENT_LODS];   // pass 0 view, 1/2 shadow cascades
  for (auto& a : bucket) for (auto& b : a) for (auto& v : b) v.clear();
  bool anyCrater = g_scenery.anyGone();
  // the chunks nearest first, so each bucket's instances go out front to back and the depth test rejects what nearer
  // buildings hide before it is shaded (row by row, a heading towards -z drew the city back to front). The offsets
  // sorted once for the widest reach; a narrower one (a camera feed's) skips the rest
  static std::vector<std::pair<short, short>> ring; static int ringRad = -1;
  if (rad > ringRad) {
    ring.clear();
    for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) ring.push_back({(short)dx, (short)dz});
    std::stable_sort(ring.begin(), ring.end(), [](const std::pair<short, short>& a, const std::pair<short, short>& b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
    ringRad = rad;
  }
  for (const Copy& cp : copies) {
    const float ox = cp.ox, oz = cp.oz;
    const vec3 camL = cam - vec3(ox, 0.f, oz);   // (the camera as this copy of the islands sees it: what is drawn moves by the offset)
    const int lcx = Scenery::chunkOf(camL.x), lcz = Scenery::chunkOf(camL.z);
    const vec3 shC[2] = {newCenter[0] - vec3(ox, 0.f, oz), newCenter[1] - vec3(ox, 0.f, oz)};
    auto moved = [&](const Ent& e) { Ent w = e; w.x += ox; w.z += oz; return w; };
  for (const auto& off : ring) {
      const int dx = off.first, dz = off.second;
      if (dx < -rad || dx > rad || dz < -rad || dz > rad) continue;
      Scenery::Chunk* ch = g_scenery.get(lcx + dx, lcz + dz);
      if (!ch || ch->ents.empty()) continue;
      float x0 = Scenery::chunkX0(lcx + dx), z0 = Scenery::chunkX0(lcz + dz), x1 = x0 + Scenery::CH, z1 = z0 + Scenery::CH;
      float ex = std::max(std::max(x0 - camL.x, camL.x - x1), 0.f), ez = std::max(std::max(z0 - camL.z, camL.z - z1), 0.f);
      float ey = std::max(std::max(ch->ymin - camL.y, camL.y - ch->ymax), 0.f);
      float dmin = sqrtf(ex * ex + ez * ez + ey * ey);
      float fx = std::max(fabsf(x0 - camL.x), fabsf(x1 - camL.x)), fz = std::max(fabsf(z0 - camL.z), fabsf(z1 - camL.z));
      float fy = std::max(fabsf(ch->ymin - camL.y), fabsf(ch->ymax - camL.y));
      float dmax = sqrtf(fx * fx + fz * fz + fy * fy);
      bool affected = anyCrater && g_scenery.chunkAffected(lcx + dx, lcz + dz);
      // A nominal 12 m chunk pad can discard a wide terminal, an animated
      // crown, or buried foundations before their correct instance bounds run.
      // Rebuild this authored all-LOD union only when chunk content changes.
      auto& chunkBounds=entityChunkBounds[size_t(lcz+dz)*Scenery::NC+lcx+dx];
      const auto& actual=chunkBounds.get(*ch,allEntityBounds);
      bool inView=dmin<farAll && (fp.pano>0.f || (chunkBounds.valid &&
        boxVisible(actual.lo.x+ox,actual.lo.y,actual.lo.z+oz,actual.hi.x+ox,actual.hi.y,actual.hi.z+oz)));
      bool inSh[2] = {false, false};
      for (int c = 0; c < 2; c++)
        if (shDirty[c]) {
          // the cascade box, grown by how far this chunk's tallest casters reach (tall things outside it still cast
          // into it; the per-caster test below then keeps only those that do)
          float r = cR[c] + std::max(210.f, chunkShPad(ch));
          float hx = std::max(std::max(x0 - shC[c].x, shC[c].x - x1), 0.f), hz = std::max(std::max(z0 - shC[c].z, shC[c].z - z1), 0.f);
          inSh[c] = hx < r && hz < r;
        }
      if (!inView && !inSh[0] && !inSh[1]) continue;
      for (int k = 0; k < EK_COUNT; k++) {
        uint32_t b = ch->off[k], e = ch->off[k + 1];
        if (b == e) continue;
        float far = entRangeOf(R, k), l0, l1;
        entLodLimits(R, k, l0, l1);
        const float close = feedPass ? 0.f : entCloseLimit(R,k);
        float shadowL0, shadowL1;
        entLodLimits(shadowRanges, k, shadowL0, shadowL1);
        bool thin = entThins(k);
        bool viewK = inView && dmin < far;
        if (!viewK && !inSh[0] && !inSh[1]) continue;
        // the whole chunk in one LOD band and inside the draw distance: hand its instances over in one block (the
        // vertex shader does the distance thinning per instance, exactly as below)
        int lodN = entDetailAt(dmin, close, l0, l1), lodF = entDetailAt(dmax, close, l0, l1);
        bool bulk = viewK && !affected && dmax < far && lodN == lodF && lodN != kEntCloseLod && !entDetailSpanFades(k,dmin,dmax,close,l0,l1);
        if (bulk) {
          // thinned kinds: only the prefix that can survive anywhere in the chunk (keys ascending, nearest point's
          // keep fraction); the vertex shader thins the rest of the way per instance
          uint32_t eb = e;
          if (thin) {
            float kp = entKeepDrawn(k, std::max(dmin, 1.f));
            eb = (uint32_t)(std::lower_bound(ch->ents.begin() + b, ch->ents.begin() + e, kp, [](const Ent& x, float v) { return entThinKey(x) < v; }) - ch->ents.begin());
          }
          auto& bk = bucket[0][k][lodN]; const size_t n0 = bk.size(); bk.insert(bk.end(), ch->ents.begin() + b, ch->ents.begin() + eb);
          if (ox != 0.f || oz != 0.f) for (size_t j = n0; j < bk.size(); j++) { bk[j].x += ox; bk[j].z += oz; }
        }
        if (bulk && !inSh[0] && !inSh[1]) continue;
        for (uint32_t i = b; i < e; i++) {
          const Ent& e0 = ch->ents[i];
          if (affected && g_scenery.destroyed(e0)) continue;
          const Ent en = moved(e0);   // (where it is drawn)
          float ddx = en.x - cam.x, ddy = en.y - cam.y, ddz = en.z - cam.z;
          float d = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
          int lod = entDetailAt(d, close, l0, l1);
          bool keep = !thin || entThinKey(en) < entKeepDrawn(k, d);   // (the ground texture takes over distant forest; fading in or out, still drawn)
          // Chunk culling is deliberately coarse. Close meshes can be thousands
          // of triangles each, so reject their offscreen instances as one unit,
          // including the near-detail half of a close cross-fade. The panorama
          // has a cylindrical frustum; ordinary perspective planes do not apply.
          const bool closeVisible = lod != kEntCloseLod || fp.pano > 0.f || !closeEntityBounds[k].valid ||
            entBoundsVisible(entInstanceBounds(closeEntityBounds[k],k,en),pl);
          if (viewK && d < far && !bulk && keep && closeVisible) {
            bucket[0][k][lod].push_back(en);
            // (and the farther detail level too where the two are cross-fading)
            const int also = entDetailAlso(k,d,close,l0,l1);
            if (also >= 0 && entRange[k].count[also] > 0) bucket[0][k][also].push_back(en);
          }
          // shadows: only what can cast into the faded circle the shader uses (radius kShFade1 x R around the
          // centre; a caster's shadow reaches h / tan(sun elevation) away), thinned like the trees themselves
          bool shKeep = !thin || entThinKey(en) < entKeep(k, d);   // shadows only from what is drawn (from halfway through its fade)
          for (int c = 0; c < 2; c++)
            if (inSh[c] && shKeep && k != EK_RWYLIGHT && k != EK_PAPI) {
              float sx = e0.x - shC[c].x, sz = e0.z - shC[c].z;
              float h = kEntInfo[k].h * en.sy, er = std::max(kEntInfo[k].hx * en.sx, kEntInfo[k].hz * en.sz) + h * shReach;
              float cr = cR[c] * kShFade1 + er; if (sx * sx + sz * sz > cr * cr) continue;
              // Keep shadow detail at its original threshold when extending the view LODs.
              int sl = c == 0 ? std::min(entLodAt(d, shadowL0, shadowL1), 1) : (entClass(k) == EC_BUILDING ? 1 : 2);
              if (c == 1 && thin && kEntInfo[k].h * en.sy < 3.f) continue;   // boulders and bushes don't reach the far cascade
              bucket[1 + c][k][sl].push_back(en);
            }
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
  // Dynamic vehicles use the same meshes/Ent instance layout, with a separate pose per small draw.
  for(int vi=0;vi<(int)fp.groundVehicles.size();++vi) {
    const auto& v=fp.groundVehicles[vi];if(!validGroundVehicle(v)) continue;
    const Ent& e=v.entity;int k=v.kind;float d=length(vec3(e.x,e.y,e.z)-cam),l0,l1;entLodLimits(R,k,l0,l1);
    float close=feedPass?0.f:entCloseLimit(R,k);int lod=entDetailAt(d,close,l0,l1);
    size_t first=entStage.size();entStage.push_back(e);
    const bool closeVisible = lod != kEntCloseLod || fp.pano > 0.f || !closeEntityBounds[k].valid ||
      entBoundsVisible(entInstanceBounds(closeEntityBounds[k],k,e),pl);
    if(d<entRangeOf(R,k) && entRange[k].count[lod]>0 && closeVisible) {
      draws[0].push_back({k,lod,first,1,vi});
      int also=entDetailAlso(k,d,close,l0,l1);
      if(also>=0 && entRange[k].count[also]>0) draws[0].push_back({k,also,first,1,vi});
    }
    for(int c=0;c<2 && sunUp && !feedPass;++c) {
      const auto& info=kEntInfo[k];float pad=std::max(info.hx*e.sx,info.hz*e.sz)+info.h*e.sy*shReach+60.f;
      if(shDirty[c] && std::fabs(e.x-newCenter[c].x)<cR[c]+pad && std::fabs(e.z-newCenter[c].z)<cR[c]+pad)
        draws[1+c].push_back({k,0,first,1,vi});
    }
  }
  if (!feedPass) { entDrawn = 0; for (auto& d : draws[0]) entDrawn += d.count; }
  glBindVertexArray(vaoEnt);
  glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);
  glBufferData(GL_ARRAY_BUFFER, std::max<size_t>(entStage.size(), 1) * sizeof(Ent), entStage.empty() ? nullptr : entStage.data(), GL_STREAM_DRAW);
  // each draw with its kind's class's own build (progs: the pass's three, set up alike): the program changes only
  // where the class does
  auto issue = [&](const GLuint* progs, const std::vector<Draw>& list) {
    GLuint prog = 0; GLint uk = -1, uf = -1, ut = -1, ur = -1, ul = -1, ull = -1, uclose = -1, uw0 = -1, uw1 = -1;
    for (const Draw& d : list) {
      if (progs[entClass(d.kind)] != prog) {
        prog = progs[entClass(d.kind)]; glUseProgram(prog);
        uk = glGetUniformLocation(prog, "uKind"); uf = glGetUniformLocation(prog, "uFar"); ut = glGetUniformLocation(prog, "uThin"); ur = glGetUniformLocation(prog, "uThinRef");
        ul = glGetUniformLocation(prog, "uLod"); ull = glGetUniformLocation(prog, "uLodL"); uclose = glGetUniformLocation(prog,"uCloseLod");
        uw0 = glGetUniformLocation(prog, "uWheel0"); uw1 = glGetUniformLocation(prog, "uWheel1");
      }
      if(d.vehicle>=0) { const float* a=fp.groundVehicles[d.vehicle].angle;glUniform4fv(uw0,1,a);glUniform2f(uw1,a[4],a[5]); }
      else { glUniform4f(uw0,0,0,0,0);glUniform2f(uw1,0,0); }
      glUniform1f(uf, entRangeOf(R, d.kind)); glUniform1f(ut, entThins(d.kind) ? 1.f : 0.f); glUniform1f(ur, entThinRef(d.kind));
      { float l0, l1; entLodLimits(R, d.kind, l0, l1); glUniform1i(ul, d.lod); glUniform2f(ull, l0, l1); glUniform1f(uclose,feedPass?0.f:entCloseLimit(R,d.kind)); }   // (the cross-fade between detail levels)
      glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.first * sizeof(Ent)));
      glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.first * sizeof(Ent) + 16));
      glUniform1i(uk, d.kind);
      glDrawArraysInstanced(GL_TRIANGLES, entRange[d.kind].first[d.lod], entRange[d.kind].count[d.lod], d.count);
    }
  };
  // the bridges: their meshes with the buildings' program, one instance at each copy's offset (no thinning or detail
  // levels: uLod -1)
  std::vector<Ent> bridgeInst;
  for (const Copy& cp : copies) bridgeInst.push_back(Ent{cp.ox, 0.f, cp.oz, 0.f, 1.f, 1.f, 1.f, 0.5f});
  if (vaoBridge && !bridgeInst.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, vboBridgeInst); glBufferData(GL_ARRAY_BUFFER, bridgeInst.size() * sizeof(Ent), bridgeInst.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);   // (the entities' instance pointers are set against theirs)
  }
  auto issueBridges = [&](GLuint prog, const std::vector<BridgeDraw>& list) {
    if (list.empty() || !vaoBridge) return;
    glUseProgram(prog);
    glBindVertexArray(vaoBridge); glBindBuffer(GL_ARRAY_BUFFER, vboBridgeInst);
    glUniform1i(glGetUniformLocation(prog, "uKind"), kBridgeKind); glUniform1f(glGetUniformLocation(prog, "uFar"), 1e9f);
    glUniform1f(glGetUniformLocation(prog, "uThin"), 0.f); glUniform1f(glGetUniformLocation(prog, "uThinRef"), 1.f);
    glUniform1i(glGetUniformLocation(prog, "uLod"), -1); glUniform2f(glGetUniformLocation(prog, "uLodL"), 0.f, 0.f);
    glUniform1f(glGetUniformLocation(prog, "uCloseLod"), 0.f);
    glUniform4f(glGetUniformLocation(prog, "uWheel0"), 0, 0, 0, 0); glUniform2f(glGetUniformLocation(prog, "uWheel1"), 0, 0);
    for (const BridgeDraw& d : list) {
      glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.copy * sizeof(Ent)));
      glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Ent), (void*)(d.copy * sizeof(Ent) + 16));
      glDrawArraysInstanced(GL_TRIANGLES, bridgeRange[d.bridge].first, bridgeRange[d.bridge].count, 1);
    }
    glBindVertexArray(vaoEnt); glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);
  };
  auto bindMats = [&](GLuint prog) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb); glUniform1i(glGetUniformLocation(prog, "uAlb"), 0);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm); glUniform1i(glGetUniformLocation(prog, "uNrm"), 1);
    bindEnvironmentMaterials(prog, 0, 1);
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
    for (GLuint p : progEntSh) {
      glUseProgram(p);
      bindMats(p);
      glUniformMatrix4fv(glGetUniformLocation(p, "uVP"), 1, GL_FALSE, shVP[c].m);
      glUniform1i(glGetUniformLocation(p, "uShadowPass"), 1);
      glUniform1f(glGetUniformLocation(p, "uTime"), fp.time);
      glUniform3f(glGetUniformLocation(p, "uWind"), fp.windSock.x, fp.windSock.y, fp.windSock.z);
    }
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.f);
    issue(progEntSh, draws[1 + c]);
    issueBridges(progEntSh[EC_BUILDING], bridgeSh[c]);
    groundShadowKey[c]=nextGroundKey[c];
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // ------------------------------------------------ G-buffer
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, gb);
  glViewport(0, 0, rw, rh);
  glClearColor(0, 0, 0, 0); glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // the raster path: the player's aircraft is the nearest thing in the frame (in the cockpit, half the screen), so its
  // baked mesh's depth goes in before anything else and everything behind the cabin walls or the airframe fails the
  // depth test before it is shaded (the scenery here, the terrain and the sea after; the mesh is shaded later, in the
  // objects pass, on exactly this depth)
  if (earlyMesh) {
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    drawPlaneMeshDepth(fp, *earlyMesh, fp.plane.rot, fp.plane.pos, -1);
    glBindVertexArray(vaoEnt); glBindBuffer(GL_ARRAY_BUFFER, vboEntInst);
  }
  if (!draws[0].empty() || !bridgeView.empty()) {
    // near detail levels first, and buildings and rocks before trees: the big near occluders fill the depth buffer
    // early, so less of what lies behind them gets shaded
    std::stable_sort(draws[0].begin(), draws[0].end(), [](const Draw& a, const Draw& b) {
      if (a.lod != b.lod) return (a.lod == kEntCloseLod ? -1 : a.lod) < (b.lod == kEntCloseLod ? -1 : b.lod);
      return (2 - entClass(a.kind)) < (2 - entClass(b.kind));
    });
    const mat4 pv = viewMat(fp);
    for (GLuint p : progEnt) {
      glUseProgram(p);
      bindMats(p);
      glUniformMatrix4fv(glGetUniformLocation(p, "uVP"), 1, GL_FALSE, vp.m);
      glUniformMatrix4fv(glGetUniformLocation(p, "uPanoView"), 1, GL_FALSE, pv.m);
      glUniform2f(glGetUniformLocation(p, "uPano"), fp.pano, fp.panoTanY);
      glUniform2f(glGetUniformLocation(p, "uJit"), jitX, jitY);
      glUniform1f(glGetUniformLocation(p, "uLogC"), 2.f / log2f(40000.f + 1.f));
      glUniform1f(glGetUniformLocation(p, "uTime"), fp.time);
      glUniform3f(glGetUniformLocation(p, "uWind"), fp.windSock.x, fp.windSock.y, fp.windSock.z);
      glUniform1i(glGetUniformLocation(p, "uShadowPass"), 0);
      glUniform3f(glGetUniformLocation(p, "uCam"), cam.x, cam.y, cam.z);
      glUniform3f(glGetUniformLocation(p, "uCamV"), cam.x, cam.y, cam.z);
      glUniform1f(glGetUniformLocation(p, "uNight"), fp.night);
      glUniform1f(glGetUniformLocation(p, "uRwyLights"), fp.rwyLights);
      glUniform1f(glGetUniformLocation(p, "uWet"), fp.wet);
      glUniform1f(glGetUniformLocation(p, "uSnow"), fp.snow);
    }
    if (!(dbgOff & kProbeScenery)) { issueBridges(progEnt[EC_BUILDING], bridgeView); issue(progEnt, draws[0]); }
  }
  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  static const bool dbg = getenv("ENTDBG") != nullptr;
  if (dbg && !feedPass) {
    int nSh[2] = {0, 0};
    for (int c = 0; c < 2; c++) for (auto& d : draws[1 + c]) nSh[c] += d.count;
    printf("ent: stream %.2f gather %.2f total %.2f ms, %d view / %d+%d shadow inst, dirty %d%d\n", tStream, tGather - tStream,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count(), entDrawn, nSh[0], nSh[1], (int)shDirty[0], (int)shDirty[1]);
  }
  if (!feedPass) entCpuMs = lerpf(entCpuMs, (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count(), 0.1f);
}
