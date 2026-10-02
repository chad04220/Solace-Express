// Air Xpress - Windows platform layer: window, OpenGL context, input, audio output
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <objbase.h>
#include "game.h"

// ------------------------------------------------------------------ WGL bits
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);

// ------------------------------------------------------------------ 60 Hz frame pacing
// On 60/120/180/240 Hz displays vsync runs every 1st/2nd/3rd/4th refresh (adaptive when WGL_EXT_swap_control_tear is
// available, so a late frame tears once instead of halving to 30 fps); on other refresh rates a precise software
// limiter holds 60 fps.
static PFNWGLSWAPINTERVALEXTPROC s_swapInterval = nullptr;
static bool s_tear = false;
static int s_vsyncDiv = 0;   // > 0: swap interval that gives 60 Hz; 0: software limiter
static void setupPacing(HWND hwnd) {
  int hz = 60;
  MONITORINFOEXA mi; mi.cbSize = sizeof mi;
  if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
    DEVMODEA dm; ZeroMemory(&dm, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) hz = (int)dm.dmDisplayFrequency;
  }
  int div = 0;
  for (int d = 1; d <= 4; d++) if (abs(hz - 60 * d) <= 1) div = d;
  s_vsyncDiv = s_swapInterval ? div : 0;
  if (s_swapInterval) s_swapInterval(s_vsyncDiv ? (s_tear ? -s_vsyncDiv : s_vsyncDiv) : 0);
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
    case WM_DISPLAYCHANGE: case WM_EXITSIZEMOVE: if (s_swapInterval || !s_vsyncDiv) setupPacing(h); break;   // refresh rate / monitor may have changed
    case WM_SIZE: if (g_ren.ok) g_ren.resize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
      if (in && wp < 256) { if (!(lp & (1 << 30))) in->pressed[wp] = true; in->down[wp] = true; }
      if (wp == VK_F10 || wp == VK_MENU) return 0;
      if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) { toggleFullscreen(); setupPacing(h); return 0; }
      break;
    case WM_KEYUP: case WM_SYSKEYUP:
      if (in && wp < 256) in->down[wp] = false;
      if (wp == VK_F10 || wp == VK_MENU) return 0;
      break;
    case WM_KILLFOCUS: if (in) memset(in->down, 0, sizeof(in->down)); break;
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

// ------------------------------------------------------------------ audio output (waveOut, dedicated thread)
static const int kAudioBuffers = 4, kAudioFrames = 960;  // 20 ms per buffer at 48 kHz
static HWAVEOUT s_waveOut = nullptr;
static WAVEHDR s_hdr[kAudioBuffers];
static int16_t s_pcm[kAudioBuffers][kAudioFrames * 2];
static HANDLE s_audioEvent = nullptr;
static volatile bool s_audioRun = true;

