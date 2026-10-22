// Somnium Volandi - towns, roads, farmland, forests, rocks and buildings (CPU side; mirrored in shaders.h)
#include "scenery.h"
#include "airport_layout.h"
#include "entities.h"
#include "earth_charts.h"
#include <cstring>
#include <mutex>
#include <unordered_map>

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
  // The eastern islands, settled (2026-10): Kaleo's plain - a port city on its east shore, a market town in the middle,
  // villages round them - and a village each on Palm Bay's island, Far Isle and Nordholm's one level stretch of coast.
  // Each site surveyed dry and level over its whole radius (every 28 m lot's footprint within 2.6 m).
  {"Kailani", 25500, 1500, 1200, 2},
  {"Canefield", 20500, -2500, 700, 1},
  {"Ember Bay", 18500, -5500, 360, 0},
  {"Mango Grove", 17500, 0, 360, 0},
  {"Mill Creek", 23000, 5000, 360, 0},
  {"Coral Cove", 29000, 17200, 300, 0},
  {"Turtle Bay", 37800, 33400, 300, 0},
  {"Skarvik", 35200, -26000, 300, 0},
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
// The roads the islands were first drawn with: gone from the map (the road network replaced them, road_network.h), kept
// only as part of the heightfield's recipe - the ground along them was smoothed, and that ground is preserved
static std::vector<RoadSeg> legacyRoads;

static void initRoads() {
  static std::once_flag once;
  std::call_once(once, [] {
    for (const auto& p : kRoadPolys) {
      int n = (int)p[0];
      for (int i = 0; i + 1 < n; i++)   // (the first polyline declares eight points but supplies seven: its zero-filled
                                        // last segment, to (0, 0), is part of the recipe too)
        legacyRoads.push_back({p[1 + 2 * i] * 1000.f, p[2 + 2 * i] * 1000.f, p[3 + 2 * i] * 1000.f, p[4 + 2 * i] * 1000.f});
    }
  });
}

void sceneryInit() { initRoads(); }

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

// (measured as the old roads were: from the centreline of a country road 12 m across - so from the platform's edge, plus
// 6 m - whatever the class; the scenery's clearances were tuned to that)
float roadDistance(float x, float z, int* segOut) { return roadEdgeDistance(sceneryWorld().roadGrid, x, z, segOut) + 6.f; }

int communityAt(float x, float z) {
  const World& W = sceneryWorld();
  if (!W.settlements.empty()) {   // (the settlements' own extents, once they're grown)
    int town = -1; const float share = settlementShare(W.settlements, x, z, &town);
    return share < 1.f ? town : -1;
  }
  int best = -1; float distance = 1.f;
  for (int i = 0; i < (int)W.towns.size(); i++) {
    const Town& t = W.towns[i];
    float d = hypotf(x - t.x, z - t.z) / t.r;
    if (d < distance) { best = i; distance = d; }
  }
  return best;
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

void fieldPatch(float x, float z, float& border, float& crop) {
  const float qx = x / 170.f + valueNoise(x / 600.f, z / 600.f) * 0.6f, qz = z / 170.f + valueNoise(x / 600.f + 7.3f, z / 600.f + 7.3f) * 0.6f;
  const float gx = floorf(qx), gz = floorf(qz), fx = qx - gx, fz = qz - gz;
  float d1 = 9.f, d2 = 9.f; int idx = 0, idz = 0;
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    const int cx = (int)gx + i, cz = (int)gz + j;
    const float rx = i + hash2i(cx + 91, cz + 7) - fx, rz = j + hash2i(cx - 3, cz + 55) - fz, d = rx * rx + rz * rz;
    if (d < d1) { d2 = d1; d1 = d; idx = cx; idz = cz; } else if (d < d2) d2 = d;
  }
  border = sqrtf(d2) - sqrtf(d1);
  crop = hash2i(idx + 13, idz + 31);
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
  for (const Town& t : kTerrainTowns) {   // (the legacy list only: settlements added since leave the ground alone)
    float d = sqrtf((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z));
    amp *= lerpf(0.4f, 1.f, smoothstepf(0.5f * t.r, 1.2f * t.r, d));
  }
  (void)h;
}

