// Solace Express - Windows platform layer: window, OpenGL context, input, audio output
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
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "game.h"
#include "menu_video_win.h"

// Ask hybrid-graphics laptops for the dedicated GPU: the integrated one may reject or take minutes over the ray tracer
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
      if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) {   // Alt+Enter: fullscreen only (the Enter never reaches the game)
        if (!(lp & (1 << 30))) { toggleFullscreen(); setupPacing(h); if (g_game) g_game->set.fullscreen = g_fullscreen; }
        return 0;
      }
      if (in && wp < 256) { if (!(lp & (1 << 30))) in->pressed[wp] = true; in->down[wp] = true; }
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

// ------------------------------------------------------------------ audio output (waveOut, dedicated thread)
static const int kAudioBuffers = 4, kAudioFrames = 960;  // 20 ms per buffer at 48 kHz
static HWAVEOUT s_waveOut = nullptr;
static WAVEHDR s_hdr[kAudioBuffers];
static int16_t s_pcm[kAudioBuffers][kAudioFrames * 2];
static HANDLE s_audioEvent = nullptr;
static std::atomic<bool> s_audioRun{true};
static HANDLE s_audioThread = nullptr;

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
  s_audioThread = CreateThread(nullptr, 0, audioThread, nullptr, 0, nullptr);
  return true;
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
// first-time compile this way because NVIDIA's driver holds a process-wide lock while it links the ray tracer: no
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
  std::atomic<int> done{0}; std::atomic<bool> fin{false};
  std::thread rep([&] { int last = -1; while (!fin) { int d = done; if (d != last) { last = d; say("%d\n", d); } Sleep(15); } });
  bool ok = g_ren.compilePrograms(&done);
  fin = true; rep.join();
  say("%d\n", (int)done);
  say("m %d\n", g_shaderCacheMisses.load());
  wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctx);
  return ok ? 0 : 1;
}

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
  g_game = &game;
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

  // startup.log names the GPU in use (support aid)
  std::string gpu = std::string((const char*)glGetString(GL_RENDERER)) + " / " + (const char*)glGetString(GL_VERSION);
  // the ray tracer samples 22 textures in one fragment shader; OpenGL 3.3 only guarantees 16 (every current GPU has 32)
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
    WIN32_FIND_DATAA fd; HANDLE h = dir.empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((dir + "\\*.bin").c_str(), &fd);
    cached = h != INVALID_HANDLE_VALUE; if (cached) FindClose(h);
  }
  game.shaderFirstRun = !cached;
  auto fatal = [&](const std::string& what) {
    FILE* f = fopen((game.saveDir + "\\error.log").c_str(), "w");
    if (f) { fprintf(f, "%s\nRenderer: %s\n", what.c_str(), (const char*)glGetString(GL_RENDERER)); fclose(f); }
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
  std::thread worldThread([&] { g_world.build(); worldDone = true; });
  // the first-time compile runs in a child process (see buildShaderCacheChild); this process then loads the results
  PROCESS_INFORMATION child = {};
  std::atomic<int> childDone{-1}, childMisses{0};
  std::thread childReader;
  const std::string stampPath = g_shaderCacheDir.empty() ? std::string() : g_shaderCacheDir + "\\stamp.txt", stamp = shaderCacheStamp();
  bool cacheCurrent = false;
  if (!stampPath.empty()) if (FILE* f = fopen(stampPath.c_str(), "r")) { char b[32] = {}; cacheCurrent = fscanf(f, "%31s", b) == 1 && stamp == b; fclose(f); }
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
        childReader = std::thread([rd, &childDone, &childMisses] {
          std::string line; char buf[256]; DWORD n = 0;
          while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0)
            for (DWORD i = 0; i < n; i++) {
              if (buf[i] != '\n') { line += buf[i]; continue; }
              if (line.size() > 2 && line[0] == 'm') childMisses = atoi(line.c_str() + 2);
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
  // the ray tracer is most of the work: its progress is estimated from the last measured compile time
  float estRT = cached ? 1.5f : 40.f;
  if (!cached && !g_shaderCacheDir.empty())
    if (FILE* f = fopen((g_shaderCacheDir + "\\compile_time.txt").c_str(), "r")) { float v; if (fscanf(f, "%f", &v) == 1 && v > 1 && v < 3600) estRT = v; fclose(f); }
  LARGE_INTEGER freq, prev, now;
  QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
  LARGE_INTEGER t0 = prev; float compileSecs = 0;
  // The intro animates on its own thread with its own GL context. That context shares nothing with the others, so
  // nothing the loading work does in the driver (shader links on the worker context, texture uploads and the
  // renderer's set-up on the main one) can stall it. The main thread only posts the progress target and stage.
  struct Intro {
    std::mutex m; std::string stage;
    std::atomic<float> target{0.f};
    std::atomic<int> state{0};          // 0 starting, 1 running, 2 fade out and stop, 3 stop now, 4 finished, -1 unavailable
    std::thread th;
  } intro;
  intro.th = std::thread([&] {
    HDC dcI = GetDC(g_hwnd);
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    HGLRC ctxI = createAttribs ? createAttribs(dcI, nullptr, attrs) : nullptr;
    if (!ctxI || !wglMakeCurrent(dcI, ctxI)) { if (ctxI) wglDeleteContext(ctxI); intro.state = -1; return; }
    if (s_swapInterval) s_swapInterval(s_vsyncDiv ? s_vsyncDiv : 0);
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
      float target = intro.target;
      shown = std::max(shown, shown + (target - shown) * 0.12f);
      std::string stage; { std::lock_guard<std::mutex> lk(intro.m); stage = intro.stage; }
      GetClientRect(g_hwnd, &rc);
      if (rc.right > 0 && rc.bottom > 0) { R->W = rc.right; R->H = rc.bottom; }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, R->W, R->H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
      R->uiBegin(); game.drawIntro(shown, stage, t, ic, fade, *R); R->uiEnd();
      SwapBuffers(dcI);
      if (!s_vsyncDiv) Sleep(14);
    }
    glFinish();
    R.reset();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctxI);   // takes the intro's textures and buffers with it
    ReleaseDC(g_hwnd, dcI);
    intro.state = 4;
  });
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
    if (!s_vsyncDiv) Sleep(14);
    return t;
  };
  // ends the intro: `fade` plays its fade-out first; afterwards this thread owns the window's drawing again
  auto stopIntro = [&](bool fade) {
    if (!introThreaded) { if (fade) for (int i = 0; i < 20; i++) introFrame(1.f, "READY", 1.f - i / 20.f); return; }
    if (intro.state != 1) return;
    if (fade) { intro.target = 1.f; { std::lock_guard<std::mutex> lk(intro.m); intro.stage = "READY"; } }
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
    float sp = !ctx2 ? 0.f : compileState == 1 ? 1.f : d == 0 ? 0.8f * std::min(0.97f, 1.f - expf(-t / (estRT * 0.6f))) : 0.8f + 0.2f * (d - 1) / (Renderer::kProgramCount - 1);
    float wp = built ? 1.f : std::min(0.95f, t / 4.f);
    float target = 0.1f * wp + 0.7f * sp;   // (the rest: textures, then the menu and the aircraft shells - see below)
    std::string stage = !ctx2 ? "PREPARING" : d == 0 ? (cached ? "LOADING SHADERS FROM CACHE" : "COMPILING RAY TRACING SHADERS")
                      : compileState == 0 ? "COMPILING SHADERS  " + std::to_string(d) + " / " + std::to_string(Renderer::kProgramCount) : "SHADERS READY";
    if (!built) stage += "   //   GENERATING THE SOLACE ISLANDS";
    introFrame(target, stage, 1.f);
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
  if (!ctx2) introFrame(0.12f, cached ? "LOADING SHADERS FROM CACHE" : "COMPILING SHADERS (this can take a minute)", 1.f);
  if (ctx2 && compileState == 1 && !stampPath.empty() && !cacheCurrent && g_ren.dispError.empty())   // the cache now holds this build
    if (FILE* f = fopen(stampPath.c_str(), "w")) { fprintf(f, "%s\n", stamp.c_str()); fclose(f); }
  if ((g_shaderCacheMisses > 0 || childMisses > 0) && ctx2 && !g_shaderCacheDir.empty())
    if (FILE* f = fopen((g_shaderCacheDir + "\\compile_time.txt").c_str(), "w")) { fprintf(f, "%.1f\n", compileSecs); fclose(f); }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  game.init(false);
  introFrame(0.82f, "PREPARING TEXTURES", 1.f);
  g_ren.renderScale = 1.0f; g_ren.quality = game.set.quality;
  GetClientRect(g_hwnd, &cr);
  if (!g_ren.init(std::max(64L, cr.right), std::max(64L, cr.bottom))) { stopIntro(false); fatal(g_ren.error); return 1; }
  {
    std::string cl = GetCommandLineA();
    bool tool = cl.find("--bench ") != std::string::npos || cl.find("--shots ") != std::string::npos || cl.find("--profile ") != std::string::npos || cl.find("--analyze") != std::string::npos || cl.find("--loadshots") != std::string::npos || cl.find("--menuvideo") != std::string::npos;
    // a normal start loads the menu's first place and builds every aircraft's hull under the intro (rendered
    // offscreen: the intro keeps the window), so the menu opens complete and no flight waits for a hull
    if (!tool) game.prewarm([&](float f, const std::string& what) { introFrame(0.84f + 0.15f * f, what, 1.f); });
    if (game.quit) { stopIntro(false); return 0; }
    stopIntro(!tool);   // the bench and shot tools draw straight away; a normal start fades the intro out
  }
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) {
    fprintf(f, "Shader cache: %s (%d loaded, %d compiled)\n", g_shaderCacheDir.empty() ? "unavailable" : g_shaderCacheDir.c_str(), g_shaderCacheHits.load(), g_shaderCacheMisses.load());
    if (!g_ren.dispError.empty()) fprintf(f, "Display shader failed (cockpit screens disabled):\n%s\n", g_ren.dispError.c_str());
    fclose(f);
  }
  // Analysis: SolaceExpress.exe --analyze [scenes] - a thorough look at where the frame time goes, written to
  // analysis.txt (heat maps of the ray tracer's per-pixel work in the "analysis" folder), all at 1920x1080:
  //   CPU vs GPU (update / submit / wait), exact per-pass GPU times (the GPU is waited on at each pass boundary),
  //   frame time against render resolution (does it scale with pixel count?), what each ray tracer feature costs,
  //   and the per-pixel work the ray tracer does (terrain samples, aircraft distance-field samples, cloud steps,
  //   effect / light steps), averaged and drawn as heat maps.
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--analyze");
    if (k != std::string::npos) {
      std::string list = "menu,air,storm,night,cockpit,rjet,rjetc,wr_8_0_0_0_1";
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
      SetWindowTextA(g_hwnd, "Solace Express - analysis: building the counting ray tracer (about a minute)");
      LARGE_INTEGER c0, c1; QueryPerformanceCounter(&c0);
      bool haveCost = g_ren.buildCostProgram();
      QueryPerformanceCounter(&c1);
      fprintf(af, "Solace Express performance analysis\nGPU: %s\nCPU threads: %u   Quality: %d   Window: %dx%d\n",
              gpu.c_str(), std::thread::hardware_concurrency(), g_ren.quality, g_ren.W, g_ren.H);
      fprintf(af, "Counting ray tracer: %s (%.1f s)\n", haveCost ? "built" : ("FAILED - " + g_ren.error.substr(0, 300)).c_str(), (double)(c1.QuadPart - c0.QuadPart) / freq.QuadPart);
      fprintf(af, "\nHow to read this: 'ms' is wall-clock time per frame with the GPU finished (vsync off). Per-pass times are\n"
                  "exact (the GPU is waited on between passes, which adds a little overhead). Resolution scaling: if a frame\n"
                  "takes ~2.2x as long at 100%% as at 67%% (2.2x the pixels), the per-pixel ray tracing is the bottleneck.\n");
      auto qpcMs = [&](LARGE_INTEGER a, LARGE_INTEGER b) { return (double)(b.QuadPart - a.QuadPart) / freq.QuadPart * 1000.0; };
      auto pump = [&] { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } };
      static const char* kPassName[Renderer::kPasses] = {"scenery+shadows+displays", "ray trace", "TAA", "sprites", "bloom", "light shafts", "composite"};
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
          fprintf(af, "GPU passes (exact, %.2f ms total):\n", tot);
          for (int p = 0; p < Renderer::kPasses; p++) fprintf(af, "  %-26s %7.2f ms  %5.1f%%\n", kPassName[p], passSum[p], tot > 0 ? passSum[p] / tot * 100.0 : 0.0);
        }
        // 3. resolution scaling
        double ms100 = 0, ms67 = 0;
        {
          static const struct { float scale; const char* name; } kRes[] = {{1.f, "100%"}, {0.85f, " 85%"}, {0.75f, " 75%"}, {0.67f, " 67%"}};
          fprintf(af, "Render resolution (ray trace + TAA at a fraction of 1920x1080, upscaled):\n");
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
                  r > 1.8 ? "per-pixel ray tracing dominates" : r > 1.35 ? "mostly per-pixel, with a fixed cost besides" : "a fixed per-frame cost dominates (not pixel work)");
        }
        // 4. features
        std::string top; double topSave = 0;
        {
          double base = timed(30);
          fprintf(af, "Ray tracer features (frame time with it switched off; base %.2f ms):\n", base);
          for (const auto& F : kFeat) {
            g_ren.dbgOff = F.bit;
            double ms = timed(30);
            g_ren.dbgOff = 0;
            fprintf(af, "  %-26s %7.2f ms   saves %6.2f ms (%4.1f%%)\n", F.name, ms, base - ms, (base - ms) / base * 100.0);
            if (base - ms > topSave) { topSave = base - ms; top = F.name; }
          }
        }
        // 5. per-pixel work of the ray tracer
        if (haveCost) {
          g_ren.costMap = true; frames(2); glFinish();
          std::vector<float> cm; int w = 0, h = 0;
          g_ren.readCostMap(cm, w, h);
          g_ren.costMap = false;
          static const char* kCat[4] = {"terrain height samples", "aircraft shape samples", "cloud march steps", "effect / light steps"};
          size_t np = (size_t)w * h;
          fprintf(af, "Ray tracer work per pixel (%dx%d):            mean     95th pct      max\n", w, h);
          float p95v[4] = {};
          for (int c = 0; c < 4; c++) {
            std::vector<float> v(np); double sum = 0; float mx = 0;
            for (size_t i = 0; i < np; i++) { v[i] = cm[i * 4 + c]; sum += v[i]; mx = std::max(mx, v[i]); }
            std::nth_element(v.begin(), v.begin() + np * 95 / 100, v.end());
            p95v[c] = v[np * 95 / 100];
            fprintf(af, "  %-26s           %8.1f %10.0f %10.0f\n", kCat[c], sum / np, p95v[c], mx);
          }
          // heat maps: the four counters side by side (2x2), each scaled to its own 95th percentile
          int hw = w / 2, hh = h / 2;
          std::vector<uint8_t> img((size_t)w * h * 3, 0);
          for (int c = 0; c < 4; c++) {
            int ox = (c & 1) * hw, oy = (c < 2 ? 1 : 0) * hh;   // bottom-up rows: the first two on the top row
            float sc2 = p95v[c] > 0 ? 1.f / p95v[c] : 0.f;
            for (int y = 0; y < hh; y++)
              for (int x = 0; x < hw; x++) {
                float vv = cm[((size_t)(y * 2) * w + x * 2) * 4 + c] * sc2;
                float t = std::min(vv, 1.5f) / 1.5f;   // black -> blue -> red -> yellow -> white
                float r = std::min(1.f, t * 2.2f), gg = std::max(0.f, std::min(1.f, t * 2.2f - 0.9f)), bl = t < 0.3f ? t * 3.f : std::max(0.f, 1.f - (t - 0.3f) * 3.f) + std::max(0.f, t * 3.f - 2.f);
                uint8_t* o = &img[((size_t)(oy + y) * w + ox + x) * 3];
                o[0] = (uint8_t)(r * 255); o[1] = (uint8_t)(gg * 255); o[2] = (uint8_t)(std::min(bl, 1.f) * 255);
              }
          }
          writePNG((dir + "\\analysis\\" + sc + "_work.png").c_str(), w, h, img);
          fprintf(af, "  (heat map: analysis\\%s_work.png - top left terrain, top right aircraft, bottom left clouds, bottom right effects)\n", sc.c_str());
        }
        // a picture of the scene for reference
        frames(3); glFinish();
        g_ren.screenshotPNG((dir + "\\analysis\\" + sc + ".png").c_str());
        double rt = passSum[1];
        sums.push_back({sc, ms100, rt, ms67 > 0 ? ms100 / ms67 : 0, top});
        fflush(af);
        delete g;
      }
      fprintf(af, "\n==================== summary ====================\n");
      fprintf(af, "%-16s %9s %9s %12s  %s\n", "scene", "frame ms", "RT ms", "100%/67%", "most expensive feature");
      for (auto& S : sums) fprintf(af, "%-16s %9.2f %9.2f %12.2f  %s\n", S.sc.c_str(), S.ms, S.rt, S.scale, S.top.c_str());
      fclose(af);
      return 0;
    }
  }
  // Profile: SolaceExpress.exe --profile scene1,scene2,... renders each scene at 1920x1080 once normally and once with
  // each ray tracer feature switched off (Renderer::dbgOff), and writes profile.txt next to the exe: what every
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
          for (int i = 0; i < N; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); }
          glFinish(); QueryPerformanceCounter(&f1);
          double ms = (double)(f1.QuadPart - f0.QuadPart) / freq.QuadPart * 1000.0 / N;
          if (F.bit == 0) base = ms;
          if (pf) {
            if (F.bit == 0) fprintf(pf, "  %-26s %7.2f ms   (scenery+shadows %.2f, ray trace %.2f)\n", F.name, ms, g_ren.passMs[0], g_ren.passMs[1]);
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
  // plus the GPU time of every pass) and writes bench.txt next to the exe
  {
    std::string cl = GetCommandLineA();
    size_t k = cl.find("--bench ");
    if (k != std::string::npos) {
      std::string list = cl.substr(k + 8); list = list.substr(0, list.find(' ')) + ",";
      int sw2 = 0, sh2 = 0; size_t kz = cl.find("--size ");
      if (kz != std::string::npos) sscanf(cl.c_str() + kz + 7, "%dx%d", &sw2, &sh2);
      if (kz != std::string::npos && cl.compare(kz + 7, 6, "native") == 0) { if (!g_fullscreen) toggleFullscreen(); }   // the whole desktop
      else if (sw2 > 64 && sh2 > 64) {
        if (g_fullscreen) toggleFullscreen();
        RECT wr = {0, 0, sw2, sh2}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      }
      { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
      { RECT rc; GetClientRect(g_hwnd, &rc); if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom); }
      if (s_swapInterval) s_swapInterval(0);   // unlocked: measure what the GPU can do
      char exe[MAX_PATH] = {}; DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string dir(exe, n); dir = dir.substr(0, dir.find_last_of("\\/"));
      std::string outName = "bench.txt"; size_t ko = cl.find("--out ");
      if (ko != std::string::npos) { outName = cl.substr(ko + 6); outName = outName.substr(0, outName.find(' ')); }
      FILE* bf = fopen((dir + "\\" + outName).c_str(), "w");
      if (bf) fprintf(bf, "GPU: %s\nDesktop %dx%d, render %dx%d, quality %d\n\n", gpu.c_str(), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), g_ren.W, g_ren.H, g_ren.quality);
      g_ren.entSync = true;
      for (size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
        std::string sc = list.substr(a, b - a);
        if (sc.empty()) continue;
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
        SetWindowTextA(g_hwnd, ("Solace Express - benchmarking " + sc).c_str());
        Game* g = new Game();
        g->saveDir = game.saveDir;
        g->initHeadless(); g->iconTex = iconTex; g->debugScene(sc);
        g_ren.entSync = false;
        for (int i = 0; i < 40; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); }
        glFinish();
        LARGE_INTEGER f0, f1; QueryPerformanceCounter(&f0);
        const int N = 120;
        for (int i = 0; i < N; i++) { g->update(1.f / 60.f); g->render(); SwapBuffers(g_hdc); }
        glFinish(); QueryPerformanceCounter(&f1);
        double ms = (double)(f1.QuadPart - f0.QuadPart) / freq.QuadPart * 1000.0 / N;
        if (bf) {
          const float* pm = g_ren.passMs;
          fprintf(bf, "%-22s %6.2f ms/frame (%5.1f fps)   GPU %6.2f ms: scenery+shadows %.2f  raytrace %.2f  taa %.2f  sprites %.2f  bloom %.2f  shafts %.2f  composite %.2f\n",
                  sc.c_str(), ms, 1000.0 / ms, g_ren.gpuMs, pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6]);
          fflush(bf);
        }
        g_ren.entSync = true;
        delete g;
      }
      if (bf) fclose(bf);
      return 0;
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
  // The menu montage as a video: SolaceExpress.exe --menuvideo [--kbps N] renders the whole montage loop (8 shots,
  // 128 s) offline at 1920x1080 and 30 fps, every frame with its scenery complete, and encodes it to menu.mp4 next to
  // the exe (H.264, 5 Mbps by default: about 80 MB). The main menu then plays that instead of ray tracing the montage.
  {
    std::string cl = GetCommandLineA();
    if (cl.find("--menuvideo") != std::string::npos) {
      int kbps = 5000; size_t kb = cl.find("--kbps "); if (kb != std::string::npos) kbps = std::clamp(atoi(cl.c_str() + kb + 7), 500, 40000);
      if (g_fullscreen) toggleFullscreen();
      RECT wr = {0, 0, 1920, 1080}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
      SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
      MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
      RECT rc; GetClientRect(g_hwnd, &rc);
      if (rc.right != g_ren.W || rc.bottom != g_ren.H) g_ren.resize(rc.right, rc.bottom);
      const int fps = 30, frames = 128 * fps;   // the montage's loop: 8 shots of 16 s, each fading through black
      std::string path = game.assetDir + "\\menu.mp4", tmp = game.assetDir + "\\menu_rendering.mp4";
      DeleteFileA(tmp.c_str());
      MenuVideoWriter vw;
      int w = g_ren.W & ~1, h = g_ren.H & ~1;
      if (!vw.open(tmp, w, h, fps, kbps)) { MessageBoxA(g_hwnd, ("Could not record the menu video: " + vw.error).c_str(), "Solace Express", MB_OK); return 1; }
      g_ren.entSync = true;   // every frame with its scenery complete
      Game* g = new Game();
      g->saveDir = game.saveDir; g->assetDir = game.assetDir;
      g->initHeadless(); g->iconTex = iconTex; g->debugScene("menuT0"); g->sceneOnly = true;
      std::vector<uint8_t> px((size_t)g_ren.W * g_ren.H * 3), fr((size_t)w * h * 3);
      LARGE_INTEGER f0, f1, fq; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&f0);
      bool ok = true;
      for (int i = 0; i < frames && ok; i++) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) game.quit = true; TranslateMessage(&m); DispatchMessageW(&m); }
        if (game.quit) { ok = false; break; }
        g->update(1.f / fps); g->render();
        glFinish();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, g_ren.W, g_ren.H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < h; y++) memcpy(&fr[(size_t)y * w * 3], &px[(size_t)y * g_ren.W * 3], (size_t)w * 3);
        ok = vw.write(fr.data());
        SwapBuffers(g_hdc);
        if (i % 15 == 0) {
          QueryPerformanceCounter(&f1);
          double el = (double)(f1.QuadPart - f0.QuadPart) / fq.QuadPart, left = el / (i + 1) * (frames - i - 1);
          SetWindowTextA(g_hwnd, ("Solace Express - rendering the menu video: frame " + std::to_string(i + 1) + " / " + std::to_string(frames) +
                                  "  (about " + std::to_string((int)(left / 60.0 + 0.5)) + " min left)").c_str());
        }
      }
      delete g;
      ok = vw.finish() && ok;
      if (ok) { DeleteFileA(path.c_str()); ok = MoveFileA(tmp.c_str(), path.c_str()) != 0; }
      else DeleteFileA(tmp.c_str());
      if (!ok && !game.quit) MessageBoxA(g_hwnd, "Recording the menu video failed.", "Solace Express", MB_OK);
      return ok ? 0 : 1;
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
  // the pre-rendered menu montage, when render_menu.bat has made one (or it shipped with the game)
  MenuVideoPlayer menuVideo;
  if (menuVideo.open(game.assetDir + "\\menu.mp4")) game.menuVideo = [&menuVideo](float t) { return menuVideo.frame(t); };
  startAudio();

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
