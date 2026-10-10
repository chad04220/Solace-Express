// Real camera/input integration, CPU only: no render, window, or GL context.
#include "../src/game.h"
#include "../src/models.h"
#include <filesystem>
struct GameTest {
  static int run() {
    g_world.build(); // updateCamera samples the real terrain under the eye
    Game g; g.initHeadless(); int fails=0, checks=0;
    auto check=[&](bool v,const char* n){++checks;if(!v){++fails;printf("FAIL: %s\n",n);}};
    g.screen=SCR_FLIGHT;g.camMode=1;g.plane.pos=vec3(0,1000,0);g.plane.q=quat();g.set.headLook=false;g.in.pad=true;g.armInputs();
    auto aimAt=[&](vec3 ray){ray=normalize(ray);g.camYaw=atan2f(-ray.x,-ray.z);g.camPitch=asinf(ray.y)+.12f;};
    for(int model=0;model<kAircraftCount;++model){
      g.plane.spec=&kAircraft[model];CockpitFocusTarget p[24];int n=modelCockpitFocusTargets(model,p,24);
      check(n>0 && n<=24,"each cockpit has bounded targets");
      for(int i=0;i<n;++i){
        vec3 aim=p[i].center-kModels[model].eye;
        check(CockpitFocusZoom::proximity(kModels[model].eye,normalize(aim),p[i])<1e-3f,"actual display center intersects its bounds");
      }
      aimAt(p[0].center-kModels[model].eye);
      for(int i=0;i<180;++i)g.updateCamera(1.f/60);
      check(fabsf(g.ckZoom-1.8f)<1e-3f,"controller look acquires actual cockpit display");
      // Zero-delta mouse drag and controller neutral produce the same viewing ray.
      g.in.pad=false;g.in.mDown[1]=true;
      for(int i=0;i<30;++i)g.updateCamera(1.f/60);
      check(fabsf(g.ckZoom-1.8f)<1e-3f,"mouse look preserves focus without projection feedback");
      aimAt(vec3(0,0,-1));
      for(int i=0;i<180;++i)g.updateCamera(1.f/60);
      check(fabsf(g.ckZoom-1)<1e-3f,"forward view releases display magnification");
      g.in.mDown[1]=false;g.in.pad=true;
    }
    g.plane.spec=&kAircraft[0];CockpitFocusTarget p[24];modelCockpitFocusTargets(0,p,24);
    aimAt(p[0].center-kModels[0].eye);
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    const float stableYaw=g.lookYaw, stablePitch=g.lookPitch;
    g.plane.q=quat::axisAngle(vec3(0,1,0),1.3f)*quat::axisAngle(vec3(0,0,1),.7f);
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    check(fabsf(g.ckZoom-1.8f)<1e-3f && g.lookYaw==stableYaw && g.lookPitch==stablePitch,"banked aircraft keeps body aim stable without auto rotation");
    g.plane.q=quat();
    const float beforeYaw=g.lookYaw;
    g.in.rx=1;
    for(int i=0;i<30;++i)g.updateCamera(1.f/60);
    g.in.rx=0;
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    check(g.lookYaw<beforeYaw-.9f && fabsf(g.ckZoom-1)<1e-3f,"real controller look input moves away and releases");
    aimAt(p[0].center-kModels[0].eye);g.in.pad=false;g.in.mDown[1]=true;
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    g.in.mdx=10;
    for(int i=0;i<40;++i)g.updateCamera(1.f/60);
    g.in.mdx=0;
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    check(fabsf(g.ckZoom-1)<1e-3f,"real mouse drag moves away and releases");
    g.in.mDown[1]=false;g.in.pad=true;
    aimAt(p[0].center-kModels[0].eye);g.ckZoomT=2.2f;
    for(int i=0;i<180;++i)g.updateCamera(1.f/60);
    check(fabsf(g.ckZoom-2.2f)<1e-3f && g.ckZoomT==2.2f,"wheel setting remains authoritative and unchanged");
    g.in.down[g.set.keyBind[ACT_ZOOM]]=true;
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    check(fabsf(g.ckZoom-2.8f)<1e-3f,"manual hold zoom remains functional");
    g.in.down[g.set.keyBind[ACT_ZOOM]]=false;
    g.camMode=0;g.updateCamera(1.f/60);
    check(g.ckZoom==1 && g.cockpitFocus.target==-1 && g.ckZoomT==2.2f,"exterior clears transient zoom but preserves wheel choice");
    g.camMode=1;g.ckZoomT=1;g.updateCamera(1.f/60);check(g.ckZoom<1.2f,"cockpit transition starts smoothly");
    g.set.cockpitFocusZoom=false;
    for(int i=0;i<120;++i)g.updateCamera(1.f/60);
    check(fabsf(g.ckZoom-1)<1e-3f,"disabled option leaves normal camera");
    // Instrument integration: exercise the real buildFrame upload, not a duplicate display formula.
    g.plane.gear=0;
    check(g.buildFrame().plane.hud2[3]==0.f,"retracted gear reaches cockpit gear annunciator");
    g.plane.gear=1;
    check(g.buildFrame().plane.hud2[3]==1.f,"extended gear reaches cockpit gear annunciator");
    g.plane.spec=&kAircraft[3]; // two engines: independent failure channels, remaining slots absent
    g.plane.fail.engineHealth[0]=.25f;g.plane.fail.engineHealth[1]=.75f;
    auto instrumentFrame=g.buildFrame();
    check(instrumentFrame.plane.engineHealth[0]==.25f && instrumentFrame.plane.engineHealth[1]==.75f,
          "independent engine health reaches live cockpit instruments");
    check(instrumentFrame.plane.engineHealth[2]==0 && instrumentFrame.plane.engineHealth[3]==0,
          "absent engine slots remain absent in cockpit upload");
    const auto dir=std::filesystem::temp_directory_path()/("solace_camera_settings_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);g.saveDir=dir.string();g.saveSettings();g.set.cockpitFocusZoom=true;g.loadSettings();
    check(!g.set.cockpitFocusZoom,"focus preference round trips through settings");
    std::filesystem::remove_all(dir);
    printf("Cockpit camera integration: %d checks, %d failures\n",checks,fails);return fails?1:0;
  }
};
int main(){return GameTest::run();}
