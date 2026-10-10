// Test-only actual production environment capture and draw-cost review on surfaceless EGL.
// Usage: environment_review_harness output-dir [W H frames] [frozen-views.csv] [scene-filter]
// No aircraft, traffic, cockpit or weapons. Never a hardware FPS certification.
#include "renderer.h"
#include "materials.h"
#include "game.h"
#include "scenery.h"
#if __has_include("volcano_effects.h")
#include "volcano_effects.h"
#define ENV_REVIEW_VOLCANO 1
#endif
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <numeric>
#include <sys/stat.h>
#include <type_traits>
#include <utility>
extern const char* reviewStartupMode();
unsigned long long reviewEntityTriangles[3]={},reviewEntityInstances[3]={},reviewEntityDrawCalls[3]={},reviewEntityUploadBytes=0;
static float reviewNearRadius(){const char* value=getenv("ASSET_REVIEW_NEAR_SHADOW_RADIUS");return value?float(atof(value)):(getenv("ASSET_REVIEW_NEAR_SHADOW_96")?96.f:0.f);}
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
template<class T,class=void>struct EnvMaterialCount{static int get(const T&){return 0;}};
template<class T>struct EnvMaterialCount<T,std::void_t<decltype(std::declval<T>().matEnvScanned)>>{static int get(const T& r){return r.matEnvScanned;}};
template<class T,class=void>struct EnvFallbackCount{static int get(const T&){return 0;}};
template<class T>struct EnvFallbackCount<T,std::void_t<decltype(std::declval<T>().matEnvFallbackScanned)>>{static int get(const T& r){return r.matEnvFallbackScanned;}};
struct GameTest { static void light(FrameParams& fp,float hour) { static Game g; g.wx.cloudCover=fp.cloudCover; g.computeSun(hour,fp.sunDir,fp.sunCol,fp.night); } };
struct Shot{std::string name;vec3 camera,target;float fov=55,cloud=.15f,hour=14.3f;};
static std::vector<Shot> shots(){
  std::vector<Shot>s;
  auto look=[&](const char*n,float x,float z,vec3 off,float targetY=8,float fov=55){vec3 t(x,std::max(0.f,g_world.height(x,z))+targetY,z);s.push_back({n,t+off,t,fov,.15f});};
  look("city_port_verde",-29500,10500,vec3(430,190,560),20,60);
  look("town_kaleo",17400,7000,vec3(290,130,330),10,58);
  look("rural_meadowbrook",-6600,15700,vec3(320,125,410),8,58);
  const int ap=g_world.findAirport("CAP");if(ap>=0){const auto&a=g_world.airports[ap];vec3 t=a.pos()+vec3(200,15,0);s.push_back({"airport_capital",t+vec3(500,175,550),t,60,.15f});}
  look("forest_cedar_ridge",-24000,-5000,vec3(140,50,210),12,60);
  look("rocks_kaleo",23000,10000,vec3(180,80,220),7,52);
  look("roads_orchard",-15300,3250,vec3(250,90,350),3,55);
  look("volcano_kaleo",25000,-9000,vec3(1400,850,1800),75,58);
  look("shore_water",26960,22300,vec3(130,78,170),2,58);
  look("foliage_closeup",-23998,-4999.39f,vec3(32,10,42),7.75f,53);
  look("rock_closeup",23035.55f,10035.52f,vec3(8,5,10),1.9f,53);
  if(ap>=0){std::vector<AptItem>items;airportItems(ap,items);for(const auto&i:items)if(i.kind==EK_TRUCK){vec3 t(i.e.x,i.e.y+1.6f,i.e.z);float a=i.e.yaw;vec3 local(11,7,17);vec3 off(cosf(a)*local.x+sinf(a)*local.z,local.y,-sinf(a)*local.x+cosf(a)*local.z);s.push_back({"airport_vehicle_detail",t+off,t,57,.15f});break;}}
  {const float px=-6600,pz=15700;float best=1e30f;Ent found{};bool ok=false;const int cx=Scenery::chunkOf(px),cz=Scenery::chunkOf(pz);
    for(int dz=-2;dz<=2;dz++)for(int dx=-2;dx<=2;dx++){auto*c=g_scenery.ensure(cx+dx,cz+dz,2);for(int k=EK_HOUSE;k<=EK_FARMHOUSE;k++)for(unsigned i=c->off[k];i<c->off[k+1];i++){const auto&e=c->ents[i];float d=(e.x-px)*(e.x-px)+(e.z-pz)*(e.z-pz);if(d<best){best=d;found=e;ok=true;}}}
    if(ok){vec3 t(found.x,found.y+4,found.z);s.push_back({"rural_house_detail",t+vec3(24,14,32),t,57,.15f});}}

  return s;
}
static void meshReport(const std::string&out){
  std::vector<EVert>v;EntMeshRange r[EK_COUNT];buildEntityMeshes(v,r);
  std::ofstream f(out+"/mesh_inventory.csv");f<<"kind,name,lod,triangles,vertices,bytes\n";
  for(int k=0;k<EK_COUNT;k++)for(int l=0;l<ENT_LODS;l++)f<<k<<','<<kEntInfo[k].name<<','<<l<<','<<r[k].count[l]/3<<','<<r[k].count[l]<<','<<(size_t)r[k].count[l]*sizeof(EVert)<<'\n';
  printf("MESH_INVENTORY %zu vertices; %zu bytes; %zu triangles across all kinds/LODs\n",v.size(),v.size()*sizeof(EVert),v.size()/3);
}
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<2){puts("Usage: environment_review_harness output-dir [W H frames] [frozen-views.csv] [scene-filter]");return 2;}
  const std::string out=argv[1];const int W=argc>2?atoi(argv[2]):1920,H=argc>3?atoi(argv[3]):1080,frames=argc>4?std::max(1,atoi(argv[4])):8;
  std::filesystem::create_directories(out);meshReport(out);
  if(!initGL(W,H)){puts("EGL unavailable");return 77;}
  printf("GL: %s; native %dx%d; renderScale=1; quality=Medium\n",glGetString(GL_RENDERER),W,H);
  printf("DIAGNOSTIC: actual production environment geometry/shaders. %s Not RTX hardware performance.\n",reviewStartupMode());
  {std::ofstream mode(out+"/renderer_mode.txt");mode<<reviewStartupMode()<<"\n";
    if(reviewNearRadius()>0) {
      mode<<"DIAGNOSTIC ONLY: review-adapter near shadow radius "<<reviewNearRadius()<<"m, unchanged texture allocations; production cascade policy untouched.\n";
      printf("DIAGNOSTIC ONLY: review-adapter near shadow radius%.1fm, unchanged texture allocations.\n",reviewNearRadius());
    }
  }
  const std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):out+"/cache";std::filesystem::create_directories(cache);
  struct stat executable{};if(stat("/proc/self/exe",&executable)!=0){perror("executable stamp");return 1;}
  const std::string worldStamp="living-islands-"+std::to_string((long long)executable.st_size)+"-"+std::to_string((long long)executable.st_mtime);
  g_world.build(cache+"/world.bin",worldStamp);
  g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=1;g_ren.entSync=true;
  if(!g_ren.init(W,H,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){fprintf(stderr,"init failed: %s\n",g_ren.error.c_str());return 1;}
  if(!g_shaderNotes.empty())printf("SHADER NOTES: %s\n",g_shaderNotes.c_str());
  const int envMaps=EnvMaterialCount<Renderer>::get(g_ren),envFallback=EnvFallbackCount<Renderer>::get(g_ren);printf("MATERIALS: %d base scanned layers; %d high-resolution environment layers; %d natural-colour 512px fallback layers; directory %s\n",g_ren.matScanned,envMaps,envFallback,g_ren.matDir.c_str());
#ifdef ENV_REVIEW_VOLCANO
  const int requiredEnvMaps=kEnvMatLayers;
#else
  const int requiredEnvMaps=0; // frozen original archive predates the optional environment material set
#endif
  if(getenv("REQUIRE_ENV_MATERIALS") && (requiredEnvMaps<=0 || envMaps!=requiredEnvMaps || envFallback!=requiredEnvMaps)){fprintf(stderr,"Required high-resolution or natural fallback grass/asphalt/concrete maps not active\n");return 4;}
  auto views=shots();
  if(argc>5&&std::string(argv[5])!="-"){
    views.clear();std::ifstream f(argv[5]);std::string line;std::getline(f,line);
    while(std::getline(f,line)){std::replace(line.begin(),line.end(),',',' ');std::istringstream q(line);Shot s;q>>s.name>>s.camera.x>>s.camera.y>>s.camera.z>>s.target.x>>s.target.y>>s.target.z>>s.fov>>s.cloud;if(q){float hour;if(q>>hour)s.hour=hour;views.push_back(s);}}
    if(views.empty()){fprintf(stderr,"No valid frozen views\n");return 2;}
  }
  {std::ofstream f(out+"/views.csv");f<<"name,camera_x,camera_y,camera_z,target_x,target_y,target_z,fov_degrees,cloud_cover,hour,near_shadow_radius_override\n"<<std::setprecision(9);for(auto&s:views)f<<s.name<<','<<s.camera.x<<','<<s.camera.y<<','<<s.camera.z<<','<<s.target.x<<','<<s.target.y<<','<<s.target.z<<','<<s.fov<<','<<s.cloud<<','<<s.hour<<','<<reviewNearRadius()<<'\n';}
  std::ofstream csv(out+"/frames.csv");csv<<"scene,frame,wall_ms,instances,entity_triangles,entity_draws,shadow_triangles,upload_bytes,chunks,pending,width,height,render_scale,quality\n";
  for(auto&s:views){
    if(argc>6&&s.name.find(argv[6])==std::string::npos)continue;
    g_ren.resetTemporal();FrameParams fp{};fp.camPos=s.camera;fp.camBack=normalize(s.camera-s.target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=s.fov*DEG;
    fp.cloudCover=s.cloud;GameTest::light(fp,s.hour);fp.exposure=1.f+fp.night*.8f;fp.rwyLights=fp.night;fp.cloudBase=1500;fp.fogB=.00002f;fp.time=20;fp.wind=fp.windSock=vec3(2,0,4);fp.vignette=.15f;
    printf("SCENE %s camera %.3f %.3f %.3f target %.3f %.3f %.3f\n",s.name.c_str(),s.camera.x,s.camera.y,s.camera.z,s.target.x,s.target.y,s.target.z);
    std::vector<SpriteVert> alpha,add;
#ifdef ENV_REVIEW_VOLCANO
    volcano::append(fp,g_ren.quality,alpha,add);
    printf("VOLCANO %zu alpha sprites / %zu additive sprites; %d total point lights\n",alpha.size()/6,add.size()/6,fp.plN);
#endif
    for(int f=0;f<frames;f++){
      auto t=std::chrono::steady_clock::now();g_ren.renderScene(fp,alpha,add);glFinish();const GLenum glError=glGetError();if(glError){fprintf(stderr,"GL error 0x%x in %s frame %d\n",glError,s.name.c_str(),f);return 3;}double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
      csv<<s.name<<','<<f<<','<<ms<<','<<reviewEntityInstances[0]<<','<<reviewEntityTriangles[0]<<','<<reviewEntityDrawCalls[0]<<','<<reviewEntityTriangles[1]+reviewEntityTriangles[2]<<','<<reviewEntityUploadBytes<<','<<g_ren.entChunks<<','<<g_ren.entPending<<','<<W<<','<<H<<",1,1\n";csv.flush();
      printf("FRAME %s %d %.1f ms; %llu instances; %llu triangles; %llu draws; %d pending\n",s.name.c_str(),f,ms,reviewEntityInstances[0],reviewEntityTriangles[0],reviewEntityDrawCalls[0],g_ren.entPending);
    }
    std::string path=out+"/"+s.name+".png";if(!g_ren.screenshotPNG(path.c_str()))return 1;printf("WROTE %s\n",path.c_str());
  }
  return 0;
}
