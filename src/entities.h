// Air Xpress - environment entities: trees, bushes, rock formations and buildings.
// The terrain is a plain heightfield; everything standing on it is a separate entity placed deterministically
// from the world masks, streamed in 256 m chunks around the camera and drawn as instanced meshes.
#pragma once
#include "world.h"
#include <memory>

enum EntKind {
  // foliage
  EK_FIR = 0, EK_SPRUCE, EK_PINE, EK_OAK, EK_BIRCH, EK_PALM, EK_BUSH,
  // rocks
  EK_BOULDER, EK_BLOCK, EK_SLAB, EK_OUTCROP, EK_SPIRE, EK_SEASTACK,
  // buildings
  EK_HOUSE, EK_HOUSE_HIP, EK_HOUSE_L, EK_FARMHOUSE, EK_TOWNHOUSE, EK_SHOP, EK_APARTMENT, EK_OFFICE, EK_TOWER, EK_SKYSCRAPER,
  EK_WAREHOUSE, EK_BARN, EK_SILO, EK_CHURCH, EK_WATERTOWER, EK_LIGHTHOUSE, EK_GASSTATION,
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
  // Collision: returns the kind + 1 of an entity the sphere (p, r) touches, 0 if none.
  int collide(vec3 p, float r);
  // Plasma craters destroy what stands in them (x, z, radius)
  std::vector<vec3> craters;
  bool destroyed(const Ent& e) const;
  // Drops tree-level data of chunks far from the camera and whole chunks further out
  void trim(vec3 cam, float keepDetail, float keepAll, int frame);
  void clear() { chunks.clear(); }
  size_t generated() const;
private:
  std::vector<std::unique_ptr<Chunk>> chunks;
  void generate(Chunk& c, int cx, int cz, int level);
};

extern Scenery g_scenery;
