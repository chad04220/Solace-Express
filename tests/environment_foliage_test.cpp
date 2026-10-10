// CPU-only contracts for the environmental model/foliage prototype. No GL context or world build is needed.
#include "entity_mesh.h"
#include "entity_lod.h"
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

void sameRanges(const EntRanges& a,const EntRanges& b) {
  for(int k=0;k<EK_COUNT;++k) {
    float a0,a1,b0,b1;entLodLimits(a,k,a0,a1);entLodLimits(b,k,b0,b1);
    assert(entRangeOf(a,k)==entRangeOf(b,k) && a0==b0 && a1==b1);
  }
  assert(a.sh0==b.sh0 && a.sh1==b.sh1 && a.shRes==b.shRes);
}

// Integral of the unchanged keep fraction over a flat full-circle population of unit density.
// This estimates surviving instances/vertices, not frustum-visible objects, GPU work or FPS.
double keptArea(int k,double distance) {
  double ref=entThinRef(k),d=std::min(distance,ref);
  return double(PI)*d*d+(distance>ref?2.0*double(PI)*ref*ref*log(distance/ref):0.0);
}
double modelVertices(const EntRanges& ranges,int k,const EntMeshRange& mesh) {
  float l0,l1;entLodLimits(ranges,k,l0,l1);
  double a0=keptArea(k,l0),a1=keptArea(k,l1),far=keptArea(k,entRangeOf(ranges,k));
  return a0*mesh.count[0]+(a1-a0)*mesh.count[1]+(far-a1)*mesh.count[2];
}

void foliageRanges(const EntMeshRange* meshes) {
  // Frozen pre-extension policy: feeds, shadow LODs, all non-foliage ranges must retain it.
  const EntRanges baseline[] = {
    {2600,800,1700,9000,9000,190,900,280,1100,550,2800,300,1600,2048},
    {4500,1300,2800,13000,13000,260,1300,380,1600,850,4000,420,2600,2048},
    {7000,1800,3800,18000,18000,360,1800,480,2000,1300,5500,520,3600,4096}
  };
  const float expected[3][4]={{210,1000,2900,900},{290,1450,5000,1450},{400,2000,7800,2000}};
  for(int q=-2;q<=4;++q) {
    int preset=std::clamp(q,0,2);
    sameRanges(entBaseRangesFor(q),baseline[preset]);
    sameRanges(entRangesFor(q,true),baseline[0]); // feeds remain original Low even on High
    sameRanges(entRangesFor(q),entRangesFor(preset));
  }
  for(int q=0;q<3;++q) {
    const auto before=baseline[q],after=entRangesFor(q);
    assert(after.t0==expected[q][0] && after.t1==expected[q][1]);
    assert(after.tree==expected[q][2] && after.bush==expected[q][3]);
    assert(after.sh0==before.sh0 && after.sh1==before.sh1 && after.shRes==before.shRes);
    // Detailed chunk generation grows with foliage; the wider scenery scan is unchanged.
    assert(after.tree>std::max(after.rock,after.bush) && after.tree+300.f<after.build);
    double streamRatio=pow((after.tree+300.0)/(before.tree+300.0),2);
    assert(streamRatio>1.0 && streamRatio<1.25);
    double minInstances=1e9,maxInstances=0,minVertices=1e9,maxVertices=0;
    for(int k=0;k<EK_COUNT;++k) {
      float old0,old1,l0,l1;entLodLimits(before,k,old0,old1);entLodLimits(after,k,l0,l1);
      float oldFar=entRangeOf(before,k),far=entRangeOf(after,k);
      if(entClass(k)!=EC_TREE) {
        assert(l0==old0 && l1==old1 && far==oldFar);
        continue;
      }
      assert(l0>old0 && l1>old1 && far>oldFar);
      assert(l0/old0<=1.12501f && l1/old1<=1.12501f && far/oldFar<=1.12501f);
      assert(l0>0 && l0<l1 && l1<far);
      if(k==EK_BUSH) assert(l0==after.t0*.6f && l1==after.t1*.5f);
      // Existing switch points now keep the previous higher-detail model. Test both sides
      // of each new switch, using the same selection helper as bulk and per-instance draws.
      assert(entLodAt(old0,l0,l1)==0 && entLodAt(old1,l0,l1)==1);
      assert(entLodAt(std::nextafter(l0,0.f),l0,l1)==0 && entLodAt(l0,l0,l1)==1);
      assert(entLodAt(std::nextafter(l0,far),l0,l1)==1);
      assert(entLodAt(std::nextafter(l1,0.f),l0,l1)==1 && entLodAt(l1,l0,l1)==2);
      assert(entLodAt(std::nextafter(l1,far),l0,l1)==2);
      assert(entLodAt(oldFar,l0,l1)==2 && entLodAt(std::nextafter(far,0.f),l0,l1)==2);
      if(q>0) {
        float prev0,prev1;auto prev=entRangesFor(q-1);entLodLimits(prev,k,prev0,prev1);
        assert(l0>prev0 && l1>prev1 && far>entRangeOf(prev,k));
      }
      double instances=keptArea(k,far)/keptArea(k,oldFar);
      double vertices=modelVertices(after,k,meshes[k])/modelVertices(before,k,meshes[k]);
      assert(instances>1.0 && instances<1.13 && vertices>1.0 && vertices<1.26);
      minInstances=std::min(minInstances,instances);maxInstances=std::max(maxInstances,instances);
      minVertices=std::min(minVertices,vertices);maxVertices=std::max(maxVertices,vertices);
    }
    printf("foliage ranges q%d: flat-circle model, surviving instances +%.2f..%.2f%%, vertices +%.2f..%.2f%%; detail-stream area +%.2f%% (not measured frame cost)\n",
      q,100*(minInstances-1),100*(maxInstances-1),100*(minVertices-1),100*(maxVertices-1),100*(streamRatio-1));
  }
}

