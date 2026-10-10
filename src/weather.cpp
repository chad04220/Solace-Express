// Solace Express - the weather's fields (weather.h): the wind, the clouds and the rain round the aircraft
#include "weather.h"
#include <mutex>

namespace wxfield {

// ---------------------------------------------------------------- the baked cloud noise
static std::vector<uint8_t> s_cov, s_vol;
static std::once_flag s_noiseOnce;

static void genNoise() {
  auto hp = [](int x, int y, int z, int P) {
    x = ((x % P) + P) % P; y = ((y % P) + P) % P; z = ((z % P) + P) % P;
    // Hash mixing deliberately wraps at 32 bits; signed products would be undefined at these cell sizes.
    uint32_t hx = uint32_t(x)*73856093u ^ (uint32_t(z)*19349663u) ^ (uint32_t(P)*7919u);
    uint32_t hy = uint32_t(y)*83492791u + uint32_t(z)*2971u;
    return hash2i(static_cast<int32_t>(hx), static_cast<int32_t>(hy));
  };
  auto s3 = [](float t) { return t * t * (3.f - 2.f * t); };
  auto vn2 = [&](float x, float y, int P) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = s3(x - ix), fy = s3(y - iy);
    return lerpf(lerpf(hp(ix, iy, 0, P), hp(ix + 1, iy, 0, P), fx), lerpf(hp(ix, iy + 1, 0, P), hp(ix + 1, iy + 1, 0, P), fx), fy);
  };
  const int CN = kCovN;
  s_cov.resize((size_t)CN * CN);
  parallelFor(CN, [&](int y) {
    for (int x = 0; x < CN; x++) {
      float qx = (x + 0.5f) / CN * 16.f, qy = (y + 0.5f) / CN * 16.f, s = 0, a = 0.5f; int P = 16;
      for (int o = 0; o < 4; o++) { s += a * vn2(qx, qy, P); qx *= 2; qy *= 2; P *= 2; a *= 0.5f; }
      s_cov[(size_t)y * CN + x] = (uint8_t)std::min(255.f, s / 0.9375f * 255.f + 0.5f);
    }
  });
  const int VN = kVolN, VP = 32;   // 4 texels per lattice cell
  s_vol.resize((size_t)VN * VN * VN);
  parallelFor(VN, [&](int z) {
    for (int y = 0; y < VN; y++)
      for (int x = 0; x < VN; x++) {
        float px = (x + 0.5f) / VN * VP, py = (y + 0.5f) / VN * VP, pz = (z + 0.5f) / VN * VP;
        int ix = (int)floorf(px), iy = (int)floorf(py), iz = (int)floorf(pz);
        float fx = s3(px - ix), fy = s3(py - iy), fz = s3(pz - iz);
        auto h = [&](int a, int b, int c) { return hp(ix + a, iy + b, iz + c, VP); };
        float v = lerpf(lerpf(lerpf(h(0, 0, 0), h(1, 0, 0), fx), lerpf(h(0, 1, 0), h(1, 1, 0), fx), fy),
                        lerpf(lerpf(h(0, 0, 1), h(1, 0, 1), fx), lerpf(h(0, 1, 1), h(1, 1, 1), fx), fy), fz);
        s_vol[((size_t)z * VN + y) * VN + x] = (uint8_t)(v * 255.f + 0.5f);
      }
  });
}
const uint8_t* coverageMap() { std::call_once(s_noiseOnce, genNoise); return s_cov.data(); }
const uint8_t* noiseVolume() { std::call_once(s_noiseOnce, genNoise); return s_vol.data(); }