// ---------------------------------------------------------------- world integration
void World::bakeMask() {
  generationCheckpoint();
  initRoads();
  sceneryBakeCommunityLots(*this);   // (first: a settlement's ground is as far as its buildings)
  generationPhase(GenerationPhase::Mask);
  mask.assign((size_t)MASK_N * MASK_N * 4, 0);
  if (roadGrid.head.empty()) roadGrid.head.assign((size_t)MASK_N * MASK_N, 0);
  roadGrid.forest.assign((size_t)MASK_N * MASK_N, 0);
  // the real Earth: its woods and its fields where the land cover has them (earth_charts.h maskFields), not the noise's
  std::vector<float> earthForest, earthFarm;
  if (earthChart) earthCharts::maskFields(*earthChart, earthForest, earthFarm);
  const bool earth = !earthForest.empty();
  const SettlementLookup settle(settlements);
  parallelFor(MASK_N, [&](int j) {
    for (int i = 0; i < MASK_N; i++) {
      float x = -WORLD_HALF + (i + 0.5f) * MASK_TEXEL, z = -WORLD_HALF + (j + 0.5f) * MASK_TEXEL;
      float b[4]; sampleBase(x, z, b);
      int rs = -1; const float re = roadEdgeDistance(roadGrid, x, z, &rs), rd = re + 6.f;   // (as roadDistance measures it)
      const bool raceGround = rs >= 0 && re < 250.f && roadGrid.segs[rs].cls >= RC_CIRCUIT;   // (the racetrack's: meadow, no wood, no crops)
      // forest-patch noise baked once instead of evaluating 3 octaves of value noise at every ray-march step: the road
      // grid's second channel
      float forestNoise = clampf(coverFbm(x / 1400.f + 3.1f, z / 1400.f, 3) / 0.875f, 0, 1);
      if (earth) {   // (the cover's woods, their edges ragged by the noise: in a closed forest well over the trees' threshold.
                     // The noise read where neighbouring charts read it, World::anchorPoint: their woods' edges agree)
        const float f = earthForest[(size_t)j * MASK_N + i];
        const vec2 a = anchorPoint(x, z);
        const float wide = clampf(coverFbm(a.x / 1400.f + 3.1f, a.y / 1400.f, 3) / 0.875f, 0, 1);
        forestNoise = clampf(0.08f + 0.55f * f + 0.32f * (coverFbm(a.x / 260.f + 3.1f, a.y / 260.f, 3) / 0.875f - 0.5f) + 0.12f * f * wide, 0, 1);
      }
      roadGrid.forest[(size_t)j * MASK_N + i] = raceGround ? 0 : (uint8_t)lroundf(forestNoise * 127.f);
      // the settlements (settlements.h, their lots): their ground as far as their buildings and gardens - the woods
      // and the fields come up to the last houses, and fill the gaps between the streets - its core urban
      float dens = 0, urban = 0;
      int town = -1; const float share = settle.share(x, z, &town);
      if (town >= 0 && share < 1.15f) {
        std::vector<int> near; lotsIn(*this, x - 70.f, z - 70.f, x + 70.f, z + 70.f, near);
        float d = 1e9f;
        for (int id : near) { const Lot& L = settlementLots(*this)[id]; d = std::min(d, hypotf(L.cx - x, L.cz - z) - std::max(L.hw, L.hd)); }
        const int kind = towns[town].kind;
        dens = smoothstepf(45.f, 8.f, d) * (kind == 2 ? 0.95f : kind == 1 ? 0.85f : 0.65f) * (0.85f + 0.3f * valueNoise(x / 220.f, z / 220.f));
        urban = (kind == 2 ? smoothstepf(0.65f, 0.15f, share) : kind == 1 ? 0.62f * smoothstepf(0.5f, 0.05f, share) : 0.2f * smoothstepf(0.4f, 0.f, share)) * smoothstepf(0.f, 0.3f, dens);
      }
      float ai = airportInfluence(*this, x, z);
      bool land = b[0] > 2.5f;
      dens *= (1.f - smoothstepf(0.02f, 0.12f, ai)) * (land ? 1.f : 0.f) * (b[1] < 90.f ? 1.f : 0.f);
      float a = 0;
      if (earth && b[0] > 0.5f) {   // (the cover's fields, never under a town or on an airfield)
        a = smoothstepf(0.15f, 0.6f, earthFarm[(size_t)j * MASK_N + i]) * (1.f - smoothstepf(0.04f, 0.3f, dens)) * (1.f - smoothstepf(0.01f, 0.1f, ai)) * smoothstepf(0.6f, 2.f, b[0]);
      } else if (b[0] > 0.5f) {
        a = smoothstepf(4.f, 15.f, b[0]) * smoothstepf(420.f, 260.f, b[0]) * smoothstepf(0.4f, 0.6f, b[2]) * smoothstepf(45.f, 25.f, b[1]) *
            smoothstepf(0.32f, 0.5f, valueNoise(x / 3000.f + 7.7f, z / 3000.f - 3.3f)) * (1.f - smoothstepf(0.04f, 0.3f, dens)) * (1.f - smoothstepf(0.01f, 0.1f, ai)) *
            (raceGround ? 0.f : 1.f);
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
}

void World::sampleMask(float x, float z, float out[4]) const {
  if (!repeating && (fabsf(x) > WORLD_HALF || fabsf(z) > WORLD_HALF)) {
    out[0] = 1.f; out[1] = out[2] = out[3] = 0.f; return;
  }
  float fx = (x + WORLD_HALF) / MASK_TEXEL - 0.5f, fz = (z + WORLD_HALF) / MASK_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  // Clamp the integer lookup, retaining the original interpolation fraction.
  // Even a distant ocean coordinate must never overflow a float-to-int cast.
  int i0 = (int)std::clamp(flx, -1.f, float(MASK_N)), j0 = (int)std::clamp(flz, -1.f, float(MASK_N));
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j, int c) { i = std::clamp(i, 0, MASK_N - 1); j = std::clamp(j, 0, MASK_N - 1); return mask[((size_t)j * MASK_N + i) * 4 + c] / 255.f; };
  for (int c = 0; c < 4; c++) {
    float a = at(i0, j0, c), b = at(i0 + 1, j0, c), cc = at(i0, j0 + 1, c), d = at(i0 + 1, j0 + 1, c);
    out[c] = (a * (1 - tx) + b * tx) * (1 - tz) + (cc * (1 - tx) + d * tx) * tz;
  }
}

void World::maskTexel(float x, float z, float out[4]) const {
  if (!repeating && (fabsf(x) > WORLD_HALF || fabsf(z) > WORLD_HALF)) {
    out[0] = 1.f; out[1] = out[2] = out[3] = 0.f; return;
  }
  int i = (int)std::clamp(floorf((x + WORLD_HALF) / MASK_TEXEL), 0.f, float(MASK_N - 1));
  int j = (int)std::clamp(floorf((z + WORLD_HALF) / MASK_TEXEL), 0.f, float(MASK_N - 1));
  for (int c = 0; c < 4; c++) out[c] = mask[((size_t)j * MASK_N + i) * 4 + c] / 255.f;
}

float World::forestAt(float x, float z) const {
  if (!repeating && (fabsf(x) > WORLD_HALF || fabsf(z) > WORLD_HALF)) return 0.f;
  float fx = (x + WORLD_HALF) / MASK_TEXEL - 0.5f, fz = (z + WORLD_HALF) / MASK_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  // Clamp the integer lookup, retaining the original interpolation fraction.
  // Even a distant ocean coordinate must never overflow a float-to-int cast.
  int i0 = (int)std::clamp(flx, -1.f, float(MASK_N)), j0 = (int)std::clamp(flz, -1.f, float(MASK_N));
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j) { i = std::clamp(i, 0, MASK_N - 1); j = std::clamp(j, 0, MASK_N - 1); return roadGrid.forest[(size_t)j * MASK_N + i] / 127.f; };
  float a = at(i0, j0), b = at(i0 + 1, j0), c = at(i0, j0 + 1), d = at(i0 + 1, j0 + 1);
  return ((a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz) * 0.875f;
}

float World::groundHeight(float x, float z, int octaves) const { return height(x, z, octaves); }

// ---------------------------------------------------------------- the settlements' lots
// Every building's place along a street or a road through a settlement, facing it: what stands there by how far out in
// the settlement it is - towers at a city's heart, offices and apartments round them, shops and terraces, then houses
// with their gardens, farmhouses at the edge, warehouses on the roads out of a town - its frontage, its setback from the
// road and the gap to the next as that kind has them; never on a road's platform, on another building, over the sea or
// an airfield, nor on ground falling more than its footprint can take. Laid out once the world is built or loaded.
namespace {
const float kLotCell = 64.f;
int64_t lotKey(int i, int j) { return (int64_t)i * 1000003 + j; }
void cellsAdd(std::unordered_map<int64_t, std::vector<int>>& cells, float x, float z, int id) {
  cells[lotKey((int)floorf(x / kLotCell), (int)floorf(z / kLotCell))].push_back(id);
}
void cellsIn(const std::unordered_map<int64_t, std::vector<int>>& cells, float x0, float z0, float x1, float z1, std::vector<int>& out) {
  out.clear();
  for (int j = (int)floorf(z0 / kLotCell); j <= (int)floorf(z1 / kLotCell); j++)
    for (int i = (int)floorf(x0 / kLotCell); i <= (int)floorf(x1 / kLotCell); i++) {
      auto it = cells.find(lotKey(i, j)); if (it == cells.end()) continue;
      out.insert(out.end(), it->second.begin(), it->second.end());
    }
}

// what stands on a lot: its kind, half frontage and depth, setback from the road's platform, the gap to the next
struct LotPlan { int kind; float hw, hd, setback, gap; };
LotPlan planLot(int townKind, float share, int cls, float h, float h2, vec2 at) {
  // (by district: a little further in here, further out there - no zone a ring; but a city's heart is always its heart)
  const float calm = townKind == 2 ? smoothstepf(0.f, 0.2f, share) : 1.f;
  share = std::max(0.f, share + (valueNoise(at.x / 350.f + 17.f, at.y / 350.f - 5.f) - 0.5f) * (townKind == 0 ? 0.1f : 0.2f) * calm);
  auto plan = [&](int k, float setback, float gap) {
    const EntKindInfo& I = kEntInfo[k];
    return LotPlan{k, I.hx * (0.88f + 0.24f * h2), I.hz * (0.92f + 0.16f * h), setback, gap};
  };
  auto house = [&]() { return h < 0.38f ? plan(EK_HOUSE, 6.f, 6.f) : h < 0.7f ? plan(EK_HOUSE_HIP, 6.f, 6.f) : plan(EK_HOUSE_L, 7.f, 7.f); };
  const bool road = cls != RC_STREET;
  if (road && townKind > 0 && share > 0.72f && h < 0.22f) return plan(EK_WAREHOUSE, 10.f, 10.f);   // (a town's way out)
  if (townKind == 2) {
    // a city: its skyline peaks at the heart - the supertalls, the skyscrapers and round towers about them - and falls
    // away through towers and tower blocks to the mid-rise office blocks, then flats, terraces and houses
    if (share < 0.04f) return h < 0.3f ? plan(EK_SUPERTALL, 10.f, 18.f) : h < 0.65f ? plan(EK_SKYSCRAPER, 8.f, 14.f) : plan(EK_ROUNDTOWER, 8.f, 14.f);
    if (share < 0.08f) return h < 0.25f ? plan(EK_SKYSCRAPER, 8.f, 14.f) : h < 0.45f ? plan(EK_ROUNDTOWER, 8.f, 14.f) : h < 0.7f ? plan(EK_TOWER, 6.f, 10.f) : plan(EK_SLABTOWER, 6.f, 10.f);
    if (share < 0.13f) return h < 0.15f ? plan(EK_TOWER, 6.f, 10.f) : h < 0.35f ? plan(EK_SLABTOWER, 6.f, 10.f) : h < 0.55f ? plan(EK_OFFICE, 4.f, 6.f) : h < 0.85f ? plan(EK_MIDRISE, 2.f, 3.f) : plan(EK_APARTMENT, 3.f, 4.f);
    if (share < 0.19f) return h < 0.4f ? plan(EK_MIDRISE, 1.f, 1.f) : h < 0.55f ? plan(EK_OFFICE, 4.f, 6.f) : h < 0.65f ? plan(EK_SLABTOWER, 6.f, 10.f) : plan(EK_APARTMENT, 3.f, 4.f);
    if (share < 0.3f) return road && h < 0.3f ? plan(EK_SHOP, 0.8f, 0.5f) : h < 0.15f ? plan(EK_MIDRISE, 1.f, 1.f) : h < 0.25f ? plan(EK_OFFICE, 4.f, 6.f) : h < 0.65f ? plan(EK_APARTMENT, 3.f, 4.f) : plan(EK_TOWNHOUSE, 2.5f, 0.5f);
    if (share < 0.5f) return road && h < 0.3f ? plan(EK_SHOP, 0.8f, 0.5f) : h < 0.3f ? plan(EK_APARTMENT, 3.f, 4.f) : h < 0.65f ? plan(EK_TOWNHOUSE, 2.5f, 0.5f) : house();
    if (share < 0.75f) return h < 0.3f ? plan(EK_TOWNHOUSE, 2.5f, 0.5f) : house();
    return house();
  }
  if (townKind == 1) {
    if (share < 0.15f) return h < 0.4f ? plan(EK_SHOP, 0.8f, 0.5f) : h < 0.65f ? plan(EK_APARTMENT, 3.f, 4.f) : h < 0.95f ? plan(EK_TOWNHOUSE, 2.5f, 0.5f) : plan(EK_OFFICE, 4.f, 6.f);
    if (share < 0.42f) return road && h < 0.3f ? plan(EK_SHOP, 0.8f, 0.5f) : h < 0.5f ? plan(EK_TOWNHOUSE, 2.5f, 0.5f) : house();
    if (share > 0.85f && h > 0.9f) return plan(EK_FARMHOUSE, 12.f, 20.f);
    return house();
  }
  if (share < 0.3f) return road && h < 0.3f ? plan(EK_SHOP, 0.8f, 0.5f) : h < 0.55f ? plan(EK_TOWNHOUSE, 2.5f, 0.5f) : house();
  if (share > 0.8f && h > 0.65f) return plan(EK_FARMHOUSE, 12.f, 20.f);
  return house();
}

// an oriented rectangle: centre, its frontage's direction (unit), half extents along it and back from it
struct Footprint { vec2 c, u; float hw, hd; };
bool overlaps(const Footprint& a, const Footprint& b) {
  const vec2 av(-a.u.y, a.u.x), bv(-b.u.y, b.u.x), d = b.c - a.c;
  for (vec2 ax : {a.u, av, b.u, bv}) {
    const float ra = a.hw * fabsf(dot2(ax, a.u)) + a.hd * fabsf(dot2(ax, av)), rb = b.hw * fabsf(dot2(ax, b.u)) + b.hd * fabsf(dot2(ax, bv));
    if (fabsf(dot2(d, ax)) > ra + rb) return false;
  }
  return true;
}
}  // namespace

const WorldSceneryState& sceneryState(const World& world) {
  static const WorldSceneryState empty;
  return world.sceneryState ? *world.sceneryState : empty;
}

WorldSceneryState& sceneryStateForBuild(World& world) {
  if (!world.sceneryState) world.sceneryState = std::make_shared<WorldSceneryState>();
  else if (!world.sceneryState.unique()) world.sceneryState = std::make_shared<WorldSceneryState>(*world.sceneryState);
  return *world.sceneryState;
}

const std::vector<Lot>& settlementLots(const World& world) { return sceneryState(world).lots; }
const std::vector<PlacedEnt>& civicLandmarks(const World& world) { return sceneryState(world).civic; }
const std::vector<TreeSpot>& settlementTrees(const World& world) { return sceneryState(world).trees; }
void lotsIn(const World& world, float x0, float z0, float x1, float z1, std::vector<int>& out) { cellsIn(sceneryState(world).lotCells, x0, z0, x1, z1, out); }
void treesIn(const World& world, float x0, float z0, float x1, float z1, std::vector<int>& out) { cellsIn(sceneryState(world).treeCells, x0, z0, x1, z1, out); }
const std::vector<Lot>& settlementLots() { return settlementLots(sceneryWorld()); }
const std::vector<PlacedEnt>& civicLandmarks() { return civicLandmarks(sceneryWorld()); }
const std::vector<TreeSpot>& settlementTrees() { return settlementTrees(sceneryWorld()); }
void lotsIn(float x0, float z0, float x1, float z1, std::vector<int>& out) { lotsIn(sceneryWorld(), x0, z0, x1, z1, out); }
void treesIn(float x0, float z0, float x1, float z1, std::vector<int>& out) { treesIn(sceneryWorld(), x0, z0, x1, z1, out); }

void sceneryBakeCommunityLots(World& world) {
  generationPhase(GenerationPhase::CommunityLots);
  WorldSceneryState& state = sceneryStateForBuild(world);
  state.lots.clear(); state.trees.clear(); state.lotCells.clear(); state.treeCells.clear(); state.civic.clear();
  if (world.settlements.empty()) return;
  const SettlementLookup settle(world.settlements);
  RoadIndex roads;
  for (size_t pi = 0; pi < world.roads.paths.size(); pi++) roads.addPath(world.roads.paths[pi], (int)pi);
  // what the lots are laid into: the footprints claimed so far (with their margins, in 64 m cells), and the lots made
  struct Claims { std::unordered_map<int64_t, std::vector<int>> placed; std::vector<Footprint> prints; std::vector<Lot> lots; std::vector<int> near; };
  Claims main;
  auto ground = [&](float x, float z) { return world.groundHeight(x, z, 7); };
  // a footprint, if it can stand there: on dry, buildable ground, clear of the roads and of the buildings so far
  auto fits = [&](Claims& C, const Footprint& f, float margin, float fall, int town, float maxShare, float& base) {
    // (all of these together, so in any order: the cheap first - the buildings so far, the roads, the settlement - and
    // the ground's last, most places failing before it)
    Footprint m = f; m.hw += margin; m.hd += margin;
    // (the cells its own square reaches: any footprint overlapping it was claimed in one of them, claim() filing each in
    // every cell its circle reaches)
    const float r = hypotf(m.hw, m.hd);
    cellsIn(C.placed, m.c.x - r, m.c.y - r, m.c.x + r, m.c.y + r, C.near);
    for (int id : C.near) if (overlaps(m, C.prints[id])) return false;
    const vec2 v(-f.u.y, f.u.x);
    vec2 at[9];
    for (int k = 0; k < 9; k++) {
      const float a = (k % 3 - 1) * f.hw, b = (k / 3 - 1) * f.hd;
      const vec2 p = f.c + f.u * a + v * b;
      at[k] = p;
      if (roads.nearest(p, 3.f, -1, nullptr, nullptr, true) < 1.f) return false;   // (a platform within a metre)
      int t = -1; const float share = settle.share(p.x, p.y, &t);
      if (t != town || share > maxShare || airportExclusion(world, p.x, p.y) > 0.02f) return false;
    }
    float lo = 1e9f, hi = -1e9f;
    for (const vec2& p : at) {
      const float g = ground(p.x, p.y);
      lo = std::min(lo, g); hi = std::max(hi, g);
      if (g < buildableHeight(world, p.x, p.y)) return false;
    }
    if (hi - lo > fall) return false;
    base = lo - 0.05f;
    return true;
  };
  auto claim = [&](Claims& C, const Footprint& f, float margin) {
    Footprint g = f; g.hw += margin; g.hd += margin;
    const int id = (int)C.prints.size(); C.prints.push_back(g);
    // (in every cell its corners reach)
    const float r = hypotf(g.hw, g.hd);
    for (int j = (int)floorf((g.c.y - r) / kLotCell); j <= (int)floorf((g.c.y + r) / kLotCell); j++)
      for (int i = (int)floorf((g.c.x - r) / kLotCell); i <= (int)floorf((g.c.x + r) / kLotCell); i++) C.placed[lotKey(i, j)].push_back(id);
  };
  auto addLot = [&](Claims& C, const Footprint& f, int kind, float base, float seed, int town, int street, float share) {
    if (world.earthChart) {   // (the real Earth: under an approach, only a building below its surface - airportExclusion)
      float grounds, cap; airportGroundsCap(world, f.c.x, f.c.y, grounds, cap);
      if (base + kEntInfo[kind].h > cap) return;
    }
    Lot L = {};
    L.present = true; L.cx = f.c.x; L.cz = f.c.y; L.hw = f.hw; L.hd = f.hd; L.ground = base;
    const vec2 front(-f.u.y, f.u.x);   // (local +z: the frontage turned a quarter - towards the road)
    L.yaw = atan2f(front.x, front.y);
    L.kind = kind; L.seed = seed; L.town = town; L.street = street; L.share = share;
    L.type = kind == EK_APARTMENT || kind == EK_OFFICE || (kind >= EK_TOWER && kind <= EK_MIDRISE) ? 1 : 0;
    L.wallH = kEntInfo[kind].h; L.roofH = 0.f; L.ridgeX = seed < 0.5f ? 1 : 0;
    C.lots.push_back(L);
  };
  auto tree = [&](float x, float z, float key, float scale) {
    float base[4]; world.sampleBase(x, z, base);
    const int kind = base[3] > .45f ? EK_SPRUCE : base[2] > .85f && ground(x, z) < 70.f && key < 0.5f ? EK_PALM : key < 0.3f ? EK_BIRCH : EK_OAK;
    const int id = (int)state.trees.size(); state.trees.push_back({x, z, scale, kind, key}); cellsAdd(state.treeCells, x, z, id);
  };
  // ---- each settlement's centre: a church on its main road, and in a town or a city a park beside it
  for (const SettlementField& F : world.settlements) {
    generationCheckpoint();
    const vec2 g(F.gx, F.gz), u(F.axisC, F.axisS), v(-F.axisS, F.axisC);
    const float roadEdge = roadSpec(RC_ROAD).halfPlatform;
    for (float side : {1.f, -1.f}) {
      Footprint f{g + v * (side * (roadEdge + 8.f + kEntInfo[EK_CHURCH].hz)), u * -side, kEntInfo[EK_CHURCH].hx, kEntInfo[EK_CHURCH].hz};
      float base;
      if (!fits(main, f, 3.f, 2.5f, F.town, 1.f, base)) continue;
      claim(main, f, 3.f); addLot(main, f, EK_CHURCH, base, hash2i(F.town, 739), F.town, -1, 0.f);
      if (F.kind > 0) {   // (the park across the way: trees on a loose grid, its ground claimed from the lots)
        const float R = F.kind == 2 ? 75.f : 55.f;
        const vec2 pc = g - v * (side * (roadEdge + 6.f + R));
        Footprint park{pc, u, R * 0.8f, R * 0.8f};
        float pb;
        if (fits(main, park, 0.f, 6.f, F.town, 1.f, pb)) {
          claim(main, park, 0.f);
          for (float a = -R; a <= R; a += 11.f) for (float b = -R; b <= R; b += 11.f) {
            const float key = hash2i((int)(pc.x + a), (int)(pc.y + b));
            const vec2 p = pc + u * (a + (key - 0.5f) * 6.f) + v * (b + (hash2i((int)(pc.y + b), (int)(pc.x + a)) - 0.5f) * 6.f);
            if (hypotf(a, b) > R || key > 0.55f || roads.nearest(p, 6.f, -1, nullptr, nullptr, true) < 3.f) continue;
            tree(p.x, p.y, key, 0.7f + 0.3f * key);
          }
        }
      }
      break;
    }
  }
  // ---- the cities' landmarks (docs/LIVING_ISLANDS_PLAN.md, Phase C), each on ground no road crosses (on a fine grid:
  // fits() looks only at a footprint's corners, edges and middle) and claimed before the lots: Solace Capital's stadium,
  // on the flattest dry ground nearest its heart that it fits; Port Verde's container terminal, along a straight 430 m
  // of its shore with deep water off it and low, flat land behind
  auto open = [&](const Footprint& f, float step, float lowest, float highest, float fall, float& lo, float& hi) {
    const vec2 v(-f.u.y, f.u.x);
    lo = 1e9f; hi = -1e9f;
    for (float a = -f.hw; a <= f.hw + 0.1f; a += step)
      for (float b = -f.hd; b <= f.hd + 0.1f; b += step) {
        const vec2 p = f.c + f.u * a + v * b;
        const float g = ground(p.x, p.y);
        lo = std::min(lo, g); hi = std::max(hi, g);
        if (g < lowest || g > highest || hi - lo > fall || airportInfluence(world, p.x, p.y) > 0.02f) return false;
        if (roads.nearest(p, 4.f, -1, nullptr, nullptr, true) < 2.f) return false;
      }
    std::vector<int> near;
    cellsIn(main.placed, f.c.x - 300.f, f.c.y - 300.f, f.c.x + 300.f, f.c.y + 300.f, near);
    for (int id : near) if (overlaps(f, main.prints[id])) return false;
    return true;
  };
  for (const SettlementField& F : world.settlements) {
    generationCheckpoint();
    if (!world.procedural && !strcmp(world.towns[F.town].name, "Solace Capital")) {
      const vec2 u(F.axisC, F.axisS);
      float bestShare = 1e9f, base = 0.f; Footprint best{};
      for (float dz = -2600.f; dz <= 2600.f; dz += 50.f)
        for (float dx = -2600.f; dx <= 2600.f; dx += 50.f) {
          generationCheckpoint();
          const vec2 c(F.cx + dx, F.cz + dz);
          int t = -1; const float share = settle.share(c.x, c.y, &t);
          if (t != F.town || share < 0.3f || share > 1.15f || share >= bestShare) continue;
          const Footprint f{c, u, 96.f, 80.f};
          float lo, hi;
          if (!open(f, 12.f, 2.5f, 1e9f, 5.f, lo, hi)) continue;
          bestShare = share; best = f; base = lo;
        }
      if (bestShare < 1e8f) { claim(main, best, 6.f); state.civic.push_back({EK_STADIUM, {best.c.x, base - 0.1f, best.c.y, atan2f(-u.y, u.x), 1.f, 1.f, 1.f, 0.5f}}); }
    }
    if (!world.procedural && !strcmp(world.towns[F.town].name, "Port Verde")) {
      // (the city stands on its hill, its shore 4 km off: the terminal is reclaimed from the shallows there out to 8 m of
      // water at its berth - its apron's back on the land, the nearest such stretch to the city)
      float bestScore = 1e9f, top = 0.f; vec2 at, sea;
      for (float dz = -6000.f; dz <= 6000.f; dz += 25.f)
        for (float dx = -6000.f; dx <= 6000.f; dx += 25.f) {
          generationCheckpoint();
          const vec2 P(F.cx + dx, F.cz + dz);
          const float dist = hypotf(dx, dz);
          if (dist >= bestScore) continue;
          const float g = ground(P.x, P.y);
          if (g < -7.f || g > 3.f) continue;
          int t = -1; const float share = settle.share(P.x, P.y, &t);
          if (t >= 0 && t != F.town && share < 1.2f) continue;
          for (int k = 0; k < 16; k++) {
            const vec2 d(cosf(k * 0.3927f), sinf(k * 0.3927f)), a(-d.y, d.x);
            bool ok = true; int landed = 0;
            for (float s = -215.f; s <= 215.f && ok; s += 30.f) {
              const vec2 Q = P + a * s;
              ok = ground(Q.x + d.x * 20.f, Q.y + d.y * 20.f) < -8.f && ground(Q.x + d.x * 45.f, Q.y + d.y * 45.f) < -8.f;
              landed += ground(Q.x - d.x * 125.f, Q.y - d.y * 125.f) > 0.5f;
            }
            if (!ok || landed < 8) continue;
            const Footprint f{P - d * 62.f, a, 215.f, 62.f};
            float lo, hi;
            if (!open(f, 20.f, -12.f, 6.f, 18.f, lo, hi)) continue;
            bestScore = dist; at = P; sea = d; top = std::max(hi + 0.2f, 3.f);
            break;
          }
        }
      if (bestScore < 1e8f) {
        const vec2 a(-sea.y, sea.x);
        claim(main, Footprint{at - sea * 62.f, a, 215.f, 62.f}, 8.f);
        const float yaw = atan2f(sea.x, sea.y);   // (local +z out to sea, along the quay +x)
        auto here = [&](float x, float z) { return at + vec2(sea.y, -sea.x) * x + sea * z; };
        state.civic.push_back({EK_QUAY, {at.x, top, at.y, yaw, 1.f, 1.f, 1.f, 0.5f}});
        for (float x : {-140.f, -50.f, 40.f, 130.f}) { const vec2 p = here(x, -18.f); state.civic.push_back({EK_GANTRY, {p.x, top, p.y, yaw, 1.f, 1.f, 1.f, hash2i((int)p.x, 3)}}); }
        for (float z : {-58.f, -78.f, -98.f})
          for (float x : {-160.f, -78.f, 4.f, 86.f}) {
            const vec2 p = here(x, z); const float h = hash2i((int)p.x, (int)p.y);
            if (h < 0.15f) continue;   // (a block or two away on the ships)
            state.civic.push_back({EK_CONTAINERS, {p.x, top, p.y, yaw + (h < 0.55f ? 3.14159f : 0.f), 1.f, 1.f, 1.f, h}});
          }
        const vec2 ship = here(-20.f, 20.f);   // (alongside, her length along the quay)
        state.civic.push_back({EK_CONTAINERSHIP, {ship.x, 0.f, ship.y, atan2f(sea.y, -sea.x), 1.f, 1.f, 1.f, 0.5f}});
      }
    }
  }
  // ---- along the roads, then the streets: each side walked, a lot wherever one fits. A city's first lot near its heart
  // is its landmark, the supertall, wherever the first site for one fits. (only >= 0: that town's lots alone, from the
  // paths given)
  std::vector<char> landmark(world.towns.size(), 0);
  auto walk = [&](Claims& C, int only, const std::vector<int>& paths) {
    for (int pass = 0; pass < 2; pass++)
      for (int pi : paths) {
        generationCheckpoint();
        const RoadPath& P = world.roads.paths[pi];
        if (P.cls == RC_HIGHWAY || (P.cls >= RC_TRACK && P.cls != RC_STREET) || (pass == 0) != (P.cls != RC_STREET) || P.pts.size() < 2) continue;   // (no lots along a farm's track or in its yard, nor by the racetrack)
        const float half = roadSpec(P.cls).halfPlatform;
        std::vector<float> along(P.pts.size(), 0.f);
        for (size_t k = 1; k < P.pts.size(); k++) along[k] = along[k - 1] + hypotf(P.pts[k].x - P.pts[k - 1].x, P.pts[k].z - P.pts[k - 1].z);
        for (int sideI = 0; sideI < 2; sideI++) {
          const float side = sideI ? 1.f : -1.f;
          size_t k = 0;
          for (float s = 6.f + 8.f * hash2i((int)pi, sideI); s < along.back() - 4.f;) {
            generationCheckpoint();
            while (k + 2 < P.pts.size() && along[k + 1] < s) k++;
            if (k < P.bridge.size() && P.bridge[k]) { s += 10.f; continue; }
            const RoadPoint &A = P.pts[k], &B = P.pts[k + 1];
            const float t = std::clamp((s - along[k]) / std::max(along[k + 1] - along[k], 1e-3f), 0.f, 1.f);
            const vec2 at(A.x + (B.x - A.x) * t, A.z + (B.z - A.z) * t), d = vec2(B.x - A.x, B.z - A.z) * (1.f / std::max(along[k + 1] - along[k], 1e-3f));
            const vec2 out = vec2(-d.y, d.x) * side;
            int town = -1; const float share = settle.share(at.x, at.y, &town);
            const float reach = P.cls == RC_STREET ? 1.f : 1.1f;   // (houses strung out a little way along the roads out)
            if (town < 0 || share >= reach || (only >= 0 && town != only)) { s += 12.f; continue; }
            const int ix = (int)floorf(at.x / 3.f) * 7 + sideI, iz = (int)floorf(at.y / 3.f);
            const float h = hash2i(ix + 911, iz - 77), h2v = hash2i(ix - 31, iz + 503), sparse = hash2i(ix + 5, iz + 9);
            LotPlan L = planLot(world.towns[town].kind, share, P.cls, h, h2v, at);
            if (world.towns[town].kind == 2 && !landmark[town] && share < 0.08f) {
              const EntKindInfo& I = kEntInfo[EK_SUPERTALL];
              L = LotPlan{EK_SUPERTALL, I.hx, I.hz, 10.f, 18.f};
            }
            // (thinning out towards the edge)
            if (sparse < smoothstepf(0.7f, reach, share) * 0.75f) { s += L.hw * 2.f + L.gap; continue; }
            const float dist = half + L.setback + L.hd;
            Footprint f{at + out * dist, d * side, L.hw, L.hd};
            f.u = vec2(-out.y, out.x);   // (the frontage along the road, its front - local +z - towards it)
            float base;
            const bool big = (L.kind >= EK_OFFICE && L.kind <= EK_MIDRISE) || L.kind == EK_WAREHOUSE;
            if (fits(C, f, std::max(0.25f, L.gap * 0.5f), big ? 3.5f : 2.6f, town, reach, base)) {
              claim(C, f, std::max(0.25f, L.gap * 0.5f));
              addLot(C, f, L.kind, base, h2v, town, pi, share);
              landmark[town] = landmark[town] || L.kind == EK_SUPERTALL;
              s += L.hw * 2.f + L.gap;
            } else s += 4.f;
          }
        }
      }
  };
  std::vector<int> every(world.roads.paths.size());
  for (size_t pi = 0; pi < every.size(); pi++) every[pi] = (int)pi;
  if (!world.earthChart) walk(main, -1, every);
  else {
    // (a real-Earth chart's hundreds of thousands of lots: each town's walked on its own, in parallel - a lot lies
    // wholly in its own town, so the towns' walks hardly meet - each from the claims so far, along the paths reaching
    // its field; their lots then in the towns' order)
    std::vector<Claims> jobs(world.settlements.size());
    struct Bounds { float lx, lz, hx, hz; };
    std::vector<Bounds> bounds(every.size(), Bounds{1e9f, 1e9f, -1e9f, -1e9f});   // (each path's, once)
    for (int pi : every)
      for (const RoadPoint& q : world.roads.paths[pi].pts) {
        Bounds& b = bounds[pi];
        b.lx = std::min(b.lx, q.x); b.hx = std::max(b.hx, q.x); b.lz = std::min(b.lz, q.z); b.hz = std::max(b.hz, q.z);
      }
    parallelFor((int)jobs.size(), [&](int k) {
      const SettlementField& F = world.settlements[k];
      const float x1 = F.x0 + F.n * SETTLE_CELL, z1 = F.z0 + F.n * SETTLE_CELL;
      std::vector<int> paths;
      for (int pi : every) {
        const Bounds& b = bounds[pi];
        if (b.hx >= F.x0 && b.lx <= x1 && b.hz >= F.z0 && b.lz <= z1) paths.push_back(pi);
      }
      jobs[k].placed = main.placed; jobs[k].prints = main.prints;
      walk(jobs[k], F.town, paths);
    });
    for (Claims& J : jobs) main.lots.insert(main.lots.end(), J.lots.begin(), J.lots.end());
  }
  for (const Lot& L : main.lots) { const int id = (int)state.lots.size(); state.lots.push_back(L); cellsAdd(state.lotCells, L.cx, L.cz, id); }
  // ---- street trees: along the streets of a town's or a city's centre, on the pavements every 14 m, clear of the
  // junctions and of the buildings' fronts
  for (size_t pi = 0; pi < world.roads.paths.size(); pi++) {
    generationCheckpoint();
    const RoadPath& P = world.roads.paths[pi];
    if (P.cls != RC_STREET) continue;
    float acc = 0.f, arc = 0.f;
    for (size_t k = 1; k < P.pts.size(); k++) {
      const vec2 a(P.pts[k - 1].x, P.pts[k - 1].z), b(P.pts[k].x, P.pts[k].z);
      const float l = length(b - a); if (l < 1e-3f) continue;
      const vec2 d = (b - a) * (1.f / l), n(-d.y, d.x);
      for (float s = 14.f - acc; s < l; s += 14.f) {
        const vec2 at = a + d * s;
        int town = -1; const float share = settle.share(at.x, at.y, &town);
        if (town < 0 || world.towns[town].kind == 0 || share > 0.6f) continue;
        for (float side : {-1.f, 1.f}) {
          // (not on a street lamp: road_furniture.cpp stands them on the right, as it runs, 15 m on and every 30 m, on
          // the left every 30 m)
          const float r = fmodf(arc + s + (side > 0.f ? 15.f : 0.f), 30.f);
          if (std::min(r, 30.f - r) < 3.f) continue;
          const vec2 p = at + n * (side * (roadSpec(RC_STREET).halfPaved + 1.2f));
          if (roads.nearest(p, 8.f, (int)pi, nullptr, nullptr, true) < 4.f || ground(p.x, p.y) < 2.5f) continue;   // (another road: a junction)
          tree(p.x, p.y, hash2i((int)p.x, (int)p.y), 0.55f + 0.15f * hash2i((int)p.y, (int)p.x));
        }
      }
      acc = fmodf(acc + l, 14.f); arc += l;
    }
  }
}
