// Enemy-only deterministic combat. No renderer, aircraft catalogue, career or save dependencies.
#pragma once
#include "common.h"
#include <array>

namespace hive {
constexpr int MaxActors = 8, MaxProjectiles = 96, MaxEvents = 128;
constexpr int MaxObjectives = 3, MaxWaves = 6, MaxTargets = 8;
constexpr float FixedStep = 1.f / 60.f;
enum class Type : uint8_t { Needle = 0, Bastion = 1, Cantor = 2, Archon = 3 };
enum class Team : uint8_t { Player, Hive };
enum class Weapon : uint8_t { Pulse, Bomb, Lance };
// Shared spawn lifetime lets visuals derive age without changing Projectile layout.
float projectileLifetime(Weapon weapon);
enum class Phase : uint8_t { Approach, Telegraph, Firing, Recover };
enum class EventType : uint8_t { Spawn, Telegraph, Fire, Hit, Death, PlayerDamage, ObjectiveDamage, Relay, ScanComplete, Wave, Success, Failure };
enum class MissionKind : uint8_t { None, Practice, Recon, Defense, Strike };
enum class MissionStatus : uint8_t { Inactive, Active, Extract, Success, Failed };
struct RoleSpec { float cruise, burst, acceleration, turnAcceleration, hull, radius, clearance, charge, recovery; };
const RoleSpec& roleSpec(Type type);
struct WorldCallbacks {
  void* context = nullptr;
  float (*terrainHeight)(void*, float x, float z) = nullptr;
  bool (*lineOfSight)(void*, const vec3& from, const vec3& to) = nullptr;
  // Return first blocking fraction [0,1], or >1 for no hit. Receives a nearest-image, unwrapped segment.
  float (*sweepFraction)(void*, const vec3& from, const vec3& to, float radius) = nullptr;
  bool sweepIncludesTerrain = false; // Complete callback replaces terrain-only fallback.
};
// Local +X right, +Y up, -Z forward, shared with the enemy render transform.
struct BodyFrame { vec3 right, up, back; };
BodyFrame bodyFrame(vec3 forward, vec3 up = vec3(0,1,0));
struct BodyBox { vec3 center, halfExtent; };
constexpr int MaxPlayerBoxes = 12;
struct PlayerSnapshot {
  vec3 position, velocity, forward = vec3(0, 0, -1);
  float radius = 12; // Fallback only when bodyBoxCount == 0.
  vec3 up = vec3(0,1,0);
  std::array<BodyBox, MaxPlayerBoxes> bodyBoxes{};
  int bodyBoxCount = 0; // Authored aircraft body-local boxes; orientation includes bank via up.
  bool alive = true, scanning = false;
};
struct Actor {
  uint32_t id = 0;
  Type type = Type::Needle;
  bool alive = false;
  vec3 position, velocity, aim, previousPosition;
  float hull = 0, shield = 0, timer = 0, shotTimer = 0, relayInterrupted = 0;
  float burstRemaining = 0, burstCooldown = 0, shieldDisrupted = 0;
  Phase phase = Phase::Approach;
  int shotsRemaining = 0, escortWaves = 0;
  uint32_t relayTarget = 0;
  uint32_t shotsFired = 0;
};
// Actual authored role sockets in metres. Shot index selects alternating guns/payload bays.
vec3 weaponSocket(const Actor& actor, uint32_t shotIndex);
// Shaped, local hull/wing/pod proxy sweep; >1 means clear. No seam adjustment here.
float actorHitFraction(const Actor& actor, vec3 from, vec3 to, float projectileRadius = 0);
struct Projectile {
  uint32_t id = 0, owner = 0;
  Team team = Team::Hive;
  Weapon weapon = Weapon::Pulse;
  bool alive = false;
  vec3 position, velocity;
  float damage = 0, radius = 2, life = 0, blastRadius = 0;
};
struct Event { EventType type = EventType::Hit; uint32_t source = 0, target = 0; vec3 position; float value = 0; };
enum class ShotContactKind : uint8_t { None, World, Actor, Bomb };
struct ShotContact {
  ShotContactKind kind = ShotContactKind::None;
  float fraction = 2; // First contact on the supplied nearest-image segment; >1 means clear.
  uint32_t id = 0; // Actor/projectile namespace according to kind; 0 for World/None.
};
struct Objective {
  uint32_t id = 0;
  vec3 position;
  float radius = 60, health = 100, scan = 0;
  bool scanned = false;
};
struct Wave {
  float at = 0;
  vec3 position;
  std::array<Type, MaxActors> types{};
  int count = 0;
};
struct MissionConfig {
  MissionKind kind = MissionKind::None;
  std::array<Objective, MaxObjectives> objectives{};
  int objectiveCount = 0;
  std::array<Wave, MaxWaves> waves{};
  int waveCount = 0;
  std::array<uint32_t, MaxTargets> targetIds{};
  int targetCount = 0;
  vec3 extraction;
  float extractionRadius = 250, scanRange = 900, scanSeconds = 6;
};
struct Mission {
  MissionConfig config;
  MissionStatus status = MissionStatus::Inactive;
  std::array<bool, MaxTargets> targetsDestroyed{};
  int nextWave = 0, waveSpawned = 0;
  float elapsed = 0;
  bool waveAnnounced = false;
};
class Combat {
public:
  std::array<Actor, MaxActors> actors{};
  std::array<Projectile, MaxProjectiles> projectiles{};
  std::array<Event, MaxEvents> events{};
  int eventCount = 0;
  uint32_t droppedEvents = 0;
  Mission mission;
  // Actor IDs are monotonic within one reset epoch; 0 is invalid. A reset invalidates old IDs.
  void reset(uint32_t seed = 1, float wrapSize = 0);
  uint32_t spawn(Type type, vec3 position, vec3 velocity = vec3());
  Actor* find(uint32_t id);
  const Actor* find(uint32_t id) const;
  void clearEvents();
  void startMission(const MissionConfig& config);
  // Invalid/negative dt and paused calls do nothing. Cap to 0.25 s and 15 fixed steps; discard excess time.
  void step(float dt, const PlayerSnapshot& player, const WorldCallbacks& world = {}, bool paused = false);
  // Instant swept shot; nearest obstruction wins. Returns hit enemy ID, otherwise 0.
  uint32_t playerShot(vec3 from, vec3 to, float damage, const WorldCallbacks& world = {}, ShotContact* contact = nullptr);
  void playerBlast(vec3 center, float radius, float damage, const WorldCallbacks& world = {});
  // EMP removes shields and temporarily prevents receiving relay shields; never damages hull.
  void playerEMP(vec3 center, float radius, float disruptSeconds, const WorldCallbacks& world = {});
  // For world-origin rebasing only, not ordinary toroidal crossings (handled by wrapSize).
  void shiftOrigin(vec3 offset);
  int aliveCount() const;
  float simulatedSeconds() const { return time_; }
  vec3 displacement(vec3 from, vec3 to) const;
private:
  uint32_t rng_ = 1, nextId_ = 1, nextProjectileId_ = 1;
  float wrap_ = 0, accumulator_ = 0, time_ = 0;
  uint32_t random();
  vec3 canonical(vec3 position) const;
  void emit(EventType type, uint32_t source, uint32_t target, vec3 position, float value = 0);
  void damage(Actor& actor, float amount, uint32_t source);
  void tick(const PlayerSnapshot& player, const WorldCallbacks& world);
  void tickActor(Actor& actor, const PlayerSnapshot& player, const WorldCallbacks& world);
  void tickProjectile(Projectile& projectile, const PlayerSnapshot& player, const WorldCallbacks& world);
  void tickMission(const PlayerSnapshot& player, const WorldCallbacks& world);
  void fire(Actor& actor, const PlayerSnapshot& player);
  void blast(vec3 center, float radius, float amount, Team team, uint32_t source, const PlayerSnapshot& player, const WorldCallbacks& world);
};
} // namespace hive
