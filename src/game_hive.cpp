// Somnium Volandi - the service board's sorties and the hive encounter
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include "airport_layout.h"
#include "entities.h"
#include "entity_lod.h"
#include "hive_world_collision.h"
#include "scenery.h"
#include "regional_surface.h"
#include "models.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "military_missions.h"
#include "game_internal.h"

// Military sorties are career-backed loaners; research practice uses the same combat engine off the books.
void Game::launchMilitary(int kind) {
  if (launchCardPending() || !military::mission(kind) || !career.atHome() || g_world.procedural) return;   // (Defense Command is the islands')
  Contract c = career.militaryContract(kind);
  if (career.canFly(c, c.forceAircraft) != Career::SRC_MILITARY) return;
  if (commitBlocked()) { hubMsg = "Your last result isn't saved yet: retry the save first."; hubMsgTime = 5; return; }
  if (career.attemptOpen || career.military.activeAttempt) return;
  // Headless simulations deliberately bypass presentation. Interactive launches defer even the save:
  // slow storage, encounter setup and the first renderer bake must all happen after a visible card.
  if (headless) { beginMilitaryLaunch(c); return; }
  pendingMilitaryLaunch = c;
  militaryLaunchFrameDrawn = militaryLaunchFramePresented = false;
  loading.reset(g_ren.bakeCount);
  screen = SCR_LOADING;
  in.pressed[K_ENTER] = in.pressed[' '] = false; in.mPressed[0] = false;
  in.buttonsPressed &= ~(PAD_A | PAD_START);
}

bool Game::beginMilitaryLaunch(const Contract& c) {
  if (career.canFly(c, c.forceAircraft) != Career::SRC_MILITARY) return false;
  bool begun=false;
  if (!commitLaunch([&](Career& k) { begun=k.beginMilitary(c); }) || !begun) return false;
  const auto p = career.plan(c, c.forceAircraft, Career::SRC_MILITARY);
  startFlight(c, c.forceAircraft, Career::SRC_MILITARY, &p);
  return militaryFlight && (screen == SCR_LOADING || screen == SCR_FLIGHT);
}

hive::WorldCallbacks Game::hiveWorld() {
  hive::WorldCallbacks w;
  w.terrainHeight = [](void*, float x, float z) { return std::max(0.f, pitGround(x,z,g_world.height(x,z))); };
  w.sweepFraction = [](void*, const vec3& a, const vec3& b, float radius) { return hiveWorldSweep(a,b,radius); };
  w.sweepIncludesTerrain = true;
  return w;
}

