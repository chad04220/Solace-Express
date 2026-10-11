#include "../src/hive_combat.h"
#include "../src/finite_float.h"
#include <cstdio>
#include <limits>
#include <cmath>
using namespace hive;
static int fails=0,checks=0;
static void check(bool value,const char* label){++checks;if(!value){++fails;std::printf("FAIL %s\n",label);}}
static int events(const Combat& c,EventType type){int n=0;for(int i=0;i<c.eventCount;++i)n+=c.events[i].type==type;return n;}
static float wall(void*,const vec3& a,const vec3& b,float){if(a.x<50 && b.x>=50)return (50-a.x)/(b.x-a.x);return 2;}
static float completeClear(void*,const vec3&,const vec3&,float){return 2;}
static float badSweep(void*,const vec3&,const vec3&,float){return std::numeric_limits<float>::quiet_NaN();}
static float solidWall(void*,const vec3& a,const vec3& b,float radius){
  const float low=50-radius,high=60+radius,delta=b.x-a.x;
  if(std::abs(delta)<1e-8f)return a.x>=low&&a.x<=high?0.f:2.f;
  float enter=(low-a.x)/delta,leave=(high-a.x)/delta;if(enter>leave)std::swap(enter,leave);
  enter=std::max(0.f,enter);leave=std::min(1.f,leave);return enter<=leave?enter:2.f;
}
static float countedTerrain(void* context,float,float){++*static_cast<int*>(context);return 10000;}
static bool noLOS(void*,const vec3&,const vec3&){return false;}
static void steps(Combat& c,int n,PlayerSnapshot p={},WorldCallbacks w={}){for(int i=0;i<n;++i)c.step(FixedStep,p,w);}
int main(){
  PlayerSnapshot p;p.position={0,1000,0};
  Combat c;c.reset(123);
  check(int(Type::Needle)==0&&int(Type::Bastion)==1&&int(Type::Cantor)==2&&int(Type::Archon)==3,"stable type identities");
  check(roleSpec(Type::Needle).cruise>roleSpec(Type::Cantor).cruise && roleSpec(Type::Cantor).cruise>roleSpec(Type::Bastion).cruise && roleSpec(Type::Bastion).cruise>roleSpec(Type::Archon).cruise,"role speeds ordered");
  check(roleSpec(Type::Archon).hull==2500 && roleSpec(Type::Needle).hull==300,"role durability");
  for(int i=0;i<MaxActors;++i)check(c.spawn(Type(i%4),{float(i*100),1000,1000})!=0,"bounded spawn succeeds");
  check(c.spawn(Type::Needle,{})==0&&c.aliveCount()==8,"actor budget");
  check(c.spawn(Type(255),{})==0,"invalid type rejected");
  c.reset(9); auto near=c.spawn(Type::Needle,{30,1000,0});auto far=c.spawn(Type::Needle,{90,1000,0});
  check(c.playerShot({0,1000,0},{200,1000,0},25)==near,"nearest shot target");
  check(c.find(near)->hull==275&&c.find(far)->hull==300,"baseline damage only nearest");
  WorldCallbacks w;w.sweepFraction=wall;
  c.find(near)->position={100,1000,100};
  check(c.playerShot({0,1000,0},{200,1000,0},100,w)==0&&c.find(far)->hull==300,"wall occludes enemy");
  c.clearEvents(); c.playerShot({0,1000,0},{200,1000,0},10000);c.playerShot({0,1000,0},{200,1000,0},10000);
  check(events(c,EventType::Death)==1,"one death event per actor");
  auto replacement=c.spawn(Type::Bastion,{0,1000,500});check(replacement>far,"stable IDs never reused in epoch");
  c.reset();near=c.spawn(Type::Needle,{90,1000,0}); c.playerBlast({0,1000,0},100,100,w);check(c.find(near)->hull==300,"blast wall occlusion");
  c.playerBlast({0,1000,0},100,100);check(c.find(near)->hull<300&&c.find(near)->hull>200,"blast falloff");
  c.reset();near=c.spawn(Type::Needle,{990,1000,0});c.reset(1,2000);near=c.spawn(Type::Needle,{-990,1000,0});
  check(c.playerShot({970,1000,0},{-960,1000,0},25)==near,"seam nearest-image shot");
  auto before=c.find(near)->position;c.shiftOrigin({-2000,0,0});check(c.find(near)->position.x==before.x-2000,"origin rebase");
  c.reset();near=c.spawn(Type::Needle,{100,1000,0});
  c.projectiles[0].alive=true;c.projectiles[0].position={0,1000,0};c.projectiles[0].velocity={12000,0,0};c.projectiles[0].life=1;c.projectiles[0].damage=25;c.projectiles[0].team=Team::Player;
  c.step(FixedStep,p);check(c.find(near)->hull==275&&!c.projectiles[0].alive,"fast swept player projectile");
  c.projectiles[0]={};c.projectiles[0].alive=true;c.projectiles[0].position={-100,1000,0};c.projectiles[0].velocity={12000,0,0};c.projectiles[0].life=1;c.projectiles[0].damage=6;c.clearEvents();
  c.step(FixedStep,p);check(events(c,EventType::PlayerDamage)==1,"enemy projectile damages player");
  check(c.find(near)->hull==275,"enemy projectiles team filter");
  c.projectiles[0]={};c.projectiles[0].alive=true;c.projectiles[0].weapon=Weapon::Bomb;c.projectiles[0].position={50,1000,0};c.projectiles[0].life=1;
  c.playerShot({0,1000,0},{70,1000,0},25);check(!c.projectiles[0].alive,"bomb interceptable");
  c.reset();auto relay=c.spawn(Type::Cantor,{0,1000,0});near=c.spawn(Type::Needle,{100,1000,0});
  p.position={2000,1000,0};c.find(near)->shield=99.99f;c.step(FixedStep,p);
  check(c.find(near)->shield==100,"relay shield hard cap");
  c.playerEMP({0,1000,0},500,5);check(c.find(near)->shield==0&&c.find(near)->hull==300,"EMP no hull damage");
  steps(c,30,p);check(c.find(near)->shield==0&&c.find(relay)->relayTarget==0,"EMP interrupts incoming and outgoing shields");
  c.reset();relay=c.spawn(Type::Cantor,{0,1000,0});near=c.spawn(Type::Needle,{100,1000,0});w={};w.lineOfSight=noLOS;c.step(FixedStep,p,w);check(c.find(near)->shield==0,"relay requires LOS");
  c.reset();auto boss=c.spawn(Type::Archon,{0,1000,0});c.find(boss)->hull=1700;c.step(FixedStep,p);check(c.aliveCount()==3&&c.find(boss)->escortWaves==1,"boss threshold escorts");
  c.find(boss)->hull=800;c.step(FixedStep,p);check(c.aliveCount()==5&&c.find(boss)->escortWaves==2,"second boss threshold");steps(c,120,p);check(c.aliveCount()==5,"boss finite escorts");
  c.find(boss)->phase=Phase::Recover;c.find(boss)->timer=10;float shield=c.find(boss)->shield;c.step(FixedStep,p);check(c.find(boss)->shield<shield,"boss recovery shield vulnerability");
  Combat a,b;a.reset(123);b.reset(123);for(int i=0;i<4;++i){a.spawn(Type(i),{float(i*200),1000,1000});b.spawn(Type(i),{float(i*200),1000,1000});}
  for(int i=0;i<600;++i){a.step(FixedStep,p);b.step(FixedStep,p);}
  bool same=true;for(int i=0;i<8;++i)same&=a.actors[i].id==b.actors[i].id&&a.actors[i].position.x==b.actors[i].position.x&&a.actors[i].hull==b.actors[i].hull;
  check(same&&a.eventCount==b.eventCount&&a.simulatedSeconds()==b.simulatedSeconds(),"seeded determinism");
  c.reset();near=c.spawn(Type::Needle,{0,1000,0});float time=c.simulatedSeconds();c.step(1,p,{},true);c.step(-1,p);c.step(std::numeric_limits<float>::quiet_NaN(),p);
  check(c.simulatedSeconds()==time,"pause negative and NaN dt inert");c.step(100,p);check(c.simulatedSeconds()>.24f&&c.simulatedSeconds()<.251f,"large dt bounded to 15 ticks");
  vec3 old=c.find(near)->velocity;c.step(FixedStep,p);check(length(c.find(near)->velocity-old)<=220*FixedStep+.001f,"bounded acceleration");
  c.reset();near=c.spawn(Type::Needle,{0,1000,0},{650,0,0});c.find(near)->burstRemaining=.001f;c.find(near)->burstCooldown=10;old=c.find(near)->velocity;c.step(FixedStep,p);check(length(c.find(near)->velocity-old)<=220*FixedStep+.001f,"burst recovery has inertia");
  MissionConfig config;config.kind=MissionKind::Recon;config.objectiveCount=1;config.objectives[0].position={0,1000,-100};config.scanSeconds=.1f;config.extraction={5000,1000,0};
  c.reset();c.startMission(config);p.position={0,1000,0};p.forward={0,0,-1};p.scanning=true;w={};w.lineOfSight=noLOS;steps(c,30,p,w);check(c.mission.config.objectives[0].scan==0,"blocked scan no progress");
  p.scanning=false;steps(c,30,p);check(c.mission.config.objectives[0].scan==0,"scan requires sensor input");p.scanning=true;steps(c,8,p);check(c.mission.status==MissionStatus::Extract,"scan dwell requires extraction");p.position=config.extraction;c.step(FixedStep,p);check(c.mission.status==MissionStatus::Success,"recon extraction success");
  c.reset();config={};config.kind=MissionKind::Defense;config.objectiveCount=1;config.objectives[0].position={0,1000,0};config.waveCount=2;config.waves[0].at=0;config.waves[1].at=1;config.waves[0].count=config.waves[1].count=1;config.waves[0].position=config.waves[1].position={0,1000,0};config.extraction={3000,1000,0};c.startMission(config);p.position={5000,1000,0};c.step(FixedStep,p);check(c.aliveCount()==1,"first defense wave");c.playerBlast({0,1000,0},5000,100000);c.step(FixedStep,p);check(c.mission.status==MissionStatus::Active,"future waves prevent early victory");steps(c,60,p);check(c.aliveCount()==1,"scheduled next wave");c.playerBlast({0,1000,0},5000,100000);c.step(FixedStep,p);check(c.mission.status==MissionStatus::Extract,"defense all waves resolved");
  c.mission.config.objectives[0].health=0;c.step(FixedStep,p);check(c.mission.status==MissionStatus::Failed,"objective loss even during extraction");
  c.reset();near=c.spawn(Type::Needle,{0,1000,0});far=c.spawn(Type::Needle,{500,1000,0});config={};config.kind=MissionKind::Strike;config.targetCount=1;config.targetIds[0]=near;config.extraction={3000,1000,0};c.startMission(config);p.position={5000,1000,0};c.playerShot({450,1000,0},{550,1000,0},1000);c.step(FixedStep,p);check(c.mission.status==MissionStatus::Active,"unassigned kill does not finish strike");c.playerShot({-50,1000,0},{50,1000,0},1000);c.step(FixedStep,p);check(c.mission.status==MissionStatus::Extract,"strike target IDs then extraction");p.position=config.extraction;c.step(FixedStep,p);check(c.mission.status==MissionStatus::Success,"strike extracted");
  c.reset();config={};config.kind=MissionKind::Practice;config.waveCount=1;config.waves[0].count=8;config.waves[0].position={0,1000,0};c.startMission(config);for(int i=0;i<8;++i)c.spawn(Type::Needle,{0,1000,0});c.step(FixedStep,p);check(c.mission.nextWave==0,"full budget defers wave");c.playerBlast({0,1000,0},5000,100000);c.step(FixedStep,p);check(c.mission.nextWave==1&&c.aliveCount()==8,"deferred wave not silently lost");
  c.reset();check(c.aliveCount()==0&&c.eventCount==0&&c.mission.status==MissionStatus::Inactive&&c.simulatedSeconds()==0,"complete reset");
  c.startMission(config);p.alive=false;c.step(FixedStep,p);check(c.mission.status==MissionStatus::Failed,"player loss fails mission");
  c.reset();for(int i=0;i<200;++i){auto id=c.spawn(Type::Needle,{0,1000,0});c.playerShot({-30,1000,0},{30,1000,0},1000);check(id!=0,"ID allocation under slot reuse");}check(c.eventCount==MaxEvents&&c.droppedEvents>0,"bounded event queue");

  // Fixed-step grouping preserves a deterministic trajectory, including capped catch-up.
  a.reset(7);b.reset(7);a.spawn(Type::Bastion,{0,1000,0});b.spawn(Type::Bastion,{0,1000,0});
  p.alive=true;p.position={1500,1000,0};
  for(int i=0;i<120;++i)a.step(FixedStep,p);
  for(int i=0;i<60;++i)b.step(FixedStep*2,p);
  check(a.actors[0].position.x==b.actors[0].position.x&&a.actors[0].velocity.x==b.actors[0].velocity.x,"frame grouping determinism");
  c.reset();near=c.spawn(Type::Needle,{0,1,0});steps(c,60,p);check(c.find(near)->position.y>1,"low spawn climbs without teleport or permanent overlap");
  c.reset();near=c.spawn(Type::Needle,{0,1000,0},{0,0,-520});c.find(near)->timer=0;p.position={0,1000,-1000};c.clearEvents();
  c.step(FixedStep,p);check(c.find(near)->phase==Phase::Telegraph&&events(c,EventType::Telegraph)==1&&events(c,EventType::Fire)==0,"needle announces before travel projectile");
  vec3 lock=c.find(near)->aim;p.position={50,1000,-1000};steps(c,20,p);check(c.find(near)->aim.x==lock.x&&c.find(near)->aim.z==lock.z,"telegraphed aim does not perfectly track");
  steps(c,60,p);check(events(c,EventType::Fire)==3&&c.find(near)->phase==Phase::Recover,"needle finite three-shot burst");
  c.reset();config={};config.kind=MissionKind::Defense;config.objectiveCount=1;config.objectives[0].position={0,0,0};config.waveCount=1;config.waves[0].at=100;c.startMission(config);
  auto bomber=c.spawn(Type::Bastion,{100,700,0});c.find(bomber)->timer=0;p.position={5000,1000,0};c.step(FixedStep,p);
  check(c.find(bomber)->phase==Phase::Telegraph&&c.find(bomber)->aim.y==0,"bastion targets ground site even with distant player");
  c.reset();near=c.spawn(Type::Needle,{990,1000,0});c.reset(1,2000);near=c.spawn(Type::Needle,{990,1000,0},{650,0,0});p.position={-800,1000,0};steps(c,3,p);check(c.find(near)->position.x<0,"actor wraps continuously across world seam");
  c.reset();config={};config.kind=MissionKind::Recon;config.objectiveCount=1;config.objectives[0].position={0,0,0};config.scanSeconds=.1f;c.startMission(config);p.position={0,100,0};p.forward={0,-1,0};p.scanning=true;steps(c,7,p);check(c.mission.config.objectives[0].scanned,"terrain surface endpoint permits reconnaissance");
  c.reset();config={};config.kind=MissionKind::Practice;config.waveCount=1;config.waves[0].count=1;c.startMission(config);for(int i=0;i<8;++i)c.spawn(Type::Needle,{0,1000,0});c.clearEvents();steps(c,10,p);check(events(c,EventType::Wave)==1,"blocked wave announces only once");

  // Authored local hulls retain exterior air gaps instead of filling their bounding spheres.
  Actor shape;shape.alive=true;shape.velocity={0,0,-100};
  for(int role=0;role<4;++role) {
    shape.type=Type(role);
    check(actorHitFraction(shape,{-50,0,0},{50,0,0})<=1,"shaped hull center hit");
    check(actorHitFraction(shape,{-50,9,0},{50,9,0})>1,"space above authored hull is not damageable");
    for(int turn=0;turn<8;++turn) {
      float angle=turn*PI/4;shape.velocity={100*std::sin(angle),20,100*std::cos(angle)};BodyFrame frame=bodyFrame(shape.velocity);
      auto point=[&](vec3 local){return frame.right*local.x+frame.up*local.y+frame.back*local.z;};
      check(actorHitFraction(shape,point({-50,0,0}),point({50,0,0}))<=1,"collision follows pitched/yawed render frame");
      check(actorHitFraction(shape,point({-50,9,0}),point({50,9,0}))>1,"rotated empty envelope remains empty");
    }
    shape.velocity={0,0,-100};
  }
  shape.type=Type::Archon;
  check(actorHitFraction(shape,{0,-4,-15},{0,4,-15})>1,"Archon open bow fork is empty");
  check(actorHitFraction(shape,{5.5f,-4,-15},{5.5f,4,-15})<=1,"Archon solid bow arm is damageable");
  check(actorHitFraction(shape,{-2,3,-8},{2,3,-8})>1,"Archon sloped keep does not fill upper bow air");
  shape.type=Type::Cantor;
  check(actorHitFraction(shape,{0,2,-1},{0,2,1})>1,"Cantor split vane center remains empty");
  check(actorHitFraction(shape,{.9f,2,-1},{.9f,2,1})<=1,"Cantor canted vane is damageable");
  shape.type=Type::Needle;
  check(actorHitFraction(shape,{4,100,1},{4,-100,1})<=1,"thin wing swept without tunnelling");
  check(actorHitFraction(shape,{5.7f,.25f,1},{6,.25f,1})>1&&actorHitFraction(shape,{5.7f,.25f,1},{6,.25f,1},.18f)<=1,"projectile cross-section expands thin wing contact");
  shape.position={100,1000,200};shape.velocity={100,0,0};
  vec3 socket=weaponSocket(shape,0);check(length(socket-vec3(103.5f,999.98f,201.36f))<.001f,"Needle socket uses body-local gun station");
  check(length(weaponSocket(shape,0)-weaponSocket(shape,1))>2.7f,"Needle alternates separated guns");
  shape.type=Type::Bastion;check(length(weaponSocket(shape,0)-weaponSocket(shape,1))==5,"Bastion alternating bays");
  check(length(weaponSocket(shape,0)-weaponSocket(shape,4))>3.3f,"Bastion cycles fore mid aft cassettes");
  shape.type=Type::Archon;check(length(weaponSocket(shape,0)-vec3(108.86f,1000.08f,200))<.001f,"Archon siege aperture socket");
  c.reset();bomber=c.spawn(Type::Bastion,{0,1000,0});c.find(bomber)->timer=0;p={};p.position={500,900,0};p.velocity={10,0,0};c.step(FixedStep,p);
  check(c.find(bomber)->phase==Phase::Telegraph&&c.find(bomber)->aim.x>p.position.x,"practice Bastion attacks predicted player path");
  c.find(bomber)->phase=Phase::Firing;c.find(bomber)->shotsRemaining=3;c.find(bomber)->shotTimer=0;c.clearEvents();c.step(FixedStep,p);
  check(events(c,EventType::Fire)==1&&c.projectiles[0].radius==.45f,"Bastion emits visible-size travelling bomb");
  c.reset();p={};p.position={0,1000,0};p.bodyBoxCount=1;p.bodyBoxes[0]={{0,0,0},{1,.5f,5}};
  auto projectile=[&](float y){auto& q=c.projectiles[0];q={};q.alive=true;q.position={-20,y,0};q.velocity={2400,0,0};q.life=1;q.radius=.18f;q.damage=6;};
  projectile(1003);c.step(FixedStep,p);check(events(c,EventType::PlayerDamage)==0,"player shaped hull rejects old bounding-sphere empty space");
  projectile(1000);c.step(FixedStep,p);check(events(c,EventType::PlayerDamage)==1,"player shaped body hit");
  c.clearEvents();p.up={1,0,0};projectile(1000);c.projectiles[0].position.z=3;c.step(FixedStep,p);check(events(c,EventType::PlayerDamage)==1,"player bank frame remains hittable");
  c.reset();near=c.spawn(Type::Needle,{100,1000,0});int terrainCalls=0;w={};w.context=&terrainCalls;w.sweepFraction=completeClear;w.terrainHeight=countedTerrain;w.sweepIncludesTerrain=true;
  check(c.playerShot({0,1000,0},{200,1000,0},25,w)==near&&terrainCalls==0,"complete production sweep bypasses sampled terrain fallback");
  w.sweepFraction=badSweep;check(c.playerShot({0,1000,0},{200,1000,0},25,w)==0,"malformed production sweep fails closed");

  // A final Game frame snapshot must be sampled through every fixed substep, not
  // reused at the endpoint. The projectile and player cross halfway through it.
  auto crossing=[&](float frame,float carry,float wrap,float shift,bool grouped) {
    Combat simulation;simulation.reset(1,wrap);
    PlayerSnapshot moving;moving.velocity={600,0,0};moving.bodyBoxCount=1;
    moving.bodyBoxes[0]={{0,0,0},{1,.5f,1}};
    const float accepted=std::min(frame,.25f), span=accepted*600;
    auto canonicalX=[&](float x){return wrap>0?std::remainder(x,wrap):x;};
    moving.position={canonicalX(shift-span),1000,0};
    if(carry>0)simulation.step(carry,moving);
    auto& shot=simulation.projectiles[0];shot.alive=true;shot.position={canonicalX(shift-span*.5f),1000,-accepted*50};
    shot.velocity={0,0,100};shot.radius=.18f;shot.damage=6;shot.life=1;
    if(grouped) {moving.position.x=canonicalX(shift);simulation.step(frame,moving);}
    else {
      const int count=int(std::round(accepted/FixedStep));
      for(int i=1;i<=count;++i) {moving.position.x=canonicalX(shift-span+i*600*FixedStep);simulation.step(FixedStep,moving);}
    }
    return std::make_pair(events(simulation,EventType::PlayerDamage),simulation.simulatedSeconds());
  };
  for(float carry:{0.f,.01f}) for(float wrap:{0.f,2000.f}) {
    const float shift=wrap>0?1020.f:0.f;
    const auto grouped=crossing(.1f,carry,wrap,shift,true), individual=crossing(.1f,carry,wrap,shift,false);
    check(grouped.first==1 && grouped.first==individual.first,"moving player crossing matches grouped ticks including carry and wrap seam");
    check(std::abs(grouped.second-individual.second)<1e-6f,"moving player interpolation preserves fixed simulation time");
  }
  const auto capped=crossing(100.f,.01f,2000.f,1020.f,true), accepted=crossing(.25f,.01f,2000.f,1020.f,false);
  check(capped.first==1 && capped.first==accepted.first,"long frame reconstructs only capped moving player window plus retained remainder");
  check(std::abs(capped.second-accepted.second)<1e-6f && capped.second<.251f,"capped interpolation never simulates discarded wall time");

  // Fixed nose weapons must obtain a forward solution, then retain it through
  // charge and burst. Reorientation must not create a rearward muzzle shot.
  for(Type type:{Type::Needle,Type::Archon}) {
    for(vec3 target:{vec3(0,1000,-1000),vec3(1000,1000,0),vec3(0,1000,1000)}) {
      c.reset();const auto id=c.spawn(type,{0,1000,0},{0,0,-roleSpec(type).cruise});c.find(id)->timer=0;
      p={};p.position=target;c.clearEvents();c.step(FixedStep,p);
      check((c.find(id)->phase==Phase::Telegraph)==(target.z<0),"nose weapon telegraphs forward targets but not side/rear targets");
      check(events(c,EventType::Fire)==0,"forward solution still requires a telegraph before firing");
    }
    for(Phase phase:{Phase::Telegraph,Phase::Firing}) {
      c.reset();const auto id=c.spawn(type,{0,1000,0},{0,0,-roleSpec(type).cruise});auto* shooter=c.find(id);
      shooter->phase=phase;shooter->timer=0;shooter->shotTimer=0;shooter->shotsRemaining=3;shooter->aim={0,1000,1000};
      p={};p.position=shooter->aim;c.clearEvents();c.step(FixedStep,p);
      check(shooter->phase==Phase::Approach && shooter->shotsRemaining==0 && events(c,EventType::Fire)==0,"locked target leaving forward cone cancels charge or burst");
    }
    c.reset();const auto id=c.spawn(type,{0,1000,0},{0,0,-roleSpec(type).cruise});c.find(id)->timer=0;
    p={};p.position={0,1000,1500};p.velocity={30,0,0};bool fired=false;
    for(int n=0;n<1800 && !fired;++n) {
      p.position+=p.velocity*FixedStep;c.clearEvents();c.step(FixedStep,p);
      if(events(c,EventType::Fire)) {
        fired=true;
        for(const auto& shot:c.projectiles)if(shot.alive && shot.owner==id)
          check(dot(normalize(c.find(id)->velocity),normalize(shot.velocity))>.93f,"repositioned shot exits along forward weapon cone");
      }
    }
    check(fired,"AI eventually turns onto a moving contact and fires after an initial rear contact");
  }

  // The optional contact result distinguishes an intercepted bomb or world hit
  // from a clear segment without changing legacy actor-ID return semantics.
  ShotContact contact;c.reset();w={};w.sweepFraction=solidWall;w.sweepIncludesTerrain=true;
  near=c.spawn(Type::Needle,{30,1000,0});far=c.spawn(Type::Needle,{80,1000,0});
  const float expected=actorHitFraction(*c.find(near),{0,1000,0},{100,1000,0});
  check(c.playerShot({0,1000,0},{100,1000,0},25,w,&contact)==near && contact.kind==ShotContactKind::Actor && contact.id==near && std::abs(contact.fraction-expected)<1e-6f,"shot contact returns exact nearest hull fraction and legacy actor ID");
  c.find(near)->alive=false;
  check(c.playerShot({0,1000,0},{100,1000,0},25,w,&contact)==0 && contact.kind==ShotContactKind::World && contact.id==0 && contact.fraction==.5f && c.find(far)->hull==300,"shot contact reports intervening world boundary without damaging farther actor");
  auto& intercepted=c.projectiles[0];intercepted={};intercepted.alive=true;intercepted.id=77;intercepted.weapon=Weapon::Bomb;intercepted.radius=.5f;intercepted.position={20,1000,0};
  check(c.playerShot({0,1000,0},{100,1000,0},25,w,&contact)==0 && contact.kind==ShotContactKind::Bomb && contact.id==77 && std::abs(contact.fraction-.195f)<1e-6f && !intercepted.alive,"bomb interception exposes projectile ID and physical fraction while retaining zero actor return");
  check(c.playerShot({0,1100,0},{10,1100,0},25,w,&contact)==0 && contact.kind==ShotContactKind::None && contact.fraction>1 && contact.id==0,"clear shot resets stale contact output");
  contact={ShotContactKind::Actor,.2f,77};c.playerShot({0,1100,0},{10,1100,0},-1,w,&contact);
  check(contact.kind==ShotContactKind::None && contact.fraction>1 && contact.id==0,"invalid shot resets stale contact output");
  c.reset(1,2000);near=c.spawn(Type::Needle,{-990,1000,0});
  check(c.playerShot({970,1000,0},{-960,1000,0},25,{},&contact)==near && contact.kind==ShotContactKind::Actor && contact.fraction>=0 && contact.fraction<=1,"optional shot contact follows nearest toroidal segment");

  // Backing the explosion away from a wall contact leaves the near side exposed
  // while the same blocker continues protecting its far side.
  for(float bombRadius:{0.f,.45f}) for(bool farSide:{false,true}) {
    c.reset();p={};p.position={farSide?65.f:45.f,1000,10};p.bodyBoxCount=1;p.bodyBoxes[0]={{0,0,0},{1,1,1}};
    auto& bomb=c.projectiles[0];bomb.alive=true;bomb.weapon=Weapon::Bomb;bomb.position={40,1000,0};bomb.velocity={1800,0,0};bomb.radius=bombRadius;bomb.blastRadius=40;bomb.damage=10;bomb.life=1;
    w={};w.sweepFraction=solidWall;w.sweepIncludesTerrain=true;c.step(FixedStep,p,w);
    check(!bomb.alive && events(c,EventType::PlayerDamage)==(farSide?0:1),"enemy wall-impact blast damages only unobstructed near side");
  }

  // These guards also run under production fast-math; std::isfinite is not a
  // valid implementation there because the optimizer assumes finite operands.
  for(uint32_t bits:{0u,0x80000000u,1u,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc00000u,0xffc00001u}) {
    float value;std::memcpy(&value,&bits,sizeof value);
    check(floatValidation::finite(value)==((bits&0x7f800000u)!=0x7f800000u),"bit-based finite classification survives fast-math including signed zero/subnormal/max/Inf/NaN");
  }
  for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
    c.reset();p={};p.position={0,1000,0};
    c.step(bad,p);check(c.simulatedSeconds()==0,"all nonfinite frame times leave combat unchanged under fast-math");
    p.position.x=bad;c.step(FixedStep,p);check(c.simulatedSeconds()==0,"nonfinite player snapshot is rejected under fast-math");
    check(c.spawn(Type::Needle,{bad,1000,0})==0,"nonfinite spawn position is rejected under fast-math");
    near=c.spawn(Type::Needle,{0,1000,0});c.find(near)->shield=100;
    c.playerShot({-20,1000,0},{20,1000,0},bad);c.playerBlast({0,1000,0},bad,100);c.playerEMP({0,1000,0},100,bad);
    check(c.find(near)->hull==300 && c.find(near)->shield==100,"nonfinite player damage/radius/disruption cannot mutate an actor");
    check(actorHitFraction(*c.find(near),{-20,1000,0},{20,1000,0},bad)>1,"nonfinite projectile radius is rejected under fast-math");
    auto& badProjectile=c.projectiles[0];badProjectile.alive=true;badProjectile.position={50,1000,0};badProjectile.life=bad;
    p.position={0,1000,0};c.step(FixedStep,p);check(!badProjectile.alive,"nonfinite projectile life is rejected under fast-math");
  }
  std::printf("Hive combat: %d checks, %d failures\n",checks,fails);return fails?1:0;
}