// the textures as the GPU samples them: GL_LINEAR, GL_REPEAT, 8-bit texels (u, v, w in texture units)
static float covTex(float u, float v) {
  const uint8_t* c = coverageMap();
  float x = u * kCovN - 0.5f, y = v * kCovN - 0.5f, fx = floorf(x), fy = floorf(y), tx = x - fx, ty = y - fy;
  const unsigned M = kCovN - 1, ix = (unsigned)(int)fx, iy = (unsigned)(int)fy;
  auto at = [&](unsigned i, unsigned j) { return (float)c[(size_t)(j & M) * kCovN + (i & M)]; };
  return lerpf(lerpf(at(ix, iy), at(ix + 1, iy), tx), lerpf(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty) * (1.f / 255.f);
}
static float cn3(vec3 p) {   // noise_tex.glsl cn3: the volume at p / 32
  const uint8_t* c = noiseVolume();
  float x = p.x * (kVolN / 32.f) - 0.5f, y = p.y * (kVolN / 32.f) - 0.5f, z = p.z * (kVolN / 32.f) - 0.5f;
  float fx = floorf(x), fy = floorf(y), fz = floorf(z), tx = x - fx, ty = y - fy, tz = z - fz;
  const unsigned M = kVolN - 1, ix = (unsigned)(int)fx, iy = (unsigned)(int)fy, iz = (unsigned)(int)fz;
  auto at = [&](unsigned i, unsigned j, unsigned k) { return (float)c[((size_t)(k & M) * kVolN + (j & M)) * kVolN + (i & M)]; };
  float a = lerpf(lerpf(at(ix, iy, iz), at(ix + 1, iy, iz), tx), lerpf(at(ix, iy + 1, iz), at(ix + 1, iy + 1, iz), tx), ty);
  float b = lerpf(lerpf(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), tx), lerpf(at(ix, iy + 1, iz + 1), at(ix + 1, iy + 1, iz + 1), tx), ty);
  return lerpf(a, b, tz) * (1.f / 255.f);
}

// ---------------------------------------------------------------- the clouds and the rain
static vec3 windDir(const Weather& wx) { float wf = wx.windFrom * DEG; return vec3(-sinf(wf), 0, cosf(wf)); }   // the way the air moves
float cloudThickness(const Weather& wx) { return 900.f + 900.f * wx.cloudCover; }

float cloudColumn(const Weather& wx, float x, float z) {
  float cov = covTex((x + wx.cloudDrift.x) / (5200.f * 16.f), (z + wx.cloudDrift.y) / (5200.f * 16.f)) * 0.9375f;
  return cov - (1.05f - wx.cloudCover * 0.75f);
}

float cloudDensity(const Weather& wx, vec3 p, bool detail) {
  if (wx.cloudCover < 0.02f) return 0.f;
  const float thick = cloudThickness(wx), hf = (p.y - wx.cloudBase) / thick;
  if (hf < 0.f || hf > 1.f) return 0.f;
  // the tops lean downwind (the wind is stronger aloft): the cloud at height hf is the column from upwind of it
  vec3 d = windDir(wx);
  float lean = hf * std::min(wx.windSpeed * 30.f, 450.f), lx = d.x * lean, lz = d.z * lean;
  float cov = covTex((p.x - lx + wx.cloudDrift.x) / (5200.f * 16.f), (p.z - lz + wx.cloudDrift.y) / (5200.f * 16.f)) * 0.9375f;
  float shape = smoothstepf(0.f, 0.07f, hf) * smoothstepf(1.f, 0.4f - 0.22f * wx.cloudCover, hf);
  float den = cov - (1.05f - wx.cloudCover * 0.75f) + shape * 0.45f - 0.45f;
  if (den < -0.2f) return 0.f;
  vec3 w = p + vec3(wx.cloudDrift.x - lx, 0, wx.cloudDrift.y - lz);
  float bill = cn3(w / 760.f) * 0.6f + cn3((w - vec3(0, wx.cloudBoil, 0)) / 270.f + vec3(11.3f, 4.1f, 7.7f)) * 0.4f;
  den += (bill - 0.55f) * 0.42f * (0.55f + hf);
  if (detail) {
    vec3 wv = w + wx.cloudDetail;
    float e = (cn3(wv / 95.f + vec3(3.7f, 0, 1.9f)) - 0.5f) * 0.17f + (cn3(wv / 36.f + vec3(17.1f, 9.3f, 5.5f)) - 0.5f) * 0.06f;
    den += e * (1.f - smoothstepf(0.f, 0.3f, den));
  }
  return clampf(den * 4.5f, 0.f, 1.f);
}

float rainAt(const Weather& wx, vec3 p) {
  if (wx.precip == 0 || wx.cloudCover < 0.02f) return 0.f;
  const float top = wx.cloudBase + cloudThickness(wx);
  if (p.y > top) return 0.f;
  // rain falls at about 9 m/s (snow 1.5), and the wind carries it: below the base it fell from a cloud upwind
  const float fall = wx.precip == 2 ? 1.5f : 9.f;
  const float drift = std::min(std::max(wx.cloudBase - p.y, 0.f) * wx.windSpeed / fall, 3000.f);
  const vec3 d = windDir(wx);
  // (under a cloud at all, and then the heavier the denser the field above: an overcast rains everywhere, lightly
  // between its heavy cells; scattered showers only under their clouds)
  float cov = covTex((p.x - d.x * drift + wx.cloudDrift.x) / (5200.f * 16.f), (p.z - d.z * drift + wx.cloudDrift.y) / (5200.f * 16.f)) * 0.9375f;
  float c = cov - (1.05f - wx.cloudCover * 0.75f);
  return smoothstepf(-0.25f, 0.15f, c) * (0.35f + 0.65f * smoothstepf(0.42f, 0.62f, cov)) * (wx.storm ? 1.f : 0.85f) * smoothstepf(top, top - 300.f, p.y);
}

