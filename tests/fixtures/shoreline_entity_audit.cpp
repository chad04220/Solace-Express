#include "world.h"
#include "entities.h"
#include <cstdio>
int main(){g_world.build();vec3 target(26960,std::max(0.f,g_world.height(26960,22300))+2,22300),cam=target+vec3(130,78,170);
 vec3 back=normalize(cam-target),right=normalize(cross(vec3(0,1,0),back)),up=cross(back,right);
 for(vec2 pixel:{vec2(105,232),vec2(123,230),vec2(378,205)}){
  vec3 dir=normalize(right*((2*pixel.x/1920-1)*tanf(58*DEG*.5f)*1920/1080)+up*((1-2*pixel.y/1080)*tanf(58*DEG*.5f))-back);
  int kind=0;Ent e{};float d=g_scenery.raycast(cam,dir,8000,&kind,&e);
  printf("PIXEL %.0f,%.0f ray %.6f %.6f %.6f hit %.3f kind %d %s entity(%.3f,%.3f,%.3f), ground %.6f\n",pixel.x,pixel.y,dir.x,dir.y,dir.z,d,kind,kind?kEntInfo[kind-1].name:"none",e.x,e.y,e.z,g_world.height(e.x,e.z));
 }
 size_t trees=0,buildings=0,seaStacks=0;int wetTrees=0,wetBuildings=0;
 for(int cz=Scenery::chunkOf(18300);cz<=Scenery::chunkOf(24300);++cz)for(int cx=Scenery::chunkOf(22960);cx<=Scenery::chunkOf(28960);++cx){
  auto c=g_scenery.ensure(cx,cz,2);if(!c)continue;
  for(int kind=0;kind<EK_COUNT;++kind)for(unsigned j=c->off[kind];j<c->off[kind+1];++j){auto&e=c->ents[j];float h=g_world.height(e.x,e.z,7);
   if(kind==EK_SEASTACK)++seaStacks;
   if(entClass(kind)==EC_TREE){++trees;if(h<.3f){++wetTrees;if(wetTrees<8)printf("WET TREE %s %.3f %.3f ground %.3f\n",kEntInfo[kind].name,e.x,e.z,h);}}
   if(entClass(kind)==EC_BUILDING&&kind!=EK_RWYLIGHT&&kind!=EK_PAPI){++buildings;if(h<.3f){++wetBuildings;if(wetBuildings<8)printf("WET BUILDING %s %.3f %.3f ground %.3f\n",kEntInfo[kind].name,e.x,e.z,h);}}
  }
 }
 printf("6km shoreline box: %zu trees (%d wet), %zu building/fixture instances (%d wet), %zu intentionally offshore sea stacks\n",trees,wetTrees,buildings,wetBuildings,seaStacks);
 return wetTrees||wetBuildings?1:0;
}
