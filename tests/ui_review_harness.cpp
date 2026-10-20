// Production UI review capture on surfaceless EGL; no display server required.
// ui modes intentionally omit the 3D scene and print this limitation to stdout.
#include "../src/game.h"
#include "../src/menu_layout.h"
#include "../src/hangar_catalog.h"
#include "../src/models.h"
#include "../src/military_missions.h"
#include "../src/load_pacer.h"
#include "../src/ui_identity.h"
#include <dlfcn.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
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
  // Diagnostic-only: isolate software-GL mixed-fleet shader compilation when
  // explicitly requested. Production settings and default fixture traffic stay on.
  static bool airTrafficEnabled() { const char* value=getenv("HARNESS_AIR_TRAFFIC"); return !value||std::string(value)!="0"; }
  static void checkSeamless(Game& g, const std::string& scene, const std::string& output) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const std::string id=scene.substr(9);
    auto start=procedural::find(id);require(start.has_value(),"seamless fixture needs a canonical region ID or solace");
    g.cacheDir=getenv("SHADERCACHE")?getenv("SHADERCACHE"):"";g.buildStamp="seamless-egl-v1";
    g.debugScene("region-"+id);g.freeCraft=kGalleon;g.freeAirport=0;g.freeAirborne=true;g.launchFreeFlight();
    g.set.traffic=airTrafficEnabled();g_ren.entSync=false;g_ren.profileRegionRender=true;g.hudOn=false;g.set.fov=40;g.toasts.clear();
    // Clear diagnostic weather only; ordinary Free Flight defaults are unchanged.
    g.wx.visibility=150000.f;g.wx.cloudCover=0;g.wx.precip=0;g.wx.storm=false;
    g.wxStart=g.contract.wx=g.contract.wxEnd=g.wx;g.contract.wxShift=false;
    procedural::Descriptor neighbor;
    for(const auto& candidate:procedural::nearby(start->origin,12,start->worldSeed))
      if(!candidate.authored&&candidate.id!=start->id&&(start->authored||std::abs(std::remainder(candidate.origin.longitude-start->origin.longitude,360.))>1.)){neighbor=candidate;break;}
    require(!neighbor.id.empty(),"seamless fixture needs a neighboring region");
    if(start->authored){
      // Use the production topology barrier during fixture preparation, before
      // measured flight frames. The authored geometry remains untouched.
      g.setEarthDestination(neighbor);
      const auto ready=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      while(g_world.repeating&&std::chrono::steady_clock::now()<ready)g.updateEarthFlight();
      require(!g_world.repeating,"Solace topology activation did not complete");
      g.retireEarthPrefetch();if(g.earthRetireJob.valid())g.earthRetireJob.get();
    }
    const double bearing=*geo::initialBearing(start->origin,neighbor.origin);
    double lo=0,hi=geo::surfaceDistance(start->origin,neighbor.origin);
    for(int i=0;i<48;++i){const double mid=(lo+hi)*.5;
      if(procedural::describe(geo::destination(start->origin,bearing,mid),start->worldSeed).id==start->id)lo=mid;else hi=mid;}
    geo::Position before=geo::destination(start->origin,bearing,hi-10000.);before.height=5000.;
    geo::Position across; bool admissible=false;
    double oldClearance=0,newClearance=0,publicationDistance=0;
    // Solace's irregular ownership can end at 71 km, much earlier than the
    // live scenery drain guard. Find the first point satisfying the actual
    // ownership/hysteresis/60 km old-chart clearance, with at least 5 km left
    // before the destination's entire 80 km square (a conservative land bound).
    const double centerDistance=geo::surfaceDistance(start->origin,neighbor.origin);
    const double destinationEnvelope=std::sqrt(2.)*40000.;
    for(double distance=hi+1000.;distance<centerDistance-destinationEnvelope-5000.;distance+=1000.) {
      const auto pGeo=geo::destination(start->origin,bearing,distance);
      const auto behind=geo::destination(start->origin,bearing,distance-250.);
      const vec3 p=regionTravel::Chart(start->origin).local(pGeo);
      const double dx=std::max(0.,double(std::abs(p.x))-40000.),dz=std::max(0.,double(std::abs(p.z))-40000.);
      const double clearance=std::hypot(dx,dz);
      if(clearance>60500.&&procedural::describe(pGeo,start->worldSeed).id==neighbor.id&&
          procedural::describe(behind,start->worldSeed).id==neighbor.id) {
        across=pGeo;across.height=5000.;oldClearance=clearance;
        newClearance=geo::surfaceDistance(pGeo,neighbor.origin)-destinationEnvelope;
        publicationDistance=distance;admissible=true;break;
      }
    }
    require(admissible&&newClearance>5000.,"fixture needs an admissible handoff safely before destination land");
    printf("SEAMLESS_GEOMETRY owner_edge_km=%.3f publication_distance_km=%.3f old_square_clearance_km=%.3f destination_square_disk_clearance_km=%.3f\n",
      hi/1000.,publicationDistance/1000.,oldClearance/1000.,newClearance/1000.);
    const regionTravel::Chart initial(start->origin);
    auto place=[&](geo::Position p){const vec3 local=initial.local(p);const float heading=float(*initial.bearing(local,neighbor.origin));
      g.plane.reset(&kAircraft[kGalleon],local,heading,kAircraft[kGalleon].maxFuel*.8f,85.f,true,kAircraft[kGalleon].cruise);
      g.settleAirborneStart();g.plane.apEngage(Plane::AP_HOLD,-1,g.wx);g.plane.apAlt=5000;g.plane.apHeading=heading;
      g.camPos=g.plane.pos;g.camQ=g.plane.q;};
    place(before);g.earthFlight=false;g.paused=false;g.botControl=true;
    g.dbgCam=true;g.dbgFollow=false;
    auto view=[&](bool backward){const regionTravel::Chart current(g.flightRegion.origin);
      g.dbgCamPos=g.plane.pos+vec3(0,15,0);g.dbgCamLook=current.local(backward?start->origin:neighbor.origin);};
    std::ofstream timings(output+"/"+scene+"-frames.csv");
    timings<<"frame,phase,region,flight_seconds,update_ms,render_ms,total_ms,upload_bytes,upload_ms,commit_ms,distant_mesh_draws,terrain_ms,shadow_ms,lighting_ms,shadow_rows,horizon_wanted,horizon_ready,horizon_resident,horizon_bytes,horizon_ms,combined_bytes,horizon_cache_bytes,horizon_selection_complete,horizon_pending,air_traffic_enabled,horizon_peak_resident,aircraft_shadow_ms,traffic_shadow_ms,enemy_shadow_ms,displays_ms,feeds_ms,objects_ms,shadow_proxy_ms,postprocess_ms,world_data_rows,staged_data_rows,horizon_collision_grid_bytes,horizon_pending_grid_bytes,horizon_pinned_meshes\n";
    int frames=0;double maximum=0,maximumUpdate=0,maximumPump=0;size_t maximumBytes=0;
    std::string phase="warmup";std::map<std::string,std::vector<double>> phaseFrames;
    bool handoffSeen=false;double handoffFrameMs=0;
    auto step=[&](){view(false);const float clock=g.flightClock;const auto t0=std::chrono::steady_clock::now();
      g.update(1.f/60);g.in.endFrame();
      // Fixture-only baseline holds the detailed candidate cold while still
      // preparing the same visible neighborhood as normal Earth flight.
      if(!g.earthFlight)g_ren.updateRegionHorizon(regionTravel::Chart(g.flightRegion.origin).position(g.dbgCamPos),g.flightRegion.worldSeed);
      const auto t1=std::chrono::steady_clock::now();view(false);g.render();glFinish();
      const auto t2=std::chrono::steady_clock::now();
      auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
      const double u=ms(t0,t1),r=ms(t1,t2),total=ms(t0,t2);maximum=std::max(maximum,total);maximumUpdate=std::max(maximumUpdate,u);
      maximumPump=std::max(maximumPump,g_ren.worldUploadMsThisFrame);maximumBytes=std::max(maximumBytes,g_ren.worldUploadBytesThisFrame);
      phaseFrames[phase].push_back(total);
      if(g_world.regionId!=start->id&&!handoffSeen){handoffSeen=true;handoffFrameMs=total;require(g_ren.terrainShadowRowsThisFrame<=8,"handoff must not rebake all terrain shadow rows synchronously");}
      timings<<frames++<<','<<phase<<','<<g_world.regionId<<','<<g.flightClock<<','<<u<<','<<r<<','<<total<<','<<g_ren.worldUploadBytesThisFrame<<','<<g_ren.worldUploadMsThisFrame<<','<<g_ren.worldUploadCommitMs<<','<<g_ren.distantRegionDraws<<','<<g_ren.regionTerrainRenderMs<<','<<g_ren.regionShadowRenderMs<<','<<g_ren.regionLightingRenderMs<<','<<g_ren.terrainShadowRowsThisFrame<<','<<g_ren.horizonWantedMeshes<<','<<g_ren.horizonReadyMeshes<<','<<g_ren.horizonResidentMeshes<<','<<g_ren.horizonUploadBytesThisFrame<<','<<g_ren.horizonUploadMsThisFrame<<','<<(g_ren.worldUploadBytesThisFrame+g_ren.horizonUploadBytesThisFrame)<<','<<g_ren.horizonCacheBytes<<','<<g_ren.horizonSelectionComplete<<','<<g_ren.horizonPendingMeshes<<','<<g.set.traffic<<','<<g_ren.horizonPeakResidentMeshes;
      for (double cost : g_ren.regionAuxRenderMs) timings<<','<<cost;
      timings<<','<<g_ren.regionWorldDataRows<<','<<g_ren.regionStagedDataRows<<','<<g_ren.horizonCpuGridBytes<<','<<g_ren.horizonPendingGridBytes<<','<<g_ren.horizonPinnedMeshes<<'\n';timings.flush();
      require(g.screen==SCR_FLIGHT&&!g.paused&&!g.crashed,"seamless frame must keep actual flight running");
      require(g.flightClock>clock,"seamless simulation clock cannot freeze during preparation");
      require(g_ren.worldUploadBytesThisFrame+g_ren.horizonUploadBytesThisFrame<=2*1024*1024,"combined horizon/detail staging exceeded its per-frame byte budget");
      require(g_ren.horizonError.empty(),"visible-neighborhood generation failed during fixture");
      require(g_ren.horizonResidentMeshes<=64&&g_ren.horizonPeakResidentMeshes<=64,"visible-neighborhood resident mesh cap exceeded");
      require(g.earthPrefetchError.empty(),"region prefetch failed during seamless render fixture");
      if(frames%30==0)printf("SEAMLESS frame=%d region=%s flight=%.3f update=%.3fms render=%.3fms uploaded=%zu\n",frames,g_world.regionId.c_str(),g.flightClock,u,r,g_ren.worldUploadBytesTotal);
    };
    printf("SEAMLESS FIXTURE: explicit test-only placement at 5 km altitude, clear 150 km visibility / no cloud, near %s -> %s ownership boundary; real Game update/render, asynchronous World generation and budgeted GPU upload.\n",start->id.c_str(),neighbor.id.c_str());
    printf("HARNESS_AIR_TRAFFIC=%d (diagnostic fixture setting; road traffic and scenery enabled)\n",int(g.set.traffic));
    // Cold first-use costs are retained in their own rows, never discarded.
    for(int i=0;i<5;++i)step();
    const auto horizonDeadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    while((g_ren.horizonWantedMeshes<=0||g_ren.horizonCpuPending||g_ren.horizonPendingMeshes>0||g_ren.horizonReadyMeshes<g_ren.horizonWantedMeshes)&&std::chrono::steady_clock::now()<horizonDeadline)step();
    require(g_ren.horizonWantedMeshes>0&&g_ren.horizonReadyMeshes>=g_ren.horizonWantedMeshes&&g_ren.horizonSelectionComplete,"warm baseline needs its entire requested visible neighborhood");
    phase="baseline";for(int i=0;i<30;++i)step();
    phase="prefetch";place(across);g.setEarthDestination(neighbor);
    step();require(g_world.regionId==start->id,"late-readiness frame must retain its active chart");
    // Move back only as explicit harness setup, allowing complete before/after
    // views of the same crossing while the production scheduler prepares normally.
    place(before);g.setEarthDestination(neighbor);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(15);
    while(!g_ren.worldUploadReady()&&std::chrono::steady_clock::now()<deadline)step();
    require(g_ren.worldUploadReady()&&g_world.regionId==start->id,"candidate must become fully GPU-ready while the original flight remains active");
    place(across);g.setEarthDestination(neighbor);
    auto capture=[&](const char* phase,bool backward){g.toasts.clear();view(backward);for(int i=0;i<3;++i)g.render();glFinish();
      const std::string path=output+"/"+scene+"-"+phase+(backward?"-backward.png":"-forward.png");
      require(g_ren.screenshotPNG(path.c_str()),"could not write seamless screenshot");printf("WROTE %s distant_mesh_draws=%d\n",path.c_str(),g_ren.distantRegionDraws);};
    capture("before",false);capture("before",true);
    const auto geographicBefore=regionTravel::Chart(g.flightRegion.origin).position(g.plane.pos);
    const float fuelBefore=g.plane.fuel;
    const auto handoffDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    phase="handoff";
    while(g_world.regionId==start->id&&std::chrono::steady_clock::now()<handoffDeadline)step();
    require(g_world.regionId==neighbor.id,"ready region did not publish over ocean");
    const double motion=geo::surfaceDistance(geographicBefore,regionTravel::Chart(g.flightRegion.origin).position(g.plane.pos));
    require(motion<1000.&&g.plane.fuel<=fuelBefore&&g.plane.fuel>fuelBefore-10.f,"handoff must preserve position and fuel continuity");
    capture("after",false);capture("after",true);
    phase="after";for(int i=0;i<30;++i)step();
    for(auto& entry:phaseFrames){auto values=entry.second;std::sort(values.begin(),values.end());
      auto quantile=[&](double p){return values[std::min(values.size()-1,size_t(std::ceil(p*values.size())-1))];};
      printf("SEAMLESS_PHASE %s frames=%zu p95_ms=%.3f p99_ms=%.3f worst_ms=%.3f\n",entry.first.c_str(),values.size(),quantile(.95),quantile(.99),values.back());}
    printf("SEAMLESS_HANDOFF full_frame_ms=%.3f horizon_wanted=%d ready=%d resident=%d peak=%d cpu_meshes=%zu cache_bytes=%zu\n",handoffFrameMs,g_ren.horizonWantedMeshes,g_ren.horizonReadyMeshes,g_ren.horizonResidentMeshes,g_ren.horizonPeakResidentMeshes,g_ren.horizonCpuBuildCount,g_ren.horizonCacheBytes);
    printf("SEAMLESS FUNCTIONAL_PASS (no frame-latency acceptance threshold) frames=%d simulation_seconds=%.3f max_frame_ms=%.3f max_update_ms=%.3f max_upload_bytes=%zu max_upload_ms=%.3f begin_ms=%.3f commit_ms=%.3f total_upload_bytes=%zu upload_frames=%u handoff_motion_m=%.3f\n",frames,g.flightClock,maximum,maximumUpdate,maximumBytes,maximumPump,g_ren.worldUploadBeginMs,g_ren.worldUploadCommitMs,g_ren.worldUploadBytesTotal,g_ren.worldUploadFrames,motion);
    printf("REGIONAL_COLLISION_GRIDS resident_bytes=%zu pending_bytes=%zu pinned_meshes=%d\n",g_ren.horizonCpuGridBytes,g_ren.horizonPendingGridBytes,g_ren.horizonPinnedMeshes);
    g.retireEarthPrefetch();g_ren.profileRegionRender=false;
  }
  static void checkHorizon(Game& g,const std::string& scene,const std::string& output) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const size_t split=scene.find('-');
    require(split!=std::string::npos,"horizon fixture requires horizon<km>-<regionID>");
    const float altitude=1000.f*float(atof(scene.substr(7,split-7).c_str()));
    const auto descriptor=procedural::find(scene.substr(split+1));
    require(descriptor&&altitude>=1000.f&&altitude<=40000.f&&!descriptor->authored,"horizon fixture needs generated region and 1-40 km camera altitude");
    // Keep foreground land in frame as well as the curved horizon. A sea-level
    // target 180 km away tilted down only 5-10 degrees in the 16-30 km fixtures.
    float pitchDegrees=18.f;
    if(const char* value=getenv("HARNESS_HORIZON_PITCH_DEG")) {
      char* end=nullptr;pitchDegrees=strtof(value,&end);
      require(end!=value&&*end=='\0'&&std::isfinite(pitchDegrees)&&pitchDegrees>=0.f&&pitchDegrees<=80.f,
        "HARNESS_HORIZON_PITCH_DEG must be a finite downward angle from 0 to 80 degrees");
    }
    const float pitch=pitchDegrees*DEG;
    g.cacheDir=getenv("SHADERCACHE")?getenv("SHADERCACHE"):"";g.buildStamp="seamless-egl-v1";
    g.debugScene("region-"+descriptor->id);g.freeCraft=kGalleon;g.freeAirborne=true;g.launchFreeFlight();
    g.set.traffic=airTrafficEnabled();g.botControl=true;g.paused=false;g.hudOn=false;g.set.fov=55;g.dbgCam=true;g.dbgFollow=false;
    g.plane.apEngage(Plane::AP_HOLD,-1,g.wx);g.plane.apAlt=g.plane.pos.y;
    g.wx.visibility=200000.f;g.wx.cloudCover=0;g.wx.precip=0;g.wx.storm=false;
    g.wxStart=g.contract.wx=g.contract.wxEnd=g.wx;g.contract.wxShift=false;
    g_ren.entSync=false;g_ren.profileRegionRender=true;
    auto view=[&](int direction){const float angle=direction*PI*.5f;
      g.dbgCamPos=vec3(0,altitude,0);
      g.dbgCamLook=g.dbgCamPos+vec3(sinf(angle)*cosf(pitch),-sinf(pitch),-cosf(angle)*cosf(pitch))*180000.f;};
    std::ofstream log(output+"/"+scene+"-frames.csv");
    log<<"frame,flight_seconds,total_ms,wanted,ready,resident,pending,cpu_pending,selection_complete,horizon_bytes,detail_bytes,combined_bytes,cache_bytes,air_traffic_enabled,peak_resident,collision_grid_bytes,pending_grid_bytes,pinned_meshes,camera_down_pitch_deg\n";
    int frames=0;double maximum=0;const auto started=std::chrono::steady_clock::now();
    auto step=[&]{view(0);const float clock=g.flightClock;const auto t0=std::chrono::steady_clock::now();
      g.update(1.f/60);g.in.endFrame();view(0);g.render();glFinish();
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();maximum=std::max(maximum,ms);
      const size_t bytes=g_ren.horizonUploadBytesThisFrame+g_ren.worldUploadBytesThisFrame;
      log<<frames++<<','<<g.flightClock<<','<<ms<<','<<g_ren.horizonWantedMeshes<<','<<g_ren.horizonReadyMeshes<<','<<g_ren.horizonResidentMeshes<<','<<g_ren.horizonPendingMeshes<<','<<g_ren.horizonCpuPending<<','<<g_ren.horizonSelectionComplete<<','<<g_ren.horizonUploadBytesThisFrame<<','<<g_ren.worldUploadBytesThisFrame<<','<<bytes<<','<<g_ren.horizonCacheBytes<<','<<g.set.traffic<<','<<g_ren.horizonPeakResidentMeshes<<','<<g_ren.horizonCpuGridBytes<<','<<g_ren.horizonPendingGridBytes<<','<<g_ren.horizonPinnedMeshes<<','<<pitchDegrees<<'\n';log.flush();
      require(g.screen==SCR_FLIGHT&&!g.paused&&!g.crashed&&g.flightClock>clock,"high-altitude camera must not freeze the underlying actual flight");
      require(g_ren.horizonResidentMeshes<=64&&g_ren.horizonPeakResidentMeshes<=64,"high-altitude resident mesh cap exceeded");
      require(bytes<=2*1024*1024,"high-altitude combined upload budget exceeded");require(g_ren.horizonError.empty(),"high-altitude horizon worker failed");
      if(frames%30==0)printf("HORIZON frame=%d wanted=%d ready=%d resident=%d bytes=%zu flight=%.3f\n",frames,g_ren.horizonWantedMeshes,g_ren.horizonReadyMeshes,g_ren.horizonResidentMeshes,bytes,g.flightClock);
    };
    printf("HORIZON FIXTURE camera-only altitude=%.0fm, downward_pitch_deg=%.2f, static all-direction views, clear200km/no cloud. Aircraft remains in normal low-altitude Galleon Free Flight; 30km is a visibility stress, not aircraft performance. Initial cache ready=%d wanted=%d.\n",altitude,pitchDegrees,g_ren.horizonReadyMeshes,g_ren.horizonWantedMeshes);
    printf("HARNESS_AIR_TRAFFIC=%d (diagnostic fixture setting; road traffic and scenery enabled)\n",int(g.set.traffic));
    const auto deadline=started+std::chrono::minutes(5);
    do{step();}while((g_ren.horizonWantedMeshes<=0||g_ren.horizonReadyMeshes<g_ren.horizonWantedMeshes||g_ren.horizonCpuPending||g_ren.horizonPendingMeshes>0)&&std::chrono::steady_clock::now()<deadline);
    require(g_ren.horizonWantedMeshes>0&&g_ren.horizonReadyMeshes>=g_ren.horizonWantedMeshes&&g_ren.horizonSelectionComplete,"high-altitude neighborhood did not settle completely");
    const double settle=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    for(int i=0;i<30;++i)step();
    for(int direction=0;direction<4;++direction){g.toasts.clear();view(direction);for(int i=0;i<3;++i)g.render();glFinish();
      const std::string path=output+"/"+scene+"-"+std::to_string(direction*90)+"deg.png";
      require(g_ren.screenshotPNG(path.c_str()),"could not save high-altitude screenshot");printf("WROTE %s distant_draws=%d\n",path.c_str(),g_ren.distantRegionDraws);}
    printf("HORIZON PASS altitude_m=%.0f downward_pitch_deg=%.2f settle_seconds=%.3f frames=%d wanted=%d ready=%d resident=%d peak=%d cpu_meshes=%zu cache_bytes=%zu max_full_frame_ms=%.3f\n",altitude,pitchDegrees,settle,frames,g_ren.horizonWantedMeshes,g_ren.horizonReadyMeshes,g_ren.horizonResidentMeshes,g_ren.horizonPeakResidentMeshes,g_ren.horizonCpuBuildCount,g_ren.horizonCacheBytes,maximum);
    printf("REGIONAL_COLLISION_GRIDS resident_bytes=%zu pending_bytes=%zu pinned_meshes=%d\n",g_ren.horizonCpuGridBytes,g_ren.horizonPendingGridBytes,g_ren.horizonPinnedMeshes);
    g.retireEarthPrefetch();g_ren.profileRegionRender=false;
  }
  static void checkInteractions(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    auto frame=[&](){g.focusList.clear();g_ren.uiBegin();FrameParams fp{};g.drawResearch(fp);g_ren.uiEnd();g.in=Input{};};
    const int ids[]={kVigil,kReverie,kResearchJet,kSomnus};
    require(g.resCraft==kResearchJet,"original research default is XR-30");
    g.debugScene("research10");
    for(int cycle=0;cycle<3;cycle++) for(int i=0;i<4;i++){
      require(g.resCraft==ids[i],"research keyboard sequence");
      g.in.pressed[K_TAB]=true;frame();
    }
    // Click the actual adaptive cards: tall viewports use the original list, constrained ones compact tiles.
    auto L=g.researchLayout();const float s=g.S();
    for(int cycle=0;cycle<3;cycle++) for(int i=0;i<4;i++){
      Game::Focusable target{};for(const auto& f:g.focusList)if(f.id==0x7E5C0000u+uint32_t(i))target=f;
      require(target.id,"research adaptive layout exposes each airframe target");
      g.in.mx=target.x+target.w*.5f;g.in.my=target.y+target.h*.5f;g.in.mPressed[0]=true;frame();
      require(g.resCraft==ids[i],"research mouse airframe selection");
    }
    g.resCard=0;g.resCraft=kSomnus;frame();require(g.resCard==-1,"incompatible research test cleared");
    g.resAirport=0;g.in.pressed[K_LEFT]=true;frame();require(g.resAirport==(int)g_world.airports.size()-1,"site wraps left");
    g.in.pressed[K_RIGHT]=true;frame();require(g.resAirport==0,"site wraps right");
    const float x1=g_ren.W-24*s,hs=s*std::clamp((x1-L.lx)/(1232.f*s),.55f,1.f);
    const float c2=L.lx+(16+112+290+40)*hs,c3=c2+182*hs,chipY=L.bot+(16+12+16+17)*s;
    g.in.mx=c2+(90+38)*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(!g.resAirborne,"runway start hit target");
    g.in.mx=c2+43*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(g.resAirborne,"airborne start hit target");
    for(int i=0;i<3;i++){g.in.mx=c3+(80*i+38)*hs;g.in.my=chipY;g.in.mPressed[0]=true;frame();require(g.resWx==i,"weather hit target");}
    g.in.pressed[K_ESC]=true;frame();require(g.screen==SCR_MENU,"research escape returns to menu");
    printf("PASS original research XR-30 default, repeated keyboard/mouse selections, incompatible test reset, site wrapping, start position, weather, escape\n");
  }
  static void checkScrollFocus(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    g.career.money=10000000;g.career.license=LIC_ATP;
    g.career.fleet.push_back({0,g.career.location,50,.5f});g.screen=SCR_HUB;
    for(int tab:{TAB_HANGAR,TAB_AIRLINE}){
      g.hubTab=tab;g.selHangar=1;g.focusNav=false;g.focusList.clear();
      g_ren.uiBegin();g.drawHub();g_ren.uiEnd();
      require(!g.focusList.empty(),"scroll region registers buttons");
      std::vector<Game::Focusable> inspector;for(const auto& f:g.focusList)if(f.x>g_ren.W*.5f)inspector.push_back(f);require(!inspector.empty(),"inspector registers economic actions");
      auto target=*std::max_element(inspector.begin(),inspector.end(),[](const Game::Focusable& a,const Game::Focusable& b){return a.y<b.y;});
      g.focusId=target.id;g.focusNav=true;
      for(int frame=0;frame<3;frame++){
        g.focusList.clear();g_ren.uiBegin();g.drawHub();g_ren.uiEnd();
        auto it=std::find_if(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==target.id;});
        require(it!=g.focusList.end()&&g.focusId==target.id&&g.focusNav,"scroll preserves keyboard focus identity");
        if(frame==2)require(it->y>=0&&it->y+it->h<=g_ren.H,"focused bottom action scrolls on screen");
      }
      printf("PASS tab %d offscreen action focus identity and reachability\n",tab);
    }
  }
  static void checkResearchLoadout(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    g.debugScene("research40");g.in=Input{};g.uiDt=1.f/60;g.focusNav=false;g.researchView.idle=0;
    auto frame=[&](){g.gamepadMenus(1.f/60);g.focusNavigate();g_ren.uiBegin();FrameParams fp{};
      g.researchPreviewCamera(fp);g.drawResearch(fp);g_ren.uiEnd();g.in.endFrame();};
    auto control=[&](uint32_t id){for(const auto& f:g.focusList)if(f.id==id)return f;return Game::Focusable{};};
    frame();frame();const auto L=g.researchLayout();const float s=g.S();
    for(uint32_t id:{0x7E5F0010u,0x7E5F0011u,0x7E5F0012u}) {
      const auto f=control(id);
      require(f.id==id && f.x>=L.rx && f.x+f.w<=L.rx+L.rw && f.y>=L.top+36*s && f.y+f.h<=L.bot-24*s,
        "research equipment is visible inside right rail, outside aircraft stage");
      const float yaw=g.researchView.yaw;g.focusNav=false;g.in.mx=f.x+f.w*.5f;g.in.my=f.y+f.h*.5f;
      g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=12;frame();
      require(!g.researchView.drag && std::fabs(g.researchView.yaw-yaw)<.001f,"research rail click cannot drag aircraft preview");
      g.in.mDown[0]=false;frame();
    }
    FrameParams equipped{};g.researchPreviewCamera(equipped);
    require(equipped.plane.model==kSomnus && equipped.plane.wrBombSet==int(g.practiceBomb) &&
      int(equipped.plane.wr[6][3])==int(g.practiceForward) && equipped.plane.wr[6][0]>0,
      "research render frame carries selected weapon and payload model");
    for(uint32_t id:{0x7E5F0030u,0x7E5F0031u,0x7E5F0032u,0x7E5F0033u,0x7E5F0034u}) {
      const auto f=control(id);
      require(f.id==id && f.x>=L.px0 && f.x+f.w<=L.px1+.01f && f.y>=L.bot-56*s && f.y+f.h<=L.bot,
        "research preview controls fit a dedicated strip outside aircraft stage and equipment rail");
    }
    const float yaw=g.researchView.yaw;g.focusNav=false;g.in.mx=L.cx;g.in.my=L.bot-76*s;
    g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=20;frame();
    require(g.researchView.drag && std::fabs(g.researchView.yaw-yaw-.16f)<.001f,"lower aircraft stage accepts drag across former equipment overlay");
    g.in.mDown[0]=false;frame();g.in.mx=L.rx+L.rw*.5f;g.in.my=L.bot-40*s;g.in.wheel=-1000;frame();frame();
    require(control(0x7E5F0010u).y<L.top+36*s,"research specifications scroll beneath their clipped rail");
    g.focusNav=true;g.focusId=0x7E5F0010u;frame();frame();
    require(control(0x7E5F0010u).y>=L.top+36*s-.01f,"focused equipment scrolls back into view");
    g.in=Input{};g.focusNav=false;g.in.pressed[K_ESC]=true;frame();
    require(g.screen==SCR_MENU,"research loadout review returns without launching");
    printf("PASS research right-rail kit layout, native preview equipment fields, no clickthrough, full-stage drag and focus scroll\n");
  }
  static void checkLoading(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    const auto oldPending=g_ren.entPending;
    require(!g_ren.tshRequiredPending(1.f),"missing optional shadow shader does not block readiness");
    require(!g_ren.tshRequiredPending(-.1f),"night shadow does not block readiness");
    g_ren.entPending=0;
    g.in=Input{};g.loadFrames=2;g.loadObservedFrame=-1;g.loadStableFrames=0;g.loadReadyT=-1;g.loadShown=0;g.loadBakeSeen=g_ren.bakeCount;
    for(int i=0;i<30;i++)g.updateLoading(1.f/30);
    require(g.loadReadyT<0&&g.loadStableFrames==1&&g.loadShown<1,"repeated updates without a rendered frame cannot complete loading");
    g.loadFrames=3;g.updateLoading(1.f/30);require(g.loadReadyT>=0&&g.loadShown==1,"distinct stable completed frame permits readiness");
    g_ren.entPending=oldPending;
    printf("PASS loading requires distinct completed frames; absent optional shadow shader cannot deadlock\n");
  }
  static void checkHangarFraming(Game& g) {
    for(int row=0;row<hangarCatalogCount();row++){
      const int i=hangarSpecAt(row);g.selHangar=i;FrameParams fp{};g.hangarPreviewCamera(fp);
      const float s=g.S(),W=g_ren.W,H=g_ren.H;
      const auto L=hangarLayout(24*s,126*s,W-48*s,H-146*s,s);
      const auto& a=kAircraft[i];float th=tanf(fp.fovY*.5f);
      const auto& md=kModels[i];const auto gst=gearStations(a);const float gh=fp.plane.M[19*4],wr=a.special?.38f:md.wheelR;
      const float nr=a.special?.33f:a.taildragger?.1f:md.gear==3?wr*.75f:wr*.85f;
      const vec3 centres[]={vec3(-gst.track,wr-gh,gst.mainZ),vec3(gst.track,wr-gh,gst.mainZ),vec3(0,a.taildragger?-gh+.11f*a.fusLen+.1f:nr-gh,a.taildragger?gst.tailZ:gst.noseZ)};
      for(int wheel=0;wheel<3;wheel++){const auto c=centres[wheel];float bottom=fp.plane.pos.y+fp.plane.rot[1]*c.x+fp.plane.rot[4]*c.y+fp.plane.rot[7]*c.z-(wheel==2?nr:wr);if(fabsf(bottom)>.0001f){fprintf(stderr,"FAIL parked tyre contact aircraft%d wheel%d bottom%.6f\n",i,wheel,bottom);exit(3);}}
      if(fp.plane.propCount>0 && (fp.plane.Pr[1]!=0||fp.plane.Pr[0]!=0)){fprintf(stderr,"FAIL parked prop has motion blur/spin\n");exit(3);}

      for(vec3 b:{vec3(a.span*.5f,0,0),vec3(-a.span*.5f,0,0),vec3(0,0,a.fusLen*.5f),vec3(0,0,-a.fusLen*.5f),vec3(0,a.fusRad*3,0)}){
        const float* r=fp.plane.rot;
        vec3 p=fp.plane.pos+vec3(r[0]*b.x+r[3]*b.y+r[6]*b.z,r[1]*b.x+r[4]*b.y+r[7]*b.z,r[2]*b.x+r[5]*b.y+r[8]*b.z)-fp.camPos;
        float z=-dot(p,fp.camBack),x=W*.5f+dot(p,fp.camRight)/z/th*H*.5f,y=H*.5f-dot(p,fp.camUp)/z/th*H*.5f;
        if(z<=0||x<L.previewX||x>L.previewX+L.previewWidth||y<L.previewY||y>L.previewY+L.previewHeight){fprintf(stderr,"FAIL hangar %d dimension endpoint outside preview: %.1f %.1f\n",i,x,y);exit(3);}
      }
    }
    printf("PASS all %d catalog aircraft dimension endpoints fit hangar preview; tyres grounded within0.1mm and props stationary\n",hangarCatalogCount());
  }
  static void checkCatalog(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=0;g.in=Input{};g.career.money=10000000;g.career.license=LIC_ATP;
    const auto money=g.career.money;const auto fleet=g.career.fleet.size();
    auto id=[](int spec){uint32_t h=2166136261u;auto mix=[&](uint32_t v){h^=v;h*=16777619u;};mix(0);mix(spec);for(char c:std::string("hangar-catalog-airframe"))mix((uint8_t)c);return h;};
    auto frame=[&](){g.gamepadMenus(1.f/60.f);g.focusNavigate();g_ren.uiBegin();g.drawHub();g_ren.uiEnd();g.in=Input{};};
    frame();g.focusId=id(0);g.focusNav=true;
    for(int row=0;row<hangarCatalogCount();row++){
      if(row){g.in.pressed[K_DOWN]=true;frame();}
      const int spec=hangarSpecAt(row);require(g.focusId==id(spec),"keyboard traverses13-row catalog in designation order");
      g.in.pressed[K_ENTER]=true;frame();require(g.selHangar==spec,"Enter selects focused catalog row");
      if(hangarResearchLocked(spec)){
        const float ss=g.S();const auto L=hangarLayout(24*ss,126*ss,g_ren.W-48*ss,g_ren.H-146*ss,ss);
        for(const auto& f:g.focusList)require(!(f.x>=L.rightX&&f.y>=126*ss),"classified inspector exposes no economic controls");
      }
    }
    for(int row=hangarCatalogCount()-2;row>=0;row--){g.in.pad=true;g.in.buttonsPressed=PAD_UP;frame();const int spec=hangarSpecAt(row);require(g.focusId==id(spec),"D-pad traverses catalog in reverse order");g.in.pad=true;g.in.buttonsPressed=PAD_A;frame();require(g.selHangar==spec,"gamepad A selects focused catalog row");}
    for(int row=kNumAircraft;row<hangarCatalogCount();row++){int spec=hangarSpecAt(row);g.focusId=id(spec);g.focusNav=true;frame();auto it=std::find_if(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==id(spec);});require(it!=g.focusList.end()&&it->y>=0&&it->y+it->h<=g_ren.H,"classified row scrolls on screen");g.in.mx=it->x+it->w*.5f;g.in.my=it->y+it->h*.5f;g.in.mPressed[0]=true;frame();require(g.selHangar==spec,"mouse selects all4classified rows");}
    require(g.career.money==money&&g.career.fleet.size()==fleet,"catalog selection never mutates economy");
    puts("PASS complete catalog keyboard/Enter and D-pad/A traversal, all4classified mouse targets, no classified economic controls");
  }
  static void checkFreeFlightUI(Game& g) {
    auto require=[](bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(3);}};
    auto id=[](int value,const char* label){uint32_t h=2166136261u;auto mix=[&](uint32_t v){h^=v;h*=16777619u;};mix(0);mix(value);for(const char* c=label;*c;c++)mix((uint8_t)*c);return h;};
    const auto money=g.career.money;const auto fleet=g.career.fleet.size();const int location=g.career.location,story=g.career.storyIndex;
    g.beginFreeFlightSetup();g.freeCraft=0;g.freeAirport=0;g.in=Input{};
    auto frame=[&](){g.gamepadMenus(1.f/60);g.focusNavigate();g_ren.uiBegin();FrameParams fp{};g.drawFreeFlightSetup(fp);g_ren.uiEnd();g.in=Input{};};
    frame();g.focusId=id(0,"free-flight-airframe");g.focusNav=true;
    for(int i=0;i<kNumAircraft;i++){if(i){g.in.pressed[K_DOWN]=true;frame();}require(g.focusId==id(careerSpecAt(i),"free-flight-airframe"),"free-flight keyboard reaches every career aircraft");g.in.pressed[K_ENTER]=true;frame();require(g.freeCraft==careerSpecAt(i),"free-flight selects each aircraft without licence gate");g.selHangar=kSomnus;FrameParams preview{};g.hangarPreviewCamera(preview);require(preview.plane.model==careerSpecAt(i)&&preview.hangarPreview&&!preview.hangarClassified,"free-flight actual preview uses freeCraft, never career/classified selection");}
    g.focusId=id(0,"free-flight-airport");g.focusNav=true;frame();
    for(int i=0;i<(int)g_world.airports.size();i++){if(i){g.in.pressed[K_DOWN]=true;frame();}require(g.focusId==id(i,"free-flight-airport"),"free-flight keyboard reaches every airport");g.in.pressed[K_ENTER]=true;frame();require(g.freeAirport==i,"free-flight selects each airport");}
    const float ss=g.S();auto L=hangarLayout(24*ss,126*ss,g_ren.W-48*ss,g_ren.H-146*ss,ss);float px=L.rightX+18*ss,iw=L.rightWidth-36*ss,yy=g_ren.H-189*ss;
    g.in.mx=px+iw*.75f;g.in.my=yy+40*ss;g.in.mPressed[0]=true;frame();require(g.freeAirborne,"airborne setup hit target");
    g.in.mx=px+iw*.25f;g.in.my=yy+40*ss;g.in.mPressed[0]=true;frame();require(!g.freeAirborne,"runway setup hit target");
    g.in.pressed[K_ESC]=true;frame();require(g.screen==SCR_MENU,"free-flight escape returns main menu");
    require(g.career.money==money&&g.career.fleet.size()==fleet&&g.career.location==location&&g.career.storyIndex==story,"free-flight UI leaves career untouched");
    printf("PASS free-flight %d aircraft + %zu airport keyboard selection, runway/airborne and Escape, career unchanged\n",kNumAircraft,g_world.airports.size());
  }
  // Review fixtures retain production launch gates: later mission captures have an explicit coherent
  // prerequisite history. Ordinary military<ID> / _locked scenes always use fresh-career visibility.
  static void militaryPrerequisites(Game& g,int selected,bool completed=false) {
    if(selected==military::LegacyRecon)return;
    for(int id:military::kBoardMissionIds) {
      if(id==selected && !completed)break;
      g.career.military.completedMissions|=uint32_t(1)<<id;
      ++g.career.military.successes; ++g.career.military.sorties;
      if(id==selected)break;
    }
    g.career.military.rank=std::min(10,g.career.military.successes/3);
  }
  static void setup(Game& g,const std::string& sc,float scale) {
    g.realTime=30;g.uiDt=1.f;g.set.uiScale=scale;g.assetDir="assets";g.saveDir=(std::filesystem::temp_directory_path()/"somnium-ui-review-settings").string();std::filesystem::create_directories(g.saveDir);
    if(!g.iconTex){int w,h;std::vector<uint8_t> px;if(readImage(getenv("UI_ICON")?getenv("UI_ICON"):"assets/icon.png",w,h,px))g.iconTex=g_ren.makeTexture(px.data(),w,h);}
    if(sc.rfind("military_pause",0)==0 || sc.rfind("military_debrief",0)==0) {
      const bool pause=sc.rfind("military_pause",0)==0;
      const int id=atoi(sc.c_str()+(pause?14:16));militaryPrerequisites(g,id);
      g.launchMilitary(id); g.paused=pause;
      if(!pause) g.endFlight(false,"Military sortie aborted",OUT_ABANDONED);
      g.toasts.clear();g.in=Input{};
    } else if(sc.rfind("military_launch",0)==0) {
      const int id=atoi(sc.c_str()+15);militaryPrerequisites(g,id);
      g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;g.militarySelection=id;
      g.headless=false;g.diskless=true;g.in=Input{};
      const auto L=g.militaryLayout();const float s=std::min(g.S(),std::min(g_ren.H/720.f,g_ren.W/1120.f));
      g.in.mx=L.rx+L.rw*.5f;g.in.my=g_ren.H-55*s;g.in.mPressed[0]=g.in.mDown[0]=true;
    } else if(sc.rfind("military_loading",0)==0) {
      const int id=atoi(sc.c_str()+16);militaryPrerequisites(g,id);
      // _first captures the production draw-only card before any saved attempt or flight setup.
      // Other captures retain the deliberate headless fixture for an already initialized scene.
      if(sc.find("_first")!=std::string::npos) { g.headless=false;g.diskless=true; }
      g.launchMilitary(id);g.screen=SCR_LOADING;
      g.toasts.clear(); g.in=Input{};
      if(sc.find("live")!=std::string::npos) g.loadImg[fmt("air_%d",g.specIdx)]=0;
    } else if(sc.rfind("military",0)==0) {
      g.screen=SCR_HUB; g.hubTab=TAB_CONTRACTS; g.hubList=2; g.militarySelection=atoi(sc.c_str()+8);
      if(sc.find("_unlocked")!=std::string::npos || sc.find("_completed")!=std::string::npos)
        militaryPrerequisites(g,g.militarySelection,sc.find("_completed")!=std::string::npos);
      printf("MILITARY REVIEW: mission=%d state=%s completion_bits=%u. Fixture progression; live UI and aircraft renderer.\n",
        g.militarySelection,g.career.militaryMissionCompleted(g.militarySelection)?"completed/replayable":
        g.career.militaryMissionUnlocked(g.militarySelection)?"available":"classified",g.career.military.completedMissions);
    }
    if(sc.rfind("region-",0)==0) {
      g.debugScene(sc);
      if(const char* requested=getenv("HARNESS_REGION_VIEW")) {
        const std::string view=requested;const Airport& a=g_world.airports.front();
        const float angle=a.heading*DEG;const vec3 right(cosf(angle),0,sinf(angle)),forward=a.dir();
        const auto type=g_world.regionRecipe?g_world.regionRecipe->descriptor.archetype:procedural::Archetype::Archipelago;
        if(view=="lagoon"&&type==procedural::Archetype::Atolls) {
          // Airport0 sits on the eastern reef: look radially across its lagoon.
          g.dbgCamPos=a.pos()+right*1800.f+forward*700.f+vec3(0,650.f,0);
          g.dbgCamLook=a.pos()-right*6000.f;g.dbgCamLook.y=0;
        } else if(view=="relief"&&type==procedural::Archetype::Fjords) {
          // The next channel lies6km east of airport0; look inland along it.
          g.dbgCamPos=a.pos()+right*6000.f-forward*3000.f+vec3(0,700.f,0);
          g.dbgCamLook=a.pos()+right*6000.f+forward*15000.f;g.dbgCamLook.y=150.f;
        } else throw std::invalid_argument("HARNESS_REGION_VIEW must match lagoon/atolls or relief/fjords");
        g.dbgCamPos.y=std::max(g.dbgCamPos.y,g_world.height(g.dbgCamPos.x,g.dbgCamPos.z)+250.f);
        g.dbgCam=true;g.dbgFollow=false;g.set.fov=55;g.toasts.clear();
        printf("REGION CAMERA FIXTURE %s id=%s camera=(%.1f,%.1f,%.1f) look=(%.1f,%.1f,%.1f); actual landforms, weather/material/exposure unchanged\n",
          view.c_str(),g_world.regionId.c_str(),g.dbgCamPos.x,g.dbgCamPos.y,g.dbgCamPos.z,g.dbgCamLook.x,g.dbgCamLook.y,g.dbgCamLook.z);
      }
    }
    if(sc.rfind("freeflight",0)==0){g.beginFreeFlightSetup();g.freeCraft=atoi(sc.c_str()+10);g.freeAirborne=sc.find("airborne")!=std::string::npos;
      if(sc.find("earth")!=std::string::npos) g.freeRegionMap=true;
      if(sc.find("selected")!=std::string::npos && g.freeRegions.size()>1) g.freeRegion=1;
    }
    if(sc.rfind("research40",0)==0) {
      g.debugScene("research40");g.researchView.select(g.resCraft);
      if(sc.find("kinetic_emp")!=std::string::npos){g.practiceForward=hive::ForwardSet::Kinetic;g.practiceBomb=hive::BombSet::EMP;g.practiceWave=3;}
      if(sc.find("heavy_penetrator")!=std::string::npos){g.practiceForward=hive::ForwardSet::Heavy;g.practiceBomb=hive::BombSet::Penetrator;g.practiceWave=1;}
      if(sc.find("testcard")!=std::string::npos)for(int i=0;i<Game::kNumResCards;++i)if(Game::kResCards[i].craft==kSomnus){g.resCard=i;break;}
      if(sc.find("underside")!=std::string::npos){g.researchView.pitch=-.35f;g.researchView.yaw=2.15f;g.researchView.zoom=1.1f;}
      if(sc.find("focus")!=std::string::npos){g.focusScreen=SCR_RESEARCH*4;g.focusNav=true;g.focusId=0x7E5F0011u;}
      if(sc.find("face")!=std::string::npos)g.realTime=61;
      if(sc.find("helix")!=std::string::npos)g.realTime=43;
      const auto tick=sc.find("_t");if(tick!=std::string::npos)g.realTime=atof(sc.c_str()+tick+2);
      if(sc.find("reduced")!=std::string::npos)g.set.menuMotion=false;
    } else if(sc.rfind("research",0)==0||sc=="menuT5")g.debugScene(sc=="research30"?"research":sc);
    if(sc.rfind("hangar",0)==0){g.screen=SCR_HUB;g.hubTab=TAB_HANGAR;g.selHangar=atoi(sc.c_str()+6);}
    if((sc.rfind("hangar",0)==0 || sc.rfind("freeflight",0)==0) &&
       (sc.find("rotated")!=std::string::npos || sc.find("zoomed")!=std::string::npos)) {
      FrameParams selected{};g.hangarPreviewCamera(selected);
      if(sc.find("rotated")!=std::string::npos)g.hangarView.yaw=MenuPreviewView::defaultYaw+90*DEG;
      if(sc.find("zoomed")!=std::string::npos)g.hangarView.zoom=1.35f;
    }
    if(sc.find("airline_active")!=std::string::npos){g.career.money=1000000;g.career.license=LIC_ATP;g.career.storyIndex=(int)g_story.size();for(int i=0;i<5;i++){g.career.fleet.push_back({i,g.career.location,50,.9f});g.career.airline.pilots.push_back({"Review pilot "+std::to_string(i+1),2,200});}}
    if(sc=="logbook"){g.screen=SCR_HUB;g.hubTab=TAB_LOGBOOK;}
    if(sc.rfind("passport",0)==0){   // (passport_scroll: its foot) the passport with the islands' stamps and three regions beyond them
      g.screen=SCR_HUB;g.hubTab=TAB_LOGBOOK;g.logPage=2;
      g.profile.airports={g_world.airports[0].code,g_world.airports[2].code,"pr1-0-0-17:UMA","pr1-0-0-17:UMB","pr1-1-9-19:ASA"};
      g.profile.sites={"windfarm","pr1-0-0-17/crater0","pr1-0-0-17/sound1","pr1-1-9-19/lagoon0"};
      g.profile.seen={"ufo","region:pr1-4-17-7"};
    }
    if(sc=="menu_saved"||sc=="menu_overwrite"){g.hasSave=true;g.confirmNew=sc=="menu_overwrite";}
    if(sc=="contracts"){g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;}
    if(sc.rfind("airline",0)==0){g.screen=SCR_HUB;g.hubTab=TAB_AIRLINE;}
    if(sc=="settings"||sc=="controls"){g.screen=SCR_HUB;g.hubTab=TAB_SETTINGS;g.settingsPage=sc=="controls"?1:0;}
    if(sc.rfind("loading",0)==0){
      g.contract=g_story[0];g.plane.spec=&kAircraft[0];g.wx=g.contract.wx;
      g.loadT=5;g.loadShown=.57f;g.loadMap=true;g.screen=SCR_LOADING;
      g.loadFrames=2;g.loadPend0=80;g.loadStableFrames=0;g_ren.entPending=32;g.loadShown=.64f;
      sscanf(sc.c_str(),"loading_%f_%f",&g.loadShown,&g.loadT);
      if(sc.find("ready")!=std::string::npos){g.loadReadyT=g.loadT;g.loadShown=1;g.loadStableFrames=2;g_ren.entPending=0;}
      // No map shader is initialized in UI-only mode. Real packaged photographs remain available.
    }
  }
  // Context-only review camera: production metre-scale room, aircraft and
  // rendering are unchanged. Omit menu rails so the real workbench is visible.
  static void renderBenchContext(Game& g) {
    FrameParams fp=g.buildFrame();const float size=fp.hangarSize;
    fp.camPos=vec3(.15f,.45f,-1.35f)*size;
    const vec3 target=vec3(-.10f,.08f,.10f)*size, forward=normalize(target-fp.camPos);
    fp.camBack=-forward;fp.camRight=normalize(cross(forward,vec3(0,1,0)));fp.camUp=cross(fp.camRight,forward);
    fp.fovY=55*DEG;g_ren.renderScene(fp,{},{});
    g_ren.uiBegin();g_ren.rect(18,18,610,68,vec3(.015f,.025f,.035f),.88f,4);
    g_ren.text(32,30,23,kAircraft[fp.plane.model].name,vec3(.95f,.95f,.90f),1);
    g_ren.text(32,63,13,"SAME 0.90 m WORKBENCH / 2.40 m TOP / 3 m FLOOR GRID",svui::amber,1);
    float bx=0,by=0;const bool shown=g_ren.project(fp,vec3(-.98f*size,.9f,.74f*size),bx,by);
    if(!shown||bx<0||bx>g_ren.W||by<0||by>g_ren.H){fprintf(stderr,"FAIL: contextual workbench must be visible\n");exit(3);}
    g_ren.line(bx,by-3,bx,by-28,1.5f,svui::amber,.95f);
    g_ren.text(bx,by-44,12,"0.90 m WORKBENCH",svui::amber,1,1,true);
    g_ren.uiEnd();
    printf("METRIC CONTEXT: model=%d room=%.2f m bench top=(%.2f,0.90,%.2f) projected=(%.2f,%.2f)\n",fp.plane.model,size,-.98f*size,.74f*size,bx,by);
  }
  static void checkImmediateLaunch(const Game& g) {
    if(g.screen!=SCR_LOADING || !g.pendingMilitaryLaunch || !g.militaryLaunchFrameDrawn ||
        g.militaryLaunchFramePresented || g.career.attemptOpen || g.career.military.activeAttempt ||
        g.militaryFlight || g.loadFrames!=0 || g.loadReadyT>=0) {
      fprintf(stderr,"FAIL: Deploy must replace its own menu frame with the loading card before any save/mission setup\n");exit(3);
    }
    const auto& c=*g.pendingMilitaryLaunch;const auto& preview=g_ren.loadingPreview();
    if(!preview.matches(c.forceAircraft,c.type-CT_MIL_RECON) || preview.width>1280 || preview.height>720 ||
        !g.loadImg.empty() || preview.matches((c.forceAircraft+1)%kAircraftCount,c.type-CT_MIL_RECON)) {
      fprintf(stderr,"FAIL: first loading card must reuse the matching unclassified aircraft scene without image decoding\n");exit(3);
    }
    printf("PASS real Deploy replaced the same menu frame with matching aircraft snapshot %dx%d (model=%d mission=%d); setup still waits for presentation\n",
      preview.width,preview.height,preview.craft,preview.mission);
  }
  static void scrollSetup(Game& g,const std::string& sc,int frame) {
    g.focusList.clear();
    if(frame==1 && sc.find("scroll")!=std::string::npos){g.in.mx=g_ren.W*.85f;g.in.my=g_ren.H*.5f;g.in.wheel=-100;}
  }
  static void draw(Game& g,const std::string& sc) {
    if(sc.rfind("matrix",0)==0){
      float t=sc.find("wing")!=std::string::npos?25.f:sc.find("helix")!=std::string::npos?43.f:7.f;
      if(sc.rfind("matrix_t",0)==0)t=atof(sc.c_str()+8);
      g_ren.rect(0,0,float(g_ren.W),float(g_ren.H),svui::codeInk,1);
      svui::matrix(g_ren,0,0,float(g_ren.W),float(g_ren.H),t,.85f,sc.find("reduced")==std::string::npos);
    }
    else if(sc.rfind("military_pause",0)==0)g.drawPause();
    else if(sc.rfind("military_debrief",0)==0)g.drawDebrief();
    else if(sc.rfind("military_loading",0)==0)g.drawLoading();
    else if(sc.rfind("menu",0)==0)g.drawMenu();
    else if(sc=="pause")g.drawPause();
    else if(sc.rfind("freeflight",0)==0){FrameParams fp{};g.drawFreeFlightSetup(fp);}
    else if(sc.rfind("intro",0)==0){
      float p=.4f,t=5;sscanf(sc.c_str(),"intro_%f_%f",&p,&t);g.shaderFirstRun=true;
      LoadPacer pace;int ids[6];const char* labels[]={"GRAPHICS & WORLD","SHADER CACHE","PILOT SYSTEMS","RENDERER","MENU SCENERY","AIRCRAFT"};
      for(int i=0;i<6;++i)ids[i]=pace.add(labels[i],10,0,labels[i]);
      const int at=std::min(5,int(p*6));pace.begin(ids[at]);pace.setSub(p*6-at);pace.task("Surface field / GPU rows",32,128);
      if(p>=1)pace.end();
      g.drawIntro(p,p>=1?"All flight systems ready":"Preparing aircraft geometry: XR-40 Somnus / exterior | Surface field evaluation",t,g.iconTex,1.f,g_ren,&pace);
    }
    else if(sc.rfind("loading",0)==0)g.drawLoading();
    else if(sc.rfind("research",0)==0){FrameParams fp{};g.drawResearch(fp);}
    else g.drawHub();
  }
};
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<3){fprintf(stderr,"Usage: ui_review_harness scenes(comma separated) output-dir [W H ui-scale scene-mode]\nMilitary state pairs: military4_locked,military4_unlocked,military4_completed; fresh initial: military3\n");return 2;}
  const int W=argc>3?atoi(argv[3]):1920,H=argc>4?atoi(argv[4]):1080;
  const float scale=argc>5?atof(argv[5]):1;const bool full=argc>6&&std::string(argv[6])=="scene";