// ---------------------------------------------------------------- the wind
vec3 meanWind(const Weather& wx, float agl) {
  float prof = clampf(powf(std::max(agl, 1.f) / 10.f, 0.14f), 0.35f, 1.6f);
  float from = (wx.windFrom + 15.f * smoothstepf(0.f, 1000.f, agl)) * DEG;
  return vec3(-sinf(from), 0, cosf(from)) * (wx.windSpeed * prof);
}
vec3 driftWind(const Weather& wx) { return windDir(wx) * (wx.windSpeed * 1.15f); }

static float groundAt(float x, float z) { return std::max(g_world.height(x, z, 3), 0.f); }

Local local(const Weather& wx, vec3 p, float agl) {
  Local L;
  const float U = wx.windSpeed, g0 = p.y - agl, h = std::max(agl, 0.f);
  const vec3 d = windDir(wx);
  const float Ua = length(meanWind(wx, h));
  // ---- the terrain: the wind climbing a slope lifts, pouring down the lee it sinks; a ridge upwind that stands
  // above the aircraft sends rotors down its lee. The lift fades with height over the hills' own scale.
  float hU = groundAt(p.x - d.x * 300.f, p.z - d.z * 300.f), hD = groundAt(p.x + d.x * 300.f, p.z + d.z * 300.f);
  float hR = groundAt(p.x - d.x * 900.f, p.z - d.z * 900.f);
  float relief = std::max(std::max(hU, hD), std::max(hR, g0)) - std::min(std::min(hU, hD), std::min(hR, std::max(g0, 0.f)));
  float slope = clampf((hD - hU) / 600.f, -0.45f, 0.45f);
  float H = 220.f + 0.6f * relief;
  L.lift = clampf(Ua * slope * expf(-h / H), -4.f, 5.f);
  float lee = std::max(0.f, -slope) * expf(-h / H) + 0.3f * smoothstepf(-80.f, 120.f, hR - p.y);
  // ---- the eddies: the weather's own (a little stronger in the lowest 300 m, as before), or what the wind stirs
  // up over the ground (MIL-F-8785C: sigma_w = 0.1 x the wind at 20 ft), the rougher the ground the more - the sea
  // smooth, forest, towns and broken hills rough - whichever is the stronger
  float mask[4] = {0, 0, 0, 0}, forest = 0;
  if (!g_world.mask.empty() && !g_world.roadGrid.head.empty()) { g_world.sampleMask(p.x, p.z, mask); forest = g_world.forestAt(p.x, p.z); }
  float rough = g0 <= 0.5f ? 0.5f : 1.f + 0.3f * clampf(forest + mask[2], 0.f, 1.f) + 0.6f * clampf(relief / 350.f, 0.f, 1.f);
  float nearG = smoothstepf(300.f, 0.f, h) * (h > 3.f ? 1.f : 0.f);
  float base = 0.8f * wx.turbulence * (1.f + 1.5f * nearG);
  float mech = 0.08f * U * rough * smoothstepf(600.f, 0.f, h);
  float sigma = std::max(base, mech) + 0.12f * Ua * lee;
  // ---- the clouds: thermals under fair-weather cumulus on a sunny day (rising under the clouds' cores, sinking
  // gently between them), carrying on up inside the cloud; bumps inside a cumulus; a storm cell's downdraft under its
  // rain, spreading out near the ground as a gust front, and its updraft and violence inside
  const float top = wx.cloudBase + cloudThickness(wx);
  L.cloud = cloudDensity(wx, p, false);
  L.rain = rainAt(wx, p);
  float c0 = wx.cloudCover >= 0.02f ? cloudColumn(wx, p.x, p.z) : -1.f;
  float sun = smoothstepf(9.5f, 12.f, wx.timeOfDay) * smoothstepf(18.5f, 15.5f, wx.timeOfDay);
  float cu = smoothstepf(0.08f, 0.22f, wx.cloudCover) * smoothstepf(0.88f, 0.6f, wx.cloudCover) * (wx.precip ? 0.35f : 1.f) * (wx.storm ? 0.f : 1.f);
  float conv = sun * cu * (g0 > 0.5f ? 1.f : 0.3f) * smoothstepf(0.f, 0.12f, wx.turbulence);   // (still air - a calm, settled day - has none)
  float core = smoothstepf(-0.25f, -0.08f, c0);   // (where the clouds' billows can build: their columns, the top third or so)
  vec3 draft(0, L.lift, 0);
  if (conv > 0.f && p.y < top) {
    if (p.y < wx.cloudBase) {
      float zi = std::max(wx.cloudBase - g0, 60.f), z = clampf(h / zi, 0.f, 1.f);
      float prof = powf(z, 1.f / 3.f) * (1.f - 0.8f * z) * 1.45f * smoothstepf(0.f, 60.f, h);
      L.thermal = 2.4f * conv * (core - 0.15f) * prof;
      sigma += 0.4f * conv * prof * core;
    } else L.thermal = 2.6f * conv * L.cloud;
  }
  draft.y += L.thermal;
  if (L.cloud > 0.f) sigma += L.cloud * (wx.cloudCover > 0.85f ? 0.15f : 0.25f + 0.5f * cu);
  if (wx.storm) {
    float cell = smoothstepf(0.05f, 0.3f, c0);
    if (p.y < wx.cloudBase) {
      draft.y -= 2.5f * cell * smoothstepf(30.f, 350.f, h);
      float gx = cloudColumn(wx, p.x + 500.f, p.z) - cloudColumn(wx, p.x - 500.f, p.z), gz = cloudColumn(wx, p.x, p.z + 500.f) - cloudColumn(wx, p.x, p.z - 500.f);
      float gl = sqrtf(gx * gx + gz * gz);
      if (gl > 1e-4f) { float k = -3.5f * cell * clampf(gl * 8.f, 0.f, 1.f) * (1.f - smoothstepf(150.f, 700.f, h)) / gl; draft.x += gx * k; draft.z += gz * k; }
      sigma += 0.45f + 0.5f * cell;
    } else if (p.y < top) {
      draft.y += 4.f * cell * L.cloud;
      sigma += 0.45f + 1.f * cell * std::max(L.cloud, 0.3f);
    } else sigma += 0.2f;
  }
  L.draft = draft;
  L.sigma = sigma;
  return L;
}

