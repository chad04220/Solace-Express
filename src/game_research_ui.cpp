// Solace Express - the research programme terminal (Project NIGHTGLASS): a biometric access sequence, then the
// airframe selection with a live ray-traced 3D preview of the chosen craft, its decrypted specification and
// manoeuvre envelope, and the sortie parameters.
#include "game.h"
#include "models.h"

namespace {
const vec3 R_ICE(0.42f, 0.93f, 1.f), R_DIM(0.36f, 0.55f, 0.66f), R_TEXT(0.88f, 0.97f, 1.f), R_RED(1.f, 0.27f, 0.24f);
const vec3 R_AMBER(1.f, 0.72f, 0.26f), R_GREEN(0.4f, 1.f, 0.62f), R_VIOLET(0.8f, 0.5f, 1.f), R_INK(0.0f, 0.015f, 0.03f);
const vec3 R_TEAL(0.3f, 0.95f, 0.8f);
const char* kGlyphs = "0123456789ABCDEF#%&@$*+=<>/\\|";

// the airframes on the register, in the order of their cards (the XR number order); every craft-specific piece of
// the terminal reads from here, so a new research craft is one more row
struct SpecRow { const char* k; const char* v; float bar; };
struct ResCraftInfo {
  int idx;                 // index into kAircraft
  const char* code;        // the programme code on the card
  const char* num;         // "XR-9"
  const char* name;        // "SPECTER"
  vec3 colour;             // the terminal's accent while it is selected
  float bars[3];           // velocity, agility, signature (0..1)
  const char* title;       // the decrypted specification title
  SpecRow rows[8];
  float nTop, nBot, nStep; // the envelope chart's scale
  float nMax, nMin, vMax, ms;   // the envelope: load limits, top Mach, the Mach of the 1 g stall
  const char* notes[5];    // handling directives
  const char* contractId;
};
const ResCraftInfo kResCraft[] = {
  {kNightjar, "NG-XR8-N  //  BLK 0", "XR-8", "NIGHTJAR", R_TEAL, {0.3f, 0.35f, 0.92f}, "CONVENTIONAL TWIN-JET DEMONSTRATOR",
   {{"CONFIGURATION", "Tapered low wing, conventional tail", -1}, {"PROPULSION", "2 x turbofan, 25 kN total", 0.08f},
    {"THRUST / WEIGHT", "0.45 : 1", 0.1f}, {"TOP SPEED", "Mach 0.8", 0.22f},
    {"VECTORING", "None - conventional controls", 0.f}, {"ROLL RATE", "120 deg/s", 0.3f},
    {"AIRFRAME", "+6 / -3 g", 0.07f}, {"ENDURANCE", "1,300 kg fuel, 1,250 km", 0.3f}},
   10.f, -5.f, 5.f, 6.f, -3.f, 0.9f, 0.25f,
   {"Conventional controls: no FBW, no vectoring", "Flaps and gear as any jet: ~150 kt over the fence", "Fuel burns: a 1,300 kg cell, plan the sortie",
    "Long hard runways only", "The programme's flying testbed for sensors and skins"}, "XR8"},
  {kResearchJet, "NG-XR9-S  //  BLK 3", "XR-9", "SPECTER", R_ICE, {0.7f, 0.72f, 0.45f}, "HYPERSONIC-CAPABLE RESEARCH MODEL",
   {{"CONFIGURATION", "Lifting body, cranked delta, canards", -1}, {"PROPULSION", "2 x turbofan, 472 kN full reheat", 0.72f},
    {"THRUST / WEIGHT", "4.4 : 1 with reheat", 0.66f}, {"TOP SPEED", "Mach 2.5+", 0.69f},
    {"VECTORING", "2D nozzles, +-29 deg pitch", 0.4f}, {"ROLL RATE", "315 deg/s", 0.78f},
    {"AIRFRAME", "+50 / -25 g", 0.55f}, {"ENDURANCE", "Unrestricted research cell", 1.f}},
   60.f, -30.f, 30.f, 50.f, -25.f, 2.5f, 0.42f,
   {"No flaps: land fast, ~140 kt, long runways", "Nozzles vector with the stick for pitch", "FBW commands rotation - no g limiter",
    "Reheat lights above 85% throttle (2x thrust)", "C: cockpit view flies on the displays"}, "XR9"},
  {kWraith, "NG-XR11-W  //  BLK 1", "XR-11", "WRAITH", R_VIOLET, {1.f, 0.97f, 0.06f}, "STEALTH AEROBATIC RESEARCH MODEL",
   {{"CONFIGURATION", "Faceted body, diamond wing, V-tail", -1}, {"PROPULSION", "4 x tilting pods, 520 kN boosted", 0.8f},
    {"THRUST / WEIGHT", "2.2 dry, 4.6 boosted", 0.7f}, {"TOP SPEED", "Mach 3.6+", 1.f},
    {"VTOL", "Pods tilt 0 - 90 deg, vanes", 1.f}, {"ROLL RATE", "400 deg/s", 1.f},
    {"AIRFRAME", "+90 / -45 g", 1.f}, {"SIGNATURE", "Active refractive cloak", 0.06f}},
   100.f, -50.f, 50.f, 90.f, -45.f, 3.6f, 0.32f,
   {"F / V tilt the pods: 0 forward, 90 hover", "X or double-tap brake: cloak", "Y weapons hot: lasers LMB/Enter, bomb Bksp",
    "Slow on low power the pods do the flying", "Airframe holds +90 / -45 g"}, "XR11"},
};
const int kNumResCraft = sizeof(kResCraft) / sizeof(kResCraft[0]);
int resCraftSlot(int idx) { for (int k = 0; k < kNumResCraft; k++) if (kResCraft[k].idx == idx) return k; return 1; }

uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float rnd01(uint32_t i) { return (hsh(i) & 0xffffff) / 16777216.f; }

// a string that decrypts: each character flickers through glyphs until its turn, left to right
std::string decrypt(const std::string& s, float t, float perChar = 0.018f) {
  std::string o = s;
  int frame = (int)(t * 30.f);
  for (size_t i = 0; i < o.size(); i++) {
    if (o[i] == ' ' || t > 0.12f + i * perChar) continue;
    o[i] = t < 0.f ? ' ' : kGlyphs[hsh((uint32_t)(i * 977 + frame * 131)) % 29];
  }
  return o;
}
std::string ellipsize(const std::string& str, float width, float size) {
  if (g_ren.textWidth(str, size) <= width) return str;
  std::string t = str;
  while (!t.empty() && g_ren.textWidth(t + "...", size) > width) t.pop_back();
  return t + "...";
}
std::string hexWord(uint32_t v, int n) { static const char* d = "0123456789ABCDEF"; std::string o; for (int i = n - 1; i >= 0; i--) o += d[(v >> (i * 4)) & 15]; return o; }

void ring(float cx, float cy, float r, float th, vec3 c, float a, int seg = 72, float a0 = 0.f, float a1 = 6.2832f) {
  float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
  for (int i = 1; i <= seg; i++) {
    float t = a0 + (a1 - a0) * i / seg, x = cx + cosf(t) * r, y = cy + sinf(t) * r;
    g_ren.line(px, py, x, y, th, c, a); px = x; py = y;
  }
}
// a ring broken into n dashes, turned by rot
void dashRing(float cx, float cy, float r, float th, vec3 c, float a, int n, float fill, float rot) {
  for (int i = 0; i < n; i++) { float a0 = rot + i * 6.2832f / n; ring(cx, cy, r, th, c, a, 6, a0, a0 + 6.2832f / n * fill); }
}
void corners(float x, float y, float w, float h, float L, float t, vec3 c, float a) {
  g_ren.rect(x, y, L, t, c, a); g_ren.rect(x, y, t, L, c, a);
  g_ren.rect(x + w - L, y, L, t, c, a); g_ren.rect(x + w - t, y, t, L, c, a);
  g_ren.rect(x, y + h - t, L, t, c, a); g_ren.rect(x, y + h - L, t, L, c, a);
  g_ren.rect(x + w - L, y + h - t, L, t, c, a); g_ren.rect(x + w - t, y + h - L, t, L, c, a);
}
// a panel with a cut corner, a hairline frame and a title tab
void slab(float x, float y, float w, float h, float s, vec3 acc, float a) {
  g_ren.rectGrad(x, y, w, h, vec3(0.0f, 0.03f, 0.05f), vec3(0.0f, 0.01f, 0.02f), 0.78f * a);
  g_ren.rectOutline(x, y, w, h, acc, 0.22f * a, 0, 1 * s);
  corners(x - 1 * s, y - 1 * s, w + 2 * s, h + 2 * s, 12 * s, 2 * s, acc, 0.85f * a);
  for (int i = 0; i < 3; i++) g_ren.rect(x + w - (14 + i * 7) * s, y + 5 * s, 4 * s, 2 * s, acc, 0.6f * a);
}
void tag(float x, float y, float s, const std::string& t, vec3 c, float a) {   // ▌LABEL with a trailing rule
  g_ren.rect(x, y + 2 * s, 3 * s, 11 * s, c, a);
  float tw = g_ren.text(x + 9 * s, y, 11.5f * s, t, c, a, 0, false);
  (void)tw;
}

// top-view silhouettes, x right / y aft, unit size
void silhouette(int craft, float cx, float cy, float sc, vec3 c, float a, float th) {
  auto mirror = [&](const float* p, int n) {
    for (int side = -1; side <= 1; side += 2)
      for (int i = 0; i + 1 < n; i++)
        g_ren.line(cx + side * p[i * 2] * sc, cy + p[i * 2 + 1] * sc, cx + side * p[i * 2 + 2] * sc, cy + p[i * 2 + 3] * sc, th, c, a);
  };
  if (craft == kWraith) {
    static const float body[] = {0.f, -1.f, 0.22f, -0.55f, 0.95f, 0.18f, 0.9f, 0.3f, 0.3f, 0.62f, 0.42f, 0.95f, 0.18f, 0.88f, 0.f, 0.8f};
    mirror(body, 8);
    for (int i = 0; i < 4; i++) {   // the four pods
      float px = kWraithPods[i].x / 6.4f, py = kWraithPods[i].z / 8.2f;
      g_ren.rectOutline(cx + px * sc - 0.07f * sc, cy + py * sc - 0.14f * sc, 0.14f * sc, 0.28f * sc, c, a, 0.06f * sc, th);
    }
  } else if (craft == kResearchJet) {
    static const float body[] = {0.f, -1.f, 0.1f, -0.7f, 0.16f, -0.35f, 0.42f, -0.42f, 0.2f, -0.18f, 0.5f, 0.28f, 0.98f, 0.52f, 0.92f, 0.66f, 0.3f, 0.7f, 0.2f, 0.92f, 0.f, 0.92f};
    mirror(body, 11);
    g_ren.line(cx - 0.08f * sc, cy + 0.92f * sc, cx - 0.08f * sc, cy + 0.98f * sc, th, c, a);
    g_ren.line(cx + 0.08f * sc, cy + 0.92f * sc, cx + 0.08f * sc, cy + 0.98f * sc, th, c, a);
  } else {   // a conventional twin jet (the XR-8): tapered wing, tailplane, two aft engines
    static const float body[] = {0.f, -1.f, 0.11f, -0.72f, 0.13f, -0.18f, 0.95f, 0.34f, 0.93f, 0.46f, 0.15f, 0.42f, 0.14f, 0.72f, 0.44f, 0.86f, 0.42f, 0.94f, 0.1f, 0.92f, 0.f, 0.96f};
    mirror(body, 11);
    for (int side = -1; side <= 1; side += 2)
      g_ren.rectOutline(cx + side * 0.2f * sc - 0.045f * sc, cy + 0.5f * sc, 0.09f * sc, 0.24f * sc, c, a, 0.04f * sc, th);
    g_ren.line(cx, cy + 0.62f * sc, cx, cy + 0.98f * sc, th, c, a);
  }
}
}

