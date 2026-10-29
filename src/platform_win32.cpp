// Somnium Volandi - Windows platform layer: window, OpenGL context, input, audio output
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "dev_switches.h"
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <shellapi.h>
#include <psapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "crash.h"
#include "game.h"
#include "jobs.h"
#include "tool_modes_win32.h"
#include "earth_charts.h"
#include "joystick.h"
#include "load_pacer.h"
#include "world_load_progress.h"

// Ask hybrid-graphics laptops for the dedicated GPU: the integrated one may reject or take minutes over the big scene shaders
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// ------------------------------------------------------------------ WGL bits
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);

// ------------------------------------------------------------------ frame pacing
// The frame-rate target (Settings): 0 runs at the monitor's refresh rate on vsync (adaptive when
// WGL_EXT_swap_control_tear is available, so a late frame tears once instead of halving the rate); a number caps the
// frame rate with a precise software limiter and vsync off.
static PFNWGLSWAPINTERVALEXTPROC s_swapInterval = nullptr;
static bool s_tear = false;
static int s_limitHz = 0;   // > 0: the software limiter's rate; 0: vsync paces
static Game* s_pacingGame = nullptr;
static void setupPacing(HWND hwnd) {
  int hz = 60;
  MONITORINFOEXA mi; mi.cbSize = sizeof mi;
  if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
    DEVMODEA dm; ZeroMemory(&dm, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) hz = (int)dm.dmDisplayFrequency;
  }
  int target = s_pacingGame ? s_pacingGame->set.fpsTarget : 0;
  if (s_pacingGame) s_pacingGame->monitorHz = hz;
  if (target <= 0 && s_swapInterval) { s_limitHz = 0; s_swapInterval(s_tear ? -1 : 1); }
  else { s_limitHz = target > 0 ? target : hz; if (s_swapInterval) s_swapInterval(0); }
}

// ------------------------------------------------------------------ XInput (loaded dynamically)
struct XGamepad { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XState { DWORD dwPacketNumber; XGamepad Gamepad; };
typedef DWORD(WINAPI* PFNXINPUTGETSTATE)(DWORD, XState*);
static PFNXINPUTGETSTATE s_xinput = nullptr;
struct XVibration { WORD left, right; };
typedef DWORD(WINAPI* PFNXINPUTSETSTATE)(DWORD, XVibration*);
static PFNXINPUTSETSTATE s_xinputSet = nullptr;
// with no pad there, its slot asked again a second on, or at once when a device comes or goes: XInput's answer for an
// empty slot can take a millisecond and more, every frame (pollPad)
static ULONGLONG s_padRetryAt = 0;
// the game's rumble (Game::rumbleLo / rumbleHi, 0..1) to the pad's motors: sent when it changes, off when the window
// loses the focus or the game ends
static void sendRumble(float lo, float hi) {
  if (!s_xinputSet) return;
  static int last[2] = {-1, -1};
  const int l = (int)(clampf(lo, 0.f, 1.f) * 65535.f), r = (int)(clampf(hi, 0.f, 1.f) * 65535.f);
  if (abs(l - last[0]) < 600 && abs(r - last[1]) < 600 && !(l == 0 && last[0] != 0) && !(r == 0 && last[1] != 0)) return;
  XVibration v{(WORD)l, (WORD)r};
  if (s_xinputSet(0, &v) == 0) { last[0] = l; last[1] = r; }
}

static Game* g_game = nullptr;
static HWND g_hwnd = nullptr;
static HDC g_hdc = nullptr;
static bool g_fullscreen = false;
static std::string s_startupLog;   // the save folder's startup.log, once the folder is known (a drained job's failure is appended: jobs.h)
static WINDOWPLACEMENT g_prevPlacement = {sizeof(WINDOWPLACEMENT)};
static HMODULE g_opengl32 = nullptr;

static void* wglProc(const char* name) {
  void* p = (void*)wglGetProcAddress(name);
  if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) p = (void*)GetProcAddress(g_opengl32, name);
  return p;
}

