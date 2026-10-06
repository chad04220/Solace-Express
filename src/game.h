// Solace Express - game state, flight session, cameras, effects, UI
#pragma once
#include <functional>
#include <optional>
#include <set>
#include "common.h"
#include <unordered_map>
#include <future>
#include <map>
#include "world.h"
#include "aircraft.h"
#include "traffic.h"
#include "career.h"
#include "renderer.h"
#include "entities.h"
#include "audio.h"
#include "radio.h"
#include "atc.h"
#include "keys.h"

struct Input {
  bool down[256] = {}, pressed[256] = {};
  float mx = 0, my = 0, mdx = 0, mdy = 0, wheel = 0;
  bool mDown[3] = {}, mPressed[3] = {}, mReleased[3] = {};
  bool pad = false; float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0; unsigned buttons = 0, buttonsPressed = 0;
  void endFrame() { memset(pressed, 0, sizeof(pressed)); for (int i = 0; i < 3; i++) mPressed[i] = mReleased[i] = false; wheel = 0; mdx = mdy = 0; buttonsPressed = 0; }
};

// Rebindable flight actions. Each has one keyboard key and one gamepad button; the sticks and triggers stay fixed
// (left stick pitch/roll, triggers throttle) and the arrow keys / PgUp / PgDn / Home / End stay as fixed alternates.
enum Action {
  ACT_PITCH_DN = 0, ACT_PITCH_UP, ACT_ROLL_L, ACT_ROLL_R, ACT_YAW_L, ACT_YAW_R, ACT_THR_UP, ACT_THR_DN, ACT_TRIM_UP, ACT_TRIM_DN,
  ACT_FLAPS_DN, ACT_FLAPS_UP, ACT_GEAR, ACT_BRAKE, ACT_PARK, ACT_AP, ACT_STUNT, ACT_LIGHTS, ACT_ENGINE, ACT_TIME,
  ACT_CAMERA, ACT_HUD, ACT_MAP, ACT_MINIMAP, ACT_RADIO, ACT_ANR, ACT_ZOOM,
  ACT_CLOAK, ACT_WEAPONS, ACT_FIRE, ACT_BOMB, ACT_COUNT
};
struct ActionInfo { const char* id; const char* name; int group; int key; unsigned pad; };
extern const ActionInfo kActions[ACT_COUNT];
extern const char* const kActionGroups[];
std::string keyName(int vk);
std::string padName(unsigned bit);

struct Settings {
  int quality = 1;   // (the render scale is the renderer's own: Renderer::renderScale)
  float master = 0.8f, engineVol = 1.0f, sfxVol = 0.9f, radioVol = 0.6f, atcVol = 0.9f;
  bool invertPitch = false, showHints = true, metric = false, fullscreen = false, traffic = true;
  int radioStation = 0;
  float mouseSens = 1.0f;
  int resMode = 1;   // 0 native, 1 auto (holds the frame-rate target, the default), 2 85%, 3 75%, 4 67% (TAA upscales to the display)
  int fpsTarget = 60;   // the frame-rate cap (30 / 60 / 90 / 120 / 144 / 240); 0: the monitor's refresh rate (vsync). 60 by default: a 240 Hz screen would otherwise ask for 240 fps
  int renderer = 1;    // 1 the raster renderer (the default, docs/RENDERER_REBUILD.md), 0 the ray tracer
  float fov = 55;      // the outside views' vertical field of view (degrees); the cockpit's is 19 wider
  bool headLook = true;   // the cockpit view leans into turns when nothing else moves it
  bool cbHud = false;     // colour-blind palette: good / bad as blue / orange instead of green / red
  float uiScale = 1.f;    // on top of the window-height scale
  bool hudCam[4] = {true, true, true, true};   // the HUD on or off, remembered per camera
  int keyBind[ACT_COUNT]; unsigned padBind[ACT_COUNT];
  Settings() { resetBindings(); }
  void resetBindings() { for (int i = 0; i < ACT_COUNT; i++) { keyBind[i] = kActions[i].key; padBind[i] = kActions[i].pad; } }
};

