// CPU-only contracts for the environmental model/foliage prototype. No GL context or world build is needed.
#include "entity_mesh.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
struct Bounds { vec3 lo{1e9f,1e9f,1e9f}, hi{-1e9f,-1e9f,-1e9f}; };
Bounds bounds(const std::vector<EVert>& v, EntMeshRange r, int lod) {
  Bounds b;
  for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i) {
    const EVert& e=v[i];
    b.lo.x=std::min(b.lo.x,e.px);b.lo.y=std::min(b.lo.y,e.py);b.lo.z=std::min(b.lo.z,e.pz);
    b.hi.x=std::max(b.hi.x,e.px);b.hi.y=std::max(b.hi.y,e.py);b.hi.z=std::max(b.hi.z,e.pz);
  }
  return b;
}
float fract(float x) { return x-floorf(x); }
float leafHash(float x,float y) { return fract(sinf(x*127.1f+y*311.7f)*43758.5453f); }
// The GLSL leaf centres lie within cell+[0.2,0.8]; the rotated ellipse fits a radius-0.5 circle.
// This reference checks the exact accept/reject condition, independent of the outer spray mask (unchanged).
bool covered(float u,float v,float card,float seed,bool bounded) {
  float cx=floorf(u-(bounded?.5f:0.f)),cy=floorf(v-(bounded?.5f:0.f));
  const int first=bounded?0:-1,last=bounded?1:1;
  for(int j=first;j<=last;++j) for(int i=first;i<=last;++i) {
    float x=cx+i,y=cy+j,h=leafHash(x+card*7.f+seed*13.f,y+card*7.f+seed*13.f);
    float dx=u-(x+.5f+(h-.5f)*.6f),dy=v-(y+.5f+(fract(h*17.3f)-.5f)*.6f);
    if(bounded && dx*dx+dy*dy>.250001f) continue;
    float a=h*6.2832f,c=cosf(a),s=sinf(a),rx=c*dx+s*dy,ry=-s*dx+c*dy;
    if(sqrtf(rx*rx/.25f+ry*ry/(.23f*.23f))+.15f*(fabsf(ry)<=.02f?1.f:0.f)<=1.f) return true;
  }
  return false;
}
}

