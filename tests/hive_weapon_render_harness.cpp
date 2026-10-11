// Actual production XR-40 rigid loadout inspection. Parent controls serial GPU execution.
#include "../src/renderer.h"
#include "test_world.h"
#include "../src/models.h"
#include <cstring>
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


static void aim(FrameParams& f,vec3 target,vec3 offset,float fov=.55f){
 f.camPos=target+offset;f.camBack=normalize(offset);
 f.camRight=normalize(cross(vec3(0,1,0),f.camBack));f.camUp=cross(f.camBack,f.camRight);f.fovY=fov;
}
static FrameParams fixture(int kind,bool night){
 FrameParams f{};f.hangarPreview=true;f.hangarSize=35;f.hangarOrigin=vec3(0);
 f.plane.on=true;f.plane.model=kWraith;packModelOf(kWraith,f.plane.M);
 f.plane.pos=vec3(0,kind==1?5.f:4.f,0);f.plane.rot[0]=f.plane.rot[4]=f.plane.rot[8]=1;
 f.plane.colBase=vec3(.085f,.10f,.12f);f.plane.colStripe=vec3(.12f,.65f,.9f);
 f.plane.wr[4][1]=1;f.plane.wr[4][2]=1;f.plane.wr[6][0]=1;
 f.plane.wr[6][3]=float(kind);f.plane.wrBombSet=kind;
 f.cloudCover=0;f.fogB=.00001f;f.night=night?1:0;
 f.sunDir=normalize(vec3(-.45f,night?-.2f:.7f,-.5f));
 f.sunCol=night?vec3(.07f,.09f,.14f):vec3(1.1f,1.04f,.94f);
 return f;
}
static void released(FrameParams& f,int kind,vec3 pos,int index=0){
 f.fx.bombs=std::max(f.fx.bombs,index+1);
 f.fx.bomb[index][0]=pos.x;f.fx.bomb[index][1]=pos.y;f.fx.bomb[index][2]=pos.z;f.fx.bomb[index][3]=.29f;
 f.fx.bombRot[index][0]=f.fx.bombRot[index][4]=f.fx.bombRot[index][8]=1;
 f.fx.bombStyle[index][0]=kind==1?.4f:.15f;f.fx.bombStyle[index][1]=kind==1?.35f:.65f;f.fx.bombStyle[index][2]=kind==1?.25f:1;
 f.fx.bombStyle[index][3]=float(kind);
}
static bool capture(FrameParams f,const std::string& root,const std::string& name){
 g_ren.resetTemporal();
 for(int frame=0;frame<6;frame++){
  f.time=1.f; // hold emitter pulse phase fixed across every comparison
  g_ren.renderScene(f,{},{});glFinish();
  GLenum err=glGetError();if(err!=GL_NO_ERROR){fprintf(stderr,"GL error %u in %s\n",unsigned(err),name.c_str());return false;}
 }
 const std::string path=root+"/"+name+".png";
 g_ren.screenshot(path.c_str());printf("CAPTURE %s\n",path.c_str());return true;
}
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(argc<2){fprintf(stderr,"usage: hive_weapon_render_harness OUTPUT [MODE=all|guns|bay|released|camera] [WIDTH=1920 HEIGHT=1080] [KIND=-1|0|1|2]\n");return 2;}
 const std::string root=argv[1],mode=argc>2?argv[2]:"all";
 if(mode!="all"&&mode!="guns"&&mode!="bay"&&mode!="released"&&mode!="camera")return 2;
 const int W=argc>3?atoi(argv[3]):1920,H=argc>4?atoi(argv[4]):1080,only=argc>5?atoi(argv[5]):-1;
 if(W<320||H<180||W>3840||H>2160||only< -1||only>2)return 2;
 std::filesystem::create_directories(root);
 if(!initGL(W,H)){puts("EGL initialization failed");return 1;}
 printf("GL %s / native %dx%d\n",glGetString(GL_RENDERER),W,H);
 buildTestWorld();g_shaderCacheDir=root+"/cache";std::filesystem::create_directories(g_shaderCacheDir);
 g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=2;g_ren.entSync=true;
 if(!g_ren.init(W,H,[](float p,const std::string& s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){
  fprintf(stderr,"Renderer init failed: %s\n",g_ren.error.c_str());return 1;
 }
 g_ren.bakeYield=[](){};
 // Exercise an initially absent EXTERIOR cache while the view requests cockpit.
 // Subsequent release draws must reuse this warm cache rather than bake on release.
 auto warm=fixture(0,false);warm.plane.PS[3]=1;g_ren.warmWraithStores(warm);
 printf("WARM exterior stores from cockpit packet complete\n");
 const char* names[]={"pulse-plasma","kinetic-penetrator","charged-emp"};
 for(int kind=0;kind<3;kind++){
  if(only>=0&&kind!=only)continue;
  for(int night=0;night<2;night++){
   const std::string label=std::string(names[kind])+(night?"-night":"-day");
   auto f=fixture(kind,night!=0);const vec3 inspectionOffset(0,kind==1?-3.8f:-2.6f,kind==1?-.51f:-.35f);const vec3 bay=f.plane.pos+vec3(0,-.305f,.1f);
   if(mode=="all"||mode=="guns")for(int fire=0;fire<2;fire++){
    auto shot=f;shot.plane.wr[5][3]=float(fire);
    aim(shot,f.plane.pos+vec3(.95f,-.66f,-5.9f),vec3(1.2f,-.7f,-2.4f));
    if(fire){
     shot.fx.beams=1;vec3 muzzle=f.plane.pos+vec3(.95f,-.68f,-6.44f),head=muzzle+vec3(0,0,-2.8f);
     float* a=shot.fx.beamA[0];float* b=shot.fx.beamB[0];
     a[0]=muzzle.x;a[1]=muzzle.y;a[2]=muzzle.z;a[3]=.22f;b[0]=head.x;b[1]=head.y;b[2]=head.z;b[3]=1;
     if(kind){float* s=shot.fx.beamStyle[0];s[0]=kind==1?1:.2f;s[1]=kind==1?.45f:.55f;s[2]=kind==1?.1f:1;s[3]=kind==1?.65f:1.8f;}
    }
    if(!capture(shot,root,label+(fire?"-gun-active":"-gun-idle")))return 3;
   }
   if(mode=="all"||mode=="bay"){
    aim(f,bay,inspectionOffset);
    if(!capture(f,root,label+"-bay-open"))return 3;
    auto closed=f;closed.plane.wr[4][1]=0;
    if(!capture(closed,root,label+"-bay-closed"))return 3;
   }
   if(mode=="all"||mode=="released"){
    auto store=f;store.plane.on=false;released(store,kind,bay);
    aim(store,bay,inspectionOffset); // exact same pose/scale/camera as bay-open
    if(!capture(store,root,label+"-released-same-pose"))return 3;
   }
   if(mode=="all"||mode=="camera"){
    auto store=f;store.plane.on=false;vec3 p(0,1.3f,0);released(store,kind,p);
    aim(store,p,vec3(1.4f,.55f,-2.7f),.7f);
    if(!capture(store,root,label+"-near-ground-shadow"))return 3;
    // Swap two different hardware kinds on one ray: nearer opaque hardware must win.
    vec3 farther=p-store.camBack*1.0f;released(store,(kind+1)%3,farther,1);
    if(!capture(store,root,label+"-overlap-near-first"))return 3;
    released(store,kind,farther,0);released(store,(kind+1)%3,p,1);
    if(!capture(store,root,label+"-overlap-near-second"))return 3;
   }
  }
 }
 if(!g_shaderNotes.empty()){fprintf(stderr,"SHADER NOTES %s\n",g_shaderNotes.c_str());return 4;}
 return 0;
}
