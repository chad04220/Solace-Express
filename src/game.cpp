// Solace Express - game flow, flight session, cameras, particles, lights, audio feed
#include "game.h"
#include "load_pacer.h"
#include <chrono>
#include <ctime>
#include <thread>
#include "airport_layout.h"
#include "entities.h"
#include "models.h"

// XR-30 wingtip (body frame) matching mapJet's cranked delta in shaders.h
static const vec3 kJetWingTip(5.62f, -0.38f, 4.4f);
// XR-30 nozzle swivel (0 aft .. 90 deg down) plus pitch vectoring; mirrored by mapJet and the exhaust plumes
static float jetNozzleAngle(const Plane& p) { return p.nozzle * 0.5f * PI - clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1) * 0.5f; }
static const vec3 kWraithWingTip(6.2f, -0.24f, 2.0f);
// research craft exhaust exits and jet directions (body coords): XR-30 two 2D nozzles, XR-40 four pods.
// strength: that exhaust's share of the thrust (1 = the XR-30's)
static int jetExhausts(const Plane& p, vec3* pos, vec3* dir, float* strength) {
  if (p.spec->special == 2) {
    for (int i = 0; i < 4; i++) {
      float a0 = p.podTilt[i], a = a0 + p.podVane[i], y = p.podYaw[i];
      pos[i] = kWraithPods[i] + vec3(0, -sinf(a0), cosf(a0)) * 1.55f;
      dir[i] = normalize(vec3(-sinf(y), -sinf(a) * cosf(y), cosf(a) * cosf(y)));
      strength[i] = clampf(p.podThr[i] * 1.4f, 0.f, 1.2f);
    }
    return 4;
  }
  float a = jetNozzleAngle(p);
  for (int s = 0; s < 2; s++) { pos[s] = vec3(s ? 0.82f : -0.82f, -0.12f, 7.75f) + vec3(0, -sinf(a), cosf(a)); dir[s] = vec3(0, -sinf(a), cosf(a)); strength[s] = 1.f; }
  return 2;
}

// ------------------------------------------------------------------ control bindings
const char* const kActionGroups[] = {"FLIGHT", "SYSTEMS", "VIEW / COMMS", "XR-40 WRAITH"};
const ActionInfo kActions[ACT_COUNT] = {
  {"pitchDown", "Pitch down (nose down)", 0, 'W', 0},          {"pitchUp", "Pitch up (nose up)", 0, 'S', 0},
  {"rollLeft", "Roll left", 0, 'A', 0},                         {"rollRight", "Roll right", 0, 'D', 0},
  {"yawLeft", "Rudder left", 0, 'Q', PAD_LB},                   {"yawRight", "Rudder right", 0, 'E', PAD_RB},
  {"throttleUp", "Throttle up", 0, K_SHIFT, 0},                 {"throttleDown", "Throttle down", 0, K_CTRL, 0},
  {"trimUp", "Trim nose up", 0, K_RBRACKET, PAD_UP},            {"trimDown", "Trim nose down", 0, K_LBRACKET, PAD_DOWN},
  {"flapsDown", "Flaps down / pods to VTOL", 1, 'F', PAD_B},    {"flapsUp", "Flaps up / pods forward", 1, 'V', PAD_X},
  {"gear", "Landing gear", 1, 'G', PAD_Y},                      {"brake", "Wheel brakes", 1, K_SPACE, PAD_A},
  {"parkingBrake", "Parking brake", 1, 'B', PAD_LEFT},          {"autopilot", "Autopilot", 1, 'Z', PAD_RS},
  {"aerobatics", "Aerobatics: fly the next figure / stop", 1, 0xBA, 0},
  {"lights", "Landing lights", 1, 'L', 0},                      {"engine", "Engine start", 1, 'I', 0},
  {"timeAccel", "Time acceleration", 1, 'T', 0},
  {"camera", "Cycle camera", 2, 'C', PAD_BACK},                 {"hud", "Show / hide HUD", 2, 'H', 0},
  {"gpsMap", "GPS moving map", 2, 'N', 0},                      {"minimap", "Minimap", 2, K_TAB, 0},
  {"radio", "Internet radio", 2, 'R', 0},                       {"anr", "Headset noise cancelling", 2, 'M', PAD_LS},
  {"zoom", "Cockpit zoom (hold; wheel zooms too)", 2, 'U', 0},
  {"cloak", "Cloak (or double-tap brake)", 3, 'X', 0},          {"weapons", "Weapons hot / safe", 3, 'Y', PAD_Y},
  {"fire", "Fire lasers (or left mouse)", 3, K_ENTER, PAD_RB},  {"bomb", "Drop bomb (or middle mouse)", 3, K_BACK, PAD_LB},
};

std::string keyName(int vk) {
  if (vk <= 0) return "--";
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
  if (vk >= 0x70 && vk <= 0x7B) return fmt("F%d", vk - 0x6F);
  if (vk >= 0x60 && vk <= 0x69) return fmt("NUM %d", vk - 0x60);
  switch (vk) {
    case K_BACK: return "BACKSPACE"; case K_TAB: return "TAB"; case K_ENTER: return "ENTER"; case K_SHIFT: return "SHIFT";
    case K_CTRL: return "CTRL"; case 0x12: return "ALT"; case K_SPACE: return "SPACE"; case K_PGUP: return "PAGE UP"; case K_PGDN: return "PAGE DOWN";
    case K_END: return "END"; case K_HOME: return "HOME"; case K_LEFT: return "LEFT"; case K_UP: return "UP"; case K_RIGHT: return "RIGHT";
    case K_DOWN: return "DOWN"; case 0x2D: return "INSERT"; case 0x2E: return "DELETE"; case 0x14: return "CAPS LOCK";
    case K_LBRACKET: return "["; case K_RBRACKET: return "]"; case K_PLUS: return "="; case K_MINUS: return "-";
    case 0xBA: return ";"; case 0xBC: return ","; case 0xBE: return "."; case 0xBF: return "/"; case 0xC0: return "`"; case 0xDC: return "\\"; case 0xDE: return "'";
    case 0x6A: return "NUM *"; case 0x6B: return "NUM +"; case 0x6D: return "NUM -"; case 0x6E: return "NUM ."; case 0x6F: return "NUM /";
  }
  return fmt("KEY %02X", vk);
}

// A lesson hint's {actionId} tokens (kActions ids) as the keys bound now. With the default bindings this is the text
// the voice lines were recorded from (AtcVoice matches it); after a rebind it no longer matches any recording, so the
// hint is shown and never spoken naming the old key.
// ------------------------------------------------------------------ input contexts
// where each action works: the flight actions in flight; the stick, rudder and throttle also with the GPS or radio
// open (they're flying the aircraft), but not the buttons those overlays use themselves (D-pad, A); the radio in the
// pause menu too
unsigned Game::actionCtxMask(int a) {
  const unsigned F = 1u << Game::CTX_FLIGHT, O = 1u << Game::CTX_OVERLAY, P = 1u << Game::CTX_PAUSE;
  switch (a) {
    case ACT_PITCH_DN: case ACT_PITCH_UP: case ACT_ROLL_L: case ACT_ROLL_R: case ACT_YAW_L: case ACT_YAW_R:
    case ACT_THR_UP: case ACT_THR_DN: case ACT_MAP: case ACT_HUD: case ACT_CAMERA: case ACT_ZOOM: case ACT_FIRE: return F | O;
    case ACT_RADIO: case ACT_ANR: return F | O | P;
    default: return F;
  }
}
Game::InputCtx Game::inputContext() const {
  if (bindCapture >= 0) return CTX_BIND;
  if (confirmNew) return CTX_DIALOG;
  if (screen != SCR_FLIGHT) return CTX_SCREEN;
  if (paused) return CTX_PAUSE;
  if (showMap || showRadio) return CTX_OVERLAY;
  return CTX_FLIGHT;
}
bool Game::actOk(int a) const { return (actionCtxMask(a) >> ctx) & 1u; }
// on a change of context everything held is set aside until released; a controller lost in flight pauses it (and
// coming back doesn't resume: the player does, from the pause menu)
void Game::armInputs() {
  ctx = inputContext();
  if (ctx != lastCtx) {
    for (int k = 0; k < 256; k++) if (in.down[k]) keyUnarmed[k] = true;
    padUnarmed |= in.buttons;
    lastCtx = ctx;
  }
  for (int k = 0; k < 256; k++) { if (!in.down[k]) keyUnarmed[k] = false; if (keyUnarmed[k]) in.down[k] = in.pressed[k] = false; }
  padUnarmed &= in.buttons;
  in.buttons &= ~padUnarmed; in.buttonsPressed &= ~padUnarmed;
  if (padWas && !in.pad && screen == SCR_FLIGHT && !paused) { paused = true; settingsFromPause = false; toast("Controller disconnected - paused. Reconnect it, or carry on with the keyboard", vec3(1, 0.85f, 0.5f)); }
  padWas = in.pad;
}

std::string Game::expandHint(const std::string& raw) const {
  std::string out;
  for (size_t i = 0; i < raw.size(); i++) {
    size_t e = raw[i] == '{' ? raw.find('}', i) : std::string::npos;
    int a = -1;
    if (e != std::string::npos) { std::string id = raw.substr(i + 1, e - i - 1); for (int k = 0; k < ACT_COUNT; k++) if (id == kActions[k].id) a = k; }
    if (a < 0) { out += raw[i]; continue; }
    out += keyName(set.keyBind[a]); i = e;
  }
  return out;
}

// The flown time for a job's plan when it's in; else (start) a background flight is begun for it if none is running
void Game::applyQuote(const Contract& c, Career::LaunchPlan& e, bool start) {
  if (quoteJob.valid() && quoteJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) quoteFlown[quoteJobKey] = quoteJob.get();
  std::string key = fmt("%s|%d|%d|%d", c.id.c_str(), e.spec, c.from, c.to);
  auto it = quoteFlown.find(key);
  if (it != quoteFlown.end()) { career.useFlownTime(e, c, it->second.first, it->second.second); return; }
  if (!start || quoteJob.valid() || c.forceAircraft >= 0) return;   // (lessons are flown by hand: the quick estimate stands)
  Plane::perf(&kAircraft[e.spec]);   // (learned here first: the worker then only reads it)
  quoteJobKey = key;
  Contract cc = c; int spec = e.spec;
  quoteJob = std::async(std::launch::async, [cc, spec] { float fuel = -1; float m = simulateFlightMinutes(cc, spec, &fuel); return std::make_pair(m, fuel); });
}

std::string padName(unsigned bit) {
  static const char* n[] = {"A", "B", "X", "Y", "LB", "RB", "VIEW", "MENU", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "LS CLICK", "RS CLICK"};
  for (int i = 0; i < 14; i++) if (bit & (1u << i)) return n[i];
  return "--";
}

// Settings / pause menu "press a key" capture. Runs before anything else reads the frame's input and swallows it,
// so the press that sets a binding never also flies the aircraft or closes the menu.
void Game::updateBindCapture(float dt) {
  if (bindCapture < 0) return;
  bindCaptureT += dt;
  int a = bindCapture;
  bool done = false;
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START) || bindCaptureT > 8.f) done = true;   // cancel
  else if (bindCaptureDev == 0) {
    for (int k = 1; k < 256 && !done; k++) {
      if (!in.pressed[k] || k == K_ESC || k == K_F11 || k == 0x72) continue;
      for (int b = 0; b < ACT_COUNT; b++)   // a key already used in the same group swaps over
        if (b != a && set.keyBind[b] == k && kActions[b].group == kActions[a].group) set.keyBind[b] = set.keyBind[a];
      set.keyBind[a] = k; done = true;
    }
  } else if (in.buttonsPressed) {
    unsigned bit = in.buttonsPressed & ~(unsigned)PAD_START;
    if (bit) {
      bit &= ~(bit - 1);   // lowest set bit
      for (int b = 0; b < ACT_COUNT; b++)
        if (b != a && set.padBind[b] == bit && kActions[b].group == kActions[a].group) set.padBind[b] = set.padBind[a];
      set.padBind[a] = bit; done = true;
    }
  }
  if (done) { bindCapture = -1; saveSettings(); g_audio.trigger(SFX_CLICK); }
  memset(in.pressed, 0, sizeof(in.pressed)); in.buttonsPressed = 0;
  for (int i = 0; i < 3; i++) in.mPressed[i] = false;
}

// ------------------------------------------------------------------ settings / save
static std::string joinPath(const std::string& d, const char* f) { return d.empty() ? std::string(f) : d + "/" + f; }

void Game::loadSettings() {
  FILE* f = fopen(joinPath(saveDir, "settings.cfg").c_str(), "r");
  if (!f) return;
  resDone.clear(); trialBest.clear();   // (read twice at startup: the lists are the file's, not appended to)
  char k[64]; float v;
  while (fscanf(f, "%63s %f", k, &v) == 2) {
    if (!std::isfinite(v) || fabsf(v) > 1e6f) continue;   // a damaged value keeps the default
    std::string s = k;
    if (s == "renderScale") continue;   // old setting (the renderer scales itself)
    else if (s.rfind("rescard.", 0) == 0) { if (v > 0.5f) resDone.insert(s.substr(8)); }
    else if (s.rfind("trial.", 0) == 0) { size_t dot = s.rfind('.'); if (dot > 6 && v >= 0.f) { auto& L = trialBest[s.substr(6, dot - 6)]; L.push_back(v); std::sort(L.begin(), L.end()); if (L.size() > 5) L.resize(5); } }
    else if (s == "quality") set.quality = (int)clampf(v, 0, 2);
    else if (s == "master") set.master = clampf(v, 0, 1);
    else if (s == "engineVol") set.engineVol = clampf(v, 0, 1.5f);
    else if (s == "sfxVol") set.sfxVol = clampf(v, 0, 1.5f);
    else if (s == "radioVol") set.radioVol = clampf(v, 0, 1);
    else if (s == "atcVol") set.atcVol = clampf(v, 0, 1);
    else if (s == "invertPitch") set.invertPitch = v != 0;
    else if (s == "showHints") set.showHints = v != 0;
    else if (s == "traffic") set.traffic = v != 0;
    else if (s == "metric") set.metric = v != 0;
    else if (s == "fullscreen") set.fullscreen = v != 0;
    else if (s == "radioStation") set.radioStation = (int)v;
    else if (s == "mouseSens") set.mouseSens = clampf(v, 0.2f, 3.f);
    else if (s == "renderRes") set.resMode = std::clamp((int)v, 0, 4);
    else if (s == "fpsTarget") set.fpsTarget = std::clamp((int)v, 0, 240);
    else if (s == "renderer") {}   // (the old renderer choice: there is one renderer now)
    else if (s == "fov") set.fov = clampf(v, 40.f, 80.f);
    else if (s == "headLook") set.headLook = v != 0;
    else if (s == "cbHud") set.cbHud = v != 0;
    else if (s == "uiScale") set.uiScale = clampf(v, 0.8f, 1.4f);
    else if (s.rfind("hudCam", 0) == 0 && s.size() == 7 && s[6] >= '0' && s[6] <= '3') set.hudCam[s[6] - '0'] = v != 0;
    else if (s.rfind("key.", 0) == 0 || s.rfind("pad.", 0) == 0)
      for (int i = 0; i < ACT_COUNT; i++)
        if (s.compare(4, std::string::npos, kActions[i].id) == 0) {
          if (s[0] == 'k') set.keyBind[i] = std::clamp((int)v, 0, 255); else set.padBind[i] = (unsigned)v & 0x3FFFu;
        }
  }
  fclose(f);
}

// Written only when something changed (the settings page calls this every frame), through a temp file so an
// interrupted write never leaves a half-written settings file.
void Game::saveSettings() {
  std::string t = fmt("quality %d\nmaster %f\nengineVol %f\nsfxVol %f\nradioVol %f\ninvertPitch %d\nshowHints %d\nmetric %d\nfullscreen %d\nradioStation %d\nmouseSens %f\ntraffic %d\natcVol %f\n",
          set.quality, set.master, set.engineVol, set.sfxVol, set.radioVol, set.invertPitch, set.showHints, set.metric, set.fullscreen, set.radioStation, set.mouseSens, set.traffic, set.atcVol);
  t += fmt("renderRes %d\nfpsTarget %d\nfov %f\nheadLook %d\ncbHud %d\nuiScale %f\nhudCam0 %d\nhudCam1 %d\nhudCam2 %d\nhudCam3 %d\n", set.resMode, set.fpsTarget, set.fov, set.headLook, set.cbHud, set.uiScale, set.hudCam[0], set.hudCam[1], set.hudCam[2], set.hudCam[3]);
  for (int i = 0; i < ACT_COUNT; i++) t += fmt("key.%s %d\npad.%s %u\n", kActions[i].id, set.keyBind[i], kActions[i].id, set.padBind[i]);
  for (const std::string& id : resDone) t += "rescard." + id + " 1\n";
  for (auto& tb : trialBest) for (size_t i = 0; i < tb.second.size(); i++) t += fmt("trial.%s.%d %f\n", tb.first.c_str(), (int)i, tb.second[i]);
  if (t == settingsWritten) return;
  std::string path = joinPath(saveDir, "settings.cfg"), tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return;
  bool ok = fwrite(t.data(), 1, t.size(), f) == t.size();
  ok = fclose(f) == 0 && ok;
  if (ok) ok = replaceFile(tmp, path);   // (in one step: a crash between a remove and a rename left no settings at all)
  if (!ok) remove(tmp.c_str());
  if (ok) settingsWritten = t;
}

// Built-in presets. Bump kStationsVersion when adding presets: older station files get the new ones appended.
static const int kStationsVersion = 2;
static const std::pair<const char*, const char*> kDefaultStations[] = {
  {"SomaFM Groove Salad (ambient)", "https://ice1.somafm.com/groovesalad-128-mp3"},
  {"SomaFM Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3"},
  {"SomaFM Secret Agent", "https://ice1.somafm.com/secretagent-128-mp3"},
  {"SomaFM Lush", "https://ice1.somafm.com/lush-128-mp3"},
  {"SomaFM Indie Pop Rocks", "https://ice1.somafm.com/indiepop-128-mp3"},
  {"Radio Paradise (eclectic)", "https://stream.radioparadise.com/mp3-128"},
  {"Radio Paradise Mellow", "https://stream.radioparadise.com/mellow-128"},
  {"KEXP Seattle", "https://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
  // US East Coast public and college stations (free, listener-supported)
  {"WNYC 93.9 New York - NPR news", "https://fm939.wnyc.org/wnycfm"},
  {"WQXR 105.9 New York - classical", "https://stream.wqxr.org/wqxr"},
  {"WFMU 91.1 Jersey City - freeform", "https://stream0.wfmu.org/freeform-128k"},
  {"WBGO 88.3 Newark - jazz", "https://wbgo.streamguys1.com/wbgo128"},
  {"WFUV 90.7 New York - indie & folk", "https://onair.wfuv.org/onair-hi"},
  {"WXPN 88.5 Philadelphia - AAA", "https://wxpnhi.xpn.org/xpnhi"},
  {"WBUR 90.9 Boston - NPR news", "https://icecast-stream.wbur.org/wbur"},
  {"GBH 89.7 Boston - NPR news", "https://wgbh-live.streamguys1.com/wgbh"},
  {"CRB 99.5 Boston - classical", "https://wgbh-live.streamguys1.com/classical-hi"},
  {"WMBR 88.1 MIT Cambridge - college", "https://wmbr.org:8002/hi"},
  {"WUNC 91.5 Chapel Hill - NPR news", "https://wunc-ice.streamguys1.com/wunc-128-mp3"},
  {"WCPE 89.7 Raleigh - classical", "https://audio-mp3.ibiblio.org/wcpe.mp3"},
};

static void writeStations(const std::string& path, const std::vector<std::pair<std::string, std::string>>& st) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  fprintf(f, "# Solace Express internet radio stations\n# stations-version %d\n# One per line:  Name|URL   (MP3 or AAC HTTP/HTTPS streams). Add your own below.\n", kStationsVersion);
  for (auto& s : st) fprintf(f, "%s|%s\n", s.first.c_str(), s.second.c_str());
  fclose(f);
}

void Game::loadStations() {
  stations.clear();
  std::string path = joinPath(saveDir, "radio_stations.txt");
  FILE* f = fopen(path.c_str(), "r");
  bool userFile = f != nullptr;
  if (!f) f = fopen("radio_stations.txt", "r");
  int version = 1;
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (s.compare(0, 19, "# stations-version ") == 0) version = atoi(s.c_str() + 19);
      if (s.empty() || s[0] == '#') continue;
      size_t bar = s.find('|');
      if (bar == std::string::npos) stations.push_back({s, s});
      else stations.push_back({s.substr(0, bar), s.substr(bar + 1)});
    }
    fclose(f);
  }
  if (stations.empty() || version < kStationsVersion || (!userFile && !saveDir.empty())) {
    // add presets the list doesn't have yet (keeps the player's own stations and order)
    for (auto& d : kDefaultStations) {
      bool have = false;
      for (auto& s : stations) if (s.second == d.second) have = true;
      if (!have) stations.push_back({d.first, d.second});
    }
    if (!saveDir.empty() || userFile) writeStations(path, stations);
  }
  set.radioStation = std::clamp(set.radioStation, 0, (int)stations.size() - 1);
}

void Game::saveGame() {
  if (career.save(joinPath(saveDir, "career.sav"))) hasSave = true;
  else toast("Couldn't save the career (disk full or folder not writable)", vec3(1.f, 0.4f, 0.3f));
}
bool Game::commit(const std::function<void(Career&)>& change) {
  Career cand = pendingCareer ? *pendingCareer : career;
  change(cand);
  if (headless && saveDir.empty()) { career = cand; pendingCareer.reset(); return true; }   // (tests: no disk)
  if (cand.save(joinPath(saveDir, "career.sav"))) { career = cand; pendingCareer.reset(); hasSave = true; saveWhy.clear(); return true; }
  pendingCareer = cand; retryT = 0;
  saveWhy = "Couldn't save the career (disk full or folder not writable)";
  toast(saveWhy + " - will retry", vec3(1.f, 0.4f, 0.3f));
  return false;
}
bool Game::retryCommit() {
  if (!pendingCareer) return true;
  if (!pendingCareer->save(joinPath(saveDir, "career.sav"))) return false;
  career = *pendingCareer; pendingCareer.reset(); hasSave = true; saveWhy.clear();
  toast("Career saved", vec3(0.6f, 1.f, 0.7f));
  return true;
}
// A career flight: the save marks the attempt open first (a crash to desktop mid-flight then shows on the next start
// that a flight was interrupted), then the flight starts
// The fuel at take-off: what was chosen on the card, else the plan's estimate with a quarter to spare (never less
// than an owned aircraft already holds, never more than the tanks)
float Game::chosenFuel(const Contract& c, int spec, Career::Source src, const Career::LaunchPlan& p) const {
  const AircraftSpec& s = kAircraft[spec];
  if (s.special) return s.maxFuel;
  if (launchFuelKg > 0) return std::min(launchFuelKg, s.maxFuel);
  float have = 0.f;
  if (src == Career::SRC_OWNED) { int oi = career.ownedIndexFor(spec); if (oi >= 0) have = career.fleet[oi].fuel; }
  if (src != Career::SRC_OWNED) return s.maxFuel;   // a rental comes full (the weight limit may still ask for less)
  (void)c;
  return std::min(std::max(p.fuelKgEst * 1.25f, have), s.maxFuel);
}
// A launch's save is its precondition: when it fails, nothing flies and the launch is not left pending to be saved
// later (a retried save would then record an open flight that never started); the card's button tries again
bool Game::commitLaunch(const std::function<void(Career&)>& change) {
  if (commitBlocked()) { hubMsg = "Your last result isn't saved yet: retry the save first."; hubMsgTime = 5; return false; }
  if (commit(change)) return true;
  pendingCareer.reset(); saveWhy.clear();
  hubMsg = "The career could not be saved (disk full or folder not writable): the flight was not started. Try again."; hubMsgTime = 6;
  return false;
}
void Game::beginCareerFlight(const Contract& c, int spec, Career::Source src) {
  Career::LaunchPlan p = career.plan(c, spec, src);
  career.planFuel(p, c, chosenFuel(c, spec, src, p));
  if (!commitLaunch([&](Career& k) {
    k.attempt++; k.attemptOpen = true;
    // (lessons and checkrides are flown whole: no job; a free flight leaves a job waiting at its stop as it is)
    if (Career::resumable(c)) k.accept(c, spec, src, p);
    else if (!(k.job && k.job->state == Career::JobState::RECOVERY)) k.job.reset();
  })) return;
  startFlight(c, spec, src);
}
void Game::continueJob(int spec, Career::Source src) {
  if (!career.job || career.job->state != Career::JobState::RECOVERY) return;
  Contract c = career.job->continuation();
  bool took = commitLaunch([&](Career& k) {
    Career::JobState& J = *k.job;
    if (spec != J.spec || src != J.src) { J.hirePaid = false; J.ferryPaid = false; }   // a different aircraft: a new hire, its own ferry (C3)
    J.spec = spec; J.src = src; J.state = Career::JobState::ACTIVE; k.attempt++; k.attemptOpen = true;
  });
  if (!took || !career.job || career.job->state != Career::JobState::ACTIVE) return;   // the save did not take: the job stays as it was, nothing flies on a stale career
  startFlight(c, spec, src);
  applyJobLeg();
}
// the committed job's leg carries on from its checkpoints, its clock and the ride so far, its paid fees waived
void Game::applyJobLeg() {
  if (!career.job || career.job->state != Career::JobState::ACTIVE) return;
  const Career::JobState& J = *career.job;
  wpIndex = std::min(J.wpDone, (int)contract.wps.size()); result.wpDone = wpIndex; jobClockBase = J.jobClockMin * 60.f;
  result.patient = J.patient; result.comfort = J.comfort;
  if (J.hirePaid) launchPlan.hire = 0;
  if (J.ferryPaid) launchPlan.ferry = 0;
  jobLeg = true;
}
void Game::continuationWaivers(Career::LaunchPlan& p, const Contract& c, int spec, Career::Source src) const {
  if (!career.job) return;
  const Career::JobState& J = *career.job;
  if (spec != J.spec || src != J.src) return;   // another aircraft: its hire and its ferry are new
  if (J.hirePaid) p.hire = 0;
  if (J.ferryPaid) p.ferry = 0;
  p.net = c.payout - p.fees() - p.fuelCostEst;
}
// The pause menu's Restart: the same flight from the start, in the mode it was flown in. A practice approach or a
// trial stays off the books, a job's leg carries on from where the job waited (its checkpoints, clock and ride, its
// paid fees), a research sortie relaunches (the review of v3.31.0, C1: Restart made practice and trials career
// flights, and reset a job leg's clock and comfort)
void Game::restartFlight() {
  if (researchFlight) { launchResearch(); return; }
  const bool iso = isolatedFlight, leg = jobLeg;
  Contract c = contract;
  startFlight(c, specIdx, source);
  if (iso) isolatedFlight = true;
  if (leg) applyJobLeg();
}
// The debrief's second button: a job waiting at its stop flies on from there with its clock, ride and paid fees (as
// the hub's Continue does); anything else (a lesson, a checkride, a job that ended) is flown again from the start
void Game::retryFromDebrief() {
  // the aircraft must still be one the job can be flown in: if it is gone (repossessed, sold) or no longer suits, the
  // hub's chooser offers the ones that can, with the reason (the review of v3.31.0, C2: a repossessed aircraft flew)
  std::string why;
  if (career.job && career.job->state == Career::JobState::RECOVERY) {
    const auto sc = career.canFly(career.job->continuation(), specIdx, &why);
    if (sc == Career::SRC_NONE) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; hubMsg = std::string("Choose another aircraft: ") + kAircraft[specIdx].name + " - " + why; hubMsgTime = 6; return; }
    continueJob(specIdx, sc);
  } else {
    Contract c = contract; auto sc = career.canFly(c, specIdx, &why);
    if (sc == Career::SRC_NONE && source == Career::SRC_LESSON) sc = Career::SRC_LESSON;   // (the school's aircraft, as the hub gives it)
    if (sc == Career::SRC_NONE) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; hubMsg = std::string("Choose another aircraft: ") + kAircraft[specIdx].name + " - " + why; hubMsgTime = 6; return; }
    beginCareerFlight(c, specIdx, sc);
  }
  if (screen == SCR_DEBRIEF) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; }   // (the launch didn't take: its message is on the hub)
}
void Game::releaseJob() {
  if (!career.job || commitBlocked()) return;
  commit([](Career& k) { k.releaseJob(); });
  hubMsg = "Job released: the load stays where it is."; hubMsgTime = 4;
}
void Game::practiseApproach(int spec, Career::Source src) {
  if (!career.job) return;
  Contract c; c.id = "PRACTICE"; c.title = "Practice: " + career.job->c.title; c.type = CT_FERRY; c.from = career.job->at; c.to = career.job->c.to; c.wx = career.job->c.wx;
  c.brief = "An approach flown for practice: nothing here counts for the job or the career.";
  startFlight(c, spec, src);
  isolatedFlight = true;
}

void Game::init(bool buildWorld) {
  if (buildWorld) g_world.build();
  buildStory();
  loadSettings();
  applyUiPalette();
  wantPacing = true;   // (the frame-rate target from the settings)
  loadStations();
  {   // every type's performance, learned by flying it, on threads side by side (the job board needs the career ones now);
    // kept with the cache, so a launch after the first reads them
    const std::string pf = cacheDir.empty() ? std::string() : joinPath(cacheDir, "perf.bin");
    if (pf.empty() || !Plane::perfLoad(pf, buildStamp)) {
      std::vector<std::thread> th;
      for (int i = 0; i < kNumAircraft; i++) th.emplace_back([i] { Plane::perf(&kAircraft[i]); });
      for (auto& t : th) t.join();
      if (!pf.empty()) Plane::perfSave(pf, buildStamp);
    }
  }
  career.newGame();
  std::string sav = joinPath(saveDir, "career.sav");
  hasSave = career.load(sav);
  if (!hasSave && career.load(sav + ".bak")) { hasSave = true; toast("Career save was damaged: restored the previous save"); }
  if (hasSave && career.attemptOpen) {   // the last session ended inside a flight: the career stands as it was before it
    toast(fmt("Your last flight was interrupted: you are back at %s", g_world.airports[career.location].name), vec3(1.f, 0.8f, 0.4f));
    // an accepted job whose leg never ended waits where the leg began (the hub's recovery card offers it again)
    commit([](Career& k) { k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
  }
  if (!hasSave) {
    career.newGame();
    // keep an unreadable save aside rather than overwriting it with a new career
    if (FILE* f = fopen(sav.c_str(), "r")) { fclose(f); remove((sav + ".damaged").c_str()); rename(sav.c_str(), (sav + ".damaged").c_str()); toast("Career save couldn't be read: kept as career.sav.damaged"); }
  }
  radio.init();
  radio.setVolume(set.radioVol);
  atc.load(assetDir + "/voice");   // tower voices (absent: the towers stay silent)
  camQ = quat();
}

void Game::initHeadless() { headless = true; career.newGame(); set.resMode = 0; failuresArmed = false; }   // tools and tests render at the full resolution; nothing breaks unless the test breaks it

// ------------------------------------------------------------------ failures (C7)
void Game::rollFailures(const Contract& c, int spec, Career::Source src) {
  failPlan = FailPlan();
  if (!failuresArmed || c.startAirborne && c.type == CT_LESSON) return;
  float chance = career.failureChance(src, spec);
  if (chance <= 0.f) return;
  // deterministic per attempt: the same flight retried after a crash gets the same roll, a new attempt a new one
  uint32_t h = 2166136261u; for (char ch : c.id) h = (h ^ (uint8_t)ch) * 16777619u;
  Rng r(h ^ (career.attempt * 2654435761u) ^ (uint32_t)spec * 97u);
  if (r.uni() >= chance) return;
  const AircraftSpec& s = kAircraft[spec];
  // which: engines most often, the gear only on retractables, icing only in weather that can make it
  float w[FAIL_COUNT] = {0, 3.f, 2.f, 1.5f, 1.2f, s.retract ? 1.5f : 0.f, 1.f, (c.wx.precip == 2 || (c.wx.cloudCover > 0.6f && c.wx.cloudBase > 1800.f)) ? 1.5f : 0.f};
  float sum = 0; for (float x : w) sum += x;
  float pick = r.uni() * sum; int kind = FAIL_ENGINE_PARTIAL;
  for (int k = 1; k < FAIL_COUNT; k++) { if (pick < w[k]) { kind = k; break; } pick -= w[k]; }
  failPlan.kind = kind; failPlan.engine = s.engines > 1 ? (int)(r.uni() * s.engines) % std::min(s.engines, 4) : 0;
  float est = std::max(launchPlan.minutesEst * 60.f, 240.f);
  failPlan.at = 90.f + r.uni() * clampf(est * 0.6f, 60.f, 1500.f);   // somewhere in the first part of the flight, never on the take-off roll
}
// The job types' live records (C6): the medevac patient and the VIP's comfort drift down with g, bank, turbulence
// bumps and a firm touchdown; the survey counts its time in the altitude band between the first and the last
// checkpoint; the low-vis run checks the alignment the first time the aircraft comes below minimums near the
// destination (a go-around afterwards clears it); night freight notes the landing light at touchdown.
void Game::updateJobMeters(float dt, float gs) {
  (void)gs;
  const Contract& c = contract;
  if (researchFlight || isolatedFlight || crashed) return;
  bool air = !plane.onGround && takeoffAnnounced;
  if (c.type == CT_MEDEVAC || c.type == CT_VIP) {
    float& m = c.type == CT_MEDEVAC ? result.patient : result.comfort;
    float gLim = c.type == CT_MEDEVAC ? 1.5f : 1.3f, bankLim = 30.f;
    float rate = 0;
    if (air) {
      rate += std::max(0.f, plane.gLoad - gLim) * 0.08f + std::max(0.f, (c.type == CT_MEDEVAC ? 0.6f : 0.75f) - plane.gLoad) * 0.08f;
      rate += std::max(0.f, fabsf(plane.bankDeg()) - bankLim) * 0.003f;
      rate += std::max(0.f, wx.turbulence - 0.25f) * 0.01f;
    }
    if (plane.ev.touchdown && fabsf(plane.ev.touchdownVs) * 196.85f > 300.f) m -= (fabsf(plane.ev.touchdownVs) * 196.85f - 300.f) / 1500.f;
    m = clampf(m - rate * dt, 0.f, 1.f);
  }
  if (c.type == CT_SURVEY && !c.wps.empty() && air && wpIndex > 0 && wpIndex < (int)c.wps.size()) {
    surveyT += dt; if (fabsf(plane.pos.y - c.wps[0].alt) <= 46.f) surveyInT += dt;
    result.surveyInBand = surveyT > 1.f ? surveyInT / surveyT : 1.f;
  }
  if (c.type == CT_IFR && air) {
    const Airport& A = g_world.airports[c.to];
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (result.goArounds > minimumsGoArounds) { minimumsGoArounds = result.goArounds; minimumsChecked = false; result.belowMinimumsUnaligned = false; }
    float minimums = std::max(60.f, wx.cloudBase - A.elev - 30.f);
    if (!minimumsChecked && dA < 8000.f && plane.pos.y - A.elev < minimums) {
      minimumsChecked = true;
      bool rev = dot(plane.vel, A.dir()) < 0.f;
      vec3 ld = rev ? -A.dir() : A.dir(), rel = plane.pos - A.threshold(rev); rel.y = 0;
      float lat = fabsf(rel.x * ld.z - rel.z * ld.x);
      float hErr = fabsf(wrapAngle((atan2f(ld.x, -ld.z) - plane.heading() * DEG))) / DEG;
      bool aligned = lat < A.width * 1.5f + 25.f && hErr < 20.f;
      if (!aligned) { result.belowMinimumsUnaligned = true; toast("Below minimums and not lined up - GO AROUND", vec3(1, 0.45f, 0.35f), true); }
      else toast("Runway in sight", vec3(0.6f, 1, 0.7f));
    }
  }
  if (c.type == CT_NIGHT && plane.ev.touchdown) result.landingLightOn = landingLight;
}
// the research test card: the current step's condition, held for its time where it asks one, then the next
void Game::updateResearchCard(float dt) {
  if (!researchFlight || resCard < 0 || resCardDone || crashed || resCard >= kNumResCards) return;
  const ResCard& C = kResCards[resCard];
  if (resStep >= C.n) return;
  const ResStep& st = C.steps[resStep];
  bool air = !plane.onGround, inst = false;
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  switch (st.kind) {
    case RS_MACH: inst = air && plane.mach >= st.v; break;
    case RS_ALT: inst = plane.pos.y >= st.v; break;
    case RS_G: inst = air && plane.gLoad >= st.v; break;
    case RS_NEGG: inst = air && plane.gLoad <= st.v; break;
    case RS_ROLL: inst = air && fabsf(plane.w.z) / DEG >= st.v; break;
    case RS_SLOW: inst = air && plane.agl() > 150.f && plane.ias <= st.v; break;
    case RS_STALL: inst = air && plane.agl() > 600.f && plane.stallWarn > 0.9f; break;
    case RS_CLIMB: inst = air && plane.vel.y >= st.v; break;
    case RS_HOVER: inst = air && plane.nozzle > 0.7f && length(plane.vel) < 4.f && plane.agl() > 4.f; break;
    case RS_VLAND: inst = plane.onGround && touchedDown && plane.nozzle > 0.7f && gs < 1.f && touchdownFpm < 400.f; break;
    case RS_CLOAK: inst = wraith.stealth >= 0.95f; break;
    case RS_LASER: inst = wraith.shots >= (int)st.v; break;
    case RS_BOMB: inst = wraith.dropped >= (int)st.v; break;
    case RS_LAND: { float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA); inst = plane.onGround && touchedDown && gs < 2.f && ap == resAirport && dA < g_world.airports[ap].length * 0.5f + 600.f; } break;
  }
  resHold = inst ? resHold + dt : 0.f;
  if (!inst || resHold < st.hold) return;
  resStep++; resHold = 0;
  if (resStep >= C.n) {
    resCardDone = true; resDone.insert(C.id); saveSettings();
    toast(fmt("TEST CARD %s COMPLETE - %s SIGNED OFF", C.id, C.title), vec3(0.5f, 1.f, 0.6f)); g_audio.trigger(SFX_SUCCESS);
  } else { toast(fmt("STEP %d of %d: %s", resStep + 1, C.n, C.steps[resStep].label), vec3(0.9f, 0.9f, 0.6f)); g_audio.trigger(SFX_CHIME, 0.8f); }
}
// Dynamic weather (C8): the conditions drift from the contract's weather to its forecast over the flight's estimated
// time (and a little beyond: the front keeps moving), with a slow wander on top; everything that reads wx (the flight
// model, the tower, the HUD) sees the current state. On the autopilot, a wind that has swung to favour the other
// runway end re-plans the approach while there is still room.
void Game::updateWeather(float dt) {
  if (researchFlight || !contract.wxShift) return;
  float T = std::max(launchPlan.minutesEst * 60.f, 120.f) * 1.1f;
  float f = smoothstepf(0.f, 1.f, clampf((jobClockBase + flightClock) / T, 0.f, 1.f));
  const Weather& a = wxStart; const Weather& b = contract.wxEnd;
  float wander = 6.f * sinf(gameTime * 0.011f) + 3.f * sinf(gameTime * 0.037f + 1.f);
  wx.windFrom = wrapDeg360(a.windFrom + wrapAngle((b.windFrom - a.windFrom) * DEG) / DEG * f + wander * f);
  wx.windSpeed = a.windSpeed + (b.windSpeed - a.windSpeed) * f + 0.4f * sinf(gameTime * 0.023f) * f;
  wx.gust = a.gust + (b.gust - a.gust) * f;
  wx.turbulence = a.turbulence + (b.turbulence - a.turbulence) * f;
  wx.cloudCover = a.cloudCover + (b.cloudCover - a.cloudCover) * f;
  wx.cloudBase = a.cloudBase + (b.cloudBase - a.cloudBase) * f;
  wx.visibility = a.visibility + (b.visibility - a.visibility) * f;
  if (f > 0.5f && (wx.precip != b.precip || wx.storm != b.storm)) { wx.precip = b.precip; wx.storm = b.storm; toast(b.precip == 2 ? "Snow has set in" : b.precip == 1 ? "Rain has started" : "The rain has stopped", vec3(0.8f, 0.8f, 0.8f)); }
  apRepickT = std::max(0.f, apRepickT - dt);
  if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) && plane.apStage == Plane::APS_NAV && plane.apAirport >= 0 && apRepickT <= 0.f) {
    const Airport& A = g_world.airports[plane.apAirport];
    float hw = cosf((wx.windFrom - A.heading) * DEG) * wx.windSpeed, hwR = -hw;   // headwind component on each end (m/s)
    bool better = plane.apRev ? hw > hwR + 2.5f : hwR > hw + 2.5f;   // the other end has 5 kt more headwind
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (better && dA > 6000.f) {   // (the planner weighs the wind against the flying round: it may keep the end it has)
      bool was = plane.apRev;
      plane.apEngage(plane.apMode, plane.apAirport, wx);
      apRepickT = plane.apRev != was ? 90.f : 8.f;
      if (!plane.apDecline.empty()) {   // (neither end is safe in the new wind: the hold it falls back to is said, and why)
        g_audio.trigger(SFX_AP_DISC);
        toast("Autopilot: the wind has shifted - unable to autoland at " + plane.apDecline + " - holding heading and height", vec3(1, 0.75f, 0.35f));
        toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      } else if (plane.apRev != was) toast(fmt("Autopilot: the wind has shifted - now runway %02d at %s", A.rwyNumber(plane.apRev), A.code), vec3(0.6f, 1, 0.6f));
    }
  }
}
// your airline's scheduled flights (C11): when a route's line passes within 15 km of you and none of its flights
// is in the air, one appears on the line a way off and flies on to the route's destination in the company livery
void Game::airlineTraffic(float dt) {
  airlineTrafficT -= dt;
  if (airlineTrafficT > 0 || researchFlight || career.airline.routes.empty() || !traffic.enabled || plane.onGround) return;
  airlineTrafficT = 20.f;
  for (size_t ri = 0; ri < career.airline.routes.size(); ri++) {
    const Career::Route& r = career.airline.routes[ri];
    if (r.fleetIdx < 0 || r.fleetIdx >= (int)career.fleet.size() || traffic.routeFlying((int)ri)) continue;
    vec3 a = g_world.airports[r.from].pos(), b = g_world.airports[r.to].pos(); a.y = b.y = 0;
    vec3 ab = b - a; float L = length(ab); if (L < 1.f) continue;
    vec3 p0 = vec3(plane.pos.x, 0, plane.pos.z);
    float t = clampf(dot(p0 - a, ab) / (L * L), 0.05f, 0.95f);
    vec3 q = a + ab * t; float dist = length(q - p0);
    if (dist > 15000.f || dist < 2500.f) continue;
    float alt = std::max(g_world.height(q.x, q.z), 0.f) + 900.f + 150.f * (ri % 3);
    traffic.spawnRoute(career.fleet[r.fleetIdx].spec, vec3(q.x, alt, q.z), r.to, (int)ri);
    break;
  }
}

