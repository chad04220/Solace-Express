// Somnium Volandi - hand-designed archipelago "The Solace Islands"
#include "world.h"
#include "dev_switches.h"
#include "world_load_progress.h"
#include "procedural_regions.h"
#include "earth_charts.h"
#include "regional_surface.h"
#include <cstdio>
#include <cstring>
#include "scenery.h"
#include <deque>
#include <chrono>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif

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
  // 4.0's seaplane bases (docs/WINGS_OVER_WATER.md), appended: every field above keeps its index. Each a water lane
  // 1000 m by 60 m, surveyed clear - its own length and 200 m more at each end, 50 m more either side - in water at
  // least 1.5 m deep, its approaches under a 1 in 18 slope for 3 km, as sheltered as the coast allows, with a shore
  // within a short jetty of it for the dock (World::build: seaplaneDocks). Floats only.
  {"VBY", "Verde Bay", -25000, 14700, 0, 70, 1000, 60, SURF_WATER, 0,
   "A sheltered bay on Verdana's south-west coast, below Port Verde. A marked water lane, a dock and fuel."},
  {"FJW", "Fjordhaven Water", 18200, -25600, 0, 80, 1000, 60, SURF_WATER, 0,
   "The fjord's calm water above Fjordhaven. Steep sides: expect downdrafts off the slopes."},
  {"ICH", "Ice Harbor", 31100, -24400, 0, 110, 1000, 60, SURF_WATER, 0,
   "A fishing village on Nordholm's coast that no runway reaches. Cold water and an icy dock."},
  {"PBL", "Palm Bay Lagoon", 26600, 22400, 0, 120, 1000, 60, SURF_WATER, 0,
   "Glassy water behind Palm Bay's reef. The gentlest water landing in the islands."},
  {"SLM", "Saltmarsh", -16000, 18200, 0, 60, 1000, 60, SURF_WATER, 0,
   "A marsh village on Verdana's south coast, reached by water. Watch for the birds."},
  {"WVL", "Westvale", -27900, -3600, 0, 0, 1000, 60, SURF_WATER, 0,
   "A west-coast village's harbour, open to the Atlantic swell when the wind is in the west."},
  {"TBY", "Turtle Bay", 37400, 31900, 0, 100, 1000, 60, SURF_WATER, 0,
   "Far Isle's resort lagoon. The guests arrive by floatplane."},
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
    if (a.water() || beyondRunway(x - a.x, z - a.z, a.length)) continue;   // (a seaplane base shapes no ground)
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

void World::sampleAuthoredBase(float x, float z, float out[4]) {
  uint32_t bx = 0, bz = 0; std::memcpy(&bx, &x, sizeof(x)); std::memcpy(&bz, &z, sizeof(z));
  if ((bx & 0x7f800000u) == 0x7f800000u || (bz & 0x7f800000u) == 0x7f800000u ||
      std::abs(x) > WORLD_HALF || std::abs(z) > WORLD_HALF) {
    out[0] = -SEA_DEPTH; out[1] = out[2] = out[3] = 0.f; return;
  }
  sceneryInit(); // immutable legacy road table, protected by call_once
  computeTexel(x, z, out);
}

