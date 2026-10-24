// XR-40 Battle: the existing aircraft/renderer/weapons with an independent game loop.
#include "game.h"
#include "models.h"
#include "finite_float.h"
#include <filesystem>
#include <fstream>

std::vector<std::string> wrap(const std::string&,float,float);
namespace {
const vec3 ink(.018f,.027f,.043f), panelInk(.035f,.052f,.073f), ice(.51f,.84f,.88f), dim(.55f,.65f,.70f), white(.91f,.94f,.94f), amber(1.f,.69f,.31f), red(1.f,.36f,.27f);
float scale(float requested) { return std::min(requested,std::min(g_ren.W/1280.f,g_ren.H/800.f)); }
const char* theaters[]={"COAST / LIGHTHOUSE KEY","HIGHLANDS / SPINE PASS","CAPITAL / SOLACE"};
const char* codes[]={"LHK","SMP","CAP"};
const char* skies[]={"CLEAR / 14:00","DUSK / 18:30","STORM / 10:00"};
int field(int theater) { int i=g_world.findAirport(codes[std::clamp(theater,0,2)]);return i<0?0:i; }
void text(float x,float y,float size,const std::string& t,vec3 c=white,int align=0) { g_ren.text(x,y,size,t,c,1,align,false); }
void battlePanel(float x,float y,float w,float h,float s) {
  g_ren.rect(x,y,w,h,panelInk,.95f,3*s);g_ren.rectOutline(x,y,w,h,ice,.20f,3*s,s);
}
void bar(float x,float y,float w,float f,vec3 c,float s) {
  g_ren.rect(x,y,w,3*s,dim,.16f);g_ren.rect(x,y,w*clampf(f,0,1),3*s,c,.9f);
}
void paragraph(float x,float& y,float w,float s,const std::string& t,vec3 c=dim) {
  for(const auto& line:wrap(t,w,13*s)) {text(x,y,13*s,line,c);y+=19*s;}
}
std::string recordPath(const std::string& dir) { return (std::filesystem::path(dir)/"battle.cfg").string(); }
}

void Game::initBattle() {
  career=Career{};profile=Profile{};profile.welcomed=true;
  loadSettings();applyUiPalette();wantPacing=true;
  set.traffic=false;set.failures=false;failuresArmed=false;
  battleRecord={};battleConfig={};battleSaveError.clear();
  if(!diskless && !saveDir.empty()) {
    auto read=[&](const std::string& path) {
      std::ifstream f(path);std::string tag;int v=0,mode=0,diff=0,theater=0,sky=0,gun=0,bomb=0;unsigned seed=40;
      BattleRecord r;
      if(!(f>>tag>>v>>r.sorties>>r.wins>>r.best>>r.kills>>mode>>diff>>theater>>sky>>gun>>bomb>>seed) || tag!="XR40_BATTLE" || v!=1)return false;
      for(int n:{r.sorties,r.wins,r.best,r.kills})if(n<0 || n>100000000)return false;
      if(r.wins>r.sorties)return false;
      battleRecord=r;battleConfig.mode=xr40::Mode(std::clamp(mode,0,3));battleConfig.difficulty=xr40::Difficulty(std::clamp(diff,0,2));
      battleConfig.seed=seed;battleTheater=std::clamp(theater,0,2);battleWeather=std::clamp(sky,0,2);
      practiceForward=hive::ForwardSet(std::clamp(gun,0,2));practiceBomb=hive::BombSet(std::clamp(bomb,0,2));return true;
    };
    if(!read(recordPath(saveDir)))read(recordPath(saveDir)+".bak");
  }
  resCraft=kSomnus;resCard=-1;resAirport=field(battleTheater);resAirborne=true;
  somnus.bolts.reserve(96);somnus.bombs.reserve(24);somnus.blasts.reserve(32);
  particles.reserve(4096);camQ=quat();screen=SCR_MENU;
  atc.load(assetDir+"/voice");
  if(!diskless)g_music.start(float(g_audio.sampleRate));
}

