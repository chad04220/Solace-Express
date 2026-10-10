// Cold-bake shading repair; positions, materials, AO and triangle ranges are unchanged.
#pragma once
#include "mesh_validation.h"
#include <algorithm>

namespace aircraftMesh {
struct PlanarRepairStats { size_t tested=0, planar=0, repaired=0, addedVertices=0; };
// Inset field gradients are already evaluated by the rigid-part crease pass: three
// RGBA records per triangle, one a fifth of the way from each corner to its centre.
// Only a field-verified planar face receives flat corner normals. Rounded edges and
// curved/twisted faces fail the gradient agreement test and retain their own normals.
inline bool repairVerifiedPlanarNormals(std::vector<float>& v,std::vector<uint32_t>& ix,
                                       const std::vector<float>& gradients,PlanarRepairStats& stats){
  stats={};
  if(v.size()%8 || ix.size()%3 || gradients.size()!=ix.size()*4)return false;
  const size_t nv=v.size()/8;
  for(uint32_t q:ix)if(q>=nv)return false;
  constexpr double faceCos=.9993908270190958; // 2 degrees, matches the static cabin test
  constexpr double agreeCos=.9999904807207345; // .25 degrees among independent field samples
  constexpr double keepCos=.9996573249755573; // 1.5 degrees, no needless vertex copies
  const size_t nt=ix.size()/3;stats.tested=nt;
  for(size_t t=0;t<nt;t++){
    const float* p[3]={&v[size_t(ix[3*t])*8],&v[size_t(ix[3*t+1])*8],&v[size_t(ix[3*t+2])*8]};
    if(p[0][6]!=p[1][6] || p[0][6]!=p[2][6])continue;
    double a[3],b[3],f[3];
    for(int j=0;j<3;j++){a[j]=double(p[1][j])-p[0][j];b[j]=double(p[2][j])-p[0][j];}
    f[0]=a[1]*b[2]-a[2]*b[1];f[1]=a[2]*b[0]-a[0]*b[2];f[2]=a[0]*b[1]-a[1]*b[0];
    double fl=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);if(!(fl>1e-14))continue;
    for(double& q:f)q/=fl;
    double n[3][3];bool flat=true;
    for(int k=0;k<3;k++){
      const float* g=&gradients[(t*3+k)*4];
      if(!normalValid(g)){flat=false;break;}
      const double l=std::sqrt(double(g[0])*g[0]+double(g[1])*g[1]+double(g[2])*g[2]);
      for(int j=0;j<3;j++)n[k][j]=g[j]/l;
    }
    if(!flat)continue;
    if(f[0]*n[0][0]+f[1]*n[0][1]+f[2]*n[0][2]<0)for(double& q:f)q=-q;
    for(int k=0;k<3 && flat;k++){
      double fd=0;for(int j=0;j<3;j++)fd+=f[j]*n[k][j];if(fd<faceCos)flat=false;
      for(int q=k+1;q<3;q++){double nd=0;for(int j=0;j<3;j++)nd+=n[k][j]*n[q][j];if(nd<agreeCos)flat=false;}
    }
    if(!flat)continue;
    stats.planar++;
    bool off=false;
    for(int k=0;k<3;k++){double nd=0;for(int j=0;j<3;j++)nd+=f[j]*p[k][j+3];if(nd<keepCos)off=true;}
    if(!off)continue;
    // Appending may reallocate v, so re-fetch each source by its index, not p[].
    for(int k=0;k<3;k++){
      float q[8];std::memcpy(q,&v[size_t(ix[t*3+k])*8],sizeof q);
      for(int j=0;j<3;j++)q[j+3]=float(f[j]);
      ix[t*3+k]=uint32_t(v.size()/8);v.insert(v.end(),q,q+8);
    }
    stats.repaired++;stats.addedVertices+=3;
  }
  return true;
}
}
