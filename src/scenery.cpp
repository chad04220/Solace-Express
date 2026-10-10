// Solace Express - towns, roads, farmland, forests, rocks and buildings (CPU side; mirrored in shaders.h)
#include "scenery.h"
#include "airport_layout.h"
#include <mutex>

// ---------------------------------------------------------------- hand-placed settlements (metres)
// Immutable legacy coordinates are part of the heightfield recipe, including places that
// were underwater or inside an approach corridor. Moving scenery must not move terrain.
static const Town kTerrainTowns[] = {
  {"Port Verde", -29500, 10500, 1320, 2},
  {"Solace Capital", -3200, -1200, 1200, 2},
  {"Kaleo Town", 17400, 7000, 840, 1},
  {"Fjordhaven", 17500, -28600, 660, 1},
  {"Northpoint", -25300, -24100, 600, 1},
  {"Meadowbrook", -6600, 15700, 420, 0},
  {"Cedar Ridge", -23600, -5500, 360, 0},
  {"Orchard Valley", -15300, 3250, 330, 0},
  {"Harlan", 200, 22500, 210, 0},
  {"Palm Bay", 27600, 21100, 300, 0},
  {"Far Isle Resort", 35000, 35000, 360, 0},
  {"Lighthouse Key", -33600, 26200, 130, 0},
  {"Westvale", -30000, -2000, 360, 0},
  {"Riverton", -12000, -1000, 420, 0},
  {"Greenhollow", -4000, 9000, 360, 0},
  {"Saltmarsh", -20000, 22000, 300, 0},
  {"Kaleo Springs", 23000, 14000, 360, 0},
  {"Ice Harbor", 27000, -27500, 270, 0},
};
// Surveyed dry, gentle sites for the seven formerly empty communities. Existing populated
// settlements retain their centres; radius/kind stay unchanged for all communities.
const Town kTowns[] = {
  {"Port Verde", -29500, 10500, 1320, 2},
  {"Solace Capital", -3200, -1200, 1200, 2},
  {"Kaleo Town", 17400, 7000, 840, 1},
  {"Fjordhaven", 17500, -28600, 660, 1},
  {"Northpoint", -25300, -24100, 600, 1},
  {"Meadowbrook", -6600, 15700, 420, 0},
  {"Cedar Ridge", -23600, -5500, 360, 0},
  {"Orchard Valley", -15300, 3250, 330, 0},
  {"Harlan", 600, 22000, 210, 0},
  {"Palm Bay", 27600, 21100, 300, 0},
  {"Far Isle Resort", 35000, 35000, 360, 0},
  {"Lighthouse Key", -33000, 27900, 130, 0},
  {"Westvale", -27000, -3400, 360, 0},
  {"Riverton", -16400, 3000, 420, 0},
  {"Greenhollow", -4800, 8000, 360, 0},
  {"Saltmarsh", -16500, 17700, 300, 0},
  {"Kaleo Springs", 23000, 14000, 360, 0},
  {"Ice Harbor", 31000, -24900, 270, 0},
};
const int kNumTowns = sizeof(kTowns) / sizeof(kTowns[0]);

