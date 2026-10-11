// Enemy craft authoring/runtime boundary. Metres; +X right, +Y up, -Z forward.
#pragma once
#include "common.h"

constexpr int kEnemyCraftTypes = 4;
constexpr int kMaxEnemyCraft = 8;
constexpr float kEnemyVtolPeriod = 36.f;
enum class EnemyCraftType : int { Fighter = 0, Bomber = 1, Support = 2, Boss = 3 };
struct EnemyCraftSpec {
  const char* id;
  const char* role;
  vec3 boundsMin, boundsMax; // conservative local bounds, including embedded lift pods
  float radius;
  float hoverHeight; // visual demo height above the parked position, not flight performance
};
extern const EnemyCraftSpec kEnemyCraftSpecs[kEnemyCraftTypes];

struct EnemyCraftVisual {
  EnemyCraftType type = EnemyCraftType::Fighter;
  vec3 pos;
  float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // body -> world; rigid, column-major
  float state[4] = {0.35f, 0, 0, 0}; // lift power, role activation (0..1), reserved, reserved
};
struct EnemyFleetUniforms {
  int count = 0;
  float posRadius[kMaxEnemyCraft][4] = {};
  float rot[kMaxEnemyCraft][9] = {};
  float state[kMaxEnemyCraft][4] = {};
  int type[kMaxEnemyCraft] = {};
};
enum class EnemyVtolPhase { Parked, Takeoff, Hover, Landing };
struct EnemyVtolPose { EnemyCraftVisual visual; EnemyVtolPhase phase = EnemyVtolPhase::Parked; };

bool enemyCraftValid(const EnemyCraftVisual& craft);
// Compacts valid entries, clamps controls, rejects scaled/non-finite transforms. No GL needed.
EnemyFleetUniforms packEnemyFleet(const EnemyCraftVisual* crafts, int count);
// Lowest point of the rotated conservative AABB. Caller must supply a valid rigid transform.
float enemyCraftLowestY(const EnemyCraftVisual& craft);
// floorY is the maximum terrain/obstacle height over the whole footprint, not a centre sample.
// Kinematic authoring demo: yaw only; no AI, weapon damage, collision response or flight physics.
EnemyVtolPose sampleEnemyVtol(EnemyCraftType type, vec3 anchor, float floorY, double seconds, float yaw = 0.f);

// Independent draw capacity for every live combat projectile, never sharing player/relay FX slots.
constexpr int kMaxHiveOrdnance = 96;
struct HiveOrdnanceVisual {
  vec3 tail, head;
  float radius = .10f;
  vec3 color = vec3(1.f, .35f, .08f);
  float intensity = 1.f;
  int kind = 0; // 0 bolt, 1 unpowered penetrator, 2 EMP, 3 plasma
};
