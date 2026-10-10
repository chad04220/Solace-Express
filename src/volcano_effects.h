// Bounded, deterministic Mount Kaleo scenery effects. Visual only: no terrain/flight changes.
#pragma once
#include "renderer.h"
#include <algorithm>
#include <cmath>

namespace volcano {
constexpr float x = 25000.f, z = -9000.f;
constexpr int maxSmoke = 64, maxEmbers = 40;
inline float unit(int i, int salt) { return hash2i(i * 37 + salt, i * 13 - salt); }
inline float activity(float time) { return .65f + .35f * (.5f + .5f * sinf(time * .31f)) * (.5f + .5f * sinf(time * .73f)); }
inline vec3 vent(const World& world) { return vec3(x, world.height(x, z) + 14.f, z); }
inline void billboard(std::vector<SpriteVert>& out, vec3 p, float size, vec3 c, float alpha, int kind, float soft) {
  SpriteVert q[4]={{p.x,p.y,p.z,0,0,c.x,c.y,c.z,alpha,(float)kind,soft,size},
    {p.x,p.y,p.z,1,0,c.x,c.y,c.z,alpha,(float)kind,soft,size},
    {p.x,p.y,p.z,1,1,c.x,c.y,c.z,alpha,(float)kind,soft,size},
    {p.x,p.y,p.z,0,1,c.x,c.y,c.z,alpha,(float)kind,soft,size}};
  for(int j : {0,1,2,0,2,3}) out.push_back(q[j]);
}
// Called by the live game and review harness. Work and output have hard bounds independent of elapsed time.
inline void append(FrameParams& fp, int quality, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add) {
  if(fp.hangarPreview || g_world.hm.empty() || !std::isfinite(fp.time)) return;
  const vec3 base=vent(g_world); const float distance=length(fp.camPos-base);
  if(distance>=24000.f) return;
  const float visible=1.f-smoothstepf(18000.f,24000.f,distance), t=std::max(0.f,fp.time);
  const int smokeCount=quality<=0?24:quality==1?40:maxSmoke;
  struct Puff { vec3 p; float size,alpha,distance; };
  std::vector<Puff> puffs; puffs.reserve(smokeCount);
  for(int i=0;i<smokeCount;++i) {
    const float life=38.f+unit(i,9)*16.f, age=fmodf(t+unit(i,61)*life,life), f=age/life;
    const float a=unit(i,17)*6.2831853f, r=20.f+unit(i,24)*70.f;
    const vec3 plume=vec3(cosf(a)*r,0,sinf(a)*r)+
      vec3(fp.wind.x,0,fp.wind.z)*(age*(.35f+.65f*f))+
      vec3(sinf(age*.21f+a)*age*.8f,age*(9.f+unit(i,33)*4.f),cosf(age*.17f+a)*age*.7f);
    const float fade=smoothstepf(0.f,.08f,f)*(1.f-smoothstepf(.68f,1.f,f));
    const vec3 p=base+plume;
    puffs.push_back({p,22.f+age*(1.35f+unit(i,52)*.6f),visible*fade*.32f,length(p-fp.camPos)});
  }
  std::sort(puffs.begin(),puffs.end(),[](const Puff&a,const Puff&b){return a.distance>b.distance;});
  for(const auto&p:puffs) billboard(alpha,p.p,p.size,vec3(.22f,.205f,.19f),p.alpha,SPR_SMOKE,18.f);
  // Intermittent Strombolian spatter, ballistic rather than a permanent fountain. It cools and falls back into the crater.
  if(distance<5500.f) {
    const int n=quality<=0?12:quality==1?24:maxEmbers;
    const float nearFade=1.f-smoothstepf(3500.f,5500.f,distance);
    for(int i=0;i<n;++i) {
      const float period=12.f+unit(i,3)*5.f, age=fmodf(t+unit(i,75)*period,period);
      const float life=3.1f+unit(i,8)*1.7f;
      if(age>life) continue;
      const float a=unit(i,4)*6.2831853f, radial=8.f+unit(i,5)*18.f;
      vec3 p=base+vec3(cosf(a)*radial*age,(32.f+unit(i,6)*20.f)*age-4.905f*age*age,sinf(a)*radial*age)+vec3(fp.wind.x,0,fp.wind.z)*age*.22f;
      if(p.y<g_world.height(p.x,p.z)+1.f) continue;
      const float f=age/life, fade=smoothstepf(0.f,.08f,f)*(1.f-f)*nearFade;
      billboard(add,p,1.1f+unit(i,7)*2.2f,lerp(vec3(12.f,5.f,.7f),vec3(3.f,.15f,.01f),f),fade,SPR_GLOW,2.f);
    }
  }
  if(distance<2200.f && fp.plN<12) {
    auto& light=fp.pl[fp.plN++]; light.pos=base+vec3(0,18,0); light.radius=70.f; // physical emitter radius, not influence distance
    light.col=vec3(18000.f,3500.f,400.f)*(activity(t)*(1.f-smoothstepf(1500.f,2200.f,distance))); light.cosCut=-2.f; light.dir=vec3(0,1,0); light.shadow=0;
  }
}
}
