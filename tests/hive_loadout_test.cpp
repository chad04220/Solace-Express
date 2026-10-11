#include "hive_loadout.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <cstdio>
int main() {
 using namespace hive;
 Loadout l;
 for(int kind=0;kind<3;++kind) {
   l.reset((ForwardSet)kind,(BombSet)kind);
   const auto& s=forwardSpec(l.forward);
   assert(l.rounds==s.rounds && l.bombs==bombSpec(l.bomb).rounds);
   const float fullMass=l.massKg();
   assert(fullMass>0 && fullMass<2000);
   --l.rounds; assert(std::fabs(l.massKg()-(fullMass-s.roundMassKg))<.002f); ++l.rounds;
   --l.bombs; assert(std::fabs(l.massKg()-(fullMass-bombSpec(l.bomb).storeMassKg))<.002f); ++l.bombs;
   l.rounds+=100; l.bombs+=100; assert(std::fabs(l.massKg()-fullMass)<.002f); l.reset((ForwardSet)kind,(BombSet)kind);
   int fired=0; for(int i=0;i<60000 && l.rounds>0;++i) fired+=l.step(1.f/60,true);
   assert(fired==s.rounds && l.rounds==0 && !l.step(.25f,true));
   assert(l.heat>=0 && l.heat<=1);
   l.reset((ForwardSet)kind,(BombSet)kind);
   int initial=l.bombs; assert(l.releaseBomb() && !l.releaseBomb() && l.bombs==initial-1);
   for(int i=0;i<300;++i) l.step(1.f/60,false);
   assert(l.releaseBomb());
   int before=l.rounds; assert(!l.step(-1,true)); assert(!l.step(std::numeric_limits<float>::quiet_NaN(),true)); assert(l.rounds==before);
 }
 l.reset(ForwardSet::Heavy,BombSet::EMP);
 assert(!l.step(.25f,true)); assert(!l.step(.25f,true)); assert(l.charge>.4f);
 assert(!l.step(.1f,false) && l.charge==0);
 for(int i=0;i<4;++i) l.step(.25f,true);
 assert(l.rounds==forwardSpec(ForwardSet::Heavy).rounds-1);
 l.reset((ForwardSet)99,(BombSet)99); assert(l.forward==ForwardSet::Heavy && l.bomb==BombSet::EMP);
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
   l.reset();l.heat=.4f;l.cooldown=.3f;l.charge=.2f;l.bombCooldown=.5f;const auto before=l;
   assert(!l.step(bad,true));
   assert(l.rounds==before.rounds && l.bombs==before.bombs && l.heat==before.heat && l.cooldown==before.cooldown && l.charge==before.charge && l.bombCooldown==before.bombCooldown);
 }
 puts("hive_loadout_test: ammunition, thermal, charge, bomb cooldown, invalid time checks passed");
}