static void toggleFullscreen() {
  DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
  if (!g_fullscreen) {
    MONITORINFO mi = {sizeof(mi)};
    if (GetWindowPlacement(g_hwnd, &g_prevPlacement) && GetMonitorInfo(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
      SetWindowLong(g_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
      SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = true;
  } else {
    SetWindowLong(g_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(g_hwnd, &g_prevPlacement);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    g_fullscreen = false;
  }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Input* in = g_game ? &g_game->in : nullptr;
  switch (msg) {
    case WM_CLOSE: if (g_game) g_game->quit = true; return 0;
    case WM_DISPLAYCHANGE: case WM_EXITSIZEMOVE: setupPacing(h); break;   // refresh rate / monitor may have changed
    case WM_DEVICECHANGE: joyDevicesChanged(); s_padRetryAt = 0; break;   // (a joystick, throttle, pedals or pad plugged in or out)
    case WM_SIZE: if (g_ren.ok) g_ren.resize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: {   // (a window no smaller than 800 x 600 inside: the menus and settings are laid out for it - the review of v3.44.0, UI-4)
      RECT r = {0, 0, 800, 600}; AdjustWindowRect(&r, (DWORD)GetWindowLongPtr(h, GWL_STYLE), FALSE);
      MINMAXINFO* mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = r.right - r.left; mm->ptMinTrackSize.y = r.bottom - r.top;
      return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
      if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) {   // Alt+Enter: fullscreen only (the Enter never reaches the game)
        if (!(lp & (1 << 30))) { toggleFullscreen(); setupPacing(h); if (g_game) g_game->set.fullscreen = g_fullscreen; }
        return 0;
      }
      if (in && wp < 256 && !(lp & (1 << 30))) { in->pressed[wp] = true; in->down[wp] = true; }   // (autorepeat changes nothing: a key set aside on a context change stays aside until released)
      if (wp == VK_F10 || wp == VK_MENU) return 0;
      break;
    case WM_KEYUP: case WM_SYSKEYUP:
      if (in && wp < 256) in->down[wp] = false;
      if (wp == VK_F10 || wp == VK_MENU) return 0;
      break;
    case WM_SETFOCUS:
      if (g_game) g_game->focusGained();
      return 0;
    case WM_KILLFOCUS:   // switched away: nothing stays held, and a flight in progress pauses
      if (g_game) g_game->focusLost();
      if (GetCapture() == h) ReleaseCapture();
      break;
    case WM_MOUSEMOVE: if (in) { float x = (float)(short)LOWORD(lp), y = (float)(short)HIWORD(lp); in->mdx += x - in->mx; in->mdy += y - in->my; in->mx = x; in->my = y; } return 0;
    case WM_LBUTTONDOWN: if (in) { in->mDown[0] = in->mPressed[0] = true; SetCapture(h); } return 0;
    case WM_LBUTTONUP: if (in) { in->mDown[0] = false; in->mReleased[0] = true; ReleaseCapture(); } return 0;
    case WM_RBUTTONDOWN: if (in) { in->mDown[1] = in->mPressed[1] = true; SetCapture(h); } return 0;
    case WM_RBUTTONUP: if (in) { in->mDown[1] = false; in->mReleased[1] = true; ReleaseCapture(); } return 0;
    case WM_MBUTTONDOWN: if (in) in->mDown[2] = in->mPressed[2] = true; return 0;
    case WM_MBUTTONUP: if (in) { in->mDown[2] = false; in->mReleased[2] = true; } return 0;
    case WM_MOUSEWHEEL: if (in) in->wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.f; return 0;
    case WM_CHAR: if (in && wp >= 32 && wp < 127 && in->text.size() < 64) in->text += (char)wp; return 0;   // (typed text: the welcome's name)
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ------------------------------------------------------------------ audio output (WASAPI, waveOut as the fallback)
// The mixer (g_audio.render) is pulled by a dedicated thread. WASAPI shared mode, event driven, 10 ms buffer, float
// stereo at 48 kHz with the engine converting to the device's own format: about 10-20 ms of latency. If the device goes
// away (headphones unplugged, default device changed) the stream is reopened on the new default; if WASAPI can't be
// opened at all the old waveOut path (4 x 20 ms) carries on as before.
static std::atomic<bool> s_audioRun{true};
static HANDLE s_audioThread = nullptr;
static HANDLE s_audioEvent = nullptr;
// -- waveOut
static const int kAudioBuffers = 4, kAudioFrames = 960;  // 20 ms per buffer at 48 kHz
static HWAVEOUT s_waveOut = nullptr;
static WAVEHDR s_hdr[kAudioBuffers];
static int16_t s_pcm[kAudioBuffers][kAudioFrames * 2];
static bool s_wasapi = false;

static DWORD WINAPI waveOutThread(LPVOID) {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  static float buf[kAudioFrames * 2];
  while (s_audioRun) {
    WaitForSingleObject(s_audioEvent, 50);
    for (int i = 0; i < kAudioBuffers; i++) {
      if (!(s_hdr[i].dwFlags & WHDR_DONE)) continue;
      g_audio.render(buf, kAudioFrames);
      for (int k = 0; k < kAudioFrames * 2; k++) s_pcm[i][k] = (int16_t)(clampf(buf[k], -1.f, 1.f) * 32000.f);
      s_hdr[i].dwFlags &= ~WHDR_DONE;
      waveOutWrite(s_waveOut, &s_hdr[i], sizeof(WAVEHDR));
    }
  }
  return 0;
}
static bool startWaveOut() {
  WAVEFORMATEX wf = {};
  wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 48000; wf.wBitsPerSample = 16;
  wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 48000 * 4;
  if (waveOutOpen(&s_waveOut, WAVE_MAPPER, &wf, (DWORD_PTR)s_audioEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
  for (int i = 0; i < kAudioBuffers; i++) {
    memset(&s_hdr[i], 0, sizeof(WAVEHDR));
    memset(s_pcm[i], 0, sizeof(s_pcm[i]));
    s_hdr[i].lpData = (LPSTR)s_pcm[i]; s_hdr[i].dwBufferLength = sizeof(s_pcm[i]);
    waveOutPrepareHeader(s_waveOut, &s_hdr[i], sizeof(WAVEHDR));
    waveOutWrite(s_waveOut, &s_hdr[i], sizeof(WAVEHDR));
  }
  s_audioThread = CreateThread(nullptr, 0, waveOutThread, nullptr, 0, nullptr);
  return true;
}

// -- WASAPI (the interfaces by their GUIDs: no uuid library needed on MinGW)
static const GUID kClsidMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIidIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIidIAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID kIidIAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID kSubFormatFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};   // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif
struct WasapiOut {
  IAudioClient* client = nullptr; IAudioRenderClient* render = nullptr;
  UINT32 bufferFrames = 0;
  std::wstring devId;   // the endpoint the stream is on (a change of the default moves it: defaultChanged)
  void close() {
    if (client) client->Stop();
    if (render) { render->Release(); render = nullptr; }
    if (client) { client->Release(); client = nullptr; }
    bufferFrames = 0; devId.clear();
  }
  // the default render endpoint's id ("" when there is none)
  static std::wstring defaultId() {
    std::wstring r;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator, (void**)&en)) || !en) return r;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev) {
      LPWSTR id = nullptr;
      if (SUCCEEDED(dev->GetId(&id)) && id) { r = id; CoTaskMemFree(id); }
      dev->Release();
    }
    en->Release();
    return r;
  }
  // Windows' default output moved to another device that is still there (the old one is not invalidated, so the stream
  // would play on it): the caller reopens on the new default
  bool defaultChanged() const { std::wstring d = defaultId(); return !d.empty() && !devId.empty() && d != devId; }
  // the default render endpoint, shared mode, 10 ms, our float stereo 48 kHz converted by the engine
  bool open() {
    close();
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator, (void**)&en)) || !en) return false;
    IMMDevice* dev = nullptr;
    HRESULT hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    en->Release();
    if (FAILED(hr) || !dev) return false;
    { LPWSTR id = nullptr; if (SUCCEEDED(dev->GetId(&id)) && id) { devId = id; CoTaskMemFree(id); } }
    hr = dev->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, (void**)&client);
    dev->Release();
    if (FAILED(hr) || !client) { client = nullptr; devId.clear(); return false; }
    WAVEFORMATEXTENSIBLE wf = {};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; wf.Format.nChannels = 2; wf.Format.nSamplesPerSec = 48000;
    wf.Format.wBitsPerSample = 32; wf.Format.nBlockAlign = 8; wf.Format.nAvgBytesPerSec = 48000 * 8; wf.Format.cbSize = 22;
    wf.Samples.wValidBitsPerSample = 32; wf.dwChannelMask = 3; wf.SubFormat = kSubFormatFloat;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 100000 /* 10 ms, in 100 ns */, 0, &wf.Format, nullptr);
    if (FAILED(hr)) { close(); return false; }
    if (FAILED(client->SetEventHandle(s_audioEvent)) || FAILED(client->GetBufferSize(&bufferFrames)) || bufferFrames == 0) { close(); return false; }
    if (FAILED(client->GetService(kIidIAudioRenderClient, (void**)&render)) || !render) { close(); return false; }
    // silence in the whole buffer first, so the stream starts clean
    BYTE* data = nullptr;
    if (SUCCEEDED(render->GetBuffer(bufferFrames, &data)) && data) render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(client->Start())) { close(); return false; }
    return true;
  }
  // one event's worth: fill whatever the device has consumed. false: the device is gone (reopen)
  bool pump() {
    UINT32 padding = 0;
    HRESULT hr = client->GetCurrentPadding(&padding);
    if (FAILED(hr)) return hr != AUDCLNT_E_DEVICE_INVALIDATED && hr != AUDCLNT_E_SERVICE_NOT_RUNNING;
    UINT32 frames = bufferFrames > padding ? bufferFrames - padding : 0;
    if (frames == 0) return true;
    BYTE* data = nullptr;
    hr = render->GetBuffer(frames, &data);
    if (FAILED(hr) || !data) return hr != AUDCLNT_E_DEVICE_INVALIDATED && hr != AUDCLNT_E_SERVICE_NOT_RUNNING;
    g_audio.render((float*)data, (int)frames);
    render->ReleaseBuffer(frames, 0);
    return true;
  }
};
static WasapiOut s_wo;
static DWORD WINAPI wasapiThread(LPVOID) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  int lost = 0;
  ULONGLONG nextCheck = GetTickCount64() + 1000;
  while (s_audioRun) {
    if (!s_wo.client) {   // the device went away: reopen on the new default, trying every half second
      if (s_wo.open()) lost = 0;
      else { Sleep(500); if (++lost > 120) Sleep(2000); continue; }
    }
    WaitForSingleObject(s_audioEvent, 200);
    if (!s_audioRun) break;
    if (!s_wo.pump()) { s_wo.close(); continue; }
    if (GetTickCount64() >= nextCheck) {   // once a second: follow a change of Windows' default output device
      nextCheck = GetTickCount64() + 1000;
      if (s_wo.defaultChanged()) s_wo.close();
    }
  }
  s_wo.close();
  CoUninitialize();
  return 0;
}

static bool startAudio() {
  g_audio.init(48000);
  s_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  // WASAPI first (opened on the mixer thread itself so the COM apartment is its own); the main thread only checks it
  // can be opened once, so a machine without it falls straight back to waveOut
  {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WasapiOut probe;
    bool ok = probe.open();
    probe.close();
    if (ok) {
      s_wasapi = true;
      s_audioThread = CreateThread(nullptr, 0, wasapiThread, nullptr, 0, nullptr);
      return true;
    }
  }
  return startWaveOut();
}

static void stopAudio() {
  // stop the mixer thread and wait for it to finish its last buffer before the device goes away
  s_audioRun = false;
  if (s_audioThread) {
    if (s_audioEvent) SetEvent(s_audioEvent);
    WaitForSingleObject(s_audioThread, 2000);
    CloseHandle(s_audioThread); s_audioThread = nullptr;
  }
  if (s_waveOut) { waveOutReset(s_waveOut); for (int i = 0; i < kAudioBuffers; i++) waveOutUnprepareHeader(s_waveOut, &s_hdr[i], sizeof(WAVEHDR)); waveOutClose(s_waveOut); s_waveOut = nullptr; }
  if (s_audioEvent) { CloseHandle(s_audioEvent); s_audioEvent = nullptr; }
}
const char* audioBackendName() { return s_wasapi ? "WASAPI (shared, 10 ms)" : "waveOut (4 x 20 ms)"; }

// ------------------------------------------------------------------ gamepad
static void pollPad(Input& in) {
  if (!s_xinput) return;
  if (!in.pad && GetTickCount64() < s_padRetryAt) return;
  XState st = {};
  if (s_xinput(0, &st) != 0) {   // disconnected: nothing it was holding stays held
    if (in.pad) { in.buttons = 0; in.lx = in.ly = in.rx = in.ry = in.lt = in.rt = 0; }
    in.pad = false; s_padRetryAt = GetTickCount64() + 1000; return;
  }
  in.pad = true;
  auto ax = [](SHORT v) { return clampf(v / 32767.f, -1, 1); };
  in.lx = ax(st.Gamepad.sThumbLX); in.ly = ax(st.Gamepad.sThumbLY); in.rx = ax(st.Gamepad.sThumbRX); in.ry = ax(st.Gamepad.sThumbRY);
  in.lt = st.Gamepad.bLeftTrigger / 255.f; in.rt = st.Gamepad.bRightTrigger / 255.f;
  WORD b = st.Gamepad.wButtons;
  unsigned m = 0;
  if (b & 0x1000) m |= PAD_A; if (b & 0x2000) m |= PAD_B; if (b & 0x4000) m |= PAD_X; if (b & 0x8000) m |= PAD_Y;
  if (b & 0x0100) m |= PAD_LB; if (b & 0x0200) m |= PAD_RB; if (b & 0x0020) m |= PAD_BACK; if (b & 0x0010) m |= PAD_START;
  if (b & 0x0001) m |= PAD_UP; if (b & 0x0002) m |= PAD_DOWN; if (b & 0x0004) m |= PAD_LEFT; if (b & 0x0008) m |= PAD_RIGHT;
  if (b & 0x0040) m |= PAD_LS; if (b & 0x0080) m |= PAD_RS;
  in.buttonsPressed |= m & ~in.buttons;
  in.buttons = m;
}

