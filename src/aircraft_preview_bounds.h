// Metric inspection bounds of the exact exterior mesh uploaded to the GPU.
// Read once from existing CPU data; no cache-format change or GPU readback.
#pragma once
#include "common.h"

struct AircraftPreviewBounds {
  vec3 left, right, nose, tail;
  bool valid = false;
};

inline AircraftPreviewBounds aircraftPreviewBounds(const std::vector<float>& vertices) {
  AircraftPreviewBounds b;
  if (vertices.empty() || vertices.size() % 8) return b;
  for (size_t i = 0; i < vertices.size(); i += 8) {
    const vec3 p(vertices[i], vertices[i+1], vertices[i+2]);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
    if (!b.valid) { b.left = b.right = b.nose = b.tail = p; b.valid = true; }
    if (p.x < b.left.x) b.left = p;
    if (p.x > b.right.x) b.right = p;
    if (p.z < b.nose.z) b.nose = p;
    if (p.z > b.tail.z) b.tail = p;
  }
  b.valid = b.valid && b.right.x > b.left.x && b.tail.z > b.nose.z;
  return b;
}