static DWORD WINAPI audioThread(LPVOID) {
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

static bool startAudio() {
  g_audio.init(48000);
  WAVEFORMATEX wf = {};
  wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 48000; wf.wBitsPerSample = 16;
  wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 48000 * 4;
  s_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (waveOutOpen(&s_waveOut, WAVE_MAPPER, &wf, (DWORD_PTR)s_audioEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
  for (int i = 0; i < kAudioBuffers; i++) {
    memset(&s_hdr[i], 0, sizeof(WAVEHDR));
    memset(s_pcm[i], 0, sizeof(s_pcm[i]));
    s_hdr[i].lpData = (LPSTR)s_pcm[i]; s_hdr[i].dwBufferLength = sizeof(s_pcm[i]);
    waveOutPrepareHeader(s_waveOut, &s_hdr[i], sizeof(WAVEHDR));
    waveOutWrite(s_waveOut, &s_hdr[i], sizeof(WAVEHDR));
  }
  CreateThread(nullptr, 0, audioThread, nullptr, 0, nullptr);
  return true;
}

static void stopAudio() {
  s_audioRun = false;
  Sleep(60);
  if (s_waveOut) { waveOutReset(s_waveOut); for (int i = 0; i < kAudioBuffers; i++) waveOutUnprepareHeader(s_waveOut, &s_hdr[i], sizeof(WAVEHDR)); waveOutClose(s_waveOut); }
}

// ------------------------------------------------------------------ gamepad
static void pollPad(Input& in) {
  if (!s_xinput) return;
  XState st = {};
  if (s_xinput(0, &st) != 0) { in.pad = false; return; }
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
  std::string d = n ? std::string(buf) + "\\AirXpress" : std::string(".");
  CreateDirectoryA(d.c_str(), nullptr);
  return d;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  SetProcessDPIAware();
  timeBeginPeriod(1);
  static Game game;
  g_game = &game;
  game.saveDir = userDir();

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndProc; wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
  wc.lpszClassName = L"AirXpressWnd";
  RegisterClassExW(&wc);
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  int ww = std::min(1600, sw * 4 / 5), wh = ww * 9 / 16;
  RECT r = {0, 0, ww, wh};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Air Xpress", WS_OVERLAPPEDWINDOW, (sw - (r.right - r.left)) / 2, (sh - (r.bottom - r.top)) / 2,
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
  if (!ctx) { MessageBoxA(g_hwnd, "Air Xpress needs an OpenGL 3.3 capable graphics driver.\nPlease update your graphics drivers.", "Air Xpress", MB_ICONERROR); return 1; }
  wglMakeCurrent(g_hdc, ctx);
  wglDeleteContext(legacy);
  g_opengl32 = LoadLibraryA("opengl32.dll");
  const char* missing = nullptr;
  if (!glLoad(wglProc, &missing)) { MessageBoxA(g_hwnd, (std::string("Missing OpenGL function: ") + missing).c_str(), "Air Xpress", MB_ICONERROR); return 1; }
  s_swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
  if (auto ext = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT")) { const char* e = ext(); s_tear = e && strstr(e, "WGL_EXT_swap_control_tear"); }
  timeBeginPeriod(1);   // 1 ms Sleep granularity for the frame limiter
  setupPacing(g_hwnd);
  const char* xdlls[] = {"xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll"};
  for (auto d : xdlls) { HMODULE m = LoadLibraryA(d); if (m) { s_xinput = (PFNXINPUTGETSTATE)GetProcAddress(m, "XInputGetState"); if (s_xinput) break; } }

  ShowWindow(g_hwnd, SW_SHOW);
  // loading screen while the world and textures are generated
  glViewport(0, 0, ww, wh); glClearColor(0.03f, 0.05f, 0.08f, 1); glClear(GL_COLOR_BUFFER_BIT); SwapBuffers(g_hdc);

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  game.init();
  if (game.set.fullscreen) { toggleFullscreen(); setupPacing(g_hwnd); }
  RECT cr; GetClientRect(g_hwnd, &cr);
  g_ren.renderScale = 1.0f; g_ren.quality = game.set.quality;
  if (!g_ren.init(std::max(64L, cr.right), std::max(64L, cr.bottom))) {
    FILE* f = fopen((game.saveDir + "\\error.log").c_str(), "w");
    if (f) { fprintf(f, "%s\nRenderer: %s\n", g_ren.error.c_str(), (const char*)glGetString(GL_RENDERER)); fclose(f); }
    MessageBoxA(g_hwnd, ("Graphics initialisation failed:\n" + g_ren.error.substr(0, 1500)).c_str(), "Air Xpress", MB_ICONERROR);
    return 1;
  }
  startAudio();

  LARGE_INTEGER freq, prev, now;
  QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
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
      if (!s_vsyncDiv) {   // software 60 fps limiter: sleep most of the way, spin the last ~2 ms
        static LONGLONG deadline = 0;
        const LONGLONG period = freq.QuadPart / 60;
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
    if (game.wantFullscreenToggle) { game.wantFullscreenToggle = false; toggleFullscreen(); game.set.fullscreen = g_fullscreen; setupPacing(g_hwnd); }
    game.in.endFrame();
  }
  game.shutdown();
  stopAudio();
  timeEndPeriod(1);
  timeEndPeriod(1);
  return 0;
}
