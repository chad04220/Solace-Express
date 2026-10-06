// Solace Express - the raster renderer (Renderer::mode == 1): a deferred pipeline that draws every surface with the
// rasterizer and lights the frame once. Shared with the ray tracer: the scenery pass and its shadow cascades, the
// terrain-shadow bake, the display atlases, the cloud pass, and everything after the lit frame (TAA, sprites, bloom,
// light shafts, post). See docs/RENDERER_REBUILD.md.
#include "renderer.h"
#include "shaders.h"

bool Renderer::compileRaster() {
  std::string e;
  progLight = linkProgramCached(kFullscreenVS, lightFSAssembly(""), e);
  if (!progLight) { error = "Lighting shader: " + e; return false; }
  progObjects = linkProgramCached(kFullscreenVS, objectsFSAssembly(""), e);
  if (!progObjects) { error = "Objects shader: " + e; return false; }
  progShProxy = linkProgramCached(kFullscreenVS, shadowProxyFSAssembly(""), e);
  if (!progShProxy) { error = "Shadow proxy shader: " + e; return false; }
  progEffects = linkProgramCached(kFullscreenVS, effectsFSAssembly(""), e);
  if (!progEffects) { error = "Effects shader: " + e; return false; }
  // the airframe shadow maps: the baked mesh (and the moving hull) from a light, plain depth
  static const char* kShMapVS = "#version 330 core\nlayout(location = 0) in vec3 aPos; uniform mat4 uVP; uniform mat3 uRot; uniform vec3 uPos;\nvoid main(){ gl_Position = uVP*vec4(uRot*aPos + uPos, 1.0); }\n";
  progShMap = linkProgramCached(kShMapVS, "#version 330 core\nvoid main(){}\n", e);
  if (!progShMap) { error = "Shadow map shader: " + e; return false; }
  progShMov = linkProgramCached(kShMapVS, "#version 330 core\nout float oM; void main(){ oM = 1.0; }\n", e);
  if (!progShMov) { error = "Shadow map (moving hull) shader: " + e; return false; }
  if (!compilePlaneMesh()) return false;
  return compileTerrainMesh();
}

// The lit frame for a view from the G-buffer the raster passes filled: into texRaw (colour + TAA class), texDepth (view
// distance) and texCloudMask, exactly what the ray tracer writes, so the clouds, the TAA and everything after run as before.
void Renderer::rasterWorld(const FrameParams& fp) {
  // scenery: its shadow cascades and the G-buffer, which it clears (entity_render.cpp)
  envOn = false;   // (the terrain envelope is the ray tracer's)
  ckMaskPrev = false;   // (so is last frame's cabin mask: here the cabin itself is drawn into that depth and would fail it)
  drawEntities(fp);
  // the ground and the sea, depth-tested against the scenery
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, gb);
  glViewport(0, 0, rw, rh);
  drawTerrainMesh(fp);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The aircraft, the traffic, wreck pieces, debris and the UFO: marched through their distance fields by a full-screen
