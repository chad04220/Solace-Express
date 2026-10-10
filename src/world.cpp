// Solace Express - hand-designed archipelago "The Solace Islands"
#include "world.h"
#include <cstdio>
#include <cstring>
#include "scenery.h"
#include <deque>

World g_world;
std::vector<GroundPit> g_groundPits;
// a pit's floor and rim (common.glsl craterH, exactly): a bowl with a raised lip, its edge ragged in a few lobes that
// differ from pit to pit
bool craterShape(float x, float z, float cx, float cz, float R, float D, float& h) {
  const float dx = x - cx, dz = z - cz, r = sqrtf(dx * dx + dz * dz);
  if (r > 1.9f * R) return false;
  const float a = atan2f(dz, dx), s = (cx * 0.0137f + cz * 0.0191f - floorf(cx * 0.0137f + cz * 0.0191f)) * 6.2832f;
  const float d = r / (R * (1.f + 0.09f * sinf(3.f * a + s) + 0.05f * sinf(5.f * a + 2.3f * s)));
  if (d > 1.8f) return false;
  D = fabsf(D);
  h += -D * std::max(1.f - d * d, 0.f) + 0.22f * D * expf(-(d - 1.f) * (d - 1.f) * 14.f);
  return true;
}
float pitGround(float x, float z, float g) {
  if (g_groundPits.empty() || g <= 0.3f) return g;   // (not in the sea: the shader digs only land)
  float h = 0; bool in = false;
  for (const GroundPit& p : g_groundPits) in = craterShape(x, z, p.x, p.z, p.R, p.D, h) || in;
  return in ? g + h : g;
}
std::atomic<int> g_worldStage{0};

