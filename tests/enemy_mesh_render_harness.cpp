// Actual production enemy mesh/material/shadow inspection on surfaceless EGL.
#include "../src/renderer.h"
#include "test_world.h"
#include <dlfcn.h>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
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

int main(int argc,char**argv) {
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<3){fprintf(stderr,"usage: enemy_mesh_render_harness TYPE OUTPUT [WIDTH HEIGHT]\n");return 2;}
  int type=atoi(argv[1]);if(type<0||type>=kEnemyCraftTypes)return 2;
  int W=argc>3?atoi(argv[3]):1920,H=argc>4?atoi(argv[4]):1080;
  if(W<320||H<180||W>3840||H>2160)return 2;
  std::filesystem::create_directories(argv[2]);
  if(!initGL(W,H)){puts("EGL initialization failed");return 1;}
  printf("GL %s / native %dx%d\n",glGetString(GL_RENDERER),W,H);
  buildTestWorld();
  g_shaderCacheDir=std::string(argv[2])+"/cache";std::filesystem::create_directories(g_shaderCacheDir);
  g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=2;g_ren.entSync=true;
  if(!g_ren.init(W,H,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){
    fprintf(stderr,"Renderer init failed: %s\n",g_ren.error.c_str());return 1;
  }
  g_ren.bakeYield=[](){};
  const auto& spec=kEnemyCraftSpecs[type];
  for(int night=0;night<2;++night)for(int angle=0;angle<4;++angle){
    FrameParams fp{};fp.hangarPreview=true;fp.hangarOrigin=vec3(0,0,0);fp.hangarSize=std::max(55.f,spec.radius*4.f);
    fp.plane.on=false;fp.cloudCover=0;fp.fogB=.00001f;fp.enemyN=1;
    auto& c=fp.enemies[0];c.type=(EnemyCraftType)type;c.pos=vec3(0,1-spec.boundsMin.y,0);
    c.state[0]=.6f;c.state[1]=.6f;
    vec3 target=c.pos+(spec.boundsMin+spec.boundsMax)*.5f;
    float yaw=(angle*90.f+35.f)*PI/180.f;
    float distance=spec.radius*3.1f;
    fp.camPos=target+vec3(sinf(yaw)*distance,distance*.36f,-cosf(yaw)*distance);
    fp.camBack=normalize(fp.camPos-target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=.72f;
    fp.sunDir=normalize(vec3(-.45f,night?-.2f:.7f,-.5f));fp.sunCol=night?vec3(.07f,.09f,.14f):vec3(1.1f,1.04f,.94f);fp.night=night?1.f:0.f;
    g_ren.resetTemporal();
    for(int frame=0;frame<4;++frame){fp.time=frame/60.f;g_ren.renderScene(fp,{},{});glFinish();if(glGetError()!=GL_NO_ERROR){fprintf(stderr,"GL error at type%d angle%d night%d\n",type,angle,night);return 3;}}
    std::string path=std::string(argv[2])+"/"+spec.id+(night?"-night-":"-day-")+std::to_string(angle)+".png";
    g_ren.screenshot(path.c_str());printf("CAPTURE %s\n",path.c_str());
  }
  if(!g_shaderNotes.empty())printf("SHADER NOTES %s\n",g_shaderNotes.c_str());
  return 0;
}