// pass that writes the G-buffer with depth (the terrain and the scenery occlude them through the depth test). The
// player's aircraft starts its march on its rasterized hull, as in the ray tracer.
void Renderer::rasterObjects(const FrameParams& fp) {
  hullOn = false;
  const int slot = fp.plane.PS[3] > 0.5f ? 1 : 0;
  // the player's aircraft as a mesh where it never moves (aircraft_mesh.cpp): then only its moving parts are marched,
  // from the hull round them; without one, the whole airframe is marched from its full hull as the ray tracer does
  const bool meshUse = planeMeshWanted(fp) && fp.pano <= 0.f;
  const uint64_t meshK = meshUse ? hullKey(fp, slot) : 0;
  auto pm = meshUse ? planeMeshes.find(meshK) : planeMeshes.end();
  const bool meshOn = pm != planeMeshes.end() && pm->second.ok;
  const bool hullUse = !meshOn && hullWanted(fp) && fp.pano <= 0.f;   // (the hulls are flat rasters: a panorama camera marches without them)
  const uint64_t hullK = hullUse ? hullKey(fp, slot) : 0;
  // (with the mesh the hull is the moving parts' only: no near segment even with the eye inside it - a ray starting inside
  // reads 0 and marches from the eye - and the march ends where the ray leaves the moving volume: the cabin's
  // panel, roof and seats are the mesh's, and the yoke's pixels alone march, as deep as the yoke's hull)
  static const bool oldNear = getenv("OLDNEAR") != nullptr;   // (debug A/B: the 1.2 m near march with the eye inside the moving hull, no exit)
  if (meshOn) { if (oldNear) drawHull(fp, slot, pm->second.movKey, pm->second.eyeInMov ? -1.f : 0.f); else drawHull(fp, slot, pm->second.movKey, 0.f, true); }
  else if (hullUse && hulls.count(hullK)) drawHull(fp, slot, hullK);
  // the traffic: the same light aircraft, each with its model's mesh when one is baked (then its hull is the moving
  // parts' too), else its full hull
  const PlaneMesh* trafMesh[kMaxTrafficDrawn] = {};
  const int trafN = std::min(fp.trafficN, kMaxTrafficDrawn);
  if (!meshOff && progPlaneMesh) for (int k = 0; k < trafN; k++) { auto it = planeMeshes.find(trafficModelKey(fp.traffic[k].t)); if (it != planeMeshes.end() && it->second.ok) trafMesh[k] = &it->second; }
  if (fp.pano <= 0.f) drawTrafficHulls(fp, trafMesh); else trafHullOn = false;
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, gb);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  if (meshOn) drawPlaneMesh(fp, pm->second, fp.plane.rot, fp.plane.pos, -1);
  for (int k = 0; k < trafN; k++) {
    if (!trafMesh[k]) continue;
    const float* t = fp.traffic[k].t;
    float rot[9] = {t[25 * 4], t[25 * 4 + 1], t[25 * 4 + 2], t[26 * 4], t[26 * 4 + 1], t[26 * 4 + 2], t[27 * 4], t[27 * 4 + 1], t[27 * 4 + 2]};
    drawPlaneMesh(fp, *trafMesh[k], rot, vec3(t[24 * 4], t[24 * 4 + 1], t[24 * 4 + 2]), k);
  }
  // the depth so far (the terrain, the scenery, the meshes) copied out: the march goes no further than it on any ray,
  // and a pixel whose moving hull begins behind it marches nothing (the cabin's panel and roof come from the mesh: only
  // the yoke's pixels, in front of it, march - and the traffic's and the debris' traces stop at the nearest surface too)
  bool sceneZ = false;
  if (glBlitFramebuffer) {
    if (!texDepthCopy || depthCopyW < rw || depthCopyH < rh) {
      int w = std::max(rw, depthCopyW), h = std::max(rh, depthCopyH);
      if (texDepthCopy) glDeleteTextures(1, &texDepthCopy);
      glGenTextures(1, &texDepthCopy); glBindTexture(GL_TEXTURE_2D, texDepthCopy);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindTexture(GL_TEXTURE_2D, 0);
      if (!fboDepthCopy) glGenFramebuffers(1, &fboDepthCopy);
      glBindFramebuffer(GL_FRAMEBUFFER, fboDepthCopy);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texDepthCopy, 0);
      { GLenum none = GL_NONE; glDrawBuffers(1, &none); } glReadBuffer(GL_NONE);
      depthCopyW = w; depthCopyH = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboGB); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboDepthCopy);
    glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
    glDrawBuffers(4, gb);
    sceneZ = true;
  }
  setRT(progObjects, fp);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  glUniform1f(U(progObjects, "uLogC"), 2.f / log2f(40000.f + 1.f));
  glUniform1i(U(progObjects, "uMeshOn"), meshOn ? 1 : 0);
  glActiveTexture(GL_TEXTURE0 + 28); glBindTexture(GL_TEXTURE_2D, sceneZ ? texDepthCopy : 0); glUniform1i(U(progObjects, "uSceneZ"), 28); glUniform1i(U(progObjects, "uSceneZOn"), sceneZ ? 1 : 0);
  glBindVertexArray(vaoEmpty);
  static const bool noMarch = getenv("NOMARCH") != nullptr;   // (debug: the objects pass without its full-screen march, to time the mesh draws alone)
  if (!noMarch) glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // a new airframe or view: bake its mesh (or its hull) with the ray tracer's own shape code (used from the next frame on)
  if (!feedPass) {
    if (meshUse && pm == planeMeshes.end()) { setRT(progHullBake, fp); bakePlaneMesh(fp, slot, meshK); }
    else if (hullUse && !hulls.count(hullK)) { setRT(progHullBake, fp); bakeHull(fp, slot, hullK); }
  }
}

