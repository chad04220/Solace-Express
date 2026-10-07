// Solace Express - menus, hub screens and flight HUD
#include "game.h"

// High-tech glass UI: deep navy panels, cyan accents, amber for warnings
static const vec3 C_PANEL(0.015f, 0.035f, 0.06f), C_PANEL2(0.04f, 0.085f, 0.13f), C_ACCENT(0.32f, 0.86f, 1.0f), C_WARN(1.0f, 0.72f, 0.22f);
static const vec3 C_TEXT(0.93f, 0.97f, 1.0f), C_DIM(0.56f, 0.68f, 0.79f);
static vec3 C_GOOD(0.42f, 1.0f, 0.68f), C_BAD(1.0f, 0.4f, 0.38f);   // (the colour-blind palette swaps these for blue / orange)
namespace {
const float kHudBand = 56.f, kHudRail = 92.f, kHudMsgW = 330.f;   // in S() units
void hudRing(float cx, float cy, float r, float th, vec3 c, float a, int n = 40, float a0 = 0.f, float a1 = 6.2832f) {
  for (int i = 0; i < n; i++) { float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n; g_ren.line(cx + cosf(t0) * r, cy + sinf(t0) * r, cx + cosf(t1) * r, cy + sinf(t1) * r, th, c, a); }
}
// a filled triangle: a fan of hairlines from the apex across the base (the renderer has no polygon)
void hudTri(float ax, float ay, float bx, float by, float cx, float cy, vec3 c, float a) {
  float L = sqrtf((cx - bx) * (cx - bx) + (cy - by) * (cy - by));
  int n = std::max(2, (int)(L / 0.9f));
  for (int i = 0; i <= n; i++) { float t = (float)i / n; g_ren.line(ax, ay, bx + (cx - bx) * t, by + (cy - by) * t, 1.4f, c, a); }
}
// a dart: a swept arrowhead pointing along (dx, dy) with its tip at (x, y) and a notched tail, over a dark outline
void hudDart(float x, float y, float dx, float dy, float len, float wid, vec3 c, float a, bool outline = true) {
  float px = -dy, py = dx;
  auto draw = [&](float grow, vec3 col, float al) {
    float bx = x - dx * (len + grow), by = y - dy * (len + grow), w = wid + grow;
    float tx = x + dx * grow, ty = y + dy * grow;
    float nx = x - dx * (len * 0.62f), ny = y - dy * (len * 0.62f);   // the notch
    hudTri(tx, ty, bx + px * w, by + py * w, nx, ny, col, al);
    hudTri(tx, ty, bx - px * w, by - py * w, nx, ny, col, al);
  };
  if (outline) draw(1.6f, vec3(0.0f, 0.02f, 0.04f), a * 0.85f);
  draw(0.f, c, a);
}
// an arrow from (x0, y0) to (x1, y1): a tapered shaft and a filled head, over a dark outline
void hudArrow(float x0, float y0, float x1, float y1, float shaft, float headL, float headW, vec3 c, float a) {
  float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy); if (L < 1e-3f) return;
  dx /= L; dy /= L; float px = -dy, py = dx;
  float bx = x1 - dx * headL, by = y1 - dy * headL;
  for (int pass = 0; pass < 2; pass++) {
    float g = pass ? 0.f : 1.6f; vec3 col = pass ? c : vec3(0.0f, 0.02f, 0.04f); float al = pass ? a : a * 0.85f;
    g_ren.line(x0 - dx * g, y0 - dy * g, bx + dx * 1.f, by + dy * 1.f, shaft + 2 * g, col, al);
    hudTri(x1 + dx * g, y1 + dy * g, bx + px * (headW + g), by + py * (headW + g), bx - px * (headW + g), by - py * (headW + g), col, al);
  }
  g_ren.rect(x0 - shaft * 0.9f, y0 - shaft * 0.9f, shaft * 1.8f, shaft * 1.8f, c, a, shaft * 0.9f);   // a round tail
}
void hudChevronUp(float cx, float cy, float w, float h, float th, vec3 c, float a) {   // a filled chevron (the warning icons)
  float t = th * 0.9f;
  hudTri(cx, cy - h, cx - w, cy + h, cx - w + t * 1.4f, cy + h, c, a); hudTri(cx, cy - h, cx, cy - h + t * 1.6f, cx - w + t * 1.4f, cy + h, c, a);
  hudTri(cx, cy - h, cx + w, cy + h, cx + w - t * 1.4f, cy + h, c, a); hudTri(cx, cy - h, cx, cy - h + t * 1.6f, cx + w - t * 1.4f, cy + h, c, a);
}
}   // namespace
static void applyPalette(bool cb) { C_GOOD = cb ? vec3(0.35f, 0.72f, 1.0f) : vec3(0.42f, 1.0f, 0.68f); C_BAD = cb ? vec3(1.0f, 0.58f, 0.12f) : vec3(1.0f, 0.4f, 0.38f); }
static const vec3 C_BTN(0.04f, 0.09f, 0.14f), C_BTN_HI(0.07f, 0.2f, 0.29f), C_INK(0.01f, 0.05f, 0.08f);

static uint32_t uid(float x, float y, const std::string& s) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix((uint32_t)(int)x); mix((uint32_t)(int)y);
  for (char c : s) mix((uint8_t)c);
  return h;
}
static vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

void Game::applyUiPalette() { applyPalette(set.cbHud); }
float Game::S() const { return std::max(0.6f, g_ren.H / 720.f) * set.uiScale; }

bool Game::hovered(float x, float y, float w, float h) const {
  if (hitClipOn && (in.mx < hitClip[0] || in.mx >= hitClip[2] || in.my < hitClip[1] || in.my >= hitClip[3])) return false;
  return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h;
}

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
  if (uiGlass) {   // dark glass over the live scene: hairline edge, a bright lead on the top edge, small corner ticks
    a = std::min(1.f, a / 0.78f); r = 2 * s;
    g_ren.glow(x, y, w, h, vec3(0, 0, 0), 0.25f * a, r, 18 * s);
    g_ren.rectGrad(x, y, w, h, vec3(0.02f, 0.05f, 0.08f), vec3(0.004f, 0.014f, 0.028f), 0.66f * a, r);
    g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.11f * a, r, 1.f * s);
    g_ren.rect(x, y, w, 1.f * s, C_ACCENT, 0.22f * a);
    g_ren.glow(x, y - 0.5f * s, 64 * s, 2 * s, C_ACCENT, 0.35f * a, 1 * s, 6 * s);
    g_ren.rect(x, y - 0.5f * s, 64 * s, 2 * s, C_ACCENT, 0.95f * a);
    brackets(x - 1 * s, y - 1 * s, w + 2 * s, h + 2 * s, 7 * s, 1.5f * s, C_ACCENT, 0.45f * a);
    return;
  }
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
  if (uiGlass) {   // flat glass rows: brighter on hover, an accent edge and soft glow when selected
    r = 2 * s;
    if (ts > 0.01f) g_ren.glow(x, y, w, h, C_ACCENT, 0.14f * ts, r, 12 * s);
    float k = std::max(th * 0.6f, ts);
    g_ren.rectGrad(x, y, w, h, mixc(vec3(0.03f, 0.07f, 0.1f), vec3(0.05f, 0.15f, 0.22f), k), mixc(vec3(0.01f, 0.03f, 0.05f), vec3(0.02f, 0.08f, 0.12f), k), 0.42f + 0.38f * k, r);
    g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.06f + 0.22f * th + 0.5f * ts, r, 1.f * s);
    g_ren.rect(x, y, 2 * s, h, accent, 0.35f + 0.65f * std::max(th, ts));
    if (ts > 0.01f) { g_ren.rect(x + w - 26 * s, y + h - 1 * s, 26 * s, 1 * s, C_ACCENT, ts); float cx = x + w - 10 * s, cy = y + h * 0.5f; g_ren.line(cx - 4 * s, cy - 5 * s, cx, cy, 1.5f * s, C_ACCENT, ts); g_ren.line(cx, cy, cx - 4 * s, cy + 5 * s, 1.5f * s, C_ACCENT, ts); }
    return;
  }
  if (ts > 0.01f) g_ren.glow(x, y, w, h, C_ACCENT, 0.2f * ts, r, 10 * s);
  float k = std::max(th * 0.55f, ts);
  g_ren.rectGrad(x, y, w, h, mixc(vec3(0.035f, 0.07f, 0.11f), vec3(0.06f, 0.17f, 0.25f), k), mixc(vec3(0.02f, 0.045f, 0.07f), vec3(0.04f, 0.11f, 0.17f), k), 0.93f, r);
  g_ren.rectOutline(x, y, w, h, C_ACCENT, 0.1f + 0.3f * th + 0.55f * ts, r, 1.f * s);
  g_ren.rect(x, y + h * 0.15f * (1 - std::max(th, ts)), 3 * s, h * (0.7f + 0.3f * std::max(th, ts)), accent, 0.45f + 0.55f * std::max(th, ts));
  if (ts > 0.01f) { float cx = x + w - 10 * s, cy = y + h * 0.5f; g_ren.line(cx - 5 * s, cy - 6 * s, cx, cy, 2 * s, C_ACCENT, ts); g_ren.line(cx, cy, cx - 5 * s, cy + 6 * s, 2 * s, C_ACCENT, ts); }
}

static std::string ellipsize(const std::string& str, float width, float size);
bool Game::button(float x, float y, float w, float h, const std::string& label, bool enabled, bool highlight) {
  float s = S(), r = 4 * s;
  uint32_t id = uid(x, y, label);
  bool focused = focusNav && focusId == id && enabled;
  if (enabled) focusList.push_back({id, x, y, w, h});
  bool hov = enabled && (hovered(x, y, w, h) || focused);
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
  { float tw = g_ren.textWidth(label, ts), room = w - 16 * s; if (tw > room && tw > 0.f) ts = std::max(ts * room / tw, 9 * s); }   // (a long label shrinks to fit)
  g_ren.text(x + w * 0.5f, y + (h - ts) * 0.5f - ts * 0.08f + pr * 1.f * s, ts, ellipsize(label, w - 12 * s, ts), tc, 1.f, 1, false);
  if (focused) g_ren.rectOutline(x - 3 * s, y - 3 * s, w + 6 * s, h + 6 * s, C_ACCENT, 0.7f + 0.3f * sinf(realTime * 6.f), r + 2 * s, 2.f * s);
  if (focused && (in.pressed[K_ENTER] || in.pressed[' '])) { in.pressed[K_ENTER] = in.pressed[' '] = false; g_audio.trigger(SFX_CLICK); return true; }
  if (hov && !focused && in.mPressed[0]) { g_audio.trigger(SFX_CLICK); return true; }
  if (focused && in.mPressed[0] && hovered(x, y, w, h)) { g_audio.trigger(SFX_CLICK); return true; }
  return false;
}

std::vector<std::string> wrap(const std::string& s, float width, float size) {   // (shared with the research terminal)
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

// A key in a key / value list: plain, or in the hub's glass style a small upper-case label
static std::string upperS(std::string t) { for (char& c : t) c = (char)toupper((unsigned char)c); return t; }
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
  bool flightRail = screen == SCR_FLIGHT && hudOn && !(showMap && uiAnim.count(0x6e61u) && uiAnim[0x6e61u] > 0.6f);
  if (screen != SCR_FLIGHT) y = g_ren.H - 120 * s;
  if (flightRail) {   // the right message column: newest at the top, each sliding in from the screen's edge
    float rail = (camMode == 1 ? 16.f : kHudRail + 28.f) * s, rw = std::min(kHudMsgW * s, g_ren.W * 0.5f - rail - 160 * s), rx = g_ren.W - rail - rw;
    float yy = std::max((kHudBand + 12) * s, hudMsgNext);
    int shown = 0;
    for (int i = (int)toasts.size() - 1; i >= 0 && shown < 4; i--, shown++) {
      auto& t = toasts[i];
      float a = clampf(std::min(t.t * 4.f, (5.f - t.t) * 1.5f), 0, 1);
      float slide = (1.f - clampf(t.t * 5.f, 0, 1)) * 40 * s;
      float ts = 12.5f * s, hh = 22 * s;
      std::string txt = ellipsize(t.text, rw - 30 * s, ts);
      g_ren.rectGrad(rx + slide, yy, rw, hh, vec3(0.02f, 0.06f, 0.1f), vec3(0.0f, 0.02f, 0.04f), 0.7f * a, 3 * s);
      g_ren.rect(rx + slide, yy, 3 * s, hh, t.col, a);
      g_ren.rect(rx + slide + rw - 10 * s, yy + 8 * s, 5 * s, 5 * s, t.col, 0.8f * a, 2.5f * s);
      g_ren.text(rx + slide + 10 * s, yy + 5 * s, ts, txt, t.col, a, 0, false);
      yy += hh + 4 * s;
    }
    return;
  }
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

// ------------------------------------------------------------------ intro / shader compile screen
// First thing on screen: the game's icon large in a rotating targeting ring, the title revealed letter by letter
// over a moving perspective grid, and a segmented progress bar while the world is generated and the shaders are
// compiled (or loaded from the cache). fade: 1 = fully shown, falls to 0 as it hands over to the main menu.
void Game::drawIntro(float progress, const std::string& stage, float t, unsigned icon, float fade, Renderer& R) {
  float W = (float)R.W, H = (float)R.H, s = std::max(0.5f, H / 720.f);
  float e = clampf(t / 0.8f, 0, 1) * fade;   // fade in
  R.rectGrad(0, 0, W, H, vec3(0.012f, 0.03f, 0.055f), vec3(0.0f, 0.006f, 0.014f), 1.f);
  // perspective grid floor scrolling toward the viewer
  float hz = H * 0.62f;
  for (int i = 0; i < 18; i++) {
    float z = fmodf(i + t * 0.9f, 18.f) / 18.f;
    float y = hz + (H - hz) * z * z;
    R.rect(0, y, W, 1.f * s, C_ACCENT, 0.12f * z * e);
  }
  for (int i = -14; i <= 14; i++) R.line(W * 0.5f + i * 18 * s, hz, W * 0.5f + i * 150 * s, H, 1.f * s, C_ACCENT, 0.07f * e);
  R.rectGrad(0, hz - 60 * s, W, 70 * s, vec3(0.012f, 0.03f, 0.055f), vec3(0.03f, 0.1f, 0.16f), 0.6f * e);
  // drifting motes
  for (int i = 0; i < 60; i++) {
    float px = fmodf(hash2i(i, 3) * W + t * (8 + 20 * hash2i(i, 5)) * s, W), py = hash2i(i, 7) * H * 0.9f;
    float tw = 0.4f + 0.6f * sinf(t * (1.5f + hash2i(i, 9) * 2.f) + i);
    R.rect(px, py, 2 * s, 2 * s, C_ACCENT, 0.25f * tw * e, 1 * s);
  }
  // icon in a halo with counter-rotating segmented rings
  float ic = 200 * s, cx = W * 0.5f, cy = H * 0.3f;
  float pulse = 0.5f + 0.5f * sinf(t * 2.2f);
  float pop = 1.f - powf(1.f - clampf((t - 0.1f) / 0.7f, 0, 1), 3.f);
  float isz = ic * (0.85f + 0.15f * pop);
  R.glow(cx - isz * 0.5f, cy - isz * 0.5f, isz, isz, vec3(1.f, 0.62f, 0.25f), (0.22f + 0.1f * pulse) * e, isz * 0.2f, 60 * s);
  R.glow(cx - isz * 0.5f, cy - isz * 0.5f, isz, isz, C_ACCENT, (0.18f + 0.08f * pulse) * e, isz * 0.2f, 26 * s);
  auto ring = [&](float r, int segs, float gap, float rot, float th, vec3 c, float a) {
    for (int k = 0; k < segs; k++) {
      float a0 = rot + k * 2 * PI / segs, a1 = a0 + (2 * PI / segs) * (1 - gap);
      int n = 10;
      for (int j = 0; j < n; j++) {
        float b0 = a0 + (a1 - a0) * j / n, b1 = a0 + (a1 - a0) * (j + 1) / n;
        R.line(cx + cosf(b0) * r, cy + sinf(b0) * r, cx + cosf(b1) * r, cy + sinf(b1) * r, th, c, a);
      }
    }
  };
  float rr = ic * 0.78f;
  ring(rr, 3, 0.18f, t * 0.6f, 2.5f * s, C_ACCENT, 0.85f * e * pop);
  ring(rr + 12 * s, 24, 0.55f, -t * 0.25f, 1.5f * s, C_ACCENT, 0.45f * e * pop);
  ring(rr + 26 * s, 2, 0.7f, -t * 0.9f + 1.f, 1.f * s, vec3(1.f, 0.66f, 0.3f), 0.7f * e * pop);
  for (int k = 0; k < 4; k++) {   // ticks at the cardinal points
    float a = k * PI * 0.5f + PI * 0.25f;
    R.line(cx + cosf(a) * (rr + 34 * s), cy + sinf(a) * (rr + 34 * s), cx + cosf(a) * (rr + 46 * s), cy + sinf(a) * (rr + 46 * s), 2 * s, C_ACCENT, 0.7f * e * pop);
  }
  if (icon) R.image((GLuint)icon, cx - isz * 0.5f, cy - isz * 0.5f, isz, isz, 0, 0, 1, 1, e);
  else R.rect(cx - isz * 0.5f, cy - isz * 0.5f, isz, isz, vec3(0.05f, 0.1f, 0.18f), e, isz * 0.18f);
  // a light sweep crossing the icon every few seconds
  float sw = fmodf(t * 0.45f, 1.6f);
  if (sw < 1.f) {
    float sx = cx - isz * 0.5f + isz * sw;
    float hh = isz * 0.8f * sinf(sw * PI);
    R.rect(sx - 3 * s, cy - hh * 0.5f, 6 * s, hh, vec3(1, 1, 1), 0.18f * e, 3 * s);
    R.glow(sx - 3 * s, cy - hh * 0.5f, 6 * s, hh, vec3(1, 1, 1), 0.12f * e, 3 * s, 14 * s);
  }
  // title: letters arrive one by one with a short glow
  const std::string title = "SOLACE EXPRESS";
  float ts = 64 * s, tw = R.textWidth(title, ts) + (title.size() - 1) * 10 * s, tx = cx - tw * 0.5f, ty = cy + rr + 42 * s;
  for (size_t i = 0; i < title.size(); i++) {
    float li = clampf((t - 0.5f - i * 0.07f) / 0.35f, 0, 1);
    std::string ch(1, title[i]);
    float cw = R.textWidth(ch, ts);
    if (li > 0 && li < 1) R.glow(tx, ty + 10 * s, cw, ts * 0.7f, C_ACCENT, 0.5f * (1 - li) * e, 4 * s, 18 * s);
    R.text(tx, ty - (1 - li) * 14 * s, ts, ch, mixc(C_ACCENT, C_TEXT, li), li * e, 0, true);
    tx += cw + 10 * s;
  }
  float ul = clampf((t - 1.2f) / 0.6f, 0, 1);
  R.rect(cx - tw * 0.5f * ul, ty + ts * 1.02f, tw * ul, 2 * s, C_ACCENT, 0.9f * e);
  R.glow(cx - tw * 0.5f * ul, ty + ts * 1.02f, tw * ul, 2 * s, C_ACCENT, 0.4f * e, 1 * s, 10 * s);
  R.text(cx, ty + ts * 1.02f + 14 * s, 15 * s, "P I L O T   C A R E E R   A C R O S S   T H E   S O L A C E   I S L A N D S", C_ACCENT, 0.85f * ul * e, 1, false);
  // progress bar: segmented track, glowing fill head, percentage and the current stage
  float bw = std::min(680 * s, W - 80 * s), bh = 10 * s, bx = cx - bw * 0.5f, by = H - 104 * s;
  float p = clampf(progress, 0, 1);
  R.text(bx, by - 30 * s, 14 * s, stage, C_TEXT, 0.9f * e, 0, false);
  R.text(bx + bw, by - 30 * s, 14 * s, fmt("%3.0f%%", p * 100), C_ACCENT, e, 2, false);
  R.rect(bx - 3 * s, by - 3 * s, bw + 6 * s, bh + 6 * s, C_ACCENT, 0.08f * e, 3 * s);
  R.rectOutline(bx - 3 * s, by - 3 * s, bw + 6 * s, bh + 6 * s, C_ACCENT, 0.35f * e, 3 * s, 1 * s);
  int segs = 48; float gw = bw / segs;
  for (int k = 0; k < segs; k++) {
    float f = clampf(p * segs - k, 0, 1);
    if (f <= 0) { R.rect(bx + k * gw + 1 * s, by, gw - 2 * s, bh, C_ACCENT, 0.06f * e, 1 * s); continue; }
    vec3 c = mixc(C_ACCENT * 0.7f, C_ACCENT, (float)k / segs);
    R.rect(bx + k * gw + 1 * s, by, (gw - 2 * s) * f, bh, c, e, 1 * s);
  }
  float hx = bx + bw * p;
  if (p > 0.001f && p < 0.999f) {
    R.glow(hx - 4 * s, by - 2 * s, 8 * s, bh + 4 * s, C_ACCENT, (0.5f + 0.3f * pulse) * e, 4 * s, 16 * s);
    R.rect(hx - 1.5f * s, by - 4 * s, 3 * s, bh + 8 * s, vec3(1, 1, 1), 0.9f * e, 1.5f * s);
  }
  // a scanning shimmer travelling the filled part
  float shx = bx + fmodf(t * 260 * s, std::max(bw * p, 1.f));
  if (p > 0.05f) R.rect(shx, by, 18 * s, bh, vec3(1, 1, 1), 0.18f * e, 2 * s);
  R.text(cx, by + 24 * s, 12 * s, shaderFirstRun ? "First launch: the shaders are compiled for your GPU and cached, later launches start much faster" : "Shaders loaded from the cache",
             C_DIM, 0.75f * e, 1, false);
  // copyright
  {   // the font has no copyright sign: a ringed C ahead of the line
    const std::string cr = "2026 CDAIII.  ALL RIGHTS RESERVED.";
    float cs = 12 * s, lw = R.textWidth(cr, cs), d = 14 * s, x0 = cx - (lw + d + 6 * s) * 0.5f, y0 = H - 34 * s;
    R.rectOutline(x0, y0 - 1 * s, d, d, C_DIM, 0.7f * e, d * 0.5f, 1.2f * s);
    R.text(x0 + d * 0.5f, y0 + 1.5f * s, 9 * s, "C", C_DIM, 0.8f * e, 1, false);
    R.text(x0 + d + 6 * s, y0, cs, cr, C_DIM, 0.7f * e, 0, false);
  }
}

// ------------------------------------------------------------------ pre-flight loading screen
void Game::drawLoading() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  bool air = !plane.onGround;
  const Airport& ap = g_world.airports[air ? contract.to : contract.from];
  // a picture rendered by render_loading.bat - the airport, or the aircraft in flight - when there is one on disk
  std::string key = air ? fmt("air_%d", (int)(plane.spec - kAircraft)) : std::string(ap.code);
  auto li = loadImg.find(key);
  if (li == loadImg.end()) {
    unsigned tex = 0; int w = 0, h = 0; std::vector<uint8_t> px;
    // a picture rendered on this PC by render_loading.bat (.png) wins over the one that ships with the game (.jpg)
    for (const char* ext : {".png", ".jpg"})
      if (!tex && readImage((assetDir + "/loading/" + key + ext).c_str(), w, h, px)) tex = g_ren.makeTexture(px.data(), w, h);
    li = loadImg.emplace(key, tex).first;
  }
  unsigned pic = li->second;
  if (!loadMap && !pic) {   // otherwise the aerial image of the airport, rendered once from the real terrain
    loadMap = true; gpsMapValid = false;
    g_ren.renderMap(ap.x, ap.z, 2400.f, 1536);
  }
  float rt = loadReadyT >= 0 ? loadT - loadReadyT : 0.f;
  float cover = 1.f - smoothstepf(0.f, 1.4f, rt);   // the card fades out onto the live shot once the scenery is in
  if (cover > 0.001f) {
    g_ren.rect(0, 0, W, H, vec3(0.01f, 0.02f, 0.035f), cover);
    if (pic) {   // slow push-in over the picture (cropped to fill the screen)
      float z = 1.f - 0.06f * std::min(loadT, 12.f) / 12.f, asp = (W / H) / (16.f / 9.f);
      float su = z * std::min(1.f, asp), sv = z * std::min(1.f, 1.f / asp);
      g_ren.image(pic, 0, 0, W, H, 0.5f - su * 0.5f, 0.5f - sv * 0.5f, 0.5f + su * 0.5f, 0.5f + sv * 0.5f, cover);
      g_ren.flushUIPublic();
    } else if (g_ren.mapTex()) {   // slow push-in over the aerial image
      float z = 0.62f - 0.05f * std::min(loadT, 12.f) / 12.f;
      float sv = z, su = z * W / H;
      if (su > 0.98f) { sv *= 0.98f / su; su = 0.98f; }
      g_ren.image(g_ren.mapTex(), 0, 0, W, H, 0.5f - su * 0.5f, 0.5f - sv * 0.5f, 0.5f + su * 0.5f, 0.5f + sv * 0.5f, cover);
      g_ren.flushUIPublic();
    }
    // runway marker on the image
  }
  // legibility gradients
  g_ren.rectGrad(0, H * 0.55f, W, H * 0.45f, vec3(0, 0, 0), vec3(0.0f, 0.01f, 0.02f), 0.75f);
  g_ren.rectGrad(0, 0, W, 90 * s, vec3(0.0f, 0.01f, 0.02f), vec3(0, 0, 0), 0.55f);
  // header
  bool research = researchFlight;
  g_ren.text(40 * s, 26 * s, 13 * s, research ? "CONFIDENTIAL RESEARCH FLIGHT" : contract.type == CT_LESSON ? "FLIGHT LESSON" : "CONTRACT", C_ACCENT, 1, 0, false);
  g_ren.text(40 * s, 44 * s, 30 * s, contract.title, C_TEXT, 1);
  // mission card
  float cx = 40 * s, cy = H - 250 * s, cw = std::min(720 * s, W - 80 * s);
  const Airport& from = g_world.airports[contract.from];
  const Airport& to = g_world.airports[contract.to];
  header(cx, cy, cw, air ? "IN FLIGHT  //  INBOUND" : "DEPARTURE");
  g_ren.text(cx, cy + 26 * s, 40 * s, fmt("%s", ap.code), C_TEXT, 1);
  g_ren.text(cx + g_ren.textWidth(ap.code, 40 * s) + 16 * s, cy + 40 * s, 20 * s, ap.name, C_DIM, 1);
  float ly = cy + 86 * s;
  auto kv = [&](float x, const char* k, const std::string& v) {
    g_ren.text(x, ly, 11 * s, k, C_DIM, 1, 0, false);
    g_ren.text(x, ly + 15 * s, 17 * s, v, C_TEXT, 1);
  };
  kv(cx, "AIRCRAFT", plane.spec->name);
  if (contract.from != contract.to) kv(cx + 240 * s, "ROUTE", fmt("%s  >  %s", from.code, to.code));
  kv(cx + 420 * s, "CONDITIONS", wx.describe());
  // progress / ready prompt
  float by = H - 70 * s, bw = cw, bh = 8 * s;
  if (loadReadyT < 0) {
    g_ren.text(cx, by - 24 * s, 13 * s, "PREPARING SCENERY", C_TEXT, 0.9f, 0, false);
    g_ren.text(cx + bw, by - 24 * s, 13 * s, fmt("%3.0f%%", loadShown * 100), C_ACCENT, 1, 2, false);
    g_ren.rect(cx, by, bw, bh, C_ACCENT, 0.12f, 2 * s);
    g_ren.rectGrad(cx, by, std::max(bw * loadShown, 4 * s), bh, C_ACCENT * 0.7f, C_ACCENT, 1, 2 * s);
    float hx = cx + bw * loadShown;
    g_ren.glow(hx - 4 * s, by - 2 * s, 8 * s, bh + 4 * s, C_ACCENT, 0.5f + 0.3f * sinf(realTime * 6.f), 4 * s, 14 * s);
  } else {
    float pulse = 0.6f + 0.4f * sinf(realTime * 3.f);
    float bx = cx, bwid = 380 * s;
    g_ren.glow(bx, by - 12 * s, bwid, 40 * s, C_ACCENT, 0.25f * pulse, 4 * s, 16 * s);
    g_ren.rectGrad(bx, by - 12 * s, bwid, 40 * s, C_ACCENT * 1.05f, C_ACCENT * 0.6f, 0.95f, 4 * s);
    g_ren.text(bx + bwid * 0.5f, by - 1 * s, 17 * s, in.pad ? "PRESS  A  TO FLY" : "CLICK OR PRESS ENTER TO FLY", C_INK, 1, 1, false);
    g_ren.text(bx + bwid + 20 * s, by + 1 * s, 13 * s, in.pad ? "B  BACK" : "ESC  BACK", C_DIM, 0.9f, 0, false);
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
  float titleX = 60 * s;
  if (iconTex) {   // the icon beside the title, in a slowly breathing halo
    float isz = 76 * s, pulse = 0.5f + 0.5f * sinf(realTime * 1.6f);
    g_ren.glow(60 * s, 90 * s, isz, isz, vec3(1.f, 0.62f, 0.25f), 0.18f + 0.08f * pulse, isz * 0.2f, 22 * s);
    g_ren.image(iconTex, 60 * s, 90 * s, isz, isz);
    titleX += isz + 22 * s;
  }
  g_ren.text(titleX, iconTex ? 92 * s : 86 * s, iconTex ? 62 * s : 74 * s, "SOLACE EXPRESS", C_TEXT, 1, 0);
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
    if (button(60 * s, y, bw, bh, "New Career", true, !hasSave)) { if (hasSave) confirmNew = true; else { pendingCareer.reset(); career.newGame(); saveGame(); screen = SCR_HUB; } }
  } else {
    if (button(60 * s, y, bw * 0.48f, bh, "Overwrite!", true, true)) { pendingCareer.reset(); career.newGame(); saveGame(); confirmNew = false; screen = SCR_HUB; }
    if (button(60 * s + bw * 0.52f, y, bw * 0.48f, bh, "Cancel")) confirmNew = false;
  }
  y += bh + 14 * s;
  if (button(60 * s, y, bw, bh, "Settings")) { screen = SCR_HUB; hubTab = TAB_SETTINGS; settingsPage = 0; }
  y += bh + 14 * s;
  if (button(60 * s, y, bw, bh, "Controls")) { screen = SCR_HUB; hubTab = TAB_SETTINGS; settingsPage = 1; }
  {   // (Project NIGHTGLASS has no button: the terminal opens on the held combo - the sticks on the pad, U + I on the keyboard)
    if (in.pressed[K_ESC]) confirmRes = false;
  }
  y += bh + 14 * s;
  if (button(60 * s, y, bw, bh, "Quit")) quit = true;
}

