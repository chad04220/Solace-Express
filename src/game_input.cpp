// Somnium Volandi - input: the action bindings, input contexts, joysticks, settings files, the D-pad walk
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include "airport_layout.h"
#include "entities.h"
#include "entity_lod.h"
#include "hive_world_collision.h"
#include "scenery.h"
#include "regional_surface.h"
#include "models.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "military_missions.h"
#include "game_internal.h"

// ------------------------------------------------------------------ control bindings
const char* const kActionGroups[] = {"FLIGHT", "SYSTEMS", "VIEW / COMMS", "XR-40 SOMNUS"};
const ActionInfo kActions[ACT_COUNT] = {
  {"pitchDown", "Pitch down (nose down)", 0, 'W', 0},          {"pitchUp", "Pitch up (nose up)", 0, 'S', 0},
  {"rollLeft", "Roll left", 0, 'A', 0},                         {"rollRight", "Roll right", 0, 'D', 0},
  {"yawLeft", "Rudder left", 0, 'Q', PAD_LB},                   {"yawRight", "Rudder right", 0, 'E', PAD_RB},
  {"throttleUp", "Throttle up (VTOL: hold)", 0, K_SHIFT, 0},    {"throttleDown", "Throttle down", 0, K_CTRL, 0},
  {"trimUp", "Trim nose up", 0, K_RBRACKET, PAD_UP},            {"trimDown", "Trim nose down", 0, K_LBRACKET, PAD_DOWN},
  {"flapsDown", "Flaps down / pods to VTOL", 1, 'F', PAD_B},    {"flapsUp", "Flaps up / pods forward", 1, 'V', PAD_X},
  {"gear", "Landing gear", 1, 'G', PAD_Y},                      {"brake", "Wheel brakes", 1, K_SPACE, PAD_A},
  {"parkingBrake", "Parking brake", 1, 'B', PAD_LEFT},          {"autopilot", "Autopilot", 1, 'Z', PAD_RS},
  {"aerobatics", "Aerobatics: fly the next figure / stop", 1, 0xBA, 0},
  {"lights", "Landing lights", 1, 'L', 0},                      {"engine", "Engine start (hold: shut down)", 1, 'I', 0},
  {"timeAccel", "Time warp x1-x16 (above x4 may stutter on slower PCs)", 1, 'T', 0},
  {"camera", "Cycle camera", 2, 'C', PAD_BACK},                 {"hud", "Show / hide HUD", 2, 'H', 0},
  {"gpsMap", "GPS moving map", 2, 'N', 0},                      {"minimap", "Minimap", 2, K_TAB, 0},
  {"radio", "Internet radio", 2, 'R', 0},                       {"anr", "Headset noise cancelling", 2, 'M', PAD_LS},
  {"zoom", "Cockpit zoom (hold; wheel zooms too)", 2, 'U', 0},
  {"photo", "Photo mode", 2, K_F2, 0},   // (not a letter: they are all taken, K by the J + K hold)
  {"cloak", "Cloak / XR-50 rocket (or double-press A)", 3, 'X', 0},          {"weapons", "Weapons hot / safe", 3, 'Y', PAD_Y},
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
    case ACT_RADIO: case ACT_ANR: case ACT_PHOTO: return F | O | P;   // (photo mode: opened over a flight, the GPS or the pause menu)
    default: return F;
  }
}
Game::InputCtx Game::inputContext() const {
  if (bindCapture >= 0 || axisCapture >= 0) return CTX_BIND;
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
    joyUnarmed.resize(in.joys.size());
    for (size_t j = 0; j < in.joys.size(); j++) for (int b = 0; b < kJoyButtons; b++) joyUnarmed[j][b] |= in.joys[j].button[b];
    // Hover power is momentary, so triggers held across a pause / overlay must be released before powering up
    // again, just like a held key or button. Keep this separate from the conventional throttle's input path.
    if (plane.spec && plane.spec->special == 2 && plane.ctl.flaps > 0.99f) {
      vtolLtUnarmed |= in.pad && in.lt > 0.05f;
      vtolRtUnarmed |= in.pad && in.rt > 0.05f;
    }
    lastCtx = ctx;
  }
  if (!in.pad || in.lt <= 0.05f) vtolLtUnarmed = false;
  if (!in.pad || in.rt <= 0.05f) vtolRtUnarmed = false;
  for (int k = 0; k < 256; k++) { if (!in.down[k]) keyUnarmed[k] = false; if (keyUnarmed[k]) in.down[k] = in.pressed[k] = false; }
  padUnarmed &= in.buttons;
  in.buttons &= ~padUnarmed; in.buttonsPressed &= ~padUnarmed;
  joyUnarmed.resize(in.joys.size());   // (a device that came or went: its held buttons count from here)
  for (size_t j = 0; j < in.joys.size(); j++) for (int b = 0; b < kJoyButtons; b++) {
    if (!in.joys[j].button[b]) joyUnarmed[j][b] = false;
    if (joyUnarmed[j][b]) in.joys[j].button[b] = in.joys[j].pressed[b] = false;
  }
  updateJoysticks();
  // the device the prompts name: the gamepad from when it connects or is used, the keyboard from a key press
  if (in.pad && (!padWas || in.buttonsPressed || fabsf(in.lx) + fabsf(in.ly) + fabsf(in.rx) + fabsf(in.ry) > 0.6f || in.lt + in.rt > 0.3f)) padActive = true;
  for (int k = 1; k < 256 && padActive; k++) if (in.pressed[k]) padActive = false;
  if (padWas && !in.pad && screen == SCR_FLIGHT && !paused) { paused = true; settingsFromPause = false; toast("Controller disconnected - paused. Reconnect it, or carry on with the keyboard", vec3(1, 0.85f, 0.5f)); }
  padWas = in.pad;
}

