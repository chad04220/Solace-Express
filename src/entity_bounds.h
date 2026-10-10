// Conservative close-instance bounds derived from the authored mesh, including
// the vertex shader's foliage deformation, wheel roll, and windsock animation.
#pragma once
#include "entity_mesh.h"

struct EntLocalBounds {
  vec3 lo{1e9f},hi{-1e9f};
  float radiusXZ=0,frondV=0,sockRadius=0;
  bool valid=false;
};
struct EntWorldBounds {vec3 lo,hi;};
inline void entBoundsPoint(EntLocalBounds& b,vec3 p) {
  b.valid=true;
  b.lo={std::min(b.lo.x,p.x),std::min(b.lo.y,p.y),std::min(b.lo.z,p.z)};
  b.hi={std::max(b.hi.x,p.x),std::max(b.hi.y,p.y),std::max(b.hi.z,p.z)};
  b.radiusXZ=std::max(b.radiusXZ,hypotf(p.x,p.z));
}
inline void entBuildLocalBounds(const std::vector<EVert>& vertices,const EntMeshRange ranges[EK_COUNT],EntLocalBounds out[EK_COUNT],bool allLods=false) {
  for(int kind=0;kind<EK_COUNT;++kind) {
    out[kind]=EntLocalBounds{};auto&b=out[kind];
    // Union both close-fade participants; a single decision must keep or reject
    // both geometries. No nominal metadata is substituted for authored extents.
    for(int lod=0;lod<ENT_LODS;++lod)if(allLods||lod==0||lod==3)for(int i=ranges[kind].first[lod];i<ranges[kind].first[lod]+ranges[kind].count[lod];++i) {
      const auto&v=vertices[i];entBoundsPoint(b,{v.px,v.py,v.pz});
      int part=int(v.part+.5f);
      if(part>=P_WHEEL0&&part<=P_WHEEL5) {
        float radius=hypotf(v.py-v.u,v.pz-v.v);
        entBoundsPoint(b,{v.px,v.u-radius,v.v-radius});entBoundsPoint(b,{v.px,v.u+radius,v.v+radius});
      }
      if(part==P_FROND)b.frondV=std::max(b.frondV,fabsf(v.v));
      if(kind==EK_WINDSOCK&&part==P_SOCK)b.sockRadius=std::max(b.sockRadius,length(vec3(v.px,v.py-6.f,v.pz)));
    }
  }
}
inline EntWorldBounds entInstanceBounds(const EntLocalBounds&b,int kind,const Ent&e) {
  const float sx=fabsf(e.sx),sy=fabsf(e.sy),sz=fabsf(e.sz);
  float cy=cosf(e.yaw),sn=sinf(e.yaw);
  vec3 lo=b.lo,hi=b.hi;
  float y0=std::min(lo.y*e.sy,hi.y*e.sy),y1=std::max(lo.y*e.sy,hi.y*e.sy);
  EntWorldBounds out;
  if(entClass(kind)==EC_TREE) {
    const float divisor=kind==EK_BUSH?1.8f:kind==EK_PALM?10.f:12.f;
    const float t=clampf(std::max(hi.y,0.f)/divisor,0.f,1.6f);
    // Arbitrary height-dependent twist preserves radius. Radial warp contributes
    // <=21%; lean <=0.45*t^2; the two wind axes each contribute <=0.06*h^2*sy.
    float radial=b.radiusXZ*1.21f+(kind==EK_BUSH?0.f:.45f*t*t);
    float h=std::max(hi.y,0.f)/14.f;
    float radius=radial*std::max(sx,sz)+1.414214f*.06f*h*h*sy+.08f*b.frondV;
    out={{e.x-radius,e.y+y0,e.z-radius},{e.x+radius,e.y+y1,e.z+radius}};
  } else {
    vec3 centre=(lo+hi)*.5f,half=(hi-lo)*.5f;
    centre={centre.x*e.sx,centre.y*e.sy,centre.z*e.sz};half={half.x*sx,half.y*sy,half.z*sz};
    vec3 world(e.x+cy*centre.x+sn*centre.z,e.y+centre.y,e.z-sn*centre.x+cy*centre.z);
    vec3 extent(fabsf(cy)*half.x+fabsf(sn)*half.z,half.y,fabsf(sn)*half.x+fabsf(cy)*half.z);
    out={world-extent,world+extent};
    if(kind==EK_WINDSOCK&&b.sockRadius>0) {
      // The sock's shader bypasses instance yaw/scale except for the mast-top
      // height. Its orthonormal animated frame preserves this local radius.
      vec3 centre(e.x,e.y+6.f*e.sy,e.z),r(b.sockRadius);
      out.lo={std::min(out.lo.x,centre.x-r.x),std::min(out.lo.y,centre.y-r.y),std::min(out.lo.z,centre.z-r.z)};
      out.hi={std::max(out.hi.x,centre.x+r.x),std::max(out.hi.y,centre.y+r.y),std::max(out.hi.z,centre.z+r.z)};
    }
  }
  // Roundoff at 40 km world coordinates and subpixel projection jitter.
  out.lo-=vec3(.25f);out.hi+=vec3(.25f);
  return out;
}
inline bool entBoundsVisible(const EntWorldBounds&b,const float planes[6][4]) {
  for(int i=0;i<6;++i) {
    const float*p=planes[i];
    float x=p[0]>=0?b.hi.x:b.lo.x,y=p[1]>=0?b.hi.y:b.lo.y,z=p[2]>=0?b.hi.z:b.lo.z;
    if(p[0]*x+p[1]*y+p[2]*z+p[3]<0)return false;
  }return true;
}


// Cached by a monotonic generation identity, never by address or vector size.
// Worker replacement, level upgrades, trimming, clear/regeneration and reused
// allocation addresses cannot retain an obsolete bound. Camera/wind changes do
// not rebuild: each instance bound already envelopes every animation phase.
struct EntChunkBoundsCache {
  uint64_t revision=0;
  EntWorldBounds bounds{};
  bool valid=false;
  unsigned rebuilds=0;
  const EntWorldBounds& get(const Scenery::Chunk& chunk,const EntLocalBounds models[EK_COUNT]) {
    if(revision!=chunk.revision||chunk.revision==0) {
      valid=false;++rebuilds;
      for(int kind=0;kind<EK_COUNT;++kind)for(uint32_t i=chunk.off[kind];i<chunk.off[kind+1];++i) {
        if(!models[kind].valid)continue;
        auto b=entInstanceBounds(models[kind],kind,chunk.ents[i]);
        if(!valid){bounds=b;valid=true;}
        else {
          bounds.lo={std::min(bounds.lo.x,b.lo.x),std::min(bounds.lo.y,b.lo.y),std::min(bounds.lo.z,b.lo.z)};
          bounds.hi={std::max(bounds.hi.x,b.hi.x),std::max(bounds.hi.y,b.hi.y),std::max(bounds.hi.z,b.hi.z)};
        }
      }
      revision=chunk.revision;
    }
    return bounds;
  }
};