#ifdef UI_REVIEW_FLEET_ONLY
  {std::istringstream scenes(argv[1]);std::string sc;
    while(std::getline(scenes,sc,',')){bool fleetHangar=sc.rfind("hangar",0)==0&&atoi(sc.c_str()+6)>=0&&atoi(sc.c_str()+6)<kAircraftCount;bool freeSetup=sc.rfind("freeflight",0)==0&&atoi(sc.c_str()+10)>=0&&isCareerAircraft(atoi(sc.c_str()+10));bool research=sc=="research10"||sc=="research20"||sc.rfind("research30",0)==0||sc.rfind("research40",0)==0;bool military=sc.rfind("military",0)==0&&sc.find("loading")==std::string::npos&&sc.find("pause")==std::string::npos&&sc.find("debrief")==std::string::npos;bool allowed=fleetHangar||freeSetup||research||military||sc=="menuT5";if(!full||!allowed){fprintf(stderr,"Preview diagnostic supports hangar, free flight, Research, Military and fixed menuT5 scenes in scene mode.\n");return 2;}}}
  printf("DIAGNOSTIC: production startup and rendering programs; pre-baked actual selected meshes and availability assertions. UI_VERIFY_PREVIEWS also checks actual GPU aircraft pixels and classified outline privacy. Not native Windows startup validation.\n");