// road network as polylines (km)
static const float kRoadPolys[][18] = {
  // n points, then x,z pairs (max 7 points)
  {8, -29.5f, 10.5f, -26.f, 7.5f, -20.f, 6.f, -15.3f, 3.25f, -12.f, -1.f, -6.f, -2.f, -3.2f, -1.2f},
  {2, -3.2f, -1.2f, 0.3f, -2.2f},
  {5, -3.2f, -1.2f, -4.f, 4.f, -4.f, 9.f, -6.6f, 15.7f, -8.f, 14.5f},
  {4, -6.6f, 15.7f, -3.f, 19.f, 0.2f, 22.5f, -0.5f, 21.f},
  {5, -6.6f, 15.7f, -12.f, 17.f, -20.f, 22.f, -26.f, 15.f, -29.5f, 10.5f},
  {6, -29.5f, 10.5f, -31.f, 3.f, -30.f, -2.f, -26.f, -4.f, -23.6f, -5.5f, -22.f, -6.f},
  {5, -23.6f, -5.5f, -25.f, -12.f, -28.f, -19.f, -25.3f, -24.1f, -26.f, -26.f},
  {2, 17.4f, 7.0f, 20.f, 8.f},
  {4, 17.4f, 7.0f, 19.f, 0.f, 21.f, -4.f, 22.6f, -6.5f},
  {4, 17.4f, 7.0f, 23.f, 14.f, 27.6f, 21.1f, 28.f, 20.f},
  {2, 17.5f, -28.6f, 16.f, -27.f},
  {3, 17.5f, -28.6f, 22.f, -28.5f, 27.f, -27.5f},
  {2, 35.f, 35.f, 36.f, 34.f},
  {3, -15.3f, 3.25f, -14.f, 4.f, -18.f, 9.f},
};
std::vector<RoadSeg> g_roads;
std::vector<CommunityPlan> g_communityPlans;
static std::vector<RoadSeg> legacyRoads;

static void initRoads() {
  static std::once_flag once;
  std::call_once(once, [] {
    for (const auto& p : kRoadPolys) {
      int n = (int)p[0];
      for (int i = 0; i + 1 < n; i++) {
        RoadSeg r{p[1 + 2 * i] * 1000.f, p[2 + 2 * i] * 1000.f, p[3 + 2 * i] * 1000.f, p[4 + 2 * i] * 1000.f};
        legacyRoads.push_back(r);
        // The first old polyline declares eight points but supplies seven. Preserve its
        // zero-filled final segment ONLY in the terrain modifier, not as a road to (0,0).
        if (&p != &kRoadPolys[0] || i < 6) g_roads.push_back(r);
      }
    }
    for (const Town& t : kTowns) {
      float best = 1e20f, c = 1.f, s = 0.f;
      for (const RoadSeg& r : g_roads) {
        float dx = r.bx - r.ax, dz = r.bz - r.az, length = hypotf(dx, dz);
        float u = clampf(((t.x - r.ax) * dx + (t.z - r.az) * dz) / (length * length), 0.f, 1.f);
        float distance = hypotf(r.ax + u * dx - t.x, r.az + u * dz - t.z);
        if (distance < best) { best = distance; c = dx / length; s = -dz / length; }
      }
      if (best > 2.f * t.r) { c = 1.f; s = 0.f; }
      g_communityPlans.push_back({c, s, t.kind == 2 ? 3 : t.kind == 1 ? 5 : 6, t.kind == 2 ? 2 : 4});
    }
  });
}

static void invalidateCommunityLotCache();
void sceneryInit() { initRoads(); invalidateCommunityLotCache(); }

static float roadDistanceIn(const std::vector<RoadSeg>& roads, float x, float z, int* segOut = nullptr) {
  float best = 1e9f; int bi = -1, k = 0;
  for (const RoadSeg& r : roads) {
    float dx = r.bx - r.ax, dz = r.bz - r.az;
    float t = clampf(((x - r.ax) * dx + (z - r.az) * dz) / (dx * dx + dz * dz + 1e-3f), 0, 1);
    float qx = r.ax + dx * t - x, qz = r.az + dz * t - z;
    float d2 = qx * qx + qz * qz;
    if (d2 < best) { best = d2; bi = k; }
    k++;
  }
  if (segOut) *segOut = bi;
  return sqrtf(best);
}

float roadDistance(float x, float z, int* segOut) { return roadDistanceIn(g_roads, x, z, segOut); }

int communityAt(float x, float z) {
  int best = -1; float distance = 1.f;
  for (int i = 0; i < kNumTowns; i++) {
    const Town& t = kTowns[i];
    float d = hypotf(x - t.x, z - t.z) / t.r;
    if (d < distance) { best = i; distance = d; }
  }
  return best;
}

