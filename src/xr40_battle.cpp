#include "xr40_battle.h"
#include "finite_float.h"

namespace xr40 {
namespace {
float boundedDt(float dt) { return floatValidation::finite(dt) ? clampf(dt,0,.25f) : 0; }
float ground(const hive::WorldCallbacks& w, vec3 p) { return w.terrainHeight ? std::max(0.f,w.terrainHeight(w.context,p.x,p.z)) : 0; }
}
const char* modeName(Mode m) {
  switch(m) { case Mode::Defense:return "SHIELD LINE"; case Mode::Gauntlet:return "NIGHTGLASS";
    case Mode::Survival:return "ENDLESS SIEGE"; default:return "FIRST CONTACT"; }
}
const char* briefing(Mode m) {
  switch(m) {
    case Mode::Defense:return "Protect the relay from Bastion bombers. Break Cantor shield links, clear three waves, then return to the extraction gate.";
    case Mode::Gauntlet:return "Four escalating waves culminate in an Archon command ship. Destroy its relay escorts, evade the lance, and extract.";
    case Mode::Survival:return "Survive escalating waves. Between waves, enter the extraction gate to bank your score, or stay in the fight. No final wave.";
    default:return "Intercept two groups of Hive aircraft. Learn the XR-40's thrust-vectoring, forward weapons and cloak, then fly through extraction.";
  }
}
const char* difficultyName(Difficulty d) {
  switch(d) { case Difficulty::Cadet:return "CADET"; case Difficulty::Ace:return "ACE"; default:return "PILOT"; }
}
int Battle::waveGoal() const { return config.mode==Mode::Survival?0:config.mode==Mode::Gauntlet?4:config.mode==Mode::Defense?3:2; }
float Battle::damageScale() const { return config.difficulty==Difficulty::Cadet?.55f:config.difficulty==Difficulty::Ace?1.35f:1.f; }
void Battle::begin(Config c,hive::Combat& combat,vec3 arena,vec3 recovery,const hive::WorldCallbacks& world) {
  *this=Battle{};
  if(int(c.mode)<0 || c.mode>=Mode::Count)c.mode=Mode::Intercept;
  if(int(c.difficulty)<0 || c.difficulty>=Difficulty::Count)c.difficulty=Difficulty::Pilot;
  config=c;center=arena;extraction=supply=recovery;
  objectiveHealth=c.difficulty==Difficulty::Cadet?1400.f:c.difficulty==Difficulty::Ace?700.f:1000.f;
  combat.reset(config.seed);nextWave(combat,world);
}
void Battle::nextWave(hive::Combat& combat,const hive::WorldCallbacks& world) {
  ++wave;phase=Phase::Combat;relief=0;
  hive::MissionConfig m;
  m.kind=config.mode==Mode::Defense?hive::MissionKind::Defense:hive::MissionKind::Practice;
  m.extraction=extraction;m.extractionRadius=350;
  if(config.mode==Mode::Defense) {
    m.objectiveCount=1;m.objectives[0].id=9001;m.objectives[0].position=center;
    m.objectives[0].position.y=ground(world,center)+25;
    m.objectives[0].radius=70;m.objectives[0].health=objectiveHealth;
  }
  // Defer spawning to Combat's fixed ticks. A wave cannot skip actors when the pool is full.
  m.waveCount=1;auto& w=m.waves[0];w.at=0;
  const int level=int(config.difficulty);
  w.count=std::clamp(1+wave+level,2,6); // leave two slots for an Archon's reinforcements
  if(config.mode==Mode::Intercept)w.count=std::min(w.count,4);
  const float angle=float((config.seed+uint32_t(wave)*97u)%360)*DEG;
  w.position=center+vec3(sinf(angle)*1700,150,cosf(angle)*1700);
  float floor=ground(world,w.position);
  for(int i=0;i<w.count;++i)floor=std::max(floor,ground(world,w.position+vec3(float(i%3)*160,0,float(i%2)*160)));
  w.position.y=std::max(center.y,floor+650);
  for(int i=0;i<w.count;++i)w.types[i]=hive::Type::Needle;
  if(config.mode==Mode::Defense)w.types[0]=hive::Type::Bastion;
  if(wave>=2 || config.mode==Mode::Defense)w.types[w.count-1]=hive::Type::Cantor;
  if(wave>=3 && w.count>2)w.types[1]=hive::Type::Bastion;
  if((config.mode==Mode::Gauntlet && wave==4) || (config.mode==Mode::Survival && wave%3==0))w.types[0]=hive::Type::Archon;
  combat.startMission(m);
}
void Battle::observe(const hive::Combat& combat) {
  if(!active())return;
  for(int i=0;i<combat.eventCount;++i) {
    const auto& e=combat.events[i];
    if(e.type==hive::EventType::PlayerDamage)stats.damage+=e.value*damageScale();
    if(e.type==hive::EventType::Hit && e.source==0)++stats.hits;
    if(e.type==hive::EventType::Death && e.source==0) {
      const auto* a=combat.find(e.target);const int points=a?(a->type==hive::Type::Archon?1200:a->type==hive::Type::Cantor?350:200):200;
      ++stats.kills;stats.score+=points*(int(config.difficulty)+2)/3;
    }
  }
}
void Battle::update(float raw,hive::Combat& combat,const hive::PlayerSnapshot& player,float hull,const hive::WorldCallbacks& world) {
  const float dt=boundedDt(raw);if(!active() || dt<=0)return;
  stats.elapsed+=dt;serviceCooldown=std::max(0.f,serviceCooldown-dt);
  if(!player.alive || hull<=0) {finish(Phase::Defeat,"XR-40 lost");return;}
  if(config.mode==Mode::Defense && combat.mission.config.objectiveCount) {
    objectiveHealth=combat.mission.config.objectives[0].health;
    if(objectiveHealth<=0) {finish(Phase::Defeat,"Relay destroyed");return;}
  }
  const vec3 off=player.position-center;
  if(std::hypot(off.x,off.z)>arenaRadius())boundary+=dt;else boundary=0;
  if(boundary>=20) {finish(Phase::Defeat,"Left the combat area");return;}
  if(config.mode!=Mode::Survival && stats.elapsed>=1200) {finish(Phase::Defeat,"Sortie time limit reached");return;}
  const bool atExit=length(player.position-extraction)<=350;
  if(phase==Phase::Extraction || (phase==Phase::Relief && config.mode==Mode::Survival && atExit)) {
    if(atExit) {stats.score+=int(clampf(hull,0,100))*5;finish(Phase::Victory,"Aircraft recovered / objectives secured");}
    return;
  }
  if(phase==Phase::Relief) {
    relief-=dt;if(relief<=0)nextWave(combat,world);
    return;
  }
  bool shots=false;for(const auto& p:combat.projectiles)shots|=p.alive;
  if(combat.mission.nextWave==combat.mission.config.waveCount && combat.aliveCount()==0 && !shots) {
    ++stats.waves;stats.score+=250;combat.mission.status=hive::MissionStatus::Inactive;
    if(waveGoal()>0 && wave>=waveGoal())phase=Phase::Extraction;
    else {phase=Phase::Relief;relief=25;}
  }
}
bool Battle::updateCloak(float raw,bool requested,bool firing) {
  const float dt=boundedDt(raw);if(!active())return false;
  if(cloakLocked_ && cloak>=.25f)cloakLocked_=false;
  const bool on=requested && !firing && !cloakLocked_ && cloak>0;
  cloak=clampf(cloak+dt*(on?-1.f/14.f:1.f/22.f),0,1);
  if(cloak<=0)cloakLocked_=true;
  return on && cloak>0;
}
bool Battle::resupply(float raw,vec3 pos,float speed,bool busy) {
  const float dt=boundedDt(raw);
  if(!active() || dt<=0 || serviceCooldown>0 || busy || !floatValidation::finite(speed) || speed>40 || length(pos-supply)>250) {service=0;return false;}
  service+=dt;
  if(service<5)return false;
  service=0;serviceCooldown=45;cloak=1;cloakLocked_=false;++stats.resupplies;stats.score=std::max(0,stats.score-150);return true;
}
void Battle::finish(Phase outcome,const std::string& why) {
  if(!active())return;
  phase=outcome;reason=why;
}
std::string Battle::objective(int hostiles) const {
  if(boundary>0)return fmt("RETURN TO COMBAT AREA / %.0f s",std::max(0.f,20-boundary));
  if(phase==Phase::Extraction)return "OBJECTIVES SECURED / FLY THROUGH EXTRACTION";
  if(phase==Phase::Relief)return fmt("WAVE CLEAR / NEXT IN %.0f s%s",relief,config.mode==Mode::Survival?" / EXTRACT TO BANK SCORE":"");
  if(config.mode==Mode::Defense)return fmt("DEFEND RELAY %.0f HP / WAVE %d / %d HOSTILES",objectiveHealth,wave,hostiles);
  return fmt("WAVE %d%s / %d HOSTILES / SCORE %d",wave,waveGoal()?fmt(" OF %d",waveGoal()).c_str():"",hostiles,stats.score);
}
} // namespace xr40
