// Solace Express - environment entities: deterministic placement, chunk streaming and collisions
#include "entities.h"
#include "scenery.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace { std::atomic<uint64_t> entityChunkRevision{1}; }
void Scenery::Chunk::markChanged() { revision=entityChunkRevision.fetch_add(1,std::memory_order_relaxed); }

Scenery g_scenery;

// ---------------------------------------------------------------- worker pool for chunk generation
struct Scenery::Async {
  struct Job { int cx, cz, level, epoch; Chunk c; };
  std::mutex m;
  std::condition_variable cv;
  std::deque<Job> todo;
  std::vector<Job> done;
  std::unordered_map<int, int> busy;   // chunk index -> level being generated (main thread only)
  std::vector<std::thread> threads;
  bool stop = false;
  bool draining = false;
  int epoch = 0;                       // bumped by clear(): results from before it are dropped
  int active = 0;                      // workers reading the active world outside the queue lock
};

Scenery::Scenery() : async(new Async()) {
  int hw = (int)std::thread::hardware_concurrency();
  int n = std::clamp(hw - 1, 0, 6);    // leave the main / GL thread its own core
  for (int t = 0; t < n; t++)
    async->threads.emplace_back([this] {
      Async& A = *async;
      for (;;) {
        Async::Job j;
        {
          std::unique_lock<std::mutex> lk(A.m);
          A.cv.wait(lk, [&] { return A.stop || !A.todo.empty(); });
          if (A.stop) return;
          j = std::move(A.todo.front()); A.todo.pop_front();
          A.active++;
        }
        generate(j.c, j.cx, j.cz, j.level);
        std::lock_guard<std::mutex> lk(A.m);
        A.done.push_back(std::move(j));
        A.active--;
        A.cv.notify_all();
      }
    });
}

Scenery::~Scenery() {
  { std::lock_guard<std::mutex> lk(async->m); async->stop = true; }
  async->cv.notify_all();
  for (auto& t : async->threads) t.join();
}

int Scenery::workers() const { return (int)async->threads.size(); }

void Scenery::beginDrain() {
  std::lock_guard<std::mutex> lk(async->m);
  async->draining = true; async->todo.clear(); async->busy.clear(); async->epoch++;
}
bool Scenery::isDrained() const {
  std::lock_guard<std::mutex> lk(async->m);
  return async->draining && async->active == 0 && async->todo.empty();
}
void Scenery::resumeStreaming() {
  std::lock_guard<std::mutex> lk(async->m);
  async->draining = false;
}

void Scenery::clear() {
  // No new request/ensure/pump may run concurrently. A region replacement must wait for
  // readers of the old world, rather than merely throwing their eventual results away.
  std::unique_lock<std::mutex> lk(async->m);
  async->draining = true; async->todo.clear(); async->busy.clear(); async->epoch++;
  async->cv.wait(lk, [&] { return async->active == 0; });
  async->done.clear();
  chunks.clear(); async->draining = false;
}

bool Scenery::request(int cx, int cz, int level) {
  Async& A = *async;
  { std::lock_guard<std::mutex> lk(A.m); if (A.draining) return false; }
  if (A.threads.empty() || cx < 0 || cz < 0 || cx >= NC || cz >= NC) return false;
  int idx = cz * NC + cx;
  auto it = A.busy.find(idx);
  if (it != A.busy.end() && it->second >= level) return true;   // already on its way
  Async::Job j{cx, cz, level, 0, {}};
  if (Chunk* c = get(cx, cz)) { if (c->level >= level) return true; j.c = *c; }
  {
    std::lock_guard<std::mutex> lk(A.m);
    // keep the queue short so it always holds the chunks nearest the camera, not ones requested frames ago
    if ((int)A.todo.size() >= 2 * (int)A.threads.size()) return false;
    j.epoch = A.epoch;
    A.todo.push_back(std::move(j));
  }
  A.busy[idx] = level;
  A.cv.notify_one();
  return true;
}

void Scenery::pump(std::vector<int>& installed) {
  Async& A = *async;
  std::vector<Async::Job> got;
  int epoch;
  { std::lock_guard<std::mutex> lk(A.m); got.swap(A.done); epoch = A.epoch; }
  if (chunks.empty()) chunks.resize((size_t)NC * NC);
  for (Async::Job& j : got) {
    int idx = j.cz * NC + j.cx;
    auto it = A.busy.find(idx);
    if (it != A.busy.end() && it->second <= j.level) A.busy.erase(it);
    if (j.epoch != epoch) continue;
    auto& p = chunks[idx];
    if (p && p->level >= j.c.level) continue;   // generated synchronously in the meantime
    int lastUse = p ? p->lastUse : 0;
    p.reset(new Chunk(std::move(j.c)));
    p->lastUse = lastUse;
    p->markChanged();  // publication gets a fresh identity even after an older worker job
    installed.push_back(idx);
  }
}

const EntKindInfo kEntInfo[EK_COUNT] = {
  {"Fir", 3.2f, 14.f, 3.2f}, {"Spruce", 2.6f, 20.f, 2.6f}, {"Pine", 3.6f, 16.f, 3.6f}, {"Oak", 5.0f, 11.f, 5.0f},
  {"Birch", 2.8f, 13.f, 2.8f}, {"Palm", 4.2f, 10.f, 4.2f}, {"Bush", 1.6f, 1.8f, 1.6f},
  {"Boulder", 1.0f, 1.4f, 1.0f}, {"Block", 1.1f, 1.3f, 0.9f}, {"Slab", 1.6f, 0.8f, 1.2f}, {"Outcrop", 7.f, 8.f, 6.f},
  {"Spire", 3.5f, 24.f, 3.5f}, {"Sea stack", 10.f, 30.f, 9.f},
  {"House", 4.5f, 8.6f, 5.5f}, {"Bungalow", 5.0f, 5.8f, 5.0f}, {"L-house", 6.0f, 8.2f, 6.0f}, {"Farmhouse", 5.5f, 9.0f, 6.0f},
  {"Townhouses", 9.0f, 10.5f, 5.5f}, {"Shop", 7.0f, 5.5f, 7.0f}, {"Apartments", 9.0f, 22.f, 7.0f}, {"Office", 9.0f, 38.f, 9.0f},
  {"Tower", 10.f, 80.f, 10.f}, {"Skyscraper", 9.0f, 135.f, 9.0f},
  {"Supertall", 12.f, 236.f, 12.f}, {"Round tower", 9.5f, 124.f, 9.5f}, {"Tower block", 14.f, 64.f, 7.5f}, {"Office block", 15.f, 31.f, 11.f},
  {"Warehouse", 12.f, 8.5f, 9.0f}, {"Barn", 6.0f, 10.f, 9.0f},
  {"Silo", 3.0f, 19.f, 3.0f}, {"Church", 5.0f, 28.f, 13.f}, {"Water tower", 5.2f, 28.f, 5.2f}, {"Lighthouse", 3.4f, 27.f, 3.4f},
  {"Gas station", 8.0f, 5.5f, 7.0f}, {"Runway light", 0.12f, 0.42f, 0.12f}, {"PAPI unit", 0.45f, 0.8f, 0.35f},
  {"Hangar", 20.f, 13.f, 16.f}, {"Arch hangar", 9.f, 7.5f, 12.f}, {"T-hangars", 24.f, 4.8f, 7.f}, {"Terminal", 60.f, 15.f, 20.f},
  {"Control tower", 5.f, 34.f, 5.f}, {"Flight centre", 10.f, 5.f, 7.f}, {"Fuel tank", 6.f, 9.f, 6.f}, {"Fuel pump", 2.5f, 2.6f, 1.6f},
  {"Windsock", 1.9f, 6.6f, 1.9f}, {"Beacon", 1.2f, 15.f, 1.2f}, {"Parked aircraft", 5.5f, 2.7f, 4.2f}, {"Parked airliner", 17.f, 11.5f, 19.5f},
  {"Jet bridge", 1.6f, 5.6f, 10.f}, {"Car", 0.9f, 1.5f, 2.25f}, {"Truck", 1.25f, 3.1f, 4.5f}, {"Fence", 10.f, 2.4f, 0.1f},
  {"Localizer", 18.f, 3.2f, 1.f}, {"Radar", 5.f, 22.f, 5.f}, {"Mast", 1.5f, 12.f, 1.5f}, {"Floodlight mast", 1.5f, 20.f, 1.5f},
  {"Street lamp", 0.3f, 10.f, 1.f}, {"Guardrail", 8.f, 0.9f, 0.2f}, {"Road sign", 1.3f, 3.f, 0.12f}, {"Utility pole", 1.05f, 9.f, 0.2f},
  {"Wind turbine", 2.5f, 138.f, 2.5f},   // (its tower: the rotor, 46 m round the hub at 92 m, isn't solid)
  {"Shipwreck", 7.f, 22.f, 37.f}, {"Offshore platform", 22.f, 80.f, 22.f},   // (the wreck from its keel; the platform from the sea)
  {"Observatory", 9.f, 14.5f, 6.5f}, {"Star fort", 62.f, 8.2f, 62.f}, {"Landing stage", 5.5f, 2.6f, 25.5f},
  {"Stadium", 92.f, 31.f, 74.f}, {"Quay", 215.f, 1.f, 63.f}, {"Gantry crane", 16.f, 72.f, 35.f}, {"Container stack", 37.f, 10.6f, 7.5f},
  {"Container ship", 17.f, 40.f, 115.f},   // (the quay from its apron's top; the ship from the sea)
  {"Pit garages", 48.f, 13.5f, 11.f}, {"Pit wall", 24.f, 1.1f, 3.f}, {"Grandstand", 60.f, 24.f, 16.f},   // (the pit wall: its concrete; the fence above it isn't solid)
  {"Race transporter", 1.3f, 4.1f, 8.3f}, {"Dam", 160.f, 72.f, 60.f},   // (the dam: its arch and powerhouse; its lake's edge beyond)
  {"Ferry", 9.f, 27.f, 50.f}, {"Yacht", 1.8f, 14.f, 5.2f},   // (from the sea: y 0 the waterline)
  {"Ferry pier", 7.f, 3.3f, 20.f}, {"Linkspan", 15.f, 14.f, 13.f},   // (the deck, solid; the ramp and its gantry)
};

