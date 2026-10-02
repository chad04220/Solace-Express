// Air Xpress - menus, hub screens and flight HUD
#include "game.h"

// High-tech glass UI: deep navy panels, cyan accents, amber for warnings
static const vec3 C_PANEL(0.015f, 0.035f, 0.06f), C_PANEL2(0.04f, 0.085f, 0.13f), C_ACCENT(0.32f, 0.86f, 1.0f), C_WARN(1.0f, 0.72f, 0.22f);
static const vec3 C_TEXT(0.93f, 0.97f, 1.0f), C_DIM(0.56f, 0.68f, 0.79f), C_GOOD(0.42f, 1.0f, 0.68f), C_BAD(1.0f, 0.4f, 0.38f);
static const vec3 C_BTN(0.04f, 0.09f, 0.14f), C_BTN_HI(0.07f, 0.2f, 0.29f), C_INK(0.01f, 0.05f, 0.08f);

static uint32_t uid(float x, float y, const std::string& s) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix((uint32_t)(int)x); mix((uint32_t)(int)y);
  for (char c : s) mix((uint8_t)c);
  return h;
}
static vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

float Game::S() const { return std::max(0.6f, g_ren.H / 720.f); }

bool Game::hovered(float x, float y, float w, float h) const { return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h; }

// Eases a per-widget value toward target (hover glows, sliding indicators)
float Game::anim(uint32_t id, float target, float rate) {
  auto it = uiAnim.find(id);
  if (it == uiAnim.end()) it = uiAnim.emplace(id, target == 1.f ? 0.f : target).first;
  it->second += (target - it->second) * (1.f - expf(-rate * uiDt));
  return it->second;
}

static void brackets(float x, float y, float w, float h, float L, float t, vec3 c, float a) {
  g_ren.rect(x, y, L, t, c, a); g_ren.rect(x, y, t, L, c, a);
  g_ren.rect(x + w - L, y, L, t, c, a); g_ren.rect(x + w - t, y, t, L, c, a);
  g_ren.rect(x, y + h - t, L, t, c, a); g_ren.rect(x, y + h - L, t, L, c, a);
  g_ren.rect(x + w - L, y + h - t, L, t, c, a); g_ren.rect(x + w - t, y + h - L, t, L, c, a);
}

void Game::panel(float x, float y, float w, float h, float a) {
  float s = S(), r = 4 * s;
  a = std::min(1.f, a / 0.78f);
  g_ren.glow(x, y, w, h, vec3(0, 0, 0), 0.45f * a, r, 22 * s);
  g_ren.rectGrad(x, y, w, h, C_PANEL2, C_PANEL, 0.88f * a, r);
  g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.16f * a, r, 1.f * s);
  brackets(x, y, w, h, 16 * s, 2 * s, C_ACCENT, 0.75f * a);
  g_ren.rect(x + 26 * s, y, w - 52 * s, 1.f * s, C_ACCENT, 0.35f * a);
}

// Lighter glass used for in-flight displays so the world stays visible
void Game::hudPanel(float x, float y, float w, float h, float a) {
  float s = S(), r = 4 * s;
  g_ren.rectGrad(x, y, w, h, vec3(0.02f, 0.06f, 0.1f), vec3(0.0f, 0.02f, 0.04f), 0.5f * a, r);
  g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.18f * a, r, 1.f * s);
  brackets(x, y, w, h, 10 * s, 1.5f * s, C_ACCENT, 0.7f * a);
}

// Section label: marker, small caps text and a hairline with end ticks
void Game::header(float x, float y, float w, const std::string& label) {
  float s = S();
  g_ren.rect(x, y + 3 * s, 3 * s, 11 * s, C_ACCENT, 1);
  float tw = g_ren.text(x + 10 * s, y, 13 * s, label, C_ACCENT, 1, 0, false);
  float lx = x + 20 * s + tw;
  if (lx < x + w - 10 * s) {
    g_ren.rect(lx, y + 8 * s, x + w - lx, 1 * s, C_ACCENT, 0.25f);
    g_ren.rect(x + w - 6 * s, y + 6 * s, 6 * s, 5 * s, C_ACCENT, 0.5f);
  }
}

// Selectable list card with animated hover / selection glow
void Game::card(float x, float y, float w, float h, bool sel, bool hov, vec3 accent) {
  float s = S(), r = 4 * s;
  uint32_t id = uid(x, y, "card");
  float th = anim(id, hov ? 1.f : 0.f, 14), ts = anim(id + 7, sel ? 1.f : 0.f, 12);
  if (ts > 0.01f) g_ren.glow(x, y, w, h, C_ACCENT, 0.2f * ts, r, 10 * s);
  float k = std::max(th * 0.55f, ts);
  g_ren.rectGrad(x, y, w, h, mixc(vec3(0.035f, 0.07f, 0.11f), vec3(0.06f, 0.17f, 0.25f), k), mixc(vec3(0.02f, 0.045f, 0.07f), vec3(0.04f, 0.11f, 0.17f), k), 0.93f, r);
  g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.1f + 0.3f * th + 0.55f * ts, r, 1.f * s);
  g_ren.rect(x, y + h * 0.15f * (1 - std::max(th, ts)), 3 * s, h * (0.7f + 0.3f * std::max(th, ts)), accent, 0.45f + 0.55f * std::max(th, ts));
  if (ts > 0.01f) { float cx = x + w - 10 * s, cy = y + h * 0.5f; g_ren.line(cx - 5 * s, cy - 6 * s, cx, cy, 2 * s, C_ACCENT, ts); g_ren.line(cx, cy, cx - 5 * s, cy + 6 * s, 2 * s, C_ACCENT, ts); }
}

bool Game::button(float x, float y, float w, float h, const std::string& label, bool enabled, bool highlight) {
  float s = S(), r = 4 * s;
  bool hov = enabled && hovered(x, y, w, h);
  uint32_t id = uid(x, y, label);
  float t = anim(id, hov ? 1.f : 0.f, 14);
  float pr = anim(id ^ 0x5bd1e995u, hov && in.mDown[0] ? 1.f : 0.f, 30);
  vec3 tc;
  if (!enabled) {
    g_ren.rectGrad(x, y, w, h, vec3(0.05f, 0.07f, 0.09f), vec3(0.03f, 0.04f, 0.06f), 0.85f, r);
    g_ren.rectOutline(x, y, w, h, C_DIM, 0.12f, r, 1.f * s);
    tc = C_DIM * 0.55f;
  } else if (highlight) {
    float pulse = 0.5f + 0.5f * sinf(realTime * 3.f);
    g_ren.glow(x, y, w, h, C_ACCENT, 0.22f + 0.25f * t + 0.08f * pulse, r, 8 * s + 8 * s * t);
    g_ren.rectGrad(x, y, w, h, C_ACCENT * (1.05f + 0.12f * t), C_ACCENT * 0.6f, 0.96f, r);
    g_ren.rect(x + 2 * s, y + 2 * s, w - 4 * s, 1.f * s, vec3(1, 1, 1), 0.45f);
    tc = C_INK;
  } else {
    if (t > 0.01f) g_ren.glow(x, y, w, h, C_ACCENT, 0.16f * t, r, 10 * s);
    vec3 b = mixc(C_BTN, C_BTN_HI, t);
    g_ren.rectGrad(x, y, w, h, b * 1.35f, b, 0.9f, r);
    g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.2f + 0.55f * t, r, 1.f * s);
    float bh = h * (0.45f + 0.55f * t);
    g_ren.rect(x, y + (h - bh) * 0.5f, 2.5f * s, bh, C_ACCENT, 0.5f + 0.5f * t);
    tc = mixc(C_TEXT * 0.88f, vec3(1, 1, 1), t);
  }
  // sheen sweeping across a hovered button
  if (enabled && t > 0.02f) {
    float ph = fmodf(realTime * 0.8f, 1.5f) / 1.5f, sx = x + (w + 40 * s) * ph - 20 * s;
    float x0 = std::max(x + 2 * s, sx - 10 * s), x1 = std::min(x + w - 2 * s, sx + 10 * s);
    if (x1 > x0) g_ren.rect(x0, y + 1 * s, x1 - x0, h - 2 * s, vec3(1, 1, 1), 0.07f * t);
  }
  if (pr > 0.01f) g_ren.rect(x, y, w, h, vec3(1, 1, 1), 0.18f * pr, r);
  float ts = std::min(h * 0.46f, 18 * S());
  g_ren.text(x + w * 0.5f, y + (h - ts) * 0.5f - ts * 0.08f + pr * 1.f * s, ts, label, tc, 1.f, 1, false);
  if (hov && in.mPressed[0]) { g_audio.trigger(SFX_CLICK); return true; }
  return false;
}

static std::vector<std::string> wrap(const std::string& s, float width, float size) {
  std::vector<std::string> lines; std::string cur, word;
  auto flush = [&]() {
    if (word.empty()) return;
    std::string t = cur.empty() ? word : cur + " " + word;
    if (g_ren.textWidth(t, size) > width && !cur.empty()) { lines.push_back(cur); cur = word; } else cur = t;
    word.clear();
  };
  for (char ch : s) { if (ch == ' ') flush(); else if (ch == '\n') { flush(); lines.push_back(cur); cur.clear(); } else word += ch; }
  flush();
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

// Shortens a string with "..." so it fits the given width
static std::string ellipsize(const std::string& str, float width, float size) {
  if (g_ren.textWidth(str, size) <= width) return str;
  std::string t = str;
  while (!t.empty() && g_ren.textWidth(t + "...", size) > width) t.pop_back();
  while (!t.empty() && t.back() == ' ') t.pop_back();
  return t + "...";
}
// Picks the largest font size (down to minSize) at which the text fits, then ellipsizes if it still doesn't
static float fitText(float x, float y, float width, float size, float minSize, const std::string& str, vec3 col, float a = 1.f) {
  float sz = size;
  while (sz > minSize && g_ren.textWidth(str, sz) > width) sz -= 0.5f;
  g_ren.text(x, y + (size - sz) * 0.5f, sz, ellipsize(str, width, sz), col, a);
  return sz;
}

std::string Game::fmtMoney(int m) const {
  bool neg = m < 0; long v = labs((long)m);
  std::string s = std::to_string(v), o;
  for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += ','; o += s[i]; }
  return (neg ? "-$" : "$") + o;
}
std::string Game::fmtSpeed(float ms) const { return set.metric ? fmt("%.0f km/h", ms * 3.6f) : fmt("%.0f kt", ms * MS_TO_KT); }
std::string Game::fmtAlt(float m) const { return set.metric ? fmt("%.0f m", m) : fmt("%.0f ft", m * M_TO_FT); }

// ------------------------------------------------------------------ toasts
void Game::drawToasts() {
  float s = S(), y = 128 * s;
  if (screen != SCR_FLIGHT) y = g_ren.H - 120 * s;
  for (auto& t : toasts) {
    float a = clampf(std::min(t.t * 4.f, (5.f - t.t) * 1.5f), 0, 1);
    float slide = (1.f - clampf(t.t * 5.f, 0, 1)) * -14 * s;
    float ts = 16 * s, w = g_ren.textWidth(t.text, ts) + 40 * s, x = g_ren.W * 0.5f - w * 0.5f, yy = y + slide;
    g_ren.glow(x, yy, w, ts + 14 * s, t.col, 0.12f * a, 3 * s, 10 * s);
    g_ren.rectGrad(x, yy, w, ts + 14 * s, vec3(0.02f, 0.07f, 0.11f), vec3(0.0f, 0.02f, 0.04f), 0.75f * a, 3 * s);
    g_ren.rectOutline(x, yy, w, ts + 14 * s, t.col, 0.35f * a, 3 * s, 1.f * s);
    g_ren.rect(x, yy, 3 * s, ts + 14 * s, t.col, a);
    g_ren.text(g_ren.W * 0.5f + 2 * s, yy + 6 * s, ts, t.text, t.col, a, 1);
    y += ts + 22 * s;
  }
  if (hubMsgTime > 0 && screen == SCR_HUB) {
    float ts = 17 * s, w = g_ren.textWidth(hubMsg, ts) + 44 * s, x = g_ren.W * 0.5f - w * 0.5f, yy = g_ren.H - 72 * s;
    g_ren.glow(x, yy, w, ts + 16 * s, C_ACCENT, 0.2f, 3 * s, 12 * s);
    g_ren.rectGrad(x, yy, w, ts + 16 * s, C_PANEL2, C_PANEL, 0.96f, 3 * s);
    g_ren.rectOutline(x, yy, w, ts + 16 * s, C_ACCENT, 0.5f, 3 * s, 1.f * s);
    g_ren.text(g_ren.W * 0.5f, yy + 8 * s, ts, hubMsg, C_ACCENT, 1, 1);
  }
}

