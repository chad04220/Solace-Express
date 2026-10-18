// Joysticks, throttles and pedals on Windows (joystick.h): DirectInput 8 game controllers, read every frame into
// Input::joys. XInput gamepads (an "IG_" in the device path) stay with the gamepad path, so a pad is never read twice.
// The devices are found again whenever Windows reports a device change (joyDevicesChanged, from WM_DEVICECHANGE).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include "joystick.h"

namespace {
struct Device { IDirectInputDevice8W* d = nullptr; JoyState st; };
IDirectInput8W* s_di = nullptr;
HWND s_hwnd = nullptr;
std::vector<Device> s_devs;
bool s_rescan = true;

std::string narrow(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 1 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
std::string guidText(const GUID& g) {
  char b[40];
  snprintf(b, sizeof b, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", (unsigned long)g.Data1, g.Data2, g.Data3,
           g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
  return b;
}

// which of DIJOYSTATE2's eight axes the device reports (its sliders in order: the first is slider 1)
BOOL CALLBACK onAxis(LPCDIDEVICEOBJECTINSTANCEW o, void* ctx) {
  JoyState& st = *(JoyState*)ctx;
  const GUID& t = o->guidType;
  int i = t == GUID_XAxis ? 0 : t == GUID_YAxis ? 1 : t == GUID_ZAxis ? 2 : t == GUID_RxAxis ? 3 : t == GUID_RyAxis ? 4 : t == GUID_RzAxis ? 5 : -1;
  if (t == GUID_Slider) i = st.hasAxis[6] ? 7 : 6;
  if (i >= 0 && !st.hasAxis[i]) { st.hasAxis[i] = true; st.axes++; }
  return DIENUM_CONTINUE;
}

BOOL CALLBACK onDevice(LPCDIDEVICEINSTANCEW inst, void*) {
  IDirectInputDevice8W* d = nullptr;
  if (FAILED(s_di->CreateDevice(inst->guidInstance, &d, nullptr))) return DIENUM_CONTINUE;
  DIPROPGUIDANDPATH gp = {};
  gp.diph.dwSize = sizeof gp; gp.diph.dwHeaderSize = sizeof(DIPROPHEADER); gp.diph.dwHow = DIPH_DEVICE;
  if (SUCCEEDED(d->GetProperty(DIPROP_GUIDANDPATH, &gp.diph)) && (wcsstr(gp.wszPath, L"IG_") || wcsstr(gp.wszPath, L"ig_"))) { d->Release(); return DIENUM_CONTINUE; }
  if (FAILED(d->SetDataFormat(&c_dfDIJoystick2)) || FAILED(d->SetCooperativeLevel(s_hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) { d->Release(); return DIENUM_CONTINUE; }
  DIPROPRANGE r = {};   // every axis to -32768..32767; the dead zones are the game's own (the driver's is left at none)
  r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof(DIPROPHEADER); r.diph.dwHow = DIPH_DEVICE; r.lMin = -32768; r.lMax = 32767;
  d->SetProperty(DIPROP_RANGE, &r.diph);
  Device v; v.d = d;
  v.st.id = guidText(inst->guidInstance); v.st.name = narrow(inst->tszProductName);
  d->EnumObjects(onAxis, &v.st, DIDFT_AXIS);
  DIDEVCAPS caps = {}; caps.dwSize = sizeof caps;
  if (SUCCEEDED(d->GetCapabilities(&caps))) { v.st.buttons = std::min((int)caps.dwButtons, 120); v.st.hats = std::min((int)caps.dwPOVs, 2); }
  d->Acquire();
  s_devs.push_back(v);
  return s_devs.size() < 16 ? DIENUM_CONTINUE : DIENUM_STOP;
}
}  // namespace

bool joyInit(void* hwnd) {
  s_hwnd = (HWND)hwnd;
  return SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&s_di, nullptr));
}

void joyDevicesChanged() { s_rescan = true; }

void joyPoll(std::vector<JoyState>& out) {
  if (!s_di) { out.clear(); return; }
  if (s_rescan) {
    s_rescan = false;
    for (Device& v : s_devs) { v.d->Unacquire(); v.d->Release(); }
    s_devs.clear();
    s_di->EnumDevices(DI8DEVCLASS_GAMECTRL, onDevice, nullptr, DIEDFL_ATTACHEDONLY);
  }
  out.resize(s_devs.size());
  for (size_t k = 0; k < s_devs.size(); k++) {
    Device& v = s_devs[k];
    DIJOYSTATE2 js = {};
    HRESULT hr = v.d->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { v.d->Acquire(); hr = v.d->Poll(); }
    // (a failed read keeps the last one: a lever read as zero would be half throttle. A device unplugged goes at the
    // rescan Windows' device change brings)
    if ((FAILED(hr) && hr != DI_NOEFFECT) || FAILED(v.d->GetDeviceState(sizeof js, &js))) {
      std::fill(std::begin(v.st.pressed), std::end(v.st.pressed), false);
      out[k] = v.st;
      continue;
    }
    const LONG ax[kJoyAxes] = {js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.rglSlider[0], js.rglSlider[1]};
    for (int i = 0; i < kJoyAxes; i++) v.st.axis[i] = v.st.hasAxis[i] ? std::clamp(ax[i] / 32767.f, -1.f, 1.f) : 0.f;
    for (int b = 0; b < kJoyButtons; b++) {
      bool down = false;
      if (b < 120) down = b < v.st.buttons && (js.rgbButtons[b] & 0x80);
      else if ((b - 120) / 4 < v.st.hats) {   // a hat: centred is 0xFFFF; else hundredths of a degree clockwise from up
        const DWORD pov = js.rgdwPOV[(b - 120) / 4];
        if (LOWORD(pov) != 0xFFFF) {
          const int dir = (b - 120) % 4, deg = (int)(pov / 100) % 360, c = dir * 90;
          down = std::min(std::abs(deg - c), 360 - std::abs(deg - c)) < 68;   // (a diagonal holds both of its neighbours)
        }
      }
      v.st.pressed[b] = down && !v.st.button[b];
      v.st.button[b] = down;
    }
    out[k] = v.st;
  }
}