// ------------------------------------------------------------------ hub
void Game::drawHub() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  uiGlass = true;   // (panels and cards in the hub's glass style; reset at the end)
  // atmosphere, as on the main menu: the live scene behind dark glass and a scan line drifting down
  g_ren.rect(0, 0, W, H, vec3(0.0f, 0.01f, 0.02f), 0.28f);
  float scan = fmodf(realTime * 60.f * s, H + 80 * s) - 40 * s;
  g_ren.rectGrad(0, scan, W, 40 * s, vec3(0, 0, 0), C_ACCENT, 0.035f);
  // header strip: icon, title and its underline, the licence beneath; bank, reputation and location on the right
  g_ren.rectGrad(0, 0, W, 72 * s, vec3(0.01f, 0.035f, 0.06f), vec3(0.0f, 0.012f, 0.025f), 0.7f);
  g_ren.rect(0, 72 * s, W, 1 * s, C_ACCENT, 0.18f);
  float hx = 24 * s;
  if (iconTex) {
    float isz = 40 * s, pulse = 0.5f + 0.5f * sinf(realTime * 1.6f);
    g_ren.glow(hx, 16 * s, isz, isz, vec3(1.f, 0.62f, 0.25f), 0.14f + 0.06f * pulse, isz * 0.2f, 12 * s);
    g_ren.image(iconTex, hx, 16 * s, isz, isz);
    hx += isz + 14 * s;
  }
  float tw = g_ren.text(hx, 13 * s, 26 * s, "SOLACE EXPRESS", C_TEXT, 1);
  g_ren.rect(hx + 2 * s, 44 * s, 54 * s, 2 * s, C_ACCENT, 1);
  g_ren.rect(hx + 60 * s, 44.5f * s, std::max(tw - 60 * s, 20 * s), 1 * s, C_ACCENT, 0.35f);
  g_ren.text(hx + 2 * s, 50 * s, 12 * s, std::string("CAREER  //  ") + licenseName(career.license), C_ACCENT, 1, 0, false);
  const Airport& loc = g_world.airports[career.location];
  float rx = W - 24 * s;   // stat chips, laid out from the right edge
  auto chip = [&](const char* k, const std::string& v, vec3 c, float maxW) {
    float vs = 19 * s;
    std::string vv = ellipsize(v, maxW, vs);
    float w = std::max(g_ren.textWidth(vv, vs), g_ren.textWidth(k, 11 * s));
    float x0 = rx - w;
    g_ren.text(x0, 16 * s, 11 * s, k, C_DIM, 1, 0, false);
    g_ren.text(x0, 31 * s, vs, vv, c, 1);
    g_ren.rect(x0 - 18 * s, 18 * s, 1 * s, 34 * s, C_ACCENT, 0.25f);
    rx = x0 - 36 * s;
  };
  chip("LOCATION", fmt("%s  %s", loc.code, loc.name), C_TEXT, std::max(140 * s, W * 0.26f));
  chip("REPUTATION", fmt("%d", career.reputation), C_ACCENT, 120 * s);
  chip("BANK", fmtMoney(career.money), career.money < 0 ? C_BAD : C_GOOD, 160 * s);
  // tabs: numbered labels over a hairline, a glowing bar glides under the active one
  const char* tabs[] = {"CONTRACTS", "HANGAR", "AIRLINE", "LOGBOOK", "SETTINGS"};
  const int nTabs = 5;
  float tx = 24 * s, ty = 80 * s, tabW[5], tabX[5];
  g_ren.rectGrad(0, 73 * s, W, 44 * s, vec3(0.008f, 0.028f, 0.05f), vec3(0.0f, 0.01f, 0.02f), 0.55f);   // glass band behind the tabs
  // the tabs fit the room left of the Radio and Main Menu buttons: smaller type, then without their numbers, when the
  // window is narrow for the UI scale (the review of v3.31.0, U4: Radio covered SETTINGS at 720p and 140%)
  float tfs = 15 * s; bool numbered = true;
  {
    const float room = W - 280 * s - 16 * s - tx;
    auto total = [&](float fsz, bool num) { float t = 0; for (int i = 0; i < nTabs; i++) t += g_ren.textWidth(num ? fmt("%02d  %s", i + 1, tabs[i]) : std::string(tabs[i]), fsz) + 28 * s + 6 * s; return t; };
    float t = total(tfs, true);
    if (t > room) tfs = std::max(11 * s, tfs * room / t);
    if (total(tfs, true) > room) { numbered = false; tfs = 15 * s; t = total(tfs, false); if (t > room) tfs = std::max(10 * s, tfs * room / t); }
  }
  for (int i = 0; i < nTabs; i++) {
    std::string lab = numbered ? fmt("%02d  %s", i + 1, tabs[i]) : std::string(tabs[i]);
    float w = g_ren.textWidth(lab, tfs) + 28 * s;
    tabX[i] = tx; tabW[i] = w;
    bool hov = hovered(tx, ty, w, 34 * s);
    float h = anim(uid(tx, ty, "tab"), hov ? 1.f : 0.f, 14);
    if (h > 0.01f) g_ren.rectGrad(tx, ty, w, 34 * s, vec3(0.03f, 0.1f, 0.15f), vec3(0.01f, 0.04f, 0.07f), 0.5f * h, 2 * s);
    vec3 tc = hubTab == i ? C_TEXT : mixc(C_DIM, C_TEXT, h);
    const float tyt = ty + 17 * s - tfs * 0.53f;
    if (numbered) g_ren.text(tx + 14 * s, tyt, tfs, fmt("%02d", i + 1), hubTab == i ? C_ACCENT : C_ACCENT * 0.55f, 1, 0, false);
    g_ren.text(tx + 14 * s + (numbered ? g_ren.textWidth("00  ", tfs) : 0.f), tyt, tfs, tabs[i], tc, 1, 0, false);
    if (hov && in.mPressed[0] && hubTab != i) { hubTab = i; g_audio.trigger(SFX_CLICK); }
    tx += w + 6 * s;
  }
  g_ren.rect(24 * s, ty + 34 * s, tx - 30 * s, 1 * s, C_ACCENT, 0.15f);
  float ux = anim(0x7ab5u, tabX[hubTab], 14), uw = anim(0x7ab6u, tabW[hubTab], 14);
  g_ren.glow(ux + 8 * s, ty + 32 * s, uw - 16 * s, 3 * s, C_ACCENT, 0.5f, 1.5f * s, 8 * s);
  g_ren.rect(ux + 8 * s, ty + 32 * s, uw - 16 * s, 3 * s, C_ACCENT, 1);
  if (button(W - 280 * s, ty, 120 * s, 34 * s, showRadio ? "Radio <" : "Radio", true, showRadio)) showRadio = !showRadio;
  if (button(W - 150 * s, ty, 126 * s, 34 * s, "Main Menu")) { screen = SCR_MENU; if (!retryCommit()) {} else saveGame(); }
  float cx = 24 * s, cy = 126 * s, cw = W - 48 * s, ch = H - 146 * s;
  switch (hubTab) {
    case TAB_CONTRACTS: drawHubContracts(cx, cy, cw, ch); break;
    case TAB_HANGAR: drawHubHangar(cx, cy, cw, ch); break;
    case TAB_AIRLINE: drawHubAirline(cx, cy, cw, ch); break;
    case TAB_LOGBOOK: drawHubLogbook(cx, cy, cw, ch); break;
    default: { float pw = std::min(cw, (settingsPage == 1 ? 940 : 760) * s); panel(cx, cy, pw, ch); drawSettings(cx + 24 * s, cy + 20 * s, pw - 48 * s, ch - 40 * s); break; }
  }
  if (showRadio) drawRadioPanel(W - 460 * s, 126 * s);
  uiGlass = false;
  if (in.pressed[K_ESC]) { if (showRadio) showRadio = false; else { screen = SCR_MENU; if (retryCommit()) saveGame(); } }
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
  struct Card { const Contract* c; bool story; bool free; bool job = false; int trial = -1; };
  std::vector<Card> cards;
  static Contract jobCont;   // the open job's next leg (pinned on top while the job waits for it)
  const bool openJob = career.job && career.job->state == Career::JobState::RECOVERY;
  if (hubList == 1) { for (int k = 0; k < TR_COUNT; k++) cards.push_back({nullptr, false, false, false, k}); }
  else {
    if (openJob) { jobCont = career.job->continuation(); cards.push_back({&jobCont, false, false, true}); }
    const Contract* st = career.nextStory();
    if (st) cards.push_back({st, true, false});
    cards.push_back({nullptr, false, true});
    for (auto& c : career.board) cards.push_back({&c, false, false});
  }
  selContract = std::clamp(selContract, 0, (int)cards.size() - 1);
  float cy = y + 14 * s;
  {   // the list switch: the work, or the trials (off the books, scored on a local board)
    float sw = (lw - 40 * s) * 0.5f;
    if (button(x + 16 * s, cy - 4 * s, sw, 26 * s, hubList == 0 ? "WORK" : "work", true, hubList == 0) && hubList != 0) { hubList = 0; selContract = 0; selAircraft = -1; }
    if (button(x + 24 * s + sw, cy - 4 * s, sw, 26 * s, hubList == 1 ? "TRIALS" : "trials", true, hubList == 1) && hubList != 1) { hubList = 1; selContract = 0; selAircraft = -1; }
    cy += 32 * s;
  }
  if (hubList == 0) { header(x + 16 * s, cy, lw - 32 * s, ellipsize(career.finished ? "CAMPAIGN COMPLETE - FREELANCE JOBS CONTINUE" : "AVAILABLE WORK", lw - 60 * s, 13 * s)); cy += 28 * s; }
  else { header(x + 16 * s, cy, lw - 32 * s, "TRIALS  -  OFF THE BOOKS, LOCAL BEST TIMES"); cy += 28 * s; }
  // the cards scroll (mouse wheel, or the right stick) when the list is longer than the panel: every job stays
  // reachable (the review of v3.31.0, U2: the last two freelance jobs had no way in), the selected card kept in view
  const float chh = 66 * s, step = chh + 8 * s, listTop = cy, listBot = y + h - 8 * s;
  const int visCards = std::max(1, (int)floorf((listBot - listTop + 8 * s) / step)), nCards = (int)cards.size();
  static int listScroll = 0; static int listFor = -1, selSeen = -1;
  if (listFor != hubList) { listFor = hubList; listScroll = 0; }
  if (hovered(x, listTop, lw, listBot - listTop) && in.wheel != 0) { listScroll -= (int)in.wheel; in.wheel = 0; }
  if (selSeen != selContract) {   // a new selection (keys, a stick) scrolls itself into view
    selSeen = selContract;
    if (selContract < listScroll) listScroll = selContract;
    if (selContract >= listScroll + visCards) listScroll = selContract - visCards + 1;
  }
  listScroll = std::clamp(listScroll, 0, std::max(0, nCards - visCards));
  if (listScroll > 0) g_ren.text(x + lw - 18 * s, listTop - 24 * s, 12 * s, fmt("^ %d more", listScroll), C_DIM, 1, 2);
  if (listScroll + visCards < nCards) g_ren.text(x + lw - 18 * s, listBot - 6 * s, 12 * s, fmt("%d more v", nCards - listScroll - visCards), C_DIM, 1, 2);
  for (int i = listScroll; i < nCards && i < listScroll + visCards; i++) {
    bool sel = i == selContract;
    bool hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    card(x + 10 * s, cy, lw - 20 * s, chh, sel, hov, cards[i].job ? C_GOOD : cards[i].story ? C_WARN : C_ACCENT);
    if (hov && in.mPressed[0]) { selContract = i; selAircraft = -1; launchFuelKg = -1; g_audio.trigger(SFX_CLICK); }
    if (cards[i].trial >= 0) {
      int k = cards[i].trial;
      auto it = trialBest.find(trialId(k));
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, std::string("TRIAL  ") + trialName(k), C_TEXT);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize(it != trialBest.end() && !it->second.empty() ? "Best: " + trialScore(k, it->second[0]) : "No time set yet", lw - 48 * s, 14 * s), it != trialBest.end() ? C_GOOD : C_DIM, 1);
    } else if (cards[i].free) {
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, "Free Flight / Ferry", C_TEXT);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize("Fly anywhere for fun or to reposition. No pay.", lw - 48 * s, 14 * s), C_DIM, 1);
    } else {
      const Contract& c = *cards[i].c;
      std::string tag = cards[i].job ? fmt("JOB CONTINUES  LEG %d  ", career.job->legs + 1) : cards[i].story ? fmt("STORY CH.%d  ", c.chapter) : "";
      float cardW = lw - 62 * s;
      fitText(x + 24 * s, cy + 9 * s, cardW, 17 * s, 13 * s, tag + c.title, cards[i].story ? C_WARN : C_TEXT);
      std::string sub = fmt("%s  %s > %s  %.0f km", contractTypeName(c.type), g_world.airports[c.from].code, g_world.airports[c.to].code, g_world.distanceKm(c.from, c.to));
      float payW = g_ren.text(x + lw - 24 * s, cy + 37 * s, 16 * s, fmtMoney(c.payout), C_GOOD, 1, 2);
      g_ren.text(x + 24 * s, cy + 37 * s, 14 * s, ellipsize(sub, cardW - payW - 12 * s, 14 * s), C_DIM, 1);
    }
    cy += step;
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
  if (cd.trial >= 0) { free = trialContract(cd.trial); cd.c = &free; }
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
  // The briefing and its figures scroll (mouse wheel) in a region of their own above the aircraft chooser, whose rows
  // keep their place at the panel's foot: a long briefing never pushes the chooser off the panel or runs under it
  const float chooserH = 26 * s + ((kNumAircraft + 1) / 2) * (30 * s + 6 * s);
  const float chooserTop = std::min(std::max(y + h - 70 * s - chooserH, y + 70 * s + mapW + 8 * s), y + h - 70 * s - 26 * s - 2 * (30 * s + 6 * s));
  // (the route map ends above the chooser: on a short panel - a large UI scale - it is drawn smaller rather than over it)
  mapW = std::max(60 * s, std::min(mapW, chooserTop - (y + 60 * s) - 16 * s));
  const float detTop = py, detBot = chooserTop - 10 * s;
  static float detScroll = 0.f, detMax = 0.f; static int detFor = -1;
  if (detFor != hubList * 1000 + selContract) { detFor = hubList * 1000 + selContract; detScroll = 0.f; detMax = 0.f; }
  if (hovered(px, detTop, textW, detBot - detTop) && in.wheel != 0) { detScroll -= in.wheel * 48.f * s; in.wheel = 0; }
  detScroll = std::clamp(detScroll, 0.f, detMax);
  auto inView = [&](float yy, float hh) { return yy >= detTop && yy + hh <= detBot; };   // (a control scrolled out of the region is neither drawn nor clickable)
  g_ren.uiClip(px - 4 * s, detTop, px + textW + 4 * s, detBot);
  py -= detScroll;
  for (auto& l : wrap(c.brief, textW, 16 * s)) { g_ren.text(px, py, 16 * s, l, C_TEXT, 0.92f); py += 22 * s; }
  if (cd.job) {   // where the job stands: legs flown, the clock, the load's place
    const Career::JobState& J = *career.job;
    std::string so = fmt("The load is at %s after %d leg%s (%.0f min on the clock%s). Fly it on to %s to be paid; release it and it stays here.",
                         g_world.airports[J.at].name, J.legs, J.legs == 1 ? "" : "s", J.jobClockMin, c.timeLimitMin > 0 ? fmt(" of %.0f", c.timeLimitMin).c_str() : "", g_world.airports[c.to].name);
    for (auto& l : wrap(so, textW, 15 * s)) { g_ren.text(px, py, 15 * s, l, C_GOOD, 0.95f); py += 20 * s; }
  }
  py += 10 * s;
  auto row = [&](const std::string& k, const std::string& v, vec3 col = C_TEXT) {
    g_ren.text(px, py + 3 * s, 11.5f * s, upperS(k), C_DIM, 0.95f, 0, false);
    auto vl = wrap(v, textW - 130 * s, 15 * s);
    for (size_t i = 0; i < vl.size(); i++) { g_ren.text(px + 130 * s, py, 15 * s, vl[i], col, 1); py += (i + 1 < vl.size() ? 19 : 23) * s; }
    if (vl.empty()) py += 23 * s;
  };
  if (cd.free) {
    if (inView(py - 4 * s, 28 * s) && button(px + 130 * s, py - 4 * s, 34 * s, 28 * s, "<")) { do freeDest = (freeDest + (int)g_world.airports.size() - 1) % g_world.airports.size(); while (freeDest == career.location); }
    g_ren.text(px, py, 15 * s, "Destination", C_DIM, 1);
    g_ren.text(px + 172 * s, py + 1 * s, 15 * s, ellipsize(fmt("%s %s", g_world.airports[freeDest].code, g_world.airports[freeDest].name), textW - 172 * s - 48 * s, 15 * s), C_ACCENT, 1);
    if (inView(py - 4 * s, 28 * s) && button(px + textW - 40 * s, py - 4 * s, 34 * s, 28 * s, ">")) { do freeDest = (freeDest + 1) % g_world.airports.size(); while (freeDest == career.location); }
    py += 30 * s;
  }
  const Airport& A = g_world.airports[c.from]; const Airport& B = g_world.airports[c.to];
  row("Route", fmt("%s %s  >  %s %s", A.code, A.name, B.code, B.name));
  row("Distance", fmt("%.1f km", g_world.distanceKm(c.from, c.to)));
  row("Destination", fmt("Rwy %02d/%02d, %.0f m %s, elev %s", B.rwyNumber(false), B.rwyNumber(true), B.length, surfaceName(B.surface), fmtAlt(B.elev).c_str()));
  if (c.cargoKg || c.pax) row("Load", c.pax ? fmt("%d passengers, %d kg", c.pax, c.cargoKg) : fmt("%d kg cargo%s", c.cargoKg, c.fragile ? " (FRAGILE)" : ""));
  if (c.timeLimitMin > 0) row("Deadline", fmt("%.0f minutes", c.timeLimitMin), C_BAD);
  row("Weather", c.wx.describe());
  if (c.wxShift) row("Forecast", "by arrival: " + c.wxEnd.describe(), C_WARN);
  if (cd.trial >= 0) {   // the local board
    auto it = trialBest.find(trialId(cd.trial));
    std::string b;
    if (it != trialBest.end()) for (size_t i = 0; i < it->second.size(); i++) b += (i ? "   " : "") + fmt("%d. %s", (int)i + 1, trialScore(cd.trial, it->second[i]).c_str());
    row("Best", b.empty() ? "no time set yet" : b, C_GOOD);
    row("Counts for", "nothing: no pay, no fees, no logbook entry", C_DIM);
  }
  if (c.payout) row("Payment", c.repBonusPct > 0 ? fmt("%s (incl. +%d%% reputation bonus)", fmtMoney(c.payout).c_str(), c.repBonusPct) : fmtMoney(c.payout), C_GOOD);
  if (selAircraft >= 0 && selAircraft < kNumAircraft) {   // for the aircraft picked below (last frame's choice)
    auto esrc = career.canFly(c, selAircraft);
    if (esrc != Career::SRC_NONE) {
      // (the plan runs the autopilot's approach planner: cached for this job, aircraft and career state)
      static std::string planKey; static Career::LaunchPlan e;
      std::string key = fmt("%s|%d|%d|%d|%d|%u", c.id.c_str(), selAircraft, (int)esrc, career.location, career.money < 1500, career.boardSeed);
      if (key != planKey) { e = career.plan(c, selAircraft, esrc); planKey = key; }
      if (!e.flown) applyQuote(c, e, true);   // (the flown time replaces the quick estimate when it's in)
      int ops = e.fees() + e.fuelCostEst;
      if (c.payout > 0 || ops > 0) {
        std::string parts;
        auto part = [&](const char* n, int v) { if (v) parts += (parts.empty() ? "" : ", ") + fmt("%s %s", n, fmtMoney(v).c_str()); };
        part("hire", e.hire); part("positioning", e.positioning); part("ferry", e.ferry); part("fuel ~", e.fuelCostEst);
        row("Operating costs", ops ? fmt("%s (%s)", fmtMoney(ops).c_str(), parts.c_str()) : std::string(e.fuel == Career::LaunchPlan::FUEL_INCLUDED ? "none - fuel included" : "none"), C_DIM);
        row("Est. net", fmtMoney(e.net), e.net >= 0 ? C_GOOD : C_BAD);
      }
      row("Est. time", fmt("about %.0f min (+- %.0f)%s", e.minutesEst, e.minutesSigma, e.flown || c.forceAircraft >= 0 ? "" : "  - flying it on the autopilot..."), e.mayBeLate(c.timeLimitMin) ? C_BAD : C_TEXT);
      {   // fuel and weight: the tanks at take-off (arrows: 5% of the tanks a step), the take-off weight against the
          // limit, the roll it needs here, and the uplift's price for an owned aircraft
        const AircraftSpec& sp = kAircraft[selAircraft];
        if (!sp.special) {
          float fuel = chosenFuel(c, selAircraft, esrc, e);
          float payload = (float)c.cargoKg + c.pax * 85.f + 85.f, mass = sp.emptyMass + fuel + payload;
          bool heavy = mass > sp.maxMass() + 0.5f;
          const PerfModel& P = Plane::perf(&sp);
          float sigma = expf(-g_world.airports[c.from].elev / 8500.f);
          float roll = P.toRoll > 0 ? P.toRoll * (mass / sp.maxMass()) * (mass / sp.maxMass()) / sigma : 0.f;
          Career::LaunchPlan ef = e; career.planFuel(ef, c, fuel);
          std::string v = fmt("%.0f kg (%.0f%%)  -  take-off %.0f of %.0f kg%s", fuel, 100.f * fuel / sp.maxFuel, mass, sp.maxMass(), heavy ? "  OVERWEIGHT" : "");
          if (roll > 0) v += fmt(", roll ~%.0f m", roll);
          if (ef.fuel == Career::LaunchPlan::FUEL_PURCHASED) v += ef.fuelUpliftKg > 0.5f ? fmt(", uplift %.0f kg for %s", ef.fuelUpliftKg, fmtMoney(ef.fuelCostEst).c_str()) : " (tanks hold it)";
          if (inView(py - 4 * s, 24 * s) && button(px + 130 * s - 36 * s, py - 4 * s, 28 * s, 24 * s, "<")) launchFuelKg = std::max(sp.maxFuel * 0.1f, fuel - sp.maxFuel * 0.05f);
          if (inView(py - 4 * s, 24 * s) && button(px + textW - 30 * s, py - 4 * s, 28 * s, 24 * s, ">")) launchFuelKg = std::min(sp.maxFuel, fuel + sp.maxFuel * 0.05f);
          float pyRow = py;
          row("Fuel", v, heavy ? C_BAD : C_TEXT);
          (void)pyRow;
          float est = e.fuelKgEst;
          if (est > 0) { float res = (fuel - est) / est; row("", res < 0.f ? fmt("%.0f kg short of the estimate: expect to run dry", est - fuel) : fmt("reserve +%.0f%% over the estimate of %.0f kg", res * 100.f, est), res < 0.1f ? C_BAD : res < 0.25f ? C_WARN : C_DIM); }
        }
      }
      if (c.payout > 0) {
        std::string rules;
        if (c.timeLimitMin > 0) rules += "late: -50%";
        if (c.pax > 0) rules += std::string(rules.empty() ? "" : ", ") + "passengers: -15% past 45 deg bank or 1.9 g";
        if (c.fragile) rules += std::string(rules.empty() ? "" : ", ") + "fragile: -40% past 2 g or a 400 fpm touchdown";
        const char* own = c.type == CT_MEDEVAC ? "patient under 70%: -20%, under 35%: -40%; over 90% on time: +10%" : c.type == CT_VIP ? "comfort under 70%: -10%, under 40%: -30%; over 90%: +15% tip"
                        : c.type == CT_NIGHT ? "landing light off at touchdown: -10%" : c.type == CT_IFR ? "below minimums not lined up: -25%; flown to minimums: +5%" : c.type == CT_SURVEY ? "-0.6% per 1% of the pattern outside the band; all in: +5%" : nullptr;
        if (own) rules += std::string(rules.empty() ? "" : ", ") + own;
        if (!rules.empty()) row("Deductions", rules, C_DIM);
      }
      if (!cd.free) row("Challenge", e.challenge, C_WARN);
    }
  }
  if (c.ownedOnly) row("Requirement", "Your own aircraft", C_ACCENT);
  if (c.grantLicense > career.license) row("Reward", std::string("Earns ") + licenseName(c.grantLicense), C_ACCENT);
  if (c.from != career.location && c.type != CT_LESSON) { int pc = career.positioningCost(c); row("Positioning", pc ? fmt("Airline ticket to %s: %s", A.code, fmtMoney(pc).c_str()) : "Free courtesy ride", C_DIM); }
  g_ren.uiClipOff();
  detMax = std::max(0.f, py + detScroll - detBot);   // (how far the region scrolls: next frame's limit)
  if (detMax > 0.f) {   // a thin bar beside the text: where the view is in the briefing
    const float trackH = detBot - detTop, barH = std::max(20 * s, trackH * trackH / (trackH + detMax));
    g_ren.rect(px + textW + 8 * s, detTop, 3 * s, trackH, C_ACCENT, 0.12f);
    g_ren.rect(px + textW + 8 * s, detTop + (trackH - barH) * detScroll / detMax, 3 * s, barH, C_ACCENT, 0.7f);
  }
  {   // the route map in a hairline frame with corner ticks and a caption
    float mx = dx + dw - mapW - 22 * s, my = y + 60 * s;
    drawMapView(mx, my, mapW, mapW, c.from, c.to, &c.wps);
    g_ren.rectOutline(mx - 4 * s, my - 4 * s, mapW + 8 * s, mapW + 8 * s, C_ACCENT, 0.2f, 2 * s, 1 * s);
    brackets(mx - 4 * s, my - 4 * s, mapW + 8 * s, mapW + 8 * s, 10 * s, 1.5f * s, C_ACCENT, 0.8f);
    g_ren.text(mx - 4 * s, my - 22 * s, 11.5f * s, fmt("ROUTE  //  %s > %s", g_world.airports[c.from].code, g_world.airports[c.to].code), C_ACCENT, 0.9f, 0, false);
  }
  // aircraft selection
  py = chooserTop;
  header(px, py, iw, "CHOOSE AIRCRAFT"); const float chooserY = py; py += 26 * s;
  // every aircraft's eligibility first, then the flyable ones listed ahead of the rest: a panel too short for all the
  // rows drops unavailable aircraft, never one the job can be flown in (the review of v3.24.0: an owned Starling, the
  // seventh row, went unseen and unselectable on a long briefing)
  int firstOk = -1;
  std::vector<Career::Source> srcs(kNumAircraft); std::vector<std::string> whys(kNumAircraft);
  std::vector<int> order(kNumAircraft); int nOrder = 0;
  for (int i = 0; i < kNumAircraft; i++) {
    srcs[i] = career.canFly(c, i, &whys[i]);
    if ((c.type == CT_FERRY || c.type == CT_TRIAL) && srcs[i] == Career::SRC_NONE && career.license == LIC_STUDENT && i == 0) srcs[i] = Career::SRC_LESSON;
  }
  // the player's own aircraft first, then the other flyable ones, then the rest
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < kNumAircraft; i++) {
      const bool own = srcs[i] == Career::SRC_OWNED, ok = srcs[i] != Career::SRC_NONE;
      if ((pass == 0 && own) || (pass == 1 && ok && !own) || (pass == 2 && !ok)) { order[nOrder++] = i; if (ok && firstOk < 0) firstOk = i; }
    }
  float rowH = 30 * s, rowStep = rowH + 6 * s;
  float colW = (iw - 10 * s) * 0.5f;
  // the rows scroll (mouse wheel, or the right stick) when the panel can't show them all: every aircraft stays
  // reachable (the review of v3.31.0, U1: an owned Osprey sat below six rentals, out of reach)
  const float chooserBot = y + h - 70 * s;
  const int totalRows = (kNumAircraft + 1) / 2, visRows = std::max(1, (int)floorf((chooserBot - py + 6 * s) / rowStep));
  static int chooserScroll = 0; static int chooserFor = -1;
  if (chooserFor != hubList * 1000 + selContract) { chooserFor = hubList * 1000 + selContract; chooserScroll = 0; }
  if (hovered(px, py, iw, visRows * rowStep) && in.wheel != 0) { chooserScroll -= (int)in.wheel; in.wheel = 0; }
  chooserScroll = std::clamp(chooserScroll, 0, std::max(0, totalRows - visRows));
  int hidden = 0, below = 0, above = chooserScroll * 2;
  for (int j = 0; j < kNumAircraft; j++) {
    const int i = order[j];
    const std::string& why = whys[i];
    const auto src = srcs[i];
    const int row = j / 2 - chooserScroll;
    if (row < 0) continue;
    float rx = px + (j % 2) * (colW + 10 * s), ry = py + row * rowStep;
    if (row >= visRows) { below = kNumAircraft - j; for (int k = j; k < kNumAircraft; k++) hidden += srcs[order[k]] == Career::SRC_NONE; break; }
    bool sel = selAircraft == i;
    bool hov = hovered(rx, ry, colW, rowH) && src != Career::SRC_NONE;
    card(rx, ry, colW, rowH, sel, hov, src == Career::SRC_NONE ? C_DIM * 0.4f : src == Career::SRC_OWNED ? C_GOOD : C_ACCENT);
    if (hov && in.mPressed[0]) { selAircraft = i; launchFuelKg = -1; g_audio.trigger(SFX_CLICK); }
    // (the name and the status share the row: each kept to its side, shortened when the row is narrow)
    const float nameW = g_ren.textWidth(kAircraft[i].name, 15 * s), nameMax = colW * 0.48f;
    g_ren.text(rx + 10 * s, ry + 7 * s, 15 * s, nameW > nameMax ? ellipsize(kAircraft[i].name, nameMax, 15 * s) : std::string(kAircraft[i].name), src == Career::SRC_NONE ? C_DIM * 0.6f : C_TEXT, 1);
    std::string st;
    if (src == Career::SRC_LESSON) st = "School aircraft";
    else if (src == Career::SRC_OWNED) { int fc = career.ferryCost(c, i); st = fc ? fmt("Owned (ferry %s)", fmtMoney(fc).c_str()) : "Owned"; }
    else if (src == Career::SRC_RENT) st = fmt("Rent %s", fmtMoney(kAircraft[i].rentFee).c_str());
    else st = why;
    float sts = 12.5f * s;
    const float stMax = colW - std::min(nameW, nameMax) - (sel ? 46 : 34) * s;
    while (g_ren.textWidth(st, sts) > std::min(colW * 0.55f, stMax) && st.size() > 4) st = st.substr(0, st.size() - 4) + "...";
    g_ren.text(rx + colW - (sel ? 22 : 10) * s, ry + 9 * s, sts, st, src == Career::SRC_NONE ? C_BAD * 0.8f : src == Career::SRC_OWNED ? C_GOOD : C_WARN, 1, 2);
  }
  if (selAircraft >= 0 && (selAircraft >= kNumAircraft || srcs[selAircraft] == Career::SRC_NONE)) selAircraft = -1;
  if (selAircraft < 0) selAircraft = firstOk;
  {   // what is scrolled out of view, on the header's line; the chosen aircraft named there whenever its row is out of view
    bool selShown = false;
    for (int j = above; j < kNumAircraft - below; j++) selShown |= order[j] == selAircraft;
    std::string note;
    if (above + below > 0) note = fmt("%s%d more%s - scroll", above > 0 ? "^ " : "", above + below, below > 0 ? " v" : "") + (hidden > 0 ? fmt(" (%d not available here)", hidden) : std::string());
    if (selAircraft >= 0 && !selShown) note = std::string("flying the ") + kAircraft[selAircraft].name + (note.empty() ? "" : "   " + note);
    if (!note.empty()) g_ren.text(px + iw, chooserY + 2 * s, 12 * s, note, selAircraft >= 0 && !selShown ? C_GOOD : C_DIM, 1, 2);
  }
  bool otherWhileJob = openJob && !cd.job && !cd.free;   // (another job waits until this one is delivered or released)
  bool overweight = false;
  if (selAircraft >= 0) {
    const AircraftSpec& sp = kAircraft[selAircraft]; auto esrc = career.canFly(c, selAircraft);
    if (!sp.special && esrc != Career::SRC_NONE) { Career::LaunchPlan e0 = career.plan(c, selAircraft, esrc); float fuel = chosenFuel(c, selAircraft, esrc, e0); overweight = sp.emptyMass + fuel + c.cargoKg + c.pax * 85.f + 85.f > sp.maxMass() + 0.5f; }
  }
  bool can = selAircraft >= 0 && !commitBlocked() && !otherWhileJob && !overweight;
  if (cd.job) {
    if (button(dx + dw - 262 * s - 2 * 150 * s, y + h - 62 * s, 140 * s, 46 * s, "Release job", !commitBlocked(), false)) releaseJob();
    if (button(dx + dw - 262 * s - 150 * s, y + h - 62 * s, 140 * s, 46 * s, "Practise", can, false) && selAircraft >= 0) practiseApproach(selAircraft, career.canFly(c, selAircraft));
  }
  if (button(dx + dw - 262 * s, y + h - 62 * s, 240 * s, 46 * s, commitBlocked() ? "Save pending" : otherWhileJob ? "Job in progress" : overweight ? "Overweight" : can ? (cd.job ? "CONTINUE" : "FLY!") : "No suitable aircraft", can, can)) {
    auto src = career.canFly(c, selAircraft);
    if ((c.type == CT_FERRY || c.type == CT_TRIAL) && src == Career::SRC_NONE) src = Career::SRC_LESSON;
    Contract go = c;
    if (go.type == CT_FERRY) { go.from = career.location; }
    if (cd.trial >= 0) { startFlight(go, selAircraft, src); isolatedFlight = true; }   // (a trial: off the books)
    else if (cd.job) continueJob(selAircraft, src); else beginCareerFlight(go, selAircraft, src);
  }
  if (commitBlocked()) g_ren.text(px, y + h - 50 * s, 14 * s, "Your last result isn't saved yet (" + saveWhy + "). Retrying...", C_BAD, 1);
  else if (otherWhileJob) g_ren.text(px, y + h - 50 * s, 14 * s, "Deliver or release the job in progress first (its card is at the top).", C_WARN, 1);
  else if (overweight) g_ren.text(px, y + h - 50 * s, 14 * s, "Over the take-off weight limit: take less fuel (the arrows by the fuel row), or a bigger aircraft.", C_BAD, 1);
  else if (!can) g_ren.text(px, y + h - 50 * s, 14 * s, "Tip: check the Hangar to buy an aircraft, or earn your next licence through the story.", C_DIM, 1);
}