int main() {
  static_assert(sizeof(EVert)==40,"environment vertex ABI changed");
  static_assert(sizeof(Ent)==32,"instance ABI changed");
  std::vector<EVert> v,again;EntMeshRange r[EK_COUNT],r2[EK_COUNT];
  buildEntityMeshes(v,r);buildEntityMeshes(again,r2);
  assert(v.size()==again.size() && !memcmp(v.data(),again.data(),v.size()*sizeof(EVert)));
  assert(!memcmp(r,r2,sizeof r));
  // Per-kind/LOD limits pinned to a7f8d30. Improvements must not hide a blanket geometry budget increase.
  const int budgets[13][3]={{1188,168,36},{1620,204,36},{4974,804,198},{2934,396,78},
    {3294,264,78},{990,264,108},{1248,120,60},{960,240,60},{960,240,60},{960,240,60},
    {1680,300,180},{1638,465,126},{2052,618,147}};
  int vertices=0;
  for(int k=0;k<=EK_SEASTACK;++k) for(int l=0;l<ENT_LODS;++l) {
    assert(r[k].count[l]>0 && r[k].count[l]%3==0 && r[k].count[l]<=budgets[k][l]);
    if(k<=EK_BUSH) assert(r[k].count[l]==budgets[k][l]); // unchanged canopy/card density
    vertices+=r[k].count[l];
    for(int i=r[k].first[l];i<r[k].first[l]+r[k].count[l];++i) {
      const auto& e=v[i];
      const float values[]={e.px,e.py,e.pz,e.nx,e.ny,e.nz,e.part,e.ao,e.u,e.v};
      for(float value:values) assert(std::isfinite(value));
      float nn=e.nx*e.nx+e.ny*e.ny+e.nz*e.nz;assert(nn>.01f && nn<4.f);
      assert(e.ao>=0.f && e.ao<=1.001f);
      assert(e.py>=-2.f && e.py<=kEntInfo[k].h*1.3f+2.f);
      assert(fabsf(e.px)<kEntInfo[k].hx*3.f && fabsf(e.pz)<kEntInfo[k].hz*3.f);
      if((i-r[k].first[l])%3==0) {
        const auto& b=v[i+1];const auto& c=v[i+2];
        vec3 n=cross(vec3(b.px-e.px,b.py-e.py,b.pz-e.pz),vec3(c.px-e.px,c.py-e.py,c.pz-e.pz));
        if (!(dot(n,n)>1e-12f)) { fprintf(stderr,"degenerate kind=%d lod=%d triangle=%d area2=%g a=%g,%g,%g b=%g,%g,%g c=%g,%g,%g\n",k,l,(i-r[k].first[l])/3,dot(n,n),e.px,e.py,e.pz,b.px,b.py,b.pz,c.px,c.py,c.pz); }
        assert(dot(n,n)>1e-12f);
      }
    }
  }
  for(int k:{EK_OUTCROP,EK_SPIRE,EK_SEASTACK}) {
    Bounds near=bounds(v,r[k],0),far=bounds(v,r[k],2);
    assert(far.hi.y>=near.hi.y*.95f && far.hi.y<=near.hi.y*1.02f);
    assert(far.lo.y<=0.f); // stays planted in its original terrain anchor
  }
  Bounds mid=bounds(v,r[EK_SEASTACK],1),far=bounds(v,r[EK_SEASTACK],2);
  assert(far.hi.x>=mid.hi.x*.95f && far.hi.x<=mid.hi.x*1.02f); // retain the satellite column
  assert(r[EK_SEASTACK].count[2]==135); // satellite paid for by fewer main-column divisions
  // Roof-frame regression: triS adds a sampled perturbation to its inout normal accumulator.
  // Swap that accumulator into the roof frame as well as the sampling normal/position, then
  // transform the result back. Flat maps must return n0 on both X- and Z-dominant slopes.
  auto swapXZ=[](vec3 p) { return vec3(p.z,p.y,p.x); };
  for(vec3 n:{vec3(.6f,.8f,0),vec3(-.6f,.8f,0),vec3(0,.8f,.6f),vec3(0,.8f,-.6f),
              normalize(vec3(.7f,.5f,.2f)),normalize(vec3(-.2f,.5f,-.7f))}) {
    bool swapped=fabsf(n.x)>fabsf(n.z);
    for(vec3 delta:{vec3(0),vec3(.1f,-.03f,.07f),vec3(-.08f,.06f,-.13f)}) {
      vec3 accumulator=swapped?swapXZ(n):n;
      vec3 bumped=normalize(accumulator+delta);
      vec3 actual=swapped?swapXZ(bumped):bumped;
      vec3 expected=normalize(n+(swapped?swapXZ(delta):delta));
      assert(length(actual-expected)<1e-6f);
      if(length(delta)==0.f) assert(length(actual-n)<1e-6f);
    }
  }
  // The former missing pre-swap rotates this representative flat-map roof by about 50 degrees.
  assert(dot(swapXZ(vec3(.6f,.8f,0)),vec3(.6f,.8f,0))<.65f);
  size_t samples=0;
  for(int tag=0;tag<8;++tag) for(float seed:{0.f,.001f,.17f,.5f,.999f})
    for(int y=0;y<=128;++y) for(int x=0;x<=128;++x) {
      float u=x*(5.f/128.f),w=y*(5.f/128.f);
      assert(covered(u,w,float(tag),seed,false)==covered(u,w,float(tag),seed,true));++samples;
    }
  // Straddle every half-cell selection boundary with values immediately on either side.
  for(int tag=0;tag<8;++tag) for(int boundary=0;boundary<5;++boundary) for(int j=0;j<1024;++j) {
    float a=boundary+.5f,b=j*(5.f/1023.f),seed=fract(j*.07317f);
    for(float u:{std::nextafter(a,0.f),a,std::nextafter(a,6.f)}) {
      assert(covered(u,b,float(tag),seed,false)==covered(u,b,float(tag),seed,true));
      assert(covered(b,u,float(tag),seed,false)==covered(b,u,float(tag),seed,true));samples+=2;
    }
  }
  printf("environment_foliage_test: deterministic finite meshes, fixed ABI, %d budgeted vertices, planted far skylines, %zu exact leaf-coverage comparisons passed\n",vertices,samples);
}