static inline float h2(int a, int b) { return hash2i(a, b); }

Scenery::Chunk* Scenery::get(int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= NC || cz >= NC) return nullptr;
  if (chunks.empty()) chunks.resize((size_t)NC * NC);
  return chunks[(size_t)cz * NC + cx].get();
}

Scenery::Chunk* Scenery::ensure(int cx, int cz, int level) {
  if (cx < 0 || cz < 0 || cx >= NC || cz >= NC) return nullptr;
  if (chunks.empty()) chunks.resize((size_t)NC * NC);
  auto& p = chunks[(size_t)cz * NC + cx];
  if (!p) p.reset(new Chunk());
  if (p->level < level) generate(*p, cx, cz, level);
  return p.get();
}

size_t Scenery::generated() const { size_t n = 0; for (auto& c : chunks) if (c) n++; return n; }

bool Scenery::destroyed(const Ent& e) const {
  for (const vec3& c : craters) { float dx = e.x - c.x, dz = e.z - c.y; if (dx * dx + dz * dz < c.z * c.z * 1.44f) return true; }
  if (wrecked.empty()) return false;
  auto it = wrecked.find(chunkOf(e.z) * NC + chunkOf(e.x));
  if (it == wrecked.end()) return false;
  uint64_t k = keyOf(e);
  for (uint64_t w : it->second) if (w == k) return true;
  return false;
}

bool Scenery::chunkAffected(int cx, int cz) const {
  if (!wrecked.empty() && wrecked.count(cz * NC + cx)) return true;
  float x0 = chunkX0(cx), z0 = chunkX0(cz), x1 = x0 + CH, z1 = z0 + CH;
  for (const vec3& c : craters) {
    float dx = std::max(std::max(x0 - c.x, c.x - x1), 0.f), dz = std::max(std::max(z0 - c.y, c.y - z1), 0.f);
    if (dx * dx + dz * dz < c.z * c.z * 1.44f) return true;
  }
  return false;
}

int Scenery::hitPoints(const Ent& e, int kind) {
  if (entClass(kind) == EC_TREE || kind <= EK_SLAB) return 1;
  const EntKindInfo& I = kEntInfo[kind];
  float vol = (2.f * I.hx * e.sx) * (I.h * e.sy) * (2.f * I.hz * e.sz);
  return std::clamp((int)lroundf(vol / 260.f), 2, 12);
}

bool Scenery::damage(const Ent& e, int kind, int amount) {
  if (destroyed(e)) return false;
  uint64_t k = keyOf(e);
  int& h = hits[k];
  h += amount;
  if (h < hitPoints(e, kind)) return false;
  hits.erase(k);
  // filed under the chunk its position falls in (destroyed() looks there) and, on a chunk seam, the neighbour too: the
  // chunk that placed it may be either side of the rounding, and only a chunk with something wrecked is drawn without
  // its wrecks (the review of v3.44.0, WLD-3: 13 in 1.21 M stayed drawn, and cast shadows, with no collision)
  const int cx = chunkOf(e.x), cz = chunkOf(e.z);
  const float fx = e.x - chunkX0(cx), fz = e.z - chunkX0(cz), seam = 0.5f;
  const int ox = fx < seam ? -1 : fx > CH - seam ? 1 : 0, oz = fz < seam ? -1 : fz > CH - seam ? 1 : 0;
  for (int dz = 0; dz <= (oz ? 1 : 0); dz++)
    for (int dx = 0; dx <= (ox ? 1 : 0); dx++) {
      const int nx = cx + dx * ox, nz = cz + dz * oz;
      if (nx >= 0 && nx < NC && nz >= 0 && nz < NC) wrecked[nz * NC + nx].push_back(k);
    }
  wreckRev++;
  return true;
}

// ------------------------------------------------------------------ placement helpers
// The lighthouse on Lighthouse Key: the low land point nearest the sea around the village (3 to 16 m up, the sea 45 m
// on beyond it), and the way it looks out to sea
bool findLighthouseSite(const World& world, float& lx, float& lz, float& yaw) {
  if (world.procedural) return false;   // Lighthouse Key is an authored Solace landmark.
  const Town* T = nullptr;
  for (const Town& town : world.towns) if (!strcmp(town.name, "Lighthouse Key")) T = &town;
  if (!T) return false;
  auto ground = [&](float x, float z) { return world.groundHeight(x, z, 7); };
  float best = 1e9f;
  for (float r = 80.f; r <= 700.f; r += 20.f)
    for (int a = 0; a < 72; a++) {
      float ang = a * 5.f * DEG, dx = cosf(ang), dz = sinf(ang);
      float x = T->x + dx * r, z = T->z + dz * r, g = ground(x, z);
      if (g < 3.f || g > 16.f || ground(x + dx * 45.f, z + dz * 45.f) > -0.5f) continue;
      float score = r * 0.02f + fabsf(g - 7.f);
      if (score < best) { best = score; lx = x; lz = z; yaw = atan2f(dx, dz); }
    }
  return best < 1e8f;
}

