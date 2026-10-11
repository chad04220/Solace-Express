// Exact shading normals for verified machined Wraith cylinder/ring surfaces.
// Geometry/materials stay unchanged; sharp cap/wall seams get separate corners.
#pragma once
#include "aircraft_mesh_build_types.h"
#include <vector>
#include <cstdint>
#include <cmath>
namespace wraithLoadout {
struct RoundSurface { float y,z,outer,inner,half; };
inline std::vector<RoundSurface> roundSurfaces(int type){
 using namespace aircraftBuild;
 std::vector<RoundSurface> s;
 if(type==PT_WR_BOMB){for(int j:{-1,1})s.push_back({0,j*.152f,.238f,.221f,.020f});s.push_back({0,.26f,.061f,0,.018f});}
 if(type==PT_WR_KINETIC){for(int j=0;j<3;j++)s.push_back({-.02f,-.36f-j*.29f,.096f,.075f,.020f});s.push_back({-.02f,-1.045f,.094f,.082f,.026f});s.push_back({-.02f,-.26f,.109f,0,.055f});}
 if(type==PT_WR_CHARGED){s.push_back({-.02f,-1.034f,.086f,.047f,.030f});s.push_back({-.02f,-1.048f,.049f,0,.025f});s.push_back({-.02f,-1.067f,.038f,0,.008f});}
 if(type==PT_WR_PENETRATOR){for(int j=0;j<2;j++)s.push_back({0,-.23f+j*.49f,.166f,.150f,.018f});s.push_back({0,.665f,.132f,0,.042f});s.push_back({0,.71f,.062f,0,.018f});}
 if(type==PT_WR_EMP){for(int j=-1;j<=1;j++)s.push_back({0,j*.06f,.224f,.188f,.014f});s.push_back({0,0,.230f,.188f,.008f});for(int j:{-1,1}){s.push_back({0,j*.408f,.206f,0,.025f});s.push_back({0,j*.449f,.122f,0,.019f});}}
 return s;
}
inline size_t repairRoundNormals(int type,std::vector<float>& vb,std::vector<uint32_t>& ib){
 const auto surfaces=roundSurfaces(type);size_t repaired=0;
 // Tolerance is below the 3 mm lattice and only admits a single authored face.
 constexpr float tol=.0015f;
 for(size_t t=0;t+2<ib.size();t+=3){
  vec3 p[3];for(int k=0;k<3;k++){const float* v=&vb[size_t(ib[t+k])*8];p[k]=vec3(v[0],v[1],v[2]);}
  vec3 fn=cross(p[1]-p[0],p[2]-p[0]);float fl=length(fn);if(fl<1e-12f)continue;fn=fn/fl;
  bool fixed=false;
  for(const auto& s:surfaces){
   vec3 q[3];float r[3];for(int k=0;k<3;k++){q[k]=p[k]-vec3(0,s.y,s.z);r[k]=sqrtf(q[k].x*q[k].x+q[k].y*q[k].y);}
   for(int face=0;face<4&&!fixed;face++){
    if(face==1&&s.inner<=0)continue;
    bool valid=true;vec3 n[3];
    for(int k=0;k<3;k++){
     if(face<2){float radius=face==0?s.outer:s.inner;valid=valid&&fabsf(r[k]-radius)<=tol&&fabsf(q[k].z)<=s.half+tol&&r[k]>1e-6f;n[k]=vec3(q[k].x/r[k],q[k].y/r[k],0)*(face==0?1.f:-1.f);}
     else{float sign=face==2?1.f:-1.f;valid=valid&&fabsf(q[k].z-sign*s.half)<=tol&&r[k]<=s.outer+tol&&r[k]>=s.inner-tol;n[k]=vec3(0,0,sign);}
     valid=valid&&dot(fn,n[k])>.85f;
    }
    if(!valid)continue;
    for(int k=0;k<3;k++){float v[8];const size_t at=size_t(ib[t+k])*8;for(int j=0;j<8;j++)v[j]=vb[at+j];v[3]=n[k].x;v[4]=n[k].y;v[5]=n[k].z;ib[t+k]=uint32_t(vb.size()/8);vb.insert(vb.end(),v,v+8);}
    repaired++;fixed=true;
   }
   if(fixed)break;
  }
 }
 return repaired;
}
}
