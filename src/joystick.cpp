// Joysticks, throttles and pedals: the portable half (joystick.h). The devices themselves: joystick_win.cpp
#include "joystick.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

const FlightAxisInfo kFlightAxes[AX_COUNT] = {
  {"pitch", "Pitch (elevator)", false},
  {"roll", "Roll (ailerons)", false},
  {"yaw", "Yaw (rudder)", false},
  {"throttle", "Throttle", true},
  {"brake", "Wheel brakes", true},
};

int joyFind(const std::vector<JoyState>& joys, const std::string& id, const std::string& name) {
  for (size_t i = 0; i < joys.size(); i++) if (!id.empty() && joys[i].id == id) return (int)i;
  for (size_t i = 0; i < joys.size(); i++) if (!name.empty() && joys[i].name == name) return (int)i;
  return -1;
}

float shapeAxis(float raw, const AxisBind& b, bool unipolar) {
  if (!std::isfinite(raw)) return 0.f;
  float v = std::clamp(b.invert ? -raw : raw, -1.f, 1.f);
  const float dead = std::clamp(b.dead, 0.f, .45f), curve = std::clamp(b.curve, 0.f, 1.f);
  if (unipolar) {   // a lever: its whole travel is 0..1, with a dead zone at each end so idle and full are reached
    float u = (v + 1.f) * .5f;
    u = std::clamp((u - dead) / (1.f - 2.f * dead), 0.f, 1.f);
    return (1.f - curve) * u + curve * u * u * u;
  }
  const float a = std::fabs(v);   // a stick: a dead zone about the centre, the rest rescaled so full travel is still 1
  if (a <= dead) return 0.f;
  const float x = (a - dead) / (1.f - dead);
  return std::copysign((1.f - curve) * x + curve * x * x * x, v);
}

const char* joyAxisName(int axis) {
  static const char* n[kJoyAxes] = {"X", "Y", "Z", "RX", "RY", "RZ", "SLIDER 1", "SLIDER 2"};
  return axis >= 0 && axis < kJoyAxes ? n[axis] : "--";
}

// Lines: "axis <id> <axis> <invert> <dead> <curve> <device id> <device name...>" and
// "button <action id> <button> <device id> <device name...>" (the name runs to the end of the line)
std::string joyConfigText(const AxisBind (&axes)[AX_COUNT], const JoyButtonBind* buttons, const char* const* actionIds, int actions) {
  std::string t = "# Somnium Volandi joystick bindings (Settings -> Joystick and Controls)\n";
  char b[160];
  for (int i = 0; i < AX_COUNT; i++) {
    const AxisBind& a = axes[i];
    snprintf(b, sizeof b, "axis %s %d %d %.3f %.3f ", kFlightAxes[i].id, a.axis, a.invert ? 1 : 0, a.dead, a.curve);
    t += b + (a.dev.empty() ? std::string("-") : a.dev) + " " + a.devName + "\n";
  }
  for (int i = 0; i < actions; i++) {
    if (!buttons[i].bound()) continue;
    snprintf(b, sizeof b, "button %s %d ", actionIds[i], buttons[i].button);
    t += b + (buttons[i].dev.empty() ? std::string("-") : buttons[i].dev) + " " + buttons[i].devName + "\n";
  }
  return t;
}

void joyConfigParse(const std::string& text, AxisBind (&axes)[AX_COUNT], JoyButtonBind* buttons, const char* const* actionIds, int actions) {
  std::istringstream in(text);
  std::string line;
  auto rest = [](std::istringstream& ls) { std::string r; std::getline(ls, r); size_t i = r.find_first_not_of(' '); return i == std::string::npos ? std::string() : r.substr(i); };
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    std::istringstream ls(line);
    std::string kind, id, dev;
    if (!(ls >> kind >> id)) continue;
    if (kind == "axis") {
      int ax = -1, inv = 0; float dead = .05f, curve = 0.f;
      if (!(ls >> ax >> inv >> dead >> curve >> dev)) continue;
      for (int i = 0; i < AX_COUNT; i++) if (id == kFlightAxes[i].id) {
        AxisBind& a = axes[i];
        a.axis = ax >= 0 && ax < kJoyAxes ? ax : -1; a.invert = inv != 0;
        a.dead = std::isfinite(dead) ? std::clamp(dead, 0.f, .45f) : .05f; a.curve = std::isfinite(curve) ? std::clamp(curve, 0.f, 1.f) : 0.f;
        a.dev = dev == "-" ? std::string() : dev; a.devName = rest(ls);
      }
    } else if (kind == "button") {
      int bt = -1;
      if (!(ls >> bt >> dev) || bt < 0 || bt >= kJoyButtons) continue;
      for (int i = 0; i < actions; i++) if (id == actionIds[i]) { buttons[i].button = bt; buttons[i].dev = dev == "-" ? std::string() : dev; buttons[i].devName = rest(ls); }
    }
  }
}