float Game::joyAxis(int axis, bool* bound) const {
  const AxisBind& b = set.axisBind[axis];
  const int d = b.bound() ? joyFind(in.joys, b.dev, b.devName) : -1;
  if (bound) *bound = d >= 0;
  return d >= 0 && actOk(ACT_PITCH_UP) ? shapeAxis(in.joys[d].axis[b.axis], b, kFlightAxes[axis].unipolar) : 0.f;   // (flight input only where the flight's keys work)
}

// The joysticks' housekeeping, every frame: the first one ever seen has its stick bound to pitch and roll, so it flies
// as soon as it is plugged in; a bound flight axis's device lost in flight pauses it, as a lost gamepad does
void Game::updateJoysticks() {
  if (!set.joyAutoDone && !in.joys.empty()) {
    for (const JoyState& j : in.joys) if (j.hasAxis[0] && j.hasAxis[1]) {
      bool any = false; for (const AxisBind& a : set.axisBind) any |= a.bound();
      if (!any) {
        AxisBind roll; roll.dev = j.id; roll.devName = j.name; roll.axis = 0;
        AxisBind pitch = roll; pitch.axis = 1;   // (DirectInput's Y grows toward the pilot: pulling back is nose up)
        set.axisBind[AX_ROLL] = roll; set.axisBind[AX_PITCH] = pitch;
        toast(j.name + " found: its stick flies pitch and roll. Set up its other axes in Settings -> Joystick", vec3(.6f, .9f, 1.f), false);
      }
      set.joyAutoDone = true; saveSettings();
      break;
    }
  }
  bool bound = false, present = true;
  for (const AxisBind& a : set.axisBind) if (a.bound()) { bound = true; present &= joyFind(in.joys, a.dev, a.devName) >= 0; }
  const bool here = bound && present;
  if (joyFlightBound && !here && bound && screen == SCR_FLIGHT && !paused) {
    paused = true; settingsFromPause = false;
    toast("Joystick disconnected - paused. Reconnect it, or carry on with the keyboard", vec3(1, 0.85f, 0.5f));
  }
  joyFlightBound = here;
}

void Game::soakFlyOn() { if (screen == SCR_FLIGHT && plane.spec && !plane.onGround && !plane.apOn) plane.apEngage(Plane::AP_HOLD, -1, wx); }