// ------------------------------------------------------------------ trials (C12)
const float Game::kFormationRunS = 180.f;
Contract Game::trialContract(int kind) const {
  Contract c; c.type = CT_TRIAL; c.payout = 0; c.minLicense = LIC_STUDENT; c.id = trialId(kind); c.title = trialName(kind);
  int home = career.location;
  c.from = c.to = home;
  c.wx = Weather(); c.wx.windSpeed = 3.f; c.wx.cloudCover = 0.3f; c.wx.timeOfDay = 10.f;
  if (kind == TR_STOL) { int smp = g_world.findAirport("SMP"); if (smp >= 0) c.from = c.to = smp; }
  const Airport& A = g_world.airports[c.from];
  if (kind == TR_SPOT) c.brief = fmt("Take off, fly a circuit and put the wheels down as close as you can to the mark 300 m past the threshold at %s, as softly as you can. Score: metres from the mark plus a quarter of the touchdown rate in fpm. Lower is better.", A.name);
  else if (kind == TR_FORMATION) c.brief = fmt("Take off from %s and climb above 300 m: the Spectre display pair joins you and flies its show around you for three minutes. Hold a steady platform for them - wings near level, no yanking, no diving at the ground - and land back here when they bow out. Score: the seconds you spent out of a steady platform. Lower is better.", A.name);
  else if (kind == TR_STOL) c.brief = "Summit Pass, 600 m of gravel at 5,400 ft. Take off, come round and land as short as you can: the score is the landing roll from touchdown to a stop. Lower is better. The Bushmaster is the natural choice.";
  else {
    // the gate course: eight rings out from the runway, each bending a little, hugging the ground
    uint32_t seed = kind == TR_DAILY ? (uint32_t)(time(nullptr) / 86400) * 2654435761u : 0x5eedu + (uint32_t)home * 131u;
    Rng r(seed);
    vec3 p = A.pos() + A.dir() * 1800.f; float hdg = atan2f(A.dir().x, -A.dir().z);
    for (int k = 0; k < 8; k++) {
      hdg += (r.uni() - 0.5f) * 0.9f;
      p = p + vec3(sinf(hdg), 0, -cosf(hdg)) * 1100.f;
      float hmax = 0; for (int q = 0; q < 8; q++) { float a = q * 0.785f; hmax = std::max(hmax, g_world.height(p.x + cosf(a) * 250.f, p.z + sinf(a) * 250.f)); }
      float alt = std::max(hmax, A.elev) + 140.f + r.uni() * 80.f;
      c.wps.push_back({p.x, p.z, alt});
    }
    c.brief = kind == TR_DAILY ? fmt("Today's course: eight gates laid out from %s, new every day. The clock runs from the first gate to the last; land back here when you are through. Lower is better.", A.name)
                               : fmt("Eight gates low over the country out of %s. The clock runs from the first gate to the last; land back here when you are through. Lower is better.", A.name);
  }
  return c;
}
std::string Game::trialScore(int kind, float v) const {
  if (v < 0) return "DNF";
  if (kind == TR_SPOT) return fmt("%.0f pts", v);
  if (kind == TR_STOL) return fmt("%.0f m roll", v);
  if (kind == TR_FORMATION) return fmt("%.0f s out", v);
  return fmt("%d:%02d.%d", (int)v / 60, (int)v % 60, (int)(v * 10) % 10);
}
void Game::finishTrial(bool success) {
  int kind = -1; for (int k = 0; k < TR_COUNT; k++) if (contract.id == trialId(k)) kind = k;
  if (kind < 0) return;
  float score = -1;
  if (success && result.landed) {
    if (kind == TR_SPOT && result.tdPastThrM >= 0) score = fabsf(result.tdPastThrM - 300.f) + fabsf(touchdownFpm) * 0.25f;
    else if (kind == TR_STOL && result.tdPastThrM >= 0 && result.stopLeftM >= 0) score = std::max(0.f, result.rwyLenM - result.tdPastThrM - result.stopLeftM);
    else if ((kind == TR_GATES || kind == TR_DAILY) && trialT0 >= 0 && trialT1 > trialT0 && wpIndex >= (int)contract.wps.size()) score = trialT1 - trialT0;
    else if (kind == TR_FORMATION && formT >= kFormationRunS) score = formLost;
  }
  std::string msg = std::string(trialName(kind)) + ": " + trialScore(kind, score);
  if (score >= 0) {
    auto& L = trialBest[contract.id];
    L.push_back(score); std::sort(L.begin(), L.end()); if (L.size() > 5) L.resize(5);
    int rank = 1; for (float v : L) { if (v < score) rank++; }
    msg += rank == 1 ? "  -  a new best!" : fmt("  -  rank %d (best %s)", rank, trialScore(kind, L[0]).c_str());
    saveSettings();
  }
  hubMsg = msg; hubMsgTime = 8;
}
void Game::fireFailure(int kind, int engine) {
  if (!plane.failNow(kind, engine)) return;
  const AircraftSpec& s = *plane.spec;
  std::string m = failureName(kind);
  if ((kind == FAIL_ENGINE_PARTIAL || kind == FAIL_ENGINE_TOTAL) && s.engines > 1) m += fmt(" - engine %d", engine + 1);
  if (kind == FAIL_GEAR_STUCK) m += plane.fail.gearStuck == 1 ? " UP" : " DOWN";
  if (kind == FAIL_PITOT) m += " - airspeed unreliable";
  if (kind == FAIL_ALTERNATOR) m += " - on battery, land soon";
  toast(m, vec3(1, 0.45f, 0.35f), false);   // (shown; the annunciator speaks the failure with its guidance, updateComms)
  g_audio.trigger(SFX_BEEP, 1.f);
  if (plane.apOn && (kind == FAIL_ENGINE_TOTAL || kind == FAIL_ENGINE_PARTIAL || kind == FAIL_PITOT)) {
    plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - " + std::string(failureName(kind)), vec3(1, 0.7f, 0.3f));
  }
  result.failureKinds |= 1 << kind;
}
void Game::updateFailures(float dt) {
  (void)dt;
  // ice: in cloud or precipitation below freezing it builds; clear warm air melts it (about 0 C above 2300 m, or in snow)
  bool freezing = wx.precip == 2 || plane.pos.y > 2300.f;
  float top = wx.cloudBase + 600.f + 1600.f * wx.cloudCover;
  bool inCloud = wx.cloudCover > 0.45f && plane.pos.y > wx.cloudBase && plane.pos.y < top;
  bool wet = inCloud || (wx.precip > 0 && plane.pos.y < top);
  plane.iceFeed = !failuresArmed && plane.fail.ice <= 0.f ? 0.f : (wet && freezing) ? 1.f : (!wet && !freezing) ? -1.f : 0.f;
  if (plane.fail.ice > 0.05f) result.failureKinds |= 1 << FAIL_ICING;
  if (plane.fail.alternator && plane.fail.avionicsDark()) {
    if (plane.apOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot off - battery flat", vec3(1, 0.7f, 0.3f)); }
    if (showMap) { showMap = false; toast("GPS dark - battery flat", vec3(1, 0.45f, 0.35f)); }
  }
  if (failPlan.kind == 0 || failPlan.fired || crashed || plane.onGround) return;
  if (failPlan.kind == FAIL_GEAR_STUCK) {   // waits for the gear to be commanded the other way
    bool wantMove = (plane.ctl.gearDown && plane.gear < 0.99f) || (!plane.ctl.gearDown && plane.gear > 0.01f);
    if (flightClock < failPlan.at || !wantMove) return;
  } else if (flightClock < failPlan.at || plane.agl() < 120.f) return;
  failPlan.fired = true;
  fireFailure(failPlan.kind, failPlan.engine);
}

void Game::shutdown() { saveSettings(); radio.shutdown(); }

std::vector<Game::Annunciator> Game::hudAnnunciators() const {
  const Failures& F = plane.fail;
  const AircraftSpec& spc = *plane.spec;
  std::vector<Annunciator> ann;
  for (int e = 0; e < spc.engines && e < 4; e++) {
    std::string slot = "engine" + std::to_string(e + 1);
    if (F.engineHealth[e] <= 0.f)
      ann.push_back({spc.engines > 1 ? fmt("ENGINE %d FAILED", e + 1) : plane.glideOnly() ? fmt("ENGINE FAILURE  glide %.0f:1, best %.0f kt", plane.glideRatio(), Plane::perf(&spc).vy * MS_TO_KT * 1.1f) : std::string("ENGINE FAILED"), slot, "failed", true});
    else if (F.engineHealth[e] < 0.999f) ann.push_back({spc.engines > 1 ? fmt("ENGINE %d POWER LOSS", e + 1) : std::string("ENGINE POWER LOSS"), slot, "loss", false});
  }
  if (F.alternator) ann.push_back({F.avionicsDark() ? std::string("BATTERY FLAT  no autopilot, no GPS") : fmt("ALTERNATOR  battery %.0f%%", F.battery * 100.f), "alternator", F.avionicsDark() ? "flat" : "battery", F.avionicsDark()});
  if (F.pitot) ann.push_back({"PITOT BLOCKED  airspeed unreliable", "pitot", "blocked", false});
  if (F.gearStuck) ann.push_back({F.gearStuck == 1 ? "GEAR STUCK UP  belly landing: paved, level, slow" : "GEAR STUCK DOWN  slower, more fuel", "gear", F.gearStuck == 1 ? "up" : "down", F.gearStuck == 1});
  if (F.flapAsym) ann.push_back({"FLAP ASYMMETRY  hold the wing up", "flaps", "asym", false});
  if (F.ice > 0.05f) ann.push_back({fmt("ICING %.0f%%  leave the cloud, keep speed", F.ice * 100.f), "ice", F.ice > 0.5f ? "severe" : "icing", F.ice > 0.5f});
  return ann;
}

void Game::toast(const std::string& s, vec3 col, bool voiced) {
  toasts.push_back({s, 0.f, col});
  if (voiced && (screen == SCR_FLIGHT || screen == SCR_LOADING)) commsPending.push_back({s, contract.id});
  if (toasts.size() > 5) toasts.erase(toasts.begin());
}

// ------------------------------------------------------------------ sun & sky
void Game::computeSun(float tod, vec3& dir, vec3& col, float& night) const {
  float a = (tod - 6.f) / 12.f * PI;
  dir = normalize(vec3(cosf(a), sinf(a) * 0.93f, 0.35f + 0.1f * sinf(a)));
  float y = dir.y;
  float od = 1.f / std::max(y * 1.4f + 0.08f, 0.02f);   // optical depth: positive and bounded (x50) once the sun is down
  col = vec3(expf(-0.10f * od * 0.45f), expf(-0.23f * od * 0.45f), expf(-0.56f * od * 0.45f)) * smoothstepf(-0.06f, 0.05f, y);
  col = col * (1.f - 0.75f * wx.cloudCover * wx.cloudCover) * (wx.storm ? 0.6f : 1.f);
  night = smoothstepf(0.06f, -0.14f, y);
}

// ------------------------------------------------------------------ flight session
void Game::startFlight(const Contract& c, int spec, Career::Source src) {
  static bool dispWarned = false;   // say once if the cockpit display shader could not be built on this GPU
  if (!g_ren.dispError.empty() && !dispWarned && !headless) { dispWarned = true; toast("Cockpit display shader failed on this GPU (details in startup.log)", vec3(1.f, 0.45f, 0.35f)); }
  contract = c; specIdx = spec; source = src;
  launchPlan = career.plan(c, spec, src);   // the quote this flight is settled against (fees exactly as shown)
  career.planFuel(launchPlan, c, chosenFuel(c, spec, src, launchPlan));
  applyQuote(c, launchPlan, false);
  researchFlight = false;   // a career flight; launchResearch sets it again for its own
  wx = c.wx; wxStart = c.wx; timeOfDay = wx.timeOfDay; apRepickT = 0;
  const AircraftSpec& s = kAircraft[spec];
  const Airport& a = g_world.airports[c.from];
  // runway into the wind (lessons with rings keep the published runway so the rings line up; the ringless ones, like
  // the cross-country and the checkride, take off into the wind like any other flight)
  float h0 = a.heading;
  bool reverse = false;
  if (c.type != CT_LESSON || c.wps.empty()) {
    float hw0 = cosf((wx.windFrom - h0) * DEG), hw1 = cosf((wx.windFrom - h0 - 180.f) * DEG);
    reverse = hw1 > hw0;
  }
  float hdg = reverse ? h0 + 180.f : h0;
  vec3 start = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
  float payloadKg = (float)c.cargoKg + c.pax * 85.f + 85.f;
  float fuel = chosenFuel(c, spec, src, launchPlan);
  launchFuelKg = -1;
  plane.reset(&s, start, hdg, fuel, payloadKg, c.startAirborne, s.cruise);
  plane.apComfort = c.gentle();   // gentle for passengers or a fragile load, else the airframe's whole envelope (the stick is never limited)
  fuelStart = plane.fuel;
  rollFailures(c, spec, src);
  isolatedFlight = false; jobLeg = false; jobClockBase = 0; attemptFrom = c.from;
  wpIndex = 0; flightClock = 0; crashTimer = 0; endTimer = 0; airBreak = false; crashEndT = 7.5f; gTunnel = 0;
  surveyT = surveyInT = 0; minimumsChecked = false; minimumsGoArounds = 0; trialT0 = trialT1 = -1; formT = formLost = 0; formDone = false;
  hudPrevIas = 0; hudTrend = 0;
  traffic.reset();
  ufo = Ufo(); ufo.next = 180.f + sparkRng.uni() * 240.f;   // first encounter after 3-7 minutes in the air
  paused = false; showMap = false; landed = completed = crashed = false;
  result = FlightResult();
  timeAccel = 1; camMode = camMode == 1 ? 1 : 0; camYaw = 0; camPitch = 0.12f; camZoom = 1; camArm = 0; camArmV = 0; camSpd = 0;
  lookYaw = 0; lookPitch = -0.13f;
  camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  flapNotch = 0; phase = 0; lastHintPhase = -1; hint.clear();
  takeoffAnnounced = c.startAirborne; touchedDown = false; touchdownFpm = 0; stillTimer = 0; tdRunway = -2;   // (an airborne start has no takeoff to announce)
  engineAutoStarted = false; startDelay = 1.2f;
  parkingBrake = !c.startAirborne;   // a start on the ground is parked: the brake is released to roll
  if (!plane.onGround) settleAirborneStart();
  particles.clear(); bursts.clear(); pops.clear(); boomT = -1; for (auto& tt : pieceTrail) tt.clear(); trail.clear(); tipTrail[0].clear(); tipTrail[1].clear(); tipOn = false; trailT = 0; wreck.clear(); debris.clear(); craterR = 0;
  lightning = 0; nextLightning = 6; thunderDelay = -1;
  landingLight = true;
  approachMinAgl = 1e9f; thrPrevAlong = -1e9f; appLow = false; appHigh = false; coaching.clear();
  apWasOn = false; apDest = -1; wraith = WraithState(); g_scenery.resetDamage();
  licenseBefore = career.license;
  // the player sees a loading screen while the scenery around the start is generated (tests fly straight away)
  screen = headless ? SCR_FLIGHT : SCR_LOADING;
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadMap = false; dbgCam = dbgFollow = false;
  atc.cancel(); atcF = AtcFlight(); hintsVoiced.clear(); commsPending.clear();   // (the last flight's calls go before this one's announcements)
  atc.valid = [this](const AtcVoice::Tx& t) { return t.key < 0 || t.key == atcKey(); };
  failVoiced.clear();
  toast(fmt("%s - %s", a.code, a.name), vec3(0.7f, 0.9f, 1.0f));
  toast(fmt("Runway %02d, %s", a.rwyNumber(reverse), wx.describe().c_str()), vec3(0.8f, 0.8f, 0.8f));
  atcF.dep = c.from; atcF.arr = c.to; atcF.depRev = reverse;
  if (c.startAirborne) atcF.phase = 3;
}

void Game::endFlight(bool success, const std::string& reason, FlightOutcome outcome) {
  if (researchFlight) {  // research flights never touch the career: back to the research menu
    researchFlight = false; paused = false; showMap = false;
    screen = SCR_RESEARCH; resOpened = realTime;
    if (!reason.empty()) toast(reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));
    return;
  }
  debriefTitle.clear();
  result.success = success;
  result.outcome = success ? OUT_SUCCESS : outcome;
  result.failReason = reason;
  result.flightMin = flightClock / 60.f;
  result.maxG = plane.maxG; result.minG = plane.minG;
  result.fuelUsedKg = std::max(0.f, fuelStart - plane.fuel);
  result.fuelLeftKg = std::max(0.f, plane.fuel);
  result.fuelLeftFrac = plane.spec->maxFuel > 0 ? clampf(plane.fuel / plane.spec->maxFuel, 0.f, 1.f) : 1.f;
  result.shutDownAtStand = success && plane.onGround && !plane.engineRunning && parkingBrake && g_world.onRunway(plane.pos.x, plane.pos.z, 6.f) < 0;
  result.touchdownFpm = touchdownFpm;
  result.late = contract.timeLimitMin > 0 && (jobClockBase + flightClock) / 60.f > contract.timeLimitMin;
  result.landed = touchedDown && plane.onGround;
  if (!result.landed) result.touchdownFpm = 0;
  if (success && !isolatedFlight) {   // a checkride flown short of its standard is a fail, whatever else went well
    const std::string f = Career::checkrideFault(contract, result);
    if (!f.empty()) { success = false; result.success = false; result.outcome = OUT_CHECKRIDE_FAILED; result.failReason = "Checkride not passed: " + f; debriefTitle = result.failReason; }
  }
  coaching = landingCoaching();
  if (isolatedFlight) {   // a practice flight or a trial: back to the hub, nothing settled
    if (contract.type == CT_TRIAL) finishTrial(success);
    isolatedFlight = false; paused = false; showMap = false; screen = SCR_HUB; hubTab = TAB_CONTRACTS;
    toast(success ? "Practice flight complete" : reason.empty() ? "Practice flight over" : reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));
    return;
  }
  Contract c = contract;
  if (c.type == CT_FERRY) { c.story = false; }
  // the settlement is one transaction: the career only changes when the save succeeds; a failed save keeps the
  // settled copy pending (the debrief shows it, and offers the retry), and a retry can never pay twice
  { std::vector<PayoutLine> lines; int st = 0;
    bool jobLeg = career.job && career.job->state == Career::JobState::ACTIVE && career.job->c.id == c.id;
    bool jobGoesOn = false;
    commit([&](Career& k) {
      k.attemptOpen = false;
      if (!jobLeg) {   // (a free flight flown while a job waits at its stop: the job stays as it is)
        lines = k.settle(c, specIdx, source, result, &st, &launchPlan);
        if (!(k.job && k.job->state == Career::JobState::RECOVERY)) k.job.reset();
        return;
      }
      const AircraftSpec& s = kAircraft[specIdx];
      if (success) { lines = k.settleJob(result, launchPlan, &st); return; }
      // a leg that ended short: the job carries on where the load is (a crash or running dry in the air ends it)
      if (outcome == OUT_DIVERTED && result.divertedTo >= 0) { lines = k.closeLeg(result, launchPlan, result.divertedTo, 0, ""); jobGoesOn = true; }
      else if (outcome == OUT_OFF_AIRPORT && result.landed) {   // the load is brought by road to the nearest airport
        int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z);
        lines = k.closeLeg(result, launchPlan, std::max(ap, 0), source == Career::SRC_LESSON ? 0 : 150 + (int)s.rentFee, "Aircraft and load recovered from the field"); jobGoesOn = true;
      } else if (outcome == OUT_ABANDONED && !crashed) {   // the load goes back to where this leg began
        lines = k.closeLeg(result, launchPlan, attemptFrom, source == Career::SRC_LESSON ? 0 : 150 + (int)s.rentFee, "Load returned to the departure"); jobGoesOn = true;
      } else { lines = k.settle(c, specIdx, source, result, &st, &launchPlan); k.job.reset(); }
    });
    payout = lines; stars = st;
    if (jobGoesOn && career.job) debriefTitle = fmt("%s - job continues from %s", reason.c_str(), g_world.airports[career.job->at].code);
  }
  lastSuccess = success;
  if (debriefTitle.empty()) debriefTitle = success ? (c.type == CT_FERRY ? "Flight complete" : "Contract complete!") : (reason.empty() ? "Flight failed" : reason);
  g_audio.trigger(success ? SFX_SUCCESS : SFX_FAIL);
  if (success && c.payout > 0) g_audio.trigger(SFX_CASH);
  screen = SCR_DEBRIEF;
  paused = false; showMap = false;
}

// One coaching point from what was recorded on the arrival: the most important thing to work on, or what went well
std::string Game::landingCoaching() const {
  const FlightResult& r = result;
  if (r.goArounds > 0 && touchedDown) return r.goArounds == 1 ? "Good call going around: a go-around is always the safe decision when an approach isn't working."
                                                               : fmt("%d go-arounds: set up earlier - on the centreline, at approach speed, by 3 km out.", r.goArounds);
  if (!result.landed || r.tdPastThrM < 0) return "";
  float vref = plane.spec->vref * MS_TO_KT, fpm = touchdownFpm;
  if (fpm > 600) return fmt("Touchdown at %.0f fpm: hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.", fpm);
  if (r.thrKt > vref * 1.3f) return fmt("You crossed the threshold at %.0f kt (aim for about %.0f): slow down earlier on final - extra speed floats and uses up runway.", r.thrKt, vref + 5.f);
  if (r.tdPastThrM > r.rwyLenM * 0.45f) return fmt("You touched down %.0f m past the threshold, beyond the middle of the runway: aim for the first third.", r.tdPastThrM);
  if (r.stopLeftM >= 0 && r.stopLeftM < r.rwyLenM * 0.12f) return fmt("You stopped with only %.0f m of runway to spare: brake firmly once all wheels are down.", std::max(0.f, r.stopLeftM));
  if (r.thrKt > 0 && r.thrKt < vref * 0.95f) return fmt("Slow over the threshold (%.0f kt, near the stall): keep about %.0f kt until the flare.", r.thrKt, vref + 5.f);
  if (r.thrAglM > 40.f) return fmt("High over the threshold (%.0f m): get down onto a 3 degree path earlier.", r.thrAglM);
  std::string well = "Well flown";
  if (r.thrKt > 0) well += fmt(": %.0f kt over the threshold", r.thrKt);
  well += fmt(", down %.0f m in", r.tdPastThrM);
  if (r.stopLeftM >= 0) well += fmt(", %.0f m to spare", r.stopLeftM);
  return well + ".";
}

int Game::computePhase() const {
  if (!plane.spec) return 0;
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (touchedDown && plane.onGround) return 6;
  if ((plane.onGround || flightClock < 1.f) && !takeoffAnnounced) return gs < 3.f ? 0 : 1;   // (settling onto the runway at the start)
  bool finalLeg = contract.wps.empty() || wpIndex >= (int)contract.wps.size() - 1;
  const Airport& d = dest();
  float dd = length(vec3(plane.pos.x - d.x, 0, plane.pos.z - d.z));
  if (finalLeg && !contract.wps.empty() && wpIndex >= (int)contract.wps.size()) finalLeg = true;
  if (finalLeg && (contract.to != contract.from || wpIndex >= (int)contract.wps.size())) {
    if (dd < d.length * 0.5f + 1800.f && plane.agl() < 180.f) return 5;
    if (dd < 6000.f) return 4;
  }
  if (plane.agl() < 300.f && length(vec3(plane.pos.x - g_world.airports[contract.from].x, 0, plane.pos.z - g_world.airports[contract.from].z)) < 4000.f) return 2;
  return 3;
}

// the autopilot is flying a leg where time acceleration is safe (holds, en route, descent orbit - not the approach)
void Game::cycleApDest(int dir) {
  int n = (int)g_world.airports.size();
  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  auto d2 = [&](int i) { float dx = g_world.airports[i].x - plane.pos.x, dz = g_world.airports[i].z - plane.pos.z; return dx * dx + dz * dz; };
  std::sort(order.begin(), order.end(), [&](int a, int b) { return d2(a) < d2(b); });
  if (apDest < 0) apDest = (int)(&dest() - &g_world.airports[0]);   // first pick: the contract's destination
  else {
    int k = (int)(std::find(order.begin(), order.end(), apDest) - order.begin());
    apDest = order[((k + dir) % n + n) % n];
  }
  g_audio.trigger(SFX_CLICK);
}

bool Game::apCruising() const {
  return plane.apOn && (plane.apMode == Plane::AP_HOLD || (plane.apMode == Plane::AP_APPR && plane.apStage == Plane::APS_NAV));
}

void Game::engageAutopilot() {
  if (plane.fail.avionicsDark()) { toast("Autopilot unavailable - battery flat", vec3(1, 0.45f, 0.35f)); return; }
  if (apDest >= 0) {
    plane.apEngage(Plane::AP_NAV, apDest, wx);
    const Airport& a = g_world.airports[apDest];
    if (!plane.apDecline.empty()) toast("Autopilot: unable to autoland at " + plane.apDecline + " - holding heading and height", vec3(1, 0.75f, 0.35f));
    else toast(fmt("Autopilot: AUTOLAND %s runway %02d", a.code, a.rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f));
    toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
  } else {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    toast("Autopilot ON: the stick trims heading and altitude, throttle sets speed", vec3(0.6f, 1, 0.6f));
    toast("Pick an AUTOLAND airport on the GPS map to fly there", vec3(0.8f, 0.8f, 0.8f));
  }
  g_audio.trigger(SFX_CLICK);
}

void Game::flightControls(float dt) {
  Controls& c = plane.ctl;
  auto key = [&](int k) { return in.down[k]; };
  float pitchIn = ((actKey(ACT_PITCH_UP) || key(K_DOWN)) ? 1.f : 0.f) - ((actKey(ACT_PITCH_DN) || key(K_UP)) ? 1.f : 0.f);
  float rollIn = ((actKey(ACT_ROLL_R) || key(K_RIGHT)) ? 1.f : 0.f) - ((actKey(ACT_ROLL_L) || key(K_LEFT)) ? 1.f : 0.f);
  float yawIn = (actKey(ACT_YAW_R) ? 1.f : 0.f) - (actKey(ACT_YAW_L) ? 1.f : 0.f);
  if (set.invertPitch) pitchIn = -pitchIn;
  float padP = 0, padR = 0, padY = 0;
  if (in.pad) {
    auto dz = [](float v) { return fabsf(v) < 0.12f ? 0.f : (v - (v > 0 ? 0.12f : -0.12f)) / 0.88f; };
    padP = -dz(in.ly) * (set.invertPitch ? -1.f : 1.f); padR = dz(in.lx);
    padY = (actPad(ACT_YAW_R) ? 1.f : 0.f) - (actPad(ACT_YAW_L) ? 1.f : 0.f);
    padP += (actPad(ACT_PITCH_UP) ? 1.f : 0.f) - (actPad(ACT_PITCH_DN) ? 1.f : 0.f);
    padR += (actPad(ACT_ROLL_R) ? 1.f : 0.f) - (actPad(ACT_ROLL_L) ? 1.f : 0.f);
    padP = padP * fabsf(padP) * 0.4f + padP * 0.6f; padR = padR * fabsf(padR) * 0.4f + padR * 0.6f;
  }
  bool manual = fabsf(pitchIn) + fabsf(rollIn) > 0 || fabsf(padP) + fabsf(padR) > 0.3f;
  // (a refused autoland's hold too: the pilot asked for a landing, not a hold, and was told the stick takes over - the
  // review of v3.31.0, F3)
  bool apNav = plane.apOn && (plane.apMode == Plane::AP_APPR || plane.apMode == Plane::AP_STUNT || (plane.apMode == Plane::AP_HOLD && !plane.apDecline.empty()));
  if (plane.apOn) {
    if (apNav) {   // flying a GPS route / autoland: any real stick input hands control back
      if (fabsf(pitchIn + padP) > 0.5f || fabsf(rollIn + padR) > 0.5f) {
        plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
      }
    } else {       // hold mode: the stick moves the targets (A/D heading, W/S altitude)
      plane.apHeading = wrapDeg360(plane.apHeading + (rollIn + padR) * 20.f * dt);
      plane.apAlt = std::max(plane.apAlt + (pitchIn + padP) * 45.f * dt, g_world.height(plane.pos.x, plane.pos.z) + 60.f);
    }
  }
  if (!plane.apOn) {
    float tp = clampf(pitchIn + padP, -1, 1), tr = clampf(rollIn + padR, -1, 1);
    float rate = manual ? 2.8f : 5.f;
    c.pitch = approach(c.pitch, tp, rate, dt);
    c.roll = approach(c.roll, tr, rate, dt);
  }
  (void)manual;
  // XR-40 weapons hot: the bumpers fire and bomb, so the rudder is frozen until weapons are safe again
  if (plane.spec->special == 2 && wraith.armed && !plane.onGround) { yawIn = 0; padY = 0; }
  c.yaw = approach(c.yaw, clampf(yawIn + padY, -1, 1), 4.f, dt);
  // throttle
  float thr = 0;
  if (actDown(ACT_THR_UP) || key(K_PGUP) || key(K_PLUS)) thr += 0.55f;
  if (actDown(ACT_THR_DN) || key(K_PGDN) || key(K_MINUS)) thr -= 0.55f;
  if (in.pad) thr += (in.rt - in.lt) * 0.6f;
  bool numKey = in.pressed['0'];
  for (int k = 1; k <= 9; k++) numKey |= in.pressed['0' + k] && !actKey(ACT_THR_DN);
  if (plane.apOn && plane.apMode == Plane::AP_HOLD && plane.apSpeed > 0 && numKey) { plane.apSpeed = 0; toast("Autothrottle off - manual throttle", vec3(1, 0.85f, 0.5f)); }
  if (apNav) {}   // the autopilot has the throttle
  else if (plane.apOn && plane.apSpeed > 0) {   // autothrottle: throttle inputs move the speed target
    const AircraftSpec& sp = *plane.spec;
    plane.apSpeed = clampf(plane.apSpeed + thr * 30.f * dt, sp.vref * 1.2f, sp.cruise * (sp.special ? 1.6f : 1.15f));
  } else {
    c.throttle = clampf(c.throttle + thr * dt, 0, 1);
    for (int k = 1; k <= 9; k++) if (in.pressed['0' + k] && !actKey(ACT_THR_DN)) c.throttle = k / 9.f;
    if (in.pressed['0']) c.throttle = 0;
  }
  // trim
  float tr = (actDown(ACT_TRIM_UP) || key(K_HOME) ? 1.f : 0.f) - (actDown(ACT_TRIM_DN) || key(K_END) ? 1.f : 0.f);
  c.trim = clampf(c.trim + tr * 0.35f * dt, -1, 1);
  // flaps
  auto flapToast = [&]() {
    if (plane.spec->special == 2) toast(flapNotch > 0.99f ? "Pods 90 deg - VTOL hover" : fmt("Pods %d deg", (int)lroundf(flapNotch * 90)), vec3(0.8f, 0.55f, 1));
    else toast(fmt("Flaps %d%%", (int)lroundf(flapNotch * 100)), vec3(0.8f, 0.9f, 1));
  };
  if (apNav) flapNotch = c.flaps;   // the autopilot runs the flaps on the approach
  else if (plane.spec->special == 1) flapNotch = 0;   // the XR-30 has no flaps: a delta wing and canards
  else {
    if (actPressed(ACT_FLAPS_DN)) { flapNotch = std::min(1.f, flapNotch + 1.f / 3.f); flapToast(); }
    if (actPressed(ACT_FLAPS_UP)) { flapNotch = std::max(0.f, flapNotch - 1.f / 3.f); flapToast(); }
  }
  c.flaps = flapNotch;
  // gear
  bool wrAir = plane.spec->special == 2 && !plane.onGround;   // XR-40 in the air: gamepad Y is weapons hot / safe instead of the gear
  bool gearPad = actPadP(ACT_GEAR) && !(wrAir && set.padBind[ACT_GEAR] == set.padBind[ACT_WEAPONS]);
  if ((actKeyP(ACT_GEAR) || gearPad || (!showMap && (in.buttonsPressed & PAD_RIGHT))) && plane.spec->retract) {
    if (plane.onGround && c.gearDown) toast("Gear lever is locked on the ground", vec3(1, 0.6f, 0.4f));
    else { c.gearDown = !c.gearDown; toast(c.gearDown ? "Gear down" : "Gear up", vec3(0.8f, 1, 0.8f)); }
    if (plane.fail.gearStuck) toast(plane.fail.gearStuck == 1 ? "Gear won't come down - it's stuck up" : "Gear won't retract - it's stuck down", vec3(1, 0.45f, 0.35f));
  }
  // brakes: B / D-pad left = parking brake toggle, Space = wheel brakes
  bool& parking = parkingBrake;   // (set by startFlight for a start on the ground)
  if (plane.apDone) {
    plane.apDone = false; parking = true;
    if (plane.apOverrun) toast("Autoland: stopped past the end of the runway - parking brake set", vec3(1, 0.45f, 0.35f));
    else toast("Autoland complete - parking brake set", vec3(0.5f, 1, 0.6f));
    plane.apOverrun = false; g_audio.trigger(SFX_AP_DISC, 0.7f);
  }
  if (plane.spec->special == 2) wraithControls(dt);
  if (actKeyP(ACT_PARK) || (!showMap && actPadP(ACT_PARK))) { parking = !parking; toast(parking ? "Parking brake SET" : "Parking brake released", vec3(1, 0.85f, 0.5f)); }
  float wb = actDown(ACT_BRAKE) ? 1.f : 0.f;
  if (wb > 0 && parking && plane.onGround && length(plane.vel) > 2.f) parking = false;
  c.brake = parking ? 1.f : wb;
  // autopilot: Z / right stick click. With an autoland airport picked on the GPS it flies there and lands;
  // otherwise it holds heading, altitude and speed
  if (actPressed(ACT_AP)) {
    if (plane.apOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot OFF", vec3(1, 0.7f, 0.3f)); }
    else if (plane.onGround) toast("Autopilot needs to be airborne", vec3(1, 0.6f, 0.4f));
    else engageAutopilot();
  }
  // aerobatics: the autopilot flies the next figure, sized to this aircraft (it climbs or dives for the height and
  // speed first); pressed again it stops the figure and recovers to level flight
  if (actPressed(ACT_STUNT)) {
    if (plane.onGround) toast("Aerobatics need to be airborne", vec3(1, 0.6f, 0.4f));
    else if (plane.apOn && plane.apMode == Plane::AP_STUNT) { plane.apStuntStop(); toast("Aerobatics: recovering to level flight", vec3(1, 0.85f, 0.5f)); }
    else {
      plane.apStuntBegin(stuntNext, wx);
      toast(fmt("Aerobatics: %s", Plane::stuntName(plane.apStunt)), vec3(0.6f, 1, 0.6f));
      toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      stuntNext = (stuntNext + 1) % Plane::STUNT_COUNT;
      g_audio.trigger(SFX_CLICK);
    }
  }
  if (actPressed(ACT_LIGHTS)) { landingLight = !landingLight; toast(landingLight ? "Landing lights ON" : "Landing lights OFF"); }
  if (actPressed(ACT_ENGINE) && !plane.engineRunning && plane.fuel > 0) { plane.starterTime = 0.01f; toast("Engine start"); }
  // time acceleration
  if (actPressed(ACT_TIME)) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    if (!apCruising() && (plane.onGround || plane.agl() < 250.f || dd < 3500.f)) { timeAccel = 1; toast("Time acceleration only available in cruise", vec3(1, 0.7f, 0.4f)); }
    else { timeAccel = timeAccel >= 4 ? 1 : timeAccel * 2; toast(fmt("Time x%.0f", timeAccel)); }
  }
}

