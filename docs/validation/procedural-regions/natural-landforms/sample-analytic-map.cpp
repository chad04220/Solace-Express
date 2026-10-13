#include "procedural_regions.h"
#include <cstdio>
int main(int argc,char**argv){procedural::Recipe r(*procedural::find(argv[1]));FILE*f=fopen(argv[2],"wb");const int n=512;fwrite(&n,4,1,f);for(int z=0;z<n;++z)for(int x=0;x<n;++x){float h[4];r.sample(-38000.f+(x+.5f)*76000.f/n,-38000.f+(z+.5f)*76000.f/n,h);fwrite(h,4,4,f);}fclose(f);}