// value noise in [-1, 1] (rms 0.40) with its gradient: one component of the eddies' velocity
static inline float h3(int x, int y, int z, int seed) {   // (mixed in unsigned arithmetic: it wraps)
  const uint32_t a = uint32_t(x) + uint32_t(z) * 1619u + uint32_t(seed) * 7919u, b = uint32_t(y) + uint32_t(z) * 31337u - uint32_t(seed) * 104729u;
  return hash2i(static_cast<int32_t>(a), static_cast<int32_t>(b));
}
static float vnoise3g(vec3 p, int seed, vec3& g) {
  float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
  int ix = (int)fx, iy = (int)fy, iz = (int)fz;
  float x = p.x - fx, y = p.y - fy, z = p.z - fz;
  auto fd = [](float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); };
  auto dfd = [](float t) { return 30.f * t * t * (t * (t - 2.f) + 1.f); };
  float ux = fd(x), uy = fd(y), uz = fd(z), dux = dfd(x), duy = dfd(y), duz = dfd(z);
  float c000 = h3(ix, iy, iz, seed), c100 = h3(ix + 1, iy, iz, seed), c010 = h3(ix, iy + 1, iz, seed), c110 = h3(ix + 1, iy + 1, iz, seed);
  float c001 = h3(ix, iy, iz + 1, seed), c101 = h3(ix + 1, iy, iz + 1, seed), c011 = h3(ix, iy + 1, iz + 1, seed), c111 = h3(ix + 1, iy + 1, iz + 1, seed);
  float k1 = c100 - c000, k2 = c010 - c000, k3 = c001 - c000, k4 = c000 - c100 - c010 + c110, k5 = c000 - c010 - c001 + c011;
  float k6 = c000 - c100 - c001 + c101, k7 = -c000 + c100 + c010 - c110 + c001 - c101 - c011 + c111;
  float v = c000 + k1 * ux + k2 * uy + k3 * uz + k4 * ux * uy + k5 * uy * uz + k6 * uz * ux + k7 * ux * uy * uz;
  g = vec3(dux * (k1 + k4 * uy + k6 * uz + k7 * uy * uz), duy * (k2 + k5 * uz + k4 * ux + k7 * uz * ux), duz * (k3 + k6 * ux + k5 * uy + k7 * ux * uy)) * 2.f;
  return 2.f * v - 1.f;
}

