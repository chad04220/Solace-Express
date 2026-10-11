// Solace Express - the renderer: a deferred pipeline that draws every surface with the
// rasterizer and lights the frame once. Its parts: the scenery pass and its shadow cascades, the
// terrain-shadow bake, the display atlases, the cloud pass, and everything after the lit frame (TAA, sprites, bloom,
// light shafts, post). See docs/RENDERER_REBUILD.md.
#include "renderer.h"
#include "shaders.h"

bool Renderer::compileRaster(const std::function<void()>& step) {
  std::string e;   // (each program's own log: linkProgramCached appends, so it is cleared before every build)
  setCompileStage("lighting");
  e.clear(); progLight = linkProgramCached(kFullscreenVS, lightFSAssembly(""), e);
  if (!progLight) { error = "Lighting shader: " + e; return false; }
  if (step) step();
  setCompileStage("traffic propellers");
  e.clear(); progTrafficProps = linkProgramCached(kPropDiscVS, propDiscFSAssembly(), e);
  if (!progTrafficProps) { error = "Traffic prop shader: " + e; return false; }
  if (step) step();
  // the airframes' full-screen passes at launch: only their builds with no airframe in them - the UFO and the debris'
  // march, and the shadows from the maps alone. Every build with an aircraft's code is made the first time a frame
  // needs it (afPassProgram), so an edit to one aircraft compiles nothing at launch
  setCompileStage("UFO and debris");
  e.clear(); progObjectsNoAf = linkProgramCached(kFullscreenVS, objectsFSAssembly("#define AF_LIGHT\n#define OBJ_NO_AF\n"), e);
  if (!progObjectsNoAf) { error = "Objects (UFO, debris) shader: " + e; return false; }
  if (step) step();
  setCompileStage("aircraft shadows (maps only)");
  e.clear(); progShProxyMaps = linkProgramCached(kFullscreenVS, shadowProxyFSAssembly("#define AF_LIGHT\n#define PROXY_MAPS_ONLY\n"), e);
  if (!progShProxyMaps) { error = "Shadow proxy (maps) shader: " + (proxyError.empty() ? e : proxyError + "\n" + e); return false; }
  if (step) step();
  // the airframe shadow maps: the baked mesh (and the moving hull) from a light, plain depth
  static const char* kShMapVS = "#version 330 core\nlayout(location = 0) in vec3 aPos; uniform mat4 uVP; uniform mat3 uRot; uniform vec3 uPos;\n"
    "uniform sampler2D uPartPose; uniform int uPartInst;\n"   // (a cockpit's rigid part at its pose: plane_mesh_vs.glsl)
    "void main(){ vec3 p = aPos; if (uPartInst >= 0) { int b = (uPartInst + gl_InstanceID)*4; p = mat3(texelFetch(uPartPose, ivec2(b, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 1, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 2, 0), 0).xyz)*aPos + texelFetch(uPartPose, ivec2(b + 3, 0), 0).xyz; }\n"
    "  gl_Position = uVP*vec4(uRot*p + uPos, 1.0); }\n";
  setCompileStage("aircraft shadow maps");
  e.clear(); progShMap = linkProgramCached(kShMapVS, "#version 330 core\nvoid main(){}\n", e);
  if (!progShMap) { error = "Shadow map shader: " + e; return false; }
  if (step) step();
  // (a wreck piece: cut to its share of the airframe, wreck_clip.glsl)
  static const char* kShMapWreckVS = "layout(location = 0) in vec3 aPos; uniform mat4 uVP; uniform mat3 uRot; uniform vec3 uPos;\n"
    "uniform sampler2D uPartPose; uniform int uPartInst; out vec3 vB; flat out int vPc;\n"
    "void main(){ vec3 p = aPos; mat3 rot = uRot; vec3 at = uPos; vPc = -1;\n"
    "  if (uPartInst >= 0) { int b = (uPartInst + gl_InstanceID)*4; mat3 R = mat3(texelFetch(uPartPose, ivec2(b, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 1, 0), 0).xyz, texelFetch(uPartPose, ivec2(b + 2, 0), 0).xyz);\n"
    "    vec3 T = texelFetch(uPartPose, ivec2(b + 3, 0), 0).xyz; p = R*aPos + T; if (uWreckParts == 1) { vPc = brkRigidOwner(R*uPartC + T); rot = uPcRot[vPc]; at = uPcPos[vPc]; } }\n"
    "  vB = p; gl_Position = uVP*vec4(rot*p + at, 1.0); }\n";
  e.clear(); progShMapWreck = linkProgramCached(std::string("#version 330 core\nuniform int uWreck;\n") + kWreckClip + kShMapWreckVS,
                                                std::string("#version 330 core\nin vec3 vB; flat in int vPc; uniform int uWreck;\n") + kWreckClip + "void main(){ if (vPc < 0 && brkOwner(vB) != uPcK) discard; }\n", e);
  if (!progShMapWreck) { fprintf(stderr, "Shadow map (wreck) shader: %s\n", e.c_str()); }   // (not needed to fly: a wreck then casts no shadow)
  if (step) step();
  e.clear(); progShMov = linkProgramCached(kShMapVS, "#version 330 core\nout float oM; void main(){ oM = 1.0; }\n", e);
  if (!progShMov) { error = "Shadow map (moving hull) shader: " + e; return false; }
  if (step) step();
  setCompileStage("aircraft meshes");
  if (!compilePlaneMesh(step)) return false;
  setCompileStage("terrain and water");
  return compileTerrainMesh(step);
}

