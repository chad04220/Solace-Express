// Deterministic structural checks for the close botanical / geological meshes.
// Geometry counts are explicit CPU work budgets, never a hardware FPS claim.
#include "entity_mesh.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <map>
#include <numeric>
#include <tuple>

namespace {
int failures=0,checks=0;
void check(bool yes,const char* message,int kind=-1,int lod=-1) {
  ++checks;if(!yes){++failures;std::printf("FAIL %s kind=%d lod=%d\n",message,kind,lod);}
}
struct Bounds {vec3 lo{1e9f},hi{-1e9f};};
Bounds bounds(const std::vector<EVert>& mesh,EntMeshRange range,int lod) {
  Bounds b;for(int i=range.first[lod];i<range.first[lod]+range.count[lod];++i){const auto&v=mesh[i];
    b.lo={std::min(b.lo.x,v.px),std::min(b.lo.y,v.py),std::min(b.lo.z,v.pz)};
    b.hi={std::max(b.hi.x,v.px),std::max(b.hi.y,v.py),std::max(b.hi.z,v.pz)};
  }return b;
}
int partCount(const std::vector<EVert>& mesh,EntMeshRange range,int lod,int part) {
  int count=0;for(int i=range.first[lod];i<range.first[lod]+range.count[lod];++i)count+=int(mesh[i].part+.5f)==part;return count;
}

// Mesh components use shared quantized vertices, then attach through geometric
// intersections of their wood surfaces. Open tube rims are capped only for this
// test so a twig beginning inside the end of a limb counts as physically joined.
// This catches visually floating branch hierarchies, not just a tall trunk bbox.
struct WoodTriangle {vec3 p[3];Bounds b;};
struct WoodComponent {std::vector<WoodTriangle> triangles;Bounds b;};
WoodTriangle woodTriangle(vec3 a,vec3 b,vec3 c) {
  WoodTriangle t{{a,b,c},{}};
  for(vec3 p:{a,b,c}) {t.b.lo={std::min(t.b.lo.x,p.x),std::min(t.b.lo.y,p.y),std::min(t.b.lo.z,p.z)};t.b.hi={std::max(t.b.hi.x,p.x),std::max(t.b.hi.y,p.y),std::max(t.b.hi.z,p.z)};}
  return t;
}
bool touches(Bounds a,Bounds b) {
  constexpr float e=.0002f;
  return a.lo.x<=b.hi.x+e&&a.hi.x+e>=b.lo.x&&a.lo.y<=b.hi.y+e&&a.hi.y+e>=b.lo.y&&a.lo.z<=b.hi.z+e&&a.hi.z+e>=b.lo.z;
}
bool pierces(vec3 a,vec3 b,const WoodTriangle&t) {
  vec3 d=b-a,e0=t.p[1]-t.p[0],e1=t.p[2]-t.p[0],h=cross(d,e1);float det=dot(e0,h);
  if(fabsf(det)<1e-11f)return false;
  vec3 q=a-t.p[0];float u=dot(q,h)/det;if(u<-.0001f||u>1.0001f)return false;
  vec3 v=cross(q,e0);float w=dot(d,v)/det;if(w<-.0001f||u+w>1.0001f)return false;
  float f=dot(e1,v)/det;return f>=-.0001f&&f<=1.0001f;
}
bool intersects(const WoodComponent&a,const WoodComponent&b) {
  if(!touches(a.b,b.b))return false;
  for(const auto&t:a.triangles)for(const auto&u:b.triangles)if(touches(t.b,u.b))
    for(int j=0;j<3;++j)if(pierces(t.p[j],t.p[(j+1)%3],u)||pierces(u.p[j],u.p[(j+1)%3],t))return true;
  return false;
}
void woodConnected(const std::vector<EVert>&mesh,EntMeshRange range,int kind) {
  using Key=std::tuple<int,int,int>;
  std::map<Key,int> vertices;std::vector<vec3> points;std::vector<int> parent;std::vector<std::array<int,3>> triangles;
  auto root=[&](int x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
  auto unite=[&](int a,int b){a=root(a);b=root(b);if(a!=b)parent[b]=a;};
  auto vertex=[&](const EVert&v){Key key{int(lroundf(v.px*100000)),int(lroundf(v.py*100000)),int(lroundf(v.pz*100000))};auto it=vertices.find(key);if(it!=vertices.end())return it->second;int id=int(points.size());vertices[key]=id;points.push_back({v.px,v.py,v.pz});parent.push_back(id);return id;};
  for(int i=range.first[3];i<range.first[3]+range.count[3];i+=3)if(mesh[i].part==P_BARK){std::array<int,3> t{vertex(mesh[i]),vertex(mesh[i+1]),vertex(mesh[i+2])};unite(t[0],t[1]);unite(t[0],t[2]);triangles.push_back(t);}
  std::map<int,std::vector<std::array<int,3>>> grouped;
  for(auto t:triangles)grouped[root(t[0])].push_back(t);
  std::vector<WoodComponent> wood;
  for(const auto&g:grouped) {
    WoodComponent c;std::map<std::pair<int,int>,int> edges;
    for(auto t:g.second) {
      c.triangles.push_back(woodTriangle(points[t[0]],points[t[1]],points[t[2]]));
      for(int j=0;j<3;++j){auto key=std::minmax(t[j],t[(j+1)%3]);++edges[key];}
    }
    std::map<int,std::vector<int>> boundary;
    for(const auto&e:edges)if(e.second==1){boundary[e.first.first].push_back(e.first.second);boundary[e.first.second].push_back(e.first.first);}
    std::map<int,bool> seen;
    for(const auto&v:boundary)if(!seen[v.first]) {
      std::vector<int> todo{v.first},loop;seen[v.first]=true;
      while(!todo.empty()){int q=todo.back();todo.pop_back();loop.push_back(q);for(int n:boundary[q])if(!seen[n]){seen[n]=true;todo.push_back(n);}}
      vec3 centre(0);for(int q:loop)centre+=points[q];centre=centre/float(loop.size());
      for(int q:loop)for(int n:boundary[q])if(q<n)c.triangles.push_back(woodTriangle(points[q],points[n],centre));
    }
    for(const auto&t:c.triangles){c.b.lo={std::min(c.b.lo.x,t.b.lo.x),std::min(c.b.lo.y,t.b.lo.y),std::min(c.b.lo.z,t.b.lo.z)};c.b.hi={std::max(c.b.hi.x,t.b.hi.x),std::max(c.b.hi.y,t.b.hi.y),std::max(c.b.hi.z,t.b.hi.z)};}
    wood.push_back(std::move(c));
  }
  std::vector<bool> supported(wood.size());std::vector<int> queue;
  for(size_t i=0;i<wood.size();++i)if(wood[i].b.lo.y<=.001f){supported[i]=true;queue.push_back(int(i));}
  for(size_t i=0;i<queue.size();++i)for(size_t j=0;j<wood.size();++j)if(!supported[j]&&intersects(wood[queue[i]],wood[j])){supported[j]=true;queue.push_back(int(j));}
  for(size_t i=0;i<wood.size();++i){
    if(!supported[i])std::printf("unsupported wood %s component %zu bounds (%.3f %.3f %.3f) (%.3f %.3f %.3f) triangles %zu\n",kEntInfo[kind].name,i,wood[i].b.lo.x,wood[i].b.lo.y,wood[i].b.lo.z,wood[i].b.hi.x,wood[i].b.hi.y,wood[i].b.hi.z,wood[i].triangles.size());
    check(supported[i],"every close woody component physically connected to ground",kind,3);
  }
  std::printf("wood connectivity %s: %zu / %zu supported components\n",kEntInfo[kind].name,queue.size(),wood.size());
}

}
int main() {
  static_assert(ENT_LODS==4,"The botanical close-detail slot is index 3");
  static_assert(sizeof(EVert)==40&&sizeof(Ent)==32,"Preserve vertex/instance ABI");
  std::vector<EVert> mesh,again;EntMeshRange ranges[EK_COUNT],repeat[EK_COUNT];
  buildEntityMeshes(mesh,ranges);buildEntityMeshes(again,repeat);
  check(mesh.size()==again.size()&&!std::memcmp(mesh.data(),again.data(),mesh.size()*sizeof(EVert)),"deterministic vertices");
  check(!std::memcmp(ranges,repeat,sizeof ranges),"deterministic ranges");
  const int budgets[13][4]={
    {1638,204,36,15534},{1998,234,36,20430},{2664,852,198,7044},
    {2322,468,78,16080},{2340,588,78,12048},{1206,264,108,7140},{840,240,60,5904},
    {960,240,60,3840},{960,240,60,3840},{960,240,60,3840},
    {1680,300,180,6720},{1638,465,126,8820},{2052,618,135,12024}
  };
  for(int kind=EK_FIR;kind<=EK_SEASTACK;++kind)for(int lod=0;lod<ENT_LODS;++lod) {
    const auto&r=ranges[kind];
    check(r.count[lod]>0&&r.count[lod]%3==0&&r.count[lod]<=budgets[kind][lod],"species-specific vertex ceiling",kind,lod);
    check(r.count[2]<r.count[1]&&r.count[1]<r.count[0]&&r.count[0]<r.count[3],"strict four-tier detail ordering",kind,lod);
    for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i) {
      const auto&v=mesh[i];
      for(float f:{v.px,v.py,v.pz,v.nx,v.ny,v.nz,v.part,v.ao,v.u,v.v})check(std::isfinite(f),"finite vertex",kind,lod);
      check(fabsf(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz-1.f)<.001f,"unit normal",kind,lod);
      check(v.ao>=0&&v.ao<=1,"bounded ambient occlusion",kind,lod);
      if((i-r.first[lod])%3==0) {
        const auto&b=mesh[i+1];const auto&c=mesh[i+2];
        vec3 area=cross(vec3(b.px-v.px,b.py-v.py,b.pz-v.pz),vec3(c.px-v.px,c.py-v.py,c.pz-v.pz));
        check(dot(area,area)>1e-12f,"nondegenerate triangle",kind,lod);
      }
    }
    const auto b=bounds(mesh,r,lod);
    check(b.lo.y>=-2.f&&b.hi.y<=kEntInfo[kind].h*1.1f+.05f,"nominal vertical envelope",kind,lod);
    if(lod==3||lod==0)check(b.lo.y<=0.f,"near geometry planted",kind,lod);
    if(kind<=EK_BUSH&&lod==3) {
      check(partCount(mesh,r,lod,P_LEAF)==0&&partCount(mesh,r,lod,P_NEEDLE)==0,"no opaque close crown pillows",kind,lod);
      check(partCount(mesh,r,lod,P_BARK)>100,"actual woody support",kind,lod);
      // Each quad is a spray. Its leaves/needles are small enough to resolve as
      // foliage at a walking-distance camera, rather than metre-wide paddles.
      for(int i=r.first[lod];i<r.first[lod]+r.count[lod];i+=3)if(mesh[i].part==P_LEAFCARD) {
        const auto&a=mesh[i];const auto&b=mesh[i+1];const auto&c=mesh[i+2];
        float longest=std::max({length(vec3(a.px-b.px,a.py-b.py,a.pz-b.pz)),length(vec3(a.px-c.px,a.py-c.py,a.pz-c.pz)),length(vec3(b.px-c.px,b.py-c.py,b.pz-c.pz))});
        check(longest<1.6f,"fine close spray size",kind,lod);
      }
      auto near=bounds(mesh,r,0);
      check(b.hi.y>=near.hi.y*.94f&&b.hi.y<=near.hi.y*1.09f,"close/near crown height continuity",kind,lod);
    }
    if(kind>=EK_BOULDER)check(partCount(mesh,r,lod,P_ROCK)==r.count[lod],"geology material identity",kind,lod);
    if(kind==EK_PALM&&lod==3) {
      int leaflets=0;
      for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i)if(mesh[i].part==P_FROND){++leaflets;check(mesh[i].v<0.f&&mesh[i].v>=-1.f,"physical leaflet bypass tag",kind,lod);}
      check(leaflets>=3000,"individually modelled palm leaflets",kind,lod);
    }
    if((kind==EK_FIR||kind==EK_SPRUCE)&&lod!=2) {
      float stemTop=-1,stemBottom=1e9;
      for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i)if(mesh[i].part==P_BARK){stemTop=std::max(stemTop,mesh[i].py);stemBottom=std::min(stemBottom,mesh[i].py);}
      check(stemTop>=kEntInfo[kind].h*.999f&&stemBottom<=-.99f,"leader reaches apex from planted trunk",kind,lod);
      if(lod==3)check(fabsf(b.hi.y-bounds(mesh,r,0).hi.y)<.01f,"hero conifer crown retains near-tier maximum height",kind,lod);
    }
  }

  // Eight azimuths protect width as well as height. This catches a pine's
  // multi-lobed crown collapsing into one narrow far-facing card or a missing
  // side of a tree at a LOD transition. Actual alpha coverage is render-tested.
  for(int kind=EK_FIR;kind<=EK_BUSH;++kind)for(int az=0;az<8;++az) {
    float a=az*PI/8,cs=cosf(a),sn=sinf(a),width[4]={};
    for(int lod=0;lod<4;++lod) {
      float lo=1e9f,hi=-1e9f;
      for(int i=ranges[kind].first[lod];i<ranges[kind].first[lod]+ranges[kind].count[lod];++i){float q=mesh[i].px*cs+mesh[i].pz*sn;lo=std::min(lo,q);hi=std::max(hi,q);}
      width[lod]=hi-lo;
    }
    for(auto pair:{std::pair<int,int>{3,0},{0,1},{1,2}}) {
      float ratio=width[pair.first]/width[pair.second];
      if(ratio<.74f||ratio>1.36f)std::printf("width ratio %s az%d lod%d/%d %.3f\n",kEntInfo[kind].name,az,pair.first,pair.second,ratio);
      check(ratio>=.74f&&ratio<=1.36f,"eight-view adjacent LOD crown envelope",kind,pair.first);
    }
  }

  for(int k=EK_FIR;k<=EK_BUSH;++k)woodConnected(mesh,ranges[k],k);
  for(int k:{EK_OUTCROP,EK_SPIRE,EK_SEASTACK}) {
    auto near=bounds(mesh,ranges[k],0),far=bounds(mesh,ranges[k],2);
    check(far.hi.y>=near.hi.y*.95f&&far.hi.y<=near.hi.y*1.02f,"distant formation height preserved",k,2);
    check(far.lo.y<=0,"distant formation planted",k,2);
  }
  auto mid=bounds(mesh,ranges[EK_SEASTACK],1),far=bounds(mesh,ranges[EK_SEASTACK],2);
  check(far.hi.x>=mid.hi.x*.95f&&far.hi.x<=mid.hi.x*1.02f,"satellite sea stack preserved");
  std::printf("environment_nature_detail_test: %d checks, %d failures. Counts are geometry budgets, not GPU timing.\n",checks,failures);
  return failures?1:0;
}