// ---------------------------------------------------------------- noise
float hash2i(int x, int y) {
  uint32_t h = (uint32_t)x * 0x8da6b343u + (uint32_t)y * 0xd8163841u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

void noised(float px, float pz, float& v, float& dx, float& dz) {
  float fx = floorf(px), fz = floorf(pz);
  int ix = (int)fx, iz = (int)fz;
  float x = px - fx, z = pz - fz;
  float ux = x * x * x * (x * (x * 6.f - 15.f) + 10.f), uz = z * z * z * (z * (z * 6.f - 15.f) + 10.f);
  float dux = 30.f * x * x * (x * (x - 2.f) + 1.f), duz = 30.f * z * z * (z * (z - 2.f) + 1.f);
  float a = hash2i(ix, iz), b = hash2i(ix + 1, iz), c = hash2i(ix, iz + 1), d = hash2i(ix + 1, iz + 1);
  float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
  v = -1.f + 2.f * (a + k1 * ux + k2 * uz + k4 * ux * uz);
  dx = 2.f * dux * (k1 + k4 * uz);
  dz = 2.f * duz * (k2 + k4 * ux);
}

float terrainFbm(float px, float pz, int octaves) {
  float a = 0, b = 1, sx = 0, sz = 0;
  for (int i = 0; i < octaves; i++) {
    float v, dx, dz; noised(px, pz, v, dx, dz);
    sx += dx; sz += dz;
    a += b * v / (1.f + sx * sx + sz * sz);
    b *= 0.5f;
    float nx = 1.6f * px - 1.2f * pz, nz = 1.2f * px + 1.6f * pz;
    px = nx; pz = nz;
  }
  return a;
}

static float valueFbm(float x, float z, int oct) {
  float s = 0, a = 0.5f;
  for (int i = 0; i < oct; i++) { float v, dx, dz; noised(x, z, v, dx, dz); s += a * v; a *= 0.5f; x = x * 2.03f + 17.1f; z = z * 2.03f - 7.3f; }
  return s;
}

// ---------------------------------------------------------------- map data
// Coordinates in metres: x = east, z = south (north is -z). Heading 0 = north.
static const Airport kAirports[] = {
  {"MDB", "Meadowbrook Field", -8000, 14000, 45, 50, 1100, 24, SURF_ASPHALT, 1,
   "Home of the Meadowbrook Flight Academy. Gentle farmland, long runway, friendly winds."},
  {"HFS", "Harlan Farm Strip", -1000, 21000, 70, 170, 650, 18, SURF_GRASS, 0,
   "A grass strip between orchards. Watch for a soft surface and short length."},
  {"PVI", "Port Verde International", -26000, 6000, 15, 90, 2200, 45, SURF_ASPHALT, 2,
   "Busy west-coast port and cargo hub. Sea breezes can be strong in the afternoon."},
  {"CAP", "Solace Capital", 0, -4000, 20, 140, 2800, 50, SURF_ASPHALT, 2,
   "The capital's international gateway on the eastern shore of Verdana."},
  {"CDR", "Cedar Ridge", -22000, -6000, 420, 0, 1000, 24, SURF_ASPHALT, 1,
   "Valley airfield hemmed in by forested ridges. Expect mechanical turbulence."},
  {"SMP", "Summit Pass", -14000, -14000, 1650, 120, 600, 18, SURF_GRAVEL, 0,
   "A gravel shelf high on the Spine mountains. Thin air, short strip, no go-arounds."},
  {"NPT", "Northpoint", -26000, -26000, 25, 70, 1500, 30, SURF_ASPHALT, 1,
   "Fishing town on the north cape. Coastal fog is common."},
  {"LHK", "Lighthouse Key", -33000, 27000, 8, 100, 550, 18, SURF_GRASS, 0,
   "A tiny islet with a lighthouse and a grass runway. Crosswinds off the open sea."},
  {"GLR", "Gull Rock", 3000, 31000, 6, 80, 480, 18, SURF_ASPHALT, 0,
   "A rock in the sea with a short paved strip. Water on every side."},
  {"KLO", "Kaleo Regional", 20000, 8000, 10, 20, 1900, 40, SURF_ASPHALT, 2,
   "Gateway to tropical Kaleo island. Trade winds and afternoon showers."},
  {"PMB", "Palm Bay", 28000, 20000, 4, 110, 750, 22, SURF_SAND, 0,
   "Beach strip of packed sand beside a turquoise lagoon."},
  {"VCF", "Volcano Crater Field", 22600, -6500, 900, 160, 650, 20, SURF_GRAVEL, 0,
   "Research outpost on the flank of Mount Kaleo. Updrafts, downdrafts and ash."},
  {"FJH", "Fjordhaven", 16000, -27000, 20, 90, 1300, 30, SURF_ASPHALT, 1,
   "Nordholm's harbour town at the mouth of a steep fjord. Cold, gusty, often snowy."},
  {"GLS", "Glacier Station", 30000, -31000, 1450, 60, 700, 22, SURF_SNOW, 0,
   "Science camp on the Nordholm ice plateau. Packed-snow runway at altitude."},
  {"FAR", "Far Isle", 36000, 34000, 12, 135, 1700, 35, SURF_ASPHALT, 1,
   "Remote resort island far to the south-east. Plan your fuel."},
  {"ORC", "Orchard Valley", -14000, 4000, 210, 30, 800, 20, SURF_GRASS, 0,
   "Inland farming co-op field among orchards and vineyards."},
};

struct Blob { float x, z, rx, rz, rot, amp, lush, cold; };
static const Blob kBlobs[] = {
  // Verdana (main island)
  {-16000, 0, 15000, 21000, 0.15f, 1.0f, 0.65f, 0.1f},
  {-24000, -23000, 10000, 8000, 0.4f, 0.9f, 0.55f, 0.35f},
  {-6000, 16000, 12000, 9000, -0.2f, 0.95f, 0.75f, 0.0f},
  {-2000, -5000, 8000, 10000, 0.0f, 0.9f, 0.6f, 0.05f},
  {-28000, 9000, 8000, 9000, 0.0f, 0.9f, 0.6f, 0.0f},
  {-12000, -16000, 9000, 9000, 0.0f, 0.8f, 0.5f, 0.3f},
  // islets
  {-33000, 27000, 3000, 2200, 0.3f, 0.85f, 0.5f, 0.0f},
  {3000, 31000, 1600, 1100, 0.0f, 0.8f, 0.3f, 0.0f},
  // Kaleo (tropical)
  {21000, 3000, 8500, 16000, 0.12f, 1.0f, 1.0f, 0.0f},
  {28000, 19000, 7000, 5000, 0.3f, 0.95f, 0.9f, 0.0f},
  // Nordholm (arctic)
  {22000, -31000, 14000, 6500, 0.05f, 1.0f, 0.25f, 1.0f},
  {33000, -28000, 6000, 6000, 0.0f, 0.9f, 0.2f, 1.0f},
  // Far Isle
  {36000, 34000, 5000, 3800, 0.5f, 0.95f, 0.85f, 0.0f},
};

struct Ridge { float ax, az, bx, bz, h, w; };
static const Ridge kRidges[] = {
  {-24000, -22000, -8000, -8000, 1350, 4200},   // The Spine (north)
  {-8000, -8000, -12000, 6000, 800, 3600},     // The Spine (south)
  {-19000, -2000, -30000, 2000, 450, 3000},    // western hills (Cedar Ridge side)
  {-17000, 10000, -6000, 22000, 220, 4000},    // Meadow downs
  {14000, -33000, 34000, -30000, 1300, 3500},  // Nordholm range
  {26000, 8000, 22000, 18000, 300, 3000},      // Kaleo south hills
};

struct Cone { float x, z, h, r; };
static const Cone kCones[] = {
  {25000, -9000, 2300, 5200},    // Mount Kaleo (volcano)
  {-17000, -17000, 900, 3500},   // Spine peak "Old Grey"
  {-10000, -11000, 700, 3000},   // Spine peak "Watcher"
  {30000, -34000, 500, 4000},    // Nordholm dome (ice plateau)
  {-33500, 26400, 40, 800},      // Lighthouse knoll
};

static float segDist(float px, float pz, float ax, float az, float bx, float bz, float& t) {
  float dx = bx - ax, dz = bz - az;
  t = clampf(((px - ax) * dx + (pz - az) * dz) / (dx * dx + dz * dz), 0, 1);
  float qx = ax + dx * t - px, qz = az + dz * t - pz;
  return sqrtf(qx * qx + qz * qz);
}

static void computeTexel(float x, float z, float out[4]) {
  float f = 0, lush = 0, cold = 0, wsum = 1e-4f;
  for (const Blob& b : kBlobs) {
    float dx = x - b.x, dz = z - b.z, c = cosf(b.rot), s = sinf(b.rot);
    float u = (dx * c - dz * s) / b.rx, v = (dx * s + dz * c) / b.rz;
    float w = b.amp * expf(-(u * u + v * v) * 1.4f);
    f = std::max(f, w) + 0.25f * std::min(f, w);
    lush += b.lush * w; cold += b.cold * w; wsum += w;
  }
  lush /= wsum; cold /= wsum;
  // ragged coastline
  f += 0.32f * valueFbm(x / 4200.f, z / 4200.f, 5) + 0.06f * valueFbm(x / 900.f, z / 900.f, 3);
  float land = smoothstepf(0.45f, 0.62f, f);
  float h = f > 0.5f ? (f - 0.5f) * 260.f : std::max((f - 0.5f) * 260.f, -70.f);
  float amp = lerpf(6.f, 22.f, land);
  for (const Ridge& r : kRidges) {
    float t, d = segDist(x, z, r.ax, r.az, r.bx, r.bz, t);
    float taper = smoothstepf(0.f, 0.15f, t) * smoothstepf(1.f, 0.85f, t) * 0.7f + 0.3f;
    float rh = r.h * taper * expf(-(d / r.w) * (d / r.w)) * land;
    h += rh; amp += rh * 0.42f;
  }
  for (const Cone& c : kCones) {
    float d = sqrtf((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z));
    float k = std::max(0.f, 1.f - d / c.r);
    float ch = c.h * powf(k, 1.5f);
    if (c.h > 2000) ch -= 260.f * powf(smoothstepf(700.f, 0.f, d), 2.f);  // crater
    h += ch; amp += ch * 0.25f;
  }
  sceneryBaseMod(x, z, h, amp);
  for (const Airport& a : kAirports) {
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
    // Approach funnel: keep terrain below a glide corridor off both runway ends
    float d = u - a.length * 0.5f;
    if (d > 0 && d < 7000) {
      float slope = a.elev > 600 ? 0.045f : 0.032f;
      float halfw = 250.f + 0.18f * d;
      float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
      float cap = a.elev + 10.f + slope * d;
      float nb = std::min(h, cap), na = std::min(amp, std::max(0.f, (cap - nb) / 1.5f));
      h = lerpf(h, nb, wc); amp = lerpf(amp, na, wc);
    }
    float hu = a.length * 0.5f + 260.f, hv = a.size == 2 ? 420.f : 240.f;
    float du = std::max(0.f, u - hu), dv = std::max(0.f, v - hv);
    float w = 1.f - smoothstepf(0.f, 550.f, sqrtf(du * du + dv * dv));
    h = lerpf(h, a.elev, w);
    amp *= (1.f - w);
    lush = lerpf(lush, std::max(lush, 0.5f), w);
  }
  out[0] = h; out[1] = amp; out[2] = lush; out[3] = cold;
}

// What an airport allows the ground at a point to be: grounds 0..1, how much it lies on a field's flattened grounds
// (left alone), and cap, the height its approach funnels hold the terrain under (+inf outside them) - computeTexel's
// own rules, for anything that reshapes the ground afterwards
static void airportLimits(float x, float z, float& grounds, float& cap) {
  grounds = 0.f; cap = 1e9f;
  for (const Airport& a : kAirports) {
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
    float d = u - a.length * 0.5f;
    if (d > 0 && d < 7000) {
      float halfw = 250.f + 0.18f * d;
      float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
      if (wc > 0.001f) cap = std::min(cap, a.elev + 10.f + (a.elev > 600 ? 0.045f : 0.032f) * d);
    }
    float hu = a.length * 0.5f + 260.f, hv = a.size == 2 ? 420.f : 240.f;
    float du = std::max(0.f, u - hu), dv = std::max(0.f, v - hv);
    grounds = std::max(grounds, 1.f - smoothstepf(0.f, 550.f, sqrtf(du * du + dv * dv)));
  }
}

void airportGroundsCap(float x, float z, float& grounds, float& cap) { airportLimits(x, z, grounds, cap); }

// The height a road may be built up to under an approach funnel: its cap where the funnel holds the ground down in full
// (computeTexel's own weight 0.9 and over), unlimited elsewhere - on a funnel's fringe the ground keeps most of its own
// height, and a road with it
float airportFunnelCeiling(float x, float z) {
  float ceiling = 1e9f;
  for (const Airport& a : kAirports) {
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
    float d = u - a.length * 0.5f;
    if (d <= 0 || d >= 7000) continue;
    float halfw = 250.f + 0.18f * d;
    float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
    if (wc >= 0.9f) ceiling = std::min(ceiling, a.elev + 10.f + (a.elev > 600 ? 0.045f : 0.032f) * d);
  }
  return ceiling;
}

// 0..1: how strongly a point belongs to an airport's flattened grounds or approach funnel
float airportInfluence(float x, float z) {
  float best = 0;
  for (const Airport& a : kAirports) {
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
    float d = u - a.length * 0.5f;
    if (d > 0 && d < 7000) {
      float halfw = 250.f + 0.18f * d;
      best = std::max(best, (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d)));
    }
    float hu = a.length * 0.5f + 260.f, hv = a.size == 2 ? 420.f : 240.f;
    float du = std::max(0.f, u - hu), dv = std::max(0.f, v - hv);
    best = std::max(best, 1.f - smoothstepf(0.f, 550.f, sqrtf(du * du + dv * dv)));
  }
  return best;
}