void Game::spawn(vec3 p, vec3 v, float life, float size, float grow, vec3 col, float alpha, int kind, float drag, float buoy) {
  if (particles.size() > 9000) return;
  particles.push_back({p, v, life, life, size, grow, col, alpha, kind, drag, buoy, false, true});
}

// checkpoint gate i: centre and half-extent axes (gate faces along the leg leading to it)
bool Game::ringGeom(int i, vec3& c, vec3& ax, vec3& ay) const {
  if (i < 0 || i >= (int)contract.wps.size()) return false;
  const Waypoint& w = contract.wps[i];
  c = vec3(w.x, w.alt, w.z);
  vec3 prev = i == 0 ? g_world.airports[contract.from].pos() : vec3(contract.wps[i - 1].x, contract.wps[i - 1].alt, contract.wps[i - 1].z);
  vec3 nd = normalize(vec3(c.x - prev.x, 0, c.z - prev.z) + vec3(0.001f, 0, 0));
  ax = normalize(cross(nd, vec3(0, 1, 0))) * 90.f; ay = vec3(0, 90.f, 0);
  return true;
}

void Game::updateFlight(float dt) {
  if (paused) return;
  if (!botControl) flightControls(dt);
  // automatic engine start sequence after the scene fades in
  if (!engineAutoStarted && !contract.startAirborne) {
    startDelay -= dt;
    if (startDelay <= 0) { engineAutoStarted = true; plane.starterTime = 0.01f; toast("Starting engine..."); }
  }
  bool wasRunning = plane.engineRunning;
  // (time acceleration is switched off before this frame's step, not after it: a 4x frame on the approach was one
  // too many)
  if (timeAccel > 1) {
    float dd = length(vec3(plane.pos.x - dest().x, 0, plane.pos.z - dest().z));
    bool approach = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV;
    if ((!apCruising() && (plane.agl() < 250.f || dd < 3500.f)) || approach || plane.onGround || crashed) { timeAccel = 1; toast("Time acceleration off"); }
  }
  float simDt = dt * timeAccel;
  vec3 prevPos = plane.pos;
  if (!crashed) {
    updateWeather(simDt);
    updateFailures(simDt);
    updateResearchCard(simDt);
    plane.step(simDt, wx, gameTime);
    flightClock += simDt;
    timeOfDay += simDt / 3600.f;
    if (plane.ev.bellyLanding && !result.bellyLanding) { result.bellyLanding = true; toast("Belly landing - hold it straight", vec3(1, 0.6f, 0.3f)); g_audio.trigger(SFX_CRASH, 0.4f); }
  }
  gameTime += simDt;
  if (plane.engineRunning && !wasRunning) {
    // starter catches: puff of smoke from the exhausts
    vec3 ex = plane.pos + plane.q.rotate(vec3(0.4f, -plane.spec->fusRad * 0.6f, -plane.spec->fusLen * 0.35f));
    for (int i = 0; i < 14; i++) spawn(ex, plane.q.rotate(vec3(0.8f + i * 0.05f, -0.5f, 2.f)) + vec3(0, 0.6f, 0), 2.5f, 0.6f, 1.6f, vec3(0.55f, 0.58f, 0.62f), 0.55f, SPR_SMOKE, 1.5f, 0.3f);
    if (!takeoffAnnounced && contract.type == CT_LESSON) toast("Engine running. Release the parking brake with " + keyName(set.keyBind[ACT_PARK]) + ".", vec3(0.7f, 1, 0.7f));   // ("B" by default)
    else if (!takeoffAnnounced) toast("Engine running.", vec3(0.7f, 1, 0.7f));   // (the takeoff clearance is the tower's: updateAtc)
  }
  // gear / flap motor cues
  if (prevGear > 0.99f && plane.gear < 0.99f) g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
  if (prevGear < 0.99f && plane.gear >= 0.99f) g_audio.trigger(SFX_GEAR_CLUNK, 1.0f);
  prevGear = plane.gear; prevFlaps = plane.flaps;
  // weather effects
  if (wx.storm) {
    nextLightning -= dt;
    if (nextLightning <= 0) { lightning = 1.f; nextLightning = 6.f + (rand() % 1000) * 0.015f; thunderDelay = 0.8f + (rand() % 100) * 0.03f; }
    if (thunderDelay > 0) { thunderDelay -= dt; if (thunderDelay <= 0) g_audio.trigger(SFX_THUNDER, 0.7f + (rand() % 30) * 0.01f); }
  }
  lightning = std::max(0.f, lightning - dt * 4.f) * (lightning > 0.5f ? 1.f : (rand() % 3 ? 1.f : 0.3f));
  cloudOff = cloudOff + vec2(-sinf(wx.windFrom * DEG), cosf(wx.windFrom * DEG)) * (wx.windSpeed * 2.f * simDt);

  updateUfo(simDt);
  // AI traffic
  traffic.enabled = set.traffic;
  airlineTraffic(simDt);
  if (traffic.update(simDt, plane.pos, plane.vel, plane.onGround || crashed, plane.spec->span) && !crashed && !plane.ev.crashed) {
    plane.ev.crashed = true; plane.ev.crashReason = "Mid-air collision";
  }
  for (auto& f : traffic.puffs) spawn(f.p, f.v, f.life, f.size, f.grow, f.col, f.alpha, f.kind, 1.f, 0.f);
  for (auto& b : traffic.booms) g_audio.trigger(SFX_BOOM, b.second);
  updateWraith(simDt);
  for (float f : traffic.flybys) g_audio.trigger(SFX_FLYBY, f);
  for (auto& m : traffic.radio) toast(m, vec3(1.f, 0.78f, 0.3f));
  // entertainment: O + P held for a second while flying summons the Spectre display pair (again: sends them home).
  // They fly their show around you and bow out when you land or settle onto an approach
  {
    bool flying = !crashed && !plane.onGround;
    if (flying && in.down['O'] && in.down['P']) {
      escortSummon += dt;
      if (escortSummon >= 1.f && !escortLatch) {
        escortLatch = true;
        if (traffic.escortActive()) traffic.dismissEscort();
        else if (traffic.escortStop) toast("Spectre display pair: climb away from the field first", vec3(1.f, 0.78f, 0.3f));
        else { traffic.spawnEscort(plane.pos, plane.vel); g_audio.trigger(SFX_CHIME, 0.8f); }
      }
    } else { escortSummon = 0; escortLatch = false; }
    float dn; int na = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dn);
    (void)na;
    bool onApproach = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV && plane.apStage != Plane::APS_GOAROUND;
    traffic.escortStop = crashed || plane.onGround || onApproach || (plane.agl() < 150.f && dn < 4000.f);
    // the formation run (C12): the pair joins once the player is up; the run clocks the time with them alongside and
    // the time the player was not a steady platform (steep bank, hard g, a quick roll)
    if (contract.type == CT_TRIAL && contract.id == trialId(TR_FORMATION) && !formDone) {
      if (flying && !traffic.escortActive() && !traffic.escortStop && plane.agl() > 300.f && formT <= 0.f) { traffic.spawnEscort(plane.pos, plane.vel); g_audio.trigger(SFX_CHIME, 0.8f); }
      if (traffic.escortActive() && traffic.escAct != Traffic::ESC_JOIN) {
        formT += simDt;
        bool steady = fabsf(plane.bankDeg()) < 35.f && fabsf(plane.gLoad - 1.f) < 0.5f && fabsf(plane.w.z) < 0.7f && !plane.onGround;
        if (!steady) formLost += simDt;
        if (formT >= kFormationRunS) {
          formDone = true; traffic.dismissEscort();
          toast(fmt("Formation run complete - %.0f s out of a steady platform. Land back at %s.", formLost, g_world.airports[contract.to].code), vec3(0.5f, 1.f, 0.6f)); g_audio.trigger(SFX_SUCCESS);
        }
      }
    }
  }
  {
    // g-force tunnel: a faint red tint from the first noticeable g that slowly closes into the full ring as the load
    // nears the airframe's limit (regular aircraft 1.8 -> 6 g, the XR-30's damped cell 4 -> 50 g; negative g from
    // -0.5 g). It builds over ~1 s and recovers over ~1.5 s
    bool jet = plane.spec->special != 0;
    float gp = smoothstepf(jet ? 4.f : 1.8f, jet ? 50.f : 6.f, plane.gLoad), gn = smoothstepf(jet ? -2.f : -0.5f, jet ? -25.f : -3.f, plane.gLoad);
    float target = crashed ? 0.f : std::max(gp, gn);
    gTunnel = approach(gTunnel, target, target > gTunnel ? 1.2f : 0.7f, dt);
  }
  if (crashed) {
    crashTimer += dt;
    // skip the crash sequence: A on the gamepad (or Enter / Space) goes straight to the results
    if (screen == SCR_FLIGHT && crashTimer > 0.8f && ((in.buttonsPressed & PAD_A) || in.pressed[K_ENTER] || in.pressed[' '])) {
      in.buttonsPressed &= ~PAD_A; in.pressed[K_ENTER] = in.pressed[' '] = false;   // don't let the same press click the results screen
      in.mPressed[0] = false; in.mDown[0] = false; padHoldA = false;
      endFlight(false, plane.ev.crashReason);
      return;
    }
    updateWreck(dt);
    camYaw += dt * 0.12f;   // slow orbit around the crash site
    if (crashTimer > crashEndT && screen == SCR_FLIGHT) endFlight(false, plane.ev.crashReason);
    return;
  }
  if (plane.ev.crashed) {
    vec3 impactVel = plane.vel;
    crashed = true;
    g_audio.trigger(SFX_CRASH);
    toast(plane.ev.crashReason, vec3(1, 0.4f, 0.3f));
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.5f;
    bool air = plane.ev.crashReason.find("Structural") != std::string::npos && plane.agl() > 4.f;   // overstressed in flight: it comes apart in the air
    breakUp(impactVel, water, air);
    plane.vel = vec3(); plane.w = vec3();
    if (camMode == 1 || camMode == 3) camMode = 2;
    camZoom = std::max(camZoom, air ? 2.6f : 1.8f); camPitch = 0.3f;
    return;
  }
  // takeoff
  if (!takeoffAnnounced && !plane.onGround && plane.agl() > 8.f) {
    takeoffAnnounced = true;
    toast("Positive climb!", vec3(0.7f, 1, 0.7f));
  }
  // the arrival at the destination, for the debrief's coaching: speed and height over the threshold of the runway
  // end being flown towards, where it touched down, and any go-around from a low approach
  if (!researchFlight) {
    const Airport& A = g_world.airports[contract.to];
    bool rev = dot(plane.vel, A.dir()) < 0.f;
    vec3 ld = rev ? -A.dir() : A.dir(), rel = plane.pos - A.threshold(rev); rel.y = 0;
    float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (!plane.onGround && lat < A.width * 2.f + 30.f && thrPrevAlong < 0.f && along >= 0.f && along < 300.f) {
      result.thrKt = plane.ias * MS_TO_KT; result.thrAglM = plane.pos.y - A.elev - plane.gearHeight();
    }
    thrPrevAlong = lat < 600.f ? along : -1e9f;
    // a go-around: down low on an approach (having been up and away first, and descending: not the climb-out from the
    // same field) and then climbing back above 150 m without touching down
    if (!plane.onGround && plane.agl() > 150.f) appHigh = true;
    if (appHigh && !plane.onGround && dA < 3000.f && plane.agl() < 60.f && plane.vel.y < 0.f) appLow = true;
    if (appLow && !plane.onGround && plane.agl() > 150.f) { appLow = false; result.goArounds++; result.thrKt = -1; }
    if (plane.ev.touchdown && takeoffAnnounced && lat < A.width && along > -50.f && along < A.length) { result.tdPastThrM = std::max(0.f, along); result.rwyLenM = A.length; result.centerlineErr = lat; }
    if (plane.ev.touchdown && takeoffAnnounced && atcF.goAround) result.landedAgainstGoAround = true;
    if (plane.onGround) { appLow = false; appHigh = false; if (result.tdPastThrM >= 0 && length(plane.vel) > 1.f) result.stopLeftM = A.length - along; }   // (while still rolling: its direction says which end)
  }
  // touchdown
  if (plane.ev.touchdown && takeoffAnnounced) {
    float fpm = -plane.ev.touchdownVs * 196.85f;
    touchdownFpm = fpm; touchedDown = true;
    tdRunway = g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);   // (the arrival's evidence: the last touchdown's runway, or -1 off every runway)
    g_audio.trigger(SFX_TOUCHDOWN, clampf(fpm / 600.f, 0.15f, 1.f));
    std::string r = fpm < 120 ? "BUTTER!" : fpm < 250 ? "Smooth landing" : fpm < 450 ? "Good landing" : fpm < 650 ? "Firm landing" : "HARD landing!";
    toast(fmt("%s  %.0f fpm", r.c_str(), fpm), fpm < 450 ? vec3(0.6f, 1, 0.6f) : vec3(1, 0.6f, 0.3f));
    float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
    for (int side = -1; side <= 1; side += 2) {
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * std::max(1.2f, plane.spec->span * 0.13f), -plane.gearHeight(), 0));
      int n = (int)clampf(gs * 0.3f, 4, 18);
      for (int i = 0; i < n; i++) spawn(wp, plane.vel * 0.3f + vec3((rand() % 100 - 50) * 0.02f, 0.5f, (rand() % 100 - 50) * 0.02f), 2.5f + (rand() % 100) * 0.01f, 0.6f, 1.8f, vec3(0.75f, 0.75f, 0.76f), 0.45f, SPR_SMOKE, 2.f, 0.2f);
    }
  }
  if (!plane.onGround) result.maxBank = std::max(result.maxBank, fabsf(plane.bankDeg()));
  // rolling dust / spray
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  updateJobMeters(simDt, gs);   // (simulated time: at 4x the patient rides four seconds of turbulence a frame)
  if (plane.onGround && plane.groundRough > 0.3f && gs > 4.f) {
    dustAccum += dt * gs * 0.6f;
    int rw = g_world.onRunway(plane.pos.x, plane.pos.z, 3);
    int surf = rw >= 0 ? g_world.airports[rw].surface : SURF_GRASS;
    vec3 col = surf == SURF_SAND ? vec3(0.8f, 0.72f, 0.55f) : surf == SURF_SNOW ? vec3(0.95f, 0.96f, 1.f) : surf == SURF_GRAVEL ? vec3(0.55f, 0.5f, 0.45f) : vec3(0.45f, 0.42f, 0.32f);
    while (dustAccum > 1) {
      dustAccum -= 1;
      int side = rand() & 1 ? 1 : -1;
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * std::max(1.2f, plane.spec->span * 0.13f), -plane.gearHeight(), 0.5f));
      spawn(wp, plane.vel * 0.15f + vec3(0, 0.8f, 0), 2.5f, 0.8f, 2.2f, col, surf == SURF_GRASS ? 0.15f : 0.35f, SPR_SMOKE, 1.5f, 0.1f);
    }
  }
  // prop wash dust on rough ground
  if (plane.onGround && plane.groundRough > 0.3f && plane.engineSpool > 0.5f && gs < 15.f && (rand() % 3 == 0)) {
    vec3 bp = plane.pos + plane.q.rotate(vec3((rand() % 100 - 50) * 0.04f, -plane.gearHeight(), 3.f));
    spawn(bp, plane.q.rotate(vec3(0, 0.3f, 8.f)), 1.5f, 0.6f, 2.f, vec3(0.6f, 0.55f, 0.45f), 0.18f, SPR_SMOKE, 2.f, 0.f);
  }
  // wingtip vapour in humid air under g: a continuous ribbon from each tip. Points are dropped at the tips and stay
  // in the air mass (drifting with the wind); the strip between them widens and thins out as it ages.
  for (int sd = 0; sd < 2; sd++) {
    auto& tt = tipTrail[sd];
    for (auto& q : tt) { q.age += simDt; q.p += plane.windVel * simDt; }
    while (!tt.empty() && tt.front().age > 1.8f) tt.erase(tt.begin());
  }
  float vapK = plane.onGround ? 0.f : clampf((plane.gLoad - 1.7f) / 1.5f, 0.f, 1.f) * ((wx.precip > 0 || plane.pos.y > wx.cloudBase - 300.f) ? 1.f : 0.f);
  if (vapK > 0.f || tipOn) {
    if (vapK > 0.f && !tipOn) tipSeg++;
    vec3 tip = plane.spec->special == 2 ? kWraithWingTip : plane.spec->special ? kJetWingTip : modelWingTip(kModels[plane.spec - kAircraft]);
    for (int sd = 0; sd < 2; sd++) {
      float sx = sd ? 1.f : -1.f;
      tipTrail[sd].push_back({plane.pos + plane.q.rotate(vec3(sx * tip.x, tip.y, tip.z + 0.3f)), 0.f, vapK, tipSeg});
      if (tipTrail[sd].size() > 400) tipTrail[sd].erase(tipTrail[sd].begin());
    }
    tipOn = vapK > 0.f;
  }
  if (plane.spec->special) jetEffects(simDt);
  // the edge of the chart: warned well before it (past 1.2x the half-width the flight is lost)
  {
    float edge = std::max(fabsf(plane.pos.x), fabsf(plane.pos.z)) / WORLD_HALF;
    edgeWarnT -= dt;
    if (edge > 1.04f && edgeWarnT <= 0) {
      edgeWarnT = 8.f; g_audio.trigger(SFX_BEEP);
      toast(edge > 1.12f ? "LEAVING THE CHART - TURN BACK NOW" : "Approaching the edge of the chart - turn back", vec3(1, 0.5f, 0.2f));
    }
  }
  // GPS breadcrumb trail
  trailT += dt;
  if (trailT > 2.f && !plane.onGround) { trailT = 0; trail.push_back(vec2(plane.pos.x, plane.pos.z)); if (trail.size() > 500) trail.erase(trail.begin()); }
  // waypoints
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    vec3 wp(w.x, w.alt, w.z);
    vec3 gc, gx, gy;
    if (ringGeom(wpIndex, gc, gx, gy)) {
      // energy sparks drifting around the active gate's rim
      sparkAccum += dt * 70.f;
      Rng& r = sparkRng;
      while (sparkAccum >= 1.f) {
        sparkAccum -= 1.f;
        float a = r.range(0, 6.2832f), rr = r.range(0.8f, 0.92f);
        vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
        vec3 tang = gx * (-sinf(a) / 90.f) + gy * (cosf(a) / 90.f);
        vec3 nrm = normalize(cross(gx, gy));
        spawn(gc + radial * (rr * 90.f), tang * r.range(6.f, 16.f) + radial * r.range(-8.f, 2.f) + nrm * r.range(-4.f, 4.f), r.range(0.8f, 1.6f), r.range(2.5f, 5.f), -1.5f,
              vec3(0.3f, 1.f, 0.6f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, 0.f);
      }
    }
    float cpDist = length(plane.pos - wp);
    {   // the closest approach of this frame's movement to the ring centre: a fast aircraft can't skip through a ring
      vec3 d = plane.pos - prevPos; float L2 = dot(d, d);
      if (L2 > 1e-4f) { float u = clampf(dot(wp - prevPos, d) / L2, 0.f, 1.f); cpDist = std::min(cpDist, length(wp - (prevPos + d * u))); }
    }
    if (cpDist < 110.f) {
      // gate burst: shockwave + a shower of sparks flung outward
      if (ringGeom(wpIndex, gc, gx, gy)) {
        bursts.push_back({gc, gx, gy, vec3(0.3f, 1.f, 0.6f), 0.f});
        Rng& r = sparkRng;
        for (int k = 0; k < 160; k++) {
          float a = r.range(0, 6.2832f);
          vec3 radial = gx * (cosf(a) / 90.f) + gy * (sinf(a) / 90.f);
          vec3 nrm = normalize(cross(gx, gy));
          spawn(gc + radial * 78.f, radial * r.range(30.f, 90.f) + nrm * r.range(-25.f, 25.f) + plane.vel * 0.3f, r.range(0.8f, 1.8f), r.range(3.f, 6.f), -1.5f,
                (k % 3 ? vec3(0.4f, 1.f, 0.7f) : vec3(1.f, 1.f, 0.8f)) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 1.4f, -3.f);
        }
      }
      wpIndex++; result.wpDone = wpIndex;
      if (contract.type == CT_TRIAL) { if (wpIndex == 1) trialT0 = flightClock; if (wpIndex >= (int)contract.wps.size()) trialT1 = flightClock; }
      g_audio.trigger(SFX_CHIME);
      toast(wpIndex < (int)contract.wps.size() ? fmt("Checkpoint %d of %d", wpIndex, (int)contract.wps.size()) : "All checkpoints passed!", vec3(0.5f, 1, 0.7f));
      if (contract.wps.size() && wpIndex >= (int)contract.wps.size() && contract.id == "L1") { endTimer = 2.f; }
    }
  }
  if (endTimer > 0) { endTimer -= dt; if (endTimer <= 0) { endFlight(true, ""); return; } }
  // fuel warnings
  static bool lowFuelWarned = false;
  if (flightClock < 0.1f) lowFuelWarned = false;
  if (!lowFuelWarned && plane.fuel < plane.spec->maxFuel * 0.15f) { lowFuelWarned = true; g_audio.trigger(SFX_BEEP); toast("LOW FUEL", vec3(1, 0.5f, 0.2f)); }
  // completion
  if (plane.onGround && takeoffAnnounced && gs < 2.5f && !researchFlight) {
    stillTimer += dt;
    if (stillTimer > 1.2f) {
      float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
      bool atField = ap >= 0 && dA < g_world.airports[ap].length * 0.5f + 600.f;
      // an arrival is a landing on that field's runway (taxiing off to the apron afterwards is fine): the last
      // touchdown's runway, or where it stands when no touchdown was seen (a flight placed on the ground); stopping
      // near a field after landing beside or beyond its runway is an off-field landing, the load recovered by road
      const int rwy = tdRunway != -2 ? tdRunway : g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);
      if (wpIndex < (int)contract.wps.size()) {
        if (stillTimer >= 1.25f && stillTimer - dt < 1.25f) toast("Checkpoints remaining - take off again to continue", vec3(1, 0.8f, 0.4f));
      } else if (atField && rwy == ap && ap == contract.to) { endFlight(true, ""); return; }
      else if (atField && rwy == ap) { result.divertedTo = ap; endFlight(false, fmt("Diverted to %s", g_world.airports[ap].name), OUT_DIVERTED); return; }
      else if (stillTimer > 3.f) { endFlight(false, atField ? "Landed off the runway" : "Landed off-airport", OUT_OFF_AIRPORT); return; }
    }
  } else stillTimer = 0;
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && !takeoffAnnounced) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && takeoffAnnounced) {
    // stopped dry: done, unless this is the destination with nothing left to fly (the completion check above ends
    // that one as a success). With checkpoints still to collect there's no way to take off again.
    float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
    bool wpsLeft = wpIndex < (int)contract.wps.size();
    if (wpsLeft) { endFlight(false, "Out of fuel with checkpoints remaining", OUT_OUT_OF_FUEL); return; }
    if (!(ap == contract.to && dA < 2000)) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  }
  (void)prevPos;
  // tutorial hints
  phase = computePhase();
  if (contract.hints.size() > (size_t)phase && phase != lastHintPhase) {
    lastHintPhase = phase;
    if (!contract.hints[phase].empty()) {
      hint = expandHint(contract.hints[phase]);
      if (set.showHints && std::find(hintsVoiced.begin(), hintsVoiced.end(), hint) == hintsVoiced.end()) { hintsVoiced.push_back(hint); commsPending.push_back({hint, contract.id}); }   // each said once
    }
  }
}

// ------------------------------------------------------------------ pre-flight loading screen
// After a flight is chosen the world is held still while the scenery around the start position is generated (with a
// bigger per-frame budget), behind a card showing an aerial image of the airport. Once nothing is left to stream it
// cross-fades into a live establishing shot of the aircraft, and the player starts the flight when ready.
void Game::updateLoading(float dt) {
  loadT += dt;
  if (loadReadyT < 0) {
    dbgCam = false;
    updateCamera(dt);   // stream around the camera the flight will open with
    g_ren.entBudgetMs = 14.f;
    int pend = g_ren.entPending;
    if (loadT > 0.05f) loadPend0 = std::max(loadPend0, pend);
    float prog = loadPend0 > 0 ? 1.f - (float)pend / loadPend0 : clampf(loadT / 0.8f, 0.f, 1.f);
    loadShown = std::max(loadShown, loadShown + (prog - loadShown) * (1.f - expf(-dt * 8.f)));
    if (pend == 0 && loadT > (researchFlight ? 0.15f : 0.8f)) { loadReadyT = loadT; loadShown = 1.f; g_ren.entBudgetMs = 2.5f; }
    // a research sortie: the preview has already streamed its airport, so the flight opens at once (no establishing shot)
    if (loadReadyT >= 0 && researchFlight && !headless) {
      screen = SCR_FLIGHT; dbgCam = false;
      if (!resAirborne) { camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15)); }
      return;
    }
  } else {
    // establishing shot: a slow orbit round the aircraft on the ground, or a camera flying alongside in the air
    float t = loadT - loadReadyT, size = std::max(plane.spec->span, plane.spec->fusLen);
    dbgCam = true; dbgFollow = false;
    if (plane.onGround) {
      float a = plane.heading() * DEG + 2.2f + t * 0.07f, R = size * 1.9f + 10.f;
      dbgCamPos = plane.pos + vec3(sinf(a) * R, R * 0.32f + 2.f, -cosf(a) * R);
      dbgCamLook = plane.pos + vec3(0, size * 0.1f, 0);
    } else {
      float R = size * 2.2f + 8.f;
      dbgCamPos = plane.pos + plane.right() * (R * 0.85f) + plane.forward() * (R * (0.7f - 0.04f * t)) + vec3(0, R * 0.18f, 0);
      dbgCamLook = plane.pos + plane.forward() * (size * 0.2f);
    }
    bool go = in.pressed[K_ENTER] || in.pressed[' '] || in.mPressed[0] || (in.buttonsPressed & PAD_A);
    if (go && t > 0.5f) {
      screen = SCR_FLIGHT; dbgCam = false;
      camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
      in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false; in.buttonsPressed &= ~PAD_A;
      g_audio.trigger(SFX_CLICK);
    }
  }
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) {   // back out to where the flight was chosen
    dbgCam = false; g_ren.entBudgetMs = 2.5f;
    // a career flight that never left the loading screen: its attempt closes and its job waits at its stop, as after
    // an interrupted session (nothing is charged: no leg was flown); an unsaved close shows as pending and blocks launches
    if (career.attemptOpen && !isolatedFlight && !researchFlight)
      commit([](Career& k) { k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
    screen = researchFlight ? SCR_RESEARCH : SCR_HUB;
    researchFlight = false;   // (nothing of the cancelled session carries into the next flight)
  }
}

// ------------------------------------------------------------------ camera
void Game::updateCamera(float dt) {
  if (!plane.spec) return;
  if (actPressed(ACT_CAMERA)) {
    camMode = (camMode + 1) % 4; camYaw = 0; camPitch = 0.12f; lookYaw = 0; lookPitch = -0.13f;
    hudOn = set.hudCam[camMode];   // (the HUD as it was last left in this view)
    static const char* names[] = {"Chase camera", "Cockpit view", "Orbit camera", "Flyby camera"};
    toast(names[camMode]);
    if (camMode == 3) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 350.f + plane.right() * 40.f + vec3(0, 12, 0);
  }
  if (in.wheel != 0 && showMap) gpsRangeTarget = clampf(gpsRangeTarget * powf(0.8f, in.wheel), 1500.f, 40000.f);
  else if (in.wheel != 0 && !showRadio && camMode == 1) ckZoomT = clampf(ckZoomT * powf(1.2f, in.wheel), 1.f, 4.f);
  else if (in.wheel != 0 && !showRadio) camZoom = clampf(camZoom * powf(0.88f, in.wheel), 0.35f, 4.f);
  {   // cockpit zoom eases toward the wheel setting, or 2.8x while the zoom action is held
    float tz = camMode == 1 ? (actDown(ACT_ZOOM) ? std::max(ckZoomT, 2.8f) : ckZoomT) : 1.f;
    ckZoom += (tz - ckZoom) * (1.f - expf(-dt * 10.f));
  }
  bool drag = in.mDown[1] || (camMode == 2 && in.mDown[0]);
  // chase and orbit cameras use reversed pitch: stick up / drag up swings the camera down so the view tilts up
  float pitchDir = (camMode == 0 || camMode == 2) ? -1.f : 1.f;
  float lookK = camMode == 1 ? 1.f / ckZoom : 1.f;   // zoomed in: finer look control
  if (drag) { camYaw -= in.mdx * 0.005f * set.mouseSens * lookK; camPitch = clampf(camPitch + pitchDir * in.mdy * 0.004f * set.mouseSens * lookK, -1.3f, 1.4f); }
  if (in.pad) { float rx = fabsf(in.rx) > 0.2f ? in.rx : 0, ry = fabsf(in.ry) > 0.2f ? in.ry : 0; camYaw -= rx * 2.f * dt; camPitch = clampf(camPitch + pitchDir * ry * 1.5f * dt, -1.3f, 1.4f); }
  const AircraftSpec& s = *plane.spec;
  float size = std::max(s.fusLen, s.span);
  if (camMode == 0) {
    // no auto-recentre: the camera stays where it was swung until the view is changed (C)
    // follow orientation with lag; reduce roll so the horizon isn't nauseating
    vec3 f = plane.forward();
    if (length(plane.vel) > 15.f && !plane.onGround) f = normalize(lerp(f, normalize(plane.vel), 0.5f));
    float yaw = atan2f(f.x, -f.z), pitch = asinf(clampf(f.y, -1, 1));
    quat target = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), pitch * 0.85f);
    camQ = slerp(camQ, target, 1.f - expf(-4.f * dt));
    // spring arm: it lengthens with airspeed (relative to the type's cruise) so the aircraft sits smaller on screen
    // the faster it goes, stretches when the aircraft accelerates away and compresses when it slows, through a
    // slightly under-damped spring
    float base = (size * 0.85f + 5.f) * camZoom;
    float spd = length(plane.vel), sPrev = camSpd;
    camSpd = camArm <= 0.f ? spd : camSpd + (spd - camSpd) * (1.f - expf(-dt * 3.f));
    float acc = dt > 1e-4f ? (camSpd - sPrev) / dt : 0.f;
    float rel = spd / std::max(s.cruise, 20.f);
    float armT = base * (0.9f + 0.45f * std::min(rel, 1.f) + 0.3f * clampf(rel - 1.f, 0.f, 2.f) + clampf(acc * 0.025f, -0.12f, 0.25f));
    if (camArm <= 0.f) { camArm = armT; camArmV = 0; }
    const float wn = 3.2f, zeta = 0.8f;   // natural frequency (rad/s), damping
    camArmV += (wn * wn * (armT - camArm) - 2.f * zeta * wn * camArmV) * dt;
    camArm += camArmV * dt;
    float dist = std::max(camArm, base * 0.7f);
    quat orbit = camQ * quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist)) + vec3(0, s.fusRad * 0.6f, 0);
  } else if (camMode == 1) {
    if (drag || in.pad) { lookYaw = camYaw; lookPitch = camPitch - 0.12f; }
    else { float rest = plane.spec->special ? -0.24f : -0.13f;   // XR-30: rest the view so the instrument console is in sight
      // head-look: the eyes lead a turn a little (into the bank, and towards the nose when it pitches up)
      float leadYaw = set.headLook && !plane.onGround ? clampf(-plane.bankDeg() / 60.f, -1.f, 1.f) * 0.30f : 0.f;
      float leadPitch = set.headLook && !plane.onGround ? clampf(plane.pitchDeg() / 30.f, -0.5f, 0.5f) * 0.10f : 0.f;
      lookYaw = approach(lookYaw, leadYaw, 2.f, dt); lookPitch = approach(lookPitch, rest + leadPitch, 2.f, dt); camYaw = lookYaw; camPitch = lookPitch + 0.12f; }
    camPos = plane.pos + plane.q.rotate(kModels[plane.spec - kAircraft].eye);
  } else if (camMode == 2) {
    float dist = (size * 1.4f + 8.f) * camZoom;
    quat orbit = quat::axisAngle(vec3(0, 1, 0), camYaw) * quat::axisAngle(vec3(1, 0, 0), -camPitch);
    camPos = plane.pos + orbit.rotate(vec3(0, 0, dist));
  } else {
    if (!botControl && length(camPos - plane.pos) > 700.f) camPos = plane.pos + normalize(vec3(plane.vel.x, 0, plane.vel.z) + vec3(0.01f, 0, 0)) * 400.f + plane.right() * 45.f + vec3(0, 8, 0);
  }
  float gh = std::max(g_world.height(camPos.x, camPos.z, 6), 0.f) + 1.5f;
  if (camPos.y < gh) camPos.y = gh;
}