// ------------------------------------------------------------------ main menu
void Game::drawMenu() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rectGrad(0, 0, W * 0.46f, H, vec3(0.01f, 0.04f, 0.07f), vec3(0.0f, 0.015f, 0.03f), 0.78f);
  g_ren.rect(W * 0.46f, 0, 1.f * s, H, C_ACCENT, 0.3f);
  // drifting scan line down the menu column
  float scan = fmodf(realTime * 60.f * s, H + 80 * s) - 40 * s;
  g_ren.rectGrad(0, scan, W * 0.46f, 40 * s, vec3(0, 0, 0), C_ACCENT, 0.05f);
  g_ren.text(60 * s, 86 * s, 74 * s, "AIR XPRESS", C_TEXT, 1, 0);
  g_ren.rect(64 * s, 166 * s, 120 * s, 3 * s, C_ACCENT, 1);
  g_ren.rect(190 * s, 167 * s, 220 * s, 1 * s, C_ACCENT, 0.35f);
  g_ren.text(64 * s, 178 * s, 20 * s, "Pilot career across the Solace Islands", C_ACCENT, 1, 0);
  float y = 250 * s, bw = 300 * s, bh = 52 * s;
  if (hasSave) {
    if (button(60 * s, y, bw, bh, "Continue Career", true, true)) { screen = SCR_HUB; career.refreshBoard(); }
    y += bh + 14 * s;
  }
  if (in.pressed[K_ESC]) confirmNew = false;
  if (!confirmNew) {
    if (button(60 * s, y, bw, bh, "New Career", true, !hasSave)) { if (hasSave) confirmNew = true; else { career.newGame(); saveGame(); screen = SCR_HUB; } }
  } else {
    if (button(60 * s, y, bw * 0.48f, bh, "Overwrite!", true, true)) { career.newGame(); saveGame(); confirmNew = false; screen = SCR_HUB; }
    if (button(60 * s + bw * 0.52f, y, bw * 0.48f, bh, "Cancel")) confirmNew = false;
  }
  y += bh + 14 * s;
  if (button(60 * s, y, bw, bh, "Settings")) { screen = SCR_HUB; hubTab = TAB_SETTINGS; }
  y += bh + 14 * s;
  if (button(60 * s, y, bw, bh, "Quit")) quit = true;
  g_ren.text(60 * s, H - 70 * s, 14 * s, "F11 FULLSCREEN   //   GAMEPAD SUPPORTED   //   R RADIO   M MUFFLE   N GPS MAP", C_DIM, 0.9f);
  g_ren.text(60 * s, H - 45 * s, 13 * s, "v1.3  -  Real-time GPU ray-traced terrain, water, clouds and aircraft", C_DIM, 0.6f);
}

// ------------------------------------------------------------------ hub
void Game::drawHub() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  // top bar
  g_ren.rectGrad(0, 0, W, 64 * s, vec3(0.02f, 0.06f, 0.1f), vec3(0.0f, 0.02f, 0.04f), 0.9f);
  g_ren.rect(0, 64 * s - 1 * s, W, 1 * s, C_ACCENT, 0.4f);
  g_ren.text(20 * s, 16 * s, 30 * s, "AIR XPRESS", C_TEXT, 1);
  g_ren.rect(22 * s, 50 * s, 60 * s, 2 * s, C_ACCENT, 1);
  const Airport& loc = g_world.airports[career.location];
  float x = 250 * s;
  auto stat = [&](float w, const char* k, const std::string& v, vec3 c, float vs) {
    g_ren.rect(x - 14 * s, 14 * s, 1 * s, 36 * s, C_ACCENT, 0.3f);
    g_ren.text(x, 12 * s, 12 * s, k, C_DIM, 1, 0, false);
    g_ren.text(x, 30 * s, vs, ellipsize(v, w - 24 * s, vs), c, 1);
    x += w;
  };
  stat(290 * s, "PILOT LICENCE", licenseName(career.license), C_TEXT, 18 * s);
  stat(170 * s, "BANK", fmtMoney(career.money), career.money < 0 ? C_BAD : C_GOOD, 20 * s);
  stat(130 * s, "REPUTATION", fmt("%d", career.reputation), C_ACCENT, 18 * s);
  stat(std::max(160 * s, W - 20 * s - x), "LOCATION", fmt("%s  %s", loc.code, loc.name), C_TEXT, 18 * s);
  // tabs with a sliding underline
  const char* tabs[] = {"CONTRACTS", "HANGAR", "LOGBOOK", "SETTINGS"};
  float tx = 20 * s, ty = 76 * s, tw = 150 * s;
  g_ren.rectGrad(tx, ty, 4 * (tw + 10 * s) - 10 * s, 38 * s, vec3(0.02f, 0.06f, 0.1f), vec3(0.0f, 0.02f, 0.04f), 0.75f, 3 * s);
  for (int i = 0; i < 4; i++) {
    bool hov = hovered(tx, ty, tw, 38 * s);
    float h = anim(uid(tx, ty, "tab"), hov ? 1.f : 0.f, 14);
    if (h > 0.01f) g_ren.rectGrad(tx, ty, tw, 38 * s, vec3(0.05f, 0.16f, 0.24f), vec3(0.02f, 0.06f, 0.1f), 0.8f * h, 3 * s);
    g_ren.text(tx + tw * 0.5f, ty + 11 * s, 16 * s, tabs[i], hubTab == i ? C_TEXT : mixc(C_DIM, C_TEXT, h), 1, 1, false);
    if (hov && in.mPressed[0] && hubTab != i) { hubTab = i; g_audio.trigger(SFX_CLICK); }
    tx += tw + 10 * s;
  }
  float ux = anim(0x7ab5u, 20 * s + hubTab * (tw + 10 * s), 14);
  g_ren.glow(ux + 10 * s, ty + 35 * s, tw - 20 * s, 3 * s, C_ACCENT, 0.5f, 1.5f * s, 8 * s);
  g_ren.rect(ux + 10 * s, ty + 35 * s, tw - 20 * s, 3 * s, C_ACCENT, 1);
  if (button(W - 300 * s, ty, 130 * s, 38 * s, showRadio ? "Radio <" : "Radio", true, showRadio)) showRadio = !showRadio;
  if (button(W - 160 * s, ty, 140 * s, 38 * s, "Main Menu")) { screen = SCR_MENU; saveGame(); }
  float cx = 20 * s, cy = 126 * s, cw = W - 40 * s, ch = H - 146 * s;
  switch (hubTab) {
    case TAB_CONTRACTS: drawHubContracts(cx, cy, cw, ch); break;
    case TAB_HANGAR: drawHubHangar(cx, cy, cw, ch); break;
    case TAB_LOGBOOK: drawHubLogbook(cx, cy, cw, ch); break;
    default: panel(cx, cy, std::min(cw, 760 * s), ch); drawSettings(cx + 24 * s, cy + 20 * s, std::min(cw, 760 * s) - 48 * s, ch); break;
  }
  if (showRadio) drawRadioPanel(W - 460 * s, 126 * s);
  if (in.pressed[K_ESC]) { if (showRadio) showRadio = false; else { screen = SCR_MENU; saveGame(); } }
}

void Game::drawMapView(float x, float y, float w, float h, int from, int to, const std::vector<Waypoint>* wps) {
  float s = S();
  float sz = std::min(w, h);
  float ox = x + (w - sz) * 0.5f, oy = y + (h - sz) * 0.5f;
  g_ren.image(g_ren.minimapTex, ox, oy, sz, sz);
  g_ren.flushUIPublic();
  auto toS = [&](float wx_, float wz) { return vec2(ox + (wx_ + WORLD_HALF) / (2 * WORLD_HALF) * sz, oy + (wz + WORLD_HALF) / (2 * WORLD_HALF) * sz); };
  if (from >= 0 && to >= 0) {
    vec2 prev = toS(g_world.airports[from].x, g_world.airports[from].z);
    if (wps) for (auto& wp : *wps) { vec2 p = toS(wp.x, wp.z); g_ren.line(prev.x, prev.y, p.x, p.y, 3 * s, C_ACCENT, 0.9f); g_ren.rect(p.x - 4 * s, p.y - 4 * s, 8 * s, 8 * s, vec3(0.3f, 1, 0.5f), 1, 4 * s); prev = p; }
    vec2 b = toS(g_world.airports[to].x, g_world.airports[to].z);
    g_ren.line(prev.x, prev.y, b.x, b.y, 3 * s, C_ACCENT, 0.9f);
  }
  for (size_t i = 0; i < g_world.airports.size(); i++) {
    const Airport& a = g_world.airports[i];
    vec2 p = toS(a.x, a.z);
    bool key = (int)i == from || (int)i == to;
    vec3 c = (int)i == career.location ? vec3(0.3f, 0.8f, 1.f) : key ? C_WARN : vec3(1, 1, 1);
    float r = (a.size + 2) * 2.2f * s;
    g_ren.rect(p.x - r, p.y - r, r * 2, r * 2, vec3(0, 0, 0), 0.7f, r);
    g_ren.rect(p.x - r * 0.65f, p.y - r * 0.65f, r * 1.3f, r * 1.3f, c, 1, r);
    if (sz > 300 * s || key) g_ren.text(p.x + r + 3 * s, p.y - 8 * s, 13 * s, a.code, c, 1);
  }
}

void Game::drawHubContracts(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.36f, 470 * s);
  panel(x, y, lw, h);
  // build card list: story, free flight, freelance
  struct Card { const Contract* c; bool story; bool free; };
  std::vector<Card> cards;
  const Contract* st = career.nextStory();
  if (st) cards.push_back({st, true, false});
  cards.push_back({nullptr, false, true});
  for (auto& c : career.board) cards.push_back({&c, false, false});
  selContract = std::clamp(selContract, 0, (int)cards.size() - 1);
  float cy = y + 14 * s;
  header(x + 16 * s, cy, lw - 32 * s, ellipsize(career.finished ? "CAMPAIGN COMPLETE - FREELANCE JOBS CONTINUE" : "AVAILABLE WORK", lw - 60 * s, 13 * s)); cy += 28 * s;
  for (int i = 0; i < (int)cards.size(); i++) {
    float chh = 66 * s;
    if (cy + chh > y + h - 8 * s) break;
    bool sel = i == selContract;
    bool hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    card(x + 10 * s, cy, lw - 20 * s, chh, sel, hov, cards[i].story ? C_WARN : C_ACCENT);
    if (hov && in.mPressed[0]) { selContract = i; selAircraft = -1; g_audio.trigger(SFX_CLICK); }
    if (cards[i].free) {
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, "Free Flight / Ferry", C_TEXT);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize("Fly anywhere for fun or to reposition. No pay.", lw - 48 * s, 14 * s), C_DIM, 1);
    } else {
      const Contract& c = *cards[i].c;
      std::string tag = cards[i].story ? fmt("STORY CH.%d  ", c.chapter) : "";
      float cardW = lw - 62 * s;
      fitText(x + 24 * s, cy + 9 * s, cardW, 17 * s, 13 * s, tag + c.title, cards[i].story ? C_WARN : C_TEXT);
      std::string sub = fmt("%s  %s > %s  %.0f km", contractTypeName(c.type), g_world.airports[c.from].code, g_world.airports[c.to].code, g_world.distanceKm(c.from, c.to));
      float payW = g_ren.text(x + lw - 24 * s, cy + 37 * s, 16 * s, fmtMoney(c.payout), C_GOOD, 1, 2);
      g_ren.text(x + 24 * s, cy + 37 * s, 14 * s, ellipsize(sub, cardW - payW - 12 * s, 14 * s), C_DIM, 1);
    }
    cy += chh + 8 * s;
  }
  // details
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  Card cd = cards[selContract];
  Contract free;
  if (cd.free) {
    free.id = "FREE"; free.title = "Free Flight"; free.type = CT_FERRY; free.from = career.location;
    if (freeDest == career.location) freeDest = (freeDest + 1) % g_world.airports.size();
    free.to = freeDest; free.payout = 0; free.minLicense = LIC_STUDENT;
    free.brief = "Take any aircraft you can fly to any airport. Great for sightseeing, practising landings, or moving your own aircraft. Rental fees still apply.";
    free.wx.timeOfDay = 8.f + fmodf(realTime * 0.0f + career.flights * 3.7f, 11.f);
    free.wx.windFrom = (float)((career.flights * 77) % 360); free.wx.windSpeed = 3.f; free.wx.cloudCover = 0.35f;
    cd.c = &free;
  }
  const Contract& c = *cd.c;
  float px = dx + 22 * s, py = y + 18 * s, iw = dw - 44 * s;
  float mapW = std::min(iw * 0.42f, h * 0.48f);
  float textW = iw - mapW - 20 * s;
  {
    auto tl = wrap(c.title, textW, 24 * s);
    if (tl.size() > 2) { tl.resize(2); tl[1] = ellipsize(tl[1] + " ...", textW, 24 * s); }
    for (auto& l : tl) { g_ren.text(px, py, 24 * s, l, cd.story ? C_WARN : C_TEXT, 1); py += 31 * s; }
    py += 8 * s;
  }
  for (auto& l : wrap(c.brief, textW, 16 * s)) { g_ren.text(px, py, 16 * s, l, C_TEXT, 0.92f); py += 22 * s; }
  py += 10 * s;
  auto row = [&](const std::string& k, const std::string& v, vec3 col = C_TEXT) {
    g_ren.text(px, py, 15 * s, k, C_DIM, 1);
    auto vl = wrap(v, textW - 130 * s, 15 * s);
    for (size_t i = 0; i < vl.size(); i++) { g_ren.text(px + 130 * s, py, 15 * s, vl[i], col, 1); py += (i + 1 < vl.size() ? 19 : 23) * s; }
    if (vl.empty()) py += 23 * s;
  };
  if (cd.free) {
    if (button(px + 130 * s, py - 4 * s, 34 * s, 28 * s, "<")) { do freeDest = (freeDest + (int)g_world.airports.size() - 1) % g_world.airports.size(); while (freeDest == career.location); }
    g_ren.text(px, py, 15 * s, "Destination", C_DIM, 1);
    g_ren.text(px + 172 * s, py + 1 * s, 15 * s, ellipsize(fmt("%s %s", g_world.airports[freeDest].code, g_world.airports[freeDest].name), textW - 172 * s - 48 * s, 15 * s), C_ACCENT, 1);
    if (button(px + textW - 40 * s, py - 4 * s, 34 * s, 28 * s, ">")) { do freeDest = (freeDest + 1) % g_world.airports.size(); while (freeDest == career.location); }
    py += 30 * s;
  }
  const Airport& A = g_world.airports[c.from]; const Airport& B = g_world.airports[c.to];
  row("Route", fmt("%s %s  >  %s %s", A.code, A.name, B.code, B.name));
  row("Distance", fmt("%.1f km", g_world.distanceKm(c.from, c.to)));
  row("Destination", fmt("Rwy %02d/%02d, %.0f m %s, elev %s", B.rwyNumber(false), B.rwyNumber(true), B.length, surfaceName(B.surface), fmtAlt(B.elev).c_str()));
  if (c.cargoKg || c.pax) row("Load", c.pax ? fmt("%d passengers, %d kg", c.pax, c.cargoKg) : fmt("%d kg cargo%s", c.cargoKg, c.fragile ? " (FRAGILE)" : ""));
  if (c.timeLimitMin > 0) row("Deadline", fmt("%.0f minutes", c.timeLimitMin), C_BAD);
  row("Weather", c.wx.describe());
  if (c.payout) row("Payment", fmtMoney(c.payout), C_GOOD);
  if (c.ownedOnly) row("Requirement", "Your own aircraft", C_ACCENT);
  if (c.grantLicense > career.license) row("Reward", std::string("Earns ") + licenseName(c.grantLicense), C_ACCENT);
  if (c.from != career.location && c.type != CT_LESSON) { int pc = career.positioningCost(c); row("Positioning", pc ? fmt("Airline ticket to %s: %s", A.code, fmtMoney(pc).c_str()) : "Free courtesy ride", C_DIM); }
  drawMapView(dx + dw - mapW - 22 * s, y + 60 * s, mapW, mapW, c.from, c.to, &c.wps);
  // aircraft selection
  py = std::max(py + 8 * s, y + 70 * s + mapW);
  header(px, py, iw, "CHOOSE AIRCRAFT"); py += 26 * s;
  int firstOk = -1;
  float rowH = 30 * s;
  float colW = (iw - 10 * s) * 0.5f;
  for (int i = 0; i < kNumAircraft; i++) {
    std::string why;
    auto src = career.canFly(c, i, &why);
    if (c.type == CT_FERRY && src == Career::SRC_NONE && career.license == LIC_STUDENT && i == 0) src = Career::SRC_LESSON;
    if (src != Career::SRC_NONE && firstOk < 0) firstOk = i;
    float rx = px + (i % 2) * (colW + 10 * s), ry = py + (i / 2) * (rowH + 6 * s);
    if (ry + rowH > y + h - 70 * s) break;
    bool sel = selAircraft == i;
    bool hov = hovered(rx, ry, colW, rowH) && src != Career::SRC_NONE;
    card(rx, ry, colW, rowH, sel, hov, src == Career::SRC_NONE ? C_DIM * 0.4f : src == Career::SRC_OWNED ? C_GOOD : C_ACCENT);
    if (hov && in.mPressed[0]) { selAircraft = i; g_audio.trigger(SFX_CLICK); }
    g_ren.text(rx + 10 * s, ry + 7 * s, 15 * s, kAircraft[i].name, src == Career::SRC_NONE ? C_DIM * 0.6f : C_TEXT, 1);
    std::string st;
    if (src == Career::SRC_LESSON) st = "School aircraft";
    else if (src == Career::SRC_OWNED) { int fc = career.ferryCost(c, i); st = fc ? fmt("Owned (ferry %s)", fmtMoney(fc).c_str()) : "Owned"; }
    else if (src == Career::SRC_RENT) st = fmt("Rent %s", fmtMoney(kAircraft[i].rentFee).c_str());
    else st = why;
    float sts = 12.5f * s;
    while (g_ren.textWidth(st, sts) > colW * 0.55f && st.size() > 4) st = st.substr(0, st.size() - 4) + "...";
    g_ren.text(rx + colW - (sel ? 22 : 10) * s, ry + 9 * s, sts, st, src == Career::SRC_NONE ? C_BAD * 0.8f : src == Career::SRC_OWNED ? C_GOOD : C_WARN, 1, 2);
  }
  if (selAircraft >= 0) {
    auto src = career.canFly(c, selAircraft);
    if (c.type == CT_FERRY && src == Career::SRC_NONE && career.license == LIC_STUDENT && selAircraft == 0) src = Career::SRC_LESSON;
    if (src == Career::SRC_NONE) selAircraft = -1;
  }
  if (selAircraft < 0) selAircraft = firstOk;
  bool can = selAircraft >= 0;
  if (button(dx + dw - 262 * s, y + h - 62 * s, 240 * s, 46 * s, can ? "FLY!" : "No suitable aircraft", can, can)) {
    auto src = career.canFly(c, selAircraft);
    if (c.type == CT_FERRY && src == Career::SRC_NONE) src = Career::SRC_LESSON;
    if (c.type == CT_FERRY && src == Career::SRC_RENT) {}
    Contract go = c;
    if (go.type == CT_FERRY) { go.from = career.location; }
    startFlight(go, selAircraft, src);
  }
  if (!can) g_ren.text(px, y + h - 50 * s, 14 * s, "Tip: check the Hangar to buy an aircraft, or earn your next licence through the story.", C_DIM, 1);
}

