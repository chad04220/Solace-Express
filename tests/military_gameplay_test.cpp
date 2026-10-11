// Real Game-layer military/practice regression suite. No window, renderer initialization or GPU bakes.
// Positions and input are scripted, but mission transitions, damage, launch/settlement and resets run production code.
#include "../src/game.h"
#include "test_world.h"
#include "../src/hive_world_collision.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iterator>

struct GameTest {
  static int run() {
    namespace fs = std::filesystem;
    buildTestWorld(); buildStory(); g_audio.init(48000);
    const fs::path root=fs::temp_directory_path() / ("solace_military_game_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    auto make=[&]() {
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

    // Every loaner launches for an unlicensed, penniless pilot without adding it to the owned/research roster.
    for(int kind=0;kind<3;++kind) {
      auto q=make(); Game& g=*q;
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
      const std::string settled=snapshot(g.career); g.endFlight(true,"Duplicate",OUT_SUCCESS);
      check(snapshot(g.career)==settled && snapshot(g.career,true)==before,"repeated completion cannot double-pay or mutate civilian state");
    }

    // Defense: actual scheduled wave spawning/deaths, protected asset survival, extraction and failure.
    {
      auto q=make(); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(1);
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
      auto q=make(); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(2);
      auto& m=g.hiveCombat.mission;
      check(m.config.targetCount==2 && m.config.targetIds[0] && m.config.targetIds[1],"strike records two designated stable IDs");
      g.plane.pos=vec3(0,18000,0); g.plane.vel=vec3(); killAll(g); g.updateHive(dt);
      check(m.targetsDestroyed[0] && m.targetsDestroyed[1] && m.status==hive::MissionStatus::Extract && g.screen==SCR_FLIGHT,"strike target destruction gates extraction instead of immediate payout");
      g.plane.pos=m.config.extraction; g.updateHive(dt);
      check(g.lastSuccess && g.career.military.credits==1800 && snapshot(g.career,true)==before,"strike extraction completes separate service settlement");
    }

    // Pause/time acceleration, real hit-to-crash path, abort and loading/menu cancellation.
    {
      auto q=make(); Game& g=*q; const std::string before=snapshot(g.career,true); g.launchMilitary(1);
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

    // Render packets cannot silently drop live ordnance, and collision follows the banked aircraft shape.
    {
      auto q=make(); Game& g=*q; g.launchMilitary(1); killAll(g);
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
      auto q=make(); Game& g=*q; const fs::path blocked=root/"not_a_directory"; { std::ofstream f(blocked); f<<"block"; }
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
      auto q=make();Game& g=*q;g.launchMilitary(2);g.plane.pos=vec3(0,12000,-1000);
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
