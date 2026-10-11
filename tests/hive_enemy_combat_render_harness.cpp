// Actual production enemy mesh/material/shadow inspection on surfaceless EGL.
#include "../src/game.h"
#include <memory>
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


// GameTest is the existing production test friend; visual packets always use buildFrame.
struct GameTest {
 static FrameParams packet(Game& g,vec3 focus,float radius,vec3 offset=vec3()){
  auto f=g.buildFrame();f.plane.on=false;for(auto& feed:f.feeds)feed.on=false;
  f.hangarPreview=true;f.hangarSize=std::max(55.f,4*radius);f.hangarOrigin=vec3(focus.x,focus.y-radius*.3f-3,focus.z);
  f.cloudCover=0;f.fogB=.00001f;f.night=0;f.sunDir=normalize(vec3(-.45f,.7f,-.5f));f.sunCol=vec3(1.1f,1.04f,.94f);
  if(length(offset)<.1f)offset=vec3(radius*1.4f,radius*.8f,-radius*2.4f);
  f.camPos=focus+offset;f.camBack=normalize(offset);f.camRight=normalize(cross(vec3(0,1,0),f.camBack));f.camUp=cross(f.camBack,f.camRight);f.fovY=.75f;
  return f;
 }
 static bool capture(const std::string& root,const std::string& name,FrameParams f){
  g_ren.resetTemporal();for(int j=0;j<4;j++){g_ren.renderScene(f,{},{});glFinish();if(glGetError()!=GL_NO_ERROR)return false;}
  const auto path=root+"/"+name+".png";g_ren.screenshot(path.c_str());printf("CAPTURE %s enemies=%d ordnance=%d relayBeams=%d\n",path.c_str(),f.enemyN,f.hiveOrdnanceN,f.fx.beams);return true;
 }
 static int run(const std::string& root,int selected){
  auto q=std::make_unique<Game>();auto& g=*q;g.initHeadless();g.diskless=true;g.botControl=true;g.set.traffic=false;g.launchMilitary(1);
  hive::WorldCallbacks world{};world.terrainHeight=[](void*,float,float){return 0.f;};
  hive::Projectile emitted{};bool haveShot=false;
  for(int kind=0;kind<4;kind++){
   if(selected>=0&&kind!=selected)continue;
   g.hiveCombat.reset(37,0);g.plane.pos=vec3(0,200,-900);g.plane.vel=vec3();
   hive::PlayerSnapshot player{};player.position=g.plane.pos;player.radius=4;
   auto id=g.hiveCombat.spawn(hive::Type(kind),vec3(0,220,0),vec3(0,0,-100));
   if(kind==2)g.hiveCombat.spawn(hive::Type::Needle,vec3(25,220,-35),vec3(0,0,-100));
   bool charged=false,fired=false,relay=false;int flightFrames=0;
   for(int tick=0;tick<1800;tick++){
    g.hiveCombat.clearEvents();g.hiveCombat.step(hive::FixedStep,player,world);g.realTime+=hive::FixedStep;
    auto* actor=g.hiveCombat.find(id);if(!actor||!actor->alive)return 5;
    const auto& spec=kEnemyCraftSpecs[kind];
    if(!charged&&actor->phase==hive::Phase::Telegraph){
     charged=true;if(!capture(root,std::string(spec.id)+"-real-charge",packet(g,actor->position,spec.radius)))return 3;
    }
    if(kind==2&&!relay&&actor->relayTarget){
     relay=true;auto f=packet(g,actor->position+vec3(10,0,-15),spec.radius*1.7f);
     if(f.fx.beams==0)return 6;
     if(!capture(root,"cantor-real-relay",f))return 3;
     g.hiveCombat.playerEMP(actor->position,500,4);g.hiveCombat.step(hive::FixedStep,player,world);
     auto off=packet(g,actor->position+vec3(10,0,-15),spec.radius*1.7f);
     if(off.fx.beams!=0)return 7;
     if(!capture(root,"cantor-real-relay-interrupted",off))return 3;
     break;
    }
    if(!fired)for(int j=0;j<g.hiveCombat.eventCount;j++){
     const auto& e=g.hiveCombat.events[j];if(e.type!=hive::EventType::Fire||e.source!=id)continue;
     fired=true;for(const auto& p:g.hiveCombat.projectiles)if(p.alive&&p.id==e.target){emitted=p;haveShot=true;}
     const vec3 socket=hive::weaponSocket(*actor,actor->shotsFired-1);
     const float error=length(socket-e.position);
     printf("REAL FIRE kind=%d time=%.3f shot=%u socket_error_after_motion=%.4f\n",kind,g.hiveCombat.simulatedSeconds(),e.target,error);
     if(error>length(actor->velocity)*hive::FixedStep+1.f)return 8;
     auto f=packet(g,actor->position,spec.radius);if(f.hiveOrdnanceN==0)return 9;
     if(!capture(root,std::string(spec.id)+"-real-first-fire",f))return 3;
     break;
    }
    if(fired&&++flightFrames==10){
     vec3 shotPos=actor->position;for(const auto& p:g.hiveCombat.projectiles)if(p.alive&&p.owner==id){shotPos=p.position;break;}
     const vec3 flightFocus=(actor->position+shotPos)*.5f;
     const float flightRadius=std::max(spec.radius*1.6f,length(shotPos-actor->position)*.5f+spec.radius);
     auto f=packet(g,flightFocus,flightRadius);
     if(!capture(root,std::string(spec.id)+"-real-projectile-flight",f))return 3;
     // Actual origin-shift API, then the same Game packet projection at offset world coordinates.
     const vec3 shift(12000,0,-9000);g.hiveCombat.shiftOrigin(shift);g.plane.pos+=shift;
     auto shifted=packet(g,flightFocus+shift,flightRadius);
     if(!capture(root,std::string(spec.id)+"-offset-world",shifted))return 3;
     break;
    }
   }
   if(kind!=2&&(!charged||!fired)){fprintf(stderr,"No real telegraph/fire for kind%d\n",kind);return 10;}
   if(kind==2&&!relay)return 11;
  }
  // Capacity fixture deliberately replicates a real emitted projectile into every
  // simulation slot. It is not claimed as a naturally generated96-shot encounter.
  if(haveShot){
   g.hiveCombat.reset(37,0);g.plane.pos=vec3(0,200,0);
   for(int i=0;i<hive::MaxProjectiles;i++){
    auto& p=g.hiveCombat.projectiles[i];p=emitted;p.alive=true;p.id=1000+i;p.position=vec3((i%16-7.5f)*2.4f,200+(i/16)*2.f,0);p.velocity=vec3(0,0,-25);
   }
   auto f=packet(g,vec3(0,205,0),18,vec3(0,6,-65));
   if(f.hiveOrdnanceN!=96)return 12;
   if(!capture(root,"capacity96-game-packet",f))return 3;
   // Lower half deliberately behind the opaque inspection floor: same sprites,
   // depth test must hide covered hardware while all96 remain in the packet.
   for(int i=0;i<48;i++)g.hiveCombat.projectiles[i].position.y=f.hangarOrigin.y-2;
   auto covered=packet(g,vec3(0,205,0),18,vec3(0,6,-65));
   if(covered.hiveOrdnanceN!=96)return 13;
   if(!capture(root,"capacity96-floor-cover-depth",covered))return 3;
  }
  return 0;
 }
};
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);if(argc<2){fprintf(stderr,"usage: hive_enemy_combat_render_harness OUTPUT [TYPE=-1|0..3] [WIDTH HEIGHT]\n");return 2;}
 int type=argc>2?atoi(argv[2]):-1,W=argc>3?atoi(argv[3]):1920,H=argc>4?atoi(argv[4]):1080;
 if(type< -1||type>3||W<320||H<180||W>3840||H>2160)return 2;
 std::filesystem::create_directories(argv[1]);if(!initGL(W,H))return 1;
 buildTestWorld();buildStory();g_audio.init(48000);
 g_shaderCacheDir=std::string(argv[1])+"/cache";std::filesystem::create_directories(g_shaderCacheDir);
 g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=2;g_ren.entSync=true;
 if(!g_ren.init(W,H,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());}))return 1;
 g_ren.bakeYield=[](){};const int result=GameTest::run(argv[1],type);
 if(!g_shaderNotes.empty())fprintf(stderr,"SHADER NOTES %s\n",g_shaderNotes.c_str());return result;
}
