// Cold-path orientation recovery. Geometry, attributes and triangle ranges are unchanged.
#pragma once
#include "mesh_validation.h"
#include <algorithm>
#include <unordered_map>

namespace aircraftMesh {
struct WindingRepairStats { size_t triangles=0, candidates=0, pairedSkipped=0, flipped=0; };
namespace orientationDetail {
using Key=std::array<std::array<uint32_t,3>,3>;
struct KeyHash {
  size_t operator()(const Key& k) const {
    size_t h=1469598103934665603ull;
    for(const auto& p:k) for(uint32_t x:p) {h^=x;h*=1099511628211ull;}
    return h;
  }
};
inline Key keyOf(const std::vector<float>& v,const uint32_t* ix,bool& odd){
  Key k;
  for(int i=0;i<3;i++)for(int j=0;j<3;j++){
    const float x=v[size_t(ix[i])*8+j];uint32_t bits=0;
    if(x!=0.f) std::memcpy(&bits,&x,sizeof bits); // canonicalise signed zero
    k[i][j]=bits;
  }
  odd=false;
  for(int i=0;i<2;i++)for(int j=i+1;j<3;j++)if(k[j]<k[i]){std::swap(k[j],k[i]);odd=!odd;}
  return k;
}
}
// Flip only unambiguously reversed, single-material triangles: every field normal must
// oppose the geometric normal by at least cos(18.2deg), and every pair of field normals
// must agree within cos(5.73deg). Ambiguous creases remain untouched. Explicit coincident
// front/back triangle pairs are excluded, including pairs with duplicated vertex records.
// This uses no eye direction: valid back-facing or mirrored geometry is not reoriented.
inline bool repairCoherentWinding(const std::vector<float>& v,std::vector<uint32_t>& ix,WindingRepairStats& stats){
  stats={};
  if(v.size()%8 || ix.size()%3) return false;
  const size_t nv=v.size()/8;stats.triangles=ix.size()/3;
  for(uint32_t q:ix){
    if(q>=nv) return false;
    const float* p=&v[size_t(q)*8];
    for(int j=0;j<7;j++)if(!finite(p[j]))return false;
    if(!normalValid(p+3))return false;
  }
  struct Candidate {size_t at;bool odd;bool paired=false;};
  std::vector<Candidate> candidates;
  std::unordered_map<orientationDetail::Key,std::vector<size_t>,orientationDetail::KeyHash> keys;
  for(size_t at=0;at<ix.size();at+=3){
    const float *p[3]={&v[size_t(ix[at])*8],&v[size_t(ix[at+1])*8],&v[size_t(ix[at+2])*8]};
    if(p[0][6]!=p[1][6] || p[0][6]!=p[2][6])continue;
    double a[3],b[3],g[3];for(int j=0;j<3;j++){a[j]=double(p[1][j])-p[0][j];b[j]=double(p[2][j])-p[0][j];}
    g[0]=a[1]*b[2]-a[2]*b[1];g[1]=a[2]*b[0]-a[0]*b[2];g[2]=a[0]*b[1]-a[1]*b[0];
    double dot[3]={};for(int i=0;i<3;i++)for(int j=0;j<3;j++)dot[i]+=g[j]*p[i][j+3];
    if(dot[0]>=0 || dot[1]>=0 || dot[2]>=0)continue;
    const double g2=g[0]*g[0]+g[1]*g[1]+g[2]*g[2];if(g2<=1e-28)continue;
    double n2[3]={};for(int i=0;i<3;i++)for(int j=0;j<3;j++)n2[i]+=double(p[i][j+3])*p[i][j+3];
    bool coherent=true;
    for(int i=0;i<3;i++)if(dot[i]>-.95*std::sqrt(g2*n2[i]))coherent=false;
    for(int i=0;i<2;i++)for(int j=i+1;j<3;j++){
      double nd=0;for(int k=0;k<3;k++)nd+=double(p[i][k+3])*p[j][k+3];
      if(nd<.995*std::sqrt(n2[i]*n2[j]))coherent=false;
    }
    if(!coherent)continue;
    bool odd;auto key=orientationDetail::keyOf(v,&ix[at],odd);
    keys[key].push_back(candidates.size());candidates.push_back({at,odd,false});
  }
  stats.candidates=candidates.size();
  if(candidates.empty())return true;
  // Preserve intentionally double-sided geometry. Hash only the sparse repair candidates.
  for(size_t at=0;at<ix.size();at+=3){
    bool odd;const auto key=orientationDetail::keyOf(v,&ix[at],odd);auto found=keys.find(key);
    if(found==keys.end())continue;
    for(size_t k:found->second)if(candidates[k].odd!=odd)candidates[k].paired=true;
  }
  for(const auto& c:candidates){
    if(c.paired){stats.pairedSkipped++;continue;}
    std::swap(ix[c.at+1],ix[c.at+2]);stats.flipped++;
  }
  return true;
}
}
