// Solace Express - renderer interface
#pragma once
#include <chrono>
#include <functional>
#include <atomic>
#include <unordered_map>
#include "common.h"
#include "gl.h"
#include "world.h"
#include "entity_mesh.h"
#include "ground_vehicle.h"
#include "feed_cameras.h"

struct SpriteVert { float x, y, z, u, v, r, g, b, a, kind, soft, bill = 0; };   // bill > 0: x,y,z is the centre of a camera-facing square of that half size
enum SpriteKind { SPR_SMOKE = 0, SPR_GLOW = 1, SPR_RING = 2, SPR_RAIN = 3, SPR_FIRE = 4, SPR_SNOW = 5, SPR_SHOCK = 6, SPR_SPARK = 7, SPR_RIBBON = 8, SPR_FLAME = 9 };

struct PlaneVisual {
  bool on = false;
  vec3 pos; float rot[9];  // body->world, column-major
  float M[24 * 4];          // model geometry (models.cpp packModel)
  float PS[4], Ctl[4], Pr[4], I0[4], I1[4], I2[4];  // state, controls, prop, instruments
  float wheel[3] = {};      // the wheels' roll (radians about body +x): main left, main right, nose / tail (Plane::wheelMotion)
  int model = -1;           // the type (index into kAircraft): the field reads it for a type's own fittings (kOsprey)
  vec3 colBase, colStripe;
  float reg[3] = {65, 65, 65};   // registration letters (character codes)
  float prop[2][4]; int propCount = 0;
  float hud[4] = {0, 0, 0, 0}, hud2[4] = {1, 0, 0, 0}, hudV[3] = {0, 0, -1}, hud3[4] = {0, 0, 0, 1000};  // research jet HUD data
  float flame[4] = {0, 0, 0, 0};  // research jet exhaust: spool, reheat, nozzle vector angle (rad), mach
  float vapor[4] = {0, 0, 0, 0};  // transonic vapour cone: density, start z, start radius, length (body space)
  int lensN = 0; float lensP[6][4] = {}, lensC[6][4] = {}, lensD[6][4] = {};   // light fixtures (body space): lens centre | emission | axis + tint
  float wr[7][4] = {};            // XR-40: pod tilts, yaw vanes, thrusts, pitch vanes | fan, bay, lasers, stealth | surfaces, laser fire | bomb, cloak front, armed
};

// XR-40 weapons in the world (see weaponsFx in shaders.h)
struct FxVisual {
  int beams = 0; float beamA[16][4], beamB[16][4]; // laser bolts: tail + radius, head + intensity
  int bombs = 0; float bomb[8][4];                 // dark-energy bombs: centre + radius
  int blasts = 0; float blast[6][4], blastI[6][4]; // detonations: centre + radius, age 0..1 + intensity
  float pip[4] = {0, 0, 0, 0};                     // XR-40 bomb impact prediction: world point + valid
  float feed[4] = {0, 0, 0, 0};                    // XR-40 belly camera target: world point + active
};

// One AI traffic aircraft for the ray tracer: 32 texels (see loadTraffic in shaders.h)
struct TrafficVisual { float t[32 * 4]; };
static const int kMaxTrafficDrawn = 12;

struct WreckVisual {
  int pieces = 0;               // > 0: draw these clipped pieces instead of the intact aircraft
  vec3 pos[5]; float rot[5][9]; // piece centre (world) and body->world rotation
  vec3 C[5], H[5];              // clip box (body coords) of each piece
  int debris = 0;
  float deb[16][4], debQ[16][4];  // chunk centre + size (negative = charred), orientation quaternion (w,x,y,z)
  int craterN = 0; float crater[24][4] = {};  // x, z, radius, depth (negative depth: dark-energy crater)
};

