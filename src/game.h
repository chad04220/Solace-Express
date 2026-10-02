// Air Xpress - game state, flight session, cameras, effects, UI
#pragma once
#include "common.h"
#include <unordered_map>
#include "world.h"
#include "aircraft.h"
#include "traffic.h"
#include "career.h"
#include "renderer.h"
#include "audio.h"
#include "radio.h"

// Virtual key codes (Windows VK values)
enum {
  K_BACK = 0x08, K_TAB = 0x09, K_ENTER = 0x0D, K_SHIFT = 0x10, K_CTRL = 0x11, K_ESC = 0x1B, K_SPACE = 0x20,
  K_PGUP = 0x21, K_PGDN = 0x22, K_END = 0x23, K_HOME = 0x24, K_LEFT = 0x25, K_UP = 0x26, K_RIGHT = 0x27, K_DOWN = 0x28,
  K_F1 = 0x70, K_F11 = 0x7A, K_F12 = 0x7B, K_LBRACKET = 0xDB, K_RBRACKET = 0xDD, K_PLUS = 0xBB, K_MINUS = 0xBD
};
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_BACK = 64, PAD_START = 128, PAD_UP = 256, PAD_DOWN = 512, PAD_LEFT = 1024, PAD_RIGHT = 2048, PAD_LS = 4096, PAD_RS = 8192 };

struct Input {
  bool down[256] = {}, pressed[256] = {};
  float mx = 0, my = 0, mdx = 0, mdy = 0, wheel = 0;
  bool mDown[3] = {}, mPressed[3] = {}, mReleased[3] = {};
  bool pad = false; float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0; unsigned buttons = 0, buttonsPressed = 0;
  void endFrame() { memset(pressed, 0, sizeof(pressed)); for (int i = 0; i < 3; i++) mPressed[i] = mReleased[i] = false; wheel = 0; mdx = mdy = 0; buttonsPressed = 0; }
};

struct Settings {
  float renderScale = 1.0f; int quality = 1;   // renderScale: the most the dynamic resolution may use
  float master = 0.8f, engineVol = 1.0f, sfxVol = 0.9f, radioVol = 0.6f;
  bool invertPitch = false, showHints = true, metric = false, fullscreen = false, traffic = true;
  int radioStation = 0;
  float mouseSens = 1.0f;
};

struct Particle { vec3 p, v; float life, maxLife, size, grow; vec3 col; float alpha; int kind; float drag, buoy; bool instant = false; };  // instant: no fade-in (trails)

enum GameScreen { SCR_MENU = 0, SCR_HUB, SCR_FLIGHT, SCR_DEBRIEF, SCR_RESEARCH };
enum HubTab { TAB_CONTRACTS = 0, TAB_HANGAR, TAB_LOGBOOK, TAB_SETTINGS };

class Game {
  friend struct GameTest;
public:
  bool botControl = false;  // tests drive plane.ctl directly
  Input in;
  bool quit = false;
  bool wantFullscreenToggle = false;
  Settings set;
  std::string saveDir;

  void init();
  void initHeadless();
  void debugScene(const std::string& name);
  void update(float dt);
  void render();
  void shutdown();

private:
  // ---------------------------------------------------------------- state
  Career career;
  GameScreen screen = SCR_MENU;
  int hubTab = TAB_CONTRACTS;
  float gameTime = 0, realTime = 0;
  bool hasSave = false;
  bool headless = false;

  // flight session
  Plane plane;
  Contract contract;
  int specIdx = 0; Career::Source source = Career::SRC_NONE;
  Weather wx;
  float timeOfDay = 12;
  int wpIndex = 0;
  float flightClock = 0, crashTimer = 0, endTimer = 0;
  bool paused = false, showMap = false, showRadio = false, hudOn = true, showMinimap = false;
  bool landed = false, completed = false, crashed = false;
  FlightResult result;
  float fuelStart = 0;
  float timeAccel = 1;
  int camMode = 0; float camYaw = 0, camPitch = 0.12f, camDist = 0, camZoom = 1;
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
  int apDest = -1;              // airport picked on the GPS for the autopilot to fly to and land at
  bool apCruising() const;
  void engageAutopilot();
  void cycleApDest(int dir);
  float approachMinAgl = 1e9f;

