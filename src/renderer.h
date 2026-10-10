// Solace Express - renderer interface
#pragma once
#include "benchmark_metrics.h"
#include <array>
#include <chrono>
#include <functional>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "common.h"
#include "gl.h"
#include "world.h"
#include "entity_mesh.h"
#include "ground_vehicle.h"
#include "feed_cameras.h"
#include "exhaust.h"

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
  float engineHealth[4] = {1,1,1,1}; // actual per-engine failure health, for live cockpit indication
  ExhaustVisual exhaust;
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

// One AI traffic aircraft for the shaders: 32 texels (see loadTraffic in shaders.h)
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
std::string meshCacheStamp();     // fingerprint of the sources the aircraft mesh bake depends on + the driver
std::string shaderCacheStamp();   // fingerprint of all shader sources + the driver (current context needed)
bool writePNG(const char* path, int w, int h, const std::vector<uint8_t>& rgbBottomUp);
bool readImage(const char* path, int& w, int& h, std::vector<uint8_t>& rgbaTopDown);   // PNG or JPEG
GLuint linkProgramCached(const std::string& vs, const std::string& fs, std::string& err, bool* usedSafeGear = nullptr);
extern std::string g_shaderNotes;          // programs the driver's compiler rejected and what built instead (startup.log)
void shaderNote(const std::string& s);      // (adds a line to it; safe from the compile threads)
GLint U(GLuint prog, const char* name);   // a uniform's location (cached per program; name must be a string literal)

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
  vec3 wind;   // surface wind velocity (m/s, the way the air moves): the sea's waves, the clouds' lean
  vec3 windSock;   // the surface wind with its gusts where the camera is (the windsocks swing with them)
  vec3 cloudDet; float cloudBoil = 0;   // the cloud detail's drift through the cloud bodies and the billows' rise (m: Weather::cloudDetail, cloudBoil)
  // the aircraft's wake through the cloud (clouds.glsl wakeCarve): a path of points (xyz, the tunnel's radius there),
  // each segment's strength (0 where the path left the cloud layer) and the bounding sphere of it all
  static constexpr int kWakeMax = 20;
  float wake[kWakeMax][4] = {}, wakeK[kWakeMax] = {}, wakeB[4] = {0, 0, 0, 0}; int wakeN = 0;
  // the windscreen's rain and cloud (post_fs.glsl, the cockpit view): where on screen the airflow over the glass
  // streams from (uv, may lie far off screen; z +1 away from it, -1 towards it), how fast (w: 0 still .. 1 fast),
  // and the cloud's fine mist on the glass
  float rainFlow[4] = {0.5f, -1.f, 1.f, 0.f}; float glassMist = 0;
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
  std::string matDir;          // the scanned material layers (assets/materials; empty or missing: procedural ones)
  int matScanned = 0;          // how many of the layers came from it
  int dbgOff = 0;            // profiling: renderer features switched off (uDbg bits)
  // the analysis's cost probes, above the features' bits: one piece of the frame's work left out to time it (the picture
  // is wrong while one is on): the scenery's G-buffer draws, the terrain's, the objects pass's march, the player's
  // airframe mesh shaded flat (a build of its own, made when first asked for: drawPlaneMesh, PROBE_MESH_SHADE)
  static constexpr int kProbeScenery = 1 << 12, kProbeTerrain = 1 << 13, kProbeMarch = 1 << 14, kProbeMeshShade = 1 << 15;
  bool screenWindows = getenv("SCREENFEEDS") == nullptr;   // the research craft's displays are windows (no camera feeds but the bomb camera's; SCREENFEEDS=1 brings the cameras back)
  int bakeCount = 0;   // airframe meshes and hulls baked or loaded so far (the research terminal's warm-up waits for a frame that bakes nothing)
  int bakeBuilt = 0;   // of them, built from scratch (not loaded from the mesh cache): the diagnostics report it
  std::function<void()> bakeYield;   // called between the bake's evaluation batches (the benchmark answers the window's messages during a long bake)
  // a long bake shows a frame (bakeYield) about every 30 ms of real time: between its GPU bands and inside its long CPU
  // loops, so a screen drawn from the callback (the research terminal's boot sequence) never freezes
  std::chrono::steady_clock::time_point bakeYieldAt{};
  bool bakeDue() { auto now = std::chrono::steady_clock::now(); if (now - bakeYieldAt < std::chrono::milliseconds(30)) return false; bakeYieldAt = now; return true; }
  void bakeTick() { if (bakeYield && bakeDue()) { bakeYield(); hullBakeUploaded[0] = hullBakeUploaded[1] = false; } }
  bool ok = false;
  std::string error;
  GLuint minimapTex = 0;

  bool initUI(int w, int h);                     // UI program + font only (the intro screen)
  static constexpr int kProgramCount = 19;
  float terrainCeiling() const { return maxH; }   // highest point of the terrain (m)
  // analysis tool (--analyze, the harness's BENCHWALL): exact per-pass times (the GPU is waited on at every pass boundary)
  bool syncTiming = false; double passWall[11] = {};   // (kPasses)
  std::string dispError;   // set when the cockpit display shader failed to build (the screens stay dark)
  std::string proxyError;  // set when the marched shadow proxy failed to build (the airframes' shadows come from their maps alone)
  bool compilePrograms(std::atomic<int>* done);  // scene programs; safe on a worker thread with a shared context
  bool init(int w, int h);                       // everything else (runs compilePrograms itself if not done yet)
  GLuint makeTexture(const uint8_t* rgba, int w, int h);
  void renderMap(float cx, float cz, float half, int N);   // GPS aerial image into mapTex()
  GLuint mapTex() const { return texMap; }
  void resize(int w, int h);
  void setRenderScale(float s);   // the render resolution only: the TAA history stays at display resolution, no pop
  void renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites);
  mat4 viewProj(const FrameParams& fp, float zNear = 0.5f, float zFar = 90000.f) const;
  mat4 viewProjRel(const FrameParams& fp, float zNear, float zFar) const;   // the same from the camera at the origin
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
  // UI clipping (screen pixels, y down): what is drawn until uiClipOff() shows only inside the box
  void uiClip(float x0, float y0, float x1, float y1) { flushUI(); uiClipOn = true; uiClipBox[0] = x0; uiClipBox[1] = y0; uiClipBox[2] = x1; uiClipBox[3] = y1; }
  void uiClipOff() { flushUI(); uiClipOn = false; }
  bool screenshot(const char* path);
  bool screenshotPNG(const char* path);
  // the program being compiled or loaded now (compilePrograms, on its worker thread), for the loading screen
  std::string compileStage() { std::lock_guard<std::mutex> lk(stageMu); return stageName; }
  void setCompileStage(const char* s) { std::lock_guard<std::mutex> lk(stageMu); stageName = s; }
  std::mutex stageMu; std::string stageName;
  bool meshCached = false;
  std::function<void()> onBakeStart;   // called as an aircraft mesh begins to build (not read from the cache): the loading screen's wording   // this build's aircraft meshes are in the cache already (checkMeshCache: a launch reads them, it builds none)
  void checkMeshCache();
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
  // quarter-resolution clouds: the cloud march, its full-resolution composite, their targets
  GLuint progClouds = 0, progCloudComp = 0, texCloud = 0, texCloudD = 0, fboCloud = 0, texCloudMask = 0, fboComp = 0;
  // the clouds' own accumulation (kCloudAccFS, the main view's alone): this frame's cloud distance (the march's third
  // target) and two histories, (in-scatter, transmittance) and (cloud distance, scene depth)
  GLuint progCloudAcc = 0, texCloudT = 0, texCloudAcc[2] = {0, 0}, texCloudAccD[2] = {0, 0}, fboCloudAcc[2] = {0, 0};
  int cloudAccIdx = 0, cloudAccRw = 0, cloudAccRh = 0, cloudAccFrame = -10; bool cloudAccValid = false;
  int cw = 0, ch = 0;
  // baked terrain sun shadow (world space): front = the one the shaders read, back = the one being baked
  GLuint progTShBake = 0, texTSh[2] = {0, 0}, fboTSh = 0;
  int tshFront = -1, tshBack = 0, tshRow = 0; bool tshBaking = false; vec3 tshSun, tshBakeSun;
  static constexpr int kTShN = 2048, kTShRows = 64;   // texels per side, rows baked per frame
  void bakeTerrainShadow(const FrameParams& fp);            // the terrain sun-shadow bake, a band of rows a frame
  bool depthValid = false;   // the depth target holds a frame
  void renderDisplays(const FrameParams& fp, bool panel, int half = -1);   // half: the pages' even (0) or odd (1) half alone, -1 all
  GLuint progSprite = 0, progDown = 0, progUp = 0, progRayMask = 0, progRay = 0, progPost = 0, progUI = 0, progTAA = 0, progFeedRays = 0;
  static constexpr int kBloomMips = 6;
  GLuint fboMip[kBloomMips] = {}, texMip[kBloomMips] = {}; int mipW[kBloomMips] = {}, mipH[kBloomMips] = {};
  GLuint fboRay[2] = {0, 0}, texRay[2] = {0, 0};
  GLuint vaoEmpty = 0, vaoSprite = 0, vboSprite = 0, vaoUI = 0, vboUI = 0;
  GLuint texHM = 0, texAlb = 0, texNrm = 0, texFont = 0, texMask = 0, texRoadId = 0, texData = 0, texHMax = 0;
  struct V4 { float x, y, z, w; };
  GLuint fboScene = 0, texColor = 0, texDepth = 0, fboSprite = 0;
  // temporal AA: the lighting pass writes texRaw; the resolve blends it with the reprojected history into texHist[cur] + texColor
  GLuint texRaw = 0, texHist[2] = {0, 0}, fboTAA[2] = {0, 0};
  GLuint texTraffic = 0;
  std::array<TrafficVisual, kMaxTrafficDrawn> trafficUpload = {}; int trafficUploadN = -1;
  int histIdx = 0, frameNo = 0, histW = 0, histH = 0; bool histValid = false;   // (histW/H: the history textures' size)
  // GPU frame time from a ring of timer queries (read a few frames late so the CPU never waits on them)
  GLuint gpuQ[4] = {0, 0, 0, 0}; bool gpuQUsed[4] = {false, false, false, false}; int gpuQi = 0;
  uint64_t gpuQFrame[4] = {};