// Shader programs, compiled once and then loaded from the driver-binary cache in g_shaderCacheDir (empty: no cache)
extern std::string g_shaderCacheDir;
extern std::atomic<int> g_shaderCacheHits, g_shaderCacheMisses;   // bumped from several GL threads at startup
std::string shaderCacheStamp();   // fingerprint of all shader sources + the driver (current context needed)
bool writePNG(const char* path, int w, int h, const std::vector<uint8_t>& rgbBottomUp);
bool readImage(const char* path, int& w, int& h, std::vector<uint8_t>& rgbaTopDown);   // PNG or JPEG
GLuint linkProgramCached(const std::string& vs, const std::string& fs, std::string& err);
GLint U(GLuint prog, const char* name);   // a uniform's location (cached per program)

struct FrameParams {
  std::vector<GroundVehicleVisual> groundVehicles;   // explicitly driven ground vehicles (none yet: the airport furniture is parked)
  vec3 camPos; vec3 camRight, camUp, camBack; float fovY = 1.0f;
  float dt = 1.f / 60.f;   // this frame's real time step (frame-rate independent blending: the TAA)
  float pano = 0.f, panoTanY = 0.f;   // > 0: a panoramic (cylindrical) camera feed: its half angle (rad) and vertical extent (fovY then only culls)
  float time = 0;
  vec3 sunDir, sunCol; float night = 0;
  float planeTerrSh = 1.f;   // terrain's sun shadow at the player's aircraft (one value for the whole airframe, from the CPU)
  bool prefetchOn = false; vec3 prefetchPos;   // scenery to have the worker threads build ahead (the menu tour's next place)
  float cloudCover = 0.3f, cloudBase = 1500, fogB = 0.0001f, wet = 0, snow = 0, lightning = 0, storm = 0;
  vec2 windOff;
  vec3 wind;   // surface wind velocity (m/s, the way the air moves): windsocks
  PlaneVisual plane;
  WreckVisual wreck;
  FxVisual fx;
  FeedCamera feeds[kMaxFeeds];   // research jets: the cameras whose pictures the cockpit displays show (feed_cameras.h)
  int feedRig = 0;               // their rig: 0 none, 1 XR-30, 2 XR-40
  vec3 landLightPos, landLightDir; float landLight = 0;
  vec3 flameLightPos, flameLight;  // a blast's light (radiance; zero when off), folded into the point lights
  struct PointLight { vec3 pos; float radius; vec3 col; float cosCut; vec3 dir; float shadow; };
  PointLight pl[12]; int plN = 0;
  int dispMode = 0, dispCk = 0;   // cockpit display atlases this frame: bit 1 display pages, bit 2 instrument panel (cockpit type)
  float rwyLights = 0;   // airport lighting on (night / low visibility): 0..1   // point / spot lights: radiance, spot cutoff (-2 omni), shadow stop distance (0 none)
  float exposure = 1.0f, rainLens = 0, fade = 1, vignette = 0.6f, gLoad = 0;
  bool sealedCockpit = false;
  int trafficN = 0; TrafficVisual traffic[kMaxTrafficDrawn];
  bool ufoOn = false; vec3 ufoPos; float ufoRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, ufoAnim[4] = {0, 0, 0, 0};   // UFO encounter   // XR-30 cockpit view: no sun glare (the pilot sees the sun only on the displays)   // gLoad: g-force tunnel 0..1
};

struct UIVert { float x, y, u, v, r, g, b, a, mode, hx, hy, p; };

