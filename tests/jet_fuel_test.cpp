// Jet fuel is measured in kg, never a map-size fraction. The same engine curve is
// exercised here as in Plane::step, the HUD, and the career quote.
#include "../src/aircraft.h"
#include "../src/career.h"
#include "../src/hive_loadout.h"
#include "test_world.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <filesystem>

namespace {
int failures = 0;
void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } }
bool near(float a, float b, float absolute=.001f) { return std::fabs(a-b) <= absolute; }
Weather calm() { Weather w; w.windSpeed=w.gust=w.turbulence=0; return w; }
void curves() {
  for (int i=0;i<kAircraftCount;++i) {
    const auto& s=kAircraft[i]; Plane p; p.spec=&s;
    if(s.engineType!=ENG_JET) {
      const float old=s.maxFuel/(s.rangeKm*1000.f/s.cruise*.8f);
      check(near(p.fuelFlowMax(),old),"non-jet full-power calibration preserved");
      check(near(p.fuelFlowAt(.37f,s.cruise,1.225f),old*(.2f+.8f*.37f)),"non-jet throttle curve preserved");
      continue;
    }
    AircraftSpec resized=s;resized.maxFuel*=2;resized.rangeKm*=.1f;Plane sameEngine;sameEngine.spec=&resized;
    check(near(p.fuelFlowAt(.7f,200,1.225f),sameEngine.fuelFlowAt(.7f,200,1.225f)),"tank size and range metadata do not create engine efficiency");
    if(s.jetWetTsfc==0)check(p.fuelFlowAt(.35f,200,1.225f,.5f)<p.fuelFlowAt(.7f,200,1.225f)*.6f,"half of installed jet thrust pays approximately half the fuel");
    check(s.jetDryTsfc>0 && s.jetDryTsfc<.0001f,"jet TSFC has physical SI units");
    check(s.maxTakeoffMass>s.emptyMass+s.maxFuel+85,"full fuel plus pilot below MTOW");
    const float payload=s.cargoKg+s.pax*85.f+85;
    check(s.emptyMass+s.maxFuelForPayload(payload)+payload<=s.maxMass()+.01f,"payload-limited fuel respects MTOW");
    float prior=0;
    for(int k=0;k<=100;++k) { const float flow=p.fuelFlowAt(k/100.f,200,1.225f);check(flow>=prior,"throttle never makes fuel burn smaller");prior=flow; }
    const float idle=p.fuelFlowAt(0,0,1.225f), dry=p.fuelFlowAt(.85f,200,1.225f), max=p.fuelFlowAt(1,200,1.225f);
    check(idle>0 && dry>idle,"running jet has finite idle and greater powered burn");
    if(s.jetWetTsfc>0) check(max>dry*3,"reheat costs materially more than dry thrust");
    check(p.fuelFlowAt(.7f,200,isaDensity(10000))<p.fuelFlowAt(.7f,200,1.225f),"same throttle burns less in thin air");
    check(p.fuelFlowAt(.7f,200,1.225f,0)==0,"no live engines consume no fuel");
    p.fuel=s.maxFuel; p.engineRunning=false; check(p.fuelFlowKgS()==0,"stopped engines consume no fuel");
    p.engineRunning=true;p.fuel=0;check(p.fuelFlowKgS()==0,"empty tanks consume no fuel");
    p.fuel=s.maxFuel;
    printf("MODEL %s tank %.0f mtow %.0f full_payload %.0f full_payload_fuel %.0f idle %.1f max_static %.1f nominal %.1f kg/h range %.0f km reserve30 %.0f kg\n",s.id,s.maxFuel,s.maxMass(),payload,s.maxFuelForPayload(payload),idle*3600,p.fuelFlowMax()*3600,p.nominalFuelFlowKgS()*3600,p.nominalRangeKm(),p.nominalFuelFlowKgS()*1800);
  }
}
// Hold the flight condition only in this metering test. This isolates substep
// rounding and engine state from the separate unrestrained flight test below.
void metering() {
  const Weather wx=calm();
  for(int i:{6,kNightjar,kResearchJet,kMantis,kWraith,kAtlas}) {
    const auto&s=kAircraft[i];
    for(float spool:{0.f,.65f,1.f}) {
      Plane p;p.reset(&s,vec3(-39000,2000,35000),0,s.maxFuel,85,true,180);p.sceneryHits=false;
      p.soundSpeed=isa(2000).a;p.density=isaDensity(2000);p.airspeed=180;
      p.engineSpool=spool;p.ctl.throttle=spool;
      const float rate=p.fuelFlowAt(spool,180,p.density),initial=p.fuel;
      for(int k=0;k<60*60;++k) {
        p.pos=vec3(-39000,2000,35000);p.vel=vec3(0,0,-180);p.q=quat();p.w=vec3();
        p.step(1.f/60,wx,k/60.f);
      }
      const float used=initial-p.fuel;
      printf("METER %s throttle %.2f predicted %.5f actual %.5f kg\n",s.id,spool,rate*60,used);
      check(!p.ev.crashed&&std::fabs(used-rate*60)<std::max(.015f,rate*60*.002f),"240 Hz integrated burn agrees with shared kg/s model");
    }
    Plane p;p.reset(&s,vec3(-39000,2000,35000),0,.001f,85,true,180);p.sceneryHits=false;
    p.engineSpool=p.ctl.throttle=1;
    for(int k=0;k<60;++k)p.step(1.f/60,wx,k/60.f);
    check(p.fuel==0&&!p.engineRunning&&p.fuelFlowKgS()==0,"fuel starvation stops every jet including research");
  }
}
void cruiseFlights(int seconds, int only=-1, bool rangeRun=false) {
  const Weather wx=calm();
  for(int i:{6,kNightjar,kMantis,kResearchJet,kWraith,kAtlas}) {
    if(only>=0 && i!=only)continue;
    const auto&s=kAircraft[i];const float payload=i==kAtlas?15385.f:i==6?500.f:85.f;
    Plane p;p.reset(&s,vec3(-39000,s.fuelCruiseAlt,35000),0,s.maxFuel,payload,true,s.fuelCruiseTas);p.sceneryHits=false;
    p.engineSpool=s.fuelCruiseSpool;p.ctl.throttle=p.engineSpool;
    p.apEngage(Plane::AP_HOLD,-1,wx);p.apAlt=s.fuelCruiseAlt;
    p.apSpeed=calibratedAirspeed(s.fuelCruiseTas,isa(s.fuelCruiseAlt));p.apHeading=0;
    double modelKg=0,totalDistance=0,speed=0,spool=0;int ticks=0;float maxSlip=0,maxBank=0;int n=0;const float initial=p.fuel;
    float begin=p.fuel;
    for(int k=0;k<seconds*30&&!p.ev.crashed&&p.engineRunning && (!rangeRun || p.fuel>p.nominalFuelFlowKgS()*1800);++k) {
      const float flow=p.fuelFlowKgS();p.step(1.f/30,wx,k/30.f);
      modelKg+=(flow+p.fuelFlowKgS())*.5/30;totalDistance+=p.airspeed/30;++ticks;
      if(only>=0 && k%(300*30)==0)printf("TRACE %s t %.0f fuel %.0f altitude %.0f V %.2f spool %.3f pitch %.2f bank %.2f alpha %.2f beta %.2f elevator %.3f gear %.3f flap %.3f ice %.2f g %.2f\n",s.id,k/30.f,p.fuel,p.pos.y,p.airspeed,p.engineSpool,p.pitchDeg(),p.bankDeg(),p.alpha/DEG,p.beta/DEG,p.ctl.pitch,p.gear,p.flaps,p.fail.ice,p.gLoad);
      if(k==60*30) begin=p.fuel;
      if(k>60*30){speed+=p.airspeed;spool+=p.engineSpool;maxSlip=std::max(maxSlip,std::fabs(p.beta/DEG));maxBank=std::max(maxBank,std::fabs(p.bankDeg()));++n;}
    }
    if(rangeRun)printf("RANGE_FLOWN %s duration %.2f h distance %.0f km remaining %.0f kg (30-minute reference reserve)\n",s.id,ticks/30.f/3600,totalDistance/1000,p.fuel);
    const float actual=initial-p.fuel,rate=n?(begin-p.fuel)/(n/30.f):0;
    const float v=n?speed/n:0,u=n?spool/n:0;
    const float range=rate>0?s.maxFuel/rate*v/1000:0;
    printf("CRUISE %s seconds %.1f crash %d alt %.0f target %.0f speed %.2f target %.2f spool %.4f burn %.2f model %.2f kg steady %.1f kg/h mean_burn_equivalent_range %.0f km nominal %.0f km\n",s.id,ticks/30.f,p.ev.crashed,p.pos.y,s.fuelCruiseAlt,v,s.fuelCruiseTas,u,actual,modelKg,rate*3600,range,s.maxFuel/p.nominalFuelFlowKgS()*s.fuelCruiseTas/1000);
    check(!p.ev.crashed&&n>0&&p.engineRunning,"cruise stays airborne with finite fuel");
    check(std::fabs(actual-modelKg)<std::max(.05,modelKg*.004),"free-running simulation burn agrees with estimator");
    check(std::fabs(p.pos.y-s.fuelCruiseAlt)<150 && std::fabs(v-s.fuelCruiseTas)<s.fuelCruiseTas*.12f,"jet holds efficient cruise altitude and TAS");
    if(i==kAtlas)check(maxSlip<3.f&&maxBank<3.f,"long transport cruise does not develop drag-inducing Dutch roll");
    check(range>1000 && range<11000,"measured jet range is plausible and finite");
    check(std::fabs(p.rangeLeftKm()-p.fuel/p.fuelFlowKgS()*p.airspeed/1000)<.01f,"HUD actual range uses current fuel flow and TAS");
  }
}
void hoverAndLoadout() {
  const auto&s=kAircraft[kWraith];
  for(int gun=0;gun<3;++gun)for(int bomb=0;bomb<3;++bomb){hive::Loadout l;l.reset((hive::ForwardSet)gun,(hive::BombSet)bomb);check(s.emptyMass+s.maxFuel+85+l.massKg()<=s.maxMass(),"every loaded Wraith equipment choice fits MTOW with full fuel");}
  Plane p;p.reset(&s,vec3(-39000,800,35000),0,s.maxFuel,85,true,.1f);p.vel=vec3();p.ctl.flaps=p.flaps=p.nozzle=1;p.sceneryHits=false;
  float integral=.68f;double used=0;const Weather wx=calm();
  for(int k=0;k<120*60&&!p.ev.crashed;++k) {
    integral=clampf(integral-p.vel.y*.0002f,0.f,1.f);p.ctl.throttle=clampf(integral-p.vel.y*.04f,0.f,1.f);
    const float before=p.fuelFlowKgS();p.step(1.f/60,wx,k/60.f);used+=(before+p.fuelFlowKgS())*.5/60;
  }
  printf("HOVER %s used %.1f kg in 120 s model %.1f flow %.0f kg/h vs %.3f altitude %.1f\n",s.id,s.maxFuel-p.fuel,used,p.fuelFlowKgS()*3600,p.vel.y,p.pos.y);
  check(!p.ev.crashed&&std::fabs(p.vel.y)<1.f&&s.maxFuel-p.fuel>300,"VTOL hover remains stable and consumes substantial fuel");
  check(std::fabs(s.maxFuel-p.fuel-used)<std::max(1.0,used*.005),"vectored-pod hover bills the same actual thrust flow");
  check(p.rangeLeftKm()<30,"hover does not claim cruise distance while burning fuel");
}
void missionQuotes() {
  for(int si:{6,kAtlas}) {
    Career c;c.newGame();Contract q;q.from=g_world.findAirport("CAP");q.to=g_world.findAirport("PVI");
    q.type=CT_PAX;q.pax=si==6?4:120;q.cargoKg=si==6?100:1200;q.wx=calm();
    auto plan=c.plan(q,si,Career::SRC_OWNED);const float quick=plan.fuelKgEst;
    float used=-1;const float minutes=simulateFlightMinutes(q,si,&used);
    printf("QUOTE %s quick %.1f kg actual %.1f kg time %.2f min ratio %.3f\n",kAircraft[si].id,quick,used,minutes,quick/used);
    check(minutes>0&&used>0&&std::fabs(quick-used)<used*.25f,"local-mission quick quote follows actual jet flight consumption");
    c.useFlownTime(plan,q,minutes,used);
    check(plan.flown&&near(plan.fuelKgEst,used),"flown quote uses actual consumed kilograms");
    Career::LaunchPlan research;research.spec=kResearchJet;research.fuel=Career::LaunchPlan::FUEL_INCLUDED;
    c.useFlownTime(research,q,10,123.25f);
    check(near(research.fuelKgEst,123.25f),"research estimate never erases real fuel consumption");
  }
}