Game::ResLayout Game::researchLayout() const {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  ResLayout L;
  L.lx = 24 * s; L.lw = std::min(300 * s, W * 0.25f);
  L.rw = std::min(380 * s, W * 0.31f); L.rx = W - 24 * s - L.rw;
  L.top = 62 * s; L.bot = H - 132 * s;
  L.px0 = L.lx + L.lw + 18 * s; L.px1 = L.rx - 18 * s;
  L.cx = 0.5f * (L.px0 + L.px1); L.cy = 0.5f * (L.top + L.bot);
  L.r = std::max(60 * s, 0.45f * std::min(L.px1 - L.px0, L.bot - L.top));
  return L;
}

void Game::drawResearch(const FrameParams& fp) {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float kIntro = 4.9f;
  if (resAuthed && realTime - resOpened < kIntro - 0.6f) resOpened = realTime - (kIntro - 0.6f);   // returning: just the resume flash
  float T = realTime - resOpened;
  bool wr = resCraft == kWraith;
  const ResCraftInfo& RC = kResCraft[resCraftSlot(resCraft)];
  vec3 ACC = RC.colour;
  // ------------------------------------------------------------------ biometric access sequence
  if (T < kIntro) {
    bool skip = in.mPressed[0] || in.pressed[K_ENTER] || in.pressed[' '] || in.pressed[K_ESC];
    if (skip && T > 0.3f) { resOpened = realTime - kIntro; in.mPressed[0] = false; in.pressed[K_ENTER] = in.pressed[K_ESC] = false; resAuthed = true; return; }
    // stage cues
    static float lastT = 99.f;
    auto cue = [&](float at, int sfx, float v) { if (lastT < at && T >= at) g_audio.trigger(sfx, v); };
    cue(0.35f, SFX_BEEP, 0.4f); cue(1.75f, SFX_BEEP, 0.5f); cue(2.9f, SFX_BEEP, 0.5f); cue(3.75f, SFX_BEEP, 0.5f); cue(4.05f, SFX_CHIME, 0.9f);
    lastT = T;
    float fadeOut = smoothstepf(4.35f, kIntro, T);
    g_ren.rect(0, 0, W, H, vec3(0, 0.004f, 0.01f), 0.96f * (1.f - fadeOut));
    float A = 1.f - fadeOut;
    // scanlines and a faint grid
    for (float y = 0; y < H; y += 3 * s) g_ren.rect(0, y, W, 1, R_ICE, 0.025f * A);
    for (float x = fmodf(T * 20.f * s, 48 * s); x < W; x += 48 * s) g_ren.rect(x, 0, 1, H, R_ICE, 0.03f * A);
    // header terminal
    const char* boot[] = {"NIGHTGLASS SECURE TERMINAL  v9.4.1  //  QUANTUM LINK ESTABLISHED", "SUBJECT PRESENT  //  BEGIN MULTI-FACTOR BIOMETRIC VERIFICATION"};
    for (int i = 0; i < 2; i++) {
      float st = i * 0.18f; if (T < st) break;
      std::string l = boot[i]; l = l.substr(0, std::min(l.size(), (size_t)((T - st) * 90.f)));
      g_ren.text(40 * s, (30 + i * 20) * s, 13 * s, l, i ? R_DIM : R_ICE, A, 0, false);
    }
    g_ren.text(W - 40 * s, 30 * s, 13 * s, "SESSION " + hexWord(hsh((uint32_t)resOpened * 7 + 3), 8), R_DIM, A, 2, false);
    g_ren.text(W - 40 * s, 50 * s, 13 * s, "CLEARANCE REQUIRED: OMEGA-BLACK", R_RED, A * (fmodf(T, 0.8f) < 0.55f ? 1.f : 0.4f), 2, false);
    float pw = std::min(360 * s, (W - 120 * s) / 3.f), ph = std::min(400 * s, H - 230 * s), py0 = 100 * s;
    float gap = (W - 3 * pw) / 4.f;
    // ---- 1: fingerprint
    auto stagePanel = [&](int k, const char* title, float t0, float t1, bool ok) {
      float x = gap + k * (pw + gap);
      float ap = A * smoothstepf(t0 - 0.2f, t0, T);
      slab(x, py0, pw, ph, s, ok ? R_GREEN : R_ICE, ap);
      tag(x + 14 * s, py0 + 12 * s, s, title, ok ? R_GREEN : R_ICE, ap);
      g_ren.text(x + pw - 14 * s, py0 + 12 * s, 11.5f * s, ok ? "VERIFIED" : T >= t0 ? "SCANNING" : "STANDBY", ok ? R_GREEN : T >= t0 ? R_AMBER : R_DIM, ap * (ok || fmodf(T, 0.5f) < 0.33f ? 1.f : 0.3f), 2, false);
      float prog = clampf((T - t0) / (t1 - t0), 0, 1);
      g_ren.rect(x + 14 * s, py0 + ph - 18 * s, pw - 28 * s, 3 * s, R_ICE, 0.12f * ap);
      g_ren.rect(x + 14 * s, py0 + ph - 18 * s, (pw - 28 * s) * prog, 3 * s, ok ? R_GREEN : R_ICE, 0.9f * ap);
      return x;
    };
    {
      const float t0 = 0.35f, t1 = 1.75f; bool ok = T >= t1;
      float x = stagePanel(0, "DERMAL RIDGE SCAN", t0, t1, ok), ap = A * smoothstepf(t0 - 0.2f, t0, T);
      float cx = x + pw * 0.5f, cy = py0 + ph * 0.42f, rx = pw * 0.26f, ry = ph * 0.3f;
      float bar = cy - ry + 2.f * ry * clampf((T - t0) / (t1 - t0 - 0.2f), 0, 1);
      // ridges: distorted loops clipped to the fingertip's oval, bright once the scan bar has passed them
      for (int k = 1; k < 22; k++) {
        float rk = k / 22.f;
        float px = 0, py = 0; bool have = false;
        for (int i = 0; i <= 64; i++) {
          float th = i / 64.f * 6.2832f;
          float r = rk * (1.f + 0.07f * sinf(3 * th + k * 0.37f) + 0.04f * sinf(5 * th + 1.3f));
          float x2 = cx + cosf(th) * r * rx * 1.25f, y2 = cy + 0.15f * ry + sinf(th) * r * ry * (th > 3.1416f ? 1.35f : 0.9f);
          float ex = (x2 - cx) / rx, ey = (y2 - cy) / ry;
          bool in = ex * ex + ey * ey < 1.f;
          if (in && have) { bool lit = y2 < bar; g_ren.line(px, py, x2, y2, 1.4f * s, lit ? (ok ? R_GREEN : R_ICE) : R_DIM, ap * (lit ? 0.85f : 0.22f)); }
          px = x2; py = y2; have = in;
        }
      }
      if (!ok) { g_ren.rect(cx - rx - 10 * s, bar - 1 * s, 2 * rx + 20 * s, 2 * s, R_ICE, ap); g_ren.glow(cx - rx - 10 * s, bar - 3 * s, 2 * rx + 20 * s, 6 * s, R_ICE, 0.5f * ap, 3 * s, 10 * s); }
      // minutiae and their constellation
      float lx2 = 0, ly2 = 0;
      for (int m = 0; m < 11; m++) {
        float ma = rnd01(m * 3 + 1) * 6.2832f, mr = 0.15f + 0.7f * rnd01(m * 3 + 2);   // inside the print
        float mx = cx + cosf(ma) * mr * rx, my = cy + sinf(ma) * mr * ry;
        if (my > bar) continue;
        g_ren.rectOutline(mx - 4 * s, my - 4 * s, 8 * s, 8 * s, R_AMBER, ap, 0, 1.2f * s);
        if (m) g_ren.line(lx2, ly2, mx, my, 1 * s, R_AMBER, 0.35f * ap);
        lx2 = mx; ly2 = my;
      }
      float tyy = py0 + ph * 0.8f;
      int mcount = std::min(23, (int)((T - t0) / (t1 - t0) * 23.f));
      g_ren.text(x + 16 * s, tyy, 12 * s, fmt("MINUTIAE      %02d / 23", std::max(0, mcount)), R_DIM, ap, 0, false);
      g_ren.text(x + 16 * s, tyy + 18 * s, 12 * s, ok ? "RIDGE MATCH   99.981 %" : fmt("RIDGE MATCH   %06.3f %%", 40.f + 59.f * clampf((T - t0) / (t1 - t0), 0, 1) + rnd01((uint32_t)(T * 30)) * 0.9f), ok ? R_GREEN : R_ICE, ap, 0, false);
    }
    // ---- 2: retina
    {
      const float t0 = 1.35f, t1 = 2.9f; bool ok = T >= t1;
      float x = stagePanel(1, "RETINAL VASCULATURE", t0, t1, ok), ap = A * smoothstepf(t0 - 0.2f, t0, T);
      float cx = x + pw * 0.5f, cy = py0 + ph * 0.43f, R = std::min(pw, ph) * 0.3f;
      float p = clampf((T - t0) / (t1 - t0), 0, 1);
      float pr = R * (0.36f - 0.12f * smoothstepf(0.2f, 0.6f, p));   // the pupil contracts in the scan light
      vec3 ic = ok ? R_GREEN : R_ICE;
      for (int i = 0; i < 140; i++) {   // iris fibres
        float th = i / 140.f * 6.2832f + rnd01(i) * 0.03f, r1 = R * (0.92f + 0.08f * rnd01(i + 300));
        g_ren.line(cx + cosf(th) * pr, cy + sinf(th) * pr, cx + cosf(th) * r1, cy + sinf(th) * r1, 1 * s, ic, ap * (0.15f + 0.35f * rnd01(i + 77)));
      }
      ring(cx, cy, R, 1.5f * s, ic, 0.8f * ap);
      {   // vessels: branching random walks out from the optic disc, kept inside the retina
        float dx0 = cx + R * 0.45f, dy0 = cy - R * 0.1f;
        ring(dx0, dy0, R * 0.1f, 1.2f * s, R_AMBER, 0.6f * ap, 24);
        for (int v = 0; v < 7; v++) {
          float ang = v * 0.9f + 1.8f, x0 = dx0, y0 = dy0;
          for (int st = 0; st < 10; st++) {
            ang += (rnd01(v * 40 + st) - 0.5f) * 0.8f;
            float step = R * 0.12f, x1 = x0 + cosf(ang) * step, y1 = y0 + sinf(ang) * step;
            float rr = sqrtf((x1 - cx) * (x1 - cx) + (y1 - cy) * (y1 - cy));
            if (rr > R * 0.95f) break;
            if (st / 10.f < p * 1.4f) g_ren.line(x0, y0, x1, y1, (2.2f - st * 0.15f) * s, R_RED, 0.75f * ap);
            x0 = x1; y0 = y1;
          }
        }
      }
      g_ren.rect(cx - pr, cy - pr, 2 * pr, 2 * pr, R_INK, ap, pr);   // the pupil over the vessels
      ring(cx, cy, pr, 1.5f * s, ic, ap);
      dashRing(cx, cy, R * 1.22f, 2 * s, ic, 0.7f * ap, 24, 0.55f, T * 0.9f);
      dashRing(cx, cy, R * 1.36f, 1.2f * s, ic, 0.4f * ap, 60, 0.3f, -T * 0.5f);
      float sw = T * 4.f;   // radar sweep
      if (!ok) for (int k = 0; k < 10; k++) g_ren.line(cx, cy, cx + cosf(sw - k * 0.05f) * R * 1.3f, cy + sinf(sw - k * 0.05f) * R * 1.3f, 2 * s, R_ICE, ap * (0.6f - k * 0.06f));
      g_ren.line(cx - R * 1.5f, cy, cx - R * 1.15f, cy, 1 * s, ic, ap); g_ren.line(cx + R * 1.15f, cy, cx + R * 1.5f, cy, 1 * s, ic, ap);
      g_ren.line(cx, cy - R * 1.5f, cx, cy - R * 1.15f, 1 * s, ic, ap); g_ren.line(cx, cy + R * 1.15f, cx, cy + R * 1.5f, 1 * s, ic, ap);
      float tyy = py0 + ph * 0.8f;
      std::string hsx; for (int i = 0; i < 3; i++) hsx += hexWord(hsh(i * 91 + (ok ? 7 : (int)(T * 20))), 4) + " ";
      g_ren.text(x + 16 * s, tyy, 12 * s, "IRIS CODE     " + hsx, ok ? R_GREEN : R_DIM, ap, 0, false);
      g_ren.text(x + 16 * s, tyy + 18 * s, 12 * s, ok ? "HAMMING DIST  0.0031   MATCH" : fmt("HAMMING DIST  %.4f", 0.5f - 0.49f * p), ok ? R_GREEN : R_ICE, ap, 0, false);
    }
    // ---- 3: neural signature
    {
      const float t0 = 2.5f, t1 = 3.75f; bool ok = T >= t1;
      float x = stagePanel(2, "CORTICAL RESONANCE", t0, t1, ok), ap = A * smoothstepf(t0 - 0.2f, t0, T);
      float p = clampf((T - t0) / (t1 - t0), 0, 1);
      float gx = x + 16 * s, gw = pw - 32 * s;
      for (int tr = 0; tr < 4; tr++) {   // EEG traces converging on the stored signature
        float by = py0 + (70 + tr * 58) * s;
        g_ren.rect(gx, by, gw, 1, R_DIM, 0.25f * ap);
        float lxp = gx, lyp = by;
        for (int i = 1; i <= 90; i++) {
          float u = i / 90.f, ph2 = u * 18.f + T * (3.f + tr), noise = (rnd01(i * 7 + tr * 1000 + (int)(T * 12)) - 0.5f) * (1.f - p);
          float y = by + (sinf(ph2 * (1 + tr * 0.4f)) * 0.5f + sinf(ph2 * 2.7f + tr) * 0.3f + noise * 1.2f) * 12 * s;
          float xx = gx + u * gw;
          g_ren.line(lxp, lyp, xx, y, 1.3f * s, ok ? R_GREEN : (tr == 3 ? R_AMBER : R_ICE), ap * 0.85f);
          lxp = xx; lyp = y;
        }
        g_ren.text(gx, by - 30 * s, 10 * s, fmt("CH-%d  %s", tr + 1, tr == 3 ? "THETA" : tr == 2 ? "ALPHA" : tr == 1 ? "BETA" : "GAMMA"), R_DIM, ap, 0, false);
      }
      float tyy = py0 + ph * 0.8f;
      g_ren.text(x + 16 * s, tyy, 12 * s, fmt("HEART RATE    %d BPM", 64 + (int)(6 * sinf(T * 2.f))), R_DIM, ap, 0, false);
      g_ren.text(x + 16 * s, tyy + 18 * s, 12 * s, ok ? "NEURAL MATCH  CONFIRMED" : fmt("COHERENCE     %.2f", 0.2f + 0.79f * p), ok ? R_GREEN : R_ICE, ap, 0, false);
    }
    // ---- grant
    if (T > 3.75f) {
      float g = smoothstepf(3.75f, 3.95f, T);
      float flash = std::max(0.f, 1.f - (T - 4.05f) * 4.f) * (T > 4.05f ? 1.f : 0.f);
      g_ren.rect(0, 0, W, H, R_GREEN, 0.12f * flash * A);
      float by = py0 + ph + 26 * s;
      g_ren.rect(0, by - 6 * s, W, 64 * s, R_INK, 0.85f * g * A);
      g_ren.rect(0, by - 6 * s, W, 1 * s, R_GREEN, 0.6f * g * A); g_ren.rect(0, by + 58 * s, W, 1 * s, R_GREEN, 0.6f * g * A);
      g_ren.text(W * 0.5f, by + 2 * s, 30 * s, decrypt("ACCESS GRANTED  //  PROJECT NIGHTGLASS", T - 3.75f, 0.006f), R_GREEN, g * A, 1, false);
      g_ren.text(W * 0.5f, by + 38 * s, 12 * s, decrypt("SUBJECT VERIFIED  -  CLEARANCE OMEGA-BLACK  -  WELCOME BACK, CAPTAIN", T - 3.8f, 0.004f), R_TEXT, g * A, 1, false);
    }
    // shutters open into the terminal
    if (T > 4.35f) { float o = smoothstepf(4.35f, kIntro, T); g_ren.rect(0, 0, W, H * 0.5f * (1 - o), R_INK, 1); g_ren.rect(0, H - H * 0.5f * (1 - o), W, H * 0.5f * (1 - o), R_INK, 1); }
    g_ren.text(W * 0.5f, H - 24 * s, 11 * s, "CLICK  /  ENTER  /  A   TO SKIP", R_DIM, 0.6f * A, 1, false);
    return;
  }
  resAuthed = true;
  float e = smoothstepf(kIntro, kIntro + 0.5f, T);
  float dt = uiDt;
  ResLayout L = researchLayout();
  // selection changes re-run the scan and the decryption
  if (resCraft != resLastCraft) { resLastCraft = resCraft; resSelT = realTime; g_audio.trigger(SFX_BEEP, 0.4f); }
  float selT = realTime - resSelT;
  auto click = [&](float x, float y, float w, float h) { bool c = hovered(x, y, w, h) && in.mPressed[0]; if (c) { in.mPressed[0] = false; g_audio.trigger(SFX_CLICK); } return c; };
  // ------------------------------------------------------------------ frame: tint, scanlines, top band
  g_ren.rectGrad(0, 0, W, H, vec3(0, 0.01f, 0.02f), vec3(0.01f, 0, 0.01f), 0.35f * e);
  g_ren.rect(0, 0, L.px0 - 6 * s, H, R_INK, 0.45f * e); g_ren.rect(L.px1 + 6 * s, 0, W - L.px1 - 6 * s, H, R_INK, 0.45f * e);
  for (float y = fmodf(realTime * 12.f, 4 * s); y < H; y += 4 * s) g_ren.rect(0, y, W, 1, R_ICE, 0.018f * e);
  g_ren.rect(0, 0, W, 46 * s, R_INK, 0.8f * e);
  g_ren.rect(0, 46 * s, W, 1 * s, ACC, 0.5f * e);
  g_ren.text(24 * s, 10 * s, 18 * s, "NIGHTGLASS", ACC, e, 0, false);
  g_ren.text(24 * s + g_ren.textWidth("NIGHTGLASS", 18 * s) + 12 * s, 15 * s, 11 * s, "ADVANCED PROJECTS DIVISION  //  SECTOR 0x3F  //  EYES ONLY", R_DIM, e, 0, false);
  {
    int t = (int)(realTime * 100) % 8640000;
    std::string clk = fmt("T+%02d:%02d:%02d.%02d", t / 360000 % 24, t / 6000 % 60, t / 100 % 60, t % 100);
    float rx3 = W - 110 * s;   // (clear of the frame-rate counter in the corner)
    g_ren.text(rx3, 9 * s, 13 * s, clk, R_TEXT, e, 2, false);
    g_ren.text(rx3, 27 * s, 10 * s, "LINK " + hexWord(hsh((uint32_t)(realTime * 4)), 6) + "  //  ENCRYPTED  //  NOT LOGGED", R_DIM, e, 2, false);
    float lx3 = rx3 - g_ren.textWidth(clk, 13 * s) - 24 * s;
    dashRing(lx3, 17 * s, 7 * s, 1.5f * s, ACC, e, 6, 0.6f, realTime * 3.f);
  }
  // ------------------------------------------------------------------ left: airframe cards
  {
    float x = L.lx, y = L.top, w = L.lw;
    tag(x, y, s, fmt("AIRFRAMES  //  %02d ON REGISTER", kNumResCraft), ACC, e); y += 22 * s;
    // the cards share the column with the programme log: full size when they fit, squeezed (fonts included) when not
    float h = clampf((L.bot - y - 12 * s * (kNumResCraft - 1) - 70 * s) / kNumResCraft, 84 * s, 150 * s), f = h / (150 * s);
    for (int k = 0; k < kNumResCraft; k++) {
      const ResCraftInfo& C = kResCraft[k];
      int craft = C.idx;
      bool sel = resCraft == craft, hov = hovered(x, y, w, h);
      vec3 c = C.colour;
      float lift = anim(0x5e10 + k, sel ? 1.f : hov ? 0.5f : 0.f, 10.f);
      g_ren.rectGrad(x, y, w, h, c * (0.05f + 0.08f * lift), vec3(0, 0.01f, 0.02f), 0.85f * e);
      g_ren.rectOutline(x, y, w, h, c, (0.18f + 0.5f * lift) * e, 0, 1 * s);
      if (sel) { corners(x - 3 * s, y - 3 * s, w + 6 * s, h + 6 * s, 14 * s, 2 * s, c, e); g_ren.glow(x, y, w, h, c, 0.12f * e, 0, 14 * s); }
      // a glint that runs across the selected card
      if (sel) { float gx = x + fmodf(realTime * 0.6f, 1.6f) * w - 0.3f * w; if (gx > x && gx < x + w - 30 * s) g_ren.rect(gx, y + 1, 30 * s, h - 2, c, 0.05f * e); }
      g_ren.text(x + 14 * s, y + 10 * f * s, 10.5f * f * s, C.code, R_DIM, e, 0, false);
      g_ren.text(x + 14 * s, y + 26 * f * s, 34 * f * s, C.num, sel ? R_TEXT : c, e, 0, false);
      g_ren.text(x + 14 * s, y + 66 * f * s, 14 * f * s, C.name, c, e, 0, false);
      silhouette(craft, x + w - 58 * f * s, y + 58 * f * s, 44 * f * s, c, (0.45f + 0.5f * lift) * e, 1.3f * s);
      const char* bl[3] = {"VELOCITY", "AGILITY", "SIGNATURE"};
      for (int b = 0; b < 3; b++) {
        float by = y + (92 + b * 17) * f * s;
        g_ren.text(x + 14 * s, by, 10 * f * s, bl[b], R_DIM, e, 0, false);
        float bx = x + 90 * s, bw = w - 104 * s;
        for (int q = 0; q < 20; q++) {
          bool on = q < (int)(C.bars[b] * 20 + 0.5f);
          g_ren.rect(bx + q * bw / 20.f, by + 2 * s, bw / 20.f - 2 * s, 8 * f * s, b == 2 ? R_AMBER : c, (on ? 0.85f : 0.12f) * e);
        }
      }
      if (click(x, y, w, h)) resCraft = craft;
      y += h + 12 * s;
    }
    if (in.pressed[K_TAB] || (in.buttonsPressed & PAD_X)) resCraft = kResCraft[(resCraftSlot(resCraft) + 1) % kNumResCraft].idx;
    // programme telemetry: a slow scroll of cryptic log lines
    tag(x, y + 4 * s, s, "PROGRAMME LOG", ACC, e); y += 26 * s;
    int rows = (int)((L.bot - y) / (15 * s));
    int base = (int)(realTime * 1.5f);
    for (int i = 0; i < rows; i++) {
      uint32_t hsv = hsh(base + i);
      static const char* ev[] = {"SORTIE NOMINAL", "TELEMETRY SEALED", "CELL CHARGE 100%", "CLOAK CALIBRATED", "FBW SELF-TEST OK", "RANGE CLEARED", "WINGMAN OFFLINE", "SPECTRUM QUIET"};
      g_ren.text(x, y + i * 15 * s, 10.5f * s, hexWord(hsv, 4) + "  " + fmt("%04u", (hsv >> 8) % 9999) + "  " + ev[hsv % 8], R_DIM, e * (0.35f + 0.5f * (i == rows - 1 ? fmodf(realTime * 1.5f, 1.f) : 1.f)), 0, false);
    }
  }
  // ------------------------------------------------------------------ centre: the 3D preview stage
  {
    float cx = L.cx, cy = L.cy, R = L.r;
    // interaction: drag to turn, wheel to zoom, right stick turns
    bool inStage = hovered(L.px0, L.top, L.px1 - L.px0, L.bot - L.top);
    if (inStage && in.mPressed[0]) resDrag = true;
    if (!in.mDown[0]) resDrag = false;
    if (resDrag) { resYaw += in.mdx * 0.008f; resPitch = clampf(resPitch + in.mdy * 0.006f, -0.5f, 1.0f); resIdleT = 0; }
    else resIdleT += dt;
    if (in.pad && fabsf(in.rx) > 0.15f) { resYaw += in.rx * dt * 2.f; resIdleT = 0; }
    if (inStage && in.wheel != 0) { resZoom = clampf(resZoom * (1.f + in.wheel * 0.1f), 0.6f, 1.8f); in.wheel = 0; }
    if (resIdleT > 2.5f) resYaw += dt * 0.18f;   // slow turntable when left alone
    // stage graphics
    dashRing(cx, cy, R * 1.0f, 1.5f * s, ACC, 0.55f * e, 90, 0.5f, realTime * 0.05f);
    dashRing(cx, cy, R * 1.05f, 3 * s, ACC, 0.35f * e, 8, 0.12f, -realTime * 0.2f);
    ring(cx, cy, R * 0.86f, 1 * s, ACC, 0.15f * e);
    for (int i = 0; i < 72; i++) {   // bearing ticks with the camera's heading around the craft
      float th = i / 72.f * 6.2832f - 1.5708f, L1 = i % 6 == 0 ? 12 * s : 5 * s;
      g_ren.line(cx + cosf(th) * (R * 1.1f), cy + sinf(th) * (R * 1.1f), cx + cosf(th) * (R * 1.1f + L1), cy + sinf(th) * (R * 1.1f + L1), 1 * s, ACC, 0.5f * e);
    }
    float brg = fmodf(fmodf(resYaw / DEG, 360.f) + 360.f, 360.f);
    g_ren.text(cx, std::min(cy + R * 1.1f + 14 * s, L.bot - 16 * s), 11 * s, fmt("VIEW  %03.0f  //  ELEV %+03.0f  //  ZOOM %.1fx      DRAG TO ROTATE  -  WHEEL TO ZOOM", brg, resPitch / DEG, resZoom), R_DIM, e, 1, false);
    // brackets on the stage
    float sx0 = L.px0, sy0 = L.top, sw = L.px1 - L.px0, sh = L.bot - L.top;
    corners(sx0, sy0, sw, sh, 18 * s, 2 * s, ACC, 0.7f * e);
    g_ren.text(sx0 + 10 * s, sy0 + 8 * s, 11 * s, "LIVE RENDER  //  " + std::string(g_world.airports[resAirport].code) + " RANGE", ACC, e, 0, false);
    // re-scan band after a selection change
    if (selT < 1.2f) {
      float yb = cy - R + 2 * R * smoothstepf(0.f, 1.1f, selT);
      g_ren.rect(cx - R, yb, 2 * R, 2 * s, ACC, 0.9f * e);
      g_ren.glow(cx - R, yb - 6 * s, 2 * R, 12 * s, ACC, 0.4f * e, 6 * s, 16 * s);
      g_ren.rect(cx - R, cy - R, 2 * R, yb - (cy - R), ACC, 0.04f * e);
    }
    // callouts: points on the actual airframe, projected from the live render
    struct Call { vec3 b; const char* t; float side; };
    std::vector<Call> calls;
    float fl = kAircraft[resCraft].fusLen * 0.5f;
    if (wr) calls = {{vec3(0, 0, -fl), "SENSOR NOSE  //  PASSIVE", -1}, {kWraithPods[1], "POD 2  //  130 kN  0-90 DEG", 1},
                     {kWraithPods[2], "POD 3  //  VECTORING VANES", -1}, {vec3(6.2f, -0.24f, 2.0f), "FACETED SKIN  //  CLOAK MESH", 1}};
    else if (resCraft == kResearchJet) calls = {{vec3(0, 0, -fl), "SYNTHETIC-VISION POD", -1}, {vec3(2.1f, 0.1f, -4.4f), "CANARD  //  ALL-MOVING", 1},
                  {vec3(5.62f, -0.38f, 4.4f), "CRANKED DELTA  //  NO FLAPS", 1}, {vec3(-0.82f, -0.12f, 7.75f), "2D NOZZLE  //  +-29 DEG", -1}};
    else {   // a conventional airframe: its parts from the parametric model
      const ModelDef& md = kModels[resCraft];
      vec3 tip = modelWingTip(md), fin = modelFinTop(md);
      calls = {{vec3(0, 0, -fl), "SENSOR NOSE  //  TEST RADAR", -1}, {tip, "TAPERED WING  //  SLOTTED FLAPS", 1},
               {vec3(-md.nacX, md.nacY, md.nacZ0 + md.nacLen * 0.5f), "TURBOFAN  //  12.5 kN", -1}, {fin, "CONVENTIONAL TAIL", 1}};
    }
    float ca = e * smoothstepf(0.6f, 1.3f, selT);
    int ci = 0;
    for (auto& c : calls) {
      float px, py;
      if (!g_ren.project(fp, resPrevPos + resPrevQ.rotate(c.b), px, py)) continue;
      // each side has a slot near the top and one near the bottom of the stage, away from the craft
      int slot = 0; for (int q = 0; q < ci; q++) if (calls[q].side == c.side) slot++;
      float lx = c.side > 0 ? L.px1 - 12 * s : L.px0 + 12 * s, ly = slot == 0 ? L.top + 58 * s : L.bot - 40 * s;
      float ex = lx + (c.side > 0 ? -170 * s : 170 * s);
      g_ren.rect(px - 3 * s, py - 3 * s, 6 * s, 6 * s, ACC, ca, 3 * s);
      ring(px, py, 8 * s + 2 * s * sinf(realTime * 3 + ci), 1 * s, ACC, 0.6f * ca, 20);
      g_ren.line(px, py, ex, ly, 1 * s, ACC, 0.6f * ca);
      g_ren.line(ex, ly, lx, ly, 1 * s, ACC, 0.6f * ca);
      g_ren.text(c.side > 0 ? lx : lx, ly - 15 * s, 10.5f * s, decrypt(c.t, selT - 0.8f - ci * 0.1f, 0.01f), R_TEXT, ca, c.side > 0 ? 2 : 0, false);
      ci++;
    }
  }
  // ------------------------------------------------------------------ right: specification, envelope, notes
  {
    float x = L.rx, y = L.top, w = L.rw;
    tag(x, y, s, "SPECIFICATION  //  DECRYPTED", ACC, e); y += 24 * s;
    slab(x, y, w, L.bot - y, s, ACC, e);
    float px = x + 14 * s, iw = w - 28 * s, py = y + 12 * s;
    g_ren.text(px, py, 11 * s, decrypt(RC.title, selT, 0.01f), ACC, e, 0, false); py += 18 * s;
    const SpecRow* rows = RC.rows;
    for (int i = 0; i < 8; i++) {
      float rt = selT - 0.1f - i * 0.06f;
      g_ren.text(px, py, 10 * s, rows[i].k, R_DIM, e, 0, false);
      g_ren.text(px + 112 * s, py - 1 * s, 12 * s, decrypt(rows[i].v, rt, 0.012f), R_TEXT, e, 0, false);
      if (rows[i].bar >= 0) {
        float f = rows[i].bar * smoothstepf(0.f, 0.5f, rt);
        g_ren.rect(px + 112 * s, py + 15 * s, iw - 112 * s, 2 * s, ACC, 0.12f * e);
        g_ren.rect(px + 112 * s, py + 15 * s, (iw - 112 * s) * f, 2 * s, rows[i].k[0] == 'S' && wr ? R_AMBER : ACC, 0.9f * e);
      }
      py += 23 * s;
    }
    // manoeuvre envelope (V-n): load factor against Mach
    py += 6 * s;
    tag(px, py, s, "MANOEUVRE ENVELOPE", ACC, e); py += 20 * s;
    float ch = std::min(120 * s, L.bot - py - 120 * s);
    if (ch > 50 * s) {
      float gx0 = px + 26 * s, gw = iw - 30 * s, gy0 = py, gh = ch;
      float mMax = RC.vMax > 1.5f ? 4.f : 1.f, nTop = RC.nTop, nBot = RC.nBot, nStep = RC.nStep;   // each craft on its own scale
      auto X = [&](float m) { return gx0 + m / mMax * gw; };
      auto Y = [&](float n) { return gy0 + (nTop - n) / (nTop - nBot) * gh; };
      for (int m = 0; m <= 4; m++) { float mm = m * mMax / 4.f; g_ren.rect(X(mm), gy0, 1, gh, R_DIM, 0.18f * e); g_ren.text(X(mm), gy0 + gh + 3 * s, 9 * s, mMax > 1.5f ? fmt("M%d", m) : fmt("M%.2f", mm), R_DIM, e, 1, false); }
      for (float n = nBot; n <= nTop + 0.1f; n += nStep) { g_ren.rect(gx0, Y(n), gw, 1, R_DIM, n == 0 ? 0.4f : 0.18f * e); g_ren.text(gx0 - 4 * s, Y(n) - 5 * s, 9 * s, fmt("%+.0f", n), R_DIM, e, 2, false); }
      float nMax = RC.nMax, nMin = RC.nMin, vMax = RC.vMax, ms = RC.ms;
      std::vector<std::pair<float, float>> env;
      for (int i = 0; i <= 20; i++) { float m = ms + (vMax - ms) * i / 20.f; env.push_back({m, std::min(nMax, (m / ms) * (m / ms))}); }
      for (int i = 20; i >= 0; i--) { float m = ms + (vMax - ms) * i / 20.f; env.push_back({m, std::max(nMin, -(m / ms) * (m / ms) * 0.5f)}); }
      env.push_back(env.front());
      float drawn = smoothstepf(0.2f, 1.2f, selT) * (env.size() - 1);
      for (size_t i = 0; i + 1 < env.size() && i < drawn; i++)
        g_ren.line(X(env[i].first), Y(env[i].second), X(env[i + 1].first), Y(env[i + 1].second), 1.6f * s, ACC, e);
      size_t k = (size_t)(fmodf(realTime * 6.f, (float)env.size() - 1));   // a marker running round the edge
      g_ren.rect(X(env[k].first) - 3 * s, Y(env[k].second) - 3 * s, 6 * s, 6 * s, R_AMBER, e, 3 * s);
      py += gh + 22 * s;
    }
    tag(px, py, s, "HANDLING DIRECTIVES", ACC, e); py += 20 * s;
    for (int i = 0; i < 5 && py < L.bot - 16 * s; i++) { g_ren.text(px, py, 11 * s, std::string("> ") + RC.notes[i], R_DIM, e, 0, false); py += 16 * s; }
  }
  // ------------------------------------------------------------------ bottom: sortie parameters
  {
    float y0 = L.bot + 16 * s, x0 = L.lx, x1 = W - 24 * s, h = H - y0 - 34 * s;
    slab(x0, y0, x1 - x0, h, s, ACC, e);
    // laid out for a 1280 px wide bar and squeezed (fonts included) when the window is narrower, so nothing overlaps
    const float hs = s * std::clamp((x1 - x0) / (1232.f * s), 0.55f, 1.f);
    float cx = x0 + 16 * hs, cy = y0 + 12 * s;
    auto chip = [&](float x, float y, float w, float hh, const std::string& t, bool on) {
      bool hov = hovered(x, y, w, hh);
      g_ren.rect(x, y, w, hh, on ? ACC * 0.25f : R_INK, (on ? 0.9f : 0.7f) * e);
      g_ren.rectOutline(x, y, w, hh, ACC, (on ? 0.9f : hov ? 0.6f : 0.25f) * e, 0, 1 * s);
      if (on) g_ren.rect(x, y + hh - 2 * s, w, 2 * s, ACC, e);
      g_ren.text(x + w * 0.5f, y + hh * 0.5f - 6.5f * s, 11.5f * hs, t, on ? R_TEXT : R_DIM, e, 1, false);
      return click(x, y, w, hh);
    };
    int na = (int)g_world.airports.size();
    if (chip(cx, cy + 16 * s, 96 * hs, 34 * s, "<  ABORT", false) || in.pressed[K_ESC]) { screen = SCR_MENU; return; }
    cx += 112 * hs;
    // site
    g_ren.text(cx, cy, 10 * hs, "INSERTION SITE", R_DIM, e, 0, false);
    const Airport& a = g_world.airports[resAirport];
    float sw = 290 * hs;
    if (chip(cx, cy + 16 * s, 26 * hs, 34 * s, "<", false) || in.pressed[K_LEFT]) resAirport = (resAirport + na - 1) % na;
    g_ren.text(cx + 36 * hs, cy + 16 * s, 16 * hs, ellipsize(std::string(a.code) + "  " + a.name, sw - 44 * hs, 16 * hs), R_TEXT, e, 0, false);
    g_ren.text(cx + 36 * hs, cy + 36 * s, 10 * hs, ellipsize(fmt("RWY %02d/%02d  //  %.0f M %s  //  ELEV %.0f M", a.rwyNumber(false), a.rwyNumber(true), a.length, surfaceName(a.surface), a.elev), sw - 44 * hs, 10 * hs), R_DIM, e, 0, false);
    if (chip(cx + sw, cy + 16 * s, 26 * hs, 34 * s, ">", false) || in.pressed[K_RIGHT]) resAirport = (resAirport + 1) % na;
    float c2 = cx + sw + 40 * hs;
    g_ren.text(c2, cy, 10 * hs, "INSERTION", R_DIM, e, 0, false);
    if (chip(c2, cy + 16 * s, 86 * hs, 34 * s, "AIRBORNE", resAirborne)) resAirborne = true;
    if (chip(c2 + 90 * hs, cy + 16 * s, 76 * hs, 34 * s, "RUNWAY", !resAirborne)) resAirborne = false;
    float c3 = c2 + 182 * hs;
    g_ren.text(c3, cy, 10 * hs, "ATMOSPHERE", R_DIM, e, 0, false);
    const char* wxs[] = {"CLEAR", "OVERCAST", "STORM"};
    for (int i = 0; i < 3; i++) if (chip(c3 + i * 80 * hs, cy + 16 * s, 76 * hs, 34 * s, wxs[i], resWx == i)) resWx = i;
    // time of day slider (the preview's light follows it)
    float c4 = c3 + 256 * hs, tw = std::max(50 * hs, x1 - 16 * hs - 226 * hs - c4);
    g_ren.text(c4, cy, 10 * hs, fmt("LOCAL TIME  %02d:00", (int)resTime), R_DIM, e, 0, false);
    {
      float f = (resTime - 5.f) / 16.f, by = cy + 31 * s;
      g_ren.rect(c4, by, tw, 3 * s, ACC, 0.15f * e);
      g_ren.rect(c4, by, tw * f, 3 * s, ACC, 0.9f * e);
      g_ren.rect(c4 + tw * f - 5 * s, by - 6 * s, 10 * s, 15 * s, ACC, e, 2 * s);
      if (hovered(c4 - 6 * s, cy + 12 * s, tw + 12 * s, 36 * s) && in.mDown[0] && !resDrag) resTime = floorf(5.f + clampf((in.mx - c4) / tw, 0, 1) * 16.f + 0.5f);
    }
    // initiate / abort
    float bw = 210 * hs, bx = x1 - 16 * hs - bw, bh = 46 * s, by = y0 + (h - bh) * 0.5f;
    bool hov = hovered(bx, by, bw, bh);
    g_ren.rect(bx, by, bw, bh, ACC * (hov ? 0.45f : 0.3f), e);
    for (float st = fmodf(realTime * 30.f, 16 * s); st < bw; st += 16 * s) g_ren.line(bx + st, by + bh, bx + std::min(bw, st + 10 * s), by, 3 * s, ACC, 0.12f * e);
    g_ren.rectOutline(bx, by, bw, bh, ACC, e, 0, 1.5f * s);
    g_ren.glow(bx, by, bw, bh, ACC, (hov ? 0.35f : 0.15f) * e, 0, 14 * s);
    g_ren.text(bx + bw * 0.5f, by + 9 * s, 16 * hs, std::string("INITIATE  ") + RC.num, R_TEXT, e, 1, false);
    g_ren.text(bx + bw * 0.5f, by + 29 * s, 9 * hs, "SORTIE NOT RECORDED IN LOGBOOK", R_DIM, e, 1, false);
    if (click(bx, by, bw, bh) || in.pressed[K_ENTER]) { launchResearch(); return; }
  }
  g_ren.text(W * 0.5f, H - 22 * s, 10.5f * s, in.pad ? "L-STICK CURSOR   A SELECT   X SWITCH AIRFRAME   R-STICK ROTATE   LB / RB SITE   START INITIATE   B ABORT"
                                                     : "TAB SWITCH AIRFRAME   <- / -> SITE   DRAG ROTATE   WHEEL ZOOM   ENTER INITIATE   ESC ABORT",
             R_DIM, 0.75f * e, 1, false);
  if (fmodf(realTime, 1.4f) < 1.0f) g_ren.text(W - 24 * s, H - 22 * s, 10.5f * s, "CLASSIFIED", R_RED, 0.85f * e, 2, false);
}