// ---- the full-screen airframe passes' programs: the aircraft a pass covers this frame - all of one type: that type's
// own build (its code alone); several types, or one without an own build: the shared build, the light aircraft's when
// no research jet is among them. Each made the first time a frame needs it (from the binary cache after the first
// launch); one that fails falls back to the shared, and the shared to the pass's own fallback (the caller's)
GLuint Renderer::linkAfPass(int pass, const std::string& d, const std::string& who) {
  // (the shadow proxy and the effects each have builds with less in them, for a driver whose compiler fails on the
  // whole even with its own options (linkProgramCached): NVIDIA's did on v3.35.0's and v3.36.0's. The first that builds
  // is used and startup.log says which. What each leaves out: the traffic's sun shadows marched (those without a map),
  // the airframe's in the landing lights; the XR-40 cloak's view through its cloaked part, the research jets' flames)
  static const char* const kProxyLess[] = {"", "#define PROXY_NO_TRAFFIC\n", "#define PROXY_NO_LIGHTS\n", "#define PROXY_NO_TRAFFIC\n#define PROXY_NO_LIGHTS\n"};
  static const char* const kProxyLessName[] = {"", "the traffic's marched shadows", "the airframe's shadows in the lights", "the traffic's marched shadows and the airframe's in the lights"};
  static const char* const kFxLess[] = {"", "#define FX_NO_CLOAK\n", "#define FX_NO_PLUMES\n", "#define FX_NO_CLOAK\n#define FX_NO_PLUMES\n"};
  static const char* const kFxLessName[] = {"", "the cloak's view", "the jets' flames", "the cloak's view and the jets' flames"};
  static const char* const kName[] = {"Aircraft", "Aircraft shadows", "Effects"};
  const std::string name = std::string(kName[pass]) + " (" + who + ")";
  setCompileStage(name.c_str());
  std::string e, first; GLuint p = 0;
  for (int k = 0; k < (pass == kAfObjects ? 1 : 4) && !p; k++) {
    const std::string dk = d + (pass == kAfProxy ? kProxyLess[k] : pass == kAfEffects ? kFxLess[k] : "");
    e.clear();
    p = linkProgramCached(kFullscreenVS, pass == kAfObjects ? objectsFSAssembly(dk) : pass == kAfProxy ? shadowProxyFSAssembly(dk) : effectsFSAssembly(dk), e);
    if (k == 0) first = e;
    if (p && k > 0) shaderNote(name + ": built without " + (pass == kAfProxy ? kProxyLessName[k] : kFxLessName[k]));
  }
  setCompileStage("");
  if (!p) {
    shaderNote(name + " failed:\n" + first);
    if (pass == kAfProxy && proxyError.empty()) proxyError = "Shadow proxy shader: " + first;
  }
  return p;
}
GLuint Renderer::afPassProgram(int pass, int model, bool research) {
  if (model >= 0) {
    AfOwn& a = afOwn[model];
    if (!a.passTried[pass]) {
      a.passTried[pass] = true;
      float M[96]; packModelOf(model, M);
      a.pass[pass] = linkAfPass(pass, aircraftDefines(model, M), std::string("the ") + kAircraft[model].name + "'s own");
    }
    if (a.pass[pass]) return a.pass[pass];
  }
  static const bool all = getenv("AF_ALL") != nullptr;   // (debug: every aircraft build always)
  const int v = research || all ? 0 : 1;
  if (!afSharedTried[pass][v]) { afSharedTried[pass][v] = true; afShared[pass][v] = linkAfPass(pass, v ? "#define AF_LIGHT\n" : "", v ? "light aircraft build" : "every aircraft"); }
  return afShared[pass][v];
}
// one more aircraft a pass covers: model collects its type (-2 none yet, -1 several, or one without an own build) and
// research whether a research jet is among them
void Renderer::afCover(int& model, bool& research, const float* M, int claimed) {
  const int m = afModelOf(M, claimed);
  research = research || M[2] > 4.5f;
  model = model == -2 ? m : model == m ? m : -1;
}

// The rigid parts' poses for this view: the player's aircraft's and the traffic's (computed again by the objects pass:
// a camera feed in between computes its own)
void Renderer::updatePartPoses(const FrameParams& fp) {
  const PlaneMesh* player = nullptr; const PlaneMesh* traf[kMaxTrafficDrawn] = {};
  if (planeMeshWanted(fp) && fp.pano <= 0.f) {
    auto pm = planeMeshes.find(hullKey(fp, fp.plane.PS[3] > 0.5f ? 1 : 0));
    if (pm != planeMeshes.end() && pm->second.ok) player = &pm->second;
  } else if (const PlaneMesh* wm = wreckMesh(fp)) player = wm;   // (a broken-up aircraft: its pieces' parts)
  if (!meshOff && fp.pano <= 0.f)
    for (int k = 0; k < std::min(fp.trafficN, kMaxTrafficDrawn); k++) { auto it = planeMeshes.find(trafficModelKey(fp.traffic[k].t)); if (it != planeMeshes.end() && it->second.ok && meshProgramFor(fp.traffic[k].t, -1)) traf[k] = &it->second; }
  computePartPoses(fp, player, traf);
}

