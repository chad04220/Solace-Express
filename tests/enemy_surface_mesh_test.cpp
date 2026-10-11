// Standalone CPU fixtures for the GPU-batched enemy surface extraction contract.
#include "enemy_surface_mesh.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>

namespace {
float boxDistance(vec3 p,vec3 halfSize) {
  vec3 q(std::fabs(p.x)-halfSize.x,std::fabs(p.y)-halfSize.y,std::fabs(p.z)-halfSize.z);
  return length(vec3(std::max(q.x,0.f),std::max(q.y,0.f),std::max(q.z,0.f)))+
         std::min(std::max({q.x,q.y,q.z}),0.f);
}
using Field=std::function<float(vec3)>;
struct Result {std::vector<float> vertices;std::vector<uint32_t> indices;};
vec3 position(const Result& mesh,uint32_t index) {
  const float* v=&mesh.vertices[size_t(index)*8];return vec3(v[0],v[1],v[2]);
}
Result extract(const Field& field,bool singularNormals=false) {
  Result mesh;size_t maxBatch=0,ticks=0;
  auto evaluate=[&](const std::vector<vec3>& points,std::vector<float>& out,bool normals) {
    maxBatch=std::max(maxBatch,points.size());out.assign(points.size()*4,0.f);
    for(size_t j=0;j<points.size();j++) {
      const vec3& p=points[j];
      if(normals) {
        constexpr float e=.0002f;
        vec3 n=normalize(vec3(field(p+vec3(e,0,0))-field(p-vec3(e,0,0)),
                              field(p+vec3(0,e,0))-field(p-vec3(0,e,0)),
                              field(p+vec3(0,0,e))-field(p-vec3(0,0,e))));
        out[j*4]=singularNormals?std::numeric_limits<float>::quiet_NaN():n.x;
        out[j*4+1]=n.y;out[j*4+2]=n.z;
      } else {
        out[j*4]=field(p);out[j*4+1]=p.x<0.f?4.f:7.f;out[j*4+2]=.8f;
      }
    }
  };
  assert(enemyMesh::build(vec3(-.5f),vec3(.5f),evaluate,[&](){ticks++;},mesh.vertices,mesh.indices));
  assert(!mesh.indices.empty());assert(aircraftMesh::valid(mesh.vertices,mesh.indices));
  assert(maxBatch<=32768 && maxBatch>0 && ticks>0);
  float maxError=0;
  for(size_t j=0;j<mesh.vertices.size();j+=8) {
    maxError=std::max(maxError,std::fabs(field(vec3(mesh.vertices[j],mesh.vertices[j+1],mesh.vertices[j+2]))));
    assert(mesh.vertices[j+6]==4.f || mesh.vertices[j+6]==7.f);
    assert(std::fabs(mesh.vertices[j+7]-.8f)<1e-6f);
  }
  assert(maxError<.002f);
  printf("enemy mesh: %zu vertices, %zu triangles, max surface error %.6f m\n",mesh.vertices.size()/8,mesh.indices.size()/3,maxError);
  return mesh;
}
void assertOutward(const Result& mesh,const std::function<vec3(vec3)>& centreFor) {
  for(size_t t=0;t<mesh.indices.size();t+=3) {
    vec3 a=position(mesh,mesh.indices[t]),b=position(mesh,mesh.indices[t+1]),c=position(mesh,mesh.indices[t+2]);
    vec3 centre=(a+b+c)/3.f,n=cross(b-a,c-a);
    assert(dot(n,centre-centreFor(centre))>0.f);
  }
}
}
int main() {
  assert(enemyMesh::kSpacing==.015625f);
  assert(enemyMesh::kAlgorithmVersion>0 && std::strlen(enemyMesh::kAlgorithmManifest)>0);
  // Curved closed surface: exact outward orientation survives simplification.
  auto sphere=extract([](vec3 p){return length(p)-.15f;});
  assertOutward(sphere,[](vec3){return vec3();});
  // A 30 mm slab has two independent broad faces and survives the fine lattice.
  auto thin=extract([](vec3 p){return boxDistance(p,vec3(.21f,.015f,.13f));});
  assertOutward(thin,[](vec3){return vec3();});
  size_t top=0,bottom=0;
  for(size_t j=0;j<thin.vertices.size();j+=8) {
    top+=std::fabs(thin.vertices[j+1]-.015f)<1e-5f;
    bottom+=std::fabs(thin.vertices[j+1]+.015f)<1e-5f;
  }
  assert(top>3 && bottom>3);
  // Disconnected components must both remain, without triangles bridging the gap.
  Field pair=[](vec3 p){return std::min(length(p-vec3(.23f,0,0))-.13f,length(p+vec3(.23f,0,0))-.09f);};
  for(bool singular:{false,true}) {
    auto mesh=extract(pair,singular);size_t left=0,right=0;
    assertOutward(mesh,[](vec3 p){return vec3(p.x<0.f?-.23f:.23f,0,0);});
    for(size_t t=0;t<mesh.indices.size();t+=3) {
      bool side=position(mesh,mesh.indices[t]).x<0;
      for(int k=1;k<3;k++)assert((position(mesh,mesh.indices[t+k]).x<0)==side);
      if(side)left++;else right++;
    }
    assert(left>0 && right>0);
  }
  // Malformed sampling never overwrites previously usable caller output.
  std::vector<float> vertices{11.f};std::vector<uint32_t> indices{22};
  auto preserved=[&](){assert(vertices==std::vector<float>{11.f});assert(indices==std::vector<uint32_t>{22});};
  enemyMesh::Evaluator missing=[](const auto&,auto& out,bool){out.clear();};
  assert(!enemyMesh::build(vec3(-1),vec3(1),missing,{},vertices,indices));preserved();
  enemyMesh::Evaluator invalid=[](const auto& points,auto& out,bool){out.assign(points.size()*4,std::numeric_limits<float>::infinity());};
  assert(!enemyMesh::build(vec3(-1),vec3(1),invalid,{},vertices,indices));preserved();
  assert(!enemyMesh::build(vec3(-1),vec3(1),{}, {},vertices,indices));preserved();
  assert(!enemyMesh::build(vec3(1),vec3(-1),missing,{},vertices,indices));preserved();
  // An unbounded surface cannot produce a silently clipped "complete" mesh.
  enemyMesh::Evaluator plane=[](const auto& points,auto& out,bool normals){
    out.assign(points.size()*4,0.f);for(size_t q=0;q<points.size();q++) {
      out[q*4]=normals?1.f:points[q].x;
      out[q*4+1]=normals?0.f:7.f;out[q*4+2]=normals?0.f:1.f;
    }
  };
  assert(!enemyMesh::build(vec3(-.1f),vec3(.1f),plane,{},vertices,indices));preserved();
  puts("enemy_surface_mesh_test passed");
}
