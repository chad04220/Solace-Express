#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/hive_world_collision.h"
#include "../src/entities.h"
#include "test_world.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main() {
  buildTestWorld();
  int checked=0;
  auto check=[&](bool yes){++checked;assert(yes);};
  check(hiveWorldSweep(vec3(49000,100,49000),vec3(51000,100,49000),0)>1);
  float f=hiveWorldSweep(vec3(49000,30,49000),vec3(49000,-30,49000),0);
  check(f>.49f&&f<.51f);
  f=hiveWorldSweep(vec3(49000,30,49000),vec3(49000,-30,49000),10);
  check(f>.32f&&f<.34f);
  check(hiveWorldSweep(vec3(49000,-1,49000),vec3(49000,-1,49000),0)==0);
  check(hiveWorldSweep(vec3(0,10000,0),vec3(1000,10000,0),20)>1);
  check(hiveWorldSweep(vec3(0,100,0),vec3(std::numeric_limits<float>::quiet_NaN(),100,0),0)==0);
  check(hiveWorldSweep(vec3(0,100,0),vec3(1,100,0),-1)==0);
  for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
    check(hiveWorldSweep(vec3(bad,100,0),vec3(1,100,0),0)==0);
    check(hiveWorldSweep(vec3(0,100,0),vec3(1,bad,0),0)==0);
    check(hiveWorldSweep(vec3(0,100,0),vec3(1,100,0),bad)==0);
  }
  check(hiveWorldSweep(vec3(-std::numeric_limits<float>::max(),100,0),vec3(std::numeric_limits<float>::max(),100,0),0)==0);
  // Actual authored scenery: increasing the sweep radius cannot miss an earlier centreline hit.
  for(vec2 spot:{vec2(-8000,14600),vec2(-20000,-5000),vec2(1500,-3500)}) {
    auto* chunk=g_scenery.ensure(Scenery::chunkOf(spot.x),Scenery::chunkOf(spot.y),2);
    if(!chunk)continue;
    for(int kind=0;kind<EK_COUNT;++kind) {
      if(kind==EK_RWYLIGHT||kind==EK_PAPI)continue;
      for(uint32_t i=chunk->off[kind];i<chunk->off[kind+1]&&i<chunk->off[kind]+2;++i) {
        const auto e=chunk->ents[i];const auto& spec=kEntInfo[kind];
        vec3 p(e.x,e.y+spec.h*e.sy*.65f,e.z);
        float span=std::max(spec.hx*e.sx,spec.hz*e.sz)+30;
        vec3 a=p-vec3(span,0,0),dir(1,0,0);
        float small=g_scenery.sweepSphere(a,dir,span*2,.4f);
        float large=g_scenery.sweepSphere(a,dir,span*2,8);
        if(small>=0)check(large>=0&&large<=small+.001f);
      }
    }
  }
  check(checked>20);
  printf("hive_world_collision: %d checks passed\n",checked);
}