Sample wind(const Weather& wx, const Local& L, vec3 p, float agl, float time, vec3 airOff, float span) {
  Sample S;
  S.v = meanWind(wx, agl) + L.draft;
  const vec3 pa = p - airOff;   // where this air was when the flight began: the eddies and the gusts ride the wind
  // ---- the eddies: four fixed sizes (480, 160, 54, 18 m) weighted by a Kolmogorov spectrum (rms ~ size^1/3) up to the
  // largest eddies the height allows (MIL-F-8785C: the vertical's as big as the height, the horizontal's larger), so
  // they are small and quick near the ground and long and smooth aloft; they evolve slowly as they drift. The
  // horizontal eddies are stronger near the ground; the vertical ones fade in the last wingspan (the air can't flow
  // through the surface). Their gradient across a wing (the smaller than the span averaged out) is the gusts' roll,
  // pitch and yaw.
  if (L.sigma > 1e-4f) {
    const float a = std::max(agl, 1.f), hft = std::min(a * M_TO_FT, 1000.f), k = 0.177f + 0.000823f * hft;
    const float Lw = clampf(a, 20.f, 480.f), Lu = clampf(a / powf(k, 1.2f), 60.f, 480.f);
    const float ku = clampf(1.f / powf(k, 0.4f), 1.f, 1.6f);
    const float sw = L.sigma * clampf(agl / std::max(span, 8.f), 0.3f, 1.f), su = L.sigma * ku;
    static const float kL[4] = {480.f, 160.f, 54.f, 18.f};
    auto amp = [](float l, float Lo) { return powf(std::min(l, Lo) / Lo, 1.f / 3.f) * (l > Lo ? Lo / l : 1.f); };
    float nw = 0, nu = 0;
    for (int i = 0; i < 4; i++) { float aw = amp(kL[i], Lw), au = amp(kL[i], Lu); nw += aw * aw; nu += au * au; }
    nw = 1.f / (0.40f * sqrtf(nw)); nu = 1.f / (0.40f * sqrtf(nu));
    const vec3 evo = vec3(0.31f, 0.83f, 0.47f) * time;   // (1 m/s: the eddies change as they go, even in a calm)
    for (int i = 0; i < 4; i++) {
      const float l = kL[i], aw = amp(l, Lw) * nw * sw, au = amp(l, Lu) * nu * su;
      const float f = l * l / (l * l + 0.5f * span * span) / l;   // (per metre; the span's averaging)
      vec3 q = (pa + evo) / l + vec3(i * 17.3f, i * 5.1f, i * 11.7f), gu, gv, gw;
      float vu = vnoise3g(q, 1, gu), vw = vnoise3g(q + vec3(41.2f, 7.7f, 3.1f), 2, gw), vv = vnoise3g(q + vec3(5.9f, 23.3f, 61.4f), 3, gv);
      S.v += vec3(vu * au, vw * aw, vv * au);
      S.gx += gu * (au * f); S.gy += gw * (aw * f); S.gz += gv * (au * f);
    }
  }
  // ---- the gusts: bursts of stronger wind (patches about 150 m along the wind and 300 m across, carried by it and
  // changing as they come) that build in a second or two, peak at the reported gust and die away, swinging the wind
  // a little as they pass; a lull between them. A parked aircraft meets one every ten or twenty seconds; flying,
  // they come quicker and sharper.
  if (wx.gust > 0.05f) {
    const vec3 d = windDir(wx), c(d.z, 0, -d.x);
    float s = (pa.x * d.x + pa.z * d.z) / 150.f, x = (pa.x * c.x + pa.z * c.z) / 300.f;
    float n, n2, dx, dz;
    noised(s + time * 0.07f, x + time * 0.03f + 31.7f, n, dx, dz);
    noised(s * 0.5f + 7.3f, x - time * 0.02f, n2, dx, dz);
    float e = smoothstepf(0.05f, 0.55f, n);
    S.burst = e;
    const float G = wx.gust * lerpf(1.f, 0.35f, smoothstepf(200.f, 1200.f, agl));   // (the gusts are the surface layer's: weaker aloft)
    S.v += d * (G * (1.05f * e - 0.12f)) + c * (G * 0.35f * e * n2) + vec3(0, G * 0.08f * e * n2, 0);
  }
  return S;
}

}  // namespace wxfield