void Game::drawHubHangar(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.34f, 420 * s);
  panel(x, y, lw, h);
  float cy = y + 16 * s;
  header(x + 16 * s, cy, lw - 32 * s, "AIRCRAFT MARKET"); cy += 30 * s;
  for (int i = 0; i < kNumAircraft; i++) {
    float chh = 54 * s;
    bool sel = selHangar == i, hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    card(x + 10 * s, cy, lw - 20 * s, chh, sel, hov, C_ACCENT);
    if (hov && in.mPressed[0]) { selHangar = i; g_audio.trigger(SFX_CLICK); }
    bool owned = career.ownedIndexFor(i) >= 0;
    g_ren.text(x + 24 * s, cy + 8 * s, 17 * s, kAircraft[i].name, C_TEXT, 1);
    g_ren.text(x + 24 * s, cy + 31 * s, 13 * s, kAircraft[i].role, C_DIM, 1);
    g_ren.text(x + lw - (sel ? 34 : 24) * s, cy + 18 * s, 15 * s, owned ? "OWNED" : fmtMoney(kAircraft[i].price), owned ? C_GOOD : C_WARN, 1, 2);
    cy += chh + 8 * s;
  }
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  const AircraftSpec& a = kAircraft[selHangar];
  float px = dx + 24 * s, py = y + 20 * s;
  fitText(px, py, dw - 48 * s, 30 * s, 18 * s, a.name, C_TEXT); py += 42 * s;
  g_ren.text(px, py, 17 * s, ellipsize(a.role, dw - 48 * s, 17 * s), C_ACCENT, 1); py += 30 * s;
  header(px, py, dw - 48 * s, "SPECIFICATIONS"); py += 26 * s;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 200 * s, py, 16 * s, ellipsize(v, dw - 248 * s, 16 * s), C_TEXT, 1); py += 26 * s; };
  const char* et[] = {"Piston", "Turboprop", "Turbofan"};
  row("Engines", fmt("%d x %s%s", a.engines, et[a.engineType], a.engineType == ENG_PISTON ? fmt(" (%d-cyl)", a.cylinders).c_str() : ""));
  row("Cruise speed", fmtSpeed(a.cruise));
  row("Range", fmt("%.0f km", a.rangeKm));
  row("Passengers", fmt("%d", a.pax));
  row("Cargo", fmt("%.0f kg", a.cargoKg));
  row("Runway needed", fmt("%.0f m at sea level%s", a.runwayM, a.roughOK ? ", gravel/snow OK" : a.runwayM < 900 ? ", grass OK" : ", paved only"));
  row("Landing gear", a.taildragger ? "Taildragger" : a.retract ? "Retractable tricycle" : "Fixed tricycle");
  row("Licence", licenseName(a.license));
  row("Price", fmtMoney(a.price));
  row("Rental", a.rentFee ? fmtMoney(a.rentFee) + " per flight" : "Not available for rent");
  py += 14 * s;
  int oi = career.ownedIndexFor(selHangar);
  if (oi < 0) {
    bool can = career.money >= a.price && career.license >= a.license;
    if (button(px, py, 240 * s, 46 * s, fmt("Buy for %s", fmtMoney(a.price).c_str()), can, can)) {
      std::string m; if (career.buy(selHangar, &m)) { g_audio.trigger(SFX_CASH); saveGame(); } hubMsg = m; hubMsgTime = 4;
    }
    if (!can) g_ren.text(px + 260 * s, py + 14 * s, 15 * s, career.license < a.license ? std::string("Requires ") + licenseName(a.license) : "Not enough money", C_BAD, 1);
  } else {
    g_ren.text(px, py, 16 * s, fmt("Parked at %s", g_world.airports[career.fleet[oi].location].name), C_GOOD, 1);
    py += 30 * s;
    if (button(px, py, 240 * s, 46 * s, fmt("Sell for %s", fmtMoney(a.price * 7 / 10).c_str()))) {
      std::string m; if (career.sell(oi, &m)) { g_audio.trigger(SFX_CASH); saveGame(); } hubMsg = m; hubMsgTime = 4;
    }
  }
  py += 70 * s;
  header(px, py, dw - 48 * s, "YOUR FLEET"); py += 26 * s;
  if (career.fleet.empty()) g_ren.text(px, py, 15 * s, "You don't own any aircraft yet. Rentals are available everywhere.", C_DIM, 1);
  for (auto& f : career.fleet) { g_ren.text(px, py, 15 * s, ellipsize(fmt("%s  -  at %s", kAircraft[f.spec].name, g_world.airports[f.location].code), dw - 48 * s, 15 * s), C_TEXT, 1); py += 22 * s; }
}

void Game::drawHubLogbook(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.4f, 500 * s);
  panel(x, y, lw, h);
  float px = x + 24 * s, py = y + 20 * s;
  g_ren.text(px, py, 26 * s, "Pilot Logbook", C_TEXT, 1); py += 40 * s;
  header(px, py, lw - 48 * s, "RECORD"); py += 26 * s;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 220 * s, py, 16 * s, ellipsize(v, lw - 268 * s, 16 * s), C_TEXT, 1); py += 27 * s; };
  row("Licence", licenseName(career.license));
  row("Flights", fmt("%d", career.flights));
  row("Successful landings", fmt("%d", career.landings));
  row("Accidents", fmt("%d", career.crashes));
  row("Flight hours", fmt("%.1f", career.hours));
  row("Softest landing", career.bestLandingFpm < 9000 ? fmt("%.0f fpm", career.bestLandingFpm) : "-");
  row("Reputation", fmt("%d", career.reputation));
  row("Fleet", fmt("%d aircraft", (int)career.fleet.size()));
  py += 20 * s;
  header(px, py, lw - 48 * s, "LICENCES"); py += 26 * s;
  const char* unl[] = {"Kestrel trainer (school)", "Kestrel & Wren rentals, cargo work", "Bushmaster, Islander, Pelican; passengers; buying aircraft", "Meridian airliner & Starling jet"};
  for (int l = 0; l < 4; l++) {
    bool got = career.license >= l;
    g_ren.rectOutline(px, py + 2 * s, 12 * s, 12 * s, got ? C_GOOD : C_DIM, 0.8f, 2 * s, 1.5f * s);
    if (got) g_ren.rect(px + 3 * s, py + 5 * s, 6 * s, 6 * s, C_GOOD, 1, 1 * s);
    g_ren.text(px + 22 * s, py, 14 * s, licenseName(l), got ? C_GOOD : C_DIM, 1); py += 19 * s;
    g_ren.text(px + 22 * s, py, 13 * s, ellipsize(unl[l], lw - 70 * s, 13 * s), C_DIM, 0.8f); py += 22 * s;
  }
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  px = dx + 24 * s; py = y + 20 * s;
  g_ren.text(px, py, 22 * s, "Story progress", C_TEXT, 1); py += 32 * s;
  {
    float frac = g_story.empty() ? 0.f : (float)career.storyIndex / g_story.size();
    float bw = dw - 48 * s, f = anim(0x51a7u, frac, 4);
    g_ren.rect(px, py, bw, 6 * s, C_ACCENT, 0.12f, 3 * s);
    g_ren.glow(px, py, bw * f, 6 * s, C_ACCENT, 0.4f, 3 * s, 6 * s);
    g_ren.rectGrad(px, py, std::max(bw * f, 6 * s), 6 * s, C_ACCENT, C_ACCENT * 0.6f, 1, 3 * s);
    g_ren.text(px + bw, py - 24 * s, 12 * s, fmt("%d / %d MISSIONS", career.storyIndex, (int)g_story.size()), C_DIM, 1, 2, false);
    py += 18 * s;
  }
  const char* chapters[] = {"Flight School", "Private Pilot", "Commercial Pilot", "Owner-Operator", "Airline Captain"};
  int lastCh = -1;
  float colX = px;
  for (int i = 0; i < (int)g_story.size(); i++) {
    const Contract& c = g_story[i];
    if (c.chapter != lastCh) { if (py > y + h - 60 * s) { colX += dw * 0.5f; py = y + 76 * s; } lastCh = c.chapter; py += 6 * s; header(colX, py, dw * 0.5f - 40 * s, chapters[c.chapter]); py += 22 * s; }
    if (py > y + h - 30 * s) { colX += dw * 0.5f; py = y + 76 * s; }
    bool done = i < career.storyIndex, cur = i == career.storyIndex;
    vec3 mc = done ? C_GOOD : cur ? C_ACCENT : C_DIM * 0.5f;
    if (cur) g_ren.glow(colX + 12 * s, py + 3 * s, 9 * s, 9 * s, C_ACCENT, 0.4f + 0.3f * sinf(realTime * 4.f), 4.5f * s, 6 * s);
    g_ren.rect(colX + 12 * s, py + 3 * s, 9 * s, 9 * s, mc, done || cur ? 1.f : 0.5f, done ? 1.5f * s : 4.5f * s);
    g_ren.text(colX + 28 * s, py, 13.5f * s, ellipsize(c.title, dw * 0.5f - 52 * s, 13.5f * s), done ? C_GOOD * 0.9f : cur ? C_TEXT : C_DIM * 0.7f, 1);
    py += 19 * s;
  }
}