namespace {
struct Ctx {
  std::vector<Ent>* out[EK_COUNT];
  float x0, z0, x1, z1;
  bool inside(float x, float z) const { return x >= x0 && x < x1 && z >= z0 && z < z1; }
};

float ground(float x, float z) { return g_world.groundHeight(x, z, 7); }

// lowest ground under a footprint (rotated half extents), so a building's plinth never floats on a slope
float footprintGround(float x, float z, float yaw, float hx, float hz) {
  float c = cosf(yaw), s = sinf(yaw), g = ground(x, z);
  for (int k = 0; k < 4; k++) {
    float lx = (k & 1) ? hx : -hx, lz = (k & 2) ? hz : -hz;
    g = std::min(g, ground(x + c * lx + s * lz, z - s * lx + c * lz));
  }
  return g;
}

void put(Ctx& C, int kind, float x, float y, float z, float yaw, float sx, float sy, float sz, float seed) {
  C.out[kind]->push_back({x, y, z, yaw, sx, sy, sz, seed});
}

// building of a kind fitted to a footprint (half extents along its own x / z) on the lowest ground under it
void putBuilding(Ctx& C, int kind, float x, float z, float yaw, float hw, float hd, float height, float seed, float validatedGround = 1e9f) {
  const EntKindInfo& I = kEntInfo[kind];
  float sx = clampf(hw / I.hx, 0.7f, 1.3f), sz = clampf(hd / I.hz, 0.7f, 1.3f);
  float sy = entBuildingVerticalScale(kind, height > 0 ? height / I.h : .9f + .2f * seed);
  float g = validatedGround < 1e8f ? validatedGround : footprintGround(x, z, yaw, I.hx * sx, I.hz * sz) - .05f;
  put(C, kind, x, g, z, yaw, sx, sy, sz, seed);
}

// The active region's immutable, lazily populated geometry. Reset only after all
// scenery workers have finished, before replacing the world they refer to.
struct EntityRegionCache {
  std::once_flag lighthouseOnce, airportOnce, padOnce;
  bool lighthouseFound = false;
  float lighthouseX = 0.f, lighthouseZ = 0.f, lighthouseYaw = 0.f, pad = 20.f;
  struct Bounds { float x0, z0, x1, z1; };
  std::vector<std::vector<AptItem>> airportItems;
  std::vector<Bounds> airportBounds;
};
std::unique_ptr<EntityRegionCache>& regionCache() {
  static std::unique_ptr<EntityRegionCache> cache(new EntityRegionCache());
  return cache;
}

// The lighthouse stands on the shore of Lighthouse Key: the low land point nearest the sea around the village
bool lighthouseSite(float& lx, float& lz, float& yaw) {
  EntityRegionCache& cache = *regionCache();
  std::call_once(cache.lighthouseOnce, [&] {
    cache.lighthouseFound = findLighthouseSite(g_world, cache.lighthouseX, cache.lighthouseZ, cache.lighthouseYaw);
  });
  lx = cache.lighthouseX; lz = cache.lighthouseZ; yaw = cache.lighthouseYaw;
  return cache.lighthouseFound;
}

// tree species for a site
int treeSpecies(float g, float lush, float cold, float sp, float sp2) {
  if (cold > 0.45f || g > 650.f) return sp2 < 0.45f ? EK_FIR : sp2 < 0.85f ? EK_SPRUCE : EK_PINE;
  if (lush > 0.85f && g < 70.f && sp > 0.35f) return EK_PALM;
  if (sp < 0.18f) return sp2 < 0.5f ? EK_FIR : EK_PINE;
  return sp2 < 0.58f ? EK_OAK : sp2 < 0.84f ? EK_BIRCH : sp2 < 0.9f ? EK_PINE : EK_FIR;
}
}  // namespace

void sceneryResetWorldCaches() {
  g_scenery.clear();
  regionCache().reset(new EntityRegionCache());
  g_scenery.craters.clear();
  g_scenery.resetDamage();
}

bool seaStackShore(float x, float z) {
  for (int k = 0; k < 12; k++) {
    const float ca = cosf(k * 0.5236f), sa = sinf(k * 0.5236f);
    for (float d = 30.f; d <= 220.f; d += 30.f)
      if (g_world.naturalHeight(x + ca * d, z + sa * d) > 0.f) {   // (the shore this way: steep behind it?)
        if (g_world.naturalHeight(x + ca * (d + 40.f), z + sa * (d + 40.f)) > 14.f || g_world.naturalHeight(x + ca * (d + 100.f), z + sa * (d + 100.f)) > 26.f) return true;
        break;
      }
  }
  return false;
}

