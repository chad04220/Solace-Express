#include "../src/cockpit_focus_zoom.h"
#include <cstdio>
static int checks=0, failures=0;
static void check(bool v,const char* name){++checks;if(!v){++failures;std::printf("FAIL: %s\n",name);}}
static const CockpitFocusTarget panels[] = {
  {vec3(-.12f,-.4f,-1),vec3(0,0,1),vec2(.16f,.1f)},
  {vec3(.12f,-.4f,-1),vec3(0,0,1),vec2(.16f,.1f)}
};
static float step(CockpitFocusZoom& z, vec3 aim, float dt=1.f/60, float manual=1.f, bool enabled=true, bool cockpit=true,int model=0){
  return z.update(dt,cockpit,model,enabled,vec3(0),normalize(aim),panels,2,manual);
}
int main(){
  CockpitFocusZoom z;
  check(step(z,panels[0].center)<1.2f,"acquisition is gradual");
  for(int i=0;i<120;++i)step(z,panels[0].center);
  check(z.target==0 && fabsf(z.zoom-1.8f)<1e-4f,"center acquires full bounded zoom");
  float full=z.zoom;step(z,vec3(0,0,-1));
  check(z.zoom<full && z.zoom>1.5f,"look away releases smoothly");
  for(int i=0;i<120;++i)step(z,vec3(0,0,-1));
  check(fabsf(z.zoom-1.f)<1e-4f,"release returns to baseline");
  check(CockpitFocusZoom::proximity(vec3(0),normalize(-panels[0].center),panels[0])>1e5f,"behind-eye targets rejected");
  step(z,panels[0].center,1.f);
  for(int i=0;i<120;++i)step(z,vec3((i%2?1:-1)*.001f,-.4f,-1));
  check(z.target==0,"nearby screens do not flicker at bisector");
  step(z,panels[1].center,.1f);check(z.target==1,"deliberate move acquires neighbor");
  for(int i=0;i<120;++i)step(z,panels[1].center + vec3(.003f,.002f,0));
  check(fabsf(z.zoom-1.8f)<1e-4f,"center dead zone suppresses aim jitter");
  CockpitFocusZoom a,b;for(int i=0;i<30;++i)step(a,panels[0].center,1.f/30);for(int i=0;i<144;++i)step(b,panels[0].center,1.f/144);
  check(fabsf(a.zoom-b.zoom)<1e-5f,"acquisition is dt independent");
  for(int i=0;i<30;++i)step(a,vec3(0,0,-1),1.f/30);for(int i=0;i<144;++i)step(b,vec3(0,0,-1),1.f/144);
  check(fabsf(a.zoom-b.zoom)<1e-5f,"release is dt independent");
  for(int i=0;i<120;++i)step(z,panels[0].center,1.f/60,2.8f);
  check(fabsf(z.zoom-2.8f)<1e-4f,"manual hold wins rather than multiplying zoom");
  for(int i=0;i<120;++i)step(z,vec3(0,0,-1),1.f/60,1.44f);
  check(fabsf(z.zoom-1.44f)<1e-4f,"look away preserves wheel zoom");
  step(z,panels[0].center,1.f,4);check(z.zoom<=4.f,"total zoom remains bounded");
  float prior=z.zoom;step(z,panels[0].center,1.f/60,1,false);
  check(z.zoom<prior && z.zoom>1.f && z.target==-1,"off toggle smoothly releases");
  step(z,panels[0].center,1.f/60,1,true,false);check(z.zoom==1.f && z.target==-1,"outside clears auto state");
  check(step(z,panels[1].center)<1.2f,"cockpit reentry has no stale magnification");
  step(z,panels[1].center,2);check(step(z,panels[0].center,1.f/60,1,true,true,1)<1.2f,"aircraft change clears state");
  // No input projection or prior magnification is passed to proximity: unchanged body aim stays unchanged.
  float r=CockpitFocusZoom::proximity(vec3(0),normalize(panels[0].center+vec3(.08f,0,0)),panels[0]);
  for(int i=0;i<240;++i)step(z,panels[0].center+vec3(.08f,0,0));
  check(fabsf(r-.5f)<1e-5f && fabsf(r-CockpitFocusZoom::proximity(vec3(0),normalize(panels[0].center+vec3(.08f,0,0)),panels[0]))<1e-6f,"aim score independent of zoom state");
  CockpitFocusTarget horizontal{vec3(0,-.6f,-.2f),vec3(0,1,0),vec2(.12f,.1f)};
  check(CockpitFocusZoom::proximity(vec3(0),normalize(horizontal.center),horizontal)<1e-5f,"horizontal console center supported");
  CockpitFocusTarget tilted{vec3(.3f,-.4f,-.8f),normalize(vec3(-.2f,.5f,1)),vec2(.15f,.1f)};
  check(CockpitFocusZoom::proximity(vec3(0),normalize(tilted.center),tilted)<1e-5f,"canted panel center supported");
  vec3 eye(.2f,.8f,1.4f); tilted.center += eye;
  check(CockpitFocusZoom::proximity(eye,normalize(tilted.center-eye),tilted)<1e-5f,"body-space eye offset supported");
  // Hold each sampled ray until fully settled: these assertions verify the *spatial* transfer
  // function, independent of the temporal smoothing used while the player moves their view.
  for(const auto& panel : {panels[0], horizontal, tilted}) {
    const vec3 origin = panel.center.z > 0 ? eye : vec3(0);
    const vec3 n=normalize(panel.normal);
    vec3 right=normalize(cross(vec3(0,1,0),n));if(length(right)<.5f)right=vec3(1,0,0);
    const vec3 up=cross(n,right), centerRay=normalize(panel.center-origin);
    for(int spoke=0;spoke<8;++spoke) {
      const float angle=spoke*PI/4.f;
      float previousZoom=1.f, previousAngle=PI;
      for(int sample=110;sample>=0;--sample) {
        const float radius=sample*.01f;
        const vec3 hit=panel.center+right*(cosf(angle)*panel.half.x*radius)+up*(sinf(angle)*panel.half.y*radius);
        const vec3 aim=normalize(hit-origin);
        const float aimAngle=atan2f(length(cross(aim,centerRay)),dot(aim,centerRay));
        CockpitFocusZoom sampleState;
        const float value=sampleState.update(10.f,true,0,true,origin,aim,&panel,1,1.f);
        const float expected=1.f+.8f*(1.f-smoothstepf(.12f,1.f,radius));
        check(fabsf(value-expected)<2e-5f,"settled magnification follows continuous proximity curve");
        check(value+2e-5f>=previousZoom,"approaching exact display center never reduces zoom");
        check(aimAngle<=previousAngle+2e-5f,"sampled angular separation decreases toward display center");
        check(value-previousZoom<.014f,"dense spatial samples have no on-off jump");
        if(sample>12 && sample<100)check(value>previousZoom+1e-6f,"every interior distance step progressively increases zoom");
        previousZoom=value;previousAngle=aimAngle;
      }
    }
  }
  // Center-core/outer-edge values meet smoothly, rather than jumping at acquisition/release.
  auto settledSingle=[&](float r){CockpitFocusZoom q;return q.update(10,true,0,true,vec3(0),
      normalize(panels[0].center+vec3(panels[0].half.x*r,0,0)),panels,1,1);};
  check(fabsf(settledSingle(1.0001f)-settledSingle(.9999f))<1e-6f,"outer acquisition edge is spatially continuous");
  check(fabsf(settledSingle(.1201f)-settledSingle(.1199f))<1e-6f,"central dead-zone joins curve continuously");
  check(settledSingle(.3f)>settledSingle(.6f)+.2f && settledSingle(.6f)>settledSingle(.9f)+.2f,"different focal distances yield distinct stable magnifications");
  // Cross overlapping displays in both directions. Hysteresis can retain different identities,
  // but must not affect the desired level or create a spatial step at a later identity switch.
  CockpitFocusZoom fromLeft,fromRight;
  step(fromLeft,panels[0].center,10);step(fromRight,panels[1].center,10);
  float lv=step(fromLeft,vec3(0,-.4f,-1),10),rv=step(fromRight,vec3(0,-.4f,-1),10);
  check(fromLeft.target!=fromRight.target && fabsf(lv-rv)<1e-6f,"identity hysteresis preserves the same continuous zoom envelope");
  float previousLeft=1,previousRight=1;
  for(int sample=0;sample<=700;++sample){
    float x=-.35f+sample*.001f;
    lv=step(fromLeft,vec3(x,-.4f,-1),10);rv=step(fromRight,vec3(-x,-.4f,-1),10);
    CockpitFocusZoom clean;
    float cleanValue=step(clean,vec3(x,-.4f,-1),10);
    check(fabsf(lv-cleanValue)<1e-6f,"settled envelope is independent of retained target identity");
    check(fabsf(lv-previousLeft)<.009f && fabsf(rv-previousRight)<.009f,"overlapping-target boundary has no spatial magnification jump");
    previousLeft=lv;previousRight=rv;
  }
  std::printf("Cockpit focus zoom: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
