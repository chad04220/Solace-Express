// Solace Express - camera feeds: the research jets' cockpit cameras drawn into the feed atlas (see feed_cameras.h)
#include "renderer.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

FeedMounts g_feedMounts[3];

// Exchange the render targets (and the per-view state that goes with them) with v: every pass then draws for v's view.
void Renderer::swapView(ViewTargets& v) {
  std::swap(W, v.W); std::swap(H, v.H); std::swap(rw, v.rw); std::swap(rh, v.rh); std::swap(cw, v.cw); std::swap(ch, v.ch);
  std::swap(texRaw, v.texRaw); std::swap(texDepth, v.texDepth); std::swap(texCloudMask, v.texCloudMask);
  std::swap(texCloud, v.texCloud); std::swap(texCloudD, v.texCloudD); std::swap(fboCloud, v.fboCloud); std::swap(fboComp, v.fboComp);
  std::swap(fboScene, v.fboScene);
  for (int i = 0; i < 3; i++) std::swap(texGB[i], v.texGB[i]);
  std::swap(texGBDepth, v.texGBDepth); std::swap(fboGB, v.fboGB);
  std::swap(texEnv, v.texEnv); std::swap(texEnvDepth, v.texEnvDepth); std::swap(fboEnv, v.fboEnv);
  std::swap(depthValid, v.depthValid); std::swap(envOn, v.envOn); std::swap(hullOn, v.hullOn); std::swap(trafHullOn, v.trafHullOn);
  std::swap(ckMaskPrev, v.ckMaskPrev); std::swap(jitX, v.jitX); std::swap(jitY, v.jitY);
}

// The pictures are only needed while the pilot can see the displays: the cockpit view of a research jet.
bool Renderer::feedsWanted(const FrameParams& fp) const {
  return fp.feedRig > 0 && fp.plane.on && fp.plane.PS[3] > 0.5f && fp.wreck.pieces == 0 && progRT;
}

// Where each camera's lens sits: from the eye out along its direction, the first point outside the airframe's skin
// (the outside shape, evaluated by the hull bake's program; the caller has its uniforms set for this aircraft).
void Renderer::measureFeedMounts(const FrameParams& fp) {
  const int rig = fp.feedRig;
  FeedMount mt[kMaxFeeds];
  const int n = feedRig(rig, 1.f, mt);
  const float* M = fp.plane.M;
  const vec3 E(M[22 * 4], M[22 * 4 + 1], M[22 * 4 + 2]);
  const int S = 240; const float step = 0.025f;   // out to 6 m
  std::vector<vec3> pts;
  for (int i = 0; i < n; i++) for (int s = 0; s < S; s++) pts.push_back(E + mt[i].dir * (s * step));
  // the nose: along the centreline, ahead of the eye, from 1.2 m below it to 0.4 m above
  const int NY = 17, NZ = 320; const float nstep = 0.04f;   // out to 12.8 m ahead
  const size_t n0 = pts.size();
  for (int j = 0; j < NY; j++) for (int s = 0; s < NZ; s++) pts.push_back(E + vec3(0.f, -1.2f + j * 0.1f, -s * nstep));
  float ps[4] = {fp.plane.PS[0], fp.plane.PS[1], fp.plane.PS[2], 0.f}, ctl[4] = {0, 0, 0, 0};   // the outside shape, controls centred
  glUniform1i(glGetUniformLocation(progHullBake, "uHStN"), 1);
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStPS"), 1, ps);
  glUniform4fv(glGetUniformLocation(progHullBake, "uHStCtl"), 1, ctl);
  std::vector<float> d;
  hullEval(pts, d);
  FeedMounts& fm = g_feedMounts[rig];
  for (int i = 0; i < n; i++) {
    int last = -1;
    for (int s = 0; s < S; s++) if (d[(size_t)i * S + s] < 0.f) last = s;
    fm.skin[i] = last < 0 ? 0.f : (last + 1) * step;
  }
  fm.nose = vec3(0.f, 0.f, -fm.skin[0]);
  float best = 0.f;
  for (int j = 0; j < NY; j++) for (int s = 0; s < NZ; s++)
    if (d[n0 + (size_t)j * NZ + s] < 0.f && s * nstep > best) { best = s * nstep; fm.nose = vec3(0.f, -1.2f + j * 0.1f, -(s + 1) * nstep); }
  fm.ok = true;
  static const bool dbg = getenv("FEEDDBG") != nullptr;
  if (dbg) { printf("feed mounts (rig %d):", rig); for (int i = 0; i < n; i++) printf(" %.2f", fm.skin[i]); printf("  nose %.2f %.2f\n", fm.nose.y, fm.nose.z); }
}

