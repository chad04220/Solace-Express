#include "game.h"
#include "models.h"
#include <cstdio>
#include <cstring>
struct GameTest {
 static void probe(){
  Game g;g.headless=true;g.screen=SCR_MENU;
  for(int model=0;model<kAircraftCount;model++)for(int slot=0;slot<2;slot++){
   g.prewarmCraft=model;g.prewarmInside=slot!=0;g.realTime=3.f;g.update(1.f/60.f);const FrameParams fp=g.buildFrame();float packed[96];packModelOf(model,packed);
   printf("model=%d slot=%d realTime=%.9g Pr=%.9g,%.9g,%.9g,%.9g PS=%.9g,%.9g,%.9g,%.9g health=%.9g,%.9g,%.9g,%.9g canonicalPacked=%d\n",model,slot,g.realTime,fp.plane.Pr[0],fp.plane.Pr[1],fp.plane.Pr[2],fp.plane.Pr[3],fp.plane.PS[0],fp.plane.PS[1],fp.plane.PS[2],fp.plane.PS[3],fp.plane.engineHealth[0],fp.plane.engineHealth[1],fp.plane.engineHealth[2],fp.plane.engineHealth[3],memcmp(packed,fp.plane.M,sizeof packed)==0);
  }
 }
};
int main(int argc,char**argv){if(argc!=2)return 2;g_world.build(argv[1],"prebuilt-mesh-review-v346-world");GameTest::probe();}