// Report a problem (the pause menu): what was happening when the player pressed it - the build, the screen, the
// aircraft and where it was, the flight, the weather, the settings that matter and the last messages - in
// report_<time>.txt beside the logs, with room for what went wrong; the platform opens it to be filled in. Nothing is
// sent: the player sends it (with startup.log and any crash report) to the playtest
std::string Game::writeProblemReport() {
  const time_t now = time(nullptr); char stamp[32]; strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
  const std::string name = std::string("report_") + stamp + ".txt", path = saveDir.empty() ? name : saveDir + "/" + name;
  FILE* f = diskless ? nullptr : fopen(path.c_str(), "w");
  if (!f) return std::string();
  static const char* screens[] = {"menu", "hub", "flight", "debrief", "research", "loading", "free flight", "region loading"};
  fprintf(f, "Somnium Volandi problem report\nBuild: %s (%s)\nTime: %s\nScreen: %s%s\n", version.c_str(), buildStamp.c_str(), stamp,
          screens[std::clamp((int)screen, 0, 7)], paused ? " (paused)" : "");
  if (plane.spec) {
    fprintf(f, "Aircraft: %s (%s)%s\n", plane.spec->name, plane.spec->id, plane.onGround ? ", on the ground" : "");
    fprintf(f, "Position: x %.0f m, z %.0f m, altitude %.0f m (%.0f m above the ground), %.0f kt, heading %.0f, pitch %.1f\n",
            plane.pos.x, plane.pos.z, plane.pos.y, plane.agl(), length(plane.vel) * MS_TO_KT, plane.heading(), plane.pitchDeg());
    fprintf(f, "Controls: throttle %.2f, flaps %.2f, gear %s, autopilot %s, camera %d\n", plane.ctl.throttle, plane.ctl.flaps, plane.ctl.gearDown ? "down" : "up",
            plane.apOn ? plane.apStatus.c_str() : "off", camMode);
  }
  if (screen == SCR_FLIGHT || screen == SCR_DEBRIEF)
    fprintf(f, "Flight: %s \"%s\" %s -> %s%s%s%s\n", contract.id.c_str(), contract.title.c_str(),
            contract.from >= 0 && contract.from < (int)g_world.airports.size() ? g_world.airports[contract.from].code : "?",
            contract.to >= 0 && contract.to < (int)g_world.airports.size() ? g_world.airports[contract.to].code : "?",
            freeFlight ? " (free flight)" : "", researchFlight ? " (research)" : "", militaryFlight ? " (military)" : "");
  fprintf(f, "Weather: wind %.0f kt from %.0f, gusts %.0f kt, cloud %.0f%% at %.0f m, visibility %.0f m, precipitation %d%s, %.2f h\n",
          wx.windSpeed * MS_TO_KT, wx.windFrom, wx.gust * MS_TO_KT, wx.cloudCover * 100.f, wx.cloudBase, wx.visibility, wx.precip, wx.storm ? ", storm" : "", wx.timeOfDay);
  fprintf(f, "Settings: quality %d, resolution mode %d, frame-rate target %d, UI scale %.2f, traffic %d, fullscreen %d\n",
          set.quality, set.resMode, set.fpsTarget, set.uiScale, set.traffic, set.fullscreen);
  fprintf(f, "Input: gamepad %s, joysticks %d", in.pad ? "connected" : "none", (int)in.joys.size());
  for (const JoyState& j : in.joys) fprintf(f, ", %s", j.name.c_str());
  fprintf(f, "\n\nLast messages:\n");
  for (const auto& t : toasts) fprintf(f, "  %s\n", glyphText(t.text).c_str());
  const size_t n = atc.history.size();
  for (size_t i = n > 12 ? n - 12 : 0; i < n; i++) fprintf(f, "  radio: %s\n", glyphText(atc.history[i]).c_str());
  fprintf(f, "\nWhat happened? What did you expect instead? (Write below, save, and send this file with startup.log and any\n"
             "crash report from the crash folder.)\n\n");
  fclose(f);
  return path;
}

// The pad's names follow its bindings; the stick and trigger actions a pad drives without a button binding are named
// by the stick or trigger; an action the pad has no control for keeps its key (the keyboard still works with a pad).
std::string Game::actLabel(int a, bool pad, bool glyph) const {
  if (pad) {
    auto g = [&](const std::string& n) { return glyph ? padGlyph(n) : n; };
    if (set.padBind[a]) return g(padName(set.padBind[a]));
    switch (a) {
      case ACT_THR_UP: return g("RT"); case ACT_THR_DN: return g("LT");
      case ACT_PITCH_UP: case ACT_PITCH_DN: case ACT_ROLL_L: case ACT_ROLL_R: return g("LEFT STICK");
    }
  }
  return keyName(set.keyBind[a]);
}
// The hint as shown on the active device: bound controls and no "(or gamepad ...)" aside on a pad.
// Lesson speech is resolved separately by mission and phase, never by these device-specific words.
std::string Game::expandHint(const std::string& raw, bool pad, bool glyph) const {
  std::string out;
  for (size_t i = 0; i < raw.size(); i++) {
    size_t e = raw[i] == '{' ? raw.find('}', i) : std::string::npos;
    int a = -1;
    if (e != std::string::npos) { std::string id = raw.substr(i + 1, e - i - 1); for (int k = 0; k < ACT_COUNT; k++) if (id == kActions[k].id) a = k; }
    if (pad && raw.compare(i, 13, " (or gamepad ") == 0 && raw.find(')', i) != std::string::npos) { i = raw.find(')', i); continue; }
    if (a < 0) { out += raw[i]; continue; }
    out += actLabel(a, pad, glyph); i = e;
  }
  return out;
}