  // effects
  std::vector<Particle> particles;
  // crash wreckage: rigid pieces of the airframe, small debris chunks and the impact crater
  struct WreckPiece { vec3 c, v, w; quat q; vec3 C, H; bool rest; float fire; bool landed = false; };
  struct Debris { vec3 p, v, w; quat q; float size; bool charred, rest; };
  std::vector<WreckPiece> wreck;
  std::vector<Debris> debris;
  float craterX = 0, craterZ = 0, craterR = 0, craterD = 0;
  void breakUp(vec3 impactVel, bool water, bool air = false);
  bool airBreak = false;
  float gTunnel = 0;
  Traffic traffic;
  bool dbgCam = false, dbgFollow = false; vec3 dbgCamPos, dbgCamLook, dbgFollowOff;
  // UFO encounter: pulls up alongside, opens its hatch on two dancing aliens, laughs, waves and zooms off
  struct Ufo { bool on = false; float t = 0, next = 0, side = 1, hatch = 0, laugh = 0, wave = 0; vec3 pos, fwd, right, up; bool sfxLaugh = false, sfxZoom = false; };
  float escortSummon = 0; bool escortLatch = false;   // O + P held: the Spectre display pair
  // XR-11 Wraith systems: cloak, retracting laser turrets, bomb bay and the dark-energy weapons in the world
  struct WraithState {
    bool cloakOn = false; float stealth = 0, front = -12.f, padATap = 9.f;        // cloak: strength 0..1 and the wavefront along the craft (body z)
    bool armed = false; float lasers = 0;                          // turrets deployed 0..1
    float laserCD = 0, laserGlow = 0; int laserSide = 0;
    float bay = 0, bayHold = 0, bombLoaded = 1; int bombQueue = 0;  // bomb bay doors, bomb in the cradle 0..1, pending drops
    struct Bolt { vec3 a, b; float life; };
    struct Bomb { vec3 p, v; float t; };
    struct Blast { vec3 p; float R, age, dur; bool water; };
    struct Crater { float x, z, R, D; };
    std::vector<Bolt> bolts; std::vector<Bomb> bombs; std::vector<Blast> blasts; std::vector<Crater> craters;
    int kills = 0;
  } wraith;
  void wraithControls(float dt);
  void updateWraith(float dt);
  void wraithVisual(FrameParams& fp);
  void fireLaser();
  void detonate(vec3 p, bool water);
  float ufoSummon = 0;          // J + K held while flying summons the UFO after a second
  Ufo ufo;
  void startUfo();
  void updateUfo(float dt);   // debug scenes: free camera               // AI aircraft: airport circuits, cruisers, XR-9 formations, display team
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
  std::vector<std::pair<std::string, std::string>> stations;
  bool settingsFromPause = false;
  bool confirmNew = false;

  // ---------------------------------------------------------------- helpers
  void loadSettings(); void saveSettings();
  void loadStations();
  int radioScroll = 0;
  void saveGame();
  void toast(const std::string& s, vec3 col = vec3(1, 1, 1));
  void startFlight(const Contract& c, int spec, Career::Source src);
  void endFlight(bool success, const std::string& reason);
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
  int computePhase() const;
  const Airport& dest() const { return g_world.airports[contract.to]; }

  // UI (game_ui.cpp)
  float S() const;  // UI scale
  bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, bool highlight = false);
  bool hovered(float x, float y, float w, float h) const;
  void panel(float x, float y, float w, float h, float a = 0.78f);
  void hudPanel(float x, float y, float w, float h, float a = 1.f);
  void header(float x, float y, float w, const std::string& label);
  void card(float x, float y, float w, float h, bool sel, bool hov, vec3 accent);
  float anim(uint32_t id, float target, float rate);
  std::unordered_map<uint32_t, float> uiAnim;
  float uiDt = 0.016f, uiLastT = 0;
  // GPS moving map
  float gpsRange = 12000.f, gpsRangeTarget = 12000.f;
  std::vector<vec2> trail; float trailT = 0;
  void drawGps();
  // hidden Confidential Research Model menu (U + I on the main menu) and free XR-9 flights
  bool researchFlight = false;
  int resAirport = 0, resWx = 0, resCraft = kResearchJet; bool resAirborne = true; float resTime = 12.f, resOpened = 0;
  float prevMach = 0, prevAB = 0;
  // gamepad-driven menu cursor (left stick moves, A clicks, B backs out)
  float padCursorT = -100.f; bool padHoldA = false;
  void gamepadMenus(float dt);
  void drawPadCursor();
  void drawResearch();
  void launchResearch();
  void jetEffects(float dt);
  void drawMenu();
  void drawHub();
  void drawHubContracts(float x, float y, float w, float h);
  void drawHubHangar(float x, float y, float w, float h);
  void drawHubLogbook(float x, float y, float w, float h);
  void drawSettings(float x, float y, float w, float h);
  void drawHud(const FrameParams& fp);
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
