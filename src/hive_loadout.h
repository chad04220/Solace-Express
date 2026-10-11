#pragma once
#include "common.h"
namespace hive {
// Stable equipment IDs, XR-40 only. Enemy role weapons are a separate catalogue.
enum class ForwardSet : uint8_t { Pulse, Kinetic, Heavy, Count };
enum class BombSet : uint8_t { Plasma, Penetrator, EMP, Count };
struct ForwardSpec { const char* name; const char* description; float speed, damage, interval, heat, cooling, charge, recoilImpulseNs, gravity; int rounds; vec3 colour; int burstCount = 1; float burstRecovery = 0; float equipmentMassKg = 0, roundMassKg = 0; };
struct BombSpec { const char* name; const char* description; float radius, damage, cooldown, disruption; int rounds; vec3 colour; float rackMassKg = 0, storeMassKg = 0; };
const ForwardSpec& forwardSpec(ForwardSet type);
const BombSpec& bombSpec(BombSet type);
struct Loadout {
  ForwardSet forward = ForwardSet::Pulse;
  BombSet bomb = BombSet::Plasma;
  float heat = 0, cooldown = 0, charge = 0, bombCooldown = 0;
  int rounds = 0, bombs = 0, burstRemaining = 0;
  bool overheated = false;
  void reset(ForwardSet gun = ForwardSet::Pulse, BombSet payload = BombSet::Plasma);
  // One bounded frame, no catch-up fire or ammo regeneration. Release cancels a charge.
  bool step(float dt, bool trigger);
  bool releaseBomb();
  float massKg() const; // Installed kit plus remaining rounds/stores; bounded by physical magazine capacities.
};
}
