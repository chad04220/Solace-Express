// Solace Express - camera feeds for the research jets' cockpit displays
#pragma once
#include "common.h"
#include "aircraft.h"

// The XR-9 and XR-11 cockpits are sealed: the pilot sees outside only through display panels, and every panel shows
// the picture of a real camera mounted on the airframe. A camera is a game object with its own position and
// orientation in the world, field of view and resolution (FeedCamera, in FrameParams). The renderer draws the scene
// from each one into a tile of its feed atlas (a few cameras per frame, in turn) and the panels show those tiles.
//
// A rig lists a craft's cameras by slot. The shader's feedScreen (shaders_wraith_cockpit.h) maps a point on a panel
// to the same slot, so the pane geometry below must match the shader's.
static const int kMaxFeeds = 13;
static const int kFeedBombSlot = 12;   // XR-11: the bomb camera (a free-flying camera, not on the airframe)

struct FeedCamera {
  bool on = false;
  vec3 pos, right, up, back;   // world; looks along -back
  float tanX = 0.5f, tanY = 0.4f;
  float pano = 0.f;            // > 0: a panoramic camera, its picture on a cylinder round it: this half angle (rad) across
  int w = 0, h = 0;            // its picture, in pixels
  vec3 screen; float screenR = 0.f;   // where its display is (world centre and radius): only a display in view is kept live
};

// one camera of a rig, in the cockpit frame (eye at the origin, -z forward, +y up, body axes)
struct FeedMount {
  vec3 dir;                  // from the eye through the middle of its panel: the lens sits where this leaves the skin
  vec3 right, up, back;      // the panel's frame: the picture is upright on the panel and keeps its left and right
  float tanX, tanY;
  float pano = 0.f;          // > 0: panoramic (see FeedCamera)
  int w, h;
  bool nose = false;         // forward looking: mounted at the tip of the nose instead (the nose stays out of its picture)
  vec3 screen; float screenR = 0.f;   // its display: centre and radius
};

inline int feedRigOf(const AircraftSpec* s) { if (!s) return 0; int sp = s->special; return sp == 1 ? 1 : sp == 2 ? 2 : (s - kAircraft) == kMantis ? 3 : 0; }   // 1 XR-9, 2 XR-11, 3 XR-10

// A flat panel: centre c, normal n (facing the pilot), "up" hint u, half size s; mirrored to the left side when
// left. The camera looks out through it (along -n, with the panel's own axes) and the display shows it as a window
// would: the shader lays each point of the picture where the pilot's eye sees that direction through the panel
// (feedScreen), so the camera's field of view is exactly what the panel covers from the eye, and its picture has the
// main view's pixel density there (focal: the main view's focal length in pixels).
inline FeedMount feedPanel(vec3 c, vec3 n, vec3 u, vec2 s, bool left, float focal) {
  if (left) { c.x = -c.x; n.x = -n.x; }
  n = normalize(n);
  vec3 t = normalize(cross(u, n)), b = cross(n, t);
  // aimed through the middle of the panel as the eye sees it (a panel seen at a slant: overhead, floor), so a
  // symmetric picture wastes little; upright on the panel
  vec3 p4[4], a(0.f);
  for (int i = 0; i < 4; i++) { p4[i] = c + t * (i & 1 ? s.x : -s.x) + b * (i & 2 ? s.y : -s.y); a = a + normalize(p4[i]); }
  a = normalize(a);
  vec3 r = normalize(t - a * dot(t, a)), up = cross(-a, r);
  FeedMount m;
  m.dir = normalize(c); m.right = r; m.up = up; m.back = -a;
  m.screen = c; m.screenR = length(s);
  float tx = 0.f, ty = 0.f;
  for (int i = 0; i < 4; i++) {
    float z = std::max(dot(p4[i], a), 0.05f);
    tx = std::max(tx, fabsf(dot(p4[i], r)) / z); ty = std::max(ty, fabsf(dot(p4[i], up)) / z);
  }
  m.tanX = tx * 1.02f; m.tanY = ty * 1.02f;
  m.w = (int)(2.f * m.tanX * focal + 0.5f); m.h = (int)(2.f * m.tanY * focal + 0.5f);
  return m;
}

