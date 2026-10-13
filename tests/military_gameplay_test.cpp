// Real Game-layer military/practice regression suite. No window, renderer initialization or GPU bakes.
// Positions and input are scripted, but mission transitions, damage, launch/settlement and resets run production code.
#include "../src/game.h"
#include "test_world.h"
#include "../src/hive_world_collision.h"
#include "../src/military_missions.h"
#include "../src/hangar_preview.h"
#include "../src/menu_layout.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iterator>

struct GameTest {
  static int run() {
    namespace fs = std::filesystem;
    buildTestWorld(); buildStory(); g_audio.init(48000);
    const fs::path root=fs::temp_directory_path() / ("somnium_military_game_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    int failures=0, checks=0;
    auto check=[&](bool ok,const std::string& label) { ++checks; failures+=!ok; printf("Military game: %s: %s\n",label.c_str(),ok?"PASS":"FAIL"); };
    auto near=[](float a,float b) { return std::fabs(a-b)<.001f; };
    auto bytes=[](const fs::path& p) { std::ifstream f(p,std::ios::binary); return std::string(std::istreambuf_iterator<char>(f),{}); };
    int serial=0;
    auto snapshot=[&](Career c,bool civilianOnly=false) {
      if(civilianOnly) { c.military=Career::MilitaryRecord(); c.attempt=0; c.attemptOpen=false; }
      fs::path p=root/("snapshot_"+std::to_string(serial++)+".sav");
      bool ok=c.save(p.string()); check(ok,"snapshot writes"); return ok?bytes(p):std::string("FAILED SNAPSHOT");
    };
    // Existing isolated mission/mechanics fixtures explicitly earn only the prerequisite board entries.
    // Fresh-career progression tests below keep the default empty military record.
    auto make=[&](int unlockedMission=-1) {
      auto g=std::make_unique<Game>(); g->initHeadless(); g->diskless=true; g->botControl=true; g->set.traffic=false;
      g->career.money=13; g->career.license=LIC_STUDENT; g->career.reputation=7; g->career.flights=12;
      g->career.landings=8; g->career.crashes=2; g->career.hours=4.25f; g->career.bestLandingFpm=77;
      g->career.fleet={{1,1,13,.31f}}; g->career.loan={1,21000,1200,2,.12f}; g->career.insured=true;
      g->career.airline.pilots.push_back({"Military isolation pilot",2,120});
      g->career.airline.routes.push_back({0,0,1,0,3,999}); g->career.airline.earned=999;
      Contract c=g_story[4]; c.id="MILITARY_ISOLATION_JOB"; c.story=false; c.wps={{0,0,500},{1000,0,500}};
      Career::LaunchPlan p; p.spec=1; p.src=Career::SRC_OWNED; p.startAirport=c.from; p.fuelLoadKg=13;
      g->career.accept(c,1,Career::SRC_OWNED,p); g->career.job->state=Career::JobState::RECOVERY;
      g->career.job->wpDone=1; g->career.job->jobClockMin=17.5f; g->career.job->hirePaid=true;
      g->career.attempt=47; g->career.attemptOpen=false;
      if(unlockedMission!=military::LegacyRecon && military::mission(unlockedMission)) {
        for(int id:military::kBoardMissionIds) {
          if(id==unlockedMission)break;
          g->career.military.completedMissions|=uint32_t(1)<<id;
          ++g->career.military.successes; ++g->career.military.sorties;
        }
        g->career.military.rank=std::min(10,g->career.military.successes/3);
      }
      return g;
    };
    auto killAll=[&](Game& g) {
      // Real swept player shots: destruction records stable mission target IDs, rather than setting success flags.
      for(int pass=0;pass<2;++pass) for(auto& a:g.hiveCombat.actors) if(a.alive) {
        const vec3 p=a.position;
        g.hiveCombat.playerShot(p+vec3(0,50,0),p-vec3(0,50,0),100000,g.hiveWorld());
      }
    };
    const float dt=1.f/60.f;

    // Retained scene identity is independent of the current UI selection and never admits classified art.
    // The native Deploy diagnostic separately proves the matching texture contains the real rendered scene.
    {
      Renderer::LoadingPreview preview;
      preview.texture=17;preview.width=1280;preview.height=720;preview.craft=kNightjar;
      preview.mission=military::LocalSurvey;preview.ready=true;
      check(preview.matches(kNightjar,military::LocalSurvey) &&
        !preview.matches(kMantis,military::LocalSurvey) && !preview.matches(kNightjar,military::RouteRecon),
        "first loading artwork requires both the assigned aircraft and the exact rendered mission identity");
      preview.classified=true;
      check(!preview.matches(kNightjar,military::LocalSurvey),"classified aircraft snapshots cannot appear as deployed loading artwork");
      preview.classified=false;preview.ready=false;
      check(!preview.matches(kNightjar,military::LocalSurvey),"unfinished GPU snapshots cannot be used as loading artwork");
      preview.ready=true;preview.texture=0;
      check(!preview.matches(kNightjar,military::LocalSurvey),"missing GPU snapshot never masquerades as an available aircraft image");
      FrameParams fp{};fp.plane.on=true;fp.plane.model=kNightjar;
      check(!g_ren.retainLoadingPreview(fp,military::LocalSurvey) && !g_ren.loadingPreview().ready,
        "snapshot capture requires an initialized renderer and cannot trigger GL work from CPU-only loading tests");
    }

    // Interactive military launch: exercise the production card-only render branch with CPU UI geometry.
    // A renderer submission and an actual platform presentation are distinct events. No GL scene is needed
    // to prove that saves, mission initialization, collision warmup and readiness all wait for presentation.
    {
      auto q=make();Game& g=*q;g.headless=false;g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
      const std::string before=snapshot(g.career),civil=snapshot(g.career,true);
      const uint32_t attempt=g.career.attempt;
      const auto* oldSpec=g.plane.spec;const vec3 oldPos=g.plane.pos;
      const int oldBakes=g_ren.bakeCount,oldChunks=g_ren.entChunks;
      const auto oldSceneSerial=g_ren.gpuFrameSerial;
      const auto oldPreview=g_ren.loadingPreview();
      g.in.pressed[K_ENTER]=g.in.pressed[' ']=g.in.mPressed[0]=true;g.in.buttonsPressed=PAD_A|PAD_START;
      g.launchMilitary(military::LocalSurvey);
      check(g.screen==SCR_LOADING && g.pendingMilitaryLaunch && !g.militaryFlight &&
        !g.career.attemptOpen && g.plane.spec==oldSpec && length(g.plane.pos-oldPos)==0 &&
        !g.combatCollisionWarm && g.loadFrames==0 && g.loadReadyT<0 && snapshot(g.career)==before,
        "interactive Deploy changes to loading immediately without saving, resetting the aircraft or preparing the mission");
      check(!g.in.pressed[K_ENTER] && !g.in.pressed[' '] && !g.in.mPressed[0] && !(g.in.buttonsPressed&(PAD_A|PAD_START)),
        "queued Deploy consumes mouse, keyboard and controller activation edges");
      const std::string queued=g.pendingMilitaryLaunch->id;
      g.launchMilitary(military::LocalSurvey);g.launchMilitary(military::RouteRecon);
      g.framePresented();g.updateLoading(10.f);
      check(g.pendingMilitaryLaunch && g.pendingMilitaryLaunch->id==queued && !g.militaryLaunchFramePresented &&
        g.career.attempt==attempt && snapshot(g.career)==before,
        "repeated Deploy, elapsed time and a presentation without a loading draw cannot start or replace the queued sortie");
      g.render();   // Same early-return path as the native renderer, with no initialized GPU.
      const bool rendered=g.militaryLaunchFrameDrawn;
      g.updateLoading(dt);
      check(rendered && g.pendingMilitaryLaunch && !g.militaryLaunchFramePresented &&
        g.loadImg.empty() && !g.loadMap && g.loadFrames==0 && g.loadReadyT<0 &&
        g_ren.bakeCount==oldBakes && g_ren.entChunks==oldChunks && g_ren.gpuFrameSerial==oldSceneSerial &&
        g_ren.loadingPreview().texture==oldPreview.texture && g_ren.loadingPreview().ready==oldPreview.ready &&
        !g.combatCollisionWarm && snapshot(g.career)==before,
        "submitted loading card skips image decode, scene rendering, collision setup and saves until it is presented");
      g.framePresented();g.updateLoading(dt);
      check(!g.pendingMilitaryLaunch && g.screen==SCR_LOADING && g.militaryFlight &&
        g.specIdx==kNightjar && g.career.attempt==attempt+1 && g.career.military.activeAttempt==g.career.attempt &&
        g.hiveCombat.mission.config.objectiveCount==2 && !g.combatCollisionWarm && g.loadFrames==0 &&
        g.loadReadyT<0 && g.loadShown==0 && snapshot(g.career,true)==civil,
        "only the update after presentation saves one loaner attempt and initializes the real mission while keeping readiness locked");
      const uint32_t token=g.career.military.activeAttempt;
      g.launchMilitary(military::LocalSurvey);g.framePresented();
      check(!g.pendingMilitaryLaunch && g.career.military.activeAttempt==token && g.career.attempt==attempt+1,
        "duplicate activation after preparation cannot queue or commit another attempt");
      g.in.pressed[K_ESC]=true;g.updateLoading(dt);
      check(g.screen==SCR_HUB && !g.career.attemptOpen && !g.combatCollisionWarm && snapshot(g.career,true)==civil,
        "Back after prepared loading closes the token before expensive collision warmup and preserves the civilian career");
      printf("Military launch ordering trace: request -> loading card submitted -> presented -> saved attempt -> scene preparation\n");
    }
    // Cancellation before and after presentation must not save an unflown attempt or resurrect a stale card.
    for(bool pad:{false,true}) for(bool presented:{false,true}) {
      auto q=make();Game& g=*q;g.headless=false;g.screen=SCR_HUB;g.hubList=2;
      const std::string before=snapshot(g.career);
      g.launchMilitary(military::LocalSurvey);g.render();if(presented)g.framePresented();
      if(pad)g.in.buttonsPressed=PAD_B;else g.in.pressed[K_ESC]=true;
      g.updateLoading(dt);g.framePresented();
      check(g.screen==SCR_HUB && !g.pendingMilitaryLaunch && !g.militaryLaunchFrameDrawn &&
        !g.militaryLaunchFramePresented && !g.militaryFlight && !g.combatCollisionWarm && snapshot(g.career)==before,
        std::string(pad?"controller":"keyboard")+" Back cancels queued launch "+(presented?"after":"before")+" presentation without saving");
      g.launchMilitary(military::LocalSurvey);g.framePresented();g.updateLoading(dt);
      check(g.pendingMilitaryLaunch && !g.militaryLaunchFramePresented && snapshot(g.career)==before,
        "relaunch after cancellation requires its own newly rendered and presented loading card");
      g.in.pressed[K_ESC]=true;g.updateLoading(dt);
    }
    // Failed save is discovered behind the presented card; it returns to the board with no pending launch,
    // encounter or token. A subsequent retry must present a fresh loading card before it can do any work.
    {
      auto q=make();Game& g=*q;g.headless=false;g.screen=SCR_HUB;g.hubList=2;
      const std::string before=snapshot(g.career);const uint32_t attempt=g.career.attempt;
      const fs::path blocked=root/"blocked_deferred_launch";{std::ofstream f(blocked);f<<"block";}
      g.diskless=false;g.saveDir=blocked.string();g.launchMilitary(military::LocalSurvey);g.render();
      check(g.pendingMilitaryLaunch && g.hubMsg.empty() && snapshot(g.career)==before,
        "even failing storage is not touched before the loading card has been presented");
      g.framePresented();g.updateLoading(dt);
      check(g.screen==SCR_HUB && !g.pendingMilitaryLaunch && !g.pendingCareer && !g.militaryFlight &&
        !g.combatCollisionWarm && !g.hubMsg.empty() && snapshot(g.career)==before,
        "deferred launch save failure returns to military board without committing or retaining an attempt");
      g.diskless=true;g.launchMilitary(military::LocalSurvey);g.updateLoading(dt);
      check(g.pendingMilitaryLaunch && g.career.attempt==attempt,"retry after storage failure waits for a fresh presentation");
      g.render();g.framePresented();g.updateLoading(dt);
      check(g.screen==SCR_LOADING && g.militaryFlight && !g.pendingMilitaryLaunch && g.career.attempt==attempt+1,
        "retry after storage failure starts exactly one mission after its own presentation");
      g.in.pressed[K_ESC]=true;g.updateLoading(dt);
    }

    // Genuine fresh service record: every entry point must enforce the displayed sequence.
    // These cases deliberately use no prerequisite fixture, including the direct/private launch paths.
    {
      auto q=make();Game& g=*q;g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
      const std::string before=snapshot(g.career);
      check(g.career.militaryMissionUnlocked(military::LocalSurvey) &&
        !g.career.militaryMissionCompleted(military::LocalSurvey) && g.career.military.completedMissions==0,
        "fresh service record exposes its first board assignment without marking it complete");
      for(int row=1;row<military::kBoardMissionCount;++row) {
        const int id=military::kBoardMissionIds[row];const auto c=g.career.militaryContract(id);
        const auto p=g.career.plan(c,c.forceAircraft,Career::SRC_MILITARY);
        g.launchMilitary(id);
        check(!g.career.militaryMissionUnlocked(id) && g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==before,
          "fresh career rejects locked direct launch in board order "+std::to_string(row+1));
        g.startFlight(c,c.forceAircraft,Career::SRC_MILITARY,&p);
        check(g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==before,
          "direct start cannot invent an accepted token for locked mission "+std::to_string(id));
        g.contract=c;g.screen=SCR_DEBRIEF;g.retryFromDebrief();
        check(g.screen!=SCR_FLIGHT && g.screen!=SCR_LOADING && !g.militaryFlight && snapshot(g.career)==before,
          "stale debrief retry cannot bypass locked mission "+std::to_string(id));
        g.screen=SCR_HUB;
      }
      // Even the first available mission needs a legitimate committed attempt before startFlight.
      const auto first=g.career.militaryContract(military::LocalSurvey);
      g.startFlight(first,first.forceAircraft,Career::SRC_MILITARY);
      check(g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==before,
        "available mission cannot direct-start without a committed service token");
      g.contract=g.career.militaryContract(military::RouteRecon);g.specIdx=g.contract.forceAircraft;
      g.source=Career::SRC_MILITARY;g.militaryFlight=true;g.screen=SCR_LOADING;
      g.loadReadyT=0;g.loadT=3;g.loadShown=1;g.in.pressed[K_ENTER]=true;g.in.buttonsPressed=PAD_A;g.in.mPressed[0]=true;
      g.updateLoading(dt);
      check(g.screen==SCR_HUB && g.hubList==2 && !g.militaryFlight && g.source==Career::SRC_NONE &&
        !g.in.pressed[K_ENTER] && !g.in.mPressed[0] && !(g.in.buttonsPressed&PAD_A) && snapshot(g.career)==before,
        "forged ready loading state cannot bypass locked mission and consumes every launch input");
    }
    // Locked rows remain inspectable, but neither stale focus nor any input device may deploy them.
    {
      const int oldW=g_ren.W,oldH=g_ren.H;
      for(const auto size:std::array<std::pair<int,int>,3>{{{1920,1080},{1280,720},{960,540}}}) {
        g_ren.W=size.first;g_ren.H=size.second;
        auto q=make();Game& g=*q;g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
        g.militarySelection=military::RouteRecon;g.in.mx=g.in.my=-100;
        const std::string before=snapshot(g.career);
        auto frame=[&]() {g.gamepadMenus(dt);g.focusNavigate();g_ren.uiBegin();g.drawHub();g.in.endFrame();};
        auto deployVisible=[&]() {return std::any_of(g.focusList.begin(),g.focusList.end(),[](const Game::Focusable& f){return f.id==0x6D0002u;});};
        frame();
        check(!deployVisible(),"classified assignment has no enabled Deploy focus target at "+std::to_string(size.first));
        for(bool pad:{false,true}) {
          g.militarySelection=military::RouteRecon;g.focusNav=false;frame();
          g.focusNav=true;g.focusId=0x6D0002u;g.in.pad=pad;
          if(pad)g.in.buttonsPressed=PAD_A;else g.in.pressed[K_ENTER]=true;
          frame();
          check(g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==before,
            std::string(pad?"controller A":"keyboard Enter")+" cannot deploy a classified assignment through stale focus");
        }
        g.militarySelection=military::RouteRecon;g.focusNav=false;frame();
        const auto L=g.militaryLayout();const float s=std::min(g.S(),std::min(g_ren.H/720.f,g_ren.W/1120.f));
        g.in.pad=false;g.focusNav=false;g.in.mx=L.rx+L.rw*.5f;g.in.my=g_ren.H-55*s;g.in.mPressed[0]=true;frame();
        check(g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==before,
          "mouse clicking classified Deploy area cannot accept or launch a mission");
        g.in.mx=g.in.my=-100;
        FrameParams locked{};g.militaryPreviewCamera(locked);
        check(locked.plane.model==kMantis && locked.hangarPreview && locked.hangarClassified && !locked.prefetchOn &&
          locked.plane.lensN==0 && locked.landLight==0 && locked.feedRig==0 && locked.dispMode==0 && locked.plN==0,
          "classified preview uses assigned shape with silhouette pass and no lights, feeds or military world streaming");
        // Actual cached showroom triangles must not occlude the aircraft when the terminal's longer
        // camera orbit is reused for a silhouette. Exercise every locked airframe, zoom and pitch limit.
        auto occluded=[](vec3 from,vec3 to,const std::vector<hangarPreview::Vertex>& room) {
          const vec3 d=to-from;
          for(size_t i=0;i+2<room.size();i+=3) {
            const vec3 e1=room[i+1].p-room[i].p,e2=room[i+2].p-room[i].p,h=cross(d,e2);
            const float det=dot(e1,h);if(std::fabs(det)<1e-6f)continue;
            const float inv=1.f/det;const vec3 s=from-room[i].p;const float u=inv*dot(s,h);
            if(u<0 || u>1)continue;
            const vec3 q=cross(s,e1);const float v=inv*dot(d,q);if(v<0 || u+v>1)continue;
            const float t=inv*dot(e2,q);if(t>0.0001f && t<.9999f)return true;
          }
          return false;
        };
        bool clearOrbit=true;
        for(int id:{military::RouteRecon,military::HighAltitudeIntel,military::HoverScan})
          for(float zoom:{.75f,1.35f})for(float pitch:{-.5f,.45f,1.f})for(int angle=0;angle<8;++angle) {
            g.militarySelection=id;g.militaryView.select(military::mission(id)->aircraft,id);g.militaryView.zoom=zoom;g.militaryView.pitch=pitch;g.militaryView.yaw=angle*PI*.25f;
            FrameParams fp{};g.militaryPreviewCamera(fp);const auto room=hangarPreview::geometry(fp.hangarSize);
            const auto& sp=kAircraft[military::mission(id)->aircraft];const float* r=fp.plane.rot;
            for(vec3 local:{vec3(),vec3(sp.span*.5f,0,0),vec3(-sp.span*.5f,0,0),vec3(0,0,sp.fusLen*.5f),vec3(0,0,-sp.fusLen*.5f)}) {
              const vec3 point=fp.plane.pos+vec3(r[0]*local.x+r[3]*local.y+r[6]*local.z,
                r[1]*local.x+r[4]*local.y+r[7]*local.z,r[2]*local.x+r[5]*local.y+r[8]*local.z);
              clearOrbit&=!occluded(fp.camPos-fp.hangarOrigin,point-fp.hangarOrigin,room);
            }
          }
        check(clearOrbit,"classified room geometry never hides aircraft center or extremities across full orbit/zoom/pitch at "+std::to_string(size.first));
        g.militarySelection=military::LocalSurvey;const FrameParams available=g.buildFrame();
        g.militarySelection=military::RouteRecon;const FrameParams classified=g.buildFrame();
        g.militarySelection=military::LocalSurvey;const FrameParams availableAgain=g.buildFrame();
        check(!available.hangarClassified && !available.hangarPreview && available.plane.model==kNightjar &&
          classified.hangarClassified && classified.hangarPreview && classified.plane.model==kMantis &&
          !availableAgain.hangarClassified && !availableAgain.hangarPreview && availableAgain.plane.model==kNightjar,
          "production frame rebuild resets material flags when switching available to locked and back");
      }
      g_ren.W=oldW;g_ren.H=oldH;
    }
    // Only real objective completion plus extraction reveals the next assignment. Abort, failure and canceled
    // loading attempts are genuine production paths and cannot buy access through cumulative sortie counts.
    {
      auto q=make();Game& g=*q;
      for(bool prematureSuccess:{false,true}) {
        g.launchMilitary(military::LocalSurvey);
        g.endFlight(prematureSuccess,prematureSuccess?"Unmet mission":"User abort",prematureSuccess?OUT_SUCCESS:OUT_ABANDONED);
        check(!g.lastSuccess && !g.career.militaryMissionCompleted(military::LocalSurvey) &&
          !g.career.militaryMissionUnlocked(military::RouteRecon) && g.career.military.completedMissions==0,
          prematureSuccess?"failed first mission does not unlock the second":"aborted first mission does not unlock the second");
      }
      g.launchMilitary(military::LocalSurvey);g.screen=SCR_LOADING;g.in.pressed[K_ESC]=true;g.updateLoading(dt);g.in.endFrame();
      check(g.screen==SCR_HUB && !g.career.attemptOpen && g.career.military.completedMissions==0 &&
        !g.career.militaryMissionUnlocked(military::RouteRecon),"canceling first mission loading grants no completion or unlock");
      g.militarySelection=military::RouteRecon;FrameParams beforeUnlock{};g.militaryPreviewCamera(beforeUnlock);
      g.launchMilitary(military::LocalSurvey);auto& m=g.hiveCombat.mission;
      for(int i=0;i<m.config.objectiveCount;++i) {
        const vec3 site=m.config.objectives[i].position;
        g.plane.pos=site+vec3(0,350,450);g.plane.vel=vec3();g.plane.q=quat::axisAngle(vec3(1,0,0),-atan2f(350,450));
        for(int n=0;n<240 && !m.config.objectives[i].scanned;++n)g.updateHive(dt);
      }
      check(m.status==hive::MissionStatus::Extract && !g.career.militaryMissionCompleted(military::LocalSurvey) &&
        !g.career.militaryMissionUnlocked(military::RouteRecon),"successful scans alone do not unlock before extraction");
      const fs::path blocked=root/"blocked_progress_save";{std::ofstream f(blocked);f<<"block";}
      g.diskless=false;g.saveDir=blocked.string();
      g.plane.pos=m.config.extraction;g.updateHive(dt);
      check(g.screen==SCR_DEBRIEF && g.lastSuccess && g.pendingCareer &&
        g.pendingCareer->militaryMissionCompleted(military::LocalSurvey) &&
        !g.career.militaryMissionCompleted(military::LocalSurvey) && !g.career.militaryMissionUnlocked(military::RouteRecon),
        "failed successful-settlement save cannot reveal a mission before the completion is committed");
      const fs::path saved=root/"saved_progress";fs::create_directories(saved);g.saveDir=saved.string();
      check(g.retryCommit() && !g.pendingCareer,"retrying successful-settlement save commits the queued completion exactly once");
      g.diskless=true;
      check(g.screen==SCR_DEBRIEF && g.lastSuccess && g.career.militaryMissionCompleted(military::LocalSurvey) &&
        g.career.militaryMissionUnlocked(military::RouteRecon) && !g.career.militaryMissionUnlocked(military::HighAltitudeIntel) &&
        g.career.military.completedMissions==(uint32_t(1)<<military::LocalSurvey),
        "first genuine success unlocks exactly the next displayed mission and no later mission");
      FrameParams afterUnlock{};g.militaryPreviewCamera(afterUnlock);
      check(beforeUnlock.plane.model==afterUnlock.plane.model && afterUnlock.plane.model==kMantis &&
        beforeUnlock.hangarClassified && !afterUnlock.hangarClassified && !afterUnlock.hangarPreview && afterUnlock.prefetchOn,
        "same assigned aircraft changes from classified silhouette to actual model after predecessor success");
      const uint32_t completion=g.career.military.completedMissions;
      g.retryFromDebrief();
      check(g.screen==SCR_FLIGHT && g.militaryFlight && g.career.military.activeKind==military::LocalSurvey &&
        g.career.military.completedMissions==completion,"completed first mission remains replayable through debrief retry");
      g.endFlight(false,"Replay abort",OUT_ABANDONED);
      g.launchMilitary(military::RouteRecon);
      check(g.screen==SCR_FLIGHT && g.militaryFlight && g.specIdx==kMantis && g.career.military.activeKind==military::RouteRecon,
        "genuinely unlocked second assignment launches its actual loaner");
      g.endFlight(false,"Unlocked mission abort",OUT_ABANDONED);
      check(!g.career.militaryMissionUnlocked(military::HighAltitudeIntel) && g.career.military.completedMissions==completion,
        "aborting newly unlocked mission leaves following assignment classified");
    }

    // Every loaner launches for an unlicensed, penniless pilot without adding it to the owned/research roster.
    for(int kind=0;kind<3;++kind) {
      auto q=make(kind); Game& g=*q;
      const std::string before=snapshot(g.career,true); const uint32_t attempt=g.career.attempt;
      g.launchMilitary(kind);
      const auto warmActors=g.hiveCombat.actors;
      const float warmTime=g.hiveCombat.simulatedSeconds(),warmMission=g.hiveCombat.mission.elapsed;
      const int warmEvents=g.hiveCombat.eventCount,warmRounds=g.combatLoadout.rounds;
      const vec3 warmPosition=g.plane.pos;
      g.screen=SCR_LOADING; g.updateLoading(0); g.screen=SCR_FLIGHT;
      bool warmUnchanged=true;
      for(int i=0;i<hive::MaxActors;++i) warmUnchanged&=g.hiveCombat.actors[i].id==warmActors[i].id &&
        length(g.hiveCombat.actors[i].position-warmActors[i].position)==0 && g.hiveCombat.actors[i].timer==warmActors[i].timer;
      check(g.combatCollisionWarm && warmUnchanged && g.hiveCombat.simulatedSeconds()==warmTime &&
        g.hiveCombat.mission.elapsed==warmMission && g.hiveCombat.eventCount==warmEvents &&
        g.combatLoadout.rounds==warmRounds && length(g.plane.pos-warmPosition)==0,
        "loading collision warmup preserves all simulation clocks, actor state and ammunition");
      printf("Military collision setup: kind=%d warmup_ms=%.3f (measured separately from simulation)\n",kind,g.combatCollisionWarmMs);
      const double warmMs=g.combatCollisionWarmMs;
      g.warmCombatCollision();g.prefetchCombatCollision();
      check(g.combatCollisionWarmMs==warmMs && g.hiveCombat.simulatedSeconds()==warmTime && g.hiveCombat.mission.elapsed==warmMission,
        "warmup is idempotent and bounded prefetch does not advance simulation");
      check(g.screen==SCR_FLIGHT && g.militaryFlight && !g.researchFlight && !g.isolatedFlight && !g.freeFlight,"loaner launches in dedicated career-backed mode "+std::to_string(kind));
      check(g.specIdx==(kind==0?0:kWraith) && g.source==Career::SRC_MILITARY && g.plane.spec==&kAircraft[g.specIdx],"correct recon/combat loaner "+std::to_string(kind));
      check(g.career.attempt==attempt+1 && g.career.attemptOpen && g.career.military.activeAttempt==g.career.attempt,"launch commits active token "+std::to_string(kind));
      check(kind==0?near(g.plane.payload,85):near(g.plane.payload,85+g.combatLoadout.massKg()),"loaner payload includes kit/stores only for combat XR40");
      check(g.launchPlan.fees()==0 && g.launchPlan.fuelCostEst==0 && near(g.plane.fuel,g.plane.spec->maxFuel) && g.failPlan.kind==0,"supplied aircraft has full included fuel and no maintenance roll");
      check(snapshot(g.career,true)==before,"launch preserves complete civilian career");
      const uint32_t token=g.career.military.activeAttempt;
      const auto ids=g.hiveCombat.actors; g.plane.fuel=1; g.militaryHull=25; g.combatLoadout.rounds=0;
      g.restartFlight();
      bool same=true; for(int i=0;i<hive::MaxActors;++i) same&=g.hiveCombat.actors[i].id==ids[i].id && g.hiveCombat.actors[i].type==ids[i].type;
      check(g.career.military.activeAttempt==token && g.career.attempt==attempt+1 && g.militaryHull==100 && g.combatLoadout.rounds>0 && near(g.plane.fuel,g.plane.spec->maxFuel) && same,"restart retains accepted token/seed and resets encounter/loadout");
      g.endFlight(true,"Premature success request",OUT_SUCCESS);
      check(g.screen==SCR_DEBRIEF && !g.lastSuccess && g.career.military.credits==0 && g.career.military.failures==1,"unmet objectives cannot settle as success");
      check(snapshot(g.career,true)==before,"failed military settlement leaves civilian state intact");
      g.retryFromDebrief();
      check(g.screen==SCR_FLIGHT && g.militaryFlight && g.career.attempt==attempt+2 && g.career.military.activeAttempt!=token,"debrief retry creates a fresh military attempt despite waiting civilian job");
      g.endFlight(false,"Abort",OUT_ABANDONED);
    }

    // Recon: production sensor dwell must complete all sites, followed by actual extraction proximity.
    {
      auto q=make(); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(0); killAll(g);
      auto& m=g.hiveCombat.mission;
      check(m.config.kind==hive::MissionKind::Recon && m.config.objectiveCount==3,"recon config has three scan sites");
      check(g.hiveCombat.aliveCount()==0,"unarmed introductory recon has no unavoidable pursuing enemy");
      for(int i=0;i<m.config.objectiveCount;++i) {
        vec3 site=m.config.objectives[i].position;
        bool actual=false; std::vector<AptItem> items;
        for(int a=0;a<(int)g_world.airports.size();++a) { airportItems(a,items); for(const auto& item:items) {
          vec3 roof(item.e.x,item.e.y+kEntInfo[item.kind].h*item.e.sy+2.f,item.e.z);
          actual|=length(site-roof)<.01f;
        } }
        check(actual && !g.militarySiteLabels[i].empty(),"recon target is an actual labeled airport structure");
        g.plane.pos=site+vec3(0,350,450); g.plane.vel=vec3(); g.plane.q=quat::axisAngle(vec3(1,0,0),-atan2f(350,450));
        for(int n=0;n<320 && !m.config.objectives[i].scanned;++n) g.updateHive(dt);
        check(m.config.objectives[i].scanned,"recon site accumulates real line-of-sight dwell "+std::to_string(i));
      }
      check(g.screen==SCR_FLIGHT && m.status==hive::MissionStatus::Extract && g.career.military.credits==0,"all scans require extraction before reward");
      g.plane.pos=m.config.extraction; g.updateHive(dt);
      check(g.screen==SCR_DEBRIEF && g.lastSuccess && g.career.military.successes==1 && g.career.military.intelligence==1 && g.career.military.credits==600,"recon extraction settles intelligence and service credits");
      check(g.career.militaryMissionCompleted(military::LegacyRecon) && !g.career.militaryMissionUnlocked(military::RouteRecon),
        "legacy recon success stays replayable without skipping the displayed board prerequisites");
      const std::string settled=snapshot(g.career); g.endFlight(true,"Duplicate",OUT_SUCCESS);
      check(snapshot(g.career)==settled && snapshot(g.career,true)==before,"repeated completion cannot double-pay or mutate civilian state");
    }

    // Defense: actual scheduled wave spawning/deaths, protected asset survival, extraction and failure.
    {
      auto q=make(military::IslandDefense); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(1);
      auto& m=g.hiveCombat.mission; g.plane.pos=vec3(0,18000,0); g.plane.vel=vec3();
      // Compress only schedule intervals for a fast test, preserving all authored role mixes and wave counts.
      for(int i=0;i<m.config.waveCount;++i) m.config.waves[i].at=i*.05f;
      for(int n=0;n<40 && m.status==hive::MissionStatus::Active;++n) { g.updateHive(dt); killAll(g); }
      g.updateHive(dt);
      check(m.nextWave==m.config.waveCount && m.config.waveCount==3 && m.status==hive::MissionStatus::Extract && m.config.objectives[0].health>0,"defense resolves every authored wave and preserves installation");
      check(g.career.military.credits==0,"defense does not pay before extraction");
      g.plane.pos=m.config.extraction; g.updateHive(dt);
      check(g.lastSuccess && g.career.military.credits==1200 && snapshot(g.career,true)==before,"defense extraction pays service record only");
      g.launchMilitary(1); g.hiveCombat.mission.config.objectives[0].health=0; g.updateHive(dt);
      check(g.screen==SCR_DEBRIEF && !g.lastSuccess && g.career.military.failures==1 && g.career.military.credits==1200,"lost defense objective fails without another reward");
    }

    // Strike: assigned stable IDs must be destroyed through player damage; a surviving escort is not an objective.
    {
      auto q=make(military::CommandStrike); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(2);
      auto& m=g.hiveCombat.mission;
      check(m.config.targetCount==2 && m.config.targetIds[0] && m.config.targetIds[1],"strike records two designated stable IDs");
      g.plane.pos=vec3(0,18000,0); g.plane.vel=vec3(); killAll(g); g.updateHive(dt);
      check(m.targetsDestroyed[0] && m.targetsDestroyed[1] && m.status==hive::MissionStatus::Extract && g.screen==SCR_FLIGHT,"strike target destruction gates extraction instead of immediate payout");
      g.plane.pos=m.config.extraction; g.updateHive(dt);
      check(g.lastSuccess && g.career.military.credits==1800 && snapshot(g.career,true)==before,"strike extraction completes separate service settlement");
    }

    // Pause/time acceleration, real hit-to-crash path, abort and loading/menu cancellation.
    {
      auto q=make(military::CommandStrike); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(1);
      const float elapsed=g.hiveCombat.simulatedSeconds(), heat=g.combatLoadout.heat;
      g.paused=true; g.updateFlight(.2f);
      check(near(g.hiveCombat.simulatedSeconds(),elapsed) && near(g.combatLoadout.heat,heat),"pause freezes combat and equipment timers");
      g.paused=false; g.timeAccel=4; g.updateFlight(dt);
      check(g.timeAccel==1 && g.hiveCombat.simulatedSeconds()<=elapsed+dt*1.1f,"military simulation rejects time acceleration");
      auto& p=g.hiveCombat.projectiles[0]; p={}; p.alive=true; p.team=hive::Team::Hive; p.weapon=hive::Weapon::Pulse;
      p.position=g.plane.pos; p.radius=100; p.damage=200; p.life=10;
      g.updateFlight(dt);
      check(g.crashed && g.militaryHull==0,"enemy projectile damage enters the real Game crash path");
      g.crashTimer=g.crashEndT+1; g.updateFlight(dt);
      check(g.screen==SCR_DEBRIEF && !g.lastSuccess && g.result.outcome==OUT_CRASHED && g.career.military.failures==1,"crash timer settles one military failure");
      check(snapshot(g.career,true)==before,"combat crash does not charge civilian repairs or crash statistics");
      g.launchMilitary(0); g.endFlight(false,"User abort",OUT_ABANDONED);
      check(g.result.outcome==OUT_ABANDONED && g.career.military.failures==2 && !g.career.attemptOpen,"explicit abort settles and closes military token");
      g.launchMilitary(2); const int sorties=g.career.military.sorties;
      g.screen=SCR_LOADING; g.in.pressed[K_ESC]=true; g.updateLoading(dt); g.in.endFrame();
      check(g.screen==SCR_HUB && !g.militaryFlight && !g.career.attemptOpen && !g.career.military.activeAttempt && g.hiveCombat.aliveCount()==0,"loading cancellation returns to hub and clears runtime/token");
      check(g.career.military.sorties==sorties && snapshot(g.career,true)==before,"canceling before flight preserves civilian waiting job and grants nothing");
    }

    // A launch screen is a real simulation barrier. Render completion is supplied explicitly here; no GL is
    // initialized, and a timer or repeated launch input must never stand in for completed scenery frames.
    {
      auto q=make(military::CommandStrike); Game& g=*q; g.launchMilitary(2); g.screen=SCR_LOADING;
      const int oldPending=g_ren.entPending, oldBake=g_ren.bakeCount;
      const vec3 position=g.plane.pos; const float fuel=g.plane.fuel, clock=g.flightClock;
      const float combatClock=g.hiveCombat.simulatedSeconds(), missionClock=g.hiveCombat.mission.elapsed;
      const auto actors=g.hiveCombat.actors; const int rounds=g.combatLoadout.rounds;
      auto frozen=[&]() {
        bool same=length(g.plane.pos-position)==0 && g.plane.fuel==fuel && g.flightClock==clock &&
          g.hiveCombat.simulatedSeconds()==combatClock && g.hiveCombat.mission.elapsed==missionClock &&
          g.combatLoadout.rounds==rounds;
        for(int i=0;i<hive::MaxActors;++i) same&=g.hiveCombat.actors[i].id==actors[i].id &&
          length(g.hiveCombat.actors[i].position-actors[i].position)==0 && g.hiveCombat.actors[i].timer==actors[i].timer;
        return same;
      };
      g_ren.entPending=9;
      for(int n=0;n<80;++n) { g.in.pressed[K_ENTER]=g.in.pressed[' ']=g.in.mPressed[0]=true;g.in.buttonsPressed=PAD_A;g.update(dt);g.in.endFrame(); }
      check(g.screen==SCR_LOADING && g.loadReadyT<0 && g.loadShown<1 && frozen(),
        "elapsed loading time and launch spam cannot start simulation or certify unrendered work");
      g.loadFrames=2;g.update(dt);g.in.endFrame();
      check(g.loadReadyT<0 && frozen(),"completed scene frame with pending scenery stays behind loading screen");
      g_ren.entPending=0; ++g_ren.bakeCount; ++g.loadFrames;g.update(dt);g.in.endFrame();
      check(g.loadReadyT<0 && g.loadStableFrames==0 && frozen(),"a new mesh bake invalidates loading stability");
      ++g.loadFrames;g.update(dt);g.in.endFrame();
      const int firstStable=g.loadStableFrames;
      for(int n=0;n<40;++n) {g.update(dt);g.in.endFrame();}
      check(g.loadReadyT<0 && firstStable==1 && g.loadStableFrames==1 && frozen(),
        "repeated updates of one renderer frame cannot fabricate a second completed frame");
      ++g.loadFrames;g.update(dt);g.in.endFrame();
      check(g.loadReadyT>=0 && g.loadShown==1 && g.screen==SCR_LOADING && frozen(),
        "two distinct stable scene frames unlock briefing confirmation without advancing flight");
      for(int n=0;n<40;++n) {g.update(dt);g.in.endFrame();}
      check(g.screen==SCR_LOADING && frozen(),"prepared military scene waits for an explicit launch confirmation");
      g.in.pad=true;g.in.buttonsPressed=PAD_A;g.update(dt);
      check(g.screen==SCR_FLIGHT && !g.in.pressed[K_ENTER] && !g.in.mPressed[0] && !(g.in.buttonsPressed&PAD_A) && frozen(),
        "controller confirmation enters flight once and consumes launch input without simulating the loading frame");
      g.in.endFrame();g.update(dt);g.in.endFrame();
      check(g.flightClock>clock && g.hiveCombat.simulatedSeconds()>combatClock,"simulation begins only on the next actual flight update");
      g.endFlight(false,"Loading barrier test",OUT_ABANDONED);
      g_ren.entPending=oldPending;g_ren.bakeCount=oldBake;
    }

    // Back must finish one navigation only, and an unsaved cancellation must block the next launch.
    for(bool pad:{false,true}) {
      auto q=make(military::CommandStrike); Game& g=*q; const std::string civil=snapshot(g.career,true);
      g.hubList=2;g.launchMilitary(2);g.screen=SCR_LOADING;
      const uint32_t token=g.career.military.activeAttempt; const int sorties=g.career.military.sorties;
      g.in.pad=pad;
      if(pad) g.in.buttonsPressed=PAD_B; else g.in.pressed[K_ESC]=true;
      g.update(dt);
      check(g.screen==SCR_HUB && g.hubList==2 && !g.militaryFlight && !g.career.attemptOpen && !g.career.military.activeAttempt,
        std::string(pad?"controller":"keyboard")+" loading Back closes the loaner attempt on the military board");
      g.drawHub();g.in.endFrame();
      check(g.screen==SCR_HUB && g.hubList==2,"loading Back is consumed before the destination board draws");
      check(g.career.military.sorties==sorties && snapshot(g.career,true)==civil,"loading Back grants no result and preserves all civilian state");
      g.launchMilitary(2);
      check(g.militaryFlight && g.career.military.activeAttempt>token,"a canceled loading attempt can be relaunched with a fresh token");
      g.screen=SCR_LOADING;
      const fs::path blocked=root/(pad?"blocked_pad_cancel":"blocked_keyboard_cancel"); {std::ofstream f(blocked);f<<"block";}
      g.diskless=false;g.saveDir=blocked.string();g.in.pressed[K_ESC]=true;g.update(dt);g.in.endFrame();
      check(g.screen==SCR_HUB && !g.militaryFlight && g.pendingCareer && g.career.attemptOpen && !g.pendingCareer->attemptOpen,
        "failed cancellation save keeps one pending close while discarding the runtime flight");
      const auto pending=g.pendingCareer?snapshot(*g.pendingCareer):std::string();const auto active=snapshot(g.career);
      g.launchMilitary(1);
      check(g.screen==SCR_HUB && !g.militaryFlight && snapshot(g.career)==active && g.pendingCareer && snapshot(*g.pendingCareer)==pending,
        "pending loading cancellation blocks a second launch without replacing the pending close");
      const fs::path good=root/(pad?"pad_cancel_saved":"keyboard_cancel_saved");fs::create_directories(good);g.saveDir=good.string();
      check(g.retryCommit() && !g.pendingCareer && !g.career.attemptOpen && !g.career.military.activeAttempt &&
        g.career.military.sorties==sorties && snapshot(g.career,true)==civil,
        "retrying canceled launch save closes only its token and preserves civilian records");
    }

    // Every expanded catalogue entry launches its actual XR loaner and completes through the live objective
    // engine. Scripted positions isolate sensor/target requirements without substituting success flags.
    for(int missionId=military::LocalSurvey;missionId<military::kMissionCount;++missionId) {
      auto q=make(missionId); Game& g=*q; const auto& definition=*military::mission(missionId);
      const int priorSuccesses=g.career.military.successes;
      const std::string civil=snapshot(g.career,true); const uint32_t beforeAttempt=g.career.attempt;
      g.launchMilitary(missionId);auto& m=g.hiveCombat.mission;
      check(g.militaryFlight && g.source==Career::SRC_MILITARY && g.specIdx==definition.aircraft &&
        g.plane.spec==&kAircraft[definition.aircraft] && g.career.military.activeKind==missionId,
        "expanded mission launches its catalogued XR loaner "+std::to_string(missionId));
      check(g.career.attempt==beforeAttempt+1 && m.config.kind==definition.objective &&
        g.combatLoadout.forward==definition.forward && g.combatLoadout.bomb==definition.bomb &&
        near(g.plane.payload,85+(definition.aircraft==kWraith?g.combatLoadout.massKg():0)),
        "mission objective, equipment and physical payload agree with catalogue "+std::to_string(missionId));
      FrameParams loading;const auto position=g.plane.pos;const float fuel=g.plane.fuel;
      g.militaryLoadingCamera(loading);
      check(loading.plane.model==definition.aircraft && length(loading.plane.pos-g.plane.pos)<.001f &&
        loading.prefetchOn && length(loading.prefetchPos-g.plane.pos)<.001f && length(position-g.plane.pos)==0 && fuel==g.plane.fuel,
        "loading preview shows the actual selected frozen aircraft and streams its mission position "+std::to_string(missionId));
      const int oldWidth=g_ren.W,oldHeight=g_ren.H;bool loadingVisible=true;
      for(const auto viewport:std::array<std::pair<int,int>,5>{{{1920,1080},{1280,720},{960,540},{800,600},{640,480}}}){
        g_ren.W=viewport.first;g_ren.H=viewport.second;FrameParams frame;g.militaryLoadingCamera(frame);
        const auto layout=loadingLayout(g_ren.W,g_ren.H);const auto& aircraft=*g.plane.spec;
        for(vec3 point:{vec3(),vec3(aircraft.span*.5f,0,0),vec3(-aircraft.span*.5f,0,0),vec3(0,0,aircraft.fusLen*.5f),vec3(0,0,-aircraft.fusLen*.5f),vec3(0,aircraft.fusRad*3,0)}){
          const vec3 relative=g.plane.pos+g.plane.q.rotate(point)-frame.camPos;const float depth=-dot(relative,frame.camBack),th=tanf(frame.fovY*.5f);
          const float x=g_ren.W*.5f+dot(relative,frame.camRight)/depth/th*g_ren.H*.5f,y=g_ren.H*.5f-dot(relative,frame.camUp)/depth/th*g_ren.H*.5f;
          loadingVisible&=depth>0&&x>=0&&x<=g_ren.W&&y>=layout.artTop&&y<=layout.artBottom;
        }
      }
      g_ren.W=oldWidth;g_ren.H=oldHeight;
      check(loadingVisible,"entire loading aircraft fits between header and telemetry at five viewport sizes "+std::to_string(missionId));
      if(definition.objective==hive::MissionKind::Recon) {
        check(g.hiveCombat.aliveCount()==0 && m.config.objectiveCount>0 && near(m.config.scanRange,definition.scanRange) &&
          near(m.config.scanSeconds,definition.scanSeconds),"noncombat XR mission has authored sensors and no compulsory pursuers "+std::to_string(missionId));
        for(int i=0;i<m.config.objectiveCount;++i) {
          const vec3 site=m.config.objectives[i].position;
          const float high=missionId==military::HighAltitudeIntel?4500.f-site.y:missionId==military::HoverScan?75.f:350.f;
          const float ahead=missionId==military::HighAltitudeIntel?2500.f:missionId==military::HoverScan?120.f:450.f;
          auto aim=[&](float altitude,float speed,float distance=-1.f) {
            if(distance<0)distance=ahead;
            g.plane.pos=site+vec3(0,altitude,distance);g.plane.vel=vec3(0,0,-speed);
            g.plane.q=quat::axisAngle(vec3(1,0,0),-atan2f(altitude,distance));
          };
          if(i==0 && (missionId==military::HighAltitudeIntel || missionId==military::HoverScan)) {
            const float scan=m.config.objectives[i].scan;
            aim(high,missionId==military::HighAltitudeIntel?100.f:20.f);
            for(int n=0;n<60;++n)g.updateHive(dt);
            check(near(m.config.objectives[i].scan,scan),"sensor rejects unsuitable actual aircraft speed "+std::to_string(missionId));
            aim(high,0);g.plane.vel=vec3(0,missionId==military::HighAltitudeIntel?220.f:20.f,0);
            for(int n=0;n<60;++n)g.updateHive(dt);
            check(near(m.config.objectives[i].scan,scan),"vertical motion cannot bypass reconnaissance speed requirements "+std::to_string(missionId));
            aim(missionId==military::HighAltitudeIntel?3000.f-site.y:20.f,missionId==military::HighAltitudeIntel?200.f:0.f);
            for(int n=0;n<60;++n)g.updateHive(dt);
            check(near(m.config.objectives[i].scan,scan),"sensor rejects aircraft below the required altitude envelope "+std::to_string(missionId));
            aim(missionId==military::HighAltitudeIntel?6600.f-site.y:140.f,missionId==military::HighAltitudeIntel?200.f:0.f,
                missionId==military::HighAltitudeIntel?1000.f:ahead);
            for(int n=0;n<60;++n)g.updateHive(dt);
            check(near(m.config.objectives[i].scan,scan),"sensor rejects aircraft above the required altitude envelope "+std::to_string(missionId));
          }
          aim(high,missionId==military::HighAltitudeIntel?200.f:0.f);
          for(int n=0;n<int(definition.scanSeconds/dt)+30 && !m.config.objectives[i].scanned;++n)g.updateHive(dt);
          check(m.config.objectives[i].scanned && !g.militarySiteLabels[i].empty(),
            "XR sensors complete real labeled site dwell "+std::to_string(missionId)+"/"+std::to_string(i));
        }
      } else {
        g.plane.pos=vec3(0,18000,0);g.plane.vel=vec3();
        g.wraith.armed=true;g.wraith.lasers=1;g.wraith.wantFire=true;
        const int rounds=g.combatLoadout.rounds;const float payload=g.plane.payload;
        for(int n=0;n<90 && g.combatLoadout.rounds==rounds;++n){g.wraith.wantFire=true;g.updateWraith(dt);}
        check(g.combatLoadout.rounds==rounds-1 && !g.wraith.bolts.empty() && g.wraith.bolts.back().kind==definition.forward &&
          near(g.plane.payload,payload-hive::forwardSpec(definition.forward).roundMassKg),
          "armed XR mission fires its supplied finite physical gun "+std::to_string(missionId));
        g.wraith.wantFire=false;
        if(definition.objective==hive::MissionKind::Defense) {
          check(m.config.waveCount==4 && m.config.waves[0].types[0]==hive::Type::Bastion,
            "heavy defense is four bomber-led waves rather than the introductory encounter");
          for(int i=0;i<m.config.waveCount;++i)m.config.waves[i].at=i*.05f;
          for(int n=0;n<60 && m.status==hive::MissionStatus::Active;++n){g.updateHive(dt);killAll(g);}g.updateHive(dt);
          check(m.nextWave==m.config.waveCount && m.config.objectives[0].health>0,"expanded defense resolves every scheduled wave and protects the actual site");
        } else {
          check(m.config.targetCount==3,"expanded armed sweep has three separately designated target IDs "+std::to_string(missionId));
          bool roles=true;
          for(int i=0;i<m.config.targetCount;++i) {
            auto* a=g.hiveCombat.find(m.config.targetIds[i]);
            roles&=a && a->type==(missionId==military::Interception?hive::Type::Needle:hive::Type::Cantor);
            for(int j=0;j<i;++j)roles&=m.config.targetIds[i]!=m.config.targetIds[j];
          }
          check(roles,"expanded strike targets have unique IDs and the advertised enemy roles "+std::to_string(missionId));
          for(int pass=0;pass<2;++pass)for(int i=0;i<m.config.targetCount;++i) {
            auto* a=g.hiveCombat.find(m.config.targetIds[i]);if(!a || !a->alive)continue;
            const vec3 p=a->position;g.hiveCombat.playerShot(p+vec3(0,50,0),p-vec3(0,50,0),100000,g.hiveWorld());
          }
          g.updateHive(dt);
          if(missionId==military::RelaySuppression)check(g.hiveCombat.aliveCount()==2,"optional relay escorts may remain alive after designated targets are destroyed");
        }
      }
      check(g.screen==SCR_FLIGHT && m.status==hive::MissionStatus::Extract && g.career.military.credits==0,
        "expanded mission requires extraction before any service reward "+std::to_string(missionId));
      g.plane.pos=m.config.extraction;g.updateHive(dt);
      check(g.screen==SCR_DEBRIEF && g.lastSuccess && g.career.military.successes==priorSuccesses+1 &&
        g.career.military.credits==definition.reward && g.career.military.intelligence==int(definition.intelligence) && snapshot(g.career,true)==civil,
        "expanded mission settles exactly its service reward without civilian changes "+std::to_string(missionId));
      g.retryFromDebrief();
      check(g.militaryFlight && g.career.military.activeKind==missionId && g.career.attempt==beforeAttempt+2 &&
        g.specIdx==definition.aircraft && g.combatLoadout.forward==definition.forward && g.combatLoadout.bomb==definition.bomb,
        "expanded retry preserves mission identity, aircraft and equipment with a fresh attempt "+std::to_string(missionId));
      g.endFlight(false,"Expanded mission retry test",OUT_ABANDONED);
    }

    // Finite-store feasibility is separate from aim/flight difficulty: use real cooldown, charge, heat, magazine
    // consumption and shaped target damage. Targets are separated so one bomb cannot gain convenient splash
    // damage. Run both a 20% gun-miss budget and a conservative fully shielded, accurate-hit budget.
    for(bool shielded:{false,true}) {
      auto q=make(military::HeavyDefense);Game& g=*q;g.launchMilitary(military::HeavyDefense);auto& m=g.hiveCombat.mission;
      const int initialRounds=g.combatLoadout.rounds,initialBombs=g.combatLoadout.bombs;
      const auto& gun=hive::forwardSpec(g.combatLoadout.forward);const auto& bomb=hive::bombSpec(g.combatLoadout.bomb);
      for(int i=0;i<m.config.waveCount;++i)m.config.waves[i].at=i*.05f;
      g.plane.pos=vec3(0,20000,0);g.plane.vel=vec3();
      bool funded=true;int shots=0,misses=0,stores=0,targets=0;
      for(int tick=0;tick<80 && m.status==hive::MissionStatus::Active && funded;++tick) {
        g.updateHive(dt);
        std::vector<uint32_t> ids;
        for(auto& a:g.hiveCombat.actors)if(a.alive) {
          a.position=vec3(5000.f+float(ids.size())*700.f,18000.f,0);a.velocity=vec3();
          if(shielded)a.shield=std::max(a.shield,100.f);
          ids.push_back(a.id);
        }
        std::stable_sort(ids.begin(),ids.end(),[&](uint32_t a,uint32_t b){return
          (g.hiveCombat.find(a)->type==hive::Type::Cantor)>(g.hiveCombat.find(b)->type==hive::Type::Cantor);});
        for(uint32_t id:ids) {
          auto* actor=g.hiveCombat.find(id);++targets;
          if(actor->type==hive::Type::Bastion) {
            for(int wait=0;wait<40 && g.combatLoadout.bombCooldown>0;++wait)g.combatLoadout.step(.25f,false);
            if(!g.combatLoadout.releaseBomb()){funded=false;break;}
            ++stores;g.hiveCombat.playerBlast(actor->position,bomb.radius,bomb.damage,g.hiveWorld());g.syncCombatPayload();
          }
          while(actor->alive && funded) {
            bool fired=false;
            for(int charge=0;charge<100 && !fired && g.combatLoadout.rounds>0;++charge)fired=g.combatLoadout.step(.25f,true);
            if(!fired){funded=false;break;}
            ++shots;const bool miss=!shielded && shots%5==0;misses+=miss;
            const vec3 aim=actor->position+vec3(miss?150.f:0.f,0,0);
            const uint32_t hit=g.hiveCombat.playerShot(aim+vec3(0,50,0),aim-vec3(0,50,0),gun.damage,g.hiveWorld());
            funded&=miss?hit==0:hit==id;g.syncCombatPayload();
          }
        }
      }
      g.updateHive(dt);
      check(funded && targets==7 && stores==4 && shots<=40 && g.combatLoadout.rounds==initialRounds-shots &&
        g.combatLoadout.bombs==initialBombs-stores && m.status==hive::MissionStatus::Extract && m.config.objectives[0].health>0,
        shielded?"heavy defense fits supplied finite equipment even with 100 shield on every hostile":
          "heavy defense fits supplied finite equipment with every fifth gun round missing and no splash advantage");
      printf("Military finite-ammo budget: shielded=%d targets=%d shots=%d misses=%d bombs=%d remaining_rounds=%d remaining_bombs=%d\n",
        shielded,targets,shots,misses,stores,g.combatLoadout.rounds,g.combatLoadout.bombs);
      g.plane.pos=m.config.extraction;g.updateHive(dt);
      check(g.screen==SCR_DEBRIEF && g.lastSuccess,"finite-ammo heavy defense reaches real extraction and successful settlement");
    }

    // Dynamic ammunition/cadence regression: authored wave timing and enemy AI keep running while 25% of
    // finite gun rounds intentionally miss. Aircraft motion is a scripted orbit and direct-hit locations are
    // scripted; this checks combat behavior, store capacity and cooling, not human aim or flight handling.
    for(int id:{military::IslandDefense,military::CommandStrike,military::Interception,military::HeavyDefense,military::RelaySuppression}) {
      auto q=make(id);Game& g=*q;g.launchMilitary(id);auto& m=g.hiveCombat.mission;
      const auto& def=*military::mission(id);const auto& gun=hive::forwardSpec(def.forward);const auto& bomb=hive::bombSpec(def.bomb);
      const int rounds=g.combatLoadout.rounds,bombs=g.combatLoadout.bombs;
      const vec3 center=m.config.kind==hive::MissionKind::Defense?m.config.objectives[0].position:
        g.hiveCombat.find(m.config.targetIds[0])->position;
      std::set<uint32_t> bombed,enemyProjectiles;int fired=0,missed=0,dropped=0;
      for(int tick=0;tick<60*300 && m.status==hive::MissionStatus::Active && !g.plane.ev.crashed;++tick) {
        const float a=tick*dt*.1f;
        g.plane.pos=center+vec3(cosf(a)*1800.f,900.f,sinf(a)*1800.f);
        g.plane.vel=vec3(-sinf(a)*180.f,0,cosf(a)*180.f);
        g.updateHive(dt);
        for(const auto& p:g.hiveCombat.projectiles)if(p.alive)enemyProjectiles.insert(p.id);
        hive::Actor* target=nullptr;int priority=100;
        for(auto& actor:g.hiveCombat.actors)if(actor.alive) {
          bool required=m.config.kind==hive::MissionKind::Defense;
          for(int t=0;t<m.config.targetCount;++t)required|=actor.id==m.config.targetIds[t];
          if(!required)continue;
          const int rank=actor.type==hive::Type::Cantor?0:actor.type==hive::Type::Bastion?1:actor.type==hive::Type::Archon?2:3;
          if(rank<priority){target=&actor;priority=rank;}
        }
        const bool shot=g.combatLoadout.step(dt,target!=nullptr);
        if(!target)continue;
        const bool useStore=bomb.damage>0?(target->type==hive::Type::Bastion || target->type==hive::Type::Archon):
          target->shield>0 || target->type==hive::Type::Cantor;
        if(useStore && !bombed.count(target->id) && g.combatLoadout.releaseBomb()) {
          bombed.insert(target->id);++dropped;
          if(def.bomb==hive::BombSet::EMP)g.hiveCombat.playerEMP(target->position,bomb.radius,bomb.disruption,g.hiveWorld());
          else g.hiveCombat.playerBlast(target->position,bomb.radius,bomb.damage,g.hiveWorld());
        }
        if(shot) {
          ++fired;const bool miss=fired%4==0;missed+=miss;
          if(!miss && target->alive) {
            const vec3 p=target->position;
            g.hiveCombat.playerShot(p+vec3(0,50,0),p-vec3(0,50,0),gun.damage,g.hiveWorld());
          }
        }
        g.syncCombatPayload();
      }
      const float objectiveHealth=m.config.objectiveCount?m.config.objectives[0].health:0;
      check(m.status==hive::MissionStatus::Extract && !g.plane.ev.crashed && g.militaryHull>0 && fired<rounds && missed>0 &&
        g.combatLoadout.rounds==rounds-fired && g.combatLoadout.bombs==bombs-dropped && !enemyProjectiles.empty(),
        "armed sortie completes live enemy AI and authored cadence with 25-percent intentional gun misses "+std::to_string(id));
      printf("Military dynamic-ammo: mission=%d seconds=%.1f rounds=%d/%d misses=%d bombs=%d/%d enemy_projectiles=%zu hull=%.1f objective=%.1f status=%d\n",
        id,m.elapsed,fired,rounds,missed,dropped,bombs,enemyProjectiles.size(),g.militaryHull,objectiveHealth,int(m.status));
      if(m.status==hive::MissionStatus::Extract){g.plane.pos=m.config.extraction;g.updateHive(dt);}
      check(g.screen==SCR_DEBRIEF && g.lastSuccess,"live-AI finite-ammo sortie reaches successful extraction "+std::to_string(id));
    }

    // Returning a loaner removes its complete runtime footprint before entering any civilian mode.
    {
      auto q=make(military::RelaySuppression);Game& g=*q;g.launchMilitary(military::RelaySuppression);
      std::vector<AptItem> items;airportItems(g.career.location,items);
      check(!items.empty(),"cleanup regression uses an actual authored airport entity");
      if(!items.empty())check(g_scenery.damage(items[0].e,items[0].kind,100000) && g_scenery.destroyed(items[0].e),
        "cleanup regression damages an entity through the production scenery path");
      g.wraith.bolts.push_back({g.plane.pos,vec3(),vec3(0,0,-1),30,0,10,false});
      g.wraith.bombs.push_back({g.plane.pos,vec3(),0});g.wraith.craters.push_back({0,0,20,3});
      g.wraith.flattened.push_back(vec3(0,0,20));g_scenery.craters.push_back(vec3(0,0,20));
      g.wraith.wantFire=true;g.wraith.armed=true;g.wraith.bombQueue=3;g.militaryHull=12;
      g.tipTrail[0].resize(1);g.tipTrail[1].resize(1);g.pieceTrail[0].resize(1);g.bursts.resize(1);
      g.trail.resize(1);g.cloudWake.resize(1);g.tipOn=g.airBreak=true;
      g.endFlight(false,"Cleanup regression",OUT_ABANDONED);const auto settled=snapshot(g.career);
      g.returnToMilitary();FrameParams fp;g.hiveVisual(fp);
      check(!g.militaryFlight && !g.researchFlight && !g.freeFlight && !g.isolatedFlight && g.source==Career::SRC_NONE &&
        g.screen==SCR_HUB && g.hubList==2 && g.militaryHull==100 && !g.combatCollisionWarm &&
        g.hiveCombat.aliveCount()==0 && g.hiveCombat.mission.status==hive::MissionStatus::Inactive && fp.enemyN==0 && fp.hiveOrdnanceN==0,
        "returning loaner clears mode, source, hull damage, collision warm state and all enemy rendering");
      check(g.wraith.bolts.empty() && g.wraith.bombs.empty() && g.wraith.blasts.empty() && g.wraith.craters.empty() &&
        g.wraith.flattened.empty() && !g.wraith.wantFire && !g.wraith.armed && g.wraith.bombQueue==0 && g.wraith.fireLatch &&
        !g_scenery.anyGone() && (items.empty() || !g_scenery.destroyed(items[0].e)),
        "returning loaner clears all weapon effects, held controls, craters and destroyed civilian scenery");
      bool noPieceTrails=true;for(const auto& trail:g.pieceTrail)noPieceTrails&=trail.empty();
      check(g.tipTrail[0].empty() && g.tipTrail[1].empty() && noPieceTrails && g.bursts.empty() && g.trail.empty() &&
        g.cloudWake.empty() && !g.tipOn && !g.airBreak,"military return cannot leave trail, breakup or shockwave artifacts on the hub");
      g.returnToMilitary();check(snapshot(g.career)==settled,"repeated loaner cleanup never duplicates or changes settlement");
      g.beginFreeFlightSetup();g.freeCraft=0;g.freeAirport=g.career.location;g.freeAirborne=true;g.launchFreeFlight();
      check(g.freeFlight && !g.militaryFlight && g.specIdx==0 && g.source!=Career::SRC_MILITARY && near(g.plane.payload,85) &&
        g.hiveCombat.aliveCount()==0 && g.wraith.bombs.empty() && !g_scenery.anyGone() && snapshot(g.career)==settled,
        "civilian free flight after military return has no loaner equipment, enemies, damage or extra charges");
      g.endFlight(false,"",OUT_ABANDONED);
    }

    // Board selection is bounded to actual supplied XR aircraft. Enemy roles never become player aircraft.
    {
      auto q=make(military::CommandStrike);Game& g=*q;const std::string before=snapshot(g.career);const auto screen=g.screen;
      for(int invalid:{-1,military::kMissionCount,9999})g.launchMilitary(invalid);
      check(g.screen==screen && !g.militaryFlight && snapshot(g.career)==before,"invalid military mission IDs cannot launch or commit an attempt");
      bool research[4]={};bool legal=true;
      for(int id:military::kBoardMissionIds) {
        const auto& d=*military::mission(id);const auto c=g.career.militaryContract(id);
        legal&=validAircraft(d.aircraft) && isResearchAircraft(d.aircraft) && c.forceAircraft==d.aircraft;
        for(int i=0;i<4;++i)research[i]|=d.aircraft==kResearchAircraft[i];
        for(int craft=-1;craft<=kAircraftCount+4;++craft)
          legal&=g.career.canFly(c,craft)==(craft==d.aircraft?Career::SRC_MILITARY:Career::SRC_NONE);
        g.militarySelection=id;g.resCraft=(d.aircraft==kWraith?kNightjar:kWraith);
        FrameParams preview;g.militaryPreviewCamera(preview);
        legal&=preview.plane.model==d.aircraft && preview.enemyN==0 && preview.trafficN==0;
      }
      check(legal && research[0] && research[1] && research[2] && research[3],
        "military board covers all four XR models and admits only the exact player loaner, never enemy roles");
      check(snapshot(g.career)==before,"previewing every loaner is read-only to the whole career");
    }

    // Render packets cannot silently drop live ordnance, and collision follows the banked aircraft shape.
    {
      auto q=make(military::IslandDefense); Game& g=*q; g.launchMilitary(1); killAll(g);
      FrameParams fp; fp.fx.beams=16;
      for(int i=0;i<hive::MaxProjectiles;++i) {
        auto& p=g.hiveCombat.projectiles[i]; p={}; p.alive=true; p.position=g.plane.pos+vec3(float(i),100,0); p.velocity=vec3(0,0,-100);
      }
      g.hiveVisual(fp);
      check(fp.hiveOrdnanceN==hive::MaxProjectiles && fp.fx.beams==16,"full player beam budget never hides any of 96 live enemy projectiles");
      g.screen=SCR_LOADING; g.hiveVisual(fp);
      check(fp.enemyN==0 && fp.hiveOrdnanceN==0,"loading warmup does not manufacture drawable or shadow-casting actors");
      g.screen=SCR_FLIGHT; for(auto& p:g.hiveCombat.projectiles) p.alive=false;
      g.plane.pos=vec3(0,12000,0); g.plane.vel=vec3(); g.plane.q=quat();
      auto& p=g.hiveCombat.projectiles[0]; p={}; p.alive=true; p.team=hive::Team::Hive; p.position=g.plane.pos+vec3(0,8,0); p.radius=.1f; p.damage=20; p.life=10;
      g.updateHive(dt);
      check(g.militaryHull==100,"shot eight metres above fuselage misses instead of hitting old 12-metre sphere");
      p.alive=true; p.position=g.plane.pos; p.life=10; g.updateHive(dt);
      check(g.militaryHull==80,"shot through authored fuselage still applies damage");
      g.plane.q=quat::axisAngle(vec3(0,0,1),PI*.5f);
      p.alive=true; p.position=g.plane.pos+g.plane.q.rotate(vec3(4.6f,-.24f,2.f)); p.life=10; g.updateHive(dt);
      check(g.militaryHull==60,"banked wing collision uses aircraft up/orientation");
      g.endFlight(false,"Collision test done",OUT_ABANDONED);
    }

    // Save failure at launch is a precondition failure; failed settlement remains one retryable transaction.
    {
      auto q=make(military::IslandDefense); Game& g=*q; const fs::path blocked=root/"not_a_directory"; { std::ofstream f(blocked); f<<"block"; }
      g.diskless=false; g.saveDir=blocked.string(); const std::string before=snapshot(g.career);
      const auto oldScreen=g.screen; g.launchMilitary(1);
      check(g.screen==oldScreen && snapshot(g.career)==before && !g.pendingCareer && !g.career.attemptOpen,"failed launch save never starts flight or leaves a pending token");
      g.diskless=true; g.launchMilitary(1); const int prior=g.career.military.sorties;
      g.diskless=false; g.endFlight(false,"Abort requiring save",OUT_ABANDONED);
      check(g.pendingCareer && g.career.military.sorties==prior && g.pendingCareer->military.sorties==prior+1,"failed settlement keeps civilian/current career unchanged and queues one result");
      g.endFlight(false,"Repeated abort",OUT_ABANDONED);
      check(g.pendingCareer && g.pendingCareer->military.sorties==prior+1,"repeated abort while save is pending cannot settle twice");
      const fs::path good=root/"settled"; fs::create_directories(good); g.saveDir=good.string();
      check(g.retryCommit() && !g.pendingCareer && g.career.military.sorties==prior+1 && !g.career.attemptOpen,"retry saves exactly one completed military transaction");
    }

    // First-tick visual trails must begin at the firing socket, never before the shot existed.
    {
      auto q=make(2);Game& g=*q;g.launchMilitary(2);g.plane.pos=vec3(0,12000,-1000);
      for(int role=0;role<4;++role) {
        g.hiveCombat.reset(1,WRAP_SPAN);
        const auto type=static_cast<hive::Type>(role);
        const auto id=g.hiveCombat.spawn(type,vec3(0,12000,0),vec3(0,0,-200));
        auto* actor=g.hiveCombat.find(id);
        hive::PlayerSnapshot player;player.position=g.plane.pos;
        if(type!=hive::Type::Cantor) { actor->phase=hive::Phase::Firing;actor->aim=player.position;actor->shotsRemaining=1;actor->shotTimer=0; }
        g.hiveCombat.clearEvents();g.hiveCombat.step(hive::FixedStep,player);
        FrameParams fp;g.hiveVisual(fp);
        if(type==hive::Type::Cantor) {
          check(fp.hiveOrdnanceN==0,"Cantor relay role does not invent a projectile trail");continue;
        }
        const hive::Event* fire=nullptr;
        for(int e=0;e<g.hiveCombat.eventCount;++e) if(g.hiveCombat.events[e].type==hive::EventType::Fire)fire=&g.hiveCombat.events[e];
        check(fire && fp.hiveOrdnanceN==1 && length(g.hiveCombat.displacement(fire->position,fp.hiveOrdnance[0].tail))<.02f,
          "first-tick projectile trail stays at actual firing origin for role "+std::to_string(role));
        auto& p=g.hiveCombat.projectiles[0];
        const vec3 origin=p.position;
        p.life=hive::projectileLifetime(p.weapon)-.008f;p.position=origin+p.velocity*.008f;
        g.hiveVisual(fp);
        check(length(fp.hiveOrdnance[0].tail-origin)<.02f,"8ms-old projectile cannot show 25ms of prior travel "+std::to_string(role));
        p.life=hive::projectileLifetime(p.weapon)-.2f;g.hiveVisual(fp);
        check(std::fabs(length(fp.hiveOrdnance[0].head-fp.hiveOrdnance[0].tail)-std::min(35.f,length(p.velocity)*.025f))<.02f,
          "mature projectile keeps original steady trail length for role "+std::to_string(role));
      }
    }

    // The player bolt is consumed at its actual contact, including interceptable enemy ordnance.
    {
      auto q=make();Game& g=*q;g.resCraft=kWraith;g.resCard=-1;g.resAirborne=true;g.resAirport=g.career.location;g.launchResearch();
      g.plane.pos=vec3(0,12500,0);
      const uint32_t target=g.hiveCombat.spawn(hive::Type::Needle,vec3(0,12000,0));
      g.wraith.bolts.push_back({vec3(0,12000,80),vec3(0,0,-1000),vec3(0,0,-1),30,0,10,false,hive::ForwardSet::Pulse});
      g.updateBolts(.1f);
      check(!g.wraith.bolts.empty() && g.wraith.bolts[0].hit && std::fabs(g.wraith.bolts[0].h.z)<10 && g.hiveCombat.find(target)->hull<hive::roleSpec(hive::Type::Needle).hull,"player bolt stops on shaped hull instead of frame-end position");
      g.clearPracticeEncounter();
      auto& bomb=g.hiveCombat.projectiles[0];bomb={};bomb.alive=true;bomb.id=77;bomb.team=hive::Team::Hive;bomb.weapon=hive::Weapon::Bomb;bomb.position=vec3(0,12000,40);bomb.radius=.45f;bomb.life=10;
      g.wraith.bolts.push_back({vec3(0,12000,80),vec3(0,0,-1000),vec3(0,0,-1),30,0,10,false,hive::ForwardSet::Pulse});
      g.updateBolts(.1f);
      check(!bomb.alive && !g.wraith.bolts.empty() && g.wraith.bolts[0].hit && std::fabs(g.wraith.bolts[0].h.z-40.45f)<.01f,"intercepting a Hive bomb consumes the player bolt at exact contact");
      g.endFlight(false,"Bolt tests finished",OUT_ABANDONED);
    }

    // Fast physical bombs must hit intervening hulls/buildings; ground-origin LOS must not suppress blast damage.
    {
      auto q=make(); Game& g=*q; g.resCraft=kWraith;g.resCard=-1;g.resAirborne=true;g.resAirport=g.career.location;g.launchResearch();
      g.plane.pos=vec3(0,12500,0); g.plane.vel=vec3();
      const uint32_t id=g.hiveCombat.spawn(hive::Type::Needle,vec3(0,12000,0));
      g.wraith.bombs.push_back({vec3(0,12000,80),vec3(0,0,-1000),1,hive::BombSet::Plasma});
      g.updateWraith(.16f);
      check(g.wraith.bombs.empty() && g.hiveCombat.find(id)->hull<hive::roleSpec(hive::Type::Needle).hull && !g.wraith.blasts.empty() && std::fabs(g.wraith.blasts.back().p.z)<15,"fast bomb crossing Hive hull detonates at swept contact, not frame end");
      g.clearPracticeEncounter(); g.refreshGroundPits();
      std::vector<AptItem> items; airportItems(g.career.location,items); const AptItem* building=nullptr;
      for(const auto& item:items) if(item.kind==EK_ARCH_HANGAR || item.kind==EK_HANGAR) {building=&item;break;}
      check(building!=nullptr,"airport supplies actual building for bomb collision regression");
      if(building) {
        const auto& e=building->e; const auto& info=kEntInfo[building->kind];
        vec3 center(e.x,e.y+info.h*e.sy*.55f,e.z);
        const float span=std::max(info.hx*e.sx,info.hz*e.sz)+40.f;
        vec3 from=center-vec3(span,0,0), velocity(2500,0,0);
        const float tick=.08f; vec3 nextV=velocity+vec3(0,-G0,0)*tick-velocity*(2e-5f*length(velocity)*tick);
        vec3 end=from+nextV*tick; float fraction=hiveWorldSweep(from,end,.29f);
        check(fraction>=0 && fraction<=1,"real building obstructs fast bomb segment");
        const vec3 expected=lerp(from,end,clampf(fraction,0.f,1.f));
        g.wraith.bombs.push_back({from,velocity,1,hive::BombSet::Plasma});g.updateWraith(tick);
        check(g.wraith.bombs.empty() && !g.wraith.blasts.empty() && length(g.wraith.blasts.back().p-expected)<.1f,"fast bomb selects actual nearest world obstruction");
        check(!g.wraith.craters.empty(),"normal land plasma detonation retains crater behavior");
        g.clearPracticeEncounter();g.refreshGroundPits();
        const vec3 boltEnd=from+velocity*tick; const float boltFraction=hiveWorldSweep(from,boltEnd,0);
        g.wraith.bolts.push_back({from,velocity,normalize(velocity),30,0,10,false,hive::ForwardSet::Pulse});
        g.updateBolts(tick);
        check(boltFraction<=1 && !g.wraith.bolts.empty() && g.wraith.bolts[0].hit && length(g.wraith.bolts[0].h-lerp(from,boltEnd,boltFraction))<.1f,"accurate world contact consumes bolt before an intervening building");

        g.clearPracticeEncounter();g.refreshGroundPits();
        const vec3 blast=center-vec3(span,0,0), hidden=center+vec3(span,0,0);
        const uint32_t covered=g.hiveCombat.spawn(hive::Type::Archon,hidden);
        auto* a=g.hiveCombat.find(covered);const float hull=a->hull,shield=a->shield;
        check(hiveWorldSweep(blast,hidden,0)<=1,"building blocks blast line of sight");
        g.detonate(blast,false,hive::BombSet::EMP);g.detonate(blast,false,hive::BombSet::Penetrator);
        check(a->shield==shield && a->hull==hull,"building still shields enemies from EMP and explosive damage");
        g.clearPracticeEncounter();g.refreshGroundPits();
        int hitKind=0; Ent hitEntity;
        const vec3 direction=normalize(end-from);
        const float wallDistance=g_scenery.raycast(from,direction,length(end-from),&hitKind,&hitEntity);
        check(wallDistance>=0,"wall-contact regression finds exact authored scenery surface");
        if(wallDistance>=0) {
          const vec3 wall=from+direction*wallDistance, query=wall-direction*.15f;
          const uint32_t front=g.hiveCombat.spawn(hive::Type::Needle,wall-direction*15.f+vec3(0,0,12));
          const uint32_t back=g.hiveCombat.spawn(hive::Type::Archon,center+vec3(span,0,0));
          const float protectedHull=g.hiveCombat.find(back)->hull, protectedShield=g.hiveCombat.find(back)->shield;
          g.detonate(wall,false,hive::BombSet::Plasma,&query);
          check(g.hiveCombat.find(front)->hull<hive::roleSpec(hive::Type::Needle).hull,"wall contact nudges blast query outside so near-side enemy takes damage");
          check(g.hiveCombat.find(back)->hull==protectedHull && g.hiveCombat.find(back)->shield==protectedShield,"wall still shields far-side enemy after origin clearance");
          check(!g.wraith.blasts.empty() && length(g.wraith.blasts.back().p-wall)<.001f,"wall detonation visual remains at exact contact");
        }

      }
      g.clearPracticeEncounter();g.refreshGroundPits();
      const vec3 sea(49000,0,49000);const uint32_t exposed=g.hiveCombat.spawn(hive::Type::Archon,sea+vec3(20,25,0));
      auto* a=g.hiveCombat.find(exposed);const float hull=a->hull;
      g.detonate(sea,true,hive::BombSet::EMP);
      check(a->shield==0 && a->hull==hull,"surface EMP reaches exposed enemy from clearance-adjusted query origin");
      g.detonate(sea,true,hive::BombSet::Plasma);
      check(a->hull<hull && !g.wraith.blasts.empty() && g.wraith.blasts.back().p.y==0,"surface blast damages visible enemy while visual remains exactly on water");
      g.endFlight(false,"Bomb tests finished",OUT_ABANDONED);
    }

    // Research free-roam practice is entirely isolated, including live equipment swaps, damage and repeated reset.
    {
      auto q=make(); Game& g=*q; g.career.job->state=Career::JobState::ACTIVE; g.career.attemptOpen=true;
      g.resCraft=kWraith; g.resCard=-1; g.resAirborne=true; g.resAirport=g.career.location;
      const std::string before=snapshot(g.career); g.launchResearch();
      check(g.researchFlight && !g.militaryFlight && g.screen==SCR_FLIGHT,"research practice launches outside military career");
      g.spawnPracticeWave(); g.spawnPracticeWave(); g.spawnPracticeWave();
      check(g.hiveCombat.aliveCount()==hive::MaxActors,"repeated wave requests remain bounded at eight actors");
      g.practiceForward=hive::ForwardSet::Kinetic; g.practiceBomb=hive::BombSet::EMP;
      g.wraith.bombQueue=3; g.wraith.wantFire=true; g.wraith.bombs.push_back({g.plane.pos,vec3(),0});
      g.militaryHull=12; g.clearPracticeEncounter();
      check(g.hiveCombat.aliveCount()==0 && g.hiveCombat.mission.status==hive::MissionStatus::Inactive && g.wraith.bombs.empty() && !g.wraith.wantFire && g.wraith.bombQueue==0 && g.wraith.fireLatch,"equipment swap clears encounter, ordnance and held controls");
      check(g.combatLoadout.forward==hive::ForwardSet::Kinetic && g.combatLoadout.bomb==hive::BombSet::EMP && g.combatLoadout.rounds==hive::forwardSpec(hive::ForwardSet::Kinetic).rounds && g.militaryHull==100,"equipment swap refills selected stores and practice hull");
      g.plane.pos=vec3(0,12000,0); g.wraith.armed=true; g.wraith.lasers=1; g.wraith.wantFire=true;
      const float fullPayload=g.plane.payload;
      const float firingMass=g.plane.mass(); const vec3 velocityBefore=g.plane.vel;
      const int rounds=g.combatLoadout.rounds; g.updateWraith(dt);
      check(std::fabs(length(g.plane.vel-velocityBefore)*firingMass-hive::forwardSpec(hive::ForwardSet::Kinetic).recoilImpulseNs)<.1f,"gun recoil uses impulse divided by actual aircraft mass");
      check(near(g.plane.payload,fullPayload-hive::forwardSpec(hive::ForwardSet::Kinetic).roundMassKg),"firing reduces physical ammunition mass");
      check(g.combatLoadout.rounds==rounds-1 && !g.wraith.bolts.empty() && g.wraith.bolts.back().kind==hive::ForwardSet::Kinetic,"selected forward weapon fires through Game equipment and projectile path");
      const float beforeDrop=g.plane.payload;
      g.wraith.bay=1; g.wraith.bombLoaded=1; g.wraith.bombQueue=1; const int stores=g.combatLoadout.bombs; g.updateWraith(dt);
      check(near(g.plane.payload,beforeDrop-hive::bombSpec(hive::BombSet::EMP).storeMassKg),"released bomb leaves aircraft payload mass");
      check(g.combatLoadout.bombs==stores-1 && !g.wraith.bombs.empty() && g.wraith.bombs.back().kind==hive::BombSet::EMP,"selected payload consumes a finite physical store");
      FrameParams payloadVisual; payloadVisual.plane.on=true; g.wraithVisual(payloadVisual);
      if(payloadVisual.fx.bombs>0) {
        const float* rotation=payloadVisual.fx.bombRot[0];
        const vec3 forward(-rotation[6],-rotation[7],-rotation[8]);
        check(dot(forward,normalize(g.wraith.bombs[0].v))>.999f && payloadVisual.plane.wrBombSet==(int)hive::BombSet::EMP,"released rigid payload aligns to velocity and carried model uses selected type");
      } else check(false,"released payload receives a visual instance");

      const uint32_t id=g.hiveCombat.spawn(hive::Type::Archon,g.plane.pos+vec3(0,0,-200));
      auto* target=g.hiveCombat.find(id); const float hull=target->hull;
      g.detonate(target->position,false,hive::BombSet::EMP);
      check(target->shield==0 && target->relayInterrupted>0 && target->hull==hull,"EMP detonation routes through shield/relay disruption without hull damage");
      g.clearPracticeEncounter(); g.clearPracticeEncounter();
      check(near(g.plane.payload,85+g.combatLoadout.massKg()) && near(g.plane.payload,fullPayload),"repeated rearm replaces equipment mass instead of accumulating it");
      g.restartFlight();
      check(g.researchFlight && !g.militaryFlight && g.combatLoadout.forward==hive::ForwardSet::Kinetic && g.hiveCombat.aliveCount()==0,"research restart retains selected equipment but clears encounter");
      g.endFlight(false,"Practice finished",OUT_ABANDONED);
      check(g.screen==SCR_RESEARCH && snapshot(g.career)==before,"practice launch/fire/bomb/reset/restart/exit preserve exact career bytes");
      check(g.career.military.completedMissions==0 && !g.career.militaryMissionUnlocked(military::RouteRecon),
        "research practice cannot grant service completion or reveal a later military assignment");
      g.launchResearch(); g.screen=SCR_LOADING; g.in.pressed[K_ESC]=true; g.updateLoading(dt); g.in.endFrame();
      check(g.screen==SCR_RESEARCH && snapshot(g.career)==before,"research loading cancellation does not close civilian active attempt");
      g.resCraft=kMantis; g.launchResearch(); g.spawnPracticeWave();
      check(g.hiveCombat.aliveCount()==0,"practice encounter spawning is restricted to XR-40");
      g.endFlight(false,"",OUT_ABANDONED);
      g.resCraft=kWraith; g.resCard=-1; g.launchResearch(); g.resCard=0; g.spawnPracticeWave();
      check(g.hiveCombat.aliveCount()==0,"test-card sorties cannot spawn free-roam practice waves");
      g.endFlight(false,"",OUT_ABANDONED);
      check(snapshot(g.career)==before,"rejected practice operations cannot advance or settle career");
    }
    // Military widgets run their real focus, activation and navigation code at three screen sizes. This checks
    // CPU layout/input behavior; software-rendered screenshots separately verify font metrics and final pixels.
    {
      const int oldW=g_ren.W,oldH=g_ren.H;
      for(const auto size:std::array<std::pair<int,int>,3>{{{1920,1080},{1280,720},{960,540}}}) {
        g_ren.W=size.first;g_ren.H=size.second;
        auto q=make(military::RouteRecon);Game& g=*q;g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
        g.militarySelection=military::LocalSurvey;g.in.mx=g.in.my=-100;g.showRadio=true;
        const std::string before=snapshot(g.career);
        auto frame=[&]() {
          g.gamepadMenus(dt);g.focusNavigate();g_ren.uiBegin();
          if(g.screen==SCR_HUB)g.drawHub();
          else if(g.screen==SCR_DEBRIEF)g.drawDebrief();
          else if(g.screen==SCR_FLIGHT && g.paused)g.drawPause();
          g.in.endFrame();
        };
        auto focused=[&](uint32_t id) {
          return std::find_if(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==id;});
        };
        frame();check(!g.showRadio,"military terminal clears hidden civilian radio overlay before interaction");
        g.focusNav=true;g.focusId=0x6D1000u;frame();
        bool keyboard=true,controller=true,inBounds=true;
        for(int row=0;row<military::kBoardMissionCount;++row) {
          if(row) {g.in.pressed[K_DOWN]=true;frame();}
          keyboard&=g.focusId==0x6D1000u+row;
          g.in.pressed[K_ENTER]=true;frame();
          keyboard&=g.militarySelection==military::kBoardMissionIds[row] && g.screen==SCR_HUB;
          const auto f=focused(0x6D1000u+row);const auto L=g.militaryLayout();
          inBounds&=f!=g.focusList.end() && f->x>=0 && f->y>=L.top && f->x+f->w<=g_ren.W && f->y+f->h<=L.bot;
        }
        check(keyboard && inBounds,"keyboard walks and selects all nine military rows with focused row kept visible at "+std::to_string(size.first));
        for(int row=military::kBoardMissionCount-2;row>=0;--row) {
          g.in.pad=true;g.in.buttonsPressed=PAD_UP;frame();controller&=g.focusId==0x6D1000u+row;
          g.in.buttonsPressed=PAD_A;frame();controller&=g.militarySelection==military::kBoardMissionIds[row] && g.screen==SCR_HUB;
        }
        check(controller,"controller D-pad and A select the full military register in reverse at "+std::to_string(size.first));
        bool unique=true;std::set<uint32_t> ids;
        for(const auto& f:g.focusList)unique&=ids.insert(f.id).second;
        check(unique && focused(0x6D0001u)!=g.focusList.end() && focused(0x6D0002u)!=g.focusList.end(),
          "military board focus IDs are unique and expose civilian return plus deploy at "+std::to_string(size.first));
        const auto row=focused(0x6D1001u);
        if(row!=g.focusList.end()) {
          g.in.pad=false;g.focusNav=false;g.in.mx=row->x+row->w*.5f;g.in.my=row->y+row->h*.5f;g.in.mPressed[0]=true;frame();
          check(g.militarySelection==military::RouteRecon && g.screen==SCR_HUB,"mouse mission selection changes assignment without deploying");
        } else check(false,"second military row available for mouse-selection regression");
        g.in.mx=g.in.my=-100;
        check(snapshot(g.career)==before,"military menu traversal and preview leave all career bytes unchanged");
        g.pendingCareer=g.career;g.saveWhy="test pending save";frame();
        check(focused(0x6D0002u)==g.focusList.end() && focused(0x6D0003u)!=g.focusList.end(),
          "pending save removes Deploy from keyboard/controller focus and provides Retry save");
        g.pendingCareer.reset();g.saveWhy.clear();frame();
        g.focusNav=true;g.focusId=0x6D0002u;g.in.pressed[K_ENTER]=true;frame();
        check(g.screen==SCR_FLIGHT && g.militaryFlight && g.specIdx==kMantis && !g.wraith.wantFire,
          "keyboard deploy uses selected mission and does not carry its Enter into weapons");
        const uint32_t token=g.career.military.activeAttempt;g.paused=true;frame();
        bool pauseBounds=true;for(const auto& f:g.focusList)pauseBounds&=f.x>=0 && f.y>=0 && f.x+f.w<=g_ren.W && f.y+f.h<=g_ren.H;
        check(pauseBounds && focused(0x6D3002u)!=g.focusList.end() && focused(0x6D3007u)!=g.focusList.end(),
          "military pause controls fit and expose restart plus abort at "+std::to_string(size.first));
        g.focusNav=true;g.focusId=0x6D3002u;g.in.pressed[K_ENTER]=true;frame();
        check(g.militaryFlight && g.screen==SCR_FLIGHT && !g.paused && g.career.military.activeAttempt==token,
          "keyboard pause Restart keeps the same military attempt and mission");
        g.paused=true;frame();g.focusNav=true;g.focusId=0x6D3007u;g.in.pad=true;g.in.buttonsPressed=PAD_A;frame();
        check(g.screen==SCR_DEBRIEF && !g.lastSuccess && g.career.military.failures==1,
          "controller pause Abort settles once into the military debrief");
        frame();g.focusNav=true;g.focusId=0x6D2002u;g.in.buttonsPressed=PAD_A;frame();
        check(g.screen==SCR_FLIGHT && g.militaryFlight && g.career.military.activeAttempt>token && g.specIdx==kMantis,
          "controller A on debrief Retry launches only the selected military retry");
        g.endFlight(false,"Back-focus regression",OUT_ABANDONED);frame();
        const uint32_t settledAttempt=g.career.attempt;
        g.focusNav=true;g.focusId=0x6D2002u;g.in.buttonsPressed=PAD_B;frame();
        check(g.screen==SCR_HUB && g.hubList==2 && !g.militaryFlight && g.career.attempt==settledAttempt,
          "controller B while Retry is focused returns without synthetic Enter launching another sortie");
        frame();g.in.buttonsPressed=PAD_B;frame();
        check(g.screen==SCR_HUB && g.hubList==0,"controller Back from military board returns to civilian work in one step");
        g.hubList=2;frame();g.in.pad=false;g.in.pressed[K_ESC]=true;frame();
        check(g.screen==SCR_HUB && g.hubList==0,"keyboard Escape from military board returns to civilian work in one step");
      }
      g_ren.W=oldW;g_ren.H=oldH;
    }
    // Explicit pagers and briefing controls are also reachable without a mouse at the smallest review size.
    // Their boundary state is observable through enabled focus entries, and list movement through row geometry.
    {
      const int oldW=g_ren.W,oldH=g_ren.H;g_ren.W=960;g_ren.H=540;
      auto q=make(military::CommandStrike);Game& g=*q;g.screen=SCR_HUB;g.hubTab=TAB_CONTRACTS;g.hubList=2;
      g.militarySelection=military::LocalSurvey;g.in.mx=g.in.my=-100;
      const std::string before=snapshot(g.career);
      auto frame=[&]() {g.gamepadMenus(dt);g.focusNavigate();g_ren.uiBegin();g.drawHub();g.in.endFrame();};
      auto enabled=[&](uint32_t id) {return std::any_of(g.focusList.begin(),g.focusList.end(),[&](const Game::Focusable& f){return f.id==id;});};
      auto firstVisible=[&]() {
        const auto L=g.militaryLayout();const float scale=std::min(g.S(),std::min(g_ren.H/720.f,g_ren.W/1120.f));
        int first=military::kBoardMissionCount;
        for(const auto& f:g.focusList)if(f.id>=0x6D1000u && f.id<0x6D1000u+military::kBoardMissionCount &&
          f.y>=L.top+40*scale && f.y+f.h<=L.bot-44*scale)first=std::min(first,int(f.id-0x6D1000u));
        return first;
      };
      auto activate=[&](uint32_t id,bool pad) {
        g.focusNav=true;g.focusId=id;g.in.pad=pad;g.padActive=pad;
        if(pad)g.in.buttonsPressed=PAD_A;else g.in.pressed[K_ENTER]=true;
        frame();frame(); // controls update the next frame's rows / enabled boundary state
      };
      frame();frame();
      for(bool pad:{true,false}) {
        const std::string device=pad?"controller A":"keyboard Enter";
        check(firstVisible()==0 && !enabled(0x6D0010u) && enabled(0x6D0011u),device+" pager begins at first visible mission with Back disabled");
        bool moved=true;int forward=0;
        while(enabled(0x6D0011u) && forward<military::kBoardMissionCount) {
          const int prior=firstVisible();activate(0x6D0011u,pad);++forward;
          moved&=firstVisible()==prior+1 && g.militarySelection==military::LocalSurvey && g.screen==SCR_HUB;
        }
        check(moved && forward>0 && !enabled(0x6D0011u) && enabled(0x6D0010u),device+" advances visible mission pages to the disabled forward boundary without deploying");
        int backward=0;
        while(enabled(0x6D0010u) && backward<military::kBoardMissionCount) {
          const int prior=firstVisible();activate(0x6D0010u,pad);++backward;moved&=firstVisible()==prior-1;
        }
        check(moved && backward==forward && firstVisible()==0 && !enabled(0x6D0010u),device+" reverses mission pages to the first-row boundary");
      }
      for(bool pad:{true,false}) {
        const std::string device=pad?"controller A":"keyboard Enter";
        g.militarySelection=pad?military::HoverScan:military::HeavyDefense;g.focusNav=false;frame();frame();
        check(!enabled(0x6D0041u) && enabled(0x6D0042u),device+" briefing begins at top with Read up disabled");
        int down=0;
        while(enabled(0x6D0042u) && down<30){activate(0x6D0042u,pad);++down;}
        check(down>0 && down<30 && enabled(0x6D0041u) && !enabled(0x6D0042u) && g.screen==SCR_HUB,
          device+" reads the whole long briefing until Read down disables at the bottom");
        int up=0;
        while(enabled(0x6D0041u) && up<30){activate(0x6D0041u,pad);++up;}
        check(up>0 && up<30 && !enabled(0x6D0041u) && enabled(0x6D0042u),
          device+" returns briefing to the top and restores Read down");
      }
      // The renderer has no text-capture hook. Pair executable binding-resolution assertions with a source
      // contract that the rendered briefing uses those live labels for every weapon/nozzle/lift instruction.
      const std::string source=bytes(fs::path(__FILE__).parent_path().parent_path()/"src/game_military_ui.cpp");
      bool liveLabels=source.find("FLIGHT CONTROLS")!=std::string::npos;
      const int actions[]={ACT_WEAPONS,ACT_FIRE,ACT_BOMB,ACT_FLAPS_DN,ACT_FLAPS_UP,ACT_THR_UP};
      const char* names[]={"ACT_WEAPONS","ACT_FIRE","ACT_BOMB","ACT_FLAPS_DN","ACT_FLAPS_UP","ACT_THR_UP"};
      const char keys[]={'J','K','L','U','I','O'};
      const unsigned pads[]={PAD_X,PAD_Y,PAD_RB,PAD_LEFT,PAD_RIGHT,PAD_A};
      const char* expectedPads[]={"X","Y","RB","D-PAD LEFT","D-PAD RIGHT","A"};
      for(int i=0;i<6;++i) {
        g.set.keyBind[actions[i]]=keys[i];g.set.padBind[actions[i]]=pads[i];
        liveLabels&=source.find(std::string("actLabel(")+names[i]+",pad)")!=std::string::npos &&
          g.actLabel(actions[i],false)==std::string(1,keys[i]) && g.actLabel(actions[i],true)==expectedPads[i];
      }
      check(liveLabels,"briefing weapon, store, nozzle and lift text uses current remapped keyboard/controller labels");
      g.militarySelection=military::HoverScan;g.in.pad=true;g.padActive=true;frame();
      g.in.pad=false;g.padActive=false;frame();
      check(snapshot(g.career)==before,"pagination, whole-brief reading and remapped control previews preserve career bytes");
      g_ren.W=oldW;g_ren.H=oldH;
    }
    // Exercise the complete research terminal, not an isolated equipment panel: the rail must not
    // steal the aircraft preview, and controls retain their identity while their content scrolls.
    {
      const int oldW=g_ren.W,oldH=g_ren.H;
      struct ReviewSize {int w,h;float scale;};
      const ReviewSize sizes[]={{1920,1080,1.f},{1280,720,1.4f},{960,540,1.f},{800,600,1.4f}};
      const uint32_t kitIds[]={0x7E5F0010u,0x7E5F0011u,0x7E5F0012u};
      for(const auto size:sizes) {
        g_ren.W=size.w;g_ren.H=size.h;
        auto q=make();Game& g=*q;g.set.uiScale=size.scale;g.debugScene("research40");
        g.uiDt=dt;g.researchView.idle=0;g.focusScreen=SCR_RESEARCH*4;g.in.mx=g.in.my=-100;
        const std::string label=" at "+std::to_string(size.w)+"x"+std::to_string(size.h)+"/"+std::to_string(int(size.scale*100))+"%";
        const std::string before=snapshot(g.career);
        auto frame=[&]() {
          g.gamepadMenus(dt);g.focusNavigate();g_ren.uiBegin();FrameParams fp{};
          g.researchPreviewCamera(fp);g.drawResearch(fp);g.in.endFrame();
        };
        auto control=[&](uint32_t id) {
          for(const auto& f:g.focusList)if(f.id==id)return f;
          return Game::Focusable{};
        };
        auto values=[&]() {return std::array<int,3>{{int(g.practiceForward),int(g.practiceBomb),g.practiceWave}};};
        auto previewMatches=[&]() {
          FrameParams fp{};g.researchPreviewCamera(fp);
          return fp.plane.model==kWraith && fp.plane.wrBombSet==int(g.practiceBomb) &&
            near(fp.plane.wr[6][3],float(g.practiceForward)) && fp.plane.wr[6][0]>0 &&
            fp.fx.beams==0 && fp.fx.bombs==0 && g.hiveCombat.aliveCount()==0;
        };
        frame();frame();
        const auto L=g.researchLayout();const float s=g.S(),top=L.top+36*s,bottom=L.bot-24*s;
        auto visible=[&](const Game::Focusable& f) {
          return f.id && f.x>=L.rx && f.x+f.w<=L.rx+L.rw && f.y>=top-.01f && f.y+f.h<=bottom+.01f;
        };
        bool bounds=true;
        for(uint32_t id:kitIds){const auto f=control(id);bounds&=visible(f) && f.x>L.px1 && f.w>0 && f.h>0;}
        check(bounds && !g.hitClipOn,"all three loadout controls fit the separate right rail and restore hit clipping"+label);
        check(previewMatches(),"research preview shows selected carried equipment without firing or spawning"+label);

        for(int device=0;device<3;++device)for(int selected=0;selected<3;++selected) {
          bool cycled=true;const int count=selected==2?4:3;const auto initial=values();
          g.in.pad=device==2;g.padActive=device==2;
          for(int n=0;n<count;++n) {
            g.focusNav=true;g.focusId=kitIds[selected];frame();frame();
            const auto prior=values();const auto f=control(kitIds[selected]);const float yaw=g.researchView.yaw;
            if(device==0) {
              g.focusNav=false;g.in.mx=f.x+f.w*.5f;g.in.my=f.y+f.h*.5f;
              g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=7;g.in.mdy=3;
            } else if(device==1)g.in.pressed[K_ENTER]=true;
            else g.in.buttonsPressed=PAD_A;
            frame();const auto now=values();
            cycled&=now[selected]==(prior[selected]+1)%count && g.screen==SCR_RESEARCH && !g.researchView.drag && near(g.researchView.yaw,yaw);
            for(int k=0;k<3;++k)if(k!=selected)cycled&=now[k]==prior[k];
            if(device)cycled&=g.focusNav && g.focusId==kitIds[selected];
            cycled&=control(kitIds[selected]).id==kitIds[selected] && previewMatches();
            g.in.mDown[0]=false;frame();
          }
          check(cycled && values()==initial,std::string(device==0?"mouse":device==1?"keyboard Enter":"controller A")+
            " wraps selector "+std::to_string(selected)+" with stable identity, exact equipment and no preview clickthrough"+label);
        }
        for(bool pad:{false,true}) {
          g.in.pad=pad;g.padActive=pad;g.focusNav=true;g.focusId=kitIds[0];frame();
          bool reached=true;
          for(int k=1;k<3;++k){if(pad)g.in.buttonsPressed=PAD_DOWN;else g.in.pressed[K_DOWN]=true;frame();reached&=g.focusId==kitIds[k];}
          for(int k=1;k>=0;--k){if(pad)g.in.buttonsPressed=PAD_UP;else g.in.pressed[K_UP]=true;frame();reached&=g.focusId==kitIds[k];}
          check(reached,std::string(pad?"D-pad":"keyboard")+" traverses all three rail selectors in both directions"+label);
        }
        const uint32_t viewIds[]={0x7E5F0030u,0x7E5F0031u,0x7E5F0032u,0x7E5F0033u,0x7E5F0034u};
        const float stageBot=L.bot-56*s;
        g.in.pad=false;g.padActive=false;g.focusNav=false;g.in.mx=g.in.my=-100;
        std::set<uint32_t> walkedDown,walkedUp;
        const int stops=int(g.focusList.size());
        for(int n=0;n<stops+1;++n){g.in.pressed[K_DOWN]=true;frame();frame();walkedDown.insert(g.focusId);}
        for(int n=0;n<stops+1;++n){g.in.pressed[K_UP]=true;frame();frame();walkedUp.insert(g.focusId);}
        bool walked=true;for(uint32_t id:kitIds)walked&=walkedDown.count(id) && walkedUp.count(id);
        for(uint32_t id:viewIds)walked&=walkedDown.count(id) && walkedUp.count(id);
        std::vector<uint32_t> selectionIds{0x7E5D0000u};
        for(int i=0;i<4;++i)selectionIds.push_back(0x7E5C0000u+uint32_t(i));
        for(int i=0;i<Game::kNumResCards;++i)if(Game::kResCards[i].craft==kWraith)selectionIds.push_back(0x7E5D0000u+uint32_t(i+1));
        for(uint32_t id:selectionIds)walked&=walkedDown.count(id) && walkedUp.count(id);
        check(walked,"keyboard reaches every airframe, test card, equipment and preview control from unfocused terminal and back"+label);
        // Explore real D-pad transitions only from already reached states. This catches isolated focus islands
        // that tests setting each control ID directly would otherwise miss.
        g.in.pad=true;g.padActive=true;g.focusNav=false;g.in.buttonsPressed=PAD_DOWN;frame();
        std::vector<uint32_t> frontier{g.focusId};std::set<uint32_t> reached{g.focusId};
        for(size_t node=0;node<frontier.size() && node<100;++node)for(unsigned direction:{PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT}) {
          g.focusNav=true;g.focusId=frontier[node];frame();frame();
          g.in.buttonsPressed=direction;frame();frame();
          if(reached.insert(g.focusId).second)frontier.push_back(g.focusId);
        }
        bool connected=frontier.size()<100;for(uint32_t id:kitIds)connected&=reached.count(id)!=0;
        for(uint32_t id:viewIds)connected&=reached.count(id)!=0;
        for(uint32_t id:selectionIds)connected&=reached.count(id)!=0;
        check(connected,"D-pad focus graph connects initial control to all airframes, test cards, equipment and preview actions"+label);
        g.in.pad=false;g.padActive=false;g.focusNav=false;g.in.mx=g.in.my=-100;
        for(int n=0;n<stops+1 && (!g.focusNav || g.focusId!=viewIds[0]);++n){g.in.pressed[K_DOWN]=true;frame();}
        bool row=g.focusId==viewIds[0];const int rowAirport=g.resAirport;
        for(int i=1;i<5;++i){g.in.pressed[K_RIGHT]=true;frame();row&=g.focusId==viewIds[i] && g.resAirport==rowAirport;}
        for(int i=3;i>=0;--i){g.in.pressed[K_LEFT]=true;frame();row&=g.focusId==viewIds[i] && g.resAirport==rowAirport;}
        check(row,"keyboard enters preview strip naturally and walks both directions without changing insertion site"+label);
        bool separate=true;
        for(int i=0;i<5;++i) {
          const auto f=control(viewIds[i]);
          separate&=f.id==viewIds[i] && f.x>=L.px0 && f.x+f.w<=L.px1+.01f &&
            f.y>=stageBot && f.y+f.h<=L.bot && f.w>0 && f.h>0;
          if(i) {const auto prev=control(viewIds[i-1]);separate&=prev.x+prev.w<f.x;}
        }
        check(separate,"five preview controls occupy their separate non-overlapping strip above sortie parameters"+label);
        const auto beforeView=values();const int viewAirport=g.resAirport;const float viewTime=g.resTime;
        for(int device=0;device<3;++device) {
          bool actions=true;
          for(int i=0;i<5;++i) {
            g.in.pad=device==2;g.padActive=device==2;g.focusNav=true;g.focusId=viewIds[i];frame();
            g.researchView.yaw=1.1f;g.researchView.pitch=.25f;g.researchView.zoom=1.2f;g.researchView.idle=1.f;
            if(device==0) {
              const auto f=control(viewIds[i]);g.focusNav=false;
              g.in.mx=f.x+f.w*.5f;g.in.my=f.y+f.h*.5f;
              g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=11;g.in.mdy=5;
            } else if(device==1)g.in.pressed[K_ENTER]=true;
            else g.in.buttonsPressed=PAD_A;
            frame();
            const float expectedYaw=i==0?1.1f-15*DEG:i==1?1.1f+15*DEG:i==4?.7f:1.1f;
            const float expectedZoom=i==2?1.1f:i==3?1.3f:i==4?1.f:1.2f;
            actions&=near(g.researchView.yaw,expectedYaw) && near(g.researchView.zoom,expectedZoom) && near(g.researchView.pitch,i==4?.15f:.25f) &&
              near(g.researchView.idle,0) && !g.researchView.drag && g.screen==SCR_RESEARCH && values()==beforeView &&
              g.resAirport==viewAirport && near(g.resTime,viewTime);
            if(device)actions&=g.focusNav && g.focusId==viewIds[i];
            g.in.mDown[0]=false;frame();
          }
          check(actions,std::string(device==0?"mouse":device==1?"keyboard Enter":"controller A")+
            " rotates both ways, zooms both ways and resets preview without drag, launch or loadout changes"+label);
        }
        g.in.pad=false;g.padActive=false;
        for(int i:{2,3}) {
          g.focusNav=true;g.focusId=viewIds[i];
          for(int n=0;n<24;++n){g.in.pressed[K_ENTER]=true;frame();}
          check(near(g.researchView.zoom,i==2?.75f:1.35f),std::string(i==2?"zoom-out":"zoom-in")+" clamps repeated preview clicks at its limit"+label);
        }
        g.focusNav=true;g.focusId=viewIds[4];g.in.pressed[K_ENTER]=true;frame();
        g.in.pad=false;g.padActive=false;g.focusNav=false;g.researchView.idle=0;
        const auto kit=values();const float yaw=g.researchView.yaw,pitch=g.researchView.pitch;
        g.in.mx=(L.px0+L.px1)*.5f;g.in.my=stageBot-20*s;
        g.in.mPressed[0]=g.in.mDown[0]=true;g.in.mdx=25;g.in.mdy=5;frame();
        check(g.researchView.drag && near(g.researchView.yaw,yaw+.2f) && near(g.researchView.pitch,pitch+.03f) && values()==kit,
          "lower preview stage accepts the complete drag gesture after equipment moves out"+label);
        g.in.mDown[0]=false;frame();const float zoom=g.researchView.zoom;
        g.in.wheel=-1;frame();
        check(!g.researchView.drag && near(g.researchView.zoom,zoom-.1f) && values()==kit,"preview wheel zoom retains its full lower-stage target"+label);

        const float railYaw=g.researchView.yaw,railZoom=g.researchView.zoom;
        g.in.mx=L.rx+L.rw*.5f;g.in.my=(top+bottom)*.5f;g.in.wheel=1000;frame();frame();
        g.in.wheel=-.25f;frame();const auto partial=control(kitIds[0]);
        g.in.mx=partial.x+partial.w*.5f;g.in.my=top+2*s;g.in.mPressed[0]=g.in.mDown[0]=true;frame();g.in.mDown[0]=false;
        check(partial.y<top && partial.y+partial.h>top && values()==kit && !g.researchView.drag,
          "partially clipped selector rejects clicks even on its remaining visible pixels"+label);
        g.in.mx=L.rx+L.rw*.5f;g.in.my=(top+bottom)*.5f;g.in.wheel=-1000;frame();frame();
        const auto hidden=control(kitIds[0]);
        check(hidden.id && !visible(hidden) && near(g.researchView.zoom,railZoom) && near(g.researchView.yaw,railYaw) && values()==kit,
          "scrolling full specifications clips equipment without changing loadout or preview"+label);
        g.in.mx=hidden.x+hidden.w*.5f;g.in.my=hidden.y+hidden.h*.5f;g.in.mPressed[0]=g.in.mDown[0]=true;
        frame();g.in.mDown[0]=false;
        check(values()==kit && !g.researchView.drag,"clipped selector is mouse-inert outside the rail viewport"+label);
        g.in.mx=g.in.my=-100;g.focusNav=true;g.focusId=kitIds[0];g.in.pressed[K_ENTER]=true;frame();
        check(values()==kit,"offscreen focused selector defers activation until its row is visible"+label);
        frame();frame();
        check(visible(control(kitIds[0])) && g.focusId==kitIds[0],"focus scroll restores the hidden selector without losing its ID"+label);
        g.in.pressed[K_ENTER]=true;frame();
        check(int(g.practiceForward)==(kit[0]+1)%3 && g.screen==SCR_RESEARCH,"restored focused selector activates without launching"+label);

        // Controller scroll follows the focused rail scrollbar, not a stale mouse location over the aircraft.
        g.focusNav=false;g.in.mx=L.rx+L.rw*.5f;g.in.my=(top+bottom)*.5f;g.in.wheel=1000;frame();frame();
        const float firstY=control(kitIds[0]).y,stickZoom=g.researchView.zoom;const auto beforeStick=values();
        g.in.pad=true;g.padActive=true;g.focusNav=true;g.focusId=0x7E5F0020u;g.in.mx=L.cx;g.in.my=L.cy;
        g.in.ry=-1;for(int n=0;n<30;++n)frame();g.in.ry=0;
        check(control(kitIds[0]).y<firstY && near(g.researchView.zoom,stickZoom) && values()==beforeStick,
          "right stick scrolls focused specification rail without changing preview zoom or kit"+label);

        g.in.pad=false;g.padActive=false;g.focusNav=false;g.in.mx=g.in.my=-100;
        g.practiceForward=hive::ForwardSet::Kinetic;g.practiceBomb=hive::BombSet::EMP;g.practiceWave=3;
        const auto chosen=values();bool preserved=true;
        for(int n=0;n<4;++n) {
          g.in.pressed[K_TAB]=true;frame();frame();FrameParams fp{};g.researchPreviewCamera(fp);
          preserved&=values()==chosen && fp.plane.model==g.resCraft &&
            (g.resCraft==kWraith ? visible(control(kitIds[0])) : !control(kitIds[0]).id);
          for(uint32_t id:viewIds)preserved&=control(id).id==id;
          g.researchView.yaw=.7f;g.researchView.zoom=1;g.researchView.idle=0;g.focusNav=true;g.focusId=viewIds[1];g.in.pressed[K_ENTER]=true;frame();
          preserved&=near(g.researchView.yaw,.7f+15*DEG);
          g.focusId=viewIds[3];g.in.pressed[K_ENTER]=true;frame();preserved&=near(g.researchView.zoom,1.1f);
          g.focusId=viewIds[4];g.in.pressed[K_ENTER]=true;frame();
          preserved&=near(g.researchView.yaw,.7f) && near(g.researchView.pitch,.15f) && near(g.researchView.zoom,1) && values()==chosen;
        }
        for(int card=0;card<Game::kNumResCards;++card)if(Game::kResCards[card].craft==kWraith) {
          const uint32_t cardId=0x7E5D0000u+uint32_t(card+1);
          preserved&=control(cardId).id==cardId;
          g.focusNav=true;g.focusId=cardId;g.in.pressed[K_ENTER]=true;
          frame();frame();
          preserved&=g.resCard==card && !control(kitIds[0]).id && values()==chosen;
          preserved&=control(0x7E5D0000u).id==0x7E5D0000u;
          g.focusNav=true;g.focusId=0x7E5D0000u;g.in.pressed[K_ENTER]=true;
          frame();frame();
          preserved&=g.resCard==-1 && visible(control(kitIds[0])) && values()==chosen && previewMatches();
        }
        check(preserved,"airframe and every XR-40 test-card transition preserve kit, restore rail and select actual preview model"+label);
        const int airport=g.resAirport;g.focusNav=true;g.focusId=kitIds[1];g.in.pressed[K_RIGHT]=true;frame();
        check(g.resAirport==(airport+1)%int(g_world.airports.size()) && values()==chosen,
          "keyboard Right still changes insertion site while an equipment selector is focused"+label);
        g.focusNav=false;g.in.mx=L.rx+L.rw*.5f;g.in.my=(top+bottom)*.5f;g.in.wheel=-1000;frame();frame();
        g.in.pressed[K_ESC]=true;frame();const bool exited=g.screen==SCR_MENU;
        g.screen=SCR_RESEARCH;g.realTime=41;g.resOpened=g.resSeqOpened=31;g.resSeq=10;g.resSeqAt=g.realTime;
        g.focusNav=false;g.in.mx=g.in.my=-100;frame();frame();
        check(exited && visible(control(kitIds[0])) && values()==chosen,
          "reopening research resets its rail scroll while preserving selected equipment"+label);
        check(snapshot(g.career)==before,"complete research rail interaction matrix leaves all career bytes unchanged"+label);
      }
      g_ren.W=oldW;g_ren.H=oldH;
    }
    // Render CPU-side UI geometry only: exercise the real widget/input paths without a GL context.
    {
      const int oldW=g_ren.W,oldH=g_ren.H;g_ren.W=1920;g_ren.H=1080;
      auto q=make();Game& g=*q;g.screen=SCR_RESEARCH;g.resCraft=kWraith;g.resCard=-1;
      g.resOpened=g.resSeqOpened=0;g.resSeq=10;g.resSeqAt=g.realTime;g.resAuthed=true;
      g.focusScreen=SCR_RESEARCH*4;g.in.mx=g.in.my=-100;
      const std::string before=snapshot(g.career);
      auto panel=[&]() { g.drawCombatPractice(600,400,600); };
      panel();std::vector<uint32_t> ids;for(const auto& f:g.focusList) ids.push_back(f.id);
      check(ids.size()==3,"research equipment exposes three focusable selectors");
      g.in.pressed[K_DOWN]=true;g.focusNavigate();g.in.endFrame();panel();
      check(g.focusNav && g.focusId==ids[0],"keyboard Down reaches research equipment without stealing site arrows");
      for(int control=0;control<3;++control) {
        bool stable=true;
        for(int cycle=0;cycle<4;++cycle) {
          g.focusNav=true;g.focusId=ids[control];g.focusList.clear();g.in.pressed[K_ENTER]=true;panel();
          g.in.endFrame();g.focusNavigate();panel();stable&=g.focusId==ids[control] && g.focusList[control].id==ids[control];
        }
        check(stable,"repeated keyboard selector cycles retain stable focus "+std::to_string(control));
      }
      const auto gun=g.practiceForward;
      g.in.pad=true;g.focusId=ids[0];g.focusNav=true;g.in.buttonsPressed=PAD_A;
      g.gamepadMenus(dt);g.focusList.clear();panel();g.in.endFrame();g.focusNavigate();panel();
      check(g.practiceForward!=gun && g.focusId==ids[0],"controller A cycles equipment and retains focus");
      const auto bomb=g.practiceBomb;const auto payloadButton=g.focusList[1];
      g.focusNav=false;g.in.mx=payloadButton.x+10;g.in.my=payloadButton.y+10;g.in.mPressed[0]=true;
      g.focusList.clear();panel();g.in.endFrame();
      check(g.practiceBomb!=bomb,"mouse click cycles only selected payload control");
      g.in.mx=g.in.my=-100;g.in.pressed[K_RIGHT]=true;const int airport=g.resAirport;
      FrameParams fp;g.drawResearch(fp);g.in.endFrame();
      check(g.resAirport==(airport+1)%int(g_world.airports.size()),"research Right retains launch-site selection");
      const auto launchGun=g.practiceForward;const auto launchBomb=g.practiceBomb;const int launchWave=g.practiceWave;
      g.focusNav=true;g.focusId=ids[0];g.in.buttonsPressed=PAD_START;g.gamepadMenus(dt);g.drawResearch(fp);g.in.endFrame();
      check(g.researchFlight && g.screen==SCR_FLIGHT && g.practiceForward==launchGun && g.practiceBomb==launchBomb && g.practiceWave==launchWave,
        "controller Start launches while focused without also changing equipment");
      g.paused=true;g.focusList.clear();g.drawPause();
      bool bounds=true;for(const auto& f:g.focusList) bounds&=f.x>=0 && f.y>=0 && f.x+f.w<=1920 && f.y+f.h<=1080;
      check(bounds && g.focusList.size()>=12,"1080p practice pause controls fit the viewport without scrolling");
      g.in.buttonsPressed=PAD_B;g.update(0);g.in.endFrame();
      check(!g.paused && g.screen==SCR_FLIGHT,"controller Back resumes from practice pause");
      g.endFlight(false,"",OUT_ABANDONED);g.resSeqOpened=g.resOpened;g.resSeq=10;
      g.in.buttonsPressed=PAD_B;g.gamepadMenus(dt);g.drawResearch(fp);g.in.endFrame();
      check(g.screen==SCR_MENU && snapshot(g.career)==before,"controller Back cancels terminal and all equipment/input actions preserve career");
      g_ren.W=oldW;g_ren.H=oldH;
    }
    std::error_code ec; fs::remove_all(root,ec);
    printf("Military gameplay: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
  }
};
int main() { return GameTest::run(); }
