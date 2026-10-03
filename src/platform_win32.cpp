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
  if (!n) return ".";
  std::string d = std::string(buf) + "\\SolaceExpress";
  // the game used to be called Air Xpress: carry its saves, settings, stations and shader cache over
  std::string old = std::string(buf) + "\\AirXpress";
  if (GetFileAttributesA(d.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES)
    if (!MoveFileExA(old.c_str(), d.c_str(), 0)) return old;   // in use or locked: keep using it as it is
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
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "w")) { fprintf(f, "GPU: %s\n", gpu.c_str()); fclose(f); }
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
  std::thread compileThread;
  if (ctx2) compileThread = std::thread([&] {
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
    int d = progDone;
    bool compiled = !ctx2 || compileState != 0, built = worldDone;
    if (d == 0 && compileState == 0) compileSecs = t;
    float sp = !ctx2 ? 0.f : compileState == 1 ? 1.f : d == 0 ? 0.8f * std::min(0.97f, 1.f - expf(-t / (estRT * 0.6f))) : 0.8f + 0.2f * (d - 1) / (Renderer::kProgramCount - 1);
    float wp = built ? 1.f : std::min(0.95f, t / 4.f);
    float target = 0.12f * wp + 0.83f * sp;
    std::string stage = !ctx2 ? "PREPARING" : d == 0 ? (cached ? "LOADING SHADERS FROM CACHE" : "COMPILING RAY TRACING SHADERS")
                      : compileState == 0 ? "COMPILING SHADERS  " + std::to_string(d) + " / " + std::to_string(Renderer::kProgramCount) : "SHADERS READY";
    if (!built) stage += "   //   GENERATING THE SOLACE ISLANDS";
    introFrame(target, stage, 1.f);
    if (game.quit) break;
    if (compiled && built && t > 3.2f) break;   // the logo stays up long enough to be seen
  }
  if (compileThread.joinable()) compileThread.join();
  worldThread.join();
  if (ctx2) wglDeleteContext(ctx2);
  if (dc2) ReleaseDC(hw2, dc2);
  if (hw2) DestroyWindow(hw2);
  if (game.quit) { stopIntro(false); return 0; }
  if (ctx2 && compileState != 1) { stopIntro(false); fatal(g_ren.error); return 1; }
  if (!ctx2) introFrame(0.12f, cached ? "LOADING SHADERS FROM CACHE" : "COMPILING SHADERS (this can take a minute)", 1.f);
  if (g_shaderCacheMisses > 0 && ctx2 && !g_shaderCacheDir.empty())
    if (FILE* f = fopen((g_shaderCacheDir + "\\compile_time.txt").c_str(), "w")) { fprintf(f, "%.1f\n", compileSecs); fclose(f); }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  game.init(false);
  introFrame(0.97f, "PREPARING TEXTURES", 1.f);
  g_ren.renderScale = 1.0f; g_ren.quality = game.set.quality;
  GetClientRect(g_hwnd, &cr);
  if (!g_ren.init(std::max(64L, cr.right), std::max(64L, cr.bottom))) { stopIntro(false); fatal(g_ren.error); return 1; }
  {
    std::string cl = GetCommandLineA();
    bool tool = cl.find("--bench ") != std::string::npos || cl.find("--shots ") != std::string::npos;
    stopIntro(!tool);   // the bench and shot tools draw straight away; a normal start fades the intro out
  }
  if (FILE* f = fopen((game.saveDir + "\\startup.log").c_str(), "a")) {
    fprintf(f, "Shader cache: %s (%d loaded, %d compiled)\n", g_shaderCacheDir.empty() ? "unavailable" : g_shaderCacheDir.c_str(), g_shaderCacheHits.load(), g_shaderCacheMisses.load());
    if (!g_ren.dispError.empty()) fprintf(f, "Display shader failed (cockpit screens disabled):\n%s\n", g_ren.dispError.c_str());
    fclose(f);
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
