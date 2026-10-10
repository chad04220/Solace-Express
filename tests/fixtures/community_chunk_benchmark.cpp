// Standalone CPU streaming audit. Compile against current or archived baseline sources.
// Frozen camera positions keep the workload stable when settlements move.
#include "world.h"
#include "scenery.h"
#include "entities.h"
#include <chrono>
#include <cstdio>
#include <ctime>
static const Town frozenTowns[]={
  {"Port Verde", -29500, 10500, 1320, 2},
  {"Solace Capital", -3200, -1200, 1200, 2},
  {"Kaleo Town", 17400, 7000, 840, 1},
  {"Fjordhaven", 17500, -28600, 660, 1},
  {"Northpoint", -25300, -24100, 600, 1},
  {"Meadowbrook", -6600, 15700, 420, 0},
  {"Cedar Ridge", -23600, -5500, 360, 0},
  {"Orchard Valley", -15300, 3250, 330, 0},
  {"Harlan", 200, 22500, 210, 0},
  {"Palm Bay", 27600, 21100, 300, 0},
  {"Far Isle Resort", 35000, 35000, 360, 0},
  {"Lighthouse Key", -33600, 26200, 130, 0},
  {"Westvale", -30000, -2000, 360, 0},
  {"Riverton", -12000, -1000, 420, 0},
  {"Greenhollow", -4000, 9000, 360, 0},
  {"Saltmarsh", -20000, 22000, 300, 0},
  {"Kaleo Springs", 23000, 14000, 360, 0},
  {"Ice Harbor", 27000, -27500, 270, 0},
};
int main(){
 g_world.build();size_t all=0,buildings=0,trees=0,chunks=0;double total=0,maxTown=0,totalCpu=0;
 for(int t=0;t<int(sizeof(frozenTowns)/sizeof(frozenTowns[0]));++t){const Town& town=frozenTowns[t];int cx=Scenery::chunkOf(town.x),cz=Scenery::chunkOf(town.z);size_t count=0,build=0,tree=0,chunk=0;
  auto start=std::chrono::steady_clock::now();auto cpuStart=std::clock();
  for(int dz=-3;dz<=3;++dz)for(int dx=-3;dx<=3;++dx){
   float x0=Scenery::chunkX0(cx+dx),z0=Scenery::chunkX0(cz+dz);
   float ex=std::max(std::max(x0-town.x,town.x-x0-Scenery::CH),0.f),ez=std::max(std::max(z0-town.z,town.z-z0-Scenery::CH),0.f);
   if(hypotf(ex,ez)>700.f)continue;
   auto c=g_scenery.ensure(cx+dx,cz+dz,2);if(!c)continue;++chunk;count+=c->ents.size();
   for(int k=0;k<EK_COUNT;++k){size_t n=c->off[k+1]-c->off[k];if(entClass(k)==EC_BUILDING)build+=n;else if(entClass(k)==EC_TREE)tree+=n;}
  }
  double cpuMs=1000.*(std::clock()-cpuStart)/CLOCKS_PER_SEC;totalCpu+=cpuMs;double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;maxTown=std::max(maxTown,ms);
  printf("%s,%zu,%zu,%zu,%zu,%.3f,%.3f\n",town.name,chunk,count,build,tree,ms,cpuMs);chunks+=chunk;all+=count;buildings+=build;trees+=tree;
  g_scenery.clear();
 }
 printf("TOTAL,%zu,%zu,%zu,%zu,%.3f; maxTownMs=%.3f; cpuMs=%.3f\n",chunks,all,buildings,trees,total,maxTown,totalCpu);
}