std::string padGlyph(const std::string& name) {
  std::string n = name;
  for (char& c : n) if (c == ' ') c = '_';   // (one word: a line wraps around it, never through it)
  return "\x01" + n + "\x02";
}

std::string padName(unsigned bit) {
  static const char* n[] = {"A", "B", "X", "Y", "LB", "RB", "VIEW", "MENU", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "LS CLICK", "RS CLICK"};
  for (int i = 0; i < 14; i++) if (bit & (1u << i)) return n[i];
  return "--";
}

// Settings / pause menu "press a key" capture. Runs before anything else reads the frame's input and swallows it,
// so the press that sets a binding never also flies the aircraft or closes the menu.
void Game::updateBindCapture(float dt) {
  if (axisCapture >= 0) {   // a flight axis: the joystick axis moved furthest from where it was, once past half its travel;
                            // the way it moved (the capture asks for nose up, right, full) sets its direction
    bindCaptureT += dt;
    axisCaptureRest.resize(in.joys.size());
    int bestDev = -1, bestAx = -1; float best = .5f;
    for (size_t d = 0; d < in.joys.size(); d++) for (int a = 0; a < kJoyAxes; a++)
      if (in.joys[d].hasAxis[a] && fabsf(in.joys[d].axis[a] - axisCaptureRest[d][a]) > best) { best = fabsf(in.joys[d].axis[a] - axisCaptureRest[d][a]); bestDev = (int)d; bestAx = a; }
    const bool cancel = in.pressed[K_ESC] || (in.buttonsPressed & PAD_START) || bindCaptureT > 8.f;
    if (bestDev >= 0 && !cancel) {
      AxisBind& b = set.axisBind[axisCapture];
      b.dev = in.joys[bestDev].id; b.devName = in.joys[bestDev].name; b.axis = bestAx;
      b.invert = in.joys[bestDev].axis[bestAx] < axisCaptureRest[bestDev][bestAx];
    }
    if (bestDev >= 0 || cancel) { axisCapture = -1; saveSettings(); g_audio.trigger(SFX_CLICK); }
    memset(in.pressed, 0, sizeof(in.pressed)); in.buttonsPressed = 0;
    for (JoyState& j : in.joys) memset(j.pressed, 0, sizeof(j.pressed));
    for (int i = 0; i < 3; i++) in.mPressed[i] = false;
    return;
  }
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
  } else if (bindCaptureDev == 2) {   // a joystick button: the first pressed, on any device
    for (size_t d = 0; d < in.joys.size() && !done; d++) for (int bt = 0; bt < kJoyButtons && !done; bt++) {
      if (!in.joys[d].pressed[bt]) continue;
      const JoyButtonBind nb{in.joys[d].id, in.joys[d].name, bt};
      for (int b = 0; b < ACT_COUNT; b++)   // (one already used in the same group swaps over)
        if (b != a && kActions[b].group == kActions[a].group && joyDev(b) == (int)d && set.joyBind[b].button == bt) set.joyBind[b] = set.joyBind[a];
      set.joyBind[a] = nb; done = true;
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
  for (JoyState& j : in.joys) memset(j.pressed, 0, sizeof(j.pressed));
  for (int i = 0; i < 3; i++) in.mPressed[i] = false;
}

void Game::loadSettings() {
  FILE* f = fopen(joinPath(saveDir, "settings.cfg").c_str(), "r");
  if (!f) { loadJoystickConfig(); loadMachineSettings(); return; }
  resDone.clear(); trialBest.clear(); trialBestSplits.clear();   // (read twice at startup: the lists are the file's, not appended to)
  const std::string today = trialKey(TR_DAILY);
  char k[64]; float v;
  while (fscanf(f, "%63s %f", k, &v) == 2) {
    if (!std::isfinite(v) || fabsf(v) > 1e6f) continue;   // a damaged value keeps the default
    std::string s = k;
    if (s.rfind("rescard.", 0) == 0) { if (v > 0.5f) resDone.insert(s.substr(8)); }
    else if (s.rfind("trial.", 0) == 0) {
      size_t dot = s.rfind('.');
      if (dot > 6 && v >= 0.f) {
        const std::string id = s.substr(6, dot - 6);
        if (id.rfind("T_DAILY", 0) == 0 && id != today) continue;   // (another day's course: its board went with it)
        auto& L = trialBest[id]; L.push_back(v); std::sort(L.begin(), L.end()); if (L.size() > 5) L.resize(5);
      }
    }
    else if (s.rfind("trialSplit.", 0) == 0) {   // the best run's gate splits, in gate order
      size_t dot = s.rfind('.');
      if (dot > 11 && v >= 0.f) {
        const std::string id = s.substr(11, dot - 11); const int i = atoi(s.c_str() + dot + 1);
        if ((id.rfind("T_DAILY", 0) == 0 && id != today) || i < 0 || i > 64) continue;
        auto& L = trialBestSplits[id]; if ((int)L.size() <= i) L.resize(i + 1, 0.f); L[i] = v;
      }
    }
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
    else if (s == "fov") set.fov = clampf(v, 40.f, 80.f);
    else if (s == "cockpitFocusZoom") set.cockpitFocusZoom = v != 0;
    else if (s == "foveate") set.foveate = v != 0;
    else if (s == "headLook") set.headLook = v != 0;
    else if (s == "menuMotion") set.menuMotion = v != 0;
    else if (s == "cbHud") set.cbHud = v != 0;
    else if (s == "uiScale") set.uiScale = clampf(v, 0.8f, 1.4f);
    else if (s == "joyAuto") set.joyAutoDone = v != 0;
    else if (s == "assist") set.assist = std::clamp((int)v, 0, 2);
    else if (s == "camShake") set.camShake = clampf(v, 0.f, 1.5f);
    else if (s == "rumble") set.rumble = v != 0;
    else if (s == "speedFov") set.speedFov = v != 0;
    else if (s == "musicVol") set.musicVol = clampf(v, 0.f, 1.f);
    else if (s == "musicFlight") set.musicFlight = v != 0;
    else if (s == "camRecentre") set.camRecentre = v != 0;
    else if (s == "failures") set.failures = v != 0;
    else if (s == "air") set.air = std::clamp((int)v, 0, 1);
    else if (s == "hudEssential") set.hudEssential = v != 0;
    else if (s.rfind("hudCam", 0) == 0 && s.size() == 7 && s[6] >= '0' && s[6] <= '3') set.hudCam[s[6] - '0'] = v != 0;
    else if (s.rfind("key.", 0) == 0 || s.rfind("pad.", 0) == 0)
      for (int i = 0; i < ACT_COUNT; i++)
        if (s.compare(4, std::string::npos, kActions[i].id) == 0) {
          if (s[0] == 'k') set.keyBind[i] = std::clamp((int)v, 0, 255); else set.padBind[i] = (unsigned)v & 0x3FFFu;
        }
  }
  fclose(f);
  loadJoystickConfig();
  loadMachineSettings();   // (this PC's display settings over the synced file's)
}

// joystick.cfg: the flight axes and the actions' joystick buttons (joystick.h)
static const char* const* actionIds() {
  static const char* ids[ACT_COUNT];
  for (int i = 0; i < ACT_COUNT; i++) ids[i] = kActions[i].id;
  return ids;
}
void Game::loadJoystickConfig() {
  std::string text;
  if (FILE* f = fopen(joinPath(saveDir, "joystick.cfg").c_str(), "r")) {
    char b[512]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) text.append(b, n);
    fclose(f);
  }
  for (auto& a : set.axisBind) a = AxisBind();
  for (auto& b : set.joyBind) b = JoyButtonBind();
  joyConfigParse(text, set.axisBind, set.joyBind, actionIds(), ACT_COUNT);
  joyWritten = joyConfigText(set.axisBind, set.joyBind, actionIds(), ACT_COUNT);
}