// Draw the cameras due this frame into their atlas tiles: the ones without a picture yet and the bomb camera first,
// then the others in turn, a few per frame (each picture a few frames old at most, like a real feed's latency).
void Renderer::renderFeeds(const FrameParams& fp, const std::function<void(GLuint, const FrameParams&)>& setRT,
                           const std::function<void(const FrameParams&, GLuint)>& trace) {
  if (!feedsWanted(fp)) {
    if (feedRigNow) { for (bool& v : feedValid) v = false; feedRigNow = 0; }
    return;
  }
  if (!g_feedMounts[fp.feedRig].ok) {   // first sight of this craft: find the mounts (its cameras go up next frame)
    if (progHullBake) { setRT(progHullBake, fp); measureFeedMounts(fp); }
    else g_feedMounts[fp.feedRig].ok = true;
    return;
  }
  if (fp.feedRig != feedRigNow) { for (bool& v : feedValid) v = false; feedRigNow = fp.feedRig; }
  if (!texFeed) {
    glGenTextures(1, &texFeed); glBindTexture(GL_TEXTURE_2D, texFeed);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kFeedAtlasW, kFeedAtlasH, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fboFeed); glBindFramebuffer(GL_FRAMEBUFFER, fboFeed);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texFeed, 0);
    GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
  }
  if (!feedView.fboScene) {   // the feeds' own render targets, made by the same code as the main view's
    swapView(feedView);
    W = kFeedMaxW; H = kFeedMaxH;
    float rs = renderScale; renderScale = 1.f;
    createRenderTargets();
    renderScale = rs;
    swapView(feedView);
  }
  // atlas layout: the slots in order, packed in rows (a tile changes size: its picture is redrawn)
  {
    int x = 0, y = 0, rowH = 0;
    for (int k = 0; k < kMaxFeeds; k++) {
      const FeedCamera& c = fp.feeds[k];
      int w = c.on ? std::min(c.w, (int)kFeedMaxW) : 0, h = c.on ? std::min(c.h, (int)kFeedMaxH) : 0;
      if (!c.on || w < 8 || h < 8) { feedValid[k] = false; feedTileWH[k][0] = feedTileWH[k][1] = 0; continue; }
      if (x + w + 2 > kFeedAtlasW) { x = 0; y += rowH + 2; rowH = 0; }
      if (y + h > kFeedAtlasH) { feedValid[k] = false; continue; }
      if (feedTileWH[k][0] != w || feedTileWH[k][1] != h) feedValid[k] = false;
      feedTileWH[k][0] = w; feedTileWH[k][1] = h;
      feedTile[k][0] = (float)x / kFeedAtlasW; feedTile[k][1] = (float)y / kFeedAtlasH;
      feedTile[k][2] = (float)w / kFeedAtlasW; feedTile[k][3] = (float)h / kFeedAtlasH;
      x += w + 2; rowH = std::max(rowH, h);
    }
  }
  // which cameras this frame
  static const int perFrame = getenv("FEEDS") ? std::max(1, atoi(getenv("FEEDS"))) : 3;
  std::vector<int> todo;
  auto want = [&](int k) { if ((int)todo.size() < perFrame && fp.feeds[k].on && feedTileWH[k][0] > 0 && std::find(todo.begin(), todo.end(), k) == todo.end()) todo.push_back(k); };
  if (fp.feeds[kFeedBombSlot].on) want(kFeedBombSlot);   // it flies fast: every frame
  for (int k = 0; k < kMaxFeeds; k++) if (!feedValid[k]) want(k);
  for (int i = 0; i < kMaxFeeds && (int)todo.size() < perFrame; i++) {
    int k = (feedNext + i) % kMaxFeeds;
    if (k == kFeedBombSlot) continue;
    size_t before = todo.size(); want(k);
    if (todo.size() > before) feedNext = (k + 1) % kMaxFeeds;
  }
  if (todo.empty()) return;

  swapView(feedView);
  feedPass = true;
  for (int k : todo) {
    const FeedCamera& c = fp.feeds[k];
    FrameParams cf = fp;   // the scene as this camera sees it: from outside the aircraft
    cf.camPos = c.pos; cf.camRight = c.right; cf.camUp = c.up; cf.camBack = c.back;
    cf.fovY = 2.f * atanf(c.tanY);
    cf.plane.PS[3] = 0.f; cf.dispMode = 0; cf.feedRig = 0;
    W = rw = feedTileWH[k][0]; H = rh = feedTileWH[k][1];
    cw = (rw + 1) / 2; ch = (rh + 1) / 2;
    jitX = jitY = 0.f;
    drawEnvelope(cf);
    drawEntities(cf);
    hullOn = false;
    if (hullWanted(cf)) { uint64_t hk = hullKey(cf, 0); if (hulls.count(hk)) drawHull(cf, 0, hk); }
    drawTrafficHulls(cf);
    trace(cf, progRT);
    // into its tile
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboScene); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboFeed);
    int x0 = (int)(feedTile[k][0] * kFeedAtlasW + 0.5f), y0 = (int)(feedTile[k][1] * kFeedAtlasH + 0.5f);
    glBlitFramebuffer(0, 0, rw, rh, x0, y0, x0 + rw, y0 + rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    feedValid[k] = true;
  }
  feedPass = false;
  swapView(feedView);
  static const char* dump = getenv("FEEDDUMP");   // (debug: write the atlas as it stands after this frame's feeds)
  if (dump) {
    std::vector<float> px((size_t)kFeedAtlasW * kFeedAtlasH * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fboFeed); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, kFeedAtlasW, kFeedAtlasH, GL_RGBA, GL_FLOAT, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::vector<uint8_t> rgb((size_t)kFeedAtlasW * kFeedAtlasH * 3);
    for (size_t i = 0; i < (size_t)kFeedAtlasW * kFeedAtlasH; i++)
      for (int c = 0; c < 3; c++) { float v = px[i * 4 + c]; v = v / (1.f + v); rgb[i * 3 + c] = (uint8_t)(255.f * powf(std::max(v, 0.f), 1.f / 2.2f)); }
    writePNG(dump, kFeedAtlasW, kFeedAtlasH, rgb);
  }
}
