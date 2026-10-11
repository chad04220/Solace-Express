// Cold-path sparse surface nets for an arbitrary bounded signed distance field.
// Geometry and shading quality match the aircraft exterior bake. No field evaluation
// happens at draw time. The evaluator must use the same pose/material field as drawing.
#pragma once
#include "common.h"
#include "mesh_validation.h"
#include "mesh_simplify.h"
#include "aircraft_mesh_planar.h"
#include <functional>
#include <limits>
#include <unordered_map>

namespace enemyMesh {
inline constexpr float kSpacing = 0.015625f;
inline constexpr uint32_t kAlgorithmVersion = 1;
inline constexpr const char* kAlgorithmManifest =
    "enemy-mesh1:octree-band-slack1.3;blocks62.5mm;lattice15.625mm;three-clamped-pulls;"
    "sign-winding;adjacency-normal-repair;boundary-material-qem1mm-edge500mm;verified-planar-inset20pct";
using Evaluator = std::function<void(const std::vector<vec3>&, std::vector<float>&, bool)>;
namespace detail {
struct Cell { int x, y, z; };
inline uint64_t key(int x,int y,int z) {
  return (uint64_t(x)<<42)|(uint64_t(y)<<21)|uint64_t(z);
}
struct Hash { size_t operator()(uint64_t x) const {
  x ^= x>>30; x *= 0xbf58476d1ce4e5b9ULL; x ^= x>>27;
  x *= 0x94d049bb133111ebULL; return size_t(x^(x>>31));
}};
}
// eval returns four floats per point: distance/material/AO/unused when normals=false,
// normal xyz/unused when true. It can use GPU batches; calls here are at most 32K points.
// Bounds must enclose the complete surface. As for aircraft, distances may overstate
// true distance by at most 30%; arbitrary non-distance implicit functions are unsuitable.
// Returns false on malformed samples, clipped/incomplete topology or invalid geometry.
// Outputs are replaced only on success; a failed bake can safely use the SDF fallback.
inline bool build(const vec3& lo,const vec3& hi,const Evaluator& eval,
                  const std::function<void()>& tick,std::vector<float>& vb,std::vector<uint32_t>& ib) {
  using detail::Cell;
  constexpr float h=kSpacing, block=4*h;
  constexpr size_t batch=32768;
  if(!eval)return false;
  for(int j=0;j<3;j++)if(!aircraftMesh::finite(lo[j]) || !aircraftMesh::finite(hi[j]) || hi[j]<=lo[j])return false;
  const vec3 origin(std::floor(lo.x/block)*block-2*block,std::floor(lo.y/block)*block-2*block,
                    std::floor(lo.z/block)*block-2*block);
  int extent[3];
  for(int j=0;j<3;j++) {
    double e=std::ceil((double(hi[j])-origin[j])/block)+2;
    if(e>262144 || e<1)return false;
    extent[j]=int(e)*4;
  }
  int size=4;while(size<std::max({extent[0],extent[1],extent[2]}))size*=2;
  auto point=[&](float x,float y,float z){return origin+vec3(x,y,z)*h;};
  auto pulse=[&](){if(tick)tick();};
  auto sample=[&](const std::vector<vec3>& p,std::vector<float>& out,bool normals) {
    out.clear();out.reserve(p.size()*4);
    std::vector<vec3> chunk;std::vector<float> result;
    for(size_t at=0;at<p.size();at+=batch) {
      const size_t n=std::min(batch,p.size()-at);
      chunk.assign(p.begin()+at,p.begin()+at+n);result.clear();
      eval(chunk,result,normals);
      if(result.size()!=n*4)return false;
      // Invalid gradients at singularities are repaired from oriented adjacency below.
      if(!normals)for(size_t q=0;q<n;q++)for(int j=0;j<3;j++)
        if(!aircraftMesh::finite(result[q*4+j]))return false;
      out.insert(out.end(),result.begin(),result.end());pulse();
    }
    return true;
  };
  // Adaptive octree discovery, terminating in sparse 6.25 cm blocks. All descendants
  // of a retained block use the identical 1.5625 cm global lattice, so no LOD seams.
  std::vector<Cell> cells(1,Cell{0,0,0}),next;
  std::vector<vec3> points;std::vector<float> values;
  for(;;) {
    next.clear();
    const float keep=1.3f*.866025404f*(size*h)+2.5f*h;
    for(size_t at=0;at<cells.size();at+=batch) {
      size_t n=std::min(batch,cells.size()-at);points.clear();points.reserve(n);
      for(size_t q=0;q<n;q++){const Cell& c=cells[at+q];points.push_back(point(c.x+size*.5f,c.y+size*.5f,c.z+size*.5f));}
      if(!sample(points,values,false))return false;
      for(size_t q=0;q<n;q++) {
        if(std::fabs(values[q*4])>keep)continue;
        const Cell& c=cells[at+q];
        if(size==4){next.push_back(c);continue;}
        int sub=size/2;
        for(int k=0;k<8;k++) {
          Cell child{c.x+(k&1)*sub,c.y+((k>>1)&1)*sub,c.z+(k>>2)*sub};
          if(child.x<extent[0] && child.y<extent[1] && child.z<extent[2])next.push_back(child);
        }
      }
    }
    cells.swap(next);if(cells.empty())return false;
    if(size==4)break;
    size/=2;
  }
  std::vector<vec3> positions;std::vector<Cell> cubes;
  std::vector<uint8_t> crossings,negative;
  std::unordered_map<uint64_t,uint32_t,detail::Hash> vertex;
  vertex.reserve(cells.size()*8);
  constexpr int edges[12][2]={{0,1},{1,3},{2,3},{0,2},{4,5},{5,7},{6,7},{4,6},{0,4},{1,5},{3,7},{2,6}};
  // Stream block corners instead of retaining a dense grid or every corner's hash.
  // Shared block boundaries are evaluated at exactly the same lattice positions.
  constexpr size_t blocksPerBatch=batch/125;
  for(size_t at=0;at<cells.size();at+=blocksPerBatch) {
    size_t n=std::min(blocksPerBatch,cells.size()-at);points.clear();points.reserve(n*125);
    for(size_t q=0;q<n;q++) {
      const Cell& c=cells[at+q];
      for(int z=0;z<=4;z++)for(int y=0;y<=4;y++)for(int x=0;x<=4;x++)points.push_back(point(c.x+x,c.y+y,c.z+z));
    }
    if(!sample(points,values,false))return false;
    for(size_t q=0;q<n;q++) {
      const Cell& c=cells[at+q];
      for(int z=0;z<4;z++)for(int y=0;y<4;y++)for(int x=0;x<4;x++) {
        float d[8];unsigned mask=0;
        for(int k=0;k<8;k++) {
          size_t i=q*125+((z+(k>>2))*5+y+((k>>1)&1))*5+x+(k&1);
          d[k]=values[i*4];if(d[k]<0)mask|=1u<<k;
        }
        if(mask==0 || mask==255)continue;
        Cell cube{c.x+x,c.y+y,c.z+z};vec3 sum;int count=0;
        for(const auto& edge:edges) {
          int a=edge[0],b=edge[1];if((d[a]<0)==(d[b]<0))continue;
          // Double denominator avoids overflow for otherwise finite field outputs.
          float t=float(double(d[a])/(double(d[a])-d[b]));
          vec3 pa=point(cube.x+(a&1),cube.y+((a>>1)&1),cube.z+(a>>2));
          vec3 pb=point(cube.x+(b&1),cube.y+((b>>1)&1),cube.z+(b>>2));
          sum+=pa+(pb-pa)*t;count++;
        }
        if(positions.size()>=std::numeric_limits<uint32_t>::max())return false;
        vertex.emplace(detail::key(cube.x,cube.y,cube.z),uint32_t(positions.size()));
        positions.push_back(sum/float(count));cubes.push_back(cube);negative.push_back(d[0]<0);
        crossings.push_back(uint8_t(((d[0]<0)!=(d[1]<0)) | (((d[0]<0)!=(d[2]<0))<<1) | (((d[0]<0)!=(d[4]<0))<<2)));
      }
    }
  }
  std::vector<Cell>().swap(cells);std::vector<Cell>().swap(next);
  if(positions.empty())return false;
  std::vector<float> normals,field;
  for(int iteration=0;iteration<3;iteration++) {
    if(!sample(positions,normals,true) || !sample(positions,field,false))return false;
    for(size_t q=0;q<positions.size();q++) {
      if(!aircraftMesh::normalValid(&normals[q*4]))continue;
      vec3 normal=normalize(vec3(normals[q*4],normals[q*4+1],normals[q*4+2]));
      vec3 p=positions[q]-normal*clampf(field[q*4],-h,h);
      const Cell& c=cubes[q];vec3 low=point(c.x,c.y,c.z)-vec3(.25f*h);
      positions[q]=vec3(clampf(p.x,low.x,low.x+1.5f*h),clampf(p.y,low.y,low.y+1.5f*h),clampf(p.z,low.z,low.z+1.5f*h));
    }
  }
  if(!sample(positions,normals,true) || !sample(positions,field,false))return false;
  std::vector<float> vertices;std::vector<uint32_t> indices;
  vertices.reserve(positions.size()*8);indices.reserve(positions.size()*6);
  for(size_t q=0;q<positions.size();q++) {
    const vec3& p=positions[q];vec3 n;
    if(aircraftMesh::normalValid(&normals[q*4]))n=normalize(vec3(normals[q*4],normals[q*4+1],normals[q*4+2]));
    const float v[8]={p.x,p.y,p.z,n.x,n.y,n.z,std::floor(field[q*4+1]+.5f),clampf(field[q*4+2],0.f,1.f)};
    vertices.insert(vertices.end(),v,v+8);
  }
  auto find=[&](int x,int y,int z,uint32_t& out) {
    if(x<0 || y<0 || z<0)return false;
    auto i=vertex.find(detail::key(x,y,z));if(i==vertex.end())return false;out=i->second;return true;
  };
  for(size_t q=0;q<cubes.size();q++) {
    if((q&16383)==0)pulse();
    const Cell& p=cubes[q];
    for(int axis=0;axis<3;axis++)if(crossings[q]&(1<<axis)) {
      uint32_t a=uint32_t(q),b,c,d;bool complete=false;
      if(axis==0)complete=find(p.x,p.y-1,p.z,b)&&find(p.x,p.y-1,p.z-1,c)&&find(p.x,p.y,p.z-1,d);
      if(axis==1)complete=find(p.x,p.y,p.z-1,b)&&find(p.x-1,p.y,p.z-1,c)&&find(p.x-1,p.y,p.z,d);
      if(axis==2)complete=find(p.x-1,p.y,p.z,b)&&find(p.x-1,p.y-1,p.z,c)&&find(p.x,p.y-1,p.z,d);
      if(!complete)return false; // never publish a silently truncated surface
      // These rings face +axis before reversal. Sign-based winding remains defined
      // at field-gradient singularities and is independent of noisy vertex normals.
      if(!negative[q])std::swap(b,d);
      indices.insert(indices.end(),{a,b,c,a,c,d});
    }
  }
  vertex.clear();vertex.rehash(0);
  std::vector<vec3>().swap(positions);std::vector<Cell>().swap(cubes);
  std::vector<float>().swap(normals);std::vector<float>().swap(field);
  if(indices.empty() || !aircraftMesh::pruneDegenerate(vertices,indices) ||
     !aircraftMesh::repairNormals(vertices,indices) || !aircraftMesh::valid(vertices,indices))return false;
  pulse();size_t end=indices.size();simplifyMesh(vertices,indices,end,.001f,.04f,.5f);pulse();
  // Verify broad planar faces against three independent inset field gradients.
  // Stream this final shading pass, retaining the existing aircraft repair rules.
  constexpr size_t indexBatch=(batch/3)*3;
  for(size_t at=0;at<indices.size();at+=indexBatch) {
    size_t n=std::min(indexBatch,indices.size()-at);points.clear();points.reserve(n);
    auto pos=[&](uint32_t i){return vec3(vertices[size_t(i)*8],vertices[size_t(i)*8+1],vertices[size_t(i)*8+2]);};
    for(size_t t=at;t<at+n;t+=3) {
      vec3 a=pos(indices[t]),b=pos(indices[t+1]),c=pos(indices[t+2]),centre=(a+b+c)/3.f;
      points.push_back(a*.8f+centre*.2f);points.push_back(b*.8f+centre*.2f);points.push_back(c*.8f+centre*.2f);
    }
    if(!sample(points,values,true))return false;
    std::vector<uint32_t> portion(indices.begin()+at,indices.begin()+at+n);
    aircraftMesh::PlanarRepairStats stats;
    if(!aircraftMesh::repairVerifiedPlanarNormals(vertices,portion,values,stats))return false;
    std::copy(portion.begin(),portion.end(),indices.begin()+at);
  }
  if(!aircraftMesh::pruneDegenerate(vertices,indices) || !aircraftMesh::valid(vertices,indices))return false;
  vb.swap(vertices);ib.swap(indices);pulse();return true;
}
}