// Written only when something changed (the settings page calls this every frame), through a temp file so an
// interrupted write never leaves a half-written settings file.
// (false: the settings file couldn't be written)
void Game::loadMachineSettings() {
  if (cacheDir.empty()) return;   // (a test, a tool: the settings file's own)
  FILE* f = fopen(joinPath(cacheDir, "display.cfg").c_str(), "r");
  if (!f) {   // a PC's first start (or this one's before display.cfg): a Deck gets settings made for its screen
    if (steam::onDeck()) {   // (each load until display.cfg is written: the settings file is read again at the start)
      set.quality = 0; set.resMode = 1; set.fpsTarget = 30; set.fullscreen = true; set.uiScale = 1.15f;
      deckPreset = true;
    }
    return;
  }
  char k[64]; float v;
  while (fscanf(f, "%63s %f", k, &v) == 2) {
    if (!std::isfinite(v)) continue;
    const std::string s = k;
    if (s == "quality") set.quality = (int)clampf(v, 0, 2);
    else if (s == "fullscreen") set.fullscreen = v != 0;
    else if (s == "renderRes") set.resMode = std::clamp((int)v, 0, 4);
    else if (s == "fpsTarget") set.fpsTarget = std::clamp((int)v, 0, 240);
    else if (s == "foveate") set.foveate = v != 0;
    else if (s == "uiScale") set.uiScale = clampf(v, 0.8f, 1.4f);
  }
  fclose(f);
}