class Renderer {
public:
  int W = 0, H = 0;          // window size
  float renderScale = 1.0f;
  int quality = 1;           // 0 low, 1 medium, 2 high
  int dbgOff = 0;            // profiling: ray tracer features switched off (uDbg bits)
  int mode = 0;              // 0 the ray tracer, 1 the raster renderer (docs/RENDERER_REBUILD.md; needs rasterOk)
  bool screenWindows = getenv("SCREENFEEDS") == nullptr;   // the research craft's displays are windows (no camera feeds but the bomb camera's; SCREENFEEDS=1 brings the cameras back)
  int bakeCount = 0;   // airframe meshes and hulls baked so far (the research terminal's warm-up waits for a frame that bakes nothing)
  std::function<void()> bakeYield;   // called between the bake's evaluation batches (the benchmark answers the window's messages during a long bake)
  int modeForce = -1;        // the tools' --raster / the harness' RASTER: overrides the setting whenever the game applies it
  bool rasterOk = false;     // the raster renderer's programs built
  bool ok = false;
  std::string error;
  GLuint minimapTex = 0;

  bool initUI(int w, int h);                     // UI program + font only (the intro screen)
  static constexpr int kProgramCount = 22;
  float terrainCeiling() const { return maxH; }   // highest point of the terrain (m)
  // analysis tool (--analyze): exact per-pass times (the GPU is waited on at every pass boundary) and a build of the
  // ray tracer that writes its per-pixel work counters instead of colour
  bool syncTiming = false; double passWall[11] = {};   // (kPasses)
  bool costMap = false;
  bool buildCostProgram();
  bool readCostMap(std::vector<float>& out, int& w, int& h);
  std::string rtSource(const char* defines);
  std::string dispError;   // set when the cockpit display shader failed to build (the screens stay dark)
  bool compilePrograms(std::atomic<int>* done);  // scene programs; safe on a worker thread with a shared context
  bool init(int w, int h);                       // everything else (runs compilePrograms itself if not done yet)
  GLuint makeTexture(const uint8_t* rgba, int w, int h);
  void renderMap(float cx, float cz, float half, int N);   // GPS aerial image into mapTex()
  GLuint mapTex() const { return texMap; }
  void resize(int w, int h);
  void setRenderScale(float s);   // ray-trace resolution only: the TAA history stays at display resolution, no pop
  void renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites);
  mat4 viewProj(const FrameParams& fp, float zNear = 0.5f, float zFar = 90000.f) const;
  mat4 viewMat(const FrameParams& fp) const;   // world -> camera (x right, y up, z back)
  bool project(const FrameParams& fp, vec3 p, float& sx, float& sy) const;  // to window pixels

  // ---- immediate-mode 2D UI
  void clearScreen();   // the window cleared to black (when no scene is drawn under the UI)
  void uiBegin();
  void rect(float x, float y, float w, float h, vec3 c, float a = 1.0f, float radius = 0.0f);
  void line(float x0, float y0, float x1, float y1, float th, vec3 c, float a = 1.0f);
  void rectGrad(float x, float y, float w, float h, vec3 top, vec3 bottom, float a = 1.0f, float radius = 0.0f);
  void rectOutline(float x, float y, float w, float h, vec3 c, float a, float radius, float thickness);
  void glow(float x, float y, float w, float h, vec3 c, float a, float radius, float soft);  // soft halo around a rounded rect
  float text(float x, float y, float size, const std::string& s, vec3 c, float a = 1.0f, int align = 0, bool shadow = true);
  float textWidth(const std::string& s, float size) const;
  void image(GLuint tex, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, float a = 1.0f);
  void uiEnd();
  void flushUIPublic() { flushUI(); }
  bool screenshot(const char* path);
  bool screenshotPNG(const char* path);
  // environment entities: entSync generates every chunk in range before drawing (headless captures)
  bool entSync = false;
  int entDrawn = 0, entChunks = 0;
  int entPending = 0;            // scenery chunks still to generate around the camera (after this frame)
  float entBudgetMs = 2.5f;      // per-frame scenery generation budget for chunks beyond 700 m (raised on loading screens)   // instances drawn / chunks generated (F3 readout)
  float entCpuMs = 0;                // CPU time of the entity pass (streaming + culling + submission)