void Game::drawSettings(float x, float y, float w, float h) {
  float s = S();
  float py = y;
  g_ren.text(x, py, 24 * s, "Settings", C_TEXT, 1); py += 36 * s;
  header(x, py, std::min(w, 620 * S()), "DISPLAY / AUDIO / CONTROLS"); py += 28 * s;
  auto slider = [&](const std::string& label, float& v, float lo, float hi, float step, const std::string& disp) {
    g_ren.text(x, py + 6 * s, 16 * s, label, C_DIM, 1);
    if (button(x + 250 * s, py, 36 * s, 32 * s, "-")) v = clampf(v - step, lo, hi);
    bool hv = hovered(x + 296 * s, py, 200 * s, 32 * s);
    float hk = anim(uid(x, py, label), hv ? 1.f : 0.f, 14), f = (v - lo) / (hi - lo);
    g_ren.rect(x + 296 * s, py + 14 * s, 200 * s, 4 * s, C_ACCENT, 0.15f, 2 * s);
    for (int k = 0; k <= 10; k++) g_ren.rect(x + 296 * s + k * 20 * s, py + 22 * s, 1 * s, k % 5 ? 3 * s : 5 * s, C_DIM, 0.5f);
    g_ren.rectGrad(x + 296 * s, py + 14 * s, std::max(200 * s * f, 4 * s), 4 * s, C_ACCENT * 0.6f, C_ACCENT, 1, 2 * s);
    float kx = x + 296 * s + 200 * s * f;
    g_ren.glow(kx - 6 * s, py + 10 * s, 12 * s, 12 * s, C_ACCENT, 0.3f + 0.4f * hk, 6 * s, 8 * s);
    g_ren.rect(kx - 6 * s, py + 10 * s, 12 * s, 12 * s, mixc(C_ACCENT, vec3(1, 1, 1), 0.3f * hk), 1, 6 * s);
    if (hv && in.mDown[0]) v = lo + clampf((in.mx - x - 296 * s) / (200 * s), 0, 1) * (hi - lo);
    if (button(x + 506 * s, py, 36 * s, 32 * s, "+")) v = clampf(v + step, lo, hi);
    g_ren.text(x + 556 * s, py + 6 * s, 16 * s, disp, C_TEXT, 1);
    py += 42 * s;
  };
  auto toggle = [&](const std::string& label, bool& v, const char* on, const char* off) {
    g_ren.text(x, py + 6 * s, 16 * s, label, C_DIM, 1);
    if (button(x + 250 * s, py, 200 * s, 32 * s, v ? on : off, true, v)) v = !v;
    py += 42 * s;
  };
  float rs = set.renderScale;
  slider("Render resolution", set.renderScale, 0.4f, 1.0f, 0.05f, fmt("%.0f%%", set.renderScale * 100));
  if (fabsf(rs - set.renderScale) > 1e-4f) { g_ren.renderScale = set.renderScale; g_ren.resize(g_ren.W, g_ren.H); }
  g_ren.text(x, py + 6 * s, 16 * s, "Ray tracing quality", C_DIM, 1);
  const char* q[] = {"Low", "Medium", "High"};
  for (int i = 0; i < 3; i++) if (button(x + 250 * s + i * 100 * s, py, 92 * s, 32 * s, q[i], true, set.quality == i)) { set.quality = i; g_ren.quality = i; }
  py += 42 * s;
  slider("Master volume", set.master, 0, 1, 0.05f, fmt("%.0f%%", set.master * 100));
  slider("Engine volume", set.engineVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.engineVol * 100));
  slider("Effects volume", set.sfxVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.sfxVol * 100));
  float rv = set.radioVol;
  slider("Radio volume", set.radioVol, 0, 1, 0.05f, fmt("%.0f%%", set.radioVol * 100));
  if (rv != set.radioVol) radio.setVolume(set.radioVol);
  slider("Mouse sensitivity", set.mouseSens, 0.2f, 3.f, 0.1f, fmt("%.1f", set.mouseSens));
  toggle("Pitch control", set.invertPitch, "Inverted", "Normal (S = nose up)");
  toggle("Units", set.metric, "Metric", "Aviation (kt / ft)");
  toggle("Instructor hints", set.showHints, "Shown", "Hidden");
  bool fs = set.fullscreen;
  toggle("Display", set.fullscreen, "Fullscreen", "Windowed");
  if (fs != set.fullscreen) wantFullscreenToggle = true;
  py += 6 * s;
  g_ren.text(x, py, 14 * s, "Settings are saved automatically. Edit radio_stations.txt in the save folder to add stations.", C_DIM, 0.8f);
  saveSettings();
  (void)w; (void)h;
}

void Game::drawRadioPanel(float x, float y) {
  float s = S();
  const int rows = std::min((int)stations.size(), std::max(4, (int)((g_ren.H - y - 140 * s) / (34 * s))));
  const int visible = std::min(rows, 12);
  float w = 440 * s, h = (118 + 34 * visible) * s;
  panel(x, y, w, h, 0.92f);
  g_ren.text(x + 18 * s, y + 14 * s, 20 * s, "Internet Radio", C_TEXT, 1);
  if (radio.state() == Radio::PLAYING)  // live equaliser bars
    for (int i = 0; i < 12; i++) { float bh = (0.3f + 0.7f * fabsf(sinf(realTime * (3.1f + i * 0.7f) + i * 1.3f))) * 16 * s; g_ren.rect(x + w - 120 * s + i * 8 * s, y + 34 * s - bh, 5 * s, bh, C_ACCENT, 0.8f, 1 * s); }
  vec3 sc = radio.state() == Radio::PLAYING ? C_GOOD : radio.state() == Radio::FAILED ? C_BAD : C_DIM;
  g_ren.text(x + 18 * s, y + 44 * s, 14 * s, ellipsize(radio.status(), w - 36 * s, 14 * s), sc, 1);
  // scrolling station list (mouse wheel over the list, or the arrows)
  int maxScroll = std::max(0, (int)stations.size() - visible);
  float listY = y + 72 * s, listH = visible * 34 * s;
  if (hovered(x, listY, w, listH) && in.wheel != 0) { radioScroll -= (int)in.wheel; in.wheel = 0; }
  radioScroll = std::clamp(radioScroll, 0, maxScroll);
  float py = listY;
  float bw = w - (maxScroll > 0 ? 50 : 28) * s;
  for (int k = 0; k < visible; k++) {
    int i = radioScroll + k;
    if (i >= (int)stations.size()) break;
    bool cur = i == set.radioStation && radio.state() != Radio::IDLE;
    if (button(x + 14 * s, py, bw, 30 * s, ellipsize(stations[i].first, bw - 20 * s, std::min(30 * s * 0.46f, 18 * s)), true, cur)) {
      set.radioStation = i; radio.setVolume(set.radioVol); radio.play(stations[i].second); saveSettings();
    }
    py += 34 * s;
  }
  if (maxScroll > 0) {  // scrollbar
    float sx = x + w - 28 * s;
    if (button(sx, listY, 18 * s, 22 * s, "^")) radioScroll = std::max(0, radioScroll - 3);
    if (button(sx, listY + listH - 26 * s, 18 * s, 22 * s, "v")) radioScroll = std::min(maxScroll, radioScroll + 3);
    float ty = listY + 26 * s, th = listH - 56 * s, kh = std::max(20 * s, th * visible / stations.size());
    g_ren.rect(sx + 7 * s, ty, 4 * s, th, C_ACCENT, 0.15f, 2 * s);
    g_ren.rect(sx + 5 * s, ty + (th - kh) * radioScroll / maxScroll, 8 * s, kh, C_ACCENT, 0.8f, 4 * s);
  }
  if (button(x + 14 * s, py + 4 * s, 120 * s, 30 * s, "Stop")) radio.stop();
  if (button(x + 150 * s, py + 4 * s, 40 * s, 30 * s, "-")) { set.radioVol = clampf(set.radioVol - 0.1f, 0, 1); radio.setVolume(set.radioVol); }
  g_ren.text(x + 200 * s, py + 10 * s, 15 * s, fmt("Vol %.0f%%", set.radioVol * 100), C_TEXT, 1);
  if (button(x + 290 * s, py + 4 * s, 40 * s, 30 * s, "+")) { set.radioVol = clampf(set.radioVol + 0.1f, 0, 1); radio.setVolume(set.radioVol); }
  g_ren.text(x + w - 14 * s, py + 12 * s, 11 * s, fmt("%d STATIONS", (int)stations.size()), C_DIM, 0.8f, 2, false);
}

// ------------------------------------------------------------------ HUD
void Game::drawPFD(float x, float y, float sz) {
  float s = S();
  float r = sz * 0.5f, cx = x + r, cy = y + r;
  float pitch = plane.pitchDeg(), bank = plane.bankDeg();
  float ppd = sz / 50.f;  // pixels per degree
  float b = bank * DEG;
  vec2 n(-sinf(-b), cosf(-b));  // horizon normal (towards ground) in screen space
  vec2 t(n.y, -n.x);
  vec2 hc(cx + n.x * pitch * ppd, cy + n.y * pitch * ppd);
  g_ren.rect(x, y, sz, sz, vec3(0.16f, 0.42f, 0.78f), 0.85f, r);
  // ground fill as scanlines parallel to the horizon
  float d0 = (hc.x - cx) * n.x + (hc.y - cy) * n.y;
  for (float d = std::max(d0, -r); d < r; d += 2.5f * s) {
    float half = sqrtf(std::max(0.f, r * r - d * d));
    vec2 c(cx + n.x * d, cy + n.y * d);
    g_ren.line(c.x - t.x * half, c.y - t.y * half, c.x + t.x * half, c.y + t.y * half, 3.2f * s, vec3(0.45f, 0.3f, 0.15f), 0.92f);
  }
  // pitch ladder
  for (int p = -30; p <= 30; p += 5) {
    if (p == 0) continue;
    float off = (pitch - p) * ppd;
    vec2 c(cx + n.x * off, cy + n.y * off);
    if ((c.x - cx) * (c.x - cx) + (c.y - cy) * (c.y - cy) > r * r * 0.7f) continue;
    float hw = (p % 10 == 0 ? 0.22f : 0.12f) * sz;
    g_ren.line(c.x - t.x * hw, c.y - t.y * hw, c.x + t.x * hw, c.y + t.y * hw, 1.5f * s, vec3(1, 1, 1), 0.85f);
    if (p % 10 == 0) g_ren.text(c.x + t.x * (hw + 12 * s), c.y + t.y * (hw + 12 * s) - 6 * s, 11 * s, fmt("%d", abs(p)), vec3(1, 1, 1), 0.9f, 1, false);
  }
  float d = d0;
  if (fabsf(d) < r) { float half = sqrtf(r * r - d * d); g_ren.line(hc.x - t.x * half, hc.y - t.y * half, hc.x + t.x * half, hc.y + t.y * half, 2 * s, vec3(1, 1, 1), 1); }
  // aircraft symbol
  vec3 yel(1.f, 0.85f, 0.1f);
  g_ren.line(cx - sz * 0.3f, cy, cx - sz * 0.1f, cy, 4 * s, yel, 1); g_ren.line(cx + sz * 0.1f, cy, cx + sz * 0.3f, cy, 4 * s, yel, 1);
  g_ren.rect(cx - 3 * s, cy - 3 * s, 6 * s, 6 * s, yel, 1, 3 * s);
  // bank pointer
  vec2 bp(cx + sinf(b) * (r - 10 * s), cy - cosf(b) * (r - 10 * s));
  g_ren.rect(bp.x - 4 * s, bp.y - 4 * s, 8 * s, 8 * s, vec3(1, 1, 1), 1, 4 * s);
  for (int a : {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60}) {
    float aa = a * DEG; vec2 p0(cx + sinf(aa) * r, cy - cosf(aa) * r), p1(cx + sinf(aa) * (r - 7 * s), cy - cosf(aa) * (r - 7 * s));
    g_ren.line(p0.x, p0.y, p1.x, p1.y, 2 * s, vec3(1, 1, 1), 0.8f);
  }
  // speed tape (left) and altitude tape (right)
  float ias = plane.ias, alt = plane.pos.y;
  float tw = 74 * s, th = sz;
  float lx = x - tw - 8 * s, rx = x + sz + 8 * s;
  hudPanel(lx, y, tw, th);
  hudPanel(rx, y, tw + 10 * s, th);
  g_ren.glow(x, y, sz, sz, C_ACCENT, 0.18f, r, 8 * s);
  float spdU = set.metric ? ias * 3.6f : ias * MS_TO_KT;
  float altU = set.metric ? alt : alt * M_TO_FT;
  float sppx = th / 80.f, alpx = th / (set.metric ? 300.f : 1000.f);
  for (int v = (int)(spdU / 10) * 10 - 40; v <= spdU + 40; v += 10) {
    if (v < 0) continue;
    float yy = cy - (v - spdU) * sppx;
    if (yy < y + 6 * s || yy > y + th - 6 * s) continue;
    g_ren.line(lx + tw - 12 * s, yy, lx + tw, yy, 1.5f * s, vec3(1, 1, 1), 0.8f);
    if (v % 20 == 0) g_ren.text(lx + tw - 16 * s, yy - 7 * s, 13 * s, fmt("%d", v), vec3(1, 1, 1), 0.9f, 2, false);
  }
  // speed bands: stall / Vfe
  float vs0 = plane.spec->vref / 1.3f * (set.metric ? 3.6f : MS_TO_KT);
  float yStall = cy - (vs0 - spdU) * sppx;
  if (yStall < y + th) g_ren.rect(lx + tw - 5 * s, std::max(yStall, y), 5 * s, y + th - std::max(yStall, y), C_BAD, 0.9f);
  int step = set.metric ? 50 : 100;
  for (int v = ((int)altU / step) * step - step * 6; v <= altU + step * 6; v += step) {
    float yy = cy - (v - altU) * alpx;
    if (yy < y + 6 * s || yy > y + th - 6 * s) continue;
    g_ren.line(rx, yy, rx + 12 * s, yy, 1.5f * s, vec3(1, 1, 1), 0.8f);
    if (v % (step * 5) == 0) g_ren.text(rx + 16 * s, yy - 7 * s, 13 * s, fmt("%d", v), vec3(1, 1, 1), 0.9f, 0, false);
  }
  g_ren.rect(lx - 4 * s, cy - 15 * s, tw + 8 * s, 30 * s, vec3(0, 0.02f, 0.04f), 0.95f, 4 * s); g_ren.rectOutline(lx - 4 * s, cy - 15 * s, tw + 8 * s, 30 * s, C_ACCENT, 0.7f, 4 * s, 1.5f * s);
  g_ren.text(lx + tw - 6 * s, cy - 11 * s, 20 * s, fmt("%.0f", spdU), vec3(1, 1, 1), 1, 2, false);
  g_ren.rect(rx - 4 * s, cy - 15 * s, tw + 18 * s, 30 * s, vec3(0, 0.02f, 0.04f), 0.95f, 4 * s); g_ren.rectOutline(rx - 4 * s, cy - 15 * s, tw + 18 * s, 30 * s, C_ACCENT, 0.7f, 4 * s, 1.5f * s);
  g_ren.text(rx + 6 * s, cy - 11 * s, 20 * s, fmt("%.0f", altU), vec3(1, 1, 1), 1, 0, false);
  g_ren.text(lx + tw * 0.5f, y - 20 * s, 13 * s, set.metric ? "KM/H" : "KT", C_DIM, 1, 1);
  g_ren.text(rx + tw * 0.5f, y - 20 * s, 13 * s, set.metric ? "M" : "FT", C_DIM, 1, 1);
  // vertical speed
  float vsU = set.metric ? plane.vel.y : plane.vel.y * 196.85f;
  g_ren.text(rx + tw * 0.5f + 5 * s, y + th + 6 * s, 14 * s, set.metric ? fmt("VS %+.1f m/s", vsU) : fmt("VS %+.0f", vsU), fabsf(plane.vel.y) > 6 ? C_WARN : C_TEXT, 1, 1);
  // heading
  float hdg = plane.heading();
  hudPanel(cx - 40 * s, y + sz + 6 * s, 80 * s, 26 * s);
  g_ren.text(cx, y + sz + 10 * s, 17 * s, fmt("%03.0f", hdg), vec3(1, 1, 1), 1, 1, false);
  // AGL
  float agl = plane.agl();
  if (agl < 750) g_ren.text(lx + tw * 0.5f, y + th + 6 * s, 14 * s, fmt("RA %s", fmtAlt(agl).c_str()), agl < 60 ? C_WARN : C_TEXT, 1, 1);
}

