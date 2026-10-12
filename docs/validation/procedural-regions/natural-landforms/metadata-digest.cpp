#include "procedural_regions.h"
#include <cstdio>
#include <cstring>
static uint64_t hash=1469598103934665603ULL;
void bytes(const void* p,size_t n){auto b=static_cast<const unsigned char*>(p);while(n--){hash^=*b++;hash*=1099511628211ULL;}}
template<class T> void put(T v){bytes(&v,sizeof v);}
void text(const std::string& s){put(s.size());bytes(s.data(),s.size());}
int main(){for(uint64_t seed:{procedural::kDefaultWorldSeed, uint64_t(1),uint64_t(2),uint64_t(17),uint64_t(63),uint64_t(255),uint64_t(1024),uint64_t(54321),UINT64_MAX}){hash=1469598103934665603ULL;int n=0;for(int f=0;f<6;++f)for(int y=0;y<32;++y)for(int x=0;x<32;++x){char id[30];snprintf(id,sizeof id,"pr1-%d-%d-%d",f,x,y);auto d=procedural::find(id,seed);if(!d)continue;++n;text(d->id);text(d->name);put(d->worldSeed);put(d->seed);put(d->generatorVersion);put(d->origin.latitude);put(d->origin.longitude);put(d->archetype);procedural::Recipe r(*d);put(r.airports.size());for(const auto&a:r.airports){text(a.code);text(a.name);put(a.x);put(a.z);put(a.elev);put(a.heading);put(a.length);put(a.width);put(a.surface);put(a.size);text(a.blurb);put(a.hospital);}put(r.towns.size());for(const auto&t:r.towns){text(t.name);put(t.x);put(t.z);put(t.r);put(t.kind);}}printf("seed=%llu regions=%d metadata=%016llx\n",(unsigned long long)seed,n,(unsigned long long)hash);}}
