#include "game.h"
#include "candidates.h"
#include "ground_vehicle.h"
#include <cstdio>
#include <limits>
#include <set>
static int checks=0,failures=0;
static void check(bool b,const char* name){++checks;if(!b){++failures;printf("FAIL %s\n",name);}}
static bool near(double a,double b,double eps=1e-4){return std::fabs(a-b)<eps;}
struct GameTest {
  static void packing(int idx) {
    Game g;g.initHeadless();g.plane.reset(&kAircraft[idx],vec3(0,1000,0),0,0,0,true,0);
    g.screen=SCR_FLIGHT;g.plane.wheelMotion[0].travel=.17;g.plane.wheelMotion[1].travel=-.11;g.plane.wheelMotion[2].travel=.13;
    FrameParams f=g.buildFrame();const auto& s=kAircraft[idx];const auto& m=kModels[idx];
    float r=s.special?.38f:m.wheelR,nr=s.special?.33f:s.taildragger?.10f:m.gear==3?r*.75f:r*.85f;
    check(near(f.plane.wheel[0],-.17/r),"player main-left radius/direction packing");
    check(near(f.plane.wheel[1],.11/r),"player main-right independent phase packing");
    check(near(f.plane.wheel[2],-.13/nr),"player nose/tail actual visual radius packing");
  }
};
int main(){
  WheelMotion w;w.step(2,3,true);check(near(w.travel,6),"signed rolling distance");check(near(w.angle(.5f),std::remainder(-12.0,2.0*PI)),"travel/radius and aircraft direction");
  check(near(w.angle(.25f),std::remainder(-24.0,2.0*PI)),"small tyre rotates faster");
  w.step(2,-3,true);check(near(w.travel,0),"reverse cancels forward travel");
  w.step(1,20,true,1);check(near(w.travel,20),"rolling braked tyre follows actual road speed");
  double stop=w.travel;w.step(4,0,true);check(near(w.travel,stop)&&w.speed==0,"parked tyre stops");
  w.step(0,50,true);check(near(w.travel,stop),"pause does not advance");w.step(-1,50,true);check(near(w.travel,stop),"negative timestep rejected");
  w.step(1,std::numeric_limits<float>::quiet_NaN(),true);check(near(w.travel,stop),"invalid velocity rejected");check(w.angle(0)==0,"invalid radius safe");
  WheelMotion a,b;a.step(1,8,true);b=a;a.step(4,999,false);for(int i=0;i<400;++i)b.step(.01f,-999,false);
  check(near(a.travel,b.travel,.001)&&near(a.speed,b.speed,.001),"airborne spin-down frame-rate independent and ignores airspeed");
  WheelMotion brake=b;brake.step(1,0,false,1);check(brake.speed<b.speed*.001f,"airborne main-wheel braking");
  w.reset();check(w.travel==0&&w.speed==0,"respawn resets motion");w.travel=1.e9;check(std::isfinite(w.angle(.1f)),"long-running phase remains finite and wrapped");
  g_world.build();buildStory();g_audio.init(48000);
  Weather wx;wx.windSpeed=wx.gust=wx.turbulence=0;
  int ai=-1;for(int i=0;i<(int)g_world.airports.size();++i)if(std::string(g_world.airports[i].code)=="NPT")ai=i;
  const Airport& apt=g_world.airports[ai];
  const auto& roster=candidate::roster;
  for(const auto& e:roster){
    const auto& s=kAircraft[e.index];Plane p;p.reset(&s,vec3(apt.x,apt.elev,apt.z),apt.heading,s.maxFuel*.5f,0,false);p.sceneryHits=false;
    for(int i=0;i<240;++i)p.step(1.f/240,wx,float(i)/240);
    p.ctl.brake=0;p.vel=p.forward()*8.f;
    for(int i=0;i<120;++i)p.step(1.f/240,wx,1.f+float(i)/240);
    check(!p.ev.crashed,"each aircraft native ground rollout remains valid");
    check(p.wheelMotion[0].travel>1 &&p.wheelMotion[1].travel>1,"each aircraft real main contacts advance wheel motion");
    p.reset(&s,vec3(apt.x,1000,apt.z),0,0,0,true,50);p.apEngage(Plane::AP_HOLD,-1,wx);
    p.step(1.f/120,wx,0);check(p.wheelMotion[0].travel==0,"air-start does not invent wheel spin");
    GameTest::packing(e.index);
    printf("aircraft wheel coverage: %s\n",s.id);
  }
  Traffic tr;tr.enabled=true;
  TrafficCraft c;c.id=100;c.spec=0;c.role=TrafficCraft::AIRPORT;c.state=TrafficCraft::TAKEOFF;c.airport=ai;c.hdg=apt.heading*DEG;
  c.q=quat::axisAngle(vec3(0,1,0),-c.hdg);c.pos=vec3(apt.x,apt.elev+kAircraft[0].fusRad*1.3f+.55f,apt.z);c.speed=8;tr.craft.push_back(c);
  tr.update(.1f,vec3(apt.x+10000,apt.elev,apt.z),vec3(),false,10);
  check(tr.craft[0].wheelMotion[0].travel>.5,"native AI takeoff drives wheel state");
  TrafficVisual tv{};check(tr.fillVisuals(tr.craft[0].pos,&tv,1,nullptr)==1,"AI visual created");
  check(near(tv.t[25*4+3],tr.craft[0].wheelMotion[0].angle(kModels[0].wheelR)),"AI phase stored in unused rotation w");check(sizeof(tv)==32*4*sizeof(float),"AI GPU texture stride preserved");
  std::vector<EVert> verts;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(verts,ranges);
  for(int kind:{EK_GA_PLANE,EK_AIRLINER,EK_CAR,EK_TRUCK}){
    auto layout=groundWheelLayout(kind);check(layout.count>0,"all wheeled scenery kinds registered");
    for(int lod=0;lod<2;++lod){std::set<int> ids;bool matching=true;
      for(int i=ranges[kind].first[lod];i<ranges[kind].first[lod]+ranges[kind].count[lod];++i){auto& v=verts[i];int id=int(v.part+.5f)-P_WHEEL0;if(id<0||id>=6)continue;ids.insert(id);auto& wh=layout.wheel[id];matching &=near(v.ao,wh.radius)&&near(v.u,wh.center.y)&&near(v.v,wh.center.z);}
      check(int(ids.size())==layout.count&&*ids.begin()==0&&*ids.rbegin()==layout.count-1,"every mesh wheel has a stable slot in both visible LODs");check(matching,"mesh rest pivot/radius agrees with native driver");
    }
    Ent ent{0,0,0,0,1,1,1,.5f};GroundVehicleMotion motion;motion.step(kind,ent,vec3(0,0,3),0,1);
    auto visual=motion.visual(kind,ent);for(int i=0;i<layout.count;++i)check(near(visual.angle[i],std::remainder(3.0/layout.wheel[i].radius,2.0*PI)),"each ground wheel distance/radius phase");
    motion.reset();motion.step(kind,ent,vec3(0,0,0),.5f,1);check(motion.wheels[0].travel*motion.wheels[1].travel<0 ||kind==EK_AIRLINER,"turning gives opposite signed left/right travel");
    motion.reset();motion.step(kind,ent,vec3(0,0,0),0,1);check(motion.wheels[0].travel==0,"stationary ground vehicle remains parked");
    ent.sx=ent.sy=ent.sz=2;motion.step(kind,ent,vec3(0,0,3),0,1);check(near(motion.visual(kind,ent).angle[0],std::remainder(3.0/(layout.wheel[0].radius*2),2.0*PI)),"scaled ground tyre uses world radius");
    printf("ground wheel coverage: %s (%d wheels, LOD0/1)\n",kEntInfo[kind].name,layout.count);
  }
  Ent ent{0,0,0,0,1,1,1,.5f};GroundVehicleMotion gm;std::vector<GroundVehicleVisual> vs{gm.visual(EK_CAR,ent)};
  uint64_t key=groundVehicleShadowKey(vs,vec3(),300,1);gm.step(EK_CAR,ent,vec3(0,0,1),0,.1f);vs[0]=gm.visual(EK_CAR,ent);
  check(key!=groundVehicleShadowKey(vs,vec3(),300,1),"wheel pose invalidates affected cached shadow");vs[0].entity.x=100000;
  check(groundVehicleShadowKey(vs,vec3(),300,1)==groundVehicleShadowKey({},vec3(),300,1),"distant vehicles do not invalidate unrelated cascade");
  check(sizeof(Ent)==8*sizeof(float)&&sizeof(EVert)==10*sizeof(float),"scenery instance and vertex strides preserved");
  printf("wheel animation: %d/%d checks passed\n",checks-failures,checks);return failures?1:0;
}
