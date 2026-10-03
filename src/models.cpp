// Solace Express - hand-built aircraft geometry. Dimensions are metres in body coordinates (+z aft).
#include "models.h"
#include "aircraft.h"

// clang-format off
const ModelDef kModels[] = {
  // ---------------------------------------------------------------- Kestrel T2 (high-wing two-seat trainer)
  { {{-3.65f,.10f,.10f,-.02f},{-3.42f,.36f,.34f,-.04f},{-2.45f,.50f,.44f,-.01f},{-1.55f,.56f,.66f,.06f},{-0.35f,.55f,.64f,.07f},{1.0f,.34f,.42f,.14f},{2.95f,.12f,.20f,.30f},{3.65f,.05f,.10f,.34f}}, .75f,
    {5.05f,1.62f,1.10f,.25f,.74f,-1.72f,1.5f,.13f}, 1,2.4f,0,.55f, 0,0,
    {1.65f,.95f,.70f,.15f,.30f,2.60f,0}, 0,
    {1.30f,1.25f,.62f,.85f,.25f,2.25f},
    0, 0,0,0,0,0, .13f,.87f,
    0, .24f, 0,
    0, 0,0,0,0,0,
    vec3(-.28f,.50f,-1.15f), 0, -2.35f,-1.55f,.34f,-0.40f },
  // ---------------------------------------------------------------- Wren 180 (four-seat tourer)
  { {{-4.15f,.11f,.11f,-.02f},{-3.92f,.38f,.36f,-.04f},{-2.75f,.54f,.48f,-.01f},{-1.75f,.60f,.70f,.07f},{-0.10f,.59f,.69f,.08f},{1.3f,.38f,.46f,.15f},{3.40f,.13f,.22f,.32f},{4.15f,.05f,.11f,.36f}}, .75f,
    {5.50f,1.62f,1.12f,.22f,.80f,-1.85f,1.7f,.13f}, 1,2.6f,0,.55f, 0,0,
    {1.75f,1.05f,.72f,.18f,.32f,3.00f,0}, 0,
    {1.45f,1.35f,.65f,.95f,.27f,2.60f},
    0, 0,0,0,0,0, .14f,.95f,
    0, .26f, 0,
    0, 0,0,0,0,0,
    vec3(-.30f,.54f,-1.30f), 0, -2.60f,-1.75f,.36f,0.45f },
  // ---------------------------------------------------------------- Bushmaster STOL (taildragger, tundra tyres, slats)
  { {{-4.00f,.12f,.12f,0},{-3.75f,.42f,.40f,-.02f},{-2.75f,.52f,.50f,0},{-1.80f,.56f,.70f,.08f},{-0.40f,.55f,.68f,.08f},{1.0f,.36f,.45f,.16f},{3.30f,.12f,.22f,.33f},{4.00f,.05f,.12f,.36f}}, .55f,
    {6.20f,1.80f,1.70f,.05f,.82f,-1.95f,1.0f,.15f}, 1,2.9f,0,.60f, 1,0,
    {1.95f,1.10f,.80f,.15f,.35f,2.85f,0}, 0,
    {1.40f,1.30f,.70f,.80f,.28f,2.65f},
    0, 0,0,0,0,0, .16f,1.00f,
    2, .38f, 0,
    0, 0,0,0,0,0,
    vec3(-.28f,.56f,-1.25f), 0, -2.70f,-1.80f,.38f,0.30f },
  // ---------------------------------------------------------------- Islander Twin (boxy high-wing twin)
  { {{-5.45f,.15f,.15f,-.05f},{-5.20f,.40f,.40f,-.08f},{-4.40f,.60f,.60f,-.02f},{-3.50f,.72f,.82f,.04f},{-0.50f,.72f,.82f,.04f},{1.80f,.50f,.62f,.12f},{4.60f,.15f,.30f,.40f},{5.45f,.06f,.14f,.45f}}, .35f,
    {7.45f,2.05f,2.05f,0,.95f,-2.40f,1.0f,.14f}, 0,0,0,.60f, 0,0,
    {2.30f,1.30f,1.05f,.10f,.45f,3.90f,0}, 0,
    {1.90f,1.70f,1.00f,1.00f,.35f,3.50f},
    2, 2.38f,.78f,.38f,-3.60f,3.0f, .14f,1.00f,
    1, .32f, 0,
    4, -2.4f,1.2f,.22f,.20f,.17f,
    vec3(-.35f,.55f,-3.10f), 1, -4.10f,-3.40f,.32f,-2.60f },
  // ---------------------------------------------------------------- Pelican Caravan (single turboprop, cargo pod)
  { {{-5.75f,.14f,.14f,-.10f},{-5.50f,.36f,.36f,-.10f},{-4.30f,.52f,.55f,-.05f},{-3.20f,.78f,.85f,.08f},{-0.50f,.80f,.88f,.10f},{2.0f,.55f,.62f,.20f},{4.90f,.16f,.30f,.45f},{5.75f,.06f,.14f,.50f}}, .45f,
    {7.95f,2.00f,1.45f,.20f,1.00f,-2.75f,2.0f,.14f}, 1,3.3f,0,.65f, 0,0,
    {2.60f,1.35f,.95f,.20f,.55f,4.25f,0}, 0,
    {2.00f,1.90f,.95f,1.25f,.45f,3.75f},
    1, 0,0,0,0,0, .17f,1.30f,
    1, .36f, 1,
    5, -2.0f,2.2f,.30f,.21f,.18f,
    vec3(-.38f,.62f,-2.45f), 1, -3.60f,-2.90f,.40f,-2.10f },
  // ---------------------------------------------------------------- Meridian Q400 (T-tail regional turboprop airliner)
  { {{-13.0f,.15f,.15f,-.30f},{-12.6f,.60f,.62f,-.20f},{-11.5f,1.05f,1.05f,-.05f},{-9.8f,1.32f,1.38f,0},{8.0f,1.32f,1.38f,0},{10.5f,1.0f,1.10f,.25f},{12.4f,.45f,.60f,.70f},{13.0f,.20f,.30f,.85f}}, 1.0f,
    {13.70f,2.90f,1.45f,.40f,1.20f,-1.50f,2.0f,.15f}, 0,0,0,.55f, 0,1,
    {3.90f,2.10f,1.30f,.90f,0,0,0}, 1,
    {4.00f,3.60f,2.30f,2.60f,.90f,8.80f},
    3, 3.56f,.90f,.80f,-5.40f,6.4f, .34f,2.00f,
    3, .45f, 0,
    18, -8.5f,7.5f,.25f,.12f,.17f,
    vec3(-.55f,.55f,-10.60f), 2, -11.90f,-10.90f,.35f,-10.2f },
  // ---------------------------------------------------------------- Starling 500 (low-wing T-tail business jet)
  { {{-7.00f,.05f,.05f,-.15f},{-6.60f,.32f,.30f,-.15f},{-5.60f,.65f,.60f,-.05f},{-4.60f,.92f,.95f,.05f},{2.0f,.92f,.95f,.05f},{4.50f,.80f,.85f,.15f},{6.40f,.38f,.48f,.40f},{7.00f,.12f,.22f,.50f}}, 1.0f,
    {7.95f,2.50f,1.00f,1.40f,-.55f,-0.90f,4.0f,.12f}, 0,0,.80f,.55f, 0,0,
    {2.90f,1.50f,.80f,.90f,0,0,0}, 1,
    {2.40f,2.40f,1.30f,1.90f,.60f,4.40f},
    4, 1.50f,.33f,.48f,2.24f,2.52f, 0,0,
    4, .33f, 0,
    6, -3.8f,1.2f,.20f,.17f,.21f,
    vec3(-.42f,.40f,-4.75f), 2, -5.95f,-4.95f,.22f,-4.4f },
  // ---------------------------------------------------------------- XR-9 Specter (research VTOL; custom SDF in the shader, engine code 5)
  { {{-9.00f,.04f,.03f,-.05f},{-7.60f,.40f,.22f,-.02f},{-5.60f,.82f,.48f,.06f},{-3.60f,1.05f,.62f,.08f},{0.0f,1.15f,.60f,.05f},{3.50f,1.25f,.55f,0},{6.60f,1.15f,.45f,-.02f},{8.20f,.95f,.40f,-.02f}}, .45f,
    {5.60f,7.20f,1.20f,5.60f,-.20f,-1.60f,-2.0f,.045f}, 0,0,0,.82f, 0,0,
    {1.90f,1.60f,.55f,1.10f,.05f,-6.60f,0}, 0,
    {2.30f,2.60f,1.00f,1.90f,.35f,4.60f},
    5, .80f,-.10f,.55f,1.0f,7.4f, 0,0,
    4, .38f, 0,
    0, 0,0,0,0,0,
    vec3(0,.62f,-4.70f), 3, -6.0f,-4.9f,.5f,-4.0f },
  // ---------------------------------------------------------------- XR-11 Wraith (stealth research craft; faceted SDF in the shader, engine code 6)
  { {{-8.40f,.04f,.03f,-.05f},{-7.00f,.45f,.24f,-.02f},{-5.20f,.85f,.48f,.05f},{-3.20f,1.10f,.58f,.06f},{0.0f,1.25f,.58f,.04f},{3.20f,1.30f,.52f,0},{6.00f,1.05f,.42f,-.02f},{7.80f,.80f,.32f,-.02f}}, .25f,
    {6.20f,7.60f,1.40f,5.40f,-.15f,-2.30f,0.0f,.04f}, 0,0,0,.82f, 0,0,
    {0,0,0,0,0,0,0}, 0,
    {2.20f,2.40f,1.10f,1.60f,.35f,4.80f},
    6, 2.55f,-.02f,.55f,-3.30f,1.6f, 0,0,
    4, .40f, 0,
    0, 0,0,0,0,0,
    vec3(0,.58f,-4.30f), 3, -5.6f,-4.5f,.5f,-3.6f },
};
// clang-format on

