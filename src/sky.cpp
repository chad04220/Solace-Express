// Solace Express - the sky's bodies (sky.h): the celestial sphere turning with the sun's path, the moon at its phase,
// and the stars uploaded for the lighting pass's per-pixel lookup (light_fs.glsl starLight).
#include "sky.h"
#include "renderer.h"
#include "star_catalog.h"
#include <cmath>
#include <ctime>
#include <set>
#include <vector>

vec3 sunDirectionAt(float tod) {
  const float a = (tod - 6.f) / 12.f * PI;
  return normalize(vec3(cosf(a), sinf(a) * 0.93f, 0.35f + 0.1f * sinf(a)));
}

float moonAgeToday() {
  if (const char* e = getenv("MOONAGE")) return fmodf(std::max(0.f, (float)atof(e)), 29.530589f);
  // days since the new moon of 2000 January 6, 18:14 UTC, round the synodic month
  const double days = ((double)std::time(nullptr) - 947182440.0) / 86400.0, month = 29.530588853;
  const double age = fmod(days, month);
  return (float)(age < 0 ? age + month : age);
}

SkyBodies skyBodiesAt(float tod, float moonAge) {
  SkyBodies sb;
  // the celestial pole: the axis the sun's path turns about (north of the zenith's south, a few degrees up)
  const vec3 s6 = sunDirectionAt(6.f), s12 = sunDirectionAt(12.f), s18 = sunDirectionAt(18.f);
  const vec3 P = normalize(cross(s18 - s6, s12 - s6));
  // the sun among the stars in late January: ecliptic longitude 310 degrees (its declination -18, near the path's -20)
  const float eps = 23.44f * DEG, lamS = 310.f * DEG;
  const float alphaS = atan2f(sinf(lamS) * cosf(eps), cosf(lamS));
  // the equinox where the sun's hour stands now: the sun's own direction on the equator, turned back by its right
  // ascension (right ascension runs east: P x X)
  const vec3 s = sunDirectionAt(tod), u = normalize(s - P * dot(s, P));
  const vec3 X = u * cosf(alphaS) - cross(P, u) * sinf(alphaS), Y = cross(P, X);
  const float m[9] = {X.x, Y.x, P.x, X.y, Y.y, P.y, X.z, Y.z, P.z};   // (rows X, Y, P: the game's frame into the equator's)
  for (int i = 0; i < 9; i++) sb.toEquator[i] = m[i];
  // the moon: its elongation from the sun by its age, along the ecliptic (its 5 degree tilt left out)
  sb.moonAge = moonAge >= 0.f ? moonAge : moonAgeToday();
  const float elong = sb.moonAge / 29.530589f * 2.f * PI, lamM = lamS + elong;
  const vec3 eM(cosf(lamM), sinf(lamM) * cosf(eps), sinf(lamM) * sinf(eps));
  sb.moonDir = normalize(X * eM.x + Y * eM.y + P * eM.z);
  sb.moonLit = 0.5f * (1.f - cosf(elong));
  return sb;
}

// A star's colour from its B-V index: its temperature (Ballesteros 2012), the black body's colour there (Helland's
// fit), linear and of unit luminance, then partly whitened - the eye sees only the strongest stars' colours
static vec3 starColour(float bv) {
  const float T = 4600.f * (1.f / (0.92f * bv + 1.7f) + 1.f / (0.92f * bv + 0.62f)), t = T / 100.f;
  float r = t <= 66.f ? 255.f : 329.698727446f * powf(t - 60.f, -0.1332047592f);
  float g = t <= 66.f ? 99.4708025861f * logf(t) - 161.1195681661f : 288.1221695283f * powf(t - 60.f, -0.0755148492f);
  float b = t >= 66.f ? 255.f : t <= 19.f ? 0.f : 138.5177312231f * logf(t - 10.f) - 305.0447927307f;
  vec3 c(powf(clampf(r, 0.f, 255.f) / 255.f, 2.2f), powf(clampf(g, 0.f, 255.f) / 255.f, 2.2f), powf(clampf(b, 0.f, 255.f) / 255.f, 2.2f));
  const float lum = std::max(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 1e-3f);
  c = c / lum;
  return c * 0.6f + vec3(1.f) * 0.4f;
}