// ------------------------------------------------------------------ gamepad menus
// Outside active flight the left stick drives an on-screen cursor: A clicks, B backs out (same as Esc),
// the right stick scrolls lists. Any real mouse movement hands control back to the mouse.
void Game::gamepadMenus(float dt) {
  if (fabsf(in.mdx) + fabsf(in.mdy) > 0.5f) padCursorT = -100.f;
  bool menus = screen != SCR_FLIGHT || paused || crashed;
  if (!in.pad || !menus) { if (padHoldA && !(in.buttons & PAD_A)) { in.mDown[0] = false; padHoldA = false; } return; }
  auto dz = [](float v) { return fabsf(v) < 0.15f ? 0.f : (v - (v > 0 ? 0.15f : -0.15f)) / 0.85f; };
  float sx = dz(in.lx), sy = dz(in.ly);
  bool active = fabsf(sx) + fabsf(sy) > 0 || (in.buttonsPressed & (PAD_A | PAD_B));
  if (fabsf(sx) + fabsf(sy) > 0) focusNav = false;   // (the stick drives the cursor; the D-pad drives the focus: focusNavigate)
  if (active) {
    if (padCursorT < realTime - 50.f) { in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.5f; }   // first use: start centred
    padCursorT = realTime;
  }
  float speed = 1100.f * S() * dt;
  in.mx = clampf(in.mx + sx * fabsf(sx) * speed * 1.4f + sx * speed * 0.3f, 0.f, (float)g_ren.W - 1);
  in.my = clampf(in.my - sy * fabsf(sy) * speed * 1.4f - sy * speed * 0.3f, 0.f, (float)g_ren.H - 1);
  if (in.buttonsPressed & PAD_A) { if (focusNav) in.pressed[K_ENTER] = true; else { in.mPressed[0] = true; in.mDown[0] = true; padHoldA = true; } }
  if (padHoldA && !(in.buttons & PAD_A)) { in.mDown[0] = false; in.mReleased[0] = true; padHoldA = false; }
  if (in.buttonsPressed & PAD_B) {
    in.pressed[K_ESC] = true;
    if (screen == SCR_DEBRIEF) in.pressed[K_ENTER] = true;   // B on the results screen continues to the hub
  }
  float ry = dz(in.ry);
  if (ry != 0) { static float acc = 0; acc += ry * dt * 8.f; while (fabsf(acc) >= 1.f) { in.wheel += acc > 0 ? 1.f : -1.f; acc -= acc > 0 ? 1.f : -1.f; } }
  // research menu: D-pad also steps through launch sites, Start launches
  if (screen == SCR_RESEARCH) {
    int n = (int)g_world.airports.size();
    if (in.buttonsPressed & PAD_START) in.pressed[K_ENTER] = true;
    if (in.buttonsPressed & PAD_RB) resAirport = (resAirport + 1) % n;
    if (in.buttonsPressed & PAD_LB) resAirport = (resAirport + n - 1) % n;
  }
}

// the focus moves with the arrow keys (menus, the hub, the results, the pause screen; the research terminal keeps
// its own left / right for the site) and with the D-pad everywhere a menu is up; the mouse takes over again when it moves
void Game::focusNavigate() {
  focusPrev.swap(focusList); focusList.clear();
  bool menus = screen != SCR_FLIGHT || paused || crashed;
  int scr = screen * 4 + (paused ? 1 : 0) + (screen == SCR_HUB ? hubTab * 8 : 0);
  if (!menus) { focusNav = false; return; }
  if (scr != focusScreen) { focusScreen = scr; focusNav = false; }
  if (fabsf(in.mdx) + fabsf(in.mdy) > 0.5f || in.mPressed[0]) focusNav = false;
  bool keys = screen != SCR_RESEARCH;
  int dx = 0, dy = 0;
  if ((keys && in.pressed[K_LEFT]) || (in.buttonsPressed & PAD_LEFT)) dx = -1;
  if ((keys && in.pressed[K_RIGHT]) || (in.buttonsPressed & PAD_RIGHT)) dx = 1;
  if ((keys && in.pressed[K_UP]) || (in.buttonsPressed & PAD_UP)) dy = -1;
  if ((keys && in.pressed[K_DOWN]) || (in.buttonsPressed & PAD_DOWN)) dy = 1;
  if (!dx && !dy) { if (focusNav) { bool still = false; for (auto& f : focusPrev) if (f.id == focusId) still = true; if (!still && !focusPrev.empty()) focusId = focusPrev[0].id; } return; }
  if (focusPrev.empty()) return;
  const Focusable* cur = nullptr; for (auto& f : focusPrev) if (f.id == focusId) cur = &f;
  if (!focusNav || !cur) {   // first press: the top-left button
    const Focusable* best = &focusPrev[0];
    for (auto& f : focusPrev) if (f.y + f.x * 0.02f < best->y + best->x * 0.02f) best = &f;
    focusNav = true; focusId = best->id; g_audio.trigger(SFX_HOVER, 0.5f); return;
  }
  float cx = cur->x + cur->w * 0.5f, cy = cur->y + cur->h * 0.5f;
  const Focusable* best = nullptr; float bestScore = 1e18f;
  for (auto& f : focusPrev) {
    if (f.id == focusId) continue;
    float fx = f.x + f.w * 0.5f, fy = f.y + f.h * 0.5f, ddx = fx - cx, ddy = fy - cy;
    float along = dx ? ddx * dx : ddy * dy, across = dx ? fabsf(ddy) : fabsf(ddx);
    if (along <= 1.f) continue;
    float score = along + across * 2.5f;
    if (score < bestScore) { bestScore = score; best = &f; }
  }
  if (best) { focusId = best->id; g_audio.trigger(SFX_HOVER, 0.5f); }
}

void Game::drawPadCursor() {
  if (!in.pad || realTime - padCursorT > 6.f) return;
  if (screen == SCR_FLIGHT && !paused && !crashed) return;
  float s = S(), x = in.mx, y = in.my;
  float pulse = 0.5f + 0.5f * sinf(realTime * 5.f);
  bool down = in.mDown[0];
  g_ren.glow(x - 9 * s, y - 9 * s, 18 * s, 18 * s, vec3(0.32f, 0.86f, 1.f), 0.35f + 0.2f * pulse, 9 * s, 10 * s);
  g_ren.rectOutline(x - 11 * s, y - 11 * s, 22 * s, 22 * s, vec3(0.32f, 0.86f, 1.f), 0.9f, 11 * s, 2 * s);
  g_ren.rect(x - (down ? 5.f : 3.f) * s, y - (down ? 5.f : 3.f) * s, (down ? 10.f : 6.f) * s, (down ? 10.f : 6.f) * s, vec3(1, 1, 1), 1, 5 * s);
  for (int i = 0; i < 4; i++) {
    float a = i * 1.5708f + realTime * 0.8f;
    g_ren.line(x + cosf(a) * 14 * s, y + sinf(a) * 14 * s, x + cosf(a) * 19 * s, y + sinf(a) * 19 * s, 2 * s, vec3(0.32f, 0.86f, 1.f), 0.8f);
  }
}

// ------------------------------------------------------------------ XR-30 research flights
void Game::launchResearch() {
  Contract c;
  bool wr = resCraft == kWraith;
  const AircraftSpec& rs = kAircraft[resCraft];
  std::string num = rs.name; num = num.substr(0, num.find(' '));   // "XR-30"
  c.id = num; c.id.erase(std::remove(c.id.begin(), c.id.end(), '-'), c.id.end()); c.title = num + " Research Flight"; c.type = CT_FERRY;
  if (resCard >= 0 && kResCards[resCard].craft != resCraft) resCard = -1;
  if (resCard >= 0) c.title = std::string(kResCards[resCard].id) + "  " + kResCards[resCard].title;
  resStep = 0; resHold = 0; resBest = 0; resCardDone = false;
  c.from = c.to = resAirport; c.payout = 0;
  c.wx = Weather(); c.wx.timeOfDay = resTime; c.wx.windSpeed = 3; c.wx.turbulence = 0.05f;
  if (resWx == 0) { c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; }
  else if (resWx == 1) { c.wx.cloudCover = 0.6f; c.wx.cloudBase = 1300; }
  else { c.wx.cloudCover = 0.95f; c.wx.cloudBase = 800; c.wx.precip = 1; c.wx.storm = true; c.wx.windSpeed = 9; c.wx.gust = 5; c.wx.turbulence = 0.5f; c.wx.visibility = 9000; }
  startFlight(c, resCraft, Career::SRC_OWNED);
  researchFlight = true;
  toasts.clear();
  { std::string nm = rs.name; for (char& ch : nm) ch = (char)toupper((unsigned char)ch); toast(nm + (resCard >= 0 ? std::string(" // TEST CARD ") + kResCards[resCard].title : std::string(" // RESEARCH FLIGHT")), wr ? vec3(0.75f, 0.45f, 1.f) : rs.special ? vec3(0.4f, 0.9f, 1) : vec3(0.35f, 0.95f, 0.8f)); }
  if (resCard >= 0) toast(fmt("STEP 1 of %d: %s", kResCards[resCard].n, kResCards[resCard].steps[0].label), vec3(0.9f, 0.9f, 0.6f));
  if (resAirborne) {
    const Airport& a = g_world.airports[resAirport];
    vec3 p = plane.pos + a.dir() * 1500.f; p.y = std::max(a.elev, g_world.height(p.x, p.z)) + 900.f;
    plane.reset(&kAircraft[resCraft], p, plane.heading(), kAircraft[resCraft].maxFuel, 85, true, 200.f);
    plane.ctl.throttle = 0.7f; takeoffAnnounced = true;
    settleAirborneStart();   // (startFlight parked it for a start on the ground)
    camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 4, 26));
  } else if (wr) toast("F/V tilts the four thruster pods: full down for vertical takeoff", vec3(0.7f, 0.9f, 1));
  prevMach = 0;
}

void Game::jetEffects(float dt) {
  const float thr = plane.ctl.throttle;
  float ab = plane.engineRunning ? smoothstepf(0.85f, 1.f, plane.engineSpool) : 0.f;
  bool wr = plane.spec->special == 2;
  vec3 exP[4], exD[4]; float exS[4];
  int nEx = jetExhausts(plane, exP, exD, exS);
  auto frand = [] { return (rand() % 1000) * 0.001f; };
  // The plume itself is ray-marched in the shader; particles add the hot debris it sheds: blue plasma sparks when
  // dry, a storm of amber embers in reheat (violet on the XR-40). Spread over the frame's flight path so they stream.
  for (int s = 0; s < nEx; s++) {
    vec3 exDir = plane.q.rotate(exD[s]);
    vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
    vec3 ex = plane.pos + plane.q.rotate(exP[s]);
    float rate = wr ? 0.f : 25.f * ab;    // embers per second per nozzle: reheat only (dry jets and the XR-40's plasma shed none)
    int n = (int)(rate * dt + frand());
    for (int i = 0; i < n; i++) {
      float k = frand(), hot = frand();
      vec3 jitter = r * ((frand() - 0.5f) * 0.55f) + u * ((frand() - 0.5f) * 0.35f);
      vec3 v = plane.vel + exDir * (90.f + 160.f * ab) * (0.6f + 0.6f * frand()) + jitter * (25.f + 40.f * ab);
      vec3 col = lerp(wr ? vec3(0.6f, 0.3f, 1.f) : vec3(1.f, 0.38f, 0.08f), wr ? vec3(0.8f, 0.9f, 1.f) : vec3(1.f, 0.8f, 0.45f), hot * hot) * (1.5f + 1.5f * hot);
      spawn(ex + jitter - plane.vel * (dt * k) + exDir * (2.f + k * 4.f), v, 0.15f + 0.3f * frand(), 0.035f + 0.03f * hot, -0.02f,
            col, 1.f, SPR_SPARK, 2.5f, 0.f);
    }
    // reheat: a faint heat haze trailing the plume at low level
    if (ab > 0.05f) {
      if (plane.agl() < 800.f && frand() < 30.f * dt)
        spawn(ex + exDir * (8.f + 6.f * ab), plane.vel * 0.2f + exDir * 20.f, 1.2f, 1.6f, 3.5f, vec3(0.32f, 0.3f, 0.3f), 0.06f * ab, SPR_SMOKE, 1.5f, 0.2f);
    }
  }
  // reheat light-off: a shock ring and a burst of sparks out of both nozzles
  if (ab > 0.08f && prevAB <= 0.08f) {
    for (int s = 0; s < nEx; s++) {
      vec3 exDir = plane.q.rotate(exD[s]);
      vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
      vec3 ex = plane.pos + plane.q.rotate(exP[s]) + exDir * 0.2f;
      spawn(ex, plane.vel + exDir * 30.f, 0.35f, 0.6f, 9.f, vec3(1.f, 0.7f, 0.4f) * 1.5f, 1.f, SPR_SHOCK, 0.f, 0.f);
      for (int i = 0; i < (wr ? 0 : 30); i++) {   // the XR-40's plasma lights with the shock ring alone
        vec3 j = r * (frand() - 0.5f) + u * (frand() - 0.5f);
        spawn(ex, plane.vel + exDir * (60.f + 120.f * frand()) + j * 70.f, 0.25f + 0.3f * frand(), 0.12f, -0.2f, vec3(1.f, 0.75f, 0.4f) * 4.f, 1.f, SPR_SPARK, 2.f, 0.f);
      }
    }
    g_audio.trigger(SFX_BOOM, 0.25f);
  }
  prevAB = ab;
  // hover downwash: dust or spray blown out in a ring under the jet
  float agl = plane.agl();
  if (plane.nozzle > 0.5f && thr > 0.25f && agl < 35.f) {
    float gy = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f);
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.3f;
    int n = (int)((1.f - agl / 35.f) * 6.f);
    for (int i = 0; i < n; i++) {
      float ang = (rand() % 628) * 0.01f;
      vec3 d(cosf(ang), 0, sinf(ang));
      spawn(vec3(plane.pos.x, gy + 0.5f, plane.pos.z) + d * 3.f, d * (12.f + (rand() % 100) * 0.1f) + vec3(0, 1.5f, 0), 1.6f, 1.2f, 3.f,
            water ? vec3(0.9f, 0.95f, 1.f) : vec3(0.5f, 0.46f, 0.38f), water ? 0.5f : 0.35f, SPR_SMOKE, 1.2f, 0.3f);
    }
  }
  // sonic boom when passing Mach 1 (the vapour cone itself is volumetric, in the shader)
  float M = plane.mach;
  if (prevMach < 1.f && M >= 1.f && !plane.onGround) {
    g_audio.trigger(SFX_BOOM, 1.f);
    toast("MACH 1 - SONIC BOOM", vec3(0.4f, 0.9f, 1));
    vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), u = cross(r, f);
    bursts.push_back({plane.pos, r * 25.f, u * 25.f, vec3(0.7f, 0.85f, 1.f), 0.f});
  }
  prevMach = M;
}

// ------------------------------------------------------------------ lights
// Every light but the sun and moon is a point (or spot) light with the aircraft's shadows, and sits in
// a modelled fixture: a faired housing with a domed lens (nav lights in the wingtips and tail cone, the strobes
// flashing through the wingtip lenses, a beacon on top, landing lights in the wing leading edges). The exhaust has a
// light in each flame just behind its nozzle, so the nozzles and the airframe throw shadows from it.
void Game::buildLights(FrameParams& fp) {
  fp.plN = 0; fp.plane.lensN = 0;
  auto light = [&](vec3 pos, float rad, vec3 col, float cosCut, vec3 dir, float shadow) {
    if (fp.plN < 12 && col.x + col.y + col.z > 1e-4f) fp.pl[fp.plN++] = {pos, rad, col, cosCut, dir, shadow};
  };
  if (length(fp.flameLight) > 0.f) light(fp.flameLightPos, 4.f, fp.flameLight, -2.f, vec3(0, 1, 0), 0.f);   // plasma blast
  if ((screen != SCR_FLIGHT && screen != SCR_LOADING) || !plane.spec || crashed) return;
  const AircraftSpec& s = *plane.spec;
  const ModelDef& md = kModels[plane.spec - kAircraft];
  float t = realTime, night = fp.night;
  float dark = s.special == 2 ? 1.f - wraith.stealth : 1.f;   // a cloaked XR-40 runs dark
  auto W = [&](vec3 b) { return plane.pos + plane.q.rotate(b); };
  auto lens = [&](vec3 b, vec3 axis, int tint, vec3 emit) {
    int i = fp.plane.lensN; if (i >= 6) return;
    float* P = fp.plane.lensP[i]; float* C = fp.plane.lensC[i]; float* D = fp.plane.lensD[i];
    P[0] = b.x; P[1] = b.y; P[2] = b.z; P[3] = 0; C[0] = emit.x; C[1] = emit.y; C[2] = emit.z; C[3] = 0;
    D[0] = axis.x; D[1] = axis.y; D[2] = axis.z; D[3] = (float)tint; fp.plane.lensN++;
  };
  // exhaust flames
  if (s.special && plane.engineRunning && !(camMode == 1 && fp.sealedCockpit)) {
    float sp = plane.engineSpool, ab = fp.plane.flame[1];
    vec3 exP[4], exD[4]; float exS[4]; int nEx = jetExhausts(plane, exP, exD, exS);
    float flick = 0.85f + 0.15f * sinf(t * 57.f) * sinf(t * 23.f + 1.f);
    vec3 c = lerp(s.special == 2 ? vec3(0.45f, 0.35f, 1.f) : vec3(0.3f, 0.55f, 1.f), s.special == 2 ? vec3(0.9f, 0.6f, 1.f) : vec3(1.f, 0.62f, 0.3f), ab)
             * (((s.special == 2 ? 6.f * sp * sp : 14.f * sp) + 45.f * ab) * flick / (float)nEx) * dark;
    for (int k = 0; k < nEx; k++) light(W(exP[k] + exD[k] * (0.15f + 0.15f * ab)), 0.08f, c * (s.special == 2 ? exS[k] : 1.f), -2.f, vec3(0, 1, 0), 0.05f);
  }
  // fixture positions (body space)
  vec3 tip = s.special == 2 ? kWraithWingTip : s.special ? kJetWingTip : modelWingTip(md);
  vec3 tail = s.special == 2 ? vec3(0, -0.1f, 7.86f) : s.special ? vec3(0, 0.45f, 7.6f) : modelTailTip(md);
  vec3 bcn = s.special == 2 ? vec3(0, 0.53f, 1.6f) : s.special ? vec3(0, 0.67f, 1.6f) : modelFinTop(md);
  vec3 ldg;   // left landing light, in the wing leading edge
  if (s.special == 2) ldg = vec3(-0.2f, -0.38f, -6.6f);   // XR-40: under the chin, ahead of the pods, turrets and gear
  else if (s.special) ldg = vec3(-1.8f, -0.26f, 0.15f);
  else { float k = 0.3f, x = md.wing[0] * k; ldg = vec3(-x, md.wing[4] + x * tanf(md.wing[6] * DEG), md.wing[5] + md.wing[3] * k - 0.02f); }
  bool on = plane.engineRunning || plane.onGround;
  float navL = on ? 0.06f * dark : 0.f;   // ~40 cd: a faint coloured wash on the wingtips, the ground when low
  float st = fmodf(t, 1.3f);
  bool strobe = on && !plane.onGround && dark > 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f));
  bool beacon = plane.engineRunning && dark > 0.5f && fmodf(t, 1.0f) < 0.12f;
  vec3 red(1.f, 0.08f, 0.04f), green(0.1f, 1.f, 0.25f), white(1.f, 0.97f, 0.9f);
  float lensK = 3.f + 40.f * night;   // a small, intense lamp: the bloom gives it its halo
  tip.x += 0.05f;   // the lens domes stand proud of the wingtips
  vec3 tl(-tip.x, tip.y, tip.z);
  vec3 sW = strobe ? white * 2.5f : vec3(0.f);
  lens(tl, vec3(-1, 0, 0), 0, red * (on ? dark : 0.f) * lensK + sW * 8.f);
  lens(tip, vec3(1, 0, 0), 1, green * (on ? dark : 0.f) * lensK + sW * 8.f);
  lens(tail, vec3(0, 0, 1), 2, white * (on ? dark : 0.f) * lensK);
  lens(bcn, vec3(0, 1, 0), 0, beacon ? red * 30.f : vec3(0.f));
  float land = fp.landLight;
  vec3 ldgR(-ldg.x, ldg.y, ldg.z);
  lens(ldg, vec3(0, 0, -1), 2, white * land * 25.f);
  lens(ldgR, vec3(0, 0, -1), 2, white * land * 25.f);
  light(W(tl + vec3(-0.07f, 0, 0)), 0.25f, red * navL + sW, -2.f, vec3(0, 1, 0), 0.03f);
  light(W(tip + vec3(0.07f, 0, 0)), 0.25f, green * navL + sW, -2.f, vec3(0, 1, 0), 0.03f);
  light(W(tail + vec3(0, 0, 0.07f)), 0.25f, white * navL, -2.f, vec3(0, 1, 0), 0.03f);
  if (beacon) light(W(bcn + vec3(0, 0.07f, 0)), 0.25f, red * 0.8f, -2.f, vec3(0, 1, 0), 0.03f);
  if (land > 0.f) {
    vec3 ld = normalize(plane.forward() - plane.up() * 0.1f);
    light(W(ldg + vec3(0, 0, -0.08f)), 0.3f, vec3(1.f, 0.95f, 0.85f) * land * 4500.f, 0.93f, ld, 0.03f);
    light(W(ldgR + vec3(0, 0, -0.08f)), 0.3f, vec3(1.f, 0.95f, 0.85f) * land * 4500.f, 0.93f, ld, 0.03f);
  }
}

// ------------------------------------------------------------------ UFO encounter
void Game::startUfo() {
  ufo = Ufo(); ufo.on = true; ufo.side = (rand() & 1) ? 1.f : -1.f;
  g_audio.trigger(SFX_UFO_ARRIVE, 0.9f);
  toast("UNIDENTIFIED CONTACT", vec3(0.4f, 1.f, 0.6f));
}

void Game::updateUfo(float dt) {
  if (!ufo.on) {
    // summon: hold J and K together for a second while flying
    bool flying = screen == SCR_FLIGHT && !paused && !crashed && !plane.onGround;
    if (flying && in.down['J'] && in.down['K']) {
      ufoSummon += dt;
      if (ufoSummon >= 1.f) { ufoSummon = 0; startUfo(); return; }
    } else ufoSummon = 0;
    // random encounters: only while properly airborne
    bool ok = screen == SCR_FLIGHT && !crashed && !plane.onGround && plane.agl() > 150.f && length(plane.vel) > 25.f;
    if (ok) ufo.next -= dt;
    if (ufo.next <= 0 && ok) startUfo();
    return;
  }
  float t = (ufo.t += dt);
  // abort (zoom off) if the player crashes or lands
  if ((crashed || plane.onGround) && t < 23.f) { ufo.t = t = 23.f; }
  // frame that rides with the player: horizontal flight direction, its right, world up
  vec3 f = plane.vel; f.y = 0;
  f = length(f) > 1.f ? normalize(f) : normalize(vec3(plane.forward().x, 0, plane.forward().z) + vec3(1e-4f, 0, 0));
  vec3 r(-f.z, 0, f.x), u(0, 1, 0);
  float span = plane.spec->span;
  vec3 hold = r * (ufo.side * (span * 0.5f + 15.f)) + u * -0.6f + f * 2.f;         // station alongside, cabin at eye level
  vec3 from = -f * 250.f + u * 60.f + r * (ufo.side * 520.f);                       // swoops in from the rear quarter and above (in view of the XR-40's aft displays)
  float k = smoothstepf(0.f, 6.5f, t);
  vec3 off = from + (hold - from) * k;
  off.y += sinf(t * 1.7f) * 0.6f + sinf(t * 0.9f) * 0.4f;                           // floating bob
  if (t > 23.f) { float z = t - 23.f; off += f * (z * z * z * 120.f + z * 30.f) + u * (z * z * 45.f); }   // zoom away
  ufo.pos = plane.pos + off;
  // orientation: hatch side (+x) towards the player, tilted into the swoop
  vec3 toP = plane.pos - ufo.pos; toP.y = 0; toP = length(toP) > 1.f ? normalize(toP) : r * -ufo.side;
  float tilt = (1.f - k) * 0.35f * ufo.side + (t > 23.f ? -0.25f * ufo.side : 0.f) + 0.04f * sinf(t * 1.3f);
  vec3 x = toP, z = normalize(cross(x, u));
  vec3 xr = x * cosf(tilt) + u * sinf(tilt), yr = u * cosf(tilt) - x * sinf(tilt);
  ufo.right = xr; ufo.up = yr; ufo.fwd = z;
  // the show: hatch opens 8-10 s, dance 10-16, laugh 16-18.5, wave 18.5-21, hatch closes 21-23, zoom 23-26
  ufo.hatch = smoothstepf(8.f, 10.f, t) * (1.f - smoothstepf(21.f, 23.f, t));
  ufo.laugh = smoothstepf(15.8f, 16.3f, t) * (1.f - smoothstepf(18.3f, 18.8f, t));
  ufo.wave = smoothstepf(18.4f, 18.9f, t) * (1.f - smoothstepf(20.8f, 21.3f, t));
  if (t > 16.f && !ufo.sfxLaugh) { ufo.sfxLaugh = true; g_audio.trigger(SFX_UFO_LAUGH, 1.f); }
  if (t > 23.f && !ufo.sfxZoom) {
    ufo.sfxZoom = true; g_audio.trigger(SFX_UFO_ZOOM, 1.f);
    vec3 c = ufo.pos;
    bursts.push_back({c, ufo.right * 14.f, ufo.fwd * 14.f, vec3(0.4f, 1.f, 0.7f), 0.f});
    for (int i = 0; i < 40; i++) spawn(c, plane.vel + vec3((rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f), 0.6f, 0.25f, -0.3f, vec3(0.5f, 1.f, 0.8f) * 3.f, 1.f, SPR_SPARK, 1.f, 0.f);
  }
  if (t > 23.f && t < 26.f) spawn(ufo.pos, plane.vel * 0.5f, 0.5f, 3.f, -4.f, vec3(0.4f, 1.f, 0.8f) * 1.5f, 1.f, SPR_GLOW, 0.f, 0.f);   // light streak
  if (t > 26.5f) { ufo.on = false; ufo.next = 480.f + (rand() % 1000) * 0.42f; }   // next one in 8-15 minutes
}

// ------------------------------------------------------------------ crash wreckage
float Game::wreckGround(float x, float z) const {
  float g = g_world.height(x, z, 6);
  if (craterR > 0 && g > 0.3f) {
    float d = sqrtf((x - craterX) * (x - craterX) + (z - craterZ) * (z - craterZ)) / craterR;
    if (d < 1.8f) { g = g_world.groundHeight(x, z, 6) - craterD * std::max(1.f - d * d, 0.f) + 0.22f * craterD * expf(-(d - 1.f) * (d - 1.f) * 14.f); }
  }
  return g;
}

// Splits the airframe into nose, centre section, both wings and tail (each the aircraft's field clipped to a
// body-space box), throws them apart with the impact energy, scatters skin fragments and digs a crater.
// A layered explosion: a white-hot flash that lights the scene, a fireball of flame cooling from yellow to deep red
// as it billows out and rises, a dark smoke column that keeps climbing and spreading after the flames die, arcing
// sparks, burning fragments that trail fire and smoke, and a shock ring. R: fireball radius (m).
void Game::fireball(vec3 c, vec3 baseV, float R, bool air, bool water) {
  Rng r((uint32_t)(c.x * 13.f + c.z * 7.f + realTime * 1000.f));
  auto sph = [&]() { vec3 d(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)); float l = length(d); return l > 1e-3f ? d / l : vec3(0, 1, 0); };
  boomT = 0; boomP = c + vec3(0, R * 0.4f, 0); boomI = R * R * 500.f;
  for (int i = 0; i < 3; i++) spawn(c + sph() * R * 0.2f, baseV, r.range(0.12f, 0.22f), R * r.range(0.9f, 1.3f), R * 1.5f, vec3(1.f, 0.9f, 0.7f) * 1.6f, 1.f, SPR_GLOW, 0.f, 0.f);   // flash
  if (water) {   // into the sea: a tall column of spray and steam instead of a fireball
    for (int i = 0; i < 110; i++) { vec3 d = sph(); d.y = fabsf(d.y) * 2.5f + 0.6f;
      spawn(c + vec3(0, 0.5f, 0), baseV * 0.3f + d * R * r.range(1.5f, 4.f), r.range(1.8f, 3.2f), R * r.range(0.15f, 0.35f), R * 0.5f, vec3(0.92f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -7.f); }
    bursts.push_back({c + vec3(0, 0.3f, 0), vec3(R * 3.f, 0, 0), vec3(0, 0, R * 3.f), vec3(0.8f, 0.9f, 1.f), 0.f});
    return;
  }
  int nf = (int)clampf(R * 9.f, 30.f, 90.f);
  for (int i = 0; i < nf; i++) {   // the fireball: billows of flame thrown out, rising, cooling to soot
    vec3 d = sph(); if (!air) d.y = fabsf(d.y) * 0.8f + 0.15f;
    float k = r.range(0.f, 1.f);
    vec3 col = lerp(vec3(1.f, 0.82f, 0.45f), vec3(1.f, 0.5f, 0.14f), k);
    spawn(c + d * R * 0.3f * k, baseV * 0.6f + d * R * r.range(0.9f, 2.2f), r.range(1.1f, 2.3f), R * r.range(0.3f, 0.55f), R * r.range(0.35f, 0.75f), col, 0.9f, SPR_FLAME, 2.2f, r.range(3.f, 6.f));
  }
  for (int i = 0; i < 8; i++)   // a little additive heat glow in the core
    spawn(c + sph() * R * 0.2f, baseV * 0.6f, r.range(0.3f, 0.5f), R * 0.6f, R * 0.5f, vec3(1.f, 0.6f, 0.25f), 0.2f, SPR_FIRE, 2.f, 1.f);
  int ns = (int)clampf(R * 6.f, 20.f, 60.f);
  for (int i = 0; i < ns; i++) {   // smoke: a column that keeps climbing long after the flames
    vec3 d = sph(); if (!air) { d.y = fabsf(d.y) + 0.6f; d.x *= 0.5f; d.z *= 0.5f; }
    float g = r.range(0.05f, 0.13f);
    spawn(c + d * R * 0.3f, baseV * 0.3f + d * R * r.range(0.3f, 0.8f) + plane.windVel * 0.5f, r.range(5.f, 10.f), R * r.range(0.3f, 0.5f), R * r.range(0.15f, 0.3f), vec3(g, g * 0.95f, g * 0.9f), r.range(0.4f, 0.6f), SPR_SMOKE, 0.5f, air ? 1.f : r.range(4.f, 7.f));
  }
  for (int i = 0; i < (int)clampf(R * 25.f, 50.f, 160.f); i++)   // sparks arcing out and falling
    spawn(c, baseV * 0.7f + sph() * r.range(15.f, 55.f) + vec3(0, r.range(0.f, 15.f), 0), r.range(0.8f, 2.4f), r.range(0.05f, 0.12f), -0.02f, vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 0.6f, -9.f);
  for (int i = 0; i < (int)clampf(R * 2.5f, 6.f, 18.f); i++) {   // burning fragments trailing fire and smoke
    Debris d;
    d.p = c + sph() * R * 0.3f;
    vec3 dir = sph(); if (!air) dir.y = fabsf(dir.y) + 0.4f;
    d.v = baseV * 0.7f + normalize(dir) * r.range(15.f, 40.f);
    d.w = sph() * r.range(4.f, 14.f); d.q = quat::axisAngle(sph(), r.range(0, 6.f));
    d.size = r.range(0.15f, 0.45f); d.charred = true; d.rest = false; d.burn = r.range(1.2f, 3.5f);
    debris.push_back(d);
  }
  vec3 f = air && length(baseV) > 1.f ? normalize(baseV) : vec3(0, 1, 0);
  vec3 rr = normalize(cross(f, fabsf(f.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0))), u = cross(rr, f);
  bursts.push_back({c, rr * R * 3.5f, u * R * 3.5f, vec3(1.f, 0.65f, 0.35f), 0.f});   // shock ring
}

void Game::breakUp(vec3 impactVel, bool water, bool air) {
  const ModelDef& m = kModels[plane.spec - kAircraft];
  Rng r(1234 + (uint32_t)(flightClock * 100));
  float speed = length(impactVel);
  float xs = std::max(m.wing[0], m.ht[0]) + 0.6f;
  float y0 = -(plane.gearHeight() + 0.7f), y1 = std::max(m.vt[4] + m.vt[0], m.ht[4]) + 0.6f;
  float z0 = std::min(m.st[0][0], m.engine >= 2 && m.engine <= 3 ? m.nacZ0 : 0.f) - 0.9f;
  float z1 = std::max(std::max(m.st[7][0], m.vt[5] + m.vt[1]), m.ht[5] + m.ht[1]) + 0.6f;
  float zA = m.wing[5] - 0.25f, zB = m.wing[5] + std::max(m.wing[1], m.wing[3] + m.wing[2]) + 0.35f;
  float xr = modelHalfWidth(m, m.wing[5] + m.wing[1] * 0.5f) * 1.08f + 0.05f;
  float wy0 = m.wing[4] - 0.45f, wy1 = m.wing[4] + 0.5f + m.winglet * 1.2f;
  if (m.engine == 2 || m.engine == 3) { wy0 = std::min(wy0, m.nacY - m.nacR - 0.3f); wy1 = std::max(wy1, m.nacY + m.nacR + 0.2f); }
  if (m.gear == 3) wy0 = y0;
  if (m.strut) wy0 = std::min(wy0, m.st[3][3] - m.st[3][2]);
  auto box = [](vec3 lo, vec3 hi, vec3& C, vec3& H) { C = (lo + hi) * 0.5f; H = (hi - lo) * 0.5f; };
  vec3 lo[5] = {vec3(-xs, y0, z0), vec3(-xr, y0, zA), vec3(-xs, wy0, zA), vec3(xr, wy0, zA), vec3(-xs, y0, zB)};
  vec3 hi[5] = {vec3(xs, y1, zA), vec3(xr, y1, zB), vec3(-xr, wy1, zB), vec3(xs, wy1, zB), vec3(xs, y1, z1)};
  if (plane.spec->special) {   // XR-30: its own airframe (mapJet) - nose, centre, tail and both outer wings, no overlaps
    const float y0j = -1.7f, y1j = 2.9f;
    vec3 jl[5] = {vec3(-2.2f, y0j, -9.6f), vec3(-2.2f, y0j, -3.f), vec3(-5.9f, -1.1f, -3.f), vec3(2.2f, -1.1f, -3.f), vec3(-2.2f, y0j, 3.6f)};
    vec3 jh[5] = {vec3(2.2f, y1j, -3.f), vec3(2.2f, y1j, 3.6f), vec3(-2.2f, 0.8f, 6.1f), vec3(5.9f, 0.8f, 6.1f), vec3(2.2f, y1j, 9.6f)};
    for (int i = 0; i < 5; i++) { lo[i] = jl[i]; hi[i] = jh[i]; }
  }
  wreck.clear();
  vec3 centre = plane.pos;
  float energy = clampf(speed / 50.f, 0.4f, 2.5f);
  airBreak = air;
  if (air) {
    // In-flight break-up: every piece keeps the airframe's momentum and rotation, is flung apart by the failure
    // and tumbles violently; nothing touches the ground (no crater) until the pieces fall out of the sky.
    vec3 vRot = plane.q.rotate(plane.w);
    for (int i = 0; i < 5; i++) {
      WreckPiece w;
      box(lo[i], hi[i], w.C, w.H);
      w.q = plane.q;
      w.c = plane.pos + plane.q.rotate(w.C);
      vec3 arm = w.c - centre, out = length(arm) > 0.1f ? normalize(arm) : vec3(0, 1, 0);
      w.v = plane.vel * r.range(0.9f, 1.f) + cross(vRot, arm) + out * r.range(8.f, 20.f) + vec3(r.range(-4, 4), r.range(-2, 6), r.range(-4, 4));
      vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
      w.w = vRot + ax * r.range(4.f, 9.f) * (i == 1 ? 0.5f : 1.f);   // light wings and tail spin hardest
      w.rest = false; w.fire = r.range(0.7f, 1.f); w.landed = false;
      wreck.push_back(w);
    }
    debris.clear();
    for (int i = 0; i < 16; i++) {
      Debris d;
      d.p = centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4));
      d.v = plane.vel * r.range(0.6f, 0.95f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(10.f, 35.f);
      d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(6.f, 18.f);
      d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
      d.size = r.range(0.25f, 0.8f); d.charred = (i % 3) == 0; d.rest = false;
      debris.push_back(d);
    }
    craterR = 0;
    crashEndT = 1e9f;   // set once everything is down
    // the failure: the fuel tanks flash into a fireball smeared along the flight path, the slipstream tears the
    // skin away, and over the next couple of seconds the pieces go off again one after another
    pops.clear();
    for (int i = 0; i < 4; i++) {
      float k = i / 3.f;
      fireball(centre - plane.vel * (0.06f * k), plane.vel * (0.85f - 0.25f * k), 4.f + 3.f * (1.f - k), true, false);
    }
    for (int i = 0; i < 24; i++) {   // condensation and fuel mist torn off at the break
      vec3 v(r.range(-25, 25), r.range(-20, 25), r.range(-25, 25));
      spawn(centre, plane.vel * r.range(0.5f, 0.9f) + v, r.range(0.6f, 1.4f), r.range(2.f, 4.f), 6.f, vec3(0.9f, 0.92f, 0.95f), 0.35f, SPR_SMOKE, 3.f, 0.f);
    }
    for (int i = 0; i < 6; i++) pops.push_back({vec3(), vec3(), r.range(0.35f, 2.6f), r.range(2.f, 4.5f), i % 5});
    for (int i = 0; i < 20; i++) {   // extra torn skin panels
      Debris d;
      d.p = centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4));
      d.v = plane.vel * r.range(0.5f, 0.9f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(15.f, 45.f);
      d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(8.f, 22.f);
      d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
      d.size = r.range(0.2f, 0.7f); d.charred = (i % 2) == 0; d.rest = false; d.burn = i % 3 == 0 ? r.range(1.f, 3.f) : 0.f;
      debris.push_back(d);
    }
    return;
  }
  for (int i = 0; i < 5; i++) {
    WreckPiece w;
    box(lo[i], hi[i], w.C, w.H);
    w.q = plane.q;
    w.c = plane.pos + plane.q.rotate(w.C);
    vec3 out = w.c - centre; out.y = 0; out = length(out) > 0.1f ? normalize(out) : normalize(vec3(r.range(-1, 1), 0, r.range(-1, 1)));
    w.v = impactVel * r.range(0.25f, 0.45f) + out * r.range(4.f, 9.f) * energy + vec3(0, r.range(5.f, 11.f) * energy, 0);
    if (water) w.v = w.v * 0.4f;
    vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
    w.w = ax * r.range(1.f, 4.f) * energy;
    w.rest = false; w.fire = r.range(0.6f, 1.f);
    wreck.push_back(w);
  }
  // skin fragments
  debris.clear();
  for (int i = 0; i < 16; i++) {
    Debris d;
    d.p = centre + vec3(r.range(-2, 2), r.range(0.5f, 2.f), r.range(-2, 2));
    float a = r.range(0, 6.2832f);
    d.v = vec3(cosf(a), 0, sinf(a)) * r.range(6.f, 22.f) * energy + vec3(0, r.range(6.f, 18.f) * energy, 0) + impactVel * r.range(0.2f, 0.5f);
    if (water) d.v = d.v * 0.5f;
    d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(3.f, 12.f);
    d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
    d.size = r.range(0.25f, 0.8f); d.charred = (i % 3) == 0; d.rest = false;
    debris.push_back(d);
  }
  // crater (on land only)
  if (!water) {
    craterX = centre.x; craterZ = centre.z;
    craterR = clampf(3.f + speed * 0.07f + std::max(plane.spec->fusLen, plane.spec->span) * 0.12f, 4.f, 13.f);
    craterD = craterR * 0.28f;
  } else craterR = 0;
  // the impact: a fireball (or a column of spray), dirt thrown up, and a couple of secondary blasts as fuel goes up
  pops.clear();
  float R = clampf(3.f + speed * 0.05f + std::max(plane.spec->fusLen, plane.spec->span) * 0.15f, 4.f, 11.f);
  fireball(centre + vec3(0, 1.f, 0), impactVel * 0.15f, R, false, water);
  if (!water) {
    for (int i = 0; i < 50; i++) spawn(centre, vec3(r.range(-12, 12), r.range(6, 20), r.range(-12, 12)) * energy * 0.6f, r.range(1.5f, 3.f), r.range(1.2f, 2.6f), 2.f, vec3(0.32f, 0.25f, 0.18f), 0.85f, SPR_SMOKE, 1.5f, -5.f);   // dirt
    for (int i = 0; i < 3; i++) pops.push_back({vec3(), vec3(), r.range(0.4f, 2.2f), R * r.range(0.4f, 0.6f), r.range(0.f, 4.99f) > 2.5f ? 1 : 0});
  }
}

