// DISABLED EXPERIMENT: not part of the production renderer or CMake targets.
// See README.md for the rejected coverage tradeoff and standalone-only use.
// Quantized near-shadow policy, handover, invalid inputs and view isolation.
#include "near_shadow_coverage.h"
#include "entity_lod.h"
#include "ground_vehicle.h"
#include <limits>
#include <array>
#include <fstream>
#include <iterator>
#include <cctype>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", what); } }
struct Sim {
  NearShadowCoverage state;
  vec3 camera{0, 1000, 0};
  float dt = 1.f / 60.f, base = 420.f, ground = 998.f;
  int reads = 0, commits = 0;
  bool step(bool main = true, bool hangar = false) {
    bool changed = state.update(camera, dt, base, main, hangar, [&](float x, float z) {
      check(x == camera.x && z == camera.z, "terrain sampled at main camera"); ++reads; return ground;
    });
    commits += changed;
    if (state.radius() > 0.f) {
      vec2 f = state.fadeRadii(state.radius());
      check(std::isfinite(f.x) && f.x > 0.f && f.y > f.x, "finite positive ordered fade bounds");
      check(f.x <= .5f*state.radius() && f.y <= .78f*state.radius(), "fade never leaves map-safe radius");
      check(state.radius() == 96.f || state.radius() == 160.f || state.radius() == 256.f || state.radius() == 300.f || state.radius() == 420.f || state.radius() == 520.f,
            "committed radius is a discrete tier");
      if (changed && state.phase() == NearShadowCoverage::Phase::Expand)
        check(state.fadeScale() == NearShadowCoverage::kFadeFloor, "radius commit occurs at hidden handover frame");
    }
    return changed;
  }
  void run(float seconds, float speed = 0.f) {
    const int n = int(std::ceil(seconds/dt));
    for (int i=0;i<n;++i) { camera.x += speed*dt; step(); }
  }
  void setAGL(float agl) { ground = camera.y - agl; }
  void settle(float agl = 2.f) { setAGL(agl); run(8.f); }
};

void qualityAndHandover() {
  for (int quality=0;quality<3;++quality) {
    Sim s; s.base=entBaseRangesFor(quality).sh0; s.step();
    check(s.state.radius()==s.base,"initial radius is quality base");
    s.run(1.4f);check(s.state.radius()==s.base,"downgrade dwell prevents early rebuild");
    s.run(1.f);check(s.state.radius()==96.f&&s.state.fadeScale()==1.f,"stationary low camera settles to 96");
    check(s.commits==2,"one initialization and one committed near rebuild");
    const int commits=s.commits;
    s.run(20.f);check(s.commits==commits,"static settled camera never churns radius");
    s.setAGL(190);s.run(2.f);
    check(s.state.radius()==s.base&&s.state.fadeScale()==1.f,"altitude restores every quality base");
    s.settle();s.run(3.f,70.f);
    check(s.state.radius()==s.base,"main-camera speed restores every quality base");
    s.settle();check(s.state.radius()==96.f,"slow camera recovers close quality after speed decays");
  }
}