void Game::drawMinimap(float x, float y, float sz, float range) {
  float s = S();
  g_ren.rect(x - 4 * s, y - 4 * s, sz + 8 * s, sz + 8 * s, vec3(0, 0, 0), 0.6f, 8 * s);
  float u0 = (plane.pos.x - range + WORLD_HALF) / (2 * WORLD_HALF), v0 = (plane.pos.z - range + WORLD_HALF) / (2 * WORLD_HALF);
  float u1 = (plane.pos.x + range + WORLD_HALF) / (2 * WORLD_HALF), v1 = (plane.pos.z + range + WORLD_HALF) / (2 * WORLD_HALF);
  g_ren.image(g_ren.minimapTex, x, y, sz, sz, u0, v0, u1, v1, 0.95f);
  g_ren.flushUIPublic();
  auto toS = [&](float wx_, float wz, bool& inside) {
    float px = x + (wx_ - plane.pos.x + range) / (2 * range) * sz, py = y + (wz - plane.pos.z + range) / (2 * range) * sz;
    inside = px > x && px < x + sz && py > y && py < y + sz;
    return vec2(clampf(px, x, x + sz), clampf(py, y, y + sz));
  };
  bool in1;
  vec2 c(x + sz * 0.5f, y + sz * 0.5f);
  vec3 target = wpIndex < (int)contract.wps.size() ? vec3(contract.wps[wpIndex].x, 0, contract.wps[wpIndex].z) : dest().pos();
  vec2 tp = toS(target.x, target.z, in1);
  g_ren.line(c.x, c.y, tp.x, tp.y, 2.5f * s, vec3(1.f, 0.3f, 1.f), 0.9f);
  for (auto& a : g_world.airports) {
    bool ins; vec2 p = toS(a.x, a.z, ins);
    if (!ins) continue;
    vec3 dir = a.dir();
    float k = sz / (2 * range);
    g_ren.line(p.x - dir.x * a.length * 0.5f * k, p.y - dir.z * a.length * 0.5f * k, p.x + dir.x * a.length * 0.5f * k, p.y + dir.z * a.length * 0.5f * k, 3 * s, &a == &dest() ? C_ACCENT : vec3(1, 1, 1), 1);
    g_ren.text(p.x + 6 * s, p.y + 4 * s, 11 * s, a.code, &a == &dest() ? C_ACCENT : vec3(1, 1, 1), 1);
  }
  for (int i = wpIndex; i < (int)contract.wps.size(); i++) { bool ins; vec2 p = toS(contract.wps[i].x, contract.wps[i].z, ins); if (ins) g_ren.rect(p.x - 4 * s, p.y - 4 * s, 8 * s, 8 * s, vec3(0.3f, 1, 0.5f), 1, 4 * s); }
  float h = plane.heading() * DEG;
  vec2 f(sinf(h), -cosf(h)), rr(cosf(h), sinf(h));
  float a = 9 * s;
  g_ren.line(c.x + f.x * a, c.y + f.y * a, c.x - f.x * a + rr.x * a * 0.7f, c.y - f.y * a + rr.y * a * 0.7f, 2.5f * s, vec3(1, 1, 0.2f), 1);
  g_ren.line(c.x + f.x * a, c.y + f.y * a, c.x - f.x * a - rr.x * a * 0.7f, c.y - f.y * a - rr.y * a * 0.7f, 2.5f * s, vec3(1, 1, 0.2f), 1);
  g_ren.text(x + sz * 0.5f, y + 4 * s, 12 * s, "N", vec3(1, 1, 1), 0.9f, 1);
}

void Game::drawHud(const FrameParams& fp) {
  (void)fp;
  if (!plane.spec) return;
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  if (!hudOn) return;
  { auto it = uiAnim.find(0x6e61u); if (showMap && it != uiAnim.end() && it->second > 0.6f) return; }  // GPS map covers the HUD
  const AircraftSpec& spc = *plane.spec;
  // mission bar
  const Airport& d = dest();
  vec3 target = wpIndex < (int)contract.wps.size() ? vec3(contract.wps[wpIndex].x, contract.wps[wpIndex].alt, contract.wps[wpIndex].z) : d.pos();
  vec3 to = target - plane.pos;
  float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  hudPanel(W * 0.5f - 300 * s, 8 * s, 600 * s, 54 * s);
  std::string obj = wpIndex < (int)contract.wps.size() ? fmt("Checkpoint %d/%d", wpIndex + 1, (int)contract.wps.size()) : fmt("Land at %s (%s)", d.code, d.name);
  if (researchFlight) obj = fmt("Free roam  -  Mach %.2f", plane.mach);
  g_ren.text(W * 0.5f, 13 * s, 16 * s, ellipsize(contract.title + "  -  " + obj, 570 * s, 16 * s), C_TEXT, 1, 1);
  std::string info = fmt("%.1f km  BRG %03.0f", dist / 1000.f, brg);
  if (gs > 10) info += fmt("  ETE %d:%02d", (int)(dist / gs) / 60, (int)(dist / gs) % 60);
  if (contract.timeLimitMin > 0) { float left = contract.timeLimitMin * 60 - flightClock; info += left > 0 ? fmt("  DEADLINE %d:%02d", (int)left / 60, (int)left % 60) : "  LATE!"; }
  if (timeAccel > 1) info += fmt("  TIME x%.0f", timeAccel);
  g_ren.text(W * 0.5f, 37 * s, 15 * s, info, C_ACCENT, 1, 1);
  // GPS arrow
  float rel = wrapAngle((brg - plane.heading()) * DEG);
  vec2 ac(W * 0.5f, 92 * s);
  float al = 24 * s;
  vec2 dv(sinf(rel), -cosf(rel)), pv(-dv.y, dv.x);
  vec3 mag(1.f, 0.35f, 1.f);
  g_ren.line(ac.x - dv.x * al, ac.y - dv.y * al, ac.x + dv.x * al, ac.y + dv.y * al, 5 * s, mag, 0.95f);
  g_ren.line(ac.x + dv.x * al, ac.y + dv.y * al, ac.x + dv.x * al * 0.3f + pv.x * al * 0.6f, ac.y + dv.y * al * 0.3f + pv.y * al * 0.6f, 5 * s, mag, 0.95f);
  g_ren.line(ac.x + dv.x * al, ac.y + dv.y * al, ac.x + dv.x * al * 0.3f - pv.x * al * 0.6f, ac.y + dv.y * al * 0.3f - pv.y * al * 0.6f, 5 * s, mag, 0.95f);
  if (camMode != 1) {
  // PFD
  float pfd = 210 * s;
  float px = 110 * s, py = H - pfd - 60 * s;
  if (camMode == 1) { px = W * 0.5f - 260 * s; py = H - pfd - 30 * s; }
  drawPFD(px, py, pfd);
  // approach guidance (glidepath + localiser)
  {
    float best = 1e9f; vec3 thr, ldir; bool found = false;
    for (int end = 0; end < 2; end++) {
      vec3 dir = end ? -d.dir() : d.dir();
      vec3 th = d.threshold(end == 1);
      vec3 rel3 = plane.pos - th;
      float along = -dot(vec3(rel3.x, 0, rel3.z), dir);
      if (along < 0) continue;
      float lat = fabsf(dot(vec3(rel3.x, 0, rel3.z), vec3(-dir.z, 0, dir.x)));
      if (along < 9000 && lat < along * 0.35f + 300 && along < best && (wpIndex >= (int)contract.wps.size())) { best = along; thr = th; ldir = dir; found = true; }
    }
    if (found && !plane.onGround && best > 100) {
      vec3 rel3 = plane.pos - thr;
      float along = best + 300.f;
      float ideal = d.elev + tanf(3.f * DEG) * along;
      float dev = (plane.pos.y - ideal) / std::max(along * 0.0122f, 8.f);  // dots
      float lat = dot(vec3(rel3.x, 0, rel3.z), vec3(-ldir.z, 0, ldir.x)) / std::max(along * 0.0175f, 10.f);
      float gx = px + pfd + 100 * s, gy = py + pfd * 0.5f;
      g_ren.rect(gx, py + 20 * s, 14 * s, pfd - 40 * s, vec3(0, 0, 0), 0.5f, 6 * s);
      g_ren.rect(gx + 2 * s, gy - 1 * s, 10 * s, 2 * s, vec3(1, 1, 1), 1);
      float dy = clampf(-dev, -2.5f, 2.5f) * (pfd - 40 * s) / 5.f;
      g_ren.rect(gx + 1 * s, gy - dy - 6 * s, 12 * s, 12 * s, vec3(1.f, 0.35f, 1.f), 1, 6 * s);
      g_ren.text(gx + 7 * s, py + 2 * s, 11 * s, "G/S", C_DIM, 1, 1);
      float lx = px + pfd * 0.5f, ly = py + pfd + 40 * s;
      g_ren.rect(lx - pfd * 0.4f, ly, pfd * 0.8f, 12 * s, vec3(0, 0, 0), 0.5f, 6 * s);
      float dx = clampf(-lat, -2.5f, 2.5f) * pfd * 0.4f / 2.5f;
      g_ren.rect(lx + dx - 6 * s, ly, 12 * s, 12 * s, vec3(1.f, 0.35f, 1.f), 1, 6 * s);
      std::string gp = dev > 1.f ? "HIGH" : dev < -1.f ? "LOW" : "ON GLIDEPATH";
      g_ren.text(gx + 7 * s, py + pfd - 12 * s, 11 * s, gp, dev < -1.5f ? C_BAD : C_TEXT, 1, 1);
    }
  }
  // engine & systems panel
  float ex = W - 300 * s, ey = H - 250 * s;
  if (camMode == 1) { ex = W * 0.5f + 160 * s; ey = H - 230 * s; }
  hudPanel(ex, ey - (plane.spec->special ? 22 * s : 0), 270 * s, 220 * s + (plane.spec->special ? 22 * s : 0));
  if (plane.spec->special) ey -= 22 * s;
  header(ex + 14 * s, ey + 10 * s, 242 * s, plane.spec->special ? "XR-9 SYSTEMS" : "SYSTEMS");
  float ty = ey + 34 * s;
  auto erow = [&](const std::string& k, const std::string& v, vec3 c = C_TEXT) { g_ren.text(ex + 14 * s, ty, 15 * s, k, C_DIM, 1); g_ren.text(ex + 256 * s, ty, 15 * s, v, c, 1, 2); ty += 22 * s; };
  const AircraftSpec& sp = *plane.spec;
  // throttle bar
  g_ren.text(ex + 14 * s, ty, 15 * s, "THR", C_DIM, 1);
  for (int k = 0; k < 20; k++) {  // segmented throttle bar
    bool on = (k + 0.5f) / 20.f < plane.ctl.throttle;
    g_ren.rect(ex + 70 * s + k * 7 * s, ty + 3 * s, 5 * s, 12 * s, on ? (k >= 17 ? C_WARN : C_ACCENT) : C_ACCENT, on ? 1.f : 0.12f, 1 * s);
  }
  g_ren.text(ex + 256 * s, ty, 15 * s, fmt("%.0f%%", plane.ctl.throttle * 100), C_TEXT, 1, 2); ty += 22 * s;
  if (sp.engineType == ENG_PISTON) erow("RPM", plane.engineRunning ? fmt("%.0f", plane.rpm) : (plane.starterTime > 0 ? "CRANKING" : "OFF"), plane.engineRunning ? C_TEXT : C_BAD);
  else erow("N1", plane.engineRunning ? fmt("%.1f%%", plane.n1) : (plane.starterTime > 0 ? fmt("START %.0f%%", plane.n1) : "OFF"), plane.engineRunning ? C_TEXT : C_BAD);
  float fuelFrac = plane.fuel / sp.maxFuel;
  if (sp.special) erow("FUEL", "RESEARCH CELL", C_ACCENT);
  else erow("FUEL", fmt("%.0f%%  ~%.0f km", fuelFrac * 100, plane.rangeLeftKm()), fuelFrac < 0.15f ? C_BAD : C_TEXT);
  if (sp.special) { erow("NOZZLE", fmt("%.0f deg%s", plane.nozzle * 90, plane.nozzle > 0.99f ? "  VTOL" : "")); erow("MACH", fmt("%.2f", plane.mach)); }
  else erow("FLAPS", fmt("%.0f%%", plane.flaps * 100));
  std::string gearS = !sp.retract ? "FIXED" : plane.gear > 0.99f ? "DOWN" : plane.gear < 0.01f ? "UP" : "TRANSIT";
  erow("GEAR", gearS, plane.gear > 0.99f ? C_GOOD : plane.gear < 0.01f ? C_DIM : C_WARN);
  erow("TRIM", fmt("%+.0f", plane.ctl.trim * 100));
  erow("GND SPD", fmtSpeed(gs));
  std::string st;
  if (plane.ctl.brake > 0.5f) st += "BRAKE ";
  if (plane.apOn) st += fmt("AP %03.0f/%s ", plane.apHeading, fmtAlt(plane.apAlt).c_str());
  if (landingLight) st += "LDG LT";
  g_ren.text(ex + 14 * s, ty, 13 * s, st, C_WARN, 1);
  } else if (!plane.spec->special) {
    // cockpit view: the 3D panel carries the instruments; add a compact readout strip
    std::string ro = fmt("IAS %s   ALT %s   HDG %03.0f   VS %+.0f   THR %.0f%%   FLAPS %.0f%%   %s   FUEL %.0f%%", fmtSpeed(plane.ias).c_str(), fmtAlt(plane.pos.y).c_str(),
                         plane.heading(), plane.vel.y * 196.85f, plane.ctl.throttle * 100, plane.flaps * 100,
                         !plane.spec->retract ? "GEAR FIXED" : plane.gear > 0.99f ? "GEAR DOWN" : plane.gear < 0.01f ? "GEAR UP" : "GEAR TRANSIT",
                         plane.fuel / plane.spec->maxFuel * 100);
    if (plane.apOn) ro += "   AP";
    if (plane.ctl.brake > 0.5f) ro += "   BRAKE";
    float tw = g_ren.textWidth(ro, 15 * s) + 30 * s;
    hudPanel(W * 0.5f - tw * 0.5f, H - 44 * s, tw, 30 * s);
    g_ren.text(W * 0.5f, H - 38 * s, 15 * s, ro, C_TEXT, 0.95f, 1);
  }
  // minimap
  float mm = 210 * s;
  float range = clampf(dist * 1.3f, 3000.f, 20000.f);
  if (showMinimap) drawMinimap(W - mm - 30 * s, 80 * s, mm, range);
  // warnings
  bool flash = fmodf(realTime, 0.8f) < 0.5f;
  float wy = H * 0.3f;
  if (plane.stallWarn > 0.8f && !plane.onGround && flash) { g_ren.text(W * 0.5f, wy, 44 * s, "STALL", C_BAD, 1, 1); wy += 52 * s; }
  bool nearDest = length(plane.pos - d.pos()) < 4000.f;
  if (!plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f && flash) { g_ren.text(W * 0.5f, wy, 40 * s, "PULL UP", C_BAD, 1, 1); wy += 48 * s; }
  if (spc.retract && plane.gear < 0.99f && !plane.onGround && plane.agl() < 200 && nearDest && plane.ias < spc.vref * 1.5f && flash) { g_ren.text(W * 0.5f, wy, 36 * s, "GEAR!", C_WARN, 1, 1); wy += 44 * s; }
  if (!plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && flash) g_ren.text(W * 0.5f, wy, 26 * s, "ENGINE OFF - press I to restart", C_BAD, 1, 1);
  // instructor hint
  if (set.showHints && !hint.empty() && !crashed) {
    float hw = std::min(760 * s, W - 40 * s);
    auto lines = wrap(hint, hw - 40 * s, 17 * s);
    float hh = 38 * s + lines.size() * 23 * s;
    float hx = W * 0.5f - hw * 0.5f, hy = camMode == 1 ? 160 * s : H - hh - 300 * s * 0 - 30 * s;
    if (camMode != 1) hy = H - hh - 20 * s, hx = std::max(hx, 360 * s);
    if (hx + hw > W - 320 * s && camMode != 1) hw = std::max(300 * s, W - 320 * s - hx);
    g_ren.rectGrad(hx, hy, hw, hh, vec3(0.02f, 0.08f, 0.06f), vec3(0.0f, 0.03f, 0.02f), 0.72f, 4 * s);
    g_ren.rectOutline(hx, hy, hw, hh, C_GOOD, 0.3f, 4 * s, 1.f * s);
    g_ren.rect(hx, hy, 3 * s, hh, C_GOOD, 1);
    g_ren.text(hx + 18 * s, hy + 8 * s, 12 * s, "INSTRUCTOR  //  COMMS", C_GOOD, 1, 0, false);
    float ly = hy + 28 * s;
    for (auto& l : wrap(hint, hw - 40 * s, 17 * s)) { g_ren.text(hx + 18 * s, ly, 17 * s, l, C_TEXT, 1); ly += 23 * s; }
  }
  // controls reminder
  if (flightClock < 25.f && !paused) g_ren.text(W * 0.5f, H - 22 * s, 13 * s, "W/S pitch  A/D roll  Q/E rudder  SHIFT/CTRL throttle  F/V flaps  G gear  B brake  Z autopilot  C camera  M muffle  N GPS map  TAB minimap  R radio  ESC pause", C_DIM, clampf((25.f - flightClock) / 5.f, 0, 1), 1);
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
  else if (radio.state() == Radio::PLAYING) g_ren.text(20 * s, 40 * s, 13 * s, "Radio: " + stations[std::clamp(set.radioStation, 0, (int)stations.size() - 1)].first, C_DIM, 0.8f);
}

