// Air Xpress - hand-designed archipelago "The Solace Islands"
#include "world.h"
#include "scenery.h"

World g_world;

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

void World::build() {
  airports.assign(std::begin(kAirports), std::end(kAirports));
  hm.resize((size_t)HM_N * HM_N * 4);
  for (int j = 0; j < HM_N; j++)
    for (int i = 0; i < HM_N; i++) {
      float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL;
      computeTexel(x, z, &hm[((size_t)j * HM_N + i) * 4]);
    }
  bakeMask();
  buildHMax();
  // Airport structures in runway-local frame (x = across, z = along runway); see Box kinds in world.h
  boxes.clear();
  for (int ai = 0; ai < (int)airports.size(); ai++) {
    const Airport& a = airports[ai];
    Rng r(1000 + ai * 77);
    float side = (ai & 1) ? 1.f : -1.f;
    float off = a.width * 0.5f + (a.size == 2 ? 170.f : 85.f);
    int nh = a.size == 0 ? 1 : (a.size == 1 ? 3 : 5);
    for (int k = 0; k < nh; k++) {
      float along = (k - (nh - 1) * 0.5f) * 58.f - a.length * 0.12f;
      float hw = r.range(16, 24), hd = r.range(14, 20), hh = r.range(7, 11) * (a.size == 0 ? 0.65f : 1.f);
      boxes.push_back({vec3(side * (off + hd), hh, along), vec3(hd, hh, hw), ai, 0});
    }
    if (a.size >= 1) {
      float th = a.size == 2 ? 20.f : 13.f;
      boxes.push_back({vec3(side * (off + 12), th, a.length * 0.08f), vec3(3.2f, th, 3.2f), ai, 1});
      boxes.push_back({vec3(side * (off + 40), 7.f, a.length * 0.18f + 30.f), vec3(26.f, 7.f, a.size == 2 ? 120.f : 40.f), ai, 2});
      boxes.push_back({vec3(side * (off + 30), 4.f, -a.length * 0.12f - 120.f), vec3(4.f, 4.f, 4.f), ai, 4});
      if (a.size == 2) {
        boxes.push_back({vec3(side * (off + 30), 4.f, -a.length * 0.12f - 135.f), vec3(4.f, 4.f, 4.f), ai, 4});
        boxes.push_back({vec3(side * (off + 70), 10.f, -a.length * 0.12f - 60.f), vec3(5.f, 10.f, 5.f), ai, 5});
      }
    } else {
      boxes.push_back({vec3(side * (off + 8), 3.5f, a.length * 0.2f), vec3(5.f, 3.5f, 4.f), ai, 3});
    }
  }
}

int World::findAirport(const char* code) const {
  for (size_t i = 0; i < airports.size(); i++) if (!strcmp(airports[i].code, code)) return (int)i;
  return -1;
}

// Upper bound of the rendered terrain (ground + detail) per cell, so the ray marcher can
// skip whole cells it passes above. Conservative: per-texel max of the base layer over the bilinear footprint, the
// detail fbm sampled densely with 6 octaves plus the bound of all finer octaves (sum of their amplitudes).
void World::buildHMax() {
  const int TPC = HM_N / HMAX_N;  // texels per cell
  const float cs = 2.f * WORLD_HALF / HMAX_N;
  hmax[0].assign((size_t)HMAX_N * HMAX_N, 0.f);
  for (int cj = 0; cj < HMAX_N; cj++)
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

float World::height(float x, float z, int octaves) const {
  float b[4]; sampleBase(x, z, b);
  float g = b[1] < 0.01f ? b[0] : b[0] + b[1] * terrainFbm(x / DETAIL_SCALE, z / DETAIL_SCALE, octaves);
  return g;   // bare ground: trees, rocks and buildings are separate entities (entities.h)
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