// The lit frame for a view from the G-buffer the raster passes filled: into texRaw (colour + TAA class), texDepth (view
// distance) and texCloudMask, so the clouds, the TAA and everything after run as before.
void Renderer::rasterWorld(const FrameParams& fp) {
  // scenery: its shadow cascades and the G-buffer, which it clears (entity_render.cpp)
  // the player's baked mesh opens the G-buffer's depth (drawEntities, right after the clear): see rasterObjects
  earlyMesh = nullptr;
  updatePartPoses(fp);   // (the rigid parts' poses for this view)
  static const bool noEarly = getenv("NOEARLY") != nullptr;   // (debug A/B: the mesh's depth only in the objects pass, as before)
  if (!noEarly && planeMeshWanted(fp) && fp.pano <= 0.f) {
    auto pm = planeMeshes.find(hullKey(fp, fp.plane.PS[3] > 0.5f ? 1 : 0));
    if (pm != planeMeshes.end() && pm->second.ok && pm->second.idx) earlyMesh = &pm->second;
  }
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
// player's aircraft starts its march on its rasterized hull.
void Renderer::rasterObjects(const FrameParams& fp) {
  hullOn = false;
  updatePartPoses(fp);
  const int slot = fp.plane.PS[3] > 0.5f ? 1 : 0;
  // the player's aircraft as a mesh where it never moves (aircraft_mesh.cpp): then only its moving parts are marched,
  // from the hull round them; without one, the whole airframe is marched from its full hull
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
  if (!meshOff) for (int k = 0; k < trafN; k++) { auto it = planeMeshes.find(trafficModelKey(fp.traffic[k].t)); if (it != planeMeshes.end() && it->second.ok && meshProgramFor(fp.traffic[k].t, -1)) trafMesh[k] = &it->second; }
  if (fp.pano <= 0.f) drawTrafficHulls(fp, trafMesh); else trafHullOn = false;
  glBindFramebuffer(GL_FRAMEBUFFER, fboGB);
  GLenum gb[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, gb);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  if (meshOn) drawPlaneMesh(fp, pm->second, fp.plane.rot, fp.plane.pos, -1, earlyMesh == &pm->second);   // (its depth is in since the frame began)
  earlyMesh = nullptr;
  // a broken-up aircraft: its pieces from its outside mesh (baked below if it has none yet)
  const PlaneMesh* wm = wreckMesh(fp);
  if (wm) drawWreck(fp, *wm);
  for (int k = 0; k < trafN; k++) {
    if (!trafMesh[k]) continue;
    const float* t = fp.traffic[k].t;
    float rot[9] = {t[25 * 4], t[25 * 4 + 1], t[25 * 4 + 2], t[26 * 4], t[26 * 4 + 1], t[26 * 4 + 2], t[27 * 4], t[27 * 4 + 1], t[27 * 4 + 2]};
    drawPlaneMesh(fp, *trafMesh[k], rot, vec3(t[24 * 4], t[24 * 4 + 1], t[24 * 4 + 2]), k);
  }
  // what the march has to do this frame: a traffic aircraft drawn as a mesh whose moving hull is empty (every moving
  // piece a rigid part) never needs it; the player's aircraft only without its mesh, with its moving hull drawn, or
  // broken up; and with nothing at all (the usual flight in a light aircraft) the full-screen pass is skipped
  int trafMarch = 0;
  for (int k = 0; k < trafN; k++) {
    if (trafMesh[k]) { auto it = hulls.find(trafMesh[k]->movKey); if (it != hulls.end() && it->second.ok && !it->second.verts) continue; }
    trafMarch |= 1 << k;
  }
  const bool afMarch = (fp.plane.on && fp.wreck.pieces == 0 && (!meshOn || hullOn)) || trafMarch != 0;
  const bool marchAny = afMarch || fp.ufoOn || fp.wreck.debris > 0;
  if (marchAny) {
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
  // (only the UFO or the debris to march: the build without any airframe in it - on the owner's GPU the airframes'
  // code alone made the UFO's march more than twice as slow)
  static const bool objFull = getenv("OBJFULL") != nullptr;   // (debug: always the full build)
  GLuint prog = progObjectsNoAf;
  if (afMarch || objFull) {   // (the aircraft it marches: the player's, and the traffic with something to march)
    int model = -2; bool research = false;
    if (fp.plane.on && fp.wreck.pieces == 0) afCover(model, research, fp.plane.M, fp.plane.model);
    for (int k = 0; k < trafN; k++) if (trafMarch & (1 << k)) afCover(model, research, fp.traffic[k].t, -1);
    prog = afPassProgram(kAfObjects, model, research);
  }
  if (!prog) prog = progObjectsNoAf;   // (no build with the airframes: the UFO and the debris still)
  static const bool objDbg = getenv("OBJDBG") != nullptr;   // (debug: why the march runs)
  if (objDbg) { int nm = 0; for (int k = 0; k < trafN; k++) nm += (trafMarch >> k) & 1; printf("objects: march %d (plane on %d mesh %d hull %d wreck %d; traffic %d of %d; ufo %d debris %d)\n", (int)marchAny, (int)fp.plane.on, (int)meshOn, (int)hullOn, fp.wreck.pieces, nm, trafN, (int)fp.ufoOn, fp.wreck.debris); }
  setRT(prog, fp);
  glUniform1i(U(prog, "uTrafMarch"), trafMarch);
  for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, 0); }   // (the G-buffer is the target here, never read)
  glUniform1f(U(prog, "uLogC"), logDepthC());
  glUniform1i(U(prog, "uMeshOn"), meshOn ? 1 : 0);
  glActiveTexture(GL_TEXTURE0 + 28); glBindTexture(GL_TEXTURE_2D, sceneZ ? texDepthCopy : 0); glUniform1i(U(prog, "uSceneZ"), 28); glUniform1i(U(prog, "uSceneZOn"), sceneZ ? 1 : 0);
  glBindVertexArray(vaoEmpty);
  static const bool noMarch = getenv("NOMARCH") != nullptr;   // (debug: the objects pass without its full-screen march, to time the mesh draws alone)
  if (!noMarch && marchAny && !(dbgOff & kProbeMarch)) glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // a new airframe or view: bake its mesh (or its hull) with the shaders' own shape code (used from the next frame on)
  if (!feedPass) {
    if (meshUse && pm == planeMeshes.end()) bakePlaneMesh(fp, slot, meshK);   // (on the aircraft's own builder: beginHullBake)
    else if (hullUse && !hulls.count(hullK)) bakeHull(fp, slot, hullK);
    else if (!wm && fp.plane.on && fp.wreck.pieces > 0 && fp.pano <= 0.f && meshProgramFor(fp.plane.M, fp.plane.model)) {
      const uint64_t k0 = hullKey(fp, 0);
      if (!planeMeshes.count(k0)) bakePlaneMesh(fp, 0, k0);
      if (getenv("WRECKDBG")) { auto it = planeMeshes.find(k0); printf("wreck: baked its mesh (%s, %d indices)\n", it != planeMeshes.end() && it->second.ok ? "ok" : "failed", it != planeMeshes.end() ? it->second.idx : -1); }
    }
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
void Renderer::ensureShadowMaps() {
  if (texShMap) return;
  glGenTextures(1, &texShMap); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMap);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, kShMapRes, kShMapRes, kShLayers, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &texShMov); glBindTexture(GL_TEXTURE_2D_ARRAY, texShMov);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, kShMapRes, kShMapRes, 4, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);   // (the player's layers only)
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glGenFramebuffers(1, &fboShMap);
}
// The traffic's sun shadows: each traffic aircraft drawn as a mesh with nothing left to march (its moving hull empty)
// into a layer of its own (4 + k), orthographic from the sun about it, so the shadow proxy looks its shadow on the
// ground up instead of marching its field per pixel (the proxy's largest cost at a busy airport)
void Renderer::rasterTrafficShadowMaps(const FrameParams& fp) {
  trafShOn = 0;
  static const bool off = getenv("SHMAPOFF") != nullptr;
  const int n = std::min(fp.trafficN, kMaxTrafficDrawn);
  if (off || !progShMap || n == 0 || fp.sunDir.y <= -0.05f || meshOff || fp.pano > 0.f) return;
  vec3 d = normalize(fp.sunDir), up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
  bool bound = false;
  for (int k = 0; k < n; k++) {
    const float* t = fp.traffic[k].t;
    const vec3 c(t[24 * 4], t[24 * 4 + 1], t[24 * 4 + 2]); const float R = t[24 * 4 + 3];
    if (length(c - fp.camPos) > 6000.f + R) continue;   // (the proxy shades the ground out to 3 km; one 3 km up, low sun, casts that far)
    auto pm = planeMeshes.find(trafficModelKey(t));
    if (pm == planeMeshes.end() || !pm->second.ok || !pm->second.idx) continue;
    auto mv = hulls.find(pm->second.movKey);
    if (mv == hulls.end() || !mv->second.ok || mv->second.verts) continue;   // (moving parts the mesh lacks: the march stays)
    if (!bound) {
      ensureShadowMaps();
      glBindFramebuffer(GL_FRAMEBUFFER, fboShMap);
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
      { GLenum none = GL_NONE; glDrawBuffers(1, &none); }
      glViewport(0, 0, kShMapRes, kShMapRes);
      glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
      glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glUseProgram(progShMap);
      glUniform1i(U(progShMap, "uPartInst"), -1);
      bound = true;
    }
    const mat4 vp = orthoMat(-R, R, -R, R, R, 3.f * R) * lookAt(c + d * (2.f * R), c, up);
    trafShVP[k] = vp;
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texShMap, 0, 4 + k);
    glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    float rot[9] = {t[25 * 4], t[25 * 4 + 1], t[25 * 4 + 2], t[26 * 4], t[26 * 4 + 1], t[26 * 4 + 2], t[27 * 4], t[27 * 4 + 1], t[27 * 4 + 2]};
    glUseProgram(progShMap);
    glUniformMatrix4fv(U(progShMap, "uVP"), 1, GL_FALSE, vp.m);
    glUniformMatrix3fv(U(progShMap, "uRot"), 1, GL_FALSE, rot);
    glUniform3f(U(progShMap, "uPos"), c.x, c.y, c.z);
    glUniform1i(U(progShMap, "uPartInst"), -1);
    glBindVertexArray(pm->second.vao);
    glDrawElements(GL_TRIANGLES, pm->second.idx, GL_UNSIGNED_INT, nullptr);
    drawPlaneParts(pm->second, progShMap, k);   // (its control surfaces and gear at their pose)
    trafShOn |= 1 << k;
  }
  if (!bound) return;
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void Renderer::rasterShadowMaps(const FrameParams& fp) {
  shOn = 0; shMovOn = false; shCabOn = false;
  static const bool off = getenv("SHMAPOFF") != nullptr;   // (debug / the analysis: the per-pixel march as before)
  // (a broken-up aircraft: its pieces near the one the camera follows, each from the airframe's mesh - wreck_clip.glsl)
  const PlaneMesh* wm = off || !progShMapWreck ? nullptr : wreckMesh(fp);
  if (off || !progShMap || (!planeMeshWanted(fp) && !wm)) return;
  // (in the cockpit the cabin mesh first: its controls, seats and panel shade the cabin, and its windows let the sun in)
  const int slot = fp.plane.PS[3] > 0.5f && !wm ? 1 : 0;
  auto pm = planeMeshes.find(hullKey(fp, slot));
  if (pm == planeMeshes.end() || !pm->second.ok || !pm->second.idx) pm = planeMeshes.find(hullKey(fp, 0));
  if (pm == planeMeshes.end() || !pm->second.ok || !pm->second.idx) return;
  const PlaneVisual& pv = fp.plane;
  float R = std::max(pv.M[0], pv.M[9 * 4] * 2.f) * 0.55f + 1.5f;   // (planeBound in the shaders)
  vec3 c = pv.pos;
  uint32_t wreckIn = 0;   // (the pieces in the maps: within 80 m of the followed one, which is all a map that size can hold sharply)
  if (wm) {
    splitWreck(fp, *wm);
    vec3 lo(1e9f), hi(-1e9f);
    for (int i = 0; i < fp.wreck.pieces; i++) {
      const vec3 m = fp.wreck.mid[i]; const float r = fp.wreck.rad[i] + 0.5f;
      if (length(m - pv.pos) > 80.f) continue;
      wreckIn |= 1u << i;
      lo = vec3(std::min(lo.x, m.x - r), std::min(lo.y, m.y - r), std::min(lo.z, m.z - r)); hi = vec3(std::max(hi.x, m.x + r), std::max(hi.y, m.y + r), std::max(hi.z, m.z + r));
    }
    if (!wreckIn) return;
    c = (lo + hi) * 0.5f; R = 0.5f * length(hi - lo);
  }
  // which maps: the sun when up; the lights in the proxy's slots (gbShadowSlot: the brightest shadow-casting first)
  int want = 0; int lightOf[4] = {-1, -1, -1, -1};
  if (fp.sunDir.y > -0.05f) want |= 1;
  for (int i = 0; i < fp.plN; i++) {
    const FrameParams::PointLight& L = fp.pl[i];
    if (L.shadow <= 0.f) continue;
    // (an omnidirectional lamp on the airframe itself - a beacon, a nav light: by day its light is lost under the sun's
    // and it needs no shadow (proxyNeedsMarch); at night it is mapped looking down, over the ground it lights)
    if (L.cosCut <= 0.05f && length(L.pos - c) < R * 1.1f && fp.sunDir.y > 0.08f) continue;
    float li = std::max(L.col.x, std::max(L.col.y, L.col.z)); int slot = 0;
    for (int k = 0; k < fp.plN; k++) {
      if (k == i || fp.pl[k].shadow <= 0.f) continue;
      float lk = std::max(fp.pl[k].col.x, std::max(fp.pl[k].col.y, fp.pl[k].col.z));
      if (lk > li || (lk == li && k < i)) slot++;
    }
    if (slot < 3) { want |= 2 << slot; lightOf[1 + slot] = i; }
  }
  if (!want) return;
  ensureShadowMaps();
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
      vec3 d, up; float fov, zn = 0.2f;
      if (L.cosCut <= 0.05f && dc < R * 1.1f) {   // an omnidirectional lamp on the airframe: straight down, nearly a hemisphere
        d = vec3(0, -1, 0); up = vec3(0, 0, 1); fov = 165.f * DEG; zn = 0.05f;
      } else if (L.cosCut > 0.05f && dc < R * 1.1f) {   // a lamp on the airframe: perspective along its beam, out to its reach
        d = normalize(L.dir); up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
        fov = std::min(2.f * (acosf(clampf(L.cosCut, -1.f, 1.f)) + 0.12f), 165.f * DEG);
      } else {   // a light on the ground (the apron's, the runway's): looking at the airframe, wide enough for its bound
        d = normalize(c - L.pos); up = fabsf(d.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
        fov = std::min(2.f * asinf(clampf(R / std::max(dc, R * 1.001f), 0.f, 1.f)) + 0.1f, 165.f * DEG);
      }
      vp = perspective(fov, 1.f, zn, L.radius * 40.f + 400.f) * lookAt(L.pos, L.pos + d, up);
    }
    shMapVP[layer] = vp;
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texShMap, 0, layer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texShMov, 0, layer);
    GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
    glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);   // (the previous layer's hull pass left depth writes off: a clear obeys the masks)
    glClearDepth(1.0); float zero[4] = {0, 0, 0, 0}; glClearBufferfv(GL_COLOR, 0, zero); glClear(GL_DEPTH_BUFFER_BIT);
    // the static airframe: depth only
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    if (wm) {   // (or the wreck's pieces, each placed and cut to its share)
      glUseProgram(progShMapWreck);
      glUniformMatrix4fv(U(progShMapWreck, "uVP"), 1, GL_FALSE, vp.m);
      glUniform1i(U(progShMapWreck, "uPartInst"), -1);
      wreckDraw = 1;   // (each piece's own triangles, then the rigid parts of them all at once)
      for (int i = 0; i < fp.wreck.pieces; i++) {
        if (!(wreckIn & (1u << i))) continue;
        meshPiece = i;
        glUniformMatrix3fv(U(progShMapWreck, "uRot"), 1, GL_FALSE, fp.wreck.rot[i]);
        glUniform3f(U(progShMapWreck, "uPos"), fp.wreck.pos[i].x, fp.wreck.pos[i].y, fp.wreck.pos[i].z);
        setWreckPiece(progShMapWreck, fp, true, vec3());
        glBindVertexArray(wm->vao);
        drawMeshBody(*wm);
      }
      meshPiece = -1; wreckDraw = 2;
      setWreckPiece(progShMapWreck, fp, true, vec3());
      glBindVertexArray(wm->vao);
      drawPlaneParts(*wm, progShMapWreck, -1);
      wreckDraw = 0;
      shOn |= 1 << layer;
      continue;
    }
    glUseProgram(progShMap);
    glUniformMatrix4fv(U(progShMap, "uVP"), 1, GL_FALSE, vp.m);
    glUniformMatrix3fv(U(progShMap, "uRot"), 1, GL_FALSE, pv.rot);
    glUniform3f(U(progShMap, "uPos"), c.x, c.y, c.z);
    glUniform1i(U(progShMap, "uPartInst"), -1);
    glBindVertexArray(pm->second.vao);
    glDrawElements(GL_TRIANGLES, pm->second.idx, GL_UNSIGNED_INT, nullptr);
    drawPlaneParts(pm->second, progShMap, -1);   // (its moving parts at their pose: no longer marched under a mask)
    // the moving hull: a mask, no depth
    if (mov) {
      glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glUseProgram(progShMov);
      glUniformMatrix4fv(U(progShMov, "uVP"), 1, GL_FALSE, vp.m);
      glUniformMatrix3fv(U(progShMov, "uRot"), 1, GL_FALSE, pv.rot);
      glUniform3f(U(progShMov, "uPos"), c.x, c.y, c.z);
      glUniform1i(U(progShMov, "uPartInst"), -1);
      if (!vaoHull) glGenVertexArrays(1, &vaoHull);
      glBindVertexArray(vaoHull); glBindBuffer(GL_ARRAY_BUFFER, mov->vbo);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
      glDrawArrays(GL_TRIANGLES, 0, mov->verts);
    }
    shOn |= 1 << layer;
    if (mov) shMovOn = true;
  }
  // the cockpit view: the cabin's own sun map, about the eye (the depth from the sun's side of the whole airframe on)
  if (slot == 1 && (want & 1) && !wm) {
    if (!texShCab) {
      glGenTextures(1, &texShCab); glBindTexture(GL_TEXTURE_2D, texShCab);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShCabRes, kShCabRes, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      // (compared by the hardware, filtered: af_shmap.glsl's sampler2DShadow)
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      glGenFramebuffers(1, &fboShCab); glBindFramebuffer(GL_FRAMEBUFFER, fboShCab);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texShCab, 0);
      glReadBuffer(GL_NONE);   // (depth alone: complete only without a read buffer under GL 3.3)
    }
    // the sun and the eye in the aircraft's own frame (pv.rot: body to world, column-major)
    const float* r = pv.rot;
    auto toBody = [&](vec3 v) { return vec3(r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z, r[6] * v.x + r[7] * v.y + r[8] * v.z); };
    const vec3 dB = normalize(toBody(normalize(fp.sunDir))), eB = toBody(fp.camPos - c);
    const float h = 2.5f, zf = 2.f * R + 3.f;
    const bool redraw = pm->second.key != shCabKey || ++shCabAge >= 8 || dot(dB, shCabDir) < cosf(0.25f * DEG) || length(eB - shCabEye) > 0.02f;
    if (redraw) {
      shCabKey = pm->second.key; shCabAge = 0; shCabDir = dB; shCabEye = eB;
      const vec3 up = fabsf(dB.y) < 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1);
      shCabBodyVP = orthoMat(-h, h, -h, h, 0.1f, zf) * lookAt(eB + dB * (2.f * R), eB, up);
      glBindFramebuffer(GL_FRAMEBUFFER, fboShCab);
      { GLenum none = GL_NONE; glDrawBuffers(1, &none); }
      glViewport(0, 0, kShCabRes, kShCabRes);
      glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
      // (the depth pushed back by its slope across a texel, by the hardware: a fixed bias is too little where the sun
      // grazes a curve - the windows' rounded lips came out blotched with their own shadow on the owner's GPU)
      glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 4.f);
      const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
      glUseProgram(progShMap);
      glUniformMatrix4fv(U(progShMap, "uVP"), 1, GL_FALSE, shCabBodyVP.m);
      glUniformMatrix3fv(U(progShMap, "uRot"), 1, GL_FALSE, ident);   // (drawn in the aircraft's frame)
      glUniform3f(U(progShMap, "uPos"), 0.f, 0.f, 0.f);
      glUniform1i(U(progShMap, "uPartInst"), -1);
      glBindVertexArray(pm->second.vao);
      glDrawElements(GL_TRIANGLES, pm->second.idx, GL_UNSIGNED_INT, nullptr);
      drawPlaneParts(pm->second, progShMap, -1);
      glDisable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0.f, 0.f);
    }
    // this frame's world-space lookup: world to body (the transpose of the rotation, about the aircraft), then the map
    mat4 w2b;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) w2b(a, b) = r[a * 3 + b];
    for (int a = 0; a < 3; a++) w2b(a, 3) = -(r[a * 3] * c.x + r[a * 3 + 1] * c.y + r[a * 3 + 2] * c.z);
    shCabVP = shCabBodyVP * w2b;
    // and from the camera, for a hit the shader has exactly relative to it (af_shmap.glsl): the same rotation, and the
    // camera's offset from the aircraft - small, so held to a hair where the world-space translation is not
    mat4 c2b = w2b; const vec3 cc = fp.camPos - c;
    for (int a = 0; a < 3; a++) c2b(a, 3) = r[a * 3] * cc.x + r[a * 3 + 1] * cc.y + r[a * 3 + 2] * cc.z;
    shCabVPc = shCabBodyVP * c2b;
    shCabBias = 0.004f / (zf - 0.1f);   // (4 mm, in the map's depth)
    shCabOn = true;
  }
  glBindVertexArray(0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Whether any shadow the proxy draws this frame has to be marched through a field: the player's (no map for the sun
// or for a shadowed light in a proxy slot - gbuffer.glsl gbShadowSlot - or a moving-hull mask in the maps) or a
// traffic aircraft's near enough to shadow the ground the proxy shades, without a map. Else the proxy's small build
// runs, with no field in it: on the owner's GPU the march's code alone made every pixel of the pass slower.
bool Renderer::proxyNeedsMarch(const FrameParams& fp) const {
  static const bool all = getenv("PROXYMARCH") != nullptr;   // (debug: always the full proxy)
  if (all || !progShProxyMaps) return true;
  const bool sun = fp.sunDir.y > -0.05f;
  if (fp.plane.on && fp.wreck.pieces == 0) {   // (a wreck's pieces shadow from the maps alone: rasterShadowMaps)
    const float R = std::max(fp.plane.M[0], fp.plane.M[9 * 4] * 2.f) * 0.55f + 1.5f;   // (planeBound, as rasterShadowMaps)
    if (shMovOn || (sun && !(shOn & 1))) return true;
    for (int i = 0; i < fp.plN; i++) {
      if (fp.pl[i].shadow <= 0.f) continue;
      const float li = std::max(fp.pl[i].col.x, std::max(fp.pl[i].col.y, fp.pl[i].col.z)); int slot = 0;
      for (int k = 0; k < fp.plN; k++) {
        if (k == i || fp.pl[k].shadow <= 0.f) continue;
        const float lk = std::max(fp.pl[k].col.x, std::max(fp.pl[k].col.y, fp.pl[k].col.z));
        if (lk > li || (lk == li && k < i)) slot++;
      }
      // (an airframe's own omni lamp - its nav lights - has no map by day: its light on the ground is lost under the
      // sun's, so its shadow there is too)
      const bool navByDay = fp.pl[i].cosCut <= 0.05f && length(fp.pl[i].pos - fp.plane.pos) < R * 1.1f && fp.sunDir.y > 0.08f;
      if (slot < 3 && !(shOn & (2 << slot)) && !navByDay) return true;
    }
  }
  if (sun)
    for (int k = 0; k < std::min(fp.trafficN, kMaxTrafficDrawn); k++) {
      const float* t = fp.traffic[k].t;
      const vec3 c(t[24 * 4], t[24 * 4 + 1], t[24 * 4 + 2]);
      if (!(trafShOn & (1 << k)) && length(c - fp.camPos) <= 6000.f + t[24 * 4 + 3]) return true;
    }
  return false;
}

// The airframes' shadows on the G-buffer's surfaces (the sun on the ground, the landing lights' beams): texGB[4]
void Renderer::rasterShadowProxy(const FrameParams& fp) {
  glBindFramebuffer(GL_FRAMEBUFFER, fboShProxy);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  GLuint prog = 0;
  if (proxyNeedsMarch(fp)) {   // (the aircraft it may march: the player's, and every traffic aircraft without a map)
    int model = -2; bool research = false;
    if (fp.plane.on && fp.wreck.pieces == 0) afCover(model, research, fp.plane.M, fp.plane.model);
    for (int k = 0; k < std::min(fp.trafficN, kMaxTrafficDrawn); k++) if (!(trafShOn & (1 << k))) afCover(model, research, fp.traffic[k].t, -1);
    prog = afPassProgram(kAfProxy, model, research);
  }
  setRT(prog ? prog : progShProxyMaps, fp);   // (the airframe shadow maps: setRT)
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
  static const bool off = getenv("RASTERNOFX") != nullptr; if (off) return;   // (debug)
  const auto& p = fp.plane;
  const bool intact = p.on && fp.wreck.pieces == 0, cockpit = p.PS[3] > 0.5f;
  const int engine = intact ? (int)(p.M[2] + 0.5f) : 0;
  // Conservative exact-empty gate. Exhaust/hologram/cloak families remain active regardless
  // of engine switches; their shader alone decides intensity. Weapons survive a missing player.
  const bool effects = fp.fx.beams + fp.fx.bombs + fp.fx.blasts > 0 ||
    (intact && (p.propCount > 0 || p.vapor[0] > .01f || (!cockpit && p.exhaust.count > 0) || (engine == 6 && (cockpit || p.wr[4][3] > .001f))));
  if (!effects || rw > histW || rh > histH) { rasterTrafficProps(fp); return; }
  // (the player's aircraft's: the XR-40's own - its cloak, its weapons, its hologram; wrecked too, its bombs still falling
  // and its blasts burning - else the light aircraft's build, which has no airframe in it: the propellers, the vapour and
  // the flames; with no aircraft, or weapons in the air with no XR-40 flying, every aircraft's)
  const bool wraith = p.on && (int)(p.M[2] + 0.5f) == 6, weapons = fp.fx.beams + fp.fx.bombs + fp.fx.blasts > 0;
  const GLuint progEffects = afPassProgram(kAfEffects, wraith ? afModelOf(p.M, p.model) : -1, wraith || weapons || !p.on);
  if (!progEffects) { rasterTrafficProps(fp); return; }
  const bool cloakTex = wraith && rasterCloak(fp);   // (the cloaked part's surface from the mesh: no march in the effects)
  const int cur = histIdx ^ 1;
  glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[cur]);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  setRT(progEffects, fp);
  glActiveTexture(GL_TEXTURE0 + 24); glBindTexture(GL_TEXTURE_2D, texRaw); glUniform1i(U(progEffects, "uRawTex"), 24);
  glActiveTexture(GL_TEXTURE0 + 25); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progEffects, "uSceneDepth"), 25);
  glActiveTexture(GL_TEXTURE0 + 29); glBindTexture(GL_TEXTURE_2D, cloakTex ? texCloak : 0); glUniform1i(U(progEffects, "uCloakTex"), 29);
  glUniform1i(U(progEffects, "uCloakTexOn"), cloakTex ? 1 : 0);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboTAA[cur]); glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboComp);
  glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  rasterTrafficProps(fp);
}

