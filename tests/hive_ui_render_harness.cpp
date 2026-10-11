// Actual military/range Game UI capture. Run only after the parent releases the GPU.
// UI-only pixel/layout review: production widgets/fonts, flat backdrop, no 3D scene.
#include "../src/game.h"
#include "test_world.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <memory>
static void* lib; static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib,n); }
template<class F> static F sym(const char*n){return reinterpret_cast<F>(dlsym(lib,n));}
static bool initGL(int W,int H){
  lib=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);if(!lib)return false;
  eglProc=sym<void*(*)(const char*)>("eglGetProcAddress");if(!eglProc)return false;
  auto gd=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglProc("eglGetPlatformDisplayEXT"));if(!gd)return false;
  void*d=gd(0x31DD,nullptr,nullptr);int ma,mi;
  if(!sym<unsigned(*)(void*,int*,int*)>("eglInitialize")(d,&ma,&mi))return false;
  sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};void*c;int n;
  if(!sym<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig")(d,ca,&c,1,&n)||!n)return false;
  const int sa[]={0x3057,W,0x3056,H,0x3038},at[]={0x3098,3,0x30FB,3,0x30FD,1,0x3038};
  void*s=sym<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface")(d,c,sa);
  void*x=sym<void*(*)(void*,void*,void*,const int*)>("eglCreateContext")(d,c,nullptr,at);const char*m=nullptr;
  return x&&sym<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent")(d,s,s,x)&&glLoad(proc,&m);
}

struct GameTest {
  static void setup(Game& g,const std::string& scene,float scale) {
    g.initHeadless();g.diskless=true;g.botControl=true;g.set.uiScale=scale;
    g.realTime=20;g.uiDt=1.f/60;g.in.mx=g.in.my=-100;
    g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
    if(scene=="military_hub") return;
    g.resCraft=kWraith;g.resCard=-1;g.resAirport=g.career.location;
    g.resAirborne=true;g.resWx=0;g.resTime=14;
    g.practiceForward=hive::ForwardSet::Kinetic;g.practiceBomb=hive::BombSet::EMP;g.practiceWave=3;
    if(scene=="practice_configuration") {
      g.screen=SCR_RESEARCH;g.resOpened=g.resSeqOpened=0;
      g.resSeq=10;g.resSeqAt=g.realTime;g.resAuthed=true;g.resWarm=false;
    } else {
      g.launchResearch();g.spawnPracticeWave();g.paused=true;
      g.toasts.clear();g.showRadio=false;g.settingsFromPause=false;
    }
  }
  static bool draw(Game& g,const std::string& scene) {
    g.focusList.clear();
    if(scene=="military_hub") g.drawHub();
    else if(scene=="practice_configuration") { FrameParams fp{};g.drawResearch(fp); }
    else g.drawPause();
    for(const auto& f:g.focusList) {
      if(f.x<0 || f.y<0 || f.x+f.w>g_ren.W || f.y+f.h>g_ren.H) {
        fprintf(stderr,"Offscreen widget in %s: %.1f %.1f %.1f %.1f\n",scene.c_str(),f.x,f.y,f.w,f.h);return false;
      }
    }
    return true;
  }
};
int main(int argc,char** argv) {
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<2) { fprintf(stderr,"usage: hive_ui_render_harness OUTPUT [UI_SCALE=1.0]\n");return 2; }
  const float scale=argc>2?std::strtof(argv[2],nullptr):1.f;
  if(!(scale>=.75f && scale<=1.4f))return 2;
  constexpr int W=1920,H=1080;
  const std::string root=argv[1];std::filesystem::create_directories(root);
  if(!initGL(W,H)) { fprintf(stderr,"surfaceless EGL initialization failed\n");return 1; }
  printf("Actual Game UI / native %dx%d / UI scale %.2f / GL %s\n",W,H,scale,glGetString(GL_RENDERER));
  puts("LIMIT: UI-only readability capture; 3D backdrop omitted. No flight/mesh rendering or mocked widgets.");
  buildTestWorld();buildStory();g_audio.init(48000);
  if(!g_ren.initUI(W,H)) { fprintf(stderr,"UI renderer init failed: %s\n",g_ren.error.c_str());return 1; }
  std::ofstream manifest(root+"/manifest.txt");
  manifest<<"Actual Game UI methods, production fonts, native 1920x1080, render scale 1, UI scale "<<scale<<"\n"
          <<"UI-only pixel-readability review. Flat dark backing replaces the 3D scene. No mock widgets.\n"
          <<"Practice fixtures use Kinetic burst / EMP / Archon-led mixed wave; career fixture is a fresh pilot.\n";
  for(const std::string scene:{"military_hub","practice_configuration","paused_range_controls"}) {
    auto g=std::make_unique<Game>();GameTest::setup(*g,scene,scale);
    for(int frame=0;frame<3;++frame) {
      glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,W,H);
      glClearColor(.014f,.025f,.042f,1);glClear(GL_COLOR_BUFFER_BIT);
      g_ren.uiBegin();const bool valid=GameTest::draw(*g,scene);g_ren.uiEnd();
      if(!valid)return 3;
    }
    glFinish();const GLenum error=glGetError();
    if(error!=GL_NO_ERROR) { fprintf(stderr,"GL error %u in %s\n",unsigned(error),scene.c_str());return 1; }
    const std::string path=root+"/"+scene+".png";
    if(!g_ren.screenshotPNG(path.c_str())) { fprintf(stderr,"PNG write failed: %s\n",path.c_str());return 1; }
    manifest<<scene<<".png: captured through production UI; all focusable controls within viewport\n";
    printf("CAPTURE %s\n",path.c_str());
  }
  return manifest?0:1;
}
