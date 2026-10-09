// Solace Express - Windows platform layer: window, OpenGL context, input, audio output
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <shellapi.h>
#include <objbase.h>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "game.h"
#include "load_pacer.h"

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

static Game* g_game = nullptr;
static HWND g_hwnd = nullptr;
static HDC g_hdc = nullptr;
static bool g_fullscreen = false;
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
    case WM_SIZE: if (g_ren.ok) g_ren.resize(LOWORD(lp), HIWORD(lp)); return 0;
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
    case WM_KILLFOCUS:   // switched away: nothing stays held, and a flight in progress pauses
      if (in) { memset(in->down, 0, sizeof(in->down)); for (int b = 0; b < 3; b++) { if (in->mDown[b]) in->mReleased[b] = true; in->mDown[b] = false; } }
      if (g_game) g_game->focusLost();
      break;
    case WM_MOUSEMOVE: if (in) { float x = (float)(short)LOWORD(lp), y = (float)(short)HIWORD(lp); in->mdx += x - in->mx; in->mdy += y - in->my; in->mx = x; in->my = y; } return 0;
    case WM_LBUTTONDOWN: if (in) { in->mDown[0] = in->mPressed[0] = true; SetCapture(h); } return 0;
    case WM_LBUTTONUP: if (in) { in->mDown[0] = false; in->mReleased[0] = true; ReleaseCapture(); } return 0;
    case WM_RBUTTONDOWN: if (in) { in->mDown[1] = in->mPressed[1] = true; SetCapture(h); } return 0;
    case WM_RBUTTONUP: if (in) { in->mDown[1] = false; in->mReleased[1] = true; ReleaseCapture(); } return 0;
    case WM_MBUTTONDOWN: if (in) in->mDown[2] = in->mPressed[2] = true; return 0;
    case WM_MBUTTONUP: if (in) { in->mDown[2] = false; in->mReleased[2] = true; } return 0;
    case WM_MOUSEWHEEL: if (in) in->wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.f; return 0;
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
  XState st = {};
  if (s_xinput(0, &st) != 0) {   // disconnected: nothing it was holding stays held
    if (in.pad) { in.buttons = 0; in.lx = in.ly = in.rx = in.ry = in.lt = in.rt = 0; }
    in.pad = false; return;
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
  std::string d = std::string(buf) + "\\SolaceExpress";
  // the game used to be called Air Xpress: carry its saves, settings, stations and shader cache over
  std::string old = std::string(buf) + "\\AirXpress";
  if (GetFileAttributesA(d.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES)
    if (!MoveFileExA(old.c_str(), d.c_str(), 0)) return old;   // in use or locked: keep using it as it is
  CreateDirectoryA(d.c_str(), nullptr);
  return d;
}

// Child process: SolaceExpress.exe --build-shader-cache "<dir>" compiles every program into the binary cache and
// exits, reporting progress on stdout ("<programs done>" lines, then "m <programs compiled>"). The game runs the long
// first-time compile this way because NVIDIA's driver holds a process-wide lock while it links the big scene programs: no
// thread of the compiling process, whatever its context, can draw until it lets go, so the intro froze. A separate
// process has its own driver state, and the game then loads the finished programs from the cache in a moment.
static int buildShaderCacheChild(HINSTANCE hInst, const std::string& dir) {
  WNDCLASSEXW wc = {sizeof(wc)}; wc.style = CS_OWNDC; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hInst; wc.lpszClassName = L"SolaceExpressCompile";
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
  HGLRC ctx = createAttribs ? createAttribs(dc, nullptr, attrs) : nullptr;
  if (!ctx) return 2;
  wglMakeCurrent(dc, ctx); wglDeleteContext(legacy);
  g_opengl32 = LoadLibraryA("opengl32.dll");
  if (!glLoad(wglProc, nullptr)) return 3;
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

// the benchmark loops: the window answers its messages between frames (no "Not Responding" on a slow scene)
static void pumpB() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--build-shader-cache \"");
    if (k != std::string::npos) {
      size_t b = k + 22, e = cl.find('"', b);
      return buildShaderCacheChild(hInst, cl.substr(b, e == std::string::npos ? std::string::npos : e - b));
    }
  }
  SetProcessDPIAware();
  timeBeginPeriod(1);
  static Game game;
  g_game = &game; s_pacingGame = &game;
  game.saveDir = userDir();
  { char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);   // pictures etc. live next to the exe
    std::string d(exe, n); size_t sl = d.find_last_of("\\/"); game.assetDir = sl == std::string::npos ? std::string(".") : d.substr(0, sl); }

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndProc; wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
  wc.lpszClassName = L"SolaceExpressWnd";
  RegisterClassExW(&wc);
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  int ww = std::min(1600, sw * 4 / 5), wh = ww * 9 / 16;
  RECT r = {0, 0, ww, wh};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Solace Express", WS_OVERLAPPEDWINDOW, (sw - (r.right - r.left)) / 2, (sh - (r.bottom - r.top)) / 2,
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
  if (!ctx) { MessageBoxA(g_hwnd, "Solace Express needs an OpenGL 3.3 capable graphics driver.\nPlease update your graphics drivers.", "Solace Express", MB_ICONERROR); return 1; }
  wglMakeCurrent(g_hdc, ctx);
  wglDeleteContext(legacy);
  g_opengl32 = LoadLibraryA("opengl32.dll");
  const char* missing = nullptr;
  if (!glLoad(wglProc, &missing)) { MessageBoxA(g_hwnd, (std::string("Missing OpenGL function: ") + missing).c_str(), "Solace Express", MB_ICONERROR); return 1; }
  s_swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
  if (auto ext = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT")) { const char* e = ext(); s_tear = e && strstr(e, "WGL_EXT_swap_control_tear"); }
  timeBeginPeriod(1);   // 1 ms Sleep granularity for the frame limiter
  setupPacing(g_hwnd);
  const char* xdlls[] = {"xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll"};
  for (auto d : xdlls) { HMODULE m = LoadLibraryA(d); if (m) { s_xinput = (PFNXINPUTGETSTATE)GetProcAddress(m, "XInputGetState"); if (s_xinput) break; } }

  ShowWindow(g_hwnd, SW_SHOW);
  game.loadSettings();   // early, for fullscreen during the intro (Game::init loads them again)
  if (game.set.fullscreen) { toggleFullscreen(); setupPacing(g_hwnd); }
  game.platformPresent = [] { SwapBuffers(g_hdc); pumpB(); };   // (a frame shown from inside a long bake: the research terminal's boot screen)

  // startup.log names the GPU in use (support aid)
  std::string gpu = std::string((const char*)glGetString(GL_RENDERER)) + " / " + (const char*)glGetString(GL_VERSION);
  // the scene programs sample up to 31 texture units in one fragment shader; OpenGL 3.3 only guarantees 16 (every current GPU has 32)
  GLint texUnits = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texUnits);
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "w")) { fprintf(f, "GPU: %s\nFragment texture units: %d\n", gpu.c_str(), (int)texUnits); fclose(f); }
  if (texUnits > 0 && texUnits < 22) {
    MessageBoxA(g_hwnd, ("This graphics driver offers " + std::to_string(texUnits) + " texture units per shader; Solace Express needs 22.\n"
                         "Please update the graphics driver, or run the game on the dedicated GPU.").c_str(), "Solace Express", MB_ICONERROR);
    return 1;
  }
  bool cached = false;
  {   // compiled shader programs are cached next to the game (or with the save data if that folder is read-only)
    char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = std::string(exe, n);
    size_t sl = dir.find_last_of("\\/");
    dir = (sl == std::string::npos ? std::string(".") : dir.substr(0, sl)) + "\\shadercache";
    auto writable = [](const std::string& d) {
      CreateDirectoryA(d.c_str(), nullptr);
      std::string probe = d + "\\.probe";
      FILE* f = fopen(probe.c_str(), "wb"); if (!f) return false;
      fclose(f); remove(probe.c_str()); return true;
    };
    if (!writable(dir)) { dir = game.saveDir + "\\shadercache"; if (!writable(dir)) dir.clear(); }
    g_shaderCacheDir = dir;
    game.cacheDir = dir;
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
    MessageBoxA(g_hwnd, ("Graphics initialisation failed:\n" + what.substr(0, 1500)).c_str(), "Solace Express", MB_ICONERROR);
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
  WNDCLASSEXW wc2 = {sizeof(wc2)}; wc2.style = CS_OWNDC; wc2.lpfnWndProc = DefWindowProcW; wc2.hInstance = hInst; wc2.lpszClassName = L"SolaceExpressGL";
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
  const bool tool = cmdLine.find("--bench ") != std::string::npos || cmdLine.find("--shots ") != std::string::npos || cmdLine.find("--profile ") != std::string::npos || cmdLine.find("--analyze") != std::string::npos || cmdLine.find("--loadshots") != std::string::npos;
  auto exists = [](const std::string& p) { return !p.empty() && GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
  g_ren.checkMeshCache();
  const bool perfFresh = game.cacheDir.empty() || !exists(game.cacheDir + "\\perf.bin");
  LoadPacer pace;
  const int stStart = pace.add("start", Renderer::kProgramCount + 1);   // (each shader program built or loaded, and the islands)
  const int stInit = pace.add("career");
  const int stTex = pace.add("renderer");
  const int stMenu = pace.add("menu");
  std::vector<int> meshSteps;
  if (!tool)
    for (const auto& it : Game::prewarmItems(true)) meshSteps.push_back(pace.add("mesh" + std::to_string(it.first) + (it.second ? "c" : "o")));
  pace.begin(stStart);
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
        childReader = std::thread([rd, &childDone, &childMisses, &childStageMu, &childStage] {
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
        });
      } else CloseHandle(rd);
      CloseHandle(wr);   // the child holds its own copy: the reader sees the end of the pipe when the child exits
    }
  }
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
  intro.th = std::thread([&] {
    HDC dcI = GetDC(g_hwnd);
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    HGLRC ctxI = createAttribs ? createAttribs(dcI, nullptr, attrs) : nullptr;
    if (!ctxI || !wglMakeCurrent(dcI, ctxI)) { if (ctxI) wglDeleteContext(ctxI); intro.state = -1; return; }
    if (s_swapInterval) s_swapInterval(1);   // (the intro runs on vsync)
    std::unique_ptr<Renderer> R(new Renderer());
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (!R->initUI(std::max(64L, rc.right), std::max(64L, rc.bottom))) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctxI); intro.state = -1; return; }
    GLuint ic = iconPx.empty() ? 0 : R->makeTexture(iconPx.data(), 256, 256);
    int expected = 0; intro.state.compare_exchange_strong(expected, 1);
    float shown = 0, fade = 1.f;
    for (;;) {
      int st = intro.state;
      if (st == 3) break;
      if (st == 2) { fade -= 1.f / 20.f; if (fade <= 0.f) break; }
      LARGE_INTEGER n; QueryPerformanceCounter(&n);
      float t = (float)(n.QuadPart - t0.QuadPart) / freq.QuadPart;
      LoadPacer* lp = intro.pacer;
      float target = lp ? lp->fraction() : (float)intro.target;
      shown = std::max(shown, shown + (target - shown) * 0.12f);
      std::string stage; { std::lock_guard<std::mutex> lk(intro.m); stage = intro.stage; }
      GetClientRect(g_hwnd, &rc);
      if (rc.right > 0 && rc.bottom > 0) { R->W = rc.right; R->H = rc.bottom; }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, R->W, R->H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
      R->uiBegin(); game.drawIntro(shown, stage, t, ic, fade, *R); R->uiEnd();
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
  float shownMain = 0;
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
    shownMain = std::max(shownMain, shownMain + (target - shownMain) * 0.12f);
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) { g_ren.W = rc.right; g_ren.H = rc.bottom; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_ren.W, g_ren.H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    g_ren.uiBegin(); game.drawIntro(shownMain, stage, t, iconTex, fade); g_ren.uiEnd();
    SwapBuffers(g_hdc);
    if (!s_swapInterval || s_limitHz > 0) Sleep(14);
    return t;
  };
  // ends the intro: `fade` plays its fade-out first; afterwards this thread owns the window's drawing again
  auto stopIntro = [&](bool fade) {
    if (!introThreaded) { if (fade) for (int i = 0; i < 20; i++) introFrame(1.f, "READY", 1.f - i / 20.f); return; }
    if (intro.state != 1) return;
    if (fade) { intro.pacer = nullptr; intro.target = 1.f; { std::lock_guard<std::mutex> lk(intro.m); intro.stage = "Ready"; } }
    intro.state = fade ? 2 : 3;
    while (intro.state != 4) {   // keep the window responsive while it finishes
      MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
      Sleep(5);
    }
    intro.th.join();
  };
  for (;;) {
    QueryPerformanceCounter(&now);
    float t = (float)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
    int d = childDone >= 0 && compileState == 0 ? std::max((int)childDone, (int)progDone) : (int)progDone;
    bool compiled = !ctx2 || compileState != 0, built = worldDone;
    if (d == 0 && compileState == 0) compileSecs = t;
    // what is being done, by name: the shader program (the child process compiling on a first launch reports a count only)
    std::string stage;
    if (!ctx2) stage = "Preparing";
    else if (compileState == 0) {
      std::string sh = g_ren.compileStage();
      if (childDone >= 0 && child.hProcess && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) { std::lock_guard<std::mutex> lk(childStageMu); sh = childStage; }   // (the child compiling: what it reports)
      stage = cacheCurrent ? "Loading the shaders from the cache" : "Compiling the shaders (the first launch of this version only)";
      if (!sh.empty()) stage += ": " + sh;
      stage += "   " + std::to_string(std::min(d + 1, Renderer::kProgramCount)) + " of " + std::to_string(Renderer::kProgramCount);
    } else stage = "Shaders ready";
    if (!built) stage += g_worldStage == 1 ? "   |   Loading the islands from the cache" : "   |   Generating the islands (once: kept for the next launch)";
    pace.setDone(stStart, (float)std::min(d, Renderer::kProgramCount) + (built ? 1.f : 0.f));   // (the programs done, and the islands)
    introFrame(pace.fraction(), stage, 1.f);
    if (game.quit) break;
    if (compiled && built && t > 3.2f) break;   // the logo stays up long enough to be seen
  }
  if (game.quit && child.hProcess) TerminateProcess(child.hProcess, 9);   // closed during the intro: don't wait for it
  if (compileThread.joinable()) compileThread.join();
  if (child.hProcess) { CloseHandle(child.hProcess); CloseHandle(child.hThread); }
  worldThread.join();
  if (ctx2) wglDeleteContext(ctx2);
  if (dc2) ReleaseDC(hw2, dc2);
  if (hw2) DestroyWindow(hw2);
  if (game.quit) { stopIntro(false); return 0; }
  if (ctx2 && compileState != 1) { stopIntro(false); fatal(g_ren.error); return 1; }
  if (!ctx2) introFrame(pace.fraction(), cached ? "Loading the shaders from the cache" : "Compiling the shaders (this can take a minute)", 1.f);
  if (ctx2 && compileState == 1 && !stampPath.empty() && !cacheCurrent && g_ren.dispError.empty())   // the cache now holds this build
    if (FILE* f = fopen(stampPath.c_str(), "w")) { fprintf(f, "%s\n", stamp.c_str()); fclose(f); }
  if ((g_shaderCacheMisses > 0 || childMisses > 0) && ctx2 && !g_shaderCacheDir.empty())
    if (FILE* f = fopen((g_shaderCacheDir + "\\compile_time.txt").c_str(), "w")) { fprintf(f, "%.1f\n", compileSecs); fclose(f); }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  pace.begin(stInit);
  introFrame(pace.fraction(), perfFresh ? "Loading your career  |  learning how each aircraft flies (once)" : "Loading your career and the aircraft performance", 1.f);
  game.init(false);
  pace.begin(stTex);
  introFrame(pace.fraction(), "Preparing the renderer: textures, materials and the GPS map", 1.f);
  g_ren.renderScale = 1.0f; g_ren.quality = game.set.quality;
  GetClientRect(g_hwnd, &cr);
  g_ren.matDir = game.assetDir + "\\materials";   // (the scanned material layers shipped beside the exe)
  if (!g_ren.init(std::max(64L, cr.right), std::max(64L, cr.bottom))) { stopIntro(false); fatal(g_ren.error); return 1; }
  {
    // a normal start loads the menu's first place and builds (or reads) every aircraft's meshes, the research jets'
    // too, under the intro (rendered offscreen: the intro keeps the window), so nothing past the menu waits for one
    if (!tool) game.prewarm([&](float f, const std::string& what) { introFrame(f, what, 1.f); }, true, &pace, stMenu, &meshSteps);
    pace.end();
    if (game.quit) { stopIntro(false); return 0; }
    stopIntro(!tool);   // the bench and shot tools draw straight away; a normal start fades the intro out
  }
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) {
    fprintf(f, "Shader cache: %s (%d loaded, %d compiled)\n", g_shaderCacheDir.empty() ? "unavailable" : g_shaderCacheDir.c_str(), g_shaderCacheHits.load(), g_shaderCacheMisses.load());
    fprintf(f, "Launch: shaders and islands %.1f s (islands %s), career %.1f s, renderer %.1f s, menu %.1f s, aircraft meshes %.1f s (%d built)\n",
            pace.tookOf("start"), g_world.fromCache ? "from the cache" : "generated", pace.tookOf("career"), pace.tookOf("renderer"), pace.tookOf("menu"), pace.tookOf("mesh"), g_ren.bakeBuilt);
    if (!g_ren.dispError.empty()) fprintf(f, "Display shader failed (cockpit screens disabled):\n%s\n", g_ren.dispError.c_str());
    if (!g_ren.proxyError.empty()) fprintf(f, "Shadow proxy shader failed (aircraft shadows from the shadow maps only):\n%s\n", g_ren.proxyError.c_str());
    if (!g_shaderNotes.empty()) fprintf(f, "%s", g_shaderNotes.c_str());   // (programs the driver's compiler rejected, and what built instead)
    fclose(f);
  }
  // Every aircraft body (outside and cockpit, the research craft's too) built or loaded before the tools draw a scene:
  // the benchmark then never times a body being built, nor a traffic aircraft marched for want of its mesh, and the
  // screenshots show the meshes the game shows. Returns how many were built from scratch and the seconds it took.
  auto buildBodies = [&](int& built, double& secs) {
    Game* g = new Game();
    g->saveDir = game.saveDir;
    g->initHeadless(); g->iconTex = iconTex;
    const int b0 = g_ren.bakeBuilt;
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    g_ren.bakeYield = [] { pumpB(); };
    g->prewarm([](float f, const std::string& what) {
      SetWindowTextA(g_hwnd, ("Solace Express - building the aircraft bodies " + std::to_string((int)(f * 100.f)) + "%  (" + what + ")").c_str());
      pumpB();
    }, true);
    g_ren.bakeYield = nullptr;
    QueryPerformanceCounter(&t1);
    built = g_ren.bakeBuilt - b0; secs = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    delete g;
  };
  // Analysis: SolaceExpress.exe --analyze [scenes] - a thorough look at where the frame time goes, written to
  // analysis.txt (with a picture of each scene in the "analysis" folder), all at 1920x1080: CPU vs GPU (update /
  // submit / wait), exact per-pass GPU times (the GPU is waited on at each pass boundary), frame time against render
  // resolution (does it scale with pixel count?) and what each feature costs.
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--analyze");
    if (k != std::string::npos) {
      std::string list = "menu,air,storm,night,cockpit,rjet,rjetc,wr_8_0_0_0_1,wr_8_0_-8_0_1_3_2_22.5_2";   // (the last: the XR-40 at night in a storm, hovering over Solace Capital - a scene that holds still: each measurement here is taken a few seconds after the last)
      if (cl.size() > k + 10 && cl[k + 9] == ' ' && cl[k + 10] != '-') { list = cl.substr(k + 10); list = list.substr(0, list.find(' ')); }
      list += ",";
      if (g_fullscreen) toggleFullscreen();
      RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
      SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
      if (s_swapInterval) s_swapInterval(0);
      char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
      CreateDirectoryA((dir + "\\analysis").c_str(), nullptr);
      FILE* af = fopen((dir + "\\analysis.txt").c_str(), "w");
      if (!af) return 1;
      { int built = 0; double secs = 0; buildBodies(built, secs); }   // (as the game's launch does: the traffic drawn from its meshes, as in flight)
      fprintf(af, "Solace Express performance analysis\nGPU: %s\nCPU threads: %u   Quality: %d   Window: %dx%d\n",
              gpu.c_str(), std::thread::hardware_concurrency(), g_ren.quality, g_ren.W, g_ren.H);
      fprintf(af, "\nHow to read this: 'ms' is wall-clock time per frame with the GPU finished (vsync off). Per-pass times are\n"
                  "serialized CPU wall times including submit + GPU waits (not isolated GPU durations). Resolution scaling: if a frame\n"
                  "takes ~2.2x as long at 100%% as at 67%% (2.2x the pixels), the per-pixel work is the bottleneck.\n");
      auto qpcMs = [&](LARGE_INTEGER a, LARGE_INTEGER b) { return (double)(b.QuadPart - a.QuadPart) / freq.QuadPart * 1000.0; };
      auto pump = [&] { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } };
      static const char* kPassName[Renderer::kPasses] = {"world+terrain shadow", "displays", "camera feeds", "objects (airframes)", "airframe shadow proxy", "lighting+clouds+effects", "TAA", "sprites", "bloom", "light shafts", "composite"};
      static const struct { int bit; const char* name; } kFeat[] = {
        {1, "volumetric clouds"}, {2, "terrain shadows"}, {4, "scenery shadow maps"}, {8, "aircraft shadow"}, {16, "point lights"},
        {32, "cloud shadows"}, {64, "half terrain march steps"}, {128, "terrain materials"}, {256, "fog / aerial perspective"},
      };
      struct Summary { std::string sc; double ms, rt, scale; std::string top; };
      std::vector<Summary> sums;
      for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
        std::string sc = list.substr(a, b - a);
        if (sc.empty()) continue;
        pump();
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        SetWindowTextA(g_hwnd, ("Solace Express - analysing " + sc).c_str());
        g_ren.entSync = true;
        Game* g = new Game();
        g->saveDir = game.saveDir;
        g->initHeadless(); g->iconTex = iconTex; g->debugScene(sc);
        g_ren.entSync = false;
        auto frames = [&](int n) { for (int i = 0; i < n; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); } };
        auto timed = [&](int n) { frames(8); glFinish(); LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0); frames(n); glFinish(); QueryPerformanceCounter(&t1); return qpcMs(t0, t1) / n; };
        frames(30);
        fprintf(af, "\n==================== %s ====================\n", sc.c_str());
        // 1. CPU vs GPU
        {
          double tu = 0, tr = 0, tw = 0; const int N = 30;
          glFinish();
          for (int i = 0; i < N; i++) {
            LARGE_INTEGER t0, t1, t2, t3;
            QueryPerformanceCounter(&t0); g->update(1.f / 60.f);
            QueryPerformanceCounter(&t1); g->render();
            QueryPerformanceCounter(&t2); glFinish();
            QueryPerformanceCounter(&t3); SwapBuffers(g_hdc);
            tu += qpcMs(t0, t1); tr += qpcMs(t1, t2); tw += qpcMs(t2, t3);
          }
          tu /= N; tr /= N; tw /= N;
          fprintf(af, "Frame: %.2f ms (%.1f fps)   CPU update %.2f ms + CPU render/submit %.2f ms, then waiting for the GPU %.2f ms\n",
                  tu + tr + tw, 1000.0 / (tu + tr + tw), tu, tr, tw);
          fprintf(af, "  -> %s\n", tw > tu + tr ? "GPU-bound: the CPU finishes long before the GPU" : "CPU-bound or balanced: the CPU side takes as long as the GPU");
          fprintf(af, "Scenery: %d instances drawn from %d chunks, %.2f ms CPU to gather   Render resolution %dx%d\n",
                  g_ren.entDrawn, g_ren.entChunks, g_ren.entCpuMs, (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale));
        }
        // 2. exact per-pass GPU times
        double passSum[Renderer::kPasses] = {};
        {
          g_ren.syncTiming = true;
          const int N = 20;
          frames(3);
          for (int i = 0; i < N; i++) { frames(1); for (int p = 0; p < Renderer::kPasses; p++) passSum[p] += g_ren.passWall[p] / N; }
          g_ren.syncTiming = false;
          double tot = 0; for (double v : passSum) tot += v;
          fprintf(af, "Serialized pass wall times (CPU submit + GPU wait, %.2f ms total):\n", tot);
          for (int p = 0; p < Renderer::kPasses; p++) fprintf(af, "  %-26s %7.2f ms  %5.1f%%\n", kPassName[p], passSum[p], tot > 0 ? passSum[p] / tot * 100.0 : 0.0);
          // the same passes from the GPU's own timestamps, the frames running as in play (nothing waited for)
          for (float& m : g_ren.passMs) m = 0.f;
          frames(60);
          double gtot = 0; for (float v : g_ren.passMs) gtot += v;
          fprintf(af, "GPU pass times (timestamp queries, frames not waited for: %.2f ms total):\n", gtot);
          for (int p = 0; p < Renderer::kPasses; p++) fprintf(af, "  %-26s %7.2f ms  %5.1f%%\n", kPassName[p], g_ren.passMs[p], gtot > 0 ? g_ren.passMs[p] / gtot * 100.0 : 0.0);
        }
        // 3. resolution scaling
        double ms100 = 0, ms67 = 0;
        {
          static const struct { float scale; const char* name; } kRes[] = {{1.f, "100%"}, {0.85f, " 85%"}, {0.75f, " 75%"}, {0.67f, " 67%"}};
          fprintf(af, "Render resolution (the passes + TAA at a fraction of 1920x1080, upscaled):\n");
          for (const auto& R : kRes) {
            g_ren.setRenderScale(R.scale); frames(4);
            double ms = timed(30);
            if (R.scale == 1.f) ms100 = ms;
            if (R.scale < 0.7f) ms67 = ms;
            fprintf(af, "  %s  %7.2f ms  (%5.1f fps)   pixels x%.2f\n", R.name, ms, 1000.0 / ms, R.scale * R.scale);
          }
          g_ren.setRenderScale(1.f); frames(4);
          double r = ms67 > 0 ? ms100 / ms67 : 0;
          fprintf(af, "  -> 100%% takes %.2fx as long as 67%% (2.23x the pixels): %s\n", r,
                  r > 1.8 ? "per-pixel work dominates" : r > 1.35 ? "mostly per-pixel, with a fixed cost besides" : "a fixed per-frame cost dominates (not pixel work)");
        }
        // 4. features
        std::string top; double topSave = 0;
        {
          double base = timed(30);
          fprintf(af, "Features (frame time with it switched off; base %.2f ms):\n", base);
          for (const auto& F : kFeat) {
            g_ren.dbgOff = F.bit;
            double ms = timed(30);
            g_ren.dbgOff = 0;
            fprintf(af, "  %-26s %7.2f ms   saves %6.2f ms (%4.1f%%)\n", F.name, ms, base - ms, (base - ms) / base * 100.0);
            if (base - ms > topSave) { topSave = base - ms; top = F.name; }
          }
          // 5. where the frame goes: one piece of the work left out at a time (Renderer::kProbe*: the picture is wrong
          // while one is, and these are not settings)
          static const struct { int bit; const char* name; } kProbe[] = {
            {Renderer::kProbeScenery, "scenery (buildings, trees)"}, {Renderer::kProbeTerrain, "terrain"},
            {Renderer::kProbeMarch, "airframe march"}, {Renderer::kProbeMeshShade, "own airframe's shading"},
          };
          fprintf(af, "Work (frame time with it left out; base %.2f ms):\n", base);
          for (const auto& P : kProbe) {
            g_ren.dbgOff = P.bit;
            double ms = timed(30);
            g_ren.dbgOff = 0;
            fprintf(af, "  %-26s %7.2f ms   costs %6.2f ms (%4.1f%%)\n", P.name, ms, base - ms, (base - ms) / base * 100.0);
          }
        }
        // a picture of the scene for reference
        frames(3); glFinish();
        g_ren.screenshotPNG((dir + "\\analysis\\" + sc + ".png").c_str());
        double rt = passSum[3];   // (the objects pass: the airframes)
        sums.push_back({sc, ms100, rt, ms67 > 0 ? ms100 / ms67 : 0, top});
        fflush(af);
        delete g;
      }
      fprintf(af, "\n==================== summary ====================\n");
      fprintf(af, "%-16s %9s %9s %12s  %s\n", "scene", "frame ms", "objects", "100%/67%", "most expensive feature");
      for (auto& S : sums) fprintf(af, "%-16s %9.2f %9.2f %12.2f  %s\n", S.sc.c_str(), S.ms, S.rt, S.scale, S.top.c_str());
      fclose(af);
      return 0;
    }
  }
  // Profile: SolaceExpress.exe --profile scene1,scene2,... renders each scene at 1920x1080 once normally and once with
  // each feature switched off (Renderer::dbgOff), and writes profile.txt next to the exe: what every
  // feature costs on this GPU
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--profile ");
    if (k != std::string::npos) {
      std::string list = cl.substr(k + 10); list = list.substr(0, list.find(' ')) + ",";
      if (g_fullscreen) toggleFullscreen();
      RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
      SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
      if (s_swapInterval) s_swapInterval(0);
      char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
      FILE* pf = fopen((dir + "\\profile.txt").c_str(), "w");
      static const struct { int bit; const char* name; } kFeat[] = {
        {0, "everything on"}, {1, "volumetric clouds"}, {2, "terrain shadows"}, {4, "scenery shadow maps"}, {8, "aircraft shadow"},
        {16, "point lights"}, {32, "cloud shadows"}, {64, "half terrain march steps"}, {128, "terrain materials"}, {256, "fog / aerial perspective"},
      };
      if (pf) fprintf(pf, "GPU: %s\nRender %dx%d, quality %d. Each line: frame time with that feature off, and what it saves.\n", gpu.c_str(), g_ren.W, g_ren.H, g_ren.quality);
      g_ren.entSync = true;
      for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
        std::string sc = list.substr(a, b - a);
        if (sc.empty()) continue;
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        if (pf) fprintf(pf, "\n%s\n", sc.c_str());
        double base = 0;
        for (const auto& F : kFeat) {
          MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
          SetWindowTextA(g_hwnd, ("Solace Express - profiling " + sc + ": " + F.name).c_str());
          Game* g = new Game();
          g->saveDir = game.saveDir;
          g->initHeadless(); g->iconTex = iconTex; g->debugScene(sc);
          g_ren.entSync = false; g_ren.dbgOff = F.bit;
          for (int i = 0; i < 30; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); }
          glFinish();
          LARGE_INTEGER f0, f1; QueryPerformanceCounter(&f0);
          const int N = 60;
          for (int i = 0; i < N; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); pumpB(); }
          glFinish(); QueryPerformanceCounter(&f1);
          double ms = (double)(f1.QuadPart - f0.QuadPart) / freq.QuadPart * 1000.0 / N;
          if (F.bit == 0) base = ms;
          if (pf) {
            if (F.bit == 0) fprintf(pf, "  %-26s %7.2f ms   (world %.2f, objects %.2f, shadow proxy %.2f, lighting %.2f)\n", F.name, ms, g_ren.passMs[0], g_ren.passMs[3], g_ren.passMs[4], g_ren.passMs[5]);
            else fprintf(pf, "  %-26s %7.2f ms   saves %6.2f ms\n", F.name, ms, base - ms);
            fflush(pf);
          }
          g_ren.dbgOff = 0; g_ren.entSync = true;
          delete g;
        }
      }
      if (pf) fclose(pf);
      return 0;
    }
  }
  // Benchmark: SolaceExpress.exe --bench scene1,scene2,... [--size WxH] times each scene (wall clock with the GPU flushed,
  // plus the GPU time of every pass) and writes bench.txt next to the exe. Optional --bench-frames N
  // and --bench-csv add long runs and raw CPU/app-present/fresh delayed GPU samples; no per-frame waits.
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--bench ");
    if (k != std::string::npos) {
      std::string list = cl.substr(k + 8);
      if (!list.empty() && list[0] == '"') { list = list.substr(1); list = list.substr(0, list.find('"')); }
      else list = list.substr(0, list.find(' '));
      std::vector<std::string> scenes; std::string sceneError;
      if (!benchmark::scenes(list, scenes, sceneError)) { MessageBoxA(g_hwnd, sceneError.c_str(), "Invalid benchmark scenes", MB_ICONERROR); return 2; }
      int sampleFrames = 120; size_t kf = cl.find("--bench-frames ");
      if (kf != std::string::npos) {
        char* end = nullptr; const char* first = cl.c_str() + kf + 15;
        long nFrames = strtol(first, &end, 10);
        if (end == first || (*end && *end != ' ') || nFrames < 2 || nFrames > 1000000) {
          MessageBoxA(g_hwnd, "--bench-frames must be an integer from 2 to 1000000.", "Invalid benchmark length", MB_ICONERROR); return 2;
        }
        sampleFrames = (int)nFrames;
      }
      const bool writeCsv = cl.find("--bench-csv") != std::string::npos;
      int sw2 = 0, sh2 = 0; size_t kz = cl.find("--size ");
      if (kz != std::string::npos) sscanf(cl.c_str() + kz + 7, "%dx%d", &sw2, &sh2);
      if (kz != std::string::npos && cl.compare(kz + 7, 6, "native") == 0) { if (!g_fullscreen) toggleFullscreen(); }   // the whole desktop
      else if (sw2 > 64 && sh2 > 64) {
        if (g_fullscreen) toggleFullscreen();
        if (cl.find("--fullscreen") != std::string::npos) {   // borderless at the monitor's origin, exactly the requested size (1920x1080 fills a 1080p screen)
          SetWindowLong(g_hwnd, GWL_STYLE, (GetWindowLong(g_hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
          SetWindowPos(g_hwnd, HWND_TOP, 0, 0, sw2, sh2, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        } else {
          RECT wr = {0, 0, sw2, sh2}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
          SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
        }
      }
      { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
      { RECT rc; GetClientRect(g_hwnd, &rc); if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom); }
      const bool swapOffAccepted = s_swapInterval && s_swapInterval(0);   // request unlocked; driver/compositor may still pace
      char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
      std::string outName = "bench.txt"; size_t ko = cl.find("--out ");
      if (ko != std::string::npos) {
        outName = cl.substr(ko + 6);
        if (!outName.empty() && outName[0] == '"') { outName = outName.substr(1); outName = outName.substr(0, outName.find('"')); }   // (a quoted path)
        else outName = outName.substr(0, outName.find(' '));
      }
      const bool absoluteOut = (outName.size() > 1 && outName[1] == ':') || outName.rfind("\\\\", 0) == 0;
      const std::string outputPath = absoluteOut ? outName : dir + "\\" + outName;
      FILE* bf = fopen(outputPath.c_str(), "w");
      if (!bf) { MessageBoxA(g_hwnd, "Cannot open benchmark output file.", "Benchmark output", MB_ICONERROR); return 2; }
      FILE* csv = writeCsv ? fopen((outputPath + ".frames.csv").c_str(), "w") : nullptr;
      if (writeCsv && !csv) { fclose(bf); MessageBoxA(g_hwnd, "Cannot open benchmark CSV output file.", "Benchmark output", MB_ICONERROR); return 2; }
      if (csv) fprintf(csv, "scene,frame_index,render_frame_serial,frame_wall_ms,present_interval_ms,update_ms,render_submit_ms,swapbuffers_ms,pump_ms,gpu_sample_id,gpu_sample_origin_frame,gpu_render_scene_ms,display_width,display_height,render_width,render_height,render_scale,quality,scenery_pending,terrain_shadow_pending,bake_count,native_1080p_valid,scene_status,frame_start_qpc,present_start_qpc\n");
      if (bf) fprintf(bf, "GPU: %s\nDesktop %dx%d, render %dx%d, quality %d\n\n", gpu.c_str(), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), g_ren.W, g_ren.H, g_ren.quality);
      fprintf(bf, "Benchmark samples: %d; fixed simulation dt 1/60 s; QPC frequency %lld Hz\n", sampleFrames, (long long)freq.QuadPart);
      fprintf(bf, "Pacing: WGL swap interval 0 requested (%s); software limiter NOT used. Driver/compositor overrides remain possible.\n", swapOffAccepted ? "accepted" : "unconfirmed");
      fprintf(bf, "Frame wall includes CPU work, driver blocking, SwapBuffers and message pump. App present intervals are NOT display intervals.\n"
                  "Batch average includes one final GPU drain; per-frame samples do not force GPU completion.\n"
                  "GPU samples are raw delayed renderScene queries, excluding later UI/presentation; per-pass snapshots remain EMA diagnostics.\n"
                  "Percentiles use nearest rank; 1%% low = reciprocal mean of the slowest ceil(1%% * sample count) intervals.\n");
      auto qpcMs = [&](LARGE_INTEGER x, LARGE_INTEGER y) { return (double)(y.QuadPart - x.QuadPart) * 1000.0 / freq.QuadPart; };
      auto printSummary = [&](const char* label, const std::vector<double>& values, bool frameRate) {
        const auto m = benchmark::summarize(values);
        if (!m.count) { fprintf(bf, "  %s: n=0 unavailable\n", label); return; }
        fprintf(bf, "  %s: n=%zu min=%.3f median=%.3f p95=%.3f p99=%.3f max=%.3f mean=%.3f ms", label, m.count, m.min, m.median, m.p95, m.p99, m.max, m.mean);
        if (frameRate && m.count) fprintf(bf, " 1%%low=%.2f fps >16.667ms=%zu/%zu", m.low1, m.overBudget, m.count);
        fprintf(bf, "\n");
      };
      auto printWeather = [&](Game* g, const char* phase) {
        const Weather& w = g->benchmarkWeather();
        fprintf(bf, "  weather %s: cover=%.4f base=%.2f precip=%d storm=%d wind_kt=%.3f from=%.3f gust_kt=%.3f turbulence=%.4f hour=%.4f visibility=%.1f\n",
                phase, w.cloudCover, w.cloudBase, w.precip, w.storm ? 1 : 0, w.windSpeed * MS_TO_KT, w.windFrom, w.gust * MS_TO_KT, w.turbulence, g->benchmarkTimeOfDay(), w.visibility);
      };
      if (cl.find("--nobodies") == std::string::npos) {   // (--nobodies: the shader compile timing alone)
        int built = 0; double secs = 0;
        buildBodies(built, secs);
        if (bf) { fprintf(bf, "aircraft bodies: %d built from scratch in %.1f s (the rest loaded from the cache)\n\n", built, secs); fflush(bf); }
      }
      g_ren.entSync = true;
      g_ren.syncTiming = false;   // normal async GPU queries; never serialized per-pass debug timing
      bool allValid = true;
      for (const std::string& sc : scenes) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        SetWindowTextA(g_hwnd, ("Solace Express - benchmarking " + sc).c_str());
        // a scene's first frame may bake an airframe's meshes (the XR-30's, a minute or more): the bake pumps the window's
        // messages between its batches, so Windows doesn't mark the game "Not Responding" and freeze the screen
        static std::string bakingScene; bakingScene = sc; static bool bakeNoted = false; bakeNoted = false;
        g_ren.bakeYield = [] { if (!bakeNoted) { bakeNoted = true; SetWindowTextA(g_hwnd, ("Solace Express - baking the airframe meshes for " + bakingScene + " (once; cached afterwards)").c_str()); } pumpB(); };
        Game* g = new Game();
        g->saveDir = game.saveDir;
        g->initHeadless(); g->iconTex = iconTex; g->debugScene(sc);
        g_ren.setRenderScale(1.f);   // explicit benchmark invariant; does not change saved settings
        fprintf(bf, "\nScene %s; requested WX=%s\n", sc.c_str(), getenv("WX") ? getenv("WX") : "(scene/default)");
        printWeather(g, "preset");
        g_ren.entSync = false;
        // warm until the scene is built: at least 40 frames, then until 20 frames in a row bake nothing and stream
        // nothing (a body, the scenery, the terrain shadow), at most 1200
        int warm = 0, quiet = 0; const int bakes0 = g_ren.bakeCount;
        for (; warm < 1200 && quiet < 20 && !game.quit; warm++) {
          const int bc = g_ren.bakeCount;
          g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); pumpB();
          const bool busy = g_ren.bakeCount != bc || g_ren.entPending > 0 || g_ren.tshPending();
          quiet = warm >= 39 && !busy ? quiet + 1 : 0;
        }
        const int warmBakes = g_ren.bakeCount - bakes0;
        g_ren.bakeYield = nullptr;
        SetWindowTextA(g_hwnd, ("Solace Express - benchmarking " + sc).c_str());
        struct Sample {
          LARGE_INTEGER begin{}, updateEnd{}, presentBegin{}, presentEnd{}, end{};
          uint64_t renderFrame = 0, gpuId = 0, gpuOrigin = 0;
          double gpu = 0; float scale = 1;
          int width = 0, height = 0, pending = 0, terrainPending = 0, bakes = 0, quality = 0;
          bool native1080 = false; int sceneStatus = 0;
        };
        std::vector<Sample> samples; samples.reserve(sampleFrames);
        const uint64_t firstGpuFrame = g_ren.gpuFrameSerial + 1;
        const uint64_t overwritten0 = g_ren.gpuSamplesOverwritten;
        uint64_t seenGpu = g_ren.gpuSample.id;
        const int measuredBakes0 = g_ren.bakeCount;
        const int measuredSceneStatus = g->benchmarkSceneStatus();
        glFinish();
        LARGE_INTEGER f0, f1; QueryPerformanceCounter(&f0);
        for (int i = 0; i < sampleFrames && !game.quit; i++) {
          Sample s; QueryPerformanceCounter(&s.begin);
          g->update(1.f / 60.f); QueryPerformanceCounter(&s.updateEnd);
          s.width = g_ren.W; s.height = g_ren.H; s.scale = g_ren.renderScale; s.quality = g_ren.quality;
          g->render(); QueryPerformanceCounter(&s.presentBegin);
          SwapBuffers(g_hdc); QueryPerformanceCounter(&s.presentEnd);
          pumpB();
          RECT client; GetClientRect(g_hwnd, &client);
          s.native1080 = client.right == 1920 && client.bottom == 1080 && g_ren.W == s.width && g_ren.H == s.height && s.width == 1920 && s.height == 1080 && fabsf(s.scale - 1.f) < 1e-6f;
          s.pending = g_ren.entPending; s.terrainPending = g_ren.tshPending() ? 1 : 0; s.bakes = g_ren.bakeCount;
          s.renderFrame = g_ren.gpuFrameSerial; s.sceneStatus = g->benchmarkSceneStatus();
          const auto& gpuSample = g_ren.gpuSample;
          if (gpuSample.id != seenGpu) {
            seenGpu = gpuSample.id;
            if (gpuSample.frame >= firstGpuFrame && gpuSample.frame <= s.renderFrame) {
              s.gpuId = gpuSample.id; s.gpuOrigin = gpuSample.frame; s.gpu = gpuSample.ms;
            }
          }
          QueryPerformanceCounter(&s.end);
          samples.push_back(s);
        }
        glFinish(); QueryPerformanceCounter(&f1);
        const int N = (int)samples.size();
        double ms = N ? qpcMs(f0, f1) / N : 0;
        std::vector<double> wall, present, update, submit, swap, gpuRaw;
        int nativeFrames = 0, streamFrames = 0, stableSceneFrames = 0;
        for (size_t i = 0; i < samples.size(); i++) {
          const Sample& s = samples[i];
          const double fw = qpcMs(s.begin, s.end), u = qpcMs(s.begin, s.updateEnd), r = qpcMs(s.updateEnd, s.presentBegin);
          const double sw = qpcMs(s.presentBegin, s.presentEnd), pump = qpcMs(s.presentEnd, s.end);
          const double pi = i ? qpcMs(samples[i - 1].presentBegin, s.presentBegin) : 0;
          wall.push_back(fw); update.push_back(u); submit.push_back(r); swap.push_back(sw);
          if (i) present.push_back(pi); if (s.gpuId) gpuRaw.push_back(s.gpu);
          stableSceneFrames += s.sceneStatus >= 0 && s.sceneStatus == measuredSceneStatus;
          nativeFrames += s.native1080; streamFrames += s.pending > 0 || s.terrainPending;
          if (csv) {
            fprintf(csv, "%s,%zu,%llu,%.6f,", benchmark::csvField(sc).c_str(), i, (unsigned long long)s.renderFrame, fw);
            if (i) fprintf(csv, "%.6f", pi); else fprintf(csv, "NA");
            fprintf(csv, ",%.6f,%.6f,%.6f,%.6f,", u, r, sw, pump);
            if (s.gpuId) fprintf(csv, "%llu,%llu,%.6f", (unsigned long long)s.gpuId, (unsigned long long)s.gpuOrigin, s.gpu);
            else fprintf(csv, "NA,NA,NA");
            fprintf(csv, ",%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%lld,%lld\n", s.width, s.height, (int)(s.width * s.scale), (int)(s.height * s.scale), s.scale, s.quality, s.pending, s.terrainPending, s.bakes, s.native1080 ? 1 : 0, s.sceneStatus, (long long)s.begin.QuadPart, (long long)s.presentBegin.QuadPart);
          }
        }
        const bool complete = N == sampleFrames && quiet >= 20 && stableSceneFrames == N && !game.quit;
        allValid = allValid && complete;
        fprintf(bf, "  capture complete=%s quiet_warmup=%s native1080_frames=%d/%d pending_stream_frames=%d measured_body_bakes=%d stable_scene_frames=%d/%d\n",
                complete ? "yes" : "no", quiet >= 20 ? "yes" : "no", nativeFrames, N, streamFrames, g_ren.bakeCount - measuredBakes0, stableSceneFrames, N);
        fprintf(bf, "  fresh_gpu_samples=%zu/%d query_overwrites=%llu; trailing pending GPU samples not force-read\n", gpuRaw.size(), N, (unsigned long long)(g_ren.gpuSamplesOverwritten - overwritten0));
        printSummary("frame work wall", wall, false); printSummary("app present interval", present, true);
        printSummary("CPU update wall", update, false); printSummary("CPU render/submit wall", submit, false);
        printSummary("SwapBuffers wall", swap, false); printSummary("GPU renderScene raw delayed", gpuRaw, false);
        printWeather(g, "measured-end");
        if (bf) {
          const float* pm = g_ren.passMs;
          fprintf(bf, "%-22s %6.2f ms/frame (%5.1f fps)   GPU %6.2f ms: world %.2f  displays %.2f  feeds %.2f  objects %.2f  shadow proxy %.2f  lighting %.2f  taa %.2f  sprites %.2f  bloom %.2f  shafts %.2f  composite %.2f\n",
                  sc.c_str(), ms, ms > 0 ? 1000.0 / ms : 0, g_ren.gpuMs, pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10]);
          fprintf(bf, "%-22s (warmed %d frames until built; %d bodies built during it)\n", "", warm, warmBakes);
          fflush(bf);
        }
        g_ren.entSync = true;
        delete g;
        if (game.quit) break;
      }
      const bool ioFailed = ferror(bf) || (csv && ferror(csv));
      bool closeFailed = fclose(bf) != 0;
      if (csv) closeFailed = fclose(csv) != 0 || closeFailed;
      return allValid && !ioFailed && !closeFailed ? 0 : 2;
    }
  }
  // Loading-screen pictures: SolaceExpress.exe --loadshots renders every airport (and every aircraft in flight) at
  // 1920x1080 with all scenery generated and the anti-aliasing settled, and saves them at 1280x720 (averaged down) in
  // the "loading" folder next to the exe, where the pre-flight loading screen picks them up.
  {
    std::string cl = GetCommandLineA();
    if (cl.find("--loadshots") != std::string::npos) {
      if (g_fullscreen) toggleFullscreen();
      RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
      SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      std::string dir = game.assetDir + "\\loading";
      CreateDirectoryA(dir.c_str(), nullptr);
      std::vector<std::pair<std::string, std::string>> jobs;   // scene, file
      for (const Airport& a : g_world.airports) jobs.push_back({std::string("loadshot_") + a.code, a.code});
      for (int i = 0; i <= kWraith; i++) jobs.push_back({"loadshot_air_" + std::to_string(i), "air_" + std::to_string(i)});
      int done = 0;
      for (auto& J : jobs) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        SetWindowTextA(g_hwnd, ("Solace Express - rendering loading pictures " + std::to_string(++done) + " / " + std::to_string(jobs.size()) + ": " + J.second).c_str());
        g_ren.entSync = true;
        Game* g = new Game();
        g->saveDir = game.saveDir; g->assetDir = game.assetDir;
        g->initHeadless(); g->iconTex = iconTex; g->debugScene(J.first);
        for (int i = 0; i < 4; i++) { g->update(1.f / 30.f); g->render(); }
        for (int i = 0; i < 40; i++) { g->update(1.f / 240.f); g->render(); }   // the TAA settles (barely moving)
        glFinish();
        int w = g_ren.W, h = g_ren.H;
        std::vector<uint8_t> px((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        SwapBuffers(g_hdc);
        // average down to 1280x720 (or keep a smaller window's size)
        int ow = std::min(1280, w), oh = std::min(720, h);
        std::vector<uint8_t> out((size_t)ow * oh * 3);
        for (int y = 0; y < oh; y++)
          for (int x = 0; x < ow; x++) {
            int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow), y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
            int acc[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const uint8_t* q = &px[((size_t)yy * w + xx) * 3]; acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2]; n++; }
            for (int c = 0; c < 3; c++) out[((size_t)y * ow + x) * 3 + c] = (uint8_t)(acc[c] / n);
          }
        writePNG((dir + "\\" + J.second + ".png").c_str(), ow, oh, out);
        delete g;
      }
      return 0;
    }
  }
  // Development captures: SolaceExpress.exe --shots scene1,scene2,... [--size 1920x1080] renders each debug scene
  // (see Game::debugScene) with all scenery generated up front and saves shots\<scene>.png next to the exe.
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--shots ");
    if (k != std::string::npos) {
      std::string list = cl.substr(k + 8); list = list.substr(0, list.find(' ')) + ",";
      int sw2 = 0, sh2 = 0; size_t kz = cl.find("--size ");
      if (kz != std::string::npos) sscanf(cl.c_str() + kz + 7, "%dx%d", &sw2, &sh2);
      if (sw2 > 64 && sh2 > 64) {
        if (g_fullscreen) toggleFullscreen();
        RECT wr = {0, 0, sw2, sh2}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      }
      char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/")) + "\\shots";
      CreateDirectoryA(dir.c_str(), nullptr);
      { int built = 0; double secs = 0; buildBodies(built, secs); }
      g_ren.entSync = true;
      for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
        std::string sc = list.substr(a, b - a);
        if (sc.empty()) continue;
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        SetWindowTextA(g_hwnd, ("Solace Express - rendering " + sc).c_str());
        Game* g = new Game();
        g->saveDir = game.saveDir;
        g->initHeadless(); g->iconTex = iconTex; g->debugScene(sc);
        for (int i = 0; i < 3; i++) { g->update(1.f / 30.f); g->render(); }
        for (int i = 0; i < 24; i++) g->render();   // TAA settles
        glFinish();
        g_ren.screenshotPNG((dir + "\\" + sc + ".png").c_str());
        SwapBuffers(g_hdc);
        delete g;
      }
      return 0;
    }
  }
  // (the menus render their scenes live: a menu.mp4 left from an older version is not played)
  startAudio();
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) { fprintf(f, "Audio: %s\n", audioBackendName()); fclose(f); }

  QueryPerformanceCounter(&prev);
  while (!game.quit) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
    prev = now;
    pollPad(game.in);
    RECT rc; GetClientRect(g_hwnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
      if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
      game.update(dt);
      game.render();
      SwapBuffers(g_hdc);
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
  game.shutdown();
  stopAudio();
  timeEndPeriod(1);
  timeEndPeriod(1);
  return 0;
}