// The cloaked XR-40's cloaked part (behind the cloak's sweeping front), drawn from its outside mesh into texCloak: the
// distance to its nearest surface along each pixel's ray (0: none) and that surface's normal (world). The effects pass
// shows the frame behind it along a ray bent by that normal; it used to march the craft's whole shape for every pixel
// the craft covered, which on the owner's GPU took the frame from over 100 fps to 59 the moment the cloak came on.
// False (the effects march instead) when the view isn't outside, or the mesh or the program isn't there.
bool Renderer::rasterCloak(const FrameParams& fp) {
  const PlaneVisual& pv = fp.plane;
  static const bool off = getenv("CLOAKMARCH") != nullptr;   // (debug A/B: the march, as before)
  if (off || !pv.on || fp.wreck.pieces > 0 || pv.PS[3] > 0.5f || pv.wr[4][3] <= 0.001f || fp.pano > 0.f || meshOff) return false;
  auto it = planeMeshes.find(hullKey(fp, 0));
  if (it == planeMeshes.end() || !it->second.ok || !it->second.idx) return false;
  const PlaneMesh& pm = it->second;
  if (!cloakTried) {
    cloakTried = true;
    std::string e;
    progCloak = linkProgramCached(planeMeshVSAssembly(""),
      "#version 330 core\n"
      "in vec3 vW; in vec3 vN; flat in float vId; in float vIdS; in float vAo; in vec3 vB; flat in int vPc;\n"
      "uniform float uCloakZ; uniform mat3 uRot;\n"
      "out vec4 oCk;\n"
      "void main(){\n"
      "  if (vB.z >= uCloakZ) discard;   // (ahead of the front: the craft as it is, in the G-buffer)\n"
      "  oCk = vec4(length(vW), normalize(uRot*vN));\n"
      "}\n", e);
    if (!progCloak) shaderNote("Cloak surface shader failed (the cloak is marched instead):\n" + e);
  }
  if (!progCloak || cloakFailed) return false;
  // allocated once at the full window size like every other target (the render resolution is its lower-left part, read
  // with texelFetch), half floats (the distance to a surface a few hundred metres off, and a normal), and checked: an
  // incomplete one falls back to the march (the review of v3.44.0, RND-1: Auto 60's every 5% step reallocated 40 MiB)
  if (!texCloak || cloakW != allocW || cloakH != allocH) {
    if (!texCloak) { glGenTextures(1, &texCloak); glGenTextures(1, &texCloakZ); glGenFramebuffers(1, &fboCloak); }
    while (glGetError() != GL_NO_ERROR) {}   // (only this allocation's errors below)
    glBindTexture(GL_TEXTURE_2D, texCloak);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, allocW, allocH, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, texCloakZ);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, allocW, allocH, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fboCloak);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texCloak, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texCloakZ, 0);
    cloakW = allocW; cloakH = allocH;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() == GL_OUT_OF_MEMORY) {
      shaderNote("Cloak target incomplete or out of memory: the cloak is marched instead");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fboCloak); glDeleteTextures(1, &texCloak); glDeleteTextures(1, &texCloakZ);
      fboCloak = texCloak = texCloakZ = 0; cloakFailed = true;
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fboCloak);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  float zero[4] = {0, 0, 0, 0}; glClearBufferfv(GL_COLOR, 0, zero); glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
  const mat4 vp = viewProjRel(fp, 0.01f, 2000.f);   // (as the mesh pass: the same jittered pixels)
  const vec3 rp = pv.pos - fp.camPos;
  glUseProgram(progCloak);
  glUniformMatrix4fv(U(progCloak, "uVP"), 1, GL_FALSE, vp.m);
  glUniform2f(U(progCloak, "uJit"), jitX, jitY);
  glUniform1f(U(progCloak, "uLogC"), logDepthC());
  glUniformMatrix3fv(U(progCloak, "uRot"), 1, GL_FALSE, pv.rot);
  glUniform3f(U(progCloak, "uPos"), rp.x, rp.y, rp.z);
  glUniform1f(U(progCloak, "uCloakZ"), pv.wr[6][1]);
  glUniform1i(U(progCloak, "uWreck"), 0); glUniform1i(U(progCloak, "uWreckParts"), 0);
  glUniform1i(U(progCloak, "uPartInst"), -1);
  glBindVertexArray(pm.vao);
  glDrawElements(GL_TRIANGLES, pm.idx, GL_UNSIGNED_INT, nullptr);
  drawPlaneParts(pm, progCloak, -1);   // (its moving parts at their pose: the turrets, the bay doors, the nozzles)
  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
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
  bindSky(progLight, fp);   // (the stars: sky.cpp)
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  depthValid = true;
}