// What an airport allows the ground at a point to be: grounds 0..1, how much it lies on a field's flattened grounds
// (left alone), and cap, the height its approach funnels hold the terrain under (+inf outside them) - computeTexel's
// own rules, for anything that reshapes the ground afterwards
static void airportLimits(const Airport* data, size_t count, float x, float z, float& grounds, float& cap) {
  grounds = 0.f; cap = 1e9f;
  for (size_t i = 0; i < count; ++i) {
    const Airport& a = data[i];
    if (a.water() || beyondRunway(x - a.x, z - a.z, a.length)) continue;
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

// An airport's other runways (World::extraRunways: the real Earth's) as a runway is read above, at their airport's
// height: the strip of level ground along each that World::buildEarthTerrain lays, and their approach funnels.
// funnelCeiling as airportFunnelCeilingFor's; influence the stronger of the two, as airportInfluenceFor's
static void extraRunwayLimits(const World& world, float x, float z, float& grounds, float& cap, float& funnelCeiling, float& influence) {
  for (const ExtraRunway& r : world.extraRunways) {
    if (beyondRunway(x - r.x, z - r.z, r.length, std::max(2410.f, r.width * 0.5f + 490.f))) continue;
    const float elev = world.airports[r.airport].elev;
    const float hd = r.heading * DEG, dx = x - r.x, dz = z - r.z;
    const float u = fabsf(dx * sinf(hd) - dz * cosf(hd)), v = fabsf(dx * cosf(hd) + dz * sinf(hd));
    const float d = u - r.length * 0.5f;
    if (d > 0 && d < 7000) {
      const float halfw = 250.f + 0.18f * d;
      const float wc = (1.f - smoothstepf(halfw, halfw + 900.f, v)) * (1.f - smoothstepf(5000.f, 7000.f, d));
      const float c = elev + 10.f + (elev > 600 ? 0.045f : 0.032f) * d;
      if (wc > 0.001f) cap = std::min(cap, c);
      if (wc >= 0.9f) funnelCeiling = std::min(funnelCeiling, c);
      influence = std::max(influence, wc);
    }
    const float du = std::max(0.f, u - (r.length * 0.5f + 150.f)), dv = std::max(0.f, v - (r.width * 0.5f + 90.f));
    const float g = 1.f - smoothstepf(0.f, 400.f, sqrtf(du * du + dv * dv));
    grounds = std::max(grounds, g); influence = std::max(influence, g);
  }
}

void airportGroundsCap(float x, float z, float& grounds, float& cap) { airportLimits(kAirports, std::size(kAirports), x, z, grounds, cap); }
void airportGroundsCap(const World& world, float x, float z, float& grounds, float& cap) {
  airportLimits(world.airports.data(), world.airports.size(), x, z, grounds, cap);
  float ceiling = 1e9f, influence = 0.f;
  extraRunwayLimits(world, x, z, grounds, cap, ceiling, influence);
}

// The height a road may be built up to under an approach funnel: its cap where the funnel holds the ground down in full
// (computeTexel's own weight 0.9 and over), unlimited elsewhere - on a funnel's fringe the ground keeps most of its own
// height, and a road with it
static float airportFunnelCeilingFor(const Airport* data, size_t count, float x, float z) {
  float ceiling = 1e9f;
  for (size_t i = 0; i < count; ++i) {
    const Airport& a = data[i];
    if (a.water() || beyondRunway(x - a.x, z - a.z, a.length)) continue;
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
static float airportInfluenceFor(const Airport* data, size_t count, float x, float z) {
  float best = 0;
  for (size_t i = 0; i < count; ++i) {
    const Airport& a = data[i];
    if (a.water() || beyondRunway(x - a.x, z - a.z, a.length)) continue;
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

float airportFunnelCeiling(float x, float z) { return airportFunnelCeilingFor(kAirports, std::size(kAirports), x, z); }
float airportFunnelCeiling(const World& world, float x, float z) {
  float ceiling = airportFunnelCeilingFor(world.airports.data(), world.airports.size(), x, z), grounds = 0.f, cap = 1e9f, influence = 0.f;
  extraRunwayLimits(world, x, z, grounds, cap, ceiling, influence);
  return ceiling;
}
float airportInfluence(float x, float z) { return airportInfluenceFor(kAirports, std::size(kAirports), x, z); }
int solaceAirportCount() { return (int)std::size(kAirports); }
const Airport& solaceAirport(int i) { return kAirports[std::clamp(i, 0, (int)std::size(kAirports) - 1)]; }
float solaceDistanceKm(int a, int b) {
  const Airport &A = solaceAirport(a), &B = solaceAirport(b);
  return sqrtf((A.x - B.x) * (A.x - B.x) + (A.z - B.z) * (A.z - B.z)) / 1000.f;
}
float airportInfluence(const World& world, float x, float z) {
  float influence = airportInfluenceFor(world.airports.data(), world.airports.size(), x, z), grounds = 0.f, cap = 1e9f, ceiling = 1e9f;
  extraRunwayLimits(world, x, z, grounds, cap, ceiling, influence);
  return influence;
}
float buildableHeight(const World& world, float x, float z) {
  if (!world.earthChart || world.earthChart->cover.empty()) return 2.5f;
  const int i = std::clamp((int)((x + WORLD_HALF) / HM_TEXEL), 0, HM_N - 1), j = std::clamp((int)((z + WORLD_HALF) / HM_TEXEL), 0, HM_N - 1);
  return world.earthChart->cover[(size_t)j * HM_N + i] == earth::COVER_BUILT ? 1.f : 2.5f;
}
float airportExclusion(const World& world, float x, float z) {
  if (!world.earthChart) return airportInfluence(world, x, z);
  float grounds, cap; airportGroundsCap(world, x, z, grounds, cap);
  return grounds;
}

void World::build(const std::string& cachePath, const std::string& stamp, bool save) {
  generationCheckpoint();
  procedural = false; repeating = false; regionId = "solace"; regionRecipe.reset();   // (one place on the Earth: world.h)
  earthChart.reset(); extraRunways.clear(); anchorGrid.clear(); buildingStyle = 0; detailScale = 1.f; noiseCos = 1.f; noiseSin = noiseX = noiseZ = 0.f;
  airports.assign(std::begin(kAirports), std::end(kAirports));
  towns.assign(kTowns, kTowns + kNumTowns);
  for (auto& a : airports) a.hospital = !strcmp(a.code, "CAP") || !strcmp(a.code, "NPT") || !strcmp(a.code, "PVI");
  buildConfigured(cachePath, stamp, save);
}

void World::buildProcedural(const procedural::Descriptor& region, const std::string& cachePath,
                            const std::string& stamp, bool save) {
  generationCheckpoint();
  if (region.authored) { build(cachePath, stamp, save); return; }
  if (earthCharts::isEarthId(region.id)) { buildEarth(region, cachePath, stamp, save); return; }
  earthChart.reset(); extraRunways.clear(); anchorGrid.clear(); buildingStyle = 0; detailScale = 1.f; noiseCos = 1.f; noiseSin = noiseX = noiseZ = 0.f;
  regionRecipe = std::make_shared<const procedural::Recipe>(region);
  procedural = true; repeating = false; regionId = region.id;
  airports = regionRecipe->airports; towns = regionRecipe->towns;
  // Region identity and the generator recipe are independent of filename/build stamp.
  // A fixed-size hexadecimal hash stays inside the legacy cache's 63-byte header.
  uint64_t h = 1469598103934665603ull;
  // Terrain content evolves without renumbering the catalog, moving airports,
  // or invalidating authored Solace's unchanged cache/save identity.
  const std::string key = std::to_string(region.generatorVersion) + ":" + std::to_string(region.worldSeed) + ":" + region.id +
    ":recipe-" + std::to_string(procedural::kRecipeRevision) + ":" + stamp;
  for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
  char cacheStamp[64]; snprintf(cacheStamp, sizeof(cacheStamp), "procedural-%016llx", (unsigned long long)h);
  buildConfigured(cachePath, cacheStamp, save);
}

void World::buildConfigured(const std::string& cachePath, const std::string& stamp, bool save) {
  generationPhase(GenerationPhase::Configuring);
  sceneryState.reset();
  auto setStage = [this](int stage) { if (this == &g_world) g_worldStage = stage; };
  // the loading screen's progress (world_load_progress.h) follows the active world alone: a region generated in the
  // background for Earth free flight must not move it
  const bool reports = this == &g_world;
  // (EARTH_TIMING=1: each stage's time on stderr - the real Earth's charts' budget)
  static const bool timing = dev::env("EARTH_TIMING") != nullptr;
  auto stageClock = std::chrono::steady_clock::now();
  auto loadStage = [reports, &stageClock](float done, const char* next) {
    if (timing) { const auto now = std::chrono::steady_clock::now(); fprintf(stderr, "  %6.2f s  then %s\n", std::chrono::duration<double>(now - stageClock).count(), next); stageClock = now; }
    if (reports) worldLoadStage(done, next);
  };
  auto loadProgress = [reports](float done) { if (reports) worldLoadProgress(done); };
  if (reports) { g_worldLoadProgress = 0.f; g_worldLoadTask = "Reading island cache"; }
  roads = RoadNetwork(); roadGrid = RoadGrid(); bridges.clear(); settlements.clear(); farms.clear(); reservoir = Reservoir(); lake = LakeMask(); lakes.clear();   // (the natural ground first: the roads are routed over it)
  setStage(cachePath.empty() ? 2 : 1);
  fromCache = !cachePath.empty() && loadCache(cachePath, stamp);
  if (fromCache) {
    loadStage(.60f,"Preparing cached terrain and lakes"); sceneryInit(); buildLakeMask();
    surveySeaplaneBases(); seaplaneDocks();
    generationPhase(GenerationPhase::Settlements);
    loadStage(.68f,"Preparing cached settlements"); settlements = buildSettlementFields(*this); farms = farmsteadsOf(roads);
    generationPhase(GenerationPhase::Scenery);
    sceneryBakeCommunityLots(*this);
    loadStage(.80f,"Preparing cached bridges"); boxes.clear(); bridges = buildBridges(*this);
    loadStage(.88f,"Preparing road furniture"); sceneryBakeRoadFurniture(*this);
    loadStage(.94f,"Preparing landmarks"); sceneryBakeLandmarks(*this);
    generationPhase(GenerationPhase::Complete); loadStage(1.f,"Islands ready"); setStage(3); return;
  }
  setStage(2);
  generationPhase(GenerationPhase::Terrain);
  loadStage(.02f,"Sampling island terrain");
  sceneryInit();
  if (earthChart) {   // (the real Earth: the data, no grid smoothing - world_earth.cpp)
    buildEarthTerrain();
  } else {
    std::atomic<int> sampledRows{0};
    hm.resize((size_t)HM_N * HM_N * 4);
    parallelFor(HM_N, [&](int j) {   // rows are independent; spread over every core
      for (int i = 0; i < HM_N; i++) {
        float x = -WORLD_HALF + (i + 0.5f) * HM_TEXEL, z = -WORLD_HALF + (j + 0.5f) * HM_TEXEL;
        float* texel = &hm[((size_t)j * HM_N + i) * 4];
        if (procedural) regionRecipe->sample(x, z, texel);
        else computeTexel(x, z, texel);
      }
      loadProgress(.02f+.12f*float(sampledRows.fetch_add(1)+1)/HM_N);
    });
    // Smooth the ground heights with a separable 1-2-1 filter (twice): bilinear interpolation of the raw samples leaves
    // visible creases along the grid on big smooth landforms (volcano flanks, ridges, sea cliffs) - a faceted, low-poly
    // look. Flat areas (airport grounds, the sea floor plateau) are unchanged by it.
    {
      loadStage(.14f,"Smoothing terrain / four row passes");
      std::atomic<int> smoothRows{0};
      std::vector<float> tmp((size_t)HM_N * HM_N);
      for (int pass = 0; pass < 2; pass++) {
        parallelFor(HM_N, [&](int j) {
          for (int i = 0; i < HM_N; i++) {
            int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, HM_N - 1);
            const float* r = &hm[(size_t)j * HM_N * 4];
            tmp[(size_t)j * HM_N + i] = (r[i0 * 4] + 2.f * r[i * 4] + r[i1 * 4]) * 0.25f;
          }
          loadProgress(.14f+.12f*float(smoothRows.fetch_add(1)+1)/(4*HM_N));
        });
        parallelFor(HM_N, [&](int j) {
          int j0 = std::max(j - 1, 0), j1 = std::min(j + 1, HM_N - 1);
          for (int i = 0; i < HM_N; i++)
            hm[((size_t)j * HM_N + i) * 4] = (tmp[(size_t)j0 * HM_N + i] + 2.f * tmp[(size_t)j * HM_N + i] + tmp[(size_t)j1 * HM_N + i]) * 0.25f;
          loadProgress(.14f+.12f*float(smoothRows.fetch_add(1)+1)/(4*HM_N));
        });
      }
    }
  }
  loadStage(.26f,"Resolving inland drainage");
  for (int pass = 0; !procedural && pass < 4 && fillInlandPits() > 0; pass++) {}   // (until none is left: see fillInlandPits)
  // A real summit bowl for Mount Kaleo. The original procedural detail overwhelms
  // its shallow analytic depression; carve only this bounded, airport-free summit.
  // All terrain outside the 520 m texel-centre disk remains byte-identical.
  loadStage(.30f,"Shaping the volcano summit");
  const int vx0 = std::max(0, (int)((25000.f - 520.f + WORLD_HALF) / HM_TEXEL));
  const int vx1 = std::min(HM_N - 1, (int)((25000.f + 520.f + WORLD_HALF) / HM_TEXEL));
  const int vz0 = std::max(0, (int)((-9000.f - 520.f + WORLD_HALF) / HM_TEXEL));
  const int vz1 = std::min(HM_N - 1, (int)((-9000.f + 520.f + WORLD_HALF) / HM_TEXEL));
  for (int j = vz0; !procedural && j <= vz1; ++j) for (int i = vx0; i <= vx1; ++i) {
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
  surveySeaplaneBases(); seaplaneDocks();   // (a generated region's seaplane bases, and each base's dock on its shore: the ground is settled from here on)
  generationPhase(GenerationPhase::Roads);
  loadStage(.34f,"Routing roads over terrain");
  roads = earthChart ? buildEarthRoads() : buildRoadNetwork(*this);
  // the settlements grown over the ground and along those roads, and their streets added to the network
  generationPhase(GenerationPhase::Settlements);
  loadStage(.46f,"Laying out settlements");
  settlements = buildSettlementFields(*this);
  loadStage(.52f,"Connecting settlement streets");
  addSettlementStreets(*this, settlements, roads);
  if (!procedural) addRacetrack(*this, settlements, roads);    // (the racetrack's circuit, its pit lane and its road in)
  loadStage(.58f,"Placing farm tracks");
  addFarmsteads(*this, settlements, roads);   // (and the farms out in the fields, each at the end of its track)
  farms = farmsteadsOf(roads);
  generationPhase(GenerationPhase::RoadGrid);
  loadStage(.64f,"Indexing road surfaces");
  buildRoadGrid(*this, roads, roadGrid);
  // the Fjordhaven reservoir: its dam's gorge, and its lake's water (World::height has it from here on)
  loadStage(.70f,"Preparing reservoirs and lake masks");
  if (!procedural) findReservoir(*this, reservoir);
  buildLakeMask();
  loadStage(.75f,"Baking terrain material masks");
  bakeMask();
  generationPhase(GenerationPhase::HeightBounds);
  loadStage(.80f,"Building height bounds");
  buildHMax();
  generationPhase(GenerationPhase::Envelope);
  loadStage(.84f,"Building terrain envelopes");
  buildEnvelope();
  // Airport buildings are raster scenery entities now (airport_scenery.cpp); the old analytic box list stays empty
  boxes.clear();
  generationPhase(GenerationPhase::Scenery);
  loadStage(.88f,"Assembling bridges");
  bridges = buildBridges(*this);
  loadStage(.92f,"Placing roadside furniture");
  sceneryBakeRoadFurniture(*this);
  loadStage(.95f,"Placing landmarks");
  sceneryBakeLandmarks(*this);
  loadStage(.98f,"Writing reusable island cache");
  if (!cachePath.empty() && save) saveCache(cachePath, stamp);
  generationPhase(GenerationPhase::Complete);
  loadStage(1.f,"Islands ready");
  setStage(3);
}

// The generated world on disk: its height and mask textures, height bounds and terrain envelope, stamped with the
// build that made them (any other build generates again)
namespace {
const uint32_t kWorldMagic = 0x574c4439u;   // "WLD9": the road network, its grid (each texel's paving entries first; 26-bit firsts) and the forest noise beside it; the reservoir; a real-Earth chart's lakes and its land cover (buildableHeight reads it)
bool replaceCacheFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
  return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(from.c_str(), to.c_str()) == 0;
#endif
}
template <class T> void putVec(FILE* f, const std::vector<T>& v) {
  generationCheckpoint();
  const uint64_t n = v.size(); fwrite(&n, 8, 1, f);
  constexpr size_t chunk = std::max<size_t>(1, (1u << 20) / sizeof(T));
  for (size_t at = 0; at < v.size(); at += chunk) {
    generationCheckpoint();
    if (fwrite(v.data() + at, sizeof(T), std::min(chunk, v.size() - at), f) != std::min(chunk, v.size() - at)) return;
  }
}
template <class T> bool getVecData(FILE* f, std::vector<T>& v, uint64_t n) {
  generationCheckpoint();
  v.resize(n);
  constexpr size_t chunk = std::max<size_t>(1, (1u << 20) / sizeof(T));
  for (size_t at = 0; at < v.size(); at += chunk) {
    generationCheckpoint();
    const size_t count = std::min(chunk, v.size() - at);
    if (fread(v.data() + at, sizeof(T), count, f) != count) return false;
  }
  return true;
}
template <class T> bool getVec(FILE* f, std::vector<T>& v, uint64_t expect) {
  generationCheckpoint();
  uint64_t n = 0; if (fread(&n, 8, 1, f) != 1 || n != expect) return false;
  return getVecData(f, v, n);
}
template <class T> bool getVecUpTo(FILE* f, std::vector<T>& v, uint64_t most) {   // (a list whose length varies)
  generationCheckpoint();
  uint64_t n = 0; if (fread(&n, 8, 1, f) != 1 || n > most) return false;
  return getVecData(f, v, n);
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
  generationPhase(GenerationPhase::CacheRead);
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), &fclose);
  FILE* f = file.get(); if (!f) return false;
  // into temporaries, every array at its exact size (the terrain code indexes them on that assumption), published only
  // when the whole file checks out: a short, malformed or other build's cache is rejected and the world generated
  World w;
  uint32_t magic = 0; char st[64] = {};
  bool ok = fread(&magic, 4, 1, f) == 1 && magic == kWorldMagic && fread(st, 1, 64, f) == 64 && stamp == std::string(st, strnlen(st, 64));
  ok = ok && getVec(f, w.hm, (uint64_t)HM_N * HM_N * 4) && getVec(f, w.mask, (uint64_t)MASK_N * MASK_N * 4);
  ok = ok && getRoads(f, w.roads) && getVecUpTo(f, w.roadGrid.segs, 1u << 23) && getVec(f, w.roadGrid.head, (uint64_t)RoadGrid::N * RoadGrid::N) && getVec(f, w.roadGrid.forest, (uint64_t)RoadGrid::N * RoadGrid::N) &&
       getVecUpTo(f, w.roadGrid.list, (uint64_t)RoadGrid::kFirstMask + 64) && getVec(f, w.roadGrid.nearHead, (uint64_t)RoadGrid::NC * RoadGrid::NC) && getVecUpTo(f, w.roadGrid.nearList, 1u << 24);
  // (every entry names a segment, every texel's entries lie in the list: the terrain indexes them unchecked)
  for (size_t i = 0; ok && i < w.roadGrid.list.size(); i++) ok = w.roadGrid.list[i] < w.roadGrid.segs.size();
  for (size_t i = 0; ok && i < w.roadGrid.nearList.size(); i++) ok = w.roadGrid.nearList[i] < w.roadGrid.segs.size();
  for (size_t i = 0; ok && i < w.roadGrid.head.size(); i++) ok = RoadGrid::first(w.roadGrid.head[i]) + RoadGrid::count(w.roadGrid.head[i]) <= w.roadGrid.list.size();
  for (size_t i = 0; ok && i < w.roadGrid.nearHead.size(); i++) ok = (w.roadGrid.nearHead[i] >> 8) + (w.roadGrid.nearHead[i] & 255u) <= w.roadGrid.nearList.size();
  for (size_t i = 0; ok && i < w.roadGrid.segs.size(); i++) ok = w.roadGrid.segs[i].cls < RC_COUNT;
  for (int L = 0; ok && L < HMAX_LEVELS; L++) { const uint64_t n = (uint64_t)(HMAX_N >> L); ok = getVec(f, w.hmax[L], n * n); }
  ok = ok && getVec(f, w.tpV0, (uint64_t)(HM_N + 1) * (HM_N + 1));
  for (int L = 0; ok && L < TP_LEVELS; L++) { const uint64_t n = (uint64_t)(HM_N >> L); ok = getVec(f, w.tpM[L], n * n); }
  std::vector<float> res;
  ok = ok && getVec(f, res, RESERVOIR_FLOATS);
  ok = ok && getVecUpTo(f, w.lakes, (uint64_t)HM_N * HM_N) && (w.lakes.empty() || w.lakes.size() == (size_t)HM_N * HM_N);
  std::vector<uint8_t> cover;
  ok = ok && getVecUpTo(f, cover, (uint64_t)HM_N * HM_N) && (cover.empty() || cover.size() == (size_t)HM_N * HM_N) && cover.empty() == !earthChart;
  if (ok) { static_assert(sizeof(Reservoir) == RESERVOIR_FLOATS * sizeof(float), "the reservoir's floats"); memcpy(&w.reservoir, res.data(), sizeof(Reservoir)); }
  ok = ok && fgetc(f) == EOF;   // (nothing after the last array: not some other format's file)
  file.reset();
  generationCheckpoint();
  // (and every number in it a number: a NaN height in a well-formed file loaded, and the terrain and the flight model
  // ran on it - the review of v3.44.0, WLD-4)
  auto finite = [](const std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); ++i) {
      if ((i & 65535u) == 0) generationCheckpoint();
      if (!std::isfinite(v[i])) return false;
    }
    return true;
  };
  ok = ok && finite(w.hm) && finite(w.tpV0) && finite(res);
  for (int L = 0; ok && L < HMAX_LEVELS; L++) ok = finite(w.hmax[L]);
  for (int L = 0; ok && L < TP_LEVELS; L++) ok = finite(w.tpM[L]);
  for (size_t i = 0; ok && i < w.roadGrid.segs.size(); i++) { const RoadSegment& r = w.roadGrid.segs[i]; ok = std::isfinite(r.ax + r.az + r.bx + r.bz + r.ah + r.bh + r.along); }
  if (!ok) return false;
  generationCheckpoint();
  hm.swap(w.hm); mask.swap(w.mask); tpV0.swap(w.tpV0);
  std::swap(roads, w.roads); std::swap(roadGrid, w.roadGrid); reservoir = w.reservoir; lakes.swap(w.lakes);
  if (earthChart) earthChart->cover.swap(cover);
  for (int L = 0; L < HMAX_LEVELS; L++) hmax[L].swap(w.hmax[L]);
  for (int L = 0; L < TP_LEVELS; L++) tpM[L].swap(w.tpM[L]);
  return true;
}
void World::saveCache(const std::string& path, const std::string& stamp) const {
  generationPhase(GenerationPhase::CacheWrite);
  const std::string tmp = path + ".tmp";
  struct TempFile {
    const std::string& path;
    ~TempFile() { remove(path.c_str()); }
  } cleanup{tmp};
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(tmp.c_str(), "wb"), &fclose);
  FILE* f = file.get(); if (!f) return;
  char st[64] = {}; strncpy(st, stamp.c_str(), 63);
  fwrite(&kWorldMagic, 4, 1, f); fwrite(st, 1, 64, f);
  putVec(f, hm); putVec(f, mask);
  putRoads(f, roads); putVec(f, roadGrid.segs); putVec(f, roadGrid.head); putVec(f, roadGrid.forest); putVec(f, roadGrid.list); putVec(f, roadGrid.nearHead); putVec(f, roadGrid.nearList);
  for (int L = 0; L < HMAX_LEVELS; L++) putVec(f, hmax[L]);
  putVec(f, tpV0);
  for (int L = 0; L < TP_LEVELS; L++) putVec(f, tpM[L]);
  { std::vector<float> res(RESERVOIR_FLOATS); memcpy(res.data(), &reservoir, sizeof(Reservoir)); putVec(f, res); }
  putVec(f, lakes);
  putVec(f, earthChart ? earthChart->cover : std::vector<uint8_t>());
  const bool ok = !ferror(f);
  const int closed = fclose(file.release());
  generationCheckpoint(); // a canceled candidate never publishes its cache
  if (!ok || closed != 0) return;
  // Replace in one filesystem operation. Never remove the previous cache first:
  // a failed publication must leave that completed file available for reuse.
  if (!replaceCacheFile(tmp, path)) return;
}

int World::findAirport(const char* code) const {
  for (size_t i = 0; i < airports.size(); i++) if (!strcmp(airports[i].code, code)) return (int)i;
  return -1;
}

// Every cell of an n x n grid over the map (cell size cs) the lake's square reaches, with its level
template <class F> static void forEachLakeCell(const World& w, float cs, int n, F f) {
  auto cell = [&](float v) { return std::clamp((int)floorf((v + WORLD_HALF) / cs), 0, n - 1); };
  if (w.lake.n) {
    const float s = w.lake.n * w.lake.cell;
    for (int j = cell(w.lake.z0 - w.lake.cell); j <= cell(w.lake.z0 + s + w.lake.cell); j++)
      for (int i = cell(w.lake.x0 - w.lake.cell); i <= cell(w.lake.x0 + s + w.lake.cell); i++) f(i, j, w.reservoir.level);
  }
  // (a real-Earth chart's: each texel with a lake's code, its level over the cells the texel overlaps)
  if (w.lakes.size() == (size_t)HM_N * HM_N)
    for (int tj = 0; tj < HM_N; tj++) for (int ti = 0; ti < HM_N; ti++) {
      const uint16_t c = w.lakes[(size_t)tj * HM_N + ti];
      if (!c) continue;
      const float x0 = -WORLD_HALF + ti * HM_TEXEL, z0 = -WORLD_HALF + tj * HM_TEXEL;
      for (int j = cell(z0); j <= cell(z0 + HM_TEXEL); j++) for (int i = cell(x0); i <= cell(x0 + HM_TEXEL); i++) f(i, j, World::lakeLevelOf(c));
    }
}

// Every cell of an n x n grid over the map (cell size cs) that a graded road segment's platform or banks reach, with the
// segment's highest end: the bounds of the terrain lift to it
template <class F> static void forEachRoadCell(const RoadGrid& grid, float cs, int n, float slack, F f) {
  for (const RoadSegment& s : grid.segs) {
    generationCheckpoint();
    if (s.flags & (RS_BRIDGE | RS_NOGRADE)) continue;
    const float R = roadSpec(s.cls).halfPlatform + roadBankMax(s.cls) + slack, top = std::max(s.ah, s.bh);
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
            { const vec2 q = noisePoint(x0 + cs * si / S, z0 + cs * sj / S); fmax = std::max(fmax, terrainFbm(q.x, q.y, 6)); }
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
  forEachLakeCell(*this, 2.f * WORLD_HALF / HMAX_N, HMAX_N, [&](int i, int j, float top) {   // (and the lake: its level over its square)
    float& m = hmax[0][(size_t)j * HMAX_N + i]; m = std::max(m, top + 4.f); });
  for (int L = 1; L < HMAX_LEVELS; L++) {
    generationCheckpoint();
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
  auto fbmAt = [&](float x, float z) { const vec2 q = noisePoint(x, z); return terrainFbm(q.x, q.y, 6); };
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
  forEachLakeCell(*this, HM_TEXEL, N, [&](int i, int j, float top) {
    for (int dj = 0; dj <= 1; dj++) for (int di = 0; di <= 1; di++) {
      float& v = tpV0[(size_t)(j + dj) * NV + i + di]; v = std::max(v, top + 0.5f); } });
  tpM[0].assign((size_t)N * N, 0.f);
  parallelFor(N, [&](int j) {
    for (int i = 0; i < N; i++)
      tpM[0][(size_t)j * N + i] = std::max(std::max(tpV0[(size_t)j * NV + i], tpV0[(size_t)j * NV + i + 1]),
                                           std::max(tpV0[(size_t)(j + 1) * NV + i], tpV0[(size_t)(j + 1) * NV + i + 1]));
  });
  for (int L = 1; L < TP_LEVELS; L++) {
    generationCheckpoint();
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
  size_t floodChecks = 0;
  while (!q.empty()) {
    if ((floodChecks++ & 1023u) == 0) generationCheckpoint();
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
    if ((w & 255u) == 0) generationCheckpoint();
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
      float grounds, cap; airportLimits(airports.data(), airports.size(), cx, cz, grounds, cap);
      float* t = &hm[k * 4];
      const float L = std::min(lift * (1.f - smoothstepf(0.f, 0.05f, grounds)), std::max(0.f, cap - t[0] - 1.5f * t[1]));
      t[0] += L;
      if (L >= lift - 0.01f || need0[k] <= 0.f) continue;
      float fmin = 1e9f;   // (the detail's lowest over the texel's reach, an eighth of a texel apart, less a margin)
      for (int sj = -8; sj <= 8; sj++) for (int si = -8; si <= 8; si++)
        { const vec2 q = noisePoint(cx + si * HM_TEXEL / 8.f, cz + sj * HM_TEXEL / 8.f); fmin = std::min(fmin, terrainFbm(q.x, q.y, 11)); }
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
  if (!repeating && std::max(fabsf(x), fabsf(z)) >= WORLD_HALF + 3000.f) {
    out[0] = -SEA_DEPTH; out[1] = out[2] = out[3] = 0.f; return;
  }
  x = wrap(x); z = wrap(z);
  float fx = (x + WORLD_HALF) / HM_TEXEL - 0.5f, fz = (z + WORLD_HALF) / HM_TEXEL - 0.5f;
  float flx = floorf(fx), flz = floorf(fz);
  int i0 = (int)flx, j0 = (int)flz;
  float tx = fx - flx, tz = fz - flz;
  auto at = [&](int i, int j, int c) { i = std::clamp(i, 0, HM_N - 1); j = std::clamp(j, 0, HM_N - 1); return hm[((size_t)j * HM_N + i) * 4 + c]; };
  for (int c = 0; c < 4; c++) {
    float a = at(i0, j0, c), b = at(i0 + 1, j0, c), cc = at(i0, j0 + 1, c), d = at(i0 + 1, j0 + 1, c);
    out[c] = (a * (1 - tx) + b * tx) * (1 - tz) + (cc * (1 - tx) + d * tx) * tz;
  }
  // past the islands' square: the open sea, settling to one depth within 3 km (common.glsl baseAt, the same)
  const float o = std::max(fabsf(x), fabsf(z)) - WORLD_HALF;
  if (o > 0.f) { const float k = smoothstepf(0.f, 3000.f, o); out[0] += (-SEA_DEPTH - out[0]) * k; out[1] *= 1.f - k; }
}

float World::naturalHeight(float x, float z, int octaves) const {
  x = wrap(x); z = wrap(z);
  float b[4]; sampleBase(x, z, b);
  if (b[1] < 0.01f) return b[0];
  const vec2 q = noisePoint(x, z);
  return b[0] + b[1] * detailScale * terrainFbm(q.x, q.y, octaves);
}

float World::height(float x, float z, int octaves) const {
  if (!repeating && std::max(fabsf(x), fabsf(z)) >= WORLD_HALF + 3000.f) {
    // Only the explicitly opted-in published Free Flight World can consult
    // drawable foreign proxies. Candidate generation and career stay native.
    if (regionalSurface::appliesTo(*this)) {
      const auto surface = regionalSurface::queryCurrent(x, z);
      if (surface.covered) return surface.height;
    }
    return -SEA_DEPTH;
  }
  // bare ground: trees, rocks and buildings are separate entities (entities.h); the roads are built into it
  x = wrap(x); z = wrap(z);
  return lakeGround(x, z, roadGrade(roadGrid, x, z, naturalHeight(x, z, octaves)));
}

void World::buildLakeMask() {
  generationCheckpoint();
  lake = LakeMask();
  if (reservoir.on > 0.f && !reservoirMask(*this, reservoir, lake)) { reservoir = Reservoir(); lake = LakeMask(); }
}
float World::lakeMaskAt(float x, float z) const {   // (exactly roads.glsl lakeMaskAt)
  if (!lake.n) return 0.f;
  const float fx = (x - lake.x0) / lake.cell - 0.5f, fz = (z - lake.z0) / lake.cell - 0.5f;
  if (fx < -1.f || fz < -1.f || fx > (float)lake.n || fz > (float)lake.n) return 0.f;
  const int i = (int)floorf(fx), j = (int)floorf(fz); const float tx = fx - i, tz = fz - j;
  auto m = [&](int a, int b) { return a < 0 || b < 0 || a >= lake.n || b >= lake.n ? 0.f : lake.m[(size_t)b * lake.n + a]; };
  return (m(i, j) * (1.f - tx) + m(i + 1, j) * tx) * (1.f - tz) + (m(i, j + 1) * (1.f - tx) + m(i + 1, j + 1) * tx) * tz;
}
uint16_t World::lakeAt(float x, float z) const {   // (exactly roads.glsl earthLakeCode)
  if (lakes.empty()) return 0;
  const int i = std::clamp((int)floorf((x + WORLD_HALF) / HM_TEXEL), 0, HM_N - 1), j = std::clamp((int)floorf((z + WORLD_HALF) / HM_TEXEL), 0, HM_N - 1);
  return lakes[(size_t)j * HM_N + i];
}
float World::lakeGround(float x, float z, float g) const {
  if (lake.n && lakeMaskAt(x, z) > 0.5f) g = std::max(g, reservoir.level);
  if (const uint16_t c = lakeAt(x, z)) g = std::max(g, lakeLevelOf(c));
  return g;
}
bool World::underLake(float x, float z) const {
  if (lake.n && lakeMaskAt(x, z) > 0.5f && naturalHeight(x, z) < reservoir.level + 0.3f) return true;
  const uint16_t c = lakeAt(x, z);
  return c && naturalHeight(x, z) < lakeLevelOf(c) + 0.3f;
}

vec3 World::normal(float x, float z) const {
  if (!repeating && std::max(fabsf(x), fabsf(z)) >= WORLD_HALF + 3000.f && regionalSurface::appliesTo(*this)) {
    const auto surface = regionalSurface::queryCurrent(x, z);
    if (surface.covered) return surface.normal;
    return vec3(0, 1, 0);
  }
  const float e = 2.0f;
  float hx = height(x + e, z) - height(x - e, z), hz = height(x, z + e) - height(x, z - e);
  return normalize(vec3(-hx, 2 * e, -hz));
}

vec2 World::anchorPoint(float x, float z) const {
  if (anchorGrid.empty()) return vec2(x, z);
  const double cell = 2.0 * WORLD_HALF / kAnchorN;
  const double u = std::clamp((x + WORLD_HALF) / cell, 0.0, (double)kAnchorN - 1e-9), v = std::clamp((z + WORLD_HALF) / cell, 0.0, (double)kAnchorN - 1e-9);
  const int i = (int)u, j = (int)v;
  const double fu = u - i, fv = v - j;
  auto at = [&](int ii, int jj, int c) { return anchorGrid[((size_t)jj * (kAnchorN + 1) + ii) * 2 + c]; };
  auto lerp2 = [&](int c) { return (at(i, j, c) * (1 - fu) + at(i + 1, j, c) * fu) * (1 - fv) + (at(i, j + 1, c) * (1 - fu) + at(i + 1, j + 1, c) * fu) * fv; };
  return vec2((float)lerp2(0), (float)lerp2(1));
}

int World::onRunway(float x, float z, float margin, int* surface) const {
  for (size_t i = 0; i < airports.size(); i++) {
    const Airport& a = airports[i];
    float hd = a.heading * DEG, dx = x - a.x, dz = z - a.z;
    float u = dx * sinf(hd) - dz * cosf(hd), v = dx * cosf(hd) + dz * sinf(hd);
    if (fabsf(u) <= a.length * 0.5f + margin && fabsf(v) <= a.width * 0.5f + margin) { if (surface) *surface = a.surface; return (int)i; }
  }
  for (const ExtraRunway& r : extraRunways) {
    const float hd = r.heading * DEG, dx = x - r.x, dz = z - r.z;
    const float u = dx * sinf(hd) - dz * cosf(hd), v = dx * cosf(hd) + dz * sinf(hd);
    if (fabsf(u) <= r.length * 0.5f + margin && fabsf(v) <= r.width * 0.5f + margin) { if (surface) *surface = r.surface; return r.airport; }
  }
  return -1;
}

// A seaplane base's dock: the nearest gentle shore (0.3 to 3 m above the sea) beside its lane - clear of the lane, its
// ends' 200 m and 90 m to either side of it - from which a jetty reaches open water within 10 m, a metre deep by 20 m
// out, and 2 m deep 30 to 70 m out across 24 m (room to moor alongside its head and turn away). The jetty runs out
// from there; an aircraft moors 25 m out. A base with no such shore within 900 m has none (dockLen 0)
void World::seaplaneDocks() { for (Airport& a : airports) if (a.water()) seaplaneDock(a); }

bool World::seaplaneDock(Airport& a) const {
  {
    a.dockLen = 0.f;
    const float hr = a.heading * DEG;
    const vec2 d(sinf(hr), -cosf(hr)), p(cosf(hr), sinf(hr)), C(a.x, a.z);
    float best = 1e9f; vec2 shore, out;
    for (float r = 60.f; r <= 900.f && r < best; r += 10.f)
      for (int k = 0; k < 144; k++) {
        const vec2 s = C + vec2(cosf(k * 0.0436332f), sinf(k * 0.0436332f)) * r, q = s - C;
        if (fabsf(q.x * d.x + q.y * d.y) < a.length * 0.5f + 200.f && fabsf(q.x * p.x + q.y * p.y) < a.width * 0.5f + 90.f) continue;
        const float g = groundHeight(s.x, s.y, 8);
        if (g < 0.3f || g > 3.f) continue;
        for (int b = 0; b < 36; b++) {
          const vec2 o(cosf(b * 0.1745329f), sinf(b * 0.1745329f));
          bool ok = true;
          for (float t = 10.f; t <= 30.f && ok; t += 5.f) ok = groundHeight(s.x + o.x * t, s.y + o.y * t, 8) < (t < 20.f ? 0.f : -1.f);
          for (float t = 30.f; t <= 70.f && ok; t += 5.f)
            for (float w : {-12.f, 0.f, 12.f}) ok = ok && groundHeight(s.x + o.x * t - o.y * w, s.y + o.y * t + o.x * w, 8) < -2.f;
          if (ok) { best = r; shore = s; out = o; break; }
        }
        if (best <= r) break;
      }
    if (best > 1e8f) return false;
    a.dockX = shore.x; a.dockZ = shore.y; a.dockDirX = out.x; a.dockDirZ = out.y; a.dockLen = 25.f;
  }
  return true;
}

// A generated region's seaplane bases (docs/WINGS_OVER_WATER.md): one in a small region, two in a larger one, each a
// water lane 1000 m by 60 m beside one of its towns, the nearest found going out from each town in turn (the fields'
// towns first) - 2 m deep along it, 200 m past its ends and 80 m either side, its approaches under a 1 in 18 slope for
// 3 km, within the chart and clear of every field and base. Appended after the region's airports, whose indices its
// travel, long hauls and stamps keep; named for the town. The islands have their own (kAirports) and the real Earth's
// charts none
void World::surveySeaplaneBases() {
  if (!regionRecipe || earthChart) return;
  const size_t land = airports.size();
  const size_t want = regionRecipe->descriptor.scale == procedural::Scale::Small ? 1 : 2;
  auto deep = [&](float x, float z) { return groundHeight(x, z, 8) < -2.f; };
  auto laneOK = [&](float cx, float cz, float hdg) {
    const float hr = hdg * DEG;
    const vec2 d(sinf(hr), -cosf(hr)), p(cosf(hr), sinf(hr));
    for (float u = -700.f; u <= 700.f; u += 50.f)
      for (float v : {-80.f, 0.f, 80.f}) if (!deep(cx + d.x * u + p.x * v, cz + d.y * u + p.y * v)) return false;
    for (int e = -1; e <= 1; e += 2)
      for (float s = 150.f; s <= 3000.f; s += 150.f)
        for (float v : {-120.f, 0.f, 120.f}) {
          const float u = e * (500.f + s), x = cx + d.x * u + p.x * v, z = cz + d.y * u + p.y * v;
          if (std::max(fabsf(x), fabsf(z)) > WORLD_HALF - 1500.f || groundHeight(x, z, 8) > s / 18.f) return false;
        }
    for (const Airport& a : airports) if (hypotf(cx - a.x, cz - a.z) < a.length * 0.5f + 3500.f) return false;
    return true;
  };
  int dockTries = 0;   // (a dock's search is the survey's dear part: a lane centre gets one, and the region forty)
  for (size_t t = 0; t < towns.size() && airports.size() - land < want && dockTries < 40; t++) {
    const Town& T = towns[t];
    bool found = false;
    for (float r = T.r + 500.f; r <= T.r + 3500.f && !found; r += 250.f)
      for (int k = 0; k < 32 && !found; k++) {
        const float a = k * (2.f * PI / 32.f), cx = T.x + cosf(a) * r, cz = T.z + sinf(a) * r;
        if (!deep(cx, cz)) continue;
        for (int hk = 0; hk < 12 && !found; hk++) {
          if (!laneOK(cx, cz, hk * 15.f)) continue;
          Airport probe{"", "", cx, cz, 0.f, (float)(hk * 15), 1000.f, 60.f, SURF_WATER, 0, ""};
          dockTries++;
          if (!seaplaneDock(probe)) break;   // (a base wants its dock - a shore within reach a jetty can serve: none here, on)
          if (!seaplaneNames) seaplaneNames = std::make_shared<std::deque<std::string>>();
          auto& N = *seaplaneNames;
          const std::string pre = land ? std::string(airports[0].code).substr(0, 2) : std::string("SW");
          N.push_back(pre + char('W' + (airports.size() - land)));
          const char* code = N.back().c_str();
          N.push_back(std::string(T.name) + " Seaplane Base");
          const char* name = N.back().c_str();
          N.push_back(std::string("A water lane beside ") + T.name + ", marked by buoys, with a dock and fuel. Floats only.");
          airports.push_back(Airport{code, name, cx, cz, 0.f, (float)(hk * 15), 1000.f, 60.f, SURF_WATER, 0, N.back().c_str()});
          found = true;
        }
      }
  }
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
