#include "entity_mesh.h"
#include "entity_lod.h"
#include <cstdio>
#include <cmath>
int main(){
  int failures=0,checks=0;
  auto check=[&](bool ok){++checks;if(!ok)++failures;};
  check(ENT_LODS==4 && kEntCloseLod==3);
  for(int k=EK_HOUSE;k<=EK_GASSTATION;++k)for(int i=0;i<=200;++i){
    float requested=.1f+i*.02f, scale=entBuildingVerticalScale(k,requested);
    check(std::isfinite(scale));
    if(k==EK_APARTMENT)check(scale==1.f);
    else if(k==EK_SILO||k==EK_WATERTOWER)check(scale>=.65f&&scale<=1.7f);
    else check(scale>=.92f&&scale<=1.08f);
  }

  for(int q=0;q<3;++q)for(int k=0;k<EK_COUNT;++k){
    auto r=entRangesFor(q);float l0,l1;entLodLimits(r,k,l0,l1);
    float h=entCloseLimit(r,k);check(h>0 && h<l0*.85f);
    for(int feed=0;feed<2;++feed){
      float c=feed?0.f:h;
      for(float d=.1f;d<entRangeOf(r,k);d=d*1.009f+.07f){
        int at=entDetailAt(d,c,l0,l1),also=entDetailAlso(k,d,c,l0,l1);float sum=0;int occupied=0;
        for(int lod=0;lod<ENT_LODS;++lod){float lo,hi;entDetailKeep(k,lod,d,c,l0,l1,lo,hi);
          check(lo>=0 && hi<=1 && hi>=lo);float share=hi-lo;sum+=share;
          if(share>0){++occupied;check(lod==at||lod==also);}
        }
        check(std::abs(sum-1.f)<1e-5f && occupied<=2);
        if(feed)check(at!=3 && also!=3);
        if(d>=h)check(at==entLodAt(d,l0,l1));
      }
      for(float a=0;a<entRangeOf(r,k);a+=37.3f){float b=a+300;
        if(entDetailAt(a,c,l0,l1)!=entDetailAt(b,c,l0,l1)||entDetailSpanFades(k,a,b,c,l0,l1))continue;
        for(float d=a;d<=b;d+=13.9f){float lo,hi;entDetailKeep(k,entDetailAt(a,c,l0,l1),d,c,l0,l1,lo,hi);check(lo==0 && hi==1);}
      }
    }
  }
  std::printf("Close-detail policy: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