void World::build(const std::string& cachePath, const std::string& stamp) {
  airports.assign(std::begin(kAirports), std::end(kAirports));
  roads = RoadNetwork(); roadGrid = RoadGrid();   // (the natural ground first: the roads are routed over it)
  for (auto& a : airports) a.hospital = !strcmp(a.code, "CAP") || !strcmp(a.code, "NPT") || !strcmp(a.code, "PVI");
  g_worldStage = cachePath.empty() ? 2 : 1;
  fromCache = !cachePath.empty() && loadCache(cachePath, stamp);
  if (fromCache) { sceneryInit(); sceneryAlignCommunities(*this); sceneryBakeCommunityLots(*this); boxes.clear(); g_worldStage = 3; return; }
  g_worldStage = 2;
  hm.resize((size_t)HM_N * HM_N * 4);
  sceneryInit();
  parallelFor(HM_N, [&](int j) {   // rows are independent; spread over every core
    for (int i = 0; i < HM_N; i++) {
      float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL;
      computeTexel(x, z, &hm[((size_t)j * HM_N + i) * 4]);
    }
  });
  // Smooth the ground heights with a separable 1-2-1 filter (twice): bilinear interpolation of the raw samples leaves
  // visible creases along the grid on big smooth landforms (volcano flanks, ridges, sea cliffs) - a faceted, low-poly
  // look. Flat areas (airport grounds, the sea floor plateau) are unchanged by it.
  {
    std::vector<float> tmp((size_t)HM_N * HM_N);
    for (int pass = 0; pass < 2; pass++) {
      parallelFor(HM_N, [&](int j) {
        for (int i = 0; i < HM_N; i++) {
          int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, HM_N - 1);
          const float* r = &hm[(size_t)j * HM_N * 4];
          tmp[(size_t)j * HM_N + i] = (r[i0 * 4] + 2.f * r[i * 4] + r[i1 * 4]) * 0.25f;
        }
      });
      parallelFor(HM_N, [&](int j) {
        int j0 = std::max(j - 1, 0), j1 = std::min(j + 1, HM_N - 1);
        for (int i = 0; i < HM_N; i++)
          hm[((size_t)j * HM_N + i) * 4] = (tmp[(size_t)j0 * HM_N + i] + 2.f * tmp[(size_t)j * HM_N + i] + tmp[(size_t)j1 * HM_N + i]) * 0.25f;
      });
    }
  }
  for (int pass = 0; pass < 4 && fillInlandPits() > 0; pass++) {}   // (until none is left: see fillInlandPits)
  // A real summit bowl for Mount Kaleo. The original procedural detail overwhelms
  // its shallow analytic depression; carve only this bounded, airport-free summit.
  // All terrain outside the 520 m texel-centre disk remains byte-identical.
  const int vx0 = std::max(0, (int)((25000.f - 520.f + WORLD_HALF) / HM_TEXEL));
  const int vx1 = std::min(HM_N - 1, (int)((25000.f + 520.f + WORLD_HALF) / HM_TEXEL));
  const int vz0 = std::max(0, (int)((-9000.f - 520.f + WORLD_HALF) / HM_TEXEL));
  const int vz1 = std::min(HM_N - 1, (int)((-9000.f + 520.f + WORLD_HALF) / HM_TEXEL));
  for (int j = vz0; j <= vz1; ++j) for (int i = vx0; i <= vx1; ++i) {
    const float x = -WORLD_HALF + (i + .5f) * HM_TEXEL;
    const float z = -WORLD_HALF + (j + .5f) * HM_TEXEL;
    const float dx = x - 25000.f, dz = z + 9000.f, r = hypotf(dx, dz);
    if (r >= 520.f || airportInfluence(x, z) > .001f) continue;
    const float angle = atan2f(dz, dx);
    const float irregularR = r * (1.f + .035f * sinf(angle * 3.f) + .025f * cosf(angle * 5.f));
    const float wall = smoothstepf(145.f, 340.f, irregularR);
    const float weight = 1.f - smoothstepf(340.f, 520.f, r);
    float* t = &hm[((size_t)j * HM_N + i) * 4];
    t[0] = lerpf(t[0], 1780.f + 230.f * wall, weight);
    t[1] = lerpf(t[1], .7f + 24.f * wall, weight);
  }
  // the roads, routed over the finished natural ground (World::height grades them in from here on)
  roads = buildRoadNetwork(*this);
  buildRoadGrid(roads, roadGrid);
  bakeMask();
  buildHMax();
  buildEnvelope();
  // Airport buildings are raster scenery entities now (airport_scenery.cpp); the old analytic box list stays empty
  boxes.clear();
  if (!cachePath.empty()) saveCache(cachePath, stamp);
  g_worldStage = 3;
}

