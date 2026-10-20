// Somnium Volandi - photo mode (game.h Game::Photo): the flight frozen where it is, a free camera within reach of the
// aircraft, a grade (exposure, vignette, colour) and a frame (a letterbox or a square), the shot saved as a PNG without
// the UI - to Pictures\Somnium Volandi, and to the player's Steam screenshots when Steam is running
#include "game.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static const float kPhotoReach = 600.f;   // how far the camera may wander from the aircraft (m)

vec3 Game::photoForward() const {
  return vec3(sinf(photo.yaw) * cosf(photo.pitch), sinf(photo.pitch), -cosf(photo.yaw) * cosf(photo.pitch));
}

vec3 Game::photoUp() const {   // (level, then turned about the view by the roll)
  const vec3 f = photoForward(), r = normalize(cross(f, vec3(0, 1, 0))), u = cross(r, f);
  return normalize(u * cosf(photo.roll) + r * sinf(photo.roll));
}

void Game::photoLook(int look) {
  struct Look { float sat, con, warm, vig, ev; };
  static const Look k[5] = {{1.f, 1.f, 0.f, 0.6f, 0.f}, {1.3f, 1.12f, 0.1f, 0.5f, 0.f}, {0.85f, 1.05f, 0.25f, 0.9f, 0.f},
                            {0.f, 1.25f, 0.f, 1.1f, 0.f}, {1.1f, 1.05f, 0.7f, 0.8f, 0.2f}};
  const Look& l = k[std::clamp(look, 0, 4)];
  photo.look = std::clamp(look, 0, 4);
  photo.saturation = l.sat; photo.contrast = l.con; photo.warmth = l.warm; photo.vignette = l.vig; photo.ev = l.ev;
}

void Game::enterPhoto() {
  if (photo.on || screen != SCR_FLIGHT || !plane.spec || botControl) return;
  photo.on = true; photo.wasPaused = paused; paused = true;
  settingsFromPause = false; showRadio = false; showMap = false;
  photo.camModeWas = camMode;
  // the camera starts where the view was, looking the same way; from the cockpit, just behind and above the aircraft
  vec3 from = camPos;
  if (camMode == 1) {
    const float size = std::max(plane.spec->fusLen, plane.spec->span);
    from = plane.pos - plane.forward() * (size * 1.1f + 4.f) + vec3(0, size * 0.35f + 1.5f, 0);
    camMode = 0;   // (the aircraft drawn from outside: the cockpit's own view is the cockpit camera's)
  }
  photo.pos = from;
  const vec3 f = normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - from);
  photo.yaw = atan2f(f.x, -f.z); photo.pitch = asinf(clampf(f.y, -1.f, 1.f)); photo.roll = 0.f;
  photo.fov = set.fov; photo.panel = true; photo.capture = 0; photo.uiDrag = false;
  g_audio.trigger(SFX_CLICK);
}

void Game::exitPhoto() {
  if (!photo.on) return;
  photo.on = false; photo.capture = 0;
  paused = photo.wasPaused; camMode = photo.camModeWas;
  g_audio.trigger(SFX_CLICK);
}