// Liang-Barsky clip of a segment to a rectangle; false when fully outside
static bool clipSeg(vec2& a, vec2& b, float x0, float y0, float x1, float y1) {
  float t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y;
  float p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
  for (int i = 0; i < 4; i++) {
    if (fabsf(p[i]) < 1e-6f) { if (q[i] < 0) return false; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; } else { if (r < t0) return false; if (r < t1) t1 = r; }
  }
  vec2 a0 = a; a = vec2(a0.x + dx * t0, a0.y + dy * t0); b = vec2(a0.x + dx * t1, a0.y + dy * t1);
  return true;
}

void Game::drawMapOverlay() { drawGps(); }

// N: animated GPS moving map (north up, centred on the aircraft) with a navigation sidebar
void Game::drawGps() {
  float open = anim(0x6e61u, showMap ? 1.f : 0.f, 12);
  if (open < 0.01f) return;
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H, T = realTime;
  float e = 1.f - (1.f - open) * (1.f - open);
  g_ren.rect(0, 0, W, H, vec3(0.0f, 0.01f, 0.02f), 0.55f * e);
  // layout: map square on the left, info column on the right
  float side = std::min(330 * s, W * 0.3f);
  float msz = std::min(H - 110 * s, W - side - 70 * s);
  float total = msz + 20 * s + side;
  float mx = W * 0.5f - total * 0.5f, my = H * 0.5f - msz * 0.5f + 16 * s;
  float grow = 0.94f + 0.06f * e;
  float mcx = mx + msz * 0.5f, mcy = my + msz * 0.5f;
  float msz2 = msz * grow; mx = mcx - msz2 * 0.5f; my = mcy - msz2 * 0.5f; msz = msz2;
  float x1 = mx + msz, y1 = my + msz;
  // zoom
  gpsRange = gpsRange + (gpsRangeTarget - gpsRange) * (1.f - expf(-10.f * uiDt));
  float range = gpsRange, k = msz / (2 * range);
  vec2 C(mcx, mcy);
  auto toS = [&](float wx_, float wz) { return vec2(mcx + (wx_ - plane.pos.x) * k, mcy + (wz - plane.pos.z) * k); };
  auto inside = [&](vec2 p, float m) { return p.x > mx + m && p.x < x1 - m && p.y > my + m && p.y < y1 - m; };
  auto seg = [&](vec2 a, vec2 b, float th, vec3 c, float al) { if (clipSeg(a, b, mx, my, x1, y1)) g_ren.line(a.x, a.y, b.x, b.y, th, c, al * e); };
  auto dashed = [&](vec2 a, vec2 b, float th, vec3 c, float al, float dash, float phase) {
    vec2 d(b.x - a.x, b.y - a.y); float L = sqrtf(d.x * d.x + d.y * d.y); if (L < 1) return;
    vec2 u(d.x / L, d.y / L);
    for (float t0 = -fmodf(phase, dash * 2); t0 < L; t0 += dash * 2) {
      float ta = std::max(t0, 0.f), tb = std::min(t0 + dash, L); if (tb <= ta) continue;
      seg(vec2(a.x + u.x * ta, a.y + u.y * ta), vec2(a.x + u.x * tb, a.y + u.y * tb), th, c, al);
    }
  };
  // frame + map image (world clipped to the frame, open sea outside the island chart)
  g_ren.glow(mx, my, msz, msz, C_ACCENT, 0.25f * e, 4 * s, 18 * s);
  g_ren.rect(mx, my, msz, msz, vec3(0.03f, 0.1f, 0.18f), e);
  {
    float wx0 = std::max(plane.pos.x - range, -WORLD_HALF), wx1 = std::min(plane.pos.x + range, WORLD_HALF);
    float wz0 = std::max(plane.pos.z - range, -WORLD_HALF), wz1 = std::min(plane.pos.z + range, WORLD_HALF);
    if (wx1 > wx0 && wz1 > wz0) {
      vec2 a = toS(wx0, wz0), b = toS(wx1, wz1);
      g_ren.image(g_ren.minimapTex, a.x, a.y, b.x - a.x, b.y - a.y, (wx0 + WORLD_HALF) / (2 * WORLD_HALF), (wz0 + WORLD_HALF) / (2 * WORLD_HALF),
                  (wx1 + WORLD_HALF) / (2 * WORLD_HALF), (wz1 + WORLD_HALF) / (2 * WORLD_HALF), e);
      g_ren.flushUIPublic();
    }
  }
  g_ren.rect(mx, my, msz, msz, vec3(0.0f, 0.05f, 0.1f), 0.28f * e);   // night-mode tint
  // grid every 5 / 10 km
  float gstep = range > 15000 ? 10000.f : range > 6000 ? 5000.f : 2000.f;
  for (float gx = floorf((plane.pos.x - range) / gstep) * gstep; gx <= plane.pos.x + range; gx += gstep) { vec2 a = toS(gx, plane.pos.z - range), b = toS(gx, plane.pos.z + range); seg(a, b, 1 * s, C_ACCENT, 0.12f); }
  for (float gz = floorf((plane.pos.z - range) / gstep) * gstep; gz <= plane.pos.z + range; gz += gstep) { vec2 a = toS(plane.pos.x - range, gz), b = toS(plane.pos.x + range, gz); seg(a, b, 1 * s, C_ACCENT, 0.12f); }
  // range rings with labels
  for (int ri = 1; ri <= 2; ri++) {
    float rr = range * 0.5f * ri, rp = rr * k;
    int n = 72;
    for (int i = 0; i < n; i += 1) { if (i % 2) continue; float a0 = i * 6.2832f / n, a1 = (i + 1) * 6.2832f / n; seg(vec2(C.x + cosf(a0) * rp, C.y + sinf(a0) * rp), vec2(C.x + cosf(a1) * rp, C.y + sinf(a1) * rp), 1.2f * s, C_ACCENT, 0.4f); }
    vec2 lp(C.x + rp * 0.7071f, C.y + rp * 0.7071f);
    if (inside(lp, 20 * s)) g_ren.text(lp.x + 4 * s, lp.y - 14 * s, 11 * s, fmt("%.1f km", rr / 1000.f), C_ACCENT, 0.8f * e, 0, false);
  }
  // fuel range ring
  float rangeM = plane.rangeLeftKm() * 1000.f;
  {
    float rp = rangeM * k; int n = 120;
    for (int i = 0; i < n; i += 2) { float a0 = i * 6.2832f / n + T * 0.05f, a1 = (i + 1) * 6.2832f / n + T * 0.05f; seg(vec2(C.x + cosf(a0) * rp, C.y + sinf(a0) * rp), vec2(C.x + cosf(a1) * rp, C.y + sinf(a1) * rp), 2 * s, C_WARN, 0.55f); }
  }
  // breadcrumb trail
  for (size_t i = 0; i < trail.size(); i++) { vec2 p = toS(trail[i].x, trail[i].y); if (inside(p, 2 * s)) g_ren.rect(p.x - 1.5f * s, p.y - 1.5f * s, 3 * s, 3 * s, C_ACCENT, (0.25f + 0.6f * (float)i / trail.size()) * e, 1.5f * s); }
  // route: departure -> checkpoints -> destination, flowing dashes; active leg bright
  const vec3 MAG(1.f, 0.35f, 1.f);
  std::vector<vec2> pts; pts.push_back(vec2(g_world.airports[contract.from].x, g_world.airports[contract.from].z));
  for (auto& w : contract.wps) pts.push_back(vec2(w.x, w.z));
  pts.push_back(vec2(dest().x, dest().z));
  int activeLeg = std::min(wpIndex, (int)pts.size() - 2);
  for (int i = 0; i + 1 < (int)pts.size(); i++) {
    vec2 a = toS(pts[i].x, pts[i].y), b = toS(pts[i + 1].x, pts[i + 1].y);
    bool act = i == activeLeg, done = i < activeLeg;
    if (act) { vec2 p = toS(plane.pos.x, plane.pos.z); a = p; seg(a, b, 6 * s, MAG, 0.18f); dashed(a, b, 3 * s, MAG, 1.f, 10 * s, -T * 40 * s); }
    else dashed(a, b, 2 * s, done ? C_DIM : MAG, done ? 0.35f : 0.6f, 6 * s, 0);
  }
  for (int i = 0; i < (int)contract.wps.size(); i++) {
    vec2 p = toS(contract.wps[i].x, contract.wps[i].z); if (!inside(p, 6 * s)) continue;
    bool done = i < wpIndex, act = i == wpIndex;
    float r = (act ? 8.f + 2.f * sinf(T * 5.f) : 6.f) * s;
    if (act) g_ren.glow(p.x - r, p.y - r, 2 * r, 2 * r, C_GOOD, 0.5f * e, r, 10 * s);
    g_ren.rectOutline(p.x - r, p.y - r, 2 * r, 2 * r, done ? C_DIM : C_GOOD, e, r, 2 * s);
    g_ren.text(p.x + r + 3 * s, p.y - 7 * s, 11 * s, fmt("CP%d", i + 1), done ? C_DIM : C_GOOD, e, 0, false);
  }
  // airports: runway to scale (min length on screen), code labels, destination pulse
  int nearest = g_world.nearestAirport(plane.pos.x, plane.pos.z);
  for (int i = 0; i < (int)g_world.airports.size(); i++) {
    const Airport& a = g_world.airports[i];
    vec2 p = toS(a.x, a.z); if (!inside(p, -40 * s)) continue;
    vec3 dir = a.dir(); float hl = std::max(a.length * 0.5f * k, 6 * s);
    bool isDest = &a == &dest();
    vec3 c = isDest ? MAG : i == nearest ? C_WARN : vec3(0.9f, 0.95f, 1.f);
    seg(vec2(p.x - dir.x * hl, p.y - dir.z * hl), vec2(p.x + dir.x * hl, p.y + dir.z * hl), 4 * s, vec3(0.05f, 0.05f, 0.07f), 0.9f);
    seg(vec2(p.x - dir.x * hl, p.y - dir.z * hl), vec2(p.x + dir.x * hl, p.y + dir.z * hl), 2 * s, c, 1.f);
    if (isDest) for (int ring = 0; ring < 2; ring++) { float ph = fmodf(T * 0.7f + ring * 0.5f, 1.f), r = (8 + 30 * ph) * s; if (inside(p, r)) g_ren.rectOutline(p.x - r, p.y - r, 2 * r, 2 * r, MAG, (1 - ph) * e, r, 2 * s); }
    if (inside(p, 30 * s)) g_ren.text(p.x + 8 * s, p.y + 4 * s, 12 * s, a.code, c, e, 0, true);
  }
  // radar sweep around the aircraft
  for (int i = 0; i < 14; i++) {
    float a = T * 1.6f - i * 0.035f;
    seg(C, vec2(C.x + cosf(a) * msz, C.y + sinf(a) * msz), 2 * s, C_ACCENT, 0.22f * (1.f - i / 14.f));
  }
  // predicted track: markers at 1, 2 and 5 minutes along the ground velocity
  vec2 gv(plane.vel.x, plane.vel.z);
  for (int m : {1, 2, 5}) {
    vec2 p = toS(plane.pos.x + gv.x * 60 * m, plane.pos.z + gv.y * 60 * m);
    seg(C, p, 1.5f * s, vec3(1, 1, 0.3f), 0.5f);
    if (inside(p, 4 * s)) { g_ren.rect(p.x - 3 * s, p.y - 3 * s, 6 * s, 6 * s, vec3(1, 1, 0.3f), e, 1 * s); g_ren.text(p.x + 6 * s, p.y - 6 * s, 10 * s, fmt("%dm", m), vec3(1, 1, 0.3f), 0.8f * e, 0, false); }
  }
  // aircraft symbol
  {
    float h = plane.heading() * DEG; vec2 f(sinf(h), -cosf(h)), r(cosf(h), sinf(h));
    float a = 12 * s;
    g_ren.glow(C.x - 10 * s, C.y - 10 * s, 20 * s, 20 * s, vec3(1, 1, 0.3f), 0.35f * e, 10 * s, 10 * s);
    vec2 nose(C.x + f.x * a, C.y + f.y * a), tl(C.x - f.x * a * 0.7f + r.x * a * 0.8f, C.y - f.y * a * 0.7f + r.y * a * 0.8f), tr(C.x - f.x * a * 0.7f - r.x * a * 0.8f, C.y - f.y * a * 0.7f - r.y * a * 0.8f);
    vec2 tail(C.x - f.x * a * 0.35f, C.y - f.y * a * 0.35f);
    for (int pass = 0; pass < 2; pass++) {
      float th = pass ? 2.5f * s : 5.f * s; vec3 c = pass ? vec3(1, 1, 0.3f) : vec3(0, 0, 0);
      g_ren.line(nose.x, nose.y, tl.x, tl.y, th, c, e); g_ren.line(nose.x, nose.y, tr.x, tr.y, th, c, e);
      g_ren.line(tl.x, tl.y, tail.x, tail.y, th, c, e); g_ren.line(tr.x, tr.y, tail.x, tail.y, th, c, e);
    }
  }
  // compass rose ticks on the frame and N/E/S/W
  for (int d = 0; d < 360; d += 10) {
    float a = d * DEG; vec2 u(sinf(a), -cosf(a));
    float L = d % 90 == 0 ? 14 * s : d % 30 == 0 ? 9 * s : 5 * s;
    // project to the square frame
    float tt = std::min(fabsf(u.x) > 1e-4f ? (msz * 0.5f) / fabsf(u.x) : 1e9f, fabsf(u.y) > 1e-4f ? (msz * 0.5f) / fabsf(u.y) : 1e9f);
    vec2 o(C.x + u.x * tt, C.y + u.y * tt);
    g_ren.line(o.x, o.y, o.x - u.x * L, o.y - u.y * L, 1.5f * s, C_ACCENT, 0.7f * e);
  }
  g_ren.text(C.x, my + 16 * s, 14 * s, "N", C_TEXT, e, 1, false); g_ren.text(C.x, y1 - 30 * s, 14 * s, "S", C_DIM, e, 1, false);
  g_ren.text(mx + 20 * s, C.y - 7 * s, 14 * s, "W", C_DIM, e, 1, false); g_ren.text(x1 - 20 * s, C.y - 7 * s, 14 * s, "E", C_DIM, e, 1, false);
  g_ren.rectOutline(mx, my, msz, msz, C_ACCENT, 0.5f * e, 4 * s, 1.5f * s);
  brackets(mx - 4 * s, my - 4 * s, msz + 8 * s, msz + 8 * s, 22 * s, 2.5f * s, C_ACCENT, e);
  // scale bar
  {
    float km = range > 15000 ? 10.f : range > 6000 ? 5.f : range > 2500 ? 2.f : 1.f, L = km * 1000 * k;
    float bx = mx + 16 * s, by = y1 - 18 * s;
    g_ren.rect(bx, by, L, 3 * s, vec3(0, 0, 0), 0.6f * e); g_ren.rect(bx, by, L * 0.5f, 3 * s, C_TEXT, e); g_ren.rect(bx + L * 0.5f, by, L * 0.5f, 3 * s, C_ACCENT, e);
    g_ren.text(bx, by - 16 * s, 11 * s, fmt("%.0f KM", km), C_TEXT, e, 0, true);
  }
  // title bar
  g_ren.text(mx, my - 30 * s, 18 * s, "GPS  //  MOVING MAP", C_TEXT, e, 0, false);
  int gmin = (int)(timeOfDay * 60) % 1440;
  g_ren.text(x1, my - 28 * s, 14 * s, fmt("RNG %.1f KM   %02d:%02d LCL   %s", range / 1000.f, gmin / 60, gmin % 60, fmodf(T, 1.f) < 0.5f ? "o" : " "), C_ACCENT, e, 2, false);

  // ---------------- info column
  float sx = x1 + 20 * s, sy = my, sw = side, sh = msz;
  panel(sx, sy, sw, sh, 0.95f * e);
  float px = sx + 18 * s, py = sy + 16 * s, vw = sw - 36 * s;
  vec3 target = wpIndex < (int)contract.wps.size() ? vec3(contract.wps[wpIndex].x, contract.wps[wpIndex].alt, contract.wps[wpIndex].z) : dest().pos();
  vec3 to = target - plane.pos; float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  float trk = gs > 2 ? wrapDeg360(atan2f(plane.vel.x, -plane.vel.z) / DEG) : plane.heading();
  // remaining route distance
  float remain = dist;
  { vec2 prev(target.x, target.z); for (int i = wpIndex + 1; i < (int)contract.wps.size(); i++) { vec2 p(contract.wps[i].x, contract.wps[i].z); remain += length(vec3(p.x - prev.x, 0, p.y - prev.y)); prev = p; }
    if (wpIndex < (int)contract.wps.size()) remain += length(vec3(dest().x - prev.x, 0, dest().z - prev.y)); }
  auto kv = [&](const char* kname, const std::string& v, vec3 c = C_TEXT, float vs = 16.f) {
    g_ren.text(px, py + 2 * s, 12 * s, kname, C_DIM, e, 0, false);
    g_ren.text(px + vw, py, vs * s, ellipsize(v, vw - 70 * s, vs * s), c, e, 2);
    py += (vs + 8.f) * s;
  };
  header(px, py, vw, "NAVIGATION"); py += 24 * s;
  std::string nxt = wpIndex < (int)contract.wps.size() ? fmt("CP%d of %d", wpIndex + 1, (int)contract.wps.size()) : std::string(dest().code);
  kv("NEXT", nxt, MAG, 20);
  kv("DIST", fmt("%.1f km", dist / 1000.f), C_TEXT, 18);
  kv("BRG / DTK", fmt("%03.0f  /  %03.0f", brg, brg));
  kv("ETE", gs > 10 ? fmt("%d:%02d", (int)(dist / gs) / 60, (int)(dist / gs) % 60) : "--:--");
  float etaH = timeOfDay + (gs > 10 ? remain / gs / 3600.f : 0);
  kv("ETA DEST", gs > 10 ? fmt("%02d:%02d LCL", ((int)etaH) % 24, (int)(fmodf(etaH, 1.f) * 60)) : "--:--");
  kv("ROUTE LEFT", fmt("%.1f km", remain / 1000.f));
  if (contract.timeLimitMin > 0) { float left = contract.timeLimitMin * 60 - flightClock; kv("DEADLINE", left > 0 ? fmt("%d:%02d", (int)left / 60, (int)left % 60) : "LATE", left > 120 ? C_TEXT : C_BAD); }
  py += 4 * s;
  header(px, py, vw, "AIRCRAFT"); py += 24 * s;
  kv("GS / TRK", fmt("%s  %03.0f", fmtSpeed(gs).c_str(), trk));
  kv("ALT / VS", fmt("%s  %+.0f", fmtAlt(plane.pos.y).c_str(), set.metric ? plane.vel.y : plane.vel.y * 196.85f));
  bool fuelOk = rangeM > remain * 1.1f;
  kv("FUEL RANGE", fmt("%.0f km  %s", rangeM / 1000.f, fuelOk ? "OK" : "LOW"), fuelOk ? C_GOOD : C_BAD);
  py += 4 * s;
  header(px, py, vw, "DESTINATION"); py += 24 * s;
  const Airport& D = dest();
  g_ren.text(px, py, 15 * s, ellipsize(fmt("%s  %s", D.code, D.name), vw, 15 * s), MAG, e); py += 22 * s;
  g_ren.text(px, py, 13 * s, ellipsize(fmt("RWY %02d/%02d  %.0f m  %s", D.rwyNumber(false), D.rwyNumber(true), D.length, surfaceName(D.surface)), vw, 13 * s), C_TEXT, e); py += 19 * s;
  g_ren.text(px, py, 13 * s, ellipsize(fmt("ELEV %s   WIND %03.0f/%.0fkt", fmtAlt(D.elev).c_str(), wx.windFrom, wx.windSpeed * MS_TO_KT), vw, 13 * s), C_DIM, e); py += 24 * s;
  if (py < sy + sh - 70 * s) {
    header(px, py, vw, "NEAREST"); py += 24 * s;
    const Airport& N = g_world.airports[nearest];
    vec3 tn = N.pos() - plane.pos; float dn = length(vec3(tn.x, 0, tn.z));
    g_ren.text(px, py, 14 * s, ellipsize(fmt("%s  %.1f km  BRG %03.0f", N.code, dn / 1000.f, wrapDeg360(atan2f(tn.x, -tn.z) / DEG)), vw, 14 * s), C_WARN, e); py += 22 * s;
  }
  // zoom controls on the map
  if (button(x1 - 46 * s, my + 12 * s, 34 * s, 30 * s, "+")) gpsRangeTarget = std::max(1500.f, gpsRangeTarget * 0.6f);
  if (button(x1 - 46 * s, my + 46 * s, 34 * s, 30 * s, "-")) gpsRangeTarget = std::min(40000.f, gpsRangeTarget / 0.6f);
  if (button(x1 - 46 * s, my + 80 * s, 34 * s, 30 * s, "R")) gpsRangeTarget = clampf(std::max(dist, 3000.f) * 1.25f, 1500.f, 40000.f);
  g_ren.text(sx + sw * 0.5f, sy + sh - 24 * s, 11.5f * s, "WHEEL ZOOM   R FIT TARGET   N CLOSE", C_DIM, 0.8f * e, 1, false);
}