bool Game::saveBattleRecord() {
  if(diskless || saveDir.empty())return true;
  namespace fs=std::filesystem;const std::string path=recordPath(saveDir),tmp=path+".tmp",bak=path+".bak";
  std::ofstream f(tmp,std::ios::trunc);
  f<<"XR40_BATTLE 1\n"<<battleRecord.sorties<<' '<<battleRecord.wins<<' '<<battleRecord.best<<' '<<battleRecord.kills<<'\n'
   <<int(battleConfig.mode)<<' '<<int(battleConfig.difficulty)<<' '<<battleTheater<<' '<<battleWeather<<' '
   <<int(practiceForward)<<' '<<int(practiceBomb)<<' '<<battleConfig.seed<<'\n';
  f.close();
  auto fail=[&]{battleSaveError="Record could not be saved. Check free space, then retry.";return false;};
  if(!f)return fail();
  std::error_code ec;const bool old=fs::exists(path,ec);if(ec)return fail();
  if(old) {fs::remove(bak,ec);ec.clear();fs::rename(path,bak,ec);if(ec)return fail();}
  fs::rename(tmp,path,ec);
  if(ec) { if(old) {std::error_code restore;fs::rename(bak,path,restore);}return fail(); }
  battleSaveError.clear();return true;
}

void Game::launchBattle() {
  if(!battleOnly || g_world.airports.empty())return;
  const xr40::Config cfg=battleConfig;
  resCraft=kSomnus;resCard=-1;resAirport=field(battleTheater);resAirborne=true;
  resWx=battleWeather==2?2:0;resTime=battleWeather==1?18.5f:battleWeather==2?10.f:14.f;
  Contract c=researchContract();c.id="XR40_BATTLE";c.title=xr40::modeName(cfg.mode);c.brief=xr40::briefing(cfg.mode);c.startAirborne=true;
  if(queueLaunch(c,kSomnus,LAUNCH_RESEARCH,[this]{launchBattle();}))return;
  Career::LaunchPlan plan;plan.spec=kSomnus;plan.src=Career::SRC_OWNED;plan.startAirport=resAirport;plan.fuelLoadKg=kAircraft[kSomnus].maxFuel;
  startFlight(c,kSomnus,Career::SRC_OWNED,&plan);
  researchFlight=isolatedFlight=true;resCard=-1;militaryFlight=false;failuresArmed=false;
  const auto& a=g_world.airports[resAirport];
  vec3 recovery=a.pos()+a.dir()*900.f;recovery.y=std::max(a.elev,g_world.height(recovery.x,recovery.z))+450.f;
  vec3 arena=recovery+a.dir()*4200.f;arena.y=std::max(recovery.y+350.f,g_world.height(arena.x,arena.z)+1100.f);
  const vec3 insertion=arena-a.dir()*1900.f;
  vec3 safe=insertion;safe.y=std::max(insertion.y,g_world.height(insertion.x,insertion.z)+750.f);
  plane.reset(&kAircraft[kSomnus],safe,a.heading,kAircraft[kSomnus].maxFuel,85,true,180.f);
  plane.ctl.throttle=.42f;plane.engineSpool=.42f;settleAirborneStart();
  camQ=plane.q;camPos=plane.pos+plane.q.rotate(vec3(0,4,26));camMode=0;hudOn=true;uiHidden=false;timeAccel=1;
  somnus=SomnusState{};somnus.armed=true;somnus.lasers=1;somnus.fireLatch=true;
  somnus.bolts.reserve(96);somnus.bombs.reserve(24);somnus.blasts.reserve(32);
  combatLoadout.reset(practiceForward,practiceBomb);combatBasePayload=85;syncCombatPayload();
  militaryHull=100;battle.begin(cfg,hiveCombat,arena,recovery,hiveWorld());
  militarySiteLabels[0]="RELAY";battleResultsSaved=false;showRadio=false;battleShowSettings=false;
  toasts.clear();atc.cancel();commsPending.clear();
  toast("XR-40 / "+std::string(xr40::modeName(cfg.mode)),ice,false);
  toast("Weapons ready. Fly to the marked contacts. Escape pauses.",white,false);
  saveBattleRecord();
  g_ren.dropHistory();
}