static std::string userDir() {
  char buf[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
  if (!n) return ".";
  std::string d = std::string(buf) + "\\SomniumVolandi";   // (a new start: the old names' folders are left as they are)
  CreateDirectoryA(d.c_str(), nullptr);
  return d;
}

// Where photo mode's shots go: the player's Pictures\Somnium Volandi, where they would look for them (made on the first
// shot). Empty when Windows has no Pictures folder for this user: the shots then go to the save folder's Photos
static std::string photosDir() {
  char buf[MAX_PATH] = {};
  if (SHGetFolderPathA(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK || !buf[0]) return "";
  return std::string(buf) + "\\Somnium Volandi";
}

// Child process: SomniumVolandi.exe --build-shader-cache "<dir>" compiles every program into the binary cache and
// exits, reporting progress on stdout ("<programs done>" lines, then "m <programs compiled>"). The game runs the long
// first-time compile this way because NVIDIA's driver holds a process-wide lock while it links the big scene programs: no
// thread of the compiling process, whatever its context, can draw until it lets go, so the intro froze. A separate
// process has its own driver state, and the game then loads the finished programs from the cache in a moment.
// a helper process at full speed: Windows may run a process with no window of its own "efficiently" (power throttling,
// EcoQoS: lower clocks), and a helper's compile is what the launch is waiting for. v3.50.2's helpers each took two or
// three times as long over a program as the game itself had, the last one compiling alone too. (SetProcessInformation
// is Windows 8's, its power throttling Windows 10's: looked up, so an older Windows simply goes without)
static void helperUnthrottled() {
  struct PowerThrottling { ULONG version, controlMask, stateMask; } s = {1, 0x1, 0};   // (EXECUTION_SPEED controlled, and off)
  typedef BOOL(WINAPI* SetInfoFn)(HANDLE, int, LPVOID, DWORD);
  if (HMODULE k = GetModuleHandleA("kernel32.dll"))
    if (auto set = (SetInfoFn)(void*)GetProcAddress(k, "SetProcessInformation")) set(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &s, sizeof s);
}

// the process's CPU time so far (every thread's: the driver's compiler may run on several), seconds
static double processCpuSeconds() {
  FILETIME c, e, k, u;
  if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
  auto s = [](const FILETIME& f) { return (double)(((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; };
  return s(k) + s(u);
}

// a helper process's GL context, current: core 3.3 on a hidden window of its own, its functions loaded. 0, else 2 (no
// context) or 3 (the functions)
static int helperContext(HINSTANCE hInst, HGLRC& ctx) {
  helperUnthrottled();
  WNDCLASSEXW wc = {sizeof(wc)}; wc.style = CS_OWNDC; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hInst; wc.lpszClassName = L"SomniumVolandiCompile";
  RegisterClassExW(&wc);
  HWND hw = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr, hInst, nullptr);
  if (!hw) return 2;
  HDC dc = GetDC(hw);
  PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
  pfd.cDepthBits = 24; pfd.cStencilBits = 8; pfd.iLayerType = PFD_MAIN_PLANE;
  SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
  HGLRC legacy = wglCreateContext(dc);
  if (!legacy || !wglMakeCurrent(dc, legacy)) return 2;
  auto createAttribs = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
  int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
  ctx = createAttribs ? createAttribs(dc, nullptr, attrs) : nullptr;
  if (!ctx) return 2;
  wglMakeCurrent(dc, ctx); wglDeleteContext(legacy);
  g_opengl32 = LoadLibraryA("opengl32.dll");
  return glLoad(wglProc, nullptr) ? 0 : 3;
}

static int buildShaderCacheChild(HINSTANCE hInst, const std::string& dir) {
  HGLRC ctx = nullptr;
  if (const int r = helperContext(hInst, ctx)) return r;
  g_shaderCacheDir = dir;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  auto say = [&](const char* fmt2, int v) { char b[32]; int n = snprintf(b, sizeof b, fmt2, v); DWORD w; if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, b, n, &w, nullptr); };
  auto sayStage = [&](const std::string& st) { std::string l = "s " + st + "\n"; DWORD w; if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, l.data(), (DWORD)l.size(), &w, nullptr); };
  std::atomic<int> done{0}; std::atomic<bool> fin{false};
  std::thread rep([&] {   // (the count of programs built, and what is being built: the game's loading screen shows both)
    int last = -1; std::string lastStage;
    while (!fin) {
      int d = done; if (d != last) { last = d; say("%d\n", d); }
      std::string st = g_ren.compileStage(); if (st != lastStage) { lastStage = st; sayStage(st); }
      Sleep(15);
    }
  });
  bool ok = g_ren.compilePrograms(&done);
  fin = true; rep.join();
  say("%d\n", (int)done);
  say("m %d\n", g_shaderCacheMisses.load());
  wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctx);
  return ok ? 0 : 1;
}

// SomniumVolandi.exe --build-aircraft-shaders "<cache>" <item>,<item>,...: a helper of the launch's (AircraftShaderHelpers)
// building those of the aircraft types' own programs (Renderer::buildAfItem) into the cache, saying on its output how
// many are done as each is, then "m <compiled>"
static int buildAircraftShadersChild(HINSTANCE hInst, const std::string& dir, const std::string& list) {
  HGLRC ctx = nullptr;
  if (const int r = helperContext(hInst, ctx)) return r;
  g_shaderCacheDir = dir;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  auto say = [&](const std::string& l) { DWORD w; if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, l.data(), (DWORD)l.size(), &w, nullptr); };
  int done = 0;
  LARGE_INTEGER f; QueryPerformanceFrequency(&f);
  for (size_t at = 0; at < list.size();) {
    const size_t comma = std::min(list.find(',', at), list.size());
    const int item = atoi(list.c_str() + at);
    if (item >= 0 && item < Renderer::afItemCount()) {
      // (each one's time and the CPU it took, in compile.log: CPU well over the time, the compiler runs on several
      // threads and helpers side by side share the cores; about the same but slower than the game compiles it, the
      // CPU runs slower in them)
      LARGE_INTEGER a, b; QueryPerformanceCounter(&a); const double c0 = processCpuSeconds();
      g_ren.buildAfItem(item);
      QueryPerformanceCounter(&b);
      compileLogLine(fmt("helper %lu: item %d took %.1f s, %.1f s of CPU", (unsigned long)GetCurrentProcessId(), item,
                            (double)(b.QuadPart - a.QuadPart) / f.QuadPart, processCpuSeconds() - c0));
    }
    say(std::to_string(++done) + "\n");
    at = comma + 1;
  }
  say("m " + std::to_string(g_shaderCacheMisses.load()) + "\n");
  wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctx);
  return 0;
}