// The generated world on disk: its height and mask textures, height bounds and terrain envelope, stamped with the
// build that made them (any other build generates again)
namespace {
const uint32_t kWorldMagic = 0x574c4434u;   // "WLD4": the road network, its grid and the forest noise in one R32UI texel
template <class T> void putVec(FILE* f, const std::vector<T>& v) { uint64_t n = v.size(); fwrite(&n, 8, 1, f); if (n) fwrite(v.data(), sizeof(T), n, f); }
template <class T> bool getVec(FILE* f, std::vector<T>& v, uint64_t expect) {
  uint64_t n = 0; if (fread(&n, 8, 1, f) != 1 || n != expect) return false;
  v.resize(n); return !n || fread(v.data(), sizeof(T), n, f) == n;
}
template <class T> bool getVecUpTo(FILE* f, std::vector<T>& v, uint64_t most) {   // (a list whose length varies)
  uint64_t n = 0; if (fread(&n, 8, 1, f) != 1 || n > most) return false;
  v.resize(n); return !n || fread(v.data(), sizeof(T), n, f) == n;
}
void putRoads(FILE* f, const RoadNetwork& net) {
  putVec(f, net.nodes);
  const uint64_t n = net.paths.size(); fwrite(&n, 8, 1, f);
  for (const RoadPath& p : net.paths) {
    const int32_t h[3] = {(int32_t)p.cls, p.from, p.to}; fwrite(h, 4, 3, f);
    putVec(f, p.pts); putVec(f, p.bridge);
  }
}
bool getRoads(FILE* f, RoadNetwork& net) {
  if (!getVecUpTo(f, net.nodes, 4096)) return false;
  uint64_t n = 0; if (fread(&n, 8, 1, f) != 1 || n > 65535) return false;
  net.paths.resize(n);
  for (RoadPath& p : net.paths) {
    int32_t h[3]; if (fread(h, 4, 3, f) != 3 || h[0] < 0 || h[0] >= RC_COUNT) return false;
    p.cls = (RoadClass)h[0]; p.from = h[1]; p.to = h[2];
    if (!getVecUpTo(f, p.pts, 1u << 20) || !getVecUpTo(f, p.bridge, 1u << 20) || p.bridge.size() + 1 != std::max<size_t>(p.pts.size(), 1)) return false;
  }
  return true;
}
}
bool World::loadCache(const std::string& path, const std::string& stamp) {
  FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
  // into temporaries, every array at its exact size (the terrain code indexes them on that assumption), published only
  // when the whole file checks out: a short, malformed or other build's cache is rejected and the world generated
  World w;
  uint32_t magic = 0; char st[64] = {};
  bool ok = fread(&magic, 4, 1, f) == 1 && magic == kWorldMagic && fread(st, 1, 64, f) == 64 && stamp == std::string(st, strnlen(st, 64));
  ok = ok && getVec(f, w.hm, (uint64_t)HM_N * HM_N * 4) && getVec(f, w.mask, (uint64_t)MASK_N * MASK_N * 4);
  ok = ok && getRoads(f, w.roads) && getVecUpTo(f, w.roadGrid.segs, 1u << 20) && getVec(f, w.roadGrid.head, (uint64_t)RoadGrid::N * RoadGrid::N) &&
       getVecUpTo(f, w.roadGrid.list, 0x80000u) && getVec(f, w.roadGrid.nearHead, (uint64_t)RoadGrid::NC * RoadGrid::NC) && getVecUpTo(f, w.roadGrid.nearList, 1u << 24);
  // (every entry names a segment, every texel's entries lie in the list: the terrain indexes them unchecked)
  for (size_t i = 0; ok && i < w.roadGrid.list.size(); i++) ok = w.roadGrid.list[i] < w.roadGrid.segs.size();
  for (size_t i = 0; ok && i < w.roadGrid.nearList.size(); i++) ok = w.roadGrid.nearList[i] < w.roadGrid.segs.size();
  for (size_t i = 0; ok && i < w.roadGrid.head.size(); i++) ok = RoadGrid::first(w.roadGrid.head[i]) + RoadGrid::count(w.roadGrid.head[i]) <= w.roadGrid.list.size();
  for (size_t i = 0; ok && i < w.roadGrid.nearHead.size(); i++) ok = (w.roadGrid.nearHead[i] >> 8) + (w.roadGrid.nearHead[i] & 255u) <= w.roadGrid.nearList.size();
  for (size_t i = 0; ok && i < w.roadGrid.segs.size(); i++) ok = w.roadGrid.segs[i].cls < RC_COUNT;
  for (int L = 0; ok && L < HMAX_LEVELS; L++) { const uint64_t n = (uint64_t)(HMAX_N >> L); ok = getVec(f, w.hmax[L], n * n); }
  ok = ok && getVec(f, w.tpV0, (uint64_t)(HM_N + 1) * (HM_N + 1));
  for (int L = 0; ok && L < TP_LEVELS; L++) { const uint64_t n = (uint64_t)(HM_N >> L); ok = getVec(f, w.tpM[L], n * n); }
  ok = ok && fgetc(f) == EOF;   // (nothing after the last array: not some other format's file)
  fclose(f);
  // (and every number in it a number: a NaN height in a well-formed file loaded, and the terrain and the flight model
  // ran on it - the review of v3.44.0, WLD-4)
  auto finite = [](const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; };
  ok = ok && finite(w.hm) && finite(w.tpV0);
  for (int L = 0; ok && L < HMAX_LEVELS; L++) ok = finite(w.hmax[L]);
  for (int L = 0; ok && L < TP_LEVELS; L++) ok = finite(w.tpM[L]);
  for (size_t i = 0; ok && i < w.roadGrid.segs.size(); i++) { const RoadSegment& r = w.roadGrid.segs[i]; ok = std::isfinite(r.ax + r.az + r.bx + r.bz + r.ah + r.bh + r.along); }
  if (!ok) return false;
  hm.swap(w.hm); mask.swap(w.mask); tpV0.swap(w.tpV0);
  std::swap(roads, w.roads); std::swap(roadGrid, w.roadGrid);
  for (int L = 0; L < HMAX_LEVELS; L++) hmax[L].swap(w.hmax[L]);
  for (int L = 0; L < TP_LEVELS; L++) tpM[L].swap(w.tpM[L]);
  return true;
}
void World::saveCache(const std::string& path, const std::string& stamp) const {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb"); if (!f) return;
  char st[64] = {}; strncpy(st, stamp.c_str(), 63);
  fwrite(&kWorldMagic, 4, 1, f); fwrite(st, 1, 64, f);
  putVec(f, hm); putVec(f, mask);
  putRoads(f, roads); putVec(f, roadGrid.segs); putVec(f, roadGrid.head); putVec(f, roadGrid.list); putVec(f, roadGrid.nearHead); putVec(f, roadGrid.nearList);
  for (int L = 0; L < HMAX_LEVELS; L++) putVec(f, hmax[L]);
  putVec(f, tpV0);
  for (int L = 0; L < TP_LEVELS; L++) putVec(f, tpM[L]);
  bool ok = !ferror(f); fclose(f);
  remove(path.c_str());
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) remove(tmp.c_str());
}