void Game::returnToBattle() {
  // Use the existing comprehensive flight teardown, with no career transaction.
  returnToMilitary();screen=SCR_MENU;researchFlight=false;settingsFromPause=false;battleShowSettings=false;
  battle=xr40::Battle{};photo.on=false;resCraft=kSomnus;resCard=-1;resAirport=field(battleTheater);
  saveBattleRecord();g_ren.dropHistory();
}
void Game::finishBattle(bool success,const std::string& why) {
  if(screen==SCR_DEBRIEF)return;
  if(battle.active())battle.finish(success?xr40::Phase::Victory:crashed?xr40::Phase::Defeat:xr40::Phase::Aborted,why);
  success=battle.phase==xr40::Phase::Victory && !crashed && militaryHull>0;
  lastSuccess=success;debriefTitle=success?"AIRCRAFT RECOVERED":battle.phase==xr40::Phase::Aborted?"SORTIE ABORTED":"SORTIE LOST";
  if(battle.reason.empty())battle.reason=why;
  result.success=success;result.flightMin=battle.stats.elapsed/60;result.fuelUsedKg=std::max(0.f,fuelStart-plane.fuel);
  if(!battleResultsSaved) {
    ++battleRecord.sorties;battleRecord.wins+=success;battleRecord.kills+=battle.stats.kills;
    if(success)battleRecord.best=std::max(battleRecord.best,battle.stats.score);
    battleResultsSaved=true;saveBattleRecord();
  }
  screen=SCR_DEBRIEF;paused=false;showMap=showRadio=false;debriefT0=realTime;
  atc.cancel();commsPending.clear();
  if(g_music.owns())g_music.sting(success?STING_SUCCESS:STING_FAIL);else g_audio.trigger(success?SFX_SUCCESS:SFX_FAIL);
}
void Game::updateBattle(float dt,const hive::PlayerSnapshot& player) {
  if(paused)return;
  const auto phase=battle.phase;const int wave=battle.wave;
  battle.update(dt,hiveCombat,player,militaryHull,hiveWorld());
  if(battle.wave!=wave)toast(fmt("WAVE %d / CONTACTS INBOUND",battle.wave),amber,false);
  if(phase!=battle.phase && battle.phase==xr40::Phase::Relief)toast("Wave clear. Recover supplies or prepare for the next group.",ice,false);
  if(phase!=battle.phase && battle.phase==xr40::Phase::Extraction)toast("Objectives secured. Fly through the extraction gate.",ice,false);
  if(battle.resupply(dt,plane.pos,length(plane.vel),somnus.wantFire || somnus.cloakOn || somnus.bombQueue>0 || combatLoadout.cooldown>0 || combatLoadout.bombCooldown>0)) {
    militaryHull=std::min(100.f,militaryHull+40.f);plane.fuel=plane.spec->maxFuel;
    combatLoadout.reset(practiceForward,practiceBomb);somnus.bombLoaded=1;syncCombatPayload();
    toast("SUPPLY LINK / AMMUNITION + FUEL / HULL +40 / SCORE -150",ice,false);
  }
  // Let the original collision/breakup sequence finish before the loss debrief.
  if(battle.phase==xr40::Phase::Victory)finishBattle(true,battle.reason);
  else if(battle.phase==xr40::Phase::Defeat && player.alive && !plane.ev.crashed && !crashed)finishBattle(false,battle.reason);
}

Game::ResLayout Game::battleLayout() const {
  const float s=scale(S()),W=float(g_ren.W),H=float(g_ren.H);
  ResLayout l{};l.lx=28*s;l.lw=278*s;l.rw=300*s;l.rx=W-28*s-l.rw;l.top=154*s;l.bot=H-112*s;
  l.px0=l.lx+l.lw+22*s;l.px1=l.rx-22*s;l.cx=(l.px0+l.px1)*.5f;l.cy=(l.top+l.bot)*.5f;l.r=100*s;return l;
}

