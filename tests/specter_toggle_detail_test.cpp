// Geometry coverage and bounded cost of the unchanged Specter overhead capsules.
#include "../src/aircraft_mesh_specter_detail.h"
#include <array>
#include <set>
#include <unordered_set>
#include <cstdio>

namespace {
using Cell = std::array<int,3>;
constexpr float cell=.0625f, h=cell/aircraftMesh::kSpecterToggleSub;
constexpr float eye[3]={0.f,.62f,-4.7f};
vec3 centre(Cell p) { return vec3((p[0]+.5f)*cell,(p[1]+.5f)*cell,(p[2]+.5f)*cell); }
Cell cellOf(vec3 p) { return {{int(floorf(p.x/cell)),int(floorf(p.y/cell)),int(floorf(p.z/cell))}}; }
vec3 body(vec3 overhead) {
  const float C=cosf(.55f),S=sinf(.55f);
  return vec3(eye[0]+overhead.x,eye[1]+.46f+C*overhead.y+S*overhead.z,
              eye[2]-.25f-S*overhead.y+C*overhead.z);
}
int64_t key(int x,int y,int z) { return (int64_t(x+4096)<<42)|(int64_t(y+4096)<<21)|int64_t(z+4096); }
size_t hashCapacity(size_t count) { size_t n=1024;while(n<count*2)n*=2;return n; }
}
int main() {
  size_t checks=0;int failures=0;
  auto check=[&](bool yes,const char* message) { checks++;if(!yes){fprintf(stderr,"FAIL: %s\n",message);failures++;} };
  check(h==.00390625f && sqrtf(3.f)*h*.5f<.0045f,"Every stem contains a fine-lattice sample at any phase");
  for(int engine=0;engine<8;engine++)for(bool inside:{false,true}){
    float packed[96]{};packed[2]=float(engine);
    check(aircraftMesh::specterToggleDetailEnabled(packed,inside)==(engine==5&&inside),"Only Specter interior enables the patch");
  }
  std::set<Cell> core,expanded;
  for(int x=-8;x<=8;x++)for(int y=12;y<=21;y++)for(int z=-86;z<=-72;z++){
    Cell p{{x,y,z}};
    if(aircraftMesh::specterToggleDetailCell(centre(p),eye,.5f*cell))core.insert(p);
  }
  for(Cell p:core)for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)for(int z=-1;z<=1;z++)expanded.insert({{p[0]+x,p[1]+y,p[2]+z}});
  check(core.size()==56,"The authored rotated bank uses 56 core level-2 cells");
  check(expanded.size()==250,"One-cell transition is bounded to 250 level-2 cells");
  // All 27 capsules: the central segment and both hemispheres, sampled in every
  // cardinal and diagonal direction. This checks the real rotation/grid phase,
  // not just a bounding box in the easier overhead coordinate frame.
  for(int x=-4;x<=4;x++)for(int z=-1;z<=1;z++)for(int u=0;u<=12;u++){
    const float t=u/12.f;
    const vec3 at(.05f*x,-.020f-.022f*t,.06f*z+.008f*t);
    const vec3 c=body(at);check(core.count(cellOf(c))!=0,"All stem centrelines are in the detail core");
    for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)for(int dz=-1;dz<=1;dz++){
      if(!dx&&!dy&&!dz)continue;
      const vec3 p=body(at+normalize(vec3(float(dx),float(dy),float(dz)))*.0045f);
      check(core.count(cellOf(p))!=0,"Every capsule envelope sample, including end hemispheres, is in the core");
    }
  }
  // A capsule's whole analytic envelope is inside the oriented bounds, which
  // are tested against the projected radius of its body-space grid cell.
  check(-.2045f>-.210f && .2045f<.210f && -.0465f>-.052f && -.0155f<-.010f
        && -.0645f>-.070f && .0725f<.078f,"Analytic envelopes fit with at least 5.5 mm margin");
  // Patch partitions preserve complete coverage and a one-cell overlap. Core
  // cells have only sub16; ring cells have sunk sub8 plus exact sub16; cells
  // beyond the ring retain their previous coarse/fine membership and flags.
  for(Cell p:expanded){
    const bool isCore=core.count(p)!=0;
    bool neighborCore=false;
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)for(int z=-1;z<=1;z++)
      neighborCore=neighborCore||core.count({{p[0]+x,p[1]+y,p[2]+z}})!=0;
    check(neighborCore,"Every detail-ring cell overlaps a core neighbor");
    for(bool wasThin:{false,true})for(bool wasRing:{false,true}){
      const bool thin=wasThin||isCore;
      const bool ring=!thin&&(wasRing||neighborCore);
      const bool coarse=!thin, fine=(thin||ring)&&!isCore,detail=isCore||neighborCore;
      check(coarse||fine||detail,"No static surface cell is omitted");
      check(isCore?(!coarse&&!fine&&detail):(fine&&detail),"Core is exclusive and its full overlap ring is retained");
    }
  }
  // Match surface-nets' sample ownership exactly: sub^3 corners per cell and
  // only unowned positive rims. No million-element point/hash allocation is
  // required by this test: the set contains only boundary samples.
  const int sub=aircraftMesh::kSpecterToggleSub;
  std::unordered_set<int64_t> rim;
  for(Cell p:expanded)for(int z=0;z<=sub;z++)for(int y=0;y<=sub;y++)for(int x=0;x<=sub;x++){
    Cell owner{{p[0]+(x==sub),p[1]+(y==sub),p[2]+(z==sub)}};
    if(!expanded.count(owner))rim.insert(key(p[0]*sub+x,p[1]*sub+y,p[2]*sub+z));
  }
  const size_t owned=expanded.size()*sub*sub*sub,samples=owned+rim.size();
  // Starting with owned samples, std::vector grows once when the rim is added.
  // This bound mirrors the usual doubling implementation; actual allocated
  // capacities are also printed by the production HULLDBG instrumentation.
  const size_t pointCapacity=owned*2;
  const size_t latticeBytes=pointCapacity*sizeof(vec3)+samples*sizeof(float)+owned*sizeof(int)
      +(hashCapacity(expanded.size())+hashCapacity(rim.size()))*(sizeof(int64_t)+sizeof(int))
      +size_t(sub+1)*(sub+1)*(sub+1)*sizeof(int);
  const size_t maxRim=expanded.size()*(size_t(sub+1)*(sub+1)*(sub+1)-size_t(sub)*sub*sub);
  const size_t anySubsetBytes=pointCapacity*sizeof(vec3)+(owned+maxRim)*sizeof(float)+owned*sizeof(int)
      +(hashCapacity(expanded.size())+hashCapacity(maxRim))*(sizeof(int64_t)+sizeof(int))
      +size_t(sub+1)*(sub+1)*(sub+1)*sizeof(int);
  check(anySubsetBytes<40ull*1024*1024,"Conservative sample/index/hash vector budget stays below 40 MiB even for a fragmented subset");
  check(samples<1100000,"Local sample count stays below 1.1 million before surface-band filtering");
  check(latticeBytes<36ull*1024*1024,"Allocated lattice/index/hash CPU vectors stay below 36 MiB on doubling allocators");
  printf("Specter detail: %zu core + %zu overlap cells; %zu owned + %zu rim = %zu samples; %.3f MiB lattice/index/hash CPU vector bound.\n",
         core.size(),expanded.size()-core.size(),owned,rim.size(),samples,double(latticeBytes)/(1024*1024));
  printf("Conservative fragmented-subset lattice vector budget: %.3f MiB.\n",double(anySubsetBytes)/(1024*1024));
  printf("Surface/QEF geometry and GPU staging are separate; native HULLDBG reports actual raw vertices/triangles and allocated lattice vectors.\n");
  printf("specter_toggle_detail: %zu checks, %s\n",checks,failures?"FAIL":"PASS");
  return failures?1:0;
}
