#include "hive_combat.h"
#include "finite_float.h"
#include <limits>

namespace hive {
namespace {
const RoleSpec roles[] = {
  {520, 650, 100, 220, 300, 8, 90, .7f, 2.5f},
  {240, 340, 35, 55, 800, 11, 130, 2.f, 12.f},
  {300, 400, 50, 80, 500, 10, 130, 1.2f, 6.f},
  {170, 250, 25, 35, 2500, 24, 180, 2.5f, 12.f}
};
bool finite(vec3 v) { return floatValidation::finite(v.x) && floatValidation::finite(v.y) && floatValidation::finite(v.z); }
float safePositive(float v, float fallback, float cap) { return floatValidation::finite(v) && v > 0 ? std::min(v, cap) : fallback; }
float terrain(const WorldCallbacks& w, vec3 p) {
  float h = w.terrainHeight ? w.terrainHeight(w.context, p.x, p.z) : 0;
  return floatValidation::finite(h) ? h : 0;
}
float blocker(const WorldCallbacks& w, vec3 a, vec3 b, float radius) {
  float result = 2;
  if (w.sweepFraction) {
    float f = w.sweepFraction(w.context, a, b, radius);
    if (!floatValidation::finite(f) || f < 0) return 0; // Malformed production query fails closed.
    if (f <= 1) result = f;
    if (w.sweepIncludesTerrain) return result;
  }
  // Bounded fallback for ground crossing. Exact scenery/terrain sweeps belong in the callback.
  for (int i = 0; i <= 16; ++i) {
    float t = i / 16.f;
    vec3 p = lerp(a, b, t);
    if (p.y - radius <= terrain(w, p)) {
      float lo = std::max(0.f, (i - 1) / 16.f), hi = t;
      for (int j = 0; j < 8; ++j) { float m = (lo + hi) * .5f; vec3 q = lerp(a,b,m); if(q.y-radius<=terrain(w,q)) hi=m; else lo=m; }
      result = std::min(result, hi); break;
    }
  }
  return result;
}
bool visible(const WorldCallbacks& w, vec3 a, vec3 b) {
  return (!w.lineOfSight || w.lineOfSight(w.context,a,b)) && blocker(w,a,b,0) >= .9999f;
}
float sphere(vec3 a, vec3 b, vec3 center, float radius) {
  vec3 d = b-a, m = a-center;
  float c = dot(m,m)-radius*radius;
  if (c <= 0) return 0;
  float aa = dot(d,d), bb = dot(m,d), disc = bb*bb-aa*c;
  if (aa < 1e-9f || bb >= 0 || disc < 0) return 2;
  float t = (-bb-std::sqrt(disc))/aa;
  return t >= 0 && t <= 1 ? t : 2;
}
// Segment clipping and authored polygon prisms avoid damaging the empty space in a craft's envelope.
float boxSweep(vec3 a,vec3 b,vec3 center,vec3 half,float r) {
  float lo=0,hi=1;vec3 d=b-a;
  for(int i=0;i<3;++i) {
    float e=half[i]+r,x=a[i]-center[i];
    if(e<=0)return 2;
    if(std::abs(d[i])<1e-8f){if(std::abs(x)>e)return 2;}
    else {float p=(-e-x)/d[i],q=(e-x)/d[i];if(p>q)std::swap(p,q);lo=std::max(lo,p);hi=std::min(hi,q);if(lo>hi)return 2;}
  }
  return lo;
}
float cross2(vec2 a,vec2 b){return a.x*b.y-a.y*b.x;}
float dot2(vec2 a,vec2 b){return a.x*b.x+a.y*b.y;}
bool insidePolygon(vec2 p,const vec2* v,int n) {
  bool inside=false;
  for(int i=0,j=n-1;i<n;j=i++) if((v[i].y>p.y)!=(v[j].y>p.y)) {
    if(p.x<(v[j].x-v[i].x)*(p.y-v[i].y)/(v[j].y-v[i].y)+v[i].x)inside=!inside;
  }
  return inside;
}
float circle2Sweep(vec2 a,vec2 d,vec2 center,float r) {
  vec2 m=a-center;float aa=dot2(d,d),bb=dot2(m,d),cc=dot2(m,m)-r*r;
  if(cc<=0)return 0;
  float disc=bb*bb-aa*cc;if(aa<1e-12f||disc<0)return 2;
  float t=(-bb-std::sqrt(disc))/aa;return t>=0&&t<=1?t:2;
}
float edge2Sweep(vec2 a,vec2 d,vec2 u,vec2 v,float r) {
  vec2 e=v-u;float len=length(e);if(len<1e-8f)return circle2Sweep(a,d,u,r);
  e=e*(1/len);vec2 side(-e.y,e.x),m=a-u;
  float hit=boxSweep({dot2(m,e),dot2(m,side),0},{dot2(m+d,e),dot2(m+d,side),0},
                     {len*.5f,0,0},{len*.5f,std::max(r,1e-6f),1},0);
  return std::min(hit,std::min(circle2Sweep(a,d,u,r),circle2Sweep(a,d,v,r)));
}
float polygonSweep(vec3 a,vec3 b,const vec2* v,int n,float low,float high,float radius) {
  float lo=0,hi=1,dy=b.y-a.y;low-=radius;high+=radius;
  if(std::abs(dy)<1e-8f){if(a.y<low||a.y>high)return 2;}
  else {float p=(low-a.y)/dy,q=(high-a.y)/dy;if(p>q)std::swap(p,q);lo=std::max(lo,p);hi=std::min(hi,q);if(lo>hi)return 2;}
  vec3 at=lerp(a,b,lo),end=lerp(a,b,hi);vec2 x(at.x,at.z),d(end.x-at.x,end.z-at.z);
  if(insidePolygon(x,v,n))return lo;
  float best=2;
  for(int i=0;i<n;++i) {
    vec2 u=v[i],z=v[(i+1)%n];
    if(radius>0)best=std::min(best,edge2Sweep(x,d,u,z,radius));
    else {
      vec2 e=z-u;float den=cross2(d,e);
      if(std::abs(den)>1e-9f){float t=cross2(u-x,e)/den,s=cross2(u-x,d)/den;if(t>=0&&t<=1&&s>=0&&s<=1)best=std::min(best,t);}
    }
  }
  return best<=1?lo+(hi-lo)*best:2;
}
vec3 localPoint(const BodyFrame& f,vec3 p){return {dot(p,f.right),dot(p,f.up),dot(p,f.back)};}
float playerHitFraction(const PlayerSnapshot& p,vec3 center,vec3 from,vec3 to,float r) {
  if(p.bodyBoxCount<=0)return sphere(from,to,center,std::max(0.f,p.radius)+r);
  BodyFrame frame=bodyFrame(p.forward,p.up);vec3 a=localPoint(frame,from-center),b=localPoint(frame,to-center);float hit=2;
  for(int i=0;i<std::min(MaxPlayerBoxes,p.bodyBoxCount);++i) {
    const auto& box=p.bodyBoxes[i];if(finite(box.center)&&finite(box.halfExtent))hit=std::min(hit,boxSweep(a,b,box.center,box.halfExtent,r));
  }
  return hit;
}

vec3 limited(vec3 v, float cap) { float n=length(v); return n>cap ? v*(cap/n) : v; }
bool inForwardFireCone(const Actor& actor,vec3 aimDelta) {
  if(actor.type==Type::Bastion)return true; // Belly stores have a separate ballistic release.
  // Fixed nose stations can correct within 20 degrees, never shoot through the
  // airframe at a contact beside or behind it. A stopped craft uses its render frame.
  return length(aimDelta)>1e-5f && dot(-bodyFrame(actor.velocity).back,normalize(aimDelta))>=.9396926f;
}
}
BodyFrame bodyFrame(vec3 forward,vec3 up) {
  if(!finite(forward)||length(forward)<1e-6f)forward={0,0,-1};
  forward=normalize(forward);if(!finite(up)||length(up)<1e-6f)up={0,1,0};
  vec3 right=cross(forward,up);
  if(length(right)<1e-6f)right=cross(forward,vec3(0,0,1));
  if(length(right)<1e-6f)right={1,0,0};
  right=normalize(right);return {right,normalize(cross(right,forward)),-forward};
}
vec3 weaponSocket(const Actor& actor,uint32_t index) {
  vec3 local;
  if(actor.type==Type::Needle)local={index%2?-1.36f:1.36f,-.02f,-3.50f};
  else if(actor.type==Type::Bastion)local={index%2?-2.5f:2.5f,-1.10f,-3.3f+float((index/2)%3)*1.7f};
  else if(actor.type==Type::Archon)local={0,.08f,-8.86f};
  BodyFrame f=bodyFrame(actor.velocity);
  return actor.position+f.right*local.x+f.up*local.y+f.back*local.z;
}
float actorHitFraction(const Actor& actor,vec3 from,vec3 to,float radius) {
  if(!finite(from)||!finite(to)||!finite(actor.position)||!floatValidation::finite(radius)||radius<0)return 2;
  BodyFrame f=bodyFrame(actor.velocity);vec3 a=localPoint(f,from-actor.position),b=localPoint(f,to-actor.position);
  float hit=2;
  auto box=[&](vec3 c,vec3 half,bool mirror=false){hit=std::min(hit,boxSweep(a,b,c,half,radius));if(mirror){c.x=-c.x;hit=std::min(hit,boxSweep(a,b,c,half,radius));}};
  auto poly=[&](const vec2* v,int n,float low,float high,bool mirror=false,vec3 shift=vec3()) {
    vec3 x=a-shift,y=b-shift;hit=std::min(hit,polygonSweep(x,y,v,n,low,high,radius));
    if(mirror){x.x=-x.x;y.x=-y.x;hit=std::min(hit,polygonSweep(x,y,v,n,low,high,radius));}
  };
  if(actor.type==Type::Needle) {
    static const vec2 hull[]={{0.f,-6.6f},{1.18f,-3.7f},{1.48f,.5f},{.84f,4.5f},{0.f,5.3f},{-.84f,4.5f},{-1.48f,.5f},{-1.18f,-3.7f}};
    static const vec2 armor[]={{0.f,-4.7f},{.78f,-2.5f},{.84f,.5f},{.52f,3.6f},{0.f,4.1f},{-.52f,3.6f},{-.84f,.5f},{-.78f,-2.5f}};
    static const vec2 wing[]={{.82f,-2.4f},{1.8f,-2.7f},{5.95f,.8f},{5.65f,2.15f},{4.4f,2.95f},{1.05f,2.15f}};
    poly(hull,8,-.58f,.58f);poly(armor,8,-.24f,.24f,false,{0,.50f,-.5f});poly(wing,6,-.24f,.12f,true);
    box({3.5f,-.12f,1.2f},{.86f,.482f,2.35f},true);box({0,.95f,2.8f},{.13f,.72f,1.65f});box({1.36f,-.02f,-2.3f},{.20f,.20f,1.25f},true);
  }
  else if(actor.type==Type::Bastion) {
    static const vec2 hull[]={{-1.7f,-6.2f},{1.7f,-6.2f},{4.6f,-3.8f},{5.1f,2.2f},{3.3f,5.8f},{-3.3f,5.8f},{-5.1f,2.2f},{-4.6f,-3.8f}};
    static const vec2 shoulder[]={{3.9f,-2.9f},{6.2f,-3.3f},{8.1f,-.9f},{8.0f,2.75f},{6.5f,4.4f},{3.8f,3.4f}};
    poly(hull,8,-.82f,1.10f);poly(shoulder,6,-.60f,.20f,true);
    box({6.25f,-.28f,.7f},{1.32f,.74f,3.35f},true);box({0,-.64f,3.35f},{1.25f,.70f,1.85f});
    for(int k=0;k<3;++k)box({2.5f,-.86f,-3.3f+k*1.7f},{.87f,.28f,.63f},true);
    box({0,1.17f,-4.1f},{.98f,.10f,.30f});box({0,.92f,2.8f},{.45f,.52f,1.4f});
  }
  else if(actor.type==Type::Cantor) {
    static const vec2 keel[]={{-0.92f,-7.40f},{0.92f,-7.40f},{2.48f,-4.30f},{2.65f,1.90f},{1.52f,7.20f},{-1.52f,7.20f},{-2.65f,1.90f},{-2.48f,-4.30f}};
    static const vec2 shoulder[]={{0.0f,-4.85f},{2.75f,-4.18f},{6.82f,-1.85f},{7.00f,0.40f},{5.82f,3.04f},{2.10f,5.45f},{0.0f,4.00f}};
    static const vec2 saddle[]={{-0.70f,-5.56f},{0.70f,-5.56f},{1.82f,-2.94f},{1.88f,2.08f},{1.02f,5.80f},{-1.02f,5.80f},{-1.88f,2.08f},{-1.82f,-2.94f}};
    poly(keel,8,-.88f,.48f);poly(shoulder,7,-.54f,.54f,true);poly(saddle,8,.25f,.97f);
    box({4.92f,-.86f,.25f},{1.04f,.583f,2.86f},true);
    // The authored outward-canted, concave crown silhouette, not its envelope box.
    static const vec2 crown[]={{-3.33f,-.30f},{-2.51f,.61f},{-.85f,2.48f},{.20f,2.73f},{1.06f,1.79f},{3.44f,.13f},{3.61f,-.30f}};
    for(float side:{-1.f,1.f}) {
      auto vane=[&](vec3 p){float x=p.x*side-.66f,y=p.y-.81f;return vec3(p.z-.35f,x*.976296f-y*.216440f,x*.216440f+y*.976296f);};
      hit=std::min(hit,polygonSweep(vane(a),vane(b),crown,7,-.22f,.30f,radius));
    }
  }
  else if(actor.type==Type::Archon) {
    static const vec2 keel[]={{-3.74f,-8.50f},{3.74f,-8.50f},{6.08f,-2.00f},{5.54f,9.32f},{2.36f,18.20f},{-2.36f,18.20f},{-5.54f,9.32f},{-6.08f,-2.00f}};
    static const vec2 fork[]={{2.58f,2.00f},{3.60f,-9.00f},{4.10f,-16.70f},{5.37f,-18.63f},{6.88f,-17.13f},{8.02f,-8.00f},{8.57f,-1.02f},{6.40f,3.20f}};
    static const vec2 shoulder[]={{0.0f,-5.50f},{6.50f,-6.91f},{12.30f,-4.49f},{16.71f,-0.20f},{16.42f,4.00f},{10.62f,8.00f},{5.72f,11.70f},{0.0f,8.00f}};
    static const vec2 deckPlan[]={{-1.76f,-8.00f},{1.76f,-8.00f},{4.49f,-2.52f},{3.66f,8.78f},{1.90f,12.18f},{-1.90f,12.18f},{-3.66f,8.78f},{-4.49f,-2.52f}};
    static const vec2 keepPlan[]={{-0.98f,-8.35f},{0.98f,-8.35f},{3.34f,-2.24f},{3.06f,6.05f},{1.39f,10.75f},{-1.39f,10.75f},{-3.06f,6.05f},{-3.34f,-2.24f}};
    poly(keel,8,-1.52f,1.08f);poly(fork,8,-1.33f,1.59f,true);poly(shoulder,8,-1.23f,1.10f,true);poly(deckPlan,8,.55f,1.79f);
    static const vec2 profile[]={{-8.80f,.83f},{-5.80f,2.37f},{-.80f,4.25f},{4.20f,4.25f},{8.90f,2.45f},{10.80f,.83f}};
    vec3 pa(a.z,a.x,a.y),pb(b.z,b.x,b.y);
    float enter=std::max(polygonSweep(a,b,keepPlan,8,-1.6f,5.6f,radius),polygonSweep(pa,pb,profile,6,-3.7f,3.7f,radius));
    float leave=std::min(1-polygonSweep(b,a,keepPlan,8,-1.6f,5.6f,radius),1-polygonSweep(pb,pa,profile,6,-3.7f,3.7f,radius));
    if(enter<=leave)hit=std::min(hit,enter);
    box({12.12f,-1.62f,1.40f},{1.95f,1.092f,5.18f},true);box({5.80f,-1.45f,-9.30f},{1.28f,.717f,3.45f},true);
    static const vec2 crown[]={{-.74f,.75f},{3.01f,2.02f},{8.03f,6.55f},{9.10f,6.13f},{10.55f,1.40f},{9.10f,.75f}};
    for(float side:{-1.f,1.f})hit=std::min(hit,polygonSweep({a.z,a.x*side-3.70f,a.y},{b.z,b.x*side-3.70f,b.y},crown,6,-.64f,.78f,radius));
    box({0,.08f,-8.62f},{1,.98f,.36f});
  }
  return hit;
}
const RoleSpec& roleSpec(Type type) { int i=int(type); return roles[i >= 0 && i < 4 ? i : 0]; }
float projectileLifetime(Weapon weapon) { return weapon==Weapon::Bomb?15.f:weapon==Weapon::Lance?4.f:2.f; }
uint32_t Combat::random() { rng_^=rng_<<13; rng_^=rng_>>17; rng_^=rng_<<5; return rng_; }
void Combat::reset(uint32_t seed, float wrapSize) {
  actors={}; projectiles={}; events={}; eventCount=0; droppedEvents=0; mission={};
  rng_=seed?seed:1; nextId_=nextProjectileId_=1; time_=accumulator_=0;
  wrap_=floatValidation::finite(wrapSize)&&wrapSize>0 ? wrapSize : 0;
}
vec3 Combat::displacement(vec3 a, vec3 b) const {
  vec3 d=b-a;
  if(wrap_>0) { d.x=std::remainder(d.x,wrap_); d.z=std::remainder(d.z,wrap_); }
  return d;
}
vec3 Combat::canonical(vec3 p) const {
  if(wrap_>0) { p.x=std::remainder(p.x,wrap_); p.z=std::remainder(p.z,wrap_); }
  return p;
}
void Combat::emit(EventType t,uint32_t s,uint32_t target,vec3 p,float v) {
  if(eventCount<MaxEvents) events[eventCount++]={t,s,target,p,v}; else ++droppedEvents;
}
void Combat::clearEvents() { eventCount=0; droppedEvents=0; }
uint32_t Combat::spawn(Type t,vec3 p,vec3 v) {
  if(int(t)>3 || !finite(p) || !finite(v) || nextId_==0) return 0;
  for(auto& a:actors) if(!a.alive) {
    a={}; a.id=nextId_++; a.type=t; a.alive=true; a.position=canonical(p); a.previousPosition=a.position;
    a.velocity=limited(v,roleSpec(t).burst); a.hull=roleSpec(t).hull;
    a.shield=t==Type::Archon ? 400.f : 0.f;
    a.timer=.2f+float(random()%1000)/1000.f;
    emit(EventType::Spawn,a.id,0,a.position,float(int(t))); return a.id;
  }
  return 0;
}
Actor* Combat::find(uint32_t id) { if(id) for(auto& a:actors) if(a.id==id) return &a; return nullptr; }
const Actor* Combat::find(uint32_t id) const { if(id) for(const auto& a:actors) if(a.id==id) return &a; return nullptr; }
int Combat::aliveCount() const { int n=0; for(const auto& a:actors) n+=a.alive; return n; }
void Combat::damage(Actor& a,float amount,uint32_t source) {
  if(!a.alive || !floatValidation::finite(amount) || amount<=0) return;
  amount=std::min(amount,100000.f); a.relayInterrupted=4; a.relayTarget=0;
  float absorbed=std::min(a.shield,amount); a.shield-=absorbed; a.hull=std::max(0.f,a.hull-(amount-absorbed));
  emit(EventType::Hit,source,a.id,a.position,amount);
  if(a.hull<=0) {
    a.alive=false; emit(EventType::Death,source,a.id,a.position);
    for(int i=0;i<mission.config.targetCount;++i) if(mission.config.targetIds[i]==a.id) mission.targetsDestroyed[i]=true;
  }
}
uint32_t Combat::playerShot(vec3 from,vec3 to,float amount,const WorldCallbacks& w,ShotContact* contact) {
  if(contact)*contact={};
  if(!finite(from)||!finite(to)||!floatValidation::finite(amount)||amount<=0) return 0;
  to=from+displacement(from,to); float closest=blocker(w,from,to,0); Actor* hit=nullptr; Projectile* bomb=nullptr;
  for(auto& a:actors) if(a.alive) { Actor proxy=a;proxy.position=from+displacement(from,a.position);float t=actorHitFraction(proxy,from,to); if(t<closest) {closest=t;hit=&a;bomb=nullptr;} }
  for(auto& p:projectiles) if(p.alive && p.team==Team::Hive && p.weapon==Weapon::Bomb) {
    float t=sphere(from,to,from+displacement(from,p.position),std::max(.45f,p.radius)); if(t<closest) {closest=t;hit=nullptr;bomb=&p;}
  }
  if(contact && closest<=1) {
    contact->kind=hit?ShotContactKind::Actor:bomb?ShotContactKind::Bomb:ShotContactKind::World;
    contact->fraction=closest;contact->id=hit?hit->id:bomb?bomb->id:0;
  }
  if(bomb) { bomb->alive=false; emit(EventType::Hit,0,bomb->id,canonical(lerp(from,to,closest)),amount); }
  if(hit) { uint32_t id=hit->id; damage(*hit,amount,0); return id; }
  return 0;
}
void Combat::blast(vec3 center,float radius,float amount,Team team,uint32_t source,const PlayerSnapshot& player,const WorldCallbacks& w) {
  if(!finite(center)||!floatValidation::finite(radius)||!floatValidation::finite(amount)||radius<=0||amount<=0) return;
  radius=std::min(radius,5000.f); amount=std::min(amount,100000.f);
  auto impact=[&](vec3 p,float bound) { vec3 d=displacement(center,p); float distance=std::max(0.f,length(d)-bound); return distance<radius && visible(w,center,center+d) ? amount*(1-distance/radius) : 0.f; };
  if(team==Team::Player) {
    for(auto& a:actors)if(a.alive) {
      vec3 d=displacement(center,a.position);Actor proxy=a;proxy.position=center+d;
      float t=actorHitFraction(proxy,center,center+d);float distance=t<=1?length(d)*t:std::numeric_limits<float>::infinity();
      if(distance<radius && visible(w,center,center+d*t))damage(a,amount*(1-distance/radius),source);
    }
  }
  else {
    float hit=player.alive ? impact(player.position,player.radius) : 0;
    if(player.alive && player.bodyBoxCount>0) {
      vec3 d=displacement(center,player.position);BodyFrame f=bodyFrame(player.forward,player.up);
      vec3 local=localPoint(f,-d);float distance=std::numeric_limits<float>::infinity();
      for(int i=0;i<std::min(MaxPlayerBoxes,player.bodyBoxCount);++i) {
        const auto& box=player.bodyBoxes[i];vec3 q=local-box.center;
        q={std::max(0.f,std::abs(q.x)-box.halfExtent.x),std::max(0.f,std::abs(q.y)-box.halfExtent.y),std::max(0.f,std::abs(q.z)-box.halfExtent.z)};
        distance=std::min(distance,length(q));
      }
      hit=distance<radius&&visible(w,center,center+d)?amount*(1-distance/radius):0;
    }
    if(hit>0) emit(EventType::PlayerDamage,source,0,player.position,hit);
    for(int i=0;i<mission.config.objectiveCount;++i) {
      auto& o=mission.config.objectives[i]; hit=impact(o.position,o.radius);
      if(hit>0 && o.health>0) { float dealt=std::min(o.health,hit); o.health-=dealt; emit(EventType::ObjectiveDamage,source,o.id,o.position,dealt); }
    }
  }
}
void Combat::playerBlast(vec3 center,float radius,float amount,const WorldCallbacks& w) { blast(center,radius,amount,Team::Player,0,{},w); }
void Combat::playerEMP(vec3 center,float radius,float seconds,const WorldCallbacks& w) {
  if(!finite(center)||!floatValidation::finite(radius)||!floatValidation::finite(seconds)||radius<=0||seconds<=0)return;
  radius=std::min(radius,5000.f);seconds=std::min(seconds,30.f);
  for(auto& a:actors)if(a.alive) {
    vec3 d=displacement(center,a.position);
    if(length(d)<=radius && visible(w,center,center+d)) {
      float removed=a.shield;a.shield=0;a.shieldDisrupted=std::max(a.shieldDisrupted,seconds);
      a.relayInterrupted=std::max(a.relayInterrupted,seconds);a.relayTarget=0;
      emit(EventType::Hit,0,a.id,a.position,removed);
    }
  }
}
void Combat::startMission(const MissionConfig& config) {
  mission={}; mission.config=config; auto& c=mission.config;
  c.objectiveCount=std::max(0,std::min(MaxObjectives,c.objectiveCount)); c.waveCount=std::max(0,std::min(MaxWaves,c.waveCount)); c.targetCount=std::max(0,std::min(MaxTargets,c.targetCount));
  c.scanSeconds=safePositive(c.scanSeconds,6,120); c.scanRange=safePositive(c.scanRange,900,10000); c.extractionRadius=safePositive(c.extractionRadius,250,5000);
  if(!finite(c.extraction)) c.extraction={};
  for(int i=0;i<c.objectiveCount;++i) { auto& o=c.objectives[i]; if(!finite(o.position)) o.position={}; o.radius=safePositive(o.radius,60,1000); o.health=safePositive(o.health,100,100000); o.scan=0; o.scanned=false; if(!o.id)o.id=uint32_t(i+1); }
  for(int i=0;i<c.waveCount;++i) { auto& wave=c.waves[i]; wave.count=std::max(0,std::min(MaxActors,wave.count)); wave.at=floatValidation::finite(wave.at)?std::max(0.f,wave.at):0; if(!finite(wave.position))wave.position={}; for(auto& t:wave.types)if(int(t)>3)t=Type::Needle; }
  std::stable_sort(c.waves.begin(),c.waves.begin()+c.waveCount,[](const Wave& a,const Wave& b){return a.at<b.at;});
  mission.status=c.kind==MissionKind::None?MissionStatus::Inactive:MissionStatus::Active;
}
void Combat::step(float dt,const PlayerSnapshot& player,const WorldCallbacks& w,bool paused) {
  if(paused||!floatValidation::finite(dt)||dt<=0||!finite(player.position)||!finite(player.velocity)||!finite(player.forward)) return;
  accumulator_+=std::min(dt,.25f);
  int count=0;
  while(accumulator_+1e-7f>=FixedStep && count<15) {
    accumulator_=std::max(0.f,accumulator_-FixedStep);
    // The game supplies the final frame pose. Each tick needs its own endpoint along
    // that motion, including the retained sub-tick remainder; otherwise catch-up
    // tests the player's last 1/60 s repeatedly and skips the earlier flight path.
    // Only accepted time (at most .25 s plus the prior remainder) is reconstructed,
    // never the discarded portion of a long frame. Orientation is held at the final
    // pose because the snapshot does not supply angular history.
    PlayerSnapshot sample=player;
    sample.position=canonical(player.position-player.velocity*accumulator_);
    tick(sample,w); ++count;
  }
}
void Combat::tick(const PlayerSnapshot& player,const WorldCallbacks& w) {
  time_+=FixedStep;
  // Snapshot IDs ensures newly spawned escorts do not get an extra tick in a reused earlier slot.
  std::array<uint32_t,MaxActors> ids{}; for(int i=0;i<MaxActors;++i) if(actors[i].alive)ids[i]=actors[i].id;
  for(uint32_t id:ids) if(Actor* a=find(id)) if(a->alive){a->previousPosition=a->position;tickActor(*a,player,w);}
  for(auto& p:projectiles) if(p.alive)tickProjectile(p,player,w);
  tickMission(player,w);
}
void Combat::fire(Actor& a,const PlayerSnapshot&) {
  const vec3 muzzle=weaponSocket(a,a.shotsFired);
  if(!inForwardFireCone(a,displacement(muzzle,a.aim)))return;
  for(auto& p:projectiles) if(!p.alive) {
    p={}; p.alive=true; p.id=nextProjectileId_++; p.owner=a.id; p.position=canonical(weaponSocket(a,a.shotsFired++));
    vec3 aim=displacement(p.position,a.aim);
    if(a.type==Type::Bastion) {
      p.weapon=Weapon::Bomb; p.damage=10; p.radius=.45f; p.blastRadius=40;
      float fall=std::sqrt(2*std::max(20.f,aim.y<0?-aim.y:20.f)/G0);
      p.velocity=limited(vec3(aim.x/fall,0,aim.z/fall),340);
    } else {
      p.weapon=a.type==Type::Archon?Weapon::Lance:Weapon::Pulse;
      p.damage=a.type==Type::Archon?25:6;
      p.radius=a.type==Type::Archon?.75f:.18f; p.velocity=normalize(aim)*(a.type==Type::Archon?1200.f:900.f);
    }
    p.life=projectileLifetime(p.weapon);
    emit(EventType::Fire,a.id,p.id,p.position,float(int(p.weapon))); return;
  }
}
void Combat::tickActor(Actor& a,const PlayerSnapshot& player,const WorldCallbacks& w) {
  const auto& s=roleSpec(a.type); float dt=FixedStep;
  a.shieldDisrupted=std::max(0.f,a.shieldDisrupted-dt);
  a.timer-=dt; a.relayInterrupted=std::max(0.f,a.relayInterrupted-dt); a.burstCooldown=std::max(0.f,a.burstCooldown-dt); a.burstRemaining=std::max(0.f,a.burstRemaining-dt);
  vec3 target=a.position+displacement(a.position,player.position);
  int objective=-1;
  if(a.type==Type::Bastion) for(int i=0;i<mission.config.objectiveCount;++i) if(mission.config.objectives[i].health>0) {objective=i;target=a.position+displacement(a.position,mission.config.objectives[i].position);break;}
  if(a.type==Type::Cantor) {
    a.relayTarget=0; Actor* ally=nullptr; float best=700;
    for(auto& b:actors) if(b.alive && b.id!=a.id && b.type!=Type::Cantor && b.shield<100 && b.shieldDisrupted<=0) {
      vec3 delta=displacement(a.position,b.position); float d=length(delta);
      if(d<best && visible(w,a.position,a.position+delta)) { best=d; ally=&b; }
    }
    if(ally) {
      target=a.position+displacement(a.position,ally->position)+vec3(0,120,250);
      if(a.relayInterrupted<=0) { ally->shield=std::min(100.f,ally->shield+8*dt); a.relayTarget=ally->id;
        // Emit once per second; the live relayTarget supplies continuous visual state.
        if(int(time_)!=int(time_-dt))emit(EventType::Relay,a.id,ally->id,a.position,ally->shield);
      }
    }
  }
  float range=length(displacement(a.position,player.position));
  bool armed=player.alive && range<(a.type==Type::Archon?3500.f:1800.f);
  if(a.type==Type::Bastion) {
    // Outside defense, bomb the player's predicted flight path instead of remaining inert.
    armed=objective>=0 ? length(displacement(a.position,target))<1800 : player.alive && range<1800;
    if(objective<0)target+=limited(player.velocity*.75f,400);
  }
  if(a.type==Type::Cantor)armed=false; // Relay is its dedicated weapon; no unannounced hitscan aura.
  const vec3 candidateAim=canonical(target+(a.type==Type::Bastion?vec3():limited(player.velocity*.35f,200)));
  auto aligned=[&](vec3 aim){return inForwardFireCone(a,displacement(weaponSocket(a,a.shotsFired),aim));};
  if((a.phase==Phase::Telegraph || a.phase==Phase::Firing) && !aligned(a.aim)) {
    // A fly-by may carry the locked aim behind the nose during charge/burst.
    // Reposition and telegraph a fresh solution instead of firing backwards.
    a.phase=Phase::Approach;a.timer=.2f;a.shotsRemaining=0;
  }
  if(a.phase==Phase::Approach && a.timer<=0 && armed && aligned(candidateAim) && visible(w,a.position,target)) {
    a.phase=Phase::Telegraph; a.timer=s.charge;
    a.aim=candidateAim;
    emit(EventType::Telegraph,a.id,objective>=0?mission.config.objectives[objective].id:0,a.aim,s.charge);
  } else if(a.phase==Phase::Telegraph && a.timer<=0) {
    a.phase=Phase::Firing; a.shotsRemaining=a.type==Type::Archon?1:3; a.shotTimer=0;
  } else if(a.phase==Phase::Firing) {
    a.shotTimer-=dt;
    if(a.shotTimer<=0) { fire(a,player); --a.shotsRemaining; a.shotTimer=a.type==Type::Bastion?1.f:.15f; }
    if(a.shotsRemaining<=0) {a.phase=Phase::Recover;a.timer=s.recovery;}
  } else if(a.phase==Phase::Recover && a.timer<=0) { a.phase=Phase::Approach;a.timer=0; }
  if(a.type==Type::Archon) {
    // Two threshold waves only; a full actor budget defers the wave until there is space.
    float threshold=a.escortWaves==0?.70f:.35f;
    if(a.escortWaves<2 && a.hull<s.hull*threshold && aliveCount()<=MaxActors-2) {
      spawn(Type::Needle,a.position+vec3(120,40,120));spawn(Type::Needle,a.position+vec3(-120,40,120));++a.escortWaves;
      emit(EventType::Wave,a.id,0,a.position,float(a.escortWaves));
    }
    // Recovery is always vulnerable; command shields never regenerate themselves.
    if(a.phase==Phase::Recover)a.shield=std::max(0.f,a.shield-80*dt);
  }
  if(a.burstCooldown<=0 && a.phase==Phase::Approach && range>1100) {a.burstRemaining=2;a.burstCooldown=10;}
  vec3 desired=target-a.position;
  if(a.phase==Phase::Recover && (a.type==Type::Needle||a.type==Type::Bastion)) desired=length(a.velocity)>1?a.velocity:vec3(0,0,-1);
  if(a.type==Type::Cantor && !a.relayTarget) desired=vec3(-desired.z*.3f,100,desired.x*.3f);
  // Look ahead, climb before terrain, and use a bounded acceleration (never teleport to altitude).
  vec3 ahead=a.position+normalize(a.velocity)*std::max(200.f,length(a.velocity)*2);
  float minimum=std::max(terrain(w,a.position),terrain(w,ahead))+s.clearance;
  float cruiseAltitude=a.type==Type::Bastion?minimum+120:std::max(minimum,target.y);
  desired.y=std::max(desired.y,(cruiseAltitude-a.position.y)*3);
  float speed=a.burstRemaining>0?s.burst:s.cruise;
  vec3 wanted=normalize(desired)*speed, delta=wanted-a.velocity;
  vec3 direction=normalize(a.velocity), longitudinal=direction*dot(delta,direction);
  vec3 acceleration=limited(longitudinal,s.acceleration*dt)+limited(delta-longitudinal,s.turnAcceleration*dt);
  if(length(a.velocity)<1)acceleration=limited(delta,s.acceleration*dt);
  a.velocity=limited(a.velocity+limited(acceleration,std::max(s.acceleration,s.turnAcceleration)*dt),s.burst);
  vec3 end=a.position+a.velocity*dt;
  float collision=blocker(w,a.position,end,s.radius);
  // A low spawn can climb out of terrain overlap without teleporting or becoming permanently stuck.
  if(a.position.y-s.radius<=terrain(w,a.position) && end.y>a.position.y &&
     end.y-terrain(w,end)>a.position.y-terrain(w,a.position)) {
    float obstacle=w.sweepFraction?w.sweepFraction(w.context,a.position,end,s.radius):2;
    if(floatValidation::finite(obstacle)&&obstacle>1)collision=2;
  }
  if(collision<=1) { a.position=canonical(lerp(a.position,end,std::max(0.f,collision-.001f))); a.velocity=vec3(0,std::min(40.f,s.acceleration),0); }
  else a.position=canonical(end);
}
void Combat::tickProjectile(Projectile& p,const PlayerSnapshot& player,const WorldCallbacks& w) {
  if(!finite(p.position)||!finite(p.velocity)||!floatValidation::finite(p.life)||!floatValidation::finite(p.damage)){p.alive=false;return;}
  p.life-=FixedStep; if(p.life<=0){p.alive=false;return;}
  if(p.weapon==Weapon::Bomb)p.velocity.y-=G0*FixedStep;
  vec3 end=p.position+p.velocity*FixedStep; float closest=blocker(w,p.position,end,p.radius);
  Actor* actor=nullptr; int objective=-1; bool playerHit=false;
  if(p.team==Team::Player) {
    for(auto& a:actors) if(a.alive) {Actor proxy=a;proxy.position=p.position+displacement(p.position,a.position);float t=actorHitFraction(proxy,p.position+displacement(a.previousPosition,a.position),end,p.radius);if(t<closest){closest=t;actor=&a;}}
  } else {
    if(player.alive) {float t=playerHitFraction(player,p.position+displacement(p.position,player.position),p.position+player.velocity*FixedStep,end,p.radius);if(t<closest){closest=t;playerHit=true;}}
    for(int i=0;i<mission.config.objectiveCount;++i) {auto& o=mission.config.objectives[i];if(o.health<=0)continue;float t=sphere(p.position,end,p.position+displacement(p.position,o.position),o.radius+p.radius);if(t<closest){closest=t;objective=i;playerHit=false;}}
  }
  if(closest<=1) {
    vec3 hit=canonical(lerp(p.position,end,closest));p.alive=false;
    if(p.blastRadius>0) {
      // A world-contact query starts outside the surface, not on/in the blocker
      // that caused it. Back off along the incoming segment (also for vertical
      // walls), then keep the ground origin clear. LOS still blocks its far side.
      if(!actor && !playerHit && objective<0)hit=canonical(hit-normalize(end-p.position)*.1f);
      hit.y=std::max(hit.y,terrain(w,hit)+.1f);
      blast(hit,p.blastRadius,p.damage,p.team,p.owner,player,w);
    } else if(actor)damage(*actor,p.damage,p.owner);
    else if(playerHit)emit(EventType::PlayerDamage,p.owner,0,hit,p.damage);
    else if(objective>=0) {auto& o=mission.config.objectives[objective];float dealt=std::min(o.health,std::max(0.f,p.damage));o.health-=dealt;emit(EventType::ObjectiveDamage,p.owner,o.id,hit,dealt);}
  } else p.position=canonical(end);
}
void Combat::tickMission(const PlayerSnapshot& player,const WorldCallbacks& w) {
  if(mission.status!=MissionStatus::Active && mission.status!=MissionStatus::Extract)return;
  auto& c=mission.config; mission.elapsed+=FixedStep;
  bool failed=!player.alive;
  if(c.kind==MissionKind::Defense)for(int i=0;i<c.objectiveCount;++i)failed|=c.objectives[i].health<=0;
  if(failed){mission.status=MissionStatus::Failed;emit(EventType::Failure,0,0,player.position);return;}
  if(mission.nextWave<c.waveCount && mission.elapsed>=c.waves[mission.nextWave].at) {
    auto& wave=c.waves[mission.nextWave];
    if(!mission.waveAnnounced){emit(EventType::Wave,0,0,wave.position,float(mission.nextWave+1));mission.waveAnnounced=true;}
    while(mission.waveSpawned<wave.count && aliveCount()<MaxActors) {
      int n=mission.waveSpawned; if(!spawn(wave.types[n],wave.position+vec3(float(n%3)*160,60+float(n/3)*90,float(n%2)*160)))break;
      ++mission.waveSpawned;
    }
    if(mission.waveSpawned==wave.count){++mission.nextWave;mission.waveSpawned=0;mission.waveAnnounced=false;}
  }
  bool done=false;
  if(c.kind==MissionKind::Recon) {
    done=c.objectiveCount>0;
    for(int i=0;i<c.objectiveCount;++i) {
      auto& o=c.objectives[i];vec3 d=displacement(player.position,o.position);float distance=length(d);
      bool facing=distance<1 || dot(normalize(player.forward),normalize(d))>.5f;
      if(!o.scanned && player.scanning && facing && distance<=c.scanRange && visible(w,player.position,player.position+d)) {
        o.scan=std::min(c.scanSeconds,o.scan+FixedStep);
        if(o.scan+1e-5f>=c.scanSeconds){o.scanned=true;emit(EventType::ScanComplete,0,o.id,o.position);}
      }
      done&=o.scanned;
    }
  } else if(c.kind==MissionKind::Defense || c.kind==MissionKind::Practice) {
    done=mission.nextWave==c.waveCount && aliveCount()==0;
    for(const auto& p:projectiles)if(p.alive && p.team==Team::Hive)done=false;
    if(c.kind==MissionKind::Defense && c.objectiveCount==0)done=false;
  } else if(c.kind==MissionKind::Strike) {
    done=c.targetCount>0;for(int i=0;i<c.targetCount;++i)done&=mission.targetsDestroyed[i];
    if(done && mission.status==MissionStatus::Active)mission.status=MissionStatus::Extract;
    done=done && length(displacement(player.position,c.extraction))<=c.extractionRadius;
  }
  if(done && (c.kind==MissionKind::Recon || c.kind==MissionKind::Defense)) {
    mission.status=MissionStatus::Extract;
    done=length(displacement(player.position,c.extraction))<=c.extractionRadius;
  }
  if(done){mission.status=MissionStatus::Success;emit(EventType::Success,0,0,player.position);}
}
void Combat::shiftOrigin(vec3 offset) {
  if(!finite(offset))return;
  for(auto& a:actors){a.position+=offset;a.aim+=offset;a.previousPosition+=offset;}
  for(auto& p:projectiles)p.position+=offset;
  for(int i=0;i<eventCount;++i)events[i].position+=offset;
  for(auto& o:mission.config.objectives)o.position+=offset;
  for(auto& wave:mission.config.waves)wave.position+=offset;
  mission.config.extraction+=offset;
}
} // namespace hive
