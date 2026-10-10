// CPU reference for close-instance frustum bounds versus actual ent_vs.glsl
// deformations. Samples both close-fade participants and the all-LOD chunk envelope.
#include "entity_bounds.h"
#include <cstdio>
#include <cmath>

namespace {
float fract(float x){return x-floorf(x);}
vec3 posed(const EVert&v,int kind,const Ent&e,float time,vec3 wind) {
  vec3 p(v.px,v.py,v.pz);int wheel=int(v.part+.5f)-P_WHEEL0;
  if(wheel>=0&&wheel<6){float a=time*(.7f+wheel*.31f),c=cosf(a),s=sinf(a),y=p.y-v.u,z=p.z-v.v;p.y=v.u+c*y-s*z;p.z=v.v+s*y+c*z;}
  if(kind<=EK_BUSH){float t=clampf(p.y/(kind==EK_BUSH?1.8f:kind==EK_PALM?10.f:12.f),0,1.6f),tw=(fract(e.seed*3.71f)-.5f)*2.4f*t,c=cosf(tw),s=sinf(tw),x=p.x,z=p.z;p.x=c*x-s*z;p.z=s*x+c*z;
    float ph=atan2f(p.z,p.x),r=1+std::min(t,1.f)*(.14f*sinf(2*ph+e.seed*41)+.07f*sinf(3*ph+e.seed*23));p.x*=r;p.z*=r;
    if(kind!=EK_BUSH){float lean=.45f*fract(e.seed*8.3f)*t*t;p.x+=cosf(e.seed*57)*lean;p.z+=sinf(e.seed*57)*lean;}
  }
  p=mul(p,{e.sx,e.sy,e.sz});
  if(kind<=EK_BUSH&&(v.part<3.5f||v.part>17.5f)){
    float h=std::max(v.py,0.f)/14,ph=time*(1.1f+.4f*fract(e.seed*7))+e.x*.05f+e.z*.04f;
    p.x+=sinf(ph)*.06f*h*h*e.sy;p.z+=cosf(ph*.83f)*.06f*h*h*e.sy;
    if(v.part>1.5f&&v.part<2.5f)p.z+=sinf(ph*2.3f+v.px)*.08f*v.v;
  }
  float c=cosf(e.yaw),s=sinf(e.yaw);vec3 result(e.x+c*p.x+s*p.z,e.y+p.y,e.z-s*p.x+c*p.z);
  if(kind==EK_WINDSOCK&&fabsf(v.part-P_SOCK)<.5f){
    vec2 w(wind.x,wind.z);float speed=length(w),k=clampf(speed/7.7f,0,1);vec2 wd=speed>.1f?w*(1.f/speed):vec2(1,0);
    float ph=time*(2+2.5f*k)+e.x*.13f;wd=wd+vec2(-wd.y,wd.x)*(sinf(ph)*.1f*(.25f+k));wd=wd*(1.f/length(wd));
    vec3 d=normalize(vec3(wd.x*std::max(k,.08f),-(1-k)*1.3f-.06f+.04f*sinf(ph*1.7f)*k,wd.y*std::max(k,.08f)));
    vec3 s1=normalize(cross(d,{0,1,0})+vec3(1e-4f,0,0)),s2=cross(s1,d);
    result=vec3(e.x,e.y+6*e.sy,e.z)+d*v.px+s2*(v.py-6)+s1*v.pz;
  }return result;
}
bool contains(EntWorldBounds b,vec3 p){return p.x>=b.lo.x&&p.x<=b.hi.x&&p.y>=b.lo.y&&p.y<=b.hi.y&&p.z>=b.lo.z&&p.z<=b.hi.z;}
}
int main(){
  std::vector<EVert> vertices;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(vertices,ranges);EntLocalBounds local[EK_COUNT];
  long checks=0,failures=0,visibleSamples=0;auto check=[&](bool yes,const char*why,int kind){++checks;if(!yes){if(failures<20)printf("FAIL %s kind%d\n",why,kind);++failures;}};
  const vec3 scales[]={{1,1,1},{.35f,.7f,.45f},{2.8f,1.25f,3.2f},{.8f,3.7f,.55f},{-1.3f,1.7f,2.1f},{2.1f,-1.2f,-.8f}};
  for(bool allLods:{false,true}) {
    entBuildLocalBounds(vertices,ranges,local,allLods);
  for(int kind=0;kind<EK_COUNT;++kind){check(local[kind].valid,"authored bounds exist",kind);
    for(float seed:{0.f,.0001f,.125f,.3333f,.501f,.731f,.917f,.9999f})for(float yaw:{0.f,.37f,1.5707963f,3.92f})for(vec3 scale:scales)for(float time:{0.f,1.93f,19.17f}){
      Ent e{-39731.25f,273.4f,38217.125f,yaw,scale.x,scale.y,scale.z,seed};auto b=entInstanceBounds(local[kind],kind,e);
      for(float f:{b.lo.x,b.lo.y,b.lo.z,b.hi.x,b.hi.y,b.hi.z})check(std::isfinite(f),"finite world bounds",kind);
      check(b.lo.x<b.hi.x&&b.lo.y<b.hi.y&&b.lo.z<b.hi.z,"nonempty world bounds",kind);
      // A deliberately grazing view: only part of the object can be in frame.
      vec3 centre=(b.lo+b.hi)*.5f,half=(b.hi-b.lo)*.5f;float radius=std::max({half.x,half.y,half.z,1.f});
      mat4 vp=perspective(50*DEG,1920.f/1080.f,.5f,90000.f)*lookAt(centre+vec3(radius*1.7f,radius*.2f,radius*2.3f),centre+vec3(radius*.7f,0,0),{0,1,0});float planes[6][4];
      for(int i=0;i<3;++i)for(int j=0;j<2;++j)for(int c=0;c<4;++c)planes[i*2+j][c]=vp(3,c)+(j?-1.f:1.f)*vp(i,c);
      bool keep=entBoundsVisible(b,planes);
      for(int lod=0;lod<ENT_LODS;++lod)if(allLods||lod==0||lod==3)for(int i=ranges[kind].first[lod];i<ranges[kind].first[lod]+ranges[kind].count[lod];i+=17){
        vec3 p=posed(vertices[i],kind,e,time,{seed*17-7,0,yaw*3-4});check(contains(b,p),"bounds contain animated authored vertex",kind);
        bool visible=true;for(auto&P:planes)if(P[0]*p.x+P[1]*p.y+P[2]*p.z+P[3]<0){visible=false;break;}
        if(visible){++visibleSamples;check(keep,"no frustum false negative for visible vertex",kind);}
      }
    }
  }
  } // close-pair and all-LOD envelopes
  // Strong rejection remains possible despite the deformation margin.
  float planes[6][4]={{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}};
  check(entBoundsVisible({{-.5f,-.5f,-.5f},{.5f,.5f,.5f}},planes),"inside retained",-1);
  check(!entBoundsVisible({{20,20,20},{21,21,21}},planes),"offscreen rejected",-1);
  printf("environment_close_bounds_test: %ld checks, %ld visible sampled vertices, %ld failures across every kind, 8 seeds, 4 yaws, 6 nonuniform/signed scales, 3 animation times, close-pair and all-LOD envelopes.\n",checks,visibleSamples,failures);
  return failures?1:0;
}