void Game::updatePhoto(float dt) {
  if (screen != SCR_FLIGHT || !plane.spec) { photo.on = false; return; }
  if (in.pressed[K_ESC] || (in.buttonsPressed & (PAD_B | PAD_START)) || actPressed(ACT_PHOTO)) { exitPhoto(); return; }
  if (in.pressed['H'] || (in.buttonsPressed & PAD_Y)) photo.panel = !photo.panel;
  if (in.pressed[K_SPACE] || (in.buttonsPressed & PAD_X)) photo.capture = 1;
  if (focusNav && (in.buttonsPressed & PAD_A)) in.pressed[K_ENTER] = true;   // (A presses what the D-pad has picked on the panel)
  // moving: W / S along the view, A / D across it, Q / E down and up (Shift faster, Ctrl slower); the left stick and
  // the triggers (the stick pressed: faster)
  const float fast = in.down[K_SHIFT] || (in.pad && (in.buttons & PAD_LS)) ? 4.f : in.down[K_CTRL] ? 0.25f : 1.f;
  vec3 mv(0, 0, 0);
  mv.z = (in.down['W'] ? 1.f : 0.f) - (in.down['S'] ? 1.f : 0.f);
  mv.x = (in.down['D'] ? 1.f : 0.f) - (in.down['A'] ? 1.f : 0.f);
  mv.y = (in.down['E'] ? 1.f : 0.f) - (in.down['Q'] ? 1.f : 0.f);
  auto dz = [](float v) { return fabsf(v) < 0.15f ? 0.f : (v - (v > 0 ? 0.15f : -0.15f)) / 0.85f; };
  if (in.pad) { mv.x += dz(in.lx); mv.z += dz(in.ly); mv.y += in.rt - in.lt; }
  const vec3 f = photoForward(), r = normalize(cross(f, vec3(0, 1, 0)));
  photo.pos = photo.pos + (f * mv.z + r * mv.x + vec3(0, mv.y, 0)) * (9.f * fast * dt);
  // turning: a drag (the right button, or the left one away from the panel), the right stick; [ ] or the bumpers roll
  float px, py, pw, ph; photoPanelRect(px, py, pw, ph);
  const bool overPanel = photo.panel && hovered(px, py, pw, ph);
  if (in.mPressed[0]) photo.uiDrag = overPanel;
  if (!in.mDown[0]) photo.uiDrag = false;
  const float zoomK = photo.fov / 60.f;   // (zoomed in: finer)
  if (in.mDown[1] || (in.mDown[0] && !photo.uiDrag)) {
    photo.yaw += in.mdx * 0.0035f * set.mouseSens * zoomK;
    photo.pitch = clampf(photo.pitch - in.mdy * 0.0035f * set.mouseSens * zoomK, -1.45f, 1.45f);
  }
  if (in.pad) {
    photo.yaw += dz(in.rx) * 1.6f * zoomK * dt;
    photo.pitch = clampf(photo.pitch + dz(in.ry) * 1.2f * zoomK * dt, -1.45f, 1.45f);
  }
  const float roll = (in.down[K_RBRACKET] || (in.pad && (in.buttons & PAD_RB)) ? 1.f : 0.f) - (in.down[K_LBRACKET] || (in.pad && (in.buttons & PAD_LB)) ? 1.f : 0.f);
  photo.roll = clampf(photo.roll + roll * 0.8f * dt, -0.8f, 0.8f);
  if (in.wheel != 0 && !overPanel) { photo.fov = clampf(photo.fov * powf(0.92f, in.wheel), 15.f, 100.f); in.wheel = 0; }
  // within reach of the aircraft, above the ground and the sea
  const vec3 off = photo.pos - plane.pos;
  const float d = length(off);
  if (d > kPhotoReach) photo.pos = plane.pos + off * (kPhotoReach / d);
  const float gh = std::max(g_world.height(photo.pos.x, photo.pos.z, 6), 0.f) + 0.6f;
  if (photo.pos.y < gh) photo.pos.y = gh;
}

static bool photoFileExists(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (f) fclose(f); return f != nullptr; }

std::string Game::savePhoto() {
  if (!g_ren.ok) return std::string();   // (no window: a test)
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  const std::string fallback = saveDir.empty() ? std::string("Photos") : saveDir + sep + "Photos";
  std::string path;
  for (const std::string& dir : {photoDir.empty() ? fallback : photoDir, fallback}) {   // (Pictures first; the save folder if it can't be written)
#ifdef _WIN32
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
    char stamp[40] = "photo";
    const time_t t = time(nullptr);
    if (const tm* lt = localtime(&t)) strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", lt);
    path = dir + sep + "Somnium Volandi " + stamp + ".png";
    for (int k = 2; photoFileExists(path) && k < 100; k++) path = dir + sep + "Somnium Volandi " + stamp + " (" + std::to_string(k) + ").png";
    if (g_ren.screenshotPNG(path.c_str())) break;
    path.clear();
  }
  if (path.empty()) return path;
  steam::addScreenshot(path, g_ren.W, g_ren.H);   // (Steam's screenshots too, when it's running)
  return path;
}
