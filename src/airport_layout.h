// Solace Express - airport ground plan, shared by the scenery placement, the AI traffic and (mirrored in GLSL in
// runwayMaterial) the terrain shader. Coordinates are runway-local: u along the runway (positive towards the
// direction of the first designator), v across it, positive to the right; `side` is the side the facilities are on.
//
//   paved airports (asphalt, regional / international): a parallel taxiway joined to the runway by exits, the apron
//   between the taxiway and the building line, and the car parks behind the buildings.
//   strips: a mown / gravel parking area beside the strip with the hangars behind it.
#pragma once
#include "world.h"
#include <algorithm>

struct AptLayout {
  float side = 1;
  bool paved = false;            // taxiway + apron (asphalt runway at a regional or international airport)
  float twV = 0, twHW = 0;       // parallel taxiway centreline and half width (paved only)
  float hold = 0;                // runway holding position on the exits (v)
  int nExit = 0; float exitU[6] = {};   // exits between the runway and the parallel taxiway
  float apV0 = 0, apV1 = 0, apU0 = 0, apU1 = 0;   // apron / parking area
  float bldV = 0;                // building line: fronts of the hangars and the terminal
  float lotV0 = 0, lotV1 = 0;    // car parks behind the building line (paved airports)
  // apron zones along u (apU0 < tie < stands / terminal ...): static tie-downs, the terminal block, AI stands
  float tieU0 = 0, tieU1 = 0, termU = 0, termHL = 0, standU0 = 0, standU1 = 0;
};

inline AptLayout aptLayout(const Airport& a, int ai) {
  AptLayout L;
  L.side = (ai & 1) ? 1.f : -1.f;
  float hw = a.width * 0.5f, len = a.length;
  L.paved = a.surface == SURF_ASPHALT && a.size > 0;
  L.hold = hw + 26.f;
  if (L.paved) {
    bool big = a.size == 2;
    L.twHW = big ? 11.5f : 7.5f;
    L.twV = hw + (big ? 95.f : 62.f);
    L.apV0 = L.twV + L.twHW;
    L.apV1 = L.apV0 + (big ? 120.f : 85.f);
    L.apU0 = -len * (big ? 0.26f : 0.22f);
    L.apU1 = len * (big ? 0.17f : 0.14f);
    L.bldV = L.apV1 + 2.f;
    L.lotV0 = L.bldV + (big ? 48.f : 34.f);
    L.lotV1 = L.lotV0 + (big ? 50.f : 34.f);
    float ap = L.apU1 - L.apU0;
    if (big) {
      L.nExit = 5;
      float e[5] = {-(len * 0.5f - 25.f), -len * 0.22f, len * 0.05f, len * 0.27f, len * 0.5f - 25.f};
      for (int i = 0; i < 5; i++) L.exitU[i] = e[i];
      L.tieU0 = L.apU0 + ap * 0.20f; L.tieU1 = L.apU0 + ap * 0.31f;
      L.termU = L.apU0 + ap * 0.51f; L.termHL = std::min(ap * 0.17f, 130.f);
      L.standU0 = L.apU0 + ap * 0.72f; L.standU1 = L.apU1 - 25.f;
    } else {
      L.nExit = 4;
      float e[4] = {-(len * 0.5f - 25.f), -len * 0.12f, len * 0.22f, len * 0.5f - 25.f};
      for (int i = 0; i < 4; i++) L.exitU[i] = e[i];
      L.tieU0 = L.apU0 + 12.f; L.tieU1 = L.apU0 + ap * 0.32f;
      L.termU = L.apU0 + ap * 0.47f; L.termHL = std::min(ap * 0.1f, 45.f);
      L.standU0 = L.apU0 + ap * 0.6f; L.standU1 = L.apU1 - 20.f;
    }
  } else {
    // strip: parking area off one side, a little in from the strip's first third
    float pu = -len * 0.18f;
    L.apV0 = hw + 16.f; L.apV1 = hw + 70.f;
    L.apU0 = pu - 60.f; L.apU1 = pu + 60.f;
    L.bldV = L.apV1 + 3.f;
    L.tieU0 = L.apU0 + 8.f; L.tieU1 = L.apU1 - 8.f;
    L.termU = pu; L.termHL = 0;
  }
  return L;
}

// runway-local <-> world (y passed through)
inline vec3 aptWorld(const Airport& a, float u, float v, float y) {
  float h = a.heading * DEG, s = sinf(h), c = cosf(h);
  return vec3(a.x + u * s + v * c, y, a.z - u * c + v * s);
}
inline vec2 aptLocal(const Airport& a, vec3 p) {
  float h = a.heading * DEG, s = sinf(h), c = cosf(h), dx = p.x - a.x, dz = p.z - a.z;
  return vec2(dx * s - dz * c, dx * c + dz * s);
}
// entity yaw whose front (+z) faces along the runway-local direction (du, dv)
inline float aptYaw(const Airport& a, float du, float dv) {
  vec3 d = aptWorld(a, du, dv, 0) - aptWorld(a, 0, 0, 0);
  return atan2f(d.x, d.z);
}
