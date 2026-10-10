// Test-only staged 1:1 scale review: actual flyable Kestrel and production house/car.
// Uses the existing standard-fleet diagnostic adapter; no geometry changes or rescaling.
#include "renderer.h"
#include "materials.h"
#include "game.h"
#include "models.h"
#include "volcano_effects.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
static void* lib;static void*(*eglProc)(const char*);
static void* proc(const char*n){void*p=eglProc(n);return p?p:dlsym(lib,n);}
template<class F>static F sym(const char*n){return reinterpret_cast<F>(dlsym(lib,n));}
static bool initGL(int W,int H){
  lib=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);if(!lib)return false;
  eglProc=sym<void*(*)(const char*)>("eglGetProcAddress");if(!eglProc)return false;
  auto gd=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglProc("eglGetPlatformDisplayEXT"));if(!gd)return false;
  void*d=gd(0x31DD,nullptr,nullptr);int ma,mi;if(!sym<unsigned(*)(void*,int*,int*)>("eglInitialize")(d,&ma,&mi))return false;
  sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};void*c;int n;
  if(!sym<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig")(d,ca,&c,1,&n)||!n)return false;
  const int sa[]={0x3057,W,0x3056,H,0x3038},at[]={0x3098,3,0x30FB,3,0x30FD,1,0x3038};
  void*s=sym<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface")(d,c,sa);
  void*x=sym<void*(*)(void*,void*,void*,const int*)>("eglCreateContext")(d,c,nullptr,at);const char*m=nullptr;
  return x&&sym<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent")(d,s,s,x)&&glLoad(proc,&m);
}