void Game::updateWreck(float dt) {
  const float G = 9.81f;
  for (auto& tt : pieceTrail) {   // smoke trails drift with the wind and fade out
    for (auto& q : tt) { q.age += dt; q.p += (plane.windVel + vec3(0, 0.6f, 0)) * dt; }
    while (!tt.empty() && tt.front().age > 7.f) tt.erase(tt.begin());
  }
  if (boomT >= 0) boomT += dt;
  for (size_t i = 0; i < pops.size(); i++) {   // secondary explosions on the pieces
    pops[i].t -= dt;
    if (pops[i].t > 0) continue;
    const Pop pp = pops[i];
    pops.erase(pops.begin() + i); i--;
    if (pp.piece < (int)wreck.size()) {
      const WreckPiece& w = wreck[pp.piece];
      bool wet = g_world.height(w.c.x, w.c.z) < 0.5f && w.c.y < 1.f;
      if (!(wet && w.c.y < -1.f)) { fireball(w.c, w.v, pp.R, airBreak && !w.landed, wet); g_audio.trigger(SFX_CRASH, 0.35f); }
    }
  }
  for (WreckPiece& w : wreck) {
    // fire and smoke from the burning pieces (stronger right after the impact)
    float heat = w.fire * clampf(1.2f - crashTimer * 0.06f, 0.3f, 1.f);
    bool wet = g_world.height(w.c.x, w.c.z) < 0.5f;
    if (!wet && rand() % 100 < (int)(heat * 40)) {
      vec3 jp = w.c + w.q.rotate(vec3((rand() % 100 - 50) * 0.01f * w.H.x, 0, (rand() % 100 - 50) * 0.01f * w.H.z));
      spawn(jp, vec3(0, 2.f + (rand() % 100) * 0.02f, 0), 0.7f, 0.45f + 0.5f * heat, 0.8f, vec3(1.f, 0.42f, 0.1f) * 0.55f, 1.f, SPR_FIRE, 0.5f, 1.f);
    }
    bool sunk = wet && w.c.y < -1.5f;
    if (!sunk && rand() % 100 < (int)(heat * 22)) spawn(w.c + vec3(0, 2.5f, 0), vec3((rand() % 100 - 50) * 0.02f, 3.5f, (rand() % 100 - 50) * 0.02f) + plane.windVel * 0.5f, 7.f, 1.5f, 2.5f, wet ? vec3(0.8f) : vec3(0.1f, 0.095f, 0.09f), 0.45f, SPR_SMOKE, 0.25f, 1.5f);
    if (w.rest) continue;
    if (wet && w.c.y < 0.3f) {
      if (!w.landed) {   // a piece falling into the sea: a tall splash
        w.landed = true;
        if (airBreak) { g_audio.trigger(SFX_CRASH, 0.5f); for (int i = 0; i < 40; i++) spawn(vec3(w.c.x, 0.2f, w.c.z), vec3((rand() % 200 - 100) * 0.06f, 8.f + (rand() % 100) * 0.14f, (rand() % 200 - 100) * 0.06f), 2.2f, 1.4f, 2.f, vec3(0.9f, 0.95f, 1.f), 0.75f, SPR_SMOKE, 1.f, -7.f); }
      }
      // in the sea: heavy drag, a short float on trapped air, then the piece sinks out of sight to the seabed
      float floatT = 1.f + 1.5f * w.fire;
      float vy = crashTimer < floatT ? (-0.35f - w.c.y) * 2.f : -2.2f - 1.2f * w.fire;
      w.v.x *= expf(-1.5f * dt); w.v.z *= expf(-1.5f * dt);
      w.v.y += (vy - w.v.y) * std::min(1.f, 3.f * dt);
      w.w = w.w * expf(-1.2f * dt);
      if (crashTimer > floatT && w.c.y > -12.f && rand() % 100 < 25)   // air escaping as it goes down
        spawn(vec3(w.c.x + (rand() % 100 - 50) * 0.03f, 0.05f, w.c.z + (rand() % 100 - 50) * 0.03f), vec3(0, 0.6f, 0), 0.9f, 0.35f, 1.5f, vec3(0.9f, 0.95f, 1.f), 0.45f, SPR_SMOKE, 1.f, 0.f);
    } else if (airBreak && !w.landed) {
      // falling and tumbling: quadratic drag brings a chunk from supersonic down to ~90 m/s terminal; it keeps spinning
      w.v.y -= G * dt;
      w.v = w.v - w.v * (0.0012f * length(w.v) * dt);
      w.w = w.w * expf(-0.05f * dt);
      // the smoke trail is a ribbon through one point per frame (continuous at any speed); the flame is drawn as a jet
      int k = (int)(&w - &wreck[0]);
      if (k < 5) {
        float heatT = clampf(1.3f - crashTimer * 0.08f, 0.35f, 1.f);
        pieceTrail[k].push_back({w.c, 0.f, (0.5f + 0.5f * w.fire) * heatT, 1});
        if (pieceTrail[k].size() > 600) pieceTrail[k].erase(pieceTrail[k].begin());
      }
    } else w.v.y -= G * dt;
    w.v = w.v * expf(-0.08f * dt);
    w.c += w.v * dt;
    float wl = length(w.w);
    if (wl > 1e-4f) { w.q = quat::axisAngle(w.w, wl * dt) * w.q; w.q.normalize(); }
    // ground contact against the corners of the piece's box
    float pen = 0; vec3 hitArm;
    for (int k = 0; k < 8; k++) {
      vec3 cl((k & 1) ? w.H.x : -w.H.x, (k & 2) ? w.H.y : -w.H.y, (k & 4) ? w.H.z : -w.H.z);
      vec3 cw = w.c + w.q.rotate(cl * 0.85f);
      float g = wreckGround(cw.x, cw.z);
      if (g - cw.y > pen) { pen = g - cw.y; hitArm = cw - w.c; }
    }
    if (pen > 0 && !w.landed) {
      // first contact of a piece that fell out of the sky: it hits hard
      w.landed = true;
      float sp = length(w.v);
      if (airBreak && sp > 8.f) {
        g_audio.trigger(SFX_CRASH, clampf(sp / 90.f, 0.3f, 1.f));
        for (int i = 0; i < 30; i++) {
          vec3 v((rand() % 200 - 100) * 0.12f, (rand() % 100) * 0.16f, (rand() % 200 - 100) * 0.12f);
          spawn(w.c, v, 0.8f + (rand() % 100) * 0.008f, 2.f, 3.f, vec3(1.f, 0.55f, 0.2f), 1.f, SPR_FIRE, 1.2f, 2.f);
          spawn(w.c, v * 1.3f, 2.f, 1.5f, 2.f, vec3(0.32f, 0.25f, 0.18f), 0.8f, SPR_SMOKE, 1.5f, -5.f);
        }
        if (craterR <= 0 && &w == &wreck[1]) {   // the heavy centre section digs the crater
          craterX = w.c.x; craterZ = w.c.z; craterR = clampf(3.f + sp * 0.06f, 4.f, 10.f); craterD = craterR * 0.28f;
        }
      }
    }
    if (pen > 0) {
      w.c.y += pen;
      if (w.v.y < 0) w.v.y = -w.v.y * 0.22f;
      w.v.x *= 0.72f; w.v.z *= 0.72f;
      w.w = w.w * 0.75f + cross(hitArm, vec3(w.v.x, 0, w.v.z)) * 0.02f;
      if (length(w.v) < 0.8f && length(w.w) < 0.35f) w.rest = true;
    }
  }
  // in-flight break-up: the camera rides with the centre section; results come up a while after the last piece is down
  if (airBreak && !wreck.empty()) {
    plane.pos = wreck.size() > 1 ? wreck[1].c : wreck[0].c;
    bool all = true; for (auto& w : wreck) all = all && (w.landed || w.c.y < -1.f);
    if (crashEndT > 1e8f && (all || crashTimer > 150.f)) crashEndT = crashTimer + 6.f;
  }
  for (Debris& d : debris) {
    if (d.burn > 0 && !d.rest) {   // a burning fragment: a flame and a thin smoke trail behind it
      d.burn -= dt;
      float k = clampf(d.burn, 0.f, 1.f);
      spawn(d.p, d.v * 0.1f, 0.25f, 0.25f + 0.5f * d.size * k, 0.6f, vec3(1.f, 0.5f, 0.15f) * (0.6f + 0.6f * k), 1.f, SPR_FIRE, 2.f, 0.5f);
      if (rand() % 2) spawn(d.p, d.v * 0.05f, 1.6f, 0.3f + d.size * 0.5f, 1.4f, vec3(0.1f), 0.35f * k + 0.1f, SPR_SMOKE, 1.f, 0.8f);
    }
    if (d.rest) continue;
    d.v.y -= G * dt; d.v = d.v * expf(-0.3f * dt);
    if (airBreak) d.v = d.v - d.v * (0.02f * length(d.v) * dt);   // light skin panels flutter down
    d.p += d.v * dt;
    float wl = length(d.w);
    if (wl > 1e-4f) { d.q = quat::axisAngle(d.w, wl * dt) * d.q; d.q.normalize(); }
    float g = wreckGround(d.p.x, d.p.z);
    if (g < 0 && d.p.y < 0.f) {   // fragments in the sea flutter down to the seabed
      d.v = d.v * expf(-4.f * dt); d.v.y = std::max(d.v.y, -0.9f - d.size); d.w = d.w * expf(-1.f * dt);
      if (d.p.y < g + d.size * 0.15f) { d.p.y = g + d.size * 0.15f; d.rest = true; }
      continue;
    }
    if (d.p.y < g + d.size * 0.15f) {
      d.p.y = g + d.size * 0.15f;
      d.v.y = fabsf(d.v.y) * 0.25f; d.v.x *= 0.6f; d.v.z *= 0.6f; d.w = d.w * 0.6f;
      if (length(d.v) < 0.7f) {  // settle flat on the ground
        d.rest = true;
        float yaw = atan2f(d.q.rotate(vec3(1, 0, 0)).z, d.q.rotate(vec3(1, 0, 0)).x);
        d.q = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), (d.size - 0.5f) * 0.4f);
      }
    }
  }
}

// ------------------------------------------------------------------ particles
void Game::updateParticles(float dt) {
  for (size_t i = 0; i < bursts.size();) { bursts[i].t += dt; if (bursts[i].t > 0.9f) { bursts[i] = bursts.back(); bursts.pop_back(); } else i++; }
  for (size_t i = 0; i < particles.size();) {
    Particle& p = particles[i];
    // spawned this frame: emitters already place particles along the path flown this frame, so moving them by a
    // whole frame of velocity too would carry them that far ahead (at 25 fps and Mach 0.9, past the nose)
    if (p.fresh) { p.fresh = false; i++; continue; }
    p.life -= dt;
    if (p.life <= 0) { particles[i] = particles.back(); particles.pop_back(); continue; }
    p.v = p.v * expf(-p.drag * dt);
    p.v.y += p.buoy * dt;
    p.p += p.v * dt;
    p.size += p.grow * dt;
    i++;
  }
}

// ------------------------------------------------------------------ frame assembly
static void fillPlaneVisual(PlaneVisual& pv, const Plane& p, float propAngle, bool inside) {
  const AircraftSpec& s = *p.spec;
  int idx = (int)(p.spec - kAircraft);
  const ModelDef& md = kModels[idx];
  pv.on = true;
  pv.pos = p.pos;
  vec3 r = p.right(), u = p.up(), b = p.q.rotate(vec3(0, 0, 1));
  float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, b.x, b.y, b.z};
  memcpy(pv.rot, m, sizeof(m));
  packModel(s, idx, p.gearHeight(), pv.M);
  // nose/tail wheel steering: same angle the physics applies to the wheel (geometry angle in the x-z plane)
  float spd = length(p.vel);
  float steer = p.ctl.yaw * 0.45f * smoothstepf(30.f, 4.f, spd);
  if (s.taildragger) steer = -steer;
  pv.PS[0] = p.gear; pv.PS[1] = p.flaps; pv.PS[2] = steer; pv.PS[3] = inside ? 1.f : 0.f;
  pv.Ctl[0] = clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1); pv.Ctl[1] = clampf(p.ctl.roll, -1, 1); pv.Ctl[2] = clampf(p.ctl.yaw, -1, 1); pv.Ctl[3] = p.ctl.throttle;
  float blur = s.engineType == ENG_JET ? 1.f : smoothstepf(250.f, 700.f, p.rpm);
  float wr = s.special ? .38f : md.wheelR;
  float nr = s.special ? .33f : s.taildragger ? .10f : md.gear == 3 ? wr*.75f : wr*.85f;
  pv.model = (int)(p.spec - kAircraft);
  pv.wheel[0] = p.wheelMotion[0].angle(wr); pv.wheel[1] = p.wheelMotion[1].angle(wr); pv.wheel[2] = p.wheelMotion[2].angle(nr);
  pv.Pr[0] = propAngle; pv.Pr[1] = blur; pv.Pr[2] = (float)std::max(s.blades, 2); pv.Pr[3] = 0;
  pv.I0[0] = p.ias * MS_TO_KT; pv.I0[1] = p.pos.y * M_TO_FT; pv.I0[2] = p.heading(); pv.I0[3] = p.vel.y * 196.85f;
  pv.I1[0] = p.pitchDeg(); pv.I1[1] = p.bankDeg();
  pv.I1[2] = s.engineType == ENG_PISTON ? p.rpm / std::max(s.maxRpm, 1.f) : p.n1 / 100.f; pv.I1[3] = p.fuel / std::max(s.maxFuel, 1.f);
  pv.I2[0] = -p.q.rotate(p.w).y / DEG; pv.I2[1] = p.beta / DEG; pv.I2[2] = p.flaps; pv.I2[3] = p.gear;
  pv.colBase = s.colBase; pv.colStripe = s.colStripe;
  { std::string r = registrationOf(s); for (int i = 0; i < 3; i++) pv.reg[i] = (float)r[3 + i]; }
  pv.propCount = modelProps(md, pv.prop);
  pv.hud[0] = p.ias; pv.hud[1] = p.pos.y; pv.hud[2] = p.heading(); pv.hud[3] = p.mach;
  pv.hud2[0] = p.gLoad; pv.hud2[1] = p.ctl.throttle; pv.hud2[2] = p.spec && p.spec->special == 1 ? jetNozzleAngle(p) / (0.5f * PI) : p.nozzle;   // XR-30: pitch vectoring (90 deg units) pv.hud2[3] = p.gear > 0.5f ? 1.f : 0.f;
  vec3 vb = length(p.vel) > 2.f ? p.q.conj().rotate(normalize(p.vel)) : vec3(0, 0, -1);
  pv.hudV[0] = vb.x; pv.hudV[1] = vb.y; pv.hudV[2] = vb.z;
  pv.hud3[0] = p.engineSpool; pv.hud3[1] = p.alpha / DEG; pv.hud3[2] = p.vel.y; pv.hud3[3] = p.agl();
  if (s.special) {
    pv.flame[0] = p.engineRunning ? p.engineSpool : 0.f; pv.flame[1] = p.engineRunning ? smoothstepf(0.7f, 1.f, p.engineSpool) : 0.f;
    pv.flame[2] = jetNozzleAngle(p); pv.flame[3] = p.mach;
  }
}

FrameParams Game::buildFrame() {
  FrameParams fp;
  fp.time = realTime; fp.dt = std::max(lastDt, 1e-4f);
  computeSun(timeOfDay, fp.sunDir, fp.sunCol, fp.night);
  {   // terrain's soft sun shadow at the aircraft: the same march as the shader's terrainShadow, done once here instead
      // of for every pixel of the airframe and cockpit
    float res = 1.f, t = 2.f;
    vec3 ro = plane.pos;
    if (fp.sunDir.y > -0.05f)
      for (int i = 0; i < 40; i++) {
        vec3 q = ro + fp.sunDir * t;
        if (q.y > g_ren.terrainCeiling()) break;
        float h = q.y - g_world.height(q.x, q.z, 4);
        res = std::min(res, 12.f * h / t);
        if (res < 0.f) { res = 0.f; break; }
        t += clampf(h * 0.6f, 6.f, 450.f);
      }
    fp.planeTerrSh = clampf(res, 0.f, 1.f);
  }
  fp.cloudCover = wx.cloudCover; fp.cloudBase = wx.cloudBase;
  fp.fogB = 1.5f / std::max(wx.visibility, 500.f);
  fp.wet = wx.precip == 1 ? 1.f : 0.f; fp.snow = wx.precip == 2 ? 0.8f : 0.f;
  fp.storm = wx.storm ? 1.f : 0.f; fp.lightning = lightning;
  fp.windOff = cloudOff;
  fp.wind = vec3(-sinf(wx.windFrom * DEG), 0, cosf(wx.windFrom * DEG)) * wx.windSpeed;
  fp.exposure = 1.0f + fp.night * 0.8f;
  if ((screen == SCR_FLIGHT || screen == SCR_DEBRIEF || screen == SCR_LOADING) && plane.spec) {
    fillPlaneVisual(fp.plane, plane, propAngle, camMode == 1);
    if (camMode == 1 && wreck.empty()) {   // cockpit view: draw this frame's display / gauge atlas
      fp.dispCk = kModels[plane.spec - kAircraft].cockpit;
      fp.dispMode = plane.spec->special ? 1 : fp.dispCk == 2 ? 3 : 2;   // glass cockpits: panel + the centre display page
    }
    {   // transonic vapour cone: strongest just below Mach 1 in humid low-level air
      float M = plane.mach, humid = clampf(0.35f + 0.45f * wx.cloudCover + (wx.precip ? 0.3f : 0.f), 0.f, 1.f) * smoothstepf(11000.f, 1500.f, plane.pos.y);
      float k = smoothstepf(0.9f, 0.965f, M) * smoothstepf(1.07f, 1.0f, M) * humid * (plane.onGround || crashed ? 0.f : 1.f);
      float span = plane.spec->span;
      fp.plane.vapor[0] = k; fp.plane.vapor[1] = -1.f; fp.plane.vapor[2] = span * 0.2f; fp.plane.vapor[3] = span * 0.7f;
    }
    if (!wreck.empty()) {
      WreckVisual& wv = fp.wreck;
      wv.pieces = std::min((int)wreck.size(), 5);
      for (int i = 0; i < wv.pieces; i++) {
        const WreckPiece& w = wreck[i];
        wv.pos[i] = w.c; wv.C[i] = w.C; wv.H[i] = w.H;
        vec3 ax = w.q.rotate(vec3(1, 0, 0)), ay = w.q.rotate(vec3(0, 1, 0)), az = w.q.rotate(vec3(0, 0, 1));
        float r[9] = {ax.x, ax.y, ax.z, ay.x, ay.y, ay.z, az.x, az.y, az.z};
        memcpy(wv.rot[i], r, sizeof r);
      }
    }
    fp.wreck.debris = std::min((int)debris.size(), 16);
    for (int i = 0; i < fp.wreck.debris; i++) {
      const Debris& d = debris[i];
      fp.wreck.deb[i][0] = d.p.x; fp.wreck.deb[i][1] = d.p.y; fp.wreck.deb[i][2] = d.p.z; fp.wreck.deb[i][3] = d.charred ? -d.size : d.size;
      fp.wreck.debQ[i][0] = d.q.w; fp.wreck.debQ[i][1] = d.q.x; fp.wreck.debQ[i][2] = d.q.y; fp.wreck.debQ[i][3] = d.q.z;
    }
    fp.wreck.craterN = 0;
    if (craterR > 0) { float* c = fp.wreck.crater[fp.wreck.craterN++]; c[0] = craterX; c[1] = craterZ; c[2] = craterR; c[3] = craterD; }
    vec3 fwd = camMode == 1 ? plane.q.rotate(quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1))))
                            : normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - camPos);
    vec3 upRef = camMode == 1 ? plane.up() : vec3(0, 1, 0);
    if (camMode == 0) upRef = normalize(lerp(vec3(0, 1, 0), camQ.rotate(vec3(0, 1, 0)), 0.3f));
    if (dbgCam && dbgFollow) { dbgCamPos = plane.pos + dbgFollowOff; dbgCamLook = plane.pos; }
    if (dbgCam) { camPos = dbgCamPos; fwd = normalize(dbgCamLook - dbgCamPos); upRef = vec3(0, 1, 0); }
    fp.camPos = camPos;
    fp.camBack = -fwd;
    fp.camRight = normalize(cross(fwd, upRef));
    fp.camUp = cross(fp.camRight, fwd);
    fp.fovY = (camMode == 1 ? set.fov + 19.f : set.fov) * DEG;
    if (camMode == 3) fp.fovY = clampf(2.f * atanf(std::max(plane.spec->span, plane.spec->fusLen) * (botControl ? 0.42f : 1.5f) / length(plane.pos - camPos)), 4.f * DEG, 60.f * DEG);
    if (camMode == 1) fp.fovY = 2.f * atanf(tanf((set.fov + 19.f) * 0.5f * DEG) / ckZoom);   // cockpit zoom: lean in to read the displays
    fp.landLight = landingLight && plane.engineRunning ? (0.3f + 0.7f * fp.night) : 0.f;
    fp.landLightPos = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
    fp.landLightDir = normalize(plane.forward() - plane.up() * 0.1f);
    wraithVisual(fp);
    buildFeedCameras(fp);
    if (boomT >= 0 && boomT < 1.6f && !(length(fp.flameLight) > 0.f)) {   // an explosion's flash lights the scene, fading as the fireball cools
      float k = boomT < 0.08f ? boomT / 0.08f : expf(-(boomT - 0.08f) * 2.6f);
      fp.flameLightPos = boomP; fp.flameLight = vec3(1.f, 0.6f, 0.28f) * (boomI * k);
    }
    buildLights(fp);
    {   // airport lighting: on at night and in low visibility
      float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
      float li = clampf(fp.night + lowVis * 0.6f + 0.12f, 0, 1);
      fp.rwyLights = li > 0.15f ? li : 0.f;
    }
    // craters flatten the scenery that stood in them (a plasma blast clears a far wider circle than its pit)
    // (laser scorch pits don't: what a bolt destroys is tracked one object at a time)
    g_scenery.craters.clear();
    if (craterR > 0) g_scenery.craters.push_back(vec3(craterX, craterZ, craterR * 1.5f));
    for (const auto& c : wraith.craters) g_scenery.craters.push_back(vec3(c.x, c.z, c.R * 4.f));
    fp.rainLens = camMode == 1 && wx.precip == 1 ? 1.f : 0.f;
    fp.sealedCockpit = camMode == 1 && plane.spec->special && !crashed;
    fp.trafficN = traffic.fillVisuals(fp.camPos, fp.traffic, kMaxTrafficDrawn, nullptr);
    fp.ufoOn = ufo.on && length(ufo.pos - fp.camPos) < 20000.f;
    if (fp.ufoOn) {
      fp.ufoPos = ufo.pos;
      float m[9] = {ufo.right.x, ufo.right.y, ufo.right.z, ufo.up.x, ufo.up.y, ufo.up.z, ufo.fwd.x, ufo.fwd.y, ufo.fwd.z};
      memcpy(fp.ufoRot, m, sizeof m);
      fp.ufoAnim[0] = ufo.hatch; fp.ufoAnim[1] = ufo.t; fp.ufoAnim[2] = ufo.laugh; fp.ufoAnim[3] = ufo.wave;
    }
    if (crashed) fp.fade = clampf(1.f - (crashTimer - (crashEndT - 1.f)), 0, 1);
    fp.gLoad = gTunnel;
  } else {
    menuBackgroundCamera(fp);
  }
  return fp;
}

// Main menu: a tour of the islands - every 16 s a different place, aircraft, time of day and camera, cutting through black
struct MenuShot { const char* ap; int craft; float tod, cloud; int cam; float alt, turn; };
static const MenuShot kMenuShots[] = {
  {"PVI", 3, 9.0f, 0.30f, 0, 260.f, 35.f},    // west-coast port, morning, side chase
  {"VCF", 7, 17.6f, 0.25f, 1, 520.f, -20.f},  // the volcano at sunset, an XR-30 roaring past a fixed camera
  {"PMB", 1, 12.5f, 0.20f, 3, 150.f, 60.f},   // the lagoon at noon, trailing chase
  {"SMP", 4, 16.5f, 0.35f, 0, 320.f, -45.f},  // the Spine mountains
  {"CAP", 5, 19.1f, 0.30f, 2, 650.f, 15.f},   // the capital at dusk, lights coming on, high orbit
  {"FJH", 2, 11.0f, 0.45f, 1, 260.f, 80.f},   // the fjord
  {"LHK", 8, 7.0f, 0.25f, 0, 200.f, -70.f},   // Lighthouse Key at dawn, the XR-40
  {"MDB", 0, 14.0f, 0.35f, 3, 210.f, 25.f},   // Meadowbrook farmland
};
static const float kMenuShotLen = 16.f;
static const MenuShot& menuShot(float t) { int n = (int)floorf(t / kMenuShotLen); return kMenuShots[((n % 8) + 8) % 8]; }

void Game::menuTour(FrameParams& fp) {
  static Plane demo;
  static int lastCraft = -1;
  MenuShot S = menuShot(realTime);
  if (prewarmCraft >= 0) S.craft = prewarmCraft;
  float u = realTime - floorf(realTime / kMenuShotLen) * kMenuShotLen;
  int ai = std::max(0, g_world.findAirport(S.ap));
  const Airport& a = g_world.airports[ai];
  if (lastCraft != S.craft) { demo.reset(&kAircraft[S.craft], a.pos() + vec3(0, 500, 0), 0, kAircraft[S.craft].maxFuel, 100, true, kAircraft[S.craft].cruise); lastCraft = S.craft; }
  {   // the next place: its scenery is built in the background while this one plays
    const MenuShot& N = menuShot(realTime + kMenuShotLen);
    fp.prefetchOn = true; fp.prefetchPos = g_world.airports[std::max(0, g_world.findAirport(N.ap))].pos();
  }
  // the aircraft passes over the airfield at mid-shot, on a heading turned from the runway's
  float hd = a.heading + S.turn;
  vec3 dir(sinf(hd * DEG), 0, -cosf(hd * DEG)), right = normalize(cross(dir, vec3(0, 1, 0)));
  float v = std::min(kAircraft[S.craft].cruise, S.craft >= kResearchJet ? 140.f : 75.f);
  vec3 C = a.pos();
  vec3 p = C + dir * ((u - kMenuShotLen * 0.5f) * v);
  float g = std::max({g_world.height(p.x, p.z), g_world.height(p.x + dir.x * 400.f, p.z + dir.z * 400.f), g_world.height(p.x - dir.x * 400.f, p.z - dir.z * 400.f), 0.f});
  p.y = std::max(C.y + S.alt, g + S.alt * 0.7f);
  float bank = 6.f * sinf(u * 0.35f);
  demo.pos = p;
  demo.q = quat::axisAngle(vec3(0, 1, 0), -hd * DEG) * quat::axisAngle(vec3(0, 0, 1), -bank * DEG) * quat::axisAngle(vec3(1, 0, 0), 1.5f * DEG);
  demo.spec = &kAircraft[S.craft];
  demo.vel = dir * v; demo.onGround = false;
  demo.rpm = 2400; demo.gear = 0.f; demo.flaps = 0; demo.nozzle = 0; demo.ctl = Controls(); demo.ctl.throttle = 0.7f;
  demo.engineRunning = true; demo.engineSpool = 0.75f;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, prewarmInside);
  float size = std::max(demo.spec->span, demo.spec->fusLen), R = size * 2.4f + 10.f;
  vec3 up(0, 1, 0), cam, look;
  if (S.cam == 0) { cam = p + right * (R * 0.9f) + dir * (R * (0.45f - 0.03f * u)) + up * (R * 0.16f); look = p + dir * (size * 0.3f); fp.fovY = 50.f * DEG; }
  else if (S.cam == 1) {   // a fixed camera beside the path, panning as it flies past
    vec3 pm = C + dir * 0.f; pm.y = p.y;
    cam = pm + right * (40.f + size * 3.f) + dir * 90.f + up * -(size * 0.6f);
    cam.y = std::max(cam.y, g_world.height(cam.x, cam.z) + 15.f);
    look = p; fp.fovY = 38.f * DEG;
  } else if (S.cam == 2) {   // high slow orbit over the airfield
    float o = u * 0.03f + 0.6f;
    cam = C + vec3(cosf(o) * 1900.f, S.alt + 450.f, sinf(o) * 1900.f);
    cam.y = std::max(cam.y, g_world.height(cam.x, cam.z) + 300.f);
    look = C + vec3(0, 60.f, 0); fp.fovY = 46.f * DEG;
  } else { cam = p - dir * (R * 1.5f) + up * (R * 0.32f) + right * (R * 0.22f); look = p + dir * (size * 2.f); fp.fovY = 52.f * DEG; }
  fp.camPos = cam;
  vec3 fwd = normalize(look - cam);
  fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, up)); fp.camUp = cross(fp.camRight, fwd);
  fp.vignette = 0.9f;
  fp.fade = smoothstepf(0.f, 0.9f, u) * smoothstepf(kMenuShotLen, kMenuShotLen - 0.9f, u);   // cut through black
}

std::vector<std::pair<int, bool>> Game::prewarmItems(bool allCraft) {
  std::vector<std::pair<int, bool>> todo;
  for (int i = 0; i <= kWraith; i++) if (allCraft || !kAircraft[i].special) { todo.push_back({i, false}); todo.push_back({i, true}); }
  return todo;
}
std::string Game::prewarmLabel(int craft, bool inside, bool fresh) {
  return std::string(fresh ? "Building the " : "Loading the ") + kAircraft[craft].name + (inside ? " cockpit's mesh" : "'s mesh") + (fresh ? "  (once: kept for the next launch)" : "  from the cache");
}
// A start in the air (a career leg that opens airborne, an airborne research sortie): the gear up where it retracts, the
// parking brake and the wheel brakes off, so nothing holds the aircraft back or hangs out in the wind from the first frame
void Game::settleAirborneStart() {
  parkingBrake = false;
  plane.ctl.brake = 0.f; plane.brakeHold = 0;
  if (plane.spec && plane.spec->retract) { plane.ctl.gearDown = false; plane.gear = 0.f; }
}

void Game::prewarm(const std::function<void(float, const std::string&)>& progress, bool allCraft, LoadPacer* pace, int menuStep, const std::vector<int>* itemSteps) {
  if (screen != SCR_MENU) return;
  bool sync = g_ren.entSync;
  // (offscreen only for these frames: the intro may draw through the same UI path in between)
  auto frame = [&] { g_ren.setOffscreen(true); update(1.f / 60.f); render(); glFinish(); g_ren.setOffscreen(false); };
  // the tour's first place: every scenery chunk in range, the shadow maps and the terrain shadow
  g_ren.entSync = true;
  realTime = 3.f;
  if (pace && menuStep >= 0) pace->begin(menuStep);
  progress(pace ? pace->fraction() : 0.f, "Loading the menu's scenery and its terrain shadow");
  for (int i = 0; i < 12 && !quit; i++) {
    realTime = 3.f; frame();
    if (pace) { pace->setSub((i + 1) / 12.f); progress(pace->fraction(), fmt("Loading the menu's scenery and its terrain shadow  (%d chunks to go)", g_ren.entPending)); }
    if (i >= 2 && g_ren.entPending == 0 && !g_ren.tshPending()) break;
  }
  g_ren.entSync = sync;
  // every light aircraft's body (outside, and the cockpit's when that is in use; with allCraft the research jets' too):
  // a frame that wants one bakes it, or loads it from the cache
  const std::vector<std::pair<int, bool>> todo = prewarmItems(allCraft);
  for (size_t k = 0; k < todo.size() && !quit; k++) {
    prewarmCraft = todo[k].first; prewarmInside = todo[k].second;
    if (pace && itemSteps && k < itemSteps->size()) pace->begin((*itemSteps)[k]);
    // worded as a cache read; a bake that begins (its cache missing or unreadable) says so, and is timed as a build
    const int step = pace && itemSteps && k < itemSteps->size() ? (*itemSteps)[k] : -1;
    const std::string count = fmt("   %d of %d", (int)k + 1, (int)todo.size());
    progress(pace ? pace->fraction() : 0.35f + 0.65f * k / todo.size(), prewarmLabel(prewarmCraft, prewarmInside, false) + count);
    bool built = false;
    g_ren.onBakeStart = [&] {
      built = true;
      if (pace && step >= 0) pace->markFresh(step, true);
      progress(pace ? pace->fraction() : 0.35f + 0.65f * k / todo.size(), prewarmLabel(prewarmCraft, prewarmInside, true) + count);
    };
    realTime = 3.f; frame();   // bakes it at the end of the frame, if this view uses one
    g_ren.onBakeStart = nullptr;
    if (pace && step >= 0 && !built) pace->markFresh(step, false);
  }
  prewarmCraft = -1; prewarmInside = false;
  realTime = 0.f;
  if (pace) pace->end();
  progress(pace ? pace->fraction() : 1.f, "Ready");
}

