// Solace Express - the UFO encounter's timeline (Codex's test): hatch, laugh and wave beats, the 26.5 s end, J+K summon, departure on landing
#include "../src/game.h"
#include <cstdio>
#include <cmath>
struct GameTest {
 static int run(){
  g_world.build();buildStory();g_audio.init(48000);static Game g;g.initHeadless();g.debugScene("ufo12_2");
  int failures=0;auto check=[&](bool ok,const char* name){printf("%s: %s\n",name,ok?"PASS":"FAIL");failures+=!ok;};
  g.plane.onGround=false;g.crashed=false;g.paused=false;
  const float times[]={0,8,9,10,12,15.8f,16.3f,17,18.5f,19,20,21,22,23,26.5f,26.51f};
  for(float t:times){g.ufo.on=true;g.ufo.t=t;g.updateUfo(0);
   float hatch=smoothstepf(8,10,t)*(1-smoothstepf(21,23,t));
   float laugh=smoothstepf(15.8f,16.3f,t)*(1-smoothstepf(18.3f,18.8f,t));
   float wave=smoothstepf(18.4f,18.9f,t)*(1-smoothstepf(20.8f,21.3f,t));
   bool ok=fabsf(g.ufo.hatch-hatch)<1e-6f&&fabsf(g.ufo.laugh-laugh)<1e-6f&&fabsf(g.ufo.wave-wave)<1e-6f&&g.ufo.on==(t<=26.5f);
   char name[100];snprintf(name,sizeof name,"Encounter t=%.2f hatch=%.3f laugh=%.3f wave=%.3f active=%d",t,g.ufo.hatch,g.ufo.laugh,g.ufo.wave,g.ufo.on);check(ok,name);
  }
  g.ufo.on=false;g.ufo.next=1000;g.ufoSummon=0;g.in.down['J']=g.in.down['K']=true;
  g.updateUfo(.99f);check(!g.ufo.on,"J+K below one second does not summon");g.updateUfo(.02f);check(g.ufo.on,"J+K after one second summons");
  g.in.down['J']=g.in.down['K']=false;g.ufo.t=12;g.plane.onGround=true;g.updateUfo(0);check(g.ufo.t==23,"Landing begins departure");
  printf("%d failures\n",failures);return failures?1:0;
 }
};
int main(){return GameTest::run();}