private:
  GLuint progMap = 0, texMap = 0, fboMap = 0; int mapN = 0;
  GLuint progDisp = 0, texPages = 0, texPanel = 0, fboDisp = 0;
  std::chrono::steady_clock::time_point syncT;
  GLuint progRTCost = 0;
  // quarter-resolution clouds: the cloud march, its full-resolution composite, their targets
  GLuint progClouds = 0, progCloudComp = 0, texCloud = 0, texCloudD = 0, fboCloud = 0, texCloudMask = 0, fboComp = 0;
  int cw = 0, ch = 0;
  // baked terrain sun shadow (world space): front = the one the ray tracer reads, back = the one being baked
  GLuint progTShBake = 0, texTSh[2] = {0, 0}, fboTSh = 0;
  int tshFront = -1, tshBack = 0, tshRow = 0; bool tshBaking = false; vec3 tshSun, tshBakeSun;
  static constexpr int kTShN = 2048, kTShRows = 64;   // texels per side, rows baked per frame
  void bakeTerrainShadow(const FrameParams& fp);            // analysis build of the ray tracer (built on demand)
  GLuint progCkMask = 0;           // cockpit occlusion mask for the scenery pass
  bool depthValid = false, ckMaskPrev = false;   // last frame's ray-traced depth is usable / was a cockpit view
  vec3 ckLookPrev, ckUpPrev; float ckFovPrev = 0;   // last frame's view inside the cabin (the mask's validity, entity_render.cpp)
  void renderDisplays(const FrameParams& fp, bool panel);
  GLuint progRT = 0, progSprite = 0, progDown = 0, progUp = 0, progRayMask = 0, progRay = 0, progPost = 0, progUI = 0, progTAA = 0, progFeedRays = 0;
  static constexpr int kBloomMips = 6;
  GLuint fboMip[kBloomMips] = {}, texMip[kBloomMips] = {}; int mipW[kBloomMips] = {}, mipH[kBloomMips] = {};
  GLuint fboRay[2] = {0, 0}, texRay[2] = {0, 0};
  GLuint vaoEmpty = 0, vaoSprite = 0, vboSprite = 0, vaoUI = 0, vboUI = 0;
  GLuint texHM = 0, texAlb = 0, texNrm = 0, texFont = 0, texMask = 0, texRoadId = 0, texData = 0, texHMax = 0;
  struct V4 { float x, y, z, w; };
  GLuint fboScene = 0, texColor = 0, texDepth = 0, fboSprite = 0;
  // temporal AA: the ray tracer writes texRaw; the resolve blends it with the reprojected history into texHist[cur] + texColor
  GLuint texRaw = 0, texHist[2] = {0, 0}, fboTAA[2] = {0, 0};
  GLuint texTraffic = 0;
  int histIdx = 0, frameNo = 0, histW = 0, histH = 0; bool histValid = false;   // (histW/H: the history textures' size)
  // GPU frame time from a ring of timer queries (read a few frames late so the CPU never waits on them)
  GLuint gpuQ[4] = {0, 0, 0, 0}; bool gpuQUsed[4] = {false, false, false, false}; int gpuQi = 0;
public:
  float gpuMs = -1.f;   // last measured GPU time of renderScene, ms (-1 = not known yet)
  static constexpr int kPasses = 11;   // world | displays | feeds | objects | airframe shadow proxy | lighting+clouds+effects (the ray tracer: scenery | displays | - | - | - | ray trace+clouds), then TAA, sprites, bloom, light shafts, composite
  float passMs[kPasses] = {};         // GPU time of each pass (timestamp queries, a few frames late)
  GLuint stampQ[4][kPasses + 1] = {}; bool stampUsed[4] = {};
  void stamp(int i) { if (syncTiming) syncStamp(i); else if (stampQ[gpuQi][i]) glQueryCounter(stampQ[gpuQi][i], GL_TIMESTAMP); }
  void syncStamp(int i);
