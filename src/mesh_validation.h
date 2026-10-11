// Cold-path aircraft mesh integrity and recovery. No SDF work is added to draws.
#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace aircraftMesh {
// Explicit algorithm manifest: change when extraction/projection/simplification changes.
inline constexpr uint32_t kAlgorithmVersion = 24;
inline constexpr uint32_t kMaxPartType = 50; // update alongside the PT_* enum when adding a rigid part
inline constexpr const char* kWraithLoadoutManifest = "wraith-loadout3:verified-planar-and-round-face-normals;separate-rigid-kinetic-charged-penetrator-emp;3mm-lattice;shared-mount-poses;selected-parts-all-passes";
inline constexpr float kAtlasDoorMaxEdge = .20f;
inline constexpr const char* kAlgorithmManifest = "mesh24:upstream-part-creases;exterior-edge500mm;finite-gradient-adjacency;prune-zero-area;semantic-fixtures;compact-flat;bounded-workers;nightjar-overhead-edge80mm;atlas-main-door-edge200mm;islander-verified-planar;conventional-cabin-coherent-winding;atlas-swept-root-bays;specter-toggle-detail3.90625mm";
inline bool finite(float f) { uint32_t bits; std::memcpy(&bits, &f, sizeof bits); return (bits & 0x7f800000u) != 0x7f800000u; }
inline bool finite(double f) { uint64_t bits; std::memcpy(&bits, &f, sizeof bits); return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull; }
inline bool normalValid(const float* p) {
  if (!finite(p[0]) || !finite(p[1]) || !finite(p[2])) return false;
  const float l2 = p[0]*p[0] + p[1]*p[1] + p[2]*p[2];
  return finite(l2) && l2 > 1e-12f;
}
inline bool valid(const float* v, size_t nf, const uint32_t* ix, size_t ni) {
  if (nf % 8 || ni % 3) return false;
  for (size_t i = 0; i < nf; i++) if (!finite(v[i])) return false;
  for (size_t i = 0; i < nf; i += 8) {
    const float l2 = v[i+3]*v[i+3] + v[i+4]*v[i+4] + v[i+5]*v[i+5];
    if (l2 < 0.98f || l2 > 1.02f || v[i+7] < 0.f || v[i+7] > 1.001f) return false;
  }
  for (size_t i = 0; i < ni; i++) if (ix[i] >= nf/8) return false;
  return true;
}
inline bool valid(const std::vector<float>& v, const std::vector<uint32_t>& ix) { return valid(v.data(), v.size(), ix.data(), ix.size()); }
// Retain crossing/QEF positions. Only unresolved field normals are reconstructed from oriented,
// area-weighted adjacent faces; surviving field normals orient the faces. Never invent a global axis.
inline bool repairNormals(std::vector<float>& v, const std::vector<uint32_t>& ix) {
  if (v.size() % 8 || ix.size() % 3) return false;
  const size_t nv = v.size()/8;
  std::vector<uint8_t> bad(nv); size_t nb = 0;
  for (size_t i = 0; i < nv; i++) { bad[i] = !normalValid(&v[i*8+3]); nb += bad[i]; }
  if (!nb) return true;
  std::vector<std::array<double,3>> sums(nv), largest(nv);
  std::vector<double> maxArea(nv);
  for (size_t t = 0; t < ix.size(); t += 3) {
    uint32_t a=ix[t], b=ix[t+1], c=ix[t+2]; if (a>=nv || b>=nv || c>=nv) return false;
    const float* A=&v[a*8], *B=&v[b*8], *C=&v[c*8];
    double x=B[0]-A[0],y=B[1]-A[1],z=B[2]-A[2], u=C[0]-A[0],w=C[1]-A[1],r=C[2]-A[2];
    std::array<double,3> n={y*r-z*w,z*u-x*r,x*w-y*u};
    const double area=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
    if (!finite(area) || area < 1e-28) continue;
    double orient=0; for (uint32_t q : {a,b,c}) if (!bad[q]) for (int j=0;j<3;j++) orient+=n[j]*v[q*8+3+j];
    if (orient<0) for (double& f:n) f=-f;
    for (uint32_t q : {a,b,c}) if (bad[q]) {
      for (int j=0;j<3;j++) sums[q][j]+=n[j];
      if (area>maxArea[q]) { largest[q]=n; maxArea[q]=area; }
    }
  }
  for (size_t i=0;i<nv;i++) if (bad[i]) {
    auto n=sums[i]; double l2=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
    if (l2<1e-28) { n=largest[i]; l2=maxArea[i]; }
    if (!(l2>1e-28) || !finite(l2)) return false;
    for (int j=0;j<3;j++) v[i*8+3+j]=(float)(n[j]/std::sqrt(l2));
  }
  return true;
}
// Crossings at an SDF singularity can produce a collapsed, zero-area closed cell. Such
// triangles have no surface or oriented normal. Remove only geometrically degenerate faces,
// then compact their now-orphaned vertices; never move a surviving vertex to invent a surface.
inline bool pruneDegenerate(std::vector<float>& v, std::vector<uint32_t>& ix) {
  if (v.size()%8 || ix.size()%3) return false;
  size_t out=0;
  for(size_t i=0;i<ix.size();i+=3) {
    const uint32_t a=ix[i],b=ix[i+1],c=ix[i+2];
    if(a>=v.size()/8 || b>=v.size()/8 || c>=v.size()/8) return false;
    const float* A=&v[a*8],*B=&v[b*8],*C=&v[c*8];
    const double x=(double)B[0]-A[0],y=(double)B[1]-A[1],z=(double)B[2]-A[2];
    const double u=(double)C[0]-A[0],w=(double)C[1]-A[1],r=(double)C[2]-A[2];
    const double nx=y*r-z*w,ny=z*u-x*r,nz=x*w-y*u,area=nx*nx+ny*ny+nz*nz;
    if (!finite(area)) return false;
    if (area <= 1e-28) continue;
    ix[out++]=a;ix[out++]=b;ix[out++]=c;
  }
  ix.resize(out);return true;
}
// Stable first-reference ordering keeps every triangle attribute and fine-patch index range intact.
inline bool compact(std::vector<float>& v, std::vector<uint32_t>& ix) {
  if (v.size()%8) return false;
  std::vector<uint32_t> map(v.size()/8, std::numeric_limits<uint32_t>::max());
  std::vector<float> out; out.reserve(v.size());
  for (uint32_t& q:ix) {
    if (q>=map.size()) return false;
    if (map[q]==std::numeric_limits<uint32_t>::max()) { map[q]=(uint32_t)(out.size()/8); out.insert(out.end(),v.begin()+q*8,v.begin()+q*8+8); }
    q=map[q];
  }
  v.swap(out); return true;
}
inline bool validBlob(const std::vector<uint32_t>& b) {
  for (size_t at=0;at<b.size();) {
    if (b.size()-at<3) return false;
    size_t nf=b[at+1],ni=b[at+2];
    if (b[at]>kMaxPartType || nf%8 || ni%3 || nf>b.size()-at-3 || ni>b.size()-at-3-nf) return false;
    // memcpy avoids aliasing uint32 storage as floats under optimized builds.
    std::vector<float> v(nf); if(nf) std::memcpy(v.data(), &b[at+3], nf*sizeof(float));
    if (!valid(v.data(),nf,b.data()+at+3+nf,ni)) return false;
    at+=3+nf+ni;
  }
  return true;
}
}
