#pragma once
#include <algorithm>

// A shallow loading strip leaves the middle of every aircraft photograph open.
// The native fallback camera uses the same clear region as the UI.
struct LoadingLayout {
  float scale, x, y, width, height, artTop, artBottom;
};
inline LoadingLayout loadingLayout(float w, float h) {
  const float s = std::max(.35f, std::min(w / 1120.f, h / 720.f));
  const float top = h - 188.f * s;
  return {s, 24.f*s, top, w-48.f*s, 170.f*s, 104.f*s, top-14.f*s};
}

// Shared between the live hangar camera and its overlay. All values are pixels.
struct HangarLayout {
  float leftWidth, rightWidth, rightX;
  float previewX, previewY, previewWidth, previewHeight;
};
inline HangarLayout hangarLayout(float x, float y, float w, float h, float s) {
  const float left = std::min(w * 0.25f, 300 * s);
  const float right = std::min(w * 0.29f, 360 * s);
  const float rightX = x + w - right, viewX = x + left + 16 * s;
  return {left, right, rightX, viewX, y + 82 * s,
          std::max(1.f, rightX - 16 * s - viewX), std::max(1.f, h - 138 * s)};
}