void Game::menuBackgroundCamera(FrameParams& fp) {
  if (screen == SCR_MENU && !getenv("MENUORBIT")) { menuTour(fp); return; }
  if (screen == SCR_RESEARCH) { researchPreviewCamera(fp); return; }
  // A Wren circles over the islands while the camera chases it in a slow arc
  static Plane demo;
  static bool initd = false;
  if (!initd) { demo.reset(&kAircraft[screen == SCR_MENU ? 1 : 0], vec3(0, 600, 0), 0, 50, 100, true, 60); initd = true; }
  int ap = career.location;
  vec3 centre = screen == SCR_MENU ? vec3(-6000, 0, 11000) : g_world.airports[ap].pos();
  float R = 2200.f, t = realTime * 0.035f;
  float alt = (screen == SCR_MENU ? 380.f : 320.f) + std::max(centre.y, 0.f);
  vec3 p = centre + vec3(cosf(t) * R, 0, sinf(t) * R); p.y = std::max(alt, g_world.height(p.x, p.z) + 250.f);
  vec3 vdir = normalize(vec3(-sinf(t), 0, cosf(t)));
  float hdg = atan2f(vdir.x, -vdir.z) / DEG;
  demo.pos = p;
  demo.q = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG) * quat::axisAngle(vec3(0, 0, 1), -18.f * DEG) * quat::axisAngle(vec3(1, 0, 0), 2.f * DEG);
  bool res = screen == SCR_RESEARCH;
  demo.spec = &kAircraft[res ? resCraft : 1];
  demo.rpm = 2400; demo.gear = res ? 0.f : 1.f; demo.flaps = 0; demo.nozzle = 0; demo.ctl = Controls(); demo.ctl.throttle = res ? 0.6f : 0.f;
  fillPlaneVisual(fp.plane, demo, realTime * 250.f, false);
  float ca = realTime * 0.05f, cr = res ? 24.f : 16.f;
  vec3 off = vec3(cosf(ca) * cr, (res ? 5.f : 3.5f) + 2.f * sinf(ca * 0.7f), sinf(ca) * cr) + vdir * -6.f;
  fp.camPos = p + off;
  vec3 fwd = normalize(p - fp.camPos + vdir * 4.f);
  fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, fwd);
  fp.fovY = 50.f * DEG;
  fp.vignette = 0.9f;
  if (screen == SCR_HUB) {   // the hub's flights start at either end of this runway: their scenery streams in while the player chooses, so the loading screen has nothing left to build
    const Airport& A = g_world.airports[ap];
    fp.prefetchOn = true; fp.prefetchPos = A.threshold(((int)(realTime * 2.f)) & 1);
  }
}

// The preview: the selected craft hangs in the air over the chosen launch site, at the sortie's time of day, and the
// camera orbits it (drag to turn, wheel to zoom) framed so it sits in the middle of the terminal's preview ring
int resCraftCount(); int resCraftAt(int k);   // game_research_ui.cpp
void Game::researchPreviewCamera(FrameParams& fp) {
  static Plane demo;
  const AircraftSpec& sp = kAircraft[resWarmCraft >= 0 ? resWarmCraft : resCraft];   // (warming up: every craft in turn)
  if (demo.spec != &sp) demo.reset(&sp, vec3(0, 600, 0), 0, 50, 85, true, 150);
  const Airport& a = g_world.airports[std::clamp(resAirport, 0, (int)g_world.airports.size() - 1)];
  vec3 P; float hdg;
  if (resAirborne) {   // hanging in the air over the field, facing back towards it
    P = a.pos() + a.dir() * 1800.f;
    P.y = std::max(a.elev, g_world.height(P.x, P.z)) + 420.f + 1.2f * sinf(realTime * 0.6f);
    hdg = a.heading + 180.f;
    demo.q = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG) * quat::axisAngle(vec3(0, 0, 1), 4.f * DEG * sinf(realTime * 0.35f)) * quat::axisAngle(vec3(1, 0, 0), 3.f * DEG);
    demo.gear = 0; demo.ctl = Controls(); demo.ctl.throttle = 0.55f; demo.engineSpool = 0.55f; demo.n1 = 70.f; demo.onGround = false;
  } else {   // parked where the flight starts: the runway's threshold, into the sortie's wind (startFlight's choice)
    float hw0 = cosf((270.f - a.heading) * DEG), hw1 = cosf((270.f - a.heading - 180.f) * DEG);   // (a research sortie's wind is from 270)
    bool reverse = hw1 > hw0;
    hdg = reverse ? a.heading + 180.f : a.heading;
    P = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
    demo.q = quat::axisAngle(vec3(0, 1, 0), -hdg * DEG);
    demo.gear = 1; demo.ctl = Controls(); demo.ctl.throttle = 0.f; demo.engineSpool = 0.f; demo.n1 = 0.f; demo.onGround = true;
    P.y = std::max(a.elev, g_world.height(P.x, P.z)) + demo.gearHeight() + (sp.taildragger ? 0.25f : 0.05f);
  }
  demo.pos = P; demo.rpm = 0; demo.flaps = 0; demo.nozzle = 0;
  fillPlaneVisual(fp.plane, demo, 0.f, resWarm && resWarmCk);   // (warming up, each craft's cockpit for a frame too: its cabin shell bakes now, not when the sortie opens)
  resPrevPos = P; resPrevQ = demo.q;
  // orbit
  ResLayout L = researchLayout();
  float W = (float)g_ren.W, H = (float)g_ren.H;
  float ext = std::max(sp.span, sp.fusLen) * 0.46f;   // (the craft fills the ring)
  fp.fovY = 32.f * DEG;
  float th = tanf(fp.fovY * 0.5f);
  float D = (H * 0.5f / L.r) * ext / th / std::clamp(resZoom, 0.6f, 1.8f);
  float yaw = resYaw + a.heading * DEG, pit = std::clamp(resPitch, -0.6f, 1.1f);
  if (!resAirborne) pit = std::max(pit, 0.04f);   // (on the ground the camera never dips below the apron)
  vec3 C = P + vec3(sinf(yaw) * cosf(pit), sinf(pit), -cosf(yaw) * cosf(pit)) * D;
  if (!resAirborne) C.y = std::max(C.y, g_world.height(C.x, C.z) + 1.6f);
  vec3 f0 = normalize(P - C), r0 = normalize(cross(f0, vec3(0, 1, 0))), u0 = cross(r0, f0);
  float nx = (L.cx - W * 0.5f) / (W * 0.5f), ny = (H * 0.5f - L.cy) / (H * 0.5f);
  vec3 T = P - r0 * (nx * D * th * (W / H)) - u0 * (ny * D * th);   // aim off-centre so the craft lands in the ring
  vec3 fwd = normalize(T - C);
  fp.camPos = C; fp.camBack = -fwd; fp.camRight = normalize(cross(fwd, vec3(0, 1, 0))); fp.camUp = cross(fp.camRight, fwd);
  fp.vignette = 0.75f;
}

void Game::buildSprites(const FrameParams& fp, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add) {
  vec3 cr = fp.camRight;
  // billboards are turned to face whichever camera draws them (the main view or a cockpit camera) in the vertex shader
  auto bill = [&](std::vector<SpriteVert>& v, vec3 p, float s, vec3 col, float a, int kind, float soft) {
    SpriteVert q[4] = {{p.x, p.y, p.z, 0, 0, col.x, col.y, col.z, a, (float)kind, soft, s}, {p.x, p.y, p.z, 1, 0, col.x, col.y, col.z, a, (float)kind, soft, s},
                       {p.x, p.y, p.z, 1, 1, col.x, col.y, col.z, a, (float)kind, soft, s}, {p.x, p.y, p.z, 0, 1, col.x, col.y, col.z, a, (float)kind, soft, s}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  auto quadAx = [&](std::vector<SpriteVert>& v, vec3 p, vec3 ax, vec3 ay, vec3 col, float a, int kind, float soft) {
    vec3 c0 = p - ax - ay, c1 = p + ax - ay, c2 = p + ax + ay, c3 = p - ax + ay;
    SpriteVert q[4] = {{c0.x, c0.y, c0.z, 0, 0, col.x, col.y, col.z, a, (float)kind, soft}, {c1.x, c1.y, c1.z, 1, 0, col.x, col.y, col.z, a, (float)kind, soft},
                       {c2.x, c2.y, c2.z, 1, 1, col.x, col.y, col.z, a, (float)kind, soft}, {c3.x, c3.y, c3.z, 0, 1, col.x, col.y, col.z, a, (float)kind, soft}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  float night = fp.night;
  float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
  float lightI = clampf(night + lowVis * 0.6f + 0.12f, 0, 1);
  // runway lighting
  for (size_t ai = 0; ai < g_world.airports.size(); ai++) {
    const Airport& a = g_world.airports[ai];
    float d = length(a.pos() - fp.camPos);
    if (d > 16000.f) continue;
    vec3 dir = a.dir(), rt(-dir.z, 0, dir.x);
    if (lightI > 0.15f) {
      for (float u = -a.length * 0.5f; u <= a.length * 0.5f + 0.1f; u += 60.f)
        for (int sd = -1; sd <= 1; sd += 2) {
          vec3 p = a.pos() + dir * u + rt * (sd * (a.width * 0.5f + 1.5f)) + vec3(0, 0.4f, 0);
          vec3 col = fabsf(u) > a.length * 0.5f - 600.f && a.size > 0 ? vec3(1.f, 0.85f, 0.45f) : vec3(1.f, 0.92f, 0.75f);
          float dl = length(p - fp.camPos);   // glints at range; close up the modelled fixture and its pool are the light
          bill(add, p, dl * 0.0009f, col * (2.0f * lightI) * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);   // a pixel-sized point at range
        }
      AptLayout L = aptLayout(a, (int)ai);
      if (L.paved) {   // blue taxiway edge lights (the same positions as the fixtures in airport_scenery.cpp)
        auto blue = [&](float u, float vv) {
          vec3 p = aptWorld(a, u, L.side * vv, a.elev + 0.4f);
          float dl = length(p - fp.camPos);
          bill(add, p, dl * 0.0012f, vec3(0.2f, 0.35f, 1.f) * 2.2f * lightI * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);
        };
        float tEnd = a.length * 0.5f - 25.f + L.twHW;
        for (float tu = -tEnd; tu <= tEnd; tu += 60.f) {
          bool atExit = false;
          for (int e = 0; e < L.nExit; e++) if (fabsf(tu - L.exitU[e]) < L.twHW + 4.f) atExit = true;
          if (!atExit) blue(tu, L.twV - L.twHW - 1.f);
          if (!(tu > L.apU0 && tu < L.apU1)) blue(tu, L.twV + L.twHW + 1.f);
        }
      }
      for (int end = -1; end <= 1; end += 2) {
        for (float v = -a.width * 0.5f; v <= a.width * 0.5f; v += 3.f) {
          vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 1.f)) + rt * v + vec3(0, 0.4f, 0);
          float dl = length(p - fp.camPos);
          bill(add, p, dl * 0.0009f, (end < 0 ? vec3(0.2f, 1.f, 0.3f) : vec3(1.f, 0.15f, 0.1f)) * 2.0f * lightI * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);
        }
        if (a.size > 0 && night > 0.3f)
          for (int k = 1; k <= 6; k++)
            for (float v = -8.f; v <= 8.f; v += 4.f) {
              vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 60.f * k)) + rt * v;
              p.y = std::max(g_world.height(p.x, p.z), a.elev) + 1.f;
              float dl = length(p - fp.camPos);
              bill(add, p, dl * 0.0009f, vec3(1.f, 0.95f, 0.85f) * 2.5f * night * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 3.f);
            }
      }
    }
    // PAPI (always on) on the left of each landing direction, 300 m past the threshold
    if (a.size > 0 || a.surface == SURF_ASPHALT) {
      for (int end = -1; end <= 1; end += 2) {
        vec3 ldir = dir * (float)(-end);              // landing direction
        vec3 thr = a.pos() + dir * (end * a.length * 0.5f);
        vec3 lrt(-ldir.z, 0, ldir.x);
        vec3 base = thr + ldir * 300.f - lrt * (a.width * 0.5f + 15.f) + vec3(0, 0.6f, 0);
        vec3 toCam = fp.camPos - base;
        float horiz = length(vec3(toCam.x, 0, toCam.z));
        if (dot(vec3(toCam.x, 0, toCam.z), ldir) > 0) continue;  // only visible from the approach side
        float ang = atan2f(toCam.y, horiz) / DEG;
        for (int i = 0; i < 4; i++) {
          float th = 2.5f + i * 0.333f;   // outer light has the highest threshold
          bool white = ang > (3.5f - i * 0.333f);
          (void)th;
          vec3 p = base - lrt * (i * 9.f);
          float dl = length(p - fp.camPos);   // the modelled unit carries it up close; a pixel-sized point beyond
          bill(add, p, dl * 0.0009f, (white ? vec3(1.f, 0.95f, 0.9f) : vec3(1.f, 0.1f, 0.08f)) * 3.0f * smoothstepf(150.f, 500.f, dl), 1.f, SPR_GLOW, 3.f);
        }
      }
    }
  }
  if ((screen == SCR_FLIGHT || screen == SCR_LOADING) && plane.spec && !crashed) {   // nav light glints
    const AircraftSpec& s = *plane.spec;
    float t = realTime;
    float dcam = length(plane.pos - fp.camPos);
    float ls = std::max(0.05f, dcam * 0.0012f);   // distant glints only: up close the modelled lens is the light
    float glint = 0.2f + 0.8f * smoothstepf(20.f, 110.f, dcam);
    float navI = (0.6f + 2.5f * night) * (1.f - wraith.stealth) * glint;   // a cloaked XR-40 runs dark
    const ModelDef& md = kModels[plane.spec - kAircraft];
    bool jet = s.special != 0;   // the XR-30 is an SDF of its own: its lights don't follow the generic model layout
    vec3 tip = s.special == 2 ? kWraithWingTip : jet ? kJetWingTip : modelWingTip(md);
    vec3 lt = plane.pos + plane.q.rotate(vec3(-tip.x, tip.y, tip.z)), rtp = plane.pos + plane.q.rotate(tip);
    if (camMode != 1) {
      bill(add, lt, ls, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rtp, ls, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, plane.pos + plane.q.rotate(s.special == 2 ? vec3(0, -0.1f, 7.93f) : jet ? vec3(0, 0.45f, 7.67f) : modelTailTip(md)), ls, vec3(1.f) * navI, 1, SPR_GLOW, 0.3f);
      if (plane.engineRunning && wraith.stealth < 0.5f && fmodf(t, 1.0f) < 0.12f) bill(add, plane.pos + plane.q.rotate(s.special == 2 ? vec3(0, 0.6f, 1.6f) : jet ? vec3(0, 0.74f, 1.6f) : modelFinTop(md) + vec3(0, 0.06f, 0)), ls * 1.6f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night) * glint, 1, SPR_GLOW, 0.3f);
      float st = fmodf(t, 1.3f);
      if (!plane.onGround && wraith.stealth < 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f))) { bill(add, lt, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); bill(add, rtp, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); }
    }
    // AI traffic lights: nav lights, beacon, strobes when airborne, landing lights on the runway and on approach,
    // reheat glow on XR-30 formations
    for (const TrafficCraft& c : traffic.craft) {
      float dc = length(c.pos - fp.camPos);
      if (dc > 9000.f) continue;
      const ModelDef& tm = kModels[c.spec];
      float ls2 = std::max(0.05f, dc * 0.0012f), g2 = 0.2f + 0.8f * smoothstepf(20.f, 110.f, dc);   // glints at range; close up the lenses glow
      vec3 tip = c.spec == kResearchJet ? kJetWingTip : modelWingTip(tm);
      vec3 lt = c.pos + c.q.rotate(vec3(-tip.x, tip.y, tip.z)), rt = c.pos + c.q.rotate(tip);
      float tnav = (0.6f + 2.5f * night) * g2;
      bill(add, lt, ls2, vec3(1.f, 0.1f, 0.05f) * tnav, 1, SPR_GLOW, 0.3f);
      bill(add, rt, ls2, vec3(0.1f, 1.f, 0.2f) * tnav, 1, SPR_GLOW, 0.3f);
      float ph = fmodf(t + c.id * 0.37f, 1.f);
      if (ph < 0.1f) bill(add, c.pos + c.q.rotate(vec3(0, kAircraft[c.spec].fusRad * 1.05f, 0)), ls2 * 1.4f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night) * g2, 1, SPR_GLOW, 0.3f);
      bool airborne = c.state >= TrafficCraft::TAKEOFF && c.state <= TrafficCraft::ROLLOUT;
      if ((airborne || c.role != TrafficCraft::AIRPORT) && fmodf(t * 0.77f + c.id * 0.13f, 1.3f) < 0.05f) { bill(add, lt, ls2 * 2.5f, vec3(4.f) * g2, 1, SPR_GLOW, 0.3f); bill(add, rt, ls2 * 2.5f, vec3(4.f) * g2, 1, SPR_GLOW, 0.3f); }
      if (c.role == TrafficCraft::AIRPORT && (c.state == TrafficCraft::TAKEOFF || c.state == TrafficCraft::FINAL || c.state == TrafficCraft::ROLLOUT || c.state == TrafficCraft::LINEUP))
        bill(add, c.pos + c.q.rotate(vec3(0, -kAircraft[c.spec].fusRad * 0.6f, -kAircraft[c.spec].fusLen * 0.4f)), ls2 * 2.f, vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night), 1, SPR_GLOW, 0.3f);
      if (c.ab > 0.5f) for (int k = -1; k <= 1; k += 2)
        bill(add, c.pos + c.q.rotate(vec3(k * 0.82f, -0.12f, 8.9f)), 0.3f + ls2, vec3(1.f, 0.65f, 0.35f) * 1.6f, 1, SPR_GLOW, 0.6f);
    }
    if (landingLight && plane.engineRunning && camMode != 1)
      bill(add, plane.pos + plane.q.rotate(vec3(s.engLayout == 0 ? -s.span * 0.25f : 0, s.engLayout == 0 ? s.wingY * s.fusRad : -s.fusRad * 0.6f, s.engLayout == 0 ? s.wingZ - s.chord * 0.5f : -0.35f * s.fusLen)), ls * 1.8f,
           vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night) * glint, 1, SPR_GLOW, 0.3f);
    // checkpoint gates: the active one spins, breathes and shows a moving breadcrumb trail to the next gate
    for (int i = wpIndex; i < (int)contract.wps.size() && i < wpIndex + 3; i++) {
      vec3 c, ax, ay;
      ringGeom(i, c, ax, ay);
      bool act = i == wpIndex;
      float br = act ? 1.f + 0.035f * sinf(t * 2.6f) : 0.85f;
      vec3 col = act ? vec3(0.2f, 1.f, 0.5f) * 2.2f : vec3(0.85f, 0.4f, 1.f) * 1.1f;
      quadAx(add, c, ax * br, ay * br, col, act ? 1.f : 0.55f, SPR_RING, 5.f);
      if (act) quadAx(add, c, ax * 1.25f, ay * 1.25f, col * 0.35f, 0.2f, SPR_SHOCK, 5.f);   // faint outer halo
      vec3 c2, ax2, ay2;
      if (act && ringGeom(i + 1, c2, ax2, ay2)) {
        float len = length(c2 - c);
        int n = std::min(14, std::max(3, (int)(len / 180.f)));
        for (int k = 0; k < n; k++) {
          float f = (k + fmodf(t * 0.8f, 1.f)) / n;
          float fade = sinf(f * 3.14159f);
          bill(add, c + (c2 - c) * f, std::max(9.f, length(c + (c2 - c) * f - fp.camPos) * 0.012f), vec3(0.85f, 0.4f, 1.f) * 3.f * fade, 1.f, SPR_SPARK, 5.f);
        }
      }
    }
    for (const RingBurst& b : bursts) {
      float k = b.t / 0.9f, s = 1.f + 2.2f * (1.f - (1.f - k) * (1.f - k));
      quadAx(add, b.c, b.ax * s, b.ay * s, b.col * 3.f, (1.f - k) * (1.f - k), SPR_SHOCK, 5.f);
      if (k < 0.35f) bill(add, b.c, 90.f * (0.5f + k * 3.f), b.col * 2.f * (1.f - k / 0.35f), 1.f, SPR_GLOW, 5.f);
    }
  }
  // trail ribbons: camera-facing strips through trail points, soft across their width (wingtip vapour; the smoke
  // trailing from break-up pieces)
  auto ribbon = [&](const std::vector<TipPt>& tt, float life, float w0, float wg, vec3 lit, float aMax) {
    for (size_t i = 1; i < tt.size(); i++) {
      const TipPt &q0 = tt[i - 1], &q1 = tt[i];
      if (q0.seg != q1.seg) continue;
      auto edge = [&](const TipPt& q, vec3 dir, vec3& lo, vec3& hi, float& al) {
        vec3 vd = normalize(q.p - fp.camPos), side = cross(dir, vd);
        float sl = length(side); side = sl > 1e-4f ? side / sl : cr;
        float w = w0 + q.age * wg;
        lo = q.p - side * w; hi = q.p + side * w;
        float lf = 1.f - q.age / life;
        al = aMax * q.a * lf * lf * smoothstepf(0.f, 0.05f, q.age) * smoothstepf(0.3f, 4.f, length(q.p - fp.camPos));
      };
      vec3 dir = q1.p - q0.p; float dl = length(dir);
      if (dl < 1e-3f) continue;
      dir = dir / dl;
      vec3 a0, b0, a1, b1; float al0, al1;
      edge(q0, dir, a0, b0, al0); edge(q1, dir, a1, b1, al1);
      if (al0 + al1 < 0.002f) continue;
      SpriteVert v0 = {a0.x, a0.y, a0.z, 0.5f, 0, lit.x, lit.y, lit.z, al0, 8.f, 1.5f}, v1 = {b0.x, b0.y, b0.z, 0.5f, 1, lit.x, lit.y, lit.z, al0, 8.f, 1.5f};
      SpriteVert v2 = {b1.x, b1.y, b1.z, 0.5f, 1, lit.x, lit.y, lit.z, al1, 8.f, 1.5f}, v3 = {a1.x, a1.y, a1.z, 0.5f, 0, lit.x, lit.y, lit.z, al1, 8.f, 1.5f};
      alpha.push_back(v0); alpha.push_back(v1); alpha.push_back(v2); alpha.push_back(v0); alpha.push_back(v2); alpha.push_back(v3);
    }
  };
  for (int sd = 0; sd < 2; sd++) ribbon(tipTrail[sd], 1.8f, 0.22f, 0.9f, vec3(1.f), 0.26f);
  for (int k = 0; k < 5; k++) ribbon(pieceTrail[k], 7.f, 0.9f, 2.4f, vec3(0.07f, 0.065f, 0.06f), 0.75f);
  // break-up pieces burning as they fall: a flame jet streaming a few metres off each one
  if (airBreak) {
    for (const WreckPiece& w : wreck) {
      if (w.landed) continue;
      float sp = length(w.v); if (sp < 1.f) continue;
      float heat = w.fire * clampf(1.3f - crashTimer * 0.08f, 0.35f, 1.f);
      vec3 vd = w.v / sp, toCam = normalize(fp.camPos - w.c);
      vec3 ax = vd - toCam * dot(vd, toCam); float al = length(ax);
      ax = al > 0.05f ? ax / al : cr;
      vec3 ay = normalize(cross(ax, toCam));
      float flick = 0.85f + 0.15f * sinf(realTime * 37.f + w.C.x * 13.f);
      float L = clampf(sp * 0.025f, 2.5f, 11.f) * heat * flick * std::max(al, 0.3f), Wd = (0.45f + 0.35f * w.fire) * heat;
      quadAx(add, w.c - ax * (L * 0.5f), ax * (L * 0.5f + Wd), ay * Wd, vec3(1.f, 0.55f, 0.18f) * 2.6f, heat, SPR_SPARK, 1.f);
      quadAx(add, w.c - ax * (L * 0.35f), ax * (L * 0.35f + Wd * 2.f), ay * Wd * 2.2f, vec3(1.f, 0.4f, 0.1f) * 0.8f, heat * 0.6f, SPR_SPARK, 1.f);
    }
  }
  // particles
  vec3 camVel = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
  std::vector<std::pair<float, const Particle*>> order;
  for (const Particle& p : particles) order.push_back({-length(p.p - fp.camPos), &p});
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& o : order) {
    const Particle& p = *o.second;
    float fade = clampf(p.life / p.maxLife, 0, 1);
    float a = p.alpha * (p.kind == SPR_FIRE || p.instant ? fade : fade * smoothstepf(0.f, 0.15f, 1.f - fade + 0.15f));
    if (p.kind == SPR_SPARK) {   // hot embers: streaks along their motion relative to the camera (1/60 s shutter)
      vec3 rv = p.v - camVel, vd = normalize(p.p - fp.camPos);
      vec3 perp = rv - vd * dot(rv, vd);
      float pl = length(perp), len = std::min(std::min(pl / 60.f * 0.6f, 2.5f), pl * (p.maxLife - p.life));   // never longer than its path since it left the nozzle
      vec3 ax = pl > 0.01f ? perp / pl : cr, ay = normalize(cross(ax, vd));
      quadAx(add, p.p - ax * (len * 0.5f), ax * (len * 0.5f + p.size), ay * p.size, p.col, fade, p.kind, 1.f);
    }
    else if (p.kind == SPR_FLAME) {   // billowing flame: white-hot yellow, then deep red, then soot (alpha-blended)
      float t = 1.f - fade;
      vec3 hot = p.col * 3.f, red = vec3(1.f, 0.28f, 0.06f) * 1.3f, soot = vec3(0.05f, 0.045f, 0.04f);
      vec3 col = t < 0.4f ? lerp(hot, red, t / 0.4f) : lerp(red, soot, std::min(1.f, (t - 0.4f) / 0.4f));
      float al = p.alpha * smoothstepf(0.f, 0.06f, 1.f - fade + 0.02f) * (t < 0.8f ? 1.f : 1.f - (t - 0.8f) / 0.2f);
      bill(alpha, p.p, std::max(p.size, 0.05f), col, al, p.kind, 1.f);
    }
    else if (p.kind == SPR_FIRE) {   // flame cools from its spawn colour toward a deep red as it ages
      float cool = 1.f - fade;
      vec3 col = lerp(p.col, vec3(0.9f, 0.18f, 0.04f) * (p.col.x + p.col.y + p.col.z) * 0.4f, cool * cool);
      bill(add, p.p, std::max(p.size, 0.05f), col, a, p.kind, 1.f);
    }
    else bill(alpha, p.p, p.size, p.col, a, p.kind, 1.f);
  }
  // precipitation around the camera
  if (screen == SCR_FLIGHT && wx.precip > 0) {
    const int N = wx.precip == 1 ? 700 : 1400;
    if (rainDrops.size() != (size_t)N) { rainDrops.resize(N); Rng r(5); for (auto& d : rainDrops) d = vec3(r.range(-30, 30), r.range(-20, 20), r.range(-30, 30)); }
    vec3 camV = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
    bool rain = wx.precip == 1;
    vec3 fall = rain ? vec3(0, -9.f, 0) : vec3(0, -1.2f, 0);
    vec3 wv = plane.windVel;
    vec3 rel = fall + wv - camV;
    for (size_t i = 0; i < rainDrops.size(); i++) {
      vec3& d = rainDrops[i];
      float tt = realTime;
      vec3 p = d + rel * fmodf(tt * 0.6f + (i % 37) * 0.13f, 1.0f) * 0.12f * (rain ? 6.f : 1.f);
      if (!rain) p = p + vec3(sinf(tt * 1.3f + i), 0, cosf(tt * 1.1f + i * 0.7f)) * 0.5f;
      // wrap into the box around the camera
      auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
      vec3 wpos(wrap(p.x, 30.f), wrap(p.y, 20.f), wrap(p.z, 30.f));
      wpos = wpos + fp.camPos;
      if (rain) {
        vec3 dir = normalize(rel); float len = clampf(length(rel) * 0.035f, 0.3f, 3.f);
        vec3 side = normalize(cross(dir, wpos - fp.camPos)) * 0.012f;
        quadAx(alpha, wpos, side, dir * len, vec3(0.75f, 0.78f, 0.85f), 0.35f, SPR_RAIN, 0.5f);
      } else {
        bill(alpha, wpos, 0.09f, vec3(1, 1, 1), 0.9f, SPR_SNOW, 0.5f);
      }
    }
  }
}

// ------------------------------------------------------------------ audio
// ------------------------------------------------------------------ tower controller
// The towers speak only from what is actually happening on this flight: a greeting at the departure airport, the
// takeoff clearance for the runway the aircraft is on (with the real wind), a handoff once it is clear of the field
// (or "remain in the pattern" for circuits), pattern-entry or straight-in instructions on the way into the
// destination, the landing clearance once the aircraft is established on final for a runway, and "exit the runway"
// on the landing roll. A go-around (climbing away after the clearance) brings a fresh approach call and clearance.
// Only fields with a tower (size 1 and up) talk; the strips are uncontrolled.
// They call the aircraft by its registration (the one painted on it): in full on first contact with each tower, then
// abbreviated (SX-ABC: "Sierra X-ray Alfa Bravo Charlie", then "Sierra Bravo Charlie"). The AI traffic at the field
// is in the exchange too: a departure holds for traffic on final or on the runway, an arrival is told it is number two
// behind an aircraft ahead of it on final (with a wake caution behind a much heavier one), the landing clearance waits
// while the runway is occupied, and an aircraft still on the runway on short final means a go-around.
void Game::updateAtc(float dt) {
  if (!atc.ok()) return;
  AtcFlight& F = atcF;
  F.t += dt; F.waitT += dt;
  if (F.dep < 0 || F.arr < 0) return;
  const float kt = MS_TO_KT;
  float agl = plane.agl(), gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  if (!plane.onGround && agl > 15.f) F.airborne = true;
  if (F.holding && F.phase == 1 && (length(plane.pos - F.holdPos) > 40.f || F.airborne)) { result.holdViolated = true; F.holding = false; }   // (moved off the hold, or took off)
  const std::string reg = registrationOf(*plane.spec);   // "SX-ABC"
  auto callsign = [&](int v, bool arrival, std::vector<std::string>& ids, std::string& txt) {
    int k = arrival && F.dep != F.arr;
    bool full = !F.called[k];
    std::string say = full ? reg.substr(0, 2) + reg.substr(3) : reg.substr(0, 1) + reg.substr(4);
    for (char ch : say) ids.push_back(atc.alpha(v, ch));
    ids.push_back("");
    txt = (full ? reg : reg.substr(0, 1) + "-" + reg.substr(4)) + ", ";
    return full ? k + 1 : 0;   // the Tx tag: updateComms marks the full form heard when it starts
  };
  // the AI traffic at an airport as its tower sees it: anything on the runway (lining up, on its takeoff run or its
  // landing roll), and the nearest aircraft on final or turning onto it, with its distance from the field
  struct RwyTraffic { bool onRunway = false, departing = false; const TrafficCraft* fin = nullptr; float finD = 1e9f; };
  auto rwyTraffic = [&](int ai) {
    RwyTraffic r; const Airport& ap = g_world.airports[ai];
    for (auto& o : traffic.craft) {
      if (!o.alive || o.role != TrafficCraft::AIRPORT || o.airport != ai) continue;
      if (o.state == TrafficCraft::LINEUP || o.state == TrafficCraft::TAKEOFF) r.onRunway = r.departing = true;
      if (o.state == TrafficCraft::ROLLOUT) r.onRunway = true;
      vec3 d = o.pos - ap.pos(); d.y = 0;
      bool fin = o.state == TrafficCraft::FINAL || (o.state == TrafficCraft::CIRCUIT && o.wp >= 3);
      if (fin && length(d) < 7000.f && length(d) < r.finD) { r.fin = &o; r.finD = length(d); }
    }
    return r;
  };
  auto runway = [&](int v, int n, std::vector<std::string>& ids) { ids.push_back(atc.digit(v, n / 10)); ids.push_back(atc.digit(v, n % 10)); };
  auto wind = [&](int v, std::vector<std::string>& ids, std::string& txt) {   // the actual wind, or nothing when calm
    int spd = (int)lroundf(wx.windSpeed * kt), gst = (int)lroundf((wx.windSpeed + wx.gust) * kt);
    if (spd < 3) return;
    int hdg = (int)lroundf(wrapDeg360(wx.windFrom) / 10.f) * 10; if (hdg == 0) hdg = 360;
    ids.push_back(atc.atom(v, "wind"));
    for (int d : {hdg / 100, (hdg / 10) % 10, 0}) ids.push_back(atc.digit(v, d));
    ids.push_back(atc.atom(v, "at"));
    auto num = [&](int k) { if (k >= 10) ids.push_back(atc.digit(v, k / 10)); ids.push_back(atc.digit(v, k % 10)); };
    num(spd);
    txt += fmt("wind %03d at %d", hdg, spd);
    if (gst >= spd + 5) { ids.push_back(atc.atom(v, "gusting")); num(gst); txt += fmt(" gusting %d", gst); }
    ids.push_back(atc.atom(v, "knots")); txt += " knots. ";
    ids.push_back("");
  };
  const Airport& D = g_world.airports[F.dep];
  const Airport& A = g_world.airports[F.arr];
  int vd = atcStation(F.dep), va = atcStation(F.arr);
  // the smallest fields (farm strips, island and mountain strips: size 0) have no tower: nothing is said there, and a
  // departure from one goes straight to the arrival exchange (if the destination has a tower)
  if (F.phase <= 2 && D.size == 0) F.phase = 3;
  if (F.phase >= 3 && A.size == 0) return;
  switch (F.phase) {
    case 0:   // on the ground at the departure airport: the greeting
      if (F.t > 2.5f) {
        const char* key = timeOfDay < 12.f ? "greeting_morning" : timeOfDay < 18.f ? "greeting_afternoon" : "greeting_evening";
        AtcVoice::Tx tx; tx.prio = 10; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(vd, false, tx.ids, tx.text);
        tx.ids.push_back(atc.line(vd, key)); tx.text += atc.text(tx.ids.back());
        tx.apt = F.dep; F.phase = 1; tx.key = atcKey(); atc.say(tx); F.waitT = 0;
      }
      break;
    case 1:   // lined up on the runway: cleared for takeoff (straight away if the aircraft is already rolling)
      if ((F.spoken >= 1 && F.waitT > 4.f && !atc.busy()) || gs > 4.f) {   // (after the greeting has been heard)
        // traffic landing or departing first: hold (the clearance comes once it's clear, or after 90 s regardless)
        RwyTraffic rt = rwyTraffic(F.dep);
        if (gs <= 4.f && (rt.onRunway || (rt.fin && rt.finD < 4500.f)) && F.trafficT < 90.f) {
          F.trafficT += dt;
          if (!F.trafficSaid) {
            AtcVoice::Tx tx; tx.prio = 85; tx.subtitle = true; tx.group = "tower";
            tx.tag = callsign(vd, false, tx.ids, tx.text);
            tx.ids.push_back(atc.line(vd, rt.departing ? "hold_departure" : rt.onRunway ? "hold_position" : "hold_arrival"));
            tx.text += atc.text(tx.ids.back());
            tx.apt = F.dep; tx.key = atcKey(); atc.say(tx); F.trafficSaid = true; F.waitT = 0;
            F.holding = true; F.holdPos = plane.pos;
          }
          break;
        }
        F.holding = false;
        AtcVoice::Tx tx; tx.prio = 90; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(vd, false, tx.ids, tx.text);
        wind(vd, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(vd, "runway")); runway(vd, D.rwyNumber(F.depRev), tx.ids); tx.ids.push_back(atc.atom(vd, "cleared_takeoff"));
        tx.text += fmt("Runway %02d, cleared for takeoff.", D.rwyNumber(F.depRev));
        tx.apt = F.dep; F.phase = 2; tx.key = atcKey(); atc.say(tx); F.trafficSaid = false; F.trafficT = 0;
      }
      break;
    case 2:   // climbing out, clear of the field: handed on (circuits stay with the tower)
      if (F.airborne && agl > 120.f && length(plane.pos - D.pos()) > 1500.f) {
        AtcVoice::Tx tx; tx.prio = 40; tx.subtitle = true; tx.group = "tower";
        if (F.dep == F.arr) { tx.ids.push_back(atc.line(vd, "remain_pattern")); tx.text = atc.text(tx.ids[0]); }
        else { tx.ids = {atc.line(vd, "contact_departure"), "", atc.line(vd, "good_day")}; tx.text = atc.text(tx.ids[0]) + " " + atc.text(tx.ids[2]); }
        tx.apt = F.dep; F.phase = F.dep == F.arr ? 4 : 3; tx.key = atcKey(); atc.say(tx); F.waitT = 0;
        if (F.dep == F.arr) F.arrRev = F.depRev;
      }
      break;
    case 3: {   // inbound: within 9 km and below 1500 m, heading for the field: the arrival runway and how to join
      vec3 toA = A.pos() - plane.pos; toA.y = 0;
      float dist = length(toA);
      if (F.airborne && dist < 9000.f && agl < 1500.f && dot(plane.vel, toA) > 0.f) {
        // the runway end: the one the aircraft is already lined up on (unless that means a tailwind of 10 kt or
        // more), else the one into the wind, or in light winds the one facing the aircraft's arrival
        auto head = [&](bool rev) { return A.heading + (rev ? 180.f : 0.f); };
        float wkt = wx.windSpeed * kt;
        int lined = -1;
        for (int r = 0; r < 2; r++) {
          vec3 d = r ? -A.dir() : A.dir(), q = plane.pos - A.threshold(r != 0); q.y = 0;
          float al = dot(q, d), la = fabsf(q.x * d.z - q.z * d.x);
          float tail = -cosf((wx.windFrom - head(r != 0)) * DEG) * wkt;
          if (al < -500.f && al > -12000.f && la < 0.25f * -al + 300.f && cosf((plane.heading() - head(r != 0)) * DEG) > 0.9f && tail < 10.f) lined = r;
        }
        if (lined >= 0) F.arrRev = lined == 1;
        else if (wkt >= 6.f) F.arrRev = cosf((wx.windFrom - head(true)) * DEG) > cosf((wx.windFrom - head(false)) * DEG);
        else F.arrRev = dot(plane.pos - A.pos(), A.dir()) > 0.f;
        vec3 ld = F.arrRev ? -A.dir() : A.dir(), thr = A.threshold(F.arrRev);
        vec3 rel = plane.pos - thr; rel.y = 0;
        float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
        float hdgAl = cosf((plane.heading() - head(F.arrRev)) * DEG);
        bool straight = along < -2000.f && lat < 0.4f * -along && hdgAl > 0.7f;
        int n = A.rwyNumber(F.arrRev);
        AtcVoice::Tx tx; tx.prio = 60; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(va, true, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(va, straight ? "make_straight_in" : "enter_left_downwind")); runway(va, n, tx.ids);
        tx.ids.push_back(atc.atom(va, "report_final"));
        tx.text += straight ? fmt("Make straight-in runway %02d, report final.", n) : fmt("Enter left downwind runway %02d, report final.", n);
        // the sequence: an aircraft on final closer in than this one lands first
        RwyTraffic rt = rwyTraffic(F.arr);
        if (rt.fin && rt.finD < dist) {
          tx.ids.push_back(""); tx.ids.push_back(atc.line(va, "number_two")); tx.text += " " + atc.text(tx.ids.back());
          if (kAircraft[rt.fin->spec].emptyMass > 2.5f * plane.spec->emptyMass) {
            tx.ids.push_back(""); tx.ids.push_back(atc.line(va, "wake_caution")); tx.text += " " + atc.text(tx.ids.back());
          }
        }
        tx.apt = F.arr; F.phase = 4; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false;
      }
      break;
    }
    case 4: {   // established on final for that runway: cleared to land
      vec3 ld = F.arrRev ? -A.dir() : A.dir(), thr = A.threshold(F.arrRev);
      vec3 rel = plane.pos - thr; rel.y = 0;
      float along = dot(rel, ld), lat = fabsf(rel.x * ld.z - rel.z * ld.x);
      float hdgAl = cosf((plane.heading() - (A.heading + (F.arrRev ? 180.f : 0.f))) * DEG);
      if (F.airborne && !plane.onGround && along < 0.f && along > -6000.f && lat < 500.f && agl < 500.f && hdgAl > 0.82f) {
        int n = A.rwyNumber(F.arrRev);
        // the runway isn't free: an aircraft on it, or one ahead on final. Continue the approach until it is; still
        // occupied on short final: go around
        RwyTraffic rt = rwyTraffic(F.arr);
        vec3 toA = A.pos() - plane.pos; toA.y = 0;
        if (rt.onRunway || (rt.fin && rt.finD < length(toA) - 300.f)) {
          AtcVoice::Tx tx; tx.subtitle = true; tx.group = "tower";
          if (rt.onRunway && along > -700.f && agl < 90.f) {
            tx.prio = 99; tx.tag = callsign(va, true, tx.ids, tx.text);
            tx.ids.push_back(atc.line(va, "go_around_aircraft")); tx.text += atc.text(tx.ids.back());
            tx.apt = F.arr; F.phase = 5; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false; F.goAround = true;
          } else if (!F.trafficSaid) {
            tx.prio = 80; tx.tag = callsign(va, true, tx.ids, tx.text);
            tx.ids.push_back(atc.line(va, rt.onRunway ? "runway_occupied" : "clearance_follows")); tx.text += atc.text(tx.ids.back());
            tx.apt = F.arr; tx.key = atcKey(); atc.say(tx); F.trafficSaid = true;
          }
          break;
        }
        AtcVoice::Tx tx; tx.prio = 90; tx.subtitle = true; tx.group = "tower";
        tx.tag = callsign(va, true, tx.ids, tx.text);
        wind(va, tx.ids, tx.text);
        tx.ids.push_back(atc.atom(va, "runway")); runway(va, n, tx.ids); tx.ids.push_back(atc.atom(va, "cleared_land"));
        tx.text += fmt("Runway %02d, cleared to land.", n);
        tx.apt = F.arr; F.phase = 5; tx.key = atcKey(); atc.say(tx); F.waitT = 0; F.trafficSaid = false;
      } else if (F.airborne && plane.onGround && length(plane.pos - A.pos()) < 3000.f) F.phase = 5;   // landed without the clearance
      break;
    }
    case 5:   // cleared: the landing roll, or a go-around (climbing away again: a fresh approach call)
      if (plane.onGround && length(plane.pos - A.pos()) < 3000.f) {
        if (gs < 18.f) {
          AtcVoice::Tx tx; tx.prio = 55; tx.subtitle = true; tx.group = "tower"; tx.ids.push_back(atc.line(va, "exit_when_able")); tx.text = atc.text(tx.ids[0]);
          tx.apt = F.arr; F.phase = 6; tx.key = atcKey(); atc.say(tx);
        }
      } else if (!plane.onGround && agl > 180.f && plane.vel.y > 2.f && F.waitT > 20.f) { F.phase = F.dep == F.arr ? 4 : 3; F.waitT = 0; F.lastValid = false; F.goAround = false; }   // (went around: the clearance no longer stands)
      if (F.goAround && !plane.onGround && agl > 150.f && plane.vel.y > 1.f) F.goAround = false;   // (complied: climbing away)
      break;
    default: break;
  }
}

