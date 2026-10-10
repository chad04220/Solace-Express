#pragma once
#include <algorithm>

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
