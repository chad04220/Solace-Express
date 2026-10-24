#include "xr40_battle.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
static int checks=0;
static void check(bool ok,const char* what){++checks;if(!ok){fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static hive::PlayerSnapshot pilot(){hive::PlayerSnapshot p;p.position={0,1200,-3000};return p;}
static void clearWave(hive::Combat& c,xr40::Battle& b){
  for(auto& a:c.actors)if(a.alive)c.playerShot(a.position+vec3(0,80,0),a.position-vec3(0,80,0),100000);
  b.observe(c);c.clearEvents();for(auto& p:c.projectiles)p.alive=false;
}
int main(){
  for(int mode=0;mode<4;++mode)for(int diff=0;diff<3;++diff){
    hive::Combat c;xr40::Battle b;auto p=pilot();b.begin({xr40::Mode(mode),xr40::Difficulty(diff),40},c,p.position,{0,400,0});
    const int count=mode==3?7:b.waveGoal();
    for(int wave=0;wave<count;++wave){
      c.step(1.f/60,p);b.observe(c);c.clearEvents();
      check(c.aliveCount()>=2 && c.aliveCount()<=6,"bounded wave budget");
      bool archon=false;for(const auto& a:c.actors)if(a.alive)archon|=a.type==hive::Type::Archon;
      if(mode==2 && wave==3)check(archon,"gauntlet ends with command ship");
      if(mode==3 && (wave+1)%3==0)check(archon,"siege periodic command ship");
      clearWave(c,b);b.update(1.f/60,c,p,100);
      check(b.stats.waves==wave+1,"each cleared wave scored once");
      if(mode!=3 && wave+1==count)check(b.phase==xr40::Phase::Extraction,"finite operation requires recovery");
      else if(wave+1<count){
        check(b.phase==xr40::Phase::Relief,"safe intermission between waves");
        for(int j=0;j<101;++j)b.update(.25f,c,p,100);
        check(b.phase==xr40::Phase::Combat,"next wave starts after intermission");
      }
    }
    p.position=b.extraction;b.update(1.f/60,c,p,100);
    check(b.phase==xr40::Phase::Victory,"all modes can extract to victory");
    const auto score=b.stats.score;b.update(10,c,p,100);b.finish(xr40::Phase::Defeat,"late");
    check(b.stats.score==score && b.phase==xr40::Phase::Victory,"terminal state cannot award twice or be overwritten");
  }
  {
    hive::Combat c;xr40::Battle b;auto p=pilot();b.begin({},c,p.position,{0,400,0});
    b.update(std::numeric_limits<float>::quiet_NaN(),c,p,100);b.update(-1,c,p,100);
    check(b.stats.elapsed==0,"invalid elapsed time ignored");
    b.update(10,c,p,100);check(b.stats.elapsed==.25f,"hitch catchup bounded");
    p.position.x=17000;for(int i=0;i<40;++i)b.update(.25f,c,p,100);
    check(b.active() && b.boundary>9,"boundary grace allows return");
    p.position.x=0;b.update(.1f,c,p,100);check(b.boundary==0,"return cancels boundary timer");
    p.position.x=17000;for(int i=0;i<81;++i)b.update(.25f,c,p,100);
    check(b.phase==xr40::Phase::Defeat,"leaving battle area fails without teleporting");
  }
  {
    hive::Combat c;xr40::Battle b;auto p=pilot();b.begin({xr40::Mode::Defense},c,p.position,{0,400,0});
    c.mission.config.objectives[0].health=0;b.update(.1f,c,p,100);
    check(b.phase==xr40::Phase::Defeat,"relay destruction ends defense");
    b.begin({},c,p.position,{0,400,0});p.alive=false;b.update(.1f,c,p,100);
    check(b.phase==xr40::Phase::Defeat,"player loss ends battle");
  }
  {
    hive::Combat c;xr40::Battle b;auto p=pilot();b.begin({},c,p.position,{0,400,0});
    check(b.updateCloak(.1f,true,false),"cloak activates");
    check(!b.updateCloak(.1f,true,true),"weapon use breaks cloak");
    for(int i=0;i<840;++i)b.updateCloak(1.f/60,true,false);
    check(!b.updateCloak(.1f,true,false) && b.cloak<.02f,"cloak exhausts and cannot immediately reactivate");
    for(int i=0;i<500;++i)b.updateCloak(1.f/60,false,false);
    check(b.updateCloak(.1f,true,false),"cloak recharges");
    check(!b.resupply(.25f,b.supply,80,false),"service requires low speed");
    check(!b.resupply(.25f,b.supply,0,true),"service disallows active weapons");
    int done=0;for(int i=0;i<21;++i)done+=b.resupply(.25f,b.supply,0,false);
    check(done==1 && b.stats.resupplies==1,"service completes once per dwell");
    for(int i=0;i<21;++i)done+=b.resupply(.25f,b.supply,0,false);
    check(done==1,"resupply cooldown blocks repeated free repairs");
  }
  {
    auto shots=[](float signature){
      hive::Combat c;c.reset(7);auto p=pilot();p.position={0,600,0};p.signature=signature;
      auto id=c.spawn(hive::Type::Needle,{0,600,-1000},{0,0,300});
      for(int i=0;i<180;++i){auto* a=c.find(id);a->position={0,600,-1000};a->velocity={0,0,300};c.step(1.f/60,p);c.clearEvents();}
      return c.find(id)->shotsFired;
    };
    check(shots(1)>0,"visible player acquired and fired on");
    check(shots(.12f)==0,"cloaked player outside reduced sensor range is not acquired");
  }
  {
    hive::Combat a,b;xr40::Battle da,db;auto p=pilot();da.begin({},a,p.position,{0,400,0});db.begin({},b,p.position,{0,400,0});
    for(int i=0;i<600;++i){a.step(1.f/60,p);a.clearEvents();}
    for(int i=0;i<200;++i){b.step(.05f,p);b.clearEvents();}
    for(int i=0;i<hive::MaxActors;++i)check(a.actors[i].id==b.actors[i].id && length(a.actors[i].position-b.actors[i].position)<.02f,"60 and 20 fps combat trajectories agree");
  }
  printf("XR-40 battle director: %d checks passed\n",checks);
}