vec2 communityLocal(int town, float x, float z) {
  const Town& t = kTowns[town]; const CommunityPlan& p = g_communityPlans[town];
  float dx = x - t.x, dz = z - t.z;
  return vec2(p.cosine * dx - p.sine * dz, p.sine * dx + p.cosine * dz);
}

vec2 communityWorld(int town, float x, float z) {
  const Town& t = kTowns[town]; const CommunityPlan& p = g_communityPlans[town];
  return vec2(t.x + p.cosine * x + p.sine * z, t.z - p.sine * x + p.cosine * z);
}

float communityStreetDistance(int town, float x, float z, float* yaw) {
  vec2 q = communityLocal(town, x, z); const CommunityPlan& p = g_communityPlans[town];
  float bx = p.blockX * LOT, bz = p.blockZ * LOT;
  float dx = q.x - floorf(q.x / bx + 0.5f) * bx, dz = q.y - floorf(q.y / bz + 0.5f) * bz;
  if (yaw) {
    float a = atan2f(p.sine, p.cosine);
    *yaw = a + (fabsf(dx) < fabsf(dz) ? (dx > 0.f ? -PI * 0.5f : PI * 0.5f) : (dz > 0.f ? PI : 0.f));
  }
  return std::min(fabsf(dx), fabsf(dz));
}

bool communityPark(int town, float localX, float localZ) {
  const CommunityPlan& p = g_communityPlans[town];
  return floorf(localX / (p.blockX * LOT)) == 0.f && floorf(localZ / (p.blockZ * LOT)) == 0.f;
}

void sceneryConnectRoads(const World& world) {
  initRoads();
  static std::once_flag once;
  std::call_once(once, [&] {
    const size_t trunkCount = g_roads.size();
    auto safe = [&](vec2 a, vec2 b) {
      float length = hypotf(b.x - a.x, b.y - a.y);
      if (length < 1.f || length > 6500.f) return false;
      int steps = (int)ceilf(length / 35.f); float previous = world.groundHeight(a.x, a.y, 7);
      for (int i = 0; i <= steps; i++) {
        float f = (float)i / steps, x = lerpf(a.x, b.x, f), z = lerpf(a.y, b.y, f);
        float h = world.groundHeight(x, z, 7);
        if (h < 2.f || fabsf(h - previous) > 4.f + length / steps * 0.24f) return false;
        for (const Airport& airport : world.airports) {
          vec2 q = aptLocal(airport, vec3(x, 0, z));
          if (fabsf(q.x) < airport.length * 0.5f + 250.f && fabsf(q.y) < airport.width * 0.5f + 75.f) return false;
        }
        previous = h;
      }
      return true;
    };
    for (const Town& town : kTowns) {
      if (g_roads.size() >= 64 || roadDistance(town.x, town.z) < 12.f) continue;
      vec2 start(town.x, town.z), destination; float best = 1e9f;
      for (size_t k = 0; k < trunkCount; k++) {
        const RoadSeg& r = g_roads[k]; vec2 ab(r.bx - r.ax, r.bz - r.az);
        float f = clampf(((start.x - r.ax) * ab.x + (start.y - r.az) * ab.y) / (ab.x * ab.x + ab.y * ab.y), 0.f, 1.f);
        vec2 p(r.ax + f * ab.x, r.az + f * ab.y); float d = length(p - start);
        if (d < best && safe(start, p)) { best = d; destination = p; }
      }
      if (best < 1e9f) g_roads.push_back({start.x, start.y, destination.x, destination.y});
    }
    for (size_t ai = 0; ai < world.airports.size() && g_roads.size() < 64; ai++) {
      const Airport& a = world.airports[ai]; AptLayout layout = aptLayout(a, (int)ai);
      float u = layout.termU + (layout.paved ? 0.f : 80.f);
      float v = layout.paved ? layout.lotV1 + 18.f : layout.bldV + 40.f;
      vec3 gate = aptWorld(a, u, layout.side * v, 0);
      vec2 start(gate.x, gate.z), destination; float best = 1e9f;
      for (size_t k = 0; k < trunkCount; k++) {
        const RoadSeg& r = g_roads[k]; vec2 ab(r.bx - r.ax, r.bz - r.az);
        float f = clampf(((start.x - r.ax) * ab.x + (start.y - r.az) * ab.y) / (ab.x * ab.x + ab.y * ab.y), 0.f, 1.f);
        vec2 p(r.ax + f * ab.x, r.az + f * ab.y);
        float d = length(p - start);
        vec2 endpoint = aptLocal(a, vec3(p.x, 0, p.y));
        float outward = endpoint.y * layout.side - v;
        // Leave through the landside gate, never back across hangars/apron. A shallow
        // angle would cut the adjacent fence despite the centreline passing its opening.
        if (outward < 30.f || outward < fabsf(endpoint.x - u) * 1.25f) continue;
        if (d < best && d < 6500.f && safe(start, p)) { best = d; destination = p; }
      }
      for (const Town& town : kTowns) {
        vec2 p(town.x, town.z); float d = length(p - start);
        vec2 endpoint = aptLocal(a, vec3(p.x, 0, p.y));
        float outward = endpoint.y * layout.side - v;
        if (outward < 30.f || outward < fabsf(endpoint.x - u) * 1.25f) continue;
        if (d < best && safe(start, p)) { best = d; destination = p; }
      }
      if (best < 1e9f) g_roads.push_back({start.x, start.y, destination.x, destination.y});
    }
  });
}

