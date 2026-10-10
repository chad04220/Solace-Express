// Matched-toolchain array audit; not a portable floating-point regression test.
// Capture only with the src tree archived from 52317bb. The immutable expected
// heightmap fingerprint prevents capturing a revised world by accident.
#include "world.h"
#include <cstdio>
#include <cstring>

static uint64_t fingerprint(const std::vector<float>& v) {
  uint64_t h=14695981039346656037ull;const uint8_t* p=(const uint8_t*)v.data();
  for(size_t i=0;i<v.size()*sizeof(float);++i)h=(h^p[i])*1099511628211ull;return h;
}
static void hashes() {
  printf("hm %zu %016llx\n",g_world.hm.size(),(unsigned long long)fingerprint(g_world.hm));
  printf("tpV0 %zu %016llx\n",g_world.tpV0.size(),(unsigned long long)fingerprint(g_world.tpV0));
  for(int i=0;i<HMAX_LEVELS;++i)printf("hmax%d %zu %016llx\n",i,g_world.hmax[i].size(),(unsigned long long)fingerprint(g_world.hmax[i]));
  for(int i=0;i<TP_LEVELS;++i)printf("tpM%d %zu %016llx\n",i,g_world.tpM[i].size(),(unsigned long long)fingerprint(g_world.tpM[i]));
}
int main(int argc,char** argv) {
  if(argc!=3|| (strcmp(argv[1],"--capture")&&strcmp(argv[1],"--compare"))) {
    puts("usage: world_terrain_array_audit --capture|--compare BASELINE.bin");return 2;
  }
  g_world.build();hashes();
  constexpr uint64_t baseline=0xdadc1e51deff9a43ull;
  if(!strcmp(argv[1],"--capture")) {
    if(fingerprint(g_world.hm)!=baseline){puts("FAIL: capture source is not the expected immutable baseline/toolchain");return 1;}
    FILE* f=fopen(argv[2],"wb");if(!f)return 2;
    bool ok=fwrite(g_world.hm.data(),sizeof(float),g_world.hm.size(),f)==g_world.hm.size();fclose(f);return ok?0:2;
  }
  std::vector<float> old(g_world.hm.size());FILE* f=fopen(argv[2],"rb");if(!f)return 2;
  bool ok=fread(old.data(),sizeof(float),old.size(),f)==old.size()&&fgetc(f)==EOF;fclose(f);
  if(!ok||fingerprint(old)!=baseline){puts("FAIL: baseline array fingerprint/size mismatch");return 1;}
  size_t changed[4]={},outside=0,changedTexels=0,nonfinite=0;float radius=0,maxDelta=0;
  for(int j=0;j<HM_N;++j)for(int i=0;i<HM_N;++i){
    float x=-WORLD_HALF+(i+.5f)*HM_TEXEL,z=-WORLD_HALF+(j+.5f)*HM_TEXEL;
    float r=hypotf(x-25000.f,z+9000.f);bool any=false;
    for(int c=0;c<4;++c){size_t at=((size_t)j*HM_N+i)*4+c;
      nonfinite+=!std::isfinite(g_world.hm[at]);if(old[at]==g_world.hm[at])continue;
      ++changed[c];any=true;if(r>=520.f)++outside;maxDelta=std::max(maxDelta,fabsf(g_world.hm[at]-old[at]));
    }
    if(any){++changedTexels;radius=std::max(radius,r);}
  }
  printf("crater: changed texels=%zu, height=%zu, amplitude=%zu, lush=%zu, cold=%zu, outside520m=%zu, nonfinite=%zu\n",
    changedTexels,changed[0],changed[1],changed[2],changed[3],outside,nonfinite);
  printf("max modified radius=%.9g m; maximum bilinear support radius=%.9g m; largest scalar delta=%.9g\n",radius,radius+sqrtf(2.f)*HM_TEXEL,maxDelta);
  return outside||nonfinite||changed[2]||changed[3]?1:0;
}
