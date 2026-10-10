// Staged multi-angle near-LOD review. Real production meshes/materials; no geometry rescaling.
// Usage: environment_asset_review_harness OUTPUT KIND_IDS [frames=8] [angle_count=4] [frozen-views.csv]
#include "renderer.h"
#include "materials.h"
#include "game.h"
#include "entity_lod.h"
#include "environment_review_diagnostics.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <sys/stat.h>
#include <map>
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

static int reviewQuality(){const char* value=getenv("ASSET_REVIEW_QUALITY");return value?std::clamp(atoi(value),0,2):1;}
static int reviewDebug(){const char* value=getenv("ASSET_REVIEW_DBG");return value?atoi(value):0;}
static float reviewHour(){const char* value=getenv("ASSET_REVIEW_HOUR");return value?std::clamp(float(atof(value)),0.f,24.f):14.3f;}
struct GameTest {static void light(FrameParams& fp){Game g;g.wx.cloudCover=.15f;g.computeSun(reviewHour(),fp.sunDir,fp.sunCol,fp.night);}};
static bool shadowReadback(const std::string& out,const std::string& name) {
  if(!getenv("ASSET_REVIEW_SHADOW_DUMP"))return true;
  const auto& d=g_environmentReviewShadowState;
  if(!d.valid||!d.gbuffer||!d.shadow[0]||d.width<=0||d.height<=0||d.shadowResolution<=0)return false;
  const std::string prefix=out+"/"+name;
  GLint previous=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
  std::vector<float> gb(size_t(d.width)*d.height*4),depth(size_t(d.shadowResolution)*d.shadowResolution);
  glBindFramebuffer(GL_FRAMEBUFFER,d.gbuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0,0,d.width,d.height,GL_RGBA,GL_FLOAT,gb.data());
  glBindFramebuffer(GL_FRAMEBUFFER,d.shadow[0]);
  glReadPixels(0,0,d.shadowResolution,d.shadowResolution,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
  glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previous));
  if(glGetError()!=0)return false;
  auto raw=[&](const char* suffix,const std::vector<float>& values){std::ofstream f(prefix+suffix,std::ios::binary);f.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*sizeof(float)));return bool(f);};
  if(!raw("_gb0.f32",gb)||!raw("_shadow_near.f32",depth))return false;
  std::ofstream m(prefix+"_shadow_metadata.json");m<<std::setprecision(10);
  auto values=[&](const char* key,const float* p,int n){m<<"  \""<<key<<"\": [";for(int i=0;i<n;++i)m<<(i?", ":"")<<p[i];m<<"],\n";};
  m<<"{\n  \"format\": \"little-endian float32; row-major pixels; OpenGL bottom row first\",\n"
   <<"  \"gb0_channels\": [\"ray_distance\", \"oct_normal_x\", \"oct_normal_y\", \"gb_class\"],\n"
   <<"  \"width\": "<<d.width<<", \"height\": "<<d.height<<", \"shadow_resolution\": "<<d.shadowResolution<<",\n";
  values("camera",d.camera,3);values("camera_basis_columns_right_up_back",d.basis,9);values("jitter_uv",d.jitter,2);values("sun_direction",d.sun,3);
  values("shadow_matrices_column_major_near_far",d.matrices,32);values("shadow_radii",d.radii,2);
  m<<"  \"tan_half_fov\": "<<d.tanHalf<<", \"aspect\": "<<d.aspect<<",\n  \"readback_only\": true\n}\n";
  printf("SHADOW_READBACK %s: GB0 %dx%d, near depth %dx%d; lighting-pass matrices/uniform state\n",name.c_str(),d.width,d.height,d.shadowResolution,d.shadowResolution);
  return bool(m);
}
static void emptyStage(vec3 origin) {
  const int cx=Scenery::chunkOf(origin.x),cz=Scenery::chunkOf(origin.z);
  // The fixture is isolated only inside this test process. No production world/source is changed.
  for(int z=-3;z<=3;z++)for(int x=-3;x<=3;x++){
    auto*c=g_scenery.ensure(cx+x,cz+z,2);c->ents.clear();std::fill(c->off,c->off+EK_COUNT+1,0u);
    c->ymin=c->ymax=origin.y;c->hmax=c->reach=0;c->markChanged();
  }
}
static void specimen(vec3 origin,int kind,float top,float reach) {
  auto*c=g_scenery.ensure(Scenery::chunkOf(origin.x),Scenery::chunkOf(origin.z),2);c->ents={{origin.x,origin.y,origin.z,0,1,1,1,.37f}};
  for(int k=0;k<=EK_COUNT;k++)c->off[k]=k<=kind?0:1;
  c->ymin=origin.y-4;c->ymax=origin.y+top;c->hmax=top;c->reach=reach;c->markChanged();
}
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);if(argc<3){puts("Usage: environment_asset_review_harness OUTPUT KIND_IDS [frames=8] [angle_count=4] [frozen-views.csv]; KIND_IDS is comma-separated enum integers or all");return 2;}
  struct Frozen{vec3 camera,target;float fov;};std::map<std::string,Frozen> frozen;
  if(argc>5){std::ifstream f(argv[5]);std::string line;std::getline(f,line);while(std::getline(f,line)){std::replace(line.begin(),line.end(),',',' ');std::istringstream q(line);std::string name,angle;int kind;Frozen v;q>>name>>kind>>angle>>v.camera.x>>v.camera.y>>v.camera.z>>v.target.x>>v.target.y>>v.target.z>>v.fov;if(q)frozen[name]=v;}if(frozen.empty()){fputs("No frozen asset views loaded\n",stderr);return 2;}}
  const std::string out=argv[1];int frames=argc>3?std::max(1,atoi(argv[3])):8,angles=argc>4?std::clamp(atoi(argv[4]),1,4):4;
  std::vector<int>kinds;if(std::string(argv[2])=="all"){for(int k=0;k<EK_COUNT;k++)kinds.push_back(k);}else{std::istringstream list(argv[2]);std::string v;while(std::getline(list,v,',')){int k=atoi(v.c_str());if(k<0||k>=EK_COUNT){puts("Bad kind index");return 2;}kinds.push_back(k);}}
  if(kinds.empty())return 2;std::filesystem::create_directories(out);if(!initGL(1920,1080))return 77;
  struct ReplayFrame {vec3 camera,target;float dt=0;int capture=0;};
  std::vector<ReplayFrame> replay;
  if(const char* path=getenv("ASSET_REVIEW_CAMERA_PATH")) {
    std::ifstream f(path);std::string line;std::getline(f,line);
    while(std::getline(f,line)){std::replace(line.begin(),line.end(),',',' ');std::istringstream q(line);int index;ReplayFrame r;
      q>>index>>r.camera.x>>r.camera.y>>r.camera.z>>r.target.x>>r.target.y>>r.target.z>>r.dt>>r.capture;
      if(!q||index!=int(replay.size())||!std::isfinite(r.dt)||r.dt<=0||r.dt>.25f){fputs("Invalid ordered camera replay row or dt\n",stderr);return 2;}
      replay.push_back(r);
    }
    if(replay.empty()||kinds.size()!=1||angles!=1||getenv("ASSET_REVIEW_CAMERA_DISTANCES")){fputs("Camera replay requires one kind, one angle, no distance override\n",stderr);return 2;}
    frames=int(replay.size());setenv("ASSET_REVIEW_COVERAGE_LOG","1",1);
  }
  std::vector<float> distanceOverrides={0.f};
  if(const char* sequence=getenv("ASSET_REVIEW_CAMERA_DISTANCES")) {
    distanceOverrides.clear();std::istringstream list(sequence);std::string item;
    while(std::getline(list,item,',')){float d=float(atof(item.c_str()));if(d<4||d>600||fabsf(d-roundf(d))>.001f){fputs("Review distances must be integer metres in4..600\n",stderr);return 2;}distanceOverrides.push_back(d);}
    if(distanceOverrides.empty()||distanceOverrides.size()>16)return 2;
  }
  printf("GL: %s; native1920x1080 quality%d scale1 debug_mask%d\n%s\n",glGetString(GL_RENDERER),reviewQuality(),reviewDebug(),reviewStartupMode());
  puts("ISOLATED STAGED ASSET REVIEW: unit-scale production meshes on a real flat apron. Nearby scenery cleared only in this test process. No forced LOD or geometry replacement. Not an ordinary generated-town arrangement or hardware benchmark.");
  const std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):out+"/cache";std::filesystem::create_directories(cache);struct stat st{};stat("/proc/self/exe",&st);
  g_world.build(cache+"/world.bin","asset-review-"+std::to_string((long long)st.st_mtime));g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=reviewQuality();g_ren.entSync=true;
  std::vector<EVert>verts;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(verts,ranges);
  std::ofstream mesh(out+"/mesh_inventory.csv");mesh<<"kind,name,lod,triangles,vertices,bytes\n";for(int k=0;k<EK_COUNT;k++)for(int l=0;l<ENT_LODS;l++)mesh<<k<<','<<kEntInfo[k].name<<','<<l<<','<<ranges[k].count[l]/3<<','<<ranges[k].count[l]<<','<<ranges[k].count[l]*sizeof(EVert)<<'\n';mesh.close();
  if(!g_ren.init(1920,1080,[](float p,const std::string&s){printf("INIT %.1f%% %s\n",p*100,s.c_str());})){fprintf(stderr,"init failed %s\n",g_ren.error.c_str());return 1;}
  if(g_ren.matEnvScanned!=kEnvMatLayers||g_ren.matEnvFallbackScanned!=kEnvMatLayers){fputs("Photographed high/fallback maps missing\n",stderr);return 4;}
  // This is the production height sampler used by the terrain shader, at its near octave count.
  // Never place a specimen at a guessed airport elevation or move the production geometry.
  const vec3 origin(-577,g_world.groundHeight(-577,-4413,11),-4413);emptyStage(origin);
  std::ofstream stage(out+"/stage_contact.csv");stage<<"kind,name,origin_x,origin_y,origin_z,ground_min,ground_max,mesh_bottom_world,wheel_part,wheel_bottom_world,ground_at_wheel,wheel_gap_m\n"<<std::setprecision(9);
  std::ofstream mode(out+"/renderer_mode.txt");mode<<reviewStartupMode()<<"\nNative 1920x1080, scale 1; isolated unit-scale fixture; no geometry forcing.\n";mode<<"Hour: "<<reviewHour()<<"; quality: "<<reviewQuality()<<" (0 Low, 1 Medium, 2 High); debug mask: "<<reviewDebug()<<"\n";
  if(reviewNearRadius()>0) {
    mode<<"DIAGNOSTIC ONLY: adapter near shadow radius "<<reviewNearRadius()<<"m, unchanged texture allocation. Production radius/coverage policy is not modified.\n";
    printf("DIAGNOSTIC ONLY: near shadow radius%.1fm at unchanged allocation; not the production cascade coverage policy.\n",reviewNearRadius());
  }
  if(getenv("ASSET_REVIEW_CAMERA_DISTANCES"))mode<<"DIAGNOSTIC distance sequence: unchanged unit meshes and frozen FOV/bearing; natural LOD/crossfade selection is retained.\n";
  if(!replay.empty())mode<<"CAMERA REPLAY: "<<replay.size()<<" ordered main-camera frames; explicit simulation dt; one temporal reset before sequence only.\n";
  mode.close();
  std::ofstream views(out+"/views.csv");views<<"name,kind,angle,camera_x,camera_y,camera_z,target_x,target_y,target_z,fov,close_lod_limit,actual_distance,selected_lod,width,height_above_grade,depth,bottom_y,top_y,camera_source,hour,quality,debug_mask,camera_profile,near_shadow_radius_override,camera_distance_override,crossfade_lod\n"<<std::setprecision(9);
  std::ofstream csv(out+"/frames.csv");csv<<"view,kind,frame,wall_ms,instances,entity_triangles,entity_draws,pending,width,height,render_scale,quality,shadow_near_triangles,shadow_far_triangles,instance_upload_bytes\n";
  std::ofstream coverage;
  if(getenv("ASSET_REVIEW_COVERAGE_LOG")) {
    coverage.open(out+"/coverage_frames.csv");coverage<<std::setprecision(9);
    coverage<<"view,frame,simulation_time,dt,camera_x,camera_y,camera_z,target_x,target_y,target_z,committed_radius,actual_near_radius,actual_far_radius,fade_scale,phase,tier,filtered_speed,agl,near_fade0,near_fade1,near_center_x,near_center_z,far_center_x,far_center_z,near_dirty,far_dirty,radius_changed,shadow_resolution,near_triangles,far_triangles,near_instances,far_instances,near_draws,far_draws,visible_triangles,visible_instances,pending,cpu_wall_ms\n";
  }
  const char* labels[]={"front_oblique","side","rear_oblique","top_oblique"};
  const char* detailLabels[]={"lamp_left_close","lamp_right_close","fascia_frontal_close","lamp_high_oblique"};
  const bool detailProfile=getenv("ASSET_REVIEW_LAMPS")!=nullptr;
  for(int kind:kinds){
    if(detailProfile&&kind!=EK_CAR&&kind!=EK_TRUCK){fputs("Lamp closeups require car/truck kinds\n",stderr);return 2;}
    vec3 lo(1e9f),hi(-1e9f);const auto&r=ranges[kind];
    for(int i=r.first[kEntCloseLod];i<r.first[kEntCloseLod]+r.count[kEntCloseLod];i++){const auto&v=verts[i];lo=vec3(std::min(lo.x,v.px),std::min(lo.y,v.py),std::min(lo.z,v.pz));hi=vec3(std::max(hi.x,v.px),std::max(hi.y,v.py),std::max(hi.z,v.pz));}
    const float height=hi.y-std::max(0.f,lo.y),width=hi.x-lo.x,depth=hi.z-lo.z,radius=.5f*sqrtf(width*width+height*height+depth*depth),distance=radius*2.25f+.75f;
    float groundLo=1e9f,groundHi=-1e9f;for(int z=0;z<=4;++z)for(int x=0;x<=4;++x){float y=g_world.groundHeight(origin.x+lerpf(lo.x,hi.x,x*.25f),origin.z+lerpf(lo.z,hi.z,z*.25f),11);groundLo=std::min(groundLo,y);groundHi=std::max(groundHi,y);}
    if(groundHi-groundLo>.025f){fprintf(stderr,"Fixture footprint is not flat for %s: %.6f..%.6f\n",kEntInfo[kind].name,groundLo,groundHi);return 6;}
    bool anyWheel=false;for(int part=P_WHEEL0;part<=P_WHEEL5;++part){const EVert* bottom=nullptr;for(int i=r.first[kEntCloseLod];i<r.first[kEntCloseLod]+r.count[kEntCloseLod];++i){const auto&v=verts[i];if(int(v.part+.5f)==part&&(!bottom||v.py<bottom->py))bottom=&v;}
      if(bottom){anyWheel=true;float ground=g_world.groundHeight(origin.x+bottom->px,origin.z+bottom->pz,11),gap=origin.y+bottom->py-ground;
        stage<<kind<<','<<kEntInfo[kind].name<<','<<origin.x<<','<<origin.y<<','<<origin.z<<','<<groundLo<<','<<groundHi<<','<<origin.y+lo.y<<','<<part<<','<<origin.y+bottom->py<<','<<ground<<','<<gap<<'\n';
        printf("CONTACT %s wheel=%d bottom=%.6f ground=%.6f gap=%.6f metres\n",kEntInfo[kind].name,part,origin.y+bottom->py,ground,gap);
      }}
    if(!anyWheel)stage<<kind<<','<<kEntInfo[kind].name<<','<<origin.x<<','<<origin.y<<','<<origin.z<<','<<groundLo<<','<<groundHi<<','<<origin.y+lo.y<<",-1,,,\n";stage.flush();
    specimen(origin,kind,hi.y,std::max(width,depth));const vec3 defaultTarget=origin+vec3((lo.x+hi.x)*.5f,std::max(0.f,lo.y)+height*.5f,(lo.z+hi.z)*.5f);
    for(float reviewDistance:distanceOverrides)for(int a=0;a<angles;a++){
      const vec3 directions[]={vec3(.62f,.15f,1),vec3(1,.025f,0),vec3(-.62f,.15f,-1),vec3(.35f,.85f,.55f)};
      const vec3 detailDirections[]={vec3(-.55f,.07f,1),vec3(.55f,.07f,1),vec3(0,0,1),vec3(.4f,.65f,1)};
      const char* angleLabel=detailProfile?detailLabels[a]:labels[a];
      const std::string baseName="kind_"+std::to_string(kind)+"_"+angleLabel;
      const std::string name=baseName+(reviewDistance>0?"_distance_"+std::to_string(int(reviewDistance)):"");vec3 target=defaultTarget;
      if(detailProfile){const float halfLamp=kind==EK_TRUCK?.75f:.56f,lampY=kind==EK_TRUCK?.80f:.67f;target=origin+vec3(a==0?-halfLamp:a==2?0.f:halfLamp,lampY,hi.z-.08f);}
      const vec3 direction=normalize(detailProfile?detailDirections[a]:directions[a]),offset=target-origin;const float closeLimit=entCloseLimit(entRangesFor(g_ren.quality),kind),b=dot(offset,direction),disc=b*b+closeLimit*closeLimit*.64f-dot(offset,offset);
      if(disc<=0){fputs("Asset exceeds its own hero detail range\n",stderr);return 5;}const float detailDistance=(kind==EK_TRUCK?1.9f:1.45f)*(a==2?1.5f:1.f),nearDistance=std::min(detailProfile?detailDistance:distance,-b+sqrtf(disc));if(!detailProfile&&nearDistance<=radius){fputs("Hero camera cannot frame full asset inside detail range\n",stderr);return 5;}
      FrameParams fp{};fp.camPos=target+direction*nearDistance;fp.camBack=normalize(fp.camPos-target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=detailProfile?55*DEG:std::max(50*DEG,2.f*asinf(std::min(.94f,radius/nearDistance))+6*DEG);
      bool cameraFrozen=false;
      if(!frozen.empty()){auto it=frozen.find(baseName);if(it==frozen.end()){if(!getenv("ALLOW_NEW_ASSET_VIEWS")){fprintf(stderr,"Missing frozen asset view %s\n",baseName.c_str());return 2;}printf("NEW VIEW %s: no earlier camera exists; using recorded default\n",baseName.c_str());}
        else {cameraFrozen=true;target=it->second.target;fp.camPos=it->second.camera;fp.camBack=normalize(fp.camPos-target);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);fp.fovY=it->second.fov*DEG;}}
      if(reviewDistance>0)fp.camPos=target+normalize(fp.camPos-target)*reviewDistance;

      fp.cloudCover=.15f;fp.cloudBase=1500;fp.fogB=.00002f;fp.time=20;fp.wind=fp.windSock=vec3(2,0,4);fp.vignette=.1f;g_ren.dbgOff=reviewDebug();GameTest::light(fp);fp.exposure=1.f+fp.night*.8f;fp.rwyLights=fp.night;g_ren.resetTemporal();
      float l0,l1;entLodLimits(entRangesFor(g_ren.quality),kind,l0,l1);const float actual=length(fp.camPos-origin);int lod=entDetailAt(actual,closeLimit,l0,l1),also=entDetailAlso(kind,actual,closeLimit,l0,l1);if(reviewDistance==0&&(lod!=kEntCloseLod||also>=0)){fputs("Fixture camera is not wholly heroLOD\n",stderr);return 5;}
      views<<name<<','<<kind<<','<<angleLabel<<','<<fp.camPos.x<<','<<fp.camPos.y<<','<<fp.camPos.z<<','<<target.x<<','<<target.y<<','<<target.z<<','<<fp.fovY/DEG<<','<<closeLimit<<','<<actual<<','<<lod<<','<<width<<','<<height<<','<<depth<<','<<lo.y<<','<<hi.y<<','<<(reviewDistance>0?(cameraFrozen?"frozen_distance_override":"new_distance_override"):cameraFrozen?"frozen":"new_default")<<','<<reviewHour()<<','<<reviewQuality()<<','<<reviewDebug()<<','<<(reviewDistance>0?"distance_transition":detailProfile?"headlamp_closeup":"full_asset")<<','<<reviewNearRadius()<<','<<reviewDistance<<','<<also<<'\n';views.flush();
      printf("ASSET %s %s selectedLOD=%d crossfade=%d selectedTriangles=%d distance=%.3f limit=%.3f\n",name.c_str(),kEntInfo[kind].name,lod,also,r.count[lod]/3,actual,closeLimit);
      float simulationTime=0;
      for(int f=0;f<frames;f++){
        vec3 currentTarget=target;
        if(!replay.empty()) {
          const auto& motion=replay[f];fp.camPos=motion.camera;currentTarget=motion.target;fp.dt=motion.dt;
          fp.camBack=normalize(fp.camPos-currentTarget);fp.camRight=normalize(cross(vec3(0,1,0),fp.camBack));fp.camUp=cross(fp.camBack,fp.camRight);
        }
        simulationTime+=fp.dt;
        auto start=std::chrono::steady_clock::now();g_ren.renderScene(fp,{},{});glFinish();if(glGetError())return 3;double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();csv<<name<<','<<kind<<','<<f<<','<<ms<<','<<reviewEntityInstances[0]<<','<<reviewEntityTriangles[0]<<','<<reviewEntityDrawCalls[0]<<','<<g_ren.entPending<<",1920,1080,1,"<<reviewQuality()<<','<<reviewEntityTriangles[1]<<','<<reviewEntityTriangles[2]<<','<<reviewEntityUploadBytes<<'\n';csv.flush();
        if(coverage.is_open()) {
          const auto& d=g_environmentReviewShadowState;if(!d.valid){fputs("Coverage state not recorded\n",stderr);return 8;}
          coverage<<name<<','<<f<<','<<simulationTime<<','<<fp.dt<<','<<fp.camPos.x<<','<<fp.camPos.y<<','<<fp.camPos.z<<','<<currentTarget.x<<','<<currentTarget.y<<','<<currentTarget.z
            <<','<<d.committedRadius<<','<<d.radii[0]<<','<<d.radii[1]<<','<<d.fadeScale<<','<<d.phase<<','<<d.tier<<','<<d.filteredSpeed<<','<<d.agl<<','<<d.nearFade[0]<<','<<d.nearFade[1]
            <<','<<d.centers[0]<<','<<d.centers[1]<<','<<d.centers[2]<<','<<d.centers[3]<<','<<d.nearDirty<<','<<d.farDirty<<','<<d.radiusChanged<<','<<d.shadowResolution
            <<','<<reviewEntityTriangles[1]<<','<<reviewEntityTriangles[2]<<','<<reviewEntityInstances[1]<<','<<reviewEntityInstances[2]<<','<<reviewEntityDrawCalls[1]<<','<<reviewEntityDrawCalls[2]
            <<','<<reviewEntityTriangles[0]<<','<<reviewEntityInstances[0]<<','<<g_ren.entPending<<','<<ms<<'\n';coverage.flush();
        }
        if(!replay.empty()&&replay[f].capture){std::ostringstream key;key<<name<<"_frame_"<<std::setw(3)<<std::setfill('0')<<f;if(!g_ren.screenshotPNG((out+"/"+key.str()+".png").c_str()))return 1;printf("REPLAY_KEYFRAME %s frame%d\n",key.str().c_str(),f);}
      }
      if(!g_ren.screenshotPNG((out+"/"+name+".png").c_str()))return 1;printf("WROTE %s/%s.png\n",out.c_str(),name.c_str());
      if(!shadowReadback(out,name)){fputs("Requested shadow diagnostic readback failed\n",stderr);return 7;}
    }
  }
  return 0;
}