bool Game::saveSettings() {
  if (diskless) return true;   // (a tool session: the player's settings stay as they were)
  if (!cacheDir.empty()) {   // this PC's display settings, apart (loadMachineSettings)
    const std::string m = fmt("quality %d\nfullscreen %d\nrenderRes %d\nfpsTarget %d\nfoveate %d\nuiScale %f\n", set.quality, set.fullscreen, set.resMode, set.fpsTarget, set.foveate, set.uiScale);
    if (m != machineWritten) {
      const std::string path = joinPath(cacheDir, "display.cfg"), tmp = path + ".tmp";
      if (FILE* f = fopen(tmp.c_str(), "w")) {
        bool ok = fwrite(m.data(), 1, m.size(), f) == m.size();
        ok = fclose(f) == 0 && ok;
        if (ok && replaceFile(tmp, path)) machineWritten = m; else remove(tmp.c_str());
      }
    }
  }
  std::string t = fmt("quality %d\nmaster %f\nengineVol %f\nsfxVol %f\nradioVol %f\ninvertPitch %d\nshowHints %d\nmetric %d\nfullscreen %d\nradioStation %d\nmouseSens %f\ntraffic %d\natcVol %f\n",
          set.quality, set.master, set.engineVol, set.sfxVol, set.radioVol, set.invertPitch, set.showHints, set.metric, set.fullscreen, set.radioStation, set.mouseSens, set.traffic, set.atcVol);
  t += fmt("renderRes %d\nfpsTarget %d\nfov %f\nheadLook %d\ncbHud %d\nuiScale %f\nhudCam0 %d\nhudCam1 %d\nhudCam2 %d\nhudCam3 %d\n", set.resMode, set.fpsTarget, set.fov, set.headLook, set.cbHud, set.uiScale, set.hudCam[0], set.hudCam[1], set.hudCam[2], set.hudCam[3]);
  t += fmt("cockpitFocusZoom %d\nfoveate %d\nmenuMotion %d\njoyAuto %d\n", set.cockpitFocusZoom, set.foveate, set.menuMotion, set.joyAutoDone);
  t += fmt("assist %d\ncamShake %f\nrumble %d\nspeedFov %d\nmusicVol %f\nmusicFlight %d\ncamRecentre %d\nfailures %d\nair %d\nhudEssential %d\n", set.assist, set.camShake, set.rumble, set.speedFov, set.musicVol, set.musicFlight, set.camRecentre, set.failures, set.air, set.hudEssential);
  {   // the joystick bindings: their own file (device names don't fit this one's "name number" lines)
    static const std::string none = [] { AxisBind a[AX_COUNT]; std::vector<JoyButtonBind> b(ACT_COUNT); return joyConfigText(a, b.data(), actionIds(), ACT_COUNT); }();
    const std::string j = joyConfigText(set.axisBind, set.joyBind, actionIds(), ACT_COUNT);
    if (j != joyWritten && !(joyWritten.empty() && j == none)) {   // (nothing bound, nothing read: no file to write)
      const std::string path = joinPath(saveDir, "joystick.cfg"), tmp = path + ".tmp";
      if (FILE* f = fopen(tmp.c_str(), "w")) {
        bool ok = fwrite(j.data(), 1, j.size(), f) == j.size();
        ok = fclose(f) == 0 && ok;
        if (ok && replaceFile(tmp, path)) joyWritten = j; else remove(tmp.c_str());
      }
    }
  }
  for (int i = 0; i < ACT_COUNT; i++) t += fmt("key.%s %d\npad.%s %u\n", kActions[i].id, set.keyBind[i], kActions[i].id, set.padBind[i]);
  for (const std::string& id : resDone) t += "rescard." + id + " 1\n";
  for (auto& tb : trialBest) for (size_t i = 0; i < tb.second.size(); i++) t += fmt("trial.%s.%d %f\n", tb.first.c_str(), (int)i, tb.second[i]);
  for (auto& tb : trialBestSplits) for (size_t i = 0; i < tb.second.size(); i++) t += fmt("trialSplit.%s.%d %f\n", tb.first.c_str(), (int)i, tb.second[i]);
  if (t == settingsWritten) return true;
  std::string path = joinPath(saveDir, "settings.cfg"), tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  bool ok = fwrite(t.data(), 1, t.size(), f) == t.size();
  ok = fclose(f) == 0 && ok;
  if (ok) ok = replaceFile(tmp, path);   // (in one step: a crash between a remove and a rename left no settings at all)
  if (!ok) remove(tmp.c_str());
  if (ok) settingsWritten = t;
  return ok;
}