void Game::drawHubHangar(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.34f, 420 * s);
  panel(x, y, lw, h);
  float cy = y + 16 * s;
  header(x + 16 * s, cy, lw - 32 * s, "AIRCRAFT MARKET"); cy += 30 * s;
  // the cards share the panel: full size when they fit, squeezed (fonts included) when the market has grown
  const float chh = clampf((y + h - 12 * s - cy - 8 * s * (kNumAircraft - 1)) / kNumAircraft, 34 * s, 54 * s), f = chh / (54 * s);
  for (int i = 0; i < kNumAircraft; i++) {
    bool sel = selHangar == i, hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    card(x + 10 * s, cy, lw - 20 * s, chh, sel, hov, C_ACCENT);
    if (hov && in.mPressed[0]) { selHangar = i; g_audio.trigger(SFX_CLICK); }
    bool owned = career.ownedIndexFor(i) >= 0;
    g_ren.text(x + 24 * s, cy + 8 * f * s, 17 * f * s, kAircraft[i].name, C_TEXT, 1);
    g_ren.text(x + 24 * s, cy + 31 * f * s, 13 * f * s, kAircraft[i].role, C_DIM, 1);
    g_ren.text(x + lw - (sel ? 34 : 24) * s, cy + 18 * f * s, 15 * f * s, owned ? "OWNED" : fmtMoney(kAircraft[i].price), owned ? C_GOOD : C_WARN, 1, 2);
    cy += chh + 8 * s;
  }
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  const AircraftSpec& a = kAircraft[selHangar];
  float px = dx + 24 * s, py = y + 20 * s;
  fitText(px, py, dw - 48 * s, 30 * s, 18 * s, a.name, C_TEXT); py += 42 * s;
  g_ren.text(px, py, 17 * s, ellipsize(a.role, dw - 48 * s, 17 * s), C_ACCENT, 1); py += 30 * s;
  header(px, py, dw - 48 * s, "SPECIFICATIONS"); py += 26 * s;
  {   // performance meters against the best in the market
    float best[3] = {0, 0, 0};
    for (int i = 0; i < kNumAircraft; i++) { best[0] = std::max(best[0], kAircraft[i].cruise); best[1] = std::max(best[1], kAircraft[i].rangeKm); best[2] = std::max(best[2], kAircraft[i].cargoKg + kAircraft[i].pax * 90.f); }
    float v[3] = {a.cruise / best[0], a.rangeKm / best[1], (a.cargoKg + a.pax * 90.f) / best[2]};
    const char* lab[3] = {"SPEED", "RANGE", "PAYLOAD"};
    float mw = (dw - 48 * s - 2 * 18 * s) / 3.f;
    for (int k = 0; k < 3; k++) {
      float mx = px + k * (mw + 18 * s), f = anim(0x6e70u + k * 17 + selHangar * 131, v[k], 8);
      g_ren.text(mx, py, 11.5f * s, lab[k], C_DIM, 0.95f, 0, false);
      g_ren.text(mx + mw, py, 11.5f * s, fmt("%.0f%%", v[k] * 100), C_ACCENT, 0.95f, 2, false);
      for (int seg = 0; seg < 20; seg++) {   // segmented bar
        float sx = mx + seg * (mw / 20.f), on = clampf(f * 20.f - seg, 0.f, 1.f);
        g_ren.rect(sx, py + 18 * s, mw / 20.f - 2 * s, 6 * s, C_ACCENT, 0.12f + 0.8f * on, 1 * s);
      }
    }
    py += 40 * s;
  }
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py + 3 * s, 11.5f * s, upperS(k), C_DIM, 0.95f, 0, false); g_ren.text(px + 200 * s, py, 16 * s, ellipsize(v, dw - 248 * s, 16 * s), C_TEXT, 1); py += 26 * s; };
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
    bool can = career.money >= a.price && career.license >= a.license && !commitBlocked();
    if (button(px, py, 240 * s, 46 * s, fmt("Buy for %s", fmtMoney(a.price).c_str()), can, can)) {
      std::string m; bool bought = false;
      commit([&](Career& k) { bought = k.buy(selHangar, &m); });
      if (bought) g_audio.trigger(SFX_CASH);
      hubMsg = m; hubMsgTime = 4;
    }
    if (!can) g_ren.text(px + 260 * s, py + 14 * s, 15 * s, commitBlocked() ? "Last result not saved yet - retrying" : career.license < a.license ? std::string("Requires ") + licenseName(a.license) : "Not enough money", C_BAD, 1);
    py += 54 * s;
    {   // the other ways in: a loan (a quarter down, the rest per flight) and a used one at 65%
      bool lic = career.license >= a.license && !commitBlocked();
      bool canFin = lic && !career.loan.open() && career.money >= career.downPayment(selHangar);
      if (button(px, py, 240 * s, 40 * s, fmt("Finance: %s down", fmtMoney(career.downPayment(selHangar)).c_str()), canFin, false)) {
        std::string m; bool ok = false; commit([&](Career& k) { ok = k.finance(selHangar, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 5;
      }
      g_ren.text(px + 250 * s, py + 2 * s, 13 * s, career.loan.open() ? "One loan at a time" : fmt("then %s per flight for %d flights (%.0f%% interest)", fmtMoney(career.loanPayment(selHangar)).c_str(), Career::kLoanTerm, career.loanRate() * 100.f), C_DIM, 1);
      bool canUsed = lic && career.money >= career.usedPrice(selHangar);
      if (button(px, py + 46 * s, 240 * s, 40 * s, fmt("Buy used: %s", fmtMoney(career.usedPrice(selHangar)).c_str()), canUsed, false)) {
        std::string m; bool ok = false; commit([&](Career& k) { ok = k.buyUsed(selHangar, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 5;
      }
      g_ren.text(px + 250 * s, py + 48 * s, 13 * s, "two thirds condition, half tanks: cheaper to buy, dearer to keep", C_DIM, 1);
      py += 46 * s;
    }
  } else {
    g_ren.text(px, py, 16 * s, fmt("Parked at %s", g_world.airports[career.fleet[oi].location].name), C_GOOD, 1);
    py += 30 * s;
    const int saleVal = career.saleValue(oi);   // (the same figure the sale pays: condition counts; a loan on it is settled from it)
    const bool saleLoan = career.loan.open() && career.loan.spec == career.fleet[oi].spec;
    if (saleLoan) g_ren.text(px + 250 * s, py + 24 * s, 13 * s, fmt("its loan's %s is repaid from it: %s to you", fmtMoney(career.loan.balance).c_str(), fmtMoney(saleVal - career.loan.balance).c_str()), C_DIM, 1);
    if (button(px, py, 240 * s, 46 * s, fmt("Sell for %s", fmtMoney(saleVal).c_str()), !commitBlocked(), !commitBlocked())) {
      std::string m; bool sold = false;
      commit([&](Career& k) { sold = k.sell(oi, &m); });
      if (sold) g_audio.trigger(SFX_CASH);
      hubMsg = m; hubMsgTime = 4;
    }
  }
  py += 70 * s;
  header(px, py, dw - 48 * s, "YOUR FLEET"); py += 26 * s;
  if (career.loan.open()) { g_ren.text(px, py, 14 * s, ellipsize(fmt("Loan on the %s: %s left, %s per flight%s", kAircraft[career.loan.spec].name, fmtMoney(career.loan.balance).c_str(), fmtMoney(career.loan.payment).c_str(), career.loan.missed ? fmt(", %d payment%s missed", career.loan.missed, career.loan.missed > 1 ? "s" : "").c_str() : ""), dw - 48 * s, 14 * s), career.loan.missed ? C_BAD : C_WARN, 1); py += 22 * s; }
  if (career.fleet.empty()) g_ren.text(px, py, 15 * s, "You don't own any aircraft yet. Rentals are available everywhere.", C_DIM, 1);
  for (size_t fi = 0; fi < career.fleet.size(); fi++) {
    const OwnedPlane& f = career.fleet[fi]; const AircraftSpec& fs = kAircraft[f.spec];
    g_ren.text(px, py, 15 * s, ellipsize(fmt("%s  -  at %s, %.0f%% fuel, condition %.0f%%%s", fs.name, g_world.airports[f.location].code, 100.f * f.fuel / std::max(fs.maxFuel, 1.f), 100.f * f.condition, career.routeOf((int)fi) >= 0 ? "  -  on an airline route" : ""), dw - 200 * s, 15 * s), career.routeOf((int)fi) >= 0 ? C_ACCENT : C_TEXT, 1);
    if (f.location == career.location && f.fuel < fs.maxFuel - 1.f) {
      int cost = (int)((fs.maxFuel - f.fuel) * career.fuelPrice(career.location, f.spec));
      if (button(px + dw - 48 * s - 150 * s, py - 4 * s, 150 * s, 26 * s, fmt("Fill up %s", fmtMoney(cost).c_str()), !commitBlocked() && career.money >= cost, false)) {
        std::string m; bool ok = false; commit([&](Career& k) { ok = k.refuel((int)fi, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 4;
      }
    }
    if (f.location == career.location && f.condition < 0.97f) {   // a service restores the condition (worn aircraft break more often)
      int cost = career.serviceCost((int)fi);
      if (button(px + dw - 48 * s - 310 * s, py - 4 * s, 150 * s, 26 * s, fmt("Service %s", fmtMoney(cost).c_str()), !commitBlocked() && career.money >= cost, false)) {
        std::string m; bool ok = false; commit([&](Career& k) { ok = k.service((int)fi, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 4;
      }
    }
    py += 26 * s;
  }
  if (!career.fleet.empty()) {   // hull insurance: a premium out of every settlement, and the repairs after a failure or a belly landing are covered
    py += 6 * s;
    if (button(px, py - 4 * s, 220 * s, 26 * s, career.insured ? "Insurance: ON" : "Insurance: OFF", !commitBlocked(), false)) {
      commit([](Career& k) { k.insured = !k.insured; }); hubMsg = career.insured ? "Hull insurance on: repairs after a failure are covered, a premium comes off each flight." : "Hull insurance off: you pay for repairs."; hubMsgTime = 4;
    }
    int prem = 0; for (auto& f : career.fleet) prem = std::max(prem, career.insurancePremium(f.spec));
    g_ren.text(px + 230 * s, py, 13 * s, fmt("premium up to %s per flight; covers repairs after a failure or a belly landing", fmtMoney(prem).c_str()), C_DIM, 1);
    py += 26 * s;
  }
}

// ------------------------------------------------------------------ the airline tab (C11)
void Game::drawHubAirline(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.5f, 620 * s);
  panel(x, y, lw, h); panel(x + lw + 16 * s, y, w - lw - 16 * s, h);
  float px = x + 20 * s, py = y + 16 * s, iw = lw - 40 * s;
  header(px, py, iw, "YOUR ROUTES"); py += 28 * s;
  if (!career.airlineOpen()) {
    for (auto& l : wrap("The airline opens with your Airline Transport licence (chapter 5 of the story). Then your own aircraft fly routes with hired pilots while you fly your own work: every flight you settle is a day of theirs.", iw, 15 * s)) { g_ren.text(px, py, 15 * s, l, C_DIM, 1); py += 21 * s; }
    return;
  }
  g_ren.text(px, py, 14 * s, fmt("Earned so far %s   -   %d incident%s   -   %d pilot%s on the payroll", fmtMoney(career.airline.earned).c_str(), career.airline.incidents, career.airline.incidents == 1 ? "" : "s", (int)career.airline.pilots.size(), career.airline.pilots.size() == 1 ? "" : "s"), C_DIM, 1); py += 26 * s;
  if (career.airline.routes.empty()) { g_ren.text(px, py, 15 * s, "No routes yet. Set one up on the right: an aircraft of yours, a destination, a pilot.", C_DIM, 1); py += 24 * s; }
  for (size_t ri = 0; ri < career.airline.routes.size(); ri++) {
    const Career::Route& r = career.airline.routes[ri];
    if (r.fleetIdx < 0 || r.fleetIdx >= (int)career.fleet.size()) continue;
    const AircraftSpec& sp = kAircraft[career.fleet[r.fleetIdx].spec];
    std::string pl = r.pilot >= 0 && r.pilot < (int)career.airline.pilots.size() ? career.airline.pilots[r.pilot].name : "-";
    g_ren.text(px, py, 15 * s, ellipsize(fmt("%s   %s > %s   %s", sp.name, g_world.airports[r.from].code, g_world.airports[r.to].code, pl.c_str()), iw - 160 * s, 15 * s), C_TEXT, 1);
    g_ren.text(px, py + 18 * s, 12.5f * s, fmt("%d flight%s, %s net, about %s a flight; condition %.0f%%", r.flights, r.flights == 1 ? "" : "s", fmtMoney(r.earned).c_str(), fmtMoney(career.routeRevenue(r) - career.routeFuelCost(r) - (r.pilot >= 0 ? career.airline.pilots[r.pilot].wage : 0)).c_str(), 100.f * career.fleet[r.fleetIdx].condition), C_DIM, 1);
    if (button(px + iw - 110 * s, py - 2 * s, 110 * s, 28 * s, "Recall", !commitBlocked(), false)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.recallRoute((int)ri, &m); }); hubMsg = m; hubMsgTime = 4; }
    py += 42 * s;
  }
  py += 8 * s;
  header(px, py, iw, "LOG"); py += 26 * s;
  if (career.airline.log.empty()) { g_ren.text(px, py, 13 * s, "Nothing to report.", C_DIM, 1); py += 18 * s; }
  for (int i = (int)career.airline.log.size() - 1; i >= 0 && py < y + h - 20 * s; i--) { g_ren.text(px, py, 13 * s, ellipsize(career.airline.log[i], iw, 13 * s), C_WARN, 1); py += 18 * s; }
  // ---- right: set up a route, the pilots
  float rx = x + lw + 36 * s, ry = y + 16 * s, rw = w - lw - 16 * s - 40 * s;
  header(rx, ry, rw, "SET UP A ROUTE"); ry += 30 * s;
  std::vector<int> freePlanes; for (size_t fi = 0; fi < career.fleet.size(); fi++) if (career.routeOf((int)fi) < 0) freePlanes.push_back((int)fi);
  if (freePlanes.empty()) { g_ren.text(rx, ry, 15 * s, "Every aircraft you own is on a route (or you own none). Buy another in the Hangar.", C_DIM, 1); ry += 24 * s; }
  else {
    airSelPlane = std::clamp(airSelPlane, 0, (int)freePlanes.size() - 1);
    int fi = freePlanes[airSelPlane]; const OwnedPlane& f = career.fleet[fi]; const AircraftSpec& sp = kAircraft[f.spec];
    auto pick = [&](const char* k, const std::string& v, int& idx, int n) {
      g_ren.text(rx, ry + 5 * s, 12 * s, k, C_DIM, 1, 0, false);
      if (button(rx + 110 * s, ry, 30 * s, 28 * s, "<")) idx = (idx + n - 1) % std::max(n, 1);
      g_ren.text(rx + 148 * s, ry + 6 * s, 15 * s, ellipsize(v, rw - 200 * s, 15 * s), C_TEXT, 1);
      if (button(rx + rw - 30 * s, ry, 30 * s, 28 * s, ">")) idx = (idx + 1) % std::max(n, 1);
      ry += 36 * s;
    };
    pick("AIRCRAFT", fmt("%s at %s", sp.name, g_world.airports[f.location].code), airSelPlane, (int)freePlanes.size());
    int na = (int)g_world.airports.size();
    airSelDest = (airSelDest % na + na) % na; if (airSelDest == f.location) airSelDest = (airSelDest + 1) % na;
    Contract c; c.from = f.location; c.to = airSelDest; c.type = CT_CARGO; std::string why; bool okRoute = career.canFly(c, f.spec, &why) != Career::SRC_NONE;
    pick("DESTINATION", fmt("%s %s%s", g_world.airports[airSelDest].code, g_world.airports[airSelDest].name, okRoute ? "" : "  -  no"), airSelDest, na);
    if (!okRoute) { g_ren.text(rx, ry, 12.5f * s, ellipsize(why, rw, 12.5f * s), C_BAD, 1); ry += 18 * s; }
    std::vector<int> freePilots; for (size_t pi = 0; pi < career.airline.pilots.size(); pi++) { bool busy = false; for (auto& r : career.airline.routes) if (r.pilot == (int)pi) busy = true; if (!busy) freePilots.push_back((int)pi); }
    if (freePilots.empty()) { g_ren.text(rx, ry + 5 * s, 12 * s, "PILOT", C_DIM, 1, 0, false); g_ren.text(rx + 148 * s, ry + 6 * s, 15 * s, "none free - hire one below", C_WARN, 1); ry += 36 * s; }
    else {
      airSelPilot = std::clamp(airSelPilot, 0, (int)freePilots.size() - 1);
      const Career::Pilot& p = career.airline.pilots[freePilots[airSelPilot]];
      pick("PILOT", fmt("%s  (rating %d, %s a flight)", p.name.c_str(), p.rating, fmtMoney(p.wage).c_str()), airSelPilot, (int)freePilots.size());
      Career::Route prev; prev.fleetIdx = fi; prev.from = f.location; prev.to = airSelDest; prev.pilot = freePilots[airSelPilot];
      g_ren.text(rx, ry, 13 * s, fmt("About %s a flight: fares %s, fuel %s, wage %s", fmtMoney(career.routeRevenue(prev) - career.routeFuelCost(prev) - p.wage).c_str(), fmtMoney(career.routeRevenue(prev)).c_str(), fmtMoney(career.routeFuelCost(prev)).c_str(), fmtMoney(p.wage).c_str()), C_DIM, 1); ry += 24 * s;
      if (button(rx, ry, 200 * s, 36 * s, "Assign route", okRoute && !commitBlocked(), okRoute)) { std::string m; bool ok = false; int pi = freePilots[airSelPilot]; commit([&](Career& k) { ok = k.assignRoute(fi, airSelDest, pi, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 5; }
      ry += 46 * s;
    }
  }
  ry += 10 * s;
  header(rx, ry, rw, "PILOTS"); ry += 28 * s;
  for (size_t pi = 0; pi < career.airline.pilots.size(); pi++) {
    const Career::Pilot& p = career.airline.pilots[pi];
    bool busy = false; for (auto& r : career.airline.routes) if (r.pilot == (int)pi) busy = true;
    g_ren.text(rx, ry + 4 * s, 14 * s, fmt("%s   rating %d   %s a flight%s", p.name.c_str(), p.rating, fmtMoney(p.wage).c_str(), busy ? "   (on a route)" : ""), C_TEXT, 1);
    if (!busy && button(rx + rw - 90 * s, ry, 90 * s, 26 * s, "Let go", !commitBlocked(), false)) { std::string m; commit([&](Career& k) { k.firePilot((int)pi, &m); }); hubMsg = m; hubMsgTime = 4; }
    ry += 30 * s;
  }
  g_ren.text(rx, ry + 4 * s, 12 * s, "FOR HIRE", C_DIM, 1, 0, false); ry += 22 * s;
  auto cands = career.pilotCandidates();
  for (size_t ci = 0; ci < cands.size() && ry < y + h - 40 * s; ci++) {
    const Career::Pilot& p = cands[ci];
    g_ren.text(rx, ry + 4 * s, 14 * s, fmt("%s   rating %d   %s a flight", p.name.c_str(), p.rating, fmtMoney(p.wage).c_str()), C_DIM, 1);
    if (button(rx + rw - 90 * s, ry, 90 * s, 26 * s, "Hire", !commitBlocked() && career.airline.pilots.size() < 6, false)) { std::string m; Career::Pilot cp = p; commit([&](Career& k) { k.hirePilot(cp, &m); }); hubMsg = m; hubMsgTime = 4; }
    ry += 30 * s;
  }
  g_ren.text(rx, std::min(ry + 6 * s, y + h - 24 * s), 12 * s, "Rating 3 fills the cabin and rarely bends anything; rating 1 is cheap and has incidents. Insurance covers their repairs too.", C_DIM, 1);
}

void Game::drawHubLogbook(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.4f, 500 * s);
  panel(x, y, lw, h);
  float px = x + 24 * s, py = y + 20 * s;
  g_ren.text(px, py, 26 * s, "Pilot Logbook", C_TEXT, 1); py += 40 * s;
  header(px, py, lw - 48 * s, "RECORD"); py += 26 * s;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py + 3 * s, 11.5f * s, upperS(k), C_DIM, 0.95f, 0, false); g_ren.text(px + 220 * s, py, 16 * s, ellipsize(v, lw - 268 * s, 16 * s), C_TEXT, 1); py += 27 * s; };
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
  g_ren.text(x, py, 24 * s, "Settings", C_TEXT, 1);
  {  // page tabs: general settings / controls, with a sliding underline
    const char* pages[] = {"GENERAL", "CONTROLS"};
    float tx = x + 150 * s, tw = 140 * s;
    for (int i = 0; i < 2; i++) {
      bool hov = hovered(tx + i * (tw + 8 * s), py - 2 * s, tw, 32 * s);
      float hk = anim(uid(tx + i * tw, py, "stab"), hov ? 1.f : 0.f, 14);
      if (hk > 0.01f) g_ren.rectGrad(tx + i * (tw + 8 * s), py - 2 * s, tw, 32 * s, vec3(0.05f, 0.16f, 0.24f), vec3(0.02f, 0.06f, 0.1f), 0.8f * hk, 3 * s);
      g_ren.text(tx + i * (tw + 8 * s) + tw * 0.5f, py + 6 * s, 15 * s, pages[i], settingsPage == i ? C_TEXT : mixc(C_DIM, C_TEXT, hk), 1, 1, false);
      if (hov && in.mPressed[0] && settingsPage != i) { settingsPage = i; bindCapture = -1; g_audio.trigger(SFX_CLICK); }
    }
    float ux = anim(0x5e77u, tx + settingsPage * (tw + 8 * s), 14);
    g_ren.glow(ux + 10 * s, py + 28 * s, tw - 20 * s, 2.5f * s, C_ACCENT, 0.5f, 1.2f * s, 7 * s);
    g_ren.rect(ux + 10 * s, py + 28 * s, tw - 20 * s, 2.5f * s, C_ACCENT, 1);
  }
  py += 44 * s;
  if (settingsPage == 1) { drawControls(x, py, w, y + h - py); return; }
  header(x, py, std::min(w, 620 * S()), "DISPLAY / AUDIO / CONTROLS"); py += 28 * s;
  // the controls scroll in a clipped region of their own (mouse wheel, the right stick; keyboard or D-pad focus scrolls
  // its control into view): every one stays reachable at any resolution and UI scale (the review of v3.31.0, U3: five
  // rows sat below a 1080p screen). A control outside the region takes no clicks.
  const float rs = 42 * s, regTop = py, regBot = y + h - 24 * s;
  static float genScroll = 0.f, genMax = 0.f;
  if (hovered(x - 8 * s, regTop, w + 16 * s, regBot - regTop) && in.wheel != 0) { genScroll -= in.wheel * 48.f * s; in.wheel = 0; }
  genScroll = std::clamp(genScroll, 0.f, genMax);
  const size_t focus0 = focusList.size();
  g_ren.uiClip(x - 8 * s, regTop, x + w + 8 * s, regBot);
  hitClipOn = true; hitClip[0] = x - 8 * s; hitClip[1] = regTop; hitClip[2] = x + w + 8 * s; hitClip[3] = regBot;
  py -= genScroll;
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
    py += rs;
  };
  auto toggle = [&](const std::string& label, bool& v, const char* on, const char* off) {
    g_ren.text(x, py + 6 * s, 16 * s, label, C_DIM, 1);
    if (button(x + 250 * s, py, 200 * s, 32 * s, v ? on : off, true, v)) v = !v;
    py += rs;
  };
  g_ren.text(x, py + 6 * s, 16 * s, "Render resolution", C_DIM, 1);
  {
    const char* rm[] = {"Native", "Auto", "85%", "75%", "67%"};
    for (int i = 0; i < 5; i++) if (button(x + 250 * s + i * 78 * s, py, 72 * s, 32 * s, rm[i], true, set.resMode == i)) set.resMode = i;
    py += 36 * s;
    g_ren.text(x + 250 * s, py, 12.5f * s, fmt("rendered at %dx%d (%.0f%%), upscaled to %dx%d by the temporal AA", (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale),
               g_ren.renderScale * 100.f, g_ren.W, g_ren.H), C_DIM, 0.85f, 0, false);
    py += 24 * s;
  }
  g_ren.text(x, py + 6 * s, 16 * s, "Frame rate", C_DIM, 1);
  {
    static const int fr[] = {0, 30, 60, 90, 120, 144, 240};
    for (int i = 0; i < 7; i++) {
      std::string l = fr[i] ? fmt("%d", fr[i]) : std::string("Display");
      if (button(x + 250 * s + i * 62 * s, py, 58 * s, 32 * s, l, true, set.fpsTarget == fr[i])) { set.fpsTarget = fr[i]; wantPacing = true; }
    }
    py += 36 * s;
    g_ren.text(x + 250 * s, py, 12.5f * s, fmt("%s: %.0f fps now, GPU %s%s", set.fpsTarget ? fmt("capped at %d", set.fpsTarget).c_str() : fmt("the display's %d Hz, on vsync", monitorHz).c_str(),
               1.f / std::max(fpsAvg, 1e-4f), g_ren.gpuMs > 0 ? fmt("%.1f ms", g_ren.gpuMs).c_str() : "n/a", set.resMode == 1 ? " (Auto resolution holds this rate)" : ""), C_DIM, 0.85f, 0, false);
    py += 24 * s;
  }
  g_ren.text(x, py + 6 * s, 16 * s, "Render quality", C_DIM, 1);
  const char* q[] = {"Low", "Medium", "High"};
  for (int i = 0; i < 3; i++) if (button(x + 250 * s + i * 100 * s, py, 92 * s, 32 * s, q[i], true, set.quality == i)) { set.quality = i; g_ren.quality = i; }
  py += rs;
  slider("Master volume", set.master, 0, 1, 0.05f, fmt("%.0f%%", set.master * 100));
  slider("Engine volume", set.engineVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.engineVol * 100));
  slider("Effects volume", set.sfxVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.sfxVol * 100));
  float rv = set.radioVol;
  slider("Radio volume", set.radioVol, 0, 1, 0.05f, fmt("%.0f%%", set.radioVol * 100));
  if (rv != set.radioVol) radio.setVolume(set.radioVol);
  slider("Voice volume", set.atcVol, 0, 1, 0.05f, fmt("%.0f%%", set.atcVol * 100));
  slider("Mouse sensitivity", set.mouseSens, 0.2f, 3.f, 0.1f, fmt("%.1f", set.mouseSens));
  slider("Field of view", set.fov, 45.f, 75.f, 1.f, fmt("%.0f deg outside, %.0f in the cockpit", set.fov, set.fov + 19.f));
  slider("UI scale", set.uiScale, 0.8f, 1.4f, 0.05f, fmt("%.0f%%", set.uiScale * 100));
  toggle("Head-look", set.headLook, "Leans into turns", "Fixed ahead");
  { bool cb = set.cbHud; toggle("HUD palette", set.cbHud, "Blue / orange (colour-blind)", "Green / red"); if (cb != set.cbHud) applyPalette(set.cbHud); }
  g_ren.text(x + 250 * s, py - 2 * s, 12.5f * s, "The HUD key remembers on / off for each camera view.", C_DIM, 0.85f, 0, false); py += 20 * s;
  toggle("Pitch control", set.invertPitch, "Inverted", "Normal (S = nose up)");
  toggle("Units", set.metric, "Metric", "Aviation (kt / ft)");
  toggle("Instructor hints", set.showHints, "Shown", "Hidden");
  toggle("Air traffic", set.traffic, "On", "Off");
  bool fs = set.fullscreen;
  toggle("Display", set.fullscreen, "Fullscreen", "Windowed");
  if (fs != set.fullscreen) wantFullscreenToggle = true;
  py += 6 * s;
  g_ren.text(x, py, 14 * s, ellipsize("Settings are saved automatically. To add radio stations, edit " + (saveDir.empty() ? std::string("radio_stations.txt in the game folder") : saveDir + "/radio_stations.txt"), w, 14 * s), C_DIM, 0.8f);
  py += 22 * s;
  genMax = std::max(0.f, py + genScroll - regBot);
  hitClipOn = false;
  g_ren.uiClipOff();
  if (genMax > 0.f) {   // where the list stands: a thin track at the right edge
    const float th = regBot - regTop, bh = std::max(24 * s, th * th / (th + genMax)), by = regTop + (th - bh) * (genScroll / genMax);
    g_ren.rect(x + w + 2 * s, regTop, 3 * s, th, C_ACCENT, 0.12f, 1.5f * s);
    g_ren.rect(x + w + 2 * s, by, 3 * s, bh, C_ACCENT, 0.7f, 1.5f * s);
  }
  // the control with keyboard / D-pad focus scrolled into view (next frame draws it there)
  if (focusNav)
    for (size_t k = focus0; k < focusList.size(); k++)
      if (focusList[k].id == focusId) {
        const float fy = focusList[k].y, fh = focusList[k].h;
        if (fy < regTop) genScroll -= regTop - fy + 8 * s;
        else if (fy + fh > regBot) genScroll += fy + fh - regBot + 8 * s;
      }
  saveSettings();
  (void)w;
}

// Controls page: every rebindable action with its keyboard key and gamepad button. Click a cell and press the new
// key / button (Esc or Menu cancels, right-click clears); a key already used in the same group swaps over.
void Game::drawControls(float x, float y, float w, float h) {
  float s = S(), rowH = 32 * s;
  float nameW = std::min(330 * s, w * 0.42f), cellW = std::min(170 * s, (w - nameW - 40 * s) * 0.5f);
  float kx = x + nameW, px = kx + cellW + 12 * s;
  // column titles with device glyphs
  g_ren.text(x, y, 12 * s, "ACTION", C_DIM, 1, 0, false);
  g_ren.rect(kx + 2 * s, y + 1 * s, 16 * s, 10 * s, C_ACCENT, 0.0f); g_ren.rectOutline(kx, y, 18 * s, 12 * s, C_ACCENT, 0.7f, 2 * s, 1 * s);
  for (int i = 0; i < 4; i++) g_ren.rect(kx + 3 * s + i * 3.5f * s, y + 3 * s, 2 * s, 2 * s, C_ACCENT, 0.8f);
  g_ren.rect(kx + 5 * s, y + 7.5f * s, 8 * s, 1.5f * s, C_ACCENT, 0.8f);
  g_ren.text(kx + 26 * s, y, 12 * s, "KEYBOARD", C_ACCENT, 1, 0, false);
  g_ren.rect(px, y + 1 * s, 20 * s, 11 * s, C_ACCENT, 0.0f); g_ren.rectOutline(px, y + 1 * s, 20 * s, 11 * s, C_ACCENT, 0.7f, 5 * s, 1 * s);
  g_ren.rect(px + 4 * s, y + 5.5f * s, 5 * s, 1.5f * s, C_ACCENT, 0.8f); g_ren.rect(px + 5.75f * s, y + 3.75f * s, 1.5f * s, 5 * s, C_ACCENT, 0.8f);
  g_ren.rect(px + 13 * s, y + 4 * s, 2.5f * s, 2.5f * s, C_ACCENT, 0.8f, 1.25f * s); g_ren.rect(px + 15.5f * s, y + 6.5f * s, 2.5f * s, 2.5f * s, C_ACCENT, 0.8f, 1.25f * s);
  g_ren.text(px + 28 * s, y, 12 * s, in.pad ? "CONTROLLER  (CONNECTED)" : "CONTROLLER", in.pad ? C_GOOD : C_ACCENT, 1, 0, false);
  y += 22 * s;
  // flattened list: group headers + action rows, scrolled a row at a time
  struct Row { int group, act; };
  std::vector<Row> rows;
  for (int g = 0; g < 4; g++) { rows.push_back({g, -1}); for (int a = 0; a < ACT_COUNT; a++) if (kActions[a].group == g) rows.push_back({g, a}); }
  float footH = 92 * s;
  int visible = std::max(4, (int)((h - footH) / rowH));
  int maxScroll = std::max(0, (int)rows.size() - visible);
  if (hovered(x, y, w, visible * rowH) && in.wheel != 0) { ctlScroll -= (int)in.wheel * 2; in.wheel = 0; }
  if (in.pad && bindCapture < 0 && fabsf(in.ry) > 0.5f) { ctlScrollAcc += in.ry * uiDt * 12.f; }
  while (ctlScrollAcc > 1.f) { ctlScroll--; ctlScrollAcc -= 1.f; }
  while (ctlScrollAcc < -1.f) { ctlScroll++; ctlScrollAcc += 1.f; }
  ctlScroll = std::clamp(ctlScroll, 0, maxScroll);
  float sy = anim(0xc7151u, (float)ctlScroll, 18);
  float listTop = y;
  for (int i = 0; i < (int)rows.size(); i++) {
    float ry = listTop + (i - sy) * rowH;
    if (ry < listTop - 0.5f * s || ry > listTop + (visible - 1) * rowH + 0.5f * s) continue;
    const Row& r = rows[i];
    if (r.act < 0) { header(x, ry + 10 * s, w - 30 * s, kActionGroups[r.group]); continue; }
    const ActionInfo& ai = kActions[r.act];
    bool rowHov = hovered(x, ry, w - 30 * s, rowH - 4 * s);
    float rh = anim(uid(x, (float)r.act, "crow"), rowHov ? 1.f : 0.f, 14);
    if (rh > 0.01f) g_ren.rectGrad(x - 6 * s, ry, w - 24 * s, rowH - 4 * s, vec3(0.04f, 0.12f, 0.18f), vec3(0.02f, 0.06f, 0.1f), 0.6f * rh, 3 * s);
    g_ren.text(x + 6 * s, ry + 6 * s, 15 * s, ai.name, mixc(C_DIM, C_TEXT, 0.6f + 0.4f * rh), 1);
    for (int dev = 0; dev < 2; dev++) {
      float cx = dev ? px : kx, cy = ry + 2 * s, cw = cellW, ch = rowH - 8 * s;
      bool cap = bindCapture == r.act && bindCaptureDev == dev;
      bool hov = hovered(cx, cy, cw, ch);
      int k = set.keyBind[r.act]; unsigned b = set.padBind[r.act];
      bool clash = false;
      for (int o = 0; o < ACT_COUNT; o++)
        if (o != r.act && kActions[o].group == ai.group && (dev ? (b && set.padBind[o] == b) : (k && set.keyBind[o] == k))) clash = true;
      bool isDef = dev ? b == ai.pad : k == ai.key;
      float hk = anim(uid(cx, (float)r.act, "cell"), hov || cap ? 1.f : 0.f, 16);
      vec3 ac = clash ? C_WARN : C_ACCENT;
      if (cap) {
        float pulse = 0.5f + 0.5f * sinf(realTime * 7.f);
        g_ren.glow(cx, cy, cw, ch, C_ACCENT, 0.25f + 0.25f * pulse, 3 * s, 10 * s);
        g_ren.rectGrad(cx, cy, cw, ch, C_ACCENT * 0.5f, C_ACCENT * 0.25f, 0.95f, 3 * s);
        g_ren.text(cx + cw * 0.5f, cy + ch * 0.5f - 6.5f * s, 12 * s, dev ? "PRESS A BUTTON" : "PRESS A KEY", C_TEXT, 0.7f + 0.3f * pulse, 1, false);
        float bw = cw * clampf(1.f - bindCaptureT / 8.f, 0, 1);
        g_ren.rect(cx, cy + ch - 2 * s, bw, 2 * s, C_TEXT, 0.8f);
      } else {
        g_ren.rectGrad(cx, cy, cw, ch, mixc(C_BTN * 1.3f, C_BTN_HI * 1.3f, hk), mixc(C_BTN, C_BTN_HI, hk), 0.9f, 3 * s);
        g_ren.rectOutline(cx, cy, cw, ch, ac, 0.22f + 0.5f * hk + (clash ? 0.3f : 0.f), 3 * s, 1 * s);
        std::string lbl = dev ? padName(b) : keyName(k);
        bool none = dev ? !b : !k;
        // keycap / button chip
        float tw = g_ren.textWidth(lbl, 13 * s) + 16 * s;
        float chx = cx + cw * 0.5f - tw * 0.5f;
        if (!none) {
          g_ren.rect(chx, cy + 3 * s, tw, ch - 6 * s, ac, 0.12f + 0.1f * hk, dev ? (ch - 6 * s) * 0.5f : 2.5f * s);
          g_ren.rect(chx, cy + ch - 4 * s, tw, 1 * s, ac, 0.5f);
        }
        g_ren.text(cx + cw * 0.5f, cy + ch * 0.5f - 7 * s, 13 * s, lbl, none ? C_DIM * 0.6f : clash ? C_WARN : C_TEXT, 1, 1, false);
        if (!isDef) g_ren.rect(cx + cw - 7 * s, cy + 4 * s, 3.5f * s, 3.5f * s, C_ACCENT, 0.9f, 1.75f * s);   // customised marker
        if (hov && in.mPressed[0]) { bindCapture = r.act; bindCaptureDev = dev; bindCaptureT = 0; g_audio.trigger(SFX_CLICK); }
        if (hov && in.mPressed[1]) { if (dev) set.padBind[r.act] = 0; else set.keyBind[r.act] = 0; saveSettings(); g_audio.trigger(SFX_CLICK); }
      }
    }
  }
  if (maxScroll > 0) {  // scrollbar
    float sx = x + w - 14 * s, th = visible * rowH - 8 * s, kh = std::max(24 * s, th * visible / rows.size());
    g_ren.rect(sx + 2 * s, listTop, 3 * s, th, C_ACCENT, 0.15f, 1.5f * s);
    g_ren.rect(sx, listTop + (th - kh) * sy / maxScroll, 7 * s, kh, C_ACCENT, 0.8f, 3.5f * s);
  }
  float fy = listTop + visible * rowH + 6 * s;
  g_ren.rect(x, fy, w - 30 * s, 1 * s, C_ACCENT, 0.2f);
  g_ren.text(x, fy + 10 * s, 12.5f * s, "FIXED   Arrows pitch/roll   PgUp/PgDn throttle   1-9, 0 set throttle   Right-drag look   Esc pause   F11 fullscreen", C_DIM, 0.85f, 0, false);
  g_ren.text(x, fy + 28 * s, 12.5f * s, "              Left stick pitch/roll   RT/LT throttle   Right stick look   Menu pause", C_DIM, 0.85f, 0, false);
  g_ren.text(x, fy + 52 * s, 12.5f * s, bindCapture >= 0 ? "Esc / Menu cancels" : "Click a cell to rebind  //  right-click to clear", C_ACCENT, 0.9f, 0, false);
  if (button(x + w - 30 * s - 190 * s, fy + 46 * s, 190 * s, 32 * s, "Reset to defaults")) { set.resetBindings(); bindCapture = -1; saveSettings(); }
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
  g_ren.glow(x, y, sz, sz, C_ACCENT, 0.18f, r, 8 * s);
  hudRing(cx, cy, r, 1.5f * s, C_ACCENT, 0.55f, 48);
  // slip / skid: a ball under the bank scale that slides with the sideslip
  float slip = clampf(plane.beta / DEG / 10.f, -1.f, 1.f);
  g_ren.rect(cx - 16 * s, y + sz - 14 * s, 32 * s, 8 * s, vec3(0, 0.02f, 0.04f), 0.8f, 4 * s);
  g_ren.rect(cx + slip * 12 * s - 4 * s, y + sz - 14 * s, 8 * s, 8 * s, fabsf(slip) > 0.6f ? C_WARN : vec3(1, 1, 1), 1, 4 * s);
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
  (void)rr;
  hudDart(c.x + f.x * a, c.y + f.y * a, f.x, f.y, a * 1.9f, a * 0.8f, vec3(1, 1, 0.2f), 1.f);
  g_ren.text(x + sz * 0.5f, y + 4 * s, 12 * s, "N", vec3(1, 1, 1), 0.9f, 1);
}

// ------------------------------------------------------------------ the in-flight HUD
// The flight UI lives on the perimeter: a slim band across the top (mission, heading tape, clock and autopilot), a
// speed tape down the left edge and an altitude tape down the right, the attitude indicator in the bottom-left
// corner with the approach needles, the systems tiles in the bottom-right, the tower's last call inboard of the
// speed tape and the messages (events, failures, toasts) inboard of the altitude tape. The middle of the screen
// stays clear for flying; the only things that enter it are the target marker, which is part of the world, and the
// warning icons, which sit just under the top band. A stall or a terrain warning also lights the screen's edges.

// a slim glass strip: hairline edges, a bright lead at one end, small corner ticks
void Game::hudStrip(float x, float y, float w, float h, float a) {
  float s = S();
  g_ren.rectGrad(x, y, w, h, vec3(0.02f, 0.06f, 0.1f), vec3(0.0f, 0.02f, 0.04f), 0.52f * a, 2 * s);
  g_ren.rect(x, y, w, 1.f * s, C_ACCENT, 0.2f * a);
  g_ren.rect(x, y + h - 1.f * s, w, 1.f * s, C_ACCENT, 0.12f * a);
  brackets(x, y, w, h, 8 * s, 1.5f * s, C_ACCENT, 0.6f * a);
}

// the warning icons, drawn with lines so they scale with the UI: a wing with the flow coming off its back, the ground
// with chevrons climbing away from it, a wheel on its leg, a stopped fan
void Game::hudWarnIcon(int kind, float cx, float cy, float sz, vec3 c, float a) {
  float s = S(), u = sz / 64.f, th = 2.6f * s;
  if (kind == 0) {   // STALL: airfoil, nose left, nose-high; three separated streamlines curling off the upper surface
    float pts[9][2] = {{-28, 6}, {-22, -4}, {-10, -10}, {4, -10}, {18, -6}, {30, 0}, {18, 3}, {0, 6}, {-16, 8}};
    for (int i = 0; i < 9; i++) { int j = (i + 1) % 9; g_ren.line(cx + pts[i][0] * u, cy + pts[i][1] * u, cx + pts[j][0] * u, cy + pts[j][1] * u, th, c, a); }
    for (int k = 0; k < 3; k++) {
      float x0 = cx + (-8 + k * 12) * u, y0 = cy + (-13 - k * 2) * u, ph = realTime * 9.f + k * 1.7f;
      for (int i = 0; i < 5; i++) {
        float t0 = i / 5.f, t1 = (i + 1) / 5.f;
        g_ren.line(x0 + t0 * 14 * u, y0 - t0 * 10 * u + sinf(ph + t0 * 6.f) * 3 * u, x0 + t1 * 14 * u, y0 - t1 * 10 * u + sinf(ph + t1 * 6.f) * 3 * u, th * 0.8f, c, a * 0.9f);
      }
    }
    g_ren.line(cx - 30 * u, cy + 20 * u, cx + 30 * u, cy + 20 * u, 1.5f * s, c, a * 0.5f);   // the relative wind, from the left
    g_ren.line(cx - 30 * u, cy + 20 * u, cx - 24 * u, cy + 16 * u, 1.5f * s, c, a * 0.5f);
  } else if (kind == 1) {   // PULL UP: rising ground, two chevrons climbing out of it
    for (int i = 0; i < 6; i++) { float w = (6 + i * 9) * u; g_ren.rect(cx - w * 0.5f, cy + (10 + i * 3.2f) * u, w, 3.4f * u, c, a * (0.55f + 0.08f * i)); }
    float bob = sinf(realTime * 8.f) * 2 * u;
    hudChevronUp(cx, cy - 2 * u + bob, 16 * u, 9 * u, th, c, a);
    hudChevronUp(cx, cy - 16 * u + bob, 16 * u, 9 * u, th, c, a * 0.75f);
  } else if (kind == 2) {   // GEAR: a wheel on its leg, the tyre as a ring with a hub
    g_ren.line(cx, cy - 26 * u, cx, cy + 2 * u, th, c, a); g_ren.line(cx - 10 * u, cy - 26 * u, cx + 10 * u, cy - 26 * u, th, c, a);
    g_ren.line(cx, cy - 14 * u, cx - 10 * u, cy - 4 * u, th * 0.8f, c, a * 0.8f);   // the drag brace
    hudRing(cx, cy + 12 * u, 13 * u, th, c, a, 28); hudRing(cx, cy + 12 * u, 4 * u, th, c, a, 16);
    g_ren.rect(cx - 30 * u, cy + 27 * u, 60 * u, 2 * u, c, a * 0.5f);
  } else {   // ENGINE: a three-blade fan in a ring, a bar across it
    hudRing(cx, cy, 24 * u, th * 0.9f, c, a, 36);
    for (int k = 0; k < 3; k++) { float an = k * 2.0944f + 0.4f; g_ren.line(cx + cosf(an) * 5 * u, cy + sinf(an) * 5 * u, cx + cosf(an) * 19 * u, cy + sinf(an) * 19 * u, th * 1.6f, c, a); }
    hudRing(cx, cy, 5 * u, th, c, a, 16);
    g_ren.line(cx - 22 * u, cy + 22 * u, cx + 22 * u, cy - 22 * u, th * 1.3f, c, a);
  }
}

void Game::drawHud(const FrameParams& fp) {
  if (!plane.spec) return;
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  if (crashed && crashTimer > 0.8f && crashTimer < crashEndT - 1.f) {   // the crash sequence can be skipped
    float a = 0.55f + 0.35f * sinf(realTime * 3.f);
    g_ren.text(W * 0.5f, H - 46 * s, 17 * s, in.pad ? "PRESS  A  TO SKIP" : "PRESS  ENTER  TO SKIP", C_TEXT, a, 1);
  }
  if (!hudOn) return;
  { auto it = uiAnim.find(0x6e61u); if (showMap && it != uiAnim.end() && it->second > 0.6f) return; }  // GPS map covers the HUD
  const AircraftSpec& spc = *plane.spec;
  const bool cockpit = camMode == 1;
  const float band = kHudBand * s, rail = kHudRail * s;
  // ---- navigation values
  const Airport& d = dest();
  bool toWp = wpIndex < (int)contract.wps.size();
  vec3 target = toWp ? vec3(contract.wps[wpIndex].x, contract.wps[wpIndex].alt, contract.wps[wpIndex].z) : d.pos();
  vec3 to = target - plane.pos;
  float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  vec3 mag(1.f, 0.35f, 1.f);
  const vec3 APC(0.3f, 0.95f, 1.f);
  float hdg = plane.heading();
  // ================================================================ the top band
  {
    hudStrip(0, 0, W, band, 1.f);
    // left: what this flight is, and the objective
    const char* kind = researchFlight ? "RESEARCH" : contract.type == CT_LESSON ? "LESSON" : contract.type == CT_TRIAL ? "TRIAL" : contract.type == CT_FERRY ? "FREE FLIGHT" : "CONTRACT";
    vec3 kc = researchFlight ? vec3(0.75f, 0.45f, 1.f) : contract.type == CT_LESSON ? C_GOOD : C_ACCENT;
    g_ren.rect(0, 0, 3 * s, band, kc, 0.95f);
    float kw = g_ren.text(14 * s, 6 * s, 9.5f * s, kind, kc, 1, 0, false);
    float leftW = W * 0.5f - 230 * s - 14 * s;
    g_ren.text(14 * s + kw + 10 * s, 5 * s, 13 * s, ellipsize(contract.title, leftW - kw - 10 * s, 13 * s), C_TEXT, 1, 0, false);
    std::string obj = toWp ? fmt("CHECKPOINT %d / %d", wpIndex + 1, (int)contract.wps.size()) : fmt("LAND  %s  %s", d.code, d.name);
    if (researchFlight) obj = resCard >= 0 ? (resCardDone ? std::string(kResCards[resCard].id) + "  CARD COMPLETE" : fmt("%s  STEP %d/%d  %s", kResCards[resCard].id, resStep + 1, kResCards[resCard].n, kResCards[resCard].steps[std::min(resStep, kResCards[resCard].n - 1)].label)) : fmt("FREE ROAM  -  MACH %.2f", plane.mach);
    g_ren.text(14 * s, 24 * s, 12 * s, ellipsize(obj, leftW, 12 * s), mag, 1, 0, false);
    {   // the nav line under it: distance and time to the target
      std::string nav = fmt("%s   BRG %03.0f   ETE %s", dist < 1000.f ? fmt("%.0f m", dist).c_str() : set.metric ? fmt("%.1f km", dist / 1000.f).c_str() : fmt("%.1f nm", dist / 1852.f).c_str(), brg, gs > 10 ? fmt("%d:%02d", (int)(dist / gs) / 60, (int)(dist / gs) % 60).c_str() : "--:--");
      g_ren.text(14 * s, 40 * s, 10.5f * s, ellipsize(nav, leftW, 10.5f * s), C_DIM, 1, 0, false);
    }
    // centre: heading tape with the bearing caret, the autopilot bug and the turn cue
    float tw = 440 * s, tx = W * 0.5f - tw * 0.5f, ty = 4 * s, th = band - 8 * s;
    g_ren.rect(tx, ty, tw, th, vec3(0.0f, 0.01f, 0.02f), 0.45f, 3 * s);
    g_ren.rect(tx, ty, 1 * s, th, C_ACCENT, 0.3f); g_ren.rect(tx + tw - 1 * s, ty, 1 * s, th, C_ACCENT, 0.3f);
    for (int a = -60; a <= 60; a += 5) {
      float hv = floorf(hdg / 5.f) * 5.f + a, off = wrapAngle((hv - hdg) * DEG) / DEG;
      if (fabsf(off) > 60.f) continue;
      float px2 = W * 0.5f + off / 60.f * tw * 0.5f;
      int hvi = ((int)roundf(hv) % 360 + 360) % 360;
      bool major = hvi % 30 == 0;
      float tl = (major ? 10 : (hvi % 10 == 0 ? 7 : 4)) * s;
      g_ren.rect(px2 - 0.5f * s, ty + th - tl, 1 * s, tl, C_TEXT, 0.7f * (1.f - fabsf(off) / 75.f));
      if (major) {
        const char* cards[] = {"N", "", "", "E", "", "", "S", "", "", "W", "", ""};
        std::string lab = cards[hvi / 30][0] ? cards[hvi / 30] : fmt("%02d", hvi / 10);
        g_ren.text(px2, ty + 4 * s, 11 * s, lab, hvi % 90 == 0 ? C_ACCENT : C_TEXT, 0.9f * (1.f - fabsf(off) / 80.f), 1, false);
      }
    }
    {   // the heading box on the lubber line
      std::string hs = fmt("%03.0f", hdg); float bw = g_ren.textWidth(hs, 15 * s) + 14 * s;
      g_ren.rect(W * 0.5f - bw * 0.5f, ty + th * 0.5f - 7 * s, bw, 20 * s, vec3(0.0f, 0.02f, 0.04f), 0.9f, 2 * s);
      g_ren.rectOutline(W * 0.5f - bw * 0.5f, ty + th * 0.5f - 7 * s, bw, 20 * s, C_ACCENT, 0.6f, 2 * s, 1 * s);
      g_ren.text(W * 0.5f, ty + th * 0.5f - 5 * s, 15 * s, hs, C_TEXT, 1, 1, false);
      g_ren.line(W * 0.5f, ty + th - 1 * s, W * 0.5f, ty + th + 5 * s, 2 * s, C_TEXT, 1);
    }
    float rel = wrapAngle((brg - hdg) * DEG) / DEG;
    float cxp = W * 0.5f + clampf(rel, -60.f, 60.f) / 60.f * tw * 0.5f;
    if (fabsf(rel) <= 60.f) hudDart(cxp, ty + th - 2 * s, 0.f, 1.f, 11 * s, 6 * s, mag, 1.f);   // the bearing: a dart pointing down onto the tape
    else { float k = rel > 0 ? 1.f : -1.f, ex2 = W * 0.5f + k * (tw * 0.5f - 6 * s), yy = ty + th * 0.5f; hudDart(ex2, yy, k, 0.f, 12 * s, 6 * s, mag, 1.f); hudDart(ex2 - k * 9 * s, yy, k, 0.f, 12 * s, 6 * s, mag, 0.6f); }
    if (fabsf(rel) > 8.f && !plane.apOn) g_ren.text(W * 0.5f + (rel > 0 ? 1 : -1) * (tw * 0.5f + 10 * s), ty + th * 0.5f - 6 * s, 12 * s, fmt("TURN %s %.0f", rel > 0 ? "R" : "L", fabsf(rel)), mag, 1, rel > 0 ? 0 : 2, false);
    if (plane.apOn) {
      float relA = wrapAngle((plane.apHeading - hdg) * DEG) / DEG;
      if (fabsf(relA) <= 60.f) { float bx = W * 0.5f + relA / 60.f * tw * 0.5f; g_ren.rect(bx - 6 * s, ty + th - 5 * s, 12 * s, 5 * s, APC, 0.95f); g_ren.rect(bx - 2 * s, ty + th - 9 * s, 4 * s, 4 * s, APC, 0.95f); }
    }
    // right: the clock, the deadline / time acceleration, the autopilot
    float rx = W - 14 * s;
    int hh = ((int)timeOfDay) % 24, mm = (int)(fmodf(timeOfDay, 1.f) * 60);
    g_ren.text(rx, 8 * s, 10.5f * s, "LOCAL", C_DIM, 1, 2, false);
    g_ren.text(rx, 20 * s, 17 * s, fmt("%02d:%02d", hh, mm), C_TEXT, 1, 2, false);
    std::string extra; vec3 ec = C_WARN;
    if (contract.timeLimitMin > 0) { float left = contract.timeLimitMin * 60 - jobClockBase - flightClock; extra = left > 0 ? fmt("DEADLINE %d:%02d", (int)left / 60, (int)left % 60) : "LATE"; ec = left > 120 ? C_WARN : C_BAD; }
    if (timeAccel > 1) extra += fmt("%sTIME x%.0f", extra.empty() ? "" : "   ", timeAccel);
    if (researchFlight && resCard >= 0 && !resCardDone) { extra = resStepText(kResCards[resCard].steps[std::min(resStep, kResCards[resCard].n - 1)]) + (resHold > 0 ? fmt("  (%.0f s)", resHold) : ""); ec = C_ACCENT; }
    if (!extra.empty()) g_ren.text(rx, 40 * s, 11.5f * s, extra, ec, 1, 2, false);
  }
  // ================================================================ warnings: icons under the band, the edges lit
  bool stall = (plane.stallWarn > 0.8f && !plane.onGround && plane.ias > 10.f) || hudDemo;
  bool pullUp = (!plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f) || hudDemo;
  bool nearDest = length(plane.pos - d.pos()) < 4000.f;
  bool gearW = (spc.retract && plane.gear < 0.99f && !plane.onGround && plane.agl() < 200 && nearDest && plane.ias < spc.vref * 1.5f) || hudDemo;
  bool engOff = (!plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && !plane.glideOnly()) || hudDemo;
  {
    float pulse = 0.5f + 0.5f * sinf(realTime * 7.f);
    float master = anim(0x5741u, (stall || pullUp) ? 1.f : 0.f, 10.f);
    if (master > 0.01f) {   // the edge alert: the screen's rim glows red, harder with the pulse
      float ea = master * (0.22f + 0.3f * pulse), eb = 7 * s;
      g_ren.glow(0, band, eb, H - band, C_BAD, ea * 0.9f, 0, 26 * s); g_ren.glow(W - eb, band, eb, H - band, C_BAD, ea * 0.9f, 0, 26 * s);
      g_ren.glow(0, H - eb, W, eb, C_BAD, ea * 0.9f, 0, 26 * s); g_ren.glow(0, band, W, eb, C_BAD, ea * 0.9f, 0, 26 * s);
      g_ren.rect(0, band, 2 * s, H - band, C_BAD, ea); g_ren.rect(W - 2 * s, band, 2 * s, H - band, C_BAD, ea); g_ren.rect(0, H - 2 * s, W, 2 * s, C_BAD, ea);
    }
    struct Wn { bool on; int kind; const char* label; vec3 col; float alpha; } wn[4] = {
      {stall, 0, "STALL", C_BAD, 1.f}, {pullUp, 1, "PULL UP", C_BAD, 1.f}, {gearW, 2, "GEAR", C_WARN, 1.f}, {engOff, 3, "ENGINE OFF", C_BAD, 1.f}};
    int n = 0; for (auto& w : wn) if (w.on) n++;
    float isz = 68 * s, gap = 14 * s, x0 = W * 0.5f - (n * isz + (n - 1) * gap) * 0.5f, iy = band + 12 * s;
    for (auto& w : wn) {
      if (!w.on) continue;
      float a = 0.6f + 0.4f * pulse;
      g_ren.rect(x0, iy, isz, isz + 16 * s, vec3(0.03f, 0.0f, 0.0f), 0.7f, 6 * s);
      g_ren.rectOutline(x0, iy, isz, isz + 16 * s, w.col, a, 6 * s, 2 * s);
      g_ren.glow(x0, iy, isz, isz + 16 * s, w.col, 0.35f * a, 6 * s, 14 * s);
      hudWarnIcon(w.kind, x0 + isz * 0.5f, iy + isz * 0.5f - 2 * s, isz * 0.78f, w.col, a);
      g_ren.text(x0 + isz * 0.5f, iy + isz - 1 * s, 10.5f * s, w.label, w.col, a, 1, false);
      x0 += isz + gap;
    }
    if (engOff && n) g_ren.text(W * 0.5f, iy + isz + 22 * s, 11 * s, "press " + keyName(set.keyBind[ACT_ENGINE]) + " to restart", C_DIM, 0.9f, 1, false);
  }
  // ================================================================ the rails
  const float stripH = cockpit ? 0.f : 46 * s, pfd = 148 * s;
  const float tileW = 80 * s, tileH = 38 * s, tileGap = 5 * s; const int tileCols = 4;
  int tileRows = spc.special == 2 ? 3 : 2;
  const float tilesTop = H - stripH - 10 * s - tileRows * tileH - (tileRows - 1) * tileGap;
  float railTop = band + 10 * s, railBot = cockpit ? H - 60 * s : std::min(H - stripH - pfd - 18 * s, tilesTop - 30 * s), railH = railBot - railTop;
  if (!cockpit && railH > 160 * s) {
    // ---- left: the speed tape
    {
      float x = 0, mid = railTop + railH * 0.5f;
      float unit = set.metric ? 3.6f : MS_TO_KT; float v = plane.ias * unit, span = set.metric ? 120.f : 60.f, ppu = railH / (2.f * span);
      hudStrip(x, railTop, rail, railH, 0.9f);
      const PerfModel& P = Plane::perf(&spc);
      float vs = (plane.flaps > 0.5f ? P.vs0 : P.vs1) * unit * sqrtf(plane.mass() / std::max(spc.emptyMass + spc.maxFuel * 0.6f + 150.f, 1.f));
      auto Y = [&](float val) { return mid - (val - v) * ppu; };
      // the low-speed band: red below the stall, amber to 1.15 Vs (never for the research craft's fly-by-wire)
      if (!spc.special && vs > 0) {
        float y0 = clampf(Y(vs), railTop + 2 * s, railBot - 2 * s), y1 = clampf(Y(vs * 1.15f), railTop + 2 * s, railBot - 2 * s), yb = railBot - 2 * s;
        if (yb > y0) g_ren.rect(x + rail - 10 * s, y0, 6 * s, yb - y0, C_BAD, 0.75f);
        if (y0 > y1) g_ren.rect(x + rail - 10 * s, y1, 6 * s, y0 - y1, C_WARN, 0.75f);
        float yr = Y(spc.vref * unit); if (yr > railTop + 4 * s && yr < railBot - 4 * s) { g_ren.rect(x + rail - 22 * s, yr - 1 * s, 12 * s, 2 * s, C_GOOD, 0.9f); g_ren.text(x + 6 * s, yr - 5 * s, 9 * s, "REF", C_GOOD, 0.9f, 0, false); }
      }
      int step = set.metric ? 20 : 10;
      for (int t = (int)floorf((v - span) / step) * step; t <= v + span; t += step) {
        if (t < 0) continue;
        float y = Y((float)t); if (y < railTop + 4 * s || y > railBot - 4 * s) continue;
        bool major = t % (step * 2) == 0;
        g_ren.rect(x + rail - (major ? 18 : 11) * s, y - 0.5f * s, (major ? 10 : 5) * s, 1 * s, C_TEXT, 0.7f);
        if (major && fabsf(y - mid) > 14 * s) g_ren.text(x + rail - 22 * s, y - 6 * s, 11 * s, fmt("%d", t), C_TEXT, 0.85f, 2, false);
      }
      // the readout box with its pointer, the trend (where the speed will be in 6 s), Mach and ground speed
      float trend = hudPrevIas > 0.f && uiDt > 1e-4f && uiDt < 0.5f ? (plane.ias - hudPrevIas) / uiDt * 6.f * unit : 0.f; hudPrevIas = plane.ias;
      hudTrend = clampf(hudTrend + (trend - hudTrend) * (1.f - expf(-4.f * uiDt)), -span * 0.7f, span * 0.7f);
      float ty2 = clampf(Y(v + hudTrend), railTop + 14 * s, railBot - 14 * s);
      if (fabsf(hudTrend) > 1.f) { g_ren.rect(x + rail - 7 * s, std::min(mid, ty2), 3 * s, fabsf(ty2 - mid), C_ACCENT, 0.9f); hudDart(x + rail - 5.5f * s, ty2, 0.f, ty2 < mid ? -1.f : 1.f, 7 * s, 4 * s, C_ACCENT, 0.95f, false); }
      float bw = rail - 14 * s;
      g_ren.rect(x + 4 * s, mid - 15 * s, bw, 30 * s, vec3(0.0f, 0.02f, 0.04f), 0.95f, 3 * s);
      g_ren.rectOutline(x + 4 * s, mid - 15 * s, bw, 30 * s, stall ? C_BAD : C_ACCENT, 0.8f, 3 * s, 1.5f * s);
      g_ren.line(x + 4 * s + bw, mid, x + 4 * s + bw + 7 * s, mid, 2 * s, C_ACCENT, 0.9f);
      g_ren.text(x + 4 * s + bw * 0.5f, mid - 11 * s, 20 * s, fmt("%.0f", v), stall ? C_BAD : C_TEXT, 1, 1, false);
      g_ren.text(x + 8 * s, railTop + 6 * s, 10 * s, set.metric ? "KM/H" : "IAS KT", C_DIM, 1, 0, false);
      if (spc.engineType == ENG_JET) g_ren.text(x + 8 * s, railBot - 42 * s, 11 * s, fmt("M %.2f", plane.mach), plane.mach > 0.95f ? C_WARN : C_TEXT, 1, 0, false);
      g_ren.text(x + 8 * s, railBot - 28 * s, 10 * s, "GS", C_DIM, 1, 0, false);
      g_ren.text(x + 8 * s, railBot - 16 * s, 11 * s, fmt("%.0f", gs * unit), C_TEXT, 1, 0, false);
    }
    // ---- right: the altitude tape, the vertical speed bar, the radar height, the target bug
    {
      float x = W - rail, mid = railTop + railH * 0.5f;
      float unit = set.metric ? 1.f : M_TO_FT; float v = plane.pos.y * unit, span = set.metric ? 200.f : 600.f, ppu = railH / (2.f * span);
      hudStrip(x, railTop, rail, railH, 0.9f);
      auto Y = [&](float val) { return mid - (val - v) * ppu; };
      int step = set.metric ? 20 : 100, labelEvery = set.metric ? 100 : 500;
      for (int t = (int)floorf((v - span) / step) * step; t <= v + span; t += step) {
        float y = Y((float)t); if (y < railTop + 4 * s || y > railBot - 4 * s) continue;
        bool major = t % labelEvery == 0;
        g_ren.rect(x + 11 * s, y - 0.5f * s, (major ? 10 : 5) * s, 1 * s, C_TEXT, 0.7f);
        if (major && fabsf(y - mid) > 14 * s) g_ren.text(x + 24 * s, y - 6 * s, 11 * s, fmt("%d", t), C_TEXT, 0.85f, 0, false);
      }
      // the ground: where the terrain under the aircraft sits on the tape
      float gnd = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f) * unit, yg = Y(gnd);
      if (yg < railBot - 2 * s) { float y0 = std::max(yg, railTop + 2 * s); g_ren.rect(x + 4 * s, y0, 6 * s, railBot - 2 * s - y0, vec3(0.75f, 0.5f, 0.25f), 0.8f); for (float yy = y0; yy < railBot - 6 * s; yy += 6 * s) g_ren.line(x + 4 * s, yy + 6 * s, x + 10 * s, yy, 1 * s, vec3(0.1f, 0.05f, 0.0f), 0.6f); }
      if (toWp) { float yt = Y(target.y * unit); if (yt > railTop + 4 * s && yt < railBot - 4 * s) hudDart(x + 10 * s, yt, -1.f, 0.f, 12 * s, 6 * s, mag, 1.f); }
      if (plane.apOn && plane.apMode == Plane::AP_HOLD) { float ya = Y(plane.apAlt * unit); if (ya > railTop + 4 * s && ya < railBot - 4 * s) g_ren.rect(x + 10 * s, ya - 3 * s, 8 * s, 6 * s, APC, 0.95f); }
      float bw = rail - 14 * s;
      g_ren.rect(x + 10 * s, mid - 15 * s, bw, 30 * s, vec3(0.0f, 0.02f, 0.04f), 0.95f, 3 * s);
      g_ren.rectOutline(x + 10 * s, mid - 15 * s, bw, 30 * s, pullUp ? C_BAD : C_ACCENT, 0.8f, 3 * s, 1.5f * s);
      g_ren.line(x + 3 * s, mid, x + 10 * s, mid, 2 * s, C_ACCENT, 0.9f);
      g_ren.text(x + 10 * s + bw * 0.5f, mid - 11 * s, 20 * s, fmt("%.0f", v), pullUp ? C_BAD : C_TEXT, 1, 1, false);
      g_ren.text(x + rail - 8 * s, railTop + 6 * s, 10 * s, set.metric ? "ALT M" : "ALT FT", C_DIM, 1, 2, false);
      float agl = plane.agl();
      if (agl * unit < (set.metric ? 800.f : 2500.f)) g_ren.text(x + 10 * s + bw * 0.5f, mid + 18 * s, 11 * s, fmt("RA %.0f", agl * unit), agl < 60.f ? C_WARN : C_ACCENT, 1, 1, false);
      // vertical speed: a bar from the middle, +-2000 fpm (+-10 m/s), the number at its tip
      float vsv = set.metric ? plane.vel.y : plane.vel.y * 196.85f, vsMax = set.metric ? 10.f : 2000.f;
      float vh = clampf(vsv / vsMax, -1.f, 1.f) * (railH * 0.5f - 40 * s);
      g_ren.rect(x + rail - 8 * s, railTop + 30 * s, 2 * s, railH - 60 * s, C_ACCENT, 0.2f);
      g_ren.rect(x + rail - 9 * s, mid - std::max(vh, 0.f), 4 * s, fabsf(vh), vsv >= 0 ? C_GOOD : C_WARN, 0.9f);
      g_ren.text(x + rail - 8 * s, railBot - 28 * s, 10 * s, set.metric ? "V/S M/S" : "V/S FPM", C_DIM, 1, 2, false);
      g_ren.text(x + rail - 8 * s, railBot - 16 * s, 11 * s, fmt("%+.0f", vsv), C_TEXT, 1, 2, false);
    }
  }
  // ================================================================ bottom-left: the attitude ball and the approach needles
  if (!cockpit) {
    float px = 16 * s, py = H - stripH - pfd - 8 * s;
    drawPFD(px, py, pfd);
    float best = 1e9f; vec3 thr, ldir; bool found = false;
    for (int end = 0; end < 2; end++) {
      vec3 dir = end ? -d.dir() : d.dir(); vec3 th = d.threshold(end == 1); vec3 rel3 = plane.pos - th;
      float along = -dot(vec3(rel3.x, 0, rel3.z), dir); if (along < 0) continue;
      float lat = fabsf(dot(vec3(rel3.x, 0, rel3.z), vec3(-dir.z, 0, dir.x)));
      if (along < 9000 && lat < along * 0.35f + 300 && along < best && (wpIndex >= (int)contract.wps.size())) { best = along; thr = th; ldir = dir; found = true; }
    }
    if (found && !plane.onGround && best > 100) {
      vec3 rel3 = plane.pos - thr; float along = best + 300.f, ideal = d.elev + tanf(3.f * DEG) * along;
      float dev = (plane.pos.y - ideal) / std::max(along * 0.0122f, 8.f);
      float lat = dot(vec3(rel3.x, 0, rel3.z), vec3(-ldir.z, 0, ldir.x)) / std::max(along * 0.0175f, 10.f);
      float gx = px + pfd + 10 * s, gy = py + pfd * 0.5f;
      g_ren.rect(gx, py + 14 * s, 12 * s, pfd - 28 * s, vec3(0, 0, 0), 0.5f, 6 * s);
      for (int k = -2; k <= 2; k++) if (k) hudRing(gx + 6 * s, gy - k * (pfd - 28 * s) / 5.f, 2 * s, 1 * s, C_TEXT, 0.6f, 10);
      g_ren.rect(gx + 1 * s, gy - 1 * s, 10 * s, 2 * s, vec3(1, 1, 1), 1);
      float dy = clampf(-dev, -2.5f, 2.5f) * (pfd - 28 * s) / 5.f;
      g_ren.rect(gx + 1 * s, gy - dy - 5 * s, 10 * s, 10 * s, mag, 1, 5 * s);
      float lx = gx + 30 * s, ly = gy - 5 * s, lw = 120 * s;
      g_ren.rect(lx, ly, lw, 10 * s, vec3(0, 0, 0), 0.5f, 5 * s);
      for (int k = -2; k <= 2; k++) if (k) hudRing(lx + lw * 0.5f + k * lw / 5.f, ly + 5 * s, 2 * s, 1 * s, C_TEXT, 0.6f, 10);
      float dx = clampf(-lat, -2.5f, 2.5f) * lw * 0.5f / 2.5f;
      g_ren.rect(lx + lw * 0.5f + dx - 5 * s, ly, 10 * s, 10 * s, mag, 1, 5 * s);
      std::string gp = dev > 1.f ? "HIGH" : dev < -1.f ? "LOW" : "ON PATH";
      g_ren.text(lx, ly - 16 * s, 9.5f * s, "LOC", C_DIM, 1, 0, false);
      g_ren.text(gx + 6 * s, py + 2 * s, 9.5f * s, "G/S", C_DIM, 1, 1, false);
      g_ren.text(lx, ly + 16 * s, 11 * s, gp, dev < -1.5f ? C_BAD : C_TEXT, 1, 0, false);
    }
  }
  // ================================================================ bottom-right: the systems tiles
  if (!cockpit) {
    const AircraftSpec& sp = spc;
    struct Tile { std::string k, v; vec3 c; float bar; };   // bar < 0: none
    std::vector<Tile> tiles;
    tiles.push_back({"THR", fmt("%.0f%%", plane.ctl.throttle * 100), plane.ctl.throttle > 0.85f ? C_WARN : C_TEXT, plane.ctl.throttle});
    if (sp.engineType == ENG_PISTON) tiles.push_back({"RPM", plane.engineRunning ? fmt("%.0f", plane.rpm) : (plane.starterTime > 0 ? "CRANK" : "OFF"), plane.engineRunning ? C_TEXT : C_BAD, plane.engineRunning ? clampf(plane.rpm / sp.maxRpm, 0.f, 1.f) : 0.f});
    else tiles.push_back({"N1", plane.engineRunning ? fmt("%.1f%%", plane.n1) : (plane.starterTime > 0 ? fmt("ST %.0f", plane.n1) : "OFF"), plane.engineRunning ? C_TEXT : C_BAD, clampf(plane.n1 / 100.f, 0.f, 1.f)});
    float fuelFrac = plane.fuel / std::max(sp.maxFuel, 1.f);
    if (sp.special) { tiles.push_back({"FUEL", "CELL", C_ACCENT, 1.f}); tiles.push_back({"MACH", fmt("%.2f", plane.mach), plane.mach > sp.designMach * 0.95f ? C_WARN : C_TEXT, -1.f}); }
    else { tiles.push_back({"FUEL", fmt("%.0f%%", fuelFrac * 100), fuelFrac < 0.15f ? C_BAD : C_TEXT, fuelFrac}); tiles.push_back({"RANGE", fmt("%.0f km", plane.rangeLeftKm()), fuelFrac < 0.15f ? C_BAD : C_TEXT, -1.f}); }
    if (sp.special == 2) {
      const vec3 VIO(0.8f, 0.5f, 1.f);
      tiles.push_back({"PODS", fmt("%.0f deg%s", plane.nozzle * 90, plane.nozzle > 0.99f ? " VTOL" : ""), C_TEXT, plane.nozzle});
      tiles.push_back({"CLOAK", wraith.stealth > 0.99f ? "ACTIVE" : wraith.stealth > 0.01f ? fmt("%.0f%%", wraith.stealth * 100) : "OFF", wraith.stealth > 0.01f ? VIO : C_DIM, wraith.stealth});
      tiles.push_back({"WEAPONS", wraith.lasers > 0.97f ? "HOT" : wraith.lasers > 0.01f ? "DEPLOY" : "SAFE", wraith.lasers > 0.97f ? C_BAD : C_DIM, -1.f});
      tiles.push_back({"PLASMA", wraith.bay > 0.05f ? "BAY OPEN" : wraith.bombLoaded >= 1.f ? "READY" : "CHARGING", wraith.bombLoaded >= 1.f ? VIO : C_WARN, wraith.bombLoaded});
    } else if (sp.special) tiles.push_back({"TVC", fmt("%+.0f deg", -clampf(plane.ctl.pitch + plane.ctl.trim * 0.3f, -1, 1) * 0.5f / DEG), C_TEXT, -1.f});
    else tiles.push_back({"FLAPS", fmt("%.0f%%", plane.flaps * 100), plane.flaps > 0.01f ? C_ACCENT : C_TEXT, plane.flaps});
    std::string gearS = !sp.retract ? "FIXED" : plane.gear > 0.99f ? "DOWN" : plane.gear < 0.01f ? "UP" : "TRANSIT";
    tiles.push_back({"GEAR", gearS, !sp.retract ? C_DIM : plane.gear > 0.99f ? C_GOOD : plane.gear < 0.01f ? C_DIM : C_WARN, sp.retract ? plane.gear : -1.f});
    tiles.push_back({"TRIM", fmt("%+.0f", plane.ctl.trim * 100), C_TEXT, -1.f});
    int rows = ((int)tiles.size() + tileCols - 1) / tileCols;
    float bx = W - tileCols * tileW - (tileCols - 1) * tileGap - 14 * s, by = H - stripH - 10 * s - rows * tileH - (rows - 1) * tileGap;
    hudStrip(bx - 8 * s, by - 20 * s, tileCols * tileW + (tileCols - 1) * tileGap + 16 * s, rows * tileH + (rows - 1) * tileGap + 28 * s, 0.9f);
    g_ren.text(bx, by - 15 * s, 9.5f * s, sp.special == 2 ? "XR-40 SYSTEMS" : sp.special ? "XR-30 SYSTEMS" : "SYSTEMS", C_ACCENT, 0.9f, 0, false);
    for (size_t i = 0; i < tiles.size(); i++) {
      float x = bx + (i % tileCols) * (tileW + tileGap), y = by + (i / tileCols) * (tileH + tileGap);
      g_ren.rect(x, y, tileW, tileH, vec3(0.0f, 0.02f, 0.04f), 0.55f, 3 * s);
      g_ren.rectOutline(x, y, tileW, tileH, C_ACCENT, 0.14f, 3 * s, 1 * s);
      g_ren.text(x + 6 * s, y + 4 * s, 9 * s, tiles[i].k, C_DIM, 1, 0, false);
      g_ren.text(x + 6 * s, y + 16 * s, 13 * s, ellipsize(tiles[i].v, tileW - 12 * s, 13 * s), tiles[i].c, 1, 0, false);
      if (tiles[i].bar >= 0.f) { g_ren.rect(x + 6 * s, y + tileH - 6 * s, tileW - 12 * s, 2.5f * s, C_ACCENT, 0.15f); g_ren.rect(x + 6 * s, y + tileH - 6 * s, (tileW - 12 * s) * clampf(tiles[i].bar, 0.f, 1.f), 2.5f * s, tiles[i].c, 0.9f); }
    }
  }
  // ================================================================ the bottom strip: wind, the instructor, g and the mode chips
  if (!cockpit) {
    float sy = H - stripH;
    hudStrip(0, sy, W, stripH, 1.f);
    // left: the wind dial and its numbers (windEnd: where they end, for the instructor beside them)
    float windEnd = 300 * s;
    {
      vec3 wv = plane.windVel; float ws = length(vec3(wv.x, 0, wv.z)); float from = wrapDeg360(atan2f(-wv.x, wv.z) / DEG);
      float cxw = 30 * s, cyw = sy + stripH * 0.5f, R = 17 * s;
      hudRing(cxw, cyw, R, 1.2f * s, C_ACCENT, 0.5f, 32);
      for (int k = 0; k < 8; k++) { float a = k * PI / 4; g_ren.line(cxw + sinf(a) * R * 0.78f, cyw - cosf(a) * R * 0.78f, cxw + sinf(a) * R, cyw - cosf(a) * R, (k % 2 ? 1.f : 1.6f) * s, C_TEXT, 0.5f); }
      g_ren.rect(cxw - 2 * s, cyw - R + 2 * s, 4 * s, 4 * s, C_TEXT, 0.9f, 2 * s);
      g_ren.text(58 * s, sy + 6 * s, 9 * s, "WIND", C_DIM, 1, 0, false);
      if (ws > 0.5f) {
        float rel = (from - hdg) * DEG; vec2 src(sinf(rel), -cosf(rel)), dv(-src.x, -src.y);
        vec3 wc(0.45f, 0.85f, 1.f); float wscale = clampf(ws / 12.f, 0.5f, 1.f);
        hudArrow(cxw + src.x * R * 0.72f, cyw + src.y * R * 0.72f, cxw + dv.x * R * 0.8f, cyw + dv.y * R * 0.8f, (1.4f + 1.4f * wscale) * s, 8 * s, (3.5f + 2.f * wscale) * s, wc, 1.f);
        float hw = ws * cosf(rel), xw = ws * sinf(rel);
        std::string g = wx.gust > 0.5f ? fmt(" G%.0f", (wx.windSpeed + wx.gust) * (set.metric ? 3.6f : MS_TO_KT)) : "";
        const std::string wtxt = fmt("%03.0f / %s", from, fmtSpeed(ws).c_str()) + g;
        g_ren.text(58 * s, sy + 16 * s, 14 * s, wtxt, C_TEXT, 1, 0, false);
        // (the components start after the wind's own figures: a gust and km/h ran them into a fixed column)
        const float cxc = 58 * s + std::max(118 * s, g_ren.textWidth(wtxt, 14 * s) + 18 * s);
        g_ren.text(cxc, sy + 6 * s, 9 * s, "COMPONENTS", C_DIM, 1, 0, false);
        const std::string comp = fmt("%s %s   X %s %s", hw >= 0 ? "HEAD" : "TAIL", fmtSpeed(fabsf(hw)).c_str(), fmtSpeed(fabsf(xw)).c_str(), xw >= 0 ? "R" : "L");
        g_ren.text(cxc, sy + 17 * s, 12 * s, comp, fabsf(xw) > 7.f ? C_WARN : C_DIM, 1, 0, false);
        windEnd = std::max(windEnd, cxc + g_ren.textWidth(comp, 12 * s));
      } else g_ren.text(58 * s, sy + 16 * s, 14 * s, "CALM", C_TEXT, 1, 0, false);
    }
    // right: g against the structure, then the mode chips and the autopilot's words
    float rx = W - 14 * s;
    {
      float gl = spc.gLimitPos(), gf = clampf(plane.gLoad / gl, -0.3f, 1.2f);
      float bwid = 90 * s, bx = rx - bwid;
      g_ren.text(rx, sy + 6 * s, 9 * s, "G LOAD", C_DIM, 1, 2, false);
      g_ren.text(bx - 8 * s, sy + 15 * s, 14 * s, fmt("%.1f", plane.gLoad), plane.gLoad > gl * 0.85f ? C_BAD : C_TEXT, 1, 2, false);
      g_ren.rect(bx, sy + 22 * s, bwid, 4 * s, C_ACCENT, 0.15f);
      g_ren.rect(bx, sy + 22 * s, bwid * clampf(gf, 0.f, 1.f), 4 * s, gf > 0.85f ? C_BAD : gf > 0.6f ? C_WARN : C_ACCENT, 0.95f);
      g_ren.rect(bx + bwid - 1 * s, sy + 18 * s, 2 * s, 12 * s, C_BAD, 0.8f);
      if (plane.overG > 0.05f) g_ren.rect(bx, sy + 30 * s, bwid * clampf(plane.overG, 0.f, 1.f), 2 * s, C_BAD, 0.9f);
      g_ren.text(bx + bwid, sy + 30 * s, 8.5f * s, fmt("+%.0f", gl), C_DIM, 0.8f, 2, false);
      rx = bx - 50 * s;
    }
    {
      float cx = rx, cy = sy + 13 * s;
      auto chip = [&](const std::string& t, vec3 c, bool on) {
        float w = g_ren.textWidth(t, 10 * s) + 14 * s; cx -= w;
        g_ren.rect(cx, cy, w, 20 * s, on ? c * 0.3f : vec3(0.0f, 0.02f, 0.04f), on ? 0.9f : 0.5f, 2 * s);
        g_ren.rectOutline(cx, cy, w, 20 * s, c, on ? 0.9f : 0.2f, 2 * s, 1 * s);
        g_ren.text(cx + 7 * s, cy + 5 * s, 10 * s, t, on ? C_TEXT : C_DIM, on ? 1.f : 0.6f, 0, false);
        cx -= 6 * s;
      };
      chip("LDG LT", C_ACCENT, landingLight); chip("BRAKE", C_WARN, plane.ctl.brake > 0.5f); chip("AP", APC, plane.apOn);
      if (plane.apOn) { std::string ap = plane.apStatus; float aw = g_ren.textWidth(ap, 11 * s); cx -= aw; g_ren.text(cx, sy + 17 * s, 11 * s, ap, APC, 1, 0, false); cx -= 10 * s; }
      rx = cx;
    }
    // centre: the instructor
    if (set.showHints && !hint.empty() && !crashed) {
      float hx0 = std::max(330 * s, windEnd + 24 * s), hx1 = rx - 24 * s, hw = hx1 - hx0;
      if (hw > 200 * s) {
        auto lines = wrap(hint, hw - 70 * s, 12.5f * s);
        if (lines.size() > 2) { lines.resize(2); lines[1] = ellipsize(lines[1] + " ...", hw - 70 * s, 12.5f * s); }
        g_ren.rect(hx0, sy + 6 * s, 3 * s, stripH - 12 * s, C_GOOD, 1);
        g_ren.text(hx0 + 12 * s, sy + 5 * s, 8.5f * s, "INSTRUCTOR", C_GOOD, 1, 0, false);
        float ly = sy + (lines.size() == 1 ? 15 * s : 7 * s) + 7 * s;
        for (auto& l : lines) { g_ren.text(hx0 + 64 * s, ly, 12.5f * s, l, C_TEXT, 1, 0, false); ly += 16 * s; }
      }
    }
  } else if (!spc.special) {
    // cockpit view: the 3D panel carries the instruments; a compact readout strip along the bottom edge
    std::string ro = fmt("IAS %s   ALT %s   VS %+.0f   THR %.0f%%   FLAPS %.0f%%   %s   FUEL %.0f%%", fmtSpeed(plane.ias).c_str(), fmtAlt(plane.pos.y).c_str(),
                         plane.vel.y * 196.85f, plane.ctl.throttle * 100, plane.flaps * 100,
                         !spc.retract ? "GEAR FIXED" : plane.gear > 0.99f ? "GEAR DOWN" : plane.gear < 0.01f ? "GEAR UP" : "GEAR TRANSIT", plane.fuel / spc.maxFuel * 100);
    if (plane.apOn) ro += "   AP " + plane.apStatus;
    if (plane.ctl.brake > 0.5f) ro += "   BRAKE";
    float tw = g_ren.textWidth(ro, 14 * s) + 30 * s;
    hudStrip(W * 0.5f - tw * 0.5f, H - 34 * s, tw, 28 * s, 0.9f);
    g_ren.text(W * 0.5f, H - 28 * s, 14 * s, ro, C_TEXT, 0.95f, 1, false);
    if (set.showHints && !hint.empty() && !crashed) {
      float hw = std::min(760 * s, W - 40 * s); auto lines = wrap(hint, hw - 40 * s, 14 * s); if (lines.size() > 2) lines.resize(2);
      float hh = 24 * s + lines.size() * 18 * s, hx = W * 0.5f - hw * 0.5f, hy = H - 34 * s - hh - 8 * s;
      g_ren.rectGrad(hx, hy, hw, hh, vec3(0.02f, 0.08f, 0.06f), vec3(0.0f, 0.03f, 0.02f), 0.72f, 3 * s); g_ren.rect(hx, hy, 3 * s, hh, C_GOOD, 1);
      g_ren.text(hx + 14 * s, hy + 5 * s, 9 * s, "INSTRUCTOR", C_GOOD, 1, 0, false);
      float ly = hy + 18 * s; for (auto& l : lines) { g_ren.text(hx + 14 * s, ly, 14 * s, l, C_TEXT, 1); ly += 18 * s; }
    }
  }
  // ================================================================ the message rails
  // left, inboard of the speed tape: the tower's last call. Right, inboard of the altitude tape: the failure
  // annunciators (steady) and above them, drawn by drawToasts, the events as they come
  float msgTop = band + 12 * s;
  if (!atcF.lastCall.empty() && !researchFlight && atcF.lastApt >= 0) {
    float rw = std::min(kHudMsgW * s, W * 0.5f - rail - 120 * s), fs = 12.5f * s, rx = (cockpit ? 16 * s : rail + 12 * s), ry = msgTop;
    if (rw > 160 * s) {
      auto lines = wrap(atcF.lastCall, rw - 24 * s, fs);
      if (lines.size() > 3) { lines.resize(3); lines[2] = ellipsize(lines[2] + " ...", rw - 24 * s, fs); }
      float rh = 28 * s + lines.size() * (fs + 5 * s);
      int age = (int)std::max(0.f, flightClock - atcF.lastT);
      vec3 tc = vec3(0.55f, 1.f, 0.72f);
      g_ren.rectGrad(rx, ry, rw, rh, vec3(0.0f, 0.05f, 0.04f), vec3(0.0f, 0.02f, 0.02f), 0.6f, 3 * s);
      g_ren.rect(rx, ry, 3 * s, rh, atcF.goAround || atcF.holding ? C_BAD : atcF.lastValid ? tc : C_WARN, 0.95f);
      g_ren.text(rx + 12 * s, ry + 6 * s, 10 * s, fmt("TWR  %s", g_world.airports[atcF.lastApt].code), tc, 1, 0, false);
      g_ren.text(rx + rw - 10 * s, ry + 6 * s, 10 * s, atcF.goAround ? std::string("GO AROUND - COMPLY") : atcF.holding ? std::string("HOLD - WAIT") : atcF.lastValid ? fmt("%d:%02d AGO%s", age / 60, age % 60, atcF.lastBeforePause ? "  (BEFORE PAUSE)" : "") : std::string("NO LONGER VALID"),
                 atcF.goAround || atcF.holding ? C_BAD : atcF.lastValid ? C_DIM : C_WARN, 1, 2, false);
      float ly = ry + 22 * s;
      for (auto& l : lines) { g_ren.text(rx + 12 * s, ly, fs, l, atcF.lastValid ? C_TEXT : C_DIM, atcF.lastValid ? 1.f : 0.6f, 0, false); ly += fs + 5 * s; }
    }
  }
  {   // annunciators (C7), bottom of the right message column, steady, each with its own lamp
    std::vector<Annunciator> ann = hudAnnunciators();
    float rw = std::min(kHudMsgW * s, W * 0.5f - rail - 160 * s), rx = W - (cockpit ? 16 * s : rail + 28 * s) - rw;
    float ay = msgTop;
    for (auto& a : ann) {
      g_ren.rectGrad(rx, ay, rw, 22 * s, vec3(0.05f, 0.01f, 0.0f), vec3(0.02f, 0.0f, 0.0f), 0.65f, 3 * s);
      vec3 ac = a.bad ? C_BAD : C_WARN;
      g_ren.rect(rx, ay, 3 * s, 22 * s, ac, 0.95f);
      g_ren.rect(rx + rw - 10 * s, ay + 8 * s, 5 * s, 5 * s, ac, 0.6f + 0.4f * (fmodf(realTime, 1.f) < 0.5f ? 1.f : 0.f), 2.5f * s);
      g_ren.text(rx + 10 * s, ay + 5 * s, 11 * s, ellipsize(a.text, rw - 28 * s, 11 * s), ac, 0.95f, 0, false);
      ay += 26 * s;
    }
    hudMsgNext = ay + (ann.empty() ? 0.f : 6 * s);   // (the toasts stack under the annunciators: drawToasts)
  }
  // minimap: inboard of the altitude tape, above the systems tiles
  if (showMinimap && !cockpit) { float mm = 168 * s, range = clampf(dist * 1.3f, 3000.f, 20000.f); drawMinimap(W - rail - 16 * s - mm, tilesTop - mm - 34 * s, mm, range); }
  else if (showMinimap) { float mm = 168 * s, range = clampf(dist * 1.3f, 3000.f, 20000.f); drawMinimap(W - 24 * s - mm, H - 60 * s - mm, mm, range); }
  // ================================================================ the target in the world (last, over the panels)
  {
    auto label = [&](float x, float y, const std::string& t) {
      float tw2 = g_ren.textWidth(t, 13 * s);
      g_ren.rect(x - tw2 * 0.5f - 6 * s, y - 2 * s, tw2 + 12 * s, 19 * s, vec3(0.01f, 0.02f, 0.04f), 0.7f, 4 * s);
      g_ren.text(x, y, 13 * s, t, mag, 1, 1);
    };
    vec3 tgt3 = target; if (!toWp) tgt3.y = d.elev + 3.f;
    float sx, sy;
    vec3 rel3 = tgt3 - fp.camPos;
    float zc = dot(rel3, -fp.camBack);
    const float bandT = band + 100 * s, bandB = cockpit ? H - 70 * s : tilesTop - 40 * s, bandC = 0.5f * (bandT + bandB), bandH = std::max(40 * s, 0.5f * (bandB - bandT));
    bool onS = zc > 1.f && g_ren.project(fp, tgt3, sx, sy) && sx > rail + 20 * s && sx < W - rail - 20 * s && sy > bandT && sy < bandB;
    std::string lab = dist < 1000.f ? fmt("%.0f m", length(rel3)) : fmt("%.1f km", dist / 1000.f);
    if (toWp && fabsf(target.y - plane.pos.y) > 45.f) lab += fmt("  %s%s", target.y > plane.pos.y ? "+" : "-", fmtAlt(fabsf(target.y - plane.pos.y)).c_str());
    if (onS) {
      float gx, gy; vec3 gpt(tgt3.x, g_world.height(tgt3.x, tgt3.z), tgt3.z);
      if (toWp && g_ren.project(fp, gpt, gx, gy) && gy > sy + 8 * s) {
        float len = gy - sy;
        for (float u = 0; u < len; u += 9 * s) g_ren.line(sx, sy + u, sx, sy + std::min(u + 5 * s, len), 1.5f * s, mag, 0.6f);
        g_ren.line(gx - 6 * s, gy, gx + 6 * s, gy, 1.5f * s, mag, 0.6f);
      }
      float r = 11 * s + 4 * s * (0.5f + 0.5f * sinf(realTime * 4.f));
      g_ren.line(sx - r, sy, sx, sy - r, 2.5f * s, mag, 0.95f); g_ren.line(sx, sy - r, sx + r, sy, 2.5f * s, mag, 0.95f);
      g_ren.line(sx + r, sy, sx, sy + r, 2.5f * s, mag, 0.95f); g_ren.line(sx, sy + r, sx - r, sy, 2.5f * s, mag, 0.95f);
      g_ren.rect(sx - 2.5f * s, sy - 2.5f * s, 5 * s, 5 * s, mag, 1, 2.5f * s);
      label(sx, sy + r + 4 * s, lab);
    } else {
      vec2 dir(dot(rel3, fp.camRight), -dot(rel3, fp.camUp));
      if (zc < 0) dir = vec2(dir.x >= 0 ? 1.f : -1.f, clampf(dir.y / (fabsf(dir.x) + fabsf(dir.y) + 1e-3f), -0.4f, 0.4f));
      { float dl = length(dir); dir = vec2(dir.x / dl, dir.y / dl); }
      float t2 = std::min(fabsf((W * 0.5f - rail - 50 * s) / std::max(fabsf(dir.x), 1e-3f)), fabsf(bandH / std::max(fabsf(dir.y), 1e-3f)));
      float ex2 = W * 0.5f + dir.x * t2, ey2 = bandC + dir.y * t2;
      if (showMinimap && !cockpit) {   // the pointer keeps off the minimap: lifted above it when they would meet
        float mm = 168 * s, mx0 = W - rail - 16 * s - mm - 30 * s, my0 = tilesTop - mm - 34 * s - 30 * s;
        if (ex2 > mx0 && ey2 > my0) ey2 = my0;
      }
      g_ren.rect(ex2 - 20 * s, ey2 - 20 * s, 40 * s, 40 * s, vec3(0.01f, 0.03f, 0.05f), 0.5f, 20 * s);
      hudRing(ex2, ey2, 19 * s, 1.2f * s, mag, 0.5f, 32);
      float pulse2 = 1.f + 0.12f * sinf(realTime * 5.f);
      hudDart(ex2 + dir.x * 11 * s * pulse2, ey2 + dir.y * 11 * s * pulse2, dir.x, dir.y, 22 * s * pulse2, 9 * s, mag, 1.f);
      label(ex2 - dir.x * 48 * s, ey2 - dir.y * 48 * s - 8 * s, lab);
    }
  }
  // (the radio: under the tower recall on the left)
  if (showRadio) drawRadioPanel(cockpit ? 16 * s : rail + 12 * s, msgTop + 110 * s);
  else if (radio.state() == Radio::PLAYING) g_ren.text(cockpit ? 16 * s : rail + 12 * s, msgTop + 110 * s, 11 * s, "RADIO  " + stations[std::clamp(set.radioStation, 0, (int)stations.size() - 1)].first, C_DIM, 0.8f, 0, false);
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
  {   // aerial imagery rendered from the real terrain on the GPU; re-rendered only when the view drifts or zooms
    int N = std::clamp((int)(msz * 1.4f / 256.f + 0.5f) * 256, 768, 2048);
    bool stale = !gpsMapValid || N != gpsMapN || range * 1.04f > gpsMapHalf || range * 1.7f < gpsMapHalf ||
                 fabsf(plane.pos.x - gpsMapC.x) + range > gpsMapHalf || fabsf(plane.pos.z - gpsMapC.y) + range > gpsMapHalf;
    if (stale && g_ren.ok) {
      gpsMapHalf = range * 1.3f; gpsMapC = vec2(plane.pos.x, plane.pos.z); gpsMapN = N;
      g_ren.renderMap(gpsMapC.x, gpsMapC.y, gpsMapHalf, N);
      gpsMapValid = true;
    }
    if (gpsMapValid && g_ren.mapTex()) {
      float u0 = (plane.pos.x - range - (gpsMapC.x - gpsMapHalf)) / (2 * gpsMapHalf), u1 = u0 + range / gpsMapHalf;
      float v0 = (plane.pos.z - range - (gpsMapC.y - gpsMapHalf)) / (2 * gpsMapHalf), v1 = v0 + range / gpsMapHalf;
      g_ren.image(g_ren.mapTex(), mx, my, msz, msz, u0, v0, u1, v1, e);
      g_ren.flushUIPublic();
    }
  }
  g_ren.rect(mx, my, msz, msz, vec3(0.0f, 0.05f, 0.1f), 0.1f * e);   // a touch of display tint over the imagery
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
  // fuel range ring (with every engine stopped: the glide range from this height instead)
  float rangeM = plane.rangeLeftKm() * 1000.f;
  bool gliding = plane.glideOnly() && !plane.onGround;
  if (gliding) rangeM = std::max(0.f, plane.agl() - 60.f) * plane.glideRatio() * 0.85f;   // (a margin for the turns and the pattern)
  {
    float rp = rangeM * k; int n = 120;
    for (int i = 0; i < n; i += 2) { float a0 = i * 6.2832f / n + T * 0.05f, a1 = (i + 1) * 6.2832f / n + T * 0.05f; seg(vec2(C.x + cosf(a0) * rp, C.y + sinf(a0) * rp), vec2(C.x + cosf(a1) * rp, C.y + sinf(a1) * rp), 2 * s, gliding ? C_BAD : C_WARN, 0.55f); }
    if (gliding) { vec2 lp(C.x, C.y - rp); if (inside(lp, 20 * s)) g_ren.text(lp.x, lp.y - 16 * s, 12 * s, fmt("GLIDE %.1f km", rangeM / 1000.f), C_BAD, 0.9f * e, 1, false); }
  }
  // breadcrumb trail
  for (size_t i = 0; i < trail.size(); i++) { vec2 p = toS(trail[i].x, trail[i].y); if (inside(p, 2 * s)) g_ren.rect(p.x - 1.5f * s, p.y - 1.5f * s, 3 * s, 3 * s, C_ACCENT, (0.25f + 0.6f * (float)i / trail.size()) * e, 1.5f * s); }
  // route: departure -> checkpoints -> destination, flowing dashes; active leg bright
  const vec3 MAG(1.f, 0.35f, 1.f), AP_CYAN(0.3f, 0.95f, 1.f);
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
    if (i == apDest) {   // autoland pick: cyan target ring
      float r = 14 * s + 2 * s * sinf(T * 4.f);
      if (inside(p, r)) { g_ren.glow(p.x - r, p.y - r, 2 * r, 2 * r, AP_CYAN, 0.3f * e, r, 8 * s); g_ren.rectOutline(p.x - r, p.y - r, 2 * r, 2 * r, AP_CYAN, e, r, 2.5f * s); }
      if (inside(p, 30 * s)) g_ren.text(p.x + 8 * s, p.y - 16 * s, 11 * s, "AUTOLAND", AP_CYAN, e, 0, true);
    }
    // click an airport to pick it for the autopilot
    if (in.mPressed[0] && inside(p, 0) && fabsf(in.mx - p.x) < 16 * s && fabsf(in.my - p.y) < 16 * s && apDest != i) { apDest = i; g_audio.trigger(SFX_CLICK); }
  }
  // the autopilot's approach plan: descent orbit, intercept and final approach course
  if (plane.apOn && plane.apMode == Plane::AP_APPR) {
    vec2 hc = toS(plane.apHoldC.x, plane.apHoldC.z); float hr = plane.apHoldR * k;
    for (int i = 0; i < 48; i += 2) { float a0 = i * 6.2832f / 48 + T * 0.3f, a1 = (i + 1) * 6.2832f / 48 + T * 0.3f; seg(vec2(hc.x + cosf(a0) * hr, hc.y + sinf(a0) * hr), vec2(hc.x + cosf(a1) * hr, hc.y + sinf(a1) * hr), 1.5f * s, AP_CYAN, 0.6f); }
    vec3 fa = plane.apTd - plane.apLd * plane.apFinalLen, fb = plane.apTd;
    seg(toS(fa.x, fa.z), toS(fb.x, fb.z), 3 * s, AP_CYAN, 0.9f);
    vec3 fx = plane.apTd - plane.apLd * (plane.apFinalLen + 4000.f);
    dashed(toS(fx.x, fx.z), toS(fa.x, fa.z), 1.5f * s, AP_CYAN, 0.6f, 6 * s, 0);
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
    (void)r;
    hudDart(C.x + f.x * a, C.y + f.y * a, f.x, f.y, a * 1.8f, a * 0.85f, vec3(1, 1, 0.3f), e);
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
  if (contract.timeLimitMin > 0) { float left = contract.timeLimitMin * 60 - jobClockBase - flightClock; kv("DEADLINE", left > 0 ? fmt("%d:%02d", (int)left / 60, (int)left % 60) : "LATE", left > 120 ? C_TEXT : C_BAD); }
  {   // the job's own meter (C6)
    auto bar = [&](const char* k, float v) { std::string b; int n = (int)(clampf(v, 0.f, 1.f) * 10.f + 0.5f); for (int i = 0; i < 10; i++) b += i < n ? "|" : "."; kv(k, fmt("%s %.0f%%", b.c_str(), v * 100.f), v > 0.7f ? C_GOOD : v > 0.4f ? C_WARN : C_BAD); };
    if (contract.type == CT_MEDEVAC) bar("PATIENT", result.patient);
    if (contract.type == CT_VIP) bar("COMFORT", result.comfort);
    if (contract.type == CT_SURVEY && !contract.wps.empty()) { float dAlt = plane.pos.y - contract.wps[0].alt; kv("SURVEY ALT", fmt("%s %s", fmtAlt(contract.wps[0].alt).c_str(), fabsf(dAlt) <= 46.f ? "IN BAND" : dAlt > 0 ? "HIGH" : "LOW"), fabsf(dAlt) <= 46.f ? C_GOOD : C_BAD); if (surveyT > 1.f) kv("IN BAND", fmt("%.0f%%", result.surveyInBand * 100.f), result.surveyInBand > 0.9f ? C_GOOD : C_WARN); }
    if (contract.type == CT_IFR) kv("MINIMUMS", fmt("%s AGL", fmtAlt(std::max(60.f, wx.cloudBase - g_world.airports[contract.to].elev - 30.f)).c_str()), result.belowMinimumsUnaligned ? C_BAD : C_TEXT);
    if (contract.type == CT_NIGHT) kv("LDG LIGHT", landingLight ? "ON" : "OFF", landingLight ? C_GOOD : C_WARN);
  }
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
  // autopilot / autoland
  header(px, py, vw, "AUTOPILOT"); py += 24 * s;
  g_ren.text(px, py, 13 * s, ellipsize(plane.apOn ? plane.apStatus : std::string("OFF  -  Z engages"), vw, 13 * s), plane.apOn ? C_GOOD : C_DIM, e); py += 20 * s;
  {
    float bw = 30 * s, bh = 26 * s;
    if (button(px, py, bw, bh, "<")) cycleApDest(-1);
    if (button(px + vw - bw, py, bw, bh, ">")) cycleApDest(1);
    std::string tl = "PICK AN AIRPORT";
    if (apDest >= 0) { const Airport& A = g_world.airports[apDest]; tl = fmt("%s  %.0f km", A.code, length(vec3(A.x - plane.pos.x, 0, A.z - plane.pos.z)) / 1000.f); }
    g_ren.text(px + vw * 0.5f, py + 5 * s, 15 * s, ellipsize(tl, vw - 2 * bw - 12 * s, 15 * s), apDest >= 0 ? AP_CYAN : C_DIM, e, 1, true);
    py += bh + 6 * s;
    bool active = plane.apOn && plane.apMode == Plane::AP_APPR && plane.apAirport == apDest;
    bool can = apDest >= 0 && !plane.onGround && !active;
    if (button(px, py, vw * 0.64f, 28 * s, active ? "AUTOLAND ACTIVE" : "ENGAGE AUTOLAND", can, can)) engageAutopilot();
    if (button(px + vw * 0.68f, py, vw * 0.32f, 28 * s, "CLEAR", apDest >= 0 && !active)) apDest = -1;
    py += 38 * s;
  }
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
  g_ren.text(sx + sw * 0.5f, sy + sh - 42 * s, 11.5f * s, "TAB / CLICK PICK   ENTER AUTOLAND", C_DIM, 0.8f * e, 1, false);
  g_ren.text(sx + sw * 0.5f, sy + sh - 24 * s, 11.5f * s, "WHEEL ZOOM   R FIT   N CLOSE", C_DIM, 0.8f * e, 1, false);
}

// ------------------------------------------------------------------ hidden research menu (U + I on the main menu)
void Game::drawPause() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.55f);
  if (settingsFromPause) {
    float pw = std::min((settingsPage == 1 ? 940 : 760) * s, W - 40 * s);
    panel(W * 0.5f - pw * 0.5f, 60 * s, pw, H - 120 * s, 0.95f);
    drawSettings(W * 0.5f - pw * 0.5f + 24 * s, 80 * s, pw - 48 * s, H - 160 * s - (settingsPage == 1 ? 0 : 50 * s));
    if (settingsPage == 0 && button(W * 0.5f + pw * 0.5f - 180 * s, H - 120 * s - 58 * s, 150 * s, 42 * s, "Back", true, true)) settingsFromPause = false;
    if (settingsPage == 1 && button(W * 0.5f - pw * 0.5f + 24 * s, H - 120 * s - 4 * s - 48 * s, 150 * s, 36 * s, "Back", true, true)) { settingsFromPause = false; bindCapture = -1; }
    return;
  }
  float pw = 300 * s, ph = std::min(392 * s, H - 20 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  g_ren.text(x + 30 * s, y + 24 * s, 28 * s, "Paused", C_TEXT, 1);
  float by = y + 80 * s, bw = 240 * s, bh = 46 * s;
  if (button(x + 30 * s, by, bw, bh, "Resume", true, true)) paused = false;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Restart flight")) { if (researchFlight) launchResearch(); else { Contract c = contract; startFlight(c, specIdx, source); } }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw * 0.48f, bh, "Settings")) { settingsFromPause = true; settingsPage = 0; }
  if (button(x + 30 * s + bw * 0.52f, by, bw * 0.48f, bh, "Controls")) { settingsFromPause = true; settingsPage = 1; }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw * 0.48f, bh, showRadio ? "Hide radio" : "Radio")) showRadio = !showRadio;
  if (button(x + 30 * s + bw * 0.52f, by, bw * 0.48f, bh, uiHidden ? "Show flight UI" : "Hide flight UI")) uiHidden = !uiHidden;   // (also LB+RB held)
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, researchFlight ? "End research flight" : "Abandon flight")) endFlight(false, researchFlight ? "" : "Abandoned flight", OUT_ABANDONED);
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
}

