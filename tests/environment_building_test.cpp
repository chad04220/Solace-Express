// CPU-only roof, contact, opening and bounded-work contracts for shop/apartment meshes.
#include "entity_mesh.h"
#include <cstdio>
#include <cmath>

namespace {
bool projectedInside(const EVert& a,const EVert& b,const EVert& c,float x,float y) {
  auto edge=[&](const EVert& p,const EVert& q){return (q.px-p.px)*(y-p.py)-(q.py-p.py)*(x-p.px);};
  float area=(b.px-a.px)*(c.py-a.py)-(b.py-a.py)*(c.px-a.px);
  if(std::abs(area)<1e-8f)return false;
  float p=edge(a,b),q=edge(b,c),r=edge(c,a);
  return (p>=-1e-6f&&q>=-1e-6f&&r>=-1e-6f)||(p<=1e-6f&&q<=1e-6f&&r<=1e-6f);
}
}
int main() {
  static_assert(ENT_LODS==4 && sizeof(EVert)==40 && sizeof(Ent)==32,"preserve four-slot mesh and renderer ABI");
  std::vector<EVert> v;EntMeshRange r[EK_COUNT];buildEntityMeshes(v,r);
  int failed=0,checks=0;
  auto check=[&](bool ok,const char* label,int k,int l){
    ++checks;if(!ok){++failed;std::printf("FAIL: %s kind=%d lod=%d\n",label,k,l);}
  };
  const int kinds[]={EK_SHOP,EK_APARTMENT};
  // The three distance tiers remain exact. Hero ceilings are separately measured,
  // rather than allowing additional architectural detail to inflate every tier.
  const int vertexBudget[2][ENT_LODS]={{192,96,90,1875},{1134,114,84,20340}};
  for(int t=0;t<2;++t)for(int l=0;l<ENT_LODS;++l) {
    int k=kinds[t],count=r[k].count[l];
    check(count>0&&count%3==0&&count<=vertexBudget[t][l],"explicit per-tier vertex ceiling",k,l);
    if(l<3)check(count==vertexBudget[t][l],"exact established distance-tier budget",k,l);
    else check(count>r[k].count[0]*3,"hero has separately authored architectural detail",k,l);
    float roofY=t?20.8f:4.6f,area=0,minY=1e9f;
    for(int i=r[k].first[l];i<r[k].first[l]+count;i+=3) {
      const auto&a=v[i];const auto&b=v[i+1];const auto&c=v[i+2];
      vec3 pa(a.px,a.py,a.pz),pb(b.px,b.py,b.pz),pc(c.px,c.py,c.pz);
      float A=length(cross(pb-pa,pc-pa))*.5f;
      check(std::isfinite(A)&&A>1e-7f,"finite positive triangle area",k,l);
      for(int q=0;q<3;++q) {
        const auto&w=v[i+q];
        for(float f:{w.px,w.py,w.pz,w.nx,w.ny,w.nz,w.part,w.ao,w.u,w.v})check(std::isfinite(f),"finite vertex attribute",k,l);
        float n=w.nx*w.nx+w.ny*w.ny+w.nz*w.nz;
        check(std::isfinite(n)&&fabsf(n-1)<1e-4f,"unit finite normal",k,l);
        check(w.py>=-3 && w.py<=(t?23.2f:5.6f)+1e-5f,"original vertical bound",k,l);
        check(fabsf(w.px)<=(t?9.12f:7.12f)+1e-5f && w.pz>=-7.12f-1e-5f && w.pz<=(t?8.3f:8.7f)+1e-5f,"original footprint bound",k,l);
        check(w.ao>=0.f&&w.ao<=1.f,"bounded ambient occlusion",k,l);
        minY=std::min(minY,w.py);
      }
      bool atRoof=fabsf(a.py-roofY)<1e-5f&&fabsf(b.py-roofY)<1e-5f&&fabsf(c.py-roofY)<1e-5f;
      if(atRoof) {
        check(a.part!=P_WALL,"no coplanar wall roof cap",k,l);
        if(a.part==P_DARK){check(a.ny>.99f,"roof faces up",k,l);area+=A;}
      }
    }
    check(minY==-3.f,"foundation retains buried contact anchor",k,l);
    check(fabsf(area-(t?252.f:196.f))<1e-4f,"single dark roof deck covers entire footprint",k,l);
    if(l==3) {
      // Probe off the sash/mullion centre. The actual front wall must contain an
      // aperture, with glazing behind it, rather than a coloured rectangle on a box.
      const float x=t?-6.25f:-4.9f,y=t?2.05f:1.6f;
      bool frontWall=false,recessedGlass=false,reveal=false;
      for(int i=r[k].first[l];i<r[k].first[l]+count;i+=3) {
        const auto&a=v[i];const auto&b=v[i+1];const auto&c=v[i+2];
        if(projectedInside(a,b,c,x,y)) {
          if(a.part==P_WALL&&fabsf(a.pz-7.f)<.01f&&fabsf(b.pz-7.f)<.01f&&fabsf(c.pz-7.f)<.01f)frontWall=true;
          if(a.part==P_GLASS&&a.nz>.99f&&a.pz<6.95f&&a.pz>6.75f)recessedGlass=true;
        }
        if(a.part==P_TRIM&&std::max({a.pz,b.pz,c.pz})>=6.999f&&std::min({a.pz,b.pz,c.pz})<6.9f)reveal=true;
      }
      check(!frontWall&&recessedGlass,"hero window is an unblocked recessed opening",k,l);
      check(reveal,"hero opening has physical reveal depth",k,l);
    }
  }
  std::printf("building_mesh_test: %d checks, %d failures\n",checks,failed);
  return failed?1:0;
}