// ------------------------------------------------------------------ hidden research menu (U + I on the main menu)
void Game::drawResearch() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H, T = realTime - resOpened;
  const vec3 RED(1.f, 0.28f, 0.25f);
  g_ren.rectGrad(0, 0, W, H, vec3(0.0f, 0.02f, 0.04f), vec3(0.03f, 0.0f, 0.01f), 0.55f);
  // access sequence
  if (T < 1.4f) {
    g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.85f * (1.f - smoothstepf(1.0f, 1.4f, T)));
    const char* lines[] = {"> AUTHENTICATING BIOMETRIC TOKEN ...", "> DECRYPTING PROJECT NIGHTGLASS ...", "> ACCESS GRANTED"};
    for (int i = 0; i < 3; i++) {
      float st = i * 0.35f; if (T < st) break;
      std::string l = lines[i]; l = l.substr(0, std::min(l.size(), (size_t)((T - st) * 60.f)));
      g_ren.text(W * 0.5f - 260 * s, H * 0.4f + i * 34 * s, 20 * s, l, i == 2 ? C_GOOD : C_ACCENT, 1, 0, false);
    }
    return;
  }
  float e = smoothstepf(1.4f, 1.8f, T);
  // dossier
  float lx = 40 * s, ly = 40 * s, lw = std::min(500 * s, W * 0.42f), lh = H - 80 * s;
  panel(lx, ly, lw, lh, 0.95f * e);
  float px = lx + 24 * s, py = ly + 20 * s;
  g_ren.rectOutline(px, py, 300 * s, 30 * s, RED, e, 3 * s, 2 * s);
  g_ren.text(px + 150 * s, py + 7 * s, 15 * s, "TOP SECRET // NIGHTGLASS", RED, e, 1, false);
  py += 46 * s;
  g_ren.text(px, py, 13 * s, "CONFIDENTIAL RESEARCH MODEL", C_ACCENT, e, 0, false); py += 22 * s;
  g_ren.text(px, py, 38 * s, "XR-9 SPECTER", C_TEXT, e); py += 52 * s;
  header(px, py, lw - 48 * s, "AIRFRAME"); py += 26 * s;
  auto row = [&](const char* k, const char* v) { g_ren.text(px, py, 13 * s, k, C_DIM, e); g_ren.text(px + 135 * s, py, 13 * s, ellipsize(v, lw - 183 * s, 13 * s), C_TEXT, e); py += 20 * s; };
  row("Configuration", "Blended lifting body, cranked delta, canards");
  row("Propulsion", "2 x afterburning turbofan, 236 kN");
  row("Thrust / weight", "2.2 : 1");
  row("Top speed", "Mach 2+ at altitude");
  row("Thrust vectoring", "2D nozzles, 0 - 90 deg, VTOL");
  row("Flight control", "Fly-by-wire, 9 g limiter, 315 deg/s roll");
  row("Cockpit", "Sealed pod, synthetic-vision displays + HUD");
  row("Fuel", "Unrestricted (research cell)");
  py += 10 * s;
  header(px, py, lw - 48 * s, "HANDLING NOTES"); py += 26 * s;
  const char* notes[] = {"F / V   swivel nozzles: 0 = forward flight, 90 = hover",
                         "Hover:  nozzles 90, ~65% throttle, stick to translate",
                         "Hands off in the hover and the jet levels itself",
                         "Above 85% throttle the afterburners light",
                         "Mach 1 sets off a sonic boom - try it low over the sea",
                         "C cockpit view: you fly on the displays only"};
  for (auto n : notes) { if (py > ly + lh - 30 * s) break; g_ren.text(px, py, 13 * s, ellipsize(n, lw - 48 * s, 13 * s), C_DIM, e); py += 20 * s; }
  // launch parameters
  float rx = lx + lw + 24 * s, rw = std::min(W - rx - 40 * s, 640 * s);
  panel(rx, ly, rw, lh, 0.95f * e);
  float qx = rx + 24 * s, qy = ly + 20 * s, qw = rw - 48 * s;
  header(qx, qy, qw, "LAUNCH SITE"); qy += 28 * s;
  int na = (int)g_world.airports.size();
  int cols = 2; float cw = (qw - 10 * s) / cols, ch = 30 * s;
  for (int i = 0; i < na; i++) {
    float bx = qx + (i % cols) * (cw + 10 * s), by = qy + (i / cols) * (ch + 6 * s);
    const Airport& a = g_world.airports[i];
    if (button(bx, by, cw, ch, ellipsize(fmt("%s  %s", a.code, a.name), cw - 20 * s, 14 * s), true, resAirport == i)) resAirport = i;
  }
  qy += ((na + cols - 1) / cols) * (ch + 6 * s) + 12 * s;
  header(qx, qy, qw, "START"); qy += 28 * s;
  if (button(qx, qy, 200 * s, 32 * s, "Airborne (3,000 ft)", true, resAirborne)) resAirborne = true;
  if (button(qx + 210 * s, qy, 200 * s, 32 * s, "On the runway", true, !resAirborne)) resAirborne = false;
  qy += 46 * s;
  header(qx, qy, qw, "CONDITIONS"); qy += 28 * s;
  const char* wxs[] = {"Clear", "Cloudy", "Storm"};
  for (int i = 0; i < 3; i++) if (button(qx + i * 110 * s, qy, 100 * s, 32 * s, wxs[i], true, resWx == i)) resWx = i;
  qy += 44 * s;
  {
    g_ren.text(qx, qy + 6 * s, 14 * s, fmt("Time  %02d:00", (int)resTime), C_DIM, e);
    float sx = qx + 120 * s, sw = std::min(260 * s, qw - 130 * s), f = (resTime - 5.f) / 16.f;
    g_ren.rect(sx, qy + 14 * s, sw, 4 * s, C_ACCENT, 0.15f, 2 * s);
    g_ren.rectGrad(sx, qy + 14 * s, sw * f, 4 * s, C_ACCENT * 0.6f, C_ACCENT, 1, 2 * s);
    g_ren.rect(sx + sw * f - 6 * s, qy + 10 * s, 12 * s, 12 * s, C_ACCENT, 1, 6 * s);
    if (hovered(sx, qy, sw, 32 * s) && in.mDown[0]) resTime = floorf(5.f + clampf((in.mx - sx) / sw, 0, 1) * 16.f + 0.5f);
  }
  float by = ly + lh - 70 * s;
  if (button(qx, by, 160 * s, 48 * s, "Back") || in.pressed[K_ESC]) { screen = SCR_MENU; return; }
  if (button(rx + rw - 24 * s - 280 * s, by, 280 * s, 48 * s, "LAUNCH XR-9", true, true) || in.pressed[K_ENTER]) launchResearch();
  g_ren.text(qx, by - 26 * s, 12 * s, "GAMEPAD:  L-STICK CURSOR   A SELECT   B BACK   LB / RB SITE   START LAUNCH", C_DIM, 0.8f * e, 0, false);
  // blinking classification footer
  if (fmodf(realTime, 1.2f) < 0.8f) g_ren.text(W * 0.5f, H - 28 * s, 12 * s, "UNAUTHORISED ACCESS IS A FEDERAL OFFENCE  //  THIS SESSION IS NOT RECORDED IN YOUR LOGBOOK", RED, 0.8f * e, 1, false);
}

