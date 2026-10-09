#include "mesh_validation.h"
#include "aircraft_mesh_overhead.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using namespace aircraftMesh;
int main() {
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float inf=std::numeric_limits<float>::infinity();
  assert(!finite(nan) && !finite(inf) && !finite(-inf) && finite(0.f));
  const float zeroNormal[3]={0,0,0}, badNormal[3]={inf,0,0}, hugeNormal[3]={1e30f,1e30f,0};
  assert(!normalValid(zeroNormal) && !normalValid(badNormal) && !normalValid(hugeNormal));
  std::vector<float> v={0,0,0,0,0,1,3,1, 1,0,0,0,0,1,3,1, 0,1,0,nan,nan,nan,3,1, 9,9,9,0,0,1,3,1};
  std::vector<uint32_t> ix={0,1,2};
  assert(!valid(v,ix)); auto before=v;
  assert(repairNormals(v,ix)); assert(valid(v,ix));
  assert(v[19]==0 && v[20]==0 && v[21]==1);
  for(size_t i=0;i<v.size();i++) if(i<19 || i>21) assert(v[i]==before[i]);
  assert(compact(v,ix)); assert(v.size()==24); assert(valid(v,ix));
  auto bad=ix; bad[2]=3; assert(!valid(v,bad));
  v[0]=nan; assert(!valid(v,ix)); v[0]=0;
  v[7]=nan; assert(!valid(v,ix)); v[7]=1;
  v[3]=v[4]=v[5]=0; assert(!valid(v,ix)); assert(repairNormals(v,ix)); assert(valid(v,ix));
  std::vector<float> isolated={0,0,0,0,0,0,1,1}; std::vector<uint32_t> empty;
  assert(!repairNormals(isolated,empty)); // no invented global normal
  std::vector<uint32_t> blob={38,(uint32_t)v.size(),(uint32_t)ix.size()};
  size_t at=blob.size();blob.resize(at+v.size());std::memcpy(blob.data()+at,v.data(),v.size()*4);blob.insert(blob.end(),ix.begin(),ix.end());
  assert(validBlob(blob));
  auto badBlob=blob;badBlob[0]=kMaxPartType+1;assert(!validBlob(badBlob));
  badBlob=blob;badBlob[1]=std::numeric_limits<uint32_t>::max();assert(!validBlob(badBlob));
  badBlob=blob;badBlob.push_back(0);assert(!validBlob(badBlob));
  badBlob=blob;std::memcpy(badBlob.data()+3,&inf,sizeof inf);assert(!validBlob(badBlob));
  blob.back()=99;assert(!validBlob(blob));blob.pop_back();assert(!validBlob(blob));
  std::vector<float> collapsed={0,0,0,0,0,0,85,1, 0,0,0,0,0,0,85,1, 0,0,0,0,0,0,85,1};
  std::vector<uint32_t> collapsedIx={0,1,2};
  assert(pruneDegenerate(collapsed,collapsedIx)); assert(collapsedIx.empty());
  assert(compact(collapsed,collapsedIx)); assert(collapsed.empty()); assert(repairNormals(collapsed,collapsedIx));
  // Bounded refinement retains the original surface and coarse/fine partition.
  std::vector<float> roof={-.24f,.1f,-.55f,0,-1,0,14,1, .24f,.1f,-.55f,0,-1,0,14,1,
                           .24f,.1f,.55f,0,-1,0,14,1, -.24f,.1f,.55f,0,-1,0,14,1};
  auto roofBefore=roof;std::vector<uint32_t> roofIx={0,1,2,0,2,3};uint32_t split=3;float eye[3]={0,0,0};
  size_t added=boundOverheadEdges(roof,roofIx,split,eye);
  assert(added>0 && split>3 && split%3==0 && split<roofIx.size());
  assert(std::equal(roofBefore.begin(),roofBefore.end(),roof.begin()));assert(valid(roof,roofIx));
  double area=0;
  for(size_t i=0;i<roofIx.size();i+=3){
    for(int k=0;k<3;k++){
      const float*a=&roof[roofIx[i+k]*8],*b=&roof[roofIx[i+(k+1)%3]*8];
      float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];assert(dx*dx+dy*dy+dz*dz<=.08f*.08f+1e-8f);
      assert(a[1]==.1f && a[6]==14.f && a[7]==1.f);
    }
    const float*a=&roof[roofIx[i]*8],*b=&roof[roofIx[i+1]*8],*c=&roof[roofIx[i+2]*8];
    area+=std::abs((double)(b[0]-a[0])*(c[2]-a[2])-(double)(b[2]-a[2])*(c[0]-a[0]))*.5;
  }
  assert(std::abs(area-.48*1.1)<1e-6);
  auto excluded=roofBefore;for(size_t i=6;i<excluded.size();i+=8)excluded[i]=11.f;
  const auto excludedBefore=excluded;std::vector<uint32_t> excludedIx={0,1,2,0,2,3};split=3;
  assert(boundOverheadEdges(excluded,excludedIx,split,eye)==0 && excluded==excludedBefore && split==3);
  // Both extreme partitions keep their meaning after local refinement.
  for (uint32_t coarse : {0u,6u}) {
    auto partitionRoof=roofBefore;std::vector<uint32_t> partitionIx={0,1,2,0,2,3};split=coarse;
    assert(boundOverheadEdges(partitionRoof,partitionIx,split,eye)>0);
    assert(split==(coarse==0 ? 0u : uint32_t(partitionIx.size())));
    assert(valid(partitionRoof,partitionIx));
  }
  // A material-14 floor remains outside the near-eye overhead selection.
  excluded=roofBefore;for(size_t i=1;i<excluded.size();i+=8)excluded[i]=-.1f;
  auto floorBefore=excluded;excludedIx={0,1,2,0,2,3};split=3;
  assert(boundOverheadEdges(excluded,excludedIx,split,eye)==0 && excluded==floorBefore && split==3);
  puts("mesh_validation_test: finite attributes, oriented recovery, no-position-change, compaction, blob bounds, overhead edge bound and coarse/fine partitions passed");
}
