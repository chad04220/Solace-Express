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
  return compileTerrainMesh();
}

// The lit frame for a view from the G-buffer the raster passes filled: into texRaw (colour + TAA class), texDepth (view
// distance) and texCloudMask, exactly what the ray tracer writes, so the clouds, the TAA and everything after run as before.
void Renderer::rasterWorld(const FrameParams& fp) {
  // scenery: its shadow cascades and the G-buffer, which it clears (entity_render.cpp)
  envOn = false;   // (the terrain envelope is the ray tracer's)
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
  const bool hullUse = hullWanted(fp) && fp.pano <= 0.f;   // (the hulls are flat rasters: a panorama camera marches without them)
  const int hullSlot = fp.plane.PS[3] > 0.5f ? 1 : 0;
  const uint64_t hullK = hullUse ? hullKey(fp, hullSlot) : 0;
  if (hullUse && hulls.count(hullK)) drawHull(fp, hullSlot, hullK);
  if (fp.pano <= 0.f) drawTrafficHulls(fp); else trafHullOn = false;
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, gb);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  setRT(progObjects, fp);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  glUniform1f(U(progObjects, "uLogC"), 2.f / log2f(40000.f + 1.f));
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // a new airframe or view: bake its hull with the ray tracer's own shape code (used from the next frame on)
  if (hullUse && !feedPass && !hulls.count(hullK)) { setRT(progHullBake, fp); bakeHull(fp, hullSlot, hullK); }
}

// The airframes' shadows on the G-buffer's surfaces (the sun on the ground, the landing lights' beams): texGB[4]
void Renderer::rasterShadowProxy(const FrameParams& fp) {
  glBindFramebuffer(GL_FRAMEBUFFER, fboShProxy);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  setRT(progShProxy, fp);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The effects over the lit, clouded frame (kEffectsFS): written into the TAA history texture the resolve is about
// to overwrite anyway (the frame can't be read and written at once), then copied back
void Renderer::rasterEffects(const FrameParams& fp) {
  if (rw > histW || rh > histH) return;   // (a camera feed larger than the main view: no scratch for it)
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