// The stars binned for the lookup: the sky's directions (the equator's frame) on a cube, kStarCellsG cells a face
// edge; each cell lists every star within half a degree of it (a pixel looks in its own cell only), and the stars are
// two texels each - direction and magnitude, colour and a seed for the twinkle (light_fs.glsl starLight, in step)
static const int kStarCellsG = 32, kStarTexW = 512;
static int starCell(vec3 e) {
  const vec3 a(fabsf(e.x), fabsf(e.y), fabsf(e.z));
  int face; float u, v;
  if (a.x >= a.y && a.x >= a.z) { face = e.x > 0 ? 0 : 1; u = e.y / a.x; v = e.z / a.x; }
  else if (a.y >= a.z) { face = e.y > 0 ? 2 : 3; u = e.x / a.y; v = e.z / a.y; }
  else { face = e.z > 0 ? 4 : 5; u = e.x / a.z; v = e.y / a.z; }
  const int cx = std::clamp((int)floorf((u * 0.5f + 0.5f) * kStarCellsG), 0, kStarCellsG - 1);
  const int cy = std::clamp((int)floorf((v * 0.5f + 0.5f) * kStarCellsG), 0, kStarCellsG - 1);
  return (face * kStarCellsG + cx) + cy * 6 * kStarCellsG;
}

void Renderer::initStars() {
  const int cells = 6 * kStarCellsG * kStarCellsG;
  std::vector<std::vector<int>> in(cells);
  std::vector<vec3> dir(kStarCount);
  for (int i = 0; i < kStarCount; i++) {
    const float ra = kStarCatalog[i][0] / 65535.f * 2.f * PI, dec = (kStarCatalog[i][1] / 65535.f - 0.5f) * PI;
    const vec3 e(cosf(dec) * cosf(ra), cosf(dec) * sinf(ra), sinf(dec));
    dir[i] = e;
    const vec3 t1 = normalize(cross(fabsf(e.z) < 0.9f ? vec3(0, 0, 1) : vec3(1, 0, 0), e)), t2 = cross(e, t1);
    const float r = 0.5f * DEG;
    std::set<int> mine;
    for (int k = 0; k < 9; k++) {
      const float a = k * PI / 4.f, w = k < 8 ? r : 0.f;
      mine.insert(starCell(normalize(e + t1 * (cosf(a) * w) + t2 * (sinf(a) * w))));
    }
    for (int c : mine) in[c].push_back(i);
  }
  std::vector<uint32_t> head((size_t)cells * 2);
  std::vector<float> list;
  for (int c = 0; c < cells; c++) {
    head[(size_t)c * 2] = (uint32_t)(list.size() / 8); head[(size_t)c * 2 + 1] = (uint32_t)in[c].size();
    for (int i : in[c]) {
      const vec3 col = starColour(kStarCatalog[i][3] / 1000.f - 0.5f);
      const float mag = kStarCatalog[i][2] / 100.f - 2.f;
      list.insert(list.end(), {dir[i].x, dir[i].y, dir[i].z, mag, col.x, col.y, col.z, (float)((i * 2654435761u) % 1000u) / 1000.f});
    }
  }
  const int texels = (int)(list.size() / 4), rows = (texels + kStarTexW - 1) / kStarTexW;
  list.resize((size_t)rows * kStarTexW * 4, 0.f);
  glGenTextures(1, &texStarCells); glBindTexture(GL_TEXTURE_2D, texStarCells);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32UI, 6 * kStarCellsG, kStarCellsG, 0, GL_RG_INTEGER, GL_UNSIGNED_INT, head.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &texStars); glBindTexture(GL_TEXTURE_2D, texStars);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kStarTexW, rows, 0, GL_RGBA, GL_FLOAT, list.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  starCellsG = kStarCellsG; starTexW = kStarTexW;
}

// the lighting pass's sky: the stars' textures (units 24, 25: free in it), the celestial frame and the moon
void Renderer::bindSky(GLuint p, const FrameParams& fp) {
  glActiveTexture(GL_TEXTURE0 + 24); glBindTexture(GL_TEXTURE_2D, texStarCells); glUniform1i(U(p, "uStarCells"), 24);
  glActiveTexture(GL_TEXTURE0 + 25); glBindTexture(GL_TEXTURE_2D, texStars); glUniform1i(U(p, "uStars"), 25);
  glUniform1i(U(p, "uStarG"), texStarCells && texStars ? starCellsG : 0); glUniform1i(U(p, "uStarW"), starTexW);
  glUniformMatrix3fv(U(p, "uSkyRot"), 1, GL_FALSE, fp.skyRot);
}
