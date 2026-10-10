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
float sign(float x){ return x>0 ? 1.f : x<0 ? -1.f : 0.f; }
// Minimal GLSL vector arithmetic used by the production-extracted projection weights.
float dot(vec2 a,vec2 b){ return a.x*b.x+a.y*b.y; }
vec2 abs(vec2 v){ return vec2(std::abs(v.x),std::abs(v.y)); }
vec2 floor(vec2 v){ return vec2(std::floor(v.x),std::floor(v.y)); }
vec2 operator/(vec2 v,vec2 d){ return vec2(v.x/d.x,v.y/d.y); }
vec2 operator*(vec2 v,vec2 d){ return vec2(v.x*d.x,v.y*d.y); }
vec2 operator+(vec2 v,float d){ return vec2(v.x+d,v.y+d); }
vec3 exp(vec3 v){ return vec3(std::exp(v.x),std::exp(v.y),std::exp(v.z)); }
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
  // Road-aligned community grid is tested in its local frame; CPU supplies the matching transform.
  for(vec2 block:{vec2(112,84),vec2(140,112),vec2(168,112)}){
    auto town=[&](float x,float z,float width){return communityStreetPaint(vec2(x,z),block,vec2(width,width));};
    near(town(0,14,.01f),1,3e-5f,"vertical street dash follows local Z");
    near(town(2,14,.01f),0,0,"street sides unpainted");
    near(town(0,8,.01f),0,0,"vertical street dash gap");
    near(town(14,0,.01f),1,3e-5f,"horizontal dash follows local X");
    near(town(14,2,.01f),0,0,"horizontal street sides unpainted");
    near(town(0,2,.01f),0,0,"intersection suppresses paint");
    near(town(0,0,.01f),0,0,"intersection has no centreline cross");
    near(town(-block.x,-10,.01f),town(0,14,.01f),3e-5f,"negative local grid keeps dash phase");
    near(town(-10,-block.y,.01f),town(14,0,.01f),3e-5f,"negative horizontal phase");
    near(town(0,14,1),.3f,1e-5f,"subpixel community paint preserves energy");
    near(communityStreetDistance(vec2(0,25),block),0,0,"street centre");
    near(communityStreetDistance(vec2(-3.5f,-20),block),3.5f,0,"negative-coordinate asphalt edge");
    near(communityStreetDistance(vec2(block.x*.5f,block.y*.5f),block),std::min(block.x,block.y)*.5f,0,"green block interior");
  }
  // Actual entity production arithmetic: flat maps preserve all axes/hemispheres, and
  // tangent perturbations never invert a surface. Cardinal walls now need 2 reads, not 6.
  for(vec3 n:normals){
    same(entityDetailNormal(n,vec3(0),1.3f),n,2e-6f,"flat entity normal preserved");
    vec3 delta(range(rng),range(rng),range(rng));
    vec3 b=entityDetailNormal(n,delta,1.3f);
    near(length(b),1,2e-6f,"unit entity detail normal");
    check(dot(b,n)>0,"detail cannot flip to the back hemisphere");
    vec3 w=entityProjectionWeights(n);
    same(w,terrainProjectionWeights(n),2e-6f,"environment projection skip matches terrain");
    check((w.x>0)+(w.y>0)+(w.z>0)>=1,"at least one valid projection");
  }
  for(vec3 n:{vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1)}){
    vec3 w=entityProjectionWeights(n);check((w.x>0)+(w.y>0)+(w.z>0)==1,"flat entity face uses 2 fetches");
  }
  for(int i=0;i<2000;i++){
    vec2 q(range(rng)*100,range(rng)*100),dx(range(rng),range(rng));
    vec2 r=waterDetailFrame(q),d=waterDetailFrame(dx);
    near(length(r),length(q),3e-5f,"water antitile rotation preserves physical scale");
    vec2 finite=waterDetailFrame(q+dx)-r;
    near(finite.x,d.x,3e-5f,"water antitile X derivative matches transformed UV");
    near(finite.y,d.y,3e-5f,"water antitile Y derivative matches transformed UV");
    vec2 ga(range(rng),range(rng)),gb(range(rng),range(rng));
    vec2 wd=waterWarpFootprint(dx,ga,gb);
    near(wd.x,dx.x+(ga.x*dx.x+ga.y*dx.y)*2.4f/19.f,2e-7f,"analytic warp X Jacobian");
    near(wd.y,dx.y+(gb.x*dx.x+gb.y*dx.y)*2.4f/31.f,2e-7f,"analytic warp Y Jacobian");
    vec2 sum=waterWarpFootprint(dx+q,ga,gb),split=waterWarpFootprint(dx,ga,gb)+waterWarpFootprint(q,ga,gb);
    near(sum.x,split.x,3e-5f,"warp footprint transform is linear X");
    near(sum.y,split.y,3e-5f,"warp footprint transform is linear Y");
  }
  vec3 extinction(.22f,.072f,.045f),last(1);
  same(waterTransmission(0,extinction),vec3(1),0,"clear zero-depth water");
  same(waterTransmission(-5,extinction),vec3(1),0,"negative depth clamped");
  for(int d=0;d<=10000;d++){
    vec3 tr=waterTransmission(d*.01f,extinction);
    check(tr.x>=0&&tr.y>=0&&tr.z>=0&&tr.z<=1,"bounded water energy");
    check(tr.x<=last.x&&tr.y<=last.y&&tr.z<=last.z,"water transmission decreases with path");
    check(tr.x<=tr.y&&tr.y<=tr.z,"red attenuates before green and blue");last=tr;
  }
  check(waterTransmission(32,extinction).x<.001f,"deep floor loses red");
  // Close room depth uses one analytic box exit. Even grazing/edge rays stay finite
  // and meet a room boundary without stepping outside the side walls or floor.
  for(int i=0;i<10000;i++){
    vec2 o(range(rng)*.499f,range(rng)*.499f);
    vec3 ray(range(rng),range(rng),-std::max(.08f,std::abs(range(rng))));
    float depth=.7f+.9f*std::abs(range(rng));
    vec3 face=entityRoomFaceDistances(o,ray,depth);
    float t=std::min(face.x,std::min(face.y,face.z));
    vec3 h=vec3(o.x,o.y,0)+ray*t;
    check(std::isfinite(t)&&t>0,"room ray exit is positive and finite");
    check(std::abs(h.x)<=.50001f&&std::abs(h.y)<=.50001f&&h.z<=.00001f&&h.z>=-depth-.00001f,"room hit stays inside box");
    check(std::min(std::abs(std::abs(h.x)-.5f),std::min(std::abs(std::abs(h.y)-.5f),std::abs(h.z+depth)))<.00002f,"room ray hits a wall");
    float footprint=std::abs(range(rng))*.2f,dist=std::abs(range(rng))*250;
    float detail=entityDetailFade(footprint,dist);
    check(detail>=0&&detail<=1,"room detail is bounded");
    check(entityDetailFade(footprint+.01f,dist)<=detail+1e-6f,"room detail fades with projected size");
    check(entityDetailFade(footprint,dist+10)<=detail+1e-6f,"room detail fades with distance");
    float lineP=range(rng);
    near(entityLine(lineP,.02f,.1f),terrainLineCoverage(lineP,.02f,.1f),1e-6f,"entity edge filtering matches tested coverage");
  }
  near(entityDetailFade(.11f,0),0,0,"subpixel rooms use mean colour");
  near(entityDetailFade(0,180),0,0,"distant rooms skip intersection");
  for(float texel:{.01f,.05f,.2f,.41015625f,.82f,2.f,10.f})for(int a=0;a<=100;a++){
    vec2 bias=environmentShadowOffsets(texel,a*.01f);
    check(bias.x>=.02f&&bias.x<=.2f&&bias.y>=.015f&&bias.y<=.1f,"contact shadow offsets stay centimetre-bounded");
    check(bias.y/6000.f<=.000016668f,"close shadow bias cannot erase a car's height");
  }
  // The actual light-matrix rows map a receiver's plane depth into each PCF tap.
  // The finite-difference reference uses world metres, independently of GLSL's
  // matrix-row scale cancellation. Grazing receivers have bounded corrections.
  for(float texel:{.05f,.2f,.41015625f,.82f})for(int i=0;i<3000;i++){
    const float radius=texel*1024.f;
    vec3 n=normalize(vec3(range(rng),range(rng),std::max(.05f,std::abs(range(rng)))));
    vec2 g=environmentShadowPlaneGradient(n,vec3(1/radius,0,0),vec3(0,1/radius,0),vec3(0,0,1.f/3000),vec2(1.f/2048,1.f/2048));
    float gx=-n.x/n.z*texel/6000,gy=-n.y/n.z*texel/6000;
    near(g.x,clamp(gx,-4.f/6000,4.f/6000),1e-9f,"shadow receiver-plane X depth agrees with world reference");
    near(g.y,clamp(gy,-4.f/6000,4.f/6000),1e-9f,"shadow receiver-plane Y depth agrees with world reference");
    check(std::abs(g.x)<=4.00001f/6000&&std::abs(g.y)<=4.00001f/6000,"receiver reference adjustment is bounded; never a uniform depth bias");
    if(std::abs(gx)<4.f/6000&&std::abs(gy)<4.f/6000){
      for(vec2 offset:{vec2(.8f,0),vec2(-.4f,.69282f),vec2(-.4f,-.69282f)}){
        vec3 delta(offset.x*texel,offset.y*texel,dot(g,offset)*6000);
        near(dot(n,delta),0,2e-7f,"PCF sample reference stays on the geometric receiver plane");
      }
    }
  }
  vec2 facing=environmentShadowPlaneGradient(vec3(0,0,1),vec3(.01f,0,0),vec3(0,.01f,0),vec3(0,0,1.f/3000),vec2(1.f/2048,1.f/2048));
  near(length(facing),0,0,"light-facing receiver needs no per-tap depth change");
  for(int i=0;i<10000;i++){
    vec2 fraction((range(rng)+1)*.5f,(range(rng)+1)*.5f),gradient(range(rng)*.0005f,range(rng)*.0005f);
    float depth=.5f,weighted=0;
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
      vec2 corner{float(x),float(y)};
      float ref=environmentShadowTapDepth(depth,gradient,fraction,corner);
      float plane=depth+gradient.x*(x-fraction.x)+gradient.y*(y-fraction.y);
      near(ref,plane,6e-8f,"each PCF reference follows the texel-centre receiver depth");
      check(ref-.0000025f<=plane,"coplanar receiver cannot shadow itself at any PCF texel phase");
      weighted+=ref*(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
    }
    near(weighted,depth,2e-7f,"bilinear corrected receiver references reconstruct the centre plane depth");
  }
  // Initial hardware PCF can be fully 0 or 1 at an occluder edge while the
  // exact receiver-plane footprint is partially lit. The close-wall gate must
  // recover both cases, and exclude terrain-like horizontal/distant receivers.
  vec3 wallNormal(0,0,1),wallSun=normalize(vec3(.2f,.8f,.5f));
  vec2 wallGradient(.00015f,.00007f),phase(.5f,.5f);
  near(environmentWallShadowFilter(wallNormal,wallSun,wallGradient,1),1,0,"sloped close wall bypasses initial PCF classification");
  near(environmentWallShadowFilter(vec3(0,1,0),wallSun,wallGradient,1),0,0,"horizontal receiver does not enter wall budget");
  near(environmentWallShadowFilter(wallNormal,wallSun,wallGradient,.5f),0,0,"distant receiver does not enter wall budget");
  near(environmentWallShadowFilter(wallNormal,-wallSun,wallGradient,1),0,0,"sun-away wall does not enter wall budget");
  near(environmentWallShadowFilter(wallNormal,wallSun,vec2(0,0),1),0,0,"light-facing plane needs no extra wall comparisons");
  for(float occluder:{.49993f,.50007f}){
    float raw=.5f-.000005f<=occluder?1.f:0.f,corrected=0;
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
      float ref=environmentShadowTapDepth(.5f,wallGradient,phase,vec2(float(x),float(y)));
      corrected+=(ref-.000005f<=occluder?.25f:0.f);
    }
    check(raw==0||raw==1,"counterexample starts fully classified by hardware PCF");
    check(corrected>0&&corrected<1,"receiver-plane footprint recovers the missing partial edge");
    float actual=environmentWallShadowFilter(wallNormal,wallSun,wallGradient,1)>.5f?corrected:raw;
    near(actual,corrected,0,"wall gate corrects both initially lit and initially shadowed edge samples");
  }
  for(float seed:{.01f,.27f,.59f,.96f}){
    int nearCount=0,farCount=0;
    for(int y=0;y<200;y++)for(int x=0;x<200;x++){
      vec2 uv((x+.5f)/200,(y+.5f)/200);
      float close=coniferShootCoverage(uv,seed,0),distant=coniferShootCoverage(uv,seed,1);
      check((close==0||close==1)&&(distant==0||distant==1),"conifer cutout is finite binary coverage");
      check(distant>=close,"subpixel conifer needles do not lose density");
      nearCount+=close>0;farCount+=distant>0;
    }
    check(nearCount>7000&&nearCount<19000,"multi-shoot spray retains 18 to 48 percent close coverage");
    check(farCount>nearCount&&farCount<26000,"distant fine sprays stay open rather than becoming solid cards");
  }
  printf("PASS: %d terrain material arithmetic checks (production-extracted GLSL; no GPU)\n",checks);
}