float valueNoise(float x, float z) {
  float fx = floorf(x), fz = floorf(z);
  int ix = (int)fx, iz = (int)fz;
  float tx = x - fx, tz = z - fz;
  tx = tx * tx * (3.f - 2.f * tx); tz = tz * tz * (3.f - 2.f * tz);
  float a = hash2i(ix, iz), b = hash2i(ix + 1, iz), c = hash2i(ix, iz + 1), d = hash2i(ix + 1, iz + 1);
  float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
  return ab + (cd - ab) * tz;
}

float coverFbm(float x, float z, int oct) {
  float s = 0, a = 0.5f;
  for (int i = 0; i < oct; i++) { s += a * valueNoise(x, z); x = x * 2.02f + 13.7f; z = z * 2.02f - 7.1f; a *= 0.5f; }
  return s;
}

void sceneryBaseMod(float x, float z, float& h, float& amp) {
  initRoads();
  // The terrain is a strict preservation boundary: do not use new access roads here.
  float rd = roadDistanceIn(legacyRoads, x, z);
  amp *= lerpf(0.3f, 1.f, smoothstepf(10.f, 70.f, rd));
  for (int i = 0; i < kNumTowns; i++) {
    const Town& t = kTerrainTowns[i];
    float d = sqrtf((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z));
    amp *= lerpf(0.4f, 1.f, smoothstepf(0.5f * t.r, 1.2f * t.r, d));
  }
  (void)h;
}

// ---------------------------------------------------------------- world integration
extern float airportInfluence(float x, float z);