int World::findAirport(const char* code) const {
  for (size_t i = 0; i < airports.size(); i++) if (!strcmp(airports[i].code, code)) return (int)i;
  return -1;
}

// Every cell of an n x n grid over the map (cell size cs) that a graded road segment's platform or banks reach, with the
// segment's highest end: the bounds of the terrain lift to it
template <class F> static void forEachRoadCell(const RoadGrid& grid, float cs, int n, float slack, F f) {
  for (const RoadSegment& s : grid.segs) {
    if (s.flags) continue;
    const float R = roadSpec(s.cls).halfPlatform + ROAD_BANK_MAX + slack, top = std::max(s.ah, s.bh);
    auto cell = [&](float v) { return std::clamp((int)floorf((v + WORLD_HALF) / cs), 0, n - 1); };
    for (int j = cell(std::min(s.az, s.bz) - R); j <= cell(std::max(s.az, s.bz) + R); j++)
      for (int i = cell(std::min(s.ax, s.bx) - R); i <= cell(std::max(s.ax, s.bx) + R); i++) f(i, j, top);
  }
}

// Upper bound of the rendered terrain (ground + detail) per cell, so the ray marcher can
// skip whole cells it passes above. Conservative: per-texel max of the base layer over the bilinear footprint, the
// detail fbm sampled densely with 6 octaves plus the bound of all finer octaves (sum of their amplitudes).
void World::buildHMax() {
  const int TPC = HM_N / HMAX_N;  // texels per cell
  const float cs = 2.f * WORLD_HALF / HMAX_N;
  hmax[0].assign((size_t)HMAX_N * HMAX_N, 0.f);
  parallelFor(HMAX_N, [&](int cj) {
    for (int ci = 0; ci < HMAX_N; ci++) {
      float b0 = -1e9f, b1 = 0.f, gmin = 1e9f;
      for (int j = cj * TPC - 1; j <= cj * TPC + TPC; j++)
        for (int i = ci * TPC - 1; i <= ci * TPC + TPC; i++) {
          const float* h = &hm[((size_t)std::clamp(j, 0, HM_N - 1) * HM_N + std::clamp(i, 0, HM_N - 1)) * 4];
          b0 = std::max(b0, h[0]); b1 = std::max(b1, h[1]); gmin = std::min(gmin, h[0] - 2.f * h[1]);
        }
      float fmax = -2.f;
      if (b1 >= 0.01f) {
        const int S = 12;
        float x0 = -WORLD_HALF + ci * cs, z0 = -WORLD_HALF + cj * cs;
        for (int sj = 0; sj <= S; sj++)
          for (int si = 0; si <= S; si++)
            fmax = std::max(fmax, terrainFbm((x0 + cs * si / S) / DETAIL_SCALE, (z0 + cs * sj / S) / DETAIL_SCALE, 6));
        fmax += 0.12f;  // finer octaves (<= 1/32) + sampling error of octaves up to 6
      }
      float top = b0 + b1 * std::max(fmax, 0.f);
      (void)gmin;
      hmax[0][(size_t)cj * HMAX_N + ci] = std::max(top, 0.f) + 4.f;   // + crater rims
    }
  });
  // the roads: a fill can lift the ground above anything the heightfield bounds - every cell a graded road's platform or
  // banks reach holds at least its highest end
  forEachRoadCell(roadGrid, 2.f * WORLD_HALF / HMAX_N, HMAX_N, 2.f * HM_TEXEL, [&](int i, int j, float top) {
    float& m = hmax[0][(size_t)j * HMAX_N + i]; m = std::max(m, top + 4.f); });
  for (int L = 1; L < HMAX_LEVELS; L++) {
    int n = HMAX_N >> L, pn = n * 2;
    hmax[L].assign((size_t)n * n, 0.f);
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++) {
        const std::vector<float>& P = hmax[L - 1];
        hmax[L][(size_t)j * n + i] = std::max(std::max(P[(size_t)(2 * j) * pn + 2 * i], P[(size_t)(2 * j) * pn + 2 * i + 1]),
                                              std::max(P[(size_t)(2 * j + 1) * pn + 2 * i], P[(size_t)(2 * j + 1) * pn + 2 * i + 1]));
      }
  }
}