public:
  benchmark::GpuSample gpuSample;   // raw completed query, with freshness and originating renderScene serial
  uint64_t gpuFrameSerial = 0, gpuSamplesOverwritten = 0;
  float gpuMs = -1.f;   // last measured GPU time of renderScene, ms (-1 = not known yet)
  static constexpr int kPasses = 11;   // world | displays | feeds | objects | airframe shadow proxy | lighting+clouds+effects, then TAA, sprites, bloom, light shafts, composite
  float passMs[kPasses] = {};         // GPU time of each pass (timestamp queries, a few frames late)
  GLuint stampQ[4][kPasses + 1] = {}; bool stampUsed[4] = {};
  void stamp(int i) { if (glErrCheck) reportGLError(i); if (syncTiming) syncStamp(i); else if (stampQ[gpuQi][i]) glQueryCounter(stampQ[gpuQi][i], GL_TIMESTAMP); }
  bool glErrCheck = getenv("GLERR") != nullptr;   // debug: print any GL error raised by the passes before each timestamp
  void reportGLError(int stampIdx);
  void syncStamp(int i);
private:
  float prevFovY = 0.f;
  vec3 prevCamPos, prevPlanePos; float prevCamRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, prevPlaneRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  int rw = 0, rh = 0, bw = 0, bh = 0;
  float maxH = 2500;
  std::vector<UIVert> ui;
  GLuint curImg = 0;
  bool uiClipOn = false; float uiClipBox[4] = {0, 0, 0, 0};
  void flushUI();
  void createTargets();
  void createRenderTargets();
  void scaleDims();
  int allocW = 0, allocH = 0;   // the size the render resolution's targets were made at (rw x rh is its lower-left part)
  float jitX = 0, jitY = 0;
  void genMaterials();
  void genCloudNoise();   // tileable cloud coverage (2D) and billow / detail noise (3D) textures
  GLuint texCloudCov = 0, texNoise3 = 0;
  void genWaves();        // the sea's wave bands (water.glsl): three tileable slope / height maps of a wind-driven sea
  GLuint texWaves = 0; float waveRms[3] = {1, 1, 1};   // (each band's slope rms, the shader's decode scale)
  void genMinimap();
  // ---- environment entities: instanced meshes -> G-buffer (lit by the lighting pass) + sun shadow cascades
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
  GLuint fboEnv = 0, texEnv = 0, texEnvDepth = 0;   // the hull passes' target (createHullTarget)
  std::vector<float> envInst;
  void createHullTarget();