void World::bakeMask() {
  initRoads();
  sceneryConnectRoads(*this);
  mask.assign((size_t)MASK_N * MASK_N * 4, 0);
  roadId.assign((size_t)MASK_N * MASK_N * 2, 0);
  parallelFor(MASK_N, [&](int j) {
    for (int i = 0; i < MASK_N; i++) {
      float x = -WORLD_HALF + (i + 0.5f) * MASK_TEXEL, z = -WORLD_HALF + (j + 0.5f) * MASK_TEXEL;
      float b[4]; sampleBase(x, z, b);
      int seg = -1;
      float rd = roadDistance(x, z, &seg);
      roadId[((size_t)j * MASK_N + i) * 2] = (uint8_t)(seg + 1);
      // forest-patch noise baked once instead of evaluating 3 octaves of value noise at every ray-march step
      roadId[((size_t)j * MASK_N + i) * 2 + 1] = (uint8_t)lroundf(clampf(coverFbm(x / 1400.f + 3.1f, z / 1400.f, 3) / 0.875f, 0, 1) * 255.f);
      float dens = 0, urban = 0;
      for (int k = 0; k < kNumTowns; k++) {
        const Town& t = kTowns[k];
        float d = sqrtf((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z));
        if (d > t.r) continue;
        float core = smoothstepf(t.r, 0.15f * t.r, d);
        float edge = smoothstepf(t.r, 0.8f * t.r, d);
        float dn = t.kind == 2 ? lerpf(0.45f, 0.92f, core) : t.kind == 1 ? lerpf(0.35f, 0.8f, core) : lerpf(0.22f, 0.6f, core);
        float ub = t.kind == 2 ? smoothstepf(0.85f * t.r, 0.f, d) : t.kind == 1 ? 0.62f * smoothstepf(0.7f * t.r, 0.f, d) : 0.2f * core;
        dens = std::max(dens, dn * edge * (0.75f + 0.5f * valueNoise(x / 220.f, z / 220.f)));
        urban = std::max(urban, ub);
      }
      float ai = airportInfluence(x, z);
      bool land = b[0] > 2.5f;
      dens *= (1.f - smoothstepf(0.02f, 0.12f, ai)) * (land ? 1.f : 0.f) * (b[1] < 90.f ? 1.f : 0.f);
      float a = 0;
      if (b[0] > 0.5f) {
        a = smoothstepf(4.f, 15.f, b[0]) * smoothstepf(420.f, 260.f, b[0]) * smoothstepf(0.4f, 0.6f, b[2]) * smoothstepf(45.f, 25.f, b[1]) *
            smoothstepf(0.32f, 0.5f, valueNoise(x / 3000.f + 7.7f, z / 3000.f - 3.3f)) * (1.f - smoothstepf(0.04f, 0.3f, dens)) * (1.f - smoothstepf(0.01f, 0.1f, ai));
      } else {
        float da; nearestAirport(x, z, &da);
        a = (ai < 0.001f && da > 2800.f && b[0] < -1.f && b[0] > -18.f) ? 1.f : 0.f;   // sea stacks allowed
      }
      uint8_t* o = &mask[((size_t)j * MASK_N + i) * 4];
      o[0] = (uint8_t)lroundf(clampf(rd / ROAD_RANGE, 0, 1) * 255.f);
      o[1] = (uint8_t)lroundf(clampf(dens, 0, 1) * 255.f);
      o[2] = (uint8_t)lroundf(clampf(urban, 0, 1) * 255.f);
      o[3] = (uint8_t)lroundf(clampf(a, 0, 1) * 255.f);
    }
  });
  sceneryBakeCommunityLots(*this);
}

void World::sampleMask(float x, float z, float out[4]) const {
  float fx = (x + WORLD_HALF) / MASK_TEXEL - 0.5f, fz = (z + WORLD_HALF) / MASK_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  int i0 = (int)flx, j0 = (int)flz;
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j, int c) { i = std::clamp(i, 0, MASK_N - 1); j = std::clamp(j, 0, MASK_N - 1); return mask[((size_t)j * MASK_N + i) * 4 + c] / 255.f; };
  for (int c = 0; c < 4; c++) {
    float a = at(i0, j0, c), b = at(i0 + 1, j0, c), cc = at(i0, j0 + 1, c), d = at(i0 + 1, j0 + 1, c);
    out[c] = (a * (1 - tx) + b * tx) * (1 - tz) + (cc * (1 - tx) + d * tx) * tz;
  }
}