struct GameTest {
  static void aircraft(FrameParams& fp) { Game g; g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=0;g.hangarPreviewCamera(fp); }
  static void light(FrameParams& fp) { Game g;g.wx.cloudCover=.15f;g.computeSun(14.3f,fp.sunDir,fp.sunCol,fp.night); }
};
static void insertSpecimen(int kind,const Ent& e) {
  auto*c=g_scenery.ensure(Scenery::chunkOf(e.x),Scenery::chunkOf(e.z),2);
  c->ents.insert(c->ents.begin()+c->off[kind+1],e);
  for(int i=kind+1;i<=EK_COUNT;i++)c->off[i]++;
  c->ymin=std::min(c->ymin,e.y);c->ymax=std::max(c->ymax,e.y+kEntInfo[kind].h*e.sy);
  c->hmax=std::max(c->hmax,kEntInfo[kind].h*e.sy);c->reach=std::max(c->reach,16.f);c->markChanged();
}
static void reportSpecimen(std::ofstream& f,int kind,const Ent&e,const std::vector<EVert>& verts,const EntMeshRange* ranges) {
  vec3 lo(1e9f),hi(-1e9f);const auto&r=ranges[kind];
  for(int i=r.first[0];i<r.first[0]+r.count[0];i++) { const auto&v=verts[i];vec3 p(v.px*e.sx,v.py*e.sy,v.pz*e.sz);lo=vec3(std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z));hi=vec3(std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)); }
  f<<kEntInfo[kind].name<<','<<hi.x-lo.x<<','<<hi.y-std::max(0.f,lo.y)<<','<<hi.z-lo.z<<','<<lo.y<<','<<hi.y<<','<<e.x<<','<<e.y<<','<<e.z<<','<<e.yaw<<','<<e.sx<<','<<e.sy<<','<<e.sz<<'\n';
}
int main(int argc,char**argv) {
  setvbuf(stdout,nullptr,_IONBF,0);if(argc<2){puts("Usage: aircraft_scale_review_harness output-dir [frames]");return 2;}
  const std::string out=argv[1];const int frames=argc>2?atoi(argv[2]):8;std::filesystem::create_directories(out);
  if(!initGL(1920,1080)){puts("EGL unavailable");return 77;}
  printf("GL: %s; native 1920x1080; Medium; renderScale=1\n",glGetString(GL_RENDERER));
  puts("STAGED SCALE DIAGNOSTIC: exact current flyable Kestrel, unit-scale production house and sedan placed on a real flat apron. Placement is a test fixture, not a generated town. Every production startup program is built; exact per-aircraft production programs are compiled on demand. Not hardware performance evidence.");
  const std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):out+"/cache";std::filesystem::create_directories(cache);
  struct stat st{};stat("/proc/self/exe",&st);g_world.build(cache+"/world.bin","scale-review-"+std::to_string((long long)st.st_mtime));
  g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.quality=1;g_ren.renderScale=1;g_ren.entSync=true;
  // CAP apron, away from its normal buildings. Every model remains at its authored metre scale.
  const vec3 origin(-577.f,20.f,-4413.f);
  const Ent house{origin.x+13,origin.y,origin.z+12,PI,1,1,1,.37f};insertSpecimen(EK_HOUSE,house);
  const Ent car{origin.x+5,origin.y,origin.z-1,PI,1,1,1,.54f};
  std::vector<EVert>v;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(v,ranges);
  std::ofstream measures(out+"/dimensions.csv");measures<<"object,width_m,above_grade_height_m,length_m,mesh_bottom_m,mesh_top_m,x,y,z,yaw,sx,sy,sz\n";
  reportSpecimen(measures,EK_HOUSE,house,v,ranges);reportSpecimen(measures,EK_CAR,car,v,ranges);
  const auto&a=kAircraft[0];measures.close();
  if(!g_ren.init(1920,1080,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){fprintf(stderr,"init failed: %s\n",g_ren.error.c_str());return 1;}
  printf("MATERIALS: %d high resolution / %d natural fallback\n",g_ren.matEnvScanned,g_ren.matEnvFallbackScanned);
  if(g_ren.matEnvScanned!=kEnvMatLayers||g_ren.matEnvFallbackScanned!=kEnvMatLayers){fputs("Environment scan set incomplete\n",stderr);return 4;}
  FrameParams fp{};GameTest::aircraft(fp);fp.hangarPreview=false;fp.hangarClassified=false;fp.plN=0;
  fp.plane.pos+=origin+vec3(-6,0,-2);fp.groundVehicles.push_back({EK_CAR,car,{}});
  const vec3 target=origin+vec3(2,3,4);fp.camPos=origin+vec3(29,17,-39);fp.camBack=normalize(fp.camPos-target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=48*DEG;
  fp.cloudCover=.15f;fp.cloudBase=1500;fp.fogB=.00002f;fp.time=20;fp.wind=fp.windSock=vec3(2,0,4);fp.exposure=1;fp.vignette=.15f;GameTest::light(fp);
  {std::ofstream m(out+"/aircraft.csv");m<<"scene,model,span_m,fuselage_length_m,position_x,position_y,position_z,camera_x,camera_y,camera_z\n";m<<"apron,"<<fp.plane.model<<','<<a.span<<','<<a.fusLen<<','<<fp.plane.pos.x<<','<<fp.plane.pos.y<<','<<fp.plane.pos.z<<','<<fp.camPos.x<<','<<fp.camPos.y<<','<<fp.camPos.z<<'\n';}
  printf("ACTUAL FLEET model=%d span=%.3f fuselage=%.3f position %.3f %.3f %.3f; no scale matrix\n",fp.plane.model,a.span,a.fusLen,fp.plane.pos.x,fp.plane.pos.y,fp.plane.pos.z);
  for(int i=0;i<frames;i++){auto t=std::chrono::steady_clock::now();g_ren.renderScene(fp,{},{});glFinish();const GLenum err=glGetError();if(err){fprintf(stderr,"GL error %x\n",err);return 3;}printf("FRAME %d %.1fms entity instances=%d pending=%d\n",i,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count(),g_ren.entDrawn,g_ren.entPending);}
  if(!g_ren.screenshotPNG((out+"/kestrel_house_car.png").c_str()))return 1;
  // A second explicitly staged view puts the same unscaled production airframe in
  // the real volcano scene. It is an airborne spatial reference, not a physics test.
  g_ren.resetTemporal();fp.groundVehicles.clear();fp.plane.pos=vec3(25200,2050,-8760);
  const vec3 bowl(25000,1860,-9000);fp.camPos=vec3(25500,2260,-8400);fp.camBack=normalize(fp.camPos-bowl);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=60*DEG;
  {std::ofstream m(out+"/aircraft.csv",std::ios::app);m<<"volcano,"<<fp.plane.model<<','<<a.span<<','<<a.fusLen<<','<<fp.plane.pos.x<<','<<fp.plane.pos.y<<','<<fp.plane.pos.z<<','<<fp.camPos.x<<','<<fp.camPos.y<<','<<fp.camPos.z<<'\n';}
  std::vector<SpriteVert>alpha,add;volcano::append(fp,g_ren.quality,alpha,add);
  printf("VOLCANO SCALE: unscaled Kestrel camera distance %.3fm; vent distance %.3fm; bowl wall radius340m, changed terrain disk radius520m. Airframe is staged airborne, not a flight-simulation replay.\n",length(fp.plane.pos-fp.camPos),length(bowl-fp.camPos));
  for(int i=0;i<frames;i++){g_ren.renderScene(fp,alpha,add);glFinish();if(glGetError())return 3;printf("VOLCANO SCALE FRAME %d pending=%d\n",i,g_ren.entPending);}
  if(!g_ren.screenshotPNG((out+"/kestrel_volcano_scale.png").c_str()))return 1;
  return 0;
}
