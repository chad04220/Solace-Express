// Prepared dynamic draw packet; existing airport furniture remains stationary.
#ifndef SOLACE_PREPARED_GROUND_VEHICLE_H
#define SOLACE_PREPARED_GROUND_VEHICLE_H
#include "entities.h"
#include "wheel_motion.h"
#include <array>
#include <cstring>

struct GroundWheel { vec3 center; float radius; };
struct GroundWheelLayout { std::array<GroundWheel, 6> wheel{}; int count = 0; };
inline GroundWheelLayout groundWheelLayout(int kind) {
  GroundWheelLayout l;
  auto add = [&](float x, float y, float z, float r) { l.wheel[l.count++] = {vec3(x,y,z),r}; };
  if (kind == EK_GA_PLANE) { add(-1.25f,.27f,1.f,.27f); add(1.25f,.27f,1.f,.27f); add(0,.22f,3.45f,.22f); }
  if (kind == EK_AIRLINER) {
    for (int s : {-1,1}) for (int w : {-1,1}) add(s*3.8f+w*.4f,.57f,-1.2f,.57f);
    for (int w : {-1,1}) add(w*.22f,.38f,14.6f,.38f);
  }
  if (kind == EK_CAR) for (int i=0;i<4;++i) add(i&1?.8f:-.8f,.31f,i&2?1.35f:-1.35f,.31f);
  if (kind == EK_TRUCK) for (int i=0;i<6;++i) add(i&1?1.f:-1.f,.48f,i<2?3.4f:i<4?-2.4f:-3.6f,.48f);
  return l;
}
struct GroundVehicleVisual { int kind = EK_CAR; Ent entity{}; float angle[6] = {}; };
inline bool validGroundVehicle(const GroundVehicleVisual& v) {
  const Ent& e=v.entity;
  return groundWheelLayout(v.kind).count>0 && e.sx>0 && e.sy>0 && e.sz>0 &&
    std::isfinite(e.x) && std::isfinite(e.y) && std::isfinite(e.z) && std::isfinite(e.yaw) &&
    std::isfinite(e.sx) && std::isfinite(e.sy) && std::isfinite(e.sz);
}
struct GroundVehicleMotion {
  std::array<WheelMotion,6> wheels{};
  void reset() { for (auto& w:wheels) w.reset(); }
  // A future ground driver calls this once per SIMULATION tick, using actual velocity and yaw rate.
  // Camera feeds/shadow passes only consume visual(); they never advance animation.
  void step(int kind,const Ent& e,vec3 velocity,float yawRate,float dt) {
    const auto l=groundWheelLayout(kind);
    const float c=cosf(e.yaw),s=sinf(e.yaw);
    const vec3 forward(s,0,c);
    for(int i=0;i<l.count;++i) {
      const vec3 p=l.wheel[i].center;
      vec3 local(p.x*e.sx,p.y*e.sy,p.z*e.sz);
      vec3 v=cross(vec3(0,yawRate,0),local);
      vec3 world(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);
      wheels[i].step(dt,dot(velocity+world,forward),true);
    }
  }
  GroundVehicleVisual visual(int kind,const Ent& e) const {
    GroundVehicleVisual v; v.kind=kind; v.entity=e;
    const auto l=groundWheelLayout(kind);
    // Roll radius scales with the ground-plane Z extent. Non-uniform Y/Z makes an elliptical tyre;
    // game furniture uses circular tyres. The prepared driver should retain sy == sz.
    for(int i=0;i<l.count;++i) v.angle[i]=wheels[i].angle(l.wheel[i].radius*e.sz,1.f);
    return v;
  }
};
inline uint64_t groundVehicleShadowKey(const std::vector<GroundVehicleVisual>& v,vec3 center,float radius,float reach) {
  uint64_t h=1469598103934665603ULL;
  auto add=[&](uint32_t x){h=(h^x)*1099511628211ULL;};
  auto f=[&](float x){uint32_t u;std::memcpy(&u,&x,sizeof(u));add(u);};
  for(const auto& p:v) {
    if(!validGroundVehicle(p)) continue;
    const auto& e=p.entity;const auto& k=kEntInfo[p.kind];
    const float pad=std::max(k.hx*e.sx,k.hz*e.sz)+k.h*e.sy*reach+60.f;
    if(std::fabs(e.x-center.x)>radius+pad || std::fabs(e.z-center.z)>radius+pad) continue;
    add(uint32_t(p.kind));f(e.x);f(e.y);f(e.z);f(e.yaw);f(e.sx);f(e.sy);f(e.sz);
    for(float a:p.angle) f(a);
  }
  return h;
}

#endif