void World::maskTexel(float x, float z, float out[4]) const {
  int i = std::clamp((int)floorf((x + WORLD_HALF) / MASK_TEXEL), 0, MASK_N - 1);
  int j = std::clamp((int)floorf((z + WORLD_HALF) / MASK_TEXEL), 0, MASK_N - 1);
  for (int c = 0; c < 4; c++) out[c] = mask[((size_t)j * MASK_N + i) * 4 + c] / 255.f;
}

float World::forestAt(float x, float z) const {
  float fx = (x + WORLD_HALF) / MASK_TEXEL - 0.5f, fz = (z + WORLD_HALF) / MASK_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  int i0 = (int)flx, j0 = (int)flz;
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j) { i = std::clamp(i, 0, MASK_N - 1); j = std::clamp(j, 0, MASK_N - 1); return roadId[((size_t)j * MASK_N + i) * 2 + 1] / 255.f; };
  float a = at(i0, j0), b = at(i0 + 1, j0), c = at(i0, j0 + 1), d = at(i0 + 1, j0 + 1);
  return ((a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz) * 0.875f;
}

float World::groundHeight(float x, float z, int octaves) const {
  float b[4]; sampleBase(x, z, b);
  if (b[1] < 0.01f) return b[0];
  return b[0] + b[1] * terrainFbm(x / DETAIL_SCALE, z / DETAIL_SCALE, octaves);
}

// Lots live in the settlement's local grid, with frontage on real drawn streets.
// The finite 28 m lattice bounds both generation cost and building density.
static bool calculateCommunityLot(const World& world, int town, int i, int j, Lot& L) {
  L = {}; L.town = town;
  float lx = (i + 0.5f) * LOT, lz = (j + 0.5f) * LOT;
  if (communityPark(town, lx, lz)) return false;
  vec2 center = communityWorld(town, lx, lz);
  L.cx = center.x; L.cz = center.y;
  if (communityAt(L.cx, L.cz) != town) return false;
  float street = communityStreetDistance(town, L.cx, L.cz, &L.yaw);
  if (street > 21.f) return false; // courtyards and back gardens, not another random row
  int si = i + town * 137, sj = j - town * 193;
  L.seed = hash2i(si * 17 + 3, sj * 19 + 5);
  float m[4]; world.maskTexel(L.cx, L.cz, m);
  if (m[1] < 0.025f || hash2i(si * 11 + 5, sj * 13 - 1) > m[1] * 1.12f) return false;
  float h3 = hash2i(si + 3, sj - 7), h4 = hash2i(si - 9, sj + 4), h5 = hash2i(si + 15, sj + 21);
  // Neighbours share a street line; small along-frontage variation avoids a stamped array.
  float jitter = (hash2i(si * 3, sj * 5) - 0.5f) * 2.f;
  L.cx += cosf(L.yaw) * jitter; L.cz -= sinf(L.yaw) * jitter;
  communityStreetDistance(town, L.cx, L.cz, &L.yaw);
  float urban = m[2];
  L.type = urban > 0.48f ? 1 : 0;
  if (L.type == 1) {
    L.hw = 7.f + 2.f * h3; L.hd = 6.f + 2.f * h4;
    // Dense island cities retain a substantial skyline. Height grows towards the core;
    // floor-height steps and coherent street frontage replace arbitrary scattered towers.
    L.wallH = 8.f + 3.2f * floorf(urban * urban * 58.f * powf(h5, 1.15f) / 3.2f);
    if (kTowns[town].kind == 2 && urban > 0.65f && L.seed < 0.25f) L.wallH = 44.f + 20.f * h5;
    L.roofH = 0.f; L.ridgeX = 0;
  } else {
    L.hw = 4.f + 1.8f * h3; L.hd = 4.8f + 1.8f * h4;
    L.wallH = 3.2f + (urban > 0.2f || h5 > 0.78f ? 3.f : 0.f);
    L.roofH = 1.8f + h5; L.ridgeX = L.seed < 0.5f ? 1 : 0;
  }
  // Include minimum fitted mesh sizes and wider shop/townhouse fronts in the support
  // envelope. The cached base then covers the actual chosen mesh without resampling its
  // footprint every time a chunk is streamed. It also makes road exclusion conservative.
  float supportW = std::max(L.hw, 4.2f), supportD = L.hd;
  float kindKey = hash2i(si * 31 + 7, sj * 17 - 3);
  bool shopFront = fabsf(lz) < LOT && urban > .08f && kindKey < .24f;
  if (L.type == 1) { supportW = std::max(supportW, 7.f); supportD = std::max(supportD, 7.f); }
  else if (urban > .23f || shopFront) { supportW = std::max(supportW, 7.f); supportD = std::max(supportD, 5.5f); }
  if (roadDistance(L.cx, L.cz) < hypotf(supportW, supportD) + 7.f) return false;
  float low = world.groundHeight(L.cx, L.cz, 7), high = low;
  float c = cosf(L.yaw), s = sinf(L.yaw);
  for (int k = 0; k < 4; k++) {
    float x = (k & 1) ? supportW : -supportW, z = (k & 2) ? supportD : -supportD;
    float gx = L.cx + c * x + s * z, gz = L.cz - s * x + c * z;
    float h = world.groundHeight(gx, gz, 7);
    if (airportInfluence(gx, gz) > 0.02f) return false;
    low = std::min(low, h); high = std::max(high, h);
  }
  if (low < 2.5f || high - low > 2.6f) return false;
  L.ground = low - 0.05f; L.present = true;
  return true;
}

