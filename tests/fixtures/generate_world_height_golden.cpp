// Audit-only capture. Compile against src archived from immutable 52317bb.
#include "world.h"
#include "scenery.h"
#include <cstdio>
#include <vector>
int main(int argc,char** argv){
 if(argc!=2)return 2;g_world.build();FILE* f=fopen(argv[1],"w");if(!f)return 2;
 std::vector<vec2> points;
 // Entire archipelago, shorelines and water: 625 m regular lattice.
 for(int z=0;z<=128;++z)for(int x=0;x<=128;++x)points.emplace_back(-40000.f+625.f*x,-40000.f+625.f*z);
 // Extra 200 m probes in and around every original settlement's terrain influence.
 for(int town=0;town<kNumTowns;++town)for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x)
  points.emplace_back(kTowns[town].x+x*200.f,kTowns[town].z+z*200.f);
 fprintf(f,"SOLACE_WORLD_HEIGHT_GOLDEN 1 52317bb1f74a5ee9e66e063e2f17d5be786955a0 %zu\n",points.size());
 for(const auto& p:points)fprintf(f,"%a %a %a %a %a\n",p.x,p.y,g_world.height(p.x,p.y,7),g_world.height(p.x,p.y,8),g_world.height(p.x,p.y,11));
 fclose(f);fprintf(stderr,"Captured %zu immutable full-world height samples at three octave counts.\n",points.size());
}