static void stationAt(const ModelDef& m, float z, float& hw, float& hh, float& cy) {
  if (z <= m.st[0][0]) { hw = m.st[0][1]; hh = m.st[0][2]; cy = m.st[0][3]; return; }
  for (int i = 0; i < 7; i++) {
    if (z <= m.st[i + 1][0]) {
      float t = (z - m.st[i][0]) / (m.st[i + 1][0] - m.st[i][0]);
      t = t * t * (3 - 2 * t);
      hw = lerpf(m.st[i][1], m.st[i + 1][1], t); hh = lerpf(m.st[i][2], m.st[i + 1][2], t); cy = lerpf(m.st[i][3], m.st[i + 1][3], t);
      return;
    }
  }
  hw = m.st[7][1]; hh = m.st[7][2]; cy = m.st[7][3];
}

float modelHalfWidth(const ModelDef& m, float z) { float hw, hh, cy; stationAt(m, z, hw, hh, cy); return hw; }

int modelProps(const ModelDef& m, float out[2][4]) {
  if (m.engine <= 1) {
    out[0][0] = 0; out[0][1] = m.st[0][3]; out[0][2] = m.st[0][0] - 0.12f - m.spinnerR * 0.6f; out[0][3] = m.propR;
    return 1;
  }
  if (m.engine <= 3) {
    for (int i = 0; i < 2; i++) {
      out[i][0] = (i ? 1.f : -1.f) * m.nacX; out[i][1] = m.nacY; out[i][2] = m.nacZ0 - m.spinnerR * 1.2f - 0.08f; out[i][3] = m.propR;
    }
    return 2;
  }
  return 0;
}

