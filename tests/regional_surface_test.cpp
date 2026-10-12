#include "../src/regional_surface.h"
#include "../src/region_travel.h"
#include "../src/aircraft.h"
#include "../src/hive_world_collision.h"
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace regionalSurface;
int checks = 0;
void check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
Handle make(const procedural::Descriptor& d, const std::function<float(float,float)>& height,
            Provenance provenance = Provenance::Analytic, int n = 129) {
  std::vector<GridVertex> grid; grid.reserve(size_t(n) * n);
  const regionTravel::Chart chart(d.origin); const geo::LocalFrame frame(d.origin);
  for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
    const float wx = -WORLD_HALF + x * (2.f * WORLD_HALF / (n - 1));
    const float wz = -WORLD_HALF + z * (2.f * WORLD_HALF / (n - 1));
    const float raw = height(wx, wz);
    grid.push_back({frame.toLocal(chart.position(vec3(wx, std::max(raw, -.5f), wz))), raw});
  }
  return makeSurface(d, n, std::move(grid), provenance);
}
// Independent transcription of the far-region vertex shader before planetPos.
vec3 shaderPosition(const regionalSurface::Surface& s, geo::Position origin, const GridVertex& v) {
  geo::FrameChange transform(geo::LocalFrame(s.origin), geo::LocalFrame(origin));
  const vec3 c0=transform.vector(vec3(1,0,0)), c1=transform.vector(vec3(0,1,0));
  const vec3 c2=transform.vector(vec3(0,0,1)), c3=transform.point(vec3());
  const vec3 q=c0*v.position.x+c1*v.position.y+c2*v.position.z+c3;
  const float horizontal=std::sqrt(q.x*q.x+q.z*q.z);
  const float angle=std::atan2(horizontal,float(geo::radius())+q.y);
  const vec2 bearing=horizontal>.001f?vec2(q.x/horizontal,q.z/horizontal):vec2();
  return {bearing.x*angle*float(geo::radius()),std::max(v.height,-.5f),bearing.y*angle*float(geo::radius())};
}
void parity() {
  const geo::Position locations[]={{15,145,0},{0,44.99,0},{0,179.5,0},{88,121,0},{-88,-70,0}};
  for (auto location:locations) {
    const auto d=procedural::describe(location);
    auto grid=make(d,[](float x,float z){return 200.f+.002f*x+.003f*z;});
    for (const double separation : {260000.,1200000.}) {
    const geo::Position origin=geo::destination(d.origin,73.,separation);
    double maxError=0;
    for (int j=2;j<127;j+=13) for(int i=2;i<127;i+=11) for(int tri=0;tri<2;++tri) {
      const size_t k=size_t(j)*129+i;
      const size_t index[2][3]={{k,k+129,k+1},{k+1,k+129,k+130}};
      const vec3 a=shaderPosition(*grid,origin,grid->grid[index[tri][0]]);
      const vec3 b=shaderPosition(*grid,origin,grid->grid[index[tri][1]]);
      const vec3 c=shaderPosition(*grid,origin,grid->grid[index[tri][2]]);
      for (const auto weights:{vec3(.2f,.3f,.5f),vec3(.6f,.3f,.1f),vec3(0,.5f,.5f),
                               vec3(.5f,0,.5f),vec3(.5f,.5f,0)}) {
        const vec3 p=a*weights.x+b*weights.y+c*weights.z;
        const auto q=sample(grid,origin,p.x,p.z);
        if (!q.covered || !q.land) {
          const auto actual=projectVertex(*grid,origin,grid->grid[index[tri][0]]);
          std::fprintf(stderr,"PARITY_FAILURE region=%s distance=%.0f cell=%d,%d triangle=%d weights=%.4f,%.4f,%.4f query=%.9f,%.9f covered=%d land=%d vertex_delta=%.9f,%.9f,%.9f\n",
            d.id.c_str(),separation,i,j,tri,weights.x,weights.y,weights.z,p.x,p.z,q.covered,q.land,actual.x-a.x,actual.y-a.y,actual.z-a.z);
        }
        check(q.covered&&q.land,"projected cell/diagonal remains covered across seam/date-line/poles");
        maxError=std::max(maxError,double(std::abs(q.height-p.y)));
        check(std::abs(q.height-p.y)<.025f,"projected triangle height matches vertex shader interpolation");
        // An exact shared edge can legally select either adjacent face. This
        // smooth test ramp still gives nearly identical geometric normals.
        const vec3 normal=normalize(cross(b-a,c-a));
        check(dot(normal,q.normal)>.9999f,"projected geometric contact normal matches its triangle");
      }
    }
    check(grid->grid.size()*sizeof(GridVertex)==129*129*16,"retained collision grid is compact and bounded");
    std::printf("SURFACE %s separation_km=%.0f projected_triangle_max_error_m=%.6f\n",d.id.c_str(),separation/1000.,maxError);
    }
  }
}
void shoreline() {
  const auto d=procedural::describe({15,145,0});
  auto mesh=make(d,[](float x,float){return x<=0?100.f:-100.f;});
  const size_t k=64*129+64;
  const vec3 a=shaderPosition(*mesh,d.origin,mesh->grid[k]);
  const vec3 b=shaderPosition(*mesh,d.origin,mesh->grid[k+129]);
  const vec3 c=shaderPosition(*mesh,d.origin,mesh->grid[k+1]);
  // Raw interpolation would put this point deep under water. The renderer's
  // clamped corners instead place it at 19.6m and collision must agree.
  const vec3 land=a*.1f+b*.1f+c*.8f;
  const auto hit=sample(mesh,d.origin,land.x,land.z);
  check(hit.land&&std::abs(hit.height-19.6f)<.01f,"shoreline uses clamped GPU vertex heights, not raw seabed interpolation");
  const vec3 sea=a*.001f+b*.001f+c*.998f;
  const auto wet=sample(mesh,d.origin,sea.x,sea.z);
  check(wet.covered&&!wet.land&&wet.height==-SEA_DEPTH,"negative clamped interpolant is discarded exactly as the fragment shoreline");
  auto empty=make(d,[](float,float){return -2.f;});
  check(!sample(empty,d.origin,0,0).land,"fully submerged triangles provide no invented land");
}
void adjacentEdge() {
  const auto d=procedural::describe({15,145,0});
  auto mesh=make(d,[](float x,float){return x<=0?100.f:1100.f;});
  const auto origin=geo::destination(d.origin,73.,1200000.);
  const size_t k=64*129+64;
  const vec3 a=projectVertex(*mesh,origin,mesh->grid[k]);
  const vec3 b=projectVertex(*mesh,origin,mesh->grid[k+129]);
  const vec3 c=projectVertex(*mesh,origin,mesh->grid[k+1]);
  // Just inside the steep right-hand cell, but within numerical coverage
  // allowance of the earlier flat left-hand cell. The actual containing face
  // must win, rather than snapping this query down to that flat shared edge.
  const vec3 p=a*.4996f+b*.5f+c*.0004f;
  const double ux=double(b.x)-a.x,uz=double(b.z)-a.z,vx=double(c.x)-a.x,vz=double(c.z)-a.z;
  const double px=double(p.x)-a.x,pz=double(p.z)-a.z,det=ux*vz-uz*vx;
  const double wb=(px*vz-pz*vx)/det,wc=(ux*pz-uz*px)/det,wa=1.-wb-wc;
  const float expected=float(wa*a.y+wb*b.y+wc*c.y);
  check(wa>0&&wb>0&&wc>0&&expected>100.15f,"stepped-edge regression lies strictly inside the rising adjacent face");
  const auto hit=sample(mesh,origin,p.x,p.z);
  check(hit.land&&std::abs(hit.height-expected)<.001f,"exact adjacent face wins over an earlier tolerant clamped edge");
}
void integration() {
  clearFlightContext();
  const auto active=procedural::solaceDescriptor();
  procedural::Descriptor foreign;
  for(const auto& d:procedural::nearby(active.origin,12))if(!d.authored){foreign=d;break;}
  check(!foreign.id.empty(),"foreign test fixture exists");
  auto surface=make(foreign,[](float,float){return 300.f;});
  auto replacement=make(foreign,[](float,float){return 450.f;},Provenance::FullDerived);
  const vec3 point=regionTravel::Chart(active.origin).local(foreign.origin);
  g_world=World{}; g_world.repeating=false;
  check(g_world.height(point.x,point.z)==-SEA_DEPTH,"ordinary nonrepeating World has no implicit collision opt-in");
  auto protection=foreign.origin; protection.height=1000;
  setFlightContext(active,protection,15,50);
  check(mayActivate(surface),"safe airborne first reveal accepted");
  publishDrawableSurfaces({surface});
  check(appliesTo(g_world)&&std::abs(g_world.height(point.x,point.z)-300.f)<.001f,"published foreign collider routes through active World height");
  check(g_world.normal(point.x,point.z).y>.9999f,"World normal uses foreign triangle normal");
  check(currentUpperBound(point,point,15)>=300.f,"foreign sweep bounds cover nearby published land");
  check(currentUpperBound(vec3(),vec3(),15)==0.f,"distant proxy summits do not block all-ocean ownership transitions");
  World candidate;candidate.repeating=false;
  check(candidate.height(point.x,point.z)==-SEA_DEPTH,"candidate generation never consults global flight collider");
  g_world.repeating=true;
  check(!appliesTo(g_world),"career topology rejects fallback even when an old flight context exists");
  g_world.repeating=false;
  g_world.regionId="mismatched-active-region";
  check(!appliesTo(g_world)&&g_world.height(point.x,point.z)==-SEA_DEPTH,"stale context cannot route into a mismatched active World identity");
  g_world.regionId="solace";
  setFlightContext(procedural::solaceDescriptor(active.worldSeed+1),protection,15,50);
  check(appliesTo(g_world)&&g_world.height(point.x,point.z)==-SEA_DEPTH,"authored context remains seed independent but stale generated proxy seed is rejected");
  setFlightContext(foreign,protection,15,50);
  check(!appliesTo(g_world),"stale chart origin and region are rejected before fallback query");
  setFlightContext(active,protection,15,50);
  // Populate the local broad phase so this exercises the far-chart early-out,
  // rather than its conservative missing-hmax infinity fallback.
  g_world.hmax[0].assign(size_t(HMAX_N)*HMAX_N,0.f);
  const float t=hiveWorldSweep(point+vec3(0,1200,0),point+vec3(0,100,0),1.f);
  check(t>.7f&&t<.9f,"1100m high-speed sweep cannot skip foreign terrain through the old ocean fast path");
  Plane plane;plane.reset(&kAircraft[0],point+vec3(0,297,0),0,kAircraft[0].maxFuel,85,true,60);
  plane.sceneryHits=false; Weather weather;weather.windSpeed=weather.gust=weather.turbulence=0;
  plane.step(.01f,weather,0);
  check(plane.ev.crashed&&plane.ev.crashReason!="Ditched in the sea","actual Plane contacts encounter foreign land rather than fictitious ocean");
  protection.height=320;setFlightContext(active,protection,15,50);
  check(isPinned(surface)&&regionPinned(foreign.id),"contact-near proxy pins its exact representation");
  check(!mayReplace(surface,replacement)&&!mayActivate(replacement),"higher full-derived surface cannot teleport under a low aircraft");
  check(mayReplace(surface,surface)&&g_world.height(point.x,point.z)==300.f,"same-version retention preserves height under contact");
  clearFlightContext();setFlightContext(active,protection,15,50);
  check(!mayActivate(surface)&&!queryCurrent(point.x,point.z).covered,"late first mesh stays hidden and uncollidable while unsafe");
  protection.height=1000;setFlightContext(active,protection,15,50);
  check(mayReplace(surface,replacement),"safe altitude permits paired version promotion");
  publishDrawableSurfaces({replacement});
  check(g_world.height(point.x,point.z)==450.f,"paired replacement uses new collision identity");
  check(residentSurfaceCount()==1&&residentGridBytes()<=129*129*16,"published CPU storage remains bounded without duplicate triangle buffers");
  const auto begin=std::chrono::steady_clock::now();double sum=0;
  for(int i=0;i<10000;++i)sum+=g_world.height(point.x+(i%101),point.z+(i%73));
  const double queryMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  check(sum>0,"contact microbenchmark evaluated real terrain");
  std::printf("SURFACE_CONTACT_BENCH queries=10000 wall_ms=%.3f\n",queryMs);
  std::vector<Handle> neighborhood{replacement};
  for(int f=0;f<6&&neighborhood.size()<64;++f) for(int y=0;y<32&&neighborhood.size()<64;y+=4)
    for(int x=0;x<32&&neighborhood.size()<64;x+=4) {
      const auto d=procedural::find("pr1-"+std::to_string(f)+"-"+std::to_string(x)+"-"+std::to_string(y));
      if(d&&d->id!=foreign.id) neighborhood.push_back(make(*d,[](float,float){return 200.f;},Provenance::Analytic,2));
    }
  check(neighborhood.size()==64,"cache-cap fixture has64 distinct actual region identities");
  // Force every miss through the cheap cull before the final matching surface.
  std::rotate(neighborhood.begin(),neighborhood.begin()+1,neighborhood.end());
  publishDrawableSurfaces(neighborhood);
  check(residentSurfaceCount()==64&&residentGridBytes()<=64*129*129*16,"64-surface registry retains bounded compact grids");
  const auto fullBegin=std::chrono::steady_clock::now();sum=0;
  for(int i=0;i<10000;++i)sum+=g_world.height(point.x+(i%101),point.z+(i%73));
  const double fullMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fullBegin).count();
  check(sum>0,"64-surface benchmark performs actual height queries");
  const auto sweepBegin=std::chrono::steady_clock::now();
  const float fullSweep=hiveWorldSweep(point+vec3(0,1200,0),point+vec3(0,100,0),1.f);
  const double sweepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sweepBegin).count();
  check(fullSweep>.6f&&fullSweep<.8f,"64-surface broad phase cannot reject the450m foreign terrain sweep");
  std::printf("SURFACE_FULL_CACHE_BENCH surfaces=64 queries=10000 wall_ms=%.3f sweep1100m_ms=%.3f\n",fullMs,sweepMs);
  bool capped=false;neighborhood.push_back(surface);
  try{publishDrawableSurfaces(neighborhood);}catch(const std::invalid_argument&){capped=true;}
  check(capped&&residentSurfaceCount()==64,"oversized registry rejected without changing its prior snapshot");
  bool invalid=false;
  try{makeSurface(foreign,130,std::vector<GridVertex>{},Provenance::Analytic);}catch(const std::invalid_argument&){invalid=true;}
  check(invalid,"unbounded collision mesh resolution rejected");
  clearFlightContext();
  check(g_world.height(point.x,point.z)==-SEA_DEPTH&&!regionPinned(foreign.id),"exit clears all fallback state and contact pins");
}
}
int main(){try{parity();shoreline();adjacentEdge();integration();std::printf("PASS regional surfaces: %d checks\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL regional surfaces: %s\n",e.what());return 1;}}
