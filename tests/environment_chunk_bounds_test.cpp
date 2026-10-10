// Exact parent-chunk bounds, revision invalidation, and adversarial chunk edges.
#include "entity_bounds.h"
#include "scenery.h"
#include <chrono>
#include <thread>
#include <cstdio>

namespace {
int checks=0,failures=0;
void check(bool yes,const char*why){++checks;if(!yes){++failures;printf("FAIL %s\n",why);}}
bool contains(const EntWorldBounds&a,const EntWorldBounds&b){return a.lo.x<=b.lo.x&&a.lo.y<=b.lo.y&&a.lo.z<=b.lo.z&&a.hi.x>=b.hi.x&&a.hi.y>=b.hi.y&&a.hi.z>=b.hi.z;}
void specimen(Scenery::Chunk&c,int kind,Ent e){c.ents={e};for(int k=0;k<=EK_COUNT;++k)c.off[k]=k<=kind?0:1;c.level=2;c.markChanged();}
}
int main(){
  std::vector<EVert> v;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(v,ranges);EntLocalBounds models[EK_COUNT];entBuildLocalBounds(v,ranges,models,true);
  int crossings=0;
  for(int kind=0;kind<EK_COUNT;++kind)for(float yaw:{0.f,.47f,1.1f,1.9f,2.8f})for(float scale:{.6f,1.f,1.6f})for(float seed:{.001f,.13f,.5f,.87f,.999f}){
    const auto&i=kEntInfo[kind];float r=hypotf(i.hx*scale,i.hz*scale);if(r>=Scenery::CH*.49f)continue;
    Ent e{Scenery::CH-r-.0001f,20.f,Scenery::CH*.5f,yaw,scale,scale*1.07f,scale,seed};
    Scenery::Chunk c;specimen(c,kind,e);c.reach=0;c.ymin=e.y;c.ymax=e.y+i.h*e.sy;c.hmax=i.h*e.sy;
    EntChunkBoundsCache cache;auto world=entInstanceBounds(models[kind],kind,e);auto parent=cache.get(c,models);
    check(cache.valid&&contains(parent,world),"parent contains all-LOD animated instance bounds");
    if(world.hi.x>Scenery::CH){++crossings;check(parent.hi.x>Scenery::CH,"reach-zero newly protruding geometry is retained");}
    unsigned builds=cache.rebuilds;for(int n=0;n<20;++n)cache.get(c,models);check(cache.rebuilds==builds,"camera/time changes do not rebuild invariant envelope");
    // Same allocation, same count, same kind: only the revision detects this move.
    uint64_t old=c.revision;c.ents[0].x+=500;c.markChanged();check(c.revision>old,"direct content revision advances");
    auto moved=cache.get(c,models);check(cache.rebuilds==builds+1&&moved.hi.x>parent.hi.x+499,"same-size same-address mutation invalidates cache");
    Scenery::Chunk replacement;specimen(replacement,kind,e);old=replacement.revision;c=std::move(replacement);check(c.revision==old,"source replacement identity moves with content");
    auto restored=cache.get(c,models);check(cache.rebuilds==builds+2&&contains(restored,world),"replacement at reused cache slot invalidates");
    // Removing geometry keeps empty chunks from borrowing a previous occupant's box.
    c.ents.clear();std::fill(c.off,c.off+EK_COUNT+1,0);c.markChanged();cache.get(c,models);check(!cache.valid,"empty regenerated chunk has no stale bound");
  }
  check(crossings>0,"adversarial nominal reach-zero cases actually cross the edge");
  // Exercise the real generation/trim/clear/async publication paths on a safe,
  // flat all-sea field. No world cache or user-visible scenery is changed.
  g_world.hm.assign(size_t(HM_N)*HM_N*4,0.f);g_world.mask.assign(size_t(MASK_N)*MASK_N*4,0);g_world.roadGrid=RoadGrid();g_world.roadGrid.head.assign(size_t(MASK_N)*MASK_N,0);g_world.airports.clear();
  const int cx=0,cz=0;auto*c=g_scenery.ensure(cx,cz,1);uint64_t first=c->revision;check(first>0,"real generation has revision");
  auto*same=g_scenery.ensure(cx,cz,1);check(same->revision==first,"unchanged ensure keeps revision");
  c=g_scenery.ensure(cx,cz,2);uint64_t upgraded=c->revision;check(upgraded>first,"level upgrade has new revision");
  g_scenery.trim({0,0,0},1.f,1000000.f,1000);c=g_scenery.get(cx,cz);check(c&&c->level==1&&c->revision>upgraded,"detail trim invalidates bounds");
  uint64_t trimmed=c->revision;g_scenery.clear();c=g_scenery.ensure(cx,cz,1);check(c->revision>trimmed,"clear and allocator reuse cannot reuse generation identity");
  if(g_scenery.workers()>0){
    uint64_t before=c->revision;check(g_scenery.request(cx,cz,2),"async upgrade queued");
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);std::vector<int> installed;
    while(std::chrono::steady_clock::now()<until){g_scenery.pump(installed);c=g_scenery.get(cx,cz);if(c&&c->level==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    check(c&&c->level==2&&c->revision>before&&!installed.empty(),"async replacement publishes fresh identity");
    // A pending result from before clear must not replace the regenerated chunk.
    g_scenery.clear();check(g_scenery.request(cx,cz,2),"pre-clear async job queued");g_scenery.clear();c=g_scenery.ensure(cx,cz,1);uint64_t afterClear=c->revision;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));installed.clear();g_scenery.pump(installed);c=g_scenery.get(cx,cz);
    check(c&&c->level==1&&c->revision==afterClear&&installed.empty(),"stale async epoch cannot replace current source chunk");
  }
  printf("environment_chunk_bounds_test: %d checks, %d reach-zero protrusion cases, %d failures.\n",checks,crossings,failures);return failures?1:0;
}