// Terrain envelope. Vertex v sits on texel centre v; cell c spans texel centres c..c+1, where the base layer is exactly
// the bilinear patch of its four texels. That patch lies within |a - b - c + d|/4 of either triangle pair over the cell,
// and the detail adds at most (max amplitude) x (max fbm, sampled at the corners and centre, + the bound of the finer
// octaves and of the sampling error, as in buildHMax). So with every vertex raised by the largest such allowance of the
// cells around it, the triangles over a cell can only lie above the terrain there. Coarser levels bound whole blocks.
void World::buildEnvelope() {
  const int N = HM_N, NV = HM_N + 1;
  auto H = [&](int i, int j, int c) { return hm[((size_t)std::clamp(j, 0, N - 1) * N + std::clamp(i, 0, N - 1)) * 4 + c]; };
  auto fbmAt = [&](float x, float z) { return terrainFbm(x / DETAIL_SCALE, z / DETAIL_SCALE, 6); };
  auto vx = [&](float i) { return -WORLD_HALF + (i + 0.5f) * HM_TEXEL; };
  // detail amplitude near each vertex: the fbm is only needed where some cell around it has detail
  std::vector<float> fV((size_t)NV * NV, 0.f);
  parallelFor(NV, [&](int j) {
    for (int i = 0; i < NV; i++) {
      float b1 = 0.f;
      for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) b1 = std::max(b1, H(i + di, j + dj, 1));
      fV[(size_t)j * NV + i] = b1 >= 0.01f ? fbmAt(vx((float)i), vx((float)j)) : 0.f;
    }
  });
  std::vector<float> D((size_t)N * N);   // allowance of each cell above the triangles through its corner heights
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++) {
      float a = H(i, j, 0), b = H(i + 1, j, 0), c = H(i, j + 1, 0), d = H(i + 1, j + 1, 0);
      float b1 = std::max(std::max(H(i, j, 1), H(i + 1, j, 1)), std::max(H(i, j + 1, 1), H(i + 1, j + 1, 1)));
      float e = 0.f;
      if (b1 >= 0.01f) {
        float f = std::max(std::max(fV[(size_t)j * NV + i], fV[(size_t)j * NV + i + 1]), std::max(fV[(size_t)(j + 1) * NV + i], fV[(size_t)(j + 1) * NV + i + 1]));
        f = std::max(f, fbmAt(vx(i + 0.5f), vx(j + 0.5f)));
        e = b1 * std::max(f + 0.14f, 0.f);
      }
      D[(size_t)j * N + i] = fabsf(a - b - c + d) * 0.25f + e + 0.05f;
    }
  });
  tpV0.assign((size_t)NV * NV, 0.f);
  parallelFor(NV, [&](int j) {
    for (int i = 0; i < NV; i++) {
      float m = 0.f;
      for (int dj = -1; dj <= 0; dj++) for (int di = -1; di <= 0; di++) {
        int ci = i + di, cj = j + dj;
        if (ci < 0 || cj < 0 || ci >= N || cj >= N) continue;
        m = std::max(m, D[(size_t)cj * N + ci]);
      }
      tpV0[(size_t)j * NV + i] = H(i, j, 0) + m;
    }
  });
  // the roads (as in buildHMax): every vertex of a cell a graded road reaches at least its highest end - so the
  // triangles over that cell stay above the platform and its banks too
  forEachRoadCell(roadGrid, HM_TEXEL, N, HM_TEXEL, [&](int i, int j, float top) {
    for (int dj = 0; dj <= 1; dj++) for (int di = 0; di <= 1; di++) {
      float& v = tpV0[(size_t)(j + dj) * NV + i + di]; v = std::max(v, top + 0.5f); } });
  tpM[0].assign((size_t)N * N, 0.f);
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++)
      tpM[0][(size_t)j * N + i] = std::max(std::max(tpV0[(size_t)j * NV + i], tpV0[(size_t)j * NV + i + 1]),
                                           std::max(tpV0[(size_t)(j + 1) * NV + i], tpV0[(size_t)(j + 1) * NV + i + 1]));
  });
  for (int L = 1; L < TP_LEVELS; L++) {
    int n = N >> L, pn = n * 2;
    const std::vector<float>& P = tpM[L - 1];
    tpM[L].assign((size_t)n * n, 0.f);
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++)
        tpM[L][(size_t)j * n + i] = std::max(std::max(P[(size_t)(2 * j) * pn + 2 * i], P[(size_t)(2 * j) * pn + 2 * i + 1]),
                                             std::max(P[(size_t)(2 * j + 1) * pn + 2 * i], P[(size_t)(2 * j + 1) * pn + 2 * i + 1]));
  }
}