void saveAndCost() {
  Career c;c.newGame();c.money=1000000;c.fleet.clear();
  // Historical saved kilograms remain kilograms: no tank-percent migration or free uplift.
  c.fleet.push_back({6,c.location,1100,1});c.fleet.push_back({kAtlas,c.location,14000,1});
  const std::string path=(std::filesystem::temp_directory_path()/
    ("solace-jet-fuel-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".cfg")).string();
  check(c.save(path),"save old jet fuel quantities");Career d;check(d.load(path),"load old jet fuel quantities");
  check(d.fleet.size()==2&&d.fleet[0].fuel==1100&&d.fleet[1].fuel==14000,"old saved kilograms preserved exactly");
  std::string msg;int money=d.money;
  const int cost=(int)((kAircraft[6].maxFuel-1100)*d.fuelPrice(d.location,6));
  check(d.refuel(0,&msg)&&d.fleet[0].fuel==kAircraft[6].maxFuel&&money-d.money==cost,"refuel bills actual newly added kilograms");
  money=d.money;check(!d.refuel(0,&msg)&&money==d.money,"full tank cannot be billed twice");
  for(const char* suffix:{"",".bak",".tmp"})std::remove((path+suffix).c_str());
}
}
int main(int argc,char**argv) {
  int seconds=120,only=-1;bool meter=true,rangeRun=false;
  for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--long"))seconds=3600;if(!strcmp(argv[i],"--cruise-only"))meter=false;if(!strcmp(argv[i],"--only")&&i+1<argc)only=atoi(argv[++i]);if(!strcmp(argv[i],"--range")){seconds=12*3600;rangeRun=true;}}
  buildTestWorld();curves();if(meter){metering();hoverAndLoadout();missionQuotes();saveAndCost();}cruiseFlights(seconds,only,rangeRun);
  printf("Jet fuel: %d failures\n",failures);return failures?1:0;
}