void Game::setupMilitaryEncounter() {
  hiveCombat.reset(career.military.missionSeed, combatWrap());
  militaryHull = 100; militarySettled = false;
  const auto* mission = military::missionForContract(contract.type);
  if (!mission) return; // Career launch validates the catalogue before committing the attempt.
  for(auto& label:militarySiteLabels) label.clear();
  combatLoadout.reset(mission->forward, mission->bomb);
  // Separate insertion and objective areas. Mission positions remain canonical over world seams.
  vec3 origin=plane.pos; origin.y=std::max(origin.y,g_world.height(origin.x,origin.z)+650.f);
  if(mission->id==military::HighAltitudeIntel) origin.y=4500.f;
  if(mission->id==military::StratosphericRecon) origin.y=6500.f;
  if(mission->id==military::HypersonicRecon) origin.y=9000.f;
  camPos+=origin-plane.pos; // Keep the flight camera with the supplied insertion altitude.
  plane.pos=origin;
  hive::MissionConfig m; m.extraction=origin; m.extractionRadius=300;
  vec3 f=plane.forward(); f.y=0; f=normalize(f+vec3(0,0,.0001f));
  vec3 right=cross(f,vec3(0,1,0));
  auto point=[&](float distance,float side,float altitude) { vec3 p=origin+f*distance+right*side; p.x=g_world.wrap(p.x); p.z=g_world.wrap(p.z); p.y=std::max(altitude,g_world.height(p.x,p.z)+250.f); return p; };
  // Select the exact same authored airport entities the scenery generator renders and collides with.
  // Sensor aim points sit just above each roof, never inside a building or at an arbitrary airborne marker.
  auto site=[&](int airport,int preferred,int index) {
    std::vector<AptItem> items; airportItems(airport,items);
    const AptItem* picked=nullptr;
    const int priorities[]={preferred,EK_CTRL_TOWER,EK_FBO,EK_ARCH_HANGAR,EK_HANGAR,EK_T_HANGAR};
    for(int kind:priorities) {
      for(const auto& item:items) if(item.kind==kind) {
        const auto& e=item.e; const auto& info=kEntInfo[item.kind];
        const vec3 roof(e.x,e.y+info.h*e.sy+2.f,e.z);
        bool used=false;
        for(int j=0;j<index;++j) used|=length(hiveCombat.displacement(roof,m.objectives[j].position))<1.f;
        if(!used) { picked=&item;break; }
      }
      if(picked) break;
    }
    hive::Objective o; o.id=100+index;
    if(picked) {
      const auto& e=picked->e; const auto& info=kEntInfo[picked->kind];
      o.position=vec3(e.x,e.y+info.h*e.sy+2.f,e.z);
      o.radius=clampf(std::max(info.hx*e.sx,info.hz*e.sz),8.f,35.f);
      militarySiteLabels[index]=std::string(g_world.airports[airport].code)+" "+info.name;
    } else {
      // Every authored airport has a hangar; keep a safe airport navigation point for custom worlds.
      const auto& a=g_world.airports[airport]; o.position=a.pos()+vec3(0,3,0);o.radius=10;
      militarySiteLabels[index]=std::string(a.code)+" operations site";
    }
    return o;
  };
  contract.brief=mission->brief;
  if (mission->objective==hive::MissionKind::Recon) {
    m.kind=hive::MissionKind::Recon; m.scanRange=mission->scanRange; m.scanSeconds=mission->scanSeconds;
    const bool local=mission->id==military::LocalSurvey || mission->id==military::HoverScan;
    const int wanted=mission->id==military::HoverScan?1:(mission->id==military::LocalSurvey || mission->id==military::HighAltitudeIntel)?2:3;
    std::vector<int> airports; for(int i=0;i<(int)g_world.airports.size();++i) if(!g_world.airports[i].water()) airports.push_back(i);   // (the land fields: a seaplane base has no tower or hangars)
    m.objectiveCount=local?wanted:std::min(wanted,(int)airports.size());
    std::stable_sort(airports.begin(),airports.end(),[&](int a,int b) { return length(hiveCombat.displacement(origin,g_world.airports[a].pos()))<length(hiveCombat.displacement(origin,g_world.airports[b].pos())); });
    const int preferred[]={EK_CTRL_TOWER,EK_HANGAR,EK_FUEL_TANK};
    for(int i=0;i<m.objectiveCount;++i) {
      m.objectives[i]=site(local?contract.from:airports[i],preferred[i],i);
      contract.brief+=" "+militarySiteLabels[i]+".";
    }
    // Sensors are the objective: unarmed XR sorties do not spawn unavoidable pursuing enemies.
    // The precision hover sortie carries defensive stores, but no combat is needed to complete it.
  } else if (mission->objective==hive::MissionKind::Defense) {
    const bool heavy=mission->id==military::HeavyDefense;
    m.kind=hive::MissionKind::Defense; m.objectiveCount=1;
    m.objectives[0]=site(contract.from,heavy?EK_HANGAR:EK_CTRL_TOWER,0); m.objectives[0].health=600;
    contract.brief+=" Protected site: "+militarySiteLabels[0]+".";
    m.waveCount=heavy?4:3;
    for(int i=0;i<m.waveCount;++i) {
      auto& w=m.waves[i]; w.at=i*(heavy?32.f:28.f);
      w.position=m.objectives[0].position+f*(2600.f+i*150.f)+right*((i-1)*500.f);
      w.position.x=g_world.wrap(w.position.x); w.position.z=g_world.wrap(w.position.z);
      w.position.y=std::max(m.objectives[0].position.y+650.f,g_world.height(w.position.x,w.position.z)+300.f);
      // Four bomber-led waves, bounded to the supplied 48 heavy rounds and six Plasma stores.
      // Solo bomber, fighter escort, relay escort, fighter escort: no stacked blast damage is required.
      w.count=heavy?(i==0?1:2):(i==2?3:2);
      w.types[0]=heavy?hive::Type::Bastion:hive::Type::Needle;
      w.types[1]=heavy?(i==2?hive::Type::Cantor:hive::Type::Needle):hive::Type::Bastion;
      w.types[2]=hive::Type::Cantor;
    }
  } else {
    m.kind=hive::MissionKind::Strike;
    if(mission->id==military::Interception) {
      m.targetCount=3;
      for(int i=0;i<3;++i) m.targetIds[i]=hiveCombat.spawn(hive::Type::Needle,point(2600.f+i*550.f,(i-1)*750.f,origin.y+float(i)*140.f));
    } else if(mission->id==military::RelaySuppression) {
      m.targetCount=3;
      for(int i=0;i<3;++i) m.targetIds[i]=hiveCombat.spawn(hive::Type::Cantor,point(4000.f+i*200.f,(i-1)*550.f,origin.y+120.f));
      for(int i=0;i<2;++i) hiveCombat.spawn(hive::Type::Bastion,point(3400.f,(i?1.f:-1.f)*650.f,origin.y));
    } else {
      m.targetCount=2;
      m.targetIds[0]=hiveCombat.spawn(hive::Type::Archon,point(4300,0,origin.y+150));
      m.targetIds[1]=hiveCombat.spawn(hive::Type::Cantor,point(4000,500,origin.y+200));
      hiveCombat.spawn(hive::Type::Needle,point(3500,-450,origin.y));
    }
  }
  hiveCombat.startMission(m);
  syncCombatPayload();
  toast("MILITARY LOANER / return to extraction after objectives",vec3(.35f,.85f,1));
  if(m.objectiveCount>0) toast((m.kind==hive::MissionKind::Recon?"FIRST SCAN: ":"PROTECT: ")+militarySiteLabels[0],vec3(.35f,.85f,1));
}
void Game::clearPracticeEncounter() {
  if (!researchFlight || resCard>=0 || specIdx!=kSomnus) return;
  hiveCombat.reset(1, combatWrap()); militaryHull=100;
  combatCollisionWarm=false; combatCollisionWarmMs=0; combatPrefetchCursor=0;
  // A range reset clears ordnance and damage together; it cannot touch the career.
  somnus=SomnusState(); g_scenery.resetDamage();
  combatLoadout.reset(practiceForward,practiceBomb);
  syncCombatPayload();
}
void Game::spawnPracticeWave() {
  if (!researchFlight || resCard>=0 || specIdx!=kSomnus || crashed) return;
  hive::MissionConfig m; m.kind=hive::MissionKind::Practice; hiveCombat.startMission(m);
  for(int i=0;i<4 && hiveCombat.aliveCount()<hive::MaxActors;++i) {
    vec3 p=plane.pos+plane.forward()*(1700.f+i*240.f)+plane.right()*((i-1.5f)*260.f);
    p.y=std::max(p.y,g_world.height(p.x,p.z)+300.f);
    hiveCombat.spawn((hive::Type)((practiceWave+i)%4),p);
  }
  // The range control is an explicit setup action; prepare its new corridors before resuming combat.
  combatCollisionWarm=false; warmCombatCollision();
  toast("Practice wave spawned (8 craft maximum)",vec3(.7f,.6f,1));
}
std::string Game::hiveObjectiveText() const {
  const auto& m=hiveCombat.mission;
  if (m.status==hive::MissionStatus::Extract) return fmt("RETURN TO EXTRACTION  %.0f m",length(hiveCombat.displacement(plane.pos,m.config.extraction)));
  if(m.config.kind==hive::MissionKind::Recon) {
    int n=0; float scan=0; float nearest=1e9f; int target=0;
    for(int i=0;i<m.config.objectiveCount;++i) { const auto& o=m.config.objectives[i]; n+=o.scanned; if(!o.scanned) { float d=length(hiveCombat.displacement(plane.pos,o.position)); if(d<nearest) { nearest=d; target=i+1; scan=o.scan; } } }
    const auto* spec=military::missionForContract(contract.type);
    // Mandatory sensor conditions lead the HUD line: long site names are ellipsized by the flight UI.
    if(spec && military::groundSpeedSortie(spec->id)) {
      if(plane.pos.y<spec->minAltitude) return fmt("SCAN PAUSED / CLIMB TO %.0f m MSL",spec->minAltitude);
      if(plane.pos.y>spec->maxAltitude) return fmt("SCAN PAUSED / DESCEND BELOW %.0f m MSL",spec->maxAltitude);
      const float speed=sqrtf(plane.vel.x*plane.vel.x+plane.vel.z*plane.vel.z);
      if(speed<spec->minSpeed) return fmt("SCAN PAUSED / SPEED %.0f kt / NEED %.0f+",speed*MS_TO_KT,spec->minSpeed*MS_TO_KT);
    }
    if(spec && spec->id==military::HoverScan) {
      const float speed=length(plane.vel), above=plane.pos.y-m.config.objectives[0].position.y;
      if(speed>spec->maxSpeed) return fmt("SCAN PAUSED / SLOW FROM %.0f TO 23 kt",speed*MS_TO_KT);
      if(above<spec->minAltitude) return "SCAN PAUSED / CLIMB 30-120 m ABOVE ROOF";
      if(above>spec->maxAltitude) return "SCAN PAUSED / DESCEND 30-120 m ABOVE ROOF";
    }
    return fmt("SCAN %d/%d %.0f%% / %s %.0f m",n,m.config.objectiveCount,100*scan/std::max(.1f,m.config.scanSeconds),target>0?militarySiteLabels[target-1].c_str():"EXTRACTION",nearest);
  }
  if(m.config.kind==hive::MissionKind::Defense) return fmt("DEFEND %s %.0f%% / WAVE %d/%d / HOSTILES %d",militarySiteLabels[0].c_str(),100*m.config.objectives[0].health/600.f,m.nextWave,m.config.waveCount,hiveCombat.aliveCount());
  if(m.config.kind==hive::MissionKind::Strike) {
    int destroyed=0; for(int i=0;i<m.config.targetCount;++i) destroyed+=m.targetsDestroyed[i];
    const auto* spec=military::missionForContract(contract.type);
    const char* role=spec && spec->id==military::Interception?"INTERCEPT NEEDLES":spec && spec->id==military::RelaySuppression?"STRIKE CANTOR RELAYS":"STRIKE HIVE COMMAND";
    return fmt("%s / TARGETS %d/%d / HOSTILES %d / HULL %.0f%%",role,destroyed,m.config.targetCount,hiveCombat.aliveCount(),militaryHull);
  }
  return fmt("PRACTICE / HOSTILES %d / HULL %.0f%% / PAUSE FOR RANGE CONTROLS",hiveCombat.aliveCount(),militaryHull);
}
void Game::syncCombatPayload() {
  if(specIdx!=kSomnus || !(militaryFlight || (researchFlight && resCard<0))) return;
  // A fixed non-equipment baseline prevents repeated swaps/rearms from accumulating mass.
  plane.payload=std::max(0.f,combatBasePayload)+combatLoadout.massKg();
}
void Game::warmCombatCollision() {
  if(combatCollisionWarm || !(militaryFlight || (researchFlight && resCard<0 && specIdx==kSomnus))) return;
  const auto start=std::chrono::steady_clock::now();
  // Query the production collision path without stepping Combat or touching mission clocks.
  // Radius and nearest-image coordinates match later actor/weapon queries.
  auto corridor=[&](vec3 a,vec3 b,float radius) { hiveWorldSweep(a,a+hiveCombat.displacement(a,b),radius); };
  corridor(plane.pos,plane.pos+plane.forward()*500.f,12.f);
  const auto& m=hiveCombat.mission.config;
  for(const auto& a:hiveCombat.actors) if(a.alive) {
    corridor(a.position,plane.pos,0);
    corridor(a.position,a.position+a.velocity*2.f,hive::roleSpec(a.type).radius);
  }
  for(int i=0;i<m.objectiveCount;++i) {
    const vec3 p=m.objectives[i].position;
    corridor(p+vec3(0,1500,0),p,1.f);
    corridor(p+vec3(-500,350,0),p+vec3(500,350,0),20.f);
  }
  for(int i=0;i<m.waveCount;++i) {
    const auto& w=m.waves[i];
    const vec3 target=m.objectiveCount?m.objectives[0].position:plane.pos;
    corridor(w.position,target,0);
    corridor(w.position,plane.pos,0);
  }
  combatCollisionWarmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  combatCollisionWarm=true;
}
void Game::prefetchCombatCollision() {
  // Use the supported worker queue (the renderer's main-thread pump installs results).
  // Queue at most ONE missing level-2 chunk per update, or generate one when no workers exist.
  // A cache miss in an authoritative sweep still generates synchronously.
  constexpr int slots=hive::MaxActors+1, neighborhoods=9, total=slots*neighborhoods;
  for(int n=0;n<total;++n) {
    const int item=combatPrefetchCursor++%total, slot=item%slots, offset=item/slots;
    vec3 p;
    if(slot==hive::MaxActors) p=plane.pos+plane.vel*2.f;
    else { const auto& a=hiveCombat.actors[slot]; if(!a.alive) continue; p=a.position+a.velocity*2.f; }
    p.x=g_world.wrap(p.x+(offset%3-1)*Scenery::CH); p.z=g_world.wrap(p.z+(offset/3-1)*Scenery::CH);
    const int cx=Scenery::chunkOf(p.x),cz=Scenery::chunkOf(p.z);
    const auto* chunk=g_scenery.get(cx,cz);
    if(!chunk || chunk->level<2) {
      if(g_scenery.workers()>0) g_scenery.request(cx,cz,2);
      else g_scenery.ensure(cx,cz,2);
      break;
    }
  }
  combatPrefetchCursor%=total;
}
void Game::updateHive(float dt) {
  if(!militaryFlight && !(researchFlight && resCard<0 && specIdx==kSomnus)) return;
  if(dt>0 && !paused) prefetchCombatCollision();
  hive::PlayerSnapshot p; p.position=plane.pos; p.velocity=plane.vel; p.forward=plane.forward(); p.alive=!crashed && !plane.ev.crashed; p.scanning=false;
  if(militaryFlight) {
    const auto* mission=military::missionForContract(contract.type);
    const float roof=hiveCombat.mission.config.objectiveCount?hiveCombat.mission.config.objectives[0].position.y:0.f;
    const float speed=mission && military::groundSpeedSortie(mission->id)?sqrtf(plane.vel.x*plane.vel.x+plane.vel.z*plane.vel.z):length(plane.vel);
    p.scanning=mission && military::sensorEnvelope(*mission,plane.pos.y,speed,roof);
  }
  p.up=plane.up();
  auto box=[&](vec3 center,vec3 halfExtent) {
    if(p.bodyBoxCount<hive::MaxPlayerBoxes) p.bodyBoxes[p.bodyBoxCount++]={center,halfExtent};
  };
  if(specIdx==kSomnus) {
    // Coarse local collision volumes follow wrSection, diamond-wing panels and the four articulated pods.
    // Unlike the old 12 m sphere, empty air above/below the thin wings is not a hit.
    box(vec3(0,-.10f,-6.3f),vec3(.65f,.38f,2.1f));
    box(vec3(0,-.06f,.05f),vec3(1.31f,.60f,4.45f));
    box(vec3(0,-.12f,6.15f),vec3(1.10f,.38f,1.65f));
    for(int side:{-1,1}) {
      box(vec3(side*2.15f,-.24f,.1f),vec3(.95f,.14f,3.3f));
      box(vec3(side*4.65f,-.24f,2.0f),vec3(1.55f,.12f,1.3f));
    }
    for(int i=0;i<4;++i) {
      const float tilt=plane.podTilt[i];
      box(kSomnusPods[i],vec3(.55f,.55f*std::fabs(cosf(tilt))+1.52f*std::fabs(sinf(tilt)),1.52f*std::fabs(cosf(tilt))+.55f*std::fabs(sinf(tilt))));
    }
    box(vec3(0,1.0f,6.1f),vec3(3.0f,1.0f,1.1f));
  } else if(validAircraft(specIdx)) {
    const auto& m=kModels[specIdx];
    for(int i=0;i<7;++i) {
      const float* a=m.st[i]; const float* b=m.st[i+1];
      box(vec3(0,(a[3]+b[3])*.5f,(a[0]+b[0])*.5f),vec3(std::max(a[1],b[1]),std::max(a[2],b[2])+std::fabs(a[3]-b[3])*.5f,std::fabs(b[0]-a[0])*.5f));
    }
    auto surface=[&](const float* wing) {
      const float chord=(wing[1]+wing[2])*.5f;
      for(int side:{-1,1}) box(vec3(side*wing[0]*.5f,wing[4]+sinf(wing[6]*DEG)*wing[0]*.5f,wing[5]+wing[3]*.5f+chord*.5f),vec3(wing[0]*.5f,.12f,chord*.5f));
    };
    surface(m.wing); surface(m.ht);
    box(vec3(0,m.vt[4]+m.vt[0]*.5f,m.vt[5]+m.vt[1]*.5f),vec3(.12f,m.vt[0]*.5f,m.vt[1]*.5f));
  }
  hiveCombat.step(dt,p,hiveWorld(),paused);
  for(int i=0;i<hiveCombat.eventCount;++i) {
    const auto& e=hiveCombat.events[i];
    vec3 at=plane.pos+hiveCombat.displacement(plane.pos,e.position);
    if(e.type==hive::EventType::PlayerDamage && !crashed) {
      militaryHull=std::max(0.f,militaryHull-e.value);
      plane.w+=vec3(.015f,.009f,.02f)*std::min(e.value,30.f);
      g_audio.triggerAt(SFX_IMPACT,at,clampf(e.value*.025f,.12f,.65f));
      if(militaryHull<=0) { plane.ev.crashed=true; plane.ev.crashReason="Hull lost to Hive fire"; }
    }
    if(e.type==hive::EventType::Death || e.type==hive::EventType::Hit || e.type==hive::EventType::Fire) {
      int n=e.type==hive::EventType::Death?28:e.type==hive::EventType::Hit?8:3;
      for(int j=0;j<n;++j) spawn(at,vec3(sparkRng.range(-15,15),sparkRng.range(-4,18),sparkRng.range(-15,15)),.25f+sparkRng.uni()*.5f,.2f,.3f,vec3(1,.55f,.24f)*2,1,SPR_SPARK,.6f,-1);
      if(e.type==hive::EventType::Death) { ++somnus.kills; g_audio.triggerAt(SFX_CRASH,at,.45f); for(int j=0;j<8;++j) spawn(at,vec3(0,5+j,0),2.f,3.f,4.f,vec3(.12f),.6f,SPR_SMOKE,1,0); }
    }
    if(e.type==hive::EventType::Fire) {
      const auto* actor = hiveCombat.find(e.source);
      g_audio.triggerAt(SFX_CANNON,at,.32f,actor?actor->velocity:vec3());
    }
    if(e.type==hive::EventType::Hit) g_audio.triggerAt(SFX_METAL,at,.3f);
    if(e.type==hive::EventType::Telegraph && e.target==0 && realTime-audioThreatAt>9.f) {
      audioThreatAt=realTime; toast("INCOMING FIRE - BREAK AWAY",vec3(1,.45f,.2f));
    }
    if(e.type==hive::EventType::ObjectiveDamage && realTime-audioObjectiveAt>12.f) {
      audioObjectiveAt=realTime; toast("PROTECTED SITE UNDER ATTACK",vec3(1,.55f,.2f));
    }
    if(e.type==hive::EventType::Wave && e.source==0) toast("HOSTILE WAVE INBOUND",vec3(1,.55f,.2f));
    if(e.type==hive::EventType::ScanComplete) toast("Intelligence site recorded",vec3(.3f,1,.7f));
  }
  hiveCombat.clearEvents();
  if (hiveCombat.mission.status == hive::MissionStatus::Active) audioExtractSaid = false;
  if (hiveCombat.mission.status == hive::MissionStatus::Extract && !audioExtractSaid) {
    audioExtractSaid = true; toast("OBJECTIVES COMPLETE - RETURN TO EXTRACTION",vec3(.3f,1,.7f));
  }
  if(militaryFlight && !militarySettled && !crashed && !plane.ev.crashed) {
    if(hiveCombat.mission.status==hive::MissionStatus::Success) endFlight(true,"Military objectives secured");
    else if(hiveCombat.mission.status==hive::MissionStatus::Failed) endFlight(false,"Military objective lost",OUT_ABANDONED);
  }
}