void jitterAndDwell() {
  const float centers[3]={30.f,70.f,142.f};
  const float expected[3]={96.f,160.f,256.f};
  for(int t=0;t<3;++t) {
    Sim s;s.settle(t==0?2.f:t==1?45.f:95.f);
    check(s.state.radius()==expected[t],"establish each non-base tier");
    s.setAGL(centers[t]);s.run(2.f);const int before=s.commits;
    for(int n=0;n<1800;++n) {s.setAGL(centers[t]+std::sin(float(n)*.1f)*.5f);s.step();}
    check(s.commits==before,"stationary threshold hysteresis prevents jitter churn");
  }
  for(int t=0;t<3;++t) {
    Sim s;s.settle(t==0?2.f:t==1?45.f:95.f);const int before=s.commits;
    const float edge[3]={35.f,80.f,160.f};
    for(int n=0;n<600;++n){s.setAGL(edge[t]+((n/5)%2?.25f:-.25f));s.step();}
    check(s.commits==before,"stationary jitter across upgrade edge cannot accumulate dwell");
  }
  for(int t=0;t<3;++t) {
    Sim s;s.settle(t==0?45.f:t==1?95.f:190.f);const int before=s.commits;
    const float edge[3]={25.f,60.f,125.f};
    for(int n=0;n<600;++n){s.setAGL(edge[t]+((n/6)%2?.25f:-.25f));s.step();}
    check(s.commits==before,"stationary jitter across downgrade edge cannot accumulate dwell");
  }
  Sim s;s.settle();int before=s.commits;
  // Threshold crossings shorter than the0.25s dwell must not accumulate.
  for(int n=0;n<10;++n){s.setAGL(36);s.run(.1f);s.setAGL(34);s.run(.4f);}
  check(s.commits==before,"interrupted upgrade dwell resets instead of accumulating");
  s.setAGL(42);s.run(1.5f);check(s.state.radius()==160.f,"sustained threshold restores next tier");
  s.setAGL(24);s.run(1.f);check(s.state.radius()==160.f,"downgrade requires 1.5s continuous dwell");
  s.run(2.f);check(s.state.radius()==96.f,"sustained low altitude downgrades");
}

std::array<float,3> motionAtRate(int hz) {
  Sim s;s.dt=1.f/hz;s.settle();
  std::array<float,3> times{};const float target[3]={160,256,420};
  for(int n=1;n<=hz*14;++n){
    const float t=float(n)/hz;const float v=std::min(t*8.f,75.f);
    s.camera.x+=v*s.dt;s.step();
    for(int k=0;k<3;++k)if(!times[k]&&s.state.radius()==target[k])times[k]=t;
  }
  for(float t:times)check(t>0,"speed ramp visits every radius tier");
  check(s.state.radius()==420.f,"speed ramp returns to base");
  s.run(6.f);check(s.state.radius()==96.f,"camera stopping returns to close tier");
  return times;
}
std::array<float,3> altitudeAtRate(int hz) {
  Sim s;s.dt=1.f/hz;s.settle();std::array<float,3> times{};
  const float target[3]={160,256,420};
  for(int n=1;n<=hz*14;++n){
    const float t=float(n)/hz;s.setAGL(std::min(2.f+t*20.f,210.f));s.step();
    for(int k=0;k<3;++k)if(!times[k]&&s.state.radius()==target[k])times[k]=t;
  }
  for(float t:times)check(t>0,"AGL ramp visits every radius tier");
  check(s.state.radius()==420.f,"high AGL restores base without camera motion");
  return times;
}
void rates() {
  auto a=motionAtRate(30),b=motionAtRate(60),c=motionAtRate(144);
  for(int i=0;i<3;++i){check(std::fabs(a[i]-c[i])<.12f,"30/144Hz ramp commitment within frame quantization");check(std::fabs(b[i]-c[i])<.07f,"60/144Hz ramp commitment within frame quantization");}
  a=altitudeAtRate(30);b=altitudeAtRate(60);c=altitudeAtRate(144);
  for(int i=0;i<3;++i){check(std::fabs(a[i]-c[i])<.10f,"30/144Hz AGL ramp commitment within frame quantization");check(std::fabs(b[i]-c[i])<.06f,"60/144Hz AGL ramp commitment within frame quantization");}

}