struct TipPt { vec3 p; float age, a; int seg; };   // wingtip vapour ribbon point (in the air mass)
struct Particle { vec3 p, v; float life, maxLife, size, grow; vec3 col; float alpha; int kind; float drag, buoy; bool instant = false; bool fresh = true; };  // instant: no fade-in (trails)

enum GameScreen { SCR_MENU = 0, SCR_HUB, SCR_FLIGHT, SCR_DEBRIEF, SCR_RESEARCH, SCR_LOADING };
enum HubTab { TAB_CONTRACTS = 0, TAB_HANGAR, TAB_AIRLINE, TAB_LOGBOOK, TAB_SETTINGS };

class Game {
  friend struct GameTest;
public:
  bool botControl = false;  // tests drive plane.ctl directly
  Input in;
  bool quit = false;
  // launch: while the intro still shows, render the menu tour's first place offscreen until its scenery and shadows
  // are in, and every light aircraft's hull is built (progress: fraction 0..1, what is being done)
  void prewarm(const std::function<void(float, const std::string&)>& progress);
  int prewarmCraft = -1; bool prewarmInside = false;   // the menu tour shows this aircraft instead (prewarm only)
  bool wantFullscreenToggle = false;
  int monitorHz = 60;            // the display's refresh rate (the platform layer sets it)
  bool wantPacing = false;       // the frame-rate target changed: the platform layer re-applies its pacing
  // the pre-rendered menu montage (Windows: menu_video_win.cpp): returns the texture to show at this time, or 0 to
  // ray trace the montage live; and sceneOnly, set while recording it (the scene without the menu on top)
  std::function<unsigned(float)> menuVideo;
  bool sceneOnly = false;
  void focusLost() { if (screen == SCR_FLIGHT && !crashed) paused = true; }   // the window lost focus: a flight pauses
  Settings set;
  std::string saveDir;
  std::string assetDir = ".";                          // folder of the exe (pre-rendered loading pictures live in assetDir\loading)

