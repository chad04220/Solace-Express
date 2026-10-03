// Air Xpress - environment entities: trees, bushes, rock formations and buildings.
// The terrain is a plain heightfield; everything standing on it is a separate entity placed deterministically
// from the world masks, streamed in 256 m chunks around the camera and drawn as instanced meshes.
#pragma once
#include "world.h"
#include <memory>
#include <unordered_map>

enum EntKind {
  // foliage
  EK_FIR = 0, EK_SPRUCE, EK_PINE, EK_OAK, EK_BIRCH, EK_PALM, EK_BUSH,
  // rocks
  EK_BOULDER, EK_BLOCK, EK_SLAB, EK_OUTCROP, EK_SPIRE, EK_SEASTACK,
  // buildings
  EK_HOUSE, EK_HOUSE_HIP, EK_HOUSE_L, EK_FARMHOUSE, EK_TOWNHOUSE, EK_SHOP, EK_APARTMENT, EK_OFFICE, EK_TOWER, EK_SKYSCRAPER,
  EK_WAREHOUSE, EK_BARN, EK_SILO, EK_CHURCH, EK_WATERTOWER, EK_LIGHTHOUSE, EK_GASSTATION,
  // airport fixtures (seed = lamp colour: 0 white, 1 amber, 2 green, 3 red)
  EK_RWYLIGHT,
  EK_PAPI,     // seed = the unit's glide-slope threshold (deg): white above it, red below
  EK_COUNT
};
enum EntClass { EC_TREE = 0, EC_ROCK, EC_BUILDING };
inline int entClass(int k) { return k <= EK_BUSH ? EC_TREE : k <= EK_SEASTACK ? EC_ROCK : EC_BUILDING; }

// Nominal size of each kind's mesh (metres, before the per-instance scale): half width (x), full height (y),
// half depth (z). Meshes stand on y = 0 with their front facing +z.
struct EntKindInfo { const char* name; float hx, h, hz; };
extern const EntKindInfo kEntInfo[EK_COUNT];

// One placed entity (also the GPU instance record: 2 x vec4)
struct Ent { float x, y, z, yaw, sx, sy, sz, seed; };

class Scenery {
public:
  static constexpr float CH = 256.f;   // chunk size (m)
  static constexpr int NC = 313;       // chunks per side (covers the 80 km world)
  // level 1: buildings and large rock formations (cheap, streamed far out); level 2: + trees, bushes, boulders
  struct Chunk {
    int level = 0;
    std::vector<Ent> ents;              // sorted by kind
    uint32_t off[EK_COUNT + 1] = {};    // ents[off[k], off[k+1]) are of kind k
    float ymin = 0, ymax = 0;           // vertical bounds of everything in the chunk
    int lastUse = 0;
  };
  Chunk* get(int cx, int cz);                        // nullptr when outside the world; never generates
  Chunk* ensure(int cx, int cz, int level);          // generates up to the level if needed
  static int chunkOf(float v) { return (int)floorf((v + WORLD_HALF) / CH); }
  static float chunkX0(int c) { return c * CH - WORLD_HALF; }
  // Collision: returns the kind + 1 of an entity the sphere (p, r) touches, 0 if none (entOut: a copy of it).
  int collide(vec3 p, float r, Ent* entOut = nullptr);
  // First entity a segment (a, unit d, length L) passes through: distance along it, or -1 (kindOut = kind + 1)
  float raycast(vec3 a, vec3 d, float L, int* kindOut = nullptr, Ent* entOut = nullptr);
  // Craters destroy what stands in them (x, z, radius)
  std::vector<vec3> craters;
  bool destroyed(const Ent& e) const;
  bool chunkAffected(int cx, int cz) const;   // anything in this chunk destroyed or under a crater
  bool anyGone() const { return !craters.empty() || !wrecked.empty(); }
  // Weapon damage: hit points scale with the entity's size (a tree or a boulder takes one hit, a skyscraper twelve).
  // Returns true when this hit destroys it. Destroyed entities stay gone until resetDamage().
  bool damage(const Ent& e, int kind, int amount = 1);
  static int hitPoints(const Ent& e, int kind);
  void resetDamage() { wrecked.clear(); hits.clear(); wreckRev++; }
  int wreckRev = 0;   // bumps whenever something is destroyed (the renderer refreshes its shadow maps)
  // Drops tree-level data of chunks far from the camera and whole chunks further out
  void trim(vec3 cam, float keepDetail, float keepAll, int frame);
  void clear();
  size_t generated() const;
  // Background generation on worker threads (generate() only reads g_world). request() queues a chunk and returns
  // false when there are no workers or the queue is full; pump() installs finished chunks on the calling (main)
  // thread and appends their indices (cz * NC + cx) to `installed`.
  bool request(int cx, int cz, int level);
  void pump(std::vector<int>& installed);
  int workers() const;
  Scenery();
  ~Scenery();
private:
  struct Async;
  std::unique_ptr<Async> async;
  std::vector<std::unique_ptr<Chunk>> chunks;
  std::unordered_map<int, std::vector<uint64_t>> wrecked;   // chunk index -> keys of destroyed entities
  std::unordered_map<uint64_t, int> hits;                   // damage taken so far
  static uint64_t keyOf(const Ent& e) { return ((uint64_t)(uint32_t)(int)lroundf(e.x * 8.f) << 32) | (uint32_t)(int)lroundf(e.z * 8.f); }
  void generate(Chunk& c, int cx, int cz, int level);
};

extern Scenery g_scenery;