// The aircraft types' own shader programs (Renderer::buildAfItem: 46 of them) were most of a first launch's compiling
// after an update: one after another in the prewarm, 2.5 minutes on an RTX 3070 laptop (v3.50.1's diagnostics). Helper
// processes compile them side by side - each its own context, its own run of the driver's compiler - into the shader
// cache while the launch does everything else; the prewarm then loads them. aircraft_stamp.txt says the cache holds
// this build's. What a helper that fails, or is stopped, did not build the prewarm compiles itself, as before.
struct AircraftShaderHelpers {
  struct Helper { PROCESS_INFORMATION pi = {}; std::thread reader; std::atomic<int> done{0}, built{0}; int items = 0; };
  std::vector<std::unique_ptr<Helper>> helpers;
  int items = 0;
  std::string stampPath, stamp;
  LARGE_INTEGER t0 = {};
  bool started() const { return !helpers.empty(); }
  int done() const { int n = 0; for (const auto& h : helpers) n += std::min(h->done.load(), h->items); return n; }
  int built() const { int n = 0; for (const auto& h : helpers) n += h->built.load(); return n; }
  bool running() const { for (const auto& h : helpers) if (WaitForSingleObject(h->pi.hProcess, 0) == WAIT_TIMEOUT) return true; return false; }
  ~AircraftShaderHelpers() { finish(true); }
  // when the cache doesn't hold this build's (the calling thread's context current: the stamp is the driver's too)
  void start(const std::string& cacheDir) {
    if (cacheDir.empty() || started()) return;
    stampPath = cacheDir + "\\aircraft_stamp.txt"; stamp = aircraftProgramStamp();
    if (FILE* f = fopen(stampPath.c_str(), "r")) { char b[32] = {}; const bool current = fscanf(f, "%31s", b) == 1 && stamp == b; fclose(f); if (current) return; }
    // how many: half the CPU's threads less one (the launch's own work runs beside them), 1 to 6, and no more than the
    // free memory holds (the compiler takes some hundreds of MB on these programs)
    int n = std::clamp((int)std::thread::hardware_concurrency() / 2 - 1, 1, 6);
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) n = std::clamp((int)(((long long)ms.ullAvailPhys - (1536ll << 20)) / (512ll << 20)), 1, n);
    char exe[MAX_PATH] = {}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
    QueryPerformanceCounter(&t0);
    for (const std::vector<int>& share : Renderer::afShareItems(n)) {
      if (share.empty()) continue;
      std::string cmd = std::string("\"") + exe + "\" --build-aircraft-shaders \"" + cacheDir + "\" ";
      for (size_t k = 0; k < share.size(); k++) cmd += (k ? "," : "") + std::to_string(share[k]);
      SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
      HANDLE rd = nullptr, wr = nullptr;
      if (!CreatePipe(&rd, &wr, &sa, 0)) continue;
      SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
      STARTUPINFOA si = {sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
      std::vector<char> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
      auto h = std::make_unique<Helper>(); h->items = (int)share.size();
      if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &h->pi)) {
        CloseHandle(rd); CloseHandle(wr); continue;
      }
      CloseHandle(wr);   // (the helper holds its own copy: the reader sees the pipe end when it exits)
      Helper* hp = h.get();
      hp->reader = std::thread([hp, rd] {
        std::string line; char buf[256]; DWORD got = 0;
        while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got > 0)
          for (DWORD i = 0; i < got; i++) {
            if (buf[i] != '\n') { line += buf[i]; continue; }
            if (line.size() > 2 && line[0] == 'm') hp->built = atoi(line.c_str() + 2);
            else if (!line.empty() && line[0] >= '0' && line[0] <= '9') hp->done = atoi(line.c_str());
            line.clear();
          }
        CloseHandle(rd);
      });
      items += h->items;
      helpers.push_back(std::move(h));
    }
  }
  // waits for every helper (stop: ends them first); true when each built its share, and then the stamp is written
  bool finish(bool stop) {
    if (helpers.empty()) return false;
    bool all = true;
    for (const auto& h : helpers) {
      if (stop) TerminateProcess(h->pi.hProcess, 9);
      WaitForSingleObject(h->pi.hProcess, INFINITE);
      if (h->reader.joinable()) h->reader.join();
      DWORD code = 1; GetExitCodeProcess(h->pi.hProcess, &code);
      all = all && code == 0 && h->done >= h->items;
      CloseHandle(h->pi.hProcess); CloseHandle(h->pi.hThread);
    }
    lastCount = (int)helpers.size(); lastBuilt = built(); lastItems = items;
    LARGE_INTEGER t1; QueryPerformanceCounter(&t1); LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    lastSecs = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
    helpers.clear(); items = 0;
    const bool ok = all && !stop;
    if (ok) if (FILE* fo = fopen(stampPath.c_str(), "w")) { fprintf(fo, "%s\n", stamp.c_str()); fclose(fo); }
    return ok;
  }
  // waits for them as long as they get on (with nothing done for 5 minutes, the shader cache helper's limit, they are
  // stopped), `tick` called between looks: it draws the progress and answers the window, and false stops them (the game
  // closed); true as finish
  bool wait(const std::function<bool(int done, int items)>& tick) {
    LARGE_INTEGER f, now, moved; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&moved);
    int last = -1;
    const int all = items;
    bool go = true;
    while (go && running()) {
      QueryPerformanceCounter(&now);
      const int d = done();
      if (d != last) { last = d; moved = now; }
      else if (now.QuadPart - moved.QuadPart > f.QuadPart * 300) break;
      go = tick(d, all);
    }
    return finish(!go || running());
  }
  int lastCount = 0, lastBuilt = 0, lastItems = 0; double lastSecs = 0;   // (the last finish's: the helpers, what they compiled, of how many, in how long)
};