  void loadSettings();
  void applyUiPalette();   // the HUD / UI good-bad colours from the settings (game_ui.cpp)
  // startup screen, drawn with the given renderer (the intro thread has its own, in its own GL context)
  void drawIntro(float progress, const std::string& stage, float t, unsigned icon, float fade, Renderer& R = g_ren);
  bool shaderFirstRun = false;
  unsigned iconTex = 0;   // the application icon (intro screen, main menu)
  void init(bool buildWorld = true);   // buildWorld false: g_world.build() already ran (on the intro's worker thread)
  void initHeadless();
  void debugScene(const std::string& name);
  void update(float dt);
  void render();
  void shutdown();

private:
  // ---------------------------------------------------------------- state
  Career career;
  // Every change to the career is a transaction: the change is made on a copy, the copy is saved, and only a saved
  // copy becomes the career. A failed save leaves the career as it was and keeps the copy as pending, to be retried
  // (on the debrief, on entering the hub); meanwhile nothing else may commit (Accept / Buy / Sell are disabled).
  std::optional<Career> pendingCareer;
  bool commit(const std::function<void(Career&)>& change);   // true: saved (or no save wanted)
  bool retryCommit();
  bool commitBlocked() const { return pendingCareer.has_value(); }
  float retryT = 0;
  std::string saveWhy;      // why the last commit could not be saved
  void beginCareerFlight(const Contract& c, int spec, Career::Source src);   // marks the attempt open in the save, accepts the job, then startFlight
  void continueJob(int spec, Career::Source src);    // the next leg of the open job, from where its load is
  void releaseJob();                                 // the open job cancelled: the load stays where it is, nothing charged
  void practiseApproach(int spec, Career::Source src);   // a flight to the job's destination that touches nothing in the career
  bool isolatedFlight = false;   // a practice flight: endFlight returns to the hub without any settlement
  float launchFuelKg = -1;       // the fuel chosen on the job card for the next flight (-1: the plan's default)
  float chosenFuel(const Contract& c, int spec, Career::Source src, const Career::LaunchPlan& p) const;   // what the tanks hold at take-off
  float jobClockBase = 0;        // seconds already on the job's clock from earlier legs (deadlines count from it)
  // C7: one failure may be rolled for the flight (from the aircraft's condition), scheduled at a clock time; the gear
  // kind waits for the next gear command instead. Tests and tools fly with the roll disarmed and fire failures themselves.
  struct FailPlan { int kind = 0, engine = 0; float at = -1; bool fired = false; } failPlan;
  bool failuresArmed = true;
  // C12 trials: flights against the clock or the tape, off the books, with a local leaderboard per trial
  // (settings.cfg: trial.<id>.<rank> score). Spot landing and the STOL contest score the touchdown; the gate runs
  // score the time from the first gate to the last; the daily course is the gate run seeded by the date.
  enum TrialKind { TR_SPOT = 0, TR_STOL, TR_GATES, TR_DAILY, TR_FORMATION, TR_COUNT };
  Contract trialContract(int kind) const;
  std::map<std::string, std::vector<float>> trialBest;
  float trialT0 = -1, trialT1 = -1;
  int hubList = 0;   // the contracts tab's card list: 0 work, 1 trials
  void finishTrial(bool success);
  static const char* trialName(int kind) { static const char* n[] = {"Spot landing", "STOL contest", "Gate run", "Daily gate course", "Formation run"}; return n[kind]; }
  static const char* trialId(int kind) { static const char* n[] = {"T_SPOT", "T_STOL", "T_GATES", "T_DAILY", "T_FORM"}; return n[kind]; }
  static const float kFormationRunS;   // the formation run: seconds with the pair alongside
  float formT = 0, formLost = 0; bool formDone = false;   // the formation run: time with the pair, time spent out of a steady platform
  std::string trialScore(int kind, float v) const;   // "14 m from the mark", "212 m roll", "1:42"
  // C6 job meters: the patient / VIP comfort, the survey band time, the low-vis minimums check
  float surveyT = 0, surveyInT = 0; bool minimumsChecked = false; int minimumsGoArounds = 0;
  void updateJobMeters(float dt, float gs);
  void rollFailures(const Contract& c, int spec, Career::Source src);
  void updateFailures(float dt);
  void fireFailure(int kind, int engine);   // breaks it now, with the warning and the autopilot's reaction
  int attemptFrom = 0;           // where this attempt departed (an abandoned leg brings the load back there)
  GameScreen screen = SCR_MENU;
  int hubTab = TAB_CONTRACTS;
  float gameTime = 0, realTime = 0;
  bool hasSave = false;
  bool confirmRes = false;
  bool resWarm = false; int resWarmFrames = 0, resBakeSeen = 0;   // the research terminal warming up behind its boot screen (the craft's shells, the airport's scenery)
  std::function<void()> platformPresent;   // swaps the window's buffers and answers its messages (platform_win32): a frame shown from inside a long bake   // the main menu asked whether to enter the research terminal before the campaign is done
  bool headless = false;

