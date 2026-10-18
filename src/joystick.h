#pragma once
// Joysticks, throttles and pedals. The platform fills Input::joys each frame (joystick_win.cpp: DirectInput, XInput
// gamepads left to the gamepad path); the player binds the flight axes and any action's button to them (Settings ->
// Joystick, Settings -> Controls), and each axis has its own direction, dead zone and response curve. The bindings
// live in joystick.cfg beside settings.cfg: device names don't fit settings.cfg's "name number" lines.
#include <string>
#include <vector>

constexpr int kJoyAxes = 8, kJoyButtons = 128;

struct JoyState {
  std::string id;     // the device's DirectInput instance GUID: the same device on this PC keeps it
  std::string name;   // its product name ("T.16000M", "TWCS Throttle")
  int axes = 0, buttons = 0, hats = 0;   // how many it has; hat h's directions are buttons 120 + 4h on (up, right, down, left)
  bool hasAxis[kJoyAxes] = {};        // which of X, Y, Z, RX, RY, RZ, slider 1, slider 2 it reports
  float axis[kJoyAxes] = {};          // -1..1
  bool button[kJoyButtons] = {}, pressed[kJoyButtons] = {};   // held; went down this frame
};

enum FlightAxis { AX_PITCH, AX_ROLL, AX_YAW, AX_THROTTLE, AX_BRAKE, AX_COUNT };
struct FlightAxisInfo { const char* id; const char* name; bool unipolar; };   // unipolar: 0..1 (a lever), else -1..1
extern const FlightAxisInfo kFlightAxes[AX_COUNT];

struct AxisBind {
  std::string dev, devName;   // the device (its id; its name, to find it again on another USB port)
  int axis = -1;              // which of its axes; -1 unbound
  bool invert = false;
  float dead = .05f;          // the dead zone: the centre's for a stick, each end's for a lever (fraction of travel)
  float curve = 0.f;          // 0 linear .. 1 the most gentle about the centre (a stick) or the bottom (a lever)
  bool bound() const { return axis >= 0; }
};
struct JoyButtonBind { std::string dev, devName; int button = -1; bool bound() const { return button >= 0; } };

// The device a binding names: its own id first, else the first with the same product name, else -1
int joyFind(const std::vector<JoyState>& joys, const std::string& id, const std::string& name);
// A raw -1..1 reading through the binding: direction, dead zone, curve. A stick axis gives -1..1, a lever 0..1
float shapeAxis(float raw, const AxisBind& b, bool unipolar);
// "X", "Y", "Z", "RX", "RY", "RZ", "SLIDER 1", "SLIDER 2"
const char* joyAxisName(int axis);

// joystick.cfg: what it holds and reading it back (actionIds[i] names action i's button binding). Unknown lines and
// unknown ids are skipped, so a file from a later version still loads
std::string joyConfigText(const AxisBind (&axes)[AX_COUNT], const JoyButtonBind* buttons, const char* const* actionIds, int actions);
void joyConfigParse(const std::string& text, AxisBind (&axes)[AX_COUNT], JoyButtonBind* buttons, const char* const* actionIds, int actions);

#ifdef _WIN32
bool joyInit(void* hwnd);                 // DirectInput; false: none (the game runs without joysticks)
void joyDevicesChanged();                 // WM_DEVICECHANGE: find the devices again on the next poll
void joyPoll(std::vector<JoyState>& out); // this frame's state of every device
#endif
