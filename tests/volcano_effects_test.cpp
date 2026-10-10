#include "volcano_effects.h"
#include <cstdio>
#include <cstring>
int main(){
  g_world.build(); int failures=0, checks=0;
  auto check=[&](bool b,const char*label){++checks;if(!b){++failures;printf("FAIL %s\n",label);}};
  const vec3 base=volcano::vent(g_world);
  check(fabsf((base.y-14.f)-1780.f)<2.f,"vent anchored to excavated crater floor");
  for(int i=0;i<16;++i){
    float angle=i*6.2831853f/16.f;
    float rim=g_world.height(volcano::x+340.f*cosf(angle),volcano::z+340.f*sinf(angle));
    check(rim>base.y+140.f,"crater wall rises above molten floor");
  }
  for(int q=0;q<3;++q)for(float t:{0.f,.1f,12.f,44.f,10000.f})for(float d:{100.f,3500.f,15000.f,25000.f}){
    FrameParams fp;fp.camPos=base+vec3(d,100,0);fp.wind=vec3(8,0,-3);fp.time=t;
    std::vector<SpriteVert>a,b;volcano::append(fp,q,a,b);
    check(a.size()<=volcano::maxSmoke*6 && b.size()<=volcano::maxEmbers*6,"hard sprite budget");
    check(fp.plN<=1,"bounded vent light");
    if(fp.plN && d<1500.f){
      const auto& l=fp.pl[0];
      float irradiance=l.col.x/(100.f*100.f+l.radius*l.radius);
      check(irradiance>.05f && irradiance<3.f,"vent light survives renderer threshold without overexposure");
    }
    if(d>=24000)check(a.empty()&&b.empty()&&fp.plN==0,"distant volcano culled");
    for(const auto&v:a){check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.a>=0&&v.a<=1&&v.bill>0,"finite smoke");}
    for(const auto&v:b){check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&v.a>=0&&v.a<=1,"finite embers");check(v.y>=g_world.height(v.x,v.z),"embers above terrain");}
    FrameParams other;other.camPos=fp.camPos;other.wind=fp.wind;other.time=fp.time;
    std::vector<SpriteVert>aa,bb;volcano::append(other,q,aa,bb);
    check(a.size()==aa.size()&&b.size()==bb.size(),"deterministic counts");
    check(a.empty()||!memcmp(a.data(),aa.data(),a.size()*sizeof(SpriteVert)),"deterministic smoke");
  }
  FrameParams fp;fp.camPos=base;fp.hangarPreview=true;std::vector<SpriteVert>a,b;volcano::append(fp,2,a,b);check(a.empty()&&b.empty(),"no indoor effects");
  fp.hangarPreview=false;fp.plN=12;volcano::append(fp,2,a,b);check(fp.plN==12,"existing light capacity respected");
  printf("Volcano: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
