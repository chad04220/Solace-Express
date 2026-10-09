#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace aircraftMesh {
// Keep simplified overhead triangles local in camera space. Logging vertex depth on a
// long triangle that spans the eye can put the ceiling behind the roof beyond it.
// This bounded material-14 patch leaves positions and all exterior fixtures unchanged.
// Call only for the verified Nightjar interior/cockpit-2 mesh, after static/fine merge and
// before flat shading. Other models need their own boundary/camera-envelope validation.
inline size_t boundOverheadEdges(std::vector<float>& vb, std::vector<uint32_t>& ib,
                                uint32_t& fineStart, const float eye[3]) {
  constexpr float edge2 = .08f * .08f;
  const uint32_t oldFine = fineStart;
  uint32_t newFine = 0;
  const size_t oldCount = ib.size();
  std::vector<uint32_t> next; next.reserve(ib.size());
  std::unordered_map<uint64_t, uint32_t> midpoints;
  auto midpoint = [&](uint32_t a, uint32_t b) {
    const uint64_t key = (uint64_t(std::min(a,b)) << 32) | std::max(a,b);
    auto found = midpoints.find(key); if (found != midpoints.end()) return found->second;
    float m[8];
    for (int j=0; j<8; ++j) m[j] = .5f*(vb[size_t(a)*8+j]+vb[size_t(b)*8+j]);
    float n = std::sqrt(m[3]*m[3]+m[4]*m[4]+m[5]*m[5]);
    if (n < 1e-10f) {
      for (int j=3; j<6; ++j) m[j] = vb[size_t(a)*8+j];
      n = std::sqrt(m[3]*m[3]+m[4]*m[4]+m[5]*m[5]);
    }
    for (int j=3; j<6; ++j) m[j] /= std::max(n,1e-20f);
    m[6] = 14.f; // both endpoints are the same semantic material
    const uint32_t index = uint32_t(vb.size()/8);
    vb.insert(vb.end(),m,m+8); midpoints.emplace(key,index); return index;
  };
  std::vector<std::array<uint32_t,3>> pending;
  for (size_t t=0; t<ib.size(); t+=3) {
    bool overhead=true;
    for (int k=0; k<3 && overhead; ++k) {
      const float* v=&vb[size_t(ib[t+k])*8];
      overhead = v[6]==14.f && v[1]>eye[1]+.04f && std::abs(v[0])<.31f
                 && std::abs(v[2]-eye[2])<.70f;
    }
    if (!overhead) next.insert(next.end(),ib.begin()+t,ib.begin()+t+3);
    else {
      pending.push_back({ib[t],ib[t+1],ib[t+2]});
      while (!pending.empty()) {
        auto tri=pending.back(); pending.pop_back(); int longest=0; float longest2=0.f;
        for (int k=0; k<3; ++k) {
          const float* a=&vb[size_t(tri[k])*8]; const float* b=&vb[size_t(tri[(k+1)%3])*8];
          const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];
          const float d2=dx*dx+dy*dy+dz*dz;
          if (d2>longest2) {longest2=d2;longest=k;}
        }
        if (longest2<=edge2) {next.insert(next.end(),tri.begin(),tri.end());continue;}
        const uint32_t a=tri[longest],b=tri[(longest+1)%3],c=tri[(longest+2)%3],m=midpoint(a,b);
        pending.push_back({a,m,c});pending.push_back({m,b,c});
      }
    }
    if (t+3==oldFine) newFine=uint32_t(next.size());
  }
  ib.swap(next);fineStart=newFine;
  return (ib.size()-oldCount)/3;
}
}