void inputsAndIsolation() {
  Sim s;s.settle();const float nan=std::numeric_limits<float>::quiet_NaN();
  for(float dt:{0.f,-.01f,nan,std::numeric_limits<float>::infinity(),.5f}) {
    s.dt=dt;s.step();check(s.state.radius()==s.base&&s.state.speed()==0.f,"invalid dt resets conservatively");
    s.dt=1.f/60;s.settle();check(s.state.radius()==96.f,"valid samples recover after invalid dt");
  }
  s.dt=std::numeric_limits<float>::denorm_min();s.camera.x+=1.f;
  int tinyReads=s.reads;s.step();
  check(s.state.radius()==s.base&&s.state.speed()==0&&tinyReads==s.reads,"overflowing camera speed from tiny positive dt resets before terrain sampling");
  s.dt=1.f/60;s.settle();
  s.camera.x+=1000;s.step();check(s.state.radius()==s.base&&s.state.speed()==0.f,"teleport clears speed and restores base");
  s.settle();s.camera.x=nan;int reads=s.reads;s.step();check(s.reads==reads,"nonfinite camera never reaches terrain sampler");
  s.camera={0,1000,0};s.settle();s.ground=nan;s.step();check(s.state.radius()==s.base&&std::isfinite(s.state.speed()),"NaN ground cannot poison state");
  s.ground=998;s.settle();s.base=520;s.step();check(s.state.radius()==520&&s.state.speed()==0,"quality change clears history");
  s.settle();s.state.reset();s.step();check(s.state.radius()==520&&s.state.speed()==0,"temporal reset clears history");
  // An ocean's seabed must not inflate camera altitude above the visible water.
  s.ground=-300;s.camera.y=2;s.run(8.f);check(s.state.radius()==96&&s.state.agl()==2,"AGL uses sea level over water");
  s.ground=900;s.camera.y=902;s.run(8.f);check(s.state.radius()==96&&s.state.agl()==2,"AGL uses local ground on mountains");
  s.ground=910;s.run(3.f);check(s.state.agl()==0,"below-ground AGL is clamped");
  // Freeze even in an active handover, with hostile feed data and quality.
  s.ground=998;s.camera={0,1000,0};s.state.reset();s.step();s.run(1.6f);
  check(s.state.phase()==NearShadowCoverage::Phase::Shrink,"isolation test is inside handover");
  auto copy=s.state;reads=s.reads;int commits=s.commits;
  for(int i=0;i<400;++i){s.camera={nan,nan,nan};s.base=300;s.dt=nan;s.step(false,false);s.step(true,true);}
  check(reads==s.reads&&commits==s.commits,"feeds/hangar never sample terrain or commit radius");
  check(s.state.radius()==copy.radius()&&s.state.speed()==copy.speed()&&s.state.phase()==copy.phase()&&s.state.fadeScale()==copy.fadeScale(),"feeds/hangar freeze main controller state");
  s.camera={0,1000,0};s.base=520;s.dt=1.f/60;
  for(int i=0;i<900;++i) {
    s.camera.x+=s.dt*(i<300?22.f:0.f);
    const bool a=s.step();
    const bool b=copy.update(s.camera,s.dt,s.base,true,false,[&](float,float){return s.ground;});
    check(a==b&&s.state.radius()==copy.radius()&&s.state.speed()==copy.speed()&&s.state.agl()==copy.agl()&&s.state.phase()==copy.phase()&&s.state.fadeScale()==copy.fadeScale(),"post-feed trajectory equals untouched controller including private history");
  }
  for(float base:{nan,-10.f,0.f,255.f}) {
    NearShadowCoverage c;
    c.update({0,2,0},1.f/60,base,true,false,[](float,float){return 0.f;});
    check(c.radius()==420.f&&c.speed()==0.f,"invalid quality base uses conservative medium radius");
  }

}