public:
  int envChunks = 0;
private:
  // ---- aircraft hull meshes (aircraft_hull.cpp): where each pixel's exact airframe march starts
  struct HullMesh { uint64_t key = 0; GLuint vbo = 0; int verts = 0; bool ok = false; };
  // the aircraft mesh (aircraft_mesh.cpp): the static part of the airframe baked from its field, and the hull of the
  // part that moves (the march's start on the raster path, where the mesh leaves off)
  struct PartMesh { int type = 0; GLuint vao = 0, vbo = 0, ibo = 0; int idx = 0; };   // a cockpit's rigid moving part, in its own frame (plane_parts.glsl)
  struct PlaneMesh { std::vector<PartMesh> parts; uint64_t key = 0; GLuint vao = 0, vbo = 0, ibo = 0; int idx = 0; bool ok = false; uint64_t movKey = 0; bool eyeInMov = false; };
  std::unordered_map<uint64_t, PlaneMesh> planeMeshes;
  GLuint progTrafficProps = 0;
  void rasterTrafficProps(const FrameParams& fp);
  GLuint progPlaneMesh = 0, progPlaneMeshDepth = 0;   // (the depth pre-pass: the airframe's inner and outer skins both face the camera; only the nearest is shaded)
  void setScreenCut(GLuint p, const FrameParams& fp, bool on);   // the research cockpits' windows cut (cabin_windows.glsl)
  bool compilePlaneMesh();
  bool planeMeshWanted(const FrameParams& fp) const;
  void bakePlaneMesh(const FrameParams& fp, int slot, uint64_t key);
  void drawPlaneMesh(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK, bool depthDone = false);
  void drawPlaneMeshDepth(const FrameParams& fp, const PlaneMesh& pm, const float* rot, const vec3& pos, int trafK);
  // the rigid parts' poses this frame (computePartPoses: kPartPoseFS into texPartPose, 4 texels an instance, from the
  // instance list in texPartInfo), for the player's aircraft (owner 0) and each traffic aircraft k (owner k + 1); read
  // by every part draw's vertex shader
  static constexpr int kMaxPoseInst = 512;
  struct PoseOwner { const PlaneMesh* pm = nullptr; int base = 0, n = 0; };
  PoseOwner poseOwner[1 + kMaxTrafficDrawn];
  std::vector<float> poseRevision;
  std::array<const PlaneMesh*, 1 + kMaxTrafficDrawn> poseMeshes = {};
  std::vector<int> poseType;   // each instance's part type
  GLuint progPartPose = 0, texPartPose = 0, texPartInfo = 0, fboPartPose = 0;
  void computePartPoses(const FrameParams& fp, const PlaneMesh* player, const PlaneMesh* const* traffic);
  void updatePartPoses(const FrameParams& fp);
  void drawPlaneParts(const PlaneMesh& pm, GLuint prog, int trafK);   // (the program bound, its uniforms set: each part instance at its pose)
  const PlaneMesh* earlyMesh = nullptr;   // the player's mesh whose depth opens this frame's G-buffer (rasterWorld; drawEntities draws it)
  uint64_t trafficModelKey(const float* t) const;   // hullKey(slot 0) of a traffic aircraft's model
  std::unordered_map<uint64_t, HullMesh> hulls;   // every airframe baked so far, outside and cockpit (keyed by hullKey)
  GLuint progHull = 0, progHullBake = 0, progHullBakeNormal = 0, vaoHull = 0, texHPts = 0, texHNormals = 0, texHOut = 0, fboHOut = 0, fboHull = 0, texHullDepth = 0;
  int hullDepthW = 0, hullDepthH = 0;
  bool hullOn = false;
  bool compileHull(const std::string& bakeVS, const std::string& bakeFS);
  void hullEval(const std::vector<vec3>& pts, std::vector<float>& out);
  // Bake inputs live on the CPU: both programs receive the same model, states and part selectors.
  // No selector is recovered from GL (the normal program can optimize some uniforms away).
  FrameParams hullBakeFrame;
  std::array<float, 512> hullBakePS{}, hullBakeCtl{}, hullBakeWr{}, hullBakeWr2{};
  int hullBakeStates = 0, hullBakeMode = 0, hullBakeState = 0, hullBakePart = -1;
  float hullBakeSideX = 1.f, hullBakeSideY = 1.f;
  bool hullBakeUploaded[2] = {false, false};
  void beginHullBake(const FrameParams& fp, int states, const float* ps, const float* ctl,
                     const float* wr = nullptr, const float* wr2 = nullptr);
  GLuint bindHullBake(bool restore = false);
  void hullEval4(const std::vector<vec3>& pts, std::vector<float>& out, const std::vector<float>* normals = nullptr);
  void hullEvalBatch(const vec3* pts, size_t n, float* out, const float* normals);
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
  // view (scenery G-buffer, objects, lighting, clouds) on a second set of render targets of its own size
  struct ViewTargets {
    int W = 0, H = 0, rw = 0, rh = 0, cw = 0, ch = 0, allocW = 0, allocH = 0;
    GLuint texRaw = 0, texDepth = 0, texCloudMask = 0, texCloud = 0, texCloudD = 0, fboCloud = 0, fboComp = 0, fboScene = 0;
    GLuint texGB[5] = {0, 0, 0, 0, 0}, texGBDepth = 0, fboGB = 0, fboShProxy = 0, texEnv = 0, texEnvDepth = 0, fboEnv = 0;
    bool depthValid = false, hullOn = false, trafHullOn = false;
    float jitX = 0, jitY = 0;
  };
  void swapView(ViewTargets& v);
  ViewTargets feedView;                // (allocated at the largest picture; each camera uses its corner of it)
  static constexpr int kFeedMaxW = 2048, kFeedMaxH = 1024, kFeedAtlasW = 4096, kFeedAtlasH = 3072;
  GLuint texFeed = 0, fboFeed = 0;
  int feedAtlasW = 0, feedAtlasH = 0;
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
  bool hullOff = getenv("HULLOFF") != nullptr;           // debug: no hulls (every aircraft march starts from the camera)
  bool meshOff = getenv("MESHOFF") != nullptr;           // debug: no aircraft meshes on the raster path (the whole airframe marches)
  bool tshPending() const { return tshBaking || tshFront < 0; }
  void resetTemporal() {   // forget every frame-to-frame accumulation (TAA history, jitter/seed sequence, terrain-shadow bake):
    frameNo = 0; histIdx = 0; histValid = false; cloudAccValid = false;   // the next frame renders as if it were the first (exact test comparisons)
    tshFront = -1; tshBack = 0; tshRow = 0; tshBaking = false;
    for (bool& v : shValid) v = false;   // shadow cascades re-render
  }
