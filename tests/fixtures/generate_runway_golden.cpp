// Baseline capture utility. Compile ONLY with the src/ tree archived from
// 52317bb1f74a5ee9e66e063e2f17d5be786955a0, never with the revised checkout.
#include "world.h"
#include "airport_layout.h"
#include "entities.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

static uint32_t bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static std::vector<float> axis(float half, float margin, float step) {
  std::vector<float> v;
  for (float p = -half - margin; p <= half + margin; p += step) v.push_back(p);
  for (float p : {-half-margin, -half, -half+.25f, 0.f, half-.25f, half, half+margin}) v.push_back(p);
  std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); return v;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  g_world.build();
  FILE* f = fopen(argv[1], "w"); if (!f) return 2;
  fprintf(f, "SOLACE_RUNWAY_GOLDEN 1 52317bb1f74a5ee9e66e063e2f17d5be786955a0\n%zu\n", g_world.airports.size());
  size_t total = 0;
  for (const auto& a : g_world.airports) {
    fprintf(f, "%s %a %a %a %a %a %a %d %d %d %d %d\n", a.code,
      a.x,a.z,a.elev,a.heading,a.length,a.width,a.surface,a.size,(int)a.hospital,a.rwyNumber(false),a.rwyNumber(true));
    fprintf(f,"\"%s\" \"%s\"\n",a.name,a.blurb);
    for (vec3 p : {a.dir(),a.threshold(false),a.threshold(true)}) fprintf(f, "%a %a %a\n", p.x,p.y,p.z);
    auto us = axis(a.length*.5f,60.f,5.f), vs = axis(a.width*.5f,25.f,2.f);
    fprintf(f, "%zu %zu\n", us.size(),vs.size());
    for (float p : us) fprintf(f,"%a ",p); fprintf(f,"\n");
    for (float p : vs) fprintf(f,"%a ",p); fprintf(f,"\n");
    std::vector<uint32_t> heights; float maxNormal = 0, maxElevation = 0;
    for (float u : us) for (float v : vs) {
      vec3 p=aptWorld(a,u,v,0), n=g_world.normal(p.x,p.z);
      float h=g_world.height(p.x,p.z);
      if (h!=g_world.height(p.x,p.z,7)||h!=g_world.height(p.x,p.z,11)) return 3;
      heights.push_back(bits(h)); maxNormal=std::max(maxNormal,std::max(fabsf(n.x),fabsf(n.z)));
      maxElevation=std::max(maxElevation,fabsf(h-a.elev));
    }
    std::vector<std::pair<unsigned,uint32_t>> runs;
    for (uint32_t b : heights) { if (!runs.empty()&&runs.back().second==b) ++runs.back().first; else runs.push_back({1,b}); }
    fprintf(f,"%zu %a %a\n",runs.size(),maxNormal,maxElevation);
    for (auto r : runs) fprintf(f,"%u %08x\n",r.first,r.second);
    float x0=1e9f,z0=1e9f,x1=-1e9f,z1=-1e9f;
    for (float u : {-a.length*.5f-430.f,a.length*.5f+430.f})
      for (float v : {-a.width*.5f-65.f,a.width*.5f+65.f}) {
        vec3 p=aptWorld(a,u,v,0); x0=std::min(x0,p.x); x1=std::max(x1,p.x); z0=std::min(z0,p.z); z1=std::max(z1,p.z);
      }
    std::vector<AptItem> lights;
    for (int cz=Scenery::chunkOf(z0);cz<=Scenery::chunkOf(z1);++cz)
      for (int cx=Scenery::chunkOf(x0);cx<=Scenery::chunkOf(x1);++cx) {
        auto ch=g_scenery.ensure(cx,cz,1); if(!ch) continue;
        for (int k : {EK_RWYLIGHT,EK_PAPI}) for (unsigned j=ch->off[k];j<ch->off[k+1];++j)
          if (k==EK_PAPI||ch->ents[j].seed<4.f) lights.push_back({k,ch->ents[j]});
      }
    std::sort(lights.begin(),lights.end(),[](const AptItem& a,const AptItem& b){
      if(a.kind!=b.kind)return a.kind<b.kind;
      if(a.e.x!=b.e.x)return a.e.x<b.e.x;
      if(a.e.z!=b.e.z)return a.e.z<b.e.z;
      return a.e.seed<b.e.seed;
    });
    fprintf(f,"%zu\n",lights.size());
    for(const auto& p:lights) fprintf(f,"%d %a %a %a %a %a %a %a %a\n",p.kind,p.e.x,p.e.y,p.e.z,p.e.yaw,p.e.sx,p.e.sy,p.e.sz,p.e.seed);
    total+=heights.size();
    fprintf(stderr,"%s: %zu samples, %zu runs, max elevation delta %.9g, normal %.9g\n",a.code,heights.size(),runs.size(),maxElevation,maxNormal);
  }
  fclose(f); fprintf(stderr,"Captured %zu immutable terrain samples.\n",total);
}