#endif
  std::filesystem::create_directories(argv[2]);
  if(!initGL(W,H)){puts("surfaceless EGL init failed");return 1;}
  printf("GL: %s; %dx%d; UI scale %.2f; %s\n",glGetString(GL_RENDERER),W,H,scale,full?"actual full scene":"LAYOUT PREVIEW: production UI, 3D backdrop omitted");
  const std::string cache=getenv("SHADERCACHE")?getenv("SHADERCACHE"):"";
  if(!cache.empty())std::filesystem::create_directories(cache);
  g_world.build(cache.empty()?"":cache+"/world.bin","ui-review-world-v1");buildStory();
  g_shaderCacheDir=cache;g_ren.matDir="assets/materials";g_ren.renderScale=1;g_ren.quality=1;g_ren.entSync=true;
  if(!(full?g_ren.init(W,H,[](float p,const std::string& stage){printf("INIT %.1f%% %s\n",p*100,stage.c_str());}):g_ren.initUI(W,H))){fprintf(stderr,"renderer init: %s\n",g_ren.error.c_str());return 1;}
  if(!g_shaderNotes.empty())printf("PRODUCTION SHADER NOTES: %s\n",g_shaderNotes.c_str());
  g_ren.bakeYield=[](){static auto last=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();if(std::chrono::duration<double>(now-last).count()>10){puts("Production mesh bake batch completed; continuing");last=now;}};
  std::istringstream input(argv[1]);std::string sc;
  while(std::getline(input,sc,',')){
    Game* g=new Game();g->initHeadless();GameTest::setup(*g,sc,scale);
    if(sc.rfind("horizon",0)==0){if(!full){fprintf(stderr,"Horizon review requires scene mode.\n");return 2;}GameTest::checkHorizon(*g,sc,argv[2]);delete g;continue;}
    if(sc.rfind("seamless-",0)==0){if(!full){fprintf(stderr,"Seamless review requires scene mode.\n");return 2;}GameTest::checkSeamless(*g,sc,argv[2]);delete g;continue;}
    if(sc=="interactions"){GameTest::checkInteractions(*g);GameTest::checkResearchLoadout(*g);GameTest::checkScrollFocus(*g);GameTest::checkLoading(*g);GameTest::checkHangarFraming(*g);GameTest::checkCatalog(*g);GameTest::checkFreeFlightUI(*g);delete g;continue;}
    for(int f=0;f<(full?(getenv("FRAMES")?atoi(getenv("FRAMES")):1):3);f++){
      GameTest::scrollSetup(*g,sc,f);
      if(full){if(sc.rfind("military_loading",0)==0) g->update(1.f/30.f); if(sc.find("_bench")!=std::string::npos)GameTest::renderBenchContext(*g);else g->render();if(sc.rfind("military_launch",0)==0)GameTest::checkImmediateLaunch(*g);}else{
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,W,H);glClearColor(.014f,.025f,.042f,1);glClear(GL_COLOR_BUFFER_BIT);
        g_ren.uiBegin();GameTest::draw(*g,sc);g_ren.uiEnd();
      }
    }
    glFinish();const std::string path=std::string(argv[2])+"/"+sc+".png";
    if(!g_ren.screenshotPNG(path.c_str()))return 1;
    printf("WROTE %s\n",path.c_str());delete g;
  }
  g_ren.releaseLoadingPreview();
  return 0;
}