void packModel(const AircraftSpec& s, int idx, float gh, float o[24 * 4]) {
  const ModelDef& m = kModels[idx];
  auto put = [&](int i, float a, float b, float c, float d) { o[i * 4] = a; o[i * 4 + 1] = b; o[i * 4 + 2] = c; o[i * 4 + 3] = d; };
  float L = s.fusLen;
  put(0, L, (float)m.gear, (float)m.engine, s.fusRad);
  for (int i = 0; i < 8; i++) put(1 + i, m.st[i][0], m.st[i][1], m.st[i][2], m.st[i][3]);
  put(9, m.wing[0], m.wing[1], m.wing[2], m.wing[3]);
  put(10, m.wing[4], m.wing[5], tanf(m.wing[6] * DEG), m.wing[7]);
  put(11, (float)m.strut, m.strutX, m.winglet, m.flapFrac);
  float hty = m.ht[4], htz = m.ht[5];
  if (m.ttail) { hty = m.vt[4] + m.vt[0] - 0.05f; htz = m.vt[5] + m.vt[3] + (m.vt[2] - m.ht[1]) * 0.5f; }
  put(12, m.ht[0], m.ht[1], m.ht[2], m.ht[3]);
  put(13, hty, htz, tanf(m.ht[6] * DEG), (float)m.ttail);
  put(14, m.vt[0], m.vt[1], m.vt[2], m.vt[3]);
  put(15, m.vt[4], m.vt[5], m.roundness, (float)m.slats);
  put(16, m.nacX, m.nacY, m.nacR, m.nacZ0);
  put(17, m.nacLen, m.spinnerR, m.propR, (float)m.cargoPod);
  float track = std::max(1.2f, s.span * 0.13f);
  put(18, track, m.wheelR, s.taildragger ? -0.10f * L : 0.04f * L, -0.36f * L);
  put(19, gh, 0.45f * L, s.taildragger ? 1.f : 0.f, (float)m.deice);
  put(20, (float)m.winCount, m.winZ0, m.winZ1, m.winY);
  float panelZ = m.eye.z - (m.cockpit == 2 ? 0.85f : 0.68f);
  put(21, m.winW, m.winH, (float)m.cockpit, panelZ);
  put(22, m.eye.x, m.eye.y, m.eye.z, modelHalfWidth(m, panelZ) * 0.93f);
  put(23, m.wsZ0, m.wsZ1, m.wsY, m.sideZ1);
}

vec3 modelWingTip(const ModelDef& m) { return vec3(m.wing[0] + 0.02f, m.wing[4] + m.wing[0] * tanf(m.wing[6] * DEG), m.wing[5] + m.wing[3] + m.wing[2] * 0.25f); }
vec3 modelFinTop(const ModelDef& m) { return vec3(0, m.vt[4] + m.vt[0] + 0.04f, m.vt[5] + m.vt[3] + m.vt[2] * 0.4f); }
vec3 modelTailTip(const ModelDef& m) { return vec3(0, m.st[7][3], m.st[7][0] + 0.03f); }