void Game::drawBattleMenu() {
  const float s=scale(S()),W=float(g_ren.W),H=float(g_ren.H);const ResLayout l=battleLayout();
  showRadio=false;
  if(battleShowSettings) {
    g_ren.rect(0,0,W,H,ink,.98f);drawSettings(60*s,84*s,W-120*s,H-180*s);
    if(militaryButton(0xB410,28*s,24*s,180*s,38*s,"BACK TO HANGAR") || in.pressed[K_ESC]) {battleShowSettings=false;in.pressed[K_ESC]=false;saveSettings();}return;
  }
  g_ren.rect(0,0,W,130*s,ink,.95f);g_ren.rect(0,H-94*s,W,94*s,ink,.97f);
  g_ren.rect(28*s,26*s,38*s,3*s,ice,1);
  text(28*s,42*s,13*s,"SOMNIUM VOLANDI / INDEPENDENT COMBAT OPERATIONS",ice);
  text(26*s,67*s,46*s,"XR-40",white);text(203*s,81*s,28*s,"BATTLE SIMULATOR",white);
  text(W-28*s,47*s,11*s,"RECOVERED BEST",dim,2);text(W-28*s,65*s,28*s,std::to_string(battleRecord.best),ice,2);
  text(W-28*s,104*s,11*s,fmt("%d SORTIES  /  %d RECOVERED",battleRecord.sorties,battleRecord.wins),dim,2);
  battlePanel(l.lx,l.top,l.lw,l.bot-l.top,s);battlePanel(l.rx,l.top,l.rw,l.bot-l.top,s);
  const float x=l.lx+18*s,bw=l.lw-36*s;float y=l.top+19*s;
  text(x,y,11*s,"01 / OPERATION",ice);y+=29*s;
  for(int i=0;i<4;++i) {
    if(militaryButton(0xB420+i,x,y,bw,46*s,xr40::modeName(xr40::Mode(i)),true,int(battleConfig.mode)==i))battleConfig.mode=xr40::Mode(i);
    y+=55*s;
  }
  y+=11*s;text(x,y,11*s,"ENGAGEMENT RULES",ice);y+=27*s;
  paragraph(x,y,bw,s,xr40::briefing(battleConfig.mode));y+=12*s;
  paragraph(x,y,bw,s,"Sortie clock: 20 minutes (unlimited in Siege). Remain within 16 km of the operation center.",dim);
  // The clock is time; keep the label unambiguous even at small UI scales.
  const float cx=l.px0,cw=l.px1-l.px0;
  g_ren.rect(cx,l.top,cw,53*s,ink,.88f,3*s);
  text(cx+cw*.5f,l.top+9*s,12*s,"SOMNUS / FOUR-POD VECTOR FLIGHT",ice,1);
  text(cx+cw*.5f,l.top+31*s,11*s,"VTOL / STEALTH FIELD / MODULAR ORDNANCE",dim,1);
  updateMenuPreview(researchView,cx,l.top+60*s,cw,l.bot-l.top-152*s,0xB440);
  drawMenuPreviewControls(researchView,0xB440,cx,l.bot-80*s,cw,s,panelInk,ice,white,dim,3*s);
  g_ren.rect(cx,l.bot-24*s,cw,24*s,ink,.88f,3*s);
  text(cx+cw*.5f,l.bot-22*s,11*s,fmt("SEED %u / REPLAYABLE ENCOUNTERS",battleConfig.seed),dim,1);
  const float rx=l.rx+18*s,rw=l.rw-36*s;float ry=l.top+19*s;
  text(rx,ry,11*s,"02 / AIRFRAME CONFIGURATION",ice);ry+=29*s;
  auto select=[&](uint32_t id,const std::string& label,const std::string& value) {
    text(rx,ry,10*s,label,dim);ry+=19*s;bool clicked=militaryButton(id,rx,ry,rw,40*s,value);ry+=52*s;return clicked;
  };
  if(select(0xB450,"FORWARD WEAPON",hive::forwardSpec(practiceForward).name))practiceForward=hive::ForwardSet((int(practiceForward)+1)%3);
  if(select(0xB451,"BELLY PAYLOAD",hive::bombSpec(practiceBomb).name))practiceBomb=hive::BombSet((int(practiceBomb)+1)%3);
  if(select(0xB452,"DIFFICULTY",xr40::difficultyName(battleConfig.difficulty)))battleConfig.difficulty=xr40::Difficulty((int(battleConfig.difficulty)+1)%3);
  if(select(0xB453,"THEATER",theaters[battleTheater]))battleTheater=(battleTheater+1)%3;
  if(select(0xB454,"CONDITIONS",skies[battleWeather]))battleWeather=(battleWeather+1)%3;
  if(militaryButton(0xB455,rx,ry,rw,34*s,"NEW ENCOUNTER SEED"))++battleConfig.seed;
  text(28*s,H-73*s,12*s,"FLIGHT + COMBAT",ice);
  text(28*s,H-50*s,12*s,"WASD pitch / roll   Q/E yaw   Shift/Ctrl power   F/V pod tilt",white);
  text(28*s,H-28*s,11*s,"Mouse 1 fire   Backspace payload   X cloak   C camera   Esc pause / controls",dim);
  if(militaryButton(0xB460,W-588*s,H-72*s,120*s,44*s,"SETTINGS")) {battleShowSettings=true;settingsPage=0;}
  if(militaryButton(0xB461,W-454*s,H-72*s,92*s,44*s,"EXIT")) {saveBattleRecord();quit=true;}
  if(militaryButton(0xB462,W-348*s,H-72*s,320*s,44*s,"DEPLOY XR-40",true,true))launchBattle();
}