private:
  vec3 prevCamPos, prevPlanePos; float prevCamRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, prevPlaneRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  int rw = 0, rh = 0, bw = 0, bh = 0;
  float maxH = 2500;
  std::vector<UIVert> ui;
  GLuint curImg = 0;
  void flushUI();
  void createTargets();
  void createRenderTargets();
  void scaleDims();
  int allocW = 0, allocH = 0;   // the size the ray-tracing resolution's targets were made at (rw x rh is its lower-left part)
  float jitX = 0, jitY = 0;
  void genMaterials();
  void genCloudNoise();   // tileable cloud coverage (2D) and billow / detail noise (3D) textures
  GLuint texCloudCov = 0, texNoise3 = 0;
  void genMinimap();
  // ---- environment entities: instanced meshes -> G-buffer (lit by the ray tracer) + sun shadow cascades
  GLuint progEnt = 0, progEntSh = 0, vaoEnt = 0, vboEntMesh = 0, vboEntInst = 0;
  GLuint fboGB = 0, texGB[5] = {0, 0, 0, 0, 0}, texGBDepth = 0, fboShProxy = 0;   // (texGB[4]: the raster renderer's shadow proxy)
  GLuint fboSh[2] = {0, 0}, texSh[2] = {0, 0}; int shRes = 0;
  // shadows fade between kShFade0 and kShFade1 x the cascade radius around shIdeal (camera-anchored, so a cached
  // map re-rendering never changes a pixel: each map covers at least 0.84 x its radius around that point)
  static constexpr float kShFade0 = 0.5f, kShFade1 = 0.78f;
  vec3 shIdeal[2]; float entTreeFar = 4500.f;
  mat4 shVP[2]; vec3 shCenter[2], shSun[2]; bool shValid[2] = {false, false}; int shGen[2] = {-1, -1}, shAge[2] = {0, 0}; float shR[2] = {0, 0};
  EntMeshRange entRange[EK_COUNT];
  std::vector<Ent> entStage;
  uint64_t groundShadowKey[2] = {};   // the driven vehicles each cascade last drew (a change re-renders it)
  int entFrame = 0, entGenCount = 0;
  bool initEntities();
  // ---- terrain envelope mesh (terrain_envelope.cpp): where each pixel's exact terrain march starts
  GLuint progEnv = 0, vaoEnv = 0, vboEnvInst = 0, texEnvV0 = 0, texEnvM = 0, fboEnv = 0, texEnv = 0, texEnvDepth = 0;
  std::vector<float> envInst;
  bool envOn = false;
  bool compileEnvelope();
  void initEnvelope();
  void createEnvelopeTarget();
  void drawEnvelope(const FrameParams& fp);
public:
  int envChunks = 0;
