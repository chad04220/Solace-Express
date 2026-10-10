#ifdef NDEBUG
#undef NDEBUG
#endif
#include "aircraft_mesh_planar.h"
#include <cassert>
#include <cstdio>
int main(){
  // Actual Islander PT12 triangle814: a rounded trailing-edge normal survived the
  // 12-degree crease threshold and smeared over this 640 mm planar face.
  std::vector<float> v={5.625076f,1.0393723f,-.35270765f,.12510794f,-.96303415f,.23856507f,2,1,
    5.1505876f,1.0043087f,-.60956967f,.01735514f,-.99445903f,.10368333f,2,1,
    5.7867846f,1.0222983f,-.54353684f,.01735514f,-.99445885f,.10368405f,2,1};
  const auto before=v;std::vector<uint32_t> ix={0,1,2};const auto originalIx=ix;
  std::vector<float> g;for(int k=0;k<3;k++)g.insert(g.end(),{.01735514f,-.99445903f,.10368333f,0});
  aircraftMesh::PlanarRepairStats s;assert(aircraftMesh::repairVerifiedPlanarNormals(v,ix,g,s));
  assert(s.repaired==1 && s.addedVertices==3 && ix.size()==3 && v.size()==48);
  for(int k=0;k<3;k++)for(int j:{0,1,2,6,7})assert(v[ix[k]*8+j]==before[k*8+j]);
  for(int k=1;k<3;k++)for(int j=3;j<6;j++)assert(v[ix[k]*8+j]==v[ix[0]*8+j]);
  assert(aircraftMesh::valid(v,ix));
  const auto repaired=v;const auto repairedIx=ix;
  assert(aircraftMesh::repairVerifiedPlanarNormals(v,ix,g,s));assert(s.repaired==0 && v==repaired && ix==repairedIx);
  // Different sampled gradients mean a true bend. Neither that nor a material seam
  // may be flattened, and invalid sample records leave the original triangle alone.
  for(int mode=0;mode<3;mode++){
    v=before;ix=originalIx;auto q=g;
    if(mode==0){q[4]=.20f;q[5]=-.974f;q[6]=.103f;}
    if(mode==1)v[14]=5.f;
    if(mode==2)q[0]=q[1]=q[2]=0;
    const auto old=v;assert(aircraftMesh::repairVerifiedPlanarNormals(v,ix,q,s));assert(s.repaired==0 && v==old && ix==originalIx);
  }
  v=before;ix={0,1,999};const auto bad=ix;assert(!aircraftMesh::repairVerifiedPlanarNormals(v,ix,g,s));assert(v==before && ix==bad);
  std::puts("PASS: planar rigid-face repair preserves geometry/attributes, rejects bends/seams and fixes the actual Islander witness");
}
