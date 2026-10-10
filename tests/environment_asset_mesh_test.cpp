// CPU-only structural, silhouette and work-budget contracts for the island asset pass.
// These are mesh counts, not a GPU benchmark or an RTX 3070 frame-rate claim.
#include "entity_mesh.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <cstddef>

namespace {
int checks=0,failures=0;
void check(bool ok,const char* label,int kind=-1,int lod=-1) {
  ++checks;
  if(!ok) {++failures;std::printf("FAIL %s kind=%d lod=%d\n",label,kind,lod);}
}
struct Bounds {vec3 lo{1e9f},hi{-1e9f};};
Bounds bounds(const std::vector<EVert>& v,const EntMeshRange& r,int lod) {
  Bounds b;
  for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i) {const auto&e=v[i];
    b.lo={std::min(b.lo.x,e.px),std::min(b.lo.y,e.py),std::min(b.lo.z,e.pz)};
    b.hi={std::max(b.hi.x,e.px),std::max(b.hi.y,e.py),std::max(b.hi.z,e.pz)};
  }
  return b;
}
int partCount(const std::vector<EVert>&v,const EntMeshRange&r,int lod,int part) {
  int n=0;for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i) n+=int(v[i].part+.5f)==part;return n;
}
}
int main() {
  static_assert(sizeof(EVert)==40 && sizeof(Ent)==32,"preserve renderer and instance ABI");
  static_assert(ENT_LODS==4,"budgets explicitly cover near, middle, far and hero slots");
  static_assert(offsetof(EVert,part)==24 && offsetof(EVert,ao)==28 && offsetof(EVert,u)==32 && offsetof(EVert,v)==36,
    "preserve material and wheel pivot attribute offsets");
  std::vector<EVert> v,again;EntMeshRange r[EK_COUNT],rr[EK_COUNT];
  buildEntityMeshes(v,r);buildEntityMeshes(again,rr);
  check(v.size()==again.size() && !std::memcmp(v.data(),again.data(),v.size()*sizeof(EVert)),"deterministic vertices");
  check(!std::memcmp(r,rr,sizeof r),"deterministic ranges");
  // Validate ranges before any triangle or material access, so a broken builder
  // produces a test failure instead of an out-of-bounds read in the test itself.
  bool safeRanges=true;
  for(int k=0;k<EK_COUNT;++k)for(int lod=0;lod<ENT_LODS;++lod) {
    const int first=r[k].first[lod],count=r[k].count[lod];
    bool safe=first>=0 && count>=0 && count%3==0 && size_t(first)<=v.size() && size_t(count)<=v.size()-size_t(first);
    check(safe,"valid range bounds",k,lod);safeRanges&=safe;
  }
  if(!safeRanges)return 1;
  // Per-kind ceilings are deliberate. Hero construction has its own measured budget;
  // distant structures and rock formations keep their established lower-tier costs.
  // All four ceilings are reviewed authored counts. Never exempt the hero slot
  // from its budget or index a three-slot table with the four-slot mesh ABI.
  const int budget[EK_COUNT][ENT_LODS]={
    {1638,204,36,15534},{1998,234,36,20430},{2664,852,198,7044},{2322,468,78,16080},{2340,588,78,12048},{1206,264,108,7140},{840,240,60,5904},
    {960,240,60,3840},{960,240,60,3840},{960,240,60,3840},{1680,300,180,6720},{1638,465,126,8820},{2052,618,135,12024},
    {312,102,72,5802},{186,96,66,3048},{270,138,108,6372},{516,174,72,4992},{522,216,108,10728},{192,96,90,1875},{1134,114,84,20340},
    {306,186,156,5562},{264,264,228,29718},{456,216,180,5520},{324,132,72,3450},{240,168,108,2352},{390,144,126,5490},{456,150,114,4758},
    {600,180,144,2388},{726,330,234,6033},{414,228,108,2100},{390,306,0,390},{384,276,0,384},
    {312,114,78,5916},{504,228,132,5868},{264,210,90,1344},{846,594,96,16782},{726,462,318,4950},{288,132,90,1800},
    {636,288,96,5088},{324,228,222,2478},{360,228,228,4428},{468,216,180,2478},{906,702,294,18129},{3408,2220,852,62838},{276,240,144,4956},
    {3279,1623,96,25569},{1476,792,168,24918},{90,66,42,300},{960,498,72,1716},{1572,372,372,5220},{210,183,75,894},{120,96,96,1872}
  };
  size_t cursor=0;int totals[ENT_LODS]={};
  for(int k=0;k<EK_COUNT;++k) for(int lod=0;lod<ENT_LODS;++lod) {
    check(r[k].first[lod]==int(cursor),"contiguous ranges",k,lod);
    check(r[k].count[lod]>=0 && r[k].count[lod]%3==0 && r[k].count[lod]<=budget[k][lod],"strict vertex ceiling",k,lod);
    if(k<=EK_SEASTACK && lod==2) check(r[k].count[lod]==budget[k][lod],"natural far cost unchanged",k,lod);
    if(k>=EK_HOUSE && k<=EK_PAPI && lod<3)check(r[k].count[lod]==budget[k][lod],"established building/fixture tier cost unchanged",k,lod);
    check(r[k].count[lod]>0 || (lod==2 && (k==EK_RWYLIGHT || k==EK_PAPI)),"required mesh slot exists",k,lod);
    totals[lod]+=r[k].count[lod];cursor+=r[k].count[lod];
    for(int i=r[k].first[lod];i<r[k].first[lod]+r[k].count[lod];++i) {
      const auto&e=v[i];const float values[]={e.px,e.py,e.pz,e.nx,e.ny,e.nz,e.part,e.ao,e.u,e.v};
      for(float f:values) check(std::isfinite(f),"finite vertex",k,lod);
      check(std::abs(e.nx*e.nx+e.ny*e.ny+e.nz*e.nz-1.f)<.001f,"unit normal",k,lod);
      check(e.part>=P_BARK && e.part<=P_WHEEL5 && e.part==std::floor(e.part),"valid material slot",k,lod);
      if((i-r[k].first[lod])%3==0) {
        const auto&b=v[i+1];const auto&c=v[i+2];
        vec3 n=cross(vec3(b.px-e.px,b.py-e.py,b.pz-e.pz),vec3(c.px-e.px,c.py-e.py,c.pz-e.pz));
        check(dot(n,n)>1e-12f,"nondegenerate triangle",k,lod);
      }
    }
  }
  check(cursor==v.size(),"all vertices accounted for");
  // Frozen pre-pass construction envelopes, including their existing porches/overhangs. Tiny
  // non-coplanar surface offsets are allowed; collision dimensions and scenery APIs are unchanged.
  struct Envelope {int kind;vec3 lo,hi;};
  const Envelope envelope[]={
    {EK_HOUSE,{-5,-3,-6},{5,9.4f,6.7f}}, {EK_HOUSE_HIP,{-5.6f,-3,-5.6f},{5.6f,6.4f,6.6f}},
    {EK_HOUSE_L,{-6.45f,-3,-6.45f},{6.45f,9,6.45f}}, {EK_FARMHOUSE,{-6,-3,-5},{6,10,6.4f}},
    {EK_TOWNHOUSE,{-9.12f,-3,-5.62f},{9.12f,11.6f,6.4f}},
    {EK_SHOP,{-7.12f,-3,-7.12f},{7.12f,5.6f,8.7f}}, {EK_APARTMENT,{-9.12f,-3,-7.12f},{9.12f,23.2f,8.3f}},
    {EK_OFFICE,{-9.35f,-3,-9.35f},{9.35f,39.8f,9.35f}}, {EK_TOWER,{-10.3f,-3,-10.3f},{10.3f,80,10.3f}},
    {EK_SKYSCRAPER,{-10.5f,-3,-10.5f},{10.5f,135,10.5f}},
    {EK_SILO,{-3.3f,-2,-3.3f},{3.3f,18.6f,3.3f}}, {EK_WATERTOWER,{-5.84957f,-1.01439f,-6},{5.84957f,28,6}},
    {EK_LIGHTHOUSE,{-6.3f,-2,-4.3f},{3.4f,27,4.3f}}, {EK_GASSTATION,{-8,-1,-7},{8,7,7}},
    {EK_WAREHOUSE,{-12.4f,-3,-9.4f},{12.4f,8.5f,10.6f}},
    {EK_BARN,{-6.4f,-3,-9.35f},{6.4f,10,9.35f}}, {EK_CHURCH,{-5.4f,-3,-13.4f},{5.4f,28.4f,13.12f}},
    {EK_HANGAR,{-20.6f,-2,-16.6f},{20.6f,13,16.6f}}, {EK_ARCH_HANGAR,{-9.1f,-2,-12.1f},{9.1f,7.5f,12.25f}},
    {EK_T_HANGAR,{-24.4f,-2,-7.4f},{24.4f,4.8f,7.4f}}, {EK_TERMINAL,{-63,-2,-27},{63,16.6f,25}},
    {EK_CTRL_TOWER,{-5.2f,-2,-5.2f},{5.2f,34,5.2f}}, {EK_FBO,{-10.2f,-2,-7.2f},{10.2f,9,9}},
    {EK_CAR,{-.9f,0,-2.2f},{.9f,1.46f,2.2f}}, {EK_TRUCK,{-1.25f,0,-4.4f},{1.25f,3.05f,4.55f}}
  };
  for(const auto&e:envelope) for(int lod=0;lod<ENT_LODS;++lod) {
    Bounds b=bounds(v,r[e.kind],lod);
    check(b.lo.x>=e.lo.x-.006f && b.lo.y>=e.lo.y-.006f && b.lo.z>=e.lo.z-.006f &&
      b.hi.x<=e.hi.x+.006f && b.hi.y<=e.hi.y+.006f && b.hi.z<=e.hi.z+.006f,"preserved construction envelope",e.kind,lod);
  }
  for(int k=EK_HOUSE;k<=EK_GASSTATION;++k) {
    Bounds near=bounds(v,r[k],0),hero=bounds(v,r[k],3);
    check(std::abs(hero.lo.y-near.lo.y)<.006f && hero.lo.y<-.9f,"hero retains buried construction anchor",k,3);
    check(r[k].count[3]>r[k].count[0],"hero is separately detailed architecture",k,3);
  }
  for(int k:{EK_RWYLIGHT,EK_PAPI}) {
    check(r[k].count[3]==r[k].count[0] && !std::memcmp(v.data()+r[k].first[3],v.data()+r[k].first[0],r[k].count[0]*sizeof(EVert)),
      "runway fixture hero exactly reuses preserved near mesh",k,3);
  }
  for(int k:{EK_FIR,EK_SPRUCE}) {
    for(int lod:{0,1,3}) {
      float stemTop=-1e9f,stemBottom=1e9f;
      std::set<int> occupiedStemBands;
      for(int i=r[k].first[lod];i<r[k].first[lod]+r[k].count[lod];++i) if(v[i].part==P_BARK) {
        stemTop=std::max(stemTop,v[i].py);stemBottom=std::min(stemBottom,v[i].py);
        occupiedStemBands.insert(int(std::floor(v[i].py*2.f)));
      }
      check(stemTop>=kEntInfo[k].h-.001f && stemBottom<=-.99f,"planted conifer stem reaches its leader",k,lod);
      check(occupiedStemBands.size()>=5,"supporting conifer branches span its crown",k,lod);
    }
    Bounds near=bounds(v,r[k],0),far=bounds(v,r[k],2),hero=bounds(v,r[k],3);
    check(std::abs(near.hi.y-far.hi.y)<.01f && near.lo.y<=-.99f,"conifer far height and ground anchor",k,0);
    check(std::abs(hero.hi.y-near.hi.y)<.01f && hero.lo.y<=-.99f,"conifer hero height and ground anchor",k,3);
  }
  for(int k=EK_FIR;k<=EK_BUSH;++k) {
    check(partCount(v,r[k],3,P_LEAF)==0 && partCount(v,r[k],3,P_NEEDLE)==0,"hero foliage has no opaque crown shell",k,3);
    check(partCount(v,r[k],3,P_BARK)>100,"hero foliage has substantial woody support",k,3);
    check(r[k].count[3]>r[k].count[0],"hero detail exceeds near foliage",k,3);
  }
  check(partCount(v,r[EK_OAK],0,P_BARK)>=270,"oak crown has supporting forked limbs");
  check(partCount(v,r[EK_BIRCH],0,P_BARK)>=300,"birch has fine branching");
  check(partCount(v,r[EK_BUSH],0,P_BARK)>=72,"shrub has woody stems");
  for(int k:{EK_CAR,EK_TRUCK}) for(int lod=0;lod<ENT_LODS;++lod) {
    bool raked=false;
    for(int i=r[k].first[lod];i<r[k].first[lod]+r[k].count[lod];++i) {
      const auto&e=v[i];if(e.part==P_GLASS && std::abs(e.nz)>.5f && std::abs(e.ny)>.15f) raked=true;
    }
    check(raked,"sloped windscreen retained in every LOD",k,lod);
    if(lod!=2) {
      int wheels=k==EK_CAR?4:6;
      for(int w=0;w<wheels;++w) {
        check(partCount(v,r[k],lod,P_WHEEL0+w)>0,"distinct animated wheel slot",k,lod);
        const EVert* pivot=nullptr;float maxRadius=0;
        for(int i=r[k].first[lod];i<r[k].first[lod]+r[k].count[lod];++i) if(v[i].part==P_WHEEL0+w) {
          const auto&e=v[i];float radius=std::sqrt((e.py-e.u)*(e.py-e.u)+(e.pz-e.v)*(e.pz-e.v));
          if(!pivot)pivot=&e;
          check(e.ao==pivot->ao && e.u==pivot->u && e.v==pivot->v,"wheel has one constant animation pivot",k,lod);
          check(radius<=e.ao+1e-4f && e.ao>0,"wheel radius and rest pivot ABI",k,lod);
          maxRadius=std::max(maxRadius,radius);
        }
        if(pivot) {
          check(std::abs(maxRadius-pivot->ao)<1e-4f,"wheel geometry reaches encoded tyre radius",k,lod);
          check(std::abs(pivot->u-pivot->ao)<1e-4f,"rotating tyre is tangent to ground",k,lod);
        }
      }
    }
    Bounds near=bounds(v,r[k],0),current=bounds(v,r[k],lod);
    check(std::abs(current.hi.y-near.hi.y)<.05f && std::abs(current.hi.x-near.hi.x)<.05f && std::abs(current.hi.z-near.hi.z)<.06f,"vehicle LOD silhouette continuity",k,lod);
  }
  // A unit-length normal can still point into solid bodywork. Identify each
  // tapered reflector annulus by its physical lamp coordinates, independently
  // of the emitter's chosen reference point, and require its cavity-facing normal.
  struct Reflector {int kind;float x,y,outer,inner,front,back;};
  const Reflector reflectors[]={
    {EK_CAR,-.5643f,.582f,.055f,.055f*.38f,2.152f,2.113f},
    {EK_CAR,-.4191f,.582f,.047f,.047f*.38f,2.152f,2.113f},
    {EK_CAR,.4257f,.582f,.055f,.055f*.38f,2.152f,2.113f},
    {EK_CAR,.5709f,.582f,.047f,.047f*.38f,2.152f,2.113f},
    {EK_TRUCK,-1.0252f,.88f,.066f,.029f,4.535f,4.508f},
    {EK_TRUCK,-.8407f,.88f,.066f,.029f,4.535f,4.508f},
    {EK_TRUCK,.8448f,.88f,.066f,.029f,4.535f,4.508f},
    {EK_TRUCK,1.0293f,.88f,.066f,.029f,4.535f,4.508f}
  };
  for(const auto& lamp:reflectors) {
    int covered=0;float slope=(lamp.outer-lamp.inner)/(lamp.front-lamp.back);
    for(int i=r[lamp.kind].first[3];i<r[lamp.kind].first[3]+r[lamp.kind].count[3];i+=3) {
      bool annulus=true;
      for(int j=0;j<3;++j) {
        const auto&e=v[i+j];float radius=std::hypot(e.px-lamp.x,e.py-lamp.y);
        float expectedRadius=lamp.inner+(e.pz-lamp.back)*slope;
        if(e.part!=P_METAL || e.pz<lamp.back-1e-5f || e.pz>lamp.front+1e-5f || std::abs(radius-expectedRadius)>.0001f)annulus=false;
      }
      if(!annulus)continue;
      for(int j=0;j<3;++j) {
        const auto&e=v[i+j];vec3 radial(e.px-lamp.x,e.py-lamp.y,0);radial=normalize(radial);
        vec3 expected=normalize(vec3(-radial.x,-radial.y,slope)),normal(e.nx,e.ny,e.nz);
        check(dot(normal,expected)>.995f && e.nz>0.f && dot(normal,radial)<0.f,"reflector normal faces cavity and opening",lamp.kind,3);
        ++covered;
      }
    }
    check(covered>=96,"each headlamp has a sampled concave reflector",lamp.kind,3);
  }
  {
    int band=0;
    for(int i=r[EK_TRUCK].first[0];i<r[EK_TRUCK].first[0]+r[EK_TRUCK].count[0];++i) {
      const auto&e=v[i];bool atBand=std::abs(std::abs(e.pz+2.9f)-.055f)<.0001f || std::abs(std::abs(e.pz-.9f)-.055f)<.0001f;
      if(e.part==P_DARK && atBand && e.py>.84f) ++band;
    }
    check(band==144,"tank bands match twelve-sided barrel without dashed intersections",EK_TRUCK,0);
  }
  std::printf("environment_asset_mesh_test: %d checks, %d failures; near/mid/far/hero vertices %d/%d/%d/%d, total %zu (CPU geometry only)\n",checks,failures,totals[0],totals[1],totals[2],totals[3],v.size());
  return failures?1:0;
}
