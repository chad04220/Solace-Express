// Somnium Volandi - the development tools the exe runs instead of the game when its command line asks (diagnostics.bat
// runs them): --analyze, --profile, --soak, --bench, --loadshots and --shots. Each makes a diskless Game of a debug
// scene (Game::debugScene) on the window WinMain made, after the launch's steps, and writes what it found next to the
// exe. Windows only: they drive the window, the swap chain and the process's counters directly.
#pragma once
#include <windows.h>
#include <functional>
#include <string>

class Game;

// true when the command line asks for one of the tools: WinMain then skips what only the game needs (Steam, the menu's
// scenery, the aircraft meshes) and leaves the development switches on (dev_switches.h)
bool toolLaunch(const std::string& cmdLine);

// What loading every aircraft body took before a tool drew its first scene (ToolHost::buildBodies): the bodies ready
// built in the release's aircraft folder, from this PC's mesh cache or built from scratch, the packaged ones missing or
// refused, and the shader programs compiled for them (each aircraft type's own: compiled the first time it is drawn
// after an update, then loaded from the cache), here and by the helper processes, with the seconds each took
struct BodyLoad { int ready = 0, cached = 0, built = 0, refused = 0, programs = 0, helpers = 0, helperPrograms = 0; double secs = 0, programSecs = 0, helperSecs = 0; };

// What a tool borrows from WinMain: the launch's Game (its folders, its version, its quit flag when the window is
// closed), the window and its device context, the GPU's name for the reports' headers, and the platform's own routines
struct ToolHost {
  Game& game;
  std::string gpu;                            // the renderer and driver strings (glGetString)
  unsigned iconTex = 0;                       // the window icon's texture, given to every Game a tool makes
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  LARGE_INTEGER freq = {};                    // QueryPerformanceFrequency
  std::function<bool()> fullscreen;           // whether the window is fullscreen now
  std::function<void()> toggleFullscreen;
  std::function<bool(int)> swapInterval;      // wglSwapIntervalEXT, or empty without the extension
  std::function<void(BodyLoad&)> buildBodies; // every aircraft body loaded or built, with its programs (WinMain's)
};

// runs the tool the command line names and returns its exit code; -1 when the command line names none
int runToolMode(const std::string& cmdLine, ToolHost& host);
