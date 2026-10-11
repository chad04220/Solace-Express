// CPU-only career-menu regression: production UI generation, no window/GL context.
#include "../src/game.h"
#include "test_world.h"
#include "../src/hangar_catalog.h"
#include <chrono>
#include <cstdio>
#include <future>

static bool same(const Career::LaunchPlan& a, const Career::LaunchPlan& b) {
  return a.spec==b.spec && a.src==b.src && a.startAirport==b.startAirport && a.positioning==b.positioning &&
    a.ferry==b.ferry && a.hire==b.hire && a.fuel==b.fuel && a.fuelKgEst==b.fuelKgEst &&
    a.minutesEst==b.minutesEst && a.minutesSigma==b.minutesSigma && a.fuelLoadKg==b.fuelLoadKg &&
    a.fuelUpliftKg==b.fuelUpliftKg && a.fuelCostEst==b.fuelCostEst && a.net==b.net &&
    a.challenge==b.challenge && a.tCruise==b.tCruise && a.tClimb==b.tClimb && a.tOrbit==b.tOrbit &&
    a.tApproach==b.tApproach && a.flown==b.flown && a.flownFailed==b.flownFailed;
}
struct GameTest {
  static int run() {
    buildTestWorld(); buildStory();
    int fails=0, checks=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++fails;printf("FAIL: %s\n",name);}};
    Game g; g.initHeadless(); g.career.license=LIC_PPL; g.career.money=50000;
    Contract c=g_story[4]; int spec=1; auto src=Career::SRC_OWNED;
    g.career.location=c.from; g.career.fleet={{spec,c.from,20.f,1.f}};
    auto parity=[&](const char* name, bool continuing=false) {
      auto want=g.career.plan(c,spec,src);
      if(continuing)g.continuationWaivers(want,c,spec,src);
      g.applyQuote(c,want,false);
      g.career.planFuel(want,c,g.chosenFuel(c,spec,src,want));
      auto got=g.menuLaunchPlan(c,spec,src,continuing,false);
      check(same(got,want),name); return got;
    };
    auto first=parity("all cached fields exactly match original fresh calculation");
    unsigned builds=g.menuPlanCache.builds;
    for(int i=0;i<200;++i)check(same(first,g.menuLaunchPlan(c,spec,src,false,false)),"unchanged plan stays exact");
    check(g.menuPlanCache.builds==builds,"200 unchanged frames perform zero full plans");
    g.launchFuelKg=40; parity("fuel adjustment uses current selected tank");
    check(g.menuPlanCache.builds==builds,"selected uplift never invalidates route plan");
    g.launchFuelKg=0.5f; auto low=parity("second same-frame fuel change is live");
    check(low.fuelLoadKg==0.5f,"gate sees final fuel choice");
    g.launchFuelKg=-1;
    auto miss=[&](const char* name){unsigned before=g.menuPlanCache.builds;parity(name);check(g.menuPlanCache.builds==before+1,"changed input rebuilds exactly once");};
    c.to=g_world.findAirport("CAP");miss("same-ID destination invalidates");
    c.wx.windFrom+=13.125f;miss("same-ID wind invalidates");
    c.cargoKg+=25;miss("same-ID payload invalidates");
    c.pax+=1;miss("passenger comfort invalidates");
    c.wps.push_back({-2000,4000,900});miss("waypoint count invalidates");
    c.wps[0].x+=0.03125f;miss("exact waypoint coordinate invalidates");
    c.wpStart=1;miss("continuation checkpoint invalidates");
    c.from=g_world.findAirport("PVI");miss("continuation departure invalidates");
    c.wxEnd.cloudCover=.9f;c.wxShift=true;miss("forecast invalidates");
    g.career.fleet[0].fuel=75;miss("owned tank change invalidates raw economics");
    g.career.fleet[0].location=g_world.findAirport("MDB");miss("owned ferry location invalidates");
    g.career.location=c.to;miss("career location invalidates positioning");
    g.career.money=100;miss("courtesy positioning threshold invalidates");
    ++g_scenery.wreckRev;miss("destroyed-obstacle revision invalidates");
    g_groundPits.push_back({31000,31000,10,3});miss("terrain damage invalidates");g_groundPits.clear();
    spec=0;src=Career::SRC_RENT;parity("same-frame aircraft/source change uses new plan");
    check(g.menuPlanCache.base.spec==0,"cache tracks newly selected aircraft");
    spec=1;src=Career::SRC_OWNED;c=g_story[4];g.career.location=c.from;g.career.money=50000;
    g.career.job.emplace();g.career.job->spec=spec;g.career.job->src=src;
    g.career.job->positioningPaid=true;g.career.job->ferryPaid=true;g.career.job->hirePaid=true;
    parity("continuation waivers are applied fresh",true);builds=g.menuPlanCache.builds;
    g.career.job->ferryPaid=false;parity("changed waiver uses unmodified cached base",true);
    check(g.menuPlanCache.builds==builds,"waiver changes require no terrain search");
    g.career.job.reset();
    g.career.fleet[0].fuel=0;
    // Make a completed flight quote disagree deliberately with the quick estimate.
    g.quoteFlown[menuPlan::quoteInputs(c,spec)]={22.f,12.f};
    auto quoted=parity("display and gate use the same completed flown quote");
    check(quoted.flown && quoted.fuelKgEst==12.f && quoted.fuelLoadKg==15.f,"owned default tank follows flown fuel with reserve");
    builds=g.menuPlanCache.builds;
    g.quoteFlown[menuPlan::quoteInputs(c,spec)]={26.f,20.f};
    quoted=parity("new completed quote replaces old displayed estimate immediately");
    check(quoted.fuelLoadKg==25 && g.menuPlanCache.builds==builds,"new quote updates tank without full replan");
    // Full eligible payload: a full tank exceeds design mass, while this short
    // route's quick default can fit. A newly flown quote must disable the gate.
    {
      Contract boundary=c; const auto& sp=kAircraft[spec];
      boundary.cargoKg=(int)sp.cargoKg;boundary.pax=sp.pax;
      auto quick=g.career.plan(boundary,spec,src);
      const float quickTank=g.chosenFuel(boundary,spec,src,quick);
      auto heavy=[&](float tank){return sp.emptyMass+tank+boundary.cargoKg+boundary.pax*85.f+85.f>sp.maxMass()+.5f;};
      g.quoteFlown[menuPlan::quoteInputs(boundary,spec)]={30,sp.maxFuel};
      auto gate=g.menuLaunchPlan(boundary,spec,src,false,false);
      check(!heavy(quickTank) && heavy(gate.fuelLoadKg), "flown quote crosses actual overweight boundary");
      check(same(gate,g.finalizeLaunchPlan(boundary,spec,src)),"menu gate agrees with final launch fuel and economics");
    }
    // Fuel policies remain exact and cheap after the route estimate is cached.
    g.launchFuelKg=-1;g.career.fleet[0].fuel=80;
    auto floor=g.menuLaunchPlan(c,spec,src,false,false);
    check(floor.fuelLoadKg>=80,"owned default never discards existing tank fuel");
    auto rental=g.menuLaunchPlan(c,spec,Career::SRC_RENT,false,false);
    check(rental.fuelLoadKg==kAircraft[spec].maxFuel,"rental default is a full tank");
    g.launchFuelKg=kAircraft[spec].maxFuel+500;
    check(g.menuLaunchPlan(c,spec,src,false,false).fuelLoadKg==kAircraft[spec].maxFuel,"explicit fuel choice clamps to tank capacity");
    g.launchFuelKg=-1;g.career.fleet[0].fuel=0;
    // Exercise each waiver with a genuinely nonzero fee, and fresh aircraft hire.
    {
      g.career.location=c.to;g.career.fleet[0].location=c.to;
      g.career.job.emplace();auto& j=*g.career.job;j.spec=spec;j.src=src;
      auto paid=g.menuLaunchPlan(c,spec,src,true,false);unsigned before=g.menuPlanCache.builds;
      check(paid.positioning>0 && paid.ferry>0,"waiver fixture starts with real positioning and ferry fees");
      j.positioningPaid=true;j.ferryPaid=true;
      auto waived=g.menuLaunchPlan(c,spec,src,true,false);
      check(waived.positioning==0 && waived.ferry==0 && g.menuPlanCache.builds==before,"paid positioning and ferry change without replanning");
      j.spec=0;
      check(g.menuLaunchPlan(c,spec,src,true,false).ferry==paid.ferry,"changed aircraft owes its own ferry fee");
      j.spec=spec;j.src=Career::SRC_RENT;j.hirePaid=false;
      auto hired=g.menuLaunchPlan(c,spec,Career::SRC_RENT,true,false);before=g.menuPlanCache.builds;
      check(hired.hire>0,"rental waiver fixture has nonzero hire");
      j.hirePaid=true;
      check(g.menuLaunchPlan(c,spec,Career::SRC_RENT,true,false).hire==0 && g.menuPlanCache.builds==before,"paid hire changes without replanning");
      j.src=Career::SRC_OWNED;
      check(g.menuLaunchPlan(c,spec,Career::SRC_RENT,true,false).hire==hired.hire,"new rental source owes its own hire");
      g.career.job.reset();g.career.location=c.from;
    }
    auto oldKey=menuPlan::quoteInputs(c,spec);c.wx.windSpeed+=.03125f;
    auto changed=parity("same-ID weather rejects stale flown quote");
    check(!changed.flown && oldKey!=menuPlan::quoteInputs(c,spec),"quote identity covers exact forecast");
    // Harvest an old ready job under its captured key; do not assign it to a new selection.
    std::promise<std::pair<float,float>> completed;
    g.quoteJob=completed.get_future();g.quoteJobKey=oldKey;completed.set_value({29,30});
    changed=parity("late old result is not applied to changed contract");check(!changed.flown,"late result remains keyed to original request");
    Game other;other.initHeadless();other.career=g.career;
    auto separate=other.menuLaunchPlan(c,spec,src,false,false);
    check(other.menuPlanCache.builds==1 && same(separate,changed),"independent Game owns its own cache");
    // Exercise real career UI generation. Lessons have no async flight quote.
    Game ui;ui.initHeadless();ui.screen=SCR_HUB;ui.hubTab=TAB_CONTRACTS;
    g_ren.W=1920;g_ren.H=1080;ui.in.mx=ui.in.my=-1;
    auto frame=[&]{ui.focusList.clear();g_ren.uiBegin();ui.drawHub();};
    frame();frame();builds=ui.menuPlanCache.builds;
    auto t=std::chrono::steady_clock::now();
    for(int i=0;i<120;++i)frame();
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count()/120;
    check(ui.menuPlanCache.builds==builds,"real Contracts UI: 120 steady frames perform zero full plans");
    ui.launchFuelKg=10;frame();check(ui.menuPlanCache.builds==builds,"real Contracts UI fuel changes reuse route");
    printf("Real Contracts UI CPU-only steady average %.4f ms; %u full plans before, %u after 120 frames.\n",ms,builds,ui.menuPlanCache.builds);
    // Hangar never quotes a route, even with the full fleet and changing inputs.
    Game hangar;hangar.initHeadless();hangar.screen=SCR_HUB;hangar.hubTab=TAB_HANGAR;
    hangar.career.license=LIC_ATP;hangar.career.money=10000000;hangar.in.mx=hangar.in.my=-1;
    for(int spec:kCareerAircraft)hangar.career.fleet.push_back({spec,hangar.career.location,kAircraft[spec].maxFuel*.5f,.8f});
    for(int mode=0;mode<3;++mode)for(int i=0;i<120;++i){
      if(mode==1)hangar.selHangar=hangarSpecAt(i%hangarCatalogCount());
      if(mode==2)hangar.career.fleet[0].fuel=kAircraft[hangar.career.fleet[0].spec].maxFuel*((i%3)*.5f);
      auto preview=hangar.buildFrame();
      check(preview.hangarPreview && preview.plane.model==hangar.selHangar,"Hangar preview follows selected aircraft");
      hangar.focusList.clear();g_ren.uiBegin();hangar.drawHub();
    }
    check(hangar.menuPlanCache.builds==0 && !hangar.quoteJob.valid() && hangar.quoteFlown.empty(),"Hangar steady/selection/fuel frames never plan or start flight quotes");
    printf("%d checks, %d failures. No GPU, display cadence or RTX FPS claim.\n",checks,fails);
    return fails?1:0;
  }
};
int main(){return GameTest::run();}