// Ground below the sea that the open sea can't reach - a hollow the detail layer digs into low land, 14 of them in the
// islands as generated, the deepest 30 m, the largest 1.4 km^2 - would be flooded by the flat sea: a pool of sea water
// in a field (which the physics ditched in) that came and went with the terrain's detail. Each is lifted, with the
// ground round it, until its floor is a valley floor 8 m up: the base heightmap is raised by a smooth bump (wide
// enough that the hollow's rim rises with it, so no moat and no step) and every texel of detail is kept. 8 m clears
// the band the terrain shader paints as beach, so the floor grows grass and forest like the land round it. Airport
// grounds and approach funnels are never lifted. Raising ground can cut a sea inlet off into a new hollow, so build()
// runs this until it finds none. Once per build: the world is cached.
int World::fillInlandPits() {
  const int F = 4, N = HM_N * F;                 // fine cells of about 10 m (N a multiple of 64: a row is whole words)
  const float S = HM_TEXEL / F;
  const size_t NN = (size_t)N * N, W = (size_t)N / 64;
  std::vector<uint64_t> below(NN / 64, 0), sea(NN / 64, 0);
  auto get = [](const std::vector<uint64_t>& v, size_t k) { return (v[k >> 6] >> (k & 63)) & 1u; };
  auto put = [](std::vector<uint64_t>& v, size_t k) { v[k >> 6] |= 1ull << (k & 63); };
  // below the sea: the detail layer's bound (|fbm| < 2) decides most cells without evaluating it
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++) {
      const float x = -WORLD_HALF + (i + 0.5f) * S, z = -WORLD_HALF + (j + 0.5f) * S;
      float b[4]; sampleBase(x, z, b);
      const bool lo = b[0] + 2.f * b[1] < 0.f || (b[0] - 2.f * b[1] < 0.f && height(x, z, 11) < 0.f);
      if (lo) below[(size_t)j * W + (i >> 6)] |= 1ull << (i & 63);
    }
  });
  // the open sea: everything below it that the map's edge reaches
  std::deque<uint32_t> q;
  for (int i = 0; i < N; i++)
    for (size_t k : {(size_t)i, (size_t)(N - 1) * N + i, (size_t)i * N, (size_t)i * N + N - 1})
      if (get(below, k) && !get(sea, k)) { put(sea, k); q.push_back((uint32_t)k); }
  while (!q.empty()) {
    const uint32_t k = q.front(); q.pop_front();
    const int x = (int)(k % N), y = (int)(k / N);
    const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
    for (int d = 0; d < 4; d++) {
      if (nx[d] < 0 || ny[d] < 0 || nx[d] >= N || ny[d] >= N) continue;
      const size_t kk = (size_t)ny[d] * N + nx[d];
      if (get(below, kk) && !get(sea, kk)) { put(sea, kk); q.push_back((uint32_t)kk); }
    }
  }
  // the lift each texel needs: the deepest any pit cell in its reach (the bilinear cells round it) lies below the floor
  const float target = 8.f;
  std::vector<float> req((size_t)HM_N * HM_N, 0.f);
  int pits = 0;
  for (size_t w = 0; w < NN / 64; w++) {
    const uint64_t m = below[w] & ~sea[w];
    if (!m) continue;
    for (int b = 0; b < 64; b++) {
      if (!((m >> b) & 1u)) continue;
      const size_t k = w * 64 + (size_t)b;
      const float x = -WORLD_HALF + ((k % N) + 0.5f) * S, z = -WORLD_HALF + ((k / N) + 0.5f) * S;
      const float need = target + 0.3f - height(x, z, 11);   // (0.3: what the coarser detail far off can take away)
      const float fx = (x + WORLD_HALF) / HM_TEXEL - 0.5f, fz = (z + WORLD_HALF) / HM_TEXEL - 0.5f;
      const int i0 = (int)floorf(fx), j0 = (int)floorf(fz);
      for (int dj = 0; dj <= 1; dj++) for (int di = 0; di <= 1; di++) {
        const size_t t = (size_t)std::clamp(j0 + dj, 0, HM_N - 1) * HM_N + std::clamp(i0 + di, 0, HM_N - 1);
        req[t] = std::max(req[t], need);
      }
      pits++;
    }
  }
  if (!pits) return 0;
  const std::vector<float> need0 = req;   // (before it's spread: the texels that shape a pit themselves)
  // the bump: the requirement spread by a running maximum, then smoothed by a box of the same radius - every texel
  // then gets at least what it needs (each one in its box is at least its own need) and the ground rises gently,
  // ~0.6 km across
  const int R = 8;
  auto pass = [&](std::vector<float>& v, bool maxf, bool rows) {
    std::vector<float> o(v.size());
    parallelFor(HM_N, [&](int a) {
      for (int c = 0; c < HM_N; c++) {
        float acc = 0.f;
        for (int d = -R; d <= R; d++) {
          const int cc = std::clamp(c + d, 0, HM_N - 1);
          const float x = rows ? v[(size_t)a * HM_N + cc] : v[(size_t)cc * HM_N + a];
          acc = maxf ? std::max(acc, x) : acc + x;
        }
        (rows ? o[(size_t)a * HM_N + c] : o[(size_t)c * HM_N + a]) = maxf ? acc : acc / (2 * R + 1);
      }
    });
    v.swap(o);
  };
  pass(req, true, true); pass(req, true, false); pass(req, false, true); pass(req, false, false);
  // raised - but never on an airport's flattened grounds, and in its approach funnels no higher than they hold the
  // terrain (its detail included, as computeTexel keeps it). A hollow the lift can't fill there has its detail
  // flattened instead, just above the sea
  parallelFor(HM_N, [&](int tj) {
    for (int ti = 0; ti < HM_N; ti++) {
      const size_t k = (size_t)tj * HM_N + ti;
      const float lift = req[k];
      if (lift <= 0.f) continue;
      const float cx = -WORLD_HALF + (ti + 0.5f) * HM_TEXEL, cz = -WORLD_HALF + (tj + 0.5f) * HM_TEXEL;
      float grounds, cap; airportLimits(cx, cz, grounds, cap);
      float* t = &hm[k * 4];
      const float L = std::min(lift * (1.f - smoothstepf(0.f, 0.05f, grounds)), std::max(0.f, cap - t[0] - 1.5f * t[1]));
      t[0] += L;
      if (L >= lift - 0.01f || need0[k] <= 0.f) continue;
      float fmin = 1e9f;   // (the detail's lowest over the texel's reach, an eighth of a texel apart, less a margin)
      for (int sj = -8; sj <= 8; sj++) for (int si = -8; si <= 8; si++)
        fmin = std::min(fmin, terrainFbm((cx + si * HM_TEXEL / 8.f) / DETAIL_SCALE, (cz + sj * HM_TEXEL / 8.f) / DETAIL_SCALE, 11));
      fmin -= 0.05f;
      const float floor1 = 1.f;
      if (t[0] + t[1] * fmin >= floor1) continue;
      if (t[0] > floor1 && fmin < 0.f) t[1] = std::min(t[1], (t[0] - floor1) / -fmin);
      else { t[0] = std::max(t[0], floor1); t[1] = 0.f; }
    }
  });
  return pits;
}