namespace {
struct CommunityLotCache {
  int range = 0, side = 0;
  std::vector<Lot> lots;
};
std::vector<CommunityLotCache> communityLotCache;
const World* communityLotWorld = nullptr;
}

static void invalidateCommunityLotCache() { communityLotWorld = nullptr; }

void sceneryBakeCommunityLots(const World& world) {
  // World setup runs before chunk streaming. Keep expensive slope/airport checks here,
  // not in every neighbouring chunk and again when that chunk upgrades to tree detail.
  communityLotWorld = nullptr;
  communityLotCache.resize(kNumTowns);
  parallelFor(kNumTowns, [&](int town) {
    CommunityLotCache& cache = communityLotCache[town];
    cache.range = (int)ceilf(kTowns[town].r / LOT) + 1;
    cache.side = cache.range * 2 + 1;
    cache.lots.resize((size_t)cache.side * cache.side);
    for (int j = -cache.range; j <= cache.range; j++)
      for (int i = -cache.range; i <= cache.range; i++)
        calculateCommunityLot(world, town, i, j, cache.lots[(size_t)(j + cache.range) * cache.side + i + cache.range]);
  });
  communityLotWorld = &world;
}

bool communityLot(const World& world, int town, int i, int j, Lot& L) {
  if (town < 0 || town >= kNumTowns) { L = {}; return false; }
  if (communityLotWorld == &world) {
    const CommunityLotCache& cache = communityLotCache[town];
    if (i < -cache.range || i > cache.range || j < -cache.range || j > cache.range) { L = {}; return false; }
    L = cache.lots[(size_t)(j + cache.range) * cache.side + i + cache.range];
    return L.present;
  }
  return calculateCommunityLot(world, town, i, j, L);
}

bool World::lotAt(int i, int j, Lot& L) const {
  // Compatibility query for a world-space cell. Chunk generation uses local indices directly.
  float x = (i + 0.5f) * LOT, z = (j + 0.5f) * LOT;
  int town = communityAt(x, z);
  if (town < 0) { L = {}; return false; }
  vec2 q = communityLocal(town, x, z);
  return communityLot(*this, town, (int)floorf(q.x / LOT), (int)floorf(q.y / LOT), L);
}
