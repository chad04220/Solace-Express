#include "procedural_regions.h"
#include <chrono>
#include <cstdio>
int main(){for(const char* id : {"pr1-1-9-19","pr1-4-17-7"}){procedural::Recipe r(*procedural::find(id));volatile double total=0.;auto begin=std::chrono::steady_clock::now();for(int k=0;k<8;++k)for(int z=0;z<512;++z)for(int x=0;x<512;++x){float h[4];r.sample(-37991.f+148.4f*x,-37991.f+148.4f*z,h);total+=h[0];}double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();printf("%s sample_count=2097152 seconds=%.6f ns_per_sample=%.1f memory=%zu sum=%.3f\n",id,s,s*1e9/2097152.,r.memoryBytes(),total);}}