static mat4 orthoMat(float l, float r, float b, float t, float n, float f) {
  mat4 m; m(0, 0) = 2.f / (r - l); m(1, 1) = 2.f / (t - b); m(2, 2) = -2.f / (f - n);
  m(0, 3) = -(r + l) / (r - l); m(1, 3) = -(t + b) / (t - b); m(2, 3) = -(f + n) / (f - n); return m;
}
// The airframe shadow maps: the player's baked static mesh seen from the sun and from the three brightest
// shadow-casting lights, so the proxy below reads a depth instead of marching the field per pixel (the single biggest
// cost of the raster frame on the owner's GPU: 55 ms at night, 36 ms in the cockpit). The moving parts are not in
// the mesh: their hull is drawn into a mask and the proxy marches the field only there.
void Renderer::rasterShadowMaps(const FrameParams& fp) {
  shOn = 0;
  static const bool off = getenv("SHMAPOFF") != nullptr;   // (debug / the analysis: the per-pixel march as before)
  if (off || !progShMap || !planeMeshWanted(fp)) return;
  // the outside mesh, or in the cockpit the cabin mesh (the same airframe with its windows cut: an occluder all the same)
  auto pm = planeMeshes.find(hullKey(fp, 0));
  if (pm == planeMeshes.end() || !pm->second.ok || !pm->second.idx) pm = planeMeshes.find(hullKey(fp, fp.plane.PS[3] > 0.5f ? 1 : 0));
  if (pm == planeMeshes.end() || !pm->second.ok || !pm->second.idx) return;
  const PlaneVisual& pv = fp.plane;
  const float R = std::max(pv.M[0], pv.M[9 * 4] * 2.f) * 0.55f + 1.5f;   // (planeBound in the shaders)
  const vec3 c = pv.pos;
  // which maps: the sun when up; the lights in the proxy's slots (gbShadowSlot: the brightest shadow-casting first)
  int want = 0; int lightOf[4] = {-1, -1, -1, -1};
  if (fp.sunDir.y > -0.05f) want |= 1;
  for (int i = 0; i < fp.plN; i++) {
    const FrameParams::PointLight& L = fp.pl[i];
    if (L.shadow <= 0.f) continue;
    if (L.cosCut <= 0.05f && length(L.pos - c) < R * 1.1f) continue;   // (an omnidirectional lamp on the airframe itself: no view to map, it keeps the march)
    float li = std::max(L.col.x, std::max(L.col.y, L.col.z)); int slot = 0;
    for (int k = 0; k < fp.plN; k++) {
      if (k == i || fp.pl[k].shadow <= 0.f) continue;
      float lk = std::max(fp.pl[k].col.x, std::max(fp.pl[k].col.y, fp.pl[k].col.z));
      if (lk > li || (lk == li && k < i)) slot++;
    }
    if (slot < 3) { want |= 2 << slot; lightOf[1 + slot] = i; }
  }
  if (!want) return;
  if (!texShMap) {
    glGenTextures(1, &texShMap); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMap);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, kShMapRes, kShMapRes, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &texShMov); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMov);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, kShMapRes, kShMapRes, 4, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glGenFramebuffers(1, &fboShMap);
  }
  auto movIt = hulls.find(pm->second.movKey);
  const HullMesh* mov = movIt != hulls.end() && movIt->second.ok && movIt->second.verts ? &movIt->second : nullptr;
  glBindFramebuffer(GL_FRAMEBUFFER, fboShMap);
  glViewport(0, 0, kShMapRes, kShMapRes);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  for (int layer = 0; layer < 4; layer++) {
    if (!(want & (1 << layer))) continue;
    mat4 vp;
    if (layer == 0) {   // the sun: orthographic about the airframe, the ground beyond the far plane compared at depth 1
      vec3 d = normalize(fp.sunDir), up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
      vp = orthoMat(-R, R, -R, R, R, 3.f * R) * lookAt(c + d * (2.f * R), c, up);
    } else {
      const FrameParams::PointLight& L = fp.pl[lightOf[layer]];
      float dc = length(L.pos - c);
      vec3 d, up; float fov;
      if (L.cosCut > 0.05f && dc < R * 1.1f) {   // a lamp on the airframe: perspective along its beam, out to its reach
        d = normalize(L.dir); up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
        fov = std::min(2.f * (acosf(clampf(L.cosCut, -1.f, 1.f)) + 0.12f), 165.f * DEG);
      } else {   // a light on the ground (the apron's, the runway's): looking at the airframe, wide enough for its bound
        d = normalize(c - L.pos); up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
        fov = std::min(2.f * asinf(clampf(R / std::max(dc, R * 1.001f), 0.f, 1.f)) + 0.1f, 165.f * DEG);
      }
      vp = perspective(fov, 1.f, 0.2f, L.radius * 40.f + 400.f) * lookAt(L.pos, L.pos + d, up);
    }
    shMapVP[layer] = vp;
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texShMap, 0, layer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texShMov, 0, layer);
    GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
    glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);   // (the previous layer's hull pass left depth writes off: a clear obeys the masks)
    glClearDepth(1.0); float zero[4] = {0, 0, 0, 0}; glClearBufferfv(GL_COLOR, 0, zero); glClear(GL_DEPTH_BUFFER_BIT);
    // the static airframe: depth only
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glUseProgram(progShMap);
    glUniformMatrix4fv(U(progShMap, "uVP"), 1, GL_FALSE, vp.m);
    glUniformMatrix3fv(U(progShMap, "uRot"), 1, GL_FALSE, pv.rot);
    glUniform3f(U(progShMap, "uPos"), c.x, c.y, c.z);
    glBindVertexArray(pm->second.vao);
    glDrawElements(GL_TRIANGLES, pm->second.idx, GL_UNSIGNED_INT, nullptr);
    // the moving hull: a mask, no depth
    if (mov) {
      glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glUseProgram(progShMov);
      glUniformMatrix4fv(U(progShMov, "uVP"), 1, GL_FALSE, vp.m);
      glUniformMatrix3fv(U(progShMov, "uRot"), 1, GL_FALSE, pv.rot);
      glUniform3f(U(progShMov, "uPos"), c.x, c.y, c.z);
      if (!vaoHull) glGenVertexArrays(1, &vaoHull);
      glBindVertexArray(vaoHull); glBindBuffer(GL_ARRAY_BUFFER, mov->vbo);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
      glDrawArrays(GL_TRIANGLES, 0, mov->verts);
    }
    shOn |= 1 << layer;
  }
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The airframes' shadows on the G-buffer's surfaces (the sun on the ground, the landing lights' beams): texGB[4]
void Renderer::rasterShadowProxy(const FrameParams& fp) {
  glBindFramebuffer(GL_FRAMEBUFFER, fboShProxy);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  setRT(progShProxy, fp);
  glUniform1i(U(progShProxy, "uAfShOn"), shOn);
  if (shOn) {
    glUniformMatrix4fv(U(progShProxy, "uAfShVP"), 4, GL_FALSE, shMapVP[0].m);
    glActiveTexture(GL_TEXTURE0 + 26); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMap); glUniform1i(U(progShProxy, "uAfShMap"), 26);
    glActiveTexture(GL_TEXTURE0 + 27); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMov); glUniform1i(U(progShProxy, "uAfShMov"), 27);
  }
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  static const char* dump = getenv("PROXYDUMP");   // (debug: the proxy as an image - R sun, G/B/A the first three shadowed lights)
  if (dump) {
    std::vector<unsigned char> px((size_t)rw * rh * 4);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    if (FILE* f = fopen(dump, "wb")) {
      fprintf(f, "P6\n%d %d\n255\n", rw, rh);
      for (int y = rh - 1; y >= 0; y--) for (int x = 0; x < rw; x++) { const unsigned char* p = &px[((size_t)y * rw + x) * 4]; unsigned char c[3] = {p[1], p[2], p[3]}; fwrite(c, 1, 3, f); }
      fclose(f);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The effects over the lit, clouded frame (kEffectsFS): written into the TAA history texture the resolve is about
// to overwrite anyway (the frame can't be read and written at once), then copied back
void Renderer::rasterEffects(const FrameParams& fp) {
  if (rw > histW || rh > histH) return;   // (a camera feed larger than the main view: no scratch for it)
  static const bool off = getenv("RASTERNOFX") != nullptr; if (off) return;   // (debug)
  const int cur = histIdx ^ 1;
  glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[cur]);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  setRT(progEffects, fp);
  glActiveTexture(GL_TEXTURE0 + 24); glBindTexture(GL_TEXTURE_2D, texRaw); glUniform1i(U(progEffects, "uRawTex"), 24);
  glActiveTexture(GL_TEXTURE0 + 25); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progEffects, "uSceneDepth"), 25);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboTAA[cur]); glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboComp);
  glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::rasterLight(const FrameParams& fp) {
  glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
  GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
  glDrawBuffers(3, bufs);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  setRT(progLight, fp);
  glActiveTexture(GL_TEXTURE0 + 22); glBindTexture(GL_TEXTURE_2D, texGB[3]); glUniform1i(U(progLight, "uGB3"), 22);
  glActiveTexture(GL_TEXTURE0 + 23); glBindTexture(GL_TEXTURE_2D, texGB[4]); glUniform1i(U(progLight, "uShProxy"), 23);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  depthValid = true;
}