// Thinned instances dissolve in and out (entFade, mirrored by ent_vs.glsl) rather than switching: the fade is continuous
// in distance, whole well inside an instance's turn and gone past it, never drawn where the CPU has already dropped the
// instance (entKeepDrawn), and on average as dense as the thinning it replaces.
void foliageFade() {
  for(int q=0;q<3;++q) {
    const EntRanges R=entRangesFor(q);
    for(int k=0;k<=EK_SLAB;++k) {
      if(!entThins(k)) continue;
      const float ref=entThinRef(k),far=entRangeOf(R,k);
      double worstStep=0,worstDensity=0;
      for(float key=0.0005f;key<1.f;key+=0.0137f) {
        float prev=entFade(key,ref,1.f,far);
        for(float d=1.f;d<far*1.01f;d*=1.0025f) {
          const float f=entFade(key,ref,d,far);
          assert(f>=0.f && f<=1.f);
          worstStep=std::max(worstStep,double(fabsf(f-prev)));prev=f;
          if(f>0.f) assert(key<entKeepDrawn(k,d)+1e-6f && d<far);   // drawn by the shader: handed over by the CPU
          if(d<0.85f*ref) assert(f==1.f);                           // full density near: no thinning there at all
        }
      }
      // the step per quarter percent of the distance closed: at most a few percent of a whole instance (the old switch was
      // all of it at once)
      assert(worstStep<0.05);
      for(float d=ref*1.2f;d<far*0.85f;d*=1.1f) {
        double sum=0;const int n=20000;
        for(int i=0;i<n;++i) sum+=entFade((i+0.5f)/n,ref,d,far);
        worstDensity=std::max(worstDensity,fabs(sum/n/entKeep(k,d)-1.0));
      }
      assert(worstDensity<0.02);
      if(q==2 && (k==EK_FIR || k==EK_BUSH)) printf("foliage fade %s: worst step %.3f of an instance per 0.25%% of the distance, density within %.2f%% of the thinning\n",kEntInfo[k].name,worstStep,100*worstDensity);
    }
  }
}

// A tree's detail levels cross-fade (entLodKeep, mirrored by ent_vs.glsl): at every distance their shares of the
// screen-door add up to exactly one whole tree, the levels with a share are exactly the ones the CPU hands over (the
// one entLodAt picks and the farther one entLodAlso adds), each share changes smoothly, and the nearer model never
// reaches past its switch.
void detailCrossFade() {
  for(int q=0;q<3;++q) for(int feed=0;feed<2;++feed) {
    const EntRanges R=entRangesFor(q,feed!=0);
    for(int k=0;k<=EK_BUSH;++k) {
      assert(entLodFades(k));
      float l0,l1;entLodLimits(R,k,l0,l1);
      assert(l0<l1*(1.f-kEntLodFade));   // the two fades never overlap
      float prev[ENT_LODS]={1.f,0.f,0.f};double worst=0;
      for(float d=1.f;d<entRangeOf(R,k);d*=1.0025f) {
        float sum=0;int also=entLodAlso(d,l0,l1),at=entLodAt(d,l0,l1);
        for(int lod=0;lod<ENT_LODS;++lod) {
          float lo,hi;entLodKeep(lod,d,l0,l1,lo,hi);
          const float share=std::max(hi-lo,0.f);
          sum+=share;
          const bool handed=lod==at||lod==also;
          if(share>0.f) assert(handed);
          if(lod==at && also<0) assert(share==1.f);   // outside a fade: the one level, whole
          worst=std::max(worst,double(fabsf(share-prev[lod])));prev[lod]=share;
        }
        assert(fabsf(sum-1.f)<1e-5f);
        if(also>=0) assert(also==at+1);
      }
      assert(worst<0.06);
      // a chunk's span clear of both fades: one level, whole, everywhere in it (the block path)
      for(float a=1.f;a<entRangeOf(R,k);a*=1.07f) {
        const float b=a+300.f;
        if(entLodSpanFades(a,b,l0,l1) || entLodAt(a,l0,l1)!=entLodAt(b,l0,l1)) continue;
        for(float d=a;d<=b;d+=7.f) { float lo,hi;entLodKeep(entLodAt(a,l0,l1),d,l0,l1,lo,hi); assert(lo==0.f && hi==1.f); }
      }
      if(q==2 && !feed && (k==EK_FIR || k==EK_BUSH)) printf("detail cross-fade %s: %.0f-%.0f m and %.0f-%.0f m, worst step %.3f of a tree per 0.25%% of the distance\n",kEntInfo[k].name,l0*(1.f-kEntLodFade),l0,l1*(1.f-kEntLodFade),l1,worst);
    }
  }
}
}

int main() {
  static_assert(sizeof(EVert)==40,"environment vertex ABI changed");
  foliageFade();
  detailCrossFade();
  static_assert(sizeof(Ent)==32,"instance ABI changed");
  std::vector<EVert> v,again;EntMeshRange r[EK_COUNT],r2[EK_COUNT];
  buildEntityMeshes(v,r);buildEntityMeshes(again,r2);
  foliageRanges(r);
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