// Where the launch keeps what it builds: %LOCALAPPDATA%\SomniumVolandi. Local, not Roaming: none of it is the player's
// (it is all rebuilt at will), the shader binaries are this GPU's and driver's alone, and a roaming profile would carry
// hundreds of MB between machines at every sign-in. Where Local can't be written, next to the game, then with the save
// data, as before. A cache in either of those old places (a shadercache folder) is moved over once - a rename on the
// same drive; from another drive it is cleared instead, as most of it is the last build's and would be rebuilt anyway.
static std::string cacheDirFor(const std::string& exeDir, const std::string& saveDir) {
  auto writable = [](const std::string& d) {
    CreateDirectoryA(d.c_str(), nullptr);
    std::string probe = d + "\\.probe";
    FILE* f = fopen(probe.c_str(), "wb"); if (!f) return false;
    fclose(f); remove(probe.c_str()); return true;
  };
  auto clearOld = [](const std::string& d) {   // (only what the cache itself writes there, then the folder if that empties it)
    for (const char* pat : {"\\*.bin", "\\*.try", "\\*.txt", "\\*.log"}) {
      WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((d + pat).c_str(), &fd);
      if (h == INVALID_HANDLE_VALUE) continue;
      do { DeleteFileA((d + "\\" + fd.cFileName).c_str()); } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
    RemoveDirectoryA(d.c_str());
  };
  const std::string olds[] = {exeDir + "\\shadercache", saveDir + "\\shadercache"};
  char local[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH);
  if (n && n < MAX_PATH) {
    const std::string dir = std::string(local) + "\\SomniumVolandi";
    if (GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
      for (const std::string& old : olds)
        if (GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES && MoveFileExA(old.c_str(), dir.c_str(), 0)) break;
    if (writable(dir)) {
      for (const std::string& old : olds) if (GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES) clearOld(old);
      return dir;
    }
  }
  for (const std::string& old : olds) if (writable(old)) return old;
  return std::string();
}

// the window answers its messages inside a long bake or a helper wait (no "Not Responding" on a slow scene)
static void pumpB() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  {
    std::string cl = GetCommandLineA();
    // the development switches (dev_switches.h): on for a tool's run (diagnostics.bat), the launch's helper
    // processes and --dev; off in a player's ordinary launch, so nothing in their shell changes the game
    dev::enable(toolLaunch(cl) || cl.find("--dev") != std::string::npos || cl.find("--build-") != std::string::npos);
    size_t k = cl.find("--build-shader-cache \"");
    if (k != std::string::npos) {
      size_t b = k + 22, e = cl.find('"', b);
      return buildShaderCacheChild(hInst, cl.substr(b, e == std::string::npos ? std::string::npos : e - b));
    }
    k = cl.find("--build-aircraft-shaders \"");
    if (k != std::string::npos) {
      const size_t b = k + 26, e = cl.find('"', b);
      if (e == std::string::npos) return 2;
      std::string list = cl.substr(e + 1);
      list.erase(0, list.find_first_not_of(' ')); list = list.substr(0, list.find(' '));
      return buildAircraftShadersChild(hInst, cl.substr(b, e - b), list);
    }
  }
  SetProcessDPIAware();
  timeBeginPeriod(1);
  static Game game;
  g_game = &game; s_pacingGame = &game;
  game.saveDir = userDir();
  s_startupLog = game.saveDir + "\\startup.log";
  jobs::setLog([](const std::string& line) { if (FILE* f = fopen(s_startupLog.c_str(), "a")) { fprintf(f, "%s\n", line.c_str()); fclose(f); } });
  game.photoDir = photosDir();
  { char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);   // pictures etc. live next to the exe
    std::string d(exe, n); size_t sl = d.find_last_of("\\/"); game.assetDir = sl == std::string::npos ? std::string(".") : d.substr(0, sl); }
  {   // crash reports (crash.h): the handlers first, then what the last session left
    char version[96] = "development build";
    if (FILE* f = fopen((game.assetDir + "\\VERSION.txt").c_str(), "r")) {
      if (fgets(version, sizeof version, f)) version[strcspn(version, "\r\n")] = 0;
      fclose(f);
    }
    game.version = version;
    const std::string crashDir = game.saveDir + "\\crash";
    crashInstall(crashDir, game.saveDir + "\\startup.log", version);
    game.crashReport = crashTakePending(crashDir);
    crashPrune(crashDir);
    game.platformOpen = [](const std::string& path) { ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    if (strstr(GetCommandLineA(), "--test-crash")) { volatile int* none = nullptr; *none = 1; }   // (checks the reports end to end)
  }
  // Steam (steam.h): before the window, as Valve asks, when steam_api64.dll is beside the exe; never in a tool's run
  if (!toolLaunch(GetCommandLineA())) steam::init(game.assetDir);

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndProc; wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
  wc.lpszClassName = L"SomniumVolandiWnd";
  RegisterClassExW(&wc);
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  int ww = std::min(1600, sw * 4 / 5), wh = ww * 9 / 16;
  RECT r = {0, 0, ww, wh};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Somnium Volandi", WS_OVERLAPPEDWINDOW, (sw - (r.right - r.left)) / 2, (sh - (r.bottom - r.top)) / 2,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
  g_hdc = GetDC(g_hwnd);
  PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
  pfd.cDepthBits = 24; pfd.cStencilBits = 8; pfd.iLayerType = PFD_MAIN_PLANE;
  SetPixelFormat(g_hdc, ChoosePixelFormat(g_hdc, &pfd), &pfd);
  HGLRC legacy = wglCreateContext(g_hdc);
  wglMakeCurrent(g_hdc, legacy);
  auto createAttribs = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
  HGLRC ctx = nullptr;
  if (createAttribs) {
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    ctx = createAttribs(g_hdc, nullptr, attrs);
  }
  if (!ctx) { MessageBoxA(g_hwnd, "Somnium Volandi needs an OpenGL 3.3 capable graphics driver.\nPlease update your graphics drivers.", "Somnium Volandi", MB_ICONERROR); return 1; }
  wglMakeCurrent(g_hdc, ctx);
  wglDeleteContext(legacy);
  g_opengl32 = LoadLibraryA("opengl32.dll");
  const char* missing = nullptr;
  if (!glLoad(wglProc, &missing)) { MessageBoxA(g_hwnd, (std::string("Missing OpenGL function: ") + missing).c_str(), "Somnium Volandi", MB_ICONERROR); return 1; }
  crashInstall(game.saveDir + "\\crash", game.saveDir + "\\startup.log", nullptr);   // (ours again, over any the driver set)
  s_swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
  if (auto ext = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT")) { const char* e = ext(); s_tear = e && strstr(e, "WGL_EXT_swap_control_tear"); }
  timeBeginPeriod(1);   // 1 ms Sleep granularity for the frame limiter
  setupPacing(g_hwnd);
  const char* xdlls[] = {"xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll"};
  for (auto d : xdlls) { HMODULE m = LoadLibraryA(d); if (m) { s_xinput = (PFNXINPUTGETSTATE)GetProcAddress(m, "XInputGetState"); s_xinputSet = (PFNXINPUTSETSTATE)GetProcAddress(m, "XInputSetState"); if (s_xinput) break; } }
  joyInit(g_hwnd);   // joysticks, throttles and pedals (joystick_win.cpp); none: the game runs without them

  ShowWindow(g_hwnd, SW_SHOW);
  game.cacheDir = cacheDirFor(game.assetDir, game.saveDir);   // (first: this PC's own display settings are kept there, display.cfg)
  game.loadSettings();   // early, for fullscreen during the intro (Game::init loads them again)
  if (game.set.fullscreen) { toggleFullscreen(); setupPacing(g_hwnd); }
  game.platformPresent = [] { SwapBuffers(g_hdc); pumpB(); };   // (a frame shown from inside a long bake: the research terminal's boot screen)

  // startup.log names the GPU in use (support aid)
  std::string gpu = std::string((const char*)glGetString(GL_RENDERER)) + " / " + (const char*)glGetString(GL_VERSION);
  // the scene programs sample up to 31 texture units in one fragment shader; OpenGL 3.3 only guarantees 16 (every current GPU has 32)
  GLint texUnits = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texUnits);
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "w")) { fprintf(f, "GPU: %s\nFragment texture units: %d\n", gpu.c_str(), (int)texUnits); fclose(f); }
  if (texUnits > 0 && texUnits < 22) {
    MessageBoxA(g_hwnd, ("This graphics driver offers " + std::to_string(texUnits) + " texture units per shader; Somnium Volandi needs 22.\n"
                         "Please update the graphics driver, or run the game on the dedicated GPU.").c_str(), "Somnium Volandi", MB_ICONERROR);
    return 1;
  }
  bool cached = false;
  {   // what the launch builds - the shader programs as the driver compiled them, the islands, each type's learned
      // performance, the aircraft bodies - is kept in %LOCALAPPDATA%\SomniumVolandi (cacheDirFor, above)
    char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string exeDir = std::string(exe, n);
    size_t sl = exeDir.find_last_of("\\/");
    exeDir = sl == std::string::npos ? std::string(".") : exeDir.substr(0, sl);
    const std::string dir = cacheDirFor(exeDir, game.saveDir);
    g_shaderCacheDir = dir;
    game.cacheDir = dir;
    // the real Earth's pack, beside the exe or in the user's or the cache's folder, and its fetched fine elevation
    // (earth_fetch.h) kept with the rest of what the game builds and fetches
    earthCharts::setSearch({game.assetDir + "\\earth", game.saveDir + "\\earth", dir + "\\earth"}, dir + "\\earth-glo90");
    // this build's stamp (the exe's size and time): what the launch caches beside the shaders (the islands, the aircraft
    // performance) is kept for this build only, and any other build makes it again
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (n && GetFileAttributesExA(exe, GetFileExInfoStandard, &fa)) {
      char b[64]; snprintf(b, sizeof b, "%08lx%08lx-%08lx%08lx", fa.nFileSizeHigh, fa.nFileSizeLow, fa.ftLastWriteTime.dwHighDateTime, fa.ftLastWriteTime.dwLowDateTime);
      game.buildStamp = b;
    }
    WIN32_FIND_DATAA fd; HANDLE h = dir.empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((dir + "\\*.bin").c_str(), &fd);
    cached = h != INVALID_HANDLE_VALUE; if (cached) FindClose(h);
  }
  game.shaderFirstRun = !cached;
  auto fatal = [&](const std::string& what) {
    FILE* f = fopen((game.saveDir + "\\error.log").c_str(), "w");
    if (f) { fprintf(f, "%s\nRenderer: %s / %s\n%s", what.c_str(), (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION), g_shaderNotes.c_str()); fclose(f); }
    if (!g_shaderNotes.empty()) if (FILE* fl = fopen((game.saveDir + "\\startup.log").c_str(), "a")) { fprintf(fl, "%s", g_shaderNotes.c_str()); fclose(fl); }
    MessageBoxA(g_hwnd, ("Graphics initialisation failed:\n" + what.substr(0, 1500)).c_str(), "Somnium Volandi", MB_ICONERROR);
  };
  RECT cr; GetClientRect(g_hwnd, &cr);
  if (!g_ren.initUI(std::max(64L, cr.right), std::max(64L, cr.bottom))) { fatal(g_ren.error); return 1; }

  // the intro shows the application icon large: its 256 px frame from the exe's resources
  GLuint iconTex = 0;
  std::vector<uint8_t> iconPx;
  if (HICON hi = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR)) {
    ICONINFO ii = {};
    if (GetIconInfo(hi, &ii)) {
      BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = 256; bi.bmiHeader.biHeight = -256;
      bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
      std::vector<uint8_t> px(256 * 256 * 4);
      HDC sdc = GetDC(nullptr);
      if (ii.hbmColor && GetDIBits(sdc, ii.hbmColor, 0, 256, px.data(), &bi, DIB_RGB_COLORS) == 256) {
        for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);   // BGRA -> RGBA
        iconTex = g_ren.makeTexture(px.data(), 256, 256);
        iconPx = px;
      }
      ReleaseDC(nullptr, sdc);
      if (ii.hbmColor) DeleteObject(ii.hbmColor);
      if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
    DestroyIcon(hi);
  }

  game.iconTex = iconTex;
  // Shaders compile on a worker thread with its own context (sharing objects with the main one) on a hidden
  // window, while another thread generates the world and the main thread animates the intro at 60 Hz.
  WNDCLASSEXW wc2 = {sizeof(wc2)}; wc2.style = CS_OWNDC; wc2.lpfnWndProc = DefWindowProcW; wc2.hInstance = hInst; wc2.lpszClassName = L"SomniumVolandiGL";
  RegisterClassExW(&wc2);
  HWND hw2 = CreateWindowExW(0, wc2.lpszClassName, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr, hInst, nullptr);
  HDC dc2 = hw2 ? GetDC(hw2) : nullptr;
  HGLRC ctx2 = nullptr;
  if (dc2 && SetPixelFormat(dc2, GetPixelFormat(g_hdc), &pfd)) {
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    ctx2 = createAttribs(dc2, ctx, attrs);
  }
  std::atomic<int> progDone{0}, compileState{0};   // state: 0 running, 1 ok, 2 failed
  std::atomic<bool> worldDone{false};
  const std::string worldCache = game.cacheDir.empty() || game.buildStamp.empty() ? std::string() : game.cacheDir + "\\world.bin";
  std::thread worldThread([&] { g_world.build(worldCache, game.buildStamp); worldDone = true; });
  // the first-time compile runs in a child process (see buildShaderCacheChild); this process then loads the results
  PROCESS_INFORMATION child = {};
  std::atomic<int> childDone{-1}, childMisses{0};
  std::atomic<bool> childReadDone{false};
  std::mutex childStageMu; std::string childStage;   // (what the child is building, as it reports it)
  std::thread childReader;
  const std::string stampPath = g_shaderCacheDir.empty() ? std::string() : g_shaderCacheDir + "\\stamp.txt", stamp = shaderCacheStamp();
  bool cacheCurrent = false;
  if (!stampPath.empty()) if (FILE* f = fopen(stampPath.c_str(), "r")) { char b[32] = {}; cacheCurrent = fscanf(f, "%31s", b) == 1 && stamp == b; fclose(f); }
  // ---- the launch's steps (load_pacer.h), the bar showing how many of their things are done: the shader programs and
  // the islands side by side, the career and the aircraft performance, the renderer's textures, the menu, then every
  // aircraft's meshes
  const std::string cmdLine = GetCommandLineA();
  // (--raster, from older scripts, is accepted and ignored: there is one renderer)
  const bool tool = toolLaunch(cmdLine);
  auto exists = [](const std::string& p) { return !p.empty() && GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
  g_ren.prebuiltAircraftDir = game.assetDir + "\\aircraft"; // immutable full-quality geometry bundled beside the exe
  g_ren.checkMeshCache();
  const bool perfFresh = game.cacheDir.empty() || !exists(game.cacheDir + "\\perf.bin");
  LoadPacer pace;
  // the launch's bar paced by the last launch's measured times (load_pacer.h): a step moves it as far as its real work
  if (!game.cacheDir.empty()) pace.loadTimes(game.cacheDir + "\\launch_pacing.txt");
  if (ctx2 && !g_shaderCacheDir.empty() && !cacheCurrent) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (CreatePipe(&rd, &wr, &sa, 0)) {
      SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
      char exe[MAX_PATH] = {}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string cmd = std::string("\"") + exe + "\" --build-shader-cache \"" + g_shaderCacheDir + "\"";
      STARTUPINFOA si = {sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
      std::vector<char> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
      if (CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &child)) {
        childDone = 0;
        childReader = std::thread([rd, &childDone, &childMisses, &childStageMu, &childStage, &childReadDone] {
          std::string line; char buf[256]; DWORD n = 0;
          while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0)
            for (DWORD i = 0; i < n; i++) {
              if (buf[i] != '\n') { line += buf[i]; continue; }
              if (line.size() >= 2 && line[0] == 's' && line[1] == ' ') { std::lock_guard<std::mutex> lk(childStageMu); childStage = line.substr(2); }
              else if (line.size() > 2 && line[0] == 'm') childMisses = atoi(line.c_str() + 2);
              else if (!line.empty()) childDone = atoi(line.c_str());
              line.clear();
            }
          CloseHandle(rd);
          childReadDone = true;
        });
      } else CloseHandle(rd);
      CloseHandle(wr);   // the child holds its own copy: the reader sees the end of the pipe when the child exits
    }
  }
  // the aircraft types' own programs, when the cache doesn't hold this build's: compiled by helpers from now on, beside
  // everything else (the tools start theirs in buildBodies)
  AircraftShaderHelpers afHelpers;
  if (!tool) afHelpers.start(g_shaderCacheDir);
  // the launch's steps, each a category on the loading screen: what each really loads, and the bar paced by the time
  // each took at the last launch (pace.loadTimes)
  const int stStart = pace.add("start", Renderer::kProgramCount, 0.f, "SHADER PROGRAMS");   // (each shader program built or loaded)
  const int stWorld = pace.add("world", 16, 0.f, "WORLD");   // (the islands, built or read from the cache beside them)
  const int stCache = child.hProcess ? pace.add("optional-cache", Renderer::kProgramCount, 0.f, "SHADER CACHE") : -1;
  const int stInit = pace.add("career", kNumAircraft + 4, 0.f, "PILOT SYSTEMS");
  const int stTex = pace.add("renderer", 11, 0.f, "RENDERER");
  const int stAfShaders = afHelpers.started() ? pace.add("aircraft-shaders", afHelpers.items, 0.f, "AIRCRAFT SHADERS") : -1;
  const int stMenu = tool ? -1 : pace.add("menu", 12, 0.f, "MENU SCENERY");
  std::vector<int> meshSteps;
  if (!tool)
    for (const auto& it : Game::prewarmItems(true)) meshSteps.push_back(pace.add("mesh" + std::to_string(it.first) + (it.second ? "c" : "o"), 9, 9.f, "AIRCRAFT BODIES"));
  const int stStage = tool ? -1 : pace.add("stage", 10, 0.f, "PREVIEW STAGE");   // (the stand every craft preview is drawn at, and its circuit)
  if (!tool)   // (then the hive's craft, after the aircraft: prewarmHiveItems)
    for (int k = 0; k < Game::prewarmHiveItems(true); k++) meshSteps.push_back(pace.add("mesh-hive" + std::to_string(k), 9, 9.f, "HIVE CRAFT"));
  pace.begin(stStart);
  std::thread compileThread;
  if (ctx2) compileThread = std::thread([&] {
    if (child.hProcess) {   // wait for the child's compile (it may take a minute on the first run), then load its results
      if (WaitForSingleObject(child.hProcess, 300000) == WAIT_TIMEOUT) TerminateProcess(child.hProcess, 9);
      if (childReader.joinable()) childReader.join();
    }
    if (game.quit) { compileState = 2; return; }   // closed during the intro
    wglMakeCurrent(dc2, ctx2);
    bool ok = g_ren.compilePrograms(&progDone);
    wglMakeCurrent(nullptr, nullptr);
    compileState = ok ? 1 : 2;
  });
  LARGE_INTEGER freq, prev, now;
  QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
  LARGE_INTEGER t0 = prev; float compileSecs = 0;
  // The intro animates on its own thread with its own GL context. That context shares nothing with the others, so
  // nothing the loading work does in the driver (shader links on the worker context, texture uploads and the
  // renderer's set-up on the main one) can stall it. The main thread only posts the progress target and stage.
  struct Intro {
    std::mutex m; std::string stage;
    std::atomic<float> target{0.f};
    std::atomic<LoadPacer*> pacer{nullptr};   // (set: the bar follows the pacer every frame, not only when the main thread posts)
    std::atomic<int> state{0};          // 0 starting, 1 running, 2 fade out and stop, 3 stop now, 4 finished, -1 unavailable
    std::thread th;
  } intro;
  const bool introMotion=game.set.menuMotion; // immutable across both intro contexts
  std::atomic<bool> synchronousShaders{false};
  intro.th = std::thread([&] {
    HDC dcI = GetDC(g_hwnd);
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    HGLRC ctxI = createAttribs ? createAttribs(dcI, nullptr, attrs) : nullptr;
    if (!ctxI || !wglMakeCurrent(dcI, ctxI)) { if (ctxI) wglDeleteContext(ctxI); ReleaseDC(g_hwnd, dcI); intro.state = -1; return; }
    if (s_swapInterval) s_swapInterval(1);   // (the intro runs on vsync)
    std::unique_ptr<Renderer> R(new Renderer());
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (!R->initUI(std::max(64L, rc.right), std::max(64L, rc.bottom))) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctxI); ReleaseDC(g_hwnd, dcI); intro.state = -1; return; }
    GLuint ic = iconPx.empty() ? 0 : R->makeTexture(iconPx.data(), 256, 256);
    int expected = 0; intro.state.compare_exchange_strong(expected, 1);
    float shown = 0, fade = 1.f, lastTime = 0.f;
    for (;;) {
      int st = intro.state;
      if (st == 3) break;
      LARGE_INTEGER n; QueryPerformanceCounter(&n);
      float t = (float)(n.QuadPart - t0.QuadPart) / freq.QuadPart;
      float dt = std::max(0.f, t - lastTime); lastTime = t;
      if (st == 2) { fade -= dt / 0.35f; if (fade <= 0.f) break; }
      if (synchronousShaders) { pace.setDone(stStart, (float)progDone); pace.setDone(stWorld, 16.f); }
      LoadPacer* lp = intro.pacer;
      float target = lp ? lp->fraction() : (float)intro.target;
      shown = clampf(target,0.f,1.f); // exact completed work; the independent decoration supplies motion
      std::string stage; { std::lock_guard<std::mutex> lk(intro.m); stage = intro.stage; }
      GetClientRect(g_hwnd, &rc);
      if (rc.right > 0 && rc.bottom > 0) { R->W = rc.right; R->H = rc.bottom; }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, R->W, R->H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
      R->uiBegin(); game.drawIntro(shown, stage, t, ic, fade, *R, &pace, introMotion); R->uiEnd();
      SwapBuffers(dcI);
      if (!s_swapInterval) Sleep(14);
    }
    glFinish();
    R.reset();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctxI);   // takes the intro's textures and buffers with it
    ReleaseDC(g_hwnd, dcI);
    intro.state = 4;
  });
  intro.pacer = &pace;
  while (intro.state == 0) Sleep(1);
  const bool introThreaded = intro.state == 1;
  if (!introThreaded) intro.th.join();
  // falls back to drawing the intro on this thread if the intro context could not be made
  float shownMain = 0, lastMainTime = 0;
  auto introFrame = [&](float target, const std::string& stage, float fade) {
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
    QueryPerformanceCounter(&now);
    float t = (float)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
    if (introThreaded) {
      intro.target = target;
      { std::lock_guard<std::mutex> lk(intro.m); intro.stage = stage; }
      Sleep(10);
      return t;
    }
    shownMain = clampf(target,0.f,1.f);
    lastMainTime = t;
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) { g_ren.W = rc.right; g_ren.H = rc.bottom; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_ren.W, g_ren.H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    g_ren.uiBegin(); game.drawIntro(shownMain, stage, t, iconTex, fade, g_ren, &pace, introMotion); g_ren.uiEnd();
    SwapBuffers(g_hdc);
    if (!s_swapInterval || s_limitHz > 0) Sleep(14);
    return t;
  };
  // ends the intro: `fade` plays its fade-out first; afterwards this thread owns the window's drawing again
  auto stopIntro = [&](bool fade) {
    if (!introThreaded) {
      if (fade) {
        LARGE_INTEGER start; QueryPerformanceCounter(&start);
        float alpha = 1.f;
        while (alpha > 0.f && !game.quit) {
          introFrame(1.f, "Ready", alpha);
          LARGE_INTEGER n; QueryPerformanceCounter(&n);
          alpha = 1.f - (float)(n.QuadPart - start.QuadPart) / (freq.QuadPart * 0.35f);
        }
      }
      return;
    }
    if (intro.state != 1) return;
    if (fade) { intro.pacer = nullptr; intro.target = 1.f; { std::lock_guard<std::mutex> lk(intro.m); intro.stage = "Ready"; } }
    intro.state = fade ? 2 : 3;
    while (intro.state != 4) {   // keep the window responsive while it finishes
      MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
      Sleep(5);
    }
    intro.th.join();
  };
  bool helperSkipped = false;
  auto finalizeCacheHelper = [&] {
    if (stCache < 0) return;
    pace.setDone(stCache, (float)std::max(0, std::min((int)childDone, Renderer::kProgramCount)));
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(child.hProcess, &code);
    helperSkipped = code != 0 || childDone < Renderer::kProgramCount;
    if (helperSkipped) pace.skipRemaining(stCache);
  };
  for (;;) {
    QueryPerformanceCounter(&now);
    float t = (float)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
    const bool helperRunning = child.hProcess && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT;
    int d = helperRunning ? (int)childDone : (int)progDone;
    if (stCache >= 0) {
      pace.setDone(stCache, (float)std::max(0, std::min((int)childDone, Renderer::kProgramCount)));
      if (!helperRunning && childReadDone) {
        // A failed/timed-out helper is optional. Its unfinished programs are skipped,
        // never reported as built; the independent main-context pass still has to finish.
        finalizeCacheHelper();
      }
    }
    bool compiled = !ctx2 || compileState != 0, built = worldDone;
    if (d == 0 && compileState == 0) compileSecs = t;
    // what is being done, by name: the shader program (the child process compiling on a first launch reports a count only)
    std::string stage;
    if (!ctx2) stage = "Preparing";
    else if (compileState == 0) {
      std::string sh = g_ren.compileStage();
      if (childDone >= 0 && child.hProcess && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) { std::lock_guard<std::mutex> lk(childStageMu); sh = childStage; }   // (the child compiling: what it reports)
      stage = helperRunning ? "Preparing GPU shader cache" : helperSkipped ? "Cache preparation skipped; building graphics programs" : "Loading graphics programs";
      if (!sh.empty()) stage += ": " + sh;
      stage += "   " + std::to_string(std::min(d + 1, Renderer::kProgramCount)) + " of " + std::to_string(Renderer::kProgramCount);
    } else stage = "Shaders ready";
    if (!built) stage += " | "+std::string(g_worldLoadTask.load())+fmt(" (%d%%)",int(g_worldLoadProgress.load()*100));
    pace.setDone(stStart, (float)std::min((int)progDone, Renderer::kProgramCount));   // (the programs done, and the islands beside them)
    pace.setDone(stWorld, built ? 16.f : 16.f*g_worldLoadProgress.load());
    if(compileState==0)pace.task("Graphics programs",float(std::min((int)progDone,Renderer::kProgramCount)),float(Renderer::kProgramCount));
    else pace.task(g_worldLoadTask.load(),g_worldLoadProgress.load(),1.f);
    introFrame(pace.fraction(), stage, 1.f);
    if (game.quit) break;
    if (compiled && built) break;   // no artificial delay after the real work finishes
  }
  if (game.quit && child.hProcess) TerminateProcess(child.hProcess, 9);   // closed during the intro: don't wait for it
  if (compileThread.joinable()) compileThread.join();
  // Reconcile once more after joining: the helper and main compile may both finish
  // between the loop's snapshots. The reader is now joined and its final count stable.
  // Do this before closing the process handle or begin(career) can close optional work.
  finalizeCacheHelper();
  if (child.hProcess) { CloseHandle(child.hProcess); CloseHandle(child.hThread); }
  worldThread.join();
  if (ctx2) wglDeleteContext(ctx2);
  if (dc2) ReleaseDC(hw2, dc2);
  if (hw2) DestroyWindow(hw2);
  if (game.quit) { stopIntro(false); return 0; }
  if (ctx2 && compileState != 1) { stopIntro(false); fatal(g_ren.error); return 1; }
  if (!ctx2) {
    introFrame(pace.fraction(), "Preparing graphics programs", 1.f);
    synchronousShaders = true;
    bool shaderOk = g_ren.compilePrograms(&progDone);
    synchronousShaders = false;
    if (!shaderOk) { stopIntro(false); fatal(g_ren.error); return 1; }
    pace.setDone(stStart, (float)Renderer::kProgramCount); pace.setDone(stWorld, 16.f);
  }
  if (ctx2 && compileState == 1 && !stampPath.empty() && !cacheCurrent && g_ren.dispError.empty())   // the cache now holds this build
    if (FILE* f = fopen(stampPath.c_str(), "w")) { fprintf(f, "%s\n", stamp.c_str()); fclose(f); }
  if ((g_shaderCacheMisses > 0 || childMisses > 0) && ctx2 && !g_shaderCacheDir.empty())
    if (FILE* f = fopen((g_shaderCacheDir + "\\compile_time.txt").c_str(), "w")) { fprintf(f, "%.1f\n", compileSecs); fclose(f); }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  pace.begin(stInit);
  introFrame(pace.fraction(), perfFresh ? "Loading your career  |  learning how each aircraft flies (once)" : "Loading your career and the aircraft performance", 1.f);
  game.diskless = tool;   // (diagnostics and other tools: the player's career and settings are never written)
  game.init(false, [&](float f, const std::string& what) { pace.setSub(f); introFrame(pace.fraction(), what, 1.f); });
  if (game.quit) { stopIntro(false); return 0; }
  pace.begin(stTex);
  introFrame(pace.fraction(), "Preparing the renderer: textures, materials and the GPS map", 1.f);
  g_ren.renderScale = 1.0f; g_ren.quality = game.set.quality;
  GetClientRect(g_hwnd, &cr);
  g_ren.matDir = game.assetDir + "\\materials";   // (the scanned material layers shipped beside the exe)
  // These callbacks run on material workers: only the locked tracker is touched,
  // never window messages or GL. The intro's separate context draws the updates.
  g_ren.onMaterialProgress=[&](float f,int done,int total){pace.setSub((4.f+f)/11.f);pace.task("Surface material layers",float(done),float(total));};
  const bool rendererReady=g_ren.init(std::max(64L, cr.right), std::max(64L, cr.bottom), [&](float f, const std::string& what) { pace.clearTask();pace.setSub(f); introFrame(pace.fraction(), what, 1.f); });
  g_ren.onMaterialProgress=nullptr;
  if(!rendererReady) {stopIntro(false);fatal(g_ren.error);return 1;}
  if (game.quit) { stopIntro(false); return 0; }
  // the graphics started: an error.log left by an earlier launch that failed is that launch's, not this one's (the
  // diagnostics collect it, and v3.42.0's still showed v3.36.0's NVIDIA compiler failure, fixed since)
  if (!tool) DeleteFileA((game.saveDir + "\\error.log").c_str());
  {
    // a normal start loads the menu's first place and builds (or reads) every aircraft's meshes, the research jets'
    // too, under the intro (rendered offscreen: the intro keeps the window), so nothing past the menu waits for one;
    // first the helpers' aircraft programs (the prewarm loads them), if they are still at it
    if (afHelpers.started()) {
      pace.begin(stAfShaders);
      afHelpers.wait([&](int done, int items) {
        pace.setDone(stAfShaders, (float)done);
        pace.task("Aircraft shader programs", (float)done, (float)items);
        introFrame(pace.fraction(), fmt("Compiling the aircraft's shader programs side by side  (once after an update)   %d of %d", std::min(done + 1, items), items), 1.f);
        return !game.quit;
      });
      pace.clearTask();
    }
    if (game.quit) { stopIntro(false); return 0; }
    if (!tool) game.prewarm([&](float f, const std::string& what) { introFrame(f, what, 1.f); }, true, &pace, stMenu, &meshSteps, stStage);
    if (!game.quit) { pace.end(); if (!tool && !game.cacheDir.empty()) pace.saveTimes(game.cacheDir + "\\launch_pacing.txt"); }
    if (game.quit) { stopIntro(false); return 0; }
    stopIntro(!tool);   // the bench and shot tools draw straight away; a normal start fades the intro out
  }
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) {
    if (helperSkipped) fprintf(f, "Optional shader-cache preparation skipped unfinished work; main-context program loading completed independently.\n");
    fprintf(f, "Shader cache: %s (%d loaded, %d compiled)\n", g_shaderCacheDir.empty() ? "unavailable" : g_shaderCacheDir.c_str(), g_shaderCacheHits.load(), g_shaderCacheMisses.load());
    if (afHelpers.lastCount)
      fprintf(f, "Aircraft shader programs: %d compiled by %d helper processes side by side, %.1f s from the start (%d items)\n",
              afHelpers.lastBuilt, afHelpers.lastCount, afHelpers.lastSecs, afHelpers.lastItems);
    fprintf(f, "Ground photographs: %s\n", g_ren.matEnvScanned ? (g_ren.envMatCompressed ? "2K, block-compressed (BC3)" : "2K, uncompressed") : g_ren.matEnvFallbackScanned ? "512" : "none (procedural)");
    fprintf(f, "Foveated shading: %s\n", g_ren.vrsAvailable ? "available (NVIDIA variable-rate shading)" : "not available on this card (Auto resolution lowers the whole frame)");
    fprintf(f, "Launch: shaders %.1f s, islands %.1f s (%s), career %.1f s, renderer %.1f s, ",
            pace.tookOf("start"), pace.tookOf("world"), g_world.fromCache ? "from the cache" : "generated", pace.tookOf("career"), pace.tookOf("renderer"));
    if (tool) fprintf(f, "menu, aircraft meshes and the preview stage not prepared (a tool's run: its scenes prepare their own)\n");
    else fprintf(f, "menu %.1f s, aircraft meshes %.1f s (%d built), preview stage %.1f s\n", pace.tookOf("menu"), pace.tookOf("mesh"), g_ren.bakeBuilt, pace.tookOf("stage"));
    if (!g_ren.dispError.empty()) fprintf(f, "Display shader failed (cockpit screens disabled):\n%s\n", g_ren.dispError.c_str());
    if (!g_ren.proxyError.empty()) fprintf(f, "Shadow proxy shader failed (aircraft shadows from the shadow maps only):\n%s\n", g_ren.proxyError.c_str());
    if (!g_shaderNotes.empty()) fprintf(f, "%s", g_shaderNotes.c_str());   // (programs the driver's compiler rejected, and what built instead)
    fclose(f);
  }
  // Every aircraft body (outside and cockpit, the research craft's too) loaded or built before the tools draw a scene,
  // with the shader programs that draw it: the benchmark then never times a body or a program being built, nor a
  // traffic aircraft marched for want of its mesh, and the screenshots show the meshes the game shows. What it took:
  // the bodies ready-built (the release's aircraft folder), from this PC's mesh cache or built from scratch, the
  // packaged ones missing or refused, and the programs compiled for them (each aircraft type's own: compiled the first
  // time it is drawn after an update, then loaded from the cache) and the seconds that took.
  auto buildBodies = [&](BodyLoad& r) {
    // (every Game a tool makes is diskless, as the tool's own: its scenes' crashes, fees and settlements stay in memory.
    // v3.47.0's diagnostics charged the player's career for their break-up scenes: these Games saved to its folder)
    Game* g = new Game();
    g->saveDir = game.saveDir; g->diskless = true;
    g->initHeadless(); g->voicesOff = true; g->iconTex = iconTex;
    const int b0 = g_ren.bakeBuilt, p0 = g_ren.prebuiltMeshHits, m0 = g_ren.prebuiltMeshMisses, c0 = g_ren.meshCacheHits, s0 = g_shaderCacheMisses;
    const long long u0 = g_shaderCompileUs;
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    {   // the aircraft types' own programs, by helpers side by side as the game's launch has them (AircraftShaderHelpers)
      AircraftShaderHelpers helpers;
      helpers.start(g_shaderCacheDir);
      if (helpers.started()) {
        helpers.wait([](int done, int items) {
          SetWindowTextA(g_hwnd, fmt("Somnium Volandi - compiling the aircraft's shader programs side by side   %d of %d", done, items).c_str());
          pumpB(); Sleep(20);
          return true;
        });
        r.helpers = helpers.lastCount; r.helperPrograms = helpers.lastBuilt; r.helperSecs = helpers.lastSecs;
      }
    }
    g_ren.bakeYield = [] { pumpB(); };
    g->prewarm([](float f, const std::string& what) {
      SetWindowTextA(g_hwnd, ("Somnium Volandi - preparing the aircraft " + std::to_string((int)(f * 100.f)) + "%  (" + what + ")").c_str());
      pumpB();
    }, true);
    g_ren.bakeYield = nullptr;
    QueryPerformanceCounter(&t1);
    r.built = g_ren.bakeBuilt - b0; r.secs = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    r.ready = g_ren.prebuiltMeshHits - p0; r.refused = g_ren.prebuiltMeshMisses - m0; r.cached = g_ren.meshCacheHits - c0;
    r.programs = g_shaderCacheMisses - s0; r.programSecs = (g_shaderCompileUs - u0) * 1e-6;
    delete g;
  };
  // a tool's run instead of the game (tool_modes_win32.h): --analyze, --profile, --soak, --bench, --loadshots or --shots,
  // each a function of what the launch made here
  {
    ToolHost host{game, gpu, iconTex, g_hwnd, g_hdc, freq, [] { return g_fullscreen; }, [] { toggleFullscreen(); },
                  s_swapInterval ? std::function<bool(int)>([](int i) { return s_swapInterval(i) != FALSE; }) : std::function<bool(int)>(),
                  [&](BodyLoad& r) { buildBodies(r); }};
    const int code = runToolMode(cmdLine, host);
    if (code >= 0) return code;
  }
  // (the menus render their scenes live: a menu.mp4 left from an older version is not played)
  startAudio();
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) { fprintf(f, "Audio: %s\n%s\n", audioBackendName(), steam::status().c_str()); fclose(f); }
  joyPoll(game.in.joys);   // (the joysticks found: a support aid, like the GPU)
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) {
    fprintf(f, "Joysticks: %d\n", (int)game.in.joys.size());
    for (const JoyState& j : game.in.joys) fprintf(f, "  %s (%d axes, %d buttons, %d hats)\n", j.name.c_str(), j.axes, j.buttons, j.hats);
    fclose(f);
  }

  QueryPerformanceCounter(&prev);
  while (!game.quit) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
    prev = now;
    pollPad(game.in);
    joyPoll(game.in.joys);
    game.filterControllerFocus();
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
      if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
      game.update(dt);
      game.render();
      if (SwapBuffers(g_hdc)) game.framePresented();
      if (game.in.pad) { const bool fg = GetForegroundWindow() == g_hwnd; sendRumble(fg ? game.rumbleLo : 0.f, fg ? game.rumbleHi : 0.f); }
      if (s_limitHz > 0) {   // software limiter: sleep most of the way, spin the last ~2 ms
        static LONGLONG deadline = 0;
        const LONGLONG period = freq.QuadPart / s_limitHz;
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        deadline += period;
        if (deadline < t.QuadPart - period || deadline > t.QuadPart + 2 * period) deadline = t.QuadPart;   // fell behind / first frame: resync
        for (;;) {
          QueryPerformanceCounter(&t);
          LONGLONG left = deadline - t.QuadPart;
          if (left <= 0) break;
          if (left * 1000 > 2 * freq.QuadPart) Sleep(1); else YieldProcessor();
        }
      }
    } else Sleep(16);
    if (game.wantPacing) { game.wantPacing = false; setupPacing(g_hwnd); }
    if (game.wantFullscreenToggle) { game.wantFullscreenToggle = false; toggleFullscreen(); game.set.fullscreen = g_fullscreen; setupPacing(g_hwnd); }
    game.in.endFrame();
  }
  sendRumble(0.f, 0.f);
  game.shutdown();
  steam::shutdown();
  stopAudio();
  timeEndPeriod(1);
  timeEndPeriod(1);
  return 0;
}
