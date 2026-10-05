// Solace Express - hand-built aircraft geometry. Dimensions are metres in body coordinates (+z aft).
#include "models.h"
#include "aircraft.h"

// clang-format off
const ModelDef kModels[] = {
  // ---------------------------------------------------------------- Kestrel T2 (high-wing two-seat trainer)
  { {{-3.65f,0.100f,0.090f,-0.020f},{-3.45f,0.350f,0.340f,-0.040f},{-2.55f,0.480f,0.425f,-0.025f},{-1.70f,0.550f,0.615f,0.115f},{-0.45f,0.550f,0.610f,0.130f},{1.00f,0.380f,0.410f,0.190f},{2.90f,0.150f,0.180f,0.320f},{3.65f,0.060f,0.080f,0.380f}}, 0.62f,
    {5.05f,1.62f,1.10f,.25f,.74f,-1.72f,1.5f,.13f}, 1,2.4f,0,.55f, 0,0,
    {1.65f,.95f,.70f,.15f,.30f,2.60f,0}, 0,
    {1.25f,1.15f,.80f,.36f,.25f,2.32f},
    0, 0,0,0,0,0, .13f,.87f,
    0, .24f, 0,
    0, 0,0,0,0,0,
    vec3(-.28f,.50f,-1.15f), 0, -2.35f,-1.55f,.34f,-0.40f },
  // ---------------------------------------------------------------- Wren 180 (four-seat tourer)
  // Cantilever touring wing, fuller rear cabin and taller tapered fin distinguish it from the strut-braced trainer.
  { {{-4.15f,0.100f,0.090f,-0.020f},{-3.92f,0.380f,0.350f,-0.050f},{-2.80f,0.520f,0.445f,-0.035f},{-1.80f,0.600f,0.645f,0.095f},{-0.20f,0.620f,0.645f,0.115f},{1.60f,0.450f,0.420f,0.160f},{3.40f,0.150f,0.190f,0.290f},{4.15f,0.060f,0.090f,0.350f}}, 0.86f,
    {5.50f,1.62f,1.00f,.55f,.80f,-1.85f,1.7f,.13f}, 0,0,0,.55f, 0,0,
    {1.75f,1.05f,.72f,.18f,.32f,3.00f,0}, 0,
    {1.62f,1.45f,.50f,1.02f,.27f,2.60f},
    0, 0,0,0,0,0, .14f,.95f,
    1, .26f, 0,
    1, .50f,1.72f,.22f,.27f,.20f,
    vec3(-.30f,.54f,-1.30f), 0, -2.60f,-1.75f,.36f,0.45f },
  // ---------------------------------------------------------------- Bushmaster STOL (taildragger, tundra tyres, slats)
  { {{-4.00f,0.110f,0.100f,-0.020f},{-3.75f,0.400f,0.390f,-0.030f},{-2.75f,0.500f,0.490f,-0.010f},{-1.80f,0.560f,0.680f,0.100f},{-0.40f,0.560f,0.665f,0.115f},{1.00f,0.380f,0.440f,0.180f},{3.30f,0.130f,0.170f,0.330f},{4.00f,0.060f,0.085f,0.385f}}, 0.50f,
    {6.20f,1.80f,1.70f,.05f,.82f,-1.95f,1.0f,.15f}, 1,2.9f,0,.60f, 1,0,
    {1.95f,1.10f,.80f,.15f,.35f,2.85f,0}, 0,
    {1.40f,1.30f,.70f,.80f,.28f,2.65f},
    0, 0,0,0,0,0, .16f,1.00f,
    2, .38f, 0,
    0, 0,0,0,0,0,
    vec3(-.28f,.56f,-1.25f), 0, -2.70f,-1.80f,.38f,0.30f },
  // ---------------------------------------------------------------- Islander Twin (boxy high-wing twin)
  { {{-5.48f,.03f,.03f,-.07f},{-5.20f,.40f,.40f,-.08f},{-4.40f,.60f,.60f,-.02f},{-3.50f,.72f,.82f,.04f},{-0.50f,.72f,.82f,.04f},{1.80f,.50f,.62f,.12f},{4.60f,.15f,.30f,.40f},{5.45f,.06f,.14f,.45f}}, .35f,
    {7.45f,2.05f,2.05f,0,.95f,-2.40f,1.0f,.14f}, 0,0,0,.60f, 0,0,
    {2.30f,1.30f,1.05f,.10f,.45f,3.90f,0}, 0,
    {1.90f,1.70f,1.00f,1.00f,.35f,3.50f},
    2, 2.38f,.78f,.38f,-3.35f,2.85f, .14f,1.00f,
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
  { {{-13.05f,.03f,.03f,-.32f},{-12.6f,.60f,.62f,-.20f},{-11.5f,1.05f,1.05f,-.05f},{-9.8f,1.32f,1.38f,0},{8.0f,1.32f,1.38f,0},{10.5f,1.0f,1.10f,.25f},{12.4f,.45f,.60f,.70f},{13.0f,.20f,.30f,.85f}}, 1.0f,
    {13.70f,2.90f,1.45f,.40f,1.20f,-1.50f,2.0f,.15f}, 0,0,0,.55f, 0,1,
    {3.90f,2.10f,1.30f,.90f,0,0,0}, 1,
    {4.00f,3.60f,2.30f,2.60f,.90f,8.80f},
    3, 3.56f,.90f,.80f,-3.90f,5.6f, .34f,2.00f,
    3, .45f, 0,
    18, -8.5f,7.5f,.25f,.12f,.17f,
    vec3(-.55f,.55f,-10.60f), 2, -11.90f,-10.90f,.35f,-10.2f },
  // ---------------------------------------------------------------- Starling 500 (low-wing T-tail business jet)
  { {{-7.00f,.05f,.05f,-.15f},{-6.60f,.32f,.30f,-.15f},{-5.60f,.65f,.60f,-.05f},{-4.60f,.92f,.95f,.05f},{2.0f,.92f,.95f,.05f},{4.50f,.80f,.85f,.15f},{6.40f,.38f,.48f,.40f},{7.00f,.12f,.22f,.50f}}, 1.0f,
    {7.95f,2.50f,1.00f,1.40f,-.55f,-0.90f,4.0f,.12f}, 0,0,.80f,.55f, 0,0,
    {2.90f,1.50f,.80f,.90f,0,0,0}, 1,
    {2.40f,2.40f,1.30f,1.90f,.60f,4.40f},
    4, 1.50f,.45f,.48f,2.24f,2.52f, 0,0,
    4, .33f, 0,
    6, -3.8f,1.2f,.20f,.17f,.21f,
    vec3(-.42f,.40f,-4.75f), 2, -5.95f,-4.95f,.22f,-4.4f },
  // ---------------------------------------------------------------- Swift S6 (low-wing retractable tourer)
  { // Eight closed nose-to-tail fuselage stations: z, half width, half height, centre y.
  {{-4.35f,.10f,.09f,-.055f},{-4.10f,.35f,.31f,-.060f},
  {-2.95f,.49f,.41f,-.035f},{-1.90f,.62f,.66f,.075f},
  {-.25f,.63f,.65f,.105f},{1.55f,.43f,.43f,.160f},
  {3.70f,.14f,.18f,.270f},{4.25f,.055f,.075f,.315f}}, .94f, // elliptical cabin
  {5.70f,1.98f,1.02f,.72f,-.74f,-1.13f,5.0f,.125f}, // low tapered wing; spar below floor
  0,0.0f,.34f,.57f,0,0,                               // no struts; winglets; 57% flaps
  {1.90f,1.08f,.62f,.40f,.28f,3.06f,0.0f},0,           // conventional horizontal tail
  {1.55f,1.55f,.55f,.76f,.23f,2.70f},                  // fin and rudder
  0,0.0f,0.0f,0.0f,0.0f,0.0f,.13f,1.03f,             // nose piston; spinner and prop
  4,.28f,0,                                         // wing/body retracts; no cargo pod
  1,.35f,1.45f,.24f,.28f,.205f,                       // one passenger window per side
  vec3(-.32f,.52f,-1.40f),0,-2.80f,-2.14f,.31f,.50f }, // left-seat analog; windshield ahead of panel
  // ---------------------------------------------------------------- Osprey C6 (six-seat piston twin)
  { // Fuselage stations: pointed luggage nose, six-seat cabin, tapered tailcone.
  {{-4.90f,0.06f,0.06f,-0.06f},{-4.45f,0.37f,0.35f,-0.04f},{-3.10f,0.63f,0.67f,0.03f},{-2.10f,0.72f,0.90f,0.09f},
  {0.55f,0.72f,0.90f,0.09f},{2.15f,0.49f,0.58f,0.16f},{4.25f,0.16f,0.24f,0.30f},{4.90f,0.06f,0.08f,0.34f}}, 0.72f,
  {6.20f,2.25f,1.35f,0.55f,-0.68f,-0.80f,4.0f,0.13f},
  0,0.0f,0.0f,0.59f,0,1, // no struts/winglets/slats; flaps to 59%; de-ice boots
  {2.15f,1.35f,0.85f,0.28f,0.34f,3.15f,0.0f},0, // conventional horizontal tail
  {1.85f,1.65f,0.65f,0.85f,0.32f,2.80f}, // tapered swept fin
  2,2.30f,-0.54f,0.40f,-1.50f,2.60f,0.16f,1.05f, // twin three-blade piston nacelles
  4,0.29f,0, // retract into low wing/body; no cargo pod
  2,-0.55f,1.45f,0.25f,0.27f,0.22f, // two large cabin windows per side
  vec3(-0.34f,0.74f,-1.90f),1, // left pilot eye; analog twin cockpit
  -3.05f,-2.62f,0.40f,-0.65f }, // windshield and pilot side windows
  // ---------------------------------------------------------------- XR-8 Nightjar (civil twin-jet research demonstrator; generic field)
  { // Eight closed stations; enough roof height for the offset glass-cockpit eye.
  {{-8.30f,.035f,.035f,-.035f},{-6.65f,.28f,.22f,-.030f},
  {-5.05f,.56f,.49f,-.010f},{-3.55f,.77f,.79f,.045f},
  {-.30f,.93f,.80f,.075f},{3.10f,.78f,.72f,.075f},
  {6.75f,.28f,.33f,.155f},{8.00f,.065f,.090f,.225f}}, .77f,
  {8.30f,3.50f,1.10f,.08f,-.75f,-1.50f,2.0f,.11f},   // near-straight LE, mildly forward quarter-chord; 11% thick
  0,0.0f,.42f,.58f,0,0,                              // winglets; 58% flaps; no special surfaces
  {2.65f,1.55f,.80f,.48f,.83f,5.72f,0.0f},0,          // conventional raised horizontal tail
  {2.22f,2.28f,.77f,1.40f,.55f,4.85f},                // tall swept fin
  4,1.63f,.16f,.45f,2.54f,3.37f,0.0f,0.0f,            // ordinary aft twin jets and generated pylons
  4,.35f,0,                                        // wing/body retracts; no cargo pod
  0,0.0f,0.0f,0.0f,0.0f,0.0f,                       // no passenger windows
  vec3(-.42f,.59f,-3.30f),2,-5.45f,-4.23f,.30f,-2.20f }, // glass cockpit; avoid centre-display overlap using data
  // ---------------------------------------------------------------- XR-9 Specter (research jet; custom SDF in the shader, engine code 5)
  { {{-9.00f,.04f,.03f,-.05f},{-7.60f,.40f,.22f,-.02f},{-5.60f,.82f,.48f,.06f},{-3.60f,1.05f,.62f,.08f},{0.0f,1.15f,.60f,.05f},{3.50f,1.25f,.55f,0},{6.60f,1.15f,.45f,-.02f},{8.20f,.95f,.40f,-.02f}}, .45f,
    {5.60f,7.20f,1.20f,5.60f,-.20f,-1.60f,-2.0f,.045f}, 0,0,0,.82f, 0,0,
    {1.90f,1.60f,.55f,1.10f,.05f,-6.60f,0}, 0,
    {2.30f,2.60f,1.00f,1.90f,.35f,4.60f},
    5, .80f,-.10f,.55f,1.0f,7.4f, 0,0,
    4, .38f, 0,
    0, 0,0,0,0,0,
    vec3(0,.62f,-4.70f), 3, -6.0f,-4.9f,.5f,-4.0f },
  // ---------------------------------------------------------------- XR-10 Mantis (forward-swept demonstrator; its own SDF in mantis_sdf.glsl, engine code 7)
  { // eight hull proxy stations, nose to tail (the field is its own; these size the physics and the lights)
    {{-8.0f,.07f,.07f,.05f},{-6.3f,.44f,.44f,.05f},{-4.8f,.79f,.79f,.05f},{-3.9f,.98f,.98f,.05f},
     {-.3f,1.02f,1.02f,.04f},{2.65f,1.05f,1.05f,.04f},{6.1f,.40f,.40f,.20f},{8.0f,.07f,.07f,.28f}}, 1.0f,
    {6.6f,3.15f,1.35f,-2.48f,-.62f,.65f,0.0f,.10f},   // forward-swept wing proxy
    0,0.0f,0.0f,.52f,0,0,
    {2.75f,1.18f,1.18f,0.0f,-.15f,-4.19f,0.0f},0,      // the canards stand in for the tail proxy
    {2.3f,1.84f,1.10f,.80f,.20f,4.30f},
    7,1.55f,.05f,.48f,1.73f,3.07f,0.0f,0.0f,          // custom field; conventional twin-jet physics
    4,.26f,0,
    0,0.0f,0.0f,0.0f,0.0f,0.0f,
    vec3(0.0f,.65f,-3.35f),2,                          // sealed camera cockpit
    -4.95f,-4.45f,.45f,-2.7f },
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

// monotone cubic through the stations (mirrors fusSection in shaders.h)
static float fbSlope(float d0, float d1, float h0, float h1) {
  return d0 * d1 <= 0.f ? 0.f : 3.f * (h0 + h1) / ((2.f * h1 + h0) / d0 + (h1 + 2.f * h0) / d1);
}
static void stationAt(const ModelDef& m, float z, float& hw, float& hh, float& cy) {
  z = clampf(z, m.st[0][0], m.st[7][0]);
  int i = 0;
  for (int k = 0; k < 7; k++) { i = k; if (z <= m.st[k + 1][0]) break; }
  const float* a = m.st[i]; const float* b = m.st[i + 1];
  float h = std::max(b[0] - a[0], 1e-3f), t = clampf((z - a[0]) / h, 0.f, 1.f), t2 = t * t, t3 = t2 * t;
  float out[3];
  for (int c = 1; c <= 3; c++) {
    float dd = (b[c] - a[c]) / h, ma = dd * 0.5f, mb = dd * 0.5f;
    if (i > 0) { const float* p = m.st[i - 1]; float h0 = std::max(a[0] - p[0], 1e-3f); ma = fbSlope((a[c] - p[c]) / h0, dd, h0, h); }
    if (i < 6) { const float* n = m.st[i + 2]; float h1 = std::max(n[0] - b[0], 1e-3f); mb = fbSlope(dd, (n[c] - b[c]) / h1, h, h1); }
    out[c - 1] = a[c] * (2 * t3 - 3 * t2 + 1) + ma * h * (t3 - 2 * t2 + t) + b[c] * (-2 * t3 + 3 * t2) + mb * h * (t3 - t2);
  }
  hw = out[0]; hh = out[1]; cy = out[2];
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
  // main wheels: nacelle-retracting gear sits under the rear of the engine nacelle (it folds up into it)
  float mz = m.gear == 3 ? m.nacZ0 + m.nacLen * 0.6f : s.taildragger ? -0.10f * L : 0.04f * L;
  // retracting nose gear: behind the nose taper, where the full section begins, so its bay lies flush on the belly
  float nz = m.gear >= 3 ? std::max(-0.36f * L, m.st[3][0] + 0.35f) : -0.36f * L;
  put(18, track, m.wheelR, mz, nz);
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
