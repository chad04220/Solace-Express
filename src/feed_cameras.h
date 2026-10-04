// Solace Express - camera feeds for the research jets' cockpit displays
#pragma once
#include "common.h"

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
  int w = 0, h = 0;            // its picture, in pixels
};

// one camera of a rig, in the cockpit frame (eye at the origin, -z forward, +y up, body axes)
struct FeedMount {
  vec3 dir;                  // from the eye through the middle of its panel: the lens sits where this leaves the skin
  vec3 right, up, back;      // the panel's frame: the picture is upright on the panel and keeps its left and right
  float tanX, tanY;
  int w, h;
};

inline int feedRigOf(int special) { return special == 1 ? 1 : special == 2 ? 2 : 0; }   // 1 XR-9, 2 XR-11

// A flat panel: centre c, normal n (facing the pilot), "up" hint u, half size s; mirrored to the left side when
// left. The camera looks out through it (along -n) with the panel's own axes, about a quarter wider than the panel
// looks from the eye.
inline FeedMount feedPanel(vec3 c, vec3 n, vec3 u, vec2 s, bool left, float quality) {
  if (left) { c.x = -c.x; n.x = -n.x; }
  n = normalize(n);
  vec3 t = normalize(cross(u, n)), b = cross(n, t);
  float dist = fabsf(dot(c, n));
  FeedMount m;
  m.dir = normalize(c); m.right = t; m.up = b; m.back = n;
  m.tanY = 1.25f * s.y / dist; m.tanX = m.tanY * s.x / s.y;
  m.h = (int)(s.y * 600.f * quality + 0.5f); m.w = (int)(m.h * s.x / s.y + 0.5f);
  return m;
}

// the rig of a research jet (empty for anything else); quality scales the pictures' resolution
inline int feedRig(int rig, float quality, FeedMount out[kMaxFeeds]) {
  if (rig == 1) {
    // XR-9: a panoramic display on a cylinder around the eye (r 0.64 m, +-1.25 rad, y 0.02 +- 0.30), fed by three
    // cameras 0.833 rad apart (each a third of it), and a side display bay either side
    const float seg = 1.25f * 2.f / 3.f, tx = tanf(seg * 0.5f), ty = (0.30f / 0.64f) / cosf(seg * 0.5f);
    for (int k = 0; k < 3; k++) {
      float c = (k - 1) * seg;
      FeedMount& m = out[k];
      m.dir = vec3(sinf(c), 0.f, -cosf(c));
      m.right = vec3(cosf(c), 0.f, sinf(c)); m.up = vec3(0, 1, 0); m.back = vec3(-sinf(c), 0.f, cosf(c));
      m.tanX = tx; m.tanY = ty;
      m.h = (int)(260.f * quality + 0.5f); m.w = (int)(m.h * tx / ty + 0.5f);
    }
    out[3] = feedPanel(vec3(0.635f, 0.04f, 0.24f), vec3(-1, 0, 0), vec3(0, 1, 0), vec2(0.3f, 0.2f), true, quality * 1.3f);
    out[4] = feedPanel(vec3(0.635f, 0.04f, 0.24f), vec3(-1, 0, 0), vec3(0, 1, 0), vec2(0.3f, 0.2f), false, quality * 1.3f);
    return 5;
  }
  if (rig == 2) {
    // XR-11: front panel and its two wings, side and aft displays, overhead, chin, footwell floor and the floor panes
    // beside the seat (the shader's WF / WW / WS / WA / WO / WC / WL / WB panes)
    const vec3 Y(0, 1, 0), F(0, 0, -1);
    out[0] = feedPanel(vec3(0.f, 0.07f, -1.2f), vec3(0.f, 0.2425f, 0.9701f), Y, vec2(0.4f, 0.33f), false, quality);
    out[1] = feedPanel(vec3(0.6f, 0.07f, -0.93f), vec3(-0.7686f, 0.1774f, 0.6147f), Y, vec2(0.24f, 0.33f), true, quality);
    out[2] = feedPanel(vec3(0.6f, 0.07f, -0.93f), vec3(-0.7686f, 0.1774f, 0.6147f), Y, vec2(0.24f, 0.33f), false, quality);
    out[3] = feedPanel(vec3(0.8f, -0.075f, -0.4f), vec3(-1, 0, 0), Y, vec2(0.34f, 0.345f), true, quality);
    out[4] = feedPanel(vec3(0.8f, -0.075f, -0.4f), vec3(-1, 0, 0), Y, vec2(0.34f, 0.345f), false, quality);
    out[5] = feedPanel(vec3(0.8f, -0.075f, 0.34f), vec3(-1, 0, 0), Y, vec2(0.25f, 0.345f), true, quality);
    out[6] = feedPanel(vec3(0.8f, -0.075f, 0.34f), vec3(-1, 0, 0), Y, vec2(0.25f, 0.345f), false, quality);
    out[7] = feedPanel(vec3(0.f, 0.403f, -0.55f), vec3(0, -1, 0), F, vec2(0.42f, 0.36f), false, quality);
    out[8] = feedPanel(vec3(0.f, -0.5f, -1.0f), vec3(0.f, 0.7509f, 0.6604f), Y, vec2(0.38f, 0.2f), false, quality);
    out[9] = feedPanel(vec3(0.f, -0.775f, -0.66f), vec3(0, 1, 0), F, vec2(0.4f, 0.34f), false, quality);
    out[10] = feedPanel(vec3(0.47f, -0.705f, 0.4f), vec3(-0.3714f, 0.9285f, 0.f), F, vec2(0.14f, 0.2f), true, quality * 1.4f);
    out[11] = feedPanel(vec3(0.47f, -0.705f, 0.4f), vec3(-0.3714f, 0.9285f, 0.f), F, vec2(0.14f, 0.2f), false, quality * 1.4f);
    return 12;
  }
  return 0;
}

// Where each rig camera's lens sits: the distance from the eye along its dir to the outside of the skin, measured
// once per craft from the airframe's own shape (Renderer::measureFeedMounts). Until then no camera is placed.
struct FeedMounts { bool ok = false; float skin[kMaxFeeds] = {}; };
extern FeedMounts g_feedMounts[3];   // by rig
