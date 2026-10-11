// Geometry for every active damaging projectile, independent of finite beam/light effect slots.
#pragma once
#include "renderer.h"
inline void buildHiveOrdnanceSprites(const FrameParams& fp,int H,std::vector<SpriteVert>& ordnanceBody,std::vector<SpriteVert>& ordnanceGlow){
  ordnanceBody.clear(); ordnanceGlow.clear();
  ordnanceBody.reserve(kMaxHiveOrdnance*6); ordnanceGlow.reserve(kMaxHiveOrdnance*18);
  auto quad=[&](std::vector<SpriteVert>& out,vec3 a,vec3 b,vec3 side,vec3 color,float alpha,int kind){
    const vec3 pos[4]={a-side,b-side,b+side,a+side};
    const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i:{0,1,2,0,2,3}){SpriteVert v{};v.x=pos[i].x;v.y=pos[i].y;v.z=pos[i].z;v.u=uv[i][0];v.v=uv[i][1];v.r=color.x;v.g=color.y;v.b=color.z;v.a=alpha;v.kind=float(kind);v.soft=.025f;out.push_back(v);}
  };
  for(int k=0;k<std::clamp(fp.hiveOrdnanceN,0,kMaxHiveOrdnance);k++){
    const auto& o=fp.hiveOrdnance[k];
    const vec3 delta=o.head-o.tail,centre=(o.head+o.tail)*.5f;
    const float distance=length(centre-fp.camPos);
    const float radius=std::max(o.radius, distance*tanf(fp.fovY*.5f)/std::max(H,1)*1.3f);
    vec3 direction=length(delta)>.001f?normalize(delta):fp.camRight;
    vec3 side=cross(direction,centre-fp.camPos);
    side=length(side)>.001f?normalize(side):fp.camUp;
    if(o.kind==0){
      vec3 tail=o.tail;if(length(delta)<radius*2)tail=o.head-direction*(radius*2);
      quad(ordnanceGlow,tail,o.head,side*(radius*2),o.color,o.intensity,SPR_SPARK);
      quad(ordnanceGlow,tail,o.head,side*(radius*.55f),vec3(1.f),o.intensity,SPR_SPARK);
      // A shot approaching the camera must not disappear as its ribbon becomes edge-on.
      // Keep the physical projectile head visible independently of projected trail length.
      quad(ordnanceGlow,o.head-fp.camRight*radius,o.head+fp.camRight*radius,fp.camUp*radius,o.color,o.intensity,SPR_GLOW);
    }else{
      // An opaque body remains visible even for inert/EMP rounds. Glow is optional.
      vec3 p=o.head;quad(ordnanceBody,p-fp.camRight*radius,p+fp.camRight*radius,fp.camUp*radius,o.kind==1?vec3(.18f,.20f,.23f):o.color,.98f,10);
      if(o.kind!=1)quad(ordnanceGlow,p-fp.camRight*radius*1.8f,p+fp.camRight*radius*1.8f,fp.camUp*radius*1.8f,o.color,o.intensity*(o.kind==2?.35f:1.f),SPR_GLOW);
    }
  }
}
