// Runs extracted production GLSL arithmetic with the game's vector operations. No graphics driver/context.
#include "common.h"
#include <random>
#include <stdexcept>
using std::abs;
float min(float a,float b){ return std::min(a,b); }
float max(float a,float b){ return std::max(a,b); }
float clamp(float x,float a,float b){ return clampf(x,a,b); }
float mix(float a,float b,float t){ return lerpf(a,b,t); }
float smoothstep(float a,float b,float t){ return smoothstepf(a,b,t); }
float fract(float x){ return x-std::floor(x); }
// Minimal GLSL vector arithmetic used by the production-extracted projection weights.
vec3 abs(vec3 v){ return vec3(std::abs(v.x),std::abs(v.y),std::abs(v.z)); }
vec3 pow(vec3 v,vec3 p){ return vec3(std::pow(v.x,p.x),std::pow(v.y,p.y),std::pow(v.z,p.z)); }
vec3 max(vec3 v,float f){ return vec3(std::max(v.x,f),std::max(v.y,f),std::max(v.z,f)); }
vec3 operator-(vec3 v,float f){ return vec3(v.x-f,v.y-f,v.z-f); }
vec3& operator/=(vec3& v,float f){ v=v/f;return v; }
#include "terrain_material_actual.inc"
static int checks=0;
void check(bool ok,const char* what){ ++checks; if(!ok) throw std::runtime_error(what); }
void near(float a,float b,float eps,const char* what){ check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=eps,what); }
void same(vec3 a,vec3 b,float eps,const char* what){ near(a.x,b.x,eps,what);near(a.y,b.y,eps,what);near(a.z,b.z,eps,what); }
vec3 weights(vec3 n){ vec3 w(n.x*n.x*n.x*n.x,n.y*n.y*n.y*n.y,n.z*n.z*n.z*n.z);return w/(w.x+w.y+w.z); }
int main(){
  vec3 flat(0,0,1);
  // Axes, lower hemispheres and arbitrary slopes all preserve a flat normal exactly.
  std::vector<vec3> normals={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{.6f,.8f,0},{-.6f,.8f,0},{0,.8f,.6f}};
  std::mt19937 rng(712);std::uniform_real_distribution<float> range(-1,1);
  for(int i=0;i<2000;++i) normals.push_back(normalize(vec3(range(rng),range(rng),range(rng))));
  for(vec3 n:normals){
    vec3 w=weights(n), ts=terrainTriNormal(n,flat,flat,flat,w);
    same(ts,flat,1e-6f,"flat map has zero terrain relief");
    same(applyTS(n,ts,.6f),n,1e-6f,"flat map preserves every normal");
    vec3 nx(.27f,-.31f,1),ny(-.21f,.43f,1),nz(.37f,.13f,1);
    vec3 d=vec3(0,nx.y,nx.x)*w.x+vec3(ny.x,0,ny.y)*w.y+vec3(nz.x,nz.y,0)*w.z;
    vec3 expected=normalize(n+(d-n*dot(n,d))*.6f);
    same(applyTS(n,terrainTriNormal(n,nx,ny,nz,w),.6f),expected,2e-6f,"projection normals reorient into world tangent plane");
  }
  for(float z:{.89999f,.90001f,-.89999f,-.90001f}){
    vec3 n=normalize(vec3(std::sqrt(1-z*z),0,z)),w=weights(n);
    vec3 ts=terrainTriNormal(n,vec3(.2f,.3f,1),vec3(.4f,.1f,1),vec3(.1f,-.2f,1),w);
    check(std::isfinite(length(applyTS(n,ts,.6f))),"terrain frame switch is finite");
  }
  // Keep v3.39's texture budget: one projection on axes, three on a diagonal, continuous threshold.
  for(vec3 n:normals){
    vec3 w=terrainProjectionWeights(n),base=weights(n);
    vec3 ref(std::max(base.x-.02f,0.f),std::max(base.y-.02f,0.f),std::max(base.z-.02f,0.f));
    ref=ref/(ref.x+ref.y+ref.z);
    same(w,ref,2e-6f,"release threshold and renormalization preserved");
    near(w.x+w.y+w.z,1,2e-7f,"surviving projection weights sum to one");
    same(applyTS(n,terrainTriNormal(n,flat,flat,flat,w),.6f),n,1e-6f,"skipped projections preserve flat normal");
  }
  for(vec3 n:{vec3(1,0,0),vec3(0,-1,0),vec3(0,0,1)}){
    vec3 w=terrainProjectionWeights(n);
    check((w.x>0)+(w.y>0)+(w.z>0)==1,"axis uses exactly one projection");
  }
  vec3 diagonal=terrainProjectionWeights(normalize(vec3(1,1,1)));
  same(diagonal,vec3(1.f/3),1e-7f,"diagonal retains all three projections");
  for(float x:{.01999f,.02f,.02001f}){
    vec3 n(std::pow(x,.25f),std::pow(1-x,.25f),0),w=terrainProjectionWeights(n);
    check(x>.02f ? w.x>0 : w.x<1e-7f,"2 percent threshold skips negligible projections");
    near(w.x,std::max(x-.02f,0.f)/(std::max(x-.02f,0.f)+1-x-.02f),2e-7f,"threshold fade stays continuous");
  }
  // Rotated tile gradients are the coordinate Jacobian's transpose; packed height/AO never enter this helper.
  for(int i=0;i<2000;++i){
    vec2 q(range(rng),range(rng)),r=groundRotatedNormal(q);
    near(r.x,.8253f*q.x+.5646f*q.y,1e-7f,"rotated gradient X");
    near(r.y,-.5646f*q.x+.8253f*q.y,1e-7f,"rotated gradient Y");
    near(length(r),length(q)*std::sqrt(.8253f*.8253f+.5646f*.5646f),3e-7f,"rotation preserves gradient length up to original UV scale");
  }
  vec2 zero=groundRotatedNormal(vec2(0,0));near(length(zero),0,0,"flat rotated sample");
  float prior=.6f;
  for(int i=0;i<=50000;++i){
    float d=i*.1f,v=terrainBumpStrength(d);check(v<=prior+1e-7f&&v>=.25f-1e-7f&&v<=.6f+1e-7f,"bounded continuous distance fade");prior=v;
  }
  near(terrainBumpStrength(1600),.6f,0,"close relief unchanged");near(terrainBumpStrength(2400),.25f,1e-7f,"far relief unchanged");
  near(terrainBumpStrength(1999.99f),terrainBumpStrength(2000.01f),2e-5f,"no former 2km step");
  near(terrainDetailWeight(.25f),1,0,"fully resolved groove unchanged");near(terrainDetailWeight(1),0,0,"unresolved groove removed");
  prior=1;
  for(int i=0;i<=2000;++i){float v=terrainDetailWeight(i*.001f);check(v<=prior+1e-7f&&v>=0&&v<=1,"smooth derivative-based microdetail fade");prior=v;}
  // Compare the production line filter with a double-precision interval intersection reference.
  std::uniform_real_distribution<float> positive(.0001f,100.f);
  for(int i=0;i<10000;++i){
    float p=range(rng)*100,hw=positive(rng)*.1f,width=positive(rng);
    double lo=std::max(double(p)-double(width)*.5,-double(hw)),hi=std::min(double(p)+double(width)*.5,double(hw));
    float ref=float(std::clamp((hi-lo)/width,0.,1.));
    near(terrainLineCoverage(p,hw,width),ref,1e-5f,"line coverage agrees with interval reference");
    near(terrainLineCoverage(p,hw,width),terrainLineCoverage(-p,hw,width),1e-6f,"line coverage symmetric");
  }
  near(terrainLineCoverage(0,.11f,10),.022f,1e-7f,"subpixel line energy retained rather than fattened");
  near(terrainLineCoverage(0,.45f,.001f),1,1e-5f,"close runway centreline unchanged");
  near(terrainLineCoverage(2,.45f,.001f),0,0,"outside close line unchanged");
  for(float duty:{.02f,.12f,.3f,.5f,.6f}) for(float width:{1.f,2.f,8.f,100.f}) for(int phase=0;phase<100;++phase){
    float p=phase*.01f;near(terrainStripeCoverage(p,duty,width),duty,1e-6f,"integer-width stripe footprint equals mean coverage");
  }
  for(int i=0;i<10000;++i){
    float p=range(rng)*100,width=positive(rng),duty=(range(rng)+1)*.5f;
    float a=terrainStripeCoverage(p,duty,width);check(a>=0&&a<=1&&std::isfinite(a),"stripe filter bounded");
    near(a,terrainStripeCoverage(p+8,duty,width),5e-4f,"stripe periodic for positive/negative world coordinates");
  }
  near(terrainStripeCoverage(.25f,.5f,.01f),1,2e-6f,"resolved stripe retained");
  near(terrainStripeCoverage(.75f,.5f,.01f),0,0,"resolved gap retained");
  near(terrainStripeCoverage(-1e-6f,.5f,.1f),terrainStripeCoverage(1e-6f,.5f,.1f),3e-5f,"no fract-boundary jump");
  // Same untouched 28 m lot grid as terrainMaterial, including GLSL mod for negative coordinates.
  auto town=[](float x,float z,float width){
    float ex=std::abs(fract(x/28.f)-.5f)*28.f, ez=std::abs(fract(z/28.f)-.5f)*28.f;
    float ix=std::floor(x/28.f+.5f),iz=std::floor(z/28.f+.5f);
    float sx=ix-3.f*std::floor(ix/3.f)==0 ? ex : 0,sz=iz-2.f*std::floor(iz/2.f)==0 ? ez : 0;
    return terrainTownPaint(vec2(x,z),sx,sz,vec2(width,width));
  };
  near(town(84,14,.01f),1,3e-5f,"vertical street dash follows Z");
  near(town(82,14,.01f),0,0,"vertical street sides are unpainted");
  near(town(84,8,.01f),0,0,"vertical street dash gap");
  near(town(14,56,.01f),1,3e-5f,"horizontal street dash follows X");
  near(town(14,54,.01f),0,0,"horizontal street sides are unpainted");
  near(town(84,2,.01f),0,0,"vertical street at crossing road suppresses paint");
  near(town(2,56,.01f),0,0,"horizontal street at crossing road suppresses paint");
  near(town(84,56,.01f),0,0,"intersection suppresses both centerlines");
  near(town(-84,-10,.01f),town(84,14,.01f),3e-5f,"negative X grid keeps dash phase");
  near(town(-10,-56,.01f),town(14,56,.01f),3e-5f,"negative Z grid keeps dash phase");
  near(town(84,14,1),.3f,1e-5f,"town paint retains fractional subpixel coverage");
  printf("PASS: %d terrain material arithmetic checks (production-extracted GLSL; no GPU)\n",checks);
}