// the rig of a research jet (empty for anything else); focal: the main view's focal length in pixels (sets the
// pictures' resolution: the displays are as sharp as the screen they're seen on)
inline int feedRig(int rig, float focal, FeedMount out[kMaxFeeds]) {
  if (rig == 1) {
    // XR-9: a panoramic display on a cylinder around the eye (r 0.64 m, +-1.25 rad, y 0.02 +- 0.30), shown by one
    // panoramic camera at the nose whose picture is that same cylinder (slots 1 and 2 are unused); a side display bay
    // either side
    {
      FeedMount& m = out[0];
      m.dir = vec3(0, 0, -1); m.right = vec3(1, 0, 0); m.up = vec3(0, 1, 0); m.back = vec3(0, 0, 1);
      m.pano = 1.25f * 1.01f; m.tanY = 0.32f / 0.64f * 1.02f; m.tanX = tanf(1.25f);
      m.screen = vec3(0.f, 0.02f, -0.64f); m.screenR = 0.9f;
      m.w = (int)(2.f * m.pano * focal + 0.5f); m.h = (int)(2.f * m.tanY * focal + 0.5f);
      m.nose = true;
      for (int k = 1; k <= 2; k++) { out[k] = m; out[k].w = out[k].h = 0; }   // (no camera: the atlas skips it)
    }
    out[3] = feedPanel(vec3(0.635f, 0.04f, 0.24f), vec3(-1, 0, 0), vec3(0, 1, 0), vec2(0.3f, 0.2f), true, focal);
    out[4] = feedPanel(vec3(0.635f, 0.04f, 0.24f), vec3(-1, 0, 0), vec3(0, 1, 0), vec2(0.3f, 0.2f), false, focal);
    return 5;
  }
  if (rig == 2) {
    // XR-11: the curved front display (a cylinder r 1.0 m about (0, 0.07, -0.15), y 0.07 +- 0.33, spanning +-0.74 rad
    // from the eye) shows one wide camera at the nose, looking straight ahead (slots 1 and 2 are unused); then the
    // side and aft displays, overhead, chin, footwell floor and the floor panes beside the seat (the shader's WF / WS /
    // WA / WO / WC / WL / WB)
    const vec3 Y(0, 1, 0), F(0, 0, -1);
    {
      FeedMount& m = out[0];
      m.dir = F; m.right = vec3(1, 0, 0); m.up = Y; m.back = vec3(0, 0, 1);
      m.tanX = 0.93f; m.tanY = 0.36f;   // what the display covers, seen from the eye
      m.screen = vec3(0.f, 0.07f, -1.0f); m.screenR = 1.0f;
      m.w = (int)(2.f * m.tanX * focal + 0.5f); m.h = (int)(2.f * m.tanY * focal + 0.5f);
      for (int k = 1; k <= 2; k++) { out[k] = m; out[k].w = out[k].h = 0; }   // (no camera: the atlas skips it)
    }
    out[3] = feedPanel(vec3(0.8f, -0.075f, -0.4f), vec3(-1, 0, 0), Y, vec2(0.34f, 0.345f), true, focal);
    out[4] = feedPanel(vec3(0.8f, -0.075f, -0.4f), vec3(-1, 0, 0), Y, vec2(0.34f, 0.345f), false, focal);
    out[5] = feedPanel(vec3(0.8f, -0.075f, 0.34f), vec3(-1, 0, 0), Y, vec2(0.25f, 0.345f), true, focal);
    out[6] = feedPanel(vec3(0.8f, -0.075f, 0.34f), vec3(-1, 0, 0), Y, vec2(0.25f, 0.345f), false, focal);
    out[7] = feedPanel(vec3(0.f, 0.403f, -0.55f), vec3(0, -1, 0), F, vec2(0.42f, 0.36f), false, focal);
    out[8] = feedPanel(vec3(0.f, -0.5f, -1.0f), vec3(0.f, 0.7509f, 0.6604f), Y, vec2(0.38f, 0.2f), false, focal);
    out[9] = feedPanel(vec3(0.f, -0.775f, -0.66f), vec3(0, 1, 0), F, vec2(0.4f, 0.34f), false, focal);
    out[10] = feedPanel(vec3(0.47f, -0.705f, 0.4f), vec3(-0.3714f, 0.9285f, 0.f), F, vec2(0.14f, 0.2f), true, focal);
    out[11] = feedPanel(vec3(0.47f, -0.705f, 0.4f), vec3(-0.3714f, 0.9285f, 0.f), F, vec2(0.14f, 0.2f), false, focal);
    out[0].nose = out[1].nose = out[2].nose = out[8].nose = true;   // the front camera and the chin pane
    return 12;
  }
  if (rig == 3) {   // XR-10: a flat front pane and a pane either side of the seat (mantis_sdf.glsl part 40, material 112)
    const vec3 Y(0, 1, 0);
    out[0] = feedPanel(vec3(0.f, -0.24f, -0.773f), vec3(0, 0, 1), Y, vec2(0.41f, 0.177f), false, focal);
    out[1] = feedPanel(vec3(0.547f, -0.285f, -0.16f), vec3(-1, 0, 0), Y, vec2(0.37f, 0.172f), true, focal);
    out[2] = feedPanel(vec3(0.547f, -0.285f, -0.16f), vec3(-1, 0, 0), Y, vec2(0.37f, 0.172f), false, focal);
    out[0].nose = true;
    return 3;
  }
  return 0;
}

// Where each rig camera's lens sits: the distance from the eye along its dir to the outside of the skin, and the tip
// of the nose (cockpit frame) for the forward cameras, measured once per craft from the airframe's own shape
// (Renderer::measureFeedMounts). Until then no camera is placed.
struct FeedMounts { bool ok = false; float skin[kMaxFeeds] = {}; vec3 nose; };
extern FeedMounts g_feedMounts[4];   // by rig