void Game::drawPause() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.55f);
  if (settingsFromPause) {
    float pw = std::min(760 * s, W - 40 * s);
    panel(W * 0.5f - pw * 0.5f, 60 * s, pw, H - 120 * s, 0.95f);
    drawSettings(W * 0.5f - pw * 0.5f + 24 * s, 80 * s, pw - 48 * s, H - 160 * s);
    if (button(W * 0.5f + pw * 0.5f - 180 * s, H - 120 * s, 150 * s, 42 * s, "Back", true, true)) settingsFromPause = false;
    return;
  }
  float pw = 760 * s, ph = std::min(530 * s, H - 20 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  g_ren.text(x + 30 * s, y + 24 * s, 28 * s, "Paused", C_TEXT, 1);
  header(x + 310 * s, y + 52 * s, pw - 340 * s, "CONTROLS");
  float by = y + 80 * s, bw = 240 * s, bh = 46 * s;
  if (button(x + 30 * s, by, bw, bh, "Resume", true, true)) paused = false;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Restart flight")) { if (researchFlight) launchResearch(); else { Contract c = contract; startFlight(c, specIdx, source); } }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Settings")) settingsFromPause = true;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, showRadio ? "Hide radio" : "Radio")) showRadio = !showRadio;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, researchFlight ? "End research flight" : "Abandon flight")) endFlight(false, researchFlight ? "" : "Abandoned flight");
  float cx = x + 310 * s, cy = y + 80 * s;
  const char* lines[] = {"W / S ........ pitch down / up", "A / D ........ roll", "Q / E ........ rudder / nosewheel", "SHIFT / CTRL . throttle (1-9, 0)",
                         "F / V ........ flaps down / up", "G ............ landing gear", "B ............ parking brake", "SPACE ........ wheel brakes",
                         "[ / ] ........ elevator trim", "Z ............ autopilot (A/D steer)", "T ............ time acceleration", "C ............ camera  (right-drag look)",
                         "L ............ landing lights", "M ............ muffle engine noise", "N ............ GPS moving map", "TAB .......... minimap",
                         "R ............ internet radio", "H ............ hide / show HUD"};
  for (auto l : lines) {
    std::string str(l); size_t dot = str.find(" .");
    std::string key = str.substr(0, dot), rest = dot == std::string::npos ? "" : str.substr(str.find_first_not_of(". ", dot));
    float kw = g_ren.textWidth(key, 13 * s) + 12 * s;
    g_ren.rect(cx, cy - 1 * s, kw, 19 * s, C_ACCENT, 0.12f, 3 * s); g_ren.rectOutline(cx, cy - 1 * s, kw, 19 * s, C_ACCENT, 0.4f, 3 * s, 1.f * s);
    g_ren.text(cx + 6 * s, cy + 1 * s, 13 * s, key, C_ACCENT, 1, 0, false);
    g_ren.text(cx + 130 * s, cy + 1 * s, 13.5f * s, rest, C_DIM, 1);
    cy += 22 * s;
  }
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
}

void Game::drawDebrief() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.35f);
  float pw = std::min(720 * s, W - 40 * s), ph = std::min(600 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  float px = x + 30 * s, py = y + 24 * s;
  for (auto& l : wrap(debriefTitle, pw - 60 * s, 28 * s)) { g_ren.text(px, py, 28 * s, l, lastSuccess ? C_GOOD : C_BAD, 1); py += 34 * s; }
  py += 8 * s;
  g_ren.text(px, py, 18 * s, ellipsize(contract.title, pw - 60 * s, 18 * s), C_TEXT, 1); py += 34 * s;
  if (lastSuccess && contract.type != CT_FERRY) {
    for (int i = 0; i < 3; i++) {
      float pop = anim(uid(px + i, py, "star"), 1.f, 6.f - i * 1.5f);
      float sz = 32 * s * (i < stars ? pop : 1.f), cx = px + i * 44 * s + 16 * s, cy = py + 16 * s;
      if (i < stars) g_ren.glow(cx - sz * 0.5f, cy - sz * 0.5f, sz, sz, C_WARN, 0.5f * pop, sz * 0.5f, 12 * s);
      g_ren.rect(cx - sz * 0.5f, cy - sz * 0.5f, sz, sz, i < stars ? C_WARN : vec3(0.12f, 0.16f, 0.2f), 1, sz * 0.5f);
      g_ren.rect(cx - sz * 0.22f, cy - sz * 0.22f, sz * 0.44f, sz * 0.44f, i < stars ? vec3(1, 0.95f, 0.8f) : vec3(0.2f, 0.25f, 0.3f), 1, sz * 0.22f);
    }
    py += 48 * s;
  }
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 230 * s, py, 16 * s, ellipsize(v, pw - 290 * s, 16 * s), C_TEXT, 1); py += 24 * s; };
  header(px, py, pw - 60 * s, "FLIGHT DATA"); py += 24 * s;
  if (result.landed) row("Touchdown", fmt("%.0f fpm", touchdownFpm));
  row("Flight time", fmt("%d:%02d", (int)flightClock / 60, (int)flightClock % 60));
  row("Max G", fmt("%.2f", plane.maxG));
  row("Max bank", fmt("%.0f deg", result.maxBank));
  row("Fuel used", fmt("%.0f kg", result.fuelUsedKg));
  py += 10 * s;
  header(px, py, pw - 60 * s, "SETTLEMENT"); py += 24 * s;
  int total = 0;
  for (auto& l : payout) {
    float vw = g_ren.text(x + pw - 30 * s, py, 16 * s, fmtMoney(l.amount), l.amount >= 0 ? C_GOOD : C_BAD, 1, 2);
    g_ren.text(px, py, 16 * s, ellipsize(l.label, pw - 80 * s - vw, 16 * s), C_TEXT, 1);
    total += l.amount; py += 24 * s;
  }
  g_ren.rect(px, py + 2 * s, pw - 60 * s, 1 * s, C_ACCENT, 0.5f); py += 10 * s;
  g_ren.text(px, py, 18 * s, "Total", C_TEXT, 1);
  g_ren.text(x + pw - 30 * s, py, 18 * s, fmtMoney(total), total >= 0 ? C_GOOD : C_BAD, 1, 2); py += 34 * s;
  if (career.license > licenseBefore) { fitText(px, py, pw - 60 * s, 22 * s, 14 * s, std::string("NEW LICENCE: ") + licenseName(career.license), C_WARN); py += 34 * s; }
  if (career.finished && lastSuccess && contract.story && contract.id == g_story.back().id) { for (auto& l : wrap("You've completed the Air Xpress campaign. Congratulations, Captain!", pw - 60 * s, 18 * s)) { g_ren.text(px, py, 18 * s, l, C_ACCENT, 1); py += 24 * s; } }
  if (button(x + pw - 230 * s, y + ph - 66 * s, 200 * s, 46 * s, "Continue", true, true) || in.pressed[K_ENTER]) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; }
  if (!lastSuccess && button(x + 30 * s, y + ph - 66 * s, 200 * s, 46 * s, "Try again")) { Contract c = contract; startFlight(c, specIdx, career.canFly(c, specIdx) != Career::SRC_NONE ? career.canFly(c, specIdx) : source); }
}