void Game::drawBattlePause() {
  const float s=scale(S()),W=float(g_ren.W),H=float(g_ren.H),w=500*s,h=530*s,x=(W-w)*.5f,y=(H-h)*.5f;
  g_ren.rect(0,0,W,H,ink,.68f);battlePanel(x,y,w,h,s);
  text(x+28*s,y+24*s,11*s,"XR-40 / FLIGHT ON HOLD",ice);text(x+28*s,y+50*s,32*s,"Battle paused");
  float yy=y+109*s;
  auto row=[&](uint32_t id,const char* title){bool hit=militaryButton(id,x+28*s,yy,w-56*s,43*s,title,true,id==0xB470);yy+=55*s;return hit;};
  if(row(0xB470,"RESUME"))paused=false;
  if(row(0xB471,"RESTART THIS SORTIE"))launchBattle();
  if(row(0xB472,"FLIGHT CONTROLS / GAMEPAD / HOTAS")){settingsFromPause=true;settingsPage=1;}
  if(row(0xB473,"GRAPHICS / AUDIO / ASSISTS")){settingsFromPause=true;settingsPage=0;}
  if(row(0xB474,"ABORT TO DEBRIEF"))finishBattle(false,"Pilot aborted the sortie");
  yy+=6*s;paragraph(x+28*s,yy,w-56*s,s,"SUPPLY: hold below 40 m/s within 250 m of the cyan gate for 5 seconds. Keep weapons idle and cloak off. Refill ammunition and fuel, repair 40 hull. Cost: 150 score; cooldown: 45 seconds.");
}

void Game::drawBattleDebrief() {
  const float s=scale(S()),W=float(g_ren.W),H=float(g_ren.H),w=740*s,h=620*s,x=(W-w)*.5f,y=(H-h)*.5f;
  g_ren.rect(0,0,W,H,ink,.75f);battlePanel(x,y,w,h,s);
  text(x+32*s,y+28*s,12*s,"XR-40 / AFTER ACTION",ice);
  text(x+32*s,y+57*s,34*s,debriefTitle,lastSuccess?ice:red);
  text(x+32*s,y+103*s,15*s,xr40::modeName(battle.config.mode));
  float py=y+134*s;paragraph(x+32*s,py,w-64*s,s,battle.reason,dim);
  g_ren.rect(x+32*s,y+199*s,w-64*s,1*s,ice,.22f);
  text(x+32*s,y+220*s,12*s,lastSuccess?"RECOVERED SCORE":"UNRECOVERED SCORE",dim);
  text(x+32*s,y+243*s,62*s,std::to_string(battle.stats.score),lastSuccess?ice:dim);
  const std::pair<const char*,std::string> lines[]={
    {"HOSTILES DESTROYED",std::to_string(battle.stats.kills)}, {"WAVES CLEARED",std::to_string(battle.stats.waves)},
    {"FLIGHT TIME",fmt("%02d:%02d",int(battle.stats.elapsed)/60,int(battle.stats.elapsed)%60)},
    {"HULL REMAINING",fmt("%.0f%%",militaryHull)}, {"RESUPPLIES",std::to_string(battle.stats.resupplies)}};
  float sy=y+222*s;
  for(const auto& entry:lines) {text(x+350*s,sy,11*s,entry.first,dim);text(x+w-32*s,sy,16*s,entry.second,white,2);sy+=36*s;}
  text(x+32*s,y+448*s,12*s,"Only a successful extraction banks a high score.",dim);
  if(!battleSaveError.empty()) {text(x+32*s,y+477*s,11*s,battleSaveError,amber);if(militaryButton(0xB488,x+w-180*s,y+466*s,148*s,32*s,"RETRY SAVE"))saveBattleRecord();}
  if(militaryButton(0xB480,x+32*s,y+h-78*s,218*s,46*s,"RETRY SORTIE",true,true))launchBattle();
  if(militaryButton(0xB481,x+262*s,y+h-78*s,218*s,46*s,"BATTLE HANGAR"))returnToBattle();
  if(militaryButton(0xB482,x+492*s,y+h-78*s,216*s,46*s,"EXIT")){saveBattleRecord();quit=true;}
}