// The internet radio's stations are the player's own: none ship with the game. The first run writes the save folder's
// radio_stations.txt with how to add them; a list an earlier version filled with presets is kept as it is.
void Game::loadStations() {
  stations.clear();
  std::string path = joinPath(saveDir, "radio_stations.txt");
  FILE* f = fopen(path.c_str(), "r");
  bool userFile = f != nullptr;
  if (!f) f = fopen("radio_stations.txt", "r");
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (s.empty() || s[0] == '#') continue;
      size_t bar = s.find('|');
      if (bar == std::string::npos) stations.push_back({s, s});
      else stations.push_back({s.substr(0, bar), s.substr(bar + 1)});
    }
    fclose(f);
  }
  if (!userFile && !saveDir.empty() && !diskless)
    if (FILE* w = fopen(path.c_str(), "w")) {
      fprintf(w, "# Somnium Volandi internet radio stations\n"
                 "# One per line:  Name|URL   (an MP3 or AAC stream over HTTP or HTTPS), for example:\n"
                 "#   My Station|https://example.com/stream.mp3\n"
                 "# Lines starting with # are ignored. After editing, press Reload in the game's radio panel.\n");
      for (auto& s : stations) fprintf(w, "%s|%s\n", s.first.c_str(), s.second.c_str());
      fclose(w);
    }
  set.radioStation = stations.empty() ? 0 : std::clamp(set.radioStation, 0, (int)stations.size() - 1);
}

void Game::focusLost() {
  windowFocused = false; controllerFocusRearm = true;
  if (screen == SCR_FLIGHT && !crashed) paused = true;
  hangarView.release(); researchView.release(); militaryView.release();
  std::memset(in.down, 0, sizeof(in.down));
  in.endFrame();
  for (int i = 0; i < 3; ++i) { in.mReleased[i] = in.mDown[i]; in.mDown[i] = false; }
  in.buttons = in.buttonsPressed = 0;
  in.lx = in.ly = in.rx = in.ry = in.lt = in.rt = 0;
  padHoldA = false;
}

void Game::filterControllerFocus() {
  if (windowFocused && !controllerFocusRearm) return;
  const bool neutral = !in.pad || (!in.buttons && std::fabs(in.lx) <= .15f && std::fabs(in.ly) <= .15f &&
    std::fabs(in.rx) <= .15f && std::fabs(in.ry) <= .15f && in.lt <= .15f && in.rt <= .15f);
  // Read the raw neutral state before suppressing it. Run once after polling, so a held button cannot
  // turn into a new edge on return, nor can this frame's filtered zeroes prematurely re-arm it.
  if (windowFocused && neutral) controllerFocusRearm = false;
  else controllerFocusRearm = true;
  in.buttons = in.buttonsPressed = 0;
  in.lx = in.ly = in.rx = in.ry = in.lt = in.rt = 0;
  if (padHoldA) { in.mDown[0] = in.mPressed[0] = false; in.mReleased[0] = true; padHoldA = false; }
}


// ------------------------------------------------------------------ gamepad menus
// Outside active flight the left stick drives an on-screen cursor: A clicks, B backs out (same as Esc),
// the right stick scrolls lists. Any real mouse movement hands control back to the mouse.
void Game::gamepadMenus(float dt) {
  if (!windowFocused) return;
  if (fabsf(in.mdx) + fabsf(in.mdy) > 0.5f) padCursorT = -100.f;
  bool menus = (screen != SCR_FLIGHT || paused || crashed) && !photo.on;   // (photo mode's sticks fly its camera)
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
  if (ry != 0 && !menuPreviewTarget()) { static float acc = 0; acc += ry * dt * 8.f; while (fabsf(acc) >= 1.f) { in.wheel += acc > 0 ? 1.f : -1.f; acc -= acc > 0 ? 1.f : -1.f; } }
  // research menu: D-pad also steps through launch sites, Start launches
  if (screen == SCR_RESEARCH) {
    // Every research airframe has focusable preview controls. Start remains a
    // distinct launch action, never an extra activation of the focused control.
    if (in.buttonsPressed & PAD_RB) resAirport = stepResearchSite(resAirport, 1);
    if (in.buttonsPressed & PAD_LB) resAirport = stepResearchSite(resAirport, -1);
  }
}