void fadeEndpointsAndCache() {
  for(int hz:{30,60,144}) {
    Sim s;s.dt=1.f/hz;s.step();
    for(int n=0;n<hz*2&&s.state.phase()==NearShadowCoverage::Phase::Stable;++n)s.step();
    check(s.state.phase()==NearShadowCoverage::Phase::Shrink&&s.state.fadeScale()==1.f,"shrink begins at full footprint");
    float previous=1.f;int nearRebuilds=0;float actual=s.state.radius();
    for(int n=0;n<hz;++n) {
      const auto phase=s.state.phase();s.step();
      if(phase==NearShadowCoverage::Phase::Shrink)check(s.state.fadeScale()<=previous,"shrink fade monotonically contracts");
      if(phase==NearShadowCoverage::Phase::Expand)check(s.state.fadeScale()>=previous,"expand fade monotonically recovers");
      if(actual!=s.state.radius()){++nearRebuilds;actual=s.state.radius();}
      // The production radius mismatch invalidation compares actual map radius
      // to committed cR; fading is only a uniform update, never another rebuild.
      const auto f=s.state.fadeRadii(actual);
      check(f.y<=actual*.78f&&actual*2.f/2048.f>0,"fade and texel size use the actual committed map");
      previous=s.state.fadeScale();
    }
    check(nearRebuilds==1,"handover commits exactly one near radius");
    check(s.state.phase()==NearShadowCoverage::Phase::Stable&&s.state.fadeScale()==1.f,"expand endpoint is exact full footprint");
  }
}

void fastTakeoffDuringExpansion() {
  for(int hz:{30,60,144}) {
    Sim s;s.dt=1.f/hz;s.settle();s.setAGL(42);
    for(int n=0;n<hz*2&&s.state.phase()!=NearShadowCoverage::Phase::Expand;++n)s.step();
    check(s.state.radius()==160&&s.state.phase()==NearShadowCoverage::Phase::Expand,"takeoff test begins during smaller-map expansion");
    s.setAGL(190);float elapsed=0;
    while(elapsed<1.25f&&s.state.radius()!=s.base){s.camera.x+=75.f*s.dt;s.step();elapsed+=s.dt;}
    check(s.state.radius()==s.base&&elapsed<.85f,"fast takeoff restores base within current expand plus upgrade dwell and shrink");
    check(s.state.phase()==NearShadowCoverage::Phase::Expand&&s.state.fadeScale()==NearShadowCoverage::kFadeFloor,"fast takeoff also commits at hidden handover");
  }
}

void transitionSafetyAndKeys() {
  Sim s;s.step();s.run(1.6f);check(s.state.phase()==NearShadowCoverage::Phase::Shrink,"starts handover after dwell");
  // Rise without a camera-position jump by lowering terrain during the shrink.
  const int before=s.commits;
  s.setAGL(190);s.run(.1f);
  check(s.state.radius()==s.base&&s.state.phase()==NearShadowCoverage::Phase::Expand&&s.state.fadeScale()==NearShadowCoverage::kFadeFloor,"rising during shrink cancels unsafe narrowing at hidden handover");
  check(s.commits==before&&s.state.agl()==190&&s.state.speed()==0,"cancelled narrowing neither resets camera nor rebuilds unchanged map");
  s.run(.4f);
  s.settle();
  std::vector<GroundVehicleVisual> v(1);v[0].kind=EK_CAR;v[0].entity={300,0,0,0,1,1,1,.5f};
  const vec3 center{};const float reach=1.f;
  auto empty=groundVehicleShadowKey({},center,s.state.radius(),reach);
  check(groundVehicleShadowKey(v,center,s.state.radius(),reach)==empty,"outside-small-radius dynamic vehicle cannot invalidate near map");
  check(groundVehicleShadowKey(v,center,s.base,reach)!=empty,"base-radius vehicle key includes same distant vehicle");
  const uint64_t far=groundVehicleShadowKey(v,center,2600.f,reach);
  const float actual=s.state.radius();s.setAGL(42);s.run(.35f);
  check(s.state.radius()==actual&&s.state.fadeScale()<1.f,"fade changes do not change culling footprint");
  check(groundVehicleShadowKey(v,center,s.state.radius(),reach)==empty,"shrinking fade does not change actual-radius vehicle keys");
  check(groundVehicleShadowKey(v,center,2600.f,reach)==far,"near policy leaves far vehicle keys unchanged");
}