void World::sampleBase(float x, float z, float out[4]) const {
  float fx = (x + WORLD_HALF) / HM_TEXEL - 0.5f, fz = (z + WORLD_HALF) / HM_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  int i0 = (int)flx, j0 = (int)flz;
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j, int c) { i = std::clamp(i, 0, HM_N - 1); j = std::clamp(j, 0, HM_N - 1); return hm[((size_t)j * HM_N + i) * 4 + c]; };
  for (int c = 0; c < 4; c++) {
    float a = at(i0, j0, c), b = at(i0 + 1, j0, c), cc = at(i0, j0 + 1, c), d = at(i0 + 1, j0 + 1, c);
    out[c] = (a * (1 - tx) + b * tx) * (1 - tz) + (cc * (1 - tx) + d * tx) * tz;
  }
}

float World::naturalHeight(float x, float z, int octaves) const {
  float b[4]; sampleBase(x, z, b);
  return b[1] < 0.01f ? b[0] : b[0] + b[1] * terrainFbm(x / DETAIL_SCALE, z / DETAIL_SCALE, octaves);
}

float World::height(float x, float z, int octaves) const {
  // bare ground: trees, rocks and buildings are separate entities (entities.h); the roads are built into it
  return roadGrade(roadGrid, x, z, naturalHeight(x, z, octaves));
}

vec3 World::normal(float x, float z) const {
  const float e = 2.0f;
  float hx = height(x + e, z) - height(x - e, z), hz = height(x, z + e) - height(x, z - e);
  return normalize(vec3(-hx, 2 * e, -hz));
}

int World::onRunway(float x, float z, float margin) const {
  for (size_t i = 0; i < airports.size(); i++) {
    const Airport& a = airports[i];
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = dx * sinf(hd) - dz * cosf(hd), v = dx * cosf(hd) + dz * sinf(hd);
    if (fabsf(u) <= a.length * 0.5f + margin && fabsf(v) <= a.width * 0.5f + margin) return (int)i;
  }
  return -1;
}

float World::distanceKm(int a, int b) const {
  float dx = airports[a].x - airports[b].x, dz = airports[a].z - airports[b].z;
  return sqrtf(dx * dx + dz * dz) / 1000.f;
}

int World::nearestAirport(float x, float z, float* distOut) const {
  int best = -1; float bd = 1e30f;
  for (size_t i = 0; i < airports.size(); i++) {
    float dx = airports[i].x - x, dz = airports[i].z - z, d = sqrtf(dx * dx + dz * dz);
    if (d < bd) { bd = d; best = (int)i; }
  }
  if (distOut) *distOut = bd;
  return best;
}

std::string Weather::describe() const {
  std::string s = fmt("Wind %03.0f@%.0fkt", wrapDeg360(windFrom), windSpeed * MS_TO_KT);
  if (gust > 0.5f) s += fmt(" G%.0f", (windSpeed + gust) * MS_TO_KT);
  s += cloudCover < 0.15f ? ", clear" : cloudCover < 0.45f ? ", scattered" : cloudCover < 0.8f ? ", broken" : ", overcast";
  if (cloudCover >= 0.15f) s += fmt(" %.0fft", cloudBase * M_TO_FT);
  s += visibility >= 20000 ? ", vis 10+km" : fmt(", vis %.1fkm", visibility / 1000.f);
  if (precip == 1) s += storm ? ", thunderstorms" : ", rain";
  if (precip == 2) s += ", snow";
  int hh = (int)timeOfDay, mm = (int)((timeOfDay - hh) * 60);
  s += fmt(", %02d:%02d", hh, mm);
  return s;
}
