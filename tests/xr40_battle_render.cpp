// Actual XR-40 product capture: UI-only by default, production 3D with "scene".
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
  static void setup(Game& g) { g.battleOnly=true;g.diskless=true;g.initHeadless();g.initBattle();g.realTime=20;g.uiDt=1.f/60;g.in.mx=g.in.my=-100; }
  static void draw(Game& g,int screen) {
    g.focusList.clear();
    if(screen==0)g.drawBattleMenu();
    else if(screen==1)g.drawBattlePause();
    else if(screen==2)g.drawBattleDebrief();
    else g.drawBattleHud(g.buildFrame());
    for(const auto& f:g.focusList)if(f.x<0||f.y<0||f.x+f.w>g_ren.W||f.y+f.h>g_ren.H) {fprintf(stderr,"offscreen control %u\n",f.id);std::exit(3);}
  }
  static void launch(Game& g){g.launchBattle();g.updateHive(1.f/60);}
  static void cockpit(Game& g){g.camMode=1;g.updateCamera(1.f);}
  static void won(Game& g){g.battle.phase=xr40::Phase::Victory;g.battle.stats.score=2450;g.battle.stats.kills=7;g.battle.stats.waves=2;g.battle.stats.elapsed=236;g.finishBattle(true,"Aircraft recovered / objectives secured");}
};
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);if(argc<2)return 2;
  const int W=argc>2?atoi(argv[2]):1920,H=argc>3?atoi(argv[3]):1080;
  const std::string out=argv[1];std::filesystem::create_directories(out);
  if(!initGL(W,H))return 1;
  buildTestWorld();g_audio.init(48000);
  const bool scene=argc>4 && std::string(argv[4])=="scene";
  if(scene) {g_ren.quality=0;g_ren.matDir="assets/materials";if(argc>5)g_ren.prebuiltAircraftDir=argv[5];g_shaderCacheDir="/tmp/xr40-render-cache";std::filesystem::create_directories(g_shaderCacheDir);if(!g_ren.init(W,H))return 1;}
  else if(!g_ren.initUI(W,H))return 1;
  auto game=std::make_unique<Game>();GameTest::setup(*game);game->set.uiScale=1.4f;
  if(scene) {
    for(int i=0;i<3;++i) {game->render();glFinish();}
    if(!g_ren.screenshotPNG((out+"/hangar-scene.png").c_str()))return 5;
    printf("Captured real XR-40 hangar through production renderer\n");
    GameTest::launch(*game);
    for(int i=0;i<6;++i) {game->update(1.f/60);game->render();glFinish();}
    if(!g_ren.screenshotPNG((out+"/flight-scene.png").c_str()))return 5;
    printf("Captured real XR-40 flight through production renderer\n");
    GameTest::cockpit(*game);
    for(int i=0;i<3;++i){game->render();glFinish();}
    if(!g_ren.screenshotPNG((out+"/cockpit-scene.png").c_str()))return 5;
    printf("Aircraft geometry: packaged %d, freshly baked %d\n",g_ren.prebuiltMeshHits,g_ren.bakeBuilt);
    if(argc>5 && (g_ren.prebuiltMeshHits!=2 || g_ren.bakeBuilt!=0))return 6;
    return glGetError()==GL_NO_ERROR?0:4;
  }
  const char* scenes[]={"hangar","pause","debrief","hud"};
  for(int i=0;i<4;++i){
    if(i==1 || i==3)GameTest::launch(*game);if(i==2)GameTest::won(*game);
    for(int f=0;f<3;++f){glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,W,H);glClearColor(.018f,.027f,.043f,1);glClear(GL_COLOR_BUFFER_BIT);g_ren.uiBegin();GameTest::draw(*game,i);g_ren.uiEnd();}
    glFinish();if(glGetError()!=GL_NO_ERROR)return 4;
    if(!g_ren.screenshotPNG((out+"/"+scenes[i]+".png").c_str()))return 5;
    printf("Captured actual UI: %s %dx%d (flat backdrop, no 3D)\n",scenes[i],W,H);
  }
}
