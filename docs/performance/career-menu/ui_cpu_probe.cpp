#include "game.h"
#include "test_world.h"
#include "hangar_catalog.h"
#include <chrono>
#include <cstdio>
#include <cstdint>
static uint64_t planCalls=0;
extern "C" Career::LaunchPlan realPlan(const Career*,const Contract&,int,Career::Source) asm("__real__ZNK6Career4planERK8ContractiNS_6SourceE");
extern "C" Career::LaunchPlan wrappedPlan(const Career* career,const Contract& c,int spec,Career::Source src) asm("__wrap__ZNK6Career4planERK8ContractiNS_6SourceE");
extern "C" Career::LaunchPlan wrappedPlan(const Career* career,const Contract& c,int spec,Career::Source src) { ++planCalls;return realPlan(career,c,spec,src); }
struct GameTest {
 static int run() {
  buildTestWorld();buildStory();g_ren.W=1920;g_ren.H=1080;
  for(int story:{0,4}){
   Game g;g.initHeadless();g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;
   g.career.storyIndex=story;g.career.license=story?LIC_PPL:LIC_STUDENT;g.career.money=50000;
   const auto& c=g_story[story];g.career.location=c.from;g.career.refreshBoard();
   g.selAircraft=story?1:0;g.in.mx=g.in.my=-1;
   if(story){auto p=g.career.plan(c,g.selAircraft,Career::SRC_RENT);
#ifdef CACHE_PATCH
    auto key=menuPlan::quoteInputs(c,g.selAircraft);
#else
    auto key=fmt("%s|%d|%d|%d",c.id.c_str(),g.selAircraft,c.from,c.to);
#endif
    g.quoteFlown[key]={p.minutesEst,p.fuelKgEst};}
   auto frame=[&]{g.focusList.clear();g_ren.uiBegin();g.drawHub();};
   for(int i=0;i<3;++i)frame();
   planCalls=0;std::vector<double> samples;
   for(int i=0;i<120;++i){auto t=std::chrono::steady_clock::now();frame();samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}
   std::sort(samples.begin(),samples.end());double mean=0;for(auto m:samples)mean+=m;
   printf("%s 120 real UI generations: full_plan_calls=%llu mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",c.id.c_str(),(unsigned long long)planCalls,mean/120,samples[60],samples[114],samples.back());fflush(stdout);
  }
  for(int mode=0;mode<3;++mode){
   Game g;g.initHeadless();g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.career.license=LIC_ATP;g.career.money=10000000;
   for(int spec:kCareerAircraft)g.career.fleet.push_back({spec,g.career.location,kAircraft[spec].maxFuel*.5f,.8f});
   g.selHangar=1;g.in.mx=g.in.my=-1;
   auto frame=[&](int i){
    if(mode==1)g.selHangar=hangarSpecAt(i%hangarCatalogCount());
    if(mode==2)g.career.fleet[0].fuel=kAircraft[g.career.fleet[0].spec].maxFuel*((i%3)*.5f);
    auto fp=g.buildFrame();
    if(!fp.hangarPreview || fp.plane.model!=g.selHangar)return false;
    g.focusList.clear();g_ren.uiBegin();g.drawHub();return true;
   };
   for(int i=0;i<hangarCatalogCount();++i)if(!frame(i))return 2;
   planCalls=0;std::vector<double> samples;
   for(int i=0;i<120;++i){auto t=std::chrono::steady_clock::now();if(!frame(i))return 2;samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}
   std::sort(samples.begin(),samples.end());double mean=0;for(auto m:samples)mean+=m;
   printf("Hangar %s: 120 full-fleet UI+preview-CPU frames: full_plan_calls=%llu quote_job_started=%d mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",mode==0?"steady":mode==1?"aircraft-switches":"fuel-changes",(unsigned long long)planCalls,g.quoteJob.valid()?1:0,mean/120,samples[60],samples[114],samples.back());fflush(stdout);
  }
  return 0;
 }
};
int main(){return GameTest::run();}
