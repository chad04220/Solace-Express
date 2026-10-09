#include "entity_mesh.h"
#include <cstdio>
#include <cmath>
int main(){
 std::vector<EVert> v;EntMeshRange r[EK_COUNT];buildEntityMeshes(v,r);int failed=0,checks=0;
 auto check=[&](bool ok,const char*label){checks++;if(!ok){failed++;printf("FAIL: %s\n",label);}};
 const int kinds[]={EK_SHOP,EK_APARTMENT};const int tri[2][3]={{64,32,30},{378,38,28}};
 for(int t=0;t<2;t++)for(int l=0;l<3;l++){
  int k=kinds[t];check(r[k].count[l]/3==tri[t][l],"exact reduced triangle budget");float roofY=t?20.8f:4.6f,area=0;
  for(int i=r[k].first[l];i<r[k].first[l]+r[k].count[l];i+=3){
   auto&a=v[i];auto&b=v[i+1];auto&c=v[i+2];vec3 pa(a.px,a.py,a.pz),pb(b.px,b.py,b.pz),pc(c.px,c.py,c.pz);float A=length(cross(pb-pa,pc-pa))*.5f;
   check(std::isfinite(A)&&A>1e-7f,"finite positive triangle area");
   for(int q=0;q<3;q++){auto&w=v[i+q];float n=w.nx*w.nx+w.ny*w.ny+w.nz*w.nz;check(std::isfinite(n)&&fabsf(n-1)<1e-4f,"unit finite normal");check(w.py>=-3 && w.py<=(t?23.2f:5.6f)+1e-5f,"original vertical bound");check(fabsf(w.px)<=(t?9.12f:7.12f)+1e-5f && w.pz>=(t?-7.12f:-7.12f)-1e-5f && w.pz<=(t?8.3f:8.7f)+1e-5f,"original footprint bound");}
   bool atRoof=fabsf(a.py-roofY)<1e-5f&&fabsf(b.py-roofY)<1e-5f&&fabsf(c.py-roofY)<1e-5f;
   if(atRoof){check(a.part!=P_WALL,"no coplanar wall roof cap");if(a.part==P_DARK){check(a.ny>.99f,"roof faces up");area+=A;}}
  }
  check(fabsf(area-(t?252.f:196.f))<1e-4f,"single dark roof deck covers entire footprint");
 }
 printf("building_mesh_test: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
