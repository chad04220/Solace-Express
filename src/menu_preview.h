#pragma once
#include "common.h"

// One inspection contract for every aircraft menu. Each screen owns its state;
// viewing never touches flight controls, equipment, ownership, or launch state.
struct MenuPreviewView {
  static constexpr float defaultYaw = .7f, defaultPitch = .15f;
  static constexpr float minZoom = .75f, maxZoom = 1.35f;
  float yaw = defaultYaw, pitch = defaultPitch, zoom = 1.f, idle = 0.f;
  int craft = -1, context = -1, held = -1, heldSource = 0;
  float holdTime = 0.f;
  bool drag = false;
  void release() { drag = false; held = -1; heldSource = 0; holdTime = 0.f; }
  void reset() { yaw = defaultYaw; pitch = defaultPitch; zoom = 1.f; idle = 0.f; release(); }
  void select(int model, int scene = 0) {
    if (craft != model || context != scene) { craft = model; context = scene; reset(); }
  }
  void rotate(float delta) { yaw = std::remainder(yaw + delta, 2.f * PI); idle = 0.f; }
  void elevate(float delta) { pitch = clampf(pitch + delta, -.5f, 1.f); idle = 0.f; }
  void magnify(float delta) { zoom = clampf(zoom + delta, minZoom, maxZoom); idle = 0.f; }
  void action(int index) {
    drag = false;
    if (index < 2) rotate((index ? 1.f : -1.f) * 15.f * DEG);
    else if (index < 4) magnify(index == 2 ? -.1f : .1f);
    else reset();
  }
};

// The same five buttons and two group labels at every size. Short symbols stay
// readable on narrow stages instead of reducing whole words to a few pixels.
struct MenuPreviewControls {
  float x, y, width, scale, buttonWidth, step, buttonY, buttonHeight;
};
inline MenuPreviewControls menuPreviewControls(float x, float y, float width, float s) {
  const float pad = 10.f*s, gap = 6.f*s;
  const float bw = std::max(1.f, (width - 2.f*pad - 4.f*gap)/5.f);
  return {x+pad,y,width,s,bw,bw+gap,y+23.f*s,26.f*s};
}

struct MenuPreviewAxes { vec3 forward, right, up; };
// Aim a level-roll camera so the target lands at the exact stage centre. The
// older small-angle aim offset drifted badly at wide lenses/elevated views.
inline MenuPreviewAxes menuPreviewAxes(vec3 toTarget,float sx,float sy) {
  const vec3 f=normalize(toTarget);
  const float k=1.f/std::sqrt(1.f+sx*sx+sy*sy);
  const float pitch=asinf(clampf(f.y/(k*std::sqrt(1.f+sy*sy)),-1.f,1.f))-atanf(sy);
  const float yaw=atan2f(f.x,-f.z)-atan2f(sx,cosf(pitch)-sy*sinf(pitch));
  const vec3 forward(sinf(yaw)*cosf(pitch),sinf(pitch),-cosf(yaw)*cosf(pitch));
  const vec3 right(cosf(yaw),0,sinf(yaw));
  return {forward,right,cross(right,forward)};
}