std::string compact(std::string s) {
  s.erase(std::remove_if(s.begin(),s.end(),[](unsigned char c){return std::isspace(c);}),s.end());return s;
}
std::string sourceFile(const std::string& path) {
  std::ifstream f(path);check(bool(f),"source contract input is readable");
  return compact(std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()));
}
void sourceContract(const std::string& root) {
  const auto entity=sourceFile(root+"/src/entity_render.cpp");
  const auto renderer=sourceFile(root+"/src/renderer.cpp");
  const auto header=sourceFile(root+"/src/renderer.h");
  auto has=[&](const std::string& s,const char* needle,const char* why){check(s.find(compact(needle))!=std::string::npos,why);};
  has(entity,"nearShadowCoverage.update(cam, fp.dt, shadowRanges.sh0, !feedPass, fp.hangarPreview,","main-camera dt and feed/hangar guard own adaptive update");
  has(entity,"[](float x, float z) { return g_world.groundHeight(x, z); }","main-camera terrain sample is lazy");
  has(entity,"float cR[2] = {R.sh0, R.sh1};","far radius still uses original quality policy");
  has(entity,"cR[0] = nearShadowCoverage.radius() > 0.f ? nearShadowCoverage.radius() : R.sh0;","only near candidate radius uses committed controller state");
  check(entity.find("cR[0]=nearShadowCoverage.radius()")<entity.find("autoadded="),"committed radius is available before new-chunk invalidation");
  has(entity,"float r = cR[c] * kShFade1 + std::max(300.f, chunkShPad(g_scenery.get(cx, cz)));","new chunks invalidate actual committed coverage");
  has(entity,"shR[c] != cR[c]","map radius mismatch invalidates the selected cascade once");
  has(entity,"moved > cR[c] * 0.12f","coverage-safe movement refresh threshold is retained");
  has(entity,"groundVehicleShadowKey(fp.groundVehicles,center,cR[c],shReach)","dynamic vehicle keys use actual committed culling radius");
  has(entity,"float r = cR[c] + std::max(210.f, chunkShPad(ch));","chunk caster culling uses committed coverage");
  has(entity,"float cr = cR[c] * kShFade1 + er;","instance caster culling uses committed coverage");
  has(entity,"std::fabs(e.x-newCenter[c].x)<cR[c]+pad","dynamic vehicle caster culling uses committed coverage");
  has(entity,"float texel = 2.f * cR[c] / shRes;","raster grid snapping uses committed map radius");
  has(entity,"shR[c] = cR[c];","map rebuild records actual raster radius");
  has(entity,"shRes = R.shRes;","quality map allocation is unchanged");
  has(renderer,"glUniform2f(U(p, \"uShTexel\"), shR[0] * 2.f / std::max(shRes, 1), shR[1] * 2.f / std::max(shRes, 1));","texel uniforms use actual raster radii for both cascades");
  has(renderer,"const vec2 nearFade = nearShadowCoverage.fadeRadii(shR[0]);","near fade derives from actual map radius");
  has(renderer,"glUniform4f(U(p, \"uShFadeR\"), nearFade.x, nearFade.y, shR[1] * kShFade0, shR[1] * kShFade1);","handover affects only near fade bounds; far fade is unchanged");
  has(header,"static constexpr float kShFade0 = 0.5f, kShFade1 = 0.78f;","controller and renderer share map-safe fade constants");
  has(header,"NearShadowCoverage nearShadowCoverage;","adaptive state is per renderer");
  has(header,"nearShadowCoverage.reset();","temporal resets also clear main-camera speed history");
}
}
int main(int argc,char**argv){qualityAndHandover();jitterAndDwell();rates();inputsAndIsolation();fadeEndpointsAndCache();fastTakeoffDuringExpansion();transitionSafetyAndKeys();if(argc>1)sourceContract(argv[1]);else printf("Source wiring contract skipped: supply repository root.\n");printf("near_shadow_coverage_test: %d checks, %d failures\n",checks,failures);return failures?1:0;}

