#include "career.h"
#include "entities.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <ctime>

int main(int argc,char** argv) {
  if(argc!=2)return 2;
  g_world.build(argv[1], "solace-test-run", false);
  if(!g_world.fromCache){fprintf(stderr,"World cache rejected\n");return 3;}
  buildStory();
  Career career; career.license=LIC_ATP; career.money=2000000;
  const char* ids[]={"L1","C1","P1","A4"};
  for(const char* id:ids){
    auto it=std::find_if(g_story.begin(),g_story.end(),[&](const Contract& c){return c.id==id;});
    if(it==g_story.end())continue;
    const Contract& c=*it;
    int spec=c.forceAircraft>=0?c.forceAircraft:c.requireSpec>=0?c.requireSpec:1;
    career.location=c.from;
    Plane::perf(&kAircraft[spec]);
    auto t=std::chrono::steady_clock::now(); auto first=career.plan(c,spec,Career::SRC_RENT);
    double firstMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    std::vector<double> samples; volatile float sink=0;
    const auto cpu=std::clock();
    for(int n=0;n<100;n++) {t=std::chrono::steady_clock::now();auto p=career.plan(c,spec,Career::SRC_RENT);sink+=p.minutesEst+p.fuelKgEst;samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}
    const double cpuMs=1000.*(std::clock()-cpu)/CLOCKS_PER_SEC/100.;
    std::sort(samples.begin(),samples.end());
    printf("%s %s->%s aircraft=%d cold_plan_ms=%.3f warm_p50_ms=%.3f p95=%.3f max=%.3f process_cpu_mean_ms=%.3f minutes=%.4f fuel_kg=%.4f chunks=%zu\n",id,g_world.airports[c.from].code,g_world.airports[c.to].code,spec,firstMs,samples[50],samples[95],samples.back(),cpuMs,first.minutesEst,first.fuelKgEst,g_scenery.generated());fflush(stdout);
  }
}
