#include "aircraft_mesh_orientation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace aircraftMesh;
int main(int argc,char**argv){
  if(argc==2){
    std::ifstream in(argv[1],std::ios::binary);uint32_t h[6];in.read((char*)h,sizeof h);
    std::vector<float> v(h[1]);std::vector<uint32_t> ix(h[2]);in.read((char*)v.data(),v.size()*4);in.read((char*)ix.data(),ix.size()*4);assert(in.good());
    auto before=v;auto old=ix;WindingRepairStats s;assert(repairCoherentWinding(v,ix,s));assert(v==before&&ix.size()==old.size());
    printf("{\"triangles\":%zu,\"candidates\":%zu,\"paired_skipped\":%zu,\"flipped\":%zu}\n",s.triangles,s.candidates,s.pairedSkipped,s.flipped);return 0;
  }
  const std::vector<float> original={0,0,0,0,0,1,11,1, 1,0,0,0,0,1,11,1, 0,1,0,0,0,1,11,1};
  auto v=original;std::vector<uint32_t> ix={0,2,1};WindingRepairStats s;
  assert(repairCoherentWinding(v,ix,s)&&s.flipped==1&&ix==std::vector<uint32_t>({0,1,2})&&v==original);
  auto good=ix;assert(repairCoherentWinding(v,ix,s)&&s.flipped==0&&ix==good);
  for(size_t i=5;i<v.size();i+=8) { v[i]=-1; }
  ix={0,2,1};good=ix;
  assert(repairCoherentWinding(v,ix,s)&&s.flipped==0&&ix==good); // valid back surface
  v=original;ix={0,2,1,0,1,2};good=ix;
  assert(repairCoherentWinding(v,ix,s)&&s.candidates==1&&s.pairedSkipped==1&&s.flipped==0&&ix==good);
  v.insert(v.end(),original.begin(),original.end());ix={0,2,1,3,4,5};good=ix;
  assert(repairCoherentWinding(v,ix,s)&&s.pairedSkipped==1&&s.flipped==0&&ix==good); // duplicate-record two-sided pair
  v=original;v[6]=145;ix={0,2,1};good=ix;
  assert(repairCoherentWinding(v,ix,s)&&s.flipped==0&&ix==good); // mixed-material boundary
  v=original;v[3]=.6f;v[5]=.8f;
  assert(repairCoherentWinding(v,ix,s)&&s.flipped==0&&ix==good); // disagreeing normals
  v=original;v[3]=std::numeric_limits<float>::quiet_NaN();
  assert(!repairCoherentWinding(v,ix,s)&&ix==good);
  v=original;ix={0,2,4};good=ix;assert(!repairCoherentWinding(v,ix,s)&&ix==good);
  v=original;ix={0,2};good=ix;assert(!repairCoherentWinding(v,ix,s)&&ix==good);
  v=original;ix={0,0,0};good=ix;assert(repairCoherentWinding(v,ix,s)&&s.flipped==0&&ix==good);
  // Triangle order/count is invariant, so a coarse/fine split remains an index-count boundary.
  v=original;ix={0,2,1,0,2,1};const uint32_t fineStart=3;
  assert(repairCoherentWinding(v,ix,s)&&s.flipped==2&&ix.size()==6&&fineStart==3&&v==original);
  assert(ix==std::vector<uint32_t>({0,1,2,0,1,2}));
  // Native Swift bright-fleck witness, static cache triangle 146476.
  std::vector<float> swift={-0.385614038f,0.185181677f,-2.74278712f,0.793511391f,-0.544367909f,0.272035629f,11.0f,0.69958353f,-0.385950923f,0.180993259f,-2.75090051f,0.797143102f,-0.538379967f,0.273331016f,11.0f,0.647515893f,-0.388762414f,0.174513072f,-2.75607133f,0.807487905f,-0.527468622f,0.264083415f,11.0f,0.610155404f};
  const auto swiftBefore=swift;std::vector<uint32_t> swiftIx={0,1,2};
  assert(repairCoherentWinding(swift,swiftIx,s)&&s.flipped==1&&swift==swiftBefore&&swiftIx==std::vector<uint32_t>({0,2,1}));
  puts("mesh_orientation_test: coherent reversal, valid backface, paired geometry, mixed materials, ambiguous normals, invalid input, immutable attributes and partition preservation passed");
}
