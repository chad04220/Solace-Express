// Native gameplay regression: real AP_NAV -> approach -> flare -> rollout, no test-pilot controls.
// Both new aircraft, both Atlas-compatible fields, legal mid/max loads, calm/windy, standard/comfort.
#include "aircraft.h"
#include <cstdio>
#include <cstring>

static int failures=0;
static void run(int craft,const char* code,int load,float wind,bool comfort,int failedEngines=0,bool forceGoAround=false) {
  const AircraftSpec& s=kAircraft[craft];const int ai=g_world.findAirport(code);const Airport& a=g_world.airports[ai];
  Weather wx;wx.windSpeed=wind;wx.windFrom=wrapDeg360(a.heading+25.f);wx.turbulence=wind>0?.15f:0.f;wx.gust=0;
  const float maxPayload=s.cargoKg+s.pax*85.f+85.f;
  const float fuel=s.maxFuel*(load==1?.75f:load==2?1.f:.5f);
  const float payload=load==1?maxPayload:load==2?100.f:maxPayload*.5f;
  const vec3 side(-a.dir().z,0,a.dir().x);
  vec3 start=a.pos()+side*14000.f+a.dir()*3000.f;
  start.y=std::max(a.elev+1200.f,g_world.height(start.x,start.z)+500.f);
  Plane p;p.reset(&s,start,wrapDeg360(a.heading+120.f),fuel,payload,true,s.cruise*.85f);
  p.ctl.gearDown=!s.retract;p.gear=p.ctl.gearDown?1.f:0.f;p.ctl.throttle=.7f;
  for(int i=0;i<failedEngines;++i)p.failNow(FAIL_ENGINE_TOTAL,i);
  p.apComfort=comfort;p.apEngage(Plane::AP_NAV,ai,wx);
  const float initialMass=p.mass();bool touchdown=false;float sink=0;int k=0,goArounds=0,lastStage=p.apStage;
  float navBank=0.f,navMinG=1.f,navMaxG=1.f,maxRolloutCross=0.f,maxContactCross=0.f;bool disturbed=false,forcedStage=false;
  // Ordinary cases terminate at 1,500 s. The explicitly induced missed-approach
  // diagnostic allows 2,400 s for a second complete pattern, with the same landing bounds.
  const int limit=forceGoAround?2400:1500;
  for(;k<limit*60&&!p.ev.crashed&&!p.apDone&&p.apDecline.empty();++k){
    if(forceGoAround&&!disturbed&&p.apStage==Plane::APS_FINAL){
      const vec3 rr(-p.apLd.z,0,p.apLd.x),r=p.pos-p.apTd;const float dist=-dot(r,p.apLd);
      if(p.pos.y-p.gearHeight()-a.elev<110.f&&dist<2100.f&&dist>500.f){
        // An explicit initial-state disturbance tests recovery, separate from the
        // untouched native 34-case matrix. No controls are overridden afterward.
        p.pos=p.pos+rr*(a.width*.6f-dot(r,rr));p.vel=p.vel+rr*(3.f-dot(p.vel,rr));disturbed=true;
      }
    }
    if(forceGoAround&&disturbed&&!forcedStage&&p.apStage==Plane::APS_FINAL){
      const float dist=-dot(p.pos-p.apTd,p.apLd);
      if(p.pos.y-p.gearHeight()-a.elev<100.f&&dist<2000.f&&dist>250.f){
        p.apStage=Plane::APS_GOAROUND;p.apStageT=0;forcedStage=true;
        printf("FORCED_GOAROUND %.3f distance %.3f height %.3f\n",k/60.f,dist,p.pos.y-p.gearHeight()-a.elev);
      }
    }
    p.step(1.f/60.f,wx,k/60.f);
    if(k>5*60&&p.apStage==Plane::APS_NAV){navBank=std::max(navBank,fabsf(p.bankDeg()));navMinG=std::min(navMinG,p.gLoad);navMaxG=std::max(navMaxG,p.gLoad);}
    if(forceGoAround&&goArounds>0&&((p.apStage==Plane::APS_FINAL||p.apStage==Plane::APS_FLARE)&&k%60==0||p.apStage==Plane::APS_ROLLOUT&&k%6==0||p.ev.touchdown)){
      const vec3 rr(-p.apLd.z,0,p.apLd.x),r=p.pos-p.apTd;
      printf("ROLLTRACE %.3f stage%d dist%.2f cross%.2f vside%.2f speed%.2f height%.2f bank%.2f heading%.2f yawrate%.2f yaw%.3f integral%.3f brake%.2f\n",k/60.f,p.apStage,-dot(r,p.apLd),dot(r,rr),dot(p.vel,rr),length(p.vel),p.pos.y-p.gearHeight()-a.elev,p.bankDeg(),p.heading(),p.w.y/DEG,p.ctl.yaw,p.apYawI,p.ctl.brake);
    }
    if(p.ev.touchdown&&!touchdown){touchdown=true;sink=-p.ev.touchdownVs;}
    // Keep checking through any rollout-to-flare bounce after first contact.
    // These are the production main/nose ground-contact stations, transformed by
    // the actual attitude, rather than an origin-only runway-width check.
    if(touchdown||p.apStage==Plane::APS_ROLLOUT){
      maxRolloutCross=std::max(maxRolloutCross,fabsf(dot(p.pos-a.pos(),side)));
      const GearStations g=gearStations(s);const float gh=p.gearHeight();
      for(const vec3 c:{vec3(-g.track,-gh,g.mainZ),vec3(g.track,-gh,g.mainZ),vec3(0,-gh,g.noseZ)})
        maxContactCross=std::max(maxContactCross,fabsf(dot(p.pos+p.q.rotate(c)-a.pos(),side)));
    }
    if(p.apStage==Plane::APS_GOAROUND&&lastStage!=p.apStage)++goArounds;
    lastStage=p.apStage;
  }
  const vec3 rel=p.pos-a.pos();const float along=fabsf(dot(rel,a.dir())),cross=fabsf(dot(rel,side));
  const bool comfortOk=!(load==2&&comfort)||(navBank<=26.f&&navMinG>=.8f&&navMaxG<=1.3f);
  const bool recoveryOk=!forceGoAround||(disturbed&&goArounds>0);
  const bool ok=maxContactCross<a.width*.5f&&recoveryOk&&maxRolloutCross<a.width*.5f&&comfortOk&&!p.ev.crashed&&p.apDone&&!p.apOverrun&&touchdown&&sink<3.f&&along<a.length*.5f&&cross<a.width*.5f;
  const char* reason = p.ev.crashed ? p.ev.crashReason.c_str() : !p.apDecline.empty() ? p.apDecline.c_str() :
    !recoveryOk ? "missed-approach fixture not exercised" : !p.apDone ? "timeout" : p.apOverrun ? "runway overrun" : !touchdown ? "no touchdown" :
    sink>=3.f ? "hard touchdown" : maxContactCross>=a.width*.5f ? "gear contact leaves runway width" : maxRolloutCross>=a.width*.5f ? "rollout leaves runway width" : cross>=a.width*.5f ? "outside runway width" : along>=a.length*.5f ? "outside runway length" : !comfortOk ? "en-route comfort envelope" : "";
  printf("%s,%s,%s,%d,%d,%.0f,%.1f,%s,%.1f,%.2f,%.1f,%.2f,%d,%.2f,%.3f,%.3f,%.2f,%.2f,%s,%s\n",s.id,code,load==1?"maximum":load==2?"legacy-fixture":"mid",failedEngines,(int)forceGoAround,initialMass,wind,comfort?"comfort":"standard",k/60.f,sink,along,cross,goArounds,navBank,navMinG,navMaxG,maxRolloutCross,maxContactCross,ok?"PASS":"FAIL",reason);
  fflush(stdout);failures+=!ok;
}
// The gate uses pavement width and the live climb capability, not a hardcoded
// aircraft/runway identity. The same predicted 21 m origin offset plus its 3.45 m main track fits CAP but not PVI;
// closing drift is accepted. The new rejection policy requires full engine power;
// degraded engines retain the existing forced-approach behavior, and loss of both
// engines must never ask for a climb.
static void lateralGateCases(){
  for(const char* code:{"CAP","PVI"})for(int failed:{0,1,2})for(float drift:{-3.f,3.f}){
    const int ai=g_world.findAirport(code);const Airport& a=g_world.airports[ai];const AircraftSpec& s=kAircraft[kAtlas];
    Weather wx;wx.windSpeed=wx.turbulence=wx.gust=0;
    Plane p;p.reset(&s,a.pos()+vec3(0,1200,0),a.heading,s.maxFuel*.75f,s.cargoKg+s.pax*85.f+85.f,true,90);
    p.apComfort=true;p.apEngage(Plane::AP_APPR,ai,wx);
    const vec3 side(-p.apLd.z,0,p.apLd.x);
    p.pos=p.apTd-p.apLd*1500.f+side*12.f;p.pos.y=a.elev+p.gearHeight()+80.f;
    p.vel=p.apLd*90.f+side*drift;p.apStage=Plane::APS_FINAL;p.apStageT=20.f;
    for(int i=0;i<failed;++i)p.failNow(FAIL_ENGINE_TOTAL,i);
    p.apSense();const bool capable=p.apEnv.canGoAround;
    const bool expected=failed==0&&capable&&fabsf(12.f+3.f*drift)+gearStations(s).track>a.width*.5f;
    p.step(1.f/60.f,wx,0);
    const bool ok=(p.apStage==Plane::APS_GOAROUND)==expected&&(failed==2?!capable:capable);
    printf("LATERAL_GATE %s engines-out %d drift %+.0f predicted %.0f halfwidth %.1f climb-plan %.1f go-around %d %s\n",
      code,failed,drift,12.f+3.f*drift,a.width*.5f,p.apEnv.climbPlan,p.apStage==Plane::APS_GOAROUND,ok?"PASS":"FAIL");
    failures+=!ok;
  }
  // A newly acquired ground contact must enter rollout, never take the new
  // early-break go-around path before FINAL's ordinary on-ground transition.
  const int ai=g_world.findAirport("PVI");const Airport& a=g_world.airports[ai];const AircraftSpec& s=kAircraft[kAtlas];
  Weather wx;wx.windSpeed=wx.turbulence=wx.gust=0;
  Plane p;p.reset(&s,a.pos()+vec3(0,1200,0),a.heading,s.maxFuel*.75f,s.cargoKg+s.pax*85.f+85.f,true,75);
  p.apComfort=true;p.apEngage(Plane::AP_APPR,ai,wx);const vec3 rr(-p.apLd.z,0,p.apLd.x);
  p.pos=p.apTd-p.apLd*275.f+rr*20.f;p.pos.y=a.elev+p.gearHeight();p.vel=p.apLd*75.f+rr*3.f;
  p.q=quat::axisAngle(vec3(0,1,0),-atan2f(p.apLd.x,-p.apLd.z));p.onGround=true;p.apStage=Plane::APS_FINAL;p.apStageT=20.f;
  p.step(1.f/60.f,wx,0);const bool ok=p.apStage==Plane::APS_ROLLOUT;
  printf("LATERAL_GATE ground-contact enters rollout %s\n",ok?"PASS":"FAIL");failures+=!ok;
  // A matched jam is an available landing configuration, not a reason to wait
  // forever for a flap position the actuator cannot reach before returning to NAV.
  Plane jam;jam.reset(&s,a.pos()+vec3(0,3000,0),a.heading,s.maxFuel*.75f,s.cargoKg+s.pax*85.f+85.f,true,95);
  jam.apEngage(Plane::AP_APPR,ai,wx);jam.fail.flapAsym=true;jam.fail.flapAt=.25f;jam.flaps=.25f;
  jam.apStage=Plane::APS_GOAROUND;jam.apStageT=20.f;jam.step(1.f/60.f,wx,0);
  const bool jamOk=jam.apStage==Plane::APS_NAV&&jam.flaps>.01f;
  printf("LATERAL_GATE jammed-flap safe-height returns to NAV %s\n",jamOk?"PASS":"FAIL");failures+=!jamOk;
}
int main(int argc,char**argv){
  g_world.build();const int only=argc>1?atoi(argv[1]):-1;
  printf("aircraft,airport,load,engines_out,induced_go_around,initial_mass_kg,wind_mps,mode,seconds,touchdown_sink_mps,stop_from_centre_m,cross_track_m,go_arounds,nav_max_bank_deg,nav_min_g,nav_max_g,max_rollout_cross_m,max_contact_cross_m,result,reason\n");
  if(argc>1&&!strcmp(argv[1],"--comfort-fixture")){run(kAtlas,"CAP",2,0.f,true);return failures?1:0;}
  lateralGateCases();
  if(argc>1&&!strcmp(argv[1],"--gate-cases"))return failures?1:0;
  if(argc>1&&!strcmp(argv[1],"--engine-out")){
    for(const char* airport:{"CAP","PVI"})for(int load:{0,1})for(float wind:{0.f,6.f})run(kAtlas,airport,load,wind,true,1);
    printf("%s: %d engine-out failures\n",failures?"FAILED":"PASS",failures);return failures?1:0;
  }
  if(argc>1&&!strcmp(argv[1],"--go-around")){
    run(kAtlas,"CAP",1,6.f,true,0,true);
    printf("%s: %d induced-go-around failures\n",failures?"FAILED":"PASS",failures);return failures?1:0;
  }
  for(int craft:{kLarkspur,kAtlas})if(only<0||only==craft)
    for(const char* airport:{"CAP","PVI"})for(int load:{0,1})for(float wind:{0.f,6.f})for(bool comfort:{false,true})run(craft,airport,load,wind,comfort);
  if(only<0||only==kAtlas)run(kAtlas,"CAP",2,6.f,false); // exact aggregate regression fixture
  if(only<0||only==kAtlas)run(kAtlas,"CAP",2,0.f,true); // exact full-fuel comfort NAV route and envelope
  printf("%s: %d native-autoland failures\n",failures?"FAILED":"PASS",failures);
  return failures?1:0;
}