// The comms channel, every frame: what the game showed this frame is resolved to voice lines (AtcVoice::resolve)
// and queued; the warnings speak on their rising edge (and again if they persist); the towers run their exchange.
// Nothing stale is ever said: the queue is dropped on pause, at the end of the flight and when the aircraft
// crashes (then only the crash itself is announced).
void Game::updateComms(float dt) {
  bool live = (screen == SCR_FLIGHT || screen == SCR_LOADING) && !paused;
  if (!live) { atc.cancel(); commsPending.clear(); commsCrashSeen = false; return; }
  if (!atc.ok()) { commsPending.clear(); return; }
  if (crashed && !commsCrashSeen) { atc.cancel(); commsCrashSeen = true; }
  if (!crashed) commsCrashSeen = false;
  if (screen == SCR_FLIGHT && !crashed && flightClock > 2.f) {   // the HUD's warnings (game_ui.cpp), spoken as they come on
    const AircraftSpec& spc = *plane.spec;
    const Airport& d = g_world.airports[contract.to];
    bool w[4] = {plane.stallWarn > 0.8f && !plane.onGround && plane.ias > 10.f,
                 !plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f,
                 spc.retract && plane.gear < 0.99f && !plane.onGround && plane.agl() < 200 && length(plane.pos - d.pos()) < 4000.f && plane.ias < spc.vref * 1.5f,
                 !plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && !plane.glideOnly()};   // (a failed engine has no restart to advise)
    const char* say[4] = {"STALL", "PULL UP", "GEAR!", nullptr};
    for (int i = 0; i < 4; i++) {
      if (w[i] && (!warnWas[i] || realTime - warnLastT[i] > 8.f)) {
        std::string m = say[i] ? say[i] : "ENGINE OFF - press " + keyName(set.keyBind[ACT_ENGINE]) + " to restart";
        AtcVoice::Tx tx;
        if (atc.resolve(m, contract.id, in.pad, tx)) { tx.prio = std::max(tx.prio, 95); atc.say(tx); }
        warnLastT[i] = realTime;
      }
      warnWas[i] = w[i];
    }
    // the failure annunciators (hudAnnunciators), spoken when they come on or change severity: each wording once
    for (auto& a : hudAnnunciators()) {
      auto it = failVoiced.find(a.slot);
      if (it != failVoiced.end() && it->second == a.key) continue;
      failVoiced[a.slot] = a.key;
      AtcVoice::Tx tx;
      if (atc.resolve(a.text, contract.id, in.pad, tx)) atc.say(tx);
    }
  }
  for (auto& m : commsPending) { AtcVoice::Tx tx; if (atc.resolve(m.text, m.mission, in.pad, tx)) atc.say(tx); }
  commsPending.clear();
  if (screen == SCR_FLIGHT && !crashed && !researchFlight) updateAtc(dt);
  // while someone is talking the music and the engine sit lower (a headset's comms priority): quick down, slow up
  voiceDuck = approach(voiceDuck, atc.busy() && live ? 1.f : 0.f, atc.busy() ? 6.f : 1.5f, dt);
  AtcVoice::Tx st = atc.update(dt);
  if (!st.ids.empty() && st.subtitle) {
    toast("TOWER  " + st.text, vec3(0.55f, 1.f, 0.72f), false); atcF.spoken++;
    atcF.lastCall = st.text; atcF.lastApt = st.apt; atcF.lastT = flightClock; atcF.lastValid = true;
  }
  if (!st.ids.empty() && st.tag > 0) atcF.called[st.tag - 1] = true;
}

void Game::feedAudio() {
  AudioParams ap;
  ap.master = set.master; ap.engineVol = set.engineVol * (1.f - 0.35f * voiceDuck); ap.sfxVol = set.sfxVol; ap.voiceVol = set.atcVol;
  {   // the radio music, lowered under speech
    static float sentVol = -1.f;
    float rv = set.radioVol * (1.f - 0.65f * voiceDuck);
    if (fabsf(rv - sentVol) > 0.01f || (rv != sentVol && (rv == set.radioVol || voiceDuck >= 1.f))) { radio.setVolume(rv); sentVol = rv; }
  }
  static bool muffled = false;
  if (actKeyP(ACT_ANR) || (screen == SCR_FLIGHT && !paused && actPadP(ACT_ANR))) { muffled = !muffled; toast(muffled ? "Engine noise muffled (headset ANR on)" : "Headset ANR off"); }
  ap.muffled = muffled;
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    ap.inFlight = true;
    ap.engineType = s.engineType; ap.engines = s.engines; ap.cylinders = s.cylinders; ap.blades = s.blades;
    ap.research = s.special != 0; ap.nozzle = plane.nozzle; ap.mach = plane.mach;
    ap.rpm = s.engineType == ENG_JET ? plane.n1 : plane.rpm; ap.maxRpm = s.maxRpm; ap.n1 = plane.n1;
    ap.spool = plane.engineSpool; ap.throttle = plane.engineRunning ? plane.ctl.throttle : 0.f;
    ap.running = plane.engineRunning; ap.cranking = !plane.engineRunning && plane.starterTime > 0;
    for (int e = 0; e < 4; e++) ap.engineHealth[e] = plane.fail.engineHealth[e];
    ap.airspeed = plane.airspeed; ap.groundSpeed = length(vec3(plane.vel.x, 0, plane.vel.z)); ap.onGround = plane.onGround; ap.rough = plane.groundRough;
    ap.stallWarn = plane.onGround ? 0.f : plane.stallWarn; ap.stallIsShaker = s.engineType == ENG_JET || s.license == LIC_ATP;
    ap.gearMoving = plane.gear > 0.01f && plane.gear < 0.99f; ap.flapsMoving = fabsf(plane.flaps - plane.ctl.flaps) > 0.02f;
    ap.gearDown = plane.gear; ap.flaps = plane.flaps;
    ap.interior = camMode == 1;
    ap.rain = wx.precip == 1 ? (wx.storm ? 1.f : 0.6f) : 0.f; ap.turbulence = wx.turbulence;
    ap.paused = paused;
  }
  g_audio.setParams(ap);
}

// ------------------------------------------------------------------ main update / render
void Game::update(float dt) {
  if (pendingCareer && (screen == SCR_HUB || screen == SCR_DEBRIEF || screen == SCR_MENU)) {   // a settlement or purchase that couldn't be saved: tried again now and then
    retryT += dt;
    if (retryT > 3.f) { retryT = 0; retryCommit(); }
  }
  // the render resolution: native, a fixed scale, or adjusted to hold the frame-rate target (Settings)
  fpsAvg = lerpf(fpsAvg, dt, 1.f - expf(-dt * 3.f));   // (a third of a second, whatever the frame rate)
  maxFrameWin = std::max(maxFrameWin, dt); maxFrameT += dt;
  if (maxFrameT >= 1.f) { maxFrameMs = maxFrameWin * 1000.f; maxFrameWin = 0; maxFrameT = 0; }
  if (!headless && g_ren.ok) {   // render resolution: native, a fixed scale, or adjusted to hold 60 fps on the GPU
    static const float fixedScale[5] = {1.f, 1.f, 0.85f, 0.75f, 0.67f};
    float want = fixedScale[std::clamp(set.resMode, 0, 4)];
    if (set.resMode == 1) {
      autoScaleT += dt;
      float gms = g_ren.gpuMs;
      if (gms > 0 && autoScaleT > 0.4f) {
        // (the GPU budget of the frame-rate target, with 8% to spare; up again only with a quarter of it free)
        float budget = 1000.f / effectiveHz() * 0.92f;
        if (gms > budget) { autoScale = std::max(0.5f, autoScale * std::sqrt(budget * 0.97f / gms)); autoScaleT = 0; }
        else if (gms < budget * 0.75f && autoScale < 1.f) { autoScale = std::min(1.f, autoScale + 0.05f); autoScaleT = 0; }   // (up again only with a quarter of the budget free)
      }
      want = autoScale;
    } else autoScale = 1.f;
    want = floorf(want * 20.f + 0.5f) / 20.f;   // 5% steps: the targets are rebuilt only when it changes
    if (fabsf(g_ren.renderScale - want) > 1e-3f) g_ren.setRenderScale(want);
  }
  // the flight sim replays the whole frame time in steps of at most 50 ms, so the simulation keeps pace with the
  // clock down to 4 fps; a longer hitch (window drag, alt-tab) is dropped rather than replayed. UI and animation
  // timers just use the clamped step.
  float simDt = std::min(dt, 0.25f);
  dt = std::min(dt, 0.05f);
  lastDt = dt;
  realTime += dt;
  updateBindCapture(dt);
  armInputs();
  // gamepad: both bumpers held together for a second hides the whole flight UI; again brings it back (not while the
  // XR-40's weapons are armed: the bumpers are its triggers then; the pause menu has the same switch)
  bool bumpersFree = !(screen == SCR_FLIGHT && plane.spec && plane.spec->special == 2 && wraith.armed);
  if (in.pad && bumpersFree && (in.buttons & PAD_LB) && (in.buttons & PAD_RB)) {
    bumperHold += dt;
    if (bumperHold >= 1.f && !bumperFired) { bumperFired = true; uiHidden = !uiHidden; g_audio.trigger(SFX_CLICK); }
  } else { bumperHold = 0; bumperFired = false; }
  for (auto& t : toasts) t.t += dt;
  while (!toasts.empty() && toasts.front().t > 5.f) toasts.erase(toasts.begin());
  hubMsgTime = std::max(0.f, hubMsgTime - dt);
  if (in.pressed[K_F11]) wantFullscreenToggle = true;
  if (in.pressed[0x72]) showPerf = !showPerf;   // F3
  gamepadMenus(dt);
  focusNavigate();
  if (screen == SCR_HUB && in.pad && !((in.buttons & PAD_LB) && (in.buttons & PAD_RB))) {   // the shoulders step through the hub's tabs
    const int nTabs = TAB_SETTINGS + 1;
    if (in.buttonsPressed & PAD_RB) { hubTab = (hubTab + 1) % nTabs; g_audio.trigger(SFX_CLICK); }
    if (in.buttonsPressed & PAD_LB) { hubTab = (hubTab + nTabs - 1) % nTabs; g_audio.trigger(SFX_CLICK); }
  }
  bool padCombo = in.pad && (in.buttons & PAD_LS) && (in.buttons & PAD_RS) && (in.buttonsPressed & (PAD_LS | PAD_RS));
  if (screen == SCR_MENU && ((in.down['U'] && in.down['I'] && (in.pressed['U'] || in.pressed['I'])) || padCombo)) {
    screen = SCR_RESEARCH; resOpened = realTime; g_audio.trigger(SFX_BEEP);
    resWarm = true; resWarmFrames = 0; resBakeSeen = g_ren.bakeCount;   // the terminal's boot screen shows at once; the craft and its airport warm behind it
  }
  if (screen == SCR_RESEARCH && resWarm) {   // warming: the scenery streams flat out, the previewed craft's shells bake; done when a frame bakes nothing and nothing is pending
    g_ren.entBudgetMs = 14.f;
    bool baked = g_ren.bakeCount != resBakeSeen; resBakeSeen = g_ren.bakeCount;
    if (resWarmFrames > 2 * resCraftCount() + 3 && !baked && g_ren.entPending == 0 && !g_ren.tshPending()) { resWarm = false; resWarmCraft = -1; resWarmCk = false; g_ren.entBudgetMs = 2.5f; }
  }
  if (screen == SCR_FLIGHT && (actKeyP(ACT_RADIO) || (!paused && actPadP(ACT_RADIO)))) showRadio = !showRadio;
  radio.poll();
  if (screen == SCR_FLIGHT) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START)) {
      if (showMap) showMap = false; else if (showRadio) showRadio = false;
      else if (paused && settingsFromPause && !(in.buttonsPressed & PAD_START)) settingsFromPause = false;   // B / Esc: back to the pause menu
      else { paused = !paused; settingsFromPause = false; if (paused && !atcF.lastCall.empty()) atcF.lastBeforePause = true; }
    }
    if (!paused && actPressed(ACT_MAP)) { if (plane.fail.avionicsDark() && !showMap) toast("GPS dark - battery flat", vec3(1, 0.45f, 0.35f)); else { showMap = !showMap; g_audio.trigger(SFX_CLICK); } }
    if (showMap && !paused) {   // GPS open: Tab / D-pad pick the autoland airport, Enter / A engages the autopilot to it
      if (in.pressed[K_TAB] || (in.buttonsPressed & PAD_RIGHT)) cycleApDest(in.down[K_SHIFT] ? -1 : 1);
      if (in.buttonsPressed & PAD_LEFT) cycleApDest(-1);
      if ((in.pressed[K_ENTER] || (in.buttonsPressed & PAD_A)) && apDest >= 0 && !plane.onGround) engageAutopilot();
    } else if (actPressed(ACT_MINIMAP)) { showMinimap = !showMinimap; toast(showMinimap ? "Minimap shown" : "Minimap hidden"); }
    if (actPressed(ACT_HUD)) { hudOn = !hudOn; set.hudCam[std::clamp(camMode, 0, 3)] = hudOn; }
    if (!paused) {
      int nSim = std::max(1, (int)ceilf(simDt / 0.05f - 1e-4f));
      Input frameIn; bool saved = false;   // key presses act once: the catch-up steps see the held state without the press edges
      for (int k = 0; k < nSim && screen == SCR_FLIGHT && !paused; k++) {
        float h = simDt / nSim;
        updateFlight(h);
        if (screen == SCR_FLIGHT) updateParticles(h * timeAccel);
        if (k == 0 && nSim > 1) { frameIn = in; saved = true; in.endFrame(); }
      }
      if (saved) in = frameIn;
      if (screen == SCR_FLIGHT) updateCamera(dt);
      if (plane.spec) {
        float rps = plane.spec->engineType == ENG_TURBOPROP ? plane.rpm / 60.f : plane.rpm / 60.f;
        propAngle = fmodf(propAngle + rps * 2 * PI * dt * (plane.rpm < 400 ? 1.f : 0.0f) + (plane.rpm >= 400 ? dt * 3.f : 0.f), 2 * PI * 100);
      }
    }
  } else if (screen == SCR_LOADING) {
    updateLoading(dt);
  } else if (screen == SCR_DEBRIEF) {
    if (crashed) { crashTimer += dt; updateWreck(dt); updateParticles(dt); }   // the wreck keeps settling/sinking behind the results
  } else {
    wx = Weather(); wx.cloudCover = 0.35f; wx.cloudBase = 1500; wx.visibility = 45000; wx.windSpeed = 4;
    timeOfDay = screen == SCR_MENU ? menuShot(realTime).tod : 15.8f;
    if (screen == SCR_MENU) wx.cloudCover = menuShot(realTime).cloud;
    if (screen == SCR_RESEARCH) {   // the research preview shows the sortie's light and sky
      timeOfDay = resTime; wx.cloudCover = resWx == 0 ? 0.15f : resWx == 1 ? 0.65f : 0.92f; wx.cloudBase = resWx == 2 ? 900.f : 1400.f;
    }
    cloudOff = cloudOff + vec2(dt * 8.f, dt * 3.f);
  }
  updateComms(dt);
  feedAudio();
}

void Game::render() {
  // every menu draws its scene live (the main menu's tour, the hub's airport, the research terminal's preview): the
  // scenery a flight opens with streams in while the player chooses. vid: a full-screen picture instead of the scene
  // (the research terminal's boot screen)
  unsigned vid = 0;
  FrameParams fp;
  // the research terminal warming up: its first frame is the boot screen alone (nothing of the scene, so it is on the
  // screen the instant the combo is held); the frames after it draw the preview, which streams the airport and bakes
  // the craft's shells - during a bake the renderer calls back and the boot screen is drawn and shown from inside it
  if (screen == SCR_RESEARCH && resWarm) {
    if (resWarmFrames == 0) vid = 1;
    resWarmFrames++;
    // every research craft, outside and from the cockpit, one a frame: each one's shells bake now behind the boot
    // screen, so picking one in the terminal never stalls on a bake (the XR-40's, with the most moving parts, froze
    // the game for seconds when it was first selected); then the selected craft again
    const int item = resWarmFrames - 2, nItems = 2 * resCraftCount();
    resWarmCraft = item >= 0 && item < nItems ? resCraftAt(item / 2) : -1;
    resWarmCk = item >= 0 && item < nItems && (item & 1);
    g_ren.bakeYield = [this] {
      GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
      // (the sequence runs on the real clock: a frame from inside a bake moves it on by the time that has passed)
      static auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      realTime += std::min(0.1f, std::chrono::duration<float>(now - last).count()); last = now;
      g_ren.clearScreen();
      FrameParams f0; g_ren.uiBegin(); drawResearch(f0); g_ren.uiEnd();
      if (platformPresent) platformPresent();
      glUseProgram((GLuint)prog);
    };
  } else if (g_ren.bakeYield && screen != SCR_RESEARCH) g_ren.bakeYield = nullptr;
  if (!vid) {
    fp = buildFrame();
    std::vector<SpriteVert> a, b;
    buildSprites(fp, a, b);
    g_ren.renderScene(fp, a, b);
  } else g_ren.clearScreen();
  if (screen == SCR_RESEARCH && !resWarm) g_ren.bakeYield = nullptr;
  if (sceneOnly) return;
  g_ren.uiBegin();
  if (vid > 1) {   // cover the window, cropping the 16:9 picture as needed
    float W = (float)g_ren.W, H = (float)g_ren.H, ar = 16.f / 9.f, sa = W / H;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (sa > ar) { float k = ar / sa; v0 = 0.5f - 0.5f * k; v1 = 0.5f + 0.5f * k; } else { float k = sa / ar; u0 = 0.5f - 0.5f * k; u1 = 0.5f + 0.5f * k; }
    g_ren.image(vid, 0, 0, W, H, u0, v0, u1, v1, 1.f);
  }
  uiDt = clampf(realTime - uiLastT, 0.f, 0.1f); uiLastT = realTime;
  switch (screen) {
    case SCR_MENU: drawMenu(); break;
    case SCR_HUB: drawHub(); break;
    case SCR_FLIGHT: if (!uiHidden || paused || showMap) { drawHud(fp); drawMapOverlay(); } if (paused) drawPause(); break;
    case SCR_DEBRIEF: drawDebrief(); break;
    case SCR_RESEARCH: drawResearch(fp); break;
    case SCR_LOADING: drawLoading(); break;
  }
  if (!(uiHidden && screen == SCR_FLIGHT && !paused)) drawToasts();
  drawPadCursor();
  if (!headless) {   // always-on frame-rate counter, top right
    float s = S(), fps = 1.f / std::max(fpsAvg, 1e-4f);
    std::string t = fmt("%.0f FPS", fps);
    float ts = 13 * s, tw = g_ren.textWidth(t, ts);
    vec3 c = fps >= 57.f ? vec3(0.45f, 1.f, 0.6f) : fps >= 40.f ? vec3(1.f, 0.82f, 0.3f) : vec3(1.f, 0.4f, 0.35f);
    g_ren.rect(g_ren.W - tw - 18 * s, 4 * s, tw + 14 * s, ts + 8 * s, vec3(0, 0, 0), 0.45f, 3 * s);
    g_ren.text(g_ren.W - 11 * s, 7 * s, ts, t, c, 1, 2, false);
  }
  if (showPerf) {
    float s = S();
    std::string t = fmt("%.0f fps  %.1f ms   GPU %s   res %.0f%% (%dx%d)", 1.f / std::max(fpsAvg, 1e-4f), fpsAvg * 1000.f,
                        g_ren.gpuMs > 0 ? fmt("%.1f ms", g_ren.gpuMs).c_str() : "n/a", g_ren.renderScale * 100.f,
                        (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale));
    t += fmt("   worst %.1f ms   target %d fps   scenery %d drawn, %d chunks, %.1f ms CPU", maxFrameMs, effectiveHz(), g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs);
    t += fmt("   particles %d  debris %d  wreck %d  traffic %d  comms %d", (int)particles.size(), (int)debris.size(), (int)wreck.size(), (int)traffic.craft.size(), (int)atc.history.size());
    const float* pm = g_ren.passMs;
    std::string t2 = fmt("GPU ms:  world %.1f   displays %.1f   feeds %.1f   objects %.1f   shadow proxy %.1f   lighting+clouds %.1f   TAA %.1f   sprites %.1f   bloom %.1f   shafts %.1f   composite %.1f",
                         pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]);
    float tw = std::max(g_ren.textWidth(t, 14 * s), g_ren.textWidth(t2, 14 * s)) + 20 * s;
    g_ren.rect(6 * s, 6 * s, tw, 46 * s, vec3(0, 0, 0), 0.6f);
    g_ren.text(14 * s, 10 * s, 14 * s, t, fpsAvg > 1.05f / effectiveHz() ? vec3(1, 0.5f, 0.3f) : vec3(0.5f, 1, 0.6f), 1, 0, false);
    g_ren.text(14 * s, 30 * s, 14 * s, t2, vec3(0.75f, 0.85f, 1.f), 1, 0, false);
  }
  g_ren.uiEnd();
}