private:
  // ---- aircraft hull meshes (aircraft_hull.cpp): where each pixel's exact airframe march starts
  struct HullMesh { uint64_t key = 0; GLuint vbo = 0; int verts = 0; bool ok = false; };
  // the aircraft mesh (aircraft_mesh.cpp): the static part of the airframe baked from its field, and the hull of the
  // part that moves (the march's start on the raster path, where the mesh leaves off)
  struct PlaneMesh { uint64_t key = 0; GLuint vao = 0, vbo = 0, ibo = 0; int idx = 0; bool ok = false; uint64_t movKey = 0; bool eyeInMov = false; };
  std::unordered_map<uint64_t, PlaneMesh> planeMeshes;
  GLuint progPlaneMesh = 0, progPlaneMeshDepth = 0;   // (the depth pre-pass: the airframe's inner and outer skins both face the camera; only the nearest is shaded)
  bool compilePlaneMesh();
  bool planeMeshWanted(const FrameParams& fp) const;
  void bakePlaneMesh(const FrameParams& fp, int slot, uint64_t key);
  void drawPlaneMesh(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK);
  uint64_t trafficModelKey(const float* t) const;   // hullKey(slot 0) of a traffic aircraft's model
  std::unordered_map<uint64_t, HullMesh> hulls;   // every airframe baked so far, outside and cockpit (keyed by hullKey)
  GLuint progHull = 0, progHullBake = 0, vaoHull = 0, texHPts = 0, texHOut = 0, fboHOut = 0, fboHull = 0, texHullDepth = 0;
  int hullDepthW = 0, hullDepthH = 0;
  bool hullOn = false;
  bool compileHull(const std::string& bakeVS, const std::string& bakeFS);
  void hullEval(const std::vector<vec3>& pts, std::vector<float>& out);
  void hullEval4(const std::vector<vec3>& pts, std::vector<float>& out);
  void bakeHull(const FrameParams& fp, int slot, uint64_t key);
  uint64_t hullKey(const FrameParams& fp, int slot) const;
  bool hullWanted(const FrameParams& fp) const;
  float hullNear(const FrameParams& fp) const;
  void drawHull(const FrameParams& fp, int slot, uint64_t key, float nearOverride = -1.f, bool exitToo = false);
  GLuint texDepthCopy = 0, fboDepthCopy = 0; int depthCopyW = 0, depthCopyH = 0;   // the G-buffer's depth after the mesh draws: the objects pass marches no further (raster_renderer.cpp)
  bool hullExitOn = false;   // the hull pass wrote the hull volume's end per ray (uEnv's fourth channel): the march stops there
  float hullNearNow = 0.f;   // the hull pass's near distance this frame (uHullNear): hullNear(fp), or a mesh's moving hull's own
  void drawTrafficHulls(const FrameParams& fp, const PlaneMesh* const* meshes = nullptr);   // meshes[k]: that traffic's mesh (its moving hull is drawn instead)
  void ensureHullTarget();
  bool trafHullOn = false;
  // camera feeds: each research-jet camera is drawn into a tile of the feed atlas, through the same passes as the main
  // view (envelope, scenery G-buffer, ray tracer, clouds) on a second set of render targets of its own size
  struct ViewTargets {
    int W = 0, H = 0, rw = 0, rh = 0, cw = 0, ch = 0, allocW = 0, allocH = 0;
    GLuint texRaw = 0, texDepth = 0, texCloudMask = 0, texCloud = 0, texCloudD = 0, fboCloud = 0, fboComp = 0, fboScene = 0;
    GLuint texGB[5] = {0, 0, 0, 0, 0}, texGBDepth = 0, fboGB = 0, fboShProxy = 0, texEnv = 0, texEnvDepth = 0, fboEnv = 0;
    bool depthValid = false, envOn = false, hullOn = false, trafHullOn = false, ckMaskPrev = false;
    vec3 ckLookPrev, ckUpPrev; float ckFovPrev = 0;
    float jitX = 0, jitY = 0;
  };
  void swapView(ViewTargets& v);
  ViewTargets feedView;                // (allocated at the largest picture; each camera uses its corner of it)
  static constexpr int kFeedMaxW = 2048, kFeedMaxH = 1024, kFeedAtlasW = 4096, kFeedAtlasH = 3072;
  GLuint texFeed = 0, fboFeed = 0;
  float feedTile[kMaxFeeds][4] = {};   // atlas rectangle of each slot (uv: x0, y0, w, h)
  int feedTileWH[kMaxFeeds][2] = {};
  bool feedValid[kMaxFeeds] = {};      // its tile holds a picture
  int feedAge[kMaxFeeds] = {};         // frames since its picture was drawn
  int feedRigNow = 0;
  bool feedPass = false;               // drawing a camera feed (the scenery pass leaves streaming and shadows alone)
  bool feedsWanted(const FrameParams& fp) const;
  void measureFeedMounts(const FrameParams& fp);
  void renderFeeds(const FrameParams& fp, const std::function<void(GLuint, const FrameParams&)>& setRT, const std::function<void(const FrameParams&, GLuint)>& trace,
                   const std::function<void(const FrameParams&)>& effects);
  // offscreen frames (the launch prewarm): the composite and the UI go to a hidden target instead of the window
  GLuint fboOff = 0, texOff = 0; int offW = 0, offH = 0;
