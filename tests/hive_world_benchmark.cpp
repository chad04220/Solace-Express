// Diagnostic CPU workload, not a rendering/FPS benchmark. Run explicitly after the world fixture.
#include "../src/hive_combat.h"
#include "../src/hive_world_collision.h"
#include "../src/entities.h"
#include "test_world.h"
#include <chrono>
#include <numeric>
#include <vector>

struct Queries { uint64_t terrain=0, sweeps=0; };
static float ground(float x,float z) { return std::max(0.f,pitGround(x,z,g_world.height(x,z))); }
static double percentile(const std::vector<double>& sorted,double p) {
  return sorted[std::min(sorted.size()-1,size_t(std::ceil(p*sorted.size())-1))];
}
static bool run(bool pressure) {
  using namespace hive;
  const int warm=600, measured=3600;
  const auto& apt=g_world.airports[g_world.findAirport("MDB")];
  const vec3 anchor(apt.x,apt.elev+650.f,apt.z);
  Queries q,warmQueries; WorldCallbacks w; w.context=&q; w.sweepIncludesTerrain=true;
  w.terrainHeight=[](void* c,float x,float z) { ++static_cast<Queries*>(c)->terrain; return ground(x,z); };
  w.sweepFraction=[](void* c,const vec3& a,const vec3& b,float r) { ++static_cast<Queries*>(c)->sweeps; return hiveWorldSweep(a,b,r); };
  Combat combat; combat.reset(20261011,WRAP_SPAN);
  PlayerSnapshot player; player.position=anchor; player.forward=vec3(0,0,-1); player.radius=10;
  player.bodyBoxCount=2; player.bodyBoxes[0]={{0,0,0},{2,2,8}}; player.bodyBoxes[1]={{0,0,0},{9,1,3}};
  uint64_t injected=0,respawns=0,events=0,fires=0,hits=0,actorSum=0,projectileSum=0;
  int minActors=MaxActors,maxProjectiles=0,minProjectiles=MaxProjectiles;
  std::vector<double> samples,warmSamples; samples.reserve(measured); warmSamples.reserve(warm);
  for(int tick=0;tick<warm+measured;++tick) {
    const float a=tick*FixedStep*.075f;
    player.position=anchor+vec3(350*std::sin(a),50*std::sin(a*.37f),350*std::cos(a));
    player.velocity={26.25f*std::cos(a),1.3875f*std::cos(a*.37f),-26.25f*std::sin(a)};
    player.forward=normalize(player.velocity);
    for(int slot=0;slot<MaxActors;++slot) if(!combat.actors[slot].alive) {
      float angle=slot*(2*PI/MaxActors); vec3 p=anchor+vec3(std::cos(angle)*1000,slot*35.f,std::sin(angle)*1000);
      p.y=std::max(p.y,ground(p.x,p.z)+250);
      combat.spawn(Type(slot%4),p,normalize(player.position-p)*roleSpec(Type(slot%4)).cruise);
      if(tick>=warm) ++respawns;
    }
    // Deliberately sustained collision-pressure case: refill the bounded simulation pool.
    // 32 bombs traverse real airport terrain/scenery; 64 pulses cross the combat area.
    if(pressure) for(int slot=0;slot<MaxProjectiles;++slot) if(!combat.projectiles[slot].alive) {
      const float theta=(slot*2*PI/MaxProjectiles)+float(tick%180)*.01f;
      vec3 p=anchor+vec3(std::cos(theta)*900,0,std::sin(theta)*900);
      Projectile shot; shot.alive=true; shot.team=Team::Hive; shot.owner=0; shot.id=100000+uint32_t(tick*MaxProjectiles+slot);
      shot.damage=1; shot.life=8; shot.position=p;
      if(slot<32) { shot.weapon=Weapon::Bomb; shot.radius=.75f; shot.blastRadius=25; shot.position.y=ground(p.x,p.z)+8;
        shot.velocity={25*std::cos(theta),-120,25*std::sin(theta)}; }
      else { shot.weapon=Weapon::Pulse; shot.radius=.18f; shot.velocity=normalize(player.position-p)*900; }
      combat.projectiles[slot]=shot; if(tick>=warm) ++injected;
    }
    if(tick==warm) { warmQueries=q; q={}; combat.droppedEvents=0; }
    combat.clearEvents();
    int actors=combat.aliveCount(),projectiles=0; for(const auto& p:combat.projectiles) projectiles+=p.alive;
    auto start=std::chrono::steady_clock::now();
    combat.step(FixedStep,player,w);
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(tick<warm) { warmSamples.push_back(ms); continue; }
    samples.push_back(ms); actorSum+=actors; projectileSum+=projectiles; minActors=std::min(minActors,actors);
    maxProjectiles=std::max(maxProjectiles,projectiles); minProjectiles=std::min(minProjectiles,projectiles);
    events+=combat.eventCount;
    for(int i=0;i<combat.eventCount;++i) { fires+=combat.events[i].type==EventType::Fire;
      hits+=combat.events[i].type==EventType::Hit || combat.events[i].type==EventType::PlayerDamage; }
  }
  const double firstTick=warmSamples.front();
  std::sort(warmSamples.begin(),warmSamples.end());
  std::sort(samples.begin(),samples.end());
  printf("scenario=%s initial_prefix_ticks=%d initial_prefix_seconds=10 scenery_cache=%s first_tick_ms=%.6f "
         "prefix_total_ms=%.6f prefix_mean_ms=%.6f prefix_p95_ms=%.6f prefix_p99_ms=%.6f prefix_max_ms=%.6f "
         "prefix_terrain_queries=%llu prefix_sweep_queries=%llu\n",
    pressure?"saturated_96_real_world":"natural_8_mixed_real_world",warm,
    pressure?"shared_with_previous_scenario":"initial_scene_first_touch",firstTick,
    std::accumulate(warmSamples.begin(),warmSamples.end(),0.),
    std::accumulate(warmSamples.begin(),warmSamples.end(),0.)/warmSamples.size(),
    percentile(warmSamples,.95),percentile(warmSamples,.99),warmSamples.back(),
    (unsigned long long)warmQueries.terrain,(unsigned long long)warmQueries.sweeps);

  printf("scenario=%s warmup_seconds=10 measured_seconds=60 ticks=%d mean_ms=%.6f p95_ms=%.6f p99_ms=%.6f max_ms=%.6f "
         "actors_mean=%.2f actors_min=%d projectiles_mean=%.2f projectiles_min=%d projectiles_max=%d "
         "events=%llu fire_events=%llu hit_events=%llu dropped_events=%u respawns=%llu injected_projectiles=%llu terrain_queries=%llu sweep_queries=%llu\n",
    pressure?"saturated_96_real_world":"natural_8_mixed_real_world",measured,
    std::accumulate(samples.begin(),samples.end(),0.)/samples.size(),percentile(samples,.95),percentile(samples,.99),samples.back(),
    double(actorSum)/measured,minActors,double(projectileSum)/measured,minProjectiles,maxProjectiles,
    (unsigned long long)events,(unsigned long long)fires,(unsigned long long)hits,combat.droppedEvents,
    (unsigned long long)respawns,(unsigned long long)injected,(unsigned long long)q.terrain,(unsigned long long)q.sweeps);
  fflush(stdout);
  return minActors==8 && maxProjectiles>0 && events>0 && q.sweeps>0 && (!pressure || minProjectiles==96);
}
int main() {
  buildTestWorld();
  puts("Hive CPU benchmark: real production terrain/scenery sweep callbacks; no renderer/GPU. Host has concurrent workload; timings are not isolated CPU or GPU FPS claims.");
  fflush(stdout);
  bool ok=run(false); ok=run(true)&&ok; return ok?0:1;
}
