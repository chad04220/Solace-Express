// Diagnostic only: native production renderer, selected actual airframe and fixed review cameras.
#include "game.h"
#include "models.h"
#include "materials.h"
#include "shaders.h"
#include "shader_prune.h"
#include "mesh_validation.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
static void* lib;static void*(*eglProc)(const char*);
static void* proc(const char*n){void*p=eglProc(n);return p?p:dlsym(lib,n);}
template<class F>static F sym(const char*n){return reinterpret_cast<F>(dlsym(lib,n));}
static bool initGL(int W,int H){
 lib=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);if(!lib)return false;
 eglProc=sym<void*(*)(const char*)>("eglGetProcAddress");if(!eglProc)return false;
 auto gd=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglProc("eglGetPlatformDisplayEXT"));if(!gd)return false;
 void*d=gd(0x31DD,nullptr,nullptr);int ma,mi;if(!sym<unsigned(*)(void*,int*,int*)>("eglInitialize")(d,&ma,&mi))return false;
 sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2);const int ca[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};void*c;int n;
 if(!sym<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig")(d,ca,&c,1,&n)||!n)return false;
 const int sa[]={0x3057,W,0x3056,H,0x3038},at[]={0x3098,3,0x30FB,3,0x30FD,1,0x3038};
 void*s=sym<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface")(d,c,sa);
 void*x=sym<void*(*)(void*,void*,void*,const int*)>("eglCreateContext")(d,c,nullptr,at);const char*m=nullptr;
 return x&&sym<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent")(d,s,s,x)&&glLoad(proc,&m);
}
struct GameTest {
 static FrameParams frame(int model,const std::string& view,bool world){
  if(view.find("prewarm")!=std::string::npos){
   Game g;g.headless=true;g.screen=SCR_MENU;g.prewarmCraft=model;g.prewarmInside=view.find("cockpit")!=std::string::npos;g.realTime=3.f;g.update(1.f/60.f);
   FrameParams fp=g.buildFrame();printf("REVIEW_ACTUAL_PREWARM model=%d inside=%d realTime=%.9g propPhase=%.9g\n",model,int(g.prewarmInside),g.realTime,fp.plane.Pr[0]);return fp;
  }

  Game g;g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=model;FrameParams fp{};g.hangarPreviewCamera(fp);
  const auto&a=kAircraft[model];const auto&m=kModels[model];const float size=std::max(a.span,a.fusLen);
  const bool inside=view.rfind("cockpit",0)==0;const bool running=view.find("running")!=std::string::npos||inside;
  float gear=view.find("gear_up")!=std::string::npos?0:view.find("gear_half")!=std::string::npos?.5f:1;
  // Diagnostic gear sweep: set the real Plane gear state at exact percentage samples.
  if(const auto at=view.find("gear_pct");at!=std::string::npos){
   const std::string token=view.substr(at+8,3);
   if(token.size()!=3||token.find_first_not_of("0123456789")!=std::string::npos||std::stoi(token)>100)throw std::runtime_error("Invalid diagnostic gear percentage");
   gear=std::stoi(token)*.01f;
  }
  const float defl=view.find("positive")!=std::string::npos?1:view.find("negative")!=std::string::npos?-1:0;
  const vec3 origin=world?vec3(-577,g_world.groundHeight(-577,-4413,11),-4413):vec3(0);
  g.screen=SCR_FLIGHT;g.camMode=inside?1:0;g.timeOfDay=14.3f;g.realTime=20;g.set.headLook=false;g.set.cockpitFocusZoom=false;
  g.plane.reset(&a,origin+fp.plane.pos,0,a.maxFuel*.7f,85,false,0);g.plane.pos=origin+fp.plane.pos;g.plane.q=quat::axisAngle(vec3(1,0,0),atan2f(fp.plane.rot[5],fp.plane.rot[4])); // Preserve production showroom parked pitch, including taildraggers.
  if(view.find("belly")!=std::string::npos)g.plane.pos.y+=size*1.1f; // Airborne static inspection fixture, not a flown state.
  if(model==kAtlas&&view.find("loaded")!=std::string::npos){g.plane.pos.y=origin.y+3.6196f;g.plane.q=quat::axisAngle(vec3(1,0,0),.0861f*DEG);} // Pose measured by actual Plane::step at74385kg CAP.
  g.plane.gear=gear;g.plane.flaps=defl?1:0;g.plane.ctl.pitch=defl;g.plane.ctl.roll=defl;g.plane.ctl.yaw=defl;g.plane.ctl.throttle=running?.7f:0;
  if(view.find("yaw_left")!=std::string::npos)g.plane.ctl.yaw=-1;
  if(view.find("yaw_right")!=std::string::npos)g.plane.ctl.yaw=1;
  if(view.find("throttle_off")!=std::string::npos)g.plane.ctl.throttle=0;
  if(view.find("throttle_full")!=std::string::npos)g.plane.ctl.throttle=1;
  g.plane.engineRunning=running;g.plane.rpm=running?a.maxRpm*.8f:0;g.plane.n1=running?75:0;g.plane.engineSpool=running?.7f:0;g.propAngle=running?1.1f:0;
  if(view.find("phase0")!=std::string::npos)g.propAngle=0;
  if(view.find("phase1")!=std::string::npos)g.propAngle=1.1f;
  if(view.find("failed_left")!=std::string::npos)g.plane.fail.engineHealth[0]=0;
  // Real state fixtures for before/after interface parity; all visuals still come from Game::buildFrame.
  if(view.find("refactor_specter_vector")!=std::string::npos){
   g.plane.gear=.5f;g.plane.ctl.pitch=.8f;g.plane.ctl.roll=-.65f;g.plane.ctl.yaw=.4f;g.plane.ctl.throttle=1;g.plane.nozzle=0;
   g.plane.onGround=false;g.plane.engineRunning=true;g.plane.engineSpool=.7f;g.plane.n1=75;g.plane.rpm=a.maxRpm*.8f;
  }
  if(view.find("refactor_wraith")!=std::string::npos){
   g.plane.gear=1;g.plane.nozzle=1;g.plane.ctl.pitch=.8f;g.plane.ctl.roll=-.65f;g.plane.ctl.yaw=.4f;g.plane.ctl.throttle=1;
   g.plane.onGround=false;g.plane.engineRunning=true;g.plane.engineSpool=.7f;g.plane.n1=75;g.plane.rpm=a.maxRpm*.8f;
   for(int i=0;i<4;++i){g.plane.podTilt[i]=PI*.5f;g.plane.podYaw[i]=(i%2?1.f:-1.f)*.12f;g.plane.podVane[i]=(i%2?1.f:-1.f)*.18f;g.plane.podThr[i]=.66f;}
   g.plane.fanAngle=.7854f;g.plane.surf=vec3(.6f,-.4f,.5f);
   if(view.find("weapons")!=std::string::npos){g.wraith.armed=true;g.wraith.lasers=1;g.wraith.bay=1;g.wraith.bombLoaded=1;g.wraith.laserGlow=.6f;}
   if(view.find("empty")!=std::string::npos)g.wraith.bombLoaded=0;
   if(view.find("cloak")!=std::string::npos){g.wraith.cloakOn=true;g.wraith.stealth=.5f;g.wraith.front=0;}
  }
  g.camPos=g.plane.pos+g.plane.q.rotate(m.eye);g.lookPitch=-.13f;g.lookYaw=0;g.ckZoom=g.ckZoomT=1;
  if(view.find("breakup")!=std::string::npos){
   // Staged separation of real production breakup owners: no substitute geometry.
   const bool full=view.find("full")!=std::string::npos;
   g.plane.pos.y+=size*(full?.70f:.30f);
   g.breakUp(vec3(0,0,-50),false,true);
   for(auto& w:g.wreck){
    w.fire=0;
    if(full){
     // Exploded inspection only: translate each real production piece outward from its own mass center.
     vec3 offset=w.body.cg*.60f;
     if(w.kind==BK_GEAR)offset.y-=size*.08f;
     if(w.kind==BK_FIN)offset.y+=size*.10f;
     if(w.kind==BK_PROP)offset.z-=size*.08f;
     w.c+=g.plane.q.rotate(offset);
    }else if(w.kind==BK_GEAR){w.c.y-=size*.08f;w.c.x+=w.side*size*.24f;if(!w.side)w.c.z-=size*.18f;}
   }
   if(const char* inventory=getenv("REVIEW_WRECK_DUMP")){
    std::ofstream f(inventory);f<<"index,kind,side,rigid_only,cg_x,cg_y,cg_z,lo_x,lo_y,lo_z,hi_x,hi_y,hi_z,center_x,center_y,center_z,offset_x,offset_y,offset_z\n";
    for(size_t i=0;i<g.wreck.size();++i){const auto&w=g.wreck[i];vec3 off=w.c-g.plane.pos-w.q.rotate(w.body.cg);
     f<<i<<','<<breakKindName(w.kind)<<','<<w.side<<','<<w.rigidOnly<<','<<w.body.cg.x<<','<<w.body.cg.y<<','<<w.body.cg.z<<','<<w.body.lo.x<<','<<w.body.lo.y<<','<<w.body.lo.z<<','<<w.body.hi.x<<','<<w.body.hi.y<<','<<w.body.hi.z<<','<<w.c.x<<','<<w.c.y<<','<<w.c.z<<','<<off.x<<','<<off.y<<','<<off.z<<'\n';
    }
   }
   g.debris.clear();g.particles.clear();g.pops.clear();g.boomT=-1;
  }
  FrameParams live=g.buildFrame();
  if(world){fp=live;fp.cloudCover=.15f;fp.fogB=.00002f;fp.hangarPreview=false;fp.hangarClassified=false;fp.trafficN=0;}
  else {fp.plane=live.plane;fp.dispCk=live.dispCk;fp.dispMode=live.dispMode;fp.planeTerrSh=1;}
  if(!world)fp.hangarSize*=2.5f; // Larger diagnostic room keeps fixed inspection cameras inside its walls.
  fp.time=20;fp.vignette=.14f;fp.exposure=1;fp.plane.lensN=0;
  vec3 target,eye;
  if(inside){
   eye=g.plane.pos+g.plane.q.rotate(m.eye);vec3 direction(0,-sinf(.13f),-cosf(.13f));
   if(view.find("down")!=std::string::npos)direction=vec3(0,-.8f,-.65f);
   if(view.find("left")!=std::string::npos)direction=vec3(-1,-.15f,-.25f);
   if(view.find("right")!=std::string::npos)direction=vec3(1,-.15f,-.25f);
   if(view.find("aft")!=std::string::npos)direction=vec3(.1f,-.2f,1);
   if(view.find("overhead")!=std::string::npos)direction=vec3(-m.eye.x,.65f,-.12f);
   target=eye+g.plane.q.rotate(direction);fp.fovY=74*DEG;
  }else {
   target=g.plane.pos+vec3(0,a.fusRad*.35f,0);vec3 direction(.85f,.33f,-1.35f);
   if(view.find("side")!=std::string::npos)direction=vec3(1,.065f,0);
   if(view.find("rear")!=std::string::npos)direction=vec3(-.8f,.38f,1.4f);
   if(view.find("top")!=std::string::npos)direction=vec3(.01f,1,.015f);
   if(view.find("belly")!=std::string::npos){direction=vec3(.5f,-.75f,-1.2f);if(!world){fp.hangarOrigin.y=-size;}}
   if(view.find("nose")!=std::string::npos)direction=vec3(.2f,.18f,-1);
   const float distance=size*1.45f;
   eye=target+normalize(direction)*distance;fp.fovY=40*DEG;
   if(view.find("detail")!=std::string::npos){target=g.plane.pos+vec3(0,0,-a.fusLen*.4f);eye=target+normalize(direction)*size*.42f;fp.fovY=44*DEG;}
  }
  if(view.rfind("bay_",0)==0&&model==kAtlas){
    target=g.plane.pos+vec3(0,-2.35f,3.8f);eye=target+vec3(8.f,-7.f,-7.5f);fp.fovY=42*DEG;
  }
  if(view.rfind("fan_",0)==0&&model==kAtlas){
    float side=view.find("left")!=std::string::npos?-1.f:1.f;
    target=g.plane.pos+vec3(side*6.30f,-1.85f,-3.80f);eye=target+vec3(side*1.35f,.22f,-5.1f);fp.fovY=42*DEG;
  }
  if(view.rfind("wheel_",0)==0){
   const auto stations=gearStations(a);bool nose=view.find("nose")!=std::string::npos;float side=view.find("wheel_left")!=std::string::npos?-1.f:1.f; // Nose steering comparisons keep the exact same camera.
   float wr=m.wheelR,nr=a.taildragger?.1f:m.gear==3?wr*.75f:wr*.85f;
   target=g.plane.pos+g.plane.q.rotate(vec3(nose?0:side*stations.track,(nose?nr:wr)-g.plane.gearHeight(),nose?stations.noseZ:stations.mainZ));
   eye=target+vec3(side*1.25f,.46f,-1.55f);fp.fovY=44*DEG;
  }
  if(view.find("breakup")!=std::string::npos){
   target=g.plane.pos+vec3(0,-size*.065f,0);vec3 dir(.8f,.12f,-1.35f);float distance=size*1.60f;fp.fovY=40*DEG;
   if(view.find("full")!=std::string::npos){
    dir=vec3(.85f,.40f,-1.35f);
    if(view.find("rear")!=std::string::npos)dir=vec3(-.8f,.4f,1.35f);
    if(view.find("top")!=std::string::npos)dir=vec3(.01f,1.f,.015f);
    // Fit the real separated body envelopes, especially the expanded nose/tail in top view.
    // Camera-only framing; no piece geometry or orientation is changed here.
    std::vector<vec3> corners;vec3 lo(1e9f),hi(-1e9f);
    for(const auto&w:g.wreck)for(int i=0;i<8;++i){
     const vec3 p=w.c+w.q.rotate(vec3(i&1?w.body.hi.x:w.body.lo.x,i&2?w.body.hi.y:w.body.lo.y,i&4?w.body.hi.z:w.body.lo.z)-w.body.cg);
     corners.push_back(p);lo=vec3(std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z));hi=vec3(std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z));
    }
    if(!corners.empty()){
     target=(lo+hi)*.5f;const vec3 back=normalize(dir),right=normalize(cross(vec3(0,1,0),back)),up=cross(back,right);
     const float tanY=tanf(fp.fovY*.5f),tanX=tanY*(1920.f/1080.f);distance=0;
     for(const vec3&p:corners){const vec3 v=p-target;distance=std::max(distance,dot(v,back)+std::max(fabsf(dot(v,right))/tanX,fabsf(dot(v,up))/tanY));}
     distance+=size*.12f;
    }
   }
   eye=target+normalize(dir)*distance;
  }
  fp.camPos=eye;fp.camBack=normalize(eye-target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);
  return fp;
 }
};
static int identityReview(const char*out){
 if(!initGL(32,32))return 77;std::filesystem::create_directories(out);
 std::ofstream csv(std::string(out)+"/identities.csv");csv<<"model,slot,key,stamp,cache_name\n";
 std::ofstream(std::string(out)+"/driver.txt")<<glGetString(GL_VENDOR)<<"|"<<glGetString(GL_RENDERER)<<"|"<<glGetString(GL_VERSION)<<"\n";
 for(int model=0;model<kAircraftCount;model++)for(int slot=0;slot<2;slot++){
  float M[96];packModelOf(model,M);uint64_t h=1469598103934665603ull ^ uint64_t(slot);
  auto mix=[&](const void*p,size_t n){auto b=(const uint8_t*)p;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}};mix(M,sizeof M);
  if(slot){mix(&model,sizeof model);float foot[4],seat[2];modelCabinFit(model,M[21*4+3],foot,seat);mix(foot,sizeof foot);mix(seat,sizeof seat);float cockpit[36];packCockpitLayout(model,cockpit);mix(cockpit,sizeof cockpit);}
  std::string defines=aircraftDefines(model,M)+(slot==0?"#define AF_OUTSIDE\n":"");const auto stamp=meshCacheStamp(defines);
  char key[17];snprintf(key,sizeof key,"%016llx",(unsigned long long)h);csv<<model<<','<<slot<<','<<key<<','<<stamp<<",mesh_"<<key<<'_'<<stamp<<".bin\n";
  const auto base=std::string(out)+"/"+std::to_string(model)+"_"+std::to_string(slot);
  std::ofstream packed(base+".packed",std::ios::binary);packed.write((const char*)M,sizeof M);
  std::ofstream(base+".sdf.glsl")<<shaderPrune::prune(hullBakeFSAssembly(defines));std::ofstream(base+".normals.glsl")<<shaderPrune::prune(hullBakeFSAssembly(defines+"#define HULL_BAKE_NORMALS\n"));
 }
 return 0;
}
int main(int argc,char**argv){
 if(argc==3&&std::string(argv[1])=="--identity")return identityReview(argv[2]);
 setvbuf(stdout,nullptr,_IONBF,0);if(argc<4){puts("Usage: aircraft_review OUTPUT MODEL VIEWS_CSV [FRAMES=6] [W=1920 H=1080] [world]");return 2;}
 std::string out=argv[1],list=argv[3];int model=atoi(argv[2]),frames=argc>4?atoi(argv[4]):6,W=argc>5?atoi(argv[5]):1920,H=argc>6?atoi(argv[6]):1080;bool world=argc>7&&std::string(argv[7])=="world";
 if(!validAircraft(model)||frames<1||W<32||H<32)return 2;std::filesystem::create_directories(out);if(!initGL(W,H))return 77;
 std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):out+"/cache";std::filesystem::create_directories(cache);
 printf("ACTUAL AIRCRAFT REVIEW: %s, model%d; %dx%d scale1, Medium. Staged %s view, actual production meshes/materials/parts. EGL %s. Not hardware performance evidence.\n",kAircraft[model].name,model,W,H,world?"outdoor":"hangar",glGetString(GL_RENDERER));
 const auto reviewWorldStart=std::chrono::steady_clock::now();g_world.build(cache+"/world.bin","prebuilt-mesh-review-v346-world");printf("REVIEW_WORLD_SECONDS %.9f\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-reviewWorldStart).count());g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.quality=1;g_ren.renderScale=1;g_ren.entSync=true;
#ifdef REVIEW_PORTABLE_API
 if(const char* dir=getenv("REVIEW_PACKAGE_DIR"))g_ren.prebuiltAircraftDir=dir;
 printf("REVIEW_PACKAGE_DIR %s\n",g_ren.prebuiltAircraftDir.c_str());
#endif
 const auto reviewInitStart=std::chrono::steady_clock::now();
 if(!g_ren.init(W,H,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){fprintf(stderr,"INIT FAIL %s\n",g_ren.error.c_str());return 1;}
 printf("REVIEW_INIT_SECONDS %.9f shader_hits=%d shader_misses=%d\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-reviewInitStart).count(),g_shaderCacheHits.load(),g_shaderCacheMisses.load());
 std::ofstream meta(out+"/views.csv");meta<<"name,model,width,height,frames,eye_x,eye_y,eye_z,fov,gear,prop_angle,prop_blur,pitch,roll,yaw,inside,throttle,wreck_pieces,gear_owner_left,gear_owner_right,gear_owner_nose\n";
 std::istringstream views(list);std::string view;
 while(std::getline(views,view,',')){
  const std::string wreckDump=out+"/"+std::string(kAircraft[model].id)+"_"+view+"_wreck.csv";setenv("REVIEW_WRECK_DUMP",wreckDump.c_str(),1);
  FrameParams fp=GameTest::frame(model,view,world);g_ren.resetTemporal();
  if(true){
   std::ofstream state(out+"/"+std::string(kAircraft[model].id)+"_"+view+"_frame_state.csv");state<<std::setprecision(9)<<"field,index,value\n";
   auto arr=[&](const char*n,const float*v,int count){for(int i=0;i<count;++i)state<<n<<','<<i<<','<<v[i]<<'\n';};
   auto vec=[&](const char*n,vec3 v){float a[3]={v.x,v.y,v.z};arr(n,a,3);};
   arr("M",fp.plane.M,96);arr("PS",fp.plane.PS,4);arr("Ctl",fp.plane.Ctl,4);arr("Pr",fp.plane.Pr,4);arr("flame",fp.plane.flame,4);arr("engineHealth",fp.plane.engineHealth,4);
   for(int i=0;i<7;++i)arr(("wr"+std::to_string(i)).c_str(),fp.plane.wr[i],4);
   vec("planePos",fp.plane.pos);arr("planeRot",fp.plane.rot,9);vec("camPos",fp.camPos);vec("camBack",fp.camBack);vec("camRight",fp.camRight);vec("camUp",fp.camUp);vec("colBase",fp.plane.colBase);vec("colStripe",fp.plane.colStripe);
   arr("time",&fp.time,1);arr("fov",&fp.fovY,1);
  }
  const std::string dump=out+"/"+std::string(kAircraft[model].id)+"_"+view+"_geometry.csv";setenv("REVIEW_MESH_DUMP",dump.c_str(),1);
  printf("VIEW %s native%dx%d gear%.2f rpm-blur%.2f inside%.0f\n",view.c_str(),W,H,fp.plane.PS[0],fp.plane.Pr[1],fp.plane.PS[3]);
  for(int f=0;f<frames;++f){auto start=std::chrono::steady_clock::now();g_ren.renderScene(fp,{},{});glFinish();GLenum e=glGetError();printf("FRAME %s %d %.2fs GL%x\n",view.c_str(),f,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),e);if(e)return 3;}
  const std::string name=std::string(kAircraft[model].id)+"_"+view;const std::string path=out+"/"+name+".png";if(!g_ren.screenshotPNG(path.c_str()))return 4;
  meta<<name<<','<<model<<','<<W<<','<<H<<','<<frames<<','<<fp.camPos.x<<','<<fp.camPos.y<<','<<fp.camPos.z<<','<<fp.fovY/DEG<<','<<fp.plane.PS[0]<<','<<fp.plane.Pr[0]<<','<<fp.plane.Pr[1]<<','<<fp.plane.Ctl[0]<<','<<fp.plane.Ctl[1]<<','<<fp.plane.Ctl[2]<<','<<fp.plane.PS[3]<<','<<fp.plane.Ctl[3]<<','<<fp.wreck.pieces<<','<<fp.wreck.gearOwner[0]<<','<<fp.wreck.gearOwner[1]<<','<<fp.wreck.gearOwner[2]<<'\n';meta.flush();printf("SAVED %s\n",path.c_str());
 }
 printf("CACHE shader_hits=%d shader_misses=%d meshes_requested=%d meshes_built=%d\n",g_shaderCacheHits.load(),g_shaderCacheMisses.load(),g_ren.bakeCount,g_ren.bakeBuilt);
#ifdef REVIEW_PORTABLE_API
 printf("REVIEW_PREBUILT hits=%d misses=%d\n",g_ren.prebuiltMeshHits,g_ren.prebuiltMeshMisses);
#endif
 std::ofstream(out+"/shader_notes.txt")<<g_shaderNotes;
 return 0;
}