public:
  void setOffscreen(bool on);
  bool hullBaked(const FrameParams& fp) const;   // the hull this frame wants is ready (or none is wanted)
  bool hullCockpit = getenv("HULLCOCKPIT") != nullptr;   // cockpit hulls too (in testing)
  bool hullOff = getenv("HULLOFF") != nullptr;           // debug: no hulls (every aircraft march starts from the camera)
  bool meshOff = getenv("MESHOFF") != nullptr;           // debug: no aircraft meshes on the raster path (the whole airframe marches)
  bool envOff = getenv("ENVOFF") != nullptr;             // debug: no terrain envelope (every pixel marches from the camera)
  bool tshPending() const { return tshBaking || tshFront < 0; }
  void resetTemporal() {   // forget every frame-to-frame accumulation (TAA history, jitter/seed sequence, terrain-shadow bake):
    frameNo = 0; histIdx = 0; histValid = false;   // the next frame renders as if it were the first (exact test comparisons)
    tshFront = -1; tshBack = 0; tshRow = 0; tshBaking = false;
    for (bool& v : shValid) v = false;   // shadow cascades re-render
  }
private:
  GLuint screenFbo = 0;
  void drawEntities(const FrameParams& fp);
  void createGBuffer();
  // ---- the passes of a frame, shared by the ray tracer and the raster renderer (renderer.cpp)
  bool cloudSplit = false;   // this frame's clouds come from the quarter-resolution cloud pass
  const std::vector<SpriteVert>* curAlpha = nullptr; const std::vector<SpriteVert>* curAdd = nullptr;   // this frame's sprites
  void setRT(GLuint p, const FrameParams& fp);          // the scene's uniforms and textures for a program
  void traceRT(const FrameParams& fp, GLuint prog);     // the ray tracer over a view
  void cloudPass(const FrameParams& fp);                // the clouds at a quarter of the pixels, composited over the lit view
  void drawSprites(const FrameParams& fp, float texW, float texH, float uvsX, float uvsY);
  void feedEffects(const FrameParams& f);
  // ---- the raster renderer (raster_renderer.cpp, terrain_mesh.cpp)
  GLuint progLight = 0, progObjects = 0, progShProxy = 0, progEffects = 0, progTerrain = 0, progWater = 0;
  // the airframe shadow maps (raster_renderer.cpp): the player's baked static mesh rendered from the sun (layer 0,
  // orthographic) and from the three brightest shadow-casting lights (layers 1-3, perspective along each beam); a
  // second array marks where the moving hull is, so the proxy still marches the field for the gear, the surfaces and
  // the props. uShOn: bit 0 the sun, bits 1-3 the light slots
  GLuint progShMap = 0, progShMov = 0, texShMap = 0, texShMov = 0, fboShMap = 0; int shOn = 0; mat4 shMapVP[4];
  static constexpr int kShMapRes = 1024;
  void rasterShadowMaps(const FrameParams& fp);
  GLuint iboTerrain = 0, vaoTerrain = 0, vboTerrainInst = 0, vaoWater = 0, vboWater = 0, iboWater = 0; int waterIdx = 0;
  std::vector<float> terrInst; int terrChunks = 0;
  bool compileRaster();
  bool compileTerrainMesh();
  void initTerrainMesh();
  void selectTerrainChunks(const FrameParams& fp);
  void drawTerrainMesh(const FrameParams& fp);
  void rasterWorld(const FrameParams& fp);
  void rasterObjects(const FrameParams& fp);
  void rasterShadowProxy(const FrameParams& fp);
  void rasterEffects(const FrameParams& fp);
  void rasterLight(const FrameParams& fp);
};

extern Renderer g_ren;
