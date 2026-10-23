// Somnium Volandi - game flow, flight session, cameras, particles, lights, audio feed
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
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

static void fillPlaneVisual(PlaneVisual& pv, const Plane& p, float propAngle, bool inside);

// XR-30 wingtip (body frame) matching mapJet's cranked delta in shaders.h
static const vec3 kJetWingTip(5.62f, -0.38f, 4.4f);
// XR-30 nozzle swivel (0 aft .. 90 deg down) plus pitch vectoring; mirrored by mapJet and the exhaust plumes
static float jetNozzleAngle(const Plane& p) { return p.nozzle * 0.5f * PI - clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1) * 0.5f; }
static const vec3 kSomnusWingTip(6.2f, -0.24f, 2.0f);
// research craft exhaust exits and jet directions (body coords): XR-30 two 2D nozzles, XR-40 four pods.
// strength: that exhaust's share of the thrust (1 = the XR-30's)
static int jetExhausts(const Plane& p, vec3* pos, vec3* dir, float* strength) {
  if (p.spec->special == 2) {
    for (int i = 0; i < 4; i++) {
      float a0 = p.podTilt[i], a = a0 + p.podVane[i], y = p.podYaw[i];
      pos[i] = kSomnusPods[i] + vec3(0, -sinf(a0), cosf(a0)) * 1.55f;
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

// The flown time for a job's plan when it's in; else (start) a background flight is begun for it if none is running
void Game::applyQuote(const Contract& c, Career::LaunchPlan& e, bool start) {
  if (quoteJob.valid() && quoteJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) quoteFlown[quoteJobKey] = quoteJob.get();
  if (c.longHaul()) return;   // (across the ocean, beyond this world: the quick estimate is the quote)
  // An ID alone is not a route: FREE, a continued job, or a changed forecast
  // can keep its ID while its estimated time and fuel change.
  std::string key = menuPlan::quoteInputs(c, e.spec);
  auto it = quoteFlown.find(key);
  if (it != quoteFlown.end()) { career.useFlownTime(e, c, it->second.first, it->second.second); return; }
  if (!start || quoteJob.valid() || c.forceAircraft >= 0) return;   // (lessons are flown by hand: the quick estimate stands)
  Plane::perf(&kAircraft[e.spec]);   // (learned here first: the worker then only reads it)
  quoteJobKey = key;
  Contract cc = c; int spec = e.spec;
  quoteJob = std::async(std::launch::async, [cc, spec] { float fuel = -1; float m = simulateFlightMinutes(cc, spec, &fuel); return std::make_pair(m, fuel); });
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

// ------------------------------------------------------------------ settings / save
static std::string joinPath(const std::string& d, const char* f) { return d.empty() ? std::string(f) : d + "/" + f; }


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

void Game::saveGame() {
  if (diskless) return;
  if (career.save(joinPath(saveDir, "career.sav"))) hasSave = true;
  else toast("Couldn't save the career (disk full or folder not writable)", vec3(1.f, 0.4f, 0.3f));
}
bool Game::commit(const std::function<void(Career&)>& change) {
  Career cand = pendingCareer ? *pendingCareer : career;
  change(cand);
  if ((headless && saveDir.empty()) || diskless) { career = cand; pendingCareer.reset(); return true; }   // (tests, tools: no disk)
  if (cand.save(joinPath(saveDir, "career.sav"))) { career = cand; pendingCareer.reset(); hasSave = true; saveWhy.clear(); return true; }
  pendingCareer = cand; retryT = 0;
  saveWhy = "Couldn't save the career (disk full or folder not writable)";
  toast(saveWhy + " - will retry", vec3(1.f, 0.4f, 0.3f));
  return false;
}
bool Game::retryCommit() {
  if (!pendingCareer) return true;
  if (diskless) { career = *pendingCareer; pendingCareer.reset(); return true; }
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
  if (launchFuelKg > 0) return std::min(launchFuelKg, s.maxFuel);
  if (p.fuelLoadKg >= 0) return clampf(p.fuelLoadKg, 0.f, s.maxFuel);
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
// Reuse only the expensive, unchanged route calculation. The flown quote, paid
// continuation fees, selected tank and current uplift are applied to a fresh copy.
// Both the visible figures and the Fly gate use this path, including a selection
// or fuel change made later in the same UI frame.
Career::LaunchPlan Game::menuLaunchPlan(const Contract& c, int spec, Career::Source src, bool continuing, bool startQuote) {
  Career::LaunchPlan p = menuPlanCache.get(career, c, spec, src);
  if (continuing) continuationWaivers(p, c, spec, src);
  applyQuote(c, p, startQuote);
  career.planFuel(p, c, chosenFuel(c, spec, src, p));
  return p;
}
Career::LaunchPlan Game::finalizeLaunchPlan(const Contract& c, int spec, Career::Source src) {
  Career::LaunchPlan p = career.plan(c, spec, src);
  applyQuote(c, p, false);   // route estimate first: it must not replace the player's selected uplift afterward
  career.planFuel(p, c, chosenFuel(c, spec, src, p));
  return p;
}
void Game::beginCareerFlight(const Contract& c, int spec, Career::Source src) {
  // (a contract of the world loaded, in the career's own region: the campaign's are the islands' alone)
  if (g_world.regionId != career.region || (c.story && !career.atHome())) { hubMsg = "The campaign is flown in the Solace Islands: travel home to fly it."; hubMsgTime = 5; return; }
  // (the route's plan and the save, then the flight: all behind the loading card - Game::queueLaunch)
  if (!commitBlocked() && queueLaunch(c, spec, LAUNCH_CAREER, [this, c, spec, src] { beginCareerFlight(c, spec, src); })) return;
  const Career::LaunchPlan p = finalizeLaunchPlan(c, spec, src);
  if (!commitLaunch([&](Career& k) {
    k.attempt++; k.attemptOpen = true;
    // (lessons and checkrides are flown whole: no job; a free flight leaves a job waiting at its stop as it is)
    if (Career::resumable(c)) k.accept(c, spec, src, p);
    else if (!(k.job && k.job->state == Career::JobState::RECOVERY)) k.job.reset();
  })) return;
  earthFlight = earthCourseHold = false; earthDestination.reset();   // (a career flight's chart is its region's, unless it is a long haul)
  if (!c.longHaul()) { startFlight(c, spec, src, &p); return; }
  Contract local = c; local.to = c.from;   // (until the destination's region is reached: the departure stands in for it)
  startFlight(local, spec, src, &p);
  beginHaul(c);
}
// A long haul under way: its state, the destination on this chart, and the Earth route the regions ahead are prepared on
void Game::beginHaul(const Contract& c) {
  const auto to = procedural::find(c.toRegion);
  const Airport* target = procedural::airportOf(c.toRegion, c.to);
  if (!to || !target) return;
  haul = Haul{}; haul.on = true; haul.job = c; haul.to = *to; haul.target = *target;
  haul.place = airportPlace(c.toRegion, *target);
  if (auto here = procedural::find(career.region)) flightRegion = *here; else flightRegion = procedural::solaceDescriptor();
  earthFlight = true; earthDestination = *to; earthCourseHold = false;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  chartHaul();
  toast(fmt("Long haul: %.0f km to %s %s, %s", c.haulKm, target->code, target->name, earthCharts::regionPhrase(*to).c_str()), vec3(0.6f, 1.f, 0.8f), false);
  toast("The GPS shows the way: the regions ahead are made ready as you cross", vec3(0.8f, 0.9f, 1.f), false);
}
void Game::chartHaul() {
  if (!haul.on) return;
  const vec3 at = regionTravel::Chart(flightRegion.origin).local(haul.place);
  const Airport* real = procedural::airportOf(haul.job.toRegion, haul.job.to);
  if (real) haul.target = *real;
  haul.target.x = at.x; haul.target.z = at.z;
}
void Game::continueJob(int spec, Career::Source src) {
  if (!career.job || career.job->state != Career::JobState::RECOVERY) return;
  if (!commitBlocked() && queueLaunch(career.job->continuation(), spec, LAUNCH_CAREER, [this, spec, src] { continueJob(spec, src); })) return;
  Contract c = career.job->continuation();
  Career::LaunchPlan p = finalizeLaunchPlan(c, spec, src);
  continuationWaivers(p, c, spec, src);
  bool took = commitLaunch([&](Career& k) {
    Career::JobState& J = *k.job;
    if (spec != J.spec || src != J.src) { J.hirePaid = false; J.ferryPaid = false; }   // a different aircraft: a new hire, its own ferry (C3)
    J.plan = p; J.spec = spec; J.src = src; J.state = Career::JobState::ACTIVE; k.attempt++; k.attemptOpen = true;
  });
  if (!took || !career.job || career.job->state != Career::JobState::ACTIVE) return;   // the save did not take: the job stays as it was, nothing flies on a stale career
  startFlight(c, spec, src, &p);
  applyJobLeg();
}
// the committed job's leg carries on from its checkpoints, its clock and the ride so far, its paid fees waived
void Game::applyJobLeg() {
  if (!career.job || career.job->state != Career::JobState::ACTIVE) return;
  const Career::JobState& J = *career.job;
  wpIndex = std::min(J.wpDone, (int)contract.wps.size()); result.wpDone = wpIndex; jobClockBase = J.jobClockMin * 60.f;
  result.patient = J.patient; result.comfort = J.comfort;
  surveyT = J.surveySec; surveyInT = J.surveyInSec; result.surveySec = surveyT; result.surveyInSec = surveyInT;
  result.surveyHistoryKnown = J.surveyHistoryKnown;
  result.surveyInBand = surveyT > 1.f ? surveyInT / surveyT : 1.f;
  if (J.hirePaid) launchPlan.hire = 0;
  if (J.ferryPaid) launchPlan.ferry = 0;
  jobLeg = true;
}
void Game::continuationWaivers(Career::LaunchPlan& p, const Contract& c, int spec, Career::Source src) const {
  if (!career.job) return;
  const Career::JobState& J = *career.job;
  if (J.positioningPaid) p.positioning = 0;
  if (spec != J.spec || src != J.src) { p.net = c.payout - p.fees() - p.fuelCostEst; return; }   // another aircraft: its hire and its ferry are new
  if (J.hirePaid) p.hire = 0;
  if (J.ferryPaid) p.ferry = 0;
  p.net = c.payout - p.fees() - p.fuelCostEst;
}
// The pause menu's Restart: the same flight from the start, in the mode it was flown in. A practice approach or a
// trial stays off the books, a job's leg carries on from where the job waited (its checkpoints, clock and ride, its
// paid fees), a research sortie relaunches (the review of v3.31.0, C1: Restart made practice and trials career
// flights, and reset a job leg's clock and comfort)
void Game::restartFlight() {
  if (freeFlight) { launchFreeFlight(); return; }
  if (haul.on) { toast("A long haul can't be restarted in flight: end the flight to call it off", vec3(1.f, 0.75f, 0.35f)); return; }
  if (researchFlight) { launchResearch(); return; }
  if (queueLaunch(contract, specIdx, LAUNCH_CAREER, [this] { restartFlight(); })) return;
  if (militaryFlight) { Contract c=contract; auto p=launchPlan; startFlight(c,specIdx,source,&p); return; }
  const bool iso = isolatedFlight, leg = jobLeg;
  Contract c = contract;
  const Career::LaunchPlan p = launchPlan;   // retry the accepted load and price, even if a newer estimate has arrived
  startFlight(c, specIdx, source, &p);
  if (iso) isolatedFlight = true;
  if (leg) applyJobLeg();
}
// The debrief's second button: a job waiting at its stop flies on from there with its clock, ride and paid fees (as
// the hub's Continue does); anything else (a lesson, a checkride, a job that ended) is flown again from the start
void Game::retryFromDebrief() {
  if (isMilitaryContract(contract.type)) { launchMilitary(contract.type-CT_MIL_RECON); return; }
  if (contract.longHaul()) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; return; }   // (a long haul: the board offers it again, or another)
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
  if (queueLaunch(c, spec, LAUNCH_CAREER, [this, spec, src] { practiseApproach(spec, src); })) return;
  startFlight(c, spec, src);
  isolatedFlight = true;
}

void Game::initFreeRegions() {
  if (!freeRegions.empty()) return;
  freeRegions.push_back(procedural::solaceDescriptor());
  for (const auto& region : procedural::recommendations())
    if (!region.authored) freeRegions.push_back(region);
}

// The military's combat space wraps as the world does (world.h): none does now
static float combatWrap() { return g_world.repeating ? WRAP_SPAN : 0.f; }

// The region the flight's chart is drawn about: the one Earth free flight has reached, else the World's own (Solace,
// for a career flight, a lesson, a sortie)
procedural::Descriptor Game::chartRegion() const {
  if ((freeFlight || haul.on) && earthFlight && !flightRegion.id.empty()) return flightRegion;
  if (auto region = procedural::find(g_world.regionId)) return *region;
  return procedural::solaceDescriptor();
}

// The generated regions round the flight on the horizon (Renderer::updateRegionHorizon), whatever the flight: they are
// where the Earth goes on past the islands' sea. On the real Earth, its far patches there (earth_far.h)
void Game::updateHorizon() {
  if (!g_ren.ok || screen != SCR_FLIGHT || g_world.repeating) return;
  g_ren.earthHorizon = realEarthPlay() ? earthCharts::pack() : nullptr;
  const procedural::Descriptor region = chartRegion();
  vec3 camera = dbgCam ? dbgCamPos : camPos;
  if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) camera = plane.pos;
  g_ren.updateRegionHorizon(regionTravel::Chart(region.origin).position(camera), region.worldSeed);
}

void Game::chooseFreeRegion(const procedural::Descriptor& region) {
  initFreeRegions();
  for (int i = 0; i < (int)freeRegions.size(); ++i)
    if (freeRegions[i].id == region.id) { freeRegion = i; return; }
  // A picker visit adds one descriptor, never a terrain map or an unbounded Earth catalog.
  // Retain the small launch catalog and replace the previous ad-hoc choice.
  const size_t fixed = 1 + procedural::recommendations().size();
  if (freeRegions.size() > fixed) freeRegions.resize(fixed);
  freeRegions.push_back(region); freeRegion = (int)freeRegions.size() - 1;
}

void Game::loadFreeRegion(int index, GameScreen after) {
  initFreeRegions();
  if (regionJob.valid() || regionUploading || index < 0 || index >= (int)freeRegions.size()) return;
  const procedural::Descriptor target = freeRegions[index];
  if (regionError.empty() && !quoteJob.valid() && g_world.regionId == target.id) {
    freeRegion = index; screen = after; return;
  }
  // Initial/menu loads may wait on previous speculation, but the flight thread
  // must never destroy a running async future (its destructor would join).
  retireEarthPrefetch();
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  freeRegion = index; loadingRegion = target; regionAfterLoad = after;
  regionPoseChange.reset(); earthFlight = earthCourseHold = false; earthDestination.reset();
  regionLoadTime = 0; regionLoadStage = 0; regionError.clear(); regionCancel = false;
  freeRegionMap = false; screen = SCR_REGION_LOADING;
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  paused = showMap = showRadio = settingsFromPause = false;
  atc.cancel(); commsPending.clear(); toasts.clear(); traffic.reset();
  hiveCombat.reset(1, combatWrap());
  g_audio.setParams(AudioParams{});
  gpsMapValid = loadMap = false;
  menuPlanCache = menuPlan::Cache{}; quoteFlown.clear();
  focusList.clear(); focusPrev.clear();
  in.pressed[K_ESC] = in.pressed[K_ENTER] = in.pressed[' '] = false;
  in.mPressed[0] = false; in.buttonsPressed = 0;
  const std::string path = diskless || cacheDir.empty() ? std::string() :
    joinPath(cacheDir, target.authored ? "world.bin" : "procedural-world.bin");
  const std::string stamp = buildStamp;
  const bool save = !diskless, makeMap = g_ren.ok;
  // A flown career quote may still be reading the original World. Moving its
  // future here drains it without freezing the loading screen or publishing a stale quote.
  regionJob = std::async(std::launch::async,
    [this, target, path, stamp, save, makeMap, retired = std::move(earthRetireJob), quote = std::move(quoteJob)]() mutable {
      if (retired.valid()) { try { retired.get(); } catch (...) {} }
      if (quote.valid()) { try { quote.get(); } catch (...) {} }
      sceneryResetWorldCaches(); // joins old-world scenery jobs before any global changes
      g_roadTraffic = RoadTraffic{};
      g_groundPits.clear();
      regionLoadStage = 1;
      // (a real-Earth chart: its fine elevation fetched first where it can be - offline, a few seconds lost, then ETOPO's)
      if (earthCharts::isEarthId(target.id)) if (auto* fetch = glo90()) { wantGlo90(target.origin); fetch->settle(40.0); }
      // Release the old terrain first: a second full region is never resident.
      g_world = World{};
      if (target.authored) g_world.build(path, stamp, save);
      else g_world.buildProcedural(target, path, stamp, save);
      if (g_world.airports.empty()) throw std::runtime_error("This region has no usable airports.");
      regionLoadStage = 2;
      // (its textures' and buffers' contents, the map and its levels, made here: the loading screen then only uploads
      // them, a slice a frame, as a flight's next region is published)
      return makeMap ? Renderer::prepareWorldUpload(g_world) : Renderer::WorldUploadData{};
    });
}

std::string Game::screenWorld() const { return screenWorldFor(screen); }
std::string Game::screenWorldFor(GameScreen screen) const {
  const std::string& here = g_world.regionId;
  switch (screen) {
    case SCR_HUB: if (hubTab == TAB_SETTINGS) return std::string();   // (the settings need no world: opened from the menu, none is loaded for them)
      [[fallthrough]];
    case SCR_DEBRIEF: return here == career.region ? std::string() : career.region;
    case SCR_RESEARCH: return here == "solace" ? std::string() : std::string("solace");
    case SCR_MENU: return here == "solace" || (hasSave && here == career.region) ? std::string() : std::string("solace");
    default: return std::string();
  }
}

std::string Game::careerPlaceName() const {
  if (g_world.regionId == career.region && career.location >= 0 && career.location < (int)g_world.airports.size()) return g_world.airports[career.location].name;
  if (career.atHome()) return "the Solace Islands";
  if (earthCharts::isEarthId(career.region)) if (const Airport* a = procedural::airportOf(career.region, career.location)) return a->name;
  if (auto d = procedural::find(career.region)) return "the " + d->name;
  return career.region;
}

void Game::loadCareerWorld(const std::string& id, GameScreen after) {
  if (regionJob.valid() || regionUploading || g_world.regionId == id) return;
  const auto region = procedural::find(id);
  if (!region) return;
  chooseFreeRegion(*region);
  loadFreeRegion(freeRegion, after);
}

void Game::updateRegionLoading(float dt) {
  regionLoadTime += dt;
  // Cancellation is deferred until the builder joins. No reader may resume in a partial World.
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) regionCancel = true;
  try {
    if (regionUploading) {   // the world's textures and buffers, up to 24 MB of them a frame, then published at once
      if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload(24u << 20, 8.0)) {
        if (g_ren.worldUploadError().empty()) return;
        Renderer::WorldUploadData back;   // (a staging that failed: the world loaded as before, all of it now)
        g_ren.cancelWorldUpload(&back);
        g_ren.reloadWorld(back.minimapLevels.empty() ? std::vector<uint8_t>{} : back.minimapLevels[0]);
      } else g_ren.commitWorldUpload(nullptr, false);
      regionUploading = false;
      finishRegionLoad();
      return;
    }
    if (!regionJob.valid() || regionJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto upload = regionJob.get();
    regionLoadStage = 3;
    if (g_ren.ok) {
      if (g_ren.beginWorldUpload(g_world, std::move(upload))) { regionUploading = true; return; }
      g_ren.reloadWorld(upload.minimapLevels.empty() ? std::vector<uint8_t>{} : upload.minimapLevels[0]);
    }
    finishRegionLoad();
  } catch (const std::exception& e) {
    regionUploading = false; regionError = e.what();
  } catch (...) {
    regionUploading = false; regionError = "The region could not be prepared.";
  }
}

void Game::finishRegionLoad() {
  freeAirport = freeLaunchPending ? std::clamp(freeAirport, 0, (int)g_world.airports.size() - 1) : 0;
  if (!freeEarthIdent.empty()) {   // (the real Earth: the airport chosen, its chart now loaded)
    if (g_world.earthChart) freeAirport = std::max(0, g_world.findAirport(freeEarthIdent.c_str()));
    freeEarthIdent.clear();
  }
  crashed = completed = landed = false;
  dbgCam = dbgFollow = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  g_ren.entBudgetMs = 2.5f;
  const bool launchAfterPreparation = freeLaunchPending && !regionCancel;
  freeLaunchPending = false;
  if (regionCancel && !loadingRegion.authored) {   // (a generated region given up: back to the islands)
    loadFreeRegion(0, SCR_MENU); return;
  }
  screen = regionCancel ? SCR_MENU : regionAfterLoad;
  if (screen == SCR_HUB && g_world.regionId == career.region) { career.refreshBoard(); selContract = 0; selAircraft = -1; }   // (the career's region: its board)
  if (launchAfterPreparation && screen == SCR_FREE_FLIGHT) { launchFreeFlight(); return; }
  if (screen == SCR_FREE_FLIGHT) toast(loadingRegion.name + " ready. Choose an airport and take flight.", vec3(.6f, 1.f, .8f), false);
}

void Game::openEarthPicker() {
  if (!freeFlight || crashed) return;
  initFreeRegions();
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  // Nearby choices are descriptors only; every island is generated when reached.
  freeRegions.clear(); freeRegions.push_back(procedural::solaceDescriptor(flightRegion.worldSeed));
  for (const auto& region : procedural::nearby(earthPosition, 12, flightRegion.worldSeed))
    if (!region.authored) freeRegions.push_back(region);
  chooseFreeRegion(earthDestination ? *earthDestination : flightRegion);
  freeRegionMap = true; showMap = showRadio = false; paused = true;
}

// On the real Earth a route goes to a real airport: its chart's place (the runway's centre on the lattice), the
// autopilot holding the course there as for a region
void Game::setEarthAirportRoute(const earth::AirportRecord& airport) {
  const auto chart = earthCharts::chartForAirport(airport.ident);
  if (!chart) return;
  setEarthDestination(*chart);
  earthRouteIdent = airport.ident + " " + airport.name;
}

void Game::setEarthDestination(const procedural::Descriptor& region) {
  if (!freeFlight || crashed || screen != SCR_FLIGHT) return;
  earthRouteIdent.clear();
  const auto destination = region; // the picker may replace descriptor storage below
  earthDestination = destination; earthFlight = true;
  earthPrefetchFailedId.clear(); earthPrefetchError.clear(); // selecting a route explicitly retries a failed candidate
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  gpsMapValid = false;
  earthCourseHold = !plane.onGround;
  if (earthCourseHold) {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    earthCourseHold = plane.apOn;
    if (auto heading = regionTravel::Chart(flightRegion.origin).bearing(plane.pos, destination.origin)) plane.apHeading = (float)*heading;
    apDest = -1;
  }
  toast(destination.name + " route set. Nearby regions prepare in the background as you fly.", vec3(.6f, 1.f, .8f));
  if (plane.spec && geo::surfaceDistance(earthPosition, destination.origin) / 1000. >
      plane.nominalRangeKm())
    toast("Route exceeds nominal fuel range. Choose a closer stop or a longer-range aircraft.", vec3(1.f, .7f, .3f));
}

void Game::retireEarthPrefetch() {
  if (earthPrefetchCancel) earthPrefetchCancel->requested = true;
  earthPrefetchCancel.reset();
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.cancelWorldUpload(&retiredGpu);
  if (earthSceneryDraining) { g_scenery.resumeStreaming(); earthSceneryDraining = false; }
  earthPrefetchId.clear(); earthPrefetchFailedId.clear(); earthPrefetchError.clear();
  if (!earthPrefetchJob.valid() && !earthPrefetch) return;
  // A cancelled candidate may still be building. Transfer every owner into a
  // worker instead of blocking on its future or freeing large arrays here.
  earthRetireJob = std::async(std::launch::async,
    [job = std::move(earthPrefetchJob), candidate = std::move(earthPrefetch), retired = std::move(earthRetireJob), gpu = std::move(retiredGpu)]() mutable {
      if (retired.valid()) { try { retired.get(); } catch (...) {} }
      if (job.valid()) { try { auto abandoned = job.get(); } catch (...) {} }
      candidate.reset(); gpu = Renderer::WorldUploadData{};
    });
}

void Game::updateEarthSurfaceContext() {
  // every flight: the generated regions on its horizon are solid (a career flight never travels into them, but flies
  // into their ground as into any other)
  if (screen != SCR_FLIGHT || !plane.spec || g_world.repeating) {
    if (regionalSurface::flightContextEnabled()) regionalSurface::clearFlightContext();
    earthFacilitiesPending = earthPromotionDeferred = false;
    return;
  }
  const procedural::Descriptor region = chartRegion();
  // Protect the airframe plus the next maximum replayed frame of travel. Time
  // acceleration multiplies real geographic progress, including at low FPS.
  const float bodyRadius = .6f * std::max(plane.spec->span, plane.spec->fusLen) + plane.gearHeight();
  const float travel = length(vec3(plane.vel.x, 0, plane.vel.z)) * .25f * std::max(timeAccel, 1.f);
  regionalSurface::setFlightContext(region, regionTravel::Chart(region.origin).position(plane.pos),
    bodyRadius + travel, 50.f);
}

bool Game::earthPromotionSafe(const World& candidate, const regionTravel::Change& change) const {
  if (plane.onGround || crashed || !plane.spec || regionalSurface::regionPinned(candidate.regionId)) return false;
  if (g_ren.ok) {
    // Textures may be uploaded while a contact-pinned proxy remains visible.
    // The exact full-derived drawable surface must be safe before World moves.
    const auto surface = g_ren.worldUploadSurface();
    if (!surface || !regionalSurface::mayActivate(surface)) return false;
  }
  const float halfWidth = plane.spec->span * .6f, halfLength = plane.spec->fusLen * .6f;
  const float bodyRadius = std::max(halfWidth, halfLength) + plane.gearHeight();
  const float travel = length(vec3(plane.vel.x, 0, plane.vel.z)) * .25f * std::max(timeAccel, 1.f);
  const float radius = (bodyRadius + travel) * 1.02f + 2.f; // local chart scale/float slack
  const vec3 center = change.point(plane.pos);
  const float x0 = center.x - radius, x1 = center.x + radius, z0 = center.z - radius, z1 = center.z + radius;
  float incomingTop = 0.f;
  if (x0 < WORLD_HALF + 3000.f && x1 > -WORLD_HALF - 3000.f &&
      z0 < WORLD_HALF + 3000.f && z1 > -WORLD_HALF - 3000.f) {
    if (candidate.hmax[0].size() != size_t(HMAX_N) * HMAX_N) return false;
    auto cell = [](float v) { return std::clamp(int(std::floor((v + WORLD_HALF) * HMAX_N / (2.f * WORLD_HALF))), 0, HMAX_N - 1); };
    for (int z = cell(z0); z <= cell(z1); ++z) for (int x = cell(x0); x <= cell(x1); ++x)
      incomingTop = std::max(incomingTop, candidate.hmax[0][size_t(z) * HMAX_N + x]);
  }
  // hmax includes4m of crater allowance, even over perfectly flat sea. The
  // incoming chart starts without craters. Any positive terrain also reserves
  //350m for its ordinary generated building/tree skyline before clearance.
  bool incomingWater = incomingTop <= 4.f;
  if (!incomingWater) incomingTop += 350.f;
  auto obstacle = [&](float loX, float loZ, float hiX, float hiZ, float top) {
    if (hiX < x0 || loX > x1 || hiZ < z0 || loZ > z1) return;
    incomingWater = false; incomingTop = std::max(incomingTop, top);
  };
  for (const auto& box : candidate.boxes) obstacle(box.c.x-box.h.x, box.c.z-box.h.z, box.c.x+box.h.x, box.c.z+box.h.z, box.c.y+box.h.y);
  for (const auto& bridge : candidate.bridges) obstacle(bridge.minX, bridge.minZ, bridge.maxX, bridge.maxZ, bridge.maxY);
  auto placed = [&](const auto& items) { for (const auto& item : items) {
    const auto& info = kEntInfo[item.kind]; const auto& e = item.e;
    const float reach = std::hypot(info.hx * e.sx, info.hz * e.sz);
    obstacle(e.x-reach, e.z-reach, e.x+reach, e.z+reach, e.y+info.h*e.sy);
  } };
  placed(landmarks(candidate)); placed(civicLandmarks(candidate));
  const float oldTop = regionalSurface::currentUpperBound(plane.pos, plane.pos, radius);
  const bool boundClear = plane.pos.y - bodyRadius >= std::max(oldTop, incomingTop) + 250.f;
  const bool boundedWater = incomingWater && oldTop <= 0.f;
  const vec3 right = plane.right(), forward = plane.forward();
  bool allWater = true, clearOfLand = true;
  for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
    const vec3 contact = plane.pos + right * (x * halfWidth) + forward * (z * halfLength) - vec3(0, plane.gearHeight(), 0);
    const vec3 mapped = change.point(contact);
    // Non-global candidate Worlds explicitly bypass the regional fallback.
    const float current = g_world.height(contact.x, contact.z);
    const float incoming = candidate.height(mapped.x, mapped.z);
    allWater &= current < 0.f && incoming < 0.f;
    clearOfLand &= contact.y - std::max(current, 0.f) >= 250.f && mapped.y - std::max(incoming, 0.f) >= 250.f;
  }
  return (allWater && boundedWater) || (clearOfLand && boundClear);
}

void Game::updateEarthFlight() {
  updateEarthSurfaceContext();
  if (!(freeFlight || haul.on) || !earthFlight || crashed || screen != SCR_FLIGHT) { updateHorizon(); return; }
  const regionTravel::Chart chart(flightRegion.origin);
  earthPosition = chart.position(plane.pos);
  if (earthDestination && earthCourseHold) {
    const geo::Position goal = haul.on ? haul.place : earthDestination->origin;   // (a long haul's: its airport itself)
    if (!plane.apOn || plane.apMode != Plane::AP_HOLD) earthCourseHold = false;
    else if (geo::surfaceDistance(earthPosition, goal) < (haul.on ? 9000. : 2500.)) {
      earthCourseHold = false; plane.apDisengage();
      if (haul.on) toast(fmt("The %s ahead: land at %s %s", haul.to.name.c_str(), haul.target.code, haul.target.name), vec3(.6f, 1.f, .8f));
      else if (!earthRouteIdent.empty()) toast(earthRouteIdent + " ahead: it is on the GPS map", vec3(.6f, 1.f, .8f));
      else toast(earthDestination->id == g_world.regionId ? "Destination region reached. Choose a local airport on the GPS map." :
        "Destination region reached. Local airports remain unavailable while detailed terrain prepares.", vec3(.6f, 1.f, .8f));
    } else if (auto heading = chart.bearing(plane.pos, goal)) plane.apHeading = (float)*heading;
  }
  updateHorizon();
  if (realEarthPlay()) { slideEarthChart(chart); return; }
  auto owner = procedural::describe(earthPosition, flightRegion.worldSeed);
  earthFacilitiesPending = owner.id != g_world.regionId; earthPromotionDeferred = false;
  procedural::Descriptor wanted = owner;
  if (owner.id == flightRegion.id) {
    // Start a neighbor on the runway, not shortly before the seam. Even slow
    // aircraft then provide minutes of ocean travel for CPU/GPU preparation.
    vec3 horizontal(plane.vel.x, 0, plane.vel.z);
    if (length(horizontal) < 10.f) horizontal = plane.forward();
    auto heading = geo::initialBearing(earthPosition, chart.position(plane.pos + normalize(horizontal) * 1000.f));
    if (earthDestination && (earthCourseHold || plane.onGround)) heading = geo::initialBearing(earthPosition, earthDestination->origin);
    if (heading) for (double distance = 25000.; distance <= 700000.; distance += 25000.) {
      auto ahead = procedural::describe(geo::destination(earthPosition, *heading, distance), flightRegion.worldSeed);
      if (ahead.id != flightRegion.id) { wanted = ahead; break; }
    }
  }
  if (earthRetireJob.valid()) {
    if (earthRetireJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthRetireJob.get(); } catch (...) {}
  }
  if (earthSceneryDraining && (!earthPrefetch || owner.id != earthPrefetch->region.id)) {
    g_scenery.resumeStreaming(); earthSceneryDraining = false;
  }
  if (earthPrefetchJob.valid()) {
    // Ask obsolete generation to stop at its next safe checkpoint, but retain
    // its future until ready. Owner priority wins over a speculative heading.
    if (wanted.id != earthPrefetchId && earthPrefetchCancel) earthPrefetchCancel->requested = true;
    if (earthPrefetchJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthPrefetch = earthPrefetchJob.get(); }
    catch (const GenerationCancelled&) { earthPrefetchError.clear(); }
    catch (const std::exception& e) { earthPrefetchError = e.what(); earthPrefetchFailedId = earthPrefetchId; }
    catch (...) { earthPrefetchError = "Region preparation failed"; earthPrefetchFailedId = earthPrefetchId; }
    earthPrefetchCancel.reset();
    if (!earthPrefetchError.empty()) toast("Terrain preparation failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false);
  }
  if (earthPrefetch && earthPrefetch->region.id != wanted.id) { retireEarthPrefetch(); return; }
  if (!earthPrefetch) {
    if (wanted.id == flightRegion.id || wanted.id == earthPrefetchFailedId) return;
    earthPrefetchId = wanted.id; earthPrefetchError.clear();
    const std::string path = diskless || cacheDir.empty() ? std::string() :
      joinPath(cacheDir, wanted.authored ? "world.bin" : "procedural-world.bin");
    const std::string stamp = buildStamp;
    const bool save = !diskless, prepareGpu = g_ren.ok;
    earthPrefetchCancel = std::make_shared<GenerationCancelState>();
    earthPrefetchJob = std::async(std::launch::async, [wanted, path, stamp, save, prepareGpu, cancel = earthPrefetchCancel] {
      ScopedParallelLimit threadBudget(2);
      ScopedGenerationCancellation cancellation(cancel);
      generationCheckpoint();
      auto candidate = std::make_unique<PrefetchedRegion>();
      candidate->region = wanted; candidate->world = std::make_unique<World>();
      if (wanted.authored) candidate->world->build(path, stamp, save);
      else candidate->world->buildProcedural(wanted, path, stamp, save);
      generationCheckpoint();
      candidate->world->repeating = false;
      if (candidate->world->airports.empty()) throw std::runtime_error("Region has no usable airports");
      if (prepareGpu) candidate->upload = Renderer::prepareWorldUpload(*candidate->world);
      generationCheckpoint();
      return candidate;
    });
    return;
  }
  if (g_ren.ok) {
    if (!earthPrefetch->gpuStarted) {
      if (!g_ren.beginWorldUpload(*earthPrefetch->world, std::move(earthPrefetch->upload))) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
        toast("Terrain upload failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false); return;
      }
      earthPrefetch->gpuStarted = true;
    }
    if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload()) {
      if (!g_ren.worldUploadError().empty()) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
        toast("Terrain upload failed. Reselect the Earth route to retry.", vec3(1.f, .65f, .3f), false);
      }
      return;
    }
  }
  if (owner.id == flightRegion.id || owner.id != earthPrefetch->region.id) return;
  const regionTravel::Change promotionChange(chart, regionTravel::Chart(earthPrefetch->region.origin), plane.pos);
  if (!earthPromotionSafe(*earthPrefetch->world, promotionChange)) {
    earthPromotionDeferred = true;
    if (earthSceneryDraining) { g_scenery.resumeStreaming(); earthSceneryDraining = false; }
    return;
  }
  // Ownership has a 250 m hysteresis to prevent float round-trip ping-pong.
  vec3 horizontal(plane.vel.x, 0, plane.vel.z);
  if (length(horizontal) > .1f && procedural::describe(chart.position(plane.pos - normalize(horizontal) * 250.f),
      flightRegion.worldSeed).id != owner.id) return;
  // Start the scenery drain only far out over ocean. This is not a visibility
  // cutoff: distant coastlines use the renderer's coarse overlap across handoff.
  // Weather changes cannot delay the collision chart into the next island.
  // If preparation is late, aircraft/physics keep going over open ocean.
  const float dx = std::max(0.f, std::abs(plane.pos.x) - 40000.f);
  const float dz = std::max(0.f, std::abs(plane.pos.z) - 40000.f);
  if (std::sqrt(dx * dx + dz * dz) < 60000.f) return;
  if (quoteJob.valid()) {   // (a background quote still reading the islands: the chart moves only once it is done)
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { earthPromotionDeferred = true; return; }
    try { quoteJob.get(); } catch (...) {}
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  if (!earthSceneryDraining) { g_scenery.beginDrain(); earthSceneryDraining = true; }
  if (!g_scenery.isDrained()) return;
  sceneryResetWorldCaches(); // already drained: no hidden worker join
  g_roadTraffic = RoadTraffic{}; g_groundPits.clear();
  loadingRegion = earthPrefetch->region; regionResumePaused = paused;
  regionPoseChange = promotionChange;
  auto retired = std::make_unique<World>(std::move(g_world));
  earthPrefetch->scenery.reset();   // (a real-Earth chart's staged scenery, were one here: its workers done with the world first)
  g_world = std::move(*earthPrefetch->world);
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.commitWorldUpload(&retiredGpu); // staged resources only; no bulk upload
  earthPrefetch.reset(); earthPrefetchId.clear();
  earthRetireJob = std::async(std::launch::async, [old = std::move(retired), gpu = std::move(retiredGpu)]() mutable { old.reset(); gpu = Renderer::WorldUploadData{}; });
  atc.cancel(); commsPending.clear(); traffic.reset(); hiveCombat.reset(1, combatWrap());
  gpsMapValid = loadMap = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  continueEarthFlight();
  g_scenery.resumeStreaming(); earthSceneryDraining = false;
}

// On the real Earth: a chart of it, or Solace in a Free Flight with the pack there (and the generated islands not chosen)
bool Game::realEarthPlay() const {
  if (!earthFlight) return false;
  if (earthCharts::isEarthId(flightRegion.id)) return true;
  if (flightRegion.id != "solace" || !earthCharts::pack()) return false;
  if (haul.on) return career.realEarth;   // (a career's long haul out of the islands: its world beyond them)
  return freeFlight && !freeGeneratedEarth;
}

// The real Earth's fine elevation (earth_fetch.h): the fetcher made when first wanted, and the tiles a chart at a place reads
earthFetch::Glo90Fetcher* Game::glo90() {
  if (!glo90Fetch && !earthCharts::glo90Cache().empty() && earthCharts::pack())
    glo90Fetch = std::make_unique<earthFetch::Glo90Fetcher>(earthCharts::glo90Cache());
  return glo90Fetch.get();
}
void Game::wantGlo90(const geo::Position& where) {
  auto pack = earthCharts::pack();
  if (auto* fetch = glo90(); fetch && pack) fetch->want(*pack, earthFetch::tilesAround(where.latitude, where.longitude, WORLD_HALF * 1.5f));
}

const std::vector<Game::BeyondAirport>& Game::beyondAirports() {
  if (earthBeyondFor == flightRegion.id) return earthBeyond;
  earthBeyondFor = flightRegion.id; earthBeyond.clear();
  const auto pack = earthCharts::pack();
  if (!pack || !earthCharts::isEarthId(flightRegion.id)) return earthBeyond;
  const regionTravel::Chart chart(flightRegion.origin);
  const double lat = flightRegion.origin.latitude, lon = flightRegion.origin.longitude, reach = 120000.0;
  const double dLat = reach / 111000.0 + 0.1, dLon = dLat / std::max(std::cos(lat * PI / 180.0), 0.2);
  for (const earth::AirportRecord& a : pack->airports({lat - dLat, lon - dLon, lat + dLat, lon + dLon})) {
    if (a.runways.empty()) continue;
    const vec3 p = chart.local({a.lat, a.lon, 0.0});
    // (one in the square is g_world's, or one the chart's selection left out: never there to land on)
    if (std::max(fabsf(p.x), fabsf(p.z)) < WORLD_HALF || hypotf(p.x, p.z) > reach) continue;
    const earth::Runway& r = a.runways[0];
    vec3 along;
    if (r.endsKnown) along = chart.local({r.lat2, r.lon2, 0.0}) - chart.local({r.lat1, r.lon1, 0.0});
    else along = chart.local(geo::destination({a.lat, a.lon, 0.0}, r.hasHeading ? r.heading : 0.0, 1000.0)) - p;
    along.y = 0;
    BeyondAirport b;
    b.pos = vec2(p.x, p.z);
    const float span = length(along);
    b.dir = span > 1.f ? vec2(along.x / span, along.z / span) : vec2(0.f, 1.f);
    b.length = std::max(r.length, 300.f); b.size = a.type; b.ident = a.ident;
    earthBeyond.push_back(std::move(b));
  }
  return earthBeyond;
}

// The real Earth's pack installed from the game (earth_install.h): its zips from the source beside the game names (a
// release of the game's own otherwise), downloaded and unpacked in the user's folder, there beside where they go -
// the core alone, the land streamed as it is flown, unless whole (every zip: to fly with no connection)
bool Game::startEarthInstall(bool whole) {
  if (diskless || saveDir.empty() || (earthInstaller && earthInstaller->state() == earthInstall::Installer::RUNNING) || earthReopenJob.valid()) return false;
#ifdef _WIN32
  const std::string sep = "\\";
#else
  const std::string sep = "/";
#endif
  earthInstaller.reset();
  earthInstaller = std::make_unique<earthInstall::Installer>(earthInstall::sourceFor(assetDir), saveDir + sep + "earth-download", saveDir + sep + "earth", whole);
  return true;
}
void Game::updateEarthInstall() {
  if (!earthInstaller || earthInstaller->state() != earthInstall::Installer::DONE) return;
  // the new pack opened on a worker (its index, its tables and its cache folder read there), the installer's line still
  // up meanwhile; the user's earth folder is among those the game looks in
  if (!earthReopenJob.valid()) { earthReopenJob = std::async(std::launch::async, [] { earthCharts::reopen(); }); return; }
  if (earthReopenJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  try { earthReopenJob.get(); } catch (...) {}
  earthInstaller.reset();
  if (auto pack = earthCharts::pack())
    toast(pack->streamed() ? "The real Earth is ready: its land comes as you fly. Fly it from Free Flight, or out beyond the islands" :
                             "The real Earth is installed: fly it from Free Flight, or out beyond the islands", vec3(.6f, 1.f, .8f), false);
  else toast("The real Earth was installed but doesn't open: " + earthCharts::packError(), vec3(1.f, .5f, .35f), false);
}

// The real Earth (earth_charts.h, docs/REAL_EARTH_PLAN.md section 5): one chart after another under the flight, its
// edge never in reach. Past 5 km from its chart's centre the next is prepared ahead along the track - a minute's flying
// on, 20 to 60 km, on the lattice; one already preparing, or ready, kept while it is within 8 km of that - and the flight
// moves into it once nearer its centre than this one's by 2 km: over land or sea, at any height, its height over the
// ground kept when it is low (the two charts' grounds a few metres apart). Never on the ground
void Game::slideEarthChart(const regionTravel::Chart& chart) {
  earthFacilitiesPending = earthPromotionDeferred = false;
  vec3 horizontal(plane.vel.x, 0, plane.vel.z);
  const float speed = length(horizontal);
  if (speed < 10.f) { horizontal = plane.forward(); horizontal.y = 0; }
  if (length(horizontal) < 1e-3f) horizontal = vec3(0, 0, -1);
  horizontal = normalize(horizontal);
  const float out = hypotf(plane.pos.x, plane.pos.z);
  std::string wanted = flightRegion.id;   // (ids alone each frame: a chart's name is looked up once, when it starts)
  {   // (and every two seconds the fine elevation the next chart will read, fetched ahead of it)
    const vec3 ahead = plane.pos + horizontal * std::clamp(speed * 60.f, 20000.f, 60000.f);
    glo90WantT -= 1.f / 60.f;
    if (glo90WantT <= 0.f) { glo90WantT = 2.f; wantGlo90(chart.position(ahead)); }
  }
  if (out > 5000.f && !plane.onGround && !crashed) {
    const vec3 aim = plane.pos + horizontal * std::clamp(speed * 60.f, 20000.f, 60000.f);
    auto near = [&](const geo::Position& origin) { const vec3 c = chart.local(origin); return hypotf(c.x - aim.x, c.z - aim.z) < 8000.f; };
    // (a route's destination chart once the aim is within 40 km of it: the flight arrives in the chart about its airport)
    const bool toDestination = earthDestination && earthCharts::isEarthId(earthDestination->id) && [&] {
      const vec3 c = chart.local(earthDestination->origin); return hypotf(c.x - aim.x, c.z - aim.z) < 40000.f; }();
    if (toDestination) wanted = earthDestination->id;
    else if (earthPrefetch && near(earthPrefetch->region.origin)) wanted = earthPrefetch->region.id;
    else if (earthPrefetchJob.valid() && near(earthPrefetchOrigin)) wanted = earthPrefetchId;
    else { const geo::Position g = chart.position(aim); wanted = earthCharts::slideIdAt(g.latitude, g.longitude); }
  }
  if (earthRetireJob.valid()) {
    if (earthRetireJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthRetireJob.get(); } catch (...) {}
  }
  if (earthPrefetchJob.valid()) {
    if (wanted != earthPrefetchId && earthPrefetchCancel) earthPrefetchCancel->requested = true;
    if (earthPrefetchJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { earthPrefetch = earthPrefetchJob.get(); }
    catch (const GenerationCancelled&) { earthPrefetchError.clear(); }
    catch (const std::exception& e) { earthPrefetchError = e.what(); earthPrefetchFailedId = earthPrefetchId; }
    catch (...) { earthPrefetchError = "Chart preparation failed"; earthPrefetchFailedId = earthPrefetchId; }
    earthPrefetchCancel.reset();
    if (!earthPrefetchError.empty()) toast("The terrain ahead couldn't be prepared: " + earthPrefetchError, vec3(1.f, .65f, .3f), false);
  }
  if (earthPrefetch && earthPrefetch->region.id != wanted) { retireEarthPrefetch(); return; }
  if (!earthPrefetch) {
    if (wanted == flightRegion.id || wanted == earthPrefetchFailedId) return;
    const auto next = wanted == "solace" ? std::optional<procedural::Descriptor>(procedural::solaceDescriptor(flightRegion.worldSeed)) :
                                           earthCharts::find(wanted);
    if (!next) return;
    earthPrefetchId = wanted; earthPrefetchOrigin = next->origin; earthPrefetchError.clear();
    const bool prepareGpu = g_ren.ok;
    earthPrefetchCancel = std::make_shared<GenerationCancelState>();
    // (built on two threads, never written to the cache: a flight lays down a chart every few minutes. The islands are
    // read from theirs, as the launch reads them)
    const std::string solacePath = diskless || cacheDir.empty() ? std::string() : joinPath(cacheDir, "world.bin");
    earthPrefetchJob = std::async(std::launch::async, [wanted = *next, prepareGpu, cancel = earthPrefetchCancel, solacePath,
                                                       stamp = buildStamp, save = !diskless] {
      ScopedParallelLimit threadBudget(2);
      ScopedGenerationCancellation cancellation(cancel);
      generationCheckpoint();
      auto candidate = std::make_unique<PrefetchedRegion>();
      candidate->region = wanted; candidate->world = std::make_unique<World>();
      if (wanted.authored) candidate->world->build(solacePath, stamp, save);
      else candidate->world->buildProcedural(wanted, std::string(), std::string(), false);
      generationCheckpoint();
      candidate->world->repeating = false;
      if (prepareGpu) candidate->upload = Renderer::prepareWorldUpload(*candidate->world);
      generationCheckpoint();
      return candidate;
    });
    return;
  }
  if (g_ren.ok) {
    if (!earthPrefetch->gpuStarted) {
      if (!g_ren.beginWorldUpload(*earthPrefetch->world, std::move(earthPrefetch->upload))) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error; return;
      }
      earthPrefetch->gpuStarted = true;
    }
    if (!g_ren.worldUploadReady() && !g_ren.pumpWorldUpload()) {
      if (!g_ren.worldUploadError().empty()) {
        const auto failedId = earthPrefetch->region.id, error = g_ren.worldUploadError();
        retireEarthPrefetch(); earthPrefetchFailedId = failedId; earthPrefetchError = error;
      }
      return;
    }
  }
  // the move: nearer the new chart's centre than this one's by 2 km, and in the air - out of the islands over open water
  // (their chart holds them, its neighbours only the sea round them)
  const vec3 centre = chart.local(earthPrefetch->region.origin);
  auto moves = [&](vec3 p) {
    return hypotf(p.x - centre.x, p.z - centre.z) <= hypotf(p.x, p.z) - 2000.f &&
           (!flightRegion.authored || std::max(fabsf(p.x), fabsf(p.z)) >= WORLD_HALF - 10000.f);
  };
  // the new chart's scenery staged first, on workers of its own (Scenery::stage), about where the move will put the
  // aircraft: where the track first meets it, ten minutes on at most, else where the aircraft is. The move waits for what
  // stands within 1.5 km of it, so the land comes across with its buildings and trees rather than emptied and filling in
  // round the aircraft - unless the flight is 20 km out already, when it moves regardless
  int unstaged = 0;
  {
    auto& staged = earthPrefetch->scenery;
    if (!staged) staged = std::make_unique<Scenery>(2, earthPrefetch->world.get());
    if (!earthSceneryDraining) staged->resumeStreaming();   // (a move begun and put off: streaming again)
    vec3 at = plane.pos;
    const vec3 track(plane.vel.x, 0.f, plane.vel.z);
    for (int t = 0; t <= 600; t += 2) if (moves(plane.pos + track * (float)t)) { at = plane.pos + track * (float)t; break; }
    const EntRanges R = entRangesFor(g_ren.quality);
    const float farAll = std::max(R.big, R.build) + 300.f, farDetail = std::max(std::max(R.tree, R.rock), R.bush) + 300.f;
    unstaged = staged->stage(regionTravel::Chart(earthPrefetch->region.origin).local(chart.position(at)), farDetail, farAll, 1500.f);
  }
  if (!moves(plane.pos)) return;
  if (unstaged > 0 && out < 20000.f) return;
  if (plane.onGround || crashed || !plane.spec) { earthPromotionDeferred = true; return; }
  if (quoteJob.valid()) {
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { earthPromotionDeferred = true; return; }
    try { quoteJob.get(); } catch (...) {}
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  if (!earthSceneryDraining) { g_scenery.beginDrain(); earthPrefetch->scenery->beginDrain(); earthSceneryDraining = true; }
  if (!g_scenery.isDrained() || !earthPrefetch->scenery->isDrained()) return;
  const float above = plane.pos.y - g_world.height(plane.pos.x, plane.pos.z);
  const std::string wasNamed = flightRegion.name;
  sceneryResetWorldCaches();
  g_roadTraffic = RoadTraffic{}; g_groundPits.clear();
  loadingRegion = earthPrefetch->region; regionResumePaused = paused;
  regionPoseChange = regionTravel::Change(chart, regionTravel::Chart(earthPrefetch->region.origin), plane.pos);
  earthCarried.clear();
  for (const Airport& a : g_world.airports) earthCarried.push_back({a.code ? a.code : "", a.elev});
  auto retired = std::make_unique<World>(std::move(g_world));
  g_world = std::move(*earthPrefetch->world);
  g_scenery.adopt(*earthPrefetch->scenery);   // (its chunks, made from the world now g_world)
  Renderer::WorldUploadData retiredGpu;
  if (g_ren.ok) g_ren.commitWorldUpload(&retiredGpu);
  earthPrefetch.reset(); earthPrefetchId.clear(); earthPrefetchFailedId.clear();
  earthRetireJob = std::async(std::launch::async, [old = std::move(retired), gpu = std::move(retiredGpu)]() mutable { old.reset(); gpu = Renderer::WorldUploadData{}; });
  atc.cancel(); commsPending.clear(); hiveCombat.reset(1, combatWrap());
  traffic.carry(*regionPoseChange, [&](int old, float* lift) { return carriedAirport(old, lift); });   // (where they were)
  gpsMapValid = loadMap = false;
  wreck.clear(); debris.clear(); pops.clear(); particles.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  continueEarthFlight();
  earthCarried.clear();
  if (above < 30.f) {   // (low: its height over this chart's ground what it was over the last's)
    const float lift = g_world.height(plane.pos.x, plane.pos.z) + above - plane.pos.y;
    plane.pos.y += lift; plane.wxAir.y += lift; camPos.y += lift; dbgCamPos.y += lift; dbgCamLook.y += lift;
  }
  if (flightRegion.name == wasNamed) while (!toasts.empty() && toasts.back().text.rfind("Entering ", 0) == 0) toasts.pop_back();
  g_scenery.resumeStreaming(); earthSceneryDraining = false;
}

void Game::continueEarthFlight() {
  g_audio.resetWorld(); audioCameraValid = false;
  if (!regionPoseChange) throw std::runtime_error("Missing Earth-flight frame transition.");
  const auto& change = *regionPoseChange;
  // The aircraft is never reset/refuelled: body-space dynamics, engine state,
  // controls, failures, fuel and clocks survive the chart handoff unchanged.
  const vec3 oldPosition = plane.pos;
  const vec3 airRelative = oldPosition - plane.wxAir;
  const vec3 cloudRelative = oldPosition + vec3(cloudOff.x, 0, cloudOff.y);
  plane.pos = change.point(plane.pos); plane.vel = change.vector(plane.vel); plane.q = change.attitude(plane.q);
  plane.windVel = change.vector(plane.windVel); plane.windAvg = change.vector(plane.windAvg);
  plane.wxAir = plane.pos - change.vector(airRelative); plane.wxl.draft = change.vector(plane.wxl.draft);
  plane.apVelPrev = change.vector(plane.apVelPrev); plane.apHeading = change.heading(plane.apHeading);
  plane.apStuntHdg = change.heading(plane.apStuntHdg); plane.apStuntRight0 = change.vector(plane.apStuntRight0);
  plane.apLd = change.vector(plane.apLd); plane.apTd = change.point(plane.apTd); plane.apHoldC = change.point(plane.apHoldC);
  plane.seamShift = vec3();
  // (an approach to a field the new chart holds too - the next real-Earth chart's, found by its ident - flown on, its
  // touchdown and its heights on that chart's ground; any other holds)
  float apLift = 0.f;
  const int apTo = plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) ? carriedAirport(plane.apAirport, &apLift) : -1;
  if (apTo >= 0) { plane.apTd.y += apLift; plane.apHoldAlt += apLift; plane.apIntAlt += apLift; }
  else if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR)) {
    plane.apMode = Plane::AP_HOLD; plane.apHeading = plane.heading(); plane.apAlt = plane.pos.y;
    plane.apStatus = "HOLD / NEW REGION";
  }
  plane.apAirport = apTo; plane.apHoldFor = carriedAirport(plane.apHoldFor); plane.apStuntWasAirport = carriedAirport(plane.apStuntWasAirport);
  if ((plane.apStuntWasMode == Plane::AP_NAV || plane.apStuntWasMode == Plane::AP_APPR) && plane.apStuntWasAirport < 0) plane.apStuntWasMode = Plane::AP_HOLD;
  // (the tower's exchange carried on with the same field, when the new chart holds it - and, on a long haul, it is the
  // one the flight is to land at)
  const AtcFlight tower = atcF;
  auto carryTower = [&](int arrival) {
    const int arr = carriedAirport(tower.arr), dep = carriedAirport(tower.dep);
    if (arr < 0 || (arrival >= 0 && arr != arrival)) return;
    atcF = tower; atcF.arr = arr; atcF.dep = dep >= 0 ? dep : arr; atcF.lastApt = carriedAirport(tower.lastApt);
    atcF.holdPos = change.point(tower.holdPos);
    if (dep < 0 && atcF.phase < 3) atcF.phase = 3;
  };
  camPos = change.point(camPos); camQ = change.attitude(camQ); camVel = change.vector(camVel);
  dbgCamPos = change.point(dbgCamPos); dbgCamLook = change.point(dbgCamLook);
  // Chase/cockpit yaw is body-relative; only the orbit camera stores world yaw.
  if (camMode == 2) camYaw -= (float)change.headingDelta() * DEG;
  wx.windFrom = change.heading(wx.windFrom); wxStart.windFrom = change.heading(wxStart.windFrom);
  contract.wx.windFrom = change.heading(contract.wx.windFrom); contract.wxEnd.windFrom = change.heading(contract.wxEnd.windFrom);
  const vec3 drift = change.vector(cloudRelative) - plane.pos; cloudOff = vec2(drift.x, drift.z);
  cloudDet = change.vector(cloudDet); wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet;
  flightRegion = loadingRegion; earthFlight = true;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  if (haul.on) {   // a long haul: on toward its destination, or there - the job's own airport the flight lands at
    const int near = std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z));
    // (there: the destination's own chart, or on the real Earth any chart that holds its airport - in the square, not
    // one beyond it)
    int at = flightRegion.id == haul.job.toRegion && haul.job.to < (int)g_world.airports.size() ? haul.job.to : -1;
    if (at < 0 && earthCharts::isEarthId(haul.job.toRegion) && haul.target.code) {
      const int i = g_world.findAirport(haul.target.code);
      if (i >= 0 && std::max(fabsf(g_world.airports[i].x), fabsf(g_world.airports[i].z)) < WORLD_HALF) at = i;
    }
    haul.arrived = at >= 0;
    contract.from = near; contract.to = haul.arrived ? at : near;
    chartHaul();
    attemptFrom = near; wpIndex = 0; apDest = carriedAirport(apDest); tdRunway = -2;
    atcF = AtcFlight{}; atcF.dep = near; atcF.arr = contract.to; atcF.phase = plane.onGround ? 0 : 3;
    carryTower(contract.to);
    somnus = SomnusState{}; ufo = Ufo{}; ufo.next = 180.f;
    tipOn = false; trailT = 0; wakeGap = true; wakeOdo = 0;
    craterR = craterD = 0; boomT = -1;
    regionPoseChange.reset();
    paused = regionResumePaused; screen = SCR_FLIGHT;
    earthFacilitiesPending = earthPromotionDeferred = false; updateEarthSurfaceContext();
    if (haul.arrived) toast(fmt("Over %s: land at %s %s", earthCharts::regionPhrase(flightRegion).c_str(), dest().code, dest().name), vec3(.6f, 1.f, .8f));
    else toast(fmt("Over %s: on to %s", earthCharts::regionPhrase(flightRegion).c_str(), earthCharts::regionPhrase(haul.to).c_str()), vec3(.6f, 1.f, .8f));
    return;
  }
  freeAirport = std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z));
  contract.from = contract.to = freeAirport; contract.wps.clear();
  contract.title = "Free Flight / " + flightRegion.name;
  launchPlan.startAirport = attemptFrom = freeAirport; wpIndex = 0; apDest = carriedAirport(apDest); tdRunway = -2;
  atcF = AtcFlight{}; atcF.dep = atcF.arr = freeAirport; atcF.phase = plane.onGround ? 0 : 3;
  carryTower(-1);
  somnus = SomnusState{}; ufo = Ufo{}; ufo.next = 180.f;
  tipOn = false; trailT = 0; wakeGap = true; wakeOdo = 0;
  craterR = craterD = 0; boomT = -1;
  regionPoseChange.reset();
  freeFlight = isolatedFlight = true; paused = regionResumePaused; screen = SCR_FLIGHT;
  earthFacilitiesPending = earthPromotionDeferred = false; updateEarthSurfaceContext();
  toast("Entering " + flightRegion.name + ".", vec3(.6f, 1.f, .8f));
}

int Game::carriedAirport(int old, float* lift) const {
  if (lift) *lift = 0.f;
  if (old < 0 || old >= (int)earthCarried.size() || earthCarried[old].code.empty()) return -1;
  const int i = g_world.findAirport(earthCarried[old].code.c_str());
  if (i >= 0 && lift) *lift = g_world.airports[i].elev - earthCarried[old].elev;
  return i;
}

int Game::atcStation(int airport) const {
  if (g_world.earthChart && airport >= 0 && airport < (int)g_world.airports.size() && g_world.airports[airport].code) {
    uint32_t h = 2166136261u;   // (the real Earth's: by its ident, the same voice in every chart that holds it)
    for (const char* c = g_world.airports[airport].code; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
    return (int)(h % AtcVoice::kVoices);
  }
  return (airport * 7 + 2) % AtcVoice::kVoices;
}

// Main-menu Free Flight never quotes, accepts, saves or settles a career job. A fresh
// zero-fee plan supplies full tanks even without a licence, money or an owned aircraft.
void Game::beginFreeFlightSetup() {
  initFreeRegions();
  if (auto region = procedural::find(g_world.regionId)) chooseFreeRegion(*region);
  freeRegionMap = false;
  freeCraft = validCareerSelection(freeCraft);
  freeAirport = g_world.airports.empty() ? 0 : std::clamp(freeAirport, 0, (int)g_world.airports.size() - 1);
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  hiveCombat.reset(1, combatWrap());
  paused = showMap = showRadio = settingsFromPause = false;
  confirmNew = confirmRes = false;
  atc.cancel(); commsPending.clear(); toasts.clear();
  screen = SCR_FREE_FLIGHT;
}

void Game::cancelFreeFlightSetup() {
  if (screen != SCR_FREE_FLIGHT) return;
  freeRegionMap = false;
  if (!screenWorldFor(SCR_MENU).empty()) { loadFreeRegion(0, SCR_MENU); return; }   // (the menu stands in the islands, or the career's region)
  screen = SCR_MENU;
  // No hub return/save path: opening or closing this mode cannot create a career save.
}

Contract Game::freeFlightContract() const {
  Contract c;
  c.id = "FREE_FLIGHT"; c.title = g_world.procedural && freeRegion >= 0 && freeRegion < (int)freeRegions.size() ? "Free Flight / " + freeRegions[freeRegion].name : "Free Flight"; c.type = CT_FERRY;
  c.from = c.to = freeAirport; c.startAirborne = freeAirborne;
  c.brief = "Fly anywhere. Full tanks, no fees, and no career progress or penalties.";
  c.wx = Weather(); c.wx.timeOfDay = 12.f; c.wx.windSpeed = 3.f;
  c.wx.gust = 0; c.wx.turbulence = 0.05f; c.wx.cloudCover = 0.15f; c.wx.visibility = 60000.f;
  return c;
}

void Game::launchFreeFlight() {
  if (screen != SCR_FREE_FLIGHT && !freeFlight) return;
  if (freeFlight && !flightRegion.id.empty()) chooseFreeRegion(flightRegion);
  if (!freeRegions.empty() && (freeRegion < 0 || freeRegion >= (int)freeRegions.size() ||
      freeRegions[freeRegion].id != g_world.regionId)) return;
  // Validate at the boundary too: a stale selection or direct call must never unlock research.
  if (!isCareerAircraft(freeCraft) ||
      freeAirport < 0 || freeAirport >= (int)g_world.airports.size()) return;
  if (!freeFlight && quoteJob.valid()) {
    if (quoteJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      // Initial launch preparation may wait; an already flying Earth session never does.
      freeLaunchPending = true; loadFreeRegion(freeRegion); return;
    }
    try { quoteJob.get(); } catch (...) {}
    quoteFlown.clear(); menuPlanCache = menuPlan::Cache{};
  }
  const AircraftSpec& s = kAircraft[freeCraft];
  // (the flight's setup and its first scene behind the loading card: Game::queueLaunch)
  if (queueLaunch(freeFlightContract(), freeCraft, LAUNCH_FREE, [this] { launchFreeFlight(); })) return;
  Contract c = freeFlightContract();
  Career::LaunchPlan p;
  p.spec = freeCraft; p.src = Career::SRC_NONE; p.startAirport = freeAirport; p.fuelLoadKg = s.maxFuel;
  const float careerFuelChoice = launchFuelKg;
  startFlight(c, freeCraft, Career::SRC_NONE, &p);
  launchFuelKg = careerFuelChoice;
  freeFlight = isolatedFlight = true;
  flightRegion = freeRegions.empty() ? procedural::solaceDescriptor() : freeRegions[freeRegion];
  // Every Free Flight departure participates in Earth travel: fly off the islands and on into the next region.
  earthFlight = true;
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  failPlan = FailPlan();
  if (freeAirborne) {
    const Airport& a = g_world.airports[freeAirport];
    vec3 pos = plane.pos + plane.forward() * 1500.f;
    pos.y = std::max(a.elev, g_world.height(pos.x, pos.z)) + 900.f;
    plane.reset(&s, pos, plane.heading(), s.maxFuel, 85.f, true, s.cruise);
    settleAirborneStart();
    engineAutoStarted = takeoffAnnounced = true;
    camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  }
  earthPosition = regionTravel::Chart(flightRegion.origin).position(plane.pos);
  toasts.clear();
  toast(std::string(s.name) + " / FREE FLIGHT", vec3(0.6f, 1.f, 0.8f), false);
  toast(freeAirborne ? "Already airborne. Fly anywhere, then return from the pause menu." : "Full tanks. Release the parking brake when ready.", vec3(0.8f, 0.9f, 1.f));
}

void Game::returnToFreeFlight() {
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  retireEarthPrefetch();
  if (!flightRegion.id.empty()) chooseFreeRegion(flightRegion);
  earthFlight = earthCourseHold = false; earthDestination.reset(); freeRegionMap = freeEarthPicker = false; earthRouteIdent.clear();
  freeFlight = isolatedFlight = researchFlight = militaryFlight = false;
  hiveCombat.reset(1, combatWrap());
  paused = showMap = showRadio = settingsFromPause = false;
  dbgCam = dbgFollow = false;
  atc.cancel(); commsPending.clear(); toasts.clear();
  screen = SCR_FREE_FLIGHT;
}

// Return a service loaner without leaving weapon effects or damaged scenery behind on the civilian hub.
// Closing an unflown attempt is saved transactionally; a failed save remains pending and blocks another launch.
void Game::returnToMilitary() {
  pendingMilitaryLaunch.reset();
  militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
  if (career.military.activeAttempt && !commitBlocked())
    commit([](Career& k) { k.abandonMilitaryAttempt(); });
  militaryFlight = militarySettled = researchFlight = freeFlight = isolatedFlight = false;
  paused = showMap = showRadio = settingsFromPause = false;
  crashed = completed = landed = false; dbgCam = dbgFollow = false;
  hiveCombat.reset(1, combatWrap()); combatLoadout.reset(); militaryHull = 100;
  combatCollisionWarm = false; combatCollisionWarmMs = 0; combatPrefetchCursor = 0;
  somnus = SomnusState(); wreck.clear(); debris.clear(); pops.clear(); particles.clear(); pits.clear();
  bursts.clear(); trail.clear(); cloudWake.clear(); tipTrail[0].clear(); tipTrail[1].clear();
  for (auto& smoke : pieceTrail) smoke.clear();
  tipOn = airBreak = false; trailT = 0; wakeGap = true; wakeOdo = 0;
  craterR = craterD = 0; boomT = -1; g_groundPits.clear(); g_scenery.craters.clear(); g_scenery.resetDamage();
  traffic.reset(); atc.cancel(); commsPending.clear(); toasts.clear();
  g_ren.entBudgetMs = 2.5f;
  source = Career::SRC_NONE; screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 2;
  in.pressed[K_ESC] = in.pressed[K_ENTER] = in.pressed[' '] = false;
  in.mPressed[0] = false; in.buttonsPressed &= ~(PAD_A | PAD_B | PAD_START);
}

void Game::init(bool buildWorld, const std::function<void(float, const std::string&)>& progress) {
  const float units = float(kNumAircraft + 4);
  auto report = [&](int done, const std::string& detail) { if (progress) progress(done / units, detail); };
  report(0, "Preparing career systems");
  if (buildWorld) g_world.build();
  buildStory();
  loadSettings();
  applyUiPalette();
  wantPacing = true;   // (the frame-rate target from the settings)
  loadStations();
  report(1, "Settings, story and radio stations ready");
  {   // every type's performance, learned by flying it, on threads side by side (the job board needs the career ones now);
    // kept with the cache, so a launch after the first reads them
    const std::string pf = cacheDir.empty() ? std::string() : joinPath(cacheDir, "perf.bin");
    if (pf.empty() || !Plane::perfLoad(pf, buildStamp)) {
      std::vector<std::thread> th;
      std::atomic<int> completed{0};
      for (int i : kCareerAircraft) th.emplace_back([i, &completed] { Plane::perf(&kAircraft[i]); completed.fetch_add(1); });
      if (progress) {
        while (completed.load() < kNumAircraft) {
          const int n = completed.load();
          report(1 + n, fmt("Flight performance: %d / %d airframes checked", n, kNumAircraft));
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
      }
      for (auto& t : th) t.join();
      if (!pf.empty()) Plane::perfSave(pf, buildStamp);
    }
  }
  report(1 + kNumAircraft, "Aircraft performance data ready");
  career.newGame();
  std::string sav = joinPath(saveDir, "career.sav");
  hasSave = !diskless && career.load(sav);   // (a tool's flights never touch the player's career: a fresh one, in memory)
  if (!hasSave && !diskless && career.load(sav + ".bak")) { hasSave = true; toast("Career save was damaged: restored the previous save"); }
  if (hasSave && career.attemptOpen) {   // the last session ended inside a flight: the career stands as it was before it
    toast(fmt("Your last flight was interrupted: you are back at %s", careerPlaceName().c_str()), vec3(1.f, 0.8f, 0.4f));
    // an accepted job whose leg never ended waits where the leg began (the hub's recovery card offers it again)
    commit([](Career& k) { if (k.military.activeAttempt) { k.abandonMilitaryAttempt(); return; } k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
  }
  if (!hasSave) {
    career.newGame();
    // keep an unreadable save aside rather than overwriting it with a new career
    if (FILE* f = diskless ? nullptr : fopen(sav.c_str(), "r")) { fclose(f); remove((sav + ".damaged").c_str()); rename(sav.c_str(), (sav + ".damaged").c_str()); toast("Career save couldn't be read: kept as career.sav.damaged"); }
  }
  loadProfile();
  report(2 + kNumAircraft, "Career and recovery checks complete");
  radio.init();
  radio.setVolume(radioLevel()); radioSentVol = radioLevel();
  if (!diskless) g_music.start((float)g_audio.sampleRate);   // (the game's own score: music.h)
  report(3 + kNumAircraft, "Radio initialized");
  atc.load(assetDir + "/voice");   // tower voices (absent: the towers stay silent)
  camQ = quat();
  report(4 + kNumAircraft, "Voice library indexed");
}

void Game::initHeadless() {
  headless = true;
  // Multi-scene diagnostics can start a fresh Game while another generated region
  // is resident: a fresh career there (its board drawn when the hub opens).
  if (g_world.procedural) { career = Career{}; career.region = g_world.regionId; career.location = 0; }
  else career.newGame();
  set.resMode = 0; failuresArmed = false; g_ren.foveation = 0.f; g_ren.prepassAutoOn = false;
}   // tools and tests render at the full resolution; nothing breaks unless the test breaks it

// ------------------------------------------------------------------ failures (C7)
void Game::rollFailures(const Contract& c, int spec, Career::Source src) {
  failPlan = FailPlan();
  if (!failuresArmed || !set.failures || (c.startAirborne && c.type == CT_LESSON)) return;   // (Settings: random failures off)
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
// The job types' live records (C6): the medevac patient and the VIP's comfort drift down with g (turbulence's bumps
// among it), bank and a firm touchdown; the survey counts its time in the altitude band between the first and the last
// checkpoint; the low-vis run checks the alignment the first time the aircraft comes below minimums near the
// destination (a go-around afterwards clears it); night freight notes the landing light at touchdown.
void Game::updateJobMeters(float dt, float gs) {
  (void)gs;
  const Contract& c = contract;
  if (researchFlight || isolatedFlight || militaryFlight || crashed) return;
  bool air = !plane.onGround && takeoffAnnounced;
  if (c.type == CT_MEDEVAC || c.type == CT_VIP) {
    float& m = c.type == CT_MEDEVAC ? result.patient : result.comfort;
    float gLim = c.type == CT_MEDEVAC ? 1.5f : 1.3f, bankLim = 30.f;
    float rate = 0;
    if (air) {
      rate += std::max(0.f, plane.gLoad - gLim) * 0.08f + std::max(0.f, (c.type == CT_MEDEVAC ? 0.6f : 0.75f) - plane.gLoad) * 0.08f;
      rate += std::max(0.f, fabsf(plane.bankDeg()) - bankLim) * 0.003f;
      // (turbulence costs what its gusts put on the aircraft - the g above - and no more: a flat charge for the air
      // itself drained the meter however gently the aircraft was flown, review CAR-1)
    }
    if (plane.ev.touchdown && fabsf(plane.ev.touchdownVs) * 196.85f > 300.f) m -= (fabsf(plane.ev.touchdownVs) * 196.85f - 300.f) / 1500.f;
    m = clampf(m - rate * dt, 0.f, 1.f);
  }
  if (c.type == CT_SURVEY && !c.wps.empty() && air && wpIndex > 0 && wpIndex < (int)c.wps.size()) {
    surveyT += dt; if (fabsf(plane.pos.y - c.wps[0].alt) <= 46.f) surveyInT += dt;
    result.surveyInBand = surveyT > 1.f ? surveyInT / surveyT : 1.f;
    result.surveySec = surveyT; result.surveyInSec = surveyInT;   // (the job's whole: a continued leg starts from its earlier legs' - applyJobLeg)
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
    case RS_CLOAK: inst = somnus.stealth >= 0.95f; break;
    case RS_LASER: inst = somnus.shots >= (int)st.v; break;
    case RS_BOMB: inst = somnus.dropped >= (int)st.v; break;
    case RS_LAND: { float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA); inst = plane.onGround && touchedDown && gs < 2.f && ap == resAirport && dA < g_world.airports[ap].length * 0.5f + 600.f; } break;
  }
  resHold = inst ? resHold + dt : 0.f;
  if (!inst || resHold < st.hold) return;
  resStep++; resHold = 0;
  if (resStep >= C.n) {
    resCardDone = true; resDone.insert(C.id);
    // (signed off only when it is on record: a settings file that can't be written says so - the review of v3.44.0, CAR-10)
    if (saveSettings()) toast(fmt("TEST CARD %s COMPLETE - %s SIGNED OFF", C.id, C.title), vec3(0.5f, 1.f, 0.6f));
    else toast(fmt("TEST CARD %s COMPLETE - SIGNED OFF THIS SESSION ONLY: THE RECORD COULD NOT BE SAVED", C.id), vec3(1.f, 0.6f, 0.4f));
    g_audio.trigger(SFX_SUCCESS);
  } else { toast(fmt("STEP %d of %d: %s", resStep + 1, C.n, C.steps[resStep].label), vec3(0.9f, 0.9f, 0.6f)); g_audio.trigger(SFX_CHIME, 0.8f); }
}
// The clouds move with the wind (weather.h): the field drifts at twice the surface wind, its fine detail a little
// faster still (the wisps stream off the clouds' downwind edges) and the billows rise through it, quickly in a
// sunny day's cumulus, hardly at all in a grey overcast. wx carries them, so the bumps, the rain and the icing are
// where the clouds are drawn.
void Game::moveClouds(float dt) {
  // (the offsets are added to the position the field is read at, so the clouds move against them: adding the wind
  // had them drifting into it)
  const vec3 d(-sinf(wx.windFrom * DEG), 0, cosf(wx.windFrom * DEG));
  cloudOff = cloudOff - vec2(d.x, d.z) * (wx.windSpeed * 2.f * dt);
  cloudDet += (vec3(0, 1.4f, 0) - d * (wx.windSpeed * 0.6f)) * dt;
  const float cu = smoothstepf(0.08f, 0.22f, wx.cloudCover) * smoothstepf(0.9f, 0.6f, wx.cloudCover);
  cloudBoil += (0.3f + 1.4f * cu + (wx.storm ? 2.f : 0.f)) * dt;
  wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
}
Game::WakePt Game::wakeHere(vec3 c, bool brk, float odo) const {
  // the pair the airframe's surfaces are rolling up (aero_wake.h), which sinks at w0 = G / (2 pi b0) under its own
  // downwash and slows as it decays over a few times b0 / w0. Without one (too slow to fly), a lifting line's: the
  // weight times the load shed round two cores pi/4 of the span apart
  const AeroWake& wk = plane.wake;
  float gam = wk.gamPair, b0 = wk.b0, py = wk.pairY;
  if (b0 < 0.1f) {
    b0 = 0.25f * PI * plane.spec->span;
    const float rho = 1.225f * powf(std::max(1.f - 2.2558e-5f * plane.pos.y, 0.3f), 4.2559f);
    gam = plane.mass() * G0 * clampf(plane.gLoad, -6.f, 12.f) / (rho * std::max(plane.airspeed, 20.f) * b0);
    py = plane.spec->wingY;
  }
  const float w0 = gam / (2.f * PI * b0);
  // the bank: the airframe's x axis against the path's level across, seen from behind
  const vec3 f = normalize(plane.vel - plane.windVel + vec3(0, 0, 1e-4f)), sd = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-5f, 0, 0)), upv = cross(sd, f);
  const vec3 bx = plane.q.rotate(vec3(1, 0, 0));
  return {c, 0.f, brk, w0, clampf(4.f * b0 / std::max(fabsf(w0), 0.05f), 20.f, 120.f), odo, gam, b0, atan2f(dot(bx, upv), dot(bx, sd)), py};
}
// The aircraft came back in over the map's seam (Plane::step, world.h WRAP_HALF): what the flight has put in the world
// round it comes with it - the camera, the smoke and the sparks, the wreckage, the wingtip ribbons, its path through the
// cloud, the traffic flying with it, the renderer's last frame - so nothing jumps. What belongs to a place (the
// craters, the fields' own traffic, the route) stays: across the seam the map is the same.
void Game::followSeam(vec3 d) {
  g_audio.shiftWorld(d); audioCameraPrevious += d;
  camPos += d; dbgCamPos += d; dbgCamLook += d;
  for (auto& p : particles) p.p += d;
  for (auto& w : wreck) w.c += d;
  for (auto& b : debris) b.p += d;
  for (auto& p : pops) p.p += d;
  for (auto& tr : pieceTrail) for (auto& t : tr) { t.p += d; t.sw.c += d; }
  for (auto& tr : tipTrail) for (auto& t : tr) { t.p += d; t.sw.c += d; }
  boomP += d;
  for (auto& w : cloudWake) w.c += d;
  ufo.pos += d;
  for (auto& b : somnus.bolts) b.h += d;
  for (auto& b : somnus.bombs) b.p += d;
  for (auto& b : somnus.blasts) b.p += d;
  somnus.cam.pos += d; somnus.cam.look += d; somnus.cam.blastP += d;
  traffic.followSeam(d);
  g_ren.followSeam(d);
}

// The aircraft through the cloud: its path in the cloud layer is kept (a point every 150 m, or sooner in a turn) in
// the cloud field's frame, with the vortex pair as it was there, and the cloud pass stirs and clears a channel along
// it that opens a moment behind the aircraft, widens and fills in again over the next minute (FrameParams::wake). Flying in or near cloud, wisps of it stream
// past (their parallax is the speed you feel in cloud), and the wingtips' vortices draw ribbons through it.
void Game::updateCloudWake(float dt) {
  // (the rain and the cloud at the aircraft, smoothed: the windscreen, the rain round the camera, the visibility, the drumming on the airframe)
  rainNow = flightClock < 0.1f ? plane.wxl.rain : approach(rainNow, plane.wxl.rain, 1.2f, dt);   // (raining already as the flight opens)
  float mist = clampf(plane.wxl.cloud * 2.f, 0.f, 0.8f) * smoothstepf(5.f, 30.f, plane.airspeed);
  mistNow = approach(mistNow, mist, mist > mistNow ? 3.f : 0.4f, dt);
  for (auto& w : cloudWake) w.age += dt;
  while (!cloudWake.empty() && cloudWake.front().age > 75.f) cloudWake.erase(cloudWake.begin());
  const float top = wx.cloudBase + wxfield::cloudThickness(wx);
  const bool inLayer = wx.cloudCover >= 0.02f && !crashed && plane.spec && plane.airspeed > 12.f && plane.pos.y > wx.cloudBase - 80.f && plane.pos.y < top + 80.f;
  const vec3 c = plane.pos + vec3(cloudOff.x, 0, cloudOff.y);
  if (inLayer) {
    bool push = cloudWake.empty();
    if (!push) {
      const WakePt& L = cloudWake.back();
      float dd = length(c - L.c);
      bool turned = false;
      if (cloudWake.size() >= 2 && !L.brk && dd > 1.f) { const WakePt& P = cloudWake[cloudWake.size() - 2]; turned = dot(normalize(c - L.c), normalize(L.c - P.c)) < 0.995f; }
      push = dd > 150.f || (dd > 30.f && turned) || L.age > 2.5f;
    }
    if (push) {
      if (!wakeGap && !cloudWake.empty()) wakeOdo += length(c - cloudWake.back().c);
      cloudWake.push_back(wakeHere(c, wakeGap, wakeOdo));
      wakeGap = false;
    }
    while (cloudWake.size() > (size_t)FrameParams::kWakeMax - 1) cloudWake.erase(cloudWake.begin());
  } else wakeGap = true;
  // wisps: the cloud just ahead, as puffs that stay in the air and stream past
  if (!plane.spec || crashed || plane.onGround) return;
  const float ahead = std::max(plane.wxl.cloud, wxfield::cloudDensity(wx, plane.pos + plane.vel * 0.7f, true));
  if (ahead > 0.02f && plane.airspeed > 15.f) {
    wispAccum += length(plane.vel) * dt * std::min(ahead * 3.f, 1.f) / 9.f;   // (one every 9 m flown in thick cloud)
    const float span = plane.spec->span;
    Rng& r = sparkRng;
    vec3 f = normalize(plane.vel), sd = normalize(cross(f, vec3(0, 1, 0))), upv = cross(sd, f);
    for (; wispAccum >= 1.f; wispAccum -= 1.f) {
      float ang = r.range(0.f, 2.f * PI), rad = span * r.range(0.45f, 2.6f);
      vec3 p = plane.pos + f * r.range(25.f, 70.f) + (sd * cosf(ang) + upv * sinf(ang)) * rad;
      float dens = wxfield::cloudDensity(wx, p, true);
      if (dens < 0.02f) continue;
      float g = r.range(0.86f, 0.97f);
      const size_t n0 = particles.size();
      spawn(p, plane.windVel, r.range(1.6f, 2.6f), r.range(4.f, 9.f), r.range(1.5f, 3.f), vec3(g, g, g * 1.02f), clampf(0.04f + dens * 0.14f, 0.f, 0.16f), SPR_SMOKE, 0.f, 0.f);
      if (particles.size() > n0) particles.back().air = true;   // (the airframe's flow carries it: updateParticles)
    }
  } else wispAccum = 0;
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
        toast("Autopilot: the wind has shifted - unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
        toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      } else if (plane.apRev != was) toast(fmt("Autopilot: the wind has shifted - now runway %02d at %s", A.rwyNumber(plane.apRev), A.code), vec3(0.6f, 1, 0.6f));
    }
  }
}
// your airline's scheduled flights (C11): when a route's line passes within 15 km of you and none of its flights
// is in the air, one appears on the line a way off and flies on to the route's destination in the company livery
void Game::airlineTraffic(float dt) {
  airlineTrafficT -= dt;
  if (airlineTrafficT > 0 || researchFlight || freeFlight || career.airline.routes.empty() || !traffic.enabled || plane.onGround) return;
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
  else if (kind == TR_FORMATION) c.brief = fmt("Take off from %s and climb above 300 m: the Nocturne display pair joins you and flies its show around you for three minutes. Hold a steady platform for them - wings near level, no yanking, no diving at the ground - and land back here when they bow out. Score: the seconds you spent out of a steady platform. Lower is better.", A.name);
  else if (kind == TR_STOL) c.brief = "Summit Pass, 600 m of gravel at 5,400 ft. Take off, come round and land as short as you can: the score is the landing roll from touchdown to a stop. Lower is better. The Scrubrunner is the natural choice.";
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
std::string Game::trialBoard(int kind, int spec) const {
  if (kind == TR_DAILY) {   // (the same course for everyone that day, in whatever aircraft)
    char d[16] = ""; const time_t t = time(nullptr);
    if (const tm* g = gmtime(&t)) strftime(d, sizeof d, "%Y-%m-%d", g);
    return std::string(trialName(kind)) + " - " + d;
  }
  return std::string(trialName(kind)) + " - " + kAircraft[std::clamp(spec, 0, kAircraftCount - 1)].name;
}
std::string Game::trialKey(int kind) const {
  if (kind != TR_DAILY) return trialId(kind);
  return fmt("%s.%u", trialId(kind), (unsigned)(time(nullptr) / 86400));   // (today's course: its own board)
}
void Game::trialMedalMarks(int kind, int spec, float m[3]) const {
  const AircraftSpec& s = kAircraft[std::clamp(spec, 0, kAircraftCount - 1)];
  if (kind == TR_SPOT) { m[0] = 25.f; m[1] = 50.f; m[2] = 90.f; return; }
  if (kind == TR_FORMATION) { m[0] = 3.f; m[1] = 10.f; m[2] = 25.f; return; }
  if (kind == TR_STOL) { const float r = std::max(s.runwayM, 150.f); m[0] = r * 0.5f; m[1] = r * 0.75f; m[2] = r * 1.1f; return; }
  // the gate runs: the course from the first gate to the last at the type's cruise speed
  const Contract c = trialContract(kind);
  float len = 0.f;
  for (size_t i = 1; i < c.wps.size(); i++) len += length(vec3(c.wps[i].x - c.wps[i - 1].x, c.wps[i].alt - c.wps[i - 1].alt, c.wps[i].z - c.wps[i - 1].z));
  const float t = len / std::max(s.cruise, 20.f);
  m[0] = t; m[1] = t * 1.2f; m[2] = t * 1.5f;
}
int Game::trialMedal(int kind, int spec, float score) const {
  if (score < 0.f) return 0;
  float m[3]; trialMedalMarks(kind, spec, m);
  return score <= m[0] ? 3 : score <= m[1] ? 2 : score <= m[2] ? 1 : 0;
}
static std::string ghostPath(const std::string& dir, const std::string& key) { return (dir.empty() ? std::string() : dir + "/") + "ghost_" + key + ".bin"; }
void Game::loadGhost(int kind) {
  ghostBest.clear();
  if (diskless || (headless && saveDir.empty()) || (kind != TR_GATES && kind != TR_DAILY)) return;
  FILE* f = fopen(ghostPath(saveDir, trialKey(kind)).c_str(), "rb");
  if (!f) return;
  uint32_t n = 0;
  if (fread(&n, 4, 1, f) == 1 && n > 0 && n < 200000) {
    ghostBest.resize(n);
    if (fread(ghostBest.data(), sizeof(vec3), n, f) != n) ghostBest.clear();
  }
  fclose(f);
}
void Game::saveGhost(int kind) {
  if (diskless || (headless && saveDir.empty()) || ghostRec.empty()) return;
  if (kind == TR_DAILY) {   // (yesterday's course's ghost goes with it)
    const unsigned day = (unsigned)(time(nullptr) / 86400);
    for (unsigned d = day - 7; d < day; d++) remove(ghostPath(saveDir, fmt("%s.%u", trialId(kind), d)).c_str());
  }
  FILE* f = fopen(ghostPath(saveDir, trialKey(kind)).c_str(), "wb");
  if (!f) return;
  const uint32_t n = (uint32_t)ghostRec.size();
  fwrite(&n, 4, 1, f); fwrite(ghostRec.data(), sizeof(vec3), n, f);
  fclose(f);
}
void Game::startTrial(int kind, int spec, int src) {
  if (g_world.procedural) return;   // (the trials' courses are the islands')
  startFlight(trialContract(kind), spec, (Career::Source)src); isolatedFlight = true;   // (a trial: off the books)
  trialResult = TrialResult(); trialResult.kind = -1;
  trialSplits.clear(); ghostRec.clear(); loadGhost(kind);
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
  TrialResult& R = trialResult;
  R = TrialResult(); R.kind = kind; R.spec = specIdx; R.src = (int)source; R.score = score; R.splits = trialSplits;
  const std::string key = trialKey(kind);
  R.bestSplits = trialBestSplits.count(key) ? trialBestSplits[key] : std::vector<float>();
  if (score < 0) R.why = result.failReason.empty() ? (success ? "The run didn't count: land back on the runway to finish" : "Did not finish") : result.failReason;
  std::string msg = std::string(trialName(kind)) + ": " + trialScore(kind, score);
  if (score >= 0) {
    auto& L = trialBest[key];
    L.push_back(score); std::sort(L.begin(), L.end()); if (L.size() > 5) L.resize(5);
    int rank = 1; for (float v : L) { if (v < score) rank++; }
    R.rank = rank; R.best = L[0]; R.newBest = rank == 1;
    msg += rank == 1 ? "  -  a new best!" : fmt("  -  rank %d (best %s)", rank, trialScore(kind, L[0]).c_str());
    if (R.newBest) { if (!trialSplits.empty()) trialBestSplits[key] = trialSplits; saveGhost(kind); }
    // the medal, the profile's best of it for this trial, the achievements
    R.medal = trialMedal(kind, specIdx, score);
    if (R.medal > 0) {
      const std::string mk = std::string("medal.") + trialId(kind);
      if (R.medal > profile.count(mk)) profile.num[mk] = (float)R.medal;
      award("medal");
      if (R.medal == 3) award("gold");
      bool all = true; for (int k = 0; k < TR_COUNT; k++) all &= profile.count(std::string("medal.") + trialId(k)) >= 3.f;
      if (all) award("gold_all");
      saveProfile();
      const char* names[] = {"", "BRONZE", "SILVER", "GOLD"};
      msg += std::string("  -  ") + names[R.medal];
    }
    saveSettings();
    if (!botControl && !diskless) {   // the worldwide board on Steam (the times in milliseconds, the rest whole)
      R.board = trialBoard(kind, specIdx);
      const bool ms = kind == TR_GATES || kind == TR_DAILY || kind == TR_FORMATION;
      steam::submitScore(R.board, ms, (int)lroundf(ms ? score * 1000.f : score));
    }
  }
  if (g_music.owns()) g_music.sting(R.medal > 0 ? STING_MEDAL : score >= 0 ? STING_SUCCESS : STING_FAIL);
  hubMsg = msg; hubMsgTime = 8;
}
void Game::fireFailure(int kind, int engine) {
  if (!plane.failNow(kind, engine)) return;
  if (timeAccel > 1.f) timeAccel = 1.f;   // (a failure is flown at real time)
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
  // (in the cloud that is drawn there, weather.cpp, or in its rain)
  bool freezing = wx.precip == 2 || plane.pos.y > 2300.f;
  bool inCloud = plane.wxl.cloud > 0.05f;
  bool wet = inCloud || plane.wxl.rain > 0.05f;
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

Game::~Game() {
  regionalSurface::clearFlightContext();
  if (earthPrefetchCancel) earthPrefetchCancel->requested = true;
  // The loader captures this for its stage flag. Join before member destruction,
  // even when the player closes the window halfway through generation.
  if (regionJob.valid()) { try { regionJob.get(); } catch (...) {} }
  if (earthPrefetchJob.valid()) { try { earthPrefetchJob.get(); } catch (...) {} }
  if (earthRetireJob.valid()) { try { earthRetireJob.get(); } catch (...) {} }
  g_sceneryProfiles.reset();   // (the finals profiled for this game's flights: their worker reads the world)
}

void Game::shutdown() {
  regionalSurface::clearFlightContext();
  retireEarthPrefetch();
  if (earthRetireJob.valid()) { try { earthRetireJob.get(); } catch (...) {} }
  if (regionJob.valid()) { try { regionJob.get(); } catch (...) {} }
  g_sceneryProfiles.reset();
  saveSettings(); radio.shutdown(); g_music.own(false); g_music.stop(); g_ren.releaseLoadingPreview();
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
  if (F.flapAsym) ann.push_back({fmt("FLAP ASYMMETRY  left stuck at %.0f%%: keep the lever there", F.flapAt / std::max(aeroGeom(spc).flapMax, 0.05f) * 100.f), "flaps", "asym", false});
  if (F.ice > 0.05f) ann.push_back({fmt("ICING %.0f%%  leave the cloud, keep speed", F.ice * 100.f), "ice", F.ice > 0.5f ? "severe" : "icing", F.ice > 0.5f});
  return ann;
}

void Game::callout(const std::string& s) { if (screen == SCR_FLIGHT || screen == SCR_LOADING) commsPending.push_back({s, contract.id}); }
void Game::toast(const std::string& s, vec3 col, bool voiced) {
  toasts.push_back({s, 0.f, col});
  if (voiced && (screen == SCR_FLIGHT || screen == SCR_LOADING)) commsPending.push_back({s, contract.id});
  if (toasts.size() > 5) toasts.erase(toasts.begin());
}

// ------------------------------------------------------------------ sun & sky
void Game::computeSun(float tod, vec3& dir, vec3& col, float& night, float h, float* dim) const {
  dir = sunDirectionAt(tod);   // (its path: sky.h)
  float y = dir.y;
  // the sunlight reaching h up (common.glsl sunLightAt, exactly): through the air above it, and from up there over a
  // horizon that has dipped below the level - an aircraft high over the dusk is still in the sun, and above the air
  // it is the sun's own white
  h = std::max(h, 0.f);
  const float R = planetRadius(), dens = expf(-h / 8000.f), dip = R > 0.f ? sqrtf(std::max(1.f - (R / (R + h)) * (R / (R + h)), 0.f)) : 0.f;
  const float lowest = R > 0.f ? (R + h) * sqrtf(std::max(1.f - y * y, 0.f)) - R : h;
  const float k = y >= 0.f ? dens : 2.f * expf(-std::max(lowest, 0.f) / 8000.f) - dens;
  float od = k / std::max(y * 1.4f + 0.08f, 0.02f);   // optical depth: positive and bounded (x50) once the sun is down
  col = vec3(expf(-0.10f * od * 0.45f), expf(-0.23f * od * 0.45f), expf(-0.56f * od * 0.45f)) * smoothstepf(-0.06f, 0.05f, y + dip);
  const float weather = (1.f - 0.75f * wx.cloudCover * wx.cloudCover) * (wx.storm ? 0.6f : 1.f);
  col = col * weather;
  if (dim) *dim = weather;
  night = smoothstepf(0.06f, -0.14f, y);
}

// ------------------------------------------------------------------ flight session
void Game::startFlight(const Contract& c, int spec, Career::Source src, const Career::LaunchPlan* finalized) {
  // All military entry points, including direct starts and pause retries, require
  // the already committed, unsettled career attempt. UI visibility is not access control.
  if (isMilitaryContract(c.type) || src == Career::SRC_MILITARY) {
    const auto& m = career.military;
    if (src != Career::SRC_MILITARY || !isMilitaryContract(c.type) || commitBlocked() ||
        !career.attemptOpen || !m.activeAttempt || m.activeAttempt != career.attempt ||
        m.activeAttempt <= m.lastSettledAttempt || m.activeKind != c.type-CT_MIL_RECON ||
        m.activeAirport != c.from || career.canFly(c,spec) != Career::SRC_MILITARY) return;
  }
  static bool dispWarned = false;   // say once if the cockpit display shader could not be built on this GPU
  if (!g_ren.dispError.empty() && !dispWarned && !headless) { dispWarned = true; toast("Cockpit display shader failed on this GPU (details in startup.log)", vec3(1.f, 0.45f, 0.35f)); }
  regionalSurface::clearFlightContext(); earthFacilitiesPending = earthPromotionDeferred = false;
  haul = Haul{};   // (a long haul's own state follows its start: beginHaul)
  contract = c; specIdx = spec; source = src;
  launchPlan = finalized ? *finalized : finalizeLaunchPlan(c, spec, src);   // accepted quote, tank load and bill agree
  militaryFlight=isMilitaryContract(c.type); militarySettled=false; militaryHull=100; militarySiteLabels={};
  hiveCombat.reset(1, combatWrap()); combatLoadout.reset();
  combatCollisionWarm=false; combatCollisionWarmMs=0; combatPrefetchCursor=0;
  researchFlight = freeFlight = false;   // isolated launchers set their own mode after the shared reset
  wx = c.wx; timeOfDay = wx.timeOfDay; apRepickT = 0;
  if ((set.air == 0 || calmRetry) && !botControl) {   // (Settings: calm air - a third of the gusts and the turbulence, the fronts' too; a lesson retried in calm air, this once)
    wx.gust *= 0.35f; wx.turbulence *= 0.35f;
    if (calmRetry) { wx.windSpeed = std::min(wx.windSpeed, 4.f); wx.gust = 0.f; }
    if (contract.wxShift) { contract.wxEnd.gust *= 0.35f; contract.wxEnd.turbulence *= 0.35f; }
  }
  calmRetry = false;
  wxStart = wx;
  wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
  cloudWake.clear(); wakeGap = true; wakeOdo = 0; wispAccum = 0; rainNow = mistNow = 0;
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
  combatBasePayload=payloadKg;
  float fuel = launchPlan.fuelLoadKg;
  launchFuelKg = -1;
  g_audio.resetWorld(); ++audioSceneId; audioCameraValid=false; audioPlayerCone=-1e9f; audioBoomCooldown=0; audioMachArmed=true;
  audioThreatAt=audioObjectiveAt=-100; audioExtractSaid=false; commsEndScreen=-1;
  plane.reset(&s, start, hdg, fuel, payloadKg, c.startAirborne, s.cruise);
  plane.apComfort = c.gentle();   // gentle for passengers or a fragile load, else the airframe's whole envelope (the stick is never limited)
  fuelStart = plane.fuel;
  rollFailures(c, spec, src);
  isolatedFlight = false; jobLeg = false; jobClockBase = 0; attemptFrom = c.from;
  wpIndex = 0; flightClock = 0; crashTimer = 0; endTimer = 0; airBreak = false; crashEndT = 7.5f; gTunnel = 0; gTunnelPin = -1;
  surveyT = surveyInT = 0; minimumsChecked = false; minimumsGoArounds = 0; trialT0 = trialT1 = -1; formT = formLost = 0; formDone = false;
  hudPrevIas = 0; hudTrend = 0;
  traffic.reset();
  ufo = Ufo(); ufo.next = 180.f + sparkRng.uni() * 240.f;   // first encounter after 3-7 minutes in the air
  paused = false; showMap = false; landed = completed = crashed = false;
  result = FlightResult();
  timeAccel = 1; camMode = camMode == 1 ? 1 : 0; camYaw = 0; camPitch = 0.12f; camZoom = 1; camArm = 0; camArmV = 0; camSpd = 0;
  lookYaw = 0; lookPitch = -0.13f;
  cockpitFocus = CockpitFocusZoom(); ckZoom = 1.f; // a new flight never inherits a previous display lock
  camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 3, 15));
  flapNotch = 0; phase = 0; lastHintPhase = -1; hint.clear();
  joyThrTaken = false; joyThrLast = -1;   // (the throttle lever takes over when it moves, not at the start)
  arrivalAt = -1; arrivalTold = false; engineShutDown = false; engineHold = 0; engineHeld = false; tdStampT = -1.f;
  flightAch.clear(); flightKm = 0; profileT = 0; pbOld = -1.f; pbNew = false; lastMark = LandingMark(); touchdownApplauded = false;
  trialSplits.clear(); ghostRec.clear(); ghostBest.clear();   // (a trial loads its ghost after: startTrial)
  sightReq = -1; sightAsked = sightDone = false; photoFlashT = -10.f;
  if ((c.type == CT_PAX || c.type == CT_VIP) && !c.story && c.from != c.to) {   // a landmark near the way: the passengers may ask
    const Airport& A = g_world.airports[c.from]; const Airport& B = g_world.airports[c.to];
    const auto& sites = profileSites(); float best = 12000.f;
    for (int i = 0; i < (int)sites.size(); i++) {
      const vec3& p = sites[i].pos;
      const float d = length(vec3(p.x - A.x, 0, p.z - A.z)) + length(vec3(p.x - B.x, 0, p.z - B.z)) - length(vec3(B.x - A.x, 0, B.z - A.z));
      if (d < best) { best = d; sightReq = i; }
    }
  }
  takeoffAnnounced = c.startAirborne; touchedDown = false; touchdownFpm = 0; stillTimer = 0; tdRunway = -2;   // (an airborne start has no takeoff to announce)
  engineAutoStarted = false; startDelay = 1.2f;
  parkingBrake = !c.startAirborne;   // a start on the ground is parked: the brake is released to roll
  if (!plane.onGround) settleAirborneStart();
  particles.clear(); bursts.clear(); pops.clear(); boomT = -1; for (auto& tt : pieceTrail) tt.clear(); trail.clear(); tipTrail[0].clear(); tipTrail[1].clear(); tipOn = false; trailT = 0; wreck.clear(); debris.clear(); craterR = 0; pits.clear();
  lightning = 0; nextLightning = 6; strikes.clear(); lightningGlow = 0.f;
  landingLight = true;
  approachMinAgl = 1e9f; thrPrevAlong = -1e9f; appLow = false; appHigh = false; coaching.clear();
  apWasOn = false; apDest = -1; somnus = SomnusState(); g_scenery.resetDamage();
  licenseBefore = career.license;
  // the player sees a loading screen while the scenery around the start is generated (tests fly straight away)
  screen = headless ? SCR_FLIGHT : SCR_LOADING;
  if (!loadLogging) loadLogBegin();
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadFrames = 0; loadViewsDrawn = false; loadViewsStep = 0; loadWarmStep = 0; loadWarmAt = -1; loadStableFrames = 0; loadBakeSeen = g_ren.bakeCount; loadObservedFrame = -1; loadMap = false; dbgCam = dbgFollow = false; benchPin = false;
  atc.cancel(); atcF = AtcFlight(); hintsVoiced.clear(); commsPending.clear(); lessonVoiceWarned = false;   // (the last flight's calls go before this one's announcements)
  // (a hazard call is said only while the hazard holds: a "PULL UP" queued behind the tower played at 1,958 m, climbing -
  // the review of v3.44.0, AUD-3)
  atc.valid = [this](const AtcVoice::Tx& t) { return t.key < 0 || (t.key >= kWarnKey ? t.key - kWarnKey < 4 && warnWas[t.key - kWarnKey] : t.key == atcKey()); };
  failVoiced.clear();
  toast(fmt("%s - %s", a.code, a.name), vec3(0.7f, 0.9f, 1.0f));
  toast(fmt("Runway %02d, %s", a.rwyNumber(reverse), wx.describe().c_str()), vec3(0.8f, 0.8f, 0.8f));
  atcF.dep = c.from; atcF.arr = c.to; atcF.depRev = reverse;
  if (c.startAirborne) atcF.phase = 3;
  if (militaryFlight) setupMilitaryEncounter();
}

void Game::endFlight(bool success, const std::string& reason, FlightOutcome outcome) {
  PartTimer ptEnd(*this, "flight end");
  if (militaryFlight) {
    if(militarySettled) return;
    militarySettled=true;
    success=success && hiveCombat.mission.status==hive::MissionStatus::Success && !crashed && militaryHull>0;
    result.success=success; result.outcome=success?OUT_SUCCESS:outcome; result.failReason=reason; result.flightMin=flightClock/60.f;
    result.fuelUsedKg=std::max(0.f,fuelStart-plane.fuel); result.maxG=plane.maxG; result.minG=plane.minG;
    commit([&](Career& k) { payout=k.settle(contract,specIdx,Career::SRC_MILITARY,result,&stars,&launchPlan); });
    lastSuccess=success; debriefTitle=success?"Military sortie complete":reason.empty()?"Military sortie aborted":reason;
    coaching.clear(); screen=SCR_DEBRIEF; paused=false; showMap=false; debriefT0 = realTime;
    profileFlightEnd(success);
    if (g_music.owns()) g_music.sting(success ? STING_SUCCESS : STING_FAIL); else g_audio.trigger(success?SFX_SUCCESS:SFX_FAIL);
    announceFlightEnd(success?"Military objectives secured":crashed?(reason=="Hull lost to Hive fire"?reason:"Flight ended after impact"):
      reason=="Military objective lost"?reason:"Flight ended");
    return;
  }
  if (freeFlight) {
    const bool impact=crashed;
    profileFlightEnd(success || outcome == OUT_DIVERTED || outcome == OUT_OFF_AIRPORT);
    returnToFreeFlight();
    toast(success || outcome == OUT_DIVERTED || outcome == OUT_OFF_AIRPORT ? "Free flight complete" : reason.empty() || outcome == OUT_ABANDONED ? "Back in the Free Flight hangar" : reason,
          vec3(0.6f, 0.9f, 1.f), false);
    announceFlightEnd(impact?"Flight ended after impact":"Free flight complete");
    return;
  }
  if (researchFlight) {  // research flights never touch the career: back to the research menu
    hiveCombat.reset(1, combatWrap());
    researchFlight = false; paused = false; showMap = false;
    screen = SCR_RESEARCH; resOpened = realTime;
    if (!reason.empty()) toast(reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));
    announceFlightEnd(crashed?"Flight ended after impact":"Research flight complete");
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
  { PartTimer pt(*this, "flight end: coaching"); coaching = landingCoaching(); lastMark = landingMark(result, *plane.spec); }
  if (isolatedFlight) {   // a practice flight or a trial: back to the hub, nothing settled
    profileFlightEnd(success);
    const bool trial = contract.type == CT_TRIAL;
    if (trial) finishTrial(success);
    isolatedFlight = false; paused = false; showMap = false; screen = SCR_HUB; hubTab = TAB_CONTRACTS;
    if (!trial) toast(success ? "Practice flight complete" : reason.empty() ? "Practice flight over" : reason, success ? vec3(0.6f, 1, 0.7f) : vec3(1, 0.5f, 0.4f));   // (a trial: its card says it)
    announceFlightEnd(success?"Practice flight complete":"Practice flight over");
    return;
  }
  Contract c = haul.on ? haul.job : contract;   // (a long haul settles as it was accepted)
  haul = Haul{};
  if (c.type == CT_FERRY) { c.story = false; }
  // the settlement is one transaction: the career only changes when the save succeeds; a failed save keeps the
  // settled copy pending (the debrief shows it, and offers the retry), and a retry can never pay twice
  { std::vector<PayoutLine> lines; int st = 0;
    bool jobLeg = career.job && career.job->state == Career::JobState::ACTIVE && career.job->c.id == c.id;
    bool jobGoesOn = false;
    PartTimer pt(*this, "flight end: career settled and saved");
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
  if (c.type == CT_LESSON) { if (success) lessonFails.erase(c.id); else lessonFails[c.id]++; }   // (twice running: the debrief offers calm air)
  if (debriefTitle.empty()) debriefTitle = success ? (c.type == CT_FERRY ? "Flight complete" : "Contract complete!") : (reason.empty() ? "Flight failed" : reason);
  // the profile: the landing's mark, a flight by the book, a flawless one; then the career's milestones
  if (success && result.landed) {
    if (lastMark.letter == 'S') award("grade_s");
    if (lastMark.letter == 'S' && stars == 3 && c.payout > 0) award("flawless");
    if (result.shutDownAtStand) award("by_the_book");
  }
  if (success && c.type == CT_TOUR && !c.story && result.wpDone >= (int)c.wps.size()) award("photo");
  if (success && c.id.rfind("LEG", 0) == 0) {   // a legendary contract: the pilot's, for good
    profile.num["legend." + c.id] = 1.f;
    bool all = true; for (const Contract& l : Career::legendaryContracts()) all &= profile.count("legend." + l.id) > 0.f;
    if (all) award("legend");
  }
  { PartTimer pt(*this, "flight end: profile"); profileFlightEnd(success); profileCareerCheck(); }
  if (g_music.owns()) g_music.sting(success ? STING_SUCCESS : STING_FAIL); else g_audio.trigger(success ? SFX_SUCCESS : SFX_FAIL);
  if (success && c.payout > 0) g_audio.trigger(SFX_CASH);
  debriefT0 = realTime;
  screen = SCR_DEBRIEF;
  paused = false; showMap = false;
  PartTimer pt(*this, "flight end: announced");
  announceFlightEnd(result.outcome==OUT_CHECKRIDE_FAILED?"Checkride not passed":success?"Flight complete":crashed?"Flight ended after impact":"Flight ended",coaching);
}

// One coaching point from what was recorded on the arrival: the most important thing to work on, or what went well
std::string Game::landingCoaching() const {
  const FlightResult& r = result;
  if (r.goArounds > 0 && touchedDown) return r.goArounds == 1 ? "Good call going around: a go-around is always the safe decision when an approach isn't working."
                                                               : fmt("%d go-arounds: set up earlier - on the centreline, at approach speed, by 3 km out.", r.goArounds);
  if (!result.landed || r.tdPastThrM < 0) return "";
  float vref = plane.spec->vref * MS_TO_KT, fpm = touchdownFpm;
  if (touchdownGrade(fpm) == TD_HARD) return fmt("Touchdown at %.0f fpm: hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.", fpm);
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

// a job's arrival waits at the field for the pilot to park (lessons and checkride lessons, trials and practice
// flights end at the stop - their briefings and recorded lines say so - as do the tests' bots)
bool Game::arrivalWaits() const {
  return !botControl && !isolatedFlight && contract.type != CT_LESSON && contract.type != CT_TRIAL && !researchFlight && !militaryFlight;
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
  auto d2 = [&](int i) { float dx = g_world.wrap(g_world.airports[i].x - plane.pos.x), dz = g_world.wrap(g_world.airports[i].z - plane.pos.z); return dx * dx + dz * dz; };   // (the short way round the map)
  std::sort(order.begin(), order.end(), [&](int a, int b) { return d2(a) < d2(b); });
  if (apDest < 0) apDest = haulPending() ? std::max(0, g_world.nearestAirport(plane.pos.x, plane.pos.z)) : (int)(&dest() - &g_world.airports[0]);   // first pick: the contract's destination (a long haul's not yet on this chart: the nearest)
  else {
    int k = (int)(std::find(order.begin(), order.end(), apDest) - order.begin());
    apDest = order[((k + dir) % n + n) % n];
  }
  g_audio.trigger(SFX_CLICK);
}

// What keeps the flight at real time now ("" nothing): the ground and the approach, low or steep over the terrain, the
// destination or a checkpoint close, a warning, a visitor, combat. The autopilot's cruise may warp lower and nearer
// the field than the pilot's hands (its own approach turns it off)
std::string Game::warpBlocker() const {
  if (militaryFlight || hiveCombat.aliveCount() > 0 || hiveCombat.mission.status == hive::MissionStatus::Active) return "combat runs at real time";
  if (crashed || plane.onGround) return "on the ground";
  if (plane.apOn && plane.apMode == Plane::AP_APPR && plane.apStage != Plane::APS_NAV) return "the autopilot is on its approach";
  if (plane.apOn && plane.apMode == Plane::AP_STUNT) return "aerobatics fly at real time";
  const float dd = length(vec3(g_world.wrap(plane.pos.x - dest().x), 0, g_world.wrap(plane.pos.z - dest().z)));
  if (!apCruising()) {
    if (plane.agl() < 250.f) return "too low over the ground";
    if (dd < 3500.f && !freeFlight) return "nearing the destination";
    for (float t : {5.f, 10.f, 20.f}) {   // (where the path it is on goes in the next 20 s of flight)
      const vec3 q = plane.pos + plane.vel * t;
      if (q.y < std::max(g_world.height(q.x, q.z), 0.f) + 150.f) return "terrain ahead";
    }
  }
  if (wpIndex < (int)contract.wps.size()) {
    const Waypoint& w = contract.wps[wpIndex];
    if (length(vec3(g_world.wrap(plane.pos.x - w.x), plane.pos.y - w.alt, g_world.wrap(plane.pos.z - w.z))) < 1500.f) return "checkpoint ahead";
  }
  if (plane.stallWarn > 0.6f) return "stall warning";
  if (gTunnel > 0.05f) return "high g";
  if (ufo.on) return "something is alongside";
  if (plane.spec && plane.fuel < plane.spec->maxFuel * 0.1f && !plane.spec->special) return "low fuel";
  return "";
}

bool Game::apCruising() const {
  return plane.apOn && (plane.apMode == Plane::AP_HOLD || (plane.apMode == Plane::AP_APPR && plane.apStage == Plane::APS_NAV));
}

void Game::engageAutopilot() {
  if (plane.fail.avionicsDark()) { toast("Autopilot unavailable - battery flat", vec3(1, 0.45f, 0.35f)); return; }
  if (apDest >= 0) {
    plane.apEngage(Plane::AP_NAV, apDest, wx);
    const Airport& a = g_world.airports[apDest];
    if (!plane.apOn) { toast("Autopilot: unable - " + plane.apDecline, vec3(1, 0.45f, 0.35f)); return; }   // (no power, no runway in reach)
    if (plane.apAirport >= 0 && plane.apAirport != apDest) { toast("Autopilot: " + plane.apDecline, vec3(1, 0.75f, 0.35f)); toast(fmt("Autopilot: GLIDING to %s runway %02d", g_world.airports[plane.apAirport].code, g_world.airports[plane.apAirport].rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f)); }
    else if (!plane.apDecline.empty()) toast("Autopilot: unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
    else toast(fmt("Autopilot: AUTOLAND %s runway %02d", a.code, a.rwyNumber(plane.apRev)), vec3(0.6f, 1, 0.6f));
    toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
  } else {
    plane.apEngage(Plane::AP_HOLD, -1, wx);
    if (haulPending() && plane.apOn && earthDestination) {   // a long haul: the autopilot holds the course for its destination
      earthCourseHold = true;
      if (auto heading = regionTravel::Chart(flightRegion.origin).bearing(plane.pos, haul.place)) plane.apHeading = (float)*heading;
      toast(fmt("Autopilot ON: the course for %s, %.0f km - it hands back 9 km out", haul.target.code,
        geo::surfaceDistance(earthPosition, haul.place) / 1000.), vec3(0.6f, 1, 0.6f));
    } else {
      toast("Autopilot ON: the stick trims heading and altitude, throttle sets speed", vec3(0.6f, 1, 0.6f));
      toast("Pick an AUTOLAND airport on the GPS map to fly there", vec3(0.8f, 0.8f, 0.8f));
    }
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
  // joystick, pedals and throttle lever (joystick.h): positions, each through its binding's dead zone and curve. The
  // lever has the throttle once it moves, and gives it up to a key, trigger or number key until it moves again
  bool jP = false, jR = false, jY = false, jT = false;
  padP += joyAxis(AX_PITCH, &jP); padR += joyAxis(AX_ROLL, &jR); padY += joyAxis(AX_YAW, &jY);
  const float lever = joyAxis(AX_THROTTLE, &jT);
  if (!jT) { joyThrTaken = false; joyThrLast = -1; }
  else if (joyThrLast < 0) joyThrLast = lever;   // (bound, back, or a new flight: it takes over when it moves, not before)
  const bool leverMoved = jT && fabsf(lever - joyThrLast) > (joyThrTaken ? 0.f : 0.02f);
  bool manual = fabsf(pitchIn) + fabsf(rollIn) > 0 || fabsf(padP) + fabsf(padR) > 0.3f;
  // (a refused autoland's hold too: the pilot asked for a landing, not a hold, and was told the stick takes over - the
  // review of v3.31.0, F3)
  bool apNav = plane.apOn && (plane.apMode == Plane::AP_APPR || plane.apMode == Plane::AP_STUNT || (plane.apMode == Plane::AP_HOLD && !plane.apDecline.empty()));
  // aerobatics: a figure is flown only while the pilot leaves the aircraft alone. Any flight input - a stick, a trigger,
  // a key or button for anything the aircraft does - hands it straight back, wherever the figure has got to; the view,
  // the HUD, the map, the radio and the pause don't (the stunt key itself: below)
  if (plane.apOn && plane.apMode == Plane::AP_STUNT) {
    bool touched = fabsf(pitchIn) + fabsf(rollIn) + fabsf(yawIn) + fabsf(padP) + fabsf(padR) + fabsf(padY) > 0.f || (in.pad && (in.lt > 0.1f || in.rt > 0.1f)) || (leverMoved && (!joyThrTaken || fabsf(lever - joyThrLast) > 0.01f));
    for (int a = 0; a < ACT_COUNT && !touched; a++) if (a != ACT_STUNT && a != ACT_AP && (a < ACT_CAMERA || a > ACT_ZOOM)) touched = actDown(a);   // (the autopilot key turns it all off: below)
    for (int k : {K_PGUP, K_PGDN, K_PLUS, K_MINUS, K_HOME, K_END}) touched = touched || in.down[k];
    for (int k = '0'; k <= '9'; k++) touched = touched || in.down[k];
    if (in.pad && !touched) {   // (and any other button: all but the view's, the instruments' and the pause)
      unsigned keep = PAD_START | set.padBind[ACT_STUNT] | set.padBind[ACT_AP];
      for (int a = ACT_CAMERA; a <= ACT_ZOOM; a++) keep |= set.padBind[a];
      touched = (in.buttonsPressed & ~keep) != 0;
    }
    if (touched) {
      plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
      apNav = false;
    }
  }
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
  // Flight assists (Settings): the inputs command rates and the aircraft holds its attitude (Plane::assistControl) -
  // the keyboard and the gamepad by default, every input when asked, and always in time warp; in the air only
  const bool joyFlying = jP || jR;
  // (the research craft's own fly-by-wire already takes rate commands: assisted only in the warp)
  plane.assistOn = !plane.apOn && !plane.onGround && (timeAccel > 1.f || (!plane.spec->special && (set.assist == 2 || (set.assist == 1 && !joyFlying))));
  if (!plane.apOn) {
    float tp = clampf(pitchIn + padP, -1, 1), tr = clampf(rollIn + padR, -1, 1);
    float rate = joyFlying ? 14.f : manual ? 2.8f : 5.f;   // (a joystick's position is the control's, near enough at once)
    if (plane.assistOn) {   // (the commands eased in as the keys are - a tap is a small change - and out at once)
      auto ease = [&](float cur, float tgt) { return approach(cur, tgt, joyFlying ? 14.f : fabsf(tgt) < fabsf(cur) ? 16.f : 4.f, dt); };
      plane.assistPitch = ease(plane.assistPitch, tp); plane.assistRoll = ease(plane.assistRoll, tr);
    } else {
      plane.assistPitch = plane.assistRoll = 0.f;
      c.pitch = approach(c.pitch, tp, rate, dt);
      c.roll = approach(c.roll, tr, rate, dt);
    }
  } else plane.assistPitch = plane.assistRoll = 0.f;
  (void)manual;
  // XR-40 weapons hot: the bumpers fire and bomb, so the rudder is frozen until weapons are safe again
  if (plane.spec->special == 2 && somnus.armed && !plane.onGround) { yawIn = 0; padY = 0; }
  c.yaw = approach(c.yaw, clampf(yawIn + padY, -1, 1), jY ? 14.f : 4.f, dt);
  plane.assistYaw = plane.assistOn ? c.yaw * 0.6f : 0.f;   // (assisted, the rudder pedals add to the coordination)
  // throttle
  float thr = 0;
  if (actDown(ACT_THR_UP) || key(K_PGUP) || key(K_PLUS)) thr += 0.55f;
  if (actDown(ACT_THR_DN) || key(K_PGDN) || key(K_MINUS)) thr -= 0.55f;
  if (in.pad) thr += (in.rt - in.lt) * 0.6f;
  bool numKey = in.pressed['0'];
  for (int k = 1; k <= 9; k++) numKey |= in.pressed['0' + k] && !actKey(ACT_THR_DN);
  const bool leverTakes = leverMoved && !joyThrTaken;
  if (leverMoved) { joyThrTaken = true; joyThrLast = lever; }
  if (thr != 0 || numKey) { joyThrTaken = false; joyThrLast = jT ? lever : -1.f; }   // (the last throttle input wins)
  if (plane.apOn && plane.apMode == Plane::AP_HOLD && plane.apSpeed > 0 && (numKey || leverTakes)) { plane.apSpeed = 0; toast("Autothrottle off - manual throttle", vec3(1, 0.85f, 0.5f)); }
  // (while the autopilot has the throttle the lever waits where it is: handed back, the power stays until it moves)
  if (apNav || (plane.apOn && plane.apSpeed > 0)) { joyThrTaken = false; joyThrLast = jT ? lever : -1.f; }
  if (apNav) {}   // the autopilot has the throttle
  else if (plane.apOn && plane.apSpeed > 0) {   // autothrottle: throttle inputs move the speed target
    const AircraftSpec& sp = *plane.spec;
    plane.apSpeed = clampf(plane.apSpeed + thr * 30.f * dt, sp.vref * 1.2f, sp.cruise * (sp.special ? 1.6f : 1.15f));
  } else {
    c.throttle = clampf(c.throttle + thr * dt, 0, 1);
    if (joyThrTaken) c.throttle = lever;
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
    if (plane.spec->special == 2 && flapNotch > 0.99f && !plane.apOn)
      toast("VTOL: hold " + actLabel(ACT_THR_UP, padActive, true) + "; release to idle", vec3(0.8f, 0.55f, 1));
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
  if ((actKeyP(ACT_GEAR) || gearPad || actJoyP(ACT_GEAR) || (!showMap && (in.buttonsPressed & PAD_RIGHT))) && plane.spec->retract) {
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
  // a figure flown (or given up) and level again: aerobatics ends, and the aircraft goes back to what was flying it
  // before - the pilot (the autopilot off), the autopilot's hold, or its route to the field it was flying to (the
  // recorded lines: what the autopilot's own disconnect and engagement say)
  if (plane.apStuntEnded) {
    plane.apStuntEnded = false;
    if (!plane.apOn) {}   // (already handed back)
    else if (!plane.apStuntWasOn) { plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f)); }
    else if (plane.apStuntWasAirport >= 0) { const int keep = apDest; apDest = plane.apStuntWasAirport; engageAutopilot(); apDest = keep; }
    else toast("Autopilot ON: the stick trims heading and altitude, throttle sets speed", vec3(0.6f, 1, 0.6f));
  }
  if (plane.spec->special == 2) somnusControls(dt);
  if (actKeyP(ACT_PARK) || actJoyP(ACT_PARK) || (!showMap && actPadP(ACT_PARK))) { parking = !parking; toast(parking ? "Parking brake SET" : "Parking brake released", vec3(1, 0.85f, 0.5f)); }
  float wb = std::max(actDown(ACT_BRAKE) ? 1.f : 0.f, joyAxis(AX_BRAKE));   // (toe brakes: as far as they are pressed)
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
  // speed first), once; pressed again during it, like any other input, it hands the aircraft back at once
  if (actPressed(ACT_STUNT)) {
    if (plane.onGround) toast("Aerobatics need to be airborne", vec3(1, 0.6f, 0.4f));
    else if (plane.apOn && plane.apMode == Plane::AP_STUNT) {
      plane.apDisengage(); g_audio.trigger(SFX_AP_DISC); toast("Autopilot disconnected - your aircraft", vec3(1, 0.7f, 0.3f));
    } else {
      plane.apStuntBegin(stuntNext, wx);
      toast(fmt("Aerobatics: %s", Plane::stuntName(plane.apStunt)), vec3(0.6f, 1, 0.6f));
      toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      stuntNext = (stuntNext + 1) % Plane::STUNT_COUNT;
      g_audio.trigger(SFX_CLICK);
    }
  }
  if (actPressed(ACT_LIGHTS)) { landingLight = !landingLight; toast(landingLight ? "Landing lights ON" : "Landing lights OFF"); }
  // the engine key: pressed with the engine stopped, it starts it; held a second with it running, on the ground at a
  // walking pace or slower, it shuts it down (the arrival's shutdown at the stand: a starter press brings it back)
  {
    const float gsNow = length(vec3(plane.vel.x, 0, plane.vel.z));
    if (actDown(ACT_ENGINE) && plane.engineRunning && plane.onGround && gsNow < 3.f && !engineHeld) {
      engineHold += dt;
      if (engineHold >= 1.f) {
        plane.engineRunning = false; plane.starterTime = 0; engineShutDown = true; engineHeld = true;
        toast(plane.spec->engines > 1 ? "Engines shut down" : "Engine shut down", vec3(0.85f, 0.95f, 1.f), false);
        g_audio.trigger(SFX_CLICK);
      }
    } else if (!actDown(ACT_ENGINE)) { engineHold = 0; engineHeld = false; }
    if (actPressed(ACT_ENGINE) && !plane.engineRunning && plane.fuel > 0 && !engineHeld) { plane.starterTime = 0.01f; engineShutDown = false; toast("Engine start"); }
  }
  // time acceleration
  if (actPressed(ACT_TIME)) {
    const std::string why = warpBlocker();
    if (timeAccel >= 16.f) { timeAccel = 1; toast("Time warp off"); }
    else if (!why.empty()) { timeAccel = 1; toast("Time warp unavailable - " + why, vec3(1, 0.7f, 0.4f)); }
    else {
      timeAccel *= 2.f;
      toast(fmt("Time warp x%.0f", timeAccel), vec3(0.45f, 0.9f, 1.f));
      if (timeAccel > 4.f && !warpWarned) {   // (once a session: the cost is the PC's, not the flight's)
        warpWarned = true;
        toast("Above x4 the world streams in much faster: slower PCs may stutter", vec3(0.85f, 0.85f, 0.85f), false);
      }
      if (!plane.apOn) toast("Warp flies assisted: let go and it holds its bank and its climb", vec3(0.85f, 0.95f, 1.f), false);
    }
  }
  // Only the explicitly selected 90-degree hover notch uses momentary manual power. Resolve it after the pod
  // and autopilot switches so entry, stick takeover and AP disengagement cannot carry a stale throttle command
  // for one physics step. Intermediate / forward notches and every engaged autopilot retain their normal law.
  if (!plane.apOn && plane.spec->special == 2 && c.flaps > 0.99f) {
    float power = 0.f;
    if (actOk(ACT_THR_UP)) {
      if (actDown(ACT_THR_UP) || key(K_PGUP) || key(K_PLUS)) power = 1.f;
      for (int k = 1; k <= 9; k++) if (key('0' + k)) power = std::max(power, k / 9.f);
      if (joyThrTaken) power = std::max(power, lever);
      const float rt = in.pad && !vtolRtUnarmed && in.rt > 0.05f ? in.rt : 0.f;
      const float lt = in.pad && !vtolLtUnarmed && in.lt > 0.05f ? in.lt : 0.f;
      power = clampf(std::max(power, rt) - lt, 0.f, 1.f);
      if (actDown(ACT_THR_DN) || key(K_PGDN) || key(K_MINUS) || key('0')) power = 0.f;
    }
    c.throttle = power;   // engineSpool still follows this target at the aircraft's existing spool rate
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

void Game::updateShake(float dt) {
  shakeTrauma = std::max(0.f, shakeTrauma - dt * 1.6f);
  float b = 0.f;
  const bool live = screen == SCR_FLIGHT && !paused && !crashed && plane.spec && timeAccel <= 1.f;
  if (live) {
    const float hp = plane.gLoad - gLp; gLp += (plane.gLoad - gLp) * (1.f - expf(-dt * 2.5f));
    b += clampf(fabsf(hp) * 0.55f, 0.f, 0.5f);                        // (the air's bumps, felt as g)
    b += smoothstepf(0.45f, 1.f, plane.stallWarn) * 0.45f;            // (the stall's buffet)
    b += gTunnel * 0.35f;                                             // (near the airframe's limit)
    if (plane.onGround) {
      const float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
      b += clampf(plane.groundRough, 0.f, 1.f) * smoothstepf(2.f, 30.f, gs) * 0.35f + smoothstepf(5.f, 50.f, gs) * 0.05f;
    }
  } else gLp = plane.spec ? plane.gLoad : 1.f;
  shakeBuffet += (b - shakeBuffet) * (1.f - expf(-dt * 8.f));
  if (!live) shakeBuffet = std::min(shakeBuffet, 0.f + b);
  shakeT += dt;
  const float on = live || (screen == SCR_FLIGHT && crashed && !paused) ? 1.f : 0.f;
  rumbleLo = set.rumble ? on * clampf(shakeTrauma * shakeTrauma * 1.2f + shakeBuffet * 0.5f, 0.f, 1.f) : 0.f;
  rumbleHi = set.rumble ? on * clampf(shakeBuffet * 0.9f + shakeTrauma * 0.3f, 0.f, 1.f) : 0.f;
}

void Game::updateFlight(float dt) {
  updateShake(paused ? 0.f : dt);
  if (paused) { rumbleLo = rumbleHi = 0.f; return; }
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
    const std::string why = warpBlocker();
    if (!why.empty()) { timeAccel = 1; toast("Time warp off - " + why, vec3(1, 0.8f, 0.45f)); }
  }
  if(militaryFlight || hiveCombat.aliveCount()>0 || hiveCombat.mission.status==hive::MissionStatus::Active) timeAccel=1;
  float simDt = dt * timeAccel;
  vec3 prevPos = plane.pos;
  refreshGroundPits();   // (what the wheels, the weapons and the wreckage meet this frame)
  moveClouds(simDt);
  if (!crashed) {
    // (time warp: the frame's flight in steps of at most x4 - the rate the autopilot's and the assists' loops, the
    // weather and the failures were made at - ending early at a touchdown or an impact, which the frame then sees)
    const int chunks = std::max(1, (int)ceilf(timeAccel / 4.f - 1e-3f));
    const float h = simDt / chunks;
    for (int k = 0; k < chunks; k++) {
    updateWeather(h);
    updateFailures(h);
    updateResearchCard(h);
    { PartTimer pt(*this, "flight model"); plane.step(h, wx, gameTime + h * k); }
    if (plane.seamShift.x != 0.f || plane.seamShift.z != 0.f) { prevPos += plane.seamShift; followSeam(plane.seamShift); plane.seamShift = vec3(); }
    if (benchPin) { plane.pos = benchPinPos; plane.q = benchPinQ; plane.w = vec3(); }   // (a timing scene held in place: flying, going nowhere)
    if (plane.apWindEvent != apWindSaid) {   // (the autopilot went around for a wind that turned behind it on the final: Plane::step)
      if (plane.apWindEvent == 1) toast("Autopilot: too much tailwind to stop on this runway - going around", vec3(1, 0.75f, 0.35f));
      else if (plane.apWindEvent == 2) {
        if (!plane.apDecline.empty()) {
          g_audio.trigger(SFX_AP_DISC);
          toast("Autopilot: the wind has shifted - unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
          toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
        } else if (plane.apAirport >= 0) toast(fmt("Autopilot: the wind has shifted - now runway %02d at %s", g_world.airports[plane.apAirport].rwyNumber(plane.apRev), g_world.airports[plane.apAirport].code), vec3(0.6f, 1, 0.6f));
        plane.apWindEvent = 0;
      }
      apWindSaid = plane.apWindEvent;
    }
    flightClock += h;
    timeOfDay += h / 3600.f;
    if (plane.ev.bellyLanding && !result.bellyLanding) { result.bellyLanding = true; toast("Belly landing - hold it straight", vec3(1, 0.6f, 0.3f)); g_audio.triggerLocal(SFX_METAL, vec3(0,-plane.gearHeight(),0), .7f); }
    if (plane.ev.touchdown || plane.ev.crashed) { simDt = h * (k + 1); break; }
    }
  }
  gameTime += simDt;
  if (plane.engineRunning && !wasRunning) {
    // starter catches: puff of smoke from the exhausts
    vec3 ex = plane.pos + plane.q.rotate(vec3(0.4f, -plane.spec->fusRad * 0.6f, -plane.spec->fusLen * 0.35f));
    for (int i = 0; i < 14; i++) spawn(ex, plane.q.rotate(vec3(0.8f + i * 0.05f, -0.5f, 2.f)) + vec3(0, 0.6f, 0), 2.5f, 0.6f, 1.6f, vec3(0.55f, 0.58f, 0.62f), 0.55f, SPR_SMOKE, 1.5f, 0.3f);
    if (!takeoffAnnounced && contract.type == CT_LESSON) toast("Engine running. Release the parking brake with " + actLabel(ACT_PARK, padPrompts(), true) + ".", vec3(0.7f, 1, 0.7f));   // ("B" by default)
    else if (!takeoffAnnounced) toast("Engine running.", vec3(0.7f, 1, 0.7f));   // (the takeoff clearance is the tower's: updateAtc)
  }
  // gear / flap motor cues
  if (prevGear > 0.99f && plane.gear < 0.99f) g_audio.triggerLocal(SFX_GEAR_CLUNK, vec3(0,-plane.gearHeight(),0), .6f);
  if (prevGear < 0.99f && plane.gear >= 0.99f) g_audio.triggerLocal(SFX_GEAR_CLUNK, vec3(0,-plane.gearHeight(),0), 1.f);
  prevGear = plane.gear; prevFlaps = plane.flaps;
  // weather effects: in a storm a stroke every 6 to 21 s, 600 m to 4 km off - down to the ground from the cloud's base
  // (two in five), crawling along under the base (one in five), or hidden in the cloud, lighting it from inside
  if (wx.storm) {
    nextLightning -= dt;
    if (nextLightning <= 0) {
      nextLightning = 6.f + (rand() % 1000) * .015f;
      const float angle = (rand() % 6283) * .001f, range = 600.f + (rand() % 3400), kind = (rand() % 1000) * .001f;
      const float base = std::max(wx.cloudBase, 300.f);
      const vec3 foot = camPos + vec3(cosf(angle) * range, 0.f, sinf(angle) * range);
      const float drift = (rand() % 1000) * 0.6f, da = (rand() % 6283) * .001f;
      vec3 top = foot + vec3(cosf(da) * drift, 0.f, sinf(da) * drift); top.y = base;
      const uint32_t seed = (uint32_t)rand() * 2654435761u + 1u;
      if (kind < 0.4f) makeStrike(top, vec3(foot.x, std::max(g_world.height(foot.x, foot.z), 0.f), foot.z), true, seed);
      else if (kind < 0.6f) {   // (a crawler: a few hundred metres to a couple of km along under the base)
        const float L = 500.f + (rand() % 1500), a2 = (rand() % 6283) * .001f;
        makeStrike(top - vec3(0, 20.f, 0), top + vec3(cosf(a2) * L, -40.f - (rand() % 80), sinf(a2) * L), false, seed);
      } else makeStrike(top + vec3(0, 300.f, 0), top + vec3(0, 300.f, 0), false, seed);   // (in the cloud: no channel seen)
      const Strike& S = strikes.back();
      g_audio.triggerAt(SFX_THUNDER, (S.top + S.foot) * 0.5f, .85f);   // the mixer delays the sound by the actual distance / speed of sound
    }
  }
  // the strokes' light: the flash over everything, as bright as the nearest stroke is close; the newest one's in its cloud
  {
    float flash = 0.f;
    for (Strike& S : strikes) {
      S.t += dt;
      const float d = length((S.top + S.foot) * 0.5f - camPos), f = S.flash(S.t);
      flash = std::max(flash, f * clampf(2400.f / std::max(d, 1.f), 0.3f, 1.2f) * (S.a.empty() ? 0.6f : 1.f));
    }
    while (!strikes.empty() && strikes.front().t > 1.4f) strikes.erase(strikes.begin());
    lightning = flash;
    lightningGlow = strikes.empty() ? 0.f : strikes.back().flash(strikes.back().t);
    if (!strikes.empty()) lightningTop = strikes.back().top;
  }
  { PartTimer pt(*this, "cloud wake"); updateCloudWake(simDt); }

  updateUfo(simDt);
  // AI traffic
  traffic.enabled = set.traffic && !militaryFlight;
  traffic.audioObserver = dbgCam ? dbgCamPos : camPos;
  if(!militaryFlight) airlineTraffic(simDt);
  if (traffic.update(simDt, plane.pos, plane.vel, plane.onGround || crashed, plane.spec->span) && !crashed && !plane.ev.crashed) {
    plane.ev.crashed = true; plane.ev.crashReason = "Mid-air collision";
  }
  for (auto& f : traffic.puffs) spawn(f.p, f.v, f.life, f.size, f.grow, f.col, f.alpha, f.kind, 1.f, 0.f);
  for (auto& b : traffic.booms) { g_audio.triggerAt(SFX_BOOM, b.first, b.second); shake(0.3f * clampf(b.second, 0.f, 1.f)); }
  { PartTimer pt(*this, "XR-40 systems"); updateSomnus(simDt); }
  updateHive(simDt);
  if(screen!=SCR_FLIGHT) return;
  // Continuous, individually tracked traffic engines provide the pass and its real Doppler shift.
  for (auto& m : traffic.radio) toast(m, vec3(1.f, 0.78f, 0.3f));
  // entertainment: O + P held for a second while flying summons the Nocturne display pair (again: sends them home).
  // They fly their show around you and bow out when you land or settle onto an approach
  {
    bool flying = !crashed && !plane.onGround;
    if (flying && in.down['O'] && in.down['P']) {
      escortSummon += dt;
      if (escortSummon >= 1.f && !escortLatch) {
        escortLatch = true;
        if (traffic.escortActive()) traffic.dismissEscort();
        else if (traffic.escortStop) toast("Nocturne display pair: climb away from the field first", vec3(1.f, 0.78f, 0.3f));
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
    // g-force lens (g_lens.glsl): a clear view up to 80% of the airframe's limit load either way; from there a dark red
    // iris closes over it, most of the way by the limit itself and the rest as the airframe takes the overstress that breaks
    // it (Plane::step: overG fails it at 1, twice the limit at once), so it is fully over the view as the airframe lets go
    const float frac = plane.gLoad >= 0.f ? plane.gLoad / plane.spec->gLimitPos() : plane.gLoad / plane.spec->gLimitNeg();
    const float stress = clampf(std::max(plane.overG, frac - 1.f), 0.f, 1.f);
    const float target = crashed ? 0.f : 0.85f * clampf((frac - 0.8f) / 0.2f, 0.f, 1.f) + 0.15f * stress;
    gTunnel = gTunnelPin >= 0.f ? gTunnelPin : approach(gTunnel, target, target > gTunnel ? 4.f : crashed ? 1.2f : 2.f, dt);
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
    { PartTimer pt(*this, "wreck"); updateWreck(dt); }
    camYaw += dt * 0.12f;   // slow orbit around the crash site
    if (crashTimer > crashEndT && screen == SCR_FLIGHT) endFlight(false, plane.ev.crashReason);
    return;
  }
  if (plane.ev.crashed) {
    vec3 impactVel = plane.vel;
    crashed = true;
    toast(plane.ev.crashReason, vec3(1, 0.4f, 0.3f));
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.5f;
    bool air = plane.ev.crashReason.find("Structural") != std::string::npos && plane.agl() > 4.f;   // overstressed in flight: it comes apart in the air
    g_audio.triggerAt(water && !air ? SFX_SPLASH : air ? SFX_METAL : SFX_IMPACT, plane.pos, 1.f);
    if (!water && plane.fuel > 1.f) g_audio.triggerAt(SFX_CRASH, plane.pos, clampf(.35f+sqrtf(plane.fuel)*.035f,.35f,1.f));
    { PartTimer pt(*this, "break-up"); breakUp(impactVel, water, air); }
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
    if (plane.ev.touchdown && takeoffAnnounced && atcF.goAround && !atcF.goAroundUnable) result.landedAgainstGoAround = true;
    if (plane.onGround) { appLow = false; appHigh = false; if (result.tdPastThrM >= 0 && length(plane.vel) > 1.f) result.stopLeftM = A.length - along; }   // (while still rolling: its direction says which end)
  }
  // touchdown
  if (plane.ev.touchdown && takeoffAnnounced) {
    float fpm = -plane.ev.touchdownVs * 196.85f;
    touchdownFpm = fpm; touchedDown = true;
    tdRunway = g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);   // (the arrival's evidence: the last touchdown's runway, or -1 off every runway)
    const GearStations audioGear = gearStations(*plane.spec);
    const AudioSurface contactSurface=tdRunway>=0?AudioSurface(g_world.airports[tdRunway].surface):AudioSurface::Grass;
    // A belly arrival has no tyres to spin up; its metal contact is emitted above.
    if(!result.bellyLanding) for (float side : {-1.f,1.f}) g_audio.touchdown(
      plane.pos+plane.q.rotate(vec3(side*audioGear.track,-plane.gearHeight(),audioGear.mainZ)),
      clampf(fpm/600.f,.15f,.8f),length(vec3(plane.vel.x,0,plane.vel.z)),contactSurface);
    // (the rating on the touchdown scale the settlement pays by: career.h touchdownGrade - spoken, and stamped on the HUD)
    callout(fmt("%s  %.0f fpm", touchdownGradeCall(touchdownGrade(fpm)), fpm));
    tdStampT = realTime; tdStampFpm = fpm; arrivalTold = false;
    profileTouchdown(fpm, tdRunway);   // (airport stamps, butter, the type's softest: game_profile.cpp)
    shake(clampf(fabsf(fpm) / 750.f, 0.12f, 0.8f));
    float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
    for (int side = -1; side <= 1; side += 2) {
      const GearStations gst = gearStations(*plane.spec);   // (at the main wheels)
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ));
      int n = (int)clampf(gs * 0.3f, 4, 18);
      for (int i = 0; i < n; i++) spawn(wp, plane.vel * 0.3f + vec3((rand() % 100 - 50) * 0.02f, 0.5f, (rand() % 100 - 50) * 0.02f), 2.5f + (rand() % 100) * 0.01f, 0.6f, 1.8f, vec3(0.75f, 0.75f, 0.76f), 0.45f, SPR_SMOKE, 2.f, 0.2f);
    }
  }
  if (!plane.onGround) result.maxBank = std::max(result.maxBank, fabsf(plane.bankDeg()));
  profileTick(dt);   // (landmarks found, sightings, the distance flown)
  updateSightseeing();
  if (contract.type == CT_TRIAL && trialT0 >= 0.f && trialT1 < 0.f)   // (the ghost's track: where this run was, every kGhostDt s from the first gate)
    while ((float)ghostRec.size() * kGhostDt <= flightClock - trialT0 && ghostRec.size() < 100000) ghostRec.push_back(plane.pos);
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
      const GearStations gst = gearStations(*plane.spec);   // (behind the main wheels)
      vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ + 0.5f));
      spawn(wp, plane.vel * 0.15f + vec3(0, 0.8f, 0), 2.5f, 0.8f, 2.2f, col, surf == SURF_GRASS ? 0.15f : 0.35f, SPR_SMOKE, 1.5f, 0.1f);
    }
  }
  // a wet runway in the rain: spray thrown up behind the main wheels at speed, a mist trailing off them
  if (plane.onGround && wx.precip == 1 && rainNow > 0.1f && plane.groundRough <= 0.3f && gs > 12.f) {
    sprayAccum += dt * gs * 0.45f * std::min(rainNow * 1.5f, 1.f);
    const GearStations gst = gearStations(*plane.spec);
    while (sprayAccum > 1) {
      sprayAccum -= 1;
      const int side = rand() & 1 ? 1 : -1;
      const vec3 wp = plane.pos + plane.q.rotate(vec3(side * gst.track, -plane.gearHeight(), gst.mainZ + 0.6f));
      spawn(wp, plane.vel * 0.55f + plane.right() * (side * (1.f + (rand() % 100) * 0.03f)) + vec3(0, 1.f + (rand() % 100) * 0.02f, 0),
            0.9f + (rand() % 100) * 0.008f, 0.4f, 2.6f, vec3(0.86f, 0.88f, 0.92f), 0.32f, SPR_SMOKE, 2.6f, -0.4f);
    }
  } else sprayAccum = 0.f;
  // prop wash dust on rough ground
  if (plane.onGround && plane.groundRough > 0.3f && plane.engineSpool > 0.5f && gs < 15.f && (rand() % 3 == 0)) {
    vec3 bp = plane.pos + plane.q.rotate(vec3((rand() % 100 - 50) * 0.04f, -plane.gearHeight(), 3.f));
    spawn(bp, plane.q.rotate(vec3(0, 0.3f, 8.f)), 1.5f, 0.6f, 2.f, vec3(0.6f, 0.55f, 0.45f), 0.18f, SPR_SMOKE, 2.f, 0.f);
  }
  // wingtip vapour in humid air under g: a continuous ribbon from each tip. Points are dropped at the tips and stay
  // in the air mass (drifting with the wind); the strip between them widens and thins out as it ages. Each is caught
  // by its tip's vortex as it is dropped and winds round its core as the pair sinks (aero_wake.h), so the ribbon
  // curls inboard and down behind the wing - faster the harder the wing is pulling.
  for (int sd = 0; sd < 2; sd++) {
    auto& tt = tipTrail[sd];
    for (auto& q : tt) {
      q.age += simDt; q.p += plane.windVel * simDt;
      if (q.caught) { q.sw.c += plane.windVel * simDt; q.p = aeroSwirlStep(q.sw, q.p, simDt); }
    }
    while (!tt.empty() && tt.front().age > 1.8f) tt.erase(tt.begin());
  }
  float vapK = plane.onGround ? 0.f : clampf((plane.gLoad - 1.7f) / 1.5f, 0.f, 1.f) * ((wx.precip > 0 || plane.pos.y > wx.cloudBase - 300.f) ? 1.f : 0.f);
  if (!plane.onGround && plane.wxl.cloud > 0.03f) vapK = std::max(vapK, 0.35f * smoothstepf(0.03f, 0.3f, plane.wxl.cloud) * smoothstepf(20.f, 50.f, plane.airspeed));   // (in cloud the tips' vortices stir it at any g)
  if (vapK > 0.f || tipOn) {
    if (vapK > 0.f && !tipOn) tipSeg++;
    vec3 tip = plane.spec->special == 2 ? kSomnusWingTip : plane.spec->special ? kJetWingTip : modelWingTip(kModels[plane.spec - kAircraft]);
    for (int sd = 0; sd < 2; sd++) {
      float sx = sd ? 1.f : -1.f;
      TipPt t{plane.pos + plane.q.rotate(vec3(sx * tip.x, tip.y, tip.z + 0.3f)), 0.f, vapK, tipSeg};
      t.caught = aeroWakeCatch(plane.wake, plane.pos, plane.q, t.p, t.sw);
      tipTrail[sd].push_back(t);
      if (tipTrail[sd].size() > 400) tipTrail[sd].erase(tipTrail[sd].begin());
    }
    tipOn = vapK > 0.f;
  }
  if (plane.spec->special) { PartTimer pt(*this, "jet effects"); jetEffects(simDt); }
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
        const size_t n0 = particles.size();
        spawn(gc + radial * (rr * 90.f), tang * r.range(6.f, 16.f) + radial * r.range(-8.f, 2.f) + nrm * r.range(-4.f, 4.f), r.range(0.8f, 1.6f), r.range(2.5f, 5.f), -1.5f,
              vec3(0.3f, 1.f, 0.6f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, 0.f);
        if (particles.size() > n0) particles.back().gate = true;
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
          const size_t n0 = particles.size();
          spawn(gc + radial * 78.f, radial * r.range(30.f, 90.f) + nrm * r.range(-25.f, 25.f) + plane.vel * 0.3f, r.range(0.8f, 1.8f), r.range(3.f, 6.f), -1.5f,
                (k % 3 ? vec3(0.4f, 1.f, 0.7f) : vec3(1.f, 1.f, 0.8f)) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 1.4f, -3.f);
          if (particles.size() > n0) particles.back().gate = true;
        }
      }
      wpIndex++; result.wpDone = wpIndex; shake(0.1f);
      if (contract.type == CT_TOUR && !contract.story && wpIndex >= (int)contract.wps.size()) {   // the photo flight's shot
        photoFlashT = realTime; g_audio.trigger(SFX_CLICK, 1.f);
        toast("Click! The photographer has the shot", vec3(1.f, 0.95f, 0.8f), false);
      }
      if (contract.type == CT_TRIAL) {
        if (wpIndex == 1) trialT0 = flightClock;
        if (wpIndex >= (int)contract.wps.size()) trialT1 = flightClock;
        if (wpIndex >= 2 && trialT0 >= 0.f) {   // the split at this gate, against the best run's
          const float sp = flightClock - trialT0; trialSplits.push_back(sp);
          int kind = -1; for (int k = 0; k < TR_COUNT; k++) if (contract.id == trialId(k)) kind = k;
          auto it = kind >= 0 ? trialBestSplits.find(trialKey(kind)) : trialBestSplits.end();
          const size_t i = trialSplits.size() - 1;
          if (it != trialBestSplits.end() && i < it->second.size() && it->second[i] > 0.f) {
            const float d = sp - it->second[i];
            toast(fmt("Gate %d  %s  %+.1f s", wpIndex, trialScore(TR_GATES, sp).c_str(), d), d <= 0.f ? vec3(0.45f, 1.f, 0.6f) : vec3(1.f, 0.55f, 0.4f), false);
          } else toast(fmt("Gate %d  %s", wpIndex, trialScore(TR_GATES, sp).c_str()), vec3(0.6f, 0.9f, 1.f), false);
        }
      }
      if (contract.type == CT_TRIAL && g_music.owns()) g_music.note(wpIndex - 1); else g_audio.trigger(SFX_CHIME);   // (a gate run climbs a scale, gate by gate)
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
  if (plane.onGround && takeoffAnnounced && gs < 2.5f && !researchFlight && !militaryFlight) {
    stillTimer += dt;
    if (stillTimer > 1.2f) {
      float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
      bool atField = ap >= 0 && dA < g_world.airports[ap].length * 0.5f + 600.f;
      // an arrival is a landing on that field's runway (taxiing off to the apron afterwards is fine): the last
      // touchdown's runway, or where it stands when no touchdown was seen (a flight placed on the ground); stopping
      // near a field after landing beside or beyond its runway is an off-field landing, the load recovered by road
      const int rwy = tdRunway != -2 ? tdRunway : g_world.onRunway(plane.pos.x, plane.pos.z, 10.f);
      // checkpoints still to fly hold back only the arrival: on a field's runway the flight takes off again to go on,
      // or the leg ends there with the parking brake set (a safe diversion - the job carries the checkpoints flown);
      // anywhere else it is a field landing like any other (the review of v3.44.0, CAR-5: the leg could not be closed)
      const bool wpsLeft = wpIndex < (int)contract.wps.size(), onField = atField && rwy == ap;
      // a job's arrival waits for the parking brake: the pilot taxis in and parks (lessons, trials and practice
      // flights end at the stop, as their briefings say; arrivalWaits)
      const bool waits = arrivalWaits() && onField && !parkingBrake;
      arrivalAt = onField ? ap : -1;
      if (onField && wpsLeft && !parkingBrake) {
        if (stillTimer >= 1.25f && stillTimer - dt < 1.25f) {   // (the first line as recorded for the voice)
          toast("Checkpoints remaining - take off again to continue", vec3(1, 0.8f, 0.4f));
          toast(expandHint("Or set the parking brake ({parkingBrake}) to end the leg here", padPrompts(), true), vec3(1, 0.8f, 0.4f), false);
        }
      } else if (waits) {
        if (!arrivalTold) {
          arrivalTold = true;
          if (arrivedAt(ap)) {
            toast(fmt("Welcome to %s", g_world.airports[ap].name), vec3(0.6f, 1, 0.7f), false);
            toast(expandHint("Taxi clear of the runway, then set the parking brake ({parkingBrake}) to finish", padPrompts(), true), vec3(0.85f, 0.95f, 1.f), false);
            if (contract.payout > 0 && !engineShutDown) toast(expandHint("Shut down first (hold {engine}) for the airmanship bonus", padPrompts(), true), vec3(0.85f, 0.95f, 1.f), false);
          } else toast(expandHint(fmt("Landed at %s: set the parking brake ({parkingBrake}) to end the flight here, or take off again", g_world.airports[ap].name), padPrompts(), true), vec3(1, 0.8f, 0.4f), false);
        }
      } else if (onField && arrivedAt(ap) && !wpsLeft) { endFlight(true, ""); return; }
      else if (onField) { result.divertedTo = ap; endFlight(false, fmt(wpsLeft ? "Leg ended at %s with checkpoints remaining" : "Diverted to %s", g_world.airports[ap].name), OUT_DIVERTED); return; }
      else if (stillTimer > 3.f) { endFlight(false, atField ? "Landed off the runway" : "Landed off-airport", OUT_OFF_AIRPORT); return; }
    }
  } else { stillTimer = 0; if (gs > 4.f) arrivalAt = -1; }
  if (militaryFlight && plane.fuel<=0 && plane.onGround && gs<1.f) { endFlight(false,"Military sortie out of fuel",OUT_OUT_OF_FUEL); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && !takeoffAnnounced) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  if (plane.fuel <= 0 && plane.onGround && gs < 1.f && takeoffAnnounced) {
    // stopped dry: done, unless this is the destination with nothing left to fly (the completion check above ends
    // that one as a success). With checkpoints still to collect there's no way to take off again.
    float dA; int ap = g_world.nearestAirport(plane.pos.x, plane.pos.z, &dA);
    bool wpsLeft = wpIndex < (int)contract.wps.size();
    if (wpsLeft) { endFlight(false, "Out of fuel with checkpoints remaining", OUT_OUT_OF_FUEL); return; }
    if (!(arrivedAt(ap) && dA < 2000)) { endFlight(false, "Out of fuel", OUT_OUT_OF_FUEL); return; }
  }
  (void)prevPos;
  // tutorial hints
  updateLessonHint();
}

void Game::updateLessonHint() {
  phase = computePhase();
  if (contract.hints.size() <= (size_t)phase || contract.hints[phase].empty()) return;
  hint = expandHint(contract.hints[phase], padPrompts(), true);   // every frame: device switches and rebinding stay current
  if (phase == lastHintPhase) return;
  lastHintPhase = phase;
  const std::string key = contract.id + ".phase" + std::to_string(phase);
  if (set.showHints && std::find(hintsVoiced.begin(), hintsVoiced.end(), key) == hintsVoiced.end()) {
    hintsVoiced.push_back(key);
    commsPending.push_back({hint, contract.id, phase});   // exact lesson identity, never a fixed keyboard/controller alias
  }
}

// ------------------------------------------------------------------ pre-flight loading screen
// After a flight is chosen the world is held still while the scenery around the start position is generated (with a
// bigger per-frame budget), behind a card showing an aerial image of the airport. Once nothing is left to stream it
// cross-fades into a live establishing shot of the aircraft, and the player starts the flight when ready.
bool Game::loadingShadowPending() const {
  vec3 direction, colour; float night;
  computeSun(timeOfDay, direction, colour, night);
  return g_ren.tshRequiredPending(direction.y);
}
void Game::updateLoading(float dt) {
  // Back always wins over setup, including the update immediately after the first presented card.
  if (pendingLaunch) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) {
      cancelQueuedLaunch();
      in.pressed[K_ESC] = false; in.buttonsPressed &= ~PAD_B;   // (the menu returned to doesn't take it as its own Back)
      return;
    }
    loadT += dt;
    if (!launchFramePresented) return;
    PendingLaunch L = std::move(*pendingLaunch);
    pendingLaunch.reset();
    launchFrameDrawn = launchFramePresented = false;
    // the launch runs from where it was asked for: one that doesn't take (a save that fails, a selection gone stale)
    // leaves the player there with its message; one that does sets up the flight and its loading screen again
    screen = L.from; paused = L.paused;
    launchRunning = true; L.begin(); launchRunning = false;
    // (the flight set up: its loading screen as it now stands goes up on its own before the first frame of its scene,
    // which can take a while - the driver finishing the flight's programs on their first draw - so it never stands
    // still on the card of a flight not yet begun)
    launchSetUpShown = screen == SCR_LOADING;
    if (L.kind == LAUNCH_CAREER && screen == SCR_DEBRIEF) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; }   // (the debrief's retry didn't take: its message is on the hub)
    return;
  }
  if (pendingMilitaryLaunch) {
    if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) { returnToMilitary(); return; }
    loadT += dt;
    if (!militaryLaunchFramePresented) return;
    const Contract c = *pendingMilitaryLaunch;
    pendingMilitaryLaunch.reset();
    militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
    if (!beginMilitaryLaunch(c)) returnToMilitary();
    else launchSetUpShown = screen == SCR_LOADING;   // (its loading screen alone first, as a queued launch's: above)
    return;   // the first real scene and collision warmup still use the normal readiness path
  }
  if (militaryFlight || isMilitaryContract(contract.type)) {
    const auto& m = career.military;
    if (!militaryFlight || source != Career::SRC_MILITARY || commitBlocked() ||
        !career.attemptOpen || !m.activeAttempt || m.activeAttempt != career.attempt ||
        m.activeAttempt <= m.lastSettledAttempt || m.activeKind != contract.type-CT_MIL_RECON ||
        m.activeAirport != contract.from || career.canFly(contract,specIdx) != Career::SRC_MILITARY) {
      returnToMilitary(); return;
    }
  }
  if (militaryFlight && (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B))) { returnToMilitary(); return; }
  warmCombatCollision();
  loadT += dt;
  if (!headless) g_ren.prepareTerrainDepth();   // (ground depth first's program, for its trials in flight: renderer.h)
  if (!headless && earthFlight) g_ren.prepareTerrainDither();   // (the ground's program for the Earth regions' far blends)
  if (loadReadyT < 0) {
    dbgCam = false;
    updateCamera(dt);   // stream around the camera the flight will open with
    g_ren.entBudgetMs = 14.f;
    const int pend = std::max(0, g_ren.entPending);
    loadPend0 = std::max(loadPend0, pend);
    const bool shadowPending = loadingShadowPending();
    const bool baked = loadBakeSeen != g_ren.bakeCount;
    loadBakeSeen = g_ren.bakeCount;
    // Observe completed scene frames; elapsed time alone is not evidence that any work finished.
    if (loadFrames != loadObservedFrame) {
      const bool viewsIn = headless || loadViewsDrawn || crashed || !plane.spec;   // (the flight's views each drawn once: render)
      if (loadFrames >= 2 && pend == 0 && !shadowPending && !baked && viewsIn) ++loadStableFrames;
      else loadStableFrames = 0;
      loadObservedFrame = loadFrames;
    }
    const float scenery = loadPend0 > 0 ? 1.f - float(pend) / loadPend0 : (loadFrames >= 2 ? 1.f : 0.f);
    const float verified = std::min(loadStableFrames / 2.f, 1.f);
    const float completed = 0.20f * loadSetupDone() + 0.55f * scenery + 0.15f * (loadFrames >= 2 && !shadowPending ? 1.f : 0.f) + 0.10f * verified;
    loadShown = std::max(loadShown, std::min(completed, 0.99f));
    if (loadStableFrames >= 2) { loadReadyT = loadT; loadShown = 1.f; g_ren.entBudgetMs = 2.5f; }
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
    if (freeFlight) { returnToFreeFlight(); return; }
    if (militaryFlight) { returnToMilitary(); return; }
    // a career flight that never left the loading screen: its attempt closes and its job waits at its stop, as after
    // an interrupted session (nothing is charged: no leg was flown); an unsaved close shows as pending and blocks launches
    if (career.attemptOpen && !isolatedFlight && !researchFlight)
      commit([](Career& k) { if (k.military.activeAttempt) { k.abandonMilitaryAttempt(); return; } k.attemptOpen = false; if (k.job && k.job->state == Career::JobState::ACTIVE) k.job->state = Career::JobState::RECOVERY; });
    screen = researchFlight ? SCR_RESEARCH : SCR_HUB;
    militaryFlight=false; hiveCombat.reset(1, combatWrap());
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
  bool drag = in.mDown[1] || (camMode == 2 && in.mDown[0]);
  // chase and orbit cameras use reversed pitch: stick up / drag up swings the camera down so the view tilts up
  float pitchDir = (camMode == 0 || camMode == 2) ? -1.f : 1.f;
  float lookK = camMode == 1 ? 1.f / ckZoom : 1.f;   // zoomed in: finer look control
  if (drag) { camYaw -= in.mdx * 0.005f * set.mouseSens * lookK; camPitch = clampf(camPitch + pitchDir * in.mdy * 0.004f * set.mouseSens * lookK, -1.3f, 1.4f); }
  if (in.pad) { float rx = fabsf(in.rx) > 0.2f ? in.rx : 0, ry = fabsf(in.ry) > 0.2f ? in.ry : 0; camYaw -= rx * 2.f * dt; camPitch = clampf(camPitch + pitchDir * ry * 1.5f * dt, -1.3f, 1.4f); }
  camIdle = drag || (in.pad && (fabsf(in.rx) > 0.2f || fabsf(in.ry) > 0.2f)) ? 0.f : camIdle + dt;
  const AircraftSpec& s = *plane.spec;
  float size = std::max(s.fusLen, s.span);
  if (camMode == 0) {
    // the camera stays where it was swung until the view is changed (C) - or, with Settings' recentre, it eases back
    // behind the aircraft once left alone for 4 s (a brushed right stick lost the heading: the review of v3.50.3)
    if (set.camRecentre && camIdle > 4.f) {
      const float k = 1.f - expf(-dt * 1.6f);
      camYaw = wrapAngle(camYaw) * (1.f - k); camPitch += (0.12f - camPitch) * k;
    }
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
  // Use the same body-space viewing ray as rendering, never a projected/zoomed screen position.
  // Both mouse drag and controller look reach this path; no camera rotation or eye movement is added.
  const int focusModel = int(plane.spec - kAircraft);
  CockpitFocusTarget focusPanels[24];
  const int focusCount = camMode == 1 ? modelCockpitFocusTargets(focusModel, focusPanels, 24) : 0;
  const vec3 focusAim = quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(
      quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1)));
  const float manualZoom = actDown(ACT_ZOOM) ? std::max(ckZoomT, 2.8f) : ckZoomT;
  ckZoom = cockpitFocus.update(dt, camMode == 1, focusModel, set.cockpitFocusZoom && !dbgCam,
      plane.q.conj().rotate(camPos - plane.pos), focusAim, focusPanels, focusCount, manualZoom);
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
    int n = (int)g_world.airports.size();
    // Every research airframe has focusable preview controls. Start remains a
    // distinct launch action, never an extra activation of the focused control.
    if (in.buttonsPressed & PAD_RB) resAirport = (resAirport + 1) % n;
    if (in.buttonsPressed & PAD_LB) resAirport = (resAirport + n - 1) % n;
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

// ------------------------------------------------------------------ XR-30 research flights
Contract Game::researchContract() const {
  Contract c;
  const AircraftSpec& rs = kAircraft[resCraft];
  std::string num = rs.name; num = num.substr(0, num.find(' '));   // "XR-30"
  c.id = num; c.id.erase(std::remove(c.id.begin(), c.id.end(), '-'), c.id.end()); c.title = num + " Research Flight"; c.type = CT_FERRY;
  if (resCard >= 0 && kResCards[resCard].craft == resCraft) c.title = std::string(kResCards[resCard].id) + "  " + kResCards[resCard].title;
  c.from = c.to = resAirport; c.payout = 0;
  c.wx = Weather(); c.wx.timeOfDay = resTime; c.wx.windSpeed = 3; c.wx.turbulence = 0.05f;
  if (resWx == 0) { c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; }
  else if (resWx == 1) { c.wx.cloudCover = 0.6f; c.wx.cloudBase = 1300; }
  else { c.wx.cloudCover = 0.95f; c.wx.cloudBase = 800; c.wx.precip = 1; c.wx.storm = true; c.wx.windSpeed = 9; c.wx.gust = 5; c.wx.turbulence = 0.5f; c.wx.visibility = 9000; }
  return c;
}

void Game::launchResearch() {
  // (the flight's setup and its first scene - the craft's programs compiled, its airfield streamed - behind the
  // loading card, not behind a frozen terminal: Game::queueLaunch)
  { Contract card = researchContract(); card.startAirborne = resAirborne;
    if (queueLaunch(card, resCraft, LAUNCH_RESEARCH, [this] { launchResearch(); })) return; }
  bool wr = resCraft == kSomnus;
  const AircraftSpec& rs = kAircraft[resCraft];
  if (resCard >= 0 && kResCards[resCard].craft != resCraft) resCard = -1;
  resStep = 0; resHold = 0; resBest = 0; resCardDone = false;
  Contract c = researchContract();
  // (its own fuel, never the job card's choice: that stays for the career's next flight - the review of v3.44.0, CAR-7)
  const float careerFuelChoice = launchFuelKg;
  launchFuelKg = -1;
  Career::LaunchPlan researchPlan;
  researchPlan.spec = resCraft; researchPlan.src = Career::SRC_OWNED;
  researchPlan.startAirport = resAirport; researchPlan.fuelLoadKg = rs.maxFuel;
  startFlight(c, resCraft, Career::SRC_OWNED, &researchPlan);
  launchFuelKg = careerFuelChoice;
  researchFlight = true;
  combatLoadout.reset(practiceForward,practiceBomb);
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
  } else if (wr) toast(expandHint("{flapsDown}/{flapsUp}: tilt the four thruster pods fully down for vertical takeoff",padPrompts(),true), vec3(0.7f, 0.9f, 1));
  combatBasePayload=plane.payload; syncCombatPayload();
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
    for (int e = 0; e < nEx; ++e) g_audio.triggerLocal(SFX_REHEAT, exP[e], .45f / sqrtf(float(nEx)));
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
    if (wr && !water)   // the XR-40's plasma where each pod's jet strikes the ground: a violet glow, and sparks off it
      for (int s = 0; s < nEx; s++) {
        const vec3 ex = plane.pos + plane.q.rotate(exP[s]), d = plane.q.rotate(exD[s]);
        if (d.y > -0.5f) continue;
        const float t = (ex.y - gy) / -d.y;
        if (t > 30.f) continue;
        const float k = (1.f - t / 30.f) * smoothstepf(0.25f, 0.8f, thr);
        const vec3 hit = vec3(ex.x + d.x * t, gy + 0.25f, ex.z + d.z * t);
        spawn(hit, vec3(0.f), 0.05f, 1.6f + 2.4f * k, 0.f, vec3(0.6f, 0.42f, 1.f) * (1.5f * k), 1.f, SPR_GLOW, 0.f, 0.f);
        for (int i = 0; i < (int)(40.f * k * dt + frand()); i++) {
          const float a = frand() * 6.2832f;
          spawn(hit, vec3(cosf(a), 0.f, sinf(a)) * (8.f + 14.f * frand()) + vec3(0.f, 2.f + 4.f * frand(), 0.f), 0.25f + 0.3f * frand(), 0.03f, -0.02f,
                vec3(0.75f, 0.6f, 1.f) * 3.f, 1.f, SPR_SPARK, 1.5f, -6.f);
        }
      }
  }
  // The pilot feels transonic buffeting; an external observer hears a boom only when the Mach cone arrives.
  float M = plane.mach;
  if (M < .94f) audioMachArmed = true;
  if (audioMachArmed && M >= 1.f && !plane.onGround) {
    audioMachArmed = false;
    g_audio.triggerLocal(SFX_TRANSONIC, kModels[plane.spec-kAircraft].eye+vec3(0,0,-1), .45f);
    toast("MACH 1 - SUPERSONIC", vec3(0.4f, 0.9f, 1));
    vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), u = cross(r, f);
    // the boom: two shock rings racing out, and a collar of condensation flashing round the airframe at the shock -
    // denser in damp air - for a fraction of a second, thinning as it falls back (a thin ring: thicker or longer-lived,
    // it swallowed the chase camera, the whole view white)
    bursts.push_back({plane.pos, r * 25.f, u * 25.f, vec3(0.7f, 0.85f, 1.f), 0.f, false});
    bursts.push_back({plane.pos - f * 6.f, r * 45.f, u * 45.f, vec3(0.55f, 0.7f, 1.f), 0.f, false});
    const float humid = clampf(0.35f + 0.45f * wx.cloudCover + (wx.precip ? 0.3f : 0.f), 0.f, 1.f) * smoothstepf(13000.f, 1500.f, plane.pos.y);
    const float span = plane.spec->span, len = plane.spec->fusLen, R0 = 0.3f * span + 1.2f;
    const int n = (int)(36.f + 54.f * humid);
    for (int i = 0; i < n; i++) {
      const float a = (i + frand()) * 6.2832f / n, along = (frand() - 0.35f) * len * 0.3f;
      const vec3 radial = r * cosf(a) + u * sinf(a);
      const float rr = R0 * (0.7f + 0.4f * frand());
      spawn(plane.pos + radial * rr - f * along, plane.vel + radial * (3.f + 4.f * frand()),
            0.3f + 0.3f * frand(), 0.5f + 0.5f * frand(), 1.5f + 1.5f * frand(), vec3(0.96f, 0.97f, 1.f), 0.22f + 0.2f * humid, SPR_SMOKE, 0.6f, 0.f);
    }
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
  float dark = s.special == 2 ? 1.f - somnus.stealth : 1.f;   // a cloaked XR-40 runs dark
  auto W = [&](vec3 b) { return plane.pos + plane.q.rotate(b); };
  auto lens = [&](vec3 b, vec3 axis, int tint, vec3 emit) {
    int i = fp.plane.lensN; if (i >= 6) return;
    float* P = fp.plane.lensP[i]; float* C = fp.plane.lensC[i]; float* D = fp.plane.lensD[i];
    P[0] = b.x; P[1] = b.y; P[2] = b.z; P[3] = 0; C[0] = emit.x; C[1] = emit.y; C[2] = emit.z; C[3] = 0;
    D[0] = axis.x; D[1] = axis.y; D[2] = axis.z; D[3] = (float)tint; fp.plane.lensN++;
  };
  // Exactly one light per live exhaust, with the same moving anchor/radius/power as its plume.
  // This replaces the old special-craft and separate Reverie paths; a failed or unfueled engine emits neither.
  if (!(camMode == 1 && fp.sealedCockpit)) {
    const ExhaustVisual& ex = fp.plane.exhaust;
    const float flick = .85f + .15f*sinf(t*57.f)*sinf(t*23.f + 1.f);
    for (int k = 0; k < ex.count; ++k) {
      const float sp = ex.power[k][0], ab = ex.power[k][1], intensity = ex.power[k][2];
      vec3 c = lerp(vec3(.3f, .55f, 1.f), vec3(1.f, .62f, .3f), ab)
               *((14.f*sp + 45.f*ab)*intensity*flick*dark/(float)std::max(s.engines, 1));
      light(W(exhaustPosition(ex, k) + exhaustDirection(ex, k)*(.15f + .15f*ab)),
            std::max(.05f, ex.exit[k][3]*.2f), c, -2.f, vec3(0, 1, 0), .05f);
    }
  }
  // fixture positions (body space)
  vec3 tip = s.special == 2 ? kSomnusWingTip : s.special ? kJetWingTip : modelWingTip(md);
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
  g_audio.triggerAt(SFX_UFO_ARRIVE, ufo.pos, .9f, plane.vel, 900001);
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
  if (t > 16.f && !ufo.sfxLaugh) { ufo.sfxLaugh = true; g_audio.triggerAt(SFX_UFO_LAUGH, ufo.pos, 1.f, plane.vel, 900001); }
  if (t > 23.f && !ufo.sfxZoom) {
    ufo.sfxZoom = true; g_audio.triggerAt(SFX_UFO_ZOOM, ufo.pos, 1.f, plane.vel, 900001);
    vec3 c = ufo.pos;
    bursts.push_back({c, ufo.right * 14.f, ufo.fwd * 14.f, vec3(0.4f, 1.f, 0.7f), 0.f});
    for (int i = 0; i < 40; i++) spawn(c, plane.vel + vec3((rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f), 0.6f, 0.25f, -0.3f, vec3(0.5f, 1.f, 0.8f) * 3.f, 1.f, SPR_SPARK, 1.f, 0.f);
  }
  if (t > 23.f && t < 26.f) spawn(ufo.pos, plane.vel * 0.5f, 0.5f, 3.f, -4.f, vec3(0.4f, 1.f, 0.8f) * 1.5f, 1.f, SPR_GLOW, 0.f, 0.f);   // light streak
  if (t > 26.5f) { ufo.on = false; ufo.next = 480.f + (rand() % 1000) * 0.42f; }   // next one in 8-15 minutes
}

// ------------------------------------------------------------------ lightning
// A stroke's channel: the line from the cloud to the ground broken again and again - each piece's middle pushed aside
// up to a fifth of its length, square to it - so it zigzags at every scale as a real one does; branches forking off its
// upper two thirds, down and out, some forking once more, none reaching the ground. Its light: the stepped leader
// feeling its way down for 45 ms (faint, only as far as it has come), then the return stroke, all of it blinding at
// once, then a restroke or three down the main channel 50 to 120 ms apart
float Game::Strike::flash(float at) const {
  float f = 0.f;
  for (int i = 0; i < strokes; i++) { const float d = at - restrike[i]; if (d >= 0.f) f = std::max(f, (i ? 0.8f : 1.f) * expf(-d * 22.f)); }
  return f;
}
void Game::makeStrike(vec3 top, vec3 foot, bool ground, uint32_t seed) {
  Rng r(seed);
  Strike S; S.top = top; S.foot = foot; S.ground = ground;
  const float H = std::max(length(top - foot), 1.f);
  // a channel from a to b, broken `levels` times; each segment with its glow's half width and brightness
  auto channel = [&](vec3 a, vec3 b, int levels, float w, float k, float rough) {
    std::vector<vec3> P{a, b};
    for (int l = 0; l < levels; l++) {
      std::vector<vec3> Q; Q.reserve(P.size() * 2);
      for (size_t i = 0; i + 1 < P.size(); i++) {
        const vec3 d = P[i + 1] - P[i]; const float L = length(d);
        vec3 n = cross(d, vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
        n = length(n) > 1e-4f ? normalize(n) : vec3(1, 0, 0);
        Q.push_back(P[i]); Q.push_back((P[i] + P[i + 1]) * 0.5f + n * (L * rough * r.range(-1.f, 1.f)));
      }
      Q.push_back(P.back()); P.swap(Q);
    }
    for (size_t i = 0; i + 1 < P.size(); i++) {
      S.a.push_back(P[i]); S.b.push_back(P[i + 1]); S.w.push_back(w); S.k.push_back(k);
      S.along.push_back(clampf(length((P[i] + P[i + 1]) * 0.5f - top) / H, 0.f, 1.f));
    }
    return P;
  };
  if (H > 2.f) {   // (a flash hidden in the cloud has no channel)
    const std::vector<vec3> M = channel(top, foot, 7, ground ? 6.f : 4.f, ground ? 1.f : 0.6f, 0.2f);
    S.main = (int)S.a.size();
    const vec3 down = normalize(foot - top);
    const int nb = 4 + (int)r.range(0.f, 6.f);
    for (int i = 0; i < nb; i++) {
      const int at = (int)r.range(4.f, (float)M.size() * 0.66f);
      const vec3 side = normalize(cross(down, normalize(vec3(r.range(-1, 1), r.range(-0.3f, 0.3f), r.range(-1, 1)) + vec3(1e-3f, 0, 0))));
      const float L = H * r.range(0.12f, 0.35f) * (1.f - (float)at / M.size());
      const vec3 to = M[at] + normalize(down * r.range(0.5f, 1.f) + side * r.range(0.4f, 1.1f)) * L;
      const std::vector<vec3> B = channel(M[at], to, 5, 3.f, 0.6f, 0.24f);
      if (r.uni() < 0.6f) {   // (and a twig off it)
        const int j = (int)r.range(1.f, B.size() * 0.6f);
        channel(B[j], B[j] + normalize(down + side * r.range(-1.2f, 1.2f)) * (L * r.range(0.25f, 0.5f)), 4, 1.8f, 0.4f, 0.26f);
      }
    }
  }
  S.strokes = 2 + (int)r.range(0.f, 2.99f);   // (2 to 4)
  float t0 = 0.045f;
  for (int i = 0; i < S.strokes; i++) { S.restrike[i] = t0; t0 += r.range(0.05f, 0.12f); }
  if (strikes.size() >= 3) strikes.erase(strikes.begin());
  strikes.push_back(std::move(S));
}

// ------------------------------------------------------------------ crash wreckage
// the ground under the wreckage: the pits dug this flight in it (g_groundPits)
float Game::wreckGround(float x, float z) const { return pitGround(x, z, g_world.height(x, z, 6)); }
// this flight's pits, in the order the renderer draws them (frame(): the first 24): the crash crater, the pieces' pits,
// the plasma craters and the laser scorches, newest first
void Game::refreshGroundPits() {
  g_groundPits.clear(); g_scenery.craters.clear();
  if (screen != SCR_FLIGHT && screen != SCR_DEBRIEF) return;
  // craters flatten the scenery that stood in them (a plasma blast clears a far wider circle than its pit; laser scorch
  // pits don't: what a bolt destroys is tracked one object at a time)
  if (craterR > 0) g_scenery.craters.push_back(vec3(craterX, craterZ, craterR * 1.5f));
  for (const Pit& p : pits) if (p.R > 1.2f) g_scenery.craters.push_back(vec3(p.x, p.z, p.R * 1.3f));
  for (const vec3& f : somnus.flattened) g_scenery.craters.push_back(f);
  if (craterR > 0) g_groundPits.push_back({craterX, craterZ, craterR, craterD});
  for (const Pit& p : pits) g_groundPits.push_back({p.x, p.z, p.R, p.D});
  for (const auto& c : somnus.craters) g_groundPits.push_back({c.x, c.z, c.R, c.D});
  for (int i = (int)somnus.scorch.size() - 1; i >= 0; i--) g_groundPits.push_back({somnus.scorch[i].x, somnus.scorch[i].z, somnus.scorch[i].R, somnus.scorch[i].D});
  if (g_groundPits.size() > 24) g_groundPits.resize(24);   // (the renderer's 24)
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
    d.body = debrisPanel(1.2f * d.size, 2.f * d.size, r.range(15.f, 30.f));
    debris.push_back(d);
  }
  vec3 f = air && length(baseV) > 1.f ? normalize(baseV) : vec3(0, 1, 0);
  vec3 rr = normalize(cross(f, fabsf(f.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0))), u = cross(rr, f);
  bursts.push_back({c, rr * R * 3.5f, u * R * 3.5f, vec3(1.f, 0.65f, 0.35f), 0.f});   // shock ring
}

// A piece of the wreck hitting the ground or the sea with E joules: a crater and a burst of earth, dust and sparks - or
// a fireball, if it was burning - all of it sized by the blow. A crater's size goes as the cube root of the energy that
// dug it (an engine coming down at 90 m/s: a 5 m pit; a propeller at 40: a scrape); the earth flies out faster and
// further, and the dust rises higher, from a harder blow
float Game::partImpact(vec3 p, vec3 v, float E, float fire, bool water) {
  Rng r((uint32_t)(p.x * 31.f + p.z * 17.f + realTime * 977.f));
  const float s = cbrtf(std::max(E, 1.f)), R = clampf(0.055f * s, 0.4f, 18.f);
  auto sph = [&]() { vec3 d(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)); float l = length(d); return l > 1e-3f ? d / l : vec3(0, 1, 0); };
  vec3 hd(v.x, 0, v.z); hd = length(hd) > 1.f ? normalize(hd) : vec3();   // (the way it was going: the earth flies on that way)
  // Impact energy controls source level/timbre; distance is applied once by the mixer, with no loudness floor.
  if (E > 80.f) g_audio.triggerAt(water ? SFX_SPLASH : SFX_IMPACT, p, clampf(s / 130.f, .06f, .85f));
  if (!water && E > 2500.f) g_audio.triggerAt(SFX_METAL, p, clampf(s / 220.f, .08f, .45f));
  if (!water && fire > .5f && E > 15000.f) g_audio.triggerAt(SFX_CRASH, p, clampf(s / 270.f * fire, .08f, .6f));
  if (water) {   // a column of spray as tall as the blow, and a ring racing out over the water
    const int n = (int)clampf(R * 24.f, 10.f, 110.f);
    for (int i = 0; i < n; i++) {
      vec3 d = sph(); d.y = fabsf(d.y) * 2.8f + 0.5f;
      spawn(p + vec3(0, 0.3f, 0), d * (r.range(0.8f, 1.6f) * (3.f + 4.f * sqrtf(R))), r.range(1.6f, 3.f), clampf(R * r.range(0.25f, 0.45f), 0.4f, 6.f), R * 0.4f, vec3(0.92f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -7.f);
    }
    bursts.push_back({p + vec3(0, 0.2f, 0), vec3(R * 3.f, 0, 0), vec3(0, 0, R * 3.f), vec3(0.8f, 0.9f, 1.f), 0.f});
    return 0.f;
  }
  // the pit (a scrape below 0.6 m is earth thrown up, no more), the smallest given up for a bigger one past the limit
  float dug = 0.f;
  if (R >= 0.6f) {
    if ((int)pits.size() >= kMaxPits) {
      auto least = std::min_element(pits.begin(), pits.end(), [](const Pit& a, const Pit& b) { return a.R < b.R; });
      if (least->R < R) pits.erase(least);
    }
    if ((int)pits.size() < kMaxPits) { dug = R * r.range(0.26f, 0.34f); pits.push_back({p.x, p.z, R, dug}); refreshGroundPits(); }   // (the ground the pieces meet has it at once)
  }
  // earth thrown out of it: clods arcing out and falling back, downrange more than up
  const int nc = (int)clampf(8.f + R * 16.f, 8.f, 160.f);
  const float ve = 4.f + 7.f * sqrtf(R);
  for (int i = 0; i < nc; i++) {
    const float a = r.range(0.f, 6.2832f), el = r.range(0.45f, 1.3f), sp = ve * r.range(0.45f, 1.f);
    vec3 out = normalize(vec3(cosf(a), 0, sinf(a)) + hd * 0.7f);
    const float k = r.range(0.7f, 1.1f);
    spawn(p + vec3(0, 0.2f, 0), out * (sp * cosf(el)) + vec3(0, sp * sinf(el), 0), r.range(1.f, 2.2f), clampf(R * r.range(0.08f, 0.2f), 0.12f, 2.5f), 0.2f,
          vec3(0.3f, 0.24f, 0.17f) * k, 0.95f, SPR_SMOKE, 0.6f, -9.f);
  }
  // the dust: a skirt rolling out low over the ground, and a cloud rising over the pit that drifts off on the wind
  if (getenv("WRECKDBG")) printf("impact at %.1f s: %.0f kJ, crater %.1f m%s\n", crashTimer, E * 1e-3f, R, fire > 0.3f ? ", burning" : "");
  const int nd = (int)clampf(6.f + R * 5.f, 6.f, 60.f);
  for (int i = 0; i < nd; i++) {
    const float a = r.range(0.f, 6.2832f);
    const bool skirt = i % 2 == 0;
    vec3 out = vec3(cosf(a), 0, sinf(a));
    vec3 dv = skirt ? out * r.range(2.f, 3.f + 2.5f * R) + vec3(0, r.range(0.2f, 1.f), 0) : out * r.range(0.5f, 1.5f + R) + vec3(0, r.range(1.f, 2.f + 1.2f * R), 0);
    spawn(p + vec3(0, 0.4f, 0), dv + plane.windVel * 0.3f, r.range(2.5f, 5.f) * (1.f + 0.06f * R), clampf(R * r.range(0.4f, 0.7f), 0.6f, 9.f), R * r.range(0.25f, 0.5f) + 0.4f,
          vec3(0.46f, 0.39f, 0.31f) * r.range(0.8f, 1.05f), skirt ? 0.35f : 0.4f, SPR_SMOKE, skirt ? 1.4f : 0.8f, skirt ? 0.2f : r.range(0.6f, 1.6f));
  }
  bursts.push_back({p + vec3(0, 0.3f, 0), vec3(R * 4.f, 0, 0), vec3(0, 0, R * 4.f), vec3(0.55f, 0.46f, 0.36f), 0.f});   // the shock, racing out over the ground
  // a burning piece: what fuel it had goes up with it; a cold one throws sparks off the metal
  if (fire > 0.3f) fireball(p + vec3(0, 0.5f, 0), v * 0.08f, clampf(0.6f * R * fire + 1.f, 1.5f, 10.f), false, false);
  else {
    const int ns = (int)clampf(R * 14.f, 6.f, 70.f);
    for (int i = 0; i < ns; i++)
      spawn(p + vec3(0, 0.2f, 0), hd * r.range(3.f, 12.f) + sph() * r.range(4.f, 6.f + 6.f * sqrtf(R)) + vec3(0, r.range(1.f, 6.f), 0), r.range(0.3f, 0.9f), r.range(0.04f, 0.09f), -0.02f,
            vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, -9.f);
  }
  return dug;
}

void Game::breakUp(vec3 impactVel, bool water, bool air) {
  shake(1.f);
  // Capture the ordinary production pose before the caller clears velocity.
  // In particular, speed-dependent nose steering must not snap to its taxi angle.
  FrameParams captured{};
  fillPlaneVisual(captured.plane, plane, propAngle, false);
  somnusVisual(captured);
  wreckPlanePose = captured.plane;
  const AircraftSpec& s = *plane.spec;
  Rng r(1234 + (uint32_t)(flightClock * 100));
  float speed = length(impactVel);
  // the airframe comes apart into its own components (breakup.h): props, nacelles, gear legs, struts, the tail's halves,
  // the fins, the wings and the fuselage in three, each flying on what it is made of
  BreakPiece P[kMaxBreakPieces];
  const int n = breakPieces(s, plane.gear, plane.gearHeight(), P);
  DebrisBody B[kMaxBreakPieces];
  breakBodies(s, P, n, plane.mass(), plane.payload, B);
  wreck.clear();
  const vec3 centre = plane.pos + plane.q.rotate(s.special ? vec3() : aeroGeom(s).cg);   // (the airframe's centre of mass)
  float energy = clampf(speed / 50.f, 0.4f, 2.5f);
  airBreak = air;
  vec3 vRot = plane.q.rotate(plane.w);
  for (int i = 0; i < n; i++) {
    WreckPiece w;
    w.C = P[i].C; w.H = P[i].H; w.kind = P[i].kind; w.side = P[i].side; w.rigidOnly = P[i].rigidOnly; w.body = B[i];
    w.cutN = breakCuts(s, P, n, i, w.cut);
    w.q = plane.q;
    w.c = plane.pos + plane.q.rotate(B[i].cg);
    // what burns: the wings' tanks, the engines and the fuselage; the light pieces only smoulder
    const int k = P[i].kind;
    w.fire = k == BK_WING || k == BK_CENTRE ? r.range(0.7f, 1.f) : k == BK_NACELLE ? r.range(0.6f, 0.9f) : k == BK_NOSE ? r.range(0.35f, 0.65f) :
             k == BK_AFT ? r.range(0.2f, 0.4f) : k == BK_TAIL || k == BK_FIN || k == BK_CANARD ? r.range(0.05f, 0.15f) : 0.f;
    if (s.special) w.fire = std::max(w.fire, 0.6f);
    // (a small piece is flung harder, and spins faster)
    const float small = clampf(4.f / std::max(B[i].size, 0.5f), 0.6f, 2.f);
    vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
    if (air) {
      // in flight: every piece keeps the airframe's momentum and rotation, is flung apart by the failure and tumbles;
      // its own air takes it from there - nothing touches the ground (no crater) until the pieces fall out of the sky
      vec3 arm = w.c - centre, out = length(arm) > 0.1f ? normalize(arm) : vec3(0, 1, 0);
      w.v = plane.vel * r.range(0.96f, 1.f) + cross(vRot, arm) + out * (r.range(6.f, 14.f) * small) + vec3(r.range(-3, 3), r.range(-2, 4), r.range(-3, 3));
      w.w = vRot + ax * (r.range(1.5f, 4.f) * small);
    } else {
      vec3 out = w.c - centre; out.y = 0; out = length(out) > 0.1f ? normalize(out) : normalize(vec3(r.range(-1, 1), 0, r.range(-1, 1)));
      w.v = impactVel * r.range(0.25f, 0.45f) + out * (r.range(4.f, 9.f) * energy * small) + vec3(0, r.range(5.f, 11.f) * energy, 0);
      if (water) w.v = w.v * 0.4f;
      w.w = ax * (r.range(1.f, 4.f) * energy * small);
    }
    w.rest = false; w.landed = false;
    wreck.push_back(w);
  }
  // the pieces that burn hottest go off again over the next couple of seconds
  std::vector<int> hot;
  for (int i = 0; i < n; i++) if (wreck[i].fire > 0.5f) hot.push_back(i);
  if (hot.empty()) hot.push_back(n - 1);
  // torn skin and chunks of structure: plates that flutter down on their own air (a chunk of structure is heavier)
  debris.clear();
  auto fragment = [&](vec3 p, vec3 v, float size, bool charred, float sigma, float spin) {
    Debris d;
    d.p = p; d.v = v; d.size = size; d.charred = charred; d.rest = false;
    d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * spin;
    d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
    d.body = debrisPanel(1.2f * size, 2.f * size, sigma);   // (the plate drawn: 2 x 1.2 sizes - prims.glsl traceDebris)
    debris.push_back(d);
  };
  if (air) {
    for (int i = 0; i < 16; i++) {
      const bool ch = (i % 3) == 0;
      fragment(centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4)), plane.vel * r.range(0.6f, 0.95f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(10.f, 35.f),
               r.range(0.25f, 0.8f), ch, ch ? r.range(18.f, 35.f) : r.range(8.f, 16.f), r.range(6.f, 18.f));
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
    for (int i = 0; i < 6; i++) pops.push_back({vec3(), vec3(), r.range(0.35f, 2.6f), r.range(2.f, 4.5f), hot[i % hot.size()]});
    for (int i = 0; i < 20; i++) {   // extra torn skin panels
      const size_t at = debris.size();
      fragment(centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4)), plane.vel * r.range(0.5f, 0.9f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(15.f, 45.f),
               r.range(0.2f, 0.7f), (i % 2) == 0, r.range(4.f, 8.f), r.range(8.f, 22.f));
      debris[at].burn = i % 3 == 0 ? r.range(1.f, 3.f) : 0.f;
    }
    return;
  }
  for (int i = 0; i < 16; i++) {
    float a = r.range(0, 6.2832f);
    vec3 v = vec3(cosf(a), 0, sinf(a)) * r.range(6.f, 22.f) * energy + vec3(0, r.range(6.f, 18.f) * energy, 0) + impactVel * r.range(0.2f, 0.5f);
    if (water) v = v * 0.5f;
    const bool ch = (i % 3) == 0;
    fragment(centre + vec3(r.range(-2, 2), r.range(0.5f, 2.f), r.range(-2, 2)), v, r.range(0.25f, 0.8f), ch, ch ? r.range(18.f, 35.f) : r.range(8.f, 16.f), r.range(3.f, 12.f));
  }
  // crater (on land only): as the cube root of the blow - the aircraft's mass and its speed into the ground (a light
  // aircraft at 70 m/s: some 7 m across; an airliner or a research jet far faster: up to 20 m)
  const float vn = std::max(-impactVel.y, 0.f), Eh = 0.5f * plane.mass() * (vn * vn + 0.3f * std::max(speed * speed - vn * vn, 0.f));
  const float sE = cbrtf(std::max(Eh, 1.f));
  if (!water) {
    craterX = centre.x; craterZ = centre.z;
    craterR = clampf(0.06f * sE, 3.f, 20.f);
    craterD = craterR * 0.3f;
  } else craterR = 0;
  // the impact: a fireball (or a column of spray) as big as the fuel and the blow, the earth thrown up, and a couple
  // of secondary blasts as fuel goes up
  pops.clear();
  float R = clampf(2.f + 0.035f * sE + sqrtf(std::max(plane.fuel, 0.f)) * 0.12f, 4.f, 16.f);
  fireball(centre + vec3(0, 1.f, 0), impactVel * 0.15f, R, false, water);
  if (!water) {
    const int nd = (int)clampf(craterR * 8.f, 40.f, 180.f);
    for (int i = 0; i < nd; i++) spawn(centre, vec3(r.range(-12, 12), r.range(6, 20), r.range(-12, 12)) * (energy * 0.6f * clampf(craterR / 7.f, 0.8f, 2.2f)), r.range(1.5f, 3.f), r.range(1.2f, 2.6f) * clampf(craterR / 7.f, 0.8f, 2.f), 2.f, vec3(0.32f, 0.25f, 0.18f), 0.85f, SPR_SMOKE, 1.5f, -5.f);   // dirt
    bursts.push_back({centre + vec3(0, 0.3f, 0), vec3(craterR * 4.f, 0, 0), vec3(0, 0, craterR * 4.f), vec3(0.55f, 0.46f, 0.36f), 0.f});
    for (int i = 0; i < 3; i++) pops.push_back({vec3(), vec3(), r.range(0.4f, 2.2f), R * r.range(0.4f, 0.6f), hot[(size_t)r.range(0.f, hot.size() - 0.01f)]});
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
      if (!(wet && w.c.y < -1.f)) { fireball(w.c, w.v, pp.R, airBreak && !w.landed, wet); g_audio.triggerAt(wet ? SFX_SPLASH : SFX_CRASH, w.c, clampf(pp.R*.065f,.08f,.5f)); }
    }
  }
  for (WreckPiece& w : wreck) {
    // fire and smoke from the burning pieces (stronger right after the impact)
    float heat = w.fire * clampf(1.2f - crashTimer * 0.06f, 0.3f, 1.f);
    bool wet = g_world.height(w.c.x, w.c.z) < 0.5f;
    if (!wet && rand() % 100 < (int)(heat * 40)) {
      const vec3 he = (w.body.hi - w.body.lo) * 0.5f, ce = (w.body.hi + w.body.lo) * 0.5f - w.body.cg;   // (over what the piece is made of)
      vec3 jp = w.c + w.q.rotate(ce + vec3((rand() % 100 - 50) * 0.01f * he.x, 0, (rand() % 100 - 50) * 0.01f * he.z));
      spawn(jp, vec3(0, 2.f + (rand() % 100) * 0.02f, 0), 0.7f, 0.45f + 0.5f * heat, 0.8f, vec3(1.f, 0.42f, 0.1f) * 0.55f, 1.f, SPR_FIRE, 0.5f, 1.f);
    }
    bool sunk = wet && w.c.y < -1.5f, flown = false;
    if (!sunk && rand() % 100 < (int)(heat * 22)) spawn(w.c + vec3(0, 2.5f, 0), vec3((rand() % 100 - 50) * 0.02f, 3.5f, (rand() % 100 - 50) * 0.02f) + plane.windVel * 0.5f, 7.f, 1.5f, 2.5f, wet ? vec3(0.8f) : vec3(0.1f, 0.095f, 0.09f), 0.45f, SPR_SMOKE, 0.25f, 1.5f);
    if (w.rest) continue;
    if (wet && w.c.y < 0.3f) {
      if (!w.landed) {   // a piece falling into the sea: a splash as big as the blow
        w.landed = true;
        const float vn = std::max(-w.v.y, 0.f), E = 0.5f * w.body.mass * (vn * vn + 0.25f * std::max(dot(w.v, w.v) - vn * vn, 0.f));
        if (E > 1500.f && (airBreak || crashTimer > 0.3f)) partImpact(vec3(w.c.x, 0.2f, w.c.z), w.v, E, 0.f, true);
      }
      // in the sea: heavy drag, a short float on trapped air, then the piece sinks out of sight to the seabed
      float floatT = 1.f + 1.5f * w.fire;
      float vy = crashTimer < floatT ? (-0.35f - w.c.y) * 2.f : -2.2f - 1.2f * w.fire;
      w.v.x *= expf(-1.5f * dt); w.v.z *= expf(-1.5f * dt);
      w.v.y += (vy - w.v.y) * std::min(1.f, 3.f * dt);
      w.w = w.w * expf(-1.2f * dt);
      if (crashTimer > floatT && w.c.y > -12.f && rand() % 100 < 25)   // air escaping as it goes down
        spawn(vec3(w.c.x + (rand() % 100 - 50) * 0.03f, 0.05f, w.c.z + (rand() % 100 - 50) * 0.03f), vec3(0, 0.6f, 0), 0.9f, 0.35f, 1.5f, vec3(0.9f, 0.95f, 1.f), 0.45f, SPR_SMOKE, 1.f, 0.f);
    } else if (!w.landed) {
      // in the air, on its own: its surfaces' lift and drag as plates, its fuselage as bluff slices (breakup.h) - a wing
      // tumbles and drifts, an engine falls like a stone
      debrisStep(w.body, w.c, w.v, w.q, w.w, plane.windVel, isaDensity(std::max(w.c.y, 0.f)), dt);
      flown = true;
      // the smoke trail is a ribbon through one point per frame (continuous at any speed); the flame is drawn as a jet
      int k = (int)(&w - &wreck[0]);
      if (airBreak && k < kMaxBreakPieces && w.fire > 0.25f) {
        float heatT = clampf(1.3f - crashTimer * 0.08f, 0.35f, 1.f);
        pieceTrail[k].push_back({w.c, 0.f, (0.5f + 0.5f * w.fire) * heatT, 1});
        if (pieceTrail[k].size() > 600) pieceTrail[k].erase(pieceTrail[k].begin());
      }
    } else w.v.y -= G * dt;
    if (!flown) {
      w.v = w.v * expf(-0.08f * dt);
      w.c += w.v * dt;
      float wl = length(w.w);
      if (wl > 1e-4f) { w.q = quat::axisAngle(w.w, wl * dt) * w.q; w.q.normalize(); }
    }
    // ground contact against the corners of what the piece is made of
    float pen = 0; vec3 hitArm;
    const vec3 lo = w.body.lo - w.body.cg, hi = w.body.hi - w.body.cg;
    for (int k = 0; k < 8; k++) {
      vec3 cl((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
      vec3 cw = w.c + w.q.rotate(cl * 0.9f);
      float g = wreckGround(cw.x, cw.z);
      if (g - cw.y > pen) { pen = g - cw.y; hitArm = cw - w.c; }
    }
    if (pen > 0 && !w.landed && (airBreak || crashTimer > 0.3f)) {
      // first contact: the blow is the piece's own - its mass, and its speed into the ground with a little of its skid
      // (an impact's own pieces, thrown from the fireball, once they come down again). Where it digs a pit - under it -
      // or comes down in the crash's own crater, it drives itself into the ground there, a share of its own thickness
      // and of the pit's depth, and the earth takes its way: it stays where it struck, half buried
      w.landed = true;
      const float vn = std::max(-w.v.y, 0.f), E = 0.5f * w.body.mass * (vn * vn + 0.25f * std::max(dot(w.v, w.v) - vn * vn, 0.f));
      float dug = 0.f;
      if (E > 1500.f) dug = partImpact(vec3(w.c.x, wreckGround(w.c.x, w.c.z), w.c.z), w.v, E, w.fire, false);
      else if (craterR > 0.f && hypotf(w.c.x - craterX, w.c.z - craterZ) < craterR) dug = craterD;
      if (dug > 0.f) {
        const vec3 ext = hi - lo;
        w.bury = std::min(0.45f * std::min(ext.x, std::min(ext.y, ext.z)), 0.6f * dug);
        w.v.x *= 0.3f; w.v.z *= 0.3f;
      }
    }
    if (w.landed) w.groundT += dt;
    pen -= w.bury;   // (its crater's floor that far over its lowest point: it sinks in until it gets there)
    if (pen > 0) {
      // down on the ground: no rebound - the earth takes the blow - and it slides, rolls and turns to a stop, a piece
      // dug in far sooner; at rest once still and its pit is under it (dug the frame it struck, drawn the next)
      w.c.y += pen;
      if (w.v.y < 0) w.v.y = 0.f;
      const float grip = expf(-(w.bury > 0.f ? 14.f : 7.f) * dt);
      w.v.x *= grip; w.v.z *= grip;
      w.w = w.bury > 0.f ? w.w * expf(-12.f * dt) : w.w * expf(-4.f * dt) + cross(hitArm, vec3(w.v.x, 0, w.v.z)) * (1.2f * dt);
      if (w.groundT > 0.25f && length(w.v) < 0.8f && length(w.w) < 0.35f) w.rest = true;
    }
  }
  // in-flight break-up: the camera rides with the centre section; results come up a while after the last piece is down
  if (airBreak && !wreck.empty()) {
    plane.pos = wreck.back().c;   // (the centre section)
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
    float g = wreckGround(d.p.x, d.p.z);
    if (g >= 0.f || d.p.y >= 0.f) debrisStep(d.body, d.p, d.v, d.q, d.w, plane.windVel, isaDensity(std::max(d.p.y, 0.f)), dt);   // (a plate: it flutters and drifts down)
    else {   // (under the sea: sinking, below)
      d.v.y -= G * dt; d.p += d.v * dt;
      float wl = length(d.w);
      if (wl > 1e-4f) { d.q = quat::axisAngle(d.w, wl * dt) * d.q; d.q.normalize(); }
    }
    if (g < 0 && d.p.y < 0.f) {   // fragments in the sea flutter down to the seabed
      d.v = d.v * expf(-4.f * dt); d.v.y = std::max(d.v.y, -0.9f - d.size); d.w = d.w * expf(-1.f * dt);
      if (d.p.y < g + d.size * 0.15f) { d.p.y = g + d.size * 0.15f; d.rest = true; }
      continue;
    }
    if (d.p.y < g + d.size * 0.15f) {
      d.p.y = g + d.size * 0.15f;
      if (-d.v.y > 8.f) {   // a chunk coming down hard throws up a puff of earth (and, hard enough, digs a little pit)
        const float E = 0.5f * d.body.mass * d.v.y * d.v.y;
        const float dug = E > 4000.f ? partImpact(d.p, d.v, E, 0.f, false) : 0.f;
        if (dug > 0.f) {   // (one that dug its pit stays stuck in it, edge first, at the angle it struck, half buried)
          d.p.y = g - dug; d.v = vec3(); d.w = vec3(); d.rest = true;
          continue;
        }
      }
      d.v.y = 0.f; d.v.x *= 0.45f; d.v.z *= 0.45f; d.w = d.w * 0.5f;   // (no rebound: it skids and tumbles to a stop)
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
  // the air the airframe moves (cloud wisps): near it, the flow round it - lifted ahead of the wing, pushed aside by the
  // nose, blown back and turned by a slipstream, pushed down behind the wing and round its tips (aero_wake.h); once
  // behind the wing, a parcel close to one of the pair's vortices is caught by it and turns round it as the pair sinks
  const AeroWake& wk = plane.wake;
  const bool flow = plane.spec && !plane.ev.crashed && wk.V > 3.f;
  const AeroGeom* ag = flow && plane.spec->special == 0 ? &aeroGeom(*plane.spec) : nullptr;
  const float reach = std::max(wk.span, 4.f);
  for (size_t i = 0; i < particles.size();) {
    Particle& p = particles[i];
    // spawned this frame: emitters already place particles along the path flown this frame, so moving them by a
    // whole frame of velocity too would carry them that far ahead (at 25 fps and Mach 0.9, past the nose)
    if (p.fresh) { p.fresh = false; i++; continue; }
    p.life -= dt;
    if (p.life <= 0) { particles[i] = particles.back(); particles.pop_back(); continue; }
    p.v = p.v * expf(-p.drag * dt);
    p.v.y += p.buoy * dt;
    if (p.air && flow) {
      if (p.caught) { p.sw.c += p.v * dt; p.p = aeroSwirlStep(p.sw, p.p, dt); }
      else {
        const vec3 rb = plane.q.conj().rotate(p.p - plane.pos);
        if (fabsf(rb.x) < 1.5f * reach && fabsf(rb.y) < reach && rb.z > -1.5f * reach && rb.z < wk.pairZ + reach)
          p.p += plane.q.rotate(aeroWakeInduced(wk, ag, rb)) * dt;
        if (!p.passed && rb.z > wk.pairZ + 0.3f * reach) { p.passed = true; p.caught = aeroWakeCatch(wk, plane.pos, plane.q, p.p, p.sw); }
      }
    }
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
  // (the surfaces as the physics deflects them: the stick, the trim, the factory's rigging and any augmenter)
  if (s.special == 0 && p.flightTime > 0.f) { pv.Ctl[0] = p.ctlSurf.x; pv.Ctl[1] = p.ctlSurf.y; pv.Ctl[2] = p.ctlSurf.z; }   // (a display aircraft that never flew: its stick)
  else { pv.Ctl[0] = clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1); pv.Ctl[1] = clampf(p.ctl.roll, -1, 1); pv.Ctl[2] = clampf(p.ctl.yaw, -1, 1); }
  pv.Ctl[3] = p.ctl.throttle;
  float blur = s.engineType == ENG_JET ? 1.f : smoothstepf(45.f, 650.f, p.rpm);
  float wr, nr; modelWheelRadii(idx, wr, nr);
  pv.model = (int)(p.spec - kAircraft);
  pv.wheel[0] = p.wheelMotion[0].angle(wr); pv.wheel[1] = p.wheelMotion[1].angle(wr); pv.wheel[2] = p.wheelMotion[2].angle(nr);
  pv.Pr[0] = propAngle; pv.Pr[1] = blur; pv.Pr[2] = (float)std::max(s.blades, 2); pv.Pr[3] = p.flapLeft() - p.flaps;   // (a split flap: the left one's own)
  pv.I0[0] = p.ias * MS_TO_KT; pv.I0[1] = p.pos.y * M_TO_FT; pv.I0[2] = p.heading(); pv.I0[3] = p.vel.y * 196.85f;
  pv.I1[0] = p.pitchDeg(); pv.I1[1] = p.bankDeg();
  pv.I1[2] = s.engineType == ENG_PISTON ? p.rpm / std::max(s.maxRpm, 1.f) : p.n1 / 100.f; pv.I1[3] = p.fuel / std::max(s.maxFuel, 1.f);
  for (int i=0;i<4;i++) pv.engineHealth[i] = i<s.engines ? p.fail.engineHealth[i] : 0.f; // truthful per-engine cockpit failure indication
  pv.I2[0] = -p.q.rotate(p.w).y / DEG; pv.I2[1] = p.beta / DEG; pv.I2[2] = p.flaps; pv.I2[3] = p.gear;
  pv.colBase = s.colBase; pv.colStripe = s.colStripe;
  { std::string r = registrationOf(s); for (int i = 0; i < 3; i++) pv.reg[i] = (float)r[3 + i]; }
  pv.propCount = modelProps(md, pv.prop);
  pv.hud[0] = p.ias; pv.hud[1] = p.pos.y; pv.hud[2] = p.heading(); pv.hud[3] = p.mach;
  pv.hud2[0] = p.gLoad; pv.hud2[1] = p.ctl.throttle; pv.hud2[2] = p.spec && p.spec->special == 1 ? jetNozzleAngle(p) / (0.5f * PI) : p.nozzle;   // XR-30: pitch vectoring (90 deg units)
  pv.hud2[3] = p.gear > 0.5f ? 1.f : 0.f;
  vec3 vb = length(p.vel) > 2.f ? p.q.conj().rotate(normalize(p.vel)) : vec3(0, 0, -1);
  pv.hudV[0] = vb.x; pv.hudV[1] = vb.y; pv.hudV[2] = vb.z;
  pv.hud3[0] = p.engineSpool; pv.hud3[1] = p.alpha / DEG; pv.hud3[2] = p.vel.y; pv.hud3[3] = p.agl();
  pv.exhaust = buildExhaustVisual(p, md);
  pv.exhaust.style = s.special == 2 ? 1.f : 0.f;
  pv.flame[0] = pv.flame[1] = 0.f;
  for (int i = 0; i < pv.exhaust.count; ++i) {
    pv.flame[0] = std::max(pv.flame[0], pv.exhaust.power[i][0]);
    pv.flame[1] = std::max(pv.flame[1], pv.exhaust.power[i][1]);
  }
  pv.flame[2] = s.special == 1 ? jetNozzleAngle(p) : 0.f; pv.flame[3] = p.mach;
}

FrameParams Game::buildFrame() {
  FrameParams fp;
  fp.time = realTime; fp.dt = std::max(lastDt, 1e-4f);
  computeSun(timeOfDay, fp.sunDir, fp.sunCol, fp.night, plane.pos.y, &fp.sunDim);   // (the sun on the aircraft; the lighting pass works out the rest place by place)
  {   // the stars and the moon (sky.h): the sky turning with the sun's hour, the moon at today's phase
    const SkyBodies sb = skyBodiesAt(timeOfDay);
    for (int i = 0; i < 9; i++) fp.skyRot[i] = sb.toEquator[i];
    fp.moonDir = sb.moonDir; fp.moonLit = sb.moonLit;
  }
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
  // (in the rain the visibility follows how hard it is raining here: the heavier cells close it in)
  const bool flying = screen == SCR_FLIGHT && plane.spec;
  fp.fogB = 1.5f / std::max(wx.visibility * (flying && wx.precip ? 1.25f - 0.5f * rainNow : 1.f), 500.f);
  fp.wet = wx.precip == 1 ? 1.f : 0.f; fp.snow = wx.precip == 2 ? 0.8f : 0.f;
  fp.storm = wx.storm ? 1.f : 0.f; fp.lightning = lightning; fp.lightningTop = lightningTop; fp.lightningGlow = lightningGlow;
  fp.windOff = cloudOff; fp.cloudDet = cloudDet; fp.cloudBoil = cloudBoil;
  fp.wind = vec3(-sinf(wx.windFrom * DEG), 0, cosf(wx.windFrom * DEG)) * wx.windSpeed;
  fp.windSock = fp.wind;
  if (flying) {   // the windsocks swing with the gusts coming through (the bursts the aircraft meets, where the camera is)
    wxfield::Local calm; float g = std::max(g_world.height(camPos.x, camPos.z, 3), 0.f);
    fp.windSock = wxfield::wind(wx, calm, vec3(camPos.x, g + 8.f, camPos.z), 8.f, gameTime, plane.wxAir, 10.f).v;
    fp.windSock.y = 0;
  }
  // the aircraft's wake through the cloud: its points where the cloud field has carried them, the channel's radius
  // (the pair's oval, about its spacing either side, spreading with the turbulence as it ages), how far the vortex
  // pair has sunk, the distance along the path in Crow wavelengths (about 8.6 core spacings), the age, the pair as it
  // was there, and each segment's strength (opening in a moment, filling in over a minute, faded at the oldest end;
  // none across a gap)
  fp.wakeN = 0;
  static const bool wakeOff = getenv("WAKEOFF") != nullptr;   // (debug A/B: no wake through the cloud)
  if (flying && !wakeOff && !cloudWake.empty() && wx.cloudCover >= 0.02f) {
    std::vector<WakePt> pts = cloudWake;
    if (!wakeGap && !crashed) { const WakePt L = pts.back(); const vec3 hp = plane.pos + vec3(cloudOff.x, 0, cloudOff.y); pts.push_back(wakeHere(hp, false, L.odo + length(hp - L.c))); }
    const int n = std::min((int)pts.size(), FrameParams::kWakeMax), o = (int)pts.size() - n;
    const float span = plane.spec->span, crowL = 6.75f * span;
    vec3 lo(1e9f), hi(-1e9f); float rMax = 0, sMax = 0;
    for (int i = 0; i < n; i++) {
      const WakePt& w = pts[o + i];
      vec3 p = w.c - vec3(cloudOff.x, 0, cloudOff.y);
      float r = w.b0 * (1.05f + 0.2f * sqrtf(w.age)) + 1.f;
      float sink = clampf(w.w0 * w.T * (1.f - expf(-w.age / w.T)), -4.f * span, 4.f * span);
      fp.wake[i][0] = p.x; fp.wake[i][1] = p.y; fp.wake[i][2] = p.z; fp.wake[i][3] = r;
      fp.wakeP[i][1] = sink; fp.wakeP[i][2] = w.odo / crowL; fp.wakeP[i][3] = w.age;
      fp.wakeG[i][0] = w.gam; fp.wakeG[i][1] = w.b0; fp.wakeG[i][2] = w.tilt; fp.wakeG[i][3] = w.py;
      lo = vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)); hi = vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)); rMax = std::max(rMax, r); sMax = std::max(sMax, fabsf(sink));
      if (i + 1 < n) {
        const WakePt& v = pts[o + i + 1];
        float age = 0.5f * (w.age + v.age);
        fp.wakeP[i][0] = v.brk ? 0.f : smoothstepf(0.f, 0.7f, age) * std::min(1.f, 1.25f * expf(-age / 45.f)) * smoothstepf(0.f, 2.f, (float)i);
      } else fp.wakeP[i][0] = 0.f;
    }
    vec3 cen = (lo + hi) * 0.5f;
    fp.wakeB[0] = cen.x; fp.wakeB[1] = cen.y; fp.wakeB[2] = cen.z; fp.wakeB[3] = length(hi - lo) * 0.5f + 2.2f * rMax + sMax;   // (clouds.glsl: a point as far as 2.2 radii out may be in the channel's turbulent wall, the pair sunk away from the path)
    fp.wakeN = n;
    // the airframe's vortices and engines now (the young wake's: older, only the pair matters), from the pair's centre
    const AeroWake& wk = plane.wake;
    fp.wakeVN = fp.wakeEN = 0;
    for (int k = 0; k < wk.nv && fp.wakeVN < 10; k++) {
      const AeroWake::Vortex& v = wk.v[k];
      const bool winds = v.surf == AS_WING && v.gam * v.r.x * wk.gamPair > 0.f;   // (a wing vortex turning as its side's tip's: it winds into it)
      float* q = fp.wakeV[fp.wakeVN++]; q[0] = v.r.x; q[1] = v.r.y - wk.pairY; q[2] = v.gam; q[3] = winds ? 1.f : 0.f;
    }
    for (int e = 0; e < wk.nj && fp.wakeEN < 4; e++) {
      const AeroWake::Jet& j = wk.j[e];
      float* q = fp.wakeE[fp.wakeEN++]; q[0] = j.r.x; q[1] = j.r.y; q[2] = j.hot ? -j.R : j.R; q[3] = j.swirl;
    }
    fp.wakeA[0] = wk.gamPair; fp.wakeA[1] = wk.b0; fp.wakeA[2] = 7.f * std::max(wk.span, 1.f) / std::max(wk.V, 20.f); fp.wakeA[3] = 0.f;
    static const bool wakeDbg = getenv("WAKEDBG") != nullptr;   // (debug: the wake handed to the cloud pass)
    if (wakeDbg) {
      printf("wake: %d points, sphere r %.0f\n", n, fp.wakeB[3]);
      for (int i = 0; i < n; i++) printf("  %2d: %+7.0f %+6.0f %+7.0f from the aircraft, r %4.1f, K %.2f, sunk %4.1f, age %4.1f, density %.2f\n", i, fp.wake[i][0] - plane.pos.x, fp.wake[i][1] - plane.pos.y, fp.wake[i][2] - plane.pos.z, fp.wake[i][3], fp.wakeP[i][0], fp.wakeP[i][1], fp.wakeP[i][3], wxfield::cloudDensity(wx, vec3(fp.wake[i][0], fp.wake[i][1], fp.wake[i][2]), true));
    }
  }
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
      static_assert(kWreckPieces == kMaxBreakPieces && kWreckCuts == kMaxBreakCuts, "the renderer's wreck limits are the breakup's");
      wv.pieces = std::min((int)wreck.size(), kWreckPieces);
      std::fill(std::begin(wv.gearOwner), std::end(wv.gearOwner), -1);
      for (int i = 0; i < wv.pieces; i++) {
        const WreckPiece& w = wreck[i];
        wv.pos[i] = w.c - w.q.rotate(w.body.cg); wv.C[i] = w.C; wv.H[i] = w.rigidOnly ? vec3(-1.f) : w.H;
        if (w.kind == BK_GEAR) wv.gearOwner[w.side < 0 ? 0 : w.side > 0 ? 1 : 2] = i;
        wv.mid[i] = wv.pos[i] + w.q.rotate((w.body.lo + w.body.hi) * 0.5f); wv.rad[i] = 0.5f * length(w.body.hi - w.body.lo) + 0.5f;
        wv.cutN[i] = w.cutN;
        wv.burn[i] = clampf(w.fire * (0.15f + crashTimer / (airBreak ? 8.f : 2.f)), 0.f, 1.f);   // (an impact's fireball chars at once; a failure in the air as the pieces burn)
        for (int k = 0; k < w.cutN; k++) { const BreakCut& c = w.cut[k]; float* o = wv.cut[i][k]; o[0] = c.C.x; o[1] = c.C.y; o[2] = c.C.z; o[3] = c.H.x; o[4] = c.H.y; o[5] = c.H.z; }
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
    refreshGroundPits();   // (the pits drawn are the pits the aircraft, the weapons and the wreckage meet)
    fp.wreck.craterN = 0;
    for (const GroundPit& p : g_groundPits) { float* c = fp.wreck.crater[fp.wreck.craterN++]; c[0] = p.x; c[1] = p.z; c[2] = p.R; c[3] = p.D; }
    vec3 fwd = camMode == 1 ? plane.q.rotate(quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1))))
                            : normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - camPos);
    vec3 upRef = camMode == 1 ? plane.up() : vec3(0, 1, 0);
    if (camMode == 0) upRef = normalize(lerp(vec3(0, 1, 0), camQ.rotate(vec3(0, 1, 0)), 0.3f));
    if (photo.on) { camPos = photo.pos; fwd = photoForward(); upRef = photoUp(); }   // (photo mode's free camera)
    if (dbgCam && dbgFollow) { dbgCamPos = plane.pos + dbgFollowOff; dbgCamLook = plane.pos; }
    if (dbgCam) { camPos = dbgCamPos; fwd = normalize(dbgCamLook - dbgCamPos); upRef = vec3(0, 1, 0); }
    if (screen == SCR_FLIGHT && !dbgCam && !photo.on && camMode != 3 && set.camShake > 0.f) {   // (the shake: small turns of the view, 2-5 Hz)
      const float a = (shakeTrauma * shakeTrauma + shakeBuffet) * set.camShake * (camMode == 1 ? 0.7f : 1.f);
      if (a > 1e-4f) {
        const float t = shakeT;
        auto n = [&](float f, float ph) { return sinf(t * f + ph) * 0.6f + sinf(t * f * 2.31f + ph * 1.7f) * 0.4f; };
        const vec3 right = normalize(cross(fwd, upRef));
        fwd = normalize(fwd + right * (n(23.f, 0.f) * a * 1.1f * DEG) + upRef * (n(29.f, 1.3f) * a * 1.4f * DEG));
        upRef = normalize(upRef + right * (n(19.f, 2.1f) * a * 0.9f * DEG));
      }
    }
    fp.camPos = camPos;
    fp.camBack = -fwd;
    fp.camRight = normalize(cross(fwd, upRef));
    fp.camUp = cross(fp.camRight, fwd);
    fp.fovY = (camMode == 1 ? set.fov + 19.f : set.fov) * DEG;
    if (camMode == 3) fp.fovY = clampf(2.f * atanf(std::max(plane.spec->span, plane.spec->fusLen) * (botControl ? 0.42f : 1.5f) / length(plane.pos - camPos)), 4.f * DEG, 60.f * DEG);
    if (camMode == 1) fp.fovY = 2.f * atanf(tanf((set.fov + 19.f) * 0.5f * DEG) / ckZoom);   // cockpit zoom: lean in to read the displays
    if (camMode == 0 && set.speedFov && screen == SCR_FLIGHT && plane.spec)   // (the chase view widens a little with speed: up to 5 deg past cruise)
      fp.fovY += smoothstepf(0.6f, 1.6f, length(plane.vel) / std::max(plane.spec->cruise, 20.f)) * 5.f * DEG;
    if (screen == SCR_LOADING && militaryFlight) militaryLoadingCamera(fp);
    if (photo.on) {   // (photo mode: its lens, exposure, vignette, grade and frame)
      fp.fovY = photo.fov * DEG;
      fp.exposure *= exp2f(photo.ev);
      fp.vignette = photo.vignette;
      fp.grade[0] = photo.saturation; fp.grade[1] = photo.contrast; fp.grade[2] = photo.warmth;
      const float aspect = (float)std::max(g_ren.W, 1) / std::max(g_ren.H, 1);
      static const float kFrame[4] = {0.f, 2.39f, 1.85f, 1.f};   // (none, the cinema's, the wide screen's, a square)
      const float want = kFrame[std::clamp(photo.frame, 0, 3)];
      fp.bars[0] = want > 0.f && want < aspect ? 0.5f * (1.f - want / aspect) : 0.f;
      fp.bars[1] = want > 0.f && want > aspect ? 0.5f * (1.f - aspect / want) : 0.f;
    }
    g_audio.setView(fp.camPos,fp.camRight,fp.camUp,-fp.camBack);
    fp.landLight = landingLight && plane.engineRunning ? (0.3f + 0.7f * fp.night) : 0.f;
    fp.landLightPos = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
    fp.landLightDir = normalize(plane.forward() - plane.up() * 0.1f);
    somnusVisual(fp);
    if (!wreck.empty()) {
      // Freeze articulation while preserving the moving world frame:
      // nearby-wreck shadow selection still needs its current position.
      const vec3 worldPos = fp.plane.pos;
      float worldRot[9]; memcpy(worldRot, fp.plane.rot, sizeof worldRot);
      fp.plane = wreckPlanePose; fp.plane.pos = worldPos;
      memcpy(fp.plane.rot, worldRot, sizeof worldRot);
    }
    if (!(screen == SCR_LOADING && militaryFlight)) buildFeedCameras(fp);
    if (screen == SCR_LOADING && !headless) fp.warmCockpit = feedRigOf(plane.spec);   // (the cockpit ready before the flight opens: raster_renderer.cpp)
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
    // (the scenery the craters flattened: refreshGroundPits)
    // the windscreen (cockpit view): the rain here and the cloud's mist on it, streaming away on screen from where
    // the air meets the glass - a little below where the nose points, so over the roof; out of a side window, aft
    fp.rainLens = 0; fp.glassMist = 0;
    // (not in the research jets' sealed pods: their windows are displays, and with no glass to bead on, the drops
    // streamed over the cabin's own frames and panels - more than 1.3 m from the eye, the depth the post pass takes
    // for "seen through the glass" - in the XR-40 at night in a storm)
    if (camMode == 1 && !crashed && !plane.spec->special && !(screen == SCR_LOADING && militaryFlight)) {
      fp.rainLens = wx.precip == 1 ? rainNow : 0.f;
      fp.glassMist = mistNow;
      vec3 f = normalize(plane.forward() - plane.up() * 0.45f);
      float x = dot(f, fp.camRight), y = dot(f, fp.camUp), z = dot(f, -fp.camBack);
      float ty = tanf(fp.fovY * 0.5f), tx = ty * (float)g_ren.W / std::max(g_ren.H, 1);
      float sign = 1.f;
      if (z < 0.f) { x = -x; y = -y; z = -z; sign = -1.f; }   // (the source behind: the drops run towards the point ahead the air goes)
      vec2 ndc = z > 0.02f ? vec2(x / z / tx, y / z / ty) : vec2(x, y) * (1.f / std::max(length(vec2(x, y)), 1e-3f)) * 50.f;
      if (length(ndc) > 50.f) ndc = ndc * (50.f / length(ndc));
      fp.rainFlow[0] = 0.5f + 0.5f * ndc.x; fp.rainFlow[1] = 0.5f + 0.5f * ndc.y; fp.rainFlow[2] = sign;
      fp.rainFlow[3] = smoothstepf(6.f, 45.f, plane.airspeed);
    }
    fp.sealedCockpit = camMode == 1 && plane.spec->special && !crashed && !(screen == SCR_LOADING && militaryFlight);
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

static const float kHubOrbitR = 2200.f, kHubOrbitRate = 0.035f;   // (the hub's camera: radius, radians a second)
int Game::hubField() const { return g_world.regionId == career.region && career.location < (int)g_world.airports.size() ? career.location : -1; }
vec3 Game::hubOrbitAt(vec3 centre) const { const float t = realTime * kHubOrbitRate; return centre + vec3(cosf(t) * kHubOrbitR, 0, sinf(t) * kHubOrbitR); }

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
  // and round the hub's camera (menuBackgroundCamera's orbit of its field), which the menu cuts to: what stands within
  // 700 m of a camera is built on the game's thread when missing, some three dozen chunks in the cut's first frame
  if (const int ap = hubField(); ap >= 0) { fp.prefetchNearR = 1500.f; fp.prefetchNearPos = hubOrbitAt(g_world.airports[ap].pos()); }
  // the aircraft passes over the airfield at mid-shot, on a heading turned from the runway's
  float hd = a.heading + S.turn;
  vec3 dir(sinf(hd * DEG), 0, -cosf(hd * DEG)), right = normalize(cross(dir, vec3(0, 1, 0)));
  float v = std::min(kAircraft[S.craft].cruise, kAircraft[S.craft].engineType == ENG_JET ? 140.f : 75.f);
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
  for (int i = 0; i < kAircraftCount; i++) if (allCraft || !kAircraft[i].special) { todo.push_back({i, false}); todo.push_back({i, true}); }
  return todo;
}
std::string Game::prewarmLabel(int craft, bool inside, int stage) {
  const std::string what = std::string("the ") + kAircraft[craft].name + (inside ? "'s cockpit" : "'s body");
  switch (stage) {
    case 1: return "Loaded " + what + ", ready-built";
    case 2: return "Loaded " + what + " from this PC's cache";
    case 3: return "Building " + what + "  (once: kept for the next launch)";
    case 4: return std::string("Compiling the ") + kAircraft[craft].name + "'s shader programs  (once after an update)";
    default: return "Preparing " + what;
  }
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
  int firstPending = 0;
  for (int i = 0; !quit; i++) {
    realTime = 3.f; frame();
    firstPending = std::max(firstPending, g_ren.entPending);
    const bool ready = i >= 2 && g_ren.entPending == 0 && !loadingShadowPending();
    if (pace) {
      const float chunks = firstPending > 0 ? 1.f - float(g_ren.entPending) / firstPending : (i >= 2 ? 1.f : 0.f);
      pace->setSub(ready ? 1.f : std::min(0.95f, 0.8f * chunks + (loadingShadowPending() ? 0.f : 0.15f)));
      progress(pace->fraction(), fmt("Preparing menu scenery  (%d chunks remaining)%s", g_ren.entPending, loadingShadowPending() ? " / baking terrain lighting" : ""));
    }
    if (ready) break;
  }
  g_ren.entSync = sync;
  // every light aircraft's body (outside, and the cockpit's when that is in use; with allCraft the research jets' too):
  // a frame that wants one bakes it, or loads it from the cache
  const std::vector<std::pair<int, bool>> todo = prewarmItems(allCraft);
  for (size_t k = 0; k < todo.size() && !quit; k++) {
    prewarmCraft = todo[k].first; prewarmInside = todo[k].second;
    if (pace && itemSteps && k < itemSteps->size()) pace->begin((*itemSteps)[k]);
    // worded as it goes: where its body came from (ready-built, this PC's cache, or built), and its shader programs
    // compiling - after an update, the most of this step's time
    const std::string count = fmt("   %d of %d", (int)k + 1, (int)todo.size());
    bool saying = false;   // (the launch's progress draws a frame, which may compile a program of its own: not said again)
    auto say = [&](int stage) {
      if (saying) return;
      saying = true; progress(pace ? pace->fraction() : 0.35f + 0.65f * k / todo.size(), prewarmLabel(prewarmCraft, prewarmInside, stage) + count); saying = false;
    };
    say(0);
    g_ren.onBodyLoaded = [&](bool packaged) { say(packaged ? 1 : 2); };
    g_ren.onBakeStart = [&] { say(3); };
    g_ren.onMeshProgress = [&](float f,const std::string& task) {
      if(pace){pace->setSub(f);pace->task(task,1,1);}say(3);
    };
    g_ren.onMeshBatch = [&](int done,int all,const std::string& task) {if(pace)pace->task(task,float(done),float(all));};
    g_onProgramBuild = [&](const std::string&) { say(4); };
    realTime = 3.f; frame();   // bakes it at the end of the frame, if this view uses one
    if(pace)pace->setSub(1.f);
    g_ren.onBodyLoaded = nullptr; g_ren.onBakeStart = nullptr; g_ren.onMeshProgress = nullptr; g_ren.onMeshBatch = nullptr; g_onProgramBuild = nullptr;
  }
  prewarmCraft = -1; prewarmInside = false;
  realTime = 0.f;
  if (!quit) {
    if (pace) pace->end();
    progress(pace ? pace->fraction() : 1.f, "Ready");
  }
}

void Game::menuBackgroundCamera(FrameParams& fp) {
  if (screen == SCR_MENU && !getenv("MENUORBIT")) { menuTour(fp); return; }
  if (screen == SCR_RESEARCH) { researchPreviewCamera(fp); return; }
  if (screen == SCR_HUB && hubTab == TAB_CONTRACTS && hubList == 2) { militaryPreviewCamera(fp); return; }
  if (screen == SCR_FREE_FLIGHT || (screen == SCR_HUB && hubTab == TAB_HANGAR)) { hangarPreviewCamera(fp); return; }
  // An Ambler circles over the islands while the camera chases it in a slow arc
  static Plane demo;
  static bool initd = false;
  if (!initd) { demo.reset(&kAircraft[screen == SCR_MENU ? 1 : 0], vec3(0, 600, 0), 0, 50, 100, true, 60); initd = true; }
  const int ap = hubField();
  vec3 centre = screen == SCR_MENU || ap < 0 ? vec3(-6000, 0, 11000) : g_world.airports[ap].pos();
  const float t = realTime * kHubOrbitRate;
  float alt = (screen == SCR_MENU ? 380.f : 320.f) + std::max(centre.y, 0.f);
  vec3 p = hubOrbitAt(centre); p.y = std::max(alt, g_world.height(p.x, p.z) + 250.f);
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

// A dedicated showroom is isolated from the flight simulation. Selection changes only the
// displayed airframe; purchasing, fuel, wear and the active flight remain untouched.
void Game::hangarPreviewCamera(FrameParams& fp) {
  const int craft = screen == SCR_FREE_FLIGHT ? validCareerSelection(freeCraft) : std::clamp(selHangar, 0, kAircraftCount - 1);
  hangarView.select(craft, screen == SCR_FREE_FLIGHT);
  const AircraftSpec& sp = kAircraft[craft];
  static Plane demo;
  if (demo.spec != &sp) demo.reset(&sp, vec3(0), 0, sp.maxFuel, 85, false, 0);
  // Spawn clearance is for physics to settle; a static showroom must sit on the
  // rendered tyres immediately. The main-wheel centres are (wr-gh, mainZ).
  // gearTailShape uses (-gh + .11*L + .1, tailZ), radius .1. Solve the
  // main/tail bottom heights together; pitching about their X axles leaves each
  // circular tyre's vertical support radius unchanged. Do not use the physics
  // tail contact approximation here or change any aircraft geometry/flight pose.
  const float gh = demo.gearHeight(), wr = kModels[craft].wheelR;
  float pitch = 0.f, height = gh;
  if (sp.taildragger) {
    const GearStations stations = gearStations(sp);
    const float dy = .11f*sp.fusLen + .1f - wr, dz = stations.tailZ - stations.mainZ;
    const float r = sqrtf(dy*dy + dz*dz);
    pitch = atan2f(dy, dz) + asinf(clampf((wr-.1f)/r, -1.f, 1.f));
    height = wr - (wr-gh)*cosf(pitch) + stations.mainZ*sinf(pitch);
  }
  // Turn the parked airframe about world-up, rather than orbiting through the
  // showroom's opaque walls. Yaw after parking pitch preserves all tyre heights.
  demo.q = quat::axisAngle(vec3(0, 1, 0), -(hangarView.yaw-MenuPreviewView::defaultYaw)) * quat::axisAngle(vec3(1, 0, 0), pitch);
  demo.pos = vec3(0, height, 0);
  demo.vel = vec3(0); demo.onGround = true; demo.engineRunning = false;
  demo.gear = 1.f; demo.flaps = 0; demo.nozzle = 0; demo.rpm = 0; demo.engineSpool = 0; demo.n1 = 0;
  demo.ctl = Controls(); demo.ctl.throttle = 0; demo.ctl.gearDown = true;
  fillPlaneVisual(fp.plane, demo, 0.f, false);
  fp.hangarPreview = true; fp.hangarClassified = hangarResearchLocked(craft); fp.hangarOrigin = vec3(0);
  fp.hangarSize = std::max(12.f, std::max(sp.span, sp.fusLen));
  fp.prefetchOn = false; fp.cloudCover = 0; fp.fogB = 0; fp.wet = 0; fp.snow = 0; fp.storm = 0;
  fp.lightning = 0; fp.wind = vec3(0); fp.windSock = vec3(0); fp.planeTerrSh = 1;
  fp.sunDir = normalize(vec3(-0.25f, 0.65f, -0.72f)); fp.sunCol = vec3(0.30f, 0.34f, 0.39f); fp.night = 0;
  fp.trafficN = 0; fp.ufoOn = false; fp.feedRig = 0; fp.dispMode = 0; fp.landLight = 0;
  fp.plN = 0;
  for (int side : {-1, 1}) for (float z : {-0.35f, 0.45f}) {
    FrameParams::PointLight& light = fp.pl[fp.plN++];
    light.pos = vec3(side * 0.55f, 0.645f, z) * fp.hangarSize;
    light.radius = fp.hangarSize * 0.045f;   // emitter size, not a light range
    light.col = vec3(0.72f, 0.85f, 1.f) * (fp.hangarSize * fp.hangarSize * 0.30f);
    light.cosCut = -2; light.dir = vec3(0, -1, 0); light.shadow = side == 1 && z < 0 ? fp.hangarSize * 1.5f : 0;
  }
  if (fp.hangarClassified) {
    // The dedicated silhouette pass ignores materials/emission as well; keep the
    // physical scene entirely shut down so no effects can reveal the airframe.
    fp.plN = 0; fp.sunCol = vec3(0); fp.sealedCockpit = true;
    fp.plane.lensN = 0; fp.plane.exhaust = ExhaustVisual{};
    std::fill(&fp.plane.flame[0], &fp.plane.flame[0]+4, 0.f);
    std::fill(&fp.plane.vapor[0], &fp.plane.vapor[0]+4, 0.f);
    std::memset(fp.plane.wr, 0, sizeof(fp.plane.wr));
  }
  const float s = S(), W = float(std::max(g_ren.W, 1)), H = float(std::max(g_ren.H, 1));
  const HangarLayout L = hangarLayout(24*s, 126*s, W-48*s, H-146*s, s);
  const float cx = L.previewX + L.previewWidth * 0.5f, cy = L.previewY + L.previewHeight * 0.5f;
  const float radius = std::max(sp.span, sp.fusLen) * 0.65f;
  const float pixels = std::max(1.f, std::min(L.previewWidth, L.previewHeight) * 0.40f);
  const vec3 focus = demo.pos + vec3(0, sp.fusRad * 0.45f, 0);
  const float elevation = clampf(hangarView.pitch,.04f,.35f);
  const vec3 viewingRay(sinf(MenuPreviewView::defaultYaw)*cosf(elevation),sinf(elevation),-cosf(MenuPreviewView::defaultYaw)*cosf(elevation));
  // Keep the eye below the lowest roof beams and inside the side walls even on
  // tall/narrow windows. Zoom changes only the lens, never this safe position.
  const float roomDistance = std::min((fp.hangarSize * .54f - focus.y) / viewingRay.y,
                                      fp.hangarSize * 1.10f / viewingRay.x);
  const float fitDistance = H * .5f * radius / (pixels * tanf(17.f * DEG));
  const float distance = std::max(radius * 1.25f, std::min(fitDistance, roomDistance));
  // A close camera needs the sphere's angular radius, not its flat radius/distance.
  // This keeps elevated views inside the stage even near the showroom roof.
  const float tangentDistance=std::sqrt(std::max(distance*distance-radius*radius,radius*radius*.25f));
  const float th = H * .5f * radius / (pixels * tangentDistance * clampf(hangarView.zoom, MenuPreviewView::minZoom, MenuPreviewView::maxZoom));
  fp.fovY = 2.f * atanf(th);
  const vec3 C = focus + viewingRay * distance;
  const float nx = (cx-W*.5f)/(W*.5f), ny = (H*.5f-cy)/(H*.5f);
  const auto axes=menuPreviewAxes(focus-C,nx*th*W/H,ny*th);
  fp.camPos=C;fp.camBack=-axes.forward;fp.camRight=axes.right;fp.camUp=axes.up;
  fp.exposure = 1.f; fp.vignette = 0.32f;
}

// The military terminal shows the exact mission-assigned airframe through the research scene's native renderer.
// Its demo plane never enters the career fleet or the active flight session.
void Game::militaryPreviewCamera(FrameParams& fp) {
  const Contract c = career.militaryContract(militarySelection);
  const bool classified = !career.militaryMissionUnlocked(militarySelection);
  const int craft = c.forceAircraft >= 0 && c.forceAircraft < kAircraftCount ? c.forceAircraft : 0;
  militaryView.select(craft,militarySelection);
  const AircraftSpec& sp = kAircraft[craft];
  const Airport& a = g_world.airports[std::clamp(c.from, 0, (int)g_world.airports.size()-1)];
  Plane demo; demo.reset(&sp, a.pos(), a.heading+180.f, sp.maxFuel, 85, true, sp.cruise);
  demo.pos = a.pos() + a.dir()*1800.f;
  demo.pos.y = std::max(a.elev, g_world.height(demo.pos.x,demo.pos.z)) + 650.f;
  demo.gear = 0; demo.flaps = demo.nozzle = 0; demo.rpm = 0; demo.ctl.throttle = .45f;
  fillPlaneVisual(fp.plane,demo,0,false);
  const ResLayout L = militaryLayout();
  const float W = std::max(g_ren.W,1), H = std::max(g_ren.H,1);
  fp.fovY = 32.f*DEG; const float th = tanf(fp.fovY*.5f);
  const float s=std::min(S(),std::min(H/720.f,W/1120.f));
  const float pixels=std::max(1.f,.40f*std::min(L.px1-L.px0,L.bot-L.top-135*s));
  const float D=H*.5f*std::max(sp.span,sp.fusLen)*.65f/(pixels*th*clampf(militaryView.zoom,MenuPreviewView::minZoom,MenuPreviewView::maxZoom));
  const float yaw = militaryView.yaw, pitch = clampf(militaryView.pitch,-.5f,1.f);
  const vec3 P = demo.pos, C = P+demo.q.rotate(vec3(sinf(yaw)*cosf(pitch),sinf(pitch),-cosf(yaw)*cosf(pitch)))*D;
  const auto axes=menuPreviewAxes(P-C,(L.cx-W*.5f)/(W*.5f)*th*W/H,(H*.5f-L.cy)/(H*.5f)*th);
  fp.camPos=C;fp.camBack=-axes.forward;fp.camRight=axes.right;fp.camUp=axes.up;
  fp.vignette=.45f; fp.prefetchOn=true; fp.prefetchPos=demo.pos;
  fp.trafficN=0; fp.ufoOn=false; fp.feedRig=fp.dispMode=0;
  // Reuse the hangar's geometry-only outline. Registrations, paint, cockpit
  // screens and effects cannot leak into a classified preview.
  // FrameParams is rebuilt each frame, so an unlocked selection immediately returns
  // to its normal outdoor aircraft render without retaining classified materials.
  if (classified) {
    fp.hangarPreview = fp.hangarClassified = true;
    // Keep the whole interactive orbit inside the room. A normal-sized showroom
    // puts its opaque walls between the long terminal camera and the aircraft.
    fp.hangarSize = std::max(12.f,D*3.f);
    fp.hangarOrigin = demo.pos-vec3(0,fp.hangarSize*.28f,0);
    fp.prefetchOn=false; fp.cloudCover=fp.fogB=fp.wet=fp.snow=fp.storm=0;
    fp.lightning=0; fp.wind=fp.windSock=vec3(0); fp.planeTerrSh=1;
    fp.plN=0; fp.sunCol=vec3(0); fp.sealedCockpit=true;
    fp.landLight=0; fp.plane.lensN=0; fp.plane.exhaust=ExhaustVisual{};
    std::fill(std::begin(fp.plane.flame),std::end(fp.plane.flame),0.f);
    std::fill(std::begin(fp.plane.vapor),std::end(fp.plane.vapor),0.f);
    std::memset(fp.plane.wr,0,sizeof(fp.plane.wr));
    // The terminal's olive glass is darker than the civilian hangar overlay.
    // Lift only this material-free outline so it stays discernible there.
    fp.exposure=3.f; fp.vignette=.25f;
  }
}

// A loading preview is a camera over the actual frozen sortie, never a substitute aircraft or a separate simulation.
// The same frame still streams the mission world and contributes to the existing readiness checks.
void Game::militaryLoadingCamera(FrameParams& fp) {
  const AircraftSpec& sp = *plane.spec;
  fillPlaneVisual(fp.plane,plane,propAngle,false);
  fp.dispMode=fp.feedRig=0; fp.fovY=32.f*DEG;
  const float W=std::max(g_ren.W,1), H=std::max(g_ren.H,1), th=tanf(fp.fovY*.5f);
  const LoadingLayout L=loadingLayout(W,H);
  const float D=std::max(sp.span,sp.fusLen)*2.7f;
  const vec3 P=plane.pos, C=P+plane.right()*(D*.7f)+plane.forward()*(D*.65f)+vec3(0,D*.3f,0);
  const vec3 f=normalize(P-C), r=normalize(cross(f,vec3(0,1,0))), u=cross(r,f);
  const float centerY=(L.artTop+L.artBottom)*.5f, ny=(H*.5f-centerY)/(H*.5f);
  const vec3 aim=P-r*(.1f*D*th*W/H)-u*(ny*D*th);
  const vec3 look=normalize(aim-C);
  fp.camPos=C; fp.camBack=-look; fp.camRight=normalize(cross(look,vec3(0,1,0))); fp.camUp=cross(fp.camRight,look);
  fp.prefetchOn=true; fp.prefetchPos=plane.pos; fp.vignette=.35f;
}

// The preview: the selected craft hangs in the air over the chosen launch site, at the sortie's time of day, and the
// camera orbits it (drag to turn, wheel to zoom) framed so it sits in the middle of the terminal's preview ring
int resCraftCount(); int resCraftAt(int k);   // game_research_ui.cpp
void Game::researchPreviewCamera(FrameParams& fp) {
  researchView.select(resCraft);
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
  if (sp.special == 2) {
    // Preview the chosen hardware, not the live flight's old loadout. This is
    // visual-only: no ammunition, encounter or career state changes in the terminal.
    fp.plane.wr[6][3] = float(practiceForward);
    fp.plane.wrBombSet = int(practiceBomb);
    fp.plane.wr[6][0] = 1.f;
    if (!resWarm && resCard < 0) {
      fp.plane.wr[4][1] = 1.f; // open payload bay for equipment inspection
      fp.plane.wr[4][2] = 1.f; // deployed forward hardware, weapons remain safe
    }
  }
  resPrevPos = P; resPrevQ = demo.q;
  // orbit
  ResLayout L = researchLayout();
  float W = (float)g_ren.W, H = (float)g_ren.H;
  float ext = std::max(sp.span, sp.fusLen) * 0.65f;   // (the craft fills the ring)
  fp.fovY = 32.f * DEG;
  float th = tanf(fp.fovY * 0.5f);
  const float pixels=std::max(1.f,.40f*std::min(L.px1-L.px0,L.bot-L.top-56*S()));
  float D = (H * 0.5f / pixels) * ext / th / clampf(researchView.zoom,MenuPreviewView::minZoom,MenuPreviewView::maxZoom);
  float yaw = researchView.yaw, pit = clampf(researchView.pitch,-.5f,1.f);
  if (!resAirborne) pit = std::max(pit, 0.04f);   // (on the ground the camera never dips below the apron)
  vec3 C = P + demo.q.rotate(vec3(sinf(yaw) * cosf(pit), sinf(pit), -cosf(yaw) * cosf(pit))) * D;
  if (!resAirborne) C.y = std::max(C.y, g_world.height(C.x, C.z) + 1.6f);
  const auto axes=menuPreviewAxes(P-C,(L.cx-W*.5f)/(W*.5f)*th*W/H,(H*.5f-L.cy)/(H*.5f)*th);
  fp.camPos=C;fp.camBack=-axes.forward;fp.camRight=axes.right;fp.camUp=axes.up;
  fp.vignette = 0.75f;
}

// The blended sprites far to near, all of them together: the volcano's plume came after the sorted smoke and a distant
// plume drew over nearer smoke and trails (the review of v3.44.0, RND-5). Each quad is six vertices, placed by its middle
void sortAlphaQuads(std::vector<SpriteVert>& v, vec3 cam) {
  const size_t n = v.size() / 6;
  if (n < 2) return;
  std::vector<std::pair<float, uint32_t>> order(n);
  for (size_t i = 0; i < n; i++) {
    const SpriteVert* q = &v[i * 6];
    const vec3 c((q[0].x + q[1].x + q[2].x + q[5].x) * 0.25f, (q[0].y + q[1].y + q[2].y + q[5].y) * 0.25f, (q[0].z + q[1].z + q[2].z + q[5].z) * 0.25f);
    order[i] = {-length(c - cam), (uint32_t)i};
  }
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<SpriteVert> out; out.reserve(v.size());
  for (auto& o : order) out.insert(out.end(), v.begin() + o.second * 6, v.begin() + o.second * 6 + 6);
  out.insert(out.end(), v.begin() + n * 6, v.end());   // (a tail that isn't a whole quad, as it was)
  v.swap(out);
}

void Game::buildSprites(const FrameParams& fp, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add) {
  vec3 cr = fp.camRight;
  // billboards are turned to face whichever camera draws them (the main view or a cockpit camera) in the vertex shader
  // (kind: the sprite's kind, and in its fraction - under 0.4 - a value of its own: a puff's pattern, a splash's age)
  auto bill = [&](std::vector<SpriteVert>& v, vec3 p, float s, vec3 col, float a, float kind, float soft) {
    SpriteVert q[4] = {{p.x, p.y, p.z, 0, 0, col.x, col.y, col.z, a, kind, soft, s}, {p.x, p.y, p.z, 1, 0, col.x, col.y, col.z, a, kind, soft, s},
                       {p.x, p.y, p.z, 1, 1, col.x, col.y, col.z, a, kind, soft, s}, {p.x, p.y, p.z, 0, 1, col.x, col.y, col.z, a, kind, soft, s}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  auto quadAx = [&](std::vector<SpriteVert>& v, vec3 p, vec3 ax, vec3 ay, vec3 col, float a, float kind, float soft) {
    vec3 c0 = p - ax - ay, c1 = p + ax - ay, c2 = p + ax + ay, c3 = p - ax + ay;
    SpriteVert q[4] = {{c0.x, c0.y, c0.z, 0, 0, col.x, col.y, col.z, a, kind, soft}, {c1.x, c1.y, c1.z, 1, 0, col.x, col.y, col.z, a, kind, soft},
                       {c2.x, c2.y, c2.z, 1, 1, col.x, col.y, col.z, a, kind, soft}, {c3.x, c3.y, c3.z, 0, 1, col.x, col.y, col.z, a, kind, soft}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  float night = fp.night;
  float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
  float lightI = clampf(night + lowVis * 0.6f + 0.12f, 0, 1);
  // the traffic's lamps at range (road_traffic.h): two headlights white, two tail lights red, a pixel's point each - up
  // close the modelled lamps are the light (ent_fs2.glsl)
  if (night > 0.05f) {
    const vec2 off(fp.camPos.x - g_world.wrap(fp.camPos.x), fp.camPos.z - g_world.wrap(fp.camPos.z));   // (in the copy of the islands the camera is over)
    for (const RoadCar& c : g_roadTraffic.all()) {
      vec3 at; vec2 h;
      if (!g_roadTraffic.place(g_world, c, at, h)) continue;
      at.x += off.x; at.z += off.y;
      const float dl = length(at - fp.camPos), fade = smoothstepf(40.f, 160.f, dl);
      if (fade <= 0.f || dl > RoadTraffic::kDrawn + 400.f) continue;
      const float L = c.kind == EK_CAR ? 2.2f : 8.3f;
      const vec3 f(h.x, 0.f, h.y), r(-h.y, 0.f, h.x);
      for (float s : {-0.6f, 0.6f}) {
        bill(add, at + f * L + r * s + vec3(0, 0.75f, 0), dl * 0.0016f, vec3(1.f, 0.93f, 0.8f) * (5.f * night * fade), 1.f, SPR_GLOW, 2.f);
        bill(add, at - f * L + r * s + vec3(0, 0.9f, 0), dl * 0.0013f, vec3(1.f, 0.05f, 0.02f) * (4.f * night * fade), 1.f, SPR_GLOW, 2.f);
      }
    }
    // the ferries' navigation lights (scenery.h): her two masthead lights white, at the end she's heading for a sidelight
    // either side (red to port, green to starboard), a white stern light at the other; out to 15 km
    for (const FerryRoute& fr : ferryRoutes()) {
      vec2 p0, p1, h; bool docked = false;
      if (!ferryAt(fr, fp.time, p0, h, &docked) || !ferryAt(fr, fp.time + 1.f, p1, h)) continue;
      const vec3 at(p0.x + off.x, 0.f, p0.y + off.y);
      const float dl = length(at - fp.camPos), fade = smoothstepf(60.f, 300.f, dl);
      if (fade <= 0.f || dl > 15000.f) continue;
      const vec2 mv = p1 - p0; const vec2 g = length(mv) > 0.1f ? mv * (1.f / length(mv)) : h;   // (the way she's going)
      const vec3 f(g.x, 0.f, g.y), r(-g.y, 0.f, g.x), hz(h.x, 0.f, h.y);
      const float sz = dl * 0.0016f, I = 5.f * night * fade;
      for (float s : {-29.f, 29.f}) bill(add, at + hz * s + vec3(0, 26.3f, 0), sz, vec3(1.f, 0.95f, 0.85f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at + f * 29.f - r * 8.3f + vec3(0, 18.5f, 0), sz, vec3(1.f, 0.06f, 0.03f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at + f * 29.f + r * 8.3f + vec3(0, 18.5f, 0), sz, vec3(0.1f, 1.f, 0.35f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at - f * 32.5f + vec3(0, 16.f, 0), sz, vec3(1.f, 0.95f, 0.85f) * (0.8f * I), 1.f, SPR_GLOW, 2.f);
    }
    // and the yachts' anchor lights at their mastheads, out to 4 km
    for (const PlacedEnt& p : landmarks()) {
      if (p.kind != EK_BOAT) continue;
      const vec3 at(p.e.x + sinf(p.e.yaw) * p.e.sz + off.x, 14.1f * p.e.sy, p.e.z + cosf(p.e.yaw) * p.e.sz + off.y);
      const float dl = length(at - fp.camPos), fade = smoothstepf(40.f, 160.f, dl);
      if (fade <= 0.f || dl > 4000.f) continue;
      bill(add, at, dl * 0.0012f, vec3(1.f, 0.95f, 0.85f) * (3.f * night * fade), 1.f, SPR_GLOW, 2.f);
    }
  }
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
    float navI = (0.6f + 2.5f * night) * (1.f - somnus.stealth) * glint;   // a cloaked XR-40 runs dark
    const ModelDef& md = kModels[plane.spec - kAircraft];
    bool jet = s.special != 0;   // the XR-30 is an SDF of its own: its lights don't follow the generic model layout
    vec3 tip = s.special == 2 ? kSomnusWingTip : jet ? kJetWingTip : modelWingTip(md);
    vec3 lt = plane.pos + plane.q.rotate(vec3(-tip.x, tip.y, tip.z)), rtp = plane.pos + plane.q.rotate(tip);
    if (camMode != 1) {
      bill(add, lt, ls, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rtp, ls, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, plane.pos + plane.q.rotate(s.special == 2 ? vec3(0, -0.1f, 7.93f) : jet ? vec3(0, 0.45f, 7.67f) : modelTailTip(md)), ls, vec3(1.f) * navI, 1, SPR_GLOW, 0.3f);
      if (plane.engineRunning && somnus.stealth < 0.5f && fmodf(t, 1.0f) < 0.12f) bill(add, plane.pos + plane.q.rotate(s.special == 2 ? vec3(0, 0.6f, 1.6f) : jet ? vec3(0, 0.74f, 1.6f) : modelFinTop(md) + vec3(0, 0.06f, 0)), ls * 1.6f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night) * glint, 1, SPR_GLOW, 0.3f);
      float st = fmodf(t, 1.3f);
      if (!plane.onGround && somnus.stealth < 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f))) { bill(add, lt, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); bill(add, rtp, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); }
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
    // the route from gate to gate (SPR_ROUTE): a curve leaving each gate the way it faces and arriving at the next the
    // same way (cubic Hermite, level at both ends: a climb or a descent between them is an S), drawn as a ribbon of
    // light turned to the camera - 8 m wide, never thinner than a few pixels - with chevrons flowing along it toward
    // the next gate (sprite_fs.glsl); it grows out of each gate's centre and fades as the camera nears it
    const float focal = 0.5f * g_ren.H / tanf(0.5f * std::max(fp.fovY, 0.05f));   // (pixels per metre at a metre)
    std::vector<vec3> rp; std::vector<float> rs;
    auto route = [&](vec3 p0, vec3 ax0, vec3 p1, vec3 ax1, vec3 col, float a) {
      const vec3 up(0, 1, 0);
      const float lh = length(vec3(p1.x - p0.x, 0, p1.z - p0.z));
      if (lh < 1.f) return;
      const vec3 t0 = normalize(cross(up, ax0)) * (lh * 0.9f), t1 = normalize(cross(up, ax1)) * (lh * 0.9f);
      const int n = std::min(420, std::max(32, (int)(lh / 9.f)));
      rp.resize(n + 1); rs.resize(n + 1);
      for (int k = 0; k <= n; k++) {
        const float u = (float)k / n, u2 = u * u, u3 = u2 * u;
        rp[k] = p0 * (2 * u3 - 3 * u2 + 1) + t0 * (u3 - 2 * u2 + u) + p1 * (3 * u2 - 2 * u3) + t1 * (u3 - u2);
        rs[k] = k ? rs[k - 1] + length(rp[k] - rp[k - 1]) : 0.f;
      }
      const float total = rs[n];
      vec3 side = normalize(cross(rp[1] - rp[0], up)), pl, pr; float pa = 0, pu = 0;
      for (int k = 0; k <= n; k++) {
        const vec3 d = rp[std::min(k + 1, n)] - rp[std::max(k - 1, 0)], view = rp[k] - fp.camPos;
        const float dist = length(view);
        vec3 sd = cross(d, view); const float sl = length(sd);
        if (sl > 1e-3f * length(d) * dist) side = sd * (1.f / sl);   // (seen end-on: the last side kept)
        const float hw = std::max(4.f, 2.5f * dist / focal);
        const vec3 l = rp[k] - side * hw, r = rp[k] + side * hw;
        const float fa = a * smoothstepf(0.f, 70.f, rs[k]) * smoothstepf(0.f, 70.f, total - rs[k]) * smoothstepf(20.f, 140.f, dist);
        const float uu = rs[k] / 30.f;   // (one chevron every 30 m)
        if (k && (fa > 0.f || pa > 0.f)) {
          SpriteVert q[4] = {{pl.x, pl.y, pl.z, pu, 0, col.x, col.y, col.z, pa, (float)SPR_ROUTE, 2.f}, {pr.x, pr.y, pr.z, pu, 1, col.x, col.y, col.z, pa, (float)SPR_ROUTE, 2.f},
                             {r.x, r.y, r.z, uu, 1, col.x, col.y, col.z, fa, (float)SPR_ROUTE, 2.f}, {l.x, l.y, l.z, uu, 0, col.x, col.y, col.z, fa, (float)SPR_ROUTE, 2.f}};
          alpha.push_back(q[0]); alpha.push_back(q[1]); alpha.push_back(q[2]); alpha.push_back(q[0]); alpha.push_back(q[2]); alpha.push_back(q[3]);
        }
        pl = l; pr = r; pa = fa; pu = uu;
      }
    };
    // the route: the leg being flown (from the gate just passed), brightest, then on through the next two gates (not in
    // photo mode's shots unless it shows them)
    const bool markers = !photo.on || photo.markers;
    for (int i = std::max(wpIndex, 1) - 1; markers && i < wpIndex + 2; i++) {
      vec3 c0, ax0, ay0, c1, ax1, ay1;
      if (ringGeom(i, c0, ax0, ay0) && ringGeom(i + 1, c1, ax1, ay1)) route(c0, ax0, c1, ax1, vec3(0.9f, 0.32f, 1.f), i < wpIndex ? 1.f : i == wpIndex ? 0.75f : 0.4f);
    }
    // checkpoint gates: the active one spins and breathes
    for (int i = wpIndex; markers && i < (int)contract.wps.size() && i < wpIndex + 3; i++) {
      vec3 c, ax, ay;
      ringGeom(i, c, ax, ay);
      bool act = i == wpIndex;
      float br = act ? 1.f + 0.035f * sinf(t * 2.6f) : 0.85f;
      vec3 col = act ? vec3(0.2f, 1.f, 0.5f) * 2.2f : vec3(0.85f, 0.4f, 1.f) * 1.1f;
      quadAx(add, c, ax * br, ay * br, col, act ? 1.f : 0.55f, SPR_RING, 5.f);
      if (act) quadAx(add, c, ax * 1.25f, ay * 1.25f, col * 0.35f, 0.2f, SPR_SHOCK, 5.f);   // faint outer halo
    }
    for (const RingBurst& b : bursts) {
      if (!markers) break;
      float k = b.t / 0.9f, s = 1.f + 2.2f * (1.f - (1.f - k) * (1.f - k));
      quadAx(add, b.c, b.ax * s, b.ay * s, b.col * 3.f, (1.f - k) * (1.f - k), SPR_SHOCK, 5.f);
      if (k < 0.35f && b.flash) bill(add, b.c, 90.f * (0.5f + k * 3.f), b.col * 2.f * (1.f - k / 0.35f), 1.f, SPR_GLOW, 5.f);
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
  for (int k = 0; k < kMaxBreakPieces; k++) ribbon(pieceTrail[k], 7.f, 0.9f, 2.4f, vec3(0.07f, 0.065f, 0.06f), 0.75f);
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
  // lightning: each channel segment a ribbon turned to the camera (its glow at least a few pixels wide however far off),
  // the stepped leader faint and only as far down as it has come, then the strokes; a flash on the ground it struck
  for (const Strike& S : strikes) {
    const float lead = S.restrike[0], t = S.t;
    const float first = t >= lead ? expf(-(t - lead) * 22.f) : 0.f, after = t >= lead ? 0.1f * expf(-(t - lead) * 5.f) : 0.f, all = S.flash(t);
    for (size_t i = 0; i < S.a.size(); i++) {
      float I;
      if (t < lead) { if (S.along[i] > t / lead) continue; I = 0.12f * S.k[i]; }
      else I = S.k[i] * (((int)i < S.main ? all : first) + after);
      if (I < 0.01f) continue;
      const vec3 m = (S.a[i] + S.b[i]) * 0.5f, d = (S.b[i] - S.a[i]) * 0.5f, vd = normalize(m - fp.camPos);
      vec3 side = cross(d, vd); const float sl = length(side); if (sl < 1e-4f) continue;
      const float w = std::max(S.w[i], length(m - fp.camPos) * 0.0022f);
      const float dl = length(d);
      quadAx(add, m, d * ((dl + w * 0.35f) / std::max(dl, 1e-3f)), side * (w / sl), vec3(0.85f, 0.88f, 1.f) * 7.f, I, SPR_BOLT, 6.f);
    }
    if (S.ground && t >= lead) bill(add, S.foot + vec3(0, 6.f, 0), 70.f, vec3(0.8f, 0.85f, 1.f) * 3.f, all, SPR_GLOW, 8.f);
  }
  // particles
  vec3 camVel = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
  std::vector<std::pair<float, const Particle*>> order;
  for (const Particle& p : particles) order.push_back({-length(p.p - fp.camPos), &p});
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& o : order) {
    const Particle& p = *o.second;
    if (p.gate && photo.on && !photo.markers) continue;   // (photo mode: the gates' sparks go with the gates)
    // (its own billows: a seed from what it was born with, the same every frame of its life)
    const float seed = 0.4f * (0.5f + 0.5f * sinf(p.maxLife * 7919.f + p.grow * 104.7f + p.col.x * 31.f)) * 0.999f;
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
      bill(alpha, p.p, std::max(p.size, 0.05f), col, al, p.kind + seed, 1.f);
    }
    else if (p.kind == SPR_FIRE) {   // flame cools from its spawn colour toward a deep red as it ages
      float cool = 1.f - fade;
      vec3 col = lerp(p.col, vec3(0.9f, 0.18f, 0.04f) * (p.col.x + p.col.y + p.col.z) * 0.4f, cool * cool);
      bill(add, p.p, std::max(p.size, 0.05f), col, a, p.kind + seed, 1.f);
    }
    else bill(alpha, p.p, p.size, p.col, a, p.kind + (p.kind == SPR_SMOKE ? seed : 0.f), 1.f);
  }
  // precipitation around the camera: as many drops as it is raining hard here (weather.cpp rainAt: heavier under
  // the denser cells, none above the tops), each streaked along its motion past the camera (its fall, the wind and
  // gusts that carry it, the camera's own speed: a 1/30 s shutter), some bigger and quicker than others; lit by
  // the sky, by lightning, and brightly where the landing lights' beams catch them
  const float precipK = wx.precip == 2 ? std::max(rainNow, 0.25f) : rainNow;
  if (screen == SCR_FLIGHT && wx.precip > 0 && precipK > 0.02f) {
    bool rain = wx.precip == 1;
    const int NMax = rain ? 1800 : 2000, N = (int)(NMax * (rain ? 0.15f + 0.85f * precipK : 0.4f + 0.6f * precipK));
    if (rainDrops.size() != (size_t)NMax) { rainDrops.resize(NMax); Rng r(5); for (auto& d : rainDrops) d = vec3(r.range(-30, 30), r.range(-20, 20), r.range(-30, 30)); }
    vec3 camV = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
    vec3 wv = plane.windVel;
    const bool lit = landingLight && plane.spec && plane.engineRunning && !crashed;
    const vec3 lp = plane.spec ? plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f) : vec3(), ld = plane.spec ? normalize(plane.forward() - plane.up() * 0.1f) : vec3(0, 0, -1);
    const float nightK = smoothstepf(0.1f, -0.12f, fp.sunDir.y);
    for (int i = 0; i < N; i++) {
      vec3& d = rainDrops[i];
      float tt = realTime, big = (float)((i * 2654435761u) >> 24) / 255.f;   // (the drop's size: the bigger ones fall faster)
      vec3 fall = rain ? vec3(0, -(6.5f + 3.5f * big), 0) : vec3(0, -(0.9f + 0.6f * big), 0);
      vec3 rel = fall + wv - camV;
      vec3 p = d + rel * fmodf(tt * 0.6f + (i % 37) * 0.13f, 1.0f) * 0.12f * (rain ? 6.f : 1.f);
      if (!rain) p = p + vec3(sinf(tt * 1.3f + i), 0, cosf(tt * 1.1f + i * 0.7f)) * (0.5f + 0.06f * wx.windSpeed);
      // wrap into the box around the camera
      auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
      vec3 wpos(wrap(p.x, 30.f), wrap(p.y, 20.f), wrap(p.z, 30.f));
      const float dc = length(wpos);
      if (dc < 3.f) continue;   // (none inside the cabin, none smeared across the whole view)
      wpos = wpos + fp.camPos;
      vec3 col = rain ? vec3(0.75f, 0.78f, 0.85f) : vec3(1, 1, 1);
      if (lit) {   // in a landing light's beam: lit far above the night's ambient (col past 1 is light, SPR_RAIN / SPR_SNOW)
        vec3 tl = wpos - lp; float dl = length(tl), cs = dot(tl, ld) / std::max(dl, 0.1f);
        float k = smoothstepf(0.9f, 0.97f, cs) / (1.f + dl * dl / 900.f) * (0.35f + 0.65f * nightK);
        if (k > 0.01f) col = vec3(1.f) + vec3(1.f, 0.93f, 0.8f) * (2.2f * k);
      }
      if (lightning > 0.05f) col = vec3(1.f) + vec3(0.8f, 0.85f, 1.f) * (lightning * 0.6f) + (col.x > 1.f ? col - vec3(1.f) : vec3());
      if (rain) {   // (a 1/60 s streak, and the nearest shorter and fainter: a drop a few metres off crossed the whole view)
        vec3 dir = normalize(rel); float len = std::min(clampf(length(rel) * 0.018f, 0.25f, 2.f), dc * 0.12f);
        vec3 side = normalize(cross(dir, wpos - fp.camPos)) * (0.008f + 0.008f * big);
        quadAx(alpha, wpos, side, dir * len, col, (0.25f + 0.2f * precipK) * smoothstepf(3.f, 9.f, dc), SPR_RAIN, 0.5f);
      } else {
        bill(alpha, wpos, 0.07f + 0.05f * big, col, 0.9f, SPR_SNOW, 0.5f);
      }
    }
    if (rain) {
      vec3 col = vec3(0.75f, 0.78f, 0.85f);
      if (lightning > 0.05f) col = vec3(1.f) + vec3(0.8f, 0.85f, 1.f) * (lightning * 0.6f);
      // the rain further off (30 to 110 m): coarser, fainter streaks - a third of the near ones' number, each standing
      // for many drops - so that heavy rain has depth rather than ending a few metres from the eye
      const int NF = N / 3;
      for (int i = 0; i < NF; i++) {
        const vec3& d = rainDrops[i];
        const float big = (float)((i * 2246822519u) >> 24) / 255.f;
        const vec3 rel = vec3(0, -(7.5f + 2.5f * big), 0) + wv - camV;
        vec3 p = d * 3.7f + rel * fmodf(realTime * 0.4f + (i % 41) * 0.11f, 1.0f) * 1.2f;
        auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
        vec3 wpos(wrap(p.x, 110.f), wrap(p.y, 45.f), wrap(p.z, 110.f));
        const float dc = length(vec3(wpos.x, 0.f, wpos.z));
        if (dc < 30.f || dc > 110.f) continue;
        wpos = wpos + fp.camPos;
        const vec3 dir = normalize(rel), side = normalize(cross(dir, wpos - fp.camPos)) * (0.04f + 0.03f * big);
        quadAx(alpha, wpos, side, dir * (2.5f + 2.f * big), col, (0.05f + 0.06f * precipK) * smoothstepf(110.f, 70.f, dc), SPR_RAIN, 2.f);
      }
      // splashes where it lands round the camera while it is low: a ring spreading on the ground or the water and, on the
      // ground, a crown of droplets thrown up - each spot's own tenth of a second, then on to another spot
      const float camGround = std::max(g_world.height(fp.camPos.x, fp.camPos.z), 0.f), camAgl = fp.camPos.y - camGround;
      if (camAgl < 60.f) {
        const int NS = (int)(260.f * precipK * (1.f - camAgl / 60.f));
        for (int i = 0; i < NS; i++) {
          const float cyc = realTime * 9.f + (float)((i * 2654435761u) >> 8) / 16777216.f;
          const float ph = cyc - floorf(cyc);
          Rng rr((uint32_t)i * 73856093u ^ (uint32_t)(int)floorf(cyc) * 19349663u ^ 0x9e3779b9u);
          vec3 at = fp.camPos + vec3(rr.range(-1.f, 1.f), 0.f, rr.range(-1.f, 1.f)) * (3.f + 22.f * rr.uni());
          const float g = g_world.height(at.x, at.z);
          const bool water = g < 0.3f;
          at.y = water ? 0.03f : g + 0.04f;
          const float dc = length(at - fp.camPos);
          if (dc < 1.5f) continue;
          const float sz = 0.06f + 0.22f * ph + 0.05f * rr.uni();
          quadAx(alpha, at, vec3(sz, 0, 0), vec3(0, 0, sz), col, (water ? 0.55f : 0.4f) * smoothstepf(28.f, 12.f, dc), SPR_SPLASH + ph * 0.399f, 0.05f);
          if (!water && ph < 0.55f && dc < 12.f) bill(alpha, at + vec3(0, 0.06f, 0), 0.09f, col, 0.35f * smoothstepf(12.f, 5.f, dc), 14.f + ph * 0.399f, 0.05f);   // (the crowns only close by: further off a few pixels' chevron)
        }
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
  // (the hold counts from where the aircraft is once the call has been heard - the channel free again - not from where
  // it was when the call was queued: it tripped while still being spoken - the review of v3.44.0, FLT-6)
  if (F.holding && !F.holdHeard && !atc.busy()) { F.holdHeard = true; F.holdPos = plane.pos; }
  if (F.holding && F.holdHeard && F.phase == 1 && (length(plane.pos - F.holdPos) > 40.f || F.airborne)) { result.holdViolated = true; F.holding = false; }   // (moved off the hold, or took off)
  const std::string reg = registrationOf(*plane.spec);   // "SX-ABC"
  auto callsign = [&](int v, bool arrival, std::vector<std::string>& ids, std::string& txt) {
    int k = arrival && F.dep != F.arr;
    bool full = !F.called[k];
    std::string say = full ? reg.substr(0, 2) + reg.substr(3) : reg.substr(0, 1) + reg.substr(4);
    for (char ch : say) ids.push_back(atc.alpha(v, ch));
    ids.push_back(AtcVoice::kPause);
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
    ids.push_back(AtcVoice::kPause);
  };
  // the arrival tower is the operational destination's: the field the autopilot is landing at when that isn't the
  // contract's (a GPS alternate, a glide to the nearest field) - a fresh inbound call to it; the contract's own
  // destination is untouched (the review of v3.44.0, FLT-5)
  if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) && plane.apAirport >= 0 && plane.apAirport != F.arr && F.airborne && !plane.onGround) {
    F.arr = plane.apAirport;
    if (F.phase >= 4 && F.phase <= 5) { F.phase = 3; F.waitT = 0; F.trafficSaid = false; F.goAround = F.goAroundUnable = false; }
  }
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
        // traffic landing or departing first: hold (the clearance comes once it's clear; after 90 s the tower clears the
        // traffic out of the way first - never the player onto it: the review of v3.44.0, FLT-4)
        if (F.trafficT >= 90.f) { traffic.clearRunway(F.dep); F.trafficT = 0; }
        RwyTraffic rt = rwyTraffic(F.dep);
        if (gs <= 4.f && (rt.onRunway || (rt.fin && rt.finD < 4500.f))) {
          F.trafficT += dt;
          if (!F.trafficSaid) {
            AtcVoice::Tx tx; tx.prio = 85; tx.subtitle = true; tx.group = "tower";
            tx.tag = callsign(vd, false, tx.ids, tx.text);
            tx.ids.push_back(atc.line(vd, rt.departing ? "hold_departure" : rt.onRunway ? "hold_position" : "hold_arrival"));
            tx.text += atc.text(tx.ids.back());
            tx.apt = F.dep; tx.key = atcKey(); atc.say(tx); F.trafficSaid = true; F.waitT = 0;
            F.holding = true; F.holdHeard = false; F.holdPos = plane.pos;
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
        else { tx.ids = {atc.line(vd, "contact_departure"), AtcVoice::kPause, atc.line(vd, "good_day")}; tx.text = atc.text(tx.ids[0]) + " " + atc.text(tx.ids[2]); }
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
          tx.ids.push_back(AtcVoice::kPause); tx.ids.push_back(atc.line(va, "number_two")); tx.text += " " + atc.text(tx.ids.back());
          if (kAircraft[rt.fin->spec].emptyMass > 2.5f * plane.spec->emptyMass) {
            tx.ids.push_back(AtcVoice::kPause); tx.ids.push_back(atc.line(va, "wake_caution")); tx.text += " " + atc.text(tx.ids.back());
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
            // (on the autopilot's approach it goes around itself - or, unable to climb away, lands: not the player's doing)
            F.goAroundUnable = plane.apOn && plane.apAirport >= 0 && !plane.apTowerGoAround();
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
      // (until the wheels are on, the runway is watched: traffic on it takes the clearance back - continue, or go
      // around on short final, as before it was given: the review of v3.44.0, FLT-4)
      if (!F.goAround && F.airborne && !plane.onGround && !touchedDown && rwyTraffic(F.arr).onRunway) { F.phase = 4; F.trafficSaid = false; break; }
      if (plane.onGround && length(plane.pos - A.pos()) < 3000.f) {
        if (gs < 18.f) {
          AtcVoice::Tx tx; tx.prio = 55; tx.subtitle = true; tx.group = "tower"; tx.ids.push_back(atc.line(va, "exit_when_able")); tx.text = atc.text(tx.ids[0]);
          tx.apt = F.arr; F.phase = 6; tx.key = atcKey(); atc.say(tx);
        }
      } else if (!plane.onGround && agl > 180.f && plane.vel.y > 2.f && F.waitT > 20.f) { F.phase = F.dep == F.arr ? 4 : 3; F.waitT = 0; F.lastValid = false; F.goAround = F.goAroundUnable = false; }   // (went around: the clearance no longer stands)
      if (F.goAround && !plane.onGround && agl > 150.f && plane.vel.y > 1.f) F.goAround = F.goAroundUnable = false;   // (complied: climbing away)
      break;
    default: break;
  }
}

// The comms channel, every frame: what the game showed this frame is resolved to voice lines (AtcVoice::resolve)
// and queued; the warnings speak on their rising edge (and again if they persist); the towers run their exchange.
// Nothing stale is ever said: the queue is dropped on pause, at the end of the flight and when the aircraft
// crashes (then only the crash itself is announced).
void Game::announceFlightEnd(const std::string& message, const std::string& followup) {
  // Explicit results and their landing feedback survive the screen transition.
  atc.cancel(); commsPending.clear(); commsEndScreen=-1;
  AtcVoice::Tx tx;
  if(atc.resolve(message,contract.id,padPrompts(),tx)) {
    tx.prio=90; atc.say(tx); commsEndScreen=int(screen); commsEndTime=0;
    if(!followup.empty() && atc.resolve(followup,contract.id,padPrompts(),tx)) {tx.prio=35;atc.say(tx);}
  }
}

void Game::updateComms(float dt) {
  bool live = (screen == SCR_FLIGHT || screen == SCR_LOADING) && !paused && !launchCardPending();
  if(commsEndScreen>=0) {
    if(int(screen)!=commsEndScreen || paused) {commsEndScreen=-1;atc.cancel();}
    else {
      atc.update(dt); commsEndTime+=dt;
      voiceDuck=approach(voiceDuck,atc.busy()?1.f:0.f,atc.busy()?6.f:1.5f,dt);
      if(commsEndTime>1.f && !atc.pending()) commsEndScreen=-1;
      return;
    }
  }
  // (paused, or in the menus, nobody is talking: the music comes back up - it stayed at the duck when a pause cut a
  // call short: the review of v3.44.0, AUD-2)
  if (!live) { atc.cancel(); commsPending.clear(); commsCrashSeen = false; voiceDuck = approach(voiceDuck, 0.f, 1.5f, dt); return; }
  auto lessonUnavailable = [&]() {
    if (lessonVoiceWarned || voicesOff) return;
    lessonVoiceWarned = true;
    toast("Lesson recording unavailable. Use the on-screen controls; check the voice assets.", vec3(1.f, 0.8f, 0.4f), false);
  };
  if (!atc.ok()) {
    for (const auto& m : commsPending) if (m.lessonPhase >= 0) { lessonUnavailable(); break; }
    commsPending.clear(); voiceDuck = 0.f; return;
  }
  if (crashed && !commsCrashSeen) { atc.cancel(); commsCrashSeen = true; }
  if (!crashed) commsCrashSeen = false;
  if (screen == SCR_FLIGHT && !crashed && flightClock > 2.f) {   // the HUD's warnings (game_ui.cpp), spoken as they come on
    const AircraftSpec& spc = *plane.spec;
    const Airport& d = g_world.airports[contract.to];
    bool w[4] = {plane.stallWarn > 0.8f && !plane.onGround && plane.ias > 10.f,
                 !plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f,
                 spc.retract && plane.gear < 0.99f && !plane.onGround && plane.agl() < 200 && length(plane.pos - d.pos()) < 4000.f && plane.ias < spc.vref * 1.5f,
                 !plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && !plane.glideOnly() && !(engineShutDown && plane.onGround)};   // (a failed engine has no restart to advise; one shut down at the stand, none needed)
    const char* say[4] = {"STALL", "PULL UP", "GEAR!", nullptr};
    for (int i = 0; i < 4; i++) {
      if (w[i] && (!warnWas[i] || realTime - warnLastT[i] > 8.f)) {
        std::string m = say[i] ? say[i] : "ENGINE OFF - press " + actLabel(ACT_ENGINE, padPrompts(), true) + " to restart";
        AtcVoice::Tx tx;
        if (atc.resolve(m, contract.id, padPrompts(), tx)) { tx.prio = std::max(tx.prio, 95); tx.key = kWarnKey + i; atc.say(tx); }   // (said only while it still holds)
        warnLastT[i] = realTime;
      }
      warnWas[i] = w[i];
    }
    // the failure annunciators (hudAnnunciators), spoken when they come on or change severity: each wording once
    for (auto& a : hudAnnunciators()) {
      auto it = failVoiced.find(a.slot);
      if (it != failVoiced.end() && it->second == a.key) continue;
      AtcVoice::Tx tx;
      if (atc.resolve(a.text, contract.id, padPrompts(), tx)) { atc.say(tx); failVoiced[a.slot] = a.key; }
    }
  }
  for (const auto& m : commsPending) {
    AtcVoice::Tx tx;
    bool resolved = m.lessonPhase >= 0 ? atc.resolveLesson(m.mission, m.lessonPhase, m.text, tx) : atc.resolve(m.text, m.mission, padPrompts(), tx);
    if (m.lessonPhase >= 0 && resolved) for (const auto& id : tx.ids) resolved = atc.decodes(id) && resolved;
    if (resolved) atc.say(tx);
    else if (m.lessonPhase >= 0) lessonUnavailable();
  }
  commsPending.clear();
  if (screen == SCR_FLIGHT && !crashed && !researchFlight && !militaryFlight) updateAtc(dt);
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
  ap.scene = (screen == SCR_FLIGHT || (screen == SCR_DEBRIEF && crashed)) && plane.spec;
  ap.paused = paused;
  if (ap.scene && !audioSceneWas) { ++audioSceneId; audioCameraValid=false; audioMachArmed=true; audioPlayerCone=-1e9f; }
  audioSceneWas=ap.scene; ap.sceneId=audioSceneId;
  ap.wrapPeriod=g_world.repeating?WRAP_SPAN:0;
  ap.listener.position=dbgCam?dbgCamPos:camPos;
  if (plane.spec) {
    ap.aircraftPosition=plane.pos; ap.aircraftVelocity=plane.vel; ap.aircraftRotation=plane.q;
    ap.cabinEye=kModels[plane.spec-kAircraft].eye; ap.soundSpeed=plane.soundSpeed;
    ap.interior=camMode==1 && !dbgCam && !crashed;
    vec3 forward=ap.interior?plane.q.rotate(quat::axisAngle(vec3(0,1,0),lookYaw).rotate(quat::axisAngle(vec3(1,0,0),lookPitch).rotate(vec3(0,0,-1)))):
      normalize(plane.pos+vec3(0,plane.spec->fusRad*.3f,0)-ap.listener.position);
    vec3 up=ap.interior?plane.up():vec3(0,1,0);
    if(camMode==0 && !dbgCam) up=normalize(lerp(vec3(0,1,0),camQ.rotate(vec3(0,1,0)),.3f));
    if(dbgCam) forward=normalize(dbgCamLook-dbgCamPos);
    ap.listener.forward=forward; ap.listener.right=normalize(cross(forward,up)); ap.listener.up=cross(ap.listener.right,forward);
    vec3 movement=audioDisplacement(audioCameraPrevious,ap.listener.position,ap.wrapPeriod);
    bool continuous=audioCameraValid && audioCameraMode==camMode && lastDt>1e-4f && length(movement)<2000.f;
    ap.listener.velocity=continuous?movement/(lastDt*(dbgCam?1.f:std::max(timeAccel,1.f))):(camMode==3?vec3():plane.vel);
    if(length(ap.listener.velocity)>2000) ap.listener.velocity=plane.vel; // camera cuts are not supersonic ears
    audioCameraPrevious=ap.listener.position;audioCameraValid=ap.scene;audioCameraMode=camMode;
    float cone=audioMachCone(plane.pos,plane.vel,ap.listener.position,plane.soundSpeed,ap.wrapPeriod);
    audioBoomCooldown=std::max(0.f,audioBoomCooldown-lastDt);
    if(ap.scene && !paused && !ap.interior && camMode==3 && audioPlayerCone>-1e8f && audioPlayerCone<0 && cone>=0 && audioBoomCooldown<=0) {
      g_audio.triggerAt(SFX_BOOM,plane.pos,.85f);audioBoomCooldown=6.f;
      shake(0.45f);   // (the shock front's double jolt reaching the flyby camera)
    }
    audioPlayerCone=cone;
  }
  ap.master = set.master; ap.engineVol = set.engineVol * (1.f - 0.35f * voiceDuck); ap.sfxVol = set.sfxVol; ap.voiceVol = set.atcVol;
  {   // the radio music, at the master volume (the review of v3.44.0, AUD-1), lowered under speech
    // (Radio::setVolume reaches the music ring only while the radio plays: idle, the ring is the score's - updateMusic)
    const float rv = radioLevel();
    if (rv != radioSentVol) { radio.setVolume(rv); radioSentVol = rv; }
  }
  static bool muffled = false;
  if (actKeyP(ACT_ANR) || actJoyP(ACT_ANR) || (screen == SCR_FLIGHT && !paused && actPadP(ACT_ANR))) { muffled = !muffled; toast(muffled ? "Engine noise muffled (headset ANR on)" : "Headset ANR off"); }
  ap.muffled = muffled;
  if (screen == SCR_FLIGHT && plane.spec && !crashed) {
    const AircraftSpec& s = *plane.spec;
    ap.inFlight = true;
    ap.signature = clampf(powf((s.engineType==ENG_JET?30000.f:200000.f)/std::max(s.power,1.f),.055f),.82f,1.2f);
    ap.engineType = s.engineType; ap.engines = s.engines; ap.cylinders = s.cylinders; ap.blades = s.blades;
    ap.research = s.special != 0; ap.nozzle = plane.nozzle; ap.mach = plane.mach;
    ap.rpm = s.engineType == ENG_JET ? plane.n1 : plane.rpm; ap.maxRpm = s.maxRpm; ap.n1 = plane.n1;
    ap.spool = plane.engineSpool; ap.throttle = plane.engineRunning ? plane.ctl.throttle : 0.f;
    ap.running = plane.engineRunning; ap.cranking = !plane.engineRunning && plane.starterTime > 0;
    for (int e = 0; e < 4; e++) ap.engineHealth[e] = plane.fail.engineHealth[e];
    ap.airspeed = plane.airspeed; ap.groundSpeed = length(vec3(plane.vel.x, 0, plane.vel.z)); ap.onGround = plane.onGround; ap.rough = plane.groundRough;
    static_assert(SURF_ASPHALT==int(AudioSurface::Paved) && SURF_GRASS==int(AudioSurface::Grass) &&
      SURF_GRAVEL==int(AudioSurface::Gravel) && SURF_SNOW==int(AudioSurface::Snow) && SURF_SAND==int(AudioSurface::Sand),"audio ground materials match the world");
    int groundAirport=plane.onGround?g_world.onRunway(plane.pos.x,plane.pos.z,4.f):-1;
    ap.groundSurface=groundAirport>=0?AudioSurface(g_world.airports[groundAirport].surface):AudioSurface::Grass;
    ap.stallWarn = plane.onGround ? 0.f : plane.stallWarn; ap.stallIsShaker = s.engineType == ENG_JET || s.license == LIC_ATP;
    ap.gearMoving = plane.gear > 0.01f && plane.gear < 0.99f; ap.flapsMoving = fabsf(plane.flaps - plane.ctl.flaps) > 0.02f;
    ap.gearDown = plane.gear; ap.flaps = plane.flaps;
    const int model = int(plane.spec-kAircraft);
    const auto& md=kModels[model];
    ap.engineRadius=clampf(sqrtf(std::max(s.power,1.f))*(s.engineType==ENG_PISTON?.025f:.13f),9.f,50.f);
    ap.gearPosition=vec3(0,-plane.gearHeight(),gearStations(s).mainZ);
    ap.slip=dot(plane.vel-plane.windVel,plane.right())/std::max(plane.airspeed,1.f);
    ap.buffet=clampf(plane.stallWarn*.6f+plane.wxl.sigma*.3f+expf(-powf((plane.mach-.98f)*16.f,2))*.25f,0,1);
    if(s.special) {
      vec3 xp[4],xd[4];float strength[4];int count=jetExhausts(plane,xp,xd,strength);
      for(int e=0;e<count;++e) {ap.enginePosition[e]=xp[e];ap.engineDirection[e]=xd[e];ap.enginePower[e]=s.special==2?clampf(plane.podThr[e],0,1.6f):1.f;}
    } else {
      const auto& geometry=aeroGeom(s);
      for(int e=0;e<std::min(s.engines,4);++e) {
        ap.enginePosition[e]=e<geometry.nEng?geometry.eng[e].pos:vec3(s.engines==1?0.f:(e%2?md.nacX:-md.nacX),md.nacY,md.nacZ0);
        ap.engineDirection[e]=vec3(0,0,1);
      }
    }
    // (the rain drumming on the airframe as hard as it rains here, louder the faster it meets it; the wind noise
    // gusting with the eddies and gust bursts the aircraft is flying through)
    ap.rain = wx.precip == 1 ? clampf(rainNow * (0.75f + 0.35f * smoothstepf(10.f, 70.f, plane.airspeed)), 0.f, 1.f) : 0.f;
    ap.turbulence = clampf(0.6f * plane.wxl.sigma + 0.3f * plane.gustBurst, 0.f, 1.f);
    ap.paused = paused;
  }
  if (ap.scene) {
    // Retain IDs across ordering changes: sorting by audibility never swaps an engine's oscillator state.
    std::vector<std::pair<float,AudioEmitter>> candidates;
    auto offer=[&](AudioEmitter e) {
      float distance=length(audioDisplacement(ap.listener.position,e.position,ap.wrapPeriod));
      if(distance<6500) candidates.push_back({e.level*e.radius/std::max(e.radius,distance),e});
    };
    if(set.traffic) for(const auto& c:traffic.craft) if(c.alive && c.state!=TrafficCraft::PARKED) {
      const auto& sp=kAircraft[c.spec]; AudioEmitter e;e.id=10000+c.id;e.position=c.pos;e.velocity=c.vel;e.forward=c.q.rotate(vec3(0,0,-1));
      e.engineType=sp.engineType;e.cylinders=sp.cylinders;e.blades=sp.blades;e.spool=clampf(c.throttle,0,1);
      e.n1=22+e.spool*78;e.rpm=sp.maxRpm*(.3f+.7f*e.spool);e.radius=sp.engineType==ENG_PISTON?22:55;e.level=.7f;offer(e);
    }
    for(const auto& a:hiveCombat.actors) if(a.alive) {
      AudioEmitter e;e.id=100000+a.id;e.position=plane.pos+hiveCombat.displacement(plane.pos,a.position);e.velocity=a.velocity;
      e.forward=normalize(a.velocity);e.spool=clampf(length(a.velocity)/180.f,.25f,1.f);e.n1=30+e.spool*70;e.level=.65f;e.radius=45;offer(e);
    }
    for(size_t i=0;i<wreck.size();++i) if(wreck[i].fire>.2f && wreck[i].c.y>0) {
      AudioEmitter e;e.id=500000+uint32_t(i);e.kind=AudioEmitter::Fire;e.position=wreck[i].c;e.velocity=wreck[i].v;
      e.level=wreck[i].fire*clampf(1.2f-crashTimer*.06f,.15f,1);e.radius=8;offer(e);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.first>b.first;});
    for(size_t i=0;i<candidates.size() && ap.emitterCount<19;++i) ap.emitters[ap.emitterCount++]=candidates[i].second;
    if(ufo.on) {AudioEmitter e;e.id=900001;e.kind=AudioEmitter::Track;e.position=ufo.pos;e.velocity=plane.vel;ap.emitters[ap.emitterCount++]=e;}
  }
  g_audio.setParams(ap);
}

// ------------------------------------------------------------------ main update / render
std::string Game::slowestUpdateText() const {
  std::string t = fmt("%.1f ms", slowestUpdate.total);
  for (const auto& p : slowestUpdate.parts) t += fmt("; %s %.1f", p.first, p.second);
  return t;
}
void Game::resetSlowestUpdate() { slowestUpdate = UpdateParts(); }

void Game::update(float dt) {
  // (the update timed whole and part by part: the slowest kept, for the benchmark to say what a hitch was spent on)
  struct Whole { Game& g; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~Whole() { g.updParts.total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (g.updParts.total > g.slowestUpdate.total) g.slowestUpdate = g.updParts; } } whole{*this};
  updParts.parts.clear();
  if (loadLogging) loadLogTick();
  g_ren.beginRegionUploadFrame(); // one shared detail + horizon GPU budget per displayed frame
  if (screen != SCR_FLIGHT) { rumbleLo = rumbleHi = 0.f; shakeTrauma = shakeBuffet = 0.f; }   // (the motors and the shake are the flight's)
  if (screen != SCR_REGION_LOADING) { PartTimer pt(*this, "career world"); const std::string want = screenWorld(); if (!want.empty()) loadCareerWorld(want, screen); }
  if (screen == SCR_REGION_LOADING) {
    realTime += std::min(dt, .1f); lastDt = std::min(dt, .05f);
    if (in.pressed[K_F11]) wantFullscreenToggle = true;
    gamepadMenus(lastDt); focusNavigate(); radio.poll(); updateMusic(); updateSteam(dt);
    updateRegionLoading(std::min(dt, .25f));
    return;
  }
  { PartTimer pt(*this, "earth surface"); updateEarthSurfaceContext(); } // establish the collision snapshot protection before physics reads World
  if (pendingCareer && (screen == SCR_HUB || screen == SCR_DEBRIEF || screen == SCR_MENU)) {   // a settlement or purchase that couldn't be saved: tried again now and then
    retryT += dt;
    if (retryT > 3.f) { retryT = 0; PartTimer pt(*this, "career save retry"); retryCommit(); }
  }
  { PartTimer pt(*this, "earth install"); updateEarthInstall(); }
  // the render resolution: native, a fixed scale, or adjusted to hold the frame-rate target (Settings)
  fpsAvg = lerpf(fpsAvg, dt, 1.f - expf(-dt * 3.f));   // (a third of a second, whatever the frame rate)
  maxFrameWin = std::max(maxFrameWin, dt); maxFrameT += dt;
  if (maxFrameT >= 1.f) { maxFrameMs = maxFrameWin * 1000.f; maxFrameWin = 0; maxFrameT = 0; }
  if (!headless && g_ren.ok) {   // render resolution: native, a fixed scale, or Auto: held to the frame-rate target
    static const float fixedScale[5] = {1.f, 1.f, 0.85f, 0.75f, 0.67f};
    float want = fixedScale[std::clamp(set.resMode, 0, 4)];
    // Auto (quality_governor.h): the screen's edges coarser first where the card has foveated shading, then the render
    // scale, each step kept only if the GPU's frame time shows it paid; back the other way when there is room
    autoRes.vrs = set.foveate && g_ren.vrsAvailable;
    if (set.resMode == 1 && !photo.on) {
      // (the GPU budget of the frame-rate target, with 8% to spare)
      autoRes.frame(dt, g_ren.gpuMs, 1000.f / effectiveHz() * 0.92f);
      g_ren.foveation = autoRes.foveation;
      want = autoRes.scale;
    } else { autoRes.reset(); g_ren.foveation = 0.f; }
    if (photo.on) want = 1.f;   // (photo mode: every pixel at the full resolution, all of it fully shaded)
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
  bool bumpersFree = !(screen == SCR_FLIGHT && plane.spec && plane.spec->special == 2 && somnus.armed) && !photo.on;
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
  if (screen == SCR_HUB && !(hubTab == TAB_CONTRACTS && hubList == 2) && in.pad && !((in.buttons & PAD_LB) && (in.buttons & PAD_RB))) {   // the shoulders step through the hub's tabs
    const int nTabs = TAB_SETTINGS + 1;
    if (in.buttonsPressed & PAD_RB) { hubTab = (hubTab + 1) % nTabs; g_audio.trigger(SFX_CLICK); }
    if (in.buttonsPressed & PAD_LB) { hubTab = (hubTab + nTabs - 1) % nTabs; g_audio.trigger(SFX_CLICK); }
  }
  bool padCombo = in.pad && (in.buttons & PAD_LS) && (in.buttons & PAD_RS) && (in.buttonsPressed & (PAD_LS | PAD_RS));
  if (screen == SCR_MENU && ((in.down['U'] && in.down['I'] && (in.pressed['U'] || in.pressed['I'])) || padCombo)) {
    screen = SCR_RESEARCH; resOpened = realTime; g_audio.trigger(SFX_BEEP); award("classified");
    resWarm = true; resWarmFrames = 0; resBakeSeen = g_ren.bakeCount;   // the terminal's boot screen shows at once; the craft and its airport warm behind it
    resWork = 0.f; resPend0 = 0;
  }
  if (screen == SCR_RESEARCH && resWarm) {   // warming: the scenery streams flat out, the previewed craft's shells bake; done when a frame bakes nothing and nothing is pending
    g_ren.entBudgetMs = 14.f;
    bool baked = g_ren.bakeCount != resBakeSeen; resBakeSeen = g_ren.bakeCount;
    // how much of it is done, for the boot screen's bar and its scans: the craft views drawn (render() has drawn
    // resWarmFrames frames, the first the boot screen alone, then one view each and the selected craft until the check
    // below), the scenery chunks made out of the most the site has waited on (counted from the first frame of the
    // site), and its terrain lighting
    const int nViews = 2 * resCraftCount() + 3, f = resWarmFrames;
    const bool site = f >= 2, lit = site && !loadingShadowPending();
    if (site) resPend0 = std::max(resPend0, g_ren.entPending);
    const float views = clampf((f - 1.f) / nViews, 0.f, 1.f), chunks = !site ? 0.f : resPend0 > 0 ? 1.f - (float)g_ren.entPending / resPend0 : 1.f;
    resWork = std::max(resWork, 0.25f * views + 0.65f * chunks + 0.10f * (lit ? 1.f : 0.f));
    if (resWarmFrames > 2 * resCraftCount() + 3 && !baked && g_ren.entPending == 0 && !loadingShadowPending()) { resWarm = false; resWarmCraft = -1; resWarmCk = false; g_ren.entBudgetMs = 2.5f; resWork = 1.f; }
  }
  if (screen == SCR_FLIGHT && (actKeyP(ACT_RADIO) || actJoyP(ACT_RADIO) || (!paused && actPadP(ACT_RADIO)))) showRadio = !showRadio;
  { PartTimer pt(*this, "radio"); radio.poll(); }
  { PartTimer pt(*this, "music"); updateMusic(); }
  { PartTimer pt(*this, "steam"); updateSteam(dt); }
  if (screen == SCR_HUB) { static float careerCheckT = 0; if ((careerCheckT += dt) > 1.f) { careerCheckT = 0; profileCareerCheck(); } }   // (licences, the fleet, the airline: achievements)
  for (size_t i = 0; i < achShow.size() && i < 1; i++) achShow[i].t += dt;   // (the banner on show)
  if (!achShow.empty() && achShow.front().t > 4.5f) achShow.erase(achShow.begin());
  if (screen == SCR_FLIGHT) {
    if (photo.on) updatePhoto(dt);   // (photo mode: the flight stays paused under it, and the keys are its own)
    else if (actPressed(ACT_PHOTO) && !settingsFromPause && !freeRegionMap && !freeEarthPicker) enterPhoto();
    else if (freeEarthPicker && in.pressed[K_ESC]) {}   // (the airport picker's own: its query cleared, else it closes)
    else if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_START)) {
      if (freeRegionMap) freeRegionMap = false;
      else if (freeEarthPicker) freeEarthPicker = false;
      else if (showMap) showMap = false; else if (showRadio) showRadio = false;
      else if (paused && settingsFromPause && !(in.buttonsPressed & PAD_START)) settingsFromPause = false;   // B / Esc: back to the pause menu
      else { paused = !paused; settingsFromPause = false; if (paused && !atcF.lastCall.empty()) atcF.lastBeforePause = true; }
    }
    if (!paused && actPressed(ACT_MAP)) { if (plane.fail.avionicsDark() && !showMap) toast("GPS dark - battery flat", vec3(1, 0.45f, 0.35f)); else { showMap = !showMap; g_audio.trigger(SFX_CLICK); } }
    if (showMap && !paused) {   // GPS open: Tab / D-pad pick the autoland airport, Enter / A engages the autopilot to it
      if (in.pressed[K_TAB] || (in.buttonsPressed & PAD_RIGHT)) cycleApDest(in.down[K_SHIFT] ? -1 : 1);
      if (in.buttonsPressed & PAD_LEFT) cycleApDest(-1);
      if ((in.pressed[K_ENTER] || (in.buttonsPressed & PAD_A)) && apDest >= 0 && !plane.onGround) engageAutopilot();
    } else if (!photo.on && actPressed(ACT_MINIMAP)) { showMinimap = !showMinimap; toast(showMinimap ? "Minimap shown" : "Minimap hidden"); }
    if (!photo.on && actPressed(ACT_HUD)) { hudOn = !hudOn; set.hudCam[std::clamp(camMode, 0, 3)] = hudOn; }
    if (!paused) {
      int nSim = std::max(1, (int)ceilf(simDt / 0.05f - 1e-4f));
      Input frameIn; bool saved = false;   // key presses act once: the catch-up steps see the held state without the press edges
      for (int k = 0; k < nSim && screen == SCR_FLIGHT && !paused; k++) {
        float h = simDt / nSim;
        { PartTimer pt(*this, "flight"); updateFlight(h); }
        if (screen == SCR_FLIGHT) { PartTimer pt(*this, "particles"); updateParticles(h * timeAccel); }
        if (k == 0 && nSim > 1) { frameIn = in; saved = true; in.endFrame(); }
      }
      if (saved) in = frameIn;
      if (screen == SCR_FLIGHT) { PartTimer pt(*this, "camera"); updateCamera(dt); }
      if (screen == SCR_FLIGHT && !crashed) {   // the finals the autopilot may plan next, profiled ahead (Plane::wantFinals): the GPS pick, the field it flies to, the job's
        if (apDest >= 0) Plane::wantFinals(apDest);
        if (plane.apAirport >= 0) Plane::wantFinals(plane.apAirport);
        if (plane.apHoldFor >= 0) Plane::wantFinals(plane.apHoldFor);
        if (!haulPending() && contract.to >= 0 && contract.to < (int)g_world.airports.size()) Plane::wantFinals(contract.to);
      }
      if (plane.spec && wreck.empty()) {
        float rps = plane.spec->engineType == ENG_TURBOPROP ? plane.rpm / 60.f : plane.rpm / 60.f;
        propAngle = fmodf(propAngle + rps * 2 * PI * dt * (plane.rpm < 400 ? 1.f : 0.0f) + (plane.rpm >= 400 ? dt * 3.f : 0.f), 2 * PI * 100);
      }
    }
  } else if (screen == SCR_LOADING) {
    PartTimer pt(*this, "loading"); updateLoading(dt);
  } else if (screen == SCR_DEBRIEF) {
    if (crashed) { PartTimer pt(*this, "debrief wreck"); crashTimer += dt; updateWreck(dt); updateParticles(dt); }   // the wreck keeps settling/sinking behind the results
  } else {
    wx = Weather(); wx.cloudCover = 0.35f; wx.cloudBase = 1500; wx.visibility = 45000; wx.windSpeed = 4;
    timeOfDay = screen == SCR_MENU ? menuShot(realTime).tod : 15.8f;
    if (screen == SCR_MENU) wx.cloudCover = menuShot(realTime).cloud;
    if (screen == SCR_RESEARCH) {   // the research preview shows the sortie's light and sky
      timeOfDay = resTime; wx.cloudCover = resWx == 0 ? 0.15f : resWx == 1 ? 0.65f : 0.92f; wx.cloudBase = resWx == 2 ? 900.f : 1400.f;
    }
    cloudOff = cloudOff + vec2(dt * 8.f, dt * 3.f);
    wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
  }
  if (screen == SCR_REGION_LOADING) return;
  { PartTimer pt(*this, "earth flight"); updateEarthFlight(); } // once per displayed frame, including paused background preparation
  { PartTimer pt(*this, "comms"); updateComms(dt); }
  { PartTimer pt(*this, "audio"); feedAudio(); }
}

void Game::renderLaunchCard() {
  // No buildFrame, scene streaming, mesh baking, collision queries or picture decoding on this path.
  // CPU-only tests use the same card builder without issuing GL commands.
  if (g_ren.ok) g_ren.clearScreen();
  g_ren.uiBegin();
  drawLoading();
  if (g_ren.ok) g_ren.uiEnd();
  if (pendingMilitaryLaunch) militaryLaunchFrameDrawn = true;
  if (pendingLaunch) launchFrameDrawn = true;
}

void Game::framePresented() {
  if (screen == SCR_LOADING && pendingMilitaryLaunch && militaryLaunchFrameDrawn)
    militaryLaunchFramePresented = true;
  if (screen == SCR_LOADING && pendingLaunch && launchFrameDrawn)
    launchFramePresented = true;
  militaryLaunchFrameDrawn = launchFrameDrawn = false;
}

void Game::render() {
  if (screen != SCR_REGION_LOADING) { const std::string want = screenWorld(); if (!want.empty()) loadCareerWorld(want, screen); }
  if (screen == SCR_REGION_LOADING) {
    g_ren.clearScreen(); g_ren.uiBegin(); drawRegionLoading(); drawPadCursor(); g_ren.uiEnd();
    return;
  }
  if (screen == SCR_LOADING && launchCardPending()) { renderLaunchCard(); return; }
  if (screen == SCR_LOADING && launchSetUpShown) { launchSetUpShown = false; renderLaunchCard(); return; }
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
    resLoadTask.clear();resTaskDone=resTaskUnits=0;
    g_ren.onMeshProgress=[this](float f,const std::string& task){
      const float views=clampf((std::max(0,resWarmFrames-2)+f)/(2.f*resCraftCount()+3.f),0.f,1.f);
      const float chunks=resPend0>0?clampf(1.f-float(g_ren.entPending)/resPend0,0.f,1.f):0.f;
      resWork=std::max(resWork,.25f*views+.65f*chunks+.10f*(resWarmFrames>=2&&!loadingShadowPending()?1.f:0.f));
      resLoadTask=task;resTaskDone=resTaskUnits=1;
    };
    g_ren.onMeshBatch=[this](int done,int total,const std::string& task){resLoadTask=task;resTaskDone=done;resTaskUnits=total;};
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
  } else if (screen == SCR_LOADING && !headless && loadReadyT < 0 && platformPresent && (loadMap || militaryFlight)) {
    // the loading screen, drawn and shown from inside a long bake or warm-up (the hive craft, the XR-40's cockpit):
    // on its own the screen would hold still until the flight opened. (Its aerial map was rendered by the frame
    // before: never from inside a bake)
    g_ren.bakeYield = [this] {
      GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
      static auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count()); last = now;
      realTime += dt; loadT += dt;
      g_ren.clearScreen();
      g_ren.uiBegin(); drawLoading(); g_ren.uiEnd();
      platformPresent(); ++loadShownInside;
      glUseProgram((GLuint)prog);
    };
  } else if (g_ren.bakeYield && screen != SCR_RESEARCH) g_ren.bakeYield = nullptr;
  if (!vid) {
    // The loading screen, under its card (and under the frame drawn after them): the flight's views once each - inside
    // and out, and in the XR-40 a store of each kind and a young blast - before it opens. Building a body or linking a
    // program isn't all of it: drivers finish a program on its first draw. The first look inside the XR-40 took 90 s
    // on the software renderer, each look after it under one; its first bomb seen from outside as long.
    if (screen == SCR_LOADING && !headless && loadReadyT < 0 && loadFrames >= 2 && loadWarmStep >= loadWarmItems() && !loadViewsDrawn && plane.spec && !crashed) {
      const int cm = camMode, baked = g_ren.bakeCount; const vec3 cp = camPos;
      const float size = std::max(plane.spec->span, plane.spec->fusLen);
      // (one a frame, the loading screen drawn and shown between them; the view the flight opens with last: the frame
      // after cuts from the other)
      const int order[2] = {cm == 1 ? 1 : 0, cm == 1 ? 0 : 1};
      for (int view : {order[std::min(loadViewsStep, 1)]}) {
        camMode = view;
        camPos = view == 1 ? plane.pos + plane.q.rotate(kModels[plane.spec - kAircraft].eye)
                           : cm != 1 ? cp : plane.pos + plane.q.rotate(vec3(0, size * 0.3f, size * 1.4f));
        FrameParams w = buildFrame();
        if (plane.spec->special == 2) {
          const vec3 ahead = -w.camBack;
          w.fx.bombs = 3;
          for (int k = 0; k < 3; k++) {   // (kinds: 0 plasma, 1 penetrator, 2 EMP)
            const vec3 at = w.camPos + ahead * 14.f + w.camRight * ((k - 1) * 2.f);
            float* o = w.fx.bomb[k]; o[0] = at.x; o[1] = at.y; o[2] = at.z; o[3] = .29f;
            float* st = w.fx.bombStyle[k]; st[0] = st[1] = st[2] = 1.f; st[3] = (float)k;
            const float rot[9] = {w.camRight.x, w.camRight.y, w.camRight.z, w.camUp.x, w.camUp.y, w.camUp.z, w.camBack.x, w.camBack.y, w.camBack.z};
            std::copy(rot, rot + 9, w.fx.bombRot[k]);
          }
          const vec3 at = w.camPos + ahead * 400.f;
          w.fx.blasts = 1; float* o = w.fx.blast[0]; o[0] = at.x; o[1] = at.y; o[2] = at.z; o[3] = 100.f;
          float* I = w.fx.blastI[0]; I[0] = 0.2f; I[1] = 1.f; I[2] = I[3] = 0.f;
        }
        std::vector<SpriteVert> a, b;
        buildSprites(w, a, b);
        g_ren.renderScene(w, a, b);
      }
      camMode = cm; camPos = cp;
      g_ren.dropHistory();
      if (g_ren.bakeCount == baked) loadViewsStep++;   // (a body built at its end was not drawn yet: again, next frame)
      loadViewsDrawn = loadViewsStep >= 2;
    }
    fp = buildFrame();
    std::vector<SpriteVert> a, b;
    if (!fp.hangarPreview) {
      buildSprites(fp, a, b);
      const size_t own = a.size(); if (!g_world.procedural) volcano::append(fp, g_ren.quality, a, b);
      if (a.size() != own) sortAlphaQuads(a, fp.camPos);   // (only with the plume in view: the rest comes sorted)
    }
    g_ren.renderScene(fp, a, b);
    // The current, assigned aircraft is already rendered. Retain that clean scene before menu text or
    // panels can cover it, so Deploy can show the same aircraft instantly without another render/decode.
    if (screen == SCR_HUB && hubTab == TAB_CONTRACTS && hubList == 2)
      g_ren.retainLoadingPreview(fp, militarySelection);
    if (screen == SCR_LOADING && loadReadyT < 0) ++loadFrames;
  } else g_ren.clearScreen();
  if(screen==SCR_RESEARCH){g_ren.onMeshProgress=nullptr;g_ren.onMeshBatch=nullptr;}
  if (screen == SCR_LOADING) g_ren.bakeYield = nullptr;   // (only this frame's scene: an update never draws the loading screen)
  if (screen == SCR_RESEARCH && !resWarm) g_ren.bakeYield = nullptr;
  if (sceneOnly) return;
  if (photo.on && photo.capture && screen == SCR_FLIGHT) {   // (the shot: the scene just drawn, before any of the UI)
    photo.capture = 0;
    photo.last = savePhoto(); photo.savedT = realTime;
    if (!photo.last.empty()) g_audio.trigger(SFX_CHIME, 0.5f);
  }
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
    case SCR_FREE_FLIGHT: drawFreeFlightSetup(fp); break;
    case SCR_HUB: drawHub(); break;
    case SCR_FLIGHT:
      if (photo.on) { drawPhotoPanel(); break; }   // (photo mode: its own panel, or nothing)
      if (!uiHidden || paused || showMap) {   // (the HUD under the g-force lens, flushed on its own: the map and the pause menu stay clear of it)
        g_ren.flushUIPublic(); g_ren.uiGLoad = fp.gLoad; g_ren.uiTime = fp.time;
        drawHud(fp); g_ren.flushUIPublic(); g_ren.uiGLoad = 0;
        if (!uiHidden && !showMap) drawLandingStamp();
        if (realTime - photoFlashT < 0.4f) { const float k = 1.f - (realTime - photoFlashT) / 0.4f; g_ren.rect(0, 0, (float)g_ren.W, (float)g_ren.H, vec3(1, 1, 1), 0.75f * k * k); }   // (the photo flight's shot)
        drawMapOverlay();
      }
      if (paused) drawPause();
      break;
    case SCR_DEBRIEF: drawDebrief(); break;
    case SCR_RESEARCH: drawResearch(fp); break;
    case SCR_LOADING: drawLoading(); break;
  }
  if (!(uiHidden && screen == SCR_FLIGHT && !paused) && !photo.on) { drawToasts(); drawAchievementBanner(); }
  drawPadCursor();
  if (!headless && !photo.on) {   // always-on frame-rate counter, top right
    float s = S(), fps = 1.f / std::max(fpsAvg, 1e-4f);
    std::string t = fmt("%.0f FPS", fps);
    float ts = 13 * s, tw = g_ren.textWidth(t, ts);
    vec3 c = fps >= 57.f ? vec3(0.45f, 1.f, 0.6f) : fps >= 40.f ? vec3(1.f, 0.82f, 0.3f) : vec3(1.f, 0.4f, 0.35f);
    g_ren.rect(g_ren.W - tw - 18 * s, 4 * s, tw + 14 * s, ts + 8 * s, vec3(0, 0, 0), 0.45f, 3 * s);
    g_ren.text(g_ren.W - 11 * s, 7 * s, ts, t, c, 1, 2, false);
  }
  if (showPerf && !photo.on) {
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
  // Deploy, Fly, Initiate, Take flight and Restart are handled by the menus' UI, after this frame's scene was drawn.
  // Replace that entire menu frame now, so the very next SwapBuffers shows the loading card, never a frozen menu.
  if (screen == SCR_LOADING && launchCardPending()) renderLaunchCard();
}

// ------------------------------------------------------------------ development scenes for the render harness
// An airport's detail views (apv_ scenes, REGION_APV): 0 over the apron, 1 at the terminal's front, 2 along the stands,
// 3 from past the runway's end
static void airportView(int ai, int view, vec3& cam, vec3& look) {
  const Airport& a = g_world.airports[ai];
  AptLayout L = aptLayout(a, ai);
  float mid = 0.5f * (L.apU0 + L.apU1);
  if (view == 0) { cam = aptWorld(a, L.apU0 - 120.f, L.side * (L.twV - 60.f), 0); cam.y = a.elev + 70.f; look = aptWorld(a, mid, L.side * L.bldV, a.elev + 5.f); }
  else if (view == 1) { cam = aptWorld(a, L.termU - 140.f, L.side * (L.apV0 + 10.f), 0); cam.y = a.elev + 4.f; look = aptWorld(a, L.termU + 40.f, L.side * L.bldV, a.elev + 6.f); }
  else if (view == 2) { cam = aptWorld(a, L.standU0 - 60.f, L.side * (L.twV - 30.f), 0); cam.y = a.elev + 25.f; look = aptWorld(a, L.standU1, L.side * L.bldV, a.elev + 4.f); }
  else { cam = aptWorld(a, -a.length * 0.5f - 300.f, -L.side * 60.f, 0); cam.y = a.elev + 160.f; look = aptWorld(a, 0, L.side * L.apV0, a.elev); }
}
static std::string s_wxScene;   // a debug scene's weather: "<scene>@<WX>" (as the WX variable), for the render server's shots
void Game::debugScene(const std::string& name) {
  if (name.find('@') != std::string::npos) { s_wxScene = name.substr(name.find('@') + 1); debugScene(name.substr(0, name.find('@'))); s_wxScene.clear(); return; }
  if (name.compare(0, 7, "region-") == 0) {
    std::string regionId = name.substr(7);
    const bool ocean = regionId.size() > 6 && regionId.compare(regionId.size() - 6, 6, "_ocean") == 0;
    if (ocean) regionId.resize(regionId.size() - 6);
    auto descriptor = procedural::find(regionId);
    if (!descriptor) throw std::invalid_argument("Unknown region scene: " + name);
    beginFreeFlightSetup(); chooseFreeRegion(*descriptor); loadFreeRegion(freeRegion);
    while (screen == SCR_REGION_LOADING && (regionJob.valid() || regionUploading)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10)); updateRegionLoading(.01f);
    }
    if (!regionError.empty()) throw std::runtime_error(regionError);
    freeCraft = 0; freeAirport = 0; freeAirborne = true;
    if (const char* code = getenv("REGION_AIRPORT")) freeAirport = std::max(0, g_world.findAirport(code));   // (REGION_AIRPORT=<code>: start there)
    launchFreeFlight();
    camMode = 2; camYaw = .65f; camPitch = .2f; camZoom = 3.f; hudOn = false;
    if (ocean) {
      // Explicit review fixture beyond the land chart, looking east away from it.
      // No normal route-selection or region-transition code teleports a flight.
      plane.pos = vec3(140000.f, 2000.f, 0);
      plane.q = quat::axisAngle(vec3(0, 1, 0), -.5f * PI);
      plane.vel = plane.forward() * plane.spec->cruise;
      camYaw = -.5f * PI; camPitch = .15f;
    }
    if (getenv("REGION_ALT")) plane.pos.y = std::max(plane.pos.y, (float)atof(getenv("REGION_ALT")));
    if (getenv("REGION_TOD")) timeOfDay = (float)atof(getenv("REGION_TOD"));   // (REGION_TOD=<hours>: the time of day there)
    if (getenv("REGION_STYLE")) g_world.buildingStyle = atoi(getenv("REGION_STYLE"));   // (REGION_STYLE=<n>: its houses built another way)
    for (int i = 0; i < 8; ++i) updateCamera(.1f);
    // REGION_VIEW="x,y,z,lookx,looky,lookz": a fixed camera over the region (its overview, for a review shot)
    if (const char* view = getenv("REGION_VIEW")) {
      vec3 p, l;
      if (sscanf(view, "%f,%f,%f,%f,%f,%f", &p.x, &p.y, &p.z, &l.x, &l.y, &l.z) == 6) { dbgCam = true; dbgFollow = false; dbgCamPos = p; dbgCamLook = l; }
    }
    // REGION_APV=<view>: an airport detail view of REGION_AIRPORT's field (airportView)
    if (const char* apv = getenv("REGION_APV"); apv && getenv("REGION_AIRPORT")) {
      airportView(freeAirport, atoi(apv), dbgCamPos, dbgCamLook); dbgCam = true; dbgFollow = false;
    }
    // REGION_GPS=<range m>: the GPS open over the region (its airports and landmarks)
    if (const char* gps = getenv("REGION_GPS")) { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = std::max(2000.f, (float)atof(gps)); }
    toasts.clear(); botControl = true;
    if (getenv("REGION_HUB")) {   // REGION_HUB=1: the career's hub in the region (a CPL career travelled there, its board drawn)
      endFlight(false, "Region hub", OUT_ABANDONED);
      career = Career{}; career.license = LIC_CPL; career.money = 25000; career.storyIndex = 12;
      career.region = descriptor->id; career.location = procedural::mainAirport(descriptor->id); career.refreshBoard();
      freeFlight = isolatedFlight = false; screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 0; selContract = std::min(2, (int)career.board.size() + 1);
      dbgCam = false; realTime = 20; toasts.clear();
    }
    printf("Region scene: %s / %s / %zu airports / %zu towns / actual Free Flight\n",
      descriptor->id.c_str(), descriptor->name.c_str(), g_world.airports.size(), g_world.towns.size());
    return;
  }
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
    resCraft = name == "research40" ? kSomnus : name == "research10" ? kVigil : name == "research20" ? kReverie : kResearchJet; resLastCraft = resCraft; resSelT = 20; resAirport = std::max(0, g_world.findAirport("CAP"));
    resSeqOpened = resOpened; resSeq = realTime - resOpened; resSeqAt = realTime; resWarm = false; resWork = 1.f;   // (the boot sequence's own clock long past the shutters: drawResearch)
    return;
  }
  if (name.rfind("researchscan", 0) == 0) {   // the biometric sequence at a moment: researchscan<tenths of a second into the sequence>, the warm-up as far on as the scans show
    screen = SCR_RESEARCH; realTime = 30; resAuthed = false; resOpened = realTime - atoi(name.c_str() + 12) / 10.f;
    resSeq = realTime - resOpened; resSeqAt = realTime; resSeqOpened = resOpened;
    resWarm = resSeq < kResScan1; resWork = clampf((resSeq - kResScan0) / (kResScan1 - kResScan0), 0.f, 1.f); resWarmFrames = 1 + (int)(resWork * 4.f * (2 * resCraftCount() + 3)); resWarmCraft = -1;
    return;
  }
  if (name.compare(0, 3, "wr_") == 0) {   // XR-40: wr_<mode>_<cam yaw>_<cam pitch>_<cam dist>_<seconds>[_<airport>_<weather>_<hour>_<over town>]
    // modes: 0 cruise, 1 hover, 2 parked, 3 cloak spreading, 4 cloaked, 5 turrets out + bay open, 6 lasers firing,
    // 7 plasma bomb (camera on the impact), 8 cockpit. The optional tail: the research flight's airport, weather (0 clear,
    // 1 cloud, 2 storm) and hour, and 1 to fly it in low over the town nearest that airport (wr_8_0_-8_0_1_3_2_22.5_1:
    // the cockpit at night in a storm, coming in over Solace Capital)
    int mode = 0, site = -1, wxSel = -1, overTown = 0; float yawD = 210, pitD = 12, dist = 30, secs = 1.5f, hour = -1.f;
    sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f_%d_%d_%f_%d", &mode, &yawD, &pitD, &dist, &secs, &site, &wxSel, &hour, &overTown);
    const int siteWas = resAirport, wxWas = resWx;
    if (site >= 0) resAirport = std::min(site, (int)g_world.airports.size() - 1);
    if (wxSel >= 0) resWx = std::min(wxSel, 2);
    resCraft = kSomnus; realTime = 20; resAirborne = mode != 2; resTime = getenv("TOD") ? (float)atof(getenv("TOD")) : hour >= 0.f ? hour : 12.f; launchResearch();
    resAirport = siteWas; resWx = wxWas;   // (the research menu's own choices: this scene's are for this flight only)
    // (over town 1: 3 km short of the town's centre, 450 m over it, heading across it at 200 m/s. 2: the same flight
    // over its centre, held there - flying on, going nowhere - a scene that holds still, for timing the worst of it: in
    // the analysis the flypast was over the city for some measurements and short of it for others, and a hover on the
    // pods climbed away from it)
    const bool pinTown = overTown == 2 && resAirborne;
    if (overTown && resAirborne && !g_world.towns.empty()) {
      const Airport& a = g_world.airports[std::clamp(site, 0, (int)g_world.airports.size() - 1)];
      const Town* T = &g_world.towns[0]; float best = 1e18f;
      for (const Town& town : g_world.towns) { const float d = (town.x - a.x) * (town.x - a.x) + (town.z - a.z) * (town.z - a.z); if (d < best) { best = d; T = &town; } }
      const vec3 c(T->x, 0.f, T->z), from = c + normalize(vec3(a.x - T->x, 0.f, a.z - T->z)) * 3000.f;
      const float hdg = atan2f(c.x - from.x, -(c.z - from.z)) / DEG;
      const vec3 at = pinTown ? c : from;
      plane.reset(&kAircraft[resCraft], vec3(at.x, std::max(g_world.height(at.x, at.z), 0.f) + 450.f, at.z), hdg, kAircraft[resCraft].maxFuel, 85, true, 200.f);
      plane.ctl.throttle = 0.7f; settleAirborneStart();
      camQ = plane.q; camPos = plane.pos + plane.q.rotate(vec3(0, 4, 26));
      if (pinTown) { benchPin = true; benchPinPos = plane.pos; benchPinQ = plane.q; }
    }
    botControl = true;
    if (mode == 1) { plane.ctl.flaps = 1; flapNotch = 1; plane.flaps = plane.nozzle = 1; plane.vel = vec3(); plane.ctl.throttle = 0.66f; plane.engineRunning = true; plane.engineSpool = 0.66f; }
    else if (mode != 2) { plane.apEngage(Plane::AP_HOLD, -1, wx); }
    if (mode == 3 || mode == 4) somnus.cloakOn = true;
    if (getenv("WRCLOUD")) { wx.cloudCover = (float)atof(getenv("WRCLOUD")); wx.cloudBase = plane.pos.y - 60.f; }   // craft inside the cloud deck
    if (mode == 5 || mode == 6) { somnus.armed = true; }
    if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.apSpeed = 900.f; }
    if (getenv("WRSPD")) { plane.vel = plane.forward() * (float)atof(getenv("WRSPD")); plane.apSpeed = (float)atof(getenv("WRSPD")); }
    if (mode == 5) somnus.bayHold = 100.f;
    // 9: cockpit looking down while a bomb falls (secs after release); 10: the same, secs after it goes off
    bool bombCk = mode == 9 || mode == 10 || (mode == 8 && getenv("WRBOMB"));
    if (mode == 7 || bombCk) { somnus.bombQueue = 1; somnus.armed = bombCk; }
    if (getenv("WRUFO")) { startUfo(); ufo.side = (float)atof(getenv("WRUFO")); }   // UFO alongside (side -1 / 1)
    float firstDrop = 0; vec3 blastAt;
    int frames = (int)((mode == 3 ? 0.55f : mode == 7 || mode == 10 ? 60.f : mode == 9 ? 30.f : secs) * 60.f);
    float dropT = -1, boomAt = -1;
    for (int i = 0; i < frames; i++) {
      if (mode == 9 || mode == 10) {
        if (dropT < 0 && !somnus.bombs.empty()) dropT = realTime;
        if (boomAt < 0 && !somnus.blasts.empty()) boomAt = realTime;
        if (mode == 9 && dropT >= 0 && realTime - dropT > secs) break;
        if (mode == 10 && boomAt >= 0 && realTime - boomAt > secs) break;
      }
      realTime += 1 / 60.f; update(1 / 60.f);
      if (mode == 6) somnus.wantFire = true;   // trigger held
      if (getenv("WRTHR")) { plane.ctl.throttle = (float)atof(getenv("WRTHR")); plane.engineSpool = std::max(plane.engineSpool, plane.ctl.throttle * 0.98f); }
      if (mode == 7 && !somnus.blasts.empty() && firstDrop == 0) { firstDrop = realTime; blastAt = somnus.blasts[0].p; }
      if (mode == 7 && firstDrop > 0 && realTime - firstDrop > secs) break;
    }
    if (mode == 6) {
      for (auto& b : somnus.bolts) b.life = b.age + 1.f;   // keep the bolts in flight bright for the screenshot
      // the streak of the youngest visible bolt must start at its lens, however fast the craft flies
      // (in the craft's frame the tail sits on the lens's firing line, 2000 m/s x age - streak out from it)
      float worstOff = 0, worstAlong = 0; int n = 0;
      for (auto& b : somnus.bolts) if (b.len > 0.05f && !b.hit && b.age < 0.5f) {
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
    for (auto& b : somnus.blasts) printf("wr: blast age %.2f R %.0f at %.0f %.0f %.0f particles %d\n", b.age, b.R, b.p.x, b.p.y, b.p.z, (int)particles.size());
    printf("wr: mode %d stealth %.2f lasers %.2f bay %.2f bombs %d blasts %d tilt %.2f %.2f %.2f %.2f thr %.2f\n", mode, somnus.stealth, somnus.lasers, somnus.bay, (int)somnus.bombs.size(), (int)somnus.blasts.size(),
           plane.podTilt[0], plane.podTilt[1], plane.podTilt[2], plane.podTilt[3], plane.podThr[0]);
    if (mode == 8 || mode == 9 || mode == 10) { camMode = 1; lookYaw = yawD * DEG; lookPitch = pitD * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f; return; }
    hudOn = false; dbgCam = true;
    float h = plane.heading() * DEG, yw = h + yawD * DEG, pt = pitD * DEG;
    vec3 focus = mode == 7 && firstDrop > 0 ? blastAt + vec3(0, 30.f, 0) : plane.pos;
    dbgCamLook = focus; dbgCamPos = focus + vec3(sinf(yw) * cosf(pt), sinf(pt), -cosf(yw) * cosf(pt)) * dist;
    if (!(mode == 7 && firstDrop > 0)) { dbgFollow = true; dbgFollowOff = dbgCamPos - plane.pos; }
    return;
  }
  if (name == "rboom") {   // the XR-30 the moment it goes through Mach 1, the chase camera behind it: the boom's collar and rings
    realTime = 20; resAirborne = true; launchResearch();
    for (int i = 0; i < 60; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    plane.vel = plane.forward() * 346.f; audioMachArmed = true; toasts.clear();
    return;
  }
  if (name == "rjet" || name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") { realTime = 20; resAirborne = true; launchResearch(); if (name == "rjetc" || name == "rjetl" || name == "rjetd" || name == "rjetr") camMode = 1; for (int i = 0; i < 90; i++) { realTime += 1 / 30.f; update(1 / 30.f); } toasts.clear(); if (name == "rjetl") lookYaw = 1.75f; if (name == "rjetr") { lookYaw = -1.2f; lookPitch = -0.6f; } if (name == "rjetd") lookPitch = -0.75f; return; }
  if (name == "radio") { loadStations(); screen = SCR_HUB; showRadio = true; realTime = 20; radioScroll = 6; return; }
  if (name.size() == 4 && name.compare(0, 3, "hub") == 0) { screen = SCR_HUB; hubTab = name[3] - '0'; realTime = 20; return; }
  Contract c = g_story[0];
  int spec = 0;
  if (name.size() == 3 && name[0] == 'm' && ((name[1] >= '0' && name[1] <= '9') || (name[1] >= 'a' && name[1] < 'a' + kAircraftCount - 10))) {
    // model inspection: m<aircraft><view>  views: e = exterior 3/4 front, r = controls deflected from behind,
    // c = cockpit, g = on the ground, s = side, d = cockpit with controls deflected, f = a split flap (the left one
    // stuck up, the right one down) from behind
    spec = name[1] >= 'a' ? name[1] - 'a' + 10 : name[1] - '0'; char v = name[2];   // (aircraft 10 on: a, b, c ...)
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
    if (v == 'f') { plane.failNow(FAIL_FLAP_ASYM, 0); botControl = true; plane.ctl.flaps = 1.f; plane.flaps = aeroGeom(s).flapMax; camYaw = 0.f; camPitch = 0.3f; camZoom = 0.3f; }
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
  if (const char* sp = getenv("SPEC")) for (int t : kCareerAircraft) if (t == atoi(sp)) spec = t;   // (SPEC=<career aircraft>: that one flies the scene)
  startFlight(c, spec, Career::SRC_OWNED);
  if (const char* w = !s_wxScene.empty() ? s_wxScene.c_str() : getenv("WX")) {   // weather checks: WX="cover,base m,precip,storm,wind kt,from deg,gust kt,turbulence,time of day"
    float v[9] = {wx.cloudCover, wx.cloudBase, (float)wx.precip, wx.storm ? 1.f : 0.f, wx.windSpeed * MS_TO_KT, wx.windFrom, wx.gust * MS_TO_KT, wx.turbulence, wx.timeOfDay};
    sscanf(w, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
    wx.cloudCover = v[0]; wx.cloudBase = v[1]; wx.precip = (int)v[2]; wx.storm = v[3] > 0.5f; wx.windSpeed = v[4] / MS_TO_KT; wx.windFrom = v[5];
    wx.gust = v[6] / MS_TO_KT; wx.turbulence = v[7]; wx.timeOfDay = timeOfDay = v[8]; wx.visibility = wx.precip ? 9000.f : 40000.f;
    wxStart = wx; contract.wx = wx; contract.wxShift = false;
  }
  realTime = 10;
  plane.starterTime = 0.01f; plane.engineRunning = true; plane.rpm = 1000; engineAutoStarted = true;
  {
    float px, pz, agl, hdg; char cm = 'c';
    float bagl = 300, blook = -70, bafter = 0.25f;
    if (sscanf(name.c_str(), "wrbomb_%f_%f_%f", &bagl, &blook, &bafter) >= 1) {   // XR-40 cockpit: drop a bomb and watch it go off through the glass floor
      resCraft = kSomnus; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      plane.pos.y = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + bagl;
      plane.apEngage(Plane::AP_HOLD, -1, wx); plane.apAlt = plane.pos.y; botControl = true;
      somnus.armed = true;
      for (int i = 0; i < 90; i++) { realTime += 1 / 60.f; update(1 / 60.f); }
      somnus.bombQueue = 1;
      float since = -1;
      for (int i = 0; i < 60 * 40; i++) {
        realTime += 1 / 60.f; update(1 / 60.f);
        if (since < 0 && !somnus.blasts.empty()) since = 0;
        if (since >= 0) { since += 1 / 60.f; if (since >= bafter) break; }
        if (since < 0 && bafter < 0 && !somnus.bombs.empty() && somnus.bombs[0].t > -bafter) break;   // negative: a falling bomb
      }
      printf("wrbomb: bombs %d blasts %d pip %d\n", (int)somnus.bombs.size(), (int)somnus.blasts.size(), 0);
      camMode = 1; lookYaw = 0; lookPitch = blook * DEG; camYaw = 0; camPitch = lookPitch + 0.12f; hudOn = true; toasts.clear(); hint.clear();
      return;
    }
    float fx, fz, fsec = 3, fpitch = 28, fyaw = 0;
    if (sscanf(name.c_str(), "lasertest_%f_%f_%f_%f_%f", &fx, &fz, &fsec, &fpitch, &fyaw) >= 2) {   // XR-40 firing at the ground ahead from a fixed hover
      resCraft = kSomnus; realTime = 20; resAirborne = true; resTime = 12.f; launchResearch();
      float g = std::max(g_world.height(fx, fz), 0.f);
      vec3 pos(fx, g + 70.f, fz);
      quat q = quat::axisAngle(vec3(0, 1, 0), -fyaw * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG);
      somnus.armed = true; botControl = true;
      for (int i = 0; i < (int)((fsec + 1.f) * 60.f); i++) {
        plane.pos = pos; plane.q = q; plane.vel = vec3(); plane.w = vec3();
        if (i > 60) somnus.wantFire = true;
        realTime += 1 / 60.f; update(1 / 60.f);
        if ((i & 7) == 0) { plane.q = quat::axisAngle(vec3(0, 1, 0), (-fyaw + 6.f * sinf(i * 0.05f)) * DEG) * quat::axisAngle(vec3(1, 0, 0), -fpitch * DEG); q = plane.q; }   // sweep the aim
      }
      printf("lasertest: %d destroyed, %d scorch pits, %d bolts in flight\n", somnus.wrecked, (int)somnus.scorch.size(), (int)somnus.bolts.size());
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
    // the weather in flight: straight and level on the autopilot from (x, altitude, z) for some seconds (through the
    // cloud and rain WX sets), then the chase camera (c), the cockpit (k) or a look back along the path flown from
    // beside the aircraft (b: its wake through the cloud; t from above and behind; w inside the channel half a km back;
    // o off to the side of the hole it came out of the cloud by): wxfly_<x>_<z>_<alt m>_<hdg>_<seconds>_<cam>_<aircraft>
    float fsecs = 0; int fspec = 1;
    if (sscanf(name.c_str(), "wxfly_%f_%f_%f_%f_%f_%c_%d", &px, &pz, &agl, &hdg, &fsecs, &cm, &fspec) >= 5) {
      fspec = validCareerSelection(fspec);
      plane.reset(&kAircraft[fspec], vec3(px, agl, pz), hdg, kAircraft[fspec].maxFuel, 100, true, kAircraft[fspec].cruise);
      takeoffAnnounced = true; camQ = plane.q; hudOn = false; hint.clear(); botControl = true;
      plane.apEngage(Plane::AP_HOLD, -1, wx); plane.apAlt = agl; plane.apHeading = hdg;
      if (cm == 'k') camMode = 1;
      // (negative seconds: on until that long after it came out of a cloud it went through, at most two minutes)
      float inT = 0, outT = 0;
      for (int i = 0; i < (int)(fabsf(fsecs) * 60.f) || (fsecs < 0.f && i < 7200); i++) {
        if (crashed) break;
        realTime += 1 / 60.f; update(1 / 60.f);
        if (plane.wxl.cloud > 0.2f) { inT += 1 / 60.f; outT = 0; } else if (inT > 2.f && plane.wxl.cloud < 0.02f) outT += 1 / 60.f;
        if (fsecs < 0.f && inT > 2.f && outT >= -fsecs) break;
      }
      printf("wxfly: at (%.0f, %.0f, %.0f) %.0f kt, cloud here %.2f, rain %.2f, eddies %.2f m/s, lift %+.1f thermal %+.1f, wake points %d\n", plane.pos.x, plane.pos.y, plane.pos.z,
             plane.airspeed * MS_TO_KT, plane.wxl.cloud, plane.wxl.rain, plane.wxl.sigma, plane.wxl.lift, plane.wxl.thermal, (int)cloudWake.size());
      toasts.clear();
      if (cm == 't' && cloudWake.size() >= 5) {   // (from above and ahead, looking down along the path: its trench through a cloud top)
        const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake.back().c - off, B = cloudWake[cloudWake.size() - 5].c - off;
        dbgCam = true; dbgFollow = false; dbgCamPos = A + normalize(A - B) * 120.f + vec3(0, 140.f, 0); dbgCamLook = (A + B) * 0.5f;
        for (float h : {140.f, 260.f, 420.f}) {   // (high enough to be out of the cloud)
          const vec3 cp = A + normalize(A - B) * (0.85f * h) + vec3(0, h, 0);
          if (wxfield::cloudDensity(wx, cp, true) < 0.01f && wxfield::cloudDensity(wx, (cp + dbgCamLook) * 0.5f + vec3(0, 40.f, 0), true) < 0.05f) { dbgCamPos = cp; break; }
        }
      }
      if (cm == 'w' && cloudWake.size() >= 9) {   // (inside the channel half a kilometre back, looking on back along it)
        const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake[cloudWake.size() - 5].c - off, B = cloudWake[cloudWake.size() - 9].c - off;
        dbgCam = true; dbgFollow = false; dbgCamPos = A + vec3(0, 4.f, 0); dbgCamLook = B;
      }
      // (a camera in clear air with a clear view of a point: the first of some places round it with no cloud on the line
      // to it, short of the last stretch)
      auto clearView = [&](vec3 at, vec3 d, float short_) {
        const vec3 sd = normalize(cross(d, vec3(0, 1, 0)));
        for (float dist : {350.f, 600.f, 950.f}) for (float az : {35.f, -35.f, 65.f, -65.f, 15.f, 90.f}) for (float el : {6.f, 20.f, 40.f}) {
          const vec3 cp = at + (d * cosf(az * DEG) + sd * sinf(az * DEG)) * (dist * cosf(el * DEG)) + vec3(0, dist * sinf(el * DEG), 0);
          bool clear = true;
          for (float t = 0.f; t < 1.f && clear; t += 0.02f) { const vec3 q = cp + (at - cp) * t; if (length(q - at) > short_ && wxfield::cloudDensity(wx, q, true) > 0.03f) clear = false; }
          if (clear) return cp;
        }
        return at + d * 400.f + vec3(0, 60.f, 0);
      };
      if (cm == 'o' && cloudWake.size() >= 2) {   // (off to the side, at the hole where the path came out of the cloud)
        const vec3 off(cloudOff.x, 0, cloudOff.y);
        int e = (int)cloudWake.size() - 1;
        while (e > 0 && wxfield::cloudDensity(wx, cloudWake[e].c - off, true) < 0.25f) e--;
        const vec3 E = cloudWake[e].c - off, d = normalize(cloudWake.back().c - cloudWake[std::max(e - 1, 0)].c);
        dbgCam = true; dbgFollow = false; dbgCamPos = clearView(E, d, 90.f); dbgCamLook = E;
      }
      if (cm == 'b') {   // (down the wake's own line: the path drifts a little off the heading)
        dbgCam = true; dbgFollow = false; vec3 f = plane.forward(); dbgCamPos = plane.pos - f * 8.f + vec3(0, 2.f, 0); dbgCamLook = plane.pos - f * 400.f;
        if (cloudWake.size() >= 5) {
          const vec3 off(cloudOff.x, 0, cloudOff.y), A = cloudWake.back().c - off, B = cloudWake[cloudWake.size() - 5].c - off;
          dbgCamPos = A + normalize(A - B) * 40.f + vec3(0, 1.f, 0); dbgCamLook = B;
        }
      }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
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
  if (name == "air" || name == "sunset" || name == "mountain" || name == "cockpit" || name == "jet" || name == "storm" || name == "snow" || name == "hud" || name == "forest" || name == "seam" || name == "seamgps" || name == "at") {
    vec3 p(-4000, 600, 9000); float hdg = 40;
    if (name == "forest") { p = vec3(-960, 270, 23500); hdg = 0; }   // (low, north up the island's longest forest: 9 km of it ahead, 130-250 m below - what the trees cost, their fades and detail cross-fades)
    if (name == "sunset") { timeOfDay = 18.2f; p = vec3(-26000, 300, 14000); hdg = 270; }
    if (name == "mountain") { p = vec3(-20000, 1900, -9000); hdg = 60; }
    if (name == "jet") { p = vec3(8000, 900, 6000); hdg = 80; }
    if (name == "storm") { p = vec3(10000, 700, 10000); hdg = 120; }
    if (name == "snow") { p = vec3(20000, 600, -24000); hdg = 80; }
    if (name == "at") {   // AT="x,z,height above the ground,heading": anywhere (CAMPITCH tilts the chase camera down, degrees)
      float x = 0, z = 0, agl = 300, h = 0; if (getenv("AT")) sscanf(getenv("AT"), "%f,%f,%f,%f", &x, &z, &agl, &h);
      p = vec3(x, std::max(g_world.height(x, z), 0.f) + agl, z); hdg = h;
    }
    if (name == "seam" || name == "seamgps") { p = vec3(WRAP_HALF - 1500.f, getenv("SEAMALT") ? (float)atof(getenv("SEAMALT")) : 1500.f, 4000.f); hdg = getenv("SEAMHDG") ? (float)atof(getenv("SEAMHDG")) : 90.f; }   // (out east of the islands, past where the map used to wrap: the open ocean ahead)
    plane.reset(&kAircraft[spec], p, hdg, kAircraft[spec].maxFuel, 100, true, kAircraft[spec].cruise);
    takeoffAnnounced = true;
    camQ = plane.q;
    if (name == "cockpit") camMode = 1;
    if (name == "storm" && getenv("STRIKE")) {   // STRIKE=<metres ahead>,<degrees off the nose>,<seconds into it at the shot>: a stroke to the ground
      float R = 1500.f, off = 0.f, at = 0.06f; sscanf(getenv("STRIKE"), "%f,%f,%f", &R, &off, &at);
      const float a = (hdg + off) * DEG;
      const vec3 foot = p + vec3(sinf(a) * R, 0.f, -cosf(a) * R);
      makeStrike(vec3(foot.x + 150.f, std::max(wx.cloudBase, 300.f), foot.z - 80.f), vec3(foot.x, std::max(g_world.height(foot.x, foot.z), 0.f), foot.z), true, 7);
      strikes.back().t = at - 0.1f;   // (the shot's three frames, a 30th of a second apart, on)
      nextLightning = 1e9f;
    }
    if (name == "hud" && getenv("TDSTAMP")) { tdStampT = realTime - 0.6f; tdStampFpm = (float)atof(getenv("TDSTAMP")); }   // (TDSTAMP=fpm: the touchdown stamp)
    if (name == "hud") {   // HUDDEMO=1: every warning, a tower call, failures and messages lit, to look at the layout
      if (getenv("HUDDEMO")) {
        hudDemo = true;
        atcF.lastCall = "Ambler Alfa Bravo, runway 05, wind 040 at 8, cleared to land."; atcF.lastApt = contract.to; atcF.lastT = flightClock - 42.f;
        plane.fail.engineHealth[0] = 0.4f; plane.fail.alternator = true; plane.fail.battery = 0.6f; plane.fail.ice = 0.3f;
        toast("Checkpoint 2 of 6", vec3(0.5f, 1, 0.7f)); toast("Gear down", vec3(0.8f, 1, 0.8f)); toast("Time acceleration off", vec3(0.8f, 0.8f, 0.8f));
        hint = "Hold a steady 3 degree descent on power, then ease back to flare as the runway fills the windscreen.";
        plane.apOn = true; plane.apMode = Plane::AP_HOLD; plane.apHeading = hdg + 25.f; plane.apAlt = 900.f; plane.apStatus = fmt("HDG %03.0f  ALT %.0f", hdg + 25.f, 900.f * M_TO_FT);
        showMinimap = true;
      }
    }
  }
  if (name == "rings" || name == "ringburst") {
    // (RINGWP: the gates already flown through; RINGDIST, RINGUP: the start that far before the next, that high above its centre)
    if (getenv("RINGWP")) wpIndex = std::min(std::max(atoi(getenv("RINGWP")), 0), std::max((int)contract.wps.size() - 1, 0));
    vec3 c, ax, ay; ringGeom(wpIndex, c, ax, ay);
    vec3 nd = normalize(cross(vec3(0, 1, 0), ax));
    float dist = name == "rings" ? 420.f : 160.f;
    if (getenv("RINGDIST")) dist = (float)atof(getenv("RINGDIST"));
    float hdg = atan2f(nd.x, -nd.z) / DEG;
    plane.reset(&kAircraft[0], c - nd * dist - vec3(0, getenv("RINGUP") ? -(float)atof(getenv("RINGUP")) : 12.f, 0), hdg, kAircraft[0].maxFuel, 100, true, kAircraft[0].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear(); botControl = true;
    if (getenv("RINGCOCKPIT")) camMode = 1;
    plane.ctl = Controls(); plane.ctl.throttle = 0.7f;
    if (getenv("SIGHT")) { sightReq = atoi(getenv("SIGHT")); sightAsked = true; sightDone = false; }   // (SIGHT=<site>: the passengers' request on the HUD)
    if (getenv("GHOST")) {   // (a gate run's ghost alongside: the best run 80 m ahead and 30 m to the right)
      contract.type = CT_TRIAL; trialT0 = flightClock; trialT1 = -1.f; ghostBest.clear();
      const vec3 rt = normalize(cross(nd, vec3(0, 1, 0)));
      for (int i = 0; i < 400; i++) ghostBest.push_back(plane.pos + nd * (80.f + i * kGhostDt * kAircraft[0].cruise * 1.05f) + rt * 30.f);
    }
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
    for (int i = 0; i < (name == "rings" ? 60 : 42); i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (getenv("PHOTO")) {   // (PHOTO=<look 0-4>: photo mode over the scene; PHOTOFRAME=<0-3>; PHOTOPANEL=0: without its panel; PHOTOSHOT=<folder>: the shot taken, saved there)
      botControl = false; enterPhoto(); botControl = true;
      photoLook(atoi(getenv("PHOTO")));
      if (getenv("PHOTOFRAME")) photo.frame = atoi(getenv("PHOTOFRAME"));
      if (getenv("PHOTOPANEL")) photo.panel = atoi(getenv("PHOTOPANEL")) != 0;
      if (getenv("PHOTOSHOT")) { photoDir = getenv("PHOTOSHOT"); photo.capture = 1; }
      if (getenv("PHOTOPAD")) in.pad = true;   // (PHOTOPAD: the panel's controller prompts)
    }
    return;
  }
  if (name.compare(0, 8, "airbreak") == 0) {   // XR-30 overstressed at speed: breaks up in the air; airbreak<seconds after>
    bool cessna = name.size() > 8 && name[8] == 'c';   // airbreakc<s>: a light aircraft pushed over hard in a dive instead
    float after = name.size() > 8 + cessna ? atof(name.c_str() + 8 + cessna) : 3.f;
    resAirborne = true; realTime = 20; launchResearch(); hudOn = false;
    const int bt = getenv("BRKTYPE") ? atoi(getenv("BRKTYPE")) : 1;   // (debug: the type that breaks up, for airbreakc)
    if (cessna) { plane.reset(&kAircraft[bt], plane.pos, 90, kAircraft[bt].maxFuel, 100, true, kAircraft[bt].cruise); researchFlight = false; }
    const float agl = getenv("BRKALT") ? (float)atof(getenv("BRKALT")) : 2500.f;   // (debug: a lower break-up, its pieces down in seconds)
    plane.pos.y = std::max(getenv("BRKALT") ? 0.f : plane.pos.y, std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + agl);
    plane.vel = plane.forward() * (cessna ? 95.f : 620.f); plane.ctl.throttle = 1; plane.engineSpool = 1; botControl = true; plane.ctl.pitch = cessna ? -1.f : 1.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.25f; camZoom = 1.2f;
    float tb = -1, tEnd = -1;
    for (int i = 0; i < 60 * 200; i++) {
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("airbreak: broke up (%s), %d pieces, %.0f m AGL\n", plane.ev.crashReason.c_str(), (int)wreck.size(), plane.agl()); }
      if (crashed && tEnd < 0 && crashEndT < 1e8f) { tEnd = crashTimer; float sp = 0; for (auto& w : wreck) sp = std::max(sp, length(w.v));
        float big = craterR; for (const Pit& p : pits) big = std::max(big, p.R);
        printf("airbreak: all pieces down %.1f s after the break-up (%zu craters, the largest %.1f m) %s\n", crashTimer, pits.size(), big, crashTimer > 5.f ? "ok" : "FAIL"); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("airbreak: FAIL - no break-up\n");
    if (getenv("BRKZOOM")) camZoom = (float)atof(getenv("BRKZOOM"));   // (debug: closer in)
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 5, "crash") == 0) {   // crash<type>_<seconds after>: type flown into the ground in a steep dive at cruise speed
    int idx = 1; float after = 1.f; sscanf(name.c_str() + 5, "%d_%f", &idx, &after);
    idx = std::clamp(idx, 0, kAircraftCount - 1);
    if (isResearchAircraft(idx)) { resCraft = idx; resAirborne = true; realTime = 20; launchResearch(); hudOn = false; }
    else plane.reset(&kAircraft[idx], vec3(-960, 0, 23500), 0, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise);
    {   // (over land, and land where it comes down: some 150 m on)
      const vec3 f = normalize(vec3(plane.forward().x, 0, plane.forward().z));
      const vec3 o = g_world.airports.empty() ? vec3(-960, 0, 23500) : g_world.airports[0].pos() + vec3(250, 0, 250);   // (open ground by an airport)
      for (int k = 0; k < 400; k++) {
        const vec3 p0 = vec3(o.x + 97.f * (k % 20), 0, o.z - 113.f * (k / 20)), p1 = p0 + f * 150.f;
        if (g_world.height(p0.x, p0.z) > 15.f && g_world.height(p1.x, p1.z) > 15.f) { plane.pos.x = p0.x; plane.pos.z = p0.z; break; }
      }
    }
    plane.pos.y = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) + 180.f;
    plane.q = quat::axisAngle(plane.right(), -50.f * DEG) * plane.q;
    plane.vel = plane.forward() * (isResearchAircraft(idx) ? 250.f : kAircraft[idx].cruise); plane.ctl.throttle = 1; botControl = true; plane.ctl.pitch = 0.f;
    camMode = 2; camYaw = 0.9f; camPitch = 0.3f; camZoom = getenv("BRKZOOM") ? (float)atof(getenv("BRKZOOM")) : 1.8f;
    float tb = -1, vHit = 0;
    for (int i = 0; i < 60 * 60; i++) {
      if (!crashed) vHit = length(plane.vel);
      realTime += 1 / 60.f; update(1 / 60.f);
      if (crashed && tb < 0) { tb = flightClock; printf("crash: %s, %d pieces, %.0f m/s\n", plane.ev.crashReason.c_str(), (int)wreck.size(), vHit); }
      if (crashed && crashTimer >= after) break;
    }
    if (!crashed) printf("crash: FAIL - never hit\n");
    if (getenv("WRECKDBG"))   // each piece where it ended: down or not, at rest, how far it dug in, its lowest corner against the ground there
      for (const WreckPiece& w : wreck) {
        float low = 1e9f, gnd = 0.f;
        const vec3 lo = w.body.lo - w.body.cg, hi = w.body.hi - w.body.cg;
        for (int k = 0; k < 8; k++) {
          const vec3 cw = w.c + w.q.rotate(vec3((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z) * 0.9f);
          if (cw.y - wreckGround(cw.x, cw.z) < low) { low = cw.y - wreckGround(cw.x, cw.z); gnd = wreckGround(cw.x, cw.z); }
        }
        printf("  piece kind %d: %s%s, %.2f m dug in, lowest corner %+.2f m from the ground (%.1f m; %.1f under the surface's), %.1f m/s\n", w.kind, w.landed ? "down" : "in the air",
               w.rest ? ", at rest" : "", w.bury, low, gnd, g_world.height(w.c.x, w.c.z, 6) - gnd, length(w.v));
      }
    for (int i = 0; i < 5; i++) updateCamera(1 / 60.f);
    toasts.clear(); return;
  }
  if (name.compare(0, 3, "ckv") == 0) {   // cockpit view of aircraft N: ckv<N>_<look yaw deg>_<look pitch deg>_<hour>[_<roll input -1..1>[_<flaps 0..1>]][@<x>,<z>]
    int idx = 0; float ly = 0, lpch = -8, hour = 11, roll = 0, fl = 0; sscanf(name.c_str() + 3, "%d_%f_%f_%f_%f_%f", &idx, &ly, &lpch, &hour, &roll, &fl);
    if (idx == kResearchJet) { resAirborne = true; resTime = hour; launchResearch(); }
    else { timeOfDay = hour; plane.reset(&kAircraft[idx], vec3(-4000, 600, 9000), 40, kAircraft[idx].maxFuel, 100, true, kAircraft[idx].cruise); camQ = plane.q; takeoffAnnounced = true; }
    size_t at = name.find('@');   // (ckv...@x,z: out at x, z - by the map's edge, where world-space floats are coarsest)
    float ax = 0, az = 0;
    if (at != std::string::npos && sscanf(name.c_str() + at + 1, "%f,%f", &ax, &az) == 2) { plane.pos = vec3(ax, std::max(g_world.height(ax, az), 0.f) + 600.f, az); camQ = plane.q; }
    camMode = 1; hint.clear(); toasts.clear();
    for (int i = 0; i < 4; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    if (const char* cs = getenv("CKCTL")) {   // (debug: the controls held at pitch,roll,yaw,throttle - yokes, pedals and levers at their stops,
      // the aircraft held where it was while they get there: flown, full aft stick at cruise breaks a jet up)
      float cp = 0, cr = 0, cy = 0, ct = 0.7f; sscanf(cs, "%f,%f,%f,%f", &cp, &cr, &cy, &ct);
      botControl = true;
      const vec3 p0 = plane.pos, v0 = plane.vel; const quat q0 = plane.q;
      for (int i = 0; i < 20; i++) {
        plane.ctl.pitch = cp; plane.ctl.roll = cr; plane.ctl.yaw = cy; plane.ctl.throttle = ct; plane.engineSpool = ct;
        realTime += 1 / 30.f; update(1 / 30.f);
        plane.pos = p0; plane.vel = v0; plane.q = q0; plane.w = vec3(); plane.gLoad = 1.f;
      }
      gTunnel = 0;
    }
    lookYaw = ly * DEG; lookPitch = lpch * DEG; camYaw = lookYaw; camPitch = lookPitch + 0.12f;
    if (!getenv("CKCTL")) { plane.ctl.roll = roll; botControl = roll != 0.f || fl != 0.f; }   // (a held roll input: the yokes turn)
    if (fl != 0.f) { plane.ctl.flaps = fl; plane.flaps = fl; }
    toasts.clear(); hint.clear(); return;
  }
  if (name.compare(0, 3, "brk") == 0) {   // an in-flight break-up: brk<aircraft>_<seconds after>_<view yaw deg>: the pieces, seen from 30 m
    int idx = 0; float secs = 0.4f, vy = 90.f; sscanf(name.c_str() + 3, "%d_%f_%f", &idx, &secs, &vy);
    Contract c; c.from = 0; c.to = 1; c.title = "Break-up"; c.wx = Weather(); c.wx.timeOfDay = 13.f; c.wx.cloudCover = 0.1f;
    realTime = 20; startFlight(c, idx, Career::SRC_OWNED);
    plane.reset(&kAircraft[idx], vec3(-4000, 900, 9000), 40, kAircraft[idx].maxFuel * 0.5f, 100, true, kAircraft[idx].cruise * 0.7f);
    takeoffAnnounced = true; camQ = plane.q;
    plane.ev.crashed = true; plane.ev.crashReason = "Structural failure";
    for (float tt = 0; tt < secs + 1 / 30.f; tt += 1 / 30.f) { realTime += 1 / 30.f; update(1 / 30.f); }
    vec3 cen; for (auto& w : wreck) cen = cen + w.c; if (!wreck.empty()) cen = cen * (1.f / wreck.size());
    vec3 off(sinf(vy * DEG) * 30.f, 12.f, -cosf(vy * DEG) * 30.f);
    dbgCam = true; dbgFollow = false; dbgCamPos = cen + off; dbgCamLook = cen;
    toasts.clear(); hint.clear(); uiHidden = true; hudOn = false; return;
  }
  if (name.compare(0, 3, "trf") == 0) {   // AI traffic: trf<seconds>_<view> at Solace Capital; view 0 = airport overview, k = chase craft k-1
    float secs = 60; int view = 0; sscanf(name.c_str() + 3, "%f_%d", &secs, &view);
    set.traffic = getenv("NOTRF") == nullptr;
    int ai = g_world.findAirport("CAP"); const Airport& a = g_world.airports[ai];
    float h = a.heading * DEG, sn = sinf(h), cs = cosf(h), side = aptSide(a, ai);
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
  if (name.compare(0, 4, "gtun") == 0) {   // g-force lens at a forced strength (percent, held), chase view: gtun<pct>
    resAirborne = true; realTime = 20; launchResearch();
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    gTunnel = gTunnelPin = atof(name.c_str() + 4) / 100.f; toasts.clear(); return;
  }
  // loading-screen pictures (--loadshots): loadshot_<CODE> an airport from an elevated three-quarter view with the aircraft
  // on its runway; loadshot_air_<n> aircraft n in flight, filmed from alongside. Afternoon light, a little cloud.
  if (name.compare(0, 13, "loadshot_air_") == 0) {
    static const char* kSpot[kAircraftCount] = {"MDB", "PMB", "ORC", "KLO", "FJH", "CAP", "PVI", "VCF", "LHK", "SMP", "GLS", "NPT", "HFS", "ORC", "CAP"};   // a different place each
    int n = std::clamp(atoi(name.c_str() + 13), 0, kAircraftCount - 1);
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
  if (name.compare(0, 4, "gav_") == 0) {   // an aircraft parked on the runway, orbit view: gav_<spec>_<yaw>_<pitch>_<dist>[_<gear>[_<lift m>]]
    int sp = 0; float yawD = 120, pitD = 10, dist = 0, gearAt = -1, lift = 0;
    sscanf(name.c_str() + 4, "%d_%f_%f_%f_%f_%f", &sp, &yawD, &pitD, &dist, &gearAt, &lift);
    sp = std::clamp(sp, 0, kAircraftCount - 1);
    Contract c; c.from = g_world.findAirport("CAP"); c.to = g_world.findAirport("MDB"); c.title = "Aircraft check";
    c.wx = Weather(); c.wx.timeOfDay = getenv("TOD") ? (float)atof(getenv("TOD")) : 14.5f; c.wx.cloudCover = 0.2f; c.wx.visibility = 60000;
    realTime = 20;
    if (kAircraft[sp].special) { resCraft = sp; resAirport = c.from; resAirborne = false; resTime = c.wx.timeOfDay; resWx = 0; resCard = -1; launchResearch(); camMode = 0; }   // (the research craft as their flights show them; from outside: their flights start in the cockpit, and its cabin alone was drawn)
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
    if (const char* th = getenv("GAVTHR")) {   // (debug: the engines running at this throttle - reheat from 0.85 on the jets that have it)
      const float t = clampf((float)atof(th), 0.f, 1.f);
      botControl = true; plane.engineRunning = true; plane.ctl.throttle = t; plane.engineSpool = t; plane.ctl.brake = 1;
      plane.rpm = plane.spec->engineType == ENG_JET ? 24.f + 76.f * t : plane.spec->maxRpm * 0.9f; plane.n1 = 24.f + 76.f * t;
    }
    if (gearAt >= 0) {   // (the gear held part way: lifted clear of the runway for the few frames that follow)
      plane.gear = std::min(gearAt, 1.f); plane.ctl.gearDown = gearAt >= 0.5f; plane.pos.y += 2.f + lift; plane.vel = vec3();   // (lift: high enough to look up at its underside)
      dbgCamPos = plane.pos + off;
    }
    return;
  }
  if (name.compare(0, 5, "wcam_") == 0) {   // a free camera over the world: wcam_<x>_<z>_<height above ground>_<yaw>_<pitch>[_<hour>] (m, deg; yaw 0 north)
    float x = 0, z = 0, hgt = 300, yaw = 0, pitch = -30, tod = 11;
    sscanf(name.c_str() + 5, "%f_%f_%f_%f_%f_%f", &x, &z, &hgt, &yaw, &pitch, &tod);
    Contract c; c.from = 0; c.to = 1; c.title = "World";
    c.wx = Weather(); c.wx.timeOfDay = tod; c.wx.cloudCover = 0.15f; c.wx.visibility = 60000; c.wx.windSpeed = 4; c.wx.windFrom = 200;
    realTime = 20; startFlight(c, 0, Career::SRC_OWNED);
    for (int i = 0; i < 10; i++) { realTime += 1 / 30.f; update(1 / 30.f); }
    vec3 cam(x, std::max(g_world.height(x, z), 0.f) + hgt, z);
    vec3 dir(sinf(yaw * DEG) * cosf(pitch * DEG), sinf(pitch * DEG), -cosf(yaw * DEG) * cosf(pitch * DEG));
    dbgCam = true; dbgFollow = false; dbgCamPos = cam; dbgCamLook = cam + dir * 100.f;
    toasts.clear(); hint.clear(); uiHidden = true; return;
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
    airportView(ai, view, dbgCamPos, dbgCamLook);
    dbgCam = true; dbgFollow = false;
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
  if (name == "hud" && getenv("PADPROMPTS")) { in.pad = true; padActive = true; }   // (PADPROMPTS: the hints as a controller shows them)
  if (name == "hud") hint = contract.hints.size() ? expandHint(contract.hints[2], padPrompts(), true) : "";
  if (name == "gps" || name == "pause" || name == "minimap") {
    plane.reset(&kAircraft[1], vec3(-4000, 600, 9000), 40, kAircraft[1].maxFuel, 100, true, kAircraft[1].cruise);
    takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
    for (int i = 0; i < 60; i++) trail.push_back(vec2(-4000 - sinf(40 * DEG) * i * 110.f + sinf(i * 0.1f) * 300.f, 9000 + cosf(40 * DEG) * i * 110.f));
    if (name == "gps") { showMap = true; uiAnim[0x6e61u] = 1.f; }
    if (name == "pause") paused = true;
    if (name == "minimap") showMinimap = true;
    for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "at" && getenv("CAMPITCH")) { camPitch = (float)atof(getenv("CAMPITCH")) * DEG; camZoom = 2.5f; for (int i = 0; i < 30; i++) updateCamera(0.1f); }
  if (name == "at" && getenv("CAMYAW")) {   // CAMYAW=<deg>: the orbit camera that far round from dead astern (CAMZOOM: its distance)
    camMode = 2; camYaw = (float)atof(getenv("CAMYAW")) * DEG; camPitch = getenv("CAMPITCH") ? (float)atof(getenv("CAMPITCH")) * DEG : .12f;
    camZoom = getenv("CAMZOOM") ? (float)atof(getenv("CAMZOOM")) : .5f; for (int i = 0; i < 30; i++) updateCamera(0.1f);
  }
  if (name == "seamgps") { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = 25000.f; }
  if (name == "longhaul" || name == "longhaulgps") {   // a long haul under way: an ATP's Emissary 20 km out of Port Verde
    int jet = -1; for (int t : kCareerAircraft) if (!strcmp(kAircraft[t].id, "emissary")) jet = t;
    career = Career{}; career.newGame(); career.license = LIC_ATP; career.money = 400000; career.location = g_world.findAirport("PVI");
    career.fleet = {{jet, career.location, kAircraft[jet].maxFuel, 1.f}}; career.refreshBoard();
    for (const Contract& c : career.board) if (c.longHaul() && career.canFly(c, jet) == Career::SRC_OWNED) { botControl = false; beginCareerFlight(c, jet, Career::SRC_OWNED); break; }
    if (haul.on) {
      const Airport& a = g_world.airports[contract.from];
      vec3 to(dest().x - a.x, 0, dest().z - a.z); to = to * (1.f / std::max(length(to), 1.f));
      plane.reset(plane.spec, a.pos() + to * 20000.f + vec3(0, 3000, 0), atan2f(to.x, -to.z) / DEG, plane.spec->maxFuel * 0.8f, 100, true, plane.spec->cruise);
      settleAirborneStart(); takeoffAnnounced = true; camQ = plane.q; hint.clear(); toasts.clear();
      if (name == "longhaulgps") { showMap = true; uiAnim[0x6e61u] = 1.f; gpsRangeTarget = gpsRange = 120000.f; }
      for (int i = 0; i < 30; i++) updateCamera(0.1f);
      printf("Long haul scene: %s, %.0f km to %s %s\n", contract.title.c_str(), haul.job.haulKm, dest().code, dest().name);
    }
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

// Military sorties are career-backed loaners; research practice uses the same combat engine off the books.
void Game::launchMilitary(int kind) {
  if (launchCardPending() || !military::mission(kind) || !career.atHome() || g_world.procedural) return;   // (Defense Command is the islands')
  Contract c = career.militaryContract(kind);
  if (career.canFly(c, c.forceAircraft) != Career::SRC_MILITARY) return;
  if (commitBlocked()) { hubMsg = "Your last result isn't saved yet: retry the save first."; hubMsgTime = 5; return; }
  if (career.attemptOpen || career.military.activeAttempt) return;
  // Headless simulations deliberately bypass presentation. Interactive launches defer even the save:
  // slow storage, encounter setup and the first renderer bake must all happen after a visible card.
  if (headless) { beginMilitaryLaunch(c); return; }
  pendingMilitaryLaunch = c;
  militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadFrames = 0; loadViewsDrawn = false; loadViewsStep = 0; loadWarmStep = 0; loadWarmAt = -1;
  loadStableFrames = 0; loadObservedFrame = -1; loadMap = false;
  screen = SCR_LOADING;
  in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false;
  in.buttonsPressed &= ~(PAD_A | PAD_START);
}

void Game::loadLogBegin() {
  if (headless || saveDir.empty()) return;
  loadLog.clear(); loadLogging = true; loadShownInside = 0; loadLogStart = loadLogLast = std::chrono::steady_clock::now();
}
void Game::loadLogTick() {
  const auto now = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(now - loadLogLast).count();
  loadLogLast = now;
  if (screen == SCR_LOADING && loadLog.size() < 4000) {
    loadLog.push_back({ms, launchCardPending(), launchSetUpShown, loadFrames, std::max(0, g_ren.entPending), loadViewsStep, loadingShadowPending(), loadWarmStep, loadShownInside});
    loadShownInside = 0;
    return;
  }
  loadLogging = false;
  if (!loadLog.empty()) loadLog.push_back({ms, false, false, loadFrames, std::max(0, g_ren.entPending), loadViewsStep, false, loadWarmStep, loadShownInside});   // (the last frame of it)
  loadShownInside = 0;
  FILE* f = fopen((saveDir + "/loading.log").c_str(), "w");
  if (!f) return;
  const double total = std::chrono::duration<double>(now - loadLogStart).count();
  fprintf(f, "Somnium Volandi - the loading screen of the last launch: %s, %s\n%.2f s from the card to %s, %zu frames\n\n",
          contract.title.c_str(), plane.spec ? plane.spec->name : "-", total, screen == SCR_FLIGHT ? "the flight" : "leaving it", loadLog.size());
  fprintf(f, "frame       ms  what was on the screen\n");
  for (size_t i = 0; i < loadLog.size(); i++) {
    const LoadLogFrame& L = loadLog[i];
    if (L.card) fprintf(f, "%5zu %8.1f  the card (the flight not set up yet)\n", i, L.ms);
    else fprintf(f, "%5zu %8.1f  %s: scene frame %d, warmed %d of %d, %d scenery chunks to make, views drawn %d of 2%s%s\n", i, L.ms, L.setUp ? "set up, the loading screen alone" : i + 1 == loadLog.size() ? "the last" : "loading",
                 L.frames, std::min(L.warm, loadWarmItems()), loadWarmItems(), L.pending, std::min(L.views, 2), L.shadows ? ", terrain shadows to do" : "",
                 L.shown ? fmt(", shown %d times from inside a bake", L.shown).c_str() : "");
  }
  fclose(f);
}

bool Game::queueLaunch(const Contract& card, int spec, LaunchKind kind, std::function<void()> begin) {
  if (headless || launchRunning) return false;
  if (launchCardPending()) return true;   // (one launch at a time: a second press while the card is up does nothing)
  loadLogBegin();
  PendingLaunch L; L.card = card; L.spec = spec; L.kind = kind; L.from = screen; L.paused = paused; L.begin = std::move(begin);
  pendingLaunch = std::move(L);
  launchFrameDrawn = launchFramePresented = false;
  loadT = 0; loadReadyT = -1; loadShown = 0; loadPend0 = 0; loadFrames = 0; loadViewsDrawn = false; loadViewsStep = 0; loadWarmStep = 0; loadWarmAt = -1;
  loadStableFrames = 0; loadObservedFrame = -1; loadMap = false; launchSetUpShown = false;
  screen = SCR_LOADING;
  in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false;
  in.buttonsPressed &= ~(PAD_A | PAD_START);
  return true;
}
void Game::cancelQueuedLaunch() {
  if (!pendingLaunch) return;
  screen = pendingLaunch->from; paused = pendingLaunch->paused;
  pendingLaunch.reset();
  launchFrameDrawn = launchFramePresented = false;
}

bool Game::beginMilitaryLaunch(const Contract& c) {
  if (career.canFly(c, c.forceAircraft) != Career::SRC_MILITARY) return false;
  bool begun=false;
  if (!commitLaunch([&](Career& k) { begun=k.beginMilitary(c); }) || !begun) return false;
  const auto p = career.plan(c, c.forceAircraft, Career::SRC_MILITARY);
  startFlight(c, c.forceAircraft, Career::SRC_MILITARY, &p);
  return militaryFlight && (screen == SCR_LOADING || screen == SCR_FLIGHT);
}

hive::WorldCallbacks Game::hiveWorld() {
  hive::WorldCallbacks w;
  w.terrainHeight = [](void*, float x, float z) { return std::max(0.f, pitGround(x,z,g_world.height(x,z))); };
  w.sweepFraction = [](void*, const vec3& a, const vec3& b, float radius) { return hiveWorldSweep(a,b,radius); };
  w.sweepIncludesTerrain = true;
  return w;
}

void Game::setupMilitaryEncounter() {
  hiveCombat.reset(career.military.missionSeed, combatWrap());
  militaryHull = 100; militarySettled = false;
  const auto* mission = military::missionForContract(contract.type);
  if (!mission) return; // Career launch validates the catalogue before committing the attempt.
  for(auto& label:militarySiteLabels) label.clear();
  combatLoadout.reset(mission->forward, mission->bomb);
  // Separate insertion and objective areas. Mission positions remain canonical over world seams.
  vec3 origin=plane.pos; origin.y=std::max(origin.y,g_world.height(origin.x,origin.z)+650.f);
  if(mission->id==military::HighAltitudeIntel) origin.y=4500.f;
  camPos+=origin-plane.pos; // Keep the flight camera with the supplied insertion altitude.
  plane.pos=origin;
  hive::MissionConfig m; m.extraction=origin; m.extractionRadius=300;
  vec3 f=plane.forward(); f.y=0; f=normalize(f+vec3(0,0,.0001f));
  vec3 right=cross(f,vec3(0,1,0));
  auto point=[&](float distance,float side,float altitude) { vec3 p=origin+f*distance+right*side; p.x=g_world.wrap(p.x); p.z=g_world.wrap(p.z); p.y=std::max(altitude,g_world.height(p.x,p.z)+250.f); return p; };
  // Select the exact same authored airport entities the scenery generator renders and collides with.
  // Sensor aim points sit just above each roof, never inside a building or at an arbitrary airborne marker.
  auto site=[&](int airport,int preferred,int index) {
    std::vector<AptItem> items; airportItems(airport,items);
    const AptItem* picked=nullptr;
    const int priorities[]={preferred,EK_CTRL_TOWER,EK_FBO,EK_ARCH_HANGAR,EK_HANGAR,EK_T_HANGAR};
    for(int kind:priorities) {
      for(const auto& item:items) if(item.kind==kind) {
        const auto& e=item.e; const auto& info=kEntInfo[item.kind];
        const vec3 roof(e.x,e.y+info.h*e.sy+2.f,e.z);
        bool used=false;
        for(int j=0;j<index;++j) used|=length(hiveCombat.displacement(roof,m.objectives[j].position))<1.f;
        if(!used) { picked=&item;break; }
      }
      if(picked) break;
    }
    hive::Objective o; o.id=100+index;
    if(picked) {
      const auto& e=picked->e; const auto& info=kEntInfo[picked->kind];
      o.position=vec3(e.x,e.y+info.h*e.sy+2.f,e.z);
      o.radius=clampf(std::max(info.hx*e.sx,info.hz*e.sz),8.f,35.f);
      militarySiteLabels[index]=std::string(g_world.airports[airport].code)+" "+info.name;
    } else {
      // Every authored airport has a hangar; keep a safe airport navigation point for custom worlds.
      const auto& a=g_world.airports[airport]; o.position=a.pos()+vec3(0,3,0);o.radius=10;
      militarySiteLabels[index]=std::string(a.code)+" operations site";
    }
    return o;
  };
  contract.brief=mission->brief;
  if (mission->objective==hive::MissionKind::Recon) {
    m.kind=hive::MissionKind::Recon; m.scanRange=mission->scanRange; m.scanSeconds=mission->scanSeconds;
    const bool local=mission->id==military::LocalSurvey || mission->id==military::HoverScan;
    const int wanted=mission->id==military::HoverScan?1:(mission->id==military::LocalSurvey || mission->id==military::HighAltitudeIntel)?2:3;
    m.objectiveCount=local?wanted:std::min(wanted,(int)g_world.airports.size());
    std::vector<int> airports; for(int i=0;i<(int)g_world.airports.size();++i) airports.push_back(i);
    std::stable_sort(airports.begin(),airports.end(),[&](int a,int b) { return length(hiveCombat.displacement(origin,g_world.airports[a].pos()))<length(hiveCombat.displacement(origin,g_world.airports[b].pos())); });
    const int preferred[]={EK_CTRL_TOWER,EK_HANGAR,EK_FUEL_TANK};
    for(int i=0;i<m.objectiveCount;++i) {
      m.objectives[i]=site(local?contract.from:airports[i],preferred[i],i);
      contract.brief+=" "+militarySiteLabels[i]+".";
    }
    // Sensors are the objective: unarmed XR sorties do not spawn unavoidable pursuing enemies.
    // The precision hover sortie carries defensive stores, but no combat is needed to complete it.
  } else if (mission->objective==hive::MissionKind::Defense) {
    const bool heavy=mission->id==military::HeavyDefense;
    m.kind=hive::MissionKind::Defense; m.objectiveCount=1;
    m.objectives[0]=site(contract.from,heavy?EK_HANGAR:EK_CTRL_TOWER,0); m.objectives[0].health=600;
    contract.brief+=" Protected site: "+militarySiteLabels[0]+".";
    m.waveCount=heavy?4:3;
    for(int i=0;i<m.waveCount;++i) {
      auto& w=m.waves[i]; w.at=i*(heavy?32.f:28.f);
      w.position=m.objectives[0].position+f*(2600.f+i*150.f)+right*((i-1)*500.f);
      w.position.x=g_world.wrap(w.position.x); w.position.z=g_world.wrap(w.position.z);
      w.position.y=std::max(m.objectives[0].position.y+650.f,g_world.height(w.position.x,w.position.z)+300.f);
      // Four bomber-led waves, bounded to the supplied 48 heavy rounds and six Plasma stores.
      // Solo bomber, fighter escort, relay escort, fighter escort: no stacked blast damage is required.
      w.count=heavy?(i==0?1:2):(i==2?3:2);
      w.types[0]=heavy?hive::Type::Bastion:hive::Type::Needle;
      w.types[1]=heavy?(i==2?hive::Type::Cantor:hive::Type::Needle):hive::Type::Bastion;
      w.types[2]=hive::Type::Cantor;
    }
  } else {
    m.kind=hive::MissionKind::Strike;
    if(mission->id==military::Interception) {
      m.targetCount=3;
      for(int i=0;i<3;++i) m.targetIds[i]=hiveCombat.spawn(hive::Type::Needle,point(2600.f+i*550.f,(i-1)*750.f,origin.y+float(i)*140.f));
    } else if(mission->id==military::RelaySuppression) {
      m.targetCount=3;
      for(int i=0;i<3;++i) m.targetIds[i]=hiveCombat.spawn(hive::Type::Cantor,point(4000.f+i*200.f,(i-1)*550.f,origin.y+120.f));
      for(int i=0;i<2;++i) hiveCombat.spawn(hive::Type::Bastion,point(3400.f,(i?1.f:-1.f)*650.f,origin.y));
    } else {
      m.targetCount=2;
      m.targetIds[0]=hiveCombat.spawn(hive::Type::Archon,point(4300,0,origin.y+150));
      m.targetIds[1]=hiveCombat.spawn(hive::Type::Cantor,point(4000,500,origin.y+200));
      hiveCombat.spawn(hive::Type::Needle,point(3500,-450,origin.y));
    }
  }
  hiveCombat.startMission(m);
  syncCombatPayload();
  toast("MILITARY LOANER / return to extraction after objectives",vec3(.35f,.85f,1));
  if(m.objectiveCount>0) toast((m.kind==hive::MissionKind::Recon?"FIRST SCAN: ":"PROTECT: ")+militarySiteLabels[0],vec3(.35f,.85f,1));
}
void Game::clearPracticeEncounter() {
  if (!researchFlight || resCard>=0 || specIdx!=kSomnus) return;
  hiveCombat.reset(1, combatWrap()); militaryHull=100;
  combatCollisionWarm=false; combatCollisionWarmMs=0; combatPrefetchCursor=0;
  // A range reset clears ordnance and damage together; it cannot touch the career.
  somnus=SomnusState(); g_scenery.resetDamage();
  combatLoadout.reset(practiceForward,practiceBomb);
  syncCombatPayload();
}
void Game::spawnPracticeWave() {
  if (!researchFlight || resCard>=0 || specIdx!=kSomnus || crashed) return;
  hive::MissionConfig m; m.kind=hive::MissionKind::Practice; hiveCombat.startMission(m);
  for(int i=0;i<4 && hiveCombat.aliveCount()<hive::MaxActors;++i) {
    vec3 p=plane.pos+plane.forward()*(1700.f+i*240.f)+plane.right()*((i-1.5f)*260.f);
    p.y=std::max(p.y,g_world.height(p.x,p.z)+300.f);
    hiveCombat.spawn((hive::Type)((practiceWave+i)%4),p);
  }
  // The range control is an explicit setup action; prepare its new corridors before resuming combat.
  combatCollisionWarm=false; warmCombatCollision();
  toast("Practice wave spawned (8 craft maximum)",vec3(.7f,.6f,1));
}
std::string Game::hiveObjectiveText() const {
  const auto& m=hiveCombat.mission;
  if (m.status==hive::MissionStatus::Extract) return fmt("RETURN TO EXTRACTION  %.0f m",length(hiveCombat.displacement(plane.pos,m.config.extraction)));
  if(m.config.kind==hive::MissionKind::Recon) {
    int n=0; float scan=0; float nearest=1e9f; int target=0;
    for(int i=0;i<m.config.objectiveCount;++i) { const auto& o=m.config.objectives[i]; n+=o.scanned; if(!o.scanned) { float d=length(hiveCombat.displacement(plane.pos,o.position)); if(d<nearest) { nearest=d; target=i+1; scan=o.scan; } } }
    const auto* spec=military::missionForContract(contract.type);
    // Mandatory sensor conditions lead the HUD line: long site names are ellipsized by the flight UI.
    if(spec && spec->id==military::HighAltitudeIntel) {
      if(plane.pos.y<spec->minAltitude) return "SCAN PAUSED / CLIMB TO 3500 m MSL";
      if(plane.pos.y>spec->maxAltitude) return "SCAN PAUSED / DESCEND BELOW 6500 m MSL";
      const float speed=sqrtf(plane.vel.x*plane.vel.x+plane.vel.z*plane.vel.z);
      if(speed<spec->minSpeed) return fmt("SCAN PAUSED / SPEED %.0f kt / NEED 350+",speed*MS_TO_KT);
    }
    if(spec && spec->id==military::HoverScan) {
      const float speed=length(plane.vel), above=plane.pos.y-m.config.objectives[0].position.y;
      if(speed>spec->maxSpeed) return fmt("SCAN PAUSED / SLOW FROM %.0f TO 23 kt",speed*MS_TO_KT);
      if(above<spec->minAltitude) return "SCAN PAUSED / CLIMB 30-120 m ABOVE ROOF";
      if(above>spec->maxAltitude) return "SCAN PAUSED / DESCEND 30-120 m ABOVE ROOF";
    }
    return fmt("SCAN %d/%d %.0f%% / %s %.0f m",n,m.config.objectiveCount,100*scan/std::max(.1f,m.config.scanSeconds),target>0?militarySiteLabels[target-1].c_str():"EXTRACTION",nearest);
  }
  if(m.config.kind==hive::MissionKind::Defense) return fmt("DEFEND %s %.0f%% / WAVE %d/%d / HOSTILES %d",militarySiteLabels[0].c_str(),100*m.config.objectives[0].health/600.f,m.nextWave,m.config.waveCount,hiveCombat.aliveCount());
  if(m.config.kind==hive::MissionKind::Strike) {
    int destroyed=0; for(int i=0;i<m.config.targetCount;++i) destroyed+=m.targetsDestroyed[i];
    const auto* spec=military::missionForContract(contract.type);
    const char* role=spec && spec->id==military::Interception?"INTERCEPT NEEDLES":spec && spec->id==military::RelaySuppression?"STRIKE CANTOR RELAYS":"STRIKE HIVE COMMAND";
    return fmt("%s / TARGETS %d/%d / HOSTILES %d / HULL %.0f%%",role,destroyed,m.config.targetCount,hiveCombat.aliveCount(),militaryHull);
  }
  return fmt("PRACTICE / HOSTILES %d / HULL %.0f%% / PAUSE FOR RANGE CONTROLS",hiveCombat.aliveCount(),militaryHull);
}
void Game::syncCombatPayload() {
  if(specIdx!=kSomnus || !(militaryFlight || (researchFlight && resCard<0))) return;
  // A fixed non-equipment baseline prevents repeated swaps/rearms from accumulating mass.
  plane.payload=std::max(0.f,combatBasePayload)+combatLoadout.massKg();
}
void Game::warmCombatCollision() {
  if(combatCollisionWarm || !(militaryFlight || (researchFlight && resCard<0 && specIdx==kSomnus))) return;
  const auto start=std::chrono::steady_clock::now();
  // Query the production collision path without stepping Combat or touching mission clocks.
  // Radius and nearest-image coordinates match later actor/weapon queries.
  auto corridor=[&](vec3 a,vec3 b,float radius) { hiveWorldSweep(a,a+hiveCombat.displacement(a,b),radius); };
  corridor(plane.pos,plane.pos+plane.forward()*500.f,12.f);
  const auto& m=hiveCombat.mission.config;
  for(const auto& a:hiveCombat.actors) if(a.alive) {
    corridor(a.position,plane.pos,0);
    corridor(a.position,a.position+a.velocity*2.f,hive::roleSpec(a.type).radius);
  }
  for(int i=0;i<m.objectiveCount;++i) {
    const vec3 p=m.objectives[i].position;
    corridor(p+vec3(0,1500,0),p,1.f);
    corridor(p+vec3(-500,350,0),p+vec3(500,350,0),20.f);
  }
  for(int i=0;i<m.waveCount;++i) {
    const auto& w=m.waves[i];
    const vec3 target=m.objectiveCount?m.objectives[0].position:plane.pos;
    corridor(w.position,target,0);
    corridor(w.position,plane.pos,0);
  }
  combatCollisionWarmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  combatCollisionWarm=true;
}
void Game::prefetchCombatCollision() {
  // Use the supported worker queue (the renderer's main-thread pump installs results).
  // Queue at most ONE missing level-2 chunk per update, or generate one when no workers exist.
  // A cache miss in an authoritative sweep still generates synchronously.
  constexpr int slots=hive::MaxActors+1, neighborhoods=9, total=slots*neighborhoods;
  for(int n=0;n<total;++n) {
    const int item=combatPrefetchCursor++%total, slot=item%slots, offset=item/slots;
    vec3 p;
    if(slot==hive::MaxActors) p=plane.pos+plane.vel*2.f;
    else { const auto& a=hiveCombat.actors[slot]; if(!a.alive) continue; p=a.position+a.velocity*2.f; }
    p.x=g_world.wrap(p.x+(offset%3-1)*Scenery::CH); p.z=g_world.wrap(p.z+(offset/3-1)*Scenery::CH);
    const int cx=Scenery::chunkOf(p.x),cz=Scenery::chunkOf(p.z);
    const auto* chunk=g_scenery.get(cx,cz);
    if(!chunk || chunk->level<2) {
      if(g_scenery.workers()>0) g_scenery.request(cx,cz,2);
      else g_scenery.ensure(cx,cz,2);
      break;
    }
  }
  combatPrefetchCursor%=total;
}
void Game::updateHive(float dt) {
  if(!militaryFlight && !(researchFlight && resCard<0 && specIdx==kSomnus)) return;
  if(dt>0 && !paused) prefetchCombatCollision();
  hive::PlayerSnapshot p; p.position=plane.pos; p.velocity=plane.vel; p.forward=plane.forward(); p.alive=!crashed && !plane.ev.crashed; p.scanning=false;
  if(militaryFlight) {
    const auto* mission=military::missionForContract(contract.type);
    const float roof=hiveCombat.mission.config.objectiveCount?hiveCombat.mission.config.objectives[0].position.y:0.f;
    const float speed=mission && mission->id==military::HighAltitudeIntel?sqrtf(plane.vel.x*plane.vel.x+plane.vel.z*plane.vel.z):length(plane.vel);
    p.scanning=mission && military::sensorEnvelope(*mission,plane.pos.y,speed,roof);
  }
  p.up=plane.up();
  auto box=[&](vec3 center,vec3 halfExtent) {
    if(p.bodyBoxCount<hive::MaxPlayerBoxes) p.bodyBoxes[p.bodyBoxCount++]={center,halfExtent};
  };
  if(specIdx==kSomnus) {
    // Coarse local collision volumes follow wrSection, diamond-wing panels and the four articulated pods.
    // Unlike the old 12 m sphere, empty air above/below the thin wings is not a hit.
    box(vec3(0,-.10f,-6.3f),vec3(.65f,.38f,2.1f));
    box(vec3(0,-.06f,.05f),vec3(1.31f,.60f,4.45f));
    box(vec3(0,-.12f,6.15f),vec3(1.10f,.38f,1.65f));
    for(int side:{-1,1}) {
      box(vec3(side*2.15f,-.24f,.1f),vec3(.95f,.14f,3.3f));
      box(vec3(side*4.65f,-.24f,2.0f),vec3(1.55f,.12f,1.3f));
    }
    for(int i=0;i<4;++i) {
      const float tilt=plane.podTilt[i];
      box(kSomnusPods[i],vec3(.55f,.55f*std::fabs(cosf(tilt))+1.52f*std::fabs(sinf(tilt)),1.52f*std::fabs(cosf(tilt))+.55f*std::fabs(sinf(tilt))));
    }
    box(vec3(0,1.0f,6.1f),vec3(3.0f,1.0f,1.1f));
  } else if(validAircraft(specIdx)) {
    const auto& m=kModels[specIdx];
    for(int i=0;i<7;++i) {
      const float* a=m.st[i]; const float* b=m.st[i+1];
      box(vec3(0,(a[3]+b[3])*.5f,(a[0]+b[0])*.5f),vec3(std::max(a[1],b[1]),std::max(a[2],b[2])+std::fabs(a[3]-b[3])*.5f,std::fabs(b[0]-a[0])*.5f));
    }
    auto surface=[&](const float* wing) {
      const float chord=(wing[1]+wing[2])*.5f;
      for(int side:{-1,1}) box(vec3(side*wing[0]*.5f,wing[4]+sinf(wing[6]*DEG)*wing[0]*.5f,wing[5]+wing[3]*.5f+chord*.5f),vec3(wing[0]*.5f,.12f,chord*.5f));
    };
    surface(m.wing); surface(m.ht);
    box(vec3(0,m.vt[4]+m.vt[0]*.5f,m.vt[5]+m.vt[1]*.5f),vec3(.12f,m.vt[0]*.5f,m.vt[1]*.5f));
  }
  hiveCombat.step(dt,p,hiveWorld(),paused);
  for(int i=0;i<hiveCombat.eventCount;++i) {
    const auto& e=hiveCombat.events[i];
    vec3 at=plane.pos+hiveCombat.displacement(plane.pos,e.position);
    if(e.type==hive::EventType::PlayerDamage && !crashed) {
      militaryHull=std::max(0.f,militaryHull-e.value);
      plane.w+=vec3(.015f,.009f,.02f)*std::min(e.value,30.f);
      g_audio.triggerAt(SFX_IMPACT,at,clampf(e.value*.025f,.12f,.65f));
      if(militaryHull<=0) { plane.ev.crashed=true; plane.ev.crashReason="Hull lost to Hive fire"; }
    }
    if(e.type==hive::EventType::Death || e.type==hive::EventType::Hit || e.type==hive::EventType::Fire) {
      int n=e.type==hive::EventType::Death?28:e.type==hive::EventType::Hit?8:3;
      for(int j=0;j<n;++j) spawn(at,vec3(sparkRng.range(-15,15),sparkRng.range(-4,18),sparkRng.range(-15,15)),.25f+sparkRng.uni()*.5f,.2f,.3f,vec3(1,.55f,.24f)*2,1,SPR_SPARK,.6f,-1);
      if(e.type==hive::EventType::Death) { ++somnus.kills; g_audio.triggerAt(SFX_CRASH,at,.45f); for(int j=0;j<8;++j) spawn(at,vec3(0,5+j,0),2.f,3.f,4.f,vec3(.12f),.6f,SPR_SMOKE,1,0); }
    }
    if(e.type==hive::EventType::Fire) {
      const auto* actor = hiveCombat.find(e.source);
      g_audio.triggerAt(SFX_CANNON,at,.32f,actor?actor->velocity:vec3());
    }
    if(e.type==hive::EventType::Hit) g_audio.triggerAt(SFX_METAL,at,.3f);
    if(e.type==hive::EventType::Telegraph && e.target==0 && realTime-audioThreatAt>9.f) {
      audioThreatAt=realTime; toast("INCOMING FIRE - BREAK AWAY",vec3(1,.45f,.2f));
    }
    if(e.type==hive::EventType::ObjectiveDamage && realTime-audioObjectiveAt>12.f) {
      audioObjectiveAt=realTime; toast("PROTECTED SITE UNDER ATTACK",vec3(1,.55f,.2f));
    }
    if(e.type==hive::EventType::Wave && e.source==0) toast("HOSTILE WAVE INBOUND",vec3(1,.55f,.2f));
    if(e.type==hive::EventType::ScanComplete) toast("Intelligence site recorded",vec3(.3f,1,.7f));
  }
  hiveCombat.clearEvents();
  if (hiveCombat.mission.status == hive::MissionStatus::Active) audioExtractSaid = false;
  if (hiveCombat.mission.status == hive::MissionStatus::Extract && !audioExtractSaid) {
    audioExtractSaid = true; toast("OBJECTIVES COMPLETE - RETURN TO EXTRACTION",vec3(.3f,1,.7f));
  }
  if(militaryFlight && !militarySettled && !crashed && !plane.ev.crashed) {
    if(hiveCombat.mission.status==hive::MissionStatus::Success) endFlight(true,"Military objectives secured");
    else if(hiveCombat.mission.status==hive::MissionStatus::Failed) endFlight(false,"Military objective lost",OUT_ABANDONED);
  }
}
