// Air Xpress - environment entities: deterministic placement, chunk streaming and collisions
#include "entities.h"
#include "scenery.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

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
  int epoch = 0;                       // bumped by clear(): results from before it are dropped
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
        }
        generate(j.c, j.cx, j.cz, j.level);
        std::lock_guard<std::mutex> lk(A.m);
        A.done.push_back(std::move(j));
      }
    });
}

Scenery::~Scenery() {
  { std::lock_guard<std::mutex> lk(async->m); async->stop = true; }
  async->cv.notify_all();
  for (auto& t : async->threads) t.join();
}

int Scenery::workers() const { return (int)async->threads.size(); }

void Scenery::clear() {
  chunks.clear();
  std::lock_guard<std::mutex> lk(async->m);
  async->todo.clear(); async->busy.clear(); async->epoch++;
}

bool Scenery::request(int cx, int cz, int level) {
  Async& A = *async;
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
  {"Tower", 10.f, 80.f, 10.f}, {"Skyscraper", 9.0f, 135.f, 9.0f}, {"Warehouse", 12.f, 8.5f, 9.0f}, {"Barn", 6.0f, 10.f, 9.0f},
  {"Silo", 3.0f, 19.f, 3.0f}, {"Church", 5.0f, 28.f, 13.f}, {"Water tower", 5.2f, 28.f, 5.2f}, {"Lighthouse", 3.4f, 27.f, 3.4f},
  {"Gas station", 8.0f, 5.5f, 7.0f}, {"Runway light", 0.12f, 0.42f, 0.12f}, {"PAPI unit", 0.45f, 0.8f, 0.35f},
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
  wrecked[chunkOf(e.z) * NC + chunkOf(e.x)].push_back(k);
  wreckRev++;
  return true;
}

// ------------------------------------------------------------------ placement helpers
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
void putBuilding(Ctx& C, int kind, float x, float z, float yaw, float hw, float hd, float height, float seed) {
  const EntKindInfo& I = kEntInfo[kind];
  float sx = clampf(hw / I.hx, 0.7f, 1.3f), sz = clampf(hd / I.hz, 0.7f, 1.3f);
  float sy = height > 0 ? clampf(height / I.h, 0.65f, 1.7f) : 0.9f + 0.2f * seed;
  float g = footprintGround(x, z, yaw, I.hx * sx, I.hz * sz);
  put(C, kind, x, g - 0.05f, z, yaw, sx, sy, sz, seed);
}

// The lighthouse stands on the shore of Lighthouse Key: the low land point nearest the sea around the village
bool lighthouseSite(float& lx, float& lz, float& yaw) {
  static int state = 0; static float sx = 0, sz = 0, sy = 0;
  if (!state) {
    state = 2;
    const Town* T = nullptr;
    for (int i = 0; i < kNumTowns; i++) if (!strcmp(kTowns[i].name, "Lighthouse Key")) T = &kTowns[i];
    float best = 1e9f;
    if (T)
      for (float r = 80.f; r <= 700.f; r += 20.f)
        for (int a = 0; a < 72; a++) {
          float ang = a * 5.f * DEG, dx = cosf(ang), dz = sinf(ang);
          float x = T->x + dx * r, z = T->z + dz * r, g = ground(x, z);
          if (g < 3.f || g > 16.f || ground(x + dx * 45.f, z + dz * 45.f) > -0.5f) continue;
          float score = r * 0.02f + fabsf(g - 7.f);
          if (score < best) { best = score; sx = x; sz = z; sy = atan2f(dx, dz); state = 1; }
        }
  }
  lx = sx; lz = sz; yaw = sy;
  return state == 1;
}

