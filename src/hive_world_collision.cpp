#include "hive_world_collision.h"
#include "finite_float.h"
#include "entities.h"
#include "world.h"
#include <limits>
namespace {
float surface(vec3 p) { return std::max(0.f,pitGround(wrapCoord(p.x),wrapCoord(p.z),g_world.height(p.x,p.z))); }
// The hmax grid includes road fills. Add a conservative bound for dynamic crater rims.
float upperBound(vec3 a,vec3 b,float radius) {
  if(g_world.hmax[0].size()!=size_t(HMAX_N)*HMAX_N) return std::numeric_limits<float>::infinity();
  float shiftX=wrapCoord((a.x+b.x)*.5f)-(a.x+b.x)*.5f;
  float shiftZ=wrapCoord((a.z+b.z)*.5f)-(a.z+b.z)*.5f;
  a.x+=shiftX;b.x+=shiftX;a.z+=shiftZ;b.z+=shiftZ;
  float loX=std::min(a.x,b.x)-radius,hiX=std::max(a.x,b.x)+radius;
  float loZ=std::min(a.z,b.z)-radius,hiZ=std::max(a.z,b.z)+radius;
  const float cell=2*WORLD_HALF/HMAX_N;
  // Outside the authored square, sampleBase clamps to edge texels then fades toward
  // -SEA_DEPTH. Base + 2*amplitude bounds all eight signed FBM octaves, even outside
  // the hmax grid; the largest corner base/amplitude bounds the bilinear footprint.
  if(loX< -WORLD_HALF || hiX>=WORLD_HALF || loZ< -WORLD_HALF || hiZ>=WORLD_HALF) {
    if(g_world.hm.size()!=size_t(HM_N)*HM_N*4) return std::numeric_limits<float>::infinity();
    auto cellIndex=[](float v){return std::clamp(int(floorf((v+WORLD_HALF)/HM_TEXEL-.5f)),0,HM_N-1);};
    int ix0=cellIndex(loX),ix1=std::min(HM_N-1,cellIndex(hiX)+1);
    int iz0=cellIndex(loZ),iz1=std::min(HM_N-1,cellIndex(hiZ)+1);
    float base=-SEA_DEPTH,amp=0;
    for(int z=iz0;z<=iz1;++z)for(int x=ix0;x<=ix1;++x){const float* h=&g_world.hm[(size_t(z)*HM_N+x)*4];base=std::max(base,h[0]);amp=std::max(amp,std::abs(h[1]));}
    float top=std::max(0.f,base+2*amp)+4;
    for(const auto& p:g_groundPits)top+=std::abs(p.D);
    return top;
  }
  int x0=int((loX+WORLD_HALF)/cell),x1=int((hiX+WORLD_HALF)/cell);
  int z0=int((loZ+WORLD_HALF)/cell),z1=int((hiZ+WORLD_HALF)/cell);
  float top=0;
  for(int z=z0;z<=z1;++z) for(int x=x0;x<=x1;++x) top=std::max(top,g_world.hmax[0][size_t(z)*HMAX_N+x]);
  for(const auto& p:g_groundPits) top+=std::abs(p.D);
  return top;
}
bool groundContact(vec3 p,float r) {
  if(p.y-r<=surface(p)) return true;
  if(r<=.01f) return false;
  // Underside footprint probes catch a slope touching a ship before its centre passes the ridge.
  const float ring=r*.70710678f, drop=r*.70710678f;
  for(int i=0;i<8;++i) {
    float a=i*(PI*.25f);
    if(p.y-drop<=surface(p+vec3(cosf(a)*ring,0,sinf(a)*ring))) return true;
  }
  return false;
}
}
float hiveWorldSweep(vec3 a,vec3 b,float radius) {
  for(int k=0;k<3;++k) if(!floatValidation::finite(a[k])||!floatValidation::finite(b[k])) return 0;
  if(!floatValidation::finite(radius)||radius<0||radius>1000) return 0;
  vec3 delta=b-a;float distance=length(delta);
  if(distance>WRAP_SPAN || !floatValidation::finite(distance)) return 0;
  if(distance<1e-5f) {
    vec3 p(wrapCoord(a.x),a.y,wrapCoord(a.z));
    return groundContact(p,radius)||g_scenery.collide(p,radius)?0.f:2.f;
  }
  vec3 dir=delta/distance;
  // Short canonical pieces also keep entity chunk enumeration bounded at wrap seams.
  for(float start=0;start<distance;) {
    float end=std::min(start+64.f,distance),len=end-start;
    vec3 p=a+dir*start,q=a+dir*end;
    vec3 mid=(p+q)*.5f,shift(wrapCoord(mid.x)-mid.x,0,wrapCoord(mid.z)-mid.z);
    float scenery=g_scenery.sweepSphere(p+shift,dir,len,radius);
    float stop=scenery>=0?start+scenery:end;
    if(std::min(p.y,(a+dir*stop).y)-radius<=upperBound(p,a+dir*stop,radius)) {
      float previous=start;
      if(groundContact(p,radius)) return start/distance;
      for(float t=std::min(start+1.f,stop);t<=stop && stop>start;) {
        if(groundContact(a+dir*t,radius)) {
          float lo=previous,hi=t;
          for(int j=0;j<12;++j) {float m=(lo+hi)*.5f;if(groundContact(a+dir*m,radius))hi=m;else lo=m;}
          return hi/distance;
        }
        if(t==stop)break;
        previous=t;t=std::min(t+1.f,stop);
      }
    }
    if(scenery>=0)return stop/distance;
    start=end;
  }
  return 2.f;
}