// the focus moves with the arrow keys (menus, the hub, the results, the pause screen; the research terminal keeps
// its own left / right for the site) and with the D-pad everywhere a menu is up; the mouse takes over again when it moves
void Game::focusNavigate() {
  focusPrev.swap(focusList); focusList.clear();
  focusStep = 0; focusPointer = false;
  // (the focus's place, for the pointer while the D-pad leads: hovered, the right stick's scroll)
  auto point = [&]() { if (focusNav) for (auto& f : focusPrev) if (f.id == focusId) { focusPointer = true; focusPointerX = f.x + f.w * 0.5f; focusPointerY = f.y + f.h * 0.5f; } };
  bool menus = screen != SCR_FLIGHT || paused || crashed;
  int scr = screen * 4 + (paused ? 1 : 0) + (screen == SCR_HUB ? hubTab * 8 : 0);
  if (!menus) { focusNav = false; return; }
  if (scr != focusScreen) { focusScreen = scr; focusNav = false; hangarView.release(); researchView.release(); militaryView.release(); }
  if (fabsf(in.mdx) + fabsf(in.mdy) > 0.5f || in.mPressed[0]) focusNav = false;
  const bool researchViewFocus = screen == SCR_RESEARCH && focusNav &&
    focusId >= 0x7E5F0030u && focusId < 0x7E5F0035u;
  bool keys = screen != SCR_RESEARCH || researchViewFocus;
  int dx = 0, dy = 0;
  if ((keys && in.pressed[K_LEFT]) || (in.buttonsPressed & PAD_LEFT)) dx = -1;
  if ((keys && in.pressed[K_RIGHT]) || (in.buttonsPressed & PAD_RIGHT)) dx = 1;
  if (researchViewFocus) in.pressed[K_LEFT] = in.pressed[K_RIGHT] = false;
  if (in.pressed[K_UP] || (in.buttonsPressed & PAD_UP)) dy = -1;
  if (in.pressed[K_DOWN] || (in.buttonsPressed & PAD_DOWN)) dy = 1;
  if (!dx && !dy) { if (focusNav) { bool still = false; for (auto& f : focusPrev) if (f.id == focusId) still = true; if (!still && !focusPrev.empty()) focusId = focusPrev[0].id; } point(); return; }
  if (focusPrev.empty()) return;
  // Tab switches research airframes and unfocused Left/Right selects the site.
  // Give keyboard-only pilots a complete, deterministic Up/Down control walk;
  // spatial-only vertical movement strands controls in neighbouring columns.
  if (screen == SCR_RESEARCH && (in.pressed[K_UP] || in.pressed[K_DOWN]) &&
      !(in.buttonsPressed & (PAD_UP | PAD_DOWN))) {
    int index = -1;
    if (focusNav) for (size_t i = 0; i < focusPrev.size(); ++i)
      if (focusPrev[i].id == focusId) index = int(i);
    index = index < 0 ? 0 : std::clamp(index + dy, 0, int(focusPrev.size()) - 1);
    focusNav = true; focusId = focusPrev[index].id;
    g_audio.trigger(SFX_HOVER,.5f); point(); return;
  }
  const Focusable* cur = nullptr; for (auto& f : focusPrev) if (f.id == focusId) cur = &f;
  if (!focusNav || !cur) {   // first press: the top-left button
    const Focusable* best = &focusPrev[0];
    for (auto& f : focusPrev) if (f.y + f.x * 0.02f < best->y + best->x * 0.02f) best = &f;
    focusNav = true; focusId = best->id; g_audio.trigger(SFX_HOVER, 0.5f); point(); return;
  }
  if (dx && !dy && cur->adjust) { focusStep = dx; point(); return; }   // (left / right step the value of what has the focus)
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
  point();
}

void Game::drawPadCursor() {
  if (!in.pad || realTime - padCursorT > 6.f || focusNav) return;   // (the D-pad's focus is outlined where it is)
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
