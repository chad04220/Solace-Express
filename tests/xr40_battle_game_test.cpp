#include "game.h"
#include "test_world.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
static int checks=0;
static void check(bool ok,const char* what){++checks;if(!ok){fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
struct GameTest {
  static void run(const std::filesystem::path& path){
    auto make=[&]{auto g=std::make_unique<Game>();g->battleOnly=true;g->diskless=true;g->initHeadless();g->initBattle();g->botControl=true;return g;};
    auto g=make();check(g->screen==SCR_MENU && g->resCraft==kSomnus,"dedicated hangar starts without career gate");
    const auto warm=Game::prewarmItems(true,true);check(warm.size()==2 && warm[0].first==kSomnus && warm[1].first==kSomnus && warm[1].second,"only XR-40 exterior and cockpit prewarm");
    int items=0;for(const auto& share:Renderer::afShareItems(4,kSomnus))for(int item:share){int model,kind;Renderer::afItemOf(item,model,kind);check(model==kSomnus,"startup shader work excludes other aircraft");++items;}
    check(items==4,"four XR-40 shader compilation work items");
    for(int mode=0;mode<4;++mode)for(int theater=0;theater<3;++theater){
      g->battleConfig.mode=xr40::Mode(mode);g->battleTheater=theater;
      g->practiceForward=hive::ForwardSet::Heavy;g->practiceBomb=hive::BombSet::EMP;
      g->launchBattle();
      check(g->screen==SCR_FLIGHT && g->specIdx==kSomnus && g->researchFlight && !g->militaryFlight,"all modes launch original XR-40 directly");
      check(!g->plane.onGround && g->plane.agl()>=749 && g->plane.gear==0,"insertion is airborne, clear of terrain, gear retracted");
      check(!g->career.attemptOpen && !g->career.military.activeAttempt && !g->pendingCareer,"battle cannot open a career attempt");
      check(g->combatLoadout.forward==hive::ForwardSet::Heavy && g->combatLoadout.bomb==hive::BombSet::EMP,"chosen hardware reaches actual weapon state");
      check(std::fabs(g->plane.payload-85-g->combatLoadout.massKg())<.1f,"equipment mass applied to real flight model");
      const int ap=g->resAirport;check(ap==g_world.findAirport(theater==0?"LHK":theater==1?"SMP":"CAP"),"theater resolves its authored airport");
      g->paused=true;float t=g->battle.stats.elapsed;g->update(.1f);check(g->battle.stats.elapsed==t,"pause freezes sortie and combat clocks");
      g->paused=false;g->updateHive(1.f/60);check(g->hiveCombat.aliveCount()>=2,"real Game update spawns enemies");
      g->returnToBattle();check(g->screen==SCR_MENU && !g->researchFlight && !g->hiveCombat.aliveCount() && g->somnus.bolts.empty(),"hangar return clears combat and ordnance");
    }
    // Rendering and actual buffer presentation are separate gates, as in the main game.
    g->headless=false;g->launchBattle();check(g->pendingLaunch && g->screen==SCR_LOADING && !g->researchFlight,"deploy shows loading card before setup");
    g->updateLoading(.1f);check(g->pendingLaunch && g->battle.phase==xr40::Phase::Ready,"elapsed time cannot start unpresented launch");
    g->in.pressed[K_ESC]=true;g->updateLoading(.1f);check(!g->pendingLaunch && g->screen==SCR_MENU,"loading cancel returns to battle hangar");
    g->in.endFrame();g->headless=true;g->launchBattle();
    g->battle.phase=xr40::Phase::Victory;g->battle.stats.score=1400;g->finishBattle(true,"test recovery");
    const int sorties=g->battleRecord.sorties;g->finishBattle(true,"duplicate");
    check(g->screen==SCR_DEBRIEF && g->battleRecord.best==1400 && g->battleRecord.sorties==sorties,"victory records once");
    g->returnToBattle();g->launchBattle();g->battle.stats.score=9999;g->finishBattle(false,"aborted");
    check(g->battleRecord.best==1400 && g->battle.phase==xr40::Phase::Aborted,"aborting does not bank score");
    g->diskless=false;g->saveDir=path.string();g->battleConfig.seed=91;
    check(g->saveBattleRecord(),"record saves atomically");
    auto loaded=make();loaded->diskless=false;loaded->saveDir=path.string();loaded->initBattle();
    check(loaded->battleRecord.best==1400 && loaded->battleConfig.seed==91,"record and configuration round-trip");
    // Saving twice rotates a valid backup; truncated primary then recovers it.
    check(g->saveBattleRecord(),"record backup rotates");
    {std::ofstream f(path/"battle.cfg");f<<"XR40_BATTLE 1 bad";}
    loaded->initBattle();check(loaded->battleRecord.best==1400,"damaged primary recovers backup");
    g->saveDir=(path/"missing"/"nested").string();check(!g->saveBattleRecord() && !g->battleSaveError.empty(),"failed storage is reported without discarding in-memory result");
    g->saveDir=path.string();g->battleConfig.seed=92;g->shutdown();loaded->initBattle();
    check(loaded->battleConfig.seed==92,"closing the window persists hangar configuration");
    check(!std::filesystem::exists(path/"career.sav") && !std::filesystem::exists(path/"profile.cfg"),"standalone never writes civilian career/profile files");
    g->diskless=true;loaded->diskless=true;g_music.stop();
  }
};
int main(){
  setvbuf(stdout,nullptr,_IONBF,0);buildTestWorld();g_audio.init(48000);
  const auto path=std::filesystem::temp_directory_path()/"xr40-battle-integration";std::filesystem::create_directories(path);
  GameTest::run(path);std::filesystem::remove_all(path);
  printf("XR-40 game integration: %d checks passed\n",checks);
}
