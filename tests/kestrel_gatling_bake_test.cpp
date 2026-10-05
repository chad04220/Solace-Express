// Guard the production hull/mesh animation envelope against the 128-state limit.
#include "../src/aircraft.h"
#include "../src/models.h"
#include "../src/hull_mesh.h"
#include <cstdio>
#include <cmath>

int main(){
 Plane plane;plane.spec=&kAircraft[kPeregrine];float packed[96];
 packModel(*plane.spec,kPeregrine,plane.gearHeight(),packed);
 int failed=0,checks=0;
 auto check=[&](bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL %s\n",label);}};
 auto near=[](float a,float b){return std::abs(a-b)<.000001f;};
 for(bool inside:{false,true}){
  auto states=hullStateList(packed,inside);
  check(states.size()==125,"native Kestrel hull includes all 97 original + 28 Gatling stations");
  check(states.size()<128,"Gatling hull stations leave room below the 128 uniform-array ceiling");
  auto has=[&](float door,float carrier,float phase){
   for(const auto& s:states)if(near(s.wr[0],door)&&near(s.wr[1],carrier)&&near(s.wr[2],phase))return true;
   return false;
  };
  for(int i=1;i<=6;++i)check(has(i/6.f,0,0),"every bay door station is retained");
  for(int i=1;i<=5;++i)for(int r=0;r<2;++r)check(has(1,i/5.f,r*PI/6.f),"every slide station includes both rotor envelope orientations");
  for(int i=0;i<8;++i)check(has(1,1,i*PI/24.f),"all eight deployed rotor phases are retained without truncation");
  for(int i=0;i<4;++i)check(has(0,0,i*PI/12.f),"all four stopped and stowed rotor phases are retained");
  for(const auto& s:states){
   check(s.ps[3]==(inside?1.f:0.f),"all bake states preserve the requested interior/exterior slot");
   check(s.wr[0]>=0&&s.wr[0]<=1&&s.wr[1]>=0&&s.wr[1]<=1,"all bay and carrier states stay normalized");
   check(s.wr[1]==0||s.wr[0]==1,"baked carrier never moves through a partially closed bay");
  }
  std::printf("FX-27 %s hull envelope: %zu/128 states, full door/carrier/rotor/stowed phase coverage.\n",inside?"interior":"exterior",states.size());
 }
 std::printf("FX-27 hull-bake sequence: %d checks, %d failed.\n",checks,failed);return failed?1:0;
}