// ------------------------------------------------------------------ development scenes for the render harness
void Game::debugScene(const std::string& name) {
  career.newGame(); career.license = LIC_ATP;
  if (name == "menu") { screen = SCR_MENU; realTime = 20; return; }
  if (name.compare(0, 5, "menuT") == 0) { screen = SCR_MENU; realTime = (float)atof(name.c_str() + 5); return; }   // the menu tour at t seconds
  if (name == "hub") { screen = SCR_HUB; realTime = 20; return; }
  if (name.compare(0, 4, "jcam") == 0) {  // XR-30 close-up from an orbit angle: jcam<yaw deg>_<pitch deg>
    float yawD = 0, pitD = 10, thrP = -1, nozP = 0, zoom = 0.55f, tod = -1; sscanf(name.c_str() + 4, "%f_%f_%f_%f_%f_%f", &yawD, &pitD, &thrP, &nozP, &zoom, &tod);
    if (tod >= 0) resTime = tod;
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudCover = 0.3f;
    if (getenv("JCLOUD")) { wx.cloudCover = (float)atof(getenv("JCLOUD")); wx.cloudBase = plane.pos.y + 250.f; }   // cloud checks
    if (thrP >= 0) {   // optional throttle (and nozzle) percent: let the exhaust plumes and sparks develop
      plane.ctl.throttle = thrP / 100.f; plane.engineSpool = thrP / 100.f; plane.ctl.flaps = plane.flaps = plane.nozzle = nozP / 100.f;
      for (int i = 0; i < 30; i++) { realTime += 1 / 60.f; update(1 / 60.f); plane.engineSpool = thrP / 100.f; }
    }
    if (getenv("JMACH")) {   // flying at a forced Mach number (vapour cone checks)
      float M = (float)atof(getenv("JMACH"));
      for (int i = 0; i < 10; i++) { plane.vel = plane.forward() * (M * 330.f); realTime += 1 / 60.f; update(1 / 60.f); }
      plane.vel = plane.forward() * (M * 330.f); plane.mach = M;
    }
    if (getenv("WRSPARKS")) { int n = 0; for (auto& q : particles) if (q.kind == SPR_SPARK && n < 16) { vec3 b = plane.q.conj().rotate(q.p - plane.pos), v = plane.q.conj().rotate(q.v - plane.vel); printf("spark body (%.1f, %.1f, %.1f) rel v (%.0f, %.0f, %.0f) age %.2f\n", b.x, b.y, b.z, v.x, v.y, v.z, q.maxLife - q.life); n++; } }
    camMode = 2; camYaw = yawD * DEG; camPitch = pitD * DEG; camZoom = zoom; hudOn = false;
    for (int i = 0; i < 5; i++) updateCamera(0.1f);
    toasts.clear(); return;
  }
  if (name == "vapour") {  // XR-30 pulling g near the cloud base: wingtip vapour must trail behind the tips
    resAirborne = true; realTime = 20; launchResearch(); wx.cloudBase = 300; wx.cloudCover = 0.2f;
    plane.vel = plane.forward() * 280.f; plane.ctl.throttle = 0.9f; botControl = true; plane.ctl.pitch = 0.6f; plane.ctl.gearDown = false; plane.gear = 0;
    for (int i = 0; i < 40; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    camMode = 2; camYaw = 1.2f; camPitch = 0.25f; camZoom = 1.2f; for (int i = 0; i < 5; i++) updateCamera(0.1f); toasts.clear();
    { int ni = 0; float dmin = 1e9f; for (auto& tt : tipTrail) for (auto& q : tt) { ni++; dmin = std::min(dmin, length(q.p - plane.pos)); } printf("vapour: %d ribbon points, nearest %.1f m from the CG, g=%.1f\n", ni, dmin, plane.gLoad); }
    return;
  }
  if (name == "pad") {  // gamepad menu navigation self-test
    auto frame = [&](unsigned btn) { in.pad = true; in.buttonsPressed = btn & ~in.buttons; in.buttons = btn; realTime += 1 / 30.f; update(1 / 30.f); render(); in.endFrame(); };
    screen = SCR_MENU; realTime = 20;
    frame(PAD_LS | PAD_RS); frame(0);
    printf("pad: combo -> research menu: %s\n", screen == SCR_RESEARCH ? "ok" : "FAIL");
    for (int i = 0; i < 50; i++) frame(0);           // let the access sequence finish
    int site = resAirport; frame(PAD_RB); frame(0);
    printf("pad: RB steps launch site: %s\n", resAirport == (site + 1) % (int)g_world.airports.size() ? "ok" : "FAIL");
    frame(PAD_B); frame(0);
    printf("pad: B backs out to main menu: %s\n", screen == SCR_MENU ? "ok" : "FAIL");
    screen = SCR_HUB; hubTab = 0; frame(0);
    float s = S(); float tx = 20 * s + 1 * (150 * s + 10 * s) + 75 * s, ty = 76 * s + 19 * s;
    in.lx = 1.f; float x0 = in.mx; frame(0); in.lx = 0;
    printf("pad: stick moves cursor: %s\n", in.mx > x0 ? "ok" : "FAIL");
    in.mx = tx; in.my = ty; frame(PAD_A); frame(0);
    printf("pad: A clicks the Hangar tab: %s\n", hubTab == 1 ? "ok" : "FAIL");
    in.lx = 0.5f; frame(0); in.lx = 0; in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.4f;
    // crash, then A skips the crash sequence straight to the results (and the press doesn't click through them)
    resAirborne = true; launchResearch();
    plane.vel = plane.forward() * 620.f; botControl = true; plane.ctl.pitch = 1;
    for (int i = 0; i < 600 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    bool wasCrash = crashed && screen == SCR_FLIGHT;
    in.mx = g_ren.W * 0.5f; in.my = g_ren.H * 0.75f; frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the research crash back to the research menu: %s\n", wasCrash && screen == SCR_RESEARCH ? "ok" : "FAIL");
    // career flight: overstress it, skip with A, land on the results screen without clicking through
    startFlight(g_story[0], 1, Career::SRC_OWNED);
    plane.pos.y += 1500.f; plane.vel = plane.forward() * 95.f; plane.onGround = false; botControl = true; plane.ctl.pitch = -1;
    for (int i = 0; i < 900 && !crashed; i++) frame(0);
    for (int i = 0; i < 60; i++) frame(0);
    wasCrash = crashed && screen == SCR_FLIGHT;
    frame(PAD_A); frame(0); frame(0);
    printf("pad: A skips the career crash to the results: %s\n", wasCrash && screen == SCR_DEBRIEF ? "ok" : "FAIL");
    return;
  }
  if (name == "research" || name == "research40" || name == "research10" || name == "research20") {   // the terminal, settled (selection decrypted)
    screen = SCR_RESEARCH; realTime = 30; resOpened = 20; resAuthed = true;
    resCraft = name == "research40" ? kWraith : name == "research10" ? kNightjar : name == "research20" ? kMantis : kResearchJet; resLastCraft = resCraft; resSelT = 20; resAirport = std::max(0, g_world.findAirport("CAP"));
    return;
  }
  if (name.rfind("researchscan", 0) == 0) {   // the biometric sequence at a moment: researchscan<tenths of a second>
    screen = SCR_RESEARCH; realTime = 30; resAuthed = false; resOpened = realTime - atoi(name.c_str() + 12) / 10.f;
    return;
  }
  if (name.compare(0, 3, "wr_") == 0) {   // XR-40: wr_<mode>_<cam yaw>_<cam pitch>_<cam dist>_<seconds>
    // modes: 0 cruise, 1 hover, 2 parked, 3 cloak spreading, 4 cloaked, 5 turrets out + bay open, 6 lasers firing,
    // 7 plasma bomb (camera on the impact), 8 cockpit
    int mode = 0; float yawD = 210, pitD = 12, dist = 30, secs = 1.5f;
    sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f", &mode, &yawD, &pitD, &dist, &secs);
    resCraft = kWraith; realTime = 20; resAirborne = mode != 2; resTime = getenv("TOD") ? (float)atof(getenv("TOD")) : 12.f; launchResearch();
    botControl = true;
    if (mode == 1) { plane.ctl.flaps = 1; flapNotch = 1; plane.flaps = plane.nozzle = 1; plane.vel = vec3(); plane.ctl.throttle = 0.66f; plane.engineRunning = true; plane.engineSpool = 0.66f; }
    else if (mode != 2) { plane.apEngage(Plane::AP_HOLD, -1, wx); }
    if (mode == 3 || mode == 4) wraith.cloakOn = true;
    if (getenv("WRCLOUD")) { wx.cloudCover = (float)atof(getenv("WRCLOUD")); wx.cloudBase = plane.pos.y - 60.f; }   // craft inside the cloud deck
    if (mode == 5 || mode == 6) { wraith.armed = true; }
    if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.apSpeed = 900.f; }
    if (getenv("WRSPD")) { plane.vel = plane.forward() * (float)atof(getenv("WRSPD")); plane.apSpeed = (float)atof(getenv("WRSPD")); }
    if (mode == 5) wraith.bayHold = 100.f;
    // 9: cockpit looking down while a bomb falls (secs after release); 10: the same, secs after it goes off
    bool bombCk = mode == 9 || mode == 10 || (mode == 8 && getenv("WRBOMB"));
    if (mode == 7 || bombCk) { wraith.bombQueue = 1; wraith.armed = bombCk; }
    if (getenv("WRUFO")) { startUfo(); ufo.side = (float)atof(getenv("WRUFO")); }   // UFO alongside (side -1 / 1)
    float firstDrop = 0; vec3 blastAt;
    int frames = (int)((mode == 3 ? 0.55f : mode == 7 || mode == 10 ? 60.f : mode == 9 ? 30.f : secs) * 60.f);
    float dropT = -1, boomAt = -1;
    for (int i = 0; i < frames; i++) {
      if (mode == 9 || mode == 10) {
        if (dropT < 0 && !wraith.bombs.empty()) dropT = realTime;
        if (boomAt < 0 && !wraith.blasts.empty()) boomAt = realTime;
        if (mode == 9 && dropT >= 0 && realTime - dropT > secs) break;
        if (mode == 10 && boomAt >= 0 && realTime - boomAt > secs) break;
      }
      realTime += 1 / 60.f; update(1 / 60.f);
      if (mode == 6) wraith.wantFire = true;   // trigger held
      if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.engineSpool = std::max(plane.engineSpool, plane.ctl.throttle * 0.98f); }
      if (mode == 7 && !wraith.blasts.empty() && firstDrop == 0) { firstDrop = realTime; blastAt = wraith.blasts[0].p; }
      if (mode == 7 && firstDrop > 0 && realTime - firstDrop > secs) break;
    }
    if (mode == 6) {
      for (auto& b : wraith.bolts) b.life = b.age + 1.f;   // keep the bolts in flight bright for the screenshot
      // the streak of the youngest visible bolt must start at its lens, however fast the craft flies
      // (in the craft's frame the tail sits on the lens's firing line, 2000 m/s x age - streak out from it)
      float worstOff = 0, worstAlong = 0; int n = 0;
      for (auto& b : wraith.bolts) if (b.len > 0.05f && !b.hit && b.age < 0.5f) {
        float off = 1e9f, alongErr = 0;
        for (int s = 0; s < 2; s++) {
          vec3 lens = plane.pos + plane.q.rotate(vec3(s ? 0.95f : -0.95f, -0.68f, -6.44f));
          vec3 r = b.h - b.d * b.len - lens; float al = dot(r, b.d), o = length(r - b.d * al);
          if (o < off) { off = o; alongErr = al - (2000.f * b.age - b.len); }
        }
        worstOff = std::max(worstOff, off); worstAlong = std::max(worstAlong, fabsf(alongErr)); n++;
      }
      printf("wr: speed %.0f m/s, %d young bolts: streak tails off the firing line by <= %.2f m, along-error <= %.2f m\n", length(plane.vel), n, worstOff, worstAlong);
    }
    if (getenv("WRSPARKS")) { int n = 0; for (auto& q : particles) if (q.kind == SPR_SPARK && n < 12) { vec3 b = plane.q.conj().rotate(q.p - plane.pos); printf("spark body (%.1f, %.1f, %.1f) life %.2f\n", b.x, b.y, b.z, q.life); n++; } }
    if (ufo.on) { vec3 r = plane.q.conj().rotate(ufo.pos - plane.pos); printf("wr: ufo t %.1f at craft-frame (%.0f, %.0f, %.0f) m\n", ufo.t, r.x, r.y, r.z); }
    toasts.clear(); hint.clear();
    for (auto& b : wraith.blasts) printf("wr: blast age %.2f R %.0f at %.0f %.0f %.0f particles %d\n", b.age, b.R, b.p.x, b.p.y, b.p.z, (int)particles.size());
    printf("wr: mode %d stealth %.2f lasers %.2f bay %.2f bombs %d blasts %d tilt %.2f %.2f %.2f %.2f thr %.2f\n", mode, wraith.stealth, wraith.lasers, wraith.bay, (int)wraith.bombs.size(), (int)wraith.blasts.size(),
           plane.podTilt[0], plane.podTilt[1], plane.podTilt[2], plane.podTilt[3], plane.podThr[0]);
    if (mode == 8 || mode == 9 || mode == 10) { camMode = 1; lookYaw = yawD * DEG; lookPitch = pitD * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f; return; }
    hudOn = false; dbgCam = true;
    float h = plane.heading() * DEG, yw = h + yawD * DEG, pt = pitD * DEG;
    vec3 focus = mode == 7 && firstDrop > 0 ? blastAt + vec3(0, 30.f, 0) : plane.pos;
    dbgCamLook = focus; dbgCamPos = focus + vec3(sinf(yw) * cosf(pt), sinf(pt), -cosf(yw) * cosf(pt)) * dist;
    if (!(mode == 7 && firstDrop > 0)) { dbgFollow = true; dbgFollowOff = dbgCamPos - plane.pos; }
    return;
  }
  if (name == "rjet" || name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") { realTime = 20; resAirborne = true; launchResearch(); if (name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") camMode = 1; for (int i = 0; i < 90; i++) { realTime += 1 / 30.f; update(1 / 30.f); } toasts.clear(); if (name == "rjetl") lookYaw = 1.75f; if (name == "rjetr") { lookYaw = -1.2f; lookPitch = -0.6f; } if (name == "rjetd") lookPitch = -0.75f; return; }
  if (name == "radio") { loadStations(); screen = SCR_HUB; showRadio = true; realTime = 20; radioScroll = 6; return; }
  if (name.size() == 4 && name.compare(0, 3, "hub") == 0) { screen = SCR_HUB; hubTab = name[3] - '0'; realTime = 20; return; }
  Contract c = g_story[0];
  int spec = 0;
  if (name.size() == 3 && name[0] == 'm') {
    // model inspection: m<aircraft><view>  views: e = exterior 3/4 front, r = controls deflected from behind,
    // c = cockpit, g = on the ground, s = side, d = cockpit with controls deflected
    spec = name[1] - '0'; char v = name[2];
    c.wx = Weather(); c.wx.cloudCover = 0.25f; c.wx.timeOfDay = 10.5f; c.wx.windSpeed = 0; c.wx.turbulence = 0;
    startFlight(c, spec, Career::SRC_OWNED);
    realTime = 10; engineAutoStarted = true;
    const AircraftSpec& s = kAircraft[spec];
    if (v != 'g') { plane.reset(&s, vec3(-6000, 700, 16000), 0, s.maxFuel, 100, true, (v == 'r' || v == 'd' || v == 'b') ? s.vref * 1.3f : s.cruise); plane.gear = 1; plane.ctl.gearDown = true; }
    else { plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 900; plane.n1 = 60; }
    takeoffAnnounced = true;
    float size = std::max(s.fusLen, s.span);
    camMode = 2; camZoom = (v == 'g' ? 0.6f : 0.42f);
    camYaw = 3.14159f - 0.75f; camPitch = 0.18f;
    if (v == 'r' || v == 'd' || v == 'b') { botControl = true; plane.ctl.roll = 1; plane.ctl.pitch = 1; plane.ctl.yaw = 1; plane.ctl.flaps = v == 'b' ? 0.f : 1.f; plane.flaps = plane.ctl.flaps; camYaw = v == 'b' ? 0.f : -0.55f; camPitch = v == 'b' ? 0.22f : 0.35f; if (v == 'b') camZoom = 0.3f; }
    if (v == 's') { camYaw = 1.5708f; camPitch = 0.05f; }
    if (v == 'c' || v == 'd') { camMode = 1; lookYaw = 0; lookPitch = v == 'd' ? -0.45f : -0.13f; }
    if (v == 'y' || v == 'p' || v == 'a') {
      botControl = true; plane.ctl = Controls(); plane.ctl.throttle = 0.6f;
      if (v == 'y') plane.ctl.yaw = 1; if (v == 'p') plane.ctl.pitch = 1; if (v == 'a') plane.ctl.roll = 1;
      camMode = 3;
      vec3 off = v == 'y' ? vec3(0, size * 1.6f, size * 0.9f) : v == 'p' ? vec3(size * 1.5f, 0.3f, 0) : vec3(0, size * 0.25f, size * 1.8f);
      hint.clear(); toasts.clear(); hudOn = false;
      camPos = plane.pos + off;
      return;
    }
    (void)size;
    hint.clear(); toasts.clear(); hudOn = false;
    camPos = plane.pos;
    updateCamera(0.016f);
    if (v == 'd') { lookPitch = -0.35f; }
    return;
  }
  if (name == "storm") { c = g_story[g_story.size() - 1]; spec = 5; }
  if (name == "jet") { c = g_story[g_story.size() - 4]; spec = 6; }
  if (name == "night") { c = g_story[11]; c.wx.timeOfDay = 21.5f; spec = 1; }
  if (name == "snow") { c = g_story[20]; spec = 2; }
  startFlight(c, spec, Career::SRC_OWNED);
  realTime = 10;
  plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 1000; engineAutoStarted = true;
  {
    float px, pz, agl, hdg; char cm = 'c';
    float bagl = 300, blook = -70, bafter = 0.25f;
    if (sscanf(name.c_str(), "wrbomb_%f_%f_%f", &bagl, &blook, &bafter) >= 1) {   // XR-40 cockpit: drop a bomb and watch it go off through the glass floor
      resCraft = kWraith; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      plane.pos.y = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + bagl;
      plane.apEngage(Plane::AP_HOLD, -1, wx); plane.apAlt = plane.pos.y; botControl = true;
      wraith.armed = true;
      for (int i = 0; i < 90; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
      wraith.bombQueue = 1;
      float since = -1;
      for (int i = 0; i < 60 * 40; i++) {
        realTime += 1 / 60.f; update(1 / 60.f);
        if (since < 0 && !wraith.blasts.empty()) since = 0;
        if (since >= 0) { since += 1 / 60.f; if (since >= bafter) break; }
        if (since < 0 && bafter < 0 && !wraith.bombs.empty() && wraith.bombs[0].t > -bafter) break;   // negative: a falling bomb
      }
      printf("wrbomb: bombs %d blasts %d pip %d\n", (int)wraith.bombs.size(), (int)wraith.blasts.size(), 0);
      camMode = 1; lookYaw = 0; lookPitch = blook * DEG; camYaw = 0; camPitch = lookPitch + 0.12f; hudOn = true; toasts.clear(); hint.clear();
      return;
    }
    float fx, fz, fsec = 3, fpitch = 28, fyaw = 0;
    if (sscanf(name.c_str(), "lasertest_%f_%f_%f_%f_%f", &fx, &fz, &fsec, &fpitch, &fyaw) >= 2) {   // XR-40 firing at the ground ahead from a fixed hover
      resCraft = kWraith; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      float g = std::max(g_world.height(fx, fz), 0.f);
      vec3 pos(fx, g + 70.f, fz);
      quat q = quat::axisAngle(vec3(0, 1, 0), -fyaw * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG);
      wraith.armed = true; botControl = true;
      for (int i = 0; i < (int)((fsec + 1.f) * 60.f); i++) {
        plane.pos = pos; plane.q = q; plane.vel = vec3(); plane.w = vec3();
        if (i > 60) wraith.wantFire = true;
        realTime += 1 / 60.f; update(1 / 60.f);
        if ((i & 7) == 0) { plane.q = quat::axisAngle(vec3(0, 1, 0), (-fyaw + 6.f * sinf(i * 0.05f)) * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG); q = plane.q; }   // sweep the aim
      }
      printf("lasertest: %d destroyed, %d scorch pits, %d bolts in flight\n", wraith.wrecked, (int)wraith.scorch.size(), (int)wraith.bolts.size());
      vec3 f = q.rotate(vec3(0, 0, -1)); vec3 tgt = pos + f * (70.f / std::max(-f.y, 0.2f));
      hudOn = false; dbgCam = true; dbgFollow = false; toasts.clear(); hint.clear();
      float cd = getenv("LTDIST") ? (float)atof(getenv("LTDIST")) : 1.f;
      dbgCamLook = tgt; dbgCamPos = tgt + vec3(70.f, 45.f, 40.f) * cd;
      for (int i = 0; i < 5; i++) updateCamera(0.1f);
      return;
    }
    float yw, pt, dist, lh = 8, tod = -1;
    if (sscanf(name.c_str(), "look_%f_%f_%f_%f_%f_%f_%f", &px, &pz, &yw, &pt, &dist, &lh, &tod) >= 5) {   // free camera on a ground point
      if (tod >= 0) timeOfDay = tod;
      plane.reset(&kAircraft[1], vec3(px + 4000.f, std::max(g_world.height(px + 4000.f, pz), 0.f) + 900.f, pz), 90, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; hudOn = false; hint.clear(); toasts.clear(); dbgCam = true; dbgFollow = false;
      dbgCamLook = vec3(px, std::max(g_world.height(px, pz), 0.f) + lh, pz);
      dbgCamPos = dbgCamLook + vec3(sinf(yw * DEG) * cosf(pt * DEG), sinf(pt * DEG), -cosf(yw * DEG) * cosf(pt * DEG)) * dist;
      dbgCamPos.y = std::max(dbgCamPos.y, std::max(g_world.height(dbgCamPos.x, dbgCamPos.z), 0.f) + 2.f);
      for (int i = 0; i < 5; i++) updateCamera(0.1f);
      return;
    }
    if (sscanf(name.c_str(), "at_%f_%f_%f_%f_%c", &px, &pz, &agl, &hdg, &cm) >= 4) {
      plane.reset(&kAircraft[1], vec3(px, std::max(g_world.height(px, pz), 0.f) + agl, pz), hdg, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
      takeoffAnnounced = true; camQ = plane.q; hudOn = false; hint.clear(); toasts.clear();
      if (cm == 'k') camMode = 1;
      if (cm == 'f') { camMode = 3; botControl = true; camPos = plane.pos + vec3(0, 40, 0) - plane.forward() * 60.f; }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      return;
    }
  }
  if (name == "air" || name == "sunset" || name == "mountain" || name == "cockpit" || name == "jet" || name == "storm" || name == "snow" || name == "hud") {
    vec3 p(-4000, 600, 9000); float hdg = 40;
    if (name == "sunset") { timeOfDay = 18.2f; p = vec3(-26000, 300, 14000); hdg = 270; }
    if (name == "mountain") { p = vec3(-20000, 1900, -9000); hdg = 60; }
    if (name == "jet") { p = vec3(8000, 900, 6000); hdg = 80; }
    if (name == "storm") { p = vec3(10000, 700, 10000); hdg = 120; }
    if (name == "snow") { p = vec3(20000, 600, -24000); hdg = 80; }
    plane.reset(&kAircraft[spec], p, hdg, kAircraft[spec].maxFuel, 100, true, kAircraft[spec].cruise);
    takeoffAnnounced = true;
    camQ = plane.q;
    if (name == "cockpit") camMode = 1;
    if (name == "hud") {   // HUDDEMO=1: every warning, a tower call, failures and messages lit, to look at the layout
      if (getenv("HUDDEMO")) {
        hudDemo = true;
        atcF.lastCall = "Wren Alfa Bravo, runway 05, wind 040 at 8, cleared to land."; atcF.lastApt = contract.to; atcF.lastT = flightClock - 42.f;
        plane.fail.engineHealth[0] = 0.4f; plane.fail.alternator = true; plane.fail.battery = 0.6f; plane.fail.ice = 0.3f;
        toast("Checkpoint 2 of 6", vec3(0.5f, 1, 0.7f)); toast("Gear down", vec3(0.8f, 1, 0.8f)); toast("Time acceleration off", vec3(0.8f, 0.8f, 0.8f));
        hint = "Hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.";
        plane.apOn = true; plane.apMode = Plane::AP_HOLD; plane.apHeading = hdg + 25.f; plane.apAlt = 900.f; plane.apStatus = fmt("HDG %03.0f  ALT %.0f", hdg + 25.f, 900.f * M_TO_FT);
        showMinimap = true;
      }
    }
  }
  if (name == "rings" || name == "ringburst") {
    vec3 c, ax, ay; ringGeom(0, c, ax, ay);
    vec3 nd = normalize(cross(vec3(0, 1, 0), ax));
    float dist = name == "rings" ? 420.f : 160.f;
    float hdg = atan2f(nd.x, -nd.z) / DEG;
    plane.reset(&kAircraft[0], c - nd * dist - vec3(0, 12, 0), hdg, kAircraft[0].maxFuel, 100, true, kAircraft[0].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true;
    plane.ctl = Controls(); plane.ctl.throttle = 0.7f;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < (name == "rings" ? 60 : 42); i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    return;
  }
  if (name.compare(0, 8, "airbreak") == 0) {   // XR-30 overstressed at speed: breaks up in the air; airbreak<seconds after>
    bool cessna = name.size() > 8 && name[8] == 'c';   // airbreakc<s>: a light aircraft pushed over hard in a dive instead
    float after = name.size() > 8 + cessna ? atof(name.c_str() + 8 + cessna) : 3.f;
    resAirborne = true; realTime = 20; launchResearch(); hudOn = false;
    if (cessna) { plane.reset(&kAircraft[1], plane.pos, 90, kAircraft[1].maxFuel, 100, true, 90); researchFlight = false; }
    plane.pos.y = std::max(plane.pos.y, g_world.height(plane.pos.x, plane.pos.z) + 2500.f);
    plane.vel = plane.forward() * (cessna ? 95.f : 620.f); plane.ctl.throttle = 1; plane.engineSpool = 1; botControl = true; plane.ctl.pitch = cessna ? -1.f : 1.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.25f; camZoom = 1.2f;
    float tb = -1, tEnd = -1;
    for (int i = 0; i < 60 * 200; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("airbreak: broke up (%s), %d pieces, %.0f m AGL\n", plane.ev.crashReason.c_str(), (int)wreck.size(), plane.agl()); }
      if (crashed && tEnd < 0 && crashEndT < 1e8f) { tEnd = crashTimer; float sp = 0; for (auto& w : wreck) sp = std::max(sp, length(w.v));
        printf("airbreak: all pieces down %.1f s after the break-up (crater %.1f m) %s\n", crashTimer, craterR, crashTimer > 5.f ? "ok" : "FAIL"); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("airbreak: FAIL - no break-up\n");
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 3, "ckv") == 0) {   // cockpit view of aircraft N: ckv<N>_<look yaw deg>_<look pitch deg>_<hour>[_<roll input -1..1>[_<flaps 0..1>]]
    int idx = 0; float ly = 0, lpch = -8, hour = 11, roll = 0, fl = 0; sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f_%f", &idx, &ly, &lpch, &hour, &roll, &fl);
    if (idx == kResearchJet) { resAirborne = true; resTime = hour; launchResearch(); }
    else { timeOfDay = hour; plane.reset(&kAircraft[idx], vec3(-4000, 600, 9000), 40, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise); camQ = plane.q; takeoffAnnounced = true; }
    camMode = 1; hint.clear(); toasts.clear();
    for (int i = 0; i < 4; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    lookYaw = ly * DEG; lookPitch = lpch * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f;
    plane.ctl.roll = roll; botControl = roll != 0.f || fl != 0.f;   // (a held roll input: the yokes turn)
    if (fl != 0.f) { plane.ctl.flaps = fl; plane.flaps = fl; }
    toasts.clear(); hint.clear(); return;
  }
  if (name.compare(0, 3, "trf") == 0) {   // AI traffic: trf<seconds>_<view> at Solace Capital; view 0 = airport overview, k = chase craft k-1
    float secs = 60; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &secs, &view);
    set.traffic = getenv("NOTRF") == nullptr;
    int ai = g_world.findAirport("CAP"); const Airport& a = g_world.airports[ai];
    float h = a.heading * DEG, sn = sinf(h), cs = cosf(h), side = (ai & 1) ? 1.f : -1.f;
    auto W = [&](float u, float v) { return vec3(a.x + u * sn + v * cs, 0, a.z - u * cs + v * sn); };
    vec3 pp = W(a.length * 0.47f, side * (a.width * 0.5f + 130.f)); pp.y = g_world.height(pp.x, pp.z) + 1.5f;
    plane.reset(&kAircraft[1], pp, a.heading, kAircraft[1].maxFuel, 100, false, 0);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 15.f;
    for (float tt = 0; tt < secs; tt += 1 / 20.f) { realTime += 1 / 20.f; update(1 / 20.f); }
    const char* st[] = {"PARKED", "TAXI_OUT", "HOLD", "LINEUP", "TAKEOFF", "CLIMB", "CIRCUIT", "FINAL", "ROLLOUT", "TAXI_IN", "FLY"};
    const char* rl[] = {"airport", "cruiser", "formation", "stunt", "escort"};
    for (int i = 0; i < (int)traffic.craft.size(); i++) { const TrafficCraft& c = traffic.craft[i];
      printf("trf %2d %-9s %-17s %-8s agl %6.0f spd %5.1f dist %6.0f man %d\n", i, rl[c.role], kAircraft[c.spec].name, st[c.state], c.pos.y - g_world.height(c.pos.x, c.pos.z), c.speed, length(c.pos - plane.pos), c.man); }
    dbgCam = true; hudOn = false; toasts.clear();
    if (view == 0) { dbgCamLook = W(0, side * 60.f); dbgCamLook.y = a.elev; dbgCamPos = W(-a.length * 0.2f, -side * 700.f); dbgCamPos.y = a.elev + 250.f; }
    else if (view - 1 < (int)traffic.craft.size()) { const TrafficCraft& c = traffic.craft[view - 1]; float sz = kAircraft[c.spec].fusLen;
      dbgCamLook = c.pos; dbgCamPos = c.pos + c.q.rotate(vec3(sz * 0.9f, sz * 0.35f, sz * 1.6f)); }
    return;
  }
  if (name == "ufosummon") {   // J + K held for a second while flying summons the UFO
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['J'] = true; in.down['K'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && ufo.on) early = true; }
    in.down['J'] = in.down['K'] = false;
    printf("ufosummon: %s\n", ufo.on && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escsummon") {   // O + P held for a second while flying summons the display pair
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; botControl = true; plane.ctl.throttle = 0.75f;
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    in.down['O'] = true; in.down['P'] = true;
    bool early = false;
    for (int i = 0; i < 36; i++) { realTime += 1 / 30.f; update(1 / 30.f); if (i == 20 && traffic.count(TrafficCraft::ESCORT)) early = true; }
    in.down['O'] = in.down['P'] = false;
    printf("escsummon: %s\n", traffic.count(TrafficCraft::ESCORT) == 2 && !early ? "ok" : "FAIL");
    return;
  }
  if (name == "escdismiss") {   // O + P again mid-helix: the pair blends out of the act, waves and leaves without jumps
    plane.reset(&kAircraft[1], vec3(-4000, 900, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worstJump = 0; std::vector<vec3> last;
    for (float x = 0; x < 50.f; x += 1 / 60.f) {
      if (fabsf(x - 27.f) < 1e-3f) traffic.dismissEscort();
      realTime += 1 / 60.f; update(1 / 60.f);
      std::vector<vec3> now; std::vector<float> spd;
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT) { now.push_back(c.pos); spd.push_back(c.speed); }
      if (now.size() == last.size()) for (size_t i = 0; i < now.size(); i++) worstJump = std::max(worstJump, length(now[i] - last[i]) * 60.f / std::max(spd[i], 30.f));
      last = now;
    }
    // a frame's step must match the jet's own speed (no teleports when the act is cut short)
    printf("escdismiss: worst step / speed %.2f, %d escorts left %s\n", worstJump, traffic.count(TrafficCraft::ESCORT),
           worstJump < 1.3f && traffic.count(TrafficCraft::ESCORT) == 0 ? "ok" : "FAIL");
    return;
  }
  if (name.compare(0, 3, "esc") == 0) {   // display pair at t seconds: esc<t>_<view>_<aircraft> (0 chase, 1 cockpit, 2 side camera)
    float tt = 20; int view = 0, sp = 1; sscanf(name.c_str() + 3, "%f_%d_%d", &tt, &view, &sp);
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    plane.engineRunning = true; plane.engineSpool = 0.7f;
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    traffic.spawnEscort(plane.pos, plane.vel);
    float worst = 0;
    for (float x = 0; x < tt; x += 1 / 60.f) {
      realTime += 1 / 60.f; update(1 / 60.f);
      for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT && length(c.vel) > 1.f) worst = std::max(worst, acosf(clampf(dot(c.q.rotate(vec3(0, 0, -1)), normalize(c.vel)), -1.f, 1.f)) / DEG);
    }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::ESCORT)
      printf("esc: act %d t %.1f  jet %+.0f  %.0f m from the player  %.0f m/s  agl %.0f\n", traffic.escAct, traffic.escT, c.escSide, length(c.pos - plane.pos), c.speed, c.pos.y - g_world.height(c.pos.x, c.pos.z));
    printf("esc: worst nose-off-path %.1f deg %s\n", worst, worst < 2.f ? "ok" : "FAIL");
    toasts.clear();
    if (view == 1) { camMode = 1; lookYaw = 0; lookPitch = -0.05f; camYaw = 0; camPitch = 0.12f; }
    else if (view == 2) { hudOn = false; dbgCam = true; vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0))); dbgCamPos = plane.pos + r * 160.f + vec3(0, 40, 0) - f * 60.f; dbgCamLook = plane.pos + f * 60.f; }
    return;
  }
  if (name.compare(0, 3, "ufo") == 0) {   // UFO encounter at t seconds: ufo<t>_<view> (0 chase-style, 1 cockpit, 2 close-up of the hatch)
    float tt = 12; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &tt, &view);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.f;
    botControl = true; plane.ctl.throttle = 0.75f;
    startUfo(); ufo.side = 1.f;
    for (float x = 0; x < tt; x += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    toasts.clear(); hudOn = false;
    if (view == 1) { camMode = 1; lookYaw = -80.f * DEG; lookPitch = 0.f; camYaw = lookYaw; camPitch = 0.12f; }
    else { dbgCam = true; dbgCamLook = ufo.pos + ufo.up * 1.5f;
      dbgCamPos = view == 2 ? ufo.pos + ufo.right * 9.f + ufo.up * 2.5f : plane.pos - ufo.right * 3.f + ufo.up * 2.f; }
    printf("ufo: t %.1f hatch %.2f laugh %.2f wave %.2f, %.1f m from the player\n", ufo.t, ufo.hatch, ufo.laugh, ufo.wave, length(ufo.pos - plane.pos));
    return;
  }
  if (name.compare(0, 3, "xrf") == 0) {   // XR-30 formation pass: xrf<seconds after spawn>; camera at the player looking at the leader
    float secs = 20; sscanf(name.c_str() + 3, "%f", &secs);
    plane.reset(&kAircraft[1], vec3(-4000, 700, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); timeOfDay = 14.f;
    traffic.reset(); traffic.spawnFormation(plane.pos, vec3());
    for (float tt = 0; tt < secs; tt += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    for (auto& c : traffic.craft) if (c.role == TrafficCraft::FORMATION && c.leader < 0) {
      printf("xrf: leader %.0f m from the player, %.0f m/s\n", length(c.pos - plane.pos), c.speed);
      dbgCam = true; dbgCamPos = plane.pos + vec3(0, 3, 0); dbgCamLook = c.pos; break; }
    hudOn = false; toasts.clear(); return;
  }
  if (name.compare(0, 4, "gtun") == 0) {   // g-force tunnel at a forced strength (percent), chase view: gtun<pct>
    resAirborne = true; realTime = 20; launchResearch();
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    gTunnel = atof(name.c_str() + 4) / 100.f; toasts.clear(); return;
  }
  // loading-screen pictures (--loadshots): loadshot_<CODE> an airport from an elevated three-quarter view with the aircraft
  // on its runway; loadshot_air_<n> aircraft n in flight, filmed from alongside. Afternoon light, a little cloud.
  if (name.compare(0, 13, "loadshot_air_") == 0) {
    static const char* kSpot[kWraith + 1] = {"MDB", "PMB", "ORC", "KLO", "FJH", "CAP", "PVI", "VCF", "LHK", "SMP", "GLS", "NPT", "HFS"};   // a different place each
    int n = std::clamp(atoi(name.c_str() + 13), 0, kWraith);
    resCraft = n; resAirborne = true; resTime = 16.3f; resWx = 0; resAirport = std::max(0, g_world.findAirport(kSpot[n]));
    launchResearch();
    {   // at its own cruise speed, holding height (the research launch's 200 m/s tears a light aircraft apart)
      const Airport& a = g_world.airports[resAirport];
      vec3 p = a.pos() + a.dir() * 1500.f; p.y = std::max(a.elev, g_world.height(p.x, p.z)) + 450.f;
      plane.reset(&kAircraft[n], p, a.heading, kAircraft[n].maxFuel, 85, true, kAircraft[n].cruise);
      plane.ctl.throttle = 0.7f; botControl = true; plane.apEngage(Plane::AP_HOLD, -1, wx);
    }
    wx.cloudCover = 0.3f; realTime = 20;
    for (int i = 0; i < 20; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    float size = std::max(plane.spec->span, plane.spec->fusLen), R = size * 1.5f + 6.f;
    dbgCam = true; dbgFollow = true;
    dbgFollowOff = plane.right() * (R * 0.85f) + plane.forward() * (R * 0.55f) + vec3(0, R * 0.16f, 0);
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 4, "gav_") == 0) {   // an aircraft parked on the runway, orbit view: gav_<spec>_<yaw>_<pitch>_<dist>[_<gear>]
    int sp = 0; float yawD = 120, pitD = 10, dist = 0, gearAt = -1;
    sscanf(name.c_str() + 4, "%d_%f_%f_%f_%f", &sp, &yawD, &pitD, &dist, &gearAt);
    sp = std::clamp(sp, 0, kWraith);
    Contract c; c.from = g_world.findAirport("CAP"); c.to = g_world.findAirport("MDB"); c.title = "Aircraft check";
    c.wx = Weather(); c.wx.timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.5f; c.wx.cloudCover = 0.2f; c.wx.visibility = 60000;
    realTime = 20;
    if (kAircraft[sp].special) { resCraft = sp; resAirport = c.from; resAirborne = false; resTime = c.wx.timeOfDay; resWx = 0; resCard = -1; launchResearch(); }   // (the research craft as their flights show them)
    else startFlight(c, sp, Career::SRC_OWNED);
    for (int i = 0; i < 30; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (dist <= 0) dist = kAircraft[sp].span * 1.15f;
    float y = yawD * DEG, pt = pitD * DEG;
    vec3 off = (plane.forward() * cosf(y) + plane.right() * sinf(y)) * (dist * cosf(pt)) + vec3(0, dist * sinf(pt), 0);
    dbgCam = true; dbgFollow = true; dbgFollowOff = off; dbgCamPos = plane.pos + off; dbgCamLook = plane.pos;
    toasts.clear(); hint.clear(); uiHidden = true; hudOn = false;
    camMode = getenv("GAVOUT") ? 0 : 1;   // (the cabin model shows the interior through the windows; GAVOUT: the outside model)
    if (const char* cs = getenv("GAVCTL")) {   // (debug: the controls held at pitch,roll,yaw,flaps - the surfaces' directions)
      float cp = 0, cr = 0, cy = 0, cf = 0; sscanf(cs, "%f,%f,%f,%f", &cp, &cr, &cy, &cf);
      botControl = true; plane.ctl.pitch = cp; plane.ctl.roll = cr; plane.ctl.yaw = cy; plane.ctl.flaps = cf; plane.flaps = cf;
      const int n = getenv("GAVCTLN") ? atoi(getenv("GAVCTLN")) : 60;   // (the settle time: the controls' smoothing)
      for (int i = 0; i < n; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    }
    if (gearAt >= 0) {   // (the gear held part way: lifted clear of the runway for the few frames that follow)
      plane.gear = std::min(gearAt, 1.f); plane.ctl.gearDown = gearAt >= 0.5f; plane.pos.y += 2.f; plane.vel = vec3();
    }
    return;
  }
  if (name.compare(0, 4, "apv_") == 0 && name.size() >= 9) {   // airport detail views: apv_<CODE>_<view>_<hour>
    int ai = std::max(0, g_world.findAirport(name.substr(4, 3).c_str()));
    int view = atoi(name.c_str() + 8);
    size_t us = name.find('_', 8);
    float tod = us != std::string::npos ? (float)atof(name.c_str() + us + 1) : 15.f;
    Contract c; c.from = ai; c.to = (ai + 1) % (int)g_world.airports.size(); c.title = "Airport";
    c.wx = Weather(); c.wx.timeOfDay = tod; c.wx.cloudCover = 0.25f; c.wx.visibility = 60000; c.wx.windSpeed = 6; c.wx.windFrom = 200;
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    const Airport& a = g_world.airports[ai];
    AptLayout L = aptLayout(a, ai);
    float mid = 0.5f * (L.apU0 + L.apU1);
    vec3 cam, look;
    if (view == 0) { cam = aptWorld(a, L.apU0 - 120.f, L.side * (L.twV - 60.f), 0); cam.y = a.elev + 70.f; look = aptWorld(a, mid, L.side * L.bldV, a.elev + 5.f); }
    else if (view == 1) { cam = aptWorld(a, L.termU - 140.f, L.side * (L.apV0 + 10.f), 0); cam.y = a.elev + 4.f; look = aptWorld(a, L.termU + 40.f, L.side * L.bldV, a.elev + 6.f); }
    else if (view == 2) { cam = aptWorld(a, L.standU0 - 60.f, L.side * (L.twV - 30.f), 0); cam.y = a.elev + 25.f; look = aptWorld(a, L.standU1, L.side * L.bldV, a.elev + 4.f); }
    else { cam = aptWorld(a, -a.length * 0.5f - 300.f, -L.side * 60.f, 0); cam.y = a.elev + 160.f; look = aptWorld(a, 0, L.side * L.apV0, a.elev); }
    dbgCam = true; dbgFollow = false; dbgCamPos = cam; dbgCamLook = look;
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 9, "loadshot_") == 0) {
    int ai = std::max(0, g_world.findAirport(name.substr(9).c_str()));
    Contract c; c.from = ai; c.to = (ai + 1) % (int)g_world.airports.size(); c.title = "Airport";
    c.wx = Weather(); c.wx.timeOfDay = 16.3f; c.wx.cloudCover = 0.3f; c.wx.visibility = 60000;
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    const Airport& a = g_world.airports[ai];
    vec3 ctr = a.pos(), d = a.dir(), r = normalize(cross(d, vec3(0, 1, 0)));
    float len = a.length;
    vec3 cam = ctr + r * (len * 0.3f + 200.f) - d * (len * 0.4f + 120.f);
    cam.y = std::max(a.elev, g_world.height(cam.x, cam.z)) + 90.f + len * 0.03f;
    dbgCam = true; dbgFollow = false; dbgCamPos = cam; dbgCamLook = ctr + d * (len * 0.05f) + vec3(0, 5.f, 0);
    toasts.clear(); hint.clear(); uiHidden = true; return;
  }
  if (name.compare(0, 3, "apt") == 0 && name.size() >= 6) {   // on the runway of an airport: apt<CODE>, e.g. aptHFS
    int ai = g_world.findAirport(name.substr(3, 3).c_str());
    Contract c; c.from = std::max(ai, 0); c.to = (c.from + 1) % (int)g_world.airports.size(); c.title = "Airport check";
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 30; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    toasts.clear(); return;
  }
  if (name == "loading" || name == "loadingready" || name == "loadingair") {   // the pre-flight loading screen: card / live shot
    if (name == "loadingair") { resAirborne = true; resCraft = kResearchJet; launchResearch(); }
    else { Contract c = career.board.empty() ? Contract() : career.board[0]; if (career.board.empty()) { c.from = 0; c.to = 1; c.title = "Lesson 1: Takeoff and Climb"; } startFlight(c, 0, Career::SRC_OWNED); }
    screen = SCR_LOADING; loadT = 0; loadReadyT = -1; loadMap = false; toasts.clear(); hint.clear();
    if (name != "loading") { loadT = 3.f; loadReadyT = 0.f; loadShown = 1.f; }
    return;
  }
  if (name == "seacrash") {  // ditches into deep sea: the wreck must float briefly, then sink to the seabed
    float gx = 0, gz = 0;
    for (int i = 0; i < 4000 && g_world.height(gx, gz) > -25.f; i++) { gx = -30000.f + (i % 63) * 1000.f; gz = -30000.f + (i / 63) * 1000.f; }
    plane.reset(&kAircraft[1], vec3(gx, 50.f, gz), 30, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[1].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    float hiW = -1e9f, hiD = -1e9f;
    for (int i = 0; i < 60 * 40; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (i == 60 * 2) { for (auto& w : wreck) hiW = std::max(hiW, w.c.y); }
    }
    float topW = -1e9f; for (auto& w : wreck) topW = std::max(topW, w.c.y);
    for (auto& d : debris) hiD = std::max(hiD, d.p.y);
    printf("seacrash: seabed %.0f m, highest piece %.1f m at 2 s, %.1f m at 40 s, highest fragment %.1f m %s\n", g_world.height(gx, gz), hiW, topW, hiD,
           crashed && hiW > -2.f && topW < -3.f && hiD < -3.f ? "ok" : "FAIL");
    toasts.clear(); return;
  }
  if (name.compare(0, 5, "crash") == 0) {
    // flies into a field nose-down, then advances the wreck simulation by the given number of tenths of a second
    int sp = name.size() > 6 ? name[5] - '0' : 1;
    float after = name.size() > 7 ? atof(name.c_str() + 7) * 0.1f : 1.f;
    float gx = -9000, gz = 11500;
    plane.reset(&kAircraft[sp], vec3(gx, g_world.height(gx, gz) + 60.f, gz), 30, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise);
    plane.q = plane.q * quat::axisAngle(vec3(1, 0, 0), -0.6f);
    plane.vel = plane.q.rotate(vec3(0, 0, -kAircraft[sp].cruise));
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true; hudOn = false;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < 600 && !crashed; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
    for (float tt = 0; tt < after; tt += 1 / 60.f) { realTime += 1 / 60.f; crashTimer = std::min(crashTimer, 5.f); update(1 / 60.f); }
    toasts.clear();
    return;
  }
  if (name == "top") { camMode = 2; camYaw = 0.3f; camPitch = 1.35f; camZoom = 4.f; }
  if (name == "orbit") { camMode = 2; camYaw = 2.3f; camPitch = 0.25f; camZoom = 0.6f; timeOfDay = 9.0f; }
  for (int i = 0; i < 30; i++) updateCamera(0.1f);
  if (name == "hud") hint = contract.hints.size() ? expandHint(contract.hints[2]) : "";
  if (name == "gps" || name == "pause" || name == "minimap") {
    plane.reset(&kAircraft[1], vec3(-4000, 600, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
    for (int i = 0; i < 60; i++) trail.push_back(vec2(-4000 - sinf(40 * DEG) * i * 110.f + sinf(i * 0.1f) * 300.f, 9000 + cosf(40 * DEG) * i * 110.f));
    if (name == "gps") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    if (name == "pause") paused = true;
    if (name == "minimap") showMinimap = true;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "gpsap" || name == "apfinal" || name == "apvtol") {   // autopilot: GPS autoland pick, then the approach
    int sp = name == "apvtol" ? 8 : 4;   // (the XR-40: the research jet that lands vertically)
    plane.reset(&kAircraft[sp], vec3(-4000, 900, 9000), 40, kAircraft[sp].maxFuel, 100, true, kAircraft[sp].cruise * 0.8f);
    plane.engineRunning = true; plane.engineSpool = 0.7f; plane.ctl.throttle = 0.7f;
    takeoffAnnounced = true; engineAutoStarted = true; camQ = plane.q; hint.clear();
    apDest = g_world.findAirport("CAP"); engageAutopilot();
    int target = name == "gpsap" ? -1 : name == "apfinal" ? Plane::APS_FINAL : Plane::APS_HOVER;
    for (int i = 0; i < (target < 0 ? 90 : 60 * 1500); i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (target >= 0 && plane.apStage == target && plane.apStageT > (target == Plane::APS_HOVER ? 14.f : 25.f)) break;
    }
    toasts.clear();
    if (name == "gpsap") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
}
