// Air Xpress - towns, roads, farmland, forests, rocks and buildings (CPU side; mirrored in shaders.h)
#include "scenery.h"

// ---------------------------------------------------------------- hand-placed settlements (metres)
const Town kTowns[] = {
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

static void initRoads() {
  if (!g_roads.empty()) return;
  for (auto& p : kRoadPolys) {
    int n = (int)p[0];
    for (int i = 0; i + 1 < n; i++)
      g_roads.push_back({p[1 + 2 * i] * 1000.f, p[2 + 2 * i] * 1000.f, p[3 + 2 * i] * 1000.f, p[4 + 2 * i] * 1000.f});
  }
}

float roadDistance(float x, float z, int* segOut) {
  float best = 1e9f; int bi = -1, k = 0;
  for (const RoadSeg& r : g_roads) {
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
  float rd = roadDistance(x, z);
  amp *= lerpf(0.3f, 1.f, smoothstepf(10.f, 70.f, rd));
  for (int i = 0; i < kNumTowns; i++) {
    const Town& t = kTowns[i];
    float d = sqrtf((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z));
    amp *= lerpf(0.4f, 1.f, smoothstepf(0.5f * t.r, 1.2f * t.r, d));
  }
  (void)h;
}

// ---------------------------------------------------------------- world integration
extern float airportInfluence(float x, float z);

void World::bakeMask() {
  initRoads();
  mask.assign((size_t)MASK_N * MASK_N * 4, 0);
  roadId.assign((size_t)MASK_N * MASK_N * 2, 0);
  for (int j = 0; j < MASK_N; j++)
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

bool World::lotAt(int i, int j, Lot& L) const {
  L.present = false;
  L.cx = (i + 0.5f + (hash2i(i * 3 + 1, j * 5 + 2) - 0.5f) * 0.14f) * LOT;
  L.cz = (j + 0.5f + (hash2i(i * 7 - 3, j * 3 + 9) - 0.5f) * 0.14f) * LOT;
  float m[4]; maskTexel(L.cx, L.cz, m);
  if (m[1] < 0.01f || hash2i(i * 11 + 5, j * 13 - 1) > m[1] * 1.25f) return false;
  float h3 = hash2i(i + 3, j - 7), h4 = hash2i(i - 9, j + 4), h5 = hash2i(i + 15, j + 21), h6 = hash2i(i - 31, j - 2);
  L.seed = hash2i(i * 17 + 3, j * 19 + 5);
  float urban = m[2];
  if (urban > 0.45f) {
    L.type = 1;
    L.hw = 6.f + 3.5f * h3; L.hd = 6.f + 3.5f * h4;
    L.wallH = 9.f + urban * urban * 50.f * powf(h5, 1.5f); L.roofH = 0; L.ridgeX = 0;
  } else {
    L.type = 0;
    L.hw = 3.5f + 2.f * h3; L.hd = 4.5f + 2.5f * h4;
    L.wallH = 3.f + 2.5f * h5 + (urban > 0.2f ? 3.f : 0.f); L.roofH = 1.8f + 1.2f * h6; L.ridgeX = L.seed < 0.5f ? 1 : 0;
  }
  if (m[0] * ROAD_RANGE < std::max(L.hw, L.hd) + 6.f) return false;
  L.ground = groundHeight(L.cx, L.cz, 5) - 1.0f;
  if (L.ground < 1.5f) return false;
  L.present = true;
  return true;
}