void Game::drawDebrief() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.35f);
  float pw = std::min(720 * s, W - 40 * s), ph = std::min(760 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
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
  // The foot of the panel, from the buttons up, stays put: the unsaved notice, the campaign's end, a new licence and
  // the total. What is above it - the flight data, the coaching and the settlement's lines - scrolls (mouse wheel, the
  // right stick) in the room that is left, as one body: at a large UI scale on a small screen that room may hold only
  // a few lines (the review of v3.24.0, R9, and of v3.31.0, U4: the total's divider ran through the first fee)
  const bool campaignEnd = career.finished && lastSuccess && contract.story && contract.id == g_story.back().id;
  const auto campaignLines = campaignEnd ? wrap("You've completed the Solace Express campaign. Congratulations, Captain!", pw - 60 * s, 18 * s) : std::vector<std::string>();
  const bool newLic = career.license > licenseBefore;
  const float campY = (commitBlocked() ? y + ph - 108 * s : y + ph - 76 * s) - campaignLines.size() * 24 * s;
  const float licY = campY - (newLic ? 34 * s : 0.f), totalY = licY - 34 * s;
  int total = 0;
  for (auto& l : payout) total += l.amount;
  const float bodyTop = py, bodyBot = std::max(bodyTop + 24 * s, totalY - 12 * s);
  static float payScroll = 0.f, bodyH = 0.f; static std::string payFor;
  const std::string payKey = debriefTitle + "|" + contract.id + "|" + std::to_string(payout.size()) + "|" + std::to_string(total);
  if (payKey != payFor) { payFor = payKey; payScroll = 0.f; }
  const float payMax = std::max(0.f, bodyH - (bodyBot - bodyTop));
  if (hovered(x, bodyTop, pw, bodyBot - bodyTop) && in.wheel != 0) { payScroll -= in.wheel * 48.f * s; in.wheel = 0; }
  payScroll = std::clamp(payScroll, 0.f, payMax);
  g_ren.uiClip(x, bodyTop - 2 * s, x + pw, bodyBot);
  py = bodyTop - payScroll;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 230 * s, py, 16 * s, ellipsize(v, pw - 290 * s, 16 * s), C_TEXT, 1); py += 24 * s; };
  header(px, py, pw - 60 * s, "FLIGHT DATA"); py += 24 * s;
  if (result.landed) row("Touchdown", fmt("%.0f fpm", touchdownFpm));
  row("Flight time", fmt("%d:%02d", (int)flightClock / 60, (int)flightClock % 60));
  row("Max G", fmt("%.2f", plane.maxG));
  row("Max bank", fmt("%.0f deg", result.maxBank));
  row("Fuel used", fmt("%.0f kg", result.fuelUsedKg));
  if (!coaching.empty()) {   // one coaching point from the arrival (Game::landingCoaching)
    auto cl = wrap(coaching, pw - 60 * s - 30 * s, 15 * s);
    if (cl.size() > 2) { cl.resize(2); cl[1] = ellipsize(cl[1] + " ...", pw - 90 * s, 15 * s); }
    py += 4 * s;
    g_ren.rect(px, py, 3 * s, cl.size() * 20 * s, C_ACCENT, 0.8f);
    for (auto& l : cl) { g_ren.text(px + 12 * s, py, 15 * s, l, C_ACCENT, 0.95f); py += 20 * s; }
  }
  py += 10 * s;
  header(px, py, pw - 60 * s, "SETTLEMENT"); py += 24 * s;
  for (auto& l : payout) {
    float vw = g_ren.text(x + pw - 30 * s, py, 16 * s, fmtMoney(l.amount), l.amount >= 0 ? C_GOOD : C_BAD, 1, 2);
    g_ren.text(px, py, 16 * s, ellipsize(l.label, pw - 80 * s - vw, 16 * s), C_TEXT, 1);
    py += 24 * s;
  }
  bodyH = py + payScroll - bodyTop;
  g_ren.uiClipOff();
  if (payMax > 0.f) {   // a thin bar at the panel's edge: where the view is in the body
    const float trackH = bodyBot - bodyTop, barH = std::max(20 * s, trackH * trackH / (trackH + payMax));
    g_ren.rect(x + pw - 18 * s, bodyTop, 3 * s, trackH, C_ACCENT, 0.12f);
    g_ren.rect(x + pw - 18 * s, bodyTop + (trackH - barH) * payScroll / payMax, 3 * s, barH, C_ACCENT, 0.7f);
  }
  g_ren.rect(px, totalY - 8 * s, pw - 60 * s, 1 * s, C_ACCENT, 0.5f);
  g_ren.text(px, totalY, 18 * s, "Total", C_TEXT, 1);
  g_ren.text(x + pw - 30 * s, totalY, 18 * s, fmtMoney(total), total >= 0 ? C_GOOD : C_BAD, 1, 2);
  if (newLic) fitText(px, licY, pw - 60 * s, 22 * s, 14 * s, std::string("NEW LICENCE: ") + licenseName(career.license), C_WARN);
  { float cy = campY; for (auto& l : campaignLines) { g_ren.text(px, cy, 18 * s, l, C_ACCENT, 1); cy += 24 * s; } }
  if (commitBlocked()) {   // the settlement above is what will be saved; until it is, the career stands as before the flight
    fitText(px, y + ph - 100 * s, pw - 60 * s, 20 * s, 13 * s, "NOT SAVED YET: " + saveWhy, C_BAD);
    if (button(x + 30 * s, y + ph - 66 * s, 200 * s, 46 * s, "Retry save")) retryCommit();
  }
  if (button(x + pw - 230 * s, y + ph - 66 * s, 200 * s, 46 * s, "Continue", true, true) || in.pressed[K_ENTER]) { retryCommit(); screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; }
  const bool jobWaits = career.job && career.job->state == Career::JobState::RECOVERY;
  if (!lastSuccess && !commitBlocked() && button(x + 30 * s, y + ph - 66 * s, 200 * s, 46 * s, jobWaits ? "Continue job" : "Try again")) retryFromDebrief();
}