private:
  GLuint screenFbo = 0;
  void drawEntities(const FrameParams& fp);
  void createGBuffer();
  // ---- the passes of a frame (renderer.cpp)
  bool cloudSplit = false;   // this frame's clouds come from the quarter-resolution cloud pass
  const std::vector<SpriteVert>* curAlpha = nullptr; const std::vector<SpriteVert>* curAdd = nullptr;   // this frame's sprites
  void setRT(GLuint p, const FrameParams& fp);          // the scene's uniforms and textures for a program
  void cloudPass(const FrameParams& fp);                // the clouds at a quarter of the pixels, composited over the lit view
  void drawSprites(const FrameParams& fp, float texW, float texH, float uvsX, float uvsY);
  void feedEffects(const FrameParams& f);
  // ---- the raster renderer (raster_renderer.cpp, terrain_mesh.cpp)
  GLuint progLight = 0, progObjects = 0, progShProxy = 0, progEffects = 0, progTerrain = 0, progWater = 0;
  // The big airframe programs in two builds: [0] every aircraft, [1] AF_LIGHT - without the research jets' code
  // (plane_common.glsl RESEARCH_ON), so each pixel pays for less. pickAfPrograms points progObjects, progShProxy,
  // progEffects and progPlaneMesh at the light build whenever no research jet is in the frame.
  // The aircraft mesh pass draws each aircraft on its own, so it has two more builds: [2] AF_JET, the XR-30's alone,
  // and [3] AF_WRAITH, the XR-40's alone (drawPlaneMesh picks by the aircraft drawn)
  GLuint progObjectsV[2] = {}, progShProxyV[2] = {}, progEffectsV[2] = {}, progPlaneMeshV[4] = {}, progPlaneMeshProbe[4] = {};
  void pickAfPrograms(const FrameParams& fp);
  // the airframe shadow maps (raster_renderer.cpp): the player's baked static mesh rendered from the sun (layer 0,
  // orthographic) and from the three brightest shadow-casting lights (layers 1-3, perspective along each beam); a
  // second array marks where the moving hull is, so the proxy still marches the field there (the XR-30's nozzles, the
  // XR-40's). uShOn: bit 0 the sun, bits 1-3 the light slots. Layers 4 + k: traffic aircraft k's sun shadow from its
  // mesh (trafShOn bit k), so the proxy marches only the traffic that still has moving parts
  GLuint progShMap = 0, progShMov = 0, texShMap = 0, texShMov = 0, fboShMap = 0; int shOn = 0; mat4 shMapVP[4];
  // the cabin's own sun map in the cockpit view: 5 m about the eye at 2048 texels (2.4 mm), for the cabin's light and
  // shade - the whole airframe's map (layer 0, ~1.4 cm a texel) speckled the posts and frames a hand's width away
  GLuint texShCab = 0, fboShCab = 0; bool shCabOn = false; mat4 shCabVP, shCabVPc; float shCabBias = 0.f;
  // (kept in the aircraft's own frame and drawn again only when the sun has turned against it, the eye has moved, a
  // different cockpit is in use, or every 8th frame for the moving controls: shCabVP is this frame's world-space form)
  mat4 shCabBodyVP; vec3 shCabDir, shCabEye; uint64_t shCabKey = 0; int shCabAge = 1 << 20;
  static constexpr int kShCabRes = 2048;
  bool shMovOn = false;   // the player's maps carry a moving-hull mask this frame (the proxy marches the field there)
  GLuint progShProxyMaps = 0, progObjectsNoAf = 0;   // (progObjectsNoAf: the objects pass with only the UFO and the debris to march)
  bool proxyNeedsMarch(const FrameParams& fp) const;
  int trafShOn = 0; mat4 trafShVP[kMaxTrafficDrawn];
  static constexpr int kShMapRes = 1024, kShLayers = 4 + kMaxTrafficDrawn;
  void ensureShadowMaps();
  void rasterShadowMaps(const FrameParams& fp);
  void rasterTrafficShadowMaps(const FrameParams& fp);
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