// ------------------------------------------------------------------ chunk generation
void Scenery::generate(Chunk& ch, int cx, int cz, int level) {
  std::vector<Ent> lists[EK_COUNT];
  // keep what the chunk already has
  for (int k = 0; k < EK_COUNT; k++) lists[k].assign(ch.ents.begin() + ch.off[k], ch.ents.begin() + ch.off[k + 1]);
  Ctx C;
  for (int k = 0; k < EK_COUNT; k++) C.out[k] = &lists[k];
  C.x0 = chunkX0(cx); C.z0 = chunkX0(cz); C.x1 = C.x0 + CH; C.z1 = C.z0 + CH;
  const int from = ch.level + 1;

  for (int L = from; L <= level; L++) {
    // ---------------------------------------------------------------- airport lighting fixtures (L1): runway edge
    // lights every 60 m (amber over the last 600 m of a paved runway), green / red threshold bars, approach lights
    if (L == 1)
      for (const Airport& a : g_world.airports) {
        vec3 c = a.pos(), dir = a.dir(), rt(-dir.z, 0, dir.x);
        float ext = a.length * 0.5f + 420.f;
        if (c.x + ext < C.x0 || c.x - ext > C.x1 || c.z + ext < C.z0 || c.z - ext > C.z1) continue;
        auto put = [&](vec3 p, int col) {
          if (!C.inside(p.x, p.z)) return;
          float y = std::max(g_world.height(p.x, p.z), a.elev - 0.5f);
          C.out[EK_RWYLIGHT]->push_back({p.x, y, p.z, 0.f, 1.f, 1.f, 1.f, (float)col + 0.5f});
        };
        for (float u = -a.length * 0.5f; u <= a.length * 0.5f + 0.1f; u += 60.f)
          for (int sd = -1; sd <= 1; sd += 2)
            put(c + dir * u + rt * (sd * (a.width * 0.5f + 1.5f)), fabsf(u) > a.length * 0.5f - 600.f && a.size > 0 ? 1 : 0);
        if (a.size > 0 || a.surface == SURF_ASPHALT)   // PAPI: four units left of each landing direction, 300 m in
          for (int end = -1; end <= 1; end += 2) {
            vec3 ld = dir * (float)(-end), lrt(-ld.z, 0, ld.x);
            vec3 base = c + dir * (end * a.length * 0.5f) + ld * 300.f - lrt * (a.width * 0.5f + 15.f);
            for (int i = 0; i < 4; i++) {
              vec3 p = base - lrt * (i * 9.f);
              if (!C.inside(p.x, p.z)) continue;
              float y = std::max(g_world.height(p.x, p.z), a.elev - 0.5f);
              C.out[EK_PAPI]->push_back({p.x, y, p.z, atan2f(-ld.x, -ld.z), 1.f, 1.f, 1.f, 3.5f - i * 0.333f});
            }
          }
        for (int end = -1; end <= 1; end += 2) {
          for (float v = -a.width * 0.5f; v <= a.width * 0.5f + 0.01f; v += 3.f)
            put(c + dir * (end * (a.length * 0.5f + 1.f)) + rt * v, end < 0 ? 2 : 3);
          if (a.size > 0)
            for (int k = 1; k <= 6; k++)
              for (float v = -8.f; v <= 8.f; v += 4.f) put(c + dir * (end * (a.length * 0.5f + 60.f * k)) + rt * v, 0);
        }
      }
    // ---------------------------------------------------------------- airport buildings, aircraft, vehicles (L1)
    if (L == 1) {
      EntityRegionCache& cache = *regionCache();
      auto& items = cache.airportItems;
      auto& boxes = cache.airportBounds;
      using Bounds = EntityRegionCache::Bounds;
      std::call_once(cache.airportOnce, [&] {
        items.resize(g_world.airports.size()); boxes.resize(g_world.airports.size());
        for (size_t i = 0; i < items.size(); i++) {
          airportItems((int)i, items[i]);
          Bounds b{1e9f, 1e9f, -1e9f, -1e9f};
          for (auto& it : items[i]) { float r = std::max(kEntInfo[it.kind].hx * it.e.sx, kEntInfo[it.kind].hz * it.e.sz);
            b.x0 = std::min(b.x0, it.e.x - r); b.z0 = std::min(b.z0, it.e.z - r); b.x1 = std::max(b.x1, it.e.x + r); b.z1 = std::max(b.z1, it.e.z + r); }
          boxes[i] = b;
        }
      });
      for (size_t i = 0; i < items.size(); i++) {
        const Bounds& b = boxes[i];
        if (b.x1 < C.x0 || b.x0 > C.x1 || b.z1 < C.z0 || b.z0 > C.z1) continue;
        for (auto& it : items[i]) if (C.inside(it.e.x, it.e.z)) C.out[it.kind]->push_back(it.e);
      }
    }
    // ---------------------------------------------------------------- the settlements (scenery.h): their buildings,
    // each on its lot facing its road; a car on a house's drive; trees in the gardens behind the houses, on the
    // pavements of the centres' streets and in their parks
    {
      std::vector<int> ids;
      lotsIn(C.x0 - 32.f, C.z0 - 32.f, C.x1 + 32.f, C.z1 + 32.f, ids);
      const std::vector<Lot>& lots = settlementLots();
      for (int id : ids) {
        const Lot& lot = lots[id];
        const float c = cosf(lot.yaw), s = sinf(lot.yaw);
        auto at = [&](float lx, float lz, float& x, float& z) { x = lot.cx + c * lx + s * lz; z = lot.cz - s * lx + c * lz; };
        const int si = (int)floorf(lot.cx), sj = (int)floorf(lot.cz);
        const bool house = lot.kind == EK_HOUSE || lot.kind == EK_HOUSE_HIP || lot.kind == EK_HOUSE_L || lot.kind == EK_FARMHOUSE;
        if (L == 1) {
          if (C.inside(lot.cx, lot.cz)) putBuilding(C, lot.kind, lot.cx, lot.cz, lot.yaw, lot.hw, lot.hd, 0.f, lot.seed, lot.ground);
          // (a car on the drive beside a house, nose to the road)
          if (house && h2(si * 13 - 5, sj * 29 + 11) < 0.35f) {
            float x, z; at((lot.hw + 2.2f) * (lot.seed < 0.5f ? 1.f : -1.f), lot.hd - 1.5f, x, z);
            if (C.inside(x, z) && roadDistance(x, z) > 7.f && ground(x, z) > 2.5f)
              put(C, EK_CAR, x, footprintGround(x, z, lot.yaw, .9f, 2.25f), z, lot.yaw, 1.f, 1.f, 1.f, h2(si, sj));
          }
        } else if (house || lot.kind == EK_TOWNHOUSE) {
          // the back garden: a tree or two, or three (a terrace's, one in three); now and then one in the front
          for (int t = 0; t < 4; t++) {
            const float ht = h2(si * 5 + t * 71, sj * 3 - t * 13);
            if (ht > (lot.kind == EK_TOWNHOUSE ? (t ? 0.f : 0.33f) : t == 0 ? 0.9f : t == 1 ? 0.65f : t == 2 ? 0.35f : 0.3f)) continue;
            float x, z;
            if (t == 3) at((h2(si + t, sj - 9) < 0.5f ? -1.f : 1.f) * (lot.hw + 1.5f), lot.hd + 2.5f, x, z);   // (the front garden)
            else at((h2(si + t, sj - 9) - 0.5f) * 2.f * (lot.hw + 2.f), -(lot.hd + 3.f + 7.f * h2(si - t, sj + 3)), x, z);
            if (!C.inside(x, z) || roadDistance(x, z) < 9.f || ground(x, z) < 2.5f) continue;
            float base[4]; g_world.sampleBase(x, z, base);
            const int sp = base[3] > .45f ? EK_SPRUCE : base[2] > .85f && ground(x, z) < 70.f ? EK_PALM : ht < .3f ? EK_BIRCH : EK_OAK;
            const float sc = .55f + .25f * h2(si - t * 7, sj + 41);
            put(C, sp, x, ground(x, z) - .15f, z, ht * 40.f, sc, sc, sc, h2(si * 3 + t, sj * 7));
          }
        }
      }
      if (L == 2) {
        treesIn(C.x0, C.z0, C.x1, C.z1, ids);
        const std::vector<TreeSpot>& trees = settlementTrees();
        for (int id : ids) {
          const TreeSpot& t = trees[id];
          if (!C.inside(t.x, t.z)) continue;
          put(C, t.kind, t.x, ground(t.x, t.z) - .15f, t.z, t.seed * 40.f, t.scale, t.scale, t.scale, t.seed);
        }
      }
    }

    if (L == 1) {
      // ---------------------------------------------------------------- farmsteads (World::farms): round each yard -
      // the farmhouse by the way in, facing the yard; the barn across its far end; a machine shed along one side; the
      // silos by the barn; often a second shed; a lorry or a car in the yard. Each building faces the yard and stands
      // clear of its platform and of every road
      for (const Farmstead& F : g_world.farms) {
        if (F.x < C.x0 - 90.f || F.x > C.x1 + 90.f || F.z < C.z0 - 90.f || F.z > C.z1 + 90.f) continue;
        const vec2 in(F.dx, F.dz), across(-F.dz, F.dx);
        const float seed = F.seed, mirror = h2((int)F.x, (int)F.z + 7) < 0.5f ? 1.f : -1.f;   // (the house on one side or the other)
        auto at = [&](float a, float b) { return vec2(F.x, F.z) + across * (a * mirror) + in * b; };
        auto facing = [&](vec2 p) { const vec2 v = vec2(F.x, F.z) - p; return atan2f(v.x, v.y); };   // (its front, local +z, to the yard)
        auto farmBuilding = [&](int kind, vec2 p, float yaw, float hw, float hd, float height, float hs) {
          if (!C.inside(p.x, p.y) || roadDistance(p.x, p.y) - 6.f < std::max(hw, hd) + 1.f || ground(p.x, p.y) < 3.f) return;
          putBuilding(C, kind, p.x, p.y, yaw, hw, hd, height, hs);
        };
        auto square = [](vec2 front) { return atan2f(front.x, front.y); };   // (its front, local +z, along front)
        // (the yard's platform: 13 m either side of its centre line, from 23 m before its centre to 23 m beyond)
        const vec2 house = at(-23.f, -6.f), barn = at(0.f, 36.f), shed = at(26.f, 4.f);
        farmBuilding(EK_FARMHOUSE, house, square(across * mirror), 5.5f, 6.f, 0, seed);
        farmBuilding(EK_BARN, barn, square(in * -1.f), 6.f + 1.5f * seed, 9.f + 2.f * seed, 0, h2((int)F.x + 1, (int)F.z));
        farmBuilding(EK_WAREHOUSE, shed, square(across * -mirror), 9.f + 2.f * seed, 7.f, 6.5f, seed);
        farmBuilding(EK_SILO, at(16.f, 30.f), 0.f, 3.f, 3.f, 15.f + 8.f * seed, seed);
        if (seed > 0.4f) farmBuilding(EK_SILO, at(23.f, 30.f), 0.f, 2.6f, 2.6f, 13.f, seed * 0.7f);
        if (seed < 0.6f) farmBuilding(EK_BARN, at(-24.f, 18.f), square(across * mirror), 5.f, 8.f, 0, h2((int)F.z, (int)F.x));
        // (in the yard: the farm's lorry, or a car by the house)
        const vec2 lorry = at(5.f, 6.f), car = at(-8.f, -10.f);
        if (C.inside(lorry.x, lorry.y) && seed > 0.35f) put(C, EK_TRUCK, lorry.x, ground(lorry.x, lorry.y), lorry.y, facing(lorry) + 1.3f, 1.f, 1.f, 1.f, seed);
        if (C.inside(car.x, car.y)) put(C, EK_CAR, car.x, ground(car.x, car.y), car.y, square(in), 1.f, 1.f, 1.f, h2((int)F.x - 3, (int)F.z));
      }
      // ---------------------------------------------------------------- road furniture (road_furniture.cpp)
      for (auto r = roadFurnitureIn(cx, cz); r.first != r.second; ++r.first) C.out[r.first->kind]->push_back(r.first->e);
      // ---------------------------------------------------------------- landmarks (landmarks.cpp)
      for (auto r = landmarksIn(cx, cz); r.first != r.second; ++r.first) if (r.first->kind > EK_SLAB) C.out[r.first->kind]->push_back(r.first->e);
      // ---------------------------------------------------------------- lighthouse
      { float lx, lz, ly; if (lighthouseSite(lx, lz, ly) && C.inside(lx, lz)) putBuilding(C, EK_LIGHTHOUSE, lx, lz, ly, 3.4f, 3.4f, 0, 0.5f); }
      // ---------------------------------------------------------------- sea stacks (70 m cells): off the cliffed shores
      // alone, close in (seaStackShore) - an open bay or a beach has none
      for (int sj = (int)floorf(C.z0 / STACK_CELL); sj <= (int)floorf((C.z1 - 1e-3f) / STACK_CELL); sj++)
        for (int si = (int)floorf(C.x0 / STACK_CELL); si <= (int)floorf((C.x1 - 1e-3f) / STACK_CELL); si++) {
          float x = (si + 0.5f + (h2(si + 17, sj) - 0.5f) * 0.4f) * STACK_CELL, z = (sj + 0.5f + (h2(si, sj + 23) - 0.5f) * 0.4f) * STACK_CELL;
          if (!C.inside(x, z)) continue;
          float g = ground(x, z);
          if (g > 0.3f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          if (m[3] < 0.5f) continue;
          float depthOk = smoothstepf(-16.f, -4.f, g) * smoothstepf(-0.5f, -2.5f, g);
          if (h2(si * 7 + 3, sj * 11 - 5) > 0.18f * depthOk) continue;
          if (!seaStackShore(x, z)) continue;
          float r = 7.f + 8.f * h2(si, sj + 31), H = 14.f + 30.f * h2(si + 5, sj + 9);
          put(C, EK_SEASTACK, x, g - 1.5f, z, h2(si - 3, sj + 3) * 6.28f, r / 10.f, (H - g + 1.5f) / 30.f, r / 10.f * (0.8f + 0.3f * h2(si, sj)), h2(si * 3, sj));
        }
      // ---------------------------------------------------------------- outcrops (64 m) and spires (160 m)
      const float OC = 64.f;
      for (int oj = (int)floorf(C.z0 / OC); oj <= (int)floorf((C.z1 - 1e-3f) / OC); oj++)
        for (int oi = (int)floorf(C.x0 / OC); oi <= (int)floorf((C.x1 - 1e-3f) / OC); oi++) {
          float hp = h2(oi * 5 + 9, oj * 7 - 1);
          if (hp > 0.4f) continue;
          float x = (oi + 0.5f + (h2(oi + 3, oj - 8) - 0.5f) * 0.7f) * OC, z = (oj + 0.5f + (h2(oi - 6, oj + 2) - 0.5f) * 0.7f) * OC;
          if (!C.inside(x, z) || g_world.underLake(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          if (b[1] < 0.01f) continue;
          float g = ground(x, z);
          if (g < 6.f) continue;
          float slope = (fabsf(ground(x + 10, z) - ground(x - 10, z)) + fabsf(ground(x, z + 10) - ground(x, z - 10))) / 20.f;
          float m[4]; g_world.sampleMask(x, z, m);
          float p = smoothstepf(70.f, 220.f, b[1]) * 0.22f + smoothstepf(1000.f, 1500.f, g) * 0.12f + smoothstepf(0.35f, 0.8f, slope) * 0.2f;
          p *= smoothstepf(12.f, 24.f, m[0] * ROAD_RANGE) * (1.f - smoothstepf(0.02f, 0.1f, m[1])) * (1.f - 0.8f * m[3]);
          if (hp > p) continue;
          float sc = 0.55f + 0.9f * h2(oi * 3, oj * 3 + 1);
          float yaw = h2(oi + 1, oj + 1) * 6.28f;
          put(C, EK_OUTCROP, x, footprintGround(x, z, yaw, 6.f * sc, 5.f * sc) - 1.2f * sc, z, yaw, sc, sc * (0.7f + 0.6f * h2(oi, oj + 9)), sc, h2(oi * 9, oj));
        }
      const float SPC = 160.f;
      for (int pj = (int)floorf(C.z0 / SPC); pj <= (int)floorf((C.z1 - 1e-3f) / SPC); pj++)
        for (int pi = (int)floorf(C.x0 / SPC); pi <= (int)floorf((C.x1 - 1e-3f) / SPC); pi++) {
          float hp = h2(pi * 3 - 11, pj * 13 + 4);
          if (hp > 0.3f) continue;
          float x = (pi + 0.5f + (h2(pi + 9, pj) - 0.5f) * 0.7f) * SPC, z = (pj + 0.5f + (h2(pi, pj + 9) - 0.5f) * 0.7f) * SPC;
          if (!C.inside(x, z) || g_world.underLake(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          float g = ground(x, z);
          float p = 0.3f * smoothstepf(110.f, 260.f, b[1]) * smoothstepf(0.45f, 0.75f, b[2]) * smoothstepf(40.f, 80.f, g) * smoothstepf(950.f, 700.f, g);
          float m[4]; g_world.sampleMask(x, z, m);
          p *= (1.f - smoothstepf(0.02f, 0.1f, m[1])) * smoothstepf(15.f, 30.f, m[0] * ROAD_RANGE);
          if (hp > p) continue;
          float sc = 0.55f + 0.75f * h2(pi * 7, pj * 7);
          float yaw = h2(pi - 2, pj + 2) * 6.28f;
          put(C, EK_SPIRE, x, footprintGround(x, z, yaw, 3.f * sc, 3.f * sc) - 1.f, z, yaw, sc, sc * (0.8f + 0.5f * h2(pi, pj - 4)), sc, h2(pi * 5, pj));
        }
    } else {
      // (each farm's compound - its yard and the buildings round it - and each landmark's clearing are kept clear of
      // trees, bushes and boulders)
      std::vector<vec3> clearings;   // (x, z, radius)
      for (const Farmstead& F : g_world.farms)
        if (F.x > C.x0 - 60.f && F.x < C.x1 + 60.f && F.z > C.z0 - 60.f && F.z < C.z1 + 60.f) clearings.push_back(vec3(F.x, F.z, 48.f));
      for (const vec3& c : landmarkClearings())
        if (c.x > C.x0 - c.z && c.x < C.x1 + c.z && c.y > C.z0 - c.z && c.y < C.z1 + c.z) clearings.push_back(vec3(c.x, c.y, c.z));
      auto inFarmstead = [&](float x, float z) { for (const vec3& f : clearings) if ((x - f.x) * (x - f.x) + (z - f.y) * (z - f.y) < f.z * f.z) return true; return false; };
      // ---------------------------------------------------------------- forests and lone trees (6 m cells)
      const float TC = 6.f;
      for (int tj = (int)floorf(C.z0 / TC); tj <= (int)floorf((C.z1 - 1e-3f) / TC); tj++)
        for (int ti = (int)floorf(C.x0 / TC); ti <= (int)floorf((C.x1 - 1e-3f) / TC); ti++) {
          float hp = h2(ti * 3 + 11, tj * 5 - 7);
          if (hp > 0.62f) continue;   // the most a cell can be planted
          float x = (ti + 0.5f + (h2(ti + 101, tj - 31) - 0.5f) * 0.7f) * TC, z = (tj + 0.5f + (h2(ti - 57, tj + 77) - 0.5f) * 0.7f) * TC;
          if (!C.inside(x, z) || inFarmstead(x, z) || g_world.underLake(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          if (b[1] < 2.5f) continue;   // airport grounds and flattened land
          float g = ground(x, z);
          if (g < 4.f) continue;
          float lush = b[2], cold = b[3], amp = b[1];
          float m[4]; g_world.sampleMask(x, z, m);
          float roadD = m[0] * ROAD_RANGE, town = m[1], farm = m[3];
          float fn = g_world.forestAt(x, z);
          float treeline = smoothstepf(1500.f - cold * 900.f, 1100.f - cold * 700.f, g);
          float fd = smoothstepf(0.42f - 0.1f * lush, 0.5f - 0.1f * lush, fn) * treeline;
          fd = std::max(fd, 0.022f * treeline);
          // farmland: the trees stand in the hedgerows between the fields (the terrain's patchwork: fieldPatch) and alone
          // here and there in a pasture, never out in a crop
          if (farm > 0.05f) {
            float border, crop; fieldPatch(x, z, border, crop);
            const float fdField = (border < 0.03f ? 0.6f : crop >= 0.76f ? 0.02f : 0.f) * treeline;
            fd += (fdField - fd) * smoothstepf(0.12f, 0.4f, farm);
          }
          fd *= smoothstepf(4.f, 9.f, g) * smoothstepf(2.5f, 8.f, amp) * smoothstepf(9.f, 18.f, roadD) * (1.f - smoothstepf(0.03f, 0.2f, town));
          if (hp > fd * 0.56f) continue;
          float sp = h2(ti * 13 + 1, tj * 7 + 3), sp2 = h2(ti * 5 - 9, tj * 11 + 2), hv = h2(ti - 3, tj + 19);
          int k = treeSpecies(g, lush, cold, sp, sp2);
          // forest-interior trees grow taller, lone trees spread wider
          float sc = (0.72f + 0.5f * hv) * (k == EK_PALM ? 1.f : lerpf(0.9f, 1.08f, fd));
          float sy = sc * (0.88f + 0.24f * h2(ti + 7, tj - 7));
          put(C, k, x, g - 0.15f, z, h2(ti * 7, tj * 3) * 6.2832f, sc, sy, sc * (0.9f + 0.2f * h2(ti, tj + 5)), h2(ti * 17 + 3, tj * 19 + 5));
        }
      // ---------------------------------------------------------------- bushes (8 m cells)
      const float BC = 8.f;
      for (int bj = (int)floorf(C.z0 / BC); bj <= (int)floorf((C.z1 - 1e-3f) / BC); bj++)
        for (int bi = (int)floorf(C.x0 / BC); bi <= (int)floorf((C.x1 - 1e-3f) / BC); bi++) {
          float hp = h2(bi * 9 - 4, bj * 7 + 13);
          if (hp > 0.9f) continue;   // (the most a cell can be planted: a hedgerow's; elsewhere at most 0.3, as before)
          float x = (bi + 0.5f + (h2(bi + 41, bj - 3) - 0.5f) * 0.8f) * BC, z = (bj + 0.5f + (h2(bi - 8, bj + 33) - 0.5f) * 0.8f) * BC;
          if (!C.inside(x, z) || inFarmstead(x, z) || g_world.underLake(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          if (b[1] < 2.5f) continue;
          float g = ground(x, z);
          if (g < 3.f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          float treeline = smoothstepf(1700.f - b[3] * 900.f, 1200.f - b[3] * 700.f, g);
          float fn = g_world.forestAt(x, z);
          float edge = smoothstepf(0.3f, 0.42f, fn) * smoothstepf(0.6f, 0.46f, fn);   // forest margins are scrubby
          float p = (0.05f + 0.25f * edge) * (0.5f + 0.5f * b[2]);
          if (m[3] > 0.05f) {   // (farmland: the hedgerows are thick with them, the fields clear)
            float border, crop; fieldPatch(x, z, border, crop);
            p += ((border < 0.035f ? 0.8f : crop >= 0.76f ? 0.01f : 0.f) - p) * smoothstepf(0.12f, 0.4f, m[3]);
          }
          p *= treeline * smoothstepf(2.5f, 8.f, b[1]) * smoothstepf(8.f, 14.f, m[0] * ROAD_RANGE) * (1.f - smoothstepf(0.03f, 0.2f, m[1]));
          if (hp > p) continue;
          float sc = 0.6f + 0.8f * h2(bi * 3, bj * 5);
          put(C, EK_BUSH, x, g - 0.1f, z, h2(bi, bj * 3) * 6.28f, sc, sc * (0.8f + 0.4f * h2(bi + 1, bj)), sc, h2(bi * 7, bj * 13));
        }
      // ---------------------------------------------------------------- boulders (16 m cells)
      for (int rj = (int)floorf(C.z0 / ROCK_CELL); rj <= (int)floorf((C.z1 - 1e-3f) / ROCK_CELL); rj++)
        for (int ri = (int)floorf(C.x0 / ROCK_CELL); ri <= (int)floorf((C.x1 - 1e-3f) / ROCK_CELL); ri++) {
          float hp = h2(ri * 5 - 13, rj * 3 + 29);
          if (hp > 0.3f) continue;
          float x = (ri + 0.5f + (h2(ri + 41, rj - 9) - 0.5f) * 0.6f) * ROCK_CELL, z = (rj + 0.5f + (h2(ri - 21, rj + 63) - 0.5f) * 0.6f) * ROCK_CELL;
          if (!C.inside(x, z) || inFarmstead(x, z) || g_world.underLake(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          if (b[1] < 0.01f) continue;
          float g = ground(x, z);
          if (g < 0.4f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          float farm = m[3];
          float rdn = smoothstepf(60.f, 200.f, b[1]) * 0.25f + smoothstepf(900.f, 1400.f, g) * 0.12f + smoothstepf(3.f, 0.6f, g) * 0.06f * (1.f - farm);
          rdn *= smoothstepf(10.f, 20.f, m[0] * ROAD_RANGE) * (1.f - smoothstepf(0.03f, 0.2f, m[1]));
          if (hp > rdn * 0.6f) continue;
          float hr = h2(ri + 7, rj - 77), kind = h2(ri * 3, rj * 7);
          int k = kind < 0.5f ? EK_BOULDER : kind < 0.8f ? EK_BLOCK : EK_SLAB;
          float r = 0.8f + 3.6f * hr * hr;
          float sc = r / kEntInfo[k].hx, sy = sc * (0.75f + 0.4f * h2(ri, rj + 3));
          put(C, k, x, g - 0.22f * kEntInfo[k].h * sy, z, h2(ri - 5, rj) * 6.28f, sc, sy, sc * (0.8f + 0.4f * h2(ri + 2, rj)), h2(ri * 11, rj * 13));
        }
      // ---------------------------------------------------------------- landmarks' stones (landmarks.cpp: the rocks
      // kinds a chunk drops when it is trimmed back, so with its other level-2 scenery)
      for (auto r = landmarksIn(cx, cz); r.first != r.second; ++r.first) if (r.first->kind <= EK_SLAB) C.out[r.first->kind]->push_back(r.first->e);
    }
  }
  // nothing grows or stands up into a bridge: what is under a deck's footprint and would reach its girders goes (the
  // ground under a bridge is left as it was, so the masks keep nothing off it)
  if (!g_world.bridges.empty())
    for (int k = 0; k < EK_COUNT; k++)
      lists[k].erase(std::remove_if(lists[k].begin(), lists[k].end(), [&](const Ent& e) {
        if (k == EK_GUARDRAIL) return false;   // (laid up to a deck's joint, at the road's height: the parapets carry on)
        float under;
        const float R = std::max(kEntInfo[k].hx * e.sx, kEntInfo[k].hz * e.sz);
        return bridgeOver(g_world.bridges, e.x, e.z, R, &under) && e.y + kEntInfo[k].h * e.sy > under - 0.5f;
      }), lists[k].end());
  ch.ents.clear();
  // The per-kind lists already know the final size. Reserve once before concatenation so streaming a dense
  // chunk does not repeatedly copy its instances or retain a doubled growth allocation.
  size_t entityCount = 0;
  for (const auto& list : lists) entityCount += list.size();
  ch.ents.reserve(entityCount);
  ch.ymin = 1e9f; ch.ymax = -1e9f; ch.reach = 0; ch.hmax = 0;
  const float x0 = chunkX0(cx), z0 = chunkX0(cz);
  for (int k = 0; k < EK_COUNT; k++) {
    // trees, bushes and small rocks in ascending order of their thinning key, so the renderer keeps a prefix of the
    // list when it thins a whole chunk out with distance (see entThinKey)
    if (entThins(k)) std::sort(lists[k].begin(), lists[k].end(), [](const Ent& a, const Ent& b) { return entThinKey(a) < entThinKey(b); });
    ch.off[k] = (uint32_t)ch.ents.size();
    for (const Ent& e : lists[k]) {
      ch.ents.push_back(e);
      ch.ymin = std::min(ch.ymin, e.y); ch.ymax = std::max(ch.ymax, e.y + kEntInfo[k].h * e.sy);
      ch.hmax = std::max(ch.hmax, kEntInfo[k].h * e.sy);
      float R = hypotf(kEntInfo[k].hx * e.sx, kEntInfo[k].hz * e.sz);   // (a big terminal reaches well into the next chunk)
      if (k == EK_POLE && e.seed < 1.5f) R = std::max(R, 50.f * e.sz + 1.f);   // (a pole's wires, to the next pole)
      ch.reach = std::max({ch.reach, x0 - (e.x - R), e.x + R - (x0 + CH), z0 - (e.z - R), e.z + R - (z0 + CH)});
    }
  }
  ch.off[EK_COUNT] = (uint32_t)ch.ents.size();
  if (ch.ents.empty()) { ch.ymin = 0; ch.ymax = 0; }
  ch.level = std::max(ch.level, level);
  ch.markChanged();
}

void Scenery::trim(vec3 cam, float keepDetail, float keepAll, int frame) {
  if (chunks.empty()) return;
  for (int cz = 0; cz < NC; cz++)
    for (int cx = 0; cx < NC; cx++) {
      auto& p = chunks[(size_t)cz * NC + cx];
      if (!p) continue;
      float x = chunkX0(cx) + CH * 0.5f, z = chunkX0(cz) + CH * 0.5f;
      const float ddx = g_world.wrap(x - cam.x), ddz = g_world.wrap(z - cam.z);   // (the short way: a chunk the far side of the map's seam is drawn there too)
      float d = sqrtf(ddx * ddx + ddz * ddz);
      if (d > keepAll && frame - p->lastUse > 120) { p.reset(); continue; }
      if (d > keepDetail && p->level >= 2 && frame - p->lastUse > 120) {   // (not a chunk prefetched for where the camera goes next)
        // drop the level-2 kinds (trees, bushes, boulders)
        std::vector<Ent> keep; uint32_t off[EK_COUNT + 1];
        for (int k = 0; k < EK_COUNT; k++) {
          off[k] = (uint32_t)keep.size();
          if (k > EK_SLAB) keep.insert(keep.end(), p->ents.begin() + p->off[k], p->ents.begin() + p->off[k + 1]);
        }
        off[EK_COUNT] = (uint32_t)keep.size();
        p->ents.swap(keep); memcpy(p->off, off, sizeof off); p->level = 1; p->markChanged();
      }
    }
}

// ------------------------------------------------------------------ collisions
// Entities belong to the chunk holding their centre, but a large one (an airport terminal) reaches into its
// neighbours. Queries visit every chunk within the largest reach any entity can have (entityPad: natural scenery stays
// within 20 m, the airport buildings are measured from the layouts), and skip each chunk the query doesn't come within
// that chunk's own reach of.
static float outside(float v, float lo, float hi) { return v < lo ? lo - v : v > hi ? v - hi : 0.f; }
// Convert only inside the finite local chunk domain. Ocean queries can be many
// charts away: reject them before integer conversion and never enumerate empty
// coordinates between a distant query and the current chart.
static int boundedChunk(float v) {
  if (!(v >= -WORLD_HALF)) return -1;
  if (v >= Scenery::chunkX0(Scenery::NC)) return Scenery::NC;
  return Scenery::chunkOf(v);
}
static float entityPad() {
  EntityRegionCache& cache = *regionCache();
  std::call_once(cache.padOnce, [&] {
    std::vector<AptItem> items;
    for (int i = 0; i < (int)g_world.airports.size(); i++) {
      items.clear(); airportItems(i, items);
      for (auto& it : items) cache.pad = std::max(cache.pad, hypotf(kEntInfo[it.kind].hx * it.e.sx, kEntInfo[it.kind].hz * it.e.sz) + 2.f);
    }
  });
  return cache.pad;
}
// what an approach has to clear besides the ground: the tops of the trees and buildings near a point on its path
float Scenery::obstacleTop(float x, float z, float r) {
  float top = -1e9f;
  const float pad = entityPad();
  const int z0 = std::max(0, boundedChunk(z - r - pad)), z1 = std::min(NC - 1, boundedChunk(z + r + pad));
  const int x0 = std::max(0, boundedChunk(x - r - pad)), x1 = std::min(NC - 1, boundedChunk(x + r + pad));
  for (int cz = z0; cz <= z1; cz++)
    for (int cx = x0; cx <= x1; cx++) {
      Chunk* ch = ensure(cx, cz, 2);
      if (!ch || ch->ents.empty() || ch->ymax <= top) continue;
      for (int k = 0; k < EK_COUNT; k++) {
        if (k == EK_RWYLIGHT || k == EK_PAPI) continue;   // (frangible airport fixtures)
        const EntKindInfo& I = kEntInfo[k];
        for (uint32_t i = ch->off[k]; i < ch->off[k + 1]; i++) {
          const Ent& e = ch->ents[i];
          const float R = hypotf(I.hx * e.sx, I.hz * e.sz) + r, dx = x - e.x, dz = z - e.z;
          if (dx * dx + dz * dz < R * R && !destroyed(e)) top = std::max(top, e.y + I.h * e.sy);
        }
      }
    }
  return top;
}
int Scenery::collide(vec3 p, float r, Ent* entOut) {
  if (bridgeCollide(g_world.bridges, p, r)) return kBridgeKind + 1;   // (the road network's bridges: no entity, entOut unset)
  float pad = entityPad();
  const int c0x = std::max(0, boundedChunk(p.x - r - pad)), c1x = std::min(NC - 1, boundedChunk(p.x + r + pad));
  const int c0z = std::max(0, boundedChunk(p.z - r - pad)), c1z = std::min(NC - 1, boundedChunk(p.z + r + pad));
  for (int cz = c0z; cz <= c1z; cz++)
    for (int cx = c0x; cx <= c1x; cx++) {
      float ox = outside(p.x, chunkX0(cx), chunkX0(cx) + CH), oz = outside(p.z, chunkX0(cz), chunkX0(cz) + CH);
      if (ox * ox + oz * oz > (pad + r) * (pad + r)) continue;
      Chunk* ch = ensure(cx, cz, 2);
      if (!ch || ch->ents.empty() || p.y - r > ch->ymax || p.y + r < ch->ymin) continue;
      if (ox * ox + oz * oz > (ch->reach + r) * (ch->reach + r) && (ox > 0 || oz > 0)) continue;   // (beyond this chunk's own reach)
      for (int k = 0; k < EK_COUNT; k++) {
        if (k == EK_RWYLIGHT || k == EK_PAPI) continue;   // frangible airport fixtures
        const EntKindInfo& I = kEntInfo[k];
        int cls = entClass(k);
        for (uint32_t i = ch->off[k]; i < ch->off[k + 1]; i++) {
          const Ent& e = ch->ents[i];
          float dx = p.x - e.x, dz = p.z - e.z, ly = p.y - e.y;
          float R = hypotf(I.hx * e.sx, I.hz * e.sz);   // the circle through the footprint's corners
          if (dx * dx + dz * dz > (R + r) * (R + r) || ly < -r - 1.f || ly > I.h * e.sy + r) continue;
          if (destroyed(e)) continue;
          if (cls == EC_TREE) {
            // the crown (trunks alone are too thin to matter at flying speeds)
            float crownR = I.hx * e.sx * 0.8f, y0 = I.h * e.sy * (k == EK_PALM ? 0.7f : k == EK_PINE ? 0.5f : 0.22f);
            if (k == EK_BUSH) y0 = 0;
            if (ly > y0 - r && dx * dx + dz * dz < (crownR + r) * (crownR + r)) { if (entOut) *entOut = e; return k + 1; }
          } else if (cls == EC_ROCK) {
            float rr = I.hx * e.sx * 0.85f, hh = I.h * e.sy * 0.92f;
            float q = (dx * dx + dz * dz) / ((rr + r) * (rr + r)) + (ly * ly) / ((hh + r) * (hh + r));
            if (q < 1.f) { if (entOut) *entOut = e; return k + 1; }
          } else {
            float c = cosf(e.yaw), s = sinf(e.yaw);
            float lx = c * dx - s * dz, lz = s * dx + c * dz;
            if (fabsf(lx) < I.hx * e.sx + r && fabsf(lz) < I.hz * e.sz + r && ly < I.h * e.sy + r * 0.5f) { if (entOut) *entOut = e; return k + 1; }
          }
        }
      }
    }
  return 0;
}

// Segment against the same shapes collide() uses (tree crown cylinders, rock ellipsoids, building boxes), inflated by
// the bolt radius, solved exactly: nothing between samples can be missed.
float Scenery::raycast(vec3 a, vec3 d, float L, int* kindOut, Ent* entOut) {
  return sweepSphere(a, d, L, 0.4f, kindOut, entOut);
}

float Scenery::sweepSphere(vec3 a, vec3 d, float L, float r, int* kindOut, Ent* entOut) {
  if (!std::isfinite(L) || L < 0 || !std::isfinite(r) || r < 0 || r > 1000.f) return -1.f;
  for (int i = 0; i < 3; ++i) if (!std::isfinite(a[i]) || !std::isfinite(d[i])) return -1.f;
  vec3 b = a + d * L;
  // Endpoint ground heights cannot reject a ridge or a tall building between them.
  // Per-chunk vertical bounds below provide the safe early rejection instead.
  // slab test of the segment against an axis-aligned box in some frame (o, dir given in that frame)
  auto slab = [&](vec3 o, vec3 dir, vec3 mn, vec3 mx, float& t0) {
    float tn = 0.f, tf = L;
    for (int k = 0; k < 3; k++) {
      float oo = k == 0 ? o.x : k == 1 ? o.y : o.z, dd = k == 0 ? dir.x : k == 1 ? dir.y : dir.z;
      float lo = k == 0 ? mn.x : k == 1 ? mn.y : mn.z, hi = k == 0 ? mx.x : k == 1 ? mx.y : mx.z;
      if (fabsf(dd) < 1e-8f) { if (oo < lo || oo > hi) return false; continue; }
      float t1 = (lo - oo) / dd, t2 = (hi - oo) / dd;
      if (t1 > t2) std::swap(t1, t2);
      tn = std::max(tn, t1); tf = std::min(tf, t2);
      if (tn > tf) return false;
    }
    t0 = tn; return true;
  };
  float best = -1.f; int bestK = 0; Ent bestE{};
  float pad = entityPad() + r;   // (chunks and reach: see collide)
  float sx0 = std::min(a.x, b.x), sx1 = std::max(a.x, b.x), sz0 = std::min(a.z, b.z), sz1 = std::max(a.z, b.z);
  float ylo = std::min(a.y, b.y) - r, yhi = std::max(a.y, b.y) + r;
  const int c0z = std::max(0, boundedChunk(sz0 - pad)), c1z = std::min(NC - 1, boundedChunk(sz1 + pad));
  const int c0x = std::max(0, boundedChunk(sx0 - pad)), c1x = std::min(NC - 1, boundedChunk(sx1 + pad));
  for (int cz = c0z; cz <= c1z; cz++)
    for (int cx = c0x; cx <= c1x; cx++) {
      float cx0 = chunkX0(cx), cz0 = chunkX0(cz);
      float gx = std::max({cx0 - sx1, sx0 - (cx0 + CH), 0.f}), gz = std::max({cz0 - sz1, sz0 - (cz0 + CH), 0.f});   // box gap
      if (gx * gx + gz * gz > pad * pad) continue;
      Chunk* ch = ensure(cx, cz, 2);
      if (!ch || ch->ents.empty() || ylo > ch->ymax || yhi < ch->ymin) continue;
      if (gx * gx + gz * gz > (ch->reach + r) * (ch->reach + r)) continue;
      for (int k = 0; k < EK_COUNT; k++) {
        if (k == EK_RWYLIGHT || k == EK_PAPI) continue;
        const EntKindInfo& I = kEntInfo[k];
        int cls = entClass(k);
        for (uint32_t i = ch->off[k]; i < ch->off[k + 1]; i++) {
          const Ent& e = ch->ents[i];
          float R = hypotf(I.hx * e.sx, I.hz * e.sz) + r, H = I.h * e.sy;
          // quick reject: horizontal distance from the segment, vertical range
          vec3 rel = vec3(e.x, 0, e.z) - vec3(a.x, 0, a.z);
          vec3 dh(d.x, 0, d.z); float dh2 = dot(dh, dh);
          float th = dh2 > 1e-8f ? clampf(dot(rel, dh) / dh2, 0.f, L) : 0.f;
          vec3 cp = vec3(a.x, 0, a.z) + dh * th;
          if ((cp.x - e.x) * (cp.x - e.x) + (cp.z - e.z) * (cp.z - e.z) > R * R || yhi < e.y - 1.f || ylo > e.y + H + r) continue;
          vec3 o = a - vec3(e.x, e.y, e.z);
          float t = -1.f;
          if (cls == EC_TREE) {   // crown: vertical cylinder from y0 to the top
            float cr = I.hx * e.sx * 0.8f + r, y0 = H * (k == EK_PALM ? 0.7f : k == EK_PINE ? 0.5f : 0.22f) - r;
            if (k == EK_BUSH) y0 = -r;
            float A = d.x * d.x + d.z * d.z, B = o.x * d.x + o.z * d.z, C = o.x * o.x + o.z * o.z - cr * cr;
            float tn = 0.f, tf = L;
            if (A > 1e-8f) { float disc = B * B - A * C; if (disc < 0) continue; float sq = sqrtf(disc); tn = std::max(tn, (-B - sq) / A); tf = std::min(tf, (-B + sq) / A); }
            else if (C > 0) continue;
            if (fabsf(d.y) > 1e-8f) { float t1 = (y0 - o.y) / d.y, t2 = (H + r - o.y) / d.y; if (t1 > t2) std::swap(t1, t2); tn = std::max(tn, t1); tf = std::min(tf, t2); }
            else if (o.y < y0 || o.y > H + r) continue;
            if (tn <= tf) t = tn;
          } else if (cls == EC_ROCK) {   // ellipsoid about the base point
            float rr = I.hx * e.sx * 0.85f + r, hh = H * 0.92f + r;
            vec3 os(o.x / rr, o.y / hh, o.z / rr), ds(d.x / rr, d.y / hh, d.z / rr);
            float A = dot(ds, ds), B = dot(os, ds), C = dot(os, os) - 1.f, disc = B * B - A * C;
            if (disc < 0) continue;
            float t1 = (-B - sqrtf(disc)) / A, t2 = (-B + sqrtf(disc)) / A;
            if (t2 < 0 || t1 > L) continue;
            t = std::max(t1, 0.f);
          } else {   // building: oriented box in its own frame
            float c = cosf(e.yaw), s = sinf(e.yaw);
            vec3 lo(c * o.x - s * o.z, o.y, s * o.x + c * o.z), ld(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
            float t0;
            if (slab(lo, ld, vec3(-I.hx * e.sx - r, -1.f - r, -I.hz * e.sz - r), vec3(I.hx * e.sx + r, H + r, I.hz * e.sz + r), t0)) t = t0;
          }
          if (t < 0.f || t > L || (best >= 0.f && t >= best) || destroyed(e)) continue;
          best = t; bestK = k; bestE = e;
        }
      }
    }
  if (best >= 0.f) { if (kindOut) *kindOut = bestK + 1; if (entOut) *entOut = bestE; }
  // the road network's bridges stop it too (nearer than any entity: kindOut 0, no entity to damage)
  const float tb = bridgeRaycast(g_world.bridges, a, d, L);
  if (tb >= 0.f && (best < 0.f || tb < best)) { best = tb; if (kindOut) *kindOut = 0; }
  return best;
}