void Game::drawBattleHud(const FrameParams& fp) {
  if(!plane.spec || !hudOn)return;
  const float s=scale(S()),W=float(g_ren.W),H=float(g_ren.H);
  hudMsgNext=126*s;
  battlePanel(20*s,20*s,240*s,104*s,s);text(34*s,31*s,11*s,"XR-40 / SOMNUS",ice);
  text(34*s,53*s,21*s,fmt("HULL %.0f",militaryHull),militaryHull<30?red:white);
  text(246*s,59*s,13*s,fmt("%02d:%02d",int(battle.stats.elapsed)/60,int(battle.stats.elapsed)%60),dim,2);
  bar(34*s,85*s,212*s,militaryHull/100,militaryHull<30?red:ice,s);text(34*s,101*s,11*s,fmt("SCORE %d   /   KILLS %d",battle.stats.score,battle.stats.kills),dim);
  const std::string objective=battle.objective(hiveCombat.aliveCount());
  g_ren.rect(W*.5f-310*s,20*s,620*s,44*s,ink,.82f,3*s);text(W*.5f,35*s,13*s,objective,battle.boundary>0?red:ice,1);
  battlePanel(W-300*s,38*s,280*s,76*s,s);
  text(W-24*s,46*s,13*s,fmt("%.0f KT   /   %.0f m AGL",plane.airspeed*MS_TO_KT,plane.agl()),white,2);
  text(W-24*s,70*s,12*s,fmt("POWER %.0f%% / PODS %.0f DEG",plane.ctl.throttle*100,plane.nozzle*90),dim,2);
  text(W-24*s,94*s,12*s,fmt("FUEL %.0f%%",100*plane.fuel/plane.spec->maxFuel),dim,2);
  const float cx=W*.5f,cy=H*.5f;
  float aimX=cx,aimY=cy;
  g_ren.project(fp,plane.pos+plane.forward()*2400.f,aimX,aimY);
  if(aimX>0 && aimX<W && aimY>0 && aimY<H)
    for(int sign:{-1,1}) {g_ren.line(aimX+sign*10*s,aimY,aimX+sign*26*s,aimY,1.5f*s,ice,.85f);g_ren.line(aimX,aimY+sign*10*s,aimX,aimY+sign*19*s,1.5f*s,ice,.85f);}
  auto marker=[&](vec3 p,const std::string& label,vec3 color,float size) {
    float x,y;const bool visible=g_ren.project(fp,p,x,y);
    if(visible && x>24*s && x<W-24*s && y>142*s && y<H-180*s) {
      g_ren.rectOutline(x-size*s,y-size*s,2*size*s,2*size*s,color,.9f,1*s,s);
      if(!label.empty())text(x,y+size*s+6*s,10*s,label,color,1);return;
    }
    const vec3 d=normalize(p-fp.camPos);float dx=dot(d,fp.camRight),dy=-dot(d,fp.camUp);
    if(dot(d,-fp.camBack)<0 && std::fabs(dx)<.02f)dx=.05f;
    const float len=std::max(.01f,std::sqrt(dx*dx+dy*dy));dx/=len;dy/=len;
    x=cx+dx*W*.35f;y=cy+dy*H*.30f;
    g_ren.line(x-dy*5*s,y+dx*5*s,x+dx*9*s,y+dy*9*s,2*s,color,.85f);
    g_ren.line(x+dy*5*s,y-dx*5*s,x+dx*9*s,y+dy*9*s,2*s,color,.85f);
    if(!label.empty())text(x,y+18*s,10*s,label,color,1);
  };
  const hive::Actor* selected=nullptr;float best=-2;
  for(const auto& actor:hiveCombat.actors)if(actor.alive) {
    const float aim=dot(normalize(actor.position-plane.pos),plane.forward());
    if(aim>best) {best=aim;selected=&actor;}
  }
  for(const auto& actor:hiveCombat.actors)if(actor.alive) {
    const float dist=length(actor.position-plane.pos);
    const char* name=actor.type==hive::Type::Needle?"NEEDLE":actor.type==hive::Type::Bastion?"BASTION":actor.type==hive::Type::Cantor?"CANTOR":"ARCHON";
    marker(actor.position,&actor==selected?fmt("%s %.1f km",name,dist/1000):std::string(),actor.phase==hive::Phase::Telegraph?amber:red,actor.type==hive::Type::Archon?18:12);
  }
  if(selected && best>.7f) {
    const vec3 rel=selected->position-plane.pos,v=selected->velocity-plane.vel;const float speed=hive::forwardSpec(combatLoadout.forward).speed;
    const float a=dot(v,v)-speed*speed,b=2*dot(rel,v),c=dot(rel,rel),disc=b*b-4*a*c;
    float t=0;if(disc>=0 && std::fabs(a)>1e-4f) {float t0=(-b-std::sqrt(disc))/(2*a),t1=(-b+std::sqrt(disc))/(2*a);t=t0>0?t0:t1;}
    if(t>0 && t<4) {float x,y;vec3 aim=selected->position+v*t-vec3(0,-hive::forwardSpec(combatLoadout.forward).gravity*t*t*.5f,0);
      if(g_ren.project(fp,aim,x,y))g_ren.rectOutline(x-4*s,y-4*s,8*s,8*s,ice,.9f,2*s,s);}
    text(cx,H-153*s,11*s,fmt("TARGET HULL %.0f / SHIELD %.0f",selected->hull,selected->shield),amber,1);
  }
  if(battle.phase==xr40::Phase::Extraction || (battle.phase==xr40::Phase::Relief && battle.config.mode==xr40::Mode::Survival))
    marker(battle.extraction,fmt("EXTRACT %.1f km",length(plane.pos-battle.extraction)/1000),ice,25);
  else marker(battle.supply,fmt("SUPPLY %.1f km",length(plane.pos-battle.supply)/1000),ice,18);
  if(battle.config.mode==xr40::Mode::Defense && hiveCombat.mission.config.objectiveCount)
    marker(hiveCombat.mission.config.objectives[0].position,"DEFEND RELAY",amber,20);
  // Heading-relative radar: one marker per actual actor; no world-scene queries.
  const float rx=112*s,ry=H-115*s,rad=72*s;battlePanel(20*s,H-208*s,184*s,188*s,s);
  text(32*s,H-195*s,10*s,"TACTICAL / 6 KM",dim);
  g_ren.line(rx-rad,ry,rx+rad,ry,s,dim,.4f);g_ren.line(rx,ry-rad,rx,ry+rad,s,dim,.4f);
  for(const auto& actor:hiveCombat.actors)if(actor.alive) {
    const vec3 d=actor.position-plane.pos;float dx=dot(d,plane.right())/6000,dy=-dot(d,plane.forward())/6000;
    const float k=std::max(1.f,std::hypot(dx,dy));dx/=k;dy/=k;g_ren.rect(rx+dx*rad-2*s,ry+dy*rad-2*s,4*s,4*s,red,1);
  }
  g_ren.rect(rx-2*s,ry-2*s,4*s,4*s,ice,1);
  const float bx=W-302*s,by=H-170*s;battlePanel(bx,by,282*s,150*s,s);
  text(bx+14*s,by+12*s,11*s,hive::forwardSpec(combatLoadout.forward).name,ice);
  text(bx+268*s,by+12*s,12*s,std::to_string(combatLoadout.rounds),white,2);bar(bx+14*s,by+35*s,254*s,combatLoadout.heat,combatLoadout.overheated?red:amber,s);
  text(bx+14*s,by+48*s,11*s,hive::bombSpec(combatLoadout.bomb).name,dim);text(bx+268*s,by+48*s,12*s,std::to_string(combatLoadout.bombs),white,2);
  text(bx+14*s,by+77*s,11*s,somnus.cloakOn?"CLOAK ACTIVE":"CLOAK RESERVE",somnus.cloakOn?ice:dim);bar(bx+14*s,by+98*s,254*s,battle.cloak,ice,s);
  text(bx+14*s,by+116*s,10*s,"C camera / Esc controls / X cloak",dim);
  if(length(plane.pos-battle.supply)<450)text(cx,H-124*s,13*s,battle.serviceCooldown>0?fmt("SUPPLY RECHARGING %.0f s",battle.serviceCooldown):battle.service>0?fmt("SUPPLY LINK %.0f%%",battle.service*20):"SUPPLY / WITHIN 250 m / BELOW 40 m/s / WEAPONS IDLE",ice,1);
  if(!somnus.armed)text(cx,H-101*s,13*s,"WEAPONS SAFE / Y TO ARM",amber,1);
  if(plane.agl()<120 && plane.vel.y<-7)text(cx,H*.70f,24*s,"PULL UP",red,1);
  if(crashed)text(cx,H*.65f,24*s,"AIRFRAME LOST",red,1);
}