  // flight session
  Plane plane;
  Contract contract;
  int specIdx = 0; Career::Source source = Career::SRC_NONE;
  Weather wx;
  Weather wxStart;   // C8: the flight's weather drifts from the contract's wx to its wxEnd (updateWeather)
  float apRepickT = 0;
  void updateWeather(float dt);
  float timeOfDay = 12;
  int wpIndex = 0;
  float flightClock = 0, crashTimer = 0, endTimer = 0;
  bool paused = false, showMap = false, showRadio = false, hudOn = true, showMinimap = false;
  bool landed = false, completed = false, crashed = false;
  FlightResult result;
  float fuelStart = 0;
  float timeAccel = 1;
  int camMode = 0; float camYaw = 0, camPitch = 0.12f, camDist = 0, camZoom = 1;
  float camArm = 0, camArmV = 0, camSpd = 0;   // chase camera spring arm: length (m), its rate, smoothed airspeed
  float lookYaw = 0, lookPitch = 0;
  quat camQ; vec3 camPos; vec3 camVel;
  float propAngle = 0;
  bool landingLight = true;
  float flapNotch = 0;
  int phase = 0, lastHintPhase = -1;
  std::string hint;
  float lightning = 0, nextLightning = 5, thunderDelay = -1;
  float prevGear = 1, prevFlaps = 0;
  bool takeoffAnnounced = false;
  float touchdownFpm = 0; bool touchedDown = false;
  int runwayReverse = 0;
  float stillTimer = 0;
  bool engineAutoStarted = false;
  float startDelay = 0;
  vec2 cloudOff;
  int diversion = -1;
  bool apWasOn = false;
  int stuntNext = 0;            // the next aerobatic figure the aerobatics key flies (Plane::Stunt)
  int apDest = -1;              // airport picked on the GPS for the autopilot to fly to and land at
  bool apCruising() const;
  void engageAutopilot();
  void cycleApDest(int dir);
  float approachMinAgl = 1e9f;
  float thrPrevAlong = -1e9f; bool appLow = false, appHigh = false;   // arrival recording (updateFlight): threshold crossing, go-arounds
  bool parkingBrake = true;
  std::string coaching;   // the debrief's one coaching point (endFlight)

  // effects
  std::vector<Particle> particles;
  // crash wreckage: rigid pieces of the airframe, small debris chunks and the impact crater
  struct WreckPiece { vec3 c, v, w; quat q; vec3 C, H; bool rest; float fire; bool landed = false; };
  struct Debris { vec3 p, v, w; quat q; float size; bool charred, rest; float burn = 0; };   // burn: seconds it trails fire
  struct Pop { vec3 p, v; float t, R; int piece; };   // a delayed secondary explosion (on a wreck piece when piece >= 0)
  std::vector<Pop> pops;
  std::vector<TipPt> pieceTrail[5];   // smoke trail of each falling break-up piece
  float boomT = -1, boomI = 0; vec3 boomP;           // the flash of the latest explosion lights the scene
  std::vector<WreckPiece> wreck;
  std::vector<Debris> debris;
  float craterX = 0, craterZ = 0, craterR = 0, craterD = 0;
  void breakUp(vec3 impactVel, bool water, bool air = false);
  bool airBreak = false;
  float gTunnel = 0;
  Traffic traffic;
  bool dbgCam = false, dbgFollow = false; vec3 dbgCamPos, dbgCamLook, dbgFollowOff;
  std::unordered_map<std::string, unsigned> loadImg;   // loading pictures by name (0: none on disk)
  // UFO encounter: pulls up alongside, opens its hatch on two dancing aliens, laughs, waves and zooms off
  struct Ufo { bool on = false; float t = 0, next = 0, side = 1, hatch = 0, laugh = 0, wave = 0; vec3 pos, fwd, right, up; bool sfxLaugh = false, sfxZoom = false; };
  float escortSummon = 0; bool escortLatch = false;   // O + P held: the Spectre display pair
  // XR-40 Wraith systems: cloak, retracting laser turrets, bomb bay and the dark-energy weapons in the world
  struct WraithState {
    bool cloakOn = false; float stealth = 0, front = -12.f, padATap = 9.f;        // cloak: strength 0..1 and the wavefront along the craft (body z)
    bool armed = false; float lasers = 0;                          // turrets deployed 0..1
    bool fireLatch = true;   // a click / Enter still held from the launch menu doesn't fire (or arm) until released
    float laserCD = 0, laserGlow = 0; int laserSide = 0;
    float bay = 0, bayHold = 0, bombLoaded = 1; int bombQueue = 0;  // bomb bay doors, bomb in the cradle 0..1, pending drops
    struct Bolt { vec3 h, v, d; float len, age, life; bool hit; };   // head, world velocity, aim; streak length behind the head
    bool wantFire = false;
    struct Bomb { vec3 p, v; float t; };
    struct Blast { vec3 p; float R, age, dur; bool water; };
    struct Crater { float x, z, R, D; };
    std::vector<Bolt> bolts; std::vector<Bomb> bombs; std::vector<Blast> blasts; std::vector<Crater> craters;
    struct BombCam { bool on = false; int phase = 0; vec3 pos, look, blastP; float t = 0, orbit = 0, tanHalf = 0.3f; } cam;   // spawned bomb camera (floor screen)
    std::vector<Crater> scorch;   // small laser craters (most recent 16)
    int wrecked = 0;              // trees, rocks and buildings destroyed
    int kills = 0;
    int shots = 0, dropped = 0;   // laser bolts fired, bombs released (the test cards count them)
  } wraith;
  void wraithControls(float dt);
  void updateWraith(float dt);
  void wraithVisual(FrameParams& fp);
  void buildFeedCameras(FrameParams& fp);
  void buildLights(FrameParams& fp);
  void fireLaser();
  void updateBolts(float dt);
  void laserImpact(vec3 at, int craft, int entKind, const Ent* ent);
  void addScorch(float x, float z, float R, float D);
  void detonate(vec3 p, bool water);
  void fireball(vec3 c, vec3 baseV, float R, bool air, bool water);
  void updateBombCam(float dt);
  void updateLoading(float dt);
  void drawLoading();
  float ufoSummon = 0;          // J + K held while flying summons the UFO after a second
  Ufo ufo;
  void startUfo();
  void updateUfo(float dt);   // debug scenes: free camera               // AI aircraft: airport circuits, cruisers, XR-30 formations, display team
  float fpsAvg = 1.f / 60.f; bool showPerf = false;   // F3: frame-rate / GPU time / resolution overlay              // smoothed g-force screen-edge effect 0..1          // broke up in flight: pieces tumble down before anything hits the ground
  float crashEndT = 7.5f;         // crashTimer at which the results screen comes up
  void updateWreck(float dt);
  float wreckGround(float x, float z) const;
  struct RingBurst { vec3 c, ax, ay, col; float t; };
  std::vector<RingBurst> bursts;   // checkpoint shockwaves
  float sparkAccum = 0;
  Rng sparkRng{77};
  bool ringGeom(int i, vec3& c, vec3& ax, vec3& ay) const;
  std::vector<vec3> rainDrops;
  float dustAccum = 0;

