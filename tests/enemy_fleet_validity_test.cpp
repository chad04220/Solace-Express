#ifdef NDEBUG
#undef NDEBUG
#endif
#include "enemy_fleet.h"
#include "mesh_validation.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 EnemyCraftVisual c;assert(enemyCraftValid(c));
 for(float bad:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  for(int j=0;j<3;j++){auto x=c;if(j==0)x.pos.x=bad;else if(j==1)x.pos.y=bad;else x.pos.z=bad;assert(!enemyCraftValid(x));assert(packEnemyFleet(&x,1).count==0);}
  for(int j=0;j<9;j++){auto x=c;x.rot[j]=bad;assert(!enemyCraftValid(x));}
  for(int j=0;j<4;j++){auto x=c;x.state[j]=bad;assert(!enemyCraftValid(x));}
  auto pose=sampleEnemyVtol(EnemyCraftType::Fighter,vec3(bad),bad,double(bad),bad);
  assert(enemyCraftValid(pose.visual));
 }
 assert(!aircraftMesh::finite(std::numeric_limits<double>::infinity()));
 assert(!aircraftMesh::finite(std::numeric_limits<double>::quiet_NaN()));
 std::puts("PASS enemy transforms reject nonfinite inputs and VTOL sanitizes them under fast-math");
}