// tree species for a site
int treeSpecies(float g, float lush, float cold, float sp, float sp2) {
  if (cold > 0.45f || g > 650.f) return sp2 < 0.45f ? EK_FIR : sp2 < 0.85f ? EK_SPRUCE : EK_PINE;
  if (lush > 0.85f && g < 70.f && sp > 0.35f) return EK_PALM;
  if (sp < 0.18f) return sp2 < 0.5f ? EK_FIR : EK_PINE;
  return sp2 < 0.58f ? EK_OAK : sp2 < 0.84f ? EK_BIRCH : sp2 < 0.9f ? EK_PINE : EK_FIR;
}
}  // namespace

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
    // ---------------------------------------------------------------- town lots (buildings L1, garden trees L2)
    int i0 = (int)floorf(C.x0 / LOT) - 1, i1 = (int)floorf(C.x1 / LOT) + 1, j0 = (int)floorf(C.z0 / LOT) - 1, j1 = (int)floorf(C.z1 / LOT) + 1;
    for (int j = j0; j <= j1; j++)
      for (int i = i0; i <= i1; i++) {
        Lot lot;
        if (!g_world.lotAt(i, j, lot) || !C.inside(lot.cx, lot.cz)) continue;
        float m[4]; g_world.maskTexel(lot.cx, lot.cz, m);
        float urban = m[2], roadD = m[0] * ROAD_RANGE;
        float hk = h2(i * 31 + 7, j * 17 - 3), hk2 = h2(i * 13 - 5, j * 29 + 11);
        float yaw = (j & 1) ? 0.f : PI;   // front towards the street on this lot's z side (streets on even z lines)
        int kind;
        float height = 0;
        if (lot.type == 1) {
          float Ht = lot.wallH;
          if (Ht > 38.f) { kind = hk < 0.4f ? EK_SKYSCRAPER : EK_TOWER; height = kind == EK_SKYSCRAPER ? Ht * 2.6f : Ht * 1.7f; }
          else if (Ht > 19.f) { kind = hk < 0.5f ? EK_OFFICE : EK_APARTMENT; height = kind == EK_OFFICE ? Ht * 1.15f : Ht; }
          else { kind = hk < 0.4f ? EK_SHOP : hk < 0.75f ? EK_TOWNHOUSE : EK_APARTMENT; height = kind == EK_APARTMENT ? std::max(Ht, 13.f) : 0.f; }
        } else {
          if (hk < 0.006f && m[1] > 0.25f) kind = EK_CHURCH;
          else if (hk < 0.011f && urban > 0.08f) kind = EK_WATERTOWER;
          else if (roadD < 34.f && hk < 0.1f) kind = EK_GASSTATION;
          else if (urban > 0.2f) kind = hk < 0.35f ? EK_TOWNHOUSE : hk < 0.58f ? EK_SHOP : hk < 0.8f ? EK_HOUSE : EK_HOUSE_L;
          else kind = hk < 0.38f ? EK_HOUSE : hk < 0.66f ? EK_HOUSE_HIP : hk < 0.88f ? EK_HOUSE_L : EK_FARMHOUSE;
        }
        bool house = kind == EK_HOUSE || kind == EK_HOUSE_HIP || kind == EK_HOUSE_L || kind == EK_FARMHOUSE;
        if (L == 1) {
          float hw = lot.hw, hd = lot.hd;
          if (kind == EK_CHURCH || kind == EK_WATERTOWER || kind == EK_GASSTATION) { hw = kEntInfo[kind].hx; hd = kEntInfo[kind].hz; }
          if (kind == EK_TOWNHOUSE || kind == EK_SHOP) { hw = std::max(hw, 7.f); hd = std::max(hd, 5.5f); }
          putBuilding(C, kind, lot.cx, lot.cz, yaw, hw, hd, height, lot.seed);
        } else if (house && lot.type == 0) {
          // garden trees behind the house
          for (int t = 0; t < 2; t++) {
            float ht = h2(i * 5 + t * 71, j * 3 - t * 13);
            if (ht > (t ? 0.25f : 0.6f)) continue;
            float side = h2(i + t, j - 9) < 0.5f ? -1.f : 1.f;
            float lx = side * (lot.hw + 2.5f + 3.f * ht), lz = -(lot.hd + 3.f + 2.f * hk2);
            float c = cosf(yaw), s = sinf(yaw);
            float x = lot.cx + c * lx + s * lz, z = lot.cz - s * lx + c * lz;
            float b[4]; g_world.sampleBase(x, z, b);
            int sp = b[3] > 0.45f ? EK_SPRUCE : (ht < 0.3f ? EK_BIRCH : EK_OAK);
            float sc = 0.55f + 0.35f * h2(i - t * 7, j + 41);
            put(C, sp, x, ground(x, z) - 0.15f, z, ht * 40.f, sc, sc * (0.9f + 0.2f * hk2), sc, h2(i * 3 + t, j * 7));
          }
        }
      }

    if (L == 1) {
      // ---------------------------------------------------------------- farmsteads on farmland (320 m grid)
      const float FC = 320.f;
      for (int fj = (int)floorf(C.z0 / FC) - 1; fj <= (int)floorf(C.z1 / FC); fj++)
        for (int fi = (int)floorf(C.x0 / FC) - 1; fi <= (int)floorf(C.x1 / FC); fi++) {
          float x = (fi + 0.5f + (h2(fi * 7 + 1, fj * 3 - 2) - 0.5f) * 0.6f) * FC, z = (fj + 0.5f + (h2(fi - 4, fj * 9 + 5) - 0.5f) * 0.6f) * FC;
          if (!C.inside(x, z) || h2(fi * 11 + 3, fj * 5 + 7) > 0.42f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          if (m[3] < 0.45f || m[1] > 0.01f || m[0] * ROAD_RANGE < 30.f) continue;
          float g = ground(x, z);
          if (g < 4.f || fabsf(ground(x + 25, z) - ground(x - 25, z)) > 6.f || fabsf(ground(x, z + 25) - ground(x, z - 25)) > 6.f) continue;
          float yaw = h2(fi + 13, fj - 17) * 2.f * PI, c = cosf(yaw), s = sinf(yaw);
          auto at = [&](float lx, float lz, float& wx, float& wz) { wx = x + c * lx + s * lz; wz = z - s * lx + c * lz; };
          float seed = h2(fi * 3, fj * 5), wx, wz;
          putBuilding(C, EK_FARMHOUSE, x, z, yaw, 5.5f, 6.f, 0, seed);
          at(24.f, -8.f, wx, wz); putBuilding(C, EK_BARN, wx, wz, yaw + PI * 0.5f, 6.f + 1.5f * seed, 9.f + 2.f * seed, 0, h2(fi, fj + 1));
          at(36.f, 6.f, wx, wz); putBuilding(C, EK_SILO, wx, wz, yaw, 3.f, 3.f, 15.f + 8.f * seed, seed);
          if (seed > 0.45f) { at(29.f, 9.f, wx, wz); putBuilding(C, EK_SILO, wx, wz, yaw, 2.6f, 2.6f, 13.f, seed * 0.7f); }
          if (h2(fi - 3, fj + 8) > 0.45f) { at(-6.f, -26.f, wx, wz); putBuilding(C, EK_WAREHOUSE, wx, wz, yaw, 9.f, 7.f, 6.5f, seed); }
        }
      // ---------------------------------------------------------------- lighthouse
      { float lx, lz, ly; if (lighthouseSite(lx, lz, ly) && C.inside(lx, lz)) putBuilding(C, EK_LIGHTHOUSE, lx, lz, ly, 3.4f, 3.4f, 0, 0.5f); }
      // ---------------------------------------------------------------- sea stacks (70 m cells)
      for (int sj = (int)floorf(C.z0 / STACK_CELL); sj <= (int)floorf((C.z1 - 1e-3f) / STACK_CELL); sj++)
        for (int si = (int)floorf(C.x0 / STACK_CELL); si <= (int)floorf((C.x1 - 1e-3f) / STACK_CELL); si++) {
          float x = (si + 0.5f + (h2(si + 17, sj) - 0.5f) * 0.4f) * STACK_CELL, z = (sj + 0.5f + (h2(si, sj + 23) - 0.5f) * 0.4f) * STACK_CELL;
          if (!C.inside(x, z)) continue;
          float g = ground(x, z);
          if (g > 0.3f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          if (m[3] < 0.5f) continue;
          float depthOk = smoothstepf(-16.f, -4.f, g) * smoothstepf(-0.5f, -2.5f, g);
          if (h2(si * 7 + 3, sj * 11 - 5) > 0.025f * depthOk) continue;
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
          if (!C.inside(x, z)) continue;
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
          if (!C.inside(x, z)) continue;
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
      // ---------------------------------------------------------------- forests and lone trees (6 m cells)
      const float TC = 6.f;
      for (int tj = (int)floorf(C.z0 / TC); tj <= (int)floorf((C.z1 - 1e-3f) / TC); tj++)
        for (int ti = (int)floorf(C.x0 / TC); ti <= (int)floorf((C.x1 - 1e-3f) / TC); ti++) {
          float hp = h2(ti * 3 + 11, tj * 5 - 7);
          if (hp > 0.62f) continue;   // the most a cell can be planted
          float x = (ti + 0.5f + (h2(ti + 101, tj - 31) - 0.5f) * 0.7f) * TC, z = (tj + 0.5f + (h2(ti - 57, tj + 77) - 0.5f) * 0.7f) * TC;
          if (!C.inside(x, z)) continue;
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
          fd *= smoothstepf(4.f, 9.f, g) * smoothstepf(2.5f, 8.f, amp) * smoothstepf(9.f, 18.f, roadD) * (1.f - smoothstepf(0.03f, 0.2f, town)) * (1.f - 0.9f * farm);
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
          if (hp > 0.3f) continue;
          float x = (bi + 0.5f + (h2(bi + 41, bj - 3) - 0.5f) * 0.8f) * BC, z = (bj + 0.5f + (h2(bi - 8, bj + 33) - 0.5f) * 0.8f) * BC;
          if (!C.inside(x, z)) continue;
          float b[4]; g_world.sampleBase(x, z, b);
          if (b[1] < 2.5f) continue;
          float g = ground(x, z);
          if (g < 3.f) continue;
          float m[4]; g_world.sampleMask(x, z, m);
          float treeline = smoothstepf(1700.f - b[3] * 900.f, 1200.f - b[3] * 700.f, g);
          float fn = g_world.forestAt(x, z);
          float edge = smoothstepf(0.3f, 0.42f, fn) * smoothstepf(0.6f, 0.46f, fn);   // forest margins are scrubby
          float p = (0.05f + 0.25f * edge) * treeline * smoothstepf(2.5f, 8.f, b[1]) * smoothstepf(8.f, 14.f, m[0] * ROAD_RANGE) *
                    (1.f - smoothstepf(0.03f, 0.2f, m[1])) * (1.f - 0.85f * m[3]) * (0.5f + 0.5f * b[2]);
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
          if (!C.inside(x, z)) continue;
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
    }
  }
  ch.ents.clear();
  ch.ymin = 1e9f; ch.ymax = -1e9f;
  for (int k = 0; k < EK_COUNT; k++) {
    ch.off[k] = (uint32_t)ch.ents.size();
    for (const Ent& e : lists[k]) {
      ch.ents.push_back(e);
      ch.ymin = std::min(ch.ymin, e.y); ch.ymax = std::max(ch.ymax, e.y + kEntInfo[k].h * e.sy);
    }
  }
  ch.off[EK_COUNT] = (uint32_t)ch.ents.size();
  if (ch.ents.empty()) { ch.ymin = 0; ch.ymax = 0; }
  ch.level = std::max(ch.level, level);
}

void Scenery::trim(vec3 cam, float keepDetail, float keepAll, int frame) {
  if (chunks.empty()) return;
  for (int cz = 0; cz < NC; cz++)
    for (int cx = 0; cx < NC; cx++) {
      auto& p = chunks[(size_t)cz * NC + cx];
      if (!p) continue;
      float x = chunkX0(cx) + CH * 0.5f, z = chunkX0(cz) + CH * 0.5f;
      float d = sqrtf((x - cam.x) * (x - cam.x) + (z - cam.z) * (z - cam.z));
      if (d > keepAll && frame - p->lastUse > 120) { p.reset(); continue; }
      if (d > keepDetail && p->level >= 2) {
        // drop the level-2 kinds (trees, bushes, boulders)
        std::vector<Ent> keep; uint32_t off[EK_COUNT + 1];
        for (int k = 0; k < EK_COUNT; k++) {
          off[k] = (uint32_t)keep.size();
          if (k > EK_SLAB) keep.insert(keep.end(), p->ents.begin() + p->off[k], p->ents.begin() + p->off[k + 1]);
        }
        off[EK_COUNT] = (uint32_t)keep.size();
        p->ents.swap(keep); memcpy(p->off, off, sizeof off); p->level = 1;
      }
    }
}

// ------------------------------------------------------------------ collisions
int Scenery::collide(vec3 p, float r, Ent* entOut) {
  int c0x = chunkOf(p.x - r - 20.f), c1x = chunkOf(p.x + r + 20.f), c0z = chunkOf(p.z - r - 20.f), c1z = chunkOf(p.z + r + 20.f);
  for (int cz = c0z; cz <= c1z; cz++)
    for (int cx = c0x; cx <= c1x; cx++) {
      Chunk* ch = ensure(cx, cz, 2);
      if (!ch || ch->ents.empty() || p.y - r > ch->ymax || p.y + r < ch->ymin) continue;
      for (int k = 0; k < EK_COUNT; k++) {
        if (k == EK_RWYLIGHT || k == EK_PAPI) continue;   // frangible airport fixtures
        const EntKindInfo& I = kEntInfo[k];
        int cls = entClass(k);
        for (uint32_t i = ch->off[k]; i < ch->off[k + 1]; i++) {
          const Ent& e = ch->ents[i];
          float dx = p.x - e.x, dz = p.z - e.z, ly = p.y - e.y;
          float R = std::max(I.hx * e.sx, I.hz * e.sz);
          if (dx * dx + dz * dz > (R + r) * (R + r) * 1.5f || ly < -r - 1.f || ly > I.h * e.sy + r) continue;
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

float Scenery::raycast(vec3 a, vec3 d, float L, int* kindOut, Ent* entOut) {
  // sampled along the segment (bolts and the like); skipped when the whole segment is well above the ground
  vec3 b = a + d * L;
  float g = std::max(std::min(g_world.groundHeight(a.x, a.z, 4), g_world.groundHeight(b.x, b.z, 4)), 0.f);
  if (std::min(a.y, b.y) - g > 220.f) return -1.f;
  for (float t = 0; t <= L; t += 2.5f) {
    int k = collide(a + d * t, 0.4f, entOut);
    if (k) { if (kindOut) *kindOut = k; return t; }
  }
  return -1.f;
}
