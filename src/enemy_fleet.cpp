#include "enemy_fleet.h"
#include "mesh_validation.h"

const EnemyCraftSpec kEnemyCraftSpecs[kEnemyCraftTypes] = {
  {"needle", "fast light interceptor", {-6.05f, -.602f, -6.70f}, {6.05f, 1.67f, 5.40f}, 8, 12},
  {"bastion", "armored medium bomber", {-8.3f, -1.34f, -6.4f}, {8.3f, 1.47f, 6.f}, 11, 9},
  {"cantor", "support relay", {-7.2f, -1.50f, -7.6f}, {7.2f, 3.60f, 7.4f}, 10, 11},
  {"archon", "boss command ship", {-17.f, -2.80f, -19.f}, {17.f, 6.8f, 18.6f}, 24, 16},
};

static bool validType(EnemyCraftType type) { int i = static_cast<int>(type); return i >= 0 && i < kEnemyCraftTypes; }
bool enemyCraftValid(const EnemyCraftVisual& c) {
  if (!validType(c.type) || !aircraftMesh::finite(c.pos.x) || !aircraftMesh::finite(c.pos.y) || !aircraftMesh::finite(c.pos.z)) return false;
  for (float f : c.rot) if (!aircraftMesh::finite(f)) return false;
  for (float f : c.state) if (!aircraftMesh::finite(f)) return false;
  vec3 x(c.rot[0], c.rot[1], c.rot[2]), y(c.rot[3], c.rot[4], c.rot[5]), z(c.rot[6], c.rot[7], c.rot[8]);
  return fabsf(dot(x, x) - 1) < .002f && fabsf(dot(y, y) - 1) < .002f && fabsf(dot(z, z) - 1) < .002f &&
         fabsf(dot(x, y)) < .002f && fabsf(dot(x, z)) < .002f && fabsf(dot(y, z)) < .002f && dot(cross(x, y), z) > .998f;
}
EnemyFleetUniforms packEnemyFleet(const EnemyCraftVisual* crafts, int count) {
  EnemyFleetUniforms u;
  if (!crafts) return u;
  for (int i = 0; i < std::clamp(count, 0, kMaxEnemyCraft); ++i) {
    const auto& c = crafts[i];
    if (!enemyCraftValid(c)) continue;
    int n = u.count++, type = static_cast<int>(c.type);
    u.posRadius[n][0] = c.pos.x; u.posRadius[n][1] = c.pos.y; u.posRadius[n][2] = c.pos.z;
    u.posRadius[n][3] = kEnemyCraftSpecs[type].radius;
    memcpy(u.rot[n], c.rot, sizeof c.rot); memcpy(u.state[n], c.state, sizeof c.state);
    u.state[n][0] = clampf(c.state[0], 0, 1); u.state[n][1] = clampf(c.state[1], 0, 1);
    u.type[n] = type;
  }
  return u;
}
float enemyCraftLowestY(const EnemyCraftVisual& c) {
  if (!validType(c.type)) return c.pos.y;
  const auto& s = kEnemyCraftSpecs[static_cast<int>(c.type)];
  float y = c.pos.y;
  for (int j = 0; j < 3; ++j) y += c.rot[j * 3 + 1] * (c.rot[j * 3 + 1] >= 0 ? s.boundsMin[j] : s.boundsMax[j]);
  return y;
}
EnemyVtolPose sampleEnemyVtol(EnemyCraftType type, vec3 anchor, float floorY, double seconds, float yaw) {
  EnemyVtolPose p;
  if (!validType(type)) type = EnemyCraftType::Fighter;
  p.visual.type = type;
  const auto& s = kEnemyCraftSpecs[static_cast<int>(type)];
  double phase = aircraftMesh::finite(seconds) ? fmod(seconds, double(kEnemyVtolPeriod)) : 0;
  if (phase < 0) phase += kEnemyVtolPeriod;
  float t = float(phase), h = 0, bob = 0;
  if (t >= 4 && t < 10) { p.phase = EnemyVtolPhase::Takeoff; h = smoothstepf(4, 10, t); }
  else if (t >= 10 && t < 24) {
    p.phase = EnemyVtolPhase::Hover; h = 1;
    // Envelope has zero value/slope at both ends, preserving smooth takeoff/landing joins.
    float env = sinf(PI * (t - 10) / 14); bob = .18f * env * env * sinf((t - 10) * 1.3f);
  } else if (t >= 24 && t < 32) { p.phase = EnemyVtolPhase::Landing; h = 1 - smoothstepf(24, 32, t); }
  yaw = aircraftMesh::finite(yaw) ? yaw : 0;
  float c = cosf(yaw), sn = sinf(yaw);
  float r[9] = {c, 0, -sn, 0, 1, 0, sn, 0, c}; memcpy(p.visual.rot, r, sizeof r);
  p.visual.pos = {aircraftMesh::finite(anchor.x) ? anchor.x : 0, 0, aircraftMesh::finite(anchor.z) ? anchor.z : 0};
  // Parked = stable low levitation; hull and pods never become landing legs.
  p.visual.pos.y = (aircraftMesh::finite(floorY) ? floorY : 0) - s.boundsMin.y + .35f + s.hoverHeight * h + bob;
  p.visual.state[0] = .35f + .65f * h;
  p.visual.state[1] = smoothstepf(12, 14, t) * (1 - smoothstepf(20, 22, t));
  return p;
}
