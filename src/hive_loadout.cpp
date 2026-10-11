#include "hive_loadout.h"
#include "finite_float.h"
#include <cmath>
namespace hive {
namespace {
const ForwardSpec guns[] = {
 {"Pulse laser", "Fast pulsed emitter / moderate damage / heat limited", 2400, 18, .12f, .095f, .24f, 0, 0, 0, 500, vec3(1,.28f,.38f), 1, 0, 220, 0},
 {"Kinetic burst", "High velocity cannon / ballistic drop / finite ammunition", 1050, 24, .075f, .05f, .19f, 0, 360, 1, 360, vec3(1,.72f,.3f), 3, .35f, 180, .35f},
 {"Charged heavy", "Hold to charge / armor punch / slow thermal recovery", 1650, 120, 1.25f, .62f, .16f, .85f, 1320, 0, 48, vec3(.3f,.72f,1), 1, 0, 310, .8f}
};
const BombSpec bombs[] = {
 {"Plasma", "Wide thermal blast / strong falloff / limited stores", 230, 240, 2.5f, 0, 6, vec3(.65f,.28f,1), 70, 85},
 {"Penetrator", "Compact conventional blast / high direct damage", 90, 520, 1.5f, 0, 8, vec3(1,.65f,.25f), 70, 110},
 {"EMP", "Disrupts shields and relay / no hull damage", 420, 0, 4, 12, 4, vec3(.25f,.8f,1), 70, 72}
};
}
const ForwardSpec& forwardSpec(ForwardSet t) { return guns[std::clamp((int)t,0,2)]; }
const BombSpec& bombSpec(BombSet t) { return bombs[std::clamp((int)t,0,2)]; }
void Loadout::reset(ForwardSet g, BombSet b) {
 forward=(ForwardSet)std::clamp((int)g,0,2); bomb=(BombSet)std::clamp((int)b,0,2);
 heat=cooldown=charge=bombCooldown=0; burstRemaining=0; overheated=false; rounds=forwardSpec(forward).rounds; bombs=bombSpec(bomb).rounds;
}
bool Loadout::step(float dt, bool trigger) {
 if (!floatValidation::finite(dt) || dt<=0) return false;
 dt=std::min(dt,.25f); const auto& s=forwardSpec(forward);
 heat=std::max(0.f,heat-s.cooling*dt); cooldown=std::max(0.f,cooldown-dt); bombCooldown=std::max(0.f,bombCooldown-dt);
 if (overheated && heat<.3f) overheated=false;
 if (!trigger) { charge=0; burstRemaining=0; return false; }
 if (overheated || rounds<=0 || cooldown>0) { charge=0; return false; }
 charge+=dt; if (charge<s.charge) return false;
 if (heat+s.heat>1) { overheated=true; charge=0; return false; }
 if(burstRemaining<=0) burstRemaining=s.burstCount;
 --burstRemaining; heat+=s.heat; cooldown=s.interval+(burstRemaining==0?s.burstRecovery:0); charge=0; --rounds; return true;
}
float Loadout::massKg() const {
 const auto& g=forwardSpec(forward); const auto& b=bombSpec(bomb);
 return g.equipmentMassKg+b.rackMassKg+std::clamp(rounds,0,g.rounds)*g.roundMassKg+std::clamp(bombs,0,b.rounds)*b.storeMassKg;
}
bool Loadout::releaseBomb() { if (bombs<=0 || bombCooldown>0) return false; --bombs; bombCooldown=bombSpec(bomb).cooldown; return true; }
}