  // debrief
  std::vector<PayoutLine> payout; int stars = 0; bool lastSuccess = false; std::string debriefTitle;
  int licenseBefore = 0;

  // hub selections
  int selContract = 0, selAircraft = -1, selHangar = 0, freeDest = 0;
  bool freeFlightCard = false;
  std::string hubMsg; float hubMsgTime = 0;

  // toasts
  struct Toast { std::string text; float t; vec3 col; };
  std::vector<Toast> toasts;

  Radio radio;
  // tower controller: the voices (atc.h) and where this flight is in its exchange with the towers (updateAtc)
  AtcVoice atc;
  struct AtcFlight {
    int phase = 0;      // 0 greeting, 1 takeoff clearance, 2 handoff, 3 en route, 4 approach issued, 5 cleared to land, 6 done
    float t = 0, waitT = 0;
    int dep = -1, arr = -1;
    bool depRev = false, arrRev = false, airborne = false;
    int spoken = 0;   // tower transmissions heard so far
    bool called[2] = {};   // departure / arrival tower: a call with the full registration has been heard (then abbreviated)
    bool trafficSaid = false;   // the current wait for traffic (hold for an arrival, runway occupied) has been called
    float trafficT = 0;         // how long that wait has lasted
    // the latest tower instruction, for the HUD's recall line: its text, airport, when it was said, and whether it
    // still stands (a go-around voids a landing clearance; it is never replayed or re-issued from here)
    std::string lastCall; int lastApt = -1; float lastT = 0; bool lastValid = true;
    // the instructions compliance is scored on: a hold (where the aircraft was told to wait) and a go-around
    bool holding = false; vec3 holdPos; bool goAround = false;
    bool lastBeforePause = false;   // the recall line was said before a pause: labelled so
  } atcF;
  int atcKey() const { return atcF.phase * 4 + (atcF.depRev ? 1 : 0) + (atcF.arrRev ? 2 : 0); }   // E6: a tower call is valid while this is what it was made for
  void updateAtc(float dt);
  // every in-flight message the voices may say: the toasts, the lesson hints (with the lesson's id), the warnings
  struct CommsMsg { std::string text, mission; };
  std::vector<CommsMsg> commsPending;
  bool commsCrashSeen = false;
  std::vector<std::string> hintsVoiced;   // the lesson hints said this flight
  bool warnWas[4] = {}; float warnLastT[4] = {-99, -99, -99, -99};   // stall, pull up, gear, engine off: rising edges
  // a failure annunciator (C7): the HUD draws text, comms speak it when its state (slot -> key) comes on or changes
  // severity, never for the numbers alone (battery and ice percentages move every frame)
  struct Annunciator { std::string text, slot, key; bool bad; };
  std::vector<Annunciator> hudAnnunciators() const;
  std::map<std::string, std::string> failVoiced;   // slot -> the state key last spoken this flight
  void updateComms(float dt);
  float edgeWarnT = 0;   // chart-edge warning repeat
  float voiceDuck = 0;   // 0..1: music and engine lowered while someone is talking
  std::string settingsWritten;   // the settings file's last written contents (written again only when changed)
  int atcStation(int airport) const { return (airport * 7 + 2) % AtcVoice::kVoices; }   // each airport keeps one voice
  std::vector<std::pair<std::string, std::string>> stations;
  bool settingsFromPause = false;
  int settingsPage = 0;                    // 0 general, 1 controls
  int bindCapture = -1, bindCaptureDev = 0; // action waiting for a key (dev 0) or gamepad button (dev 1)
  float bindCaptureT = 0;
  std::vector<TipPt> tipTrail[2]; int tipSeg = 0; bool tipOn = false;
  int ctlScroll = 0; float ctlScrollAcc = 0;
  float ckZoom = 1.f, ckZoomT = 1.f;
  float autoScale = 0.75f, autoScaleT = 0;   // (starts at three quarters: the upscaler makes it hard to tell, and the first frames are the slow ones)   // dynamic resolution state   // cockpit view zoom (current, target)
  float loadT = 0, loadReadyT = -1, loadShown = 0; int loadPend0 = 0; bool loadMap = false;   // pre-flight loading screen
  bool gpsMapValid = false; vec2 gpsMapC; float gpsMapHalf = 0; int gpsMapN = 0;   // cached GPS aerial image
  bool uiHidden = false, bumperFired = false; float bumperHold = 0;   // LB + RB held 1 s: hide / show the flight UI
  // bound action state: keyboard key or gamepad button
  std::string expandHint(const std::string& raw) const;   // {actionId} tokens -> the bound keys
  // input contexts: who gets the input this frame, in priority order (a key-binding capture, a dialog, the pause
  // menu, a menu screen, a flight overlay - GPS or radio - then the flight). An action works only in the contexts in
  // kActionCtx; on a change of context every held key and button is ignored until it's released (no A held through
  // the loading screen braking, no Resume press firing the lasers)
  enum InputCtx { CTX_BIND, CTX_DIALOG, CTX_PAUSE, CTX_SCREEN, CTX_OVERLAY, CTX_FLIGHT };
  InputCtx ctx = CTX_SCREEN, lastCtx = CTX_SCREEN;
  bool keyUnarmed[256] = {}; unsigned padUnarmed = 0; bool padWas = false;
  InputCtx inputContext() const;
  static unsigned actionCtxMask(int a);
  void armInputs();
  bool actOk(int a) const;
  bool actKey(int a) const { int k = set.keyBind[a]; return actOk(a) && k > 0 && k < 256 && in.down[k]; }
  bool actKeyP(int a) const { int k = set.keyBind[a]; return actOk(a) && k > 0 && k < 256 && in.pressed[k]; }
  bool actPad(int a) const { return actOk(a) && in.pad && (in.buttons & set.padBind[a]); }
  bool actPadP(int a) const { return actOk(a) && in.pad && (in.buttonsPressed & set.padBind[a]); }
  bool actDown(int a) const { return actKey(a) || actPad(a); }
  bool actPressed(int a) const { return actKeyP(a) || actPadP(a); }
  bool confirmNew = false;

