// XR-40's standalone sortie director. No career, renderer, platform or disk dependencies.
#pragma once
#include "hive_combat.h"
#include "hive_loadout.h"
#include <string>

namespace xr40 {
enum class Mode { Intercept, Defense, Gauntlet, Survival, Count };
enum class Difficulty { Cadet, Pilot, Ace, Count };
enum class Phase { Ready, Combat, Relief, Extraction, Victory, Defeat, Aborted };
struct Config {
  Mode mode = Mode::Intercept;
  Difficulty difficulty = Difficulty::Pilot;
  uint32_t seed = 40;
};
const char* modeName(Mode mode);
const char* briefing(Mode mode);
const char* difficultyName(Difficulty difficulty);
struct Stats {
  int kills = 0, hits = 0, score = 0, waves = 0, resupplies = 0;
  float elapsed = 0, damage = 0;
};
class Battle {
public:
  Config config;
  Stats stats;
  Phase phase = Phase::Ready;
  vec3 center, extraction, supply;
  int wave = 0;
  float relief = 0, boundary = 0, service = 0, serviceCooldown = 0, cloak = 1;
  float objectiveHealth = 900;
  std::string reason;
  void begin(Config settings, hive::Combat& combat, vec3 arena, vec3 recovery, const hive::WorldCallbacks& world = {});
  void observe(const hive::Combat& combat); // consume events once, before Combat::clearEvents
  void update(float dt, hive::Combat& combat, const hive::PlayerSnapshot& player, float hull,
              const hive::WorldCallbacks& world = {});
  bool updateCloak(float dt, bool requested, bool firing);
  bool resupply(float dt, vec3 position, float speed, bool weaponsBusy);
  void finish(Phase outcome, const std::string& why);
  bool active() const { return phase==Phase::Combat || phase==Phase::Relief || phase==Phase::Extraction; }
  int waveGoal() const;
  float damageScale() const;
  float arenaRadius() const { return 16000.f; }
  std::string objective(int hostiles) const;
private:
  bool cloakLocked_ = false;
  void nextWave(hive::Combat& combat, const hive::WorldCallbacks& world);
};
} // namespace xr40