  // ---------------------------------------------------------------- helpers
  void saveSettings();
  void loadStations();
  int radioScroll = 0;
  void saveGame();
  void toast(const std::string& s, vec3 col = vec3(1, 1, 1), bool voiced = true);   // voiced: a voice line says it, if the packs have one
  void startFlight(const Contract& c, int spec, Career::Source src);
  void endFlight(bool success, const std::string& reason, FlightOutcome outcome = OUT_CRASHED);
  void updateFlight(float dt);
  void flightControls(float dt);
  void updateCamera(float dt);
  void updateParticles(float dt);
  void spawn(vec3 p, vec3 v, float life, float size, float grow, vec3 col, float alpha, int kind, float drag = 1.f, float buoy = 0.f);
  void computeSun(float tod, vec3& dir, vec3& col, float& night) const;
  FrameParams buildFrame();
  void buildSprites(const FrameParams& fp, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add);
  void feedAudio();
  void menuBackgroundCamera(FrameParams& fp);
  void menuTour(FrameParams& fp);   // main menu: a tour of the islands
  int computePhase() const;
  std::string landingCoaching() const;
  const Airport& dest() const { return g_world.airports[contract.to]; }

  // UI (game_ui.cpp)
  float S() const;  // UI scale
  bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, bool highlight = false);
  bool hovered(float x, float y, float w, float h) const;
  // E5.4: keyboard / D-pad focus through the menus. Every enabled button registers itself for the frame; the arrow
  // keys (outside flight) and the D-pad move the focus to the nearest button in that direction, Enter / Space / A
  // press it. Moving the mouse hands control back to the cursor.
  struct Focusable { uint32_t id; float x, y, w, h; };
  std::vector<Focusable> focusList, focusPrev;
  uint32_t focusId = 0; bool focusNav = false; int focusScreen = -1;
  void focusNavigate();
  void panel(float x, float y, float w, float h, float a = 0.78f);
  void hudPanel(float x, float y, float w, float h, float a = 1.f);
  void header(float x, float y, float w, const std::string& label);
  void card(float x, float y, float w, float h, bool sel, bool hov, vec3 accent);
  bool uiGlass = false;   // the career hub's style: translucent glass panels and cards over the live scene
  float anim(uint32_t id, float target, float rate);
  std::unordered_map<uint32_t, float> uiAnim;
  float uiDt = 0.016f, uiLastT = 0;
  // GPS moving map
  float gpsRange = 12000.f, gpsRangeTarget = 12000.f;
  std::vector<vec2> trail; float trailT = 0;
  void drawGps();
  // hidden Confidential Research Model menu (U + I on the main menu) and free XR-30 flights
  bool researchFlight = false;
  float lastDt = 1.f / 60.f;     // the last frame's real time step (the renderer's frame-rate independent blending)
  float maxFrameMs = 0, maxFrameWin = 0, maxFrameT = 0;   // the worst frame time over the last second (F3)
  int effectiveHz() const { return set.fpsTarget > 0 ? set.fpsTarget : std::max(monitorHz, 30); }
  Career::LaunchPlan launchPlan;   // the job's launch plan, made at startFlight: settle() charges its fees
  // quotes: each job flown headless on the autopilot in the background, one at a time (simulateFlightMinutes); the
  // card shows the quick estimate until the flown time is in
  std::map<std::string, std::pair<float, float>> quoteFlown;   // job|aircraft -> minutes, fuel kg (minutes < 0: didn't get there)
  std::future<std::pair<float, float>> quoteJob; std::string quoteJobKey;
  void applyQuote(const Contract& c, Career::LaunchPlan& e, bool start);
  int resAirport = 0, resWx = 0, resCraft = kResearchJet; bool resAirborne = true; float resTime = 12.f, resOpened = 0;
  // research test cards: each asks a craft for its limits or exercises one of its systems, step by step. The table is
  // in game_research_ui.cpp; the flight tracks the current step here and a finished card is remembered in settings.cfg.
  enum ResStepKind { RS_MACH = 0, RS_ALT, RS_G, RS_NEGG, RS_ROLL, RS_SLOW, RS_STALL, RS_CLIMB, RS_HOVER, RS_VLAND, RS_CLOAK, RS_LASER, RS_BOMB, RS_LAND };
  struct ResStep { int kind; float v, hold; const char* label; };
  struct ResCard { int craft; const char* id; const char* title; const char* brief; ResStep steps[5]; int n; };
  static const ResCard kResCards[]; static const int kNumResCards;
  int resCard = -1, resStep = 0; float resHold = 0, resBest = 0; bool resCardDone = false;
  std::set<std::string> resDone;   // finished card ids
  void updateResearchCard(float dt);
  std::string resStepText(const ResStep& st) const;   // "MACH 2.70 x 10 s" and the like
  bool resAuthed = false, resDrag = false;   // biometric sequence passed this session; dragging the preview
  float resYaw = 0.7f, resPitch = 0.15f, resZoom = 1.f, resSelT = -10.f, resIdleT = 10.f; int resLastCraft = -1;   // preview orbit, selection time
  vec3 resPrevPos; quat resPrevQ;   // where the preview craft is (for the callouts)
  float prevMach = 0, prevAB = 0;
  // gamepad-driven menu cursor (left stick moves, A clicks, B backs out)
  float padCursorT = -100.f; bool padHoldA = false;
  void gamepadMenus(float dt);
  void drawPadCursor();
  void drawResearch(const FrameParams& fp);   // game_research_ui.cpp
  struct ResLayout { float lx, lw, rx, rw, top, bot, px0, px1, cx, cy, r; };   // the terminal's columns and preview ring
  ResLayout researchLayout() const;
  void researchPreviewCamera(FrameParams& fp);
  void launchResearch();
  void jetEffects(float dt);
  void drawMenu();
  void drawHub();
  void drawHubContracts(float x, float y, float w, float h);
  void drawHubHangar(float x, float y, float w, float h);
  void drawHubLogbook(float x, float y, float w, float h);
  void drawHubAirline(float x, float y, float w, float h);
  int airSelPlane = 0, airSelDest = 0, airSelPilot = 0;   // the airline tab's route set-up
  float airlineTrafficT = 0;
  void airlineTraffic(float dt);   // your routes' scheduled flights appear as traffic along their lines
  void drawSettings(float x, float y, float w, float h);
  void drawControls(float x, float y, float w, float h);
  void updateBindCapture(float dt);
  void drawHud(const FrameParams& fp);
  void hudStrip(float x, float y, float w, float h, float a = 1.f);
  void hudWarnIcon(int kind, float cx, float cy, float sz, vec3 c, float a);
  float hudPrevIas = 0, hudTrend = 0;   // the speed tape's trend vector
  bool hudDemo = false;                 // harness: every warning lit (scene hud with HUDDEMO=1)
  float hudMsgNext = 0;                 // where the right message column continues under the annunciators
  void drawPFD(float x, float y, float size);
  void drawMinimap(float x, float y, float size, float rangeM);
  void drawMapOverlay();
  void drawRadioPanel(float x, float y);
  void drawPause();
  void drawDebrief();
  void drawToasts();
  void drawMapView(float x, float y, float w, float h, int from, int to, const std::vector<Waypoint>* wps);
  std::string fmtMoney(int m) const;
  std::string fmtSpeed(float ms) const;
  std::string fmtAlt(float m) const;
};
